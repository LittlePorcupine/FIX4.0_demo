# 测试

## 测试框架

- **单元测试**: [Catch2](https://github.com/catchorg/Catch2) v2
- **属性测试**: [RapidCheck](https://github.com/emil-e/rapidcheck)

## 构建

```bash
# 构建测试（RapidCheck 会通过 CMake FetchContent 自动下载）
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
| `unit/test_rapidcheck_integration.cpp` | RapidCheck 属性测试集成验证 |

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

# 只运行 RapidCheck 属性测试
./tests/build/unit_tests [rapidcheck]

# 只运行边界测试
./tests/build/unit_tests [edge]

# 列出所有测试
./tests/build/unit_tests --list-tests
```

## 属性测试 (Property-Based Testing)

属性测试使用 RapidCheck 框架，与 Catch2 集成。属性测试会自动生成大量随机测试数据来验证代码的正确性属性。

### 编写属性测试

```cpp
#include "../catch2/catch.hpp"
#include <rapidcheck.h>
#include <rapidcheck/catch.h>

// **Feature: paper-trading-system, Property 2: 限价单撮合正确性**
// **Validates: Requirements 4.1, 4.2, 4.3, 4.4**
TEST_CASE("限价单撮合正确性", "[matching][property]") {
    rc::prop("买单价格>=卖一价时应成交",
        [](int buyPrice, int askPrice) {
            // 属性测试逻辑
            bool shouldMatch = (buyPrice >= askPrice);
            // ... 验证撮合结果
            return shouldMatch == actualMatch;
        });
}
```

### 运行属性测试

```bash
# 运行所有属性测试
./tests/build/unit_tests [rapidcheck]

# 运行特定属性测试
./tests/build/unit_tests "限价单撮合正确性"
```

### 配置

RapidCheck 默认运行 100 次迭代。可以通过环境变量配置：

```bash
# 设置迭代次数
RC_PARAMS="max_success=200" ./tests/build/unit_tests [rapidcheck]

# 设置随机种子（用于复现失败）
RC_PARAMS="seed=12345" ./tests/build/unit_tests [rapidcheck]
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
./tests/e2e/test_multi_client.sh    # 多客户端并发会话
./tests/e2e/test_trading.sh         # 单客户端交易流程
./tests/e2e/test_multi_trading.sh   # 多客户端并发交易
```

### 测试内容

**test_session_flow.sh** - 会话层测试
- 服务端启动和监听
- 客户端连接和 Logon 握手
- 会话建立确认
- 心跳消息发送和接收
- 连接断开和资源清理

**test_multi_client.sh** - 并发会话测试
- 5 个客户端同时连接
- 所有会话并发建立
- 连接分布到多个工作线程
- 所有客户端心跳交换
- 所有会话正确清理

**test_trading.sh** - 单客户端交易测试
- NewOrderSingle 消息发送和接收
- 订单添加到 OrderBook
- 买卖订单撮合成交
- OrderCancelRequest 撤单处理
- ExecutionReport 回报发送
- 订单状态验证 (New/PartiallyFilled/Filled/Canceled)

**test_multi_trading.sh** - 多客户端并发交易测试
- 3 个客户端同时发送订单
- 跨客户端订单撮合
- 并发撤单处理
- ExecutionReport 正确路由到各客户端
- 多种订单状态并存验证

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
