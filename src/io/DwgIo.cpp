#include "DwgIo.hpp"

#include "GsIo.hpp"
#include "LibreDwgCad.hpp"
#include "PointFileExt.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace {

// REQ-175 / ADR-044: after a valid LibreDWG file, JSON document + uint64 LE length + 16-byte magic.
constexpr char kMagic[16] = {'G', 'O', 'S', 'U', 'R', 'V', 'E', 'Y', '_', 'D', 'O', 'C', 'v', '1', '\n', '\0'};
constexpr size_t kMagicLen = 16;
constexpr size_t kLenLen = 8;
constexpr size_t kFooter = kMagicLen + kLenLen;

void WriteU64Le(std::ostream& out, std::uint64_t n) {
  unsigned char b[8];
  for (int i = 0; i < 8; ++i)
    b[static_cast<size_t>(i)] = static_cast<unsigned char>((n >> (8 * i)) & 0xFFu);
  out.write(reinterpret_cast<const char*>(b), 8);
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

bool AppendGoSurveyPayload(const char* pathUtf8, const AppCommandState& st, std::vector<std::string>& log) {
  const std::string json = SerializeGoSurveyJson(st);
  // The file was just written by LibreDWG; a sync client / antivirus can still hold it open
  // briefly (issue #167). Retry the append open with a short bounded backoff before giving up.
  std::ofstream f;
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (attempt > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(50 * attempt));
    f.open(pathUtf8, std::ios::binary | std::ios::app);
    if (f)
      break;
    f.clear();
  }
  if (!f) {
    log.push_back(std::string("DWG save — could not append GoSurvey document to ") + pathUtf8);
    return false;
  }
  f.write(json.data(), static_cast<std::streamsize>(json.size()));
  WriteU64Le(f, static_cast<std::uint64_t>(json.size()));
  f.write(kMagic, static_cast<std::streamsize>(kMagicLen));
  if (!f) {
    log.push_back("DWG save — failed while writing the GoSurvey document trailer.");
    return false;
  }
  log.push_back("DWG save — GoSurvey document preserved in file.");
  return true;
}

}  // namespace

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
  std::ifstream in(pathUtf8, std::ios::binary);
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
  std::error_code ec;
  const std::filesystem::path dst(pathUtf8);
  const std::filesystem::path staged = dst.string() + ".gosurvey-save.tmp";
  std::filesystem::remove(staged, ec);
  const std::string stagedUtf8 = staged.string();

  if (!ExportLibreCadFile(st, stagedUtf8.c_str(), log, /*asDxf=*/false)) {
    std::filesystem::remove(staged, ec);
    return false;
  }
  if (!AppendGoSurveyPayload(stagedUtf8.c_str(), st, log)) {
    std::filesystem::remove(staged, ec);
    log.push_back("DWG save — file was NOT written; the GoSurvey document could not be embedded.");
    return false;
  }

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
    return false;
  }
  return true;
}

bool OpenDrawingDocument(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    log.push_back("Open drawing — no path.");
    return false;
  }
  if (pointfile::EndsWithIgnoreCaseAscii(pathUtf8, ".gs"))
    return LoadGoSurveyFile(st, pathUtf8, log);
  return ImportDwgFile(st, pathUtf8, log);
}

bool SaveDrawingDocument(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log) {
  if (pathUtf8 == nullptr || pathUtf8[0] == '\0') {
    log.push_back("Save drawing — no path.");
    return false;
  }
  if (pointfile::EndsWithIgnoreCaseAscii(pathUtf8, ".gs"))
    return SaveGoSurveyFile(st, pathUtf8, log);
  return ExportDwgFile(st, pathUtf8, log);
}
