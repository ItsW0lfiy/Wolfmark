#pragma once

#include <QWidget>
#include <QAbstractButton>

namespace wolfmark::qt {

class CaptionButton final : public QAbstractButton {
public:
    enum class Action { Minimize, Maximize, Close };
    explicit CaptionButton(Action action, QWidget* parent = nullptr);
    void setMaximized(bool maximized);
    void setNativeInteraction(bool hovered, bool pressed);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Action action_;
    bool maximized_ = false;
    bool native_hovered_ = false;
    bool native_pressed_ = false;
};

class WolfTitleBar final : public QWidget {
public:
    explicit WolfTitleBar(QWidget* window);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QWidget* window_ = nullptr;
};

} // namespace wolfmark::qt
