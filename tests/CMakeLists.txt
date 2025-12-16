# 测试 CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

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

# =============================================================================
# RapidCheck 属性测试库 (通过 FetchContent 获取)
# =============================================================================
include(FetchContent)

FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG        ff6af6fc683159deb51c543b065eba14dfcf329b  # 锁定到稳定commit
)

# 设置 RapidCheck 选项：启用 Catch2 集成
set(RC_ENABLE_CATCH ON CACHE BOOL "Enable Catch2 integration")

FetchContent_MakeAvailable(rapidcheck)
message(STATUS "RapidCheck property-based testing library: ENABLED (via FetchContent)")

# SQLite 依赖
find_package(SQLite3 REQUIRED)
message(STATUS "SQLite3 found: ${SQLite3_VERSION}")

# 创建测试库（复用主项目的源文件）
add_library(fix_engine_test STATIC
    ../src/fix/session.cpp
    ../src/fix/session_manager.cpp
    ../src/core/connection.cpp
    ../src/fix/fix_frame_decoder.cpp
    ../src/base/config.cpp
    ../src/app/simulation_app.cpp
    ../src/app/matching_engine.cpp
    ../src/app/order_book.cpp
    ../src/app/instrument_manager.cpp
    ../src/app/account_manager.cpp
    ../src/app/position_manager.cpp
    ../src/app/risk_manager.cpp
    ../src/market/mock_md_adapter.cpp
    ../src/storage/sqlite_store.cpp
)
target_include_directories(fix_engine_test PUBLIC ../include)
target_link_libraries(fix_engine_test PUBLIC SQLite::SQLite3)

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
    unit/test_session_manager.cpp
    unit/test_sqlite_store.cpp
    # RapidCheck 属性测试
    unit/test_rapidcheck_integration.cpp
    unit/test_account.cpp
    unit/test_position.cpp
    unit/test_instrument.cpp
    unit/test_instrument_manager.cpp
    unit/test_account_manager.cpp
    unit/test_position_manager.cpp
    unit/test_risk_manager.cpp
)

target_link_libraries(unit_tests PRIVATE fix_engine_test)
if (UNIX)
    target_link_libraries(unit_tests PRIVATE Threads::Threads)
endif()

# 链接 RapidCheck 库（用于属性测试）
target_link_libraries(unit_tests PRIVATE rapidcheck)

# 添加 RapidCheck Catch2 集成头文件路径 (FetchContent 下载位置)
target_include_directories(unit_tests PRIVATE
    ${rapidcheck_SOURCE_DIR}/extras/catch/include
)

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
