#include "page_preview.h"
#include "document_output.h"
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <algorithm>
#include <cmath>

namespace wolfmark::qt {
namespace { constexpr int gap = 24; }
PagePreview::PagePreview(QWidget* parent) : QAbstractScrollArea(parent) {
    setAccessibleName(QStringLiteral("Document page layout preview"));
    setAccessibleDescription(QStringLiteral("Use Reader for native text selection and accessible document text. This view previews printed pages."));
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    verticalScrollBar()->setSingleStep(40);
    horizontalScrollBar()->setSingleStep(40);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (pageChanged) pageChanged(currentPage());
    });
}
void PagePreview::setDocument(const DocumentOutput* document) {
    document_ = document;
    if (!document_) { viewport()->update(); return; }
    page_ = std::clamp(page_, 1, document_->pageCount()); relayout();
}
void PagePreview::setContinuous(bool continuous) {
    page_ = currentPage(); continuous_ = continuous; relayout(); setCurrentPage(page_);
}
int PagePreview::currentPage() const {
    if (!document_) return 1;
    if (!continuous_) return page_;
    const double stride = document_->paperPoints().height() * scale_ + gap;
    const double centre = verticalScrollBar()->value() + std::min(viewport()->height() / 2.0, stride / 2) - gap;
    return std::clamp(static_cast<int>(std::floor(centre / stride)) + 1, 1, document_->pageCount());
}
void PagePreview::setCurrentPage(int page) {
    if (!document_) return;
    page_ = std::clamp(page, 1, document_->pageCount());
    if (continuous_) verticalScrollBar()->setValue(static_cast<int>((page_ - 1) * (document_->paperPoints().height() * scale_ + gap)));
    else { verticalScrollBar()->setValue(0); viewport()->update(); }
    if (pageChanged) pageChanged(page_);
}
void PagePreview::fitToWidth() { const int page = currentPage(); fit_ = 1; relayout(); setCurrentPage(page); }
void PagePreview::fitInView() { const int page = currentPage(); fit_ = 2; relayout(); setCurrentPage(page); }
void PagePreview::setZoomFactor(double factor) { const int page = currentPage(); fit_ = 0; zoom_ = std::clamp(factor, .1, 4.0); relayout(); setCurrentPage(page); }
void PagePreview::relayout() {
    if (!document_) return;
    const auto size = document_->paperPoints();
    scale_ = fit_ == 0 ? zoom_ * logicalDpiX() / 72.0 : std::max(.05, (viewport()->width() - gap * 2) / size.width());
    if (fit_ == 2) scale_ = std::min(scale_, std::max(.05, (viewport()->height() - gap * 2) / size.height()));
    const int count = continuous_ ? document_->pageCount() : 1;
    const double height = count * (size.height() * scale_ + gap) + gap;
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setRange(0, std::max(0, static_cast<int>(std::ceil(size.width() * scale_ + gap * 2)) - viewport()->width()));
    verticalScrollBar()->setRange(0, std::max(0, static_cast<int>(std::min(2.0e9, std::ceil(height))) - viewport()->height()));
    viewport()->update();
}
void PagePreview::resizeEvent(QResizeEvent* event) { const int page = currentPage(); QAbstractScrollArea::resizeEvent(event); relayout(); setCurrentPage(page); }
void PagePreview::scrollContentsBy(int, int) { viewport()->update(); }
void PagePreview::paintEvent(QPaintEvent*) {
    painted_pages_ = 0;
    QPainter painter(viewport()); painter.fillRect(viewport()->rect(), QColor(QStringLiteral("#303030")));
    if (!document_) return;
    const auto paper = document_->paperPoints();
    const double width = paper.width() * scale_;
    const double height = paper.height() * scale_;
    const double stride = height + gap;
    const double x = std::max(static_cast<double>(gap), (viewport()->width() - width) / 2) - horizontalScrollBar()->value();
    const int first = continuous_ ? std::max(0, static_cast<int>((verticalScrollBar()->value() - gap) / stride)) : page_ - 1;
    const int last = continuous_ ? std::min(document_->pageCount() - 1, static_cast<int>((verticalScrollBar()->value() + viewport()->height()) / stride)) : first;
    for (int page = first; page <= last; ++page) {
        ++painted_pages_;
        const double y = gap + (continuous_ ? page * stride : 0) - verticalScrollBar()->value();
        painter.fillRect(QRectF(x + 3, y + 3, width, height), QColor(QStringLiteral("#181818")));
        painter.save(); painter.translate(x, y); painter.scale(scale_, scale_);
        document_->paintPage(painter, page); painter.restore();
    }
}
}
