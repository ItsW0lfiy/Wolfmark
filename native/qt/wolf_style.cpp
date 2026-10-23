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
        QWidget { color: #ebe9e8; font-family: "Segoe UI Variable Text", "Segoe UI"; font-size: 10pt; }
        QWidget#appWindow { background: #101012; }
        QWidget#contentShell { background: #101012; }
        QLabel { background: transparent; }

        #titleBar { background: #101012; border-bottom: 1px solid #2c2c2c; }
        #documentSidebar { background: #19191c; border-right: 1px solid #34343a; }
        #brandSymbol { background: #282828; border: 1px solid #3a3a3a; border-radius: 7px; }
        #sidebarIdentity { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 18px; font-weight: 600; }
        #sidebarSection { color: #939098; font-size: 10px; font-weight: 600; letter-spacing: 1px; padding: 0 7px; }
        #documentSidebar QPushButton { background: transparent; border-color: transparent; text-align: left; min-height: 34px; padding-left: 11px; }
        #documentSidebar QPushButton:hover { background: #252525; }

        QTreeWidget { background: transparent; border: none; color: #bcb9bb; font-size: 13px; outline: 0; }
        QTreeWidget::item { padding: 7px 6px; border: 1px solid transparent; border-radius: 6px; }
        QTreeWidget::item:hover { background: #252525; color: #f5f3f3; }
        QTreeWidget::item:selected { background: #302629; color: #f5f3f3; border-left: 3px solid #c52a50; }
        QTreeWidget::item:focus { border-color: #c52a50; }
        #openDocuments { background: #202024; border: 1px solid #303036; border-radius: 8px; padding: 3px; }
        #openDocuments::item { padding: 6px 5px; }
        #openDocuments::item:selected { background: #3a2029; color: #f5f3f3; }

        QPushButton { background: #1d1d1d; border: 1px solid #353535; border-radius: 6px; padding: 5px 11px; color: #d5d2d3; }
        QPushButton:hover { background: #303036; border-color: #55555c; color: #f5f3f3; }
        QPushButton:pressed { background: #3a2029; border-color: #c52a50; }
        QPushButton:focus { border: 1px solid #c52a50; }
        QPushButton:disabled { background: #171717; border-color: #242424; color: #656260; }
        QPushButton[primary="true"] { background: #c52a50; border-color: #c52a50; color: #ffffff; font-weight: 600; }
        QPushButton[primary="true"]:hover { background: #d83a61; border-color: #d83a61; }
        QPushButton[primary="true"]:pressed { background: #a51e41; border-color: #a51e41; }
        QPushButton[nav="true"] { background: transparent; border-color: transparent; border-radius: 7px; text-align: left; padding: 9px 12px; }
        QPushButton[nav="true"]:checked { background: #302629; border-left: 3px solid #c52a50; color: #f5f3f3; padding-left: 9px; }
        QPushButton[titleTool="true"] { background: transparent; border-color: transparent; padding: 3px 7px; }
        QPushButton[titleTool="true"]:hover { background: #303036; border-color: #3d3d3d; }
        QPushButton[titleTool="true"]:pressed { background: #3a2029; border-color: #c52a50; }

        #documentTitle { color: #c9c6c3; font-size: 13px; font-weight: 500; }
        #zoomValue { color: #9c9895; font-size: 12px; background: transparent; border-color: transparent; }
        #documentStack { background: #101012; }
        #emptyTitle { font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 23px; font-weight: 600; color: #ece9e6; }
        #emptyOpen { background: #c52a50; border-color: #c52a50; color: #ffffff; padding: 7px 14px; }
        #emptyOpen:hover { background: #d83a61; border-color: #d83a61; }
        #emptyHint { color: #939098; margin-top: 4px; }
        #diagnosticsBar { background: #101012; border-top: 1px solid #34343a; color: #777472; font-size: 11px; }

        QTextEdit { background: #101012; border: 0; selection-background-color: #653040; selection-color: #ffffff; }
        QScrollBar:vertical { background: #101012; width: 12px; margin: 0; }
        QScrollBar::handle:vertical { background: #3c3c3c; min-height: 32px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #5c4a4e; }
        QScrollBar:horizontal { background: #101012; height: 12px; margin: 0; }
        QScrollBar::handle:horizontal { background: #3c3c3c; min-width: 32px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: #5c4a4e; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        QToolTip { background: #242424; color: #ebe9e8; border: 1px solid #55555c; padding: 5px 7px; }
        QMenu { background: #202024; color: #ebe9e8; border: 1px solid #3b3b3b; padding: 6px; }
        QMenu::item { border-radius: 5px; padding: 7px 25px 7px 11px; }
        QMenu::item:selected { background: #3a2029; color: #ffffff; }
        QMenu::separator { height: 1px; background: #34343a; margin: 6px 9px; }

        QDialog { background: #151518; }
        #settingsDialog { background: #151518; }
        #settingsNavigation { background: #19191c; border-right: 1px solid #34343a; }
        #settingsContent { background: #101012; }
        #settingsBrand { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 20px; font-weight: 600; }
        #settingsTitle { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 25px; font-weight: 600; }
        #settingsSectionTitle { color: #ebe9e8; font-size: 14px; font-weight: 600; }
        #settingsHint { color: #9c9895; }
        #settingsResult { color: #d5d2d3; padding: 8px 0; }
        #settingsCard { background: #202024; border: 1px solid #363636; border-radius: 9px; }

        QCheckBox { color: #d5d2d3; spacing: 9px; min-height: 25px; }
        QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #595959; border-radius: 5px; background: #1a1a1a; }
        QCheckBox::indicator:hover { border-color: #c52a50; }
        QCheckBox::indicator:checked { background: #c52a50; border-color: #d83a61; }
        QCheckBox:focus { color: #ffffff; }
        QLineEdit, QComboBox { background: #202024; border: 1px solid #404040; border-radius: 6px; padding: 7px 9px; color: #ebe9e8; selection-background-color: #653040; selection-color: #ffffff; }
        QLineEdit:focus, QComboBox:focus { border-color: #c52a50; }
        QPlainTextEdit { background: #101012; color: #ebe9e8; border: none; selection-background-color: #653040; selection-color: #ffffff; }
        #documentStatus { color: #c1bdbb; font-size: 11px; }
        #documentStatus[attention="true"] { color: #e27680; }
        #documentTabClose { color: #a6a19f; background: transparent; border: 0; border-radius: 4px; padding: 0; font-size: 15px; }
        #documentTabClose:hover { color: #f5f3f3; background: #453034; }
        #documentTabClose:pressed { color: #ffffff; background: #5a303a; }
        QSlider::groove:horizontal { height: 5px; border-radius: 2px; background: #34343a; }
        QSlider::sub-page:horizontal { background: #c52a50; border-radius: 2px; }
        QSlider::handle:horizontal { width: 15px; margin: -5px 0; border-radius: 7px; background: #f0edeb; border: 1px solid #c52a50; }
        #documentTabs { background: #141414; border-bottom: 1px solid #34343a; }
        #documentTabs::tab { background: #242428; color: #aaa6a3; border: 0; border-right: 1px solid #34343a; padding: 5px 10px; }
        #documentTabs::tab:selected { background: #252022; color: #ffffff; border-bottom: 2px solid #c52a50; }

        #updateBanner { background: #1d1719; border-bottom: 1px solid #4a2930; }
        #updateLabel { color: #e1dedd; font-weight: 500; }
    )"));
}

} // namespace wolfmark::style
