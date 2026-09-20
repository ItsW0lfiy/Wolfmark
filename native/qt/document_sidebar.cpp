#include "document_sidebar.h"
#include "wolf_style.h"
#include "smooth_scroll_controller.h"

#include <QApplication>
#include <QBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTreeWidget>
#include <QWheelEvent>
#include <algorithm>
#include <vector>

namespace wolfmark::qt {

class SmoothTreeWidget final : public QTreeWidget {
public:
    explicit SmoothTreeWidget(QWidget* parent = nullptr)
        : QTreeWidget(parent), scrolling_(verticalScrollBar()) {
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    }

    void revealItem(QTreeWidgetItem* item, bool animate) {
        if (!item) return;
        const QRect item_rect = visualItemRect(item);
        if (!item_rect.isValid()) return;
        int destination = verticalScrollBar()->value();
        constexpr int inset = 6;
        if (item_rect.top() < inset) {
            destination += item_rect.top() - inset;
        } else if (item_rect.bottom() > viewport()->height() - inset) {
            destination += item_rect.bottom() - viewport()->height() + inset;
        }
        destination = std::clamp(destination, verticalScrollBar()->minimum(),
                                 verticalScrollBar()->maximum());
        const bool reduced = qEnvironmentVariable("WOLFMARK_REDUCED_MOTION") == QStringLiteral("1");
        if (animate && !reduced) {
            scrolling_.addWheelDistance(destination - verticalScrollBar()->value());
        } else {
            scrolling_.moveDirectlyTo(destination);
        }
    }

    [[nodiscard]] bool scrollRunning() const { return scrolling_.isRunning(); }
    [[nodiscard]] int scrollTarget() const { return scrolling_.targetValue(); }
    [[nodiscard]] qint64 firstChangeMicros() const { return scrolling_.firstChangeMicros(); }
    [[nodiscard]] QVector<int> scrollSamples() const { return scrolling_.frameValues(); }
    void cancelSmoothScroll() { scrolling_.cancel(); }

protected:
    void wheelEvent(QWheelEvent* event) override {
        if (!event->pixelDelta().isNull()) {
            scrolling_.cancel();
            QTreeWidget::wheelEvent(event);
            return;
        }
        if (event->angleDelta().y() != 0) {
            const double wheel_step = std::max(36.0,
                static_cast<double>(verticalScrollBar()->singleStep()) * 3.0);
            const double scaled = -static_cast<double>(event->angleDelta().y()) *
                                  wheel_step / 120.0;
            wheel_fraction_ += scaled;
            const double movement = std::trunc(wheel_fraction_);
            wheel_fraction_ -= movement;
            const double distance = movement == 0.0 ? std::copysign(1.0, scaled) : movement;
            if (qEnvironmentVariable("WOLFMARK_REDUCED_MOTION") == QStringLiteral("1")) {
                scrolling_.moveDirectlyTo(verticalScrollBar()->value() +
                                          static_cast<int>(std::lround(distance)));
            } else {
                scrolling_.addWheelDistance(distance);
            }
            event->accept();
            return;
        }
        QTreeWidget::wheelEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        scrolling_.cancel();
        QTreeWidget::mousePressEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        scrolling_.cancel();
        QTreeWidget::keyPressEvent(event);
    }

private:
    SmoothScrollController scrolling_;
    double wheel_fraction_ = 0.0;
};

DocumentSidebar::DocumentSidebar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("documentSidebar"));
    setAccessibleName(QStringLiteral("Document navigation"));
    setAttribute(Qt::WA_StyledBackground);
    setFixedWidth(style::metric::sidebar_width);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 24, 18, 18);
    layout->setSpacing(8);
    auto* identity = new QHBoxLayout;
    auto* symbol = new QLabel;
    symbol->setPixmap(QApplication::windowIcon().pixmap(24, 24));
    auto* name = new QLabel(QStringLiteral("Wolfmark"));
    name->setObjectName(QStringLiteral("sidebarIdentity"));
    identity->addWidget(symbol);
    identity->addSpacing(6);
    identity->addWidget(name);
    identity->addStretch();
    layout->addLayout(identity);
    layout->addSpacing(24);
    auto* open_button = new QPushButton(QStringLiteral("Open document"));
    open_button->setMinimumHeight(36);
    open_button->setToolTip(QStringLiteral("Open Markdown or text file (Ctrl+O)"));
    connect(open_button, &QPushButton::clicked, this, [this] { if (open) open(); });
    layout->addWidget(open_button);
    reload_ = new QPushButton(QStringLiteral("Reload"));
    reload_->setMinimumHeight(36);
    reload_->setEnabled(false);
    reload_->setToolTip(QStringLiteral("Reload current document (F5)"));
    connect(reload_, &QPushButton::clicked, this, [this] { if (reload) reload(); });
    layout->addWidget(reload_);
    layout->addSpacing(28);
    auto* label = new QLabel(QStringLiteral("OPEN DOCUMENTS"));
    label->setObjectName(QStringLiteral("sidebarSection"));
    layout->addWidget(label);
    documents_ = new SmoothTreeWidget;
    documents_->setObjectName(QStringLiteral("openDocuments"));
    documents_->setAccessibleName(QStringLiteral("Open documents"));
    documents_->setHeaderHidden(true);
    documents_->setColumnCount(2);
    documents_->setRootIsDecorated(false);
    documents_->setIndentation(0);
    documents_->setUniformRowHeights(true);
    documents_->setTextElideMode(Qt::ElideMiddle);
    documents_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    documents_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    documents_->setFrameShape(QFrame::NoFrame);
    documents_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    documents_->header()->setStretchLastSection(false);
    documents_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    documents_->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    documents_->setColumnWidth(1, 28);
    connect(documents_, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int column) {
        if (!item) return;
        const int index = item->data(0, Qt::UserRole).toInt();
        if (column == 1) {
            if (close_document) close_document(index);
        } else if (activate_document) {
            activate_document(index);
        }
    });
    connect(documents_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* item, int) {
        if (activate_document && item)
            activate_document(item->data(0, Qt::UserRole).toInt());
    });
    layout->addWidget(documents_);
    layout->addSpacing(20);
    auto* outline_label = new QLabel(QStringLiteral("OUTLINE"));
    outline_label->setObjectName(QStringLiteral("sidebarSection"));
    layout->addWidget(outline_label);
    outline_ = new SmoothTreeWidget;
    outline_->setObjectName(QStringLiteral("documentOutline"));
    outline_->setAccessibleName(QStringLiteral("Document heading outline"));
    outline_->setHeaderHidden(true);
    outline_->setRootIsDecorated(false);
    outline_->setIndentation(12);
    outline_->setUniformRowHeights(true);
    outline_->setTextElideMode(Qt::ElideRight);
    outline_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outline_->setFrameShape(QFrame::NoFrame);
    outline_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    const auto activate = [this](QTreeWidgetItem* item) { activateOutlineItem(item); };
    connect(outline_, &QTreeWidget::itemClicked, this, activate);
    connect(outline_, &QTreeWidget::itemActivated, this, activate);
    layout->addWidget(outline_, 1);
}

void DocumentSidebar::setDocuments(const QStringList& filenames, int active_index) {
    documents_->clear();
    for (int index = 0; index < filenames.size(); ++index) {
        auto* item = new QTreeWidgetItem(documents_);
        item->setText(0, filenames.at(index));
        item->setText(1, QStringLiteral("×"));
        item->setTextAlignment(1, Qt::AlignCenter);
        item->setToolTip(0, filenames.at(index));
        item->setToolTip(1, QStringLiteral("Close document"));
        item->setData(0, Qt::UserRole, index);
        if (index == active_index) documents_->setCurrentItem(item);
    }
    const int rows = std::clamp(static_cast<int>(filenames.size()), 1, 4);
    documents_->setFixedHeight(rows * 34 + 4);
    reload_->setEnabled(active_index >= 0);
}

void DocumentSidebar::setOutline(const QJsonArray& outline) {
    outline_->clear();
    outline_items_.clear();
    std::vector<std::pair<int, QTreeWidgetItem*>> parents;
    for (const auto& value : outline) {
        const auto entry = value.toObject();
        const int level = entry.value("level").toInt();
        while (!parents.empty() && parents.back().first >= level) parents.pop_back();
        auto* item = parents.empty() ? new QTreeWidgetItem(outline_) :
                                      new QTreeWidgetItem(parents.back().second);
        item->setText(0, entry.value("title").toString());
        item->setToolTip(0, item->text(0));
        item->setData(0, Qt::UserRole, entry.value("anchor").toString());
        outline_items_.insert(entry.value("anchor").toString(), item);
        parents.emplace_back(level, item);
    }
    outline_->expandAll();
}

bool DocumentSidebar::revealOutlineAnchor(const QString& anchor, bool animate) {
    const auto found = outline_items_.constFind(anchor);
    if (found == outline_items_.cend()) return false;
    outline_->revealItem(found.value(), animate);
    return true;
}

bool DocumentSidebar::activateOutlineAnchorForTest(const QString& anchor) {
    const auto found = outline_items_.constFind(anchor);
    if (found == outline_items_.cend()) return false;
    activateOutlineItem(found.value());
    return true;
}

void DocumentSidebar::activateOutlineItem(QTreeWidgetItem* item) {
    if (!item) return;
    outline_->revealItem(item, false);
    if (navigate) navigate(item->data(0, Qt::UserRole).toString());
}

bool DocumentSidebar::outlineScrollRunning() const {
    return outline_->scrollRunning();
}

int DocumentSidebar::outlineScrollValue() const {
    return outline_->verticalScrollBar()->value();
}

int DocumentSidebar::outlineScrollTarget() const {
    return outline_->scrollTarget();
}

qint64 DocumentSidebar::outlineFirstChangeMicros() const {
    return outline_->firstChangeMicros();
}

QVector<int> DocumentSidebar::outlineScrollSamples() const {
    return outline_->scrollSamples();
}

bool DocumentSidebar::outlineAnchorVisible(const QString& anchor) const {
    const auto found = outline_items_.constFind(anchor);
    if (found == outline_items_.cend()) return false;
    const auto bounds = outline_->visualItemRect(found.value());
    return bounds.isValid() && bounds.top() >= 0 &&
           bounds.bottom() <= outline_->viewport()->height();
}

bool DocumentSidebar::testPartialOutlineWheel() {
    if (outline_->verticalScrollBar()->maximum() <= 0) return false;
    outline_->cancelSmoothScroll();
    outline_->verticalScrollBar()->setValue(0);
    const QPointF position(8, 8);
    QWheelEvent partial(position, outline_->viewport()->mapToGlobal(position.toPoint()),
                        QPoint(), QPoint(0, -30), Qt::NoButton, Qt::NoModifier,
                        Qt::NoScrollPhase, false);
    QApplication::sendEvent(outline_->viewport(), &partial);
    return partial.isAccepted() && outline_->scrollRunning() && outline_->scrollTarget() > 0;
}

void DocumentSidebar::cancelOutlineScroll() {
    outline_->cancelSmoothScroll();
}
} // namespace wolfmark::qt
