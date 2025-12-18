/**
 * @file message_panel.hpp
 * @brief 消息面板组件（可聚焦/可滚动）
 */

#pragma once

#include <ftxui/component/component.hpp>
#include "../../client_state.hpp"
#include <memory>
#include <string>

namespace fix40::client::tui {

struct MessagePanelState {
    int selectedIndex = 0;
    size_t lastMessageCount = 0;
};

ftxui::Component MessagePanelComponent(
    std::shared_ptr<MessagePanelState> panelState,
    const std::shared_ptr<ClientState>& state);

} // namespace fix40::client::tui

