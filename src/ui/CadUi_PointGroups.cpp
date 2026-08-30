// Point Group manager (REQ-067).
//
// Its own translation unit rather than more of CadUi.cpp, following CadUi_TraverseEditor.cpp and
// CadUi_ImportExportPoints.cpp.
//
// The panel deliberately shows the **resolved count and the resolved ids** live while the rule is
// being typed. A rule-based group is otherwise invisible until something consumes it, and the two
// mistakes this feature makes easy — a rule that matches nothing, and an empty rule the user thinks
// means "everything" — are both instantly obvious when the count is on screen next to the fields.

#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "SurveyPoints.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <string>
#include <vector>

namespace {

/// Unique default name for a new group: "Point Group", "Point Group (2)", …
std::string NextGroupName(const AppCommandState& cmd) {
  const std::string base = "Point Group";
  if (FindPointGroupIndex(cmd, base) < 0)
    return base;
  for (int n = 2; n < 10000; ++n) {
    const std::string candidate = base + " (" + std::to_string(n) + ")";
    if (FindPointGroupIndex(cmd, candidate) < 0)
      return candidate;
  }
  return base;
}

} // namespace

void DrawPointGroupManagerWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  if (!cmd.showPointGroupManagerWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(760, 460), ImGuiCond_FirstUseEver);
  bool open = cmd.showPointGroupManagerWindow;
  if (!ImGui::Begin("Point Groups", &open)) {
    cmd.showPointGroupManagerWindow = open;
    ImGui::End();
    return;
  }
  cmd.showPointGroupManagerWindow = open;

  static int selIdx = 0;
  if (!cmd.pointGroupManagerFocusName.empty()) {
    const int focused = FindPointGroupIndex(cmd, cmd.pointGroupManagerFocusName);
    if (focused >= 0)
      selIdx = focused;
    cmd.pointGroupManagerFocusName.clear();
  }
  if (selIdx >= static_cast<int>(cmd.pointGroups.size()))
    selIdx = static_cast<int>(cmd.pointGroups.size()) - 1;
  if (selIdx < 0)
    selIdx = 0;

  const float footer = ImGui::GetFrameHeightWithSpacing() + 8.f;
  int deleteIdx = -1;

  // ── Left: the group list ──────────────────────────────────────────────────────────────────────
  ImGui::BeginChild("##pglist_outer", ImVec2(220.f, -footer), false);
  ImGui::TextUnformatted("Groups:");
  ImGui::BeginChild("##pglist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.f - 4.f), true);
  for (size_t i = 0; i < cmd.pointGroups.size(); ++i) {
    const bool sel = (static_cast<int>(i) == selIdx);
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(cmd.pointGroups[i].name.c_str(), sel))
      selIdx = static_cast<int>(i);
    ImGui::PopID();
  }
  if (cmd.pointGroups.empty())
    ImGui::TextDisabled("(none)");
  ImGui::EndChild();

  if (ImGui::Button("New", ImVec2(-1, 0))) {
    PushUndoSnapshot(cmd, "New point group");  // group edits are undoable (REQ-067)
    PointGroup g;
    g.name = NextGroupName(cmd);
    cmd.pointGroups.push_back(std::move(g));
    selIdx = static_cast<int>(cmd.pointGroups.size()) - 1;
    log->push_back("Created point group \"" + cmd.pointGroups.back().name + "\".");
  }
  ImGui::BeginDisabled(cmd.pointGroups.empty());
  if (ImGui::Button("Delete", ImVec2(-1, 0)))
    deleteIdx = selIdx;
  ImGui::EndDisabled();
  ImGui::EndChild();

  ImGui::SameLine();

  // ── Right: the selected group's rule, and what it currently resolves to ───────────────────────
  ImGui::BeginChild("##pgright", ImVec2(0, -footer), false);
  if (cmd.pointGroups.empty()) {
    ImGui::TextDisabled("No point groups yet. \"New\" creates one.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "A point group is a rule, not a fixed list — points imported later join it automatically if "
        "they match.");
  } else {
    PointGroup& g = cmd.pointGroups[static_cast<size_t>(selIdx)];

    ImGui::TextUnformatted("Name");
    ImGui::SetNextItemWidth(-1);
    std::string nameBuf = g.name;
    if (ImGui::InputText("##pgname", &nameBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
      const int clash = FindPointGroupIndex(cmd, nameBuf);
      if (nameBuf.empty()) {
        log->push_back("Point group name cannot be empty — keeping \"" + g.name + "\".");
      } else if (clash >= 0 && clash != selIdx) {
        // REQ-067: renaming onto an existing name is refused with a specific message, not silently
        // accepted into two groups the user can no longer tell apart.
        log->push_back("A point group named \"" + nameBuf + "\" already exists — rename refused.");
      } else {
        PushUndoSnapshot(cmd, "Rename point group");
        log->push_back("Renamed point group \"" + g.name + "\" to \"" + nameBuf + "\".");
        g.name = nameBuf;
      }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Include points matching ANY of:");
    ImGui::Separator();

    bool ruleEdited = false;
    ImGui::TextUnformatted("Point numbers");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##pgids", &g.rule.idRangesText))
      ruleEdited = true;
    ImGui::TextDisabled("Ranges and singles, e.g.  1-500, 1200, 1400-1450");

    ImGui::Spacing();
    ImGui::TextUnformatted("Description matches");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##pgdesc", &g.rule.descriptionMatch))
      ruleEdited = true;
    ImGui::TextDisabled("Wildcards: *  any run,  ?  one character. Case-insensitive.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Raw description matches");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##pgraw", &g.rule.rawDescriptionMatch))
      ruleEdited = true;
    ImGui::TextDisabled("The field code as collected — unaffected by later description edits.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Selected points");
    ImGui::SameLine();
    ImGui::BeginDisabled(cmd.selectedSurveyPointIndices.empty());
    if (ImGui::SmallButton("Add selection")) {
      PushUndoSnapshot(cmd, "Add points to group");
      int added = 0;
      for (int pi : cmd.selectedSurveyPointIndices) {
        if (pi < 0 || static_cast<size_t>(pi) >= cmd.surveyPoints.size())
          continue;
        const int id = cmd.surveyPoints[static_cast<size_t>(pi)].id;
        if (std::find(g.rule.explicitIds.begin(), g.rule.explicitIds.end(), id) == g.rule.explicitIds.end()) {
          g.rule.explicitIds.push_back(id);
          ++added;
        }
      }
      log->push_back("Added " + std::to_string(added) + " point(s) to \"" + g.name + "\".");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g.rule.explicitIds.empty());
    if (ImGui::SmallButton("Clear picks")) {
      PushUndoSnapshot(cmd, "Clear group picks");
      g.rule.explicitIds.clear();
    }
    ImGui::EndDisabled();
    {
      std::string ids;
      for (size_t i = 0; i < g.rule.explicitIds.size() && i < 40; ++i)
        ids += (i ? ", " : "") + std::to_string(g.rule.explicitIds[i]);
      if (g.rule.explicitIds.size() > 40)
        ids += ", …";
      ImGui::TextDisabled("%s", g.rule.explicitIds.empty() ? "(none picked)" : ids.c_str());
    }

    // ── Live resolution ─────────────────────────────────────────────────────────────────────────
    // Recomputed every frame from the current points, never cached — the same property that makes a
    // group pick up a later import also makes this readout always true.
    ImGui::Spacing();
    ImGui::Separator();
    const std::vector<int> members = ResolvePointGroup(cmd, g, nullptr);
    if (g.rule.empty()) {
      // The trap this readout exists to prevent: an empty rule is EMPTY, not "everything".
      ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.f),
                         "No criteria set — this group contains 0 points.");
      ImGui::TextDisabled("An empty rule matches nothing, not every point.");
    } else if (members.empty()) {
      ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.f), "Matches 0 of %d points.",
                         static_cast<int>(cmd.surveyPoints.size()));
    } else {
      ImGui::Text("Matches %d of %d points.", static_cast<int>(members.size()),
                  static_cast<int>(cmd.surveyPoints.size()));
    }
    {
      std::string preview;
      for (size_t i = 0; i < members.size() && i < 30; ++i)
        preview += (i ? ", " : "") + std::to_string(cmd.surveyPoints[static_cast<size_t>(members[i])].id);
      if (members.size() > 30)
        preview += ", …";
      if (!preview.empty())
        ImGui::TextWrapped("Point numbers: %s", preview.c_str());
    }
    // Report unparseable range tokens once per edit rather than every frame (REQ-201 without spam).
    if (ruleEdited)
      (void)ResolvePointGroup(cmd, g, log);
  }
  ImGui::EndChild();

  if (ImGui::Button("Close"))
    cmd.showPointGroupManagerWindow = false;

  ImGui::End();

  if (deleteIdx >= 0 && deleteIdx < static_cast<int>(cmd.pointGroups.size())) {
    PushUndoSnapshot(cmd, "Delete point group");
    log->push_back("Deleted point group \"" + cmd.pointGroups[static_cast<size_t>(deleteIdx)].name + "\".");
    cmd.pointGroups.erase(cmd.pointGroups.begin() + deleteIdx);
    if (selIdx >= static_cast<int>(cmd.pointGroups.size()))
      selIdx = static_cast<int>(cmd.pointGroups.size()) - 1;
  }
}
