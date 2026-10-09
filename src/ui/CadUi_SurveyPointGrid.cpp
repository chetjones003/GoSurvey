// Survey Point Database grid (REQ-400): an editable, sortable table of every point in the active
// project's database. The toolspace Survey Database section (REQ-377, CadUi_Toolspace.cpp) already
// filters which points a drawing *shows*; this panel is the first place to see the rows themselves.
//
// Its own translation unit, following CadUi_PointGroups.cpp.

#include "CadUi.hpp"
#include "CadUiInternal.hpp"

#include "CadCommands.hpp"
#include "ProjectPoints.hpp"
#include "PointGroupRule.hpp"
#include "io/ProjectPointDb.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using Tab = AppCommandState::DrawingTab;

const AppCommandState::ProjectSession* ActiveSessionConst(const AppCommandState& st) {
  const int i = st.activeDrawingIdx;
  if (i < 1 || i >= static_cast<int>(st.drawingTabs.size()))
    return nullptr;
  const Tab& tab = st.drawingTabs[static_cast<size_t>(i)];
  if (tab.projectUid == 0 || tab.pointsMode != Tab::PointsMode::Shared)
    return nullptr;
  for (const auto& s : st.openProjects)
    if (s.uid == tab.projectUid)
      return &s;
  return nullptr;
}

/// First point group (in declared order) whose rule matches this entry, or empty.
std::string GroupNameFor(const AppCommandState& cmd, const SurveyPoint& p) {
  for (const PointGroup& g : cmd.pointGroups) {
    if (g.rule.empty())
      continue;
    std::vector<std::string> discard;
    const auto ranges = ParseIdRanges(g.rule.idRangesText, &discard);
    if (PointMatchesRule(g.rule, ranges, p.id, p.description, p.rawDescription))
      return g.name;
  }
  return std::string();
}

enum class Col { Number, Northing, Easting, Elevation, Description, Visible, Source, Group };

}  // namespace

void DrawSurveyPointGridWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  if (!cmd.showSurveyPointGridWindow)
    return;

  projpts::Db* db = ActiveProjectDb(cmd);
  if (!db) {
    cmd.showSurveyPointGridWindow = false;  // REQ-400 clause 3: no project, no panel
    return;
  }
  const AppCommandState::ProjectSession* session = ActiveSessionConst(cmd);
  const bool readOnly = session != nullptr && session->readOnly;

  bool open = cmd.showSurveyPointGridWindow;
  PushProductDialogAccent();
  // No SetNextWindowSize: the window sizes itself to the table (which itself auto-sizes its columns
  // to content, SizingFixedFit below), then stays manually resizable like every other grid window.
  if (!ImGui::Begin("Survey Point Database", &open)) {
    cmd.showSurveyPointGridWindow = open;
    ImGui::End();
    PopProductDialogAccent();
    return;
  }
  PaintProductDialogAccentFrame();
  BeginStyledDialog();
  cmd.showSurveyPointGridWindow = open;

  if (readOnly)
    ImGui::TextDisabled("This project is open read-only: every cell here is view-only.");

  std::unordered_set<int> visibleHere;
  visibleHere.reserve(cmd.surveyPoints.size());
  for (const SurveyPoint& p : cmd.surveyPoints)
    visibleHere.insert(p.id);

  static std::unordered_set<int> s_selected;  // point numbers checked in column 0 (REQ-400 clause 7)

  static ImGuiTableSortSpecs* s_lastSortSpecs = nullptr;
  std::vector<int> order(db->points.size());
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = static_cast<int>(i);

  // Excel/Sheets feel: every column is sized from its OWN widest cell (measured below) rather than a
  // guessed pixel width, and each editable widget then fills that column exactly (SetNextItemWidth(-1)
  // inside PushGridCellStyle/PopGridCellStyle, the same flat no-border/no-rounding treatment the
  // Properties panel uses) instead of the default 3D-bevelled input box.
  auto Fmt3 = [](double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    return std::string(buf);
  };
  const float pad = ImGui::GetStyle().CellPadding.x * 2.f + 8.f;
  float wNum = ImGui::CalcTextSize("Number").x;
  float wNorth = ImGui::CalcTextSize("Northing").x;
  float wEast = ImGui::CalcTextSize("Easting").x;
  float wElev = ImGui::CalcTextSize("Elevation").x;
  float wDesc = ImGui::CalcTextSize("Description").x;
  float wVis = ImGui::CalcTextSize("Visible").x;
  float wSrc = ImGui::CalcTextSize("Source drawing").x;
  float wGrp = ImGui::CalcTextSize("Point group").x;
  std::vector<std::string> northStr(db->points.size()), eastStr(db->points.size()), elevStr(db->points.size());
  std::vector<std::string> groupStr(db->points.size());
  for (size_t i = 0; i < db->points.size(); ++i) {
    const SurveyPoint& p = db->points[i].point;
    northStr[i] = Fmt3(p.northing);
    eastStr[i] = Fmt3(p.easting);
    elevStr[i] = Fmt3(p.elevation);
    groupStr[i] = GroupNameFor(cmd, p);
    if (groupStr[i].empty())
      groupStr[i] = "-";
    wNum = std::max(wNum, ImGui::CalcTextSize(std::to_string(p.id).c_str()).x);
    wNorth = std::max(wNorth, ImGui::CalcTextSize(northStr[i].c_str()).x);
    wEast = std::max(wEast, ImGui::CalcTextSize(eastStr[i].c_str()).x);
    wElev = std::max(wElev, ImGui::CalcTextSize(elevStr[i].c_str()).x);
    wDesc = std::max(wDesc, ImGui::CalcTextSize(p.description.c_str()).x);
    wSrc = std::max(wSrc, ImGui::CalcTextSize(db->points[i].sourceDrawing.c_str()).x);
    wGrp = std::max(wGrp, ImGui::CalcTextSize(groupStr[i].c_str()).x);
  }
  const float checkboxW = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
  const float colNumber = checkboxW + wNum + pad;
  const float colNorth = wNorth + pad;
  const float colEast = wEast + pad;
  const float colElev = wElev + pad;
  const float colDesc = wDesc + pad;
  const float colVis = wVis + pad;
  const float colSrc = wSrc + pad;
  const float colGrp = wGrp + pad;

  // Same contract as every other spreadsheet-style grid (kGridTableFlags: Sortable + Reorderable +
  // Resizable + a frozen header, REQ-081), with explicit WidthFixed columns (computed above) in place
  // of StretchProp so a column never clips its own widest cell, and the WINDOW auto-sizes to the sum.
  const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_Reorderable | ImGuiTableFlags_Sortable |
                                ImGuiTableFlags_SortMulti | ImGuiTableFlags_Hideable |
                                ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("##req400_grid", 8, flags)) {
    // Excel/Sheets look, header included: light background, dark text, pushed BEFORE TableHeadersRow
    // so the header picks it up too. Row fill itself is NOT done via ImGuiCol_TableRowBg/RowBgAlt —
    // explicit per-row TableSetBgColor below instead, which is immune to any row/column caching that
    // style-colour push timing can run into on a column-count or sort change.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.09f, 0.09f, 0.10f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.90f, 0.94f, 1.f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.80f, 0.89f, 1.f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0.88f, 0.88f, 0.88f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, ImVec4(0.80f, 0.80f, 0.80f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0.65f, 0.65f, 0.65f, 1.f));

    ImGui::TableSetupColumn("Number", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort,
                            colNumber, static_cast<ImGuiID>(Col::Number));
    ImGui::TableSetupColumn("Northing", ImGuiTableColumnFlags_WidthFixed, colNorth, static_cast<ImGuiID>(Col::Northing));
    ImGui::TableSetupColumn("Easting", ImGuiTableColumnFlags_WidthFixed, colEast, static_cast<ImGuiID>(Col::Easting));
    ImGui::TableSetupColumn("Elevation", ImGuiTableColumnFlags_WidthFixed, colElev, static_cast<ImGuiID>(Col::Elevation));
    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthFixed, colDesc, static_cast<ImGuiID>(Col::Description));
    ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, colVis, static_cast<ImGuiID>(Col::Visible));
    ImGui::TableSetupColumn("Source drawing", ImGuiTableColumnFlags_WidthFixed, colSrc, static_cast<ImGuiID>(Col::Source));
    ImGui::TableSetupColumn("Point group", ImGuiTableColumnFlags_WidthFixed, colGrp, static_cast<ImGuiID>(Col::Group));
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs())
      if (specs->SpecsDirty || s_lastSortSpecs != specs) {
        s_lastSortSpecs = specs;
        specs->SpecsDirty = false;
      }
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
      const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
      const Col col = static_cast<Col>(spec.ColumnUserID);
      const bool asc = spec.SortDirection == ImGuiSortDirection_Ascending;
      std::sort(order.begin(), order.end(), [&](int ia, int ib) {
        const SurveyPoint& a = db->points[static_cast<size_t>(ia)].point;
        const SurveyPoint& b = db->points[static_cast<size_t>(ib)].point;
        bool less = false;
        switch (col) {
          case Col::Number: less = a.id < b.id; break;
          case Col::Northing: less = a.northing < b.northing; break;
          case Col::Easting: less = a.easting < b.easting; break;
          case Col::Elevation: less = a.elevation < b.elevation; break;
          case Col::Description: less = a.description < b.description; break;
          case Col::Visible: less = (visibleHere.count(a.id) != 0) < (visibleHere.count(b.id) != 0); break;
          case Col::Source:
            less = db->points[static_cast<size_t>(ia)].sourceDrawing < db->points[static_cast<size_t>(ib)].sourceDrawing;
            break;
          case Col::Group: less = GroupNameFor(cmd, a) < GroupNameFor(cmd, b); break;
        }
        return asc ? less : !less;
      });
    }

    int visualRow = 0;
    for (const int rowIdx : order) {
      const projpts::Entry& entry = db->points[static_cast<size_t>(rowIdx)];
      SurveyPoint edited = entry.point;
      bool changedNumber = false, changedOther = false;
      ImGui::PushID(entry.point.id);
      ImGui::TableNextRow();
      ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                             (visualRow++ % 2 == 0) ? IM_COL32(255, 255, 255, 255) : IM_COL32(240, 240, 240, 255));

      ImGui::TableSetColumnIndex(0);
      bool checked = s_selected.count(entry.point.id) != 0;
      // Checkbox is unaffected by this cell's own FrameBg push above, so give it a border — an
      // unchecked box on a white cell is otherwise invisible (white square on white).
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.f, 1.f, 1.f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.90f, 0.94f, 1.f, 1.f));
      ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.55f, 0.58f, 1.f));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
      if (ImGui::Checkbox("##sel", &checked)) {
        const bool visible = visibleHere.count(entry.point.id) != 0;
        if (checked && visible)
          s_selected.insert(entry.point.id);
        else
          s_selected.erase(entry.point.id);
        const std::vector<int> wanted(s_selected.begin(), s_selected.end());
        const int refused = SelectDatabasePoints(cmd, wanted);
        if (!visible && checked)
          log->push_back("Survey Point Database - point " + std::to_string(entry.point.id) +
                         " is hidden in this drawing and cannot be selected here.");
        else if (refused > 0)
          log->push_back("Survey Point Database - " + std::to_string(refused) +
                         " selected point(s) are hidden in this drawing and were not selected.");
      }
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(3);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.f);
      ImGui::BeginDisabled(readOnly);
      PushGridCellStyle();
      int num = edited.id;
      if (ImGui::InputInt("##num", &num, 0, 0) && num != edited.id) {
        edited.id = num;
        changedNumber = true;
      }
      PopGridCellStyle();
      ImGui::EndDisabled();

      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-1.f);
      ImGui::BeginDisabled(readOnly);
      PushGridCellStyle();
      if (ImGui::InputDouble("##n", &edited.northing, 0.0, 0.0, "%.3f"))
        changedOther = true;
      PopGridCellStyle();
      ImGui::EndDisabled();

      ImGui::TableSetColumnIndex(2);
      ImGui::SetNextItemWidth(-1.f);
      ImGui::BeginDisabled(readOnly);
      PushGridCellStyle();
      if (ImGui::InputDouble("##e", &edited.easting, 0.0, 0.0, "%.3f"))
        changedOther = true;
      PopGridCellStyle();
      ImGui::EndDisabled();

      ImGui::TableSetColumnIndex(3);
      ImGui::SetNextItemWidth(-1.f);
      ImGui::BeginDisabled(readOnly);
      PushGridCellStyle();
      if (ImGui::InputDouble("##z", &edited.elevation, 0.0, 0.0, "%.3f"))
        changedOther = true;
      PopGridCellStyle();
      ImGui::EndDisabled();

      ImGui::TableSetColumnIndex(4);
      ImGui::SetNextItemWidth(-1.f);
      ImGui::BeginDisabled(readOnly);
      PushGridCellStyle();
      if (ImGui::InputText("##d", &edited.description))
        changedOther = true;
      PopGridCellStyle();
      ImGui::EndDisabled();

      ImGui::TableSetColumnIndex(5);
      ImGui::TextUnformatted(visibleHere.count(entry.point.id) ? "Yes" : "No");

      ImGui::TableSetColumnIndex(6);
      ImGui::TextUnformatted(entry.sourceDrawing.c_str());

      ImGui::TableSetColumnIndex(7);
      ImGui::TextUnformatted(groupStr[static_cast<size_t>(rowIdx)].c_str());

      if (!readOnly && (changedNumber || changedOther) &&
          (ImGui::IsItemDeactivatedAfterEdit() || changedNumber)) {
        const EditDatabasePointResult r = EditDatabasePoint(cmd, entry.point.id, edited, ImGui::GetTime(), *log);
        if (r.status == EditDatabasePointStatus::Collision) {
          cmd.surveyPointGridConflict.active = true;
          cmd.surveyPointGridConflict.oldNumber = entry.point.id;
          cmd.surveyPointGridConflict.newNumber = edited.id;
          cmd.surveyPointGridConflict.pendingValues = edited;
        }
      }
      ImGui::PopID();
    }
    ImGui::PopStyleColor(7);  // Text, FrameBg, FrameBgHovered, FrameBgActive, TableHeaderBg, 2x TableBorder*
    ImGui::EndTable();
  }

  if (cmd.surveyPointGridConflict.active)
    ImGui::OpenPopup("##req400_conflict");
  if (ImGui::BeginPopupModal("##req400_conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const auto& c = cmd.surveyPointGridConflict;
    ImGui::Text("Point number %d already exists in the project database.", c.newNumber);
    ImGui::TextWrapped("Overwrite replaces that point with this one's new values. Renumber keeps the "
                       "existing point and gives this edit the next free number instead.");
    if (StyledButton("Overwrite", ImVec2(0, 0), /*primary=*/true)) {
      ResolveGridNumberConflict(cmd, GridConflictAnswer::Overwrite, ImGui::GetTime(), *log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (StyledButton("Renumber", ImVec2(0, 0), /*primary=*/false)) {
      ResolveGridNumberConflict(cmd, GridConflictAnswer::Renumber, ImGui::GetTime(), *log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (StyledButton("Cancel", ImVec2(0, 0), /*primary=*/false)) {
      ResolveGridNumberConflict(cmd, GridConflictAnswer::Cancel, ImGui::GetTime(), *log);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::End();
  PopProductDialogAccent();
}
