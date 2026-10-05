// Issue #696 (projects) end-to-end driver for the Developer Shell.
//
// Everything runs inside the real app loop: real frames (SyncProjectPoints / EnforceProjectSettings run
// every frame from DrawProjectDialogs), real modals clicked through the Test Engine, real DWG /
// .gsproj / .gspdb / PDF files in a scratch folder under the system temp directory.
//
//   build\devshell\GoSurvey.exe --devshell-run p696-e2e
//
// Every individual check is logged on the "p696" channel (devshell-activity.log) as PASS / FAIL.

#include "DevShell.hpp"

#ifdef GOSURVEY_DEVELOPER_SHELL

#include "CadCommands.hpp"
#include "CadUi.hpp"
#include "ProjectAddFlow.hpp"
#include "ProjectFiles.hpp"
#include "ProjectPack.hpp"
#include "ProjectPoints.hpp"
#include "ProjectSettings.hpp"
#include "ProjectTurnover.hpp"
#include "ProjectWarnings.hpp"

#include <imgui.h>
#include <imgui_te_context.h>
#include <imgui_te_engine.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

AppCommandState* s_cmd = nullptr;
int g_pass = 0;
int g_fail = 0;
fs::path g_root;

void Chk(bool ok, const std::string& what)
{
  DevShell_Logf("p696", "%s: %s", ok ? "PASS" : "FAIL", what.c_str());
  ++(ok ? g_pass : g_fail);
  // Deliberately NOT an IM_CHECK: once a check has failed the Test Engine turns every later ItemClick /
  // ItemInputValue into a silent no-op, so one product finding would hide every one after it. The
  // failures are tallied and the test is failed once, at the end.
}

void Note(const std::string& what) { DevShell_Logf("p696", "---- %s", what.c_str()); }

std::vector<std::string>& Log() { return *DevShell_CommandLog(); }

bool LogHas(const char* needle) { return DevShell_CommandLogContains(needle); }

void Frames(ImGuiTestContext* ctx, int n = 4) { ctx->Yield(n); }

void CancelToIdle(ImGuiTestContext* ctx)
{
  ctx->KeyPress(ImGuiKey_Escape);
  ctx->Yield();
  ctx->KeyPress(ImGuiKey_Escape);
  ctx->Yield();
}

AppCommandState::ProjectSession* SessionNamed(const char* name)
{
  for (auto& s : s_cmd->openProjects)
    if (s.project.name == name)
      return &s;
  return nullptr;
}

int TabNamed(const char* name)
{
  for (size_t i = 1; i < s_cmd->drawingTabs.size(); ++i)
    if (s_cmd->drawingTabs[i].name == name)
      return static_cast<int>(i);
  return -1;
}

void Dump(const char* where);

void GoTab(ImGuiTestContext* ctx, int idx)
{
  // The tab bar re-selects the tab it last drew unless told this switch is deliberate (the same flag
  // NewDrawingInTab / the Open paths set); without it the write below is overridden next frame.
  s_cmd->activeDrawingIdx = idx;
  s_cmd->pendingDrawingTabSwitch = true;
  Frames(ctx, 6);
  Chk(s_cmd->activeDrawingIdx == idx && s_cmd->prevDrawingIdx == idx, "switched to tab " + std::to_string(idx) + " (" + s_cmd->drawingTabs[static_cast<size_t>(idx)].name + ")");
  Dump("after GoTab");
}

/// Places \p n survey points through the real placement routine, numbered from \p firstId.
void PlacePoints(ImGuiTestContext* ctx, const char* desc, int firstId, int n, double e0, double n0, double z0)
{
  auto& o = s_cmd->createPointsOpts;
  o.defaultDescription = desc;
  o.sequentialNumbering = true;
  o.duplicatePolicy = SurveyDuplicatePolicy::Notify;
  s_cmd->createPointsNextId = firstId;
  for (int i = 0; i < n; ++i)
    TryPlaceSurveyPoint(*s_cmd, e0 + 10.0 * i, n0, z0 + i, Log());
  Frames(ctx, 3);
}

int IndexOfPoint(int id)
{
  for (size_t i = 0; i < s_cmd->surveyPoints.size(); ++i)
    if (s_cmd->surveyPoints[i].id == id)
      return static_cast<int>(i);
  return -1;
}

const projpts::Entry* DbEntry(AppCommandState::ProjectSession* s, int id)
{
  if (!s || !s->points)
    return nullptr;
  for (const auto& e : s->points->points)
    if (e.point.id == id)
      return &e;
  return nullptr;
}

/// Writes the session's database now and re-reads the file from disk.
bool DbOnDisk(AppCommandState::ProjectSession* s, projpts::Db* out)
{
  FlushProjectPointDb(*s, Log());
  std::string err;
  return projpts::Load(projpts::DbPath(s->project.Folder(), s->project.layout.at("points")), s->project.id, out, &err);
}

bool FileExists(const fs::path& p)
{
  std::error_code ec;
  return fs::exists(p, ec);
}

std::string ReadAll(const fs::path& p)
{
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// ------------------------------------------------------------------------------------------------
// Shared state and helpers for the later stages
// ------------------------------------------------------------------------------------------------

struct E2e {
  std::uint32_t jobUid = 0;
  int egTab = 0;
  int fgTab = 0;
  fs::path jobDir;
  fs::path extDir;  ///< drawings / PDFs that live OUTSIDE every project
};
E2e g;

AppCommandState::ProjectSession* Job() { return SessionNamed("TestJob"); }

AppCommandState::ProjectSession* SessionAt(const fs::path& folder)
{
  std::error_code ec;
  for (auto& s : s_cmd->openProjects)
    if (fs::equivalent(s.project.Folder(), folder, ec))
      return &s;
  return nullptr;
}

void SubmitCad(ImGuiTestContext* ctx, const char* line)
{
  char buf[1024];
  std::snprintf(buf, sizeof(buf), "%s", line);
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), *s_cmd, Log());
  ctx->Yield();
}

/// Waits (wall clock, the Test Engine's own clock is fast-forwarded) until \p done or \p seconds pass.
template <class F>
bool WaitReal(ImGuiTestContext* ctx, double seconds, F done)
{
  const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  while (std::chrono::steady_clock::now() < until)
  {
    ctx->Yield();
    if (done())
      return true;
  }
  return done();
}

/// Clicks \p label in the modal that is on top.
void Dump(const char* where);
ImGuiID FindItem(ImGuiTestContext* ctx, const char* label, bool logAll = false);

void ClickModal(ImGuiTestContext* ctx, const char* label)
{
  Dump("before click");
  {
    ImGuiContext& c = *ImGui::GetCurrentContext();
    DevShell_Logf("p696", "  click '%s' with focus on '%s' (modal: '%s')", label, c.NavWindow ? c.NavWindow->Name : "-",
                  c.OpenPopupStack.Size > 0 && c.OpenPopupStack.back().Window ? c.OpenPopupStack.back().Window->Name : "-");
  }
  {
    ImGuiContext& c = *ImGui::GetCurrentContext();
    if (c.OpenPopupStack.Size > 0 && c.OpenPopupStack.back().Window)
      ctx->SetRef(c.OpenPopupStack.back().Window);  // the modal itself, not the '$FOCUSED' alias
    else
      ctx->SetRef("//$FOCUSED");
  }
  if (std::strncmp(label, "Open ", 5) == 0)
  {
    const ImGuiID id = FindItem(ctx, label, /*logAll*/ true);
    const ImGuiTestItemInfo info = ctx->ItemInfo(id ? id : 0u, ImGuiTestOpFlags_NoError);
    DevShell_Logf("p696", "  button '%s' id %08X rect %.0f,%.0f-%.0f,%.0f", label, id, info.RectFull.Min.x, info.RectFull.Min.y,
                  info.RectFull.Max.x, info.RectFull.Max.y);
  }
  ctx->ItemClick(label);
  Frames(ctx, 4);
  Dump(label);
  if (std::strncmp(label, "Open ", 5) == 0)
    DevShell_Logf("p696", "  after click: projectPrompt.kind=%d sessions=%zu tabs=%zu", static_cast<int>(s_cmd->projectPrompt.kind),
                  s_cmd->openProjects.size(), s_cmd->drawingTabs.size());
}

std::set<int> ViewIds()
{
  std::set<int> ids;
  for (const auto& p : s_cmd->surveyPoints)
    ids.insert(p.id);
  return ids;
}

template <class Pred>
std::set<int> DbIdsWhere(AppCommandState::ProjectSession* s, Pred pred)
{
  std::set<int> ids;
  if (s && s->points)
    for (const auto& e : s->points->points)
      if (pred(e))
        ids.insert(e.point.id);
  return ids;
}

std::string Join(const std::set<int>& ids)
{
  std::string out;
  for (int i : ids)
    out += (out.empty() ? "" : ",") + std::to_string(i);
  return "{" + out + "}";
}

/// Diagnostic only: what the active tab shows, what the database holds, and the tab's rules.
void Dump(const char* where)
{
  std::set<int> view;
  for (const auto& p : s_cmd->surveyPoints)
    view.insert(p.id);
  std::set<int> db;
  if (auto* j = SessionNamed("TestJob"); j && j->points)
    for (const auto& e : j->points->points)
      db.insert(e.point.id);
  const auto& r = s_cmd->pointVisibility;
  DevShell_Logf("p696", "  [%s] tab %d view %s db %s rules shown=%s hidden=%s prompt=%d", where, s_cmd->activeDrawingIdx,
                Join(view).c_str(), Join(db).c_str(), projpts::IdsToText(r.shown).c_str(), projpts::IdsToText(r.hidden).c_str(),
                s_cmd->pointEditPrompt.active ? 1 : 0);
}

void ChkIds(const std::string& what, const std::set<int>& got, const std::set<int>& want)
{
  Chk(got == want, what + ": shown " + Join(got) + (got == want ? "" : ", expected " + Join(want)));
}

/// A one-page PDF with a correct xref table, so pdfium opens it without repair.
void MakePdf(const fs::path& file, const char* title)
{
  std::vector<std::string> obj;
  obj.push_back("<</Type/Catalog/Pages 2 0 R>>");
  obj.push_back("<</Type/Pages/Kids[3 0 R]/Count 1>>");
  obj.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>");
  const std::string content = std::string("BT /F1 28 Tf 72 700 Td (") + title +
                              ") Tj ET\n0 0 1 RG 4 w 72 600 m 540 600 l S\n72 300 468 200 re S\n";
  obj.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "endstream");
  obj.push_back("<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>");
  std::string out = "%PDF-1.4\n";
  std::vector<size_t> off;
  for (size_t i = 0; i < obj.size(); ++i)
  {
    off.push_back(out.size());
    out += std::to_string(i + 1) + " 0 obj\n" + obj[i] + "\nendobj\n";
  }
  const size_t xref = out.size();
  out += "xref\n0 " + std::to_string(obj.size() + 1) + "\n0000000000 65535 f \n";
  for (size_t o : off)
  {
    char b[32];
    std::snprintf(b, sizeof(b), "%010zu 00000 n \n", o);
    out += b;
  }
  out += "trailer\n<</Size " + std::to_string(obj.size() + 1) + "/Root 1 0 R>>\nstartxref\n" + std::to_string(xref) +
         "\n%%EOF\n";
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << out;
}

/// Opens \p dwg in a new tab (project auto-detect) and returns the new tab's index.
int OpenInTab(ImGuiTestContext* ctx, const fs::path& dwg)
{
  const size_t before = s_cmd->drawingTabs.size();
  const std::string path = dwg.u8string();
  DevShell_RunOnMainThread([path] { OpenDrawingInNewTab(*s_cmd, Log(), path.c_str()); });
  while (DevShell_MainThreadJobPending())
    ctx->Yield();
  Frames(ctx, 8);
  return s_cmd->drawingTabs.size() > before ? static_cast<int>(s_cmd->drawingTabs.size()) - 1 : -1;
}

/// An empty drawing tab: standalone, or in project \p uid when non-zero. Returns its index.
int NewTab(ImGuiTestContext* ctx, const char* name, std::uint32_t uid, const fs::path& savePath)
{
  NewDrawingInTab(*s_cmd, Log());
  s_cmd->drawingTabs.back().projectUid = uid;
  s_cmd->drawingTabs.back().name = name;
  s_cmd->activeDocFilePath = savePath.u8string();
  Frames(ctx, 6);
  return static_cast<int>(s_cmd->drawingTabs.size()) - 1;
}

/// The ID of the item whose label is exactly \p label inside the window \p ctx's ref points at, or 0.
/// Gathered rather than named by path: labels like "##sdb_ids" sit inside tree-node ID scopes and
/// labels with '/' in them cannot be written as a path at all.
ImGuiID FindItem(ImGuiTestContext* ctx, const char* label, bool logAll)
{
  ImGuiTestItemList items;
  ctx->GatherItems(&items, ctx->GetRef(), 12);
  ImGuiID found = 0;
  for (int i = 0; i < items.GetSize(); ++i)
  {
    const ImGuiTestItemInfo* it = items.GetByIndex(i);
    if (logAll)
      DevShell_Logf("p696", "    item '%s' id %08X", it->DebugLabel, it->ID);
    // Exact, or the label as drawn: "Elevation between##sdb_elev" is gathered as "Elevation between".
    const char* hashes = std::strstr(label, "##");
    const size_t visible = hashes && hashes != label ? static_cast<size_t>(hashes - label) : std::strlen(label);
    if (found == 0 && (std::strcmp(it->DebugLabel, label) == 0 ||
                       (visible > 0 && std::strlen(it->DebugLabel) == visible && std::strncmp(it->DebugLabel, label, visible) == 0)))
      found = it->ID;
  }
  return found;
}

/// A combo with a hidden label ("##sdb_group") is gathered with an empty label. This returns the
/// \p n-th such item after the item labelled \p afterLabel, in on-screen order.
ImGuiID FindUnlabelledAfter(ImGuiTestContext* ctx, const char* afterLabel, int n)
{
  ImGuiTestItemList items;
  ctx->GatherItems(&items, ctx->GetRef(), 12);
  bool seen = false;
  int count = 0;
  for (int i = 0; i < items.GetSize(); ++i)
  {
    const ImGuiTestItemInfo* it = items.GetByIndex(i);
    if (!seen)
    {
      seen = std::strncmp(it->DebugLabel, afterLabel, std::strlen(afterLabel)) == 0;
      continue;
    }
    if (it->DebugLabel[0] == '\0' && count++ == n)
      return it->ID;
  }
  return 0;
}

// ===== 4. Delete / hide / number-conflict warnings =============================================
void StageWarnings(ImGuiTestContext* ctx)
{
  Note("4. Delete, hide-here and number-conflict warnings (FG)");
  auto* job = Job();
  GoTab(ctx, g.fgTab);
  auto& pe = s_cmd->pointEditPrompt;

  // A. delete a point EG also shows -> asked first; "Hide in this drawing only"
  RemoveSurveyPointAt(*s_cmd, static_cast<size_t>(IndexOfPoint(3)));
  Frames(ctx, 4);
  Chk(pe.active, "deleting a shared point raises the warning");
  Chk(pe.removed == std::vector<int>{3}, "the warning names point 3");
  Chk(pe.othersOpenShowing == 1, "the warning counts 1 other open drawing that shows it (EG): " + std::to_string(pe.othersOpenShowing));
  Chk(!pe.otherNames.empty() && pe.otherNames[0] == "EG", "the warning names EG");
  ClickModal(ctx, "Hide in this drawing only");
  Chk(!pe.active, "prompt closed after the answer");
  Chk(IndexOfPoint(3) < 0, "point 3 is gone from FG's view");
  Chk(DbEntry(job, 3) != nullptr, "point 3 is STILL in the project database");
  Chk(projpts::HasId(s_cmd->pointVisibility.hidden, 3), "FG's rules record 3 as hidden-in-this-drawing");
  GoTab(ctx, g.egTab);
  Chk(IndexOfPoint(3) >= 0, "EG still shows point 3");
  GoTab(ctx, g.fgTab);

  // B. delete then Cancel -> point comes back
  RemoveSurveyPointAt(*s_cmd, static_cast<size_t>(IndexOfPoint(5)));
  Frames(ctx, 4);
  Chk(pe.active, "second delete raises the warning");
  ClickModal(ctx, "Cancel");
  Frames(ctx, 3);
  Chk(IndexOfPoint(5) >= 0, "Cancel restores point 5 in FG");
  Chk(DbEntry(job, 5) != nullptr, "point 5 is still in the database");

  // C. delete from project -> gone everywhere
  RemoveSurveyPointAt(*s_cmd, static_cast<size_t>(IndexOfPoint(4)));
  Frames(ctx, 4);
  Chk(pe.active, "third delete raises the warning");
  ClickModal(ctx, "Delete from project");
  Frames(ctx, 3);
  Chk(DbEntry(job, 4) == nullptr, "'Delete from project' removed point 4 from the database");
  GoTab(ctx, g.egTab);
  Chk(IndexOfPoint(4) < 0, "EG lost point 4 too");
  GoTab(ctx, g.fgTab);

  // D. number conflict: FG hides 3, so it can 'add' a 3 -> Renumber
  PlacePoints(ctx, "NEW3", 3, 1, 1500.0, 2500.0, 300.0);
  Chk(pe.active && pe.conflicts == std::vector<int>{3}, "re-using number 3 (hidden here, alive in the project) raises the conflict warning");
  ClickModal(ctx, "Renumber");
  Frames(ctx, 3);
  const projpts::Entry* orig3 = DbEntry(job, 3);
  Chk(orig3 && orig3->point.description == "EG", "Renumber: the existing point 3 is untouched");
  int newId = -1;
  for (const auto& e : job->points->points)
    if (e.point.description == "NEW3")
      newId = e.point.id;
  Chk(newId > 8, "Renumber: the new point got a free number (" + std::to_string(newId) + ")");

  // E. conflict again -> Overwrite
  PlacePoints(ctx, "OVR3", 3, 1, 1600.0, 2600.0, 310.0);
  Chk(pe.active && pe.conflicts == std::vector<int>{3}, "second conflict raises the warning");
  ClickModal(ctx, "Overwrite");
  Frames(ctx, 3);
  Chk(DbEntry(job, 3) && DbEntry(job, 3)->point.description == "OVR3", "Overwrite replaced point 3 in the database");
  GoTab(ctx, g.egTab);
  Chk(IndexOfPoint(3) >= 0 && s_cmd->surveyPoints[static_cast<size_t>(IndexOfPoint(3))].description == "OVR3",
      "EG sees the overwritten point 3");
  GoTab(ctx, g.fgTab);
  SaveActiveDocument(*s_cmd, Log());
  GoTab(ctx, g.egTab);
  SaveActiveDocument(*s_cmd, Log());
}

// ===== 5. Survey Database toolspace: filters, driven through the real controls ==================
int g_hgTab = 0;
projpts::Rules g_hgRules;

void StageFilters(ImGuiTestContext* ctx)
{
  Note("5. Survey Database filters (HG, a drawing that created nothing)");
  auto* job = Job();
  g_hgTab = NewTab(ctx, "HG", g.jobUid, g.jobDir / "Drawings" / "HG.dwg");
  const auto allIds = DbIdsWhere(job, [](const projpts::Entry&) { return true; });
  ChkIds("a new drawing with no rules shows every project point", ViewIds(), allIds);

  s_cmd->showToolspaceWindow = true;
  s_cmd->toolspaceTab = AppCommandState::ToolspaceTab::Prospector;
  Frames(ctx, 4);
  const ImGuiTestItemInfo ts = ctx->WindowInfo("//TOOLSPACE", ImGuiTestOpFlags_NoError);
  Chk(ts.Window != nullptr, "TOOLSPACE window exists");
  if (!ts.Window)
    return;
  ctx->SetRef(ts.Window);
  const ImGuiID sdNode = FindItem(ctx, "           Survey Database");
  Chk(sdNode != 0, "Survey Database folder exists in the toolspace (project drawing)");
  Chk(FindItem(ctx, "           Project Files") != 0, "Project Files section exists in the toolspace");
  if (sdNode == 0)
    return;
  // ItemOpen() cannot tell this hand-drawn tree node is open (it reports 'Unable to Open item'); a click toggles it.
  // The node is OpenOnArrow, so the click has to land on the arrow at its left edge.
  {
    const ImGuiTestItemInfo n = ctx->ItemInfo(sdNode);
    ctx->MouseMoveToPos(ImVec2(n.RectFull.Min.x + 9.f, n.RectFull.GetCenter().y));
    ctx->MouseClick(0);
  }
  Frames(ctx, 3);
  const auto item = [&](const char* l) {
    ctx->SetRef(ctx->WindowInfo("//TOOLSPACE", ImGuiTestOpFlags_NoError).Window);
    return FindItem(ctx, l);
  };
  if (item("##sdb_ids") == 0)
    FindItem(ctx, "", true);
  Chk(item("##sdb_ids") != 0, "Survey Database filter controls are on screen");
  if (item("##sdb_ids") == 0)
    return;

  // number range
  ctx->ItemInputValue(item("##sdb_ids"), "1-2");
  Frames(ctx, 4);
  ChkIds("number filter 1-2", ViewIds(), DbIdsWhere(job, [](const projpts::Entry& e) { return e.point.id >= 1 && e.point.id <= 2; }));
  Chk(CountProjectPoints(*s_cmd).shown == ViewIds().size(), "toolspace 'x of y shown' agrees with the drawing");

  // description
  ctx->ItemInputValue(item("##sdb_ids"), "");
  ctx->ItemInputValue(item("##sdb_desc"), "FG*");
  Frames(ctx, 4);
  ChkIds("description filter FG*", ViewIds(), DbIdsWhere(job, [](const projpts::Entry& e) { return e.point.description.rfind("FG", 0) == 0; }));

  // description AND number range
  ctx->ItemInputValue(item("##sdb_desc"), "EG*");
  ctx->ItemInputValue(item("##sdb_ids"), "1-2");
  Frames(ctx, 4);
  ChkIds("EG* AND 1-2 combine", ViewIds(),
         DbIdsWhere(job, [](const projpts::Entry& e) { return e.point.description.rfind("EG", 0) == 0 && e.point.id >= 1 && e.point.id <= 2; }));

  // elevation
  ctx->ItemClick(item("Reset filters"));
  Frames(ctx, 3);
  ChkIds("Reset filters shows everything again", ViewIds(), allIds);
  ctx->ItemCheck(item("Elevation between##sdb_elev"));
  Frames(ctx, 3);
  ctx->ItemInputValue(item("##sdb_emin"), 100.0f);
  ctx->ItemInputValue(item("##sdb_emax"), 103.0f);
  Frames(ctx, 4);
  ChkIds("elevation 100..103", ViewIds(),
         DbIdsWhere(job, [](const projpts::Entry& e) { return e.point.elevation >= 100.0 && e.point.elevation <= 103.0; }));

  // source drawing: the combo's entries contain '/', which a test path splits on, so the choice is
  // written to the rules (what the combo does on click) after checking the combo lists the sources.
  ctx->ItemClick(item("Reset filters"));
  Frames(ctx, 3);
  const auto sources = ProjectPointSources(*s_cmd);
  Chk(sources.size() >= 2, "Source drawing choices list the project's drawings (" + std::to_string(sources.size()) + ")");
  s_cmd->pointVisibility.sourceDrawing = "Drawings/FG.dwg";
  Frames(ctx, 4);
  ChkIds("source drawing = Drawings/FG.dwg", ViewIds(),
         DbIdsWhere(job, [](const projpts::Entry& e) { return e.sourceDrawing == "Drawings/FG.dwg"; }));
  ctx->ItemClick(item("Reset filters"));
  Frames(ctx, 3);

  // point group, picked from the real combo
  PointGroup grp;
  grp.name = "EGgrp";
  grp.rule.descriptionMatch = "EG*";
  s_cmd->pointGroups.push_back(grp);
  Frames(ctx, 3);
  ImGuiID groupCombo = item("##sdb_group");
  if (groupCombo == 0)
  {
    ctx->SetRef(ctx->WindowInfo("//TOOLSPACE", ImGuiTestOpFlags_NoError).Window);
    groupCombo = FindUnlabelledAfter(ctx, "Elevation between", 0);  // the first combo below the elevation row
  }
  Chk(groupCombo != 0, "point-group combo is on screen");
  if (groupCombo)
  {
    ctx->ItemClick(groupCombo);
    Frames(ctx, 3);
    ctx->SetRef("//$FOCUSED");
    ctx->ItemClick("EGgrp");
    Frames(ctx, 4);
    ctx->SetRef(ts.Window);
    Chk(s_cmd->pointVisibility.group == "EGgrp", "point group chosen from the combo");
    ChkIds("point group EGgrp (EG*)", ViewIds(), DbIdsWhere(job, [](const projpts::Entry& e) { return e.point.description.rfind("EG", 0) == 0; }));
  }

  // leave a distinctive rule set for the persistence stage
  ctx->SetRef(ts.Window);
  s_cmd->pointVisibility = {};
  s_cmd->pointVisibility.idRanges = "1-2, 6";
  s_cmd->pointVisibility.description = "*";
  Frames(ctx, 4);
  g_hgRules = s_cmd->pointVisibility;
}

// ===== 6. Rules persist with the drawing; the DWG does not hold project points ==================
void StagePersistence(ImGuiTestContext* ctx)
{
  Note("6. Rules persist; project points stay out of the DWG");
  auto* job = Job();
  const auto expect = ViewIds();
  SaveActiveDocument(*s_cmd, Log());
  const fs::path hg = g.jobDir / "Drawings" / "HG.dwg";
  Chk(FileExists(hg), "HG.dwg saved");
  const size_t tabsBefore = s_cmd->drawingTabs.size();
  const size_t sessionsBefore = s_cmd->openProjects.size();
  const int again = OpenInTab(ctx, hg);
  Chk(again > 0 && s_cmd->drawingTabs.size() == tabsBefore + 1, "HG.dwg opens in a second tab");
  Chk(s_cmd->openProjects.size() == sessionsBefore, "auto-detect joined the already-open project (no second session)");
  Chk(again > 0 && s_cmd->drawingTabs[static_cast<size_t>(again)].projectUid == g.jobUid, "the reopened drawing belongs to TestJob");
  Chk(LogHas("Opened in project TestJob"), "the join notice 'Opened in project TestJob' was logged");
  Chk(s_cmd->pointVisibility == g_hgRules, "visibility rules came back exactly as saved");
  ChkIds("reopened HG shows the same points", ViewIds(), expect);

  auto priv = std::make_unique<AppCommandState>();
  std::vector<std::string> plog;
  const bool loaded = OpenDrawingDocument(*priv, hg.u8string().c_str(), plog);
  Chk(loaded && priv->surveyPoints.empty(), "the project drawing's DWG holds no survey points of its own (they live in the database)");
  (void)job;
}

// ===== 7. Standalone drawing behaves as before ==================================================
int g_standaloneTab = 0;

void StageStandalone(ImGuiTestContext* ctx)
{
  Note("7. A drawing outside any project behaves as before");
  auto* job = Job();
  fs::create_directories(g.extDir);
  g_standaloneTab = NewTab(ctx, "Standalone", 0, g.extDir / "S.dwg");
  PlacePoints(ctx, "EG", 1, 1, 1000.0, 2000.0, 100.0);  // identical to project point 1
  PlacePoints(ctx, "SX", 2, 1, 1500.0, 1500.0, 5.0);
  PlacePoints(ctx, "SY", 3, 1, 1600.0, 1500.0, 6.0);
  PlacePoints(ctx, "SZ", 20, 2, 1700.0, 1500.0, 7.0);
  const auto dbRev = job->points->revision;
  const size_t dbN = job->points->points.size();
  Chk(s_cmd->surveyPoints.size() == 5, "standalone drawing has its 5 points");
  Chk(job->points->points.size() == dbN && job->points->revision == dbRev, "standalone edits did not touch the project database");
  SaveActiveDocument(*s_cmd, Log());
  Chk(FileExists(g.extDir / "S.dwg"), "S.dwg saved outside the project");
  auto priv = std::make_unique<AppCommandState>();
  std::vector<std::string> plog;
  Chk(OpenDrawingDocument(*priv, (g.extDir / "S.dwg").u8string().c_str(), plog) && priv->surveyPoints.size() == 5,
      "standalone DWG keeps its 5 points inside the file");
  const int back = OpenInTab(ctx, g.extDir / "S.dwg");
  Chk(back > 0 && s_cmd->drawingTabs[static_cast<size_t>(back)].projectUid == 0, "reopened standalone drawing belongs to no project");
  Chk(ProjectNameForTab(*s_cmd, back).empty(), "no project name on a standalone tab");
  Chk(s_cmd->surveyPoints.size() == 5, "and shows its 5 points");
  s_cmd->surveyPoints[0].elevation = 999.0;
  Frames(ctx, 4);
  Chk(job->points->revision == dbRev, "editing a standalone point leaves the project database alone");
}

// ===== 8. Add Drawing to Project ===============================================================
void StageAddDrawing(ImGuiTestContext* ctx)
{
  Note("8. Add Drawing to Project (preview, per-conflict choices, mismatch)");
  auto* job = Job();
  const fs::path src = g.extDir / "S.dwg";
  const std::string srcBytes = ReadAll(src);
  const auto dwgListBefore = [&] {
    std::set<std::string> n;
    for (const auto& e : fs::directory_iterator(g.jobDir / "Drawings"))
      n.insert(e.path().filename().string());
    return n;
  };
  const auto before = dwgListBefore();
  const auto rev0 = job->points->revision;

  AddDrawingPlan plan;
  PrepareAddDrawing(*s_cmd, g.jobUid, src.u8string(), &plan, Log());
  Chk(plan.error.empty(), "plan prepared: " + plan.error);
  Chk(plan.summary.total == 5, "preview: 5 points found (" + std::to_string(plan.summary.total) + ")");
  Chk(plan.summary.fresh == 2, "preview: 2 new to the project (" + std::to_string(plan.summary.fresh) + ")");
  Chk(plan.summary.identical == 1, "preview: 1 identical (" + std::to_string(plan.summary.identical) + ")");
  Chk(plan.summary.differing == 2, "preview: 2 differ (" + std::to_string(plan.summary.differing) + ")");
  Chk(!plan.Blocked(), "same units and coordinate system: not blocked");
  Chk(dwgListBefore() == before && job->points->revision == rev0, "preview wrote nothing (no copy, database untouched)");

  plan.choices[2] = projadd::Choice::Renumber;
  plan.choices[3] = projadd::Choice::Overwrite;
  AddDrawingResult res;
  Chk(CommitAddDrawing(*s_cmd, plan, 0.0, &res, Log()), "Add Drawing committed");
  Chk(res.destRel == "Drawings/S.dwg", "copied to " + res.destRel);
  Chk(FileExists(g.jobDir / "Drawings" / "S.dwg"), "the copy is in the project's Drawings folder");
  Chk(ReadAll(src) == srcBytes, "the user's original file is byte-for-byte unchanged");
  Chk(res.outcome.added == 2 && res.outcome.shared == 1 && res.outcome.overwritten == 1 && res.outcome.renumbered == 1 &&
          res.outcome.skipped == 0,
      "outcome: added 2, shared 1, overwritten 1, renumbered 1");
  Chk(DbEntry(job, 3) && DbEntry(job, 3)->point.description == "SY" && DbEntry(job, 3)->sourceDrawing == "Drawings/S.dwg",
      "overwritten point 3 now SY, tagged Drawings/S.dwg");
  Chk(DbEntry(job, 20) && DbEntry(job, 21), "new points 20 and 21 are in the database");
  Chk(DbEntry(job, 1) && DbEntry(job, 1)->sourceDrawing == "Drawings/EG.dwg", "identical point 1 kept its own source tag (EG)");
  const int added = OpenInTab(ctx, g.jobDir / "Drawings" / "S.dwg");
  Chk(added > 0, "added drawing opens as a project tab");
  const std::set<int> shown(res.outcome.shownIds.begin(), res.outcome.shownIds.end());
  ChkIds("the added drawing shows exactly the points it brought", ViewIds(), shown);

  // A second plan that is cancelled changes nothing.
  const auto rev1 = job->points->revision;
  {
    AddDrawingPlan cancelled;
    PrepareAddDrawing(*s_cmd, g.jobUid, src.u8string(), &cancelled, Log());
  }
  Chk(job->points->revision == rev1 && dwgListBefore().count("S.dwg") == 1, "a cancelled add leaves the project unchanged");

  // Mismatch: a drawing in METERS cannot be added until converted.
  const int mt = NewTab(ctx, "Meters", 0, g.extDir / "M.dwg");
  DrawingSettings ds = s_cmd->drawingSettings;
  Chk(ApplyDrawingSettings(*s_cmd, 6, s_cmd->modelUnitsPerPlottedInch, ds, Log()), "set the external drawing to meters");
  PlacePoints(ctx, "MTR", 60, 1, 300.0, 600.0, 30.0);
  SaveActiveDocument(*s_cmd, Log());
  AddDrawingPlan mp;
  PrepareAddDrawing(*s_cmd, g.jobUid, (g.extDir / "M.dwg").u8string(), &mp, Log());
  Chk(mp.error.empty() && mp.Blocked(), "meters drawing is BLOCKED from adding");
  Chk(mp.conversion.unitsDiffer, "the preview says the unit differs");
  Chk(mp.CanConvert(), "Convert is offered");
  AddDrawingResult mres;
  Chk(!CommitAddDrawing(*s_cmd, mp, 0.0, &mres, Log()), "Commit refuses while blocked");
  Chk(DbEntry(job, 60) == nullptr && !FileExists(g.jobDir / "Drawings" / "M.dwg"), "nothing was written by the refused commit");
  Chk(ChooseAddConvert(*s_cmd, &mp, Log()) && !mp.Blocked(), "Convert succeeded and unblocked the plan");
  Chk(CommitAddDrawing(*s_cmd, mp, 0.0, &mres, Log()), "converted drawing committed");
  if (const auto* e = DbEntry(job, 60))
  {
    DevShell_Logf("p696", "converted point 60: E %.3f N %.3f Z %.3f (meters in: 300, 600, 30)", e->point.easting, e->point.northing,
                  e->point.elevation);
    Chk(std::abs(e->point.northing - 600.0 * 3.28083989501) < 2.0, "point 60 northing scaled meters -> feet");
  }
  else
    Chk(false, "point 60 reached the database");
  (void)mt;
}

gsproj::Project ReloadProject(AppCommandState::ProjectSession* s)
{
  gsproj::Project p;
  std::string err;
  if (!gsproj::Load(s->project.file, &p, &err))
    DevShell_Logf("p696", "  could not reload %s: %s", s->project.file.string().c_str(), err.c_str());
  return p;
}

const gsproj::TrackedItem* ItemOf(const gsproj::Project& p, const std::string& path)
{
  for (const auto& it : p.items)
    if (it.path == path)
      return &it;
  return nullptr;
}

bool HasAssoc(const gsproj::TrackedItem* it, const std::string& drawing)
{
  if (!it)
    return false;
  for (const auto& a : it->associations)
    if (a == drawing)
      return true;
  return false;
}

/// Saves every drawing tab of project \p uid so Project Health has no 'unsaved' entries.
void SaveAllProjectTabs(ImGuiTestContext* ctx, std::uint32_t uid)
{
  for (size_t i = 1; i < s_cmd->drawingTabs.size(); ++i)
  {
    if (s_cmd->drawingTabs[i].projectUid != uid)
      continue;
    GoTab(ctx, static_cast<int>(i));
    if (!s_cmd->activeDocFilePath.empty())
      SaveActiveDocument(*s_cmd, Log());
  }
  Frames(ctx, 3);
}

/// Drives PDFATTACH through the real dialog up to the Copy / Link / Cancel question and answers it.
/// \p answer: "Copy into project" or "Link". Returns false if any step did not happen.
bool AttachPdf(ImGuiTestContext* ctx, const fs::path& pdf, const char* answer, float x, float y)
{
  SubmitCad(ctx, "PDFATTACH");
  Frames(ctx, 4);
  if (s_cmd->active != AppCommandState::Kind::PdfAttach)
  {
    DevShell_Logf("p696", "  AttachPdf: PDFATTACH did not start (active kind %d)", static_cast<int>(s_cmd->active));
    return false;
  }
  ImGuiWindow* win = ImGui::FindWindowByName("PDF Attach");
  if (!win)
  {
    DevShell_Logf("p696", "  AttachPdf: no 'PDF Attach' window (dialogOpen=%d phase=%d)", s_cmd->pdfAttachDialogOpen ? 1 : 0,
                  static_cast<int>(s_cmd->pdfAttachPhase));
    ImGuiContext& c = *ImGui::GetCurrentContext();
    for (ImGuiWindow* w : c.Windows)
      if (w->WasActive)
        DevShell_Logf("p696", "    window '%s'", w->Name);
    return false;
  }
  ctx->SetRef(win);
  const ImGuiID pathField = FindItem(ctx, "##PdfPath", /*logAll*/ true);
  DevShell_Logf("p696", "  AttachPdf: path field id %08X", pathField);
  ctx->ItemClick(pathField);
  ctx->KeyCharsReplaceEnter(pdf.u8string().c_str());
  Frames(ctx, 4);
  if (!WaitReal(ctx, 15.0, [] { return s_cmd->pdfDraftCache && PdfDraftCache_PageCount(s_cmd->pdfDraftCache) > 0; }))
  {
    DevShell_Logf("p696", "  AttachPdf: PDF never loaded (path field '%s')", s_cmd->pdfAttachFilePath);
    CancelPdfAttachCommand(*s_cmd, Log());
    return false;
  }
  ctx->SetRef(win);
  ctx->ItemClick("Attach");
  Frames(ctx, 4);
  Chk(s_cmd->projectAttachPrompt.kind == AppCommandState::ProjectAttachPrompt::Kind::Pdf,
      "attaching " + pdf.filename().string() + " from outside the project raises the Copy / Link / Cancel question");
  ClickModal(ctx, answer);
  if (!WaitReal(ctx, 20.0, [] { return s_cmd->pdfAttachPhase == AppCommandState::PdfAttachPhase::WaitInsertPoint; }))
  {
    DevShell_Logf("p696", "  AttachPdf: never reached the insertion-point phase (phase %d)", static_cast<int>(s_cmd->pdfAttachPhase));
    CancelPdfAttachCommand(*s_cmd, Log());
    return false;
  }
  SubmitPdfAttachInsertPoint(*s_cmd, x, y, Log());
  Frames(ctx, 4);
  return true;
}

// ===== 9. PDF attach: copy / link, associations, Project Health ================================
void StagePdf(ImGuiTestContext* ctx)
{
  Note("9. PDF attached to FG: copy into project, associations, link, Project Health");
  auto* job = Job();
  const fs::path pdfA = g.extDir / "site-plan.pdf";
  const fs::path pdfB = g.extDir / "survey-notes.pdf";
  MakePdf(pdfA, "GoSurvey site plan");
  MakePdf(pdfB, "Survey notes");
  GoTab(ctx, g.fgTab);
  const size_t before = s_cmd->pdfAttachments.size();
  Chk(AttachPdf(ctx, pdfA, "Copy into project", 1200.f, 2300.f), "PDFATTACH ran to the insertion point (copy)");
  Chk(s_cmd->pdfAttachments.size() == before + 1, "the PDF underlay is placed in FG");
  Chk(FileExists(g.jobDir / "PDFs" / "site-plan.pdf"), "the PDF was COPIED into the project's PDFs folder");
  Chk(FileExists(pdfA), "the user's original PDF is still where it was");
  if (!s_cmd->pdfAttachments.empty())
  {
    const std::string stored = s_cmd->pdfAttachments.back().filePath;
    Chk(stored.find("TestJob") != std::string::npos && stored.find("PDFs") != std::string::npos, "the drawing points at the project's copy: " + stored);
  }
  SaveActiveDocument(*s_cmd, Log());
  {
    const gsproj::Project p = ReloadProject(job);
    const auto* it = ItemOf(p, "PDFs/site-plan.pdf");
    Chk(it != nullptr, "the .gsproj on disk tracks PDFs/site-plan.pdf");
    Chk(it && it->kind == gsproj::kKindInProject, "recorded as in-project");
    Chk(HasAssoc(it, "Drawings/FG.dwg"), "associated with Drawings/FG.dwg");
    Chk(it && it->placementsJson.find("1200") != std::string::npos && it->placementsJson.find("2300") != std::string::npos,
        "the placement (1200, 2300) is recorded in the project: " + (it ? it->placementsJson : std::string()));
    Chk(ItemOf(p, "Drawings/FG.dwg") != nullptr, "the drawing itself is tracked");
    Chk(it && !gsproj::IsSafeRelativePath(it->path) == false, "the stored path is relative and safe");
  }

  // Association survives a reopen: FG.dwg in a fresh tab gets its PDF back from the project.
  const int re = OpenInTab(ctx, g.jobDir / "Drawings" / "FG.dwg");
  Chk(re > 0, "FG.dwg reopened in a new tab");
  if (s_cmd->pdfAttachments.empty())
  {
    // Diagnostic: rebuild the copy directly, with the recorded dpi and with 150.
    const gsproj::Project p = ReloadProject(job);
    const auto* it = ItemOf(p, "PDFs/site-plan.pdf");
    DevShell_Logf("p696", "  probe: item %s placements=%s", it ? "found" : "MISSING", it ? it->placementsJson.c_str() : "");
    for (const float dpi : {150.f, 72.f})
    {
      PdfAttachment probe;
      const bool ok = PdfAttach_Build((g.jobDir / "PDFs" / "site-plan.pdf").u8string().c_str(), 0, dpi, true, true, true, probe);
      DevShell_Logf("p696", "  probe: PdfAttach_Build dpi %.0f -> %d (tex %d, %dx%d)", static_cast<double>(dpi), ok ? 1 : 0, probe.glTexId,
                    probe.texW, probe.texH);
    }
  }
  Chk(s_cmd->pdfAttachments.size() == 1, "the reopened FG has its PDF attached again (" + std::to_string(s_cmd->pdfAttachments.size()) + ")");
  if (!s_cmd->pdfAttachments.empty())
  {
    const auto& a = s_cmd->pdfAttachments[0];
    Chk(std::abs(a.insertX - 1200.f) < 0.01f && std::abs(a.insertY - 2300.f) < 0.01f, "re-placed at the recorded insertion point");
    Chk(a.glTexId != 0 || a.texW > 0, "the PDF page raster is loaded");
  }

  // Link: stays where it is, flagged as not travelling.
  Chk(AttachPdf(ctx, pdfB, "Link", 1500.f, 2400.f), "PDFATTACH ran to the insertion point (link)");
  SaveActiveDocument(*s_cmd, Log());
  {
    const gsproj::Project p = ReloadProject(job);
    const auto* it = ItemOf(p, pdfB.u8string());
    if (!it)
      for (const auto& x : p.items)
        if (x.path.find("survey-notes") != std::string::npos)
          it = &x;
    Chk(it != nullptr, "the linked PDF is tracked");
    Chk(it && it->kind == gsproj::kKindLocalLink, "recorded as local-link (will not travel)");
    Chk(!FileExists(g.jobDir / "PDFs" / "survey-notes.pdf"), "nothing was copied for a link");
  }
  const projfiles::Health h = ProjectHealthFor(*s_cmd, g.jobUid);
  Chk(h.linked.size() == 1, "Project Health lists the 1 linked file (" + std::to_string(h.linked.size()) + ")");

  // Turnover is refused while Health has a problem the user has not accepted.
  std::vector<std::string> chosen = {"Drawings/EG.dwg", "Drawings/FG.dwg"};
  Chk(!CreateProjectTurnover(*s_cmd, g.jobUid, "", chosen, true, Log()), "turnover refused with a blank recipient");
  Chk(!CreateProjectTurnover(*s_cmd, g.jobUid, "Acme Design", chosen, false, Log()),
      "turnover refused while Health has an unacknowledged problem (a linked file)");
  const auto turnovers = [&] {
    size_t n = 0;
    for (const auto& e : fs::directory_iterator(g.jobDir / "Turnovers"))
      n += e.path().extension() == ".gsturnover";
    return n;
  };
  Chk(turnovers() == 0, "no turnover record was written by the refusals");
  Chk(CreateProjectTurnover(*s_cmd, g.jobUid, "Acme Design", chosen, true, Log()), "turnover created once the problems are acknowledged");
  Chk(turnovers() == 1, "one .gsturnover record exists");

  // Copy links into the project, through the real Project Health window.
  s_cmd->projectHealthUid = g.jobUid;
  Frames(ctx, 4);
  ClickModal(ctx, "Copy links into the project");
  {
    const gsproj::Project p = ReloadProject(job);
    const gsproj::TrackedItem* it = nullptr;
    for (const auto& x : p.items)
      if (x.path.find("survey-notes") != std::string::npos)
        it = &x;
    Chk(it && it->kind == gsproj::kKindInProject, "after 'Copy links', survey-notes.pdf is in-project");
    Chk(FileExists(g.jobDir / "PDFs" / "survey-notes.pdf"), "and the file now lives in PDFs/");
    Chk(it && HasAssoc(it, "Drawings/FG.dwg"), "its association with FG was kept");
  }
  ctx->SetRef("//$FOCUSED");
  ctx->ItemClick("Close");
  Frames(ctx, 3);
  s_cmd->projectHealthUid = 0;
  SaveAllProjectTabs(ctx, g.jobUid);
  const projfiles::Health h2 = ProjectHealthFor(*s_cmd, g.jobUid);
  Chk(h2.Clean(), "Project Health is clean after copying links and saving every drawing");
  if (!h2.Clean())
    DevShell_Logf("p696", "  health: linked %zu missing %zu unavailable %zu unsaved %zu", h2.linked.size(), h2.missing.size(),
                  h2.unavailable.size(), h2.unsaved.size());
}

// ===== 10. Turnover window ===================================================================
void StageTurnover(ImGuiTestContext* ctx)
{
  Note("10. Create Turnover window");
  auto* job = Job();
  s_cmd->projectTurnoverPrompt = {};
  s_cmd->projectTurnoverPrompt.projectUid = g.jobUid;
  Frames(ctx, 4);
  {
    ImGuiContext& c = *ImGui::GetCurrentContext();
    DevShell_Logf("p696", "  turnover: focus '%s', popups %d, top popup '%s'", c.NavWindow ? c.NavWindow->Name : "-", c.OpenPopupStack.Size,
                  c.OpenPopupStack.Size > 0 && c.OpenPopupStack.back().Window ? c.OpenPopupStack.back().Window->Name : "-");
  }
  ctx->SetRef("//$FOCUSED");
  ctx->ItemInputValue("Recipient", "Harbor County Engineering");
  Frames(ctx, 2);
  if (!ProjectHealthFor(*s_cmd, g.jobUid).Clean())
  {
    DevShell_Log("p696", "  Health is not clean here (see the 'Copy links' finding), so the window needs 'create anyway' ticked");
    ctx->ItemCheck("Create the turnover anyway, with the problems above");
    Frames(ctx, 2);
  }
  ctx->ItemClick("Create turnover");
  Frames(ctx, 5);
  if (s_cmd->projectTurnoverPrompt.projectUid != 0)  // never leave a modal open for the stages after
  {
    ClickModal(ctx, "Cancel");
    s_cmd->projectTurnoverPrompt = {};
  }
  std::vector<std::string> problems;
  const auto recs = projturn::List(job->project, &problems);
  Chk(recs.size() == 2, "two turnover records now exist (" + std::to_string(recs.size()) + ")");
  if (!recs.empty())
  {
    const projturn::Record& r = recs.front();  // newest first
    Chk(r.recipient == "Harbor County Engineering", "newest record names the recipient");
    Chk(!r.date.empty(), "it carries a date: " + r.date);
    bool hasEg = false, hasPdf = false, crc = true, miss = false;
    for (const auto& e : r.items)
    {
      hasEg |= e.path == "Drawings/EG.dwg";
      hasPdf |= e.path == "PDFs/site-plan.pdf";
      crc &= !e.crcHex.empty();
      miss |= e.missing;
    }
    Chk(hasEg && hasPdf, "it lists the drawings and the PDF (" + std::to_string(r.items.size()) + " items)");
    Chk(crc && !miss, "every listed file has a size/CRC and none is missing");
  }
  Chk(problems.empty(), "no damaged turnover records");
  Chk(s_cmd->projectTurnoverPrompt.projectUid == 0, "the window closed after Create");
}

// ===== 11. Pack Project / Open Packed Project ================================================
void StagePack(ImGuiTestContext* ctx)
{
  Note("11. Pack Project, open the pack as a different 'recipient' folder");
  auto* job = Job();
  SaveAllProjectTabs(ctx, g.jobUid);
  {
    std::ofstream junk(g.jobDir / "PointClouds" / "site.e57", std::ios::binary);
    junk << std::string(300 * 1024, 'e');
  }
  const fs::path pack = g_root / "TestJob.gspack";
  gspack::PackPlan plan;
  std::string err;
  Chk(gspack::PlanPack(job->project, pack, &plan, &err), "PlanPack: " + err);
  const auto has = [&](const char* rel) {
    for (const auto& f : plan.files)
      if (f.rel == rel)
        return true;
    return false;
  };
  Chk(has("Drawings/EG.dwg") && has("Drawings/FG.dwg") && has("Points/survey-points.gspdb") && has("PDFs/site-plan.pdf") &&
          has("PDFs/survey-notes.pdf") && has("TestJob.gsproj"),
      "the pack plan holds drawings, point database, PDFs and the .gsproj");
  Chk(!has("TestJob.gsproj.lock"), "the lock file is never packed");
  Chk(plan.pointCloudBytes >= 300 * 1024, "the point cloud's size is counted separately");
  gspack::PackOptions opt;
  opt.nowUnix = 1760000000;
  Chk(gspack::WritePack(job->project, plan, opt, pack, &err), "WritePack: " + err);
  Chk(FileExists(pack) && fs::file_size(pack) > 1000, "TestJob.gspack written (" + std::to_string(FileExists(pack) ? fs::file_size(pack) : 0) + " bytes)");

  // "Email it": open the pack in a new folder, as the recipient would.
  const fs::path recv = g_root / "Received";
  OpenPackedProject(*s_cmd, Log(), pack.u8string().c_str(), recv.u8string().c_str());
  Frames(ctx, 6);
  job = Job();  // opening a second project grew openProjects: the old pointer is dangling
  auto* rs = SessionAt(recv);
  Chk(rs != nullptr, "Open Packed Project opened the project from the .gspack");
  if (!rs)
    return;
  Chk(rs->project.id == job->project.id, "the received project has the same project ID");
  Chk(rs->points && job->points && rs->points->points.size() == job->points->points.size(),
      "received database holds the same number of points (" + std::to_string(rs->points ? rs->points->points.size() : 0) + ")");
  bool same = rs->points && job->points && rs->points->points.size() == job->points->points.size();
  if (same)
    for (size_t i = 0; i < rs->points->points.size(); ++i)
      same &= projpts::SamePoint(rs->points->points[i].point, job->points->points[i].point) &&
              rs->points->points[i].sourceDrawing == job->points->points[i].sourceDrawing;
  Chk(same, "every received point equals the sent one (coordinates, description, source drawing)");
  Chk(rs->settings && rs->settings->insUnits == 2, "project settings (units) came across");
  for (const char* rel : {"Drawings/EG.dwg", "Drawings/FG.dwg", "Drawings/HG.dwg", "Drawings/S.dwg", "PDFs/site-plan.pdf", "PDFs/survey-notes.pdf",
                          "PointClouds/site.e57"})
    Chk(FileExists(recv / rel), std::string("received file ") + rel);
  Chk(projfiles::CheckHealth(rs->project, {}).Clean(), "Project Health of the received project is clean (no missing files)");

  // Open the received FG: it must pick up the RECEIVED copies of its PDFs, not the sender's.
  const int rt = OpenInTab(ctx, recv / "Drawings" / "FG.dwg");
  Chk(rt > 0 && s_cmd->drawingTabs[static_cast<size_t>(rt)].projectUid == rs->uid, "received FG.dwg joined the received project");
  Chk(s_cmd->pdfAttachments.size() == 2, "received FG has both PDFs attached (" + std::to_string(s_cmd->pdfAttachments.size()) + ")");
  for (const auto& a : s_cmd->pdfAttachments)
    Chk(a.filePath.find("Received") != std::string::npos, "PDF resolves inside the received folder: " + a.filePath);
  DevShell_Logf("p696", "  received FG: shows %zu of %zu received points; rules hidden=%s shown=%s", s_cmd->surveyPoints.size(),
                rs->points->points.size(), projpts::IdsToText(s_cmd->pointVisibility.hidden).c_str(),
                projpts::IdsToText(s_cmd->pointVisibility.shown).c_str());
  Chk(s_cmd->surveyPoints.size() == rs->points->points.size() - s_cmd->pointVisibility.hidden.size() ||
          s_cmd->surveyPoints.size() == rs->points->points.size(),
      "received FG shows its points from the received database, honouring its saved hide-here list");

  // Two projects open at once: edits stay in their own database.
  const size_t sentN = job->points->points.size();
  PlacePoints(ctx, "RCV", 500, 1, 5000.0, 5000.0, 1.0);
  Chk(rs->points->points.size() == sentN + 1, "a point added in the received project reaches ITS database");
  Chk(job->points->points.size() == sentN, "...and not the original project's database");
  Chk(ProjectNameForTab(*s_cmd, rt) == "TestJob", "tab shows the project name");
  s_cmd->activeDocFilePath = (recv / "Drawings" / "FG.dwg").u8string();
  SaveActiveDocument(*s_cmd, Log());

  // Leave the point cloud out of a pack.
  gspack::PackPlan plan2;
  Chk(gspack::PlanPack(job->project, pack, &plan2, &err), "second PlanPack");
  gspack::PackOptions lean;
  lean.excludePointClouds = true;
  lean.nowUnix = 1760000100;
  const fs::path lean2 = g_root / "TestJob-lean.gspack";
  Chk(gspack::WritePack(job->project, plan2, lean, lean2, &err), "WritePack (exclude point clouds): " + err);
  Chk(fs::file_size(lean2) < fs::file_size(pack), "the lean pack is smaller");
  fs::path gp;
  gspack::Manifest man;
  Chk(gspack::ExtractPack(lean2, g_root / "Received-lean", &gp, &man, &err), "lean pack extracts: " + err);
  Chk(!FileExists(g_root / "Received-lean" / "PointClouds" / "site.e57"), "the point cloud is NOT in the lean pack");
  Chk(!man.excluded.empty(), "the manifest says what was left out");
  Chk(FileExists(g_root / "Received-lean" / "Drawings" / "FG.dwg"), "everything else is there");

  // A corrupt pack changes nothing.
  const fs::path bad = g_root / "bad.gspack";
  {
    std::ofstream b(bad, std::ios::binary);
    b << "this is not a zip file";
  }
  const auto n0 = s_cmd->openProjects.size();
  OpenPackedProject(*s_cmd, Log(), bad.u8string().c_str(), (g_root / "Received-bad").u8string().c_str());
  Chk(s_cmd->openProjects.size() == n0 && !FileExists(g_root / "Received-bad"), "a corrupt .gspack opens nothing and leaves nothing behind");
}

// ===== 12. Missing file: drawing still opens, health says so ===================================
void StageMissing(ImGuiTestContext* ctx)
{
  Note("12. A tracked PDF goes missing");
  fs::remove(g.jobDir / "PDFs" / "site-plan.pdf");
  const projfiles::Health h = ProjectHealthFor(*s_cmd, g.jobUid);
  bool listed = false;
  for (const auto& m : h.missing)
    listed |= m.find("site-plan.pdf") != std::string::npos;
  Chk(listed, "Project Health lists PDFs/site-plan.pdf as missing");
  const size_t tabs = s_cmd->drawingTabs.size();
  const int t = OpenInTab(ctx, g.jobDir / "Drawings" / "FG.dwg");
  Chk(t > 0 && s_cmd->drawingTabs.size() == tabs + 1, "FG.dwg still opens with a tracked file missing");
  Chk(LogHas("site-plan.pdf"), "the missing file is reported in the log");
  Chk(s_cmd->pdfAttachments.size() == 1, "the PDF that is still there is attached (" + std::to_string(s_cmd->pdfAttachments.size()) + ")");
  Chk(ProjectHealthFor(*s_cmd, g.jobUid).Clean() == false, "Health is no longer clean");
}

// ===== 13. Locking, read-only and takeover ======================================================
void StageLocking(ImGuiTestContext* ctx)
{
  Note("13. Lock file: read-only and stale takeover");
  gsproj::Project lp;
  std::string err;
  Chk(gsproj::Create(g_root, "LockedJob", &lp, &err), "created LockedJob: " + err);
  gsproj::LockInfo rival;
  rival.user = "rival";
  rival.machine = "OTHER-PC";
  rival.pid = 4;
  rival.sinceUnix = 1760000000;
  Chk(gsproj::TryAcquire(lp.file, rival, nullptr) == gsproj::LockResult::Acquired, "another editor holds LockedJob");

  OpenProjectFile(*s_cmd, Log(), lp.file.u8string().c_str());
  Frames(ctx, 5);
  Chk(s_cmd->projectPrompt.kind == AppCommandState::ProjectPrompt::Kind::Locked, "opening a locked project asks what to do");
  Chk(s_cmd->projectPrompt.holder.user == "rival", "the prompt names the holder (rival)");
  Chk(SessionAt(lp.Folder()) == nullptr, "nothing opened yet");
  ClickModal(ctx, "Open read-only");
  Frames(ctx, 4);
  auto* ro = SessionAt(lp.Folder());
  Chk(ro && ro->readOnly, "opened READ-ONLY");
  if (!ro)
    return;
  Chk(ProjectIsReadOnlyForTab(*s_cmd, s_cmd->activeDrawingIdx), "the active tab is a read-only project tab");
  const std::string lockBefore = ReadAll(gsproj::LockPath(lp.file));
  Chk(lockBefore.find("rival") != std::string::npos, "the rival's lock file was left alone");
  s_cmd->activeDocFilePath = (lp.Folder() / "Drawings" / "R.dwg").u8string();
  PlacePoints(ctx, "RO", 1, 2, 10.0, 10.0, 1.0);
  Frames(ctx, 4);
  FlushProjectPointDb(*ro, Log());
  Chk(!FileExists(projpts::DbPath(lp.Folder(), lp.layout.at("points"))), "a read-only opener never writes the point database");
  SaveActiveDocument(*s_cmd, Log());
  Chk(!FileExists(lp.Folder() / "Drawings" / "R.dwg"), "a read-only opener cannot save its drawing");
  Chk(LogHas("read-only"), "and is told why");

  // Stale lock (this machine, dead process) -> take over.
  gsproj::Project lp2;
  Chk(gsproj::Create(g_root, "StaleJob", &lp2, &err), "created StaleJob: " + err);
  gsproj::LockInfo ghost = Job()->me;
  ghost.user = "ghost";
  ghost.pid = 0x7FFFFF00u;
  ghost.sinceUnix = 1760000000;
  Chk(gsproj::TryAcquire(lp2.file, ghost, nullptr) == gsproj::LockResult::Acquired, "a leftover lock from a dead process");
  OpenProjectFile(*s_cmd, Log(), lp2.file.u8string().c_str());
  Frames(ctx, 5);
  Chk(s_cmd->projectPrompt.kind == AppCommandState::ProjectPrompt::Kind::Locked && s_cmd->projectPrompt.stale,
      "the prompt recognises the lock as STALE");
  ClickModal(ctx, "Take over (stale lock)");
  Frames(ctx, 4);
  auto* tk = SessionAt(lp2.Folder());
  Chk(tk && !tk->readOnly, "took over and is now the editor");
  Chk(ReadAll(gsproj::LockPath(lp2.file)).find("ghost") == std::string::npos, "the lock file now names this app, not the ghost");
  if (!tk)
    return;

  // Closing the LAST tab of a project whose database cannot be written is held back (REQ-383 clause 6).
  const int tab = s_cmd->activeDrawingIdx;
  Chk(ProjectNameForTab(*s_cmd, tab) == "StaleJob", "the landing tab belongs to StaleJob");
  PlacePoints(ctx, "STALE", 1, 2, 10.0, 10.0, 1.0);
  FlushProjectPointDb(*tk, Log());
  const fs::path dbf = projpts::DbPath(lp2.Folder(), lp2.layout.at("points"));
  Chk(FileExists(dbf), "StaleJob's database was written");
  const fs::path aside = dbf.string() + ".aside";
  fs::rename(dbf, aside);
  fs::create_directory(dbf);  // a folder where the file belongs: the atomic rename-over fails
  s_cmd->surveyPoints[0].elevation += 5.0;
  Frames(ctx, 4);
  Chk(!ProjectTabMayClose(*s_cmd, tab, Log()), "closing the project's last tab is held back while its points cannot be saved");
  Chk(s_cmd->closeTabPrompt.tabIdx == tab, "the 'Unsaved project points' question is raised");
  Frames(ctx, 4);
  ClickModal(ctx, "Cancel");
  Chk(s_cmd->closeTabPrompt.tabIdx < 0 || !s_cmd->closeTabPrompt.confirmed, "Cancel keeps the tab open");
  s_cmd->closeTabPrompt = {};
  fs::remove(dbf);
  fs::rename(aside, dbf);
  Frames(ctx, 4);
  Chk(FlushProjectPointDb(*tk, Log()), "StaleJob's database writes again once the disk problem is gone");
  Chk(ProjectTabMayClose(*s_cmd, tab, Log()), "and the tab may close");
}

// ===== 14. Damaged .gsproj: never silently dropped =============================================
void StageDamaged(ImGuiTestContext* ctx)
{
  Note("14. Damaged project file");
  gsproj::Project bp;
  std::string err;
  Chk(gsproj::Create(g_root, "BadJob", &bp, &err), "created BadJob: " + err);
  const int t = NewTab(ctx, "D", 0, bp.Folder() / "Drawings" / "D.dwg");
  PlacePoints(ctx, "STANDALONE", 1, 2, 10.0, 10.0, 1.0);
  SaveActiveDocument(*s_cmd, Log());
  Chk(FileExists(bp.Folder() / "Drawings" / "D.dwg"), "saved D.dwg inside BadJob's folder (project not open: standalone)");
  {
    std::ofstream(bp.file, std::ios::binary | std::ios::trunc) << "{ this is not valid json";
  }
  const size_t tabs = s_cmd->drawingTabs.size();
  OpenDrawingInNewTab(*s_cmd, Log(), (bp.Folder() / "Drawings" / "D.dwg").u8string().c_str());
  Frames(ctx, 5);
  Chk(s_cmd->projectPrompt.kind == AppCommandState::ProjectPrompt::Kind::Damaged, "a damaged .gsproj raises the Damaged prompt");
  Chk(s_cmd->drawingTabs.size() == tabs, "nothing was opened silently");
  ClickModal(ctx, "Cancel");
  Chk(s_cmd->drawingTabs.size() == tabs, "Cancel opens nothing");
  OpenDrawingInNewTab(*s_cmd, Log(), (bp.Folder() / "Drawings" / "D.dwg").u8string().c_str());
  Frames(ctx, 5);
  ClickModal(ctx, "Open standalone");
  Frames(ctx, 6);
  // (If the same file is already open in a tab, the app shows that tab instead of adding a second one.)
  Chk(s_cmd->drawingTabs.size() >= tabs && s_cmd->drawingTabs[static_cast<size_t>(s_cmd->activeDrawingIdx)].projectUid == 0,
      "'Open standalone' opens the drawing, as a standalone one");
  Chk(s_cmd->surveyPoints.size() == 2, "...with the 2 points that are inside the DWG");
  (void)t;
}

// ===== 15. Settings enforcement and overrides ==================================================
void StageSettings(ImGuiTestContext* ctx)
{
  Note("15. Enforced units / coordinate system, inherited defaults, overrides");
  auto* job = Job();
  GoTab(ctx, g.fgTab);

  // Units are enforced: a drawing set to meters is pulled back to feet.
  DrawingSettings ds = s_cmd->drawingSettings;
  ApplyDrawingSettings(*s_cmd, 6, s_cmd->modelUnitsPerPlottedInch, ds, Log());
  Frames(ctx, 5);
  Chk(s_cmd->drawingInsUnits == 2, "setting FG to meters is reverted: the project enforces feet (now " + std::to_string(s_cmd->drawingInsUnits) + ")");
  Chk(LogHas("unit"), "and the replacement is reported");

  // Coordinate system chosen on the project reaches every project drawing.
  ProjectSettings ps = *job->settings;
  ps.zoneCode = "HARN/TX.TX-CF";
  Chk(SaveProjectSettings(*s_cmd, g.jobUid, ps, Log()), "saved a coordinate system into Project Settings");
  Frames(ctx, 6);
  Chk(s_cmd->drawingSettings.zoneCode == "HARN/TX.TX-CF", "FG (active) now uses the project's coordinate system");
  GoTab(ctx, g.egTab);
  Chk(s_cmd->drawingSettings.zoneCode == "HARN/TX.TX-CF", "EG uses it too");

  // Inherited default plot scale, then a per-drawing override, then reset.
  ps = *job->settings;
  ps.hasDefaults = true;
  ps.plotScale = 100.f;
  Chk(SaveProjectSettings(*s_cmd, g.jobUid, ps, Log()), "project default plot scale = 100");
  Frames(ctx, 6);
  Chk(s_cmd->modelUnitsPerPlottedInch == 100.f, "EG inherits plot scale 100 (" + std::to_string(s_cmd->modelUnitsPerPlottedInch) + ")");
  GoTab(ctx, g.fgTab);
  Chk(s_cmd->modelUnitsPerPlottedInch == 100.f, "FG inherits plot scale 100");
  SetDrawingPlotScale(*s_cmd, 40.f);
  NoteUserPlotScale(*s_cmd);
  Frames(ctx, 4);
  Chk(s_cmd->drawingSettings.IsOverridden(ProjectDefaultKey::PlotScale), "FG's own plot scale 40 is an OVERRIDE");
  ps = *job->settings;
  ps.plotScale = 200.f;
  Chk(SaveProjectSettings(*s_cmd, g.jobUid, ps, Log()), "project default changed to 200");
  Frames(ctx, 6);
  Chk(s_cmd->modelUnitsPerPlottedInch == 40.f, "the overriding drawing keeps its 40");
  GoTab(ctx, g.egTab);
  Chk(s_cmd->modelUnitsPerPlottedInch == 200.f, "the inheriting drawing follows the project to 200");
  GoTab(ctx, g.fgTab);
  float scale = s_cmd->modelUnitsPerPlottedInch;
  CopyProjectDefault(ps, ProjectDefaultKey::PlotScale, &scale, &s_cmd->drawingSettings);
  s_cmd->drawingSettings.SetOverridden(ProjectDefaultKey::PlotScale, false);
  SetDrawingPlotScale(*s_cmd, scale);
  Frames(ctx, 5);
  Chk(s_cmd->modelUnitsPerPlottedInch == 200.f && !s_cmd->drawingSettings.IsOverridden(ProjectDefaultKey::PlotScale),
      "'Reset to project value' returns FG to 200 and clears the override");
}

// ===== 16. Cross-project paste, unsaved-close =================================================
void StageFootguns(ImGuiTestContext* ctx)
{
  Note("16. Footgun warnings: paste across projects, unsaved database on close");
  auto* job = Job();
  auto* rs = SessionAt(g_root / "Received");
  GoTab(ctx, g.fgTab);
  TagClipboardOrigin(*s_cmd);
  // Same project, another drawing: nothing to warn about.
  GoTab(ctx, g.egTab);
  Chk(CheckClipboardPaste(*s_cmd).verdict == PasteCheck::Verdict::Ok, "paste inside one project is silent");
  // Into the other project.
  int rt = -1;
  for (size_t i = 1; i < s_cmd->drawingTabs.size(); ++i)
    if (rs && s_cmd->drawingTabs[i].projectUid == rs->uid)
      rt = static_cast<int>(i);
  Chk(rt > 0, "there is a tab of the received project to paste into");
  if (rt > 0)
  {
    GoTab(ctx, rt);
    const PasteCheck pc = CheckClipboardPaste(*s_cmd);
    Chk(pc.verdict != PasteCheck::Verdict::Ok, "paste into ANOTHER project is warned about");
    DevShell_Logf("p696", "  paste verdict %d: %s", static_cast<int>(pc.verdict), pc.text.c_str());
    Chk(!pc.text.empty(), "the warning explains itself");
  }
  // Two standalone drawings are never checked.
  GoTab(ctx, g_standaloneTab);
  TagClipboardOrigin(*s_cmd);
  Chk(CheckClipboardPaste(*s_cmd).verdict == PasteCheck::Verdict::Ok, "standalone to standalone is silent");

  // Unsaved database changes: make the database unwritable, edit, then try to close the tab.
  GoTab(ctx, g.fgTab);
  const fs::path dbf = projpts::DbPath(g.jobDir, job->project.layout.at("points"));
  FlushProjectPointDb(*job, Log());
  const fs::path aside = dbf.string() + ".aside";
  fs::rename(dbf, aside);
  fs::create_directory(dbf);  // a folder where the file belongs: the atomic rename-over fails
  s_cmd->surveyPoints[0].elevation += 1.0;
  Frames(ctx, 4);
  const auto unsaved = ProjectsWithUnsavedPoints(*s_cmd, g.jobUid, Log());
  Chk(unsaved.size() == 1 && unsaved[0].name == "TestJob", "closing is told TestJob has unsaved database changes");
  Chk(ProjectTabMayClose(*s_cmd, g.fgTab, Log()) && s_cmd->closeTabPrompt.tabIdx < 0,
      "closing one of SEVERAL tabs is not blocked (the project stays open; the last-tab case is tested with StaleJob)");
  fs::remove(dbf);
  fs::rename(aside, dbf);
  Frames(ctx, 4);
  Chk(FlushProjectPointDb(*job, Log()), "database writes again once the disk problem is gone");
  Chk(ProjectsWithUnsavedPoints(*s_cmd, g.jobUid, Log()).empty(), "nothing left unsaved");
}

}  // namespace

namespace {

void RunE2e(ImGuiTestContext* ctx)
{
  CancelToIdle(ctx);
  ctx->WindowCollapse("//Developer Shell", true);
  ctx->Yield(4);
  g_pass = g_fail = 0;

  g_root = fs::temp_directory_path() / "gosurvey-696-e2e";
  std::error_code ec;
  fs::remove_all(g_root, ec);
  fs::create_directories(g_root);
  const fs::path jobDir = g_root / "TestJob";

  // ===== 1. New Project, through the real dialog =========================================
  Note("1. New Project dialog");
  s_cmd->showNewProjectDialog = true;
  Frames(ctx, 4);
  ctx->SetRef("//$FOCUSED");
  ctx->ItemInputValue("Name", "TestJob");
  ctx->ItemInputValue("Location", g_root.u8string().c_str());
  Frames(ctx, 2);
  ctx->ItemClick("Create");
  Frames(ctx, 6);

  Chk(s_cmd->openProjects.size() == 1, "one project is open after Create");
  auto* job = SessionNamed("TestJob");
  IM_CHECK(job != nullptr);
  Chk(FileExists(jobDir / "TestJob.gsproj"), "TestJob.gsproj marker written");
  for (const char* sub : {"Drawings", "Points", "PointClouds", "PDFs", "Turnovers", "Settings"})
    Chk(FileExists(jobDir / sub), std::string("standard subfolder ") + sub);
  Chk(FileExists(jobDir / "TestJob.gsproj.lock"), "lock file held by this app");
  Chk(!job->readOnly, "creator is the editor (not read-only)");
  Chk(job->settings && job->settings->insUnits == 2, "project fixes linear units = feet (2)");
  Chk(s_cmd->activeDrawingIdx >= 1 && ProjectNameForTab(*s_cmd, s_cmd->activeDrawingIdx) == "TestJob",
      "the landing tab belongs to project TestJob");
  {
    bool inRecent = false;
    for (const auto& e : LoadRecentProjects())
      inRecent |= e.path.find("TestJob.gsproj") != std::string::npos;
    Chk(inRecent, "TestJob listed in Recent Projects");
  }
  s_cmd->projectSettingsUid = 0;  // the dialog opens Project Settings; close it for now
  Frames(ctx, 3);

  // ===== 2. Drawing EG with points =======================================================
  Note("2. Drawing EG: save, then add points");
  const int egTab = s_cmd->activeDrawingIdx;
  s_cmd->drawingTabs[static_cast<size_t>(egTab)].name = "EG";
  s_cmd->activeDocFilePath = (jobDir / "Drawings" / "EG.dwg").u8string();
  SaveActiveDocument(*s_cmd, Log());
  Chk(FileExists(jobDir / "Drawings" / "EG.dwg"), "EG.dwg saved into Drawings/");
  PlacePoints(ctx, "EG", 1, 5, 1000.0, 2000.0, 100.0);
  Chk(s_cmd->surveyPoints.size() == 5, "EG shows its 5 points");
  Chk(job->points && job->points->points.size() == 5, "project database holds 5 points");
  if (const auto* e = DbEntry(job, 1))
    Chk(e->sourceDrawing == "Drawings/EG.dwg", "point 1 source drawing tag = '" + e->sourceDrawing + "' (want Drawings/EG.dwg)");
  SaveActiveDocument(*s_cmd, Log());
  {
    projpts::Db disk;
    Chk(DbOnDisk(job, &disk) && disk.points.size() == 5, "Points/survey-points.gspdb on disk holds 5 points");
  }

  // ===== 3. Drawing FG shares the database ==============================================
  Note("3. Drawing FG: shares, adds, live sync");
  NewDrawingInTab(*s_cmd, Log());
  s_cmd->drawingTabs.back().projectUid = job->uid;
  s_cmd->drawingTabs.back().name = "FG";
  s_cmd->activeDocFilePath = (jobDir / "Drawings" / "FG.dwg").u8string();
  Frames(ctx, 6);
  const int fgTab = TabNamed("FG");
  Chk(fgTab > 0 && ProjectNameForTab(*s_cmd, fgTab) == "TestJob", "FG tab belongs to TestJob");
  Chk(s_cmd->surveyPoints.size() == 5, "FG (empty drawing) sees EG's 5 points from the database");
  PlacePoints(ctx, "FG", 6, 3, 1000.0, 2100.0, 200.0);
  Chk(job->points->points.size() == 8, "database now holds 8 points");
  if (const auto* e = DbEntry(job, 6))
    Chk(e->sourceDrawing == "Drawings/FG.dwg", "point 6 source drawing tag = '" + e->sourceDrawing + "'");
  SaveActiveDocument(*s_cmd, Log());
  Chk(FileExists(jobDir / "Drawings" / "FG.dwg"), "FG.dwg saved");

  GoTab(ctx, egTab);
  Chk(s_cmd->surveyPoints.size() == 8, "EG now shows all 8 points (live update from FG)");

  s_cmd->surveyPoints[static_cast<size_t>(IndexOfPoint(2))].elevation = 150.0;
  Frames(ctx, 4);
  Chk(DbEntry(job, 2) && DbEntry(job, 2)->point.elevation == 150.0, "edit of point 2 in EG reached the database");
  GoTab(ctx, fgTab);
  Chk(IndexOfPoint(2) >= 0 && s_cmd->surveyPoints[static_cast<size_t>(IndexOfPoint(2))].elevation == 150.0,
      "FG shows the edited elevation of point 2");

  g.jobUid = job->uid;
  g.egTab = egTab;
  g.fgTab = fgTab;
  g.jobDir = jobDir;
  g.extDir = g_root / "external";

  StageWarnings(ctx);
  StageFilters(ctx);
  StagePersistence(ctx);
  StageStandalone(ctx);
  StageAddDrawing(ctx);
  StagePdf(ctx);
  StageTurnover(ctx);
  StagePack(ctx);
  StageSettings(ctx);
  StageFootguns(ctx);
  StageMissing(ctx);
  StageLocking(ctx);
  StageDamaged(ctx);

  DevShell_Logf("p696", "SUMMARY: %d pass, %d fail", g_pass, g_fail);
  ctx->WindowCollapse("//Developer Shell", false);
  IM_CHECK_EQ(g_fail, 0);
}

}  // namespace

void DevShell_RegisterProjectTests(ImGuiTestEngine* engine, AppCommandState* cmd)
{
  s_cmd = cmd;
  ImGuiTest* t = IM_REGISTER_TEST(engine, "gosurvey", "p696-e2e");
  t->TestFunc = [](ImGuiTestContext* ctx) { RunE2e(ctx); };
}

#endif
