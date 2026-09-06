#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "StringUtil.hpp"
#include "GsIo.hpp"
#include "DxfIo.hpp"
#include "DwgIo.hpp"
#include "AppPaths.hpp"
#include "WinFileDialogs.hpp"

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
/// inserts the definition exactly as authored.
float InsertRotZFromCwNorthDeg(float deg) { return -deg * 0.01745329252f; }

/// Distance from the committed insertion point to \p wx,\p wy after ORTHO, used for the live
/// on-screen scale pick. Shared by the commit path and the preview ghost so they cannot drift.
float InsertLiveScaleDist(const AppCommandState& st, float wx, float wy) {
  float lx = wx;
  float ly = wy;
  ApplyOrthoConstrainFromAnchor(st, st.insertBlockX, st.insertBlockY, &lx, &ly, st.orthoMode);
  const float dx = lx - st.insertBlockX;
  const float dy = ly - st.insertBlockY;
  return std::sqrt(dx * dx + dy * dy);
}

/// Bearing from the committed insertion point to \p wx,\p wy in the clockwise-from-north
/// convention (after ORTHO), used for the live on-screen rotation pick. Returns the current stored
/// angle when the cursor sits on the insertion point. Shared by commit and preview.
float InsertLiveRotDeg(const AppCommandState& st, float wx, float wy) {
  float lx = wx;
  float ly = wy;
  ApplyOrthoConstrainFromAnchor(st, st.insertBlockX, st.insertBlockY, &lx, &ly, st.orthoMode);
  const float dx = lx - st.insertBlockX;
  const float dy = ly - st.insertBlockY;
  if (dx * dx + dy * dy <= 1.e-10f)
    return st.insertBlockRotDeg;
  return BearingCwNorthDegFromMathAngleRad(std::atan2(dy, dx));
}

EntityAttributes NewBlockAttr(const AppCommandState& st) {
  EntityAttributes a;
  a.layer = st.currentLayer.empty() ? std::string("0") : st.currentLayer;
  a.color = "ByLayer";
  a.linetype = "ByLayer";
  a.lineweightMm = -1.f;
  a.transparency = -1.f;
  return a;
}

std::vector<std::string> SplitCommaRest(std::istream& in) {
  std::string rest;
  std::getline(in, rest);
  std::vector<std::string> f;
  std::string cur;
  for (char c : rest) {
    if (c == ',') {
      f.push_back(StringUtil::trimCopy(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty() || !rest.empty())
    f.push_back(StringUtil::trimCopy(cur));
  if (!f.empty() && f[0].empty())
    f.erase(f.begin());
  return f;
}

void NoteRecent(AppCommandState& st, const std::string& name) {
  st.blockRecent.erase(std::remove_if(st.blockRecent.begin(), st.blockRecent.end(),
                                      [&](const std::string& n) { return CadBlockEqCi(n, name); }),
                       st.blockRecent.end());
  st.blockRecent.insert(st.blockRecent.begin(), name);
  if (st.blockRecent.size() > 24)
    st.blockRecent.resize(24);
}

void CaptureSelectionInto(const AppCommandState& st, CadBlockContent* c, float bx, float by, float bz) {
  assert(c != nullptr);
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= st.userLinesFlat.size())
        continue;
      c->lines.push_back(st.userLinesFlat[k] - bx);
      c->lines.push_back(st.userLinesFlat[k + 1] - by);
      c->lines.push_back(st.userLinesFlat[k + 2] - bz);
      c->lines.push_back(st.userLinesFlat[k + 3] - bx);
      c->lines.push_back(st.userLinesFlat[k + 4] - by);
      c->lines.push_back(st.userLinesFlat[k + 5] - bz);
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.userLineAttrs.size())
        a = st.userLineAttrs[static_cast<size_t>(e.index)];
      c->lineAttrs.push_back(a);
      c->lineVis.push_back("");
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 4;
      if (k + 3 >= st.userCirclesCxCyZR.size())
        continue;
      c->circles.push_back(st.userCirclesCxCyZR[k] - bx);
      c->circles.push_back(st.userCirclesCxCyZR[k + 1] - by);
      c->circles.push_back(st.userCirclesCxCyZR[k + 2] - bz);
      c->circles.push_back(st.userCirclesCxCyZR[k + 3]);
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.userCircleAttrs.size())
        a = st.userCircleAttrs[static_cast<size_t>(e.index)];
      c->circleAttrs.push_back(a);
      float bnx = 0.f, bny = 0.f, bnz = 1.f;
      CircleNormalAt(st.userCircleNormals, static_cast<size_t>(e.index), &bnx, &bny, &bnz);
      PushCircleNormal(c->circleNormals, bnx, bny, bnz);
      c->circleVis.push_back("");
    } else if (e.type == SelectedEntity::Type::Annotation) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadAnnotations.size())
        continue;
      CadAnnotation t = st.cadAnnotations[static_cast<size_t>(e.index)];
      t.insX -= bx;
      t.insY -= by;
      t.insZ -= bz;
      c->texts.push_back(std::move(t));
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.cadAnnotationAttrs.size())
        a = st.cadAnnotationAttrs[static_cast<size_t>(e.index)];
      c->textAttrs.push_back(a);
    } else if (e.type == SelectedEntity::Type::Arc) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.userArcs.size())
        continue;
      CadArc a = st.userArcs[static_cast<size_t>(e.index)];
      a.cx -= bx;
      a.cy -= by;
      c->arcs.push_back(a);
      EntityAttributes at{};
      if (static_cast<size_t>(e.index) < st.userArcAttrs.size())
        at = st.userArcAttrs[static_cast<size_t>(e.index)];
      c->arcAttrs.push_back(at);
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadBlockRefs.size())
        continue;
      CadBlockNested n;
      n.defName = st.cadBlockRefs[static_cast<size_t>(e.index)].defName;
      n.xf = st.cadBlockRefs[static_cast<size_t>(e.index)].xf;
      n.xf.x -= bx;
      n.xf.y -= by;
      n.xf.z -= bz;
      c->nested.push_back(std::move(n));
    } else if (e.type == SelectedEntity::Type::Mesh) {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadMeshes.size())
        continue;
      c->meshes.push_back(st.cadMeshes[static_cast<size_t>(e.index)]);
      EntityAttributes a{};
      if (static_cast<size_t>(e.index) < st.cadMeshAttrs.size())
        a = st.cadMeshAttrs[static_cast<size_t>(e.index)];
      c->meshAttrs.push_back(a);
    }
  }
}

void CadBlockCaptureDrawing(const AppCommandState& st, CadBlockContent* c) {
  assert(c != nullptr);
  c->lines = st.userLinesFlat;
  c->lineAttrs = st.userLineAttrs;
  c->lineVis.assign(st.userLineAttrs.size(), "");
  c->circles = st.userCirclesCxCyZR;
  c->circleAttrs = st.userCircleAttrs;
  c->circleVis.assign(st.userCircleAttrs.size(), "");
  c->circleNormals = st.userCircleNormals;
  EnsureCircleNormals(c->circleNormals, st.userCirclesCxCyZR.size() / 4);
  c->arcs = st.userArcs;
  c->arcAttrs = st.userArcAttrs;
  c->ellipses = st.userEllipses;
  c->ellAttrs = st.userEllAttrs;
  c->polyOffsets = st.userPolylineOffsets;
  c->polyVerts = st.userPolylineVerts;
  c->polyVertsBulge = st.userPolylineVertsBulge;  // REQ-316 / ADR-047
  c->polyClosed = st.userPolylineClosed;
  c->polyAttrs = st.userPolylineAttrs;
  c->texts = st.cadAnnotations;
  c->textAttrs = st.cadAnnotationAttrs;
  c->meshes = st.cadMeshes;
  c->meshAttrs = st.cadMeshAttrs;
  c->solids = st.cadSolids;          // REQ-320 / ADR-051
  c->solidAttrs = st.cadSolidAttrs;
  for (const CadBlockRef& r : st.cadBlockRefs) {
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
  st.userLinesFlat        = c.lines;
  st.userLineAttrs        = c.lineAttrs;
  st.userLineAttrs.resize(c.lines.size() / 6);
  st.userCirclesCxCyZR    = c.circles;
  st.userCircleAttrs      = c.circleAttrs;
  st.userCircleAttrs.resize(c.circles.size() / 4);
  st.userCircleNormals    = c.circleNormals;
  EnsureCircleNormals(st.userCircleNormals, c.circles.size() / 4);
  st.userArcs             = c.arcs;
  st.userArcAttrs         = c.arcAttrs;
  st.userArcAttrs.resize(c.arcs.size());
  st.userEllipses         = c.ellipses;
  st.userEllAttrs         = c.ellAttrs;
  st.userEllAttrs.resize(c.ellipses.size());
  st.userPolylineOffsets  = c.polyOffsets;
  st.userPolylineVerts    = c.polyVerts;
  st.userPolylineVertsBulge = c.polyVertsBulge;  // REQ-316 / ADR-047
  SyncPolylineBulge(st.userPolylineVertsBulge, st.userPolylineVerts.size());  // legacy block defs have none
  st.userPolylineClosed   = c.polyClosed;
  st.userPolylineAttrs    = c.polyAttrs;
  st.cadAnnotations       = c.texts;
  st.cadAnnotationAttrs   = c.textAttrs;
  st.cadAnnotationAttrs.resize(c.texts.size());
  // Hide everything that is not the block being edited.
  st.cadFilledRegions.clear();
  st.cadFilledRegionAttrs.clear();
  st.cadMeshes.clear();
  st.cadMeshAttrs.clear();
  st.cadSurfaces.clear();
  st.cadSurfaceAttrs.clear();
  st.cadSolids.clear();
  st.cadSolidAttrs.clear();
  st.cadTables.clear();
  st.cadTableAttrs.clear();
  st.cadBlockRefs.clear();
  st.cadBlockRefAttrs.clear();
  st.surveyPoints.clear();
  st.featureLineOffsets.clear();
  st.featureLineVerts.clear();
  st.featureLineClosed.clear();
  st.featureLineElevPt.clear();
  st.featureLineInfo.clear();
  st.featureLineAttrs.clear();
}

/// ADR-043: harvest the model arrays back into a definition's primitive geometry on Save. Leaves
/// \c nested / \c meshes and the dynamic-block authoring model (parameters/actions/attrDefs)
/// untouched — those are edited through their own commands, not this surface.
void HarvestDrawingPrimitivesIntoContent(const AppCommandState& st, CadBlockContent* c) {
  assert(c != nullptr);
  c->lines = st.userLinesFlat;
  c->lineAttrs = st.userLineAttrs;
  c->lineAttrs.resize(st.userLinesFlat.size() / 6);
  c->lineVis.assign(st.userLinesFlat.size() / 6, "");
  c->circles = st.userCirclesCxCyZR;
  c->circleAttrs = st.userCircleAttrs;
  c->circleAttrs.resize(st.userCirclesCxCyZR.size() / 4);
  c->circleVis.assign(st.userCirclesCxCyZR.size() / 4, "");
  c->circleNormals = st.userCircleNormals;
  EnsureCircleNormals(c->circleNormals, st.userCirclesCxCyZR.size() / 4);
  c->arcs = st.userArcs;
  c->arcAttrs = st.userArcAttrs;
  c->arcAttrs.resize(st.userArcs.size());
  c->ellipses = st.userEllipses;
  c->ellAttrs = st.userEllAttrs;
  c->ellAttrs.resize(st.userEllipses.size());
  c->polyOffsets = st.userPolylineOffsets;
  c->polyVerts = st.userPolylineVerts;
  c->polyVertsBulge = st.userPolylineVertsBulge;  // REQ-316 / ADR-047
  c->polyClosed = st.userPolylineClosed;
  c->polyAttrs = st.userPolylineAttrs;
  c->texts = st.cadAnnotations;
  c->textAttrs = st.cadAnnotationAttrs;
  c->textAttrs.resize(st.cadAnnotations.size());
}

bool DrawingHasCaptureableGeometry(const AppCommandState& st) {
  return !st.userLinesFlat.empty() || !st.userCirclesCxCyZR.empty() || !st.userArcs.empty() ||
         !st.userEllipses.empty() || st.userPolylineOffsets.size() >= 2 || !st.cadAnnotations.empty() ||
         !st.cadMeshes.empty() || !st.cadSolids.empty() || !st.importedDxfAttrDefs.empty();
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
  const int i = CadBlockFindDef(dest.blockDefs, def.name);
  if (i >= 0) {
    CadBlockDefinition& have = dest.blockDefs[static_cast<size_t>(i)];
    const bool upgradeMatchline =
        CadBlockNameIsMatchline(def.name) &&
        (have.attrDefs.size() < def.attrDefs.size() || have.content.texts.size() < def.content.texts.size());
    if (!upgradeMatchline) {
      log.push_back("BLOCKIMPORT — skipped duplicate \"" + def.name + "\".");
      return 0;
    }
    have = std::move(def);
    return 1;
  }
  dest.blockDefs.push_back(std::move(def));
  return 1;
}

int ImportCadBlocksFromPathImpl(AppCommandState& dest, const char* pathUtf8, std::vector<std::string>& log) {
  if (!pathUtf8 || pathUtf8[0] == '\0') {
    log.push_back("BLOCKIMPORT — no path.");
    return -1;
  }
  const std::string ext = LowerExt(pathUtf8);
  AppCommandState scratch;
  bool ok = false;
  if (ext == ".dxf")
    ok = ImportDxfFile(scratch, pathUtf8, log);
  else if (ext == ".dwg")
    ok = ImportDwgFile(scratch, pathUtf8, log);
  else {
    // .gs block-library import was removed by issue #264 (D-2026-09-03-h); re-adding a
    // block-library container is tracked as issue #284, not part of this one.
    log.push_back("BLOCKIMPORT — expected a .dxf or .dwg file.");
    return -1;
  }
  if (!ok)
    return -1;
  int n = 0;
  for (CadBlockDefinition& d : scratch.blockDefs)
    n += MergeBlockDef(dest, std::move(d), log);
  if (DrawingHasCaptureableGeometry(scratch)) {
    CadBlockDefinition wrap;
    wrap.name = FileStemUtf8(pathUtf8);
    wrap.units = CadDrawingInsUnitsName(scratch.drawingInsUnits);
    wrap.attrDefs = scratch.importedDxfAttrDefs;
    CadBlockCaptureDrawing(scratch, &wrap.content);
    CadBlockBakeBasePoint(&wrap);
    n += MergeBlockDef(dest, std::move(wrap), log);
  }
  for (CadBlockDefinition& d : dest.blockDefs)
    CadBlockAuthorMatchlineDynamics(&d);
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
    if (ext == ".dxf" || ext == ".dwg")
      files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  int n = 0;
  for (const fs::path& p : files) {
    std::vector<std::string> ignored;
    const int k = ImportCadBlocksFromPathImpl(dest, p.u8string().c_str(), ignored);
    if (k > 0)
      n += k;
  }
  if (n > 0)
    log.push_back("Block library — loaded " + std::to_string(n) + " bundled definition(s).");
}

void EraseSelectedSources(AppCommandState& st) {
  std::vector<int> lines;
  std::vector<int> circles;
  std::vector<int> anns;
  std::vector<int> arcs;
  std::vector<int> meshes;
  std::vector<int> refs;
  for (const SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg)
      lines.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Circle)
      circles.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Annotation)
      anns.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Arc)
      arcs.push_back(e.index);
    else if (e.type == SelectedEntity::Type::Mesh)
      meshes.push_back(e.index);
    else if (e.type == SelectedEntity::Type::BlockRef)
      refs.push_back(e.index);
  }
  auto dropLines = [&](std::vector<int>& idx) {
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    for (int i = static_cast<int>(idx.size()) - 1; i >= 0; --i) {
      const int k = idx[static_cast<size_t>(i)];
      if (k < 0)
        continue;
      const size_t o = static_cast<size_t>(k) * 6;
      if (o + 5 < st.userLinesFlat.size())
        st.userLinesFlat.erase(st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(o),
                               st.userLinesFlat.begin() + static_cast<std::ptrdiff_t>(o + 6));
      if (static_cast<size_t>(k) < st.userLineAttrs.size())
        st.userLineAttrs.erase(st.userLineAttrs.begin() + k);
    }
  };
  dropLines(lines);
  auto dedup = [](std::vector<int>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  };
  dedup(circles);
  for (int i = static_cast<int>(circles.size()) - 1; i >= 0; --i) {
    const int k = circles[static_cast<size_t>(i)];
    const size_t o = static_cast<size_t>(k) * 4;
    if (o + 3 < st.userCirclesCxCyZR.size())
      st.userCirclesCxCyZR.erase(st.userCirclesCxCyZR.begin() + static_cast<std::ptrdiff_t>(o),
                                 st.userCirclesCxCyZR.begin() + static_cast<std::ptrdiff_t>(o + 4));
    if (static_cast<size_t>(k) < st.userCircleAttrs.size())
      st.userCircleAttrs.erase(st.userCircleAttrs.begin() + k);
    EraseCircleNormal(st.userCircleNormals, static_cast<size_t>(k));
  }
  dedup(anns);
  for (int i = static_cast<int>(anns.size()) - 1; i >= 0; --i) {
    const int k = anns[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.cadAnnotations.size())
      st.cadAnnotations.erase(st.cadAnnotations.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.cadAnnotationAttrs.size())
      st.cadAnnotationAttrs.erase(st.cadAnnotationAttrs.begin() + k);
  }
  dedup(arcs);
  for (int i = static_cast<int>(arcs.size()) - 1; i >= 0; --i) {
    const int k = arcs[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.userArcs.size())
      st.userArcs.erase(st.userArcs.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.userArcAttrs.size())
      st.userArcAttrs.erase(st.userArcAttrs.begin() + k);
  }
  std::sort(meshes.begin(), meshes.end());
  for (int i = static_cast<int>(meshes.size()) - 1; i >= 0; --i) {
    const int k = meshes[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.cadMeshes.size())
      st.cadMeshes.erase(st.cadMeshes.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.cadMeshAttrs.size())
      st.cadMeshAttrs.erase(st.cadMeshAttrs.begin() + k);
  }
  std::sort(refs.begin(), refs.end());
  for (int i = static_cast<int>(refs.size()) - 1; i >= 0; --i) {
    const int k = refs[static_cast<size_t>(i)];
    if (k >= 0 && static_cast<size_t>(k) < st.cadBlockRefs.size())
      st.cadBlockRefs.erase(st.cadBlockRefs.begin() + k);
    if (k >= 0 && static_cast<size_t>(k) < st.cadBlockRefAttrs.size())
      st.cadBlockRefAttrs.erase(st.cadBlockRefAttrs.begin() + k);
  }
  st.selection.clear();
}

void ExplodeRef(AppCommandState& st, const CadBlockRef& ref, const EntityAttributes& insertAttr) {
  std::vector<CadBlockWorldSeg> segs;
  CadBlockCollectWorldLines(st.blockDefs, ref, insertAttr, &segs);
  for (const CadBlockWorldSeg& s : segs) {
    st.userLinesFlat.push_back(s.x0);
    st.userLinesFlat.push_back(s.y0);
    st.userLinesFlat.push_back(s.z0);
    st.userLinesFlat.push_back(s.x1);
    st.userLinesFlat.push_back(s.y1);
    st.userLinesFlat.push_back(s.z1);
    EntityAttributes a = s.attr;
    a.id = 0;
    st.userLineAttrs.push_back(a);
  }
  std::vector<CadAnnotation> anns;
  CadBlockCollectWorldAnnotations(st.blockDefs, ref, &anns);
  for (CadAnnotation& t : anns) {
    st.cadAnnotations.push_back(std::move(t));
    EntityAttributes a = insertAttr;
    a.id = 0;
    st.cadAnnotationAttrs.push_back(std::move(a));
  }
}

bool PlaceInsertImpl(AppCommandState& st, std::string_view name, CadBlockXform xf, bool explode,
                     std::vector<std::string>& log) {
  const int di = CadBlockFindDef(st.blockDefs, name);
  if (di < 0) {
    log.push_back("INSERT — no block named \"" + std::string(name) + "\".");
    return false;
  }
  CadBlockRef r;
  r.defName = st.blockDefs[static_cast<size_t>(di)].name;
  r.xf = xf;
  PushUndoSnapshot(st, "Insert");
  const float us = CadBlockUnitsScale(st.blockDefs[static_cast<size_t>(di)].units,
                                      CadDrawingInsUnitsName(st.drawingInsUnits));
  r.xf.sx *= us;
  r.xf.sy *= us;
  r.xf.sz *= us;
  const CadBlockDefinition& def = st.blockDefs[static_cast<size_t>(di)];
  for (const CadBlockAttrDef& ad : def.attrDefs)
    CadBlockAttrSet(&r, ad.tag, ad.defaultValue);
  const EntityAttributes attr = NewBlockAttr(st);
  const bool paper = ActivePaperGeometryTarget(st) != nullptr;
  if (paper && explode) {
    log.push_back("INSERT — explode is model space only; placing as a reference.");
    explode = false;
  }
  if (paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    L->paperBlockRefs.push_back(r);
    L->paperBlockRefAttrs.push_back(attr);
  } else {
    st.cadBlockRefs.push_back(r);
    st.cadBlockRefAttrs.push_back(attr);
    if (explode) {
      ExplodeRef(st, st.cadBlockRefs.back(), st.cadBlockRefAttrs.back());
      st.cadBlockRefs.pop_back();
      st.cadBlockRefAttrs.pop_back();
    }
  }
  NoteRecent(st, r.defName);
  EnsureEntityIds(st);
  BumpCadGpuCache(st);
  if (explode)
    log.push_back("INSERT — exploded \"" + r.defName + "\".");
  else
    log.push_back("INSERT — placed \"" + r.defName + "\".");
  return true;
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

bool ImportCadBlocksFromPath(AppCommandState& dest, const char* pathUtf8, std::vector<std::string>& log) {
  return ImportCadBlocksFromPathImpl(dest, pathUtf8, log) >= 0;
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
  return PlaceInsertImpl(st, name, xf, explode, log);
}

void StartInsertBlockCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::None && st.active != AppCommandState::Kind::InsertBlock)
    CancelActiveCommand(st, log);
  st.active = AppCommandState::Kind::InsertBlock;
  st.lastCommand = AppCommandState::Kind::InsertBlock;
  st.insertBlockDialogOpen = true;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitDialog;
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
  st.insertBlockPath[0] = '\0';
  st.insertBlockName[0] = '\0';
  if (!st.blockRecent.empty())
    std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "%s", st.blockRecent.front().c_str());
  else if (!st.blockDefs.empty())
    std::snprintf(st.insertBlockName, sizeof(st.insertBlockName), "%s", st.blockDefs.front().name.c_str());
  std::snprintf(st.insertBlockAngleBuf, sizeof(st.insertBlockAngleBuf), "0");
  CadBlocksApplyInsertNameDefaults(st);
  log.push_back("INSERT — select a block in the Insert dialog.");
}

void CadBlocksApplyInsertNameDefaults(AppCommandState& st) {
  // Previously forced 90° for matchline blocks — a workaround for INSERT applying rotation
  // counter-clockwise. The bundled matchline definitions are authored pointing north, and INSERT
  // now honours the clockwise-from-north convention (InsertRotZFromCwNorthDeg), so rotation 0
  // places them exactly as authored. No name needs a non-zero default any more.
  (void)st;
}

namespace {

// Parse a float without throwing. Returns false (and leaves out untouched) when
// the token is not a clean number — callers report a usage error instead of
// letting std::invalid_argument/out_of_range escape the command dispatcher.
bool TryParseF(const std::string& s, float& out) {
  try {
    size_t used = 0;
    const float v = std::stof(s, &used);
    while (used < s.size() && std::isspace(static_cast<unsigned char>(s[used])))
      ++used;
    if (used != s.size())
      return false;
    out = v;
    return true;
  } catch (...) {
    return false;
  }
}

CadBlockXform InsertDialogXform(const AppCommandState& st) {
  CadBlockXform xf;
  xf.x = st.insertBlockX;
  xf.y = st.insertBlockY;
  xf.z = st.insertBlockZ;
  xf.sx = st.insertBlockSx;
  xf.sy = st.insertBlockSy;
  xf.sz = st.insertBlockSz;
  xf.rotZ = InsertRotZFromCwNorthDeg(st.insertBlockRotDeg);
  return xf;
}

void FinishInsertCommand(AppCommandState& st) {
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
  if (static_cast<size_t>(st.insertBlockAttrRefIndex) >= st.cadBlockRefs.size())
    return nullptr;
  return &st.cadBlockRefs[static_cast<size_t>(st.insertBlockAttrRefIndex)];
}

void CadBlocksAfterPlace(AppCommandState& st, std::vector<std::string>& log) {
  if (st.insertBlockExplode) {
    FinishInsertCommand(st);
    return;
  }
  const int di = CadBlockFindDef(st.blockDefs, st.insertBlockName);
  if (di < 0 || st.blockDefs[static_cast<size_t>(di)].attrDefs.empty()) {
    FinishInsertCommand(st);
    return;
  }
  const CadBlockDefinition& def = st.blockDefs[static_cast<size_t>(di)];
  const bool paper = ActivePaperGeometryTarget(st) != nullptr;
  if (paper) {
    PaperLayout* L = ActivePaperGeometryTarget(st);
    if (!L || L->paperBlockRefs.empty()) {
      FinishInsertCommand(st);
      return;
    }
    st.insertBlockAttrPaper = true;
    st.insertBlockAttrRefIndex = static_cast<int>(L->paperBlockRefs.size()) - 1;
  } else if (!st.cadBlockRefs.empty()) {
    st.insertBlockAttrPaper = false;
    st.insertBlockAttrRefIndex = static_cast<int>(st.cadBlockRefs.size()) - 1;
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

void CadBlocksCommitInsertDialog(AppCommandState& st, std::vector<std::string>& log) {
  if (st.insertBlockName[0] == '\0') {
    log.push_back("INSERT — choose a block name.");
    return;
  }
  if (CadBlockFindDef(st.blockDefs, st.insertBlockName) < 0) {
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
  if (st.insertBlockSpecifyPoint) {
    st.insertBlockDialogOpen = false;
    st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitInsertPoint;
    log.push_back("INSERT — specify insertion point.");
    return;
  }
  if (st.insertBlockSpecifyScale) {
    st.insertBlockDialogOpen = false;
    st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitScale;
    log.push_back("INSERT — specify scale (click a point).");
    return;
  }
  if (st.insertBlockSpecifyRot) {
    st.insertBlockDialogOpen = false;
    st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitRotation;
    log.push_back("INSERT — specify rotation angle <0d0'0\"> (type degrees or click).");
    return;
  }
  if (CadBlockPlaceInsert(st, st.insertBlockName, InsertDialogXform(st), st.insertBlockExplode, log))
    CadBlocksAfterPlace(st, log);
}

void CadBlocksCommitInsertAttrDialog(AppCommandState& st, std::vector<std::string>& log) {
  CadBlockRef* r = InsertAttrTarget(st);
  const int di = CadBlockFindDef(st.blockDefs, st.insertBlockName);
  if (r && di >= 0) {
    const CadBlockDefinition& def = st.blockDefs[static_cast<size_t>(di)];
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
  if (refIndex < 0 || static_cast<size_t>(refIndex) >= st.cadBlockRefs.size())
    return false;
  CadBlockRef& r = st.cadBlockRefs[static_cast<size_t>(refIndex)];
  const int di = CadBlockFindDef(st.blockDefs, r.defName);
  if (di < 0)
    return false;
  const CadBlockDefinition& def = st.blockDefs[static_cast<size_t>(di)];
  if (which == 3 && CadBlockHasMatchlineDyn(def)) {
    CadBlockToggleMatchlineFlip(&r, def);
    return false;
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
  CadBlockParamSet(r, "DistNeg", st.entityGripOrigX0);
  CadBlockParamSet(r, "DistPos", st.entityGripOrigY0);
  CadBlockParamSet(r, "SheetOff", st.entityGripOrigX1);
  CadBlockParamSet(r, "NorthOff", st.entityGripOrigY1);
  CadBlockParamSet(r, "Flip", st.entityGripOrigR);
  r->xf.x = st.entityGripOrigCx;
  r->xf.y = st.entityGripOrigCy;
}

bool CadBlockInsertPreviewXform(const AppCommandState& st, float curX, float curY, CadBlockXform* out) {
  assert(out != nullptr);
  using Ph = AppCommandState::InsertBlockPhase;
  if (st.insertBlockPhase != Ph::WaitInsertPoint && st.insertBlockPhase != Ph::WaitScale &&
      st.insertBlockPhase != Ph::WaitRotation)
    return false;
  const int di = CadBlockFindDef(st.blockDefs, st.insertBlockName);
  if (di < 0)
    return false;

  CadBlockXform xf;
  if (st.insertBlockPhase == Ph::WaitInsertPoint) {
    xf.x = curX;
    xf.y = curY;
  } else {
    xf.x = st.insertBlockX;
    xf.y = st.insertBlockY;
  }
  xf.z = st.insertBlockZ;

  float sx = st.insertBlockSx;
  float sy = st.insertBlockSy;
  float sz = st.insertBlockSz;
  if (st.insertBlockPhase == Ph::WaitScale && st.insertBlockSpecifyScale) {
    const float d = InsertLiveScaleDist(st, curX, curY);
    if (d > 1.e-8f) {
      sx = d;
      if (st.insertBlockUniformScale) {
        sy = d;
        sz = d;
      }
    }
  }

  float rotDeg = st.insertBlockRotDeg;
  if (st.insertBlockPhase == Ph::WaitRotation && st.insertBlockSpecifyRot)
    rotDeg = InsertLiveRotDeg(st, curX, curY);

  // CadBlockPlaceInsert multiplies the transform by the block-unit scale after building it from
  // the dialog; fold the same factor in here so the ghost matches the commit within REQ-101.
  const float us = CadBlockUnitsScale(st.blockDefs[static_cast<size_t>(di)].units,
                                      CadDrawingInsUnitsName(st.drawingInsUnits));
  xf.sx = sx * us;
  xf.sy = sy * us;
  xf.sz = sz * us;
  xf.rotZ = InsertRotZFromCwNorthDeg(rotDeg);
  *out = xf;
  return true;
}

void SubmitInsertBlockPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  using Ph = AppCommandState::InsertBlockPhase;
  if (st.active != AppCommandState::Kind::InsertBlock)
    return;
  if (st.insertBlockPhase == Ph::WaitInsertPoint) {
    st.insertBlockX = wx;
    st.insertBlockY = wy;
    if (st.insertBlockSpecifyScale) {
      st.insertBlockPhase = Ph::WaitScale;
      log.push_back("INSERT — specify scale (click a point).");
      return;
    }
    if (st.insertBlockSpecifyRot) {
      st.insertBlockPhase = Ph::WaitRotation;
      log.push_back("INSERT — specify rotation angle <0d0'0\"> (type degrees or click).");
      return;
    }
    if (CadBlockPlaceInsert(st, st.insertBlockName, InsertDialogXform(st), st.insertBlockExplode, log))
      CadBlocksAfterPlace(st, log);
    return;
  }
  if (st.insertBlockPhase == Ph::WaitScale) {
    const float dist = InsertLiveScaleDist(st, wx, wy);
    if (dist < 1.e-8f) {
      log.push_back("INSERT — scale point must be away from the insertion point.");
      return;
    }
    st.insertBlockSx = dist;
    if (st.insertBlockUniformScale) {
      st.insertBlockSy = dist;
      st.insertBlockSz = dist;
    }
    if (st.insertBlockSpecifyRot) {
      st.insertBlockPhase = Ph::WaitRotation;
      log.push_back("INSERT — specify rotation angle <0d0'0\"> (type degrees or click).");
      return;
    }
    if (CadBlockPlaceInsert(st, st.insertBlockName, InsertDialogXform(st), st.insertBlockExplode, log))
      CadBlocksAfterPlace(st, log);
    return;
  }
  if (st.insertBlockPhase == Ph::WaitRotation) {
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
  for (const CadBlockDefinition& d : st.blockDefs) {
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
        if (stem.empty() || CadBlockFindDef(st.blockDefs, stem) >= 0)
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
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadBlockRefs.size())
      continue;
    const std::string& nm = st.cadBlockRefs[static_cast<size_t>(e.index)].defName;
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
  int di = CadBlockFindDef(st.blockDefs, name);
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
    di = CadBlockFindDef(st.blockDefs, name);
  }
  if (di < 0) {
    CadBlockDefinition def;
    def.name = name;
    def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
    st.blockDefs.push_back(std::move(def));
    di = CadBlockFindDef(st.blockDefs, name);
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
  st.blockEditorName = st.blockDefs[static_cast<size_t>(di)].name;
  st.blockEditorSnapshot = st.blockDefs[static_cast<size_t>(di)];
  LoadBlockPrimitivesIntoDrawing(st, st.blockDefs[static_cast<size_t>(di)].content);
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

int PickCadBlockRefAt(float wx, float wy, const AppCommandState& st, float orthoHalfHeightWorld) {
  const float tol = std::max(orthoHalfHeightWorld * 0.02f, 1.e-4f);
  int best = -1;
  float bestD = 1.e30f;
  for (int i = 0; i < static_cast<int>(st.cadBlockRefs.size()); ++i) {
    if (CadBlockHitWorld(st.blockDefs, st.cadBlockRefs[static_cast<size_t>(i)], wx, wy, tol)) {
      const float dx = wx - st.cadBlockRefs[static_cast<size_t>(i)].xf.x;
      const float dy = wy - st.cadBlockRefs[static_cast<size_t>(i)].xf.y;
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
    if (f.size() < 3) {
      log.push_back("BLOCK — usage: BLOCK <name>, <baseX>, <baseY>[, RETAIN|DELETE|CONVERT][, <baseZ>].");
      return true;
    }
    if (st.selection.empty()) {
      log.push_back("BLOCK — select geometry first, then run BLOCK.");
      return true;
    }
    if (CadBlockFindDef(st.blockDefs, f[0]) >= 0) {
      log.push_back("BLOCK — name \"" + f[0] + "\" is already used.");
      return true;
    }
    float bx = 0.f, by = 0.f, bz = 0.f;
    try {
      bx = std::stof(f[1]);
      by = std::stof(f[2]);
      if (f.size() >= 5)
        bz = std::stof(f[4]);
    } catch (...) {
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
    def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
    CaptureSelectionInto(st, &def.content, bx, by, bz);
    CadBlockBakeBasePoint(&def);
    for (const CadBlockNested& n : def.content.nested) {
      if (CadBlockWouldCycle(st.blockDefs, def.name, n.defName)) {
        log.push_back("BLOCK — refused: nested block would create a circular reference.");
        return true;
      }
    }
    st.blockDefs.push_back(std::move(def));
    if (mode == "delete" || mode == "convert")
      EraseSelectedSources(st);
    st.selection.clear();
    if (mode == "convert" || mode.empty()) {
      CadBlockRef r;
      r.defName = f[0];
      r.xf.x = bx;
      r.xf.y = by;
      r.xf.z = bz;
      st.cadBlockRefs.push_back(std::move(r));
      st.cadBlockRefAttrs.push_back(NewBlockAttr(st));
      st.selection.push_back(
          {SelectedEntity::Type::BlockRef, static_cast<int>(st.cadBlockRefs.size()) - 1});
    }
    NoteRecent(st, f[0]);
    BumpCadGpuCache(st);
    log.push_back("BLOCK — created \"" + f[0] + "\".");
    return true;
  }

  if (tok == "insert") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 3) {
      StartInsertBlockCommand(st, log);
      return true;
    }
    CadBlockXform xf;
    try {
      xf.x = std::stof(f[1]);
      xf.y = std::stof(f[2]);
      if (f.size() >= 5) {
        xf.sx = std::stof(f[3]);
        xf.sy = std::stof(f[4]);
      }
      if (f.size() >= 6)
        xf.rotZ = InsertRotZFromCwNorthDeg(std::stof(f[5]));
      if (f.size() >= 7)
        xf.sz = std::stof(f[6]);
      if (f.size() >= 8)
        xf.z = std::stof(f[7]);
    } catch (...) {
      log.push_back("INSERT — coordinates and scales must be numbers.");
      return true;
    }
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
      if (k < 0 || static_cast<size_t>(k) >= st.cadBlockRefs.size())
        continue;
      EntityAttributes a = NewBlockAttr(st);
      if (static_cast<size_t>(k) < st.cadBlockRefAttrs.size())
        a = st.cadBlockRefAttrs[static_cast<size_t>(k)];
      ExplodeRef(st, st.cadBlockRefs[static_cast<size_t>(k)], a);
      st.cadBlockRefs.erase(st.cadBlockRefs.begin() + k);
      if (static_cast<size_t>(k) < st.cadBlockRefAttrs.size())
        st.cadBlockRefAttrs.erase(st.cadBlockRefAttrs.begin() + k);
      ++n;
    }
    st.selection.clear();
    BumpCadGpuCache(st);
    log.push_back("EXPLODE — " + std::to_string(n) + " block reference(s).");
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
    const int di = CadBlockFindDef(st.blockDefs, st.blockEditorName);
    if (di >= 0) {
      if (st.blockEditActive)  // ADR-043: fold the model arrays back into the definition
        HarvestDrawingPrimitivesIntoContent(st, &st.blockDefs[static_cast<size_t>(di)].content);
      st.blockEditorSnapshot = st.blockDefs[static_cast<size_t>(di)];
    }
    st.blockEditorDirty = false;
    BumpCadGpuCache(st);
    st.blockEditCleanRevision = st.cadGpuRevision;
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
    const int di = CadBlockFindDef(st.blockDefs, st.blockEditorName);
    if (discard) {
      if (di >= 0)
        st.blockDefs[static_cast<size_t>(di)] = st.blockEditorSnapshot;
      log.push_back("BCLOSE — discarded edits to \"" + st.blockEditorName + "\".");
    } else {
      if (di >= 0 && st.blockEditActive)
        HarvestDrawingPrimitivesIntoContent(st, &st.blockDefs[static_cast<size_t>(di)].content);
      log.push_back("BCLOSE — closed \"" + st.blockEditorName + "\".");
    }
    if (st.blockEditActive) {
      // The stash captured blockDefs at enter time; keep the definitions as this session left them
      // and take model geometry, INSERT references and everything else back from the stash.
      std::vector<CadBlockDefinition> editedDefs = std::move(st.blockDefs);
      CadRestoreGeometrySnapshot(st, st.blockEditModelStash);
      st.blockDefs = std::move(editedDefs);
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
    if (CadBlockFindDef(st.blockDefs, f[0]) >= 0) {
      log.push_back("BSAVEAS — name \"" + f[0] + "\" is already used.");
      return true;
    }
    const int di = CadBlockFindDef(st.blockDefs, st.blockEditorName);
    if (di < 0) {
      log.push_back("BSAVEAS — editor block missing.");
      return true;
    }
    PushUndoSnapshot(st, "Bsaveas");
    CadBlockDefinition copy = st.blockDefs[static_cast<size_t>(di)];
    copy.name = f[0];
    copy.id = 0;
    st.blockDefs.push_back(std::move(copy));
    st.blockEditorName = f[0];
    log.push_back("BSAVEAS — copied to \"" + f[0] + "\".");
    return true;
  }

  if (tok == "blockredef" || tok == "redefine") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 3) {
      log.push_back("BLOCKREDEF — usage: BLOCKREDEF <name>, <baseX>, <baseY>.");
      return true;
    }
    const int di = CadBlockFindDef(st.blockDefs, f[0]);
    if (di < 0) {
      log.push_back("BLOCKREDEF — no block named \"" + f[0] + "\".");
      return true;
    }
    float bx = 0.f, by = 0.f;
    if (!TryParseF(f[1], bx) || !TryParseF(f[2], by)) {
      log.push_back("BLOCKREDEF — base point must be numbers.");
      return true;
    }
    PushUndoSnapshot(st, "Blockredef");
    CadBlockContent c;
    CaptureSelectionInto(st, &c, bx, by, 0.f);
    st.blockDefs[static_cast<size_t>(di)].content = std::move(c);
    st.blockDefs[static_cast<size_t>(di)].baseX = 0.f;
    st.blockDefs[static_cast<size_t>(di)].baseY = 0.f;
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
    const int di = CadBlockFindDef(st.blockDefs, st.blockEditorName);
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
    st.blockDefs[static_cast<size_t>(di)].attrDefs.push_back(std::move(d));
    st.blockEditorDirty = true;
    log.push_back("ATTDEF — tag " + f[0] + ".");
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
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadBlockRefs.size())
        continue;
      CadBlockAttrSet(&st.cadBlockRefs[static_cast<size_t>(e.index)], f[0], f[1]);
      ++n;
    }
    log.push_back("ATTEDIT — " + std::to_string(n) + " reference(s).");
    return true;
  }

  if (tok == "attsync") {
    PushUndoSnapshot(st, "Attsync");
    int n = 0;
    for (CadBlockRef& r : st.cadBlockRefs) {
      const int di = CadBlockFindDef(st.blockDefs, r.defName);
      if (di < 0)
        continue;
      const CadBlockDefinition& def = st.blockDefs[static_cast<size_t>(di)];
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
    for (const CadBlockRef& r : st.cadBlockRefs) {
      const int di = CadBlockFindDef(st.blockDefs, r.defName);
      if (di < 0)
        continue;
      const CadBlockDefinition& def = st.blockDefs[static_cast<size_t>(di)];
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
    if (st.blockDefs.empty()) {
      log.push_back("BLOCKLIST — (none).");
      return true;
    }
    for (const CadBlockDefinition& d : st.blockDefs) {
      const int refs = CadBlockCountRefs(st.cadBlockRefs, d.name);
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
      if (k >= 0 && static_cast<size_t>(k) < st.cadBlockRefs.size())
        name = st.cadBlockRefs[static_cast<size_t>(k)].defName;
    }
    const int di = CadBlockFindDef(st.blockDefs, name);
    if (di < 0) {
      log.push_back("BLOCKSTATS — name a definition or select a reference.");
      return true;
    }
    const CadBlockDefinition& d = st.blockDefs[static_cast<size_t>(di)];
    log.push_back("Definition: " + d.name);
    log.push_back("References: " + std::to_string(CadBlockCountRefs(st.cadBlockRefs, d.name)));
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
    for (int i = static_cast<int>(st.blockDefs.size()) - 1; i >= 0; --i) {
      const std::string& nm = st.blockDefs[static_cast<size_t>(i)].name;
      if (CadBlockCountRefs(st.cadBlockRefs, nm) > 0 || CadBlockDefUsedBy(st.blockDefs, nm))
        continue;
      if (!f.empty() && !CadBlockEqCi(f[0], "blocks") && !CadBlockEqCi(f[0], nm) &&
          !CadBlockEqCi(f[0], "*"))
        continue;
      st.blockDefs.erase(st.blockDefs.begin() + i);
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
    const int di = CadBlockFindDef(st.blockDefs, f[0]);
    if (di < 0) {
      log.push_back("BLOCKRENAME — no block named \"" + f[0] + "\".");
      return true;
    }
    if (CadBlockFindDef(st.blockDefs, f[1]) >= 0) {
      log.push_back("BLOCKRENAME — \"" + f[1] + "\" is already used.");
      return true;
    }
    PushUndoSnapshot(st, "Blockrename");
    const std::string old = st.blockDefs[static_cast<size_t>(di)].name;
    st.blockDefs[static_cast<size_t>(di)].name = f[1];
    for (CadBlockRef& r : st.cadBlockRefs) {
      if (CadBlockEqCi(r.defName, old))
        r.defName = f[1];
    }
    for (CadBlockDefinition& d : st.blockDefs) {
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
    const int di = CadBlockFindDef(st.blockDefs, st.blockEditorName);
    if (di < 0)
      return true;
    CadBlockParameter p;
    p.name = f[0];
    p.kind = ParseParamKind(f[1]);
    if (f.size() >= 3 && !TryParseF(f[2], p.value)) {
      log.push_back("BPARAM — value must be a number.");
      return true;
    }
    st.blockDefs[static_cast<size_t>(di)].parameters.push_back(std::move(p));
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
    const int di = CadBlockFindDef(st.blockDefs, st.blockEditorName);
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
    st.blockDefs[static_cast<size_t>(di)].actions.push_back(std::move(a));
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
    const int di = CadBlockFindDef(st.blockDefs, st.blockEditorName);
    if (di < 0)
      return true;
    st.blockDefs[static_cast<size_t>(di)].visibilityStates.push_back(f[0]);
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
      if (e.index >= 0 && static_cast<size_t>(e.index) < st.cadBlockRefs.size())
        st.cadBlockRefs[static_cast<size_t>(e.index)].visState = f[0];
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
      if (e.index >= 0 && static_cast<size_t>(e.index) < st.cadBlockRefs.size())
        CadBlockParamSet(&st.cadBlockRefs[static_cast<size_t>(e.index)], f[0], v);
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
    const int di = CadBlockFindDef(st.blockDefs, st.blockEditorName);
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
      st.userLinesFlat.insert(st.userLinesFlat.end(), seg, seg + 6);
      st.userLineAttrs.push_back(EntityAttributes{});
    } else {
      CadBlockContent& c = st.blockDefs[static_cast<size_t>(di)].content;
      c.lines.push_back(x1);
      c.lines.push_back(y1);
      c.lines.push_back(0.f);
      c.lines.push_back(x2);
      c.lines.push_back(y2);
      c.lines.push_back(0.f);
      c.lineAttrs.push_back(EntityAttributes{});
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
    const int hi = CadBlockFindDef(st.blockDefs, f[0]);
    const int ci = CadBlockFindDef(st.blockDefs, f[1]);
    if (hi < 0 || ci < 0) {
      log.push_back("BLOCKNEST — both names must exist.");
      return true;
    }
    if (CadBlockWouldCycle(st.blockDefs, f[0], f[1])) {
      log.push_back("BLOCKNEST — refused circular reference.");
      return true;
    }
    CadBlockNested n;
    n.defName = st.blockDefs[static_cast<size_t>(ci)].name;
    st.blockDefs[static_cast<size_t>(hi)].content.nested.push_back(std::move(n));
    BumpCadGpuCache(st);
    log.push_back("BLOCKNEST — " + f[1] + " inside " + f[0] + ".");
    return true;
  }

  if (tok == "insunits") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.empty()) {
      log.push_back("INSUNITS — " + CadDrawingInsUnitsName(st.drawingInsUnits) + ".");
      return true;
    }
    st.drawingInsUnits = CadDrawingInsUnitsCode(f[0]);
    log.push_back("INSUNITS — drawing units " + CadDrawingInsUnitsName(st.drawingInsUnits) + ".");
    return true;
  }

  if (tok == "blockunits") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("BLOCKUNITS — usage: BLOCKUNITS <name>, <units>.");
      return true;
    }
    const int di = CadBlockFindDef(st.blockDefs, f[0]);
    if (di < 0) {
      log.push_back("BLOCKUNITS — no block named \"" + f[0] + "\".");
      return true;
    }
    st.blockDefs[static_cast<size_t>(di)].units = f[1];
    log.push_back("BLOCKUNITS — " + f[0] + " is " + f[1] + ".");
    return true;
  }

  if (tok == "blocksearch") {
    const std::vector<std::string> f = SplitCommaRest(args);
    const std::string q = f.empty() ? st.blockLibraryFilter : f[0];
    st.blockLibraryFilter = q;
    int n = 0;
    for (const CadBlockDefinition& d : st.blockDefs) {
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
    for (const CadBlockDefinition& d : st.blockDefs) {
      float mnX = 0, mnY = 0, mxX = 0, mxY = 0;
      CadBlockRef preview;
      preview.defName = d.name;
      CadBlockWorldAabb(st.blockDefs, preview, &mnX, &mnY, &mxX, &mxY);
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
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("WBLOCK — usage: WBLOCK <name>, <path.dwg>.");
      return true;
    }
    const int di = CadBlockFindDef(st.blockDefs, f[0]);
    if (di < 0) {
      log.push_back("WBLOCK — no block named \"" + f[0] + "\".");
      return true;
    }
    AppCommandState tmp;
    tmp.blockDefs.push_back(st.blockDefs[static_cast<size_t>(di)]);
    if (!ExportDwgFile(tmp, f[1].c_str(), log)) {
      log.push_back("WBLOCK — could not write " + f[1] + ".");
      return true;
    }
    log.push_back("WBLOCK — wrote \"" + f[0] + "\" to " + f[1] + ".");
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
    if (CadBlockFindDef(st.blockDefs, f[0]) >= 0) {
      log.push_back("MAKEBLOCK — name \"" + f[0] + "\" is already used.");
      return true;
    }
    PushUndoSnapshot(st, "Makeblock");
    CadBlockDefinition def;
    def.name = f[0];
    def.units = CadDrawingInsUnitsName(st.drawingInsUnits);
    st.blockDefs.push_back(std::move(def));
    log.push_back("MAKEBLOCK — created \"" + f[0] + "\".");
    return true;
  }

  if (tok == "mkline") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 4) {
      log.push_back("MKLINE — usage: MKLINE <x0>, <y0>, <x1>, <y1>.");
      return true;
    }
    try {
      const float x0 = std::stof(f[0]);
      const float y0 = std::stof(f[1]);
      const float x1 = std::stof(f[2]);
      const float y1 = std::stof(f[3]);
      PushUndoSnapshot(st, "Mkline");
      st.userLinesFlat.push_back(x0);
      st.userLinesFlat.push_back(y0);
      st.userLinesFlat.push_back(0.f);
      st.userLinesFlat.push_back(x1);
      st.userLinesFlat.push_back(y1);
      st.userLinesFlat.push_back(0.f);
      st.userLineAttrs.push_back(NewBlockAttr(st));
      EnsureEntityIds(st);
      BumpCadGpuCache(st);
      log.push_back("MKLINE — added.");
    } catch (...) {
      log.push_back("MKLINE — coordinates must be numbers.");
    }
    return true;
  }

  if (tok == "selline") {
    st.selection.clear();
    const int n = static_cast<int>(st.userLinesFlat.size() / 6);
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
    if (st.cadBlockRefs.empty()) {
      log.push_back("SELBLOCK — no block references.");
      return true;
    }
    st.selection.push_back(
        {SelectedEntity::Type::BlockRef, static_cast<int>(st.cadBlockRefs.size()) - 1});
    log.push_back("SELBLOCK — insert " + std::to_string(st.cadBlockRefs.size() - 1) + ".");
    return true;
  }

  if (tok == "movesel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("MOVESEL — usage: MOVESEL <dx>, <dy>.");
      return true;
    }
    try {
      const float dx = std::stof(f[0]);
      const float dy = std::stof(f[1]);
      PushUndoSnapshot(st, "Movesel");
      for (const SelectedEntity& e : st.selection) {
        if (e.type != SelectedEntity::Type::BlockRef)
          continue;
        if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadBlockRefs.size())
          continue;
        CadBlockTranslate(&st.cadBlockRefs[static_cast<size_t>(e.index)], dx, dy, 0.f);
      }
      BumpCadGpuCache(st);
      log.push_back("MOVESEL — done.");
    } catch (...) {
      log.push_back("MOVESEL — offsets must be numbers.");
    }
    return true;
  }

  if (tok == "copysel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 2) {
      log.push_back("COPYSEL — usage: COPYSEL <dx>, <dy>.");
      return true;
    }
    float dx = 0.f;
    float dy = 0.f;
    try {
      dx = std::stof(f[0]);
      dy = std::stof(f[1]);
    } catch (...) {
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
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadBlockRefs.size())
        continue;
      CadBlockRef c = st.cadBlockRefs[static_cast<size_t>(e.index)];
      CadBlockTranslate(&c, dx, dy, 0.f);
      extra.push_back(std::move(c));
      EntityAttributes a = NewBlockAttr(st);
      if (static_cast<size_t>(e.index) < st.cadBlockRefAttrs.size())
        a = st.cadBlockRefAttrs[static_cast<size_t>(e.index)];
      a.id = 0;
      extraA.push_back(std::move(a));
      ++n;
    }
    st.cadBlockRefs.insert(st.cadBlockRefs.end(), extra.begin(), extra.end());
    st.cadBlockRefAttrs.insert(st.cadBlockRefAttrs.end(), extraA.begin(), extraA.end());
    EnsureEntityIds(st);
    BumpCadGpuCache(st);
    log.push_back("COPYSEL — " + std::to_string(n) + " reference(s).");
    return true;
  }

  if (tok == "rotatesel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 3) {
      log.push_back("ROTATESEL — usage: ROTATESEL <baseX>, <baseY>, <deg>.");
      return true;
    }
    try {
      const float bx = std::stof(f[0]);
      const float by = std::stof(f[1]);
      const float rad = std::stof(f[2]) * 0.01745329252f;
      PushUndoSnapshot(st, "Rotatesel");
      for (const SelectedEntity& e : st.selection) {
        if (e.type != SelectedEntity::Type::BlockRef)
          continue;
        if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadBlockRefs.size())
          continue;
        CadBlockRotateZ(&st.cadBlockRefs[static_cast<size_t>(e.index)], bx, by, rad);
      }
      BumpCadGpuCache(st);
      log.push_back("ROTATESEL — done.");
    } catch (...) {
      log.push_back("ROTATESEL — values must be numbers.");
    }
    return true;
  }

  if (tok == "scalesel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 3) {
      log.push_back("SCALESEL — usage: SCALESEL <baseX>, <baseY>, <factor>.");
      return true;
    }
    try {
      const float bx = std::stof(f[0]);
      const float by = std::stof(f[1]);
      const float sc = std::stof(f[2]);
      PushUndoSnapshot(st, "Scalesel");
      for (const SelectedEntity& e : st.selection) {
        if (e.type != SelectedEntity::Type::BlockRef)
          continue;
        if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadBlockRefs.size())
          continue;
        CadBlockScaleAbout(&st.cadBlockRefs[static_cast<size_t>(e.index)], bx, by, sc);
      }
      BumpCadGpuCache(st);
      log.push_back("SCALESEL — done.");
    } catch (...) {
      log.push_back("SCALESEL — values must be numbers.");
    }
    return true;
  }

  if (tok == "mirrorsel") {
    const std::vector<std::string> f = SplitCommaRest(args);
    if (f.size() < 4) {
      log.push_back("MIRRORSEL — usage: MIRRORSEL <x0>, <y0>, <x1>, <y1>.");
      return true;
    }
    try {
      const float x0 = std::stof(f[0]);
      const float y0 = std::stof(f[1]);
      const float x1 = std::stof(f[2]);
      const float y1 = std::stof(f[3]);
      PushUndoSnapshot(st, "Mirrorsel");
      for (const SelectedEntity& e : st.selection) {
        if (e.type != SelectedEntity::Type::BlockRef)
          continue;
        if (e.index < 0 || static_cast<size_t>(e.index) >= st.cadBlockRefs.size())
          continue;
        CadBlockMirror(&st.cadBlockRefs[static_cast<size_t>(e.index)], x0, y0, x1, y1);
      }
      BumpCadGpuCache(st);
      log.push_back("MIRRORSEL — done.");
    } catch (...) {
      log.push_back("MIRRORSEL — coordinates must be numbers.");
    }
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
    if (f.size() >= 2) {
      try {
        dx = std::stof(f[0]);
        dy = std::stof(f[1]);
      } catch (...) {
        log.push_back("PASTEBLOCK — offsets must be numbers.");
        return true;
      }
    }
    if (st.clipboard.blockRefs.empty()) {
      log.push_back("PASTEBLOCK — clipboard has no block references.");
      return true;
    }
    PushUndoSnapshot(st, "Pasteblock");
    for (size_t i = 0; i < st.clipboard.blockRefs.size(); ++i) {
      CadBlockRef r = st.clipboard.blockRefs[i];
      CadBlockTranslate(&r, dx, dy, 0.f);
      st.cadBlockRefs.push_back(std::move(r));
      EntityAttributes a = NewBlockAttr(st);
      if (i < st.clipboard.blockRefAttrs.size())
        a = st.clipboard.blockRefAttrs[i];
      a.id = 0;
      st.cadBlockRefAttrs.push_back(std::move(a));
    }
    EnsureEntityIds(st);
    BumpCadGpuCache(st);
    log.push_back("PASTEBLOCK — " + std::to_string(st.clipboard.blockRefs.size()) + " reference(s).");
    return true;
  }

  if (tok == "blockmodel") {
    st.activeSpaceIndex = kModelSpaceIndex;
    log.push_back("BLOCKMODEL — model space.");
    return true;
  }
  if (tok == "blockpaper") {
    if (st.paperLayouts.empty())
      AddPaperLayout(st);
    if (st.paperLayouts.empty()) {
      log.push_back("BLOCKPAPER — no layouts.");
      return true;
    }
    st.activeSpaceIndex = 0;
    log.push_back("BLOCKPAPER — " + st.paperLayouts[0].name);
    return true;
  }

  return false;
}
