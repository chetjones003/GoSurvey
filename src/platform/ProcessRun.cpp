#include "ProcessRun.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

std::wstring WideFromUtf8(const std::string& utf8) {
  if (utf8.empty())
    return std::wstring();
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
  if (n <= 0)
    return std::wstring();
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &w[0], n);
  return w;
}

/// Quotes one argument per the rules CommandLineToArgvW uses: backslashes are literal except
/// when they immediately precede the closing quote, where they must be doubled.
void AppendQuotedArg(std::wstring& cmdLine, const std::wstring& arg) {
  cmdLine.push_back(L'"');
  size_t backslashes = 0;
  for (const wchar_t c : arg) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      cmdLine.append(backslashes * 2 + 1, L'\\');
      backslashes = 0;
    } else if (backslashes) {
      cmdLine.append(backslashes, L'\\');
      backslashes = 0;
    }
    cmdLine.push_back(c);
  }
  cmdLine.append(backslashes * 2, L'\\');
  cmdLine.push_back(L'"');
}

} // namespace

bool RunProcessAndWait(const std::string& exePathUtf8,
                       const std::vector<std::string>& argsUtf8,
                       const std::string& workingDirUtf8,
                       int timeoutMs,
                       int* exitCodeOut) {
  if (exitCodeOut)
    *exitCodeOut = -1;
  if (exePathUtf8.empty())
    return false;

  const std::wstring wexe = WideFromUtf8(exePathUtf8);
  std::wstring cmdLine;
  AppendQuotedArg(cmdLine, wexe);
  for (const std::string& a : argsUtf8) {
    cmdLine.push_back(L' ');
    AppendQuotedArg(cmdLine, WideFromUtf8(a));
  }

  const std::wstring wcwd = WideFromUtf8(workingDirUtf8);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};

  // CreateProcessW may write to the command-line buffer, so it must be mutable.
  std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
  mutableCmd.push_back(L'\0');

  const BOOL ok = CreateProcessW(wexe.c_str(),
                                 mutableCmd.data(),
                                 nullptr,
                                 nullptr,
                                 FALSE,
                                 CREATE_NO_WINDOW,
                                 nullptr,
                                 wcwd.empty() ? nullptr : wcwd.c_str(),
                                 &si,
                                 &pi);
  if (!ok)
    return false;

  const DWORD waited = WaitForSingleObject(pi.hProcess, timeoutMs > 0 ? static_cast<DWORD>(timeoutMs) : INFINITE);
  bool completed = false;
  if (waited == WAIT_OBJECT_0) {
    DWORD code = 0;
    if (GetExitCodeProcess(pi.hProcess, &code) && exitCodeOut)
      *exitCodeOut = static_cast<int>(code);
    completed = true;
  } else {
    // Timed out (or the wait itself failed): kill the child so it cannot outlive the app.
    TerminateProcess(pi.hProcess, 1);
  }

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return completed;
}

#else

bool RunProcessAndWait(const std::string&,
                       const std::vector<std::string>&,
                       const std::string&,
                       int,
                       int* exitCodeOut) {
  if (exitCodeOut)
    *exitCodeOut = -1;
  return false;
}

#endif
