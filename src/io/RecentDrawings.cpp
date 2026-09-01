#include "RecentDrawings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace recent {

namespace {

std::string PathKey(const std::string& p) {
  // Case-insensitive, separator-normalised compare for the dedup key — Windows paths differing only
  // in slash direction or drive-letter case are the same drawing. Display still uses the stored text.
  std::string k = p;
  for (char& c : k) {
    if (c == '\\')
      c = '/';
    else
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return k;
}

std::string StemOf(const std::string& p) {
  std::error_code ec;
  std::string s = std::filesystem::path(p).stem().u8string();
  return s.empty() ? p : s;
}

std::vector<Entry> ReadRaw(const std::filesystem::path& jsonFile) {
  std::vector<Entry> out;
  std::ifstream f(jsonFile, std::ios::binary);
  if (!f)
    return out;
  try {
    nlohmann::json j;
    f >> j;
    if (!j.is_array())
      return out;
    for (const auto& e : j) {
      if (!e.is_object() || !e.contains("path") || !e["path"].is_string())
        continue;
      Entry ent;
      ent.path = e["path"].get<std::string>();
      if (ent.path.empty())
        continue;
      ent.name = (e.contains("name") && e["name"].is_string() && !e["name"].get<std::string>().empty())
                     ? e["name"].get<std::string>()
                     : StemOf(ent.path);
      if (e.contains("thumb") && e["thumb"].is_string())
        ent.thumb = e["thumb"].get<std::string>();
      if (e.contains("lastOpenedUnix") && e["lastOpenedUnix"].is_number_integer())
        ent.lastOpenedUnix = e["lastOpenedUnix"].get<std::int64_t>();
      out.push_back(std::move(ent));
    }
  } catch (...) {
    return {};
  }
  return out;
}

bool WriteRaw(const std::filesystem::path& jsonFile, const std::vector<Entry>& entries) {
  nlohmann::json j = nlohmann::json::array();
  for (const auto& e : entries) {
    nlohmann::json o;
    o["path"] = e.path;
    o["name"] = e.name;
    o["thumb"] = e.thumb;
    o["lastOpenedUnix"] = e.lastOpenedUnix;
    j.push_back(std::move(o));
  }
  try {
    if (const auto dir = jsonFile.parent_path(); !dir.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
    }
    std::ofstream f(jsonFile, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f)
      return false;
    f << j.dump(2);
    return f.good();
  } catch (...) {
    return false;
  }
}

}  // namespace

std::vector<Entry> Load(const std::filesystem::path& jsonFile) {
  std::vector<Entry> v = ReadRaw(jsonFile);
  if (static_cast<int>(v.size()) > kMaxEntries)
    v.resize(kMaxEntries);
  return v;
}

void Note(const std::filesystem::path& jsonFile, const std::string& drawingPath,
          const std::string& thumbFileName, std::int64_t nowUnix) {
  if (drawingPath.empty())
    return;
  std::vector<Entry> v = ReadRaw(jsonFile);

  const std::string key = PathKey(drawingPath);
  std::string keepThumb = thumbFileName;
  v.erase(std::remove_if(v.begin(), v.end(),
                         [&](const Entry& e) {
                           if (PathKey(e.path) != key)
                             return false;
                           if (keepThumb.empty())
                             keepThumb = e.thumb;  // preserve an earlier-captured thumbnail
                           return true;
                         }),
          v.end());

  Entry ent;
  ent.path = drawingPath;
  ent.name = StemOf(drawingPath);
  ent.thumb = keepThumb;
  ent.lastOpenedUnix = nowUnix;
  v.insert(v.begin(), std::move(ent));

  if (static_cast<int>(v.size()) > kMaxEntries)
    v.resize(kMaxEntries);
  WriteRaw(jsonFile, v);
}

void Remove(const std::filesystem::path& jsonFile, const std::string& drawingPath) {
  std::vector<Entry> v = ReadRaw(jsonFile);
  const std::string key = PathKey(drawingPath);
  const std::size_t before = v.size();
  v.erase(std::remove_if(v.begin(), v.end(),
                         [&](const Entry& e) { return PathKey(e.path) == key; }),
          v.end());
  if (v.size() != before)
    WriteRaw(jsonFile, v);
}

void Clear(const std::filesystem::path& jsonFile) {
  WriteRaw(jsonFile, {});
}

}  // namespace recent
