/**
 * @file account_panel.hpp
 * @brief 账户资金面板组件
 */

#pragma once

#include <ftxui/component/component.hpp>
#include "../styles.hpp"
#include "../../client_state.hpp"
#include <memory>

namespace fix40::client::tui {

/**
 * @brief 创建账户资金面板
 * 
 * 显示详细的账户资金信息
 */
ftxui::Element AccountPanelComponent(const std::shared_ptr<ClientState>& state);

} // namespace fix40::client::tui
