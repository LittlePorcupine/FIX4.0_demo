/**
 * @file app.cpp
 * @brief TUI 主应用实现
 */

#include "app.hpp"
#include "styles.hpp"
#include "components/header.hpp"
#include "components/account_panel.hpp"
#include "components/position_panel.hpp"
#include "components/order_panel.hpp"
#include "components/search_box.hpp"
#include "components/message_panel.hpp"

namespace fix40::client::tui {

using namespace ftxui;

TuiApp::TuiApp(std::shared_ptr<ClientState> state, std::shared_ptr<ClientApp> app)
    : state_(std::move(state))
    , app_(std::move(app))
    , screen_(ScreenInteractive::Fullscreen())
    , orderPanelState_(std::make_shared<OrderPanelState>())
    , orderListState_(std::make_shared<OrderListState>())
    , searchBoxState_(std::make_shared<SearchBoxState>())
    , messagePanelState_(std::make_shared<MessagePanelState>()) {
    
    // 设置状态变更回调，刷新界面
    state_->setOnStateChange([this] {
        screen_.Post(Event::Custom);
    });
}

void TuiApp::run() {
    auto mainComponent = createMainComponent();
    screen_.Loop(mainComponent);
}

void TuiApp::requestExit() {
    screen_.Exit();
}

void TuiApp::refresh() {
    screen_.Post(Event::Custom);
}

Component TuiApp::createMainComponent() {
    // 下单面板
    auto orderPanel = OrderPanelComponent(orderPanelState_, app_, state_);
    auto orderList = OrderListComponent(orderListState_, app_, state_);
    
    // 合约搜索框
    auto searchBox = SearchBoxComponent(searchBoxState_, app_, state_, 
        [this](const std::string& symbol) {
            orderPanelState_->symbol = symbol;
        });

    // 消息面板
    auto messagePanel = MessagePanelComponent(messagePanelState_, state_);
    
    // 刷新按钮
    auto refreshButton = Button("刷新 [F5]", [this] {
        app_->queryBalance();
        app_->queryPositions();
        app_->queryOrderHistory();
    });
    
    // 退出按钮
    auto exitButton = Button("退出 [Q]", [this] {
        requestExit();
    });
    
    // 板块级焦点顺序（Tab 循环）：
    // 搜索 -> 下单 -> 订单 -> 消息 -> 刷新 -> 退出
    std::vector<Component> panels = {
        searchBox,
        orderPanel,
        orderList,
        messagePanel,
        refreshButton,
        exitButton,
    };

    auto panelContainer = Container::Vertical(panels);

    // 强制 Tab 只用于“板块切换”，避免被面板内部组件（如表单）吞掉。
    // 同时屏蔽容器自身的箭头/vi 风格导航，保证焦点不会被 ↑/↓ 等改变。
    auto focusManager = CatchEvent(panelContainer, [this, panelContainer, panels](Event event) {
        // 订单撤单确认弹窗打开时，锁定焦点在订单栏，避免 Tab 切走导致弹窗“悬挂”。
        if (orderListState_ && orderListState_->showCancelDialog) {
            if (event == Event::Tab || event == Event::TabReverse) {
                return true;
            }
        }

        auto active = panelContainer->ActiveChild();

        auto index_of_active = [&]() -> int {
            if (!active) return 0;
            for (int i = 0; i < static_cast<int>(panels.size()); ++i) {
                if (panels[i].get() == active.get()) {
                    return i;
                }
            }
            return 0;
        };

        auto focus_next = [&](int dir) {
            if (panels.empty()) return;
            int cur = index_of_active();
            for (int offset = 1; offset <= static_cast<int>(panels.size()); ++offset) {
                int next = (cur + offset * dir + static_cast<int>(panels.size())) %
                           static_cast<int>(panels.size());
                if (panels[next]->Focusable()) {
                    panels[next]->TakeFocus();
                    return;
                }
            }
        };

        if (event == Event::Tab) {
            focus_next(+1);
            return true;
        }
        if (event == Event::TabReverse) {
            focus_next(-1);
            return true;
        }

        auto forward_or_swallow = [&]() -> bool {
            if (active && active->OnEvent(event)) {
                return true;
            }
            return true; // 不让容器用这些按键改焦点
        };

        if (event == Event::ArrowUp || event == Event::ArrowDown ||
            event == Event::ArrowLeft || event == Event::ArrowRight ||
            event == Event::PageUp || event == Event::PageDown ||
            event == Event::Home || event == Event::End) {
            return forward_or_swallow();
        }

        if (event.is_character()) {
            const auto c = event.character();
            if (c == "h" || c == "j" || c == "k" || c == "l" ||
                c == "H" || c == "J" || c == "K" || c == "L") {
                return forward_or_swallow();
            }
        }

        return false;
    });
    
    // 添加全局快捷键
    auto withShortcuts = CatchEvent(focusManager, [this, searchBox, orderPanel](Event event) {
        // 当光标在“合约搜索/下单”这类文本输入区域时，不拦截字符键：
        // 用户输入的 'r'/'q' 应当进入输入框，而不是触发全局刷新/退出。
        if ((searchBox && searchBox->Focused()) || (orderPanel && orderPanel->Focused())) {
            if (event == Event::Character('q') || event == Event::Character('Q') ||
                event == Event::Character('r') || event == Event::Character('R')) {
                return false; // 交给子组件（Input）处理
            }
        }

        // Q 或 Escape 退出
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            requestExit();
            return true;
        }
        // F5 刷新
        if (event == Event::F5 || event == Event::Character('r') || event == Event::Character('R')) {
            app_->queryBalance();
            app_->queryPositions();
            app_->queryOrderHistory();
            return true;
        }
        return false;
    });
    
    return Renderer(withShortcuts, [=] {
        // 顶部状态栏
        auto header = HeaderComponent(state_);

        // 搜索框失焦时自动收起下拉列表，避免遮挡其他板块。
        if (!searchBox->Focused() && searchBoxState_) {
            searchBoxState_->showDropdown = false;
        }
        
        // 左侧面板：账户 + 持仓
        auto leftPanel = vbox({
            styledBorder(AccountPanelComponent(state_), " 账户资金 "),
            styledBorder(PositionPanelComponent(state_), " 持仓 ") | flex,
        }) | size(WIDTH, EQUAL, 38);
        
        // 中间面板：搜索 + 下单 + 订单
        auto centerPanel = vbox({
            styledBorder(searchBox->Render(), " 合约搜索 "),
            styledBorder(orderPanel->Render(), " 下单 "),
            styledBorder(orderList->Render(), " 订单 ") | flex,
        }) | size(WIDTH, EQUAL, 42);
        
        // 右侧面板：消息日志
        auto rightPanel = vbox({
            styledBorder(messagePanel->Render(), " 消息 ") | flex,
        }) | flex;
        
        // 底部工具栏（移除未实现的 Tab 按钮）
        auto toolbar = hbox({
            text(" Tab:切换焦点 ") | dim,
            filler(),
            refreshButton->Render(),
            text(" "),
            exitButton->Render(),
        }) | border;
        
        // 错误提示
        auto lastError = state_->getLastError();
        Element errorBar = lastError.empty() 
            ? text("") 
            : text(" ⚠ " + lastError + " ") | color(Color::Red) | bold;
        
        // 组合布局
        return vbox({
            header,
            hbox({
                leftPanel,
                separator(),
                centerPanel,
                separator(),
                rightPanel,
            }) | flex,
            errorBar,
            toolbar,
        });
    });
}

} // namespace fix40::client::tui
