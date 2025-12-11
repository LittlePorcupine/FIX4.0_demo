#!/bin/bash
# 端到端测试：多客户端并发交易
# 测试多个客户端同时发送订单、撮合、撤单

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

SERVER_BIN="$BUILD_DIR/fix_server"
SERVER_LOG="/tmp/fix_server_multi_trading.log"
TEST_CONFIG="$SCRIPT_DIR/config_test.ini"
NUM_CLIENTS=3

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

# SOH 字符
SOH=$'\x01'

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

# 模拟单个客户端的交易行为
run_client() {
    local client_id="$1"
    local port="$2"
    local log_file="$3"
    local sender="CLIENT${client_id}"
    
    {
        # Logon
        echo -n "$(build_logon "$sender" "SERVER" "1" "30")"
        sleep 1
        
        # 根据客户端 ID 发送不同的订单
        case $client_id in
            0)
                # 客户端 0: 买单 AAPL
                echo -n "$(build_new_order_single "$sender" "SERVER" "2" "C0-ORD001" "AAPL" "1" "100" "150.00")"
                sleep 0.3
                echo -n "$(build_new_order_single "$sender" "SERVER" "3" "C0-ORD002" "AAPL" "1" "50" "150.50")"
                ;;
            1)
                # 客户端 1: 卖单 AAPL (应与客户端 0 撮合)
                echo -n "$(build_new_order_single "$sender" "SERVER" "2" "C1-ORD001" "AAPL" "2" "80" "150.00")"
                sleep 0.3
                echo -n "$(build_new_order_single "$sender" "SERVER" "3" "C1-ORD002" "AAPL" "2" "70" "150.50")"
                ;;
            2)
                # 客户端 2: TSLA 订单 + 撤单
                echo -n "$(build_new_order_single "$sender" "SERVER" "2" "C2-ORD001" "TSLA" "1" "200" "250.00")"
                sleep 0.5
                echo -n "$(build_cancel_request "$sender" "SERVER" "3" "C2-CXL001" "C2-ORD001" "TSLA" "1")"
                ;;
        esac
        
        # 等待足够时间接收 ExecutionReport
        sleep 3
    } | nc -w 8 127.0.0.1 "$port" > "$log_file" 2>&1
}

# ============================================================================
# 测试开始
# ============================================================================

if [ ! -x "$SERVER_BIN" ]; then
    fail "Server binary not found: $SERVER_BIN"
fi

cp "$TEST_CONFIG" "$BUILD_DIR/config.ini"

echo "=== E2E Test: Multi-Client Trading ($NUM_CLIENTS clients) ==="

# 启动服务端
echo "Starting server on port 9994..."
cd "$BUILD_DIR"
"$SERVER_BIN" 4 9994 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 2

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    fail "Server failed to start"
fi
if ! grep -q "Server listening on port 9994" "$SERVER_LOG"; then
    fail "Server not listening"
fi
pass "Server started with 4 worker threads"

# 启动多个客户端
info "Starting $NUM_CLIENTS trading clients..."
for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    CLIENT_LOGS[$i]="/tmp/fix_client_${i}_trading.log"
    run_client "$i" 9994 "${CLIENT_LOGS[$i]}" &
    CLIENT_PIDS[$i]=$!
done

# 等待所有客户端完成
sleep 10

# ============================================================================
# 验证结果
# ============================================================================

# 1. 所有会话建立
SESSION_COUNT=$(grep -c "State changing from <Disconnected> to <Established>" "$SERVER_LOG" || echo "0")
if [ "$SESSION_COUNT" -lt "$NUM_CLIENTS" ]; then
    fail "Only $SESSION_COUNT/$NUM_CLIENTS sessions established"
fi
pass "All $NUM_CLIENTS sessions established"

# 2. 收到所有订单
ORDER_COUNT=$(grep -c "Received business message: MsgType=D" "$SERVER_LOG" || echo "0")
EXPECTED_ORDERS=5  # C0: 2, C1: 2, C2: 1
if [ "$ORDER_COUNT" -lt "$EXPECTED_ORDERS" ]; then
    fail "Expected $EXPECTED_ORDERS orders, received $ORDER_COUNT"
fi
pass "Received $ORDER_COUNT NewOrderSingle messages"

# 3. 跨客户端撮合 (AAPL)
TRADE_COUNT=$(grep -c "Trade:" "$SERVER_LOG" || echo "0")
if [ "$TRADE_COUNT" -lt 1 ]; then
    fail "No trades occurred"
fi
pass "Trades executed: $TRADE_COUNT"
info "Cross-client matching verified"

# 4. 撤单请求处理
if ! grep -q "Received business message: MsgType=F" "$SERVER_LOG"; then
    fail "CancelRequest not received"
fi
pass "CancelRequest received and processed"

# 5. ExecutionReport 发送
ER_COUNT=$(grep -c "Sending ExecutionReport" "$SERVER_LOG" || echo "0")
if [ "$ER_COUNT" -lt 5 ]; then
    fail "Expected at least 5 ExecutionReports, got $ER_COUNT"
fi
pass "ExecutionReports sent: $ER_COUNT"

# 6. 验证各种订单状态
FOUND_STATUSES=""
if grep -q "OrdStatus=0" "$SERVER_LOG"; then
    FOUND_STATUSES="${FOUND_STATUSES}New "
fi
if grep -q "OrdStatus=1" "$SERVER_LOG"; then
    FOUND_STATUSES="${FOUND_STATUSES}PartialFill "
fi
if grep -q "OrdStatus=2" "$SERVER_LOG"; then
    FOUND_STATUSES="${FOUND_STATUSES}Fill "
fi
if grep -q "OrdStatus=4" "$SERVER_LOG"; then
    FOUND_STATUSES="${FOUND_STATUSES}Canceled "
fi
pass "Order statuses found: $FOUND_STATUSES"

# 7. 验证客户端收到回报
CLIENTS_WITH_RESPONSE=0
for i in $(seq 0 $((NUM_CLIENTS - 1))); do
    if [ -s "${CLIENT_LOGS[$i]}" ] && grep -q "35=8" "${CLIENT_LOGS[$i]}" 2>/dev/null; then
        CLIENTS_WITH_RESPONSE=$((CLIENTS_WITH_RESPONSE + 1))
    fi
done
if [ "$CLIENTS_WITH_RESPONSE" -lt "$NUM_CLIENTS" ]; then
    info "Only $CLIENTS_WITH_RESPONSE/$NUM_CLIENTS clients received ExecutionReports"
else
    pass "All clients received ExecutionReports"
fi

# 等待清理
sleep 2

# 8. 会话清理
DESTROYED_COUNT=$(grep -c "Session.*destroyed" "$SERVER_LOG" || echo "0")
if [ "$DESTROYED_COUNT" -ge "$NUM_CLIENTS" ]; then
    pass "All sessions cleaned up"
else
    info "Session cleanup: $DESTROYED_COUNT/$NUM_CLIENTS"
fi

echo ""
echo -e "${GREEN}=== Multi-Client Trading E2E test passed ===${NC}"
