#include "CadUi.hpp"
#include "CadUiHelpers.hpp"
#include "CadCommands.hpp"
#include "SurveyPoints.hpp"
#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"
#include "traverse/FbkImport.hpp"
#include "WinFileDialogs.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>
#include <string>
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

    // ---- A-priori standard errors (weights) ----
    ImGui::SeparatorText("A-priori Standard Errors");
    bool wChanged = false;
    ImGui::SetNextItemWidth(120.f);
    wChanged |= ImGui::InputDouble("Angle \xcf\x83 (sec)##lsa_sa", &cmd.traverseLsaWeights.sigmaAngleSec, 0., 0., "%.2f");
    ImGui::SameLine(0, 16); ImGui::SetNextItemWidth(120.f);
    wChanged |= ImGui::InputDouble("Dist \xcf\x83 (ft)##lsa_sd", &cmd.traverseLsaWeights.sigmaDistConstFt, 0., 0., "%.4f");
    ImGui::SameLine(0, 16); ImGui::SetNextItemWidth(120.f);
    wChanged |= ImGui::InputDouble("Dist ppm##lsa_ppm", &cmd.traverseLsaWeights.sigmaDistPpm, 0., 0., "%.2f");
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
            } else if (ImGui::BeginTable("residuals", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                                         ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("From");
                ImGui::TableSetupColumn("To");
                ImGui::TableSetupColumn("Angle resid (sec)");
                ImGui::TableSetupColumn("Dist resid (ft)");
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                ImGui::TableHeadersRow();
                ImGui::PopStyleColor();
                for (const LsaResidual& v : res.residuals) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%d", v.fromStationId);
                    ImGui::TableNextColumn(); ImGui::Text("%d", v.toStationId);
                    ImGui::TableNextColumn(); ImGui::Text("%+.2f", v.angleResidualSec);
                    ImGui::TableNextColumn(); ImGui::Text("%+.4f", v.distResidualFt);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace


void DrawTraverseEditorPanel(AppCommandState& cmd, std::vector<std::string>& log) {
    if (!cmd.showTraverseEditorWindow)
        return;

    // Recompute when inputs change.
    if (cmd.traverseDataDirty) {
        ComputeTraverse(cmd.traverseData);
        cmd.traverseDataDirty = false;
        cmd.traverseLsaComputed = false;  // inputs changed -> adjustment is stale.
    }

    // The closure window is launched from this editor; draw it here so it
    // survives the editor being collapsed.
    DrawTraverseClosureWindow(cmd, log);

    ImGui::SetNextWindowSize(ImVec2(1080, 580), ImGuiCond_FirstUseEver);
    bool open = cmd.showTraverseEditorWindow;
    if (!ImGui::Begin("Traverse Editor", &open)) {
        cmd.showTraverseEditorWindow = open;
        ImGui::End();
        return;
    }
    cmd.showTraverseEditorWindow = open;

    TraverseData& td = cmd.traverseData;

    // ---- Raw data import ----
    if (ImGui::Button("Import .fbk\xe2\x80\xa6")) {
        char path[1024]{};
        if (BrowseOpenFileFbkUtf8(path, sizeof(path))) {
            TraverseData imported;
            std::string err;
            if (FbkImport(path, imported, err)) {
                cmd.traverseData = std::move(imported);
                cmd.traverseDataDirty = true;
                cmd.traverseExpandedLeg = -1;  // selection from a prior traverse is meaningless now.
                log.push_back(std::string("TRAVERSE — imported FBK (") +
                              std::to_string(cmd.traverseData.legs.size()) + " legs): " + path);
            } else {
                log.push_back("TRAVERSE — FBK import failed: " + err);
            }
        }
    }
    ItemHelpTooltip(
        "Import an Autodesk Field Book (.fbk) raw data file.\n"
        "Replaces the current traverse with the imported stations, backsight\n"
        "orientation, and face-averaged observations.");

    // ---- Starting station header ----
    ImGui::SeparatorText("Starting Station");

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
        "This is the azimuth of the backsight direction (or any reference mark)\n"
        "that the instrument was zeroed on. The forward bearing of the first leg\n"
        "= this value + the first row's H.Angle.\n"
        "If you know the first leg's bearing directly, enter it here and set\n"
        "the first row's H.Angle to 0.");

    // ---- Options ----
    ImGui::SameLine(0, 24);
    CheckboxCol("Closed Loop##trv", &td.isClosedLoop,
                "Report closure error back to the starting station.");

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
        const float startE = static_cast<float>(td.startEasting  - cmd.worldDocumentOriginX);
        const float startN = static_cast<float>(td.startNorthing - cmd.worldDocumentOriginY);
        cmd.createPointsOpts.startNumber = td.startStationId;
        cmd.createPointsOpts.sequentialNumbering = false;
        cmd.createPointsOpts.duplicatePolicy = SurveyDuplicatePolicy::Renumber;
        TryPlaceSurveyPoint(cmd, startE, startN, static_cast<float>(td.startElevation), log);

        // Place foresight points
        for (const auto& leg : td.legs) {
            if (!leg.computed)
                break;
            const float locE = static_cast<float>(leg.computedEasting  - cmd.worldDocumentOriginX);
            const float locN = static_cast<float>(leg.computedNorthing - cmd.worldDocumentOriginY);
            cmd.createPointsOpts.startNumber = leg.stationId;
            TryPlaceSurveyPoint(cmd, locE, locN, static_cast<float>(leg.computedElevation), log);
        }

        log.push_back("TRAVERSE — committed points to drawing.");
    }
    ItemHelpTooltip("Place all computed traverse stations as survey points in the drawing.\nUse View Points to review or edit them after committing.");

    ImGui::End();
}
