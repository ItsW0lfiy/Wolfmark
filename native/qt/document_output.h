#pragma once

#include "wolfmark_api.h"
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMarginsF>
#include <QPageLayout>
#include <QTextDocument>
#include <QTextCursor>
#include <memory>

class QPagedPaintDevice;
class QPainter;
class QWidget;

namespace wolfmark::qt {

struct PageSettings {
    int paper = QPageSize::A4;
    double width_mm = 210;
    double height_mm = 297;
    bool landscape = false;
    QMarginsF margins_mm{20, 20, 20, 20};
    double content_width_mm = 0;
    QColor background{Qt::white};
    QString body_font = QStringLiteral("Segoe UI");
    QString heading_font = QStringLiteral("Georgia");
    double font_points = 11;
    double heading_scale = 1;
    int line_percent = 135;
    double paragraph_points = 7;
    QString preset = QStringLiteral("Standard A4");
    QString title;
    QString author;
    QString header;
    QString footer;
    QString first_header;
    QString first_footer;
    bool different_first = false;
    bool page_numbers = true;
    bool roman_numbers = false;
    bool toc = false;
    bool cover = false;
    bool heading_breaks = false;
    bool wrap_code = true;
    bool syntax = true;
    bool links = true;
    int image_quality = 90;
    int image_dpi = 150;

    [[nodiscard]] QPageLayout layout() const;
    [[nodiscard]] QString validate() const;
    [[nodiscard]] QJsonObject toJson() const;
    static PageSettings fromJson(const QJsonObject& object);
    static PageSettings forPreset(const QString& name);
};

// One native layout shared by page view, print preview, PDF, and editable exports.
// This is a snapshot of the Reader; ordinary source and retained view state stay untouched.
class DocumentOutput final {
public:
    DocumentOutput(const QTextDocument* source, int source_zoom, PageSettings settings);
    [[nodiscard]] QTextDocument* document() const { return document_.get(); }
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] const PageSettings& settings() const { return settings_; }
    [[nodiscard]] QSizeF paperPoints() const;
    [[nodiscard]] QRectF contentRect() const;
    [[nodiscard]] int blockedLinkSpans() const { return blocked_link_spans_; }
    void paintPage(QPainter& painter, int page) const;
    bool print(QPagedPaintDevice& device, QString* error, int first = 1, int last = 0) const;
    bool write(const QString& format, const QString& path, const WolfmarkApiTable* api, QString* error) const;

private:
    QByteArray docx(const WolfmarkApiTable* api, QString* error) const;
    QByteArray odt(const WolfmarkApiTable* api, QString* error) const;
    QByteArray standaloneHtml() const;
    QImage metrics_device_;
    std::unique_ptr<QTextDocument> document_;
    PageSettings settings_;
    int blocked_link_spans_ = 0;
};

// Returns true after applying settings. Export is an explicit action inside the dialog.
bool showDocumentOutput(QWidget* parent, const QTextDocument* source, int source_zoom,
                        const QString& source_path, PageSettings& settings,
                        const WolfmarkApiTable* api, bool export_tab = false,
                        const QTextCursor& selection = {});

} // namespace wolfmark::qt
