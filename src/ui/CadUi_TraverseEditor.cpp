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

static void ParseFace1Horiz(TraverseLeg& leg) {
    leg.hasFace1 = false;
    if (!leg.face1HorizBuf.empty()) {
        double v;
        if (TraverseParseAngle(leg.face1HorizBuf, &v)) {
            leg.face1HorizDeg = v;
            leg.hasFace1 = true;
        }
    }
}

static void ParseFace2Horiz(TraverseLeg& leg) {
    leg.hasFace2 = false;
    if (!leg.face2HorizBuf.empty()) {
        double v;
        if (TraverseParseAngle(leg.face2HorizBuf, &v)) {
            leg.face2HorizDeg = v;
            leg.hasFace2 = true;
        }
    }
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

static void ParseFace1Vert(TraverseLeg& leg) {
    if (!leg.face1VertBuf.empty()) {
        double v;
        if (TraverseParseAngle(leg.face1VertBuf, &v))
            leg.face1VertDeg = v;
    }
}

static void ParseFace2Vert(TraverseLeg& leg) {
    if (!leg.face2VertBuf.empty()) {
        double v;
        if (TraverseParseAngle(leg.face2VertBuf, &v))
            leg.face2VertDeg = v;
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

// Draw one computed value cell (read-only, grayed if not computed).
static void ReadOnlyCell(const char* label, bool valid, double val, const char* fmt = "%.4f") {
    if (valid) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), fmt, val);
        ImGui::TextUnformatted(buf);
    } else {
        ImGui::TextDisabled("—");
    }
    (void)label;
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

// ---- Raw measurements & per-leg statistics (REQ-010, REQ-011, REQ-012) ----
// View-only (REQ-013): nothing here is an editable control.
static void DrawRawMeasurements(TraverseData& td) {
    for (size_t i = 0; i < td.legs.size(); ++i) {
        TraverseLeg& leg = td.legs[i];
        if (leg.rawSets.empty())
            continue;

        ImGui::PushID(static_cast<int>(i));
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "Leg %zu  \xe2\x86\x92  Station %d  (%zu set%s)",
                      i + 1, leg.stationId, leg.rawSets.size(),
                      leg.rawSets.size() == 1 ? "" : "s");
        if (ImGui::TreeNode(hdr)) {
            const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_SizingFixedFit;
            if (ImGui::BeginTable("rawsets", 7, tf)) {
                ImGui::TableSetupColumn("Set");
                ImGui::TableSetupColumn("F1 Hz\xc2\xb0");
                ImGui::TableSetupColumn("F1 SD");
                ImGui::TableSetupColumn("F1 VA\xc2\xb0");
                ImGui::TableSetupColumn("F2 Hz\xc2\xb0");
                ImGui::TableSetupColumn("F2 SD");
                ImGui::TableSetupColumn("F2 VA\xc2\xb0");
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                ImGui::TableHeadersRow();
                ImGui::PopStyleColor();

                std::vector<double> hz, dist, va;
                for (const TraverseMeasSet& s : leg.rawSets) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::Text("%d", s.setNo);
                    ImGui::TableNextColumn();
                    if (s.hasF1) ImGui::Text("%.5f", s.f1HzDec); else ImGui::TextDisabled("\xe2\x80\x94");
                    ImGui::TableNextColumn();
                    if (s.hasF1) ImGui::Text("%.4f", s.f1Sd); else ImGui::TextDisabled("\xe2\x80\x94");
                    ImGui::TableNextColumn();
                    if (s.hasF1) ImGui::Text("%.5f", s.f1VaDec); else ImGui::TextDisabled("\xe2\x80\x94");
                    ImGui::TableNextColumn();
                    if (s.hasF2) ImGui::Text("%.5f", s.f2HzDec); else ImGui::TextDisabled("\xe2\x80\x94");
                    ImGui::TableNextColumn();
                    if (s.hasF2) ImGui::Text("%.4f", s.f2Sd); else ImGui::TextDisabled("\xe2\x80\x94");
                    ImGui::TableNextColumn();
                    if (s.hasF2) ImGui::Text("%.5f", s.f2VaDec); else ImGui::TextDisabled("\xe2\x80\x94");

                    hz.push_back(SetReducedHoriz(s));
                    dist.push_back(SetReducedDist(s));
                    va.push_back(SetReducedZenith(s));
                }
                ImGui::EndTable();

                // Statistics over the reduced per-set values (REQ-011).
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

                // Complementary distance (REQ-012): show both slope and horizontal.
                const double vaDeg = sv.n > 0 ? sv.mean
                                   : (leg.hasVertAngle ? leg.vertAngleDeg : 90.0);
                double horiz = leg.computed ? leg.computedHorizDist
                             : (leg.hasHorizDist ? leg.horizDist
                                                 : TraverseReduceToHoriz(leg.slopeDist, vaDeg, true));
                double slope = leg.hasSlopeDist ? leg.slopeDist
                                                : TraverseSlopeFromHoriz(horiz, vaDeg, true);
                ImGui::Text("Horizontal distance: %.4f    Slope distance: %.4f    (zenith %.5f\xc2\xb0)",
                            horiz, slope, vaDeg);

                ImGui::TreePop();
            }
        }
        ImGui::PopID();
    }
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
    ImGui::SameLine(0, 16);
    CheckboxCol("Face 1 / Face 2##trv", &td.useFace1Face2,
                "Show Face 1 and Face 2 columns. Angles are averaged before computation.\n"
                "Horizontal: F2 is treated as F1 + 180° (handles wrap).\n"
                "Zenith: mean = (F1 + (360° - F2)) / 2.");

    ImGui::SeparatorText("Legs");

    // ---- Table ----
    const int baseCols = 16; // always-visible columns
    const int f12Cols  = 4;  // extra when Face 1/2 mode on
    const int numCols  = td.useFace1Face2 ? baseCols + f12Cols : baseCols;

    const ImGuiTableFlags tf =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

    const float footerH = ImGui::GetFrameHeightWithSpacing() * 4.f + ImGui::GetStyle().ItemSpacing.y;

    if (ImGui::BeginTable("traverse_legs", numCols, tf, ImVec2(0.f, -footerH))) {
        ImGui::TableSetupScrollFreeze(0, 1);

        // Always-visible columns
        ImGui::TableSetupColumn("#",         ImGuiTableColumnFlags_WidthFixed,   28.f);
        ImGui::TableSetupColumn("Stn ID",    ImGuiTableColumnFlags_WidthFixed,   55.f);
        ImGui::TableSetupColumn("Desc",      ImGuiTableColumnFlags_WidthStretch, 90.f);
        ImGui::TableSetupColumn("H.Angle\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("H.Dist",    ImGuiTableColumnFlags_WidthFixed,   80.f);
        ImGui::TableSetupColumn("S.Dist",    ImGuiTableColumnFlags_WidthFixed,   80.f);
        ImGui::TableSetupColumn("V.Angle\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("Z",         ImGuiTableColumnFlags_WidthFixed,   22.f); // zenith checkbox
        // Computed
        ImGui::TableSetupColumn("Bearing\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("\xce\x94""E",       ImGuiTableColumnFlags_WidthFixed,   82.f);
        ImGui::TableSetupColumn("\xce\x94""N",       ImGuiTableColumnFlags_WidthFixed,   82.f);
        ImGui::TableSetupColumn("\xce\x94""Z",       ImGuiTableColumnFlags_WidthFixed,   72.f);
        ImGui::TableSetupColumn("Easting",   ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("Northing",  ImGuiTableColumnFlags_WidthFixed,   92.f);
        ImGui::TableSetupColumn("Elev",      ImGuiTableColumnFlags_WidthFixed,   80.f);
        ImGui::TableSetupColumn("Status",    ImGuiTableColumnFlags_WidthFixed,   56.f);

        // Face 1/2 columns
        if (td.useFace1Face2) {
            ImGui::TableSetupColumn("F1 H\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("F2 H\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("F1 V\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed, 92.f);
            ImGui::TableSetupColumn("F2 V\xc2\xb0",  ImGuiTableColumnFlags_WidthFixed, 92.f);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();

        for (size_t i = 0; i < td.legs.size(); ++i) {
            TraverseLeg& leg = td.legs[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            // # row number
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
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

            // Computed: Bearing
            ImGui::TableNextColumn();
            if (leg.computed)
                ImGui::TextUnformatted(TraverseFormatBearing(leg.computedBearingDeg).c_str());
            else
                ImGui::TextDisabled("\xe2\x80\x94");

            // ΔE
            ImGui::TableNextColumn();
            ReadOnlyCell("dE", leg.computed, leg.computedDeltaE);

            // ΔN
            ImGui::TableNextColumn();
            ReadOnlyCell("dN", leg.computed, leg.computedDeltaN);

            // ΔZ
            ImGui::TableNextColumn();
            ReadOnlyCell("dZ", leg.computed, leg.computedDeltaZ);

            // Easting
            ImGui::TableNextColumn();
            ReadOnlyCell("E", leg.computed, leg.computedEasting);

            // Northing
            ImGui::TableNextColumn();
            ReadOnlyCell("N", leg.computed, leg.computedNorthing);

            // Elevation
            ImGui::TableNextColumn();
            ReadOnlyCell("Z", leg.computed, leg.computedElevation);

            // Status
            ImGui::TableNextColumn();
            if (leg.hasSufficientData) {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.35f, 1.f), "\xe2\x9c\x93"); // ✓
            } else if (!leg.errorMsg.empty()) {
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), "\xe2\x9c\x97");    // ✗
                ItemHelpTooltip(leg.errorMsg.c_str());
            } else {
                ImGui::TextDisabled("\xe2\x80\x94");
            }

            // Face 1/2 columns (optional)
            if (td.useFace1Face2) {
                // F1 H.Angle
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##f1h", &leg.face1HorizBuf);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ParseFace1Horiz(leg);
                    cmd.traverseDataDirty = true;
                }

                // F2 H.Angle
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##f2h", &leg.face2HorizBuf);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ParseFace2Horiz(leg);
                    cmd.traverseDataDirty = true;
                }

                // F1 V.Angle
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##f1v", &leg.face1VertBuf);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ParseFace1Vert(leg);
                    cmd.traverseDataDirty = true;
                }

                // F2 V.Angle
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("##f2v", &leg.face2VertBuf);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ParseFace2Vert(leg);
                    cmd.traverseDataDirty = true;
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    // ---- Leg buttons ----
    if (ImGui::Button("+ Add Leg")) {
        TraverseLeg newLeg;
        if (!td.legs.empty())
            newLeg.stationId = td.legs.back().stationId + 1;
        else
            newLeg.stationId = td.startStationId + 1;
        newLeg.isZenithAngle = true;
        td.legs.push_back(std::move(newLeg));
        cmd.traverseDataDirty = true;
    }
    ImGui::SameLine();
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

    // ---- Raw measurements & statistics (REQ-010, 011, 012) ----
    bool anyRaw = false;
    for (const auto& leg : td.legs)
        if (!leg.rawSets.empty()) { anyRaw = true; break; }
    if (anyRaw) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Raw Measurements & Statistics"))
            DrawRawMeasurements(td);
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
