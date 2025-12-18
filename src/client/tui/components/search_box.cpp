/**
 * @file search_box.cpp
 * @brief 合约搜索框组件实现
 */

#include "search_box.hpp"
#include <algorithm>

namespace fix40::client::tui {

using namespace ftxui;

Component SearchBoxComponent(
    std::shared_ptr<SearchBoxState> searchState,
    std::shared_ptr<ClientApp> app,
    std::shared_ptr<ClientState> state,
    std::function<void(const std::string&)> onSelect) {
    
    auto input = Input(&searchState->input, "输入合约代码...");
    
    // 包装输入组件以处理事件
    auto component = CatchEvent(input, [=](Event event) {
        auto results = state->getSearchResults();
        
        // 输入变化时触发搜索
        if (event.is_character() || event == Event::Backspace) {
            // 延迟到下一帧处理，确保 input 已更新
            return false;
        }
        
        // 上下键选择
        if (event == Event::ArrowDown) {
            if (!results.empty()) {
                searchState->selectedIndex = std::min(
                    searchState->selectedIndex + 1,
                    static_cast<int>(results.size()) - 1);
                searchState->showDropdown = true;
            }
            return true;
        }
        if (event == Event::ArrowUp) {
            searchState->selectedIndex = std::max(searchState->selectedIndex - 1, 0);
            return true;
        }
        
        // 回车确认选择
        if (event == Event::Return) {
            if (searchState->showDropdown && !results.empty() &&
                searchState->selectedIndex < static_cast<int>(results.size())) {
                std::string selected = results[searchState->selectedIndex];
                searchState->input = selected;
                searchState->showDropdown = false;
                if (onSelect) {
                    onSelect(selected);
                }
                return true;
            }
        }
        
        // Tab 补全第一个结果
        if (event == Event::Tab) {
            if (!results.empty()) {
                searchState->input = results[0];
                searchState->showDropdown = false;
                if (onSelect) {
                    onSelect(results[0]);
                }
                return true;
            }
        }
        
        // Escape 关闭下拉
        if (event == Event::Escape) {
            searchState->showDropdown = false;
            return true;
        }
        
        return false;
    });
    
    // 输入变化时触发搜索（使用 shared_ptr 避免悬空引用）
    auto lastInput = std::make_shared<std::string>();
    
    return Renderer(component, [=] {
        // 检查输入是否变化
        if (searchState->input != *lastInput) {
            *lastInput = searchState->input;
            if (!searchState->input.empty()) {
                app->searchInstruments(searchState->input, 10);
                searchState->showDropdown = true;
                searchState->selectedIndex = 0;
            } else {
                state->setSearchResults({});
                searchState->showDropdown = false;
            }
        }
        
        auto results = state->getSearchResults();
        
        Elements elements;
        
        // 输入框
        auto inputElement = hbox({
            text("🔍 "),
            input->Render() | flex,
        }) | border;
        
        // 如果有输入且有结果，显示预测补全（灰色）
        if (!searchState->input.empty() && !results.empty()) {
            std::string firstResult = results[0];
            if (firstResult.find(searchState->input) == 0 && 
                firstResult.length() > searchState->input.length()) {
                std::string completion = firstResult.substr(searchState->input.length());
                inputElement = hbox({
                    text("🔍 "),
                    text(searchState->input),
                    text(completion) | dim,
                    filler(),
                }) | border;
            }
        }
        
        elements.push_back(inputElement);
        
        // 下拉列表
        if (searchState->showDropdown && !results.empty()) {
            Elements dropdownItems;
            for (size_t i = 0; i < results.size(); ++i) {
                bool selected = (static_cast<int>(i) == searchState->selectedIndex);
                auto item = text(results[i]);
                if (selected) {
                    item = item | inverted;
                }
                dropdownItems.push_back(item);
            }
            elements.push_back(vbox(std::move(dropdownItems)) | border | size(HEIGHT, LESS_THAN, 12));
        }
        
        return vbox(std::move(elements));
    });
}

} // namespace fix40::client::tui
