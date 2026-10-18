#include "EditorWindow.hpp"
#include "QtAppearance.hpp"
#include "PdfAttach.hpp"
#include "UserPrefs.hpp"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QSettings>
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
  ApplyQtAppearance(app, app.arguments().contains("--light-style") || QSettings().value("editor/lightTheme", false).toBool());
  const bool smoke = app.arguments().contains("--smoke-test");
  const bool previewSignIn = app.arguments().contains("--preview-sign-in");
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
    LoadUserStartupPrefSettings(window.State());
    if (!smoke) {
      // Started before sign-in so the update offer, when there is one, is shown first (REQ-077).
      window.StartUpdateCheck();
      window.StartAuth(previewSignIn);
    }
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
        if (argument != "--default-style" && argument != "--light-style" &&
            argument != "--preview-sign-in") { window.OpenDrawing(argument); break; }
    }
    result = app.exec();
  } // Release viewport resources and PDF worker windows before shutting down PDFium.
  PdfAttach_Shutdown();
  return result;
}
