// CadCommands_Mleader.cpp — MLEADER command (REQ-367 / issue #619): arrow tip, landing, MTEXT label.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {

void AppendPathPoint(CadMultileader* ml, float x, float y, float z) {
  ml->pathXyz.push_back(x);
  ml->pathXyz.push_back(y);
  ml->pathXyz.push_back(z);
}

void InitMultileaderLabelAtLanding(CadMultileader* ml, float landX, float landY, float landZ,
                                   AppCommandState& st) {
  const float mup = std::max(st.modelUnitsPerPlottedInch, 1.e-6f);
  const float plotH = std::max(st.activeMultileaderStyle.textSizeInches, 0.01f);
  const float h = plotH * mup;
  CadAnnotation& a = ml->label;
  a.kind = CadAnnotation::Kind::Mtext;
  a.plottedHeightInches = plotH;
  MultileaderStyles::BakeOntoMultileaderLabel(a, st.activeMultileaderStyle);
  a.insX = landX;
  a.insY = landY;
  a.insZ = landZ;
  a.boxMinX = landX;
  a.boxMinY = landY;
  a.boxMaxX = landX + 22.f * h;
  a.boxMaxY = landY + 2.6f * h;
  a.text = "Multileader";
  StampActiveTextStyleOnNewText(st, a);
}

}  // namespace

void ResetMleaderDraft(AppCommandState& st) {
  st.mleaderPhase = AppCommandState::MleaderPhase::WaitArrowTip;
  st.mleaderTipX = st.mleaderTipY = st.mleaderTipZ = 0.f;
  st.mleaderEditIndex = -1;
}

namespace {

std::optional<int> SoleSelectedMultileaderIndex(const AppCommandState& st) {
  int found = -1;
  for (const SelectedEntity& e : st.selection) {
    if (e.type != SelectedEntity::Type::Multileader)
      continue;
    if (found >= 0)
      return std::nullopt;
    found = e.index;
  }
  if (found < 0 || static_cast<size_t>(found) >= st.cadMultileaders.size())
    return std::nullopt;
  return found;
}

}  // namespace

void AbandonJustPlacedMultileader(AppCommandState& st) {
  const int ix = st.mtextRichEditorMultileaderIndex;
  if (ix < 0 || static_cast<size_t>(ix) >= st.cadMultileaders.size())
    return;
  const auto at = static_cast<std::ptrdiff_t>(ix);
  st.cadMultileaders.erase(st.cadMultileaders.begin() + at);
  if (static_cast<size_t>(ix) < st.cadMultileaderAttrs.size())
    st.cadMultileaderAttrs.erase(st.cadMultileaderAttrs.begin() + at);
  BumpCadGpuCache(st);
}

void OpenMultileaderLabelEditor(AppCommandState& st, int multileaderIndex, bool justPlaced) {
  if (multileaderIndex < 0 || static_cast<size_t>(multileaderIndex) >= st.cadMultileaders.size())
    return;
  CloseMtextRichEditorUi(st);
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorPaper = false;
  st.mtextRichEditorPlain = false;
  st.mtextRichEditorAnnIndex = -1;
  st.mtextRichEditorMarkerIndex = -1;
  st.mtextRichEditorMultileaderIndex = multileaderIndex;
  st.mtextRichEditorMultileaderJustPlaced = justPlaced;
  st.mtextRichEditorBuf = st.cadMultileaders[static_cast<size_t>(multileaderIndex)].label.text;
  st.mtextRichEditorOpen = true;
  st.mtextRichEditorFocusRequest = true;
}

void CommitMleaderLandingAt(AppCommandState& st, float landX, float landY, std::vector<std::string>& log) {
  if (st.mleaderPhase != AppCommandState::MleaderPhase::WaitLanding)
    return;
  const float landZ = CadCommitElevation(st);
  PushUndoSnapshot(st, "MLEADER");
  CadMultileader ml;
  AppendPathPoint(&ml, st.mleaderTipX, st.mleaderTipY, st.mleaderTipZ);
  AppendPathPoint(&ml, landX, landY, landZ);
  InitMultileaderLabelAtLanding(&ml, landX, landY, landZ, st);
  st.cadMultileaders.push_back(std::move(ml));
  st.cadMultileaderAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  const int ix = static_cast<int>(st.cadMultileaders.size()) - 1;
  st.mleaderPhase = AppCommandState::MleaderPhase::WaitLabel;
  log.push_back("MLEADER — edit the label (Save to finish; Esc cancels the whole multileader).");
  OpenMultileaderLabelEditor(st, ix, /*justPlaced=*/true);
}

void FinishMleaderCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.mtextRichEditorMultileaderJustPlaced = false;
  st.active = AppCommandState::Kind::None;
  ResetMleaderDraft(st);
  CloseMtextRichEditorUi(st);
  log.push_back("MLEADER placed.");
}

void StartMleaderStyleCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.mleaderStyleDraft = st.activeMultileaderStyle;
  st.showMleaderStyleDialog = true;
  log.push_back("MSTY — multileader style editor opened.");
}

void StartMleaderCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::None) {
    log.push_back("MLEADER — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Mleader;
  st.lastCommand = AppCommandState::Kind::Mleader;
  ResetMleaderDraft(st);
  log.push_back("MLEADER — specify arrowhead location (click or type X,Y). ESC cancels.");
}

void StartMleaderAddLeaderCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::None) {
    log.push_back("Add Leader — finish or cancel the active command first.");
    return;
  }
  const std::optional<int> ix = SoleSelectedMultileaderIndex(st);
  if (!ix.has_value()) {
    log.push_back("Add Leader — select exactly one multileader first.");
    return;
  }
  const CadMultileader& ml = st.cadMultileaders[static_cast<size_t>(*ix)];
  if (ml.pathXyz.size() < 6) {
    log.push_back("Add Leader — selected multileader has no landing path.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::MleaderAddLeader;
  st.mleaderEditIndex = *ix;
  st.mleaderPhase = AppCommandState::MleaderPhase::WaitArrowTip;
  log.push_back("Add Leader — specify arrowhead location for the new branch. ESC cancels.");
}

void RemoveLeaderFromSelectedMultileader(AppCommandState& st, std::vector<std::string>& log) {
  const std::optional<int> ix = SoleSelectedMultileaderIndex(st);
  if (!ix.has_value()) {
    log.push_back("Remove Leader — select exactly one multileader first.");
    return;
  }
  CadMultileader& ml = st.cadMultileaders[static_cast<size_t>(*ix)];
  if (!ml.extraLeaderPaths.empty()) {
    PushUndoSnapshot(st, "Remove Leader");
    ml.extraLeaderPaths.pop_back();
    BumpCadGpuCache(st);
    log.push_back("Remove Leader — extra branch removed.");
    return;
  }
  log.push_back("Remove Leader — multileader has only one branch.");
}

void CommitMleaderAddLeaderAt(AppCommandState& st, float tipX, float tipY, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::MleaderAddLeader || st.mleaderEditIndex < 0)
    return;
  const size_t mi = static_cast<size_t>(st.mleaderEditIndex);
  if (mi >= st.cadMultileaders.size())
    return;
  CadMultileader& ml = st.cadMultileaders[mi];
  if (ml.pathXyz.size() < 3)
    return;
  float landX = 0.f;
  float landY = 0.f;
  float landZ = 0.f;
  CadMultileaderLandingLocal(ml, &landX, &landY, &landZ);
  const float tipZ = CadCommitElevation(st);
  PushUndoSnapshot(st, "Add Leader");
  ml.extraLeaderPaths.push_back({tipX, tipY, tipZ, landX, landY, landZ});
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetMleaderDraft(st);
  log.push_back("Add Leader — branch added.");
}
