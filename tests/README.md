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
| `unit/test_config.cpp` | 配置文件解析 |
| `unit/test_thread_pool.cpp` | 线程池 |
| `unit/test_session.cpp` | Session 状态机和消息处理 |

### 运行特定测试

```bash
# 只运行 codec 相关测试
./tests/build/unit_tests [codec]

# 只运行 timing_wheel 相关测试
./tests/build/unit_tests [timing_wheel]

# 只运行 config 相关测试
./tests/build/unit_tests [config]

# 只运行 thread_pool 相关测试
./tests/build/unit_tests [thread_pool]

# 只运行 session 相关测试
./tests/build/unit_tests [session]

# 只运行边界测试
./tests/build/unit_tests [edge]

# 列出所有测试
./tests/build/unit_tests --list-tests
```

## 端到端测试

验证完整的客户端-服务端会话流程。

### 运行

```bash
# 需要先构建主项目
cmake -B build .
cmake --build build

# 运行端到端测试
./tests/e2e/test_session_flow.sh    # 单客户端会话流程
./tests/e2e/test_multi_client.sh    # 多客户端并发
```

### 测试内容

**test_session_flow.sh**
- 服务端启动和监听
- 客户端连接和 Logon 握手
- 会话建立确认
- 心跳消息发送和接收
- 连接断开和资源清理

**test_multi_client.sh**
- 5 个客户端同时连接
- 所有会话并发建立
- 连接分布到多个工作线程
- 所有客户端心跳交换
- 所有会话正确清理

测试使用 `config_test.ini` 配置文件，心跳间隔设为 3 秒以加速测试。

## 代码覆盖率

### 本地生成覆盖率报告

需要先安装 lcov：
```bash
# macOS
brew install lcov

# Ubuntu
sudo apt-get install lcov
```

运行覆盖率脚本：
```bash
./tests/scripts/coverage.sh
```

报告会生成在 `tests/build-coverage/coverage_report/index.html`。

### 手动构建（带覆盖率）

```bash
cmake -B tests/build -S tests -DENABLE_COVERAGE=ON
cmake --build tests/build
./tests/build/unit_tests
cd tests/build && make coverage
```

## CI

项目使用 GitHub Actions 进行持续集成，在 Ubuntu 和 macOS 上运行所有测试。

CI 会自动生成代码覆盖率报告并上传到 Codecov。覆盖率报告也会作为 artifact 保存，可在 Actions 页面下载。

配置文件：`.github/workflows/ci.yml`
