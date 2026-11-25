// ADR-0012 / Issue #1578: Blocks and connection-wizard command module.

#include "blocks/CadBlocksSessions.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"

#include <sstream>
#include <string>

namespace stepengine {

// ---------------------------------------------------------------------------
// InsertBlockSession
// ---------------------------------------------------------------------------

StepDescriptor InsertBlockSession::currentStep(const AppCommandState&) const {
  using IPh = InsertBlockPhase;
  StepDynInputSpec dyn;
  switch (phase) {
  case IPh::WaitInsertPoint:
    dyn.pointEntry = true;
    return StepDescriptor::Custom(
        "INSERT: Insertion point — click or X,Y or X,Y,Z | ESC cancel",
        StepClickRoute::InsertBlockPick, BlankEnterPolicy::Custom, false, false, dyn);
  case IPh::WaitScale:
    dyn.pointEntry = true;
    return StepDescriptor::Custom(
        "INSERT: Scale point — click or X,Y | ESC cancel",
        StepClickRoute::InsertBlockPick, BlankEnterPolicy::Custom, false, false, dyn);
  case IPh::WaitRotation:
    return StepDescriptor::Custom(
        "INSERT: Specify rotation angle <0d0'0\"> — click or type | ESC cancel",
        StepClickRoute::InsertBlockPick, BlankEnterPolicy::Custom);
  case IPh::WaitAlignFace:
    return StepDescriptor::Custom(
        "INSERT: Pick a flat face to align the fitting | ESC cancel",
        StepClickRoute::InsertBlockAlignFacePick, BlankEnterPolicy::Custom);
  case IPh::WaitConnectorTarget:
    return StepDescriptor::Custom(
        "INSERT: Pick target connection port — click near port | ESC cancel",
        StepClickRoute::InsertBlockPick, BlankEnterPolicy::Custom);
  case IPh::WaitAttributes:
    return StepDescriptor::Custom(
        "INSERT: Enter attribute values in the dialog | ESC cancel",
        StepClickRoute::Ignore, BlankEnterPolicy::Custom);
  case IPh::WaitDialog:
    break;
  }
  return StepDescriptor::Custom("INSERT: Configure in the Insert dialog",
                                StepClickRoute::Ignore, BlankEnterPolicy::Custom);
}

StepSubmitResult InsertBlockSession::onBlankEnter(AppCommandState& st,
                                                  std::vector<std::string>& log) {
  using IPh = InsertBlockPhase;
  if (phase == IPh::WaitAttributes) {
    CadBlocksCommitInsertAttrDialog(st, log);
    return StepSubmitResult::Handled;
  }
  if (phase == IPh::WaitRotation) {
    CadBlocksPlacePendingInsert(st, log);
    return StepSubmitResult::Handled;
  }
  return StepSubmitResult::Handled;
}

StepSubmitResult InsertBlockSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                            std::vector<std::string>& log) {
  using IPh = InsertBlockPhase;
  if (phase == IPh::WaitDialog) {
    log.push_back("INSERT — complete the Insert dialog, or ESC to cancel.");
    return StepSubmitResult::Handled;
  }
  if (phase == IPh::WaitAttributes) {
    log.push_back("INSERT — use the Edit Attributes dialog, or press Enter to accept.");
    return StepSubmitResult::Handled;
  }
  if (phase == IPh::WaitRotation) {
    if (trimmedLine.find(',') == std::string::npos) {
      float ang = 0.f;
      if (ParseAngleDegrees(trimmedLine, &ang)) {
        st.insertBlockRotDeg = ang;
        CadBlocksPlacePendingInsert(st, log);
        return StepSubmitResult::Handled;
      }
    }
  }
  if (phase == IPh::WaitInsertPoint) {
    float px = 0.f;
    float py = 0.f;
    float wz = 0.f;
    bool consumed = false;
    if (!ResolveTypedModifyPoint(st, trimmedLine, true, 0.f, 0.f, 0.f, "INSERT", &px, &py, &wz,
                                 &consumed, log)) {
      if (consumed)
        return StepSubmitResult::Handled;
      log.push_back("INSERT — type X,Y or X,Y,Z or pick in the viewport.");
      return StepSubmitResult::Handled;
    }
    int commas = 0;
    for (char c : trimmedLine) {
      if (c == ',')
        ++commas;
    }
    const bool hasExplicitZ = commas >= 2;
    if (!hasExplicitZ && CadWorkPlaneIsWorldXy(st))
      wz = CadCommitElevation(st);
    SubmitInsertBlockPick(st, px, py, wz, log);
    return StepSubmitResult::Handled;
  }
  float px = 0.f;
  float py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, /*allowRelative=*/true, st.insertBlockX,
                         st.insertBlockY)) {
    log.push_back("INSERT — type X,Y or pick in the viewport.");
    return StepSubmitResult::Handled;
  }
  SubmitInsertBlockPick(st, px, py, log);
  return StepSubmitResult::Handled;
}

void InsertBlockSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                        std::vector<std::string>& log) {
  using IPh = InsertBlockPhase;
  if (phase == IPh::WaitInsertPoint || phase == IPh::WaitScale ||
      phase == IPh::WaitRotation || phase == IPh::WaitAlignFace) {
    float wz = CadCommitElevation(st);
    if (st.viewportSnapPickValid)
      wz = st.viewportSnapPickLocalZ;
    SubmitInsertBlockPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), wz,
                          log);
  }
}

void InsertBlockSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  st.insertBlockPipeSpliceArmed = false;
  st.insertBlockDialogOpen = false;
  st.insertBlockAttrDialogOpen = false;
  st.insertBlockAttrRefIndex = -1;
  st.insertBlockSnappedPipeRun = -1;
  log.push_back("INSERT canceled.");
}

void InsertBlockSession::onReset(AppCommandState& st) {
  st.insertBlockDialogOpen = false;
  st.insertBlockAttrDialogOpen = false;
  st.insertBlockAttrRefIndex = -1;
  st.insertBlockSnappedPipeRun = -1;
}

// ---------------------------------------------------------------------------
// BConnectSession / BConnectEditSession / BConnectModeSession / BlockFittingSession
// ---------------------------------------------------------------------------

StepDescriptor BConnectSession::currentStep(const AppCommandState& st) const {
  if (st.blockEditActive && awaitingFace) {
    return StepDescriptor::Custom("", StepClickRoute::BconnectFacePick, BlankEnterPolicy::Custom);
  }
  return StepDescriptor::Text("", BlankEnterPolicy::Custom);
}

StepSubmitResult BConnectSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                         std::vector<std::string>& log) {
  BConnectSubmitLine(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult BConnectSession::onBlankEnter(AppCommandState& st,
                                               std::vector<std::string>& log) {
  BConnectSubmitLine(st, std::string(), log);
  return StepSubmitResult::Handled;
}

void BConnectSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("BCONNECT canceled.");
}

StepDescriptor BConnectEditSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Text("", BlankEnterPolicy::Custom);
}

StepSubmitResult BConnectEditSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                             std::vector<std::string>& log) {
  BConnectEditSubmitLine(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult BConnectEditSession::onBlankEnter(AppCommandState& st,
                                                   std::vector<std::string>& log) {
  BConnectEditSubmitLine(st, std::string(), log);
  return StepSubmitResult::Handled;
}

void BConnectEditSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("BCONNECTEDIT canceled.");
}

StepDescriptor BConnectModeSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Text("", BlankEnterPolicy::Custom);
}

StepSubmitResult BConnectModeSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                             std::vector<std::string>& log) {
  BConnectModeSubmitLine(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult BConnectModeSession::onBlankEnter(AppCommandState& st,
                                                   std::vector<std::string>& log) {
  BConnectModeSubmitLine(st, std::string(), log);
  return StepSubmitResult::Handled;
}

void BConnectModeSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("BCONNECTMODE canceled.");
}

StepDescriptor BlockFittingSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Text("", BlankEnterPolicy::Custom);
}

StepSubmitResult BlockFittingSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                             std::vector<std::string>& log) {
  BlockFittingSubmitLine(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult BlockFittingSession::onBlankEnter(AppCommandState& st,
                                                   std::vector<std::string>& log) {
  BlockFittingSubmitLine(st, std::string(), log);
  return StepSubmitResult::Handled;
}

void BlockFittingSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("BLOCKFITTING canceled.");
}

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------

namespace {

template <const char* kTok>
void RunBlockCmd(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::istringstream iss(args);
  CadBlocksTryIdleCommand(st, kTok, iss, log);
}

#define DEFINE_BLOCK_RUNNER(Ident, TokStr)                                                     \
  void RunBlock_##Ident(AppCommandState& st, const std::string& args,                          \
                        std::vector<std::string>& log) {                                       \
    std::istringstream iss(args);                                                              \
    CadBlocksTryIdleCommand(st, TokStr, iss, log);                                             \
  }

DEFINE_BLOCK_RUNNER(block, "block")
DEFINE_BLOCK_RUNNER(insert, "insert")
DEFINE_BLOCK_RUNNER(explode, "explode")
DEFINE_BLOCK_RUNNER(bedit, "bedit")
DEFINE_BLOCK_RUNNER(bsave, "bsave")
DEFINE_BLOCK_RUNNER(bclose, "bclose")
DEFINE_BLOCK_RUNNER(bsaveas, "bsaveas")
DEFINE_BLOCK_RUNNER(attdef, "attdef")
DEFINE_BLOCK_RUNNER(attedit, "attedit")
DEFINE_BLOCK_RUNNER(attsync, "attsync")
DEFINE_BLOCK_RUNNER(attext, "attext")
DEFINE_BLOCK_RUNNER(bconnect, "bconnect")
DEFINE_BLOCK_RUNNER(bconnectedit, "bconnectedit")
DEFINE_BLOCK_RUNNER(bconnectmode, "bconnectmode")
DEFINE_BLOCK_RUNNER(blockfitting, "blockfitting")
DEFINE_BLOCK_RUNNER(blocklist, "blocklist")
DEFINE_BLOCK_RUNNER(blockstats, "blockstats")
DEFINE_BLOCK_RUNNER(purge, "purge")
DEFINE_BLOCK_RUNNER(wblock, "wblock")
DEFINE_BLOCK_RUNNER(blockimport, "blockimport")
DEFINE_BLOCK_RUNNER(blocklib, "blocklib")
DEFINE_BLOCK_RUNNER(blocksearch, "blocksearch")
DEFINE_BLOCK_RUNNER(blockfav, "blockfav")
DEFINE_BLOCK_RUNNER(blockrecent, "blockrecent")
DEFINE_BLOCK_RUNNER(makeblock, "makeblock")
DEFINE_BLOCK_RUNNER(mkline, "mkline")
DEFINE_BLOCK_RUNNER(selline, "selline")
DEFINE_BLOCK_RUNNER(selblock, "selblock")
DEFINE_BLOCK_RUNNER(movesel, "movesel")
DEFINE_BLOCK_RUNNER(copysel, "copysel")
DEFINE_BLOCK_RUNNER(rotatesel, "rotatesel")
DEFINE_BLOCK_RUNNER(scalesel, "scalesel")
DEFINE_BLOCK_RUNNER(mirrorsel, "mirrorsel")
DEFINE_BLOCK_RUNNER(copyclip, "copyclip")
DEFINE_BLOCK_RUNNER(pasteblock, "pasteblock")
DEFINE_BLOCK_RUNNER(blockmodel, "blockmodel")
DEFINE_BLOCK_RUNNER(blockpaper, "blockpaper")
DEFINE_BLOCK_RUNNER(blockredef, "blockredef")
DEFINE_BLOCK_RUNNER(blockrename, "blockrename")
DEFINE_BLOCK_RUNNER(bparam, "bparam")
DEFINE_BLOCK_RUNNER(baction, "baction")
DEFINE_BLOCK_RUNNER(bvisibility, "bvisibility")
DEFINE_BLOCK_RUNNER(bsetvis, "bsetvis")
DEFINE_BLOCK_RUNNER(bsetparam, "bsetparam")
DEFINE_BLOCK_RUNNER(beditadd, "beditadd")
DEFINE_BLOCK_RUNNER(blocknest, "blocknest")
DEFINE_BLOCK_RUNNER(insunits, "insunits")
DEFINE_BLOCK_RUNNER(blockunits, "blockunits")
DEFINE_BLOCK_RUNNER(libexport, "libexport")
DEFINE_BLOCK_RUNNER(bgrip, "bgrip")
DEFINE_BLOCK_RUNNER(undo, "undo")
DEFINE_BLOCK_RUNNER(redo, "redo")

#undef DEFINE_BLOCK_RUNNER

} // namespace

void RegisterBlocksCommands(CommandRegistry& reg) {
  reg.Add({"block", "-block",
           "Define a block from the selection: BLOCK <name>, <x>, <y>[, CONVERT|DELETE|RETAIN]",
           CadCommandKind::None, false, true, RunBlock_block});
  reg.Add({"insert", "i",
           "Insert a block (dialog); or INSERT <name>, <x>, <y>[, sx, sy, rotDeg[, sz, z]]",
           CadCommandKind::InsertBlock, false, true, RunBlock_insert});
  reg.Add({"explode", "x", "Explode selected block references",
           CadCommandKind::None, false, true, RunBlock_explode});
  reg.Add({"bedit", "", "Open the block editor: BEDIT <name>",
           CadCommandKind::None, false, true, RunBlock_bedit});
  reg.Add({"bsave", "", "Save the block being edited",
           CadCommandKind::None, false, true, RunBlock_bsave});
  reg.Add({"bclose", "", "Close the block editor",
           CadCommandKind::None, false, true, RunBlock_bclose});
  reg.Add({"bsaveas", "", "Save the edited block under a new name",
           CadCommandKind::None, false, true, RunBlock_bsaveas});
  reg.Add({"attdef", "", "Attribute definition (in BEDIT)",
           CadCommandKind::None, false, true, RunBlock_attdef});
  reg.Add({"attedit", "", "Edit attribute values on selected inserts",
           CadCommandKind::None, false, true, RunBlock_attedit});
  reg.Add({"attsync", "", "Synchronize attributes from definitions",
           CadCommandKind::None, false, true, RunBlock_attsync});
  reg.Add({"attext", "", "Extract attributes to the command log / file",
           CadCommandKind::None, false, true, RunBlock_attext});
  reg.Add({"bconnect", "", "Add a connection port (in BEDIT): pick a face, or type its values",
           CadCommandKind::BConnect, false, true, RunBlock_bconnect});
  reg.Add({"bconnectedit", "", "Edit or remove a connection port's role/engagement/tag (in BEDIT)",
           CadCommandKind::BConnectEdit, false, true, RunBlock_bconnectedit});
  reg.Add({"bconnectmode", "", "Add/edit/remove a smart connection mode on a port (in BEDIT)",
           CadCommandKind::BConnectMode, false, true, RunBlock_bconnectmode});
  reg.Add({"blockfitting", "", "Tag the block being edited as a piping fitting (in BEDIT)",
           CadCommandKind::BlockFitting, false, true, RunBlock_blockfitting});
  reg.Add({"blocklist", "", "List block definitions",
           CadCommandKind::None, false, true, RunBlock_blocklist});
  reg.Add({"blockstats", "", "Definition statistics",
           CadCommandKind::None, false, true, RunBlock_blockstats});
  reg.Add({"purge", "-purge", "Purge unused block definitions",
           CadCommandKind::None, false, true, RunBlock_purge});
  reg.Add({"wblock", "", "Write a block definition to its own .dwg file",
           CadCommandKind::None, false, true, RunBlock_wblock});
  reg.Add({"blockimport", "", "Import block definitions (.dxf/.dwg/.sat); omit the path to browse",
           CadCommandKind::None, false, true, RunBlock_blockimport});
  reg.Add({"blocklib", "blockbrowser", "List the drawing block library with previews",
           CadCommandKind::None, false, true, RunBlock_blocklib});
  reg.Add({"blocksearch", "", "Search block names",
           CadCommandKind::None, false, true, RunBlock_blocksearch});
  reg.Add({"blockfav", "", "Favorite blocks",
           CadCommandKind::None, false, true, RunBlock_blockfav});
  reg.Add({"blockrecent", "", "Recently used blocks",
           CadCommandKind::None, false, true, RunBlock_blockrecent});
  reg.Add({"makeblock", "", "Create an empty block definition: MAKEBLOCK <name>",
           CadCommandKind::None, false, true, RunBlock_makeblock});
  reg.Add({"mkline", "", "Add a line: MKLINE x0, y0, x1, y1",
           CadCommandKind::None, false, true, RunBlock_mkline});
  reg.Add({"selline", "", "Select the last line",
           CadCommandKind::None, false, true, RunBlock_selline});
  reg.Add({"selblock", "", "Select the last block reference",
           CadCommandKind::None, false, true, RunBlock_selblock});
  reg.Add({"movesel", "", "Move selected inserts: MOVESEL dx, dy",
           CadCommandKind::None, false, true, RunBlock_movesel});
  reg.Add({"copysel", "", "Copy selected inserts: COPYSEL dx, dy",
           CadCommandKind::None, false, true, RunBlock_copysel});
  reg.Add({"rotatesel", "", "Rotate selected inserts: ROTATESEL x, y, deg",
           CadCommandKind::None, false, true, RunBlock_rotatesel});
  reg.Add({"scalesel", "", "Scale selected inserts: SCALESEL x, y, factor",
           CadCommandKind::None, false, true, RunBlock_scalesel});
  reg.Add({"mirrorsel", "", "Mirror selected inserts: MIRRORSEL x0, y0, x1, y1",
           CadCommandKind::None, false, true, RunBlock_mirrorsel});
  reg.Add({"copyclip", "", "Copy the selection to the CAD clipboard",
           CadCommandKind::None, false, true, RunBlock_copyclip});
  reg.Add({"pasteblock", "", "Paste clipboard block references: PASTEBLOCK dx, dy",
           CadCommandKind::None, false, true, RunBlock_pasteblock});
  reg.Add({"blockmodel", "", "Switch to model space",
           CadCommandKind::None, false, true, RunBlock_blockmodel});
  reg.Add({"blockpaper", "", "Switch to the first paper layout",
           CadCommandKind::None, false, true, RunBlock_blockpaper});

  // Unlisted scripting/authoring & undo/redo commands handled by CadBlocksTryIdleCommand
  reg.Add({"blockredef", "redefine", "", CadCommandKind::None, false, false, RunBlock_blockredef});
  reg.Add({"blockrename", "", "", CadCommandKind::None, false, false, RunBlock_blockrename});
  reg.Add({"bparam", "", "", CadCommandKind::None, false, false, RunBlock_bparam});
  reg.Add({"baction", "", "", CadCommandKind::None, false, false, RunBlock_baction});
  reg.Add({"bvisibility", "", "", CadCommandKind::None, false, false, RunBlock_bvisibility});
  reg.Add({"bsetvis", "", "", CadCommandKind::None, false, false, RunBlock_bsetvis});
  reg.Add({"bsetparam", "", "", CadCommandKind::None, false, false, RunBlock_bsetparam});
  reg.Add({"beditadd", "", "", CadCommandKind::None, false, false, RunBlock_beditadd});
  reg.Add({"blocknest", "", "", CadCommandKind::None, false, false, RunBlock_blocknest});
  reg.Add({"insunits", "", "", CadCommandKind::None, false, false, RunBlock_insunits});
  reg.Add({"blockunits", "", "", CadCommandKind::None, false, false, RunBlock_blockunits});
  reg.Add({"libexport", "", "", CadCommandKind::None, false, false, RunBlock_libexport});
  reg.Add({"bgrip", "", "", CadCommandKind::None, false, false, RunBlock_bgrip});
  reg.Add({"undo", "", "", CadCommandKind::None, false, false, RunBlock_undo});
  reg.Add({"redo", "", "", CadCommandKind::None, false, false, RunBlock_redo});
}

} // namespace stepengine
