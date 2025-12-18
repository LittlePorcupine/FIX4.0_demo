# CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(FixDemo CXX)

# 设置 C++ 标准为 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # 强制使用标准 C++，而非 GNU++ 扩展

# 生成 compile_commands.json 供 IDE 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 添加推荐的编译器警告选项，以提高代码质量
add_compile_options(-Wall -Wextra -pedantic)

# =============================================================================
# CTP 支持选项
# =============================================================================
option(ENABLE_CTP "Enable CTP market data adapter" ON)

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

# =============================================================================
# SQLite 依赖
# =============================================================================
find_package(SQLite3 REQUIRED)
message(STATUS "SQLite3 found: ${SQLite3_VERSION}")

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
    src/app/engine/matching_engine.cpp
    src/app/engine/order_book.cpp
    src/app/manager/account_manager.cpp
    src/app/manager/position_manager.cpp
    src/app/manager/instrument_manager.cpp
    src/app/manager/risk_manager.cpp
    src/market/mock_md_adapter.cpp
    src/storage/sqlite_store.cpp
)

# CTP 源文件（条件编译）
if(ENABLE_CTP)
    list(APPEND FIX_ENGINE_SOURCES 
        src/market/ctp_md_adapter.cpp
        src/market/ctp_trader_adapter.cpp
    )
endif()

add_library(fix_engine STATIC ${FIX_ENGINE_SOURCES})

# 为 fix_engine 库指定头文件搜索路径
target_include_directories(fix_engine PUBLIC include)

# 链接 SQLite
target_link_libraries(fix_engine PUBLIC SQLite::SQLite3)

# CTP 链接（条件编译）
if(ENABLE_CTP)
    if(APPLE)
        # 直接链接动态库文件，避免符号链接问题
        set(CTP_MD_DYLIB "${CTP_FRAMEWORK_DIR}/thostmduserapi_se.framework/Versions/A/thostmduserapi_se")
        set(CTP_TRADER_DYLIB "${CTP_FRAMEWORK_DIR}/thosttraderapi_se.framework/Versions/A/thosttraderapi_se")
        target_link_libraries(fix_engine PUBLIC "${CTP_MD_DYLIB}" "${CTP_TRADER_DYLIB}")
        set_target_properties(fix_engine PROPERTIES
            BUILD_RPATH "${CTP_FRAMEWORK_DIR}/thostmduserapi_se.framework/Versions/A;${CTP_FRAMEWORK_DIR}/thosttraderapi_se.framework/Versions/A"
        )
    elseif(UNIX)
        # Linux: 使用 IMPORTED 库方式链接，确保路径正确传递给链接器
        set(CTP_MD_SO "${CMAKE_SOURCE_DIR}/third_party/ctp/linux/lib/thostmduserapi_se.so")
        set(CTP_TRADER_SO "${CMAKE_SOURCE_DIR}/third_party/ctp/linux/lib/thosttraderapi_se.so")
        
        # 检查库文件是否存在
        if(NOT EXISTS "${CTP_MD_SO}")
            message(FATAL_ERROR "CTP MD library not found: ${CTP_MD_SO}")
        endif()
        if(NOT EXISTS "${CTP_TRADER_SO}")
            message(FATAL_ERROR "CTP Trader library not found: ${CTP_TRADER_SO}")
        endif()
        
        # 创建 IMPORTED 库目标
        add_library(ctp_md SHARED IMPORTED)
        set_target_properties(ctp_md PROPERTIES
            IMPORTED_LOCATION "${CTP_MD_SO}"
        )
        
        add_library(ctp_trader SHARED IMPORTED)
        set_target_properties(ctp_trader PROPERTIES
            IMPORTED_LOCATION "${CTP_TRADER_SO}"
        )
        
        target_link_libraries(fix_engine PUBLIC ctp_md ctp_trader)
        set_target_properties(fix_engine PROPERTIES
            BUILD_RPATH "${CMAKE_SOURCE_DIR}/third_party/ctp/linux/lib"
            INSTALL_RPATH "${CMAKE_SOURCE_DIR}/third_party/ctp/linux/lib"
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


# =============================================================================
# FTXUI 依赖（用于 TUI 客户端）
# =============================================================================
include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# 添加客户端可执行文件
add_executable(fix_client 
    src/client/main.cpp 
    src/client/client_app.cpp
    src/client/client_state.cpp
    src/client/tui/app.cpp
    src/client/tui/components/header.cpp
    src/client/tui/components/order_panel.cpp
    src/client/tui/components/position_panel.cpp
    src/client/tui/components/account_panel.cpp
    src/client/tui/components/search_box.cpp
)
# 链接 fix_engine 库和 FTXUI 到客户端
target_link_libraries(fix_client PRIVATE 
    fix_engine 
    ftxui::screen 
    ftxui::dom 
    ftxui::component
)
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
