/**
 * @file order_panel.cpp
 * @brief 下单面板和订单列表组件实现
 */

#include "order_panel.hpp"

namespace fix40::client::tui {

using namespace ftxui;

Element OrderListComponent(const std::shared_ptr<ClientState>& state) {
    auto orders = state->getOrders();
    
    if (orders.empty()) {
        return text("暂无订单") | center | dim;
    }
    
    Elements rows;
    
    // 定义固定列宽（表头和数据行必须完全一致）
    constexpr int COL_ID = 12;
    constexpr int COL_SYMBOL = 10;
    constexpr int COL_SIDE = 3;
    constexpr int COL_QTY = 5;
    constexpr int COL_STATE = 6;
    
    // 表头
    rows.push_back(hbox({
        text("订单号") | bold | size(WIDTH, EQUAL, COL_ID),
        text("合约") | bold | size(WIDTH, EQUAL, COL_SYMBOL),
        text("向") | bold | size(WIDTH, EQUAL, COL_SIDE),
        text("数量") | bold | size(WIDTH, EQUAL, COL_QTY),
        text("状态") | bold | size(WIDTH, EQUAL, COL_STATE),
    }));
    rows.push_back(separator());
    
    // 数据行（最新的在前）
    for (auto it = orders.rbegin(); it != orders.rend(); ++it) {
        const auto& order = *it;
        
        std::string stateStr;
        Color stateColor = Color::White;
        switch (order.state) {
            case OrderState::PENDING_NEW:
                stateStr = "待确";
                stateColor = Color::Yellow;
                break;
            case OrderState::NEW:
                stateStr = "挂单";
                stateColor = Color::Blue;
                break;
            case OrderState::PARTIALLY_FILLED:
                stateStr = "部成";
                stateColor = Color::Cyan;
                break;
            case OrderState::FILLED:
                stateStr = "成交";
                stateColor = Color::Green;
                break;
            case OrderState::CANCELED:
                stateStr = "撤销";
                stateColor = Color::GrayDark;
                break;
            case OrderState::REJECTED:
                stateStr = "拒绝";
                stateColor = Color::Red;
                break;
        }
        
        // 简化订单号显示（只显示后8位）
        std::string shortId = order.clOrdID;
        if (shortId.length() > COL_ID) {
            shortId = shortId.substr(shortId.length() - COL_ID);
        }
        
        // 简化方向显示
        std::string sideStr = (order.side == "BUY") ? "B" : "S";
        
        // 如果是拒绝状态且有拒绝原因，显示原因
        Element row = hbox({
            text(shortId) | size(WIDTH, EQUAL, COL_ID),
            text(order.symbol) | size(WIDTH, EQUAL, COL_SYMBOL),
            text(sideStr) | size(WIDTH, EQUAL, COL_SIDE) | color(sideColor(order.side)),
            text(formatQty(order.orderQty)) | size(WIDTH, EQUAL, COL_QTY),
            text(stateStr) | size(WIDTH, EQUAL, COL_STATE) | color(stateColor),
        });
        
        rows.push_back(row);
        
        // 拒绝原因单独一行显示
        if (order.state == OrderState::REJECTED && !order.text.empty()) {
            rows.push_back(text("  → " + order.text) | dim | color(Color::Red));
        }
    }
    
    return vbox(std::move(rows)) | vscroll_indicator | yframe | flex;
}

Component OrderPanelComponent(
    std::shared_ptr<OrderPanelState> panelState,
    std::shared_ptr<ClientApp> app,
    std::shared_ptr<ClientState> state) {
    
    auto symbolInput = Input(&panelState->symbol, "合约代码");
    auto priceInput = Input(&panelState->price, "价格");
    auto qtyInput = Input(&panelState->quantity, "数量");
    
    // 使用 panelState 中的选项，避免悬空指针
    auto sideToggle = Toggle(&panelState->sideOptions, &panelState->sideIndex);
    auto typeToggle = Toggle(&panelState->typeOptions, &panelState->orderTypeIndex);
    
    auto submitButton = Button("下单", [panelState, app, state] {
        if (panelState->symbol.empty()) {
            state->setLastError("请输入合约代码");
            return;
        }
        if (panelState->quantity.empty()) {
            state->setLastError("请输入数量");
            return;
        }
        
        std::string side = (panelState->sideIndex == 0) ? "1" : "2";
        std::string ordType = (panelState->orderTypeIndex == 0) ? "2" : "1";
        
        double price = 0.0;
        if (ordType == "2" && !panelState->price.empty()) {
            try {
                price = std::stod(panelState->price);
            } catch (...) {
                state->setLastError("价格格式错误");
                return;
            }
        }
        
        int64_t qty = 0;
        try {
            qty = std::stoll(panelState->quantity);
            if (qty <= 0) {
                state->setLastError("数量必须大于0");
                return;
            }
        } catch (...) {
            state->setLastError("数量格式错误");
            return;
        }
        
        app->sendNewOrder(panelState->symbol, side, qty, price, ordType);
    });
    
    auto container = Container::Vertical({
        symbolInput,
        priceInput,
        qtyInput,
        sideToggle,
        typeToggle,
        submitButton,
    });
    
    return Renderer(container, [=] {
        return vbox({
            hbox({text("合约: "), symbolInput->Render() | flex}),
            hbox({text("价格: "), priceInput->Render() | flex}),
            hbox({text("数量: "), qtyInput->Render() | flex}),
            hbox({text("方向: "), sideToggle->Render()}),
            hbox({text("类型: "), typeToggle->Render()}),
            separator(),
            submitButton->Render() | center,
        });
    });
}

} // namespace fix40::client::tui
