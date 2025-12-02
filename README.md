# FIX 4.0 Demo

一个基于 C++17 的 FIX 4.0 协议演示项目，包含服务端和客户端实现。

## 功能

- 完整的 FIX 4.0 会话层：Logon、Heartbeat、TestRequest、Logout
- 基于 Reactor 模式的事件驱动网络层（epoll/kqueue）
- 连接绑定线程模型，同一连接的操作串行执行
- 时间轮定时器管理心跳和超时检测

## 局限

- 仅实现会话层消息，没有业务消息（如 NewOrderSingle、ExecutionReport）
- 没有消息持久化和重传机制
- 没有加密支持
- 单机部署，不支持集群

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                        应用层                                │
│                  FixServer / Client                         │
├─────────────────────────────────────────────────────────────┤
│                        协议层                                │
│         Session (状态机) + FixCodec + FrameDecoder          │
├─────────────────────────────────────────────────────────────┤
│                        网络层                                │
│              Reactor + Connection + ThreadPool              │
├─────────────────────────────────────────────────────────────┤
│                        基础设施                              │
│                 TimingWheel + Config                        │
└─────────────────────────────────────────────────────────────┘
```

## 线程模型

```
Reactor 线程                     工作线程池
     │                              │
     ├─ epoll_wait()                │
     │                              │
     ├─ fd=5 可读 ──────────────────> 线程 A (fd=5 绑定)
     │                              │  ├─ read()
     │                              │  ├─ 解析 FIX 消息
     │                              │  └─ 状态机处理
     │                              │
     ├─ fd=8 可读 ──────────────────> 线程 B (fd=8 绑定)
     │                              │
     └─ 定时器触发 ─> tick() ───────> 派发心跳检查任务
```

Reactor 线程只负责检测 IO 事件，实际的读写和业务处理在工作线程中执行。每个连接绑定到固定的工作线程，避免锁竞争。

## 构建

```bash
mkdir build && cd build
cmake ..
make
```

## 运行

启动服务端：
```bash
./fix_server [线程数] [端口]
# 例如: ./fix_server 4 9000
```

启动客户端：
```bash
./fix_client [服务器IP] [端口]
# 例如: ./fix_client 127.0.0.1 9000
```

客户端连接后会自动发送 Logon，输入 `logout` 可优雅断开。

## 配置

`config.ini` 包含服务端口、心跳间隔、时间轮参数等配置项，详见文件注释。

## 依赖

- C++17
- CMake 3.10+
- Linux (epoll) 或 macOS (kqueue)

第三方库（已包含在项目中）：
- [moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue) - 无锁队列
