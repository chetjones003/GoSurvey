#include "CadUi.hpp"
#include "CadBlocks.hpp"
#include "AppIcon.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Loads (once, then caches) a resources/icons/<name>.png as an ImGui texture. Returns 0 on failure,
// in which case the caller renders a plain text row. Requires a current GL context (true during UI draw).
static ImTextureID BeditPaletteIcon(const char* name) {
  static std::map<std::string, ImTextureID> cache;
  auto it = cache.find(name);
  if (it != cache.end())
    return it->second;
  ImTextureID tex = 0;
  const std::filesystem::path p =
      ResolveBundledAssetPath(std::filesystem::path("resources") / "icons" / (std::string(name) + ".png"));
  if (!p.empty()) {
    if (unsigned int gl = LoadIconTextureRgba(p))
      tex = static_cast<ImTextureID>(static_cast<intptr_t>(gl));
  }
  cache.emplace(name, tex);
  return tex;
}

// A palette row: 32px icon (from resources/icons/<iconFile>.png) followed by its label. Returns true when clicked.
static bool BeditPaletteRow(const char* label, const char* iconFile) {
  const float sz = 72.f;
  ImGui::PushID(label);
  const bool clicked = ImGui::Selectable("##row", false, ImGuiSelectableFlags_None, ImVec2(0.f, sz + 6.f));
  const ImVec2 p = ImGui::GetItemRectMin();
  if (ImTextureID tex = BeditPaletteIcon(iconFile))
    ImGui::GetWindowDrawList()->AddImage(tex, ImVec2(p.x + 3.f, p.y + 3.f), ImVec2(p.x + 3.f + sz, p.y + 3.f + sz));
  const float th = ImGui::GetTextLineHeight();
  ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + sz + 12.f, p.y + (sz + 6.f - th) * 0.5f),
                                     ImGui::GetColorU32(ImGuiCol_Text), label);
  ImGui::PopID();
  return clicked;
}

// Draws text rotated 90 degrees CCW (reads bottom-to-top), centred in a column of width `colW`
// and height `colH` whose top-left is `mn`. Each glyph quad from the font atlas is rotated in place.
static void BeditVerticalText(const ImVec2& mn, float colW, float colH, const char* s) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFontBaked* baked = ImGui::GetFontBaked();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  // Measure the glyph band (perpendicular to the reading direction) so we can centre it in the column.
  float bandLo = 0.f, bandHi = 0.f, textLen = 0.f;
  for (const char* c = s; *c; ++c) {
    const ImFontGlyph* g = baked->FindGlyph(static_cast<ImWchar>(*c));
    if (!g)
      continue;
    bandLo = std::min(bandLo, g->Y0);
    bandHi = std::max(bandHi, g->Y1);
    textLen += g->AdvanceX;
  }
  ImVec2 p(mn.x + colW * 0.5f - (bandLo + bandHi) * 0.5f - 3.f, mn.y + (colH + textLen) * 0.5f);
  dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
  for (const char* c = s; *c; ++c) {
    const ImFontGlyph* g = baked->FindGlyph(static_cast<ImWchar>(*c));
    if (!g)
      continue;
    if (g->Visible) {
      dl->PrimReserve(6, 4);
      const ImVec2 a(p.x + g->Y0, p.y - g->X0);
      const ImVec2 b(p.x + g->Y0, p.y - g->X1);
      const ImVec2 cc(p.x + g->Y1, p.y - g->X1);
      const ImVec2 d(p.x + g->Y1, p.y - g->X0);
      dl->PrimQuadUV(a, b, cc, d, ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V0), ImVec2(g->U1, g->V1),
                     ImVec2(g->U0, g->V1), col);
    }
    p.y -= g->AdvanceX;
  }
  dl->PopTexture();
}

static void BeditSubmitLine(AppCommandState& cmd, std::vector<std::string>& log, const char* line) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s", line);
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), cmd, log);
}

static void PaletteTabButton(const char* label, int idx, int* tab) {
  const bool on = *tab == idx;
  const ImVec2 size(30.f, ImGui::CalcTextSize(label).x + 24.f);
  ImGui::PushID(idx);
  if (on)
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 78, 88, 255));
  if (ImGui::Button("##tab", size))
    *tab = idx;
  if (on)
    ImGui::PopStyleColor();
  const ImVec2 mn = ImGui::GetItemRectMin();
  BeditVerticalText(mn, size.x, size.y, label);
  ImGui::PopID();
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

  ImGui::BeginChild("##bapbody", ImVec2(-46.f, 0.f), true);
  if (cmd.blockAuthoringPaletteTab == 0) {
    ImGui::TextUnformatted("Parameters");
    ImGui::Separator();
    const char* items[][3] = {
        {"Point", "point", "Block_Authoring_Parameters_Point"},
        {"Linear", "linear", "Block_Authoring_Parameters_Linear"},
        {"Polar", "polar", "Block_Authoring_Parameters_Polar"},
        {"XY", "move", "Block_Authoring_Parameters_XY"},
        {"Rotation", "rotation", "Block_Authoring_Parameters_Rotation"},
        {"Alignment", "linear", "Block_Authoring_Parameters_Alignment"},
        {"Flip", "flip", "Block_Authoring_Parameters_Flip"},
        {"Visibility", "visibility", "Block_Authoring_Parameters_Visibility"},
        {"Lookup", "lookup", "Block_Authoring_Parameters_Lookup"},
        {"Basepoint", "point", "Block_Authoring_Parameters_Base_Point"}};
    for (const auto& it : items) {
      if (BeditPaletteRow(it[0], it[2])) {
        char line[96];
        std::snprintf(line, sizeof(line), "BPARAM %s, %s", it[0], it[1]);
        BeditSubmitLine(cmd, log, line);
      }
    }
  } else if (cmd.blockAuthoringPaletteTab == 1) {
    ImGui::TextUnformatted("Actions");
    ImGui::Separator();
    const char* acts[][3] = {
        {"Move", "move", "Block_Authoring_Actions_Move"},
        {"Scale", "scale", "Block_Authoring_Actions_Scale"},
        {"Stretch", "stretch", "Block_Authoring_Actions_Stretch"},
        {"Polar Stretch", "stretch", "Block_Authoring_Actions_Polar_Stretch"},
        {"Rotate", "rotate", "Block_Authoring_Actions_Rotate"},
        {"Flip", "flip", "Block_Authoring_Actions_Flip"},
        {"Array", "move", "Block_Authoring_Actions_Array"},
        {"Lookup", "lookup", "Block_Authoring_Actions_Lookup"}};
    for (const auto& it : acts) {
      if (BeditPaletteRow(it[0], it[2])) {
        char line[128];
        std::snprintf(line, sizeof(line), "BACTION %s, DistPos", it[1]);
        BeditSubmitLine(cmd, log, line);
      }
    }
    ImGui::BeginDisabled();
    BeditPaletteRow("Block Properties Table", "Block_Authoring_Actions_Table");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Block Properties Table — not implemented yet.");
  } else if (cmd.blockAuthoringPaletteTab == 2) {
    ImGui::TextUnformatted("Parameter Sets");
    ImGui::Separator();
    if (BeditPaletteRow("Point Move", "Block_Authoring_ParameterSets_Point_Move")) {
      BeditSubmitLine(cmd, log, "BPARAM Point, point");
      BeditSubmitLine(cmd, log, "BACTION move, Point");
    }
    if (BeditPaletteRow("Linear Stretch", "Block_Authoring_ParameterSets_Linear_Stretch")) {
      BeditSubmitLine(cmd, log, "BPARAM Dist, linear");
      BeditSubmitLine(cmd, log, "BACTION stretch, Dist, 0, 0, 0, 1, 0.05");
    }
    if (BeditPaletteRow("Linear Stretch Pair", "Block_Authoring_ParameterSets_Linear_Stretch_Pair")) {
      BeditSubmitLine(cmd, log, "BPARAM DistNeg, linear");
      BeditSubmitLine(cmd, log, "BPARAM DistPos, linear");
      BeditSubmitLine(cmd, log, "BACTION stretch, DistNeg, 0, 0, 0, -1, 0.05");
      BeditSubmitLine(cmd, log, "BACTION stretch, DistPos, 0, 0, 0, 1, 0.05");
    }
    if (BeditPaletteRow("Flip Set", "Block_Authoring_ParameterSets_Flip_Set")) {
      BeditSubmitLine(cmd, log, "BPARAM Flip, flip");
      BeditSubmitLine(cmd, log, "BACTION flip, Flip, 0, 0, 1, 0, 0");
    }
    if (BeditPaletteRow("Visibility Set", "Block_Authoring_ParameterSets_Visibility_Set")) {
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
      char icon[96];
      std::snprintf(icon, sizeof(icon), "Block_Authoring_Geometric_Contraints_%s", g);
      ImGui::PushID("geo");
      ImGui::BeginDisabled();
      BeditPaletteRow(g, icon);
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Constraint Parameters");
    const char* dim[] = {"Aligned", "Horizontal", "Vertical", "Angular", "Radius"};
    for (const char* d : dim) {
      char icon[96];
      std::snprintf(icon, sizeof(icon), "Block_Authoring_Contraint_Parameters_%s", d);
      ImGui::PushID("dim");
      ImGui::BeginDisabled();
      BeditPaletteRow(d, icon);
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("Geometric constraints are not available in this release.");
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##baptabs", ImVec2(42.f, 0.f), false);
  PaletteTabButton("Parameters", 0, &cmd.blockAuthoringPaletteTab);
  PaletteTabButton("Actions", 1, &cmd.blockAuthoringPaletteTab);
  PaletteTabButton("Parameter Sets", 2, &cmd.blockAuthoringPaletteTab);
  PaletteTabButton("Constraints", 3, &cmd.blockAuthoringPaletteTab);
  ImGui::EndChild();

  ImGui::End();
}
