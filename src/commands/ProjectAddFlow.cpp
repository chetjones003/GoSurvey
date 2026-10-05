#include "ProjectAddFlow.hpp"

#include "ConvertDrawing.hpp"
#include "DwgIo.hpp"
#include "ProjectSettings.hpp"
#include "SurveyPoints.hpp"
#include "io/ProjectPointDb.hpp"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

using Session = AppCommandState::ProjectSession;

const Session* FindSession(const AppCommandState& st, std::uint32_t uid) {
  for (const Session& s : st.openProjects)
    if (s.uid == uid)
      return &s;
  return nullptr;
}

Session* FindSessionMut(AppCommandState& st, std::uint32_t uid) {
  for (Session& s : st.openProjects)
    if (s.uid == uid)
      return &s;
  return nullptr;
}

std::string FolderOf(const gsproj::Project& p, const char* role, const char* fallback) {
  const auto it = p.layout.find(role);
  return it != p.layout.end() ? it->second : std::string(fallback);
}

bool UsSurveyFoot(const DrawingSettings& ds) { return ds.footDefinition == DrawingSettings::FootDefinition::UsSurvey; }

geo::ConversionInput InputFor(const AppCommandState& d, const ProjectSettings& ps) {
  geo::ConversionInput in;
  in.fromZone = d.drawingSettings.zoneCode;
  in.toZone = ps.zoneCode;
  in.fromMetersPerUnit = geo::MetersPerInsUnit(d.drawingInsUnits, UsSurveyFoot(d.drawingSettings));
  const bool toUs = ps.hasDefaults ? ps.defaults.footDefinition == DrawingSettings::FootDefinition::UsSurvey
                                   : UsSurveyFoot(d.drawingSettings);
  in.toMetersPerUnit = ps.insUnits >= 0 ? geo::MetersPerInsUnit(ps.insUnits, toUs) : 0.0;
  DrawingWorldExtents(d, &in.minX, &in.maxX, &in.minY, &in.maxY);
  return in;
}

/// The preview that depends on the (possibly converted) drawing.
void Refresh(const AppCommandState& st, AddDrawingPlan* p) {
  const Session* s = FindSession(st, p->projectUid);
  const AppCommandState& d = *p->drawing;
  p->pointsWorld = projpts::ToWorld(d.surveyPoints, d.worldDocumentOriginX, d.worldDocumentOriginY);
  p->summary = projadd::Analyze(*s->points, p->pointsWorld);
  p->overrides.clear();
  const ProjectSettings& ps = *s->settings;
  if (ps.hasDefaults)
    for (unsigned k = 0; k < kProjectDefaultKeyCount; ++k)
      if (DiffersFromProjectDefault(ps, static_cast<ProjectDefaultKey>(k), d.modelUnitsPerPlottedInch,
                                    d.drawingSettings))
        p->overrides.push_back(kProjectDefaultKeyNames[k]);
}

bool LoadDrawing(const std::string& path, std::unique_ptr<AppCommandState>* out, std::vector<std::string>& log) {
  auto d = std::make_unique<AppCommandState>();
  std::vector<std::string> loadLog;
  const bool ok = OpenDrawingDocument(*d, path.c_str(), loadLog);
  for (std::string& l : loadLog)
    log.push_back(std::move(l));
  if (!ok)
    return false;
  *out = std::move(d);
  return true;
}

}  // namespace

void PrepareAddDrawing(const AppCommandState& st, std::uint32_t projectUid, const std::string& dwgPath,
                       AddDrawingPlan* out, std::vector<std::string>& log) {
  *out = AddDrawingPlan{};
  out->projectUid = projectUid;
  out->sourcePath = dwgPath;
  const Session* s = FindSession(st, projectUid);
  if (!s) {
    out->error = "Add Drawing to Project - the project is no longer open.";
    return;
  }
  out->projectName = s->project.name;
  if (s->readOnly) {
    out->error = "Add Drawing to Project - " + s->project.name + " is open read-only, so nothing can be added to it.";
    return;
  }
  if (!s->points || !s->settings) {
    out->error = "Add Drawing to Project - the project's point database could not be read (" +
                 (s->pointsError.empty() ? std::string("unknown reason") : s->pointsError) +
                 "), so nothing can be added to it.";
    return;
  }
  std::error_code ec;
  if (dwgPath.empty() || !fs::is_regular_file(fs::u8path(dwgPath), ec)) {
    out->error = "Add Drawing to Project - the drawing was not found: " + dwgPath;
    return;
  }
  if (!LoadDrawing(dwgPath, &out->drawing, log)) {
    out->error = "Add Drawing to Project - the drawing could not be read: " + dwgPath;
    return;
  }
  out->conversion = geo::PlanConversion(InputFor(*out->drawing, *s->settings));
  if (out->conversion.Needed())
    out->blockers = UnconvertibleKinds(*out->drawing, out->conversion.zoneDiffers);
  Refresh(st, out);
}

bool ChooseAddConvert(const AppCommandState& st, AddDrawingPlan* plan, std::vector<std::string>& log) {
  if (!plan->error.empty() || !plan->drawing || !FindSession(st, plan->projectUid))
    return false;
  if (!plan->CanConvert()) {
    log.push_back("Add Drawing to Project - this drawing cannot be converted: " +
                  (!plan->blockers.empty() ? std::string("it holds objects that cannot be moved")
                                           : plan->conversion.error));
    return false;
  }
  const Session* s = FindSession(st, plan->projectUid);
  ApplyDrawingConversion(*plan->drawing, plan->conversion.transform, log);
  // The drawing now speaks the project's coordinate system and unit.
  if (s->settings->insUnits >= 0)
    plan->drawing->drawingInsUnits = s->settings->insUnits;
  if (!s->settings->zoneCode.empty())
    plan->drawing->drawingSettings.zoneCode = s->settings->zoneCode;
  plan->converted = true;
  Refresh(st, plan);
  return true;
}

bool CommitAddDrawing(AppCommandState& st, AddDrawingPlan& plan, double now, AddDrawingResult* out,
                      std::vector<std::string>& log) {
  Session* s = FindSessionMut(st, plan.projectUid);
  if (!plan.error.empty() || !plan.drawing)
    return false;
  if (!s || s->readOnly || !s->points || !s->settings) {
    log.push_back("Add Drawing to Project - the project can no longer be written; nothing was added.");
    return false;
  }
  if (plan.Blocked()) {
    log.push_back("Add Drawing to Project - the drawing's coordinate system or units differ from the project's; "
                  "nothing was added.");
    return false;
  }

  // Where the copy goes: Drawings/<name>.dwg, never over an existing file (clause 3).
  const fs::path folder = s->project.Folder() / fs::u8path(FolderOf(s->project, "drawings", "Drawings"));
  const fs::path srcPath = fs::u8path(plan.sourcePath);
  std::string stem = srcPath.stem().u8string();
  if (stem.empty())
    stem = "Drawing";
  fs::path dest = folder / fs::u8path(stem + ".dwg");
  std::error_code ec;
  for (int n = 2; fs::exists(dest, ec) && n < 10000; ++n)
    dest = folder / fs::u8path(stem + " (" + std::to_string(n) + ").dwg");
  if (fs::exists(dest, ec)) {
    log.push_back("Add Drawing to Project - no free file name for the copy in " + folder.u8string() + ".");
    return false;
  }
  const std::string rel = dest.lexically_relative(s->project.Folder()).generic_u8string();

  // The database change is made on a copy first, so a failed file write leaves the project untouched.
  projpts::Db next = *s->points;
  const projadd::Outcome outcome = projadd::Apply(&next, plan.pointsWorld, plan.choices, rel, now);

  // The copy's own state: its points live in the database now, it shows exactly what it brought, the
  // settings that differ from the project become overrides, and the project's values are enforced.
  AppCommandState& d = *plan.drawing;
  for (size_t i = d.surveyPoints.size(); i-- > 0;)
    RemoveSurveyPointAt(d, i);  // with their label MTEXT
  d.pointVisibility = projadd::RulesShowing(outcome.shownIds);
  const ProjectSettings& ps = *s->settings;
  if (ps.hasDefaults)
    for (unsigned k = 0; k < kProjectDefaultKeyCount; ++k) {
      const auto key = static_cast<ProjectDefaultKey>(k);
      if (DiffersFromProjectDefault(ps, key, d.modelUnitsPerPlottedInch, d.drawingSettings))
        d.drawingSettings.SetOverridden(key, true);
    }
  ApplyProjectToDrawing(ps, &d.drawingInsUnits, &d.modelUnitsPerPlottedInch, &d.drawingSettings);
  d.drawingTabs.resize(2);
  d.drawingTabs[1].projectUid = plan.projectUid;
  d.drawingTabs[1].pointsMode = AppCommandState::DrawingTab::PointsMode::Shared;
  d.activeDrawingIdx = 1;  // so the trailer leaves the points out (ProjectOwnsActiveTabPoints)

  // A drawing that was not converted is copied byte for byte (everything in the DWG survives, even what
  // GoSurvey does not understand) with only its GoSurvey trailer replaced. A converted one has new
  // coordinates, so its DWG body must be rewritten to match: it is saved like any GoSurvey drawing.
  std::vector<std::string> wlog;
  const bool wrote = plan.converted
                         ? SaveDrawingDocument(d, dest.u8string().c_str(), wlog)
                         : CopyDwgWithGoSurveyPayload(plan.sourcePath.c_str(), dest.u8string().c_str(), d, wlog);
  for (std::string& l : wlog)
    log.push_back(std::move(l));
  if (!wrote) {
    log.push_back("Add Drawing to Project - the copy could not be written; nothing was added.");
    // The private copy was already rearranged for writing, so this plan cannot be previewed again:
    // the dialog closes on an error and the user starts over.
    plan.error = "Add Drawing to Project - the copy could not be written; nothing was added.";
    return false;
  }
  *s->points = std::move(next);

  out->destPath = dest.u8string();
  out->destRel = rel;
  out->outcome = outcome;
  char buf[320];
  std::snprintf(buf, sizeof(buf),
                "Added \"%s\" to project %s as %s: %zu point(s) added, %zu shared, %zu overwritten, %zu renumbered, "
                "%zu skipped.",
                stem.c_str(), s->project.name.c_str(), rel.c_str(), outcome.added, outcome.shared,
                outcome.overwritten, outcome.renumbered, outcome.skipped);
  log.push_back(buf);
  return true;
}
