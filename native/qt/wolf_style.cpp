#include "wolf_style.h"

#include <QApplication>
#include <QColor>
#include <QPalette>

namespace wolfmark::style {

void apply(QApplication& application) {
    application.setStyle(QStringLiteral("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(colour::background));
    palette.setColor(QPalette::WindowText, QColor(colour::text));
    palette.setColor(QPalette::Base, QColor(colour::document));
    palette.setColor(QPalette::AlternateBase, QColor(colour::surface));
    palette.setColor(QPalette::Text, QColor(colour::text));
    palette.setColor(QPalette::Button, QColor(colour::surface));
    palette.setColor(QPalette::ButtonText, QColor(colour::text));
    palette.setColor(QPalette::Accent, QColor(colour::silver));
    palette.setColor(QPalette::Highlight, QColor(colour::selection));
    palette.setColor(QPalette::HighlightedText, QColor(colour::bright));
    palette.setColor(QPalette::Link, QColor(colour::silver));
    palette.setColor(QPalette::LinkVisited, QColor(colour::secondary));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(colour::muted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(colour::muted));
    application.setPalette(palette);
    application.setStyleSheet(QStringLiteral(R"(
        QWidget { color: #e8e8e8; font-family: "Segoe UI Variable Text", "Segoe UI"; }
        QLabel { background: transparent; }
        #titleBar { background: #101010; border-bottom: 1px solid #222222; }
        #documentSidebar { background: #101010; border-right: 1px solid #262626; }
        #sidebarIdentity { color: #e8e8e8; font-size: 17px; font-weight: 600; }
        #sidebarSection { color: #858585; font-size: 11px; font-weight: 600; padding: 0 6px; }
        #documentSidebar QPushButton { text-align: left; padding-left: 10px; font-size: 14px; }
        QTreeWidget { background: transparent; border: none; color: #aeaeae; font-size: 13px; outline: 0; }
        QTreeWidget::item { padding: 7px 4px; border: 1px solid transparent; border-radius: 4px; }
        QTreeWidget::item:hover { background: #202020; color: #e8e8e8; }
        QTreeWidget::item:selected { background: #2c2c2c; color: #eeeeee; }
        QTreeWidget::item:focus { border-color: #aaaaaa; }
        #openDocuments { background: #151515; border-radius: 6px; padding: 2px; }
        #openDocuments::item { padding: 6px 4px; }
        #openDocuments::item:selected { background: #303030; color: #f0f0f0; }
        QPushButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 3px 8px; color: #b0b0b0; }
        QPushButton:hover { background: #242424; color: #f0f0f0; }
        QPushButton:pressed { background: #303030; }
        QPushButton:focus { border: 1px solid #c8c8c8; }
        QPushButton:disabled { color: #525252; }
        #documentTitle { color: #b8b8b8; font-size: 13px; font-weight: 500; }
        #zoomValue { color: #929292; font-size: 12px; }
        #documentStack { background: #0e0e0e; }
        #emptyTitle { font-size: 20px; font-weight: 500; color: #d8d8d8; }
        #emptyOpen { background: #1c1c1c; color: #dedede; padding: 5px 12px; }
        #emptyOpen:hover { background: #303030; color: #f0f0f0; }
        #emptyHint { color: #858585; margin-top: 4px; }
        #diagnosticsBar { background: #101010; border-top: 1px solid #282828; color: #777777; font-size: 11px; }
        QTextEdit { background: #0e0e0e; border: 0; selection-background-color: #484848; selection-color: #f0f0f0; }
        QScrollBar:vertical { background: #0e0e0e; width: 12px; margin: 0; }
        QScrollBar::handle:vertical { background: #343434; min-height: 30px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #646464; }
        QScrollBar:horizontal { background: #0e0e0e; height: 12px; margin: 0; }
        QScrollBar::handle:horizontal { background: #343434; min-width: 30px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: #646464; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QToolTip { background: #1c1c1c; color: #e8e8e8; border: 1px solid #464646; padding: 4px; }
        QMenu { background: #141414; color: #e8e8e8; border: 1px solid #303030; padding: 5px; }
        QMenu::item { border-radius: 4px; padding: 6px 24px 6px 10px; }
        QMenu::item:selected { background: #303030; color: #f0f0f0; }
        QMenu::separator { height: 1px; background: #303030; margin: 5px 8px; }
        #updateBanner { background: #171717; border-bottom: 1px solid #303030; }
        #updateLabel { color: #d2d2d2; font-weight: 500; }
        #settingsTitle { color: #eeeeee; font-size: 18px; font-weight: 600; }
        #settingsHint { color: #929292; }
        #settingsResult { color: #c8c8c8; padding: 6px 0; }
        QDialog { background: #101010; }
        QCheckBox { color: #d0d0d0; spacing: 8px; }
    )"));
}

} // namespace wolfmark::style
