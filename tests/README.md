# 测试说明

本目录包含项目的所有单元测试，测试与主项目分离管理。

## 目录结构

```
tests/
├── CMakeLists.txt          # 测试专用的CMake配置
├── build.sh               # 测试构建脚本
├── run_tests.sh           # 测试运行脚本
├── test_fix_frame_decoder.cpp  # FIX帧解码器测试
├── test_safe_queue.cpp    # 安全队列测试
├── build/                 # 测试构建输出目录（不提交到git）
└── README.md              # 本文件
```

## 快速开始

### 构建测试
```bash
cd tests
./build.sh
```

### 运行所有测试
```bash
cd tests
./run_tests.sh
```

### 清理构建
```bash
cd tests
./build.sh clean
```

### 手动运行单个测试
```bash
cd tests/build
./test_fix_frame_decoder
./test_safe_queue
```

## 注意事项

1. 测试构建产物位于 `tests/build/` 目录下，该目录不会被git跟踪
2. 测试使用独立的CMake配置，不会影响主项目的构建
3. 测试会自动链接主项目的fix_engine库
4. config.ini文件会自动复制到测试构建目录

## 添加新测试

1. 在tests目录下创建新的测试源文件
2. 在tests/CMakeLists.txt中添加相应的可执行文件配置
3. 在run_tests.sh中添加新测试的运行命令