/**
 * @file search_box.hpp
 * @brief 合约搜索框组件（带预测补全）
 */

#pragma once

#include <ftxui/component/component.hpp>
#include "../styles.hpp"
#include "../../client_state.hpp"
#include "../../client_app.hpp"
#include <memory>
#include <string>
#include <functional>

namespace fix40::client::tui {

/**
 * @brief 搜索框状态
 */
struct SearchBoxState {
    std::string input;
    int selectedIndex = 0;
    bool showDropdown = false;
};

/**
 * @brief 创建合约搜索框组件
 * 
 * 支持：
 * - 输入时自动搜索
 * - 下拉列表显示搜索结果
 * - 键盘上下选择
 * - 回车确认选择
 * 
 * @param searchState 搜索状态
 * @param app 客户端应用
 * @param state 客户端状态
 * @param onSelect 选择回调
 */
ftxui::Component SearchBoxComponent(
    std::shared_ptr<SearchBoxState> searchState,
    std::shared_ptr<ClientApp> app,
    std::shared_ptr<ClientState> state,
    std::function<void(const std::string&)> onSelect);

} // namespace fix40::client::tui
