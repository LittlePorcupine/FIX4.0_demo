#!/bin/bash

# 测试构建脚本
# 用法: ./build.sh [clean]

set -e  # 遇到错误时退出

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 如果传入clean参数，清理构建目录
if [ "$1" = "clean" ]; then
    echo "清理测试构建目录..."
    rm -rf build
    echo "清理完成"
    exit 0
fi

# 创建构建目录
mkdir -p build
cd build

# 运行cmake配置
echo "配置测试构建..."
cmake ..

# 编译
echo "编译测试..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "测试构建完成！"
echo "可执行文件位置："
echo "  - $(pwd)/test_fix_frame_decoder"
echo "  - $(pwd)/test_safe_queue"
echo ""
echo "运行测试："
echo "  ./test_fix_frame_decoder"
echo "  ./test_safe_queue"