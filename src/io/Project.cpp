#include "Project.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gsproj {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string NewGuid() {
  std::random_device rd;
  std::uint32_t w[4] = {rd(), rd(), rd(), rd()};
  w[1] = (w[1] & 0xFFFF0FFFu) | 0x00004000u;  // version 4
  w[2] = (w[2] & 0x3FFFFFFFu) | 0x80000000u;  // variant 1
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x", w[0], w[1] >> 16, w[1] & 0xFFFFu,
                w[2] >> 16, w[2] & 0xFFFFu, w[3]);
  return buf;
}

bool NameIsLegal(const std::string& name) {
  if (name.empty() || name.front() == ' ' || name.back() == ' ' || name.back() == '.')
    return false;
  return name.find_first_of("\\/:*?\"<>|") == std::string::npos;
}

bool IsGsproj(const fs::path& p) {
  std::string e = p.extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e == kExtension;
}

bool ReadText(const fs::path& p, std::string* out) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

bool Fail(std::string* err, const std::string& msg) {
  if (err)
    *err = msg;
  return false;
}

std::string LockToJson(const LockInfo& l) {
  json j;
  j["user"] = l.user;
  j["machine"] = l.machine;
  j["pid"] = l.pid;
  j["sinceUnix"] = l.sinceUnix;
  return j.dump(2);
}

bool ParseLock(const std::string& text, LockInfo* out) {
  const json j = json::parse(text, nullptr, false);
  if (!j.is_object())
    return false;
  out->user = j.value("user", std::string());
  out->machine = j.value("machine", std::string());
  out->pid = j.value("pid", 0u);
  out->sinceUnix = j.value("sinceUnix", static_cast<std::int64_t>(0));
  return true;
}

bool SameHolder(const LockInfo& a, const LockInfo& b) { return a.machine == b.machine && a.pid == b.pid; }

/// Creates \p path only if it does not exist yet (the atomic "first opener wins" step).
bool CreateExclusive(const fs::path& path, const std::string& content) {
#if defined(_WIN32)
  HANDLE h = CreateFileW(path.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
  CloseHandle(h);
  return ok != FALSE;
#else
  std::FILE* fp = std::fopen(path.string().c_str(), "wx");
  if (!fp)
    return false;
  const bool ok = std::fwrite(content.data(), 1, content.size(), fp) == content.size();
  std::fclose(fp);
  return ok;
#endif
}

}  // namespace

bool IsSafeRelativePath(const std::string& rel) {
  if (rel.empty())
    return false;
  if (rel.front() == '/' || rel.front() == '\\')
    return false;
  if (rel.find(':') != std::string::npos)
    return false;
  std::string seg;
  for (std::size_t i = 0; i <= rel.size(); ++i) {
    if (i == rel.size() || rel[i] == '/' || rel[i] == '\\') {
      if (seg == "..")
        return false;
      seg.clear();
    } else {
      seg += rel[i];
    }
  }
  return true;
}

std::map<std::string, std::string> StandardLayout() {
  return {{"drawings", "Drawings"}, {"points", "Points"},       {"pointClouds", "PointClouds"},
          {"pdfs", "PDFs"},         {"turnovers", "Turnovers"}, {"settings", "Settings"}};
}

bool Create(const fs::path& parentDir, const std::string& name, Project* out, std::string* err) {
  if (!NameIsLegal(name))
    return Fail(err, "The project name is empty or contains characters a folder name cannot have.");
  std::error_code ec;
  const fs::path folder = parentDir / fs::u8path(name);
  if (fs::exists(folder, ec)) {
    for (const auto& e : fs::directory_iterator(folder, ec))
      if (IsGsproj(e.path()))
        return Fail(err, "That folder already contains a project.");
  }
  Project p;
  p.id = NewGuid();
  p.name = name;
  p.layout = StandardLayout();
  p.file = folder / fs::u8path(name + kExtension);
  for (const auto& kv : p.layout) {
    fs::create_directories(folder / fs::u8path(kv.second), ec);
    if (ec)
      return Fail(err, "Could not create the folder '" + kv.second + "': " + ec.message());
  }
  if (!Save(p, err))
    return false;
  if (out)
    *out = std::move(p);
  return true;
}

bool Load(const fs::path& gsprojFile, Project* out, std::string* err) {
  std::string text;
  if (!ReadText(gsprojFile, &text))
    return Fail(err, "The project file could not be read.");
  json j = json::parse(text, nullptr, false);
  if (!j.is_object())
    return Fail(err, "The project file is not valid.");
  Project p;
  p.formatVersion = j.value("formatVersion", 0);
  if (p.formatVersion < 1)
    return Fail(err, "The project file has no format version.");
  if (p.formatVersion > kFormatVersion)
    return Fail(err, "The project was made by a newer GoSurvey (format " + std::to_string(p.formatVersion) + ").");
  p.id = j.value("id", std::string());
  p.name = j.value("name", std::string());
  if (p.id.empty() || p.name.empty())
    return Fail(err, "The project file is missing its ID or name.");
  if (j.contains("layout") && j["layout"].is_object())
    for (auto it = j["layout"].begin(); it != j["layout"].end(); ++it) {
      if (!it.value().is_string() || !IsSafeRelativePath(it.value().get<std::string>()))
        return Fail(err, "The project's folder layout has an unsafe path.");
      p.layout[it.key()] = it.value().get<std::string>();
    }
  if (j.contains("settings") && j["settings"].is_object())
    p.settingsJson = j["settings"].dump();
  if (j.contains("items") && j["items"].is_array())
    for (const auto& it : j["items"]) {
      if (!it.is_object())
        return Fail(err, "The project's file list is not valid.");
      TrackedItem t;
      t.path = it.value("path", std::string());
      t.kind = it.value("kind", std::string(kKindInProject));
      if (t.kind == kKindInProject && !IsSafeRelativePath(t.path))
        return Fail(err, "A tracked file has a path outside the project: '" + t.path + "'.");
      if (it.contains("associations") && it["associations"].is_array())
        for (const auto& a : it["associations"])
          if (a.is_string())
            t.associations.push_back(a.get<std::string>());
      if (it.contains("placements") && it["placements"].is_array())
        t.placementsJson = it["placements"].dump();
      p.items.push_back(std::move(t));
    }
  static const char* const known[] = {"formatVersion", "id", "name", "layout", "settings", "items"};
  json extra = json::object();
  for (auto it = j.begin(); it != j.end(); ++it)
    if (std::find(std::begin(known), std::end(known), it.key()) == std::end(known))
      extra[it.key()] = it.value();
  p.extraJson = extra.dump();
  p.file = gsprojFile;
  if (out)
    *out = std::move(p);
  return true;
}

bool Save(const Project& p, std::string* err) {
  json j = json::parse(p.extraJson, nullptr, false);
  if (!j.is_object())
    j = json::object();
  j["formatVersion"] = p.formatVersion;
  j["id"] = p.id;
  j["name"] = p.name;
  j["layout"] = p.layout;
  const json settings = json::parse(p.settingsJson, nullptr, false);
  j["settings"] = settings.is_object() ? settings : json::object();
  json items = json::array();
  for (const auto& t : p.items) {
    json it;
    it["path"] = t.path;
    it["kind"] = t.kind;
    it["associations"] = t.associations;
    const json placements = json::parse(t.placementsJson, nullptr, false);
    if (placements.is_array() && !placements.empty())
      it["placements"] = placements;
    items.push_back(std::move(it));
  }
  j["items"] = std::move(items);

  const fs::path tmp = p.file.parent_path() / fs::u8path(p.file.filename().u8string() + ".tmp");
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f)
      return Fail(err, "The project file could not be written.");
    f << j.dump(2);
    f.flush();
    if (!f)
      return Fail(err, "The project file could not be written.");
  }
  std::error_code ec;
  fs::rename(tmp, p.file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return Fail(err, "The project file could not be replaced.");
  }
  return true;
}

FindResult FindProjectFor(const fs::path& drawingFile) {
  std::error_code ec;
  fs::path dir = fs::absolute(drawingFile, ec).parent_path();
  while (!dir.empty()) {
    std::vector<fs::path> found;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
      if (it->is_regular_file(ec) && IsGsproj(it->path()))
        found.push_back(it->path());
    ec.clear();
    if (found.size() > 1)
      return {FindState::Damaged, dir, "The folder holds more than one project file."};
    if (found.size() == 1) {
      std::string why;
      if (Load(found[0], nullptr, &why))
        return {FindState::Found, found[0], {}};
      return {FindState::Damaged, found[0], why};
    }
    const fs::path parent = dir.parent_path();
    if (parent == dir)
      break;
    dir = parent;
  }
  return {};
}

fs::path LockPath(const fs::path& gsprojFile) {
  return gsprojFile.parent_path() / fs::u8path(gsprojFile.filename().u8string() + ".lock");
}

LockResult TryAcquire(const fs::path& gsprojFile, const LockInfo& me, LockInfo* holder) {
  const fs::path lock = LockPath(gsprojFile);
  if (CreateExclusive(lock, LockToJson(me)))
    return LockResult::Acquired;
  if (holder) {
    *holder = LockInfo{};
    std::string text;
    if (ReadText(lock, &text))
      ParseLock(text, holder);
  }
  return LockResult::HeldByOther;
}

bool IsStale(const LockInfo& holder, const LockInfo& me, const std::function<bool(std::uint32_t)>& pidAlive) {
  if (holder.machine.empty() && holder.pid == 0)
    return true;  // unreadable lock
  return holder.machine == me.machine && !pidAlive(holder.pid);
}

bool TakeOver(const fs::path& gsprojFile, const LockInfo& me) {
  std::ofstream f(LockPath(gsprojFile), std::ios::binary | std::ios::trunc);
  if (!f)
    return false;
  f << LockToJson(me);
  return static_cast<bool>(f);
}

void Release(const fs::path& gsprojFile, const LockInfo& me) {
  const fs::path lock = LockPath(gsprojFile);
  std::string text;
  LockInfo held;
  if (!ReadText(lock, &text) || !ParseLock(text, &held) || !SameHolder(held, me))
    return;
  std::error_code ec;
  fs::remove(lock, ec);
}

}  // namespace gsproj
