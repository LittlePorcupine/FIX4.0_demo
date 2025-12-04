#!/bin/bash
# 代码覆盖率生成脚本
# 用法: ./tests/scripts/coverage.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/tests/build-coverage"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== Code Coverage Generation ===${NC}"

# 检查依赖
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo -e "${RED}Error: $1 not found${NC}"
        echo "Please install it:"
        echo "  macOS: brew install lcov"
        echo "  Ubuntu: sudo apt-get install lcov"
        exit 1
    fi
}

check_tool lcov
check_tool genhtml

# 清理旧的构建目录
echo -e "${YELLOW}Cleaning old build...${NC}"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# 配置并构建（启用覆盖率）
echo -e "${YELLOW}Configuring with coverage enabled...${NC}"
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT/tests" -DENABLE_COVERAGE=ON

echo -e "${YELLOW}Building...${NC}"
cmake --build "$BUILD_DIR" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# 运行测试
echo -e "${YELLOW}Running tests...${NC}"
cd "$BUILD_DIR"
./unit_tests

# 生成覆盖率报告
echo -e "${YELLOW}Generating coverage report...${NC}"

# 收集覆盖率数据
lcov --directory . --capture --output-file coverage.info --ignore-errors inconsistent

# 过滤掉系统头文件、测试文件和第三方库
lcov --remove coverage.info \
    '/usr/*' \
    '/Library/*' \
    '*/catch2/*' \
    '*/tests/*' \
    '*/include/base/concurrentqueue.h' \
    '*/include/base/blockingconcurrentqueue.h' \
    '*/include/base/lightweightsemaphore.h' \
    --output-file coverage.info \
    --ignore-errors unused

# 生成 HTML 报告
genhtml coverage.info --output-directory coverage_report

# 显示摘要
echo ""
echo -e "${GREEN}=== Coverage Summary ===${NC}"
lcov --summary coverage.info 2>/dev/null || true

echo ""
echo -e "${GREEN}HTML report generated at:${NC}"
echo "  $BUILD_DIR/coverage_report/index.html"
echo ""
echo "Open in browser:"
echo "  open $BUILD_DIR/coverage_report/index.html"
