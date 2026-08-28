#include "CadUi.hpp"

#include "CadCommands.hpp"
#include "SurfaceStyle.hpp"
#include "util/surfacestats.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* kBandPalette[] = {"#3148F5", "#31A2F5", "#31F5C8", "#7CF531", "#F5E631", "#F5A031", "#F53131",
                              "#C831F5"};

bool SurfacePlanBounds(const CadSurface& s, float* mnX, float* mxX, float* mnY, float* mxY) {
  if (!s.tin || s.tin->vertsXyz.size() < 3)
    return false;
  *mnX = *mxX = s.tin->vertsXyz[0];
  *mnY = *mxY = s.tin->vertsXyz[1];
  for (size_t i = 0; i + 2 < s.tin->vertsXyz.size(); i += 3) {
    *mnX = std::min(*mnX, s.tin->vertsXyz[i]);
    *mxX = std::max(*mxX, s.tin->vertsXyz[i]);
    *mnY = std::min(*mnY, s.tin->vertsXyz[i + 1]);
    *mxY = std::max(*mxY, s.tin->vertsXyz[i + 1]);
  }
  return true;
}

bool SurfaceElevRange(const CadSurface& s, double* lo, double* hi) {
  if (!s.tin || s.tin->vertsXyz.size() < 3)
    return false;
  *lo = *hi = static_cast<double>(s.tin->vertsXyz[2]);
  for (size_t i = 2; i < s.tin->vertsXyz.size(); i += 3) {
    const double z = static_cast<double>(s.tin->vertsXyz[i]);
    *lo = std::min(*lo, z);
    *hi = std::max(*hi, z);
  }
  return true;
}

template <typename T>
void MoveSameKind(std::vector<T>* v, size_t i, int delta) {
  const long long j = static_cast<long long>(i) + delta;
  if (j < 0 || j >= static_cast<long long>(v->size()))
    return;
  std::swap((*v)[i], (*v)[static_cast<size_t>(j)]);
}

void ApplyInfo(AppCommandState& cmd, int si, const std::string& name, const std::string& description,
               const std::string& styleName, std::vector<std::string>& log) {
  if (si < 0 || si >= static_cast<int>(cmd.cadSurfaces.size()))
    return;
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(si)];
  const int clash = FindSurfaceIndex(cmd, name);
  if (name.empty()) {
    log.push_back("Surface name cannot be empty — keeping \"" + s.name + "\".");
    return;
  }
  if (clash >= 0 && clash != si) {
    log.push_back("A surface named \"" + name + "\" already exists — rename refused.");
    return;
  }
  PushUndoSnapshot(cmd, "Surface properties");
  s.name = name;
  s.description = description;
  s.styleName = styleName;
  BumpCadGpuCache(cmd);
}

void DrawInformationTab(AppCommandState& cmd, std::string* name, std::string* description, std::string* styleName) {
  ImGui::Spacing();
  ImGui::TextUnformatted("Name:");
  ImGui::SetNextItemWidth(-1.f);
  ImGui::InputText("##spname", name);
  ImGui::Spacing();
  ImGui::TextUnformatted("Description:");
  ImGui::InputTextMultiline("##spdesc", description, ImVec2(-1.f, 88.f));
  ImGui::Spacing();
  ImGui::SeparatorText("Default styles");
  ImGui::TextUnformatted("Surface style:");
  ImGui::SetNextItemWidth(-80.f);
  const char* preview = styleName->empty() ? SurfaceStyles::kStandardName : styleName->c_str();
  if (ImGui::BeginCombo("##spstyle", preview)) {
    for (const SurfaceStyle& st : cmd.surfaceStyles) {
      const bool sel = (*styleName == st.name);
      if (ImGui::Selectable(st.name.c_str(), sel))
        *styleName = st.name;
      if (sel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Edit...")) {
    cmd.surfaceStyleEditorFocusName = styleName->empty() ? std::string(SurfaceStyles::kStandardName) : *styleName;
    cmd.surfaceStyleUseSurfacesTitle = false;
    cmd.showSurfaceStyleWindow = true;
  }
}

void DrawDefinitionTab(AppCommandState& cmd, CadSurface& s) {
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(1.f, 0.97f, 0.82f, 1.f));
  if (ImGui::BeginTable("##spdefopts", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Definition Options", ImGuiTableColumnFlags_WidthStretch, 0.45f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
    ImGui::TableHeadersRow();
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Build");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("automatic");
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Data operations");
    ImGui::TableNextColumn();
    ImGui::Text("%d group(s), %d file(s), %d breakline(s)", static_cast<int>(s.sourcePointGroups.size()),
                static_cast<int>(s.sourcePointFiles.size()), static_cast<int>(s.breaklines.size()));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Edit operations");
    ImGui::TableNextColumn();
    ImGui::Text("%d edge swap(s)", static_cast<int>(s.swappedEdgePicks.size()));
    ImGui::EndTable();
  }
  ImGui::PopStyleColor();

  struct Row {
    const char* type;
    std::string param;
    int kind;
    size_t index;
  };
  std::vector<Row> rows;
  for (size_t i = 0; i < s.sourcePointGroups.size(); ++i)
    rows.push_back({"Point Group", s.sourcePointGroups[i], 0, i});
  for (size_t i = 0; i < s.sourcePointFiles.size(); ++i)
    rows.push_back({"Point File", s.sourcePointFiles[i].path, 1, i});
  for (size_t i = 0; i < s.breaklines.size(); ++i)
    rows.push_back({"Breakline",
                    s.breaklines[i].description.empty() ? ("#" + std::to_string(i + 1)) : s.breaklines[i].description, 2,
                    i});
  for (size_t i = 0; i < s.contourSources.size(); ++i)
    rows.push_back({"Contour",
                    s.contourSources[i].description.empty() ? ("#" + std::to_string(i + 1))
                                                            : s.contourSources[i].description,
                    3, i});
  for (size_t i = 0; i < s.boundaries.size(); ++i) {
    const char* t = s.boundaries[i].kind == CadBoundaryKind::Mask ? "Mask" : "Boundary";
    rows.push_back({t, s.boundaries[i].name.empty() ? ("#" + std::to_string(i + 1)) : s.boundaries[i].name, 4, i});
  }
  for (size_t i = 0; i < s.swappedEdgePicks.size(); ++i)
    rows.push_back({"Edit", "Edge swap #" + std::to_string(i + 1), 5, i});

  static int selOp = 0;
  if (selOp >= static_cast<int>(rows.size()))
    selOp = static_cast<int>(rows.size()) - 1;
  if (selOp < 0)
    selOp = 0;

  auto canMove = [&](int delta) {
    if (rows.empty())
      return false;
    const int j = selOp + delta;
    if (j < 0 || j >= static_cast<int>(rows.size()))
      return false;
    return rows[static_cast<size_t>(selOp)].kind == rows[static_cast<size_t>(j)].kind;
  };
  auto applyMove = [&](int delta) {
    if (!canMove(delta))
      return;
    const Row r = rows[static_cast<size_t>(selOp)];
    PushUndoSnapshot(cmd, "Reorder surface definition");
    if (r.kind == 0)
      MoveSameKind(&s.sourcePointGroups, r.index, delta);
    else if (r.kind == 1)
      MoveSameKind(&s.sourcePointFiles, r.index, delta);
    else if (r.kind == 2)
      MoveSameKind(&s.breaklines, r.index, delta);
    else if (r.kind == 3)
      MoveSameKind(&s.contourSources, r.index, delta);
    else if (r.kind == 4)
      MoveSameKind(&s.boundaries, r.index, delta);
    else
      MoveSameKind(&s.swappedEdgePicks, r.index, delta);
    BumpCadGpuCache(cmd);
    selOp += delta;
  };

  ImGui::Spacing();
  ImGui::TextUnformatted("Operations");
  const float btnW = 40.f;
  ImGui::BeginChild("##spdefbtns", ImVec2(btnW + 4.f, 200.f), false);
  ImGui::BeginDisabled(!canMove(-1));
  if (ImGui::Button("^^##sptop", ImVec2(btnW, 0.f)))
    applyMove(-1);
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!canMove(-1));
  if (ImGui::Button("^##spup", ImVec2(btnW, 0.f)))
    applyMove(-1);
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!canMove(1));
  if (ImGui::Button("v##spdn", ImVec2(btnW, 0.f)))
    applyMove(1);
  ImGui::EndDisabled();
  ImGui::BeginDisabled(!canMove(1));
  if (ImGui::Button("vv##spbot", ImVec2(btnW, 0.f)))
    applyMove(1);
  ImGui::EndDisabled();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##spdeflist", ImVec2(0.f, 200.f), true);
  if (ImGui::BeginTable("##spops", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
    ImGui::TableSetupColumn("Operation Type");
    ImGui::TableSetupColumn("Parameters");
    ImGui::TableHeadersRow();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      const bool sel = (i == selOp);
      if (ImGui::Selectable(rows[static_cast<size_t>(i)].type, sel, ImGuiSelectableFlags_SpanAllColumns))
        selOp = i;
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(rows[static_cast<size_t>(i)].param.c_str());
    }
    ImGui::EndTable();
  }
  if (rows.empty())
    ImGui::TextDisabled("(no definition items — add them from Toolspace)");
  ImGui::EndChild();
}

void FillElevationBands(SurfaceStyle* st, const CadSurface& s, int count) {
  double lo = 0.0, hi = 0.0;
  if (!SurfaceElevRange(s, &lo, &hi) || count < 1)
    return;
  if (hi <= lo)
    hi = lo + 1.0;
  st->analysisMode = SurfaceAnalysisMode::Elevation;
  st->bands.clear();
  const int n = std::min(count, 64);
  for (int i = 1; i <= n; ++i) {
    SurfaceBand b;
    b.upperBound = lo + (hi - lo) * (static_cast<double>(i) / static_cast<double>(n));
    b.color = kBandPalette[static_cast<size_t>((i - 1) % 8)];
    st->bands.push_back(b);
  }
}

void DrawAnalysisTab(AppCommandState& cmd, int si, CadSurface& s, std::vector<std::string>* log) {
  SurfaceStyles::EnsureStandard(cmd.surfaceStyles);
  SurfaceStyle* st = SurfaceStyles::Find(cmd.surfaceStyles, s.styleName);
  if (st == nullptr)
    st = &SurfaceStyles::EnsureStandard(cmd.surfaceStyles);

  const float comboExtra = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 4.f;
  const auto comboW = [&](const char* longest) {
    return ImGui::CalcTextSize(longest).x + comboExtra;
  };

  ImGui::Spacing();
  if (ImGui::BeginTable("##spanalysis", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("cfg", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    ImGui::TableSetupColumn("prev", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Analysis type:");
    ImGui::SameLine();
    static const char* kModes[] = {"None", "Elevations", "Slope", "Direction", "Slope angle"};
    int modeIdx = static_cast<int>(st->analysisMode);
    ImGui::SetNextItemWidth(comboW("Slope angle"));
    if (ImGui::Combo("##spatype", &modeIdx, kModes, 5)) {
      DetachSurfaceStyleIfShared(cmd, static_cast<size_t>(si), log);
      st = SurfaceStyles::Find(cmd.surfaceStyles, cmd.cadSurfaces[static_cast<size_t>(si)].styleName);
      if (st == nullptr) {
        ImGui::EndTable();
        return;
      }
      PushUndoSnapshot(cmd, "Surface analysis");
      st->analysisMode = static_cast<SurfaceAnalysisMode>(modeIdx);
      BumpCadGpuCache(cmd);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Ranges");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Create ranges by:");
    ImGui::SetNextItemWidth(-1.f);
    ImGui::BeginDisabled();
    if (ImGui::BeginCombo("##sprangemode", "Number of ranges"))
      ImGui::EndCombo();
    ImGui::EndDisabled();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Range count:");
    ImGui::SameLine();
    static int rangeCount = 8;
    ImGui::SetNextItemWidth(comboW("64") + ImGui::GetFrameHeight() * 2.f);
    ImGui::InputInt("##sprangecount", &rangeCount);
    if (rangeCount < 1)
      rangeCount = 1;
    if (rangeCount > 64)
      rangeCount = 64;

    ImGui::BeginDisabled();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Datum elevation:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(comboW("0.000000"));
    float datum = 0.f;
    ImGui::InputFloat("##spdatum", &datum, 0.f, 0.f, "%.6f");
    ImGui::EndDisabled();

    if (ImGui::Button("Run", ImVec2(88.f, 0.f))) {
      DetachSurfaceStyleIfShared(cmd, static_cast<size_t>(si), log);
      st = SurfaceStyles::Find(cmd.surfaceStyles, cmd.cadSurfaces[static_cast<size_t>(si)].styleName);
      if (st != nullptr) {
        PushUndoSnapshot(cmd, "Surface analysis ranges");
        FillElevationBands(st, s, rangeCount);
        BumpCadGpuCache(cmd);
      }
    }

    ImGui::TableNextColumn();
    ImGui::BeginDisabled();
    bool preview = false;
    ImGui::Checkbox("Preview", &preview);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 1.f));
    ImGui::BeginChild("##sppreview", ImVec2(-1.f, 160.f), true);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::SeparatorText("Range Details");
  if (ImGui::BeginTable("##spbands", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                        ImVec2(0.f, 160.f))) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 48.f);
    ImGui::TableSetupColumn("Upper bound", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < st->bands.size(); ++i) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%d", static_cast<int>(i + 1));
      ImGui::TableNextColumn();
      ImGui::Text("%.4g", st->bands[i].upperBound);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(st->bands[i].color.c_str());
    }
    ImGui::EndTable();
  }
}

void DrawStatisticsTab(const CadSurface& s) {
  ImGui::Spacing();
  const SurfaceStats stt =
      s.tin ? ComputeSurfaceStats(s.tin->vertsXyz, s.tin->indices, static_cast<int>(s.breaklines.size()),
                                  s.isVolumeSurface())
            : SurfaceStats{};
  if (!stt.built) {
    ImGui::TextDisabled("Not built. Add definition items from Toolspace, then Rebuild.");
    if (!s.lastBuildMessage.empty())
      ImGui::TextWrapped("%s", s.lastBuildMessage.c_str());
    return;
  }
  if (ImGui::BeginTable("##spstats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Property");
    ImGui::TableSetupColumn("Value");
    ImGui::TableHeadersRow();
    const auto row = [](const char* k, const std::string& v) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(k);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(v.c_str());
    };
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%d", stt.points);
    row("Points", buf);
    std::snprintf(buf, sizeof(buf), "%d", stt.triangles);
    row("Triangles", buf);
    std::snprintf(buf, sizeof(buf), "%d", stt.uniqueEdges);
    row("Edges", buf);
    std::snprintf(buf, sizeof(buf), "%d", stt.breaklineEdges);
    row("Breakline edges", buf);
    std::snprintf(buf, sizeof(buf), "%.3f to %.3f", stt.minX, stt.maxX);
    row("X range", buf);
    std::snprintf(buf, sizeof(buf), "%.3f to %.3f", stt.minY, stt.maxY);
    row("Y range", buf);
    std::snprintf(buf, sizeof(buf), "%.3f to %.3f", stt.minZ, stt.maxZ);
    row("Elevation", buf);
    std::snprintf(buf, sizeof(buf), "%.3f", stt.area2d);
    row("2D area", buf);
    std::snprintf(buf, sizeof(buf), "%.3f", stt.area3d);
    row("3D area", buf);
    std::snprintf(buf, sizeof(buf), "%.2f %% to %.2f %% (mean %.2f %%)", stt.minSlopePct, stt.maxSlopePct,
                  stt.meanSlopePct);
    row("Slope", buf);
    std::snprintf(buf, sizeof(buf), "%.2f to %.2f (mean %.2f)", stt.minSlopeDeg, stt.maxSlopeDeg, stt.meanSlopeDeg);
    row("Slope (deg)", buf);
    if (s.isVolumeSurface()) {
      std::snprintf(buf, sizeof(buf), "cut %.3f  fill %.3f", stt.volumeCutFt3, stt.volumeFillFt3);
      row("Volume (ft3)", buf);
    }
    ImGui::EndTable();
  }
}

}  // namespace

void DrawSurfacePropertiesWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (log == nullptr)
    log = &discard;
  if (!cmd.showSurfacePropertiesWindow)
    return;
  if (cmd.surfacePropertiesIndex < 0 ||
      cmd.surfacePropertiesIndex >= static_cast<int>(cmd.cadSurfaces.size())) {
    cmd.showSurfacePropertiesWindow = false;
    return;
  }

  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(cmd.surfacePropertiesIndex)];
  static int loadedFor = -1;
  static std::string name;
  static std::string description;
  static std::string styleName;
  if (loadedFor != cmd.surfacePropertiesIndex) {
    loadedFor = cmd.surfacePropertiesIndex;
    name = s.name;
    description = s.description;
    styleName = s.styleName.empty() ? std::string(SurfaceStyles::kStandardName) : s.styleName;
  }

  const std::string title = "Surface Properties - " + s.name;
  ImGui::SetNextWindowSize(ImVec2(640.f, 520.f), ImGuiCond_FirstUseEver);
  bool open = cmd.showSurfacePropertiesWindow;
  if (!ImGui::Begin(title.c_str(), &open)) {
    cmd.showSurfacePropertiesWindow = open;
    ImGui::End();
    return;
  }
  cmd.showSurfacePropertiesWindow = open;
  if (!open) {
    loadedFor = -1;
    ImGui::End();
    return;
  }

  const float footer = ImGui::GetFrameHeightWithSpacing() + 10.f;
  ImGui::BeginChild("##spbody", ImVec2(0.f, -footer), false);
  if (ImGui::BeginTabBar("##sptabs")) {
    if (ImGui::BeginTabItem("Information")) {
      DrawInformationTab(cmd, &name, &description, &styleName);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Definition")) {
      DrawDefinitionTab(cmd, s);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Analysis")) {
      DrawAnalysisTab(cmd, cmd.surfacePropertiesIndex, s, log);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Statistics")) {
      DrawStatisticsTab(s);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::EndChild();

  const float bw = 88.f;
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (bw + 8.f) * 4.f);
  if (ImGui::Button("OK", ImVec2(bw, 0.f))) {
    ApplyInfo(cmd, cmd.surfacePropertiesIndex, name, description, styleName, *log);
    cmd.showSurfacePropertiesWindow = false;
    loadedFor = -1;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(bw, 0.f))) {
    cmd.showSurfacePropertiesWindow = false;
    loadedFor = -1;
  }
  ImGui::SameLine();
  if (ImGui::Button("Apply", ImVec2(bw, 0.f)))
    ApplyInfo(cmd, cmd.surfacePropertiesIndex, name, description, styleName, *log);
  ImGui::SameLine();
  ImGui::BeginDisabled();
  ImGui::Button("Help", ImVec2(bw, 0.f));
  ImGui::EndDisabled();

  ImGui::End();
}
