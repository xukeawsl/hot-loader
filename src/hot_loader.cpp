#include "hot_loader.h"

#include <filesystem>
#include <functional>

#include <sys/epoll.h>
#include <unistd.h>

namespace fs = std::filesystem;

HotLoader::~HotLoader() {
    stop();
}

void HotLoader::stop() {
    _running.store(false); // Set the running flag to false

    if (_worker_thread.joinable()) {
        _worker_thread.join(); // Wait for the worker thread to finish
    }

    std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety before cleanup
    // Clean up all registered tasks
    for (auto& [_, task] : _tasks) {
        unregister_task(task);
    }

    if (_inotify_fd >= 0) {
        close(_inotify_fd); // Close inotify file descriptor
        _inotify_fd = -1;
    }

    if (_epoll_fd >= 0) {
        close(_epoll_fd); // Close epoll file descriptor
        _epoll_fd = -1;
    }
}

int HotLoader::init() {
    _inotify_fd = inotify_init1(IN_NONBLOCK);
    if (_inotify_fd < 0) {
        return -1; // Failed to initialize inotify
    }

    _epoll_fd = epoll_create1(0);
    if (_epoll_fd < 0) {
        close(_inotify_fd);
        return -2; // Failed to create epoll instance
    }

    return 0;
}

int HotLoader::run() {
    _running.store(true); // Set the running flag to true

    _worker_thread = std::thread(std::bind(&HotLoader::work_loop, this));

    return 0; // Success
}

int HotLoader::register_task(HotLoadTask* task, OwnerShip ownership) {
    if (!task) {
        return -1; // Invalid task pointer
    }

    std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety
    
    const std::string& file = task->watch_file();
    if (_tasks.find(file) != _tasks.end()) {
        return -2; // Task already registered
    }

    _tasks[file] = task;
    _ownerships[file] = ownership;

    // Register the file with inotify
    int wd = inotify_add_watch(_inotify_fd, file.c_str(), kWatchEventMask);
    if (wd < 0) {
        return -3; // Failed to add watch
    }

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Edge-triggered mode
    event.data.fd = _inotify_fd; // Associate the inotify fd with the event
    if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _inotify_fd, &event) < 0) {
        inotify_rm_watch(_inotify_fd, wd); // Clean up if epoll_ctl fails
        return -4; // Failed to add inotify fd to epoll
    }

    task->set_watch_descriptor(wd); // Set the watch descriptor in the task

    _watch_descriptors[wd] = task;

    return 0; // Success
}

int HotLoader::unregister_task(const std::string& file) {
    std::string normalize_file = HotLoadTask::normalize_path(file);
    if (normalize_file.empty()) {
        return -1; // Invalid file path
    }

    std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety

    auto it = _tasks.find(normalize_file);
    if (it == _tasks.end()) {
        return -2; // Task not found
    }

    HotLoadTask* task = it->second;
    _tasks.erase(it);

    if (task->watch_descriptor() >= 0) {
        // Remove the watch from inotify
        inotify_rm_watch(_inotify_fd, task->watch_descriptor());
        _watch_descriptors.erase(task->watch_descriptor());
    }
    task->set_watch_descriptor(-1); // Reset the watch descriptor

    if (_ownerships[normalize_file] == OWN_TASK) {
        delete task; // Delete the task if HotLoader owns it
    }
    _ownerships.erase(normalize_file); // Remove ownership information

    return 0; // Success
}

void HotLoader::work_loop() {
    static struct epoll_event events[kMaxEventCount];
    static char event_buf[kEventBufferSize];

    while (_running.load()) {
        restart_stopped_tasks();

        int n_ready = epoll_wait(_epoll_fd, events, kMaxEventCount, kEpollTimeout);
        if (n_ready < 0) {
            if (errno == EINTR) {
                continue; // Interrupted, retry
            }
            break; // Error occurred
        }

        if (n_ready == 0) {
            continue; // No events ready, continue the loop
        }

        std::unordered_map<int, uint32_t> event_masks;

        for (int i = 0; i < n_ready; ++i) {
            if (events[i].data.fd == _inotify_fd) {
                while (true) {
                    ssize_t len = read(_inotify_fd, event_buf, kEventBufferSize);
                    if (len < 0) {
                        if (errno == EAGAIN || errno == EINTR) {
                            break; // No more events or interrupted, break the loop
                        }
                        return; // Error occurred
                    }

                    for (char* ptr = event_buf; ptr < event_buf + len;) {
                        struct inotify_event* event = reinterpret_cast<struct inotify_event*>(ptr);
                        ptr += sizeof(struct inotify_event) + event->len;

                        event_masks[event->wd] |= event->mask; // Aggregate event masks
                    }
                }
            }
        }

        // Process the aggregated events
        std::lock_guard<std::mutex> lock(_mutex); // Lock to ensure thread safety

        for (const auto& [wd, mask] : event_masks) {
            auto it = _watch_descriptors.find(wd);
            if (it != _watch_descriptors.end()) {
                HotLoadTask* task = it->second;

                if (mask & IN_IGNORED) {
                    rewatch_task(task);
                } else {
                    task->on_reload();
                }
            }
        }
    }
}

void HotLoader::restart_stopped_tasks() {
    std::lock_guard<std::mutex> lock(_mutex); // Ensure thread safety

    for (auto& [_, task] : _tasks) {
        if (task->watch_descriptor() < 0 &&
            fs::exists(task->watch_file())) {
            
            // Task is stopped, try to restart it
            int wd = inotify_add_watch(_inotify_fd, task->watch_file().c_str(), 
                                       kWatchEventMask);
            if (wd >= 0) {
                task->on_reload();

                task->set_watch_descriptor(wd);
                _watch_descriptors[wd] = task; // Update the watch descriptor map
            }
        }
    }
}

void HotLoader::rewatch_task(HotLoadTask* task) {
    if (!task) {
        return;
    }

    if (task->watch_descriptor() >= 0) {
        inotify_rm_watch(_inotify_fd, task->watch_descriptor());
    }

    if (!fs::exists(task->watch_file())) {
        task->set_watch_descriptor(-1); // Reset if file does not exist
        return;
    }

    int wd = inotify_add_watch(_inotify_fd, task->watch_file().c_str(), 
                               kWatchEventMask);
    if (wd >= 0) {
        task->on_reload();

        task->set_watch_descriptor(wd);
        _watch_descriptors[wd] = task; // Update the watch descriptor map
    } else {
        task->set_watch_descriptor(-1); // Reset if rewatch fails
    }
}
