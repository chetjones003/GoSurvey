#include "DwgIo.hpp"

#include "GsIo.hpp"
#include "LibreDwgCad.hpp"
#include "util/SaveTrace.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

// REQ-175 / ADR-044: after a valid LibreDWG file, JSON document + uint64 LE length + 16-byte magic.
constexpr char kMagic[16] = {'G', 'O', 'S', 'U', 'R', 'V', 'E', 'Y', '_', 'D', 'O', 'C', 'v', '1', '\n', '\0'};
constexpr size_t kMagicLen = 16;
constexpr size_t kLenLen = 8;
constexpr size_t kFooter = kMagicLen + kLenLen;

bool WriteU64LeFile(std::FILE* fp, std::uint64_t n) {
  if (fp == nullptr)
    return false;
  unsigned char b[8];
  for (int i = 0; i < 8; ++i)
    b[static_cast<size_t>(i)] = static_cast<unsigned char>((n >> (8 * i)) & 0xFFu);
  return std::fwrite(b, 1, 8, fp) == 8;
}

bool OpenBinaryAppendUtf8(const char* pathUtf8, std::FILE** fpOut) {
  if (fpOut == nullptr)
    return false;
  *fpOut = nullptr;
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0')
    return false;
#if defined(_MSC_VER)
  return _wfopen_s(fpOut, std::filesystem::u8path(pathUtf8).wstring().c_str(), L"ab") == 0 &&
         *fpOut != nullptr;
#else
  *fpOut = std::fopen(pathUtf8, "ab");
  return *fpOut != nullptr;
#endif
}

bool ReadU64Le(std::string_view bytes, std::uint64_t* nOut) {
  if (bytes.size() < 8 || nOut == nullptr)
    return false;
  std::uint64_t n = 0;
  for (int i = 0; i < 8; ++i)
    n |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[static_cast<size_t>(i)])) << (8 * i);
  *nOut = n;
  return true;
}

}  // namespace

bool AppendGoSurveyPayloadToDwgFile(const char* pathUtf8, const AppCommandState& st,
                                      std::vector<std::string>& log) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    log.push_back("DWG save — no path for GoSurvey document append.");
    return false;
  }
  AppendSaveTrace("append: serialize json");
  const std::string json = SerializeGoSurveyJson(st);
  AppendSaveTrace("append: json ready");
  // The file was just written by LibreDWG; a sync client / antivirus can still hold it open
  // briefly (issue #167). Retry the append open with a short bounded backoff before giving up.
  //
  // Use stdio (_wfopen on Windows) rather than std::ofstream: after the native Save-As dialog
  // returns, MSVC's iostream locale can be left in a bad state and crash on the first write
  // (access violation reading [null+0xC4] inside the standard library).
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (attempt > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));
    std::FILE* fp = nullptr;
    if (!OpenBinaryAppendUtf8(pathUtf8, &fp))
      continue;
    const bool payloadOk =
        std::fwrite(json.data(), 1, json.size(), fp) == json.size() &&
        WriteU64LeFile(fp, static_cast<std::uint64_t>(json.size())) &&
        std::fwrite(kMagic, 1, kMagicLen, fp) == kMagicLen && !std::ferror(fp);
    const int closeErr = std::fclose(fp);
    if (payloadOk && closeErr == 0) {
      log.push_back("DWG save — GoSurvey document preserved in file.");
      return true;
    }
  }
  log.push_back(std::string("DWG save — could not append GoSurvey document to ") + pathUtf8);
  return false;
}

bool TryGoSurveyDwgPayloadFromBytes(std::string_view fileBytes, std::string& jsonOut) {
  jsonOut.clear();
  if (fileBytes.size() < kFooter)
    return false;
  const std::string_view magic = fileBytes.substr(fileBytes.size() - kMagicLen, kMagicLen);
  if (std::memcmp(magic.data(), kMagic, kMagicLen) != 0)
    return false;
  std::uint64_t n = 0;
  if (!ReadU64Le(fileBytes.substr(fileBytes.size() - kFooter, kLenLen), &n))
    return false;
  if (n > fileBytes.size() - kFooter)
    return false;
  jsonOut.assign(fileBytes.substr(fileBytes.size() - kFooter - static_cast<size_t>(n), static_cast<size_t>(n)));
  return true;
}

bool ImportDwgFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    log.push_back("DWG import — no path.");
    return false;
  }
  std::ifstream in(std::filesystem::u8path(pathUtf8), std::ios::binary);
  if (!in) {
    log.push_back(std::string("DWG import — could not open ") + pathUtf8);
    return false;
  }
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string json;
  if (TryGoSurveyDwgPayloadFromBytes(bytes, json)) {
    log.push_back(std::string("DWG open — GoSurvey document in ") + pathUtf8);
    return LoadGoSurveyFromJsonUtf8(st, json, log);
  }
  return ImportLibreCadFile(st, pathUtf8, log, /*asDxf=*/false);
}

bool ExportDwgFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  // Build the complete file (LibreDWG bytes + GoSurvey trailer) beside the target, then replace the
  // target in one step. The final path may be a OneDrive placeholder that briefly locks after a
  // write (issue #167); staging avoids an export-then-reopen race on it.
  AppendSaveTrace("export: begin");
  std::error_code ec;
  const std::filesystem::path dst = std::filesystem::u8path(pathUtf8);
  const std::filesystem::path staged = std::filesystem::path(dst.u8string() + ".gosurvey-save.tmp");
  std::filesystem::remove(staged, ec);
  const std::string stagedUtf8 = staged.u8string();

  AppendSaveTrace("export: libredwg write");
  if (!ExportLibreCadFile(st, stagedUtf8.c_str(), log, /*asDxf=*/false)) {
    std::filesystem::remove(staged, ec);
    AppendSaveTrace("export: libredwg failed");
    return false;
  }
  AppendSaveTrace("export: append trailer");
  if (!AppendGoSurveyPayloadToDwgFile(stagedUtf8.c_str(), st, log)) {
    std::filesystem::remove(staged, ec);
    log.push_back("DWG save — file was NOT written; the GoSurvey document could not be embedded.");
    AppendSaveTrace("export: append failed");
    return false;
  }

  AppendSaveTrace("export: rename staged file");
  std::filesystem::rename(staged, dst, ec);
  if (ec) {
    ec.clear();
    std::filesystem::copy_file(staged, dst, std::filesystem::copy_options::overwrite_existing, ec);
    std::error_code rmEc;
    std::filesystem::remove(staged, rmEc);
  }
  if (ec) {
    std::filesystem::remove(staged, ec);
    log.push_back(std::string("DWG save — could not replace ") + pathUtf8 + " with the new drawing.");
    AppendSaveTrace("export: rename failed");
    return false;
  }
  AppendSaveTrace("export: done");
  return true;
}

bool OpenDrawingDocument(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    log.push_back("Open drawing — no path.");
    return false;
  }
  return ImportDwgFile(st, pathUtf8, log);
}

bool SaveDrawingDocument(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    log.push_back("Save drawing — no path.");
    return false;
  }
  return ExportDwgFile(st, pathUtf8, log);
}
