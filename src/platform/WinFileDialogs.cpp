#include "WinFileDialogs.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include <cstring>
#include <cwchar>

namespace {

void Utf8ToWide(const char* utf8, wchar_t* wbuf, int wcap) {
  if (!utf8 || !wbuf || wcap <= 0) {
    if (wbuf && wcap > 0)
      wbuf[0] = L'\0';
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf, wcap);
}

bool WideToUtf8(const wchar_t* wstr, char* out, size_t cap) {
  if (!wstr || !out || cap == 0)
    return false;
  const int n = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out, static_cast<int>(cap), nullptr, nullptr);
  return n > 0;
}

} // namespace

bool BrowseOpenFileCsvUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"CSV (*.csv)\0*.csv\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn))
    return false;
  return WideToUtf8(wfile, utf8Out, utf8Cap);
}

bool BrowseOpenFileDxfUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Drawing Exchange (*.dxf)\0*.dxf\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn))
    return false;
  return WideToUtf8(wfile, utf8Out, utf8Cap);
}

bool BrowseOpenFileGsUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"GoSurvey (*.gs)\0*.gs\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn))
    return false;
  return WideToUtf8(wfile, utf8Out, utf8Cap);
}

bool BrowseOpenFilePdfUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"PDF (*.pdf)\0*.pdf\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn))
    return false;
  return WideToUtf8(wfile, utf8Out, utf8Cap);
}

bool BrowseOpenFileFbkUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Autodesk Field Book (*.fbk)\0*.fbk\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn))
    return false;
  return WideToUtf8(wfile, utf8Out, utf8Cap);
}

bool BrowseSaveFileGsUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  if (defaultNameUtf8 && defaultNameUtf8[0] != '\0')
    Utf8ToWide(defaultNameUtf8, wfile, MAX_PATH);
  else
    wcscpy_s(wfile, L"drawing.gs");

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"GoSurvey (*.gs)\0*.gs\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&ofn))
    return false;

  wchar_t path[MAX_PATH]{};
  wcscpy_s(path, wfile);
  const size_t L = wcslen(path);
  const bool hasExt = L >= 3 && (_wcsicmp(path + L - 3, L".gs") == 0);
  if (!hasExt && L + 3 < MAX_PATH)
    wcscat_s(path, MAX_PATH, L".gs");
  return WideToUtf8(path, utf8Out, utf8Cap);
}

bool BrowseSaveFileDxfUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  if (defaultNameUtf8 && defaultNameUtf8[0] != '\0')
    Utf8ToWide(defaultNameUtf8, wfile, MAX_PATH);
  else
    wcscpy_s(wfile, L"drawing.dxf");

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Drawing Exchange (*.dxf)\0*.dxf\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&ofn))
    return false;

  wchar_t path[MAX_PATH]{};
  wcscpy_s(path, wfile);
  const size_t L = wcslen(path);
  const bool hasExt =
      L >= 4 && (_wcsicmp(path + L - 4, L".dxf") == 0);
  if (!hasExt && L + 4 < MAX_PATH)
    wcscat_s(path, MAX_PATH, L".dxf");
  return WideToUtf8(path, utf8Out, utf8Cap);
}

bool BrowseSaveFilePdfUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  if (defaultNameUtf8 && defaultNameUtf8[0] != '\0')
    Utf8ToWide(defaultNameUtf8, wfile, MAX_PATH);
  else
    wcscpy_s(wfile, L"plot.pdf");

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"PDF (*.pdf)\0*.pdf\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&ofn))
    return false;

  wchar_t path[MAX_PATH]{};
  wcscpy_s(path, wfile);
  const size_t L = wcslen(path);
  const bool hasExt = L >= 4 && (_wcsicmp(path + L - 4, L".pdf") == 0);
  if (!hasExt && L + 4 < MAX_PATH)
    wcscat_s(path, MAX_PATH, L".pdf");
  return WideToUtf8(path, utf8Out, utf8Cap);
}

bool BrowseSaveFileCsvUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  if (defaultNameUtf8 && defaultNameUtf8[0] != '\0')
    Utf8ToWide(defaultNameUtf8, wfile, MAX_PATH);
  else
    wcscpy_s(wfile, L"points.csv");

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"CSV (*.csv)\0*.csv\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&ofn))
    return false;

  wchar_t path[MAX_PATH]{};
  wcscpy_s(path, wfile);
  const size_t L = wcslen(path);
  const bool hasCsv =
      L >= 4 && (_wcsicmp(path + L - 4, L".csv") == 0);
  if (!hasCsv && L + 4 < MAX_PATH)
    wcscat_s(path, MAX_PATH, L".csv");
  return WideToUtf8(path, utf8Out, utf8Cap);
}

#else

bool BrowseOpenFileCsvUtf8(char* utf8Out, size_t utf8Cap) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseSaveFileCsvUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseOpenFileDxfUtf8(char* utf8Out, size_t utf8Cap) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseSaveFileDxfUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseOpenFileGsUtf8(char* utf8Out, size_t utf8Cap) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseSaveFileGsUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseSaveFilePdfUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseOpenFilePdfUtf8(char* utf8Out, size_t utf8Cap) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseOpenFileFbkUtf8(char* utf8Out, size_t utf8Cap) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

#endif
