# FIX 4.0 模拟交易系统

基于 C++17 实现的 FIX 4.0 协议模拟交易系统，支持期货模拟交易。

## 功能

### 服务端 (fix_server)
- FIX 4.0 会话层：Logon、Heartbeat、TestRequest、Logout、消息重传
- 业务消息：NewOrderSingle (D)、OrderCancelRequest (F)、ExecutionReport (8)
- 自定义消息：资金查询 (U1/U2)、持仓查询 (U3/U4)、账户推送 (U5)、持仓推送 (U6)、合约搜索 (U7/U8)
- 行情驱动撮合引擎，支持限价单和市价单
- 账户管理：资金、保证金、持仓盈亏计算
- 风控检查：保证金、持仓限额、下单数量
- SimNow 行情源集成（可选）

### 客户端 (fix_client)
- 终端 TUI 界面（基于 FTXUI）
- 实时显示账户资金、持仓、订单
- 合约搜索（支持模糊匹配）
- 下单、撤单操作
- 订单本地持久化

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                      应用层 (Application)                    │
│         SimulationApp / ClientApp / TUI                     │
├─────────────────────────────────────────────────────────────┤
│                      业务层 (Business)                       │
│    MatchingEngine / AccountManager / PositionManager        │
│    InstrumentManager / RiskManager                          │
├─────────────────────────────────────────────────────────────┤
│                      协议层 (Protocol)                       │
│         Session (状态机) + SessionManager + FixCodec        │
├─────────────────────────────────────────────────────────────┤
│                      网络层 (Network)                        │
│              Reactor + Connection + ThreadPool              │
├─────────────────────────────────────────────────────────────┤
│                      基础设施 (Infrastructure)               │
│            TimingWheel + Config + Logger + Store            │
└─────────────────────────────────────────────────────────────┘
```

## 构建

依赖：
- C++17 编译器 (GCC 8+ / Clang 7+ / MSVC 2019+)
- CMake 3.16+
- SQLite3
- Linux (epoll) 或 macOS (kqueue)

```bash
mkdir build && cd build
cmake ..
make -j4
```

## 运行

### 服务端

```bash
# 基本启动
./fix_server

# 指定端口和线程数
./fix_server -p 9000 -t 4

# 连接 SimNow 行情源
./fix_server -s simnow.ini

# 查看帮助
./fix_server -h
```

### 客户端

```bash
# 连接服务端
./fix_client -h 127.0.0.1 -p 9000 -u USER001

# 查看帮助
./fix_client --help
```

TUI 操作：
- `Tab` - 切换焦点
- `F5` / `R` - 刷新数据
- `Q` - 退出

## 配置

### config.ini
服务端配置，包含端口、心跳间隔、风控参数等。

### simnow.ini
SimNow 行情源配置（可选），需要 SimNow 账号。

```ini
[ctp]
broker_id = 9999
user_id = YOUR_USER_ID
password = YOUR_PASSWORD
md_front = tcp://182.254.243.31:40011
td_front = tcp://182.254.243.31:40001
```

## 测试

```bash
# 单元测试
cd tests && mkdir build && cd build
cmake .. && make
./unit_tests

# E2E 测试
cd tests/e2e
./test_trading.sh
./test_open_close.sh
```

## 目录结构

```
├── include/           # 头文件
│   ├── app/          # 应用层
│   ├── base/         # 基础设施
│   ├── core/         # 网络核心
│   ├── fix/          # FIX 协议
│   ├── market/       # 行情适配器
│   └── storage/      # 存储
├── src/              # 源文件
│   ├── server/       # 服务端入口
│   ├── client/       # 客户端 + TUI
│   └── ...
├── tests/            # 测试
│   ├── unit/         # 单元测试
│   └── e2e/          # 端到端测试
└── third_party/      # 第三方库
```

## 局限

- 仅支持单机部署
- 无消息加密
- 行情撮合为模拟逻辑，非真实交易所撮合规则
- SimNow 行情源需要在交易时段使用


