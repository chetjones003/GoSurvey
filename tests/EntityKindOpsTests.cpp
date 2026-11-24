// Entity-kind operations (ADR-0011 / issue #1577 phase 0): table-driven characterisation test.
//
// Puts one entity of every kind through MOVE, COPY, ROTATE (plan), ROTATE (copy), ROTATE (3D axis
// via a tilted UCS), SCALE, MIRROR, STRETCH, ERASE, and extents via `DispatchByPrimaryForTest`,
// recording today's exact behaviour — including every kind-coverage gap and silent skip.
//
// Phase 2a must keep this entire table green without changing a single row; phase 2b closes the
// documented gaps one row at a time.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "HatchGeom.hpp"
#include <GoSurvey/Geometry/brep.hpp>
#include <GoSurvey/Survey/AlignmentModel.hpp>

namespace {

constexpr double kGeomTol = 1e-3;
constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = 0.5 * kPi;

// Shared transform parameters (chosen asymmetric so X, Y, Z and no-ops never collide).
constexpr double kMoveDx = 10.0;
constexpr double kMoveDy = 20.0;
constexpr double kMoveDz = 5.0;

constexpr double kScaleFactor = 2.0;

// STRETCH crossing box [14, 25] x [5, 25] and displacement (+5, +3, 0).
// Multi-vertex entities start at x=10 (outside) and end at x=20 (inside), so true partial stretch
// moves only the inside vertex; single-anchor entities sit at (16, 12) (inside).
constexpr double kStretchDx = 5.0;
constexpr double kStretchDy = 3.0;
constexpr double kStretchBoxMinX = 14.0;
constexpr double kStretchBoxMaxX = 25.0;
constexpr double kStretchBoxMinY = 5.0;
constexpr double kStretchBoxMaxY = 25.0;

void SubmitLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  char buf[512] = {};
  std::snprintf(buf, sizeof(buf), "%s", line.c_str());
  ProcessCommandLineSubmit(buf, static_cast<int>(sizeof(buf)), st, log);
}

bool LogHasRefusal(const std::vector<std::string>& log) {
  for (const std::string& line : log) {
    if (line.find("excluded") != std::string::npos ||
        line.find("not supported") != std::string::npos ||
        line.find("not duplicated") != std::string::npos ||
        line.find("unchanged") != std::string::npos)
      return true;
  }
  return false;
}

std::string JoinLog(const std::vector<std::string>& log) {
  std::string out;
  for (size_t i = 0; i < log.size(); ++i) {
    if (i != 0)
      out += " | ";
    out += log[i];
  }
  return out;
}

struct EntityProbe {
  std::vector<ray3d::Vec3> pts;
  std::vector<double> scalars;
};

// Pure coordinate maps matching the five rigid/similarity transforms driven below.
ray3d::Vec3 MapMove3d(const ray3d::Vec3& p) { return {p.x + kMoveDx, p.y + kMoveDy, p.z + kMoveDz}; }
ray3d::Vec3 MapMoveXy(const ray3d::Vec3& p) { return {p.x + kMoveDx, p.y + kMoveDy, p.z}; }
// ROTATE "90" CW from north = -90 deg CCW about world +Z: (x, y, z) -> (y, -x, z).
ray3d::Vec3 MapRotPlan(const ray3d::Vec3& p) { return {p.y, -p.x, p.z}; }
// Tilted UCS (UCS X 90 -> ucs.zAxis = (0,-1,0)) + ROTATE "90" (-90 deg about (0,-1,0) = +90 deg
// about +Y): (x, y, z) -> (z, y, -x).
ray3d::Vec3 MapRot3d(const ray3d::Vec3& p) { return {p.z, p.y, -p.x}; }
ray3d::Vec3 MapScaleXy(const ray3d::Vec3& p) { return {p.x * kScaleFactor, p.y * kScaleFactor, p.z}; }
ray3d::Vec3 MapScale3d(const ray3d::Vec3& p) {
  return {p.x * kScaleFactor, p.y * kScaleFactor, p.z * kScaleFactor};
}
// MIRROR across Y axis (0,0)-(0,10): (x, y, z) -> (-x, y, z).
ray3d::Vec3 MapMirrorX(const ray3d::Vec3& p) { return {-p.x, p.y, p.z}; }

bool PointInStretchBox(const ray3d::Vec3& p) {
  return p.x >= kStretchBoxMinX && p.x <= kStretchBoxMaxX &&
         p.y >= kStretchBoxMinY && p.y <= kStretchBoxMaxY;
}

ray3d::Vec3 MapStretchPoint(const ray3d::Vec3& p) {
  if (PointInStretchBox(p))
    return {p.x + kStretchDx, p.y + kStretchDy, p.z};
  return p;
}

ray3d::Vec3 MapStretchWhole(const ray3d::Vec3& p) { return {p.x + kStretchDx, p.y + kStretchDy, p.z}; }

EntityProbe MapPts(const EntityProbe& src, const std::function<ray3d::Vec3(const ray3d::Vec3&)>& fn) {
  EntityProbe out = src;
  for (ray3d::Vec3& p : out.pts)
    p = fn(p);
  return out;
}

// Normalizes an axis-aligned 2D rectangle stored as 2 corners {min, max} after a point map.
EntityProbe MapAabb2d(const EntityProbe& src, const std::function<ray3d::Vec3(const ray3d::Vec3&)>& fn) {
  EntityProbe out = src;
  const double x0 = src.pts[0].x, y0 = src.pts[0].y, z0 = src.pts[0].z;
  const double x1 = src.pts[1].x, y1 = src.pts[1].y;
  const ray3d::Vec3 c[4] = {
      fn({x0, y0, z0}), fn({x1, y0, z0}), fn({x1, y1, z0}), fn({x0, y1, z0}),
  };
  double mnX = c[0].x, mxX = c[0].x, mnY = c[0].y, mxY = c[0].y;
  for (int i = 1; i < 4; ++i) {
    mnX = std::min(mnX, c[i].x);
    mxX = std::max(mxX, c[i].x);
    mnY = std::min(mnY, c[i].y);
    mxY = std::max(mxY, c[i].y);
  }
  out.pts[0] = {mnX, mnY, c[0].z};
  out.pts[1] = {mxX, mxY, c[0].z};
  return out;
}

// Computes exact AABB of an axis-aligned 3D box stored as {min, max} after a linear/affine point map.
EntityProbe MapAabb3d(const EntityProbe& src, const std::function<ray3d::Vec3(const ray3d::Vec3&)>& fn) {
  EntityProbe out = src;
  const ray3d::Vec3& a = src.pts[0];
  const ray3d::Vec3& b = src.pts[1];
  ray3d::Vec3 mn{}, mx{};
  for (int ix = 0; ix < 2; ++ix) {
    for (int iy = 0; iy < 2; ++iy) {
      for (int iz = 0; iz < 2; ++iz) {
        const ray3d::Vec3 p = fn({ix ? b.x : a.x, iy ? b.y : a.y, iz ? b.z : a.z});
        if (ix == 0 && iy == 0 && iz == 0) {
          mn = mx = p;
        } else {
          mn.x = std::min(mn.x, p.x);
          mn.y = std::min(mn.y, p.y);
          mn.z = std::min(mn.z, p.z);
          mx.x = std::max(mx.x, p.x);
          mx.y = std::max(mx.y, p.y);
          mx.z = std::max(mx.z, p.z);
        }
      }
    }
  }
  out.pts[0] = mn;
  out.pts[1] = mx;
  return out;
}

void CheckProbeNear(const EntityProbe& actual, const EntityProbe& expected, const char* kindName,
                    const char* opName, const char* which) {
  INFO("kind=" << kindName << " op=" << opName << " (" << which << ")");
  REQUIRE(actual.pts.size() == expected.pts.size());
  for (size_t i = 0; i < actual.pts.size(); ++i) {
    INFO("pt[" << i << "] actual=(" << actual.pts[i].x << "," << actual.pts[i].y << "," << actual.pts[i].z
               << ") expected=(" << expected.pts[i].x << "," << expected.pts[i].y << "," << expected.pts[i].z << ")");
    CHECK(std::fabs(actual.pts[i].x - expected.pts[i].x) <= kGeomTol);
    CHECK(std::fabs(actual.pts[i].y - expected.pts[i].y) <= kGeomTol);
    CHECK(std::fabs(actual.pts[i].z - expected.pts[i].z) <= kGeomTol);
  }
  REQUIRE(actual.scalars.size() == expected.scalars.size());
  for (size_t i = 0; i < actual.scalars.size(); ++i) {
    INFO("scalar[" << i << "] actual=" << actual.scalars[i] << " expected=" << expected.scalars[i]);
    CHECK(std::fabs(actual.scalars[i] - expected.scalars[i]) <= kGeomTol);
  }
}

enum class Outcome : std::uint8_t {
  Applied,        // Count stayed 1; geometry matches expected transform.
  Duplicated,     // Count became 2; [0] unchanged, [1] matches expected transform.
  Refused,        // Count stayed 1; geometry unchanged; log states refusal/exclusion.
  Skipped,        // Gap: count stayed 1; geometry unchanged; no refusal logged.
  BoxMissed,      // Gap (STRETCH): crossing box did not select entity; geometry unchanged.
  NotInSelection, // SectionPlane: not held in st.selection, so transform commands prompt for selection.
  Erased,         // ERASE: count dropped from 1 to 0.
};

struct OpExpect {
  Outcome outcome = Outcome::Skipped;
  std::function<EntityProbe(const EntityProbe&)> expectedGeom;
};

struct ExtentsExpect {
  bool in2d = false;
  bool in3d = false;
  bool zoomExtentsFrames = false;
  double mnX = 0.0, mxX = 0.0, mnY = 0.0, mxY = 0.0;
  double mnZ = 0.0, mxZ = 0.0;
};

struct KindRow {
  const char* name = "";
  std::function<void(AppCommandState&)> seed;
  std::function<size_t(const AppCommandState&)> count;
  std::function<EntityProbe(const AppCommandState&, size_t)> probe;
  OpExpect move;
  OpExpect copy;
  OpExpect rotatePlan;
  OpExpect rotateCopy;
  OpExpect rotate3d;
  OpExpect scale;
  OpExpect mirror;
  OpExpect stretch;
  Outcome erase = Outcome::Erased;
  ExtentsExpect extents;
};

// Convenience builders for common OpExpect entries.
OpExpect Applied(std::function<EntityProbe(const EntityProbe&)> fn) { return {Outcome::Applied, std::move(fn)}; }
OpExpect Duplicated(std::function<EntityProbe(const EntityProbe&)> fn) {
  return {Outcome::Duplicated, std::move(fn)};
}
OpExpect Refused() { return {Outcome::Refused, {}}; }
OpExpect Skipped() { return {Outcome::Skipped, {}}; }
OpExpect BoxMissed() { return {Outcome::BoxMissed, {}}; }
OpExpect NotInSel() { return {Outcome::NotInSelection, {}}; }

// ---------------------------------------------------------------------------
// Seed + probe helpers for each entity kind
// ---------------------------------------------------------------------------

void SelectOne(AppCommandState& st, SelectedEntity::Type type) {
  st.selection.clear();
  st.selection.push_back({type, 0});
}

// Helper for Arc probe: centre, start point in world, end point in world, radius + normal.
EntityProbe ProbeArc(const CadArc& a) {
  const ray3d::Vec3 sp = CurveWorldPointOnArc(a, static_cast<double>(a.startRad));
  const ray3d::Vec3 ep = CurveWorldPointOnArc(a, static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad));
  return {{ray3d::Vec3{a.cx, a.cy, a.z}, sp, ep}, {a.r, a.nx, a.ny, a.nz}};
}

// Expected arc probe for rigid/similarity transforms on our seeded flat arc (cx=10, cy=12, z=3, r=6,
// start=0 -> (16,12,3) inside stretch box, end=pi/2 -> (10,18,3) outside stretch box, normal=(0,0,1)).
EntityProbe ExpectArcMove(const EntityProbe& p) { return MapPts(p, MapMove3d); }
EntityProbe ExpectArcRotPlan(const EntityProbe& p) { return MapPts(p, MapRotPlan); }
EntityProbe ExpectArcRot3d(const EntityProbe& p) {
  EntityProbe out = MapPts(p, MapRot3d);
  // Normal (0,0,1) rotated +90 deg about +Y -> (1,0,0).
  out.scalars[1] = 1.0;
  out.scalars[2] = 0.0;
  out.scalars[3] = 0.0;
  return out;
}
EntityProbe ExpectArcScale(const EntityProbe& p) {
  EntityProbe out = MapPts(p, MapScaleXy);
  out.scalars[0] = p.scalars[0] * kScaleFactor;
  return out;
}
EntityProbe ExpectArcMirror(const EntityProbe& p) {
  // Reflection reverses handedness: old end becomes new start, old start becomes new end.
  EntityProbe out = p;
  out.pts[0] = MapMirrorX(p.pts[0]);
  out.pts[1] = MapMirrorX(p.pts[2]);
  out.pts[2] = MapMirrorX(p.pts[1]);
  return out;
}

std::vector<KindRow> BuildTable() {
  std::vector<KindRow> rows;

  // 1. LineSeg: (10,12,3) -> (20,12,4). Start outside stretch box, end inside.
  rows.push_back(KindRow{
      "LineSeg",
      [](AppCommandState& st) {
        st.drawing.pushLine(CadLineRecord{10.0, 12.0, 3.0, 20.0, 12.0, 4.0});
        st.drawing.userLineAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::LineSeg);
      },
      [](const AppCommandState& st) { return st.drawing.lineCount(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadLineRecord ln = st.drawing.line(i);
        return {{{ln.x0, ln.y0, ln.z0}, {ln.x1, ln.y1, ln.z1}}, {}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapScaleXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchPoint); }),
      Outcome::Erased,
      {true, true, true, 10.0, 20.0, 12.0, 12.0, 3.0, 4.0},
  });

  // 2. Circle: centre (16,12,3), r=2, normal=(0,0,1). Centre inside stretch box.
  rows.push_back(KindRow{
      "Circle",
      [](AppCommandState& st) {
        st.drawing.pushCircle(CadCircleRecord{16.0, 12.0, 3.0, 2.0});
        st.drawing.userCircleAttrs.push_back(EntityAttributes{});
        st.drawing.userCircleNormals.insert(st.drawing.userCircleNormals.end(), {0.f, 0.f, 1.f});
        SelectOne(st, SelectedEntity::Type::Circle);
      },
      [](const AppCommandState& st) { return st.drawing.circleCount(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadCircleRecord c = st.drawing.circle(i);
        const size_t n = i * 3;
        const double nx = n + 2 < st.drawing.userCircleNormals.size() ? st.drawing.userCircleNormals[n] : 0.0;
        const double ny = n + 2 < st.drawing.userCircleNormals.size() ? st.drawing.userCircleNormals[n + 1] : 0.0;
        const double nz = n + 2 < st.drawing.userCircleNormals.size() ? st.drawing.userCircleNormals[n + 2] : 1.0;
        return {{{c.cx, c.cy, c.cz}}, {c.r, nx, ny, nz}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRot3d);
        out.scalars[1] = 1.0;
        out.scalars[2] = 0.0;
        out.scalars[3] = 0.0;
        return out;
      }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      {true, true, true, 14.0, 18.0, 10.0, 14.0, 3.0, 3.0},
  });

  // 3. Arc: centre (10,12,3), r=6, start=0 (16,12,3 inside box), sweep=pi/2 (end=(10,18,3) outside).
  rows.push_back(KindRow{
      "Arc",
      [](AppCommandState& st) {
        CadArc a;
        a.cx = 10.0;
        a.cy = 12.0;
        a.z = 3.0;
        a.r = 6.0;
        a.startRad = 0.f;
        a.sweepRad = static_cast<float>(kHalfPi);
        st.drawing.userArcs.push_back(a);
        st.drawing.userArcAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Arc);
      },
      [](const AppCommandState& st) { return st.drawing.userArcs.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe { return ProbeArc(st.drawing.userArcs[i]); },
      Applied(ExpectArcMove),
      Duplicated(ExpectArcMove),
      Applied(ExpectArcRotPlan),
      Duplicated(ExpectArcRotPlan),
      Applied(ExpectArcRot3d),
      Applied(ExpectArcScale),
      Duplicated(ExpectArcMirror),
      // StretchOneArc moves start (16,12)->(21,15), keeps end (10,18), and refits centre/radius
      // preserving sweepRad = +pi/2 via RecomputeArcFromEndpoints -> centre (14,11), r = hypot(7,4).
      Applied([](const EntityProbe& p) -> EntityProbe {
        EntityProbe out = p;
        out.pts[0] = {14.0, 11.0, 3.0};
        out.pts[1] = {21.0, 15.0, 3.0};
        out.pts[2] = {10.0, 18.0, 3.0};
        out.scalars[0] = std::hypot(7.0, 4.0);
        return out;
      }),
      Outcome::Erased,
      // Note: ComputeWorldExtents (<16) samples the arc [10..16, 12..18]; ComputeWorldExtents3d uses
      // CollectEntityBoxes which uses the full-circle box [cx-r..cx+r, cy-r..cy+r]. Wait: we verify
      // exact 2D and 3D bounds separately in the runner! Here mn/mx are the 2D extents.
      {true, true, true, 10.0, 16.0, 12.0, 18.0, 3.0, 3.0},
  });

  // 4. Ellipse: centre (16,12,3), major=(3,0), ratio=0.5 (minor=1.5), full turn.
  rows.push_back(KindRow{
      "Ellipse",
      [](AppCommandState& st) {
        CadEllipse el;
        el.cx = 16.0;
        el.cy = 12.0;
        el.z = 3.0;
        el.majVx = 3.f;
        el.majVy = 0.f;
        el.ratio = 0.5f;
        st.drawing.userEllipses.push_back(el);
        st.drawing.userEllAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Ellipse);
      },
      [](const AppCommandState& st) { return st.drawing.userEllipses.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadEllipse& el = st.drawing.userEllipses[i];
        return {{{el.cx, el.cy, el.z}, {el.cx + el.majVx, el.cy + el.majVy, el.z}}, {el.ratio}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Refused(), // Tilted axis rotation refuses Ellipse (no 3D orientation path).
      Applied([](const EntityProbe& p) { return MapPts(p, MapScaleXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      {true, true, true, 13.0, 19.0, 10.5, 13.5, 3.0, 3.0},
  });

  // 5. Polyline: (10,12,3) -> (15,12,3) -> (20,14,4). First vertex outside stretch box, last two inside.
  rows.push_back(KindRow{
      "Polyline",
      [](AppCommandState& st) {
        st.drawing.userPolylineOffsets = {0, 3};
        st.drawing.pushVertex(CadPolyVertex{10.0, 12.0, 3.0});
        st.drawing.pushVertex(CadPolyVertex{15.0, 12.0, 3.0});
        st.drawing.pushVertex(CadPolyVertex{20.0, 14.0, 4.0});
        st.drawing.userPolylineVertsBulge = {0.f, 0.f, 0.f};
        st.drawing.userPolylineVertsNormal = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
        st.drawing.userPolylineClosed = {0};
        st.drawing.userPolylineAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Polyline);
      },
      [](const AppCommandState& st) {
        return st.drawing.userPolylineOffsets.size() > 1 ? st.drawing.userPolylineOffsets.size() - 1 : 0;
      },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        EntityProbe p;
        const int v0 = st.drawing.userPolylineOffsets[i];
        const int v1 = st.drawing.userPolylineOffsets[i + 1];
        for (int vi = v0; vi < v1; ++vi) {
          const CadPolyVertex v = st.drawing.vertex(static_cast<size_t>(vi));
          p.pts.push_back({v.x, v.y, v.z});
        }
        return p;
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapScaleXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchPoint); }),
      Outcome::Erased,
      {true, true, true, 10.0, 20.0, 12.0, 14.0, 3.0, 4.0},
  });

  // 6a. Annotation (Text): ins=(16,12,3), plottedHeight=0.1, rot=0.
  rows.push_back(KindRow{
      "AnnotationText",
      [](AppCommandState& st) {
        CadAnnotation a;
        a.kind = CadAnnotation::Kind::Text;
        a.insX = 16.f;
        a.insY = 12.f;
        a.insZ = 3.f;
        a.plottedHeightInches = 0.1f;
        a.rotationRad = 0.f;
        a.text = "HI";
        st.drawing.cadAnnotations.push_back(a);
        st.drawing.cadAnnotationAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Annotation);
      },
      [](const AppCommandState& st) { return st.drawing.cadAnnotations.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadAnnotation& a = st.drawing.cadAnnotations[i];
        return {{{a.insX, a.insY, a.insZ}}, {a.rotationRad, a.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[0] = p.scalars[0] - kHalfPi;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[0] = p.scalars[0] - kHalfPi;
        return out;
      }),
      Refused(), // Tilted-axis rotation refuses Annotation.
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        return out;
      }),
      // MIRRTEXT-off: insertion reflects, rotation stays upright.
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // Default modelUnitsPerPlottedInch = 50 -> h = 5.0, w = 0.55 * 5.0 * 2 = 5.5.
      {true, true, true, 16.0, 21.5, 7.0, 12.0, 3.0, 3.0},
  });

  // 6b. Annotation (Mtext): box [16,20] x [12,15], ins=(16,12,3).
  rows.push_back(KindRow{
      "AnnotationMtext",
      [](AppCommandState& st) {
        CadAnnotation a;
        a.kind = CadAnnotation::Kind::Mtext;
        a.insX = 16.f;
        a.insY = 12.f;
        a.insZ = 3.f;
        a.boxMinX = 16.f;
        a.boxMinY = 12.f;
        a.boxMaxX = 20.f;
        a.boxMaxY = 15.f;
        a.plottedHeightInches = 0.1f;
        a.text = "NOTE";
        st.drawing.cadAnnotations.push_back(a);
        st.drawing.cadAnnotationAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Annotation);
      },
      [](const AppCommandState& st) { return st.drawing.cadAnnotations.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadAnnotation& a = st.drawing.cadAnnotations[i];
        return {{{a.boxMinX, a.boxMinY, a.insZ}, {a.boxMaxX, a.boxMaxY, a.insZ}},
                {a.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // Existing bug recorded for phase 0: Mtext ROTATE / ROTATE-copy / MIRROR initialize mnX/mxX/mnY/mxY
      // from the UN-TRANSFORMED boxMin corner before looping over the 4 transformed corners, so the
      // resulting AABB includes the old boxMin corner (16,12).
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapAabb2d(p, MapRotPlan);
        out.pts[0].x = std::min(out.pts[0].x, p.pts[0].x);
        out.pts[1].x = std::max(out.pts[1].x, p.pts[0].x);
        out.pts[0].y = std::min(out.pts[0].y, p.pts[0].y);
        out.pts[1].y = std::max(out.pts[1].y, p.pts[0].y);
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapAabb2d(p, MapRotPlan);
        out.pts[0].x = std::min(out.pts[0].x, p.pts[0].x);
        out.pts[1].x = std::max(out.pts[1].x, p.pts[0].x);
        out.pts[0].y = std::min(out.pts[0].y, p.pts[0].y);
        out.pts[1].y = std::max(out.pts[1].y, p.pts[0].y);
        return out;
      }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapAabb2d(p, MapMirrorX);
        out.pts[0].x = std::min(out.pts[0].x, p.pts[0].x);
        out.pts[1].x = std::max(out.pts[1].x, p.pts[0].x);
        out.pts[0].y = std::min(out.pts[0].y, p.pts[0].y);
        out.pts[1].y = std::max(out.pts[1].y, p.pts[0].y);
        return out;
      }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      {true, true, true, 16.0, 20.0, 12.0, 15.0, 3.0, 3.0},
  });

  // 6c. Annotation (DimAligned): ext1=(16,12), ext2=(20,12), offset=2, ins=(18,14,3).
  rows.push_back(KindRow{
      "AnnotationDimAligned",
      [](AppCommandState& st) {
        CadAnnotation a;
        a.kind = CadAnnotation::Kind::DimAligned;
        a.insX = 18.f;
        a.insY = 14.f;
        a.insZ = 3.f;
        a.dimExt1X = 16.f;
        a.dimExt1Y = 12.f;
        a.dimExt2X = 20.f;
        a.dimExt2Y = 12.f;
        a.dimSignedOffset = 2.f;
        a.plottedHeightInches = 0.1f;
        st.drawing.cadAnnotations.push_back(a);
        st.drawing.cadAnnotationAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Annotation);
      },
      [](const AppCommandState& st) { return st.drawing.cadAnnotations.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadAnnotation& a = st.drawing.cadAnnotations[i];
        return {{{a.insX, a.insY, a.insZ}, {a.dimExt1X, a.dimExt1Y, a.insZ}, {a.dimExt2X, a.dimExt2Y, a.insZ}},
                {a.dimSignedOffset, a.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // DimAligned rough bounds pad for text; verify the core box [16..20] x [12..14] is contained.
      {true, true, true, 16.0, 20.0, 12.0, 14.0, 3.0, 3.0},
  });

  // 6d. Annotation (DimLinear): horizontal dim ext1=(16,12), ext2=(20,12), offset=2, ins=(18,14,3).
  rows.push_back(KindRow{
      "AnnotationDimLinear",
      [](AppCommandState& st) {
        CadAnnotation a;
        a.kind = CadAnnotation::Kind::DimLinear;
        a.insX = 18.f;
        a.insY = 14.f;
        a.insZ = 3.f;
        a.dimExt1X = 16.f;
        a.dimExt1Y = 12.f;
        a.dimExt2X = 20.f;
        a.dimExt2Y = 12.f;
        a.dimSignedOffset = 2.f;
        a.dimLinearVertical = false;
        a.plottedHeightInches = 0.1f;
        st.drawing.cadAnnotations.push_back(a);
        st.drawing.cadAnnotationAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Annotation);
      },
      [](const AppCommandState& st) { return st.drawing.cadAnnotations.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadAnnotation& a = st.drawing.cadAnnotations[i];
        return {{{a.dimExt1X, a.dimExt1Y, a.insZ}, {a.dimExt2X, a.dimExt2Y, a.insZ}},
                {a.dimSignedOffset, a.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // Rotating horizontal DimLinear by -90 deg keeps dimLinearVertical=false and recomputes
      // dimSignedOffset = dmy - ncmy = (-18) - (-18) = 0 (characterises today's RotateCadDimLinearAroundBase).
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[0] = 0.0;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[0] = 0.0;
        return out;
      }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      {true, true, true, 16.0, 20.0, 12.0, 14.0, 3.0, 3.0},
  });

  // 7. PdfUnderlay: insert=(16,12), scale=1, rot=0, page=4x3.
  rows.push_back(KindRow{
      "PdfUnderlay",
      [](AppCommandState& st) {
        PdfAttachment pdf;
        pdf.filePath = "test.pdf";
        pdf.insertX = 16.f;
        pdf.insertY = 12.f;
        pdf.scale = 1.f;
        pdf.rotationDeg = 0.f;
        pdf.pageWidthPts = 4.f;
        pdf.pageHeightPts = 3.f;
        st.drawing.pdfAttachments.push_back(pdf);
        SelectOne(st, SelectedEntity::Type::PdfUnderlay);
      },
      [](const AppCommandState& st) { return st.drawing.pdfAttachments.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const PdfAttachment& p = st.drawing.pdfAttachments[i];
        return {{{p.insertX, p.insertY, 0.0}}, {p.scale, p.rotationDeg}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Skipped(), // Gap: COPY silently skips PDF underlays.
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[1] = p.scalars[1] - 90.0;
        return out;
      }),
      Skipped(), // Gap: ROTATE-copy silently skips PDF underlays.
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        return out;
      }),
      Refused(), // DropMirrorUnsupportedFromSelection refuses PDF underlays.
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // Gap: PDF underlays are absent from ComputeWorldExtents / CollectEntityBoxes / ComputeWorldExtents3d.
      {false, false, false, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
  });

  // 8. FilledRegion: triangle (16,12,3), (20,12,3), (16,15,3). First vertex inside stretch box.
  rows.push_back(KindRow{
      "FilledRegion",
      [](AppCommandState& st) {
        CadFilledRegion fr;
        fr.vertsXyz = {16.0, 12.0, 3.0, 20.0, 12.0, 3.0, 16.0, 15.0, 3.0};
        fr.loopStart = {0};
        st.drawing.cadFilledRegions.push_back(fr);
        st.drawing.cadFilledRegionAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::FilledRegion);
      },
      [](const AppCommandState& st) { return st.drawing.cadFilledRegions.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        EntityProbe p;
        const auto& v = st.drawing.cadFilledRegions[i].vertsXyz;
        for (size_t k = 0; k + 2 < v.size(); k += 3)
          p.pts.push_back({v[k], v[k + 1], v[k + 2]});
        return p;
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Skipped(), // Gap: plain ROTATE silently skips filled regions.
      Skipped(), // Gap: ROTATE-copy silently skips filled regions.
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      Skipped(), // Gap: plan SCALE silently skips filled regions.
      Refused(), // DropMirrorUnsupportedFromSelection refuses filled regions.
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      {true, true, true, 16.0, 20.0, 12.0, 15.0, 3.0, 3.0},
  });

  // 9. Mesh: triangle (16,12,3), (20,12,3), (16,15,4). Reference-only geometry.
  rows.push_back(KindRow{
      "Mesh",
      [](AppCommandState& st) {
        auto m = std::make_shared<CadMesh>();
        m->vertsXyz = {16.f, 12.f, 3.f, 20.f, 12.f, 3.f, 16.f, 15.f, 4.f};
        m->indices = {0, 1, 2};
        st.drawing.cadMeshes.push_back(m);
        st.drawing.cadMeshAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Mesh);
      },
      [](const AppCommandState& st) { return st.drawing.cadMeshes.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        EntityProbe p;
        const auto& v = st.drawing.cadMeshes[i]->vertsXyz;
        for (size_t k = 0; k + 2 < v.size(); k += 3)
          p.pts.push_back({v[k], v[k + 1], v[k + 2]});
        return p;
      },
      Skipped(), // Gap: reference-only mesh silently skipped by MOVE/COPY/ROTATE/SCALE.
      Skipped(),
      Skipped(),
      Skipped(),
      Skipped(),
      Skipped(),
      Refused(),   // DropMirrorUnsupportedFromSelection refuses meshes.
      BoxMissed(), // Gap: ComputeSelectionFromRect does not select meshes.
      Outcome::Erased,
      {true, true, true, 16.0, 20.0, 12.0, 15.0, 3.0, 4.0},
  });

  // 10. FeatureLine: (10,12,3) -> (20,12,4). Start outside stretch box, end inside.
  rows.push_back(KindRow{
      "FeatureLine",
      [](AppCommandState& st) {
        st.drawing.featureLineOffsets = {0, 2};
        st.drawing.featureLineVerts = {10.0, 12.0, 3.0, 20.0, 12.0, 4.0};
        st.drawing.featureLineClosed = {0};
        st.drawing.featureLineElevPt = {0, 0};
        st.drawing.featureLineInfo.push_back(CadFeatureLineInfo{});
        st.drawing.featureLineAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::FeatureLine);
      },
      [](const AppCommandState& st) {
        return st.drawing.featureLineOffsets.size() > 1 ? st.drawing.featureLineOffsets.size() - 1 : 0;
      },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        EntityProbe p;
        const int v0 = st.drawing.featureLineOffsets[i];
        const int v1 = st.drawing.featureLineOffsets[i + 1];
        for (int vi = v0; vi < v1; ++vi) {
          const size_t b = static_cast<size_t>(vi) * 3;
          p.pts.push_back({st.drawing.featureLineVerts[b], st.drawing.featureLineVerts[b + 1],
                           st.drawing.featureLineVerts[b + 2]});
        }
        return p;
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // COPY uses AppendFeatureLineCopy which only maps XY (dz dropped — recorded here).
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Refused(), // Tilted-axis rotation refuses FeatureLine.
      Applied([](const EntityProbe& p) { return MapPts(p, MapScaleXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchPoint); }),
      Outcome::Erased,
      {true, true, true, 10.0, 20.0, 12.0, 12.0, 3.0, 4.0},
  });

  // 11. Surface (TIN): triangle (16,12,3), (20,12,3), (16,15,4). Derived geometry — refused by transforms.
  rows.push_back(KindRow{
      "Surface",
      [](AppCommandState& st) {
        CadSurface surf;
        surf.name = "EG";
        auto tin = std::make_shared<CadTin>();
        tin->vertsXyz = {16.0, 12.0, 3.0, 20.0, 12.0, 3.0, 16.0, 15.0, 4.0};
        tin->indices = {0, 1, 2};
        surf.tin = tin;
        st.drawing.cadSurfaces.push_back(surf);
        st.drawing.cadSurfaceAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Surface);
      },
      [](const AppCommandState& st) { return st.drawing.cadSurfaces.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        EntityProbe p;
        const auto& v = st.drawing.cadSurfaces[i].tin->vertsXyz;
        for (size_t k = 0; k + 2 < v.size(); k += 3)
          p.pts.push_back({v[k], v[k + 1], v[k + 2]});
        return p;
      },
      Refused(),
      Skipped(), // Gap: DuplicateCadSelectionTranslated does not call DropSurfacesFromSelectionForTransform.
      Refused(),
      Skipped(), // Gap: DuplicateCadSelectionRotated does not call DropSurfacesFromSelectionForTransform.
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Outcome::Erased,
      {true, true, true, 16.0, 20.0, 12.0, 15.0, 3.0, 4.0},
  });

  // 12. Table: ins=(16,12,3), width=4, height=2, rot=0.
  rows.push_back(KindRow{
      "Table",
      [](AppCommandState& st) {
        CadTable t;
        t.insX = 16.f;
        t.insY = 12.f;
        t.insZ = 3.f;
        t.width = 4.f;
        t.height = 2.f;
        t.rotationRad = 0.f;
        t.cols = 2;
        t.cells = {"A", "B"};
        st.drawing.cadTables.push_back(t);
        st.drawing.cadTableAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Table);
      },
      [](const AppCommandState& st) { return st.drawing.cadTables.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadTable& t = st.drawing.cadTables[i];
        return {{{t.insX, t.insY, t.insZ}},
                {t.width, t.height, t.rotationRad, t.localYFlipped ? 1.0 : 0.0}};
      },
      // Note: CadTableTranslate translates XY only (insZ unchanged in MOVE & COPY).
      Applied([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[2] = p.scalars[2] - kHalfPi;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[2] = p.scalars[2] - kHalfPi;
        return out;
      }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        // Reflecting table (rot=0, width along +X) across Y-axis flips localYFlipped and sets rot=pi.
        EntityProbe out = MapPts(p, MapMirrorX);
        out.scalars[2] = kPi;
        out.scalars[3] = 1.0;
        return out;
      }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // Table body extends down from insY: [16..20] x [10..12]; CollectEntityBoxes does not track table insZ (0..0).
      {true, true, true, 16.0, 20.0, 10.0, 12.0, 0.0, 0.0},
  });

  // 13. BlockRef: insertion (16,12,3), scale=(1,1,1), rot=0, def has line (0,0)->(4,2).
  rows.push_back(KindRow{
      "BlockRef",
      [](AppCommandState& st) {
        CadBlockDefinition def;
        def.name = "UNITBLK";
        def.content.pushLine(CadLineRecord{0.0, 0.0, 0.0, 4.0, 2.0, 0.0});
        def.content.userLineAttrs.push_back(EntityAttributes{});
        st.drawing.blockDefs.push_back(def);
        CadBlockRef ref;
        ref.defName = "UNITBLK";
        ref.xf.x = 16.f;
        ref.xf.y = 12.f;
        ref.xf.z = 3.f;
        ref.xf.sx = 1.f;
        ref.xf.sy = 1.f;
        ref.xf.sz = 1.f;
        ref.xf.rotZ = 0.f;
        st.drawing.cadBlockRefs.push_back(ref);
        st.drawing.cadBlockRefAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::BlockRef);
      },
      [](const AppCommandState& st) { return st.drawing.cadBlockRefs.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadBlockRef& r = st.drawing.cadBlockRefs[i];
        return {{{r.xf.x, r.xf.y, r.xf.z}}, {r.xf.sx, r.xf.sy, r.xf.sz, r.xf.rotZ}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[3] = p.scalars[3] - kHalfPi;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[3] = p.scalars[3] - kHalfPi;
        return out;
      }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        out.scalars[2] = p.scalars[2] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        // CadBlockMirror across the Y-axis negates sx and keeps sy=1, sz=1, rotZ=0.
        EntityProbe out = MapPts(p, MapMirrorX);
        out.scalars[0] = -1.0;
        out.scalars[1] = 1.0;
        out.scalars[2] = 1.0;
        out.scalars[3] = 0.0;
        return out;
      }),
      Skipped(), // Gap: STRETCH selects BlockRef via crossing box but ApplyStretchToSelection skips it.
      Outcome::Erased,
      {true, true, true, 16.0, 20.0, 12.0, 14.0, 3.0, 3.0},
  });

  // 14. Solid: 4x2x2 box centred in XY at (18,13), rising from z=3 to z=5 -> [16,20] x [12,14] x [3,5].
  rows.push_back(KindRow{
      "Solid",
      [](AppCommandState& st) {
        brep::Solid solid;
        brep::Problem why = brep::Problem::Ok;
        ucs::Ucs frame;
        frame.origin = {18.0, 13.0, 3.0};
        REQUIRE(brep::MakeBox(frame, 4.0, 2.0, 2.0, &solid, &why));
        st.drawing.cadSolids.push_back(std::make_shared<const brep::Solid>(std::move(solid)));
        st.drawing.cadSolidAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Solid);
      },
      [](const AppCommandState& st) { return st.drawing.cadSolids.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const brep::Bounds bb = brep::ComputeBounds(*st.drawing.cadSolids[i]);
        return {{bb.mn, bb.mx}, {}};
      },
      Applied([](const EntityProbe& p) { return MapAabb3d(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapAabb3d(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapAabb3d(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapAabb3d(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapAabb3d(p, MapRot3d); }),
      // Solids scale uniformly in all 3 axes even under plan SCALE (REQ-332).
      Applied([](const EntityProbe& p) { return MapAabb3d(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapAabb3d(p, MapMirrorX); }),
      Refused(), // DropSolidsFromSelectionForTransform refuses solids under STRETCH.
      Outcome::Erased,
      {true, true, true, 16.0, 20.0, 12.0, 14.0, 3.0, 5.0},
  });

  // 15. PipeRun: path (10,12,3) -> (20,12,4), nominalSize="4in" (OD = 4.5 in = 0.375 ft, rad = 0.1875 ft).
  rows.push_back(KindRow{
      "PipeRun",
      [](AppCommandState& st) {
        CadPipeRun run;
        run.name = "P1";
        run.nominalSize = "4in";
        run.vertsXyz = {10.0, 12.0, 3.0, 20.0, 12.0, 4.0};
        st.drawing.cadPipeRuns.push_back(run);
        st.drawing.cadPipeRunAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::PipeRun);
      },
      [](const AppCommandState& st) { return st.drawing.cadPipeRuns.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        EntityProbe p;
        const auto& v = st.drawing.cadPipeRuns[i].vertsXyz;
        for (size_t k = 0; k + 2 < v.size(); k += 3)
          p.pts.push_back({v[k], v[k + 1], v[k + 2]});
        return p;
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      // PipeRun scales its 3D route uniformly even in plan SCALE (REQ-351).
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      BoxMissed(), // Gap: ComputeSelectionFromRect skips cadPipeRuns.
      Outcome::Erased,
      // Gap: ComputeRobustWorldExtents (2D) misses PipeRun; ComputeWorldExtents3d (and zoomextents fallback) sees it.
      {false, true, true, 10.0 - 0.1875, 20.0 + 0.1875, 12.0 - 0.1875, 12.0 + 0.1875, 3.0 - 0.1875, 4.0 + 0.1875},
  });

  // 16. PointCloud: two resident points (16,12,3) and (20,14,4). Display-and-erase only.
  rows.push_back(KindRow{
      "PointCloud",
      [](AppCommandState& st) {
        auto pc = std::make_shared<CadPointCloud>();
        pc->sourcePath = "scan.e57";
        pc->pointsXyz = {16.0, 12.0, 3.0, 20.0, 14.0, 4.0};
        st.drawing.cadPointClouds.push_back(pc);
        st.drawing.cadPointCloudAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::PointCloud);
      },
      [](const AppCommandState& st) { return st.drawing.cadPointClouds.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        EntityProbe p;
        const auto& v = st.drawing.cadPointClouds[i]->pointsXyz;
        for (size_t k = 0; k + 2 < v.size(); k += 3)
          p.pts.push_back({v[k], v[k + 1], v[k + 2]});
        return p;
      },
      Skipped(), // Gap: display-only point cloud is silently skipped by all transforms.
      Skipped(),
      Skipped(),
      Skipped(),
      Skipped(),
      Skipped(),
      Skipped(),
      Skipped(),
      Outcome::Erased,
      {true, true, true, 16.0, 20.0, 12.0, 14.0, 3.0, 4.0},
  });

  // 17. SectionPlane: singular view-state clip plane (sectionPlaneSelected, never in st.selection).
  rows.push_back(KindRow{
      "SectionPlane",
      [](AppCommandState& st) {
        st.viewportSectionClip = true;
        st.sectionPlaneSelected = true;
        st.viewportSectionClipOffset = 12.0;
        st.selection.clear();
      },
      [](const AppCommandState& st) { return st.viewportSectionClip ? size_t{1} : size_t{0}; },
      [](const AppCommandState& st, size_t) -> EntityProbe {
        return {{}, {st.viewportSectionClipOffset}};
      },
      NotInSel(),
      NotInSel(),
      NotInSel(),
      NotInSel(),
      NotInSel(),
      NotInSel(),
      NotInSel(),
      BoxMissed(),
      Outcome::Erased,
      {false, false, false, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
  });

  // 18. PositionMarker: (16,12,3) with Mtext label box [17,20] x [12,14].
  rows.push_back(KindRow{
      "PositionMarker",
      [](AppCommandState& st) {
        CadPositionMarker m;
        m.x = 16.0;
        m.y = 12.0;
        m.z = 3.f;
        m.label.kind = CadAnnotation::Kind::Mtext;
        m.label.insX = 17.f;
        m.label.insY = 12.f;
        m.label.insZ = 3.f;
        m.label.boxMinX = 17.f;
        m.label.boxMinY = 12.f;
        m.label.boxMaxX = 20.f;
        m.label.boxMaxY = 14.f;
        m.label.text = "PM";
        st.drawing.cadPositionMarkers.push_back(m);
        st.drawing.cadPositionMarkerAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::PositionMarker);
      },
      [](const AppCommandState& st) { return st.drawing.cadPositionMarkers.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadPositionMarker& m = st.drawing.cadPositionMarkers[i];
        return {{{m.x, m.y, m.z}, {m.label.insX, m.label.insY, m.label.insZ}}, {}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Refused(),
      Skipped(), // Gap: DuplicateCadSelectionRotated does not call DropPositionMarkersFromSelection.
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Outcome::Erased,
      // Circle radius = 0.1 * 50 = 5.0 -> [11..21, 7..17] union label [17..20, 12..14] = [11..21, 7..17].
      {true, true, true, 11.0, 21.0, 7.0, 17.0, 3.0, 3.0},
  });

  // 19. Multileader: path (16,12,3)->(19,14,3) + label box [19,22] x [13,15].
  rows.push_back(KindRow{
      "Multileader",
      [](AppCommandState& st) {
        CadMultileader ml;
        ml.pathXyz = {16.f, 12.f, 3.f, 19.f, 14.f, 3.f};
        ml.label.kind = CadAnnotation::Kind::Mtext;
        ml.label.insX = 19.f;
        ml.label.insY = 13.f;
        ml.label.insZ = 3.f;
        ml.label.boxMinX = 19.f;
        ml.label.boxMinY = 13.f;
        ml.label.boxMaxX = 22.f;
        ml.label.boxMaxY = 15.f;
        ml.label.text = "ML";
        st.drawing.cadMultileaders.push_back(ml);
        st.drawing.cadMultileaderAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Multileader);
      },
      [](const AppCommandState& st) { return st.drawing.cadMultileaders.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadMultileader& ml = st.drawing.cadMultileaders[i];
        return {{{ml.pathXyz[0], ml.pathXyz[1], ml.pathXyz[2]},
                 {ml.pathXyz[3], ml.pathXyz[4], ml.pathXyz[5]},
                 {ml.label.insX, ml.label.insY, ml.label.insZ}},
                {}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Refused(),
      Skipped(), // Gap: DuplicateCadSelectionRotated does not call DropMultileadersFromSelection.
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Outcome::Erased,
      {true, true, true, 16.0, 22.0, 12.0, 15.0, 3.0, 3.0},
  });

  // 20. Spline: degree-1 spline (10,12,3) -> (20,14,4). Start outside stretch box, end inside.
  rows.push_back(KindRow{
      "Spline",
      [](AppCommandState& st) {
        CadSpline sp;
        sp.degree = 1;
        sp.ctrl = {{10.0, 12.0, 3.0}, {20.0, 14.0, 4.0}};
        sp.knots = {0.0, 0.0, 1.0, 1.0};
        st.drawing.cadSplines.push_back(sp);
        st.drawing.cadSplineAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Spline);
      },
      [](const AppCommandState& st) { return st.drawing.cadSplines.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        return {st.drawing.cadSplines[i].ctrl, {}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapScaleXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Skipped(), // Gap: STRETCH selects Spline via crossing box but ApplyStretchToSelection skips it.
      Outcome::Erased,
      {true, true, true, 10.0, 20.0, 12.0, 14.0, 3.0, 4.0},
  });

  // 21. Point (CAD POINT): (16,12,3). Inside stretch box.
  rows.push_back(KindRow{
      "Point",
      [](AppCommandState& st) {
        st.drawing.cadPoints.push_back(CadPoint{16.0, 12.0, 3.0});
        st.drawing.cadPointAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Point);
      },
      [](const AppCommandState& st) { return st.drawing.cadPoints.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadPoint& pt = st.drawing.cadPoints[i];
        return {{{pt.x, pt.y, pt.z}}, {}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapScaleXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Skipped(), // Gap: STRETCH selects Point via crossing box but ApplyStretchToSelection skips it.
      Outcome::Erased,
      {true, true, true, 16.0, 16.0, 12.0, 12.0, 3.0, 3.0},
  });

  // 22. Alignment: horizontal tangent (10,12) -> (20,12) (azimuth 90 deg = due east, length 10).
  rows.push_back(KindRow{
      "Alignment",
      [](AppCommandState& st) {
        alignment::Alignment al;
        al.id = 1;
        al.name = "Road1";
        REQUIRE(al.AppendTangent({10.0, 12.0}, kHalfPi, 10.0));
        st.drawing.alignments.push_back(al);
        SelectOne(st, SelectedEntity::Type::Alignment);
      },
      [](const AppCommandState& st) { return st.drawing.alignments.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const auto pts = st.drawing.alignments[i].SampleForDisplay(0.5);
        REQUIRE(pts.size() >= 2);
        return {{{pts.front().x, pts.front().y, 0.0}, {pts.back().x, pts.back().y, 0.0}}, {}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Skipped(), // Gap: tilted-axis RotateSelectedAlignments returns early silently.
      Applied([](const EntityProbe& p) { return MapPts(p, MapScaleXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Skipped(), // Gap: STRETCH selects Alignment via crossing box but ApplyStretchToSelection skips it.
      Outcome::Erased,
      {true, true, true, 10.0, 20.0, 12.0, 12.0, 0.0, 0.0},
  });

  // 23. SurveyPoint: (16,12,3), selected via selectedSurveyPointIndices.
  rows.push_back(KindRow{
      "SurveyPoint",
      [](AppCommandState& st) {
        SurveyPoint sp;
        sp.id = 101;
        sp.easting = 16.0;
        sp.northing = 12.0;
        sp.elevation = 3.0;
        sp.description = "BM";
        st.drawing.surveyPoints.push_back(sp);
        st.selectedSurveyPointIndices = {0};
      },
      [](const AppCommandState& st) { return st.drawing.surveyPoints.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const SurveyPoint& sp = st.drawing.surveyPoints[i];
        return {{{sp.easting, sp.northing, sp.elevation}}, {}};
      },
      // MOVE translates XY only (elevation unchanged).
      Applied([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      // COPY opens survey duplicate modal; applying it translates XYZ.
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Refused(), // Tilted-axis ROTATE refuses survey points with an explicit message.
      Applied([](const EntityProbe& p) { return MapPts(p, MapScaleXy); }),
      // MIRROR in world XY opens survey duplicate modal; applying it reflects XY.
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      {true, true, true, 16.0, 16.0, 12.0, 12.0, 3.0, 3.0},
  });

  return rows;
}

void CheckTransformOp(const KindRow& row, const char* opName, const OpExpect& exp,
                      const std::function<void(AppCommandState&, std::vector<std::string>&)>& run) {
  AppCommandState st;
  std::vector<std::string> log;
  row.seed(st);
  REQUIRE(row.count(st) == 1);
  const EntityProbe before = row.probe(st, 0);

  run(st, log);
  if (st.copySurveyDupModalOpen)
    ApplyCopySurveyDuplicateModalResult(st, /*applySurveyDup=*/true, log);

  INFO("kind=" << row.name << " op=" << opName << " log=[" << JoinLog(log) << "]");
  switch (exp.outcome) {
  case Outcome::Applied:
    REQUIRE(row.count(st) == 1);
    REQUIRE(static_cast<bool>(exp.expectedGeom));
    CheckProbeNear(row.probe(st, 0), exp.expectedGeom(before), row.name, opName, "transformed");
    break;
  case Outcome::Duplicated:
    REQUIRE(row.count(st) == 2);
    REQUIRE(static_cast<bool>(exp.expectedGeom));
    CheckProbeNear(row.probe(st, 0), before, row.name, opName, "original");
    CheckProbeNear(row.probe(st, 1), exp.expectedGeom(before), row.name, opName, "copy");
    break;
  case Outcome::Refused:
    REQUIRE(row.count(st) == 1);
    CheckProbeNear(row.probe(st, 0), before, row.name, opName, "unchanged");
    CHECK(LogHasRefusal(log));
    break;
  case Outcome::Skipped:
    REQUIRE(row.count(st) == 1);
    CheckProbeNear(row.probe(st, 0), before, row.name, opName, "unchanged");
    CHECK_FALSE(LogHasRefusal(log));
    break;
  case Outcome::BoxMissed:
    REQUIRE(row.count(st) == 1);
    CheckProbeNear(row.probe(st, 0), before, row.name, opName, "unchanged");
    CHECK(st.selection.empty());
    CHECK(st.selectedSurveyPointIndices.empty());
    break;
  case Outcome::NotInSelection:
    REQUIRE(row.count(st) == 1);
    CheckProbeNear(row.probe(st, 0), before, row.name, opName, "unchanged");
    break;
  case Outcome::Erased:
    CHECK(row.count(st) == 0);
    break;
  }
}

void CheckExtentsOp(const KindRow& row) {
  AppCommandState st;
  std::vector<std::string> log;
  row.seed(st);
  const ExtentsExpect& ex = row.extents;

  // 1. 2D extents (ComputeRobustWorldExtents)
  double mnX = 0.0, mxX = 0.0, mnY = 0.0, mxY = 0.0;
  int skipped2d = 0;
  const bool got2d = ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped2d);
  {
    INFO("kind=" << row.name << " op=extents2d");
    CHECK(got2d == ex.in2d);
    if (got2d && ex.in2d) {
      // DimAligned/DimLinear rough bounds pad for text; check that the entity's core box is contained
      // and finite, while exact geometric kinds match within kGeomTol.
      const bool isDim = std::string(row.name) == "AnnotationDimAligned" ||
                         std::string(row.name) == "AnnotationDimLinear" ||
                         std::string(row.name) == "Alignment";
      if (isDim) {
        CHECK(std::isfinite(mnX));
        CHECK(std::isfinite(mxX));
        CHECK(std::isfinite(mnY));
        CHECK(std::isfinite(mxY));
        CHECK(mnX <= ex.mnX + kGeomTol);
        CHECK(mxX >= ex.mxX - kGeomTol);
        CHECK(mnY <= ex.mnY + kGeomTol);
        CHECK(mxY >= ex.mxY - kGeomTol);
        CHECK(mxX - mnX < 200.0);
        CHECK(mxY - mnY < 200.0);
      } else {
        CHECK(std::fabs(mnX - ex.mnX) <= kGeomTol);
        CHECK(std::fabs(mxX - ex.mxX) <= kGeomTol);
        CHECK(std::fabs(mnY - ex.mnY) <= kGeomTol);
        CHECK(std::fabs(mxY - ex.mxY) <= kGeomTol);
      }
    }
  }

  // 2. 3D extents (ComputeWorldExtents3d)
  ray3d::Vec3 bmn{}, bmx{};
  int skipped3d = 0;
  const bool got3d = ComputeWorldExtents3d(st, &bmn, &bmx, &skipped3d);
  {
    INFO("kind=" << row.name << " op=extents3d");
    CHECK(got3d == ex.in3d);
    if (got3d && ex.in3d) {
      CHECK(std::fabs(bmn.z - ex.mnZ) <= kGeomTol);
      CHECK(std::fabs(bmx.z - ex.mxZ) <= kGeomTol);
      // Note: CollectEntityBoxes uses conservative full-circle/bounding-radius XY boxes for Arc/Ellipse.
      const bool conservative3dXy = std::string(row.name) == "Arc" ||
                                    std::string(row.name) == "Ellipse" ||
                                    std::string(row.name) == "AnnotationDimAligned" ||
                                    std::string(row.name) == "AnnotationDimLinear" ||
                                    std::string(row.name) == "Alignment";
      if (conservative3dXy) {
        CHECK(std::isfinite(bmn.x));
        CHECK(std::isfinite(bmx.x));
        CHECK(std::isfinite(bmn.y));
        CHECK(std::isfinite(bmx.y));
        CHECK(bmn.x <= ex.mnX + kGeomTol);
        CHECK(bmx.x >= ex.mxX - kGeomTol);
        CHECK(bmn.y <= ex.mnY + kGeomTol);
        CHECK(bmx.y >= ex.mxY - kGeomTol);
        CHECK(bmx.x - bmn.x < 200.0);
        CHECK(bmx.y - bmn.y < 200.0);
      } else {
        CHECK(std::fabs(bmn.x - ex.mnX) <= kGeomTol);
        CHECK(std::fabs(bmx.x - ex.mxX) <= kGeomTol);
        CHECK(std::fabs(bmn.y - ex.mnY) <= kGeomTol);
        CHECK(std::fabs(bmx.y - ex.mxY) <= kGeomTol);
      }
    }
  }

  // 3. End-to-end ZOOMEXTENTS command via DispatchByPrimaryForTest + ProcessPendingViewportZoom.
  REQUIRE(DispatchByPrimaryForTest("zoomextents", st, log));
  double panX = 0.0, panY = 0.0;
  float zoom = 1.f;
  ProcessPendingViewportZoom(st, &panX, &panY, &zoom, 800, 600, 800.f / 600.f, log);
  const bool framed = std::any_of(log.begin(), log.end(), [](const std::string& s) {
    return s.find("Zoom extents applied") != std::string::npos;
  });
  INFO("kind=" << row.name << " op=zoomextents log=[" << JoinLog(log) << "]");
  CHECK(framed == ex.zoomExtentsFrames);
}

// Exhaustiveness guard: every SelectedEntity::Type enumerator is accounted for in BuildTable().
constexpr size_t KnownSelectedEntityTypeCount() {
  using T = SelectedEntity::Type;
  constexpr T kAll[] = {
      T::LineSeg,    T::Circle,        T::Annotation, T::Polyline,     T::Arc,
      T::Ellipse,    T::PdfUnderlay,   T::FilledRegion, T::Mesh,       T::FeatureLine,
      T::Surface,    T::Table,         T::BlockRef,   T::Solid,        T::PipeRun,
      T::PointCloud, T::SectionPlane,  T::PositionMarker, T::Multileader, T::Spline,
      T::Point,      T::Alignment,
  };
  size_t count = 0;
  for (T t : kAll) {
    switch (t) {
    case T::LineSeg:
    case T::Circle:
    case T::Annotation:
    case T::Polyline:
    case T::Arc:
    case T::Ellipse:
    case T::PdfUnderlay:
    case T::FilledRegion:
    case T::Mesh:
    case T::FeatureLine:
    case T::Surface:
    case T::Table:
    case T::BlockRef:
    case T::Solid:
    case T::PipeRun:
    case T::PointCloud:
    case T::SectionPlane:
    case T::PositionMarker:
    case T::Multileader:
    case T::Spline:
    case T::Point:
    case T::Alignment:
      ++count;
      break;
    }
  }
  return count;
}

static_assert(KnownSelectedEntityTypeCount() == 22,
              "Update tests/EntityKindOpsTests.cpp when adding a SelectedEntity::Type.");

} // namespace

TEST_CASE("EntityKindOps phase 0: every entity kind across MOVE, COPY, ROTATE, 3D ROTATE, SCALE, MIRROR, STRETCH, ERASE and extents",
          "[entity-kind-ops][phase0]") {
  const std::vector<KindRow> table = BuildTable();
  // 22 SelectedEntity::Type kinds (with Annotation split into 4 sub-kinds) + SurveyPoint = 26 rows.
  REQUIRE(table.size() == KnownSelectedEntityTypeCount() + 3 + 1);

  for (const KindRow& row : table) {
    CheckTransformOp(row, "MOVE", row.move, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("move", st, log));
      SubmitLine(st, "0,0", log);
      SubmitLine(st, "10,20,5", log);
      CancelActiveCommand(st, log);
    });

    CheckTransformOp(row, "COPY", row.copy, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("copy", st, log));
      SubmitLine(st, "0,0", log);
      SubmitLine(st, "10,20,5", log);
      if (st.copySurveyDupModalOpen)
        ApplyCopySurveyDuplicateModalResult(st, /*applySurveyDup=*/true, log);
      CancelActiveCommand(st, log);
    });

    CheckTransformOp(row, "ROTATE", row.rotatePlan, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("rotate", st, log));
      SubmitLine(st, "0,0", log);
      SubmitLine(st, "90", log);
      CancelActiveCommand(st, log);
    });

    CheckTransformOp(row, "ROTATE-copy", row.rotateCopy, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("rotate", st, log));
      SubmitLine(st, "0,0", log);
      SubmitLine(st, "c", log);
      SubmitLine(st, "90", log);
      CancelActiveCommand(st, log);
    });

    CheckTransformOp(row, "ROTATE-3D", row.rotate3d, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("ucs", st, log));
      SubmitLine(st, "x", log);
      SubmitLine(st, "90", log);
      log.clear(); // keep only ROTATE's own log for refusal checks
      REQUIRE(DispatchByPrimaryForTest("rotate", st, log));
      SubmitLine(st, "0,0", log);
      SubmitLine(st, "90", log);
      CancelActiveCommand(st, log);
    });

    CheckTransformOp(row, "SCALE", row.scale, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("scale", st, log));
      SubmitLine(st, "0,0", log);
      SubmitLine(st, "2", log);
      CancelActiveCommand(st, log);
    });

    CheckTransformOp(row, "MIRROR", row.mirror, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("mirror", st, log));
      SubmitLine(st, "0,0", log);
      SubmitLine(st, "0,10", log);
      SubmitLine(st, "n", log);
      CancelActiveCommand(st, log);
    });

    CheckTransformOp(row, "STRETCH", row.stretch, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("stretch", st, log));
      st.selBoxWaitingSecond = true;
      st.selBoxAnchorX = static_cast<float>(kStretchBoxMaxX);
      st.selBoxAnchorY = static_cast<float>(kStretchBoxMaxY);
      st.selBoxAnchorZ = 0.f;
      // Right-to-left crossing box [14, 25] x [5, 25].
      SubmitViewportPick(st, kStretchBoxMinX, kStretchBoxMinY, log, /*subtract=*/false, /*windowMode=*/false);
      if (!st.selection.empty() || !st.selectedSurveyPointIndices.empty()) {
        SubmitLine(st, "0,0", log);
        SubmitLine(st, "5,3", log);
      }
      CancelActiveCommand(st, log);
    });

    CheckTransformOp(row, "ERASE", {row.erase, {}}, [](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("delete", st, log));
    });

    CheckExtentsOp(row);
  }
}
