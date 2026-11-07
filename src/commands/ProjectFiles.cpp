#include "ProjectFiles.hpp"

#include "io/ProjectTurnover.hpp"
#include "pdf/PdfAttach.hpp"
#include "util/pointcloudcache.hpp"
#include "io/DwgIo.hpp"
#include "io/GsIo.hpp"
#include "CadCommands.hpp"

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

projfiles::Role RoleOf(Kind k) {
  return (k == Kind::Pdf || k == Kind::PdfTrack) ? projfiles::Role::Pdf : projfiles::Role::PointCloud;
}

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

/// A copy of \p pc that reads from \p file (and its `.gscloud` cache beside it).
std::shared_ptr<const CadPointCloud> PointCloudAt(const CadPointCloud& pc, const std::string& file,
                                                  std::vector<std::string>& log) {
  auto moved = std::make_shared<CadPointCloud>(pc);
  moved->sourcePath = file;
  moved->cloudCachePath = file + ".gscloud";
  moved->octree = {};
  const pointcloudcache::OpenResult opened = pointcloudcache::Open(moved->cloudCachePath);
  if (opened.ok)
    moved->octree = opened.cache.octree;
  else
    log.push_back("Point cloud " + fs::u8path(file).filename().u8string() + " - .gscloud cache unavailable (" +
                  opened.errorMessage + "); showing preview sample only until re-imported.");
  return moved;
}

bool SamePath(const std::string& a, const std::string& b) {
  if (a == b)
    return true;
  std::error_code ec;
  return fs::equivalent(fs::u8path(a), fs::u8path(b), ec);
}

bool SameProjectRel(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const unsigned char ac = static_cast<unsigned char>(a[i] == '\\' ? '/' : a[i]);
    const unsigned char bc = static_cast<unsigned char>(b[i] == '\\' ? '/' : b[i]);
    if (std::tolower(ac) != std::tolower(bc)) return false;
  }
  return true;
}

CadSurface MakeLocalSurfaceReference(const CadSurface& source, const AppCommandState& sourceState,
                                     const AppCommandState& destinationState, const std::string& name,
                                     const std::string& sourceRel, std::uint64_t sourceId) {
  CadSurface reference = source;
  const double dx = sourceState.worldDocumentOriginX - destinationState.worldDocumentOriginX;
  const double dy = sourceState.worldDocumentOriginY - destinationState.worldDocumentOriginY;
  if (source.tin) {
    auto localTin = std::make_shared<CadTin>(*source.tin);
    for (size_t i = 0; i + 2 < localTin->vertsXyz.size(); i += 3) {
      localTin->vertsXyz[i] += dx;
      localTin->vertsXyz[i + 1] += dy;
    }
    reference.tin = std::move(localTin);
  }
  // A grid surface samples its own grid in the same local frame as its TIN; move the grid origin with it.
  if (source.kind == SurfaceKind::Grid) {
    reference.gridOriginX += dx;
    reference.gridOriginY += dy;
  }
  reference.name = name;
  reference.sourcePointGroups.clear();
  reference.sourcePointFiles.clear();
  reference.addedPointXyz.clear();
  reference.deletedPointPicks.clear();
  reference.movedPoints.clear();
  reference.breaklines.clear();
  reference.crossingCorrections.clear();
  reference.contourSources.clear();
  reference.boundaries.clear();
  reference.swappedEdgePicks.clear();
  reference.deletedEdgePicks.clear();
  reference.corridorFeatureLines.clear();
  reference.gradingGroup = false;
  reference.gradingMembers.clear();
  reference.volumeBaseName.clear();
  reference.volumeComparisonName.clear();
  reference.dataShortcutReadOnly = true;
  reference.dataShortcutDrawing = sourceRel;
  reference.dataShortcutSurfaceId = sourceId;
  return reference;
}

/// "Copy links into the project" moved files: every open drawing of project \p uid that still holds
/// the old path takes the copy. Without this the next save of such a drawing records the old path as a
/// link again and the file never travels (issue #724). The active tab's data lives in \p st, the others'
/// in their document snapshots. Nothing is marked as changed: the project already records the copy, and a
/// drawing saved later writes the new path.
void RepointOpenDrawings(AppCommandState& st, std::uint32_t uid,
                         const std::vector<std::pair<std::string, std::string>>& moved,
                         std::vector<std::string>& log) {
  const auto newPathFor = [&](const std::string& old) -> const std::string* {
    for (const auto& m : moved)
      if (SamePath(m.first, old))
        return &m.second;
    return nullptr;
  };
  const auto repoint = [&](std::vector<PdfAttachment>& pdfs,
                           std::vector<std::shared_ptr<const CadPointCloud>>& clouds,
                           std::vector<CadRasterImage>& rasters) {
    for (PdfAttachment& a : pdfs)
      if (const std::string* np = newPathFor(a.filePath))
        a.filePath = *np;
    for (auto& pc : clouds)
      if (pc && !pc->sourcePath.empty())
        if (const std::string* np = newPathFor(pc->sourcePath))
          pc = PointCloudAt(*pc, *np, log);
    for (CadRasterImage& image : rasters)
      if (!image.path.empty())
        if (const std::string* np = newPathFor(image.path))
          image.path = *np;
  };
  for (size_t i = 1; i < st.drawingTabs.size(); ++i) {
    if (st.drawingTabs[i].projectUid != uid)
      continue;
    if (static_cast<int>(i) == st.activeDrawingIdx)
      repoint(st.pdfAttachments, st.cadPointClouds, st.cadRasterImages);
    else if (i < st.documents.size())
      repoint(st.documents[i].pdfAttachments, st.documents[i].cadPointClouds,
              st.documents[i].cadRasterImages);
  }
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

bool TrackProjectFile(AppCommandState& st, std::uint32_t uid, const std::string& path, std::vector<std::string>& log) {
  Session* s = FindSessionMut(st, uid);
  if (s == nullptr || s->readOnly) {
    log.push_back("Add PDF - the project can no longer be written; nothing was added.");
    return false;
  }
  const std::string name = fs::u8path(path).filename().u8string();
  if (!projfiles::TrackFile(&s->project, fs::u8path(path))) {
    log.push_back(name + " is already in the project's file list.");
    return false;
  }
  std::string err;
  if (!gsproj::Save(s->project, &err)) {
    log.push_back("The project's file list could not be saved: " + err);
    return false;
  }
  log.push_back("Added " + name + " to the project's file list.");
  return true;
}

int TrackProjectFiles(AppCommandState& st, std::uint32_t uid, const std::vector<std::string>& rels,
                      std::vector<std::string>& log) {
  Session* s = FindSessionMut(st, uid);
  if (s == nullptr || s->readOnly) {
    log.push_back("Refresh - the project can no longer be written; nothing was tracked.");
    return 0;
  }
  int added = 0;
  for (const std::string& rel : rels)
    if (gsproj::IsSafeRelativePath(rel) && projfiles::TrackFile(&s->project, s->project.Folder() / fs::u8path(rel)))
      ++added;
  if (added == 0)
    return 0;
  std::string err;
  if (!gsproj::Save(s->project, &err)) {
    log.push_back("The project's file list could not be saved: " + err);
    return 0;
  }
  log.push_back("Now tracking " + std::to_string(added) + " new file" + (added == 1 ? "" : "s") + " in the project.");
  return added;
}

bool RefreshProjectFiles(AppCommandState& st, std::vector<std::string>& log) {
  const std::uint32_t uid = ActiveProjectUid(st);
  const Session* s = FindSession(st, uid);
  if (s == nullptr) {
    log.push_back("Refresh - this drawing is not in a project.");
    return false;
  }
  if (s->readOnly) {
    log.push_back("Refresh - this project is open read-only.");
    return false;
  }
  AppCommandState::ProjectRefreshPrompt& p = st.projectRefreshPrompt;
  p = {};
  p.files = projfiles::FindUntracked(s->project);
  if (p.files.empty()) {
    log.push_back("Refresh - no new files in the project folder.");
    return false;
  }
  p.projectUid = uid;
  p.picked.assign(p.files.size(), 1);
  p.openRequested = true;
  log.push_back("Refresh - found " + std::to_string(p.files.size()) + " new file" + (p.files.size() == 1 ? "" : "s") +
                " not tracked by the project.");
  return true;
}

bool AddPdfToProject(AppCommandState& st, const std::string& path, std::vector<std::string>& log) {
  const std::uint32_t uid = ActiveProjectUid(st);
  const Session* s = FindSession(st, uid);
  if (s == nullptr || path.empty()) {
    log.push_back("Add PDF - open a project drawing first.");
    return false;
  }
  if (s->readOnly) {
    log.push_back("Add PDF - this project is open read-only.");
    return false;
  }
  std::string ext = fs::u8path(path).extension().u8string();
  for (char& c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  std::error_code ec;
  if (ext != ".pdf" || !fs::is_regular_file(fs::u8path(path), ec)) {
    log.push_back("Add PDF - " + path + " is not a PDF file that can be read.");
    return false;
  }
  if (projfiles::IsInsideProject(s->project, fs::u8path(path)))
    return TrackProjectFile(st, uid, path, log);
  return RequestProjectAttach(st, Kind::PdfTrack, path, log);
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

bool CompleteProjectAttach(AppCommandState& st, AttachAnswer answer, std::vector<std::string>& log,
                           std::string* pdfPath) {
  const Kind kind = st.projectAttachPrompt.kind;
  const std::uint32_t uid = st.projectAttachPrompt.projectUid;
  if (kind == Kind::None)
    return false;
  if (answer == AttachAnswer::Cancel) {
    st.projectAttachPrompt = {};
    log.push_back("Attach cancelled.");
    return false;
  }
  std::string finalPath;
  if (!ResolveProjectAttach(st, answer == AttachAnswer::Copy, log, &finalPath))
    return false;
  if (kind == Kind::PdfTrack)  // REQ-379 clause 5: tracked only, never placed
    return TrackProjectFile(st, uid, finalPath, log);
  if (kind == Kind::PointCloud)
    return StartPointCloudImportAsync(st, finalPath, log);
  if (pdfPath != nullptr)
    *pdfPath = finalPath;
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
  // REQ-511 image paths use the same project-file association lifecycle as point-cloud sources;
  // their placements remain in the drawing trailer's modelImages array.
  for (const CadRasterImage& image : st.cadRasterImages)
    if (!image.path.empty()) clouds.push_back({fs::u8path(image.path), {}});
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
    st.cadPointClouds[i] = PointCloudAt(*pc, found, log);
  }

  // Model images are tracked with the drawing just like other external attachments. Resolve their
  // project-owned copy before the viewport tries to decode the path saved by the source machine.
  for (CadRasterImage& image : st.cadRasterImages) {
    if (image.path.empty()) continue;
    const std::string found = projfiles::FindAttachedFile(s->project, rel, image.path);
    if (found.empty()) {
      log.push_back("Model image " + fs::u8path(image.path).filename().u8string() +
                    " is missing from the project; its drape is unavailable.");
      continue;
    }
    image.path = found;
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
  RepointOpenDrawings(st, projectUid, r.moved, log);
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

bool CreateSurfaceDataShortcut(AppCommandState& st, size_t surfaceIndex, std::vector<std::string>& log) {
  const std::uint32_t uid = ActiveProjectUid(st);
  Session* session = FindSessionMut(st, uid);
  if (!session) { log.push_back("Data Shortcut — open a project drawing first."); return false; }
  if (session->readOnly) { log.push_back("Data Shortcut — this project is read-only; no shortcut was created."); return false; }
  if (surfaceIndex >= st.cadSurfaces.size() || surfaceIndex >= st.cadSurfaceAttrs.size()) {
    log.push_back("Data Shortcut — the selected surface no longer exists."); return false;
  }
  if (st.activeDocFilePath.empty()) { log.push_back("Data Shortcut — save the source drawing inside the project first."); return false; }
  if (st.cadGpuRevision != st.activeDocSavedRevision) {
    log.push_back("Data Shortcut — save the source drawing and its surface changes before creating a shortcut.");
    return false;
  }
  const std::string rel = DrawingRel(session->project, st.activeDocFilePath);
  if (rel.empty()) { log.push_back("Data Shortcut — the source drawing must be inside this project."); return false; }
  const std::uint64_t surfaceId = st.cadSurfaceAttrs[surfaceIndex].id;
  if (!surfaceId) {
    log.push_back("Data Shortcut — the surface has no saved stable identity; save the drawing before creating a shortcut.");
    return false;
  }
  json extra = json::parse(session->project.extraJson, nullptr, false);
  if (!extra.is_object()) extra = json::object();
  json shortcuts = extra.value("surfaceShortcuts", json::array());
  if (!shortcuts.is_array()) shortcuts = json::array();
  for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it)
    if (it->is_object() && it->value("drawing", std::string()) == rel &&
        it->value("surfaceId", std::uint64_t{0}) == surfaceId) { *it = {{"drawing", rel}, {"surfaceId", surfaceId}, {"name", st.cadSurfaces[surfaceIndex].name}}; goto save_shortcut; }
  shortcuts.push_back({{"drawing", rel}, {"surfaceId", surfaceId}, {"name", st.cadSurfaces[surfaceIndex].name}});
save_shortcut:
  extra["surfaceShortcuts"] = std::move(shortcuts);
  session->project.extraJson = extra.dump();
  (void)projfiles::TrackFile(&session->project, fs::u8path(st.activeDocFilePath));
  std::string error;
  if (!gsproj::Save(session->project, &error)) { log.push_back("Data Shortcut — project file could not be saved: " + error); return false; }
  log.push_back("Data Shortcut — surface \"" + st.cadSurfaces[surfaceIndex].name + "\" is available to drawings in this project.");
  return true;
}

bool CreateSurfaceReference(AppCommandState& st, const std::string& requestedSourceRel,
                            std::uint64_t sourceSurfaceId,
                            const std::string& referenceName, std::vector<std::string>& log) {
  Session* session = FindSessionMut(st, ActiveProjectUid(st));
  if (!session) { log.push_back("Create Surface Reference — open a project drawing first."); return false; }
  if (session->readOnly) { log.push_back("Create Surface Reference — this project is read-only; no reference was created."); return false; }
  if (!sourceSurfaceId || !gsproj::IsSafeRelativePath(requestedSourceRel) || referenceName.empty() ||
      FindSurfaceIndex(st, referenceName) >= 0) {
    log.push_back("Create Surface Reference — provide a registered source drawing, surface ID, and unique reference name."); return false;
  }
  json extra = json::parse(session->project.extraJson, nullptr, false);
  const json shortcuts = extra.is_object() ? extra.value("surfaceShortcuts", json::array()) : json::array();
  std::string sourceRel;
  if (shortcuts.is_array()) for (const auto& shortcut : shortcuts)
    if (shortcut.is_object() && shortcut.value("surfaceId", std::uint64_t{0}) == sourceSurfaceId &&
        SameProjectRel(shortcut.value("drawing", std::string()), requestedSourceRel)) {
      sourceRel = shortcut.value("drawing", std::string()); break;
    }
  if (!gsproj::IsSafeRelativePath(sourceRel)) { log.push_back("Create Surface Reference — that surface ID is not registered in this project."); return false; }
  const fs::path sourcePath = session->project.Folder() / fs::u8path(sourceRel);
  if (!fs::exists(sourcePath)) { log.push_back("Create Surface Reference — source drawing \"" + sourceRel + "\" is missing."); return false; }
  AppCommandState source;
  source.activeDrawingIdx = 1;
  source.documents.resize(2);
  source.drawingTabs.resize(2);
  const std::string sourceUtf8 = sourcePath.u8string();
  if (!OpenDrawingDocument(source, sourceUtf8.c_str(), log)) {
    log.push_back("Create Surface Reference — source drawing could not be opened; no reference was created."); return false;
  }
  const auto sourceAttr = std::find_if(source.cadSurfaceAttrs.begin(), source.cadSurfaceAttrs.end(),
      [&](const EntityAttributes& attr) { return attr.id == sourceSurfaceId; });
  if (sourceAttr == source.cadSurfaceAttrs.end()) { log.push_back("Create Surface Reference — source surface is missing from its drawing."); return false; }
  const size_t sourceIndex = static_cast<size_t>(sourceAttr - source.cadSurfaceAttrs.begin());
  if (sourceIndex >= source.cadSurfaces.size() || !source.cadSurfaces[sourceIndex].tin) {
    log.push_back("Create Surface Reference — source surface has no saved built TIN."); return false;
  }
  CadSurface reference = MakeLocalSurfaceReference(source.cadSurfaces[sourceIndex], source, st,
                                                    referenceName, sourceRel, sourceSurfaceId);
  PushUndoSnapshot(st, "Create surface reference");
  st.cadSurfaces.push_back(std::move(reference));
  EntityAttributes attrs;
  attrs.layer = "0";
  st.cadSurfaceAttrs.push_back(std::move(attrs));
  BumpCadGpuCache(st);
  EnsureEntityIds(st);
  log.push_back("Create Surface Reference — read-only reference \"" + referenceName + "\" created from " + sourceRel + ".");
  return true;
}

void RefreshProjectSurfaceReferences(AppCommandState& st, std::uint32_t projectUid,
                                     std::vector<std::string>& log) {
  const Session* session = FindSession(st, projectUid);
  if (!session) return;
  if (session->readOnly) {
    log.push_back("Project surface references were not refreshed because the project is read-only.");
    return;
  }
  for (size_t i = 0; i < st.cadSurfaces.size(); ++i) {
    CadSurface& surface = st.cadSurfaces[i];
    if (!surface.dataShortcutReadOnly || !gsproj::IsSafeRelativePath(surface.dataShortcutDrawing)) continue;
    const std::string sourceRel = surface.dataShortcutDrawing;
    const std::uint64_t sourceId = surface.dataShortcutSurfaceId;
    const fs::path path = session->project.Folder() / fs::u8path(sourceRel);
    AppCommandState source;
    source.activeDrawingIdx = 1; source.documents.resize(2); source.drawingTabs.resize(2);
    const std::string sourceUtf8 = path.u8string();
    if (!fs::exists(path) || !OpenDrawingDocument(source, sourceUtf8.c_str(), log)) {
      log.push_back("Surface reference \"" + surface.name + "\" — source drawing is unavailable; last saved display retained.");
      continue;
    }
    const auto attr = std::find_if(source.cadSurfaceAttrs.begin(), source.cadSurfaceAttrs.end(),
        [&](const EntityAttributes& candidate) { return candidate.id == sourceId; });
    const size_t sourceIndex = attr == source.cadSurfaceAttrs.end() ? source.cadSurfaces.size()
        : static_cast<size_t>(attr - source.cadSurfaceAttrs.begin());
    if (sourceIndex >= source.cadSurfaces.size() || !source.cadSurfaces[sourceIndex].tin) {
      log.push_back("Surface reference \"" + surface.name + "\" — source surface is unavailable; last saved display retained.");
      continue;
    }
    const std::string name = surface.name, style = surface.styleName;
    const bool reduced = surface.reducedDisplayLod;
    surface = MakeLocalSurfaceReference(source.cadSurfaces[sourceIndex], source, st,
                                        name, sourceRel, sourceId);
    surface.styleName = style; surface.reducedDisplayLod = reduced;
    RefreshSurfaceDependentsAfterReplacement(st, i, log);
  }
}
