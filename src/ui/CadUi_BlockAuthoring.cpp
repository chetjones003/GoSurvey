#include "CadUi.hpp"
#include "CadBlocks.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static void BeditSubmitLine(AppCommandState& cmd, std::vector<std::string>& log, const char* line) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", line);
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), cmd, log);
}

static void PaletteTabButton(const char* label, int idx, int* tab) {
  const bool on = *tab == idx;
  if (on)
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 78, 88, 255));
  if (ImGui::Button(label, ImVec2(26.f, 88.f)))
    *tab = idx;
  if (on)
    ImGui::PopStyleColor();
}

void DrawBlockAuthoringPalettes(AppCommandState& cmd, std::vector<std::string>& log) {
  // ADR-043: BCLOSE on a dirty session sets blockEditCloseAsked; raise the Save/Don't-Save/Cancel
  // prompt here (this function already runs every frame while a block editor is open).
  if (cmd.blockEditCloseAsked) {
    ImGui::OpenPopup("Block Editor##bclose");
    if (ImGui::BeginPopupModal("Block Editor##bclose", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Save changes to \"%s\"?", cmd.blockEditorName.c_str());
      ImGui::Spacing();
      if (ImGui::Button("Save", ImVec2(110.f, 0.f))) {
        ImGui::CloseCurrentPopup();
        BeditSubmitLine(cmd, log, "BCLOSE SAVE");
      }
      ImGui::SameLine();
      if (ImGui::Button("Don't Save", ImVec2(110.f, 0.f))) {
        ImGui::CloseCurrentPopup();
        BeditSubmitLine(cmd, log, "BCLOSE DISCARD");
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(110.f, 0.f))) {
        cmd.blockEditCloseAsked = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  if (cmd.blockEditorName.empty() || !cmd.blockAuthoringPaletteOpen)
    return;

  ImGui::SetNextWindowSize(ImVec2(280.f, 520.f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(16.f, 120.f), ImGuiCond_FirstUseEver);
  bool open = cmd.blockAuthoringPaletteOpen;
  if (!ImGui::Begin("BLOCK AUTHORING PALETTES", &open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    cmd.blockAuthoringPaletteOpen = open;
    return;
  }
  cmd.blockAuthoringPaletteOpen = open;

  ImGui::BeginChild("##bapbody", ImVec2(-32.f, 0.f), true);
  if (cmd.blockAuthoringPaletteTab == 0) {
    ImGui::TextUnformatted("Parameters");
    ImGui::Separator();
    const char* items[][2] = {{"Point", "point"},       {"Linear", "linear"},     {"Polar", "polar"},
                              {"XY", "move"},           {"Rotation", "rotation"}, {"Alignment", "linear"},
                              {"Flip", "flip"},         {"Visibility", "visibility"}, {"Lookup", "lookup"},
                              {"Basepoint", "point"}};
    for (const auto& it : items) {
      if (ImGui::Selectable(it[0])) {
        char line[96];
        std::snprintf(line, sizeof(line), "BPARAM %s, %s", it[0], it[1]);
        BeditSubmitLine(cmd, log, line);
      }
    }
  } else if (cmd.blockAuthoringPaletteTab == 1) {
    ImGui::TextUnformatted("Actions");
    ImGui::Separator();
    const char* acts[][2] = {{"Move", "move"},           {"Scale", "scale"},     {"Stretch", "stretch"},
                             {"Polar Stretch", "stretch"}, {"Rotate", "rotate"}, {"Flip", "flip"},
                             {"Array", "move"},          {"Lookup", "lookup"}};
    for (const auto& it : acts) {
      if (ImGui::Selectable(it[0])) {
        char line[128];
        std::snprintf(line, sizeof(line), "BACTION %s, DistPos", it[1]);
        BeditSubmitLine(cmd, log, line);
      }
    }
    ImGui::BeginDisabled();
    ImGui::Selectable("Block Properties Table");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Block Properties Table — not implemented yet.");
  } else if (cmd.blockAuthoringPaletteTab == 2) {
    ImGui::TextUnformatted("Parameter Sets");
    ImGui::Separator();
    if (ImGui::Selectable("Point Move")) {
      BeditSubmitLine(cmd, log, "BPARAM Point, point");
      BeditSubmitLine(cmd, log, "BACTION move, Point");
    }
    if (ImGui::Selectable("Linear Stretch")) {
      BeditSubmitLine(cmd, log, "BPARAM Dist, linear");
      BeditSubmitLine(cmd, log, "BACTION stretch, Dist, 0, 0, 0, 1, 0.05");
    }
    if (ImGui::Selectable("Linear Stretch Pair")) {
      BeditSubmitLine(cmd, log, "BPARAM DistNeg, linear");
      BeditSubmitLine(cmd, log, "BPARAM DistPos, linear");
      BeditSubmitLine(cmd, log, "BACTION stretch, DistNeg, 0, 0, 0, -1, 0.05");
      BeditSubmitLine(cmd, log, "BACTION stretch, DistPos, 0, 0, 0, 1, 0.05");
    }
    if (ImGui::Selectable("Flip Set")) {
      BeditSubmitLine(cmd, log, "BPARAM Flip, flip");
      BeditSubmitLine(cmd, log, "BACTION flip, Flip, 0, 0, 1, 0, 0");
    }
    if (ImGui::Selectable("Visibility Set")) {
      BeditSubmitLine(cmd, log, "BPARAM Vis, visibility");
      BeditSubmitLine(cmd, log, "BVISIBILITY VisibilityState0");
    }
    ImGui::TextDisabled("Other sets — not implemented yet.");
  } else {
    ImGui::TextUnformatted("Geometric Constraints");
    ImGui::Separator();
    const char* geo[] = {"Coincident", "Perpendicular", "Parallel", "Tangent", "Horizontal", "Vertical",
                         "Collinear",  "Concentric",    "Smooth",   "Symmetric", "Equal",    "Fix"};
    for (const char* g : geo) {
      ImGui::BeginDisabled();
      ImGui::Selectable(g);
      ImGui::EndDisabled();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Constraint Parameters");
    const char* dim[] = {"Aligned", "Horizontal", "Vertical", "Angular", "Radius"};
    for (const char* d : dim) {
      ImGui::BeginDisabled();
      ImGui::Selectable(d);
      ImGui::EndDisabled();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Geometric constraints are not available in this release.");
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##baptabs", ImVec2(28.f, 0.f), false);
  PaletteTabButton("Par", 0, &cmd.blockAuthoringPaletteTab);
  PaletteTabButton("Act", 1, &cmd.blockAuthoringPaletteTab);
  PaletteTabButton("Set", 2, &cmd.blockAuthoringPaletteTab);
  PaletteTabButton("Con", 3, &cmd.blockAuthoringPaletteTab);
  ImGui::EndChild();

  ImGui::End();
}
