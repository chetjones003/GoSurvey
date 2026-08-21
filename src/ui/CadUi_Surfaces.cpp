// Surface Manager (REQ-075).
//
// An explorer tree over each surface's definition — `Surfaces ▸ <surface> ▸ Definition ▸ Point
// Groups / Breaklines / Boundaries / Point Files` — with right-click ▸ Add… on the category nodes
// and Remove / Move up / Move down on the items. REQ-075 has required this since 2026-08-12; what
// stood here before was a deliberate stub that could only create, rename, rebuild and delete.
//
// **The panel calls the same functions the commands call.** Adding a breakline from the tree starts
// `StartDesignateBreaklineCommand`, exactly as typing DESIGNATEBREAKLINE does, so the panel and the
// command line cannot drift apart — and the REQ-203 driver keeps testing the same code the panel
// drives, which it could not reach through ImGui.
//
// Point Files are LINKS (REQ-086): re-read on every rebuild, and their points never become drawing
// survey points. "Import into drawing" on an item is the explicit break-the-link action.
//
// Breakline and boundary types beyond what the engine has are **not listed** at all — REQ-084's
// 2026-08-18 revision added an acceptance condition that no menu entry may be present that cannot
// act (Find… was dropped for exactly this), so Proximity / Wall / Non-destructive / Data Clip are
// absent rather than greyed.

#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"
#include "WinFileDialogs.hpp"  // REQ-086: Browse... on the Add Point File dialog

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

std::string NextSurfaceName(const AppCommandState& cmd) {
  const std::string base = "Surface";
  if (FindSurfaceIndex(cmd, base) < 0)
    return base;
  for (int n = 2; n < 10000; ++n) {
    const std::string candidate = base + " (" + std::to_string(n) + ")";
    if (FindSurfaceIndex(cmd, candidate) < 0)
      return candidate;
  }
  return base;
}

/// Elevation range of a built surface, for the readout.
bool SurfaceElevationRange(const CadSurface& s, float* lo, float* hi) {
  if (!s.tin || s.tin->vertsXyz.size() < 3)
    return false;
  float mn = s.tin->vertsXyz[2], mx = mn;
  for (size_t i = 2; i < s.tin->vertsXyz.size(); i += 3) {
    mn = std::min(mn, s.tin->vertsXyz[i]);
    mx = std::max(mx, s.tin->vertsXyz[i]);
  }
  *lo = mn;
  *hi = mx;
  return true;
}

/// REQ-075: "a surface that is out of date or rebuilding is shown as such, and the state clears when
/// the rebuild lands." Both states are already knowable without any new bookkeeping — a rebuild in
/// flight is an entry in `surfaceRebuildAsync`, and out-of-date is REQ-069's own dirty check.
enum class SurfaceState { Current, Stale, Rebuilding };

SurfaceState StateOf(const AppCommandState& cmd, size_t surfaceIndex) {
  const CadSurface& s = cmd.cadSurfaces[surfaceIndex];
  // By stable id, matching how the job itself is keyed (ADR-036 (a)). Keying this on the name showed
  // "Rebuilding" against the wrong surface the moment one was renamed mid-rebuild.
  const std::uint64_t id =
      surfaceIndex < cmd.cadSurfaceAttrs.size() ? cmd.cadSurfaceAttrs[surfaceIndex].id : 0;
  if (id != 0)
    for (const auto& job : cmd.surfaceRebuildAsync)
      if (job && job->surfaceId == id)
        return SurfaceState::Rebuilding;
  return s.builtAtRevision == cmd.cadGpuRevision ? SurfaceState::Current : SurfaceState::Stale;
}

ImVec4 StateColor(SurfaceState st) {
  switch (st) {
  case SurfaceState::Rebuilding: return ImVec4(0.45f, 0.72f, 0.95f, 1.f);
  case SurfaceState::Stale:      return ImVec4(0.95f, 0.75f, 0.35f, 1.f);
  case SurfaceState::Current:    break;
  }
  return ImVec4(0.55f, 0.80f, 0.55f, 1.f);
}

const char* StateLabel(SurfaceState st) {
  switch (st) {
  case SurfaceState::Rebuilding: return "rebuilding";
  case SurfaceState::Stale:      return "out of date";
  case SurfaceState::Current:    break;
  }
  return "current";
}

/// What a definition item points at, for the tree row — "Polyline #7", or the missing case. An id
/// that no longer resolves is SHOWN rather than hidden: it is about to be pruned by the next
/// rebuild, and a row that silently vanished would leave nothing to explain the triangle count.
std::string EntityLabel(const AppCommandState& cmd, std::uint64_t id) {
  const EntityRef ref = FindEntityById(cmd, id);
  if (!ref.valid())
    return "(missing entity #" + std::to_string(id) + ")";
  const char* kind = ref.kind == EntityKind::Line ? "Line" : ref.kind == EntityKind::Polyline ? "Polyline" : "Object";
  return std::string(kind) + " #" + std::to_string(id);
}

/// REQ-086 point-file column layouts, in the order SurveyCsvLayoutFromUiIndex expects.
const char* LayoutName(int layoutIndex) {
  switch (layoutIndex) {
  case 1:  return "PENZD";
  case 2:  return "NEZ";
  case 3:  return "ENZ";
  default: return "PNEZD";
  }
}

const char* BoundaryKindName(CadBoundaryKind k) {
  return k == CadBoundaryKind::Outer ? "Outer" : k == CadBoundaryKind::Hide ? "Hide" : "Show";
}

/// Which dialog wants opening after the tree is drawn. ImGui popups must be opened from the same id
/// stack level they are begun at, so the tree records an intent and the caller acts on it.
enum class PendingDialog { None, AddBreakline, AddBoundary, AddPointGroup, AddPointFile };

/// Moves item \p i of \p v by \p delta, clamped. REQ-075 asks for reorder, and for boundaries the
/// order is not cosmetic — REQ-069: "boundaries apply in definition order", so an outer ring after a
/// hide ring means something different from before it.
template <typename T>
bool MoveInVector(std::vector<T>& v, size_t i, int delta) {
  const long long j = static_cast<long long>(i) + delta;
  if (j < 0 || j >= static_cast<long long>(v.size()))
    return false;
  std::swap(v[i], v[static_cast<size_t>(j)]);
  return true;
}

} // namespace

void DrawSurfaceManagerWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  if (!cmd.showSurfaceManagerWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(860, 520), ImGuiCond_FirstUseEver);
  bool open = cmd.showSurfaceManagerWindow;
  if (!ImGui::Begin("Surfaces", &open)) {
    cmd.showSurfaceManagerWindow = open;
    ImGui::End();
    return;
  }
  cmd.showSurfaceManagerWindow = open;

  static int selIdx = 0;
  if (selIdx >= static_cast<int>(cmd.cadSurfaces.size()))
    selIdx = static_cast<int>(cmd.cadSurfaces.size()) - 1;
  if (selIdx < 0)
    selIdx = 0;

  // Deferred actions. Every mutation below is recorded here and applied after the tree walk, so the
  // vectors the tree is iterating are never resized underneath it.
  int deleteIdx = -1;
  PendingDialog wantDialog = PendingDialog::None;
  int dialogSurface = -1;
  int rebuildIdx = -1;
  struct RemoveReq { int surface = -1; int kind = 0; size_t index = 0; };  // 0 group, 1 breakline, 2 boundary, 3 point file
  RemoveReq removeReq;
  struct MoveReq { int surface = -1; int kind = 0; size_t index = 0; int delta = 0; };
  MoveReq moveReq;
  struct ImportFileReq { int surface = -1; size_t index = 0; };
  ImportFileReq importFileReq;

  const float footer = ImGui::GetFrameHeightWithSpacing() + 8.f;

  // ── Left: the definition tree ─────────────────────────────────────────────────────────────────
  ImGui::BeginChild("##sftree_outer", ImVec2(360.f, -footer), false);
  ImGui::TextUnformatted("Surfaces:");
  ImGui::BeginChild("##sftree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.f - 4.f), true);

  if (cmd.cadSurfaces.empty()) {
    ImGui::TextDisabled("(none)");
  }
  for (size_t si = 0; si < cmd.cadSurfaces.size(); ++si) {
    CadSurface& s = cmd.cadSurfaces[si];
    ImGui::PushID(static_cast<int>(si));

    ImGuiTreeNodeFlags surfFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (static_cast<int>(si) == selIdx)
      surfFlags |= ImGuiTreeNodeFlags_Selected;
    if (si == 0)
      ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    const bool surfOpen = ImGui::TreeNodeEx("##surf", surfFlags, "%s", s.name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
      selIdx = static_cast<int>(si);

    // State chip, right-aligned on the surface row (REQ-075).
    const SurfaceState state = StateOf(cmd, si);
    if (state != SurfaceState::Current) {
      ImGui::SameLine();
      ImGui::TextColored(StateColor(state), "[%s]", StateLabel(state));
    }

    if (ImGui::BeginPopupContextItem("##surfctx")) {
      selIdx = static_cast<int>(si);
      if (ImGui::MenuItem("Rebuild now"))
        rebuildIdx = static_cast<int>(si);
      ImGui::Separator();
      if (ImGui::MenuItem("Delete surface"))
        deleteIdx = static_cast<int>(si);
      ImGui::EndPopup();
    }

    if (surfOpen) {
      ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
      if (ImGui::TreeNodeEx("Definition", ImGuiTreeNodeFlags_SpanAvailWidth)) {

        // ── Point Groups ──────────────────────────────────────────────────────────────────────
        if (ImGui::TreeNodeEx("##pg", ImGuiTreeNodeFlags_SpanAvailWidth, "Point Groups (%d)",
                              static_cast<int>(s.sourcePointGroups.size()))) {
          if (ImGui::BeginPopupContextItem("##pgctx")) {
            if (ImGui::MenuItem("Add..."))
              { wantDialog = PendingDialog::AddPointGroup; dialogSurface = static_cast<int>(si); }
            ImGui::EndPopup();
          }
          for (size_t i = 0; i < s.sourcePointGroups.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const bool exists = FindPointGroupIndex(cmd, s.sourcePointGroups[i]) >= 0;
            if (exists)
              ImGui::BulletText("%s", s.sourcePointGroups[i].c_str());
            else
              ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.f), "  * %s  (missing)",
                                 s.sourcePointGroups[i].c_str());
            if (ImGui::BeginPopupContextItem("##pgitem")) {
              if (ImGui::MenuItem("Remove"))
                { removeReq = {static_cast<int>(si), 0, i}; }
              ImGui::EndPopup();
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        } else if (ImGui::BeginPopupContextItem("##pgctx")) {
          if (ImGui::MenuItem("Add..."))
            { wantDialog = PendingDialog::AddPointGroup; dialogSurface = static_cast<int>(si); }
          ImGui::EndPopup();
        }

        // ── Breaklines ────────────────────────────────────────────────────────────────────────
        if (ImGui::TreeNodeEx("##bl", ImGuiTreeNodeFlags_SpanAvailWidth, "Breaklines (%d)",
                              static_cast<int>(s.breaklines.size()))) {
          if (ImGui::BeginPopupContextItem("##blctx")) {
            if (ImGui::MenuItem("Add..."))
              { wantDialog = PendingDialog::AddBreakline; dialogSurface = static_cast<int>(si); }
            ImGui::EndPopup();
          }
          for (size_t i = 0; i < s.breaklines.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const CadSurfaceBreakline& bl = s.breaklines[i];
            const std::string what = EntityLabel(cmd, bl.entityId);
            if (bl.description.empty())
              ImGui::BulletText("%s", what.c_str());
            else
              ImGui::BulletText("%s  -  %s", bl.description.c_str(), what.c_str());
            if (ImGui::BeginPopupContextItem("##blitem")) {
              if (ImGui::MenuItem("Remove"))
                { removeReq = {static_cast<int>(si), 1, i}; }
              ImGui::Separator();
              if (ImGui::MenuItem("Move up", nullptr, false, i > 0))
                { moveReq = {static_cast<int>(si), 1, i, -1}; }
              if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < s.breaklines.size()))
                { moveReq = {static_cast<int>(si), 1, i, +1}; }
              ImGui::EndPopup();
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        } else if (ImGui::BeginPopupContextItem("##blctx")) {
          if (ImGui::MenuItem("Add..."))
            { wantDialog = PendingDialog::AddBreakline; dialogSurface = static_cast<int>(si); }
          ImGui::EndPopup();
        }

        // ── Boundaries ────────────────────────────────────────────────────────────────────────
        if (ImGui::TreeNodeEx("##bd", ImGuiTreeNodeFlags_SpanAvailWidth, "Boundaries (%d)",
                              static_cast<int>(s.boundaries.size()))) {
          if (ImGui::BeginPopupContextItem("##bdctx")) {
            if (ImGui::MenuItem("Add..."))
              { wantDialog = PendingDialog::AddBoundary; dialogSurface = static_cast<int>(si); }
            ImGui::EndPopup();
          }
          for (size_t i = 0; i < s.boundaries.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const CadSurfaceBoundary& b = s.boundaries[i];
            const std::string what = EntityLabel(cmd, b.entityId);
            if (b.name.empty())
              ImGui::BulletText("%s  -  %s", BoundaryKindName(b.kind), what.c_str());
            else
              ImGui::BulletText("%s  (%s)  -  %s", b.name.c_str(), BoundaryKindName(b.kind), what.c_str());
            if (ImGui::BeginPopupContextItem("##bditem")) {
              if (ImGui::MenuItem("Remove"))
                { removeReq = {static_cast<int>(si), 2, i}; }
              ImGui::Separator();
              // Order is load-bearing here, not cosmetic (REQ-069: definition order).
              if (ImGui::MenuItem("Move up", nullptr, false, i > 0))
                { moveReq = {static_cast<int>(si), 2, i, -1}; }
              if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < s.boundaries.size()))
                { moveReq = {static_cast<int>(si), 2, i, +1}; }
              ImGui::EndPopup();
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        } else if (ImGui::BeginPopupContextItem("##bdctx")) {
          if (ImGui::MenuItem("Add..."))
            { wantDialog = PendingDialog::AddBoundary; dialogSurface = static_cast<int>(si); }
          ImGui::EndPopup();
        }

        // ── Point Files (REQ-086) ─────────────────────────────────────────────────────────────
        // A LINK, not an import: the file is re-read on every rebuild and its points never become
        // drawing survey points. "Import into drawing" is the explicit break-the-link action.
        if (ImGui::TreeNodeEx("##pf", ImGuiTreeNodeFlags_SpanAvailWidth, "Point Files (%d)",
                              static_cast<int>(s.sourcePointFiles.size()))) {
          if (ImGui::BeginPopupContextItem("##pfctx")) {
            if (ImGui::MenuItem("Add..."))
              { wantDialog = PendingDialog::AddPointFile; dialogSurface = static_cast<int>(si); }
            ImGui::EndPopup();
          }
          for (size_t i = 0; i < s.sourcePointFiles.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const CadSurfacePointFile& pf = s.sourcePointFiles[i];
            ImGui::BulletText("%s  (%s%s)", pf.path.c_str(), LayoutName(pf.layoutIndex),
                              pf.skipFirstRow ? ", header" : "");
            if (ImGui::BeginPopupContextItem("##pfitem")) {
              if (ImGui::MenuItem("Import into drawing (breaks the link)"))
                { importFileReq = {static_cast<int>(si), i}; }
              ImGui::Separator();
              if (ImGui::MenuItem("Remove link"))
                { removeReq = {static_cast<int>(si), 3, i}; }
              ImGui::EndPopup();
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        } else if (ImGui::BeginPopupContextItem("##pfctx")) {
          if (ImGui::MenuItem("Add..."))
            { wantDialog = PendingDialog::AddPointFile; dialogSurface = static_cast<int>(si); }
          ImGui::EndPopup();
        }

        ImGui::TreePop();  // Definition
      }
      ImGui::TreePop();  // surface
    }
    ImGui::PopID();
  }
  ImGui::EndChild();

  ImGui::BeginDisabled(cmd.pointGroups.empty());
  if (ImGui::Button("New from group...", ImVec2(-1, 0)))
    ImGui::OpenPopup("##newsurface");
  ImGui::EndDisabled();
  if (cmd.pointGroups.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("A surface is built from point groups - create one in Survey > Groups first.");
  ImGui::BeginDisabled(cmd.cadSurfaces.empty());
  if (ImGui::Button("Delete", ImVec2(-1, 0)))
    deleteIdx = selIdx;
  ImGui::EndDisabled();

  // Creation popup: name + which groups supply the points.
  if (ImGui::BeginPopup("##newsurface")) {
    static std::string newName;
    static std::vector<char> picked;
    if (picked.size() != cmd.pointGroups.size()) {
      picked.assign(cmd.pointGroups.size(), 0);
      newName = NextSurfaceName(cmd);
    }
    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(240.f);
    ImGui::InputText("##sfname", &newName);
    ImGui::Spacing();
    ImGui::TextUnformatted("Build from point groups:");
    int total = 0;
    for (size_t i = 0; i < cmd.pointGroups.size(); ++i) {
      const std::vector<int> members = ResolvePointGroup(cmd, cmd.pointGroups[i], nullptr);
      bool on = picked[i] != 0;
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Checkbox(cmd.pointGroups[i].name.c_str(), &on))
        picked[i] = on ? 1 : 0;
      ImGui::SameLine();
      ImGui::TextDisabled("(%d)", static_cast<int>(members.size()));
      ImGui::PopID();
      if (on)
        total += static_cast<int>(members.size());
    }
    ImGui::Spacing();
    if (total < 3)
      ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.f), "Need at least 3 points; %d selected.", total);
    else
      ImGui::Text("%d point(s) selected.", total);

    ImGui::BeginDisabled(total < 3 || newName.empty());
    if (ImGui::Button("Create")) {
      std::vector<std::string> groups;
      for (size_t i = 0; i < cmd.pointGroups.size(); ++i)
        if (picked[i])
          groups.push_back(cmd.pointGroups[i].name);
      PushUndoSnapshot(cmd, "Create surface");
      const int ni = CreateSurfaceFromPointGroups(cmd, newName, groups, *log);
      if (ni >= 0)
        selIdx = ni;
      picked.clear();  // forces re-seed (and a fresh default name) next time the popup opens
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      picked.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // ── Right: the selected surface ───────────────────────────────────────────────────────────────
  ImGui::BeginChild("##sfright", ImVec2(0, -footer), false);
  if (cmd.cadSurfaces.empty()) {
    ImGui::TextDisabled("No surfaces yet.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "A surface is triangulated from the points in one or more point groups. Define a group under "
        "Survey \xe2\x96\xb8 Groups, then create a surface from it here. Once it exists, right-click "
        "Breaklines or Boundaries in the tree to add to its definition.");
  } else {
    CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(selIdx)];

    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    std::string nameBuf = s.name;
    if (ImGui::InputText("##sfrename", &nameBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
      const int clash = FindSurfaceIndex(cmd, nameBuf);
      if (nameBuf.empty())
        log->push_back("Surface name cannot be empty — keeping \"" + s.name + "\".");
      else if (clash >= 0 && clash != selIdx)
        log->push_back("A surface named \"" + nameBuf + "\" already exists — rename refused.");
      else {
        PushUndoSnapshot(cmd, "Rename surface");
        log->push_back("Renamed surface \"" + s.name + "\" to \"" + nameBuf + "\".");
        s.name = nameBuf;
        BumpCadGpuCache(cmd);
      }
    }

    ImGui::Spacing();
    const SurfaceState state = StateOf(cmd, static_cast<size_t>(selIdx));
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    ImGui::TextColored(StateColor(state), "%s", StateLabel(state));

    ImGui::Spacing();
    ImGui::Separator();
    if (!s.tin) {
      ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.f), "Not built.");
    } else {
      ImGui::Text("%d points, %d triangles.", s.vertexCount(), s.triangleCount());
      float lo = 0.f, hi = 0.f;
      if (SurfaceElevationRange(s, &lo, &hi))
        ImGui::Text("Elevation %.2f to %.2f (%.2f range).", lo, hi, hi - lo);
    }
    if (!s.lastBuildMessage.empty())
      ImGui::TextWrapped("%s", s.lastBuildMessage.c_str());

    ImGui::Spacing();
    ImGui::Text("Definition: %d group(s), %d breakline(s), %d boundary(ies).",
                static_cast<int>(s.sourcePointGroups.size()), static_cast<int>(s.breaklines.size()),
                static_cast<int>(s.boundaries.size()));

    ImGui::Spacing();
    if (ImGui::Button("Rebuild"))
      rebuildIdx = selIdx;
    ImGui::SameLine();
    ImGui::TextDisabled("Rebuilds from the current definition. Edits rebuild on their own.");
  }
  ImGui::EndChild();

  if (ImGui::Button("Close"))
    cmd.showSurfaceManagerWindow = false;

  // ── Dialogs ───────────────────────────────────────────────────────────────────────────────────
  static int dlgSurface = -1;
  static std::string dlgText;
  static int dlgBoundaryKind = 0;
  if (wantDialog != PendingDialog::None && dialogSurface >= 0) {
    dlgSurface = dialogSurface;
    dlgText.clear();
    dlgBoundaryKind = 0;
    ImGui::OpenPopup(wantDialog == PendingDialog::AddBreakline     ? "Add Breaklines"
                     : wantDialog == PendingDialog::AddBoundary    ? "Add Boundaries"
                     : wantDialog == PendingDialog::AddPointFile   ? "Add Point File"
                                                                   : "Add Point Group");
  }

  const bool dlgSurfaceValid = dlgSurface >= 0 && dlgSurface < static_cast<int>(cmd.cadSurfaces.size());

  if (ImGui::BeginPopupModal("Add Breaklines", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Description:");
    ImGui::SetNextItemWidth(360.f);
    ImGui::InputText("##bldesc", &dlgText);
    ImGui::Spacing();
    ImGui::TextUnformatted("Type:");
    ImGui::SetNextItemWidth(360.f);
    // Only Standard: the engine has one breakline type, and ADR-028 defers the rest. Listing them
    // greyed would be a control that cannot act (REQ-084's rule).
    const char* blTypes[] = {"Standard"};
    int blType = 0;
    ImGui::Combo("##bltype", &blType, blTypes, 1);
    ImGui::Spacing();
    ImGui::TextDisabled("OK, then pick a line or polyline in the drawing.");
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      if (dlgSurfaceValid) {
        cmd.designateBreaklineDescription = dlgText;
        StartDesignateBreaklineCommand(cmd, cmd.cadSurfaces[static_cast<size_t>(dlgSurface)].name, *log);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Add Boundaries", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Name:");
    ImGui::SetNextItemWidth(360.f);
    ImGui::InputText("##bdname", &dlgText);
    ImGui::Spacing();
    ImGui::TextUnformatted("Type:");
    ImGui::SetNextItemWidth(360.f);
    // The three the engine implements. Data Clip is not among them and is not listed.
    const char* bdTypes[] = {"Outer", "Hide", "Show"};
    ImGui::Combo("##bdtype", &dlgBoundaryKind, bdTypes, 3);
    ImGui::Spacing();
    ImGui::TextDisabled("OK, then pick a CLOSED polyline in the drawing.");
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      if (dlgSurfaceValid) {
        const CadBoundaryKind kind = dlgBoundaryKind == 1   ? CadBoundaryKind::Hide
                                     : dlgBoundaryKind == 2 ? CadBoundaryKind::Show
                                                            : CadBoundaryKind::Outer;
        cmd.designateBoundaryName = dlgText;
        StartDesignateBoundaryCommand(cmd, cmd.cadSurfaces[static_cast<size_t>(dlgSurface)].name, kind, *log);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Add Point File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    static int pfLayout = 0;
    static bool pfHeader = false;
    ImGui::TextUnformatted("Point file:");
    ImGui::SetNextItemWidth(420.f);
    ImGui::InputText("##pfpath", &dlgText);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
      char buf[1024] = {0};
      if (BrowseOpenFileCsvUtf8(buf, sizeof(buf)))
        dlgText = buf;
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Column layout:");
    ImGui::SetNextItemWidth(200.f);
    // A point file does not describe its own column order, so the layout is stated, not guessed —
    // guessing would swap northing for easting on a file that happens to parse either way.
    const char* layouts[] = {"PNEZD", "PENZD", "NEZ", "ENZ"};
    ImGui::Combo("##pflayout", &pfLayout, layouts, 4);
    ImGui::SameLine();
    ImGui::Checkbox("First row is a header", &pfHeader);
    ImGui::Spacing();
    ImGui::TextDisabled("The file stays LINKED: it is re-read whenever the surface rebuilds,\n"
                        "and its points do not become drawing survey points. Use\n"
                        "\"Import into drawing\" on the item to change that.");
    ImGui::Spacing();
    ImGui::BeginDisabled(dlgText.empty());
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      if (dlgSurfaceValid) {
        // Straight through the command line, so the panel and SURFACEADDFILE cannot disagree about
        // what linking means — including refusing a path that cannot be read, and the undo snapshot.
        std::string line = "SURFACEADDFILE " + cmd.cadSurfaces[static_cast<size_t>(dlgSurface)].name +
                           ", " + dlgText + ", " + layouts[pfLayout];
        if (pfHeader)
          line += ", HEADER";
        std::vector<char> buf(line.begin(), line.end());
        buf.push_back('\0');
        ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), cmd, *log);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Add Point Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!dlgSurfaceValid) {
      ImGui::CloseCurrentPopup();
    } else {
      CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(dlgSurface)];
      ImGui::TextUnformatted("Add a point group to this surface:");
      ImGui::Spacing();
      bool any = false;
      for (size_t i = 0; i < cmd.pointGroups.size(); ++i) {
        const std::string& gname = cmd.pointGroups[i].name;
        const bool already =
            std::find(s.sourcePointGroups.begin(), s.sourcePointGroups.end(), gname) != s.sourcePointGroups.end();
        if (already)
          continue;
        any = true;
        ImGui::PushID(static_cast<int>(i));
        const std::vector<int> members = ResolvePointGroup(cmd, cmd.pointGroups[i], nullptr);
        if (ImGui::Selectable(gname.c_str())) {
          PushUndoSnapshot(cmd, "Add point group to surface");
          s.sourcePointGroups.push_back(gname);
          BumpCadGpuCache(cmd);
          log->push_back("Surface \"" + s.name + "\": added point group \"" + gname + "\".");
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%d)", static_cast<int>(members.size()));
        ImGui::PopID();
      }
      if (!any)
        ImGui::TextDisabled("(every point group is already in this definition)");
      ImGui::Spacing();
      if (ImGui::Button("Cancel", ImVec2(120, 0)))
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::End();

  // ── Deferred mutations ────────────────────────────────────────────────────────────────────────
  // Applied after the tree walk so nothing resizes a vector the walk is iterating.
  if (rebuildIdx >= 0 && rebuildIdx < static_cast<int>(cmd.cadSurfaces.size())) {
    PushUndoSnapshot(cmd, "Rebuild surface");
    BuildSurfaceFromSources(cmd, cmd.cadSurfaces[static_cast<size_t>(rebuildIdx)], *log);
    BumpCadGpuCache(cmd);
  }

  if (removeReq.surface >= 0 && removeReq.surface < static_cast<int>(cmd.cadSurfaces.size())) {
    CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(removeReq.surface)];
    const char* what = removeReq.kind == 0   ? "point group"
                       : removeReq.kind == 1 ? "breakline"
                       : removeReq.kind == 2 ? "boundary"
                                             : "point file link";
    bool removed = false;
    if (removeReq.kind == 0 && removeReq.index < s.sourcePointGroups.size()) {
      PushUndoSnapshot(cmd, "Remove point group from surface");
      s.sourcePointGroups.erase(s.sourcePointGroups.begin() + static_cast<std::ptrdiff_t>(removeReq.index));
      removed = true;
    } else if (removeReq.kind == 1 && removeReq.index < s.breaklines.size()) {
      PushUndoSnapshot(cmd, "Remove surface breakline");
      s.breaklines.erase(s.breaklines.begin() + static_cast<std::ptrdiff_t>(removeReq.index));
      removed = true;
    } else if (removeReq.kind == 2 && removeReq.index < s.boundaries.size()) {
      PushUndoSnapshot(cmd, "Remove surface boundary");
      s.boundaries.erase(s.boundaries.begin() + static_cast<std::ptrdiff_t>(removeReq.index));
      removed = true;
    } else if (removeReq.kind == 3 && removeReq.index < s.sourcePointFiles.size()) {
      PushUndoSnapshot(cmd, "Unlink surface point file");
      s.sourcePointFiles.erase(s.sourcePointFiles.begin() + static_cast<std::ptrdiff_t>(removeReq.index));
      removed = true;
    }
    if (removed) {
      log->push_back("Surface \"" + s.name + "\": removed " + what + " " +
                     std::to_string(removeReq.index + 1) + " from the definition.");
      BumpCadGpuCache(cmd);  // the dynamic rebuild picks this up next frame (REQ-069)
    }
  }

  if (moveReq.surface >= 0 && moveReq.surface < static_cast<int>(cmd.cadSurfaces.size()) && moveReq.delta != 0) {
    CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(moveReq.surface)];
    bool moved = false;
    if (moveReq.kind == 1)
      moved = MoveInVector(s.breaklines, moveReq.index, moveReq.delta);
    else if (moveReq.kind == 2)
      moved = MoveInVector(s.boundaries, moveReq.index, moveReq.delta);
    if (moved) {
      PushUndoSnapshot(cmd, moveReq.kind == 1 ? "Reorder surface breaklines" : "Reorder surface boundaries");
      BumpCadGpuCache(cmd);
      log->push_back("Surface \"" + s.name + "\": reordered the definition.");
    }
  }

  if (importFileReq.surface >= 0 && importFileReq.surface < static_cast<int>(cmd.cadSurfaces.size())) {
    // REQ-086's break-the-link, routed through the command so the panel and SURFACEIMPORTFILE share
    // one definition of what it does — including refusing to drop the link when nothing imported.
    const std::string line = "SURFACEIMPORTFILE " +
                             cmd.cadSurfaces[static_cast<size_t>(importFileReq.surface)].name + ", " +
                             std::to_string(importFileReq.index + 1);
    std::vector<char> buf(line.begin(), line.end());
    buf.push_back('\0');
    ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), cmd, *log);
  }

  if (deleteIdx >= 0 && deleteIdx < static_cast<int>(cmd.cadSurfaces.size())) {
    PushUndoSnapshot(cmd, "Delete surface");  // undoable in one step (REQ-068)
    log->push_back("Deleted surface \"" + cmd.cadSurfaces[static_cast<size_t>(deleteIdx)].name + "\".");
    EraseSurfaceAtIndex(cmd, static_cast<size_t>(deleteIdx));
    if (selIdx >= static_cast<int>(cmd.cadSurfaces.size()))
      selIdx = static_cast<int>(cmd.cadSurfaces.size()) - 1;
  }
}
