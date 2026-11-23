#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"
#include "surface/CadSurfaceInternal.hpp"
#include "layers/CadLayerInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Terrain/contourgen.hpp>
#include <GoSurvey/Terrain/surfacequery.hpp>
#include <GoSurvey/Terrain/tinbuild.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/IO/RasterImage.hpp>
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

namespace {

// EXTRACT (REQ-071) — bake a surface's currently displayed contours into ordinary polylines.
//
// **The contours are regenerated here, not read back from the display cache**, and that is what makes
// REQ-071's first acceptance condition ("at exactly the displayed contour elevations") structural
// rather than a claim. `GenerateContours` is a pure function of (verts, indices, levels); this passes
// the same triangulation pointer and the same resolved style the display pass resolved, through the
// same `ResolveSurfaceContourLevels`, so the output is byte-identical to what is on screen. The cache
// holds the FLATTENED GL_LINES form, which has thrown away the per-contour offsets and the closed
// flag a polyline needs — so reading it back would be both harder and weaker.
//
// **The result is deliberately unlinked from the surface** (REQ-071's statement, not an oversight):
// no back-reference is stored in either direction, so a later rebuild does not touch these polylines
// and erasing the surface does not remove them. There is simply nothing joining them.

/// Append one `ContourResult` as polylines carrying \p attrs. Returns how many were created.
int AppendContoursAsPolylines(AppCommandState& st, const ContourResult& r, const EntityAttributes& attrs) {
  int made = 0;
  for (int c = 0; c < r.contourCount(); ++c) {
    const int begin = r.offsets[static_cast<size_t>(c)];
    const int end = r.offsets[static_cast<size_t>(c) + 1];
    if (end - begin < 2)
      continue;  // a one-vertex contour is a point, not a polyline — see contourgen on the peak case
    if (st.drawing.userPolylineOffsets.empty())
      st.drawing.userPolylineOffsets.push_back(0);
    const int baseVert = st.drawing.userPolylineOffsets.back();
    for (int v = begin; v < end; ++v) {
      st.drawing.pushVertex(CadPolyVertex{r.vertsXyz[static_cast<size_t>(v) * 3 + 0], r.vertsXyz[static_cast<size_t>(v) * 3 + 1], r.vertsXyz[static_cast<size_t>(v) * 3 + 2]});
    }
    st.drawing.userPolylineOffsets.push_back(baseVert + (end - begin));
    // Straight through from the generator: a contour that closed in the surface's interior becomes a
    // closed polyline, and one that ran off the border stays open. Re-deriving it from the vertices
    // would mean comparing floats for the seam, which is what contourgen avoids in the first place.
    st.drawing.userPolylineClosed.push_back(r.closed[static_cast<size_t>(c)] ? 1u : 0u);
    st.drawing.userPolylineAttrs.push_back(attrs);
    ++made;
  }
  return made;
}


/// The attribute store that owns this type's entities, or null for a type that carries none.
[[nodiscard]] std::vector<EntityAttributes>* DrapeAttrStoreFor(AppCommandState& st, SelectedEntity::Type type) {
  switch (type) {
    case SelectedEntity::Type::LineSeg: return &st.drawing.userLineAttrs;
    case SelectedEntity::Type::Polyline: return &st.drawing.userPolylineAttrs;
    case SelectedEntity::Type::FeatureLine: return &st.drawing.featureLineAttrs;
    default: return nullptr;
  }
}

const char* DrapeTypeName(SelectedEntity::Type t) {
  switch (t) {
    case SelectedEntity::Type::LineSeg: return "line";
    case SelectedEntity::Type::Polyline: return "polyline";
    case SelectedEntity::Type::FeatureLine: return "feature line";
    case SelectedEntity::Type::Circle: return "circle";
    case SelectedEntity::Type::Arc: return "arc";
    case SelectedEntity::Type::Ellipse: return "ellipse";
    case SelectedEntity::Type::Solid: return "solid";
    default: return "object";
  }
}

/// One entity's vertices, as pointers into the store so they can be written in place.
struct DrapeTarget {
  SelectedEntity sel;
  std::vector<double*> z;
  std::vector<double> x;
  std::vector<double> y;
};

void GatherDrapeRun(DrapeTarget& t, const std::vector<int>& offsets, std::vector<double>& verts, int pi) {
  if (pi < 0 || static_cast<size_t>(pi) + 1 >= offsets.size())
    return;
  const int v0 = offsets[static_cast<size_t>(pi)];
  const int v1 = offsets[static_cast<size_t>(pi) + 1];
  for (int vi = v0; vi < v1; ++vi) {
    const size_t k = static_cast<size_t>(vi) * 3;
    if (k + 2 >= verts.size())
      break;
    t.x.push_back(verts[k]);
    t.y.push_back(verts[k + 1]);
    t.z.push_back(&verts[k + 2]);
  }
}

/// This entity's vertices, or an empty target for a type that has none to drape.
[[nodiscard]] DrapeTarget GatherDrapeTarget(AppCommandState& st, const SelectedEntity& sel) {
  DrapeTarget t;
  t.sel = sel;
  switch (sel.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(sel.index);
    if (sel.index < 0 || k >= st.drawing.lineCount())
      return t;
    t.x.push_back(st.drawing.line(k).x0);
    t.y.push_back(st.drawing.line(k).y0);
    t.z.push_back(&st.drawing.lineComponent(k, CadLineComponent::Z0));
    t.x.push_back(st.drawing.line(k).x1);
    t.y.push_back(st.drawing.line(k).y1);
    t.z.push_back(&st.drawing.lineComponent(k, CadLineComponent::Z1));
    return t;
  }
  case SelectedEntity::Type::Polyline:
    GatherDrapeRun(t, st.drawing.userPolylineOffsets, st.drawing.userPolylineVerts, sel.index);
    return t;
  case SelectedEntity::Type::FeatureLine:
    GatherDrapeRun(t, st.drawing.featureLineOffsets, st.drawing.featureLineVerts, sel.index);
    return t;
  default:
    return t;
  }
}

/// The surface's cached spatial index, built on first use ΓÇö the same cache SURFELEV's walk keeps,
/// keyed by stable id and TIN pointer (REQ-126 / ADR-039 (c)). A drape of a long feature line would
/// otherwise rescan every triangle once per vertex.
[[nodiscard]] const TinSpatialIndex* DrapeSurfaceIndex(AppCommandState& st, size_t si) {
  const CadSurface& surf = st.drawing.cadSurfaces[si];
  const std::uint64_t surfId = si < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[si].id : 0;
  for (AppCommandState::SurfaceQueryCacheEntry& e : st.surfaceQueryCache) {
    if (e.surfaceId == surfId && e.builtFrom.lock() == surf.tin)
      return &e.index;
  }
  AppCommandState::SurfaceQueryCacheEntry e;
  e.surfaceId = surfId;
  e.builtFrom = surf.tin;
  e.index = BuildTinSpatialIndex(surf.tin->vertsXyz, surf.tin->indices);
  st.surfaceQueryCache.push_back(std::move(e));
  return &st.surfaceQueryCache.back().index;
}

/// One entity's drape, worked out but not yet written.
struct DrapeResolved {
  SelectedEntity sel;
  std::vector<double*> z;
  std::vector<double> newZ;
};

/// What laying these entities on surface `si` would do ΓÇö resolved without writing anything, so a
/// refusal costs nothing and no undo entry is pushed for a drape that turns out to move nothing.
///
/// The one place a drape is worked out, so the `DRAPE` command and the re-drape a rebuild triggers
/// cannot disagree about what "on the ground" means.
///
/// An entity is all-or-nothing: a vertex the surface does not cover leaves the whole entity out
/// (ADR-065 (f)).
[[nodiscard]] std::vector<DrapeResolved> ResolveDrapeOnto(AppCommandState& st, size_t si,
                                                          const std::vector<SelectedEntity>& items,
                                                          bool reportRefusals, std::vector<std::string>& log,
                                                          int* outRefusedKind,
                                                          std::string* outFirstKindRefused) {
  std::vector<DrapeResolved> ready;
  const CadSurface& surf = st.drawing.cadSurfaces[si];
  const TinSpatialIndex* index = DrapeSurfaceIndex(st, si);
  auto groundAt = [&](double x, double y, double* outZ) {
    return (index && !index->empty())
               ? TinElevationAtIndexed(surf.tin->vertsXyz, surf.tin->indices, *index, x, y, outZ)
               : TinElevationAt(surf.tin->vertsXyz, surf.tin->indices, x, y, outZ);
  };

  for (const SelectedEntity& sel : items) {
    if (!DrapeAttrStoreFor(st, sel.type)) {
      // A circle, arc or ellipse cannot be draped and still be itself: its shape is not a list of
      // vertices, and giving sampled points the ground's height would leave something that is no
      // longer a circle. Refused by name (REQ-201), never silently skipped.
      if (outRefusedKind)
        ++*outRefusedKind;
      if (outFirstKindRefused && outFirstKindRefused->empty())
        *outFirstKindRefused = DrapeTypeName(sel.type);
      continue;
    }
    DrapeTarget t = GatherDrapeTarget(st, sel);
    if (t.z.empty())
      continue;
    DrapeResolved r;
    r.sel = sel;
    int off = 0;
    for (size_t i = 0; i < t.z.size(); ++i) {
      double z = 0.0;
      if (!groundAt(t.x[i], t.y[i], &z)) {
        ++off;
        continue;
      }
      r.newZ.push_back(z);
      r.z.push_back(t.z[i]);
    }
    if (off > 0) {
      if (reportRefusals)
        log.push_back(std::string("DRAPE - ") + DrapeTypeName(t.sel.type) + " not draped: " +
                      std::to_string(off) + " of " + std::to_string(t.z.size()) +
                      " vertices are off \"" + surf.name + "\".");
      continue;
    }
    ready.push_back(std::move(r));
  }
  return ready;
}

/// Write what `ResolveDrapeOnto` worked out, stamping each entity's link ΓÇö the surface's stable id
/// to link it, or 0 to bake it (which also clears a link a previous drape left).
int ApplyResolvedDrape(AppCommandState& st, const std::vector<DrapeResolved>& ready, std::uint64_t linkTo) {
  int moved = 0;
  for (const DrapeResolved& r : ready) {
    for (size_t i = 0; i < r.z.size(); ++i)
      *r.z[i] = r.newZ[i];
    if (std::vector<EntityAttributes>* attrs = DrapeAttrStoreFor(st, r.sel.type)) {
      if (r.sel.index >= 0 && static_cast<size_t>(r.sel.index) < attrs->size())
        (*attrs)[static_cast<size_t>(r.sel.index)].drapedOnSurfaceId = linkTo;
    }
    ++moved;
  }
  return moved;
}

/// Every entity currently linked to this surface, in store order.
[[nodiscard]] std::vector<SelectedEntity> EntitiesLinkedToSurface(const AppCommandState& st, size_t si) {
  std::vector<SelectedEntity> out;
  const std::uint64_t id = si < st.drawing.cadSurfaceAttrs.size() ? st.drawing.cadSurfaceAttrs[si].id : 0;
  if (id == 0)
    return out;  // an unswept surface has no id to have been linked by (REQ-076)
  auto sweep = [&](const std::vector<EntityAttributes>& attrs, SelectedEntity::Type type) {
    for (size_t i = 0; i < attrs.size(); ++i)
      if (attrs[i].drapedOnSurfaceId == id)
        out.push_back({type, static_cast<int>(i)});
  };
  sweep(st.drawing.userLineAttrs, SelectedEntity::Type::LineSeg);
  sweep(st.drawing.userPolylineAttrs, SelectedEntity::Type::Polyline);
  sweep(st.drawing.featureLineAttrs, SelectedEntity::Type::FeatureLine);
  return out;
}

}  // namespace

void ReDrapeLinkedToSurface(AppCommandState& st, size_t si, std::vector<std::string>& log,
                            bool invalidateCache) {
  if (si >= st.drawing.cadSurfaces.size())
    return;
  const CadSurface& surf = st.drawing.cadSurfaces[si];
  if (!surf.tin || surf.tin->indices.empty())
    return;
  const std::vector<SelectedEntity> linked = EntitiesLinkedToSurface(st, si);
  if (linked.empty())
    return;
  const std::uint64_t id = st.drawing.cadSurfaceAttrs[si].id;
  const std::vector<DrapeResolved> ready =
      ResolveDrapeOnto(st, si, linked, /*reportRefusals=*/false, log, nullptr, nullptr);
  const int moved = ApplyResolvedDrape(st, ready, id);
  if (moved > 0) {
    if (invalidateCache) BumpCadGpuCache(st);
    log.push_back("DRAPE - " + std::to_string(moved) + " linked object(s) re-draped onto \"" + surf.name +
                  "\".");
  }
  const int missed = static_cast<int>(linked.size()) - moved;
  if (missed > 0)
    log.push_back("DRAPE - " + std::to_string(missed) +
                  " linked object(s) left where they are: no longer over \"" + surf.name + "\".");
}


/// The name of the surface this entity is draped on and follows, or empty when it follows none
/// (ADR-065 (d), GitHub #150).
///
/// The one place the link is turned into something a person reads, so the Properties panel and the
/// `DRAPELINKS` report cannot disagree about what is linked.
///
/// Empty covers three cases that are the same to the user ΓÇö never linked, baked since, or linked to
/// a surface that has been erased. The last is ADR-065 (e): the id stays on the entity and simply
/// stops resolving, so the geometry is no longer following anything and must not claim to be.
std::string DrapedOnSurfaceName(const AppCommandState& st, const SelectedEntity& e) {
  const std::vector<EntityAttributes>* attrs = nullptr;
  switch (e.type) {
    case SelectedEntity::Type::LineSeg: attrs = &st.drawing.userLineAttrs; break;
    case SelectedEntity::Type::Polyline: attrs = &st.drawing.userPolylineAttrs; break;
    case SelectedEntity::Type::FeatureLine: attrs = &st.drawing.featureLineAttrs; break;
    default: return std::string();
  }
  if (e.index < 0 || static_cast<size_t>(e.index) >= attrs->size())
    return std::string();
  const std::uint64_t id = (*attrs)[static_cast<size_t>(e.index)].drapedOnSurfaceId;
  if (id == 0)
    return std::string();
  const int si = FindSurfaceIndexById(st, id);
  if (si < 0)
    return std::string();  // erased: it follows nothing now (ADR-065 (e))
  return st.drawing.cadSurfaces[static_cast<size_t>(si)].name;
}



/// `DRAPELINKS` ΓÇö every object in the drawing that follows a surface, and which one.
///
/// The drawing-wide half of ADR-065 (d): the Properties panel answers "does THIS one move?", and this
/// answers "what in here moves when I rebuild?" ΓÇö which is the question actually asked before editing
/// a surface. A link that only existed in a file and in a panel would be a hidden attribute by any
/// practical measure.
void ExecuteDrapeLinksCommand(AppCommandState& st, std::vector<std::string>& log) {
  struct Row {
    const char* type;
    int index;
    std::string surface;
  };
  std::vector<Row> rows;
  int dangling = 0;
  auto sweep = [&](const std::vector<EntityAttributes>& attrs, SelectedEntity::Type type,
                   const char* label) {
    for (size_t i = 0; i < attrs.size(); ++i) {
      if (attrs[i].drapedOnSurfaceId == 0)
        continue;
      SelectedEntity e{type, static_cast<int>(i)};
      const std::string name = DrapedOnSurfaceName(st, e);
      if (name.empty()) {
        // The id is set but no longer resolves: the surface was erased. Counted and reported rather
        // than listed as though it still followed something (ADR-065 (e)).
        ++dangling;
        continue;
      }
      rows.push_back({label, static_cast<int>(i), name});
    }
  };
  sweep(st.drawing.userLineAttrs, SelectedEntity::Type::LineSeg, "line");
  sweep(st.drawing.userPolylineAttrs, SelectedEntity::Type::Polyline, "polyline");
  sweep(st.drawing.featureLineAttrs, SelectedEntity::Type::FeatureLine, "feature line");

  if (rows.empty() && dangling == 0) {
    log.push_back("DRAPELINKS - nothing in the drawing follows a surface.");
    return;
  }
  for (const Row& r : rows)
    log.push_back(std::string("DRAPELINKS - ") + r.type + " " + std::to_string(r.index) +
                  " follows \"" + r.surface + "\".");
  log.push_back("DRAPELINKS - " + std::to_string(rows.size()) + " object(s) follow a surface.");
  if (dangling > 0)
    log.push_back("DRAPELINKS - " + std::to_string(dangling) +
                  " object(s) name a surface that is gone; they follow nothing and stay where they are.");
}



/// `DRAPE <surface>[, LINK]` ΓÇö lay the selection on a surface, each vertex taking the elevation of
/// the ground under it (REQ-074's query; GitHub issue #150, 3D Phase 7).
///
/// **Baked unless `LINK` is asked for** (ADR-065 (a)/(b)). A baked drape stamps the elevations once
/// and stores nothing, so the drawing never changes shape because somebody edited a surface. `LINK`
/// stores the surface's **stable entity id** (ADR-065 (c) ΓÇö never its name, which can be changed,
/// and never its array index, which another surface takes after an erase), and the geometry
/// re-drapes whenever that surface is rebuilt.
///
/// Draping without `LINK` also **clears** a link a previous drape left: "bake this where it is now"
/// is the natural way to ask for that, and leaving a stale link behind would move the geometry again
/// at the next rebuild.
///
/// A vertex the surface does not cover is not draped, and its whole entity is refused by name with
/// the count (ADR-065 (f)). `TinElevationAt` never extrapolates (REQ-074), so there is no elevation
/// to give it; draping the covered vertices and leaving the rest at their old height would make a
/// shape that is neither the original nor the ground ΓÇö wrong in a way that looks plausible, which
/// REQ-201 forbids. Every other entity in the selection still drapes.
void ExecuteDrapeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(StringUtil::trimCopy(args));
  std::string surfaceName = f.empty() ? std::string() : StringUtil::trimCopy(f[0]);
  bool link = false;
  for (size_t i = 1; i < f.size(); ++i) {
    const std::string opt = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(f[i]));
    if (opt == "link") {
      link = true;
    } else if (!opt.empty()) {
      log.push_back("DRAPE - usage: DRAPE <surface>[, LINK]. Unknown option \"" +
                    StringUtil::trimCopy(f[i]) + "\".");
      return;
    }
  }

  // Same disambiguation as EXTRACT: one surface needs no naming, several are listed rather than
  // guessed, because picking silently is how the wrong surface gets used.
  if (surfaceName.empty()) {
    if (st.drawing.cadSurfaces.size() == 1) {
      surfaceName = st.drawing.cadSurfaces[0].name;
    } else if (st.drawing.cadSurfaces.empty()) {
      log.push_back("DRAPE - the drawing has no surfaces.");
      return;
    } else {
      std::string names;
      for (const CadSurface& s : st.drawing.cadSurfaces)
        names += (names.empty() ? "" : ", ") + s.name;
      log.push_back("DRAPE - usage: DRAPE <surface>[, LINK]. Surfaces: " + names + ".");
      return;
    }
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("DRAPE - no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& surf = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (!surf.tin || surf.tin->indices.empty()) {
    log.push_back("DRAPE - \"" + surf.name + "\" has never been built; nothing to drape onto.");
    return;
  }
  if (st.selection.empty()) {
    log.push_back("DRAPE - select the objects to drape first.");
    return;
  }

  std::uint64_t linkTo = 0;
  if (link) {
    // `EnsureEntityIds` is what assigns a surface its id; one created in this same batch still has
    // 0, and a link to 0 would resolve to nothing forever (REQ-076).
    EnsureEntityIds(st);
    linkTo = static_cast<size_t>(si) < st.drawing.cadSurfaceAttrs.size()
                 ? st.drawing.cadSurfaceAttrs[static_cast<size_t>(si)].id
                 : 0;
    if (linkTo == 0) {
      log.push_back("DRAPE - \"" + surf.name + "\" has no stable id yet; cannot link to it.");
      return;
    }
  }

  int refusedKind = 0;
  std::string firstKindRefused;
  const std::vector<DrapeResolved> ready = ResolveDrapeOnto(
      st, static_cast<size_t>(si), st.selection, /*reportRefusals=*/true, log, &refusedKind, &firstKindRefused);
  if (ready.empty()) {
    // Nothing will move, so no undo entry is pushed: an undo after a refused drape must take back
    // whatever the user did BEFORE it, not a step that changed nothing.
    if (refusedKind > 0)
      log.push_back("DRAPE - nothing draped: " + std::to_string(refusedKind) + " selected object(s) (" +
                    firstKindRefused + ") cannot be draped.");
    else
      log.push_back("DRAPE - nothing draped.");
    return;
  }

  PushUndoSnapshot(st, "Drape");
  const int moved = ApplyResolvedDrape(st, ready, linkTo);
  BumpCadGpuCache(st);
  std::string msg = "DRAPE - " + std::to_string(moved) + " object(s) draped onto \"" + surf.name + "\"" +
                    (link ? ", linked." : ".");
  if (refusedKind > 0)
    msg += " " + std::to_string(refusedKind) + " could not be draped (" + firstKindRefused + ").";
  log.push_back(msg);
}


/// `PADSOLID <surface>, <boundary entity id>, <elevation>` — the earthwork a building pad represents,
/// as solids you can see and measure (GitHub #150, 3D Phase 7).
///
/// Produces up to **two** solids: the CUT (ground above the pad — material to dig out) and the FILL
/// (ground below it — material to bring in). A real pad cut into a slope is usually both, they are
/// physically different shapes, and they are billed separately, so they are never merged into one
/// lump whose volume would be the two partly cancelling each other out (D-2026-10-02-a).
///
/// The depth is given as the **finished elevation** of the pad floor, which is how a site plan states
/// it — not as a thickness below ground, which would follow every bump and have no flat floor.
///
/// The boundary is a closed polyline named by its stable entity id, resolved through
/// `VolumeClipRingLocalXy` — the same resolver `VOLUMES` uses for REQ-131's clip, so a ring that
/// bounds a volume and a ring that bounds a pad cannot mean different things.

void BuildPadSolids(AppCommandState& st, const std::string& surfaceName, std::uint64_t boundaryId,
                    double padZ, std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("PADSOLID - no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& surf = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (!surf.tin || surf.tin->indices.empty()) {
    log.push_back("PADSOLID - \"" + surf.name + "\" has never been built; nothing to cut into.");
    return;
  }
  std::vector<std::pair<double, double>> ring;
  std::string ringErr;
  if (!VolumeClipRingLocalXy(st, boundaryId, &ring, &ringErr) || ring.size() < 3) {
    log.push_back("PADSOLID - the boundary " +
                  (ringErr.empty() ? std::string("must be a closed polyline") : ringErr) + ".");
    return;
  }
  if (!std::isfinite(padZ)) {
    log.push_back("PADSOLID - the pad elevation is not a number.");
    return;
  }
  // The grid the pad is built on. Deliberately coarser than the volume sampler's 250,000 cells: every
  // cell becomes four or more B-rep faces, so that resolution would be a solid with a million faces.
  // ~1,600 cells is a few thousand faces, which measures and draws like any other solid, and the
  // staircase it leaves on the boundary is smaller than the ring's own vertex spacing on any real
  // site.
  constexpr double kTargetPadCells = 1600.0;
  double minX = ring[0].first, maxX = minX, minY = ring[0].second, maxY = minY;
  for (const auto& p : ring) {
    minX = std::min(minX, p.first);
    maxX = std::max(maxX, p.first);
    minY = std::min(minY, p.second);
    maxY = std::max(maxY, p.second);
  }
  const double w = maxX - minX;
  const double h = maxY - minY;
  if (!(w > 0.0) || !(h > 0.0)) {
    log.push_back("PADSOLID - the boundary encloses no area.");
    return;
  }
  const double cell = std::max(std::sqrt((w * h) / kTargetPadCells), 1e-6);
  const int cols = std::max(1, static_cast<int>(std::ceil(w / cell)));
  const int rows = std::max(1, static_cast<int>(std::ceil(h / cell)));
  const double cellW = w / static_cast<double>(cols);
  const double cellH = h / static_cast<double>(rows);

  const TinSpatialIndex index = BuildTinSpatialIndex(surf.tin->vertsXyz, surf.tin->indices);
  const auto groundAt = [&](double x, double y, double* z) {
    return index.empty() ? TinElevationAt(surf.tin->vertsXyz, surf.tin->indices, x, y, z)
                         : TinElevationAtIndexed(surf.tin->vertsXyz, surf.tin->indices, index, x, y, z);
  };

  // Node elevations first; a node the surface does not cover makes every cell touching it unbuildable,
  // which is how the pad stops at the edge of the survey rather than extrapolating (REQ-074).
  const int nx = cols + 1;
  const int ny = rows + 1;
  std::vector<double> nodeZ(static_cast<size_t>(nx) * static_cast<size_t>(ny), 0.0);
  std::vector<std::uint8_t> nodeOk(nodeZ.size(), 0);
  for (int j = 0; j < ny; ++j) {
    for (int i = 0; i < nx; ++i) {
      const size_t k = static_cast<size_t>(j) * static_cast<size_t>(nx) + static_cast<size_t>(i);
      double z = 0.0;
      if (groundAt(minX + cellW * i, minY + cellH * j, &z)) {
        nodeZ[k] = z;
        nodeOk[k] = 1;
      }
    }
  }

  // A cell belongs to the cut or the fill by where its CENTRE sits — inside the ring, and which side
  // of the pad elevation the ground is. Centres are what REQ-131's clip already uses, so a pad and a
  // bounded volume over the same ring agree about which cells are in.
  std::vector<std::uint8_t> cutIn(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0);
  std::vector<std::uint8_t> fillIn(cutIn.size(), 0);
  int outside = 0;
  for (int cj = 0; cj < rows; ++cj) {
    for (int ci = 0; ci < cols; ++ci) {
      const size_t c = static_cast<size_t>(cj) * static_cast<size_t>(cols) + static_cast<size_t>(ci);
      bool corners = true;
      for (int dj = 0; dj <= 1 && corners; ++dj)
        for (int di = 0; di <= 1 && corners; ++di)
          if (!nodeOk[(static_cast<size_t>(cj)+ dj) * static_cast<size_t>(nx) + (static_cast<size_t>(ci)+ di)])
            corners = false;
      if (!corners) {
        ++outside;
        continue;
      }
      const double cx = minX + cellW * (static_cast<double>(ci) + 0.5);
      const double cy = minY + cellH * (static_cast<double>(cj) + 0.5);
      if (!TinPointInPolygon(cx, cy, ring))
        continue;
      double gz = 0.0;
      if (!groundAt(cx, cy, &gz)) {
        ++outside;
        continue;
      }
      if (gz > padZ)
        cutIn[c] = 1;
      else if (gz < padZ)
        fillIn[c] = 1;
    }
  }

  brep::HeightField hf;
  hf.originX = minX;
  hf.originY = minY;
  hf.cellW = cellW;
  hf.cellH = cellH;
  hf.cols = cols;
  hf.rows = rows;
  hf.nodeZ = nodeZ;
  hf.flatZ = padZ;

  struct Made { const char* what; double volume; };
  std::vector<Made> made;
  std::vector<brep::Solid> solids;
  const auto build = [&](std::vector<std::uint8_t>& mask, bool flatIsBottom, const char* what) {
    if (std::find(mask.begin(), mask.end(), 1) == mask.end())
      return;
    hf.cellIn = mask;
    hf.flatIsBottom = flatIsBottom;
    brep::Solid s;
    brep::Problem why = brep::Problem::Ok;
    if (!brep::MakeHeightFieldSolid(hf, &s, &why)) {
      log.push_back(std::string("PADSOLID - no ") + what + " solid: " + brep::ProblemText(why) + ".");
      return;
    }
    const brep::MassProperties mp = brep::ComputeMassProperties(s);
    made.push_back({what, mp.valid ? mp.volume : 0.0});
    solids.push_back(std::move(s));
  };
  build(cutIn, /*flatIsBottom=*/true, "cut");
  build(fillIn, /*flatIsBottom=*/false, "fill");

  if (solids.empty()) {
    log.push_back("PADSOLID - the ground already sits at " + FormatLinear(padZ, st.displayLinearPrecision) +
                  " across the boundary; there is nothing to cut or fill.");
    return;
  }

  PushUndoSnapshot(st, "Pad solid");
  for (brep::Solid& s : solids) {
    st.drawing.cadSolids.push_back(std::make_shared<brep::Solid>(std::move(s)));
    st.drawing.cadSolidAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Solid, {}));  // REQ-361
  }
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);

  const int p = st.displayLinearPrecision;
  for (const Made& m : made)
    log.push_back(std::string("PADSOLID - ") + m.what + " solid created, volume " + FormatVolumeYd3(m.volume, p) + ".");
  if (outside > 0)
    log.push_back("PADSOLID - " + std::to_string(outside) +
                  " cell(s) of the boundary are not over \"" + surf.name + "\" and were left out.");
}


namespace {


/// The surface PADSOLID will use if the user does not name one: the one it used last, else the only
/// surface in the drawing. Empty when there is a genuine choice to make.
std::string PadSolidDefaultSurface(const AppCommandState& st) {
  if (!st.padSolidSurface.empty() && FindSurfaceIndex(st, st.padSolidSurface) >= 0)
    return st.padSolidSurface;
  if (st.drawing.cadSurfaces.size() == 1)
    return st.drawing.cadSurfaces[0].name;
  return std::string();
}

/// The options prompt PADSOLID sits on, naming what it already has — the shape every other command
/// with keywords uses, so a user can see the current surface rather than having to remember it.
void PadSolidPrompt(AppCommandState& st, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PadSolidSession>();
  if (!ps)
    return;
  const std::string surf = PadSolidDefaultSurface(st);
  ps->phase = stepengine::PadSolidPhase::WaitOptions;
  log.push_back("PADSOLID - specify pad elevation, or [Surface]" +
                (surf.empty() ? std::string(" (no surface chosen yet)") : " <" + surf + ">") +
                ". ESC cancels.");
}

/// The boundary the command will use: the closed polyline the user has selected, or 0.
std::uint64_t PadSolidSelectedBoundary(AppCommandState& st, int* outCount) {
  int n = 0;
  std::uint64_t id = 0;
  for (const SelectedEntity& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline || e.index < 0)
      continue;
    if (static_cast<size_t>(e.index) >= st.drawing.userPolylineAttrs.size())
      continue;
    ++n;
    id = st.drawing.userPolylineAttrs[static_cast<size_t>(e.index)].id;
  }
  if (outCount)
    *outCount = n;
  return id;
}


}  // namespace

void StartPadSolidCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  auto sess = std::make_unique<stepengine::PadSolidSession>();
  auto* ps = sess.get();
  stepengine::BeginSession(st, std::move(sess));
  EnsureEntityIds(st);

  int n = 0;
  const std::uint64_t id = PadSolidSelectedBoundary(st, &n);
  if (n == 1) {
    ps->boundaryId = id;
    PadSolidPrompt(st, log);
    return;
  }
  ps->phase = stepengine::PadSolidPhase::WaitBoundary;
  log.push_back(n > 1 ? "PADSOLID - select ONE closed polyline for the boundary. ESC cancels."
                      : "PADSOLID - select the closed polyline that bounds the pad. ESC cancels.");
}


/// One typed line while PADSOLID is running: a pad elevation, or `S` to name the surface.
void HandlePadSolidText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PadSolidSession>();
  if (!ps)
    return;
  using P = stepengine::PadSolidPhase;
  const std::string tr = StringUtil::trimCopy(line);
  const std::string low = StringUtil::toLowerAsciiCopy(tr);

  if (ps->phase == P::WaitSurfaceName) {
    if (tr.empty()) {
      PadSolidPrompt(st, log);
      return;
    }
    if (FindSurfaceIndex(st, tr) < 0) {
      std::string names;
      for (const CadSurface& s : st.drawing.cadSurfaces)
        names += (names.empty() ? "" : ", ") + s.name;
      log.push_back("PADSOLID - no surface named \"" + tr + "\"." +
                    (names.empty() ? "" : " Surfaces: " + names + "."));
      return;
    }
    st.padSolidSurface = tr;
    PadSolidPrompt(st, log);
    return;
  }

  if (ps->phase != P::WaitOptions)
    return;

  if (low == "s" || low == "surface") {
    std::string names;
    for (const CadSurface& s : st.drawing.cadSurfaces)
      names += (names.empty() ? "" : ", ") + s.name;
    ps->phase = P::WaitSurfaceName;
    log.push_back(names.empty() ? "PADSOLID - the drawing has no surfaces."
                                : "PADSOLID - enter surface name. Surfaces: " + names + ".");
    return;
  }
  if (tr.empty()) {
    PadSolidPrompt(st, log);
    return;
  }

  char* end = nullptr;
  const double padZ = std::strtod(tr.c_str(), &end);
  if (!end || end == tr.c_str() || *end != '\0' || !std::isfinite(padZ)) {
    log.push_back("PADSOLID - enter a pad elevation, or S to choose the surface.");
    return;
  }
  const std::string surf = PadSolidDefaultSurface(st);
  if (surf.empty()) {
    log.push_back("PADSOLID - choose a surface first: type S.");
    return;
  }
  const std::uint64_t bid = ps->boundaryId;
  st.padSolidSurface = surf;
  BuildPadSolids(st, surf, bid, padZ, log);
  stepengine::EndSession(st);
}

/// A viewport pick while PADSOLID is waiting for its boundary.
void HandlePadSolidPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  auto* ps = st.sessionAs<stepengine::PadSolidSession>();
  if (!ps)
    return;
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2) ||
      hit.type != SelectedEntity::Type::Polyline) {
    log.push_back("PADSOLID - that is not a closed polyline. Pick the ring that bounds the pad.");
    return;
  }
  if (hit.index < 0 || static_cast<size_t>(hit.index) >= st.drawing.userPolylineAttrs.size())
    return;
  EnsureEntityIds(st);
  const std::uint64_t id = st.drawing.userPolylineAttrs[static_cast<size_t>(hit.index)].id;
  std::vector<std::pair<double, double>> ring;
  std::string err;
  if (!VolumeClipRingLocalXy(st, id, &ring, &err) || ring.size() < 3) {
    log.push_back("PADSOLID - that polyline is not closed; the boundary must be a closed ring.");
    return;
  }
  ps->boundaryId = id;
  ClearCadSelection(st);
  st.selection.push_back(hit);
  PadSolidPrompt(st, log);
}

/// `PADSOLID` on its own starts the prompted command; `PADSOLID <surface>, <elevation>` does the
/// whole thing in one line for a script or a transcript. Both end in the same `BuildPadSolids`.
void ExecutePadSolidCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string trimmed = StringUtil::trimCopy(args);
  if (trimmed.empty()) {
    StartPadSolidCommand(st, log);
    return;
  }
  const std::vector<std::string> f = SplitCommaFields(trimmed);
  if (f.size() != 2 || f[0].empty() || f[1].empty()) {
    log.push_back("PADSOLID - usage: PADSOLID <surface>, <pad elevation>, or just PADSOLID to be asked.");
    return;
  }
  int n = 0;
  const std::uint64_t id = PadSolidSelectedBoundary(st, &n);
  if (n != 1) {
    log.push_back(n == 0 ? "PADSOLID - select the closed polyline that bounds the pad first."
                         : "PADSOLID - select ONE closed polyline for the boundary, not " +
                               std::to_string(n) + ".");
    return;
  }
  char* end = nullptr;
  const double padZ = std::strtod(f[1].c_str(), &end);
  if (!end || end == f[1].c_str() || *end != '\0' || !std::isfinite(padZ)) {
    log.push_back("PADSOLID - pad elevation must be a number, not \"" + f[1] + "\".");
    return;
  }
  st.padSolidSurface = f[0];
  BuildPadSolids(st, f[0], id, padZ, log);
}


void ExecuteExtractCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  SurfaceStyles::EnsureStandard(st.drawing.surfaceStyles);

  const std::vector<std::string> f = SplitCommaFields(StringUtil::trimCopy(args));
  std::string surfaceName = f.empty() ? std::string() : f[0];
  const std::string layerArg = f.size() > 1 ? f[1] : std::string();

  if (f.size() > 2) {
    log.push_back("EXTRACT — usage: EXTRACT <surface>[, <layer>]");
    return;
  }

  // A drawing with exactly one surface does not need to be told which one. With more than one, the
  // command names them rather than guessing — picking silently is how the wrong surface gets baked.
  if (surfaceName.empty()) {
    if (st.drawing.cadSurfaces.size() == 1) {
      surfaceName = st.drawing.cadSurfaces[0].name;
    } else if (st.drawing.cadSurfaces.empty()) {
      log.push_back("EXTRACT — the drawing has no surfaces.");
      return;
    } else {
      std::string names;
      for (const CadSurface& s : st.drawing.cadSurfaces)
        names += (names.empty() ? "" : ", ") + s.name;
      log.push_back("EXTRACT — usage: EXTRACT <surface>[, <layer>]. Surfaces: " + names + ".");
      return;
    }
  }

  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("EXTRACT — no surface named \"" + surfaceName + "\".");
    return;
  }
  const CadSurface& surf = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (!surf.tin || surf.tin->indices.empty()) {
    log.push_back("EXTRACT — \"" + surf.name + "\" has never been built; nothing to extract.");
    return;
  }

  const SurfaceStyle* stylePtr = SurfaceStyles::Resolve(st.drawing.surfaceStyles, surf.styleName);
  const SurfaceStyle style = stylePtr ? *stylePtr : SurfaceStyles::StandardSurfaceStyle();
  const char* distUnit = CadDrawingInsUnitsAbbrev(st.drawing.drawingInsUnits);  // issue #832

  // REQ-071's last acceptance condition. Refused BEFORE anything is created, and the message says
  // which style switched them off — "nothing happened" with no reason is the exact silence REQ-201
  // exists to prevent.
  if (!style.minorContour.visible && !style.majorContour.visible) {
    log.push_back("EXTRACT — \"" + surf.name + "\" has both contour components switched off in style \"" +
                  style.name + "\"; nothing extracted. Turn contours on in the Surface Style editor first.");
    return;
  }

  const SurfaceContourLevels levels = ResolveSurfaceContourLevels(*surf.tin, style);
  if (levels.suppressed) {
    // ASSUMPTION-2: a style whose interval is too fine to DISPLAY is displaying no contours, so
    // extracting some would be exactly the "silently extracting a hidden interval" the requirement
    // forbids — and it is the runaway the display cap exists to prevent.
    log.push_back("EXTRACT — \"" + surf.name + "\" style \"" + style.name + "\" asks for " +
                  std::to_string(levels.levelsAsked) +
                  " contour levels, more than are drawn, so none are displayed and none were "
                  "extracted. Use a larger contour interval.");
    return;
  }
  if (levels.empty()) {
    log.push_back("EXTRACT — \"" + surf.name + "\" displays no contours at style \"" + style.name +
                  "\"'s intervals over its " + SurfaceStyles::FormatFt(levels.minZ) + " to " +
                  SurfaceStyles::FormatFt(levels.maxZ) + " " + distUnit + " range; nothing extracted.");
    return;
  }

  // Resolve the target layer BEFORE the snapshot, so an invalid name refuses without leaving an undo
  // step that did nothing.
  std::string layer = st.drawing.currentLayer.empty() ? std::string("0") : st.drawing.currentLayer;
  bool layerCreated = false;
  if (!layerArg.empty()) {
    if (const CadLayerRow* row = FindDrawingLayerRowCi(st, layerArg)) {
      layer = row->name;  // the existing spelling, so a case variant does not become a second layer
    } else {
      if (!ValidNewLayerNameChars(StringUtil::trimCopy(layerArg))) {
        log.push_back("EXTRACT — \"" + layerArg +
                      "\" is not a valid layer name (empty, too long, or reserved characters).");
        return;
      }
      layer = StringUtil::trimCopy(layerArg);
      layerCreated = true;
    }
  }

  // ONE snapshot for the whole command, so EXTRACT is a single undo step even when it also created
  // the layer. CadAddDrawingLayer is deliberately not used: it pushes a snapshot of its own, which
  // would make undoing an extraction take two presses.
  PushUndoSnapshot(st, "Extract contours");
  if (layerCreated) {
    CadLayerRow row;
    row.name = layer;
    st.drawing.drawingLayerTable.push_back(row);
  }

  const auto attrsFor = [&](const SurfaceComponentStyle& comp) {
    EntityAttributes a;
    a.layer = layer;
    // Q2: the extracted set looks like what was on screen, so a major contour stays heavier than a
    // minor one once it is ordinary geometry. A component's "ByLayer" now resolves against the
    // POLYLINE's layer, which is correct — it is an ordinary entity from here on.
    a.color = comp.color;
    a.linetype = comp.linetype;
    a.lineweightMm = comp.lineweightMm;
    a.transparency = -1.f;
    return a;  // id stays 0; the next EnsureEntityIds sweep assigns it, like any created entity
  };

  ContourResult r;
  int minorMade = 0, majorMade = 0;
  if (!levels.minor.empty()) {
    GenerateContours(surf.tin->vertsXyz, surf.tin->indices, levels.minor, &r);
    minorMade = AppendContoursAsPolylines(st, r, attrsFor(style.minorContour));
  }
  if (!levels.major.empty()) {
    GenerateContours(surf.tin->vertsXyz, surf.tin->indices, levels.major, &r);
    majorMade = AppendContoursAsPolylines(st, r, attrsFor(style.majorContour));
  }
  BumpCadGpuCache(st);

  // Levels existed but every contour at them was degenerate — a level sitting exactly on a peak is a
  // single point, not a polyline (see contourgen). Reported as the nothing it is, rather than as
  // "0 polyline(s)" wedged into a sentence that reads as a success.
  if (minorMade + majorMade == 0) {
    log.push_back("EXTRACT — \"" + surf.name + "\" produced no contour polylines at style \"" +
                  style.name + "\"'s intervals; nothing was created.");
    return;
  }

  // REQ-201, and Q3: BOTH intervals, each with its own count. Reporting one interval after
  // extracting two sets would be true and misleading at the same time.
  std::string msg = "EXTRACT — \"" + surf.name + "\": ";
  if (minorMade > 0)
    msg += std::to_string(minorMade) + " minor contour(s) at " +
           SurfaceStyles::FormatFt(style.minorIntervalFt) + " " + distUnit;
  if (minorMade > 0 && majorMade > 0)
    msg += ", ";
  if (majorMade > 0)
    msg += std::to_string(majorMade) + " major contour(s) at " +
           SurfaceStyles::FormatFt(style.majorIntervalFt) + " " + distUnit;
  msg += " — " + std::to_string(minorMade + majorMade) + " polyline(s) on layer \"" + layer + "\"" +
         (layerCreated ? " (created)" : "") + ".";
  log.push_back(msg);

  // Q4: a half-enabled style extracts the half that is displayed and SAYS which half it skipped,
  // rather than refusing work the user can plainly see on screen.
  if (!style.minorContour.visible)
    log.push_back("EXTRACT — minor contours are switched off in style \"" + style.name +
                  "\" and were not extracted.");
  if (!style.majorContour.visible)
    log.push_back("EXTRACT — major contours are switched off in style \"" + style.name +
                  "\" and were not extracted.");
  log.push_back("EXTRACT — the new polylines are ordinary geometry and are not linked to the "
                "surface: rebuilding or erasing it leaves them unchanged.");
}

bool AttachModelRasterImage(AppCommandState& st, const std::string& path, float x, float y,
                            float width, float height, float rotationRad, std::vector<std::string>& log) {
  if (path.empty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
      !std::isfinite(height) || !std::isfinite(rotationRad) || width <= 0.f || height <= 0.f) {
    log.push_back("IMAGEATTACH — path and finite placement with positive width and height are required; no image was added.");
    return false;
  }
  CadRasterImage image;
  std::string why;
  double localX = 0.0, localY = 0.0;
  CadCoord::LocalFromWorld(st, x, y, &localX, &localY);
  if (!MakeSheetRasterImage(path, static_cast<float>(localX), static_cast<float>(localY),
                            width, height, rotationRad, &image, &why)) {
    log.push_back("IMAGEATTACH — image \"" + path + "\" could not be read (" + why + "); no image was added.");
    return false;
  }
  EnsureEntityIds(st);
  image.id = AllocEntityId(st);
  image.layer = st.drawing.currentLayer.empty() ? "0" : st.drawing.currentLayer;
  PushUndoSnapshot(st, "Attach image");
  st.drawing.cadRasterImages.push_back(std::move(image));
  BumpCadGpuCache(st);
  log.push_back("IMAGEATTACH — attached \"" + path + "\" as image " +
                std::to_string(st.drawing.cadRasterImages.back().id) + ".");
  return true;
}
