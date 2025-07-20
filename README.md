# FIX 4.0 引擎与交易服务器/客户端 Demo

本项目是一个基于 C++17 实现的、跨平台的、功能完整的金融信息交换协议 (FIX) 4.0 引擎。它包含一个高性能的交易服务器和一个交互式的客户端，旨在演示现代 C++ 在网络编程和金融科技领域的应用。

## 主要特性

- **跨平台支持**: 可在 Linux (epoll) 和 macOS (kqueue) 上编译和运行。
- **现代 C++ 实现**: 完全使用 C++17 标准，充分利用了智能指针、原子操作、Lambda 表达式等现代特性。
- **高性能网络核心**: 基于 Reactor 设计模式，实现了非阻塞 I/O 和事件驱动，能够高效处理大量并发连接。
- **健壮的 FIX 协议层**:
    - 完整的 FIX 消息编解码器，自动处理 `BodyLength` 和 `CheckSum` 字段。
    - 严格的消息校验（序列号、字段格式等）。
    - 实现了会话管理的核心逻辑，包括登录、登出、心跳维持和连接活性检测 (`TestRequest`)。
- **清晰的模块化架构**: 项目被清晰地划分为基础库、网络核心、FIX 协议和应用层。
- **优雅的线程管理**: 使用线程池将 I/O 线程与工作线程分离，避免业务逻辑阻塞网络核心。
- **完善的构建系统**: 提供了一个配置良好、易于理解的 `CMakeLists.txt` 文件。

## 技术栈

- **语言**: C++17
- **构建系统**: CMake (3.10+)
- **核心库**:
    - 网络 I/O: `epoll` (Linux), `kqueue` (macOS)
    - 多线程: `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`
    - 数据结构: `SafeQueue` (线程安全队列), `TimingWheel` (时间轮)

## 项目架构

项目采用分层架构，职责清晰：

1.  **`base/` (基础库)**: 提供了与业务无关的通用组件，如 `ThreadPool`, `SafeQueue`, `TimingWheel`。
2.  **`core/` (核心层)**: 封装了底层的网络 I/O，提供 `Reactor` 事件循环和 `Connection` 连接抽象。
3.  **`fix/` (协议层)**: 实现了 FIX 协议的全部细节，包括 `FixCodec`, `Session` 状态机等。
4.  **`server/` & `client/` (应用层)**: 将底层模块组合起来，分别实现了 `FixServer` 和可交互的 `Client`。

## 如何构建和运行

### 前提条件

- C++17 兼容的编译器 (GCC, Clang)
- CMake (版本 3.10 或更高)
- Git

### 构建步骤

```bash
# 1. 克隆仓库
# git clone https://github.com/LittlePorcupine/FIX4.0_demo.git
# cd FIX4.0_demo

# 2. 创建 build 目录并运行 CMake
mkdir build
cd build
cmake ..

# 3. 编译项目
make
```

### 运行

构建成功后，`build` 目录下会生成两个可执行文件：`fix_server` 和 `fix_client`。

1.  **启动服务器**:
    打开一个终端，运行：
    ```bash
    ./build/fix_server
    ```
    服务器将开始在 `9000` 端口监听。

2.  **启动客户端**:
    打开**另一个**终端，运行：
    ```bash
    ./build/fix_client
    ```
客户端将自动连接到服务器，并进入交互模式。您可以输入 `logout` 来正常关闭连接。


