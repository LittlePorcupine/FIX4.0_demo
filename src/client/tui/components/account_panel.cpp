/**
 * @file account_panel.cpp
 * @brief 账户资金面板组件实现
 */

#include "account_panel.hpp"

namespace fix40::client::tui {

using namespace ftxui;

Element AccountPanelComponent(const std::shared_ptr<ClientState>& state) {
    auto account = state->getAccount();
    
    auto row = [](const std::string& label, const std::string& value, Color valueColor = Color::White) {
        return hbox({
            text(label) | color(Color::GrayLight) | size(WIDTH, EQUAL, 12),
            filler(),
            text(value) | bold | color(valueColor),
        });
    };
    
    return vbox({
        row("静态权益", formatMoney(account.balance)),
        row("动态权益", formatMoney(account.dynamicEquity), profitColor(account.positionProfit)),
        separator(),
        row("可用资金", formatMoney(account.available)),
        row("冻结保证金", formatMoney(account.frozenMargin)),
        row("占用保证金", formatMoney(account.usedMargin)),
        separator(),
        row("持仓盈亏", formatMoney(account.positionProfit), profitColor(account.positionProfit)),
        row("平仓盈亏", formatMoney(account.closeProfit), profitColor(account.closeProfit)),
        separator(),
        row("风险度", formatPercent(account.riskRatio), account.riskRatio > 0.8 ? Color::Red : Color::White),
    }) | flex;
}

} // namespace fix40::client::tui
