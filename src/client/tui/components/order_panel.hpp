/**
 * @file order_panel.hpp
 * @brief 下单面板和订单列表组件
 */

#pragma once

#include <ftxui/component/component.hpp>
#include "../styles.hpp"
#include "../../client_state.hpp"
#include "../../client_app.hpp"
#include <memory>
#include <string>

namespace fix40::client::tui {

/**
 * @brief 创建订单列表组件
 */
ftxui::Element OrderListComponent(const std::shared_ptr<ClientState>& state);

/**
 * @brief 下单面板状态
 */
struct OrderPanelState {
    std::string symbol;
    std::string price;
    std::string quantity;
    int sideIndex = 0;      // 0=买, 1=卖
    int orderTypeIndex = 0; // 0=限价, 1=市价
    
    // Toggle 选项（必须持久化，因为 Toggle 持有指针引用）
    std::vector<std::string> sideOptions = {"买入", "卖出"};
    std::vector<std::string> typeOptions = {"限价", "市价"};
};

/**
 * @brief 创建下单面板组件
 */
ftxui::Component OrderPanelComponent(
    std::shared_ptr<OrderPanelState> panelState,
    std::shared_ptr<ClientApp> app,
    std::shared_ptr<ClientState> state);

} // namespace fix40::client::tui
