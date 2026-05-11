#include "CadUi.hpp"

#include "DxfIo.hpp"
#include "SurveyCsv.hpp"
#include "WinFileDialogs.hpp"
#include "SurveyPoints.hpp"
#include "WinFileDialogs.hpp"

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <set>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <string>

namespace {

std::string TrimCopyUi(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
    s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

/// Shell steals keyboard via SetKeyboardFocusHere() → navigation activation → InputText selects the
/// whole buffer on that frame. Collapse selection to end-of-buffer so the next keystroke appends.
/// Deliberate Ctrl+A keeps ActiveIdIsJustActivated false, so full selection is preserved.
int CommandLineInputCallback(ImGuiInputTextCallbackData* data) {
  if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways)
    return 0;

  ImGuiContext& g = *GImGui;
  const bool justActivated = (g.ActiveId == data->ID && g.ActiveIdIsJustActivated);
  const bool fullBufSelected =
      data->BufTextLen > 0 && data->SelectionStart == 0 && data->SelectionEnd == data->BufTextLen;

  if (justActivated && fullBufSelected) {
    data->CursorPos = data->BufTextLen;
    data->SelectionStart = data->SelectionEnd = data->CursorPos;
  }
  return 0;
}

/// Nothing wants text yet but the backend queued characters — merge into \p cmdBuf and drain the
/// queue so InputText does not insert the same codepoints twice after we focus it.
void RouteQueuedCharsToCmdBuf(char* cmdBuf, int cmdBufSize, ImGuiIO& io) {
  if (io.InputQueueCharacters.empty())
    return;
  for (int n = 0; n < io.InputQueueCharacters.Size; n++) {
    const unsigned int c = static_cast<unsigned int>(io.InputQueueCharacters[n]);
    char utf8[5];
    const int nbytes = ImTextCharToUtf8(utf8, c);
    if (nbytes <= 0)
      continue;
    const size_t len = std::strlen(cmdBuf);
    if (len + static_cast<size_t>(nbytes) >= static_cast<size_t>(cmdBufSize))
      break;
    std::memcpy(cmdBuf + len, utf8, static_cast<size_t>(nbytes));
    cmdBuf[len + static_cast<size_t>(nbytes)] = '\0';
  }
  io.InputQueueCharacters.clear();
}

} // namespace

void ApplyCadDarkTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;

  style.WindowRounding = 2.f;
  style.ChildRounding = 2.f;
  style.FrameRounding = 2.f;
  style.PopupRounding = 2.f;
  style.ScrollbarRounding = 2.f;
  style.GrabRounding = 2.f;
  style.TabRounding = 2.f;
  style.WindowBorderSize = 1.f;
  style.FrameBorderSize = 1.f;
  style.WindowPadding = ImVec2(8, 8);
  style.FramePadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(8, 6);

  const ImVec4 bg0 = ImVec4(0.11f, 0.11f, 0.12f, 1.f);
  const ImVec4 bg1 = ImVec4(0.14f, 0.14f, 0.15f, 1.f);
  const ImVec4 bg2 = ImVec4(0.18f, 0.18f, 0.19f, 1.f);
  const ImVec4 accent = ImVec4(0.26f, 0.48f, 0.78f, 1.f);

  colors[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.94f, 1.f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.52f, 1.f);
  colors[ImGuiCol_WindowBg] = bg0;
  colors[ImGuiCol_ChildBg] = bg0;
  colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.13f, 0.98f);
  colors[ImGuiCol_Border] = ImVec4(0.28f, 0.28f, 0.30f, 1.f);
  colors[ImGuiCol_FrameBg] = bg1;
  colors[ImGuiCol_FrameBgHovered] = bg2;
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.36f, 0.52f, 1.f);
  colors[ImGuiCol_TitleBg] = bg1;
  colors[ImGuiCol_TitleBgActive] = bg2;
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.17f, 1.f);
  colors[ImGuiCol_ScrollbarBg] = bg0;
  colors[ImGuiCol_ScrollbarGrab] = bg2;
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.22f, 0.22f, 0.24f, 1.f);
  colors[ImGuiCol_CheckMark] = accent;
  colors[ImGuiCol_SliderGrab] = accent;
  colors[ImGuiCol_Button] = bg2;
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.40f, 0.62f, 1.f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.34f, 0.58f, 1.f);
  colors[ImGuiCol_Header] = bg2;
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.40f, 0.62f, 1.f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.34f, 0.58f, 1.f);
  colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.30f, 1.f);
  colors[ImGuiCol_Tab] = bg1;
  colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.44f, 0.68f, 1.f);
  colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.38f, 0.62f, 1.f);
  colors[ImGuiCol_TabUnfocused] = bg1;
  colors[ImGuiCol_TabUnfocusedActive] = bg2;
  colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.48f, 0.78f, 0.35f);
  colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.f);
}

void SetupMainDockLayout(ImGuiID dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGuiDockNodeFlags node_flags = ImGuiDockNodeFlags_DockSpace;
  ImGui::DockBuilderAddNode(dockspace_id, node_flags);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

  ImGuiID dock_left = 0;
  ImGuiID dock_right = 0;
  ImGuiID dock_bottom = 0;
  ImGuiID dock_center = dockspace_id;

  ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Left, 0.22f, &dock_left, &dock_center);
  ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Right, 0.24f, &dock_right, &dock_center);
  ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down, 0.30f, &dock_bottom, &dock_center);

  ImGui::DockBuilderDockWindow("Properties", dock_left);
  ImGui::DockBuilderDockWindow("Reports", dock_right);
  ImGui::DockBuilderDockWindow("Command line", dock_bottom);
  ImGui::DockBuilderDockWindow("Drawing1", dock_center);

  ImGui::DockBuilderFinish(dockspace_id);
}

void DrawMainMenuBar(AppCommandState& cmd, std::vector<std::string>& log) {
  static char dxfPath[4096]{};
  if (ImGui::BeginMenu("File")) {
    ImGui::MenuItem("New", nullptr);
    ImGui::MenuItem("Open...", nullptr);
    if (ImGui::MenuItem("Import DXF...", nullptr)) {
      if (BrowseOpenFileDxfUtf8(dxfPath, sizeof(dxfPath)))
        ImportDxfFile(cmd, dxfPath, log);
    }
    if (ImGui::MenuItem("Export DXF...", nullptr)) {
      if (BrowseSaveFileDxfUtf8(dxfPath, sizeof(dxfPath), "drawing.dxf"))
        ExportDxfFile(cmd, dxfPath, log);
    }
    ImGui::Separator();
    ImGui::MenuItem("Exit", nullptr);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    ImGui::MenuItem("Undo", nullptr);
    ImGui::MenuItem("Redo", nullptr);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem("Reset layout", nullptr);
    ImGui::EndMenu();
  }
}

static void CollectAllDrawingLayers(const AppCommandState& cmd, std::vector<std::string>* outSortedUnique) {
  std::set<std::string> layers;
  layers.insert("0");
  auto add = [&layers](const std::string& s) {
    if (!s.empty())
      layers.insert(s);
  };
  for (const auto& a : cmd.userLineAttrs)
    add(a.layer);
  for (const auto& a : cmd.userCircleAttrs)
    add(a.layer);
  for (const auto& a : cmd.userArcAttrs)
    add(a.layer);
  for (const auto& a : cmd.userEllAttrs)
    add(a.layer);
  for (const auto& a : cmd.userPolylineAttrs)
    add(a.layer);
  for (const auto& a : cmd.cadAnnotationAttrs)
    add(a.layer);
  outSortedUnique->assign(layers.begin(), layers.end());
}

static float RibbonSectionWidthPx(int nCols, float cellW) {
  const ImGuiStyle& st = ImGui::GetStyle();
  if (nCols <= 0)
    return cellW + 16.f;
  return static_cast<float>(nCols) * cellW + static_cast<float>(std::max(0, nCols - 1)) * st.ItemSpacing.x + 16.f;
}

/// One size for all buttons in a ribbon section: fits the widest and tallest label (with padding).
static ImVec2 RibbonButtonCellMetrics(std::initializer_list<const char*> labels) {
  const ImGuiStyle& st = ImGui::GetStyle();
  float maxTw = 0.f;
  float maxTh = 0.f;
  for (const char* s : labels) {
    if (!s || !s[0])
      continue;
    const ImVec2 tz = ImGui::CalcTextSize(s, nullptr, true);
    maxTw = std::max(maxTw, tz.x);
    maxTh = std::max(maxTh, tz.y);
  }
  const float minW = ImGui::GetFrameHeight() * 1.2f;
  const float minH = ImGui::GetFrameHeight() * 1.2f;
  const float w = std::max(minW, maxTw + st.FramePadding.x * 2.f + 8.f);
  const float h = std::max(minH, maxTh + st.FramePadding.y * 2.f + 6.f);
  return ImVec2(w, h);
}

static void RibbonSectionBegin(const char* childId, const char* title, float width, float height) {
  ImGui::BeginGroup();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.14f, 0.16f, 1.f));
  ImGui::BeginChild(childId, ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.64f, 0.72f, 1.f));
  ImGui::TextUnformatted(title);
  ImGui::PopStyleColor();
  ImGui::Separator();
}

static void RibbonSectionEnd() {
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::EndGroup();
}

static void RibbonItemHelp(const char* text, ImGuiHoveredFlags extraFlags = 0) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | extraFlags) && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void DrawRibbonBar(float height, AppCommandState& cmd, std::vector<std::string>& log) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));
  ImGui::BeginChild("RibbonStrip", ImVec2(0, height), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

  const ImGuiStyle& st = ImGui::GetStyle();
  const float panelH = height - st.WindowPadding.y * 2.f;
  constexpr float kLayerPanelW = 500.f;

  const ImVec2 drawCell = RibbonButtonCellMetrics(
      {"Line", "Circle", "PLine", "Arc", "Ellipse", "Dim", "Text", "Mtext"});
  const ImVec2 modCell =
      RibbonButtonCellMetrics({"Move", "Copy", "Rotate", "Erase", "Join", "Trim"});
  const ImVec2 viewCell = RibbonButtonCellMetrics({"Z extents", "Z window"});
  const ImVec2 inqCell = RibbonButtonCellMetrics({"Scale", "Mirror"});
  const ImVec2 srvCell = RibbonButtonCellMetrics({"Point"});
  const ImVec2 layBtnCell = RibbonButtonCellMetrics({"LAY"});
  const float layerRowBtnH =
      std::max({drawCell.y, modCell.y, viewCell.y, inqCell.y, srvCell.y, layBtnCell.y});

  ImGui::BeginChild("RibbonToolsLeft", ImVec2(-kLayerPanelW - st.ItemSpacing.x, panelH), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

  const int drawCols = 4;
  const int modCols = 4;
  const float wDraw = RibbonSectionWidthPx(drawCols, drawCell.x);
  const float wMod = RibbonSectionWidthPx(modCols, modCell.x);
  const float wView = RibbonSectionWidthPx(2, viewCell.x);
  const float wInq = RibbonSectionWidthPx(2, inqCell.x);
  const float wSrv = RibbonSectionWidthPx(1, srvCell.x);

  auto gridSameLine = [](int idx) {
    constexpr int kCols = 4;
    if (idx % kCols != 0)
      ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
  };

  RibbonSectionBegin("RibbonSecDraw", "Draw", wDraw, panelH);
  {
    int i = 0;
    gridSameLine(i++);
    if (ImGui::Button("Line", ImVec2(drawCell.x, drawCell.y)))
      StartLineCommand(cmd, log);
    RibbonItemHelp("Line — draw straight segments between points.\nCommand bar: LINE or L");
    gridSameLine(i++);
    if (ImGui::Button("Circle", ImVec2(drawCell.x, drawCell.y)))
      StartCircleCommand(cmd, log);
    RibbonItemHelp("Circle — center point and radius.\nCommand bar: CIRCLE or C");
    gridSameLine(i++);
    if (ImGui::Button("PLine", ImVec2(drawCell.x, drawCell.y)))
      StartPolylineCommand(cmd, log);
    RibbonItemHelp("Polyline — chain of segments; optional close.\nCommand bar: POLYLINE or PL");
    gridSameLine(i++);
    if (ImGui::Button("Arc", ImVec2(drawCell.x, drawCell.y)))
      StartArcCommand(cmd, log);
    RibbonItemHelp("Arc — three-point arc (start, mid, end).\nCommand bar: ARC");
    gridSameLine(i++);
    if (ImGui::Button("Ellipse", ImVec2(drawCell.x, drawCell.y)))
      StartEllipseCommand(cmd, log);
    RibbonItemHelp("Ellipse — center, axis endpoint, then ratio on command line.\nCommand bar: ELLIPSE or EL");
    gridSameLine(i++);
    if (ImGui::Button("Dim", ImVec2(drawCell.x, drawCell.y)))
      StartDimAlignedCommand(cmd, log);
    RibbonItemHelp("Aligned dimension — extension lines and text.\nCommand bar: DIMALIGNED or DAL");
    gridSameLine(i++);
    if (ImGui::Button("Text", ImVec2(drawCell.x, drawCell.y)))
      StartTextCommand(cmd, log);
    RibbonItemHelp("Text — single-line annotation at insertion.\nCommand bar: TEXT");
    gridSameLine(i++);
    if (ImGui::Button("Mtext", ImVec2(drawCell.x, drawCell.y)))
      StartMtextCommand(cmd, log);
    RibbonItemHelp("Mtext — multiline paragraph in a frame.\nCommand bar: MTEXT or MT");
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 10);

  RibbonSectionBegin("RibbonSecModify", "Modify", wMod, panelH);
  {
    int i = 0;
    auto g = [](int idx) {
      constexpr int kCols = 4;
      if (idx % kCols != 0)
        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
    };
    g(i++);
    if (ImGui::Button("Move", ImVec2(modCell.x, modCell.y)))
      StartMoveCommand(cmd, log);
    RibbonItemHelp("Move — relocate selected entities by base point and offset.\nCommand bar: MOVE or M");
    g(i++);
    if (ImGui::Button("Copy", ImVec2(modCell.x, modCell.y)))
      StartCopyCommand(cmd, log);
    RibbonItemHelp("Copy — duplicate selection with base point and offset.\nCommand bar: COPY or CP");
    g(i++);
    if (ImGui::Button("Rotate", ImVec2(modCell.x, modCell.y)))
      StartRotateCommand(cmd, log);
    RibbonItemHelp("Rotate — turn selection around a base point by angle.\nCommand bar: ROTATE or RO");
    g(i++);
    if (ImGui::Button("Erase", ImVec2(modCell.x, modCell.y)))
      StartDeleteCommand(cmd, log);
    RibbonItemHelp("Erase — remove entities (window or crossing selection).\nCommand bar: DELETE or DEL");
    g(i++);
    if (ImGui::Button("Join", ImVec2(modCell.x, modCell.y)))
      StartJoinCommand(cmd, log);
    RibbonItemHelp("Join — merge colinear line segments.\nCommand bar: JOIN or J");
    g(i++);
    if (ImGui::Button("Trim", ImVec2(modCell.x, modCell.y)))
      StartTrimCommand(cmd, log);
    RibbonItemHelp("Trim — shorten segments to cutting edges or drawn trim line.\nCommand bar: TRIM or TR");
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 10);

  RibbonSectionBegin("RibbonSecView", "View", wView, panelH);
  {
    if (ImGui::Button("Z extents", ImVec2(viewCell.x, viewCell.y)))
      StartZoomExtentsCommand(cmd, log);
    RibbonItemHelp("Zoom extents — fit all drawing content in the view.\nCommand bar: ZOOMEXTENTS or ZE");
    ImGui::SameLine(0, st.ItemSpacing.x);
    if (ImGui::Button("Z window", ImVec2(viewCell.x, viewCell.y)))
      StartZoomWindowCommand(cmd, log);
    RibbonItemHelp("Zoom window — zoom to a rectangle you pick with two clicks.\nCommand bar: ZOOMWINDOW or ZW");
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 10);

  RibbonSectionBegin("RibbonSecInquiry", "Inquiry", wInq, panelH);
  {
    ImGui::BeginDisabled();
    ImGui::Button("Scale", ImVec2(inqCell.x, inqCell.y));
    RibbonItemHelp("Scale — resize selection relative to a base point (not implemented yet).\nCommand bar: (none yet)",
                   ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::SameLine(0, st.ItemSpacing.x);
    ImGui::Button("Mirror", ImVec2(inqCell.x, inqCell.y));
    RibbonItemHelp("Mirror — flip selection across a mirror line (not implemented yet).\nCommand bar: (none yet)",
                   ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::EndDisabled();
  }
  RibbonSectionEnd();
  ImGui::SameLine(0, 10);

  RibbonSectionBegin("RibbonSecSurvey", "Survey", wSrv, panelH);
  {
    ImGui::BeginDisabled();
    ImGui::Button("Point", ImVec2(srvCell.x, srvCell.y));
    RibbonItemHelp(
        "Survey point — place field points in the drawing (coming soon).\nCommand bar: CREATEPOINTS or CRTPTS",
        ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::EndDisabled();
  }
  RibbonSectionEnd();

  ImGui::EndChild();

  ImGui::SameLine(0, st.ItemSpacing.x);
  ImGui::BeginChild("RibbonLayerStrip", ImVec2(kLayerPanelW, panelH), true, ImGuiWindowFlags_NoScrollbar);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.64f, 0.72f, 1.f));
  ImGui::TextUnformatted("Layer");
  ImGui::PopStyleColor();
  ImGui::Separator();

  static std::string ribbonActiveLayer = "0";
  std::vector<std::string> layerList;
  CollectAllDrawingLayers(cmd, &layerList);
  if (std::find(layerList.begin(), layerList.end(), ribbonActiveLayer) == layerList.end())
    layerList.insert(layerList.begin(), ribbonActiveLayer);

  const float layerBtnW = layBtnCell.x;
  if (ImGui::Button("LAY", ImVec2(layerBtnW, layerRowBtnH))) {
    log.push_back("LAYER — layer manager table (coming soon).");
  }
  RibbonItemHelp("Open layer manager — table of all layers (coming soon).\nCommand bar: LAYER (planned)");
  ImGui::SameLine(0, st.ItemSpacing.x);
  ImGui::SetNextItemWidth(std::max(80.f, kLayerPanelW - layerBtnW - st.ItemSpacing.x - st.WindowPadding.x * 2.f));
  const char* preview = ribbonActiveLayer.empty() ? "0" : ribbonActiveLayer.c_str();
  ImGui::PushID("RibbonLayerCombo");
  if (ImGui::BeginCombo("##ribbonlayerpick", preview, ImGuiComboFlags_HeightLargest)) {
    for (const auto& L : layerList) {
      const bool sel = L == ribbonActiveLayer;
      if (ImGui::Selectable(L.c_str(), sel))
        ribbonActiveLayer = L;
      if (sel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
  RibbonItemHelp("Current layer for new geometry (full wiring later).\nCommand bar: (set via properties for now)");

  ImGui::EndChild();

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
}

namespace {

constexpr const char* kVaries = "VARIES";

std::string MergeStrings(const std::vector<std::string>& v) {
  if (v.empty())
    return "---";
  const std::string& ref = v.front();
  for (const auto& s : v)
    if (s != ref)
      return kVaries;
  return ref;
}

std::string MergeFloatsFmt(const std::vector<float>& v, const char* fmt, float eps = 1e-5f) {
  if (v.empty())
    return "---";
  float r = v.front();
  for (float x : v)
    if (std::fabs(x - r) > eps)
      return kVaries;
  char buf[96];
  std::snprintf(buf, sizeof(buf), fmt, static_cast<double>(r));
  return buf;
}

std::string FormatXY(float x, float y) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.4f, %.4f", static_cast<double>(x), static_cast<double>(y));
  return buf;
}

/// Clockwise from north (+Y): **north = 0°**, east = 90°, decimal degrees [0, 360). App-wide bearing convention.

float BearingDegreesCwFromNorth(float dx, float dy) {
  const double rad = std::atan2(static_cast<double>(dx), static_cast<double>(dy));
  double deg = rad * (180.0 / 3.14159265358979323846);
  if (deg < 0.0)
    deg += 360.0;
  return static_cast<float>(deg);
}

const EntityAttributes& LineAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userLineAttrs.size())
    return kDef;
  return cmd.userLineAttrs[u];
}

const EntityAttributes& CircleAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userCircleAttrs.size())
    return kDef;
  return cmd.userCircleAttrs[u];
}

const EntityAttributes& ArcAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userArcAttrs.size())
    return kDef;
  return cmd.userArcAttrs[u];
}

const EntityAttributes& EllipseAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userEllAttrs.size())
    return kDef;
  return cmd.userEllAttrs[u];
}

const EntityAttributes& PolylineAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.userPolylineAttrs.size())
    return kDef;
  return cmd.userPolylineAttrs[u];
}

const EntityAttributes& AnnAttr(const AppCommandState& cmd, int idx) {
  static const EntityAttributes kDef{};
  if (idx < 0)
    return kDef;
  const size_t u = static_cast<size_t>(idx);
  if (u >= cmd.cadAnnotationAttrs.size())
    return kDef;
  return cmd.cadAnnotationAttrs[u];
}

bool ReadLineEndpoints(const AppCommandState& cmd, int idx, float* x0, float* y0, float* x1, float* y1) {
  const size_t k = static_cast<size_t>(idx) * 6;
  if (k + 5 >= cmd.userLinesFlat.size())
    return false;
  *x0 = cmd.userLinesFlat[k];
  *y0 = cmd.userLinesFlat[k + 1];
  *x1 = cmd.userLinesFlat[k + 3];
  *y1 = cmd.userLinesFlat[k + 4];
  return true;
}

bool ReadCircle(const AppCommandState& cmd, int idx, float* cx, float* cy, float* r) {
  const size_t k = static_cast<size_t>(idx) * 3;
  if (k + 2 >= cmd.userCirclesCxCyR.size())
    return false;
  *cx = cmd.userCirclesCxCyR[k];
  *cy = cmd.userCirclesCxCyR[k + 1];
  *r = cmd.userCirclesCxCyR[k + 2];
  return true;
}

void PropRow(const char* label, const std::string& value) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(label);
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(value.c_str());
}

std::string TrimUi(std::string s) {
  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  while (!s.empty() && !notSpace(static_cast<unsigned char>(s.front())))
    s.erase(s.begin());
  while (!s.empty() && !notSpace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

void CollectGeneralAttrs(const AppCommandState& cmd, const std::vector<SelectedEntity>& sel,
                         std::vector<std::string>* layers, std::vector<std::string>* colors,
                         std::vector<std::string>* ltypes, std::vector<float>* lws,
                         std::vector<float>* trans) {
  layers->clear();
  colors->clear();
  ltypes->clear();
  lws->clear();
  trans->clear();
  for (const auto& e : sel) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size())
        continue;
      const EntityAttributes& a = LineAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size())
        continue;
      const EntityAttributes& a = CircleAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size())
        continue;
      const EntityAttributes& a = AnnAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userArcs.size())
        continue;
      const EntityAttributes& a = ArcAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.userEllipses.size())
        continue;
      const EntityAttributes& a = EllipseAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int np =
          static_cast<int>(cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0);
      if (e.index < 0 || e.index >= np)
        continue;
      const EntityAttributes& a = PolylineAttr(cmd, e.index);
      layers->push_back(a.layer);
      colors->push_back(a.color);
      ltypes->push_back(a.linetype);
      lws->push_back(a.lineweightMm);
      trans->push_back(a.transparency);
    }
  }
}

struct NamedColorPreset {
  const char* label;
  const char* storage;
  float r;
  float g;
  float b;
};

static const NamedColorPreset kNamedColors[] = {
    {"By Layer", "ByLayer", 1.f, 1.f, 1.f}, {"Red", "Red", 1.f, 0.f, 0.f}, {"Yellow", "Yellow", 1.f, 1.f, 0.f},
    {"Green", "Green", 0.f, 1.f, 0.f},    {"Cyan", "Cyan", 0.f, 1.f, 1.f}, {"Blue", "Blue", 0.f, 0.f, 1.f},
    {"Magenta", "Magenta", 1.f, 0.f, 1.f}, {"White", "White", 1.f, 1.f, 1.f}, {"Gray", "Gray", 0.5f, 0.5f, 0.5f},
    {"Black", "Black", 0.f, 0.f, 0.f},    {"Orange", "Orange", 1.f, 0.5f, 0.f},
};

bool ParseHexColorRgb(const std::string& s, float* r, float* g, float* b) {
  if (s.size() < 4 || s[0] != '#')
    return false;
  auto hexVal = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
      return 10 + (c - 'A');
    return -1;
  };
  if (s.size() == 4) {
    const int rh = hexVal(s[1]);
    const int gh = hexVal(s[2]);
    const int bh = hexVal(s[3]);
    if (rh < 0 || gh < 0 || bh < 0)
      return false;
    const float rf = static_cast<float>(rh | (rh << 4)) / 255.f;
    const float gf = static_cast<float>(gh | (gh << 4)) / 255.f;
    const float bf = static_cast<float>(bh | (bh << 4)) / 255.f;
    *r = rf;
    *g = gf;
    *b = bf;
    return true;
  }
  if (s.size() != 7)
    return false;
  int rv = 0;
  int gv = 0;
  int bv = 0;
  for (int i = 0; i < 2; ++i)
    rv = rv * 16 + hexVal(s[1 + i]);
  for (int i = 0; i < 2; ++i)
    gv = gv * 16 + hexVal(s[3 + i]);
  for (int i = 0; i < 2; ++i)
    bv = bv * 16 + hexVal(s[5 + i]);
  if (rv < 0 || gv < 0 || bv < 0)
    return false;
  *r = static_cast<float>(rv) / 255.f;
  *g = static_cast<float>(gv) / 255.f;
  *b = static_cast<float>(bv) / 255.f;
  return true;
}

std::string FormatHexColorRgb(float r, float g, float b) {
  const int R = static_cast<int>(std::lround(std::clamp(r, 0.f, 1.f) * 255.f));
  const int G = static_cast<int>(std::lround(std::clamp(g, 0.f, 1.f) * 255.f));
  const int B = static_cast<int>(std::lround(std::clamp(b, 0.f, 1.f) * 255.f));
  char buf[16]{};
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", R, G, B);
  return std::string(buf);
}

bool LookupNamedColorRgb(const std::string& storage, float* r, float* g, float* b) {
  for (const auto& p : kNamedColors) {
    if (storage == p.storage) {
      *r = p.r;
      *g = p.g;
      *b = p.b;
      return true;
    }
  }
  return false;
}

std::string ColorStorageToPreviewLabel(const std::string& mergedFromSelection) {
  if (mergedFromSelection == kVaries)
    return "(mixed)";
  if (mergedFromSelection == "---" || mergedFromSelection.empty())
    return "---";
  if (mergedFromSelection == "ByLayer")
    return "By Layer";
  for (const auto& p : kNamedColors) {
    if (mergedFromSelection == p.storage)
      return p.label;
  }
  if (!mergedFromSelection.empty() && mergedFromSelection[0] == '#')
    return std::string("Custom ") + mergedFromSelection;
  return mergedFromSelection;
}

static float gCustomColorPicker[4] = {1.f, 1.f, 1.f, 1.f};

void PrepareCustomColorPicker(const AppCommandState& cmd) {
  std::vector<std::string> layers, colors, ltypes;
  std::vector<float> lws, trans;
  CollectGeneralAttrs(cmd, cmd.selection, &layers, &colors, &ltypes, &lws, &trans);
  const std::string merged = MergeStrings(colors);
  std::string seed = colors.empty() ? std::string("ByLayer") : colors.front();
  if (merged != kVaries)
    seed = merged;

  float r = 1.f;
  float g = 1.f;
  float b = 1.f;
  if (!ParseHexColorRgb(seed, &r, &g, &b))
    LookupNamedColorRgb(seed, &r, &g, &b);

  gCustomColorPicker[0] = r;
  gCustomColorPicker[1] = g;
  gCustomColorPicker[2] = b;
  gCustomColorPicker[3] = 1.f;
}

uint64_t SelectionFingerprint(const std::vector<SelectedEntity>& sel) {
  uint64_t h = sel.size();
  for (const auto& e : sel) {
    h = h * 1315423911ull + static_cast<uint64_t>(static_cast<int>(e.type));
    h = h * 1315423911ull + static_cast<uint64_t>(static_cast<uint32_t>(e.index));
  }
  return h;
}

static char gBufLayer[160]{};
static char gBufLinetype[160]{};
static float gLineweightMm = 0.18f;
static float gTransparency01 = 0.f;
static bool gLineweightMixed = false;
static bool gTransparencyMixed = false;
static bool gHintLayerMixed = false;
static bool gHintLinetypeMixed = false;
static uint64_t gPropsSelFingerprint = ~0ull;

void EnsureAttrCounts(AppCommandState& cmd) {
  bool grew = false;
  const size_t nl = cmd.userLinesFlat.size() / 6;
  while (cmd.userLineAttrs.size() < nl) {
    cmd.userLineAttrs.emplace_back();
    grew = true;
  }
  const size_t nc = cmd.userCirclesCxCyR.size() / 3;
  while (cmd.userCircleAttrs.size() < nc) {
    cmd.userCircleAttrs.emplace_back();
    grew = true;
  }
  while (cmd.userArcAttrs.size() < cmd.userArcs.size()) {
    cmd.userArcAttrs.emplace_back();
    grew = true;
  }
  while (cmd.userEllAttrs.size() < cmd.userEllipses.size()) {
    cmd.userEllAttrs.emplace_back();
    grew = true;
  }
  const size_t np =
      cmd.userPolylineOffsets.size() > 0 ? cmd.userPolylineOffsets.size() - 1 : 0;
  while (cmd.userPolylineAttrs.size() < np) {
    cmd.userPolylineAttrs.emplace_back();
    grew = true;
  }
  const size_t na = cmd.cadAnnotations.size();
  while (cmd.cadAnnotationAttrs.size() < na) {
    cmd.cadAnnotationAttrs.emplace_back();
    grew = true;
  }
  if (grew)
    BumpCadGpuCache(cmd);
}

void RefreshMixedHintFlags(AppCommandState& cmd) {
  std::vector<std::string> layers, colors, ltypes;
  std::vector<float> lws, trans;
  CollectGeneralAttrs(cmd, cmd.selection, &layers, &colors, &ltypes, &lws, &trans);
  if (!layers.empty()) {
    gHintLayerMixed = (MergeStrings(layers) == kVaries);
    gHintLinetypeMixed = (MergeStrings(ltypes) == kVaries);
  }
  gLineweightMixed = false;
  if (!lws.empty()) {
    const float r = lws.front();
    for (float w : lws) {
      if (std::fabs(w - r) > 1e-5f) {
        gLineweightMixed = true;
        break;
      }
    }
  }
  gTransparencyMixed = false;
  if (!trans.empty()) {
    const float t0 = trans.front();
    for (float t : trans) {
      if (std::fabs(t - t0) > 1e-5f) {
        gTransparencyMixed = true;
        break;
      }
    }
  }
}

void RefreshPropsBuffersFromModel(AppCommandState& cmd, const std::vector<SelectedEntity>& sel) {
  std::vector<std::string> layers, colors, ltypes;
  std::vector<float> lws, trans;
  CollectGeneralAttrs(cmd, sel, &layers, &colors, &ltypes, &lws, &trans);
  (void)colors;

  const std::string ml = MergeStrings(layers);
  const std::string mt = MergeStrings(ltypes);
  gHintLayerMixed = (ml == kVaries);
  gHintLinetypeMixed = (mt == kVaries);

  auto fillTextBuf = [&](char* buf, int bufSize, const std::string& merged) {
    if (merged.empty() || merged == kVaries || merged == "---") {
      buf[0] = '\0';
    } else {
      ImStrncpy(buf, merged.c_str(), bufSize);
      buf[bufSize - 1] = '\0';
    }
  };

  fillTextBuf(gBufLayer, IM_ARRAYSIZE(gBufLayer), ml);
  fillTextBuf(gBufLinetype, IM_ARRAYSIZE(gBufLinetype), mt);

  gLineweightMixed = false;
  gTransparencyMixed = false;
  if (!lws.empty()) {
    gLineweightMm = lws.front();
    for (float w : lws) {
      if (std::fabs(w - lws.front()) > 1e-5f) {
        gLineweightMixed = true;
        break;
      }
    }
  }
  if (!trans.empty()) {
    gTransparency01 = trans.front();
    for (float t : trans) {
      if (std::fabs(t - trans.front()) > 1e-5f) {
        gTransparencyMixed = true;
        break;
      }
    }
  }
}

void ApplyLayerToSelection(AppCommandState& cmd, const std::string& v) {
  if (v.empty())
    return;
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].layer = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].layer = v;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

void ApplyColorToSelection(AppCommandState& cmd, const std::string& v) {
  if (v.empty())
    return;
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].color = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].color = v;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

void ApplyLinetypeToSelection(AppCommandState& cmd, const std::string& v) {
  if (v.empty())
    return;
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].linetype = v;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].linetype = v;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

void ApplyLineweightToSelection(AppCommandState& cmd, float mm) {
  mm = std::max(0.f, mm);
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].lineweightMm = mm;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].lineweightMm = mm;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].lineweightMm = mm;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

void ApplyTransparencyToSelection(AppCommandState& cmd, float a) {
  a = std::clamp(a, 0.f, 1.f);
  EnsureAttrCounts(cmd);
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size() || static_cast<size_t>(e.index) >= cmd.userLineAttrs.size())
        continue;
      cmd.userLineAttrs[static_cast<size_t>(e.index)].transparency = a;
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size() || static_cast<size_t>(e.index) >= cmd.userCircleAttrs.size())
        continue;
      cmd.userCircleAttrs[static_cast<size_t>(e.index)].transparency = a;
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size() ||
          static_cast<size_t>(e.index) >= cmd.cadAnnotationAttrs.size())
        continue;
      cmd.cadAnnotationAttrs[static_cast<size_t>(e.index)].transparency = a;
    }
  }
  BumpCadGpuCache(cmd);
  RefreshMixedHintFlags(cmd);
}

/// \return true if user chose Custom — caller must `OpenPopup("GoSurveyCustomColor")` after combo/popups close.
bool DrawColorPickerRow(AppCommandState& cmd) {
  bool requestCustomPopup = false;
  std::vector<std::string> layers, colors, ltypes;
  std::vector<float> lws, trans;
  CollectGeneralAttrs(cmd, cmd.selection, &layers, &colors, &ltypes, &lws, &trans);
  (void)layers;
  (void)ltypes;
  (void)lws;

  const std::string merged = MergeStrings(colors);
  float mergedTrans = 0.f;
  if (!trans.empty()) {
    mergedTrans = trans.front();
    for (float t : trans) {
      if (std::fabs(t - mergedTrans) > 1e-5f) {
        mergedTrans = 0.f;
        break;
      }
    }
  }

  int nLine = 0;
  int nCirc = 0;
  int nAnn = 0;
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg)
      ++nLine;
    else if (e.type == SelectedEntity::Type::Circle)
      ++nCirc;
    else if (e.type == SelectedEntity::Type::Annotation)
      ++nAnn;
  }
  float dr = 0.35f;
  float dg = 0.95f;
  float db = 1.f;
  if (nCirc > 0 && nLine == 0 && nAnn == 0) {
    dr = 0.92f;
    dg = 0.55f;
    db = 1.f;
  } else if (nAnn > 0 && nLine == 0 && nCirc == 0) {
    dr = 0.85f;
    dg = 0.95f;
    db = 0.65f;
  }

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted("Color");
  ImGui::TableNextColumn();

  const float frameH = ImGui::GetFrameHeight();
  ImVec4 swatchRgb;
  if (merged == kVaries || merged == "---") {
    swatchRgb = ImVec4(0.45f, 0.45f, 0.47f, 1.f - mergedTrans);
  } else {
    float rgba[4];
    ResolveStoredColorForViewport(merged, mergedTrans, dr, dg, db, rgba);
    swatchRgb = ImVec4(rgba[0], rgba[1], rgba[2], rgba[3]);
  }

  ImGui::ColorButton("##colorswatch", swatchRgb,
                     ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                     ImVec2(std::max(18.f, frameH - 2.f), std::max(18.f, frameH - 2.f)));
  ImGui::SameLine(0.f, 6.f);
  ImGui::SetNextItemWidth(std::max(40.f, ImGui::GetContentRegionAvail().x));

  const std::string preview = ColorStorageToPreviewLabel(merged);
  const ImVec2 rowSwatchSize(18.f, ImGui::GetTextLineHeightWithSpacing());
  const ImGuiColorEditFlags rowSwatchFlags =
      ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop;

  if (ImGui::BeginCombo("##colorcombo", preview.c_str())) {
    for (const auto& p : kNamedColors) {
      const bool selected =
          (merged != kVaries && merged != "---" && !merged.empty() && merged == p.storage);
      ImGui::PushID(p.storage);
      float prgba[4];
      ResolveStoredColorForViewport(p.storage, 0.f, dr, dg, db, prgba);
      bool hit = ImGui::ColorButton("##rowsw", ImVec4(prgba[0], prgba[1], prgba[2], prgba[3]), rowSwatchFlags,
                                    rowSwatchSize);
      ImGui::SameLine(0.f, 8.f);
      hit |= ImGui::Selectable(p.label, selected, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.f, rowSwatchSize.y));
      if (hit)
        ApplyColorToSelection(cmd, p.storage);
      ImGui::PopID();
    }
    ImGui::Separator();

    ImGui::PushID("custom_row");
    float customPreview[4];
    if (!merged.empty() && merged[0] == '#' && merged != kVaries)
      ResolveStoredColorForViewport(merged, mergedTrans, dr, dg, db, customPreview);
    else {
      customPreview[0] = customPreview[1] = customPreview[2] = 1.f;
      customPreview[3] = 1.f;
    }
    bool openCustom = ImGui::ColorButton(
        "##customrowsw", ImVec4(customPreview[0], customPreview[1], customPreview[2], customPreview[3]), rowSwatchFlags,
        rowSwatchSize);
    ImGui::SameLine(0.f, 8.f);
    openCustom |= ImGui::Selectable("Custom color…", false, ImGuiSelectableFlags_SpanAvailWidth,
                                  ImVec2(0.f, rowSwatchSize.y));
    if (openCustom) {
      PrepareCustomColorPicker(cmd);
      requestCustomPopup = true;
    }
    ImGui::PopID();

    ImGui::EndCombo();
  }

  return requestCustomPopup;
}

void DrawEditableGeneralSection(AppCommandState& cmd, const std::vector<SelectedEntity>& sel) {
  (void)sel;
  if (!ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  const ImGuiInputTextFlags tflags = ImGuiInputTextFlags_EnterReturnsTrue;
  bool requestCustomColorPopup = false;

  if (ImGui::BeginTable("props_gen_ed", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Layer");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    const bool layerEnter =
        ImGui::InputTextWithHint("##layer", gHintLayerMixed ? "Mixed — enter applies to all" : "", gBufLayer,
                                 IM_ARRAYSIZE(gBufLayer), tflags);
    const bool layerDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    if (layerEnter || layerDeactivated) {
      const std::string v = TrimUi(std::string(gBufLayer));
      if (!v.empty())
        ApplyLayerToSelection(cmd, v);
    }

    requestCustomColorPopup = DrawColorPickerRow(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Linetype");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    const bool ltEnter =
        ImGui::InputTextWithHint("##linetype", gHintLinetypeMixed ? "Mixed — enter applies to all" : "",
                                 gBufLinetype, IM_ARRAYSIZE(gBufLinetype), tflags);
    const bool ltDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    if (ltEnter || ltDeactivated) {
      const std::string v = TrimUi(std::string(gBufLinetype));
      if (!v.empty())
        ApplyLinetypeToSelection(cmd, v);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Lineweight");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    const bool lwCh =
        ImGui::SliderFloat("##lw", &gLineweightMm, 0.f, 5.f, "%.2f mm");
    if (lwCh || ImGui::IsItemDeactivatedAfterEdit()) {
      ApplyLineweightToSelection(cmd, gLineweightMm);
      gLineweightMixed = false;
    }
    if (gLineweightMixed) {
      ImGui::SameLine();
      ImGui::TextDisabled("(mixed)");
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Transparency");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    const bool trCh = ImGui::SliderFloat("##tr", &gTransparency01, 0.f, 1.f, "");
    if (trCh || ImGui::IsItemDeactivatedAfterEdit()) {
      ApplyTransparencyToSelection(cmd, gTransparency01);
      gTransparencyMixed = false;
    }
    ImGui::SameLine();
    ImGui::Text("%.0f %%", static_cast<double>(gTransparency01 * 100.f));
    if (gTransparencyMixed) {
      ImGui::SameLine();
      ImGui::TextDisabled("(mixed)");
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Default text height (in)");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##defplottxt", &cmd.defaultPlottedTextHeightInches, 0.005f, 0.02f, "%.4f");
    if (cmd.defaultPlottedTextHeightInches <= 0.f)
      cmd.defaultPlottedTextHeightInches = 0.0625f;
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::EndTable();
  }

  if (requestCustomColorPopup)
    ImGui::OpenPopup("GoSurveyCustomColor");

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal("GoSurveyCustomColor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::ColorPicker4("##custpick", gCustomColorPicker,
                        ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB |
                            ImGuiColorEditFlags_NoAlpha);
    ImGui::Separator();
    if (ImGui::Button("Apply", ImVec2(120.f, 0.f))) {
      ApplyColorToSelection(cmd, FormatHexColorRgb(gCustomColorPicker[0], gCustomColorPicker[1],
                                                   gCustomColorPicker[2]));
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.f, 0.f)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void DrawSingleLineGeometryEditable(AppCommandState& cmd, int lineIdx) {
  if (!ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  const size_t k = static_cast<size_t>(lineIdx) * 6;
  if (k + 5 >= cmd.userLinesFlat.size())
    return;
  float* x0 = &cmd.userLinesFlat[k];
  float* y0 = &cmd.userLinesFlat[k + 1];
  float* x1 = &cmd.userLinesFlat[k + 3];
  float* y1 = &cmd.userLinesFlat[k + 4];

  if (ImGui::BeginTable("props_geom_line_ed", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Start X");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##lsx", x0, 0.f, 0.f, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Start Y");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##lsy", y0, 0.f, 0.f, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("End X");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##lex", x1, 0.f, 0.f, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("End Y");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##ley", y1, 0.f, 0.f, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::EndTable();
  }

  const float dx = *x1 - *x0;
  const float dy = *y1 - *y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  const float bear = BearingDegreesCwFromNorth(dx, dy);
  char lenBuf[64];
  char bearBuf[64];
  std::snprintf(lenBuf, sizeof(lenBuf), "%.4f", static_cast<double>(len));
  std::snprintf(bearBuf, sizeof(bearBuf), "%.4f°", static_cast<double>(bear));

  ImGui::Spacing();
  ImGui::TextDisabled("Derived");
  if (ImGui::BeginTable("props_geom_line_derived", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    PropRow("Length", lenBuf);
    PropRow("Rotation rel. north", bearBuf);
    ImGui::EndTable();
  }
}

void DrawSingleCircleGeometryEditable(AppCommandState& cmd, int circleIdx) {
  if (!ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  const size_t k = static_cast<size_t>(circleIdx) * 3;
  if (k + 2 >= cmd.userCirclesCxCyR.size())
    return;
  float* cx = &cmd.userCirclesCxCyR[k];
  float* cy = &cmd.userCirclesCxCyR[k + 1];
  float* r = &cmd.userCirclesCxCyR[k + 2];

  if (ImGui::BeginTable("props_geom_circ_ed", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Center X");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##cx", cx, 0.f, 0.f, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Center Y");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##cy", cy, 0.f, 0.f, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Radius");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##cr", r, 0.f, 0.f, "%.4f");
    if (*r < 1e-6f)
      *r = 1e-6f;
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    ImGui::EndTable();
  }

  constexpr float kPi = 3.14159265358979323846f;
  const float diam = 2.f * (*r);
  const float circ = 2.f * kPi * (*r);
  const float area = kPi * (*r) * (*r);
  char dBuf[64], cBuf[64], aBuf[64];
  std::snprintf(dBuf, sizeof(dBuf), "%.4f", static_cast<double>(diam));
  std::snprintf(cBuf, sizeof(cBuf), "%.4f", static_cast<double>(circ));
  std::snprintf(aBuf, sizeof(aBuf), "%.4f", static_cast<double>(area));

  ImGui::Spacing();
  ImGui::TextDisabled("Derived");
  if (ImGui::BeginTable("props_geom_circ_derived", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    PropRow("Diameter", dBuf);
    PropRow("Circumference", cBuf);
    PropRow("Area", aBuf);
    ImGui::EndTable();
  }
}

void DrawLineGeometryOnly(const AppCommandState& cmd, const std::vector<SelectedEntity>& linesOnly) {
  std::vector<float> vx0, vy0, vx1, vy1, vlen, vbear;
  for (const auto& e : linesOnly) {
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    if (!ReadLineEndpoints(cmd, e.index, &x0, &y0, &x1, &y1))
      continue;
    vx0.push_back(x0);
    vy0.push_back(y0);
    vx1.push_back(x1);
    vy1.push_back(y1);
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    vlen.push_back(len);
    vbear.push_back(BearingDegreesCwFromNorth(dx, dy));
  }
  auto mergeCoord = [&](const std::vector<float>& xs, const std::vector<float>& ys) -> std::string {
    if (xs.empty() || ys.empty() || xs.size() != ys.size())
      return "---";
    std::string ref = FormatXY(xs[0], ys[0]);
    for (size_t i = 1; i < xs.size(); ++i) {
      if (FormatXY(xs[i], ys[i]) != ref)
        return kVaries;
    }
    return ref;
  };
  const std::string startPt = mergeCoord(vx0, vy0);
  const std::string endPt = mergeCoord(vx1, vy1);
  const std::string lenStr = MergeFloatsFmt(vlen, "%.4f");
  std::string bearStr =
      vbear.empty() ? std::string("---") : MergeFloatsFmt(vbear, "%.4f°", 1e-4f);

  if (!ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  if (ImGui::BeginTable("props_geom_line", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    PropRow("Start point", startPt);
    PropRow("End point", endPt);
    PropRow("Length", lenStr);
    PropRow("Rotation rel. north", bearStr);
    ImGui::EndTable();
  }
}

void DrawCircleGeometryOnly(const AppCommandState& cmd, const std::vector<SelectedEntity>& circlesOnly) {
  std::vector<float> cxv, cyv, rv, diamv, circv, areav;
  for (const auto& e : circlesOnly) {
    float cx = 0.f, cy = 0.f, r = 0.f;
    if (!ReadCircle(cmd, e.index, &cx, &cy, &r))
      continue;
    cxv.push_back(cx);
    cyv.push_back(cy);
    rv.push_back(r);
    diamv.push_back(2.f * r);
    constexpr float kPi = 3.14159265358979323846f;
    circv.push_back(2.f * kPi * r);
    areav.push_back(kPi * r * r);
  }
  const std::string ctr = [&]() -> std::string {
    if (cxv.empty())
      return "---";
    std::string ref = FormatXY(cxv[0], cyv[0]);
    for (size_t i = 1; i < cxv.size(); ++i) {
      if (FormatXY(cxv[i], cyv[i]) != ref)
        return kVaries;
    }
    return ref;
  }();

  if (!ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  if (ImGui::BeginTable("props_geom_circ", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    PropRow("Center", ctr);
    PropRow("Radius", MergeFloatsFmt(rv, "%.4f"));
    PropRow("Diameter", MergeFloatsFmt(diamv, "%.4f"));
    PropRow("Circumference", MergeFloatsFmt(circv, "%.4f"));
    PropRow("Area", MergeFloatsFmt(areav, "%.4f"));
    ImGui::EndTable();
  }
}

void DrawSingleAnnotationGeometryEditable(AppCommandState& cmd, int annIdx) {
  if (!ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  if (annIdx < 0 || static_cast<size_t>(annIdx) >= cmd.cadAnnotations.size())
    return;
  EnsureAttrCounts(cmd);
  CadAnnotation& ann = cmd.cadAnnotations[static_cast<size_t>(annIdx)];

  const char* kindLabel = ann.kind == CadAnnotation::Kind::Text ? "TEXT" : "MTEXT";

  auto syncMtextInsFromBox = [&]() {
    const float mnX = std::min(ann.boxMinX, ann.boxMaxX);
    const float mxX = std::max(ann.boxMinX, ann.boxMaxX);
    const float mnY = std::min(ann.boxMinY, ann.boxMaxY);
    const float mxY = std::max(ann.boxMinY, ann.boxMaxY);
    ann.boxMinX = mnX;
    ann.boxMaxX = mxX;
    ann.boxMinY = mnY;
    ann.boxMaxY = mxY;
    ann.insX = mnX;
    ann.insY = mnY;
  };

  if (ImGui::BeginTable("props_geom_ann_ed", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Kind");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(kindLabel);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Insertion X");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##ainsx", &ann.insX, 0.f, 0.f, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (ann.kind == CadAnnotation::Kind::Mtext) {
        const float dx = ann.insX - ann.boxMinX;
        ann.boxMinX += dx;
        ann.boxMaxX += dx;
      }
      BumpCadGpuCache(cmd);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Insertion Y");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##ainsy", &ann.insY, 0.f, 0.f, "%.4f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      if (ann.kind == CadAnnotation::Kind::Mtext) {
        const float dy = ann.insY - ann.boxMinY;
        ann.boxMinY += dy;
        ann.boxMaxY += dy;
      }
      BumpCadGpuCache(cmd);
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Plotted height (in)");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat("##annph", &ann.plottedHeightInches, 0.001f, 0.f, "%.4f");
    if (ann.plottedHeightInches <= 0.f)
      ann.plottedHeightInches = 0.0625f;
    if (ImGui::IsItemDeactivatedAfterEdit())
      BumpCadGpuCache(cmd);

    if (ann.kind == CadAnnotation::Kind::Text) {
      float rotDeg = BearingCwNorthDegFromMathAngleRad(ann.rotationRad);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Rotation ° CW from N");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##anntrot", &rotDeg, 0.f, 0.f, "%.2f");
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        ann.rotationRad = MathAngleRadFromBearingCwNorthDeg(rotDeg);
        BumpCadGpuCache(cmd);
      }
    }

    if (ann.kind == CadAnnotation::Kind::Mtext) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Box min X");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##bmix", &ann.boxMinX, 0.f, 0.f, "%.4f");
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        syncMtextInsFromBox();
        BumpCadGpuCache(cmd);
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Box min Y");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##bmiy", &ann.boxMinY, 0.f, 0.f, "%.4f");
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        syncMtextInsFromBox();
        BumpCadGpuCache(cmd);
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Box max X");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##bmax", &ann.boxMaxX, 0.f, 0.f, "%.4f");
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        syncMtextInsFromBox();
        BumpCadGpuCache(cmd);
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Box max Y");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputFloat("##bmay", &ann.boxMaxY, 0.f, 0.f, "%.4f");
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        syncMtextInsFromBox();
        BumpCadGpuCache(cmd);
      }
    }

    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextUnformatted("Content");
  ImGui::InputTextMultiline("##anntxtmul", &ann.text, ImVec2(-FLT_MIN, 96.f));
  if (ImGui::IsItemDeactivatedAfterEdit())
    BumpCadGpuCache(cmd);

  ImGui::Spacing();
  ImGui::TextDisabled("Derived");
  const float hWorld = CadAnnotationHeightWorld(ann, cmd.modelUnitsPerPlottedInch);
  char hbuf[96];
  std::snprintf(hbuf, sizeof(hbuf), "%.4f model units", static_cast<double>(hWorld));
  if (ImGui::BeginTable("props_geom_ann_derived", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    PropRow("Model text height", std::string(hbuf));
    if (ann.kind == CadAnnotation::Kind::Mtext) {
      const float bw = std::fabs(ann.boxMaxX - ann.boxMinX);
      const float bh = std::fabs(ann.boxMaxY - ann.boxMinY);
      char wbuf[64], h2buf[64];
      std::snprintf(wbuf, sizeof(wbuf), "%.4f", static_cast<double>(bw));
      std::snprintf(h2buf, sizeof(h2buf), "%.4f", static_cast<double>(bh));
      PropRow("Box width", std::string(wbuf));
      PropRow("Box height", std::string(h2buf));
    }
    ImGui::EndTable();
  }
}

void DrawAnnotationGeometryOnly(const AppCommandState& cmd, const std::vector<SelectedEntity>& annOnly) {
  std::vector<std::string> kinds;
  std::vector<float> phIn, mwHeight, insX, insY, rotDeg;
  for (const auto& e : annOnly) {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.cadAnnotations.size())
      continue;
    const CadAnnotation& a = cmd.cadAnnotations[static_cast<size_t>(e.index)];
    kinds.push_back(a.kind == CadAnnotation::Kind::Text ? "TEXT" : "MTEXT");
    phIn.push_back(a.plottedHeightInches);
    mwHeight.push_back(CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch));
    insX.push_back(a.insX);
    insY.push_back(a.insY);
    rotDeg.push_back(BearingCwNorthDegFromMathAngleRad(a.rotationRad));
  }

  if (!ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  if (ImGui::BeginTable("props_geom_ann", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
    PropRow("Kind", MergeStrings(kinds));
    PropRow("Plotted height (in)", MergeFloatsFmt(phIn, "%.4f"));
    PropRow("Model text height", MergeFloatsFmt(mwHeight, "%.4f"));
    const std::string insXs = [&]() -> std::string {
      if (insX.empty())
        return "---";
      char b[64];
      std::snprintf(b, sizeof(b), "%.4f", static_cast<double>(insX[0]));
      std::string ref(b);
      for (size_t i = 1; i < insX.size(); ++i) {
        std::snprintf(b, sizeof(b), "%.4f", static_cast<double>(insX[i]));
        if (std::string(b) != ref)
          return kVaries;
      }
      return ref;
    }();
    const std::string insYs = [&]() -> std::string {
      if (insY.empty())
        return "---";
      char b[64];
      std::snprintf(b, sizeof(b), "%.4f", static_cast<double>(insY[0]));
      std::string ref(b);
      for (size_t i = 1; i < insY.size(); ++i) {
        std::snprintf(b, sizeof(b), "%.4f", static_cast<double>(insY[i]));
        if (std::string(b) != ref)
          return kVaries;
      }
      return ref;
    }();
    PropRow("Insertion X", insXs);
    PropRow("Insertion Y", insYs);
    PropRow("Rotation ° CW from N", MergeFloatsFmt(rotDeg, "%.2f", 1e-3f));
    ImGui::EndTable();
  }
  ImGui::TextDisabled("Select a single TEXT or MTEXT to edit content and box here.");
}

int PickSurveyPointIndex(const std::vector<SurveyPoint>& pts, float wx, float wy, float orthoHalfHeightWorld,
                         float viewportHeightPx) {
  if (pts.empty())
    return -1;
  const int fbH = static_cast<int>(std::max(1.f, std::floor(viewportHeightPx)));
  const float arm = SurveyCrossHalfExtentWorld(orthoHalfHeightWorld, fbH);
  const float tol = CadSnap::WorldToleranceFromPixels(viewportHeightPx, orthoHalfHeightWorld, 12.f);
  const float radius = std::max(arm, tol) * 1.38f;
  const float r2 = radius * radius;
  int best = -1;
  float bestD2 = 0.f;
  for (size_t i = 0; i < pts.size(); ++i) {
    const float dx = wx - pts[i].easting;
    const float dy = wy - pts[i].northing;
    const float d2 = dx * dx + dy * dy;
    if (d2 <= r2 && (best < 0 || d2 < bestD2)) {
      bestD2 = d2;
      best = static_cast<int>(i);
    }
  }
  return best;
}

void DrawSurveyPointPickProps(AppCommandState& cmd) {
  const auto& ixv = cmd.selectedSurveyPointIndices;
  if (ixv.empty())
    return;

  if (ixv.size() == 1) {
    const SurveyPoint& p = cmd.surveyPoints[static_cast<size_t>(ixv.front())];
    ImGui::TextUnformatted("Survey — 1 point");
    if (ImGui::BeginTable("props_pick_survey", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 0.42f);
      ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.58f);
      PropRow("Point ID", std::to_string(p.id));
      char nbuf[64];
      std::snprintf(nbuf, sizeof(nbuf), "%.6f", static_cast<double>(p.northing));
      PropRow("Northing (Y)", std::string(nbuf));
      std::snprintf(nbuf, sizeof(nbuf), "%.6f", static_cast<double>(p.easting));
      PropRow("Easting (X)", std::string(nbuf));
      std::snprintf(nbuf, sizeof(nbuf), "%.4f", static_cast<double>(p.elevation));
      PropRow("Elevation", std::string(nbuf));
      PropRow("Layer", p.layer.empty() ? std::string("—") : p.layer);
      PropRow("Description", p.description.empty() ? std::string("—") : p.description);
      ImGui::EndTable();
    }
    return;
  }

  ImGui::Text("Survey — %zu points", ixv.size());
  ImGui::Separator();
  for (int ix : ixv) {
    if (ix >= 0 && static_cast<size_t>(ix) < cmd.surveyPoints.size())
      ImGui::BulletText("ID %d", cmd.surveyPoints[static_cast<size_t>(ix)].id);
  }
}

} // namespace

void DrawPropertiesPanel(AppCommandState& cmd) {
  ImGui::SetNextWindowSize(ImVec2(320, 560), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Properties", nullptr)) {
    ImGui::End();
    return;
  }

  auto& svyIx = cmd.selectedSurveyPointIndices;
  svyIx.erase(std::remove_if(svyIx.begin(), svyIx.end(),
                             [&](int ix) { return ix < 0 || static_cast<size_t>(ix) >= cmd.surveyPoints.size(); }),
              svyIx.end());

  const auto& sel = cmd.selection;
  const bool haveSurveyPick = !svyIx.empty();
  const bool haveCadSel = !sel.empty();

  if (!haveSurveyPick && !haveCadSel) {
    gPropsSelFingerprint = ~0ull;
    ImGui::TextDisabled("No selection.");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Fence: left→right = window (fully inside); right→left = crossing (touches fence). Includes COGO points. "
        "Click a marker to tweak survey set; Shift subtracts. Shift+fence subtracts. ESC clears.");
    ImGui::End();
    return;
  }

  if (haveSurveyPick) {
    DrawSurveyPointPickProps(cmd);
    ImGui::Separator();
  }

  if (!haveCadSel) {
    gPropsSelFingerprint = ~0ull;
    ImGui::TextDisabled("Bulk editing: VIEWPOINTS (VWPTS).");
    ImGui::End();
    return;
  }

  EnsureAttrCounts(cmd);

  const uint64_t fp = SelectionFingerprint(sel);
  if (fp != gPropsSelFingerprint) {
    gPropsSelFingerprint = fp;
    RefreshPropsBuffersFromModel(cmd, sel);
  }

  int nLine = 0;
  int nCirc = 0;
  int nAnn = 0;
  for (const auto& e : sel) {
    if (e.type == SelectedEntity::Type::LineSeg)
      ++nLine;
    else if (e.type == SelectedEntity::Type::Circle)
      ++nCirc;
    else if (e.type == SelectedEntity::Type::Annotation)
      ++nAnn;
  }

  ImGui::Text("Selected: %d object(s)", static_cast<int>(sel.size()));
  const int typeKinds = (nLine > 0 ? 1 : 0) + (nCirc > 0 ? 1 : 0) + (nAnn > 0 ? 1 : 0);
  if (typeKinds > 1)
    ImGui::TextDisabled("(Mixed: Line %d, Circle %d, Annotation %d)", nLine, nCirc, nAnn);
  else if (nLine > 1)
    ImGui::TextDisabled("%d lines", nLine);
  else if (nCirc > 1)
    ImGui::TextDisabled("%d circles", nCirc);
  else if (nAnn > 1)
    ImGui::TextDisabled("%d annotations", nAnn);
  else if (nLine == 1)
    ImGui::TextDisabled("Line");
  else if (nCirc == 1)
    ImGui::TextDisabled("Circle");
  else if (nAnn == 1) {
    int ix = -1;
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::Annotation) {
        ix = e.index;
        break;
      }
    }
    if (ix >= 0 && static_cast<size_t>(ix) < cmd.cadAnnotations.size())
      ImGui::TextDisabled(cmd.cadAnnotations[static_cast<size_t>(ix)].kind == CadAnnotation::Kind::Text ? "TEXT"
                                                                                                         : "MTEXT");
    else
      ImGui::TextDisabled("Annotation");
  }

  ImGui::Separator();

  DrawEditableGeneralSection(cmd, sel);

  if (nLine == 0 && nCirc == 0 && nAnn > 0) {
    std::vector<SelectedEntity> annOnly;
    annOnly.reserve(static_cast<size_t>(nAnn));
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::Annotation)
        annOnly.push_back(e);
    }
    if (annOnly.size() == 1)
      DrawSingleAnnotationGeometryEditable(cmd, annOnly.front().index);
    else
      DrawAnnotationGeometryOnly(cmd, annOnly);
  } else if (nCirc == 0 && nAnn == 0 && nLine > 0) {
    std::vector<SelectedEntity> linesOnly;
    linesOnly.reserve(static_cast<size_t>(nLine));
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::LineSeg)
        linesOnly.push_back(e);
    }
    if (linesOnly.size() == 1)
      DrawSingleLineGeometryEditable(cmd, linesOnly.front().index);
    else
      DrawLineGeometryOnly(cmd, linesOnly);
  } else if (nLine == 0 && nAnn == 0 && nCirc > 0) {
    std::vector<SelectedEntity> circOnly;
    circOnly.reserve(static_cast<size_t>(nCirc));
    for (const auto& e : sel) {
      if (e.type == SelectedEntity::Type::Circle)
        circOnly.push_back(e);
    }
    if (circOnly.size() == 1)
      DrawSingleCircleGeometryEditable(cmd, circOnly.front().index);
    else
      DrawCircleGeometryOnly(cmd, circOnly);
  } else {
    if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::TextWrapped("Mixed entity types — geometry is read-only here. Edit General above, or select only "
                         "lines, circles, or annotations.");
    }
  }

  if (haveSurveyPick) {
    ImGui::Separator();
    ImGui::TextDisabled("Survey bulk edit: VIEWPOINTS (VWPTS).");
  }

  ImGui::End();
}

namespace {

static bool gPolarTrackingEnabled = false;

void PushModeToggleButtonColors(bool on) {
  if (on) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.72f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.50f, 0.82f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.36f, 0.62f, 1.f));
  }
}

void PopModeToggleButtonColors(bool on) {
  if (on)
    ImGui::PopStyleColor(3);
}

static void ItemHelpTooltip(const char* text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

/// \p modelUnitsPerPlottedInch matches common civil notation (e.g. 50 → 1"=50' when model unit is feet).
static void DrawPlotScaleCombo(AppCommandState& cmd) {
  static constexpr struct {
    const char* label;
    float modelUnitsPerPlottedInch;
  } kScales[] = {
      {"1\" = 1'", 1.f},       {"1\" = 2'", 2.f},       {"1\" = 5'", 5.f},       {"1\" = 10'", 10.f},
      {"1\" = 20'", 20.f},     {"1\" = 30'", 30.f},     {"1\" = 40'", 40.f},     {"1\" = 50'", 50.f},
      {"1\" = 60'", 60.f},     {"1\" = 80'", 80.f},     {"1\" = 100'", 100.f},   {"1\" = 120'", 120.f},
      {"1\" = 200'", 200.f},   {"1\" = 300'", 300.f},   {"1\" = 400'", 400.f},   {"1\" = 500'", 500.f},
  };

  constexpr int kN = static_cast<int>(sizeof(kScales) / sizeof(kScales[0]));
  int selected = -1;
  for (int i = 0; i < kN; ++i) {
    if (std::fabs(cmd.modelUnitsPerPlottedInch - kScales[i].modelUnitsPerPlottedInch) < 0.051f) {
      selected = i;
      break;
    }
  }

  char preview[96];
  if (selected >= 0)
    std::snprintf(preview, sizeof(preview), "%s", kScales[selected].label);
  else
    std::snprintf(preview, sizeof(preview), "1\" = %.3g' (custom)", static_cast<double>(cmd.modelUnitsPerPlottedInch));

  ImGui::PushID("plotscalecombo");
  ImGui::SetNextItemWidth(158.f);
  if (ImGui::BeginCombo("##plotscale", preview, ImGuiComboFlags_HeightLargest)) {
    for (int i = 0; i < kN; ++i) {
      const bool isSel = (selected == i);
      if (ImGui::Selectable(kScales[i].label, isSel)) {
        cmd.modelUnitsPerPlottedInch = kScales[i].modelUnitsPerPlottedInch;
        BumpCadGpuCache(cmd);
      }
      if (isSel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ItemHelpTooltip("Drawing scale: model units per plotted inch (e.g. 50 for 1\" = 50'). "
                  "Use PSCALE for values not in the list.");
  ImGui::PopID();
}

} // namespace

static const char* CommandInputHint(const AppCommandState& cmd) {
  if (cmd.active == AppCommandState::Kind::Line) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.linePhase == AppCommandState::LinePhase::NeedFirstPoint)
      return "First point (click or X,Y):";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint && cmd.segmentAnglePickPhase == SAP::WaitP1)
      return "Bearing pick — first click:";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint && cmd.segmentAnglePickPhase == SAP::WaitP2)
      return "Bearing pick — second click:";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint &&
        cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
      return "Bearing pick — Enter or +90/-45:";
    if (cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint && cmd.segmentAngleLockActive)
      return "LINE distance ± / click ray / X,Y / A clears:";
    if (cmd.orthoMode)
      return "Next — click / X,Y / @dx,dy / ortho / A or AP:";
    return "Next — click / X,Y / @dx,dy / A or AP bearing:";
  }
  if (cmd.active == AppCommandState::Kind::Polyline) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedFirstPoint)
      return "POLYLINE first point:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint && cmd.segmentAnglePickPhase == SAP::WaitP1)
      return "POLYLINE bearing pick — first click:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint && cmd.segmentAnglePickPhase == SAP::WaitP2)
      return "POLYLINE bearing pick — second click:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint &&
        cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
      return "POLYLINE bearing — Enter or +90/-45:";
    if (cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint && cmd.segmentAngleLockActive)
      return "POLYLINE distance ± / ray click / A clears / CLOSE:";
    if (cmd.orthoMode)
      return "POLYLINE next — ortho / X,Y / A / AP / CLOSE:";
    return "POLYLINE next — X,Y / A / AP / CLOSE:";
  }
  if (cmd.active == AppCommandState::Kind::Arc) {
    switch (cmd.arcPhase) {
    case AppCommandState::ArcPhase::WaitStart:
      return "ARC start:";
    case AppCommandState::ArcPhase::WaitMid:
      return "ARC mid:";
    case AppCommandState::ArcPhase::WaitEnd:
      return "ARC end:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Ellipse) {
    switch (cmd.ellPhase) {
    case AppCommandState::EllipsePhase::WaitCenter:
      return "ELLIPSE center:";
    case AppCommandState::EllipsePhase::WaitMajorEnd:
      return "ELLIPSE axis end:";
    case AppCommandState::EllipsePhase::WaitRatio:
      return "ELLIPSE ratio (cmd line):";
    }
  }
  if (cmd.active == AppCommandState::Kind::Text) {
    switch (cmd.textPhase) {
    case AppCommandState::TextCmdPhase::WaitInsertion:
      return "TEXT insertion X,Y:";
    case AppCommandState::TextCmdPhase::WaitHeight:
      return "TEXT height:";
    case AppCommandState::TextCmdPhase::WaitRotation:
      return "TEXT rotation ° CW from N:";
    case AppCommandState::TextCmdPhase::WaitString:
      return "TEXT content:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Mtext) {
    switch (cmd.mtextPhase) {
    case AppCommandState::MtextPhase::WaitCorner1:
      return "MTEXT corner 1:";
    case AppCommandState::MtextPhase::WaitCorner2:
      return "MTEXT corner 2:";
    case AppCommandState::MtextPhase::WaitString:
      return "MTEXT text:";
    }
  }
  if (cmd.active == AppCommandState::Kind::DimAligned) {
    switch (cmd.dimPhase) {
    case AppCommandState::DimPhase::WaitExt1:
      return "DIM ext 1:";
    case AppCommandState::DimPhase::WaitExt2:
      return "DIM ext 2:";
    case AppCommandState::DimPhase::WaitDimLinePt:
      return "DIM line pt:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Circle) {
    using CP = AppCommandState::CirclePhase;
    switch (cmd.circlePhase) {
    case CP::WaitCenterOrMode:
      return "Center or type 3P:";
    case CP::WaitRadius:
      return "Radius, D+diameter, or click:";
    case CP::ThreeP_WaitP1:
      return "3P — point 1:";
    case CP::ThreeP_WaitP2:
      return "3P — point 2:";
    case CP::ThreeP_WaitP3:
      return "3P — point 3:";
    }
  }
  if (cmd.active == AppCommandState::Kind::Move || cmd.active == AppCommandState::Kind::Copy) {
    using MP = AppCommandState::ModifyPhase;
    if (cmd.modifyPhase == MP::PickSelection)
      return "Window opposite corner or cancel:";
    if (cmd.modifyPhase == MP::NeedBase)
      return "Base point X,Y:";
    return "Destination @dx,dy or X,Y:";
  }
  if (cmd.active == AppCommandState::Kind::Rotate) {
    using RP = AppCommandState::RotatePhase;
    switch (cmd.rotatePhase) {
    case RP::PickSelection:
      return "Window opposite corner:";
    case RP::NeedBase:
      return "Base point X,Y:";
    case RP::NeedAngleOrReference:
      return "° CW from north / DMS / R / C (copy):";
    case RP::Ref_WaitP1:
    case RP::Ref_WaitP2:
      return "Reference point X,Y (C toggles copy):";
    case RP::AfterReference_WaitAngleOrP:
      return "Bearing ° from north / DMS / P / C (copy):";
    case RP::AnglePoints_WaitP1:
    case RP::AnglePoints_WaitP2:
      return "Angle point X,Y (C toggles copy):";
    }
  }
  if (cmd.active == AppCommandState::Kind::Delete)
    return "DELETE — window opposite corner or ESC:";
  if (cmd.active == AppCommandState::Kind::Join)
    return "JOIN — window opposite corner or ESC:";
  if (cmd.active == AppCommandState::Kind::Trim) {
    using TP = AppCommandState::TrimPhase;
    if (cmd.trimPhase == TP::SelectCuttingEdges)
      return "TRIM — cutting edges, Enter (or L = draw on segment, two clicks):";
    if (cmd.trimPhase == TP::CuttingLine_WaitP1)
      return "TRIM line-trim — first point:";
    if (cmd.trimPhase == TP::CuttingLine_WaitP2)
      return "TRIM line-trim — second point (finishes trim):";
    return "TRIM — click to trim (near end to remove), Enter when done:";
  }
  if (cmd.active == AppCommandState::Kind::Zoom)
    return "ZOOM WINDOW — opposite corner or ESC:";
  return "Command:";
}

void DrawCommandLinePanel(std::vector<std::string>& log, char* cmdBuf, int cmdBufSize, AppCommandState& cmd,
                          float cursorX, float cursorY, float cursorZ, bool* object_snap_enabled,
                          bool* ortho_mode_enabled, bool* grid_visible) {
  ImGui::SetNextWindowSize(ImVec2(900, 220), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Command line", nullptr)) {
    ImGui::End();
    return;
  }

  const char* circFooter = CircleCommandFooterHint(cmd);
  const char* lineFooter = LineCommandFooterHint(cmd);
  const char* modFooter = ModifyCommandFooterHint(cmd);
  const char* rotFooter = RotateCommandFooterHint(cmd);
  const char* delFooter = DeleteCommandFooterHint(cmd);
  const char* joinFooter = JoinCommandFooterHint(cmd);
  const char* trimFooter = TrimCommandFooterHint(cmd);
  const char* zmFooter = ZoomCommandFooterHint(cmd);
  const char* drawXFooter = DrawingExtrasFooterHint(cmd);

  std::vector<std::string> fuzzMatches;
  if (cmd.active == AppCommandState::Kind::None && cmdBuf[0] != '\0')
    fuzzMatches = FuzzyCommandMatches(cmdBuf, 8);

  auto footerNonEmpty = [](const char* s) { return s && s[0] != '\0'; };
  int footerBudgetLines = 0;
  if (footerNonEmpty(circFooter))
    footerBudgetLines += 2;
  if (footerNonEmpty(lineFooter))
    footerBudgetLines += 2;
  if (footerNonEmpty(modFooter))
    footerBudgetLines += 2;
  if (footerNonEmpty(rotFooter))
    footerBudgetLines += 2;
  if (footerNonEmpty(delFooter))
    footerBudgetLines += 2;
  if (footerNonEmpty(joinFooter))
    footerBudgetLines += 2;
  if (footerNonEmpty(trimFooter))
    footerBudgetLines += 2;
  if (footerNonEmpty(zmFooter))
    footerBudgetLines += 2;
  if (footerNonEmpty(drawXFooter))
    footerBudgetLines += 2;
  if (!fuzzMatches.empty())
    footerBudgetLines += 2;
  footerBudgetLines = std::min(footerBudgetLines, 28);

  const ImGuiStyle& st = ImGui::GetStyle();
  const float lineH = ImGui::GetTextLineHeightWithSpacing();
  const float frameH = ImGui::GetFrameHeightWithSpacing();
  const float statusStripH = ImGui::GetFrameHeight() + 2.f;
  const float sendBtnW = ImGui::CalcTextSize("Send").x + st.FramePadding.x * 2.f + 8.f;
  const float footerH = frameH * 2.15f + lineH * static_cast<float>(footerBudgetLines) + statusStripH +
                          st.ItemSpacing.y * 2.f + 4.f;

  ImGui::BeginChild("CmdScroll", ImVec2(0, -footerH), true, ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto& line : log)
    ImGui::TextUnformatted(line.c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    ImGui::SetScrollHereY(1.f);
  ImGui::EndChild();

  ImGui::Separator();
  ImGui::PushID("GoSurveyCmdPanel");

  ImGuiIO& io = ImGui::GetIO();
  if (!io.WantTextInput && io.InputQueueCharacters.Size > 0) {
    RouteQueuedCharsToCmdBuf(cmdBuf, cmdBufSize, io);
    ImGui::SetKeyboardFocusHere(0);
  }

  const float inputAvailW = ImGui::GetContentRegionAvail().x;
  ImGui::SetNextItemWidth(std::max(64.f, inputAvailW - sendBtnW - st.ItemSpacing.x));
  ImGuiInputTextFlags flags =
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways;
  const bool exec = ImGui::InputTextWithHint("##CommandLineInput", CommandInputHint(cmd), cmdBuf,
                                             static_cast<size_t>(cmdBufSize), flags, CommandLineInputCallback, nullptr);
  ImGui::SetItemDefaultFocus();
  ImGui::SameLine(0, st.ItemSpacing.x);
  if (ImGui::Button("Send", ImVec2(sendBtnW, 0.f)) || exec)
    ProcessCommandLineSubmit(cmdBuf, cmdBufSize, cmd, log);

  if (footerNonEmpty(circFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", circFooter);
    ImGui::PopStyleColor();
  }
  if (footerNonEmpty(lineFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", lineFooter);
    ImGui::PopStyleColor();
  }
  if (footerNonEmpty(modFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", modFooter);
    ImGui::PopStyleColor();
  }
  if (footerNonEmpty(rotFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", rotFooter);
    ImGui::PopStyleColor();
  }
  if (footerNonEmpty(delFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", delFooter);
    ImGui::PopStyleColor();
  }
  if (footerNonEmpty(joinFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", joinFooter);
    ImGui::PopStyleColor();
  }
  if (footerNonEmpty(trimFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", trimFooter);
    ImGui::PopStyleColor();
  }
  if (footerNonEmpty(zmFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", zmFooter);
    ImGui::PopStyleColor();
  }
  if (footerNonEmpty(drawXFooter)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", drawXFooter);
    ImGui::PopStyleColor();
  }

  if (!fuzzMatches.empty()) {
    std::string joined;
    for (size_t i = 0; i < fuzzMatches.size(); ++i) {
      if (i)
        joined += ", ";
      joined += fuzzMatches[i];
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Matches: %s", joined.c_str());
    ImGui::PopStyleColor();
  }

  ImGui::PopID();

  ImGui::Separator();
  const float statusBtnH = ImGui::GetFrameHeight();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
  ImGui::BeginChild("StatusBarStrip", ImVec2(0, statusBtnH), false, ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.f, 0.f));

  if (object_snap_enabled) {
    const bool on = *object_snap_enabled;
    PushModeToggleButtonColors(on);
    if (ImGui::Button("OSNAP", ImVec2(0.f, statusBtnH)))
      *object_snap_enabled = !*object_snap_enabled;
    PopModeToggleButtonColors(on);
    ItemHelpTooltip("Object snap — endpoint, mid, center, perpendicular (F3)");
    ImGui::SameLine(0, 4);
  }
  if (ortho_mode_enabled) {
    const bool on = *ortho_mode_enabled;
    PushModeToggleButtonColors(on);
    if (ImGui::Button("ORTHO", ImVec2(0.f, statusBtnH)))
      *ortho_mode_enabled = !*ortho_mode_enabled;
    PopModeToggleButtonColors(on);
    ItemHelpTooltip("Ortho mode — constrain to horizontal / vertical (F8)");
    ImGui::SameLine(0, 4);
  }
  if (grid_visible) {
    const bool on = *grid_visible;
    PushModeToggleButtonColors(on);
    if (ImGui::Button("GRID", ImVec2(0.f, statusBtnH)))
      *grid_visible = !*grid_visible;
    PopModeToggleButtonColors(on);
    ItemHelpTooltip("Drawing grid");
    ImGui::SameLine(0, 4);
  }
  {
    const bool on = gPolarTrackingEnabled;
    PushModeToggleButtonColors(on);
    if (ImGui::Button("POLAR", ImVec2(0.f, statusBtnH)))
      gPolarTrackingEnabled = !gPolarTrackingEnabled;
    PopModeToggleButtonColors(on);
    ItemHelpTooltip("Polar tracking (UI only for now)");
    ImGui::SameLine(0, 4);
  }

  DrawPlotScaleCombo(cmd);
  ImGui::SameLine(0, 10);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("|");
  ImGui::SameLine(0, 8);
  ImGui::AlignTextToFramePadding();
  ImGui::Text("X %.3f  Y %.3f  Z %.3f  |  UCS: World", cursorX, cursorY, cursorZ);

  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::PopStyleVar();

  ImGui::End();
}

static int HitTestMtextGrip(float mouseSx, float mouseSy, ImVec2 imgPos, ImVec2 avail, float worldLeft,
                            float worldRight, float worldBottom, float worldTop, const CadAnnotation& ann,
                            float gripRadiusPx) {
  if (ann.kind != CadAnnotation::Kind::Mtext)
    return -1;
  const float wx[4] = {ann.boxMinX, ann.boxMaxX, ann.boxMaxX, ann.boxMinX};
  const float wy[4] = {ann.boxMinY, ann.boxMinY, ann.boxMaxY, ann.boxMaxY};
  const float denx = worldRight - worldLeft + 1e-12f;
  const float deny = worldTop - worldBottom + 1e-12f;
  const float r2 = gripRadiusPx * gripRadiusPx;
  for (int i = 0; i < 4; ++i) {
    const float u = (wx[i] - worldLeft) / denx;
    const float v = (worldTop - wy[i]) / deny;
    const float sx = imgPos.x + u * avail.x;
    const float sy = imgPos.y + v * avail.y;
    const float dx = mouseSx - sx;
    const float dy = mouseSy - sy;
    if (dx * dx + dy * dy <= r2)
      return i;
  }
  return -1;
}

void DrawDrawingViewport(unsigned int viewportTextureId, AppCommandState& cmd, std::vector<std::string>& log,
                         float* panX, float* panY, float* zoom, float* outCursorX, float* outCursorY,
                         float* outCursorRawX, float* outCursorRawY, int* outFbW, int* outFbH,
                         bool object_snap_enabled, CadSnap::Hit* out_snap) {
  ImGui::SetNextWindowSize(ImVec2(900, 650), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Drawing1", nullptr)) {
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("Model");
  ImGui::SameLine();
  ImGui::Button("Layout1");
  ImGui::SameLine();
  ImGui::Button("Layout2");

  ImGui::Separator();

  ImVec2 avail = ImGui::GetContentRegionAvail();
  avail.y = std::max(avail.y, 80.f);
  ImVec2 imgPos = ImGui::GetCursorScreenPos();

  const float aspect = avail.x / std::max(avail.y, 1.f);

  ImGui::Image(static_cast<ImTextureID>(static_cast<std::intptr_t>(viewportTextureId)), avail, ImVec2(0, 1),
               ImVec2(1, 0));

  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const float mx = mouse.x - imgPos.x;
  const float my = mouse.y - imgPos.y;

  if (hovered) {
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.f && mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y) {
      const float u = mx / std::max(avail.x, 1.f);
      const float v = my / std::max(avail.y, 1.f);
      const float z0 = *zoom;
      const float halfH0 = (1.f / std::max(z0, 1.e-4f)) * 50.f;
      // Continuous zoom (works with fractional MouseWheel deltas; less "stair-step" than fixed pow steps).
      const float z1 = std::clamp(z0 * std::exp(wheel * 0.14f), 1.e-4f, 1.e5f);
      const float halfH1 = (1.f / std::max(z1, 1.e-4f)) * 50.f;
      const float dh = halfH0 - halfH1;
      *panX += (u - 0.5f) * 2.f * aspect * dh;
      *panY += dh * (1.f - 2.f * v);
      *zoom = z1;
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      ImVec2 d = ImGui::GetIO().MouseDelta;
      const float aspect = avail.x / std::max(avail.y, 1.f);
      const float halfH = (1.f / std::max(*zoom, 1.e-4f)) * 50.f;
      const float halfW = halfH * aspect;
      *panX -= (d.x / std::max(avail.x, 1.f)) * (2.f * halfW);
      *panY += (d.y / std::max(avail.y, 1.f)) * (2.f * halfH);
    }
  }

  const float halfH = (1.f / std::max(*zoom, 1.e-4f)) * 50.f;
  const float halfW = halfH * aspect;
  const float worldLeft = -halfW + *panX;
  const float worldRight = halfW + *panX;
  const float worldBottom = -halfH + *panY;
  const float worldTop = halfH + *panY;

  if (cmd.mtextGripAnnotationIndex >= 0 && hovered && mx >= 0.f && mx < avail.x && my >= 0.f && my < avail.y) {
    const float u = mx / std::max(avail.x, 1.f);
    const float v = my / std::max(avail.y, 1.f);
    const float curWx = worldLeft + u * (worldRight - worldLeft);
    const float curWy = worldTop - v * (worldTop - worldBottom);
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      ClearMtextGripInteraction(cmd);
      BumpCadGpuCache(cmd);
    } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      const size_t gi = static_cast<size_t>(cmd.mtextGripAnnotationIndex);
      if (gi < cmd.cadAnnotations.size()) {
        CadAnnotation& ann = cmd.cadAnnotations[gi];
        if (ann.kind == CadAnnotation::Kind::Mtext) {
          const float fx = cmd.mtextGripFixedCornerX;
          const float fy = cmd.mtextGripFixedCornerY;
          ann.boxMinX = std::min(fx, curWx);
          ann.boxMaxX = std::max(fx, curWx);
          ann.boxMinY = std::min(fy, curWy);
          ann.boxMaxY = std::max(fy, curWy);
          ann.insX = ann.boxMinX;
          ann.insY = ann.boxMinY;
        }
      }
    }
  }

  if (out_snap)
    *out_snap = {};

  if (hovered && mx >= 0 && mx < avail.x && my >= 0 && my < avail.y) {
    const float u = mx / std::max(avail.x, 1.f);
    const float v = my / std::max(avail.y, 1.f);
    const float rawX = worldLeft + u * (worldRight - worldLeft);
    const float rawY = worldTop - v * (worldTop - worldBottom);

    if (outCursorRawX)
      *outCursorRawX = rawX;
    if (outCursorRawY)
      *outCursorRawY = rawY;

    CadSnap::Hit snap{};
    if (object_snap_enabled) {
      const float tol = CadSnap::WorldToleranceFromPixels(avail.y, halfH, 14.f);
      const bool midCmd = cmd.active != AppCommandState::Kind::None;
      snap = CadSnap::FindBest(rawX, rawY, cmd, midCmd, tol);
    }

    if (out_snap && snap.valid)
      *out_snap = snap;

    if (snap.valid) {
      *outCursorX = snap.x;
      *outCursorY = snap.y;
    } else {
      *outCursorX = rawX;
      *outCursorY = rawY;
    }
  }

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mx >= 0 && mx < avail.x && my >= 0 &&
      my < avail.y) {
    using K = AppCommandState::Kind;
    using MP = AppCommandState::ModifyPhase;
    using RP = AppCommandState::RotatePhase;

    const float uPick = mx / std::max(avail.x, 1.f);
    const float vPick = my / std::max(avail.y, 1.f);
    const float rawPickX = worldLeft + uPick * (worldRight - worldLeft);
    const float rawPickY = worldTop - vPick * (worldTop - worldBottom);

    const bool useRawWorldForWindowRect =
        cmd.active == K::None || cmd.active == K::Delete || cmd.active == K::Join || cmd.active == K::Trim ||
        cmd.active == K::Zoom || (cmd.active == K::Move && cmd.modifyPhase == MP::PickSelection) ||
        (cmd.active == K::Rotate && cmd.rotatePhase == RP::PickSelection);
    const float wxPick = useRawWorldForWindowRect ? rawPickX : *outCursorX;
    const float wyPick = useRawWorldForWindowRect ? rawPickY : *outCursorY;
    const bool keyShift = ImGui::GetIO().KeyShift;
    constexpr float kFenceDirTolPx = 3.f;
    const float fenceDragDx = mx - cmd.selBoxAnchorScreenX;
    const bool fenceWindowMode = fenceDragDx > kFenceDirTolPx;

    if (cmd.createPointsPlacementActive && cmd.active == K::None) {
      const int hitIx =
          cmd.surveyPoints.empty()
              ? -1
              : PickSurveyPointIndex(cmd.surveyPoints, rawPickX, rawPickY, halfH, avail.y);
      if (hitIx >= 0) {
        ClearCadSelection(cmd);
        ApplySurveyPointClickSelection(cmd, hitIx, keyShift, &log);
      } else {
        TryPlaceSurveyPoint(cmd, *outCursorX, *outCursorY, cmd.createPointsOpts.defaultElevation, log);
      }
    } else if (cmd.active == K::Line || cmd.active == K::Circle || cmd.active == K::Polyline ||
               cmd.active == K::Arc || cmd.active == K::Ellipse || cmd.active == K::Text ||
               cmd.active == K::Mtext || cmd.active == K::DimAligned)
      SubmitViewportPick(cmd, *outCursorX, *outCursorY, log);
    else if (cmd.active == K::Move || cmd.active == K::Copy) {
      if (cmd.modifyPhase == MP::PickSelection) {
        if (!cmd.selBoxWaitingSecond)
          BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
        else
          SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
      } else
        SubmitViewportPick(cmd, *outCursorX, *outCursorY, log);
    } else if (cmd.active == K::Rotate) {
      if (cmd.rotatePhase == RP::PickSelection) {
        if (!cmd.selBoxWaitingSecond)
          BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
        else
          SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
      } else
        SubmitViewportPick(cmd, *outCursorX, *outCursorY, log);
    } else if (cmd.active == K::Delete) {
      if (!cmd.selBoxWaitingSecond)
        BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
      else
        SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
    } else if (cmd.active == K::Join) {
      if (!cmd.selBoxWaitingSecond)
        BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
      else
        SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
    } else if (cmd.active == K::Trim) {
      const float trimTol = CadSnap::WorldToleranceFromPixels(avail.y, halfH, 14.f);
      using TP = AppCommandState::TrimPhase;
      const bool trimCutLinePt =
          cmd.trimPhase == TP::CuttingLine_WaitP1 || cmd.trimPhase == TP::CuttingLine_WaitP2;
      const float tx = trimCutLinePt ? *outCursorX : rawPickX;
      const float ty = trimCutLinePt ? *outCursorY : rawPickY;
      SubmitTrimViewportPick(cmd, tx, ty, trimTol, log);
    } else if (cmd.active == K::Zoom) {
      if (!cmd.selBoxWaitingSecond)
        BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
      else
        SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
    } else if (cmd.active == K::None) {
      bool handled = false;
      int gripCorner = -1;
      if (cmd.selection.size() == 1 && cmd.selection[0].type == SelectedEntity::Type::Annotation) {
        const int aix = cmd.selection[0].index;
        if (aix >= 0 && static_cast<size_t>(aix) < cmd.cadAnnotations.size() &&
            cmd.cadAnnotations[static_cast<size_t>(aix)].kind == CadAnnotation::Kind::Mtext) {
          gripCorner =
              HitTestMtextGrip(mouse.x, mouse.y, imgPos, avail, worldLeft, worldRight, worldBottom, worldTop,
                               cmd.cadAnnotations[static_cast<size_t>(aix)], 10.f);
        }
      }
      if (gripCorner >= 0) {
        const int aix = cmd.selection[0].index;
        cmd.mtextGripAnnotationIndex = aix;
        cmd.mtextGripCorner = gripCorner;
        static const int kOpp[4] = {2, 3, 0, 1};
        const int opp = kOpp[gripCorner];
        const CadAnnotation& ann = cmd.cadAnnotations[static_cast<size_t>(aix)];
        const float cx[4] = {ann.boxMinX, ann.boxMaxX, ann.boxMaxX, ann.boxMinX};
        const float cy[4] = {ann.boxMinY, ann.boxMinY, ann.boxMaxY, ann.boxMaxY};
        cmd.mtextGripFixedCornerX = cx[opp];
        cmd.mtextGripFixedCornerY = cy[opp];
        handled = true;
      }
      if (!handled) {
        const int annIx = PickCadAnnotationAt(rawPickX, rawPickY, cmd, halfH, avail.y);
        if (annIx >= 0) {
          ClearMtextGripInteraction(cmd);
          SelectedEntity se;
          se.type = SelectedEntity::Type::Annotation;
          se.index = annIx;
          if (keyShift) {
            auto it = std::find_if(cmd.selection.begin(), cmd.selection.end(), [&](const SelectedEntity& x) {
              return x.type == SelectedEntity::Type::Annotation && x.index == annIx;
            });
            if (it != cmd.selection.end())
              cmd.selection.erase(it);
            else
              cmd.selection.push_back(se);
          } else {
            ClearCadSelection(cmd);
            cmd.selection.push_back(se);
          }
          EnsureAttrCounts(cmd);
          cmd.selBoxWaitingSecond = false;
          handled = true;
        }
      }
      if (!handled) {
        if (!cmd.surveyPoints.empty()) {
          const int hitIx = PickSurveyPointIndex(cmd.surveyPoints, rawPickX, rawPickY, halfH, avail.y);
          if (hitIx >= 0) {
            ClearCadSelection(cmd);
            ApplySurveyPointClickSelection(cmd, hitIx, keyShift, &log);
          } else if (!cmd.selBoxWaitingSecond)
            BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
          else
            SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
        } else {
          if (!cmd.selBoxWaitingSecond)
            BeginSelectionBoxCorner(cmd, wxPick, wyPick, mx, my);
          else
            SubmitViewportPick(cmd, wxPick, wyPick, log, keyShift, fenceWindowMode);
        }
      }
    }
  }

  std::vector<CadAnnotation> transformAnnPreviews;
  if (outCursorX && outCursorY)
    CadAnnotationCollectTransformPreviews(cmd, *outCursorX, *outCursorY, &transformAnnPreviews);

  using AK = AppCommandState::Kind;
  using AMP = AppCommandState::MtextPhase;
  const bool showMtextCmdDraft =
      cmd.active == AK::Mtext && cmd.mtextPhase == AMP::WaitString;

  if (!cmd.cadAnnotations.empty() || !transformAnnPreviews.empty() || showMtextCmdDraft) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto worldToScreen = [&](float wx, float wy, ImVec2* out) {
      const float denx = worldRight - worldLeft + 1e-12f;
      const float deny = worldTop - worldBottom + 1e-12f;
      const float u = (wx - worldLeft) / denx;
      const float v = (worldTop - wy) / deny;
      out->x = imgPos.x + u * avail.x;
      out->y = imgPos.y + v * avail.y;
    };
    const float worldPerPxY = (worldTop - worldBottom) / std::max(avail.y, 1.f);
    constexpr ImU32 kAnnCol = IM_COL32(230, 232, 238, 255);
    constexpr ImU32 kAnnTfPrevCol = IM_COL32(160, 220, 255, 130);
    constexpr ImU32 kMtextDraftCol = IM_COL32(210, 200, 140, 200);
    constexpr ImU32 kAnnSelCol = IM_COL32(120, 200, 255, 255);
    constexpr ImU32 kGripFill = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 kGripBorder = IM_COL32(60, 120, 220, 255);
    ImFont* font = ImGui::GetFont();

    auto drawAnnotationVisual = [&](const CadAnnotation& a, ImU32 col) {
      const float hWorld = CadAnnotationHeightWorld(a, cmd.modelUnitsPerPlottedInch);
      if (a.kind == CadAnnotation::Kind::Text) {
        ImVec2 sp{};
        worldToScreen(a.insX, a.insY, &sp);
        const float fontPx = std::clamp(hWorld / std::max(worldPerPxY, 1.e-6f), 10.f, 160.f);
        dl->AddText(font, fontPx, sp, col, a.text.c_str());
      } else {
        ImVec2 sa{}, sb{};
        worldToScreen(a.boxMinX, a.boxMinY, &sa);
        worldToScreen(a.boxMaxX, a.boxMaxY, &sb);
        const float rx0 = std::min(sa.x, sb.x);
        const float ry0 = std::min(sa.y, sb.y);
        const float rx1 = std::max(sa.x, sb.x);
        const float ry1 = std::max(sa.y, sb.y);
        const float fontPx = std::clamp(hWorld / std::max(worldPerPxY, 1.e-6f), 10.f, 128.f);
        const float wrapW = std::max(8.f, rx1 - rx0 - 8.f);
        dl->PushClipRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1), true);
        dl->AddText(font, fontPx, ImVec2(rx0 + 4.f, ry0 + 4.f), col, a.text.c_str(), nullptr, wrapW);
        dl->PopClipRect();
      }
    };

    auto isAnnSelected = [&](size_t ix) {
      for (const auto& e : cmd.selection) {
        if (e.type == SelectedEntity::Type::Annotation && static_cast<size_t>(e.index) == ix)
          return true;
      }
      return false;
    };

    for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai)
      drawAnnotationVisual(cmd.cadAnnotations[ai], kAnnCol);

    for (const CadAnnotation& ap : transformAnnPreviews)
      drawAnnotationVisual(ap, kAnnTfPrevCol);

    if (showMtextCmdDraft) {
      CadAnnotation d{};
      d.kind = CadAnnotation::Kind::Mtext;
      d.plottedHeightInches = std::max(cmd.defaultPlottedTextHeightInches, 1.e-6f);
      d.text = "MTEXT";
      d.boxMinX = std::min(cmd.mtxtX1, cmd.mtxtX2);
      d.boxMaxX = std::max(cmd.mtxtX1, cmd.mtxtX2);
      d.boxMinY = std::min(cmd.mtxtY1, cmd.mtxtY2);
      d.boxMaxY = std::max(cmd.mtxtY1, cmd.mtxtY2);
      d.insX = d.boxMinX;
      d.insY = d.boxMinY;
      drawAnnotationVisual(d, kMtextDraftCol);
    }

    const float gripHalf = 4.f;
    for (size_t ai = 0; ai < cmd.cadAnnotations.size(); ++ai) {
      const CadAnnotation& a = cmd.cadAnnotations[ai];
      if (a.kind != CadAnnotation::Kind::Mtext || !isAnnSelected(ai))
        continue;
      ImVec2 sa{}, sb{};
      worldToScreen(a.boxMinX, a.boxMinY, &sa);
      worldToScreen(a.boxMaxX, a.boxMaxY, &sb);
      const float rx0 = std::min(sa.x, sb.x);
      const float ry0 = std::min(sa.y, sb.y);
      const float rx1 = std::max(sa.x, sb.x);
      const float ry1 = std::max(sa.y, sb.y);
      dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1), kAnnSelCol, 0.f, 0, 2.f);
      const bool singleSel = cmd.selection.size() == 1 && cmd.selection[0].type == SelectedEntity::Type::Annotation &&
                             cmd.selection[0].index == static_cast<int>(ai);
      if (!singleSel)
        continue;
      const float wx[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
      const float wy[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
      for (int c = 0; c < 4; ++c) {
        ImVec2 gp{};
        worldToScreen(wx[c], wy[c], &gp);
        dl->AddRectFilled(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf),
                          kGripFill);
        dl->AddRect(ImVec2(gp.x - gripHalf, gp.y - gripHalf), ImVec2(gp.x + gripHalf, gp.y + gripHalf), kGripBorder,
                    0.f, 0, 1.f);
      }
    }
  }

  *outFbW = static_cast<int>(std::max(1.f, std::floor(avail.x)));
  *outFbH = static_cast<int>(std::max(1.f, std::floor(avail.y)));

  ImGui::End();
}

void DrawCreatePointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showCreatePointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(460, 560), ImGuiCond_FirstUseEver);
  bool open = cmd.showCreatePointsWindow;
  if (!ImGui::Begin("Create points", &open)) {
    cmd.showCreatePointsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showCreatePointsWindow = open;

  CreatePointsOptions& o = cmd.createPointsOpts;
  ImGui::TextWrapped("Stored coordinates match the drawing: Easting = world X, Northing = world Y.");
  ImGui::Separator();
  ImGui::InputInt("Starting point number", &o.startNumber);
  ImGui::Checkbox("Use sequential numbering", &o.sequentialNumbering);
  ImGui::InputInt("Point number offset", &o.pointNumberOffset);
  if (o.pointNumberOffset == 0)
    o.pointNumberOffset = 1;
  ImGui::InputInt("Sequence point numbers from", &o.sequenceNumbersFrom);
  ImGui::Separator();
  ImGui::TextUnformatted("Default fields for newly placed points");
  ImGui::InputText("Layer##cp_layer", &o.layer);
  ImGui::InputTextMultiline("Default description##cp_desc", &o.defaultDescription, ImVec2(-FLT_MIN, 72.f));
  ImGui::InputFloat("Default elevation##cp_z", &o.defaultElevation);

  int pol = static_cast<int>(o.duplicatePolicy);
  if (ImGui::Combo(
          "If point numbers exist",
          &pol,
          "Notify (skip duplicate)\0Renumber (next free ID)\0Merge (update coords/desc)\0Overwrite (replace "
          "row)\0\0"))
    o.duplicatePolicy = static_cast<SurveyDuplicatePolicy>(pol);

  ImGui::Separator();
  ImGui::TextUnformatted("Next ID used when you click the drawing:");
  ImGui::InputInt("##next_survey_id", &cmd.createPointsNextId);
  if (ImGui::Button("Set next ID from starting number"))
    ResetCreatePointsNextIdFromSettings(cmd);
  ImGui::SameLine();
  if (ImGui::Button("Set next ID from sequence-from"))
    cmd.createPointsNextId = o.sequenceNumbersFrom;

  ImGui::Checkbox("Place points with clicks on Drawing1", &cmd.createPointsPlacementActive);
  if (cmd.createPointsPlacementActive)
    ImGui::TextDisabled(
        "Clicks place points on empty ground; clicks on existing markers select/adjust survey picks instead. "
        "Requires idle CAD (no LINE/CIRCLE/MOVE…). ESC closes this panel and turns placement off.");

  ImGui::Separator();
  static char pathBuf[512] = "gosurvey_points.json";
  ImGui::InputText("Survey database file", pathBuf, sizeof(pathBuf));
  if (ImGui::Button("Save to file"))
    SaveSurveyPointsToJsonFile(cmd, pathBuf, log);
  ImGui::SameLine();
  if (ImGui::Button("Load from file"))
    LoadSurveyPointsFromJsonFile(cmd, pathBuf, log);

  ImGui::End();
}

void DrawViewPointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showViewPointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(960, 480), ImGuiCond_FirstUseEver);
  bool open = cmd.showViewPointsWindow;
  if (!ImGui::Begin("Viewpoints — survey database", &open)) {
    cmd.showViewPointsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showViewPointsWindow = open;

  cmd.surveyPointIdBuffers.resize(cmd.surveyPoints.size());
  for (size_t i = 0; i < cmd.surveyPoints.size(); ++i) {
    if (cmd.surveyPointIdBuffers[i].empty())
      cmd.surveyPointIdBuffers[i] = std::to_string(cmd.surveyPoints[i].id);
  }

  ImGui::Text("%zu point(s)", cmd.surveyPoints.size());
  static char pathBuf[512] = "gosurvey_points.json";
  ImGui::InputText("File##vp_path", pathBuf, sizeof(pathBuf));
  if (ImGui::Button("Save##vp"))
    SaveSurveyPointsToJsonFile(cmd, pathBuf, log);
  ImGui::SameLine();
  if (ImGui::Button("Load##vp"))
    LoadSurveyPointsFromJsonFile(cmd, pathBuf, log);

  ImGui::Separator();

  const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable;
  int pendingDelete = -1;
  if (ImGui::BeginTable("survey_pts", 7, tf, ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing()))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 64.f);
    ImGui::TableSetupColumn("Easting", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Northing", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Elev", ImGuiTableColumnFlags_WidthFixed, 84.f);
    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 56.f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < cmd.surveyPoints.size(); ++i) {
      SurveyPoint& p = cmd.surveyPoints[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(i));
      ImGui::SetNextItemWidth(96.f);
      ImGui::InputText("##id", &cmd.surveyPointIdBuffers[i]);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string t = TrimCopyUi(cmd.surveyPointIdBuffers[i]);
        char* end = nullptr;
        const long v = std::strtol(t.c_str(), &end, 10);
        const bool parsed =
            end == t.c_str() + static_cast<std::ptrdiff_t>(t.size()) && end != t.c_str();
        if (!parsed) {
          log.push_back("VIEWPOINTS — ID must be a whole number (no spaces or extra text).");
          cmd.surveyPointIdBuffers[i] = std::to_string(p.id);
        } else {
          const int nid = static_cast<int>(v);
          bool dup = false;
          for (size_t j = 0; j < cmd.surveyPoints.size(); ++j) {
            if (j != i && cmd.surveyPoints[j].id == nid)
              dup = true;
          }
          if (dup) {
            log.push_back("VIEWPOINTS — duplicate ID " + std::to_string(nid) + ".");
            cmd.surveyPointIdBuffers[i] = std::to_string(p.id);
          } else {
            p.id = nid;
            cmd.surveyPointIdBuffers[i] = std::to_string(nid);
          }
        }
      }
      ImGui::TableNextColumn();
      double de = static_cast<double>(p.easting);
      ImGui::InputDouble("##e", &de, 0., 0., "%.6f");
      if (ImGui::IsItemDeactivatedAfterEdit())
        p.easting = static_cast<float>(de);
      ImGui::TableNextColumn();
      double dn = static_cast<double>(p.northing);
      ImGui::InputDouble("##n", &dn, 0., 0., "%.6f");
      if (ImGui::IsItemDeactivatedAfterEdit())
        p.northing = static_cast<float>(dn);
      ImGui::TableNextColumn();
      double dz = static_cast<double>(p.elevation);
      ImGui::InputDouble("##z", &dz, 0., 0., "%.4f");
      if (ImGui::IsItemDeactivatedAfterEdit())
        p.elevation = static_cast<float>(dz);
      ImGui::TableNextColumn();
      ImGui::InputText("##layer", &p.layer);
      ImGui::TableNextColumn();
      ImGui::InputTextMultiline("##desc", &p.description, ImVec2(-FLT_MIN, 52.f));
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("X"))
        pendingDelete = static_cast<int>(i);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (pendingDelete >= 0)
    RemoveSurveyPointAt(cmd, static_cast<size_t>(pendingDelete));

  ImGui::End();
}

static const char* kSurveyCsvLayoutComboItems =
    "P,N,E,Z,D (point ID, northing, easting, Z, description)\0"
    "P,E,N,Z,D (point ID, easting, northing, Z, description)\0"
    "N,E,Z (northing, easting, Z — IDs assigned on import)\0"
    "E,N,Z (easting, northing, Z — IDs assigned on import)\0\0";

void DrawImportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showImportPointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
  bool open = cmd.showImportPointsWindow;
  if (!ImGui::Begin("Import points", &open)) {
    cmd.showImportPointsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showImportPointsWindow = open;

  if (ImGui::Button("Browse…")) {
    if (BrowseOpenFileCsvUtf8(cmd.surveyImportCsvPath, sizeof(cmd.surveyImportCsvPath)))
      cmd.surveyImportPreviewDirty = true;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::InputText("##imp_csv_path", cmd.surveyImportCsvPath, IM_ARRAYSIZE(cmd.surveyImportCsvPath)))
    cmd.surveyImportPreviewDirty = true;

  if (ImGui::Combo("Column order##imp", &cmd.surveyImportCsvLayoutIdx, kSurveyCsvLayoutComboItems))
    cmd.surveyImportPreviewDirty = true;
  if (ImGui::Checkbox("First row is header (skip)", &cmd.surveyImportCsvSkipFirstRow))
    cmd.surveyImportPreviewDirty = true;

  if (cmd.surveyImportPreviewDirty)
    SurveyCsvRefreshImportPreview(cmd);

  ImGui::Separator();
  ImGui::TextUnformatted("Validation summary");
  ImGui::BeginChild("imp_val", ImVec2(0, 110), true, ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.95f, 1.f));
  ImGui::TextUnformatted(cmd.surveyImportPreviewValidation.c_str());
  ImGui::PopStyleColor();
  ImGui::EndChild();

  ImGui::TextUnformatted("File preview");
  ImGui::BeginChild("imp_prev", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.f), true,
                    ImGuiWindowFlags_HorizontalScrollbar);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.92f, 0.86f, 1.f));
  ImGui::TextUnformatted(cmd.surveyImportPreviewText.empty() ? "(no preview)" : cmd.surveyImportPreviewText.c_str());
  ImGui::PopStyleColor();
  ImGui::EndChild();

  if (ImGui::Button("Import")) {
    if (SurveyCsvImportFile(cmd, log))
      cmd.surveyImportPreviewDirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh preview"))
    cmd.surveyImportPreviewDirty = true;

  ImGui::End();
}

void DrawExportPointsPanel(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.showExportPointsWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(520, 340), ImGuiCond_FirstUseEver);
  bool open = cmd.showExportPointsWindow;
  if (!ImGui::Begin("Export points", &open)) {
    cmd.showExportPointsWindow = open;
    ImGui::End();
    return;
  }
  cmd.showExportPointsWindow = open;

  if (ImGui::Button("Browse…")) {
    BrowseSaveFileCsvUtf8(cmd.surveyExportCsvPath, sizeof(cmd.surveyExportCsvPath), "points.csv");
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputText("##exp_csv_path", cmd.surveyExportCsvPath, IM_ARRAYSIZE(cmd.surveyExportCsvPath));

  ImGui::Combo("Column order##exp", &cmd.surveyExportCsvLayoutIdx, kSurveyCsvLayoutComboItems);
  ImGui::Checkbox("Write header row", &cmd.surveyExportCsvWriteHeader);

  if (ImGui::Button("Export"))
    SurveyCsvExportFile(cmd, log);

  ImGui::End();
}

void DrawSurveyReportsPanel(AppCommandState& cmd) {
  ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Reports", nullptr)) {
    ImGui::End();
    return;
  }

  if (cmd.surveyReportTabs.empty()) {
    ImGui::TextDisabled("Exported CSV files and other generated reports will appear here as tabs.");
  } else {
    const bool focusLatest = cmd.surveyReportSelectLatestPending;
    const int lastIx = static_cast<int>(cmd.surveyReportTabs.size()) - 1;
    if (ImGui::BeginTabBar("SurveyReportsTabBar", ImGuiTabBarFlags_Reorderable)) {
      for (int i = 0; i <= lastIx; ++i) {
        ImGuiTabItemFlags tabFlags = 0;
        if (focusLatest && i == lastIx)
          tabFlags |= ImGuiTabItemFlags_SetSelected;
        if (ImGui::BeginTabItem(cmd.surveyReportTabs[static_cast<size_t>(i)].first.c_str(), nullptr, tabFlags)) {
          cmd.surveyReportSelectedTab = i;
          ImGui::BeginChild("SurveyReportBody", ImVec2(0, 0), false,
                            ImGuiWindowFlags_HorizontalScrollbar);
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.84f, 1.f));
          ImGui::TextUnformatted(cmd.surveyReportTabs[static_cast<size_t>(i)].second.c_str());
          ImGui::PopStyleColor();
          ImGui::EndChild();
          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }
    if (focusLatest)
      cmd.surveyReportSelectLatestPending = false;
  }

  ImGui::End();
}

void DrawCopySurveyDuplicateModal(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.copySurveyDupModalOpen)
    return;

  if (cmd.copySurveyDupModalOpenRequested) {
    ImGui::OpenPopup("GoSurveyCopySurveyDup");
    cmd.copySurveyDupModalOpenRequested = false;
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal("GoSurveyCopySurveyDup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped(
        "Lines and circles are already copied. Choose how to add duplicate survey points when another point "
        "already uses the same ID.");
    ImGui::Separator();
    int pol = static_cast<int>(cmd.copySurveyDuplicatePolicy);
    if (ImGui::Combo(
            "If point numbers exist",
            &pol,
            "Notify (skip duplicate)\0Renumber (next free ID)\0Merge (update coords/desc)\0Overwrite (replace "
            "row)\0\0"))
      cmd.copySurveyDuplicatePolicy = static_cast<SurveyDuplicatePolicy>(pol);

    if (ImGui::Button("OK##copy_survey_dup", ImVec2(120.f, 0.f))) {
      ApplyCopySurveyDuplicateModalResult(cmd, true, log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##copy_survey_dup", ImVec2(120.f, 0.f))) {
      ApplyCopySurveyDuplicateModalResult(cmd, false, log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
