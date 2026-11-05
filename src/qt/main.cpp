#include "CrashReporter.hpp"
#include "EditorWindow.hpp"
#include "FailureReport.hpp"
#include "QtAppearance.hpp"
#include "QtSplash.hpp"
#include "PdfAttach.hpp"
#include "StartupFailure.hpp"
#include "UserPrefs.hpp"
#include <QApplication>
#include <QGuiApplication>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QImage>
#include <QPalette>
#include <QSettings>
#include <QSplashScreen>
#include <QScreen>
#include <QThread>
#include <QSurfaceFormat>
#include <QTimer>
#include <cstdio>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <algorithm>
#include <cwctype>
#endif

#ifdef _WIN32
namespace {
constexpr ULONG_PTR kOpenDrawingCopyData = 0x47534457; // "GSDW"

std::wstring LowerPath(std::wstring path) {
  std::transform(path.begin(), path.end(), path.begin(), [](wchar_t ch) { return std::towlower(ch); });
  return path;
}

struct WindowSearch {
  std::wstring executablePath;
  HWND window = nullptr;
};

BOOL CALLBACK FindGoSurveyWindow(HWND hwnd, LPARAM parameter) {
  auto* search = reinterpret_cast<WindowSearch*>(parameter);
  wchar_t title[256]{};
  const int titleLength = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
  const std::wstring windowTitle = LowerPath(std::wstring(title, static_cast<size_t>(titleLength)));
  if (windowTitle != L"gosurvey" && windowTitle.rfind(L"gosurvey — ", 0) != 0)
    return TRUE;
  DWORD processId = 0;
  GetWindowThreadProcessId(hwnd, &processId);
  if (processId == 0 || !IsWindowVisible(hwnd))
    return TRUE;
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (process == nullptr)
    return TRUE;
  std::wstring path(32768, L'\0');
  DWORD length = static_cast<DWORD>(path.size());
  const BOOL queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
  CloseHandle(process);
  if (queried && LowerPath(path.substr(0, length)) == search->executablePath) {
    search->window = hwnd;
    return FALSE;
  }
  return TRUE;
}

// Claims the per-executable-path mutex. alreadyRunning reports whether another GoSurvey from this exact
// executable path holds it; the caller keeps the handle either way for the process lifetime.
HANDLE CreateInstanceMutex(bool& alreadyRunning) {
  std::vector<wchar_t> path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  alreadyRunning = false;
  if (length == 0 || length >= path.size())
    return nullptr;
  const std::wstring canonical = LowerPath(std::wstring(path.data(), length));
  // FNV-1a gives a stable per-executable-path mutex name across GoSurvey instances.
  unsigned long long hash = 14695981039346656037ull;
  for (wchar_t ch : canonical) {
    hash ^= static_cast<unsigned long long>(ch);
    hash *= 1099511628211ull;
  }
  wchar_t name[64]{};
  swprintf_s(name, L"Local\\GoSurvey-%016llX", hash);
  HANDLE mutex = CreateMutexW(nullptr, FALSE, name);
  alreadyRunning = mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
  return mutex;
}

bool ForwardDrawingToExistingInstance(const QString& drawingPath) {
  std::vector<wchar_t> module(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
  if (length == 0 || length >= module.size())
    return false;
  WindowSearch search{LowerPath(std::wstring(module.data(), length)), nullptr};
  const ULONGLONG deadline = GetTickCount64() + 30000;
  while (GetTickCount64() < deadline) {
    EnumWindows(FindGoSurveyWindow, reinterpret_cast<LPARAM>(&search));
    if (search.window != nullptr) {
      const std::wstring path = drawingPath.toStdWString();
      COPYDATASTRUCT data{};
      data.dwData = kOpenDrawingCopyData;
      data.cbData = static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t));
      data.lpData = const_cast<wchar_t*>(path.c_str());
      DWORD_PTR result = 0;
      const LRESULT sent = SendMessageTimeoutW(search.window, WM_COPYDATA, 0,
                                               reinterpret_cast<LPARAM>(&data),
                                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result);
      if (sent != 0 && result != 0) {
        ShowWindow(search.window, SW_RESTORE);
        SetForegroundWindow(search.window);
        return true;
      }
      // A matching process can still be initializing. Re-enumerate and retry once
      // its editor window is ready to receive the handoff.
      search.window = nullptr;
    }
    Sleep(50);
  }
  return false;
}
} // namespace
#endif

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
#ifdef _WIN32
  // REQ-456: a DWG handed over by Windows file association opens in the running GoSurvey from this
  // executable path. With no such window, or for any other launch, this process continues normally.
  HANDLE instanceMutex = nullptr;
  if (!smoke && !app.arguments().contains("--test-crash")) {
    bool alreadyRunning = false;
    instanceMutex = CreateInstanceMutex(alreadyRunning);
    if (alreadyRunning) {
      for (const QString& argument : app.arguments().mid(1)) {
        if (argument.startsWith('-'))
          continue;
        if (argument.endsWith(".dwg", Qt::CaseInsensitive)) {
          // Resolve relative file-association/command-line arguments in the launching process;
          // the existing editor can have a different current working directory.
          const QString absolutePath = QFileInfo(argument).absoluteFilePath();
          if (ForwardDrawingToExistingInstance(absolutePath)) {
            CloseHandle(instanceMutex);
            return 0;
          }
        }
        break;
      }
    }
  }
#endif
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
  const qreal splashDpr =
      QGuiApplication::primaryScreen() != nullptr ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
  const QPixmap splashBackdrop = qtsplash::MakeBackdrop(QPixmap(QtAsset("qt-splash-bg.png")), splashDpr);
  QSplashScreen splash(qtsplash::MakeFrame(splashBackdrop, 0.0, qtsplash::PhaseLabelAt(0), splashDpr));
  // REQ-093: the bar and phase label follow wall-clock time on splashClock (restarted once the window is built).
  QElapsedTimer splashClock;
  const auto advanceSplash = [&] {
    const qint64 elapsed = splashClock.elapsed();
    splash.setPixmap(qtsplash::MakeFrame(splashBackdrop, qtsplash::ProgressAt(elapsed),
                                         qtsplash::PhaseLabelAt(elapsed), splashDpr));
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
    LoadUserStartupPrefs(window.State());
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
#ifdef _WIN32
  if (instanceMutex != nullptr)
    CloseHandle(instanceMutex);
#endif
  return result;
}
