#include "ProjectTurnover.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>

namespace projturn {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool Fail(std::string* err, const std::string& msg) {
  if (err)
    *err = msg;
  return false;
}

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

std::string TurnoverFolder(const gsproj::Project& p) {
  const auto it = p.layout.find("turnovers");
  if (it != p.layout.end() && !it->second.empty())
    return it->second;
  return gsproj::StandardLayout().at("turnovers");
}

/// A recipient made safe for a file name: letters, digits, '-' and '_' kept, spaces become '-', the rest
/// dropped (non-ASCII bytes too, so the name is portable); at most 40 characters.
std::string FileStem(const std::string& recipient) {
  std::string s;
  for (unsigned char c : recipient) {
    if (std::isalnum(c) && c < 128)
      s += static_cast<char>(c);
    else if (c == '-' || c == '_')
      s += static_cast<char>(c);
    else if (c == ' ' && !s.empty() && s.back() != '-')
      s += '-';
    if (s.size() >= 40)
      break;
  }
  return s.empty() ? std::string("recipient") : s;
}

/// CRC-32 and size of a file, streamed. False when it cannot be read.
bool Fingerprint(const fs::path& file, std::uintmax_t* size, std::string* crcHex) {
  std::ifstream in(file, std::ios::binary);
  if (!in)
    return false;
  mz_ulong crc = mz_crc32(0, nullptr, 0);
  std::uintmax_t total = 0;
  std::vector<char> buf(1 << 20);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const std::streamsize n = in.gcount();
    if (n > 0) {
      crc = mz_crc32(crc, reinterpret_cast<const unsigned char*>(buf.data()), static_cast<size_t>(n));
      total += static_cast<std::uintmax_t>(n);
    }
  }
  if (in.bad())
    return false;
  char hex[16];
  std::snprintf(hex, sizeof(hex), "%08lx", static_cast<unsigned long>(crc));
  *size = total;
  *crcHex = hex;
  return true;
}

json ToJson(const Record& r) {
  json items = json::array();
  for (const Entry& e : r.items) {
    json j = {{"path", e.path}, {"kind", e.kind}, {"sizeBytes", e.sizeBytes}, {"crc32", e.crcHex}};
    if (e.missing)
      j["missing"] = true;
    items.push_back(std::move(j));
  }
  return {{"formatVersion", r.formatVersion}, {"projectId", r.projectId},   {"projectName", r.projectName},
          {"recipient", r.recipient},         {"createdUnix", r.createdUnix}, {"date", r.date},
          {"items", std::move(items)}};
}

}  // namespace

std::string DateText(std::int64_t unix) {
  // Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm), UTC.
  std::int64_t days = unix >= 0 ? unix / 86400 : -((-unix + 86399) / 86400);
  days += 719468;
  const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const std::int64_t doe = days - era * 146097;
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t y = yoe + era * 400;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::int64_t mp = (5 * doy + 2) / 153;
  const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
  const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04lld-%02lld-%02lld", static_cast<long long>(m <= 2 ? y + 1 : y),
                static_cast<long long>(m), static_cast<long long>(d));
  return buf;
}

bool IsRecordPath(const gsproj::Project& p, const std::string& path) {
  const std::string prefix = TurnoverFolder(p) + "/";
  const std::string ext = kExtension;
  return path.size() > prefix.size() + ext.size() && path.compare(0, prefix.size(), prefix) == 0 &&
         path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}

std::vector<std::string> Candidates(const gsproj::Project& p) {
  std::vector<std::string> out;
  for (const gsproj::TrackedItem& it : p.items)
    if (!IsRecordPath(p, it.path))
      out.push_back(it.path);
  return out;
}

bool Create(gsproj::Project* p, const std::string& recipient, const std::vector<std::string>& chosen,
            std::int64_t nowUnix, const projfiles::Health& health, bool acknowledged, Record* out,
            std::string* err) {
  Record rec;
  rec.projectId = p->id;
  rec.projectName = p->name;
  rec.recipient = Trim(recipient);
  rec.createdUnix = nowUnix;
  rec.date = DateText(nowUnix);
  if (rec.recipient.empty())
    return Fail(err, "A turnover needs a recipient.");
  if (!health.Clean() && !acknowledged)
    return Fail(err, "Project Health found problems; accept them first, or fix them and try again.");

  const std::vector<std::string> allowed = Candidates(*p);
  std::set<std::string> seen;
  for (const std::string& path : chosen) {
    if (!seen.insert(path).second)
      continue;
    if (std::find(allowed.begin(), allowed.end(), path) == allowed.end())
      return Fail(err, "'" + path + "' is not a tracked file of this project.");
    const gsproj::TrackedItem* item = nullptr;
    for (const gsproj::TrackedItem& it : p->items)
      if (it.path == path)
        item = &it;
    Entry e;
    e.path = item->path;
    e.kind = item->kind;
    const std::string abs = projfiles::ResolveItem(*p, *item);
    std::error_code ec;
    if (abs.empty() || !Fingerprint(fs::u8path(abs), &e.sizeBytes, &e.crcHex)) {
      e.missing = true;
      e.sizeBytes = 0;
      e.crcHex.clear();
    }
    rec.items.push_back(std::move(e));
  }
  if (rec.items.empty())
    return Fail(err, "Choose at least one file to hand over.");

  // Pick a name that is not taken: <date>_<recipient>.gsturnover, then -2, -3 ...
  const std::string folder = TurnoverFolder(*p);
  std::error_code ec;
  fs::create_directories(p->Folder() / fs::u8path(folder), ec);
  if (ec)
    return Fail(err, "The Turnovers folder could not be created: " + ec.message());
  const std::string stem = rec.date + "_" + FileStem(rec.recipient);
  std::string rel;
  for (int n = 1;; ++n) {
    rel = folder + "/" + stem + (n == 1 ? std::string() : "-" + std::to_string(n)) + kExtension;
    if (!fs::exists(p->Folder() / fs::u8path(rel), ec))
      break;
    if (n > 999)
      return Fail(err, "Too many turnovers with the same date and recipient.");
  }
  rec.file = rel;

  // Atomic write: temp file beside the record, then rename.
  const fs::path target = p->Folder() / fs::u8path(rel);
  fs::path tmp = target;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f)
      return Fail(err, "The turnover record could not be written: " + tmp.u8string());
    f << ToJson(rec).dump(2);
    f.flush();
    if (!f) {
      f.close();
      fs::remove(tmp, ec);
      return Fail(err, "The turnover record could not be written: " + tmp.u8string());
    }
  }
  fs::rename(tmp, target, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return Fail(err, "The turnover record could not be saved: " + ec.message());
  }

  // Track the record so Project Files lists it, then save the project. A failed save undoes both.
  gsproj::TrackedItem t;
  t.path = rel;
  p->items.push_back(t);
  std::string saveErr;
  if (!gsproj::Save(*p, &saveErr)) {
    p->items.pop_back();
    fs::remove(target, ec);
    return Fail(err, "The project file could not be saved: " + saveErr);
  }
  if (out)
    *out = std::move(rec);
  return true;
}

bool Read(const fs::path& file, Record* out, std::string* err) {
  std::ifstream f(file, std::ios::binary);
  if (!f)
    return Fail(err, "cannot be read");
  json j = json::parse(f, nullptr, false);
  if (!j.is_object() || !j.contains("items") || !j["items"].is_array() || !j.contains("recipient") ||
      !j["recipient"].is_string())
    return Fail(err, "not a turnover record");
  Record r;
  r.formatVersion = j.value("formatVersion", 0);
  if (r.formatVersion > kFormatVersion)
    return Fail(err, "was made by a newer GoSurvey");
  r.projectId = j.value("projectId", std::string());
  r.projectName = j.value("projectName", std::string());
  r.recipient = j["recipient"].get<std::string>();
  r.createdUnix = j.value("createdUnix", static_cast<std::int64_t>(0));
  r.date = j.value("date", std::string());
  for (const json& ji : j["items"]) {
    if (!ji.is_object() || !ji.contains("path") || !ji["path"].is_string())
      return Fail(err, "an item has no path");
    Entry e;
    e.path = ji["path"].get<std::string>();
    e.kind = ji.value("kind", std::string(gsproj::kKindInProject));
    e.sizeBytes = ji.value("sizeBytes", static_cast<std::uintmax_t>(0));
    e.crcHex = ji.value("crc32", std::string());
    e.missing = ji.value("missing", false);
    r.items.push_back(std::move(e));
  }
  if (out)
    *out = std::move(r);
  return true;
}

std::vector<Record> List(const gsproj::Project& p, std::vector<std::string>* problems) {
  std::vector<Record> out;
  std::error_code ec;
  const fs::path dir = p.Folder() / fs::u8path(TurnoverFolder(p));
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || it->path().extension() != kExtension)
      continue;
    Record r;
    std::string why;
    if (Read(it->path(), &r, &why)) {
      r.file = TurnoverFolder(p) + "/" + it->path().filename().u8string();
      out.push_back(std::move(r));
    } else if (problems) {
      problems->push_back(it->path().filename().u8string() + ": " + why);
    }
  }
  std::sort(out.begin(), out.end(), [](const Record& a, const Record& b) {
    return a.createdUnix != b.createdUnix ? a.createdUnix > b.createdUnix : a.file < b.file;
  });
  return out;
}

}  // namespace projturn
