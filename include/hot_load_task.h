#pragma once

#include <string>

class HotLoadTask {
    friend class HotLoader; // Allow HotLoader to access private members
public:
    HotLoadTask(const std::string& file)
        : _file(normalize_path(file)), _watch_descriptor(-1) {}

    virtual ~HotLoadTask() = default;

    const std::string& watch_file() const {
        return _file;
    }

    virtual void on_reload() {}

    static std::string normalize_path(const std::string& input_path);

private:
    int watch_descriptor() const {
        return _watch_descriptor;
    }

    void set_watch_descriptor(int wd) {
        _watch_descriptor = wd;
    }

private:
    std::string _file;
    int _watch_descriptor; // Inotify watch descriptor
};