# 测试 CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

project(FixDemo_Tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 代码覆盖率选项
option(ENABLE_COVERAGE "Enable code coverage" OFF)

if(ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled")
    # 添加 -fprofile-update=atomic 解决多线程测试的覆盖率计数器竞争问题
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage -fprofile-update=atomic")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# 添加父目录的 include 路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 创建测试库（复用主项目的源文件）
add_library(fix_engine_test STATIC
    ../src/fix/session.cpp
    ../src/core/connection.cpp
    ../src/fix/fix_frame_decoder.cpp
    ../src/base/config.cpp
    ../src/app/simulation_app.cpp
    ../src/app/matching_engine.cpp
    ../src/app/order_book.cpp
    ../src/market/mock_md_adapter.cpp
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
    unit/test_application.cpp
    unit/test_md_adapter.cpp
    unit/test_order_book.cpp
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

# 覆盖率报告生成 target
if(ENABLE_COVERAGE)
    find_program(LCOV lcov)
    find_program(GENHTML genhtml)
    
    if(LCOV AND GENHTML)
        add_custom_target(coverage
            COMMAND ${LCOV} --directory . --zerocounters
            COMMAND $<TARGET_FILE:unit_tests>
            COMMAND ${LCOV} --directory . --capture --output-file coverage.info
            COMMAND ${LCOV} --remove coverage.info '/usr/*' '*/catch2/*' '*/tests/*' --output-file coverage.info
            COMMAND ${GENHTML} coverage.info --output-directory coverage_report
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            DEPENDS unit_tests
            COMMENT "Generating code coverage report..."
        )
        message(STATUS "Coverage target available: make coverage")
    else()
        message(WARNING "lcov/genhtml not found, coverage target disabled")
    endif()
endif()
