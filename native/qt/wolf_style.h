#pragma once

class QApplication;

namespace wolfmark::style {

namespace colour {
inline constexpr auto background = "#121010";
inline constexpr auto shell = "#181515";
inline constexpr auto sidebar = "#1c1919";
inline constexpr auto document = "#121010";
inline constexpr auto surface = "#282424";
inline constexpr auto code = "#242020";
inline constexpr auto code_separator = "#3a3434";
inline constexpr auto inline_code = "#363030";
inline constexpr auto table_header = "#292525";
inline constexpr auto table_column = "#3a3434";
inline constexpr auto table_row = "#3a3434";
inline constexpr auto table_header_rule = "#4f4848";
inline constexpr auto table_outer = "#3a3434";
inline constexpr auto raised = "#2d2828";
inline constexpr auto hover = "#363030";
inline constexpr auto active = "#3a2029";
inline constexpr auto border = "#3a3434";
inline constexpr auto border_strong = "#5c5555";
inline constexpr auto text = "#ebe9e8";
inline constexpr auto secondary = "#bcb9bb";
inline constexpr auto muted = "#989093";
inline constexpr auto silver = "#d5d2d3";
inline constexpr auto bright = "#f5f3f3";
inline constexpr auto crimson = "#c52a50";
inline constexpr auto crimson_hover = "#d83a61";
inline constexpr auto crimson_pressed = "#a51e41";
inline constexpr auto selection = "#653040";
inline constexpr auto error = "#db6673";
} // namespace colour

namespace metric {
inline constexpr int sidebar_width = 240;
inline constexpr int title_bar_height = 48;
inline constexpr int control_height = 32;
inline constexpr int small_radius = 6;
inline constexpr int surface_radius = 10;
inline constexpr int compact_spacing = 6;
inline constexpr int standard_spacing = 8;
} // namespace metric

void apply(QApplication& application);

} // namespace wolfmark::style
