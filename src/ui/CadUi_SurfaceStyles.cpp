// Surface Style editor (REQ-070 / ADR-036 (d) (i)).
//
// A style says how a surface is DRAWN — never what it is made of. Nothing in this window touches a
// surface's definition, which is why editing one cannot re-triangulate anything: the style table and
// the triangulation are separate halves of the display cache's staleness key (ADR-036 (e)).
//
// **Which tabs exist, and why the missing ones are missing** (ADR-036 (i), decision D-2026-08-21-a).
// Built: Information, Borders, Contours, Points, Triangles, Analysis, Display, Summary. Absent, each
// for a stated reason rather than an oversight:
//   * **Grid** and **Watersheds** — no requirement behind either, and Watersheds is a drainage-basin
//     *algorithm* rather than a display option, so it would be a SPEC GAP.
//   * **Directions** (REQ-072's own Analysis tab, TASK-086 §2) — no requirement covers aspect/
//     direction analysis, so the section is omitted rather than stubbed (ADR-036 (i)).
//   * **Contour smoothing** — ADR-028 left it undesigned, so the Civil 3D slider is not built.
//   * **Per-component Layer and Plot Style columns** — a surface has exactly one layer (its own
//     `EntityAttributes`) and GoSurvey has no plot-style table. A column that cannot be honoured is
//     worse than an absent one, which is REQ-084's rule about controls that cannot act.
//
// **The interval pair is validated inline and applied on commit** (Q3, answered 2026-08-21). The two
// fields accept any typing — a user replacing "5" with "10" passes through "1", which an
// entry-time refusal would reject mid-keystroke — and an invalid pair shows REQ-070's specific
// message beside them while OK stays disabled. The invalid pair never reaches the style table.

#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "CadUiHelpers.hpp"
#include "CadUiStyleWidgets.hpp"
#include "SurfaceStyle.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string TrimStyleName(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
  return s.substr(b, e - b);
}

/// The colour a component actually resolves to, for the swatch. A component's "ByLayer" defers to
/// the surface's own colour, which may itself be ByLayer — the swatch shows the end of that chain
/// rather than a generic grey, because "ByLayer" that renders green should look green here too.
ImU32 ComponentSwatch(const std::string& colorStorage) {
  for (const auto& p : kNamedColors)
    if (colorStorage == p.storage && colorStorage != "ByLayer")
      return IM_COL32(static_cast<int>(p.r * 255), static_cast<int>(p.g * 255),
                      static_cast<int>(p.b * 255), 255);
  if (colorStorage.size() == 7 && colorStorage[0] == '#') {
    const auto hex = [&](int i) {
      const char c = colorStorage[static_cast<size_t>(i)];
      return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
    };
    return IM_COL32(hex(1) * 16 + hex(2), hex(3) * 16 + hex(4), hex(5) * 16 + hex(6), 255);
  }
  return IM_COL32(150, 150, 150, 255);  // ByLayer: neutral, because it has no colour of its own
}

/// One component's four properties on one row: visible, colour, linetype, lineweight. Returns true
/// when anything changed, so the caller can bump the display cache exactly once per frame.
///
/// The same row draws every component, because a component IS the same four properties whichever one
/// it is — five hand-written blocks would be five places for a picker to fall behind the others.
bool ComponentRow(const char* label, const char* help, SurfaceComponentStyle* c) {
  bool changed = false;
  ImGui::PushID(label);

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  if (ImGui::Checkbox("##vis", &c->visible))
    changed = true;
  ImGui::SameLine();
  ImGui::TextUnformatted(label);
  if (help && *help)
    ItemHelpTooltip(help);

  // --- colour ---
  ImGui::TableNextColumn();
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    dl->AddRectFilled(ImVec2(p.x, p.y + 3.f), ImVec2(p.x + h - 6.f, p.y + h - 3.f),
                      ComponentSwatch(c->color), 2.f);
    dl->AddRect(ImVec2(p.x, p.y + 3.f), ImVec2(p.x + h - 6.f, p.y + h - 3.f),
                IM_COL32(90, 90, 90, 255), 2.f, 0, 1.f);
    ImGui::Dummy(ImVec2(h - 4.f, h));
    ImGui::SameLine();

    const char* preview = c->color.c_str();
    for (const auto& np : kNamedColors)
      if (c->color == np.storage)
        preview = np.label;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##col", preview)) {
      for (const auto& np : kNamedColors) {
        const bool sel = (c->color == np.storage);
        if (ImGui::Selectable(np.label, sel)) {
          c->color = np.storage;
          changed = true;
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  // --- linetype ---
  ImGui::TableNextColumn();
  {
    int li = 0;
    for (int i = 0; i < kEntityLinetypeCount; ++i)
      if (c->linetype == kEntityLinetypeStorage[i])
        li = i;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##lt", kEntityLinetypeLabels[li])) {
      for (int i = 0; i < kEntityLinetypeCount; ++i) {
        const bool sel = (i == li);
        if (ImGui::Selectable(kEntityLinetypeLabels[i], sel)) {
          c->linetype = kEntityLinetypeStorage[i];
          changed = true;
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  // --- lineweight ---
  ImGui::TableNextColumn();
  {
    char prev[64];
    SnprintLineweightPresetLabel(prev, sizeof(prev), c->lineweightMm, false);
    const int wi = LineweightPresetIndexFromMm(c->lineweightMm);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##lw", prev)) {
      for (int i = 0; i < kUiLineweightPresetCount; ++i) {
        char lab[64];
        SnprintLineweightPresetLabel(lab, sizeof(lab), kUiLineweightMmPresets[i], false);
        const bool sel = (i == wi);
        if (ImGui::Selectable(lab, sel)) {
          c->lineweightMm = kUiLineweightMmPresets[i];
          changed = true;
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  ImGui::PopID();
  return changed;
}

/// One row of a REQ-072 range table: the band's upper bound and colour, plus a Remove button. On
/// Remove it writes \p removeIdx rather than erasing directly — the caller owns the vector and must
/// stop iterating it this frame once an erase happens.
bool BandRow(int idx, SurfaceBand* b, int* removeIdx) {
  bool changed = false;
  ImGui::PushID(idx);
  ImGui::TableNextRow();

  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-1);
  double v = b->upperBound;
  if (ImGui::InputDouble("##bound", &v, 0.0, 0.0, "%.4g")) {
    b->upperBound = v;
    changed = true;
  }

  ImGui::TableNextColumn();
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    dl->AddRectFilled(ImVec2(p.x, p.y + 3.f), ImVec2(p.x + h - 6.f, p.y + h - 3.f), ComponentSwatch(b->color), 2.f);
    dl->AddRect(ImVec2(p.x, p.y + 3.f), ImVec2(p.x + h - 6.f, p.y + h - 3.f), IM_COL32(90, 90, 90, 255), 2.f, 0, 1.f);
    ImGui::Dummy(ImVec2(h - 4.f, h));
    ImGui::SameLine();
    const char* preview = b->color.c_str();
    for (const auto& np : kNamedColors)
      if (b->color == np.storage)
        preview = np.label;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##col", preview)) {
      for (const auto& np : kNamedColors) {
        const bool sel = (b->color == np.storage);
        if (ImGui::Selectable(np.label, sel)) {
          b->color = np.storage;
          changed = true;
        }
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }

  ImGui::TableNextColumn();
  if (ImGui::SmallButton("Remove"))
    *removeIdx = idx;

  ImGui::PopID();
  return changed;
}

/// A REQ-072 range table editor: rows of (upper bound, colour). Sorted back into strictly-ascending
/// order after every edit — the same repair `GsIo` applies to a hand-edited or corrupt `.gs` on load
/// (TASK-086 §8) — so `AssignBand`'s precondition (`util/surfaceanalysis.hpp`) holds no matter what
/// order the user typed bounds in, without the table ever refusing an in-progress edit.
///
/// \p boundLabel names the bound column's header: which quantity is being bounded, and its unit, is
/// the caller's to know (elevation in feet, or grade in percent) — this editor is the same widget
/// either way, on \ref SurfaceStyle::bands or \ref SurfaceStyle::arrowBands.
bool BandTableEditor(const char* tableId, const char* boundLabel, std::vector<SurfaceBand>* bands) {
  bool changed = false;
  if (ImGui::BeginTable(tableId, 3,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn(boundLabel, ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.f);
    ImGui::TableHeadersRow();
    int removeIdx = -1;
    for (size_t i = 0; i < bands->size(); ++i)
      changed |= BandRow(static_cast<int>(i), &(*bands)[i], &removeIdx);
    ImGui::EndTable();
    if (removeIdx >= 0) {
      bands->erase(bands->begin() + removeIdx);
      changed = true;
    }
  }
  if (ImGui::SmallButton("Add band")) {
    SurfaceBand nb;
    nb.upperBound = bands->empty() ? 0.0 : bands->back().upperBound + 1.0;
    bands->push_back(nb);
    changed = true;
  }
  if (changed)
    std::sort(bands->begin(), bands->end(),
             [](const SurfaceBand& a, const SurfaceBand& b) { return a.upperBound < b.upperBound; });
  return changed;
}

/// A component table with the four column headers, so each tab's single row lines up with every
/// other tab's.
bool BeginComponentTable(const char* id) {
  if (!ImGui::BeginTable(id, 4,
                         ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV |
                             ImGuiTableFlags_RowBg))
    return false;
  ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthStretch, 1.5f);
  ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthStretch, 1.4f);
  ImGui::TableSetupColumn("Linetype", ImGuiTableColumnFlags_WidthStretch, 1.2f);
  ImGui::TableSetupColumn("Lineweight", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  ImGui::TableHeadersRow();
  return true;
}

/// How many surfaces draw with \p name, counting the ones that resolve to it by falling back.
int SurfacesUsing(const AppCommandState& cmd, const std::string& name) {
  int n = 0;
  for (const CadSurface& s : cmd.cadSurfaces) {
    const SurfaceStyle* r = SurfaceStyles::Resolve(cmd.surfaceStyles, s.styleName);
    if (r && r->name == name)
      ++n;
  }
  return n;
}

} // namespace

void DrawSurfaceStyleWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  SurfaceStyles::EnsureStandard(cmd.surfaceStyles);
  if (!cmd.showSurfaceStyleWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
  bool open = cmd.showSurfaceStyleWindow;
  const char* title = cmd.surfaceStyleUseSurfacesTitle ? "Surfaces" : "Surface Style";
  if (!ImGui::Begin(title, &open)) {
    cmd.showSurfaceStyleWindow = open;
    if (!open)
      cmd.surfaceStyleUseSurfacesTitle = false;
    ImGui::End();
    return;
  }
  cmd.showSurfaceStyleWindow = open;
  if (!open)
    cmd.surfaceStyleUseSurfacesTitle = false;

  static int selIdx = 0;
  if (!cmd.surfaceStyleEditorFocusName.empty()) {
    for (size_t i = 0; i < cmd.surfaceStyles.size(); ++i) {
      if (cmd.surfaceStyles[i].name == cmd.surfaceStyleEditorFocusName) {
        selIdx = static_cast<int>(i);
        break;
      }
    }
    cmd.surfaceStyleEditorFocusName.clear();
  }
  if (selIdx < 0 || selIdx >= static_cast<int>(cmd.surfaceStyles.size()))
    selIdx = 0;

  bool changed = false;
  int deleteIdx = -1;
  const float footer = ImGui::GetFrameHeightWithSpacing() + 12.f;

  // ── Left: the style list ───────────────────────────────────────────────────────────────────────
  ImGui::BeginChild("##sstleft", ImVec2(190.f, -footer), false);
  ImGui::TextUnformatted("Styles:");
  ImGui::BeginChild("##sstlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.f - 6.f), true);
  for (size_t i = 0; i < cmd.surfaceStyles.size(); ++i) {
    const bool sel = (static_cast<int>(i) == selIdx);
    if (ImGui::Selectable(cmd.surfaceStyles[i].name.c_str(), sel))
      selIdx = static_cast<int>(i);
    const int used = SurfacesUsing(cmd, cmd.surfaceStyles[i].name);
    if (used > 0 && ImGui::IsItemHovered())
      ImGui::SetTooltip("%d surface(s) draw with this style.", used);
  }
  ImGui::EndChild();

  if (ImGui::Button("New...", ImVec2(-1, 0)))
    ImGui::OpenPopup("New Surface Style");

  SurfaceStyle& s = cmd.surfaceStyles[static_cast<size_t>(selIdx)];
  const bool isStandard = (s.name == SurfaceStyles::kStandardName);
  ImGui::BeginDisabled(isStandard);
  if (ImGui::Button("Delete", ImVec2(-1, 0)))
    deleteIdx = selIdx;
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    // Deliberately NOT disabled for a style in use, unlike the text-style manager: REQ-070 makes the
    // deleted-style fallback an acceptance condition, so it has to be reachable.
    if (isStandard)
      ImGui::SetTooltip("Standard cannot be deleted — it is what an unresolved style name falls back to.");
    else if (SurfacesUsing(cmd, s.name) > 0)
      ImGui::SetTooltip("Surfaces using this style will fall back to Standard. They keep the name, so\n"
                        "re-creating a style called \"%s\" adopts them back.", s.name.c_str());
  }
  ImGui::EndChild();

  ImGui::SameLine();

  // ── Right: the tabs ────────────────────────────────────────────────────────────────────────────
  ImGui::BeginChild("##sstright", ImVec2(0, -footer), false);

  // The interval pair is edited through scratch values, so an in-progress "1" on the way to "10"
  // never reaches the style. Re-seeded whenever the selection moves — comparing against the style is
  // what makes an external edit (undo, a SURFSTYLE command, reopening a file) show up here.
  static int scratchFor = -1;
  static double scratchMinor = 2.0;
  static double scratchMajor = 10.0;
  static std::string scratchName;
  if (scratchFor != selIdx || scratchName != s.name) {
    scratchFor = selIdx;
    scratchName = s.name;
    scratchMinor = s.minorIntervalFt;
    scratchMajor = s.majorIntervalFt;
  }

  std::string intervalWhy;
  const bool intervalsOk = SurfaceStyles::IntervalsCompatible(scratchMinor, scratchMajor, &intervalWhy);

  if (ImGui::BeginTabBar("##sststabs")) {
    if (ImGui::BeginTabItem("Information")) {
      ImGui::Spacing();
      ImGui::TextUnformatted("Name");
      ImGui::SetNextItemWidth(-1);
      ImGui::BeginDisabled(isStandard);
      std::string nameBuf = s.name;
      if (ImGui::InputText("##sstname", &nameBuf, ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string nm = TrimStyleName(nameBuf);
        if (nm.empty())
          log->push_back("A surface style name cannot be empty — keeping \"" + s.name + "\".");
        else if (SurfaceStyles::Find(cmd.surfaceStyles, nm) && nm != s.name)
          log->push_back("A surface style named \"" + nm + "\" already exists — rename refused.");
        else {
          PushUndoSnapshot(cmd, "Rename surface style");
          // Every surface referencing the old name is re-pointed, so a rename does not silently
          // orphan them into the Standard fallback.
          for (CadSurface& surf : cmd.cadSurfaces)
            if (surf.styleName == s.name)
              surf.styleName = nm;
          log->push_back("Renamed surface style \"" + s.name + "\" to \"" + nm + "\".");
          s.name = nm;
          changed = true;
        }
      }
      ImGui::EndDisabled();
      if (isStandard)
        ImGui::TextDisabled("Standard cannot be renamed.");

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Text("%d surface(s) draw with this style.", SurfacesUsing(cmd, s.name));
      ImGui::TextWrapped(
          "Editing a style changes every surface using it. To change one surface only, open this "
          "editor from that surface's Edit... button in the Surfaces panel (it copies the style "
          "first when others still share it). Contours, the border and the triangle network are "
          "generated from the triangulation each time the style changes — they are never objects "
          "in the drawing, are never saved to the file, and never appear in a selection. Use "
          "EXTRACT to turn the displayed contours into real polylines.");
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Contours")) {
      ImGui::Spacing();
      ImGui::SeparatorText("Interval");
      ImGui::SetNextItemWidth(140.f);
      ImGui::InputDouble("Minor interval (ft)", &scratchMinor, 0.0, 0.0, "%.4g");
      ImGui::SetNextItemWidth(140.f);
      ImGui::InputDouble("Major interval (ft)", &scratchMajor, 0.0, 0.0, "%.4g");

      if (!intervalsOk) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.45f, 0.35f, 1.f));
        ImGui::TextWrapped("%s", intervalWhy.c_str());
        ImGui::PopStyleColor();
      } else if (scratchMinor != s.minorIntervalFt || scratchMajor != s.majorIntervalFt) {
        // Valid and different: commit. This is the "apply on commit" half of Q3 — the table only
        // ever holds a pair that passed the rule.
        PushUndoSnapshot(cmd, "Surface style interval");
        s.minorIntervalFt = scratchMinor;
        s.majorIntervalFt = scratchMajor;
        changed = true;
      }

      ImGui::Spacing();
      ImGui::SeparatorText("Appearance");
      if (BeginComponentTable("##contourcomp")) {
        changed |= ComponentRow("Minor contour", "Drawn at every whole multiple of the minor interval "
                                                 "that is not also a major contour.", &s.minorContour);
        changed |= ComponentRow("Major contour", "Drawn at every whole multiple of the major interval. "
                                                 "This is the line a plan reader takes an elevation from, "
                                                 "so it is usually the heavier one.", &s.majorContour);
        ImGui::EndTable();
      }
      ImGui::Spacing();
      ImGui::SetNextItemWidth(80.f);
      if (ImGui::SliderInt("Smooth passes", &s.contourSmoothPasses, 0, 5))
        changed = true;
      ItemHelpTooltip("Chaikin corner-cutting on displayed contours only (0–5). Does not change the TIN.");
      ImGui::SetNextItemWidth(120.f);
      if (ImGui::InputDouble("Label spacing (ft)", &s.contourLabelSpacingFt, 0.0, 0.0, "%.2f")) {
        if (s.contourLabelSpacingFt < 0.0)
          s.contourLabelSpacingFt = 0.0;
        changed = true;
      }
      ItemHelpTooltip("Spacing along major contours. 0 turns labels off. Labels are overlay text, not entities.");
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Triangles")) {
      ImGui::Spacing();
      if (BeginComponentTable("##tricomp")) {
        changed |= ComponentRow("Triangles",
                                "Every edge of the triangulation. Off by default — the triangulation "
                                "is the model, the contours are the drawing.", &s.triangles);
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Borders")) {
      ImGui::Spacing();
      if (BeginComponentTable("##bordercomp")) {
        changed |= ComponentRow("Border",
                                "The outline of the triangulated area, including the rim of any hole "
                                "a hide boundary leaves. Not the convex hull.", &s.border);
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Points")) {
      ImGui::Spacing();
      if (BeginComponentTable("##ptcomp")) {
        changed |= ComponentRow("Points",
                                "The surface's triangulation vertices. Off by default: the source "
                                "points are already drawn as survey points.", &s.points);
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Analysis")) {
      ImGui::Spacing();
      ImGui::SeparatorText("Banding");
      static const char* kModeLabels[] = {"None", "Elevation", "Slope", "Direction", "Slope angle"};
      int modeIdx = static_cast<int>(s.analysisMode);
      ImGui::SetNextItemWidth(180.f);
      if (ImGui::Combo("Type", &modeIdx, kModeLabels, 5)) {
        s.analysisMode = static_cast<SurfaceAnalysisMode>(modeIdx);
        changed = true;
      }
      ItemHelpTooltip("Colours every triangle by its elevation or its slope, driven by the range "
                      "table below. A triangle takes ONE colour — its centroid elevation, or its "
                      "plane's grade — never a gradient across it.");
      if (s.analysisMode != SurfaceAnalysisMode::None) {
        ImGui::Spacing();
        const char* boundLabel =
            s.analysisMode == SurfaceAnalysisMode::Elevation ? "Upper bound (ft)"
            : s.analysisMode == SurfaceAnalysisMode::Direction ? "Upper bound (deg)"
            : s.analysisMode == SurfaceAnalysisMode::SlopeAngle ? "Upper bound (deg)"
                                                               : "Upper bound (%)";
        changed |= BandTableEditor("##bandtable", boundLabel, &s.bands);
        ImGui::Spacing();
        if (s.bands.empty())
          ImGui::TextDisabled("No bands: every triangle draws in the plain Triangles colour.");
        else
          ImGui::TextDisabled(
              "A value above the last bound draws in the plain Triangles colour, not the top band.");
      }

      ImGui::Spacing();
      ImGui::SeparatorText("Slope Arrows");
      changed |= ImGui::Checkbox("Show slope arrows", &s.slopeArrowsOn);
      ItemHelpTooltip("One arrow per triangle, pointing downhill, coloured by grade below. A "
                      "triangle flatter than a 0.1% grade draws no arrow.");
      if (s.slopeArrowsOn) {
        ImGui::Spacing();
        changed |= BandTableEditor("##arrowtable", "Upper bound (%)", &s.arrowBands);
        ImGui::Spacing();
        if (s.arrowBands.empty())
          ImGui::TextDisabled("No bands: every arrow draws in the surface's own colour.");
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Display")) {
      ImGui::Spacing();
      ImGui::TextWrapped("Every component of this style, in one place. The per-tab pages above edit "
                         "the same values.");
      ImGui::Spacing();
      if (BeginComponentTable("##allcomp")) {
        changed |= ComponentRow("Triangles", nullptr, &s.triangles);
        changed |= ComponentRow("Border", nullptr, &s.border);
        changed |= ComponentRow("Minor contour", nullptr, &s.minorContour);
        changed |= ComponentRow("Major contour", nullptr, &s.majorContour);
        changed |= ComponentRow("Points", nullptr, &s.points);
        ImGui::EndTable();
      }
      ImGui::Spacing();
      ImGui::TextDisabled("Layer and Plot Style are not listed: a surface has exactly one layer (its "
                          "own), and GoSurvey has no plot style table.");
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Summary")) {
      ImGui::Spacing();
      ImGui::Text("Style: %s", s.name.c_str());
      ImGui::Text("Contour interval: minor %s ft, major %s ft",
                  SurfaceStyles::FormatFt(s.minorIntervalFt).c_str(),
                  SurfaceStyles::FormatFt(s.majorIntervalFt).c_str());
      ImGui::Spacing();
      if (ImGui::BeginTable("##summary", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Component");
        ImGui::TableSetupColumn("Shown");
        ImGui::TableHeadersRow();
        const std::pair<const char*, const SurfaceComponentStyle*> rows[] = {
            {"Triangles", &s.triangles},         {"Border", &s.border},
            {"Minor contour", &s.minorContour},  {"Major contour", &s.majorContour},
            {"Points", &s.points},
        };
        for (const auto& r : rows) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(r.first);
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(r.second->visible ? "yes" : "no");
        }
        ImGui::EndTable();
      }
      ImGui::Spacing();
      ImGui::Text("%d surface(s) draw with this style.", SurfacesUsing(cmd, s.name));
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::EndChild();

  // ── New-style modal ────────────────────────────────────────────────────────────────────────────
  if (ImGui::BeginPopupModal("New Surface Style", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    static std::string newName = "Surface Style 1";
    ImGui::TextUnformatted("Style name:");
    ImGui::SetNextItemWidth(260.f);
    ImGui::InputText("##sstnewname", &newName);
    const std::string nm = TrimStyleName(newName);
    const bool dup = !nm.empty() && SurfaceStyles::Find(cmd.surfaceStyles, nm) != nullptr;
    if (dup)
      ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "A style with that name already exists.");
    ImGui::TextDisabled("Copied from the selected style.");
    ImGui::Spacing();
    ImGui::BeginDisabled(nm.empty() || dup);
    if (ImGui::Button("OK", ImVec2(110.f, 0.f))) {
      PushUndoSnapshot(cmd, "Add surface style");
      SurfaceStyle ns = s;
      ns.name = nm;
      cmd.surfaceStyles.push_back(ns);
      selIdx = static_cast<int>(cmd.surfaceStyles.size()) - 1;
      log->push_back("Surface style added: " + nm);
      changed = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110.f, 0.f)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // ── Footer ─────────────────────────────────────────────────────────────────────────────────────
  ImGui::Separator();
  const float bw = 100.f;
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (bw + 8.f) * 2.f);
  ImGui::BeginDisabled(!intervalsOk);
  if (ImGui::Button("OK", ImVec2(bw, 0.f))) {
    BumpCadGpuCache(cmd);
    cmd.showSurfaceStyleWindow = false;
    cmd.surfaceStyleUseSurfacesTitle = false;
  }
  ImGui::EndDisabled();
  if (!intervalsOk && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", intervalWhy.c_str());
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(bw, 0.f))) {
    cmd.showSurfaceStyleWindow = false;
    cmd.surfaceStyleUseSurfacesTitle = false;
  }

  if (deleteIdx >= 0 && deleteIdx < static_cast<int>(cmd.surfaceStyles.size())) {
    PushUndoSnapshot(cmd, "Delete surface style");
    const std::string nm = cmd.surfaceStyles[static_cast<size_t>(deleteIdx)].name;
    const int used = SurfacesUsing(cmd, nm);
    cmd.surfaceStyles.erase(cmd.surfaceStyles.begin() + deleteIdx);
    if (selIdx >= static_cast<int>(cmd.surfaceStyles.size()))
      selIdx = static_cast<int>(cmd.surfaceStyles.size()) - 1;
    // Surfaces keep their styleName on purpose (REQ-070): they fall back to Standard for now, and
    // re-creating a style with that name adopts them back rather than leaving the reference rewritten.
    log->push_back("Surface style deleted: " + nm +
                   (used > 0 ? ". " + std::to_string(used) +
                                   " surface(s) using it now draw with \"Standard\"."
                             : std::string()));
    changed = true;
  }

  if (changed)
    BumpCadGpuCache(cmd);

  ImGui::End();
}
