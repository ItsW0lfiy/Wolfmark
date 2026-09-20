#include "smooth_scroll_controller.h"

#include <QAbstractScrollArea>
#include <QEvent>

#include <algorithm>
#include <cmath>

namespace wolfmark::qt {
namespace {
constexpr double response_rate = 17.0;
// End before integer scrollbar quantization turns the final sub-pixel tail into
// isolated one-pixel updates. The snap is visually sub-pixel at normal DPR.
constexpr double stopped_velocity = 35.0;
constexpr double stopped_distance = 1.25;
constexpr double rapid_distance = 320.0;
constexpr double rapid_velocity = 900.0;
}

SmoothScrollController::SmoothScrollController(QAbstractSlider* slider)
    : slider_(slider) {
    timer_.setInterval(16);
    timer_.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer_, &QTimer::timeout, &timer_, [this] {
        frame_due_ = true;
        if (viewport_ && viewport_->isVisible()) {
            viewport_->update();
        } else {
            frame_due_ = false;
            tick();
        }
    });
    if (slider_) {
        position_ = slider_->value();
        target_ = position_;
        for (QObject* parent = slider_->parent(); parent != nullptr; parent = parent->parent()) {
            if (auto* area = qobject_cast<QAbstractScrollArea*>(parent)) {
                viewport_ = area->viewport();
                viewport_->installEventFilter(this);
                break;
            }
        }
        QObject::connect(slider_, &QAbstractSlider::sliderPressed, &timer_,
                         [this] { cancel(); });
        QObject::connect(slider_, &QObject::destroyed, &timer_, [this] {
            timer_.stop();
            slider_ = nullptr;
            running_ = false;
        });
    }
}

SmoothScrollController::~SmoothScrollController() {
    if (viewport_) viewport_->removeEventFilter(this);
}

bool SmoothScrollController::eventFilter(QObject* watched, QEvent* event) {
    if (running_ && frame_due_ && !ticking_ && watched == viewport_ &&
        event->type() == QEvent::Paint) {
        frame_due_ = false;
        ticking_ = true;
        tick();
        ticking_ = false;
    }
    return QObject::eventFilter(watched, event);
}

void SmoothScrollController::addWheelDistance(double distance) {
    if (!slider_ || distance == 0.0) return;
    const double minimum = slider_->minimum();
    const double maximum = slider_->maximum();
    if (!running_) {
        position_ = slider_->value();
        target_ = position_;
        velocity_ = 0.0;
        frame_values_.clear();
        frame_values_.push_back(slider_->value());
        first_change_us_ = -1;
        request_elapsed_.restart();
        elapsed_.restart();
        running_ = true;
    }
    target_ = std::clamp(target_ + distance, minimum, maximum);
    if (!timer_.isActive() && std::abs(target_ - position_) >= rapid_distance &&
        timer_.interval() != 8)
        timer_.setInterval(8);
    requestFrame();
}

void SmoothScrollController::moveDirectlyTo(int destination) {
    if (!slider_) return;
    cancel();
    const int value = std::clamp(destination, slider_->minimum(), slider_->maximum());
    position_ = value;
    target_ = value;
    slider_->setValue(value);
}

void SmoothScrollController::cancel() {
    timer_.stop();
    frame_due_ = false;
    running_ = false;
    velocity_ = 0.0;
    if (slider_) {
        position_ = slider_->value();
        target_ = position_;
    }
}

bool SmoothScrollController::isRunning() const { return running_; }

int SmoothScrollController::targetValue() const {
    return static_cast<int>(std::lround(target_));
}

double SmoothScrollController::velocity() const { return velocity_; }

qint64 SmoothScrollController::firstChangeMicros() const { return first_change_us_; }

const QVector<int>& SmoothScrollController::frameValues() const { return frame_values_; }

void SmoothScrollController::requestFrame() {
    if (!running_ || timer_.isActive()) return;
    // The timer requests cadence, while the viewport paint event advances the
    // state. This binds each scrollbar write to a real raster paint instead of
    // letting controller callbacks race the QWidget backing-store schedule.
    timer_.start();
}

void SmoothScrollController::tick() {
    if (!running_ || !slider_) {
        cancel();
        return;
    }
    const qint64 elapsed_ns = elapsed_.nsecsElapsed();
    elapsed_.restart();
    const double dt = std::max(0.000001, static_cast<double>(elapsed_ns) / 1'000'000'000.0);

    // Exact critically damped integration for a fixed target over dt. This is
    // stable across variable frame intervals and never imposes a px/frame cap.
    const double displacement = position_ - target_;
    const double c = velocity_ + response_rate * displacement;
    const double decay = std::exp(-response_rate * dt);
    position_ = target_ + (displacement + c * dt) * decay;
    velocity_ = (velocity_ - response_rate * c * dt) * decay;

    const bool rapid = std::abs(velocity_) >= rapid_velocity ||
                       std::abs(target_ - position_) >= rapid_distance;
    const int cadence_ms = rapid ? 8 : 16;
    if (timer_.interval() != cadence_ms) timer_.setInterval(cadence_ms);

    if (frame_sampled) frame_sampled(dt, position_, velocity_);
    int next = std::clamp(static_cast<int>(std::lround(position_)), slider_->minimum(),
                          slider_->maximum());
    if (std::abs(target_ - position_) <= stopped_distance &&
        std::abs(velocity_) <= stopped_velocity) {
        position_ = target_;
        velocity_ = 0.0;
        next = targetValue();
    }
    if (next != slider_->value()) {
        QElapsedTimer write;
        write.start();
        slider_->setValue(next);
        if (scrollbar_write_measured) scrollbar_write_measured(write.nsecsElapsed() / 1000);
        frame_values_.push_back(next);
        if (first_change_us_ < 0) first_change_us_ = request_elapsed_.nsecsElapsed() / 1000;
        if (value_changed) value_changed(next);
    }
    if (position_ == target_ && velocity_ == 0.0) {
        finish();
    }
}

void SmoothScrollController::finish() {
    timer_.stop();
    frame_due_ = false;
    running_ = false;
    if (finished) finished();
}

} // namespace wolfmark::qt
