#include "document_output.h"
#include "document_zoom.h"
#include "document_properties.h"
#include "page_preview.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QBoxLayout>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTemporaryDir>
#include <QTextDocumentWriter>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextTable>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>

namespace wolfmark::qt {
namespace {
constexpr double points_per_mm = 72.0 / 25.4;
QString xml(const QString& text) { return text.toHtmlEscaped(); }
QString number(double value) { return QString::number(value, 'f', 3); }
int twips(double points) { return static_cast<int>(std::lround(points * 20)); }
bool safeLink(const QString& target) {
    QString compact;
    for (const auto character : target) if (character.unicode() > 0x20) compact += character;
    const auto scheme = QUrl(compact).scheme().toLower();
    return scheme.isEmpty() || scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
           scheme == QStringLiteral("mailto") || scheme == QStringLiteral("file");
}
QString pageNumber(int page, bool roman) {
    if (!roman) return QString::number(page);
    QString result;
    for (const auto& item : {std::pair{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"},
                            {100, "c"}, {90, "xc"}, {50, "l"}, {40, "xl"},
                            {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}}) {
        while (page >= item.first) { result += QLatin1String(item.second); page -= item.first; }
    }
    return result;
}
QString expanded(QString text, const PageSettings& settings, int page, int total) {
    return text.replace(QStringLiteral("{title}"), settings.title)
        .replace(QStringLiteral("{author}"), settings.author)
        .replace(QStringLiteral("{date}"), QDate::currentDate().toString(Qt::ISODate))
        .replace(QStringLiteral("{page}"), pageNumber(page, settings.roman_numbers))
        .replace(QStringLiteral("{pages}"), pageNumber(total, settings.roman_numbers));
}
QByteArray png(const QImage& image, int quality = 90) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG", 100 - quality);
    return bytes;
}
QImage resourceImage(QTextDocument* document, const QString& name) {
    const auto resource = document->resource(QTextDocument::ImageResource, QUrl(name));
    if (resource.canConvert<QImage>()) return qvariant_cast<QImage>(resource);
    if (resource.canConvert<QPixmap>()) return qvariant_cast<QPixmap>(resource).toImage();
    return {};
}
void addPart(QJsonArray& parts, const QString& name, const QByteArray& data) {
    parts.append(QJsonObject{{"name", name}, {"data", QString::fromLatin1(data.toBase64())}});
}
void addXml(QJsonArray& parts, const QString& name, const QString& content) {
    addPart(parts, name, (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>") + content).toUtf8());
}
QByteArray finishPackage(const QJsonArray& parts, const WolfmarkApiTable* api, QString* error) {
    const auto input = QJsonDocument(parts).toJson(QJsonDocument::Compact);
    const auto result = api->package_document(reinterpret_cast<const std::uint8_t*>(input.constData()), static_cast<std::size_t>(input.size()));
    const auto bytes = QByteArray(reinterpret_cast<const char*>(result.data), static_cast<qsizetype>(result.len));
    api->buffer_free(result);
    if (!bytes.startsWith("PK\x03\x04")) {
        if (error) *error = QJsonDocument::fromJson(bytes).object().value("error").toString(QStringLiteral("Could not create the document package."));
        return {};
    }
    return bytes;
}
}

QPageLayout PageSettings::layout() const {
    const auto size = paper == QPageSize::Custom
        ? QPageSize(QSizeF(width_mm, height_mm), QPageSize::Millimeter, QStringLiteral("Custom"), QPageSize::ExactMatch)
        : QPageSize(static_cast<QPageSize::PageSizeId>(paper));
    return QPageLayout(size, landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                       margins_mm, QPageLayout::Millimeter);
}

QString PageSettings::validate() const {
    for (double value : {width_mm, height_mm, margins_mm.left(), margins_mm.top(), margins_mm.right(),
                         margins_mm.bottom(), content_width_mm, font_points, heading_scale, paragraph_points})
        if (!std::isfinite(value)) return QStringLiteral("Layout values must be finite numbers.");
    if (paper != QPageSize::A4 && paper != QPageSize::A3 && paper != QPageSize::A5 &&
        paper != QPageSize::B5 && paper != QPageSize::Letter && paper != QPageSize::Legal && paper != QPageSize::Custom)
        return QStringLiteral("Unsupported paper size.");
    if (width_mm < 40 || height_mm < 40 || width_mm > 2000 || height_mm > 2000)
        return QStringLiteral("Custom paper dimensions must be between 40 and 2000 mm.");
    if (margins_mm.left() < 0 || margins_mm.right() < 0 || margins_mm.top() < 0 || margins_mm.bottom() < 0)
        return QStringLiteral("Margins cannot be negative.");
    auto size = layout().fullRect(QPageLayout::Millimeter).size();
    const double width = size.width() - margins_mm.left() - margins_mm.right();
    const double height = size.height() - margins_mm.top() - margins_mm.bottom();
    if (width < 20 || height < 20) return QStringLiteral("Margins must leave at least 20 × 20 mm of content.");
    if (content_width_mm < 0 || (content_width_mm > 0 && (content_width_mm < 20 || content_width_mm > width)))
        return QStringLiteral("Content width must fit inside the page margins (0 uses all available width).");
    if (font_points < 6 || font_points > 40 || heading_scale < 0.5 || heading_scale > 2 ||
        line_percent < 100 || line_percent > 250 || paragraph_points < 0 || paragraph_points > 72 ||
        image_dpi < 72 || image_dpi > 600 || image_quality < 1 || image_quality > 100 || !background.isValid())
        return QStringLiteral("Typography, image quality, or page colour is out of range.");
    return {};
}

QJsonObject PageSettings::toJson() const {
    return {{"paper", paper}, {"width", width_mm}, {"height", height_mm}, {"landscape", landscape},
        {"left", margins_mm.left()}, {"top", margins_mm.top()}, {"right", margins_mm.right()}, {"bottom", margins_mm.bottom()},
        {"contentWidth", content_width_mm}, {"background", background.name()}, {"bodyFont", body_font},
        {"headingFont", heading_font}, {"fontPoints", font_points}, {"headingScale", heading_scale},
        {"linePercent", line_percent}, {"paragraphPoints", paragraph_points}, {"preset", preset},
        {"title", title}, {"author", author}, {"header", header}, {"footer", footer},
        {"firstHeader", first_header}, {"firstFooter", first_footer}, {"differentFirst", different_first},
        {"pageNumbers", page_numbers}, {"romanNumbers", roman_numbers}, {"toc", toc}, {"cover", cover},
        {"headingBreaks", heading_breaks}, {"wrapCode", wrap_code}, {"syntax", syntax}, {"links", links},
        {"imageQuality", image_quality}, {"imageDpi", image_dpi}};
}

PageSettings PageSettings::fromJson(const QJsonObject& object) {
    PageSettings result;
    const auto defaults = result.toJson();
    auto values = defaults;
    for (auto it = object.begin(); it != object.end(); ++it)
        if (defaults.contains(it.key()) && defaults.value(it.key()).type() == it.value().type()) values.insert(it.key(), it.value());
    result.paper = values.value("paper").toInt();
    result.width_mm = values.value("width").toDouble(); result.height_mm = values.value("height").toDouble();
    result.landscape = values.value("landscape").toBool();
    result.margins_mm = {values.value("left").toDouble(), values.value("top").toDouble(), values.value("right").toDouble(), values.value("bottom").toDouble()};
    result.content_width_mm = values.value("contentWidth").toDouble();
    result.background = QColor(values.value("background").toString());
    result.body_font = values.value("bodyFont").toString(); result.heading_font = values.value("headingFont").toString();
    result.font_points = values.value("fontPoints").toDouble(); result.heading_scale = values.value("headingScale").toDouble();
    result.line_percent = values.value("linePercent").toInt(); result.paragraph_points = values.value("paragraphPoints").toDouble();
    result.preset = values.value("preset").toString(); result.title = values.value("title").toString();
    result.author = values.value("author").toString(); result.header = values.value("header").toString(); result.footer = values.value("footer").toString();
    result.first_header = values.value("firstHeader").toString(); result.first_footer = values.value("firstFooter").toString();
    result.different_first = values.value("differentFirst").toBool(); result.page_numbers = values.value("pageNumbers").toBool();
    result.roman_numbers = values.value("romanNumbers").toBool(); result.toc = values.value("toc").toBool(); result.cover = values.value("cover").toBool();
    result.heading_breaks = values.value("headingBreaks").toBool(); result.wrap_code = values.value("wrapCode").toBool(); result.syntax = values.value("syntax").toBool();
    result.links = values.value("links").toBool();
    result.image_quality = values.value("imageQuality").toInt(); result.image_dpi = values.value("imageDpi").toInt();
    return result.validate().isEmpty() ? result : PageSettings{};
}

PageSettings PageSettings::forPreset(const QString& name) {
    PageSettings result;
    result.preset = name;
    if (name == QStringLiteral("School assignment")) {
        result.margins_mm = {25, 25, 25, 25}; result.font_points = 12; result.line_percent = 150;
        result.body_font = QStringLiteral("Times New Roman"); result.heading_font = result.body_font;
    } else if (name == QStringLiteral("Technical report")) {
        result.header = QStringLiteral("{title}"); result.footer = QStringLiteral("{author}"); result.toc = true;
    } else if (name == QStringLiteral("Compact notes")) {
        result.margins_mm = {12, 12, 12, 12}; result.font_points = 9; result.line_percent = 115; result.paragraph_points = 4;
    }
    return result;
}

DocumentOutput::DocumentOutput(const QTextDocument* source, int source_zoom, PageSettings settings)
    : metrics_device_(1, 1, QImage::Format_RGB32), document_(source->clone()), settings_(std::move(settings)) {
    metrics_device_.setDotsPerMeterX(2835); metrics_device_.setDotsPerMeterY(2835);
    document_->documentLayout()->setPaintDevice(&metrics_device_);
    document_->setUndoRedoEnabled(false);
    document_->setLayoutEnabled(false);
    DocumentZoom normalize;
    normalize.capture(document_.get());
    normalize.applyRatio(100.0 / std::max(1, source_zoom));
    document_->setDefaultFont(QFont(settings_.body_font, static_cast<int>(settings_.font_points)));
    document_->setDocumentMargin(0);
    auto root = document_->rootFrame()->frameFormat();
    root.setMargin(0); root.setPadding(0); root.setBorder(0); root.clearBackground();
    document_->rootFrame()->setFrameFormat(root);
    const auto body = contentRect().size();
    document_->setPageSize(body);
    auto option = document_->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document_->setDefaultTextOption(option);
    std::function<void(QTextFrame*)> restyle_frame = [&](QTextFrame* frame) {
        for (auto* child : frame->childFrames()) {
            auto format = child->frameFormat();
            format.setBackground(QColor(QStringLiteral("#f3f3f3")));
            format.setBorderBrush(QColor(QStringLiteral("#b0b0b0")));
            format.setLeftMargin(0); format.setRightMargin(0);
            child->setFrameFormat(format);
            if (auto* table = qobject_cast<QTextTable*>(child)) {
                auto tf = table->format();
                tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
                table->setFormat(tf);
                for (int row = 0; row < table->rows(); ++row) for (int column = 0; column < table->columns(); ++column) {
                    auto cell = table->cellAt(row, column);
                    auto cf = cell.format().toTableCellFormat();
                    cf.setBackground(QColor(row == 0 ? "#e4e4e4" : "#ffffff"));
                    cf.setBorderBrush(QColor(QStringLiteral("#b0b0b0")));
                    cf.setPadding(4);
                    cell.setFormat(cf);
                }
            }
            restyle_frame(child);
        }
    };
    restyle_frame(document_->rootFrame());
    QVector<QPair<int, int>> remove;
    for (auto block = document_->begin(); block.isValid(); block = block.next()) {
        QTextCursor cursor(block);
        auto bf = block.blockFormat();
        const int heading = bf.headingLevel();
        if (bf.lineHeightType() != QTextBlockFormat::FixedHeight) {
            bf.setLineHeight(settings_.line_percent, QTextBlockFormat::ProportionalHeight);
            bf.setBottomMargin(settings_.paragraph_points);
        }
        bf.clearBackground();
        if (heading > 0) {
            bf.setNonBreakableLines(true);
            if (settings_.heading_breaks && heading == 1 && block.position() > 0)
                bf.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        } else if (settings_.wrap_code) bf.setNonBreakableLines(false);
        cursor.setBlockFormat(bf);
        struct Span { int position; int length; QTextCharFormat format; QString text; };
        QVector<Span> spans;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            spans.append({fragment.position(), fragment.length(), fragment.charFormat(), fragment.text()});
        }
        // Formatting may merge adjacent fragments, invalidating live iterators.
        for (const auto& span : spans) {
            auto format = span.format;
            cursor.setPosition(span.position); cursor.setPosition(span.position + span.length, QTextCursor::KeepAnchor);
            if (format.anchorHref().startsWith(QStringLiteral("wolfmark-copy:"))) {
                const int tab = span.position > 0 && document_->characterAt(span.position - 1) == QLatin1Char('\t') ? 1 : 0;
                remove.append({span.position - tab, span.length + tab}); continue;
            }
            if (format.isImageFormat()) {
                auto image = format.toImageFormat();
                const auto natural = resourceImage(document_.get(), image.name());
                const QSizeF original = natural.isNull() ? QSizeF(image.width(), image.height()) : QSizeF(natural.size());
                if (original.width() > 0 && original.height() > 0) {
                    const double ratio = std::min({1.0, body.width() / original.width(), (body.height() - 10) / original.height()});
                    image.setWidth(original.width() * ratio); image.setHeight(original.height() * ratio);
                    const QSize pixels(static_cast<int>(std::ceil(image.width() * settings_.image_dpi / 72)),
                                       static_cast<int>(std::ceil(image.height() * settings_.image_dpi / 72)));
                    if (!natural.isNull() && (natural.width() > pixels.width() || natural.height() > pixels.height()))
                        document_->addResource(QTextDocument::ImageResource, QUrl(image.name()),
                            natural.scaled(pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    cursor.setCharFormat(image);
                }
                continue;
            }
            if (span.text == QString(QChar::ObjectReplacementCharacter)) {
                cursor.insertText(QString(QChar(0x200B)), format); continue;
            }
            const auto families = format.fontFamilies().toStringList();
            const bool code = families.contains(QStringLiteral("Consolas")) || families.contains(QStringLiteral("Cascadia Mono"));
            double points = format.fontPointSize();
            points = points > 0 ? points * settings_.font_points / 12.75 : settings_.font_points;
            if (heading > 0) {
                static constexpr double sizes[] = {2.15, 1.7, 1.4, 1.2, 1.1, 1.0};
                points = settings_.font_points * sizes[std::clamp(heading, 1, 6) - 1] * settings_.heading_scale;
            }
            format.setFontPointSize(points);
            const bool unsafe_link = !format.anchorHref().isEmpty() && !safeLink(format.anchorHref());
            if (unsafe_link) ++blocked_link_spans_;
            if ((!settings_.links || unsafe_link) && !format.anchorHref().isEmpty()) {
                format.setAnchor(false); format.setAnchorHref({}); format.setFontUnderline(false);
            }
            if (!code) format.setFontFamilies({heading ? settings_.heading_font : settings_.body_font});
            format.setForeground(code && settings_.syntax && format.foreground().color().saturation() > 30
                ? format.foreground().color().darker(180) : QColor(QStringLiteral("#202020")));
            if (format.background().style() != Qt::NoBrush) format.setBackground(QColor(QStringLiteral("#eeeeee")));
            cursor.setCharFormat(format);
        }
    }
    for (auto it = remove.crbegin(); it != remove.crend(); ++it) {
        QTextCursor cursor(document_.get()); cursor.setPosition(it->first); cursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    if (settings_.toc) {
        QVector<QPair<int, QString>> headings;
        for (auto block = document_->begin(); block.isValid(); block = block.next())
            if (block.blockFormat().headingLevel() > 0) headings.append({block.blockFormat().headingLevel(), block.text()});
        QTextCursor cursor(document_.get());
        auto original_block = cursor.blockFormat();
        QTextBlockFormat bf; bf.setBottomMargin(settings_.paragraph_points);
        cursor.setBlockFormat(bf);
        QTextCharFormat cf; cf.setFontFamilies({settings_.heading_font}); cf.setFontPointSize(settings_.font_points * 1.7); cf.setFontWeight(QFont::Bold);
        cursor.insertText(QStringLiteral("Contents"), cf); cursor.insertBlock();
        cf.setFontFamilies({settings_.body_font}); cf.setFontPointSize(settings_.font_points); cf.setFontWeight(QFont::Normal);
        for (const auto& heading : headings) {
            bf.setLeftMargin((heading.first - 1) * 12); cursor.setBlockFormat(bf);
            cursor.insertText(heading.second, cf); cursor.insertBlock();
        }
        original_block.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore); cursor.setBlockFormat(original_block);
    }
    if (settings_.cover) {
        QTextCursor cursor(document_.get());
        auto original_block = cursor.blockFormat();
        QTextBlockFormat bf; bf.setAlignment(Qt::AlignHCenter); bf.setTopMargin(body.height() * .25); cursor.setBlockFormat(bf);
        QTextCharFormat cf; cf.setFontFamilies({settings_.heading_font}); cf.setFontPointSize(settings_.font_points * 2.4); cf.setFontWeight(QFont::Bold);
        cursor.insertText(settings_.title, cf); cursor.insertBlock();
        bf.setTopMargin(18); cursor.setBlockFormat(bf); cf.setFontWeight(QFont::Normal); cf.setFontPointSize(settings_.font_points);
        cursor.insertText(settings_.author + QLatin1Char('\n') + QDate::currentDate().toString(Qt::ISODate), cf); cursor.insertBlock();
        original_block.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore); cursor.setBlockFormat(original_block);
    }
    document_->setMetaInformation(QTextDocument::DocumentTitle, settings_.title);
    document_->setLayoutEnabled(true);
    // QTextDocument has no keep-with-next block property. Retain headings with
    // the next content block using native page breaks after measuring layout.
    for (int pass = 0; pass < 8; ++pass) {
        QVector<int> breaks;
        for (auto block = document_->begin(); block.isValid(); block = block.next()) {
            if (block.blockFormat().headingLevel() == 0 || block.blockFormat().pageBreakPolicy().testFlag(QTextFormat::PageBreak_AlwaysBefore)) continue;
            auto following = block.next();
            while (following.isValid() && following.text().trimmed().isEmpty()) following = following.next();
            if (!following.isValid()) continue;
            const auto heading_rect = document_->documentLayout()->blockBoundingRect(block);
            const auto next_rect = document_->documentLayout()->blockBoundingRect(following);
            if (static_cast<int>(heading_rect.top() / body.height()) < static_cast<int>(next_rect.top() / body.height())) breaks.append(block.position());
        }
        if (breaks.isEmpty()) break;
        document_->setLayoutEnabled(false);
        for (int position : breaks) {
            QTextCursor cursor(document_->findBlock(position)); auto format = cursor.blockFormat();
            format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore); cursor.setBlockFormat(format);
        }
        document_->setLayoutEnabled(true);
    }
}

QSizeF DocumentOutput::paperPoints() const { return settings_.layout().fullRect(QPageLayout::Point).size(); }
QRectF DocumentOutput::contentRect() const {
    const auto paper = paperPoints();
    const auto margins = settings_.margins_mm * points_per_mm;
    QRectF rect(margins.left(), margins.top(), paper.width() - margins.left() - margins.right(), paper.height() - margins.top() - margins.bottom());
    if (settings_.content_width_mm > 0) {
        const double width = settings_.content_width_mm * points_per_mm;
        rect.adjust((rect.width() - width) / 2, 0, -(rect.width() - width) / 2, 0);
    }
    return rect;
}
int DocumentOutput::pageCount() const { return std::max(1, document_->pageCount()); }
void DocumentOutput::paintPage(QPainter& painter, int page) const {
    const auto paper = paperPoints();
    const auto content = contentRect();
    painter.fillRect(QRectF(QPointF(), paper), settings_.background);
    painter.save();
    painter.setClipRect(content);
    painter.translate(content.left(), content.top() - page * content.height());
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, QColor(QStringLiteral("#202020")));
    context.clip = QRectF(0, page * content.height(), content.width(), content.height());
    document_->documentLayout()->draw(&painter, context);
    painter.restore();
    painter.save();
    painter.setPen(QColor(QStringLiteral("#404040")));
    QFont font(settings_.body_font); font.setPixelSize(static_cast<int>(std::lround(std::max(7.0, settings_.font_points * .8)))); painter.setFont(font);
    const QString header = settings_.different_first && page == 0 ? settings_.first_header : settings_.header;
    const QString footer = settings_.different_first && page == 0 ? settings_.first_footer : settings_.footer;
    painter.drawText(QRectF(content.left(), 0, content.width(), content.top()), Qt::AlignVCenter | Qt::AlignLeft,
                     expanded(header, settings_, page + 1, pageCount()));
    QRectF foot(content.left(), content.bottom(), content.width(), paper.height() - content.bottom());
    painter.drawText(foot, Qt::AlignVCenter | Qt::AlignLeft, expanded(footer, settings_, page + 1, pageCount()));
    if (settings_.page_numbers)
        painter.drawText(foot, Qt::AlignVCenter | Qt::AlignRight,
            QStringLiteral("%1 / %2").arg(pageNumber(page + 1, settings_.roman_numbers), pageNumber(pageCount(), settings_.roman_numbers)));
    painter.restore();
}

bool DocumentOutput::print(QPagedPaintDevice& device, QString* error, int first, int last) const {
    QPainter painter;
    if (!painter.begin(&device)) { if (error) *error = QStringLiteral("The print/PDF device could not be opened."); return false; }
    const double scale = device.logicalDpiX() / 72.0;
    first = std::clamp(first, 1, pageCount()); last = last <= 0 ? pageCount() : std::clamp(last, first, pageCount());
    for (int page = first - 1; page < last; ++page) {
        if (page != first - 1 && !device.newPage()) {
            if (error) *error = QStringLiteral("The print device could not create another page.");
            painter.end();
            return false;
        }
        painter.save(); painter.scale(scale, scale); paintPage(painter, page); painter.restore();
    }
    if (!painter.end()) { if (error) *error = QStringLiteral("The print device could not finish the document."); return false; }
    return true;
}

QByteArray DocumentOutput::standaloneHtml() const {
    std::unique_ptr<QTextDocument> copy(document_->clone());
    for (auto block = copy->begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        if (!fragment.charFormat().isImageFormat()) continue;
        auto image = fragment.charFormat().toImageFormat();
        const auto bytes = png(resourceImage(copy.get(), image.name()), settings_.image_quality);
        if (bytes.isEmpty()) continue;
        image.setName(QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64()));
        QTextCursor cursor(copy.get()); cursor.setPosition(fragment.position()); cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
        cursor.setCharFormat(image);
    }
    auto html = copy->toHtml();
    const auto margins = settings_.margins_mm;
    const auto mm = settings_.layout().fullRect(QPageLayout::Millimeter).size();
    html.replace(QStringLiteral("</head>"), QStringLiteral("<style>@page{size:%1mm %2mm;margin:%3mm %4mm %5mm %6mm}body{background:%7}pre{white-space:pre-wrap}</style></head>")
        .arg(number(mm.width()), number(mm.height()), number(margins.top()), number(margins.right()), number(margins.bottom()), number(margins.left()), settings_.background.name()));
    return html.toUtf8();
}

bool DocumentOutput::write(const QString& format, const QString& path, const WolfmarkApiTable* api, QString* error) const {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) { if (error) *error = file.errorString(); return false; }
    bool ok = false;
    if (format == QStringLiteral("pdf")) {
        {
            QPdfWriter writer(&file); writer.setResolution(72); writer.setTitle(settings_.title); writer.setCreator(QStringLiteral("Wolfmark"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            writer.setAuthor(settings_.author);
#endif
            auto layout = settings_.layout(); layout.setMode(QPageLayout::FullPageMode); writer.setPageLayout(layout);
            ok = print(writer, error);
        }
    } else {
        QByteArray bytes;
        if (format == QStringLiteral("docx")) bytes = docx(api, error);
        else if (format == QStringLiteral("odt")) bytes = odt(api, error);
        else if (format == QStringLiteral("html")) bytes = standaloneHtml();
        else if (format == QStringLiteral("txt")) {
            QString text;
            for (auto block = document_->begin(); block.isValid(); block = block.next()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    const auto fragment = it.fragment();
                    if (fragment.charFormat().isImageFormat())
                        text += QStringLiteral("[%1]").arg(fragment.charFormat().toolTip());
                    else text += fragment.text();
                }
                text += QLatin1Char('\n');
            }
            text.replace(QChar::LineSeparator, QLatin1Char('\n'));
            text.remove(QChar(0x200B)); text.remove(QChar::ObjectReplacementCharacter);
            bytes = text.toUtf8();
        }
        else { if (error) *error = QStringLiteral("Unsupported export format."); return false; }
        ok = !bytes.isEmpty() && file.write(bytes) == bytes.size();
    }
    if (!ok || !file.commit()) {
        if (error && error->isEmpty()) *error = file.errorString();
        return false;
    }
    return true;
}

bool showDocumentOutput(QWidget* parent, const QTextDocument* source, int source_zoom,
                        const QString& source_path, PageSettings& settings,
                        const WolfmarkApiTable* api, bool export_tab, const QTextCursor& selection) {
    // A watcher may replace the retained Reader document during the modal loop.
    // Keep this preview's source alive independently of that session's renderer.
    std::unique_ptr<QTextDocument> source_snapshot(source->clone());
    source = source_snapshot.get();
    std::unique_ptr<QTextDocument> selected;
    if (selection.hasSelection()) {
        selected = std::make_unique<QTextDocument>();
        QTextCursor cursor(selected.get()); cursor.insertFragment(QTextDocumentFragment(selection));
        for (auto block = selected->begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it) {
            if (!it.fragment().charFormat().isImageFormat()) continue;
            const auto image = it.fragment().charFormat().toImageFormat();
            selected->addResource(QTextDocument::ImageResource, QUrl(image.name()), resourceImage(const_cast<QTextDocument*>(source), image.name()));
        }
    }
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Wolfmark — Document Page View & Export"));
    dialog.resize(1200, 820); dialog.setMinimumSize(860, 560);
    auto* shell = new QVBoxLayout(&dialog);
    auto* toolbar = new QHBoxLayout;
    auto* view_mode = new QComboBox;
    view_mode->addItems({QStringLiteral("Continuous pages"), QStringLiteral("Single page")});
    toolbar->addWidget(view_mode);
    auto* zoom = new QComboBox;
    zoom->addItems({QStringLiteral("Fit width"), QStringLiteral("Fit page"), QStringLiteral("80%"), QStringLiteral("100%"), QStringLiteral("125%"), QStringLiteral("150%")});
    toolbar->addWidget(zoom);
    auto* previous = new QPushButton(QStringLiteral("Previous"));
    auto* next = new QPushButton(QStringLiteral("Next"));
    auto* page = new QSpinBox; page->setMinimum(1); page->setAccessibleName(QStringLiteral("Current page"));
    auto* total = new QLabel;
    toolbar->addWidget(previous); toolbar->addWidget(page); toolbar->addWidget(total); toolbar->addWidget(next);
    toolbar->addStretch();
    auto* print_button = new QPushButton(QStringLiteral("Print…")); toolbar->addWidget(print_button);
    shell->addLayout(toolbar);
    auto* columns = new QHBoxLayout;
    auto* tabs = new QTabWidget; tabs->setFixedWidth(350);
    auto* page_form = new QFormLayout;
    auto* style_form = new QFormLayout;
    auto* details_form = new QFormLayout;
    auto* export_form = new QFormLayout;
    for (const auto& item : {std::pair{page_form, "Page"}, {style_form, "Style"}, {details_form, "Details"}, {export_form, "Export"}}) {
        auto* content = new QWidget; content->setLayout(item.first);
        item.first->setContentsMargins(12, 16, 12, 16); item.first->setSpacing(12); item.first->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setWidget(content); scroll->setFrameShape(QFrame::NoFrame);
        tabs->addTab(scroll, QLatin1String(item.second));
    }
    columns->addWidget(tabs);
    auto* preview = new PagePreview;
    preview->setAccessibleName(QStringLiteral("Paginated document preview"));
    columns->addWidget(preview, 1); shell->addLayout(columns, 1);
    auto* diagnostics = new QLabel(QStringLiteral("Local export. The Markdown source is never changed."));
    diagnostics->setWordWrap(true); shell->addWidget(diagnostics);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close);
    shell->addWidget(buttons);
    QHash<QString, QWidget*> fields;
    QTimer refresh; refresh.setSingleShot(true); refresh.setInterval(300);
    const auto changed = [&] { refresh.start(); };
    auto* preset = new QComboBox;
    preset->addItems({QStringLiteral("Standard A4"), QStringLiteral("School assignment"), QStringLiteral("Technical report"), QStringLiteral("Compact notes"), QStringLiteral("Custom")});
    page_form->addRow(QStringLiteral("Preset"), preset);
    const auto combo = [&](QFormLayout* form, const QString& label, const QString& key, const QStringList& labels, const QList<int>& ids) {
        auto* widget = new QComboBox; for (int i = 0; i < labels.size(); ++i) widget->addItem(labels[i], ids[i]);
        widget->setAccessibleName(label); form->addRow(label, widget); fields.insert(key, widget);
        QObject::connect(widget, &QComboBox::currentIndexChanged, &dialog, changed); return widget;
    };
    combo(page_form, QStringLiteral("Paper"), QStringLiteral("paper"),
          {"ISO A4", "ISO A3", "ISO A5", "ISO B5", "US Letter", "US Legal", "Custom"},
          {QPageSize::A4, QPageSize::A3, QPageSize::A5, QPageSize::B5, QPageSize::Letter, QPageSize::Legal, QPageSize::Custom});
    const auto decimal = [&](QFormLayout* form, const QString& label, const QString& key, double min, double max, const QString& suffix = QString()) {
        auto* widget = new QDoubleSpinBox; widget->setRange(min, max); widget->setDecimals(1); widget->setSuffix(suffix);
        widget->setAccessibleName(label); form->addRow(label, widget); fields.insert(key, widget);
        QObject::connect(widget, &QDoubleSpinBox::valueChanged, &dialog, changed); return widget;
    };
    const auto check = [&](QFormLayout* form, const QString& label, const QString& key) {
        auto* widget = new QCheckBox(label); form->addRow(widget); fields.insert(key, widget);
        QObject::connect(widget, &QCheckBox::toggled, &dialog, changed); return widget;
    };
    const auto text = [&](QFormLayout* form, const QString& label, const QString& key) {
        auto* widget = new QLineEdit; widget->setAccessibleName(label); form->addRow(label, widget); fields.insert(key, widget);
        QObject::connect(widget, &QLineEdit::textChanged, &dialog, changed); return widget;
    };
    decimal(page_form, QStringLiteral("Custom width"), "width", 40, 2000, " mm");
    decimal(page_form, QStringLiteral("Custom height"), "height", 40, 2000, " mm");
    check(page_form, QStringLiteral("Landscape"), "landscape");
    for (const auto& item : {std::pair{"Left margin", "left"}, {"Top margin", "top"}, {"Right margin", "right"}, {"Bottom margin", "bottom"}})
        decimal(page_form, QLatin1String(item.first), QLatin1String(item.second), 0, 500, " mm");
    decimal(page_form, QStringLiteral("Content width (0 = auto)"), "contentWidth", 0, 1800, " mm");
    check(page_form, QStringLiteral("Page break before H1"), "headingBreaks");
    for (const auto& item : {std::pair{"Body font", "bodyFont"}, {"Heading font", "headingFont"}}) {
        auto* widget = new QFontComboBox; widget->setAccessibleName(QLatin1String(item.first));
        style_form->addRow(QLatin1String(item.first), widget); fields.insert(QLatin1String(item.second), widget);
        QObject::connect(widget, &QFontComboBox::currentFontChanged, &dialog, changed);
    }
    decimal(style_form, QStringLiteral("Body size"), "fontPoints", 6, 40, " pt");
    decimal(style_form, QStringLiteral("Heading scale"), "headingScale", .5, 2);
    decimal(style_form, QStringLiteral("Line spacing"), "linePercent", 100, 250, "%");
    decimal(style_form, QStringLiteral("Paragraph spacing"), "paragraphPoints", 0, 72, " pt");
    combo(style_form, QStringLiteral("Page background"), "backgroundChoice", {"White", "Warm paper", "Light graphite"}, {0, 1, 2});
    check(style_form, QStringLiteral("Wrap long code lines"), "wrapCode");
    check(style_form, QStringLiteral("Syntax highlighting"), "syntax");
    decimal(style_form, QStringLiteral("Image resolution"), "imageDpi", 72, 600, " dpi");
    decimal(style_form, QStringLiteral("PNG compression effort"), "imageQuality", 1, 100, "%");
    text(details_form, QStringLiteral("Title"), "title"); text(details_form, QStringLiteral("Author"), "author");
    text(details_form, QStringLiteral("Header"), "header"); text(details_form, QStringLiteral("Footer"), "footer");
    check(details_form, QStringLiteral("Different first page"), "differentFirst");
    text(details_form, QStringLiteral("First header"), "firstHeader"); text(details_form, QStringLiteral("First footer"), "firstFooter");
    check(details_form, QStringLiteral("Page numbers"), "pageNumbers"); check(details_form, QStringLiteral("Roman page numbers"), "romanNumbers");
    check(details_form, QStringLiteral("Automatic contents"), "toc"); check(details_form, QStringLiteral("Cover page"), "cover");
    auto* tokens = new QLabel(QStringLiteral("Header/footer fields: {title}, {author}, {date}, {page}, {pages}.\nContents are regenerated from native headings."));
    tokens->setWordWrap(true); details_form->addRow(tokens);
    auto* format = new QComboBox; format->setAccessibleName(QStringLiteral("Export format"));
    for (const auto& item : {std::pair{"PDF", "pdf"}, {"Word (editable DOCX)", "docx"}, {"OpenDocument (ODT)", "odt"}, {"Standalone HTML", "html"}, {"Plain text", "txt"}})
        format->addItem(QLatin1String(item.first), QLatin1String(item.second));
    export_form->addRow(QStringLiteral("Format"), format);
    auto* scope = new QComboBox; scope->addItem(QStringLiteral("Complete document"));
    if (selected) scope->addItem(QStringLiteral("Selected Reader content"));
    export_form->addRow(QStringLiteral("Content"), scope);
    QObject::connect(scope, &QComboBox::currentIndexChanged, &dialog, changed);
    check(export_form, QStringLiteral("Preserve links"), "links");
    auto* destination = new QLineEdit; destination->setAccessibleName(QStringLiteral("Export destination"));
    export_form->addRow(QStringLiteral("Destination"), destination);
    auto* browse = new QPushButton(QStringLiteral("Choose destination…")); export_form->addRow(browse);
    auto* export_button = new QPushButton(QStringLiteral("Export document")); export_form->addRow(export_button);
    auto* note = new QLabel(QStringLiteral("PDF matches this preview. Editable formats can reflow in other applications. HTML embeds local images and remains usable offline. Missing/blocked images remain placeholders. Reopen preview after external content changes."));
    note->setWordWrap(true); export_form->addRow(note);
    bool filling = false;
    const auto fill = [&](const PageSettings& value) {
        filling = true;
        const auto values = value.toJson();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const auto v = values.value(it.key());
            if (auto* font = qobject_cast<QFontComboBox*>(it.value())) font->setCurrentFont(QFont(v.toString()));
            else if (auto* cb = qobject_cast<QCheckBox*>(it.value())) cb->setChecked(v.toBool());
            else if (auto* spin = qobject_cast<QDoubleSpinBox*>(it.value())) spin->setValue(v.toDouble());
            else if (auto* edit = qobject_cast<QLineEdit*>(it.value())) edit->setText(v.toString());
            else if (auto* cbx = qobject_cast<QComboBox*>(it.value())) {
                if (it.key() == QStringLiteral("backgroundChoice")) cbx->setCurrentIndex(value.background == QColor("#fffaf0") ? 1 : value.background == QColor("#eeeeee") ? 2 : 0);
                else cbx->setCurrentIndex(cbx->findData(v.toInt()));
            }
        }
        preset->setCurrentText(value.preset); filling = false; refresh.start();
    };
    const auto collect = [&] {
        auto values = settings.toJson();
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
            if (auto* font = qobject_cast<QFontComboBox*>(it.value())) values.insert(it.key(), font->currentFont().family());
            else if (auto* cb = qobject_cast<QCheckBox*>(it.value())) values.insert(it.key(), cb->isChecked());
            else if (auto* spin = qobject_cast<QDoubleSpinBox*>(it.value())) values.insert(it.key(), spin->value());
            else if (auto* edit = qobject_cast<QLineEdit*>(it.value())) values.insert(it.key(), edit->text());
            else if (auto* cbx = qobject_cast<QComboBox*>(it.value())) {
                if (it.key() == QStringLiteral("backgroundChoice")) values.insert("background", cbx->currentIndex() == 1 ? "#fffaf0" : cbx->currentIndex() == 2 ? "#eeeeee" : "#ffffff");
                else values.insert(it.key(), cbx->currentData().toInt());
            }
        }
        values.insert("preset", preset->currentText());
        return values;
    };
    // Validate the raw values before fromJson's recovery-to-defaults policy.
    const auto read = [&]() -> std::optional<PageSettings> {
        const auto values = collect();
        const auto candidate = PageSettings::fromJson(values);
        if (candidate.toJson() != values) {
            auto trimmed = values; trimmed.remove("backgroundChoice");
            if (candidate.toJson() != trimmed) {
                diagnostics->setText(QStringLiteral("Invalid layout: margins/content width must fit inside the selected page."));
                if (qEnvironmentVariableIsSet("WOLFMARK_OUTPUT_TEST"))
                    std::fprintf(stderr, "EXPORT_DIALOG requested=%s candidate=%s\n", QJsonDocument(trimmed).toJson(QJsonDocument::Compact).constData(), QJsonDocument(candidate.toJson()).toJson(QJsonDocument::Compact).constData());
                return std::nullopt;
            }
        }
        return candidate;
    };
    std::unique_ptr<DocumentOutput> output;
    const auto fit = [&] {
        if (zoom->currentIndex() == 0) preview->fitToWidth();
        else if (zoom->currentIndex() == 1) preview->fitInView();
        else preview->setZoomFactor(zoom->currentText().chopped(1).toDouble() / 100);
    };
    QObject::connect(&refresh, &QTimer::timeout, &dialog, [&] {
        if (filling) return;
        const auto candidate = read(); if (!candidate) { export_button->setEnabled(false); print_button->setEnabled(false); return; }
        output = std::make_unique<DocumentOutput>(scope->currentIndex() == 1 ? selected.get() : source, source_zoom, *candidate);
        preview->setDocument(output.get()); fit();
        page->setMaximum(output->pageCount()); total->setText(QStringLiteral("of %1").arg(output->pageCount()));
        export_button->setEnabled(true); print_button->setEnabled(true);
        const auto mm = candidate->layout().fullRect(QPageLayout::Millimeter).size();
        diagnostics->setText(QStringLiteral("%1 %2 · %3 × %4 mm · source unchanged").arg(output->pageCount())
            .arg(output->pageCount() == 1 ? QStringLiteral("page") : QStringLiteral("pages"), QString::number(mm.width()), QString::number(mm.height())));
        if (output->blockedLinkSpans() > 0)
            diagnostics->setText(diagnostics->text() + QStringLiteral(" · %1 unsafe link spans made inert").arg(output->blockedLinkSpans()));
    });
    QObject::connect(preset, &QComboBox::activated, &dialog, [&](int) {
        if (preset->currentText() == QStringLiteral("Custom")) return;
        auto value = PageSettings::forPreset(preset->currentText());
        value.title = qobject_cast<QLineEdit*>(fields.value("title"))->text(); value.author = qobject_cast<QLineEdit*>(fields.value("author"))->text(); fill(value);
    });
    QObject::connect(view_mode, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
        preview->setContinuous(index == 0); fit();
    });
    QObject::connect(zoom, &QComboBox::currentIndexChanged, &dialog, [&](int) { fit(); });
    QObject::connect(previous, &QPushButton::clicked, &dialog, [&] { page->setValue(page->value() - 1); });
    QObject::connect(next, &QPushButton::clicked, &dialog, [&] { page->setValue(page->value() + 1); });
    QObject::connect(page, &QSpinBox::valueChanged, &dialog, [&](int value) { preview->setCurrentPage(value); });
    preview->pageChanged = [&](int value) { QSignalBlocker blocker(page); page->setValue(value); };
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const auto extension = format->currentData().toString();
        QFileDialog picker(&dialog, QStringLiteral("Export document"), QFileInfo(source_path).absolutePath());
        picker.setAcceptMode(QFileDialog::AcceptSave); picker.setDefaultSuffix(extension);
        picker.setNameFilter(QStringLiteral("%1 (*.%2)").arg(format->currentText(), extension));
        picker.selectFile(QFileInfo(source_path).completeBaseName() + QLatin1Char('.') + extension);
        if (picker.exec() == QDialog::Accepted) destination->setText(picker.selectedFiles().value(0));
    });
    QObject::connect(format, &QComboBox::currentIndexChanged, &dialog, [&](int) { destination->clear(); });
    QObject::connect(export_button, &QPushButton::clicked, &dialog, [&] {
        refresh.stop(); const auto candidate = read(); if (!candidate) return;
        output = std::make_unique<DocumentOutput>(scope->currentIndex() == 1 ? selected.get() : source, source_zoom, *candidate);
        preview->setDocument(output.get()); fit();
        if (destination->text().trimmed().isEmpty()) { browse->click(); if (destination->text().trimmed().isEmpty()) return; }
        const QFileInfo target(destination->text());
        if ((!target.canonicalFilePath().isEmpty() && target.canonicalFilePath().compare(QFileInfo(source_path).canonicalFilePath(),
#ifdef _WIN32
            Qt::CaseInsensitive
#else
            Qt::CaseSensitive
#endif
            ) == 0) || target.absoluteFilePath() == QFileInfo(source_path).absoluteFilePath()) {
            QMessageBox::warning(&dialog, QStringLiteral("Choose another destination"), QStringLiteral("An export cannot replace its source document.")); return;
        }
        if (target.exists() && QMessageBox::question(&dialog, QStringLiteral("Replace export?"), QStringLiteral("Replace %1?").arg(target.absoluteFilePath()), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        QString error;
        if (!output->write(format->currentData().toString(), target.absoluteFilePath(), api, &error))
            QMessageBox::warning(&dialog, QStringLiteral("Export failed"), error);
        else diagnostics->setText(QStringLiteral("Exported %1").arg(target.absoluteFilePath()));
    });
    QObject::connect(print_button, &QPushButton::clicked, &dialog, [&] {
        refresh.stop(); const auto candidate = read(); if (!candidate) return;
        output = std::make_unique<DocumentOutput>(scope->currentIndex() == 1 ? selected.get() : source, source_zoom, *candidate);
        preview->setDocument(output.get()); fit();
        QPrinter printer(QPrinter::HighResolution); printer.setFullPage(true);
        auto layout = output->settings().layout(); layout.setMode(QPageLayout::FullPageMode); printer.setPageLayout(layout);
        QPrintDialog print_dialog(&printer, &dialog); print_dialog.setMinMax(1, output->pageCount());
        if (print_dialog.exec() == QDialog::Accepted) { QString error; if (!output->print(printer, &error, printer.fromPage(), printer.toPage())) QMessageBox::warning(&dialog, QStringLiteral("Print failed"), error); }
    });
    QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] {
        if (const auto candidate = read()) { settings = *candidate; diagnostics->setText(QStringLiteral("Page settings applied. Close to return to the document.")); }
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto initial = settings;
    if (initial.title.isEmpty()) initial.title = QFileInfo(source_path).completeBaseName();
    fill(initial); if (export_tab) tabs->setCurrentIndex(3);
    const auto requested_tab = qEnvironmentVariable("WOLFMARK_OUTPUT_TAB");
    if (requested_tab == QStringLiteral("page")) tabs->setCurrentIndex(0);
    else if (requested_tab == QStringLiteral("style")) tabs->setCurrentIndex(1);
    else if (requested_tab == QStringLiteral("details")) tabs->setCurrentIndex(2);
    if (qEnvironmentVariableIsSet("WOLFMARK_OUTPUT_SINGLE")) view_mode->setCurrentIndex(1);
    if (qEnvironmentVariableIsSet("WOLFMARK_OUTPUT_ZOOM")) zoom->setCurrentText(qEnvironmentVariable("WOLFMARK_OUTPUT_ZOOM") + QLatin1Char('%'));
    const auto snapshot = qEnvironmentVariable("WOLFMARK_OUTPUT_SNAPSHOT");
    if (!snapshot.isEmpty()) {
        QTimer::singleShot(1500, &dialog, [&] { dialog.grab().save(snapshot); dialog.reject(); });
    }
    if (qEnvironmentVariableIsSet("WOLFMARK_OUTPUT_TEST")) {
        QTimer::singleShot(1200, &dialog, [&] {
            const auto read_file = [](const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
            const auto before = read_file(source_path);
            auto blocked = std::make_shared<bool>(false);
            QTimer::singleShot(100, &dialog, [blocked] {
                if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    *blocked = message->text().contains(QStringLiteral("cannot replace its source")); message->accept();
                }
            });
            destination->setText(source_path); export_button->click();
            bool ok = *blocked && read_file(source_path) == before;
            std::fprintf(stderr, "EXPORT_DIALOG source_guard=%d unchanged=%d enabled=%d diagnostic=%s\n", *blocked, read_file(source_path) == before, export_button->isEnabled(), qUtf8Printable(diagnostics->text()));
            QTemporaryDir temporary;
            ok &= temporary.isValid();
            const auto target = temporary.filePath(QStringLiteral("dialog-output.pdf"));
            { QFile file(target); ok &= file.open(QIODevice::WriteOnly) && file.write("preserve existing export") > 0; }
            destination->setText(target);
            auto denied = std::make_shared<bool>(false);
            if (ok) {
                QTimer::singleShot(100, &dialog, [denied] {
                    if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                        *denied = message->defaultButton() == message->button(QMessageBox::No);
                        if (auto* button = message->button(QMessageBox::No)) button->click();
                    }
                });
                export_button->click();
                ok &= *denied && read_file(target) == QByteArray("preserve existing export");
                std::fprintf(stderr, "EXPORT_DIALOG default_no=%d existing_preserved=%d\n", *denied, read_file(target) == QByteArray("preserve existing export"));
            }
            if (ok) {
                QTimer::singleShot(100, &dialog, [] {
                    if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                        if (auto* button = message->button(QMessageBox::Yes)) button->click();
                });
                export_button->click(); ok &= read_file(target).startsWith("%PDF-");
                std::fprintf(stderr, "EXPORT_DIALOG replacement_pdf=%d diagnostic=%s\n", read_file(target).startsWith("%PDF-"), qUtf8Printable(diagnostics->text()));
            }
            ok &= read_file(source_path) == before;
            QCoreApplication::instance()->setProperty("exportDialogOk", ok);
            std::fprintf(stdout, "WOLFMARK_SMOKE export_dialog=%s source_guard=checked overwrite_no=checked overwrite_yes=checked\n", ok ? "ok" : "failed");
            dialog.reject();
        });
    }
    dialog.exec();
    preview->pageChanged = {};
    preview->setDocument(nullptr);
    return true;
}

QByteArray DocumentOutput::odt(const WolfmarkApiTable* api, QString* error) const {
    QJsonArray parts;
    addPart(parts, QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.text");
    const QString ns = QStringLiteral(" xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" office:version=\"1.3\"");
    QString automatic;
    QHash<QString, QString> style_names;
    QHash<QString, QString> images;
    const auto style = [&](const QString& family, const QString& properties) {
        const QString key = family + properties;
        auto name = style_names.value(key);
        if (name.isEmpty()) {
            name = QStringLiteral("S%1").arg(style_names.size() + 1); style_names.insert(key, name);
            automatic += QStringLiteral("<style:style style:name=\"%1\" style:family=\"%2\">%3</style:style>").arg(name, family, properties);
        }
        return name;
    };
    const auto literal = [](QString text) {
        text.remove(QChar(0x200B)); text.remove(QChar::ObjectReplacementCharacter);
        QString result;
        for (auto character : text) {
            if (character == QLatin1Char(' ')) result += QStringLiteral("<text:s/>");
            else if (character == QLatin1Char('\t')) result += QStringLiteral("<text:tab/>");
            else if (character == QLatin1Char('\n') || character == QChar::LineSeparator) result += QStringLiteral("<text:line-break/>");
            else if (character.unicode() >= 0x20) result += xml(QString(character));
        }
        return result;
    };
    const auto paragraph = [&](const QTextBlock& block) {
        const auto bf = block.blockFormat(); const int heading = bf.headingLevel();
        QString properties = QStringLiteral("<style:paragraph-properties fo:margin-left=\"%1pt\" fo:text-indent=\"%2pt\" fo:margin-top=\"%3pt\" fo:margin-bottom=\"%4pt\" fo:line-height=\"%5%\" fo:text-align=\"%6\" fo:break-before=\"%7\"/>")
            .arg(number(bf.leftMargin()), number(bf.textIndent()), number(bf.topMargin()), number(bf.bottomMargin()))
            .arg(settings_.line_percent).arg(bf.alignment().testFlag(Qt::AlignRight) ? "end" : bf.alignment().testFlag(Qt::AlignHCenter) ? "center" : "start")
            .arg(bf.pageBreakPolicy().testFlag(QTextFormat::PageBreak_AlwaysBefore) ? "page" : "auto");
        const auto name = style(QStringLiteral("paragraph"), properties);
        const auto tag = heading > 0 ? QStringLiteral("text:h") : QStringLiteral("text:p");
        QString result = QStringLiteral("<%1 text:style-name=\"%2\"%3>").arg(tag, name,
            heading > 0 ? QStringLiteral(" text:outline-level=\"%1\"").arg(heading) : QString());
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment(); const auto cf = fragment.charFormat();
            if (cf.isImageFormat()) {
                const auto image = cf.toImageFormat(); auto image_name = images.value(image.name());
                if (image_name.isEmpty()) {
                    image_name = QStringLiteral("Pictures/image%1.png").arg(images.size() + 1);
                    const auto bytes = png(resourceImage(document_.get(), image.name()), settings_.image_quality);
                    if (bytes.isEmpty()) { result += QStringLiteral("[Image unavailable]"); continue; }
                    images.insert(image.name(), image_name); addPart(parts, image_name, bytes);
                }
                result += QStringLiteral("<draw:frame text:anchor-type=\"as-char\" svg:width=\"%1pt\" svg:height=\"%2pt\"><draw:image xlink:href=\"%3\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame>")
                    .arg(number(image.width()), number(image.height()), image_name);
                continue;
            }
            QString attributes = QStringLiteral("fo:font-family=\"%1\" fo:font-size=\"%2pt\" fo:color=\"%3\"")
                .arg(xml(cf.fontFamilies().toStringList().value(0, settings_.body_font)), number(cf.fontPointSize() > 0 ? cf.fontPointSize() : settings_.font_points), cf.foreground().color().name());
            if (cf.fontWeight() >= QFont::DemiBold) attributes += QStringLiteral(" fo:font-weight=\"bold\"");
            if (cf.fontItalic()) attributes += QStringLiteral(" fo:font-style=\"italic\"");
            if (cf.fontStrikeOut()) attributes += QStringLiteral(" style:text-line-through-style=\"solid\"");
            if (cf.fontUnderline()) attributes += QStringLiteral(" style:text-underline-style=\"solid\" style:text-underline-width=\"auto\"");
            const auto run = QStringLiteral("<text:span text:style-name=\"%1\">%2</text:span>")
                .arg(style(QStringLiteral("text"), QStringLiteral("<style:text-properties %1/>").arg(attributes)), literal(fragment.text()));
            if (!cf.anchorHref().isEmpty()) result += QStringLiteral("<text:a xlink:type=\"simple\" xlink:href=\"%1\">%2</text:a>").arg(xml(cf.anchorHref()), run);
            else result += run;
        }
        return result + QStringLiteral("</%1>").arg(tag);
    };
    const auto table_style = style(QStringLiteral("table"), QStringLiteral("<style:table-properties style:width=\"%1pt\" table:align=\"left\"/>").arg(number(contentRect().width())));
    const auto cell_style = style(QStringLiteral("table-cell"), QStringLiteral("<style:table-cell-properties fo:border=\"0.5pt solid #bbbbbb\" fo:padding=\"4pt\"/>"));
    std::function<QString(QTextFrame::iterator, QTextFrame::iterator)> serialize;
    int table_id = 0;
    serialize = [&](QTextFrame::iterator begin, QTextFrame::iterator end) {
        QString result;
        for (auto it = begin; it != end; ++it) {
            if (auto* table = qobject_cast<QTextTable*>(it.currentFrame())) {
                result += QStringLiteral("<table:table table:name=\"Table%1\" table:style-name=\"%2\"><table:table-column table:number-columns-repeated=\"%3\"/>")
                    .arg(++table_id).arg(table_style).arg(table->columns());
                for (int row = 0; row < table->rows(); ++row) {
                    if (row == 0) result += QStringLiteral("<table:table-header-rows>");
                    result += QStringLiteral("<table:table-row>");
                    for (int column = 0; column < table->columns(); ++column) {
                        const auto cell = table->cellAt(row, column);
                        result += QStringLiteral("<table:table-cell table:style-name=\"%1\" office:value-type=\"string\">").arg(cell_style)
                            + serialize(cell.begin(), cell.end()) + QStringLiteral("</table:table-cell>");
                    }
                    result += QStringLiteral("</table:table-row>");
                    if (row == 0) result += QStringLiteral("</table:table-header-rows>");
                }
                result += QStringLiteral("</table:table>");
            } else if (auto* frame = it.currentFrame()) result += serialize(frame->begin(), frame->end());
            else if (it.currentBlock().isValid()) result += paragraph(it.currentBlock());
        }
        return result;
    };
    const auto body = serialize(document_->rootFrame()->begin(), document_->rootFrame()->end());
    addXml(parts, QStringLiteral("content.xml"), QStringLiteral("<office:document-content%1><office:automatic-styles>%2</office:automatic-styles><office:body><office:text>%3</office:text></office:body></office:document-content>").arg(ns, automatic, body));
    const auto header_footer = [&](QString text, bool numbers) {
        text.replace(QStringLiteral("{page}"), QString(QChar(0xE000))).replace(QStringLiteral("{pages}"), QString(QChar(0xE001)));
        text = expanded(text, settings_, 1, pageCount());
        auto encoded = literal(text);
        encoded.replace(QString(QChar(0xE000)), QStringLiteral("<text:page-number text:select-page=\"current\" style:num-format=\"%1\">1</text:page-number>").arg(settings_.roman_numbers ? "i" : "1"));
        encoded.replace(QString(QChar(0xE001)), QStringLiteral("<text:page-count>%1</text:page-count>").arg(pageCount()));
        QString result = QStringLiteral("<text:p>") + encoded;
        if (numbers) result += QStringLiteral("<text:tab/><text:page-number text:select-page=\"current\" style:num-format=\"%1\">1</text:page-number><text:s/>/<text:s/><text:page-count>%2</text:page-count>")
            .arg(settings_.roman_numbers ? "i" : "1").arg(pageCount());
        return result + QStringLiteral("</text:p>");
    };
    const auto size = settings_.layout().fullRect(QPageLayout::Millimeter).size(); const auto margins = settings_.margins_mm;
    QString styles = QStringLiteral("<office:document-styles%1><office:styles><style:default-style style:family=\"paragraph\"><style:text-properties fo:font-family=\"%2\" fo:font-size=\"%3pt\" fo:color=\"#202020\"/></style:default-style></office:styles><office:automatic-styles><style:page-layout style:name=\"Page\"><style:page-layout-properties fo:page-width=\"%4mm\" fo:page-height=\"%5mm\" style:print-orientation=\"%6\" fo:margin-left=\"%7mm\" fo:margin-top=\"%8mm\" fo:margin-right=\"%9mm\" fo:margin-bottom=\"%10mm\" fo:background-color=\"%11\"/></style:page-layout></office:automatic-styles><office:master-styles><style:master-page style:name=\"Standard\" style:page-layout-name=\"Page\"><style:header>%12</style:header><style:footer>%13</style:footer></style:master-page></office:master-styles></office:document-styles>")
        .arg(ns, xml(settings_.body_font), number(settings_.font_points), number(size.width()), number(size.height()), settings_.landscape ? "landscape" : "portrait",
             number(margins.left()), number(margins.top()), number(margins.right()))
        .arg(number(margins.bottom()), settings_.background.name(), header_footer(settings_.header, false), header_footer(settings_.footer, settings_.page_numbers));
    if (settings_.different_first) {
        styles.replace(QStringLiteral("</style:header><style:footer>"), QStringLiteral("</style:header><style:header-first>%1</style:header-first><style:footer>").arg(header_footer(settings_.first_header, false)));
        styles.replace(QStringLiteral("</style:footer></style:master-page>"), QStringLiteral("</style:footer><style:footer-first>%1</style:footer-first></style:master-page>").arg(header_footer(settings_.first_footer, settings_.page_numbers)));
    }
    addXml(parts, QStringLiteral("styles.xml"), styles);
    addXml(parts, QStringLiteral("meta.xml"), QStringLiteral("<office:document-meta%1 xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\"><office:meta><dc:title>%2</dc:title><meta:initial-creator>%3</meta:initial-creator><meta:generator>Wolfmark</meta:generator></office:meta></office:document-meta>").arg(ns, xml(settings_.title), xml(settings_.author)));
    QString manifest = QStringLiteral("<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\"><manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\" manifest:version=\"1.3\"/>");
    for (const auto& name : {QStringLiteral("content.xml"), QStringLiteral("styles.xml"), QStringLiteral("meta.xml")}) manifest += QStringLiteral("<manifest:file-entry manifest:full-path=\"%1\" manifest:media-type=\"text/xml\"/>").arg(name);
    for (const auto& name : images) manifest += QStringLiteral("<manifest:file-entry manifest:full-path=\"%1\" manifest:media-type=\"image/png\"/>").arg(name);
    addXml(parts, QStringLiteral("META-INF/manifest.xml"), manifest + QStringLiteral("</manifest:manifest>"));
    return finishPackage(parts, api, error);
}

QByteArray DocumentOutput::docx(const WolfmarkApiTable* api, QString* error) const {
    QJsonArray parts;
    QString relationships;
    int relationship_id = 0;
    int drawing_id = 0;
    int bookmark_id = 0;
    QHash<QString, QString> images;
    QHash<QString, QString> bookmarks;
    QHash<int, int> list_numbers;
    QString numbering_instances;
    const auto relationship = [&](const QString& type, const QString& target, bool external = false) {
        const auto id = QStringLiteral("rId%1").arg(++relationship_id);
        relationships += QStringLiteral("<Relationship Id=\"%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/%2\" Target=\"%3\"%4/>")
            .arg(id, type, xml(target), external ? QStringLiteral(" TargetMode=\"External\"") : QString());
        return id;
    };
    for (auto block = document_->begin(); block.isValid(); block = block.next()) for (auto it = block.begin(); !it.atEnd(); ++it)
        for (const auto& anchor : it.fragment().charFormat().anchorNames())
            if (!bookmarks.contains(anchor)) bookmarks.insert(anchor, QStringLiteral("wolfmark_%1").arg(bookmarks.size() + 1));
    const auto textRun = [](QString text) {
        text.remove(QChar(0x200B));
        text.remove(QChar::ObjectReplacementCharacter);
        QString result = QStringLiteral("<w:t xml:space=\"preserve\">");
        for (const auto character : text) {
            if (character == QLatin1Char('\t')) result += QStringLiteral("</w:t><w:tab/><w:t xml:space=\"preserve\">");
            else if (character == QLatin1Char('\n') || character == QChar::LineSeparator) result += QStringLiteral("</w:t><w:br/><w:t xml:space=\"preserve\">");
            else if (character.unicode() >= 0x20 || character.isHighSurrogate() || character.isLowSurrogate()) result += xml(QString(character));
        }
        return result + QStringLiteral("</w:t>");
    };
    const auto paragraph = [&](const QTextBlock& block) {
        const auto bf = block.blockFormat();
        const int heading = bf.headingLevel();
        const auto align = bf.alignment().testFlag(Qt::AlignRight) ? QStringLiteral("right") :
                           bf.alignment().testFlag(Qt::AlignHCenter) ? QStringLiteral("center") : QStringLiteral("left");
        QString result = QStringLiteral("<w:p><w:pPr>");
        QString numbering_properties;
        const bool native_list = bf.hasProperty(property::list_id) && !bf.boolProperty(property::list_task);
        if (native_list) {
            const int identity = bf.intProperty(property::list_id);
            int list_number = list_numbers.value(identity);
            const int level = std::clamp(bf.intProperty(property::list_level), 0, 8);
            if (list_number == 0) {
                list_number = list_numbers.size() + 1; list_numbers.insert(identity, list_number);
                numbering_instances += QStringLiteral("<w:num w:numId=\"%1\"><w:abstractNumId w:val=\"%2\"/><w:lvlOverride w:ilvl=\"%3\"><w:startOverride w:val=\"%4\"/></w:lvlOverride></w:num>")
                    .arg(list_number).arg(bf.boolProperty(property::list_ordered) ? 1 : 2).arg(level).arg(std::max(1, bf.intProperty(property::list_start)));
            }
            numbering_properties = QStringLiteral("<w:numPr><w:ilvl w:val=\"%1\"/><w:numId w:val=\"%2\"/></w:numPr>").arg(level).arg(list_number);
        }
        if (heading > 0) result += QStringLiteral("<w:pStyle w:val=\"Heading%1\"/><w:keepNext/>").arg(heading);
        if (bf.pageBreakPolicy().testFlag(QTextFormat::PageBreak_AlwaysBefore)) result += QStringLiteral("<w:pageBreakBefore/>");
        result += numbering_properties;
        const double inset = std::max(0.0, contentRect().left() - settings_.margins_mm.left() * points_per_mm);
        const auto* frame = QTextCursor(block).currentFrame();
        if (frame && frame->frameFormat().boolProperty(QTextFormat::UserProperty + 3))
            result += QStringLiteral("<w:shd w:val=\"clear\" w:fill=\"F3F3F3\"/>");
        result += QStringLiteral("<w:spacing w:after=\"%2\" w:line=\"%3\" w:lineRule=\"auto\"/><w:ind w:left=\"%4\" w:hanging=\"%5\" w:right=\"%6\"/><w:jc w:val=\"%1\"/></w:pPr>")
            .arg(align).arg(twips(bf.bottomMargin())).arg(settings_.line_percent * 240 / 100)
            .arg(twips(bf.leftMargin() + inset)).arg(std::max(0, -twips(bf.textIndent()))).arg(twips(bf.rightMargin() + inset));
        int skip_marker = native_list ? static_cast<int>(block.text().indexOf(QLatin1Char('\t'))) + 1 : 0;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto cf = fragment.charFormat();
            for (const auto& anchor : cf.anchorNames()) {
                const int id = ++bookmark_id;
                result += QStringLiteral("<w:bookmarkStart w:id=\"%1\" w:name=\"%2\"/><w:bookmarkEnd w:id=\"%1\"/>").arg(id).arg(bookmarks.value(anchor));
            }
            if (cf.isImageFormat()) {
                const auto image = cf.toImageFormat();
                auto id = images.value(image.name());
                if (id.isEmpty()) {
                    const auto bytes = png(resourceImage(document_.get(), image.name()), settings_.image_quality);
                    if (bytes.isEmpty()) { result += QStringLiteral("<w:r><w:t>[Image unavailable]</w:t></w:r>"); continue; }
                    const auto name = QStringLiteral("media/image%1.png").arg(images.size() + 1);
                    addPart(parts, QStringLiteral("word/") + name, bytes);
                    id = relationship(QStringLiteral("image"), name); images.insert(image.name(), id);
                }
                const qint64 width = static_cast<qint64>(image.width() * 12700);
                const qint64 height = static_cast<qint64>(image.height() * 12700);
                result += QStringLiteral("<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"%1\" cy=\"%2\"/><wp:docPr id=\"%3\" name=\"Image %3\"/><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\"0\" name=\"Image\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"%4\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>")
                    .arg(width).arg(height).arg(++drawing_id).arg(id);
                continue;
            }
            const auto family = cf.fontFamilies().toStringList().value(0, settings_.body_font);
            QString properties = QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:eastAsia=\"%1\"/>").arg(xml(family));
            if (cf.fontWeight() >= QFont::DemiBold) properties += QStringLiteral("<w:b/>");
            if (cf.fontItalic()) properties += QStringLiteral("<w:i/>");
            if (cf.fontStrikeOut()) properties += QStringLiteral("<w:strike/>");
            properties += QStringLiteral("<w:color w:val=\"%1\"/><w:sz w:val=\"%2\"/>")
                .arg(cf.foreground().color().name().mid(1)).arg(static_cast<int>(std::lround((cf.fontPointSize() > 0 ? cf.fontPointSize() : settings_.font_points) * 2)));
            if (cf.fontUnderline()) properties += QStringLiteral("<w:u w:val=\"single\"/>");
            if (cf.background().style() != Qt::NoBrush) properties += QStringLiteral("<w:shd w:val=\"clear\" w:fill=\"%1\"/>").arg(cf.background().color().name().mid(1));
            const QString text = fragment.text().mid(skip_marker);
            skip_marker = std::max(0, skip_marker - fragment.length());
            const QString run = QStringLiteral("<w:r><w:rPr>%1</w:rPr>%2</w:r>").arg(properties, textRun(text));
            const auto target = cf.anchorHref();
            if (target.startsWith(QLatin1Char('#')) && bookmarks.contains(target.mid(1)))
                result += QStringLiteral("<w:hyperlink w:anchor=\"%1\">%2</w:hyperlink>").arg(bookmarks.value(target.mid(1)), run);
            else if (!target.isEmpty() && !target.startsWith(QStringLiteral("wolfmark-")))
                result += QStringLiteral("<w:hyperlink r:id=\"%1\">%2</w:hyperlink>").arg(relationship(QStringLiteral("hyperlink"), target, true), run);
            else result += run;
        }
        return result + QStringLiteral("</w:p>");
    };
    std::function<QString(QTextFrame::iterator, QTextFrame::iterator)> serialize;
    serialize = [&](QTextFrame::iterator begin, QTextFrame::iterator end) {
        QString result;
        for (auto it = begin; it != end; ++it) {
            if (auto* table = qobject_cast<QTextTable*>(it.currentFrame())) {
                result += QStringLiteral("<w:tbl><w:tblPr><w:tblW w:w=\"5000\" w:type=\"pct\"/><w:tblBorders>");
                for (const auto* edge : {"top", "left", "bottom", "right", "insideH", "insideV"})
                    result += QStringLiteral("<w:%1 w:val=\"single\" w:sz=\"4\" w:color=\"BBBBBB\"/>").arg(QLatin1String(edge));
                result += QStringLiteral("</w:tblBorders><w:tblCellMar><w:top w:w=\"80\" w:type=\"dxa\"/><w:left w:w=\"80\" w:type=\"dxa\"/><w:bottom w:w=\"80\" w:type=\"dxa\"/><w:right w:w=\"80\" w:type=\"dxa\"/></w:tblCellMar></w:tblPr><w:tblGrid>");
                for (int col = 0; col < table->columns(); ++col) result += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(twips(contentRect().width()) / table->columns());
                result += QStringLiteral("</w:tblGrid>");
                for (int row = 0; row < table->rows(); ++row) {
                    result += QStringLiteral("<w:tr>");
                    if (row == 0) result += QStringLiteral("<w:trPr><w:tblHeader/></w:trPr>");
                    for (int col = 0; col < table->columns(); ++col) {
                        const auto cell = table->cellAt(row, col);
                        result += QStringLiteral("<w:tc><w:tcPr><w:tcW w:w=\"%1\" w:type=\"dxa\"/><w:shd w:val=\"clear\" w:fill=\"%2\"/></w:tcPr>")
                            .arg(twips(contentRect().width()) / table->columns()).arg(row == 0 ? QStringLiteral("E4E4E4") : QStringLiteral("FFFFFF"));
                        result += serialize(cell.begin(), cell.end()) + QStringLiteral("</w:tc>");
                    }
                    result += QStringLiteral("</w:tr>");
                }
                result += QStringLiteral("</w:tbl>");
            } else if (auto* frame = it.currentFrame()) result += serialize(frame->begin(), frame->end());
            else if (it.currentBlock().isValid()) result += paragraph(it.currentBlock());
        }
        return result;
    };
    const auto headerFooter = [&](const QString& content, bool footer, bool numbers) {
        auto templated = content;
        templated.replace(QStringLiteral("{page}"), QString(QChar(0xE000))).replace(QStringLiteral("{pages}"), QString(QChar(0xE001)));
        auto encoded = textRun(expanded(templated, settings_, 1, pageCount()));
        for (int field = 0; field < 2; ++field) {
            const auto code = field == 0 ? QStringLiteral("PAGE") : QStringLiteral("NUMPAGES");
            encoded.replace(QString(QChar(0xE000 + field)), QStringLiteral("</w:t></w:r><w:fldSimple w:instr=\"%1%2\"><w:r><w:t>%3</w:t></w:r></w:fldSimple><w:r><w:t xml:space=\"preserve\">")
                .arg(code, settings_.roman_numbers ? QStringLiteral(" \\* roman") : QString(), QString::number(field == 0 ? 1 : pageCount())));
        }
        QString body = QStringLiteral("<w:p><w:pPr><w:jc w:val=\"%1\"/></w:pPr><w:r>%2</w:r>")
            .arg(footer ? QStringLiteral("right") : QStringLiteral("left"), encoded);
        if (numbers) {
            body += QStringLiteral("<w:r><w:tab/></w:r><w:fldSimple w:instr=\"PAGE%1\"><w:r><w:t>1</w:t></w:r></w:fldSimple><w:r><w:t xml:space=\"preserve\"> / </w:t></w:r><w:fldSimple w:instr=\"NUMPAGES%1\"><w:r><w:t>%2</w:t></w:r></w:fldSimple>")
                .arg(settings_.roman_numbers ? QStringLiteral(" \\* roman") : QString(), QString::number(pageCount()));
        }
        return body + QStringLiteral("</w:p>");
    };
    const QString ns = QStringLiteral(" xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
    const QString default_header_id = relationship(QStringLiteral("header"), QStringLiteral("header1.xml"));
    const QString default_footer_id = relationship(QStringLiteral("footer"), QStringLiteral("footer1.xml"));
    addXml(parts, QStringLiteral("word/header1.xml"), QStringLiteral("<w:hdr%1>%2</w:hdr>").arg(ns, headerFooter(settings_.header, false, false)));
    addXml(parts, QStringLiteral("word/footer1.xml"), QStringLiteral("<w:ftr%1>%2</w:ftr>").arg(ns, headerFooter(settings_.footer, true, settings_.page_numbers)));
    const auto paper = paperPoints(); const auto margins = settings_.margins_mm * points_per_mm;
    QString section = QStringLiteral("<w:sectPr><w:headerReference w:type=\"default\" r:id=\"%1\"/><w:footerReference w:type=\"default\" r:id=\"%2\"/>").arg(default_header_id, default_footer_id);
    if (settings_.different_first) {
        const auto header_id = relationship(QStringLiteral("header"), QStringLiteral("header2.xml"));
        const auto footer_id = relationship(QStringLiteral("footer"), QStringLiteral("footer2.xml"));
        addXml(parts, QStringLiteral("word/header2.xml"), QStringLiteral("<w:hdr%1>%2</w:hdr>").arg(ns, headerFooter(settings_.first_header, false, false)));
        addXml(parts, QStringLiteral("word/footer2.xml"), QStringLiteral("<w:ftr%1>%2</w:ftr>").arg(ns, headerFooter(settings_.first_footer, true, settings_.page_numbers)));
        section += QStringLiteral("<w:headerReference w:type=\"first\" r:id=\"%1\"/><w:footerReference w:type=\"first\" r:id=\"%2\"/>").arg(header_id, footer_id);
    }
    section += QStringLiteral("<w:pgSz w:w=\"%1\" w:h=\"%2\" w:orient=\"%3\"/><w:pgMar w:top=\"%4\" w:right=\"%5\" w:bottom=\"%6\" w:left=\"%7\" w:header=\"%8\" w:footer=\"%9\" w:gutter=\"0\"/></w:sectPr>")
        .arg(twips(paper.width())).arg(twips(paper.height())).arg(settings_.landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"))
        .arg(twips(margins.top())).arg(twips(margins.right())).arg(twips(margins.bottom())).arg(twips(margins.left()))
        .arg(twips(margins.top() / 2)).arg(twips(margins.bottom() / 2));
    if (settings_.different_first) section.replace(QStringLiteral("</w:sectPr>"), QStringLiteral("<w:titlePg/></w:sectPr>"));
    const QString body = serialize(document_->rootFrame()->begin(), document_->rootFrame()->end());
    QString numbering = QStringLiteral("<w:numbering%1>").arg(ns);
    for (int kind = 1; kind <= 2; ++kind) {
        numbering += QStringLiteral("<w:abstractNum w:abstractNumId=\"%1\"><w:multiLevelType w:val=\"multilevel\"/>").arg(kind);
        for (int level = 0; level < 9; ++level)
            numbering += QStringLiteral("<w:lvl w:ilvl=\"%1\"><w:start w:val=\"1\"/><w:numFmt w:val=\"%2\"/><w:lvlText w:val=\"%3\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\"%4\" w:hanging=\"240\"/></w:pPr></w:lvl>")
                .arg(QString::number(level), kind == 1 ? QStringLiteral("decimal") : QStringLiteral("bullet"),
                     kind == 1 ? QStringLiteral("%") + QString::number(level + 1) + QLatin1Char('.') : QStringLiteral("•"),
                     QString::number((level + 1) * 480));
        numbering += QStringLiteral("</w:abstractNum>");
    }
    addXml(parts, QStringLiteral("word/numbering.xml"), numbering + numbering_instances + QStringLiteral("</w:numbering>"));
    relationship(QStringLiteral("numbering"), QStringLiteral("numbering.xml"));
    addXml(parts, QStringLiteral("word/document.xml"), QStringLiteral("<w:document%1 xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><w:background w:color=\"%2\"/><w:body>%3%4</w:body></w:document>")
        .arg(ns, settings_.background.name().mid(1), body, section));
    relationship(QStringLiteral("styles"), QStringLiteral("styles.xml"));
    relationship(QStringLiteral("settings"), QStringLiteral("settings.xml"));
    addXml(parts, QStringLiteral("word/settings.xml"), QStringLiteral("<w:settings%1><w:updateFields w:val=\"true\"/></w:settings>").arg(ns));
    QString styles = QStringLiteral("<w:styles%1><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"%2\" w:hAnsi=\"%2\"/><w:sz w:val=\"%3\"/></w:rPr></w:rPrDefault></w:docDefaults><w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/></w:style>")
        .arg(ns, xml(settings_.body_font)).arg(static_cast<int>(settings_.font_points * 2));
    for (int level = 1; level <= 6; ++level)
        styles += QStringLiteral("<w:style w:type=\"paragraph\" w:styleId=\"Heading%1\"><w:name w:val=\"heading %1\"/><w:basedOn w:val=\"Normal\"/><w:next w:val=\"Normal\"/><w:pPr><w:keepNext/><w:outlineLvl w:val=\"%2\"/></w:pPr></w:style>").arg(level).arg(level - 1);
    addXml(parts, QStringLiteral("word/styles.xml"), styles + QStringLiteral("</w:styles>"));
    addXml(parts, QStringLiteral("word/_rels/document.xml.rels"), QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">%1</Relationships>").arg(relationships));
    addXml(parts, QStringLiteral("docProps/core.xml"), QStringLiteral("<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>%1</dc:title><dc:creator>%2</dc:creator></cp:coreProperties>").arg(xml(settings_.title), xml(settings_.author)));
    addXml(parts, QStringLiteral("_rels/.rels"), QStringLiteral("<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"document\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/><Relationship Id=\"core\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" Target=\"docProps/core.xml\"/></Relationships>"));
    QString types = QStringLiteral("<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/><Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/><Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>");
    for (int index = 1; index <= (settings_.different_first ? 2 : 1); ++index) {
        types += QStringLiteral("<Override PartName=\"/word/header%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/><Override PartName=\"/word/footer%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>").arg(index);
    }
    types += QStringLiteral("<Override PartName=\"/word/numbering.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml\"/>");
    types += QStringLiteral("<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>");
    addXml(parts, QStringLiteral("[Content_Types].xml"), types + QStringLiteral("</Types>"));
    return finishPackage(parts, api, error);
}

} // namespace wolfmark::qt
