#!/bin/bash
# 端到端测试：验证完整的 FIX 会话流程
# 测试 Logon -> Heartbeat -> 断开 流程

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

SERVER_BIN="$BUILD_DIR/fix_server"
CLIENT_BIN="$BUILD_DIR/fix_client"
SERVER_LOG="/tmp/fix_server_test.log"
CLIENT_LOG="/tmp/fix_client_test.log"
TEST_CONFIG="$SCRIPT_DIR/config_test.ini"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

cleanup() {
    echo "Cleaning up..."
    if [ -n "$CLIENT_PID" ] && kill -0 "$CLIENT_PID" 2>/dev/null; then
        kill "$CLIENT_PID" 2>/dev/null || true
        wait "$CLIENT_PID" 2>/dev/null || true
    fi
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SERVER_LOG" "$CLIENT_LOG"
    # 恢复原始配置文件
    cp "$PROJECT_ROOT/config.ini" "$BUILD_DIR/config.ini" 2>/dev/null || true
}

trap cleanup EXIT

fail() {
    echo -e "${RED}FAIL: $1${NC}"
    echo "=== Server Log ==="
    cat "$SERVER_LOG" 2>/dev/null || echo "(no log)"
    echo "=== Client Log ==="
    cat "$CLIENT_LOG" 2>/dev/null || echo "(no log)"
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

echo "=== E2E Test: Session Flow (with Heartbeat) ==="
echo "Using test config with 3-second heartbeat interval"

# 启动服务端
echo "Starting server on port 9999..."
cd "$BUILD_DIR"
"$SERVER_BIN" 2 9999 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    fail "Server failed to start"
fi

if ! grep -q "Server listening on port 9999" "$SERVER_LOG"; then
    fail "Server not listening"
fi
pass "Server started"

# 启动客户端
echo "Starting client..."
"$CLIENT_BIN" 127.0.0.1 9999 < /dev/null > "$CLIENT_LOG" 2>&1 &
CLIENT_PID=$!

# 等待会话建立
echo "Waiting for session to establish..."
TIMEOUT=10
while [ $TIMEOUT -gt 0 ]; do
    if grep -q "State changing from <LogonSent> to <Established>" "$CLIENT_LOG" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
        fail "Client exited unexpectedly"
    fi
    sleep 1
    TIMEOUT=$((TIMEOUT - 1))
done

if [ $TIMEOUT -eq 0 ]; then
    fail "Session establishment timed out"
fi
pass "Client session established"

# 验证服务端也建立了会话
if ! grep -q "Session (SERVER): State changing from <Disconnected> to <Established>" "$SERVER_LOG"; then
    fail "Server session not established"
fi
pass "Server session established"

# 验证 Logon 消息交换
if ! grep -q "35=A" "$CLIENT_LOG"; then
    fail "Logon message not found"
fi
pass "Logon messages exchanged"

# 等待心跳交换（心跳间隔 3 秒，等待 5 秒确保至少一次心跳）
echo "Waiting for heartbeat exchange (5 seconds)..."
sleep 5

# 验证客户端发送了心跳
if ! grep -q "35=0" "$CLIENT_LOG"; then
    fail "Client did not send Heartbeat"
fi
pass "Client sent Heartbeat"

# 验证服务端发送了心跳
if ! grep -q "35=0" "$SERVER_LOG"; then
    fail "Server did not send Heartbeat"
fi
pass "Server sent Heartbeat"

# 验证心跳被正确接收
CLIENT_HB_RECV=$(grep -c "<<< RECV.*35=0" "$CLIENT_LOG" || echo "0")
SERVER_HB_RECV=$(grep -c "<<< RECV.*35=0" "$SERVER_LOG" || echo "0")

if [ "$CLIENT_HB_RECV" -lt 1 ]; then
    fail "Client did not receive any Heartbeat"
fi
pass "Client received $CLIENT_HB_RECV Heartbeat(s)"

if [ "$SERVER_HB_RECV" -lt 1 ]; then
    fail "Server did not receive any Heartbeat"
fi
pass "Server received $SERVER_HB_RECV Heartbeat(s)"

# 终止客户端
echo "Terminating client..."
kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true
CLIENT_PID=""

# 等待服务端检测到断开
sleep 2

# 验证服务端清理
if ! grep -q "Session (SERVER -> CLIENT) destroyed" "$SERVER_LOG"; then
    fail "Server session not cleaned up"
fi
pass "Server session cleaned up"

echo ""
echo -e "${GREEN}=== All E2E tests passed ===${NC}"
