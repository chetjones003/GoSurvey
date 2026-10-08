#include "StartupFailure.hpp"

#include "AppPaths.hpp"
#include "FailureReportUi.hpp"
#include "HttpFetch.hpp"
#include "TelemetryPing.hpp"
#include "UserPrefs.hpp"
#include "Version.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace startupFailure {
namespace {

const char* StageWireName(Stage stage) {
  switch (stage) {
  case Stage::GlfwInit:
    return "glfw_init";
  case Stage::GlfwCreateWindow:
    return "glfw_window";
  case Stage::OpenGlInit:
    return "opengl_init";
  }
  return "glfw_init";
}

void TryPostStartupReportSilent(Stage stage, const std::string& reportUtf8) {
#ifdef _WIN32
  if (!HasInternetConnectivity())
    return;

  std::string report = reportUtf8;
  constexpr size_t kMaxReport = 16384;
  if (report.size() > kMaxReport)
    report.resize(kMaxReport);

  const TelemetryIds ids = GetTelemetryIds();
  nlohmann::json body;
  body["installId"] = ids.installId.empty() ? "unknown" : ids.installId;
  body["version"]     = GOSURVEY_VERSION_FULL;
  body["channel"]     = "stable";
  body["os"]          = "windows";
  body["stage"]       = StageWireName(stage);
  body["report"]      = report;

  std::string error;
  std::string response;
  (void)HttpPostJson(StartupReportEndpoint, body.dump(), 8000, error, &response);
#else
  (void)stage;
  (void)reportUtf8;
#endif
}

std::string StageSummary(Stage stage) {
  switch (stage) {
  case Stage::GlfwInit:
    return "GoSurvey could not initialize its display library (GLFW).";
  case Stage::GlfwCreateWindow:
    return "GoSurvey could not create a graphics window.\n\n"
           "This usually means OpenGL 3.3 is not available from your graphics driver. "
           "OpenGL is provided by your GPU driver (it is not a separate installer like Visual C++).\n\n"
           "Try: Windows Update (Optional updates → driver updates), or install the latest driver "
           "from your PC or GPU vendor. Remote desktop and virtual machines often need 3D "
           "acceleration enabled.";
  case Stage::OpenGlInit:
    return "GoSurvey could not initialize OpenGL 3.3 for drawing.\n\n"
           "Update your graphics driver, or in Settings → System try turning off Hardware "
           "Acceleration after you can open the app from a machine that works.";
  }
  return "GoSurvey could not start.";
}

#ifdef _WIN32
int         g_lastGlfwErrorCode = 0;
std::string g_lastGlfwErrorText;
#endif

std::filesystem::path StartupFailureLogPath() {
  const std::filesystem::path dir = UserDataDirectory();
  if (dir.empty())
    return std::filesystem::path("startup-failure.log");
  return dir / "startup-failure.log";
}

void WriteStartupFailureLog(Stage stage, const std::string& detailUtf8) {
  const std::filesystem::path logPath = StartupFailureLogPath();
  std::error_code             ec;
  if (!logPath.parent_path().empty())
    std::filesystem::create_directories(logPath.parent_path(), ec);

  std::ofstream out(logPath, std::ios::app);
  if (!out)
    return;

  const std::time_t t = std::time(nullptr);
  char              timeBuf[32] = "0000-00-00 00:00:00";
  struct tm         tmInfo{};
  if (localtime_s(&tmInfo, &t) == 0)
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);

  out << "---- " << timeBuf << " ----\n";
  out << "version: " << GOSURVEY_VERSION_FULL << "\n";
  out << "stage: " << static_cast<int>(stage) << "\n";
  out << StageSummary(stage) << "\n";
  if (!detailUtf8.empty())
    out << "detail: " << detailUtf8 << "\n";
#ifdef _WIN32
  if (g_lastGlfwErrorCode != 0 || !g_lastGlfwErrorText.empty()) {
    out << "last_glfw_error: " << g_lastGlfwErrorCode << " " << g_lastGlfwErrorText << "\n";
  }
#endif
  out << "log_file: " << logPath.u8string() << "\n\n";
}

}  // namespace

void NoteGlfwError(int code, const char* description) {
#ifdef _WIN32
  g_lastGlfwErrorCode = code;
  g_lastGlfwErrorText = (description && description[0]) ? description : std::string();
#endif
  (void)code;
  (void)description;
}

[[noreturn]] void FailAndShow(Stage stage, const std::string& detailUtf8) {
  WriteStartupFailureLog(stage, detailUtf8);

  const std::string summary = StageSummary(stage);
  std::string       body    = summary;
  if (!detailUtf8.empty()) {
    body += "\n\nTechnical detail:\n";
    body += detailUtf8;
  }
#ifdef _WIN32
  if (g_lastGlfwErrorCode != 0 || !g_lastGlfwErrorText.empty()) {
    body += "\n\nLast GLFW message:\n";
    body += std::to_string(g_lastGlfwErrorCode);
    body += ": ";
    body += g_lastGlfwErrorText;
  }
#endif
  body += "\n\nA copy of this report was appended to:\n";
  body += StartupFailureLogPath().u8string();
  body += "\n\nUse \"Send This Report\" to copy this text and open GitHub to file an issue.";

  std::fprintf(stderr, "%s\n", body.c_str());

  TryPostStartupReportSilent(stage, body);

#ifdef _WIN32
  failureReportUi::ShowReportDialog(L"GoSurvey could not start", body);
  ::ExitProcess(1);
#else
  std::exit(1);
#endif
}

}  // namespace startupFailure
