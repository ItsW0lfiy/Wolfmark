#pragma once

class QApplication;

namespace wolfmark::style {

namespace colour {
inline constexpr auto background = "#0b0b0b";
inline constexpr auto shell = "#101010";
inline constexpr auto sidebar = "#161616";
inline constexpr auto document = "#121212";
inline constexpr auto surface = "#191919";
inline constexpr auto code = "#1b1b1b";
inline constexpr auto code_separator = "#303030";
inline constexpr auto inline_code = "#292929";
inline constexpr auto table_header = "#202020";
inline constexpr auto table_column = "#2b2b2b";
inline constexpr auto table_row = "#343434";
inline constexpr auto table_header_rule = "#444444";
inline constexpr auto table_outer = "#303030";
inline constexpr auto raised = "#212121";
inline constexpr auto hover = "#292929";
inline constexpr auto active = "#342326";
inline constexpr auto border = "#343434";
inline constexpr auto border_strong = "#505050";
inline constexpr auto text = "#e9e7e6";
inline constexpr auto secondary = "#bbb8b6";
inline constexpr auto muted = "#8a8785";
inline constexpr auto silver = "#d0cdca";
inline constexpr auto bright = "#f4f1ef";
inline constexpr auto crimson = "#d62d4e";
inline constexpr auto crimson_hover = "#ea3d5f";
inline constexpr auto crimson_pressed = "#b8213f";
inline constexpr auto selection = "#69313c";
inline constexpr auto error = "#d45a64";
} // namespace colour

namespace metric {
inline constexpr int sidebar_width = 252;
inline constexpr int title_bar_height = 52;
inline constexpr int control_height = 32;
inline constexpr int small_radius = 6;
inline constexpr int surface_radius = 9;
inline constexpr int compact_spacing = 6;
inline constexpr int standard_spacing = 10;
} // namespace metric

void apply(QApplication& application);

} // namespace wolfmark::style
