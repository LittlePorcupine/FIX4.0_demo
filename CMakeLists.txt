# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(FixDemo CXX)

# 设置 C++ 标准为 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 强制使用标准 C++，而非 GNU++ 扩展

# 添加推荐的编译器警告选项，以提高代码质量
add_compile_options(-Wall -Wextra -pedantic)

# 添加 include 目录，以便编译器找到头文件
include_directories(include)

# 1. 创建一个静态库 "fix_engine" 用于存放核心逻辑
add_library(fix_engine STATIC
    src/session.cpp
    src/connection.cpp
)

# 为 fix_engine 库指定头文件搜索路径
target_include_directories(fix_engine PUBLIC include)

# 在类 Unix 系统上链接 pthread
if (UNIX)
    find_package(Threads REQUIRED)
endif()

# 添加服务端可执行文件
add_executable(fix_server src/server/main.cpp src/server/server.cpp)
# 链接 fix_engine 库到服务端
target_link_libraries(fix_server PRIVATE fix_engine)
if (UNIX)
    target_link_libraries(fix_server PRIVATE Threads::Threads)
endif()


# 添加客户端可执行文件
add_executable(fix_client src/client/main.cpp src/client/client.cpp)
# 链接 fix_engine 库到客户端
target_link_libraries(fix_client PRIVATE fix_engine)
if (UNIX)
    target_link_libraries(fix_client PRIVATE Threads::Threads)
endif()

# 打印出可执行文件的路径，方便用户
message(STATUS "Server executable: ${CMAKE_BINARY_DIR}/fix_server")
message(STATUS "Client executable: ${CMAKE_BINARY_DIR}/fix_client")