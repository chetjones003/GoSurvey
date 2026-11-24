#include "CadCommandsInternal.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include "GoSurvey/Document/cadpiperun.hpp"
#include <GoSurvey/Geometry/meshgeom.hpp>
#include <GoSurvey/Geometry/spline.hpp>
#include <GoSurvey/Terrain/tinbuild.hpp>
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

void ClearPendingViewportZoom(AppCommandState& st) {
  st.pendingZoomExtents = false;
  st.pendingZoomWindow = false;
}

void RequestZoomExtents(AppCommandState& st) {
  ClearPendingViewportZoom(st);
  st.pendingZoomExtents = true;
}


namespace {

// REQ-123 (GitHub #100). "Visible through this viewport" means the same thing here as it does to
// the viewport renderer and to the plotter: an entity whose layer is frozen in the viewport is not
// there. Both of those read a MISSING attribute entry as the default — layer "0" — rather than as
// "no layer", so this does too; otherwise freezing layer "0" would hide geometry on screen and
// still drag the extents out to it.
//
/// Plan (X/Y) bounds of a TIN's `double` vertex store (Phase G, ADR-054) — `meshgeom::ComputeBounds`
/// stays `float`, matching the GPU-mesh types it otherwise serves, so this stays a tiny local helper
/// rather than widening that shared utility for its one non-mesh caller.
struct TinPlanBounds {
  bool valid = false;
  double mnX = 0.0, mxX = 0.0, mnY = 0.0, mxY = 0.0;
};

[[nodiscard]] TinPlanBounds ComputeTinPlanBounds(const std::vector<double>& vertsXyz) {
  TinPlanBounds b;
  if (vertsXyz.size() < 3)
    return b;
  b.valid = true;
  b.mnX = b.mxX = vertsXyz[0];
  b.mnY = b.mxY = vertsXyz[1];
  for (size_t i = 3; i + 2 < vertsXyz.size(); i += 3) {
    b.mnX = std::min(b.mnX, vertsXyz[i]);
    b.mxX = std::max(b.mxX, vertsXyz[i]);
    b.mnY = std::min(b.mnY, vertsXyz[i + 1]);
    b.mxY = std::max(b.mxY, vertsXyz[i + 1]);
  }
  return b;
}

// A null p vp means no filter at all, which is every caller but the floating-viewport one.
[[nodiscard]] bool EntityHiddenInViewport(const Viewport* vp, const std::vector<EntityAttributes>& attrs,
                                          size_t idx) {
  if (!vp)
    return false;
  static const EntityAttributes kDef{};
  const EntityAttributes& a = idx < attrs.size() ? attrs[idx] : kDef;
  return IsLayerFrozenInViewport(*vp, a.layer);
}

[[nodiscard]] bool LayerHiddenInViewport(const Viewport* vp, const std::string& layer) {
  return vp && IsLayerFrozenInViewport(*vp, layer);
}

}  // namespace

bool ComputeWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY, double* outMxY,
                         const Viewport* vpFilter) {
  bool any = false;
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  auto consider = [&](double x, double y) { ExpandExtents(x, y, &mnX, &mxX, &mnY, &mxY, &any); };

  if (st.drawing.lineStoreWellFormed()) {
    for (size_t i = 0; i < st.drawing.lineCount(); ++i) {
      const CadLineRecord ln = st.drawing.line(i);
      if (EntityHiddenInViewport(vpFilter, st.drawing.userLineAttrs, i))
        continue;
      consider(static_cast<double>(ln.x0), static_cast<double>(ln.y0));
      consider(static_cast<double>(ln.x1), static_cast<double>(ln.y1));
    }
  }
  if (st.drawing.circleStoreWellFormed()) {
    for (size_t ci = 0; ci < st.drawing.circleCount(); ++ci) {
      if (EntityHiddenInViewport(vpFilter, st.drawing.userCircleAttrs, ci))
        continue;
      const CadCircleRecord c = st.drawing.circle(ci);
      const double cx = c.cx;
      const double cy = c.cy;
      const double r = std::fabs(c.r);
      if (r <= 1e-12)
        continue;
      consider(cx - r, cy - r);
      consider(cx + r, cy - r);
      consider(cx - r, cy + r);
      consider(cx + r, cy + r);
    }
  }
  for (const SurveyPoint& p : st.drawing.surveyPoints) {
    if (LayerHiddenInViewport(vpFilter, p.layer))
      continue;
    consider(static_cast<double>(p.easting), static_cast<double>(p.northing));
  }
  for (size_t mi = 0; mi < st.drawing.cadPositionMarkers.size(); ++mi) {  // REQ-359: marker + label
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadPositionMarkerAttrs, mi))
      continue;
    float mnX = 0.f, mnY = 0.f, mxX = 0.f, mxY = 0.f;
    CadPositionMarkerLocalBox(st, mi, &mnX, &mnY, &mxX, &mxY);
    consider(mnX, mnY);
    consider(mxX, mxY);
  }
  for (size_t li = 0; li < st.drawing.cadMultileaders.size(); ++li) {  // REQ-367: path + label
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadMultileaderAttrs, li))
      continue;
    float mnX = 0.f, mnY = 0.f, mxX = 0.f, mxY = 0.f;
    CadMultileaderLocalBox(st.drawing.cadMultileaders[li], &mnX, &mnY, &mxX, &mxY);
    consider(mnX, mnY);
    consider(mxX, mxY);
  }

  for (size_t ai = 0; ai < st.drawing.cadAnnotations.size(); ++ai) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadAnnotationAttrs, ai))
      continue;
    const CadAnnotation& a = st.drawing.cadAnnotations[ai];
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(a, st.drawing.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    consider(static_cast<double>(amnX), static_cast<double>(amnY));
    consider(static_cast<double>(amxX), static_cast<double>(amxY));
  }

  for (size_t ti = 0; ti < st.drawing.cadTables.size(); ++ti) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadTableAttrs, ti))
      continue;
    float tmnX = 0.f, tmnY = 0.f, tmxX = 0.f, tmxY = 0.f;
    CadTableWorldAabb(st.drawing.cadTables[ti], &tmnX, &tmnY, &tmxX, &tmxY);
    consider(static_cast<double>(tmnX), static_cast<double>(tmnY));
    consider(static_cast<double>(tmxX), static_cast<double>(tmxY));
  }

  for (size_t bi = 0; bi < st.drawing.cadBlockRefs.size(); ++bi) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadBlockRefAttrs, bi))
      continue;
    float bmnX = 0.f, bmnY = 0.f, bmxX = 0.f, bmxY = 0.f;
    CadBlockWorldAabb(st.drawing.blockDefs, st.drawing.cadBlockRefs[bi], &bmnX, &bmnY, &bmxX, &bmxY);
    consider(static_cast<double>(bmnX), static_cast<double>(bmnY));
    consider(static_cast<double>(bmxX), static_cast<double>(bmxY));
  }

  for (size_t arcIx = 0; arcIx < st.drawing.userArcs.size(); ++arcIx) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.userArcAttrs, arcIx))
      continue;
    const CadArc& a = st.drawing.userArcs[arcIx];
    const double dcx = static_cast<double>(a.cx);
    const double dcy = static_cast<double>(a.cy);
    const double dr = std::fabs(static_cast<double>(a.r));
    if (dr <= 1e-12)
      continue;
    const int n = std::max(8, static_cast<int>(std::fabs(static_cast<double>(a.sweepRad)) / (3.14159265 / 16.0)) + 1);
    // Sampled in the arc's own plane (REQ-312). A tilted arc does not pass through the points its
    // XY parametrisation names, so bounds taken from those are not bounds at all — they can come
    // out SMALLER than the arc, cropping it out of ZOOM EXTENTS rather than merely approximating it.
    const bool arcFlat = IsFlatNormal(a.nx, a.ny, a.nz);
    const ucs::Ucs arcPlane = arcFlat ? ucs::Ucs{} : CurvePlane(a);
    for (int i = 0; i <= n; ++i) {
      const double t = CurveSampleAngle(static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), i, n);
      if (!arcFlat) {
        const ray3d::Vec3 p = CurvePointAt(arcPlane, dr, t);
        consider(p.x, p.y);
        continue;
      }
      double wx = 0.;
      double wy = 0.;
      CirclePointWorld(dcx, dcy, dr, t, &wx, &wy);
      consider(wx, wy);
    }
  }

  for (size_t elIx = 0; elIx < st.drawing.userEllipses.size(); ++elIx) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.userEllAttrs, elIx))
      continue;
    const CadEllipse& el = st.drawing.userEllipses[elIx];
    // Quantize through the DXF text grid so this sweep agrees with the header sweep in
    // DxfIo.cpp (issue #113). Without it a `ratio` like 0.3333333 loses ~3e-7 on write and
    // moves the extents by ~1e-5; the header then describes a different drawing than the
    // entity records, and the rebase on import shifts every coordinate by a float spacing.
    const float qMajVx =
        static_cast<float>(std::stod(std::to_string(static_cast<double>(el.majVx))));
    const float qMajVy =
        static_cast<float>(std::stod(std::to_string(static_cast<double>(el.majVy))));
    const float qRatio =
        static_cast<float>(std::stod(std::to_string(static_cast<double>(el.ratio))));
    const double ma = std::hypot(static_cast<double>(qMajVx), static_cast<double>(qMajVy));
    if (ma < 1e-12)
      continue;
    constexpr int n = 48;
    constexpr double kTwoPi = 6.283185307179586;
    const double ux = static_cast<double>(qMajVx) / ma;
    const double uy = static_cast<double>(qMajVy) / ma;
    const double px = -uy;
    const double py = ux;
    const double mb = ma * static_cast<double>(qRatio);
    const double ecx = static_cast<double>(el.cx);
    const double ecy = static_cast<double>(el.cy);
    for (int i = 0; i < n; ++i) {
      const double ang = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
      const double c = std::cos(ang);
      const double s = std::sin(ang);
      consider(ecx + ux * (ma * c) + px * (mb * s), ecy + uy * (ma * c) + py * (mb * s));
    }
  }

  // REQ-443: the curve itself, not its control polygon - a control point can sit well outside it.
  {
    std::vector<ray3d::Vec3> sp;
    for (size_t si = 0; si < st.drawing.cadSplines.size(); ++si) {
      if (EntityHiddenInViewport(vpFilter, st.drawing.cadSplineAttrs, si))
        continue;
      spline::Tessellate(st.drawing.cadSplines[si], kSplineSampleTolerance, &sp);
      for (const ray3d::Vec3& v : sp)
        consider(v.x, v.y);
    }
  }
  // REQ-480 alignments: the exact geometry's bounds (arcs bounded by their extreme points), so ZOOM EXTENTS
  // frames a drawing that holds only an alignment, grown to take in its labels (D-2026-11-04-c).
  for (const alignment::Alignment& al : st.drawing.alignments) {
    alignment::Point2 mn{}, mx{};
    if (!AlignmentLabelSets::LabelledBounds(al, CadAlignmentLabelContext(st), &mn, &mx))
      continue;
    consider(mn.x, mn.y);
    consider(mx.x, mx.y);
  }
  for (size_t pi = 0; pi < st.drawing.cadPoints.size(); ++pi) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadPointAttrs, pi))
      continue;
    consider(st.drawing.cadPoints[pi].x, st.drawing.cadPoints[pi].y);
  }

  const auto& PO = st.drawing.userPolylineOffsets;
  if (PO.size() >= 2) {
    for (size_t pi = 0; pi + 1 < PO.size(); ++pi) {
      if (EntityHiddenInViewport(vpFilter, st.drawing.userPolylineAttrs, pi))
        continue;
      const int v0 = PO[pi];
      const int v1 = PO[pi + 1];
      for (int vi = v0; vi < v1; ++vi) {
        consider(st.drawing.vertex(static_cast<size_t>(vi)).x, st.drawing.vertex(static_cast<size_t>(vi)).y);
      }
    }
  }

  // Feature lines (REQ-087). This is the SMALL-drawing path — under 16 entities — and a drawing
  // holding only feature lines lands here, so missing it would mean ZOOM EXTENTS reporting
  // "nothing to frame" on a drawing that plainly has content.
  {
    const auto& FV = st.drawing.featureLineVerts;
    const auto& FO = st.drawing.featureLineOffsets;
    for (size_t fi = 0; fi + 1 < FO.size(); ++fi) {
      if (EntityHiddenInViewport(vpFilter, st.drawing.featureLineAttrs, fi))
        continue;
      const int v0 = FO[fi];
      const int v1 = FO[fi + 1];
      for (int vi = v0; vi < v1; ++vi) {
        if ((static_cast<size_t>(vi)* 3 + 1) >= FV.size())
          break;
        consider(static_cast<double>(FV[(static_cast<size_t>(vi)* 3 + 0)]),
                 static_cast<double>(FV[(static_cast<size_t>(vi)* 3 + 1)]));
      }
    }
  }

  for (size_t fri = 0; fri < st.drawing.cadFilledRegions.size(); ++fri) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadFilledRegionAttrs, fri))
      continue;
    const CadFilledRegion& fr = st.drawing.cadFilledRegions[fri];
    for (size_t i = 0; i + 2 < fr.vertsXyz.size(); i += 3)
      consider(static_cast<double>(fr.vertsXyz[i]), static_cast<double>(fr.vertsXyz[i + 1]));
  }

  // Meshes (REQ-063). Their precomputed bounds, not their vertices: this path runs for small
  // drawings, and "small" counts entities — one mesh can still hold two million triangles.
  for (size_t mi = 0; mi < st.drawing.cadMeshes.size(); ++mi) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadMeshAttrs, mi))
      continue;
    const auto& mp = st.drawing.cadMeshes[mi];
    if (!mp)
      continue;
    const meshgeom::Bounds mb = meshgeom::ComputeBounds(mp->vertsXyz);
    if (!mb.valid)
      continue;
    consider(static_cast<double>(mb.mnX), static_cast<double>(mb.mnY));
    consider(static_cast<double>(mb.mxX), static_cast<double>(mb.mxY));
  }

  // Point clouds (REQ-171). Only the points currently resident are known here (ADR-060 — the
  // out-of-core cache may hold more than is loaded), so extents reflect what is actually resident,
  // same as any other geometry store.
  for (size_t pci = 0; pci < st.drawing.cadPointClouds.size(); ++pci) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadPointCloudAttrs, pci))
      continue;
    const auto& pc = st.drawing.cadPointClouds[pci];
    if (!pc)
      continue;
    for (size_t i = 0; i + 2 < pc->pointsXyz.size(); i += 3)
      consider(pc->pointsXyz[i], pc->pointsXyz[i + 1]);
  }

  // B-rep solids (REQ-313). Their ANALYTIC bounds, not their tessellation: a sphere has two
  // vertices and a torus four, so a bounds walk over stored points would miss almost the whole
  // solid, and the tessellation may not have been generated yet at all.
  for (size_t soi = 0; soi < st.drawing.cadSolids.size(); ++soi) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadSolidAttrs, soi))
      continue;
    const CadSolidPtr& sp = st.drawing.cadSolids[soi];
    if (!sp)
      continue;
    const brep::Bounds sb = brep::ComputeBounds(*sp);
    if (!sb.valid)
      continue;
    consider(sb.mn.x, sb.mn.y);
    consider(sb.mx.x, sb.mx.y);
  }

  for (size_t bi = 0; bi < st.drawing.cadBlockRefs.size(); ++bi) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadBlockRefAttrs, bi))
      continue;
    std::vector<CadBlockWorldSolid> ws;
    CadBlockCollectWorldSolids(st.drawing.blockDefs, st.drawing.cadBlockRefs[bi],
                               bi < st.drawing.cadBlockRefAttrs.size() ? st.drawing.cadBlockRefAttrs[bi] : EntityAttributes{},
                               &ws);
    for (const CadBlockWorldSolid& w : ws) {
      if (!w.solid)
        continue;
      const brep::Bounds sb = brep::ComputeBounds(*w.solid);
      if (!sb.valid)
        continue;
      consider(sb.mn.x, sb.mn.y);
      consider(sb.mx.x, sb.mx.y);
    }
  }

  // TIN surfaces (REQ-068: "surfaces are included in zoom-extents and in the drawing's bounding
  // box"). Their bounds, not their vertices, for the same reason meshes use theirs above — a single
  // surface can hold 200k triangles.
  for (size_t si = 0; si < st.drawing.cadSurfaces.size(); ++si) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadSurfaceAttrs, si))
      continue;
    const CadSurface& s = st.drawing.cadSurfaces[si];
    if (!s.tin)
      continue;
    const TinPlanBounds sb = ComputeTinPlanBounds(s.tin->vertsXyz);
    if (!sb.valid)
      continue;
    consider(sb.mnX, sb.mnY);
    consider(sb.mxX, sb.mxY);
  }

  if (!any)
    return false;
  *outMnX = mnX;
  *outMxX = mxX;
  *outMnY = mnY;
  *outMxY = mxY;
  return true;
}

namespace {

struct EntityBox {
  double cx;
  double cy;
  double mnX;
  double mxX;
  double mnY;
  double mxY;
  /// The entity's elevation range (GitHub issue #564 §1, D-2026-09-28-d). Read ONLY by the 3D
  /// extents behind an orbited ZOOM EXTENTS; the plan-view path reads X/Y alone, unchanged.
  double mnZ = 0.;
  double mxZ = 0.;
};

/// Widen \p b's elevation range to include \p z (the box's first Z sets both ends when \p first).
inline void EntityBoxGrowZ(EntityBox& b, double z, bool first) {
  if (first) {
    b.mnZ = b.mxZ = z;
  } else {
    b.mnZ = std::min(b.mnZ, z);
    b.mxZ = std::max(b.mxZ, z);
  }
}

[[nodiscard]] double NthPercentile(std::vector<double>& v, double p) {
  if (v.empty())
    return 0.;
  const size_t n = v.size();
  const double idxF = p * static_cast<double>(n - 1);
  const size_t k = std::clamp(static_cast<size_t>(idxF), size_t{0}, n - 1);
  std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
  return v[k];
}

// \p vpFilter as on ComputeWorldExtents (REQ-123): both sweeps must agree, or the robust path and
// the small-drawing path would answer differently for the same viewport.
void CollectEntityBoxes(const AppCommandState& st, std::vector<EntityBox>& out, const Viewport* vpFilter) {
  if (st.drawing.lineStoreWellFormed()) {
    for (size_t i = 0; i < st.drawing.lineCount(); ++i) {
      const CadLineRecord ln = st.drawing.line(i);
      if (EntityHiddenInViewport(vpFilter, st.drawing.userLineAttrs, i))
        continue;
      EntityBox b{};
      b.mnX = std::min(static_cast<double>(ln.x0), static_cast<double>(ln.x1));
      b.mxX = std::max(static_cast<double>(ln.x0), static_cast<double>(ln.x1));
      b.mnY = std::min(static_cast<double>(ln.y0), static_cast<double>(ln.y1));
      b.mxY = std::max(static_cast<double>(ln.y0), static_cast<double>(ln.y1));
      EntityBoxGrowZ(b, static_cast<double>(ln.z0), true);
      EntityBoxGrowZ(b, static_cast<double>(ln.z1), false);
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }
  if (st.drawing.circleStoreWellFormed()) {
    for (size_t ci = 0; ci < st.drawing.circleCount(); ++ci) {
      if (EntityHiddenInViewport(vpFilter, st.drawing.userCircleAttrs, ci))
        continue;
      const CadCircleRecord c = st.drawing.circle(ci);
      const double cx = c.cx;
      const double cy = c.cy;
      const double r = std::fabs(c.r);
      if (r <= 1e-12)
        continue;
      EntityBox b{};
      b.mnX = cx - r;
      b.mxX = cx + r;
      b.mnY = cy - r;
      b.mxY = cy + r;
      b.cx = cx;
      b.cy = cy;
      // A tilted circle (REQ-312) rises and falls by up to its radius; a flat one sits at its Z.
      const size_t ni = ci * 3;
      const bool flat = ni + 2 >= st.drawing.userCircleNormals.size() ||
                        IsFlatNormal(st.drawing.userCircleNormals[ni], st.drawing.userCircleNormals[ni + 1],
                                     st.drawing.userCircleNormals[ni + 2]);
      const double cz = c.cz;
      EntityBoxGrowZ(b, flat ? cz : cz - r, true);
      EntityBoxGrowZ(b, flat ? cz : cz + r, false);
      out.push_back(b);
    }
  }
  for (const SurveyPoint& p : st.drawing.surveyPoints) {
    if (LayerHiddenInViewport(vpFilter, p.layer))
      continue;
    EntityBox b{};
    b.mnX = b.mxX = b.cx = static_cast<double>(p.easting);
    b.mnY = b.mxY = b.cy = static_cast<double>(p.northing);
    EntityBoxGrowZ(b, p.elevation, true);
    out.push_back(b);
  }
  for (size_t mi = 0; mi < st.drawing.cadPositionMarkers.size(); ++mi) {  // REQ-359: marker + label
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadPositionMarkerAttrs, mi))
      continue;
    float mnX = 0.f, mnY = 0.f, mxX = 0.f, mxY = 0.f;
    CadPositionMarkerLocalBox(st, mi, &mnX, &mnY, &mxX, &mxY);
    EntityBox b{};
    b.mnX = mnX;
    b.mnY = mnY;
    b.mxX = mxX;
    b.mxY = mxY;
    b.cx = st.drawing.cadPositionMarkers[mi].x;
    b.cy = st.drawing.cadPositionMarkers[mi].y;
    EntityBoxGrowZ(b, st.drawing.cadPositionMarkers[mi].z, true);
    out.push_back(b);
  }
  for (size_t li = 0; li < st.drawing.cadMultileaders.size(); ++li) {  // REQ-367: path + label
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadMultileaderAttrs, li))
      continue;
    const CadMultileader& ml = st.drawing.cadMultileaders[li];
    float mnX = 0.f, mnY = 0.f, mxX = 0.f, mxY = 0.f;
    CadMultileaderLocalBox(ml, &mnX, &mnY, &mxX, &mxY);
    EntityBox b{};
    b.mnX = mnX;
    b.mnY = mnY;
    b.mxX = mxX;
    b.mxY = mxY;
    if (ml.pathXyz.size() >= 3) {
      b.cx = ml.pathXyz[0];
      b.cy = ml.pathXyz[1];
      EntityBoxGrowZ(b, ml.pathXyz[2], true);
    }
    out.push_back(b);
  }
  for (size_t ai = 0; ai < st.drawing.cadAnnotations.size(); ++ai) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadAnnotationAttrs, ai))
      continue;
    const CadAnnotation& a = st.drawing.cadAnnotations[ai];
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(a, st.drawing.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    EntityBox b{};
    b.mnX = static_cast<double>(amnX);
    b.mxX = static_cast<double>(amxX);
    b.mnY = static_cast<double>(amnY);
    b.mxY = static_cast<double>(amxY);
    EntityBoxGrowZ(b, static_cast<double>(a.insZ), true);
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
  for (size_t ti = 0; ti < st.drawing.cadTables.size(); ++ti) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadTableAttrs, ti))
      continue;
    float tmnX = 0.f, tmnY = 0.f, tmxX = 0.f, tmxY = 0.f;
    CadTableWorldAabb(st.drawing.cadTables[ti], &tmnX, &tmnY, &tmxX, &tmxY);
    EntityBox b{};
    b.mnX = static_cast<double>(tmnX);
    b.mxX = static_cast<double>(tmxX);
    b.mnY = static_cast<double>(tmnY);
    b.mxY = static_cast<double>(tmxY);
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
  for (size_t arcIx = 0; arcIx < st.drawing.userArcs.size(); ++arcIx) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.userArcAttrs, arcIx))
      continue;
    const CadArc& a = st.drawing.userArcs[arcIx];
    const double dr = std::fabs(static_cast<double>(a.r));
    if (dr <= 1e-12)
      continue;
    EntityBox b{};
    b.mnX = static_cast<double>(a.cx) - dr;
    b.mxX = static_cast<double>(a.cx) + dr;
    b.mnY = static_cast<double>(a.cy) - dr;
    b.mxY = static_cast<double>(a.cy) + dr;
    b.cx = static_cast<double>(a.cx);
    b.cy = static_cast<double>(a.cy);
    const bool arcFlat = IsFlatNormal(a.nx, a.ny, a.nz);
    EntityBoxGrowZ(b, arcFlat ? a.z : a.z - dr, true);
    EntityBoxGrowZ(b, arcFlat ? a.z : a.z + dr, false);
    out.push_back(b);
  }
  for (size_t elIx = 0; elIx < st.drawing.userEllipses.size(); ++elIx) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.userEllAttrs, elIx))
      continue;
    const CadEllipse& el = st.drawing.userEllipses[elIx];
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-12)
      continue;
    const double mb = ma * static_cast<double>(el.ratio);
    const double rrx = std::hypot(ma, mb);
    EntityBox b{};
    b.mnX = static_cast<double>(el.cx) - rrx;
    b.mxX = static_cast<double>(el.cx) + rrx;
    b.mnY = static_cast<double>(el.cy) - rrx;
    b.mxY = static_cast<double>(el.cy) + rrx;
    b.cx = static_cast<double>(el.cx);
    b.cy = static_cast<double>(el.cy);
    const bool elFlat = IsFlatNormal(el.nx, el.ny, el.nz);
    EntityBoxGrowZ(b, elFlat ? el.z : el.z - rrx, true);
    EntityBoxGrowZ(b, elFlat ? el.z : el.z + rrx, false);
    out.push_back(b);
  }
  {  // REQ-443: a spline's box is its curve's, with its elevation range
    std::vector<ray3d::Vec3> sp;
    for (size_t si = 0; si < st.drawing.cadSplines.size(); ++si) {
      if (EntityHiddenInViewport(vpFilter, st.drawing.cadSplineAttrs, si))
        continue;
      spline::Tessellate(st.drawing.cadSplines[si], kSplineSampleTolerance, &sp);
      if (sp.empty())
        continue;
      EntityBox b{};
      for (size_t i = 0; i < sp.size(); ++i) {
        EntityBoxGrowZ(b, sp[i].z, i == 0);
        if (i == 0) {
          b.mnX = b.mxX = sp[i].x;
          b.mnY = b.mxY = sp[i].y;
        } else {
          b.mnX = std::min(b.mnX, sp[i].x);
          b.mxX = std::max(b.mxX, sp[i].x);
          b.mnY = std::min(b.mnY, sp[i].y);
          b.mxY = std::max(b.mxY, sp[i].y);
        }
      }
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }
  for (const alignment::Alignment& al : st.drawing.alignments) {  // REQ-480, with its labels (D-2026-11-04-c)
    alignment::Point2 mn{}, mx{};
    if (!AlignmentLabelSets::LabelledBounds(al, CadAlignmentLabelContext(st), &mn, &mx))
      continue;
    EntityBox b{};
    EntityBoxGrowZ(b, 0.0, true);
    b.mnX = mn.x;
    b.mxX = mx.x;
    b.mnY = mn.y;
    b.mxY = mx.y;
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
  for (size_t pi = 0; pi < st.drawing.cadPoints.size(); ++pi) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadPointAttrs, pi))
      continue;
    const CadPoint& p = st.drawing.cadPoints[pi];
    EntityBox b{};
    b.mnX = b.mxX = p.x;
    b.mnY = b.mxY = p.y;
    b.cx = p.x;
    b.cy = p.y;
    EntityBoxGrowZ(b, p.z, true);
    out.push_back(b);
  }
  const auto& PO = st.drawing.userPolylineOffsets;
  if (PO.size() >= 2) {
    for (size_t pi = 0; pi + 1 < PO.size(); ++pi) {
      if (EntityHiddenInViewport(vpFilter, st.drawing.userPolylineAttrs, pi))
        continue;
      const int v0 = PO[pi];
      const int v1 = PO[pi + 1];
      if (v1 <= v0)
        continue;
      EntityBox b{};
      bool any = false;
      for (int vi = v0; vi < v1; ++vi) {
        const CadPolyVertex pv = st.drawing.vertex(static_cast<size_t>(vi));
        const double vx = pv.x;
        const double vy = pv.y;
        EntityBoxGrowZ(b, pv.z, !any);
        if (!any) {
          b.mnX = b.mxX = vx;
          b.mnY = b.mxY = vy;
          any = true;
        } else {
          b.mnX = std::min(b.mnX, vx);
          b.mxX = std::max(b.mxX, vx);
          b.mnY = std::min(b.mnY, vy);
          b.mxY = std::max(b.mxY, vy);
        }
      }
      if (!any)
        continue;
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }

  // Feature lines (REQ-087) — same CSR walk. Without this, ZOOM EXTENTS frames a drawing as though
  // its feature lines were not there, which is the quiet kind of miss ADR-035 (g) warns about.
  const auto& FO = st.drawing.featureLineOffsets;
  const auto& FV = st.drawing.featureLineVerts;
  if (FO.size() >= 2) {
    for (size_t fi = 0; fi + 1 < FO.size(); ++fi) {
      if (EntityHiddenInViewport(vpFilter, st.drawing.featureLineAttrs, fi))
        continue;
      const int v0 = FO[fi];
      const int v1 = FO[fi + 1];
      if (v1 <= v0)
        continue;
      EntityBox b{};
      bool any = false;
      for (int vi = v0; vi < v1; ++vi) {
        if ((static_cast<size_t>(vi)* 3 + 1) >= FV.size())
          break;
        const double vx = static_cast<double>(FV[(static_cast<size_t>(vi)* 3 + 0)]);
        const double vy = static_cast<double>(FV[(static_cast<size_t>(vi)* 3 + 1)]);
        if ((static_cast<size_t>(vi)* 3 + 2) < FV.size())
          EntityBoxGrowZ(b, static_cast<double>(FV[(static_cast<size_t>(vi)* 3 + 2)]), !any);
        if (!any) {
          b.mnX = b.mxX = vx;
          b.mnY = b.mxY = vy;
          any = true;
        } else {
          b.mnX = std::min(b.mnX, vx);
          b.mxX = std::max(b.mxX, vx);
          b.mnY = std::min(b.mnY, vy);
          b.mxY = std::max(b.mxY, vy);
        }
      }
      if (!any)
        continue;
      b.cx = 0.5 * (b.mnX + b.mxX);
      b.cy = 0.5 * (b.mnY + b.mxY);
      out.push_back(b);
    }
  }

  // Imported meshes (REQ-063: "meshes are included in zoom-extents and the drawing's bounding
  // box"). One box per mesh rather than per triangle — the extents pass is an outlier-trimmed
  // statistic over ENTITIES, and feeding it two million triangles would both swamp that statistic
  // and make ZE cost a full mesh walk per invocation.
  for (size_t mi = 0; mi < st.drawing.cadMeshes.size(); ++mi) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadMeshAttrs, mi))
      continue;
    const auto& mp = st.drawing.cadMeshes[mi];
    if (!mp)
      continue;
    const meshgeom::Bounds mb = meshgeom::ComputeBounds(mp->vertsXyz);
    if (!mb.valid)
      continue;
    EntityBox b{};
    b.mnX = static_cast<double>(mb.mnX);
    b.mxX = static_cast<double>(mb.mxX);
    b.mnY = static_cast<double>(mb.mnY);
    b.mxY = static_cast<double>(mb.mxY);
    b.mnZ = static_cast<double>(mb.mnZ);
    b.mxZ = static_cast<double>(mb.mxZ);
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }

  // Point clouds (REQ-171), one box per cloud — over the resident points (ADR-060: the full cloud
  // may not be loaded), for the same "one box per entity, not per vertex" reason meshes get above.
  for (size_t pci = 0; pci < st.drawing.cadPointClouds.size(); ++pci) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadPointCloudAttrs, pci))
      continue;
    const auto& pc = st.drawing.cadPointClouds[pci];
    if (!pc || pc->pointsXyz.size() < 3)
      continue;
    EntityBox b{};
    b.mnX = b.mxX = pc->pointsXyz[0];
    b.mnY = b.mxY = pc->pointsXyz[1];
    EntityBoxGrowZ(b, pc->pointsXyz[2], true);
    for (size_t i = 3; i + 2 < pc->pointsXyz.size(); i += 3) {
      b.mnX = std::min(b.mnX, pc->pointsXyz[i]);
      b.mxX = std::max(b.mxX, pc->pointsXyz[i]);
      b.mnY = std::min(b.mnY, pc->pointsXyz[i + 1]);
      b.mxY = std::max(b.mxY, pc->pointsXyz[i + 1]);
      EntityBoxGrowZ(b, pc->pointsXyz[i + 2], false);
    }
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }

  // B-rep solids (REQ-313), one box per solid — from the analytic bounds, for the reason the
  // zoom-extents walk gives: a sphere's two stored vertices describe almost none of it.
  for (size_t soi = 0; soi < st.drawing.cadSolids.size(); ++soi) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadSolidAttrs, soi))
      continue;
    const CadSolidPtr& sp = st.drawing.cadSolids[soi];
    if (!sp)
      continue;
    const brep::Bounds bb = brep::ComputeBounds(*sp);
    if (!bb.valid)
      continue;
    EntityBox b{};
    b.mnX = bb.mn.x;
    b.mxX = bb.mx.x;
    b.mnY = bb.mn.y;
    b.mxY = bb.mx.y;
    b.mnZ = bb.mn.z;
    b.mxZ = bb.mx.z;
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }

  // TIN surfaces (REQ-068), one box per surface — same reasoning as the meshes above.
  for (size_t si = 0; si < st.drawing.cadSurfaces.size(); ++si) {
    if (EntityHiddenInViewport(vpFilter, st.drawing.cadSurfaceAttrs, si))
      continue;
    const CadSurface& s = st.drawing.cadSurfaces[si];
    if (!s.tin)
      continue;
    const TinPlanBounds sb = ComputeTinPlanBounds(s.tin->vertsXyz);
    if (!sb.valid)
      continue;
    EntityBox b{};
    b.mnX = sb.mnX;
    b.mxX = sb.mxX;
    b.mnY = sb.mnY;
    b.mxY = sb.mxY;
    const std::vector<double>& tv = s.tin->vertsXyz;
    for (size_t i = 2; i < tv.size(); i += 3)
      EntityBoxGrowZ(b, tv[i], i == 2);
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
}

/// The boxes only a 3D extent needs (GitHub issue #564 §1, D-2026-09-28-d): filled regions, block
/// references (their 2D content at the insertion's elevation, plus any solids they carry, in full
/// 3D) and pipe runs (the centreline widened by the pipe's radius). Kept apart from
/// \ref CollectEntityBoxes so the plan-view extents — which never counted these in their robust
/// pass — are unchanged by adding them here.
void CollectEntityBoxes3dExtras(const AppCommandState& st, std::vector<EntityBox>& out) {
  for (size_t fri = 0; fri < st.drawing.cadFilledRegions.size(); ++fri) {
    if (EntityHiddenInViewport(nullptr, st.drawing.cadFilledRegionAttrs, fri))
      continue;
    const CadFilledRegion& fr = st.drawing.cadFilledRegions[fri];
    if (fr.vertsXyz.size() < 3)
      continue;
    EntityBox b{};
    for (size_t i = 0; i + 2 < fr.vertsXyz.size(); i += 3) {
      const double x = fr.vertsXyz[i];
      const double y = fr.vertsXyz[i + 1];
      b.mnX = i == 0 ? x : std::min(b.mnX, x);
      b.mxX = i == 0 ? x : std::max(b.mxX, x);
      b.mnY = i == 0 ? y : std::min(b.mnY, y);
      b.mxY = i == 0 ? y : std::max(b.mxY, y);
      EntityBoxGrowZ(b, fr.vertsXyz[i + 2], i == 0);
    }
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
  for (size_t bi = 0; bi < st.drawing.cadBlockRefs.size(); ++bi) {
    if (EntityHiddenInViewport(nullptr, st.drawing.cadBlockRefAttrs, bi))
      continue;
    float bmnX = 0.f, bmnY = 0.f, bmxX = 0.f, bmxY = 0.f;
    CadBlockWorldAabb(st.drawing.blockDefs, st.drawing.cadBlockRefs[bi], &bmnX, &bmnY, &bmxX, &bmxY);
    EntityBox b{};
    b.mnX = bmnX;
    b.mxX = bmxX;
    b.mnY = bmnY;
    b.mxY = bmxY;
    EntityBoxGrowZ(b, st.drawing.cadBlockRefs[bi].xf.z, true);
    std::vector<CadBlockWorldSolid> ws;
    CadBlockCollectWorldSolids(st.drawing.blockDefs, st.drawing.cadBlockRefs[bi],
                               bi < st.drawing.cadBlockRefAttrs.size() ? st.drawing.cadBlockRefAttrs[bi] : EntityAttributes{},
                               &ws);
    for (const CadBlockWorldSolid& w : ws) {
      if (!w.solid)
        continue;
      const brep::Bounds sb = brep::ComputeBounds(*w.solid);
      if (!sb.valid)
        continue;
      b.mnX = std::min(b.mnX, sb.mn.x);
      b.mxX = std::max(b.mxX, sb.mx.x);
      b.mnY = std::min(b.mnY, sb.mn.y);
      b.mxY = std::max(b.mxY, sb.mx.y);
      EntityBoxGrowZ(b, sb.mn.z, false);
      EntityBoxGrowZ(b, sb.mx.z, false);
    }
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
  for (const CadPipeRun& r : st.drawing.cadPipeRuns) {
    if (r.vertsXyz.size() < 3)
      continue;
    double odFeet = 0.0;
    const double rad = CadPipeNominalOdFeet(r.nominalSize, &odFeet) ? 0.5 * odFeet : 0.0;
    EntityBox b{};
    for (size_t i = 0; i + 2 < r.vertsXyz.size(); i += 3) {
      const double x = r.vertsXyz[i];
      const double y = r.vertsXyz[i + 1];
      b.mnX = i == 0 ? x - rad : std::min(b.mnX, x - rad);
      b.mxX = i == 0 ? x + rad : std::max(b.mxX, x + rad);
      b.mnY = i == 0 ? y - rad : std::min(b.mnY, y - rad);
      b.mxY = i == 0 ? y + rad : std::max(b.mxY, y + rad);
      EntityBoxGrowZ(b, r.vertsXyz[i + 2] - rad, i == 0);
      EntityBoxGrowZ(b, r.vertsXyz[i + 2] + rad, false);
    }
    b.cx = 0.5 * (b.mnX + b.mxX);
    b.cy = 0.5 * (b.mnY + b.mxY);
    out.push_back(b);
  }
}

} // namespace

bool ComputeRobustWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY,
                               double* outMxY, int* outSkipped, const Viewport* vpFilter) {
  if (outSkipped)
    *outSkipped = 0;
  std::vector<EntityBox> ents;
  ents.reserve(st.drawing.lineCount() + st.drawing.circleCount() + st.drawing.userArcs.size() +
               st.drawing.userEllipses.size() + st.drawing.cadSplines.size() + st.drawing.cadPoints.size() + st.drawing.cadAnnotations.size() +
               st.drawing.surveyPoints.size() +
               (st.drawing.userPolylineOffsets.empty() ? 0 : st.drawing.userPolylineOffsets.size() - 1));
  CollectEntityBoxes(st, ents, vpFilter);

  if (ents.size() < 16)
    return ComputeWorldExtents(st, outMnX, outMxX, outMnY, outMxY, vpFilter);

  std::vector<double> xs;
  std::vector<double> ys;
  xs.reserve(ents.size());
  ys.reserve(ents.size());
  for (const EntityBox& b : ents) {
    xs.push_back(b.cx);
    ys.push_back(b.cy);
  }

  std::vector<double> xsCopy = xs;
  std::vector<double> ysCopy = ys;
  const double xP05 = NthPercentile(xsCopy, 0.05);
  xsCopy = xs;
  const double xP95 = NthPercentile(xsCopy, 0.95);
  const double yP05 = NthPercentile(ysCopy, 0.05);
  ysCopy = ys;
  const double yP95 = NthPercentile(ysCopy, 0.95);

  const double bulkSpanX = std::max(xP95 - xP05, 0.);
  const double bulkSpanY = std::max(yP95 - yP05, 0.);
  const double midX = 0.5 * (xP05 + xP95);
  const double midY = 0.5 * (yP05 + yP95);

  // Outlier window: entities whose center is within ±5× the bulk span from the bulk midpoint are kept.
  // A 5× pad is generous enough to retain legitimate sparse content while still rejecting (0,0)-anchored
  // strays in large DXFs.
  const double radX = std::max(bulkSpanX * 5.0, 1.0);
  const double radY = std::max(bulkSpanY * 5.0, 1.0);

  bool any = false;
  double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
  int skipped = 0;
  for (const EntityBox& b : ents) {
    if (std::fabs(b.cx - midX) > radX || std::fabs(b.cy - midY) > radY) {
      ++skipped;
      continue;
    }
    if (!any) {
      mnX = b.mnX;
      mxX = b.mxX;
      mnY = b.mnY;
      mxY = b.mxY;
      any = true;
    } else {
      mnX = std::min(mnX, b.mnX);
      mxX = std::max(mxX, b.mxX);
      mnY = std::min(mnY, b.mnY);
      mxY = std::max(mxY, b.mxY);
    }
  }

  if (!any)
    return ComputeWorldExtents(st, outMnX, outMxX, outMnY, outMxY, vpFilter);

  *outMnX = mnX;
  *outMxX = mxX;
  *outMnY = mnY;
  *outMxY = mxY;
  if (outSkipped)
    *outSkipped = skipped;
  return true;
}

bool ComputeWorldExtents3d(const AppCommandState& st, ray3d::Vec3* outMin, ray3d::Vec3* outMax,
                           int* outSkipped) {
  if (outSkipped)
    *outSkipped = 0;
  if (!outMin || !outMax)
    return false;
  std::vector<EntityBox> ents;
  CollectEntityBoxes(st, ents, nullptr);
  CollectEntityBoxes3dExtras(st, ents);
  if (ents.empty())
    return false;

  // The same outlier rule as ComputeRobustWorldExtents, on the same statistic (entity centres in
  // plan), so a stray entity at (0,0) is dropped from an orbited frame exactly as from a plan one.
  // Below 16 entities there is no bulk to measure an outlier against, and every box counts.
  double midX = 0.;
  double midY = 0.;
  double radX = std::numeric_limits<double>::infinity();
  double radY = std::numeric_limits<double>::infinity();
  if (ents.size() >= 16) {
    std::vector<double> xs;
    std::vector<double> ys;
    xs.reserve(ents.size());
    ys.reserve(ents.size());
    for (const EntityBox& b : ents) {
      xs.push_back(b.cx);
      ys.push_back(b.cy);
    }
    std::vector<double> c = xs;
    const double xP05 = NthPercentile(c, 0.05);
    c = xs;
    const double xP95 = NthPercentile(c, 0.95);
    c = ys;
    const double yP05 = NthPercentile(c, 0.05);
    c = ys;
    const double yP95 = NthPercentile(c, 0.95);
    midX = 0.5 * (xP05 + xP95);
    midY = 0.5 * (yP05 + yP95);
    radX = std::max(std::max(xP95 - xP05, 0.) * 5.0, 1.0);
    radY = std::max(std::max(yP95 - yP05, 0.) * 5.0, 1.0);
  }

  bool any = false;
  int skipped = 0;
  ray3d::Vec3 mn{};
  ray3d::Vec3 mx{};
  for (const EntityBox& b : ents) {
    if (std::fabs(b.cx - midX) > radX || std::fabs(b.cy - midY) > radY) {
      ++skipped;
      continue;
    }
    if (!any) {
      mn = ray3d::Vec3{b.mnX, b.mnY, b.mnZ};
      mx = ray3d::Vec3{b.mxX, b.mxY, b.mxZ};
      any = true;
      continue;
    }
    mn = ray3d::Vec3{std::min(mn.x, b.mnX), std::min(mn.y, b.mnY), std::min(mn.z, b.mnZ)};
    mx = ray3d::Vec3{std::max(mx.x, b.mxX), std::max(mx.y, b.mxY), std::max(mx.z, b.mxZ)};
  }
  if (!any)
    return false;
  *outMin = mn;
  *outMax = mx;
  if (outSkipped)
    *outSkipped = skipped;
  return true;
}


/// reject (REQ-201). Accepts the AutoCAD-ish spellings a user is likely to try.
bool ApplyVisualStyleValue(AppCommandState& st, const std::string& raw, std::vector<std::string>& log) {
  VisualStyle s = st.viewportVisualStyle;
  if (!VisualStyleFromName(StringUtil::trimCopy(raw), &s)) {
    log.push_back("VISUALSTYLE — enter 2D, HIDDEN or SHADED.");
    return false;
  }
  st.viewportVisualStyle = s;
  log.push_back(std::string("Visual style = ") + VisualStyleName(s) + ".");
  return true;
}

/// Shared by the prompt and the inline `CROSSHAIR3D ON` form (REQ-310), like the two above.
///
/// Reuses \ref ProjectionFromName's ON/OFF spellings via its own small table rather than borrowing
/// that function: the two settings are unrelated, and a shared parser would make `CROSSHAIR3D
/// PERSPECTIVE` legal.
bool ApplyCrosshair3dValue(AppCommandState& st, const std::string& raw, std::vector<std::string>& log) {
  std::string v;
  for (char c : StringUtil::trimCopy(raw)) {
    if (c == ' ' || c == '\t')
      continue;
    v.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c));
  }
  if (v == "on" || v == "1" || v == "3d" || v == "yes")
    st.viewportCrosshair3d = true;
  else if (v == "off" || v == "0" || v == "2d" || v == "no")
    st.viewportCrosshair3d = false;
  else {
    log.push_back("CROSSHAIR3D - enter ON or OFF.");
    return false;
  }
  log.push_back(st.viewportCrosshair3d
                    ? "3D crosshair = ON - the cursor shows the UCS X/Y/Z axes."
                    : "3D crosshair = OFF - the cursor is the standard two-arm crosshair.");
  return true;
}

/// Canonical name of a projection, for the command line, the ribbon and `internal document payload` (REQ-309).
const char* ProjectionName(Camera::Projection p) {
  return p == Camera::Projection::Perspective ? "Perspective" : "Orthographic";
}

/// Parse a user-typed projection name. Case-insensitive and tolerant of the spellings someone
/// actually types, mirroring \ref VisualStyleFromName — including the raw ordinals, and the two
/// senses of the bare command (`PERSPECTIVE ON`).
bool ProjectionFromName(const std::string& raw, Camera::Projection* out) {
  if (!out)
    return false;
  std::string v;
  v.reserve(raw.size());
  for (char c : raw) {
    if (c == ' ' || c == '\t')
      continue;
    v.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c));
  }
  if (v == "o" || v == "ortho" || v == "orthographic" || v == "parallel" || v == "off" || v == "0")
    *out = Camera::Projection::Orthographic;
  else if (v == "p" || v == "persp" || v == "perspective" || v == "on" || v == "1")
    *out = Camera::Projection::Perspective;
  else
    return false;
  return true;
}

/// Shared by the prompt and the inline `PERSPECTIVE ON` form so neither can set a value the other
/// would reject (REQ-201) — the same reason \ref ApplyVisualStyleValue exists.
///
/// Switching projection touches no stored coordinate: it changes only how the drawing is looked
/// at, the rule REQ-154 states for the UCS and REQ-309 restates here.
bool ApplyProjectionValue(AppCommandState& st, const std::string& raw, std::vector<std::string>& log) {
  Camera::Projection p = st.viewportProjection;
  if (!ProjectionFromName(StringUtil::trimCopy(raw), &p)) {
    log.push_back("PERSPECTIVE - enter ON (perspective) or OFF (orthographic).");
    return false;
  }
  st.viewportProjection = p;
  log.push_back(std::string("Projection = ") + ProjectionName(p) + ".");
  return true;
}

/// Set the perspective field of view (REQ-309).
///
/// Refuses anything outside [kMinFovDeg, kMaxFovDeg] rather than clamping, because a clamp would
/// silently accept a typo: `FOV 400` is not a request for 179 degrees, it is a mistake, and
/// REQ-201 wants it refused with the previous value intact. Non-finite input is caught by the same
/// bound test, which is why it is written as a positive range check.
bool ApplyFovValue(AppCommandState& st, const std::string& raw, std::vector<std::string>& log) {
  float v = 0.f;
  if (!ParseOneFloat(raw, &v) || !std::isfinite(v) || !(v >= kMinFovDeg && v <= kMaxFovDeg)) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "FOV - enter an angle between %.0f and %.0f degrees.",
                  static_cast<double>(kMinFovDeg), static_cast<double>(kMaxFovDeg));
    log.push_back(buf);
    return false;
  }
  st.viewportFovDeg = v;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "FOV = %.4g degrees.", static_cast<double>(st.viewportFovDeg));
  log.push_back(buf);
  return true;
}


void StartZoomExtentsCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("ZOOM EXTENTS — finish or cancel the active command first.");
    return;
  }
  RequestZoomExtents(st);
}

void StartZoomWindowCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None && st.active != K::Zoom) {
    log.push_back("ZOOM WINDOW — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  stepengine::BeginSession(st, std::make_unique<stepengine::ZoomSession>(), /*recordAsLastCommand=*/false);
  log.push_back(
      "ZOOM WINDOW — click two corners (crossing window corners use cursor position, not object snap). ESC "
      "cancels.");
}

void StartPanCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  // PAN coexists with any view state but is its own command; cancel an in-progress draw/edit first.
  if (st.active != K::None && st.active != K::Pan) {
    log.push_back("PAN — finish or cancel the active command first.");
    return;
  }
  stepengine::BeginSession(st, std::make_unique<stepengine::PanSession>(), /*recordAsLastCommand=*/true);
  log.push_back("PAN — drag with the left mouse button. Press Esc, Enter, or right-click to exit.");
}

void StartOrbitCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None && st.active != K::Orbit) {
    log.push_back("ORBIT — finish or cancel the active command first.");
    return;
  }
  // Paper space is 2D by definition (ADR-025 (g)) — a sheet has no orientation to tumble, and
  // letting it tilt would put the plot geometry somewhere the plot cannot follow.
  if (st.activeSpaceIndex != kModelSpaceIndex) {
    log.push_back("ORBIT — model space only; a paper sheet is 2D.");
    return;
  }
  stepengine::BeginSession(st, std::make_unique<stepengine::OrbitSession>(), /*recordAsLastCommand=*/true);
  log.push_back("ORBIT — drag with the left mouse button to tumble. Press Esc, Enter, or right-click to exit.");
}


void ProcessPendingViewportZoom(AppCommandState& st, double* panX, double* panY, float* zoom, int fbW, int fbH,
                                float viewportAspect, std::vector<std::string>& log) {
  // REQ-123 (GitHub #100): ZOOM EXTENTS through an ACTIVATED VIEWPORT frames the model into that
  // viewport's own rectangle, not onto the screen camera.
  //
  // Handled FIRST, and deliberately above the framebuffer guard below, because this case needs no
  // framebuffer: the viewport's aspect is its rect on the SHEET (`paperWIn / paperHIn`), and its
  // framing is `modelCenter` + `scaleModelPerPaperIn`. Nothing here reads a pixel. That is not a
  // micro-optimisation — it is what makes this the one zoom behaviour a transcript can drive
  // end to end (TASK-113 DEBT-1 blocks the others precisely on `fbW <= 0`).
  //
  // The zoom LOCK is honoured, and honouring it is what keeps the feature coherent: the lock's
  // stated meaning is "pan/zoom always targets the sheet", and zoom-extents is a zoom. With the
  // lock ON this falls through and frames the sheet, exactly as paper space does.
  if (st.pendingZoomExtents && InFloatingModelSpace(st) && !st.viewportZoomLocked) {
    st.pendingZoomExtents = false;
    Viewport* vp = nullptr;
    if (st.floatingViewportLayout >= 0 &&
        static_cast<size_t>(st.floatingViewportLayout) < st.drawing.paperLayouts.size()) {
      PaperLayout& L = st.drawing.paperLayouts[static_cast<size_t>(st.floatingViewportLayout)];
      if (st.floatingViewportIndex >= 0 && static_cast<size_t>(st.floatingViewportIndex) < L.viewports.size())
        vp = &L.viewports[static_cast<size_t>(st.floatingViewportIndex)];
    }
    if (!vp) {
      log.push_back("ZOOM EXTENTS — no active viewport to frame into.");
      return;
    }
    if (vp->locked) {  // REQ-518.11: a locked viewport keeps its model centre and scale
      log.push_back("ZOOM EXTENTS — the viewport is locked; its view is unchanged.");
      return;
    }
    double mnX = 0.;
    double mxX = 0.;
    double mnY = 0.;
    double mxY = 0.;
    int skipped = 0;
    // The extents are the model's, seen THROUGH this viewport: layers frozen in it are not part of
    // what the user is looking at, so they must not drag the framing out to reach them. Same test
    // the viewport renderer and the plotter apply (D-2026-08-26-e).
    if (!ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped, vp)) {
      log.push_back("ZOOM EXTENTS — nothing to frame in this viewport.");
      return;
    }
    // ComputeRobustWorldExtents (despite the name) returns LOCAL coordinates — the same convention
    // every raw entity store uses. vp->modelCenterX/Y is WORLD (local + worldDocumentOrigin): see
    // AddViewportRect's default (`= cmd.drawing.worldDocumentOriginX`) and the screen<->model conversions in
    // CadUi.cpp/PdfPlot.cpp, which both add/subtract the origin around this same field. Framing in
    // LOCAL and writing straight into a WORLD field left the viewport pointed at the wrong place by
    // exactly worldDocumentOriginX/Y whenever it's nonzero (i.e. after any import that rebases the
    // drawing) — invisible with a fresh drawing at origin (0,0), reproducible after a DXF import.
    if (!zoomframing::FrameWorldRectInViewport(mnX, mxX, mnY, mxY, vp->paperWIn, vp->paperHIn, &vp->modelCenterX,
                                               &vp->modelCenterY, &vp->scaleModelPerPaperIn)) {
      log.push_back("ZOOM EXTENTS — the drawing extents are not a finite rectangle; view unchanged.");
      return;
    }
    vp->modelCenterX += st.drawing.worldDocumentOriginX;
    vp->modelCenterY += st.drawing.worldDocumentOriginY;
    BumpCadGpuCache(st);
    char vbuf[256];
    std::snprintf(vbuf, sizeof(vbuf),
                  "Zoom extents applied in viewport — span %.6g x %.6g, scale %.6g model units/in, "
                  "centre %.6g, %.6g skipped=%d.",
                  mxX - mnX, mxY - mnY, static_cast<double>(vp->scaleModelPerPaperIn), vp->modelCenterX,
                  vp->modelCenterY, skipped);
    log.push_back(vbuf);
    return;
  }

  if (fbW <= 0 || fbH <= 0)
    return;
  if (st.pendingZoomExtents) {
    double mnX = 0.;
    double mxX = 0.;
    double mnY = 0.;
    double mxY = 0.;
    int skipped = 0;
    // REQ-120: what "extents" means depends on the space, mirroring where middle-drag pan already
    // works (REQ-045). Reaching here means the view being navigated is the SHEET's — either paper
    // space proper, or a floating viewport with the zoom lock ON, which says pan/zoom targets the
    // sheet. The floating-and-unlocked case returned above (REQ-123); it is the only one whose
    // camera is the viewport's rather than the window's.
    //
    // REQ-120 originally excluded floating model space from this branch and framed the MODEL extents
    // into the SHEET camera instead — which is the defect issue #100 reports. Corrected here.
    const PaperLayout* sheet = nullptr;
    if (st.activeSpaceIndex >= 0 && static_cast<size_t>(st.activeSpaceIndex) < st.drawing.paperLayouts.size())
      sheet = &st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];

    if (sheet) {
      // The PAGE is the meaningful extent of a layout, and a layout with no geometry on it yet must
      // still frame to something. Paper geometry drawn OUTSIDE the sheet is deliberately not framed
      // (REQ-120, stated limitation) — framing it would need a second extents sweep over the seven
      // paper stores, which this requirement declined to add.
      mnX = 0.;
      mnY = 0.;
      mxX = static_cast<double>(sheet->sheetWidthIn());
      mxY = static_cast<double>(sheet->sheetHeightIn());
      if (!(mxX > mnX) || !(mxY > mnY)) {
        st.pendingZoomExtents = false;
        log.push_back("ZOOM EXTENTS — nothing to frame.");
        return;
      }
    } else if (!CadViewIsPlan(st) || std::fabs(st.viewportRollDeg) > 1e-4f) {
      // ORBITED (GitHub issue #564 §1, D-2026-09-28-d): frame the model's true 3D box by its
      // silhouette in THIS camera. Plan view never reaches here, so its framing is exactly the
      // pre-change path below; a rolled plan (a tilted-UCS PLAN, #153) turns the screen rectangle
      // and so is framed here too.
      st.pendingZoomExtents = false;
      ray3d::Vec3 bmn{};
      ray3d::Vec3 bmx{};
      if (!ComputeWorldExtents3d(st, &bmn, &bmx, &skipped)) {
        log.push_back("ZOOM EXTENTS — nothing to frame.");
        return;
      }
      float rot[16];
      CadViewCamera(st).ViewRotation(rot);
      const double right[3] = {rot[0], rot[4], rot[8]};
      const double up[3] = {rot[1], rot[5], rot[9]};
      const double back[3] = {rot[2], rot[6], rot[10]};
      const double mn3[3] = {bmn.x, bmn.y, bmn.z};
      const double mx3[3] = {bmx.x, bmx.y, bmx.z};
      double target[3] = {0., 0., 0.};
      float newZoom = st.viewportZoom;
      if (!zoomframing::FrameBoxInView(mn3, mx3, right, up, back, viewportAspect,
                                       st.viewportProjection == Camera::Projection::Perspective,
                                       st.viewportFovDeg, target, &newZoom)) {
        log.push_back("ZOOM EXTENTS — the drawing extents are not a finite box; view unchanged.");
        return;
      }
      st.viewportPanX = target[0];
      st.viewportPanY = target[1];
      st.viewportPanZ = target[2];
      st.viewportZoom = newZoom;
      BumpCadGpuCache(st);
      if (panX)
        *panX = st.viewportPanX;
      if (panY)
        *panY = st.viewportPanY;
      if (zoom)
        *zoom = st.viewportZoom;
      char buf3[256];
      std::snprintf(buf3, sizeof(buf3),
                    "Zoom extents applied (3D) — box %.6g x %.6g x %.6g, centre (%.6g, %.6g, %.6g) "
                    "zoom=%.6g skipped=%d.",
                    bmx.x - bmn.x, bmx.y - bmn.y, bmx.z - bmn.z, target[0], target[1], target[2],
                    static_cast<double>(st.viewportZoom), skipped);
      log.push_back(buf3);
      return;
    } else if (!ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped)) {
      // Plan view, and nothing in the 2D sweep: a drawing of pipe runs alone (or only what the
      // 3D sweep adds) still has something to frame — its plan footprint (issue #564 §1). A drawing
      // the 2D sweep DOES see never reaches here, so its framing is unchanged.
      ray3d::Vec3 bmn{};
      ray3d::Vec3 bmx{};
      if (!ComputeWorldExtents3d(st, &bmn, &bmx, &skipped)) {
        st.pendingZoomExtents = false;
        log.push_back("ZOOM EXTENTS — nothing to frame.");
        return;
      }
      mnX = bmn.x;
      mxX = bmx.x;
      mnY = bmn.y;
      mxY = bmx.y;
    }
    // REQ-122: framing REFUSES a rect that is not finite rather than writing a NaN camera, and a
    // refusal states its reason (REQ-201). The current view is left exactly as it was.
    if (!zoomframing::FrameWorldRect(mnX, mxX, mnY, mxY, viewportAspect, &st.viewportPanX, &st.viewportPanY,
                                     &st.viewportZoom)) {
      st.pendingZoomExtents = false;
      log.push_back("ZOOM EXTENTS — the drawing extents are not a finite rectangle; view unchanged.");
      return;
    }
    BumpCadGpuCache(st);
    if (panX)
      *panX = st.viewportPanX;
    if (panY)
      *panY = st.viewportPanY;
    if (zoom)
      *zoom = st.viewportZoom;
    st.pendingZoomExtents = false;
    char buf[256];
    const double spanX = mxX - mnX;
    const double spanY = mxY - mnY;
    std::snprintf(buf, sizeof(buf),
                  "Zoom extents applied — span %.6g x %.6g (local %.6g..%.6g, %.6g..%.6g) zoom=%.6g skipped=%d.",
                  spanX, spanY, mnX, mxX, mnY, mxY, static_cast<double>(st.viewportZoom), skipped);
    log.push_back(buf);
    return;
  }
  if (st.pendingZoomWindow) {
    st.pendingZoomWindow = false;
    if (!zoomframing::FrameWorldRect(static_cast<double>(st.pendingZoomMnX), static_cast<double>(st.pendingZoomMxX),
                                     static_cast<double>(st.pendingZoomMnY), static_cast<double>(st.pendingZoomMxY),
                                     viewportAspect, &st.viewportPanX, &st.viewportPanY, &st.viewportZoom)) {
      log.push_back("ZOOM WINDOW — that window is not a finite rectangle; view unchanged.");
      return;
    }
    if (panX)
      *panX = st.viewportPanX;
    if (panY)
      *panY = st.viewportPanY;
    if (zoom)
      *zoom = st.viewportZoom;
    BumpCadGpuCache(st);
    log.push_back("Zoom window applied.");
  }
}
