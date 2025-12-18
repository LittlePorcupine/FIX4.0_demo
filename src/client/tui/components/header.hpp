/**
 * @file header.hpp
 * @brief 顶部状态栏组件
 */

#pragma once

#include <ftxui/component/component.hpp>
#include "../styles.hpp"
#include "../../client_state.hpp"
#include <memory>

namespace fix40::client::tui {

/**
 * @brief 创建顶部状态栏
 * 
 * 显示：连接状态、用户ID、动态权益、可用资金
 */
ftxui::Element HeaderComponent(const std::shared_ptr<ClientState>& state);

} // namespace fix40::client::tui
