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
  // Dark charcoal workspace with lighter neutral-gray panels and ribbon.
  QPalette palette(QColor("#222426"));
  palette.setColor(QPalette::Window, QColor("#222426"));
  palette.setColor(QPalette::WindowText, QColor("#edf2f7"));
  palette.setColor(QPalette::Base, QColor("#252729"));
  palette.setColor(QPalette::AlternateBase, QColor("#2d2f31"));
  palette.setColor(QPalette::Text, QColor("#edf2f7"));
  palette.setColor(QPalette::Light, QColor("#65686a"));
  palette.setColor(QPalette::Midlight, QColor("#4b4e50"));
  palette.setColor(QPalette::Mid, QColor("#3b3d3f"));
  palette.setColor(QPalette::Dark, QColor("#2a2c2e"));
  palette.setColor(QPalette::Shadow, QColor("#18191a"));
  palette.setColor(QPalette::PlaceholderText, QColor("#a9afb3"));
  palette.setColor(QPalette::Button, QColor("#383a3c"));
  palette.setColor(QPalette::ButtonText, QColor("#edf2f7"));
  palette.setColor(QPalette::Highlight, QColor("#55595d"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Link, QColor("#d0d3d5"));
  palette.setColor(QPalette::ToolTipBase, QColor("#3b3d3f"));
  palette.setColor(QPalette::ToolTipText, QColor("#f2f4f5"));
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#a0a3a5"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#a0a3a5"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#a0a3a5"));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#3b3d3f"));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#a0a3a5"));
  app.setPalette(palette);
  app.setStyleSheet(R"(
    QMainWindow::separator { background:#4b4e50; width:3px; height:3px; }
    QToolBar { border:0; spacing:1px; padding:1px; background:#303234; }
    #editorTitleBar { background:#2a2c2e; border-bottom:1px solid #18191a; }
    QToolButton { border:1px solid transparent; border-radius:0; padding:2px 4px; }
    QToolButton:hover { background:#414345; border-color:#5c5f61; }
    QToolButton:pressed, QToolButton:checked { background:#55595d; border-color:#74787a; }
    #windowClose:hover { background:#c42b1c; color:white; border-color:#c42b1c; }
    #ribbon::pane { border:1px solid #4b4e50; background:#383a3c; }
    #ribbon QToolButton { padding:2px 5px; }
    #ribbon QWidget[ribbonPanel="true"] { border-right:1px solid #515355; }
    #ribbonPanelLabel { color:#d0d3d5; font-size:10px; border-top:1px solid #515355; padding-top:1px; }
    QTabBar::tab { background:#303234; color:#e7e9eb; border:1px solid #414345; padding:3px 12px; margin-right:1px; }
    QTabBar::tab:selected { background:#414345; border-bottom-color:#414345; }
    QTabBar::tab:hover:!selected { background:#3a3c3e; }
    #drawingTabs::tab { padding:3px 10px; }
    QDockWidget { background:#2d2f31; }
    QDockWidget::title { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #3b3d3f,stop:1 #303234); border:1px solid #4b4e50; padding:3px 6px; color:#edf2f7; }
    QTreeView { background:#292b2d; alternate-background-color:#303234; border:1px solid #414345; outline:0; }
    QTreeView::item { min-height:18px; }
    QTreeView::item:selected { background:#55595d; color:#ffffff; }
    QHeaderView::section { background:#383a3c; border:0; border-right:1px solid #4b4e50; border-bottom:1px solid #4b4e50; padding:3px 5px; color:#edf2f7; }
    QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTableWidget { background:#252729; color:#edf2f7; border:1px solid #55595d; border-radius:0; padding:2px 4px; min-height:18px; selection-background-color:#55595d; }
    QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color:#a9afb3; }
    QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { background:#303234; color:#a0a3a5; border-color:#484b4d; }
    QStatusBar { background:#303234; border-top:1px solid #414345; }
    QStatusBar::item { border:0; }
    #commandRow { background:#303234; border:1px solid #55595d; }
    #commandRecent, #commandHint { background:#2d2f31; border:1px solid #4b4e50; padding:3px 6px; }
    #commandInput { background:#252729; color:#edf2f7; border:1px solid #55595d; padding:2px 4px; }
    QMenu { background:#2d2f31; color:#edf2f7; border:1px solid #4b4e50; padding:2px; }
    QMenu::item { padding:4px 24px 4px 24px; }
    QMenu::item:selected { background:#55595d; color:#ffffff; }
    QToolTip { background:#3b3d3f; color:#f2f4f5; border:1px solid #55595d; padding:3px; }
  )");
  }
}
