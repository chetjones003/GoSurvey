#include "CadBlocks.hpp"
#include "blocks/CadBlocksInternal.hpp"
#include "blocks/CadBlockCreateInternal.hpp"
#include "connect/CadBlockConnectInternal.hpp"
#include "CadCommands.hpp"
#include "CadRubberPreview.hpp"
#include "ProjectWarnings.hpp"  // REQ-383: CheckClipboardPaste, the paste guard PASTEBLOCK shares with PASTE
#include <GoSurvey/Core/StringUtil.hpp>
#include <GoSurvey/Renderer/CadSolidDisplay.hpp>
#include "GoSurvey/Document/cadpiperun.hpp"  // CadPipeRunEndPorts (issue #486, "pipe end" connection port)
#include "util/solidpick.hpp"
#include "GsIo.hpp"
#include "DxfIo.hpp"
#include "DwgIo.hpp"
#include "AppPaths.hpp"
#include <GoSurvey/UI/FileDialogs.hpp>
#include "io/src/AcisSatParser.hpp"

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

namespace {

/// INSERT rotation is entered in the app's angle convention — 0° = north, **clockwise positive**
/// (CadCommands.hpp; same as ROTATE, which negates for the same reason). The block frame is the
/// internal CCW-from-east math frame, so a clockwise bearing negates. `xf.rotZ = 0` therefore
/// the cursor (issue #475 increment 3).
float InsertLiveScaleDist(const AppCommandState& st, float wx, float wy, float wz) {
  float lx = wx;
  float ly = wy;
  float lz = wz;
  ApplyOrthoConstrainFromAnchor(st, st.insertBlockX, st.insertBlockY, &lx, &ly, st.orthoMode, st.insertBlockZ,
                                st.uiCursorWorldZ, &lz);
  const float dx = lx - st.insertBlockX;
  const float dy = ly - st.insertBlockY;
  const float dz = lz - st.insertBlockZ;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Bearing from the committed insertion point to \p wx,\p wy in the clockwise-from-north
/// convention (after ORTHO), used for the live on-screen rotation pick. Returns the current stored
/// angle when the cursor sits on the insertion point. Shared by commit and preview.
float InsertLiveRotDeg(const AppCommandState& st, float wx, float wy) {
  float lx = wx;
  float ly = wy;
  ApplyOrthoConstrainFromAnchor(st, st.insertBlockX, st.insertBlockY, &lx, &ly, st.orthoMode, st.insertBlockZ,
                                st.uiCursorWorldZ);
  const float dx = lx - st.insertBlockX;
  const float dy = ly - st.insertBlockY;
  if (dx * dx + dy * dy <= 1.e-10f)
    return st.insertBlockRotDeg;
  return BearingCwNorthDegFromMathAngleRad(std::atan2(dy, dx));
}

void CadBlockCaptureDrawing(const AppCommandState& st, CadBlockContent* c) {
  assert(c != nullptr);
  // The entity collections are one shared store (ADR-0010, #1557 phase 3): a single slice copy.
  static_cast<CadEntityStore&>(*c) = static_cast<const CadEntityStore&>(st.drawing);
  c->lineVis.assign(st.drawing.userLineAttrs.size(), "");
  c->circleVis.assign(st.drawing.userCircleAttrs.size(), "");
  EnsureCircleNormals(c->userCircleNormals, st.drawing.circleCount());
  for (const CadBlockRef& r : st.drawing.cadBlockRefs) {
    CadBlockNested n;
    n.defName = r.defName;
    n.xf = r.xf;
    c->nested.push_back(std::move(n));
  }
}

/// ADR-043: load a definition's primitive geometry into the model arrays for in-place editing.
/// Non-primitive parts of the definition (nested blocks, meshes, parameters, actions, attribute
/// defs) are left on the definition and restored verbatim on close — this editor edits primitives.
/// Everything else in the model store is cleared so the viewport, picking and snapping see only
/// the block (isolation, ADR-043 (b)).
void LoadBlockPrimitivesIntoDrawing(AppCommandState& st, const CadBlockContent& c) {
  // The block's entity store replaces the model's (ADR-0010, #1557 phase 3): a single slice copy,
  // then the attribute arrays are padded to their entity counts as before.
  static_cast<CadEntityStore&>(st.drawing) = static_cast<const CadEntityStore&>(c);
  st.drawing.userLineAttrs.resize(st.drawing.lineCount());
  st.drawing.userCircleAttrs.resize(st.drawing.circleCount());
  EnsureCircleNormals(st.drawing.userCircleNormals, st.drawing.circleCount());
  st.drawing.userArcAttrs.resize(st.drawing.userArcs.size());
  st.drawing.userEllAttrs.resize(st.drawing.userEllipses.size());
  SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());  // legacy block defs have none
  st.drawing.cadAnnotationAttrs.resize(st.drawing.cadAnnotations.size());
  st.drawing.cadMeshAttrs.resize(st.drawing.cadMeshes.size());
  st.drawing.cadSolidAttrs.resize(st.drawing.cadSolids.size());
  // Hide everything that is not the block being edited.
  st.drawing.cadFilledRegions.clear();
  st.drawing.cadFilledRegionAttrs.clear();
  st.drawing.cadSurfaces.clear();
  st.drawing.cadSurfaceAttrs.clear();
  st.drawing.cadTables.clear();
  st.drawing.cadTableAttrs.clear();
  st.drawing.cadPositionMarkers.clear();  // REQ-359: model-space objects, never block content
  st.drawing.cadPositionMarkerAttrs.clear();
  st.drawing.cadMultileaders.clear();  // REQ-367: model-space objects, never block content
  st.drawing.cadMultileaderAttrs.clear();
  st.drawing.cadSplines.clear();  // REQ-443: model-space objects, never block content
  st.drawing.cadSplineAttrs.clear();
  st.drawing.cadPoints.clear();  // REQ-456: model-space objects, never block content
  st.drawing.cadPointAttrs.clear();
  // Pipe runs (issue #486 / REQ-345) are model-space entities, never block content, so they hide
  // the same way cadTables/cadSurfaces above do — a run drawn in the main drawing is not the block
  // being edited. pipeRunWorldSolidsSig is left stale on purpose: it is a content hash, and this
  // clear changes the content, so RefreshSolidDisplayGeometry's next pass re-derives it naturally.
  st.drawing.cadPipeRuns.clear();
  st.drawing.cadPipeRunAttrs.clear();
  st.drawing.cadPipingSystems.clear();  // issue #486 increment B3 / REQ-345 — same reason as cadPipeRuns above
  st.drawing.alignments.clear();  // REQ-480: model-space objects, never block content
  st.drawing.sampleLines.clear();  // REQ-519: sample lines belong to the alignments just removed
  st.drawing.profiles.clear();  // REQ-521: profiles belong to the alignments just removed
  st.drawing.cadBlockRefs.clear();
  st.drawing.cadBlockRefAttrs.clear();
  st.drawing.surveyPoints.clear();
  st.drawing.featureLineOffsets.clear();
  st.drawing.featureLineVerts.clear();
  st.drawing.featureLineClosed.clear();
  st.drawing.featureLineElevPt.clear();
  st.drawing.featureLineInfo.clear();
  st.drawing.featureLineAttrs.clear();
}

/// ADR-043: harvest the model arrays back into a definition's primitive geometry on Save. Leaves
/// \c nested and the dynamic-block authoring model (parameters/actions/attrDefs) untouched — those
/// are edited through their own commands, not this surface.
bool DrawingHasCaptureableGeometry(const AppCommandState& st) {
  return !st.drawing.userLinesFlat.empty() || !st.drawing.userCirclesCxCyZR.empty() || !st.drawing.userArcs.empty() ||
         !st.drawing.userEllipses.empty() || st.drawing.userPolylineOffsets.size() >= 2 || !st.drawing.cadAnnotations.empty() ||
         !st.drawing.cadMeshes.empty() || !st.drawing.cadSolids.empty() || !st.importedDxfAttrDefs.empty();
}

std::string FileStemUtf8(const char* pathUtf8) {
  const std::filesystem::path p = std::filesystem::u8path(pathUtf8 ? pathUtf8 : "");
  return p.stem().u8string();
}

std::string LowerExt(const char* pathUtf8) {
  std::string e = std::filesystem::u8path(pathUtf8 ? pathUtf8 : "").extension().u8string();
  for (char& c : e) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return e;
}

int MergeBlockDef(AppCommandState& dest, CadBlockDefinition def, std::vector<std::string>& log) {
  if (def.name.empty())
    return 0;
  const int i = CadBlockFindDef(dest.drawing.blockDefs, def.name);
  if (i >= 0) {
    CadBlockDefinition& have = dest.drawing.blockDefs[static_cast<size_t>(i)];
    const bool upgradeMatchline =
        CadBlockNameIsMatchline(def.name) &&
        (have.attrDefs.size() < def.attrDefs.size() || have.content.cadAnnotations.size() < def.content.cadAnnotations.size());
    const bool upgradeSatSolid = !def.content.cadSolids.empty() &&
                                 (have.content.cadSolids.empty() || have.units != def.units);
    if (!upgradeMatchline && !upgradeSatSolid) {
      log.push_back("BLOCKIMPORT — skipped duplicate \"" + def.name + "\".");
      return 0;
    }
    have = std::move(def);
    return 1;
  }
  dest.drawing.blockDefs.push_back(std::move(def));
  return 1;
}

/// A standalone ACIS `.sat` file (Civil 3D / AutoCAD `ACISOUT`, GitHub issue #473) — one or more 3D
/// solids and nothing else. Reads the solid into \p scratch, re-based so it sits on the origin.
bool ImportSatFileToScratch(AppCommandState& scratch, const char* pathUtf8, std::vector<std::string>& log) {
  std::ifstream f(std::filesystem::u8path(pathUtf8), std::ios::binary);
  if (!f.is_open()) {
    log.push_back("BLOCKIMPORT — cannot open \"" + std::string(pathUtf8) + "\".");
    return false;
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  const acissat::ImportResult r = acissat::ImportSatSolid(ss.str(), FileStemUtf8(pathUtf8));
  if (!r.ok) {
    log.push_back("BLOCKIMPORT — " + (r.error.empty() ? std::string("the ACIS SAT file could not be read") : r.error));
    return false;
  }
  // A `.sat` carries the model's absolute position from the drawing it was exported out of (the
  // ACIS `body` transform's translation — often thousands of units from the origin). Re-base the
  // solid so it sits ON the origin — centred in X/Y, its lowest point at Z 0 — a predictable
  // reference for MOVE-ing it into place (GitHub issue #473). The transform's orientation is kept.
  brep::Solid based = r.solid;
  const brep::Bounds bb = brep::ComputeBounds(based);
  if (bb.valid)
    based = brep::Translate(
        based, brep::Vec3{-(bb.mn.x + bb.mx.x) * 0.5, -(bb.mn.y + bb.mx.y) * 0.5, -bb.mn.z});
  scratch.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(based)));
  scratch.drawing.cadSolidAttrs.push_back(EntityAttributes{});
  // Keep the block definition units-neutral: a `.sat` header's millimetres-per-model-unit is
  // unreliable in practice (Civil 3D wrote 25.4 for a foot-scaled model), and INSERT scaling a
  // solid it cannot place has no benefit. The solid imports at the file's own coordinates.
  scratch.drawing.drawingInsUnits = 0;
  log.push_back("BLOCKIMPORT — imported an ACIS solid from \"" + std::string(pathUtf8) + "\".");
  return true;
}

/// The piping metadata in \p libraryFile's same-stem `.json` sidecar (`LIBEXPORT` writes one; a part
/// dropped in by hand needs one — resources/blocks/fittings/README.md). False when there is no
/// readable sidecar; the outputs are then untouched.
bool ReadFittingSidecar(const std::filesystem::path& libraryFile, CadPipePartType* partType,
                        std::string* nominalSize, CadPipePressureClass* pressureClass) {
  std::filesystem::path sidecar = libraryFile;
  sidecar.replace_extension(".json");
  std::error_code ec;
  if (!std::filesystem::is_regular_file(sidecar, ec))
    return false;
  std::ifstream in(sidecar, std::ios::binary);
  if (!in)
    return false;
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  try {
    const nlohmann::json j = nlohmann::json::parse(text);
    *partType = ParseCadPipePartType(j.value("partType", std::string()));
    *nominalSize = j.value("nominalSize", std::string());
    *pressureClass = ParseCadPipePressureClass(j.value("pressureClass", std::string()));
    return true;
  } catch (...) {
    return false;  // malformed sidecar — the part still imports, just without filter metadata
  }
}

int ImportCadBlocksFromPathImpl(AppCommandState& dest, const char* pathUtf8, std::vector<std::string>& log,
                                bool dropLooseSatGeometry) {
  if (!pathUtf8 || pathUtf8[0] == '\0') {
    log.push_back("BLOCKIMPORT — no path.");
    return -1;
  }
  const std::string ext = LowerExt(pathUtf8);
  AppCommandState scratch;
  bool ok = false;
  if (ext == ".dxf")
    ok = ImportDxfFile(scratch, pathUtf8, log);
  else if (ext == ".dwg") {
    const size_t logStart = log.size();
    ok = ImportDwgFile(scratch, pathUtf8, log);
    // The source file's GEODATA (REQ-362) set the scratch drawing's location, not this one's.
    log.erase(std::remove_if(log.begin() + static_cast<std::ptrdiff_t>(logStart), log.end(),
                             [](const std::string& l) { return l.rfind("GEODATA", 0) == 0; }),
              log.end());
  } else if (ext == ".sat")
    ok = ImportSatFileToScratch(scratch, pathUtf8, log);
  else {
    // Legacy `.gs` block-library import was removed by issue #264 (D-2026-09-03-h); re-adding a
    // block-library container is tracked as issue #284, not part of this one. This is historical
    // compatibility context, not a supported drawing-file path.
    log.push_back("BLOCKIMPORT — expected a .dxf, .dwg or .sat file.");
    return -1;
  }
  if (!ok)
    return -1;
  // Importing a standalone `.sat` while BEDIT is open (issue #486 increment A4) is authoring INTO
  // the block already being edited, not adding a new library part — the loose-geometry drop below
  // already delivers the solid straight into that block's content (ADR-043 aliases dest.cadSolids
  // to it during a block-edit session). Skipping the wrap-definition branch below avoids also
  // spawning a redundant second, unrelated block definition with the same content every time.
  const bool skipWrapDuringBedit = ext == ".sat" && dest.blockEditActive && dropLooseSatGeometry;
  int n = 0;
  for (CadBlockDefinition& d : scratch.drawing.blockDefs)
    n += MergeBlockDef(dest, std::move(d), log);
  if (DrawingHasCaptureableGeometry(scratch) && !skipWrapDuringBedit) {
    CadBlockDefinition wrap;
    wrap.name = FileStemUtf8(pathUtf8);
    // A standalone `.sat` is re-based into the current drawing's model units (feet for a feet
    // drawing). Tag the block the same way so INSERT does not apply an inch→foot rescale (issue
    // #475). DXF/DWG keep the imported file's $INSUNITS when present.
    wrap.units = (ext == ".sat") ? CadDrawingInsUnitsName(dest.drawing.drawingInsUnits)
                                 : CadDrawingInsUnitsName(scratch.drawing.drawingInsUnits);
    wrap.attrDefs = scratch.importedDxfAttrDefs;
    CadBlockCaptureDrawing(scratch, &wrap.content);
    CadBlockBakeBasePoint(&wrap);
    n += MergeBlockDef(dest, std::move(wrap), log);
  }
  // A `.sat` is a single model, not a block library, and INSERT cannot place a 3D solid (issue
  // #473). Drop its solid straight into the drawing, re-based onto the origin, so the user can see
  // it and MOVE it into position; the block definition above is kept for a future 3D INSERT.
  // During BEDIT this instead lands the solid directly in the block being authored (see above).
  if (ext == ".sat" && dropLooseSatGeometry) {
    for (std::size_t i = 0; i < scratch.drawing.cadSolids.size(); ++i) {
      dest.drawing.cadSolids.push_back(scratch.drawing.cadSolids[i]);
      dest.drawing.cadSolidAttrs.push_back(i < scratch.drawing.cadSolidAttrs.size() ? scratch.drawing.cadSolidAttrs[i]
                                                                    : EntityAttributes{});
    }
  }
  for (CadBlockDefinition& d : dest.drawing.blockDefs)
    CadBlockAuthorMatchlineDynamics(&d);
  // A part that came in without piping metadata of its own takes its sidecar's. Without this the
  // bundled startup sweep (which imports `fittings/*.sat`) turns a tagged 4in flange into an untyped
  // block, and the library scan then skips its sidecar because the name is already in the drawing —
  // so the Pipe Fittings palette could never list it (issue #1084).
  {
    CadPipePartType sidecarType = CadPipePartType::None;
    std::string sidecarSize;
    CadPipePressureClass sidecarClass = CadPipePressureClass::None;
    const int di = CadBlockFindDef(dest.drawing.blockDefs, FileStemUtf8(pathUtf8));
    if (di >= 0 && dest.drawing.blockDefs[static_cast<size_t>(di)].partType == CadPipePartType::None &&
        ReadFittingSidecar(std::filesystem::u8path(pathUtf8), &sidecarType, &sidecarSize, &sidecarClass) &&
        sidecarType != CadPipePartType::None) {
      CadBlockDefinition& d = dest.drawing.blockDefs[static_cast<size_t>(di)];
      d.partType = sidecarType;
      d.nominalSize = sidecarSize;
      d.pressureClass = sidecarClass;
    }
  }
  log.push_back("BLOCKIMPORT — imported " + std::to_string(n) + " definition(s) from " +
                std::string(pathUtf8) + ".");
  return n;
}

void LoadBundledBlockLibraryImpl(AppCommandState& dest, std::vector<std::string>& log) {
  namespace fs = std::filesystem;
  fs::path dir = ResolveBundledAssetPath(fs::path("resources") / "blocks");
  if (dir.empty())
    return;
  if (fs::is_regular_file(dir))
    dir = dir.parent_path();
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return;
  std::vector<fs::path> files;
  for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!e.is_regular_file(ec))
      continue;
    const std::string ext = LowerExt(e.path().u8string().c_str());
    // .sat is deliberately not globbed here: a bundled-library sweep must not drop a loose solid
    // into the user's drawing (the .sat branch of ImportCadBlocksFromPathImpl does). BLOCKIMPORT
    // of a .sat is always an explicit user action.
    if (ext == ".dxf" || ext == ".dwg")
      files.push_back(e.path());
  }
  const fs::path fitDir = dir / "fittings";
  if (fs::is_directory(fitDir, ec)) {
    for (const fs::directory_entry& e : fs::directory_iterator(fitDir, ec)) {
      if (ec)
        break;
      if (!e.is_regular_file(ec))
        continue;
      if (LowerExt(e.path().u8string().c_str()) == ".sat")
        files.push_back(e.path());
    }
  }
  std::sort(files.begin(), files.end());
  int n = 0;
  for (const fs::path& p : files) {
    std::vector<std::string> ignored;
    const bool dropLoose = LowerExt(p.u8string().c_str()) != ".sat";
    const int k = ImportCadBlocksFromPathImpl(dest, p.u8string().c_str(), ignored, dropLoose);
    if (k > 0)
      n += k;
  }
  if (n > 0)
    log.push_back("Block library — loaded " + std::to_string(n) + " bundled definition(s).");
}

bool PlaceInsertImpl(AppCommandState& st, std::string_view name, CadBlockXform xf, bool explode,
                     std::vector<std::string>& log, bool pushUndo = true,
                     const EntityAttributes* lineAttrs = nullptr) {
  const int di = CadBlockFindDef(st.drawing.blockDefs, name);
  if (di < 0) {
    log.push_back("INSERT — no block named \"" + std::string(name) + "\".");
    return false;
  }
  CadBlockRef r;
  r.defName = st.drawing.blockDefs[static_cast<size_t>(di)].name;
  r.xf = xf;
  if (pushUndo)
    PushUndoSnapshot(st, "Insert");
  const float us = CadBlockInsertUnitsScale(st, st.drawing.blockDefs[static_cast<size_t>(di)]);
  r.xf.sx *= us;
  r.xf.sy *= us;
  r.xf.sz *= us;
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  for (const CadBlockAttrDef& ad : def.attrDefs)
    CadBlockAttrSet(&r, ad.tag, ad.defaultValue);
  EntityAttributes attr = NewBlockAttr(st);
  if (lineAttrs) {  // a pipe fitting takes its run's layer and colour (REQ-353)
    attr.layer = lineAttrs->layer;
    attr.color = lineAttrs->color;
  } else if (def.partType != CadPipePartType::None) {
    // A catalogue part off any run: the Pipe fitting row (REQ-361, D-2026-09-29-f).
    attr.layer = EnsureObjectLayer(st, ObjectLayerKind::PipeFitting, def.name);
  }
  PaperLayout* const paperTarget = ActivePaperGeometryTarget(st);
  const bool paper = paperTarget != nullptr;
  if (paper && explode) {
    log.push_back("INSERT — explode is model space only; placing as a reference.");
    explode = false;
  }
  if (paper) {
    paperTarget->paperBlockRefs.push_back(r);
    paperTarget->paperBlockRefAttrs.push_back(attr);
  } else {
    st.drawing.cadBlockRefs.push_back(r);
    st.drawing.cadBlockRefAttrs.push_back(attr);
    if (explode) {
      ExplodeRef(st, st.drawing.cadBlockRefs.back(), st.drawing.cadBlockRefAttrs.back());
      st.drawing.cadBlockRefs.pop_back();
      st.drawing.cadBlockRefAttrs.pop_back();
    }
  }
  if (!def.content.cadSolids.empty() && !CadBlockXformScaleIsUniform(r.xf))
    log.push_back("INSERT — \"" + def.name +
                  "\" carries a 3D solid; non-uniform scale is not supported for solid blocks.");
  NoteRecent(st, r.defName);
  EnsureEntityIds(st);
  BumpCadGpuCache(st);
  if (explode)
    log.push_back("INSERT — exploded \"" + r.defName + "\".");
  else
    log.push_back("INSERT — placed \"" + r.defName + "\".");
  return true;
}

} // namespace

bool ImportCadBlocksFromPath(AppCommandState& dest, const char* pathUtf8, std::vector<std::string>& log) {
  return ImportCadBlocksFromPathImpl(dest, pathUtf8, log, true) >= 0;
}

std::filesystem::path CadFittingLibraryExportDir() {
  const std::filesystem::path base = UserDataDirectory();
  if (base.empty())
    return {};
  return base / "blocks" / "fittings";
}

float CadBlockInsertUnitsScale(const AppCommandState& st, const CadBlockDefinition& def) {
  // REQ-357: "Scale objects inserted from other drawings" off → inserted as drawn.
  if (!st.drawing.drawingSettings.scaleInsertedObjects)
    return 1.f;
  const double inchesPerMeter = DrawingInchesPerMeter(st.drawing.drawingSettings.footDefinition);
  const std::string drawUnits = CadDrawingInsUnitsName(st.drawing.drawingInsUnits);
  if (st.insertBlockUnitsBuf[0] != '\0')
    return CadBlockUnitsScale(st.insertBlockUnitsBuf, drawUnits, inchesPerMeter);
  return CadBlockUnitsScale(def.units, drawUnits, inchesPerMeter);
}

void CadBlocksCollectLibraryEntries(const AppCommandState& st, std::vector<CadBlockLibraryEntry>* out) {
  assert(out != nullptr);
  out->clear();
  for (const CadBlockDefinition& d : st.drawing.blockDefs) {
    if (d.name.empty() || d.dynamicAnonymous)
      continue;
    CadBlockLibraryEntry e;
    e.name = d.name;
    e.imported = true;
    e.isFitting = d.partType != CadPipePartType::None;
    e.partType = d.partType;
    e.nominalSize = d.nominalSize;
    e.pressureClass = d.pressureClass;
    out->push_back(std::move(e));
  }
  namespace fs = std::filesystem;
  fs::path dir = ResolveBundledAssetPath(fs::path("resources") / "blocks");
  if (dir.empty())
    return;
  if (fs::is_regular_file(dir))
    dir = dir.parent_path();
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return;
  const auto addFile = [&](const fs::path& p, bool fitting) {
    if (!fs::is_regular_file(p, ec))
      return;
    std::string ext = p.extension().u8string();
    ext = StringUtil::toLowerAsciiCopy(ext);
    if (ext != ".dxf" && ext != ".dwg" && ext != ".sat")
      return;
    const std::string stem = p.stem().u8string();
    if (stem.empty() || CadBlockFindDef(st.drawing.blockDefs, stem) >= 0)
      return;
    CadBlockLibraryEntry e;
    e.name = stem;
    e.path = p.u8string();
    e.imported = false;
    e.isFitting = fitting;
    // LIBEXPORT (issue #486 increment A3) writes a same-stem `.json` sidecar summarizing the
    // fitting metadata — read it here so the library pane can filter/label without importing.
    // A malformed or missing sidecar leaves the entry listed, just without filter metadata.
    if (fitting)
      (void)ReadFittingSidecar(p, &e.partType, &e.nominalSize, &e.pressureClass);
    out->push_back(std::move(e));
  };
  for (const fs::directory_entry& ent : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (ent.is_regular_file(ec))
      addFile(ent.path(), false);
  }
  const fs::path fitDir = dir / "fittings";
  if (fs::is_directory(fitDir, ec)) {
    for (const fs::directory_entry& ent : fs::directory_iterator(fitDir, ec)) {
      if (ec)
        break;
      if (ent.is_regular_file(ec))
        addFile(ent.path(), true);
    }
  }
  // User-authored fittings exported via LIBEXPORT (issue #486 increment A3) — a second location
  // beside the bundled `resources/blocks/fittings`, so exporting a part makes it show up here too.
  const fs::path userFitDir = CadFittingLibraryExportDir();
  if (!userFitDir.empty() && fs::is_directory(userFitDir, ec)) {
    for (const fs::directory_entry& ent : fs::directory_iterator(userFitDir, ec)) {
      if (ec)
        break;
      if (ent.is_regular_file(ec))
        addFile(ent.path(), true);
    }
  }
  std::sort(out->begin(), out->end(), [](const CadBlockLibraryEntry& a, const CadBlockLibraryEntry& b) {
    if (a.imported != b.imported)
      return a.imported > b.imported;
    return StringUtil::toLowerAsciiCopy(a.name) < StringUtil::toLowerAsciiCopy(b.name);
  });
}

bool CadBlocksImportLibraryEntry(AppCommandState& st, const CadBlockLibraryEntry& entry,
                                 std::vector<std::string>& log) {
  if (entry.imported || entry.path.empty())
    return CadBlockFindDef(st.drawing.blockDefs, entry.name) >= 0;
  const int nBefore = static_cast<int>(st.drawing.blockDefs.size());
  const bool dropLoose = !entry.isFitting;
  if (ImportCadBlocksFromPathImpl(st, entry.path.c_str(), log, dropLoose) < 0)
    return false;
  return CadBlockFindDef(st.drawing.blockDefs, entry.name) >= 0 || static_cast<int>(st.drawing.blockDefs.size()) > nBefore;
}

bool CadBlocksImportWithPicker(AppCommandState& dest, std::vector<std::string>& log) {
  char buf[4096]{};
  if (!BrowseOpenFileBlockUtf8(buf, sizeof(buf))) {
    log.push_back("BLOCKIMPORT — cancelled.");
    return false;
  }
  PushUndoSnapshot(dest, "Blockimport");
  std::snprintf(dest.insertBlockPath, sizeof(dest.insertBlockPath), "%s", buf);
  ImportCadBlocksFromPath(dest, buf, log);
  BumpCadGpuCache(dest);
  return true;
}

bool CadBlockPlaceInsert(AppCommandState& st, std::string_view name, CadBlockXform xf, bool explode,
                         std::vector<std::string>& log) {
  // A part the connector snap fitted onto a pipe run's end joins that line (REQ-353).
  const int runIdx = st.insertBlockSnappedPipeRun;
  st.insertBlockSnappedPipeRun = -1;
  const EntityAttributes* lineAttrs =
      runIdx >= 0 && static_cast<size_t>(runIdx) < st.drawing.cadPipeRunAttrs.size()
          ? &st.drawing.cadPipeRunAttrs[static_cast<size_t>(runIdx)]
          : nullptr;
  return PlaceInsertImpl(st, name, xf, explode, log, /*pushUndo=*/true, lineAttrs);
}

bool CadBlockPlaceInsertNoUndo(AppCommandState& st, std::string_view name, CadBlockXform xf,
                               std::vector<std::string>& log, const EntityAttributes* lineAttrs) {
  return PlaceInsertImpl(st, name, xf, /*explode=*/false, log, /*pushUndo=*/false, lineAttrs);
}

void StartInsertBlockCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::None && st.active != AppCommandState::Kind::InsertBlock)
    CancelActiveCommand(st, log);
  LoadBundledBlockLibrary(st, log);
  stepengine::BeginSession(st, std::make_unique<stepengine::InsertBlockSession>(), /*recordAsLastCommand=*/true);
  st.insertBlockDialogOpen = true;
  st.insertBlockSpecifyPoint = true;
  st.insertBlockSpecifyScale = false;
  st.insertBlockSpecifyRot = true;
  st.insertBlockUniformScale = true;
  st.insertBlockExplode = false;
  st.insertBlockX = 0.f;
  st.insertBlockY = 0.f;
  st.insertBlockZ = 0.f;
  st.insertBlockSx = 1.f;
  st.insertBlockSy = 1.f;
  st.insertBlockSz = 1.f;
  st.insertBlockRotDeg = 0.f;
  st.insertBlockRotXDeg = 0.f;
  st.insertBlockRotYDeg = 0.f;
  st.insertBlockRotXBuf[0] = '\0';
  st.insertBlockRotYBuf[0] = '\0';
  st.insertBlockSpecifyAlignFace = false;
  st.insertBlockSpecifyConnectorSnap = false;
  st.insertBlockSnappedPipeRun = -1;
  st.insertBlockConnectorName[0] = '\0';
  st.insertBlockUnitsBuf[0] = '\0';
  st.insertBlockPath[0] = '\0';
  st.insertBlockName[0] = '\0';
  if (!st.blockRecent.empty())
    std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "%s", st.blockRecent.front().c_str());
  else if (!st.drawing.blockDefs.empty())
    std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "%s", st.drawing.blockDefs.front().name.c_str());
  std::snprintf(st.insertBlockAngleBuf, sizeof(st.insertBlockAngleBuf), "0");
  CadBlocksApplyInsertNameDefaults(st);
  log.push_back("INSERT — select a block in the Insert dialog.");
}

void CadBlocksApplyInsertNameDefaults(AppCommandState& st) {
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.insertBlockName);
  if (di < 0) {
    st.insertBlockUnitsBuf[0] = '\0';
    return;
  }
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  std::string units = def.units.empty() ? CadDrawingInsUnitsName(st.drawing.drawingInsUnits) : def.units;
  std::snprintf(st.insertBlockUnitsBuf, sizeof(st.insertBlockUnitsBuf), "%s", units.c_str());
}

namespace {


void ApplyInsertDialogRotations(const AppCommandState& st, CadBlockXform* xf) {
  assert(xf != nullptr);
  xf->rotX = CadBlockRotDegToRad(st.insertBlockRotXDeg);
  xf->rotY = CadBlockRotDegToRad(st.insertBlockRotYDeg);
  xf->rotZ = InsertRotZFromCwNorthDeg(st.insertBlockRotDeg);
}

CadBlockXform InsertDialogXform(const AppCommandState& st) {
  CadBlockXform xf;
  xf.x = st.insertBlockX;
  xf.y = st.insertBlockY;
  xf.z = st.insertBlockZ;
  xf.sx = st.insertBlockSx;
  xf.sy = st.insertBlockSy;
  xf.sz = st.insertBlockSz;
  ApplyInsertDialogRotations(st, &xf);
  return xf;
}


void ApplyInsertXformToDialog(const CadBlockXform& xf, AppCommandState& st) {
  st.insertBlockX = xf.x;
  st.insertBlockY = xf.y;
  st.insertBlockZ = xf.z;
  st.insertBlockSx = xf.sx;
  st.insertBlockSy = xf.sy;
  st.insertBlockSz = xf.sz;
  st.insertBlockRotXDeg = xf.rotX * 57.2957795f;
  st.insertBlockRotYDeg = xf.rotY * 57.2957795f;
  st.insertBlockRotDeg = -xf.rotZ * 57.2957795f;
  std::snprintf(st.insertBlockRotXBuf, sizeof(st.insertBlockRotXBuf), "%.4f",
                static_cast<double>(st.insertBlockRotXDeg));
  std::snprintf(st.insertBlockRotYBuf, sizeof(st.insertBlockRotYBuf), "%.4f",
                static_cast<double>(st.insertBlockRotYDeg));
  std::snprintf(st.insertBlockAngleBuf, sizeof(st.insertBlockAngleBuf), "%.4f",
                static_cast<double>(st.insertBlockRotDeg));
}

void InsertAdvanceAfterPoint(AppCommandState& st, std::vector<std::string>& log) {
  using Ph = stepengine::InsertBlockPhase;
  auto* sess = st.sessionAs<stepengine::InsertBlockSession>();
  if (st.insertBlockSpecifyAlignFace) {
    if (sess)
      sess->phase = Ph::WaitAlignFace;
    log.push_back("INSERT — pick a flat face to align the fitting (ESC cancels).");
    return;
  }
  if (st.insertBlockSpecifyScale) {
    if (sess)
      sess->phase = Ph::WaitScale;
    log.push_back("INSERT — specify scale (click a point).");
    return;
  }
  if (st.insertBlockSpecifyRot) {
    if (sess)
      sess->phase = Ph::WaitRotation;
    log.push_back("INSERT — specify rotation angle <0d0'0\"> (type degrees or click).");
    return;
  }
  if (CadBlockPlaceInsert(st, st.insertBlockName, InsertDialogXform(st), st.insertBlockExplode, log))
    CadBlocksAfterPlace(st, log);
}

void InsertAdvanceAfterScaleOrRot(AppCommandState& st, std::vector<std::string>& log) {
  using Ph = stepengine::InsertBlockPhase;
  if (st.insertBlockSpecifyRot) {
    if (auto* sess = st.sessionAs<stepengine::InsertBlockSession>())
      sess->phase = Ph::WaitRotation;
    log.push_back("INSERT — specify rotation angle <0d0'0\"> (type degrees or click).");
    return;
  }
  if (CadBlockPlaceInsert(st, st.insertBlockName, InsertDialogXform(st), st.insertBlockExplode, log))
    CadBlocksAfterPlace(st, log);
}

void InsertAdvanceAfterAlignFace(AppCommandState& st, std::vector<std::string>& log) {
  using Ph = stepengine::InsertBlockPhase;
  if (st.insertBlockSpecifyScale) {
    if (auto* sess = st.sessionAs<stepengine::InsertBlockSession>())
      sess->phase = Ph::WaitScale;
    log.push_back("INSERT — specify scale (click a point).");
    return;
  }
  InsertAdvanceAfterScaleOrRot(st, log);
}

} // namespace

namespace {

bool PickSolidFaceAcrossDrawing(const AppCommandState& st, const ray3d::Ray& ray, const solidpick::Tolerance& tol,
                                SelectedSubObject* out, solidpick::Pick* outPick) {
  if (!out)
    return false;
  bool any = false;
  double bestT = 0.0;
  SelectedSubObject best{};
  solidpick::Pick bestPick{};
  const auto trySolid = [&](const CadSolidPtr& sp, int solidIndex) {
    if (!sp)
      return;
    const auto ce = std::find_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                                 [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
    if (ce == st.solidDisplayCache.end() || ce->empty())
      return;
    solidpick::Pick p;
    if (!solidpick::PickSubObject(*sp, ce->triVerts, ce->triFaceIds, ray, tol, &p))
      return;
    if (p.kind != solidpick::Kind::Face)
      return;
    if (any && !(p.rayT < bestT))
      return;
    any = true;
    bestT = p.rayT;
    bestPick = p;
    best.solidIndex = solidIndex;
    best.kind = p.kind;
    best.index = p.index;
    best.owner = sp;
  };
  for (size_t i = 0; i < st.drawing.cadSolids.size(); ++i)
    trySolid(st.drawing.cadSolids[i], static_cast<int>(i));
  for (size_t i = 0; i < st.blockRefWorldSolids.size(); ++i)
    trySolid(st.blockRefWorldSolids[i], static_cast<int>(i));
  if (!any)
    return false;
  *out = best;
  if (outPick)
    *outPick = bestPick;
  return true;
}

/// Nearest front-facing triangle whose face id names a real face — no vertex/edge precedence, unlike
/// `solidpick::PickSubObject` (issue #496 follow-up). BCONNECT wants exactly what the "Center of
/// face" object snap already resolves for the same click (an aim at the middle of a bore looking
/// straight down its axis is a graze against the rim EDGE by `PickSubObject`'s rules, which then
/// refuses the whole pick — the same shape as `CadSnap::RayHitSolidFace`, reproduced here rather than
/// exported, since that one lives in an anonymous namespace in CadSnap.cpp).
bool RayHitAnySolidFace(const AppCommandState& st, const ray3d::Ray& ray, CadSolidPtr* outSolid, int* outFaceIndex,
                        ray3d::Vec3* outHit) {
  bool any = false;
  double bestT = 0.0;
  const auto trySolid = [&](const CadSolidPtr& sp) {
    if (!sp)
      return;
    const auto ce = std::find_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                                 [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
    if (ce == st.solidDisplayCache.end() || ce->empty())
      return;
    for (size_t i = 0, ti = 0; i + 8 < ce->triVerts.size(); i += 9, ++ti) {
      const ray3d::Vec3 a{ce->triVerts[i], ce->triVerts[i + 1], ce->triVerts[i + 2]};
      const ray3d::Vec3 b{ce->triVerts[i + 3], ce->triVerts[i + 4], ce->triVerts[i + 5]};
      const ray3d::Vec3 c{ce->triVerts[i + 6], ce->triVerts[i + 7], ce->triVerts[i + 8]};
      ray3d::Vec3 hit;
      double t = 0.0;
      if (!ray3d::RayTriangleIntersect(ray, a, b, c, &hit, &t))
        continue;
      if (any && !(t < bestT))
        continue;
      const int fi = ti < ce->triFaceIds.size() ? ce->triFaceIds[ti] : -1;
      if (fi < 0 || static_cast<size_t>(fi) >= sp->faces.size())
        continue;
      any = true;
      bestT = t;
      *outSolid = sp;
      *outFaceIndex = fi;
      *outHit = hit;
    }
  };
  for (const CadSolidPtr& sp : st.drawing.cadSolids)
    trySolid(sp);
  for (const CadSolidPtr& sp : st.blockRefWorldSolids)
    trySolid(sp);
  return any;
}

} // namespace

bool FindNearestDrawingConnector(const AppCommandState& st, float px, float py, float pz, float maxDist,
                                 CadBlockWorldConnection* out) {
  if (!out)
    return false;
  bool any = false;
  float bestD = maxDist * maxDist;
  CadBlockWorldConnection best{};
  std::vector<CadBlockWorldConnection> world;
  for (int i = 0; i < static_cast<int>(st.drawing.cadBlockRefs.size()); ++i) {
    world.clear();
    CadBlockCollectWorldConnections(st.drawing.blockDefs, st.drawing.cadBlockRefs[static_cast<size_t>(i)], i, &world);
    for (const CadBlockWorldConnection& wc : world) {
      const float dx = wc.x - px;
      const float dy = wc.y - py;
      const float dz = wc.z - pz;
      const float d2 = dx * dx + dy * dy + dz * dz;
      if (d2 <= bestD) {
        bestD = d2;
        best = wc;
        any = true;
      }
    }
  }
  if (!any)
    return false;
  *out = best;
  return true;
}

/// Nearest bare line/polyline endpoint, OR a `CadPipeRun`'s own end face centre (issue #486,
/// user-specified 2026-09-17), to (px,py,pz) within maxDist (issue #496): the "pipe end" smart-mode
/// target. Outward normal points away from the pipe, matching the sense of a `CadBlockConnection`
/// normal so `CadBlockSnapInsertToConnection` orients the fitting the same way it would against
/// another fitting's port. A `CadPipeRun` end is read from `CadPipeRunEndPorts` — the SAME geometry
/// the run's own swept solid is built from (cadpiperun.hpp) — rather than the run's raw clicked
/// vertices, because auto-filleting can move an end slightly off its clicked position; using
/// anything else here would offer a connection point at a spot the rendered pipe does not actually
/// end at.
///
/// `*outRunIndex` receives the index of the `CadPipeRun` whose end won, or -1 for a bare line's.
static bool FindNearestPipeEndpoint(const AppCommandState& st, float px, float py, float pz, float maxDist,
                                    float* outX, float* outY, float* outZ, float* outNx, float* outNy,
                                    float* outNz, int* outRunIndex) {
  bool any = false;
  float bestD = maxDist * maxDist;
  int candidateRun = -1;
  auto consider = [&](float ex, float ey, float ez, float dirx, float diry, float dirz) {
    const float dx = ex - px;
    const float dy = ey - py;
    const float dz = ez - pz;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > bestD)
      return;
    const float l = std::sqrt(dirx * dirx + diry * diry + dirz * dirz);
    if (l <= 1.e-8f)
      return;
    bestD = d2;
    any = true;
    *outRunIndex = candidateRun;
    *outX = ex;
    *outY = ey;
    *outZ = ez;
    *outNx = dirx / l;
    *outNy = diry / l;
    *outNz = dirz / l;
  };
  for (size_t i = 0; i < st.drawing.lineCount(); ++i) {
    const CadLineRecord ln = st.drawing.line(i);
    const float x0 = static_cast<float>(ln.x0);
    const float y0 = static_cast<float>(ln.y0);
    const float z0 = static_cast<float>(ln.z0);
    const float x1 = static_cast<float>(ln.x1);
    const float y1 = static_cast<float>(ln.y1);
    const float z1 = static_cast<float>(ln.z1);
    consider(x0, y0, z0, x0 - x1, y0 - y1, z0 - z1);
    consider(x1, y1, z1, x1 - x0, y1 - y0, z1 - z0);
  }
  for (size_t ri = 0; ri < st.drawing.cadPipeRuns.size(); ++ri) {
    const CadPipeRun& run = st.drawing.cadPipeRuns[ri];
    CadPipeRunEndPort start, end;
    if (!CadPipeRunEndPorts(run, &start, &end))
      continue;  // an unfillable/unresolvable run offers no connection point (REQ-201)
    candidateRun = static_cast<int>(ri);
    consider(static_cast<float>(start.point.x), static_cast<float>(start.point.y),
             static_cast<float>(start.point.z), static_cast<float>(start.outwardNormal.x),
             static_cast<float>(start.outwardNormal.y), static_cast<float>(start.outwardNormal.z));
    consider(static_cast<float>(end.point.x), static_cast<float>(end.point.y),
             static_cast<float>(end.point.z), static_cast<float>(end.outwardNormal.x),
             static_cast<float>(end.outwardNormal.y), static_cast<float>(end.outwardNormal.z));
  }
  return any;
}

bool SubmitInsertBlockConnectorPick(AppCommandState& st, float wx, float wy, float wz, std::vector<std::string>& log,
                                    bool optional) {
  using Ph = stepengine::InsertBlockPhase;
  const auto* sess = st.sessionAs<stepengine::InsertBlockSession>();
  if (!sess || sess->phase != Ph::WaitConnectorTarget)
    return false;

  const int di = CadBlockFindDef(st.drawing.blockDefs, st.insertBlockName);
  if (di < 0 || st.drawing.blockDefs[static_cast<size_t>(di)].connections.empty()) {
    log.push_back("INSERT — the block being inserted has no connection ports.");
    return false;
  }
  const CadBlockDefinition& insertedDef = st.drawing.blockDefs[static_cast<size_t>(di)];

  constexpr float kSnap = 2.f;
  CadBlockWorldConnection tgt;
  const bool foundPortRaw = FindNearestDrawingConnector(st, wx, wy, wz, kSnap, &tgt);
  float pipeX = 0.f, pipeY = 0.f, pipeZ = 0.f, pipeNx = 0.f, pipeNy = 0.f, pipeNz = 0.f;
  int pipeRun = -1;
  const bool foundPipeEndRaw = FindNearestPipeEndpoint(st, wx, wy, wz, kSnap, &pipeX, &pipeY, &pipeZ, &pipeNx,
                                                       &pipeNy, &pipeNz, &pipeRun);
  if (!foundPortRaw && !foundPipeEndRaw) {
    // `optional` is the Pipe Fittings palette asking "snap if there is anything to snap to"
    // (REQ-350 (f)): a part staged beside the line is a placement, not a failure, so the
    // caller places it plainly and says so. Every other caller asked for a snap and gets the
    // refusal by name (REQ-201).
    if (!optional)
      log.push_back("INSERT — no connection port near that point (within 2 ft).");
    return false;
  }
  const CadConnectionModeTarget portTarget = CadBlockClassifyPortTarget(tgt.ownerPartType);
  const float portD2 = (tgt.x - wx) * (tgt.x - wx) + (tgt.y - wy) * (tgt.y - wy) + (tgt.z - wz) * (tgt.z - wz);
  const float pipeD2 =
      (pipeX - wx) * (pipeX - wx) + (pipeY - wy) * (pipeY - wy) + (pipeZ - wz) * (pipeZ - wz);

  // Which of the BLOCK'S OWN connection points to try: the one explicitly chosen in the INSERT
  // dialog, or — auto-detect (user request 2026-09-17, reported bug: a flange with a gasket-face
  // port AND a separate weld-neck-to-pipe port always used the FIRST port defined, regardless of
  // what was actually under the cursor) — every port the block has, so whichever one's OWN
  // configured mode(s) actually match what is nearby wins, not whichever happened to be added
  // first in BEDIT.
  std::vector<const CadBlockConnection*> candidates;
  if (st.insertBlockConnectorName[0] != '\0') {
    const int ci = CadBlockFindConnection(insertedDef, st.insertBlockConnectorName);
    if (ci < 0) {
      log.push_back("INSERT — the selected connection point was not found on this block.");
      return false;
    }
    candidates.push_back(&insertedDef.connections[static_cast<size_t>(ci)]);
  } else {
    for (const CadBlockConnection& c : insertedDef.connections)
      candidates.push_back(&c);
  }

  // Among every candidate PORT crossed with every candidate TARGET it actually accepts
  // (`CadBlockConnectionAcceptsTarget` — a legacy mode-less port accepts anything, unchanged), the
  // nearest accepted pairing wins: "detect what we are snapping to and use that block's connection
  // point logic," not always the first port defined.
  const CadBlockConnection* src = nullptr;
  bool usePipeEnd = false;
  float bestD2 = 0.f;
  // Two passes, EXACT matches before default-fallback ones: a port whose mode is tagged for the
  // exact target under the cursor always outranks one that merely falls back to an `isDefault`
  // mode for the same target — otherwise two single-mode ports that both happen to be flagged
  // default (an easy authoring habit: it is the only mode, so "default" reads as "the one to use")
  // would tie on distance and the wrong one could win, exactly the reported bug.
  for (int pass = 0; pass < 2 && !src; ++pass) {
    const bool exactOnly = pass == 0;
    for (const CadBlockConnection* c : candidates) {
      const bool portExact = foundPortRaw && CadBlockConnectionHasExactMode(*c, portTarget);
      const bool pipeExact = foundPipeEndRaw && CadBlockConnectionHasExactMode(*c, CadConnectionModeTarget::PipeEnd);
      const bool portOk = exactOnly ? portExact : (foundPortRaw && CadBlockConnectionAcceptsTarget(*c, portTarget));
      const bool pipeOk = exactOnly
                              ? pipeExact
                              : (foundPipeEndRaw && CadBlockConnectionAcceptsTarget(*c, CadConnectionModeTarget::PipeEnd));
      if (!portOk && !pipeOk)
        continue;
      const bool tryPipe = pipeOk && (!portOk || pipeD2 < portD2);
      const float d2 = tryPipe ? pipeD2 : portD2;
      if (!src || d2 < bestD2) {
        src = c;
        usePipeEnd = tryPipe;
        bestD2 = d2;
      }
    }
  }
  if (!src) {
    if (!optional)
      log.push_back(insertedDef.connections.size() > 1
                        ? "INSERT — nothing near that point matches any of this block's connection points."
                        : "INSERT — nothing near that point matches this connection point's configured mode(s).");
    return false;
  }

  const CadConnectionModeTarget target = usePipeEnd ? CadConnectionModeTarget::PipeEnd : portTarget;
  const CadBlockConnectionMode* mode = CadBlockResolveMode(*src, target);

  CadBlockXform xf;
  xf.sx = st.insertBlockSx;
  xf.sy = st.insertBlockSy;
  xf.sz = st.insertBlockSz;
  ApplyInsertDialogRotations(st, &xf);
  const float tX = usePipeEnd ? pipeX : tgt.x;
  const float tY = usePipeEnd ? pipeY : tgt.y;
  const float tZ = usePipeEnd ? pipeZ : tgt.z;
  const float tNx = usePipeEnd ? pipeNx : tgt.nx;
  const float tNy = usePipeEnd ? pipeNy : tgt.ny;
  const float tNz = usePipeEnd ? pipeNz : tgt.nz;
  CadBlockSnapInsertToConnection(*src, tX, tY, tZ, tNx, tNy, tNz, &xf);
  CadBlockApplyConnectionModeOffset(*src, mode, &xf);
  ApplyInsertXformToDialog(xf, st);
  st.insertBlockSnappedPipeRun = usePipeEnd ? pipeRun : -1;
  const std::string targetName = usePipeEnd ? std::string("pipe end") : tgt.name;
  std::string msg = "INSERT — snapped to " + targetName + " (" + std::string(CadConnectionModeTargetTag(target)) + ")";
  if (mode)
    msg += ", mode \"" + mode->name + "\"";
  msg += ".";
  log.push_back(msg);
  InsertAdvanceAfterPoint(st, log);
  return true;
}


void BlockFittingStart(AppCommandState& st, std::vector<std::string>& log) {
  if (st.blockEditorName.empty()) {
    log.push_back("BLOCKFITTING — open a block with BEDIT first.");
    return;
  }
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
  if (di < 0)
    return;
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  auto sess = std::make_unique<stepengine::BlockFittingSession>();
  sess->partTypePending = def.partType;
  sess->nominalSizePending = def.nominalSize;
  sess->pressureClassPending = def.pressureClass;
  sess->partNumberPending = def.partNumber;
  stepengine::BeginSession(st, std::move(sess), /*recordAsLastCommand=*/false);
  log.push_back(
      "BLOCKFITTING — part type [" +
      (def.partType == CadPipePartType::None ? std::string("none") : std::string(CadPipePartTypeTag(def.partType))) +
      "] (elbow-90|elbow-45|tee|cross|reducer|flange|valve|coupling|cap|nozzle|other|none, "
      "Enter to keep current):");
}

void BlockFittingSubmitLine(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  using Ph = stepengine::BlockFittingPhase;
  auto* sess = st.sessionAs<stepengine::BlockFittingSession>();
  if (!sess)
    return;
  const std::string line = StringUtil::trimCopy(lineIn);
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
  if (di < 0) {
    log.push_back("BLOCKFITTING — the block being edited no longer exists.");
    stepengine::EndSession(st);
    return;
  }
  CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];

  if (sess->phase == Ph::WaitPartType) {
    if (!line.empty())
      sess->partTypePending = CadBlockEqCi(line, "none") ? CadPipePartType::None : ParseCadPipePartType(line);
    sess->phase = Ph::WaitNominalSize;
    log.push_back("BLOCKFITTING — nominal size [" + sess->nominalSizePending + "] (Enter to keep current):");
    return;
  }
  if (sess->phase == Ph::WaitNominalSize) {
    if (!line.empty())
      sess->nominalSizePending = line;
    sess->phase = Ph::WaitPressureClass;
    log.push_back("BLOCKFITTING — pressure class [" +
                  (sess->pressureClassPending == CadPipePressureClass::None
                       ? std::string("none")
                       : std::string(CadPipePressureClassTag(sess->pressureClassPending))) +
                  "] (CS150|CS300|none, Enter to keep current):");
    return;
  }
  if (sess->phase == Ph::WaitPressureClass) {
    if (!line.empty())
      sess->pressureClassPending =
          CadBlockEqCi(line, "none") ? CadPipePressureClass::None : ParseCadPipePressureClass(line);
    sess->phase = Ph::WaitPartNumber;
    log.push_back("BLOCKFITTING — part number [" + sess->partNumberPending + "] (Enter to keep current):");
    return;
  }
  if (sess->phase == Ph::WaitPartNumber) {
    if (!line.empty())
      sess->partNumberPending = line;
    def.partType = sess->partTypePending;
    def.nominalSize = sess->nominalSizePending;
    def.pressureClass = sess->pressureClassPending;
    def.partNumber = sess->partNumberPending;
    st.blockEditorDirty = true;
    log.push_back("BLOCKFITTING — updated \"" + def.name + "\".");
    stepengine::EndSession(st);
    return;
  }
}

bool SubmitBconnectFacePick(AppCommandState& st, const ray3d::Ray& ray, const solidpick::Tolerance& /*tol*/,
                            std::vector<std::string>& log) {
  // No vertex/edge tolerance needed any more (issue #496 follow-up): RayHitAnySolidFace picks the
  // nearest front face by triangle hit alone, matching "Center of face" rather than
  // solidpick::PickSubObject's vertex/edge-precedence pick.
  auto* sess = st.sessionAs<stepengine::BConnectSession>();
  if (!st.blockEditActive || !sess || !sess->awaitingFace)
    return false;
  if (sess->name.empty()) {
    log.push_back("BCONNECT — internal error: missing connection name.");
    sess->awaitingFace = false;
    return false;
  }

  RefreshSolidDisplayGeometry(st);

  // Nearest-front-triangle pick, the same shape as the "Center of face" object snap the user can
  // already see land correctly on this geometry (issue #496 follow-up) — not `PickSolidFaceAcrossDrawing`
  // / `solidpick::PickSubObject`, whose vertex/edge precedence refuses the whole pick as soon as the
  // ray's closest approach to a nearby rim edge is within tolerance, which is exactly what happens
  // aiming down the axis of a bore or hub.
  CadSolidPtr sp;
  int faceIndex = -1;
  ray3d::Vec3 hit;
  if (!RayHitAnySolidFace(st, ray, &sp, &faceIndex, &hit)) {
    log.push_back("BCONNECT — no solid face under the cursor.");
    return false;
  }
  if (!sp || faceIndex < 0 || static_cast<size_t>(faceIndex) >= sp->faces.size()) {
    log.push_back("BCONNECT — that face is no longer there.");
    return false;
  }
  const brep::Face& f = sp->faces[static_cast<size_t>(faceIndex)];
  // The face's own center, exactly what "Center of face" would offer for the same click — the user
  // does not need the exact clicked point, just this face, and its center is unambiguous for both a
  // flat gasket face and a curved bore/hub.
  ray3d::Vec3 centre = f.surface.kind == brep::SurfaceKind::Plane ? brep::PlanarFaceCentroid(*sp, f)
                                                                  : brep::CurvedFaceMidpoint(f);
  ray3d::Vec3 n;
  if (f.surface.kind == brep::SurfaceKind::Plane) {
    n = f.surface.frame.zAxis;
    if (f.surface.inward)
      n = ray3d::Scale(n, -1.0);
  } else if (f.surface.kind == brep::SurfaceKind::Cylinder) {
    // A pipe's mating surface is often a bore or a hub OD, not a flat face at all — connect along
    // the cylinder's axis, at whichever end the click landed nearer to, same as a flat face
    // connects along its normal.
    const ray3d::Vec3 axis = ray3d::Normalize(f.surface.frame.zAxis);
    const ray3d::Vec3 base = f.surface.frame.origin;
    const double t = ray3d::Dot(ray3d::Sub(hit, base), axis);
    if (t <= f.surface.height * 0.5) {
      centre = base;
      n = ray3d::Scale(axis, -1.0);
    } else {
      centre = ray3d::Add(base, ray3d::Scale(axis, f.surface.height));
      n = axis;
    }
  } else {
    log.push_back("BCONNECT — pick a flat face or a pipe's cylindrical bore/hub for the connection port.");
    return false;
  }

  const int di = CadBlockFindDef(st.drawing.blockDefs, st.blockEditorName);
  if (di < 0)
    return false;
  CadBlockConnection conn;
  conn.name = sess->name;
  conn.nominalSize = sess->nominalSize;
  conn.x = static_cast<float>(centre.x);
  conn.y = static_cast<float>(centre.y);
  conn.z = static_cast<float>(centre.z);
  conn.nx = static_cast<float>(n.x);
  conn.ny = static_cast<float>(n.y);
  conn.nz = static_cast<float>(n.z);
  conn.role = sess->rolePending;
  conn.engagementLength = sess->engagementPending;
  conn.compatibilityTag = sess->compatTagPending;
  const std::string addedName = conn.name;
  st.drawing.blockDefs[static_cast<size_t>(di)].connections.push_back(std::move(conn));
  st.blockEditorDirty = true;
  // The BCONNECT wizard (issue #496) keeps st.active == Kind::BConnect through the face-pick step
  // so ESC still cancels it as one command; release it now that the pick has committed.
  stepengine::EndSession(st);
  log.push_back("BCONNECT — added connection \"" + addedName + "\".");
  return true;
}

void AppendInsertBlockGhostRubber(const AppCommandState& st, const CadBlockXform& xf,
                                  std::vector<float>& rubberLines) {
  CadBlockRef ghost;
  ghost.defName = st.insertBlockName;
  ghost.xf = xf;
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(st.drawing.blockDefs, ghost, EntityAttributes{}, &segs);
  for (const CadBlockWorldSeg& s : segs)
    PushRubberSegViewRel(rubberLines, s.x0, s.y0, s.x1, s.y1, 0., 0., s.z0, s.z1);

  std::vector<CadBlockWorldSolid> ws;
  CadBlockCollectWorldSolids(st.drawing.blockDefs, ghost, EntityAttributes{}, &ws);
  brep::Problem why = brep::Problem::Ok;
  for (const CadBlockWorldSolid& w : ws) {
    if (!w.solid)
      continue;
    std::vector<double> edges;
    if (!brep::TessellateEdges(*w.solid, kSolidChordToleranceFt, &edges, &why))
      continue;
    for (std::size_t i = 0; i + 5 < edges.size(); i += 6)
      PushRubberSegViewRel(rubberLines, edges[i], edges[i + 1], edges[i + 3], edges[i + 4], 0., 0.,
                           static_cast<float>(edges[i + 2]), static_cast<float>(edges[i + 5]));
  }
}

bool SubmitInsertBlockAlignFacePick(AppCommandState& st, const ray3d::Ray& ray, const solidpick::Tolerance& tol,
                                    std::vector<std::string>& log) {
  using Ph = stepengine::InsertBlockPhase;
  const auto* sess = st.sessionAs<stepengine::InsertBlockSession>();
  if (!sess || sess->phase != Ph::WaitAlignFace)
    return false;

  RefreshSolidDisplayGeometry(st);

  SelectedSubObject sub;
  solidpick::Pick pick;
  if (!PickSolidFaceAcrossDrawing(st, ray, tol, &sub, &pick)) {
    log.push_back("INSERT — no flat solid face under the cursor.");
    return false;
  }
  const CadSolidPtr sp = sub.owner.lock();
  if (!sp || sub.index < 0 || static_cast<size_t>(sub.index) >= sp->faces.size()) {
    log.push_back("INSERT — that face is no longer there.");
    return false;
  }
  const brep::Face& f = sp->faces[static_cast<size_t>(sub.index)];
  if (f.surface.kind != brep::SurfaceKind::Plane) {
    log.push_back("INSERT — pick a flat face to align the fitting.");
    return false;
  }
  ray3d::Vec3 n = f.surface.frame.zAxis;
  if (f.surface.inward)
    n = ray3d::Scale(n, -1.0);

  CadBlockXform orient;
  orient.x = st.insertBlockX;
  orient.y = st.insertBlockY;
  orient.z = st.insertBlockZ;
  orient.sx = st.insertBlockSx;
  orient.sy = st.insertBlockSy;
  orient.sz = st.insertBlockSz;
  orient.rotZ = -st.insertBlockRotDeg * 0.01745329252f;
  CadBlockSetLocalZAxis(&orient, static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z));
  st.insertBlockRotXDeg = orient.rotX * 57.2957795f;
  st.insertBlockRotYDeg = orient.rotY * 57.2957795f;
  std::snprintf(st.insertBlockRotXBuf, sizeof(st.insertBlockRotXBuf), "%.4f", static_cast<double>(st.insertBlockRotXDeg));
  std::snprintf(st.insertBlockRotYBuf, sizeof(st.insertBlockRotYBuf), "%.4f", static_cast<double>(st.insertBlockRotYDeg));

  log.push_back("INSERT — face aligned.");
  InsertAdvanceAfterAlignFace(st, log);
  return true;
}

void CadBlocksCommitInsertDialog(AppCommandState& st, std::vector<std::string>& log) {
  if (st.insertBlockName[0] == '\0') {
    log.push_back("INSERT — choose a block name.");
    return;
  }
  if (CadBlockFindDef(st.drawing.blockDefs, st.insertBlockName) < 0) {
    log.push_back("INSERT — no block named \"" + std::string(st.insertBlockName) + "\".");
    return;
  }
  float ang = 0.f;
  if (st.insertBlockAngleBuf[0] != '\0' && !ParseAngleDegrees(st.insertBlockAngleBuf, &ang)) {
    log.push_back("INSERT — could not parse rotation angle.");
    return;
  }
  if (st.insertBlockAngleBuf[0] != '\0')
    st.insertBlockRotDeg = ang;
  if (st.insertBlockRotXBuf[0] != '\0') {
    float rx = 0.f;
    if (!TryParseF(st.insertBlockRotXBuf, rx)) {
      log.push_back("INSERT — could not parse X rotation angle.");
      return;
    }
    st.insertBlockRotXDeg = rx;
  }
  if (st.insertBlockRotYBuf[0] != '\0') {
    float ry = 0.f;
    if (!TryParseF(st.insertBlockRotYBuf, ry)) {
      log.push_back("INSERT — could not parse Y rotation angle.");
      return;
    }
    st.insertBlockRotYDeg = ry;
  }
  auto* sess = st.sessionAs<stepengine::InsertBlockSession>();
  if (st.insertBlockSpecifyConnectorSnap) {
    const int cdi = CadBlockFindDef(st.drawing.blockDefs, st.insertBlockName);
    if (cdi < 0 || st.drawing.blockDefs[static_cast<size_t>(cdi)].connections.empty()) {
      log.push_back("INSERT — the block has no connection ports to snap from.");
      return;
    }
    st.insertBlockDialogOpen = false;
    if (sess)
      sess->phase = stepengine::InsertBlockPhase::WaitConnectorTarget;
    log.push_back("INSERT — pick the target connection port to snap to.");
    return;
  }
  if (st.insertBlockSpecifyPoint) {
    st.insertBlockDialogOpen = false;
    if (sess)
      sess->phase = stepengine::InsertBlockPhase::WaitInsertPoint;
    log.push_back("INSERT — specify insertion point.");
    return;
  }
  if (st.insertBlockSpecifyAlignFace) {
    st.insertBlockDialogOpen = false;
    if (sess)
      sess->phase = stepengine::InsertBlockPhase::WaitAlignFace;
    log.push_back("INSERT — pick a flat face to align the fitting (ESC cancels).");
    return;
  }
  if (st.insertBlockSpecifyScale) {
    st.insertBlockDialogOpen = false;
    if (sess)
      sess->phase = stepengine::InsertBlockPhase::WaitScale;
    log.push_back("INSERT — specify scale (click a point).");
    return;
  }
  if (st.insertBlockSpecifyRot) {
    st.insertBlockDialogOpen = false;
    if (sess)
      sess->phase = stepengine::InsertBlockPhase::WaitRotation;
    log.push_back("INSERT — specify rotation angle <0d0'0\"> (type degrees or click).");
    return;
  }
  if (CadBlockPlaceInsert(st, st.insertBlockName, InsertDialogXform(st), st.insertBlockExplode, log))
    CadBlocksAfterPlace(st, log);
}

void CadBlocksCommitInsertAttrDialog(AppCommandState& st, std::vector<std::string>& log) {
  CadBlockRef* r = InsertAttrTarget(st);
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.insertBlockName);
  if (r && di >= 0) {
    const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
    const int n = std::min(8, static_cast<int>(def.attrDefs.size()));
    for (int i = 0; i < n; ++i)
      CadBlockAttrSet(r, def.attrDefs[static_cast<size_t>(i)].tag, st.insertBlockAttrBuf[i]);
  }
  BumpCadGpuCache(st);
  log.push_back("INSERT — attributes updated.");
  FinishInsertCommand(st);
}

void CadBlocksPlacePendingInsert(AppCommandState& st, std::vector<std::string>& log) {
  if (CadBlockPlaceInsert(st, st.insertBlockName, InsertDialogXform(st), st.insertBlockExplode, log))
    CadBlocksAfterPlace(st, log);
}

bool CadBlockArmDynGrip(AppCommandState& st, int refIndex, int which) {
  if (refIndex < 0 || static_cast<size_t>(refIndex) >= st.drawing.cadBlockRefs.size())
    return false;
  CadBlockRef& r = st.drawing.cadBlockRefs[static_cast<size_t>(refIndex)];
  const int di = CadBlockFindDef(st.drawing.blockDefs, r.defName);
  if (di < 0)
    return false;
  const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
  if (which == 3 && CadBlockHasMatchlineDyn(def)) {
    CadBlockToggleMatchlineFlip(&r, def);
    return false;
  }
  if (CadBlockHasLinearStretchDyn(def)) {
    const std::string pname = CadBlockFirstLinearStretchParamName(def);
    st.entityGripOrigX0 = CadBlockParamValue(r, def, pname);
    st.entityGripOrigCx = r.xf.x;
    st.entityGripOrigCy = r.xf.y;
    return true;
  }
  st.entityGripOrigX0 = CadBlockParamValue(r, def, "DistNeg");
  st.entityGripOrigY0 = CadBlockParamValue(r, def, "DistPos");
  st.entityGripOrigX1 = CadBlockParamValue(r, def, "SheetOff");
  st.entityGripOrigY1 = CadBlockParamValue(r, def, "NorthOff");
  st.entityGripOrigR = CadBlockParamValue(r, def, "Flip");
  st.entityGripOrigCx = r.xf.x;
  st.entityGripOrigCy = r.xf.y;
  return true;
}

void CadBlockRestoreDynGripOrig(AppCommandState& st, CadBlockRef* r) {
  if (!r)
    return;
  const int di = CadBlockFindDef(st.drawing.blockDefs, r->defName);
  if (di >= 0) {
    const CadBlockDefinition& def = st.drawing.blockDefs[static_cast<size_t>(di)];
    if (CadBlockHasLinearStretchDyn(def)) {
      const std::string pname = CadBlockFirstLinearStretchParamName(def);
      if (!pname.empty())
        CadBlockParamSet(r, pname, st.entityGripOrigX0);
      r->xf.x = st.entityGripOrigCx;
      r->xf.y = st.entityGripOrigCy;
      return;
    }
  }
  CadBlockParamSet(r, "DistNeg", st.entityGripOrigX0);
  CadBlockParamSet(r, "DistPos", st.entityGripOrigY0);
  CadBlockParamSet(r, "SheetOff", st.entityGripOrigX1);
  CadBlockParamSet(r, "NorthOff", st.entityGripOrigY1);
  CadBlockParamSet(r, "Flip", st.entityGripOrigR);
  r->xf.x = st.entityGripOrigCx;
  r->xf.y = st.entityGripOrigCy;
}

bool CadBlockInsertPreviewXform(const AppCommandState& st, float curX, float curY, CadBlockXform* out) {
  return CadBlockInsertPreviewXform(st, curX, curY, 0.f, out);
}

bool CadBlockInsertPreviewXform(const AppCommandState& st, float curX, float curY, float curZ, CadBlockXform* out) {
  assert(out != nullptr);
  using Ph = stepengine::InsertBlockPhase;
  const auto* sess = st.sessionAs<stepengine::InsertBlockSession>();
  if (!sess)
    return false;
  const Ph phase = sess->phase;
  if (phase != Ph::WaitInsertPoint && phase != Ph::WaitScale &&
      phase != Ph::WaitRotation && phase != Ph::WaitAlignFace &&
      phase != Ph::WaitConnectorTarget)
    return false;
  const int di = CadBlockFindDef(st.drawing.blockDefs, st.insertBlockName);
  if (di < 0)
    return false;

  CadBlockXform xf;
  if (phase == Ph::WaitInsertPoint) {
    xf.x = curX;
    xf.y = curY;
    xf.z = curZ;
  } else {
    xf.x = st.insertBlockX;
    xf.y = st.insertBlockY;
    xf.z = st.insertBlockZ;
  }

  float sx = st.insertBlockSx;
  float sy = st.insertBlockSy;
  float sz = st.insertBlockSz;
  if (phase == Ph::WaitScale && st.insertBlockSpecifyScale) {
    const float d = InsertLiveScaleDist(st, curX, curY, curZ);
    if (d > 1.e-8f) {
      sx = d;
      if (st.insertBlockUniformScale) {
        sy = d;
        sz = d;
      }
    }
  }

  float rotDeg = st.insertBlockRotDeg;
  if (phase == Ph::WaitRotation && st.insertBlockSpecifyRot)
    rotDeg = InsertLiveRotDeg(st, curX, curY);

  // CadBlockPlaceInsert multiplies the transform by the block-unit scale after building it from
  // the dialog; fold the same factor in here so the ghost matches the commit within REQ-101.
  const float us = CadBlockInsertUnitsScale(st, st.drawing.blockDefs[static_cast<size_t>(di)]);
  xf.sx = sx * us;
  xf.sy = sy * us;
  xf.sz = sz * us;
  ApplyInsertDialogRotations(st, &xf);
  if (phase == Ph::WaitRotation && st.insertBlockSpecifyRot)
    xf.rotZ = InsertRotZFromCwNorthDeg(rotDeg);
  *out = xf;
  return true;
}

void SubmitInsertBlockPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  SubmitInsertBlockPick(st, wx, wy, 0.f, log);
}

void SubmitInsertBlockPick(AppCommandState& st, float wx, float wy, float wz, std::vector<std::string>& log) {
  using Ph = stepengine::InsertBlockPhase;
  auto* sess = st.sessionAs<stepengine::InsertBlockSession>();
  if (!sess)
    return;
  if (sess->phase == Ph::WaitConnectorTarget) {
    SubmitInsertBlockConnectorPick(st, wx, wy, wz, log);
    return;
  }
  if (sess->phase == Ph::WaitInsertPoint) {
    st.insertBlockX = wx;
    st.insertBlockY = wy;
    st.insertBlockZ = wz;
    // REQ-350 (f) — armed from the Pipe Fittings palette, ONE click places the part and WHERE it
    // lands decides how. Three outcomes, tried in this order, because each is strictly more
    // specific than the next:
    //
    //   1. on a pipe run -> splice it in (the PIPEFIT path: engagement cutback, run split in two,
    //      one undo step);
    //   2. otherwise, near a connection port or pipe end -> snap to it, which is what orients the
    //      part (REQ-107 / issue #496, the same `SubmitInsertBlockConnectorPick` INSERT already
    //      uses, asked OPTIONALLY so "nothing nearby" is not a refusal here);
    //   3. otherwise -> place it free at the point.
    //
    // Step 2 was MISSING, and that is the "the blind flange is not aligning correctly" report: a
    // blind flange bolts onto another flange FACE, never into the middle of a pipe, so its click
    // never reaches step 1 — and without step 2 it fell straight through to step 3 and was dropped
    // with the identity rotation it was authored with. REQ-350 (f) has said "with connection-port
    // snapping (REQ-107)" all along; `CadPipePaletteArmPart` never turned it on.
    //
    // A failed SPLICE also falls through rather than ending the click: a part that cannot splice
    // (a blind flange has ONE port; `PickElbowPorts` needs two) would otherwise consume the click
    // and place nothing at all, which is not a refusal the user can act on (REQ-201). The splice
    // has already logged WHY, and the part still lands where it was clicked.
    if (st.insertBlockPipeSpliceArmed) {
      const ray3d::Vec3 pick{static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz)};
      const std::string armed = st.insertBlockName;
      int runIdx = -1;
      if (CadPipeRunUnderPick(st, pick, &runIdx)) {
        st.insertBlockPipeSpliceArmed = false;
        stepengine::EndSession(st);
        if (CadPipeFitNamedAtPick(st, runIdx, armed, pick, log))
          return;
        // Refused, and said why. Re-arm just enough to finish the click as a placement.
        stepengine::BeginSession(
            st, std::make_unique<stepengine::InsertBlockSession>(Ph::WaitInsertPoint),
            /*recordAsLastCommand=*/false);
        sess = st.sessionAs<stepengine::InsertBlockSession>();
      }
      st.insertBlockPipeSpliceArmed = false;
      sess->phase = Ph::WaitConnectorTarget;
      if (SubmitInsertBlockConnectorPick(st, wx, wy, wz, log, /*optional=*/true))
        return;  // snapped and placed
      sess->phase = Ph::WaitInsertPoint;
      // The REASON, not the outcome: `InsertAdvanceAfterPoint` below is what actually places
      // it, and "within 2 ft" covers both "nothing there" and "nothing there this part can
      // mate with" — the snap refuses for either, and the user needs to know only that it
      // went down with the orientation it was authored with.
      log.push_back("Pipe Fittings — \"" + armed +
                    "\": nothing within 2 ft for its connection point to mate with; placing "
                    "it as authored.");
    }
    InsertAdvanceAfterPoint(st, log);
    return;
  }
  if (sess->phase == Ph::WaitScale) {
    const float dist = InsertLiveScaleDist(st, wx, wy, wz);
    if (dist < 1.e-8f) {
      log.push_back("INSERT — scale point must be away from the insertion point.");
      return;
    }
    st.insertBlockSx = dist;
    if (st.insertBlockUniformScale) {
      st.insertBlockSy = dist;
      st.insertBlockSz = dist;
    }
    InsertAdvanceAfterScaleOrRot(st, log);
    return;
  }
  if (sess->phase == Ph::WaitRotation) {
    // Store the pick direction in the same clockwise-from-north convention the typed field uses,
    // so InsertRotZFromCwNorthDeg converts it consistently (pick north → rotation 0 → as authored).
    st.insertBlockRotDeg = InsertLiveRotDeg(st, wx, wy);
    if (CadBlockPlaceInsert(st, st.insertBlockName, InsertDialogXform(st), st.insertBlockExplode, log))
      CadBlocksAfterPlace(st, log);
  }
}

void LoadBundledBlockLibrary(AppCommandState& dest, std::vector<std::string>& log) {
  LoadBundledBlockLibraryImpl(dest, log);
}

void CadBlocksCollectEditPickerNames(const AppCommandState& st, std::vector<std::string>* names) {
  assert(names != nullptr);
  names->clear();
  names->emplace_back("<Current Drawing>");
  std::vector<std::string> rest;
  for (const CadBlockDefinition& d : st.drawing.blockDefs) {
    if (!d.name.empty())
      rest.push_back(d.name);
  }
  namespace fs = std::filesystem;
  fs::path dir = ResolveBundledAssetPath(fs::path("resources") / "blocks");
  if (!dir.empty()) {
    if (fs::is_regular_file(dir))
      dir = dir.parent_path();
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
      for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
        if (ec)
          break;
        if (!e.is_regular_file(ec))
          continue;
        const std::string extLo = StringUtil::toLowerAsciiCopy(e.path().extension().u8string());
        if (extLo != ".dxf")
          continue;
        const std::string stem = e.path().stem().u8string();
        if (stem.empty() || CadBlockFindDef(st.drawing.blockDefs, stem) >= 0)
          continue;
        rest.push_back(stem);
      }
    }
  }
  std::sort(rest.begin(), rest.end(), [](const std::string& a, const std::string& b) {
    return StringUtil::toLowerAsciiCopy(a) < StringUtil::toLowerAsciiCopy(b);
  });
  rest.erase(std::unique(rest.begin(), rest.end(),
                         [](const std::string& a, const std::string& b) { return CadBlockEqCi(a, b); }),
             rest.end());
  for (std::string& s : rest)
    names->push_back(std::move(s));
}

void CadBlocksOpenEditPicker(AppCommandState& st, std::vector<std::string>& log) {
  LoadBundledBlockLibrary(st, log);
  st.blockEditPickerOpen = true;
  st.blockEditPickerName[0] = '\0';
  for (const SelectedEntity& e : st.selection) {
    if (e.type != SelectedEntity::Type::BlockRef)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
      continue;
    const std::string& nm = st.drawing.cadBlockRefs[static_cast<size_t>(e.index)].defName;
    std::snprintf(st.blockEditPickerName, sizeof(st.blockEditPickerName), "%s", nm.c_str());
    break;
  }
}

void CadBlocksEnterNamedEditor(AppCommandState& st, std::string_view nameRaw, std::vector<std::string>& log) {
  const std::string name = StringUtil::trimCopy(std::string(nameRaw));
  if (name.empty() || CadBlockEqCi(name, "<Current Drawing>")) {
    log.push_back("BEDIT — choose a block name (Current Drawing is not a definition).");
    return;
  }
  if (st.blockEditActive) {
    if (CadBlockEqCi(name, st.blockEditorName)) {
      log.push_back("BEDIT — already editing \"" + st.blockEditorName + "\".");
      return;
    }
    log.push_back("BEDIT — close \"" + st.blockEditorName + "\" first (BCLOSE).");
    return;
  }
  if (st.activeSpaceIndex != kModelSpaceIndex || InFloatingModelSpace(st)) {
    log.push_back("BEDIT — switch to model space first.");
    return;
  }
  LoadBundledBlockLibrary(st, log);
  int di = CadBlockFindDef(st.drawing.blockDefs, name);
  if (di < 0) {
    namespace fs = std::filesystem;
    fs::path dir = ResolveBundledAssetPath(fs::path("resources") / "blocks");
    if (!dir.empty()) {
      if (fs::is_regular_file(dir))
        dir = dir.parent_path();
      std::error_code ec;
      if (fs::is_directory(dir, ec)) {
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
          if (ec)
            break;
          if (!e.is_regular_file(ec))
            continue;
          const std::string ext = e.path().extension().u8string();
          const std::string extLo = StringUtil::toLowerAsciiCopy(ext);
          if (extLo != ".dxf" && extLo != ".dwg")
            continue;
          if (!CadBlockEqCi(e.path().stem().u8string(), name))
            continue;
          ImportCadBlocksFromPath(st, e.path().u8string().c_str(), log);
          break;
        }
      }
    }
    di = CadBlockFindDef(st.drawing.blockDefs, name);
  }
  if (di < 0) {
    CadBlockDefinition def;
    def.name = name;
    def.units = CadDrawingInsUnitsName(st.drawing.drawingInsUnits);
    st.drawing.blockDefs.push_back(std::move(def));
    di = CadBlockFindDef(st.drawing.blockDefs, name);
    log.push_back("BEDIT — created empty definition \"" + name + "\".");
  }
  if (di < 0)
    return;
  // ADR-043: stash the real drawing + camera, then load the definition's primitives into the model
  // arrays so every ordinary draw/modify/snap command edits the block in isolation.
  PushUndoSnapshot(st, "Bedit");
  st.blockEditUndoMark = CadActiveUndoStackSize(st);
  st.blockEditModelStash = CadCaptureGeometrySnapshot(st, "blockedit-stash");
  st.blockEditCamPanX = st.viewportPanX;
  st.blockEditCamPanY = st.viewportPanY;
  st.blockEditCamPanZ = st.viewportPanZ;
  st.blockEditCamZoom = st.viewportZoom;
  st.blockEditCamAz = st.viewportAzimuthDeg;
  st.blockEditCamEl = st.viewportElevationDeg;
  st.blockEditCamRoll = st.viewportRollDeg;  // #153
  ClearCadSelection(st);
  st.blockEditorName = st.drawing.blockDefs[static_cast<size_t>(di)].name;
  st.blockEditorSnapshot = st.drawing.blockDefs[static_cast<size_t>(di)];
  LoadBlockPrimitivesIntoDrawing(st, st.drawing.blockDefs[static_cast<size_t>(di)].content);
  st.blockEditActive = true;
  st.blockEditorDirty = false;
  st.blockAuthoringPaletteOpen = true;
  st.blockEditPickerOpen = false;
  st.pendingZoomExtents = true;
  BumpCadGpuCache(st);
  st.blockEditCleanRevision = st.cadGpuRevision;
  log.push_back("BEDIT — editing \"" + st.blockEditorName + "\" in local coordinates. BSAVE / BCLOSE.");
}

void CadBlocksCommitEditPicker(AppCommandState& st, std::vector<std::string>& log) {
  CadBlocksEnterNamedEditor(st, st.blockEditPickerName, log);
}
