/**
 * @file position_panel.cpp
 * @brief 持仓面板组件实现
 */

#include "position_panel.hpp"

namespace fix40::client::tui {

using namespace ftxui;

Element PositionPanelComponent(const std::shared_ptr<ClientState>& state) {
    auto positions = state->getPositions();
    
    if (positions.empty()) {
        return text("暂无持仓") | center | dim;
    }
    
    Elements rows;
    
    // 紧凑列宽，适应 35 宽度的左侧面板
    constexpr int COL_SYM = 8;   // 合约
    constexpr int COL_QTY = 4;   // 数量
    constexpr int COL_PX = 9;    // 价格
    constexpr int COL_PNL = 8;   // 盈亏
    
    // 表头（两行，更紧凑）
    rows.push_back(hbox({
        text("合约") | bold | size(WIDTH, EQUAL, COL_SYM),
        text("多") | bold | size(WIDTH, EQUAL, COL_QTY) | color(Color::Red),
        text("多均价") | bold | size(WIDTH, EQUAL, COL_PX),
        text("空") | bold | size(WIDTH, EQUAL, COL_QTY) | color(Color::Green),
        text("盈亏") | bold | size(WIDTH, EQUAL, COL_PNL),
    }));
    rows.push_back(separator());
    
    // 数据行
    for (const auto& pos : positions) {
        rows.push_back(hbox({
            text(pos.instrumentId) | size(WIDTH, EQUAL, COL_SYM),
            text(std::to_string(pos.longPosition)) | size(WIDTH, EQUAL, COL_QTY) | color(Color::Red),
            text(formatMoney(pos.longAvgPrice)) | size(WIDTH, EQUAL, COL_PX),
            text(std::to_string(pos.shortPosition)) | size(WIDTH, EQUAL, COL_QTY) | color(Color::Green),
            text(formatMoney(pos.profit)) | size(WIDTH, EQUAL, COL_PNL) | color(profitColor(pos.profit)),
        }));
    }
    
    return vbox(std::move(rows)) | flex;
}

} // namespace fix40::client::tui
