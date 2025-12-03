# 测试

## 单元测试

使用 [Catch2](https://github.com/catchorg/Catch2) v2 测试框架。

### 构建

```bash
cmake -B tests/build -S tests
cmake --build tests/build
```

### 运行

```bash
./tests/build/unit_tests
```

### 测试内容

| 文件 | 测试对象 |
|------|---------|
| `unit/test_fix_codec.cpp` | FIX 消息编解码 |
| `unit/test_frame_decoder.cpp` | FIX 帧解析器 |
| `unit/test_timing_wheel.cpp` | 时间轮定时器 |

### 运行特定测试

```bash
# 只运行 codec 相关测试
./tests/build/unit_tests [codec]

# 只运行 timing_wheel 相关测试
./tests/build/unit_tests [timing_wheel]

# 列出所有测试
./tests/build/unit_tests --list-tests
```
