#pragma once

class QApplication;

namespace wolfmark::style {

namespace colour {
inline constexpr auto background = "#101012";
inline constexpr auto shell = "#151518";
inline constexpr auto sidebar = "#19191c";
inline constexpr auto document = "#101012";
inline constexpr auto surface = "#242428";
inline constexpr auto code = "#202024";
inline constexpr auto code_separator = "#34343a";
inline constexpr auto inline_code = "#303036";
inline constexpr auto table_header = "#28282d";
inline constexpr auto table_column = "#34343a";
inline constexpr auto table_row = "#34343a";
inline constexpr auto table_header_rule = "#48484f";
inline constexpr auto table_outer = "#34343a";
inline constexpr auto raised = "#29292e";
inline constexpr auto hover = "#303036";
inline constexpr auto active = "#3a2029";
inline constexpr auto border = "#34343a";
inline constexpr auto border_strong = "#55555c";
inline constexpr auto text = "#ebe9e8";
inline constexpr auto secondary = "#bcb9bb";
inline constexpr auto muted = "#939098";
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
