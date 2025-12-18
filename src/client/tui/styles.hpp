/**
 * @file styles.hpp
 * @brief TUI 样式定义
 */

#pragma once

#include <ftxui/dom/elements.hpp>

namespace fix40::client::tui {

using namespace ftxui;

// 颜色定义
inline Color colorPrimary() { return Color::Blue; }
inline Color colorSuccess() { return Color::Green; }
inline Color colorDanger() { return Color::Red; }
inline Color colorWarning() { return Color::Yellow; }
inline Color colorMuted() { return Color::GrayDark; }

// 盈亏颜色
inline Color profitColor(double value) {
    if (value > 0) return Color::Green;
    if (value < 0) return Color::Red;
    return Color::White;
}

// 买卖颜色
inline Color sideColor(const std::string& side) {
    if (side == "BUY" || side == "1") return Color::Red;    // 买入红色
    if (side == "SELL" || side == "2") return Color::Green; // 卖出绿色
    return Color::White;
}

// 边框样式
inline Element styledBorder(Element inner, const std::string& title) {
    return window(text(title) | bold, inner);
}

// 格式化数字
inline std::string formatMoney(double value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    return buf;
}

inline std::string formatPercent(double value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f%%", value * 100);
    return buf;
}

inline std::string formatQty(int64_t qty) {
    return std::to_string(qty);
}

} // namespace fix40::client::tui
