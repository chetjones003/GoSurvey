// Connection Modes window (issue #496 follow-up).
//
// Graphical counterpart of the BCONNECTMODE text wizard, for the same underlying data: which snap
// target (pipe end / flange face / another fitting's generic port) selects which mode on a
// connection point, and which mode is the fallback default. Its own translation unit, following
// CadUi_PointGroups.cpp.
//
// Only usable inside BEDIT — connection points belong to the block definition currently being
// edited (\ref AppCommandState::blockEditorName), same scope BCONNECT/BCONNECTMODE already have.

#include "CadUi.hpp"

#include "CadBlocks.hpp"
#include "CadCommands.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

const char* kTargetLabels[] = {"Pipe end", "Flange face", "Generic port"};
const CadConnectionModeTarget kTargetValues[] = {CadConnectionModeTarget::PipeEnd, CadConnectionModeTarget::FlangeFace,
                                                 CadConnectionModeTarget::GenericPort};
const char* kRoleLabels[] = {"Inlet", "Outlet", "Branch"};
const CadBlockConnectionRole kRoleValues[] = {CadBlockConnectionRole::Inlet, CadBlockConnectionRole::Outlet,
                                              CadBlockConnectionRole::Branch};

int TargetComboIndex(CadConnectionModeTarget t) {
  for (int i = 0; i < 3; ++i)
    if (kTargetValues[i] == t)
      return i;
  return 2;
}

int RoleComboIndex(CadBlockConnectionRole r) {
  for (int i = 0; i < 3; ++i)
    if (kRoleValues[i] == r)
      return i;
  return 0;
}

/// Exactly one mode ends up flagged default (issue #496 AC) — clearing every other one when \p
/// keepIdx is set true, and falling back to the first mode if the caller left none set at all (e.g.
/// after deleting the mode that used to be the default).
void EnforceSingleDefault(CadBlockConnection& conn, int keepIdx) {
  if (keepIdx >= 0 && keepIdx < static_cast<int>(conn.modes.size())) {
    for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i)
      conn.modes[static_cast<size_t>(i)].isDefault = (i == keepIdx);
    return;
  }
  if (!conn.modes.empty() &&
      std::none_of(conn.modes.begin(), conn.modes.end(), [](const CadBlockConnectionMode& m) { return m.isDefault; }))
    conn.modes.front().isDefault = true;
}

std::string NextModeName(const CadBlockConnection& conn) {
  for (int n = 1; n < 1000; ++n) {
    const std::string candidate = "Mode " + std::to_string(n);
    const bool taken = std::any_of(conn.modes.begin(), conn.modes.end(),
                                   [&](const CadBlockConnectionMode& m) { return CadBlockEqCi(m.name, candidate); });
    if (!taken)
      return candidate;
  }
  return "Mode";
}

} // namespace

void DrawConnectionModesWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  if (!cmd.showConnectionModesWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(760, 460), ImGuiCond_FirstUseEver);
  bool open = cmd.showConnectionModesWindow;
  if (!ImGui::Begin("Connection Modes", &open)) {
    cmd.showConnectionModesWindow = open;
    ImGui::End();
    return;
  }
  cmd.showConnectionModesWindow = open;

  if (!cmd.blockEditActive || cmd.blockEditorName.empty()) {
    ImGui::TextWrapped("Open a block with BEDIT first — connection points belong to the block being edited.");
    ImGui::End();
    return;
  }
  const int di = CadBlockFindDef(cmd.blockDefs, cmd.blockEditorName);
  if (di < 0) {
    ImGui::TextWrapped("The block being edited no longer exists.");
    ImGui::End();
    return;
  }
  CadBlockDefinition& def = cmd.blockDefs[static_cast<size_t>(di)];

  const float footer = ImGui::GetFrameHeightWithSpacing() + 8.f;

  // ── Left: the connection points on this block ───────────────────────────────────────────────────
  static int selIdx = 0;
  if (selIdx >= static_cast<int>(def.connections.size()))
    selIdx = static_cast<int>(def.connections.size()) - 1;
  if (selIdx < 0)
    selIdx = 0;

  ImGui::BeginChild("##cmlist_outer", ImVec2(200.f, -footer), false);
  ImGui::TextUnformatted("Connection points:");
  ImGui::BeginChild("##cmlist", ImVec2(0, 0), true);
  for (size_t i = 0; i < def.connections.size(); ++i) {
    const bool sel = (static_cast<int>(i) == selIdx);
    ImGui::PushID(static_cast<int>(i));
    const CadBlockConnection& c = def.connections[i];
    std::string label = c.name.empty() ? "(unnamed)" : c.name;
    if (!c.modes.empty())
      label += "  [" + std::to_string(c.modes.size()) + " modes]";
    if (ImGui::Selectable(label.c_str(), sel))
      selIdx = static_cast<int>(i);
    ImGui::PopID();
  }
  if (def.connections.empty())
    ImGui::TextDisabled("(none — add one with BCONNECT)");
  ImGui::EndChild();
  ImGui::EndChild();

  ImGui::SameLine();

  // ── Right: the selected connection point's modes ────────────────────────────────────────────────
  ImGui::BeginChild("##cmright", ImVec2(0, -footer), false);
  if (def.connections.empty()) {
    ImGui::TextWrapped("No connection points on \"%s\" yet. Use BCONNECT (or the Add Connection ribbon button) "
                       "to add one, then come back here to give it smart modes.",
                       def.name.c_str());
  } else {
    CadBlockConnection& conn = def.connections[static_cast<size_t>(selIdx)];
    ImGui::Text("Connection point: %s", conn.name.c_str());
    ImGui::Spacing();

    if (conn.modes.empty()) {
      ImGui::TextWrapped(
          "This is a plain, single-behavior connection point: it always uses its own role/engagement/"
          "compatibility tag (set via BCONNECTEDIT), no matter what it is snapped to.");
      ImGui::Spacing();
      if (ImGui::Button("Add a mode to make this a smart connection point")) {
        PushUndoSnapshot(cmd, "Add connection mode");
        CadBlockConnectionMode m;
        m.name = NextModeName(conn);
        m.isDefault = true;
        conn.modes.push_back(m);
        cmd.blockEditorDirty = true;
        log->push_back("Added mode \"" + m.name + "\" to \"" + conn.name + "\".");
      }
    } else {
      ImGui::TextWrapped(
          "When this port is snapped to a target, the mode whose Target matches is used. If none "
          "matches, the mode marked Default applies. Existing single-mode fittings are unaffected — "
          "this only applies once a connection point has modes.");
      ImGui::Spacing();

      int removeIdx = -1;
      if (ImGui::BeginTable("##cmtable", 7,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Engagement", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Compat. tag", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 32.f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(conn.modes.size()); ++i) {
          CadBlockConnectionMode& m = conn.modes[static_cast<size_t>(i)];
          ImGui::PushID(i);
          ImGui::TableNextRow();

          ImGui::TableSetColumnIndex(0);
          ImGui::SetNextItemWidth(-1);
          std::string nameBuf = m.name;
          if (ImGui::InputText("##name", &nameBuf)) {
            if (!nameBuf.empty()) {
              PushUndoSnapshot(cmd, "Rename connection mode");
              m.name = nameBuf;
              cmd.blockEditorDirty = true;
            }
          }

          ImGui::TableSetColumnIndex(1);
          ImGui::SetNextItemWidth(-1);
          int targetIdx = TargetComboIndex(m.target);
          if (ImGui::Combo("##target", &targetIdx, kTargetLabels, 3)) {
            PushUndoSnapshot(cmd, "Set connection mode target");
            m.target = kTargetValues[targetIdx];
            cmd.blockEditorDirty = true;
          }

          ImGui::TableSetColumnIndex(2);
          ImGui::SetNextItemWidth(-1);
          int roleIdx = RoleComboIndex(m.role);
          if (ImGui::Combo("##role", &roleIdx, kRoleLabels, 3)) {
            PushUndoSnapshot(cmd, "Set connection mode role");
            m.role = kRoleValues[roleIdx];
            cmd.blockEditorDirty = true;
          }

          ImGui::TableSetColumnIndex(3);
          ImGui::SetNextItemWidth(-1);
          float eng = m.engagementLength;
          if (ImGui::InputFloat("##eng", &eng, 0.f, 0.f, "%.3f")) {
            PushUndoSnapshot(cmd, "Set connection mode engagement");
            m.engagementLength = eng;
            cmd.blockEditorDirty = true;
          }

          ImGui::TableSetColumnIndex(4);
          ImGui::SetNextItemWidth(-1);
          std::string tagBuf = m.compatibilityTag;
          if (ImGui::InputText("##tag", &tagBuf)) {
            PushUndoSnapshot(cmd, "Set connection mode tag");
            m.compatibilityTag = tagBuf;
            cmd.blockEditorDirty = true;
          }

          ImGui::TableSetColumnIndex(5);
          if (ImGui::RadioButton("##default", m.isDefault)) {
            PushUndoSnapshot(cmd, "Set default connection mode");
            EnforceSingleDefault(conn, i);
            cmd.blockEditorDirty = true;
          }

          ImGui::TableSetColumnIndex(6);
          if (ImGui::SmallButton("X"))
            removeIdx = i;

          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      ImGui::Spacing();
      if (ImGui::Button("Add mode")) {
        PushUndoSnapshot(cmd, "Add connection mode");
        CadBlockConnectionMode m;
        m.name = NextModeName(conn);
        conn.modes.push_back(m);
        cmd.blockEditorDirty = true;
        log->push_back("Added mode \"" + m.name + "\" to \"" + conn.name + "\".");
      }
      if (removeIdx >= 0 && removeIdx < static_cast<int>(conn.modes.size())) {
        PushUndoSnapshot(cmd, "Remove connection mode");
        const std::string removedName = conn.modes[static_cast<size_t>(removeIdx)].name;
        conn.modes.erase(conn.modes.begin() + removeIdx);
        EnforceSingleDefault(conn, -1);
        cmd.blockEditorDirty = true;
        log->push_back("Removed mode \"" + removedName + "\" from \"" + conn.name + "\".");
      }
    }
  }
  ImGui::EndChild();

  ImGui::End();
}
