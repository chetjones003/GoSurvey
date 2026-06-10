#include "CadUi.hpp"
#include "CadUiHelpers.hpp"
#include "CadCommands.hpp"
#include "SurveyPoints.hpp"
#include "traverse/TraverseCalc.hpp"
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
    if (!leg.face1HorizBuf.empty()) {
        double v;
        if (TraverseParseAngle(leg.face1HorizBuf, &v))
            leg.face1HorizDeg = v;
    }
}

static void ParseFace2Horiz(TraverseLeg& leg) {
    if (!leg.face2HorizBuf.empty()) {
        double v;
        if (TraverseParseAngle(leg.face2HorizBuf, &v))
            leg.face2HorizDeg = v;
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

} // namespace


void DrawTraverseEditorPanel(AppCommandState& cmd, std::vector<std::string>& log) {
    if (!cmd.showTraverseEditorWindow)
        return;

    // Recompute when inputs change.
    if (cmd.traverseDataDirty) {
        ComputeTraverse(cmd.traverseData);
        cmd.traverseDataDirty = false;
    }

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

    const float fieldW = 130.f;

    ImGui::SetNextItemWidth(80.f);
    if (ImGui::InputInt("Start ID##trv", &td.startStationId))
        cmd.traverseDataDirty = true;
    ImGui::SameLine(0, 12);
    ImGui::SetNextItemWidth(fieldW);
    if (ImGui::InputDouble("Easting##trv_e", &td.startEasting, 0., 0., "%.4f"))
        cmd.traverseDataDirty = true;
    ImGui::SameLine(0, 12);
    ImGui::SetNextItemWidth(fieldW);
    if (ImGui::InputDouble("Northing##trv_n", &td.startNorthing, 0., 0., "%.4f"))
        cmd.traverseDataDirty = true;
    ImGui::SameLine(0, 12);
    ImGui::SetNextItemWidth(fieldW);
    if (ImGui::InputDouble("Elevation##trv_z", &td.startElevation, 0., 0., "%.4f"))
        cmd.traverseDataDirty = true;
    ImGui::SameLine(0, 12);
    ImGui::SetNextItemWidth(120.f);
    if (ImGui::InputText("Ref Bearing°##trv_sb", &td.startBearingBuf)) {
        // Live parse
        ParseStartBearing(td);
        cmd.traverseDataDirty = true;
    }
    ItemHelpTooltip(
        "Reference orientation at the start station (° CW from N).\n"
        "This is the azimuth of the backsight direction (or any reference mark)\n"
        "that the instrument was zeroed on. The forward bearing of the first leg\n"
        "= this value + the first row's H.Angle.\n"
        "If you know the first leg's bearing directly, enter it here and set\n"
        "the first row's H.Angle to 0.");

    // ---- Options ----
    ImGui::SameLine(0, 20);
    if (ImGui::Checkbox("Closed Loop##trv", &td.isClosedLoop))
        cmd.traverseDataDirty = true;
    ItemHelpTooltip("Report closure error back to the starting station.");
    ImGui::SameLine(0, 16);
    if (ImGui::Checkbox("Face 1 / Face 2##trv", &td.useFace1Face2))
        cmd.traverseDataDirty = true;
    ItemHelpTooltip(
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
