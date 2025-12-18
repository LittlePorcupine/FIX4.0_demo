/**
 * @file app.hpp
 * @brief TUI 主应用
 */

#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include "../client_state.hpp"
#include "../client_app.hpp"
#include "components/order_panel.hpp"
#include "components/search_box.hpp"
#include <memory>

namespace fix40::client::tui {

/**
 * @class TuiApp
 * @brief TUI 主应用
 *
 * 管理整个 TUI 界面，包括：
 * - 顶部状态栏
 * - 左侧：账户信息 + 持仓列表
 * - 中间：下单面板 + 订单列表
 * - 底部：消息日志
 */
class TuiApp {
public:
    TuiApp(std::shared_ptr<ClientState> state, std::shared_ptr<ClientApp> app);
    
    /**
     * @brief 运行 TUI（阻塞）
     */
    void run();
    
    /**
     * @brief 请求退出
     */
    void requestExit();
    
    /**
     * @brief 刷新界面
     */
    void refresh();

private:
    ftxui::Component createMainComponent();
    
    std::shared_ptr<ClientState> state_;
    std::shared_ptr<ClientApp> app_;
    ftxui::ScreenInteractive screen_;
    
    // 组件状态
    std::shared_ptr<OrderPanelState> orderPanelState_;
    std::shared_ptr<SearchBoxState> searchBoxState_;
};

} // namespace fix40::client::tui
