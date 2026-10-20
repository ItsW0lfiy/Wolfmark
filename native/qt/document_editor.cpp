#include "document_editor.h"
#include "wolf_style.h"

#include <QCryptographicHash>
#include <QAbstractTextDocumentLayout>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QSaveFile>
#include <QScrollBar>
#include <QStringDecoder>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextLayout>
#include <QThreadPool>
#include <QWheelEvent>
#include <algorithm>
#include <atomic>
#include <mutex>

namespace wolfmark::qt {
namespace {
namespace colour = wolfmark::style::colour;
constexpr int line_ending_property = QTextFormat::UserProperty + 80;
QByteArray hashBytes(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QThreadPool& analysisPool() {
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured);
    return pool;
}

struct Part {
    int start;
    int length;
    QString kind;
    int level;
    bool marker;
    int element_start;
    int element_end;
    bool operator==(const Part& other) const {
        return start == other.start && length == other.length && kind == other.kind &&
               level == other.level && marker == other.marker &&
               (!marker || (element_start == other.element_start && element_end == other.element_end));
    }
};

struct BlockDecorations final : QTextBlockUserData {
    QString text;
    QVector<Part> parts;
};
}

struct EditorAnalysis {
    std::mutex mutex;
    std::atomic_bool cancelled = false;
    bool running = false;
    bool ready = false;
    quint64 revision = 0;
    quint64 elapsed_us = 0;
    QString source;
    QJsonObject result;
};

class MarkdownHighlighter final : public QSyntaxHighlighter {
public:
    explicit MarkdownHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}
    int apply(const QJsonArray& spans) {
        QMap<int, QVector<Part>> next;
        for (const auto& value : spans) {
            const auto span = value.toObject();
            const int start = span.value("start").toInt();
            const int end = span.value("end").toInt();
            const auto append = [&](int a, int b, bool marker) {
                for (auto block = document()->findBlock(a); block.isValid() && block.position() < b;
                     block = block.next()) {
                    const int local = std::max(0, a - block.position());
                    const int length = std::min(b - block.position(), block.length() - 1) - local;
                    if (length > 0) next[block.blockNumber()].push_back(
                        {local, length, span.value("kind").toString(), span.value("level").toInt(),
                         marker, start - block.position(), end - block.position()});
                }
            };
            append(start, end, false);
            for (const auto& marker : span.value("markers").toArray()) {
                const auto range = marker.toArray();
                append(range[0].toInt(), range[1].toInt(), true);
            }
        }
        int decorated = 0;
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
            const int number = block.blockNumber();
            const auto parts = next.value(number);
            const auto* previous = dynamic_cast<BlockDecorations*>(block.userData());
            if (!previous && parts.isEmpty()) continue;
            if (!previous || previous->parts != parts || previous->text != block.text()) {
                auto* cached = new BlockDecorations;
                cached->text = block.text();
                cached->parts = parts;
                block.setUserData(cached);
                rehighlightBlock(block);
                ++decorated;
            }
        }
        return decorated;
    }

    void setSelection(int anchor, int position) {
        const int old_start = selection_start_;
        const int old_end = selection_end_;
        selection_start_ = std::min(anchor, position);
        selection_end_ = std::max(anchor, position);
        if (!live_) return;
        // Only blocks containing markers whose contextual visibility changed
        // need a layout update. No source mutation or undo command is created.
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
            const auto* cached = dynamic_cast<BlockDecorations*>(block.userData());
            if (!cached) continue;
            const int offset = block.position();
            const bool changed = std::any_of(cached->parts.cbegin(), cached->parts.cend(), [&](const Part& part) {
                return part.marker && active(part, old_start - offset, old_end - offset) !=
                                      active(part, selection_start_ - offset, selection_end_ - offset);
            });
            if (changed) rehighlightBlock(block);
        }
    }

    void setLive(bool live) { if (live_ != live) { live_ = live; rehighlight(); } }
    void clear() {
        for (auto block = document()->begin(); block.isValid(); block = block.next()) block.setUserData(nullptr);
        rehighlight();
    }
    void setScale(int percent) { scale_ = percent / 100.0; rehighlight(); }

protected:
    void highlightBlock(const QString& text) override {
        const auto* cached = dynamic_cast<BlockDecorations*>(currentBlock().userData());
        if (!cached || text != cached->text) return;
        for (const auto& part : cached->parts) {
            const int end = std::min(part.start + part.length, static_cast<int>(text.size()));
            for (int start = part.start; start < end;) {
                auto fmt = format(start);
                int stop = start + 1;
                while (stop < end && format(stop) == fmt) ++stop;
                if (part.marker) {
                    if (live_ && !active(part, selection_start_ - currentBlock().position(),
                                        selection_end_ - currentBlock().position())) {
                        fmt.setFontPointSize(0.1);
                        fmt.setForeground(Qt::transparent);
                    } else {
                        fmt.setForeground(QColor(colour::muted));
                    }
                } else if (part.kind == QStringLiteral("heading")) {
                    fmt.setFontWeight(QFont::DemiBold);
                    fmt.setForeground(QColor(colour::bright));
                    if (live_) {
                        static constexpr double sizes[] = {24, 20, 17, 15, 14, 13};
                        fmt.setFontPointSize(sizes[std::clamp(part.level, 1, 6) - 1] * scale_);
                        fmt.setFontFamilies({QStringLiteral("Segoe UI")});
                    }
                } else if (part.kind == QStringLiteral("strong")) {
                    fmt.setFontWeight(QFont::Bold);
                } else if (part.kind == QStringLiteral("emphasis")) {
                    fmt.setFontItalic(true);
                } else if (part.kind == QStringLiteral("strike")) {
                    fmt.setFontStrikeOut(true);
                } else if (part.kind == QStringLiteral("code") || part.kind == QStringLiteral("fence")) {
                    fmt.setBackground(QColor(colour::code));
                    fmt.setForeground(QColor(QStringLiteral("#d6b688")));
                    fmt.setFontFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
                } else if (part.kind == QStringLiteral("link")) {
                    fmt.setForeground(QColor(colour::crimson_hover));
                    fmt.setFontUnderline(true);
                } else if (part.kind == QStringLiteral("quote") || part.kind == QStringLiteral("raw")) {
                    fmt.setForeground(QColor(colour::secondary));
                } else if (part.kind == QStringLiteral("table") || part.kind == QStringLiteral("list")) {
                    if (!live_) fmt.setForeground(QColor(QStringLiteral("#c9ad8f")));
                }
                setFormat(start, stop - start, fmt);
                start = stop;
            }
        }
    }

private:
    static bool active(const Part& part, int start, int end) {
        return start <= part.element_end && end >= part.element_start;
    }
    int selection_start_ = 0;
    int selection_end_ = 0;
    bool live_ = false;
    double scale_ = 1.0;
};

class EditorGutter final : public QWidget {
public:
    explicit EditorGutter(DocumentEditor* editor) : QWidget(editor), editor_(editor) {}
protected:
    void paintEvent(QPaintEvent* event) override { editor_->paintGutter(event); }
private:
    DocumentEditor* editor_;
};

DocumentEditor::DocumentEditor(const WolfmarkApiTable* api, QWidget* parent)
    : QPlainTextEdit(parent), api_(api), highlighter_(new MarkdownHighlighter(document())),
      gutter_(new EditorGutter(this)), analysis_(std::make_shared<EditorAnalysis>()) {
    setFrameShape(QFrame::NoFrame);
    setAccessibleName(QStringLiteral("Wolfmark Markdown source editor"));
    setTabStopDistance(32);
    setZoomPercent(100);
    analysis_delay_.setSingleShot(true);
    analysis_delay_.setInterval(180);
    analysis_poll_.setInterval(25);
    QObject::connect(&analysis_delay_, &QTimer::timeout, this, [this] { requestAnalysis(); });
    QObject::connect(&analysis_poll_, &QTimer::timeout, this, [this] { collectAnalysis(); });
    QObject::connect(document(), &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
        if (loading_ || (removed == 0 && added == 0)) return;
        ++source_revision_;
        if (markdown_ && isVisible()) analysis_delay_.start();
        if (stateChanged) stateChanged();
    });
    QObject::connect(document(), &QTextDocument::modificationChanged, this, [this] {
        if (!loading_ && stateChanged) stateChanged();
    });
    QObject::connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        const auto cursor = textCursor();
        highlighter_->setSelection(cursor.anchor(), cursor.position());
        if (stateChanged) stateChanged();
    });
    QObject::connect(this, &QPlainTextEdit::selectionChanged, this, [this] {
        const auto cursor = textCursor();
        highlighter_->setSelection(cursor.anchor(), cursor.position());
    });
    QObject::connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutter(); });
    QObject::connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect&, int) { gutter_->update(); });
    updateGutter();
}

DocumentEditor::~DocumentEditor() { analysis_->cancelled = true; }

bool DocumentEditor::loadFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return false; }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { if (error) *error = file.errorString(); return false; }
    file.close();
    if (!loadBytes(bytes, error)) return false;
    disk_fingerprint_ = hashBytes(bytes);
    return true;
}

bool DocumentEditor::loadBytes(const QByteArray& bytes, QString* error) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString source = decoder.decode(bytes);
    if (decoder.hasError() || source.contains(QChar::Null)) {
        if (error) *error = QStringLiteral("Unsupported encoding. Wolfmark editing requires valid UTF-8 (optional BOM); the file was not changed.");
        return false;
    }
    if (source.contains(QChar::ParagraphSeparator)) {
        if (error) *error = QStringLiteral("The file contains Unicode paragraph separators that Qt cannot preserve as literal source. The file was not changed.");
        return false;
    }
    const bool bom = bytes.startsWith("\xef\xbb\xbf");
    QString normalized;
    normalized.reserve(source.size());
    QVector<QString> endings;
    for (qsizetype index = 0; index < source.size(); ++index) {
        const auto character = source[index];
        if (character == QLatin1Char('\r')) {
            const bool crlf = index + 1 < source.size() && source[index + 1] == QLatin1Char('\n');
            endings.push_back(crlf ? QStringLiteral("\r\n") : QStringLiteral("\r"));
            if (crlf) ++index;
            normalized += QLatin1Char('\n');
        } else {
            normalized += character;
            if (character == QLatin1Char('\n')) endings.push_back(QStringLiteral("\n"));
        }
    }
    loading_ = true;
    highlighter_->clear();
    setUndoRedoEnabled(false);
    setPlainText(normalized);
    int index = 0;
    const auto fallback = endings.isEmpty() ? QStringLiteral("\n") : endings.front();
    // Block formats participate in Qt's native undo history. New lines inherit
    // nearby conventions; deleting/undoing mixed endings restores their metadata.
    QTextCursor metadata(document());
    metadata.beginEditBlock();
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        auto format = block.blockFormat();
        format.setProperty(line_ending_property, index < endings.size() ? endings[index] : fallback);
        cursor.setBlockFormat(format);
        ++index;
    }
    metadata.endEditBlock();
    utf8_bom_ = bom;
    setUndoRedoEnabled(true);
    document()->setModified(false);
    ++source_revision_;
    loading_ = false;
    if (markdown_ && isVisible()) analysis_delay_.start();
    return true;
}

QString DocumentEditor::sourceText() const {
    auto text = document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

QByteArray DocumentEditor::encodedBytes() const {
    QByteArray bytes = utf8_bom_ ? QByteArray("\xef\xbb\xbf") : QByteArray();
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        bytes += block.text().toUtf8();
        if (block.next().isValid()) {
            const auto ending = block.blockFormat().stringProperty(line_ending_property);
            bytes += ending.isEmpty() ? QByteArray("\n") : ending.toUtf8();
        }
    }
    return bytes;
}

QByteArray DocumentEditor::fingerprint(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return {}; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) { if (error) *error = file.errorString(); return {}; }
    return hash.result();
}

DocumentEditor::SaveResult DocumentEditor::saveFile(const QString& path, const QByteArray& expected, QString* error) {
    if (QFileInfo::exists(path)) {
        const auto actual = fingerprint(path, error);
        if (actual.isEmpty()) return SaveResult::Error;
        if (expected.isEmpty() || actual != expected) return SaveResult::Conflict;
        if (!QFileInfo(path).isWritable()) {
            if (error) *error = QStringLiteral("The target file is read-only. Use Save As or change its permissions.");
            return SaveResult::Error;
        }
    } else if (!expected.isEmpty()) {
        return SaveResult::Conflict;
    }
    const auto bytes = encodedBytes();
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return SaveResult::Error; }
    if (file.write(bytes) != bytes.size()) { if (error) *error = file.errorString(); return SaveResult::Error; }
    // Recheck after staging, before replacement. No event-loop entry occurs
    // between this comparison and commit. External writers do not share a lock.
    if ((!expected.isEmpty() && fingerprint(path) != expected) ||
        (expected.isEmpty() && QFileInfo::exists(path))) return SaveResult::Conflict;
    if (!file.commit()) { if (error) *error = file.errorString(); return SaveResult::Error; }
    disk_fingerprint_ = hashBytes(bytes);
    document()->setModified(false);
    return SaveResult::Saved;
}

void DocumentEditor::setLivePreview(bool live) {
    const auto cursor = textCursor();
    const auto top = cursorForPosition(QPoint(0, 0));
    const bool formatted = live && markdown_;
    highlighter_->setLive(formatted);
    auto font = QFont(formatted ? QStringLiteral("Segoe UI") : QStringLiteral("Cascadia Mono"));
    if (!formatted) {
        font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
        font.setStyleHint(QFont::Monospace);
    }
    font.setPointSizeF(12.0 * zoom_percent_ / 100.0);
    setFont(font);
    document()->setDefaultFont(font);
    setTabStopDistance(QFontMetrics(font).horizontalAdvance(QLatin1Char(' ')) * 4.0);
    updateGutter();
    setTextCursor(cursor);
    restoreViewportCursor(top);
    setAccessibleName(live ? QStringLiteral("Wolfmark Live Preview editor") : QStringLiteral("Wolfmark source editor"));
    requestAnalysis();
}

void DocumentEditor::setMarkdown(bool markdown) {
    if (markdown_ == markdown) return;
    markdown_ = markdown;
    ++source_revision_;
    decorated_revision_ = 0;
    if (!markdown) {
        highlighter_->clear();
        if (outlineChanged) outlineChanged({});
    }
    else if (isVisible()) requestAnalysis();
}

void DocumentEditor::setZoomPercent(int percent) {
    percent = std::clamp(percent, 60, 220);
    if (percent == zoom_percent_) return;
    const auto top = cursorForPosition(QPoint(0, 0));
    zoom_percent_ = percent;
    auto value = document()->defaultFont();
    value.setPointSizeF(12.0 * zoom_percent_ / 100.0);
    setFont(value);
    document()->setDefaultFont(value);
    setTabStopDistance(QFontMetrics(value).horizontalAdvance(QLatin1Char(' ')) * 4.0);
    highlighter_->setScale(zoom_percent_);
    restoreViewportCursor(top);
    updateGutter();
}

void DocumentEditor::formatSelection(const QString& marker) {
    if (isReadOnly() || !hasFocus()) return;
    auto cursor = textCursor();
    const int start = cursor.selectionStart();
    const int end = cursor.selectionEnd();
    const bool reverse = cursor.position() < cursor.anchor();
    const auto source = sourceText();
    const int size = static_cast<int>(marker.size());
    cursor.beginEditBlock();
    bool removing = start >= size && source.mid(start - size, size) == marker && source.mid(end, size) == marker;
    if (removing) {
        cursor.setPosition(end); cursor.setPosition(end + size, QTextCursor::KeepAnchor); cursor.removeSelectedText();
        cursor.setPosition(start - size); cursor.setPosition(start, QTextCursor::KeepAnchor); cursor.removeSelectedText();
        cursor.setPosition(reverse ? end - size : start - size);
        cursor.setPosition(reverse ? start - size : end - size, QTextCursor::KeepAnchor);
    } else if (end - start >= size * 2 && source.mid(start, size) == marker && source.mid(end - size, size) == marker) {
        cursor.setPosition(end - size); cursor.setPosition(end, QTextCursor::KeepAnchor); cursor.removeSelectedText();
        cursor.setPosition(start); cursor.setPosition(start + size, QTextCursor::KeepAnchor); cursor.removeSelectedText();
        cursor.setPosition(start); cursor.setPosition(end - size * 2, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(end); cursor.insertText(marker);
        cursor.setPosition(start); cursor.insertText(marker);
        cursor.setPosition(reverse ? end + size : start + size);
        cursor.setPosition(reverse ? start + size : end + size, QTextCursor::KeepAnchor);
    }
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void DocumentEditor::goToLine(int line) {
    auto block = document()->findBlockByNumber(std::clamp(line - 1, 0, document()->blockCount() - 1));
    setTextCursor(QTextCursor(block));
    centerCursor();
}
int DocumentEditor::viewportLine() const { return firstVisibleBlock().blockNumber() + 1; }
bool DocumentEditor::analysisReady() const { return !markdown_ || decorated_revision_ == source_revision_; }

bool DocumentEditor::markerMinimizedAt(int position) const {
    const auto block = document()->findBlock(position);
    if (!block.isValid() || !block.layout()) return false;
    for (const auto& range : block.layout()->formats())
        if (range.start <= position - block.position() && range.start + range.length > position - block.position())
            return range.format.fontPointSize() == 0.1;
    return false;
}

void DocumentEditor::requestAnalysis() {
    if (!markdown_ || decorated_revision_ == source_revision_) return;
    std::lock_guard lock(analysis_->mutex);
    if (analysis_->running || analysis_->ready) { analysis_poll_.start(); return; }
    analysis_->running = true;
    analysis_->ready = false;
    analysis_->revision = source_revision_;
    analysis_->source = sourceText();
    const auto bytes = analysis_->source.toUtf8();
    const auto state = analysis_;
    const auto* api = api_;
    analysisPool().start([state, bytes, api] {
        if (state->cancelled) return;
        QElapsedTimer elapsed;
        elapsed.start();
        const auto buffer = api->editor_decorations(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                     static_cast<std::size_t>(bytes.size()));
        const auto json = QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(buffer.data),
                                                            static_cast<qsizetype>(buffer.len))).object();
        api->buffer_free(buffer);
        std::lock_guard result_lock(state->mutex);
        state->result = json;
        state->elapsed_us = static_cast<quint64>(elapsed.nsecsElapsed() / 1000);
        state->ready = true;
        state->running = false;
    });
    analysis_poll_.start();
}

void DocumentEditor::collectAnalysis() {
    bool retry = false;
    {
        std::lock_guard lock(analysis_->mutex);
        if (!analysis_->ready) return;
        analysis_->ready = false;
        analysis_poll_.stop();
        if (analysis_->revision == source_revision_) {
            if (!analysis_->result.value("error").toString().isEmpty()) {
                setToolTip(analysis_->result.value("error").toString());
                highlighter_->clear();
            } else {
                const auto top = cursorForPosition(QPoint(0, 0));
                decorated_blocks_ = highlighter_->apply(analysis_->result.value("spans").toArray());
                restoreViewportCursor(top);
                if (outlineChanged) {
                    QJsonArray headings;
                    for (const auto& value : analysis_->result.value("spans").toArray()) {
                        const auto span = value.toObject();
                        if (span.value("kind").toString() != QStringLiteral("heading")) continue;
                        const auto block = document()->findBlock(span.value("start").toInt());
                        auto title = block.text().trimmed();
                        while (title.startsWith(QLatin1Char('#'))) title.remove(0, 1);
                        const int line = block.blockNumber() + 1;
                        headings.push_back(QJsonObject{{"level", span.value("level")}, {"title", title.trimmed()},
                            {"anchor", QStringLiteral("source-line:%1").arg(line)}});
                    }
                    outlineChanged(headings);
                }
            }
            decorated_revision_ = source_revision_;
            analysis_us_ = analysis_->elapsed_us;
        } else retry = isVisible();
        analysis_->source.clear();
        analysis_->result = {};
    }
    if (retry) requestAnalysis();
}

void DocumentEditor::showEvent(QShowEvent* event) { QPlainTextEdit::showEvent(event); requestAnalysis(); }
void DocumentEditor::restoreViewportCursor(const QTextCursor& cursor) {
    const auto block = cursor.block();
    if (!block.isValid()) return;
    // QPlainTextEdit scrolls in visual lines, unlike QTextEdit's pixel scrollbar.
    // Never add cursorRect pixel deltas to this scrollbar's line-based value.
    document()->documentLayout()->blockBoundingRect(block);
    const auto line = block.layout()->lineForTextPosition(cursor.positionInBlock());
    verticalScrollBar()->setValue(block.firstLineNumber() + (line.isValid() ? line.lineNumber() : 0));
}
void DocumentEditor::resizeEvent(QResizeEvent* event) {
    const auto top = cursorForPosition(QPoint(0, 0));
    QPlainTextEdit::resizeEvent(event);
    updateGutter();
    restoreViewportCursor(top);
}
void DocumentEditor::updateGutter() {
    const int width = 20 + QFontMetrics(document()->defaultFont()).horizontalAdvance(QLatin1Char('9')) *
                          static_cast<int>(QString::number(std::max(1, blockCount())).size());
    setViewportMargins(width, 12, 12, 12);
    gutter_->setGeometry(0, 0, width - 6, height());
    gutter_->update();
}
void DocumentEditor::paintGutter(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(gutter_);
    painter.fillRect(gutter_->rect(), QColor(colour::document));
    painter.setPen(QColor(colour::muted));
    painter.setFont(document()->defaultFont());
    auto block = firstVisibleBlock();
    while (block.isValid()) {
        const auto bounds = blockBoundingGeometry(block).translated(contentOffset());
        if (bounds.top() > viewport()->height()) break;
        painter.drawText(QRectF(0, bounds.top() + viewport()->y(), gutter_->width() - 3, bounds.height()),
                         Qt::AlignTop | Qt::AlignRight, QString::number(block.blockNumber() + 1));
        block = block.next();
    }
}

void DocumentEditor::keyPressEvent(QKeyEvent* event) {
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    if (control && (event->key() == Qt::Key_B || event->key() == Qt::Key_I)) {
        formatSelection(event->key() == Qt::Key_B ? QStringLiteral("**") : QStringLiteral("*"));
        event->accept(); return;
    }
    if (control && event->key() == Qt::Key_Z && event->modifiers().testFlag(Qt::ShiftModifier)) {
        redo(); event->accept(); return;
    }
    if (control && (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal ||
                    event->key() == Qt::Key_Minus || event->key() == Qt::Key_0)) {
        if (zoomRequested) zoomRequested(event->key() == Qt::Key_0 ? 100 - zoom_percent_ :
                                        event->key() == Qt::Key_Minus ? -10 : 10);
        event->accept(); return;
    }
    if ((event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) && !control) {
        auto cursor = textCursor();
        const bool unindent = event->key() == Qt::Key_Backtab || event->modifiers().testFlag(Qt::ShiftModifier);
        if (cursor.hasSelection() || unindent) {
            const int start = cursor.selectionStart();
            const int end = cursor.selectionEnd();
            const auto last = document()->findBlock(end > start ? end - 1 : end);
            auto block = document()->findBlock(start);
            cursor.beginEditBlock();
            while (block.isValid()) {
                QTextCursor line(block);
                if (!unindent) line.insertText(QStringLiteral("\t"));
                else {
                    const auto text = block.text();
                    int remove = text.startsWith(QLatin1Char('\t')) ? 1 : 0;
                    while (remove < 4 && remove < text.size() && text[remove] == QLatin1Char(' ')) ++remove;
                    line.setPosition(block.position() + remove, QTextCursor::KeepAnchor);
                    line.removeSelectedText();
                }
                if (block == last) break;
                block = block.next();
            }
            cursor.endEditBlock();
            setTextCursor(cursor);
            event->accept(); return;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
}

void DocumentEditor::wheelEvent(QWheelEvent* event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        if (zoomRequested && event->angleDelta().y() != 0) zoomRequested(event->angleDelta().y() > 0 ? 10 : -10);
        event->accept(); return;
    }
    QPlainTextEdit::wheelEvent(event);
}
void DocumentEditor::insertFromMimeData(const QMimeData* source) {
    // Even when the clipboard offers rich text, only its literal text is edited.
    insertPlainText(source->text());
}

QMimeData* DocumentEditor::createMimeDataFromSelection() const {
    auto* data = new QMimeData;
    const auto cursor = textCursor();
    auto text = document()->toRawText().mid(cursor.selectionStart(), cursor.selectionEnd() - cursor.selectionStart());
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    data->setText(text);
    return data;
}

} // namespace wolfmark::qt
