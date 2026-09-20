#pragma once

#include <QJsonArray>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QTreeWidgetItem;

namespace wolfmark::qt {
class SmoothTreeWidget;
class DocumentSidebar final : public QWidget {
public:
    explicit DocumentSidebar(QWidget* parent = nullptr);
    void setDocuments(const QStringList& filenames, int active_index);
    void setOutline(const QJsonArray& outline);
    bool revealOutlineAnchor(const QString& anchor, bool animate = true);
    bool activateOutlineAnchorForTest(const QString& anchor);
    [[nodiscard]] bool outlineScrollRunning() const;
    [[nodiscard]] int outlineScrollValue() const;
    [[nodiscard]] int outlineScrollTarget() const;
    [[nodiscard]] qint64 outlineFirstChangeMicros() const;
    [[nodiscard]] QVector<int> outlineScrollSamples() const;
    [[nodiscard]] bool outlineAnchorVisible(const QString& anchor) const;
    [[nodiscard]] bool testPartialOutlineWheel();
    void cancelOutlineScroll();
    std::function<void()> open;
    std::function<void()> reload;
    std::function<void(const QString&)> navigate;
    std::function<void(int)> activate_document;
    std::function<void(int)> close_document;

private:
    void activateOutlineItem(QTreeWidgetItem* item);

    QPushButton* reload_;
    SmoothTreeWidget* documents_;
    SmoothTreeWidget* outline_;
    QHash<QString, QTreeWidgetItem*> outline_items_;
};
} // namespace wolfmark::qt
