#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

/// inserts the definition exactly as authored.
float InsertRotZFromCwNorthDeg(float deg) { return -deg * 0.01745329252f; }

/// Distance from the committed insertion point to \p wx,\p wy,\p wz after ORTHO, used for the live
/// on-screen scale pick. Uses true 3D distance so a solid fitting scaled along a pipe axis matches

void ExplodeRef(AppCommandState& st, const CadBlockRef& ref, const EntityAttributes& insertAttr) {
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(st.drawing.blockDefs, ref, insertAttr, &segs);
  for (const CadBlockWorldSeg& s : segs) {
    st.drawing.pushLine(CadLineRecord{s.x0, s.y0, s.z0, s.x1, s.y1, s.z1});
    EntityAttributes a = s.attr;
    a.id = 0;
    st.drawing.userLineAttrs.push_back(a);
  }
  std::vector<CadAnnotation> anns;
  CadBlockCollectWorldAnnotations(st.drawing.blockDefs, ref, &anns);
  for (CadAnnotation& t : anns) {
    st.drawing.cadAnnotations.push_back(std::move(t));
    EntityAttributes a = insertAttr;
    a.id = 0;
    st.drawing.cadAnnotationAttrs.push_back(std::move(a));
  }
  std::vector<CadBlockWorldSolid> solids;
  CadBlockCollectWorldSolids(st.drawing.blockDefs, ref, insertAttr, &solids);
  for (const CadBlockWorldSolid& ws : solids) {
    if (!ws.solid)
      continue;
    st.drawing.cadSolids.push_back(ws.solid);
    EntityAttributes a = ws.attr;
    a.id = 0;
    st.drawing.cadSolidAttrs.push_back(std::move(a));
  }
}


void CadBlocksAfterPlace(AppCommandState& st, std::vector<std::string>& log);

void FinishInsertCommand(AppCommandState& st) {
  // REQ-350 (f) — the palette's splice arming lasts exactly one placement. Cleared here (and on
  // cancel) so an ordinary INSERT typed afterwards can never inherit it and splice a run.
  st.insertBlockPipeSpliceArmed = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockAttrDialogOpen = false;
  st.insertBlockAttrRefIndex = -1;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitDialog;
  st.active = AppCommandState::Kind::None;
}

CadBlockRef* InsertAttrTarget(AppCommandState& st) {
  if (st.insertBlockAttrRefIndex < 0)
    return nullptr;
  if (st.insertBlockAttrPaper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L || static_cast<size_t>(st.insertBlockAttrRefIndex) >= L->paperBlockRefs.size())
      return nullptr;
    return &L->paperBlockRefs[static_cast<size_t>(st.insertBlockAttrRefIndex)];
  }
  if (static_cast<size_t>(st.insertBlockAttrRefIndex) >= st.drawing.cadBlockRefs.size())
    return nullptr;
  return &st.drawing.cadBlockRefs[static_cast<size_t>(st.insertBlockAttrRefIndex)];
}

void CadBlocksAfterPlace(AppCommandState& st, std::vector<std::string>& log) {
  if (st.insertBlockExplode) {
    FinishInsertCommand(st);
    return;
  }
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.insertBlockName);
  if (di < 0 || st.drawing.blockDefs[static_cast<size_t>(di)].attrDefs.empty()) {
    FinishInsertCommand(st);
    return;
  }
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  const bool paper = ActivePaperGeometryTarget(st) != nullptr;
  if (paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L || L->paperBlockRefs.empty()) {
      FinishInsertCommand(st);
      return;
    }
    st.insertBlockAttrPaper = true;
    st.insertBlockAttrRefIndex = static_cast<int>(L->paperBlockRefs.size()) - 1;
  } else if (!st.drawing.cadBlockRefs.empty()) {
    st.insertBlockAttrPaper = false;
    st.insertBlockAttrRefIndex = static_cast<int>(st.drawing.cadBlockRefs.size()) - 1;
  } else {
    FinishInsertCommand(st);
    return;
  }
  CadBlockRef* r = InsertAttrTarget(st);
  if (!r) {
    FinishInsertCommand(st);
    return;
  }
  const int n = std::min(8, static_cast<int>(def.attrDefs.size()));
  for (int i = 0; i < 8; ++i)
    st.insertBlockAttrBuf[i][0] = '\0';
  for (int i = 0; i < n; ++i) {
    const std::string v = CadBlockAttrGet(*r, def, def.attrDefs[static_cast<size_t>(i)].tag);
    std::snprintf(st.insertBlockAttrBuf[i], sizeof(st.insertBlockAttrBuf[i]), "%s", v.c_str());
  }
  st.insertBlockDialogOpen = false;
  st.insertBlockAttrDialogOpen = true;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitAttributes;
  log.push_back("INSERT — enter attribute values.");
}


} // namespace
