#include "CadUi.hpp"
#include "CadUiHelpers.hpp"
#include "CadCommands.hpp"
#include "SurveyPoints.hpp"
#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"
#include "traverse/FbkImport.hpp"
#include "traverse/StarNetImport.hpp"
#include "traverse/Rw5Import.hpp"
#include "traverse/JobXmlImport.hpp"
#include "traverse/TraverseIo.hpp"
#include "traverse/TraverseCommandLang.hpp"
#include "WinFileDialogs.hpp"
#include "ProjectFiles.hpp"
#include "FontRegistry.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Parse buffer into leg's horizontal angle; call on IsItemDeactivatedAfterEdit.
static void ParseHorizAngle(TraverseLeg& leg) {
    if (leg.horizAngleBuf.empty()) {
        leg.hasHorizAngle = false;
        return;
    }
    double v;
    if (TraverseParseAngle(leg.horizAngleBuf, &v)) {
        leg.horizAngleDeg = v;
        leg.hasHorizAngle = true;
    }
    // If parse fails, keep previous value and flag; user is mid-edit.
}

static void ParseVertAngle(TraverseLeg& leg) {
    if (leg.vertAngleBuf.empty()) {
        leg.hasVertAngle = false;
        return;
    }
    double v;
    if (TraverseParseAngle(leg.vertAngleBuf, &v)) {
        leg.vertAngleDeg = v;
        leg.hasVertAngle = true;
    }
}

// REQ-403 (#834): if the active drawing belongs to a project and \p path lies inside that project's
// folder, track it (the same mechanism PDFs/point clouds use) so it travels with Pack/Turnover. A
// standalone drawing, or a file saved outside the project folder, is left exactly as saved.
static void TrackTraverseFileInProject(AppCommandState& cmd, const std::string& path,
                                       std::vector<std::string>& log) {
    const int tab = cmd.activeDrawingIdx;
    if (tab < 1 || tab >= static_cast<int>(cmd.drawingTabs.size()))
        return;
    const std::uint32_t uid = cmd.drawingTabs[static_cast<size_t>(tab)].projectUid;
    if (uid == 0)
        return;
    const AppCommandState::ProjectSession* s = nullptr;
    for (const auto& ps : cmd.openProjects)
        if (ps.uid == uid)
            s = &ps;
    if (s == nullptr || s->readOnly || !projfiles::IsInsideProject(s->project, std::filesystem::u8path(path)))
        return;
    TrackProjectFile(cmd, uid, path, log);
}

static void ParseStartBearing(TraverseData& td) {
    if (td.startBearingBuf.empty()) {
        td.hasStartBearing = false;
        return;
    }
    double v;
    if (TraverseParseAngle(td.startBearingBuf, &v)) {
        td.startBearingDeg = v;
        td.hasStartBearing = true;
    }
}

// Right-align text within the current table cell (for numeric columns).
static void RightAlignedText(const char* s) {
    const float textW = ImGui::CalcTextSize(s).x;
    const float availW = ImGui::GetContentRegionAvail().x;
    if (availW > textW)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - textW));
    ImGui::TextUnformatted(s);
}

// Reduced (face-averaged) horizontal circle reading for one set.
static double SetReducedHoriz(const TraverseMeasSet& s) {
    if (s.hasF1 && s.hasF2) return TraverseAverageFaceHoriz(s.f1HzDec, s.f2HzDec);
    if (s.hasF1) return s.f1HzDec;
    return s.f2HzDec;
}
static double SetReducedZenith(const TraverseMeasSet& s) {
    if (s.hasF1 && s.hasF2) return TraverseAverageFaceZenith(s.f1VaDec, s.f2VaDec);
    if (s.hasF1) return s.f1VaDec;
    return s.f2VaDec;
}
static double SetReducedDist(const TraverseMeasSet& s) {
    if (s.hasF1 && s.hasF2) return 0.5 * (s.f1Sd + s.f2Sd);
    if (s.hasF1) return s.f1Sd;
    return s.f2Sd;
}

// One editable numeric cell. Returns true on commit (deactivated after edit).
static bool SetCellDouble(const char* id, double* v, const char* fmt, bool enabled) {
    if (!enabled) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputDouble(id, v, 0., 0., fmt);
    const bool committed = ImGui::IsItemDeactivatedAfterEdit();
    if (!enabled) ImGui::EndDisabled();
    return committed;
}

// ---- Per-leg observation editor (REQ-018) ----
// Editable observation sets for one expanded leg: add/remove sets, edit the
// literal F1/F2 circle readings, slope distances, and zenith angles. Any edit
// re-reduces the leg from its sets (ADR-003) and marks the traverse dirty.
// Below the editor it shows the per-leg statistics (REQ-011) and the
// complementary distance (REQ-012).
static void DrawLegObservationEditor(TraverseLeg& leg, size_t legIndex, bool& dirty) {
    ImGui::PushID(static_cast<int>(legIndex));

    char title[112];
    std::snprintf(title, sizeof(title),
                  "Leg %zu  \xe2\x86\x92  Station %d \xe2\x80\x94 observations  (%zu set%s)",
                  legIndex + 1, leg.stationId, leg.rawSets.size(),
                  leg.rawSets.size() == 1 ? "" : "s");
    ImGui::SeparatorText(title);

    bool changed = false;
    int  removeIdx = -1;

    const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("editsets", 10, tf)) {
        ImGui::TableSetupColumn("Set",      ImGuiTableColumnFlags_WidthFixed, 30.f);
        ImGui::TableSetupColumn("F1",       ImGuiTableColumnFlags_WidthFixed, 24.f);
        ImGui::TableSetupColumn("F1 Hz\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed, 96.f);
        ImGui::TableSetupColumn("F1 SD",    ImGuiTableColumnFlags_WidthFixed, 84.f);
        ImGui::TableSetupColumn("F1 VA\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed, 96.f);
        ImGui::TableSetupColumn("F2",       ImGuiTableColumnFlags_WidthFixed, 24.f);
        ImGui::TableSetupColumn("F2 Hz\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed, 96.f);
        ImGui::TableSetupColumn("F2 SD",    ImGuiTableColumnFlags_WidthFixed, 84.f);
        ImGui::TableSetupColumn("F2 VA\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed, 96.f);
        ImGui::TableSetupColumn("",         ImGuiTableColumnFlags_WidthFixed, 26.f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();

        for (size_t si = 0; si < leg.rawSets.size(); ++si) {
            TraverseMeasSet& s = leg.rawSets[si];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(si));

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", s.setNo);

            // Face-1 presence + values
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##hasf1", &s.hasF1)) changed = true;
            ImGui::TableNextColumn(); changed |= SetCellDouble("##f1hz", &s.f1HzDec, "%.5f", s.hasF1);
            ImGui::TableNextColumn(); changed |= SetCellDouble("##f1sd", &s.f1Sd,    "%.4f", s.hasF1);
            ImGui::TableNextColumn(); changed |= SetCellDouble("##f1va", &s.f1VaDec, "%.5f", s.hasF1);

            // Face-2 presence + values
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##hasf2", &s.hasF2)) changed = true;
            ImGui::TableNextColumn(); changed |= SetCellDouble("##f2hz", &s.f2HzDec, "%.5f", s.hasF2);
            ImGui::TableNextColumn(); changed |= SetCellDouble("##f2sd", &s.f2Sd,    "%.4f", s.hasF2);
            ImGui::TableNextColumn(); changed |= SetCellDouble("##f2va", &s.f2VaDec, "%.5f", s.hasF2);

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("\xe2\x9c\x95")) removeIdx = static_cast<int>(si);
            ItemHelpTooltip("Remove this observation set.");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("+ Add Observation")) {
        TraverseMeasSet ms;
        ms.setNo  = static_cast<int>(leg.rawSets.size()) + 1;
        ms.hasF1  = true;          // start as a face-1 reading; user can toggle F2 on.
        ms.f1VaDec = 90.0;
        if (!leg.rawSets.empty()) {
            // Seed from the previous set so a repeat observation is quick to enter.
            const TraverseMeasSet& prev = leg.rawSets.back();
            ms.f1HzDec = prev.f1HzDec; ms.f1Sd = prev.f1Sd; ms.f1VaDec = prev.f1VaDec;
        }
        leg.rawSets.push_back(ms);
        changed = true;
    }
    ItemHelpTooltip("Append another observation set to this leg.\n"
                    "The leg's angle and distance re-average over all sets.");

    if (removeIdx >= 0) {
        leg.rawSets.erase(leg.rawSets.begin() + removeIdx);
        changed = true;
    }
    if (changed) {
        for (size_t si = 0; si < leg.rawSets.size(); ++si)   // keep set numbers contiguous.
            leg.rawSets[si].setNo = static_cast<int>(si) + 1;
        ReduceLegFromSets(leg);
        dirty = true;
    }

    // ---- Statistics over the reduced per-set values (REQ-011) ----
    std::vector<double> hz, dist, va;
    for (const TraverseMeasSet& s : leg.rawSets) {
        hz.push_back(SetReducedHoriz(s));
        dist.push_back(SetReducedDist(s));
        va.push_back(SetReducedZenith(s));
    }
    const StatSummary sh = ComputeStats(hz);
    const StatSummary sd = ComputeStats(dist);
    const StatSummary sv = ComputeStats(va);
    ImGui::TextUnformatted("Reduced (face-averaged) per-set statistics:");
    if (ImGui::BeginTable("rawstats", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Quantity");
        ImGui::TableSetupColumn("N");
        ImGui::TableSetupColumn("Mean");
        ImGui::TableSetupColumn("Sum");
        ImGui::TableSetupColumn("Std Dev");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        auto statRow = [](const char* name, const StatSummary& s, const char* fmt) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
            ImGui::TableNextColumn(); ImGui::Text("%d", s.n);
            char b[32];
            ImGui::TableNextColumn(); std::snprintf(b, sizeof(b), fmt, s.mean);   ImGui::TextUnformatted(b);
            ImGui::TableNextColumn(); std::snprintf(b, sizeof(b), fmt, s.sum);    ImGui::TextUnformatted(b);
            ImGui::TableNextColumn(); std::snprintf(b, sizeof(b), fmt, s.stddev); ImGui::TextUnformatted(b);
        };
        statRow("Horizontal\xc2\xb0", sh, "%.6f");
        statRow("Distance",     sd, "%.4f");
        statRow("Zenith\xc2\xb0",     sv, "%.6f");
        ImGui::EndTable();
    }

    // ---- Complementary distance (REQ-012): show both slope and horizontal. ----
    const double vaDeg = sv.n > 0 ? sv.mean
                       : (leg.hasVertAngle ? leg.vertAngleDeg : 90.0);
    double horiz = leg.computed ? leg.computedHorizDist
                 : (leg.hasHorizDist ? leg.horizDist
                                     : TraverseReduceToHoriz(leg.slopeDist, vaDeg, true));
    double slope = leg.hasSlopeDist ? leg.slopeDist
                                    : TraverseSlopeFromHoriz(horiz, vaDeg, true);
    ImGui::Text("Horizontal distance: %.4f    Slope distance: %.4f    (zenith %.5f\xc2\xb0)",
                horiz, slope, vaDeg);

    ImGui::PopID();
}

// ---- Closure analysis window: Unadjusted vs Least Squares (REQ-014–017) ----
static const char* LsaMeasKindName(LsaMeasKind kind) {
    switch (kind) {
    case LsaMeasKind::Angle: return "Angle";
    case LsaMeasKind::Distance: return "Distance";
    case LsaMeasKind::Vertical: return "Vertical";
    case LsaMeasKind::GpsEast: return "GPS E";
    case LsaMeasKind::GpsNorth: return "GPS N";
    case LsaMeasKind::GpsUp: return "GPS U";
    case LsaMeasKind::CoordEast: return "Easting";
    case LsaMeasKind::CoordNorth: return "Northing";
    case LsaMeasKind::CoordUp: return "Elev";
    case LsaMeasKind::Zenith: return "Zenith";
    case LsaMeasKind::Direction: return "Direction";
    }
    return "";
}

static void DrawStationOrBlank(int stationId, bool referenceBearing) {
    if (stationId < 0)
        ImGui::TextUnformatted(referenceBearing ? "REF" : "");
    else
        ImGui::Text("%d", stationId);
}

static void DrawLsaMeasurementResiduals(const LsaResult& res, const char* distUnit) {
    ImGui::TextDisabled(
        "Angle adjusted values are degrees; angle residuals and standard errors are arc-seconds. "
        "Distance, vertical, and GPS values are %s. A mark appears when the standardized residual exceeds 3.",
        distUnit);
    if (ImGui::BeginTable("meas_residuals", 8, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                   ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("At");
        ImGui::TableSetupColumn("From");
        ImGui::TableSetupColumn("To");
        ImGui::TableSetupColumn("Adjusted");
        ImGui::TableSetupColumn("Residual");
        ImGui::TableSetupColumn("Std err");
        ImGui::TableSetupColumn("Std res");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        for (const LsaMeasResidual& row : res.measResiduals) {
            const bool angle = row.kind == LsaMeasKind::Angle;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(LsaMeasKindName(row.kind));
            ImGui::TableNextColumn(); DrawStationOrBlank(row.atStationId, false);
            ImGui::TableNextColumn(); DrawStationOrBlank(row.fromStationId, angle);
            ImGui::TableNextColumn(); ImGui::Text("%d", row.toStationId);
            ImGui::TableNextColumn(); ImGui::Text("%.4f", row.adjusted);
            ImGui::TableNextColumn(); ImGui::Text("%+.4f", row.residual);
            ImGui::TableNextColumn(); ImGui::Text("%.4f", row.stdError);
            ImGui::TableNextColumn();
            if (row.flagged) ImGui::Text("%+.2f *", row.stdResidual);
            else ImGui::Text("%+.2f", row.stdResidual);
        }
        ImGui::EndTable();
    }
    if (!res.kindSummaries.empty()) {
        ImGui::SeparatorText("By measurement kind");
        if (ImGui::BeginTable("meas_summary", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("Count");
            ImGui::TableSetupColumn("Sum sq std res");
            ImGui::TableSetupColumn("Error factor");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();
            for (const LsaKindSummary& summary : res.kindSummaries) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(LsaMeasKindName(summary.kind));
                ImGui::TableNextColumn(); ImGui::Text("%d", summary.count);
                ImGui::TableNextColumn(); ImGui::Text("%.4f", summary.sumSqStdResidual);
                ImGui::TableNextColumn(); ImGui::Text("%.4f", summary.errorFactor);
            }
            ImGui::EndTable();
        }
    }
    if (res.chiSquare.evaluated) {
        const char* verdict = "passed";
        if (!res.chiSquare.passed)
            verdict = res.chiSquare.exceededUpper ? "exceeded the upper bound" : "exceeded the lower bound";
        ImGui::Text("Chi-square test at 5%%: %s (error factor %.4f, bounds %.4f to %.4f)",
                    verdict, res.chiSquare.errorFactor, res.chiSquare.lowerFactor, res.chiSquare.upperFactor);
    }
}

static void AppendLsaResidualReport(std::string& report, const LsaResult& result) {
    report += "\n--- Measurements ---\n";
    report += "Kind      At   From To     Adjusted    Residual    StdErr   StdRes\n";
    char buf[160];
    for (const LsaMeasResidual& row : result.measResiduals) {
        const std::string at = row.atStationId < 0 ? std::string(" ") : std::to_string(row.atStationId);
        const std::string from = row.fromStationId < 0
            ? std::string(row.kind == LsaMeasKind::Angle ? "REF" : " ")
            : std::to_string(row.fromStationId);
        std::snprintf(buf, sizeof(buf), "%-8s %4s %4s %4d %12.4f %11.4f %9.4f %8.2f%s\n",
                      LsaMeasKindName(row.kind), at.c_str(), from.c_str(), row.toStationId,
                      row.adjusted, row.residual, row.stdError, row.stdResidual,
                      row.flagged ? " *" : "");
        report += buf;
    }
    report += "\n--- By kind ---\n";
    for (const LsaKindSummary& summary : result.kindSummaries) {
        std::snprintf(buf, sizeof(buf), "%-8s  n=%d  sumSq=%.4f  factor=%.4f\n",
                      LsaMeasKindName(summary.kind), summary.count, summary.sumSqStdResidual,
                      summary.errorFactor);
        report += buf;
    }
    if (result.chiSquare.evaluated) {
        const char* verdict = "passed";
        if (!result.chiSquare.passed)
            verdict = result.chiSquare.exceededUpper ? "exceeded the upper bound" : "exceeded the lower bound";
        std::snprintf(buf, sizeof(buf),
                      "Chi-square test at 5%%: %s (error factor %.4f, bounds %.4f to %.4f)\n",
                      verdict, result.chiSquare.errorFactor, result.chiSquare.lowerFactor,
                      result.chiSquare.upperFactor);
        report += buf;
    }
}

static void DrawTraverseClosureWindow(AppCommandState& cmd, std::vector<std::string>& log) {
    if (!cmd.showTraverseClosureWindow)
        return;

    TraverseData& td = cmd.traverseData;

    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);
    bool open = cmd.showTraverseClosureWindow;
    if (!ImGui::Begin("Traverse Closure Analysis", &open)) {
        cmd.showTraverseClosureWindow = open;
        ImGui::End();
        return;
    }
    cmd.showTraverseClosureWindow = open;

    auto recompute = [&] {
        cmd.traverseLsaResult = ComputeTraverseLeastSquares(td, cmd.traverseLsaWeights);
        cmd.traverseLsaComputed = true;
        cmd.traverseLsaAccepted = false;
    };
    if (!cmd.traverseLsaComputed)
        recompute();

    // Active drawing's linear unit abbreviation (issue #831): labels below reflect this
    // instead of hardcoding "ft". The underlying values are unit-agnostic doubles —
    // whatever unit the project's legs were entered in.
    const char* distUnit = CadDrawingInsUnitsAbbrev(cmd.drawingInsUnits);
    char sigmaSdLabel[48];
    std::snprintf(sigmaSdLabel, sizeof(sigmaSdLabel), "Dist \xcf\x83 (%s)##lsa_sd", distUnit);

    // ---- A-priori standard errors (weights) ----
    ImGui::SeparatorText("A-priori Standard Errors");
    bool wChanged = false;
    ImGui::SetNextItemWidth(120.f);
    wChanged |= ImGui::InputDouble("Angle \xcf\x83 (sec)##lsa_sa", &cmd.traverseLsaWeights.sigmaAngleSec, 0., 0., "%.2f");
    ImGui::SameLine(0, 16); ImGui::SetNextItemWidth(120.f);
    wChanged |= ImGui::InputDouble(sigmaSdLabel, &cmd.traverseLsaWeights.sigmaDistConstFt, 0., 0., "%.4f");
    ImGui::SameLine(0, 16); ImGui::SetNextItemWidth(120.f);
    wChanged |= ImGui::InputDouble("Dist ppm##lsa_ppm", &cmd.traverseLsaWeights.sigmaDistPpm, 0., 0., "%.2f");
    if (!td.gpsVectors.empty()) {
        ImGui::SetNextItemWidth(120.f);
        char sigmaVdLabel[48];
        std::snprintf(sigmaVdLabel, sizeof(sigmaVdLabel), "Vert \xcf\x83 (%s)##lsa_vd", distUnit);
        wChanged |= ImGui::InputDouble(sigmaVdLabel, &cmd.traverseLsaWeights.sigmaVertConstFt, 0., 0., "%.4f");
        ImGui::SameLine(0, 16); ImGui::SetNextItemWidth(120.f);
        wChanged |= ImGui::InputDouble("Vert ppm##lsa_vppm", &cmd.traverseLsaWeights.sigmaVertPpm, 0., 0., "%.2f");
        ImGui::SameLine(0, 16);
        ImGui::TextDisabled("(used when GPS vectors are present)");
    }
    ImGui::SameLine(0, 16);
    if (ImGui::Button("Recompute") || wChanged)
        recompute();

    const LsaResult& res = cmd.traverseLsaResult;

    if (ImGui::BeginTabBar("closure_tabs")) {
        // ---------------------------------------------------- Closure summary --
        if (ImGui::BeginTabItem("Closure Summary")) {
            if (ImGui::BeginTable("summary", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Unadjusted");
                ImGui::TableSetupColumn("Least Squares");
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                ImGui::TableHeadersRow();
                ImGui::PopStyleColor();

                ImGui::TableNextRow();
                // --- Unadjusted column ---
                ImGui::TableNextColumn();
                if (td.closureValid) {
                    ImGui::Text("Misclosure \xce\x94""E: %.4f", td.closureDeltaE);
                    ImGui::Text("Misclosure \xce\x94""N: %.4f", td.closureDeltaN);
                    ImGui::Text("Linear misclosure: %.4f", td.closureLinear);
                    ImGui::Text("Perimeter: %.4f", td.closurePerimeter);
                    if (td.closureLinear > 1e-6)
                        ImGui::Text("Precision: 1:%.0f", td.closurePrecision);
                    else
                        ImGui::TextUnformatted("Precision: perfect");
                } else {
                    ImGui::TextDisabled("No closure (set Closed Loop and compute).");
                }
                // --- Least-squares column ---
                ImGui::TableNextColumn();
                if (res.ok) {
                    ImGui::Text("Observations: %d", res.observations);
                    ImGui::Text("Unknowns: %d", res.unknowns);
                    ImGui::Text("Redundancy: %d", res.redundancy);
                    ImGui::Text("Std dev of unit weight: %.4f", res.refStdDev);
                    ImGui::Text("Adjusted misclosure: %.6f", res.adjClosureLinear);
                    ImGui::Text("Iterations: %d", res.iterations);
                    if (res.usedGpsVectors) {
                        ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.f, 1.f),
                            "GPS vectors folded in: %d (elevation solved)", res.gpsVectorsUsed);
                        if (res.gpsVectorsSkipped > 0)
                            ImGui::TextDisabled("GPS vectors skipped (no covariance / degenerate): %d",
                                                 res.gpsVectorsSkipped);
                    } else if (!td.gpsVectors.empty()) {
                        ImGui::TextDisabled("GPS vectors present but none usable (need resolved covariance).");
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.f, 0.5f, 0.3f, 1.f), "Least squares unavailable:");
                    ImGui::TextWrapped("%s", res.message.c_str());
                }
                ImGui::EndTable();
            }

            ImGui::Separator();
            if (!res.ok) ImGui::BeginDisabled();
            if (ImGui::Button("Accept Least-Squares Result")) {
                // Write adjusted coordinates back into the legs so a subsequent
                // Commit to Drawing uses the adjusted positions.
                for (size_t s = 0; s < res.stationIds.size(); ++s) {
                    for (auto& leg : td.legs) {
                        if (leg.stationId == res.stationIds[s]) {
                            leg.computedEasting  = res.adjEasting[s];
                            leg.computedNorthing = res.adjNorthing[s];
                            if (res.usedGpsVectors)
                                leg.computedElevation = res.adjElevation[s];
                        }
                    }
                }
                cmd.traverseLsaAccepted = true;
                log.push_back("TRAVERSE — accepted least-squares adjustment (adjusted misclosure " +
                              TraverseFormatDist(res.adjClosureLinear) + ").");
            }
            if (!res.ok) ImGui::EndDisabled();
            if (cmd.traverseLsaAccepted) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.f), "\xe2\x9c\x93 accepted");
            }
            ImGui::EndTabItem();
        }

        // ---------------------------------------------------------- Residuals --
        if (ImGui::BeginTabItem("Residuals")) {
            if (!res.ok) {
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.3f, 1.f), "No residuals: %s", res.message.c_str());
            } else {
                DrawLsaMeasurementResiduals(res, distUnit);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ---- Command-style text view (REQ-404, GitHub issue #845 Phase A) ----
// ---- Command tab: line numbers + basic syntax highlighting -----------------
// The real editing (caret, selection, undo, IME) is still an ordinary
// InputTextMultiline — its glyphs are just made transparent — with a
// non-interactive colorized overlay drawn on top (ImGuiWindowFlags_NoInputs,
// so clicks/typing pass straight through to the real box underneath) and a
// line-number gutter beside it. Both are kept in lockstep with the real box's
// scroll position via ImGui's internal input-text state (imgui_internal.h,
// already used elsewhere in this codebase — CadUi.cpp, SplashScreen.cpp).
namespace travcmdui {

bool IsKeyword(const std::string& upperTok) {
    static const char* const kKeywords[] = {
        "STATION", "BEARING", "LOOP", "SCALE", "ERRORS",
        "STN", "HANG", "HDIST", "SDIST", "VANG", "DESC", "PRISM", "POLE", "TSERR",
        "CTRL", "ANGLE", "DIST", "AZIMUTH", "EDIF", "DIR",
        "ON", "OFF", "ZENITH", "ELEV", "SIDE", "CHECK", "COMBINED", "GRID", "REF",
    };
    for (const char* kw : kKeywords)
        if (upperTok == kw) return true;
    return false;
}

bool IsNumber(const std::string& tok) {
    if (tok.empty()) return false;
    size_t i = 0;
    if (tok[i] == '-' || tok[i] == '+') ++i;
    if (i >= tok.size()) return false;
    bool sawDigit = false, sawDot = false;
    for (; i < tok.size(); ++i) {
        if (tok[i] >= '0' && tok[i] <= '9') sawDigit = true;
        else if (tok[i] == '.' && !sawDot) sawDot = true;
        else return false;
    }
    return sawDigit;
}

// The GSTC header directives and per-leg commands (not argument-value tokens like
// ON/OFF/ZENITH — those aren't "commands" in their own right) — used for the
// Command tab's fuzzy autocomplete. Usage strings mirror TraverseCommandLang.hpp.
struct GstcCommandDef { const char* name; const char* usage; };
const GstcCommandDef kGstcCommands[] = {
    {"STATION", "<id> <e> <n> <z>"},
    {"BEARING", "<deg>"},
    {"ENDPOINT","<e> <n> <z>  (open-traverse known end point, for least squares)"},
    {"LOOP",    "ON|OFF"},
    {"SCALE",   "ON|OFF [COMBINED|GRID]"},
    {"ERRORS",  "<angle-arcsec> <dist-const> <dist-ppm>"},
    {"STN",     "<at> <to> [SIDE | CHECK [known-e known-n]]"},
    {"HANG",    "<from> <at> <to> <angle-deg>"},
    {"HDIST",   "<at> <to> <dist>"},
    {"SDIST",   "<at> <to> <dist>"},
    {"VANG",    "<at> <to> <angle-deg> [ZENITH|ELEV]"},
    {"DESC",    "<at> <to> <text to end of line>"},
    {"PRISM",   "<at> <to> <constant> <error>"},
    {"POLE",    "<at> <to> <error>"},
    {"TSERR",   "<at> <to> <angle-arcsec> <dist-const> <dist-ppm>"},
};
constexpr int kGstcCommandCount = static_cast<int>(sizeof(kGstcCommands) / sizeof(kGstcCommands[0]));
// The table above is ordered header directives first, then per-leg directives
// (matches TraverseCommandLang.hpp's own doc comment grouping) — the Records
// menu (DrawTraverseMenuBar) splits on this count rather than tagging each
// entry individually.
constexpr int kGstcHeaderCommandCount = 6;  // STATION, BEARING, ENDPOINT, LOOP, SCALE, ERRORS.

// Case-insensitive fuzzy match against kGstcCommands: an exact prefix ranks above a
// subsequence match (same two-tier idea as the main CAD command bar's own fuzzy
// finder, CadCommands.cpp — just scoped to this tiny, fixed keyword set rather than
// sharing that implementation). \p queryUpper must already be upper-cased.
std::vector<int> FuzzyGstcMatches(const std::string& queryUpper, int maxResults) {
    std::vector<std::pair<int, int>> scored;  // (tier, index), lower tier first.
    for (int i = 0; i < kGstcCommandCount; ++i) {
        const std::string name = kGstcCommands[i].name;
        if (queryUpper.empty()) { scored.push_back({0, i}); continue; }
        if (name.compare(0, queryUpper.size(), queryUpper) == 0) { scored.push_back({0, i}); continue; }
        size_t qi = 0;
        for (size_t ni = 0; ni < name.size() && qi < queryUpper.size(); ++ni)
            if (name[ni] == queryUpper[qi]) ++qi;
        if (qi == queryUpper.size()) scored.push_back({1, i});
    }
    std::stable_sort(scored.begin(), scored.end());
    std::vector<int> out;
    for (const auto& p : scored) {
        out.push_back(p.second);
        if (static_cast<int>(out.size()) >= maxResults) break;
    }
    return out;
}

// Tab-completion while the real InputTextMultiline is focused: the top match (set
// just before the call, from last frame's cursor position) is spliced in via the
// callback's own DeleteChars/InsertChars, which is the only safe way to mutate an
// actively-focused InputText's buffer (an external std::string edit would otherwise
// be clobbered by the widget's own internal copy on the very same frame).
struct GstcCompletionState {
    bool pending = false;
    std::string word;
};
GstcCompletionState& CompletionState() {
    static GstcCompletionState s;
    return s;
}

int GstcTextEditCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackCompletion) return 0;
    GstcCompletionState& st = CompletionState();
    if (!st.pending || st.word.empty()) return 0;
    st.pending = false;
    const char* buf = data->Buf;
    const int cursor = data->CursorPos;
    int lineStart = cursor;
    while (lineStart > 0 && buf[lineStart - 1] != '\n') --lineStart;
    int tokenEnd = lineStart;
    while (tokenEnd < data->BufTextLen && buf[tokenEnd] != ' ' && buf[tokenEnd] != '\t' && buf[tokenEnd] != '\n')
        ++tokenEnd;
    if (cursor > tokenEnd) return 0;  // cursor has moved past the leading token.
    data->DeleteChars(lineStart, tokenEnd - lineStart);
    const std::string insert = st.word + " ";
    data->InsertChars(lineStart, insert.c_str());
    return 0;
}

// Draws one source line as alternating whitespace/word runs, reproducing the
// exact characters (so it lines up pixel-for-pixel with the real, invisible
// text beneath it) with each word run colored by its token kind. The caller
// must already have zeroed ItemSpacing so lines stack at exactly font height,
// matching InputTextMultiline's own internal line pitch.
void DrawHighlightedLine(const std::string& line) {
    if (line.empty()) {
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
        return;
    }
    // Tuned for the LSA Editor's light theme (PushLsaLightTheme) — a white-ish
    // background, so these need to be dark enough to read, not the light-on-dark
    // palette a code editor would use on a black background.
    static const ImVec4 kKeywordCol(0.00f, 0.22f, 0.70f, 1.0f);
    static const ImVec4 kNumberCol(0.04f, 0.42f, 0.34f, 1.0f);
    static const ImVec4 kCommentCol(0.42f, 0.47f, 0.42f, 1.0f);
    static const ImVec4 kCodeCol(0.55f, 0.28f, 0.05f, 1.0f);
    const ImVec4 defaultCol = ImGui::GetStyle().Colors[ImGuiCol_Text];

    const size_t firstNonSpace = line.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos && firstNonSpace + 1 < line.size() && line[firstNonSpace] == '/' &&
        line[firstNonSpace + 1] == '/') {
        ImGui::TextColored(kCommentCol, "%s", line.c_str());  // whole-line comment.
        return;
    }

    bool first = true;
    bool inCode = false;
    size_t i = 0;
    while (i < line.size()) {
        const bool isSpace = (line[i] == ' ' || line[i] == '\t');
        size_t j = i;
        while (j < line.size() && ((line[j] == ' ' || line[j] == '\t') == isSpace)) ++j;
        const std::string run = line.substr(i, j - i);
        if (!inCode && !isSpace && !run.empty() && (run[0] == '\'' || run[0] == '`')) inCode = true;
        if (!first) ImGui::SameLine(0.0f, 0.0f);
        first = false;
        if (inCode) {
            ImGui::TextColored(kCodeCol, "%s", run.c_str());
        } else if (isSpace) {
            ImGui::TextUnformatted(run.c_str());
        } else {
            std::string upper = run;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!upper.empty() && upper[0] == '#')
                ImGui::TextColored(kKeywordCol, "%s", run.c_str());
            else if (IsKeyword(upper))
                ImGui::TextColored(kKeywordCol, "%s", run.c_str());
            else if (IsNumber(run))
                ImGui::TextColored(kNumberCol, "%s", run.c_str());
            else
                ImGui::TextColored(defaultCol, "%s", run.c_str());
        }
        i = j;
    }
}

}  // namespace travcmdui

// Merges `imported` onto the end of `into`: when `into` is still empty (the common
// case — the very first import into a blank traverse), this is just a replace. When
// `into` already holds legs/side shots/GPS vectors, `imported`'s are appended instead
// of clobbering them, so importing several raw-data files in one go (or one at a time,
// via repeated Import clicks) builds up one continuous traverse rather than each file
// stomping the last (issue #848 follow-up — a .godat file holds the merged result).
//
// GSTC only models ONE sequential chain with globally-unique station numbers (every
// STN's TO station id must be unique — TraverseCommandLang.cpp's `toIndex`/`sideIndex`
// check). Each raw-file importer numbers its own stations from scratch, so appending a
// second file's legs verbatim collides with the first file's ids (and was rejected by
// the parser with "station N already declared with STN"). Every id in the imported
// block is renumbered to a fresh, unused id before it's appended; its own start
// station (the point the imported side shots/GPS vectors may be anchored to) maps onto
// wherever `into`'s chain currently ends, since the new legs continue from there.
static void MergeImportedTraverse(TraverseData& into, TraverseData&& imported) {
    if (into.legs.empty() && into.sideShots.empty() && into.gpsVectors.empty()) {
        into = std::move(imported);
        return;
    }

    int maxUsedId = into.startStationId;
    for (const TraverseLeg& leg : into.legs) maxUsedId = std::max(maxUsedId, leg.stationId);
    for (const TraverseSideShot& ss : into.sideShots)
        maxUsedId = std::max({maxUsedId, ss.atStationId, ss.stationId});
    for (const TraverseGpsVector& gv : into.gpsVectors)
        maxUsedId = std::max({maxUsedId, gv.fromStationId, gv.toStationId});

    const int chainEndId = into.legs.empty() ? into.startStationId : into.legs.back().stationId;
    std::unordered_map<int, int> remap;
    remap[imported.startStationId] = chainEndId;
    int nextId = maxUsedId + 1;
    auto mapId = [&](int oldId) -> int {
        auto it = remap.find(oldId);
        if (it != remap.end()) return it->second;
        const int newId = nextId++;
        remap[oldId] = newId;
        return newId;
    };

    for (TraverseLeg& leg : imported.legs) leg.stationId = mapId(leg.stationId);
    for (TraverseSideShot& ss : imported.sideShots) {
        ss.atStationId = mapId(ss.atStationId);
        ss.stationId = mapId(ss.stationId);
    }
    for (TraverseGpsVector& gv : imported.gpsVectors) {
        gv.fromStationId = mapId(gv.fromStationId);
        gv.toStationId = mapId(gv.toStationId);
    }

    for (auto& leg : imported.legs) into.legs.push_back(std::move(leg));
    for (auto& ss : imported.sideShots) into.sideShots.push_back(std::move(ss));
    for (auto& gv : imported.gpsVectors) into.gpsVectors.push_back(std::move(gv));
}

// Imports every path in `paths` with `importFn`, merging each into cmd.traverseData
// (see MergeImportedTraverse), recording a `#IMPORT "path"` marker (surfaced in the
// Command tab's text — DrawTraverseCommandTab) and logging per-file success/failure.
template <typename ImportFn>
static void ImportRawFiles(AppCommandState& cmd, std::vector<std::string>& log,
                            const std::vector<std::string>& paths, const char* formatLabel,
                            ImportFn importFn) {
    for (const std::string& path : paths) {
        TraverseData imported;
        std::string err;
        if (importFn(path.c_str(), imported, err)) {
            const size_t importedLegs = imported.legs.size();
            const int markerLegIndex = static_cast<int>(cmd.traverseData.legs.size());
            MergeImportedTraverse(cmd.traverseData, std::move(imported));
            cmd.traverseImportMarkers.emplace_back(markerLegIndex, "IMPORT \"" + path + "\"");
            cmd.traverseDataDirty = true;
            cmd.traverseExpandedLeg = -1;  // selection from a prior traverse is meaningless now.
            log.push_back("TRAVERSE — imported " + std::string(formatLabel) + " (" +
                          std::to_string(importedLegs) + " legs): " + path);
        } else {
            std::string fail = "TRAVERSE — ";
            fail += formatLabel;
            fail += " import failed (";
            fail += path;
            fail += "): ";
            fail += err;
            log.push_back(std::move(fail));
        }
    }
}

// Raw-format import buttons (FBK, STAR*NET, RW5, JobXML) and .gstrav Save/Save
// As/Load — shared by the LSA Editor's left "Files" panel and the (secondary)
// Table View tab, so there is exactly one place this logic lives.
static void DrawTraverseFileButtons(AppCommandState& cmd, std::vector<std::string>& log) {
    TraverseData& td = cmd.traverseData;

    // ---- Raw data import — multi-select: a .godat can hold several field-day raw
    // files merged into one continuous traverse (issue #848 follow-up). ----
    if (ImGui::Button("Import .fbk\xe2\x80\xa6")) {
        std::vector<std::string> paths;
        if (BrowseOpenFilesFbkUtf8(paths))
            ImportRawFiles(cmd, log, paths, "FBK", FbkImport);
    }
    ItemHelpTooltip(
        "Import one or more Autodesk Field Book (.fbk) raw data files.\n"
        "The first import (or, into an empty traverse) replaces it; additional\n"
        "files — picked together or one at a time — append their legs so\n"
        "multiple field days merge into one continuous traverse.");

    // ---- StarNet import (GitHub issue #846, Phase B) ----
    ImGui::SameLine();
    if (ImGui::Button("Import StarNet .dat\xe2\x80\xa6")) {
        std::vector<std::string> paths;
        if (BrowseOpenFilesStarNetUtf8(paths))
            ImportRawFiles(cmd, log, paths, "STAR*NET", StarNetImport);
    }
    ItemHelpTooltip(
        "Import one or more MicroSurvey STAR*NET .dat files (conventional 2D\n"
        "traverse data: a single TB/T/TE traverse, with C lines for reference\n"
        "coordinates). StarNet records this importer doesn't support (GPS\n"
        "vectors, 3D data, general network angles, etc.) are reported as an\n"
        "import failure rather than silently dropped. Multiple files append\n"
        "(see Import .fbk tooltip).");

    // ---- Carlson RW5 import (GitHub issue #846, Phase B) ----
    ImGui::SameLine();
    if (ImGui::Button("Import RW5\xe2\x80\xa6")) {
        std::vector<std::string> paths;
        if (BrowseOpenFilesRw5Utf8(paths))
            ImportRawFiles(cmd, log, paths, "RW5", Rw5Import);
    }
    ItemHelpTooltip(
        "Import one or more Carlson SurvCE/SurvPC raw data files (*.rw5): a\n"
        "sequential OC/BK/TR traverse, with SP lines for known-point\n"
        "coordinates. GNSS vectors and non-traverse records (SS/BD/BR/FD/FR/OF)\n"
        "are either reported as unsupported or ignored by design per the\n"
        "importer's documented scope. Multiple files append (see Import .fbk\n"
        "tooltip).");

    // ---- Trimble JobXML import (GitHub issue #846, Phase B) ----
    ImGui::SameLine();
    if (ImGui::Button("Import JobXML\xe2\x80\xa6")) {
        std::vector<std::string> paths;
        if (BrowseOpenFilesJobXmlUtf8(paths))
            ImportRawFiles(cmd, log, paths, "JobXML", JobXmlImport);
    }
    ItemHelpTooltip(
        "Import one or more Trimble JobXML files (*.jxl/*.xml): a sequential\n"
        "StationRecord/BackBearingRecord/PointRecord traverse, with Reductions\n"
        "points for known-point coordinates. All distances convert from metres\n"
        "to feet. GPS/resection/offset observations are reported as unsupported.\n"
        "Multiple files append (see Import .fbk tooltip).");

    // ---- Save / Load (REQ-403, GitHub issue #834) ----
    ImGui::SameLine();
    const bool haveSavedPath = !cmd.traverseFilePath.empty();
    if (!haveSavedPath) ImGui::BeginDisabled();
    if (ImGui::Button("Save")) {
        std::string err;
        if (traverseio::Save(cmd.traverseFilePath, td, cmd.traverseLsaWeights, err)) {
            TrackTraverseFileInProject(cmd, cmd.traverseFilePath, log);
            log.push_back("TRAVERSE — saved: " + cmd.traverseFilePath);
        } else {
            log.push_back("TRAVERSE — save failed: " + err);
        }
    }
    if (!haveSavedPath) ImGui::EndDisabled();
    ItemHelpTooltip("Save the traverse back to its current .gstrav file.");

    ImGui::SameLine();
    if (ImGui::Button("Save As\xe2\x80\xa6")) {
        char path[1024]{};
        const char* defaultName = haveSavedPath ? cmd.traverseFilePath.c_str() : nullptr;
        if (BrowseSaveFileGstravUtf8(path, sizeof(path), defaultName)) {
            std::string err;
            if (traverseio::Save(path, td, cmd.traverseLsaWeights, err)) {
                cmd.traverseFilePath = path;
                TrackTraverseFileInProject(cmd, cmd.traverseFilePath, log);
                log.push_back("TRAVERSE — saved: " + cmd.traverseFilePath);
            } else {
                log.push_back("TRAVERSE — save failed: " + err);
            }
        }
    }
    ItemHelpTooltip(
        "Save the raw measurements, legs, scale-factor setting, and LSA weights\n"
        "to a .gstrav file. If this drawing belongs to a GoSurvey Project and the\n"
        "file is saved inside the project folder, it is added to the project's\n"
        "tracked files so it travels with Pack Project / Turnover.");

    ImGui::SameLine();
    if (ImGui::Button("Load\xe2\x80\xa6")) {
        char path[1024]{};
        if (BrowseOpenFileGstravUtf8(path, sizeof(path))) {
            TraverseData loaded;
            LsaWeights loadedWeights;
            std::string err;
            if (traverseio::Load(path, loaded, loadedWeights, err)) {
                cmd.traverseData = std::move(loaded);
                cmd.traverseLsaWeights = loadedWeights;
                cmd.traverseFilePath = path;
                cmd.traverseDataDirty = true;
                cmd.traverseLsaComputed = false;
                cmd.traverseLsaAccepted = false;
                cmd.traverseExpandedLeg = -1;  // selection from a prior traverse is meaningless now.
                cmd.traverseImportMarkers.clear();  // a different document — prior import history no longer applies.
                log.push_back(std::string("TRAVERSE — loaded (") +
                              std::to_string(cmd.traverseData.legs.size()) + " legs): " + path);
            } else {
                log.push_back("TRAVERSE — load failed: " + err);
            }
        }
    }
    ItemHelpTooltip("Load a previously saved .gstrav file, replacing the current traverse.");

    // ---- .godat — the command-editor text itself, as its own plain-text file ----
    // Distinct from .gstrav (a structured save of every input field): a .godat file
    // is exactly what the Command/Editor tab's text box holds, byte for byte — open
    // it in any text editor, diff it, generate it from a script.
    ImGui::SameLine();
    const bool haveGodatPath = !cmd.traverseGodatFilePath.empty();
    if (!haveGodatPath) ImGui::BeginDisabled();
    if (ImGui::Button("Save .godat")) {
        std::ofstream f(cmd.traverseGodatFilePath, std::ios::binary | std::ios::trunc);
        if (f) {
            f << cmd.traverseCommandText;
            log.push_back("TRAVERSE — saved .godat: " + cmd.traverseGodatFilePath);
        } else {
            log.push_back("TRAVERSE — .godat save failed: could not open \"" + cmd.traverseGodatFilePath + "\".");
        }
    }
    if (!haveGodatPath) ImGui::EndDisabled();
    ItemHelpTooltip("Save the Command editor's text back to its current .godat file.");

    ImGui::SameLine();
    if (ImGui::Button("Save .godat As\xe2\x80\xa6")) {
        char path[1024]{};
        const char* defaultName = haveGodatPath ? cmd.traverseGodatFilePath.c_str() : nullptr;
        if (BrowseSaveFileGodatUtf8(path, sizeof(path), defaultName)) {
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (f) {
                f << cmd.traverseCommandText;
                cmd.traverseGodatFilePath = path;
                log.push_back(std::string("TRAVERSE — saved .godat: ") + path);
            } else {
                log.push_back(std::string("TRAVERSE — .godat save failed: could not open \"") + path + "\".");
            }
        }
    }
    ItemHelpTooltip("Save the Command editor's text as a new .godat file (the GSTC source,\nplain text — not the structured .gstrav format).");

    ImGui::SameLine();
    if (ImGui::Button("Open .godat\xe2\x80\xa6")) {
        char path[1024]{};
        if (BrowseOpenFileGodatUtf8(path, sizeof(path))) {
            std::ifstream f(path, std::ios::binary);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                cmd.traverseCommandText = ss.str();
                cmd.traverseGodatFilePath = path;
                cmd.traverseImportMarkers.clear();  // a different document — prior import history no longer applies.
                TraverseData parsed;
                LsaWeights parsedWeights;
                std::string err;
                if (travcmd::Parse(cmd.traverseCommandText, parsed, parsedWeights, err)) {
                    cmd.traverseData = std::move(parsed);
                    cmd.traverseLsaWeights = parsedWeights;
                    ComputeTraverse(cmd.traverseData);
                    cmd.traverseLsaComputed = false;
                    cmd.traverseExpandedLeg = -1;
                    cmd.traverseCommandError.clear();
                    log.push_back(std::string("TRAVERSE — opened .godat (") +
                                  std::to_string(cmd.traverseData.legs.size()) + " legs): " + path);
                } else {
                    cmd.traverseCommandError = err;
                    log.push_back("TRAVERSE — .godat opened but failed to parse: " + err);
                }
                cmd.traverseCommandTextLoaded = true;  // the editor shows this text as-is, errors and all.
            } else {
                log.push_back(std::string("TRAVERSE — could not open \"") + path + "\".");
            }
        }
    }
    ItemHelpTooltip("Open a .godat command document, replacing both the Command editor's text\nand the current traverse.");
}

// File/Edit menu bar (replaces the import/save/load button row the left panel
// had no room for — same actions as DrawTraverseFileButtons, as menu items).
static void DrawTraverseMenuBar(AppCommandState& cmd, std::vector<std::string>& log) {
    TraverseData& td = cmd.traverseData;
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Import .fbk\xe2\x80\xa6")) {
            std::vector<std::string> paths;
            if (BrowseOpenFilesFbkUtf8(paths))
                ImportRawFiles(cmd, log, paths, "FBK", FbkImport);
        }
        if (ImGui::MenuItem("Import STAR*NET .dat\xe2\x80\xa6")) {
            std::vector<std::string> paths;
            if (BrowseOpenFilesStarNetUtf8(paths))
                ImportRawFiles(cmd, log, paths, "STAR*NET", StarNetImport);
        }
        if (ImGui::MenuItem("Import RW5\xe2\x80\xa6")) {
            std::vector<std::string> paths;
            if (BrowseOpenFilesRw5Utf8(paths))
                ImportRawFiles(cmd, log, paths, "RW5", Rw5Import);
        }
        if (ImGui::MenuItem("Import JobXML\xe2\x80\xa6")) {
            std::vector<std::string> paths;
            if (BrowseOpenFilesJobXmlUtf8(paths))
                ImportRawFiles(cmd, log, paths, "JobXML", JobXmlImport);
        }

        ImGui::Separator();

        const bool haveSavedPath = !cmd.traverseFilePath.empty();
        if (ImGui::MenuItem("Save .gstrav", nullptr, false, haveSavedPath)) {
            std::string err;
            if (traverseio::Save(cmd.traverseFilePath, td, cmd.traverseLsaWeights, err)) {
                TrackTraverseFileInProject(cmd, cmd.traverseFilePath, log);
                log.push_back("TRAVERSE — saved: " + cmd.traverseFilePath);
            } else {
                log.push_back("TRAVERSE — save failed: " + err);
            }
        }
        if (ImGui::MenuItem("Save .gstrav As\xe2\x80\xa6")) {
            char path[1024]{};
            const char* defaultName = haveSavedPath ? cmd.traverseFilePath.c_str() : nullptr;
            if (BrowseSaveFileGstravUtf8(path, sizeof(path), defaultName)) {
                std::string err;
                if (traverseio::Save(path, td, cmd.traverseLsaWeights, err)) {
                    cmd.traverseFilePath = path;
                    TrackTraverseFileInProject(cmd, cmd.traverseFilePath, log);
                    log.push_back("TRAVERSE — saved: " + cmd.traverseFilePath);
                } else {
                    log.push_back("TRAVERSE — save failed: " + err);
                }
            }
        }
        if (ImGui::MenuItem("Load .gstrav\xe2\x80\xa6")) {
            char path[1024]{};
            if (BrowseOpenFileGstravUtf8(path, sizeof(path))) {
                TraverseData loaded;
                LsaWeights loadedWeights;
                std::string err;
                if (traverseio::Load(path, loaded, loadedWeights, err)) {
                    cmd.traverseData = std::move(loaded);
                    cmd.traverseLsaWeights = loadedWeights;
                    cmd.traverseFilePath = path;
                    cmd.traverseDataDirty = true;
                    cmd.traverseLsaComputed = false;
                    cmd.traverseLsaAccepted = false;
                    cmd.traverseExpandedLeg = -1;
                    cmd.traverseImportMarkers.clear();  // a different document — prior import history no longer applies.
                    log.push_back(std::string("TRAVERSE — loaded (") +
                                  std::to_string(cmd.traverseData.legs.size()) + " legs): " + path);
                } else {
                    log.push_back("TRAVERSE — load failed: " + err);
                }
            }
        }

        ImGui::Separator();

        const bool haveGodatPath = !cmd.traverseGodatFilePath.empty();
        if (ImGui::MenuItem("Open .godat\xe2\x80\xa6")) {
            char path[1024]{};
            if (BrowseOpenFileGodatUtf8(path, sizeof(path))) {
                std::ifstream f(path, std::ios::binary);
                if (f) {
                    std::ostringstream ss;
                    ss << f.rdbuf();
                    cmd.traverseCommandText = ss.str();
                    cmd.traverseGodatFilePath = path;
                    cmd.traverseImportMarkers.clear();  // a different document — prior import history no longer applies.
                    TraverseData parsed;
                    LsaWeights parsedWeights;
                    std::string err;
                    if (travcmd::Parse(cmd.traverseCommandText, parsed, parsedWeights, err)) {
                        cmd.traverseData = std::move(parsed);
                        cmd.traverseLsaWeights = parsedWeights;
                        ComputeTraverse(cmd.traverseData);
                        cmd.traverseLsaComputed = false;
                        cmd.traverseExpandedLeg = -1;
                        cmd.traverseCommandError.clear();
                        log.push_back(std::string("TRAVERSE — opened .godat (") +
                                      std::to_string(cmd.traverseData.legs.size()) + " legs): " + path);
                    } else {
                        cmd.traverseCommandError = err;
                        log.push_back("TRAVERSE — .godat opened but failed to parse: " + err);
                    }
                    cmd.traverseCommandTextLoaded = true;
                } else {
                    log.push_back(std::string("TRAVERSE — could not open \"") + path + "\".");
                }
            }
        }
        if (ImGui::MenuItem("Save .godat", nullptr, false, haveGodatPath)) {
            std::ofstream f(cmd.traverseGodatFilePath, std::ios::binary | std::ios::trunc);
            if (f) {
                f << cmd.traverseCommandText;
                log.push_back("TRAVERSE — saved .godat: " + cmd.traverseGodatFilePath);
            } else {
                log.push_back("TRAVERSE — .godat save failed: could not open \"" + cmd.traverseGodatFilePath + "\".");
            }
        }
        if (ImGui::MenuItem("Save .godat As\xe2\x80\xa6")) {
            char path[1024]{};
            const char* defaultName = haveGodatPath ? cmd.traverseGodatFilePath.c_str() : nullptr;
            if (BrowseSaveFileGodatUtf8(path, sizeof(path), defaultName)) {
                std::ofstream f(path, std::ios::binary | std::ios::trunc);
                if (f) {
                    f << cmd.traverseCommandText;
                    cmd.traverseGodatFilePath = path;
                    log.push_back(std::string("TRAVERSE — saved .godat: ") + path);
                } else {
                    log.push_back(std::string("TRAVERSE — .godat save failed: could not open \"") + path + "\".");
                }
            }
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Clear Traverse")) {
            cmd.traverseData = TraverseData{};
            cmd.traverseFilePath.clear();
            cmd.traverseGodatFilePath.clear();
            cmd.traverseImportMarkers.clear();
            cmd.traverseDataDirty = true;
            cmd.traverseLsaComputed = false;
            cmd.traverseLsaAccepted = false;
            cmd.traverseExpandedLeg = -1;
            log.push_back("TRAVERSE — cleared.");
        }
        ImGui::EndMenu();
    }

    // Every GSTC directive, addable from the UI instead of needing to be typed
    // from memory — a template line (name + its usage placeholders, same text
    // the autocomplete popup and the (?) tooltip already show) is appended to
    // the Command tab's text, which live-applies like any other edit.
    if (ImGui::BeginMenu("Records")) {
        auto insertRecord = [&](const travcmdui::GstcCommandDef& c) {
            std::string& text = cmd.traverseCommandText;
            if (!text.empty() && text.back() != '\n') text += '\n';
            text += std::string(c.name) + " " + c.usage + "\n";
            log.push_back(std::string("TRAVERSE — inserted ") + c.name + " template.");
        };
        if (ImGui::BeginMenu("Header Records")) {
            for (int i = 0; i < travcmdui::kGstcHeaderCommandCount; ++i) {
                const travcmdui::GstcCommandDef& c = travcmdui::kGstcCommands[i];
                if (ImGui::MenuItem(c.name)) insertRecord(c);
                ItemHelpTooltip(c.usage);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Leg Records")) {
            for (int i = travcmdui::kGstcHeaderCommandCount; i < travcmdui::kGstcCommandCount; ++i) {
                const travcmdui::GstcCommandDef& c = travcmdui::kGstcCommands[i];
                if (ImGui::MenuItem(c.name)) insertRecord(c);
                ItemHelpTooltip(c.usage);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

// A toolbar row under the menu bar (STAR*NET-PRO style: a strip of common
// actions as buttons, separate from the File/Edit menus that hold the same
// actions plus everything else). No icon artwork of our own for these yet —
// labeled buttons stand in for it.
static void DrawTraverseToolbar(AppCommandState& cmd, std::vector<std::string>& log) {
    TraverseData& td = cmd.traverseData;
    const bool haveGstrav = !cmd.traverseFilePath.empty();
    if (!haveGstrav) ImGui::BeginDisabled();
    if (ImGui::Button("Save")) {
        std::string err;
        if (traverseio::Save(cmd.traverseFilePath, td, cmd.traverseLsaWeights, err)) {
            TrackTraverseFileInProject(cmd, cmd.traverseFilePath, log);
            log.push_back("TRAVERSE — saved: " + cmd.traverseFilePath);
        } else {
            log.push_back("TRAVERSE — save failed: " + err);
        }
    }
    if (!haveGstrav) ImGui::EndDisabled();
    ItemHelpTooltip("Save .gstrav (File menu has Save As\xe2\x80\xa6/Load\xe2\x80\xa6/the .godat actions too).");

    ImGui::SameLine();
    if (ImGui::Button("Open\xe2\x80\xa6")) {
        char path[1024]{};
        if (BrowseOpenFileGstravUtf8(path, sizeof(path))) {
            TraverseData loaded;
            LsaWeights loadedWeights;
            std::string err;
            if (traverseio::Load(path, loaded, loadedWeights, err)) {
                cmd.traverseData = std::move(loaded);
                cmd.traverseLsaWeights = loadedWeights;
                cmd.traverseFilePath = path;
                cmd.traverseDataDirty = true;
                cmd.traverseLsaComputed = false;
                cmd.traverseLsaAccepted = false;
                cmd.traverseExpandedLeg = -1;
                cmd.traverseImportMarkers.clear();  // a different document — prior import history no longer applies.
                log.push_back(std::string("TRAVERSE — loaded (") +
                              std::to_string(cmd.traverseData.legs.size()) + " legs): " + path);
            } else {
                log.push_back("TRAVERSE — load failed: " + err);
            }
        }
    }
    ItemHelpTooltip("Open a .gstrav file.");

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    if (ImGui::Button("Run")) {
        cmd.traverseLsaComputed = false;  // forces EnsureLsaComputed to redo the adjustment this frame.
        log.push_back("TRAVERSE — re-ran the least-squares adjustment.");
    }
    ItemHelpTooltip("Re-run the least-squares adjustment (it also reruns automatically whenever\nthe command text or traverse data changes).");
}

// First newline that begins a measurement line, or npos when the text is only a header.
// The index points at the '\n' so the caller can insert the following line.
static size_t FindFirstMeasurementLine(const std::string& text) {
    static const char* kWords[] = {
        "STN ", "CTRL ", "ANGLE ", "DIST ", "AZIMUTH ", "EDIF ", "ZENITH ", "DIR ",
        "HANG ", "HDIST ", "SDIST ", "VANG ", "DESC ", "PRISM ", "POLE ", "TSERR ",
    };
    size_t best = std::string::npos;
    for (const char* word : kWords) {
        const std::string needle = std::string("\n") + word;
        const size_t at = text.find(needle);
        if (at != std::string::npos && (best == std::string::npos || at < best)) best = at;
    }
    return best;
}

// Splices `#IMPORT "path"` lines into freshly Serialize()'d GSTC text, immediately
// before the measurements that import contributed. A chain import is marked in
// front of its STN block. A network import has no STN lines, so the marker goes
// in front of the first CTRL/DIST/DIR line instead of after the last one.
// travcmd::Parse treats `#IMPORT` as a note. Serialize has no import history, so
// the note is spliced in here. Markers are applied highest legIndex first so
// earlier insertions don't shift the position of later ones.
static void InsertImportMarkerComments(std::string& text,
                                        const std::vector<std::pair<int, std::string>>& markers) {
    if (markers.empty()) return;
    std::vector<std::pair<int, std::string>> sorted = markers;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [legIndex, label] : sorted) {
        size_t pos = 0;
        int count = 0;
        size_t foundPos = std::string::npos;
        for (;;) {
            const size_t p = text.find("\nSTN ", pos);
            if (p == std::string::npos) break;
            if (count == legIndex) { foundPos = p; break; }
            ++count;
            pos = p + 1;
        }
        if (foundPos == std::string::npos) foundPos = FindFirstMeasurementLine(text);
        const std::string marker = "#" + label + "\n";
        if (foundPos != std::string::npos)
            text.insert(foundPos + 1, marker);
        else if (!text.empty() && text.back() != '\n')
            text += "\n" + marker;
        else
            text += marker;
    }
}

// A GoSurvey-flavor (travcmd::) text editor for the same TraverseData the Form
// tab edits. The Command tab is the primary editing surface: it live-applies
// on every change (no Apply/Revert step) and fills the rest of the window.
// Text is only ever regenerated FROM the traverse (travcmd::Serialize) when the
// data changed from outside this tab — the Form tab or an import — via the
// existing traverseCommandTextLoaded invalidation; an edit that originates
// here never gets reformatted out from under the user's cursor.
static void DrawTraverseCommandTab(AppCommandState& cmd, std::vector<std::string>& log) {
    TraverseData& td = cmd.traverseData;
    (void)log;  // imports/Form edits still log at their own call sites; live edits don't spam the log.

    if (!cmd.traverseCommandTextLoaded) {
        cmd.traverseCommandText = travcmd::AlignColumns(travcmd::Serialize(td, cmd.traverseLsaWeights));
        InsertImportMarkerComments(cmd.traverseCommandText, cmd.traverseImportMarkers);
        cmd.traverseCommandTextLoaded = true;
        cmd.traverseCommandError.clear();
        // The text box keeps its own copy while it is focused. Tell it to pick up
        // this replacement, or the next keystroke writes the old header back over
        // the imported commands.
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(ImGui::GetID("##trv_cmd_text"))) {
            state->WantReloadUserBuf = true;
            state->ReloadSelectionStart = 0;
            state->ReloadSelectionEnd = 0;
        }
    }

    ImGui::TextDisabled("GoSurvey Traverse Command (GSTC)");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "GoSurvey's own traverse command language (not StarNet's; no \"key=value\", "
            "just plain words and numbers). Header: STATION, BEARING, LOOP, SCALE, "
            "ERRORS. Per leg: STN <at> <to>, then HANG <from> <at> <to> <angle>, "
            "HDIST/SDIST <at> <to> <dist>, VANG <at> <to> <angle> [ZENITH|ELEV], "
            "DESC <at> <to> <text>, PRISM <at> <to> <constant> <error>, POLE <at> <to> "
            "<error>, TSERR <at> <to> <angle-sec> <dist-const> <dist-ppm>.\n"
            "Start typing a command for suggestions (Tab to accept). Full reference: Help (F1).");

    // Split into display lines once per frame (cheap — these buffers are tiny).
    std::vector<std::string> lines;
    {
        const std::string& text = cmd.traverseCommandText;
        if (text.empty()) {
            lines.emplace_back();
        } else {
            size_t start = 0;
            for (size_t p = 0; p < text.size(); ++p) {
                if (text[p] != '\n') continue;
                lines.push_back(text.substr(start, p - start));
                start = p + 1;
            }
            // A trailing newline ends the last line; it is not an extra blank row.
            if (start < text.size()) lines.push_back(text.substr(start));
            if (lines.empty()) lines.emplace_back();
        }
    }

    static float s_scrollY = 0.0f;
    static float s_scrollX = 0.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    const int maxDigits = static_cast<int>(std::to_string(lines.size()).size());
    const float gutterW = ImGui::CalcTextSize(std::string(static_cast<size_t>(maxDigits), '9').c_str()).x +
                           style.FramePadding.x * 2.0f;

    // Fill the rest of the panel — no Apply/Revert bar below, just a thin status/error
    // line, so this (the primary editing surface) gets essentially the whole window.
    const float statusH = ImGui::GetFrameHeightWithSpacing();
    const float boxH = std::max(80.f, ImGui::GetContentRegionAvail().y - statusH);

    const ImGuiID textId = ImGui::GetID("##trv_cmd_text");

    // ---- Autocomplete: the leading token of the line at the cursor, read from last
    // frame's persisted cursor position (one-frame-behind — the same idiom the main
    // CAD command bar's own suggestion popup uses, CadUi.cpp) ----
    std::string partialToken;
    int tokenLineStart = -1, tokenEnd = -1;
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(textId)) {
        const std::string& text = cmd.traverseCommandText;
        const int n = static_cast<int>(text.size());
        const int cursor = std::clamp(state->GetCursorPos(), 0, n);
        int lineStart = cursor;
        while (lineStart > 0 && text[static_cast<size_t>(lineStart - 1)] != '\n') --lineStart;
        int te = lineStart;
        while (te < n && text[static_cast<size_t>(te)] != ' ' && text[static_cast<size_t>(te)] != '\t' &&
               text[static_cast<size_t>(te)] != '\n')
            ++te;
        if (cursor <= te) {
            tokenLineStart = lineStart;
            tokenEnd = te;
            partialToken = text.substr(static_cast<size_t>(lineStart), static_cast<size_t>(te - lineStart));
        }
    }
    std::string partialUpper = partialToken;
    for (char& ch : partialUpper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const std::vector<int> matches =
        partialToken.empty() ? std::vector<int>() : travcmdui::FuzzyGstcMatches(partialUpper, 8);

    // Prime Tab-completion with the top match; cleared again right after the real
    // InputTextMultiline call below (Tab, if pressed, already fired synchronously
    // inside that call) so a later, unrelated Tab press can't replay it.
    travcmdui::GstcCompletionState& compState = travcmdui::CompletionState();
    compState.pending = !matches.empty();
    compState.word = matches.empty() ? std::string() : std::string(travcmdui::kGstcCommands[matches[0]].name);

    // Consolas for the gutter, the real (invisible) text, and the colorized overlay
    // alike — all three must share identical font metrics, since the overlay and
    // gutter are positioned by assuming they use the exact same line pitch as the
    // real InputTextMultiline underneath.
    ImFont* monoFont = FontReg::Resolve("Courier New", false, false);
    ImGui::PushFont(monoFont);

    // The text box is drawn first so its scrollbar position is known before the
    // gutter and the colorized overlay are drawn. Those two are separate windows
    // and used to copy scroll only while the box was focused, so dragging the
    // scrollbar (or rolling the wheel) left the line numbers and text behind
    // until the next click.
    const ImVec2 editorOrigin = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(editorOrigin.x + gutterW + 2.0f, editorOrigin.y));

    // ---- The real, editable box (glyphs hidden; caret/selection stay visible) ----
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    ImGui::InputTextMultiline("##trv_cmd_text", &cmd.traverseCommandText, ImVec2(-FLT_MIN, boxH),
                              ImGuiInputTextFlags_CallbackCompletion, travcmdui::GstcTextEditCallback);
    ImGui::PopStyleColor();
    const ImVec2 textMin = ImGui::GetItemRectMin();
    const ImVec2 textMax = ImGui::GetItemRectMax();
    const bool textBoxActive = ImGui::IsItemActive();
    compState.pending = false;

    {
        const ImVector<ImGuiWindow*>& children = ImGui::GetCurrentWindow()->DC.ChildWindows;
        if (!children.empty()) {
            const ImGuiWindow* textWindow = children.back();
            s_scrollY = textWindow->Scroll.y;
            s_scrollX = textWindow->Scroll.x;
        }
    }

    // ImGui's native caret is drawn in ImGuiCol_Text, which is pushed fully
    // transparent above (so the real glyphs stay invisible under the colorized
    // overlay) — that hid the caret too. It's redrawn by hand in the overlay
    // below, from this frame's live cursor position (no one-frame-behind delay
    // needed here — unlike the autocomplete token above, this isn't deciding
    // what to splice into next frame's buffer, just where to draw a line now).
    int caretRow = -1;
    float caretColPx = 0.0f;
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(textId)) {
        if (textBoxActive) s_scrollX = state->Scroll.x;
        if (textBoxActive) {
            const std::string& text = cmd.traverseCommandText;
            const int n = static_cast<int>(text.size());
            const int cur = std::clamp(state->GetCursorPos(), 0, n);
            int lineStartPos = cur;
            while (lineStartPos > 0 && text[static_cast<size_t>(lineStartPos - 1)] != '\n') --lineStartPos;
            caretRow = static_cast<int>(std::count(text.begin(), text.begin() + lineStartPos, '\n'));
            const std::string prefix =
                text.substr(static_cast<size_t>(lineStartPos), static_cast<size_t>(cur - lineStartPos));
            caretColPx = ImGui::CalcTextSize(prefix.c_str()).x;
        }
    }

    // ---- Line-number gutter, scrolled with the text box even when it is not focused ----
    ImGui::SetCursorScreenPos(editorOrigin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_FrameBg]);
    if (ImGui::BeginChild("##trv_cmd_gutter", ImVec2(gutterW, boxH), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::SetScrollY(s_scrollY);
        const ImVec4 gutterCol = style.Colors[ImGuiCol_TextDisabled];
        for (size_t i = 0; i < lines.size(); ++i)
            ImGui::TextColored(gutterCol, "%*d", maxDigits, static_cast<int>(i + 1));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // ---- Inline parse-error annotation: travcmd::Parse's error is always
    // "line N: message" (TraverseCommandLang.cpp) — split that apart so the line
    // number only has to steer a highlight drawn ON that line, not be read and
    // hunted for by the user.
    int errLineIdx = -1;
    std::string errMsg;
    if (!cmd.traverseCommandError.empty()) {
        const std::string& e = cmd.traverseCommandError;
        if (e.rfind("line ", 0) == 0) {
            const size_t colonPos = e.find(':');
            if (colonPos != std::string::npos) {
                errLineIdx = std::atoi(e.c_str() + 5) - 1;
                size_t msgStart = colonPos + 1;
                while (msgStart < e.size() && e[msgStart] == ' ') ++msgStart;
                errMsg = e.substr(msgStart);
            }
        }
        if (errMsg.empty()) errMsg = e;
    }

    // ---- Colorized overlay, drawn on top, input-transparent ----
    ImGui::SetCursorScreenPos(textMin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    if (ImGui::BeginChild("##trv_cmd_overlay", ImVec2(textMax.x - textMin.x, textMax.y - textMin.y),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                              ImGuiWindowFlags_NoInputs)) {
        ImGui::SetScrollY(s_scrollY);
        ImGui::SetScrollX(s_scrollX);
        const ImVec2 overlayOrigin = ImGui::GetCursorScreenPos();
        const float lineH = ImGui::GetTextLineHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        for (size_t li = 0; li < lines.size(); ++li) {
            const ImVec2 lineTop = ImGui::GetCursorScreenPos();
            travcmdui::DrawHighlightedLine(lines[li]);
            if (static_cast<int>(li) == errLineIdx) {
                const float textW = std::max(4.0f, ImGui::CalcTextSize(lines[li].c_str()).x);
                const ImU32 errCol = IM_COL32(214, 60, 50, 255);
                const float underlineY = lineTop.y + lineH - 2.0f;
                dl->AddLine(ImVec2(lineTop.x, underlineY), ImVec2(lineTop.x + textW, underlineY), errCol, 1.5f);
                dl->AddText(ImVec2(lineTop.x, lineTop.y + lineH + 1.0f), errCol, errMsg.c_str());
            }
        }

        if (caretRow >= 0) {
            const ImVec2 caretTop(overlayOrigin.x + caretColPx, overlayOrigin.y + static_cast<float>(caretRow) * lineH);
            const ImVec2 caretBot(caretTop.x, caretTop.y + lineH);
            if (std::fmod(static_cast<float>(ImGui::GetTime()), 1.0f) < 0.5f)
                dl->AddLine(caretTop, caretBot, IM_COL32(15, 15, 15, 255), 1.5f);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::EndGroup();
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(editorOrigin.x, textMax.y + style.ItemSpacing.y));

    // ---- Autocomplete popup: "COMMAND   usage", filtered as you type ----
    if (textBoxActive && !matches.empty() && tokenLineStart >= 0) {
        const std::string& text = cmd.traverseCommandText;
        const int lineIndex =
            static_cast<int>(std::count(text.begin(), text.begin() + tokenLineStart, '\n'));
        const float lineH = ImGui::GetTextLineHeight();
        const float rowH = ImGui::GetFrameHeight();

        float contentW = 0.f;
        for (int idx : matches) {
            const travcmdui::GstcCommandDef& c = travcmdui::kGstcCommands[idx];
            const float w = ImGui::CalcTextSize(c.name).x + ImGui::CalcTextSize("   ").x +
                             ImGui::CalcTextSize(c.usage).x;
            contentW = std::max(contentW, w);
        }
        const float listW = std::clamp(contentW + 24.f, 160.f, 520.f);
        const float listH = rowH * static_cast<float>(matches.size()) + 6.f;

        ImVec2 pos(textMin.x, textMin.y + static_cast<float>(lineIndex + 1) * lineH - s_scrollY + 2.0f);
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 wmax(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
        if (pos.x + listW > wmax.x) pos.x = wmax.x - listW;
        if (pos.y + listH > wmax.y)
            pos.y = textMin.y + static_cast<float>(lineIndex) * lineH - s_scrollY - listH - 2.0f;
        pos.x = std::max(pos.x, vp->WorkPos.x);
        pos.y = std::max(pos.y, vp->WorkPos.y);

        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(listW, listH));
        const ImGuiWindowFlags pf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 3.f));
        if (ImGui::Begin("##trv_cmd_autocomplete", nullptr, pf)) {
            for (int idx : matches) {
                const travcmdui::GstcCommandDef& c = travcmdui::kGstcCommands[idx];
                ImGui::PushID(idx);
                if (ImGui::Selectable("##row", false, 0, ImVec2(0, rowH))) {
                    cmd.traverseCommandText.replace(static_cast<size_t>(tokenLineStart),
                                                    static_cast<size_t>(tokenEnd - tokenLineStart),
                                                    std::string(c.name) + " ");
                }
                ImGui::SameLine(6.f);
                ImGui::TextColored(ImVec4(0.40f, 0.68f, 0.98f, 1.0f), "%s", c.name);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", c.usage);
                ImGui::PopID();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ---- Status / error line (replaces the old Apply/Revert buttons — this tab
    // applies live). The error itself is now underlined in place on its own line
    // above (errMsg/errLineIdx) — this is just a short status, not a repeat of
    // "line N:" the user has to go hunt for.
    if (!cmd.traverseCommandError.empty())
        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), "Parse error \xe2\x80\x94 see the highlighted line above.");
    else
        ImGui::TextDisabled("Edits apply live — the Form tab reflects this traverse too.");

    // Snap columns once, when the user leaves the box. Doing this every frame
    // rewrote the buffer continuously (AlignColumns used to append a blank line
    // on each call) and the live-apply below then parsed that stale text over
    // a just-imported network.
    static bool s_textBoxWasActive = false;
    if (s_textBoxWasActive && !textBoxActive && cmd.traverseCommandError.empty() &&
        !cmd.traverseDataDirty) {
        const std::string aligned = travcmd::AlignColumns(cmd.traverseCommandText);
        if (aligned != cmd.traverseCommandText) cmd.traverseCommandText = aligned;
    }
    s_textBoxWasActive = textBoxActive;

    // ---- Live-apply: reparse whenever the text actually changed, from any source
    // (typing, a clicked autocomplete suggestion). Skip while an import or Form
    // edit is still dirty: this frame's text is the previous document, and
    // parsing it would wipe the new measurements before the next frame
    // regenerates the command text from them.
    static std::string s_lastAppliedText;
    if (!cmd.traverseDataDirty && cmd.traverseCommandText != s_lastAppliedText) {
        s_lastAppliedText = cmd.traverseCommandText;
        TraverseData parsed;
        LsaWeights parsedWeights;
        std::string err;
        if (travcmd::Parse(cmd.traverseCommandText, parsed, parsedWeights, err)) {
            cmd.traverseData = std::move(parsed);
            cmd.traverseLsaWeights = parsedWeights;
            ComputeTraverse(cmd.traverseData);
            cmd.traverseLsaComputed = false;
            cmd.traverseExpandedLeg = -1;
            cmd.traverseCommandError.clear();
        } else {
            cmd.traverseCommandError = err;
        }
    }
}

void EnsureLsaComputed(AppCommandState& cmd);  // defined below; DrawLsaStationPlot needs it first.

// ---- Left-panel "Preview": a small plan plot of the computed traverse — points,
// the legs between them, and each station's 1-sigma error ellipse once the least
// squares adjustment has run (replaces a plain easting/northing/elev table, which
// told you numbers but not the shape of the network or where it's weakest). ----
void DrawLsaStationPlot(AppCommandState& cmd) {
    TraverseData& td = cmd.traverseData;
    EnsureLsaComputed(cmd);
    const LsaResult& r = cmd.traverseLsaResult;

    struct PlotPt {
        int id = 0;
        double e = 0.0, n = 0.0;
        bool hasEllipse = false;
        double semiMajorFt = 0.0, semiMinorFt = 0.0, azimuthDeg = 0.0;
    };
    auto findAdjusted = [&](int id, double& e, double& n) -> bool {
        if (!r.ok) return false;
        for (size_t i = 0; i < r.stationIds.size(); ++i)
            if (r.stationIds[i] == id) { e = r.adjEasting[i]; n = r.adjNorthing[i]; return true; }
        return false;
    };
    auto attachEllipse = [&](PlotPt& p) {
        if (!r.ok) return;
        for (size_t i = 0; i < r.stationIds.size(); ++i) {
            if (r.stationIds[i] != p.id) continue;
            p.hasEllipse = true;
            p.semiMajorFt = r.stationSemiMajorFt[i];
            p.semiMinorFt = r.stationSemiMinorFt[i];
            p.azimuthDeg = r.stationEllipseAzimuthDeg[i];
            return;
        }
    };

    std::vector<PlotPt> pts;
    {
        PlotPt p;
        p.id = td.startStationId;
        if (!findAdjusted(p.id, p.e, p.n)) { p.e = td.startEasting; p.n = td.startNorthing; }
        attachEllipse(p);
        pts.push_back(p);
    }
    for (const TraverseLeg& leg : td.legs) {
        PlotPt p;
        p.id = leg.stationId;
        if (!findAdjusted(p.id, p.e, p.n)) {
            if (!leg.computed) continue;  // unresolved leg — nothing to plot yet.
            p.e = leg.computedEasting;
            p.n = leg.computedNorthing;
        }
        attachEllipse(p);
        pts.push_back(p);
    }

    if (pts.size() < 2) {
        ImGui::TextDisabled("Not enough computed stations yet.");
        return;
    }

    // Side/check shots (issue #853) — the traverse's radial detail/control
    // points, drawn thin and un-adjusted (least squares only ever touches the
    // loop, never these) so they don't read as part of the backbone chain.
    struct SideShotPt {
        int atStationId = 0;
        double e = 0.0, n = 0.0;
        bool isCheck = false;
        std::string label;
    };
    std::vector<SideShotPt> sidePts;
    for (const TraverseSideShot& ss : td.sideShots) {
        if (!ss.computed) continue;  // occupied station unresolved, or missing data — nothing to plot.
        SideShotPt sp;
        sp.atStationId = ss.atStationId;
        sp.e = ss.computedEasting;
        sp.n = ss.computedNorthing;
        sp.isCheck = ss.isCheckShot;
        sp.label = !ss.description.empty() ? ss.description : std::to_string(ss.stationId);
        sidePts.push_back(std::move(sp));
    }

    double minE = pts[0].e, maxE = pts[0].e, minN = pts[0].n, maxN = pts[0].n;
    for (const PlotPt& p : pts) {
        minE = std::min(minE, p.e); maxE = std::max(maxE, p.e);
        minN = std::min(minN, p.n); maxN = std::max(maxN, p.n);
    }
    for (const PlotPt& p : pts) {
        if (!p.hasEllipse) continue;
        minE = std::min(minE, p.e - p.semiMajorFt); maxE = std::max(maxE, p.e + p.semiMajorFt);
        minN = std::min(minN, p.n - p.semiMajorFt); maxN = std::max(maxN, p.n + p.semiMajorFt);
    }
    for (const SideShotPt& sp : sidePts) {
        minE = std::min(minE, sp.e); maxE = std::max(maxE, sp.e);
        minN = std::min(minN, sp.n); maxN = std::max(maxN, sp.n);
    }
    const double spanE = std::max(1e-6, maxE - minE), spanN = std::max(1e-6, maxN - minN);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float pad = 22.0f;
    const float plotW = std::max(10.0f, avail.x - 2.0f * pad);
    const float plotH = std::max(10.0f, avail.y - 2.0f * pad);

    // Persistent pan/zoom (world-space center + pixels-per-foot), navigated the
    // same way as the main 3D viewport: scroll to zoom (about the cursor),
    // middle-mouse-drag to pan, double-middle-click to zoom extents. Auto-fit
    // the first time there's something to show, and again whenever the data's
    // bounds jump somewhere the current view doesn't even overlap — the signature
    // of a coordinate-system change (e.g. the start station's E/N were just
    // edited) rather than the traverse just growing/shrinking incrementally,
    // which should NOT fight the user's own framing.
    static bool s_plotInit = false;
    static double s_centerE = 0.0, s_centerN = 0.0, s_scale = 1.0;
    static bool s_havePrevBounds = false;
    static double s_prevMinE = 0.0, s_prevMaxE = 0.0, s_prevMinN = 0.0, s_prevMaxN = 0.0;

    auto zoomExtents = [&]() {
        s_centerE = 0.5 * (minE + maxE);
        s_centerN = 0.5 * (minN + maxN);
        s_scale = std::min(plotW / spanE, plotH / spanN);
    };

    const bool boundsDisjoint = s_havePrevBounds &&
        (maxE < s_prevMinE || minE > s_prevMaxE || maxN < s_prevMinN || minN > s_prevMaxN);
    if (!s_plotInit || boundsDisjoint) {
        zoomExtents();
        s_plotInit = true;
    }
    s_prevMinE = minE; s_prevMaxE = maxE; s_prevMinN = minN; s_prevMaxN = maxN;
    s_havePrevBounds = true;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + avail.x * 0.5f, origin.y + avail.y * 0.5f);
    auto toScreen = [&](double e, double n) -> ImVec2 {
        return ImVec2(center.x + static_cast<float>((e - s_centerE) * s_scale),
                      center.y - static_cast<float>((n - s_centerN) * s_scale));  // north is up.
    };

    ImGui::InvisibleButton("##lsa_plot_canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const double worldE = s_centerE + (mouse.x - center.x) / s_scale;
            const double worldN = s_centerN - (mouse.y - center.y) / s_scale;
            s_scale = std::clamp(s_scale * std::pow(1.1, wheel), 1e-6, 1e9);
            s_centerE = worldE - (mouse.x - center.x) / s_scale;
            s_centerN = worldN + (mouse.y - center.y) / s_scale;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
            zoomExtents();
    }
    const bool panning = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f);
    if (panning) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        s_centerE -= d.x / s_scale;
        s_centerN += d.y / s_scale;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 legCol = IM_COL32(70, 100, 180, 255);
    const ImU32 ptCol = IM_COL32(30, 30, 30, 255);
    const ImU32 ellipseCol = IM_COL32(210, 70, 40, 200);
    constexpr double kDeg2RadLocal = 3.14159265358979323846 / 180.0;

    for (size_t i = 1; i < pts.size(); ++i)
        dl->AddLine(toScreen(pts[i - 1].e, pts[i - 1].n), toScreen(pts[i].e, pts[i].n), legCol, 1.5f);
    if (td.isClosedLoop && pts.size() > 2)
        dl->AddLine(toScreen(pts.back().e, pts.back().n), toScreen(pts.front().e, pts.front().n),
                    IM_COL32(70, 100, 180, 120), 1.0f);

    // Side/check shots: a thin radial line from the occupied backbone station
    // to each detail/control point, plus its own small marker + label —
    // distinguished by color from the backbone legs so the chain still reads
    // clearly even on a station with a dozen side shots off it.
    {
        std::unordered_map<int, ImVec2> backbonePx;
        for (const PlotPt& p : pts) backbonePx[p.id] = toScreen(p.e, p.n);
        const ImU32 sideCol = IM_COL32(140, 140, 145, 200);
        const ImU32 checkCol = IM_COL32(60, 150, 90, 220);
        const ImU32 sidePtCol = IM_COL32(90, 90, 95, 255);
        const ImU32 checkPtCol = IM_COL32(30, 120, 60, 255);
        for (const SideShotPt& sp : sidePts) {
            const auto atIt = backbonePx.find(sp.atStationId);
            const ImVec2 c = toScreen(sp.e, sp.n);
            if (atIt != backbonePx.end())
                dl->AddLine(atIt->second, c, sp.isCheck ? checkCol : sideCol, 1.0f);
            dl->AddCircleFilled(c, 2.5f, sp.isCheck ? checkPtCol : sidePtCol);
            dl->AddText(ImVec2(c.x + 5.0f, c.y - 7.0f), sp.isCheck ? checkPtCol : sidePtCol, sp.label.c_str());
        }
    }

    // Hover target: nearest station within a small pixel radius, OR anywhere
    // inside its own ellipse (ellipses can be much bigger than the point marker,
    // and that's exactly the part someone wants to put a number to).
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const PlotPt* hoverPt = nullptr;
    float hoverBestD2 = 1e30f;
    if (hovered && !panning) {
        for (const PlotPt& p : pts) {
            const ImVec2 c = toScreen(p.e, p.n);
            const float dx = mouse.x - c.x, dy = mouse.y - c.y;
            const float d2 = dx * dx + dy * dy;
            bool inEllipse = false;
            if (p.hasEllipse && (p.semiMajorFt > 1e-9 || p.semiMinorFt > 1e-9)) {
                const float rMaj = static_cast<float>(p.semiMajorFt * s_scale);
                const float rMin = static_cast<float>(p.semiMinorFt * s_scale);
                const float rot = static_cast<float>(p.azimuthDeg * kDeg2RadLocal);
                // Rotate the cursor offset into the ellipse's own (major,minor) axes.
                const float u = dx * std::sin(rot) + dy * std::cos(rot);
                const float v = dx * std::cos(rot) - dy * std::sin(rot);
                if (rMaj > 1e-6f && rMin > 1e-6f)
                    inEllipse = (u * u) / (rMaj * rMaj) + (v * v) / (rMin * rMin) <= 1.0f;
            }
            if (d2 <= 64.0f || inEllipse) {
                if (d2 < hoverBestD2) { hoverBestD2 = d2; hoverPt = &p; }
            }
        }
    }

    for (const PlotPt& p : pts) {
        const ImVec2 c = toScreen(p.e, p.n);
        const bool isHovered = (&p == hoverPt);
        if (p.hasEllipse && (p.semiMajorFt > 1e-9 || p.semiMinorFt > 1e-9)) {
            const float rMaj = static_cast<float>(p.semiMajorFt * s_scale);
            const float rMin = static_cast<float>(p.semiMinorFt * s_scale);
            const float rot = static_cast<float>(p.azimuthDeg * kDeg2RadLocal);
            const int segs = 32;
            ImVec2 poly[33];
            for (int s = 0; s <= segs; ++s) {
                const float t = (2.0f * 3.14159265f) * static_cast<float>(s) / static_cast<float>(segs);
                const float lx = rMaj * std::cos(t), ly = rMin * std::sin(t);
                const float dE = lx * std::sin(rot) + ly * std::cos(rot);
                const float dN = lx * std::cos(rot) - ly * std::sin(rot);
                poly[s] = ImVec2(c.x + dE, c.y - dN);
            }
            dl->AddPolyline(poly, segs + 1, ellipseCol, ImDrawFlags_None, isHovered ? 2.5f : 1.25f);
            if (isHovered) {
                // Major-axis tick, so the number below has something to point at.
                const ImVec2 axisEnd(c.x + rMaj * std::sin(rot), c.y - rMaj * std::cos(rot));
                dl->AddLine(c, axisEnd, ellipseCol, 1.0f);
            }
        }
        dl->AddCircleFilled(c, isHovered ? 4.0f : 3.0f, ptCol);
        char label[16];
        std::snprintf(label, sizeof(label), "%d", p.id);
        dl->AddText(ImVec2(c.x + 5.0f, c.y - 7.0f), ptCol, label);
    }

    if (hoverPt != nullptr) {
        if (hoverPt->hasEllipse && (hoverPt->semiMajorFt > 1e-9 || hoverPt->semiMinorFt > 1e-9)) {
            ImGui::SetTooltip("Station %d \xe2\x80\x94 1-sigma error ellipse\nSemi-major: %.4f ft\nSemi-minor: %.4f ft\nAzimuth: %.1f\xc2\xb0",
                              hoverPt->id, hoverPt->semiMajorFt, hoverPt->semiMinorFt, hoverPt->azimuthDeg);
        } else {
            ImGui::SetTooltip("Station %d \xe2\x80\x94 no error ellipse (run the least-squares adjustment).",
                              hoverPt->id);
        }
    }
}

// Lazily (re)computes cmd.traverseLsaResult the first time it's needed this
// session — the same guard DrawTraverseClosureWindow's own "recompute" lambda
// uses, duplicated here (rather than shared) since it's a one-line cache check.
void EnsureLsaComputed(AppCommandState& cmd) {
    if (!cmd.traverseLsaComputed) {
        cmd.traverseLsaResult = ComputeTraverseLeastSquares(cmd.traverseData, cmd.traverseLsaWeights);
        cmd.traverseLsaComputed = true;
    }
}

// ---- LSA Editor tab: files/summary/preview (left) | command editor (center,
// primary) | output report (right) — a StarNet-PRO-style three-pane layout. ----
// A thin draggable handle between two panels: click-drag moves *width by the
// mouse's horizontal delta (clamped), and the cursor hints resizability on
// hover even before a drag starts. ImGui has no built-in splitter widget, so
// this is the standard manual pattern (an InvisibleButton read as a drag).
// \p growsRight: true when dragging the handle rightward should grow *width
// (the splitter sits on the panel's LEFT edge, e.g. the center/right divider,
// where pulling left gives the right panel more room); false when dragging
// leftward grows it (the splitter sits on the panel's RIGHT edge, e.g. the
// left/center divider).
static void DrawVerticalSplitter(const char* strId, float* width, float minW, float maxW, float height,
                                 bool growsRight) {
    ImGui::SameLine(0.f, 0.f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));  // invisible until hovered/dragged.
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_SeparatorHovered]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_SeparatorActive]);
    ImGui::Button(strId, ImVec2(6.f, height));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) {
        const float dx = ImGui::GetIO().MouseDelta.x;
        *width = std::clamp(*width + (growsRight ? dx : -dx), minW, maxW);
    }
    ImGui::SameLine(0.f, 0.f);
}

// Same idea as DrawVerticalSplitter, rotated 90°: a draggable handle between two
// stacked sections (the LSA Editor left panel's Files/Summary/Preview), dragging
// vertically to resize the section above it. Always grows downward (dragging the
// handle down gives the section above it more height) — unlike the left/right
// splitter there's no "panel on the far side" ambiguity to resolve.
static void DrawHorizontalSplitter(const char* strId, float* height, float minH, float maxH, float width) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyle().Colors[ImGuiCol_SeparatorHovered]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyle().Colors[ImGuiCol_SeparatorActive]);
    ImGui::Button(strId, ImVec2(width, 6.f));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) {
        const float dy = ImGui::GetIO().MouseDelta.y;
        *height = std::clamp(*height + dy, minH, maxH);
    }
}

// GoSurvey's own "Hazel" neutral ladder (CadUi.cpp's dark theme — see its
// `seam/ground/titlebar/surface/raised/hover` comments) reused verbatim by
// hex value here, since those locals are file-static in CadUi.cpp and not
// exported. Darkest to lightest: seam < field < ground < titlebar < surface
// < raised < hover. Reusing the app's real steps (rather than inventing a
// separate near-black palette) is what makes the LSA Editor's chrome actually
// match the rest of GoSurvey instead of just looking like a different app.
// The LSA Editor runs one single, cohesive light theme across the WHOLE
// window — menu bar, toolbar, tab strip, both side panels, the editor, and
// the report — matching STAR*NET-PRO's own look (its entire app is one light
// Windows-classic palette, not a mix of dark chrome around light content).
// Pushed before Begin() so it also covers colors Begin() itself samples
// immediately (WindowBg, TitleBg, Border), and popped after every exit from
// DrawTraverseEditorPanel. Returns the number of colors pushed.
int PushLsaLightTheme() {
    struct C { ImGuiCol idx{}; ImVec4 col; };
    static const C kColors[] = {
        {ImGuiCol_Text,                 ImVec4(0.09f, 0.09f, 0.10f, 1.00f)},
        {ImGuiCol_TextDisabled,         ImVec4(0.50f, 0.50f, 0.52f, 1.00f)},
        {ImGuiCol_WindowBg,             ImVec4(0.94f, 0.94f, 0.95f, 1.00f)},
        {ImGuiCol_ChildBg,              ImVec4(0.97f, 0.97f, 0.98f, 1.00f)},
        {ImGuiCol_PopupBg,              ImVec4(0.98f, 0.98f, 0.99f, 1.00f)},
        {ImGuiCol_Border,               ImVec4(0.78f, 0.78f, 0.80f, 1.00f)},
        {ImGuiCol_FrameBg,              ImVec4(1.00f, 1.00f, 1.00f, 1.00f)},
        {ImGuiCol_FrameBgHovered,       ImVec4(0.90f, 0.93f, 0.99f, 1.00f)},
        {ImGuiCol_FrameBgActive,        ImVec4(0.83f, 0.88f, 0.98f, 1.00f)},
        {ImGuiCol_TitleBg,              ImVec4(0.88f, 0.88f, 0.90f, 1.00f)},
        {ImGuiCol_TitleBgActive,        ImVec4(0.82f, 0.85f, 0.92f, 1.00f)},
        {ImGuiCol_TitleBgCollapsed,     ImVec4(0.90f, 0.90f, 0.91f, 1.00f)},
        {ImGuiCol_MenuBarBg,            ImVec4(0.90f, 0.90f, 0.91f, 1.00f)},
        {ImGuiCol_ScrollbarBg,          ImVec4(0.91f, 0.91f, 0.92f, 1.00f)},
        {ImGuiCol_ScrollbarGrab,        ImVec4(0.75f, 0.75f, 0.77f, 1.00f)},
        {ImGuiCol_ScrollbarGrabHovered, ImVec4(0.65f, 0.65f, 0.68f, 1.00f)},
        {ImGuiCol_ScrollbarGrabActive,  ImVec4(0.55f, 0.55f, 0.58f, 1.00f)},
        {ImGuiCol_CheckMark,            ImVec4(0.17f, 0.45f, 0.85f, 1.00f)},
        {ImGuiCol_SliderGrab,           ImVec4(0.26f, 0.55f, 0.90f, 1.00f)},
        {ImGuiCol_SliderGrabActive,     ImVec4(0.17f, 0.45f, 0.85f, 1.00f)},
        {ImGuiCol_Button,               ImVec4(0.89f, 0.89f, 0.91f, 1.00f)},
        {ImGuiCol_ButtonHovered,        ImVec4(0.80f, 0.86f, 0.98f, 1.00f)},
        {ImGuiCol_ButtonActive,         ImVec4(0.70f, 0.79f, 0.96f, 1.00f)},
        {ImGuiCol_Header,               ImVec4(0.82f, 0.87f, 0.98f, 1.00f)},
        {ImGuiCol_HeaderHovered,        ImVec4(0.75f, 0.83f, 0.98f, 1.00f)},
        {ImGuiCol_HeaderActive,         ImVec4(0.67f, 0.78f, 0.97f, 1.00f)},
        {ImGuiCol_Separator,            ImVec4(0.78f, 0.78f, 0.80f, 1.00f)},
        {ImGuiCol_SeparatorHovered,     ImVec4(0.40f, 0.60f, 0.92f, 1.00f)},
        {ImGuiCol_SeparatorActive,      ImVec4(0.26f, 0.50f, 0.90f, 1.00f)},
        {ImGuiCol_ResizeGrip,           ImVec4(0.60f, 0.70f, 0.95f, 0.40f)},
        {ImGuiCol_ResizeGripHovered,    ImVec4(0.45f, 0.60f, 0.93f, 0.70f)},
        {ImGuiCol_ResizeGripActive,     ImVec4(0.26f, 0.50f, 0.90f, 0.90f)},
        {ImGuiCol_Tab,                  ImVec4(0.87f, 0.87f, 0.89f, 1.00f)},
        {ImGuiCol_TabHovered,           ImVec4(0.80f, 0.86f, 0.98f, 1.00f)},
        {ImGuiCol_TabActive,            ImVec4(0.95f, 0.95f, 0.96f, 1.00f)},
        {ImGuiCol_TabUnfocused,         ImVec4(0.87f, 0.87f, 0.89f, 1.00f)},
        {ImGuiCol_TabUnfocusedActive,   ImVec4(0.91f, 0.91f, 0.92f, 1.00f)},
        {ImGuiCol_TableHeaderBg,        ImVec4(0.87f, 0.87f, 0.89f, 1.00f)},
        {ImGuiCol_TableBorderStrong,    ImVec4(0.72f, 0.72f, 0.75f, 1.00f)},
        {ImGuiCol_TableBorderLight,     ImVec4(0.83f, 0.83f, 0.85f, 1.00f)},
        {ImGuiCol_TableRowBg,           ImVec4(0.97f, 0.97f, 0.98f, 1.00f)},
        {ImGuiCol_TableRowBgAlt,        ImVec4(0.93f, 0.93f, 0.95f, 1.00f)},
    };
    for (const C& c : kColors) ImGui::PushStyleColor(c.idx, c.col);
    return static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));
}

void DrawLsaEditorTab(AppCommandState& cmd, std::vector<std::string>& log) {
    TraverseData& td = cmd.traverseData;
    const float panelH = ImGui::GetContentRegionAvail().y;
    // Persist across frames (and the splitters below mutate these directly) —
    // one LSA Editor window exists at a time, so function statics are fine,
    // same idiom as the Command tab's own scroll-position statics. Defaults are
    // derived from the CURRENT window size the first time this draws, as
    // fractions of it (not fixed pixel widths), so they land at the same
    // proportions regardless of what size the window happens to be the first
    // time it opens — a fixed-pixel default looked right only at the one window
    // size it was measured against and collapsed the right panel at any other.
    static bool s_sizesInit = false;
    static float s_leftW = 0.f;
    static float s_rightW = 0.f;
    static float s_filesH = 0.f;
    static float s_summaryH = 0.f;
    if (!s_sizesInit) {
        const float totalW = ImGui::GetContentRegionAvail().x;
        s_leftW = 0.1725f * totalW;
        s_rightW = 0.2035f * totalW;
        // Fractions of the left panel's own content height (Files : Summary :
        // Preview ~= 38% : 29% : 33%, measured off the reference layout).
        s_filesH = 0.379f * panelH;
        s_summaryH = 0.287f * panelH;
        s_sizesInit = true;
    }
    const float minPanelW = 160.f;
    const float splitterW = 6.f * 2.f;  // two splitters, each reserved from the center.
    const float maxSideW = std::max(minPanelW, ImGui::GetContentRegionAvail().x - minPanelW - splitterW);
    s_leftW = std::clamp(s_leftW, minPanelW, maxSideW);
    s_rightW = std::clamp(s_rightW, minPanelW, maxSideW - s_leftW + minPanelW);
    const float leftW = s_leftW;
    const float rightW = s_rightW;

    // ---------------------------------------------------------------- Left --
    // Files/Summary/Preview are independently resizable (issue #848 follow-up) —
    // each lives in its own fixed-height child with a drag handle below it;
    // Preview (the plot) takes whatever height is left.
    const float minSectionH = 32.f;
    const float minPreviewH = 80.f;
    const float splitterH = 6.f * 2.f;  // two horizontal splitters.
    const float maxSectionH = std::max(minSectionH, panelH - minSectionH - minPreviewH - splitterH);
    s_filesH = std::clamp(s_filesH, minSectionH, maxSectionH);
    s_summaryH = std::clamp(s_summaryH, minSectionH, maxSectionH - s_filesH + minSectionH);

    if (ImGui::BeginChild("##lsa_left", ImVec2(leftW, panelH), ImGuiChildFlags_Borders)) {
        const float sectionW = ImGui::GetContentRegionAvail().x;

        if (ImGui::BeginChild("##lsa_files", ImVec2(0.f, s_filesH))) {
            ImGui::SeparatorText("Files");
            // Import/Save/Load now live in the File menu above — this panel is too
            // narrow for that whole button row (REQ-387-style floating window is
            // often sized like a document window, not a wide toolbar strip).
            if (!cmd.traverseFilePath.empty()) ImGui::TextWrapped(".gstrav: %s", cmd.traverseFilePath.c_str());
            else ImGui::TextDisabled(".gstrav: (unsaved)");
            if (!cmd.traverseGodatFilePath.empty()) ImGui::TextWrapped(".godat: %s", cmd.traverseGodatFilePath.c_str());
            else ImGui::TextDisabled(".godat: (unsaved)");
        }
        ImGui::EndChild();
        DrawHorizontalSplitter("##lsa_split_files", &s_filesH, minSectionH, maxSectionH, sectionW);

        if (ImGui::BeginChild("##lsa_summary", ImVec2(0.f, s_summaryH))) {
            ImGui::SeparatorText("Summary");
            const int stationCount = td.networkStations.empty()
                                          ? static_cast<int>(td.legs.size()) + 1
                                          : static_cast<int>(td.networkStations.size());
            if (td.closureValid) {
                ImGui::Text("Stations: %d", stationCount);
                ImGui::Text("Misclosure: %.4f", td.closureLinear);
                if (td.closureLinear > 1e-6)
                    ImGui::Text("Precision: 1:%.0f", td.closurePrecision);
                else
                    ImGui::TextUnformatted("Precision: perfect");
            } else {
                ImGui::Text("Stations: %d", stationCount);
                ImGui::TextDisabled("No closure (open traverse or not computed).");
            }
            EnsureLsaComputed(cmd);
            if (cmd.traverseLsaResult.ok) {
                ImGui::Text("Std dev of unit weight: %.4f", cmd.traverseLsaResult.refStdDev);
                ImGui::Text("Redundancy: %d", cmd.traverseLsaResult.redundancy);
            } else {
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.3f, 1.f), "LSA: %s", cmd.traverseLsaResult.message.c_str());
            }
        }
        ImGui::EndChild();
        DrawHorizontalSplitter("##lsa_split_summary", &s_summaryH, minSectionH,
                               maxSectionH - s_filesH + minSectionH, sectionW);

        ImGui::SeparatorText("Preview");
        ImGui::TextDisabled("Points, legs, and error ellipses (computed)");
        if (ImGui::BeginChild("##lsa_preview", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders)) {
            DrawLsaStationPlot(cmd);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    DrawVerticalSplitter("##lsa_split_l", &s_leftW, minPanelW, maxSideW, panelH, /*growsRight=*/true);

    // -------------------------------------------------------------- Center --
    // The whole window is one uniform light theme now (matching STAR*NET-PRO's
    // own cohesive light chrome) — no separate per-panel theme push here.
    if (ImGui::BeginChild("##lsa_center", ImVec2(-(rightW + 6.f), panelH), ImGuiChildFlags_Borders)) {
        DrawTraverseCommandTab(cmd, log);
    }
    ImGui::EndChild();

    DrawVerticalSplitter("##lsa_split_r", &s_rightW, minPanelW, maxSideW - s_leftW + minPanelW, panelH,
                         /*growsRight=*/false);

    // --------------------------------------------------------------- Right --
    if (ImGui::BeginChild("##lsa_right", ImVec2(rightW, panelH), ImGuiChildFlags_Borders)) {
        ImGui::SeparatorText("Output Report");
        EnsureLsaComputed(cmd);
        const LsaResult& r = cmd.traverseLsaResult;
        // STAR*NET-PRO's Output panel is itself sub-tabbed (Listings / Errors /
        // Coordinates / Lat-Longs / Ground / Dump, tabs along the bottom — ImGui
        // tab bars only place tabs at the top of their own region, so this is the
        // same set of pages with the tab strip on top instead). Lat/Longs and
        // Ground aren't here: both need project-wide geodetic/ground-scale math
        // GoSurvey doesn't have yet (only a single GPS-vector origin, not a full
        // project datum) — a real gap, not something to fake with placeholder
        // numbers. Dump (STAR*NET's raw internal adjustment-matrix dump) isn't
        // something GoSurvey's solver currently exposes either.
        if (ImGui::BeginChild("##lsa_report_body", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders)) {
            ImGui::PushFont(FontReg::Resolve("Courier New", false, false));
            if (ImGui::BeginTabBar("##lsa_report_tabs")) {
                if (ImGui::BeginTabItem("Listings")) {
                    if (!r.ok) {
                        ImGui::TextColored(ImVec4(0.80f, 0.25f, 0.10f, 1.f), "Least squares unavailable:");
                        ImGui::TextWrapped("%s", r.message.c_str());
                    } else {
                        std::string rpt;
                        rpt += "Observations : " + std::to_string(r.observations) + "\n";
                        rpt += "Unknowns     : " + std::to_string(r.unknowns) + "\n";
                        rpt += "Redundancy   : " + std::to_string(r.redundancy) + "\n";
                        rpt += "Iterations   : " + std::to_string(r.iterations) + "\n";
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.4f", r.refStdDev);
                        rpt += std::string("Std dev unit wt : ") + buf + "\n";
                        std::snprintf(buf, sizeof(buf), "%.6f", r.adjClosureLinear);
                        rpt += std::string("Adj. misclosure : ") + buf + "\n";
                        if (r.usedGpsVectors) {
                            rpt += "GPS vectors used: " + std::to_string(r.gpsVectorsUsed);
                            if (r.gpsVectorsSkipped > 0) rpt += " (skipped " + std::to_string(r.gpsVectorsSkipped) + ")";
                            rpt += "\n";
                        }
                        AppendLsaResidualReport(rpt, r);
                        ImGui::TextUnformatted(rpt.c_str());
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Errors")) {
                    bool any = false;
                    if (!cmd.traverseCommandError.empty()) {
                        ImGui::TextColored(ImVec4(0.80f, 0.25f, 0.10f, 1.f), "Command text:");
                        ImGui::TextWrapped("%s", cmd.traverseCommandError.c_str());
                        any = true;
                    }
                    if (!r.ok) {
                        if (any) ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.80f, 0.25f, 0.10f, 1.f), "Least squares:");
                        ImGui::TextWrapped("%s", r.message.c_str());
                        any = true;
                    }
                    if (!any) ImGui::TextDisabled("No errors.");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Coordinates")) {
                    if (ImGui::BeginTable("##lsa_report_coords", 4,
                                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
                        ImGui::TableSetupColumn("Pt", ImGuiTableColumnFlags_WidthFixed, 44.f);
                        ImGui::TableSetupColumn("Easting");
                        ImGui::TableSetupColumn("Northing");
                        ImGui::TableSetupColumn("Elev");
                        ImGui::TableHeadersRow();
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%d", td.startStationId);
                        ImGui::TableNextColumn(); ImGui::Text("%.4f", td.startEasting);
                        ImGui::TableNextColumn(); ImGui::Text("%.4f", td.startNorthing);
                        ImGui::TableNextColumn(); ImGui::Text("%.4f", td.startElevation);
                        for (const TraverseLeg& leg : td.legs) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn(); ImGui::Text("%d", leg.stationId);
                            ImGui::TableNextColumn();
                            if (leg.computed) ImGui::Text("%.4f", leg.computedEasting); else ImGui::TextDisabled("\xe2\x80\x94");
                            ImGui::TableNextColumn();
                            if (leg.computed) ImGui::Text("%.4f", leg.computedNorthing); else ImGui::TextDisabled("\xe2\x80\x94");
                            ImGui::TableNextColumn();
                            if (leg.computed) ImGui::Text("%.4f", leg.computedElevation); else ImGui::TextDisabled("\xe2\x80\x94");
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::PopFont();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

} // namespace


void DrawTraverseEditorPanel(AppCommandState& cmd, std::vector<std::string>& log) {
    if (!cmd.showTraverseEditorWindow)
        return;

    // REQ-401: resolve the scale factor every frame (cheap — one CS-MAP lookup, only while the
    // option is on) so a Drawing Settings change made elsewhere is picked up without a stale
    // cached factor. Only forces a recompute when the resolved number actually changes.
    TraverseData& tdPre = cmd.traverseData;
    std::string scaleFactorError;
    if (tdPre.applyScaleFactor) {
        double factor = 1.0;
        scaleFactorError = ResolveTraverseScaleFactor(cmd, tdPre.scaleFactorKind, &factor);
        const double resolved = scaleFactorError.empty() ? factor : 1.0;
        if (resolved != tdPre.scaleFactor) {
            tdPre.scaleFactor = resolved;
            cmd.traverseDataDirty = true;
        }
    }

    // Recompute when inputs change.
    if (cmd.traverseDataDirty) {
        ComputeTraverse(cmd.traverseData);
        cmd.traverseDataDirty = false;
        cmd.traverseLsaComputed = false;  // inputs changed -> adjustment is stale.
        // #845 Phase A: the data actually changed (from the Form tab, an import, or a
        // prior Apply) — regenerate the Command tab's text next time it's drawn. Merely
        // switching tabs must NOT hit this, or unapplied typed text would be discarded.
        cmd.traverseCommandTextLoaded = false;
    }

    // The closure window is launched from this editor; draw it here so it
    // survives the editor being collapsed.
    DrawTraverseClosureWindow(cmd, log);

    ImGui::SetNextWindowSize(ImVec2(1080, 580), ImGuiCond_FirstUseEver);
    bool open = cmd.showTraverseEditorWindow;
    // Its own OS window, same technique as the PDF viewer (REQ-387): NoAutoMerge gives
    // it a real platform viewport instead of folding it into the main window, with the
    // operating system's own minimize/maximize/close and a taskbar entry. Dragging its
    // tab onto a dock slot docks it back into GoSurvey's layout like any other panel.
    // ImGui only knows whether it actually got an OS-framed viewport AFTER Begin() —
    // s_osFramed is last frame's answer, used to decide this frame's title-bar flag
    // (same one-frame-behind idiom PdfViewerWindow.cpp uses per-viewer).
    static bool s_osFramed = false;
    ImGuiWindowClass travClass;
    travClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    travClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&travClass);
    const int chromeThemeN = PushLsaLightTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    // In its own OS window the operating system draws the title bar and close button, so
    // ImGui draws no second one. Docked, the tab is the only handle there is, so it comes
    // back. To dock a floating editor, drag it by any empty part of its tab bar.
    // NoMove alongside NoTitleBar: without a real ImGui title bar, Dear ImGui falls back to
    // "click any empty background to move the window" (imgui.cpp's StartMouseMovingWindow
    // skip-check only cancels that for a windowed title bar or a docked window) — which
    // means dragging on what looks like a panel border inside the three-pane layout below
    // was actually dragging the whole OS window. The real OS title bar already lets you
    // move it once it's floating, so ImGui's own background-drag fallback is just noise here.
    const ImGuiWindowFlags baseFlags = ImGuiWindowFlags_MenuBar;
    const bool shown = ImGui::Begin("LSA Editor###TraverseEditor", &open,
                                    baseFlags | (s_osFramed ? (ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove)
                                                            : ImGuiWindowFlags_None));
    ImGui::PopStyleVar();
    {
        ImGuiWindow* self = ImGui::GetCurrentWindow();
        s_osFramed = self->ViewportOwned && !self->DockIsActive && self->Viewport != nullptr &&
                    (self->Viewport->Flags & ImGuiViewportFlags_NoDecoration) == 0;
    }
    if (!shown) {
        cmd.showTraverseEditorWindow = open;
        ImGui::End();
        ImGui::PopStyleColor(chromeThemeN);
        return;
    }
    cmd.showTraverseEditorWindow = open;

    TraverseData& td = cmd.traverseData;

    DrawTraverseMenuBar(cmd, log);
    // STAR*NET-PRO-style toolbar strip, same light chrome as everything else —
    // a thin border is what separates it from the menu bar, not a color change.
    if (ImGui::BeginChild("##lsa_toolbar", ImVec2(0.f, ImGui::GetFrameHeightWithSpacing() + 4.f),
                          ImGuiChildFlags_Borders)) {
        DrawTraverseToolbar(cmd, log);
    }
    ImGui::EndChild();

    // GitHub issue #845 (Phase A) — "Editor" (primary: the StarNet-PRO-style three-pane
    // layout, command editor in the middle) vs. "Table View" (secondary, form/table
    // editing — kept for now, may be removed later), two-way synced through the same
    // traverseData/traverseLsaWeights.
    if (!ImGui::BeginTabBar("traverse_view_tabs")) {
        ImGui::End();
        ImGui::PopStyleColor(chromeThemeN);
        return;
    }

    if (ImGui::BeginTabItem("Editor")) {
        DrawLsaEditorTab(cmd, log);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Table View")) {

    DrawTraverseFileButtons(cmd, log);

    // ---- Starting station header ----
    ImGui::SeparatorText("Starting Station");
    // Unit indicator (issue #831): the editor gave no clue which unit entered/displayed
    // distances were in, so this makes the active drawing's linear unit explicit.
    {
        int unitSel = 2;  // Feet, matching kDrawingUnitNames' fallback default (REQ-357).
        for (int i = 0; i < kDrawingUnitCount; ++i)
            if (kDrawingUnitCodes[i] == cmd.drawingInsUnits) unitSel = i;
        ImGui::SameLine();
        ImGui::TextDisabled("(Units: %s)", kDrawingUnitNames[unitSel]);
        ItemHelpTooltip(
            "Distances entered and shown in this editor (H.Dist, S.Dist, perimeter,\n"
            "misclosure, closure analysis) use the active drawing's linear unit,\n"
            "set in Drawing Settings or the UNITS command.");
    }

    const float colGap = 14.f;

    // One column: a caption above its input field. The body lambda draws the input
    // (its width is set by the caller via SetNextItemWidth before invoking).
    auto LabeledCol = [&](const char* caption, float width, const auto& body) {
        ImGui::BeginGroup();
        ImGui::TextUnformatted(caption);
        ImGui::SetNextItemWidth(width);
        body();
        ImGui::EndGroup();
    };
    // A checkbox aligned to the input row: a blank caption-height spacer sits where
    // the other columns' captions are, so the checkbox lines up with the fields.
    auto CheckboxCol = [&](const char* label, bool* value, const char* tip) {
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0.f, ImGui::GetTextLineHeight()));
        if (ImGui::Checkbox(label, value))
            cmd.traverseDataDirty = true;
        ImGui::EndGroup();
        if (tip && *tip) ItemHelpTooltip(tip);
    };

    LabeledCol("Start ID", 90.f, [&] {
        if (ImGui::InputInt("##trv_id", &td.startStationId, 0, 0))
            cmd.traverseDataDirty = true;
    });
    ImGui::SameLine(0, colGap);
    LabeledCol("Easting", 130.f, [&] {
        if (ImGui::InputDouble("##trv_e", &td.startEasting, 0., 0., "%.4f"))
            cmd.traverseDataDirty = true;
    });
    ImGui::SameLine(0, colGap);
    LabeledCol("Northing", 130.f, [&] {
        if (ImGui::InputDouble("##trv_n", &td.startNorthing, 0., 0., "%.4f"))
            cmd.traverseDataDirty = true;
    });
    ImGui::SameLine(0, colGap);
    LabeledCol("Elevation", 130.f, [&] {
        if (ImGui::InputDouble("##trv_z", &td.startElevation, 0., 0., "%.4f"))
            cmd.traverseDataDirty = true;
    });
    ImGui::SameLine(0, colGap);
    LabeledCol("Ref Bearing\xc2\xb0", 120.f, [&] {
        if (ImGui::InputText("##trv_sb", &td.startBearingBuf)) {
            ParseStartBearing(td);
            cmd.traverseDataDirty = true;
        }
    });
    ItemHelpTooltip(
        "Reference orientation at the start station (° CW from N).\n"
        "Accepts decimal degrees or DdMmSs (e.g. 45d30m10s).\n"
        "This is the azimuth of the backsight direction (or any reference mark)\n"
        "that the instrument was zeroed on. The forward bearing of the first leg\n"
        "= this value + the first row's H.Angle.\n"
        "If you know the first leg's bearing directly, enter it here and set\n"
        "the first row's H.Angle to 0.");

    // ---- Options ----
    ImGui::SameLine(0, 24);
    CheckboxCol("Closed Loop##trv", &td.isClosedLoop,
                "Report closure error back to the starting station.");
    ImGui::SameLine(0, 24);
    CheckboxCol("Curv + Refr##trv", &td.applyCurvRefCorr,
                "Apply the combined earth curvature + atmospheric refraction correction\n"
                "C = (1 - k) * D^2 / (2R) to each leg's elevation difference (#826).\n"
                "Off = plain trigonometry (previous behavior).");
    ImGui::SameLine(0, 24);
    LabeledCol("Refr k", 90.f, [&] {
        ImGui::BeginDisabled(!td.applyCurvRefCorr);
        if (ImGui::InputDouble("##trv_k", &td.refractionCoeff, 0.0, 0.0, "%.3f"))
            cmd.traverseDataDirty = true;
        ImGui::EndDisabled();
    });
    ItemHelpTooltip("Refraction coefficient k (typical range -2 to +2).\n"
                    "0.14 = average atmospheric conditions (default).");
    if (td.applyCurvRefCorr && (td.refractionCoeff < -2.0 || td.refractionCoeff > 2.0)) {
        ImGui::SameLine(0, 8);
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f),
                           "k outside the typical -2..+2 range");
    }

    // ---- Scale factor (REQ-401, GitHub issue #828) ----
    ImGui::SameLine(0, 24);
    CheckboxCol("Scale Factor##trv", &td.applyScaleFactor,
                "Reduce each leg's horizontal distance from ground to grid using the active\n"
                "drawing's coordinate system (Drawing Settings > Transformation tab).\n"
                "Off by default; existing traverses are unaffected unless turned on.");
    if (td.applyScaleFactor) {
        ImGui::SameLine();
        LabeledCol("Factor##trv_sf", 150.f, [&] {
            int kind = static_cast<int>(td.scaleFactorKind);
            if (ImGui::Combo("##trv_sf_kind", &kind, "Combined\0Grid only\0")) {
                td.scaleFactorKind = static_cast<TraverseData::ScaleFactorKind>(kind);
                cmd.traverseDataDirty = true;
            }
        });
        ItemHelpTooltip(
            "Combined: grid scale factor x elevation/sea-level factor, as resolved by the\n"
            "Transformation tab's reference point.\n"
            "Grid only: the map-projection grid scale factor alone, ignoring elevation/sea-level\n"
            "reduction even if the Transformation tab has it enabled.");
        ImGui::SameLine();
        if (!scaleFactorError.empty())
            ImGui::TextColored(ImVec4(1.f, 0.55f, 0.3f, 1.f), "(%s)", scaleFactorError.c_str());
        else
            ImGui::Text("k = %.6f", td.scaleFactor);
    }

    ImGui::SeparatorText("Legs");

    // ---- Table ----
    // Slim grid: data-entry columns, a thin divider, then the computed-output
    // columns (tinted, read-only). Per-face F1/F2 data lives in each leg's
    // observation expander, not here.
    constexpr int kNumCols = 17;  // 8 input + 1 divider + 8 computed.

    // Calm borders: an outer frame and horizontal row rules only — no busy
    // vertical grid lines. Row striping (RowBg) carries the eye instead.
    const ImGuiTableFlags tf =
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    // Keep the expanded-leg selection valid (used below the table).
    if (cmd.traverseExpandedLeg >= static_cast<int>(td.legs.size()))
        cmd.traverseExpandedLeg = -1;  // legs shrank out from under the selection.
    const float editorH = 300.f;

    // Grow with the number of legs, but clamp so an empty traverse isn't tiny and
    // a long one scrolls instead of taking over the panel. +1 row for the add-leg
    // row, +1 for the header.
    const float rowH     = ImGui::GetFrameHeightWithSpacing();
    const int   bodyRows = std::clamp(static_cast<int>(td.legs.size()) + 1, 3, 12);
    const float tableH   = rowH * static_cast<float>(bodyRows + 1);

    // Tints: subtle gray for the read-only computed cells; a darker bar for the
    // input | computed divider column.
    const ImU32 tintComputed = ImGui::GetColorU32(ImVec4(0.46f, 0.51f, 0.58f, 0.16f));
    const ImU32 tintDivider  = ImGui::GetColorU32(ImVec4(0.38f, 0.43f, 0.50f, 0.55f));

    // A read-only computed numeric cell: tinted, right-aligned, 3 dp.
    auto computedNum = [&](bool valid, double v) {
        ImGui::TableNextColumn();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
        if (valid) {
            char b[32];
            std::snprintf(b, sizeof(b), "%.3f", v);
            RightAlignedText(b);
        } else {
            ImGui::TextDisabled("\xe2\x80\x94");
        }
    };

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.f, 4.f));
    if (ImGui::BeginTable("traverse_legs", kNumCols, tf, ImVec2(0.f, tableH))) {
        ImGui::TableSetupScrollFreeze(0, 1);

        // --- Input columns ---
        ImGui::TableSetupColumn("#",         ImGuiTableColumnFlags_WidthFixed,   50.f);
        ImGui::TableSetupColumn("Stn ID",    ImGuiTableColumnFlags_WidthFixed,   55.f);
        ImGui::TableSetupColumn("Desc",      ImGuiTableColumnFlags_WidthStretch, 90.f);
        ImGui::TableSetupColumn("H.Angle\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("H.Dist",    ImGuiTableColumnFlags_WidthFixed,   80.f);
        ImGui::TableSetupColumn("S.Dist",    ImGuiTableColumnFlags_WidthFixed,   80.f);
        ImGui::TableSetupColumn("V.Angle\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("Z",         ImGuiTableColumnFlags_WidthFixed,   22.f); // zenith checkbox
        // --- Divider ---
        ImGui::TableSetupColumn("##div",     ImGuiTableColumnFlags_WidthFixed |
                                             ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoReorder, 8.f);
        // --- Computed columns (read-only) ---
        ImGui::TableSetupColumn("Bearing\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("\xce\x94""E",       ImGuiTableColumnFlags_WidthFixed,   78.f);
        ImGui::TableSetupColumn("\xce\x94""N",       ImGuiTableColumnFlags_WidthFixed,   78.f);
        ImGui::TableSetupColumn("\xce\x94""Z",       ImGuiTableColumnFlags_WidthFixed,   70.f);
        ImGui::TableSetupColumn("Easting",   ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("Northing",  ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("Elev",      ImGuiTableColumnFlags_WidthFixed,   80.f);
        ImGui::TableSetupColumn("Status",    ImGuiTableColumnFlags_WidthFixed,   56.f);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();

        for (size_t i = 0; i < td.legs.size(); ++i) {
            TraverseLeg& leg = td.legs[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // # row number + expand toggle for the per-leg observation editor.
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const bool expanded = (cmd.traverseExpandedLeg == static_cast<int>(i));
            if (ImGui::ArrowButton("##exp", expanded ? ImGuiDir_Down : ImGuiDir_Right))
                cmd.traverseExpandedLeg = expanded ? -1 : static_cast<int>(i);
            ItemHelpTooltip("Expand this leg to view and edit its individual observations.");
            ImGui::SameLine(0, 4);
            char numBuf[8];
            std::snprintf(numBuf, sizeof(numBuf), "%zu", i + 1);
            ImGui::TextUnformatted(numBuf);

            // Stn ID
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##stn", &leg.stationId, 0, 0))
                cmd.traverseDataDirty = true;

            // Description
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##desc", &leg.description);

            // H.Angle
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##ha", &leg.horizAngleBuf);
            ItemHelpTooltip("Horizontal angle turned clockwise from the backsight direction to this foresight.\nAccepts decimal degrees or DdMmSs (e.g. 45d30m10s).");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                ParseHorizAngle(leg);
                cmd.traverseDataDirty = true;
            }

            // H.Dist
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputDouble("##hd", &leg.horizDist, 0., 0., "%.4f");
            ItemHelpTooltip("Horizontal distance (direct measurement).");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                leg.hasHorizDist = (leg.horizDist > 0.0);
                cmd.traverseDataDirty = true;
            }

            // S.Dist
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputDouble("##sd", &leg.slopeDist, 0., 0., "%.4f");
            ItemHelpTooltip("Slope (EDM) distance. Requires vertical angle to reduce to horizontal.");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                leg.hasSlopeDist = (leg.slopeDist > 0.0);
                cmd.traverseDataDirty = true;
            }

            // V.Angle
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##va", &leg.vertAngleBuf);
            ItemHelpTooltip("Vertical or zenith angle.\nAccepts decimal degrees or DdMmSs.\nToggle Z column to switch zenith/elevation convention.");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                ParseVertAngle(leg);
                cmd.traverseDataDirty = true;
            }

            // Z/E zenith toggle (compact checkbox)
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
            bool zenith = leg.isZenithAngle;
            if (ImGui::Checkbox("##zen", &zenith)) {
                leg.isZenithAngle = zenith;
                cmd.traverseDataDirty = true;
            }
            ImGui::PopStyleColor(2);
            ItemHelpTooltip("Checked = zenith angle (90° is level).\nUnchecked = elevation angle (0° is level).");

            // Divider between the input and computed groups.
            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintDivider);

            // Computed: Bearing (tinted, right-aligned).
            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
            if (leg.computed)
                RightAlignedText(TraverseFormatBearing(leg.computedBearingDeg).c_str());
            else
                ImGui::TextDisabled("\xe2\x80\x94");

            // ΔE, ΔN, ΔZ, Easting, Northing, Elevation.
            computedNum(leg.computed, leg.computedDeltaE);
            computedNum(leg.computed, leg.computedDeltaN);
            computedNum(leg.computed, leg.computedDeltaZ);
            computedNum(leg.computed, leg.computedEasting);
            computedNum(leg.computed, leg.computedNorthing);
            computedNum(leg.computed, leg.computedElevation);

            // Status (tinted).
            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
            if (leg.hasSufficientData) {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.f), "\xe2\x9c\x93"); // ✓
            } else if (!leg.errorMsg.empty()) {
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), "\xe2\x9c\x97");    // ✗
                ItemHelpTooltip(leg.errorMsg.c_str());
            } else {
                ImGui::TextDisabled("\xe2\x80\x94");
            }

            ImGui::PopID();
        }

        // ---- Add-leg row: a "+ Add Leg" action directly under the last leg. ----
        ImGui::TableNextRow();
        ImGui::TableNextColumn();  // # column
        ImGui::TableNextColumn();  // Stn ID column
        ImGui::TableNextColumn();  // Desc column (widest) holds the button
        if (ImGui::SmallButton("+ Add Leg")) {
            TraverseLeg newLeg;
            if (!td.legs.empty())
                newLeg.stationId = td.legs.back().stationId + 1;
            else
                newLeg.stationId = td.startStationId + 1;
            newLeg.isZenithAngle = true;
            td.legs.push_back(std::move(newLeg));
            cmd.traverseDataDirty = true;
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();  // CellPadding

    // ---- Per-leg observation editor (REQ-018), directly under the table. ----
    // Re-read the selection LIVE here: the arrow toggles above can change it
    // mid-frame (including to -1 when collapsing), so a stale top-of-frame flag
    // must not be trusted. Guard the full [0, size) range before indexing.
    const int exIdx = cmd.traverseExpandedLeg;
    if (exIdx >= 0 && exIdx < static_cast<int>(td.legs.size())) {
        if (ImGui::BeginChild("legobs", ImVec2(0.f, editorH), ImGuiChildFlags_Borders)) {
            const size_t idx = static_cast<size_t>(exIdx);
            DrawLegObservationEditor(td.legs[idx], idx, cmd.traverseDataDirty);
        }
        ImGui::EndChild();
    }

    // ---- Leg buttons ----
    const bool hasLegs = !td.legs.empty();
    if (!hasLegs) ImGui::BeginDisabled();
    if (ImGui::Button("- Remove Last")) {
        td.legs.pop_back();
        cmd.traverseDataDirty = true;
    }
    if (!hasLegs) ImGui::EndDisabled();
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Clear All")) {
        td.legs.clear();
        cmd.traverseDataDirty = true;
    }

    // ---- Side shots / check shots (GitHub issue #853) ----
    ImGui::SeparatorText("Side Shots / Check Shots");
    {
        const ImGuiTableFlags ssTf = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("traverse_sideshots", 9, ssTf)) {
            ImGui::TableSetupColumn("At",       ImGuiTableColumnFlags_WidthFixed, 55.f);
            ImGui::TableSetupColumn("Point",    ImGuiTableColumnFlags_WidthFixed, 55.f);
            ImGui::TableSetupColumn("Desc",     ImGuiTableColumnFlags_WidthStretch, 90.f);
            ImGui::TableSetupColumn("H.Angle\xc2\xb0", ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("Dist",     ImGuiTableColumnFlags_WidthFixed, 80.f);
            ImGui::TableSetupColumn("Check",    ImGuiTableColumnFlags_WidthFixed, 50.f);
            ImGui::TableSetupColumn("Easting",  ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("Northing", ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("Discrepancy", ImGuiTableColumnFlags_WidthFixed, 90.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            for (size_t i = 0; i < td.sideShots.size(); ++i) {
                TraverseSideShot& ss = td.sideShots[i];
                ImGui::PushID(static_cast<int>(i) + 100000);  // offset clear of the legs table's PushIDs.
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##at", &ss.atStationId, 0, 0)) cmd.traverseDataDirty = true;

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##pt", &ss.stationId, 0, 0)) cmd.traverseDataDirty = true;

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##desc", &ss.description);

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##ha", &ss.horizAngleBuf);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    double v;
                    if (TraverseParseAngle(ss.horizAngleBuf, &v)) { ss.horizAngleDeg = v; ss.hasHorizAngle = true; }
                    cmd.traverseDataDirty = true;
                }

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputDouble("##d", &ss.horizDist, 0., 0., "%.4f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ss.hasHorizDist = (ss.horizDist > 0.0);
                    cmd.traverseDataDirty = true;
                }

                ImGui::TableNextColumn();
                if (ImGui::Checkbox("##check", &ss.isCheckShot)) cmd.traverseDataDirty = true;
                ItemHelpTooltip("Check shot: a repeat observation meant to verify the setup,\nnot a new detail point.");

                const ImU32 tintComputed = ImGui::GetColorU32(ImVec4(0.46f, 0.51f, 0.58f, 0.16f));
                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
                if (ss.computed) RightAlignedText(TraverseFormatDist(ss.computedEasting).c_str());
                else ImGui::TextDisabled("\xe2\x80\x94");

                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
                if (ss.computed) RightAlignedText(TraverseFormatDist(ss.computedNorthing).c_str());
                else ImGui::TextDisabled("\xe2\x80\x94");

                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
                if (ss.isCheckShot && ss.hasKnownCoords && ss.computed) {
                    char b[32];
                    std::snprintf(b, sizeof(b), "%.4f", ss.checkDiscrepancy);
                    RightAlignedText(b);
                } else if (!ss.errorMsg.empty()) {
                    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), "\xe2\x9c\x97");
                    ItemHelpTooltip(ss.errorMsg.c_str());
                } else {
                    ImGui::TextDisabled("\xe2\x80\x94");
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::SmallButton("+ Add Side Shot")) {
            TraverseSideShot ss;
            ss.atStationId = td.legs.empty() ? td.startStationId : td.legs.back().stationId;
            ss.stationId = ss.atStationId + 1000;  // a placeholder the user is expected to renumber.
            ss.isZenithAngle = true;
            td.sideShots.push_back(std::move(ss));
            cmd.traverseDataDirty = true;
        }
        ItemHelpTooltip(
            "A shot from an already-occupied station that doesn't advance the traverse:\n"
            "a radial detail point, or (tick Check) a repeat observation to verify the\n"
            "setup. At defaults to the last occupied station; edit it to any earlier one.");
        const bool hasSideShots = !td.sideShots.empty();
        if (!hasSideShots) ImGui::BeginDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("- Remove Last Side Shot")) {
            td.sideShots.pop_back();
            cmd.traverseDataDirty = true;
        }
        if (!hasSideShots) ImGui::EndDisabled();
    }

    // ---- GPS vectors (GitHub issue #847) ----
    ImGui::SeparatorText("GPS Vectors");
    {
        if (ImGui::Checkbox("##hasgpsorigin", &td.hasGpsOrigin)) cmd.traverseDataDirty = true;
        ImGui::SameLine();
        ImGui::TextUnformatted("Origin lat/long (WGS84, degrees)");
        ItemHelpTooltip(
            "The local tangent-plane origin GPS vectors are rotated into local\n"
            "easting/northing/elevation around (e.g. a GPS station's known lat/long).\n"
            "GoSurvey has no datum of its own, so this must be supplied manually.");
        if (!td.hasGpsOrigin) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(140.f);
        if (ImGui::InputDouble("Lat##gpsorigin", &td.gpsOriginLatDeg, 0., 0., "%.8f"))
            cmd.traverseDataDirty = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.f);
        if (ImGui::InputDouble("Lon##gpsorigin", &td.gpsOriginLonDeg, 0., 0., "%.8f"))
            cmd.traverseDataDirty = true;
        if (!td.hasGpsOrigin) ImGui::EndDisabled();

        const ImGuiTableFlags gvTf = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
                                     ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("traverse_gpsvectors", 8, gvTf)) {
            ImGui::TableSetupColumn("From",     ImGuiTableColumnFlags_WidthFixed, 55.f);
            ImGui::TableSetupColumn("To",       ImGuiTableColumnFlags_WidthFixed, 55.f);
            ImGui::TableSetupColumn("\xce\x94X (m)", ImGuiTableColumnFlags_WidthFixed, 85.f);
            ImGui::TableSetupColumn("\xce\x94Y (m)", ImGuiTableColumnFlags_WidthFixed, 85.f);
            ImGui::TableSetupColumn("\xce\x94Z (m)", ImGuiTableColumnFlags_WidthFixed, 85.f);
            ImGui::TableSetupColumn("Easting",  ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("Northing", ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("Elevation", ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            for (size_t i = 0; i < td.gpsVectors.size(); ++i) {
                TraverseGpsVector& gv = td.gpsVectors[i];
                ImGui::PushID(static_cast<int>(i) + 200000);  // offset clear of other tables' PushIDs.
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##from", &gv.fromStationId, 0, 0)) cmd.traverseDataDirty = true;

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputInt("##to", &gv.toStationId, 0, 0)) cmd.traverseDataDirty = true;

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputDouble("##dx", &gv.deltaXEcefM, 0., 0., "%.4f")) cmd.traverseDataDirty = true;

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputDouble("##dy", &gv.deltaYEcefM, 0., 0., "%.4f")) cmd.traverseDataDirty = true;

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputDouble("##dz", &gv.deltaZEcefM, 0., 0., "%.4f")) cmd.traverseDataDirty = true;

                const ImU32 tintComputed = ImGui::GetColorU32(ImVec4(0.46f, 0.51f, 0.58f, 0.16f));
                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
                if (gv.computed) RightAlignedText(TraverseFormatDist(gv.computedToEasting).c_str());
                else ImGui::TextDisabled("\xe2\x80\x94");

                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
                if (gv.computed) RightAlignedText(TraverseFormatDist(gv.computedToNorthing).c_str());
                else ImGui::TextDisabled("\xe2\x80\x94");

                ImGui::TableNextColumn();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, tintComputed);
                if (gv.computed) {
                    RightAlignedText(TraverseFormatDist(gv.computedToElevation).c_str());
                } else if (!gv.errorMsg.empty()) {
                    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), "\xe2\x9c\x97");
                    ItemHelpTooltip(gv.errorMsg.c_str());
                } else {
                    ImGui::TextDisabled("\xe2\x80\x94");
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (ImGui::SmallButton("+ Add GPS Vector")) {
            TraverseGpsVector gv;
            gv.fromStationId = td.legs.empty() ? td.startStationId : td.legs.back().stationId;
            gv.toStationId = gv.fromStationId + 1000;  // a placeholder the user is expected to renumber.
            td.gpsVectors.push_back(std::move(gv));
            cmd.traverseDataDirty = true;
        }
        ItemHelpTooltip(
            "A GPS baseline vector (ECEF \xce\x94X/\xce\x94Y/\xce\x94Z, meters) between two stations —\n"
            "e.g. from a STAR*NET \"G1\" record. Resolved into local easting/northing/\n"
            "elevation only when the From station already has a known position\n"
            "(the start station, a computed leg, or another resolved GPS vector).");
        const bool hasGpsVectors = !td.gpsVectors.empty();
        if (!hasGpsVectors) ImGui::BeginDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("- Remove Last GPS Vector")) {
            td.gpsVectors.pop_back();
            cmd.traverseDataDirty = true;
        }
        if (!hasGpsVectors) ImGui::EndDisabled();
    }

    // ---- Closure ----
    if (td.isClosedLoop && td.closureValid) {
        ImGui::SameLine(0, 24);
        char closureBuf[128];
        if (td.closureLinear < 1e-6) {
            std::snprintf(closureBuf, sizeof(closureBuf), "Closure: perfect");
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.f), "%s", closureBuf);
        } else {
            const double precip = td.closurePrecision;
            std::snprintf(closureBuf, sizeof(closureBuf),
                          "Closure: \xce\x94""E=%.4f  \xce\x94""N=%.4f  Linear=%.4f  1:%.0f",
                          td.closureDeltaE, td.closureDeltaN, td.closureLinear, precip);
            const ImVec4 col = (precip >= 5000.f)
                                   ? ImVec4(0.35f, 0.85f, 0.35f, 1.f)   // good (green)
                                   : ImVec4(1.f,   0.75f, 0.2f,  1.f);  // warn (orange)
            ImGui::TextColored(col, "%s", closureBuf);
        }
    }

    // ---- Closure analysis ----
    ImGui::Separator();
    if (ImGui::Button("Calculate Closure\xe2\x80\xa6")) {
        cmd.showTraverseClosureWindow = true;
        cmd.traverseLsaComputed = false;  // recompute fresh on open.
    }
    ItemHelpTooltip(
        "Open the closure analysis: unadjusted misclosure beside a weighted\n"
        "least-squares adjustment, with per-observation residuals.\n"
        "Requires a closed loop that returns to the start station.");

    // ---- Commit to drawing ----
    ImGui::Separator();
    if (ImGui::Button("Commit to Drawing")) {
        // Snapshot for undo
        PushUndoSnapshot(cmd, "Traverse commit");

        // Place starting point
        const double startE = td.startEasting  - cmd.worldDocumentOriginX;
        const double startN = td.startNorthing - cmd.worldDocumentOriginY;
        cmd.createPointsOpts.startNumber = td.startStationId;
        cmd.createPointsOpts.sequentialNumbering = false;
        cmd.createPointsOpts.duplicatePolicy = SurveyDuplicatePolicy::Renumber;
        TryPlaceSurveyPoint(cmd, startE, startN, td.startElevation, log, /*pushUndo=*/false);

        // Place foresight points
        for (const auto& leg : td.legs) {
            if (!leg.computed)
                break;
            const double locE = leg.computedEasting  - cmd.worldDocumentOriginX;
            const double locN = leg.computedNorthing - cmd.worldDocumentOriginY;
            cmd.createPointsOpts.startNumber = leg.stationId;
            TryPlaceSurveyPoint(cmd, locE, locN, leg.computedElevation, log, /*pushUndo=*/false);
        }

        // Place side-shot/check-shot points (GitHub issue #853).
        for (const auto& ss : td.sideShots) {
            if (!ss.computed)
                continue;
            const double locE = ss.computedEasting  - cmd.worldDocumentOriginX;
            const double locN = ss.computedNorthing - cmd.worldDocumentOriginY;
            cmd.createPointsOpts.startNumber = ss.stationId;
            TryPlaceSurveyPoint(cmd, locE, locN, ss.computedElevation, log, /*pushUndo=*/false);
        }

        // Place resolved GPS vector endpoints (GitHub issue #847).
        for (const auto& gv : td.gpsVectors) {
            if (!gv.computed)
                continue;
            const double locE = gv.computedToEasting  - cmd.worldDocumentOriginX;
            const double locN = gv.computedToNorthing - cmd.worldDocumentOriginY;
            cmd.createPointsOpts.startNumber = gv.toStationId;
            TryPlaceSurveyPoint(cmd, locE, locN, gv.computedToElevation, log, /*pushUndo=*/false);
        }

        log.push_back("TRAVERSE — committed points to drawing.");
    }
    ItemHelpTooltip("Place all computed traverse stations as survey points in the drawing.\nUse View Points to review or edit them after committing.");

    ImGui::EndTabItem();
    }  // Table View tab

    ImGui::EndTabBar();
    ImGui::End();
    ImGui::PopStyleColor(chromeThemeN);
}
