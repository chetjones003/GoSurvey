#include "Autosave.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <system_error>

namespace autosave {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr const char* kSessionPrefix = "session-";
constexpr const char* kSessionFile = "session.json";
constexpr const char* kMetaSuffix = ".meta.json";
constexpr const char* kPayloadSuffix = ".gsauto";
constexpr const char* kTmpSuffix = ".tmp";
// Small grace window for NTFS/FAT mtime rounding when comparing original .dwg mtime vs savedAtUnix.
constexpr std::int64_t kNewerFileToleranceSec = 2;

void SetErr(std::string* err, const std::string& message) {
  if (err != nullptr)
    *err = message;
}

bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.substr(s.size() - suffix.size()) == suffix;
}

bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() &&
         s.substr(0, prefix.size()) == prefix;
}

// Use stdio (_wfopen_s on MSVC) rather than std::ofstream so paths with Unicode characters work
// reliably on Windows and native file dialogs cannot disturb iostream locale state (issue #167).
bool ReadAllBytes(const fs::path& path, std::string* bytesOut) {
  if (bytesOut == nullptr)
    return false;
  bytesOut->clear();
  std::FILE* fp = nullptr;
#if defined(_MSC_VER)
  if (_wfopen_s(&fp, path.wstring().c_str(), L"rb") != 0 || fp == nullptr)
    return false;
#else
  fp = std::fopen(path.string().c_str(), "rb");
  if (fp == nullptr)
    return false;
#endif
  if (std::fseek(fp, 0, SEEK_END) != 0) {
    std::fclose(fp);
    return false;
  }
  const long long sz =
#if defined(_MSC_VER)
      _ftelli64(fp);
#else
      static_cast<long long>(std::ftell(fp));
#endif
  if (sz < 0) {
    std::fclose(fp);
    return false;
  }
  if (std::fseek(fp, 0, SEEK_SET) != 0) {
    std::fclose(fp);
    return false;
  }
  bytesOut->resize(static_cast<std::size_t>(sz));
  if (sz > 0 &&
      std::fread(bytesOut->data(), 1, static_cast<std::size_t>(sz), fp) != static_cast<std::size_t>(sz)) {
    std::fclose(fp);
    bytesOut->clear();
    return false;
  }
  return std::fclose(fp) == 0;
}

bool WriteAllBytes(const fs::path& path, std::string_view bytes) {
  std::FILE* fp = nullptr;
#if defined(_MSC_VER)
  if (_wfopen_s(&fp, path.wstring().c_str(), L"wb") != 0 || fp == nullptr)
    return false;
#else
  fp = std::fopen(path.string().c_str(), "wb");
  if (fp == nullptr)
    return false;
#endif
  const bool wrote =
      (bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), fp) == bytes.size()) &&
      !std::ferror(fp) && std::fflush(fp) == 0;
  const bool closed = std::fclose(fp) == 0;
  return wrote && closed;
}

// Stage beside target and replace atomically; falls back to copy+remove if rename hits a
// transient lock (matching DwgIo.cpp). Never leaves a partial file at \p target.
bool AtomicWriteFile(const fs::path& target, std::string_view bytes, std::string* err) {
  std::error_code ec;
  if (const fs::path parent = target.parent_path(); !parent.empty()) {
    fs::create_directories(parent, ec);
    if (ec) {
      SetErr(err, "Could not create autosave directory: " + ec.message());
      return false;
    }
  }
  const fs::path staged = fs::path(target.wstring() + L".tmp");
  fs::remove(staged, ec);
  ec.clear();

  if (!WriteAllBytes(staged, bytes)) {
    fs::remove(staged, ec);
    SetErr(err, "Could not write temporary autosave file.");
    return false;
  }

  fs::rename(staged, target, ec);
  if (ec) {
    ec.clear();
    fs::copy_file(staged, target, fs::copy_options::overwrite_existing, ec);
    std::error_code rmEc;
    fs::remove(staged, rmEc);
    if (ec) {
      SetErr(err, "Could not commit autosave file: " + ec.message());
      return false;
    }
  }
  return true;
}

std::string TabBaseName(std::uint32_t tabUid) {
  return "tab-" + std::to_string(tabUid);
}

fs::path TabPayloadPath(const fs::path& sessionDir, std::uint32_t tabUid) {
  return sessionDir / (TabBaseName(tabUid) + kPayloadSuffix);
}

fs::path TabMetaPath(const fs::path& sessionDir, std::uint32_t tabUid) {
  return sessionDir / (TabBaseName(tabUid) + kMetaSuffix);
}

bool HasAnyTabEntries(const fs::path& sessionDir) {
  std::error_code ec;
  if (!fs::exists(sessionDir, ec) || !fs::is_directory(sessionDir, ec))
    return false;
  for (const auto& entry : fs::directory_iterator(sessionDir, ec)) {
    if (ec)
      break;
    const std::string name = entry.path().filename().u8string();
    if (StartsWith(name, "tab-") && EndsWith(name, kMetaSuffix))
      return true;
  }
  return false;
}

void PruneSessionDirIfEmpty(const fs::path& sessionDir) {
  if (sessionDir.empty() || HasAnyTabEntries(sessionDir))
    return;
  std::error_code ec;
  fs::remove_all(sessionDir, ec);
}

bool ReadSessionPid(const fs::path& sessionDir, std::uint32_t* pidOut) {
  if (pidOut != nullptr)
    *pidOut = 0;
  std::string bytes;
  if (!ReadAllBytes(sessionDir / kSessionFile, &bytes))
    return false;
  const json j = json::parse(bytes, nullptr, false);
  if (j.is_discarded() || !j.is_object())
    return false;
  if (!j.contains("pid") || !j["pid"].is_number_unsigned())
    return false;
  if (pidOut != nullptr)
    *pidOut = j["pid"].get<std::uint32_t>();
  return true;
}

bool ParseEntryMeta(const fs::path& metaPath, EntryMeta* out) {
  if (out == nullptr)
    return false;
  std::string bytes;
  if (!ReadAllBytes(metaPath, &bytes))
    return false;
  const json j = json::parse(bytes, nullptr, false);
  if (j.is_discarded() || !j.is_object())
    return false;

  EntryMeta m;
  if (j.contains("formatVersion") && j["formatVersion"].is_number_integer())
    m.formatVersion = j["formatVersion"].get<int>();
  if (m.formatVersion < 1 || m.formatVersion > kFormatVersion)
    return false;
  if (!j.contains("tabUid") || !j["tabUid"].is_number_unsigned())
    return false;
  m.tabUid = j["tabUid"].get<std::uint32_t>();
  if (m.tabUid == 0)
    return false;
  if (j.contains("tabName") && j["tabName"].is_string())
    m.tabName = j["tabName"].get<std::string>();
  if (m.tabName.empty())
    m.tabName = "Drawing";
  if (j.contains("originalFilePath") && j["originalFilePath"].is_string())
    m.originalFilePath = j["originalFilePath"].get<std::string>();
  if (j.contains("projectFilePath") && j["projectFilePath"].is_string())
    m.projectFilePath = j["projectFilePath"].get<std::string>();
  if (j.contains("savedAtUnix") && j["savedAtUnix"].is_number_integer())
    m.savedAtUnix = j["savedAtUnix"].get<std::int64_t>();
  if (j.contains("appVersion") && j["appVersion"].is_string())
    m.appVersion = j["appVersion"].get<std::string>();

  *out = std::move(m);
  return true;
}

bool IsOriginalFileNewer(const std::string& originalPathUtf8, std::int64_t savedAtUnix) {
  if (originalPathUtf8.empty() || savedAtUnix <= 0)
    return false;
  std::error_code ec;
  const fs::path orig = fs::u8path(originalPathUtf8);
  if (!fs::is_regular_file(orig, ec) || ec)
    return false;
  const auto ftime = fs::last_write_time(orig, ec);
  if (ec)
    return false;
  // Convert file_time_type to system_clock via current offset (portable in C++17).
  const auto fileNow = fs::file_time_type::clock::now();
  const auto sysNow = std::chrono::system_clock::now();
  const auto diff = std::chrono::duration_cast<std::chrono::seconds>(ftime - fileNow);
  const auto origUnix =
      std::chrono::duration_cast<std::chrono::seconds>(sysNow.time_since_epoch()).count() + diff.count();
  return origUnix > savedAtUnix + kNewerFileToleranceSec;
}

}  // namespace

std::string MakeSessionId(std::uint32_t pid, std::int64_t startUnixMs) {
  return std::string(kSessionPrefix) + std::to_string(pid) + "-" + std::to_string(startUnixMs);
}

fs::path SessionDirectory(const fs::path& autosaveRoot, const std::string& sessionId) {
  if (autosaveRoot.empty() || sessionId.empty())
    return {};
  return autosaveRoot / fs::u8path(sessionId);
}

bool WriteEntry(const fs::path& autosaveRoot,
                const std::string& sessionId,
                std::uint32_t pid,
                std::int64_t sessionStartedAtUnix,
                const EntryMeta& meta,
                std::string_view documentJsonUtf8,
                std::string* err) {
  if (autosaveRoot.empty() || sessionId.empty()) {
    SetErr(err, "Autosave path is not configured.");
    return false;
  }
  if (meta.tabUid == 0) {
    SetErr(err, "Invalid drawing tab id for autosave.");
    return false;
  }
  if (documentJsonUtf8.empty()) {
    SetErr(err, "Cannot write an empty autosave payload.");
    return false;
  }

  const fs::path sessionDir = SessionDirectory(autosaveRoot, sessionId);
  std::error_code ec;
  fs::create_directories(sessionDir, ec);
  if (ec) {
    SetErr(err, "Could not create autosave session directory: " + ec.message());
    return false;
  }

  const fs::path sessionFile = sessionDir / kSessionFile;
  if (!fs::exists(sessionFile, ec)) {
    json s;
    s["formatVersion"] = kFormatVersion;
    s["pid"] = pid;
    s["startedAtUnix"] = sessionStartedAtUnix;
    s["appVersion"] = meta.appVersion;
    if (!AtomicWriteFile(sessionFile, s.dump(2), err))
      return false;
  }

  // Write the document payload first, then commit metadata pointing to it.
  const fs::path payloadPath = TabPayloadPath(sessionDir, meta.tabUid);
  if (!AtomicWriteFile(payloadPath, documentJsonUtf8, err))
    return false;

  json m;
  m["formatVersion"] = kFormatVersion;
  m["tabUid"] = meta.tabUid;
  m["tabName"] = meta.tabName.empty() ? std::string("Drawing") : meta.tabName;
  m["originalFilePath"] = meta.originalFilePath;
  m["projectFilePath"] = meta.projectFilePath;
  m["savedAtUnix"] = meta.savedAtUnix;
  m["appVersion"] = meta.appVersion;

  const fs::path metaPath = TabMetaPath(sessionDir, meta.tabUid);
  if (!AtomicWriteFile(metaPath, m.dump(2), err))
    return false;

  return true;
}

void RemoveTabEntry(const fs::path& autosaveRoot,
                    const std::string& sessionId,
                    std::uint32_t tabUid) {
  if (autosaveRoot.empty() || sessionId.empty() || tabUid == 0)
    return;
  const fs::path sessionDir = SessionDirectory(autosaveRoot, sessionId);
  std::error_code ec;
  if (!fs::exists(sessionDir, ec))
    return;
  const fs::path metaPath = TabMetaPath(sessionDir, tabUid);
  const fs::path payloadPath = TabPayloadPath(sessionDir, tabUid);
  fs::remove(metaPath, ec);
  fs::remove(fs::path(metaPath.wstring() + L".tmp"), ec);
  fs::remove(payloadPath, ec);
  fs::remove(fs::path(payloadPath.wstring() + L".tmp"), ec);
  PruneSessionDirIfEmpty(sessionDir);
}

void CleanupSession(const fs::path& autosaveRoot, const std::string& sessionId) {
  if (autosaveRoot.empty() || sessionId.empty())
    return;
  const fs::path sessionDir = SessionDirectory(autosaveRoot, sessionId);
  std::error_code ec;
  fs::remove_all(sessionDir, ec);
}

std::vector<RecoveryEntry> ScanRecoverable(const fs::path& autosaveRoot,
                                           const std::string& currentSessionId,
                                           const std::function<bool(std::uint32_t)>& pidAlive) {
  std::vector<RecoveryEntry> out;
  if (autosaveRoot.empty())
    return out;

  std::error_code ec;
  if (!fs::exists(autosaveRoot, ec) || !fs::is_directory(autosaveRoot, ec))
    return out;

  std::vector<fs::path> sessionDirs;
  for (const auto& dirEntry : fs::directory_iterator(autosaveRoot, ec)) {
    if (ec)
      break;
    std::error_code isDirEc;
    if (!dirEntry.is_directory(isDirEc) || isDirEc)
      continue;
    const std::string name = dirEntry.path().filename().u8string();
    if (!StartsWith(name, kSessionPrefix))
      continue;
    if (!currentSessionId.empty() && name == currentSessionId)
      continue;
    sessionDirs.push_back(dirEntry.path());
  }

  for (const fs::path& sessionDir : sessionDirs) {
    const std::string sessionId = sessionDir.filename().u8string();
    std::uint32_t pid = 0;
    if (ReadSessionPid(sessionDir, &pid) && pid != 0 && pidAlive && pidAlive(pid))
      continue;  // Another live GoSurvey instance owns this session.

    // Dead session: remove any stray *.tmp files left by a mid-write crash and collect valid entries.
    std::vector<fs::path> metaFiles;
    std::error_code iterEc;
    for (const auto& fileEntry : fs::directory_iterator(sessionDir, iterEc)) {
      if (iterEc)
        break;
      const std::string name = fileEntry.path().filename().u8string();
      if (EndsWith(name, kTmpSuffix)) {
        std::error_code rmEc;
        fs::remove(fileEntry.path(), rmEc);
        continue;
      }
      if (StartsWith(name, "tab-") && EndsWith(name, kMetaSuffix))
        metaFiles.push_back(fileEntry.path());
    }

    std::size_t validCount = 0;
    for (const fs::path& metaPath : metaFiles) {
      EntryMeta meta;
      if (!ParseEntryMeta(metaPath, &meta))
        continue;
      const fs::path payloadPath = TabPayloadPath(sessionDir, meta.tabUid);
      std::error_code szEc;
      if (!fs::is_regular_file(payloadPath, szEc) || szEc)
        continue;
      const std::uint64_t payloadSize = fs::file_size(payloadPath, szEc);
      if (szEc || payloadSize == 0)
        continue;

      RecoveryEntry rec;
      rec.sessionId = sessionId;
      rec.sessionDir = sessionDir;
      rec.metaPath = metaPath;
      rec.payloadPath = payloadPath;
      rec.payloadSizeBytes = payloadSize;
      rec.originalFileNewer = IsOriginalFileNewer(meta.originalFilePath, meta.savedAtUnix);
      rec.meta = std::move(meta);
      out.push_back(std::move(rec));
      ++validCount;
    }

    if (validCount == 0) {
      std::error_code rmEc;
      fs::remove_all(sessionDir, rmEc);
    }
  }

  std::sort(out.begin(), out.end(), [](const RecoveryEntry& a, const RecoveryEntry& b) {
    if (a.meta.savedAtUnix != b.meta.savedAtUnix)
      return a.meta.savedAtUnix > b.meta.savedAtUnix;
    if (a.sessionId != b.sessionId)
      return a.sessionId > b.sessionId;
    return a.meta.tabUid < b.meta.tabUid;
  });

  return out;
}

bool ReadPayload(const RecoveryEntry& entry, std::string* jsonOut, std::string* err) {
  if (jsonOut == nullptr) {
    SetErr(err, "No output buffer for recovery payload.");
    return false;
  }
  if (!ReadAllBytes(entry.payloadPath, jsonOut) || jsonOut->empty()) {
    SetErr(err, "Could not read autosaved drawing file: " + entry.payloadPath.u8string());
    return false;
  }
  return true;
}

void DiscardEntry(const RecoveryEntry& entry) {
  std::error_code ec;
  if (!entry.metaPath.empty()) {
    fs::remove(entry.metaPath, ec);
    fs::remove(fs::path(entry.metaPath.wstring() + L".tmp"), ec);
  }
  if (!entry.payloadPath.empty()) {
    fs::remove(entry.payloadPath, ec);
    fs::remove(fs::path(entry.payloadPath.wstring() + L".tmp"), ec);
  }
  PruneSessionDirIfEmpty(entry.sessionDir);
}

void DiscardAll(const std::vector<RecoveryEntry>& entries) {
  for (const auto& entry : entries)
    DiscardEntry(entry);
}

}  // namespace autosave
