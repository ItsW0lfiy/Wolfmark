#pragma once
#include <QAbstractScrollArea>
#include <functional>

namespace wolfmark::qt {
class DocumentOutput;

// Page canvas only; all text shaping, pagination, and painting remain native
// QTextDocument work shared with PDF/printing. No per-page raster/picture cache.
class PagePreview final : public QAbstractScrollArea {
public:
    explicit PagePreview(QWidget* parent = nullptr);
    void setDocument(const DocumentOutput* document);
    void setContinuous(bool continuous);
    void setCurrentPage(int page);
    [[nodiscard]] int currentPage() const;
    [[nodiscard]] int paintedPages() const { return painted_pages_; }
    void fitToWidth();
    void fitInView();
    void setZoomFactor(double factor);
    std::function<void(int)> pageChanged;

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void scrollContentsBy(int, int) override;

private:
    void relayout();
    const DocumentOutput* document_ = nullptr;
    bool continuous_ = true;
    int page_ = 1;
    int fit_ = 1;
    double zoom_ = 1;
    double scale_ = 1;
    int painted_pages_ = 0;
};
}
