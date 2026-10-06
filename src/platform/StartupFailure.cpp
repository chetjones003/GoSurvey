#include "StartupFailure.hpp"

#include "AppPaths.hpp"
#include "Version.hpp"

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

#ifdef _WIN32
int    g_lastGlfwErrorCode = 0;
std::string g_lastGlfwErrorText;

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty())
    return std::wstring();
  const int need =
      ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
  if (need <= 0)
    return std::wstring();
  std::wstring out(static_cast<size_t>(need), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), need);
  return out;
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

constexpr int kEditId  = 1001;
constexpr int kCloseId = 1002;

LRESULT CALLBACK FailureWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_COMMAND:
    if (LOWORD(wParam) == kCloseId) {
      DestroyWindow(hwnd);
      return 0;
    }
    break;
  case WM_CLOSE:
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowFailureDialog(const std::wstring& body) {
  const wchar_t* kClass = L"GoSurveyStartupFailureV1";
  HINSTANCE          inst = ::GetModuleHandleW(nullptr);
  static bool        registered = false;
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = FailureWindowProc;
    wc.hInstance     = inst;
    wc.lpszClassName = kClass;
    wc.hCursor       = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    ::RegisterClassExW(&wc);
    registered = true;
  }

  const int winW = 720;
  const int winH = 420;
  int       posX = 100;
  int       posY = 100;
  if (HWND desktop = ::GetDesktopWindow()) {
    RECT rc{};
    if (::GetWindowRect(desktop, &rc)) {
      posX = rc.left + (rc.right - rc.left - winW) / 2;
      posY = rc.top + (rc.bottom - rc.top - winH) / 2;
    }
  }

  HWND hwnd = ::CreateWindowExW(
      WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME, kClass, L"GoSurvey could not start",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, posX, posY, winW, winH, nullptr, nullptr, inst,
      nullptr);
  if (!hwnd) {
    ::MessageBoxW(nullptr, body.c_str(), L"GoSurvey could not start", MB_OK | MB_ICONERROR);
    return;
  }

  ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", body.c_str(),
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                    12, 12, winW - 36, winH - 72, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
                    inst, nullptr);

  HWND btn = ::CreateWindowExW(
      0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, winW - 120, winH - 52, 96,
      28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseId)), inst, nullptr);
  if (btn)
    ::SetFocus(btn);

  ::ShowWindow(hwnd, SW_SHOW);
  ::UpdateWindow(hwnd);

  MSG message{};
  while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (!::IsDialogMessageW(hwnd, &message)) {
      ::TranslateMessage(&message);
      ::DispatchMessageW(&message);
    }
  }
}
#endif  // _WIN32

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

  std::fprintf(stderr, "%s\n", body.c_str());

#ifdef _WIN32
  ShowFailureDialog(Utf8ToWide(body));
  ::ExitProcess(1);
#else
  std::exit(1);
#endif
}

}  // namespace startupFailure
