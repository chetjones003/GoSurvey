// Feature line elevation editor (REQ-088).
//
// The table REQ-088's Statement asks for: one row per point, showing station, elevation, length to
// the next point, grade back and grade ahead. Elevation, grade back and grade ahead are editable;
// station and length are not, because they are plan geometry and this window does not move points
// in plan.
//
// **Every edit goes through the FLELEV command line**, exactly as the Surfaces panel routes through
// DESIGNATEBREAKLINE and SURFACEADDFILE. That is not ceremony: it is what keeps the panel and the
// REQ-203 driver testing the same code. A cell edited here does the same thing, in the same order,
// with the same undo step, as the transcript that tests it — so
// tests/headless/transcripts/req088-feature-line-elevation-editor.txt is a test of this window too,
// which it could not otherwise be, ImGui having no window under the driver.
//
// The table itself is DERIVED and never stored (ADR-035 (e)). It is rebuilt from the vertex chain
// every frame, so an edit made here, a MOVE made in the viewport, and an undo all show up without
// any invalidation path of their own.

#include "CadUi.hpp"

#include "CadCommands.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

/// Run one FLELEV command. Sub-actions are appended by the callers; the leading "FLELEV <n> " is
/// shared. \p flNumber is 1-based, as the command line takes it.
void RunFlElev(AppCommandState& cmd, std::vector<std::string>* log, int flNumber,
               const std::string& rest) {
  std::string line = "FLELEV " + std::to_string(flNumber) + " " + rest;
  std::vector<char> buf(line.begin(), line.end());
  buf.push_back('\0');
  ProcessCommandLineSubmit(buf.data(), static_cast<int>(buf.size()), cmd, *log);
}

/// Format a number for an editable cell. Fixed decimals rather than %g: a grade shown as "5" and
/// one shown as "5.00" are the same number, but the second is what a designer expects to see in a
/// grading table, and switching between the two as the value changes makes the column jump.
std::string CellText(double v, int decimals) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  return buf;
}

/// One editable numeric cell.
///
/// Commits on Enter or on losing focus, and only when the text actually parses to a DIFFERENT
/// number. Both halves matter: committing every frame would push an undo step per keystroke, and
/// committing an unchanged value would push an undo step for clicking into a cell and out again.
///
/// \p id must be unique per cell. Returns the new value through \p outValue when it committed.
bool NumericCell(const char* id, double current, int decimals, float width, double* outValue) {
  // ONE editing slot, not one per cell. Only one cell can hold keyboard focus at a time, so the
  // in-progress text only ever belongs to that one; a map keyed by cell id would grow with every
  // cell ever shown and be scanned once per cell per frame — quadratic on a long feature line, for
  // state that is single-valued by construction.
  static ImGuiID s_activeCell = 0;
  static std::string s_text;

  ImGui::PushID(id);
  ImGui::SetNextItemWidth(width);
  const ImGuiID key = ImGui::GetID("v");
  const bool isEditing = s_activeCell == key;

  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s", isEditing ? s_text.c_str() : CellText(current, decimals).c_str());

  // Commit when the value actually PARSES and actually CHANGED. Both halves matter: committing every
  // frame would push an undo step per keystroke, and committing an unchanged value would push one
  // for clicking into a cell and back out.
  const auto tryCommit = [&](const char* text) {
    char* end = nullptr;
    const double v = std::strtod(text, &end);
    if (!end || *end != '\0' || text[0] == '\0' || !std::isfinite(v) || std::fabs(v - current) <= 1e-9)
      return false;
    *outValue = v;
    return true;
  };

  bool committed = false;
  const bool entered = ImGui::InputText("##v", buf, sizeof(buf),
                                        ImGuiInputTextFlags_EnterReturnsTrue |
                                            ImGuiInputTextFlags_CharsDecimal);
  if (entered) {
    committed = tryCommit(buf);
    if (s_activeCell == key)
      s_activeCell = 0;
  } else if (ImGui::IsItemActive()) {
    s_activeCell = key;
    s_text = buf;
  } else if (isEditing) {
    // Focus left without Enter — commit anyway, which is what a spreadsheet does and what someone
    // tabbing down a column expects.
    committed = tryCommit(s_text.c_str());
    s_activeCell = 0;
  }
  ImGui::PopID();
  return committed;
}

} // namespace

void DrawFeatureLineElevationWindow(AppCommandState& cmd, std::vector<std::string>* log) {
  std::vector<std::string> discard;
  if (!log)
    log = &discard;
  if (!cmd.showFeatureLineElevWindow)
    return;

  ImGui::SetNextWindowSize(ImVec2(760, 460), ImGuiCond_FirstUseEver);
  bool open = cmd.showFeatureLineElevWindow;
  if (!ImGui::Begin("Feature Line Elevations", &open)) {
    cmd.showFeatureLineElevWindow = open;
    ImGui::End();
    return;
  }
  cmd.showFeatureLineElevWindow = open;
  BeginStyledDialog();

  const int flCount =
      cmd.featureLineOffsets.empty() ? 0 : static_cast<int>(cmd.featureLineOffsets.size()) - 1;
  if (flCount <= 0) {
    ImGui::TextUnformatted("No feature lines in the drawing.");
    ImGui::Spacing();
    ImGui::TextDisabled("Draw one with FEATURELINE, then reopen this window.");
    ImGui::End();
    return;
  }
  if (cmd.featureLineElevIndex >= flCount)
    cmd.featureLineElevIndex = flCount - 1;
  if (cmd.featureLineElevIndex < 0)
    cmd.featureLineElevIndex = 0;

  // --- which feature line -----------------------------------------------------------------------
  const auto labelFor = [&](int i) {
    const std::string nm =
        static_cast<size_t>(i) < cmd.featureLineInfo.size() ? cmd.featureLineInfo[i].name : std::string();
    return std::to_string(i + 1) + ": " + (nm.empty() ? std::string("(unnamed)") : nm);
  };
  ImGui::SetNextItemWidth(280);
  if (ImGui::BeginCombo("Feature line", labelFor(cmd.featureLineElevIndex).c_str())) {
    for (int i = 0; i < flCount; ++i) {
      const bool sel = i == cmd.featureLineElevIndex;
      if (ImGui::Selectable(labelFor(i).c_str(), sel))
        cmd.featureLineElevIndex = i;
      if (sel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const int flNumber = cmd.featureLineElevIndex + 1;
  std::vector<FeatureLineElevRow> rows;
  if (!BuildFeatureLineElevTable(cmd, cmd.featureLineElevIndex, &rows) || rows.empty()) {
    ImGui::TextUnformatted("That feature line has no points to show.");
    ImGui::End();
    return;
  }

  // --- raise / lower the whole line -------------------------------------------------------------
  static float raiseBy = 1.f;
  ImGui::SetNextItemWidth(120);
  ImGui::InputFloat("##raiseby", &raiseBy, 0.f, 0.f, "%.3f");
  ImGui::SameLine();
  if (ImGui::Button("Raise all"))
    RunFlElev(cmd, log, flNumber, "RAISE " + CellText(raiseBy, 6));
  ImGui::SameLine();
  if (ImGui::Button("Lower all"))
    RunFlElev(cmd, log, flNumber, "RAISE " + CellText(-static_cast<double>(raiseBy), 6));
  ImGui::SameLine();
  ImGui::TextDisabled("(REQ-088: raise or lower every point as a set)");

  ImGui::Separator();

  // Deferred edits. A command mutates the vertex arrays — and INSERT/DELETE resize them — so nothing
  // may run while the table below is being walked. Same rule the Surfaces tree follows.
  std::string pending;

  // --- the table ---------------------------------------------------------------------------------
  const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY;
  if (ImGui::BeginTable("flelev", 7, flags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 8))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Station");
    ImGui::TableSetupColumn("Elevation");
    ImGui::TableSetupColumn("Length ahead");
    ImGui::TableSetupColumn("Grade back");
    ImGui::TableSetupColumn("Grade ahead");
    ImGui::TableHeadersRow();

    for (const FeatureLineElevRow& r : rows) {
      const int pt = r.vertexIndex + 1;
      ImGui::TableNextRow();
      ImGui::PushID(pt);

      ImGui::TableNextColumn();
      ImGui::Text("%d", pt);

      ImGui::TableNextColumn();
      ImGui::TextUnformatted(r.isElevationPoint ? "elev pt" : "PI");

      // Station and length are PLAN geometry, not elevation — read-only here. Editing a station
      // would move the point along the line, which is a geometry edit (REQ-087), not this window's.
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", CellText(r.station, 3).c_str());

      ImGui::TableNextColumn();
      double v = 0.0;
      if (NumericCell("elev", static_cast<double>(r.elevation), 3, 100.f, &v))
        pending = "SET " + std::to_string(pt) + " " + CellText(v, 6);

      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", CellText(r.lengthAhead, 3).c_str());

      // A grade cell with no segment behind or ahead of it shows a dash and is not editable — there
      // is nothing for a number to mean. Printing "0.00%" and accepting an edit would invite the
      // user to set the grade of a segment that does not exist.
      ImGui::TableNextColumn();
      if (std::isnan(r.gradeBackPct)) {
        ImGui::TextDisabled("-");
      } else if (NumericCell("gb", r.gradeBackPct, 2, 80.f, &v)) {
        pending = "GRADEBACK " + std::to_string(pt) + " " + CellText(v, 6);
      }

      ImGui::TableNextColumn();
      if (std::isnan(r.gradeAheadPct)) {
        ImGui::TextDisabled("-");
      } else if (NumericCell("ga", r.gradeAheadPct, 2, 80.f, &v)) {
        pending = "GRADEAHEAD " + std::to_string(pt) + " " + CellText(v, 6);
      }

      // Deleting is offered only where it is legal. A PI's row has no Delete at all rather than a
      // Delete that refuses — REQ-084's 2026-08-18 revision: no control may be present that cannot
      // act. The command still refuses a PI, because the command line can be typed.
      if (r.isElevationPoint) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
          pending = "DELETE " + std::to_string(pt);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Delete this elevation point");
      }

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  // --- add an elevation point ---------------------------------------------------------------------
  static float insStation = 0.f;
  static float insElev = 0.f;
  ImGui::SetNextItemWidth(90);
  ImGui::InputFloat("Station", &insStation, 0.f, 0.f, "%.3f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90);
  ImGui::InputFloat("Elevation##ins", &insElev, 0.f, 0.f, "%.3f");
  ImGui::SameLine();
  if (ImGui::Button("Add elevation point"))
    pending = "INSERT " + CellText(insStation, 6) + " " + CellText(insElev, 6);

  ImGui::End();

  if (!pending.empty())
    RunFlElev(cmd, log, flNumber, pending);
}
