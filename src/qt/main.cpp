#include "EditorWindow.hpp"
#include "PdfAttach.hpp"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QSplashScreen>
#include <QSurfaceFormat>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv) {
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(format);
  QApplication app(argc, argv);
  app.setOrganizationName("GoSurvey");
  app.setApplicationName("GoSurveyQt");
  app.setStyle("Fusion");
  app.setFont(QFont("Segoe UI", 9));
  app.setProperty("defaultQtStyle", true);
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
  const bool smoke = app.arguments().contains("--smoke-test");
  QPixmap splashImage(QtAsset("splash-bg.png"));
  if (splashImage.isNull()) { splashImage = QPixmap(800, 440); splashImage.fill(QColor("#242830")); }
  splashImage = splashImage.scaled(800, 440, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  QPainter painter(&splashImage);
  painter.setPen(Qt::white);
  painter.setFont(QFont("Segoe UI", 30, QFont::DemiBold));
  painter.drawText(38, 350, "GoSurvey");
  painter.setFont(QFont("Segoe UI", 11));
  painter.drawText(40, 383, "Survey and civil design | Qt / Win32");
  painter.end();
  QSplashScreen splash(splashImage);
  if (!smoke) { splash.show(); app.processEvents(); }
  if (!PdfAttach_Init()) {
    std::fprintf(stderr, "PDFium initialization failed\n");
    return 1;
  }
  int result = 0;
  {
    EditorWindow window;
    window.show();
    splash.finish(&window);
    if (smoke) {
      QTimer::singleShot(800, &window, [&] {
        QString error;
        const bool ok = window.RunSmokeChecks(error);
        const auto screenshot = window.grab();
        screenshot.save(QCoreApplication::applicationDirPath() + "/qt-editor-smoke.png");
        std::fprintf(ok ? stdout : stderr, "%s\n", ok ? "Qt editor smoke checks passed" : error.toUtf8().constData());
        app.exit(ok ? 0 : 2);
      });
    } else {
      for (const auto& argument : app.arguments().mid(1))
        if (argument != "--default-style") { window.OpenDrawing(argument); break; }
    }
    result = app.exec();
  } // Release viewport resources and PDF worker windows before shutting down PDFium.
  PdfAttach_Shutdown();
  return result;
}
