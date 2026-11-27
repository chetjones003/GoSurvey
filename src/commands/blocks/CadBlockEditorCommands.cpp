#include "CadBlocks.hpp"
#include "blocks/CadBlocksInternal.hpp"
#include "blocks/CadBlockCreateInternal.hpp"
#include "connect/CadBlockConnectInternal.hpp"
#include "CadCommands.hpp"
#include "engine/CommandRegistry.hpp"
#include "ProjectWarnings.hpp"
#include <GoSurvey/Core/StringUtil.hpp>
#include <GoSurvey/Renderer/CadSolidDisplay.hpp>
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
#include <string>
#include <string_view>
#include <vector>

namespace {

void HarvestDrawingPrimitivesIntoContent(const AppCommandState& st, CadBlockContent* c) {
  assert(c != nullptr);
  // Same entity store copy as CadBlockCaptureDrawing, then the attribute arrays are padded.
  static_cast<CadEntityStore&>(*c) = static_cast<const CadEntityStore&>(st.drawing);
  c->userLineAttrs.resize(st.drawing.lineCount());
  c->lineVis.assign(st.drawing.lineCount(), "");
  c->userCircleAttrs.resize(st.drawing.circleCount());
  c->circleVis.assign(st.drawing.circleCount(), "");
  EnsureCircleNormals(c->userCircleNormals, st.drawing.circleCount());
  c->userArcAttrs.resize(st.drawing.userArcs.size());
  c->userEllAttrs.resize(st.drawing.userEllipses.size());
  c->cadAnnotationAttrs.resize(st.drawing.cadAnnotations.size());
  c->cadMeshAttrs.resize(st.drawing.cadMeshes.size());
  c->cadSolidAttrs.resize(st.drawing.cadSolids.size());
}


CadBlockParamKind ParseParamKind(const std::string& s) {
  const std::string l = StringUtil::toLowerAsciiCopy(s);
  if (l == "polar")
    return CadBlockParamKind::Polar;
  if (l == "rotation" || l == "rotate")
    return CadBlockParamKind::Rotation;
  if (l == "flip")
    return CadBlockParamKind::Flip;
  if (l == "visibility" || l == "vis")
    return CadBlockParamKind::Visibility;
  if (l == "move")
    return CadBlockParamKind::Move;
  if (l == "lookup")
    return CadBlockParamKind::Lookup;
  return CadBlockParamKind::Linear;
}

CadBlockActionKind ParseActionKind(const std::string& s) {
  const std::string l = StringUtil::toLowerAsciiCopy(s);
  if (l == "move")
    return CadBlockActionKind::Move;
  if (l == "rotate")
    return CadBlockActionKind::Rotate;
  if (l == "scale")
    return CadBlockActionKind::Scale;
  if (l == "flip")
    return CadBlockActionKind::Flip;
  if (l == "visibility" || l == "vis")
    return CadBlockActionKind::Visibility;
  return CadBlockActionKind::Stretch;
}


} // namespace

int PickCadBlockRefAt(float wx, float wy, const AppCommandState& st, float orthoHalfHeightWorld) {
  const float tol = std::max(orthoHalfHeightWorld * 0.02f, 1.e-4f);
  int best = -1;
  float bestD = 1.e30f;
  for (int i = 0; i < static_cast<int>(st.drawing.cadBlockRefs.size()); ++i) {
    if (CadBlockHitWorld(st.drawing.blockDefs, st.drawing.cadBlockRefs[static_cast<size_t>(i)], wx, wy, tol)) {
      const float dx = wx - st.drawing.cadBlockRefs[static_cast<size_t>(i)].xf.x;
      const float dy = wy - st.drawing.cadBlockRefs[static_cast<size_t>(i)].xf.y;
      const float d = dx * dx + dy * dy;
      if (d < bestD) {
        bestD = d;
        best = i;
      }
    }
  }
  return best;
}

bool CadBlocksTryIdleCommand(AppCommandState& st, const std::string& plotTok, std::istream& args,
                             std::vector<std::string>& log) {
  const std::string tok = StringUtil::toLowerAsciiCopy(plotTok);

  if (tok == "block" || tok == "-block") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 3 || f.size() > 5) {
      log.push_back("BLOCK — usage: BLOCK <name>, <baseX>, <baseY>[, RETAIN|DELETE|CONVERT][, <baseZ>].");
      return true;
    }
    if (st.selection.empty()) {
      log.push_back("BLOCK — select geometry first, then run BLOCK.");
      return true;
    }
    if (CadBlockFindDef(st.drawing.blockDefs, f[0]) >= 0) {
      log.push_back("BLOCK — name \"" + f[0] + "\" is already used.");
      return true;
    }
    float bx = 0.f, by = 0.f, bz = 0.f;
    if (!TryParseF(f[1], bx) || !TryParseF(f[2], by) ||
        (f.size() == 5 && !TryParseF(f[4], bz))) {
      log.push_back("BLOCK — base point must be numbers.");
      return true;
    }
    std::string mode = "convert";
    if (f.size() >= 4)
      mode = StringUtil::toLowerAsciiCopy(f[3]);
    PushUndoSnapshot(st, "Block");
    CadBlockDefinition def;
    def.name = f[0];
    def.baseX = bx;
    def.baseY = by;
    def.baseZ = bz;
    def.units = CadDrawingInsUnitsName(st.drawing.drawingInsUnits);
    CaptureSelectionInto(st, &def.content, bx, by, bz);
    CadBlockBakeBasePoint(&def);
    for (const CadBlockNested& n : def.content.nested) {
      if (CadBlockWouldCycle(st.drawing.blockDefs, def.name, n.defName)) {
        log.push_back("BLOCK — refused: nested block would create a circular reference.");
        return true;
      }
    }
    st.drawing.blockDefs.push_back(std::move(def));
    if (mode == "delete" || mode == "convert")
      EraseSelectedSources(st);
    st.selection.clear();
    if (mode == "convert" || mode.empty()) {
      CadBlockRef r;
      r.defName = f[0];
      r.xf.x = bx;
      r.xf.y = by;
      r.xf.z = bz;
      st.drawing.cadBlockRefs.push_back(std::move(r));
      st.drawing.cadBlockRefAttrs.push_back(NewBlockAttr(st));
      st.selection.push_back(
          {SelectedEntity::Type::BlockRef, static_cast<int>(st.drawing.cadBlockRefs.size()) - 1});
    }
    NoteRecent(st, f[0]);
    BumpCadGpuCache(st);
    log.push_back("BLOCK — created \"" + f[0] + "\".");
    return true;
  }

  if (tok == "insert") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      StartInsertBlockCommand(st, log);
      return true;
    }
    if (f.size() < 3 || f.size() == 4 || f.size() > 8) {
      log.push_back("INSERT — usage: INSERT <name>, <x>, <y>[, <sx>, <sy>[, <rotation>[, <sz>[, <z>]]]].");
      return true;
    }
    CadBlockXform xf;
    float x = 0.f, y = 0.f, z = 0.f, rotationDeg = 0.f;
    if (!TryParseF(f[1], x) || !TryParseF(f[2], y) ||
        (f.size() >= 5 && (!TryParseF(f[3], xf.sx) || !TryParseF(f[4], xf.sy))) ||
        (f.size() >= 6 && !TryParseF(f[5], rotationDeg)) ||
        (f.size() >= 7 && !TryParseF(f[6], xf.sz)) ||
        (f.size() == 8 && !TryParseF(f[7], z))) {
      log.push_back("INSERT — coordinates and scales must be numbers.");
      return true;
    }
    xf.x = x;
    xf.y = y;
    xf.z = z;
    xf.rotZ = InsertRotZFromCwNorthDeg(rotationDeg);
    std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "%s", f[0].c_str());
    st.insertBlockExplode = false;
    if (CadBlockPlaceInsert(st, f[0], xf, false, log))
      CadBlocksAfterPlace(st, log);
    return true;
  }

  if (tok == "explode") {
    int n = 0;
    PushUndoSnapshot(st, "Explode");
    std::vector<int> idx;
    for (const SelectedEntity& e : st.selection) {
      if (e.type == SelectedEntity::Type::BlockRef)
        idx.push_back(e.index);
    }
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    for (int i = static_cast<int>(idx.size()) - 1; i >= 0; --i) {
      const int k = idx[static_cast<size_t>(i)];
      if (k < 0 || static_cast<size_t>(k) >= st.drawing.cadBlockRefs.size())
        continue;
      EntityAttributes a = NewBlockAttr(st);
      if (static_cast<size_t>(k) < st.drawing.cadBlockRefAttrs.size())
        a = st.drawing.cadBlockRefAttrs[static_cast<size_t>(k)];
      ExplodeRef(st, st.drawing.cadBlockRefs[static_cast<size_t>(k)], a);
      st.drawing.cadBlockRefs.erase(st.drawing.cadBlockRefs.begin() + k);
      if (static_cast<size_t>(k) < st.drawing.cadBlockRefAttrs.size())
        st.drawing.cadBlockRefAttrs.erase(st.drawing.cadBlockRefAttrs.begin() + k);
      ++n;
    }
    // REQ-103 step 8 / issue #390: a Polyline (2D, 3DPOLY, or a rectangle — a 4-vertex polyline per
    // REQ-053) explodes into one LINE/ARC per segment; other selected kinds are reported (REQ-201).
    const int nPoly = ExplodeSelectedPolylines(st, log);
    st.selection.clear();
    EnsureEntityIds(st);
    BumpCadGpuCache(st);
    if (n == 0 && nPoly == 0) {
      log.push_back("EXPLODE — nothing to explode: select a block reference or a polyline.");
    } else {
      std::string msg = "EXPLODE —";
      if (n > 0)
        msg += " " + std::to_string(n) + " block reference(s)";
      if (n > 0 && nPoly > 0)
        msg += ",";
      if (nPoly > 0)
        msg += " " + std::to_string(nPoly) + " polyline(s)";
      msg += ".";
      log.push_back(msg);
    }
    return true;
  }

  if (tok == "bedit") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty()) {
      CadBlocksOpenEditPicker(st, log);
      return true;
    }
    CadBlocksEnterNamedEditor(st, f[0], log);
    return true;
  }

  if (tok == "bsave") {
    if (st.blockEditorName.empty()) {
      log.push_back("BSAVE — no block editor is open.");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di >= 0) {
      if (st.blockEditActive)  // ADR-043: fold the model arrays back into the definition
        HarvestDrawingPrimitivesIntoContent(st, &st.drawing.blockDefs[static_cast<size_t>(di)].content);
      st.blockEditorSnapshot = st.drawing.blockDefs[static_cast<size_t>(di)];
    }
    st.blockEditorDirty = false;
    BumpCadGpuCache(st);
    st.blockEditCleanRevision = st.cadGpuRevision;
    // REQ-350 / ADR-062 — this definition's geometry just changed, so any cached Pipe Fittings
    // thumbnail of it now shows the part as it WAS. Marked for re-render; the UI's service pass is what
    // actually drops it, because the renderer is not this layer's to call.
    if (std::find(st.pipeFittingThumbStale.begin(), st.pipeFittingThumbStale.end(), st.blockEditorName) ==
        st.pipeFittingThumbStale.end())
      st.pipeFittingThumbStale.push_back(st.blockEditorName);
    log.push_back("BSAVE — saved \"" + st.blockEditorName + "\". All references update.");
    return true;
  }

  if (tok == "bclose") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (st.blockEditorName.empty()) {
      log.push_back("BCLOSE — no block editor is open.");
      return true;
    }
    const std::string f0 = f.empty() ? std::string() : StringUtil::toLowerAsciiCopy(f[0]);
    const bool discard = f0 == "discard";
    const bool saveArg = f0 == "save";
    const bool dirty = st.blockEditActive && st.cadGpuRevision != st.blockEditCleanRevision;
    if (dirty && !discard && !saveArg) {
      st.blockEditCloseAsked = true;  // CadUi raises the Save / Don't Save / Cancel modal
      log.push_back("BCLOSE — unsaved changes to \"" + st.blockEditorName +
                    "\"; choose Save, Don't Save, or Cancel.");
      return true;
    }
    st.blockEditCloseAsked = false;
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (discard) {
      if (di >= 0)
        st.drawing.blockDefs[static_cast<size_t>(di)] = st.blockEditorSnapshot;
      log.push_back("BCLOSE — discarded edits to \"" + st.blockEditorName + "\".");
    } else {
      if (di >= 0 && st.blockEditActive)
        HarvestDrawingPrimitivesIntoContent(st, &st.drawing.blockDefs[static_cast<size_t>(di)].content);
      log.push_back("BCLOSE — closed \"" + st.blockEditorName + "\".");
    }
    if (st.blockEditActive) {
      // The stash captured blockDefs at enter time; keep the definitions as this session left them
      // and take model geometry, INSERT references and everything else back from the stash.
      std::vector<CadBlockDefinition> editedDefs = std::move(st.drawing.blockDefs);
      CadRestoreGeometrySnapshot(st, st.blockEditModelStash);
      st.drawing.blockDefs = std::move(editedDefs);
      st.blockEditModelStash = DrawingGeometrySnapshot{};
      st.viewportPanX = st.blockEditCamPanX;
      st.viewportPanY = st.blockEditCamPanY;
      st.viewportPanZ = st.blockEditCamPanZ;
      st.viewportZoom = st.blockEditCamZoom;
      st.viewportAzimuthDeg = st.blockEditCamAz;
      st.viewportElevationDeg = st.blockEditCamEl;
      st.viewportRollDeg = st.blockEditCamRoll;  // #153
      CadTruncateActiveUndoStack(st, st.blockEditUndoMark);  // drop session block-geometry snapshots
      st.blockEditActive = false;
    }
    ClearCadSelection(st);
    st.blockEditorName.clear();
    st.blockEditorDirty = false;
    st.blockAuthoringPaletteOpen = false;
    BumpCadGpuCache(st);
    return true;
  }

  if (tok == "bsaveas") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (st.blockEditorName.empty() || f.empty()) {
      log.push_back("BSAVEAS — usage: BSAVEAS <newname> (while BEDIT is open).");
      return true;
    }
    if (CadBlockFindDef(st.drawing.blockDefs, f[0]) >= 0) {
      log.push_back("BSAVEAS — name \"" + f[0] + "\" is already used.");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di < 0) {
      log.push_back("BSAVEAS — editor block missing.");
      return true;
    }
    PushUndoSnapshot(st, "Bsaveas");
    CadBlockDefinition copy = st.drawing.blockDefs[static_cast<size_t>(di)];
    copy.name = f[0];
    copy.id = 0;
    st.drawing.blockDefs.push_back(std::move(copy));
    st.blockEditorName = f[0];
    log.push_back("BSAVEAS — copied to \"" + f[0] + "\".");
    return true;
  }

  if (tok == "blockredef" || tok == "redefine") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 3) {
      log.push_back("BLOCKREDEF — usage: BLOCKREDEF <name>, <baseX>, <baseY>[, <baseZ>].");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, f[0]);
    if (di < 0) {
      log.push_back("BLOCKREDEF — no block named \"" + f[0] + "\".");
      return true;
    }
    float bx = 0.f, by = 0.f, bz = 0.f;
    if (!TryParseF(f[1], bx) || !TryParseF(f[2], by)) {
      log.push_back("BLOCKREDEF — base point must be numbers.");
      return true;
    }
    if (f.size() >= 4 && !TryParseF(f[3], bz)) {
      log.push_back("BLOCKREDEF — base Z must be a number.");
      return true;
    }
    PushUndoSnapshot(st, "Blockredef");
    CadBlockContent c;
    CaptureSelectionInto(st, &c, bx, by, bz);
    st.drawing.blockDefs[static_cast<size_t>(di)].content = std::move(c);
    st.drawing.blockDefs[static_cast<size_t>(di)].baseX = 0.f;
    st.drawing.blockDefs[static_cast<size_t>(di)].baseY = 0.f;
    st.drawing.blockDefs[static_cast<size_t>(di)].baseZ = 0.f;
    BumpCadGpuCache(st);
    log.push_back("BLOCKREDEF — updated \"" + f[0] + "\". References keep their transforms.");
    return true;
  }

  if (tok == "attdef") {
    if (st.blockEditorName.empty()) {
      log.push_back("ATTDEF — open a block with BEDIT first.");
      return true;
    }
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 3) {
      log.push_back("ATTDEF — usage: ATTDEF <tag>, <prompt>, <default>[, <x>, <y>].");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di < 0)
      return true;
    CadBlockAttrDef d;
    d.tag = f[0];
    d.prompt = f[1];
    d.defaultValue = f[2];
    if (f.size() >= 5) {
      if (!TryParseF(f[3], d.localX) || !TryParseF(f[4], d.localY)) {
        log.push_back("ATTDEF — x and y must be numbers.");
        return true;
      }
    }
    st.drawing.blockDefs[static_cast<size_t>(di)].attrDefs.push_back(std::move(d));
    st.blockEditorDirty = true;
    log.push_back("ATTDEF — tag " + f[0] + ".");
    return true;
  }

  if (tok == "bconnect") {
    // Prompted, one-value-at-a-time wizard (issue #496) — see BConnectStart/-SubmitLine below.
    const std::vector<std::string> f = SplitCommaRest(args);
    BConnectStart(st, f.empty() ? std::string() : f[0], log);
    return true;
  }

  if (tok == "bconnectedit") {
    // Prompted wizard (issue #496) — see BConnectEditStart/-SubmitLine below.
    const std::vector<std::string> f = SplitCommaRest(args);
    BConnectEditStart(st, f.empty() ? std::string() : f[0], log);
    return true;
  }

  if (tok == "bconnectmode") {
    // Prompted, one-value-at-a-time wizard (issue #496) — see BConnectModeStart/-SubmitLine below.
    const std::vector<std::string> f = SplitCommaRest(args);
    BConnectModeStart(st, f.empty() ? std::string() : f[0], log);
    return true;
  }

  if (tok == "blockfitting") {
    // Prompted wizard (issue #496) — see BlockFittingStart/-SubmitLine below.
    BlockFittingStart(st, log);
    return true;
  }

  if (tok == "attedit") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("ATTEDIT — usage: ATTEDIT <tag>, <value> (with block references selected).");
      return true;
    }
    PushUndoSnapshot(st, "Attedit");
    int n = 0;
    for (const SelectedEntity& e : st.selection) {
      if (e.type != SelectedEntity::Type::BlockRef)
        continue;
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
        continue;
      CadBlockAttrSet(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], f[0], f[1]);
      ++n;
    }
    log.push_back("ATTEDIT — " + std::to_string(n) + " reference(s).");
    return true;
  }

  if (tok == "attsync") {
    PushUndoSnapshot(st, "Attsync");
    int n = 0;
    for (CadBlockRef& r : st.drawing.cadBlockRefs) {
      const int di = CadBlockFindDef(st.drawing.blockDefs, r.defName);
      if (di < 0)
        continue;
      const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
      for (const CadBlockAttrDef& ad : def.attrDefs) {
        bool have = false;
        for (const CadBlockAttrValue& v : r.attributes) {
          if (CadBlockEqCi(v.tag, ad.tag))
            have = true;
        }
        if (!have) {
          CadBlockAttrSet(&r, ad.tag, ad.defaultValue);
          ++n;
        }
      }
    }
    log.push_back("ATTSYNC — filled " + std::to_string(n) + " missing attribute(s) from definitions.");
    return true;
  }

  if (tok == "attext") {
    const std::vector<std::string> f = SplitCommaRest(args);
    std::ostringstream oss;
    oss << "ATTEXT";
    for (const CadBlockRef& r : st.drawing.cadBlockRefs) {
      const int di = CadBlockFindDef(st.drawing.blockDefs, r.defName);
      if (di < 0)
        continue;
      const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
      oss << "\n" << r.defName;
      for (const CadBlockAttrDef& ad : def.attrDefs)
        oss << "," << ad.tag << "=" << CadBlockAttrGet(r, def, ad.tag);
    }
    log.push_back(oss.str());
    if (!f.empty()) {
      std::ofstream out(f[0]);
      if (out)
        out << oss.str();
    }
    return true;
  }

  if (tok == "blocklist") {
    if (st.drawing.blockDefs.empty()) {
      log.push_back("BLOCKLIST — (none).");
      return true;
    }
    for (const CadBlockDefinition& d : st.drawing.blockDefs) {
      const int refs = CadBlockCountRefs(st.drawing.cadBlockRefs, d.name);
      log.push_back("BLOCKLIST — " + d.name + " refs=" + std::to_string(refs) +
                    " nested=" + std::to_string(d.content.nested.size()) +
                    " attrs=" + std::to_string(d.attrDefs.size()) +
                    (d.parameters.empty() ? " dynamic=no" : " dynamic=yes") +
                    " vis=" + std::to_string(d.visibilityStates.size()));
    }
    return true;
  }

  if (tok == "blockstats") {
    const std::vector<std::string> f = SplitCommaRest(args);
    std::string name = f.empty() ? std::string() : f[0];
    if (name.empty() && !st.selection.empty() && st.selection[0].type == SelectedEntity::Type::BlockRef) {
      const int k = st.selection[0].index;
      if (k >= 0 && static_cast<size_t>(k) < st.drawing.cadBlockRefs.size())
        name = st.drawing.cadBlockRefs[static_cast<size_t>(k)].defName;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, name);
    if (di < 0) {
      log.push_back("BLOCKSTATS — name a definition or select a reference.");
      return true;
    }
    const CadBlockDefinition& d = st.drawing.blockDefs[static_cast<size_t>(di)];
    log.push_back("Definition: " + d.name);
    log.push_back("References: " + std::to_string(CadBlockCountRefs(st.drawing.cadBlockRefs, d.name)));
    log.push_back("Attributes: " + std::to_string(d.attrDefs.size()));
    log.push_back("Nested Blocks: " + std::to_string(d.content.nested.size()));
    log.push_back(std::string("Dynamic: ") + (d.parameters.empty() ? "No" : "Yes"));
    log.push_back("Visibility States: " + std::to_string(d.visibilityStates.size()));
    return true;
  }

  if (tok == "purge" || tok == "-purge") {
    const std::vector<std::string> f = SplitCommaRest(args);
    PushUndoSnapshot(st, "Purge");
    int n = 0;
    for (int i = static_cast<int>(st.drawing.blockDefs.size()) - 1; i >= 0; --i) {
      const std::string& nm = st.drawing.blockDefs[static_cast<size_t>(i)].name;
      if (CadBlockCountRefs(st.drawing.cadBlockRefs, nm) > 0 || CadBlockDefUsedBy(st.drawing.blockDefs, nm))
        continue;
      if (!f.empty() && !CadBlockEqCi(f[0], "blocks") && !CadBlockEqCi(f[0], nm) &&
          !CadBlockEqCi(f[0], "*"))
        continue;
      st.drawing.blockDefs.erase(st.drawing.blockDefs.begin() + i);
      ++n;
    }
    log.push_back("PURGE — removed " + std::to_string(n) + " unused block definition(s).");
    return true;
  }

  if (tok == "blockrename") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("BLOCKRENAME — usage: BLOCKRENAME <old>, <new>.");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, f[0]);
    if (di < 0) {
      log.push_back("BLOCKRENAME — no block named \"" + f[0] + "\".");
      return true;
    }
    if (CadBlockFindDef(st.drawing.blockDefs, f[1]) >= 0) {
      log.push_back("BLOCKRENAME — \"" + f[1] + "\" is already used.");
      return true;
    }
    PushUndoSnapshot(st, "Blockrename");
    const std::string old = st.drawing.blockDefs[static_cast<size_t>(di)].name;
    st.drawing.blockDefs[static_cast<size_t>(di)].name = f[1];
    for (CadBlockRef& r : st.drawing.cadBlockRefs) {
      if (CadBlockEqCi(r.defName, old))
        r.defName = f[1];
    }
    for (CadBlockDefinition& d : st.drawing.blockDefs) {
      for (CadBlockNested& n : d.content.nested) {
        if (CadBlockEqCi(n.defName, old))
          n.defName = f[1];
      }
    }
    log.push_back("BLOCKRENAME — " + old + " -> " + f[1] + ".");
    return true;
  }

  if (tok == "bparam") {
    if (st.blockEditorName.empty()) {
      log.push_back("BPARAM — open a block with BEDIT first.");
      return true;
    }
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("BPARAM — usage: BPARAM <name>, <kind>[, <value>].");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di < 0)
      return true;
    CadBlockParameter p;
    p.name = f[0];
    p.kind = ParseParamKind(f[1]);
    if (f.size() >= 3 && !TryParseF(f[2], p.value)) {
      log.push_back("BPARAM — value must be a number.");
      return true;
    }
    PushUndoSnapshot(st, "Block parameter");
    st.drawing.blockDefs[static_cast<size_t>(di)].parameters.push_back(std::move(p));
    st.blockEditorDirty = true;
    BumpCadGpuCache(st);
    log.push_back("BPARAM — added " + f[0] + ".");
    return true;
  }

  if (tok == "baction") {
    if (st.blockEditorName.empty()) {
      log.push_back("BACTION — open a block with BEDIT first.");
      return true;
    }
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("BACTION — usage: BACTION <kind>, <param>[, ox, oy, dx, dy, thresh].");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di < 0)
      return true;
    CadBlockAction a;
    a.kind = ParseActionKind(f[0]);
    a.paramName = f[1];
    if (f.size() >= 7) {
      if (!TryParseF(f[2], a.originX) || !TryParseF(f[3], a.originY) ||
          !TryParseF(f[4], a.dirX) || !TryParseF(f[5], a.dirY) ||
          !TryParseF(f[6], a.threshold)) {
        log.push_back("BACTION — ox, oy, dx, dy and thresh must be numbers.");
        return true;
      }
    }
    PushUndoSnapshot(st, "Block action");
    st.drawing.blockDefs[static_cast<size_t>(di)].actions.push_back(std::move(a));
    st.blockEditorDirty = true;
    BumpCadGpuCache(st);
    log.push_back("BACTION — bound to " + f[1] + ".");
    return true;
  }

  if (tok == "bvisibility") {
    if (st.blockEditorName.empty()) {
      log.push_back("BVISIBILITY — open a block with BEDIT first.");
      return true;
    }
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty()) {
      log.push_back("BVISIBILITY — usage: BVISIBILITY <state>.");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di < 0)
      return true;
    PushUndoSnapshot(st, "Block visibility state");
    st.drawing.blockDefs[static_cast<size_t>(di)].visibilityStates.push_back(f[0]);
    st.blockEditorDirty = true;
    BumpCadGpuCache(st);
    log.push_back("BVISIBILITY — state " + f[0] + ".");
    return true;
  }

  if (tok == "bsetvis") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty()) {
      log.push_back("BSETVIS — usage: BSETVIS <state> (selected references).");
      return true;
    }
    for (const SelectedEntity& e : st.selection) {
      if (e.type != SelectedEntity::Type::BlockRef)
        continue;
      if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.cadBlockRefs.size())
        st.drawing.cadBlockRefs[static_cast<size_t>(e.index)].visState = f[0];
    }
    BumpCadGpuCache(st);
    log.push_back("BSETVIS — " + f[0] + ".");
    return true;
  }

  if (tok == "bsetparam") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("BSETPARAM — usage: BSETPARAM <name>, <value> (selected references).");
      return true;
    }
    float v = 0.f;
    if (!TryParseF(f[1], v)) {
      log.push_back("BSETPARAM — value must be a number.");
      return true;
    }
    for (const SelectedEntity& e : st.selection) {
      if (e.type != SelectedEntity::Type::BlockRef)
        continue;
      if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.cadBlockRefs.size())
        CadBlockParamSet(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], f[0], v);
    }
    BumpCadGpuCache(st);
    log.push_back("BSETPARAM — " + f[0] + "=" + f[1] + ".");
    return true;
  }

  if (tok == "beditadd") {
    if (st.blockEditorName.empty()) {
      log.push_back("BEDITADD — open a block with BEDIT first.");
      return true;
    }
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 5 || StringUtil::toLowerAsciiCopy(f[0]) != "line") {
      log.push_back("BEDITADD — usage: BEDITADD LINE, x1, y1, x2, y2[, visState].");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
    if (di < 0)
      return true;
    float x1 = 0.f, y1 = 0.f, x2 = 0.f, y2 = 0.f;
    if (!TryParseF(f[1], x1) || !TryParseF(f[2], y1) || !TryParseF(f[3], x2) ||
        !TryParseF(f[4], y2)) {
      log.push_back("BEDITADD — x1, y1, x2, y2 must be numbers.");
      return true;
    }
    if (st.blockEditActive) {
      // ADR-043: an active session keeps the geometry in the model arrays; add there so it survives
      // the harvest on Save (and shows in the isolated view like any other draw).
      const float seg[6] = {x1, y1, 0.f, x2, y2, 0.f};
      st.drawing.pushLine(CadLineRecord{x1, y1, 0.0, x2, y2, 0.0});
      st.drawing.userLineAttrs.push_back(EntityAttributes{});
    } else {
      CadBlockContent& c = st.drawing.blockDefs[static_cast<size_t>(di)].content;
      c.pushLine(CadLineRecord{x1, y1, 0.0, x2, y2, 0.0});
      c.userLineAttrs.push_back(EntityAttributes{});
      c.lineVis.push_back(f.size() >= 6 ? f[5] : std::string());
    }
    st.blockEditorDirty = true;
    BumpCadGpuCache(st);
    log.push_back("BEDITADD — line added in local coordinates.");
    return true;
  }

  if (tok == "blocknest") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("BLOCKNEST — usage: BLOCKNEST <host>, <child>.");
      return true;
    }
    const int hi = CadBlockFindDef(st.drawing.blockDefs, f[0]);
    const int ci = CadBlockFindDef(st.drawing.blockDefs, f[1]);
    if (hi < 0 || ci < 0) {
      log.push_back("BLOCKNEST — both names must exist.");
      return true;
    }
    if (CadBlockWouldCycle(st.drawing.blockDefs, f[0], f[1])) {
      log.push_back("BLOCKNEST — refused circular reference.");
      return true;
    }
    CadBlockNested n;
    n.defName = st.drawing.blockDefs[static_cast<size_t>(ci)].name;
    st.drawing.blockDefs[static_cast<size_t>(hi)].content.nested.push_back(std::move(n));
    BumpCadGpuCache(st);
    log.push_back("BLOCKNEST — " + f[1] + " inside " + f[0] + ".");
    return true;
  }

  if (tok == "insunits") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty()) {
      log.push_back("INSUNITS — " + CadDrawingInsUnitsName(st.drawing.drawingInsUnits) + ".");
      return true;
    }
    const int code = CadDrawingInsUnitsCode(f[0]);
    if (code != st.drawing.drawingInsUnits) {
      PushUndoSnapshot(st, "INSUNITS");  // the drawing unit is undoable (REQ-357)
      st.drawing.drawingInsUnits = code;
      BumpCadGpuCache(st);
    }
    log.push_back("INSUNITS — drawing units " + CadDrawingInsUnitsName(st.drawing.drawingInsUnits) + ".");
    return true;
  }

  if (tok == "blockunits") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("BLOCKUNITS — usage: BLOCKUNITS <name>, <units>.");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, f[0]);
    if (di < 0) {
      log.push_back("BLOCKUNITS — no block named \"" + f[0] + "\".");
      return true;
    }
    st.drawing.blockDefs[static_cast<size_t>(di)].units = f[1];
    log.push_back("BLOCKUNITS — " + f[0] + " is " + f[1] + ".");
    return true;
  }

  if (tok == "blocksearch") {
    const std::vector<std::string> f = SplitCommaRest(args);
    const std::string q = f.empty() ? st.blockLibraryFilter : f[0];
    st.blockLibraryFilter = q;
    int n = 0;
    for (const CadBlockDefinition& d : st.drawing.blockDefs) {
      if (q.empty() || StringUtil::toLowerAsciiCopy(d.name).find(StringUtil::toLowerAsciiCopy(q)) !=
                           std::string::npos) {
        log.push_back("BLOCKSEARCH — " + d.name);
        ++n;
      }
    }
    if (n == 0)
      log.push_back("BLOCKSEARCH — no matches.");
    return true;
  }

  if (tok == "blockfav") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty()) {
      for (const std::string& n : st.blockFavorites)
        log.push_back("BLOCKFAV — " + n);
      if (st.blockFavorites.empty())
        log.push_back("BLOCKFAV — (none).");
      return true;
    }
    st.blockFavorites.erase(std::remove_if(st.blockFavorites.begin(), st.blockFavorites.end(),
                                           [&](const std::string& n) { return CadBlockEqCi(n, f[0]); }),
                            st.blockFavorites.end());
    st.blockFavorites.push_back(f[0]);
    log.push_back("BLOCKFAV — added " + f[0] + ".");
    return true;
  }

  if (tok == "blockrecent") {
    for (const std::string& n : st.blockRecent)
      log.push_back("BLOCKRECENT — " + n);
    if (st.blockRecent.empty())
      log.push_back("BLOCKRECENT — (none).");
    return true;
  }

  if (tok == "blocklib" || tok == "blockbrowser") {
    log.push_back("BLOCKLIB — drawing library:");
    for (const CadBlockDefinition& d : st.drawing.blockDefs) {
      float mnX = 0, mnY = 0, mxX = 0, mxY = 0;
      CadBlockRef preview;
      preview.defName = d.name;
      CadBlockWorldAabb(st.drawing.blockDefs, preview, &mnX, &mnY, &mxX, &mxY);
      log.push_back("  " + d.name + " preview " + std::to_string(mxX - mnX) + " x " +
                    std::to_string(mxY - mnY));
    }
    return true;
  }

  if (tok == "wblock") {
    // issue #284: WBLOCK writes a single block definition out to its own .dwg, using the same
    // ADR-044 JSON trailer mechanism as whole-drawing save. The trailer's blockDefs array holds
    // just this one definition; BLOCKIMPORT reading it back finds scratch.blockDefs already
    // populated (see ImportCadBlocksFromPathImpl) and merges it directly — no drawing-capture needed.
    // Both arguments given: write straight out, the way scripts and the two `[issue284][wblock]`
    // tests drive it. Bare `WBLOCK` used to answer with a usage line; at the user's request
    // (2026-09-23) it now opens the save dialog, which is what AutoCAD's own WBLOCK does.
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      StartWblockDialog(st, log);
      return true;
    }
    CadBlocksWriteBlockToFile(st, f[0], f[1].c_str(), log);
    return true;
  }

  if (tok == "pipecatalog" || tok == "pcat" || tok == "pipepalette") {
    if (const stepengine::CommandDef* def = stepengine::CommandRegistry::Instance().FindAny(tok);
        def && def->execute) {
      std::string rest;
      std::getline(args, rest);
      def->execute(st, StringUtil::trimCopy(rest), log);
      return true;
    }
  }

  if (tok == "libexport") {
    // Fittings library export workflow (issue #486 increment A3). Writes the definition as a
    // .dwg (same ADR-044 trailer WBLOCK uses, so partType/pressureClass/partNumber and every
    // connection's role/engagementLength/compatibilityTag already round-trip — see #488) plus a
    // small .json sidecar summarizing that same metadata, so a future library browser (A5) can
    // filter by size/class/type without opening the DWG. The DWG trailer stays authoritative;
    // the sidecar is a convenience index only.
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty()) {
      log.push_back("LIBEXPORT — usage: LIBEXPORT <name>[, <path.dwg>].");
      return true;
    }
    const int di = CadBlockFindDef(st.drawing.blockDefs, f[0]);
    if (di < 0) {
      log.push_back("LIBEXPORT — no block named \"" + f[0] + "\".");
      return true;
    }
    const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
    if (def.partType == CadPipePartType::None) {
      log.push_back("LIBEXPORT — \"" + f[0] + "\" has no fitting metadata; tag it with BLOCKFITTING first.");
      return true;
    }
    namespace fs = std::filesystem;
    fs::path dwgPath;
    if (f.size() >= 2 && !f[1].empty()) {
      dwgPath = fs::u8path(f[1]);
    } else {
      const fs::path dir = CadFittingLibraryExportDir();
      if (dir.empty()) {
        log.push_back("LIBEXPORT — could not determine the user library directory; pass an explicit path.");
        return true;
      }
      std::error_code mkEc;
      fs::create_directories(dir, mkEc);
      dwgPath = dir / (def.name + ".dwg");
    }
    AppCommandState tmp;
    tmp.drawing.blockDefs.push_back(def);
    if (!ExportDwgFile(tmp, dwgPath.u8string().c_str(), log)) {
      log.push_back("LIBEXPORT — could not write " + dwgPath.u8string() + ".");
      return true;
    }
    fs::path sidecar = dwgPath;
    sidecar.replace_extension(".json");
    std::ofstream out(sidecar, std::ios::binary);
    if (out) {
      nlohmann::json j;
      j["name"] = def.name;
      j["partType"] = std::string(CadPipePartTypeTag(def.partType));
      j["nominalSize"] = def.nominalSize;
      if (def.pressureClass != CadPipePressureClass::None)
        j["pressureClass"] = std::string(CadPipePressureClassTag(def.pressureClass));
      if (!def.partNumber.empty())
        j["partNumber"] = def.partNumber;
      nlohmann::json conns = nlohmann::json::array();
      for (const CadBlockConnection& c : def.connections) {
        nlohmann::json cj;
        cj["name"] = c.name;
        cj["role"] = std::string(CadBlockConnectionRoleTag(c.role));
        cj["nominalSize"] = c.nominalSize;
        conns.push_back(std::move(cj));
      }
      j["connections"] = std::move(conns);
      out << j.dump(2);
    } else {
      log.push_back("LIBEXPORT — wrote the DWG but could not write the metadata sidecar " + sidecar.u8string() + ".");
    }
    log.push_back("LIBEXPORT — wrote \"" + def.name + "\" to " + dwgPath.u8string() + ".");
    return true;
  }

  if (tok == "blockimport") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty() || f[0].empty()) {
      CadBlocksImportWithPicker(st, log);
      return true;
    }
    PushUndoSnapshot(st, "Blockimport");
    ImportCadBlocksFromPath(st, f[0].c_str(), log);
    BumpCadGpuCache(st);
    return true;
  }

  if (tok == "bgrip") {
    log.push_back("BGRIP — stretch/move/rotate/flip grips follow BPARAM values on the selected INSERT.");
    return true;
  }

  if (tok == "makeblock") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty()) {
      log.push_back("MAKEBLOCK — usage: MAKEBLOCK <name>.");
      return true;
    }
    if (CadBlockFindDef(st.drawing.blockDefs, f[0]) >= 0) {
      log.push_back("MAKEBLOCK — name \"" + f[0] + "\" is already used.");
      return true;
    }
    PushUndoSnapshot(st, "Makeblock");
    CadBlockDefinition def;
    def.name = f[0];
    def.units = CadDrawingInsUnitsName(st.drawing.drawingInsUnits);
    st.drawing.blockDefs.push_back(std::move(def));
    log.push_back("MAKEBLOCK — created \"" + f[0] + "\".");
    return true;
  }

  if (tok == "mkline") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() != 4) {
      log.push_back("MKLINE — usage: MKLINE <x0>, <y0>, <x1>, <y1>.");
      return true;
    }
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    if (!TryParseF(f[0], x0) || !TryParseF(f[1], y0) ||
        !TryParseF(f[2], x1) || !TryParseF(f[3], y1)) {
      log.push_back("MKLINE — coordinates must be numbers.");
      return true;
    }
    PushUndoSnapshot(st, "Mkline");
    st.drawing.pushLine(CadLineRecord{x0, y0, 0.0, x1, y1, 0.0});
    st.drawing.userLineAttrs.push_back(NewBlockAttr(st));
    EnsureEntityIds(st);
    BumpCadGpuCache(st);
    log.push_back("MKLINE — added.");
    return true;
  }

  if (tok == "selline") {
    st.selection.clear();
    const int n = static_cast<int>(st.drawing.lineCount());
    if (n <= 0) {
      log.push_back("SELLINE — no lines.");
      return true;
    }
    st.selection.push_back({SelectedEntity::Type::LineSeg, n - 1});
    log.push_back("SELLINE — line " + std::to_string(n - 1) + ".");
    return true;
  }

  if (tok == "selblock") {
    st.selection.clear();
    if (st.drawing.cadBlockRefs.empty()) {
      log.push_back("SELBLOCK — no block references.");
      return true;
    }
    st.selection.push_back(
        {SelectedEntity::Type::BlockRef, static_cast<int>(st.drawing.cadBlockRefs.size()) - 1});
    log.push_back("SELBLOCK — insert " + std::to_string(st.drawing.cadBlockRefs.size() - 1) + ".");
    return true;
  }

  if (tok == "movesel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() != 2) {
      log.push_back("MOVESEL — usage: MOVESEL <dx>, <dy>.");
      return true;
    }
    float dx = 0.f, dy = 0.f;
    if (!TryParseF(f[0], dx) || !TryParseF(f[1], dy)) {
      log.push_back("MOVESEL — offsets must be numbers.");
      return true;
    }
    PushUndoSnapshot(st, "Movesel");
    for (const SelectedEntity& e : st.selection) {
      if (e.type != SelectedEntity::Type::BlockRef)
        continue;
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
        continue;
      CadBlockTranslate(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], dx, dy, 0.f);
    }
    BumpCadGpuCache(st);
    log.push_back("MOVESEL — done.");
    return true;
  }

  if (tok == "copysel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() != 2) {
      log.push_back("COPYSEL — usage: COPYSEL <dx>, <dy>.");
      return true;
    }
    float dx = 0.f, dy = 0.f;
    if (!TryParseF(f[0], dx) || !TryParseF(f[1], dy)) {
      log.push_back("COPYSEL — offsets must be numbers.");
      return true;
    }
    PushUndoSnapshot(st, "Copysel");
    int n = 0;
    std::vector<CadBlockRef> extra;
    std::vector<EntityAttributes> extraA;
    for (const SelectedEntity& e : st.selection) {
      if (e.type != SelectedEntity::Type::BlockRef)
        continue;
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
        continue;
      CadBlockRef c = st.drawing.cadBlockRefs[static_cast<size_t>(e.index)];
      CadBlockTranslate(&c, dx, dy, 0.f);
      extra.push_back(std::move(c));
      EntityAttributes a = NewBlockAttr(st);
      if (static_cast<size_t>(e.index) < st.drawing.cadBlockRefAttrs.size())
        a = st.drawing.cadBlockRefAttrs[static_cast<size_t>(e.index)];
      a.id = 0;
      extraA.push_back(std::move(a));
      ++n;
    }
    st.drawing.cadBlockRefs.insert(st.drawing.cadBlockRefs.end(), extra.begin(), extra.end());
    st.drawing.cadBlockRefAttrs.insert(st.drawing.cadBlockRefAttrs.end(), extraA.begin(), extraA.end());
    EnsureEntityIds(st);
    BumpCadGpuCache(st);
    log.push_back("COPYSEL — " + std::to_string(n) + " reference(s).");
    return true;
  }

  if (tok == "rotatesel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() != 3) {
      log.push_back("ROTATESEL — usage: ROTATESEL <baseX>, <baseY>, <deg>.");
      return true;
    }
    float bx = 0.f, by = 0.f, degrees = 0.f;
    if (!TryParseF(f[0], bx) || !TryParseF(f[1], by) || !TryParseF(f[2], degrees)) {
      log.push_back("ROTATESEL — values must be numbers.");
      return true;
    }
    const float rad = degrees * 0.01745329252f;
    PushUndoSnapshot(st, "Rotatesel");
    for (const SelectedEntity& e : st.selection) {
      if (e.type != SelectedEntity::Type::BlockRef)
        continue;
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
        continue;
      CadBlockRotateZ(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], bx, by, rad);
    }
    BumpCadGpuCache(st);
    log.push_back("ROTATESEL — done.");
    return true;
  }

  if (tok == "scalesel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() != 3) {
      log.push_back("SCALESEL — usage: SCALESEL <baseX>, <baseY>, <factor>.");
      return true;
    }
    float bx = 0.f, by = 0.f, sc = 0.f;
    if (!TryParseF(f[0], bx) || !TryParseF(f[1], by) || !TryParseF(f[2], sc)) {
      log.push_back("SCALESEL — values must be numbers.");
      return true;
    }
    PushUndoSnapshot(st, "Scalesel");
    for (const SelectedEntity& e : st.selection) {
      if (e.type != SelectedEntity::Type::BlockRef)
        continue;
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
        continue;
      CadBlockScaleAbout(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], bx, by, sc);
    }
    BumpCadGpuCache(st);
    log.push_back("SCALESEL — done.");
    return true;
  }

  if (tok == "mirrorsel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() != 4) {
      log.push_back("MIRRORSEL — usage: MIRRORSEL <x0>, <y0>, <x1>, <y1>.");
      return true;
    }
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    if (!TryParseF(f[0], x0) || !TryParseF(f[1], y0) ||
        !TryParseF(f[2], x1) || !TryParseF(f[3], y1)) {
      log.push_back("MIRRORSEL — coordinates must be numbers.");
      return true;
    }
    PushUndoSnapshot(st, "Mirrorsel");
    for (const SelectedEntity& e : st.selection) {
      if (e.type != SelectedEntity::Type::BlockRef)
        continue;
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
        continue;
      CadBlockMirror(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], x0, y0, x1, y1);
    }
    BumpCadGpuCache(st);
    log.push_back("MIRRORSEL — done.");
    return true;
  }

  if (tok == "undo") {
    DoUndo(st, log);
    return true;
  }
  if (tok == "redo") {
    DoRedo(st, log);
    return true;
  }

  if (tok == "copyclip") {
    CopySelectionToClipboard(st, log);
    return true;
  }

  if (tok == "pasteblock") {
    const std::vector<std::string> f = SplitCommaRest(args);
    float dx = 0.f;
    float dy = 0.f;
    if (!f.empty() && (f.size() != 2 || !TryParseF(f[0], dx) || !TryParseF(f[1], dy))) {
      log.push_back("PASTEBLOCK — offsets must be numbers.");
      return true;
    }
    // Issue #805: Paste Special's menu runs this command from the ribbon, so it takes the same guards as
    // PASTE and PASTEORIG. A stale layout index refuses (as Cut does), and a coordinate-system mismatch refuses
    // before any change. A paper layout receives the references on its sheet, as PASTEORIG does.
    PaperLayout* paperTarget = ActivePaperGeometryTarget(st);
    if (paperTarget == nullptr && st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
      log.push_back("PASTEBLOCK — the active layout is not available. Switch to model space first.");
      return true;
    }
    if (st.clipboard.content.cadBlockRefs.empty()) {
      log.push_back("PASTEBLOCK — clipboard has no block references.");
      return true;
    }
    const PasteCheck pc = CheckClipboardPaste(st);
    if (pc.verdict != PasteCheck::Verdict::Ok) {
      // This command has no warning dialog, so it cannot offer "Paste anyway" or Convert. The message says so.
      log.push_back(std::string("PASTEBLOCK — not pasted (") + (pc.verdict == PasteCheck::Verdict::Block ? "blocked" : "needs a check") +
                    "): " + pc.text +
                    " Paste (Ctrl+V) shows the warning, but it pastes the whole clipboard, not only block references.");
      return true;
    }
    auto defs = st.clipboard.content.blockDefs;
    auto refs = st.clipboard.content.cadBlockRefs;
    if (const std::string* missing = CadBlockPreparePaste(st.drawing.blockDefs, defs, refs)) {
      log.push_back("PASTEBLOCK — missing clipboard definition \"" + *missing + "\".");
      return true;
    }
    PushUndoSnapshot(st, "Pasteblock");
    for (CadBlockDefinition& def : defs)
      st.drawing.blockDefs.push_back(std::move(def));
    for (size_t i = 0; i < refs.size(); ++i) {
      CadBlockRef& r = refs[i];
      CadBlockTranslate(&r, dx, dy, 0.f);
      // REQ-076: a paste gets new entity ids, so the copied source's id is cleared on both sheet and model.
      EntityAttributes a = i < st.clipboard.content.cadBlockRefAttrs.size() ? st.clipboard.content.cadBlockRefAttrs[i]
                           : paperTarget != nullptr              ? EntityAttributes{}
                                                                 : NewBlockAttr(st);
      a.id = 0;
      if (paperTarget != nullptr) {
        paperTarget->paperBlockRefs.push_back(std::move(r));
        paperTarget->paperBlockRefAttrs.push_back(std::move(a));
        continue;
      }
      st.drawing.cadBlockRefs.push_back(std::move(r));
      st.drawing.cadBlockRefAttrs.push_back(std::move(a));
    }
    if (paperTarget == nullptr)
      EnsureEntityIds(st);
    BumpCadGpuCache(st);
    log.push_back("PASTEBLOCK — " + std::to_string(st.clipboard.content.cadBlockRefs.size()) + " reference(s).");
    return true;
  }

  if (tok == "blockmodel") {
    st.activeSpaceIndex = kModelSpaceIndex;
    log.push_back("BLOCKMODEL — model space.");
    return true;
  }
  if (tok == "blockpaper") {
    if (st.drawing.paperLayouts.empty())
      AddPaperLayout(st);
    if (st.drawing.paperLayouts.empty()) {
      log.push_back("BLOCKPAPER — no layouts.");
      return true;
    }
    st.activeSpaceIndex = 0;
    log.push_back("BLOCKPAPER — " + st.drawing.paperLayouts[0].name);
    return true;
  }

  return false;
}
