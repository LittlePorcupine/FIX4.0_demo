#!/bin/bash
# 端到端测试：验证完整交易流程
# 测试 Logon -> NewOrderSingle -> 撮合 -> ExecutionReport -> 撤单 流程

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

SERVER_BIN="$BUILD_DIR/fix_server"
SERVER_LOG="/tmp/fix_server_trading_test.log"
CLIENT_LOG="/tmp/fix_client_trading_test.log"
TEST_CONFIG="$SCRIPT_DIR/config_test.ini"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

# SOH 字符
SOH=$'\x01'

cleanup() {
    echo "Cleaning up..."
    if [ -n "$NC_PID" ] && kill -0 "$NC_PID" 2>/dev/null; then
        kill "$NC_PID" 2>/dev/null || true
        wait "$NC_PID" 2>/dev/null || true
    fi
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SERVER_LOG" "$CLIENT_LOG"
    cp "$PROJECT_ROOT/config.ini" "$BUILD_DIR/config.ini" 2>/dev/null || true
}

trap cleanup EXIT

fail() {
    echo -e "${RED}FAIL: $1${NC}"
    echo "=== Server Log (last 100 lines) ==="
    tail -100 "$SERVER_LOG" 2>/dev/null || echo "(no log)"
    exit 1
}

pass() {
    echo -e "${GREEN}PASS: $1${NC}"
}

info() {
    echo -e "${YELLOW}INFO: $1${NC}"
}

# ============================================================================
# FIX 消息构造函数
# ============================================================================

calculate_checksum() {
    local data="$1"
    local sum=0
    for ((i=0; i<${#data}; i++)); do
        sum=$((sum + $(printf '%d' "'${data:$i:1}")))
    done
    printf "%03d" $((sum % 256))
}

build_fix_message() {
    local body="$1"
    local body_len=${#body}
    local prefix="8=FIX.4.0${SOH}9=${body_len}${SOH}${body}"
    local checksum=$(calculate_checksum "$prefix")
    echo -n "${prefix}10=${checksum}${SOH}"
}

build_logon() {
    local sender="$1" target="$2" seq_num="$3" heartbeat="$4"
    local timestamp=$(date -u +"%Y%m%d-%H:%M:%S")
    local body="35=A${SOH}49=${sender}${SOH}56=${target}${SOH}34=${seq_num}${SOH}52=${timestamp}${SOH}98=0${SOH}108=${heartbeat}${SOH}"
    build_fix_message "$body"
}

build_new_order_single() {
    local sender="$1" target="$2" seq_num="$3" cl_ord_id="$4"
    local symbol="$5" side="$6" qty="$7" price="$8"
    local timestamp=$(date -u +"%Y%m%d-%H:%M:%S")
    local body="35=D${SOH}49=${sender}${SOH}56=${target}${SOH}34=${seq_num}${SOH}52=${timestamp}${SOH}11=${cl_ord_id}${SOH}21=1${SOH}55=${symbol}${SOH}54=${side}${SOH}38=${qty}${SOH}44=${price}${SOH}40=2${SOH}59=0${SOH}60=${timestamp}${SOH}"
    build_fix_message "$body"
}

build_cancel_request() {
    local sender="$1" target="$2" seq_num="$3"
    local cl_ord_id="$4" orig_cl_ord_id="$5" symbol="$6" side="$7"
    local timestamp=$(date -u +"%Y%m%d-%H:%M:%S")
    local body="35=F${SOH}49=${sender}${SOH}56=${target}${SOH}34=${seq_num}${SOH}52=${timestamp}${SOH}11=${cl_ord_id}${SOH}41=${orig_cl_ord_id}${SOH}55=${symbol}${SOH}54=${side}${SOH}60=${timestamp}${SOH}125=F${SOH}"
    build_fix_message "$body"
}

# ============================================================================
# 测试开始
# ============================================================================

if [ ! -x "$SERVER_BIN" ]; then
    fail "Server binary not found: $SERVER_BIN"
fi

cp "$TEST_CONFIG" "$BUILD_DIR/config.ini"

echo "=== E2E Test: Trading Flow ==="
echo "Testing: Logon -> Orders -> Matching -> Cancel"

# 启动服务端
echo "Starting server on port 9995..."
cd "$BUILD_DIR"
"$SERVER_BIN" 2 9995 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    fail "Server failed to start"
fi
if ! grep -q "Server listening on port 9995" "$SERVER_LOG"; then
    fail "Server not listening"
fi
pass "Server started"

# ============================================================================
# 测试场景：订单撮合 + 撤单
# ============================================================================

info "Sending trading messages..."

{
    # 1. Logon
    echo -n "$(build_logon "CLIENT" "SERVER" "1" "30")"
    sleep 1
    
    # 2. 买单 100 @ 150.50 (AAPL)
    echo -n "$(build_new_order_single "CLIENT" "SERVER" "2" "ORD001" "AAPL" "1" "100" "150.50")"
    sleep 0.5
    
    # 3. 卖单 50 @ 150.50 (AAPL) - 应与买单撮合
    echo -n "$(build_new_order_single "CLIENT" "SERVER" "3" "ORD002" "AAPL" "2" "50" "150.50")"
    sleep 0.5
    
    # 4. 买单 200 @ 250.00 (TSLA) - 不会撮合
    echo -n "$(build_new_order_single "CLIENT" "SERVER" "4" "ORD003" "TSLA" "1" "200" "250.00")"
    sleep 0.5
    
    # 5. 撤销 TSLA 买单
    echo -n "$(build_cancel_request "CLIENT" "SERVER" "5" "CXL001" "ORD003" "TSLA" "1")"
    sleep 2
    
} | nc -w 5 127.0.0.1 9995 > "$CLIENT_LOG" 2>&1 &
NC_PID=$!

sleep 6

# ============================================================================
# 验证结果
# ============================================================================

# 1. 会话建立
if ! grep -q "State changing from <Disconnected> to <Established>" "$SERVER_LOG"; then
    fail "Session not established"
fi
pass "Session established"

# 2. 收到订单
ORDER_COUNT=$(grep -c "Received business message: MsgType=D" "$SERVER_LOG" || echo "0")
if [ "$ORDER_COUNT" -lt 3 ]; then
    fail "Expected 3 orders, received $ORDER_COUNT"
fi
pass "Received $ORDER_COUNT NewOrderSingle messages"

# 3. 订单添加到订单簿
if ! grep -q "added to bids\|added to asks" "$SERVER_LOG"; then
    fail "Orders not added to OrderBook"
fi
pass "Orders added to OrderBook"

# 4. AAPL 撮合
if grep -q "Trade:.*AAPL" "$SERVER_LOG"; then
    MATCH_LOG=$(grep "Trade:.*AAPL" "$SERVER_LOG" | head -1)
    pass "AAPL orders matched"
    info "Match: $MATCH_LOG"
else
    fail "AAPL orders did not match"
fi

# 5. 收到撤单请求
if ! grep -q "Received business message: MsgType=F" "$SERVER_LOG"; then
    fail "CancelRequest not received"
fi
pass "CancelRequest received"

# 6. ExecutionReport 发送
ER_COUNT=$(grep -c "Sending ExecutionReport" "$SERVER_LOG" || echo "0")
if [ "$ER_COUNT" -lt 4 ]; then
    fail "Expected at least 4 ExecutionReports, got $ER_COUNT"
fi
pass "ExecutionReports sent: $ER_COUNT"

# 7. 验证各种订单状态
if grep -q "OrdStatus=0" "$SERVER_LOG"; then
    pass "Found OrdStatus=New (0)"
fi
if grep -q "OrdStatus=1\|OrdStatus=2" "$SERVER_LOG"; then
    pass "Found OrdStatus=Fill (1 or 2)"
fi
if grep -q "OrdStatus=4" "$SERVER_LOG"; then
    pass "Found OrdStatus=Canceled (4)"
fi

# 终止 nc
if [ -n "$NC_PID" ] && kill -0 "$NC_PID" 2>/dev/null; then
    kill "$NC_PID" 2>/dev/null || true
    wait "$NC_PID" 2>/dev/null || true
fi
NC_PID=""

sleep 1

echo ""
echo -e "${GREEN}=== Trading E2E test passed ===${NC}"
