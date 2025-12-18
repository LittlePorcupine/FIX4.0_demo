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

namespace fix40::client::tui {

using namespace ftxui;

TuiApp::TuiApp(std::shared_ptr<ClientState> state, std::shared_ptr<ClientApp> app)
    : state_(std::move(state))
    , app_(std::move(app))
    , screen_(ScreenInteractive::Fullscreen())
    , orderPanelState_(std::make_shared<OrderPanelState>())
    , searchBoxState_(std::make_shared<SearchBoxState>()) {
    
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
    
    // 合约搜索框
    auto searchBox = SearchBoxComponent(searchBoxState_, app_, state_, 
        [this](const std::string& symbol) {
            orderPanelState_->symbol = symbol;
        });
    
    // 刷新按钮
    auto refreshButton = Button("刷新 [F5]", [this] {
        app_->queryBalance();
        app_->queryPositions();
    });
    
    // 退出按钮
    auto exitButton = Button("退出 [Q]", [this] {
        requestExit();
    });
    
    // 主容器 - 使用水平布局让焦点可以在不同区域切换
    auto container = Container::Horizontal({
        Container::Vertical({
            searchBox,
            orderPanel,
        }),
        Container::Horizontal({
            refreshButton,
            exitButton,
        }),
    });
    
    // 添加全局快捷键
    auto withShortcuts = CatchEvent(container, [this](Event event) {
        // Q 或 Escape 退出
        if (event == Event::Character('q') || event == Event::Character('Q')) {
            requestExit();
            return true;
        }
        // F5 刷新
        if (event == Event::F5 || event == Event::Character('r') || event == Event::Character('R')) {
            app_->queryBalance();
            app_->queryPositions();
            return true;
        }
        return false;
    });
    
    return Renderer(withShortcuts, [=] {
        // 顶部状态栏
        auto header = HeaderComponent(state_);
        
        // 左侧面板：账户 + 持仓
        auto leftPanel = vbox({
            styledBorder(AccountPanelComponent(state_), " 账户资金 "),
            styledBorder(PositionPanelComponent(state_), " 持仓 ") | flex,
        }) | size(WIDTH, EQUAL, 38);
        
        // 中间面板：搜索 + 下单 + 订单
        auto centerPanel = vbox({
            styledBorder(searchBox->Render(), " 合约搜索 "),
            styledBorder(orderPanel->Render(), " 下单 "),
            styledBorder(OrderListComponent(state_), " 订单 ") | flex,
        }) | size(WIDTH, EQUAL, 42);
        
        // 右侧面板：消息日志
        auto messages = state_->getMessages();
        Elements msgElements;
        // 显示最近的消息（从下往上）
        size_t start = messages.size() > 20 ? messages.size() - 20 : 0;
        for (size_t i = start; i < messages.size(); ++i) {
            msgElements.push_back(text(messages[i]) | dim);
        }
        auto rightPanel = vbox({
            styledBorder(vbox(std::move(msgElements)) | vscroll_indicator | frame | flex, " 消息 "),
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
