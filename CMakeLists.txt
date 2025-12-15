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

# =============================================================================
# CTP 支持选项
# =============================================================================
option(ENABLE_CTP "Enable CTP market data adapter" OFF)

if(ENABLE_CTP)
    message(STATUS "CTP support: ENABLED")
    add_definitions(-DENABLE_CTP)
    
    # CTP SDK 路径
    if(APPLE)
        set(CTP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/ctp/macos/include")
        set(CTP_FRAMEWORK_DIR "${CMAKE_SOURCE_DIR}/third_party/ctp/macos/framework")
    elseif(UNIX)
        set(CTP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/ctp/linux/include")
        set(CTP_LIB_DIR "${CMAKE_SOURCE_DIR}/third_party/ctp/linux/lib")
    endif()
else()
    message(STATUS "CTP support: DISABLED (use -DENABLE_CTP=ON to enable)")
endif()

# 添加 include 目录，以便编译器找到头文件
include_directories(include)

# CTP 头文件目录
if(ENABLE_CTP)
    include_directories(${CTP_INCLUDE_DIR})
endif()

# 1. 创建一个静态库 "fix_engine" 用于存放核心逻辑
set(FIX_ENGINE_SOURCES
    src/fix/session.cpp
    src/fix/session_manager.cpp
    src/core/connection.cpp
    src/fix/fix_frame_decoder.cpp
    src/base/config.cpp
    src/app/simulation_app.cpp
    src/app/matching_engine.cpp
    src/app/order_book.cpp
    src/market/mock_md_adapter.cpp
)

# CTP 源文件（条件编译）
if(ENABLE_CTP)
    list(APPEND FIX_ENGINE_SOURCES src/market/ctp_md_adapter.cpp)
endif()

add_library(fix_engine STATIC ${FIX_ENGINE_SOURCES})

# 为 fix_engine 库指定头文件搜索路径
target_include_directories(fix_engine PUBLIC include)

# CTP 链接（条件编译）
if(ENABLE_CTP)
    if(APPLE)
        # 直接链接动态库文件，避免符号链接问题
        set(CTP_DYLIB "${CTP_FRAMEWORK_DIR}/thostmduserapi_se.framework/Versions/A/thostmduserapi_se")
        target_link_libraries(fix_engine PUBLIC "${CTP_DYLIB}")
        set_target_properties(fix_engine PROPERTIES
            BUILD_RPATH "${CTP_FRAMEWORK_DIR}/thostmduserapi_se.framework/Versions/A"
        )
    elseif(UNIX)
        target_link_libraries(fix_engine PUBLIC
            "${CTP_LIB_DIR}/libthostmduserapi_se.so"
        )
        set_target_properties(fix_engine PROPERTIES
            BUILD_RPATH "${CTP_LIB_DIR}"
        )
    endif()
endif()

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

# 将 config.ini 文件复制到构建目录
configure_file(
    "${CMAKE_SOURCE_DIR}/config.ini"
    "${CMAKE_BINARY_DIR}/config.ini"
    COPYONLY
)



# 打印出可执行文件的路径，方便用户
message(STATUS "Server executable: ${CMAKE_BINARY_DIR}/fix_server")
message(STATUS "Client executable: ${CMAKE_BINARY_DIR}/fix_client")
message(STATUS "Config file: ${CMAKE_BINARY_DIR}/config.ini")
