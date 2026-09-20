#pragma once

class QApplication;

namespace wolfmark::style {

namespace colour {
inline constexpr auto background = "#080808";
inline constexpr auto shell = "#0c0c0c";
inline constexpr auto document = "#0e0e0e";
inline constexpr auto surface = "#141414";
inline constexpr auto code = "#181818";
inline constexpr auto code_separator = "#292929";
inline constexpr auto inline_code = "#242424";
inline constexpr auto table_header = "#1a1a1a";
inline constexpr auto table_column = "#1e1e1e";
inline constexpr auto table_row = "#292929";
inline constexpr auto table_header_rule = "#383838";
inline constexpr auto table_outer = "#222222";
inline constexpr auto raised = "#1c1c1c";
inline constexpr auto hover = "#242424";
inline constexpr auto active = "#303030";
inline constexpr auto border = "#303030";
inline constexpr auto border_strong = "#464646";
inline constexpr auto text = "#e8e8e8";
inline constexpr auto secondary = "#b0b0b0";
inline constexpr auto muted = "#7c7c7c";
inline constexpr auto silver = "#c8c8c8";
inline constexpr auto bright = "#f0f0f0";
inline constexpr auto selection = "#484848";
inline constexpr auto error = "#be6868";
} // namespace colour

namespace metric {
inline constexpr int sidebar_width = 236;
inline constexpr int title_bar_height = 48;
inline constexpr int control_height = 30;
inline constexpr int small_radius = 5;
inline constexpr int surface_radius = 7;
inline constexpr int compact_spacing = 6;
inline constexpr int standard_spacing = 10;
} // namespace metric

void apply(QApplication& application);

} // namespace wolfmark::style
