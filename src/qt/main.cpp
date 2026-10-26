#include "CrashReporter.hpp"
#include "EditorWindow.hpp"
#include "FailureReport.hpp"
#include "QtAppearance.hpp"
#include "QtSplash.hpp"
#include "PdfAttach.hpp"
#include "StartupFailure.hpp"
#include "UserPrefs.hpp"
#include <QApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QImage>
#include <QPalette>
#include <QSettings>
#include <QSplashScreen>
#include <QThread>
#include <QSurfaceFormat>
#include <QTimer>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
  int minidumpWriterExitCode = 1;
  if (crashReporter::RunMinidumpWriterMode(minidumpWriterExitCode))
    return minidumpWriterExitCode;

  // REQ-425 §2: a process relaunched by the crash handler only shows the report. It is read (and
  // sent) before the handler is installed, so a fault here cannot recurse.
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
  // REQ-425 §4: perform a launch-time graphics check. Skipped for the smoke
  // run, where a modal report window would hang an unattended check.
  if (!smoke) {
    startupFailure::Stage stage{};
    std::string detail;
    if (!ProbeStartupGraphics(stage, detail)) {
      ExecFailureReportDialog("GoSurvey could not start", startupFailure::RecordAndSend(stage, detail));
      return 1;
    }
  }
  const QPixmap splashBackdrop = qtsplash::MakeBackdrop(QPixmap(QtAsset("qt-splash-bg.png")));
  QSplashScreen splash(qtsplash::MakeFrame(splashBackdrop, 0.0, qtsplash::PhaseLabelAt(0)));
  // REQ-093: the bar and phase label follow wall-clock time on splashClock (restarted once the window is built).
  QElapsedTimer splashClock;
  const auto advanceSplash = [&] {
    const qint64 elapsed = splashClock.elapsed();
    splash.setPixmap(qtsplash::MakeFrame(splashBackdrop, qtsplash::ProgressAt(elapsed),
                                         qtsplash::PhaseLabelAt(elapsed)));
    app.processEvents(QEventLoop::AllEvents, 50);
  };
  if (!smoke) { splash.show(); splashClock.start(); app.processEvents(); }
  if (!PdfAttach_Init()) {
    std::fprintf(stderr, "PDFium initialization failed\n");
    return 1;
  }
  int result = 0;
  {
    EditorWindow window(qEnvironmentVariable("GOSURVEY_DEV_BRANCH"));
    LoadUserStartupPrefSettings(window.State());
    // REQ-093: the minimum display time counts from here, so the bar animates for its whole span
    // instead of sitting still while the window above is being built.
    if (!smoke) { splashClock.restart(); advanceSplash(); }
    crashReporter::SetReportChannel(window.State().updatePrefs.useBetaChannel ? "beta" : "stable");
    if (!smoke) {
      // REQ-080: launch the anonymous ping on its own worker immediately; no network result
      // participates in splash timing, update handling, or opening the editor.
      window.EnableTelemetry();
      // The update offer, when there is one, is shown before the editor (REQ-077).
      window.StartUpdateCheck();
      // REQ-077: the check gates the session. The splash stays up until it has an answer
      // (bounded by the check's own short timeout), so no drawing can begin on a build that is
      // about to offer to replace itself. REQ-093: and for at least the minimum display time.
      while (window.UpdateCheckRunning() || splashClock.elapsed() < qtsplash::kMinimumDisplayMs) {
        advanceSplash();
        QThread::msleep(15);
      }
      advanceSplash();
    }
    window.showMaximized();
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
        if (argument != "--default-style" && argument != "--light-style") { window.OpenDrawing(argument); break; }
      // Load a command-line drawing before deciding whether the Start screen should auto-open
      // What's New. The window is visible now, so startup dialogs have a valid owner.
      window.InitializeStartup();
    }
    result = app.exec();
  } // Release viewport resources and PDF worker windows before shutting down PDFium.
  PdfAttach_Shutdown();
  return result;
}
