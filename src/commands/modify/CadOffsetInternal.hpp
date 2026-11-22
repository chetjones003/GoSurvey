#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace OffsetCmd {

static void ResetOffsetDraft(AppCommandState& st) {
  st.offsetEntityValid = false;
  st.offsetEntity = {};
  st.offsetTypedDistance = 0.f;
  st.offsetThroughMode = false;
  st.offsetPhase = AppCommandState::OffsetPhase::WaitDistanceOrThrough;
  st.offsetHoverHighlightValid = false;
  st.offsetHoverEntity = {};
  st.offsetPaperLayoutIndex = -1;
}

/// Loop-back reset used between offsets within one OFFSET invocation (REQ-103): clears only the
/// picked entity, keeping the typed distance / through-mode so the next pick reuses it, matching
/// TRIM's own per-target loop.
static void ResetOffsetSelectionForNextPick(AppCommandState& st) {
  st.offsetEntityValid = false;
  st.offsetEntity = {};
  st.offsetPhase = AppCommandState::OffsetPhase::WaitSelectEntity;
  st.offsetHoverHighlightValid = false;
  st.offsetHoverEntity = {};
}

// Every function below (except ClosestPointOnSegment, kept `float` — it is also reused by BREAK
// via `using OffsetCmd::ClosestPointOnSegment` at this file's BREAK section, so widening it would
// ripple into an unrelated command) works in `double`, matching the `double` storage of
// userLinesFlat / userCirclesCxCyZR / userPolylineVerts / CadArc::cx,cy,r / CadEllipse::cx,cy
// (REQ-101). Reading those into `float` locals here used to narrow to ~float32 precision (~0.1-0.2
// units at state-plane / UTM magnitudes of 1e6+) BEFORE doing the perpendicular-offset math, so
// every repeated OFFSET rebuilt its normal from an already-truncated previous line and the error
// compounded in a consistent direction across a chain of offsets (issue: OFFSET distance drift /
// skew on non-axis-aligned geometry at large coordinate magnitude). The typed offset distance
// itself stays `float` — it is always a small number, so it loses no meaningful precision being
// added to a `double` base coordinate.
static void ClosestPointOnSegment(float ax, float ay, float bx, float by, float px, float py, float* qx,
                                  float* qy) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *qx = ax;
    *qy = ay;
    return;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  *qx = ax + t * vx;
  *qy = ay + t * vy;
}

static bool LineLineIntersectInf(double ax, double ay, double bx, double by, double cx, double cy, double dx,
                                 double dy, double* ox, double* oy) {
  const double rx = bx - ax, ry = by - ay;
  const double sx = dx - cx, sy = dy - cy;
  const double det = rx * sy - ry * sx;
  if (std::fabs(det) < 1e-12 * std::max(1.0, std::hypot(rx, ry) * std::hypot(sx, sy)))
    return false;
  const double t = ((cx - ax) * sy - (cy - ay) * sx) / det;
  *ox = ax + t * rx;
  *oy = ay + t * ry;
  return true;
}

/// REQ-329 increment 7: project a point into the active UCS plane's local 2D frame, so an OFFSET
/// side / through pick on a tilted work plane is decided in that plane rather than in world XY.
static ucs::Point2D OffsetPlaneLocal(const AppCommandState& st, double x, double y, double z) {
  return ucs::WorldToPlane(CadActiveUcsStorage(st), {x, y, z});
}

static void UnitLeftNormal(double ax, double ay, double bx, double by, double* nx, double* ny) {
  double vx = bx - ax;
  double vy = by - ay;
  const double len = std::hypot(vx, vy);
  if (len < 1e-12) {
    *nx = 0.0;
    *ny = 1.0;
    return;
  }
  vx /= len;
  vy /= len;
  *nx = -vy;
  *ny = vx;
}

// The signed perpendicular distance from (px,py) to the infinite line through (ax,ay)-(bx,by).
// Deliberately does NOT clamp to a closest point on the segment first (unlike the shared
// ClosestPointOnSegment): the normal component of (p - q) is the same for every q on the line, so
// projecting straight off (ax,ay) gives the identical answer with one less double round-trip.
static double SignedSideLine(double ax, double ay, double bx, double by, double px, double py) {
  double nx = 0.0, ny = 0.0;
  UnitLeftNormal(ax, ay, bx, by, &nx, &ny);
  return (px - ax) * nx + (py - ay) * ny;
}

static double SignedSideCircle(double cx, double cy, double r, double px, double py) {
  const double d = std::hypot(px - cx, py - cy);
  return d - r;
}

/// Append the attribute row for an OFFSET copy of the entity at \p srcIndex within \p attrs.
///
/// The copy **inherits** layer, colour, linetype, lineweight and transparency from its source —
/// that is what makes it read as an offset of that entity rather than a stranger on the current
/// layer. It must **not** inherit the source's id.
///
/// Ids are unique and never reused within a drawing (REQ-076), and architecture §11.9 builds on
/// that: a reference from one object to another *is* an id, so an id naming two entities makes
/// every such reference ambiguous. \ref EnsureEntityIds only fills ids that are 0, so a copied
/// non-zero id is never repaired — it persists to `internal document payload` and is permanent. Clearing it here hands
/// assignment back to the sweep, exactly as the clipboard does via \ref ClearEntityIdsFrom
/// (see CopySelectionToClipboard). Every caller bumps the GPU revision the sweep is gated on, so
/// the fresh id is assigned before anything can observe or save the entity.
///
/// One helper rather than the same three lines in five places, deliberately: the defect this fixes
/// (issue #58) was five copies of a correct-looking pattern that were all missing the same step.
static void PushOffsetCopyAttrs(AppCommandState& st, std::vector<EntityAttributes>& attrs,
                                int srcIndex) {
  if (srcIndex >= 0 && static_cast<size_t>(srcIndex) < attrs.size()) {
    attrs.push_back(attrs[static_cast<size_t>(srcIndex)]);
    attrs.back().id = 0;  // REQ-076: assigned fresh by EnsureEntityIds, never inherited
  } else {
    attrs.push_back(MakeNewEntityAttrs(st));  // already id 0
  }
}

static bool CommitOffsetLine(AppCommandState& st, int lineIx, float signedD, std::vector<std::string>& log) {
  const size_t k = static_cast<size_t>(lineIx);
  if (k >= st.drawing.lineCount())
    return false;
  PushUndoSnapshot(st, "Offset line");
  const double x0 = st.drawing.line(k).x0;
  const double y0 = st.drawing.line(k).y0;
  const double z0 = st.drawing.line(k).z0;  // read before push_back may reallocate
  const double x1 = st.drawing.line(k).x1;
  const double y1 = st.drawing.line(k).y1;
  const double z1 = st.drawing.line(k).z1;
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double dz = z1 - z0;
  if (std::hypot(std::hypot(dx, dy), dz) < 1e-8) {
    log.push_back("OFFSET — zero-length line.");
    return false;
  }
  // REQ-329 increment 7: under a tilted UCS the offset direction is perpendicular to the line IN the
  // active work plane (`UCS-Z × lineDir` — the same left-normal handedness `UnitLeftNormal` gives,
  // which under the World UCS reduces to exactly it). The offset copy then stays in the work plane
  // rather than sliding along world XY off it.
  double ox0 = 0.0, oy0 = 0.0, ox1 = 0.0, oy1 = 0.0, oz0 = z0, oz1 = z1;
  if (!CadWorkPlaneIsWorldXy(st)) {
    const ucs::Ucs u = CadActiveUcsStorage(st);
    const ray3d::Vec3 dir = ray3d::Normalize({dx, dy, dz});
    const ray3d::Vec3 perp = ray3d::Normalize(
        ray3d::Cross({u.zAxis.x, u.zAxis.y, u.zAxis.z}, dir));
    ox0 = x0 + perp.x * signedD;
    oy0 = y0 + perp.y * signedD;
    oz0 = z0 + perp.z * signedD;
    ox1 = x1 + perp.x * signedD;
    oy1 = y1 + perp.y * signedD;
    oz1 = z1 + perp.z * signedD;
  } else {
    double nx = 0.0, ny = 0.0;
    UnitLeftNormal(x0, y0, x1, y1, &nx, &ny);
    ox0 = x0 + nx * signedD;
    oy0 = y0 + ny * signedD;
    ox1 = x1 + nx * signedD;
    oy1 = y1 + ny * signedD;
  }
  // Defense in depth (issue #122, REQ-204 `finite-coords`): `signedD` comes from a side test on the
  // source geometry, so a source coordinate large enough to overflow the projection makes the offset
  // result inf/NaN even though every input was "finite". Never write that into the store — mirror
  // CommitCircle's non-finite refusal, and report it (REQ-201).
  if (!std::isfinite(ox0) || !std::isfinite(oy0) || !std::isfinite(oz0) || !std::isfinite(ox1) ||
      !std::isfinite(oy1) || !std::isfinite(oz1)) {
    log.push_back("OFFSET — the offset result is not a finite coordinate.");
    return false;
  }
  // The offset copy stays on the source line's plane — under the World UCS the offset is horizontal
  // so each end keeps its own Z; under a tilted UCS the perpendicular carries a Z component so the
  // copy stays in the work plane (REQ-057 / REQ-329 increment 7).
  st.drawing.pushLine(CadLineRecord{ox0, oy0, oz0, ox1, oy1, oz1});
  PushOffsetCopyAttrs(st, st.drawing.userLineAttrs, lineIx);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetCircle(AppCommandState& st, int ci, float signedD, std::vector<std::string>& log) {
  if (ci < 0 || static_cast<size_t>(ci) >= st.drawing.circleCount())
    return false;
  PushUndoSnapshot(st, "Offset circle");
  const CadCircleRecord src = st.drawing.circle(static_cast<size_t>(ci));  // copied before any push_back reallocates
  const double cx = src.cx;
  const double cy = src.cy;
  const double cz = src.cz;
  const double r = src.r;
  const double nr = r + signedD;
  if (nr <= 1e-6) {
    log.push_back("OFFSET — resulting circle radius too small.");
    return false;
  }
  st.drawing.pushCircle(CadCircleRecord{cx, cy, cz, nr});  // the offset copy stays on the source circle's plane
  PushOffsetCopyAttrs(st, st.drawing.userCircleAttrs, ci);
  float onx = 0.f, ony = 0.f, onz = 1.f;
  CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(ci), &onx, &ony, &onz);
  PushCircleNormal(st.drawing.userCircleNormals, onx, ony, onz);   // concentric: same plane (REQ-312)
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetArc(AppCommandState& st, int ai, float signedD, std::vector<std::string>& log) {
  if (ai < 0 || static_cast<size_t>(ai) >= st.drawing.userArcs.size())
    return false;
  PushUndoSnapshot(st, "Offset arc");
  const CadArc& a = st.drawing.userArcs[static_cast<size_t>(ai)];
  const double nr = a.r + signedD;
  if (nr <= 1e-6) {
    log.push_back("OFFSET — resulting arc radius too small.");
    return false;
  }
  CadArc o = a;
  o.r = nr;
  st.drawing.userArcs.push_back(o);
  PushOffsetCopyAttrs(st, st.drawing.userArcAttrs, ai);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetEllipse(AppCommandState& st, int ei, float signedD, std::vector<std::string>& log) {
  if (ei < 0 || static_cast<size_t>(ei) >= st.drawing.userEllipses.size())
    return false;
  PushUndoSnapshot(st, "Offset ellipse");
  const CadEllipse& e = st.drawing.userEllipses[static_cast<size_t>(ei)];
  const float ma = std::hypot(e.majVx, e.majVy);
  if (ma < 1e-8f) {
    log.push_back("OFFSET — degenerate ellipse.");
    return false;
  }
  const float f = (ma + signedD) / ma;
  if (ma + signedD <= 1e-6f) {
    log.push_back("OFFSET — resulting ellipse too small.");
    return false;
  }
  CadEllipse o = e;
  o.majVx *= f;
  o.majVy *= f;
  st.drawing.userEllipses.push_back(o);
  PushOffsetCopyAttrs(st, st.drawing.userEllAttrs, ei);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetPolyline(AppCommandState& st, int pi, float signedD, std::vector<std::string>& log) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
    return false;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  const int nv = v1 - v0;
  if (nv < 2) {
    log.push_back("OFFSET — polyline needs at least two vertices.");
    return false;
  }
  const bool closed =
      static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];

  std::vector<std::pair<double, double>> v;
  v.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i) {
    v.push_back({st.drawing.vertex(i).x, st.drawing.vertex(i).y});
  }

  const int n = static_cast<int>(v.size());
  const int nEdges = closed ? n : n - 1;
  if (nEdges < 1) {
    log.push_back("OFFSET — not enough edges.");
    return false;
  }

  std::vector<std::pair<double, double>> pa(static_cast<size_t>(nEdges)), pb(static_cast<size_t>(nEdges));
  for (int ei = 0; ei < nEdges; ++ei) {
    const int ia = ei;
    const int ib = closed ? (ei + 1) % n : ei + 1;
    const double ax = v[static_cast<size_t>(ia)].first;
    const double ay = v[static_cast<size_t>(ia)].second;
    const double bx = v[static_cast<size_t>(ib)].first;
    const double by = v[static_cast<size_t>(ib)].second;
    double nx = 0.0, ny = 0.0;
    UnitLeftNormal(ax, ay, bx, by, &nx, &ny);
    pa[static_cast<size_t>(ei)] = {ax + nx * signedD, ay + ny * signedD};
    pb[static_cast<size_t>(ei)] = {bx + nx * signedD, by + ny * signedD};
  }

  std::vector<std::pair<double, double>> out;
  if (!closed) {
    if (nEdges == 1) {
      out.push_back(pa[0]);
      out.push_back(pb[0]);
    } else {
      out.push_back(pa[0]);
      for (int ei = 0; ei < nEdges - 1; ++ei) {
        const auto& a0 = pa[static_cast<size_t>(ei)];
        const auto& b0 = pb[static_cast<size_t>(ei)];
        const auto& a1 = pa[(static_cast<size_t>(ei)+ 1)];
        const auto& b1 = pb[(static_cast<size_t>(ei)+ 1)];
        double ix = 0.0, iy = 0.0;
        if (LineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                  &ix, &iy))
          out.push_back({ix, iy});
        else {
          const double mx = 0.5 * (b0.first + a1.first);
          const double my = 0.5 * (b0.second + a1.second);
          out.push_back({mx, my});
        }
      }
      out.push_back(pb[(static_cast<size_t>(nEdges)- 1)]);
    }
  } else {
    out.resize(static_cast<size_t>(nEdges));
    for (int ei = 0; ei < nEdges; ++ei) {
      const int en = (ei + 1) % nEdges;
      const auto& a0 = pa[static_cast<size_t>(ei)];
      const auto& b0 = pb[static_cast<size_t>(ei)];
      const auto& a1 = pa[static_cast<size_t>(en)];
      const auto& b1 = pb[static_cast<size_t>(en)];
      double ix = 0.0, iy = 0.0;
      if (LineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second, &ix,
                               &iy))
        out[static_cast<size_t>(ei)] = {ix, iy};
      else
        out[static_cast<size_t>(ei)] = {0.5 * (b0.first + a1.first), 0.5 * (b0.second + a1.second)};
    }
  }

  if (out.size() < 2) {
    log.push_back("OFFSET — could not build offset polyline.");
    return false;
  }

  PushUndoSnapshot(st, "Offset polyline");
  if (st.drawing.userPolylineOffsets.empty())
    st.drawing.userPolylineOffsets.push_back(0);
  const int baseVert = st.drawing.userPolylineOffsets.back();
  // Carry the source's elevations rather than flattening (REQ-057). The offset is rebuilt from
  // edge intersections, so its vertex count only usually matches the source; when it does, Z maps
  // 1:1, and when it does not the first vertex's Z is used for the whole run — never 0, which
  // would silently drop a sloped polyline onto the datum.
  std::vector<float> srcZ;  // captured before appending, so the reads never alias the writes
  srcZ.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i)
    srcZ.push_back(static_cast<float>(st.drawing.vertex(i).z));
  const bool zMaps1to1 = out.size() == srcZ.size();
  const float fallbackZ = srcZ.empty() ? 0.f : srcZ.front();
  for (size_t oi = 0; oi < out.size(); ++oi) {
    st.drawing.pushVertex(CadPolyVertex{out[oi].first, out[oi].second, zMaps1to1 ? srcZ[oi] : fallbackZ});
  }
  st.drawing.userPolylineOffsets.push_back(baseVert + static_cast<int>(out.size()));
  st.drawing.userPolylineClosed.push_back(closed ? 1u : 0u);
  PushOffsetCopyAttrs(st, st.drawing.userPolylineAttrs, pi);
  BumpCadGpuCache(st);
  return true;
}

static bool CommitOffsetSigned(AppCommandState& st, float signedD, std::vector<std::string>& log) {
  if (!st.offsetEntityValid)
    return false;
  const SelectedEntity& e = st.offsetEntity;
  bool ok = false;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg:
    ok = CommitOffsetLine(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Circle:
    ok = CommitOffsetCircle(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Arc:
    ok = CommitOffsetArc(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Ellipse:
    ok = CommitOffsetEllipse(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Polyline:
    ok = CommitOffsetPolyline(st, e.index, signedD, log);
    break;
  case SelectedEntity::Type::Spline:
    log.push_back("OFFSET — spline entities are not supported; nothing changed.");
    return false;
  default:
    log.push_back("OFFSET — unsupported entity type.");
    return false;
  }
  if (ok)
    log.push_back("OFFSET — created parallel / concentric geometry.");
  return ok;
}

static void FinishOffsetAndIdle(AppCommandState& st, std::vector<std::string>& log) {
  (void)log;
  ResetOffsetDraft(st);
  st.active = AppCommandState::Kind::None;
}

/// REQ-103's documented TRIM/OFFSET per-target loop: after a successful offset, go back to
/// "select object" with the same typed distance / through-mode still armed, instead of ending the
/// command. The caller only reaches here after a successful CommitOffsetSigned.
static void LoopOffsetBackToSelect(AppCommandState& st, std::vector<std::string>& log) {
  ResetOffsetSelectionForNextPick(st);
  log.push_back("OFFSET — select next object to offset (same distance), or Enter/Esc to finish.");
}

static void HandleOffsetThroughPick(AppCommandState& st, double px, double py, std::vector<std::string>& log,
                                    bool loopToSelect = true) {
  if (!st.offsetEntityValid)
    return;
  const SelectedEntity& e = st.offsetEntity;
  const bool tilted = !CadWorkPlaneIsWorldXy(st);  // REQ-329 increment 7
  const ucs::Point2D cur = tilted ? OffsetPlaneLocal(st, px, py, CadCommitElevation(st))
                                  : ucs::Point2D{px, py};
  float signedD = 0.f;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return;
    double x0 = st.drawing.line(k).x0, y0 = st.drawing.line(k).y0;
    double x1 = st.drawing.line(k).x1, y1 = st.drawing.line(k).y1;
    if (tilted) {
      const ucs::Point2D a = OffsetPlaneLocal(st, x0, y0, st.drawing.line(k).z0);
      const ucs::Point2D b = OffsetPlaneLocal(st, x1, y1, st.drawing.line(k).z1);
      x0 = a.x; y0 = a.y;
      x1 = b.x; y1 = b.y;
    }
    signedD = static_cast<float>(SignedSideLine(x0, y0, x1, y1, cur.x, cur.y));
    break;
  }
  case SelectedEntity::Type::Circle: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.circleCount())
      return;
    const CadCircleRecord circle = st.drawing.circle(static_cast<size_t>(e.index));
    double cx = circle.cx, cy = circle.cy;
    const double r = circle.r;
    if (tilted) {
      const ucs::Point2D c = OffsetPlaneLocal(st, cx, cy, circle.cz);
      cx = c.x; cy = c.y;
    }
    signedD = static_cast<float>(SignedSideCircle(cx, cy, r, cur.x, cur.y));
    break;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    double cx = a.cx, cy = a.cy;
    if (tilted) {
      const ucs::Point2D c = OffsetPlaneLocal(st, a.cx, a.cy, a.z);
      cx = c.x; cy = c.y;
    }
    signedD = static_cast<float>(SignedSideCircle(cx, cy, a.r, cur.x, cur.y));
    break;
  }
  case SelectedEntity::Type::Polyline:
    log.push_back("OFFSET — polyline: type a distance, then pick a side (through-click not supported).");
    return;
  case SelectedEntity::Type::Ellipse:
    log.push_back("OFFSET — ellipse: type a distance, then pick a side (through-click not supported).");
    return;
  default:
    return;
  }
  if (std::fabs(signedD) < 1e-8f) {
    log.push_back("OFFSET — through point on original; pick farther away.");
    return;
  }
  if (CommitOffsetSigned(st, signedD, log) && loopToSelect)
    LoopOffsetBackToSelect(st, log);
}

static void HandleOffsetSidePick(AppCommandState& st, double px, double py, std::vector<std::string>& log,
                                 bool loopToSelect = true) {
  if (!st.offsetEntityValid || st.offsetTypedDistance <= 0.f)
    return;
  const float d = st.offsetTypedDistance;
  const SelectedEntity& e = st.offsetEntity;
  const bool tilted = !CadWorkPlaneIsWorldXy(st);
  // REQ-329 increment 7: on a tilted work plane the pick is resolved in the plane's own 2D frame.
  const ucs::Point2D cur = tilted ? OffsetPlaneLocal(st, px, py, CadCommitElevation(st))
                                  : ucs::Point2D{px, py};
  float sgn = 1.f;
  switch (e.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.lineCount())
      return;
    double ax = st.drawing.line(k).x0, ay = st.drawing.line(k).y0;
    double bx = st.drawing.line(k).x1, by = st.drawing.line(k).y1;
    if (tilted) {
      const ucs::Point2D a = OffsetPlaneLocal(st, ax, ay, st.drawing.line(k).z0);
      const ucs::Point2D b = OffsetPlaneLocal(st, bx, by, st.drawing.line(k).z1);
      ax = a.x; ay = a.y;
      bx = b.x; by = b.y;
    }
    const double sd = SignedSideLine(ax, ay, bx, by, cur.x, cur.y);
    sgn = sd >= 0.0 ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Circle: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.circleCount())
      return;
    const CadCircleRecord circle = st.drawing.circle(static_cast<size_t>(e.index));
    double cx = circle.cx, cy = circle.cy;
    const double r = circle.r;
    if (tilted) {
      const ucs::Point2D c = OffsetPlaneLocal(st, cx, cy, circle.cz);
      cx = c.x; cy = c.y;
    }
    const double side = SignedSideCircle(cx, cy, r, cur.x, cur.y);
    sgn = side >= 0.0 ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    double cx = a.cx, cy = a.cy;
    if (tilted) {
      const ucs::Point2D c = OffsetPlaneLocal(st, a.cx, a.cy, a.z);
      cx = c.x; cy = c.y;
    }
    const double side = SignedSideCircle(cx, cy, a.r, cur.x, cur.y);
    sgn = side >= 0.0 ? 1.f : -1.f;
    break;
  }
  case SelectedEntity::Type::Ellipse:
  case SelectedEntity::Type::Polyline: {
    if (tilted) {
      log.push_back(std::string("OFFSET — ") +
                    (e.type == SelectedEntity::Type::Polyline ? "polyline" : "ellipse") +
                    " offset on a tilted work plane is not supported yet (REQ-329) — offset a line,"
                    " circle or arc, or work on a plan-view UCS.");
      return;
    }
    if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi >= 0 && (static_cast<size_t>(pi)+ 1) < st.drawing.userPolylineOffsets.size()) {
        const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
        double best = 1e30;
        float bestS = 1.f;
        for (int vi = v0; vi + 1 < v1; ++vi) {
          const double ax = st.drawing.vertex(vi).x;
          const double ay = st.drawing.vertex(vi).y;
          const double bx = st.drawing.vertex(static_cast<size_t>(vi + 1)).x;
          const double by = st.drawing.vertex(static_cast<size_t>(vi + 1)).y;
          const double sd = SignedSideLine(ax, ay, bx, by, px, py);
          // Closest point on the SEGMENT (clamped), only to pick which edge is nearest the click —
          // this only decides which edge's sign wins, never a stored coordinate, so the shared
          // (float) ClosestPointOnSegment is precise enough here.
          float qx = 0.f, qy = 0.f;
          ClosestPointOnSegment(static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(bx),
                                static_cast<float>(by), static_cast<float>(px), static_cast<float>(py), &qx, &qy);
          const double dx = px - qx;
          const double dy = py - qy;
          const double dist2 = dx * dx + dy * dy;
          if (dist2 < best) {
            best = dist2;
            bestS = sd >= 0.0 ? 1.f : -1.f;
          }
        }
        sgn = bestS;
      }
    } else if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userEllipses.size()) {
      const CadEllipse& el = st.drawing.userEllipses[static_cast<size_t>(e.index)];
      const float ma = std::hypot(el.majVx, el.majVy);
      if (ma >= 1e-8f) {
        const float ux = el.majVx / ma;
        const float uy = el.majVy / ma;
        const float pxn = -uy;
        const float pyn = ux;
        const float mb = ma * el.ratio;
        constexpr float twopi = 6.28318530718f;
        double best = 1e30;
        double bx = el.cx, by = el.cy;
        for (int i = 0; i <= 48; ++i) {
          const float ang = twopi * static_cast<float>(i) / 48.f;
          const float c0 = std::cos(ang);
          const float s0 = std::sin(ang);
          const double ex = el.cx + static_cast<double>(ux * (ma * c0) + pxn * (mb * s0));
          const double ey = el.cy + static_cast<double>(uy * (ma * c0) + pyn * (mb * s0));
          const double dx = px - ex;
          const double dy = py - ey;
          const double dist2 = dx * dx + dy * dy;
          if (dist2 < best) {
            best = dist2;
            bx = ex;
            by = ey;
          }
        }
        const double ox = bx - el.cx;
        const double oy = by - el.cy;
        const double inX = px - el.cx;
        const double inY = py - el.cy;
        sgn = (inX * ox + inY * oy) >= 0.0 ? 1.f : -1.f;
      }
    }
    break;
  }
  default:
    return;
  }
  const float signedD = d * sgn;
  if (CommitOffsetSigned(st, signedD, log) && loopToSelect)
    LoopOffsetBackToSelect(st, log);
}

/// Run the existing model-space OFFSET solver against one paper object, then commit only its
/// resulting copy to the owning layout. The temporary state keeps the geometric rules shared while
/// the real document gets exactly one undo snapshot and no model-space mutation.
static bool CommitPaperOffset(AppCommandState& st, double px, double py, std::vector<std::string>& log) {
  const int li = st.offsetPaperLayoutIndex;
  if (li < 0 || static_cast<size_t>(li) >= st.drawing.paperLayouts.size() || !st.offsetEntityValid)
    return false;
  const PaperLayout& sourceLayout = st.drawing.paperLayouts[static_cast<size_t>(li)];
  AppCommandState staged;
  staged.activeSpaceIndex = kModelSpaceIndex;
  staged.drawing.userLinesFlat.clear(); staged.drawing.userLineAttrs.clear();
  staged.drawing.userCirclesCxCyZR.clear(); staged.drawing.userCircleAttrs.clear(); staged.drawing.userCircleNormals.clear();
  staged.drawing.userArcs.clear(); staged.drawing.userArcAttrs.clear();
  staged.drawing.userEllipses.clear(); staged.drawing.userEllAttrs.clear();
  staged.drawing.userPolylineOffsets.clear(); staged.drawing.userPolylineVerts.clear(); staged.drawing.userPolylineVertsBulge.clear();
  staged.drawing.userPolylineVertsNormal.clear(); staged.drawing.userPolylineClosed.clear(); staged.drawing.userPolylineAttrs.clear();
  staged.offsetEntity = {st.offsetEntity.type, 0};
  staged.offsetEntityValid = true;
  staged.offsetTypedDistance = st.offsetTypedDistance;
  staged.offsetThroughMode = st.offsetThroughMode;
  staged.offsetPhase = st.offsetPhase;
  const EntityAttributes fallback = MakeNewEntityAttrs(st);
  EntityAttributes inherited = fallback;
  using PT = PaperRef::Type;
  const int index = st.offsetEntity.index;
  switch (st.offsetEntity.type) {
  case SelectedEntity::Type::LineSeg: {
    const size_t k = static_cast<size_t>(index) * 6;
    if (index < 0 || k + 5 >= sourceLayout.paperLines.size()) return false;
    for (size_t i = 0; i < 6; ++i) staged.drawing.userLinesFlat.push_back(sourceLayout.paperLines[k + i]);
    if (static_cast<size_t>(index) < sourceLayout.paperLineAttrs.size()) inherited = sourceLayout.paperLineAttrs[index];
    staged.drawing.userLineAttrs.push_back(inherited);
    break;
  }
  case SelectedEntity::Type::Circle: {
    const size_t k = static_cast<size_t>(index) * 3;
    if (index < 0 || k + 2 >= sourceLayout.paperCircles.size()) return false;
    staged.drawing.userCirclesCxCyZR.clear();
    staged.drawing.pushCircle(CadCircleRecord{sourceLayout.paperCircles[k], sourceLayout.paperCircles[k + 1], 0.0,
                                      sourceLayout.paperCircles[k + 2]});
    staged.drawing.userCircleNormals = {0.f, 0.f, 1.f};
    if (static_cast<size_t>(index) < sourceLayout.paperCircleAttrs.size()) inherited = sourceLayout.paperCircleAttrs[index];
    staged.drawing.userCircleAttrs.push_back(inherited);
    break;
  }
  case SelectedEntity::Type::Arc:
    if (index < 0 || static_cast<size_t>(index) >= sourceLayout.paperArcs.size()) return false;
    staged.drawing.userArcs.push_back(sourceLayout.paperArcs[index]);
    if (static_cast<size_t>(index) < sourceLayout.paperArcAttrs.size()) inherited = sourceLayout.paperArcAttrs[index];
    staged.drawing.userArcAttrs.push_back(inherited);
    break;
  case SelectedEntity::Type::Ellipse:
    if (index < 0 || static_cast<size_t>(index) >= sourceLayout.paperEllipses.size()) return false;
    staged.drawing.userEllipses.push_back(sourceLayout.paperEllipses[index]);
    if (static_cast<size_t>(index) < sourceLayout.paperEllAttrs.size()) inherited = sourceLayout.paperEllAttrs[index];
    staged.drawing.userEllAttrs.push_back(inherited);
    break;
  case SelectedEntity::Type::Polyline: {
    if (index < 0 || static_cast<size_t>(index) + 1 >= sourceLayout.paperPolyOffsets.size()) return false;
    const int v0 = sourceLayout.paperPolyOffsets[index], v1 = sourceLayout.paperPolyOffsets[index + 1];
    if (v0 < 0 || v1 < v0 || static_cast<size_t>(v1) * 3 > sourceLayout.paperPolyVerts.size()) return false;
    staged.drawing.userPolylineOffsets = {0, v1 - v0};
    for (int v = v0; v < v1; ++v) {
      staged.drawing.pushVertex(CadPolyVertex{sourceLayout.paperPolyVerts[static_cast<size_t>(v) * 3], sourceLayout.paperPolyVerts[static_cast<size_t>(v) * 3 + 1], 0.0});
      staged.drawing.userPolylineVertsBulge.push_back(0.f);
      staged.drawing.userPolylineVertsNormal.insert(staged.drawing.userPolylineVertsNormal.end(), {0.f, 0.f, 1.f});
    }
    staged.drawing.userPolylineClosed.push_back(static_cast<size_t>(index) < sourceLayout.paperPolyClosed.size() ? sourceLayout.paperPolyClosed[index] : 0);
    if (static_cast<size_t>(index) < sourceLayout.paperPolyAttrs.size()) inherited = sourceLayout.paperPolyAttrs[index];
    staged.drawing.userPolylineAttrs.push_back(inherited);
    break;
  }
  default: return false;
  }

  std::vector<std::string> stagedLog;
  if (st.offsetPhase == AppCommandState::OffsetPhase::WaitThroughPick)
    HandleOffsetThroughPick(staged, px, py, stagedLog, false);
  else
    HandleOffsetSidePick(staged, px, py, stagedLog, false);
  const bool produced = staged.drawing.userLinesFlat.size() > 6 || staged.drawing.circleCount() > 1 ||
                        staged.drawing.userArcs.size() > 1 || staged.drawing.userEllipses.size() > 1 ||
                        staged.drawing.userPolylineOffsets.size() > 2;
  if (!produced) {
    log.insert(log.end(), stagedLog.begin(), stagedLog.end());
    return false;
  }

  PaperLayout updated = sourceLayout;
  switch (st.offsetEntity.type) {
  case SelectedEntity::Type::LineSeg:
    for (size_t i = 6; i < staged.drawing.userLinesFlat.size(); ++i) updated.paperLines.push_back(static_cast<float>(staged.drawing.userLinesFlat[i]));
    updated.paperLineAttrs.push_back(inherited);
    break;
  case SelectedEntity::Type::Circle:
    updated.paperCircles.insert(updated.paperCircles.end(), {static_cast<float>(staged.drawing.circle(1).cx),
        static_cast<float>(staged.drawing.circle(1).cy), static_cast<float>(staged.drawing.circle(1).r)});
    updated.paperCircleAttrs.push_back(inherited);
    break;
  case SelectedEntity::Type::Arc:
    updated.paperArcs.push_back(staged.drawing.userArcs.back()); updated.paperArcAttrs.push_back(inherited); break;
  case SelectedEntity::Type::Ellipse:
    updated.paperEllipses.push_back(staged.drawing.userEllipses.back()); updated.paperEllAttrs.push_back(inherited); break;
  case SelectedEntity::Type::Polyline: {
    const int oldEnd = updated.paperPolyOffsets.empty() ? 0 : updated.paperPolyOffsets.back();
    const int start = staged.drawing.userPolylineOffsets[1];
    const int end = staged.drawing.userPolylineOffsets[2];
    for (int v = start; v < end; ++v) {
      updated.paperPolyVerts.push_back(static_cast<float>(staged.drawing.vertex(v).x));
      updated.paperPolyVerts.push_back(static_cast<float>(staged.drawing.vertex(v).y));
      updated.paperPolyVerts.push_back(0.f);
    }
    if (updated.paperPolyOffsets.empty()) updated.paperPolyOffsets.push_back(oldEnd);
    updated.paperPolyOffsets.push_back(oldEnd + end - start);
    updated.paperPolyClosed.push_back(staged.drawing.userPolylineClosed.back());
    updated.paperPolyAttrs.push_back(inherited);
    break;
  }
  default: return false;
  }
  PushUndoSnapshot(st, "Offset paper geometry");
  st.drawing.paperLayouts[static_cast<size_t>(li)] = std::move(updated);
  BumpCadGpuCache(st);
  log.insert(log.end(), stagedLog.begin(), stagedLog.end());
  LoopOffsetBackToSelect(st, log);
  return true;
}



static void HandleOffsetViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  using OP = AppCommandState::OffsetPhase;
  switch (st.offsetPhase) {
  case OP::WaitDistanceOrThrough:
    // REQ-103: distance/through-mode is specified BEFORE an object is picked, matching AutoCAD's
    // own OFFSET order. A viewport click here isn't a valid answer to that prompt.
    log.push_back("OFFSET — type a positive offset distance, or T for through-point mode.");
    return;
  case OP::WaitSelectEntity: {
    SelectedEntity hit{};
    float d2 = 0.f;
    if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
      log.push_back("OFFSET — nothing under cursor; try again.");
      return;
    }
    // REQ-087 / REQ-201. A feature line is pickable (it has to be, to select and move), so without
    // this it would be accepted here and then dropped silently by CommitOffsetSigned's `default:`.
    // Refusing is not a limitation we are hiding — offsetting a 3D chain has no defined answer for
    // what elevation the offset copy carries, and neither REQ-087 nor REQ-088 supplies one, so the
    // Workshop does not get to pick one (CLAUDE.md layer rule 3). Stays in the select phase.
    if (hit.type == SelectedEntity::Type::FeatureLine) {
      log.push_back("OFFSET — 1 feature line ignored: offsetting one has no defined elevation for "
                    "the new line. Pick a line, circle, arc, ellipse, or polyline.");
      return;
    }
    // REQ-068 / ADR-036 (c) — the same reasoning, one kind later. A surface became pickable, so
    // without this it would be accepted here and then dropped by CommitOffsetSigned's `default:`,
    // leaving the user having picked something and watched nothing happen. There is also no answer
    // to offer: "offset a surface" would mean a second surface at a vertical or normal displacement,
    // which is a grading operation no requirement defines.
    if (hit.type == SelectedEntity::Type::Surface) {
      log.push_back("OFFSET — 1 surface ignored: a surface cannot be offset. Pick a line, circle, "
                    "arc, ellipse, or polyline.");
      return;
    }
    if (hit.type == SelectedEntity::Type::Spline) {
      log.push_back("OFFSET — splines are not supported; nothing changed.");
      return;
    }
    st.offsetEntity = hit;
    st.offsetEntityValid = true;
    if (st.offsetThroughMode) {
      st.offsetPhase = OP::WaitThroughPick;
      log.push_back("OFFSET — click the through point (line / circle / arc).");
    } else {
      st.offsetPhase = OP::WaitSidePick;
      log.push_back("OFFSET — pick which side of the object to offset.");
    }
    return;
  }
  case OP::WaitThroughPick:
    HandleOffsetThroughPick(st, wx, wy, log);
    return;
  case OP::WaitSidePick:
    HandleOffsetSidePick(st, wx, wy, log);
    return;
  }
}

} // namespace OffsetCmd
