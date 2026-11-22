#include "CadBlocks.hpp"
#include "CadBlockCreateInternal.hpp"
#include "connect/CadBlockConnectInternal.hpp"
#include "CadCommands.hpp"
#include "ProjectWarnings.hpp"  // REQ-383: CheckClipboardPaste, the paste guard PASTEBLOCK shares with PASTE
#include <GoSurvey/Core/StringUtil.hpp>
#include <GoSurvey/Renderer/CadSolidDisplay.hpp>
#include "GoSurvey/Document/cadpiperun.hpp"  // CadPipeRunEndPorts (issue #486, "pipe end" connection port)
#include <GoSurvey/UI/FileDialogs.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "CadBlocks.hpp"
#include "CadCommandsInternal.hpp"

void StartBlockCreateDialog(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::None)
    CancelActiveCommand(st, log);
  st.blockCreateDialogOpen = true;
  st.blockCreatePhase = AppCommandState::BlockCreatePhase::WaitDialog;
  st.blockCreateName[0] = '\0';
  st.blockCreateBaseX = 0.f;
  st.blockCreateBaseY = 0.f;
  st.blockCreateBaseZ = 0.f;
  st.blockCreateSpecifyBase = true;
  st.blockCreateConvertMode = 1;
  st.blockCreateDescription[0] = '\0';
  std::string du = CadDrawingInsUnitsName(st.drawing.drawingInsUnits);
  std::snprintf(st.blockCreateUnits, sizeof(st.blockCreateUnits), "%s", du.c_str());
  log.push_back("BLOCK \u2014 define a new block in the Create dialog.");
}

static bool CommitBlockCreateFromState(AppCommandState& st, std::vector<std::string>& log) {
  std::string name = StringUtil::trimCopy(std::string(st.blockCreateName));
  if (name.empty()) {
    log.push_back("BLOCK \u2014 block name is required.");
    return false;
  }
  if (CadBlockFindDef(st.drawing.blockDefs, name) >= 0) {
    log.push_back("BLOCK \u2014 name \"" + name + "\" is already used.");
    return false;
  }
  if (st.selection.empty()) {
    log.push_back("BLOCK \u2014 select geometry first, then run BLOCK.");
    return false;
  }
  float bx = st.blockCreateBaseX;
  float by = st.blockCreateBaseY;
  float bz = st.blockCreateBaseZ;
  std::string mode = "convert";
  if (st.blockCreateConvertMode == 0) mode = "retain";
  else if (st.blockCreateConvertMode == 2) mode = "delete";
  PushUndoSnapshot(st, "Block");
  CadBlockDefinition def;
  def.name = name;
  def.description = StringUtil::trimCopy(std::string(st.blockCreateDescription));
  def.baseX = bx;
  def.baseY = by;
  def.baseZ = bz;
  std::string unitStr = StringUtil::trimCopy(std::string(st.blockCreateUnits));
  if (unitStr.empty()) unitStr = CadDrawingInsUnitsName(st.drawing.drawingInsUnits);
  def.units = unitStr;
  CaptureSelectionInto(st, &def.content, bx, by, bz);
  CadBlockBakeBasePoint(&def);
  for (const CadBlockNested& n : def.content.nested) {
    if (CadBlockWouldCycle(st.drawing.blockDefs, def.name, n.defName)) {
      log.push_back("BLOCK \u2014 refused: nested block would create a circular reference.");
      return false;
    }
  }
  st.drawing.blockDefs.push_back(std::move(def));
  if (mode == "delete" || mode == "convert")
    EraseSelectedSources(st);
  st.selection.clear();
  if (mode == "convert") {
    CadBlockRef r;
    r.defName = name;
    r.xf.x = bx;
    r.xf.y = by;
    r.xf.z = bz;
    st.drawing.cadBlockRefs.push_back(std::move(r));
    st.drawing.cadBlockRefAttrs.push_back(NewBlockAttr(st));
    st.selection.push_back({SelectedEntity::Type::BlockRef, static_cast<int>(st.drawing.cadBlockRefs.size()) - 1});
  }
  NoteRecent(st, name);
  BumpCadGpuCache(st);
  log.push_back("BLOCK \u2014 created \"" + name + "\".");
  st.blockCreateDialogOpen = false;
  st.blockCreatePhase = AppCommandState::BlockCreatePhase::WaitDialog;
  return true;
}

void SubmitBlockCreateBasePointPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  SubmitBlockCreateBasePointPick(st, wx, wy, 0.f, log);
}

void SubmitBlockCreateBasePointPick(AppCommandState& st, float wx, float wy, float wz, std::vector<std::string>& log) {
  if (st.blockCreatePhase != AppCommandState::BlockCreatePhase::WaitBasePoint)
    return;
  st.blockCreateBaseX = wx;
  st.blockCreateBaseY = wy;
  st.blockCreateBaseZ = wz;
  st.blockCreateSpecifyBase = false;
  // If name and selection are already valid, create immediately; otherwise re-open dialog for user to complete.
  std::string name = StringUtil::trimCopy(std::string(st.blockCreateName));
  bool canCreate = !name.empty() && CadBlockFindDef(st.drawing.blockDefs, name) < 0 && !st.selection.empty();
  if (canCreate) {
    st.blockCreateDialogOpen = false;
    st.blockCreatePhase = AppCommandState::BlockCreatePhase::WaitDialog;
    CommitBlockCreateFromState(st, log);
  } else {
    st.blockCreateDialogOpen = true;
    st.blockCreatePhase = AppCommandState::BlockCreatePhase::WaitDialog;
    log.push_back("BLOCK \u2014 base point picked; complete the dialog and click OK.");
  }
}

void CommitBlockCreateDialog(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.blockCreateDialogOpen) return;
  std::string name = StringUtil::trimCopy(std::string(st.blockCreateName));
  if (name.empty()) {
    log.push_back("BLOCK \u2014 block name is required.");
    return;
  }
  if (CadBlockFindDef(st.drawing.blockDefs, name) >= 0) {
    log.push_back("BLOCK \u2014 name \"" + name + "\" is already used.");
    return;
  }
  if (st.selection.empty()) {
    log.push_back("BLOCK \u2014 select geometry first, then run BLOCK.");
    return;
  }
  if (st.blockCreateSpecifyBase) {
    st.blockCreateDialogOpen = false;
    st.blockCreatePhase = AppCommandState::BlockCreatePhase::WaitBasePoint;
    log.push_back("BLOCK \u2014 pick base point (ESC cancels).");
    return;
  }
  CommitBlockCreateFromState(st, log);
}

void CancelBlockCreateDialog(AppCommandState& st, std::vector<std::string>& log) {
  (void)log;
  st.blockCreateDialogOpen = false;
  st.blockCreatePhase = AppCommandState::BlockCreatePhase::WaitDialog;
}

bool CadBlocksWriteBlockToFile(AppCommandState& st, std::string_view name, const char* pathUtf8,
                               std::vector<std::string>& log) {
  // issue #284: WBLOCK writes a single block definition out to its own .dwg, using the same ADR-044
  // JSON trailer mechanism as whole-drawing save. The trailer's blockDefs array holds just this one
  // definition; BLOCKIMPORT reading it back finds scratch.blockDefs already populated (see
  // ImportCadBlocksFromPathImpl) and merges it directly — no drawing-capture needed.
  //
  // Lifted out of the `wblock` verb so the save dialog writes through exactly this path: two entry
  // points that each formatted their own messages is how a dialog and a typed command drift apart.
  if (!pathUtf8 || pathUtf8[0] == '\0') {
    log.push_back("WBLOCK — no destination file given.");
    return false;
  }
  const int di = CadBlockFindDef(st.drawing.blockDefs, name);
  if (di < 0) {
    log.push_back("WBLOCK — no block named \"" + std::string(name) + "\".");
    return false;
  }
  AppCommandState tmp;
  tmp.drawing.blockDefs.push_back(st.drawing.blockDefs[static_cast<size_t>(di)]);
  if (!ExportDwgFile(tmp, pathUtf8, log)) {
    log.push_back("WBLOCK — could not write " + std::string(pathUtf8) + ".");
    return false;
  }
  log.push_back("WBLOCK — wrote \"" + std::string(name) + "\" to " + pathUtf8 + ".");
  return true;
}

void StartWblockDialog(AppCommandState& st, std::vector<std::string>& log) {
  if (st.drawing.blockDefs.empty()) {
    // Refused with a reason rather than opening an empty picker (REQ-201's shape): there is nothing
    // to write, and a dialog listing nothing cannot say why.
    log.push_back("WBLOCK — this drawing has no block definitions to write.");
    return;
  }
  st.wblockDialogOpen = true;
  // Default to the most recently used definition when there is one, else the first — the same
  // "start where the user last was" rule CadBlocksApplyInsertNameDefaults uses for INSERT.
  std::string pick = st.drawing.blockDefs.front().name;
  if (!st.blockRecent.empty() && CadBlockFindDef(st.drawing.blockDefs, st.blockRecent.front()) >= 0)
    pick = st.blockRecent.front();
  std::snprintf(st.wblockName, sizeof(st.wblockName), "%s", pick.c_str());
  st.wblockPath[0] = '\0';
  log.push_back("WBLOCK — choose a block and a destination file.");
}

void CommitWblockDialog(AppCommandState& st, std::vector<std::string>& log) {
  if (!st.wblockDialogOpen)
    return;
  if (CadBlocksWriteBlockToFile(st, st.wblockName, st.wblockPath, log)) {
    st.wblockDialogOpen = false;
    return;
  }
  // Left OPEN on failure, so the message names what to change and the user's chosen path and block
  // are still there to correct — closing it would make them start over to fix a typo.
}

void CancelWblockDialog(AppCommandState& st, std::vector<std::string>& log) {
  (void)log;
  st.wblockDialogOpen = false;
}
