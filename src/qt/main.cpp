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
  app.setFont(QFont("Segoe UI", 10));
  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#30343b"));
  palette.setColor(QPalette::WindowText, QColor("#e1e4e8"));
  palette.setColor(QPalette::Base, QColor("#242830"));
  palette.setColor(QPalette::AlternateBase, QColor("#2e333c"));
  palette.setColor(QPalette::Text, QColor("#e1e4e8"));
  palette.setColor(QPalette::Button, QColor("#3c414a"));
  palette.setColor(QPalette::ButtonText, QColor("#e1e4e8"));
  palette.setColor(QPalette::Highlight, QColor("#367aa9"));
  palette.setColor(QPalette::HighlightedText, Qt::white);
  palette.setColor(QPalette::ToolTipBase, QColor("#f0f0f0"));
  palette.setColor(QPalette::ToolTipText, QColor("#202020"));
  app.setPalette(palette);
  app.setStyleSheet(
      "QMainWindow::separator { background:#20242b; width:4px; height:4px; }"
      "QToolBar { border:0; spacing:3px; background:#30343b; }"
      "QToolButton { padding:5px; border:1px solid transparent; border-radius:2px; }"
      "QToolButton:hover { background:#4b596b; border-color:#7196b8; }"
      "QTabBar::tab { padding:7px 16px; background:#30343b; }"
      "QTabBar::tab:selected { background:#454e5b; border-bottom:2px solid #62acd9; }"
      "QDockWidget::title { padding:7px; background:#414750; }"
      "QTreeView { border:0; } QLineEdit { padding:5px; }"
      "QStatusBar { background:#242830; }"
  );
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
    } else if (app.arguments().size() > 1) window.OpenDrawing(app.arguments().at(1));
    result = app.exec();
  } // Release viewport resources and PDF worker windows before shutting down PDFium.
  PdfAttach_Shutdown();
  return result;
}
