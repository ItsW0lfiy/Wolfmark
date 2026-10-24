#include "wolfmark_qt.h"
#include "wolf_style.h"
#include "wolfmark_settings.h"
#include "wolfmark_updates.h"
#include "wolf_title_bar.h"
#include "document_zoom.h"
#include "document_editor.h"
#include "document_sidebar.h"
#include "smooth_scroll_controller.h"
#include "windows_window_frame.h"

#include <QAbstractTextDocumentLayout>
#include <QActionGroup>
#include <QApplication>
#include <QAccessible>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QClipboard>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleOptionButton>
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
#include <QTabBar>
#include <QToolButton>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantAnimation>
#include <QVector>
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

namespace colour = wolfmark::style::colour;
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
    int source_line = 0;
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

QString fromBuffer(const WolfmarkBuffer& buffer) {
    if (buffer.data == nullptr || buffer.len == 0) {
        return {};
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(buffer.data),
                             static_cast<qsizetype>(buffer.len));
}

QJsonObject jsonFromBuffer(const WolfmarkBuffer& buffer) {
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
    QIcon fallbackIcon(applicationAssetPath(QStringLiteral("assets/icons/wolfmark.ico")));
    // Qt on Linux does not necessarily ship the ICO decoder. The same approved
    // paw artwork is also bundled as PNG for native icon resolution.
    if (fallbackIcon.isNull() || fallbackIcon.pixmap(32, 32).isNull())
        fallbackIcon = QIcon(applicationAssetPath(QStringLiteral("assets/icons/wolfmark-32x32.png")));
    return fallbackIcon;
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
                   object.value("spans").toArray(), object.value("sourceLine").toInt()};
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

class WolfButton final : public QPushButton {
public:
    explicit WolfButton(const QString& text, QWidget* parent = nullptr) : QPushButton(text, parent) {
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(wolfmark::style::metric::control_height);
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

// A bounded native tab strip: long Markdown filenames should never consume the toolbar.
class CompactDocumentTabs final : public QTabBar {
protected:
    QSize tabSizeHint(int index) const override {
        QSize result = QTabBar::tabSizeHint(index);
        result.setWidth(std::min(result.width(), 230));
        return result;
    }
};

class ScrollFrameTrace final {
public:
    ScrollFrameTrace() : enabled_(qEnvironmentVariableIsSet("WOLFMARK_SCROLL_TRACE")) {
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

    explicit DocumentView(const WolfmarkApiTable* api, void* backend, QWidget* parent = nullptr)
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
        // Reader supports keyboard selection and navigation, but must never
        // paint an insertion caret: text is read-only, not an editor surface.
        setCursorWidth(0);
        setAccessibleName(QStringLiteral("Wolfmark Markdown document"));
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
        quote_frames_.clear();
        quote_frame_bounds_.clear();
        table_frames_.clear();
        table_frame_bounds_.clear();
        decorative_frame_bounds_dirty_ = true;
        anchor_positions_.clear();
        source_positions_.clear();
        navigation_anchor_.clear();
        completed_navigation_anchor_.clear();
        navigation_target_clamped_ = false;
        completed_navigation_clamped_ = false;
        navigation_settle_.stop();
        ++navigation_epoch_;
        title_ = root.value("title").toString(QStringLiteral("Wolfmark"));
        settings_ = root.value("settings").toObject();
        metrics_ = root.value("metrics").toObject();
        plain_text_ = root.value("sourceType").toString() == QStringLiteral("plainText");
        setLineWrapMode(plain_text_ ? QTextEdit::NoWrap : QTextEdit::WidgetWidth);
        setAccessibleName(plain_text_ ? QStringLiteral("Wolfmark plain text document")
                                      : QStringLiteral("Wolfmark Markdown document"));

        auto* next = new QTextDocument(this);
        next->setDocumentMargin(0.0);
        next->setDefaultFont(baseCharacterFormat(settings_.value("bodyFontPoints").toDouble(12.75))
                                 .font());
        QPointer<QTextDocument> previous = document();
        setDocument(next);
        // QTextEdit::setDocument resets cursorWidth to the default (1px).
        // Reapply the Reader-only no-caret policy after every document load.
        setCursorWidth(0);
        if (previous && previous->parent() == this) delete previous;
        QObject::connect(next->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                         this, [this] {
            ++layout_generation_;
            scheduleNavigationRetarget();
        });
        applyDocumentWidth();

        next->setLayoutEnabled(false);
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
        alignCodeCopyHeaders();
        zoom_layout_.capture(next);
        if (zoom_percent_ != 100) zoom_layout_.apply(zoom_percent_);
        applyDocumentWidth();
        next->setLayoutEnabled(true);
        construction_us_ = static_cast<quint64>(timer.nsecsElapsed() / 1000);
        document_construction_count_++;
        QTextCursor start(next);
        start.movePosition(QTextCursor::Start);
        setTextCursor(start);
        verticalScrollBar()->setValue(0);
        QTimer::singleShot(0, this, [this] {
            queueVisibleImages();
            invalidateDecorativeFrameBounds();
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
                if (part.charFormat().anchorHref() != QStringLiteral("wolfmark-copy:0")) continue;
                QTextCursor cursor(document());
                cursor.setPosition(part.position() + 1);
                setTextCursor(cursor);
                ensureCursorVisible();
                const auto point = cursorRect(cursor).center();
                const auto frame_bounds = document()->documentLayout()->frameBoundingRect(
                    code_frames_.front());
                const bool right_aligned = point.x() + horizontalScrollBar()->value() >
                    frame_bounds.left() + frame_bounds.width() * 0.70;
                auto* previous = snapshotClipboard();
                QMouseEvent press(QEvent::MouseButtonPress, point, viewport()->mapToGlobal(point),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QMouseEvent release(QEvent::MouseButtonRelease, point, viewport()->mapToGlobal(point),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(viewport(), &press);
                QApplication::sendEvent(viewport(), &release);
                const bool copied = last_copy_text_ == code_sources_.front();
                if (!copied || !right_aligned) {
                    std::fprintf(stdout, "COPY_DIAGNOSTIC press=%d cursor=%d selected=%d anchor=%s clipboard_length=%lld expected_length=%lld\n",
                                 press_position_, textCursor().position(), textCursor().hasSelection(),
                                 cursorForPosition(point).charFormat().anchorHref().toUtf8().constData(),
                                 static_cast<long long>(QGuiApplication::clipboard()->text().size()),
                                 static_cast<long long>(code_sources_.front().size()));
                    std::fprintf(stdout, "COPY_DIAGNOSTIC right_aligned=%d frame_left=%.1f frame_width=%.1f click_x=%d\n",
                                 right_aligned, frame_bounds.left(), frame_bounds.width(),
                                 point.x() + horizontalScrollBar()->value());
                }
                QGuiApplication::clipboard()->setMimeData(previous);
                return copied && right_aligned;
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
        const auto snapshot_path = qEnvironmentVariable("WOLFMARK_AUTOSCROLL_SNAPSHOT");
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
        // Test every table built from Rust's presentation commands, including
        // those nested inside a quotation frame (not just root-frame children).
        for (const auto& tracked : table_frames_) {
            auto* table = tracked.frame.data();
            if (table == nullptr) return false;
            ++tables;
            if (table->format().border() > 1) {
                std::fprintf(stderr, "STYLE_DIAG table_border=%f\n", table->format().border());
                return false;
            }
            for (int row = 0; row < table->rows(); ++row) {
                for (int column = 0; column < table->columns(); ++column) {
                    const auto cell = table->cellAt(row, column);
                    const auto format = cell.format().toTableCellFormat();
                    if (format.leftBorder() != (column == 0 ? 0 : 0.75) || format.rightBorder() != 0 ||
                        format.topBorder() != 0) {
                        std::fprintf(stderr, "STYLE_DIAG cell_border row=%d col=%d left=%f right=%f top=%f\n",
                                     row, column, format.leftBorder(), format.rightBorder(), format.topBorder());
                        return false;
                    }
                    if (row == 0 && format.background().color() != QColor(colour::table_header)) {
                        std::fprintf(stderr, "STYLE_DIAG table_header mismatch\n");
                        return false;
                    }
                    right_aligned |= cell.firstCursorPosition().blockFormat().alignment() ==
                                     Qt::AlignRight;
                    const auto block = cell.firstCursorPosition().block();
                    for (auto it = block.begin(); !it.atEnd(); ++it) {
                        const auto background = it.fragment().charFormat().background();
                        if (background.style() != Qt::NoBrush && background.color() != QColor(colour::inline_code)) {
                            std::fprintf(stderr, "STYLE_DIAG inline background mismatch: %s\n",
                                         qUtf8Printable(background.color().name()));
                            return false;
                        }
                    }
                }
            }
        }
        if (!table_frames_.empty()) {
            if (decorative_frame_bounds_dirty_ ||
                table_frame_bounds_.size() != table_frames_.size()) {
                std::fprintf(stderr, "STYLE_DIAG table corner geometry cache is not ready\n");
                return false;
            }
            for (const auto& table : table_frame_bounds_) {
                if (!table.frame || table.bounds.isEmpty()) {
                    std::fprintf(stderr, "STYLE_DIAG table corner geometry is invalid\n");
                    return false;
                }
            }
        }
        if (!quote_frames_.empty()) {
            if (decorative_frame_bounds_dirty_ ||
                quote_frame_bounds_.size() != quote_frames_.size()) {
                std::fprintf(stderr, "STYLE_DIAG quote geometry cache is not ready\n");
                return false;
            }
            for (const auto& quote : quote_frame_bounds_) {
                if (!quote.frame || quote.bounds.isEmpty() ||
                    quote.frame->frameFormat().background().color() !=
                        QColor(QStringLiteral("#231a20"))) {
                    std::fprintf(stderr, "STYLE_DIAG quote frame geometry/background mismatch\n");
                    return false;
                }
            }
        }
        const auto palette = QApplication::palette();
        for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            for (const auto role : {QPalette::Accent, QPalette::Highlight, QPalette::Link,
                                    QPalette::Text, QPalette::Button, QPalette::ButtonText}) {
                const auto color = palette.color(group, role);
                // Slightly cool graphite is neutral, not a bright-blue UI accent.
                constexpr int saturated_blue_bias = 12;
                if (color.blue() > color.red() + saturated_blue_bias &&
                    color.blue() > color.green() + saturated_blue_bias) {
                    std::fprintf(stderr, "STYLE_DIAG saturated_blue role=%d group=%d color=%s\n",
                                 static_cast<int>(role), static_cast<int>(group),
                                 qUtf8Printable(color.name()));
                    return false;
                }
            }
        }
        if (palette.color(QPalette::Active, QPalette::Accent) != QColor(colour::crimson) ||
            palette.color(QPalette::Active, QPalette::Link) != QColor(colour::crimson_hover)) {
            std::fprintf(stderr, "STYLE_DIAG crimson palette contract mismatch\n");
            return false;
        }
        auto* accessible = QAccessible::queryAccessibleInterface(
            const_cast<DocumentView*>(this));
        std::fprintf(stderr, "STYLE_DIAG tables=%d right_aligned=%d accessible=%d interface=%d readonly=%d\n",
                     tables, right_aligned, accessible != nullptr,
                     accessible && accessible->textInterface() != nullptr, isReadOnly());
        return tables > 0 && right_aligned && accessible != nullptr &&
               accessible->textInterface() != nullptr && isReadOnly() &&
               cursorWidth() == 0;
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
        alignCodeCopyHeaders();
        const auto formats_us = profile.nsecsElapsed() / 1000;
        applyDocumentWidth();
        resizeLoadedImages();
        const auto images_us = profile.nsecsElapsed() / 1000;
        document()->setLayoutEnabled(true);
        invalidateDecorativeFrameBounds();
        setTextCursor(selection);
        if (!explicit_navigation) {
            verticalScrollBar()->setValue(at_top ? 0 : verticalScrollBar()->value() +
                                            cursorRect(anchor).top() - anchor_y);
            scroll_controller_.rebaseAfterLayout();
        }
        setUpdatesEnabled(true);
        scheduleNavigationRetarget();
        if (zoomChanged) zoomChanged(zoom_percent_);
        if (qEnvironmentVariableIsSet("WOLFMARK_PROFILE"))
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
    [[nodiscard]] int sourceLineAtViewport() const {
        if (plain_text_) return cursorForPosition(QPoint(0, 0)).blockNumber() + 1;
        const int position = viewportAnchorPosition();
        int line = 1;
        for (const auto& item : source_positions_) {
            if (item.first > position) break;
            line = item.second;
        }
        return line;
    }

    void navigateToSourceLine(int line) {
        cancelScrollMotion();
        int position = 0;
        if (plain_text_) {
            position = document()->findBlockByNumber(std::clamp(line - 1, 0, document()->blockCount() - 1)).position();
        } else {
            for (const auto& item : source_positions_) {
                if (item.second > line) break;
                position = item.first;
            }
        }
        // Reuse semantic navigation's layout-generation tracking. A freshly
        // built large document can still have a provisional scrollbar range.
        const auto target = QStringLiteral("\x1fsource-location");
        anchor_positions_.insert(target, std::clamp(position, 0, document()->characterCount() - 1));
        navigateToAnchor(target, false);
    }
    std::function<void(int)> zoomChanged;
    std::function<void()> sourceLocationSettled;
    [[nodiscard]] bool sourceNavigationActive() const {
        return navigation_anchor_ == QStringLiteral("\x1fsource-location");
    }

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
        std::fprintf(stdout, "WOLFMARK_SMOKE zoom=%s parse_delta=%llu load_delta=%llu construction_delta=%llu image_request_delta=%llu\n",
                     ok ? "ok" : "failed",
                     static_cast<unsigned long long>(after.parse_count - before.parse_count),
                     static_cast<unsigned long long>(after.load_count - before.load_count),
                     static_cast<unsigned long long>(constructionCount() - constructions),
                     static_cast<unsigned long long>(after.image_request_count - before.image_request_count));
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
        paintTableCorners(painter);
        paintCodeFrameCorners(painter);
        paintQuoteFrameCorners(painter);
        // Inline-code edges are native viewport decoration. Quote accents are
        // painted once per cached frame, not once per paragraph: this avoids
        // broken stripes and stray nested markers outside the quote surface.
        auto block = cursorForPosition(QPoint(0, 0)).block();
        for (; block.isValid(); block = block.next()) {
            QTextCursor cursor(block);
            if (cursorRect(cursor).top() > viewport()->height()) break;
            paintInlineCodeEdges(painter, block);
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
        const double radius = 4.5 * zoom_percent_ / 100.0;
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
        if (decorative_frame_bounds_dirty_) return;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(colour::document));
        painter.setClipRect(viewport()->rect());
        const QPointF scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
        const qreal radius = 10.0 * zoom_percent_ / 100.0;
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
            // The text and selection are still painted by QTextEdit. A subdued
            // native overlay supplies the missing rounded border without changing
            // QTextFrame geometry, selection offsets, or editable Markdown source.
            painter.save();
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(colour::code_separator), 1.0));
            painter.drawRoundedRect(bounds.adjusted(.5, .5, -.5, -.5), radius, radius);
            painter.restore();
        }
        painter.restore();
    }

    void paintTableCorners(QPainter& painter) const {
        if (decorative_frame_bounds_dirty_) return;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipRect(viewport()->rect());
        const QPointF scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
        const qreal radius = 9.0 * zoom_percent_ / 100.0;
        const qreal visible_top = verticalScrollBar()->value();
        const qreal visible_bottom = visible_top + viewport()->height();
        auto frame = std::lower_bound(table_frame_bounds_.cbegin(), table_frame_bounds_.cend(),
                                      visible_top,
                                      [](const TableFrameBounds& item, qreal top) {
                                          return item.bounds.bottom() < top;
                                      });
        for (; frame != table_frame_bounds_.cend() && frame->bounds.top() <= visible_bottom;
             ++frame) {
            const QRectF bounds = frame->bounds.translated(-scroll);
            if (bounds.isEmpty()) continue;
            QPainterPath square;
            square.addRect(bounds);
            QPainterPath rounded;
            rounded.addRoundedRect(bounds, radius, radius);
            // Qt's QTextTable draws square header cells. Mask only the four
            // outer corners, preserving native cell borders, selectable text,
            // keyboard focus, and the table's actual layout geometry.
            painter.setPen(Qt::NoPen);
            painter.setBrush(frame->in_quote ? QColor(QStringLiteral("#231a20"))
                                             : QColor(colour::document));
            painter.drawPath(square.subtracted(rounded));
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(colour::table_outer), 1.0));
            painter.drawRoundedRect(bounds.adjusted(.5, .5, -.5, -.5), radius, radius);
        }
        painter.restore();
    }

    void paintQuoteFrameCorners(QPainter& painter) const {
        if (decorative_frame_bounds_dirty_) return;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setClipRect(viewport()->rect());
        const QPointF scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
        const qreal radius = 9.0 * zoom_percent_ / 100.0;
        const qreal visible_top = verticalScrollBar()->value();
        const qreal visible_bottom = visible_top + viewport()->height();
        auto frame = std::lower_bound(quote_frame_bounds_.cbegin(), quote_frame_bounds_.cend(),
                                      visible_top,
                                      [](const QuoteFrameBounds& item, qreal top) {
                                          return item.bounds.bottom() < top;
                                      });
        for (; frame != quote_frame_bounds_.cend() && frame->bounds.top() <= visible_bottom;
             ++frame) {
            const QRectF bounds = frame->bounds.translated(-scroll);
            QPainterPath square;
            square.addRect(bounds);
            QPainterPath rounded;
            rounded.addRoundedRect(bounds, radius, radius);
            // Only erase the square corner pixels. Native QTextFrame text,
            // selection, layout and keyboard accessibility remain untouched.
            // Nested quotes reveal their parent panel, not the document canvas.
            painter.setBrush(frame->nested ? QColor(QStringLiteral("#231a20"))
                                           : QColor(colour::document));
            painter.drawPath(square.subtracted(rounded));
            // One continuous accent follows each native QTextFrame, including
            // nested quotes. Keep it inside the rounded panel rather than
            // calculating offsets from the text cursor for every paragraph.
            const qreal stripe = std::max<qreal>(1.5, 2.0 * zoom_percent_ / 100.0);
            painter.fillRect(QRectF(bounds.left() + 1.0, bounds.top() + radius / 2.0,
                                    stripe, std::max<qreal>(0.0, bounds.height() - radius)),
                             QColor(colour::crimson));
        }
        painter.restore();
    }

    void resizeEvent(QResizeEvent* event) override {
        const bool explicit_navigation = navigationActive();
        const auto anchor = explicit_navigation ? ViewportAnchor{} : captureViewportAnchor();
        QTextEdit::resizeEvent(event);
        applyDocumentWidth();
        alignCodeCopyHeaders();
        resizeLoadedImages();
        if (!explicit_navigation) restoreViewportAnchor(anchor);
        if (!explicit_navigation) scroll_controller_.rebaseAfterLayout();
        invalidateDecorativeFrameBounds();
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
        if (anchor.startsWith(QStringLiteral("wolfmark-copy:"))) {
            const auto index = anchor.sliced(QStringLiteral("wolfmark-copy:").size()).toInt();
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
                const double destination = std::clamp(
                    static_cast<double>(verticalScrollBar()->value()) + distance,
                    static_cast<double>(verticalScrollBar()->minimum()),
                    static_cast<double>(verticalScrollBar()->maximum()));
                scroll_controller_.moveDirectlyTo(static_cast<int>(std::lround(destination)));
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
        return qEnvironmentVariable("WOLFMARK_REDUCED_MOTION") == QStringLiteral("1");
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
        if (completed_navigation_anchor_ == QStringLiteral("\x1fsource-location") && sourceLocationSettled)
            sourceLocationSettled();
        navigation_target_clamped_ = false;
    }

    void buildBlocks(QTextCursor& cursor, std::size_t& index, int stop_kind, int depth) {
        while (index < commands_.size()) {
            const auto& command = commands_[index];
            if (command.source_line > 0)
                source_positions_.emplace_back(cursor.position(), command.source_line);
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
            case command_kind::begin_quote: {
                ++index;
                // A QTextFrame supplies a continuous quote surface across every
                // paragraph. Qt still owns selectable text and native layout.
                if (cursor.position() != 0 && !cursor.block().text().isEmpty())
                    cursor.insertBlock();
                auto spacer = bodyBlockFormat(100);
                spacer.setLineHeight(1, QTextBlockFormat::FixedHeight);
                spacer.setTopMargin(0);
                spacer.setBottomMargin(0);
                cursor.setBlockFormat(spacer);
                QTextFrameFormat quote_format;
                quote_format.setBackground(QColor(QStringLiteral("#231a20")));
                quote_format.setPadding(9.0);
                quote_format.setTopMargin(2.0);
                quote_format.setBottomMargin(6.0);
                quote_format.setLeftMargin(depth * 22.0);
                auto* frame = cursor.insertFrame(quote_format);
                quote_frames_.push_back({frame, quote_depth_ > 0});
                QTextCursor inside(frame);
                ++quote_depth_;
                buildBlocks(inside, index, command_kind::end_quote, depth);
                --quote_depth_;
                // The trailing structural paragraph created by QTextCursor is
                // not visible content and must not add a body-height gap.
                auto trailing = inside.blockFormat();
                trailing.setLineHeight(1, QTextBlockFormat::FixedHeight);
                trailing.setTopMargin(0);
                trailing.setBottomMargin(0);
                inside.setBlockFormat(trailing);
                cursor = QTextCursor(frame->lastCursorPosition());
                cursor.movePosition(QTextCursor::End);
                cursor.setBlockFormat(bodyBlockFormat());
                break;
            }
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
        if (quote_depth_ > 0 && !heading) {
            block.setLineHeight(128, QTextBlockFormat::ProportionalHeight);
            block.setTopMargin(0);
            block.setBottomMargin(4);
        }
        if (heading) {
            static constexpr double scales[] = {2.65, 1.78, 1.36, 1.18, 1.08, 1.0};
            block.setHeadingLevel(std::clamp(level, 1, 6));
            points *= scales[std::clamp(level, 1, 6) - 1];
            // Large editorial headings should not create a massive blank gap
            // before the document lead on dense desktop layouts.
            block.setTopMargin(level == 1 ? 8.0 : (level == 2 ? 13.0 : 11.0));
            block.setBottomMargin(level == 1 ? 3.0 : (level == 2 ? 8.0 : 6.0));
            block.setLineHeight(level == 1 ? 108 : 126, QTextBlockFormat::ProportionalHeight);
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
        if (heading) {
            format.setFontFamilies({QStringLiteral("Georgia"), QStringLiteral("Cambria"),
                                    QStringLiteral("Segoe UI")});
        }
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
            format.setForeground(QColor(colour::crimson_hover));
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
        // A right-aligned native tab keeps the functional Copy link at the
        // trailing edge, without an overlay that could break selection.
        inside.insertText(QStringLiteral("\t"), language);
        auto copy = language;
        copy.setAnchor(true);
        copy.setAnchorHref(QStringLiteral("wolfmark-copy:%1").arg(code_index));
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
        table_frames_.push_back({table, quote_depth_ > 0});
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
                // Right-aligned numeric headers use a wider serif face than
                // the body cells. Release some *left* padding without moving
                // the right edge, so short labels do not wrap on Linux fonts.
                cell_format.setLeftPadding(header && cell_command.number == 2 ? 6 : 14);
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
            message = QStringLiteral("Remote image blocked by Wolfmark's privacy policy");
        } else if (command.flags == 4) {
            message = QStringLiteral("Unsupported image format · %1").arg(command.target);
        } else {
            message = QStringLiteral("Image could not be displayed");
        }

        const auto resource = QUrl(QStringLiteral("wolfmark-image://%1").arg(id));
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

    void alignCodeCopyHeaders() {
        if (!document() || code_frames_.empty()) return;
        // QTextBlockFormat tab stops are fixed layout coordinates, so refresh
        // them when the viewport or zoom changes. The link stays a selectable
        // native QTextDocument anchor with the existing Copy action.
        const qreal usable_width = viewport()->width() - 2.0 * effectiveSideMargin();
        for (const auto& frame : code_frames_) {
            if (!frame) continue;
            QTextCursor header(frame->firstCursorPosition());
            const auto format = frame->frameFormat();
            const qreal content_width = usable_width - format.leftMargin() -
                format.rightMargin() - 2.0 * format.padding();
            const qreal right_edge = std::max<qreal>(90.0, content_width - 6.0);
            auto block = header.blockFormat();
            const auto tabs = block.tabPositions();
            if (tabs.size() == 1 && tabs.front().type == QTextOption::RightTab &&
                std::abs(tabs.front().position - right_edge) < 0.5) continue;
            block.setTabPositions({QTextOption::Tab(right_edge, QTextOption::RightTab)});
            header.setBlockFormat(block);
        }
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
            const auto resource = QUrl(QStringLiteral("wolfmark-image://%1").arg(result.id));
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
            invalidateDecorativeFrameBounds();
            if (!navigation_anchor_.isEmpty()) ++navigation_image_retarget_count_;
            scheduleNavigationRetarget();
            viewport()->update();
            scroll_trace_.recordImageDelivery(profile.nsecsElapsed() / 1000);
            if (qEnvironmentVariableIsSet("WOLFMARK_PROFILE"))
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

    struct QuoteFrame {
        QPointer<QTextFrame> frame;
        bool nested = false;
    };

    struct QuoteFrameBounds {
        QPointer<QTextFrame> frame;
        QRectF bounds;
        bool nested = false;
    };

    struct TableFrame {
        QPointer<QTextTable> frame;
        bool in_quote = false;
    };

    struct TableFrameBounds {
        QPointer<QTextTable> frame;
        QRectF bounds;
        bool in_quote = false;
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

    void invalidateDecorativeFrameBounds() {
        decorative_frame_bounds_dirty_ = true;
        if (decorative_frame_bounds_rebuild_pending_) return;
        decorative_frame_bounds_rebuild_pending_ = true;
        QTimer::singleShot(0, this, [this] {
            decorative_frame_bounds_rebuild_pending_ = false;
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
            quote_frame_bounds_.clear();
            quote_frame_bounds_.reserve(quote_frames_.size());
            for (const auto& quote : quote_frames_) {
                if (!quote.frame) continue;
                quote_frame_bounds_.push_back(
                    {quote.frame, document()->documentLayout()->frameBoundingRect(quote.frame),
                     quote.nested});
            }
            std::sort(quote_frame_bounds_.begin(), quote_frame_bounds_.end(),
                      [](const QuoteFrameBounds& left, const QuoteFrameBounds& right) {
                          return left.bounds.bottom() < right.bounds.bottom();
                      });
            table_frame_bounds_.clear();
            table_frame_bounds_.reserve(table_frames_.size());
            for (const auto& table : table_frames_) {
                if (!table.frame) continue;
                // QTextDocumentLayout reports a table's text-frame envelope,
                // which is offset from the painted cell border by padding and
                // trailing table margin. Decorate the visible cell perimeter,
                // not that oversized envelope (otherwise the rounded stroke
                // floats outside the table and crosses the next paragraph).
                const auto first = table.frame->cellAt(0, 0).format().toTableCellFormat();
                const auto last = table.frame->cellAt(table.frame->rows() - 1,
                                                       table.frame->columns() - 1)
                                      .format().toTableCellFormat();
                const qreal edge = 2.0 * zoom_percent_ / 100.0;
                const QRectF envelope = document()->documentLayout()->frameBoundingRect(table.frame);
                const QRectF visible = envelope.adjusted(
                    -first.leftPadding(), -first.topPadding() - edge,
                    -last.rightPadding() - edge,
                    -table.frame->format().bottomMargin() - last.bottomPadding() -
                        first.topPadding());
                table_frame_bounds_.push_back({table.frame, visible, table.in_quote});
            }
            std::sort(table_frame_bounds_.begin(), table_frame_bounds_.end(),
                      [](const TableFrameBounds& left, const TableFrameBounds& right) {
                          return left.bounds.bottom() < right.bounds.bottom();
                      });
            decorative_frame_bounds_dirty_ = false;
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

    const WolfmarkApiTable* api_ = nullptr;
    void* backend_ = nullptr;
    wolfmark::qt::SmoothScrollController scroll_controller_;
    ScrollFrameTrace scroll_trace_;
    std::vector<Command> commands_;
    std::vector<ImageOccurrence> image_occurrences_;
    std::unordered_map<std::uint32_t, bool> image_requested_;
    std::vector<QString> code_sources_;
    QString last_copy_text_;
    std::vector<QPointer<QTextFrame>> code_frames_;
    std::vector<CodeFrameBounds> code_frame_bounds_;
    std::vector<QuoteFrame> quote_frames_;
    std::vector<QuoteFrameBounds> quote_frame_bounds_;
    std::vector<TableFrame> table_frames_;
    std::vector<TableFrameBounds> table_frame_bounds_;
    bool decorative_frame_bounds_dirty_ = true;
    bool decorative_frame_bounds_rebuild_pending_ = false;
    QJsonObject settings_;
    QJsonObject metrics_;
    QString title_ = QStringLiteral("Wolfmark");
    QHash<QString, int> anchor_positions_;
    std::vector<std::pair<int, int>> source_positions_;
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
    wolfmark::qt::DocumentZoom zoom_layout_;
    quint64 construction_us_ = 0;
    quint64 document_construction_count_ = 0;
};

struct OpenDocumentSession {
    enum class Mode { Reader, LivePreview, Source };
    QString canonical_path;
    void* backend = nullptr;
    DocumentView* view = nullptr;
    wolfmark::qt::DocumentEditor* editor = nullptr;
    QStackedWidget* page = nullptr;
    Mode mode = Mode::Reader;
    quint64 rendered_revision = 0;
    bool external_changed = false;
    bool dirty_shown = false;
    QString file_error;
    int reader_entry_line = 1;
    bool has_edit_location = false;
    int zoom_percent = 100;
    QJsonArray outline;
    QJsonArray editing_outline;
    bool editing_outline_ready = false;
    QFileSystemWatcher* watcher = nullptr;
    QTimer* reload_delay = nullptr;
};

// Preserve Qt checkbox semantics while drawing a visible tick over the crimson indicator.
class WolfSettingsCheckBox final : public QCheckBox {
public:
    explicit WolfSettingsCheckBox(const QString& label, QWidget* parent = nullptr)
        : QCheckBox(label, parent) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        QCheckBox::paintEvent(event);
        if (checkState() != Qt::Checked) return;
        QStyleOptionButton option;
        initStyleOption(&option);
        const QRect indicator = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
        if (indicator.isEmpty()) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 2.0,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF a(indicator.left() + indicator.width() * 0.23,
                        indicator.top() + indicator.height() * 0.52);
        const QPointF b(indicator.left() + indicator.width() * 0.44,
                        indicator.top() + indicator.height() * 0.73);
        const QPointF c(indicator.left() + indicator.width() * 0.80,
                        indicator.top() + indicator.height() * 0.32);
        painter.drawLine(a, b);
        painter.drawLine(b, c);
    }
};

// A native Qt settings dialog with the same Windows-aware frame as the reader.
// Keeps the existing modal QDialog button contract and settings persistence.
class WolfSettingsDialog final : public QDialog {
public:
    explicit WolfSettingsDialog(QWidget* parent) : QDialog(parent) {
        setObjectName(QStringLiteral("settingsDialog"));
        setWindowTitle(QStringLiteral("Wolfmark Settings"));
        setWindowIcon(QApplication::windowIcon());
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint |
                       Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_NativeWindow);
        setModal(true);
        setMinimumSize(780, 520);
        resize(920, 620);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        chrome_ = new wolfmark::qt::WolfTitleBar(this);
        chrome_->setObjectName(QStringLiteral("settingsChrome"));
        auto* header = new QHBoxLayout(chrome_);
        header->setContentsMargins(16, 0, 0, 0);
        header->setSpacing(10);
        auto* symbol = new QLabel(chrome_);
        symbol->setPixmap(applicationIcon().pixmap(23, 23));
        symbol->setFixedSize(26, 26);
        symbol->setAlignment(Qt::AlignCenter);
        header->addWidget(symbol);

        auto* title = new QLabel(QStringLiteral("Wolfmark  /  Settings"), chrome_);
        title->setObjectName(QStringLiteral("settingsChromeTitle"));
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        header->addWidget(title);
        header->addStretch();
        using Caption = wolfmark::qt::CaptionButton;
        minimize_ = new Caption(Caption::Action::Minimize, chrome_);
        maximize_ = new Caption(Caption::Action::Maximize, chrome_);
        close_ = new Caption(Caption::Action::Close, chrome_);
        QObject::connect(minimize_, &QAbstractButton::clicked, this, &QWidget::showMinimized);
        QObject::connect(maximize_, &QAbstractButton::clicked, this, [this] {
            isMaximized() ? showNormal() : showMaximized();
        });
        QObject::connect(close_, &QAbstractButton::clicked, this, &QDialog::reject);
        header->addWidget(minimize_);
        header->addWidget(maximize_);
        header->addWidget(close_);
        outer->addWidget(chrome_);

        auto* body = new QWidget(this);
        body->setObjectName(QStringLiteral("settingsBody"));
        body->setAttribute(Qt::WA_StyledBackground);
        contents_ = new QHBoxLayout(body);
        contents_->setContentsMargins(0, 0, 0, 0);
        contents_->setSpacing(0);
        outer->addWidget(body, 1);
    }

    [[nodiscard]] QHBoxLayout* contentsLayout() const { return contents_; }

protected:
    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        wolfmark::qt::windows::installNativeFrame(this);
    }

    void changeEvent(QEvent* event) override {
        QDialog::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange && maximize_)
            maximize_->setMaximized(isMaximized());
    }

#ifdef _WIN32
    bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result) override {
        if (wolfmark::qt::windows::handleNativeFrameEvent(
                this, chrome_, minimize_, maximize_, close_, false, message, result))
            return true;
        return QDialog::nativeEvent(event_type, message, result);
    }
#endif

private:
    wolfmark::qt::WolfTitleBar* chrome_ = nullptr;
    wolfmark::qt::CaptionButton* minimize_ = nullptr;
    wolfmark::qt::CaptionButton* maximize_ = nullptr;
    wolfmark::qt::CaptionButton* close_ = nullptr;
    QHBoxLayout* contents_ = nullptr;
};

class WolfmarkWindow final : public QWidget {
public:
    explicit WolfmarkWindow(const WolfmarkApiTable* api)
        : api_(api), user_settings_(wolfmark::qt::UserSettings::load(
              QStringLiteral(WOLFMARK_PRODUCT_VERSION).contains(QLatin1Char('-')))) {
        window_state_ = api_->window_state_new();
        setObjectName(QStringLiteral("appWindow"));
        setWindowTitle(QStringLiteral("Wolfmark"));
        setWindowIcon(applicationIcon());
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint |
                       Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        setAttribute(Qt::WA_NativeWindow);
        setMinimumSize(720, 480);
        resize(1280, 820);
        setAcceptDrops(true);
        update_manager_ = new wolfmark::qt::UpdateManager(api_, this);
        buildUi();
        updateStatus();
        if (user_settings_.updates().check_on_startup &&
            !qEnvironmentVariableIsSet("WOLFMARK_DISABLE_UPDATE_CHECKS")) {
            QTimer::singleShot(1200, this, [this] { checkForUpdates(false); });
        }
    }

    ~WolfmarkWindow() override {
        for (auto& session : documents_) {
            session->editor->stateChanged = {};
            delete session->page;
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
        session->editor = new wolfmark::qt::DocumentEditor(api_);
        session->editor->setMarkdown(file.suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) != 0);
        session->editor->setLineWrapMode(user_settings_.editorWordWrap() ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        QString open_error;
        if (!session->editor->loadFile(canonical_path, &open_error)) {
            delete session->editor;
            delete session->view;
            api_->backend_free(session->backend);
            QMessageBox::warning(this, QStringLiteral("Could not open document"), open_error);
            return false;
        }
        session->page = new QStackedWidget;
        session->page->addWidget(session->view);
        session->page->addWidget(session->editor);
        session->editor->stateChanged = [this, raw = session.get()] {
            const bool dirty = raw->editor->document()->isModified();
            if (dirty != raw->dirty_shown) {
                raw->dirty_shown = dirty;
                refreshSidebar();
            }
            if (active_ == raw) updateDocumentControls();
        };
        session->editor->zoomRequested = [this, raw = session.get()](int delta) {
            if (active_ == raw) changeZoom(delta);
        };
        session->editor->outlineChanged = [this, raw = session.get()](const QJsonArray& headings) {
            raw->editing_outline = headings;
            raw->editing_outline_ready = true;
            if (active_ == raw && raw->mode != OpenDocumentSession::Mode::Reader) sidebar_->setOutline(headings);
        };
        session->view->zoomChanged = [this, raw = session.get()](int percent) {
            raw->zoom_percent = percent;
            if (active_ == raw) zoom_label_->setText(QStringLiteral("%1%").arg(percent));
        };
        session->view->sourceLocationSettled = [raw = session.get()] {
            if (raw->mode == OpenDocumentSession::Mode::Reader)
                raw->reader_entry_line = raw->view->sourceLineAtViewport();
        };
        const bool rendered = renderSession(session.get(), true);
        session->watcher = new QFileSystemWatcher(this);
        session->watcher->addPath(canonical_path);
        session->watcher->addPath(file.absolutePath());
        session->reload_delay = new QTimer(this);
        session->reload_delay->setSingleShot(true);
        session->reload_delay->setInterval(180);
        auto* raw = session.get();
        QObject::connect(session->watcher, &QFileSystemWatcher::fileChanged, this,
                         [raw](const QString&) { raw->reload_delay->start(); });
        QObject::connect(session->watcher, &QFileSystemWatcher::directoryChanged, this,
                         [raw](const QString&) { raw->reload_delay->start(); });
        QObject::connect(session->reload_delay, &QTimer::timeout, this,
                         [this, raw] { reloadSession(raw); });
        stack_->addWidget(session->page);
        documents_.push_back(std::move(session));
        activateDocument(static_cast<int>(documents_.size()) - 1);
        user_settings_.setLastOpenDirectory(file.absolutePath());
        QString settings_error;
        if (!user_settings_.save(&settings_error)) qWarning("%s", qUtf8Printable(settings_error));
        return rendered;
    }

    void runSmoke(const QString& mode) {
        if (mode == QStringLiteral("shell-controls")) {
            QTimer::singleShot(300, this, [this] {
                bool ok = active_ != nullptr && document_actions_->isVisible() &&
                          title_search_->isEnabled() && mode_segments_.size() == 3 &&
                          mode_segments_.at(0)->isChecked();
                if (ok) {
                    // Verify split-button geometry, working menu state and breadcrumb.
                    auto* open_button = sidebar_->findChild<QPushButton*>(
                        QStringLiteral("sidebarOpenButton"));
                    auto* open_options = sidebar_->findChild<QToolButton*>(
                        QStringLiteral("sidebarOpenMenuButton"));
                    const bool menu_ok = open_options && open_options->menu() &&
                        open_options->menu()->actions().size() == 2 &&
                        open_options->menu()->actions().at(1)->isEnabled();
                    const bool geometry_ok = open_button && open_options &&
                        open_button->geometry().right() + 1 == open_options->geometry().left() &&
                        open_button->height() == open_options->height();
                    const bool breadcrumb_ok = title_label_->toolTip() == current_path_ &&
                        title_label_->text().startsWith(QStringLiteral("Documents"));
                    const bool icons_ok = sidebar_->testDocumentIconMapping();
                    const bool stable_rows = sidebar_->testStableDocumentRows();
                    ok &= menu_ok && geometry_ok && breadcrumb_ok && icons_ok && stable_rows;
                    std::fprintf(stdout, "SHELL_REFERENCE split=%s breadcrumb=%s dirty_file_icons=%s stable_rows=%s\n",
                                 menu_ok && geometry_ok ? "ok" : "failed",
                                 breadcrumb_ok ? "ok" : "failed",
                                 icons_ok ? "ok" : "failed",
                                 stable_rows ? "ok" : "failed");
                    for (const auto* button : mode_segments_) {
                        const int text_width = button->fontMetrics().horizontalAdvance(button->text());
                        const int available = button->width();
                        std::fprintf(stdout, "SHELL_BUTTON text=%s width=%d minimum=%d text_width=%d hint=%d\n",
                                     qUtf8Printable(button->text()), available,
                                     button->minimumWidth(), text_width, button->sizeHint().width());
                        ok &= available >= text_width + 36;
                    }
                }
                if (ok) {
                    mode_segments_.at(2)->click();
                    ok &= active_->mode == OpenDocumentSession::Mode::Source &&
                          mode_segments_.at(2)->isChecked() &&
                          mode_group_->checkedAction()->data().toInt() == 2;
                    mode_segments_.at(1)->click();
                    const auto* live = mode_segments_.at(1);
                    ok &= active_->mode == OpenDocumentSession::Mode::LivePreview &&
                          live->isChecked() &&
                          live->width() >= live->fontMetrics().horizontalAdvance(live->text()) + 36;
                    mode_segments_.at(0)->click();
                    ok &= active_->mode == OpenDocumentSession::Mode::Reader &&
                          mode_segments_.at(0)->isChecked();
                    if (documents_.size() > 1) {
                        auto* original = active_;
                        previous_tab_->click();
                        ok &= active_ != original && previous_tab_->isEnabled();
                        next_tab_->click();
                        ok &= active_ == original && next_tab_->isEnabled();
                        ok &= tab_row_->isVisible() && tabs_->count() == static_cast<int>(documents_.size());
                        auto* add = findChild<QPushButton*>(QStringLiteral("newDocumentTab"));
                        ok &= add != nullptr && add->isVisible() && add->isEnabled();
                    }
                    title_search_->click();
                    ok &= find_bar_->isVisible() && find_input_->hasFocus();
                }
                std::fprintf(stdout, "WOLFMARK_SMOKE shell_controls=%s modes=checked find=checked\n",
                             ok ? "ok" : "failed");
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 37);
            });
            return;
        }
        if (mode == QStringLiteral("editor-performance")) {
            QTimer::singleShot(250, this, [this] {
                const auto before = api_->backend_counters(backend_);
                const auto constructions = document_->constructionCount();
                auto* editor = active_->editor;
                document_->navigateToSourceLine(editor->document()->blockCount() * 3 / 4);
                const int reader_line = document_->sourceLineAtViewport();
                QElapsedTimer timer;
                timer.start();
                setDocumentMode(2);
                const bool mapped = std::abs(editor->textCursor().blockNumber() + 1 - reader_line) <= 2;
                const auto source_mode_us = timer.nsecsElapsed() / 1000;
                auto* ready = new QTimer(this);
                auto waiting = std::make_shared<QElapsedTimer>(); waiting->start();
                QObject::connect(ready, &QTimer::timeout, this, [this, ready, waiting, editor, before, constructions, source_mode_us, mapped] {
                    if (!editor->analysisReady() && waiting->elapsed() < 30000) return;
                    ready->stop();
                    const bool decorated = editor->analysisReady();
                    QElapsedTimer editing_timer;
                    editor->goToLine(editor->document()->blockCount() * 3 / 4);
                    const int line = editor->viewportLine();
                    const int characters = editor->document()->characterCount();
                    auto cursor = editor->textCursor();
                    editing_timer.restart();
                    for (int index = 0; index < 64; ++index) cursor.insertText(QStringLiteral("x"));
                    const auto typing_us = editing_timer.nsecsElapsed() / 1000;
                    editor->setTextCursor(cursor);
                    setDocumentMode(1);
                    cursor = editor->textCursor();
                    editing_timer.restart();
                    for (int index = 0; index < 64; ++index) cursor.insertText(QStringLiteral("y"));
                    const auto live_typing_us = editing_timer.nsecsElapsed() / 1000;
                    editor->setTextCursor(cursor);
                    editing_timer.restart();
                    for (int pass = 0; pass < 6; ++pass) setDocumentMode(pass % 2 == 0 ? 1 : 2);
                    const auto modes_us = editing_timer.nsecsElapsed() / 1000;
                    bool anchored = mapped && std::abs(editor->viewportLine() - line) <= 2;
                    for (const int zoom : {80, 100, 125, 150, 100}) {
                        const int start = editor->viewportLine();
                        changeZoom(zoom - active_->zoom_percent);
                        anchored &= std::abs(editor->viewportLine() - start) <= 2;
                        anchored &= std::abs(editor->document()->defaultFont().pointSizeF() - 12.0 * zoom / 100.0) < 0.01;
                        anchored &= zoom_label_->text() == QStringLiteral("%1%").arg(zoom);
                    }
                    auto* poll = new QTimer(this);
                    auto elapsed = std::make_shared<QElapsedTimer>(); elapsed->start();
                    QObject::connect(poll, &QTimer::timeout, this,
                        [this, editor, poll, elapsed, before, constructions, source_mode_us, typing_us, live_typing_us, modes_us, characters, anchored, decorated] {
                        if (!editor->analysisReady() && elapsed->elapsed() < 30000) return;
                        poll->stop();
                        const auto after = api_->backend_counters(backend_);
                        const bool stable = before.parse_count == after.parse_count && before.load_count == after.load_count &&
                                            constructions == document_->constructionCount();
                        const bool incremental = editor->document()->blockCount() < 100 ||
                                                 editor->decoratedBlocks() < editor->document()->blockCount() / 4;
                        const bool ok = stable && anchored && decorated && incremental && editor->analysisReady() &&
                                        editor->document()->characterCount() == characters + 128;
                        const int source_line = editor->textCursor().blockNumber() + 1;
                        QElapsedTimer refresh; refresh.start();
                        setDocumentMode(0);
                        const auto refresh_us = refresh.nsecsElapsed() / 1000;
                        std::fprintf(stdout, "WOLFMARK_SMOKE editor_performance=%s chars=%d blocks=%d source_mode_us=%lld typing_64_us=%lld live_typing_64_us=%lld modes_6_us=%lld analysis_us=%llu anchor=%s reader_counters=%s decorated_before_typing=%s decorated_blocks=%d\n",
                            ok ? "ok" : "failed", characters, editor->document()->blockCount(),
                            static_cast<long long>(source_mode_us), static_cast<long long>(typing_us), static_cast<long long>(live_typing_us), static_cast<long long>(modes_us),
                            static_cast<unsigned long long>(editor->analysisMicros()), anchored ? "stable" : "changed", stable ? "stable" : "changed", decorated ? "yes" : "no", editor->decoratedBlocks());
                        auto* landing = new QTimer(this);
                        auto settling = std::make_shared<QElapsedTimer>(); settling->start();
                        QObject::connect(landing, &QTimer::timeout, this, [this, landing, settling, source_line, refresh_us, ok] {
                            if (document_->navigationActive() && settling->elapsed() < 6000) return;
                            landing->stop();
                            const int actual_line = document_->sourceLineAtViewport();
                            const bool round_trip = std::abs(actual_line - source_line) <= 4;
                            setDocumentMode(2);
                            std::fprintf(stdout, "EDITOR_READER refresh_us=%lld location=%s source_line=%d reader_line=%d\n",
                                         static_cast<long long>(refresh_us), round_trip ? "stable" : "changed", source_line, actual_line);
                            std::fflush(stdout); qApp->exit(ok && round_trip &&
                                active_->editor->textCursor().blockNumber() + 1 == source_line ? 0 : 20);
                        });
                        landing->start(25);
                    });
                    poll->start(25);
                });
                ready->start(25);
            });
            return;
        }
        if (mode == QStringLiteral("editor-sessions")) {
            auto stage = std::make_shared<QString>(QStringLiteral("starting"));
            QTimer::singleShot(15000, this, [stage] {
                std::fprintf(stderr, "EDITOR_SESSION_TIMEOUT stage=%s\n", qUtf8Printable(*stage));
                std::fflush(stderr); qApp->exit(21);
            });
            QTimer::singleShot(250, this, [this, stage] {
                *stage = QStringLiteral("modes");
                bool ok = documents_.size() == 2 && active_->mode == OpenDocumentSession::Mode::Reader;
                // UI contract: multi-document tabs are available, but the new editor
                // features must not introduce a permanent full-width toolbar row.
                ok &= tabs_->count() == 2 && !tabs_->isHidden() &&
                      findChild<QWidget*>(QStringLiteral("documentToolbar")) == nullptr &&
                      findChild<QWidget*>(QStringLiteral("documentCommandBar")) != nullptr &&
                      document_actions_->isVisible() && title_search_->isEnabled() &&
                      mode_segments_.size() == 3 && mode_segments_.at(0)->isChecked() &&
                      mode_group_->actions().size() == 3 &&
                      mode_group_->checkedAction() != nullptr &&
                      mode_group_->checkedAction()->data().toInt() == 0;
                auto* session = active_;
                auto* editor = session->editor;
                const auto disk = editor->diskFingerprint();
                const auto original = editor->sourceText();
                const auto before = api_->backend_counters(backend_);
                setDocumentMode(2);
                ok &= editor->hasFocus() && !editor->isReadOnly();
                changeZoom(25);
                ok &= active_->zoom_percent == 125 && zoom_label_->text() == QStringLiteral("125%");
                changeZoom(-25);
                auto cursor = editor->textCursor();
                cursor.movePosition(QTextCursor::End);
                cursor.insertText(QStringLiteral("\nUnsaved 月🐺 **addition**"));
                const auto edited = editor->sourceText();
                const int position = cursor.position();
                editor->setTextCursor(cursor);
                setDocumentMode(1);
                ok &= editor->sourceText() == edited && editor->textCursor().position() == position;
                editor->undo();
                ok &= editor->sourceText() == original;
                editor->redo();
                ok &= editor->sourceText() == edited;
                setDocumentMode(0);
                ok &= document_->isReadOnly() && document_->plainText().contains(QStringLiteral("Unsaved 月🐺"));
                ok &= editor->document()->isModified() && editor->diskFingerprint() == disk;
                setDocumentMode(2);
                editor->undo(); editor->redo();
                ok &= editor->sourceText() == edited;
                find_input_->setText(QStringLiteral("Unsaved"));
                *stage = QStringLiteral("find-format");
                findNext(false);
                ok &= editor->textCursor().selectedText() == QStringLiteral("Unsaved");
                focusDocument();
                editor->formatSelection(QStringLiteral("**"));
                ok &= editor->sourceText().contains(QStringLiteral("**Unsaved**"));
                editor->undo();
                ok &= editor->sourceText() == edited;
                const auto count = documents_.size();
                openDocument(session->canonical_path);
                ok &= documents_.size() == count && session->mode == OpenDocumentSession::Mode::Source;
                activateDocument(0); activateDocument(1);
                ok &= active_ == session && session->mode == OpenDocumentSession::Mode::Source;
                *stage = QStringLiteral("save");
                ok &= saveSession(session, false) && !editor->document()->isModified();
                QFile saved(session->canonical_path);
                saved.open(QIODevice::ReadOnly);
                ok &= saved.readAll() == editor->encodedBytes(); saved.close();
                const auto current = editor->sourceText();
                *stage = QStringLiteral("external-conflict");
                editor->insertPlainText(QStringLiteral(" local"));
                QFile external(session->canonical_path);
                external.open(QIODevice::Append); external.write("\nexternal writer"); external.close();
                const auto dirty = editor->sourceText();
                reloadSession(session);
                ok &= session->external_changed && editor->sourceText() == dirty;
                QString error;
                ok &= editor->saveFile(session->canonical_path, editor->diskFingerprint(), &error) ==
                      wolfmark::qt::DocumentEditor::SaveResult::Conflict;
                QTimer::singleShot(0, this, [] {
                    if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) dialog->done(QMessageBox::Cancel);
                });
                *stage = QStringLiteral("close-cancel");
                closeDocument(1);
                ok &= documents_.size() == count;
                editor->undo();
                ok &= editor->sourceText() == current;
                auto* background = documents_.front().get();
                *stage = QStringLiteral("background-reload");
                background->view->navigateToSourceLine(3003);
                background->view->verticalScrollBar()->setValue(background->view->verticalScrollBar()->value() + 30);
                const int old_line = background->view->sourceLineAtViewport();
                ok &= old_line > 2000;
                const auto active_counters = api_->backend_counters(session->backend);
                QFile background_file(background->canonical_path);
                background_file.open(QIODevice::Append); background_file.write("\n# External heading\n"); background_file.close();
                reloadSession(background);
                ok &= active_ == session && background->view->sourceLineAtViewport() == old_line;
                const auto after = api_->backend_counters(session->backend);
                ok &= after.parse_count == active_counters.parse_count && after.load_count == before.load_count;
                ok &= tabs_->count() == 2 && mode_group_->checkedAction() != nullptr &&
                      mode_group_->checkedAction()->data().toInt() == 2;
                *stage = QStringLiteral("empty-outline");
                auto heading = editor->textCursor();
                heading.movePosition(QTextCursor::Start);
                heading.select(QTextCursor::BlockUnderCursor);
                heading.removeSelectedText();
                auto* outline_poll = new QTimer(this);
                QObject::connect(outline_poll, &QTimer::timeout, this, [this, outline_poll, editor, ok, old_line]() mutable {
                    if (!editor->analysisReady()) return;
                    outline_poll->stop();
                    ok &= active_->editing_outline_ready && active_->editing_outline.isEmpty();
                    activateDocument(0); activateDocument(1);
                    ok &= !sidebar_->activateOutlineAnchorForTest(QStringLiteral("other-document"));
                    std::fprintf(stdout, "WOLFMARK_SMOKE editor_sessions=%s shared_history=checked dirty=checked find=checked save=checked conflict=checked empty_outline=checked background_anchor=%d\n",
                                 ok ? "ok" : "failed", old_line);
                    std::fflush(stdout);
                    qApp->exit(ok ? 0 : 19);
                });
                outline_poll->start(25);
            });
            return;
        }
        if (mode == QStringLiteral("editor-core")) {
            auto* editor = new wolfmark::qt::DocumentEditor(api_, this);
            editor->resize(800, 600);
            editor->show();
            editor->setFocus();
            const auto bytes = QByteArray("\xef\xbb\xbf# Heading\r\n\r\n**bold** and _italic_\n") +
                               QStringLiteral("月🐺\u00a0text\rno final newline").toUtf8();
            QString error;
            bool ok = true;
            for (const auto& sample : {QByteArray(), QByteArray("\xef\xbb\xbf"), QByteArray("one\r\ntwo\nthree\rfour"),
                                      QByteArray("---\r\nx: 1\r\n---\r\n\r\n```md\n*literal*\n```"), bytes}) {
                ok &= editor->loadBytes(sample, &error) && editor->encodedBytes() == sample;
                auto all = editor->textCursor();
                all.select(QTextCursor::Document);
                all.removeSelectedText();
                editor->undo();
                ok &= editor->encodedBytes() == sample;
            }
            ok &= editor->loadBytes(bytes, &error) && editor->encodedBytes() == bytes;
            auto cursor = editor->textCursor();
            cursor.setPosition(3);
            cursor.insertText(QStringLiteral("Edited "));
            ok &= editor->document()->isModified();
            editor->undo();
            ok &= editor->encodedBytes() == bytes;
            cursor = editor->textCursor();
            cursor.setPosition(9);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            editor->undo();
            ok &= editor->encodedBytes() == bytes;
            editor->redo(); editor->undo();
            const auto original = editor->sourceText();
            editor->setLivePreview(true);
            editor->goToLine(5);
            auto* poll = new QTimer(this);
            auto elapsed = std::make_shared<QElapsedTimer>();
            elapsed->start();
            QObject::connect(poll, &QTimer::timeout, this, [this, editor, poll, elapsed, ok, bytes, original]() mutable {
                if (!editor->analysisReady() && elapsed->elapsed() < 10000) return;
                poll->stop();
                ok &= editor->analysisReady() && editor->sourceText() == original && editor->markerMinimizedAt(0);
                editor->goToLine(1);
                ok &= !editor->markerMinimizedAt(0);
                editor->setLivePreview(false);
                ok &= !editor->markerMinimizedAt(0) && editor->encodedBytes() == bytes;
                auto* accessible = QAccessible::queryAccessibleInterface(editor);
                ok &= accessible && accessible->textInterface() &&
                      accessible->textInterface()->text(0, accessible->textInterface()->characterCount()).contains(QStringLiteral("**bold**"));
                auto* clipboard = snapshotClipboard();
                editor->selectAll(); editor->copy();
                const bool copy_exact = QGuiApplication::clipboard()->text() == editor->sourceText();
                std::fprintf(stdout, "EDITOR_CLIPBOARD source_units=%lld copied_units=%lld source_nbsp=%lld copied_nbsp=%lld\n",
                    static_cast<long long>(editor->sourceText().size()), static_cast<long long>(QGuiApplication::clipboard()->text().size()),
                    static_cast<long long>(editor->sourceText().count(QChar(0x00a0))),
                    static_cast<long long>(QGuiApplication::clipboard()->text().count(QChar(0x00a0))));
                QGuiApplication::clipboard()->setMimeData(clipboard);
                std::fprintf(stdout, "EDITOR_COPY source_exact=%s\n", copy_exact ? "yes" : "no");
                ok &= copy_exact;
                editor->setTextCursor(editor->document()->find(QStringLiteral("bold")));
                QKeyEvent bold_key(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier);
                QApplication::sendEvent(editor, &bold_key);
                ok &= !editor->sourceText().contains(QStringLiteral("**bold**"));
                QKeyEvent undo_key(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
                QApplication::sendEvent(editor, &undo_key);
                ok &= editor->encodedBytes() == bytes;
                QTemporaryDir directory(QDir::currentPath() + QStringLiteral("/out/tests/editor-XXXXXX"));
                const auto path = directory.filePath(QStringLiteral("source.md"));
                QString save_error;
                using Result = wolfmark::qt::DocumentEditor::SaveResult;
                ok &= directory.isValid() && editor->saveFile(path, {}, &save_error) == Result::Saved;
                QFile saved(path);
                saved.open(QIODevice::ReadOnly);
                ok &= saved.readAll() == bytes;
                saved.close();
                QFile external(path);
                external.open(QIODevice::WriteOnly);
                external.write("external"); external.close();
                editor->insertPlainText(QStringLiteral("unsaved"));
                ok &= editor->saveFile(path, editor->diskFingerprint(), &save_error) == Result::Conflict;
                ok &= editor->document()->isModified();
                const auto retained = editor->sourceText();
                ok &= !editor->loadBytes(QByteArray("\xff\xfe\0x", 4), &save_error) && editor->sourceText() == retained;
                const auto permissions = QFile::permissions(path);
                const auto write_permissions = QFileDevice::WriteOwner | QFileDevice::WriteUser | QFileDevice::WriteGroup | QFileDevice::WriteOther;
                const bool read_only = QFile::setPermissions(path, permissions & ~write_permissions);
                ok &= read_only && editor->saveFile(path, wolfmark::qt::DocumentEditor::fingerprint(path), &save_error) == Result::Error;
                ok &= QFile::setPermissions(path, permissions) && editor->document()->isModified();
                ok &= editor->saveFile(directory.filePath(QStringLiteral("missing/subdir/file.md")), {}, &save_error) == Result::Error;
                ok &= editor->document()->isModified();
                for (int pass = 0; pass < 30; ++pass) {
                    auto* closing = new wolfmark::qt::DocumentEditor(api_);
                    ok &= closing->loadBytes(bytes.repeated(100), &save_error);
                    closing->setLivePreview(true);
                    delete closing;
                }
                std::fprintf(stdout, "WOLFMARK_SMOKE editor_core=%s roundtrip=checked undo=checked markers=checked conflict=checked analysis_us=%llu\n",
                             ok ? "ok" : "failed", static_cast<unsigned long long>(editor->analysisMicros()));
                std::fflush(stdout);
                qApp->exit(ok ? 0 : 18);
            });
            poll->start(25);
            return;
        }
        if (mode == QStringLiteral("scroll-stability")) {
            QTimer::singleShot(250, this, [this] {
                const auto before = api_->backend_counters(backend_);
                document_->verticalScrollBar()->setValue(document_->verticalScrollBar()->maximum() * 3 / 4);
                const auto state = document_->interactionState();
                reloadSession(active_, true);
                const int restored = document_->verticalScrollBar()->value();
                QTimer::singleShot(0, this, [this, before, state, restored] {
                    const int after = document_->verticalScrollBar()->value();
                    bool ok = state.scroll > 0 && restored > 0 && after > 0;
                    const auto constructions = document_->constructionCount();
                    const QPointF position(80, 80);
                    const auto wheel = [this, position](int angle, int pixel = 0) {
                        QWheelEvent event(position, document_->viewport()->mapToGlobal(position.toPoint()),
                                          QPoint(0, pixel), QPoint(0, angle), Qt::NoButton,
                                          Qt::NoModifier, Qt::ScrollUpdate, false);
                        QApplication::sendEvent(document_->viewport(), &event);
                    };
                    for (int pass = 0; pass < 200; ++pass) wheel(-12000);
                    ok &= document_->verticalScrollBar()->value() == document_->verticalScrollBar()->maximum();
                    wheel(std::numeric_limits<int>::max());
                    ok &= document_->verticalScrollBar()->value() == 0;
                    wheel(std::numeric_limits<int>::min());
                    ok &= document_->verticalScrollBar()->value() == document_->verticalScrollBar()->maximum();
                    wheel(0, 42);
                    for (const int percent : {80, 100, 125, 150, 100}) {
                        document_->changeZoom(percent - document_->zoomPercent());
                        wheel(std::numeric_limits<int>::min());
                        if (document_->verticalScrollBar()->maximum() > 0)
                            ok &= document_->verticalScrollBar()->value() > 0;
                    }
                    const auto counters = api_->backend_counters(backend_);
                    ok &= counters.load_count == before.load_count + 1 &&
                          counters.parse_count == before.parse_count + 1 &&
                          constructions == document_->constructionCount();
                    std::fprintf(stdout, "WOLFMARK_SMOKE scroll_stability=%s before=%d restored=%d after=%d reloads=%llu\n",
                                 ok ? "ok" : "failed", state.scroll, restored, after,
                                 static_cast<unsigned long long>(counters.load_count - before.load_count));
                    std::fflush(stdout);
                    document_->verticalScrollBar()->setValue(document_->verticalScrollBar()->maximum() / 2);
                    qputenv("WOLFMARK_REDUCED_MOTION", "0");
                    const auto stable = std::make_shared<bool>(ok);
                    const auto pass = std::make_shared<int>(0);
                    auto* burst = new QTimer(this);
                    QObject::connect(burst, &QTimer::timeout, this, [this, burst, stable, pass, position] {
                        QWheelEvent input(position, document_->viewport()->mapToGlobal(position.toPoint()), QPoint(),
                                          QPoint(0, *pass == 8 ? 120 : -12000), Qt::NoButton, Qt::NoModifier,
                                          Qt::ScrollUpdate, false);
                        QApplication::sendEvent(document_->viewport(), &input);
                        if (*pass == 4) { resize(width() - 20, height()); changeZoom(25); }
                        if (*pass == 12) { resize(width() + 20, height()); changeZoom(-25); }
                        *stable &= document_->verticalScrollBar()->maximum() == 0 || document_->verticalScrollBar()->value() > 0;
                        if (++*pass < 20) return;
                        burst->stop();
                        QTimer::singleShot(1200, this, [this, stable] {
                            *stable &= document_->verticalScrollBar()->maximum() == 0 || document_->verticalScrollBar()->value() > 0;
                            std::fprintf(stdout, "WOLFMARK_SMOKE scroll_burst=%s final=%d maximum=%d\n", *stable ? "ok" : "failed",
                                         document_->verticalScrollBar()->value(), document_->verticalScrollBar()->maximum());
                            std::fflush(stdout); qApp->exit(*stable ? 0 : 7);
                        });
                    });
                    burst->start(8);
                });
            });
            return;
        }
        if (mode == QStringLiteral("startup-arguments")) {
            QTimer::singleShot(0, this, [this] {
                QStringList names;
                names.reserve(static_cast<qsizetype>(documents_.size()));
                for (const auto& session : documents_)
                    names.push_back(QFileInfo(session->canonical_path).fileName());
                const auto joined = names.join(QLatin1Char('|')).toUtf8();
                std::fprintf(stdout, "WOLFMARK_SMOKE startup_arguments=ok documents=%zu names=%s\n",
                             documents_.size(), joined.constData());
                std::fflush(stdout);
                QCoreApplication::exit(0);
            });
            return;
        }
        if (mode == QStringLiteral("settings")) {
            QTimer::singleShot(0, this, [this] {
                const auto root = qEnvironmentVariable("WOLFMARK_SETTINGS_ROOT");
                const auto migrated_directory = QDir(root).filePath(QStringLiteral("legacy-documents"));
                QSettings legacy;
                const bool migrated = user_settings_.lastOpenDirectory() == migrated_directory &&
                    !legacy.contains(QStringLiteral("lastOpenDirectory"));

                user_settings_.setCheckOnStartup(false);
                user_settings_.setIncludePrereleases(true);
                user_settings_.setEditorWordWrap(false);
                user_settings_.setLastOpenDirectory(QDir(root).filePath(QStringLiteral("new-documents")));
                QString save_error;
                const bool saved = user_settings_.save(&save_error);
                const auto reloaded = wolfmark::qt::UserSettings::load(false);
                const bool round_trip = saved && !reloaded.updates().check_on_startup &&
                    reloaded.updates().include_prereleases &&
                    !reloaded.editorWordWrap() &&
                    reloaded.lastOpenDirectory() == user_settings_.lastOpenDirectory();

                QFile malformed(user_settings_.filePath());
                const bool malformed_written = malformed.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                    malformed.write("{not valid json") > 0;
                malformed.close();
                const auto recovered = wolfmark::qt::UserSettings::load(false);
                const auto backups = QDir(root).entryList(
                    {QStringLiteral("settings.invalid-*.json")}, QDir::Files);
                const bool malformed_safe = malformed_written &&
                    !recovered.recoveryWarning().isEmpty() && backups.size() == 1;

                const bool path_ok = QFileInfo(user_settings_.filePath()).absolutePath() ==
                    QFileInfo(root).absoluteFilePath();
                const bool ok = migrated && round_trip && malformed_safe && path_ok;
                std::fprintf(stdout,
                    "SETTINGS migration=%s round_trip=%s malformed=%s atomic=%s path=%s\n",
                    migrated ? "ok" : "failed", round_trip ? "ok" : "failed",
                    malformed_safe ? "recovered" : "failed", saved ? "ok" : "failed",
                    path_ok ? "ok" : "failed");
                std::fprintf(stdout, "WOLFMARK_SMOKE settings=%s\n", ok ? "ok" : "failed");
                std::fflush(stdout);
                QDir(root).removeRecursively();
                QCoreApplication::exit(ok ? 0 : 32);
            });
            return;
        }
        if (mode == QStringLiteral("portable-settings")) {
            QTimer::singleShot(0, this, [this] {
                const auto expected = QDir(QCoreApplication::applicationDirPath())
                    .filePath(QStringLiteral("data/settings.json"));
                user_settings_.setCheckOnStartup(false);
                QString save_error;
                const bool saved = user_settings_.save(&save_error);
                const bool ok = user_settings_.portable() &&
                    QFileInfo(user_settings_.filePath()).absoluteFilePath() ==
                        QFileInfo(expected).absoluteFilePath() &&
                    saved && QFileInfo::exists(expected);
                std::fprintf(stdout, "WOLFMARK_SMOKE portable_settings=%s path=%s\n",
                             ok ? "ok" : "failed",
                             user_settings_.filePath().toUtf8().constData());
                std::fflush(stdout);
                QFile::remove(expected);
                QDir().rmdir(QFileInfo(expected).absolutePath());
                QCoreApplication::exit(ok ? 0 : 34);
            });
            return;
        }
        if (mode == QStringLiteral("settings-ui")) {
            QTimer::singleShot(0, this, [this] {
                auto page = qEnvironmentVariable("WOLFMARK_SNAPSHOT_SETTINGS_PAGE");
                if (page != QStringLiteral("about") && page != QStringLiteral("editor"))
                    page = QStringLiteral("updates");
                const auto path = QDir::current().absoluteFilePath(
                    QStringLiteral("out/visual/dev7/settings-%1.png").arg(page));
                showSettings(path);
                const bool ok = QFileInfo::exists(path);
                std::fprintf(stdout, "WOLFMARK_SMOKE settings_ui=%s path=%s\n",
                             ok ? "ok" : "failed", path.toUtf8().constData());
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 35);
            });
            return;
        }
        if (mode == QStringLiteral("updates")) {
            update_manager_->check(true, true, [this](wolfmark::qt::UpdateCheckResult first) {
                const bool first_ok = first.status == wolfmark::qt::UpdateCheckResult::Status::Available &&
                    first.release.version == QStringLiteral("0.1.0-dev.8");
                update_manager_->check(true, true,
                    [this, first_ok](wolfmark::qt::UpdateCheckResult cached) {
                    const bool cache_ok = cached.status ==
                        wolfmark::qt::UpdateCheckResult::Status::Available &&
                        cached.release.version == QStringLiteral("0.1.0-dev.8");
                    update_manager_->check(true, true,
                        [first_ok, cache_ok](wolfmark::qt::UpdateCheckResult failure) {
                        const bool failure_ok = failure.status ==
                            wolfmark::qt::UpdateCheckResult::Status::Error;
                        const bool ok = first_ok && cache_ok && failure_ok;
                        std::fprintf(stdout,
                            "UPDATES selection=%s cache_304=%s http_failure=%s\n",
                            first_ok ? "ok" : "failed", cache_ok ? "ok" : "failed",
                            failure_ok ? "safe" : "failed");
                        std::fprintf(stdout, "WOLFMARK_SMOKE updates=%s\n", ok ? "ok" : "failed");
                        std::fflush(stdout);
                        QCoreApplication::exit(ok ? 0 : 33);
                    });
                });
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
                std::fprintf(stdout, "WOLFMARK_SMOKE autoscroll_anchor=%s\n",
                             ok ? "ok" : "failed");
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 20);
            });
            return;
        }
        if (mode == QStringLiteral("native-window")) {
            QTimer::singleShot(250, this, [this] {
                using wolfmark::qt::windows::HitRole;
                const auto status = wolfmark::qt::windows::nativeFrameStatus(this);
                const auto role_at = [this](QWidget* widget) {
                    return wolfmark::qt::windows::hitTest(
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
                    wolfmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_, QPoint(1, 1), false) ==
                        HitRole::TopLeft &&
                    wolfmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(width() / 2, 1), false) == HitRole::Top &&
                    wolfmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(1, height() / 2), false) == HitRole::Left &&
                    wolfmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(width() - 2, height() / 2), false) == HitRole::Right &&
                    wolfmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(1, height() - 2), false) == HitRole::BottomLeft &&
                    wolfmark::qt::windows::hitTest(
                        this, title_bar_, minimize_, maximize_, close_,
                        QPoint(width() - 2, height() - 2), false) == HitRole::BottomRight;
                const bool fullscreen_client = wolfmark::qt::windows::hitTest(
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
                    const auto maximized_status =
                        wolfmark::qt::windows::nativeFrameStatus(this);
                    native_maximize_click();
                    QTimer::singleShot(100, this,
                        [this, status, styles, captions, minimize_client, maximize_hit, close_client,
                         title, edges, fullscreen_client,
                         before, constructions, normal_geometry, maximized, maximized_status] {
                        const auto after = api_->backend_counters(backend_);
                        const auto restored_status =
                            wolfmark::qt::windows::nativeFrameStatus(this);
                        const bool restored = !isMaximized() && geometry() == normal_geometry;
                        const bool corners =
                            (status.corner_preference == 0 || status.corner_preference == 2) &&
                            (maximized_status.corner_preference == 0 ||
                             maximized_status.corner_preference == 1) &&
                            (restored_status.corner_preference == 0 ||
                             restored_status.corner_preference == 2);
                        const bool counters = before.parse_count == after.parse_count &&
                            before.load_count == after.load_count &&
                            before.image_request_count == after.image_request_count &&
                            constructions == document_->constructionCount();
                        const bool ok = styles && captions && title && edges &&
                            fullscreen_client && maximized && restored && corners && counters;
                        std::fprintf(stdout,
                            "NATIVE_WINDOW styles=%s thick_frame=%s popup=%s "
                            "caption_hit=%s max_hit=%s min_client=%s close_client=%s "
                            "resize_hits=%s fullscreen_hit=%s buttons=%s corners=%s(%d/%d/%d) counters=%s\n",
                            styles ? "ok" : "failed", status.thick_frame ? "yes" : "no",
                            status.popup ? "yes" : "no", title ? "ok" : "failed",
                            maximize_hit ? "ok" : "failed",
                            minimize_client ? "ok" : "failed",
                            close_client ? "ok" : "failed", edges ? "ok" : "failed",
                            fullscreen_client ? "client" : "failed",
                            maximized && restored ? "ok" : "failed",
                            corners ? "ok" : "failed", status.corner_preference,
                            maximized_status.corner_preference,
                            restored_status.corner_preference,
                            counters ? "stable" : "changed");
                        std::fprintf(stdout, "WOLFMARK_SMOKE native_window=%s\n",
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
                    const int geometry_delta = final_document_y - provisional_document_y;
                    const bool geometry_resolved = final_document_y >= 0;
                    const bool owner = document_->completedNavigationAnchor() == target;
                    const bool completed = !document_->navigationActive() &&
                        document_->pendingImageDecodes() == 0 && elapsed->elapsed() < 6000;
                    const bool motion_stopped = !document_->scrollMotionRunning();
                    const bool range_expanded = final_maximum > provisional_maximum;
                    const auto after = api_->backend_counters(backend_);
                    const bool counters = before.parse_count == after.parse_count &&
                        before.load_count == after.load_count &&
                        constructions == document_->constructionCount();
                    const bool image_delivery_observed = missing || failed ||
                        document_->navigationImageRetargetCount() > 0;
                    const bool layout_settled = missing || failed ||
                        document_->layoutGeneration() > generation || std::abs(geometry_delta) <= 3;
                    const bool ok = activation && latest_wins && expected_pending && immediate &&
                        no_motion && landing && geometry_resolved && owner && completed &&
                        motion_stopped && counters && image_delivery_observed && layout_settled;

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
                        "WOLFMARK_SMOKE outline_reflow=%s one_click=%s latest_wins=%s "
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
                        std::fprintf(stdout, "WOLFMARK_SMOKE scroll_profile=ok\n");
                        const auto output_path = qEnvironmentVariable("WOLFMARK_SCROLL_PROFILE_OUTPUT");
                        if (!output_path.isEmpty()) {
                            QFile output(output_path);
                            if (output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                                output.write(summary);
                                output.write("\nWOLFMARK_SMOKE scroll_profile=ok\n");
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
                    std::fprintf(stdout, "WOLFMARK_SMOKE multidoc_watcher=failed reason=document_count\n");
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
                                      file.write("\nWolfmark background watcher update.\n") > 0;
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
                            QStringLiteral("Wolfmark background watcher update."));
                        const bool result = appended && active_stable && background_loaded &&
                            background_parse_stable && content_updated;
                        std::fprintf(stdout,
                            "WOLFMARK_SMOKE multidoc_watcher=%s active=%s background_load_delta=%lld background_parse_delta=%lld content=%s\n",
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
                        // Two animated reveals before the first paint must
                        // land on the latest requested outline destination.
                        // Otherwise the old target survives the second reveal.
                        const bool far_reveal = sidebar_->revealOutlineAnchor(
                            QStringLiteral("end-clamped-heading"), true);
                        const int far_target = sidebar_->outlineScrollTarget();
                        const bool near_reveal = sidebar_->revealOutlineAnchor(
                            QStringLiteral("heading-after-prose"), true);
                        const bool outline_latest_wins = far_reveal && near_reveal &&
                            far_target > 0 && sidebar_->outlineScrollTarget() == 0;
                        sidebar_->cancelOutlineScroll();
                        const bool cancelled = !sidebar_->outlineScrollRunning();
                        const auto after = api_->backend_counters(backend_);
                        const bool counters = before.parse_count == after.parse_count &&
                            before.load_count == after.load_count &&
                            constructions == document_->constructionCount();
                        const bool ok = distinct >= 3 && largest_jump > 0 && landed && pixel_direct &&
                            home_end_direct && reversed_target && direct_navigation &&
                            partial_wheel && outline_latest_wins && cancelled && counters;
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
                                     "WOLFMARK_SMOKE motion=%s counters=%s partial_wheel=%s "
                                     "outline_retarget=%s cancelled=%s\n",
                                     ok ? "ok" : "failed", counters ? "stable" : "changed",
                                     partial_wheel ? "ok" : "failed",
                                     outline_latest_wins ? "ok" : "failed",
                                     cancelled ? "ok" : "failed");
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
                            width_largest_jump < wolfmark::style::metric::sidebar_width / 3 &&
                            sidebar_->isVisible() &&
                            sidebar_->width() == sidebarTargetWidth();
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
                            "WOLFMARK_SMOKE motion=%s counters=%s image_request_delta=%llu\n",
                            final_ok ? "ok" : "failed", counters ? "stable" : "changed",
                            static_cast<unsigned long long>(after.image_request_count - before.image_request_count));
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
                std::vector<WolfmarkCounters> counters;
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
                             "WOLFMARK_SMOKE multidoc=%s duplicate=%s state=%s counters=%s plain_text=%s switch_average_us=%lld final=%s\n",
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
                             "WOLFMARK_SMOKE plaintext=%s chars=%lld construction_us=%llu parse_count=%llu images=%d\n",
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
                std::fprintf(stdout, "WOLFMARK_SMOKE navigation=%s outline_keyboard=%s transition_counters=%s\n",
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
                std::fprintf(stdout, "WOLFMARK_SMOKE icon=%s sizes=%lld\n",
                             ok ? "ok" : "failed", static_cast<long long>(sizes.size()));
                std::fflush(stdout);
                QCoreApplication::exit(ok ? 0 : 11);
            });
            return;
        }
        if (mode == QStringLiteral("snapshot")) {
            const auto width = qEnvironmentVariableIntValue("WOLFMARK_SNAPSHOT_WIDTH");
            const auto height = qEnvironmentVariableIntValue("WOLFMARK_SNAPSHOT_HEIGHT");
            if (width > 0 && height > 0) resize(width, height);
            QTimer::singleShot(500, this, [this] {
                const auto requested_mode = qEnvironmentVariable("WOLFMARK_SNAPSHOT_MODE");
                if (requested_mode == QStringLiteral("source")) setDocumentMode(2);
                if (requested_mode == QStringLiteral("live") && active_) { setDocumentMode(1); active_->editor->goToLine(12); }
                const auto zoom = qEnvironmentVariableIntValue("WOLFMARK_SNAPSHOT_ZOOM");
                if (zoom > 0 && document_) changeZoom(zoom - document_->zoomPercent());
                if (qEnvironmentVariable("WOLFMARK_SNAPSHOT_NO_SIDEBAR") == QStringLiteral("1")) {
                    sidebar_requested_ = false;
                    updateSidebarVisibility();
                }
                const auto scroll = qEnvironmentVariableIntValue("WOLFMARK_SNAPSHOT_SCROLL");
                if (scroll > 0 && document_) document_->verticalScrollBar()->setValue(scroll);
                if (document_ && qEnvironmentVariable("WOLFMARK_SNAPSHOT_SELECTION") == QStringLiteral("1")) {
                    if (active_->mode == OpenDocumentSession::Mode::Reader) document_->selectAll();
                    else active_->editor->selectAll();
                }
                if (qEnvironmentVariable("WOLFMARK_SNAPSHOT_MENU") == QStringLiteral("1")) {
                    auto* menu = findChild<QMenu*>(QStringLiteral("documentMenu"));
                    menu->popup(mapToGlobal(QPoint(this->width() - 300, 40)));
                    menu->setActiveAction(menu->actions().first());
                }
                QTimer::singleShot(300, this, [this] {
                    auto name = qEnvironmentVariable("WOLFMARK_SNAPSHOT_NAME", "wolfmark-ui");
                    for (auto& character : name) {
                        if (!character.isLetterOrNumber() && character != QLatin1Char('-'))
                            character = QLatin1Char('_');
                    }
                    QDir::current().mkpath(QStringLiteral("out/visual/dev7"));
                    const auto output = QDir::current().absoluteFilePath(
                        QStringLiteral("out/visual/dev7/%1.png").arg(name));
                    const auto pixels = qEnvironmentVariable("WOLFMARK_SNAPSHOT_MENU") == QStringLiteral("1")
                        ? findChild<QMenu*>(QStringLiteral("documentMenu"))->grab() : grab();
                    const bool saved = pixels.save(output, "PNG");
                    std::fprintf(stdout, "WOLFMARK_SMOKE snapshot=%s path=%s\n",
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
                std::fprintf(stdout, "WOLFMARK_SMOKE document_style=%s\n", ok ? "ok" : "failed");
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
                             "WOLFMARK_SMOKE render=%s chars=%lld construction_us=%llu parse=%llu load=%llu selection_copy=%s code_copy=%s\n",
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
                                         "WOLFMARK_SMOKE layout=%s parse_delta=%lld load_delta=%lld construction_delta=%lld image_request_delta=%lld restored=%s\n",
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
                                     "WOLFMARK_SMOKE maximize=%s parse_delta=%lld load_delta=%lld construction_delta=%lld image_request_delta=%lld geometry=%s\n",
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
                                     "WOLFMARK_SMOKE layout_normal=%s parse_delta=%lld load_delta=%lld construction_delta=%lld image_request_delta=%lld restored=%s geometry=%s\n",
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
                                      file.write("\nWolfmark watcher smoke update.\n") > 0;
                file.close();
                QTimer::singleShot(700, this, [this, before, appended] {
                    const auto after = api_->backend_counters(backend_);
                    const bool ok = appended && after.load_count > before.load_count &&
                                    after.parse_count > before.parse_count;
                    std::fprintf(stdout,
                                 "WOLFMARK_SMOKE watcher=%s load_delta=%lld parse_delta=%lld\n",
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
                             "WOLFMARK_SMOKE images=%s discovered=%d loaded=%d failed=%d pending=%d requests=%llu cache_bytes=%llu completion_ms=%lld elapsed_ms=%lld\n",
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
        // Keep the document breadcrumb legible when constrained. Ctrl+F remains
        // available even when the decorative search entry cannot fit safely.
        if (title_search_)
            title_search_->setVisible(width() >= 960);
    }

    void closeEvent(QCloseEvent* event) override {
        for (const auto& session : documents_) {
            if (!confirmDiscardOrSave(session.get())) { event->ignore(); return; }
        }
        QWidget::closeEvent(event);
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
        if (wolfmark::qt::windows::handleNativeFrameEvent(
                this, title_bar_, minimize_, maximize_, close_, fullscreen_, message, result))
            return true;
        return QWidget::nativeEvent(event_type, message, result);
    }
#endif

private:
    [[nodiscard]] int sidebarTargetWidth() const {
        // Match the approved spacious sidebar at wide desktop sizes without
        // taking a disproportionate share on smaller laptop windows.
        return std::clamp(static_cast<int>(std::lround(width() * 0.185)),
                          wolfmark::style::metric::sidebar_width, 304);
    }

    void updateSidebarVisibility() {
        if (sidebar_ == nullptr) return;
        const bool visible = !fullscreen_ && sidebar_requested_ && (sidebar_explicit_ || width() >= 1000);
        const bool reduced = qEnvironmentVariable("WOLFMARK_REDUCED_MOTION") == QStringLiteral("1");
        const int target = visible ? sidebarTargetWidth() : 0;
        if (reduced || !isVisible()) {
            sidebar_animation_.stop();
            sidebar_target_visible_ = visible;
            sidebar_->setFixedWidth(target);
            sidebar_->setVisible(visible);
            if (open_ != nullptr) open_->setVisible(!visible);
            // Branding stays visible in the global titlebar during sidebar changes.
            return;
        }
        if (sidebar_animation_.state() == QAbstractAnimation::Running &&
            sidebar_target_visible_ == visible &&
            sidebar_animation_.endValue().toInt() == target) return;
        const int current = sidebar_->isVisible() ? sidebar_->width() : 0;
        // Ordinary window resizing must follow the pointer, not continually
        // restart a short width animation (which appears as lag or wobble).
        if (visible && sidebar_target_visible_ && sidebar_->isVisible() &&
            sidebar_animation_.state() != QAbstractAnimation::Running) {
            if (current != target) sidebar_->setFixedWidth(target);
            if (open_ != nullptr) open_->hide();
            return;
        }
        if (current != target || sidebar_->isVisible() != visible) {
            sidebar_animation_.stop();
            sidebar_target_visible_ = visible;
            sidebar_->setFixedWidth(current);
            sidebar_->show();
            if (open_ != nullptr) open_->hide();
            // Header identity no longer depends on animation progress.
            const int full_duration = visible ? 170 : 150;
            const int remaining = std::abs(target - current);
            sidebar_animation_.setStartValue(current);
            sidebar_animation_.setEndValue(target);
            sidebar_animation_.setDuration(std::max(60, full_duration * remaining /
                                                        std::max(1, sidebarTargetWidth())));
            sidebar_animation_.setEasingCurve(QEasingCurve::InOutQuad);
            sidebar_animation_.start();
            return;
        }
        if (open_ != nullptr) open_->setVisible(!visible);
        // Header brand is independent from navigation visibility.
    }

    void buildUi() {
        // One top-level titlebar now spans the entire app, including the sidebar.
        // This matches the approved desktop structure without shifting document
        // state or changing any native hit-test ownership.
        auto* window_root = new QVBoxLayout(this);
        window_root->setContentsMargins(0, 0, 0, 0);
        window_root->setSpacing(0);
        auto* body = new QWidget(this);
        body->setObjectName(QStringLiteral("windowBody"));
        body->setAttribute(Qt::WA_StyledBackground);
        auto* shell = new QHBoxLayout(body);
        shell->setContentsMargins(0, 0, 0, 0);
        shell->setSpacing(0);
        window_root->addWidget(body, 1);
        sidebar_ = new wolfmark::qt::DocumentSidebar;
        QObject::connect(&sidebar_animation_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant& width) {
            const int value = width.toInt();
            sidebar_width_samples_.push_back(value);
            sidebar_->setFixedWidth(value);
        });
        QObject::connect(&sidebar_animation_, &QVariantAnimation::finished, this, [this] {
            sidebar_->setFixedWidth(sidebar_target_visible_ ? sidebarTargetWidth() : 0);
            sidebar_->setVisible(sidebar_target_visible_);
            if (open_ != nullptr) open_->setVisible(!sidebar_target_visible_);
            // Global titlebar branding remains visible.
        });
        sidebar_->open = [this] { chooseDocument(); };
        sidebar_->reload = [this] { reloadDocument(); };
        sidebar_->navigate = [this](const QString& anchor) {
            if (document_) {
                if (active_ && active_->mode != OpenDocumentSession::Mode::Reader) {
                    if (anchor.startsWith(QStringLiteral("source-line:"))) {
                        active_->editor->goToLine(anchor.sliced(12).toInt());
                    } else {
                        document_->navigateToAnchor(anchor, false);
                        active_->editor->goToLine(document_->sourceLineAtViewport());
                    }
                    focusDocument();
                    return;
                }
                document_->navigateToAnchor(anchor);
                document_->setFocus(Qt::OtherFocusReason);
            }
        };
        sidebar_->activate_document = [this](int index) { activateDocument(index); };
        sidebar_->close_document = [this](int index) { closeDocument(index); };
        shell->addWidget(sidebar_);
        auto* content = new QWidget;
        content->setObjectName(QStringLiteral("contentShell"));
        content->setAttribute(Qt::WA_StyledBackground);
        shell->addWidget(content, 1);
        auto* root = new QVBoxLayout(content);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        title_bar_ = new wolfmark::qt::WolfTitleBar(this);
        auto* title_layout = new QHBoxLayout(title_bar_);
        title_layout->setContentsMargins(14, 0, 0, 0);
        title_layout->setSpacing(8);

        title_symbol_ = new QLabel(title_bar_);
        title_symbol_->setObjectName(QStringLiteral("headerBrandSymbol"));
        title_symbol_->setFixedSize(30, 30);
        title_symbol_->setAlignment(Qt::AlignCenter);
        title_symbol_->setPixmap(applicationIcon().pixmap(24, 24));
        title_symbol_->setAttribute(Qt::WA_TransparentForMouseEvents);
        title_layout->addWidget(title_symbol_);
        auto* brand_title = new QLabel(QStringLiteral("Wolfmark"));
        brand_title->setObjectName(QStringLiteral("headerBrandTitle"));
        brand_title->setAttribute(Qt::WA_TransparentForMouseEvents);
        brand_title->setMinimumWidth(88);
        title_layout->addWidget(brand_title);
        auto* brand_separator = new QFrame;
        brand_separator->setObjectName(QStringLiteral("headerSeparator"));
        brand_separator->setFixedSize(1, 22);
        title_layout->addSpacing(12);
        title_layout->addWidget(brand_separator);
        title_layout->addSpacing(7);

        auto* navigation = new WolfButton(QString());
        navigation->setProperty("titleTool", true);
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

        title_label_ = new ElidingLabel(QStringLiteral("Documents"));
        title_label_->setObjectName(QStringLiteral("documentTitle"));
        title_label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        title_label_->setMinimumWidth(60);
        title_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        title_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
        title_layout->addWidget(title_label_, 1);
        title_search_ = new WolfButton(QStringLiteral("⌕  Search in document…   Ctrl+F"));
        title_search_->setObjectName(QStringLiteral("titleSearch"));
        title_search_->setProperty("titleTool", true);
        title_search_->setAccessibleName(QStringLiteral("Find in current document"));
        title_search_->setToolTip(QStringLiteral("Find in current document (Ctrl+F)"));
        title_search_->setEnabled(false);
        title_search_->setFixedWidth(258);
        QObject::connect(title_search_, &QPushButton::clicked, this, [this] { showFind(); });
        title_layout->addWidget(title_search_);

        document_actions_ = new QWidget;
        document_actions_->setObjectName(QStringLiteral("documentCommandBar"));
        document_actions_->setAttribute(Qt::WA_StyledBackground);
        document_actions_->setFixedHeight(48);
        auto* actions = new QHBoxLayout(document_actions_);
        actions->setContentsMargins(16, 4, 16, 4);
        actions->setSpacing(6);
        open_ = new WolfButton(QStringLiteral("Open"));
        open_->setProperty("titleTool", true);
        open_->setAccessibleName(QStringLiteral("Open document"));
        open_->setToolTip(QStringLiteral("Open Markdown or text file (Ctrl+O)"));
        QObject::connect(open_, &QPushButton::clicked, this, [this] { chooseDocument(); });
        actions->addWidget(open_);
        actions->addSpacing(6);

        // Navigation arrows are real actions over the currently opened tabs.
        // No invented document history or placeholder controls.
        previous_tab_ = new WolfButton(QStringLiteral("←"), document_actions_);
        previous_tab_->setObjectName(QStringLiteral("documentNavigationButton"));
        previous_tab_->setFixedSize(32, 32);
        previous_tab_->setAccessibleName(QStringLiteral("Previous open document"));
        previous_tab_->setToolTip(QStringLiteral("Previous open document (Alt+Left)"));
        QObject::connect(previous_tab_, &QPushButton::clicked, this, [this] {
            const int count = static_cast<int>(documents_.size());
            if (count > 1) activateDocument((activeDocumentIndex() + count - 1) % count);
        });
        next_tab_ = new WolfButton(QStringLiteral("→"), document_actions_);
        next_tab_->setObjectName(QStringLiteral("documentNavigationButton"));
        next_tab_->setFixedSize(32, 32);
        next_tab_->setAccessibleName(QStringLiteral("Next open document"));
        next_tab_->setToolTip(QStringLiteral("Next open document (Alt+Right)"));
        QObject::connect(next_tab_, &QPushButton::clicked, this, [this] {
            const int count = static_cast<int>(documents_.size());
            if (count > 1) activateDocument((activeDocumentIndex() + 1) % count);
        });
        actions->addWidget(previous_tab_);
        actions->addWidget(next_tab_);
        actions->addSpacing(8);

        // The visible command bar deliberately separates document modes from window chrome.
        // All visible mode buttons trigger the same QActionGroup command path.
        mode_group_ = new QActionGroup(this);
        mode_group_->setExclusive(true);
        int mode_id = 0;
        for (const auto& name : {QStringLiteral("Reader"), QStringLiteral("Live Preview"),
                                 QStringLiteral("Source")}) {
            auto* action = new QAction(name, mode_group_);
            action->setCheckable(true);
            action->setData(mode_id++);
            mode_group_->addAction(action);
        }
        QObject::connect(mode_group_, &QActionGroup::triggered, this,
                         [this](QAction* action) { setDocumentMode(action->data().toInt()); });
        auto* mode_switch = new QWidget(document_actions_);
        mode_switch->setObjectName(QStringLiteral("documentModeGroup"));
        mode_switch->setAttribute(Qt::WA_StyledBackground);
        auto* mode_layout = new QHBoxLayout(mode_switch);
        mode_layout->setContentsMargins(3, 3, 3, 3);
        mode_layout->setSpacing(2);
        auto* mode_buttons = new QButtonGroup(mode_switch);
        mode_buttons->setExclusive(true);
        int segment_index = 0;
        for (const auto& mode_name : {QStringLiteral("Reader"),
                                      QStringLiteral("Live Preview"),
                                      QStringLiteral("Source")}) {
            auto* segment = new WolfButton(mode_name, mode_switch);
            QPixmap mode_pixmap(19, 19);
            mode_pixmap.fill(Qt::transparent);
            {
                QPainter paint(&mode_pixmap);
                paint.setRenderHint(QPainter::Antialiasing);
                paint.setPen(QPen(QColor(colour::bright), 1.5, Qt::SolidLine,
                                  Qt::RoundCap, Qt::RoundJoin));
                if (mode_name == QStringLiteral("Reader")) {
                    paint.drawLine(QPointF(9.5, 4.5), QPointF(9.5, 16));
                    paint.drawRoundedRect(QRectF(2.5, 3.5, 7, 12), 1.8, 1.8);
                    paint.drawRoundedRect(QRectF(9.5, 3.5, 7, 12), 1.8, 1.8);
                } else if (mode_name == QStringLiteral("Live Preview")) {
                    paint.drawRoundedRect(QRectF(2.5, 3.5, 14, 10), 1.5, 1.5);
                    paint.drawLine(QPointF(9.5, 13.5), QPointF(9.5, 16));
                    paint.drawLine(QPointF(5.8, 16), QPointF(13.2, 16));
                } else {
                    paint.drawLine(QPointF(7.0, 4.5), QPointF(3.0, 9.5));
                    paint.drawLine(QPointF(3.0, 9.5), QPointF(7.0, 14.5));
                    paint.drawLine(QPointF(12.0, 4.5), QPointF(16.0, 9.5));
                    paint.drawLine(QPointF(16.0, 9.5), QPointF(12.0, 14.5));
                    paint.drawLine(QPointF(11.0, 3), QPointF(8.0, 16));
                }
            }
            segment->setIcon(QIcon(mode_pixmap));
            segment->setIconSize(QSize(19, 19));
            segment->setObjectName(QStringLiteral("modeSegmentButton"));
            segment->setProperty("modeSegment", true);
            segment->setCheckable(true);
            segment->setFixedHeight(32);
            // Reserve actual glyph width plus horizontal padding at every DPI.
            // A QSS min-width of 58px alone truncates "Live Preview".
            segment->setFixedWidth(
                std::max(mode_name == QStringLiteral("Reader") ? 145 :
                         mode_name == QStringLiteral("Live Preview") ? 170 : 132,
                         segment->fontMetrics().horizontalAdvance(mode_name) + 54));
            segment->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            segment->setAccessibleName(QStringLiteral("%1 mode").arg(mode_name));
            segment->setToolTip(QStringLiteral("Switch to %1 mode").arg(mode_name));
            const int id = segment_index++;
            mode_buttons->addButton(segment, id);
            mode_segments_.push_back(segment);
            QObject::connect(segment, &QPushButton::clicked, this,
                             [this, id] { mode_group_->actions().at(id)->trigger(); });
            mode_layout->addWidget(segment);
        }
        actions->addWidget(mode_switch);
        actions->addStretch(1);

        save_button_ = new WolfButton(QStringLiteral("Save"));
        save_button_->setProperty("titleTool", true);
        save_button_->setAccessibleName(QStringLiteral("Save current document"));
        save_button_->setToolTip(QStringLiteral("Save changes (Ctrl+S)"));
        QObject::connect(save_button_, &QPushButton::clicked, this, [this] { saveSession(active_, false); });
        save_button_->hide();
        actions->addWidget(save_button_);

        document_status_ = new QLabel;
        document_status_->setObjectName(QStringLiteral("documentStatus"));
        document_status_->setAccessibleName(QStringLiteral("Document save status"));
        document_status_->setMaximumWidth(115);
        document_status_->hide();
        actions->addWidget(document_status_);
        actions->addSpacing(6);
        auto* zoom_group = new QWidget(document_actions_);
        zoom_group->setObjectName(QStringLiteral("documentZoomGroup"));
        zoom_group->setAttribute(Qt::WA_StyledBackground);
        auto* zoom_controls = new QHBoxLayout(zoom_group);
        zoom_controls->setContentsMargins(3, 2, 3, 2);
        zoom_controls->setSpacing(0);
        zoom_out_ = new WolfButton(QStringLiteral("−"));
        zoom_out_->setProperty("titleTool", true);
        zoom_out_->setFixedWidth(28);
        zoom_out_->setAccessibleName(QStringLiteral("Zoom out"));
        zoom_label_ = new WolfButton(QStringLiteral("100%"));
        zoom_label_->setProperty("titleTool", true);
        zoom_label_->setObjectName(QStringLiteral("zoomValue"));
        zoom_label_->setFixedWidth(52);
        zoom_label_->setAccessibleName(QStringLiteral("Choose document zoom"));
        zoom_label_->setToolTip(QStringLiteral("Choose zoom · Ctrl+mouse wheel · Ctrl+0 resets"));
        auto* zoom_menu = new QMenu(zoom_label_);
        for (const int percent : {80, 100, 125, 150, 200})
            zoom_menu->addAction(QStringLiteral("%1%").arg(percent), this,
                                 [this, percent] {
                if (active_) changeZoom(percent - active_->zoom_percent);
            });
        QObject::connect(zoom_label_, &QPushButton::clicked, this, [this, zoom_menu] {
            zoom_menu->popup(zoom_label_->mapToGlobal(QPoint(0, zoom_label_->height() + 4)));
        });
        zoom_in_ = new WolfButton(QStringLiteral("+"));
        zoom_in_->setProperty("titleTool", true);
        zoom_in_->setFixedWidth(28);
        zoom_in_->setAccessibleName(QStringLiteral("Zoom in"));
        QObject::connect(zoom_out_, &QPushButton::clicked, this, [this] { changeZoom(-10); });
        QObject::connect(zoom_in_, &QPushButton::clicked, this, [this] { changeZoom(10); });
        zoom_controls->addWidget(zoom_out_);
        zoom_controls->addWidget(zoom_label_);
        zoom_controls->addWidget(zoom_in_);
        actions->addWidget(zoom_group);
        auto* more = new WolfButton(QStringLiteral("⋯"));
        more->setProperty("titleTool", true);
        more->setAccessibleName(QStringLiteral("Document actions"));
        more->setToolTip(QStringLiteral("Document actions"));
        more->setFixedWidth(32);
        auto* menu = new QMenu(more);
        menu->setObjectName(QStringLiteral("documentMenu"));
        menu->addAction(QStringLiteral("Save\tCtrl+S"), this,
                        [this] { saveSession(active_, false); });
        menu->addAction(QStringLiteral("Save As…\tCtrl+Shift+S"), this,
                        [this] { saveSession(active_, true); });
        menu->addAction(QStringLiteral("Find…\tCtrl+F"), this,
                        [this] { showFind(); });
        menu->addSeparator();
        reload_action_ = menu->addAction(QStringLiteral("Reload\tF5"), this,
                                         [this] { reloadDocument(); });
        menu->addAction(QStringLiteral("Reset zoom"), this,
                        [this] { if (active_) changeZoom(100 - active_->zoom_percent); });
        menu->addSeparator();
        menu->addAction(QStringLiteral("Fullscreen\tF11"), this,
                        [this] { toggleFullscreen(); });
        auto* settings_action = new QAction(QStringLiteral("Settings…"), this);
        settings_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
        settings_action->setShortcutContext(Qt::WindowShortcut);
        QObject::connect(settings_action, &QAction::triggered, this, [this] { showSettings(); });
        addAction(settings_action);
        menu->addAction(settings_action);
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
        document_actions_->hide();
        auto* settings_button = new WolfButton(QStringLiteral("⚙  Settings"));
        settings_button->setProperty("titleTool", true);
        settings_button->setAccessibleName(QStringLiteral("Open Wolfmark Settings"));
        QObject::connect(settings_button, &QPushButton::clicked, this, [this] { showSettings(); });
        title_layout->addWidget(settings_button);

        using Caption = wolfmark::qt::CaptionButton;
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
        window_root->insertWidget(0, title_bar_);

        tabs_ = new CompactDocumentTabs;
        tabs_->setObjectName(QStringLiteral("documentTabs"));
        tabs_->setAccessibleName(QStringLiteral("Open document tabs"));
        // We supply a restrained close button instead of Fusion's bright red close box.
        tabs_->setTabsClosable(false);
        tabs_->setExpanding(false);
        tabs_->setElideMode(Qt::ElideMiddle);
        tabs_->setUsesScrollButtons(true);
        tabs_->hide();
        QObject::connect(tabs_, &QTabBar::currentChanged, this, [this](int index) { activateDocument(index); });
        QObject::connect(tabs_, &QTabBar::tabCloseRequested, this, [this](int index) { closeDocument(index); });
        tab_row_ = new QWidget;
        tab_row_->setObjectName(QStringLiteral("documentTabRow"));
        tab_row_->setAttribute(Qt::WA_StyledBackground);
        auto* tab_layout = new QHBoxLayout(tab_row_);
        tab_layout->setContentsMargins(4, 0, 12, 0);
        tab_layout->setSpacing(3);
        tab_layout->addWidget(tabs_, 0);
        auto* new_tab = new WolfButton(QStringLiteral("+"), tab_row_);
        new_tab->setObjectName(QStringLiteral("newDocumentTab"));
        new_tab->setFixedSize(30, 30);
        new_tab->setAccessibleName(QStringLiteral("Open another document"));
        new_tab->setToolTip(QStringLiteral("Open another document (Ctrl+O)"));
        QObject::connect(new_tab, &QPushButton::clicked, this, [this] { chooseDocument(); });
        tab_layout->addWidget(new_tab);
        tab_layout->addStretch(1);
        tab_row_->hide();
        root->addWidget(tab_row_);
        root->addWidget(document_actions_);

        find_bar_ = new QWidget;
        auto* find_layout = new QHBoxLayout(find_bar_);
        find_layout->setContentsMargins(12, 4, 12, 4);
        find_input_ = new QLineEdit;
        find_input_->setPlaceholderText(QStringLiteral("Find in current document"));
        find_input_->setAccessibleName(QStringLiteral("Find in current document"));
        QObject::connect(find_input_, &QLineEdit::returnPressed, this, [this] { findNext(false); });
        QObject::connect(find_input_, &QLineEdit::textEdited, this, [this] { findNext(false, true); });
        find_layout->addWidget(find_input_, 1);
        for (const bool backwards : {true, false}) {
            auto* button = new WolfButton(backwards ? QStringLiteral("Previous") : QStringLiteral("Next"));
            QObject::connect(button, &QPushButton::clicked, this, [this, backwards] { findNext(backwards); });
            find_layout->addWidget(button);
        }
        auto* close_find = new WolfButton(QStringLiteral("Close"));
        QObject::connect(close_find, &QPushButton::clicked, this, [this] { find_bar_->hide(); focusDocument(); });
        find_layout->addWidget(close_find);
        find_bar_->hide();
        root->addWidget(find_bar_);

        const auto add_shortcut = [this](const QKeySequence& keys, std::function<void()> callback) {
            auto* action = new QAction(this);
            action->setShortcut(keys);
            QObject::connect(action, &QAction::triggered, this, std::move(callback));
            addAction(action);
        };
        add_shortcut(QKeySequence::Save, [this] { saveSession(active_, false); });
        add_shortcut(QKeySequence::SaveAs, [this] { saveSession(active_, true); });
        add_shortcut(QKeySequence::Find, [this] { showFind(); });
        add_shortcut(QKeySequence::FindNext, [this] { findNext(false); });
        add_shortcut(QKeySequence::FindPrevious, [this] { findNext(true); });
        add_shortcut(QKeySequence(QStringLiteral("Alt+Left")), [this] {
            if (previous_tab_ && previous_tab_->isEnabled()) previous_tab_->click();
        });
        add_shortcut(QKeySequence(QStringLiteral("Alt+Right")), [this] {
            if (next_tab_ && next_tab_->isEnabled()) next_tab_->click();
        });

        update_banner_ = new QWidget;
        update_banner_->setObjectName(QStringLiteral("updateBanner"));
        auto* update_layout = new QHBoxLayout(update_banner_);
        update_layout->setContentsMargins(14, 7, 14, 7);
        update_layout->setSpacing(8);
        update_label_ = new QLabel;
        update_label_->setObjectName(QStringLiteral("updateLabel"));
        update_layout->addWidget(update_label_, 1);
        update_now_ = new WolfButton(QStringLiteral("Update now"));
        update_now_->setAccessibleName(QStringLiteral("Download Wolfmark update"));
        QObject::connect(update_now_, &QPushButton::clicked, this, [this] { downloadUpdate(); });
        update_layout->addWidget(update_now_);
        auto* release_notes = new WolfButton(QStringLiteral("Release notes"));
        release_notes->setAccessibleName(QStringLiteral("Open update release notes"));
        QObject::connect(release_notes, &QPushButton::clicked, this, [this] {
            if (!available_update_.has_value()) return;
            const auto url = available_update_->html_url;
            if (url.scheme() == QStringLiteral("https") &&
                url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0)
                QDesktopServices::openUrl(url);
        });
        update_layout->addWidget(release_notes);
        auto* later = new WolfButton(QStringLiteral("Later"));
        later->setAccessibleName(QStringLiteral("Dismiss update notification"));
        QObject::connect(later, &QPushButton::clicked, update_banner_, &QWidget::hide);
        update_layout->addWidget(later);
        update_banner_->hide();
        root->addWidget(update_banner_);

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
        empty_symbol->setPixmap(applicationIcon().pixmap(32, 32));
        auto* empty_title = new QLabel(QStringLiteral("Ready to read"));
        empty_title->setObjectName(QStringLiteral("emptyTitle"));
        identity->addWidget(empty_symbol);
        identity->addWidget(empty_title);
        identity->addStretch();
        prompt_layout->addLayout(identity);
        auto* empty_hint = new QLabel(QStringLiteral("Open a Markdown or text file, or drop one here."));
        empty_hint->setObjectName(QStringLiteral("emptyHint"));
        prompt_layout->addWidget(empty_hint);
        auto* empty_open = new WolfButton(QStringLiteral("Open document"));
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
        auto initial = user_settings_.lastOpenDirectory();
        if (initial.isEmpty() || !QFileInfo(initial).isDir()) {
            initial = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        }
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open document"), initial,
            QStringLiteral("Wolfmark documents (*.md *.markdown *.txt);;Markdown files (*.md *.markdown);;Text files (*.txt);;All files (*)"));
        if (!path.isEmpty()) {
            openDocument(path);
        }
    }

    void showSettings(const QString& snapshot_path = {}) {
        WolfSettingsDialog dialog(this);
        auto* shell = dialog.contentsLayout();

        auto* navigation = new QWidget;
        navigation->setObjectName(QStringLiteral("settingsNavigation"));
        navigation->setAttribute(Qt::WA_StyledBackground);
        navigation->setFixedWidth(218);
        auto* navigation_layout = new QVBoxLayout(navigation);
        navigation_layout->setContentsMargins(20, 24, 20, 20);
        navigation_layout->setSpacing(8);
        auto* identity = new QHBoxLayout;
        auto* symbol = new QLabel;
        symbol->setObjectName(QStringLiteral("brandSymbol"));
        symbol->setFixedSize(38, 38);
        symbol->setAlignment(Qt::AlignCenter);
        symbol->setPixmap(applicationIcon().pixmap(30, 30));
        auto* brand = new QLabel(QStringLiteral("Wolfmark"));
        brand->setObjectName(QStringLiteral("settingsBrand"));
        identity->addWidget(symbol);
        identity->addSpacing(7);
        identity->addWidget(brand);
        identity->addStretch();
        navigation_layout->addLayout(identity);
        navigation_layout->addSpacing(24);

        auto* updates_nav = new QPushButton(QStringLiteral("Updates"));
        auto* about_nav = new QPushButton(QStringLiteral("About"));
        for (auto* button : {updates_nav, about_nav}) {
            button->setProperty("nav", true);
            button->setCheckable(true);
            navigation_layout->addWidget(button);
        }
        auto* navigation_group = new QButtonGroup(&dialog);
        navigation_group->setExclusive(true);
        navigation_group->addButton(updates_nav, 0);
        navigation_group->addButton(about_nav, 1);
        updates_nav->setChecked(true);
        navigation_layout->addStretch();
        auto* navigation_version = new QLabel(QStringLiteral("%1\nFree and open source")
                                                  .arg(QCoreApplication::applicationVersion()));
        navigation_version->setObjectName(QStringLiteral("settingsHint"));
        navigation_layout->addWidget(navigation_version);
        shell->addWidget(navigation);

        auto* content = new QWidget;
        content->setObjectName(QStringLiteral("settingsContent"));
        content->setAttribute(Qt::WA_StyledBackground);
        auto* content_layout = new QVBoxLayout(content);
        content_layout->setContentsMargins(32, 28, 32, 24);
        content_layout->setSpacing(18);
        auto* pages = new QStackedWidget;
        pages->setObjectName(QStringLiteral("settingsPages"));
        content_layout->addWidget(pages, 1);
        shell->addWidget(content, 1);

        // All Settings pages share the same restrained editorial hierarchy.
        const auto addPageEyebrow = [](QVBoxLayout* target, const QString& name) {
            auto* label = new QLabel(name);
            label->setObjectName(QStringLiteral("settingsEyebrow"));
            target->addWidget(label);
        };
        const auto addOption = [](QVBoxLayout* target, QCheckBox* checkbox, QLabel* description) {
            auto* row = new QFrame;
            row->setObjectName(QStringLiteral("settingsOption"));
            row->setAttribute(Qt::WA_StyledBackground);
            auto* body = new QVBoxLayout(row);
            body->setContentsMargins(14, 10, 14, 12);
            body->setSpacing(4);
            body->addWidget(checkbox);
            body->addWidget(description);
            target->addWidget(row);
        };

        auto* updates_page = new QWidget;
        auto* layout = new QVBoxLayout(updates_page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(14);
        addPageEyebrow(layout, QStringLiteral("WOLFMARK  /  PREFERENCES"));

        auto* title = new QLabel(QStringLiteral("Updates"));
        title->setObjectName(QStringLiteral("settingsTitle"));
        layout->addWidget(title);
        auto* explanation = new QLabel(QStringLiteral(
            "Wolfmark checks the public GitHub Releases feed. No account, token, or telemetry is used."));
        explanation->setObjectName(QStringLiteral("settingsHint"));
        explanation->setWordWrap(true);
        layout->addWidget(explanation);

        auto* preferences = new QFrame;
        preferences->setObjectName(QStringLiteral("settingsCard"));
        preferences->setAttribute(Qt::WA_StyledBackground);
        auto* preference_layout = new QVBoxLayout(preferences);
        preference_layout->setContentsMargins(20, 19, 20, 20);
        preference_layout->setSpacing(8);
        auto* preference_title = new QLabel(QStringLiteral("Update preferences"));
        preference_title->setObjectName(QStringLiteral("settingsSectionTitle"));
        preference_layout->addWidget(preference_title);
        auto* startup = new WolfSettingsCheckBox(QStringLiteral("Check for updates automatically"));
        startup->setChecked(user_settings_.updates().check_on_startup);
        startup->setAccessibleName(QStringLiteral("Check for updates automatically"));
        auto* startup_hint = new QLabel(QStringLiteral(
            "Checks quietly after Wolfmark opens. No account or telemetry is used."));
        startup_hint->setObjectName(QStringLiteral("settingsHint"));
        startup_hint->setWordWrap(true);
        addOption(preference_layout, startup, startup_hint);
        auto* prereleases = new WolfSettingsCheckBox(QStringLiteral("Include pre-release versions"));
        prereleases->setChecked(user_settings_.updates().include_prereleases);
        prereleases->setAccessibleName(QStringLiteral("Include pre-release versions"));
        auto* prerelease_hint = new QLabel(QStringLiteral(
            "Development builds can be less stable than regular releases."));
        prerelease_hint->setObjectName(QStringLiteral("settingsHint"));
        prerelease_hint->setWordWrap(true);
        addOption(preference_layout, prereleases, prerelease_hint);
        layout->addWidget(preferences);

        auto* version_card = new QFrame;
        version_card->setObjectName(QStringLiteral("settingsCard"));
        version_card->setAttribute(Qt::WA_StyledBackground);
        auto* version_layout = new QVBoxLayout(version_card);
        version_layout->setContentsMargins(18, 15, 18, 15);
        version_layout->setSpacing(8);
        auto* version_title = new QLabel(QStringLiteral("Current version"));
        version_title->setObjectName(QStringLiteral("settingsSectionTitle"));
        version_layout->addWidget(version_title);
        auto* version = new QLabel(QStringLiteral("Wolfmark %1")
                                       .arg(QCoreApplication::applicationVersion()));
        version->setObjectName(QStringLiteral("settingsHint"));
        version_layout->addWidget(version);
        auto* check_result = new QLabel;
        check_result->setObjectName(QStringLiteral("settingsResult"));
        check_result->setWordWrap(true);
        check_result->hide();
        version_layout->addWidget(check_result);
        auto* check_now = new WolfButton(QStringLiteral("Check now"));
        check_now->setAccessibleName(QStringLiteral("Check for updates now"));
        QObject::connect(check_now, &QPushButton::clicked, &dialog,
                         [this, prereleases, check_result, check_now] {
            check_result->setText(QStringLiteral("Checking GitHub Releases…"));
            check_result->show();
            check_now->setEnabled(false);
            checkForUpdates(true, check_result, check_now, prereleases->isChecked());
        });
        auto* check_row = new QHBoxLayout;
        check_row->addWidget(check_now);
        check_row->addStretch();
        version_layout->addLayout(check_row);
        layout->addWidget(version_card);
        layout->addStretch();
        pages->addWidget(updates_page);

        auto* about_page = new QWidget;
        auto* about_layout = new QVBoxLayout(about_page);
        about_layout->setContentsMargins(0, 0, 0, 0);
        about_layout->setSpacing(14);
        addPageEyebrow(about_layout, QStringLiteral("WOLFMARK  /  INFORMATION"));
        auto* about_title = new QLabel(QStringLiteral("About Wolfmark"));
        about_title->setObjectName(QStringLiteral("settingsTitle"));
        about_layout->addWidget(about_title);
        auto* about_intro = new QLabel(QStringLiteral(
            "A native, viewer-first Markdown and text reader for ordinary local files."));
        about_intro->setObjectName(QStringLiteral("settingsHint"));
        about_intro->setWordWrap(true);
        about_layout->addWidget(about_intro);
        auto* about_card = new QFrame;
        about_card->setObjectName(QStringLiteral("settingsCard"));
        about_card->setAttribute(Qt::WA_StyledBackground);
        auto* about_card_layout = new QVBoxLayout(about_card);
        about_card_layout->setContentsMargins(18, 16, 18, 16);
        about_card_layout->setSpacing(8);
        auto* about_version = new QLabel(QStringLiteral("Wolfmark %1")
                                             .arg(QCoreApplication::applicationVersion()));
        about_version->setObjectName(QStringLiteral("settingsSectionTitle"));
        about_card_layout->addWidget(about_version);
        auto* about_license = new QLabel(QStringLiteral(
            "Free and open-source software · GPL-3.0-only\n"
            "Rust + C++20 · Qt 6 Widgets · Comrak\n\n"
            "Build %1 · %2\n"
            "Source %3\n"
            "Commit timestamp %4")
            .arg(QStringLiteral(WOLFMARK_BUILD_ID),
                 QStringLiteral(WOLFMARK_BUILD_CHANNEL),
                 QStringLiteral(WOLFMARK_SOURCE_COMMIT),
                 QStringLiteral(WOLFMARK_SOURCE_TIMESTAMP)));
        about_license->setObjectName(QStringLiteral("settingsHint"));
        about_license->setWordWrap(true);
        about_license->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                               Qt::TextSelectableByKeyboard);
        about_card_layout->addWidget(about_license);
        about_layout->addWidget(about_card);
        about_layout->addStretch();
        pages->addWidget(about_page);

        auto* editor_nav = new QPushButton(QStringLiteral("Reader && Editor"));
        editor_nav->setProperty("nav", true);
        editor_nav->setCheckable(true);
        // Navigation order follows the reading/editing workflow, with About last.
        navigation_layout->insertWidget(navigation_layout->indexOf(about_nav), editor_nav);
        navigation_group->addButton(editor_nav, 2);
        auto* editor_page = new QWidget;
        auto* editor_layout = new QVBoxLayout(editor_page);
        editor_layout->setContentsMargins(0, 0, 0, 0);
        editor_layout->setSpacing(14);
        addPageEyebrow(editor_layout, QStringLiteral("WOLFMARK  /  DOCUMENTS"));
        auto* editor_title = new QLabel(QStringLiteral("Reader & Editor"));
        editor_title->setObjectName(QStringLiteral("settingsTitle"));
        editor_layout->addWidget(editor_title);
        auto* reader_hint = new QLabel(QStringLiteral("Newly opened documents always begin in Reader. Open tabs retain their selected mode. Saving is explicit; autosave is disabled."));
        reader_hint->setWordWrap(true);
        reader_hint->setObjectName(QStringLiteral("settingsHint"));
        editor_layout->addWidget(reader_hint);
        auto* editor_card = new QFrame;
        editor_card->setObjectName(QStringLiteral("settingsCard"));
        editor_card->setAttribute(Qt::WA_StyledBackground);
        auto* editor_card_layout = new QVBoxLayout(editor_card);
        editor_card_layout->setContentsMargins(20, 19, 20, 20);
        editor_card_layout->setSpacing(8);
        auto* editor_card_heading = new QLabel(QStringLiteral("Source editing"));
        editor_card_heading->setObjectName(QStringLiteral("settingsSectionTitle"));
        editor_card_layout->addWidget(editor_card_heading);
        auto* word_wrap = new WolfSettingsCheckBox(QStringLiteral("Wrap long source lines"));
        word_wrap->setChecked(user_settings_.editorWordWrap());
        auto* syntax_hint = new QLabel(QStringLiteral("Live Preview reveals syntax around your cursor. Complex Markdown remains fully editable; source text is never silently rewritten."));
        syntax_hint->setWordWrap(true);
        syntax_hint->setObjectName(QStringLiteral("settingsHint"));
        addOption(editor_card_layout, word_wrap, syntax_hint);
        editor_layout->addWidget(editor_card);
        editor_layout->addStretch();
        pages->addWidget(editor_page);

        QObject::connect(navigation_group, &QButtonGroup::idClicked, pages,
                         [pages](int index) { pages->setCurrentIndex(index); });

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        if (auto* save = buttons->button(QDialogButtonBox::Save)) save->setProperty("primary", true);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        auto* settings_footer = new QWidget;
        settings_footer->setObjectName(QStringLiteral("settingsFooter"));
        settings_footer->setAttribute(Qt::WA_StyledBackground);
        auto* footer_layout = new QHBoxLayout(settings_footer);
        footer_layout->setContentsMargins(12, 12, 0, 0);
        footer_layout->setSpacing(12);
        auto* footer_note = new QLabel(QStringLiteral("Changes are saved only when you select Save"));
        footer_note->setObjectName(QStringLiteral("settingsHint"));
        footer_layout->addWidget(footer_note, 1);
        footer_layout->addWidget(buttons);
        content_layout->addWidget(settings_footer);
        // The modal keyboard focus should start in navigation, not on a caption button.
        QTimer::singleShot(0, &dialog, [updates_nav] {
            updates_nav->setFocus(Qt::OtherFocusReason);
        });
        if (!snapshot_path.isEmpty()) {
            const auto requested_page = qEnvironmentVariable("WOLFMARK_SNAPSHOT_SETTINGS_PAGE");
            if (requested_page == QStringLiteral("editor")) {
                pages->setCurrentIndex(2);
                editor_nav->setChecked(true);
            } else if (requested_page == QStringLiteral("about")) {
                pages->setCurrentIndex(1);
                about_nav->setChecked(true);
            }
            QDir().mkpath(QFileInfo(snapshot_path).absolutePath());
            QTimer::singleShot(120, &dialog, [&dialog, snapshot_path] {
                dialog.grab().save(snapshot_path);
                dialog.reject();
            });
        }
        if (dialog.exec() != QDialog::Accepted) return;

        user_settings_.setCheckOnStartup(startup->isChecked());
        user_settings_.setIncludePrereleases(prereleases->isChecked());
        user_settings_.setEditorWordWrap(word_wrap->isChecked());
        for (const auto& session : documents_)
            session->editor->setLineWrapMode(word_wrap->isChecked() ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        QString error;
        if (!user_settings_.save(&error))
            QMessageBox::warning(this, QStringLiteral("Wolfmark Settings"), error);
    }

    void checkForUpdates(bool manual, QPointer<QLabel> result_label = {},
                         QPointer<QPushButton> trigger = {},
                         std::optional<bool> include_prereleases = std::nullopt) {
        const bool include = include_prereleases.value_or(
            user_settings_.updates().include_prereleases);
        update_manager_->check(include, manual,
                               [this, manual, result_label, trigger](
                                   wolfmark::qt::UpdateCheckResult result) {
            if (trigger != nullptr) trigger->setEnabled(true);
            if (result.status == wolfmark::qt::UpdateCheckResult::Status::Available) {
                available_update_ = result.release;
                update_label_->setText(QStringLiteral("Wolfmark %1 is available")
                                           .arg(result.release.version));
                update_now_->setText(user_settings_.portable()
                                         ? QStringLiteral("Download update")
                                         : QStringLiteral("Update now"));
                update_banner_->show();
                if (result_label != nullptr) {
                    result_label->setText(QStringLiteral("Wolfmark %1 is available.")
                                              .arg(result.release.version));
                    result_label->show();
                }
                return;
            }
            if (result_label != nullptr) {
                result_label->setText(
                    result.status == wolfmark::qt::UpdateCheckResult::Status::Current
                        ? QStringLiteral("Wolfmark is up to date.")
                        : result.message);
                result_label->show();
            } else if (manual && result.status == wolfmark::qt::UpdateCheckResult::Status::Error) {
                QMessageBox::warning(this, QStringLiteral("Wolfmark Update"), result.message);
            }
        });
    }

    void downloadUpdate() {
        if (!available_update_.has_value()) return;
        update_now_->setEnabled(false);
        update_label_->setText(QStringLiteral("Downloading and verifying Wolfmark %1…")
                                   .arg(available_update_->version));
        update_manager_->download(*available_update_, user_settings_.portable(),
                                  [this](wolfmark::qt::UpdateDownloadResult result) {
            update_now_->setEnabled(true);
            if (!result.valid) {
                update_label_->setText(result.message);
                QMessageBox::warning(this, QStringLiteral("Wolfmark Update"), result.message);
                return;
            }
            if (user_settings_.portable()) {
                update_label_->setText(QStringLiteral("Verified portable update downloaded."));
                QMessageBox::information(
                    this, QStringLiteral("Portable update downloaded"),
                    QStringLiteral("The verified ZIP was saved to:\n%1\n\nWolfmark will not overwrite its running portable folder.")
                        .arg(QDir::toNativeSeparators(result.file_path)));
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(result.file_path).absolutePath()));
                return;
            }
            update_label_->setText(QStringLiteral("Verified installer downloaded."));
            const auto choice = QMessageBox::question(
                this, QStringLiteral("Install Wolfmark update"),
                QStringLiteral("The Wolfmark installer was downloaded and verified.\n\nRun it now and close Wolfmark?"));
            if (choice == QMessageBox::Yes) {
                for (const auto& session : documents_)
                    if (!confirmDiscardOrSave(session.get())) return;
                if (QProcess::startDetached(result.file_path, {})) {
                    QCoreApplication::quit();
                } else {
                    QMessageBox::warning(this, QStringLiteral("Wolfmark Update"),
                                         QStringLiteral("Windows could not start the verified installer."));
                }
            }
        });
    }

    void reloadDocument() {
        reloadSession(active_, true);
    }

    void changeZoom(int delta) {
        if (!document_) return;
        if (active_->mode == OpenDocumentSession::Mode::Reader) document_->changeZoom(delta);
        else {
            active_->zoom_percent = std::clamp(active_->zoom_percent + delta, 60, 220);
            active_->editor->setZoomPercent(active_->zoom_percent);
            zoom_label_->setText(QStringLiteral("%1%").arg(active_->zoom_percent));
        }
        zoom_label_->setText(QStringLiteral("%1%").arg(active_->zoom_percent));
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
        tabs_->hide();
        tab_row_->hide();
        find_bar_->hide();
        showFullScreen();
    }

    void leaveFullscreen() {
        const auto restored = api_->window_leave_fullscreen(window_state_);
        fullscreen_ = false;
        title_bar_->show();
        updateSidebarVisibility();
        document_actions_->setVisible(!current_path_.isEmpty());
        diagnostics_bar_->setVisible(diagnostics_);
        tabs_->setVisible(documents_.size() > 1);
        tab_row_->setVisible(documents_.size() > 1);
        if (restored == 1 || pre_fullscreen_maximized_) {
            showMaximized();
        } else {
            showNormal();
        }
#ifdef _WIN32
        QTimer::singleShot(0, this, [this] {
            wolfmark::qt::windows::installNativeFrame(this);
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

    void focusDocument() {
        if (!active_) return;
        if (active_->mode == OpenDocumentSession::Mode::Reader) document_->setFocus(Qt::OtherFocusReason);
        else active_->editor->setFocus(Qt::OtherFocusReason);
    }

    void updateDocumentControls() {
        if (!active_) {
            find_bar_->hide();
            title_search_->setEnabled(false);
            return;
        }
        title_search_->setEnabled(true);
        const int mode = static_cast<int>(active_->mode);
        const auto mode_actions = mode_group_->actions();
        if (mode < mode_actions.size()) mode_actions.at(mode)->setChecked(true);
        for (int index = 0; index < mode_segments_.size(); ++index) {
            QSignalBlocker blocker(mode_segments_.at(index));
            mode_segments_.at(index)->setChecked(index == mode);
        }

        const bool dirty = active_->editor->document()->isModified();
        const bool attention = active_->external_changed || !active_->file_error.isEmpty();
        save_button_->setVisible(dirty || attention);
        save_button_->setEnabled(dirty || active_->external_changed);
        const auto cursor = active_->editor->textCursor();
        auto detail = dirty ? QStringLiteral("Unsaved changes") : QStringLiteral("Saved");
        if (active_->external_changed)
            detail += QStringLiteral(" · File changed externally — Reload or Save As");
        if (!active_->file_error.isEmpty()) detail = active_->file_error;
        if (active_->mode != OpenDocumentSession::Mode::Reader)
            detail += QStringLiteral(" · Ln %1, Col %2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1);
        const bool editing = active_->mode != OpenDocumentSession::Mode::Reader;
        document_status_->setText(!active_->file_error.isEmpty() ? QStringLiteral("Document error") :
                                  active_->external_changed ? QStringLiteral("File changed") :
                                  editing ? QStringLiteral("%1Ln %2:%3")
                                      .arg(dirty ? QStringLiteral("● ") : QString())
                                      .arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1) :
                                  QStringLiteral("Unsaved"));
        document_status_->setVisible(editing || dirty || attention);
        document_status_->setToolTip(detail);
        if (document_status_->property("attention").toBool() != attention) {
            document_status_->setProperty("attention", attention);
            document_status_->style()->unpolish(document_status_);
            document_status_->style()->polish(document_status_);
        }
        setWindowTitle(QStringLiteral("%1%2 — Wolfmark").arg(QFileInfo(active_->canonical_path).fileName(),
                                                                dirty ? QStringLiteral(" *") : QString()));
    }

    void setDocumentMode(int id) {
        if (!active_ || id < 0 || id > 2) return;
        const auto mode = static_cast<OpenDocumentSession::Mode>(id);
        if (mode == active_->mode) return;
        auto* session = active_;
        if (mode == OpenDocumentSession::Mode::Reader) {
            const int line = session->editor->textCursor().blockNumber() + 1;
            if (session->rendered_revision != session->editor->sourceRevision() && !renderSession(session)) {
                QMessageBox::warning(this, QStringLiteral("Preview failed"), session->file_error);
                updateDocumentControls(); return;
            }
            session->page->setCurrentWidget(session->view);
            session->view->changeZoom(session->zoom_percent - session->view->zoomPercent());
            session->view->navigateToSourceLine(line);
            session->reader_entry_line = session->view->sourceLineAtViewport();
            session->has_edit_location = true;
        } else {
            const bool was_reader = session->mode == OpenDocumentSession::Mode::Reader;
            const int line = session->view->sourceLineAtViewport();
            session->editor->setZoomPercent(session->zoom_percent);
            session->editor->setLivePreview(mode == OpenDocumentSession::Mode::LivePreview);
            session->page->setCurrentWidget(session->editor);
            if (was_reader && (!session->has_edit_location ||
                (line != session->reader_entry_line && !session->view->sourceNavigationActive())))
                session->editor->goToLine(line);
        }
        session->mode = mode;
        refreshSidebar();
        updateDocumentControls();
        focusDocument();
        if (find_bar_->isVisible()) highlightFindMatches();
    }

    bool saveSession(OpenDocumentSession* session, bool save_as) {
        if (!session) return false;
        QString path = session->canonical_path;
        QByteArray expected = session->editor->diskFingerprint();
        if (save_as) {
            path = QFileDialog::getSaveFileName(this, QStringLiteral("Save document as"), path,
                QStringLiteral("Markdown (*.md *.markdown);;Text (*.txt);;All files (*)"));
            if (path.isEmpty()) return false;
            path = QFileInfo(path).absoluteFilePath();
            const auto identity = QFileInfo(path).exists() ? QFileInfo(path).canonicalFilePath() : path;
            for (const auto& other : documents_) {
                if (other.get() != session && identity.compare(other->canonical_path,
#ifdef _WIN32
                    Qt::CaseInsensitive
#else
                    Qt::CaseSensitive
#endif
                    ) == 0) {
                    QMessageBox::warning(this, QStringLiteral("Document already open"),
                        QStringLiteral("That file is open in another tab. Choose a different path to preserve both sessions."));
                    return false;
                }
            }
            expected = QFileInfo::exists(path) ? wolfmark::qt::DocumentEditor::fingerprint(path) : QByteArray();
        }
        QString error;
        using Result = wolfmark::qt::DocumentEditor::SaveResult;
        const auto result = session->editor->saveFile(path, expected, &error);
        if (result == Result::Conflict) {
            session->external_changed = true;
            QMessageBox conflict(QMessageBox::Warning, QStringLiteral("File changed externally"),
                QStringLiteral("The disk file has changed, moved, or been deleted. Your edits are retained. Save As writes a separate file; Cancel keeps editing. Reload is available from the document menu."),
                QMessageBox::Cancel, this);
            auto* save_copy = conflict.addButton(QStringLiteral("Save As…"), QMessageBox::ActionRole);
            conflict.exec();
            if (conflict.clickedButton() == save_copy) return saveSession(session, true);
            if (active_ == session) updateDocumentControls();
            return false;
        }
        if (result == Result::Error) {
            session->file_error = error;
            QMessageBox::warning(this, QStringLiteral("Could not save document"), error);
            if (active_ == session) updateDocumentControls();
            return false;
        }
        const bool path_changed = session->canonical_path != QFileInfo(path).canonicalFilePath();
        if (!session->watcher->files().isEmpty()) session->watcher->removePaths(session->watcher->files());
        if (!session->watcher->directories().isEmpty()) session->watcher->removePaths(session->watcher->directories());
        session->canonical_path = QFileInfo(path).canonicalFilePath();
        session->watcher->addPath(session->canonical_path);
        session->watcher->addPath(QFileInfo(session->canonical_path).absolutePath());
        session->editor->setMarkdown(QFileInfo(path).suffix().compare(QStringLiteral("txt"), Qt::CaseInsensitive) != 0);
        if (session->mode != OpenDocumentSession::Mode::Reader)
            session->editor->setLivePreview(session->mode == OpenDocumentSession::Mode::LivePreview);
        session->external_changed = false;
        session->file_error.clear();
        if (path_changed) {
            session->rendered_revision = 0;
            if (session->mode == OpenDocumentSession::Mode::Reader) {
                const int line = session->view->sourceLineAtViewport();
                if (renderSession(session)) session->view->navigateToSourceLine(line);
            }
        }
        if (active_ == session) {
            current_path_ = session->canonical_path;
            if (path_changed) activateDocument(activeDocumentIndex());
            else updateDocumentControls();
        }
        refreshSidebar();
        return true;
    }

    bool confirmDiscardOrSave(OpenDocumentSession* session) {
        if (!session->editor->document()->isModified()) return true;
        const auto choice = QMessageBox::warning(this, QStringLiteral("Unsaved changes"),
            QStringLiteral("Save changes to %1 before closing?").arg(QFileInfo(session->canonical_path).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
        if (choice == QMessageBox::Save) return saveSession(session, false);
        return choice == QMessageBox::Discard;
    }

    void showFind() {
        if (!active_) return;
        find_bar_->show();
        find_input_->setFocus();
        find_input_->selectAll();
        highlightFindMatches();
    }

    QTextDocument* activeTextDocument() const {
        if (!active_) return nullptr;
        return active_->mode == OpenDocumentSession::Mode::Reader ? document_->document() : active_->editor->document();
    }

    void highlightFindMatches() {
        auto* target = activeTextDocument();
        if (!target) return;
        QList<QTextEdit::ExtraSelection> matches;
        QTextCursor cursor(target);
        const auto query = find_input_->text();
        if (!query.isEmpty()) {
            while (matches.size() < 5000) {
                cursor = target->find(query, cursor);
                if (cursor.isNull()) break;
                QTextEdit::ExtraSelection selection;
                selection.cursor = cursor;
                selection.format.setBackground(QColor(colour::selection));
                matches.push_back(selection);
            }
        }
        find_input_->setToolTip(QStringLiteral("%1 highlighted matches%2").arg(matches.size())
            .arg(matches.size() == 5000 ? QStringLiteral(" (highlight limit)") : QString()));
        document_->setExtraSelections(active_->mode == OpenDocumentSession::Mode::Reader ? matches : QList<QTextEdit::ExtraSelection>{});
        active_->editor->setExtraSelections(active_->mode != OpenDocumentSession::Mode::Reader ? matches : QList<QTextEdit::ExtraSelection>{});
    }

    void findNext(bool backwards, bool incremental = false) {
        auto* target = activeTextDocument();
        if (!target || find_input_->text().isEmpty()) return;
        auto cursor = active_->mode == OpenDocumentSession::Mode::Reader ? document_->textCursor() : active_->editor->textCursor();
        if (incremental) cursor.setPosition(cursor.selectionStart());
        const auto flags = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags();
        auto found = target->find(find_input_->text(), cursor, flags);
        if (found.isNull()) {
            cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
            found = target->find(find_input_->text(), cursor, flags);
        }
        if (!found.isNull()) {
            if (active_->mode == OpenDocumentSession::Mode::Reader) { document_->setTextCursor(found); document_->ensureCursorVisible(); }
            else { active_->editor->setTextCursor(found); active_->editor->ensureCursorVisible(); }
        }
        highlightFindMatches();
    }

    bool renderSession(OpenDocumentSession* session, bool from_disk = false) {
        const auto path = session->canonical_path.toUtf8();
        const auto source = session->editor->sourceText().toUtf8();
        const auto buffer = api_->render_source(session->backend,
            reinterpret_cast<const std::uint8_t*>(path.constData()), static_cast<std::size_t>(path.size()),
            reinterpret_cast<const std::uint8_t*>(source.constData()), static_cast<std::size_t>(source.size()), from_disk);
        const auto root = jsonFromBuffer(buffer);
        api_->buffer_free(buffer);
        const auto error = root.value("error").toString();
        if (!error.isEmpty() || root.isEmpty()) {
            session->file_error = error.isEmpty() ? QStringLiteral("The preview returned no document.") : error;
            return false;
        }
        session->view->load(root);
        session->outline = root.value("toc").toArray();
        session->rendered_revision = session->editor->sourceRevision();
        return true;
    }

    static qsizetype nearestSourceMatch(const QString& source, const QString& text, qsizetype expected) {
        if (text.isEmpty()) return -1;
        qsizetype nearest = -1;
        for (qsizetype match = source.indexOf(text); match >= 0; match = source.indexOf(text, match + 1)) {
            if (nearest < 0 || std::abs(match - expected) < std::abs(nearest - expected)) nearest = match;
        }
        return nearest;
    }

    void reloadSession(OpenDocumentSession* session, bool requested = false) {
        if (!session) return;
        QString error;
        const auto actual = wolfmark::qt::DocumentEditor::fingerprint(session->canonical_path, &error);
        if (actual.isEmpty()) {
            session->external_changed = true;
            session->file_error = QStringLiteral("File unavailable: %1. In-memory content is retained; use Save As.").arg(error);
            if (requested) QMessageBox::warning(this, QStringLiteral("Could not reload"), session->file_error);
            if (active_ == session) updateDocumentControls();
            return;
        }
        if (!requested && actual == session->editor->diskFingerprint()) {
            session->external_changed = false;
            session->file_error.clear();
            if (!session->watcher->files().contains(session->canonical_path)) session->watcher->addPath(session->canonical_path);
            if (active_ == session) updateDocumentControls();
            return;
        }
        if (session->editor->document()->isModified()) {
            session->external_changed = actual != session->editor->diskFingerprint();
            if (!requested || QMessageBox::warning(this, QStringLiteral("Reload modified document?"),
                QStringLiteral("Reloading %1 discards its unsaved changes. Keep editing with Cancel, or save a copy with Save As first.")
                    .arg(QFileInfo(session->canonical_path).fileName()),
                QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) {
                if (active_ == session) updateDocumentControls();
                return;
            }
        }
        const auto state = session->view->interactionState();
        const int selection_position = session->editor->textCursor().position();
        const int selection_anchor = session->editor->textCursor().anchor();
        const int old_line = session->mode == OpenDocumentSession::Mode::Reader
                                 ? session->view->sourceLineAtViewport() : session->editor->viewportLine();
        const auto anchor_block = session->editor->document()->findBlockByNumber(old_line - 1);
        const int anchor_position = std::max(0, anchor_block.position());
        const auto anchor_text = anchor_block.text();
        const auto old_source = session->editor->sourceText();
        const int context_start = std::max(0, anchor_position - 120);
        const auto anchor_context = old_source.mid(context_start, 240);
        if (!session->editor->loadFile(session->canonical_path, &error)) {
            session->file_error = error;
            if (requested) QMessageBox::warning(this, QStringLiteral("Could not reload"), error);
            if (active_ == session) updateDocumentControls();
            return;
        }
        session->external_changed = false;
        session->file_error.clear();
        if (renderSession(session, true)) {
            session->view->restoreInteractionState(state);
            int line = old_line;
            const auto source = session->editor->sourceText();
            auto match = nearestSourceMatch(source, anchor_context, context_start);
            if (match >= 0) match += anchor_position - context_start;
            else match = nearestSourceMatch(source, anchor_text, anchor_position);
            if (match >= 0) line = static_cast<int>(source.first(match).count(QLatin1Char('\n'))) + 1;
            if (line != old_line || session->view->sourceLineAtViewport() != line) session->view->navigateToSourceLine(line);
            session->editor->goToLine(line);
            const auto remap = [&](int position) {
                const int start = std::max(0, position - 120);
                const auto context = old_source.mid(start, 240);
                const auto found = nearestSourceMatch(source, context, start);
                return static_cast<int>(std::clamp<qsizetype>(found < 0 ? position : found + position - start,
                                                             0, source.size()));
            };
            auto retained = session->editor->textCursor();
            retained.setPosition(remap(selection_anchor));
            retained.setPosition(remap(selection_position), QTextCursor::KeepAnchor);
            session->editor->setTextCursor(retained);
        }
        if (!session->watcher->files().contains(session->canonical_path))
            session->watcher->addPath(session->canonical_path);
        if (active_ == session) {
            sidebar_->setOutline(session->outline);
            updateStatus();
            updateDocumentControls();
        }
    }

    void activateDocument(int index) {
        if (index < 0 || index >= static_cast<int>(documents_.size())) return;
        auto* selected = documents_[static_cast<std::size_t>(index)].get();
        if (active_ != selected) sidebar_->resetOutlineContext();
        active_ = selected;
        backend_ = active_->backend;
        document_ = active_->view;
        current_path_ = active_->canonical_path;
        const QFileInfo file(current_path_);
        // The compact title uses the reference's real document breadcrumb.
        // Its full disk path remains available in the tooltip.
        title_label_->setFullText(QStringLiteral("Documents   ›   %1").arg(file.fileName()));
        title_label_->setToolTip(current_path_);
        setWindowTitle(QStringLiteral("%1 — Wolfmark").arg(file.fileName()));
        stack_->setCurrentWidget(active_->page);
        focusDocument();
        reload_action_->setEnabled(true);
        document_actions_->setVisible(!fullscreen_);
        zoom_label_->setText(QStringLiteral("%1%").arg(active_->zoom_percent));
        refreshSidebar();
        updateStatus();
        updateDocumentControls();
        if (find_bar_->isVisible()) highlightFindMatches();
    }

    void closeDocument(int index) {
        if (index < 0 || index >= static_cast<int>(documents_.size())) return;
        auto* closing = documents_[static_cast<std::size_t>(index)].get();
        if (!confirmDiscardOrSave(closing)) return;
        const bool was_active = closing == active_;
        stack_->removeWidget(closing->page);
        closing->editor->stateChanged = {};
        delete closing->page;
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
        title_label_->setFullText(QStringLiteral("Documents"));
        title_label_->setToolTip({});
        setWindowTitle(QStringLiteral("Wolfmark"));
        reload_action_->setEnabled(false);
        document_actions_->hide();
        zoom_label_->setText(QStringLiteral("100%"));
        refreshSidebar();
        updateStatus();
        updateDocumentControls();
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
            names.push_back(QFileInfo(session->canonical_path).fileName() +
                            (session->editor->document()->isModified() ? QStringLiteral(" *") : QString()));
        sidebar_->setDocuments(names, activeDocumentIndex());
        sidebar_->setOutline(active_ ? (active_->mode != OpenDocumentSession::Mode::Reader && active_->editing_outline_ready
                                           ? active_->editing_outline : active_->outline) : QJsonArray{});
        if (tabs_) {
            const QSignalBlocker blocker(tabs_);
            while (tabs_->count() > names.size()) tabs_->removeTab(tabs_->count() - 1);
            for (int index = 0; index < names.size(); ++index) {
                if (index >= tabs_->count()) {
                    const int new_index = tabs_->addTab(names[index]);
                    auto* closer = new QToolButton(tabs_);
                    closer->setObjectName(QStringLiteral("documentTabClose"));
                    closer->setText(QStringLiteral("×"));
                    closer->setAutoRaise(true);
                    closer->setCursor(Qt::PointingHandCursor);
                    closer->setFixedSize(18, 18);
                    QObject::connect(closer, &QToolButton::clicked, this, [this, closer] {
                        // Resolve the current index: closing an earlier tab can shift indices.
                        for (int tab = 0; tab < tabs_->count(); ++tab) {
                            if (tabs_->tabButton(tab, QTabBar::RightSide) == closer) {
                                closeDocument(tab);
                                return;
                            }
                        }
                    });
                    tabs_->setTabButton(new_index, QTabBar::RightSide, closer);
                } else tabs_->setTabText(index, names[index]);
                if (auto* closer = tabs_->tabButton(index, QTabBar::RightSide)) {
                    closer->setAccessibleName(QStringLiteral("Close %1").arg(names[index]));
                    closer->setToolTip(QStringLiteral("Close document"));
                }
                tabs_->setTabToolTip(index, documents_[static_cast<std::size_t>(index)]->canonical_path);
            }
            tabs_->setCurrentIndex(activeDocumentIndex());
            // A single document already has a title and a sidebar entry.
            tabs_->setVisible(documents_.size() > 1 && !fullscreen_);
            tab_row_->setVisible(documents_.size() > 1 && !fullscreen_);
        }
        if (previous_tab_) previous_tab_->setEnabled(documents_.size() > 1);
        if (next_tab_) next_tab_->setEnabled(documents_.size() > 1);
    }

    const WolfmarkApiTable* api_ = nullptr;
    wolfmark::qt::UserSettings user_settings_;
    wolfmark::qt::UpdateManager* update_manager_ = nullptr;
    void* backend_ = nullptr;
    void* window_state_ = nullptr;
    wolfmark::qt::DocumentSidebar* sidebar_ = nullptr;
    bool sidebar_requested_ = true;
    bool sidebar_explicit_ = false;
    wolfmark::qt::WolfTitleBar* title_bar_ = nullptr;
    QWidget* document_actions_ = nullptr;
    WolfButton* title_search_ = nullptr;
    QVector<WolfButton*> mode_segments_;
    QStackedWidget* stack_ = nullptr;
    QTabBar* tabs_ = nullptr;
    QWidget* tab_row_ = nullptr;
    WolfButton* previous_tab_ = nullptr;
    WolfButton* next_tab_ = nullptr;
    QActionGroup* mode_group_ = nullptr;
    WolfButton* save_button_ = nullptr;
    QLabel* document_status_ = nullptr;
    QWidget* find_bar_ = nullptr;
    QLineEdit* find_input_ = nullptr;
    DocumentView* document_ = nullptr;
    WolfButton* open_ = nullptr;
    QAction* reload_action_ = nullptr;
    WolfButton* zoom_out_ = nullptr;
    WolfButton* zoom_in_ = nullptr;
    wolfmark::qt::CaptionButton* minimize_ = nullptr;
    wolfmark::qt::CaptionButton* maximize_ = nullptr;
    wolfmark::qt::CaptionButton* close_ = nullptr;
    WolfButton* zoom_label_ = nullptr;
    QLabel* title_symbol_ = nullptr;
    ElidingLabel* title_label_ = nullptr;
    QLabel* diagnostics_bar_ = nullptr;
    QWidget* update_banner_ = nullptr;
    QLabel* update_label_ = nullptr;
    WolfButton* update_now_ = nullptr;
    std::optional<wolfmark::qt::ReleaseInfo> available_update_;
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

void applyWolfmarkStyle(QApplication& application) {
    wolfmark::style::apply(application);
}

} // namespace

extern "C" int wolfmark_qt_run(int argc, const char* const* argv, const WolfmarkApiTable* api) {
    if (api == nullptr || api->version != 4) {
        return 2;
    }
    QCoreApplication::setOrganizationName(QStringLiteral("Wolfmark"));
    QCoreApplication::setApplicationName(QStringLiteral("Wolfmark"));
    QCoreApplication::setApplicationVersion(QStringLiteral(WOLFMARK_PRODUCT_VERSION));

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
    applyWolfmarkStyle(application);
    // Smoke tests must not update the user's application settings.
    const bool motion_smoke = std::find(argument_storage.cbegin(), argument_storage.cend(),
                                        QByteArray("--smoke-motion")) != argument_storage.cend();
    const bool scroll_profile_smoke =
        std::find(argument_storage.cbegin(), argument_storage.cend(),
                  QByteArray("--smoke-scroll-profile")) != argument_storage.cend();
    if (scroll_profile_smoke) qputenv("WOLFMARK_SCROLL_TRACE", "1");
    bool settings_smoke = false;
    bool updates_smoke = false;
    for (const auto& argument : argument_storage) {
        if (argument.startsWith("--smoke-")) {
            if (!motion_smoke && !scroll_profile_smoke)
                qputenv("WOLFMARK_REDUCED_MOTION", "1");
            if (argument != QByteArray("--smoke-portable-settings")) {
                const auto settings_root = QDir::current().absoluteFilePath(
                    QStringLiteral("out/tests/native-settings/%1")
                        .arg(QCoreApplication::applicationPid()));
                qputenv("WOLFMARK_SETTINGS_ROOT",
                        settings_root.toUtf8());
            }
            qputenv("WOLFMARK_UPDATE_STATE_ROOT",
                    QDir::current().absoluteFilePath("out/tests/native-updates").toUtf8());
            qputenv("WOLFMARK_DISABLE_UPDATE_CHECKS", "1");
            QSettings::setDefaultFormat(QSettings::IniFormat);
            const auto settings_root = qEnvironmentVariable("WOLFMARK_SETTINGS_ROOT");
            if (!settings_root.isEmpty()) {
                QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_root);
            }
            settings_smoke = argument == QByteArray("--smoke-settings");
            updates_smoke = argument == QByteArray("--smoke-updates");
            break;
        }
    }
    if (settings_smoke) {
        const auto root = qEnvironmentVariable("WOLFMARK_SETTINGS_ROOT");
        QDir(root).removeRecursively();
        QDir().mkpath(root);
        QSettings legacy;
        legacy.setValue(QStringLiteral("lastOpenDirectory"),
                        QDir(root).filePath(QStringLiteral("legacy-documents")));
        legacy.sync();
    }
    if (updates_smoke) {
        QDir(QDir::current().absoluteFilePath("out/tests/native-updates")).removeRecursively();
    }
    WolfmarkWindow window(api);
    window.show();
    wolfmark::qt::windows::installNativeFrame(&window);
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
        } else if (argument == QStringLiteral("--smoke-scroll-stability")) {
            smoke_mode = QStringLiteral("scroll-stability");
        } else if (argument == QStringLiteral("--smoke-editor-core")) {
            smoke_mode = QStringLiteral("editor-core");
        } else if (argument == QStringLiteral("--smoke-editor-sessions")) {
            smoke_mode = QStringLiteral("editor-sessions");
        } else if (argument == QStringLiteral("--smoke-shell-controls")) {
            smoke_mode = QStringLiteral("shell-controls");
        } else if (argument == QStringLiteral("--smoke-editor-performance")) {
            smoke_mode = QStringLiteral("editor-performance");
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
        } else if (argument == QStringLiteral("--smoke-settings")) {
            smoke_mode = QStringLiteral("settings");
        } else if (argument == QStringLiteral("--smoke-portable-settings")) {
            smoke_mode = QStringLiteral("portable-settings");
        } else if (argument == QStringLiteral("--smoke-settings-ui")) {
            smoke_mode = QStringLiteral("settings-ui");
        } else if (argument == QStringLiteral("--smoke-updates")) {
            smoke_mode = QStringLiteral("updates");
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
                &window, QStringLiteral("Wolfmark could not open some files"),
                QStringLiteral("Wolfmark opens .md, .markdown, and .txt documents.\n\n%1")
                    .arg(startup_errors.join(QLatin1Char('\n'))));
        });
    }
    if (!smoke_mode.isEmpty()) {
        if (document_paths.isEmpty() && smoke_mode != QStringLiteral("snapshot") &&
            smoke_mode != QStringLiteral("icon") &&
            smoke_mode != QStringLiteral("settings") &&
            smoke_mode != QStringLiteral("portable-settings") &&
            smoke_mode != QStringLiteral("settings-ui") &&
            smoke_mode != QStringLiteral("updates") &&
            smoke_mode != QStringLiteral("startup-arguments")) {
            return 3;
        }
        window.runSmoke(smoke_mode);
    }
    return application.exec();
}
