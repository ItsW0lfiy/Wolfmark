#include "wolf_title_bar.h"

#include "wolf_style.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace wolfmark::qt {

CaptionButton::CaptionButton(Action action, QWidget* parent)
    : QAbstractButton(parent), action_(action) {
    setFixedSize(44, style::metric::title_bar_height);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
    setAccessibleName(action == Action::Minimize ? QStringLiteral("Minimize") :
                      action == Action::Close ? QStringLiteral("Close") :
                                               QStringLiteral("Maximize"));
    setToolTip(accessibleName());
}

void CaptionButton::setMaximized(bool maximized) {
    maximized_ = maximized;
    setAccessibleName(maximized ? QStringLiteral("Restore") : QStringLiteral("Maximize"));
    setToolTip(accessibleName());
    update();
}

void CaptionButton::setNativeInteraction(bool hovered, bool pressed) {
    if (native_hovered_ == hovered && native_pressed_ == pressed) return;
    native_hovered_ = hovered;
    native_pressed_ = pressed;
    update();
}

void CaptionButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (underMouse() || isDown() || native_hovered_ || native_pressed_) {
        painter.fillRect(rect(), QColor(action_ == Action::Close ? "#612f2f" :
                                       isDown() || native_pressed_ ? style::colour::active :
                                                                   style::colour::hover));
    }
    if (hasFocus()) {
        painter.setPen(QColor(style::colour::silver));
        painter.drawRect(rect().adjusted(3, 3, -4, -4));
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(style::colour::secondary), 1.1));
    const QPointF origin(width() / 2.0 - 5, height() / 2.0 - 5);
    painter.translate(origin);
    if (action_ == Action::Minimize) {
        painter.drawLine(QPointF(0, 5), QPointF(10, 5));
    } else if (action_ == Action::Close) {
        painter.drawLine(QPointF(1, 1), QPointF(9, 9));
        painter.drawLine(QPointF(9, 1), QPointF(1, 9));
    } else if (maximized_) {
        painter.drawPolyline(QPolygonF{QPointF(3, 2), QPointF(3, 0), QPointF(10, 0),
                                       QPointF(10, 7), QPointF(8, 7)});
        painter.drawRect(QRectF(0, 3, 7, 7));
    } else {
        painter.drawRect(QRectF(0, 0, 10, 10));
    }
}

WolfTitleBar::WolfTitleBar(QWidget* window) : QWidget(window), window_(window) {
    setFixedHeight(style::metric::title_bar_height);
    setObjectName(QStringLiteral("titleBar"));
}

void WolfTitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window_->windowHandle() != nullptr) {
        window_->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
#ifdef _WIN32
    if (event->button() == Qt::RightButton) {
        const auto global = event->globalPosition().toPoint();
        const auto handle = reinterpret_cast<HWND>(window_->winId());
        const auto menu = GetSystemMenu(handle, FALSE);
        const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, global.x(),
                                            global.y(), 0, handle, nullptr);
        if (command != 0) {
            PostMessageW(handle, WM_SYSCOMMAND, command, 0);
        }
        event->accept();
        return;
    }
#endif
    QWidget::mousePressEvent(event);
}

void WolfTitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        window_->isMaximized() ? window_->showNormal() : window_->showMaximized();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace wolfmark::qt
