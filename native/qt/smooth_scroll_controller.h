#pragma once

#include <QAbstractSlider>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <functional>

namespace wolfmark::qt {

// Stateful, time-based wheel motion. Input updates the current trajectory rather
// than restarting an easing curve. Floating-point state is retained until the
// final scrollbar write so fast motion is not quantized into fixed pixel steps.
class SmoothScrollController final : public QObject {
public:
    explicit SmoothScrollController(QAbstractSlider* slider);
    ~SmoothScrollController() override;

    void addWheelDistance(double distance);
    void moveDirectlyTo(int destination);
    void cancel();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] int targetValue() const;
    [[nodiscard]] double velocity() const;
    [[nodiscard]] qint64 firstChangeMicros() const;
    [[nodiscard]] const QVector<int>& frameValues() const;

    std::function<void(int)> value_changed;
    std::function<void()> finished;
    std::function<void(double, double, double)> frame_sampled;
    std::function<void(qint64)> scrollbar_write_measured;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void requestFrame();
    void tick();
    void finish();

    QPointer<QAbstractSlider> slider_;
    QPointer<QWidget> viewport_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    QElapsedTimer request_elapsed_;
    QVector<int> frame_values_;
    double position_ = 0.0;
    double target_ = 0.0;
    double velocity_ = 0.0;
    bool running_ = false;
    bool frame_due_ = false;
    bool ticking_ = false;
    qint64 first_change_us_ = -1;
};

} // namespace wolfmark::qt
