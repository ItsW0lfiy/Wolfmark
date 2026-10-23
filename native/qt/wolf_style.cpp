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
        QWidget#appWindow { background: #121010; }
        QWidget#contentShell { background: #121010; }
        QLabel { background: transparent; }

        #windowBody { background: #121010; }
        #titleBar { background: #1a1717; border-bottom: 1px solid #3a3434; }
        #headerBrandSymbol { background: transparent; border: none; }
        #headerBrandTitle { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 15px; font-weight: 700; }
        #headerSeparator { background: #423c3d; border: 0; }
        #settingsEyebrow { font-size: 10px; font-weight: 700; letter-spacing: 1.3px; color: #d76b88; }
        #settingsOption { background: #2b2626; border: 1px solid #3c3535; border-radius: 9px; }
        #settingsOption:hover { background: #302a2b; border-color: #4a424a; }
        #settingsFooter { background: transparent; border-top: 1px solid #3a3434; }
        #settingsOption QCheckBox { color: #efeded; font-weight: 600; }
        #settingsOption QLabel { padding-left: 28px; }
        #documentSidebar { background: #1c1919; border-right: 1px solid #3a3434; }
        #brandSymbol { background: #282828; border: 1px solid #3a3a3a; border-radius: 7px; }
        #sidebarIdentity { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 18px; font-weight: 600; }
        #sidebarSection { color: #ada5a9; font-size: 10px; font-weight: 650; letter-spacing: 1.05px; padding: 0 7px; }
        #documentSidebar QPushButton { background: transparent; border-color: transparent; text-align: left; min-height: 34px; padding-left: 11px; }
        #documentSidebar QPushButton:hover { background: #2c282a; }
        #documentSidebar #sidebarOpenButton { background: #712038; color: #f8eef1; border: 1px solid #9d3150; border-radius: 8px; font-weight: 600; padding: 7px 12px; text-align: left; }
        #documentSidebar #sidebarOpenButton:hover { background: #8b2945; border-color: #c52a50; }
        #documentSidebar #sidebarOpenButton:pressed { background: #5e1a2f; }
        #documentSidebar #sidebarReloadButton { background: #292525; border: 1px solid #3a3434; border-radius: 8px; text-align: center; padding: 0; font-size: 20px; }
        #documentSidebar #sidebarReloadButton:hover { background: #363030; border-color: #5c5555; }
        #sidebarFooter { background: transparent; border-top: 1px solid #3a3434; }
        #sidebarFooterName { color: #f1efed; font-size: 12px; font-weight: 600; }
        #sidebarFooterVersion { color: #989093; font-size: 11px; }

        QTreeWidget { background: transparent; border: none; color: #bcb9bb; font-size: 13px; outline: 0; }
        QTreeWidget::item { padding: 7px 6px; border: 1px solid transparent; border-radius: 6px; }
        QTreeWidget::item:hover { background: #252525; color: #f5f3f3; }
        QTreeWidget::item:selected { background: #302629; color: #f5f3f3; border-left: 3px solid #c52a50; }
        QTreeWidget::item:focus { border-color: #c52a50; }
        #openDocuments { background: transparent; border: none; padding: 1px; }
        #openDocuments::item { padding: 6px 5px; border: none; border-radius: 6px; }
        #openDocuments::item:hover { background: #292525; }
        #openDocuments::item:selected { background: #3a2029; color: #f5f3f3; border-left: 3px solid #c52a50; }

        QPushButton { background: #1d1d1d; border: 1px solid #353535; border-radius: 6px; padding: 5px 11px; color: #d5d2d3; }
        QPushButton:hover { background: #363030; border-color: #5c5555; color: #f5f3f3; }
        QPushButton:pressed { background: #3a2029; border-color: #c52a50; }
        QPushButton:focus { border: 1px solid #c52a50; }
        QPushButton:disabled { background: #171717; border-color: #242424; color: #656260; }
        QPushButton[primary="true"] { background: #c52a50; border-color: #c52a50; color: #ffffff; font-weight: 600; }
        QPushButton[primary="true"]:hover { background: #d83a61; border-color: #d83a61; }
        QPushButton[primary="true"]:pressed { background: #a51e41; border-color: #a51e41; }
        QPushButton[nav="true"] { background: transparent; border-color: transparent; border-radius: 7px; text-align: left; padding: 9px 12px; }
        QPushButton[nav="true"]:checked { background: #302629; border-left: 3px solid #c52a50; color: #f5f3f3; padding-left: 9px; }
        QPushButton[titleTool="true"] { background: transparent; border-color: transparent; padding: 3px 7px; }
        QPushButton[titleTool="true"]:hover { background: #363030; border-color: #3d3d3d; }
        QPushButton[titleTool="true"]:pressed { background: #3a2029; border-color: #c52a50; }

        #documentTitle { color: #c9c6c3; font-size: 13px; font-weight: 500; }
        #titleSearch { color: #bbb8b6; background: #282424; border: 1px solid #3a3434; border-radius: 7px; padding: 5px 13px; }
        #titleSearch:hover { color: #f5f3f3; background: #363030; border-color: #5c5555; }
        #titleSearch:disabled { color: #7a7477; background: #242020; border-color: #363030; }
        #documentCommandBar { background: #181515; border-bottom: 1px solid #3a3434; }
        #documentModeGroup { background: #242020; border: 1px solid #3a3434; border-radius: 9px; }
        #documentModeGroup QPushButton[modeSegment="true"] { background: transparent; border: 1px solid transparent; border-radius: 7px; color: #bcb9bb; font-weight: 500; padding: 5px 13px; }
        #documentModeGroup QPushButton[modeSegment="true"]:hover { background: #363030; color: #f5f3f3; }
        #documentModeGroup QPushButton[modeSegment="true"]:checked { background: #712038; border-color: #a52b4b; color: #ffffff; font-weight: 600; }
        #documentModeGroup QPushButton[modeSegment="true"]:focus { border-color: #e0718b; }
        #documentZoomGroup { background: #242020; border: 1px solid #3a3434; border-radius: 8px; }
        #documentZoomGroup QPushButton[titleTool="true"] { background: transparent; border: 1px solid transparent; border-radius: 6px; color: #bbb8b6; padding: 3px 2px; }
        #documentZoomGroup QPushButton[titleTool="true"]:hover { background: #363030; color: #ffffff; }
        #documentZoomGroup QPushButton[titleTool="true"]:focus { border-color: #c52a50; }
        #zoomValue { color: #9c9895; font-size: 12px; background: transparent; border-color: transparent; }
        #documentStack { background: #121010; }
        #emptyTitle { font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 23px; font-weight: 600; color: #ece9e6; }
        #emptyOpen { background: #c52a50; border-color: #c52a50; color: #ffffff; padding: 7px 14px; }
        #emptyOpen:hover { background: #d83a61; border-color: #d83a61; }
        #emptyHint { color: #989093; margin-top: 4px; }
        #diagnosticsBar { background: #121010; border-top: 1px solid #3a3434; color: #777472; font-size: 11px; }

        QTextEdit { background: #121010; border: 0; selection-background-color: #653040; selection-color: #ffffff; }
        QScrollBar:vertical { background: #121010; width: 11px; margin: 0; }
        QScrollBar::handle:vertical { background: #3c3c3c; min-height: 32px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:vertical:hover { background: #5c4a4e; }
        QScrollBar:horizontal { background: #121010; height: 11px; margin: 0; }
        QScrollBar::handle:horizontal { background: #3c3c3c; min-width: 32px; border-radius: 5px; margin: 2px; }
        QScrollBar::handle:horizontal:hover { background: #5c4a4e; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

        QToolTip { background: #242424; color: #ebe9e8; border: 1px solid #5c5555; padding: 5px 7px; }
        QMenu { background: #242020; color: #ebe9e8; border: 1px solid #3b3b3b; padding: 6px; }
        QMenu::item { border-radius: 5px; padding: 7px 25px 7px 11px; }
        QMenu::item:selected { background: #3a2029; color: #ffffff; }
        QMenu::separator { height: 1px; background: #3a3434; margin: 6px 9px; }

        QDialog { background: #181515; }
        #settingsDialog { background: #121010; }
        #settingsChrome { background: #1c1919; border-bottom: 1px solid #3a3434; }
        #settingsChromeTitle { color: #ebe9e8; font-size: 13px; font-weight: 600; }
        #settingsBody { background: #121010; }
        #settingsNavigation { background: #1c1919; border-right: 1px solid #3a3434; }
        #settingsNavigation QPushButton[nav="true"] { border-radius: 8px; min-height: 38px; margin: 2px 0; padding: 9px 14px; font-weight: 500; }
        #settingsNavigation QPushButton[nav="true"]:checked { background: #3a2029; border-left: 3px solid #c52a50; padding-left: 9px; }
        #settingsContent { background: #121010; }
        #settingsBrand { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 20px; font-weight: 600; }
        #settingsTitle { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 29px; font-weight: 600; }
        #settingsSectionTitle { color: #ebe9e8; font-size: 14px; font-weight: 600; }
        #settingsHint { color: #9c9895; }
        #settingsResult { color: #d5d2d3; padding: 8px 0; }
        #settingsCard { background: #242020; border: 1px solid #413a3a; border-radius: 12px; }
        #settingsCard:hover { border-color: #4f4848; }
        #settingsDialog QDialogButtonBox { background: transparent; }
        #settingsDialog QDialogButtonBox QPushButton { min-width: 78px; min-height: 28px; border-radius: 7px; }

        QCheckBox { color: #d5d2d3; spacing: 9px; min-height: 25px; }
        QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #595959; border-radius: 5px; background: #1a1a1a; }
        QCheckBox::indicator:hover { border-color: #c52a50; }
        QCheckBox::indicator:checked { background: #c52a50; border-color: #d83a61; }
        QCheckBox:focus { color: #ffffff; }
        QLineEdit, QComboBox { background: #242020; border: 1px solid #404040; border-radius: 6px; padding: 7px 9px; color: #ebe9e8; selection-background-color: #653040; selection-color: #ffffff; }
        QLineEdit:focus, QComboBox:focus { border-color: #c52a50; }
        QPlainTextEdit { background: #121010; color: #ebe9e8; border: none; selection-background-color: #653040; selection-color: #ffffff; }
        #documentStatus { color: #c1bdbb; font-size: 11px; }
        #documentStatus[attention="true"] { color: #e27680; }
        #documentTabClose { color: #a6a19f; background: transparent; border: 0; border-radius: 4px; padding: 0; font-size: 15px; }
        #documentTabClose:hover { color: #f5f3f3; background: #453034; }
        #documentTabClose:pressed { color: #ffffff; background: #5a303a; }
        QSlider::groove:horizontal { height: 5px; border-radius: 2px; background: #3a3434; }
        QSlider::sub-page:horizontal { background: #c52a50; border-radius: 2px; }
        QSlider::handle:horizontal { width: 15px; margin: -5px 0; border-radius: 7px; background: #f0edeb; border: 1px solid #c52a50; }
        #documentTabRow { background: #1b1717; border-bottom: 1px solid #3a3434; min-height: 42px; }
        #newDocumentTab { background: transparent; border: 1px solid transparent; border-radius: 7px; color: #bfb6ba; font-size: 17px; }
        #newDocumentTab:hover { background: #302a2b; color: #ffffff; border-color: #484141; }
        #documentNavigationButton { background: transparent; border: 1px solid transparent; color: #bcb9bb; font-size: 17px; padding: 1px; }
        #documentNavigationButton:hover { background: #363030; color: #ffffff; }
        #documentNavigationButton:disabled { color: #6c6467; }
        #documentTabs { background: transparent; min-height: 40px; }
        #documentTabs::tab { background: #211d1d; color: #aaa6a3; border: 1px solid #3a3434; border-bottom: 0; border-radius: 7px 7px 0 0; margin: 5px 3px 0 0; padding: 8px 15px; min-height: 25px; }
        #documentTabs::tab:hover { background: #2d2727; color: #f5f3f3; }
        #documentTabs::tab:selected { background: #282424; color: #ffffff; border-top: 2px solid #c52a50; }
        #documentTabs::tab:focus { border-color: #d64d72; }
        #documentTabs::tab:disabled { color: #7a7477; }

        #updateBanner { background: #1d1719; border-bottom: 1px solid #4a2930; }
        #updateLabel { color: #e1dedd; font-weight: 500; }
    )"));
}

} // namespace wolfmark::style
