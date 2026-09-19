#include "moonmark_qt.h"
#include "moon_style.h"
#include "moon_title_bar.h"
#include "document_zoom.h"
#include "document_sidebar.h"
#include "smooth_scroll_controller.h"
#include "windows_window_frame.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QAccessible>
#include <QBoxLayout>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFrame>
#include <QTextFrameFormat>
#include <QTextImageFormat>
#include <QTextListFormat>
#include <QTextLayout>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableFormat>
#include <QTextOption>
#include <QTimer>
#include <QUrl>
#include <QVariantAnimation>
#include <QWindow>
#include <QWheelEvent>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {

namespace colour = moonmark::style::colour;
constexpr int quote_depth_property = QTextFormat::UserProperty + 1;
constexpr int inline_code_property = QTextFormat::UserProperty + 2;
constexpr int code_frame_property = QTextFormat::UserProperty + 3;

bool isSupportedDocumentFile(const QFileInfo& file) {
    if (!file.exists() || !file.isFile()) return false;
    const auto suffix = file.suffix().toLower();
    return suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown") ||
           suffix == QStringLiteral("txt");
}

namespace command_kind {
constexpr int begin_paragraph = 1;
constexpr int begin_heading = 2;
constexpr int end_block = 3;
constexpr int text = 4;
constexpr int soft_break = 5;
constexpr int hard_break = 6;
constexpr int begin_list = 7;
constexpr int end_list = 8;
constexpr int begin_item = 9;
constexpr int end_item = 10;
constexpr int code_block = 11;
constexpr int horizontal_rule = 12;
constexpr int begin_quote = 13;
constexpr int end_quote = 14;
constexpr int begin_table = 15;
constexpr int begin_row = 16;
constexpr int begin_cell = 17;
constexpr int end_cell = 18;
constexpr int end_row = 19;
constexpr int end_table = 20;
constexpr int image = 21;
constexpr int raw_html = 22;
} // namespace command_kind

namespace text_style {
constexpr int emphasis = 1;
constexpr int strong = 2;
constexpr int strike = 4;
constexpr int code = 8;
constexpr int link = 16;
constexpr int ordered = 32;
constexpr int checked = 64;
constexpr int unchecked = 128;
constexpr int header = 256;
} // namespace text_style

struct Command {
    int kind = 0;
    int level = 0;
    int flags = 0;
    QString text;
    QString target;
    QString extra;
    qint64 number = 0;
    int image_width = 0;
    int image_height = 0;
    QJsonArray spans;
};

struct ImageOccurrence {
    std::uint32_t id = 0;
    int position = 0;
    bool requested = false;
    bool loaded = false;
    bool failed = false;
    int natural_width = 0;
    int natural_height = 0;
};

QString fromBuffer(const MoonmarkBuffer& buffer) {
    if (buffer.data == nullptr || buffer.len == 0) {
        return {};
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(buffer.data),
                             static_cast<qsizetype>(buffer.len));
}

QJsonObject jsonFromBuffer(const MoonmarkBuffer& buffer) {
    if (buffer.data == nullptr || buffer.len == 0) {
        return {};
    }
    const auto bytes = QByteArray::fromRawData(reinterpret_cast<const char*>(buffer.data),
                                               static_cast<qsizetype>(buffer.len));
    return QJsonDocument::fromJson(bytes).object();
}

QString applicationAssetPath(const QString& relative) {
    const auto packaged = QCoreApplication::applicationDirPath() + QLatin1Char('/') + relative;
    if (QFileInfo::exists(packaged)) {
        return packaged;
    }
    return relative;
}

QIcon applicationIcon() {
#ifdef _WIN32
    QIcon icon;
    const auto module = GetModuleHandleW(nullptr);
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        const auto handle = static_cast<HICON>(LoadImageW(
            module, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
        if (handle == nullptr) {
            continue;
        }
        icon.addPixmap(QPixmap::fromImage(QImage::fromHICON(handle)));
        DestroyIcon(handle);
    }
    if (!icon.isNull()) {
        return icon;
    }
#endif
    return QIcon(applicationAssetPath(QStringLiteral("assets/icons/moonmark.ico")));
}

Command parseCommand(const QJsonValue& value) {
    const auto object = value.toObject();
    return Command{object.value("kind").toInt(),
                   object.value("level").toInt(),
                   object.value("flags").toInt(),
                   object.value("text").toString(),
                   object.value("target").toString(),
                   object.value("extra").toString(),
                   object.value("number").toInteger(),
                   object.value("imageWidth").toInt(),
                   object.value("imageHeight").toInt(),
                   object.value("spans").toArray()};
}

QTextCharFormat baseCharacterFormat(double points = 12.75) {
    QTextCharFormat format;
    format.setFontFamilies({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")});
    format.setFontPointSize(points);
    format.setForeground(QColor(colour::text));
    return format;
}

QTextBlockFormat bodyBlockFormat(int line_height = 150) {
    QTextBlockFormat format;
    format.setTopMargin(1.0);
    format.setBottomMargin(8.0);
    format.setLineHeight(line_height, QTextBlockFormat::ProportionalHeight);
    return format;
}

QImage placeholderImage(const QString& message, int width = 900, int height = 72,
                        bool failed = false) {
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(QColor(colour::surface));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(colour::border_strong), 2));
    painter.drawLine(1, 10, 1, height - 10);
    painter.setPen(QColor(failed ? colour::secondary : colour::muted));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 11));
    painter.drawText(image.rect().adjusted(22, 12, -22, -12), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap,
                     message);
    return image;
}

QImage transparentImagePlaceholder() {
    QImage image(1, 1, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    return image;
}

QMimeData* snapshotClipboard() {
    auto* saved = new QMimeData;
    if (const auto* current = QGuiApplication::clipboard()->mimeData()) {
        for (const auto& format : current->formats()) saved->setData(format, current->data(format));
    }
    return saved;
}

class MoonButton final : public QPushButton {
public:
    explicit MoonButton(const QString& text, QWidget* parent = nullptr) : QPushButton(text, parent) {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(moonmark::style::metric::control_height);
        setFocusPolicy(Qt::StrongFocus);
    }
};

class ElidingLabel final : public QLabel {
public:
    explicit ElidingLabel(const QString& text, QWidget* parent = nullptr) : QLabel(parent) {
        setFullText(text);
    }

    void setFullText(const QString& text) {
        full_text_ = text;
        setToolTip(text);
        updateElision();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        updateElision();
    }

private:
    void updateElision() {
        QLabel::setText(fontMetrics().elidedText(full_text_, Qt::ElideMiddle, width()));
    }

    QString full_text_;
};

class ScrollFrameTrace final {
public:
    ScrollFrameTrace() : enabled_(qEnvironmentVariableIsSet("MOONMARK_SCROLL_TRACE")) {
        clock_.start();
    }

    void reset() {
        controller_intervals_us_.clear();
        paint_intervals_us_.clear();
        paint_costs_us_.clear();
        scrollbar_write_us_.clear();
        value_change_us_.clear();
        image_scan_us_.clear();
        image_delivery_us_.clear();
        wheel_events_ = 0;
        first_input_us_ = -1;
        first_paint_after_input_us_ = -1;
        last_paint_us_ = -1;
        scroll_change_pending_ = false;
        clock_.restart();
    }

    [[nodiscard]] bool enabled() const { return enabled_; }
    void recordWheel() {
        if (!enabled_) return;
        ++wheel_events_;
        if (first_input_us_ < 0) first_input_us_ = now();
    }
    void recordControllerFrame(double dt_seconds) {
        if (enabled_) append(controller_intervals_us_, static_cast<qint64>(dt_seconds * 1'000'000));
    }
    void recordScrollbarWrite(qint64 duration_us) {
        if (enabled_) append(scrollbar_write_us_, duration_us);
    }
    void recordScrollChange() {
        if (enabled_ && first_input_us_ >= 0) scroll_change_pending_ = true;
    }
    void recordValueChange(qint64 duration_us) {
        if (enabled_) append(value_change_us_, duration_us);
    }
    void recordImageScan(qint64 duration_us) {
        if (enabled_) append(image_scan_us_, duration_us);
    }
    void recordImageDelivery(qint64 duration_us) {
        if (enabled_) append(image_delivery_us_, duration_us);
    }
    void recordPaint(qint64 started_us, qint64 duration_us) {
        if (!enabled_) return;
        if (!scroll_change_pending_) return;
        scroll_change_pending_ = false;
        if (last_paint_us_ >= 0) append(paint_intervals_us_, started_us - last_paint_us_);
        last_paint_us_ = started_us;
        append(paint_costs_us_, duration_us);
        if (first_input_us_ >= 0 && first_paint_after_input_us_ < 0)
            first_paint_after_input_us_ = started_us - first_input_us_;
    }
    [[nodiscard]] qint64 now() const { return clock_.nsecsElapsed() / 1000; }

    [[nodiscard]] QString summary() const {
        const auto intervals = statistics(paint_intervals_us_);
        const auto controller = statistics(controller_intervals_us_);
        const auto costs = statistics(paint_costs_us_);
        const auto writes = statistics(scrollbar_write_us_);
        const auto values = statistics(value_change_us_);
        const auto scans = statistics(image_scan_us_);
        const auto deliveries = statistics(image_delivery_us_);
        return QStringLiteral(
                   "SCROLL_FRAME_PROFILE wheel=%1 controller_frames=%2 paints=%3 "
                   "input_to_first_paint_us=%4 paint_interval_ms_p50=%5 p95=%6 p99=%7 worst=%8 "
                   "over_16_67=%9 over_25=%10 over_33_3=%11 over_50=%12 "
                   "paint_cost_us_p50=%13 p95=%14 p99=%15 worst=%16 "
                   "scrollbar_write_us_p95=%17 value_change_us_p95=%18 "
                   "image_scans=%19 image_scan_us_p95=%20 image_deliveries=%21 "
                   "image_delivery_us_p95=%22 controller_interval_ms_p50=%23 p95=%24 "
                   "p99=%25 worst=%26")
            .arg(wheel_events_)
            .arg(controller_intervals_us_.size())
            .arg(paint_costs_us_.size())
            .arg(first_paint_after_input_us_)
            .arg(intervals.p50 / 1000.0, 0, 'f', 2)
            .arg(intervals.p95 / 1000.0, 0, 'f', 2)
            .arg(intervals.p99 / 1000.0, 0, 'f', 2)
            .arg(intervals.worst / 1000.0, 0, 'f', 2)
            .arg(countAbove(paint_intervals_us_, 16'670))
            .arg(countAbove(paint_intervals_us_, 25'000))
            .arg(countAbove(paint_intervals_us_, 33'300))
            .arg(countAbove(paint_intervals_us_, 50'000))
            .arg(costs.p50).arg(costs.p95).arg(costs.p99).arg(costs.worst)
            .arg(writes.p95).arg(values.p95)
            .arg(image_scan_us_.size()).arg(scans.p95)
            .arg(image_delivery_us_.size()).arg(deliveries.p95)
            .arg(controller.p50 / 1000.0, 0, 'f', 2)
            .arg(controller.p95 / 1000.0, 0, 'f', 2)
            .arg(controller.p99 / 1000.0, 0, 'f', 2)
            .arg(controller.worst / 1000.0, 0, 'f', 2);
    }

private:
    struct Stats { qint64 p50 = 0; qint64 p95 = 0; qint64 p99 = 0; qint64 worst = 0; };
    static void append(QVector<qint64>& values, qint64 value) {
        constexpr qsizetype capacity = 4096;
        if (values.size() == capacity) values.remove(0, capacity / 4);
        values.push_back(value);
    }
    static Stats statistics(QVector<qint64> values) {
        if (values.isEmpty()) return {};
        std::sort(values.begin(), values.end());
        const auto at = [&values](double quantile) {
            const auto index = static_cast<qsizetype>(std::ceil((values.size() - 1) * quantile));
            return values.at(std::clamp<qsizetype>(index, 0, values.size() - 1));
        };
        return {at(0.50), at(0.95), at(0.99), values.back()};
    }
    static qsizetype countAbove(const QVector<qint64>& values, qint64 threshold) {
        return std::count_if(values.cbegin(), values.cend(),
                             [threshold](qint64 value) { return value > threshold; });
    }

    bool enabled_ = false;
    QElapsedTimer clock_;
    QVector<qint64> controller_intervals_us_;
    QVector<qint64> paint_intervals_us_;
    QVector<qint64> paint_costs_us_;
    QVector<qint64> scrollbar_write_us_;
    QVector<qint64> value_change_us_;
    QVector<qint64> image_scan_us_;
    QVector<qint64> image_delivery_us_;
    int wheel_events_ = 0;
    qint64 first_input_us_ = -1;
    qint64 first_paint_after_input_us_ = -1;
    qint64 last_paint_us_ = -1;
    bool scroll_change_pending_ = false;
};

class DocumentView final : public QTextEdit {
public:
    struct InteractionState {
        int scroll = 0;
        int position = 0;
        int anchor = 0;
        int viewport_position = 0;
        int viewport_offset = 0;
    };

    explicit DocumentView(const MoonmarkApiTable* api, void* backend, QWidget* parent = nullptr)
        : QTextEdit(parent), api_(api), backend_(backend), scroll_controller_(verticalScrollBar()) {
        setReadOnly(true);
        setAcceptRichText(false);
        setFrameShape(QFrame::NoFrame);
        setUndoRedoEnabled(false);
        setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard |
                                Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(QStringLiteral("Moonmark Markdown document"));
        viewport()->setMouseTracking(true);
        document()->setDefaultStyleSheet({});
        document()->setDocumentMargin(0.0);

        image_poll_.setInterval(15);
        QObject::connect(&image_poll_, &QTimer::timeout, this, [this] { pollImages(); });
        image_prefetch_.setSingleShot(true);
        QObject::connect(&image_prefetch_, &QTimer::timeout, this,
                         [this] { queueVisibleImages(); });
        navigation_settle_.setSingleShot(true);
        QObject::connect(&navigation_settle_, &QTimer::timeout, this,
                         [this] { settleNavigation(); });
        QObject::connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
            QElapsedTimer value_change;
            value_change.start();
            scroll_trace_.recordScrollChange();
            scheduleImagePrefetch();
            scroll_trace_.recordValueChange(value_change.nsecsElapsed() / 1000);
        });
        QObject::connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
                         [this](int, int) {
            if (navigationActive()) scheduleNavigationRetarget();
        });
        QObject::connect(verticalScrollBar(), &QScrollBar::sliderPressed, this,
                         [this] { cancelScrollMotion(); });

        scroll_controller_.value_changed = [this](int) {
            if (navigation_first_change_us_ < 0 && navigation_request_elapsed_.isValid())
                navigation_first_change_us_ = navigation_request_elapsed_.nsecsElapsed() / 1000;
        };
        scroll_controller_.finished = [this] {
            scheduleImagePrefetch(true);
            settleNavigation();
        };
        scroll_controller_.frame_sampled = [this](double dt, double, double) {
            scroll_trace_.recordControllerFrame(dt);
        };
        scroll_controller_.scrollbar_write_measured = [this](qint64 duration_us) {
            scroll_trace_.recordScrollbarWrite(duration_us);
        };

        autoscroll_.setInterval(16);
        QObject::connect(&autoscroll_, &QTimer::timeout, this, [this] { autoScrollTick(); });
    }

    void load(const QJsonObject& root) {
        cancelScrollMotion();
        stopAutoscroll();
        QElapsedTimer timer;
        timer.start();
        commands_.clear();
        for (const auto& value : root.value("commands").toArray()) {
            commands_.push_back(parseCommand(value));
        }
        image_occurrences_.clear();
        image_requested_.clear();
        code_sources_.clear();
        code_frames_.clear();
        code_frame_bounds_.clear();
        code_frame_bounds_dirty_ = true;
        anchor_positions_.clear();
        navigation_anchor_.clear();
        completed_navigation_anchor_.clear();
        navigation_target_clamped_ = false;
        completed_navigation_clamped_ = false;
        navigation_settle_.stop();
        ++navigation_epoch_;
        title_ = root.value("title").toString(QStringLiteral("Moonmark"));
        settings_ = root.value("settings").toObject();
        metrics_ = root.value("metrics").toObject();
        plain_text_ = root.value("sourceType").toString() == QStringLiteral("plainText");
        setLineWrapMode(plain_text_ ? QTextEdit::NoWrap : QTextEdit::WidgetWidth);
        setAccessibleName(plain_text_ ? QStringLiteral("Moonmark plain text document")
                                      : QStringLiteral("Moonmark Markdown document"));

        auto* next = new QTextDocument(this);
        next->setDocumentMargin(0.0);
        next->setDefaultFont(baseCharacterFormat(settings_.value("bodyFontPoints").toDouble(12.75))
                                 .font());
        setDocument(next);
        QObject::connect(next->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                         this, [this] {
            ++layout_generation_;
            scheduleNavigationRetarget();
        });
        applyDocumentWidth();

        QTextCursor cursor(next);
        cursor.beginEditBlock();
        const auto error = root.value("error").toString();
        if (!error.isEmpty()) {
            auto block = bodyBlockFormat();
            block.setTopMargin(28);
            cursor.setBlockFormat(block);
            auto format = baseCharacterFormat();
            format.setForeground(QColor(colour::error));
            cursor.insertText(error, format);
        } else if (plain_text_) {
            auto block = bodyBlockFormat(145);
            block.setTopMargin(0);
            block.setBottomMargin(0);
            cursor.setBlockFormat(block);
            auto format = baseCharacterFormat();
            format.setFontFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
            cursor.insertText(root.value("literalText").toString(), format);
        } else {
            std::size_t index = 0;
            buildBlocks(cursor, index, -1, 0);
        }
        cursor.endEditBlock();
        zoom_layout_.capture(next);
        if (zoom_percent_ != 100) zoom_layout_.apply(zoom_percent_);
        applyDocumentWidth();
        construction_us_ = static_cast<quint64>(timer.nsecsElapsed() / 1000);
        document_construction_count_++;
        QTextCursor start(next);
        start.movePosition(QTextCursor::Start);
        setTextCursor(start);
        verticalScrollBar()->setValue(0);
        QTimer::singleShot(0, this, [this] {
            verticalScrollBar()->setValue(0);
            queueVisibleImages();
            invalidateCodeFrameBounds();
        });
    }

    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] quint64 constructionMicros() const { return construction_us_; }
    [[nodiscard]] quint64 constructionCount() const { return document_construction_count_; }
    [[nodiscard]] int discoveredImages() const {
        return static_cast<int>(image_occurrences_.size());
    }
    [[nodiscard]] int loadedImages() const {
        return static_cast<int>(std::count_if(image_occurrences_.cbegin(), image_occurrences_.cend(),
                                              [](const auto& item) { return item.loaded; }));
    }
    [[nodiscard]] int failedImageDecodes() const {
        return static_cast<int>(std::count_if(image_occurrences_.cbegin(), image_occurrences_.cend(),
                                              [](const auto& item) {
                                                  return item.requested && item.failed;
                                              }));
    }
    [[nodiscard]] int pendingImageDecodes() const {
        return static_cast<int>(std::count_if(image_occurrences_.cbegin(), image_occurrences_.cend(),
                                              [](const auto& item) {
                                                  return item.requested && !item.loaded && !item.failed;
                                              }));
    }
    void resetScrollProfile() { scroll_trace_.reset(); }
    [[nodiscard]] QString scrollProfileSummary() const { return scroll_trace_.summary(); }
    [[nodiscard]] bool testPixelWheelDirect() {
        auto* bar = verticalScrollBar();
        bar->setValue(std::max(1, bar->maximum() / 3));
        const int before = bar->value();
        const QPointF position(viewport()->width() / 2.0, viewport()->height() / 2.0);
        QWheelEvent wheel(position, viewport()->mapToGlobal(position.toPoint()),
                          QPoint(0, -42), QPoint(), Qt::NoButton, Qt::NoModifier,
                          Qt::ScrollUpdate, false);
        wheelEvent(&wheel);
        Q_UNUSED(before);
        return !scrollMotionRunning();
    }

    [[nodiscard]] bool testHomeEndDirect() {
        QKeyEvent end_key(QEvent::KeyPress, Qt::Key_End, Qt::ControlModifier);
        keyPressEvent(&end_key);
        const bool end_direct = !scrollMotionRunning();
        QKeyEvent home_key(QEvent::KeyPress, Qt::Key_Home, Qt::ControlModifier);
        keyPressEvent(&home_key);
        return end_direct && !scrollMotionRunning();
    }
    [[nodiscard]] QString plainText() const { return document()->toPlainText(); }
    [[nodiscard]] InteractionState interactionState() const {
        const auto viewport_anchor = captureViewportAnchor();
        return {verticalScrollBar()->value(), textCursor().position(), textCursor().anchor(),
                viewport_anchor.position, viewport_anchor.offset};
    }

    void restoreInteractionState(const InteractionState& state) {
        QTextCursor cursor(document());
        const int limit = std::max(0, document()->characterCount() - 1);
        cursor.setPosition(std::clamp(state.anchor, 0, limit));
        cursor.setPosition(std::clamp(state.position, 0, limit), QTextCursor::KeepAnchor);
        setTextCursor(cursor);
        verticalScrollBar()->setValue(std::clamp(state.scroll, verticalScrollBar()->minimum(),
                                                 verticalScrollBar()->maximum()));
        restoreViewportAnchor({state.viewport_position, state.viewport_offset,
                               state.scroll == verticalScrollBar()->minimum()});
    }
    [[nodiscard]] bool testSelectionCopy() {
        auto* previous = snapshotClipboard();
        last_copy_text_.clear();
        QKeyEvent select_event(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
        QApplication::sendEvent(this, &select_event);
        QKeyEvent copy_event(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
        QApplication::sendEvent(this, &copy_event);
        const auto copied = last_copy_text_;
        bool clipboard_available = QGuiApplication::clipboard()->text() == copied;
        bool inline_ok = true;
        int inline_count = 0;
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                if (!fragment.charFormat().boolProperty(inline_code_property)) continue;
                QTextCursor span(document());
                span.setPosition(fragment.position());
                span.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                setTextCursor(span);
                QApplication::sendEvent(this, &copy_event);
                inline_ok &= last_copy_text_ == fragment.text();
                clipboard_available &= QGuiApplication::clipboard()->text() == last_copy_text_;
                ++inline_count;
            }
        }
        std::fprintf(stdout, "INLINE_COPY spans=%d exact=%s clipboard=%s\n", inline_count,
                     inline_ok ? "ok" : "failed",
                     clipboard_available ? "verified" : "unavailable");
        QGuiApplication::clipboard()->setMimeData(previous);
        return inline_ok && copied.size() > 40 && !copied.contains(QChar::ObjectReplacementCharacter) &&
               !copied.contains(QChar(0xFDD0)) && !copied.contains(QChar(0xFDD1));
    }

    [[nodiscard]] bool testCodeCopy() {
        if (code_sources_.empty()) return true;
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
            for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
                const auto part = fragment.fragment();
                if (part.charFormat().anchorHref() != QStringLiteral("moonmark-copy:0")) continue;
                QTextCursor cursor(document());
                cursor.setPosition(part.position() + 1);
                setTextCursor(cursor);
                ensureCursorVisible();
                const auto point = cursorRect(cursor).center();
                auto* previous = snapshotClipboard();
                QMouseEvent press(QEvent::MouseButtonPress, point, viewport()->mapToGlobal(point),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, point, viewport()->mapToGlobal(point),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(viewport(), &press);
                QApplication::sendEvent(viewport(), &release);
                const bool copied = last_copy_text_ == code_sources_.front();
                if (!copied) {
                    std::fprintf(stdout, "COPY_DIAGNOSTIC press=%d cursor=%d selected=%d anchor=%s clipboard_length=%lld expected_length=%lld\n",
                                 press_position_, textCursor().position(), textCursor().hasSelection(),
                                 cursorForPosition(point).charFormat().anchorHref().toUtf8().constData(),
                                 static_cast<long long>(QGuiApplication::clipboard()->text().size()),
                                 static_cast<long long>(code_sources_.front().size()));
                }
                QGuiApplication::clipboard()->setMimeData(previous);
                return copied;
            }
        }
        return false;
    }

    [[nodiscard]] bool testAutoscrollIndicatorLifecycle() {
        stopAutoscroll();
        const QPoint anchor(viewport()->width() / 2, viewport()->height() / 2);
        const auto middle_click = [this, anchor] {
            QMouseEvent event(QEvent::MouseButtonPress, anchor,
                              viewport()->mapToGlobal(anchor), Qt::MiddleButton,
                              Qt::MiddleButton, Qt::NoModifier);
            QApplication::sendEvent(viewport(), &event);
        };
        middle_click();
        const QRectF anchored_geometry = autoscrollIndicatorRect();
        const QPointF visual_center = anchored_geometry.center();
        const bool activated = autoscroll_active_ && autoscroll_.isActive() &&
            visual_center == anchor &&
            viewport()->cursor().shape() != Qt::SizeVerCursor;
        const int previous_scroll = verticalScrollBar()->value();
        verticalScrollBar()->setValue(std::min(verticalScrollBar()->maximum(),
                                               previous_scroll + 80));
        QApplication::processEvents();
        const bool document_scrolled = verticalScrollBar()->maximum() == 0 ||
            verticalScrollBar()->value() != previous_scroll;
        const bool pinned_during_scroll = autoscrollIndicatorRect() == anchored_geometry;
        const auto snapshot_path = qEnvironmentVariable("MOONMARK_AUTOSCROLL_SNAPSHOT");
        if (!snapshot_path.isEmpty()) viewport()->grab().save(snapshot_path);

        QMouseEvent movement(QEvent::MouseMove, anchor + QPoint(0, 80),
                             viewport()->mapToGlobal(anchor + QPoint(0, 80)),
                             Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport(), &movement);
        const bool stationary = autoscrollIndicatorRect() == anchored_geometry;
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(this, &escape);
        const bool escape_cancelled = !autoscroll_active_ && !autoscroll_.isActive() &&
            autoscrollIndicatorRect().isEmpty();

        middle_click();
        middle_click();
        const bool second_middle_cancelled = !autoscroll_active_ &&
            autoscrollIndicatorRect().isEmpty();

        middle_click();
        QMouseEvent left_click(QEvent::MouseButtonPress, anchor,
                               viewport()->mapToGlobal(anchor), Qt::LeftButton,
                               Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport(), &left_click);
        const bool left_cancelled = !autoscroll_active_ &&
            autoscrollIndicatorRect().isEmpty();
        std::fprintf(stdout,
            "AUTOSCROLL_DETAIL activated=%s stationary=%s scrolled=%s pinned=%s "
            "escape=%s second_middle=%s left_click=%s center=%.0f,%.0f expected=%d,%d\n",
            activated ? "ok" : "failed", stationary ? "ok" : "failed",
            document_scrolled ? "ok" : "failed", pinned_during_scroll ? "ok" : "failed",
            escape_cancelled ? "ok" : "failed",
            second_middle_cancelled ? "ok" : "failed",
            left_cancelled ? "ok" : "failed", visual_center.x(), visual_center.y(),
            anchor.x(), anchor.y());
        return activated && stationary && document_scrolled && pinned_during_scroll &&
            escape_cancelled && second_middle_cancelled && left_cancelled;
    }

    [[nodiscard]] bool testDocumentStyle() const {
        int tables = 0;
        bool right_aligned = false;
        for (auto* frame : document()->rootFrame()->childFrames()) {
            auto* table = qobject_cast<QTextTable*>(frame);
            if (table == nullptr) continue;
            ++tables;
            if (table->format().border() > 1) return false;
            for (int row = 0; row < table->rows(); ++row) {
                for (int column = 0; column < table->columns(); ++column) {
                    const auto cell = table->cellAt(row, column);
                    const auto format = cell.format().toTableCellFormat();
                    if (format.leftBorder() != (column == 0 ? 0 : 0.75) || format.rightBorder() != 0 ||
                        format.topBorder() != 0) return false;
                    if (row == 0 && format.background().color() != QColor(colour::table_header))
                        return false;
                    right_aligned |= cell.firstCursorPosition().blockFormat().alignment() ==
                                     Qt::AlignRight;
                    const auto block = cell.firstCursorPosition().block();
                    for (auto it = block.begin(); !it.atEnd(); ++it) {
                        const auto background = it.fragment().charFormat().background();
                        if (background.style() != Qt::NoBrush && background.color() != QColor(colour::inline_code))
                            return false;
                    }
                }
            }
        }
        const auto palette = QApplication::palette();
        for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            for (const auto role : {QPalette::Accent, QPalette::Highlight, QPalette::Link,
                                    QPalette::Text, QPalette::Button, QPalette::ButtonText}) {
                const auto color = palette.color(group, role);
                if (color.red() != color.green() || color.green() != color.blue()) return false;
            }
        }
        auto* accessible = QAccessible::queryAccessibleInterface(
            const_cast<DocumentView*>(this));
        return tables > 0 && right_aligned && accessible != nullptr &&
               accessible->textInterface() != nullptr && isReadOnly();
    }

    void queueAllImagesForSmoke() {
        for (auto& occurrence : image_occurrences_) {
            if (occurrence.loaded || occurrence.failed || occurrence.requested ||
                image_requested_.contains(occurrence.id)) {
                continue;
            }
            if (api_->queue_image(backend_, occurrence.id, 1600)) {
                image_requested_.emplace(occurrence.id, true);
                for (auto& same : image_occurrences_) {
                    if (same.id == occurrence.id) {
                        same.requested = true;
                    }
                }
            }
        }
        image_poll_.start();
    }

    void changeZoom(int delta) {
        const int percent = std::clamp(zoom_percent_ + delta, 60, 220);
        if (percent == zoom_percent_) return;
        const auto selection = textCursor();
        const bool explicit_navigation = navigationActive();
        const auto anchor = explicit_navigation ? QTextCursor{} : cursorForPosition(QPoint(0, 0));
        const int anchor_y = explicit_navigation ? 0 : cursorRect(anchor).top();
        const bool at_top = verticalScrollBar()->value() == 0;
        zoom_percent_ = percent;
        QElapsedTimer profile;
        profile.start();
        setUpdatesEnabled(false);
        document()->setLayoutEnabled(false);
        zoom_layout_.apply(percent);
        const auto formats_us = profile.nsecsElapsed() / 1000;
        applyDocumentWidth();
        resizeLoadedImages();
        const auto images_us = profile.nsecsElapsed() / 1000;
        document()->setLayoutEnabled(true);
        invalidateCodeFrameBounds();
        setTextCursor(selection);
        if (!explicit_navigation) {
            verticalScrollBar()->setValue(at_top ? 0 : verticalScrollBar()->value() +
                                            cursorRect(anchor).top() - anchor_y);
        }
        setUpdatesEnabled(true);
        scheduleNavigationRetarget();
        if (zoomChanged) zoomChanged(zoom_percent_);
        if (qEnvironmentVariableIsSet("MOONMARK_PROFILE"))
            std::fprintf(stdout, "ZOOM_PHASE formats_us=%lld image_geometry_us=%lld anchor_layout_us=%lld\n",
                         static_cast<long long>(formats_us), static_cast<long long>(images_us - formats_us),
                         static_cast<long long>(profile.nsecsElapsed() / 1000 - images_us));
    }

    [[nodiscard]] int zoomPercent() const { return zoom_percent_; }
    [[nodiscard]] bool isPlainText() const { return plain_text_; }
    [[nodiscard]] bool scrollMotionRunning() const {
        return scroll_controller_.isRunning();
    }
    [[nodiscard]] bool navigationActive() const { return !navigation_anchor_.isEmpty(); }
    [[nodiscard]] QString navigationAnchor() const { return navigation_anchor_; }
    [[nodiscard]] QString completedNavigationAnchor() const {
        return completed_navigation_anchor_;
    }
    [[nodiscard]] quint64 navigationEpoch() const { return navigation_epoch_; }
    [[nodiscard]] quint64 layoutGeneration() const { return layout_generation_; }
    [[nodiscard]] int pendingNavigationImages() const {
        const auto found = anchor_positions_.constFind(navigation_anchor_);
        if (found == anchor_positions_.cend()) return 0;
        const int heading_position = found.value();
        return static_cast<int>(std::count_if(
            image_occurrences_.cbegin(), image_occurrences_.cend(),
            [this, heading_position](const auto& image) {
                if (!image.requested || image.loaded || image.failed) return false;
                return image.position < heading_position || navigation_target_clamped_;
            }));
    }
    [[nodiscard]] bool navigationTargetClamped() const {
        return navigation_target_clamped_;
    }
    [[nodiscard]] bool completedNavigationClamped() const {
        return completed_navigation_clamped_;
    }
    void setImageDeliveryPausedForTest(bool paused) {
        image_delivery_paused_for_test_ = paused;
        if (!paused && pendingImageDecodes() > 0) image_poll_.start();
    }
    [[nodiscard]] bool hasAnchor(const QString& anchor) const {
        return anchor_positions_.contains(anchor);
    }
    [[nodiscard]] const QVector<int>& scrollMotionSamples() const {
        return scroll_controller_.frameValues();
    }
    [[nodiscard]] int scrollMotionTarget() const { return scroll_controller_.targetValue(); }
    [[nodiscard]] qint64 navigationLookupMicros() const { return navigation_lookup_us_; }
    [[nodiscard]] qint64 navigationGeometryMicros() const { return navigation_geometry_us_; }
    [[nodiscard]] qint64 navigationControllerStartMicros() const {
        return navigation_controller_start_us_;
    }
    [[nodiscard]] qint64 navigationFirstChangeMicros() const {
        return navigation_first_change_us_;
    }
    [[nodiscard]] qint64 navigationFirstPaintMicros() const { return navigation_first_paint_us_; }
    [[nodiscard]] int navigationRetargetCount() const { return navigation_retarget_count_; }
    [[nodiscard]] int navigationImageRetargetCount() const {
        return navigation_image_retarget_count_;
    }
    [[nodiscard]] int navigationInset() const {
        return static_cast<int>(22.0 * zoom_percent_ / 100.0);
    }
    [[nodiscard]] int anchorViewportY(const QString& anchor) const {
        const auto found = anchor_positions_.constFind(anchor);
        if (found == anchor_positions_.cend()) return std::numeric_limits<int>::min();
        QTextCursor cursor(document());
        cursor.setPosition(std::min(found.value() + 1, document()->characterCount() - 1));
        return cursorRect(cursor).top();
    }
    [[nodiscard]] int anchorDocumentY(const QString& anchor) const {
        const int viewport_y = anchorViewportY(anchor);
        if (viewport_y == std::numeric_limits<int>::min()) return viewport_y;
        return viewport_y + verticalScrollBar()->value();
    }
    [[nodiscard]] int viewportAnchorPosition() const {
        return captureViewportAnchor().position;
    }
    std::function<void(int)> zoomChanged;

    void navigateToAnchor(const QString& anchor, bool animate = true) {
        scroll_controller_.cancel();
        navigation_settle_.stop();
        ++navigation_epoch_;
        navigation_anchor_.clear();
        navigation_target_clamped_ = false;
        completed_navigation_clamped_ = false;
        navigation_request_elapsed_.restart();
        navigation_first_change_us_ = -1;
        navigation_first_paint_us_ = -1;
        navigation_retarget_count_ = 0;
        navigation_image_retarget_count_ = 0;
        QElapsedTimer phase;
        phase.start();
        const auto found = anchor_positions_.constFind(anchor);
        navigation_lookup_us_ = phase.nsecsElapsed() / 1000;
        if (found == anchor_positions_.cend()) return;
        navigation_anchor_ = anchor;
        completed_navigation_anchor_.clear();
        const auto destination = anchorDestination(anchor);
        navigation_geometry_us_ = phase.nsecsElapsed() / 1000 - navigation_lookup_us_;
        if (!destination.has_value()) {
            navigation_anchor_.clear();
            return;
        }
        Q_UNUSED(animate);
        scroll_controller_.moveDirectlyTo(*destination);
        // The immediate jump uses current live geometry. Queue images around the
        // landing before deciding whether the semantic target has settled: the
        // scrollbar value change normally schedules prefetch for a later turn.
        queueVisibleImages();
        navigation_corrected_generation_ = layout_generation_;
        navigation_stable_passes_ = 0;
        scheduleNavigationSettlement();
        navigation_controller_start_us_ = phase.nsecsElapsed() / 1000 -
                                          navigation_lookup_us_ - navigation_geometry_us_;
    }

    bool testZoom() {
        const auto before = api_->backend_counters(backend_);
        const auto constructions = constructionCount();
        const auto text_before = plainText();
        auto selection = textCursor();
        selection.setPosition(3);
        selection.setPosition(std::min(30, document()->characterCount() - 1), QTextCursor::KeepAnchor);
        setTextCursor(selection);
        bool ok = true;
        for (const int percent : {100, 125, 150, 100, 80, 100}) {
            QElapsedTimer timer;
            timer.start();
            changeZoom(percent - zoomPercent());
            const auto interactive_us = timer.nsecsElapsed() / 1000;
            viewport()->repaint();
            const auto painted_us = timer.nsecsElapsed() / 1000;
            const bool metrics = zoom_layout_.matches(percent);
            const bool retained = textCursor().position() == selection.position() &&
                                  textCursor().anchor() == selection.anchor();
            ok &= metrics && retained && plainText() == text_before;
            std::fprintf(stdout, "ZOOM percent=%d metrics=%s selection=%s height=%.1f interactive_us=%lld painted_us=%lld elapsed_us=%lld\n",
                         percent, metrics ? "ok" : "failed", retained ? "ok" : "failed",
                         document()->size().height(), static_cast<long long>(interactive_us),
                         static_cast<long long>(painted_us),
                         static_cast<long long>(timer.nsecsElapsed() / 1000));
        }
        const auto after = api_->backend_counters(backend_);
        ok &= before.parse_count == after.parse_count && before.load_count == after.load_count &&
              before.image_request_count == after.image_request_count && constructions == constructionCount();
        std::fprintf(stdout, "MOONMARK_SMOKE zoom=%s parse_delta=%llu load_delta=%llu construction_delta=%llu image_request_delta=%llu\n",
                     ok ? "ok" : "failed", after.parse_count - before.parse_count,
                     after.load_count - before.load_count, constructionCount() - constructions,
                     after.image_request_count - before.image_request_count);
        return ok;
    }

    bool testImageGeometry() {
        bool ok = true;
        for (const auto& occurrence : image_occurrences_) {
            QTextCursor cursor(document());
            cursor.setPosition(occurrence.position);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            const auto image = cursor.charFormat().toImageFormat();
            const auto block = cursor.block();
            const auto bounds = document()->documentLayout()->blockBoundingRect(block);
            const auto next = document()->documentLayout()->blockBoundingRect(block.next());
            const double excess = next.top() - bounds.bottom() -
                                  std::max(block.blockFormat().bottomMargin(), block.next().blockFormat().topMargin());
            std::fprintf(stdout, "IMAGE_GEOMETRY id=%u loaded=%d zoom=%d image_h=%.1f block_h=%.1f next_y=%.1f excess=%.1f line_height=%.1f type=%d\n",
                         occurrence.id, occurrence.loaded, zoom_percent_, image.height(), bounds.height(),
                         next.top(), excess, block.blockFormat().lineHeight(), block.blockFormat().lineHeightType());
            if (occurrence.natural_width > 0 && occurrence.natural_height > 0) {
                const auto expected = displayedImageSize(occurrence.natural_width,
                                                         occurrence.natural_height);
                ok &= image.width() == expected.width() && image.height() == expected.height();
            }
            // Image-only lines must not gain paragraph-leading proportional to bitmap height.
            if (block.text() == QString(QChar::ObjectReplacementCharacter)) ok &= std::abs(excess) < 2;
        }
        return ok;
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        const qint64 paint_started_us = scroll_trace_.now();
        QElapsedTimer paint_cost;
        paint_cost.start();
        QTextEdit::paintEvent(event);
        if (navigation_first_change_us_ >= 0 && navigation_first_paint_us_ < 0 &&
            navigation_request_elapsed_.isValid()) {
            navigation_first_paint_us_ = navigation_request_elapsed_.nsecsElapsed() / 1000;
        }
        QPainter painter(viewport());
        paintCodeFrameCorners(painter);
        painter.setPen(QPen(QColor(colour::border_strong), 2));
        // Qt retains layout, selection, and accessibility; only quote markers are painted.
        auto block = cursorForPosition(QPoint(0, 0)).block();
        for (; block.isValid(); block = block.next()) {
            QTextCursor cursor(block);
            const auto first = cursorRect(cursor);
            if (first.top() > viewport()->height()) break;
            paintInlineCodeEdges(painter, block);
            const int depth = block.blockFormat().intProperty(quote_depth_property);
            if (depth == 0 || block.text().isEmpty()) continue;
            cursor.movePosition(QTextCursor::EndOfBlock);
            auto bottom = cursorRect(cursor).bottom() + 4;
            const auto next = block.next();
            if (next.isValid() &&
                next.blockFormat().intProperty(quote_depth_property) == depth &&
                !next.text().isEmpty()) {
                bottom = cursorRect(QTextCursor(next)).top();
            }
            for (int level = 0; level < depth; ++level) {
                const int x = first.left() - static_cast<int>((14 + level * 22) * zoom_percent_ / 100.0);
                painter.drawLine(x, first.top() - 3, x, bottom);
            }
        }
        paintAutoscrollAnchor(painter);
        scroll_trace_.recordPaint(paint_started_us, paint_cost.nsecsElapsed() / 1000);
    }

    void paintInlineCodeEdges(QPainter& painter, const QTextBlock& block) const {
        const auto* layout = block.layout();
        if (!layout) return;
        const auto origin = document()->documentLayout()->blockBoundingRect(block).topLeft() -
                            QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
        const auto selection = textCursor();
        const double padding = 3.5 * zoom_percent_ / 100.0;
        const double radius = 1.0 * zoom_percent_ / 100.0;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(colour::inline_code));
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.charFormat().boolProperty(inline_code_property)) continue;
            // Native selection remains authoritative, including partial span selection.
            if (selection.hasSelection() && selection.selectionStart() < fragment.position() + fragment.length() &&
                selection.selectionEnd() > fragment.position()) continue;
            const QFontMetricsF metrics(fragment.charFormat().font());
            for (int index = 0; index < layout->lineCount(); ++index) {
                const auto line = layout->lineAt(index);
                const int start = std::max(fragment.position() - block.position(), line.textStart());
                int end = std::min(fragment.position() + fragment.length() - block.position(),
                                   line.textStart() + line.textLength());
                // Qt does not paint trailing wrapping whitespace; do not outline that empty area.
                const auto text = block.text();
                while (end > start && text.at(end - 1).isSpace()) --end;
                if (start >= end) continue;
                const auto x1 = line.cursorToX(start);
                const auto x2 = line.cursorToX(end);
                const QRectF ink(origin.x() + std::min(x1, x2),
                                 origin.y() + line.y() + line.ascent() - metrics.ascent(),
                                 std::abs(x2 - x1), metrics.height());
                const QRectF outer = ink.adjusted(-padding, 0, padding, 0);
                QPainterPath surface;
                surface.addRoundedRect(outer, radius, radius);
                // Paint the side breathing room only. Clipping avoids the antialiased
                // subtraction seam that could leave a punctuation-like pixel after
                // inline code in ordinary prose. Qt remains responsible for the native
                // character background, glyphs, selection, wrapping, and hit testing.
                painter.save();
                const double glyph_guard = 0.75 * zoom_percent_ / 100.0;
                painter.setClipRect(QRectF(outer.left(), outer.top(), padding - glyph_guard,
                                           outer.height()),
                                    Qt::IntersectClip);
                painter.drawPath(surface);
                painter.restore();
                painter.save();
                painter.setClipRect(QRectF(ink.right() + glyph_guard, outer.top(),
                                           padding - glyph_guard, outer.height()),
                                    Qt::IntersectClip);
                painter.drawPath(surface);
                painter.restore();
            }
        }
        painter.restore();
    }

    void paintCodeFrameCorners(QPainter& painter) const {
        if (code_frame_bounds_dirty_) return;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(colour::document));
        painter.setClipRect(viewport()->rect());
        const QPointF scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
        const qreal radius = 7.0 * zoom_percent_ / 100.0;
        const qreal visible_top = verticalScrollBar()->value();
        const qreal visible_bottom = visible_top + viewport()->height();
        auto frame = std::lower_bound(code_frame_bounds_.cbegin(), code_frame_bounds_.cend(),
                                      visible_top,
                                      [](const CodeFrameBounds& item, qreal top) {
                                          return item.bounds.bottom() < top;
                                      });
        for (; frame != code_frame_bounds_.cend() && frame->bounds.top() <= visible_bottom;
             ++frame) {
            const QRectF bounds = frame->bounds.translated(-scroll);
            QPainterPath square;
            square.addRect(bounds);
            QPainterPath rounded;
            rounded.addRoundedRect(bounds, radius, radius);
            painter.drawPath(square.subtracted(rounded));
        }
        painter.restore();
    }

    void resizeEvent(QResizeEvent* event) override {
        const bool explicit_navigation = navigationActive();
        const auto anchor = explicit_navigation ? ViewportAnchor{} : captureViewportAnchor();
        QTextEdit::resizeEvent(event);
        applyDocumentWidth();
        resizeLoadedImages();
        if (!explicit_navigation) restoreViewportAnchor(anchor);
        invalidateCodeFrameBounds();
        scheduleNavigationRetarget();
        QTimer::singleShot(0, this, [this] { queueVisibleImages(); });
    }

    void mousePressEvent(QMouseEvent* event) override {
        cancelScrollMotion();
        if (event->button() == Qt::MiddleButton) {
            autoscroll_active_ = !autoscroll_active_;
            autoscroll_anchor_ = event->position();
            autoscroll_pointer_ = event->position();
            if (autoscroll_active_) {
                viewport()->unsetCursor();
                viewport()->update(autoscrollIndicatorRect().toAlignedRect());
                autoscroll_.start();
            } else {
                stopAutoscroll();
            }
            event->accept();
            return;
        }
        if (autoscroll_active_) {
            stopAutoscroll();
            event->accept();
            return;
        }
        press_position_ = cursorForPosition(event->position().toPoint()).position();
        press_point_ = event->position().toPoint();
        selection_drag_ = false;
        QTextEdit::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (autoscroll_active_) {
            autoscroll_pointer_ = event->position();
            event->accept();
            return;
        }
        if ((event->buttons() & Qt::LeftButton) != 0 &&
            (event->position().toPoint() - press_point_).manhattanLength() > 3) {
            selection_drag_ = true;
        }
        QTextEdit::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        QTextEdit::mouseReleaseEvent(event);
        if (event->button() != Qt::LeftButton || selection_drag_ ||
            (event->position().toPoint() - press_point_).manhattanLength() > 3 ||
            event->modifiers().testFlag(Qt::ShiftModifier)) {
            return;
        }
        // Qt may select the link label on mouse release. Detect a click by its
        // gesture/anchor, not by the resulting native selection.
        const auto anchor = anchorAt(event->position().toPoint());
        if (anchor.isEmpty() || anchor != anchorAt(press_point_)) return;
        if (anchor.startsWith(QStringLiteral("moonmark-copy:"))) {
            const auto index = anchor.sliced(QStringLiteral("moonmark-copy:").size()).toInt();
            if (index >= 0 && index < static_cast<int>(code_sources_.size())) {
                last_copy_text_ = code_sources_[static_cast<std::size_t>(index)];
                QGuiApplication::clipboard()->setText(last_copy_text_);
            }
        } else if (anchor.startsWith(QLatin1Char('#'))) {
            navigateToAnchor(QUrl::fromPercentEncoding(anchor.sliced(1).toUtf8()));
        } else {
            const QUrl url(anchor);
            if (url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https") ||
                url.scheme() == QStringLiteral("mailto")) {
                QDesktopServices::openUrl(url);
            }
        }
    }

    void wheelEvent(QWheelEvent* event) override {
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            if (event->angleDelta().y() != 0) changeZoom(event->angleDelta().y() > 0 ? 10 : -10);
            event->accept();
            return;
        }
        if (!event->pixelDelta().isNull()) {
            cancelScrollMotion();
            QTextEdit::wheelEvent(event);
            return;
        }
        if (event->angleDelta().y() != 0) {
            scroll_trace_.recordWheel();
            if (!navigation_anchor_.isEmpty()) cancelScrollMotion();
            const double wheel_step = std::max(80.0,
                static_cast<double>(verticalScrollBar()->singleStep()) * 4.0);
            const double scaled = -static_cast<double>(event->angleDelta().y()) *
                                  wheel_step / 120.0;
            wheel_fraction_ += scaled;
            const double movement = std::trunc(wheel_fraction_);
            wheel_fraction_ -= movement;
            const double distance = movement == 0.0 ? std::copysign(1.0, scaled) : movement;
            if (reducedMotion()) {
                scroll_controller_.moveDirectlyTo(verticalScrollBar()->value() +
                                                  static_cast<int>(std::lround(distance)));
            } else {
                scroll_controller_.addWheelDistance(distance);
            }
            event->accept();
            return;
        }
        QTextEdit::wheelEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        cancelScrollMotion();
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            const int key = event->key();
            if (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus || key == Qt::Key_0) {
                changeZoom(key == Qt::Key_0 ? 100 - zoom_percent_ : key == Qt::Key_Minus ? -10 : 10);
                event->accept();
                return;
            }
        }
        if (event->key() == Qt::Key_Escape && autoscroll_active_) {
            stopAutoscroll();
            event->accept();
            return;
        }
        const bool copy_shortcut = event->matches(QKeySequence::Copy) ||
            (event->key() == Qt::Key_C && event->modifiers().testFlag(Qt::ControlModifier));
        if (copy_shortcut && textCursor().hasSelection()) {
            auto selected = textCursor().selectedText();
            selected.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            selected.replace(QChar::LineSeparator, QLatin1Char('\n'));
            selected.remove(QChar::ObjectReplacementCharacter);
            selected.remove(QChar(0xFDD0));
            selected.remove(QChar(0xFDD1));
            selected.remove(QChar(0x200B));
            last_copy_text_ = selected;
            QGuiApplication::clipboard()->setText(selected);
            event->accept();
            return;
        }
        QTextEdit::keyPressEvent(event);
    }

private:
    void cancelScrollMotion() {
        navigation_anchor_.clear();
        navigation_settle_.stop();
        ++navigation_epoch_;
        scroll_controller_.cancel();
    }

    [[nodiscard]] bool reducedMotion() const {
        return qEnvironmentVariable("MOONMARK_REDUCED_MOTION") == QStringLiteral("1");
    }

    [[nodiscard]] std::optional<int> anchorDestination(const QString& anchor) {
        const auto found = anchor_positions_.constFind(anchor);
        if (found == anchor_positions_.cend()) return std::nullopt;
        QTextCursor cursor(document());
        cursor.setPosition(std::min(found.value() + 1, document()->characterCount() - 1));
        const int rendered_top = cursorRect(cursor).top() + verticalScrollBar()->value();
        const int requested = rendered_top - navigationInset();
        const int destination = std::clamp(requested, verticalScrollBar()->minimum(),
                                           verticalScrollBar()->maximum());
        navigation_target_clamped_ = destination != requested;
        return destination;
    }

    [[nodiscard]] bool hasPendingNavigationImages() const {
        return pendingNavigationImages() > 0;
    }

    void scheduleNavigationSettlement() {
        if (navigation_anchor_.isEmpty()) return;
        navigation_settle_.start(32);
    }

    void scheduleNavigationRetarget() {
        if (navigation_anchor_.isEmpty() || navigation_retarget_pending_) return;
        navigation_retarget_pending_ = true;
        QTimer::singleShot(0, this, [this] {
            navigation_retarget_pending_ = false;
            retargetNavigation();
        });
    }

    void retargetNavigation() {
        if (navigation_anchor_.isEmpty()) return;
        const auto destination = anchorDestination(navigation_anchor_);
        if (!destination.has_value()) {
            cancelScrollMotion();
            return;
        }
        if (*destination != verticalScrollBar()->value()) {
            ++navigation_retarget_count_;
            scroll_controller_.moveDirectlyTo(*destination);
        }
        navigation_corrected_generation_ = layout_generation_;
        navigation_stable_passes_ = 0;
        queueVisibleImages();
        scheduleNavigationSettlement();
    }

    void settleNavigation() {
        if (navigation_anchor_.isEmpty()) return;
        const auto destination = anchorDestination(navigation_anchor_);
        if (!destination.has_value()) {
            cancelScrollMotion();
            return;
        }
        const bool generation_changed = navigation_corrected_generation_ != layout_generation_;
        const bool needs_correction = std::abs(*destination - verticalScrollBar()->value()) > 1;
        if (needs_correction) {
            ++navigation_retarget_count_;
            scroll_controller_.moveDirectlyTo(*destination);
        }
        navigation_corrected_generation_ = layout_generation_;
        queueVisibleImages();
        if (generation_changed || needs_correction || hasPendingNavigationImages()) {
            navigation_stable_passes_ = 0;
            scheduleNavigationSettlement();
            return;
        }
        // Keep one quiet debounce interval after the last relevant layout/range
        // change so queued Qt layout notifications cannot arrive after ownership
        // has already fallen back to the generic viewport anchor.
        if (navigation_stable_passes_++ == 0) {
            scheduleNavigationSettlement();
            return;
        }
        completed_navigation_anchor_ = navigation_anchor_;
        completed_navigation_clamped_ = navigation_target_clamped_;
        navigation_anchor_.clear();
        navigation_target_clamped_ = false;
    }

    void buildBlocks(QTextCursor& cursor, std::size_t& index, int stop_kind, int depth) {
        while (index < commands_.size()) {
            const auto& command = commands_[index];
            if (command.kind == stop_kind) {
                ++index;
                return;
            }
            switch (command.kind) {
            case command_kind::begin_paragraph:
                ++index;
                buildParagraph(cursor, index, false, 0, depth);
                break;
            case command_kind::begin_heading:
                ++index;
                buildParagraph(cursor, index, true, command.level, depth, command.target);
                break;
            case command_kind::begin_list:
                buildList(cursor, index, depth);
                break;
            case command_kind::code_block:
                buildCodeBlock(cursor, command, depth);
                ++index;
                break;
            case command_kind::horizontal_rule:
                buildRule(cursor, depth);
                ++index;
                break;
            case command_kind::begin_quote:
                ++index;
                ++quote_depth_;
                buildBlocks(cursor, index, command_kind::end_quote, depth + 1);
                --quote_depth_;
                cursor.setBlockFormat(bodyBlockFormat());
                break;
            case command_kind::begin_table:
                buildTable(cursor, index, depth);
                break;
            case command_kind::image:
                buildImage(cursor, command, depth);
                ++index;
                break;
            case command_kind::raw_html:
                buildRawHtml(cursor, command, depth);
                ++index;
                break;
            default:
                ++index;
                break;
            }
        }
    }

    void beginBlock(QTextCursor& cursor, QTextBlockFormat format, int depth) {
        if (cursor.position() != 0 && !cursor.block().text().isEmpty()) {
            cursor.insertBlock();
        }
        if (depth > 0) {
            format.setLeftMargin(format.leftMargin() + depth * 22.0);
            format.setRightMargin(format.rightMargin() + 8.0);
        }
        format.setProperty(quote_depth_property, quote_depth_);
        cursor.setBlockFormat(format);
    }

    void buildParagraph(QTextCursor& cursor, std::size_t& index, bool heading, int level, int depth,
                        const QString& anchor = {}) {
        auto block = bodyBlockFormat(settings_.value("lineHeightPercent").toInt(150));
        double points = settings_.value("bodyFontPoints").toDouble(12.75);
        if (heading) {
            static constexpr double scales[] = {2.05, 1.60, 1.34, 1.18, 1.08, 1.0};
            block.setHeadingLevel(std::clamp(level, 1, 6));
            points *= scales[std::clamp(level, 1, 6) - 1];
            block.setTopMargin(level == 1 ? 21.0 : (level == 2 ? 17.0 : 13.0));
            block.setBottomMargin(level <= 2 ? 9.0 : 6.0);
            block.setLineHeight(132, QTextBlockFormat::ProportionalHeight);
        }
        beginBlock(cursor, block, depth);
        if (!anchor.isEmpty()) {
            anchor_positions_.insert(anchor, cursor.position());
            QTextCharFormat named;
            named.setAnchor(true);
            named.setAnchorNames({anchor});
            cursor.insertText(QString(QChar::ObjectReplacementCharacter), named);
        }
        while (index < commands_.size() && commands_[index].kind != command_kind::end_block) {
            insertInline(cursor, commands_[index], points, heading);
            ++index;
        }
        if (index < commands_.size()) {
            ++index;
        }
        cursor.insertBlock();
    }

    void insertInline(QTextCursor& cursor, const Command& command, double points, bool heading) {
        if (command.kind == command_kind::soft_break) {
            cursor.insertText(QStringLiteral(" "), baseCharacterFormat(points));
            return;
        }
        if (command.kind == command_kind::hard_break) {
            cursor.insertText(QString(QChar::LineSeparator), baseCharacterFormat(points));
            return;
        }
        if (command.kind == command_kind::image) {
            buildInlineImage(cursor, command);
            return;
        }
        if (command.kind == command_kind::raw_html) {
            auto raw = baseCharacterFormat(points * 0.92);
            raw.setFontFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
            raw.setForeground(QColor(colour::muted));
            cursor.insertText(command.text, raw);
            return;
        }
        if (command.kind != command_kind::text) {
            return;
        }
        auto format = baseCharacterFormat(points);
        if (quote_depth_ > 0) format.setForeground(QColor(colour::secondary));
        if (heading || (command.flags & text_style::strong) != 0) {
            format.setFontWeight(heading ? QFont::DemiBold : QFont::Bold);
        }
        if ((command.flags & text_style::emphasis) != 0) {
            format.setFontItalic(true);
        }
        if ((command.flags & text_style::strike) != 0) {
            format.setFontStrikeOut(true);
        }
        if ((command.flags & text_style::code) != 0) {
            format.setFontFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
            format.setFontPointSize(points * 0.9);
            format.setBackground(QColor(colour::inline_code));
            format.setProperty(inline_code_property, true);
            format.setForeground(QColor(colour::bright));
        }
        if ((command.flags & text_style::link) != 0) {
            format.setAnchor(true);
            format.setAnchorHref(command.target);
            format.setForeground(QColor(colour::silver));
            format.setFontUnderline(true);
        }
        cursor.insertText(command.text, format);
    }

    void buildList(QTextCursor& cursor, std::size_t& index, int depth) {
        const auto begin = commands_[index++];
        int item_number = static_cast<int>(begin.number);
        while (index < commands_.size() && commands_[index].kind != command_kind::end_list) {
            if (commands_[index].kind != command_kind::begin_item) {
                ++index;
                continue;
            }
            const auto item = commands_[index++];
            QString marker = item.text;
            if ((item.flags & text_style::checked) != 0) {
                marker = QStringLiteral("☑");
            } else if ((item.flags & text_style::unchecked) != 0) {
                marker = QStringLiteral("☐");
            } else if ((begin.flags & text_style::ordered) != 0) {
                marker = QString::number(item_number++) + QStringLiteral(".");
            }

            bool marker_inserted = false;
            while (index < commands_.size() && commands_[index].kind != command_kind::end_item) {
                if (commands_[index].kind == command_kind::begin_paragraph) {
                    ++index;
                    auto block = bodyBlockFormat(settings_.value("lineHeightPercent").toInt(150));
                    block.setLeftMargin((depth + 1) * 24.0);
                    block.setTextIndent(marker_inserted ? 0.0 : -22.0);
                    QTextOption::Tab tab;
                    tab.position = 22;
                    block.setTabPositions({tab});
                    block.setBottomMargin(4.0);
                    beginBlock(cursor, block, 0);
                    auto marker_format = baseCharacterFormat();
                    marker_format.setForeground(QColor(colour::secondary));
                    if (!marker_inserted) cursor.insertText(marker + QLatin1Char('\t'), marker_format);
                    marker_inserted = true;
                    while (index < commands_.size() &&
                           commands_[index].kind != command_kind::end_block) {
                        insertInline(cursor, commands_[index],
                                     settings_.value("bodyFontPoints").toDouble(12.75), false);
                        ++index;
                    }
                    if (index < commands_.size()) {
                        ++index;
                    }
                    cursor.insertBlock();
                } else if (commands_[index].kind == command_kind::begin_list) {
                    buildList(cursor, index, depth + 1);
                } else {
                    buildBlocks(cursor, index, command_kind::end_item, depth + 1);
                }
            }
            if (index < commands_.size() && commands_[index].kind == command_kind::end_item) {
                ++index;
            }
            if (!marker_inserted) {
                auto block = bodyBlockFormat();
                block.setLeftMargin((depth + 1) * 24.0);
                beginBlock(cursor, block, 0);
                cursor.insertText(marker, baseCharacterFormat());
                cursor.insertBlock();
            }
        }
        if (index < commands_.size()) {
            ++index;
        }
    }

    void buildCodeBlock(QTextCursor& cursor, const Command& command, int depth) {
        if (cursor.position() != 0 && !cursor.block().text().isEmpty()) {
            cursor.insertBlock();
        }
        // QTextDocument requires an outer paragraph around frames. Keep the empty
        // structural paragraph tiny rather than giving it a full body-text line.
        auto spacer = bodyBlockFormat();
        spacer.setLineHeight(1, QTextBlockFormat::FixedHeight);
        spacer.setTopMargin(0);
        spacer.setBottomMargin(0);
        cursor.setBlockFormat(spacer);
        QTextFrameFormat frame_format;
        frame_format.setBackground(QColor(colour::code));
        frame_format.setBorder(0);
        frame_format.setPadding(16.0);
        frame_format.setTopMargin(6.0);
        frame_format.setBottomMargin(13.0);
        frame_format.setLeftMargin(depth * 22.0);
        frame_format.setProperty(code_frame_property, true);
        auto* frame = cursor.insertFrame(frame_format);
        code_frames_.push_back(frame);
        QTextCursor inside(frame);

        const auto code_index = static_cast<int>(code_sources_.size());
        QString source;
        for (const auto& span : command.spans) {
            source += span.toObject().value("text").toString();
        }
        code_sources_.push_back(source);

        auto header_block = bodyBlockFormat(115);
        header_block.setTopMargin(0);
        header_block.setBottomMargin(6);
        inside.setBlockFormat(header_block);
        auto language = baseCharacterFormat(9.5);
        language.setForeground(QColor(colour::muted));
        language.setFontWeight(QFont::Normal);
        inside.insertText(command.extra.isEmpty() ? QStringLiteral("code") : command.extra.toLower(),
                          language);
        inside.insertText(QStringLiteral("   ·   "), language);
        auto copy = language;
        copy.setAnchor(true);
        copy.setAnchorHref(QStringLiteral("moonmark-copy:%1").arg(code_index));
        copy.setForeground(QColor(colour::silver));
        copy.setFontUnderline(false);
        inside.insertText(QStringLiteral("Copy"), copy);
        inside.insertBlock();

        auto separator_block = bodyBlockFormat(100);
        separator_block.setLineHeight(1, QTextBlockFormat::FixedHeight);
        separator_block.setTopMargin(0);
        separator_block.setBottomMargin(8);
        separator_block.setBackground(QColor(colour::code_separator));
        inside.setBlockFormat(separator_block);
        inside.insertText(QString(QChar(0x200B)), baseCharacterFormat(1));
        inside.insertBlock();

        auto code_block = bodyBlockFormat(142);
        code_block.setTopMargin(0);
        code_block.setBottomMargin(0);
        code_block.setNonBreakableLines(true);
        inside.setBlockFormat(code_block);
        for (const auto& value : command.spans) {
            const auto span = value.toObject();
            QTextCharFormat format;
            format.setFontFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
            format.setFontPointSize(10.5);
            format.setForeground(QColor(span.value("red").toInt(), span.value("green").toInt(),
                                        span.value("blue").toInt()));
            const int flags = span.value("flags").toInt();
            format.setFontWeight((flags & 1) != 0 ? QFont::DemiBold : QFont::Normal);
            format.setFontItalic((flags & 2) != 0);
            inside.insertText(span.value("text").toString(), format);
        }
        cursor = QTextCursor(frame->lastCursorPosition());
        cursor.movePosition(QTextCursor::End);
        cursor.setBlockFormat(bodyBlockFormat());
        cursor.setCharFormat(baseCharacterFormat());
    }

    void buildRule(QTextCursor& cursor, int depth) {
        auto block = bodyBlockFormat(100);
        block.setLineHeight(1, QTextBlockFormat::FixedHeight);
        block.setTopMargin(10);
        block.setBottomMargin(10);
        block.setLeftMargin(depth * 22.0);
        block.setBackground(QColor(colour::border));
        beginBlock(cursor, block, 0);
        cursor.insertText(QString(QChar(0x200B)), baseCharacterFormat(1));
        cursor.insertBlock();
    }

    void buildTable(QTextCursor& cursor, std::size_t& index, int depth) {
        const auto begin = commands_[index++];
        if (cursor.position() != 0 && !cursor.block().text().isEmpty()) {
            cursor.insertBlock();
        }
        auto spacer = bodyBlockFormat();
        spacer.setLineHeight(1, QTextBlockFormat::FixedHeight);
        spacer.setTopMargin(0);
        spacer.setBottomMargin(0);
        cursor.setBlockFormat(spacer);
        QTextTableFormat table_format;
        table_format.setBorder(0.5);
        table_format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        table_format.setBorderBrush(QColor(colour::table_outer));
        table_format.setBorderCollapse(true);
        table_format.setHeaderRowCount(1);
        table_format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        table_format.setCellPadding(0);
        table_format.setCellSpacing(0.0);
        table_format.setTopMargin(6.0);
        table_format.setBottomMargin(13.0);
        table_format.setLeftMargin(depth * 22.0);
        int rows = 0;
        for (std::size_t scan = index; scan < commands_.size() &&
                                       commands_[scan].kind != command_kind::end_table;
             ++scan) {
            rows += commands_[scan].kind == command_kind::begin_row ? 1 : 0;
        }
        const int columns = std::max(1, static_cast<int>(begin.number));
        std::vector<bool> numeric(static_cast<std::size_t>(columns), true);
        std::vector<qreal> minimum_width(static_cast<std::size_t>(columns), 48);
        const QFontMetricsF cell_metrics(baseCharacterFormat(11.75).font());
        int scan_column = -1;
        bool scan_header = false;
        QString cell_text;
        for (std::size_t scan = index; scan < commands_.size() &&
             commands_[scan].kind != command_kind::end_table; ++scan) {
            const auto& part = commands_[scan];
            if (part.kind == command_kind::begin_row) {
                scan_column = -1;
                scan_header = (part.flags & text_style::header) != 0;
            } else if (part.kind == command_kind::begin_cell) {
                ++scan_column;
                cell_text.clear();
            } else if (part.kind == command_kind::text) {
                cell_text += part.text;
            } else if (part.kind == command_kind::end_cell && scan_column >= 0 && scan_column < columns) {
                const auto slot = static_cast<std::size_t>(scan_column);
                minimum_width[slot] = std::max(minimum_width[slot], cell_metrics.horizontalAdvance(cell_text) + 36);
                if (!scan_header) {
                    bool number = false;
                    cell_text.toDouble(&number);
                    numeric[slot] = numeric[slot] && number;
                }
            }
        }
        QList<QTextLength> widths;
        const bool has_text_column = std::find(numeric.begin(), numeric.end(), false) != numeric.end();
        for (int column = 0; column < columns; ++column) {
            const auto slot = static_cast<std::size_t>(column);
            widths.append(numeric[slot] && has_text_column
                ? QTextLength(QTextLength::FixedLength, minimum_width[slot])
                : QTextLength(QTextLength::VariableLength, 0));
        }
        table_format.setColumnWidthConstraints(widths);
        auto* table = cursor.insertTable(std::max(1, rows), columns, table_format);
        int row = 0;
        while (index < commands_.size() && commands_[index].kind != command_kind::end_table) {
            const auto row_command = commands_[index++];
            if (row_command.kind != command_kind::begin_row) {
                continue;
            }
            int column = 0;
            while (index < commands_.size() && commands_[index].kind != command_kind::end_row) {
                const auto cell_command = commands_[index++];
                if (cell_command.kind != command_kind::begin_cell) {
                    continue;
                }
                auto cell = table->cellAt(row, std::min(column, columns - 1));
                auto cell_format = cell.format().toTableCellFormat();
                const bool header = (row_command.flags & text_style::header) != 0;
                cell_format.setTopPadding(7);
                cell_format.setBottomPadding(7);
                cell_format.setLeftPadding(14);
                cell_format.setRightPadding(14);
                if (header) cell_format.setBackground(QColor(colour::table_header));
                if (column > 0) {
                    cell_format.setLeftBorder(0.75);
                    cell_format.setLeftBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                    cell_format.setLeftBorderBrush(QColor(colour::table_column));
                }
                if (row < rows - 1) {
                    cell_format.setBottomBorder(header ? 1.0 : 0.75);
                    cell_format.setBottomBorderStyle(QTextFrameFormat::BorderStyle_Solid);
                    cell_format.setBottomBorderBrush(QColor(header ? colour::table_header_rule :
                                                                     colour::table_row));
                }
                cell.setFormat(cell_format);
                auto cell_cursor = cell.firstCursorPosition();
                auto cell_block = bodyBlockFormat(125);
                cell_block.setTopMargin(0);
                cell_block.setBottomMargin(0);
                cell_block.setAlignment(cell_command.number == 2 ? Qt::AlignRight :
                                       cell_command.number == 1 ? Qt::AlignHCenter : Qt::AlignLeft);
                cell_cursor.setBlockFormat(cell_block);
                while (index < commands_.size() && commands_[index].kind != command_kind::end_cell) {
                    insertInline(cell_cursor, commands_[index], 11.75,
                                 (row_command.flags & text_style::header) != 0);
                    ++index;
                }
                if (index < commands_.size()) {
                    ++index;
                }
                ++column;
            }
            if (index < commands_.size()) {
                ++index;
            }
            ++row;
        }
        if (index < commands_.size()) {
            ++index;
        }
        cursor = QTextCursor(table->lastCursorPosition());
        cursor.movePosition(QTextCursor::End);
        cursor.setBlockFormat(bodyBlockFormat());
        cursor.setCharFormat(baseCharacterFormat());
    }

    void buildImage(QTextCursor& cursor, const Command& command, int depth) {
        // Qt applies proportional leading to the image height, not just the font.
        // Image-only blocks use explicit margins instead of bitmap-sized line leading.
        auto block = bodyBlockFormat(100);
        block.setTopMargin(6);
        block.setBottomMargin(13);
        beginBlock(cursor, block, depth);
        buildInlineImage(cursor, command);
        cursor.insertBlock();
    }

    void buildInlineImage(QTextCursor& cursor, const Command& command) {
        const auto id = static_cast<std::uint32_t>(command.number);
        const bool allowed = command.flags == 0;
        QString message;
        if (allowed) {
            message = command.text.isEmpty() ? QStringLiteral("Loading image…")
                                             : QStringLiteral("Loading %1…").arg(command.text);
        } else if (command.flags == 1) {
            message = QStringLiteral("Image not found · %1").arg(command.target);
        } else if (command.flags == 2) {
            message = QStringLiteral("Image reference blocked · %1").arg(command.target);
        } else if (command.flags == 3) {
            message = QStringLiteral("Remote image blocked by Moonmark's privacy policy");
        } else if (command.flags == 4) {
            message = QStringLiteral("Unsupported image format · %1").arg(command.target);
        } else {
            message = QStringLiteral("Image could not be displayed");
        }

        const auto resource = QUrl(QStringLiteral("moonmark-image://%1").arg(id));
        const bool geometry_known = allowed && command.image_width > 0 && command.image_height > 0;
        document()->addResource(
            QTextDocument::ImageResource, resource,
            geometry_known ? transparentImagePlaceholder()
                           : placeholderImage(message, 900, 72, !allowed));
        QTextImageFormat format;
        format.setName(resource.toString());
        const auto display_size = geometry_known
            ? displayedImageSize(command.image_width, command.image_height)
            : QSize(std::min(900, availableImageWidth()), 72);
        format.setWidth(display_size.width());
        format.setHeight(display_size.height());
        format.setToolTip(command.text);
        const int position = cursor.position();
        cursor.insertImage(format);
        image_occurrences_.push_back(
            ImageOccurrence{id, position, false, false, !allowed,
                            command.image_width, command.image_height});
    }

    void buildRawHtml(QTextCursor& cursor, const Command& command, int depth) {
        auto block = bodyBlockFormat(145);
        block.setTopMargin(4);
        block.setBottomMargin(10);
        beginBlock(cursor, block, depth);
        auto format = baseCharacterFormat(10.0);
        format.setFontFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
        format.setForeground(QColor(colour::muted));
        cursor.insertText(command.text, format);
        cursor.insertBlock();
    }

    int availableImageWidth() const {
        return std::max(240, viewport()->width() - effectiveSideMargin() * 2 - 8);
    }

    QSize displayedImageSize(int natural_width, int natural_height) const {
        if (natural_width <= 0 || natural_height <= 0) {
            return {std::min(900, availableImageWidth()), 72};
        }
        const int width = std::max(
            1, std::min(static_cast<int>(natural_width * zoom_percent_ / 100.0),
                        availableImageWidth()));
        const double scale = static_cast<double>(width) / natural_width;
        return {width, std::max(1, static_cast<int>(natural_height * scale))};
    }

    int effectiveSideMargin() const {
        const int base = settings_.value("documentPadding").toInt(48);
        return std::min(static_cast<int>(base * zoom_percent_ / 100.0),
                        std::max(20, (viewport()->width() - 400) / 12));
    }

    void applyDocumentWidth() {
        if (document() == nullptr || document()->rootFrame() == nullptr) {
            return;
        }
        auto format = document()->rootFrame()->frameFormat();
        const auto margin = static_cast<qreal>(effectiveSideMargin());
        format.setLeftMargin(margin);
        format.setRightMargin(margin);
        format.setTopMargin(22.0 * zoom_percent_ / 100.0);
        format.setBottomMargin(34.0 * zoom_percent_ / 100.0);
        document()->rootFrame()->setFrameFormat(format);
    }

    void queueVisibleImages() {
        if (backend_ == nullptr || document() == nullptr) {
            return;
        }
        QElapsedTimer scan;
        scan.start();
        const int prefetch_ahead = std::clamp(
            900 + static_cast<int>(std::abs(scroll_controller_.velocity()) * 0.30), 900, 4200);
        const int prefetch_behind = 650;
        const bool moving_down = scroll_controller_.velocity() >= 0.0;
        for (auto& occurrence : image_occurrences_) {
            if (occurrence.requested || occurrence.loaded || occurrence.failed) {
                continue;
            }
            QTextCursor cursor(document());
            cursor.setPosition(std::min(occurrence.position, document()->characterCount() - 1));
            const auto rectangle = cursorRect(cursor);
            const int above = moving_down ? prefetch_behind : prefetch_ahead;
            const int below = moving_down ? prefetch_ahead : prefetch_behind;
            if (rectangle.bottom() < -above || rectangle.top() > viewport()->height() + below) {
                continue;
            }
            if (image_requested_.contains(occurrence.id)) {
                occurrence.requested = true;
                continue;
            }
            if (api_->queue_image(backend_, occurrence.id,
                                  static_cast<std::uint32_t>(std::max(240, availableImageWidth())))) {
                image_requested_.emplace(occurrence.id, true);
                for (auto& same : image_occurrences_) {
                    if (same.id == occurrence.id) {
                        same.requested = true;
                    }
                }
                image_poll_.start();
            }
        }
        scroll_trace_.recordImageScan(scan.nsecsElapsed() / 1000);
    }

    void scheduleImagePrefetch(bool immediate = false) {
        if (image_prefetch_.isActive() && !immediate) return;
        const int delay = immediate ? 0 : (scroll_controller_.isRunning() ? 67 : 16);
        image_prefetch_.start(delay);
    }

    void pollImages() {
        if (image_delivery_paused_for_test_) return;
        // Completed decodes can wait briefly; applying QTextImageFormats forces
        // native relayout and must not consume the rapid-scroll frame budget.
        if (scroll_controller_.isRunning()) return;
        QElapsedTimer profile;
        profile.start();
        qint64 copy_us = 0;
        qint64 geometry_us = 0;
        int count = 0;
        bool received = false;
        std::optional<ViewportAnchor> viewport_anchor;
        constexpr int maximum_results_per_tick = 4;
        while (count < maximum_results_per_tick) {
            auto result = api_->poll_image(backend_);
            if (result.id == 0) {
                break;
            }
            if (!received) {
                viewport_anchor = captureViewportAnchor();
                document()->setLayoutEnabled(false);
            }
            received = true;
            ++count;
            const auto error = fromBuffer(result.error);
            if (result.error.data != nullptr) {
                api_->buffer_free(result.error);
            }
            const auto resource = QUrl(QStringLiteral("moonmark-image://%1").arg(result.id));
            if (!error.isEmpty() || result.pixels.data == nullptr || result.width == 0 ||
                result.height == 0) {
                document()->addResource(QTextDocument::ImageResource, resource,
                                        placeholderImage(QStringLiteral("Image failed · %1").arg(error),
                                                         900, 72, true));
                for (auto& occurrence : image_occurrences_) {
                    if (occurrence.id == result.id) {
                        occurrence.failed = true;
                    }
                }
            } else {
                const auto width = static_cast<int>(result.width);
                const auto height = static_cast<int>(result.height);
                QImage view(result.pixels.data, width, height, width * 4, QImage::Format_RGBA8888);
                const auto copy_start = profile.nsecsElapsed();
                document()->addResource(QTextDocument::ImageResource, resource, view.copy());
                copy_us += (profile.nsecsElapsed() - copy_start) / 1000;
                for (auto& occurrence : image_occurrences_) {
                    if (occurrence.id == result.id) {
                        occurrence.loaded = true;
                        if (occurrence.natural_width <= 0 || occurrence.natural_height <= 0) {
                            occurrence.natural_width = width;
                            occurrence.natural_height = height;
                        }
                    }
                }
            }
            if (result.pixels.data != nullptr) {
                api_->buffer_free(result.pixels);
            }
            const auto geometry_start = profile.nsecsElapsed();
            updateImageFormats(result.id);
            geometry_us += (profile.nsecsElapsed() - geometry_start) / 1000;
        }
        if (received) {
            document()->setLayoutEnabled(true);
            if (navigation_anchor_.isEmpty() && viewport_anchor.has_value())
                restoreViewportAnchor(*viewport_anchor);
            invalidateCodeFrameBounds();
            if (!navigation_anchor_.isEmpty()) ++navigation_image_retarget_count_;
            scheduleNavigationRetarget();
            viewport()->update();
            scroll_trace_.recordImageDelivery(profile.nsecsElapsed() / 1000);
            if (qEnvironmentVariableIsSet("MOONMARK_PROFILE"))
                std::fprintf(stdout, "IMAGE_DELIVERY results=%d copy_us=%lld geometry_us=%lld total_us=%lld\n",
                             count, static_cast<long long>(copy_us), static_cast<long long>(geometry_us),
                             static_cast<long long>(profile.nsecsElapsed() / 1000));
        }
        const bool pending = std::any_of(image_occurrences_.cbegin(), image_occurrences_.cend(),
                                         [](const auto& image) {
                                             return image.requested && !image.loaded && !image.failed;
                                         });
        if (!pending) {
            image_poll_.stop();
        }
    }

    void updateImageFormats(std::uint32_t id) {
        for (const auto& occurrence : image_occurrences_) {
            if (occurrence.id != id) {
                continue;
            }
            QTextCursor cursor(document());
            cursor.setPosition(occurrence.position);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            auto format = cursor.charFormat().toImageFormat();
            const auto display_size = displayedImageSize(occurrence.natural_width,
                                                         occurrence.natural_height);
            if (format.width() == display_size.width() &&
                format.height() == display_size.height()) continue;
            format.setWidth(display_size.width());
            format.setHeight(display_size.height());
            cursor.setCharFormat(format);
        }
    }

    void resizeLoadedImages() {
        std::unordered_map<std::uint32_t, bool> changed;
        for (const auto& occurrence : image_occurrences_) {
            if (occurrence.natural_width > 0 && occurrence.natural_height > 0 &&
                !changed.contains(occurrence.id)) {
                updateImageFormats(occurrence.id);
                changed.emplace(occurrence.id, true);
            }
        }
    }

    struct ViewportAnchor {
        int position = 0;
        int offset = 0;
        bool at_top = true;
    };

    struct CodeFrameBounds {
        QPointer<QTextFrame> frame;
        QRectF bounds;
    };

    [[nodiscard]] ViewportAnchor captureViewportAnchor() const {
        if (document() == nullptr) return {};
        const auto cursor = cursorForPosition(QPoint(0, 0));
        return {cursor.position(), cursorRect(cursor).top(),
                verticalScrollBar()->value() == verticalScrollBar()->minimum()};
    }

    void restoreViewportAnchor(const ViewportAnchor& anchor) {
        if (document() == nullptr || anchor.at_top) {
            if (anchor.at_top) verticalScrollBar()->setValue(verticalScrollBar()->minimum());
            return;
        }
        QTextCursor cursor(document());
        cursor.setPosition(std::clamp(anchor.position, 0,
                                      std::max(0, document()->characterCount() - 1)));
        verticalScrollBar()->setValue(std::clamp(
            verticalScrollBar()->value() + cursorRect(cursor).top() - anchor.offset,
            verticalScrollBar()->minimum(), verticalScrollBar()->maximum()));
    }

    void invalidateCodeFrameBounds() {
        code_frame_bounds_dirty_ = true;
        if (code_frame_bounds_rebuild_pending_) return;
        code_frame_bounds_rebuild_pending_ = true;
        QTimer::singleShot(0, this, [this] {
            code_frame_bounds_rebuild_pending_ = false;
            code_frame_bounds_.clear();
            if (document() == nullptr || document()->documentLayout() == nullptr) return;
            code_frame_bounds_.reserve(code_frames_.size());
            for (const auto& frame : code_frames_) {
                if (!frame) continue;
                code_frame_bounds_.push_back(
                    {frame, document()->documentLayout()->frameBoundingRect(frame)});
            }
            std::sort(code_frame_bounds_.begin(), code_frame_bounds_.end(),
                      [](const CodeFrameBounds& left, const CodeFrameBounds& right) {
                          return left.bounds.bottom() < right.bounds.bottom();
                      });
            code_frame_bounds_dirty_ = false;
            viewport()->update();
        });
    }

    void stopAutoscroll() {
        const QRect dirty = autoscrollIndicatorRect().toAlignedRect();
        autoscroll_active_ = false;
        autoscroll_.stop();
        if (!dirty.isEmpty()) viewport()->update(dirty);
        viewport()->unsetCursor();
    }

    [[nodiscard]] QRectF autoscrollIndicatorRect() const {
        if (!autoscroll_active_) return {};
        constexpr qreal diameter = 30.0;
        return {autoscroll_anchor_.x() - diameter / 2.0,
                autoscroll_anchor_.y() - diameter / 2.0, diameter, diameter};
    }

    void paintAutoscrollAnchor(QPainter& painter) const {
        const QRectF indicator = autoscrollIndicatorRect();
        if (indicator.isEmpty()) return;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF outer = indicator.adjusted(1.5, 1.5, -1.5, -1.5);
        painter.setPen(QPen(QColor(colour::border_strong), 1.2));
        painter.setBrush(QColor(colour::raised));
        painter.drawEllipse(outer);

        const QPointF center = indicator.center();
        painter.setPen(QPen(QColor(colour::silver), 1.6, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath arrows;
        arrows.moveTo(center + QPointF(-4.5, -4.5));
        arrows.lineTo(center + QPointF(0.0, -8.5));
        arrows.lineTo(center + QPointF(4.5, -4.5));
        arrows.moveTo(center + QPointF(-4.5, 4.5));
        arrows.lineTo(center + QPointF(0.0, 8.5));
        arrows.lineTo(center + QPointF(4.5, 4.5));
        painter.drawPath(arrows);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(colour::bright));
        painter.drawEllipse(center, 2.0, 2.0);
        painter.restore();
    }

    void autoScrollTick() {
        const double distance = autoscroll_pointer_.y() - autoscroll_anchor_.y();
        if (std::abs(distance) <= 12.0) {
            return;
        }
        const double speed = std::min(48.0, std::pow((std::abs(distance) - 12.0) / 22.0, 1.35) +
                                                0.5);
        auto* bar = verticalScrollBar();
        bar->setValue(std::clamp(bar->value() + static_cast<int>(std::copysign(speed, distance)),
                                 bar->minimum(), bar->maximum()));
    }

    const MoonmarkApiTable* api_ = nullptr;
    void* backend_ = nullptr;
    moonmark::qt::SmoothScrollController scroll_controller_;
    ScrollFrameTrace scroll_trace_;
    std::vector<Command> commands_;
    std::vector<ImageOccurrence> image_occurrences_;
    std::unordered_map<std::uint32_t, bool> image_requested_;
    std::vector<QString> code_sources_;
    QString last_copy_text_;
    std::vector<QPointer<QTextFrame>> code_frames_;
    std::vector<CodeFrameBounds> code_frame_bounds_;
    bool code_frame_bounds_dirty_ = true;
    bool code_frame_bounds_rebuild_pending_ = false;
    QJsonObject settings_;
    QJsonObject metrics_;
    QString title_ = QStringLiteral("Moonmark");
    QHash<QString, int> anchor_positions_;
    QString navigation_anchor_;
    QString completed_navigation_anchor_;
    bool navigation_retarget_pending_ = false;
    bool navigation_target_clamped_ = false;
    bool completed_navigation_clamped_ = false;
    bool image_delivery_paused_for_test_ = false;
    quint64 navigation_epoch_ = 0;
    quint64 layout_generation_ = 0;
    quint64 navigation_corrected_generation_ = 0;
    int navigation_stable_passes_ = 0;
    QElapsedTimer navigation_request_elapsed_;
    qint64 navigation_lookup_us_ = 0;
    qint64 navigation_geometry_us_ = 0;
    qint64 navigation_controller_start_us_ = 0;
    qint64 navigation_first_change_us_ = -1;
    qint64 navigation_first_paint_us_ = -1;
    int navigation_retarget_count_ = 0;
    int navigation_image_retarget_count_ = 0;
    QTimer navigation_settle_;
    QTimer image_poll_;
    QTimer image_prefetch_;
    QTimer autoscroll_;
    double wheel_fraction_ = 0.0;
    bool autoscroll_active_ = false;
    QPointF autoscroll_anchor_;
    QPointF autoscroll_pointer_;
    int quote_depth_ = 0;
    int press_position_ = 0;
    QPoint press_point_;
    bool selection_drag_ = false;
    int zoom_percent_ = 100;
    bool plain_text_ = false;
    moonmark::qt::DocumentZoom zoom_layout_;
    quint64 construction_us_ = 0;
    quint64 document_construction_count_ = 0;
};

struct OpenDocumentSession {
    QString canonical_path;
    void* backend = nullptr;
    DocumentView* view = nullptr;
    QJsonArray outline;
    QFileSystemWatcher* watcher = nullptr;
    QTimer* reload_delay = nullptr;
};

class MoonmarkWindow final : public QWidget {
public:
    explicit MoonmarkWindow(const MoonmarkApiTable* api) : api_(api) {
        window_state_ = api_->window_state_new();
        setObjectName(QStringLiteral("moonmarkWindow"));
        setWindowTitle(QStringLiteral("Moonmark"));
        setWindowIcon(applicationIcon());
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint |
                       Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_NativeWindow);
        setMinimumSize(720, 480);
        resize(1280, 820);
        setAcceptDrops(true);
        buildUi();
        updateStatus();
    }

    ~MoonmarkWindow() override {
        for (auto& session : documents_) {
            delete session->view;
            delete session->watcher;
            delete session->reload_delay;
            api_->backend_free(session->backend);
        }
        api_->window_state_free(window_state_);
    }

    bool openDocument(const QString& path) {
        const QFileInfo file(path);
        if (!isSupportedDocumentFile(file)) {
            return false;
        }
        const auto canonical_path = file.canonicalFilePath();
        if (canonical_path.isEmpty()) return false;
        for (int index = 0; index < static_cast<int>(documents_.size()); ++index) {
            if (QString::compare(documents_[static_cast<std::size_t>(index)]->canonical_path,
                                 canonical_path,
#ifdef _WIN32
                                 Qt::CaseInsensitive
#else
                                 Qt::CaseSensitive
#endif
                                 ) == 0) {
                activateDocument(index);
                return true;
            }
        }

        auto session = std::make_unique<OpenDocumentSession>();
        session->canonical_path = canonical_path;
        session->backend = api_->backend_new();
        session->view = new DocumentView(api_, session->backend);
        session->view->zoomChanged = [this, raw = session.get()](int percent) {
            if (active_ == raw) zoom_label_->setText(QStringLiteral("%1%").arg(percent));
        };
        const auto utf8 = canonical_path.toUtf8();
        const auto buffer = api_->open_document(session->backend,
                                                reinterpret_cast<const std::uint8_t*>(utf8.constData()),
                                                static_cast<std::size_t>(utf8.size()));
        const auto root = jsonFromBuffer(buffer);
        api_->buffer_free(buffer);
        session->view->load(root);
        session->outline = root.value("toc").toArray();
        session->watcher = new QFileSystemWatcher(this);
        session->watcher->addPath(canonical_path);
        session->reload_delay = new QTimer(this);
        session->reload_delay->setSingleShot(true);
        session->reload_delay->setInterval(180);
        auto* raw = session.get();
        QObject::connect(session->watcher, &QFileSystemWatcher::fileChanged, this,
                         [raw](const QString&) { raw->reload_delay->start(); });
        QObject::connect(session->reload_delay, &QTimer::timeout, this,
                         [this, raw] { reloadSession(raw); });
        stack_->addWidget(session->view);
        documents_.push_back(std::move(session));
        activateDocument(static_cast<int>(documents_.size()) - 1);
        QSettings settings;
        settings.setValue(QStringLiteral("lastOpenDirectory"), file.absolutePath());
        return root.value("error").toString().isEmpty();
    }

    void runSmoke(const QString& mode) {
        if (mode == QStringLiteral("startup-arguments")) {
            QTimer::singleShot(0, this, [this] {
                QStringList names;
                names.reserve(static_cast<qsizetype>(documents_.size()));
                for (const auto& session : documents_)
                    names.push_back(QFileInfo(session->canonical_path).fileName());
                const auto joined = names.join(QLatin1Char('|')).toUtf8();
                std::fprintf(stdout, "MOONMARK_SMOKE startup_arguments=ok documents=%zu names=%s\n",
                             documents_.size(), joined.constData());
                std::fflush(stdout);
                QCoreApplication::exit(0);
            });
            return;
        }
        if (mode == QStringLiteral("autoscroll-anchor")) {
            QTimer::singleShot(250, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                const bool lifecycle = document_->testAutoscrollIndicatorLifecycle();
                const auto after = api_->backend_counters(backend_);
                const bool counters = before.parse_count == after.parse_count &&
                    before.load_count == after.load_count &&
                    before.image_request_count == after.image_request_count &&
                    constructions == document_->constructionCount();
                const bool ok = lifecycle && counters;
                std::fprintf(stdout,
                    "AUTOSCROLL_ANCHOR lifecycle=%s cursor=neutral counters=%s\n",
                    lifecycle ? "ok" : "failed", counters ? "stable" : "changed");
                std::fprintf(stdout, "MOONMARK_SMOKE autoscroll_anchor=%s\n",
                             ok ? "ok" : "failed");
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 20);
            });
            return;
        }
        if (mode == QStringLiteral("native-window")) {
            QTimer::singleShot(250, this, [this] {
                using moonmark::qt::windows::HitRole;
                const auto status = moonmark::qt::windows::nativeFrameStatus(this);
                const auto role_at = [this](QWidget* widget) {
                    return moonmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        widget->mapTo(this, widget->rect().center()), false);
                };
                const bool styles = status.thick_frame && status.system_menu &&
                    status.minimize_box && status.maximize_box;
                const bool minimize_client = role_at(minimize_) == HitRole::Client;
                const bool maximize_hit = role_at(maximize_) == HitRole::Maximize;
                const bool close_client = role_at(close_) == HitRole::Client;
                const bool captions = minimize_client && maximize_hit && close_client;
                const bool title = role_at(title_label_) == HitRole::Caption;
                const bool edges =
                    moonmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_, QPoint(1, 1), false) ==
                        HitRole::TopLeft &&
                    moonmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(width() / 2, 1), false) == HitRole::Top &&
                    moonmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(1, height() / 2), false) == HitRole::Left &&
                    moonmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(width() - 2, height() / 2), false) == HitRole::Right &&
                    moonmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(1, height() - 2), false) == HitRole::BottomLeft &&
                    moonmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(width() - 2, height() - 2), false) == HitRole::BottomRight;
                const bool fullscreen_client = moonmark::qt::windows::hitTest(
                    this, title_bar_, minimize_, maximize_, close_,
                    title_label_->mapTo(this, title_label_->rect().center()), true) ==
                    HitRole::Client;
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                const auto normal_geometry = geometry();
                const auto native_maximize_click = [this] {
#ifdef _WIN32
                    const QPoint global = maximize_->mapToGlobal(maximize_->rect().center());
                    const LPARAM point = MAKELPARAM(global.x(), global.y());
                    const HWND handle = reinterpret_cast<HWND>(winId());
                    SendMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, point);
                    SendMessageW(handle, WM_NCLBUTTONUP, HTMAXBUTTON, point);
#else
                    maximize_->click();
#endif
                };
                native_maximize_click();
                QTimer::singleShot(100, this,
                    [this, status, styles, captions, minimize_client, maximize_hit, close_client,
                     title, edges, fullscreen_client,
                     before, constructions, normal_geometry, native_maximize_click] {
                    const bool maximized = isMaximized();
                    native_maximize_click();
                    QTimer::singleShot(100, this,
                        [this, status, styles, captions, minimize_client, maximize_hit, close_client,
                         title, edges, fullscreen_client,
                         before, constructions, normal_geometry, maximized] {
                        const auto after = api_->backend_counters(backend_);
                        const bool restored = !isMaximized() && geometry() == normal_geometry;
                        const bool counters = before.parse_count == after.parse_count &&
                            before.load_count == after.load_count &&
                            before.image_request_count == after.image_request_count &&
                            constructions == document_->constructionCount();
                        const bool ok = styles && captions && title && edges &&
                            fullscreen_client && maximized && restored && counters;
                        std::fprintf(stdout,
                            "NATIVE_WINDOW styles=%s thick_frame=%s popup=%s "
                            "caption_hit=%s max_hit=%s min_client=%s close_client=%s "
                            "resize_hits=%s fullscreen_hit=%s buttons=%s counters=%s\n",
                            styles ? "ok" : "failed", status.thick_frame ? "yes" : "no",
                            status.popup ? "yes" : "no", title ? "ok" : "failed",
                            maximize_hit ? "ok" : "failed",
                            minimize_client ? "ok" : "failed",
                            close_client ? "ok" : "failed", edges ? "ok" : "failed",
                            fullscreen_client ? "client" : "failed",
                            maximized && restored ? "ok" : "failed",
                            counters ? "stable" : "changed");
                        std::fprintf(stdout, "MOONMARK_SMOKE native_window=%s\n",
                                     ok ? "ok" : "failed");
                        std::fflush(stdout);
                        QCoreApplication::exit(ok ? 0 : 19);
                    });
                });
            });
            return;
        }
        if (mode == QStringLiteral("outline-reflow")) {
            QTimer::singleShot(0, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                document_->setImageDeliveryPausedForTest(true);

                const bool rapid = document_->hasAnchor(QStringLiteral("first-target")) &&
                    document_->hasAnchor(QStringLiteral("latest-target"));
                const bool missing = document_->hasAnchor(QStringLiteral("target-after-missing"));
                const bool failed = document_->hasAnchor(QStringLiteral("target-after-failure"));
                const bool bottom = document_->hasAnchor(QStringLiteral("target-near-bottom"));
                const QString target = rapid ? QStringLiteral("latest-target")
                    : missing ? QStringLiteral("target-after-missing")
                    : failed ? QStringLiteral("target-after-failure")
                    : bottom ? QStringLiteral("target-near-bottom")
                    : QStringLiteral("target-heading");

                bool activation = true;
                bool latest_wins = true;
                quint64 first_epoch = 0;
                if (rapid) {
                    activation &= sidebar_->activateOutlineAnchorForTest(
                        QStringLiteral("first-target"));
                    first_epoch = document_->navigationEpoch();
                    latest_wins &= document_->navigationAnchor() == QStringLiteral("first-target");
                }
                activation &= sidebar_->activateOutlineAnchorForTest(target);
                latest_wins &= document_->navigationAnchor() == target &&
                    (!rapid || document_->navigationEpoch() > first_epoch);

                const int provisional_document_y = document_->anchorDocumentY(target);
                const int provisional_viewport_y = document_->anchorViewportY(target);
                const int provisional_scroll = document_->verticalScrollBar()->value();
                const int provisional_maximum = document_->verticalScrollBar()->maximum();
                const int provisional_viewport_anchor = document_->viewportAnchorPosition();
                const int pending = document_->pendingNavigationImages();
                const quint64 generation = document_->layoutGeneration();
                const bool initially_clamped = document_->navigationTargetClamped();
                const bool immediate = initially_clamped ||
                    std::abs(provisional_viewport_y - document_->navigationInset()) <= 3;
                const bool expected_pending = missing ? pending == 0 : pending > 0;
                const bool no_motion = !document_->scrollMotionRunning();

                std::fprintf(stdout,
                    "OUTLINE_REFLOW_INITIAL target=%s owner=%s generation=%llu heading_y=%d "
                    "viewport_y=%d scroll=%d maximum=%d pending=%d clamped=%s "
                    "viewport_anchor=%d immediate=%s motion=%s\n",
                    target.toUtf8().constData(), document_->navigationAnchor().toUtf8().constData(),
                    static_cast<unsigned long long>(generation), provisional_document_y,
                    provisional_viewport_y, provisional_scroll, provisional_maximum, pending,
                    initially_clamped ? "yes" : "no", provisional_viewport_anchor,
                    immediate ? "yes" : "no", no_motion ? "stopped" : "running");

                document_->setImageDeliveryPausedForTest(false);
                auto* poll = new QTimer(this);
                auto elapsed = std::make_shared<QElapsedTimer>();
                elapsed->start();
                poll->setInterval(10);
                QObject::connect(poll, &QTimer::timeout, this,
                    [this, poll, elapsed, before, constructions, target, rapid, missing, failed,
                     activation, latest_wins, expected_pending, immediate, no_motion,
                     provisional_document_y, provisional_maximum, generation] {
                    if ((document_->navigationActive() ||
                         document_->pendingImageDecodes() > 0) && elapsed->elapsed() < 6000) {
                        return;
                    }
                    poll->stop();
                    poll->deleteLater();

                    const int final_document_y = document_->anchorDocumentY(target);
                    const int final_viewport_y = document_->anchorViewportY(target);
                    const int final_scroll = document_->verticalScrollBar()->value();
                    const int final_maximum = document_->verticalScrollBar()->maximum();
                    const bool final_clamped = document_->completedNavigationClamped();
                    const bool landing = final_clamped
                        ? final_scroll == final_maximum
                        : std::abs(final_viewport_y - document_->navigationInset()) <= 3;
                    const bool geometry_changed = missing || failed ||
                        final_document_y > provisional_document_y + 100;
                    const bool owner = document_->completedNavigationAnchor() == target;
                    const bool completed = !document_->navigationActive() &&
                        document_->pendingImageDecodes() == 0 && elapsed->elapsed() < 6000;
                    const bool motion_stopped = !document_->scrollMotionRunning();
                    const bool range_expanded = final_maximum > provisional_maximum;
                    const auto after = api_->backend_counters(backend_);
                    const bool counters = before.parse_count == after.parse_count &&
                        before.load_count == after.load_count &&
                        constructions == document_->constructionCount();
                    const bool layout_changed = missing || failed ||
                        document_->layoutGeneration() > generation;
                    const bool ok = activation && latest_wins && expected_pending && immediate &&
                        no_motion && landing && geometry_changed && owner && completed &&
                        motion_stopped && counters && layout_changed;

                    std::fprintf(stdout,
                        "OUTLINE_REFLOW_FINAL target=%s owner=%s generation=%llu heading_y=%d "
                        "viewport_y=%d scroll=%d maximum=%d clamped=%s range_expanded=%s "
                        "retargets=%d image_retargets=%d viewport_anchor=%d motion=%s\n",
                        target.toUtf8().constData(),
                        document_->completedNavigationAnchor().toUtf8().constData(),
                        static_cast<unsigned long long>(document_->layoutGeneration()),
                        final_document_y, final_viewport_y, final_scroll, final_maximum,
                        final_clamped ? "yes" : "no", range_expanded ? "yes" : "no",
                        document_->navigationRetargetCount(),
                        document_->navigationImageRetargetCount(),
                        document_->viewportAnchorPosition(),
                        motion_stopped ? "stopped" : "running");
                    std::fprintf(stdout,
                        "MOONMARK_SMOKE outline_reflow=%s one_click=%s latest_wins=%s "
                        "counters=%s scenario=%s\n",
                        ok ? "ok" : "failed", owner && landing ? "ok" : "failed",
                        latest_wins ? "ok" : "failed", counters ? "stable" : "changed",
                        rapid ? "rapid" : missing ? "missing" : failed ? "failed" :
                            final_clamped ? "bottom" : "image");
                    std::fflush(stdout);
                    QCoreApplication::exit(ok ? 0 : 18);
                });
                poll->start();
            });
            return;
        }
        if (mode == QStringLiteral("scroll-profile")) {
            // Measure steady scrolling after the initial document/layout paint;
            // startup construction is covered by separate load benchmarks.
            QTimer::singleShot(500, this, [this] {
                document_->verticalScrollBar()->setValue(0);
                document_->resetScrollProfile();
                auto* burst = new QTimer(this);
                auto count = std::make_shared<int>(0);
                burst->setTimerType(Qt::PreciseTimer);
                burst->setInterval(5);
                QObject::connect(burst, &QTimer::timeout, this, [this, burst, count] {
                    if ((*count)++ < 120) {
                        const QPointF position(document_->viewport()->width() / 2.0,
                                               document_->viewport()->height() / 2.0);
                        QWheelEvent wheel(position,
                                          document_->viewport()->mapToGlobal(position.toPoint()),
                                          QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                                          Qt::ScrollUpdate, false);
                        QApplication::sendEvent(document_->viewport(), &wheel);
                        return;
                    }
                    burst->stop();
                    burst->deleteLater();
                    QTimer::singleShot(1200, this, [this] {
                        const auto summary = document_->scrollProfileSummary().toUtf8();
                        std::fprintf(stdout, "%s\n", summary.constData());
                        std::fprintf(stdout, "MOONMARK_SMOKE scroll_profile=ok\n");
                        const auto output_path = qEnvironmentVariable("MOONMARK_SCROLL_PROFILE_OUTPUT");
                        if (!output_path.isEmpty()) {
                            QFile output(output_path);
                            if (output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                                output.write(summary);
                                output.write("\nMOONMARK_SMOKE scroll_profile=ok\n");
                            }
                        }
                        std::fflush(stdout);
                        QCoreApplication::exit(0);
                    });
                });
                burst->start();
            });
            return;
        }
        if (mode == QStringLiteral("multidoc-watcher")) {
            QTimer::singleShot(250, this, [this] {
                if (documents_.size() < 2) {
                    std::fprintf(stdout, "MOONMARK_SMOKE multidoc_watcher=failed reason=document_count\n");
                    std::fflush(stdout);
                    QCoreApplication::exit(17);
                    return;
                }
                activateDocument(0);
                auto* active = documents_.front().get();
                auto* background = documents_[1].get();
                const auto active_before = api_->backend_counters(active->backend);
                const auto background_before = api_->backend_counters(background->backend);
                const auto active_path = active->canonical_path;
                QFile file(background->canonical_path);
                const bool appended = file.open(QIODevice::Append | QIODevice::Text) &&
                                      file.write("\nMoonmark background watcher update.\n") > 0;
                file.close();
                QTimer::singleShot(700, this,
                    [this, active, background, active_before, background_before, active_path, appended] {
                        const auto active_after = api_->backend_counters(active->backend);
                        const auto background_after = api_->backend_counters(background->backend);
                        const bool active_stable = active_ == active && current_path_ == active_path &&
                            active_after.load_count == active_before.load_count &&
                            active_after.parse_count == active_before.parse_count &&
                            active_after.image_request_count == active_before.image_request_count;
                        const bool background_loaded =
                            background_after.load_count == background_before.load_count + 1;
                        const bool background_parse_stable = background->view->isPlainText() &&
                            background_after.parse_count == background_before.parse_count;
                        const bool content_updated = background->view->plainText().contains(
                            QStringLiteral("Moonmark background watcher update."));
                        const bool result = appended && active_stable && background_loaded &&
                            background_parse_stable && content_updated;
                        std::fprintf(stdout,
                            "MOONMARK_SMOKE multidoc_watcher=%s active=%s background_load_delta=%lld background_parse_delta=%lld content=%s\n",
                            result ? "ok" : "failed", active_stable ? "stable" : "changed",
                            static_cast<long long>(background_after.load_count - background_before.load_count),
                            static_cast<long long>(background_after.parse_count - background_before.parse_count),
                            content_updated ? "updated" : "stale");
                        std::fflush(stdout);
                        QCoreApplication::exit(result ? 0 : 17);
                    });
            });
            return;
        }
        if (mode == QStringLiteral("motion-v2")) {
            QTimer::singleShot(80, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                document_->verticalScrollBar()->setValue(0);
                const QPointF position(document_->viewport()->width() / 2.0,
                                       document_->viewport()->height() / 2.0);
                for (int index = 0; index < 12; ++index) {
                    QWheelEvent wheel(position,
                                      document_->viewport()->mapToGlobal(position.toPoint()),
                                      QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                                      Qt::ScrollUpdate, false);
                    QApplication::sendEvent(document_->viewport(), &wheel);
                }
                const int forward_target = document_->scrollMotionTarget();
                QTimer::singleShot(40, this, [this, position, forward_target, before, constructions] {
                    for (int index = 0; index < 4; ++index) {
                        QWheelEvent wheel(position,
                                          document_->viewport()->mapToGlobal(position.toPoint()),
                                          QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                                          Qt::ScrollUpdate, false);
                        QApplication::sendEvent(document_->viewport(), &wheel);
                    }
                    const bool reversed_target = document_->scrollMotionTarget() < forward_target;
                    auto* poll = new QTimer(this);
                    auto elapsed = std::make_shared<QElapsedTimer>();
                    elapsed->start();
                    poll->setInterval(10);
                    QObject::connect(poll, &QTimer::timeout, this,
                        [this, poll, elapsed, reversed_target, before, constructions] {
                        if (document_->scrollMotionRunning() && elapsed->elapsed() < 3000) return;
                        poll->stop();
                        poll->deleteLater();
                        const auto samples = document_->scrollMotionSamples();
                        int distinct = samples.isEmpty() ? 0 : 1;
                        int largest_jump = 0;
                        for (int index = 1; index < samples.size(); ++index) {
                            const int delta = samples.at(index) - samples.at(index - 1);
                            distinct += delta != 0 ? 1 : 0;
                            largest_jump = std::max(largest_jump, std::abs(delta));
                        }
                        const bool landed = document_->verticalScrollBar()->value() ==
                                            document_->scrollMotionTarget();
                        const bool pixel_direct = document_->testPixelWheelDirect();
                        const bool home_end_direct = document_->testHomeEndDirect();
                        const QString anchor = QStringLiteral("heading-after-tall-image");
                        document_->navigateToAnchor(anchor, false);
                        const bool direct_navigation =
                            std::abs(document_->anchorViewportY(anchor) -
                                     document_->navigationInset()) <= 3;
                        const bool partial_wheel = sidebar_->testPartialOutlineWheel();
                        sidebar_->cancelOutlineScroll();
                        const bool cancelled = !sidebar_->outlineScrollRunning();
                        const auto after = api_->backend_counters(backend_);
                        const bool counters = before.parse_count == after.parse_count &&
                            before.load_count == after.load_count &&
                            constructions == document_->constructionCount();
                        const bool ok = distinct >= 3 && largest_jump > 0 && landed && pixel_direct &&
                            home_end_direct &&
                            reversed_target && direct_navigation && partial_wheel && cancelled && counters;
                        std::fprintf(stdout,
                                     "MOTION_DOCUMENT samples=%d largest_jump=%d landed=%s reversal=%s direct_navigation=%s\n",
                                     distinct, largest_jump, landed ? "ok" : "failed",
                                     reversed_target ? "ok" : "failed",
                                     direct_navigation ? "ok" : "failed");
                        std::fprintf(stdout,
                                     "MOTION_INTENTS pixel=%s home=%s end=%s anchor=direct outline_wheel=smooth\n",
                                     pixel_direct ? "direct" : "failed",
                                     home_end_direct ? "direct" : "failed",
                                     home_end_direct ? "direct" : "failed");
                        std::fprintf(stdout,
                                     "MOONMARK_SMOKE motion=%s counters=%s partial_wheel=%s cancelled=%s\n",
                                     ok ? "ok" : "failed", counters ? "stable" : "changed",
                                     partial_wheel ? "ok" : "failed", cancelled ? "ok" : "failed");
                        std::fflush(stdout);
                        QCoreApplication::exit(ok ? 0 : 16);
                    });
                    poll->start();
                });
            });
            return;
        }
        if (mode == QStringLiteral("motion")) {
            QTimer::singleShot(0, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                const QString target = QStringLiteral("heading-after-tall-image");
                document_->verticalScrollBar()->setValue(0);
                document_->navigateToAnchor(target);
                auto* document_poll = new QTimer(this);
                auto elapsed = std::make_shared<QElapsedTimer>();
                elapsed->start();
                document_poll->setInterval(10);
                QObject::connect(document_poll, &QTimer::timeout, this,
                    [this, document_poll, elapsed, before, constructions, target] {
                    if ((document_->navigationActive() || document_->pendingImageDecodes() > 0) &&
                        elapsed->elapsed() < 6000) return;
                    document_poll->stop();
                    document_poll->deleteLater();

                    const auto samples = document_->scrollMotionSamples();
                    int distinct = samples.isEmpty() ? 0 : 1;
                    int largest_jump = 0;
                    bool monotonic = true;
                    for (int index = 1; index < samples.size(); ++index) {
                        const int delta = samples.at(index) - samples.at(index - 1);
                        distinct += delta != 0 ? 1 : 0;
                        largest_jump = std::max(largest_jump, std::abs(delta));
                        monotonic &= delta >= 0;
                    }
                    const int travel = samples.size() > 1
                        ? std::abs(samples.back() - samples.front()) : 0;
                    const int landing_y = document_->anchorViewportY(target);
                    const int landing_error = std::abs(landing_y - document_->navigationInset());
                    const bool temporal = distinct >= 4 && monotonic && travel > 0 &&
                        largest_jump <= 28 &&
                        document_->verticalScrollBar()->value() == document_->scrollMotionTarget();
                    const bool landing = landing_error <= 3;
                    const bool latency = document_->navigationLookupMicros() >= 0 &&
                        document_->navigationGeometryMicros() >= 0 &&
                        document_->navigationControllerStartMicros() >= 0 &&
                        document_->navigationFirstChangeMicros() >= 0 &&
                        document_->navigationFirstPaintMicros() >=
                            document_->navigationFirstChangeMicros();
                    const bool retargeted = document_->navigationRetargetCount() > 0;
                    const bool image_retargeted = document_->navigationImageRetargetCount() > 0;
                    const bool completed = !document_->navigationActive() &&
                        document_->pendingImageDecodes() == 0 && elapsed->elapsed() < 6000;
                    const auto lookup_us = document_->navigationLookupMicros();
                    const auto geometry_us = document_->navigationGeometryMicros();
                    const auto controller_us = document_->navigationControllerStartMicros();
                    const auto first_change_us = document_->navigationFirstChangeMicros();
                    const auto first_paint_us = document_->navigationFirstPaintMicros();
                    const int retarget_count = document_->navigationRetargetCount();
                    const int image_retarget_count = document_->navigationImageRetargetCount();
                    bool direct_landings = true;
                    for (const auto& anchor : {
                             QStringLiteral("heading-after-code"),
                             QStringLiteral("heading-after-table"),
                             QStringLiteral("heading-after-small-image"),
                             QStringLiteral("heading-after-tall-image"),
                             QStringLiteral("heading-after-multiple-images")}) {
                        document_->navigateToAnchor(anchor, false);
                        const int value = document_->verticalScrollBar()->value();
                        const bool clamped = value == document_->verticalScrollBar()->minimum() ||
                                             value == document_->verticalScrollBar()->maximum();
                        direct_landings &= clamped ||
                            std::abs(document_->anchorViewportY(anchor) -
                                     document_->navigationInset()) <= 3;
                    }
                    document_->navigateToAnchor(QStringLiteral("end-clamped-heading"), false);
                    const bool end_clamped = document_->verticalScrollBar()->value() ==
                                             document_->verticalScrollBar()->maximum();

                    const QString sidebar_target = QStringLiteral("end-clamped-heading");
                    sidebar_->revealOutlineAnchor(sidebar_target, false);
                    sidebar_->revealOutlineAnchor(QStringLiteral("heading-after-prose"), false);
                    const bool partial_wheel = sidebar_->testPartialOutlineWheel();
                    sidebar_->cancelOutlineScroll();
                    sidebar_->revealOutlineAnchor(sidebar_target, true);
                    auto* sidebar_poll = new QTimer(this);
                    auto sidebar_elapsed = std::make_shared<QElapsedTimer>();
                    sidebar_elapsed->start();
                    sidebar_poll->setInterval(10);
                    QObject::connect(sidebar_poll, &QTimer::timeout, this,
                        [this, sidebar_poll, sidebar_elapsed, before, constructions, temporal,
                         landing, latency, retargeted, image_retargeted, completed, direct_landings, end_clamped,
                         partial_wheel, distinct, largest_jump, travel, landing_error, lookup_us,
                         geometry_us, controller_us, first_change_us, first_paint_us,
                         retarget_count, image_retarget_count] {
                        if (sidebar_->outlineScrollRunning() && sidebar_elapsed->elapsed() < 1500)
                            return;
                        sidebar_poll->stop();
                        sidebar_poll->deleteLater();
                        const auto sidebar_samples = sidebar_->outlineScrollSamples();
                        int sidebar_distinct = sidebar_samples.isEmpty() ? 0 : 1;
                        int sidebar_largest_jump = 0;
                        bool sidebar_monotonic = true;
                        for (int index = 1; index < sidebar_samples.size(); ++index) {
                            const int delta = sidebar_samples.at(index) - sidebar_samples.at(index - 1);
                            sidebar_distinct += delta != 0 ? 1 : 0;
                            sidebar_largest_jump = std::max(sidebar_largest_jump, std::abs(delta));
                            sidebar_monotonic &= delta >= 0;
                        }
                        const bool sidebar_motion = sidebar_distinct >= 3 && sidebar_monotonic &&
                            sidebar_largest_jump <= 28 &&
                            sidebar_->outlineScrollValue() == sidebar_->outlineScrollTarget() &&
                            sidebar_->outlineAnchorVisible(QStringLiteral("end-clamped-heading"));
                        const auto sidebar_first_change_us = sidebar_->outlineFirstChangeMicros();
                        sidebar_->revealOutlineAnchor(QStringLiteral("heading-after-prose"), true);
                        sidebar_->cancelOutlineScroll();
                        const bool sidebar_cancelled = !sidebar_->outlineScrollRunning();
                        sidebar_width_samples_.clear();
                        sidebar_requested_ = false;
                        sidebar_explicit_ = true;
                        updateSidebarVisibility();
                        QTimer::singleShot(55, this, [this] {
                            sidebar_requested_ = true;
                            updateSidebarVisibility();
                        });
                        QEventLoop width_loop;
                        QTimer::singleShot(350, &width_loop, &QEventLoop::quit);
                        width_loop.exec();
                        bool width_decreased = false;
                        bool width_increased = false;
                        int width_largest_jump = 0;
                        for (int index = 1; index < sidebar_width_samples_.size(); ++index) {
                            const int delta = sidebar_width_samples_.at(index) -
                                              sidebar_width_samples_.at(index - 1);
                            width_decreased |= delta < 0;
                            width_increased |= delta > 0;
                            width_largest_jump = std::max(width_largest_jump, std::abs(delta));
                        }
                        const bool width_retarget = width_decreased && width_increased &&
                            width_largest_jump < moonmark::style::metric::sidebar_width / 3 &&
                            sidebar_->isVisible() &&
                            sidebar_->width() == moonmark::style::metric::sidebar_width;
                        const auto after = api_->backend_counters(backend_);
                        const bool counters = before.parse_count == after.parse_count &&
                            before.load_count == after.load_count &&
                            constructions == document_->constructionCount();
                        const bool ok = temporal && landing && latency && retargeted &&
                            image_retargeted && completed && direct_landings && end_clamped &&
                            partial_wheel && sidebar_motion && sidebar_cancelled && counters;
                        const bool final_ok = ok && width_retarget;
                        std::fprintf(stdout,
                            "MOTION_DOCUMENT samples=%d travel=%d largest_jump=%d monotonic=%s landing_error=%d direct_landings=%s end_clamped=%s retargets=%d image_retargets=%d lookup_us=%lld geometry_us=%lld controller_us=%lld first_change_us=%lld first_paint_us=%lld\n",
                            distinct, travel, largest_jump, temporal ? "yes" : "no", landing_error,
                            direct_landings ? "ok" : "failed", end_clamped ? "ok" : "failed",
                            retarget_count, image_retarget_count, static_cast<long long>(lookup_us),
                            static_cast<long long>(geometry_us), static_cast<long long>(controller_us),
                            static_cast<long long>(first_change_us),
                            static_cast<long long>(first_paint_us));
                        std::fprintf(stdout,
                            "MOTION_SIDEBAR samples=%d largest_jump=%d monotonic=%s first_change_us=%lld partial_wheel=%s cancelled=%s\n",
                            sidebar_distinct, sidebar_largest_jump,
                            sidebar_monotonic ? "yes" : "no",
                            static_cast<long long>(sidebar_first_change_us),
                            partial_wheel ? "ok" : "failed", sidebar_cancelled ? "ok" : "failed");
                        std::fprintf(stdout,
                            "MOTION_SIDEBAR_WIDTH samples=%d largest_jump=%d reversed=%s final=%d\n",
                            static_cast<int>(sidebar_width_samples_.size()), width_largest_jump,
                            width_retarget ? "ok" : "failed", sidebar_->width());
                        std::fprintf(stdout,
                            "MOONMARK_SMOKE motion=%s counters=%s image_request_delta=%llu\n",
                            final_ok ? "ok" : "failed", counters ? "stable" : "changed",
                            after.image_request_count - before.image_request_count);
                        std::fflush(stdout);
                        QCoreApplication::exit(final_ok ? 0 : 16);
                    });
                    sidebar_poll->start();
                });
                document_poll->start();
            });
            return;
        }
        if (mode == QStringLiteral("multidoc")) {
            QTimer::singleShot(350, this, [this] {
                bool ok = documents_.size() >= 2;
                bool plain_present = false;
                std::vector<MoonmarkCounters> counters;
                std::vector<quint64> constructions;
                for (const auto& session : documents_) {
                    counters.push_back(api_->backend_counters(session->backend));
                    constructions.push_back(session->view->constructionCount());
                    plain_present |= session->view->isPlainText();
                }
                activateDocument(0);
                document_->changeZoom(25);
                document_->verticalScrollBar()->setValue(document_->verticalScrollBar()->maximum() / 3);
                const auto retained = document_->interactionState();
                const auto first_path = documents_.front()->canonical_path;
                const auto count_before_duplicate = documents_.size();
                openDocument(first_path);
                const bool duplicate_ok = documents_.size() == count_before_duplicate &&
                                          activeDocumentIndex() == 0;
                ok &= duplicate_ok;
                if (documents_.size() > 1) activateDocument(1);
                activateDocument(0);
                const bool state_retained = document_->zoomPercent() == 125 &&
                                            document_->interactionState().scroll == retained.scroll;
                ok &= state_retained;
                QElapsedTimer switching;
                switching.start();
                constexpr int switch_count = 40;
                for (int pass = 0; pass < switch_count; ++pass)
                    activateDocument(pass % static_cast<int>(documents_.size()));
                const auto switch_average_us = switching.nsecsElapsed() / 1000 / switch_count;
                bool counters_stable = true;
                for (std::size_t index = 0; index < documents_.size(); ++index) {
                    const auto after = api_->backend_counters(documents_[index]->backend);
                    counters_stable &= after.parse_count == counters[index].parse_count &&
                                       after.load_count == counters[index].load_count &&
                                       after.image_request_count == counters[index].image_request_count &&
                                       documents_[index]->view->constructionCount() == constructions[index];
                }
                ok &= counters_stable;
                ok &= plain_present;
                const auto original_count = documents_.size();
                closeDocument(static_cast<int>(documents_.size()) - 1);
                ok &= documents_.size() + 1 == original_count;
                while (!documents_.empty()) closeDocument(0);
                ok &= active_ == nullptr && stack_->currentIndex() == 0;
                std::fprintf(stdout,
                             "MOONMARK_SMOKE multidoc=%s duplicate=%s state=%s counters=%s plain_text=%s switch_average_us=%lld final=%s\n",
                             ok ? "ok" : "failed",
                             duplicate_ok ? "deduplicated" : "failed",
                             state_retained ? "retained" : "failed",
                             counters_stable ? "stable" : "changed",
                             plain_present ? "present" : "missing",
                             static_cast<long long>(switch_average_us),
                             active_ == nullptr && stack_->currentIndex() == 0 ? "empty" : "failed");
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 15);
            });
            return;
        }
        if (mode == QStringLiteral("plaintext")) {
            QTimer::singleShot(250, this, [this] {
                QFile source(current_path_);
                const bool opened = source.open(QIODevice::ReadOnly);
                const auto expected = opened ? QString::fromUtf8(source.readAll()) : QString{};
                const auto counters = api_->backend_counters(backend_);
                const bool ok = opened && document_->isPlainText() &&
                                document_->plainText() == expected &&
                                document_->discoveredImages() == 0 && counters.parse_count == 0;
                std::fprintf(stdout,
                             "MOONMARK_SMOKE plaintext=%s chars=%lld construction_us=%llu parse_count=%llu images=%d\n",
                             ok ? "ok" : "failed", static_cast<long long>(expected.size()),
                             static_cast<unsigned long long>(document_->constructionMicros()),
                             static_cast<unsigned long long>(counters.parse_count),
                             document_->discoveredImages());
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 14);
            });
            return;
        }
        if (mode == QStringLiteral("navigation")) {
            QTimer::singleShot(300, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                auto* tree = sidebar_->findChild<QTreeWidget*>(QStringLiteral("documentOutline"));
                auto* item = tree->topLevelItem(tree->topLevelItemCount() - 1);
                while (item && item->childCount() > 0) item = item->child(item->childCount() - 1);
                bool ok = sidebar_->isVisible() && item != nullptr;
                if (item) {
                    tree->setCurrentItem(item);
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                    QApplication::sendEvent(tree, &enter);
                    ok &= document_->verticalScrollBar()->value() > 0;
                }
                resize(800, 700);
                ok &= sidebar_->isHidden();
                resize(1280, 820);
                ok &= sidebar_->isVisible();
                enterFullscreen();
                ok &= sidebar_->isHidden() && title_bar_->isHidden();
                leaveFullscreen();
                ok &= sidebar_->isVisible() && title_bar_->isVisible();
                const auto after = api_->backend_counters(backend_);
                ok &= before.parse_count == after.parse_count && before.load_count == after.load_count &&
                      before.image_request_count == after.image_request_count && constructions == document_->constructionCount();
                std::fprintf(stdout, "MOONMARK_SMOKE navigation=%s outline_keyboard=%s transition_counters=%s\n",
                             ok ? "ok" : "failed", item ? "present" : "missing",
                             constructions == document_->constructionCount() ? "stable" : "changed");
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 13);
            });
            return;
        }
        if (mode == QStringLiteral("zoom")) {
            QTimer::singleShot(300, this, [this] {
                const bool ok = document_->testZoom();
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 12);
            });
            return;
        }
        if (mode == QStringLiteral("icon")) {
            QTimer::singleShot(50, this, [] {
                const auto icon = QGuiApplication::windowIcon();
                const auto sizes = icon.availableSizes();
                const bool ok = !icon.isNull() && !sizes.isEmpty();
                std::fprintf(stdout, "MOONMARK_SMOKE icon=%s sizes=%lld\n",
                             ok ? "ok" : "failed", static_cast<long long>(sizes.size()));
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 11);
            });
            return;
        }
        if (mode == QStringLiteral("snapshot")) {
            const auto width = qEnvironmentVariableIntValue("MOONMARK_SNAPSHOT_WIDTH");
            const auto height = qEnvironmentVariableIntValue("MOONMARK_SNAPSHOT_HEIGHT");
            if (width > 0 && height > 0) resize(width, height);
            QTimer::singleShot(500, this, [this] {
                const auto zoom = qEnvironmentVariableIntValue("MOONMARK_SNAPSHOT_ZOOM");
                if (zoom > 0 && document_) changeZoom(zoom - document_->zoomPercent());
                if (qEnvironmentVariable("MOONMARK_SNAPSHOT_NO_SIDEBAR") == QStringLiteral("1")) {
                    sidebar_requested_ = false;
                    updateSidebarVisibility();
                }
                const auto scroll = qEnvironmentVariableIntValue("MOONMARK_SNAPSHOT_SCROLL");
                if (scroll > 0 && document_) document_->verticalScrollBar()->setValue(scroll);
                if (document_ && qEnvironmentVariable("MOONMARK_SNAPSHOT_SELECTION") == QStringLiteral("1"))
                    document_->selectAll();
                if (qEnvironmentVariable("MOONMARK_SNAPSHOT_MENU") == QStringLiteral("1")) {
                    auto* menu = findChild<QMenu*>(QStringLiteral("documentMenu"));
                    menu->popup(mapToGlobal(QPoint(this->width() - 300, 40)));
                    menu->setActiveAction(menu->actions().first());
                }
                QTimer::singleShot(300, this, [this] {
                    auto name = qEnvironmentVariable("MOONMARK_SNAPSHOT_NAME", "moonmark-ui");
                    for (auto& character : name) {
                        if (!character.isLetterOrNumber() && character != QLatin1Char('-'))
                            character = QLatin1Char('_');
                    }
                    QDir::current().mkpath(QStringLiteral("out/visual/dev7"));
                    const auto output = QDir::current().absoluteFilePath(
                        QStringLiteral("out/visual/dev7/%1.png").arg(name));
                    const auto pixels = qEnvironmentVariable("MOONMARK_SNAPSHOT_MENU") == QStringLiteral("1")
                        ? findChild<QMenu*>(QStringLiteral("documentMenu"))->grab() : grab();
                    const bool saved = pixels.save(output, "PNG");
                    std::fprintf(stdout, "MOONMARK_SMOKE snapshot=%s path=%s\n",
                                 saved ? "ok" : "failed", output.toUtf8().constData());
                    std::fflush(stdout);
                    QCoreApplication::exit(saved ? 0 : 10);
                });
            });
            return;
        }
        if (mode == QStringLiteral("style")) {
            QTimer::singleShot(180, this, [this] {
                const bool ok = document_->testDocumentStyle() && document_->testSelectionCopy();
                std::fprintf(stdout, "MOONMARK_SMOKE document_style=%s\n", ok ? "ok" : "failed");
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 11);
            });
            return;
        }
        if (mode == QStringLiteral("render")) {
            QTimer::singleShot(180, this, [this] {
                const auto counters = api_->backend_counters(backend_);
                const bool selection_copy_ok = document_->testSelectionCopy();
                const bool code_copy_ok = document_->testCodeCopy();
                const bool ok = !current_path_.isEmpty() && document_->plainText().size() > 40 &&
                                counters.parse_count == 1 && counters.load_count == 1 &&
                                document_->constructionCount() == 1 && selection_copy_ok && code_copy_ok;
                std::fprintf(stdout,
                             "MOONMARK_SMOKE render=%s chars=%lld construction_us=%llu parse=%llu load=%llu selection_copy=%s code_copy=%s\n",
                             ok ? "ok" : "failed",
                             static_cast<long long>(document_->plainText().size()),
                             static_cast<unsigned long long>(document_->constructionMicros()),
                             static_cast<unsigned long long>(counters.parse_count),
                             static_cast<unsigned long long>(counters.load_count),
                             selection_copy_ok ? "ok" : "failed", code_copy_ok ? "ok" : "failed");
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 4);
            });
            return;
        }
        if (mode == QStringLiteral("layout")) {
            QTimer::singleShot(300, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                resize(width() + 113, height() + 67);
                document_->changeZoom(10);
                document_->changeZoom(-10);
                showMaximized();
                QTimer::singleShot(80, this, [this, before, constructions] {
                    enterFullscreen();
                    QTimer::singleShot(80, this, [this, before, constructions] {
                        leaveFullscreen();
                        QTimer::singleShot(100, this, [this, before, constructions] {
                            const auto after = api_->backend_counters(backend_);
                            const bool ok = before.parse_count == after.parse_count &&
                                            before.load_count == after.load_count &&
                                            before.image_request_count == after.image_request_count &&
                                            constructions == document_->constructionCount() &&
                                            isMaximized();
                            std::fprintf(stdout,
                                         "MOONMARK_SMOKE layout=%s parse_delta=%lld load_delta=%lld construction_delta=%lld image_request_delta=%lld restored=%s\n",
                                         ok ? "ok" : "failed",
                                         static_cast<long long>(after.parse_count - before.parse_count),
                                         static_cast<long long>(after.load_count - before.load_count),
                                         static_cast<long long>(document_->constructionCount() -
                                                                constructions),
                                         static_cast<long long>(after.image_request_count -
                                                                before.image_request_count),
                                         isMaximized() ? "maximized" : "normal");
                            std::fflush(stdout);
                            QCoreApplication::exit(ok ? 0 : 5);
                        });
                    });
                });
            });
            return;
        }
        if (mode == QStringLiteral("maximize")) {
            QTimer::singleShot(300, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                const auto normal_geometry = geometry();
                showMaximized();
                QTimer::singleShot(100, this, [this, before, constructions, normal_geometry] {
                    const bool maximized = isMaximized();
                    showNormal();
                    QTimer::singleShot(100, this,
                                       [this, before, constructions, normal_geometry, maximized] {
                        const auto after = api_->backend_counters(backend_);
                        const bool geometry_restored = geometry() == normal_geometry;
                        const bool ok = maximized && !isMaximized() && geometry_restored &&
                                        before.parse_count == after.parse_count &&
                                        before.load_count == after.load_count &&
                                        before.image_request_count == after.image_request_count &&
                                        constructions == document_->constructionCount();
                        std::fprintf(stdout,
                                     "MOONMARK_SMOKE maximize=%s parse_delta=%lld load_delta=%lld construction_delta=%lld image_request_delta=%lld geometry=%s\n",
                                     ok ? "ok" : "failed",
                                     static_cast<long long>(after.parse_count - before.parse_count),
                                     static_cast<long long>(after.load_count - before.load_count),
                                     static_cast<long long>(document_->constructionCount() -
                                                            constructions),
                                     static_cast<long long>(after.image_request_count -
                                                            before.image_request_count),
                                     geometry_restored ? "restored" : "changed");
                        std::fflush(stdout);
                        QCoreApplication::exit(ok ? 0 : 9);
                    });
                });
            });
            return;
        }
        if (mode == QStringLiteral("layout-normal")) {
            QTimer::singleShot(300, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                const auto normal_geometry = geometry();
                enterFullscreen();
                QTimer::singleShot(80, this, [this, before, constructions, normal_geometry] {
                    leaveFullscreen();
                    QTimer::singleShot(100, this,
                                       [this, before, constructions, normal_geometry] {
                        const auto after = api_->backend_counters(backend_);
                        const bool geometry_restored = geometry() == normal_geometry;
                        const bool ok = before.parse_count == after.parse_count &&
                                        before.load_count == after.load_count &&
                                        before.image_request_count == after.image_request_count &&
                                        constructions == document_->constructionCount() &&
                                        !isMaximized() && geometry_restored;
                        std::fprintf(stdout,
                                     "MOONMARK_SMOKE layout_normal=%s parse_delta=%lld load_delta=%lld construction_delta=%lld image_request_delta=%lld restored=%s geometry=%s\n",
                                     ok ? "ok" : "failed",
                                     static_cast<long long>(after.parse_count - before.parse_count),
                                     static_cast<long long>(after.load_count - before.load_count),
                                     static_cast<long long>(document_->constructionCount() -
                                                            constructions),
                                     static_cast<long long>(after.image_request_count -
                                                            before.image_request_count),
                                     isMaximized() ? "maximized" : "normal",
                                     geometry_restored ? "restored" : "changed");
                        std::fflush(stdout);
                        QCoreApplication::exit(ok ? 0 : 7);
                    });
                });
            });
            return;
        }
        if (mode == QStringLiteral("watcher")) {
            const auto before = api_->backend_counters(backend_);
            QTimer::singleShot(100, this, [this, before] {
                QFile file(current_path_);
                const bool appended = file.open(QIODevice::Append | QIODevice::Text) &&
                                      file.write("\nMoonmark watcher smoke update.\n") > 0;
                file.close();
                QTimer::singleShot(700, this, [this, before, appended] {
                    const auto after = api_->backend_counters(backend_);
                    const bool ok = appended && after.load_count > before.load_count &&
                                    after.parse_count > before.parse_count;
                    std::fprintf(stdout,
                                 "MOONMARK_SMOKE watcher=%s load_delta=%lld parse_delta=%lld\n",
                                 ok ? "ok" : "failed",
                                 static_cast<long long>(after.load_count - before.load_count),
                                 static_cast<long long>(after.parse_count - before.parse_count));
                    std::fflush(stdout);
                    QCoreApplication::exit(ok ? 0 : 8);
                });
            });
            return;
        }
        if (mode == QStringLiteral("images") || mode == QStringLiteral("image-geometry")) {
            const bool geometry = mode == QStringLiteral("image-geometry");
            const bool initial_geometry_ok = !geometry || document_->testImageGeometry();
            document_->queueAllImagesForSmoke();
            auto* deadline = new QTimer(this);
            deadline->setInterval(25);
            auto* elapsed = new QElapsedTimer;
            elapsed->start();
            QObject::connect(deadline, &QTimer::timeout, this,
                             [this, deadline, elapsed, geometry, pass = 0,
                              geometry_ok = initial_geometry_ok]() mutable {
                const bool finished = document_->pendingImageDecodes() == 0;
                const bool timed_out = elapsed->elapsed() > 20000;
                if (!finished && !timed_out) {
                    return;
                }
                const auto completion_ms = elapsed->elapsed();
                if (geometry) {
                    geometry_ok &= document_->testImageGeometry();
                    if (pass++ == 0) {
                        resize(800, 700);
                        geometry_ok &= document_->testImageGeometry();
                        resize(1280, 820);
                        geometry_ok &= document_->testImageGeometry();
                        reloadDocument();
                        document_->queueAllImagesForSmoke();
                        return;
                    }
                    for (const int zoom : {125, 150, 100, 80, 100}) {
                        document_->changeZoom(zoom - document_->zoomPercent());
                        geometry_ok &= document_->testImageGeometry();
                    }
                }
                const bool ok = finished && document_->failedImageDecodes() == 0 && geometry_ok &&
                                document_->loadedImages() > 0 && document_->testZoom();
                const auto counters = api_->backend_counters(backend_);
                std::fprintf(stdout,
                             "MOONMARK_SMOKE images=%s discovered=%d loaded=%d failed=%d pending=%d requests=%llu cache_bytes=%llu completion_ms=%lld elapsed_ms=%lld\n",
                             ok ? "ok" : "failed", document_->discoveredImages(),
                             document_->loadedImages(), document_->failedImageDecodes(),
                             document_->pendingImageDecodes(),
                             static_cast<unsigned long long>(counters.image_request_count),
                             static_cast<unsigned long long>(counters.image_cache_bytes),
                             static_cast<long long>(completion_ms),
                             static_cast<long long>(elapsed->elapsed()));
                std::fflush(stdout);
                deadline->stop();
                delete elapsed;
                QCoreApplication::exit(ok ? 0 : 6);
            });
            deadline->start();
        }
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        updateSidebarVisibility();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_F11) {
            toggleFullscreen();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape && fullscreen_) {
            leaveFullscreen();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Open)) {
            chooseDocument();
            event->accept();
            return;
        }
        if (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_W && active_) {
            closeDocument(activeDocumentIndex());
            event->accept();
            return;
        }
        if (event->modifiers().testFlag(Qt::ControlModifier) && event->key() == Qt::Key_Tab &&
            documents_.size() > 1) {
            const int direction = event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1;
            const int count = static_cast<int>(documents_.size());
            activateDocument((activeDocumentIndex() + direction + count) % count);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_F5) {
            reloadDocument();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_F12) {
            diagnostics_ = !diagnostics_;
            updateStatus();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent* event) override {
        bool opened = false;
        for (const auto& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) opened |= openDocument(url.toLocalFile());
        }
        if (opened) event->acceptProposedAction();
    }

    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange && !fullscreen_) {
            api_->window_set_mode(window_state_, isMaximized() ? 1 : 0);
            maximize_->setMaximized(isMaximized());
        }
    }

#ifdef _WIN32
    bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result) override {
        Q_UNUSED(event_type);
        if (moonmark::qt::windows::handleNativeFrameEvent(
                this, title_bar_, minimize_, maximize_, close_, fullscreen_, message, result))
            return true;
        return QWidget::nativeEvent(event_type, message, result);
    }
#endif

private:
    void updateSidebarVisibility() {
        if (sidebar_ == nullptr) return;
        const bool visible = !fullscreen_ && sidebar_requested_ && (sidebar_explicit_ || width() >= 1000);
        const bool reduced = qEnvironmentVariable("MOONMARK_REDUCED_MOTION") == QStringLiteral("1");
        if (reduced || !isVisible()) {
            sidebar_animation_.stop();
            sidebar_target_visible_ = visible;
            sidebar_->setFixedWidth(visible ? moonmark::style::metric::sidebar_width : 0);
            sidebar_->setVisible(visible);
            if (open_ != nullptr) open_->setVisible(!visible);
            if (title_symbol_ != nullptr) title_symbol_->setVisible(!visible);
            return;
        }
        if (sidebar_animation_.state() == QAbstractAnimation::Running &&
            sidebar_target_visible_ == visible) return;
        const int target = visible ? moonmark::style::metric::sidebar_width : 0;
        const int current = sidebar_->isVisible() ? sidebar_->width() : 0;
        if (current != target || sidebar_->isVisible() != visible) {
            sidebar_animation_.stop();
            sidebar_target_visible_ = visible;
            sidebar_->setFixedWidth(current);
            sidebar_->show();
            if (open_ != nullptr) open_->hide();
            if (title_symbol_ != nullptr) title_symbol_->hide();
            const int full_duration = visible ? 170 : 150;
            const int remaining = std::abs(target - current);
            sidebar_animation_.setStartValue(current);
            sidebar_animation_.setEndValue(target);
            sidebar_animation_.setDuration(std::max(60, full_duration * remaining /
                                                        moonmark::style::metric::sidebar_width));
            sidebar_animation_.setEasingCurve(QEasingCurve::InOutQuad);
            sidebar_animation_.start();
            return;
        }
        if (open_ != nullptr) open_->setVisible(!visible);
        if (title_symbol_ != nullptr) title_symbol_->setVisible(!visible);
    }

    void buildUi() {
        auto* shell = new QHBoxLayout(this);
        shell->setContentsMargins(0, 0, 0, 0);
        shell->setSpacing(0);
        sidebar_ = new moonmark::qt::DocumentSidebar;
        QObject::connect(&sidebar_animation_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant& width) {
            const int value = width.toInt();
            sidebar_width_samples_.push_back(value);
            sidebar_->setFixedWidth(value);
        });
        QObject::connect(&sidebar_animation_, &QVariantAnimation::finished, this, [this] {
            sidebar_->setFixedWidth(sidebar_target_visible_ ? moonmark::style::metric::sidebar_width : 0);
            sidebar_->setVisible(sidebar_target_visible_);
            if (open_ != nullptr) open_->setVisible(!sidebar_target_visible_);
            if (title_symbol_ != nullptr) title_symbol_->setVisible(!sidebar_target_visible_);
        });
        sidebar_->open = [this] { chooseDocument(); };
        sidebar_->reload = [this] { reloadDocument(); };
        sidebar_->navigate = [this](const QString& anchor) {
            if (document_) {
                document_->navigateToAnchor(anchor);
                document_->setFocus(Qt::OtherFocusReason);
            }
        };
        sidebar_->activate_document = [this](int index) { activateDocument(index); };
        sidebar_->close_document = [this](int index) { closeDocument(index); };
        shell->addWidget(sidebar_);
        auto* content = new QWidget;
        shell->addWidget(content, 1);
        auto* root = new QVBoxLayout(content);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        title_bar_ = new moonmark::qt::MoonTitleBar(this);
        auto* title_layout = new QHBoxLayout(title_bar_);
        title_layout->setContentsMargins(14, 0, 0, 0);
        title_layout->setSpacing(9);

        auto* navigation = new MoonButton(QString());
        navigation->setFixedWidth(32);
        navigation->setAccessibleName(QStringLiteral("Toggle document sidebar"));
        navigation->setToolTip(QStringLiteral("Show or hide document sidebar"));
        QPixmap navigation_icon(18, 18);
        navigation_icon.fill(Qt::transparent);
        QPainter icon_painter(&navigation_icon);
        icon_painter.setPen(QPen(QColor(colour::secondary), 1));
        icon_painter.drawRoundedRect(QRectF(2, 3, 14, 12), 2, 2);
        icon_painter.drawLine(7, 3, 7, 15);
        icon_painter.end();
        navigation->setIcon(QIcon(navigation_icon));
        QObject::connect(navigation, &QPushButton::clicked, this, [this] {
            sidebar_requested_ = !sidebar_->isVisible();
            sidebar_explicit_ = true;
            updateSidebarVisibility();
        });
        title_layout->addWidget(navigation);
        title_symbol_ = new QLabel;
        title_symbol_->setPixmap(QApplication::windowIcon().pixmap(18, 18));
        title_symbol_->setAttribute(Qt::WA_TransparentForMouseEvents);
        title_layout->addWidget(title_symbol_);

        title_label_ = new ElidingLabel(QStringLiteral("Moonmark"));
        title_label_->setObjectName(QStringLiteral("documentTitle"));
        title_label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        title_label_->setMinimumWidth(60);
        title_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        title_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        title_layout->addWidget(title_label_, 1);

        document_actions_ = new QWidget;
        auto* actions = new QHBoxLayout(document_actions_);
        actions->setContentsMargins(0, 0, 6, 0);
        actions->setSpacing(2);
        open_ = new MoonButton(QStringLiteral("Open"));
        open_->setAccessibleName(QStringLiteral("Open document"));
        open_->setToolTip(QStringLiteral("Open Markdown or text file (Ctrl+O)"));
        QObject::connect(open_, &QPushButton::clicked, this, [this] { chooseDocument(); });
        actions->addWidget(open_);
        actions->addSpacing(10);
        zoom_out_ = new MoonButton(QStringLiteral("−"));
        zoom_out_->setFixedWidth(28);
        zoom_out_->setAccessibleName(QStringLiteral("Zoom out"));
        zoom_label_ = new MoonButton(QStringLiteral("100%"));
        zoom_label_->setObjectName(QStringLiteral("zoomValue"));
        zoom_label_->setFixedWidth(52);
        zoom_label_->setAccessibleName(QStringLiteral("Choose document zoom"));
        zoom_label_->setToolTip(QStringLiteral("Choose zoom · Ctrl+mouse wheel · Ctrl+0 resets"));
        auto* zoom_menu = new QMenu(zoom_label_);
        for (const int percent : {80, 100, 125, 150, 200})
            zoom_menu->addAction(QStringLiteral("%1%").arg(percent), this,
                                 [this, percent] {
                if (document_) changeZoom(percent - document_->zoomPercent());
            });
        QObject::connect(zoom_label_, &QPushButton::clicked, this, [this, zoom_menu] {
            zoom_menu->popup(zoom_label_->mapToGlobal(QPoint(0, zoom_label_->height() + 4)));
        });
        zoom_in_ = new MoonButton(QStringLiteral("+"));
        zoom_in_->setFixedWidth(28);
        zoom_in_->setAccessibleName(QStringLiteral("Zoom in"));
        QObject::connect(zoom_out_, &QPushButton::clicked, this, [this] { changeZoom(-10); });
        QObject::connect(zoom_in_, &QPushButton::clicked, this, [this] { changeZoom(10); });
        actions->addWidget(zoom_out_);
        actions->addWidget(zoom_label_);
        actions->addWidget(zoom_in_);
        auto* more = new MoonButton(QStringLiteral("⋯"));
        more->setAccessibleName(QStringLiteral("Document actions"));
        more->setToolTip(QStringLiteral("Document actions"));
        more->setFixedWidth(32);
        auto* menu = new QMenu(more);
        menu->setObjectName(QStringLiteral("documentMenu"));
        reload_action_ = menu->addAction(QStringLiteral("Reload\tF5"), this,
                                         [this] { reloadDocument(); });
        menu->addAction(QStringLiteral("Reset zoom"), this,
                        [this] { if (document_) changeZoom(100 - document_->zoomPercent()); });
        menu->addSeparator();
        menu->addAction(QStringLiteral("Fullscreen\tF11"), this,
                        [this] { toggleFullscreen(); });
        menu->addAction(QStringLiteral("Diagnostics\tF12"), this, [this] {
            diagnostics_ = !diagnostics_;
            updateStatus();
        });
        QObject::connect(more, &QPushButton::clicked, this, [more, menu] {
            menu->popup(more->mapToGlobal(QPoint(more->width() - menu->sizeHint().width(),
                                                more->height() + 4)));
        });
        actions->addSpacing(4);
        actions->addWidget(more);
        title_layout->addWidget(document_actions_);
        document_actions_->hide();

        using Caption = moonmark::qt::CaptionButton;
        minimize_ = new Caption(Caption::Action::Minimize);
        maximize_ = new Caption(Caption::Action::Maximize);
        close_ = new Caption(Caption::Action::Close);
        QObject::connect(minimize_, &QAbstractButton::clicked, this, [this] { showMinimized(); });
        QObject::connect(maximize_, &QAbstractButton::clicked, this,
                         [this] { isMaximized() ? showNormal() : showMaximized(); });
        QObject::connect(close_, &QAbstractButton::clicked, this, &QWidget::close);
        auto* captions = new QHBoxLayout;
        captions->setContentsMargins(0, 0, 0, 0);
        captions->setSpacing(0);
        captions->addWidget(minimize_);
        captions->addWidget(maximize_);
        captions->addWidget(close_);
        title_layout->addLayout(captions);
        root->addWidget(title_bar_);

        stack_ = new QStackedWidget;
        stack_->setObjectName(QStringLiteral("documentStack"));
        auto* empty = new QWidget;
        auto* empty_layout = new QVBoxLayout(empty);
        empty_layout->addStretch(2);
        auto* prompt = new QWidget;
        auto* prompt_layout = new QVBoxLayout(prompt);
        prompt_layout->setContentsMargins(0, 0, 0, 0);
        prompt_layout->setSpacing(10);
        auto* identity = new QHBoxLayout;
        identity->setSpacing(12);
        auto* empty_symbol = new QLabel;
        empty_symbol->setPixmap(QApplication::windowIcon().pixmap(32, 32));
        auto* empty_title = new QLabel(QStringLiteral("Ready to read"));
        empty_title->setObjectName(QStringLiteral("emptyTitle"));
        identity->addWidget(empty_symbol);
        identity->addWidget(empty_title);
        identity->addStretch();
        prompt_layout->addLayout(identity);
        auto* empty_hint = new QLabel(QStringLiteral("Open a Markdown or text file, or drop one here."));
        empty_hint->setObjectName(QStringLiteral("emptyHint"));
        prompt_layout->addWidget(empty_hint);
        auto* empty_open = new MoonButton(QStringLiteral("Open document"));
        empty_open->setObjectName(QStringLiteral("emptyOpen"));
        empty_open->setAccessibleName(QStringLiteral("Open document"));
        empty_open->setToolTip(QStringLiteral("Ctrl+O"));
        QObject::connect(empty_open, &QPushButton::clicked, this, [this] { chooseDocument(); });
        auto* open_row = new QHBoxLayout;
        open_row->setContentsMargins(0, 4, 0, 0);
        open_row->addWidget(empty_open);
        auto* shortcut = new QLabel(QStringLiteral("Ctrl+O"));
        shortcut->setObjectName(QStringLiteral("emptyHint"));
        open_row->addSpacing(8);
        open_row->addWidget(shortcut);
        open_row->addStretch();
        prompt_layout->addLayout(open_row);
        QTimer::singleShot(0, empty_open, [empty_open] {
            if (empty_open->isVisible()) empty_open->setFocus(Qt::OtherFocusReason);
        });
        empty_layout->addWidget(prompt, 0, Qt::AlignHCenter);
        empty_layout->addStretch(3);
        stack_->addWidget(empty);

        root->addWidget(stack_, 1);

        diagnostics_bar_ = new QLabel;
        diagnostics_bar_->setObjectName(QStringLiteral("diagnosticsBar"));
        diagnostics_bar_->setFixedHeight(26);
        diagnostics_bar_->setContentsMargins(14, 0, 14, 0);
        diagnostics_bar_->hide();
        root->addWidget(diagnostics_bar_);
        updateSidebarVisibility();
    }


    void chooseDocument() {
        QSettings settings;
        auto initial = settings.value(QStringLiteral("lastOpenDirectory")).toString();
        if (initial.isEmpty() || !QFileInfo(initial).isDir()) {
            initial = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        }
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open document"), initial,
            QStringLiteral("Moonmark documents (*.md *.markdown *.txt);;Markdown files (*.md *.markdown);;Text files (*.txt);;All files (*)"));
        if (!path.isEmpty()) {
            openDocument(path);
        }
    }

    void reloadDocument() {
        reloadSession(active_);
    }

    void changeZoom(int delta) {
        if (!document_) return;
        document_->changeZoom(delta);
        zoom_label_->setText(QStringLiteral("%1%").arg(document_->zoomPercent()));
        updateStatus();
    }

    void toggleFullscreen() {
        fullscreen_ ? leaveFullscreen() : enterFullscreen();
    }

    void enterFullscreen() {
        pre_fullscreen_maximized_ = isMaximized();
        api_->window_set_mode(window_state_, pre_fullscreen_maximized_ ? 1 : 0);
        api_->window_enter_fullscreen(window_state_);
        fullscreen_ = true;
        title_bar_->hide();
        sidebar_->hide();
        document_actions_->hide();
        diagnostics_bar_->hide();
        showFullScreen();
    }

    void leaveFullscreen() {
        const auto restored = api_->window_leave_fullscreen(window_state_);
        fullscreen_ = false;
        title_bar_->show();
        updateSidebarVisibility();
        document_actions_->setVisible(!current_path_.isEmpty());
        diagnostics_bar_->setVisible(diagnostics_);
        if (restored == 1 || pre_fullscreen_maximized_) {
            showMaximized();
        } else {
            showNormal();
        }
#ifdef _WIN32
        QTimer::singleShot(0, this, [this] {
            moonmark::qt::windows::installNativeFrame(this);
        });
#endif
    }

    void updateStatus() {
        if (!document_ || !backend_) {
            diagnostics_bar_->clear();
            diagnostics_bar_->setVisible(false);
            return;
        }
        const auto counters = api_->backend_counters(backend_);
        if (diagnostics_) {
            diagnostics_bar_->setText(
                QStringLiteral("%1/%2 images · revision/load %3 · parsed %4× · document %5 µs · cache %6 MiB")
                    .arg(document_->loadedImages())
                    .arg(document_->discoveredImages())
                    .arg(counters.load_count)
                    .arg(counters.parse_count)
                    .arg(document_->constructionMicros())
                    .arg(static_cast<double>(counters.image_cache_bytes) / (1024.0 * 1024.0), 0,
                         'f', 1));
        }
        diagnostics_bar_->setVisible(diagnostics_ && !fullscreen_);
    }

    void reloadSession(OpenDocumentSession* session) {
        if (!session || !QFileInfo::exists(session->canonical_path)) return;
        const auto state = session->view->interactionState();
        const auto utf8 = session->canonical_path.toUtf8();
        const auto buffer = api_->open_document(
            session->backend, reinterpret_cast<const std::uint8_t*>(utf8.constData()),
            static_cast<std::size_t>(utf8.size()));
        const auto root = jsonFromBuffer(buffer);
        api_->buffer_free(buffer);
        session->view->load(root);
        session->view->restoreInteractionState(state);
        session->outline = root.value("toc").toArray();
        if (!session->watcher->files().contains(session->canonical_path))
            session->watcher->addPath(session->canonical_path);
        if (active_ == session) {
            sidebar_->setOutline(session->outline);
            updateStatus();
        }
    }

    void activateDocument(int index) {
        if (index < 0 || index >= static_cast<int>(documents_.size())) return;
        active_ = documents_[static_cast<std::size_t>(index)].get();
        backend_ = active_->backend;
        document_ = active_->view;
        current_path_ = active_->canonical_path;
        const QFileInfo file(current_path_);
        title_label_->setFullText(file.dir().dirName() + QStringLiteral("   /   ") + file.fileName());
        title_label_->setToolTip(current_path_);
        setWindowTitle(QStringLiteral("%1 — Moonmark").arg(file.fileName()));
        stack_->setCurrentWidget(document_);
        document_->setFocus(Qt::OtherFocusReason);
        reload_action_->setEnabled(true);
        document_actions_->setVisible(!fullscreen_);
        zoom_label_->setText(QStringLiteral("%1%").arg(document_->zoomPercent()));
        refreshSidebar();
        updateStatus();
    }

    void closeDocument(int index) {
        if (index < 0 || index >= static_cast<int>(documents_.size())) return;
        auto* closing = documents_[static_cast<std::size_t>(index)].get();
        const bool was_active = closing == active_;
        stack_->removeWidget(closing->view);
        delete closing->view;
        delete closing->watcher;
        delete closing->reload_delay;
        api_->backend_free(closing->backend);
        documents_.erase(documents_.begin() + index);
        if (!documents_.empty()) {
            const int replacement = was_active ? std::min(index, static_cast<int>(documents_.size()) - 1)
                                               : activeDocumentIndex();
            activateDocument(std::max(0, replacement));
            return;
        }
        active_ = nullptr;
        backend_ = nullptr;
        document_ = nullptr;
        current_path_.clear();
        stack_->setCurrentIndex(0);
        title_label_->setFullText(QStringLiteral("Moonmark"));
        title_label_->setToolTip({});
        setWindowTitle(QStringLiteral("Moonmark"));
        reload_action_->setEnabled(false);
        document_actions_->hide();
        zoom_label_->setText(QStringLiteral("100%"));
        refreshSidebar();
        updateStatus();
    }

    [[nodiscard]] int activeDocumentIndex() const {
        for (int index = 0; index < static_cast<int>(documents_.size()); ++index)
            if (documents_[static_cast<std::size_t>(index)].get() == active_) return index;
        return -1;
    }

    void refreshSidebar() {
        QStringList names;
        names.reserve(static_cast<int>(documents_.size()));
        for (const auto& session : documents_)
            names.push_back(QFileInfo(session->canonical_path).fileName());
        sidebar_->setDocuments(names, activeDocumentIndex());
        sidebar_->setOutline(active_ ? active_->outline : QJsonArray{});
    }

    const MoonmarkApiTable* api_ = nullptr;
    void* backend_ = nullptr;
    void* window_state_ = nullptr;
    moonmark::qt::DocumentSidebar* sidebar_ = nullptr;
    bool sidebar_requested_ = true;
    bool sidebar_explicit_ = false;
    moonmark::qt::MoonTitleBar* title_bar_ = nullptr;
    QWidget* document_actions_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    DocumentView* document_ = nullptr;
    MoonButton* open_ = nullptr;
    QAction* reload_action_ = nullptr;
    MoonButton* zoom_out_ = nullptr;
    MoonButton* zoom_in_ = nullptr;
    moonmark::qt::CaptionButton* minimize_ = nullptr;
    moonmark::qt::CaptionButton* maximize_ = nullptr;
    moonmark::qt::CaptionButton* close_ = nullptr;
    MoonButton* zoom_label_ = nullptr;
    QLabel* title_symbol_ = nullptr;
    ElidingLabel* title_label_ = nullptr;
    QLabel* diagnostics_bar_ = nullptr;
    std::vector<std::unique_ptr<OpenDocumentSession>> documents_;
    OpenDocumentSession* active_ = nullptr;
    QString current_path_;
    bool fullscreen_ = false;
    bool pre_fullscreen_maximized_ = false;
    bool diagnostics_ = false;
    QVariantAnimation sidebar_animation_;
    QVector<int> sidebar_width_samples_;
    bool sidebar_target_visible_ = true;
};

void applyMoonmarkStyle(QApplication& application) {
    moonmark::style::apply(application);
}

} // namespace

extern "C" int moonmark_qt_run(int argc, const char* const* argv, const MoonmarkApiTable* api) {
    if (api == nullptr || api->version != 2) {
        return 2;
    }
    QCoreApplication::setOrganizationName(QStringLiteral("Moonmark"));
    QCoreApplication::setApplicationName(QStringLiteral("Moonmark"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MOONMARK_PRODUCT_VERSION));

    std::vector<QByteArray> argument_storage;
    std::vector<char*> qt_arguments;
    argument_storage.reserve(static_cast<std::size_t>(argc));
    qt_arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        argument_storage.emplace_back(argv[index]);
    }
    for (auto& argument : argument_storage) {
        qt_arguments.push_back(argument.data());
    }
    int qt_argc = argc;
    QApplication application(qt_argc, qt_arguments.data());
    application.setWindowIcon(applicationIcon());
    applyMoonmarkStyle(application);
    // Smoke tests must not update the user's application settings.
    const bool motion_smoke = std::find(argument_storage.cbegin(), argument_storage.cend(),
                                        QByteArray("--smoke-motion")) != argument_storage.cend();
    const bool scroll_profile_smoke =
        std::find(argument_storage.cbegin(), argument_storage.cend(),
                  QByteArray("--smoke-scroll-profile")) != argument_storage.cend();
    if (scroll_profile_smoke) qputenv("MOONMARK_SCROLL_TRACE", "1");
    for (const auto& argument : argument_storage) {
        if (argument.startsWith("--smoke-")) {
            if (!motion_smoke && !scroll_profile_smoke)
                qputenv("MOONMARK_REDUCED_MOTION", "1");
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                              QDir::current().absoluteFilePath("out/tests/native-settings"));
            break;
        }
    }
    MoonmarkWindow window(api);
    window.show();
    moonmark::qt::windows::installNativeFrame(&window);
    QString smoke_mode;
    QStringList document_paths;
    QStringList startup_errors;
    for (int index = 1; index < qt_argc; ++index) {
        const auto argument =
            QString::fromUtf8(qt_arguments[static_cast<std::size_t>(index)]);
        if (argument == QStringLiteral("--smoke-render")) {
            smoke_mode = QStringLiteral("render");
        } else if (argument == QStringLiteral("--smoke-style")) {
            smoke_mode = QStringLiteral("style");
        } else if (argument == QStringLiteral("--smoke-zoom")) {
            smoke_mode = QStringLiteral("zoom");
        } else if (argument == QStringLiteral("--smoke-navigation")) {
            smoke_mode = QStringLiteral("navigation");
        } else if (argument == QStringLiteral("--smoke-icon")) {
            smoke_mode = QStringLiteral("icon");
        } else if (argument == QStringLiteral("--smoke-snapshot")) {
            smoke_mode = QStringLiteral("snapshot");
        } else if (argument == QStringLiteral("--smoke-layout")) {
            smoke_mode = QStringLiteral("layout");
        } else if (argument == QStringLiteral("--smoke-maximize")) {
            smoke_mode = QStringLiteral("maximize");
        } else if (argument == QStringLiteral("--smoke-native-window")) {
            smoke_mode = QStringLiteral("native-window");
        } else if (argument == QStringLiteral("--smoke-autoscroll-anchor")) {
            smoke_mode = QStringLiteral("autoscroll-anchor");
        } else if (argument == QStringLiteral("--smoke-layout-normal")) {
            smoke_mode = QStringLiteral("layout-normal");
        } else if (argument == QStringLiteral("--smoke-watcher")) {
            smoke_mode = QStringLiteral("watcher");
        } else if (argument == QStringLiteral("--smoke-image-geometry")) {
            smoke_mode = QStringLiteral("image-geometry");
        } else if (argument == QStringLiteral("--smoke-images")) {
            smoke_mode = QStringLiteral("images");
        } else if (argument == QStringLiteral("--smoke-multidoc")) {
            smoke_mode = QStringLiteral("multidoc");
        } else if (argument == QStringLiteral("--smoke-plaintext")) {
            smoke_mode = QStringLiteral("plaintext");
        } else if (argument == QStringLiteral("--smoke-motion")) {
            smoke_mode = QStringLiteral("motion-v2");
        } else if (argument == QStringLiteral("--smoke-outline-reflow")) {
            smoke_mode = QStringLiteral("outline-reflow");
        } else if (argument == QStringLiteral("--smoke-scroll-profile")) {
            smoke_mode = QStringLiteral("scroll-profile");
        } else if (argument == QStringLiteral("--smoke-multidoc-watcher")) {
            smoke_mode = QStringLiteral("multidoc-watcher");
        } else if (argument == QStringLiteral("--smoke-startup-arguments")) {
            smoke_mode = QStringLiteral("startup-arguments");
        } else if (!argument.startsWith(QLatin1Char('-'))) {
            const QFileInfo file(QDir::current().absoluteFilePath(argument));
            if (isSupportedDocumentFile(file)) {
                document_paths.push_back(file.canonicalFilePath());
            } else if (!file.exists()) {
                startup_errors.push_back(
                    QStringLiteral("%1 — file not found").arg(QDir::toNativeSeparators(argument)));
            } else {
                startup_errors.push_back(
                    QStringLiteral("%1 — unsupported document type")
                        .arg(QDir::toNativeSeparators(argument)));
            }
        }
    }
    for (const auto& document_path : document_paths) window.openDocument(document_path);
    if (smoke_mode.isEmpty() && !startup_errors.isEmpty()) {
        QTimer::singleShot(0, &window, [&window, startup_errors] {
            QMessageBox::warning(
                &window, QStringLiteral("Moonmark could not open some files"),
                QStringLiteral("Moonmark opens .md, .markdown, and .txt documents.\n\n%1")
                    .arg(startup_errors.join(QLatin1Char('\n'))));
        });
    }
    if (!smoke_mode.isEmpty()) {
        if (document_paths.isEmpty() && smoke_mode != QStringLiteral("snapshot") &&
            smoke_mode != QStringLiteral("icon") &&
            smoke_mode != QStringLiteral("startup-arguments")) {
            return 3;
        }
        window.runSmoke(smoke_mode);
    }
    return application.exec();
}
