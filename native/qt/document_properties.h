#pragma once
#include <QTextFormat>

namespace wolfmark::qt::property {
inline constexpr int list_id = QTextFormat::UserProperty + 100;
inline constexpr int list_level = QTextFormat::UserProperty + 101;
inline constexpr int list_ordered = QTextFormat::UserProperty + 102;
inline constexpr int list_start = QTextFormat::UserProperty + 103;
inline constexpr int list_task = QTextFormat::UserProperty + 104;
}
