# 测试 CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

project(FixDemo_Tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加父目录的 include 路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 创建测试库（复用主项目的源文件）
add_library(fix_engine_test STATIC
    ../src/fix/session.cpp
    ../src/core/connection.cpp
    ../src/fix/fix_frame_decoder.cpp
    ../src/base/config.cpp
)
target_include_directories(fix_engine_test PUBLIC ../include)

# 链接 pthread
if (UNIX)
    find_package(Threads REQUIRED)
endif()

# 创建测试可执行文件
add_executable(unit_tests
    main.cpp
    unit/test_fix_codec.cpp
    unit/test_frame_decoder.cpp
    unit/test_timing_wheel.cpp
    unit/test_config.cpp
    unit/test_thread_pool.cpp
    unit/test_session.cpp
)

target_link_libraries(unit_tests PRIVATE fix_engine_test)
if (UNIX)
    target_link_libraries(unit_tests PRIVATE Threads::Threads)
endif()

# 复制 config.ini 到测试目录
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../config.ini"
    "${CMAKE_CURRENT_BINARY_DIR}/config.ini"
    COPYONLY
)

# 添加 CTest 支持
enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)

message(STATUS "Test executable: ${CMAKE_CURRENT_BINARY_DIR}/unit_tests")
