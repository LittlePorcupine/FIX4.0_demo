/**
 * @file message_panel.cpp
 * @brief 消息面板组件实现
 */

#include "message_panel.hpp"
#include <algorithm>

namespace fix40::client::tui {

using namespace ftxui;

static int clampIndex(int value, int size) {
    if (size <= 0) return 0;
    if (value < 0) return 0;
    if (value >= size) return size - 1;
    return value;
}

Component MessagePanelComponent(
    std::shared_ptr<MessagePanelState> panelState,
    const std::shared_ptr<ClientState>& state) {

    auto base = Renderer([=](bool /*focused*/) -> Element {
        auto messages = state->getMessages();
        const int count = static_cast<int>(messages.size());

        if (count == 0) {
            panelState->selectedIndex = 0;
            panelState->lastMessageCount = 0;
            return text("暂无消息") | center | dim;
        }

        // 初次渲染默认选中最新一条，便于查看最新消息。
        if (panelState->lastMessageCount == 0) {
            panelState->selectedIndex = count - 1;
        }

        // 新消息追加时：如果之前已经选中最后一条，则自动跟随到最新一条。
        if (panelState->lastMessageCount > 0 &&
            panelState->selectedIndex == static_cast<int>(panelState->lastMessageCount) - 1 &&
            static_cast<size_t>(count) > panelState->lastMessageCount) {
            panelState->selectedIndex = count - 1;
        }

        panelState->selectedIndex = clampIndex(panelState->selectedIndex, count);
        panelState->lastMessageCount = static_cast<size_t>(count);

        Elements rows;
        rows.reserve(messages.size());

        for (int i = 0; i < count; ++i) {
            Element row = text(messages[i]) | dim;
            if (i == panelState->selectedIndex) row = select(row | inverted);
            rows.push_back(row);
        }

        return vbox(std::move(rows)) | vscroll_indicator | yframe | flex;
    });

    auto withEvents = CatchEvent(base, [=](Event event) {
        auto messages = state->getMessages();
        const int count = static_cast<int>(messages.size());
        if (count <= 0) return false;

        auto move = [&](int delta) {
            panelState->selectedIndex = clampIndex(panelState->selectedIndex + delta, count);
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
            panelState->selectedIndex = 0;
            return true;
        }
        if (event == Event::End) {
            panelState->selectedIndex = count - 1;
            return true;
        }
        return false;
    });

    return withEvents;
}

} // namespace fix40::client::tui
