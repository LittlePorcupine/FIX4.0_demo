#!/bin/bash

# 测试运行脚本
# 自动构建并运行所有测试

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 构建测试
echo "构建测试..."
./build.sh

# 进入构建目录
cd build

echo ""
echo "========================================="
echo "运行 FIX Frame Decoder 测试"
echo "========================================="
./test_fix_frame_decoder

echo ""
echo "========================================="
echo "运行 Safe Queue 测试"
echo "========================================="
./test_safe_queue

echo ""
echo "========================================="
echo "所有测试完成！"
echo "========================================="