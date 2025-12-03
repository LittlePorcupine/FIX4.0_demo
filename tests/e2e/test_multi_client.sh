#!/bin/bash
# 端到端测试：验证多客户端并发连接
# 测试服务端同时处理多个客户端的能力，包括心跳交换

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

SERVER_BIN="$BUILD_DIR/fix_server"
CLIENT_BIN="$BUILD_DIR/fix_client"
SERVER_LOG="/tmp/fix_server_multi_test.log"
TEST_CONFIG="$SCRIPT_DIR/config_test.ini"
NUM_CLIENTS=5

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

declare -a CLIENT_PIDS
declare -a CLIENT_LOGS

cleanup() {
    echo "Cleaning up..."
    for pid in "${CLIENT_PIDS[@]}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SERVER_LOG"
    for log in "${CLIENT_LOGS[@]}"; do
        rm -f "$log"
    done
}

trap cleanup EXIT

fail() {
    echo -e "${RED}FAIL: $1${NC}"
    echo "=== Server Log (last 50 lines) ==="
    tail -50 "$SERVER_LOG" 2>/dev/null || echo "(no log)"
    exit 1
}

pass() {
    echo -e "${GREEN}PASS: $1${NC}"
}

# 检查可执行文件
if [ ! -x "$SERVER_BIN" ]; then
    fail "Server binary not found: $SERVER_BIN"
fi
if [ ! -x "$CLIENT_BIN" ]; then
    fail "Client binary not found: $CLIENT_BIN"
fi

# 复制测试配置到 build 目录
cp "$TEST_CONFIG" "$BUILD_DIR/config.ini"

echo "=== E2E Test: Multi-Client ($NUM_CLIENTS clients, with Heartbeat) ==="
echo "Using test config with 3-second heartbeat interval"

# 启动服务端
echo "Starting server on port 9998..."
cd "$BUILD_DIR"
"$SERVER_BIN" 4 9998 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    fail "Server failed to start"
fi

if ! grep -q "Server listening on port 9998" "$SERVER_LOG"; then
    fail "Server not listening"
fi
pass "Server started with 4 worker threads"

# 启动多个客户端
echo "Starting $NUM_CLIENTS clients..."
for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    CLIENT_LOGS[$i]="/tmp/fix_client_${i}_test.log"
    "$CLIENT_BIN" 127.0.0.1 9998 < /dev/null > "${CLIENT_LOGS[$i]}" 2>&1 &
    CLIENT_PIDS[$i]=$!
done

# 等待所有客户端建立会话
echo "Waiting for all sessions to establish..."
TIMEOUT=15
ALL_ESTABLISHED=false

while [ $TIMEOUT -gt 0 ]; do
    ESTABLISHED_COUNT=0
    for i in $(seq 0 $((NUM_CLIENTS - 1))); do
        if grep -q "State changing from <LogonSent> to <Established>" "${CLIENT_LOGS[$i]}" 2>/dev/null; then
            ESTABLISHED_COUNT=$((ESTABLISHED_COUNT + 1))
        fi
    done
    
    if [ $ESTABLISHED_COUNT -eq $NUM_CLIENTS ]; then
        ALL_ESTABLISHED=true
        break
    fi
    
    sleep 1
    TIMEOUT=$((TIMEOUT - 1))
done

if [ "$ALL_ESTABLISHED" != "true" ]; then
    fail "Not all clients established session (only $ESTABLISHED_COUNT/$NUM_CLIENTS)"
fi
pass "All $NUM_CLIENTS clients established sessions"

# 验证服务端接受了所有连接
ACCEPTED_COUNT=$(grep -c "Accepted new connection" "$SERVER_LOG" || echo "0")
if [ "$ACCEPTED_COUNT" -lt "$NUM_CLIENTS" ]; then
    fail "Server only accepted $ACCEPTED_COUNT connections (expected $NUM_CLIENTS)"
fi
pass "Server accepted all $NUM_CLIENTS connections"

# 验证连接分布到不同的工作线程
THREAD_0=$(grep -c "bindded to thread 0" "$SERVER_LOG" || echo "0")
THREAD_1=$(grep -c "bindded to thread 1" "$SERVER_LOG" || echo "0")
THREAD_2=$(grep -c "bindded to thread 2" "$SERVER_LOG" || echo "0")
THREAD_3=$(grep -c "bindded to thread 3" "$SERVER_LOG" || echo "0")

echo "Thread distribution: T0=$THREAD_0, T1=$THREAD_1, T2=$THREAD_2, T3=$THREAD_3"

THREADS_USED=0
[ "$THREAD_0" -gt 0 ] && THREADS_USED=$((THREADS_USED + 1))
[ "$THREAD_1" -gt 0 ] && THREADS_USED=$((THREADS_USED + 1))
[ "$THREAD_2" -gt 0 ] && THREADS_USED=$((THREADS_USED + 1))
[ "$THREAD_3" -gt 0 ] && THREADS_USED=$((THREADS_USED + 1))

if [ "$THREADS_USED" -lt 2 ]; then
    echo "Warning: Only $THREADS_USED thread(s) used"
else
    pass "Connections distributed across $THREADS_USED worker threads"
fi

# 等待心跳交换（心跳间隔 3 秒，等待 5 秒）
echo "Waiting for heartbeat exchange (5 seconds)..."
sleep 5

# 验证所有客户端都有心跳交换
HB_SUCCESS=0
for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    if grep -q "35=0" "${CLIENT_LOGS[$i]}" 2>/dev/null; then
        HB_SUCCESS=$((HB_SUCCESS + 1))
    fi
done

if [ "$HB_SUCCESS" -lt "$NUM_CLIENTS" ]; then
    fail "Only $HB_SUCCESS/$NUM_CLIENTS clients exchanged heartbeats"
fi
pass "All $NUM_CLIENTS clients exchanged heartbeats"

# 统计服务端心跳消息
SERVER_HB_SENT=$(grep -c ">>> SEND.*35=0" "$SERVER_LOG" || echo "0")
SERVER_HB_RECV=$(grep -c "<<< RECV.*35=0" "$SERVER_LOG" || echo "0")
echo "Server heartbeats: sent=$SERVER_HB_SENT, received=$SERVER_HB_RECV"

if [ "$SERVER_HB_SENT" -lt "$NUM_CLIENTS" ]; then
    fail "Server sent fewer heartbeats than expected"
fi
pass "Server heartbeat exchange verified"

# 终止所有客户端
echo "Terminating clients..."
for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    if [ -n "${CLIENT_PIDS[$i]}" ] && kill -0 "${CLIENT_PIDS[$i]}" 2>/dev/null; then
        kill "${CLIENT_PIDS[$i]}" 2>/dev/null || true
        wait "${CLIENT_PIDS[$i]}" 2>/dev/null || true
    fi
done

# 等待服务端清理所有会话
echo "Waiting for server to clean up..."
sleep 3

DESTROYED_COUNT=$(grep -c "Session (SERVER -> CLIENT) destroyed" "$SERVER_LOG" || echo "0")
if [ "$DESTROYED_COUNT" -lt "$NUM_CLIENTS" ]; then
    fail "Server only cleaned up $DESTROYED_COUNT sessions (expected $NUM_CLIENTS)"
fi
pass "Server cleaned up all $NUM_CLIENTS sessions"

echo ""
echo -e "${GREEN}=== Multi-client test passed ===${NC}"
