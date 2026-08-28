#include "CadUi.hpp"
#include "ToolspaceCatalog.hpp"
#include "WinFileDialogs.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr float kPreviewFrac = 0.22f;

constexpr ImGuiTreeNodeFlags kLeaf =
    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen |
    ImGuiTreeNodeFlags_DrawLinesToNodes;
constexpr ImGuiTreeNodeFlags kFolder =
    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DrawLinesToNodes;

constexpr int kTsPopupColors = 7;

struct TsPending {
  enum class Kind { None, AddPointFile, AddPointGroup };
  Kind kind = Kind::None;
  int si = -1;
  int deleteSurface = -1;
};

float TabStripWidth() {
  return ImGui::GetFontSize() + 14.f;
}

void DrawTextBottomToTop(ImDrawList* dl, ImVec2 rectMin, ImVec2 rectMax, ImU32 col, const char* text) {
  if (dl == nullptr || text == nullptr)
    return;
  const ImVec2 ts = ImGui::CalcTextSize(text);
  ImGui::PushClipRect(ImVec2(-100000.f, -100000.f), ImVec2(100000.f, 100000.f), false);
  const int vtx0 = dl->VtxBuffer.Size;
  dl->AddText(ImVec2(0.f, 0.f), col, text);
  const int vtx1 = dl->VtxBuffer.Size;
  ImGui::PopClipRect();
  const ImVec2 srcC(ts.x * 0.5f, ts.y * 0.5f);
  const ImVec2 dstC((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
  for (int i = vtx0; i < vtx1; ++i) {
    ImDrawVert& v = dl->VtxBuffer[i];
    const float x = v.pos.x - srcC.x;
    const float y = v.pos.y - srcC.y;
    v.pos.x = dstC.x + y;
    v.pos.y = dstC.y - x;
  }
}

bool SideTab(const char* id, const char* label, bool selected, float stripW) {
  const ImVec2 textSz = ImGui::CalcTextSize(label);
  const float h = textSz.x + 20.f;
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const ImVec2 size(stripW, h);
  ImGui::InvisibleButton(id, size);
  const bool pressed = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 bg = selected ? IM_COL32(255, 255, 255, 255)
                            : (hovered ? IM_COL32(72, 80, 92, 255) : IM_COL32(45, 50, 58, 255));
  const ImU32 fg = selected ? IM_COL32(20, 20, 20, 255) : IM_COL32(255, 255, 255, 255);
  dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg);
  if (selected)
    dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + 3.f, pos.y + size.y), IM_COL32(0, 120, 215, 255));
  DrawTextBottomToTop(dl, pos, ImVec2(pos.x + size.x, pos.y + size.y), fg, label);
  return pressed;
}

void DrawToolbarIcon(ImDrawList* dl, ImVec2 p, float s, int kind) {
  const ImU32 ink = IM_COL32(40, 40, 40, 255);
  if (kind == 0) {
    dl->AddRectFilled(ImVec2(p.x + s * 0.25f, p.y + s * 0.35f), ImVec2(p.x + s * 0.85f, p.y + s * 0.85f),
                      IM_COL32(70, 160, 70, 255));
    dl->AddRect(ImVec2(p.x + s * 0.25f, p.y + s * 0.35f), ImVec2(p.x + s * 0.85f, p.y + s * 0.85f), ink);
  } else if (kind == 1) {
    dl->AddRect(ImVec2(p.x + 2.f, p.y + 3.f), ImVec2(p.x + s - 6.f, p.y + s - 5.f), ink);
    dl->AddRect(ImVec2(p.x + 6.f, p.y + 6.f), ImVec2(p.x + s - 2.f, p.y + s - 2.f), ink);
  } else if (kind == 2) {
    for (int i = 0; i < 3; ++i) {
      const float y = p.y + 4.f + static_cast<float>(i) * 4.f;
      dl->AddLine(ImVec2(p.x + 3.f, y), ImVec2(p.x + s - 3.f, y), ink, 1.5f);
    }
  } else {
    dl->AddCircle(ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.38f, ink, 12, 1.5f);
    dl->AddText(ImVec2(p.x + s * 0.32f, p.y + 1.f), ink, "?");
  }
}

void DrawToolspaceToolbar() {
  const float h = ImGui::GetFrameHeight();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.86f, 0.86f, 0.86f, 1.f));
  ImGui::BeginChild("##ts_toolbar", ImVec2(0.f, h + 6.f), false);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  p.x += 6.f;
  p.y += 3.f;
  const float s = h - 2.f;
  for (int k = 0; k < 4; ++k) {
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton(("##tsico" + std::to_string(k)).c_str(), ImVec2(s, s));
    DrawToolbarIcon(dl, p, s, k);
    if (k == 3 && ImGui::IsItemHovered())
      ImGui::SetTooltip("Drawing explorer — Prospector lists survey objects; Settings lists styles.");
    p.x += s + 6.f;
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

void SubmitLine(AppCommandState& cmd, std::vector<std::string>* log, const std::string& line) {
  if (log == nullptr)
    return;
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), cmd, *log);
}

std::vector<std::string>& LogRef(std::vector<std::string>* log, std::vector<std::string>& discard) {
  return log != nullptr ? *log : discard;
}

std::string NextPointGroupName(const AppCommandState& cmd) {
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

void PushTsPopupColors() {
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1.f, 1.f, 1.f, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.f, 1.f, 1.f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.55f, 0.55f, 0.55f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.26f, 0.59f, 0.98f, 0.4f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.65f));
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.72f, 0.72f, 0.72f, 1.f));
}

bool BeginTsContext(const char* id) {
  PushTsPopupColors();
  if (!ImGui::BeginPopupContextItem(id)) {
    ImGui::PopStyleColor(kTsPopupColors);
    return false;
  }
  return true;
}

void EndTsContext() {
  ImGui::EndPopup();
  ImGui::PopStyleColor(kTsPopupColors);
}

void SelectAllSurveyPoints(AppCommandState& cmd) {
  cmd.selection.clear();
  cmd.selectedSurveyPointIndices.clear();
  cmd.selectedSurveyPointIndices.reserve(cmd.surveyPoints.size());
  for (size_t i = 0; i < cmd.surveyPoints.size(); ++i)
    cmd.selectedSurveyPointIndices.push_back(static_cast<int>(i));
}

void ZoomToSurveyPoints(AppCommandState& cmd, std::vector<std::string>* log) {
  if (cmd.surveyPoints.empty()) {
    if (log != nullptr)
      log->push_back("TOOLSPACE — no survey points to zoom to.");
    return;
  }
  float mnX = cmd.surveyPoints[0].easting;
  float mxX = mnX;
  float mnY = cmd.surveyPoints[0].northing;
  float mxY = mnY;
  for (const SurveyPoint& p : cmd.surveyPoints) {
    mnX = std::min(mnX, p.easting);
    mxX = std::max(mxX, p.easting);
    mnY = std::min(mnY, p.northing);
    mxY = std::max(mxY, p.northing);
  }
  if (mxX - mnX < 1.f) {
    mnX -= 5.f;
    mxX += 5.f;
  }
  if (mxY - mnY < 1.f) {
    mnY -= 5.f;
    mxY += 5.f;
  }
  cmd.pendingZoomExtents = false;
  cmd.pendingZoomWindow = true;
  cmd.pendingZoomMnX = mnX;
  cmd.pendingZoomMxX = mxX;
  cmd.pendingZoomMnY = mnY;
  cmd.pendingZoomMxY = mxY;
}

void PanToSurveyPoints(AppCommandState& cmd, std::vector<std::string>* log) {
  if (cmd.surveyPoints.empty()) {
    if (log != nullptr)
      log->push_back("TOOLSPACE — no survey points to pan to.");
    return;
  }
  double sx = 0.0;
  double sy = 0.0;
  for (const SurveyPoint& p : cmd.surveyPoints) {
    sx += static_cast<double>(p.easting);
    sy += static_cast<double>(p.northing);
  }
  const double n = static_cast<double>(cmd.surveyPoints.size());
  cmd.viewportPanX = sx / n;
  cmd.viewportPanY = sy / n;
}

void OpenSurfaceStyleAnalysis(AppCommandState& cmd, const CadSurface& s) {
  cmd.surfaceStyleEditorFocusName = s.styleName.empty() ? std::string("Standard") : s.styleName;
  cmd.surfaceStyleUseSurfacesTitle = false;
  cmd.showSurfaceStyleWindow = true;
}

void OpenSurfaceProperties(AppCommandState& cmd, int si) {
  cmd.surfacePropertiesIndex = si;
  cmd.showSurfacePropertiesWindow = true;
}

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

void ZoomToSurface(AppCommandState& cmd, const CadSurface& s, std::vector<std::string>* log) {
  float mnX = 0.f, mxX = 0.f, mnY = 0.f, mxY = 0.f;
  if (!SurfacePlanBounds(s, &mnX, &mxX, &mnY, &mxY)) {
    if (log != nullptr)
      log->push_back("TOOLSPACE — surface \"" + s.name + "\" is not built.");
    return;
  }
  if (mxX - mnX < 1.f) {
    mnX -= 5.f;
    mxX += 5.f;
  }
  if (mxY - mnY < 1.f) {
    mnY -= 5.f;
    mxY += 5.f;
  }
  cmd.pendingZoomExtents = false;
  cmd.pendingZoomWindow = true;
  cmd.pendingZoomMnX = mnX;
  cmd.pendingZoomMxX = mxX;
  cmd.pendingZoomMnY = mnY;
  cmd.pendingZoomMxY = mxY;
}

void PanToSurface(AppCommandState& cmd, const CadSurface& s, std::vector<std::string>* log) {
  float mnX = 0.f, mxX = 0.f, mnY = 0.f, mxY = 0.f;
  if (!SurfacePlanBounds(s, &mnX, &mxX, &mnY, &mxY)) {
    if (log != nullptr)
      log->push_back("TOOLSPACE — surface \"" + s.name + "\" is not built.");
    return;
  }
  cmd.viewportPanX = (static_cast<double>(mnX) + mxX) * 0.5;
  cmd.viewportPanY = (static_cast<double>(mnY) + mxY) * 0.5;
}

void DrawNamedSurfaceContext(AppCommandState& cmd, size_t si, std::vector<std::string>* log, TsPending& pending) {
  CadSurface& s = cmd.cadSurfaces[si];
  if (!BeginTsContext("##surfctx"))
    return;
  if (ImGui::MenuItem("Surface Properties..."))
    OpenSurfaceProperties(cmd, static_cast<int>(si));
  if (ImGui::MenuItem("Edit Surface Style..."))
    OpenSurfaceStyleAnalysis(cmd, s);
  ImGui::Separator();
  ImGui::MenuItem("Surface Level of Detail", nullptr, false, false);
  ImGui::Separator();
  if (ImGui::MenuItem("Rebuild"))
    SubmitLine(cmd, log, "SURFACEREBUILD " + s.name);
  ImGui::MenuItem("Rebuild - Automatic", nullptr, true, false);
  ImGui::Separator();
  ImGui::MenuItem("Create Snapshot", nullptr, false, false);
  ImGui::MenuItem("Remove Snapshot", nullptr, false, false);
  ImGui::MenuItem("Rebuild Snapshot", nullptr, false, false);
  ImGui::Separator();
  ImGui::MenuItem("Add Label...", nullptr, false, false);
  ImGui::Separator();
  if (ImGui::MenuItem("Delete...")) {
    pending.deleteSurface = static_cast<int>(si);
    if (cmd.surfacePropertiesIndex == static_cast<int>(si))
      cmd.showSurfacePropertiesWindow = false;
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Select")) {
    cmd.selection.clear();
    cmd.selectedSurveyPointIndices.clear();
    cmd.selection.push_back({SelectedEntity::Type::Surface, static_cast<int>(si)});
  }
  const bool built = s.tin && s.tin->vertsXyz.size() >= 3;
  if (ImGui::MenuItem("Zoom to", nullptr, false, built))
    ZoomToSurface(cmd, s, log);
  if (ImGui::MenuItem("Pan to", nullptr, false, built))
    PanToSurface(cmd, s, log);
  ImGui::Separator();
  ImGui::MenuItem("Lock", nullptr, false, false);
  ImGui::MenuItem("Unlock", nullptr, false, false);
  ImGui::Separator();
  ImGui::MenuItem("Export to DEM...", nullptr, false, false);
  ImGui::MenuItem("Export LandXML...", nullptr, false, false);
  ImGui::Separator();
  ImGui::MenuItem("Open in Project Explorer...", nullptr, false, false);
  ImGui::Separator();
  ImGui::MenuItem("Add to Model Viewer...", nullptr, false, false);
  ImGui::MenuItem("Zoom to Model Viewer...", nullptr, false, false);
  ImGui::MenuItem("Remove from Model Viewer...", nullptr, false, false);
  ImGui::Separator();
  if (ImGui::MenuItem("Refresh") && log != nullptr)
    log->push_back("TOOLSPACE — refreshed.");
  EndTsContext();
}

void DrawPointsContext(AppCommandState& cmd, std::vector<std::string>* log) {
  if (!BeginTsContext("##ptsctx"))
    return;
  if (ImGui::MenuItem("Create...")) {
    if (log != nullptr)
      StartCreatePointsCommand(cmd, *log);
    else
      cmd.showCreatePointsWindow = true;
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Import...")) {
    if (log != nullptr)
      StartImportPointsCommand(cmd, *log);
    else
      cmd.showImportPointsWindow = true;
  }
  if (ImGui::MenuItem("Export...")) {
    if (log != nullptr)
      StartExportPointsCommand(cmd, *log);
    else
      cmd.showExportPointsWindow = true;
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Edit Points...")) {
    if (log != nullptr)
      StartViewPointsCommand(cmd, *log);
    else
      cmd.showViewPointsWindow = true;
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Select", nullptr, false, !cmd.surveyPoints.empty()))
    SelectAllSurveyPoints(cmd);
  if (ImGui::MenuItem("Zoom to", nullptr, false, !cmd.surveyPoints.empty()))
    ZoomToSurveyPoints(cmd, log);
  if (ImGui::MenuItem("Pan to", nullptr, false, !cmd.surveyPoints.empty()))
    PanToSurveyPoints(cmd, log);
  EndTsContext();
}

void DrawPointGroupsFolderContext(AppCommandState& cmd, std::vector<std::string>* log) {
  if (!BeginTsContext("##pgfoldctx"))
    return;
  if (ImGui::MenuItem("Properties"))
    cmd.showPointGroupManagerWindow = true;
  if (ImGui::MenuItem("New")) {
    PushUndoSnapshot(cmd, "New point group");
    PointGroup g;
    g.name = NextPointGroupName(cmd);
    cmd.pointGroups.push_back(std::move(g));
    if (log != nullptr)
      log->push_back("Created point group \"" + cmd.pointGroups.back().name + "\".");
  }
  ImGui::MenuItem("Show Changes", nullptr, false, false);
  ImGui::MenuItem("Update", nullptr, false, false);
  ImGui::MenuItem("Export LandXML", nullptr, false, false);
  if (ImGui::MenuItem("Refresh") && log != nullptr)
    log->push_back("TOOLSPACE — refreshed.");
  EndTsContext();
}

void DrawFeatureLinesFolderContext(AppCommandState& cmd, std::vector<std::string>* log) {
  if (!BeginTsContext("##flfoldctx"))
    return;
  if (ImGui::MenuItem("Properties"))
    cmd.showFeatureLineElevWindow = true;
  if (ImGui::MenuItem("New")) {
    std::vector<std::string> discard;
    StartFeatureLineCommand(cmd, "", LogRef(log, discard));
  }
  ImGui::MenuItem("Show Changes", nullptr, false, false);
  ImGui::MenuItem("Update", nullptr, false, false);
  ImGui::MenuItem("Export LandXML", nullptr, false, false);
  if (ImGui::MenuItem("Refresh") && log != nullptr)
    log->push_back("TOOLSPACE — refreshed.");
  EndTsContext();
}

void DrawSurfacesFolderContext(AppCommandState& cmd, std::vector<std::string>* log) {
  if (!BeginTsContext("##surffoldctx"))
    return;
  if (ImGui::MenuItem("Create Surface..."))
    cmd.showCreateSurfaceWindow = true;
  ImGui::MenuItem("Create Surface From DEM...", nullptr, false, false);
  ImGui::MenuItem("Create Surface from TIN...", nullptr, false, false);
  ImGui::Separator();
  ImGui::MenuItem("Show Preview", nullptr, false, false);
  ImGui::Separator();
  ImGui::MenuItem("Reduced Level of Detail", nullptr, false, false);
  ImGui::MenuItem("High Level of Detail", nullptr, false, false);
  ImGui::Separator();
  if (ImGui::MenuItem("Rebuild Out of Date Items", nullptr, false, !cmd.cadSurfaces.empty()))
    SubmitLine(cmd, log, "SURFACEREBUILD");
  ImGui::Separator();
  ImGui::MenuItem("Create Folder", nullptr, false, false);
  ImGui::Separator();
  ImGui::MenuItem("Export to DEM...", nullptr, false, false);
  ImGui::MenuItem("Export LandXML...", nullptr, false, false);
  ImGui::Separator();
  if (ImGui::MenuItem("Refresh") && log != nullptr)
    log->push_back("TOOLSPACE — refreshed.");
  EndTsContext();
}

enum class DefFolderAdd {
  Boundary,
  Breakline,
  Contour,
  PointFile,
  PointGroup,
  Edit,
  Mask,
};

void DrawDefFolderAddRefresh(const char* popupId, AppCommandState& cmd, CadSurface& s, int si,
                             std::vector<std::string>* log, std::vector<std::string>& lg, TsPending& pending,
                             DefFolderAdd addKind) {
  if (!BeginTsContext(popupId))
    return;
  const bool addEnabled = true;
  if (ImGui::MenuItem("Add", nullptr, false, addEnabled) && addKind != DefFolderAdd::Edit) {
    if (addKind == DefFolderAdd::Boundary)
      StartDesignateBoundaryCommand(cmd, s.name, CadBoundaryKind::Outer, lg);
    else if (addKind == DefFolderAdd::Mask)
      StartDesignateBoundaryCommand(cmd, s.name, CadBoundaryKind::Mask, lg);
    else if (addKind == DefFolderAdd::Breakline)
      StartDesignateBreaklineCommand(cmd, s.name, lg);
    else if (addKind == DefFolderAdd::Contour)
      StartDesignateContourCommand(cmd, s.name, lg);
    else if (addKind == DefFolderAdd::PointFile) {
      pending.kind = TsPending::Kind::AddPointFile;
      pending.si = si;
    } else if (addKind == DefFolderAdd::PointGroup) {
      pending.kind = TsPending::Kind::AddPointGroup;
      pending.si = si;
    }
  }
  if (addKind == DefFolderAdd::Edit && ImGui::BeginMenu("Add")) {
    if (ImGui::MenuItem("Point"))
      StartSurfAddPointCommand(cmd, s.name, lg);
    if (ImGui::MenuItem("Delete Point"))
      StartSurfDelPointCommand(cmd, s.name, lg);
    if (ImGui::MenuItem("Move Point"))
      StartSurfMovePointCommand(cmd, s.name, lg);
    if (ImGui::MenuItem("Delete TIN Line"))
      StartSurfDelLineCommand(cmd, s.name, lg);
    if (ImGui::MenuItem("Swap Edge"))
      StartSurfSwapEdgeCommand(cmd, s.name, lg);
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Refresh"))
    SubmitLine(cmd, log, "SURFACEREBUILD " + s.name);
  EndTsContext();
}

void DrawSurfaceNode(AppCommandState& cmd, size_t si, std::vector<std::string>* log, TsPending& pending) {
  CadSurface& s = cmd.cadSurfaces[si];
  std::vector<std::string> discard;
  std::vector<std::string>& lg = LogRef(log, discard);
  int removeGroupIdx = -1;
  int removeSwapIdx = -1;
  int removeAddPtIdx = -1;
  int removeDelPtIdx = -1;

  ImGui::PushID(static_cast<int>(si));
  ImGui::SetNextItemOpen(false, ImGuiCond_Once);
  const bool open = ImGui::TreeNodeEx("##surf", kFolder, "%s", s.name.c_str());
  DrawNamedSurfaceContext(cmd, si, log, pending);
  if (!open) {
    ImGui::PopID();
    return;
  }

  ImGui::SetNextItemOpen(false, ImGuiCond_Once);
  const bool masksOpen = ImGui::TreeNodeEx("Masks", kFolder);
  DrawDefFolderAddRefresh("##masksfolder", cmd, s, static_cast<int>(si), log, lg, pending, DefFolderAdd::Mask);
  if (masksOpen) {
    for (size_t bi = 0; bi < s.boundaries.size(); ++bi) {
      if (s.boundaries[bi].kind != CadBoundaryKind::Mask)
        continue;
      ImGui::PushID(static_cast<int>(bi));
      const std::string lab = s.boundaries[bi].name.empty() ? std::string("Mask") : s.boundaries[bi].name;
      ImGui::TreeNodeEx(lab.c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
      if (BeginTsContext("##maskitem")) {
        if (ImGui::MenuItem("Remove"))
          SubmitLine(cmd, log, "UNDESIGNATE " + s.name + ", BOUNDARY, " + std::to_string(bi + 1));
        EndTsContext();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  ImGui::SetNextItemOpen(false, ImGuiCond_Once);
  const bool shedsOpen = ImGui::TreeNodeEx("Watersheds", kFolder);
  if (BeginTsContext("##shedctx")) {
    if (ImGui::MenuItem("Analyze..."))
      SubmitLine(cmd, log, "WATERSHED " + s.name);
    EndTsContext();
  }
  if (shedsOpen) {
    std::uint64_t sid = 0;
    if (si < cmd.cadSurfaceAttrs.size())
      sid = cmd.cadSurfaceAttrs[si].id;
    for (const AppCommandState::SurfaceWatershedCacheEntry& e : cmd.surfaceWatershedCache) {
      if (e.surfaceId != sid)
        continue;
      for (size_t bsi = 0; bsi < e.analysis.basins.size(); ++bsi) {
        const std::string lab = "Basin " + std::to_string(e.analysis.basins[bsi].id);
        ImGui::PushID(static_cast<int>(bsi));
        ImGui::TreeNodeEx(lab.c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        ImGui::PopID();
      }
    }
    ImGui::TreePop();
  }

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  const bool defOpen = ImGui::TreeNodeEx("Definition", kFolder);
  if (BeginTsContext("##deffolder")) {
    if (ImGui::MenuItem("Refresh"))
      SubmitLine(cmd, log, "SURFACEREBUILD " + s.name);
    EndTsContext();
  }
  if (defOpen) {
    const bool bdOpen = ImGui::TreeNodeEx("Boundaries", kFolder);
    DrawDefFolderAddRefresh("##bdfolder", cmd, s, static_cast<int>(si), log, lg, pending, DefFolderAdd::Boundary);
    if (bdOpen) {
      for (size_t bi = 0; bi < s.boundaries.size(); ++bi) {
        if (s.boundaries[bi].kind == CadBoundaryKind::Mask)
          continue;
        ImGui::PushID(static_cast<int>(bi));
        const std::string lab = s.boundaries[bi].name.empty() ? std::string("Boundary") : s.boundaries[bi].name;
        ImGui::TreeNodeEx(lab.c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        if (BeginTsContext("##bditem")) {
          if (ImGui::MenuItem("Remove"))
            SubmitLine(cmd, log, "UNDESIGNATE " + s.name + ", BOUNDARY, " + std::to_string(bi + 1));
          EndTsContext();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    const bool blOpen = ImGui::TreeNodeEx("Breaklines", kFolder);
    DrawDefFolderAddRefresh("##blfolder", cmd, s, static_cast<int>(si), log, lg, pending, DefFolderAdd::Breakline);
    if (blOpen) {
      for (size_t i = 0; i < s.breaklines.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string lab =
            s.breaklines[i].description.empty() ? ("Breakline " + std::to_string(i + 1)) : s.breaklines[i].description;
        ImGui::TreeNodeEx(lab.c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        if (BeginTsContext("##blitem")) {
          if (ImGui::MenuItem("Remove"))
            SubmitLine(cmd, log, "UNDESIGNATE " + s.name + ", BREAKLINE, " + std::to_string(i + 1));
          EndTsContext();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    const bool ctOpen = ImGui::TreeNodeEx("Contours", kFolder);
    DrawDefFolderAddRefresh("##ctfolder", cmd, s, static_cast<int>(si), log, lg, pending, DefFolderAdd::Contour);
    if (ctOpen) {
      for (size_t i = 0; i < s.contourSources.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string lab = s.contourSources[i].description.empty()
                                    ? ("Contour " + std::to_string(i + 1))
                                    : s.contourSources[i].description;
        ImGui::TreeNodeEx(lab.c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        if (BeginTsContext("##ctitem")) {
          if (ImGui::MenuItem("Remove"))
            SubmitLine(cmd, log, "UNDESIGNATE " + s.name + ", CONTOUR, " + std::to_string(i + 1));
          EndTsContext();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    const bool pfOpen = ImGui::TreeNodeEx("Point Files", kFolder);
    DrawDefFolderAddRefresh("##pffolder", cmd, s, static_cast<int>(si), log, lg, pending, DefFolderAdd::PointFile);
    if (pfOpen) {
      for (size_t i = 0; i < s.sourcePointFiles.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TreeNodeEx(s.sourcePointFiles[i].path.c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        if (BeginTsContext("##pfitem")) {
          if (ImGui::MenuItem("Remove"))
            SubmitLine(cmd, log, "UNDESIGNATE " + s.name + ", POINTFILE, " + std::to_string(i + 1));
          EndTsContext();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    const bool pgOpen = ImGui::TreeNodeEx("Point Groups", kFolder);
    DrawDefFolderAddRefresh("##pgdefolder", cmd, s, static_cast<int>(si), log, lg, pending, DefFolderAdd::PointGroup);
    if (pgOpen) {
      for (size_t i = 0; i < s.sourcePointGroups.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TreeNodeEx(s.sourcePointGroups[i].c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        if (BeginTsContext("##pgdeitem")) {
          if (ImGui::MenuItem("Remove"))
            removeGroupIdx = static_cast<int>(i);
          EndTsContext();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    const bool edOpen = ImGui::TreeNodeEx("Edits", kFolder);
    DrawDefFolderAddRefresh("##edfolder", cmd, s, static_cast<int>(si), log, lg, pending, DefFolderAdd::Edit);
    if (edOpen) {
      for (size_t i = 0; i < s.swappedEdgePicks.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TreeNodeEx(("Edge swap " + std::to_string(i + 1)).c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        if (BeginTsContext("##editem")) {
          if (ImGui::MenuItem("Remove"))
            removeSwapIdx = static_cast<int>(i);
          EndTsContext();
        }
        ImGui::PopID();
      }
      const size_t nAdd = s.addedPointXyz.size() / 3;
      for (size_t i = 0; i < nAdd; ++i) {
        ImGui::PushID(static_cast<int>(1000 + i));
        ImGui::TreeNodeEx(("Added point " + std::to_string(i + 1)).c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        if (BeginTsContext("##edaddpt")) {
          if (ImGui::MenuItem("Remove"))
            removeAddPtIdx = static_cast<int>(i);
          EndTsContext();
        }
        ImGui::PopID();
      }
      for (size_t i = 0; i < s.deletedPointPicks.size(); ++i) {
        ImGui::PushID(static_cast<int>(2000 + i));
        ImGui::TreeNodeEx(("Deleted point " + std::to_string(i + 1)).c_str(), kLeaf | ImGuiTreeNodeFlags_Bullet);
        if (BeginTsContext("##eddelpt")) {
          if (ImGui::MenuItem("Remove"))
            removeDelPtIdx = static_cast<int>(i);
          EndTsContext();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    ImGui::TreePop();
  }

  ImGui::TreePop();
  ImGui::PopID();

  if (removeGroupIdx >= 0 && static_cast<size_t>(removeGroupIdx) < s.sourcePointGroups.size()) {
    PushUndoSnapshot(cmd, "Remove point group from surface");
    s.sourcePointGroups.erase(s.sourcePointGroups.begin() + static_cast<std::ptrdiff_t>(removeGroupIdx));
    BumpCadGpuCache(cmd);
    lg.push_back("Surface \"" + s.name + "\": removed a point group from the definition.");
  }
  if (removeSwapIdx >= 0 && static_cast<size_t>(removeSwapIdx) < s.swappedEdgePicks.size()) {
    PushUndoSnapshot(cmd, "Remove surface edge swap");
    s.swappedEdgePicks.erase(s.swappedEdgePicks.begin() + static_cast<std::ptrdiff_t>(removeSwapIdx));
    BumpCadGpuCache(cmd);
    lg.push_back("Surface \"" + s.name + "\": removed an edge-swap edit.");
  }
  if (removeAddPtIdx >= 0) {
    const size_t i = static_cast<size_t>(removeAddPtIdx) * 3;
    if (i + 2 < s.addedPointXyz.size()) {
      PushUndoSnapshot(cmd, "Remove added surface point");
      s.addedPointXyz.erase(s.addedPointXyz.begin() + static_cast<std::ptrdiff_t>(i),
                            s.addedPointXyz.begin() + static_cast<std::ptrdiff_t>(i + 3));
      BumpCadGpuCache(cmd);
      lg.push_back("Surface \"" + s.name + "\": removed an added-point edit.");
    }
  }
  if (removeDelPtIdx >= 0 && static_cast<size_t>(removeDelPtIdx) < s.deletedPointPicks.size()) {
    PushUndoSnapshot(cmd, "Remove surface point-delete");
    s.deletedPointPicks.erase(s.deletedPointPicks.begin() + static_cast<std::ptrdiff_t>(removeDelPtIdx));
    BumpCadGpuCache(cmd);
    lg.push_back("Surface \"" + s.name + "\": removed a point-delete edit.");
  }
}

void DrawProspectorTree(AppCommandState& cmd, std::vector<std::string>* log, TsPending& pending) {
  const std::string drawing = ToolspaceActiveDrawingName(cmd);
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (!ImGui::TreeNodeEx(drawing.c_str(), ImGuiTreeNodeFlags_DefaultOpen | kFolder))
    return;

  ImGui::TreeNodeEx("Points", kLeaf);
  DrawPointsContext(cmd, log);

  ImGui::SetNextItemOpen(false, ImGuiCond_Once);
  const bool pgFoldOpen = ImGui::TreeNodeEx("Point Groups", kFolder);
  DrawPointGroupsFolderContext(cmd, log);
  if (pgFoldOpen) {
    for (const PointGroup& g : cmd.pointGroups) {
      ImGui::PushID(g.name.c_str());
      ImGui::TreeNodeEx(g.name.c_str(), kLeaf);
      if (BeginTsContext("##pgitemctx")) {
        if (ImGui::MenuItem("Properties")) {
          cmd.pointGroupManagerFocusName = g.name;
          cmd.showPointGroupManagerWindow = true;
        }
        EndTsContext();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  ImGui::SetNextItemOpen(false, ImGuiCond_Once);
  const bool surfFoldOpen = ImGui::TreeNodeEx("Surfaces", kFolder);
  DrawSurfacesFolderContext(cmd, log);
  if (surfFoldOpen) {
    for (size_t i = 0; i < cmd.cadSurfaces.size(); ++i)
      DrawSurfaceNode(cmd, i, log, pending);
    ImGui::TreePop();
  }

  ImGui::SetNextItemOpen(false, ImGuiCond_Once);
  const bool flFoldOpen = ImGui::TreeNodeEx("Feature Lines", kFolder);
  DrawFeatureLinesFolderContext(cmd, log);
  if (flFoldOpen) {
    const size_t nFl = ToolspaceFeatureLineCount(cmd);
    for (size_t i = 0; i < nFl; ++i) {
      const std::string label =
          (i < cmd.featureLineInfo.size() && !cmd.featureLineInfo[i].name.empty())
              ? cmd.featureLineInfo[i].name
              : ("Feature Line " + std::to_string(i + 1));
      ImGui::PushID(static_cast<int>(i));
      ImGui::TreeNodeEx(label.c_str(), kLeaf);
      if (BeginTsContext("##flitemctx")) {
        if (ImGui::MenuItem("Properties")) {
          cmd.featureLineElevIndex = static_cast<int>(i);
          cmd.showFeatureLineElevWindow = true;
        }
        EndTsContext();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  ImGui::TreePop();
}

void DrawSettingsTree(AppCommandState& cmd) {
  const std::string drawing = ToolspaceActiveDrawingName(cmd);
  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (!ImGui::TreeNodeEx(drawing.c_str(), ImGuiTreeNodeFlags_DefaultOpen | kFolder))
    return;

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNodeEx("General", kFolder)) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNodeEx("Text Styles", kFolder)) {
      if (BeginTsContext("##tsstyles")) {
        if (ImGui::MenuItem("Properties"))
          cmd.showTextStyleManagerWindow = true;
        EndTsContext();
      }
      for (const TextStyle& ts : cmd.textStyles) {
        ImGui::TreeNodeEx(ts.name.c_str(), kLeaf);
        if (BeginTsContext("##tsstyleitem")) {
          if (ImGui::MenuItem("Properties"))
            cmd.showTextStyleManagerWindow = true;
          EndTsContext();
        }
      }
      ImGui::TreePop();
    }
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNodeEx("Layers", kFolder)) {
      if (BeginTsContext("##tslayers")) {
        if (ImGui::MenuItem("Properties"))
          cmd.showLayerManagerWindow = true;
        EndTsContext();
      }
      for (const CadLayerRow& row : cmd.drawingLayerTable) {
        ImGui::TreeNodeEx(row.name.c_str(), kLeaf);
        if (BeginTsContext("##tslayeritem")) {
          if (ImGui::MenuItem("Properties"))
            cmd.showLayerManagerWindow = true;
          EndTsContext();
        }
      }
      ImGui::TreePop();
    }
    ImGui::TreeNodeEx("Dimension Style", kLeaf);
    if (BeginTsContext("##tsdim")) {
      if (ImGui::MenuItem("Properties"))
        cmd.showDimStyleDialog = true;
      EndTsContext();
    }
    ImGui::TreePop();
  }

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNodeEx("Surface", kFolder)) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::TreeNodeEx("Surface Styles", kFolder)) {
      if (BeginTsContext("##tssurfstyles")) {
        if (ImGui::MenuItem("Properties"))
          cmd.showSurfaceStyleWindow = true;
        EndTsContext();
      }
      for (const SurfaceStyle& ss : cmd.surfaceStyles) {
        ImGui::TreeNodeEx(ss.name.c_str(), kLeaf);
        if (BeginTsContext("##tssurfstyleitem")) {
          if (ImGui::MenuItem("Properties")) {
            cmd.surfaceStyleEditorFocusName = ss.name;
            cmd.surfaceStyleUseSurfacesTitle = false;
            cmd.showSurfaceStyleWindow = true;
          }
          EndTsContext();
        }
      }
      ImGui::TreePop();
    }
    ImGui::TreePop();
  }

  ImGui::TreePop();
}

void DrawToolspaceDefDialogs(AppCommandState& cmd, std::vector<std::string>* log, TsPending& pending) {
  static int dlgSurface = -1;
  static std::string dlgText;
  static int pfLayout = 0;
  static bool pfHeader = false;

  if (pending.kind != TsPending::Kind::None && pending.si >= 0) {
    dlgSurface = pending.si;
    dlgText.clear();
    pfLayout = 0;
    pfHeader = false;
    if (pending.kind == TsPending::Kind::AddPointFile)
      ImGui::OpenPopup("Toolspace Add Point File");
    else
      ImGui::OpenPopup("Toolspace Add Point Group");
    pending.kind = TsPending::Kind::None;
  }

  const bool dlgSurfaceValid = dlgSurface >= 0 && dlgSurface < static_cast<int>(cmd.cadSurfaces.size());
  std::vector<std::string> discard;
  std::vector<std::string>& lg = LogRef(log, discard);

  if (ImGui::BeginPopupModal("Toolspace Add Point File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Point file:");
    ImGui::SetNextItemWidth(420.f);
    ImGui::InputText("##tspfpath", &dlgText);
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
      char buf[1024] = {0};
      if (BrowseOpenFileCsvUtf8(buf, sizeof(buf)))
        dlgText = buf;
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("Column layout:");
    ImGui::SetNextItemWidth(200.f);
    const char* layouts[] = {"PNEZD", "PENZD", "NEZ", "ENZ"};
    ImGui::Combo("##tspflayout", &pfLayout, layouts, 4);
    ImGui::SameLine();
    ImGui::Checkbox("First row is a header", &pfHeader);
    ImGui::Spacing();
    ImGui::BeginDisabled(dlgText.empty());
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      if (dlgSurfaceValid) {
        std::string line = "SURFACEADDFILE " + cmd.cadSurfaces[static_cast<size_t>(dlgSurface)].name + ", " +
                           dlgText + ", " + layouts[pfLayout];
        if (pfHeader)
          line += ", HEADER";
        SubmitLine(cmd, &lg, line);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Toolspace Add Point Group", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
          lg.push_back("Surface \"" + s.name + "\": added point group \"" + gname + "\".");
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
}

enum class CreateSurfaceKind : int { Tin = 0, Grid, Corridor, TinVolume, GridVolume };

void ApplyCreatedSurfaceFields(AppCommandState& cmd, const std::string& name, const std::string& description,
                               const std::string& styleName, const std::string& layer) {
  assert(!name.empty());
  const int ni = FindSurfaceIndex(cmd, name);
  assert(ni >= 0);
  if (ni < 0)
    return;
  CadSurface& s = cmd.cadSurfaces[static_cast<size_t>(ni)];
  s.description = description;
  s.styleName = styleName;
  if (static_cast<size_t>(ni) < cmd.cadSurfaceAttrs.size())
    cmd.cadSurfaceAttrs[static_cast<size_t>(ni)].layer = layer.empty() ? std::string("0") : layer;
}

void DrawCreateSurfaceWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  static bool wasShown = false;
  static std::string name;
  static std::string description;
  static std::string layer;
  static std::string styleName;
  static std::string error;
  static int kindIdx = 0;
  static double gridOx = 0.0, gridOy = 0.0, gridSx = 10.0, gridSy = 10.0;
  static int gridCols = 2, gridRows = 2;
  static std::string volBase;
  static std::string volComp;

  if (cmd.showCreateSurfaceWindow && !wasShown) {
    SurfaceStyles::EnsureStandard(cmd.surfaceStyles);
    name = NextSurfaceName(cmd);
    description.clear();
    layer = cmd.currentLayer.empty() ? std::string("0") : cmd.currentLayer;
    styleName = SurfaceStyles::kStandardName;
    error.clear();
    kindIdx = 0;
    gridOx = 0.0;
    gridOy = 0.0;
    gridSx = 10.0;
    gridSy = 10.0;
    gridCols = 2;
    gridRows = 2;
    volBase.clear();
    volComp.clear();
    if (cmd.cadSurfaces.size() >= 2) {
      volBase = cmd.cadSurfaces[0].name;
      volComp = cmd.cadSurfaces[1].name;
    }
  }
  wasShown = cmd.showCreateSurfaceWindow;
  if (!cmd.showCreateSurfaceWindow)
    return;

  std::vector<std::string> discard;
  std::vector<std::string>& lg = LogRef(log, discard);

  ImGui::SetNextWindowSize(ImVec2(540.f, 520.f), ImGuiCond_FirstUseEver);
  bool open = cmd.showCreateSurfaceWindow;
  if (!ImGui::Begin("Create Surface", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showCreateSurfaceWindow = open;
    ImGui::End();
    return;
  }
  cmd.showCreateSurfaceWindow = open;
  if (!open) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Type");
  ImGui::SameLine(140.f);
  ImGui::SetNextItemWidth(-1.f);
  const char* kKindNames[] = {"TIN surface", "Grid surface", "Corridor surface", "TIN volume surface",
                              "Grid volume surface"};
  if (kindIdx < 0 || kindIdx > 4)
    kindIdx = 0;
  if (ImGui::BeginCombo("##cstype", kKindNames[kindIdx])) {
    for (int i = 0; i < 5; ++i) {
      const bool sel = (kindIdx == i);
      if (ImGui::Selectable(kKindNames[i], sel))
        kindIdx = i;
      if (sel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const CreateSurfaceKind kind = static_cast<CreateSurfaceKind>(kindIdx);
  if (kind == CreateSurfaceKind::Grid) {
    ImGui::TextUnformatted("Grid origin X");
    ImGui::SameLine(140.f);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputDouble("##gox", &gridOx, 0.0, 0.0, "%.4f");
    ImGui::TextUnformatted("Grid origin Y");
    ImGui::SameLine(140.f);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputDouble("##goy", &gridOy, 0.0, 0.0, "%.4f");
    ImGui::TextUnformatted("Spacing X / Y");
    ImGui::SameLine(140.f);
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputDouble("##gsx", &gridSx, 0.0, 0.0, "%.4f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.f);
    ImGui::InputDouble("##gsy", &gridSy, 0.0, 0.0, "%.4f");
    ImGui::TextUnformatted("Columns / rows");
    ImGui::SameLine(140.f);
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("##gcols", &gridCols);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.f);
    ImGui::InputInt("##grows", &gridRows);
    if (gridCols < 2)
      gridCols = 2;
    if (gridRows < 2)
      gridRows = 2;
  }
  if (kind == CreateSurfaceKind::TinVolume || kind == CreateSurfaceKind::GridVolume) {
    auto surfaceCombo = [&](const char* id, std::string* value, bool gridsOnly) {
      const char* preview = value->empty() ? "(select)" : value->c_str();
      if (!ImGui::BeginCombo(id, preview))
        return;
      for (const CadSurface& s : cmd.cadSurfaces) {
        if (gridsOnly && s.kind != SurfaceKind::Grid)
          continue;
        const bool sel = (*value == s.name);
        if (ImGui::Selectable(s.name.c_str(), sel))
          *value = s.name;
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    };
    ImGui::TextUnformatted("Base surface");
    ImGui::SameLine(140.f);
    ImGui::SetNextItemWidth(-1.f);
    surfaceCombo("##volbase", &volBase, kind == CreateSurfaceKind::GridVolume);
    ImGui::TextUnformatted("Comparison");
    ImGui::SameLine(140.f);
    ImGui::SetNextItemWidth(-1.f);
    surfaceCombo("##volcomp", &volComp, kind == CreateSurfaceKind::GridVolume);
  }

  ImGui::TextUnformatted("Surface layer");
  ImGui::SameLine(140.f);
  ImGui::SetNextItemWidth(-40.f);
  if (ImGui::BeginCombo("##cslayer", layer.c_str())) {
    for (const CadLayerRow& row : cmd.drawingLayerTable) {
      const bool sel = (layer == row.name);
      if (ImGui::Selectable(row.name.c_str(), sel))
        layer = row.name;
      if (sel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("...##cslaybtn", ImVec2(28.f, 0.f)))
    cmd.showLayerManagerWindow = true;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Layer manager");

  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(1.f, 0.97f, 0.82f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1.f, 0.99f, 0.90f, 1.f));
  if (ImGui::BeginTable("##csprops", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    ImGui::TableHeadersRow();

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Information");
    ImGui::TableNextColumn();

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(12.f, 0.f));
    ImGui::SameLine();
    ImGui::TextUnformatted("Name");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##csname", &name);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(12.f, 0.f));
    ImGui::SameLine();
    ImGui::TextUnformatted("Description");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText("##csdesc", &description);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::Dummy(ImVec2(12.f, 0.f));
    ImGui::SameLine();
    ImGui::TextUnformatted("Style");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.f);
    const char* stylePreview = styleName.empty() ? SurfaceStyles::kStandardName : styleName.c_str();
    if (ImGui::BeginCombo("##csstyle", stylePreview)) {
      for (const SurfaceStyle& ss : cmd.surfaceStyles) {
        const bool sel = (styleName == ss.name);
        if (ImGui::Selectable(ss.name.c_str(), sel))
          styleName = ss.name;
        if (sel)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleColor(2);

  ImGui::Spacing();
  ImGui::TextWrapped("Selecting OK will create a new surface which will appear in the list of surfaces in Prospector.");
  if (!error.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.25f, 1.f), "%s", error.c_str());
  }

  ImGui::Spacing();
  const float bw = 88.f;
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (bw + 8.f) * 2.f);
  ImGui::BeginDisabled(name.empty());
  if (ImGui::Button("OK", ImVec2(bw, 0.f))) {
    if (FindSurfaceIndex(cmd, name) >= 0) {
      error = "A surface named \"" + name + "\" already exists.";
    } else if (kind == CreateSurfaceKind::TinVolume || kind == CreateSurfaceKind::GridVolume) {
      if (volBase.empty() || volComp.empty())
        error = "Pick a base surface and a comparison surface.";
      else if (volBase == volComp)
        error = "Base and comparison must be different surfaces.";
      else {
        if (kind == CreateSurfaceKind::TinVolume) {
          PushUndoSnapshot(cmd, "Create volume surface");
          const int ni = CreateSurfaceFromVolumeParents(cmd, name, volBase, volComp, lg);
          if (ni >= 0) {
            ApplyCreatedSurfaceFields(cmd, name, description, styleName, layer);
            cmd.showCreateSurfaceWindow = false;
          } else
            error = lg.empty() ? "Could not create the volume surface." : lg.back();
        } else {
          SubmitLine(cmd, &lg, "SURFACECREATEVOLGRID " + name + ", " + volBase + ", " + volComp);
          if (FindSurfaceIndex(cmd, name) >= 0) {
            ApplyCreatedSurfaceFields(cmd, name, description, styleName, layer);
            cmd.showCreateSurfaceWindow = false;
          } else
            error = lg.empty() ? "Could not create the grid volume surface." : lg.back();
        }
      }
    } else if (kind == CreateSurfaceKind::Grid) {
      if (gridSx <= 0.0 || gridSy <= 0.0)
        error = "Grid spacing must be positive.";
      else {
        std::string line = "SURFACECREATEGRID " + name + ", " + std::to_string(gridOx) + ", " +
                           std::to_string(gridOy) + ", " + std::to_string(gridSx) + ", " +
                           std::to_string(gridSy) + ", " + std::to_string(gridCols) + ", " +
                           std::to_string(gridRows);
        SubmitLine(cmd, &lg, line);
        if (FindSurfaceIndex(cmd, name) >= 0) {
          ApplyCreatedSurfaceFields(cmd, name, description, styleName, layer);
          cmd.showCreateSurfaceWindow = false;
        } else
          error = lg.empty() ? "Could not create the grid surface." : lg.back();
      }
    } else if (kind == CreateSurfaceKind::Corridor) {
      SubmitLine(cmd, &lg, "SURFACECREATECORR " + name);
      if (FindSurfaceIndex(cmd, name) >= 0) {
        ApplyCreatedSurfaceFields(cmd, name, description, styleName, layer);
        cmd.showCreateSurfaceWindow = false;
      } else
        error = lg.empty() ? "Could not create the corridor surface." : lg.back();
    } else {
      PushUndoSnapshot(cmd, "Create surface");
      const int ni = CreateSurfaceFromPointGroups(cmd, name, {}, lg);
      if (ni >= 0) {
        ApplyCreatedSurfaceFields(cmd, name, description, styleName, layer);
        cmd.showCreateSurfaceWindow = false;
      } else
        error = lg.empty() ? "Could not create the surface." : lg.back();
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(bw, 0.f)))
    cmd.showCreateSurfaceWindow = false;

  ImGui::End();
}

}  // namespace

void DrawToolspaceWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  DrawCreateSurfaceWindow(cmd, log);
  DrawSurfacePropertiesWindow(cmd, log);
  if (!cmd.showToolspaceWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(300.f, 640.f), ImGuiCond_FirstUseEver);
  bool open = cmd.showToolspaceWindow;
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.14f, 0.16f, 0.18f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.18f, 0.22f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.24f, 0.26f, 1.f));
  if (!ImGui::Begin("TOOLSPACE", &open, ImGuiWindowFlags_NoCollapse)) {
    cmd.showToolspaceWindow = open;
    ImGui::End();
    ImGui::PopStyleColor(3);
    return;
  }
  cmd.showToolspaceWindow = open;

  static TsPending pending;

  const float tabW = TabStripWidth();
  ImGui::BeginChild("##ts_main", ImVec2(-tabW - 4.f, 0.f), false);

  DrawToolspaceToolbar();

  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.f, 1.f, 1.f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.96f, 0.96f, 0.96f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.1f, 0.1f, 1.f));
  const char* comboLabel = (cmd.toolspaceTab == AppCommandState::ToolspaceTab::Settings)
                               ? "Active Drawing Settings View"
                               : "Active Drawing View";
  ImGui::SetNextItemWidth(-1.f);
  if (ImGui::BeginCombo("##ts_view", comboLabel)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.f));
    if (ImGui::Selectable("Active Drawing View",
                          cmd.toolspaceTab == AppCommandState::ToolspaceTab::Prospector))
      cmd.toolspaceTab = AppCommandState::ToolspaceTab::Prospector;
    if (ImGui::Selectable("Active Drawing Settings View",
                          cmd.toolspaceTab == AppCommandState::ToolspaceTab::Settings))
      cmd.toolspaceTab = AppCommandState::ToolspaceTab::Settings;
    ImGui::PopStyleColor();
    ImGui::EndCombo();
  }
  ImGui::PopStyleColor(3);

  const float previewH = std::max(40.f, ImGui::GetContentRegionAvail().y * kPreviewFrac);
  const float treeH = std::max(80.f, ImGui::GetContentRegionAvail().y - previewH - 4.f);

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.f, 1.f, 1.f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
  ImGui::PushStyleColor(ImGuiCol_TreeLines, ImVec4(0.62f, 0.62f, 0.62f, 1.f));
  ImGuiStyle& treeStyle = ImGui::GetStyle();
  const ImGuiTreeNodeFlags prevTreeLines = treeStyle.TreeLinesFlags;
  const float prevTreeLinesSize = treeStyle.TreeLinesSize;
  const float prevTreeLinesRound = treeStyle.TreeLinesRounding;
  treeStyle.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
  treeStyle.TreeLinesSize = 1.f;
  treeStyle.TreeLinesRounding = 0.f;
  ImGui::BeginChild("##ts_tree", ImVec2(0.f, treeH), true);
  if (cmd.toolspaceTab == AppCommandState::ToolspaceTab::Settings)
    DrawSettingsTree(cmd);
  else
    DrawProspectorTree(cmd, log, pending);
  ImGui::EndChild();
  if (pending.deleteSurface >= 0 &&
      pending.deleteSurface < static_cast<int>(cmd.cadSurfaces.size())) {
    PushUndoSnapshot(cmd, "Delete surface");
    EraseSurfaceAtIndex(cmd, static_cast<size_t>(pending.deleteSurface));
  }
  pending.deleteSurface = -1;
  treeStyle.TreeLinesFlags = prevTreeLines;
  treeStyle.TreeLinesSize = prevTreeLinesSize;
  treeStyle.TreeLinesRounding = prevTreeLinesRound;
  ImGui::PopStyleColor(3);

  ImGui::BeginChild("##ts_preview", ImVec2(0.f, 0.f), true);
  ImGui::EndChild();

  ImGui::EndChild();

  ImGui::SameLine(0.f, 4.f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.20f, 0.23f, 1.f));
  ImGui::BeginChild("##ts_tabs", ImVec2(tabW, 0.f), false, ImGuiWindowFlags_NoScrollbar);
  if (SideTab("##tab_prospector", "Prospector", cmd.toolspaceTab == AppCommandState::ToolspaceTab::Prospector,
              tabW))
    cmd.toolspaceTab = AppCommandState::ToolspaceTab::Prospector;
  if (SideTab("##tab_settings", "Settings", cmd.toolspaceTab == AppCommandState::ToolspaceTab::Settings, tabW))
    cmd.toolspaceTab = AppCommandState::ToolspaceTab::Settings;
  ImGui::EndChild();
  ImGui::PopStyleColor();

  DrawToolspaceDefDialogs(cmd, log, pending);

  ImGui::End();
  ImGui::PopStyleColor(3);
}
