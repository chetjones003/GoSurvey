#include "CadCommandsInternal.hpp"
#include "CadBlocks.hpp"
#include "CadColor.hpp"
#include "GoSurvey/Document/cadpiperun.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// =================================================================================================
// B-rep solids (REQ-313 / ADR-045, GitHub issue #146 — Phase 3 of #120)
// =================================================================================================

// =================================================================================================
// B-rep solids (REQ-313 / ADR-045, GitHub issue #146 — Phase 3 of #120)
// =================================================================================================

// --- Sub-object selection (REQ-318 increment 2 / D-2026-09-04-a, issue #148) ---------------------

int ExpireSubObjectSelection(AppCommandState& st) {
  if (st.subObjectSelection.empty())
    return 0;
  const size_t before = st.subObjectSelection.size();
  auto dead = [&](SelectedSubObject& s) {
    // IDENTITY decides, the index is only a lookup (ADR-049). A solid is immutable and REPLACED
    // rather than edited, so an edit that changed the topology has put a *different* object in the
    // store and this `weak_ptr` no longer locks — that is the expiry the ADR is about, and it fires
    // however the replacement happened (a boolean, an undo, a direct edit) with no call site having
    // to remember anything.
    const CadSolidPtr sp = s.owner.lock();
    if (!sp)
      return true;
    // Erasing an UNRELATED solid shifts every index after it. The selection must survive that: the
    // solid the user picked is still there, still the same object, and losing the selection because
    // something else was deleted would be a defect rather than an expiry. So the cached index is
    // REPAIRED from the identity rather than trusted.
    if (s.solidIndex < 0 || static_cast<size_t>(s.solidIndex) >= st.drawing.cadSolids.size() ||
        st.drawing.cadSolids[static_cast<size_t>(s.solidIndex)] != sp) {
      const auto at = std::find(st.drawing.cadSolids.begin(), st.drawing.cadSolids.end(), sp);
      if (at == st.drawing.cadSolids.end())
        return true;  // still alive somewhere, but no longer in this drawing
      s.solidIndex = static_cast<int>(at - st.drawing.cadSolids.begin());
    }
    if (s.index < 0)
      return true;
    switch (s.kind) {
    case solidpick::Kind::Face:   return static_cast<size_t>(s.index) >= sp->faces.size();
    case solidpick::Kind::Edge:   return static_cast<size_t>(s.index) >= sp->edges.size();
    case solidpick::Kind::Vertex: return static_cast<size_t>(s.index) >= sp->vertices.size();
    case solidpick::Kind::None:   return true;
    }
    return true;
  };
  st.subObjectSelection.erase(
      std::remove_if(st.subObjectSelection.begin(), st.subObjectSelection.end(), dead),
      st.subObjectSelection.end());
  return static_cast<int>(before - st.subObjectSelection.size());
}

void ToggleSubObjectSelection(AppCommandState& st, const SelectedSubObject& pick, bool toggle) {
  if (pick.kind == solidpick::Kind::None || pick.index < 0 || pick.solidIndex < 0)
    return;
  auto it = std::find_if(st.subObjectSelection.begin(), st.subObjectSelection.end(),
                         [&](const SelectedSubObject& s) { return s.sameTarget(pick); });
  if (it != st.subObjectSelection.end()) {
    if (toggle)
      st.subObjectSelection.erase(it);
    return;  // a plain click on something already selected is a no-op, not a duplicate
  }
  st.subObjectSelection.push_back(pick);
}

bool PickSubObjectAcrossSolids(const AppCommandState& st, const ray3d::Ray& ray,
                              const solidpick::Tolerance& tol, SelectedSubObject* out,
                              solidpick::Pick* outPick, bool facesPickable) {
  if (!out)
    return false;
  const SectionClipPlane clip = CadActiveSectionClip(st);
  const solidpick::KeepHalfSpace keep{clip.nx, clip.ny, clip.nz, clip.c};
  const solidpick::KeepHalfSpace* keepPtr = clip.active ? &keep : nullptr;
  static const std::vector<float> kNoTriVerts;
  static const std::vector<int> kNoTriFaceIds;
  bool any = false;
  double bestT = 0.0;
  SelectedSubObject best{};
  solidpick::Pick bestPick{};
  for (size_t i = 0; i < st.drawing.cadSolids.size(); ++i) {
    if (!SolidVisible(st, i))
      continue;
    const CadSolidPtr& sp = st.drawing.cadSolids[i];
    // The solid's OWN cached triangles, not the coalesced display batch: the batch merges solids
    // that draw identically into shared buffers (#194) and carries no per-face channel, so neither
    // the pick nor the highlight can be resolved from it (REQ-318 item 13).
    const auto ce = std::find_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                                 [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
    if (ce == st.solidDisplayCache.end() || ce->empty())
      continue;  // never tessellate here — a pick must not cost a tessellation (REQ-318 item 7)
    solidpick::Pick p;
    if (!solidpick::PickSubObject(*sp, facesPickable ? ce->triVerts : kNoTriVerts,
                                  facesPickable ? ce->triFaceIds : kNoTriFaceIds, ray, tol, &p, keepPtr))
      continue;
    // DEBT-1 from TASK-189, closed here. PickSubObject's occlusion rule is per-solid — it cannot
    // know that a nearer solid stands in front of this one — so the cross-solid order is the
    // caller's, on the distance the query returns for exactly this purpose.
    if (any && !(p.rayT < bestT))
      continue;
    any = true;
    bestT = p.rayT;
    bestPick = p;
    best.solidIndex = static_cast<int>(i);
    best.kind = p.kind;
    best.index = p.index;
    best.owner = sp;
  }
  if (!any)
    return false;
  *out = best;
  if (outPick)
    *outPick = bestPick;
  return true;
}

static bool PipeRunWorldSolidVisible(const AppCommandState& st, size_t solidIndex);

/// The nearest PIPE RUN solid under \p ray, as a `SelectedEntity` of `Type::PipeRun` (issue #486).
/// A whole-entity pick only — deliberately NOT routed through `PickSubObjectAcrossSolids`/
/// `SelectedSubObject`, which is scoped to `st.drawing.cadSolids` throughout the sub-object selection
/// system (FILLET/CHAMFER/PRESSPULL edge-and-face picking): a pipe run's derived solid was never
/// meant to be sub-object-edited, only selected/highlighted/erased/reported, the same boundary
/// `Type::Solid` itself states on its own doc comment.
static bool PickClosestPipeRunEntity(const AppCommandState& st, const ray3d::Ray& ray, float tolWorld,
                                     SelectedEntity* out, double* outRayT) {
  if (!out)
    return false;
  solidpick::Tolerance tol;
  tol.vertex = static_cast<double>(tolWorld);
  tol.edge = tol.vertex;
  const bool facesPickable = EffectiveVisualStyle(st) != VisualStyle::Wireframe2D;
  bool any = false;
  double bestT = 0.0;
  int bestOwner = -1;
  for (size_t i = 0; i < st.pipeRunWorldSolids.size(); ++i) {
    if (!PipeRunWorldSolidVisible(st, i))
      continue;
    const CadSolidPtr& sp = st.pipeRunWorldSolids[i];
    const auto ce = std::find_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                                 [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
    if (ce == st.solidDisplayCache.end() || ce->empty())
      continue;  // never tessellate here — a pick must not cost a tessellation (REQ-318 item 7)
    static const std::vector<float> kNoTriVerts;
    static const std::vector<int> kNoTriFaceIds;
    solidpick::Pick p;
    if (!solidpick::PickSubObject(*sp, facesPickable ? ce->triVerts : kNoTriVerts,
                                  facesPickable ? ce->triFaceIds : kNoTriFaceIds, ray, tol, &p, nullptr))
      continue;
    if (any && !(p.rayT < bestT))
      continue;
    any = true;
    bestT = p.rayT;
    bestOwner = i < st.pipeRunWorldSolidOwnerIndex.size() ? st.pipeRunWorldSolidOwnerIndex[i] : -1;
  }
  if (!any || bestOwner < 0)
    return false;
  SelectedEntity e{};
  e.type = SelectedEntity::Type::PipeRun;
  e.index = bestOwner;
  *out = e;
  if (outRayT)
    *outRayT = bestT;
  return true;
}

bool PickClosestSolidEntity(const AppCommandState& st, const ray3d::Ray& ray, float tolWorld,
                            SelectedEntity* out, double* outRayT) {
  if (!out)
    return false;
  solidpick::Tolerance tol;
  tol.vertex = static_cast<double>(tolWorld);
  tol.edge = tol.vertex;
  SelectedSubObject sub{};
  solidpick::Pick pick{};
  // 2D Wireframe draws no faces, so none is clickable there (D-2026-09-16-b).
  const bool facesPickable = EffectiveVisualStyle(st) != VisualStyle::Wireframe2D;
  const bool haveSolid = PickSubObjectAcrossSolids(st, ray, tol, &sub, &pick, facesPickable) &&
                        sub.solidIndex >= 0 && static_cast<std::size_t>(sub.solidIndex) < st.drawing.cadSolids.size();

  // Pipe runs (issue #486): whichever of a real solid or a pipe run's derived solid the ray hits
  // FIRST wins — the same "nearer one answers" rule every other entity-vs-entity precedence in
  // this codebase follows, not a fixed priority between the two kinds.
  SelectedEntity pipeRunHit{};
  double pipeRunT = 0.0;
  const bool havePipeRun = PickClosestPipeRunEntity(st, ray, tolWorld, &pipeRunHit, &pipeRunT);

  if (haveSolid && (!havePipeRun || pick.rayT <= pipeRunT)) {
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Solid;
    e.index = sub.solidIndex;
    *out = e;
    if (outRayT)
      *outRayT = pick.rayT;
    return true;
  }
  if (havePipeRun) {
    *out = pipeRunHit;
    if (outRayT)
      *outRayT = pipeRunT;
    return true;
  }
  return false;
}


bool BuildSubObjectHoverRow(const AppCommandState& st, const SelectedSubObject& s,
                            SubObjectHoverRow* out) {
  if (!out || s.kind == solidpick::Kind::None || s.index < 0)
    return false;
  const CadSolidPtr sp = s.owner.lock();
  if (!sp || s.solidIndex < 0 || static_cast<size_t>(s.solidIndex) >= st.drawing.cadSolids.size() ||
      st.drawing.cadSolids[static_cast<size_t>(s.solidIndex)] != sp)
    return false;  // expired, or the index has not been repaired yet — say nothing rather than guess
  out->title = std::string("Solid ") + solidpick::KindName(s.kind) + " " + std::to_string(s.index);
  // 1-based, matching how the command line numbers solids everywhere else. A readout that counts
  // from zero while the log counts from one is two names for one object.
  out->solid = std::to_string(s.solidIndex + 1);
  static const EntityAttributes kDefaults{};
  const EntityAttributes& a = static_cast<size_t>(s.solidIndex) < st.drawing.cadSolidAttrs.size()
                                  ? st.drawing.cadSolidAttrs[static_cast<size_t>(s.solidIndex)]
                                  : kDefaults;
  // The STORED values, not the resolved ones. "ByLayer" is the answer the user needs — it is what
  // the Properties panel shows and what they would change — where a resolved "#FFFFFF" would hide
  // the fact that the object is following its layer at all. Same choice AutoCAD's rollover makes.
  out->color = a.color.empty() ? std::string("ByLayer") : a.color;
  out->layer = a.layer.empty() ? std::string("0") : a.layer;
  out->linetype = a.linetype.empty() ? std::string("ByLayer") : a.linetype;
  return true;
}

bool SubmitSubObjectPick(AppCommandState& st, const ray3d::Ray& ray, const solidpick::Tolerance& tol,
                         bool toggle, std::vector<std::string>& log) {
  // Mutual exclusion, and the reason it is done HERE rather than by each caller: #148's criterion 2
  // ("sub-object selection does not interfere with whole-entity selection") is only structural if
  // there is one place that enforces it. Two callers each clearing the other selection is two
  // places to forget.
  st.selection.clear();
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;  // Ctrl is unambiguous: it never drags a fence

  SelectedSubObject sub;
  if (!PickSubObjectAcrossSolids(st, ray, tol, &sub)) {
    // A miss CLEARS. It does not arm a box and it does not silently do nothing — REQ-201: the user
    // gets told which of the two just happened.
    const bool had = !st.subObjectSelection.empty();
    st.subObjectSelection.clear();
    log.push_back(had ? "Sub-object selection cleared - nothing under the cursor."
                      : "No solid face, edge or vertex under the cursor.");
    return false;
  }
  const bool wasSelected = std::any_of(st.subObjectSelection.begin(), st.subObjectSelection.end(),
                                       [&](const SelectedSubObject& s) { return s.sameTarget(sub); });
  ToggleSubObjectSelection(st, sub, toggle);
  log.push_back(std::string(toggle && wasSelected ? "Deselected " : "Selected ") +
                solidpick::KindName(sub.kind) + " " + std::to_string(sub.index) + " of solid " +
                std::to_string(sub.solidIndex + 1) + " (" + std::to_string(st.subObjectSelection.size()) +
                " sub-object(s) selected).");
  // If FILLET is running, say what the pick means FOR IT. Here rather than at the click site so
  // every route into the pick gets it - the viewport's Ctrl+click, a transcript's SUBOBJECT verb,
  // and anything later - which is the same reason the pick's own meaning lives in this function
  // rather than in `CadUi.cpp` (REQ-318 / D-2026-09-04-a).
  CadFilletReportEdgeSelection(st, log);
  CadChamferReportEdgeSelection(st, log);
  return true;
}
bool SolidVisible(const AppCommandState& st, size_t solidIndex) {
  if (solidIndex >= st.drawing.cadSolids.size())
    return false;
  if (!st.drawing.cadSolids[solidIndex])
    return false;
  if (solidIndex >= st.drawing.cadSolidAttrs.size())
    return true;  // attrs are length-locked to cadSolids; a short array means defaults, not hidden
  const EntityAttributes& a = st.drawing.cadSolidAttrs[solidIndex];
  // REQ-084 (d): an isolated-out solid is invisible, so it must not be drawn OR answer a click.
  if (CadEntityIdHidden(&st.hiddenEntityIds, a.id))
    return false;
  const CadLayerRow* lr = FindDrawingLayerRowCi(st, a.layer);
  return !(lr && (!lr->on || lr->frozen));
}


static bool BlockRefWorldSolidVisible(const AppCommandState& st, size_t solidIndex) {
  if (solidIndex >= st.blockRefWorldSolids.size())
    return false;
  if (!st.blockRefWorldSolids[solidIndex])
    return false;
  if (solidIndex >= st.blockRefWorldSolidAttrs.size())
    return true;
  const EntityAttributes& a = st.blockRefWorldSolidAttrs[solidIndex];
  if (CadEntityIdHidden(&st.hiddenEntityIds, a.id))
    return false;
  const CadLayerRow* lr = FindDrawingLayerRowCi(st, a.layer);
  return !(lr && (!lr->on || lr->frozen));
}

static std::uint64_t BlockRefWorldSolidsSig(const AppCommandState& st) {
  std::uint64_t sig = 1469598103934665603ull;
  const auto mix = [&sig](std::uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
  mix(st.drawing.cadBlockRefs.size());
  for (size_t bi = 0; bi < st.drawing.cadBlockRefs.size(); ++bi) {
    const CadBlockRef& r = st.drawing.cadBlockRefs[bi];
    for (float v : {r.xf.x, r.xf.y, r.xf.z, r.xf.sx, r.xf.sy, r.xf.sz, r.xf.rotX, r.xf.rotY, r.xf.rotZ}) {
      std::uint32_t bits = 0;
      std::memcpy(&bits, &v, sizeof(bits));
      mix(bits);
    }
    for (unsigned char c : r.defName)
      mix(c);
    // The insert's layer / colour resolve every ByBlock solid inside it, so an edit to them
    // (REQ-352 — a pipe fitting is a block reference) must re-derive the world solids too.
    if (bi < st.drawing.cadBlockRefAttrs.size()) {
      for (unsigned char c : st.drawing.cadBlockRefAttrs[bi].layer)
        mix(c);
      mix(0);
      for (unsigned char c : st.drawing.cadBlockRefAttrs[bi].color)
        mix(c);
    }
  }
  for (const CadBlockDefinition& d : st.drawing.blockDefs)
    mix(d.content.cadSolids.size());
  return sig;
}

static void RebuildBlockRefWorldSolids(AppCommandState& st) {
  const std::uint64_t sig = BlockRefWorldSolidsSig(st);
  if (sig == st.blockRefWorldSolidsSig)
    return;
  st.blockRefWorldSolidsSig = sig;
  st.blockRefWorldSolids.clear();
  st.blockRefWorldSolidAttrs.clear();
  for (size_t bi = 0; bi < st.drawing.cadBlockRefs.size(); ++bi) {
    const EntityAttributes insertAttr =
        bi < st.drawing.cadBlockRefAttrs.size() ? st.drawing.cadBlockRefAttrs[bi] : EntityAttributes{};
    std::vector<CadBlockWorldSolid> ws;
    CadBlockCollectWorldSolids(st.drawing.blockDefs, st.drawing.cadBlockRefs[bi], insertAttr, &ws);
    for (CadBlockWorldSolid& w : ws) {
      if (!w.solid)
        continue;
      st.blockRefWorldSolids.push_back(std::move(w.solid));
      st.blockRefWorldSolidAttrs.push_back(std::move(w.attr));
    }
  }
}

static bool PipeRunWorldSolidVisible(const AppCommandState& st, size_t solidIndex) {
  if (solidIndex >= st.pipeRunWorldSolids.size())
    return false;
  if (!st.pipeRunWorldSolids[solidIndex])
    return false;
  if (solidIndex >= st.pipeRunWorldSolidAttrs.size())
    return true;
  const EntityAttributes& a = st.pipeRunWorldSolidAttrs[solidIndex];
  if (CadEntityIdHidden(&st.hiddenEntityIds, a.id))
    return false;
  const CadLayerRow* lr = FindDrawingLayerRowCi(st, a.layer);
  return !(lr && (!lr->on || lr->frozen));
}

/// One run's content signature, for the per-run solid cache. Covers everything
/// \ref CadBuildPipeRunSolids reads: the path, the size, the wall and the class.
static std::uint64_t SinglePipeRunSig(const CadPipeRun& r) {
  std::uint64_t sig = 1469598103934665603ull;
  const auto mix = [&sig](std::uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
  mix(r.vertsXyz.size());
  for (double v : r.vertsXyz) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    mix(bits);
  }
  for (unsigned char c : r.nominalSize)
    mix(c);
  mix(0x9E3779B9ull);
  std::uint64_t wallBits;
  std::memcpy(&wallBits, &r.wallThicknessIn, sizeof(wallBits));
  mix(wallBits);
  for (unsigned char c : r.pressureClassTag)
    mix(c);
  return sig;
}

static std::uint64_t PipeRunWorldSolidsSig(const AppCommandState& st) {
  std::uint64_t sig = 1469598103934665603ull;
  const auto mix = [&sig](std::uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
  mix(st.drawing.cadPipeRuns.size());
  // Built from the SAME per-run signature the solid cache keys on, so the two cannot disagree about
  // what "changed" means. It previously covered only the path and the size, which left a wall or
  // class change invisible to this gate — the early-out would keep a solid built at the old wall.
  for (const CadPipeRun& r : st.drawing.cadPipeRuns)
    mix(SinglePipeRunSig(r));
  return sig;
}

/// Rebuilds \ref AppCommandState::pipeRunWorldSolids from \ref AppCommandState::cadPipeRuns
/// (issue #486 increment B1) — the same "derived, gated by a signature over the real state" shape
/// \ref RebuildBlockRefWorldSolids already uses. Each run can contribute more than one solid (one
/// cylinder per straight segment, \ref CadBuildPipeRunSolids), so the run's own attributes are
/// replicated across however many solids it actually produced.
static void RebuildPipeRunWorldSolids(AppCommandState& st) {
  const std::uint64_t sig = PipeRunWorldSolidsSig(st);
  if (sig == st.pipeRunWorldSolidsSig) {
    // The signature covers geometry only, so a layer / colour edit (REQ-352) lands here: re-copy
    // each run's attributes onto its solids rather than leave them drawing in the old ones.
    for (size_t i = 0; i < st.pipeRunWorldSolidAttrs.size() && i < st.pipeRunWorldSolidOwnerIndex.size(); ++i) {
      const size_t ri = static_cast<size_t>(st.pipeRunWorldSolidOwnerIndex[i]);
      if (ri < st.drawing.cadPipeRunAttrs.size())
        st.pipeRunWorldSolidAttrs[i] = st.drawing.cadPipeRunAttrs[ri];
    }
    return;
  }
  st.pipeRunWorldSolidsSig = sig;
  st.pipeRunWorldSolids.clear();
  st.pipeRunWorldSolidAttrs.clear();
  st.pipeRunWorldSolidOwnerIndex.clear();
  const auto rebuildT0 = std::chrono::steady_clock::now();
  st.pipeRunSolidCacheSigs.resize(st.drawing.cadPipeRuns.size(), 0);
  st.pipeRunSolidCache.resize(st.drawing.cadPipeRuns.size());
  for (size_t ri = 0; ri < st.drawing.cadPipeRuns.size(); ++ri) {
    const EntityAttributes runAttr = ri < st.drawing.cadPipeRunAttrs.size() ? st.drawing.cadPipeRunAttrs[ri] : EntityAttributes{};
    // Re-sweep only what changed. Appending one vertex while routing used to re-sweep every run in
    // the drawing; a tube costs ~11 ms per point of route, so that was the click cost growing with
    // the whole drawing rather than with the run being drawn.
    const std::uint64_t runSig = SinglePipeRunSig(st.drawing.cadPipeRuns[ri]);
    if (st.pipeRunSolidCacheSigs[ri] != runSig || st.pipeRunSolidCache[ri].empty()) {
      st.pipeRunSolidCacheSigs[ri] = runSig;
      st.pipeRunSolidCache[ri].clear();
      const auto sweepT0 = std::chrono::steady_clock::now();
      (void)CadBuildPipeRunSolids(st.drawing.cadPipeRuns[ri], &st.pipeRunSolidCache[ri]);
      const std::chrono::duration<double, std::milli> sweepMs = std::chrono::steady_clock::now() - sweepT0;
      st.pipeRunPerf.sweep.Add(sweepMs.count());
      ++st.pipeRunPerf.runsSwept;
      st.pipeRunPerf.lastRunVerts = static_cast<int>(st.drawing.cadPipeRuns[ri].vertsXyz.size() / 3);
    } else {
      ++st.pipeRunPerf.runsReused;
    }
    for (const CadSolidPtr& sp : st.pipeRunSolidCache[ri]) {
      st.pipeRunWorldSolids.push_back(sp);  // shared immutable payload (invariant §11.5 amendment)
      st.pipeRunWorldSolidAttrs.push_back(runAttr);
      st.pipeRunWorldSolidOwnerIndex.push_back(static_cast<int>(ri));
    }
  }
  const std::chrono::duration<double, std::milli> rebuildMs = std::chrono::steady_clock::now() - rebuildT0;
  st.pipeRunPerf.rebuild.Add(rebuildMs.count());
}

namespace {

/// The default colour a solid draws in when nothing overrides it — a mid grey that reads as a
/// physical object against both the light and dark viewport grounds, distinct from a surface's blue.
constexpr float kSolidDefaultR = 0.72f;
constexpr float kSolidDefaultG = 0.72f;
constexpr float kSolidDefaultB = 0.74f;

/// Narrow a run of kernel doubles into the float buffer the GPU wants.
///
/// This is the ONE place the narrowing happens, and it happens here rather than in the kernel for
/// the reason ADR-045 (g) gives: the solid's own coordinates stay `double` so its volume stays
/// exact, and only the derived triangles — which really are GPU-bound — pay the narrowing. The
/// values narrowed are storage coordinates, so they are already at local magnitude (REQ-101).
void NarrowInto(const std::vector<double>& src, std::vector<float>* dst) {
  dst->clear();
  dst->reserve(src.size());
  for (double v : src)
    dst->push_back(static_cast<float>(v));
}

/// Expand an indexed tessellation into the flat triangle layout the renderer streams.
///
/// Indexed drawing is what makes a two-million-triangle IMPORTED mesh affordable (REQ-063); a
/// primitive's tessellation is a few thousand triangles, so a second indexed GPU path here would
/// cost more in code than it saves in bandwidth. Expanding also lets the solid path share the
/// stream-upload shape the surface band fills already use.
} // namespace

/// Flattens a `brep::Tessellation` into the flat, per-vertex GL arrays the renderer uploads:
/// triangle vertices, one normal per vertex, and the owning face id per TRIANGLE. Exposed (rather
/// than file-static) because REQ-350's part thumbnails need exactly the same expansion, and a
/// second copy of it would be free to drift from the one the viewport draws — two present-day call
/// sites, which is the bar architecture invariant §11.4 sets for sharing anything at all.
void ExpandTessellation(const brep::Tessellation& t, std::vector<float>* verts, std::vector<float>* normals,
                        std::vector<int>* faceIds) {
  faceIds->clear();
  faceIds->reserve(t.triFace.size());
  for (int f : t.triFace)
    faceIds->push_back(f);
  verts->clear();
  normals->clear();
  verts->reserve(t.indices.size() * 3);
  normals->reserve(t.indices.size() * 3);
  for (std::uint32_t idx : t.indices) {
    const size_t v = static_cast<size_t>(idx) * 3;
    if (v + 2 >= t.vertsXyz.size())
      continue;  // Validate has already ruled this out; belt and braces before a GPU upload
    verts->push_back(static_cast<float>(t.vertsXyz[v]));
    verts->push_back(static_cast<float>(t.vertsXyz[v + 1]));
    verts->push_back(static_cast<float>(t.vertsXyz[v + 2]));
    if (v + 2 < t.normalsXyz.size()) {
      normals->push_back(static_cast<float>(t.normalsXyz[v]));
      normals->push_back(static_cast<float>(t.normalsXyz[v + 1]));
      normals->push_back(static_cast<float>(t.normalsXyz[v + 2]));
    } else {
      normals->push_back(0.f);
      normals->push_back(0.f);
      normals->push_back(1.f);
    }
  }
}


void RefreshSolidDisplayGeometry(AppCommandState& st) {
  // Reap first: an entry whose weak key has expired belongs to a solid that has been erased or
  // replaced. A weak_ptr and not a raw pointer, because a raw key could be matched by a NEW solid
  // allocated at the freed address — the cache would then draw the wrong shape and look plausible.
  st.solidDisplayCache.erase(std::remove_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                                            [](const CadSolidTessellation& e) { return e.key.expired(); }),
                             st.solidDisplayCache.end());

  RebuildBlockRefWorldSolids(st);
  RebuildPipeRunWorldSolids(st);

  const double tol = kSolidChordToleranceFt;
  const int isolines = std::clamp(st.viewportSolidIsolines, 0, kSolidMaxIsolines);

  // Geometry that is still being DRAFTED is tessellated on a coarser circular budget
  // (D-2026-09-24-e). While PIPERUN is routing, its provisional run is rebuilt and re-tessellated on
  // every single click and replaced moments later, so paying the finished-quality budget there is
  // paying it over and over for a picture that is about to be thrown away. Everything else — every
  // other solid, and this very run the moment the command ends — keeps the full budget.
  auto tessellateSolidPtr = [&](const CadSolidPtr& sp, bool draft = false) {
    if (!sp)
      return;
    const int circleSegs = draft ? brep::kDraftFullCircleSegments : brep::kFullCircleSegments;
    auto it = std::find_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                           [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
    // The staleness key is (solid, tolerance) and nothing else. That is #120's "do not regenerate a
    // solid's render mesh every frame" expressed as a property of the code rather than an intention:
    // a solid is immutable, so an unchanged pointer means unchanged geometry, and the early-out here
    // is before any allocation — a `clear()` above it would still cost the frame it was written to
    // save (the §11 invariant 7 lesson the surface cache already learned).
    if (it != st.solidDisplayCache.end() && it->chordTolerance == tol && it->isolineCount == isolines &&
        it->fullCircleSegments == circleSegs)
      return;

    if (it == st.solidDisplayCache.end()) {
      st.solidDisplayCache.push_back(CadSolidTessellation{});
      it = st.solidDisplayCache.end() - 1;
      it->key = sp;
    }
    const auto tessT0 = std::chrono::steady_clock::now();
    it->chordTolerance = tol;
    it->isolineCount = isolines;
    it->fullCircleSegments = circleSegs;
    it->triVerts.clear();
    it->triNormals.clear();
    it->triFaceIds.clear();
    it->edgeVerts.clear();

    brep::Tessellation tess;
    brep::Problem why = brep::Problem::Ok;
    const auto tFaces0 = std::chrono::steady_clock::now();
    if (brep::Tessellate(*sp, tol, &tess, &why, circleSegs))
      ExpandTessellation(tess, &it->triVerts, &it->triNormals, &it->triFaceIds);
    const std::chrono::duration<double, std::milli> facesMs = std::chrono::steady_clock::now() - tFaces0;
    st.pipeRunPerf.tessFaces.Add(facesMs.count());
    st.pipeRunPerf.tessTriangles += static_cast<long long>(it->triVerts.size() / 9);
    std::vector<double> edges;
    const auto tEdges0 = std::chrono::steady_clock::now();
    if (brep::TessellateEdges(*sp, tol, &edges, &why)) {
      // ISOLINES go into the SAME buffer as the edges, not a batch of their own. They are the same
      // colour and the same weight as the object - AutoCAD draws them as part of it - so a second
      // batch would be a second thing to keep in step for no visible difference.
      const std::chrono::duration<double, std::milli> edgesMs = std::chrono::steady_clock::now() - tEdges0;
      st.pipeRunPerf.tessEdges.Add(edgesMs.count());
      std::vector<double> isos;
      const auto tIso0 = std::chrono::steady_clock::now();
      if (brep::TessellateIsolines(*sp, isolines, tol, &isos, &why))
        edges.insert(edges.end(), isos.begin(), isos.end());
      const std::chrono::duration<double, std::milli> isoMs = std::chrono::steady_clock::now() - tIso0;
      st.pipeRunPerf.tessIso.Add(isoMs.count());
      NarrowInto(edges, &it->edgeVerts);
    }
    ++st.solidDisplayRegenCount;  // past the early-out: this frame actually retessellated a solid
    const std::chrono::duration<double, std::milli> tessMs = std::chrono::steady_clock::now() - tessT0;
    st.pipeRunPerf.tessellate.Add(tessMs.count());
    // A solid that fails to tessellate leaves EMPTY buffers rather than stale ones. It cannot
    // normally happen — nothing stores a solid that does not validate (REQ-201) — and drawing the
    // previous solid's triangles under this one's identity would be far worse than drawing nothing.
  };

  for (const CadSolidPtr& sp : st.drawing.cadSolids)
    tessellateSolidPtr(sp);
  for (const CadSolidPtr& sp : st.blockRefWorldSolids)
    tessellateSolidPtr(sp);
  // Only the run PIPERUN is drawing right now is a draft; every other run is finished geometry.
  const auto* pipeSess = st.sessionAs<stepengine::PipeRunSession>();
  const bool routingNow = pipeSess && pipeSess->liveIndex >= 0;
  for (std::size_t i = 0; i < st.pipeRunWorldSolids.size(); ++i) {
    const bool draft = routingNow && i < st.pipeRunWorldSolidOwnerIndex.size() &&
                       st.pipeRunWorldSolidOwnerIndex[i] == pipeSess->liveIndex;
    tessellateSolidPtr(st.pipeRunWorldSolids[i], draft);
  }

  // ----- Assembly: coalesce visible solids into a handful of draw batches (GitHub issue #194) -----
  //
  // One batch per (resolved colour, edge lineweight): every visible solid that would draw identically
  // is merged into a shared vertex buffer, so a 400-solid site model is a few draw calls, not 400
  // face uploads + 400 edge uploads + 400 cache lookups. Unlike the surface path this concatenation
  // copies vertices, so it is gated: an assembly signature over exactly the inputs read below lets an
  // orbit — which changes only the camera — reuse last frame's merged buffers untouched (§11
  // invariant 7, the lesson the surface cache already learned).
  static const EntityAttributes kDefaultSolidAttrs{};
  struct VisibleSolid {
    const CadSolidTessellation* tess = nullptr;
    float rgba[4] = {0.f, 0.f, 0.f, 0.f};
    float lineweightMm = 0.f;
  };
  std::vector<VisibleSolid> visible;
  visible.reserve(st.drawing.cadSolids.size());
  std::uint64_t sig = 1469598103934665603ull;  // FNV-1a offset basis
  const auto mix = [&sig](std::uint64_t v) {
    sig = (sig ^ v) * 1099511628211ull;
  };
  mix(st.solidDisplayRegenCount);
  for (size_t i = 0; i < st.drawing.cadSolids.size(); ++i) {
    if (!SolidVisible(st, i))
      continue;  // filtered here, so the renderer stays ignorant of layers and isolation
    const CadSolidPtr& sp = st.drawing.cadSolids[i];
    const auto it = std::find_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                                 [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
    if (it == st.solidDisplayCache.end() || it->empty())
      continue;
    // A short attribute array means defaults, not a reason to grow one here: `EnsureAttrCounts`
    // owns that repair, and appending mid-walk would both invalidate this loop's own references and
    // create an entity attribute — with a fresh id — from a display refresh, which is the last place
    // that should be minting them.
    const EntityAttributes& attr = i < st.drawing.cadSolidAttrs.size() ? st.drawing.cadSolidAttrs[i] : kDefaultSolidAttrs;
    const CadLayerRow* lr = FindDrawingLayerRowCi(st, attr.layer);
    VisibleSolid vs;
    vs.tess = &*it;
    ResolveEntityRgbaForViewport(attr, lr, kSolidDefaultR, kSolidDefaultG, kSolidDefaultB, vs.rgba);
    vs.lineweightMm = EffectiveEntityLineweightMm(attr, lr);
    visible.push_back(vs);
    mix(reinterpret_cast<std::uintptr_t>(it->triVerts.data()));
    mix(it->triVerts.size());
    mix(it->edgeVerts.size());
    for (float c : vs.rgba) {
      std::uint32_t bits;
      std::memcpy(&bits, &c, sizeof(bits));
      mix(bits);
    }
    std::uint32_t lwBits;
    std::memcpy(&lwBits, &vs.lineweightMm, sizeof(lwBits));
    mix(lwBits);
  }
  for (size_t i = 0; i < st.blockRefWorldSolids.size(); ++i) {
    if (!BlockRefWorldSolidVisible(st, i))
      continue;
    const CadSolidPtr& sp = st.blockRefWorldSolids[i];
    const auto it = std::find_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                                 [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
    if (it == st.solidDisplayCache.end() || it->empty())
      continue;
    const EntityAttributes& attr =
        i < st.blockRefWorldSolidAttrs.size() ? st.blockRefWorldSolidAttrs[i] : kDefaultSolidAttrs;
    const CadLayerRow* lr = FindDrawingLayerRowCi(st, attr.layer);
    VisibleSolid vs;
    vs.tess = &*it;
    ResolveEntityRgbaForViewport(attr, lr, kSolidDefaultR, kSolidDefaultG, kSolidDefaultB, vs.rgba);
    vs.lineweightMm = EffectiveEntityLineweightMm(attr, lr);
    visible.push_back(vs);
    mix(reinterpret_cast<std::uintptr_t>(it->triVerts.data()));
    mix(it->triVerts.size());
    mix(it->edgeVerts.size());
    for (float c : vs.rgba) {
      std::uint32_t bits;
      std::memcpy(&bits, &c, sizeof(bits));
      mix(bits);
    }
    std::uint32_t lwBits;
    std::memcpy(&lwBits, &vs.lineweightMm, sizeof(lwBits));
    mix(lwBits);
  }
  for (size_t i = 0; i < st.pipeRunWorldSolids.size(); ++i) {
    if (!PipeRunWorldSolidVisible(st, i))
      continue;
    const CadSolidPtr& sp = st.pipeRunWorldSolids[i];
    const auto it = std::find_if(st.solidDisplayCache.begin(), st.solidDisplayCache.end(),
                                 [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
    if (it == st.solidDisplayCache.end() || it->empty())
      continue;
    const EntityAttributes& attr =
        i < st.pipeRunWorldSolidAttrs.size() ? st.pipeRunWorldSolidAttrs[i] : kDefaultSolidAttrs;
    const CadLayerRow* lr = FindDrawingLayerRowCi(st, attr.layer);
    VisibleSolid vs;
    vs.tess = &*it;
    ResolveEntityRgbaForViewport(attr, lr, kSolidDefaultR, kSolidDefaultG, kSolidDefaultB, vs.rgba);
    vs.lineweightMm = EffectiveEntityLineweightMm(attr, lr);
    visible.push_back(vs);
    mix(reinterpret_cast<std::uintptr_t>(it->triVerts.data()));
    mix(it->triVerts.size());
    mix(it->edgeVerts.size());
    for (float c : vs.rgba) {
      std::uint32_t bits;
      std::memcpy(&bits, &c, sizeof(bits));
      mix(bits);
    }
    std::uint32_t lwBits;
    std::memcpy(&lwBits, &vs.lineweightMm, sizeof(lwBits));
    mix(lwBits);
  }

  st.solidDisplayGeometry.assemblySig = sig;  // set even on the early-out — the renderer keys on it
  if (sig == st.solidDisplayAssemblySig && !st.solidDisplayGeometry.solids.empty() == !visible.empty())
    return;  // inputs unchanged since last assembly — the merged buffers are still current
  st.solidDisplayAssemblySig = sig;

  st.solidDisplayGeometry.solids.clear();
  const auto quant = [](float v) { return static_cast<int>(std::lround(v * 4096.f)); };
  for (const VisibleSolid& vs : visible) {
    CadSolidDisplayBatch* dst = nullptr;
    for (CadSolidDisplayBatch& b : st.solidDisplayGeometry.solids) {
      const bool sameColour = quant(b.rgba[0]) == quant(vs.rgba[0]) && quant(b.rgba[1]) == quant(vs.rgba[1]) &&
                              quant(b.rgba[2]) == quant(vs.rgba[2]) && quant(b.rgba[3]) == quant(vs.rgba[3]);
      if (sameColour && quant(b.lineweightMm) == quant(vs.lineweightMm)) {
        dst = &b;
        break;
      }
    }
    if (!dst) {
      st.solidDisplayGeometry.solids.push_back(CadSolidDisplayBatch{});
      dst = &st.solidDisplayGeometry.solids.back();
      std::memcpy(dst->rgba, vs.rgba, sizeof(dst->rgba));
      dst->lineweightMm = vs.lineweightMm;
    }
    dst->triVerts.insert(dst->triVerts.end(), vs.tess->triVerts.begin(), vs.tess->triVerts.end());
    dst->triNormals.insert(dst->triNormals.end(), vs.tess->triNormals.begin(), vs.tess->triNormals.end());
    dst->edgeVerts.insert(dst->edgeVerts.end(), vs.tess->edgeVerts.begin(), vs.tess->edgeVerts.end());
  }
}
