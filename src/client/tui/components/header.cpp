/**
 * @file header.cpp
 * @brief 顶部状态栏组件实现
 */

#include "header.hpp"

namespace fix40::client::tui {

using namespace ftxui;

Element HeaderComponent(const std::shared_ptr<ClientState>& state) {
    auto connState = state->getConnectionState();
    auto account = state->getAccount();
    auto userId = state->getUserId();
    
    // 连接状态颜色
    Color stateColor;
    switch (connState) {
        case ConnectionState::LOGGED_IN:
            stateColor = Color::Green;
            break;
        case ConnectionState::CONNECTING:
        case ConnectionState::LOGGING_IN:
            stateColor = Color::Yellow;
            break;
        case ConnectionState::ERROR:
            stateColor = Color::Red;
            break;
        default:
            stateColor = Color::GrayDark;
            break;
    }
    
    return hbox({
        text("FIX Trading Client") | bold | color(Color::Cyan),
        text(" │ "),
        text(state->getConnectionStateString()) | color(stateColor),
        text(" │ "),
        text("用户: ") | color(Color::GrayLight),
        text(userId.empty() ? "-" : userId) | bold,
        filler(),
        text("动态权益: ") | color(Color::GrayLight),
        text(formatMoney(account.dynamicEquity)) | bold | color(profitColor(account.positionProfit)),
        text(" │ "),
        text("可用: ") | color(Color::GrayLight),
        text(formatMoney(account.available)) | bold,
        text(" │ "),
        text("风险度: ") | color(Color::GrayLight),
        text(formatPercent(account.riskRatio)) | bold | color(account.riskRatio > 0.8 ? Color::Red : Color::White),
    }) | border;
}

} // namespace fix40::client::tui
