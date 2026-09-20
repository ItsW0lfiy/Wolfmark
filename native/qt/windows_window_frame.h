#pragma once

#include <QPoint>

class QWidget;

namespace wolfmark::qt {
class CaptionButton;

namespace windows {

enum class HitRole {
    Client,
    Caption,
    Minimize,
    Maximize,
    Close,
    Left,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
};

struct NativeFrameStatus {
    bool caption = false;
    bool thick_frame = false;
    bool system_menu = false;
    bool minimize_box = false;
    bool maximize_box = false;
    bool popup = false;
    int corner_preference = 0;
};

void installNativeFrame(QWidget* window);
[[nodiscard]] NativeFrameStatus nativeFrameStatus(QWidget* window);
[[nodiscard]] HitRole hitTest(QWidget* window, QWidget* title_bar,
                              CaptionButton* minimize, CaptionButton* maximize,
                              CaptionButton* close, const QPoint& client_point,
                              bool fullscreen);
bool handleNativeFrameEvent(QWidget* window, QWidget* title_bar,
                            CaptionButton* minimize, CaptionButton* maximize,
                            CaptionButton* close, bool fullscreen, void* message,
                            qintptr* result);

} // namespace windows
} // namespace wolfmark::qt
