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
    std::shared_ptr<ClientApp> app,
    const std::shared_ptr<ClientState>& state) {

    auto getView = [state]() -> std::vector<OrderInfo> {
        auto orders = state->getOrders();
        std::vector<OrderInfo> view;
        view.reserve(orders.size());
        for (auto it = orders.rbegin(); it != orders.rend(); ++it) {
            view.push_back(*it);
        }
        return view;
    };

    auto getOrderById = [=](const std::string& clOrdID) -> std::optional<OrderInfo> {
        if (clOrdID.empty()) {
            return std::nullopt;
        }
        auto view = getView();
        for (const auto& o : view) {
            if (o.clOrdID == clOrdID) {
                return o;
            }
        }
        return std::nullopt;
    };

    auto isCancelable = [](const OrderInfo& order) -> bool {
        // “挂单”语义：订单仍处于活跃状态（可撤），包括：
        // - 待确：PENDING_NEW
        // - 挂单：NEW
        // - 部成：PARTIALLY_FILLED
        return order.state == OrderState::PENDING_NEW ||
               order.state == OrderState::NEW ||
               order.state == OrderState::PARTIALLY_FILLED;
    };

    auto orderStateText = [](OrderState state) -> std::string {
        switch (state) {
            case OrderState::PENDING_NEW: return "待确";
            case OrderState::NEW: return "挂单";
            case OrderState::PARTIALLY_FILLED: return "部成";
            case OrderState::FILLED: return "成交";
            case OrderState::CANCELED: return "撤销";
            case OrderState::REJECTED: return "拒绝";
        }
        return "未知";
    };

    auto base = Renderer([=](bool /*focused*/) -> Element {
        // 最新在前（顶部）
        std::vector<OrderInfo> view = getView();

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
            bool found = false;
            for (int i = 0; i < count; ++i) {
                if (view[i].clOrdID == listState->selectedClOrdID) {
                    listState->selectedIndex = i;
                    found = true;
                    break;
                }
            }
            // 方案 A：若旧选中订单已不存在，回到顶部（最新订单）。
            if (!found) {
                listState->selectedIndex = 0;
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
        // 弹窗打开时：
        // - Esc 关闭弹窗
        // - 其它事件交给 Modal 内部组件处理（本组件不消费）
        if (listState->showCancelDialog) {
            if (event == Event::Escape) {
                listState->showCancelDialog = false;
                listState->cancelDialogClOrdID.clear();
                return true;
            }
            return false;
        }

        // 视图顺序是最新在前，因此 index=0 是最新订单。
        std::vector<OrderInfo> view = getView();
        const int viewCount = static_cast<int>(view.size());
        if (viewCount <= 0) {
            return false;
        }

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

        // 回车打开撤单确认弹窗（仅在订单列表有选中项时）。
        if (event == Event::Return) {
            if (!listState->selectedClOrdID.empty()) {
                listState->showCancelDialog = true;
                listState->cancelDialogClOrdID = listState->selectedClOrdID;
                return true;
            }
        }
        return false;
    });

    // --------------------------
    // 撤单确认弹窗（Modal）
    // --------------------------

    auto cancelButtonOption = ButtonOption::Simple();
    cancelButtonOption.transform = [](const EntryState& s) {
        auto elem = text(s.label);
        if (s.focused) elem = elem | bold;
        if (s.active) elem = elem | inverted;
        return elem | border;
    };

    auto closeButton = Button("返回 (Esc)", [=] {
        listState->showCancelDialog = false;
        listState->cancelDialogClOrdID.clear();
    }, cancelButtonOption);

    auto cancelActionEnabled = Button("确认撤单", [=] {
        auto maybeOrder = getOrderById(listState->cancelDialogClOrdID);
        if (!maybeOrder.has_value()) {
            state->setLastError("撤单失败：订单不存在或已被清理");
            listState->showCancelDialog = false;
            listState->cancelDialogClOrdID.clear();
            return;
        }
        const auto& order = *maybeOrder;
        if (!isCancelable(order)) {
            // 不可撤：不做任何操作（按钮应为灰态时不会走到这里）
            return;
        }
        const std::string side = (order.side == "BUY") ? "1" : "2";
        app->sendCancelOrder(order.clOrdID, order.symbol, side);
        listState->showCancelDialog = false;
        listState->cancelDialogClOrdID.clear();
    }, cancelButtonOption);

    auto cancelActionDisabled = Renderer([] {
        return text("确认撤单") | dim | color(Color::GrayDark) | border;
    });

    auto cancelActionSelector = std::make_shared<int>(0);  // 0=enabled, 1=disabled（由 Renderer 动态决定）
    auto cancelActionTab = Container::Tab({cancelActionEnabled, cancelActionDisabled}, cancelActionSelector.get());

    auto dialogContainer = Container::Horizontal({
        cancelActionTab,
        closeButton,
    });

    auto dialog = Renderer(dialogContainer, [=] {
        auto maybeOrder = getOrderById(listState->cancelDialogClOrdID);
        if (!maybeOrder.has_value()) {
            *cancelActionSelector = 1;
            // 订单不在列表里时直接提示并提供返回。
            return window(
                text(" 撤单 ") | bold,
                vbox({
                    text("订单已不存在或已被刷新移除") | color(Color::Red),
                    separator(),
                    hbox({cancelActionTab->Render(), filler(), closeButton->Render()}),
                }) | size(WIDTH, GREATER_THAN, 40));
        }

        const auto& order = *maybeOrder;
        const bool canCancel = isCancelable(order);
        *cancelActionSelector = canCancel ? 0 : 1;

        if (!canCancel) {
            // 不可撤时，优先把焦点给“返回”，避免 Tab 切换焦点的需求。
            closeButton->TakeFocus();
        }

        std::string displayId = order.clOrdID;
        if (displayId.size() > 24) {
            displayId = displayId.substr(displayId.size() - 24);
        }

        auto details = vbox({
            hbox({text("合约: "), text(order.symbol) | bold}),
            hbox({text("方向: "), text(order.side == "BUY" ? "BUY" : "SELL") | color(sideColor(order.side))}),
            hbox({text("数量: "), text(formatQty(order.orderQty))}),
            hbox({text("状态: "), text(orderStateText(order.state))}),
            hbox({text("ClOrdID: "), text(displayId) | dim}),
        });

        auto tips = text(canCancel ? "确认要撤销该订单吗？" : "该订单不是挂单状态，无法撤单。") |
                   (canCancel ? color(Color::White) : color(Color::GrayDark));

        return window(
            text(" 撤单确认 ") | bold,
            vbox({
                details,
                separator(),
                tips,
                separator(),
                hbox({
                    cancelActionTab->Render(),
                    text(" "),
                    closeButton->Render(),
                }),
            }) | size(WIDTH, GREATER_THAN, 48));
    });

    return Modal(withEvents, dialog, &listState->showCancelDialog);
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
