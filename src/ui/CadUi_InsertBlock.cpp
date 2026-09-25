#include "CadUi.hpp"
#include "CadBlocks.hpp"
#include "CadRubberPreview.hpp"
#include "NumFormat.hpp"
#include "StringUtil.hpp"
#include "platform/WinFileDialogs.hpp"
#include "util/brep.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

void DrawInsertLibraryPreview(const AppCommandState& cmd, std::string_view blockName) {
  const int di = CadBlockFindDef(cmd.blockDefs, blockName);
  if (di < 0)
    return;
  CadBlockRef r;
  r.defName = cmd.blockDefs[static_cast<size_t>(di)].name;
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(cmd.blockDefs, r, EntityAttributes{}, &segs);
  std::vector<CadBlockWorldSolid> ws;
  CadBlockCollectWorldSolids(cmd.blockDefs, r, EntityAttributes{}, &ws);
  brep::Problem why = brep::Problem::Ok;
  for (const CadBlockWorldSolid& w : ws) {
    if (!w.solid)
      continue;
    std::vector<double> edges;
    if (!brep::TessellateEdges(*w.solid, kSolidChordToleranceFt, &edges, &why))
      continue;
    for (std::size_t i = 0; i + 5 < edges.size(); i += 6)
      segs.push_back(CadBlockWorldSeg{static_cast<float>(edges[i]), static_cast<float>(edges[i + 1]),
                                      static_cast<float>(edges[i + 2]), static_cast<float>(edges[i + 3]),
                                      static_cast<float>(edges[i + 4]), static_cast<float>(edges[i + 5]),
                                      EntityAttributes{}});
  }
  float minX = 1.e9f;
  float minY = 1.e9f;
  float maxX = -1.e9f;
  float maxY = -1.e9f;
  for (const CadBlockWorldSeg& s : segs) {
    minX = std::min(minX, std::min(s.x0, s.x1));
    minY = std::min(minY, std::min(s.y0, s.y1));
    maxX = std::max(maxX, std::max(s.x0, s.x1));
    maxY = std::max(maxY, std::max(s.y0, s.y1));
  }
  const ImVec2 a = ImGui::GetCursorScreenPos();
  const ImVec2 sz = ImGui::GetContentRegionAvail();
  if (segs.empty() || maxX <= minX || maxY <= minY || sz.x <= 8.f || sz.y <= 8.f)
    return;
  const float pad = 8.f;
  const float sx = (sz.x - pad * 2.f) / (maxX - minX);
  const float sy = (sz.y - pad * 2.f) / (maxY - minY);
  const float sc = std::min(sx, sy);
  auto toPx = [&](float x, float y) {
    return ImVec2(a.x + pad + (x - minX) * sc, a.y + sz.y - pad - (y - minY) * sc);
  };
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (const CadBlockWorldSeg& seg : segs)
    dl->AddLine(toPx(seg.x0, seg.y0), toPx(seg.x1, seg.y1), IM_COL32(40, 40, 40, 255), 1.f);
  // Connection ports (issue #486 increment A5): same role coloring as the BEDIT gizmo, so the
  // library preview shows where — and how — a fitting mates before it's placed.
  const CadBlockDefinition& def = cmd.blockDefs[static_cast<size_t>(di)];
  for (const CadBlockConnection& c : def.connections) {
    if (c.x < minX || c.x > maxX || c.y < minY || c.y > maxY)
      continue;
    ImU32 col = IM_COL32(90, 220, 120, 255);
    if (c.role == CadBlockConnectionRole::Outlet)
      col = IM_COL32(90, 160, 240, 255);
    else if (c.role == CadBlockConnectionRole::Branch)
      col = IM_COL32(240, 160, 60, 255);
    dl->AddCircleFilled(toPx(c.x, c.y), 3.5f, col);
  }
}

/// True when \p entry passes the library pane's part-type/pressure-class/size filters (issue
/// #486 increment A5). A part-tagged-`None` (ordinary, non-fitting) entry always passes, so
/// filters only ever narrow the fittings, never hide the rest of the library.
[[nodiscard]] bool CadBlockLibraryEntryPassesFilter(const AppCommandState& cmd, const CadBlockLibraryEntry& entry) {
  if (!entry.isFitting)
    return true;
  if (cmd.insertLibFilterPartType != CadPipePartType::None && entry.partType != cmd.insertLibFilterPartType)
    return false;
  if (cmd.insertLibFilterPressureClass != CadPipePressureClass::None &&
      entry.pressureClass != cmd.insertLibFilterPressureClass)
    return false;
  const std::string sizeQuery = StringUtil::trimCopy(std::string(cmd.insertLibFilterSizeBuf));
  if (!sizeQuery.empty() &&
      StringUtil::toLowerAsciiCopy(entry.nominalSize).find(StringUtil::toLowerAsciiCopy(sizeQuery)) ==
          std::string::npos)
    return false;
  return true;
}

} // namespace

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
    else if (cmd.insertBlockPhase == Ph::WaitAlignFace)
      hint = "INSERT — pick a flat face to align the fitting (ESC cancels).";
    else if (cmd.insertBlockPhase == Ph::WaitConnectorTarget)
      hint = "INSERT — click near a target connection port (ESC cancels).";
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

  ImGui::SetNextWindowSize(ImVec2(560.f, 520.f), ImGuiCond_FirstUseEver);
  bool open = true;
  if (!ImGui::Begin("Insert", &open, ImGuiWindowFlags_NoDocking)) {
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

  // Library filters (issue #486 increment A5): part type / pressure class / nominal size. Only
  // fitting entries are ever filtered out — ordinary blocks and drawing defs always show.
  ImGui::TextUnformatted("Filter:");
  ImGui::SameLine();
  static const char* kPartTypeFilterChoices[] = {"(any part)",  "elbow-90", "elbow-45", "tee",   "cross",
                                                  "reducer",     "flange",   "valve",    "coupling", "cap", "other",
                                                  "nozzle"};
  static const CadPipePartType kPartTypeFilterValues[] = {
      CadPipePartType::None,  CadPipePartType::Elbow90, CadPipePartType::Elbow45, CadPipePartType::Tee,
      CadPipePartType::Cross, CadPipePartType::Reducer, CadPipePartType::Flange,  CadPipePartType::Valve,
      CadPipePartType::Coupling, CadPipePartType::Cap,  CadPipePartType::Other,   CadPipePartType::Nozzle};
  int partTypeIdx = 0;
  for (int i = 0; i < static_cast<int>(std::size(kPartTypeFilterValues)); ++i) {
    if (kPartTypeFilterValues[i] == cmd.insertLibFilterPartType) {
      partTypeIdx = i;
      break;
    }
  }
  ImGui::SetNextItemWidth(120.f);
  if (ImGui::BeginCombo("##InsLibFilterPart", kPartTypeFilterChoices[partTypeIdx])) {
    for (int i = 0; i < static_cast<int>(std::size(kPartTypeFilterValues)); ++i) {
      if (ImGui::Selectable(kPartTypeFilterChoices[i], partTypeIdx == i))
        cmd.insertLibFilterPartType = kPartTypeFilterValues[i];
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  static const char* kClassFilterChoices[] = {"(any class)", "CS150", "CS300"};
  static const CadPipePressureClass kClassFilterValues[] = {CadPipePressureClass::None, CadPipePressureClass::CS150,
                                                             CadPipePressureClass::CS300};
  int classIdx = 0;
  for (int i = 0; i < static_cast<int>(std::size(kClassFilterValues)); ++i) {
    if (kClassFilterValues[i] == cmd.insertLibFilterPressureClass) {
      classIdx = i;
      break;
    }
  }
  ImGui::SetNextItemWidth(100.f);
  if (ImGui::BeginCombo("##InsLibFilterClass", kClassFilterChoices[classIdx])) {
    for (int i = 0; i < static_cast<int>(std::size(kClassFilterValues)); ++i) {
      if (ImGui::Selectable(kClassFilterChoices[i], classIdx == i))
        cmd.insertLibFilterPressureClass = kClassFilterValues[i];
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80.f);
  ImGui::InputTextWithHint("##InsLibFilterSize", "size", cmd.insertLibFilterSizeBuf,
                           sizeof(cmd.insertLibFilterSizeBuf));

  ImGui::BeginChild("##InsertLibCols", ImVec2(0.f, 180.f), false);
  ImGui::BeginChild("##InsertLibList", ImVec2(ImGui::GetContentRegionAvail().x * 0.45f, 0.f), true);
  ImGui::TextUnformatted("Library");
  std::vector<CadBlockLibraryEntry> lib;
  CadBlocksCollectLibraryEntries(cmd, &lib);
  for (const CadBlockLibraryEntry& entry : lib) {
    if (!CadBlockLibraryEntryPassesFilter(cmd, entry))
      continue;
    const bool sel = CadBlockEqCi(entry.name, cmd.insertBlockName);
    std::string label = entry.name;
    if (entry.isFitting)
      label += "  (fitting)";
    else if (!entry.imported)
      label += "  (file)";
    if (ImGui::Selectable(label.c_str(), sel)) {
      if (!entry.imported)
        CadBlocksImportLibraryEntry(cmd, entry, log);
      std::snprintf(cmd.insertBlockName, sizeof(cmd.insertBlockName), "%s", entry.name.c_str());
      CadBlocksApplyInsertNameDefaults(cmd);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      if (!entry.imported)
        CadBlocksImportLibraryEntry(cmd, entry, log);
      std::snprintf(cmd.insertBlockName, sizeof(cmd.insertBlockName), "%s", entry.name.c_str());
      CadBlocksApplyInsertNameDefaults(cmd);
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##InsertLibPrev", ImVec2(0.f, 0.f), true);
  ImGui::TextUnformatted("Preview");
  DrawInsertLibraryPreview(cmd, cmd.insertBlockName);
  ImGui::EndChild();
  ImGui::EndChild();

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

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);

  ImGui::TextUnformatted("Insertion point");
  if (ImGui::Checkbox("Snap to connector##InsConn", &cmd.insertBlockSpecifyConnectorSnap) &&
      cmd.insertBlockSpecifyConnectorSnap) {
    cmd.insertBlockSpecifyPoint = false;
    cmd.insertBlockSpecifyAlignFace = false;
    cmd.insertBlockSpecifyRot = false;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Pick a connection port on a block already in the drawing. "
        "The fitting orients so its connection anti-aligns with the target.");
  ImGui::Checkbox("Specify On-screen##InsPt", &cmd.insertBlockSpecifyPoint);
  if (ImGui::IsItemActivated() && cmd.insertBlockSpecifyPoint)
    cmd.insertBlockSpecifyConnectorSnap = false;
  ImGui::BeginDisabled(cmd.insertBlockSpecifyPoint || cmd.insertBlockSpecifyConnectorSnap);
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
  ImGui::TextUnformatted("Orientation");
  if (ImGui::Checkbox("Align to face##InsAlign", &cmd.insertBlockSpecifyAlignFace) &&
      cmd.insertBlockSpecifyAlignFace)
    cmd.insertBlockSpecifyRot = false;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "After the insertion point, pick a flat solid face to orient the fitting. "
        "Turns off on-screen rotation — use the Angle field for plan rotation if needed.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Rotation");
  ImGui::Checkbox("Specify On-screen##InsRot", &cmd.insertBlockSpecifyRot);
  ImGui::BeginDisabled(cmd.insertBlockSpecifyRot || cmd.insertBlockSpecifyAlignFace);
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputText("Angle##InsAng", cmd.insertBlockAngleBuf, sizeof(cmd.insertBlockAngleBuf));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputText("X##InsRotX", cmd.insertBlockRotXBuf, sizeof(cmd.insertBlockRotXBuf));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputText("Y##InsRotY", cmd.insertBlockRotYBuf, sizeof(cmd.insertBlockRotYBuf));
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::TextUnformatted("Units");
  const int di = CadBlockFindDef(cmd.blockDefs, cmd.insertBlockName);
  const char* unitChoices[] = {"unitless", "inches", "feet", "meters", "millimeters"};
  int unitIdx = 0;
  if (cmd.insertBlockUnitsBuf[0] != '\0') {
    for (int i = 0; i < 5; ++i) {
      if (CadBlockEqCi(cmd.insertBlockUnitsBuf, unitChoices[i])) {
        unitIdx = i;
        break;
      }
    }
  }
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Block unit:");
  ImGui::SameLine(90.f);
  ImGui::SetNextItemWidth(140.f);
  if (ImGui::BeginCombo("##InsBlockUnit", unitChoices[unitIdx])) {
    for (int i = 0; i < 5; ++i) {
      if (ImGui::Selectable(unitChoices[i], unitIdx == i)) {
        std::snprintf(cmd.insertBlockUnitsBuf, sizeof(cmd.insertBlockUnitsBuf), "%s", unitChoices[i]);
      }
    }
    ImGui::EndCombo();
  }
  const std::string drawUnits = CadDrawingInsUnitsName(cmd.drawingInsUnits);
  float factor = 1.f;
  if (di >= 0)
    factor = CadBlockInsertUnitsScale(cmd, cmd.blockDefs[static_cast<size_t>(di)]);
  ImGui::Text("Drawing unit:  %s", drawUnits.c_str());
  ImGui::Text("Unit scale factor:  %.4f", static_cast<double>(factor));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Applied to scale on insert (multiplied with the Scale fields above).");

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

void DrawBlockCreateDialog(AppCommandState& cmd, std::vector<std::string>& log) {
  using Ph = AppCommandState::BlockCreatePhase;
  if (!cmd.blockCreateDialogOpen && cmd.blockCreatePhase != Ph::WaitBasePoint)
    return;

  if (!cmd.blockCreateDialogOpen && cmd.blockCreatePhase == Ph::WaitBasePoint) {
    ImGui::SetNextWindowPos(ImVec2(10.f, ImGui::GetIO().DisplaySize.y - 60.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##BlockCreatePickHint", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoMove);
    ImGui::TextUnformatted("BLOCK — pick base point (ESC cancels).");
    ImGui::End();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
      CancelBlockCreateDialog(cmd, log);
    return;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    CancelBlockCreateDialog(cmd, log);
    return;
  }
  ImGui::SetNextWindowSize(ImVec2(420.f, 0.f), ImGuiCond_FirstUseEver);
  bool open = true;
  if (!ImGui::Begin("Create Block", &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    if (!open)
      CancelBlockCreateDialog(cmd, log);
    return;
  }
  if (!open) {
    ImGui::End();
    CancelBlockCreateDialog(cmd, log);
    return;
  }

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Name:");
  ImGui::SameLine(90.f);
  ImGui::SetNextItemWidth(220.f);
  ImGui::InputText("##BlockCreateName", cmd.blockCreateName, sizeof(cmd.blockCreateName));
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Block definition name. Must be unique.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Description:");
  ImGui::SameLine(90.f);
  ImGui::SetNextItemWidth(220.f);
  ImGui::InputText("##BlockCreateDesc", cmd.blockCreateDescription, sizeof(cmd.blockCreateDescription));

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  const std::string cfmt = DisplayFloatFmt(cmd.displayLinearPrecision);

  ImGui::TextUnformatted("Base point");
  ImGui::Checkbox("Specify On-screen##BlkPt", &cmd.blockCreateSpecifyBase);
  ImGui::BeginDisabled(cmd.blockCreateSpecifyBase);
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputFloat("X##BlkX", &cmd.blockCreateBaseX, 0.f, 0.f, cfmt.c_str());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputFloat("Y##BlkY", &cmd.blockCreateBaseY, 0.f, 0.f, cfmt.c_str());
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.f);
  ImGui::InputFloat("Z##BlkZ", &cmd.blockCreateBaseZ, 0.f, 0.f, cfmt.c_str());
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::TextUnformatted("Objects");
  int selCount = static_cast<int>(cmd.selection.size());
  ImGui::Text("Selected: %d object(s)", selCount);
  if (selCount == 0)
    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.f), "No objects selected — select geometry before creating a block.");
  ImGui::TextUnformatted("Objects action:");
  ImGui::RadioButton("Retain##BlkRet", &cmd.blockCreateConvertMode, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Convert to block##BlkConv", &cmd.blockCreateConvertMode, 1);
  ImGui::SameLine();
  ImGui::RadioButton("Delete##BlkDel", &cmd.blockCreateConvertMode, 2);

  ImGui::Spacing();
  ImGui::TextUnformatted("Settings");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Block unit:");
  ImGui::SameLine(90.f);
  ImGui::SetNextItemWidth(160.f);
  ImGui::InputText("##BlkUnit", cmd.blockCreateUnits, sizeof(cmd.blockCreateUnits));
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Block insertion unit (AutoCAD INSUNITS). Leave as drawing unit for no scaling.");

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  std::string nameTrim = StringUtil::trimCopy(std::string(cmd.blockCreateName));
  bool nameOk = !nameTrim.empty() && CadBlockFindDef(cmd.blockDefs, nameTrim) < 0;
  bool canOk = nameOk && selCount > 0;
  std::string why;
  if (!nameOk) {
    if (nameTrim.empty()) why = "Enter a block name.";
    else why = "Name already exists.";
  } else if (selCount == 0) {
    why = "Select objects to include.";
  }
  if (!canOk && !why.empty()) {
    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.f), "%s", why.c_str());
  }
  ImGui::BeginDisabled(!canOk);
  if (ImGui::Button("OK", ImVec2(90.f, 0.f)))
    CommitBlockCreateDialog(cmd, log);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)))
    CancelBlockCreateDialog(cmd, log);

  ImGui::End();
}

/// WBLOCK's save window (user request 2026-09-23). Opened by the bare `WBLOCK` verb.
///
/// Deliberately the SMALLEST thing that answers "write which block, where": the definition list the
/// Edit-block picker already uses, the same preview, and the Windows save dialog the DWG export
/// already uses (`BrowseSaveFileDwgUtf8`). AutoCAD's WBLOCK also offers Entire drawing / Objects as
/// sources — not built, because REQ-107 scopes WBLOCK to "write [a block] to its own file" and the
/// other two sources are a different feature with no requirement behind them.
void DrawWblockDialog(AppCommandState& cmd, std::vector<std::string>& log) {
  if (!cmd.wblockDialogOpen)
    return;

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    CancelWblockDialog(cmd, log);
    return;
  }
  ImGui::SetNextWindowSize(ImVec2(460.f, 0.f), ImGuiCond_FirstUseEver);
  bool open = true;
  if (!ImGui::Begin("Write Block", &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    if (!open)
      CancelWblockDialog(cmd, log);
    return;
  }
  if (!open) {
    ImGui::End();
    CancelWblockDialog(cmd, log);
    return;
  }

  ImGui::TextUnformatted("Block");
  ImGui::BeginChild("##WblockList", ImVec2(0.f, 150.f), true);
  for (const CadBlockDefinition& d : cmd.blockDefs) {
    const bool sel = CadBlockEqCi(cmd.wblockName, d.name);
    if (ImGui::Selectable(d.name.c_str(), sel))
      std::snprintf(cmd.wblockName, sizeof(cmd.wblockName), "%s", d.name.c_str());
    if (sel)
      ImGui::SetItemDefaultFocus();
  }
  ImGui::EndChild();

  {
    const int di = CadBlockFindDef(cmd.blockDefs, cmd.wblockName);
    if (di >= 0) {
      const CadBlockDefinition& d = cmd.blockDefs[static_cast<size_t>(di)];
      if (!d.description.empty())
        ImGui::TextWrapped("%s", d.description.c_str());
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("File:");
  ImGui::SameLine(60.f);
  ImGui::SetNextItemWidth(280.f);
  ImGui::InputText("##WblockPath", cmd.wblockPath, sizeof(cmd.wblockPath));
  ImGui::SameLine();
  if (ImGui::Button("Browse...")) {
    // The block's own name is the suggested file name — a WBLOCK'd file is normally named after
    // what is in it, and BLOCKIMPORT names a definition after the file stem when reading one back.
    char chosen[1024]{};
    const std::string suggest = std::string(cmd.wblockName) + ".dwg";
    if (BrowseSaveFileDwgUtf8(chosen, sizeof(chosen), suggest.c_str()))
      std::snprintf(cmd.wblockPath, sizeof(cmd.wblockPath), "%s", chosen);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  const bool haveBlock = CadBlockFindDef(cmd.blockDefs, cmd.wblockName) >= 0;
  const bool havePath = cmd.wblockPath[0] != '\0';
  const bool canOk = haveBlock && havePath;
  if (!canOk)
    ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.f), "%s",
                       !haveBlock ? "Choose a block to write." : "Choose a destination file.");
  ImGui::BeginDisabled(!canOk);
  if (ImGui::Button("OK", ImVec2(90.f, 0.f)))
    CommitWblockDialog(cmd, log);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90.f, 0.f)))
    CancelWblockDialog(cmd, log);

  ImGui::End();
}

