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
  // Neutral gray CAD chrome stays consistent across Windows appearance settings.
  QPalette palette(QColor("#535353"));
  palette.setColor(QPalette::Window, QColor("#535353"));
  palette.setColor(QPalette::WindowText, QColor("#eeeeee"));
  palette.setColor(QPalette::Base, QColor("#414141"));
  palette.setColor(QPalette::AlternateBase, QColor("#474747"));
  palette.setColor(QPalette::Text, QColor("#eeeeee"));
  palette.setColor(QPalette::Light, QColor("#777777"));
  palette.setColor(QPalette::Midlight, QColor("#646464"));
  palette.setColor(QPalette::Mid, QColor("#494949"));
  palette.setColor(QPalette::Dark, QColor("#333333"));
  palette.setColor(QPalette::Shadow, QColor("#222222"));
  palette.setColor(QPalette::PlaceholderText, QColor("#b8b8b8"));
  palette.setColor(QPalette::Button, QColor("#535353"));
  palette.setColor(QPalette::ButtonText, QColor("#eeeeee"));
  palette.setColor(QPalette::Highlight, QColor("#527a9e"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::Link, QColor("#92c8f0"));
  palette.setColor(QPalette::ToolTipBase, QColor("#ffffe1"));
  palette.setColor(QPalette::ToolTipText, Qt::black);
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#808080"));
  palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#5a5a5a"));
  palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#808080"));
  app.setPalette(palette);
  app.setStyleSheet(R"(
    QMainWindow::separator { background:#353535; width:3px; height:3px; }
    QToolBar { border:0; spacing:1px; padding:1px; background:#535353; }
    #editorTitleBar { background:#484848; border-bottom:1px solid #363636; }
    QToolButton { border:1px solid transparent; border-radius:0; padding:2px 4px; }
    QToolButton:hover { background:#686868; border-color:#8a9eaf; }
    QToolButton:pressed, QToolButton:checked { background:#5a748c; border-color:#91b3d1; }
    #windowClose:hover { background:#c42b1c; color:white; border-color:#c42b1c; }
    #ribbon::pane { border:1px solid #383838; background:#545454; }
    #ribbon QToolButton { padding:2px 5px; }
    #ribbon QWidget[ribbonPanel="true"] { border-right:1px solid #686868; }
    #ribbonPanelLabel { color:#d0d0d0; font-size:10px; border-top:1px solid #696969; padding-top:1px; }
    QTabBar::tab { background:#484848; color:#eeeeee; border:1px solid #363636; padding:3px 12px; margin-right:1px; }
    QTabBar::tab:selected { background:#606060; border-bottom-color:#606060; }
    QTabBar::tab:hover:!selected { background:#5b5b5b; }
    #drawingTabs::tab { padding:3px 10px; }
    QDockWidget::title { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #666666,stop:1 #575757); border:1px solid #3b3b3b; padding:3px 6px; color:#f0f0f0; }
    QTreeView { background:#414141; border:1px solid #353535; outline:0; }
    QTreeView::item { min-height:18px; }
    QTreeView::item:selected { background:#58758e; color:#ffffff; }
    QHeaderView::section { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #666666,stop:1 #575757); border:0; border-right:1px solid #454545; border-bottom:1px solid #3b3b3b; padding:3px 5px; color:#eeeeee; }
    QLineEdit, QComboBox { background:#393939; color:#eeeeee; border:1px solid #777777; border-radius:0; padding:2px 4px; min-height:18px; }
    QLineEdit:focus, QComboBox:focus { border-color:#9ac6e8; }
    QStatusBar { background:#505050; border-top:1px solid #363636; }
    QStatusBar::item { border:0; }
    #commandRow { background:#505050; border:1px solid #858585; }
    #commandRecent, #commandHint { background:#414141; border:1px solid #666666; padding:3px 6px; }
    #commandInput { background:#fafafa; color:#202020; border:1px solid #777777; padding:2px 4px; }
    QMenu { background:#494949; border:1px solid #333333; padding:2px; }
    QMenu::item { padding:4px 24px 4px 24px; }
    QMenu::item:selected { background:#58758e; color:#eeeeee; }
  )");
  }
}
