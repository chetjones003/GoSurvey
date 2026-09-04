#include "WinFileDialogs.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

#include "util/PointFileExt.hpp"

#include <cstring>
#include <cwchar>

namespace {

// REQ-083: a point file is comma-delimited content that may be spelled `.csv` or `.txt`, so both
// choosers offer the pair first and the single-extension entries after it for a user who wants to
// narrow the listing. Index 3 is the Text-only entry — the one signal the common dialog gives that
// an extension-less name was meant to be `.txt`.
const wchar_t* const kPointFileFilter = L"Point file (*.csv;*.txt)\0*.csv;*.txt\0"
                                        L"CSV (*.csv)\0*.csv\0"
                                        L"Text (*.txt)\0*.txt\0"
                                        L"All (*.*)\0*.*\0\0";
constexpr int kPointFileTxtFilterIndex = 3;

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

/// ImGui's command InputText can still hold Win32 mouse capture when Enter submits. GetOpenFileNameW
/// then returns false with no window. File-menu clicks do not leave capture set, which is why those
/// pickers appear and a typed BLOCKIMPORT did not.
void PrepareNativeFileDialog(OPENFILENAMEW* ofn) {
  ReleaseCapture();
  HWND owner = GetActiveWindow();
  if (!owner)
    owner = GetForegroundWindow();
  ofn->hwndOwner = owner;
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
  ofn.lpstrFilter = kPointFileFilter;
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

bool BrowseOpenFileDwgUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"AutoCAD Drawing (*.dwg)\0*.dwg\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn))
    return false;
  return WideToUtf8(wfile, utf8Out, utf8Cap);
}

bool BrowseSaveFileDwgUtf8(char* utf8Out, size_t utf8Cap, const char* defaultNameUtf8) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  if (defaultNameUtf8 && defaultNameUtf8[0] != '\0')
    Utf8ToWide(defaultNameUtf8, wfile, MAX_PATH);
  else
    wcscpy_s(wfile, L"drawing.dwg");

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"AutoCAD Drawing (*.dwg)\0*.dwg\0All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&ofn))
    return false;

  wchar_t path[MAX_PATH]{};
  wcscpy_s(path, wfile);
  const size_t L = wcslen(path);
  const bool hasExt = L >= 4 && (_wcsicmp(path + L - 4, L".dwg") == 0);
  if (!hasExt && L + 4 < MAX_PATH)
    wcscat_s(path, MAX_PATH, L".dwg");
  return WideToUtf8(path, utf8Out, utf8Cap);
}

bool BrowseOpenFileGstUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"GoSurvey Template (*.gst)\0*.gst\0All (*.*)\0*.*\0\0";
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

bool BrowseOpenFileGltfUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  // DWG is listed with the model formats because IMPORTMODEL converts it transparently via an
  // installed AutoCAD — from the user's side it is just another model file (REQ-065).
  ofn.lpstrFilter = L"3D models (*.glb;*.gltf;*.stl;*.dwg)\0*.glb;*.gltf;*.stl;*.dwg\0"
                    L"glTF (*.glb;*.gltf)\0*.glb;*.gltf\0"
                    L"STL (*.stl)\0*.stl\0"
                    L"AutoCAD drawing (*.dwg)\0*.dwg\0"
                    L"All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn))
    return false;
  return WideToUtf8(wfile, utf8Out, utf8Cap);
}

bool BrowseOpenFileBlockUtf8(char* utf8Out, size_t utf8Cap) {
  if (!utf8Out || utf8Cap < 4)
    return false;
  wchar_t wfile[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = wfile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Import Block";
  ofn.lpstrFilter = L"Blocks (*.dxf;*.dwg)\0*.dxf;*.dwg\0"
                    L"Drawing Exchange (*.dxf)\0*.dxf\0"
                    L"AutoCAD Drawing (*.dwg)\0*.dwg\0"
                    L"All (*.*)\0*.*\0\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | OFN_ENABLESIZING;
  PrepareNativeFileDialog(&ofn);
  if (!GetOpenFileNameW(&ofn))
    return false;
  return WideToUtf8(wfile, utf8Out, utf8Cap);
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
  ofn.lpstrFilter = kPointFileFilter;
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
  if (!GetSaveFileNameW(&ofn))
    return false;

  if (!WideToUtf8(wfile, utf8Out, utf8Cap))
    return false;

  // REQ-083: the name is converted first so the extension rule can be a pure, tested function over
  // narrow characters. A name already spelled `.csv` or `.txt` is written exactly as typed —
  // appending unconditionally, as this did before, produced `points.txt.csv`.
  const bool txtFilterChosen = ofn.nFilterIndex == kPointFileTxtFilterIndex;
  const std::string_view ext = pointfile::ExtensionToAppend(utf8Out, txtFilterChosen);
  if (!ext.empty()) {
    const size_t len = std::strlen(utf8Out);
    if (len + ext.size() + 1 > utf8Cap)
      return false; // no room to name the file correctly; better no path than a truncated one
    std::memcpy(utf8Out + len, ext.data(), ext.size());
    utf8Out[len + ext.size()] = '\0';
  }
  return true;
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

bool BrowseOpenFileDwgUtf8(char* utf8Out, size_t utf8Cap) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseSaveFileDwgUtf8(char* utf8Out, size_t utf8Cap, const char*) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseOpenFileGstUtf8(char* utf8Out, size_t utf8Cap) {
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

bool BrowseOpenFileGltfUtf8(char* utf8Out, size_t utf8Cap) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

bool BrowseOpenFileBlockUtf8(char* utf8Out, size_t utf8Cap) {
  if (utf8Out && utf8Cap > 0)
    utf8Out[0] = '\0';
  (void)utf8Cap;
  return false;
}

#endif
