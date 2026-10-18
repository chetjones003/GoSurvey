#include "CrashReporter.hpp"
#include "EditorWindow.hpp"
#include "FailureReport.hpp"
#include "QtAppearance.hpp"
#include "PdfAttach.hpp"
#include "StartupFailure.hpp"
#include "UserPrefs.hpp"
#include <QApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QSettings>
#include <QSplashScreen>
#include <QThread>
#include <QSurfaceFormat>
#include <QTimer>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
  // REQ-425 §2: a process relaunched by the crash handler only shows the report. It is read (and
  // sent) before the handler is installed, as in the ImGui build, so a fault here cannot recurse.
  std::string crashTitle;
  std::string crashReport;
  const bool crashReportMode = crashReporter::TakeCrashReportFromCommandLine(crashTitle, crashReport);
  if (!crashReportMode) {
    crashReporter::InstallHandler();
    if (crashReporter::CommandLineHasTestCrash()) crashReporter::TriggerTestCrash();
  }
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
  if (crashReportMode) {
    ExecFailureReportDialog(crashTitle, crashReport);
    return 0;
  }
  const bool smoke = app.arguments().contains("--smoke-test");
  // REQ-425 §4: the same launch-time graphics check the ImGui build makes. Skipped for the smoke
  // run, where a modal report window would hang an unattended check.
  if (!smoke) {
    startupFailure::Stage stage{};
    std::string detail;
    if (!ProbeStartupGraphics(stage, detail)) {
      ExecFailureReportDialog("GoSurvey could not start", startupFailure::RecordAndSend(stage, detail));
      return 1;
    }
  }
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
    crashReporter::SetReportChannel(window.State().updatePrefs.useBetaChannel ? "beta" : "stable");
    if (!smoke) {
      // Started before sign-in so the update offer, when there is one, is shown first (REQ-077).
      window.StartUpdateCheck();
      // REQ-077: the check gates the session. The splash stays up until it has an answer
      // (bounded by the check's own short timeout), so no drawing can begin on a build that is
      // about to offer to replace itself.
      while (window.UpdateCheckRunning()) {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(15);
      }
      // REQ-080: the ping goes out once the sign-in gate below resolves.
      window.EnableTelemetry();
      window.StartAuth(previewSignIn);
    }
    window.show();
    splash.finish(&window);
    // Startup dialogs need a visible owner, and What's New is eligible only after the
    // Start tab has been established by the initial Refresh().
    if (!smoke) window.InitializeStartup();
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
