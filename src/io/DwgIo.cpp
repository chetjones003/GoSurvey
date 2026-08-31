#include "DwgIo.hpp"

#include "GsIo.hpp"
#include "LibreDwgCad.hpp"
#include "PointFileExt.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
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
  std::ofstream f(pathUtf8, std::ios::binary | std::ios::app);
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
  if (!ExportLibreCadFile(st, pathUtf8, log, /*asDxf=*/false))
    return false;
  return AppendGoSurveyPayload(pathUtf8, st, log);
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
