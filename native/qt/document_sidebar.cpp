#include "document_sidebar.h"
#include "wolf_style.h"
#include "smooth_scroll_controller.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QColor>
#include <QCoreApplication>
#include <QHeaderView>
#include <QIcon>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QKeySequence>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QDir>
#include <QPushButton>
#include <QPixmap>
#include <QScrollBar>
#include <QSizePolicy>
#include <QToolButton>
#include <QSet>
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
            // addWheelDistance changes the *existing target*, not the current
            // scrollbar value. A second outline reveal must supersede the
            // first destination even when the first animation has not painted.
            const int active_target = scrolling_.isRunning()
                ? scrolling_.targetValue() : verticalScrollBar()->value();
            scrolling_.addWheelDistance(destination - active_target);
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
    void drawBranches(QPainter* painter, const QRect& rect,
                      const QModelIndex& index) const override {
        // Native tree navigation stays intact. The outline alone gets subtle
        // structural guides; the open-documents list is intentionally flat.
        if (objectName() == QStringLiteral("documentOutline") && index.parent().isValid()) {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->setPen(QPen(QColor(QStringLiteral("#49464d")), 1));
            const int step = indentation();
            const int center_x = rect.right() - step / 2;
            const int center_y = rect.center().y();
            if (center_x >= rect.left()) {
                painter->drawLine(center_x, rect.top(), center_x, center_y);
                if (index.row() + 1 < model()->rowCount(index.parent()))
                    painter->drawLine(center_x, center_y, center_x, rect.bottom());
                painter->drawLine(center_x, center_y, rect.right(), center_y);
            }
            // Carry the ancestor guide through grandchildren only while that
            // ancestor has a following sibling; no dangling guide below a leaf.
            auto ancestor = index.parent();
            int ancestor_x = center_x - step;
            while (ancestor.parent().isValid() && ancestor_x >= rect.left()) {
                if (ancestor.row() + 1 < model()->rowCount(ancestor.parent()))
                    painter->drawLine(ancestor_x, rect.top(), ancestor_x, rect.bottom());
                ancestor = ancestor.parent();
                ancestor_x -= step;
            }
            painter->restore();
        }
        QTreeWidget::drawBranches(painter, rect, index);
    }

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
                const auto destination = std::clamp(
                    static_cast<double>(verticalScrollBar()->value()) + distance,
                    static_cast<double>(verticalScrollBar()->minimum()),
                    static_cast<double>(verticalScrollBar()->maximum()));
                scrolling_.moveDirectlyTo(static_cast<int>(std::lround(destination)));
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
    layout->setContentsMargins(22, 14, 18, 14);
    layout->setSpacing(8);
    // Branding is now above the sidebar in the single global titlebar.
    // Avoid the duplicate paw and product name from the legacy side rail.
    layout->addSpacing(3);
    auto* open_row = new QHBoxLayout;
    open_row->setSpacing(0);
    auto* open_button = new QPushButton(QStringLiteral("Open document"));
    open_button->setObjectName(QStringLiteral("sidebarOpenButton"));
    QPixmap folder_icon(20, 20);
    folder_icon.fill(Qt::transparent);
    {
        QPainter painter(&folder_icon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#f8eef1")), 1.5));
        painter.drawLine(QPointF(2.5, 6.5), QPointF(7.0, 6.5));
        painter.drawLine(QPointF(7.0, 6.5), QPointF(9.5, 8.0));
        painter.drawRoundedRect(QRectF(2.5, 7.5, 15.0, 10.0), 2.0, 2.0);
    }
    open_button->setIcon(QIcon(folder_icon));
    open_button->setIconSize(QSize(20, 20));
    open_button->setFixedHeight(40);
    open_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    open_button->setToolTip(QStringLiteral("Open Markdown or text file (Ctrl+O)"));
    connect(open_button, &QPushButton::clicked, this, [this] { if (open) open(); });
    open_row->addWidget(open_button, 1);
    layout->addLayout(open_row);
    layout->addSpacing(22);
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
    layout->addSpacing(14);
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

    auto* footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("sidebarFooter"));
    footer->setAttribute(Qt::WA_StyledBackground);
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(5, 12, 0, 2);
    footer_layout->setSpacing(18);
    auto* footer_symbol = new QLabel;
    QIcon footer_icon(QDir::current().filePath(QStringLiteral("assets/icons/wolfmark-32x32.png")));
    if (footer_icon.isNull()) footer_icon = QApplication::windowIcon();
    footer_symbol->setPixmap(footer_icon.pixmap(36, 36));
    footer_symbol->setFixedSize(42, 42);
    footer_symbol->setAlignment(Qt::AlignCenter);
    footer_layout->addWidget(footer_symbol);
    auto* footer_copy = new QVBoxLayout;
    footer_copy->setSpacing(1);
    auto* footer_name = new QLabel(QStringLiteral("Wolfmark"));
    footer_name->setObjectName(QStringLiteral("sidebarFooterName"));
    auto* footer_version = new QLabel(QCoreApplication::applicationVersion());
    footer_version->setObjectName(QStringLiteral("sidebarFooterVersion"));
    footer_copy->addWidget(footer_name);
    footer_copy->addWidget(footer_version);
    footer_layout->addLayout(footer_copy);
    footer_layout->addStretch();
    layout->addWidget(footer);
}

namespace {
QString documentIconFilename(QString label) {
    // The sidebar appends a dirty marker to the displayed label. It is not
    // part of the filename and must not turn an edited .txt into a .md icon.
    if (label.endsWith(QStringLiteral(" *"))) label.chop(2);
    return label.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
        ? QStringLiteral("wolfmark-text.ico") : QStringLiteral("wolfmark-markdown.ico");
}

QIcon documentIcon(const QString& name) {
    // Reuse the approved application file icons. Never redraw or recolor the
    // locked wolfmark-markdown / wolfmark-text artwork for navigation states.
    const auto filename = documentIconFilename(name);
    for (const auto& root : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
        QIcon icon(QDir(root).filePath(QStringLiteral("assets/icons/") + filename));
        // Qt builds without an ICO decoder can return an icon handle which
        // still produces an empty pixmap. Always validate usable pixels.
        if (!icon.isNull() && !icon.pixmap(20, 20).isNull()) return icon;
    }
    return QApplication::windowIcon();
}
} // namespace

bool DocumentSidebar::testDocumentIconMapping() const {
    return documentIconFilename(QStringLiteral("notes.txt")) == QStringLiteral("wolfmark-text.ico") &&
           documentIconFilename(QStringLiteral("notes.TXT *")) == QStringLiteral("wolfmark-text.ico") &&
           documentIconFilename(QStringLiteral("notes.md *")) == QStringLiteral("wolfmark-markdown.ico") &&
           documentIconFilename(QStringLiteral("notes.markdown *")) == QStringLiteral("wolfmark-markdown.ico");
}

bool DocumentSidebar::testStableDocumentRows() const {
    DocumentSidebar probe;
    probe.setDocuments({QStringLiteral("notes.txt")}, 0);
    auto* original = probe.documents_->topLevelItem(0);
    probe.setDocuments({QStringLiteral("notes.txt *")}, 0);
    const bool dirty_stable = original == probe.documents_->topLevelItem(0) &&
                              probe.documents_->currentItem() == original;
    probe.setDocuments({QStringLiteral("notes.txt *"), QStringLiteral("other.md")}, 1);
    const bool added_stable = original == probe.documents_->topLevelItem(0) &&
                              probe.documents_->currentItem() == probe.documents_->topLevelItem(1);
    probe.setDocuments({QStringLiteral("notes.txt")}, 0);
    return dirty_stable && added_stable &&
           probe.documents_->topLevelItemCount() == 1 &&
           probe.documents_->topLevelItem(0) == original &&
           probe.documents_->currentItem() == original;
}

void DocumentSidebar::setDocuments(const QStringList& filenames, int active_index) {
    // Preserve native row identity, keyboard focus and the scroll position on
    // dirty-marker updates. Clearing and rebuilding the tree on the first edit
    // caused needless selection/layout churn for otherwise unchanged files.
    while (documents_->topLevelItemCount() > filenames.size())
        delete documents_->takeTopLevelItem(documents_->topLevelItemCount() - 1);
    for (int index = 0; index < filenames.size(); ++index) {
        auto* item = index < documents_->topLevelItemCount()
            ? documents_->topLevelItem(index) : new QTreeWidgetItem(documents_);
        const auto& label = filenames.at(index);
        if (item->text(0) != label) {
            item->setText(0, label);
            item->setIcon(0, documentIcon(label));
            item->setToolTip(0, label);
        }
        item->setText(1, QStringLiteral("×"));
        item->setTextAlignment(1, Qt::AlignCenter);
        item->setToolTip(1, QStringLiteral("Close document"));
        item->setData(0, Qt::UserRole, index);
        if (index == active_index && documents_->currentItem() != item) {
            // Selection already paints the crimson left marker. Retain the
            // approved file icon instead of substituting a color swatch.
            documents_->setCurrentItem(item);
        }
    }
    if (active_index < 0) documents_->clearSelection();
    const int rows = std::clamp(static_cast<int>(filenames.size()), 1, 4);
    documents_->setFixedHeight(rows * 34 + 4);
}

void DocumentSidebar::resetOutlineContext() {
    // Collapsed headings and scroll offsets must not leak between different files.
    outline_->clear();
    outline_items_.clear();
    displayed_outline_ = {};
}

void DocumentSidebar::setOutline(const QJsonArray& outline) {
    // Switching modes or refreshing status must not reset the user's outline view.
    if (outline == displayed_outline_) return;
    QSet<QString> collapsed;
    for (auto it = outline_items_.cbegin(); it != outline_items_.cend(); ++it) {
        if (it.value()->childCount() > 0 && !it.value()->isExpanded()) collapsed.insert(it.key());
    }
    const int old_scroll = outline_->verticalScrollBar()->value();
    outline_->clear();
    outline_items_.clear();
    displayed_outline_ = outline;
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
    for (const auto& anchor : collapsed) {
        const auto item = outline_items_.constFind(anchor);
        if (item != outline_items_.cend()) item.value()->setExpanded(false);
    }
    outline_->verticalScrollBar()->setValue(old_scroll);
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
