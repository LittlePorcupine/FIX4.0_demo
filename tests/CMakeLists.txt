# 测试专用的CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(FixDemo_Tests CXX)

# 设置 C++ 标准为 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加推荐的编译器警告选项
add_compile_options(-Wall -Wextra -pedantic)

# 设置测试可执行文件的输出目录为当前tests/build目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/build)

# 添加父目录的include路径
include_directories(../include)

# 创建fix_engine静态库（测试版本）
add_library(fix_engine_test STATIC
    ../src/fix/session.cpp
    ../src/core/connection.cpp
    ../src/fix/fix_frame_decoder.cpp
    ../src/base/config.cpp
)

# 为测试库指定头文件搜索路径
target_include_directories(fix_engine_test PUBLIC ../include)

# 在类 Unix 系统上链接 pthread
if (UNIX)
    find_package(Threads REQUIRED)
endif()

# 添加测试可执行文件
add_executable(test_fix_frame_decoder test_fix_frame_decoder.cpp)
target_link_libraries(test_fix_frame_decoder PRIVATE fix_engine_test)
if (UNIX)
    target_link_libraries(test_fix_frame_decoder PRIVATE Threads::Threads)
endif()

add_executable(test_safe_queue test_safe_queue.cpp)
target_link_libraries(test_safe_queue PRIVATE fix_engine_test)
if (UNIX)
    target_link_libraries(test_safe_queue PRIVATE Threads::Threads)
endif()

# 将config.ini文件复制到测试构建目录
configure_file(
    "${CMAKE_SOURCE_DIR}/../config.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/config.ini"
    COPYONLY
)

# 打印测试可执行文件路径
message(STATUS "Test fix_frame_decoder executable: ${CMAKE_CURRENT_SOURCE_DIR}/build/test_fix_frame_decoder")
message(STATUS "Test safe_queue executable: ${CMAKE_CURRENT_SOURCE_DIR}/build/test_safe_queue")
message(STATUS "Test config file: ${CMAKE_CURRENT_SOURCE_DIR}/build/config.ini")