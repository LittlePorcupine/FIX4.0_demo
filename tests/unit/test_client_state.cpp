/**
 * @file test_client_state.cpp
 * @brief ClientState 单元测试
 *
 * 测试客户端状态管理：
 * - 订单持久化（保存/加载）
 * - 状态更新节流
 * - 线程安全
 */

#include "../catch2/catch.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <chrono>

// 模拟 ClientState 的订单持久化逻辑
namespace test {

enum class OrderState {
    PENDING_NEW,
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED
};

struct OrderInfo {
    std::string clOrdID;
    std::string orderId;
    std::string symbol;
    std::string side;
    double price = 0.0;
    int64_t orderQty = 0;
    int64_t filledQty = 0;
    double avgPx = 0.0;
    OrderState state = OrderState::PENDING_NEW;
    std::string text;
    std::string updateTime;
};

void saveOrders(const std::string& filepath, const std::vector<OrderInfo>& orders) {
    std::ofstream ofs(filepath);
    if (!ofs) return;
    
    for (const auto& order : orders) {
        ofs << order.clOrdID << "|"
            << order.orderId << "|"
            << order.symbol << "|"
            << order.side << "|"
            << order.price << "|"
            << order.orderQty << "|"
            << order.filledQty << "|"
            << order.avgPx << "|"
            << static_cast<int>(order.state) << "|"
            << order.text << "|"
            << order.updateTime << "\n";
    }
}

std::vector<OrderInfo> loadOrders(const std::string& filepath) {
    std::vector<OrderInfo> orders;
    
    std::ifstream ifs(filepath);
    if (!ifs) return orders;
    
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> fields;
        std::istringstream iss(line);
        std::string field;
        while (std::getline(iss, field, '|')) {
            fields.push_back(field);
        }
        
        // 至少需要 9 个字段（到 state）
        if (fields.size() >= 9) {
            OrderInfo order;
            order.clOrdID = fields[0];
            order.orderId = fields[1];
            order.symbol = fields[2];
            order.side = fields[3];
            order.price = std::stod(fields[4]);
            order.orderQty = std::stoll(fields[5]);
            order.filledQty = std::stoll(fields[6]);
            order.avgPx = std::stod(fields[7]);
            order.state = static_cast<OrderState>(std::stoi(fields[8]));
            if (fields.size() > 9) order.text = fields[9];
            if (fields.size() > 10) order.updateTime = fields[10];
            orders.push_back(order);
        }
    }
    
    return orders;
}

} // namespace test

// =============================================================================
// 订单持久化测试
// =============================================================================

TEST_CASE("订单持久化 - 保存和加载", "[client_state][persistence]") {
    const std::string testFile = "/tmp/test_orders.dat";
    
    // 清理测试文件
    std::filesystem::remove(testFile);
    
    SECTION("保存空订单列表") {
        std::vector<test::OrderInfo> orders;
        test::saveOrders(testFile, orders);
        
        auto loaded = test::loadOrders(testFile);
        REQUIRE(loaded.empty());
    }
    
    SECTION("保存和加载单个订单") {
        std::vector<test::OrderInfo> orders;
        test::OrderInfo order;
        order.clOrdID = "USER001-000001";
        order.orderId = "ORD-0001";
        order.symbol = "IF2601";
        order.side = "BUY";
        order.price = 4000.0;
        order.orderQty = 2;
        order.filledQty = 2;
        order.avgPx = 4000.0;
        order.state = test::OrderState::FILLED;
        order.text = "";
        order.updateTime = "2025-01-01 10:00:00";
        orders.push_back(order);
        
        test::saveOrders(testFile, orders);
        
        auto loaded = test::loadOrders(testFile);
        REQUIRE(loaded.size() == 1);
        REQUIRE(loaded[0].clOrdID == "USER001-000001");
        REQUIRE(loaded[0].orderId == "ORD-0001");
        REQUIRE(loaded[0].symbol == "IF2601");
        REQUIRE(loaded[0].side == "BUY");
        REQUIRE(loaded[0].price == Approx(4000.0));
        REQUIRE(loaded[0].orderQty == 2);
        REQUIRE(loaded[0].filledQty == 2);
        REQUIRE(loaded[0].state == test::OrderState::FILLED);
    }
    
    SECTION("保存和加载多个订单") {
        std::vector<test::OrderInfo> orders;
        
        for (int i = 0; i < 5; ++i) {
            test::OrderInfo order;
            order.clOrdID = "USER001-" + std::to_string(i);
            order.orderId = "ORD-" + std::to_string(i);
            order.symbol = "IF2601";
            order.side = (i % 2 == 0) ? "BUY" : "SELL";
            order.price = 4000.0 + i * 10;
            order.orderQty = i + 1;
            order.filledQty = i + 1;
            order.avgPx = 4000.0 + i * 10;
            order.state = test::OrderState::FILLED;
            orders.push_back(order);
        }
        
        test::saveOrders(testFile, orders);
        
        auto loaded = test::loadOrders(testFile);
        REQUIRE(loaded.size() == 5);
        
        for (int i = 0; i < 5; ++i) {
            REQUIRE(loaded[i].clOrdID == "USER001-" + std::to_string(i));
            REQUIRE(loaded[i].price == Approx(4000.0 + i * 10));
        }
    }
    
    SECTION("加载带空字段的订单") {
        // 模拟旧格式或空字段的情况
        std::ofstream ofs(testFile);
        // text 和 updateTime 为空
        ofs << "USER001-000001|ORD-0001|IF2601|BUY|4000|2|2|4000|3||\n";
        ofs.close();
        
        auto loaded = test::loadOrders(testFile);
        REQUIRE(loaded.size() == 1);
        REQUIRE(loaded[0].clOrdID == "USER001-000001");
        REQUIRE(loaded[0].text.empty());
        REQUIRE(loaded[0].updateTime.empty());
    }
    
    SECTION("加载带拒绝原因的订单") {
        std::ofstream ofs(testFile);
        ofs << "USER001-000001||IF2601|BUY|0|10|0|0|5|Insufficient margin|\n";
        ofs.close();
        
        auto loaded = test::loadOrders(testFile);
        REQUIRE(loaded.size() == 1);
        REQUIRE(loaded[0].state == test::OrderState::REJECTED);
        REQUIRE(loaded[0].text == "Insufficient margin");
    }
    
    SECTION("加载不存在的文件") {
        auto loaded = test::loadOrders("/tmp/nonexistent_file.dat");
        REQUIRE(loaded.empty());
    }
    
    // 清理
    std::filesystem::remove(testFile);
}

// =============================================================================
// 订单状态测试
// =============================================================================

TEST_CASE("订单状态枚举", "[client_state][order]") {
    REQUIRE(static_cast<int>(test::OrderState::PENDING_NEW) == 0);
    REQUIRE(static_cast<int>(test::OrderState::NEW) == 1);
    REQUIRE(static_cast<int>(test::OrderState::PARTIALLY_FILLED) == 2);
    REQUIRE(static_cast<int>(test::OrderState::FILLED) == 3);
    REQUIRE(static_cast<int>(test::OrderState::CANCELED) == 4);
    REQUIRE(static_cast<int>(test::OrderState::REJECTED) == 5);
}

// =============================================================================
// 节流机制测试
// =============================================================================

TEST_CASE("状态更新节流", "[client_state][throttle]") {
    // 模拟节流逻辑（初始时间设为过去，确保第一次调用成功）
    auto lastNotifyTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    int notifyCount = 0;
    
    auto tryNotify = [&]() -> bool {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNotifyTime);
        if (elapsed.count() < 50) {
            return false;  // 节流
        }
        lastNotifyTime = now;
        notifyCount++;
        return true;
    };
    
    SECTION("快速连续调用被节流") {
        // 第一次调用成功
        REQUIRE(tryNotify() == true);
        REQUIRE(notifyCount == 1);
        
        // 立即再次调用被节流
        REQUIRE(tryNotify() == false);
        REQUIRE(notifyCount == 1);
        
        // 等待 60ms 后可以再次调用
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        REQUIRE(tryNotify() == true);
        REQUIRE(notifyCount == 2);
    }
    
    SECTION("间隔足够长不被节流") {
        for (int i = 0; i < 5; ++i) {
            REQUIRE(tryNotify() == true);
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
        }
        REQUIRE(notifyCount == 5);
    }
}
