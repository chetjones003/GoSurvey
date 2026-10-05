#include "ProjectFiles.hpp"

#include "io/ProjectTurnover.hpp"
#include "pdf/PdfAttach.hpp"
#include "util/pointcloudcache.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

using Session = AppCommandState::ProjectSession;
using Kind = AppCommandState::ProjectAttachPrompt::Kind;

Session* FindSessionMut(AppCommandState& st, std::uint32_t uid) {
  for (Session& s : st.openProjects)
    if (s.uid == uid)
      return &s;
  return nullptr;
}

const Session* FindSession(const AppCommandState& st, std::uint32_t uid) {
  for (const Session& s : st.openProjects)
    if (s.uid == uid)
      return &s;
  return nullptr;
}

std::uint32_t ActiveProjectUid(const AppCommandState& st) {
  const int i = st.activeDrawingIdx;
  return i >= 1 && i < static_cast<int>(st.drawingTabs.size()) ? st.drawingTabs[static_cast<size_t>(i)].projectUid : 0u;
}

projfiles::Role RoleOf(Kind k) { return k == Kind::Pdf ? projfiles::Role::Pdf : projfiles::Role::PointCloud; }

/// The drawing's project-relative name, or "" when it is not inside the project folder.
std::string DrawingRel(const gsproj::Project& p, const std::string& drawingPath) {
  const fs::path f = fs::u8path(drawingPath);
  if (!projfiles::IsInsideProject(p, f))
    return {};
  std::error_code ec;
  const fs::path rel = fs::weakly_canonical(f, ec).lexically_relative(fs::weakly_canonical(p.Folder(), ec));
  return rel.generic_u8string();
}

json PlacementOf(const AppCommandState& st, const PdfAttachment& a) {
  json j;
  j["page"] = a.pageIndex;
  j["x"] = static_cast<double>(a.insertX) + st.worldDocumentOriginX;  // world: the local origin differs per drawing
  j["y"] = static_cast<double>(a.insertY) + st.worldDocumentOriginY;
  j["scale"] = a.scale;
  j["rotationDeg"] = a.rotationDeg;
  j["dpi"] = a.pageWidthPts > 0.f ? static_cast<double>(a.texW) * 72.0 / a.pageWidthPts : 150.0;
  j["snapLines"] = a.snapLines;
  j["snapCircles"] = a.snapCircles;
  j["snapText"] = a.snapText;
  j["layer"] = a.layer;
  j["fade"] = a.fade;
  j["showBackground"] = a.showBackground;
  return j;
}

}  // namespace

bool RequestProjectAttach(AppCommandState& st, Kind kind, const std::string& path, std::vector<std::string>&) {
  const Session* s = FindSession(st, ActiveProjectUid(st));
  if (s == nullptr || s->readOnly || path.empty())
    return false;
  projfiles::AttachPlan plan;
  std::string err;
  if (!projfiles::PlanAttach(s->project, fs::u8path(path), RoleOf(kind), &plan, &err))
    return false;  // the attach itself reports an unreadable file, as it always did
  if (plan.alreadyInProject)
    return false;
  AppCommandState::ProjectAttachPrompt& p = st.projectAttachPrompt;
  p.kind = kind;
  p.projectUid = s->uid;
  p.sourcePath = path;
  p.destRel = plan.destRel;
  p.sizeBytes = plan.sizeBytes;
  p.reuse = plan.reuseExisting;
  p.openRequested = true;
  return true;
}

bool ResolveProjectAttach(AppCommandState& st, bool copy, std::vector<std::string>& log, std::string* finalPath) {
  const AppCommandState::ProjectAttachPrompt p = st.projectAttachPrompt;
  st.projectAttachPrompt = {};
  const Session* s = FindSession(st, p.projectUid);
  if (s == nullptr || s->readOnly) {
    log.push_back("Attach - the project can no longer be written; nothing was attached.");
    return false;
  }
  const fs::path src = fs::u8path(p.sourcePath);
  if (!copy) {
    *finalPath = p.sourcePath;
    log.push_back("Linked " + src.filename().u8string() + " - it stays where it is and will not travel with the project.");
    return true;
  }
  projfiles::AttachPlan plan;
  std::string err;
  if (!projfiles::PlanAttach(s->project, src, RoleOf(p.kind), &plan, &err) ||
      !projfiles::CopyIn(plan, RoleOf(p.kind), &err)) {
    log.push_back("Attach - " + err + " Nothing was attached.");
    return false;
  }
  *finalPath = plan.dest.u8string();
  log.push_back(std::string(plan.reuseExisting ? "Using the copy already in the project: " : "Copied into the project: ") +
                plan.destRel);
  return true;
}

void SyncProjectFilesOnSave(AppCommandState& st, int tabIdx, const std::string& savedPath,
                            std::vector<std::string>& log) {
  if (tabIdx < 1 || tabIdx >= static_cast<int>(st.drawingTabs.size()))
    return;
  Session* s = FindSessionMut(st, st.drawingTabs[static_cast<size_t>(tabIdx)].projectUid);
  if (s == nullptr || s->readOnly)
    return;
  const std::string rel = DrawingRel(s->project, savedPath);
  if (rel.empty())
    return;
  std::vector<projfiles::Attached> clouds, pdfs;
  for (const auto& pc : st.cadPointClouds)
    if (pc && !pc->sourcePath.empty())
      clouds.push_back({fs::u8path(pc->sourcePath), {}});
  for (const PdfAttachment& a : st.pdfAttachments)
    if (!a.filePath.empty())
      pdfs.push_back({fs::u8path(a.filePath), PlacementOf(st, a).dump()});
  if (!projfiles::SyncDrawing(&s->project, rel, clouds, pdfs))
    return;
  std::string err;
  if (!gsproj::Save(s->project, &err))
    log.push_back("The project's file list could not be saved: " + err);
}

void ApplyProjectFilesOnOpen(AppCommandState& st, std::uint32_t projectUid, const std::string& drawingPath,
                             std::vector<std::string>& log) {
  const Session* s = FindSession(st, projectUid);
  if (s == nullptr)
    return;
  const std::string rel = DrawingRel(s->project, drawingPath);
  if (rel.empty())
    return;
  std::error_code ec;

  // Point clouds: the file the project tracks wins over the path the drawing remembered (that path may
  // be another machine's).
  for (size_t i = 0; i < st.cadPointClouds.size(); ++i) {
    const std::shared_ptr<const CadPointCloud>& pc = st.cadPointClouds[i];
    if (!pc || pc->sourcePath.empty())
      continue;
    const std::string found = projfiles::FindAttachedFile(s->project, rel, pc->sourcePath);
    if (found.empty()) {
      const std::string name = fs::u8path(pc->sourcePath).filename().u8string();
      if (projfiles::IsPackOmittedName(s->project, name))  // REQ-380: left out of the pack on purpose
        log.push_back("Point cloud " + name + " is unavailable (it was left out of the pack); showing its preview "
                      "sample only.");
      else
        log.push_back("Point cloud " + name + " is missing (" + pc->sourcePath + "); showing its preview sample only.");
      continue;
    }
    if (found == pc->sourcePath)
      continue;
    auto moved = std::make_shared<CadPointCloud>(*pc);
    moved->sourcePath = found;
    moved->cloudCachePath = found + ".gscloud";
    moved->octree = {};
    const pointcloudcache::OpenResult opened = pointcloudcache::Open(moved->cloudCachePath);
    if (opened.ok)
      moved->octree = opened.cache.octree;
    else
      log.push_back("Point cloud " + fs::u8path(found).filename().u8string() + " - .gscloud cache unavailable (" +
                    opened.errorMessage + "); showing preview sample only until re-imported.");
    st.cadPointClouds[i] = std::move(moved);
  }

  // PDFs: put each recorded placement back.
  for (const auto& [file, placementText] : projfiles::PlacementsFor(s->project, rel)) {
    const json pl = json::parse(placementText, nullptr, false);
    if (!pl.is_object())
      continue;
    const std::string name = fs::u8path(file).filename().u8string();
    if (file.empty() || !fs::exists(fs::u8path(file), ec)) {
      log.push_back("PDF " + name + " is missing from the project; its underlay was not placed.");
      continue;
    }
    PdfAttachment att;
    const bool ok = PdfAttach_Build(file.c_str(), pl.value("page", 0), pl.value("dpi", 150.f),
                                    pl.value("snapLines", true), pl.value("snapCircles", true),
                                    pl.value("snapText", true), att);
    if (!ok) {
      log.push_back("PDF " + name + " could not be read; its underlay was not placed.");
      continue;
    }
    att.insertX = static_cast<float>(pl.value("x", 0.0) - st.worldDocumentOriginX);
    att.insertY = static_cast<float>(pl.value("y", 0.0) - st.worldDocumentOriginY);
    att.scale = pl.value("scale", 1.f);
    att.rotationDeg = pl.value("rotationDeg", 0.f);
    att.layer = pl.value("layer", std::string());
    att.fade = pl.value("fade", 1.f);
    att.showBackground = pl.value("showBackground", false);
    st.pdfAttachments.push_back(std::move(att));
  }
}

projfiles::Health ProjectHealthFor(const AppCommandState& st, std::uint32_t projectUid) {
  const Session* s = FindSession(st, projectUid);
  if (s == nullptr)
    return {};
  std::vector<std::string> unsaved;
  std::vector<std::pair<std::string, std::string>> seen;  // (file, tab name) already listed
  for (size_t i = 1; i < st.drawingTabs.size(); ++i) {
    if (st.drawingTabs[i].projectUid != projectUid)
      continue;
    const bool active = static_cast<int>(i) == st.activeDrawingIdx;
    const bool dirty = active ? st.cadGpuRevision != st.activeDocSavedRevision
                              : i < st.documents.size() && st.documents[i].cadGpuRevision != st.documents[i].savedRevision;
    if (!dirty)
      continue;
    // The same file open in two tabs is one drawing with unsaved changes, not two (issue #726).
    const std::string& path = active ? st.activeDocFilePath : (i < st.documents.size() ? st.documents[i].filePath : std::string());
    const std::string& name = st.drawingTabs[i].name;
    const bool again = std::any_of(seen.begin(), seen.end(), [&](const auto& k) {
      return k.second == name && !path.empty() && k.first == path;
    });
    if (again)
      continue;
    seen.emplace_back(path, name);
    unsaved.push_back(name);
  }
  return projfiles::CheckHealth(s->project, unsaved);
}

bool CopyProjectLinksIn(AppCommandState& st, std::uint32_t projectUid, std::vector<std::string>& log,
                        size_t* converted) {
  *converted = 0;
  Session* s = FindSessionMut(st, projectUid);
  if (s == nullptr || s->readOnly) {
    log.push_back("Project Health - the project is read-only; nothing was copied.");
    return false;
  }
  const projfiles::CopyLinksResult r = projfiles::CopyLinksIn(&s->project);
  for (const std::string& f : r.failed)
    log.push_back("Project Health - could not copy " + f);
  *converted = r.converted;
  if (r.converted == 0)
    return true;
  std::string err;
  if (!gsproj::Save(s->project, &err)) {
    log.push_back("Project Health - the project file could not be saved: " + err);
    return false;
  }
  log.push_back("Project Health - copied " + std::to_string(r.converted) + " linked file(s) into the project.");
  return true;
}

bool CreateProjectTurnover(AppCommandState& st, std::uint32_t projectUid, const std::string& recipient,
                           const std::vector<std::string>& chosen, bool acknowledged,
                           std::vector<std::string>& log) {
  Session* s = FindSessionMut(st, projectUid);
  if (s == nullptr || s->readOnly) {
    log.push_back("Turnover - the project is read-only; nothing was written.");
    return false;
  }
  projturn::Record rec;
  std::string err;
  if (!projturn::Create(&s->project, recipient, chosen, static_cast<std::int64_t>(std::time(nullptr)),
                        ProjectHealthFor(st, projectUid), acknowledged, &rec, &err)) {
    log.push_back("Turnover - " + err);
    return false;
  }
  log.push_back("Turnover - recorded " + std::to_string(rec.items.size()) + " file(s) for " + rec.recipient + " on " +
                rec.date + " (" + rec.file + ").");
  return true;
}
