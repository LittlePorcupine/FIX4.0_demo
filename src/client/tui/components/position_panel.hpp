/**
 * @file position_panel.hpp
 * @brief 持仓面板组件
 */

#pragma once

#include <ftxui/component/component.hpp>
#include "../styles.hpp"
#include "../../client_state.hpp"
#include <memory>

namespace fix40::client::tui {

/**
 * @brief 创建持仓面板
 * 
 * 显示持仓列表
 */
ftxui::Element PositionPanelComponent(const std::shared_ptr<ClientState>& state);

} // namespace fix40::client::tui
