#include "CadUi.hpp"
#include "CadBlocks.hpp"
#include "NumFormat.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

void DrawInsertBlockDialog(AppCommandState& cmd, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using Ph = AppCommandState::InsertBlockPhase;
  if (cmd.active != K::InsertBlock)
    return;

  if (!cmd.insertBlockDialogOpen) {
    const char* hint = nullptr;
    if (cmd.insertBlockPhase == Ph::WaitInsertPoint)
      hint = "INSERT — click the insertion point (ESC cancels).";
    else if (cmd.insertBlockPhase == Ph::WaitScale)
      hint = "INSERT — click to set scale (ESC cancels).";
    else if (cmd.insertBlockPhase == Ph::WaitRotation)
      hint = "Specify rotation angle — type degrees (matchline default 90) or click:";
    if (cmd.insertBlockAttrDialogOpen) {
      const int di = CadBlockFindDef(cmd.blockDefs, cmd.insertBlockName);
      ImGui::OpenPopup("Edit Attributes");
      const ImVec2 ds = ImGui::GetIO().DisplaySize;
      ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      bool attrOpen = true;
      if (ImGui::BeginPopupModal("Edit Attributes", &attrOpen,
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
        ImGui::Text("Block name: %s", cmd.insertBlockName);
        ImGui::Spacing();
        if (di >= 0) {
          const CadBlockDefinition& def = cmd.blockDefs[static_cast<size_t>(di)];
          const int n = std::min(8, static_cast<int>(def.attrDefs.size()));
          for (int i = 0; i < n; ++i) {
            const CadBlockAttrDef& ad = def.attrDefs[static_cast<size_t>(i)];
            const char* label = ad.prompt.empty() ? ad.tag.c_str() : ad.prompt.c_str();
            ImGui::SetNextItemWidth(220.f);
            ImGui::InputText(label, cmd.insertBlockAttrBuf[i], sizeof(cmd.insertBlockAttrBuf[i]));
          }
        }
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(90.f, 0.f)))
          CadBlocksCommitInsertAttrDialog(cmd, log);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)))
          CadBlocksCommitInsertAttrDialog(cmd, log);
        ImGui::EndPopup();
      } else if (!attrOpen) {
        CadBlocksCommitInsertAttrDialog(cmd, log);
      }
      return;
    }
    if (hint) {
      ImGui::SetNextWindowPos(ImVec2(10.f, ImGui::GetIO().DisplaySize.y - 60.f), ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.75f);
      ImGui::Begin("##InsertPickHint", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_NoMove);
      ImGui::TextUnformatted(hint);
      ImGui::End();
    }
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(420.f, 0.f), ImGuiCond_FirstUseEver);
  bool open = true;
  if (!ImGui::Begin("Insert", &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    if (!open)
      CancelActiveCommand(cmd, log);
    return;
  }
  if (!open) {
    ImGui::End();
    CancelActiveCommand(cmd, log);
    return;
  }

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Name:");
  ImGui::SameLine(90.f);
  ImGui::SetNextItemWidth(220.f);
  if (ImGui::BeginCombo("##InsertName", cmd.insertBlockName[0] ? cmd.insertBlockName : "(none)")) {
    for (const CadBlockDefinition& d : cmd.blockDefs) {
      const bool sel = std::strcmp(cmd.insertBlockName, d.name.c_str()) == 0;
      if (ImGui::Selectable(d.name.c_str(), sel)) {
        std::snprintf(cmd.insertBlockName, sizeof(cmd.insertBlockName), "%s", d.name.c_str());
        CadBlocksApplyInsertNameDefaults(cmd);
      }
      if (sel)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Browse...")) {
    const int nBefore = static_cast<int>(cmd.blockDefs.size());
    if (CadBlocksImportWithPicker(cmd, log) && static_cast<int>(cmd.blockDefs.size()) > nBefore) {
      const CadBlockDefinition& d = cmd.blockDefs.back();
      std::snprintf(cmd.insertBlockName, sizeof(cmd.insertBlockName), "%s", d.name.c_str());
      CadBlocksApplyInsertNameDefaults(cmd);
    }
  }

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Path:");
  ImGui::SameLine(90.f);
  ImGui::SetNextItemWidth(300.f);
  ImGui::BeginDisabled();
  ImGui::InputText("##InsertPath", cmd.insertBlockPath, sizeof(cmd.insertBlockPath));
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);

  ImGui::TextUnformatted("Insertion point");
  ImGui::Checkbox("Specify On-screen##InsPt", &cmd.insertBlockSpecifyPoint);
  ImGui::BeginDisabled(cmd.insertBlockSpecifyPoint);
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputFloat("X##InsX", &cmd.insertBlockX, 0.f, 0.f, cfmt.c_str());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputFloat("Y##InsY", &cmd.insertBlockY, 0.f, 0.f, cfmt.c_str());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputFloat("Z##InsZ", &cmd.insertBlockZ, 0.f, 0.f, cfmt.c_str());
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::TextUnformatted("Scale");
  ImGui::Checkbox("Specify On-screen##InsSc", &cmd.insertBlockSpecifyScale);
  ImGui::SameLine();
  if (ImGui::Checkbox("Uniform Scale", &cmd.insertBlockUniformScale) && cmd.insertBlockUniformScale) {
    cmd.insertBlockSy = cmd.insertBlockSx;
    cmd.insertBlockSz = cmd.insertBlockSx;
  }
  ImGui::BeginDisabled(cmd.insertBlockSpecifyScale);
  ImGui::SetNextItemWidth(90.f);
  if (ImGui::InputFloat("X##InsSx", &cmd.insertBlockSx, 0.f, 0.f, "%.4f") && cmd.insertBlockUniformScale) {
    cmd.insertBlockSy = cmd.insertBlockSx;
    cmd.insertBlockSz = cmd.insertBlockSx;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.f);
  ImGui::BeginDisabled(cmd.insertBlockUniformScale);
  ImGui::InputFloat("Y##InsSy", &cmd.insertBlockSy, 0.f, 0.f, "%.4f");
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.f);
  ImGui::BeginDisabled(cmd.insertBlockUniformScale);
  ImGui::InputFloat("Z##InsSz", &cmd.insertBlockSz, 0.f, 0.f, "%.4f");
  ImGui::EndDisabled();
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::TextUnformatted("Rotation");
  ImGui::Checkbox("Specify On-screen##InsRot", &cmd.insertBlockSpecifyRot);
  ImGui::BeginDisabled(cmd.insertBlockSpecifyRot);
  ImGui::SetNextItemWidth(160.f);
  ImGui::InputText("Angle##InsAng", cmd.insertBlockAngleBuf, sizeof(cmd.insertBlockAngleBuf));
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::TextUnformatted("Block Unit");
  const int di = CadBlockFindDef(cmd.blockDefs, cmd.insertBlockName);
  std::string unitName = "(none)";
  float factor = 1.f;
  if (di >= 0) {
    unitName = cmd.blockDefs[static_cast<size_t>(di)].units;
    if (unitName.empty())
      unitName = CadDrawingInsUnitsName(cmd.drawingInsUnits);
    factor = CadBlockUnitsScale(cmd.blockDefs[static_cast<size_t>(di)].units,
                                CadDrawingInsUnitsName(cmd.drawingInsUnits));
  }
  ImGui::Text("Unit:  %s", unitName.c_str());
  ImGui::Text("Factor:  %.4f", static_cast<double>(factor));

  ImGui::Spacing();
  ImGui::Checkbox("Explode", &cmd.insertBlockExplode);

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  const bool canOk = cmd.insertBlockName[0] != '\0' && di >= 0;
  ImGui::BeginDisabled(!canOk);
  if (ImGui::Button("OK", ImVec2(90.f, 0.f)))
    CadBlocksCommitInsertDialog(cmd, log);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)))
    CancelActiveCommand(cmd, log);

  ImGui::End();
}

void DrawEditBlockDefinitionDialog(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.blockEditPickerOpen)
    return;
  if (!ImGui::IsPopupOpen("Edit Block Definition"))
    ImGui::OpenPopup("Edit Block Definition");
  const ImVec2 ds = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560.f, 420.f), ImGuiCond_Appearing);
  bool open = true;
  if (!ImGui::BeginPopupModal("Edit Block Definition", &open, ImGuiWindowFlags_NoDocking)) {
    if (!open)
      cmd.blockEditPickerOpen = false;
    return;
  }
  if (!open) {
    cmd.blockEditPickerOpen = false;
    ImGui::EndPopup();
    return;
  }

  std::vector<std::string> names;
  CadBlocksCollectEditPickerNames(cmd, &names);

  ImGui::TextUnformatted("Block to create or edit");
  ImGui::SetNextItemWidth(-1.f);
  ImGui::InputText("##BeditName", cmd.blockEditPickerName, static_cast<int>(sizeof(cmd.blockEditPickerName)));

  ImGui::BeginChild("##BeditCols", ImVec2(0.f, -36.f), false);
  ImGui::BeginChild("##BeditList", ImVec2(ImGui::GetContentRegionAvail().x * 0.52f, 0.f), true);
  for (const std::string& nm : names) {
    const bool sel = CadBlockEqCi(nm, cmd.blockEditPickerName);
    if (ImGui::Selectable(nm.c_str(), sel))
      std::snprintf(cmd.blockEditPickerName, sizeof(cmd.blockEditPickerName), "%s", nm.c_str());
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
        !CadBlockEqCi(nm, "<Current Drawing>")) {
      std::snprintf(cmd.blockEditPickerName, sizeof(cmd.blockEditPickerName), "%s", nm.c_str());
      CadBlocksCommitEditPicker(cmd, log);
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##BeditRight", ImVec2(0.f, 0.f), false);
  ImGui::TextUnformatted("Preview");
  ImGui::BeginChild("##BeditPrev", ImVec2(0.f, 160.f), true);
  {
    const int di = CadBlockFindDef(cmd.blockDefs, cmd.blockEditPickerName);
    if (di >= 0) {
      CadBlockRef r;
      r.defName = cmd.blockDefs[static_cast<size_t>(di)].name;
      std::vector<CadBlockWorldSeg> segs;
      CadBlockCollectWorldLines(cmd.blockDefs, r, EntityAttributes{}, &segs);
      float minX = 1.e9f, minY = 1.e9f, maxX = -1.e9f, maxY = -1.e9f;
      for (const CadBlockWorldSeg& s : segs) {
        minX = std::min(minX, std::min(s.x0, s.x1));
        minY = std::min(minY, std::min(s.y0, s.y1));
        maxX = std::max(maxX, std::max(s.x0, s.x1));
        maxY = std::max(maxY, std::max(s.y0, s.y1));
      }
      const ImVec2 a = ImGui::GetCursorScreenPos();
      const ImVec2 sz = ImGui::GetContentRegionAvail();
      if (!segs.empty() && maxX > minX && maxY > minY && sz.x > 8.f && sz.y > 8.f) {
        const float pad = 12.f;
        const float sx = (sz.x - pad * 2.f) / (maxX - minX);
        const float sy = (sz.y - pad * 2.f) / (maxY - minY);
        const float s = std::min(sx, sy);
        auto toPx = [&](float x, float y) {
          return ImVec2(a.x + pad + (x - minX) * s, a.y + sz.y - pad - (y - minY) * s);
        };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const CadBlockWorldSeg& seg : segs)
          dl->AddLine(toPx(seg.x0, seg.y0), toPx(seg.x1, seg.y1), IM_COL32(30, 30, 30, 255), 1.f);
      }
    }
  }
  ImGui::EndChild();
  ImGui::TextUnformatted("Description");
  ImGui::BeginChild("##BeditDesc", ImVec2(0.f, 0.f), true);
  {
    const int di = CadBlockFindDef(cmd.blockDefs, cmd.blockEditPickerName);
    if (di >= 0) {
      const std::string& d = cmd.blockDefs[static_cast<size_t>(di)].description;
      if (!d.empty())
        ImGui::TextWrapped("%s", d.c_str());
    }
  }
  ImGui::EndChild();
  ImGui::EndChild();
  ImGui::EndChild();

  const bool canOk =
      cmd.blockEditPickerName[0] != '\0' && !CadBlockEqCi(cmd.blockEditPickerName, "<Current Drawing>");
  ImGui::BeginDisabled(!canOk);
  if (ImGui::Button("OK", ImVec2(80.f, 0.f)))
    CadBlocksCommitEditPicker(cmd, log);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(80.f, 0.f))) {
    cmd.blockEditPickerOpen = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Help", ImVec2(80.f, 0.f)))
    log.push_back("BEDIT — pick a drawing or library block, or type a new name, then OK.");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Choose a definition from the current drawing or the block library.");

  if (!cmd.blockEditPickerOpen)
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}
