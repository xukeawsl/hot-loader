#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <climits>
#include <unordered_map>

#include <sys/inotify.h>

#include "hot_load_task.h"

class HotLoader {
public:
    constexpr static int kMaxEventCount = 1024; // Maximum number of events to handle at once
    constexpr static int kEventBufferSize = 1024 * (sizeof(struct inotify_event) + NAME_MAX + 1); // Buffer size for inotify events
    constexpr static int kEpollTimeout = 1000; // Timeout for epoll_wait, -1 means wait indefinitely

    constexpr static int kWatchEventMask = IN_CLOSE_WRITE | IN_IGNORED;

    enum OwnerShip {
        OWN_TASK, // HotLoader owns the task and will delete it
        DOESNT_OWN_TASK // HotLoader does not own the task, caller is responsible for deletion
    };

    static HotLoader& instance() {
        static HotLoader instance;
        return instance;
    }

    int init();

    int register_task(HotLoadTask* task, OwnerShip ownership);

    int unregister_task(HotLoadTask* task) {
        if (!task) {
            return -1; // Invalid task pointer
        }
        return unregister_task(task->watch_file());
    }

    int unregister_task(const std::string& file);

    int run();

    void stop();

private:
    HotLoader() = default;
    HotLoader(const HotLoader&) = delete;
    HotLoader& operator=(const HotLoader&) = delete;
    HotLoader(HotLoader&&) = delete;
    HotLoader& operator=(HotLoader&&) = delete;
    ~HotLoader();

    void work_loop();

    void restart_stopped_tasks();

    void rewatch_task(HotLoadTask* task);

private:
    std::mutex _mutex; // Mutex to protect access to shared resources
    std::unordered_map<std::string, HotLoadTask*> _tasks; // Maps file paths to HotLoadTask pointers
    std::unordered_map<std::string, OwnerShip> _ownerships; // Maps file paths to ownership status
    std::unordered_map<int, HotLoadTask*> _watch_descriptors; // Maps inotify watch descriptors to HotLoadTask pointers
    int _inotify_fd = -1; // File descriptor for inotify
    int _epoll_fd = -1;   // File descriptor for epoll
    std::atomic<bool> _running = false; // Flag to control the running state
    std::thread _worker_thread; // Worker thread for monitoring file changes
};