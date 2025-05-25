cmake_minimum_required(VERSION 3.10)

project(HotLoader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(example example.cpp src/hot_load_task.cpp src/hot_loader.cpp)
target_include_directories(example PUBLIC include)
target_link_libraries(example PUBLIC pthread)