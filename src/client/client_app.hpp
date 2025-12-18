/**
 * @file client_app.hpp
 * @brief 客户端 FIX Application 实现
 *
 * 处理 FIX 消息的发送和接收，更新客户端状态。
 */

#pragma once

#include "fix/application.hpp"
#include "fix/fix_codec.hpp"
#include "fix/session.hpp"
#include "client_state.hpp"
#include <memory>
#include <atomic>
#include <string>

namespace fix40::client {

/**
 * @class ClientApp
 * @brief 客户端 FIX Application
 *
 * 实现 Application 接口，处理：
 * - 登录/登出
 * - 执行报告 (8)
 * - 资金查询响应 (U2)
 * - 持仓查询响应 (U4)
 * - 账户推送 (U5)
 * - 持仓推送 (U6)
 * - 合约搜索响应 (U8)
 */
class ClientApp : public Application {
public:
    /**
     * @brief 构造函数
     * @param state 客户端状态管理器
     * @param userId 用户ID（作为 SenderCompID）
     */
    ClientApp(std::shared_ptr<ClientState> state, const std::string& userId);

    ~ClientApp() override = default;

    // =========================================================================
    // Application 接口实现
    // =========================================================================

    void onLogon(const SessionID& sessionID) override;
    void onLogout(const SessionID& sessionID) override;
    void fromApp(const FixMessage& msg, const SessionID& sessionID) override;
    void toApp(FixMessage& msg, const SessionID& sessionID) override;

    // =========================================================================
    // 业务操作
    // =========================================================================

    /**
     * @brief 设置 Session
     */
    void setSession(std::shared_ptr<Session> session);

    /**
     * @brief 发送新订单
     * @param symbol 合约代码
     * @param side "1"=买, "2"=卖
     * @param qty 数量
     * @param price 价格（限价单）
     * @param ordType "1"=市价, "2"=限价
     * @return 客户端订单ID
     */
    std::string sendNewOrder(const std::string& symbol, const std::string& side,
                             int64_t qty, double price, const std::string& ordType = "2");

    /**
     * @brief 发送撤单请求
     * @param origClOrdID 原订单ID
     * @param symbol 合约代码
     * @param side 买卖方向
     */
    void sendCancelOrder(const std::string& origClOrdID, 
                         const std::string& symbol, const std::string& side);

    /**
     * @brief 查询资金
     */
    void queryBalance();

    /**
     * @brief 查询持仓
     */
    void queryPositions();

    /**
     * @brief 搜索合约
     * @param pattern 搜索前缀
     * @param maxResults 最大返回数量
     */
    void searchInstruments(const std::string& pattern, int maxResults = 10);

    /**
     * @brief 获取用户ID
     */
    const std::string& getUserId() const { return userId_; }

private:
    // 消息处理函数
    void handleExecutionReport(const FixMessage& msg);
    void handleBalanceResponse(const FixMessage& msg);
    void handlePositionResponse(const FixMessage& msg);
    void handleAccountUpdate(const FixMessage& msg);
    void handlePositionUpdate(const FixMessage& msg);
    void handleInstrumentSearchResponse(const FixMessage& msg);

    // 生成客户端订单ID
    std::string generateClOrdID();

    std::shared_ptr<ClientState> state_;
    std::weak_ptr<Session> session_;
    std::string userId_;
    std::atomic<uint64_t> orderIdCounter_{1};
    std::atomic<uint64_t> requestIdCounter_{1};
};

} // namespace fix40::client
