// Surfaces panel (REQ-068).
//
// Deliberately minimal: create a named surface from one or more point groups, rebuild it, delete it,
// and see what it actually contains. The full Surface Manager — definition reordering, style
// assignment, stale/rebuilding state — is REQ-075 and is not started here; building it now would be
// scaffolding for a requirement not in flight.
//
// It shows point count, triangle count and elevation range because those three numbers are how you
// tell a surface that built from the points you meant from one that built from the wrong group, and
// there is otherwise nothing on screen distinguishing them.

#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"

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

} // namespace

void DrawSurfaceManagerWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  if (!cmd.showSurfaceManagerWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(780, 470), ImGuiCond_FirstUseEver);
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

  const float footer = ImGui::GetFrameHeightWithSpacing() + 8.f;
  int deleteIdx = -1;

  // ── Left: surfaces ────────────────────────────────────────────────────────────────────────────
  ImGui::BeginChild("##sflist_outer", ImVec2(220.f, -footer), false);
  ImGui::TextUnformatted("Surfaces:");
  ImGui::BeginChild("##sflist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.f - 4.f), true);
  for (size_t i = 0; i < cmd.cadSurfaces.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(cmd.cadSurfaces[i].name.c_str(), static_cast<int>(i) == selIdx))
      selIdx = static_cast<int>(i);
    ImGui::PopID();
  }
  if (cmd.cadSurfaces.empty())
    ImGui::TextDisabled("(none)");
  ImGui::EndChild();

  ImGui::BeginDisabled(cmd.pointGroups.empty());
  if (ImGui::Button("New from group…", ImVec2(-1, 0)))
    ImGui::OpenPopup("##newsurface");
  ImGui::EndDisabled();
  if (cmd.pointGroups.empty() && ImGui::IsItemHovered())
    ImGui::SetTooltip("A surface is built from point groups — create one in Survey ▸ Groups first.");
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
      ImGui::TextDisabled("(%d points)", static_cast<int>(members.size()));
      ImGui::PopID();
      if (on)
        total += static_cast<int>(members.size());
    }
    ImGui::Spacing();
    // A surface needs three non-collinear points; saying so before the button is pressed beats a
    // failure message after (REQ-201 in the affirmative direction).
    if (total < 3)
      ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.f), "%d point(s) selected — need at least 3.", total);
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
        "Survey \xe2\x96\xb8 Groups, then create a surface from it here.");
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
      }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Built from:");
    if (s.sourcePointGroups.empty()) {
      ImGui::TextDisabled("(no point groups)");
    } else {
      for (const std::string& g : s.sourcePointGroups) {
        const bool exists = FindPointGroupIndex(cmd, g) >= 0;
        if (exists)
          ImGui::BulletText("%s", g.c_str());
        else
          // A source that no longer resolves is shown, not hidden: otherwise a rebuild silently
          // shrinks the surface and nothing explains why.
          ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.45f, 1.f), "  \xe2\x80\xa2 %s  (missing)", g.c_str());
      }
    }

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
    if (ImGui::Button("Rebuild")) {
      PushUndoSnapshot(cmd, "Rebuild surface");
      BuildSurfaceFromSources(cmd, s, *log);
      BumpCadGpuCache(cmd);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Rebuilds from the current points in the groups above.");
  }
  ImGui::EndChild();

  if (ImGui::Button("Close"))
    cmd.showSurfaceManagerWindow = false;

  ImGui::End();

  if (deleteIdx >= 0 && deleteIdx < static_cast<int>(cmd.cadSurfaces.size())) {
    PushUndoSnapshot(cmd, "Delete surface");  // undoable in one step (REQ-068)
    log->push_back("Deleted surface \"" + cmd.cadSurfaces[static_cast<size_t>(deleteIdx)].name + "\".");
    EraseSurfaceAtIndex(cmd, static_cast<size_t>(deleteIdx));
    if (selIdx >= static_cast<int>(cmd.cadSurfaces.size()))
      selIdx = static_cast<int>(cmd.cadSurfaces.size()) - 1;
  }
}
