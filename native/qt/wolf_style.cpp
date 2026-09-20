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
    palette.setColor(QPalette::Accent, QColor(colour::crimson));
    palette.setColor(QPalette::Highlight, QColor(colour::selection));
    palette.setColor(QPalette::HighlightedText, QColor(colour::bright));
    palette.setColor(QPalette::Link, QColor(colour::crimson_hover));
    palette.setColor(QPalette::LinkVisited, QColor(colour::silver));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(colour::muted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(colour::muted));
    application.setPalette(palette);
    application.setStyleSheet(QStringLiteral(R"(
        QWidget { color: #e9e7e6; font-family: "Segoe UI Variable Text", "Segoe UI"; font-size: 10pt; }
        QWidget#appWindow { background: #0b0b0b; }
        QWidget#contentShell { background: #121212; }
        QLabel { background: transparent; }

        #titleBar { background: #121212; border-bottom: 1px solid #2c2c2c; }
        #documentSidebar { background: #161616; border-right: 1px solid #303030; }
        #brandSymbol { background: #282828; border: 1px solid #3a3a3a; border-radius: 7px; }
        #sidebarIdentity { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 18px; font-weight: 600; }
        #sidebarSection { color: #8a8785; font-size: 10px; font-weight: 600; letter-spacing: 1px; padding: 0 7px; }
        #documentSidebar QPushButton { background: transparent; border-color: transparent; text-align: left; min-height: 34px; padding-left: 11px; }
        #documentSidebar QPushButton:hover { background: #252525; }

        QTreeWidget { background: transparent; border: none; color: #bbb8b6; font-size: 13px; outline: 0; }
        QTreeWidget::item { padding: 7px 6px; border: 1px solid transparent; border-radius: 6px; }
        QTreeWidget::item:hover { background: #252525; color: #f4f1ef; }
        QTreeWidget::item:selected { background: #302629; color: #f4f1ef; border-left: 3px solid #d62d4e; }
        QTreeWidget::item:focus { border-color: #d62d4e; }
        #openDocuments { background: #1b1b1b; border: 1px solid #292929; border-radius: 8px; padding: 3px; }
        #openDocuments::item { padding: 6px 5px; }
        #openDocuments::item:selected { background: #342326; color: #f4f1ef; }

        QPushButton { background: #1d1d1d; border: 1px solid #353535; border-radius: 6px; padding: 5px 11px; color: #d0cdca; }
        QPushButton:hover { background: #292929; border-color: #505050; color: #f4f1ef; }
        QPushButton:pressed { background: #342326; border-color: #d62d4e; }
        QPushButton:focus { border: 1px solid #d62d4e; }
        QPushButton:disabled { background: #171717; border-color: #242424; color: #656260; }
        QPushButton[primary="true"] { background: #d62d4e; border-color: #d62d4e; color: #ffffff; font-weight: 600; }
        QPushButton[primary="true"]:hover { background: #ea3d5f; border-color: #ea3d5f; }
        QPushButton[primary="true"]:pressed { background: #b8213f; border-color: #b8213f; }
        QPushButton[nav="true"] { background: transparent; border-color: transparent; border-radius: 7px; text-align: left; padding: 9px 12px; }
        QPushButton[nav="true"]:checked { background: #302629; border-left: 3px solid #d62d4e; color: #f4f1ef; padding-left: 9px; }
        QPushButton[titleTool="true"] { background: transparent; border-color: transparent; padding: 3px 7px; }
        QPushButton[titleTool="true"]:hover { background: #292929; border-color: #3d3d3d; }
        QPushButton[titleTool="true"]:pressed { background: #342326; border-color: #d62d4e; }

        #documentTitle { color: #c9c6c3; font-size: 13px; font-weight: 500; }
        #zoomValue { color: #9c9895; font-size: 12px; background: transparent; border-color: transparent; }
        #documentStack { background: #121212; }
        #emptyTitle { font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 23px; font-weight: 600; color: #ece9e6; }
        #emptyOpen { background: #d62d4e; border-color: #d62d4e; color: #ffffff; padding: 7px 14px; }
        #emptyOpen:hover { background: #ea3d5f; border-color: #ea3d5f; }
        #emptyHint { color: #8a8785; margin-top: 4px; }
        #diagnosticsBar { background: #121212; border-top: 1px solid #303030; color: #777472; font-size: 11px; }

        QTextEdit { background: #121212; border: 0; selection-background-color: #69313c; selection-color: #ffffff; }
        QScrollBar:vertical { background: #121212; width: 12px; margin: 0; }
        QScrollBar::handle:vertical { background: #3c3c3c; min-height: 32px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #5c4a4e; }
        QScrollBar:horizontal { background: #121212; height: 12px; margin: 0; }
        QScrollBar::handle:horizontal { background: #3c3c3c; min-width: 32px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: #5c4a4e; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        QToolTip { background: #242424; color: #e9e7e6; border: 1px solid #505050; padding: 5px 7px; }
        QMenu { background: #1b1b1b; color: #e9e7e6; border: 1px solid #3b3b3b; padding: 6px; }
        QMenu::item { border-radius: 5px; padding: 7px 25px 7px 11px; }
        QMenu::item:selected { background: #342326; color: #ffffff; }
        QMenu::separator { height: 1px; background: #343434; margin: 6px 9px; }

        QDialog { background: #101010; }
        #settingsDialog { background: #101010; }
        #settingsNavigation { background: #161616; border-right: 1px solid #303030; }
        #settingsContent { background: #121212; }
        #settingsBrand { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 20px; font-weight: 600; }
        #settingsTitle { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 25px; font-weight: 600; }
        #settingsSectionTitle { color: #e9e7e6; font-size: 14px; font-weight: 600; }
        #settingsHint { color: #9c9895; }
        #settingsResult { color: #d0cdca; padding: 8px 0; }
        #settingsCard { background: #1b1b1b; border: 1px solid #363636; border-radius: 9px; }

        QCheckBox { color: #d0cdca; spacing: 9px; min-height: 25px; }
        QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #595959; border-radius: 5px; background: #1a1a1a; }
        QCheckBox::indicator:hover { border-color: #d62d4e; }
        QCheckBox::indicator:checked { background: #d62d4e; border-color: #ea3d5f; }
        QCheckBox:focus { color: #ffffff; }
        QLineEdit, QComboBox { background: #1b1b1b; border: 1px solid #404040; border-radius: 6px; padding: 7px 9px; color: #e9e7e6; selection-background-color: #69313c; selection-color: #ffffff; }
        QLineEdit:focus, QComboBox:focus { border-color: #d62d4e; }
        QSlider::groove:horizontal { height: 5px; border-radius: 2px; background: #343434; }
        QSlider::sub-page:horizontal { background: #d62d4e; border-radius: 2px; }
        QSlider::handle:horizontal { width: 15px; margin: -5px 0; border-radius: 7px; background: #f0edeb; border: 1px solid #d62d4e; }
        QTabBar::tab { background: #191919; color: #aaa6a3; border: 1px solid #303030; padding: 7px 12px; }
        QTabBar::tab:selected { background: #302629; color: #ffffff; border-bottom: 2px solid #d62d4e; }

        #updateBanner { background: #1d1719; border-bottom: 1px solid #4a2930; }
        #updateLabel { color: #e1dedd; font-weight: 500; }
    )"));
}

} // namespace wolfmark::style
