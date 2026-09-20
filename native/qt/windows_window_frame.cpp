#include "windows_window_frame.h"

#include "wolf_title_bar.h"

#include <QAbstractButton>
#include <QWidget>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#pragma comment(lib, "dwmapi.lib")
#endif

namespace wolfmark::qt::windows {
namespace {

[[nodiscard]] bool contains(QWidget* window, QWidget* child, const QPoint& point) {
    if (window == nullptr || child == nullptr || !child->isVisible()) return false;
    return QRect(child->mapTo(window, QPoint{}), child->size()).contains(point);
}

[[nodiscard]] bool interactiveTitleChild(QWidget* window, QWidget* title_bar,
                                         const QPoint& point) {
    auto* child = window->childAt(point);
    while (child != nullptr && child != title_bar) {
        if (qobject_cast<QAbstractButton*>(child) != nullptr) return true;
        child = child->parentWidget();
    }
    return false;
}

#ifdef _WIN32
[[nodiscard]] int nativeHitCode(HitRole role) {
    switch (role) {
    case HitRole::Caption: return HTCAPTION;
    case HitRole::Minimize: return HTMINBUTTON;
    case HitRole::Maximize: return HTMAXBUTTON;
    case HitRole::Close: return HTCLOSE;
    case HitRole::Left: return HTLEFT;
    case HitRole::TopLeft: return HTTOPLEFT;
    case HitRole::Top: return HTTOP;
    case HitRole::TopRight: return HTTOPRIGHT;
    case HitRole::Right: return HTRIGHT;
    case HitRole::BottomRight: return HTBOTTOMRIGHT;
    case HitRole::Bottom: return HTBOTTOM;
    case HitRole::BottomLeft: return HTBOTTOMLEFT;
    case HitRole::Client: return HTCLIENT;
    }
    return HTCLIENT;
}

[[nodiscard]] HitRole roleForNativeCode(WPARAM code) {
    if (code == HTMINBUTTON) return HitRole::Minimize;
    if (code == HTMAXBUTTON) return HitRole::Maximize;
    if (code == HTCLOSE) return HitRole::Close;
    return HitRole::Client;
}

void updateCaptionInteraction(CaptionButton* minimize, CaptionButton* maximize,
                              CaptionButton* close, HitRole hovered,
                              HitRole pressed = HitRole::Client) {
    if (minimize != nullptr)
        minimize->setNativeInteraction(hovered == HitRole::Minimize,
                                       pressed == HitRole::Minimize);
    if (maximize != nullptr)
        maximize->setNativeInteraction(hovered == HitRole::Maximize,
                                       pressed == HitRole::Maximize);
    if (close != nullptr)
        close->setNativeInteraction(hovered == HitRole::Close,
                                    pressed == HitRole::Close);
}

void showSystemMenu(HWND handle, const POINT& position) {
    const auto menu = GetSystemMenu(handle, FALSE);
    if (menu == nullptr) return;
    const auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                        position.x, position.y, 0, handle, nullptr);
    if (command != 0) PostMessageW(handle, WM_SYSCOMMAND, command, 0);
}

void constrainMaximizedBounds(HWND handle, LPARAM parameter) {
    auto* bounds = reinterpret_cast<MINMAXINFO*>(parameter);
    MONITORINFO monitor_info{sizeof(MONITORINFO)};
    const auto monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitor_info)) return;
    bounds->ptMaxPosition.x = monitor_info.rcWork.left - monitor_info.rcMonitor.left;
    bounds->ptMaxPosition.y = monitor_info.rcWork.top - monitor_info.rcMonitor.top;
    bounds->ptMaxSize.x = monitor_info.rcWork.right - monitor_info.rcWork.left;
    bounds->ptMaxSize.y = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
}
#endif

} // namespace

void installNativeFrame(QWidget* window) {
#ifdef _WIN32
    if (window == nullptr) return;
    const auto handle = reinterpret_cast<HWND>(window->winId());
    auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    style |= WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    SetWindowLongPtrW(handle, GWL_STYLE, style);
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOOWNERZORDER | SWP_NOZORDER);
#else
    Q_UNUSED(window);
#endif
}

NativeFrameStatus nativeFrameStatus(QWidget* window) {
    NativeFrameStatus status;
#ifdef _WIN32
    if (window == nullptr) return status;
    const auto style = GetWindowLongPtrW(reinterpret_cast<HWND>(window->winId()), GWL_STYLE);
    status.caption = (style & WS_CAPTION) == WS_CAPTION;
    status.thick_frame = (style & WS_THICKFRAME) != 0;
    status.system_menu = (style & WS_SYSMENU) != 0;
    status.minimize_box = (style & WS_MINIMIZEBOX) != 0;
    status.maximize_box = (style & WS_MAXIMIZEBOX) != 0;
    status.popup = (style & WS_POPUP) != 0;
#else
    Q_UNUSED(window);
#endif
    return status;
}

HitRole hitTest(QWidget* window, QWidget* title_bar, CaptionButton* minimize,
                CaptionButton* maximize, CaptionButton* close,
                const QPoint& client_point, bool fullscreen) {
    if (window == nullptr || fullscreen) return HitRole::Client;
    // Minimize and close remain ordinary Qt client controls. Only maximize must
    // expose a native caption role for the Windows 11 Snap Layout contract.
    if (contains(window, minimize, client_point)) return HitRole::Client;
    if (contains(window, maximize, client_point)) return HitRole::Maximize;
    if (contains(window, close, client_point)) return HitRole::Client;
    constexpr int border = 6;
    const bool left = client_point.x() < border;
    const bool right = client_point.x() >= window->width() - border;
    const bool top = client_point.y() < border;
    const bool bottom = client_point.y() >= window->height() - border;
    if (top && left) return HitRole::TopLeft;
    if (top && right) return HitRole::TopRight;
    if (bottom && left) return HitRole::BottomLeft;
    if (bottom && right) return HitRole::BottomRight;
    if (left) return HitRole::Left;
    if (right) return HitRole::Right;
    if (top) return HitRole::Top;
    if (bottom) return HitRole::Bottom;
    if (contains(window, title_bar, client_point) &&
        !interactiveTitleChild(window, title_bar, client_point)) {
        return HitRole::Caption;
    }
    return HitRole::Client;
}

bool handleNativeFrameEvent(QWidget* window, QWidget* title_bar,
                            CaptionButton* minimize, CaptionButton* maximize,
                            CaptionButton* close, bool fullscreen, void* message,
                            qintptr* result) {
#ifdef _WIN32
    if (window == nullptr || message == nullptr || result == nullptr) return false;
    auto* native = static_cast<MSG*>(message);
    const auto handle = native->hwnd;
    LRESULT dwm_result = 0;
    bool dwm_handled = false;
    if (native->message == WM_NCHITTEST || native->message == WM_NCMOUSEMOVE ||
        native->message == WM_NCMOUSELEAVE) {
        dwm_handled = DwmDefWindowProc(
            handle, native->message, native->wParam, native->lParam, &dwm_result);
    }
    switch (native->message) {
    case WM_NCCALCSIZE:
        return false;
    case WM_GETMINMAXINFO:
        if (!fullscreen) {
            constrainMaximizedBounds(handle, native->lParam);
            *result = 0;
            return true;
        }
        return false;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(native->lParam), GET_Y_LPARAM(native->lParam)};
        if (!ScreenToClient(handle, &point)) return false;
        const auto scale = std::max(1.0, window->devicePixelRatioF());
        const QPoint logical_point(static_cast<int>(std::lround(point.x / scale)),
                                   static_cast<int>(std::lround(point.y / scale)));
        *result = nativeHitCode(hitTest(window, title_bar, minimize, maximize, close,
                                        logical_point, fullscreen));
        return true;
    }
    case WM_NCMOUSEMOVE: {
        const auto role = roleForNativeCode(native->wParam);
        updateCaptionInteraction(minimize, maximize, close, role);
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT,
                                 handle, 0};
        TrackMouseEvent(&tracking);
        if (dwm_handled) {
            *result = dwm_result;
            return true;
        }
        return false;
    }
    case WM_NCMOUSELEAVE:
        updateCaptionInteraction(minimize, maximize, close, HitRole::Client);
        if (dwm_handled) {
            *result = dwm_result;
            return true;
        }
        return false;
    case WM_NCLBUTTONDOWN: {
        const auto role = roleForNativeCode(native->wParam);
        if (role != HitRole::Maximize || maximize == nullptr) return false;
        maximize->setDown(true);
        updateCaptionInteraction(minimize, maximize, close, role, role);
        *result = 0;
        return true;
    }
    case WM_NCLBUTTONUP: {
        if (maximize == nullptr || !maximize->isDown()) return false;
        const bool activate = native->wParam == HTMAXBUTTON;
        maximize->setDown(false);
        updateCaptionInteraction(minimize, maximize, close,
                                 activate ? HitRole::Maximize : HitRole::Client);
        if (activate) maximize->click();
        *result = 0;
        return true;
    }
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (maximize != nullptr && maximize->isDown()) {
            maximize->setDown(false);
            updateCaptionInteraction(minimize, maximize, close, HitRole::Client);
        }
        return false;
    case WM_NCRBUTTONUP:
        if (native->wParam == HTCAPTION) {
            const POINT point{GET_X_LPARAM(native->lParam), GET_Y_LPARAM(native->lParam)};
            showSystemMenu(handle, point);
            *result = 0;
            return true;
        }
        return false;
    case WM_SYSKEYDOWN:
        if (native->wParam == VK_SPACE) {
            SendMessageW(handle, WM_SYSCOMMAND, SC_KEYMENU, VK_SPACE);
            *result = 0;
            return true;
        }
        return false;
    default:
        return false;
    }
#else
    Q_UNUSED(window);
    Q_UNUSED(title_bar);
    Q_UNUSED(minimize);
    Q_UNUSED(maximize);
    Q_UNUSED(close);
    Q_UNUSED(fullscreen);
    Q_UNUSED(message);
    Q_UNUSED(result);
    return false;
#endif
}

} // namespace wolfmark::qt::windows
