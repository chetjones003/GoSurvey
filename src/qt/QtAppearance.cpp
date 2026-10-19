#include "QtAppearance.hpp"
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
  palette.setColor(QPalette::Highlight, QColor("#0078d7"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Link, QColor("#0066cc"));
  palette.setColor(QPalette::ToolTipBase, QColor("#ffffe1"));
  palette.setColor(QPalette::ToolTipText, Qt::black);
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#d9d9d9"));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#808080"));
  app.setPalette(palette);
  app.setStyleSheet(R"(
    QMainWindow::separator { background:#b9bdc3; width:3px; height:3px; }
    QToolBar { border:0; spacing:1px; padding:1px; background:#e5e7ea; }
    #editorTitleBar { background:#e3e5e8; border-bottom:1px solid #a9adb4; }
    QToolButton { border:1px solid transparent; border-radius:0; padding:2px 4px; }
    QToolButton:hover { background:#e5f0fb; border-color:#93b4d3; }
    QToolButton:pressed, QToolButton:checked { background:#cee1f4; border-color:#739bbf; }
    #windowClose:hover { background:#c42b1c; color:white; border-color:#c42b1c; }
    #ribbon::pane { border:1px solid #afb4bc; background:#e9ebee; }
    #ribbon QToolButton { padding:2px 5px; }
    #ribbon QWidget[ribbonPanel="true"] { border-right:1px solid #b9bec5; }
    #ribbonPanelLabel { color:#555e69; font-size:10px; border-top:1px solid #d2d5da; padding-top:1px; }
    QTabBar::tab { background:#dce0e5; color:#30353b; border:1px solid #b0b5bd; padding:3px 12px; margin-right:1px; }
    QTabBar::tab:selected { background:#f6f7f8; border-bottom-color:#f6f7f8; }
    QTabBar::tab:hover:!selected { background:#eaf0f7; }
    #drawingTabs::tab { padding:3px 10px; }
    QDockWidget::title { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #eef0f3,stop:1 #c6cbd2); border:1px solid #a9afb8; padding:3px 6px; color:#30363f; }
    QTreeView { background:white; border:1px solid #b3b8bf; outline:0; }
    QTreeView::item { min-height:18px; }
    QTreeView::item:selected { background:#dceafb; color:#152c44; }
    QHeaderView::section { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f6f7f9,stop:1 #d9dde3); border:0; border-right:1px solid #b5bac2; border-bottom:1px solid #a9afb8; padding:3px 5px; color:#333b46; }
    QLineEdit, QComboBox { background:white; color:#202020; border:1px solid #aeb4bc; border-radius:0; padding:2px 4px; min-height:18px; }
    QLineEdit:focus, QComboBox:focus { border-color:#5d94c2; }
    QStatusBar { background:#e0e3e7; border-top:1px solid #a8afb8; }
    QStatusBar::item { border:0; }
    #commandRow { background:#e0e3e7; border:1px solid #8d98a5; }
    #commandRecent, #commandHint { background:#f1f2f4; border:1px solid #b8bec6; padding:3px 6px; }
    #commandInput { background:white; border:1px solid #aeb4bc; padding:2px 4px; }
    QMenu { background:#f7f7f7; border:1px solid #a5abb3; padding:2px; }
    QMenu::item { padding:4px 24px 4px 24px; }
    QMenu::item:selected { background:#dceafb; color:#202020; }
  )");
  } else {
  // Deep navy surfaces, aqua interaction states, and restrained amber details.
  QPalette palette(QColor("#081724"));
  palette.setColor(QPalette::Window, QColor("#081724"));
  palette.setColor(QPalette::WindowText, QColor("#f0f5fa"));
  palette.setColor(QPalette::Base, QColor("#06121d"));
  palette.setColor(QPalette::AlternateBase, QColor("#0c1e2c"));
  palette.setColor(QPalette::Text, QColor("#f0f5fa"));
  palette.setColor(QPalette::Light, QColor("#45647a"));
  palette.setColor(QPalette::Midlight, QColor("#29485d"));
  palette.setColor(QPalette::Mid, QColor("#1b364b"));
  palette.setColor(QPalette::Dark, QColor("#0b1a27"));
  palette.setColor(QPalette::Shadow, QColor("#040b12"));
  palette.setColor(QPalette::PlaceholderText, QColor("#9bb0c0"));
  palette.setColor(QPalette::Button, QColor("#12283a"));
  palette.setColor(QPalette::ButtonText, QColor("#f0f5fa"));
  palette.setColor(QPalette::Highlight, QColor("#087f97"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Link, QColor("#46d4e7"));
  palette.setColor(QPalette::ToolTipBase, QColor("#102538"));
  palette.setColor(QPalette::ToolTipText, QColor("#f2f6fa"));
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#92a6b5"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#92a6b5"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#92a6b5"));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#193346"));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#92a6b5"));
  app.setPalette(palette);
  app.setStyleSheet(R"(
    QMainWindow::separator { background:#123248; width:3px; height:3px; }
    QToolBar { border:0; spacing:2px; padding:2px; background:#061725; }
    #editorTitleBar { background:#061421; border-bottom:1px solid #102d42; }
    #windowBrandTitle { color:#f4f8fc; font-size:15px; font-weight:600; background:transparent; }
    #applicationMenu { background:#087fdb; border:1px solid #3d9cff; border-radius:8px; padding:2px; }
    #applicationMenu:hover { background:#1593ed; }
    #editorTitleBar QToolButton { color:#eaf4fb; border:1px solid transparent; border-radius:5px; padding:2px 4px; }
    #editorTitleBar QToolButton:hover { background:#12344b; border-color:#1c5674; }
    #windowClose:hover { background:#c42b1c; color:white; border-color:#c42b1c; }
    QToolButton { border:1px solid transparent; border-radius:4px; padding:3px 5px; }
    QToolButton:hover { background:#153146; border-color:#25617b; }
    QToolButton:pressed, QToolButton:checked { background:#12475a; border-color:#00c5e0; }
    #windowClose:hover { background:#c42b1c; color:white; border-color:#c42b1c; }
    #ribbonBar { background:#071a29; border-top:1px solid #123147; border-bottom:1px solid #14364c; }
    #ribbon::pane { border:1px solid #14364b; background:#081d2c; }
    #ribbon QToolButton { padding:2px 5px; border-radius:4px; }
    #ribbon QToolButton:hover { background:#123449; border-color:#1b607d; }
    #ribbon QToolButton:pressed, #ribbon QToolButton:checked { background:#0e5263; border-color:#11c9e2; }
    #ribbon QWidget[ribbonPanel="true"] { border-right:1px solid #17384b; }
    #ribbonPanelLabel { color:#9cb8c9; font-size:10px; border-top:1px solid #17384b; padding-top:2px; }
    QTabWidget::pane { border:1px solid #16394f; background:#091d2c; }
    QTabBar::tab { background:#081724; color:#dce7ef; border:1px solid #17364a; padding:6px 14px; margin-right:2px; }
    QTabBar::tab:selected { background:#10283b; color:#f4fbff; border-bottom:2px solid #00c5e0; }
    QTabBar::tab:hover:!selected { background:#102638; }
    #drawingTabs::tab { padding:6px 16px; }
    QDockWidget { background:#081724; border:1px solid #12354a; }
    QDockWidget::title { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #102b40,stop:1 #0a1e2e); border:0; border-bottom:1px solid #163b52; padding:7px 10px; color:#edf6fb; font-weight:600; }
    QTreeView { background:#061725; alternate-background-color:#091d2b; border:1px solid #12354a; outline:0; }
    QTreeView::item { min-height:23px; padding:2px 3px; }
    QTreeView::item:selected { background:#10495b; color:#f5fcff; border-left:2px solid #09c9df; }
    QHeaderView::section { background:#0d2638; border:0; border-right:1px solid #1d3c50; border-bottom:1px solid #1d3c50; padding:6px 8px; color:#dce8f0; }
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTableWidget { background:#061522; color:#edf4f9; border:1px solid #29485c; border-radius:5px; padding:5px 8px; min-height:21px; selection-background-color:#087f97; }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color:#11c9e2; }
    QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background:#0b1a27; color:#92a6b5; border-color:#1b364b; }
    QPushButton { background:#10283a; color:#eaf4fa; border:1px solid #1b3c51; border-radius:5px; padding:6px 10px; }
    QPushButton:hover { background:#15364c; border-color:#1f7089; }
    QPushButton:pressed { background:#0d485b; border-color:#00c5e0; }
    QRadioButton { color:#dceaf2; spacing:8px; }
    QRadioButton::indicator { width:16px; height:16px; border:1px solid #436277; border-radius:8px; background:#081724; }
    QRadioButton::indicator:checked { border:4px solid #00c5e0; background:#e7fbff; }
    QScrollBar:vertical { background:#071927; width:12px; margin:0; }
    QScrollBar::handle:vertical { background:#1b3a4e; min-height:28px; border-radius:5px; margin:2px; }
    QScrollBar::handle:vertical:hover { background:#28617a; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
    QScrollBar:horizontal { background:#071927; height:12px; margin:0; }
    QScrollBar::handle:horizontal { background:#1b3a4e; min-width:28px; border-radius:5px; margin:2px; }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
    QStatusBar { background:#071827; border-top:1px solid #17364a; }
    QStatusBar::item { border:0; }
    #commandRow { background:#091d2c; border:1px solid #1c4960; border-radius:7px; }
    #commandRecent, #commandHint { background:#0c2536; border:1px solid #1b3c51; padding:5px 8px; border-radius:5px; }
    #commandInput { background:#061522; color:#edf4f9; border:1px solid #29485c; border-radius:5px; padding:4px 7px; }
    QMenu { background:#0d2030; color:#edf4f9; border:1px solid #234256; padding:3px; }
    QMenu::item { padding:4px 24px 4px 24px; }
    QMenu::item:selected { background:#12475a; color:#ffffff; }
    QToolTip { background:#102538; color:#f2f6fa; border:1px solid #31546a; padding:4px 6px; }
  )");
  }
}
