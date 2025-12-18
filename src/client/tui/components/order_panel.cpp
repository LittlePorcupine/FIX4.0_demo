/**
 * @file order_panel.cpp
 * @brief 下单面板和订单列表组件实现
 */

#include "order_panel.hpp"

namespace fix40::client::tui {

using namespace ftxui;

static int clampIndex(int value, int size) {
    if (size <= 0) return 0;
    if (value < 0) return 0;
    if (value >= size) return size - 1;
    return value;
}

Component OrderListComponent(
    std::shared_ptr<OrderListState> listState,
    const std::shared_ptr<ClientState>& state) {

    auto base = Renderer([=](bool /*focused*/) -> Element {
        auto orders = state->getOrders();

        // 最新在前（顶部）
        std::vector<OrderInfo> view;
        view.reserve(orders.size());
        for (auto it = orders.rbegin(); it != orders.rend(); ++it) {
            view.push_back(*it);
        }

        const int count = static_cast<int>(view.size());
        if (count == 0) {
            listState->selectedIndex = 0;
            listState->selectedClOrdID.clear();
            listState->lastOrderCount = 0;
            return text("暂无订单") | center | dim;
        }

        // 订单数量变化时，尽量保持选中项稳定：
        // - 如果之前选中过某个 clOrdID：按 ID 恢复选中
        // - 如果之前选中“顶部(最新)”：新订单到来时继续跟随顶部
        const bool grew = listState->lastOrderCount > 0 &&
                          static_cast<size_t>(count) > listState->lastOrderCount;
        const bool was_at_top = listState->selectedIndex == 0;
        if (grew && was_at_top) {
            listState->selectedClOrdID.clear();
            listState->selectedIndex = 0;
        }

        if (!listState->selectedClOrdID.empty()) {
            for (int i = 0; i < count; ++i) {
                if (view[i].clOrdID == listState->selectedClOrdID) {
                    listState->selectedIndex = i;
                    break;
                }
            }
        }
        listState->selectedIndex = clampIndex(listState->selectedIndex, count);
        listState->selectedClOrdID = view[listState->selectedIndex].clOrdID;
        listState->lastOrderCount = static_cast<size_t>(count);

        // 定义固定列宽（表头和数据行必须完全一致）
        constexpr int COL_ID = 12;
        constexpr int COL_SYMBOL = 10;
        constexpr int COL_SIDE = 3;
        constexpr int COL_QTY = 5;
        constexpr int COL_STATE = 6;

        // 表头
        auto header = hbox({
            text("订单号") | bold | size(WIDTH, EQUAL, COL_ID),
            text("合约") | bold | size(WIDTH, EQUAL, COL_SYMBOL),
            text("向") | bold | size(WIDTH, EQUAL, COL_SIDE),
            text("数量") | bold | size(WIDTH, EQUAL, COL_QTY),
            text("状态") | bold | size(WIDTH, EQUAL, COL_STATE),
        });

        Elements rows;
        rows.reserve(view.size() * 2);

        for (int i = 0; i < count; ++i) {
            const auto& order = view[i];

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

            // 简化订单号显示（只显示后 COL_ID 位）
            std::string shortId = order.clOrdID;
            if (static_cast<int>(shortId.length()) > COL_ID) {
                shortId = shortId.substr(shortId.length() - COL_ID);
            }

            // 简化方向显示
            std::string sideStr = (order.side == "BUY") ? "B" : "S";

            Element row = hbox({
                text(shortId) | size(WIDTH, EQUAL, COL_ID),
                text(order.symbol) | size(WIDTH, EQUAL, COL_SYMBOL),
                text(sideStr) | size(WIDTH, EQUAL, COL_SIDE) | color(sideColor(order.side)),
                text(formatQty(order.orderQty)) | size(WIDTH, EQUAL, COL_QTY),
                text(stateStr) | size(WIDTH, EQUAL, COL_STATE) | color(stateColor),
            });

            if (i == listState->selectedIndex) row = select(row | inverted);

            rows.push_back(row);

            if (order.state == OrderState::REJECTED && !order.text.empty()) {
                rows.push_back(text("  → " + order.text) | dim | color(Color::Red));
            }
        }

        auto table = vbox(std::move(rows)) | vscroll_indicator | yframe | flex;

        return vbox({
            header,
            separator(),
            table,
        });
    });

    auto withEvents = CatchEvent(base, [=](Event event) {
        auto orders = state->getOrders();
        const int count = static_cast<int>(orders.size());
        if (count <= 0) {
            return false;
        }

        // 视图顺序是最新在前，因此 index=0 是最新订单。
        std::vector<OrderInfo> view;
        view.reserve(orders.size());
        for (auto it = orders.rbegin(); it != orders.rend(); ++it) {
            view.push_back(*it);
        }
        const int viewCount = static_cast<int>(view.size());

        auto move = [&](int delta) {
            listState->selectedIndex = clampIndex(listState->selectedIndex + delta, viewCount);
            if (viewCount > 0) {
                listState->selectedClOrdID = view[listState->selectedIndex].clOrdID;
            }
        };

        if (event == Event::ArrowUp) {
            move(-1);
            return true;
        }
        if (event == Event::ArrowDown) {
            move(+1);
            return true;
        }
        if (event == Event::PageUp) {
            move(-10);
            return true;
        }
        if (event == Event::PageDown) {
            move(+10);
            return true;
        }
        if (event == Event::Home) {
            listState->selectedIndex = 0;
            listState->selectedClOrdID = view[0].clOrdID;
            return true;
        }
        if (event == Event::End) {
            listState->selectedIndex = viewCount - 1;
            listState->selectedClOrdID = view[listState->selectedIndex].clOrdID;
            return true;
        }
        return false;
    });

    return withEvents;
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
