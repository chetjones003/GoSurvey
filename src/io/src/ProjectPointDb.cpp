#include "ProjectPointDb.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace projpts {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool Fail(std::string* err, const std::string& why) {
  if (err)
    *err = why;
  return false;
}

constexpr double kCoordTol = 1e-7;

bool NearlyEqual(double a, double b) { return std::fabs(a - b) <= kCoordTol; }

json PointToJson(const Entry& e) {
  const SurveyPoint& p = e.point;
  json o;  // same keys as the ADR-044 trailer's survey points: no second point schema (ADR-065 (a))
  o["id"] = p.id;
  o["easting"] = p.easting;
  o["northing"] = p.northing;
  o["elevation"] = p.elevation;
  o["description"] = p.description;
  o["rawDescription"] = p.rawDescription;
  o["layer"] = p.layer;
  o["labelStyle"] = static_cast<int>(p.labelStyle);
  o["sourceDrawing"] = e.sourceDrawing;
  return o;
}

bool PointFromJson(const json& o, Entry* e, std::string* err) {
  if (!o.is_object() || !o.contains("id") || !o["id"].is_number_integer())
    return Fail(err, "a point has no number");
  SurveyPoint& p = e->point;
  p = SurveyPoint{};
  p.id = o["id"].get<int>();
  p.easting = o.value("easting", 0.0);
  p.northing = o.value("northing", 0.0);
  p.elevation = o.value("elevation", 0.0);
  if (o.contains("description") && o["description"].is_string())
    p.description = o["description"].get<std::string>();
  if (o.contains("rawDescription") && o["rawDescription"].is_string())
    p.rawDescription = o["rawDescription"].get<std::string>();
  if (o.contains("layer") && o["layer"].is_string())
    p.layer = o["layer"].get<std::string>();
  const int ls = o.value("labelStyle", static_cast<int>(SurveyPointLabelStyle::NumberDesc));
  if (ls >= 0 && ls <= static_cast<int>(SurveyPointLabelStyle::NumberNorthEastElev))
    p.labelStyle = static_cast<SurveyPointLabelStyle>(ls);
  e->sourceDrawing.clear();
  if (o.contains("sourceDrawing") && o["sourceDrawing"].is_string())
    e->sourceDrawing = o["sourceDrawing"].get<std::string>();
  return true;
}

}  // namespace

fs::path DbPath(const fs::path& projectFolder, const std::string& pointsFolder) {
  return projectFolder / fs::u8path(pointsFolder.empty() ? "Points" : pointsFolder) / "survey-points.gspdb";
}

bool Load(const fs::path& file, const std::string& projectId, Db* out, std::string* err) {
  Db db;
  db.projectId = projectId;
  std::error_code ec;
  if (!fs::exists(file, ec)) {
    *out = std::move(db);
    return true;
  }
  std::ifstream f(file, std::ios::binary);
  if (!f)
    return Fail(err, "The point database could not be read.");
  const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object())
    return Fail(err, "The point database is damaged (it is not readable).");
  if (j.value("formatVersion", 0) > kFormatVersion)
    return Fail(err, "The point database was written by a newer GoSurvey.");
  const std::string fileProject = j.value("projectId", std::string());
  if (!fileProject.empty() && !projectId.empty() && fileProject != projectId)
    return Fail(err, "The point database belongs to a different project.");
  if (!j.contains("points") || !j["points"].is_array())
    return Fail(err, "The point database is damaged (no points list).");
  std::unordered_map<int, size_t> seen;
  for (const auto& o : j["points"]) {
    Entry e;
    std::string why;
    if (!PointFromJson(o, &e, &why))
      return Fail(err, "The point database is damaged (" + why + ").");
    if (!seen.emplace(e.point.id, db.points.size()).second)
      return Fail(err, "The point database is damaged (point " + std::to_string(e.point.id) + " appears twice).");
    db.points.push_back(std::move(e));
  }
  *out = std::move(db);
  return true;
}

bool Save(const fs::path& file, const Db& db, std::string* err) {
  json pts = json::array();
  for (const Entry& e : db.points)
    pts.push_back(PointToJson(e));
  json j;
  j["formatVersion"] = kFormatVersion;
  j["projectId"] = db.projectId;
  j["points"] = std::move(pts);

  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path tmp = file.parent_path() / fs::u8path(file.filename().u8string() + ".tmp");
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f)
      return Fail(err, "The point database could not be written.");
    f << j.dump();
    f.flush();
    if (!f) {
      f.close();
      fs::remove(tmp, ec);
      return Fail(err, "The point database could not be written.");
    }
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return Fail(err, "The point database could not be replaced.");
  }
  return true;
}

bool SamePoint(const SurveyPoint& a, const SurveyPoint& b) {
  return a.id == b.id && NearlyEqual(a.easting, b.easting) && NearlyEqual(a.northing, b.northing) &&
         NearlyEqual(a.elevation, b.elevation) && a.description == b.description &&
         a.rawDescription == b.rawDescription && a.layer == b.layer && a.labelStyle == b.labelStyle;
}

bool EqualsWorld(const std::vector<SurveyPoint>& local, double ox, double oy, const std::vector<SurveyPoint>& baseWorld) {
  if (local.size() != baseWorld.size())
    return false;
  for (size_t i = 0; i < local.size(); ++i) {
    SurveyPoint w = local[i];
    w.easting += ox;
    w.northing += oy;
    if (!SamePoint(w, baseWorld[i]))
      return false;
  }
  return true;
}

std::vector<SurveyPoint> ToWorld(const std::vector<SurveyPoint>& local, double ox, double oy) {
  std::vector<SurveyPoint> w = local;
  for (SurveyPoint& p : w) {
    p.easting += ox;
    p.northing += oy;
    p.labelMtextAnnId = 0;
  }
  return w;
}

std::vector<SurveyPoint> View(const Db& db) {
  std::vector<SurveyPoint> v;
  v.reserve(db.points.size());
  for (const Entry& e : db.points)
    v.push_back(e.point);
  return v;
}

Change ApplyChanges(Db* db, const std::vector<SurveyPoint>& baseWorld, const std::vector<SurveyPoint>& curWorld,
                    const std::string& source, double now, std::vector<std::string>* log) {
  Change c;
  std::unordered_map<int, const SurveyPoint*> base, cur;
  base.reserve(baseWorld.size());
  cur.reserve(curWorld.size());
  for (const SurveyPoint& p : baseWorld)
    base[p.id] = &p;
  for (const SurveyPoint& p : curWorld)
    cur[p.id] = &p;
  std::unordered_map<int, size_t> at;
  at.reserve(db->points.size());
  for (size_t i = 0; i < db->points.size(); ++i)
    at[db->points[i].point.id] = i;

  // Removed: in the base but no longer in the drawing.
  std::vector<char> gone(db->points.size(), 0);
  for (const SurveyPoint& p : baseWorld) {
    if (cur.count(p.id))
      continue;
    const auto it = at.find(p.id);
    if (it == at.end())
      continue;  // already gone from the database
    gone[it->second] = 1;
    ++c.removed;
  }
  // Added / edited.
  std::vector<Entry> appended;
  std::unordered_map<int, char> handled;  // a drawing holding one number twice must not write it twice
  handled.reserve(curWorld.size());
  for (const SurveyPoint& p : curWorld) {
    if (!handled.emplace(p.id, 1).second) {
      if (log)
        log->push_back("Project points - point number " + std::to_string(p.id) +
                       " appears more than once in the drawing; only the first is kept in the project database.");
      continue;
    }
    const auto b = base.find(p.id);
    const auto d = at.find(p.id);
    if (b != base.end() && SamePoint(*b->second, p))
      continue;  // unchanged by this drawing
    SurveyPoint np = p;
    np.labelMtextAnnId = 0;
    if (d != at.end() && !gone[d->second]) {
      if (SamePoint(db->points[d->second].point, np))
        continue;  // the database already agrees
      if (b == base.end() && log)
        log->push_back("Project points - point " + std::to_string(p.id) +
                       " already existed in the project database; it was overwritten.");
      db->points[d->second].point = np;
      if (b == base.end())
        ++c.added;
      else
        ++c.edited;
    } else {
      appended.push_back({np, source});
      ++c.added;
    }
  }
  if (c.removed > 0) {
    size_t w = 0;
    for (size_t i = 0; i < db->points.size(); ++i)
      if (!gone[i])
        db->points[w++] = std::move(db->points[i]);
    db->points.resize(w);
  }
  for (Entry& e : appended)
    db->points.push_back(std::move(e));
  if (c.Any()) {
    ++db->revision;
    if (!db->dirty) {
      db->dirty = true;
      db->dirtySince = now;
    }
  }
  return c;
}

}  // namespace projpts
