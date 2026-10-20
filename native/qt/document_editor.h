#pragma once

#include "wolfmark_api.h"

#include <QByteArray>
#include <QPlainTextEdit>
#include <QTimer>
#include <QJsonArray>
#include <memory>
#include <functional>

namespace wolfmark::qt {

class MarkdownHighlighter;
struct EditorAnalysis;

// One source document and native undo stack per open file. Source and Live
// Preview change layout decorations only; file bytes never come from rich text.
class DocumentEditor final : public QPlainTextEdit {
public:
    enum class SaveResult { Saved, Conflict, Error };
    explicit DocumentEditor(const WolfmarkApiTable* api, QWidget* parent = nullptr);
    ~DocumentEditor() override;

    bool loadFile(const QString& path, QString* error);
    bool loadBytes(const QByteArray& bytes, QString* error);
    [[nodiscard]] QString sourceText() const;
    [[nodiscard]] QByteArray encodedBytes() const;
    [[nodiscard]] QByteArray diskFingerprint() const { return disk_fingerprint_; }
    static QByteArray fingerprint(const QString& path, QString* error = nullptr);
    SaveResult saveFile(const QString& path, const QByteArray& expected, QString* error);
    void setLivePreview(bool live);
    void setMarkdown(bool markdown);
    void setZoomPercent(int percent);
    void formatSelection(const QString& marker);
    void goToLine(int line);
    [[nodiscard]] int viewportLine() const;
    [[nodiscard]] quint64 sourceRevision() const { return source_revision_; }
    [[nodiscard]] bool analysisReady() const;
    [[nodiscard]] quint64 analysisMicros() const { return analysis_us_; }
    [[nodiscard]] int decoratedBlocks() const { return decorated_blocks_; }
    [[nodiscard]] bool markerMinimizedAt(int position) const;
    std::function<void()> stateChanged;
    std::function<void(int)> zoomRequested;
    std::function<void(const QJsonArray&)> outlineChanged;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateGutter();
    void paintGutter(QPaintEvent* event);
    void requestAnalysis();
    void collectAnalysis();
    void restoreViewportCursor(const QTextCursor& cursor);
    friend class EditorGutter;
    const WolfmarkApiTable* api_;
    MarkdownHighlighter* highlighter_;
    QWidget* gutter_;
    QTimer analysis_delay_;
    QTimer analysis_poll_;
    std::shared_ptr<EditorAnalysis> analysis_;
    QByteArray disk_fingerprint_;
    bool utf8_bom_ = false;
    bool loading_ = false;
    bool markdown_ = true;
    int zoom_percent_ = -1;
    quint64 source_revision_ = 0;
    quint64 decorated_revision_ = 0;
    quint64 analysis_us_ = 0;
    int decorated_blocks_ = 0;
};

} // namespace wolfmark::qt
