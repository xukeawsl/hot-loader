# hot-loader

支持对 Linux 文件进行监控，文件写入并退出后触发用户注册的热加载任务

## 使用

参考 `example.cpp`

```cpp
#include <iostream>

#include "hot_loader.h" // step1: 包含头文件

// step2: 继承 HotLoadTask，实现 on_reload 重载逻辑
class MyHotLoadTask : public HotLoadTask {
public:
    MyHotLoadTask(const std::string& file) : HotLoadTask(file) {}

    void on_reload() override {
        // Custom reload logic
        std::cout << "Reloading task for file: " << watch_file() << std::endl;
    }
};

int main() {
    MyHotLoadTask task("../config1.json");

    // step3: 初始化 HotLoader
    if (HotLoader::instance().init() != 0) {
        std::cerr << "Failed to initialize HotLoader" << std::endl;
        return 1;
    }

    // step4: 可以在 run 之前注册 Task，DOESNT_ONW_TASK 表示 HotLoader 不会在注销任务时 delete
    int ret = HotLoader::instance().register_task(&task, HotLoader::DOESNT_OWN_TASK);
    if (ret != 0) {
        std::cerr << "Failed to register task: " << ret << std::endl;
        return 1;
    }

    // step4: 运行 HotLoader 进行监控
    if (HotLoader::instance().run() != 0) {
        std::cerr << "Failed to start HotLoader" << std::endl;
        return 1;
    }
    std::cout << "HotLoader is running. Press Enter to stop..." << std::endl;

    std::thread([]() {
        // 运行后注册 Task，线程安全
        HotLoader::instance().register_task(new MyHotLoadTask("../config2.cpp"), HotLoader::OWN_TASK);
        std::this_thread::sleep_for(std::chrono::seconds(10));
        // step5: 可以主动注销 Task
        if (HotLoader::instance().unregister_task("../test.cpp") != 0) {
            std::cerr << "Failed to unregister task" << std::endl;
        } else {
            std::cout << "Task unregistered successfully." << std::endl;
        }
    }).detach();

    std::cin.get(); // Wait for user input to stop
    // step6: 停止 HotLoader，会注销所有 Task
    HotLoader::instance().stop();
    std::cout << "HotLoader stopped." << std::endl;

    return 0;
}
```