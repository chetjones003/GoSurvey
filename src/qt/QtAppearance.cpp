#include "QtAppearance.hpp"
#include "QtAccent.hpp"
#include <QApplication>
#include <QFont>
#include <QPalette>

void ApplyQtAppearance(QApplication& app, bool light) {
  app.setStyle("Fusion");
  app.setFont(QFont("Segoe UI", 9));
  app.setProperty("defaultQtStyle", true);
  app.setProperty("cadLightTheme", light);
  if (light) {
  // Explicit light roles keep Fusion light even when Windows uses dark mode.
  QPalette palette(QColor("#f0f0f0"));
  palette.setColor(QPalette::Window, QColor("#f0f0f0"));
  palette.setColor(QPalette::WindowText, QColor("#202020"));
  palette.setColor(QPalette::Base, Qt::white);
  palette.setColor(QPalette::AlternateBase, QColor("#f7f7f7"));
  palette.setColor(QPalette::Text, QColor("#202020"));
  palette.setColor(QPalette::Button, QColor("#f0f0f0"));
  palette.setColor(QPalette::ButtonText, QColor("#202020"));
  palette.setColor(QPalette::Highlight, QColor(QtAccentFor(false).fill));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Link, QColor(QtAccentFor(false).link));
  palette.setColor(QPalette::ToolTipBase, QColor("#ffffe1"));
  palette.setColor(QPalette::ToolTipText, Qt::black);
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#d9d9d9"));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#808080"));
  app.setPalette(palette);
  app.setStyleSheet(QtApplyAccent(R"(
    QMainWindow::separator { background:#b9bdc3; width:3px; height:3px; }
    QMainWindow#GoSurveyQtEditor::separator { background:#c7cbd0; border-left:1px solid #f7f8f9; border-right:1px solid #9fa5ac; border-top:1px solid #f7f8f9; border-bottom:1px solid #9fa5ac; width:4px; height:4px; }
    QToolBar { border:0; spacing:1px; padding:1px; background:#e5e7ea; }
    QToolBar#ribbonBar { border-bottom:2px solid #b7bcc3; }
    #editorTitleBar { background:#e3e5e8; border-bottom:1px solid #a9adb4; }
    QToolButton { border:1px solid transparent; border-radius:0; padding:2px 4px; }
    QToolButton:hover { background:@hoverTint@; border-color:@edge@; }
    QToolButton:pressed, QToolButton:checked { background:@tint@; border-color:@edge@; }
    #windowClose:hover { background:#c42b1c; color:white; border-color:#c42b1c; }
    #ribbon::pane { border:1px solid #afb4bc; border-radius:5px; background:#e9ebee; }
    #ribbon QToolButton { padding:2px 5px; }
    #ribbonTabMenu { border:1px solid #b0b5bd; padding:2px 8px; }
    #ribbon QWidget[ribbonPanel="true"] { border-right:1px solid #b9bec5; }
    #ribbonPanelLabel { color:#555e69; font-size:10px; border-top:1px solid #d2d5da; padding-top:1px; }
    #ribbon QTabBar::tab { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f1f3f5,stop:1 #dce0e5); color:#30353b; border:1px solid #b0b5bd; border-radius:5px 5px 0 0; padding:3px 12px; margin:2px 2px 0 0; }
    #ribbon QTabBar::tab:selected { background:#f6f7f8; border-bottom:2px solid @strong@; margin-top:0; }
    #ribbon QTabBar::tab:hover:!selected { background:@hoverTint@; border-color:@edge@; }
    QTabBar::tab { background:#dce0e5; color:#30353b; border:1px solid #b0b5bd; padding:3px 12px; margin-right:1px; }
    QTabBar::tab:selected { background:#f6f7f8; border-bottom-color:#f6f7f8; }
    QTabBar::tab:hover:!selected { background:@hoverTint@; }
    #drawingTabs::tab { padding:3px 10px; }
    QDockWidget#propertiesDock, QDockWidget#toolspaceDock { background:#e9ebee; border:1px solid #a9afb8; border-top-color:#ffffff; border-left-color:#f7f8f9; border-radius:5px; }
    QDockWidget::title { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #eef0f3,stop:1 #c6cbd2); border:1px solid #a9afb8; padding:3px 6px; color:#30363f; }
    QDockWidget#propertiesDock::title, QDockWidget#toolspaceDock::title { border-radius:5px 5px 0 0; }
    QTreeView { background:white; border:1px solid #b3b8bf; outline:0; }
    QTreeView::item { min-height:18px; }
    QTreeView::item:selected { background:@tint@; color:#202020; }
    QHeaderView::section { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f6f7f9,stop:1 #d9dde3); border:0; border-right:1px solid #b5bac2; border-bottom:1px solid #a9afb8; padding:3px 5px; color:#333b46; }
    QLineEdit, QComboBox { background:white; color:#202020; border:1px solid #aeb4bc; border-radius:0; padding:2px 4px; min-height:18px; }
    QLineEdit:focus, QComboBox:focus { border-color:@strong@; }
    QStatusBar { background:#e0e3e7; border-top:1px solid #a8afb8; }
    QStatusBar::item { border:0; }
    #commandRow { background:#e0e3e7; border:1px solid #8d98a5; }
    #commandRecent, #commandHint { background:#f1f2f4; border:1px solid #b8bec6; padding:3px 6px; }
    #commandInput { background:white; border:1px solid #aeb4bc; padding:2px 4px; }
    QMenu { background:#f7f7f7; border:1px solid #a5abb3; padding:2px; }
    QMenu::item { padding:4px 24px 4px 24px; }
    QMenu::item:selected { background:@tint@; color:#202020; }
  )", false));
  } else {
  // Graphite surfaces with topo-line amber interaction states (REQ-436); see QtAccent.hpp.
  QPalette palette(QColor("#1c2024"));
  palette.setColor(QPalette::Window, QColor("#1c2024"));
  palette.setColor(QPalette::WindowText, QColor("#f0f5fa"));
  palette.setColor(QPalette::Base, QColor("#14181b"));
  palette.setColor(QPalette::AlternateBase, QColor("#242a2f"));
  palette.setColor(QPalette::Text, QColor("#f0f5fa"));
  palette.setColor(QPalette::Light, QColor("#515a61"));
  palette.setColor(QPalette::Midlight, QColor("#3a4248"));
  palette.setColor(QPalette::Mid, QColor("#30373d"));
  palette.setColor(QPalette::Dark, QColor("#111416"));
  palette.setColor(QPalette::Shadow, QColor("#0c0e10"));
  palette.setColor(QPalette::PlaceholderText, QColor("#a5afb6"));
  palette.setColor(QPalette::Button, QColor("#2a3035"));
  palette.setColor(QPalette::ButtonText, QColor("#f0f5fa"));
  palette.setColor(QPalette::Highlight, QColor(QtAccentFor(true).fill));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Link, QColor(QtAccentFor(true).link));
  palette.setColor(QPalette::ToolTipBase, QColor("#292f34"));
  palette.setColor(QPalette::ToolTipText, QColor("#f2f6fa"));
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#92a6b5"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#92a6b5"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#92a6b5"));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#343b40"));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#92a6b5"));
  app.setPalette(palette);
  app.setStyleSheet(QtApplyAccent(R"(
    QMainWindow::separator { background:#343b40; width:3px; height:3px; }
    QMainWindow#GoSurveyQtEditor::separator { background:#30373d; border-left:1px solid #626b71; border-right:1px solid #111416; border-top:1px solid #626b71; border-bottom:1px solid #111416; width:4px; height:4px; }
    QToolBar { border:0; spacing:2px; padding:2px; background:#15191c; }
    #editorTitleBar { background:#171b1e; border-bottom:1px solid #30373c; }
    #windowBrandTitle { color:#f4f8fc; font-size:15px; font-weight:600; background:transparent; }
    #applicationMenu { background:@fill@; border:1px solid @strong@; border-radius:8px; padding:2px; }
    #applicationMenu:hover { background:@fillHover@; }
    #editorTitleBar QToolButton { color:#eaf4fb; border:1px solid transparent; border-radius:5px; padding:2px 4px; }
    #editorTitleBar QToolButton:hover { background:#30383d; border-color:@edge@; }
    #windowClose:hover { background:#c42b1c; color:white; border-color:#c42b1c; }
    QToolButton { border:1px solid transparent; border-radius:4px; padding:3px 5px; }
    QToolButton:hover { background:#30383d; border-color:@edge@; }
    QToolButton:pressed, QToolButton:checked { background:@tint@; border-color:@strong@; }
    #windowClose:hover { background:#c42b1c; color:white; border-color:#c42b1c; }
    #ribbonBar { background:#191d20; border-top:1px solid #30373c; border-bottom:2px solid #41494e; }
    #ribbon::pane { border:1px solid #363e43; border-radius:5px; background:#202529; }
    #ribbon QToolButton { padding:2px 5px; border-radius:4px; }
    #ribbonTabMenu { border:1px solid #353d42; padding:2px 8px; }
    #ribbon QToolButton:hover { background:#343c41; border-color:@edge@; }
    #ribbon QToolButton:pressed, #ribbon QToolButton:checked { background:@tint@; border-color:@strong@; }
    #ribbon QWidget[ribbonPanel="true"] { border-right:1px solid #3a4247; }
    #ribbonPanelLabel { color:#b5c0c5; font-size:10px; border-top:1px solid #3a4247; padding-top:2px; }
    QTabWidget::pane { border:1px solid #3a4247; background:#22272b; }
    #ribbon QTabBar::tab { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #30373c,stop:1 #1b2023); color:#dce2e5; border:1px solid #41494e; border-radius:5px 5px 0 0; padding:6px 14px; margin:2px 2px 0 0; }
    #ribbon QTabBar::tab:selected { background:#2a3035; color:#f4f7f8; border-bottom:2px solid @strong@; margin-top:0; }
    #ribbon QTabBar::tab:hover:!selected { background:#30373c; border-color:@edge@; }
    QTabBar::tab { background:#1b2023; color:#dce2e5; border:1px solid #353d42; padding:6px 14px; margin-right:2px; }
    QTabBar::tab:selected { background:#2a3035; color:#f4f7f8; border-bottom:2px solid @strong@; }
    QTabBar::tab:hover:!selected { background:#30373c; }
    #drawingTabs::tab { padding:6px 16px; }
    QDockWidget { background:#1c2024; border:1px solid #353d42; }
    QDockWidget#propertiesDock, QDockWidget#toolspaceDock { border:1px solid #41494e; border-top-color:#626b71; border-left-color:#515a61; border-radius:5px; }
    QDockWidget::title { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #30373c,stop:1 #24292d); border:0; border-bottom:1px solid #41494e; padding:7px 10px; color:#edf1f3; font-weight:600; }
    QDockWidget#propertiesDock::title, QDockWidget#toolspaceDock::title { border-radius:5px 5px 0 0; }
    QTreeView { background:#171b1e; alternate-background-color:#202529; border:1px solid #353d42; outline:0; }
    QTreeView::item { min-height:23px; padding:2px 3px; }
    QTreeView::item:selected { background:@tint@; color:#fff8f0; border-left:2px solid @strong@; }
    QHeaderView::section { background:#292f34; border:0; border-right:1px solid #3a4247; border-bottom:1px solid #3a4247; padding:6px 8px; color:#dce2e5; }
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTableWidget { background:#15191c; color:#edf1f3; border:1px solid #424b51; border-radius:5px; padding:5px 8px; min-height:21px; selection-background-color:@fill@; }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color:@strong@; }
    QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background:#202529; color:#99a4aa; border-color:#353d42; }
    QPushButton { background:#2a3035; color:#eaf0f2; border:1px solid #41494e; border-radius:5px; padding:6px 10px; }
    QPushButton:hover { background:#343c41; border-color:@edge@; }
    QPushButton:pressed { background:@tint@; border-color:@strong@; }
    QRadioButton { color:#dceaf2; spacing:8px; }
    QRadioButton::indicator { width:16px; height:16px; border:1px solid #5b666d; border-radius:8px; background:#1c2024; }
    QRadioButton::indicator:checked { border:4px solid @strong@; background:#fff4e5; }
    QScrollBar:vertical { background:#191d20; width:12px; margin:0; }
    QScrollBar::handle:vertical { background:#3a4247; min-height:28px; border-radius:5px; margin:2px; }
    QScrollBar::handle:vertical:hover { background:#4e5a60; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
    QScrollBar:horizontal { background:#191d20; height:12px; margin:0; }
    QScrollBar::handle:horizontal { background:#3a4247; min-width:28px; border-radius:5px; margin:2px; }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
    QStatusBar { background:#191d20; border-top:1px solid #353d42; }
    QStatusBar::item { border:0; }
    #commandRow { background:#22272b; border:1px solid #41494e; border-radius:7px; }
    #commandRecent, #commandHint { background:#292f34; border:1px solid #41494e; padding:5px 8px; border-radius:5px; }
    #commandInput { background:#15191c; color:#edf1f3; border:1px solid #424b51; border-radius:5px; padding:4px 7px; }
    QMenu { background:#252b2f; color:#edf1f3; border:1px solid #454e53; padding:3px; }
    QMenu::item { padding:4px 24px 4px 24px; }
    QMenu::item:selected { background:@tint@; color:#ffffff; }
    QToolTip { background:#292f34; color:#f2f6fa; border:1px solid #515c62; padding:4px 6px; }
  )", true));
  }
}
