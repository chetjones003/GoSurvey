// Entity-kind operations (ADR-0011 / issue #1577 phase 0): table-driven characterisation test.
//
// Puts one entity of every kind through MOVE, COPY, ROTATE (plan), ROTATE (copy), ROTATE (3D axis
// via a tilted UCS), SCALE, MIRROR, STRETCH, ERASE, and extents via `DispatchByPrimaryForTest`,
// recording today's exact behaviour — including every kind-coverage gap and silent skip.
//
// Issue #1577 PR 2 moved every transform command onto one path and set each row to AutoCAD 2025's
// behaviour (ADR-0011 amendment). Rows marked "AutoCAD:" quote tools/autocad-transform-oracle/.
// SCALE is uniform in X, Y and Z about the base point, as AutoCAD's is.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "HatchGeom.hpp"
#include <GoSurvey/Document/cadpiperun.hpp>
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

// MTEXT under a similarity transform (issue #1667). The fixture's insertion is its bottom-left attachment
// point (mtextAttach 7). The insertion maps through \p fn, rotationRad grows by \p addRotation, the frame
// width/height scale by \p sizeScale, and the box is rebuilt around the insertion in the text's own frame:
// (boxMinX, boxMaxY) is the frame's top-left corner, the spans are the frame width and height.
EntityProbe MapMtextFrame(const EntityProbe& src, const std::function<ray3d::Vec3(const ray3d::Vec3&)>& fn,
                          double addRotation, double sizeScale) {
  EntityProbe out = src;
  // pts[0] = ins, pts[1] = boxMin, pts[2] = boxMax; scalars[0] = rotationRad
  const double w = (src.pts[2].x - src.pts[1].x) * sizeScale, h = (src.pts[2].y - src.pts[1].y) * sizeScale;
  const ray3d::Vec3 ins = fn(src.pts[0]);
  const double rotation = src.scalars[0] + addRotation;
  const double left = ins.x - h * std::sin(rotation), top = ins.y + h * std::cos(rotation);
  out.pts[0] = ins;
  out.pts[1] = {left, top - h, ins.z};
  out.pts[2] = {left + w, top, ins.z};
  out.scalars[0] = rotation;
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

struct Box2d {
  double mnX = 0.0, mxX = 0.0, mnY = 0.0, mxY = 0.0;
};

struct Box3d {
  double mnX = 0.0, mxX = 0.0, mnY = 0.0, mxY = 0.0, mnZ = 0.0, mxZ = 0.0;
};

struct ExtentsExpect {
  bool in2d = false;
  bool in3d = false;
  bool zoomExtentsFrames = false;
  Box2d box2d;
  Box3d box3d;
};

ExtentsExpect ExactExtents(double mnX, double mxX, double mnY, double mxY, double mnZ, double mxZ) {
  return {true, true, true, {mnX, mxX, mnY, mxY}, {mnX, mxX, mnY, mxY, mnZ, mxZ}};
}

ExtentsExpect NoExtents() { return {false, false, false, {}, {}}; }

struct KindRow {
  const char* name = "";
  std::optional<SelectedEntity::Type> entityType;
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
// Exhaustive compile-time + runtime guard for SelectedEntity::Type
// ---------------------------------------------------------------------------
// Enforces a hard compiler error (MSVC C4062 / GCC+Clang -Wswitch) whenever a new enumerator is
// added to SelectedEntity::Type without adding a case here. The test then enumerates every
// uint8_t value accepted by this switch (well-defined because SelectedEntity::Type has fixed
// underlying type std::uint8_t) and requires BuildTable() to contain every single one, with no
// hardcoded count that can drift out of sync.

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(default : 4062)
#pragma warning(error : 4062)
#elif defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#endif

constexpr bool IsKnownSelectedEntityType(SelectedEntity::Type t) {
  using T = SelectedEntity::Type;
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
    return true;
  }
  return false;
}

#ifdef _MSC_VER
#pragma warning(pop)
#elif defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// ---------------------------------------------------------------------------
// Seed + probe helpers for each entity kind
// ---------------------------------------------------------------------------

void SelectOne(AppCommandState& st, SelectedEntity::Type type) {
  st.selection.clear();
  st.selection.push_back({type, 0});
}

// Arc probe: centre, start point in world, end point in world; scalars: r, sweepRad, nx, ny, nz.
EntityProbe ProbeArc(const CadArc& a) {
  const ray3d::Vec3 sp = CurveWorldPointOnArc(a, static_cast<double>(a.startRad));
  const ray3d::Vec3 ep = CurveWorldPointOnArc(a, static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad));
  return {{ray3d::Vec3{a.cx, a.cy, a.z}, sp, ep}, {a.r, a.sweepRad, a.nx, a.ny, a.nz}};
}

EntityProbe ExpectArcMove(const EntityProbe& p) { return MapPts(p, MapMove3d); }
EntityProbe ExpectArcRotPlan(const EntityProbe& p) { return MapPts(p, MapRotPlan); }
EntityProbe ExpectArcRot3d(const EntityProbe& p) {
  EntityProbe out = MapPts(p, MapRot3d);
  // Normal (0,0,1) rotated +90 deg about +Y -> (1,0,0).
  out.scalars[2] = 1.0;
  out.scalars[3] = 0.0;
  out.scalars[4] = 0.0;
  return out;
}
EntityProbe ExpectArcScale(const EntityProbe& p) {
  EntityProbe out = MapPts(p, MapScale3d);
  out.scalars[0] = p.scalars[0] * kScaleFactor;
  return out;
}
EntityProbe ExpectArcMirror(const EntityProbe& p) {
  // Reflection reverses handedness: old end becomes new start, old start becomes new end; sweepRad preserved.
  EntityProbe out = p;
  out.pts[0] = MapMirrorX(p.pts[0]);
  out.pts[1] = MapMirrorX(p.pts[2]);
  out.pts[2] = MapMirrorX(p.pts[1]);
  return out;
}

// Multileader row after ROTATE "90" about the origin (see the row for why the label moves this way).
EntityProbe MultileaderRotated() {
  return {{{12.0, -16.0, 3.0}, {14.0, -19.0, 3.0}, {13.0, -19.0, 3.0}, {15.0, -21.0, 3.0}, {18.0, -19.0, 3.0}},
          {0.5, -kHalfPi, 0.1}};
}

std::vector<KindRow> BuildTable() {
  std::vector<KindRow> rows;

  // 1. LineSeg: (10,12,3) -> (20,12,4). Start outside stretch box, end inside.
  rows.push_back(KindRow{
      "LineSeg",
      SelectedEntity::Type::LineSeg,
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
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),  // AutoCAD: z 3 -> 6, 4 -> 8
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchPoint); }),
      Outcome::Erased,
      ExactExtents(10.0, 20.0, 12.0, 12.0, 3.0, 4.0),
  });

  // 2. Circle: centre (16,12,3), r=2, normal=(0,0,1). Centre inside stretch box.
  rows.push_back(KindRow{
      "Circle",
      SelectedEntity::Type::Circle,
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
      Applied([](const EntityProbe& p) {  // AutoCAD: centre (32,24,6), r 4
        EntityProbe out = MapPts(p, MapScale3d);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      ExactExtents(14.0, 18.0, 10.0, 14.0, 3.0, 3.0),
  });

  // 3. Arc: centre (10,12,3), r=6, start=0 (16,12,3 inside box), sweep=pi/2 (end=(10,18,3) outside).
  rows.push_back(KindRow{
      "Arc",
      SelectedEntity::Type::Arc,
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
      // 2D ComputeWorldExtents samples the swept arc [10..16, 12..18];
      // 3D ComputeWorldExtents3d (CollectEntityBoxes) uses the full-circle box [cx-r..cx+r, cy-r..cy+r].
      {true, true, true, {10.0, 16.0, 12.0, 18.0}, {4.0, 16.0, 6.0, 18.0, 3.0, 3.0}},
  });

  // 4. Ellipse: centre (16,12,3), major=(3,0), ratio=0.5 (minor=1.5), normal=(0,0,1), full turn.
  const double kEllRrx = std::hypot(3.0, 1.5);
  rows.push_back(KindRow{
      "Ellipse",
      SelectedEntity::Type::Ellipse,
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
        return {{{el.cx, el.cy, el.z}, {el.cx + el.majVx, el.cy + el.majVy, el.z}},
                {el.ratio, el.nx, el.ny, el.nz, el.startRad, el.sweepRad}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      // AutoCAD: ROTATE3D Y 90 -> centre (0,12,-16), major (0,0,-3), normal (1,0,0). The probe's second
      // point is centre + (majVx, majVy) read in the new plane, whose X axis (arbitrary-axis rule for
      // normal +X) is world +Y and Y axis world +Z: the major (0,0,-3) reads (0,-3).
      Applied([](const EntityProbe& p) {
        EntityProbe out = p;
        out.pts[0] = MapRot3d(p.pts[0]);
        out.pts[1] = {out.pts[0].x, out.pts[0].y - 3.0, out.pts[0].z};
        out.scalars[1] = 1.0;
        out.scalars[2] = 0.0;
        out.scalars[3] = 0.0;
        return out;
      }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = p;
        out.pts[0] = MapScale3d(p.pts[0]);
        out.pts[1] = {out.pts[0].x + 6.0, out.pts[0].y, out.pts[0].z};
        return out;
      }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // 2D ComputeWorldExtents samples the ellipse [13..19, 10.5..13.5];
      // 3D ComputeWorldExtents3d (CollectEntityBoxes) uses cx±hypot(ma,mb), cy±hypot(ma,mb).
      {true, true, true, {13.0, 19.0, 10.5, 13.5},
       {16.0 - kEllRrx, 16.0 + kEllRrx, 12.0 - kEllRrx, 12.0 + kEllRrx, 3.0, 3.0}},
  });

  // 5. Polyline: (10,12,3) -> (15,12,3) -> (20,14,4). First vertex outside stretch box, last two inside.
  // Probes vertices, per-vertex bulges, per-vertex normals, and closed flag so 3D rotation / copy /
  // mirror behaviour on polyline normals and bulges is frozen too (today normals stay (0,0,1)).
  rows.push_back(KindRow{
      "Polyline",
      SelectedEntity::Type::Polyline,
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
          const size_t k = static_cast<size_t>(vi);
          const CadPolyVertex v = st.drawing.vertex(k);
          p.pts.push_back({v.x, v.y, v.z});
          p.scalars.push_back(k < st.drawing.userPolylineVertsBulge.size()
                                  ? st.drawing.userPolylineVertsBulge[k]
                                  : 0.0);
          const size_t nk = k * 3;
          if (nk + 2 < st.drawing.userPolylineVertsNormal.size()) {
            p.scalars.push_back(st.drawing.userPolylineVertsNormal[nk]);
            p.scalars.push_back(st.drawing.userPolylineVertsNormal[nk + 1]);
            p.scalars.push_back(st.drawing.userPolylineVertsNormal[nk + 2]);
          } else {
            p.scalars.insert(p.scalars.end(), {0.0, 0.0, 1.0});
          }
        }
        p.scalars.push_back(i < st.drawing.userPolylineClosed.size()
                                ? static_cast<double>(st.drawing.userPolylineClosed[i])
                                : 0.0);
        return p;
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      // A straight polyline's vertex normals are never consulted, so a tilted rotation leaves them (0,0,1).
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchPoint); }),
      Outcome::Erased,
      ExactExtents(10.0, 20.0, 12.0, 14.0, 3.0, 4.0),
  });

  // 6a. Annotation (Text): ins=(16,12,3), plottedHeight=0.1, rot=0.
  rows.push_back(KindRow{
      "AnnotationText",
      SelectedEntity::Type::Annotation,
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
      Refused(), // No plane normal to tip (a known gap: AutoCAD ROTATE3D tilts TEXT).
      Applied([](const EntityProbe& p) {  // AutoCAD: insertion (32,24,6), height x2
        EntityProbe out = MapPts(p, MapScale3d);
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        return out;
      }),
      // AutoCAD (MIRRTEXT 0): the far end of the baseline mirrors and the text reads forwards from it.
      // The string is 5.5 wide here (0.55 x h 5 x 2 glyphs), so the insertion lands at -(16 + 5.5).
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = p;
        out.pts[0] = {-(p.pts[0].x + 5.5), p.pts[0].y, p.pts[0].z};
        return out;
      }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // Default modelUnitsPerPlottedInch = 50 -> h = 5.0, w = 0.55 * 5.0 * 2 = 5.5. The text stands on its
      // insertion (baseline) point, so it spans y = 12..17 (#1624: it was reported below the baseline).
      ExactExtents(16.0, 21.5, 12.0, 17.0, 3.0, 3.0),
  });

  // 6b. Annotation (Mtext): ins=(16,12,3), box=[16,20] x [12,15], rot=0, plottedHeight=0.1.
  rows.push_back(KindRow{
      "AnnotationMtext",
      SelectedEntity::Type::Annotation,
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
        a.rotationRad = 0.f;
        a.mtextAttach = 7;  // bottom-left: the insertion (16,12) is the box's bottom-left corner
        a.plottedHeightInches = 0.1f;
        a.text = "NOTE";
        st.drawing.cadAnnotations.push_back(a);
        st.drawing.cadAnnotationAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Annotation);
      },
      [](const AppCommandState& st) { return st.drawing.cadAnnotations.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadAnnotation& a = st.drawing.cadAnnotations[i];
        return {{{a.insX, a.insY, a.insZ}, {a.boxMinX, a.boxMinY, a.insZ}, {a.boxMaxX, a.boxMaxY, a.insZ}},
                {a.rotationRad, a.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // Issue #1667: every transform moves the insertion (attachment point) and rebuilds the frame around it.
      // ROTATE "90" turns it -90 deg; MIRROR keeps it upright (MIRRTEXT off, D-2026-08-23-j).
      Applied([](const EntityProbe& p) { return MapMtextFrame(p, MapRotPlan, -kHalfPi, 1.0); }),
      Duplicated([](const EntityProbe& p) { return MapMtextFrame(p, MapRotPlan, -kHalfPi, 1.0); }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapMtextFrame(p, MapScale3d, 0.0, kScaleFactor);
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        return out;
      }),
      // AutoCAD: the insertion mirrors and the attachment swaps sides (bottom-left becomes bottom-right),
      // so the frame lands on the mirror image of the old one, [-20,-16] x [12,15], reading forwards.
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = p;
        out.pts[0] = MapMirrorX(p.pts[0]);
        out.pts[1] = {-20.0, 12.0, p.pts[0].z};
        out.pts[2] = {-16.0, 15.0, p.pts[0].z};
        return out;
      }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      ExactExtents(16.0, 20.0, 12.0, 15.0, 3.0, 3.0),
  });

  // 6c. Annotation (DimAligned): ext1=(16,12), ext2=(20,12), offset=2, ins=(18,14,3), rot=0.
  rows.push_back(KindRow{
      "AnnotationDimAligned",
      SelectedEntity::Type::Annotation,
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
        a.rotationRad = 0.f;
        a.plottedHeightInches = 0.1f;
        st.drawing.cadAnnotations.push_back(a);
        st.drawing.cadAnnotationAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Annotation);
      },
      [](const AppCommandState& st) { return st.drawing.cadAnnotations.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadAnnotation& a = st.drawing.cadAnnotations[i];
        return {{{a.insX, a.insY, a.insZ}, {a.dimExt1X, a.dimExt1Y, a.insZ}, {a.dimExt2X, a.dimExt2Y, a.insZ}},
                {a.dimSignedOffset, a.rotationRad, a.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        // Rotated chord points along (0,-1) -> atan2(-1,0) = -pi/2 -> CadDimReadableTextRotation = +pi/2.
        out.scalars[1] = kHalfPi;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[1] = kHalfPi;
        return out;
      }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScale3d);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        out.scalars[2] = p.scalars[2] * kScaleFactor;
        return out;
      }),
      // The mirrored chord runs (-16,12) -> (-20,12), so its normal points down: the offset changes sign
      // to keep the dimension line at y = 14, where the mirror puts it.
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapMirrorX);
        out.scalars[0] = -p.scalars[0];
        return out;
      }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // Exact CadAnnotationRoughBounds: ext lines [16..20] x [12.048..14.08] union text quad [18..20.75] x [9..14].
      ExactExtents(16.0, 20.75, 9.0, 14.08, 3.0, 3.0),
  });

  // 6d. Annotation (DimLinear): horizontal dim ext1=(16,12), ext2=(20,12), offset=2, ins=(18,14,3), rot=0.
  rows.push_back(KindRow{
      "AnnotationDimLinear",
      SelectedEntity::Type::Annotation,
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
        a.rotationRad = 0.f;
        a.plottedHeightInches = 0.1f;
        st.drawing.cadAnnotations.push_back(a);
        st.drawing.cadAnnotationAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::Annotation);
      },
      [](const AppCommandState& st) { return st.drawing.cadAnnotations.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadAnnotation& a = st.drawing.cadAnnotations[i];
        return {{{a.insX, a.insY, a.insZ}, {a.dimExt1X, a.dimExt1Y, a.insZ}, {a.dimExt2X, a.dimExt2Y, a.insZ}},
                {a.dimSignedOffset, a.dimLinearVertical ? 1.0 : 0.0, a.rotationRad, a.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // AutoCAD: ROTATE -90 turns the dimension to measure vertically (group 50 = 270 deg); its line
      // passes x = 14, so the offset from the chord at x = 12 stays 2 and the text reads at 90 deg.
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[1] = 1.0;
        out.scalars[2] = kHalfPi;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[1] = 1.0;
        out.scalars[2] = kHalfPi;
        return out;
      }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScale3d);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        out.scalars[3] = p.scalars[3] * kScaleFactor;
        return out;
      }),
      // AutoCAD: MIRROR moves the whole dimension, text included: (18,14) -> (-18,14).
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      ExactExtents(16.0, 20.75, 9.0, 14.08, 3.0, 3.0),
  });

  // 7. PdfUnderlay: insert=(16,12), scale=1, rot=0, page=4x3.
  rows.push_back(KindRow{
      "PdfUnderlay",
      SelectedEntity::Type::PdfUnderlay,
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
        return {{{p.insertX, p.insertY, 0.0}}, {p.scale, p.rotationDeg, p.pageWidthPts, p.pageHeightPts}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[1] = p.scalars[1] - 90.0;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[1] = p.scalars[1] - 90.0;
        return out;
      }),
      Refused(),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        return out;
      }),
      Refused(), // An underlay's placement cannot store a reflection yet (AutoCAD behaviour unconfirmed).
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // Gap: PDF underlays are absent from ComputeWorldExtents / CollectEntityBoxes / ComputeWorldExtents3d.
      NoExtents(),
  });

  // 8. FilledRegion: triangle (16,12,3), (20,12,3), (16,15,3). First vertex inside stretch box.
  rows.push_back(KindRow{
      "FilledRegion",
      SelectedEntity::Type::FilledRegion,
      [](AppCommandState& st) {
        CadFilledRegion fr;
        fr.vertsXyz = {16.0, 12.0, 3.0, 20.0, 12.0, 3.0, 16.0, 15.0, 3.0};
        fr.loopStart = {0};
        fr.patternAngleDeg = 15.f;
        fr.patternScale = 2.f;
        st.drawing.cadFilledRegions.push_back(fr);
        st.drawing.cadFilledRegionAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::FilledRegion);
      },
      [](const AppCommandState& st) { return st.drawing.cadFilledRegions.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        EntityProbe p;
        const CadFilledRegion& fr = st.drawing.cadFilledRegions[i];
        for (size_t k = 0; k + 2 < fr.vertsXyz.size(); k += 3)
          p.pts.push_back({fr.vertsXyz[k], fr.vertsXyz[k + 1], fr.vertsXyz[k + 2]});
        p.scalars = {fr.patternAngleDeg, fr.patternScale};
        return p;
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // AutoCAD: ROTATE -90 turns the boundary and the pattern (15 deg -> -75 deg).
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[0] = p.scalars[0] - 90.0;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[0] = p.scalars[0] - 90.0;
        return out;
      }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      // AutoCAD: SCALE 2 doubles the boundary and the pattern scale (2 -> 4); the angle stays 15 deg.
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScale3d);
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        return out;
      }),
      // AutoCAD (MIRRHATCH 0): the boundary mirrors and the pattern keeps its 15 deg angle.
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      ExactExtents(16.0, 20.0, 12.0, 15.0, 3.0, 3.0),
  });

  // 9. Mesh: triangle (16,12,3), (20,12,3), (16,15,4). Reference-only geometry.
  rows.push_back(KindRow{
      "Mesh",
      SelectedEntity::Type::Mesh,
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
      // An imported model transforms like any AutoCAD mesh: its vertices map (the payload is replaced).
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      BoxMissed(), // Gap: ComputeSelectionFromRect does not select meshes.
      Outcome::Erased,
      ExactExtents(16.0, 20.0, 12.0, 15.0, 3.0, 4.0),
  });

  // 10. FeatureLine: (10,12,3) -> (20,12,4). Start outside stretch box, end inside.
  rows.push_back(KindRow{
      "FeatureLine",
      SelectedEntity::Type::FeatureLine,
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
        p.scalars.push_back(i < st.drawing.featureLineClosed.size()
                                ? static_cast<double>(st.drawing.featureLineClosed[i])
                                : 0.0);
        return p;
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // COPY carries the elevation change, as MOVE does.
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Refused(), // Tilted-axis rotation refuses FeatureLine.
      // SCALE is uniform for a feature line too, so it stays on the CAD linework it was drawn from.
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchPoint); }),
      Outcome::Erased,
      ExactExtents(10.0, 20.0, 12.0, 12.0, 3.0, 4.0),
  });

  // 11. Surface (TIN): triangle (16,12,3), (20,12,3), (16,15,4). Derived geometry — refused by transforms.
  rows.push_back(KindRow{
      "Surface",
      SelectedEntity::Type::Surface,
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
      Refused(), // COPY refuses a surface with the same stated reason as MOVE (REQ-201).
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Outcome::Erased,
      ExactExtents(16.0, 20.0, 12.0, 15.0, 3.0, 4.0),
  });

  // 12. Table: ins=(16,12,3), width=4, height=2, rot=0, plottedHeight=0.125.
  rows.push_back(KindRow{
      "Table",
      SelectedEntity::Type::Table,
      [](AppCommandState& st) {
        CadTable t;
        t.insX = 16.f;
        t.insY = 12.f;
        t.insZ = 3.f;
        t.width = 4.f;
        t.height = 2.f;
        t.rotationRad = 0.f;
        t.plottedHeightInches = 0.125f;
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
                {t.width, t.height, t.rotationRad, t.localYFlipped ? 1.0 : 0.0, t.plottedHeightInches}};
      },
      // MOVE and COPY carry the table's elevation, as AutoCAD's do.
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
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
        EntityProbe out = MapPts(p, MapScale3d);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        out.scalars[4] = p.scalars[4] * kScaleFactor;
        return out;
      }),
      // AutoCAD: MIRROR keeps the table readable. The insertion (top-left) goes to the mirror image of
      // the top-right corner, (-(16 + 4), 12), and the table still runs along +X.
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = p;
        out.pts[0] = {-(p.pts[0].x + 4.0), p.pts[0].y, p.pts[0].z};
        return out;
      }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      // Table body extends down from insY: [16..20] x [10..12]; CollectEntityBoxes does not track table insZ (0..0).
      ExactExtents(16.0, 20.0, 10.0, 12.0, 0.0, 0.0),
  });

  // 13. BlockRef: insertion (16,12,3), scale=(1,1,1), rot=(0,0,0), def has line (0,0)->(4,2).
  rows.push_back(KindRow{
      "BlockRef",
      SelectedEntity::Type::BlockRef,
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
        ref.xf.rotX = 0.f;
        ref.xf.rotY = 0.f;
        ref.xf.rotZ = 0.f;
        st.drawing.cadBlockRefs.push_back(ref);
        st.drawing.cadBlockRefAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::BlockRef);
      },
      [](const AppCommandState& st) { return st.drawing.cadBlockRefs.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadBlockRef& r = st.drawing.cadBlockRefs[i];
        return {{{r.xf.x, r.xf.y, r.xf.z}}, {r.xf.sx, r.xf.sy, r.xf.sz, r.xf.rotX, r.xf.rotY, r.xf.rotZ}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[5] = p.scalars[5] - kHalfPi;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRotPlan);
        out.scalars[5] = p.scalars[5] - kHalfPi;
        return out;
      }),
      // AutoCAD: ROTATE3D tilts the insert. About +Y by 90 deg that is rotY = 90 deg.
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapRot3d);
        out.scalars[4] = kHalfPi;
        return out;
      }),
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScale3d);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        out.scalars[1] = p.scalars[1] * kScaleFactor;
        out.scalars[2] = p.scalars[2] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) {
        // AutoCAD: a mirrored insert gets X scale -1; across the Y axis at rotation 0 the rotation stays 0.
        EntityProbe out = MapPts(p, MapMirrorX);
        out.scalars[0] = -1.0;
        out.scalars[1] = 1.0;
        out.scalars[2] = 1.0;
        out.scalars[5] = 0.0;
        return out;
      }),
      // AutoCAD: an insert whose insertion point is inside the window moves whole (oracle
      // insert-stretch-in: (16,12) -> (21,15); insert-stretch-out at (13.5,12) stays).
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      ExactExtents(16.0, 20.0, 12.0, 14.0, 3.0, 3.0),
  });

  // 14. Solid: 4x2x2 box centred in XY at (18,13), rising from z=3 to z=5 -> [16,20] x [12,14] x [3,5].
  rows.push_back(KindRow{
      "Solid",
      SelectedEntity::Type::Solid,
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
        const brep::Solid& s = *st.drawing.cadSolids[i];
        const brep::Bounds bb = brep::ComputeBounds(s);
        return {{bb.mn, bb.mx}, {static_cast<double>(s.vertices.size()), static_cast<double>(s.faces.size())}};
      },
      Applied([](const EntityProbe& p) { return MapAabb3d(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapAabb3d(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapAabb3d(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapAabb3d(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapAabb3d(p, MapRot3d); }),
      // Solids scale uniformly in all 3 axes even under plan SCALE (REQ-332).
      Applied([](const EntityProbe& p) { return MapAabb3d(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapAabb3d(p, MapMirrorX); }),
      Refused(), // A solid can only be moved whole (its declared capability).
      Outcome::Erased,
      ExactExtents(16.0, 20.0, 12.0, 14.0, 3.0, 5.0),
  });

  // 15. PipeRun: path (10,12,3) -> (20,12,4), nominalSize="4in" (OD = 4.5 in = 0.375 ft, rad = 0.1875 ft).
  rows.push_back(KindRow{
      "PipeRun",
      SelectedEntity::Type::PipeRun,
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
        const CadPipeRun& r = st.drawing.cadPipeRuns[i];
        for (size_t k = 0; k + 2 < r.vertsXyz.size(); k += 3)
          p.pts.push_back({r.vertsXyz[k], r.vertsXyz[k + 1], r.vertsXyz[k + 2]});
        double odFeet = 0.0;
        (void)CadPipeNominalOdFeet(r.nominalSize, &odFeet);
        p.scalars = {odFeet};
        return p;
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      // PipeRun scales its 3D route uniformly even in plan SCALE, leaving nominal pipe OD unchanged (REQ-351).
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      BoxMissed(), // Gap: ComputeSelectionFromRect skips cadPipeRuns.
      Outcome::Erased,
      // Gap: ComputeRobustWorldExtents (2D) misses PipeRun; ComputeWorldExtents3d (and zoomextents fallback) sees it.
      {false, true, true, {},
       {10.0 - 0.1875, 20.0 + 0.1875, 12.0 - 0.1875, 12.0 + 0.1875, 3.0 - 0.1875, 4.0 + 0.1875}},
  });

  // 16. PointCloud: two resident points (16,12,3) and (20,14,4). Display-and-erase only.
  rows.push_back(KindRow{
      "PointCloud",
      SelectedEntity::Type::PointCloud,
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
      // Refused with a stated reason rather than skipped (REQ-201). Transforming a cloud needs a placement
      // in the PointCloud system, a known gap against AutoCAD.
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Outcome::Erased,
      ExactExtents(16.0, 20.0, 12.0, 14.0, 3.0, 4.0),
  });

  // 17. SectionPlane: singular view-state clip plane (sectionPlaneSelected, never in st.selection).
  rows.push_back(KindRow{
      "SectionPlane",
      SelectedEntity::Type::SectionPlane,
      [](AppCommandState& st) {
        st.viewportSectionClip = true;
        st.sectionPlaneSelected = true;
        st.viewportSectionClipOffset = 12.0;
        st.viewportSectionClipFlip = false;
        st.selection.clear();
      },
      [](const AppCommandState& st) { return st.viewportSectionClip ? size_t{1} : size_t{0}; },
      [](const AppCommandState& st, size_t) -> EntityProbe {
        return {{}, {st.viewportSectionClipOffset, st.viewportSectionClipFlip ? 1.0 : 0.0}};
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
      NoExtents(),
  });

  // 18. PositionMarker: (16,12,3) with Mtext label box [17,20] x [12,14].
  rows.push_back(KindRow{
      "PositionMarker",
      SelectedEntity::Type::PositionMarker,
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
        m.label.rotationRad = 0.f;
        m.label.plottedHeightInches = 0.1f;
        m.label.text = "PM";
        st.drawing.cadPositionMarkers.push_back(m);
        st.drawing.cadPositionMarkerAttrs.push_back(EntityAttributes{});
        SelectOne(st, SelectedEntity::Type::PositionMarker);
      },
      [](const AppCommandState& st) { return st.drawing.cadPositionMarkers.size(); },
      [](const AppCommandState& st, size_t i) -> EntityProbe {
        const CadPositionMarker& m = st.drawing.cadPositionMarkers[i];
        return {{{m.x, m.y, m.z},
                 {m.label.insX, m.label.insY, m.label.insZ},
                 {m.label.boxMinX, m.label.boxMinY, m.label.insZ},
                 {m.label.boxMaxX, m.label.boxMaxY, m.label.insZ}},
                {m.label.rotationRad, m.label.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Refused(),
      Refused(), // Rotate-copy refuses it with the same reason as ROTATE (REQ-359).
      Refused(),
      Refused(),
      Refused(),
      Refused(),
      Outcome::Erased,
      // Circle radius = 0.1 * 50 = 5.0 -> [11..21, 7..17] union label [17..20, 12..14] = [11..21, 7..17].
      ExactExtents(11.0, 21.0, 7.0, 17.0, 3.0, 3.0),
  });

  // 19. Multileader: path (16,12,3)->(19,14,3) + label box [19,22] x [13,15].
  rows.push_back(KindRow{
      "Multileader",
      SelectedEntity::Type::Multileader,
      [](AppCommandState& st) {
        CadMultileader ml;
        ml.pathXyz = {16.f, 12.f, 3.f, 19.f, 14.f, 3.f};
        ml.arrowSizeWorld = 0.5f;
        ml.label.kind = CadAnnotation::Kind::Mtext;
        ml.label.mtextAttach = 7;  // bottom-left: the insertion (19,13) is the box's bottom-left corner
        ml.label.insX = 19.f;
        ml.label.insY = 13.f;
        ml.label.insZ = 3.f;
        ml.label.boxMinX = 19.f;
        ml.label.boxMinY = 13.f;
        ml.label.boxMaxX = 22.f;
        ml.label.boxMaxY = 15.f;
        ml.label.rotationRad = 0.f;
        ml.label.plottedHeightInches = 0.1f;
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
                 {ml.label.insX, ml.label.insY, ml.label.insZ},
                 {ml.label.boxMinX, ml.label.boxMinY, ml.label.insZ},
                 {ml.label.boxMaxX, ml.label.boxMaxY, ml.label.insZ}},
                {ml.arrowSizeWorld, ml.label.rotationRad, ml.label.plottedHeightInches}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // AutoCAD: ROTATE turns the leader and its label: the insertion (19,13) turns to (13,-19).
      Applied([](const EntityProbe&) { return MultileaderRotated(); }),
      Duplicated([](const EntityProbe&) { return MultileaderRotated(); }),
      Refused(), // No plane normal to tip (a known gap: AutoCAD ROTATE3D tilts a multileader).
      // AutoCAD: SCALE doubles the leader, the arrowhead and the text.
      Applied([](const EntityProbe&) {
        return EntityProbe{{{32.0, 24.0, 6.0}, {38.0, 28.0, 6.0}, {38.0, 26.0, 6.0}, {38.0, 26.0, 6.0}, {44.0, 30.0, 6.0}},
                           {1.0, 0.0, 0.2}};
      }),
      // AutoCAD: MIRROR keeps the label readable on the other side of the landing (attachment BL -> BR).
      Duplicated([](const EntityProbe&) {
        return EntityProbe{{{-16.0, 12.0, 3.0}, {-19.0, 14.0, 3.0}, {-19.0, 13.0, 3.0}, {-22.0, 13.0, 3.0}, {-19.0, 15.0, 3.0}},
                           {0.5, 0.0, 0.1}};
      }),
      // AutoCAD: each leader point inside the window moves, and the label moves with the landing
      // (oracle mleader-stretch-all). Both points are inside here, so the whole leader moves.
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      ExactExtents(16.0, 22.0, 12.0, 15.0, 3.0, 3.0),
  });

  // 20. Spline: degree-1 spline (10,12,3) -> (20,14,4). Start outside stretch box, end inside.
  rows.push_back(KindRow{
      "Spline",
      SelectedEntity::Type::Spline,
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
        const CadSpline& sp = st.drawing.cadSplines[i];
        return {sp.ctrl, {static_cast<double>(sp.degree)}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRot3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      // AutoCAD: STRETCH moves the control points (a CV spline) or fit points inside the window
      // (oracle spline-stretch). Here (10,12,3) is outside and (20,14,4) inside.
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchPoint); }),
      Outcome::Erased,
      ExactExtents(10.0, 20.0, 12.0, 14.0, 3.0, 4.0),
  });

  // 21. Point (CAD POINT): (16,12,3). Inside stretch box.
  rows.push_back(KindRow{
      "Point",
      SelectedEntity::Type::Point,
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
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),  // AutoCAD: (16,12) -> (21,15)
      Outcome::Erased,
      ExactExtents(16.0, 16.0, 12.0, 12.0, 3.0, 3.0),
  });

  // 22. Alignment: horizontal tangent (10,12) -> (20,12) (azimuth 90 deg = due east, length 10).
  rows.push_back(KindRow{
      "Alignment",
      SelectedEntity::Type::Alignment,
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
        const alignment::Alignment& al = st.drawing.alignments[i];
        const auto pts = al.SampleForDisplay(0.5);
        REQUIRE(pts.size() >= 2);
        return {{{pts.front().x, pts.front().y, 0.0}, {pts.back().x, pts.back().y, 0.0}}, {al.Length()}};
      },
      Applied([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMoveXy); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Refused(), // An alignment is horizontal; a tilted axis is refused by name (REQ-201).
      Applied([](const EntityProbe& p) {
        EntityProbe out = MapPts(p, MapScaleXy);
        out.scalars[0] = p.scalars[0] * kScaleFactor;
        return out;
      }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Refused(), // STRETCH refuses an alignment by name; edit its PIs instead.
      Outcome::Erased,
      // Exact AlignmentLabelSets::LabelledBounds for (10,12)->(20,12) with default "Standard" label set at scale 50.
      ExactExtents(-35.0, 65.0, -33.0, 74.5, 0.0, 0.0),
  });

  // 23. SurveyPoint: (16,12,3), selected via selectedSurveyPointIndices.
  rows.push_back(KindRow{
      "SurveyPoint",
      std::nullopt,
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
      // MOVE carries the elevation change, as COPY's duplicate modal already did.
      Applied([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      // COPY opens survey duplicate modal; applying it translates XYZ.
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMove3d); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapRotPlan); }),
      Refused(), // Tilted-axis ROTATE refuses survey points with an explicit message.
      Applied([](const EntityProbe& p) { return MapPts(p, MapScale3d); }),  // uniform, like every kind
      // MIRROR in world XY opens survey duplicate modal; applying it reflects XY.
      Duplicated([](const EntityProbe& p) { return MapPts(p, MapMirrorX); }),
      Applied([](const EntityProbe& p) { return MapPts(p, MapStretchWhole); }),
      Outcome::Erased,
      ExactExtents(16.0, 16.0, 12.0, 12.0, 3.0, 3.0),
  });

  return rows;
}

void CheckTransformOp(const KindRow& row, const char* opName, const OpExpect& exp,
                      const std::function<void(AppCommandState&, std::vector<std::string>&)>& run) {
  AppCommandState st;
  std::vector<std::string> log;
  row.seed(st);
  REQUIRE(row.count(st) == 1);
  if (exp.outcome != Outcome::NotInSelection && std::string(opName) != "STRETCH" &&
      std::string(row.name) != "SectionPlane") {
    REQUIRE((st.selection.size() + st.selectedSurveyPointIndices.size()) == 1);
  }
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

  // 1. 2D extents (ComputeRobustWorldExtents) — exact baseline bounds within kGeomTol.
  double mnX = 0.0, mxX = 0.0, mnY = 0.0, mxY = 0.0;
  int skipped2d = 0;
  const bool got2d = ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped2d);
  {
    INFO("kind=" << row.name << " op=extents2d actual=(" << mnX << ".." << mxX << ", " << mnY << ".." << mxY
                 << ") expected=(" << ex.box2d.mnX << ".." << ex.box2d.mxX << ", " << ex.box2d.mnY << ".."
                 << ex.box2d.mxY << ")");
    CHECK(got2d == ex.in2d);
    if (got2d && ex.in2d) {
      CHECK(std::fabs(mnX - ex.box2d.mnX) <= kGeomTol);
      CHECK(std::fabs(mxX - ex.box2d.mxX) <= kGeomTol);
      CHECK(std::fabs(mnY - ex.box2d.mnY) <= kGeomTol);
      CHECK(std::fabs(mxY - ex.box2d.mxY) <= kGeomTol);
    }
  }

  // 2. 3D extents (ComputeWorldExtents3d) — exact baseline bounds within kGeomTol.
  ray3d::Vec3 bmn{}, bmx{};
  int skipped3d = 0;
  const bool got3d = ComputeWorldExtents3d(st, &bmn, &bmx, &skipped3d);
  {
    INFO("kind=" << row.name << " op=extents3d actual=(" << bmn.x << ".." << bmx.x << ", " << bmn.y << ".." << bmx.y
                 << ", " << bmn.z << ".." << bmx.z << ") expected=(" << ex.box3d.mnX << ".." << ex.box3d.mxX << ", "
                 << ex.box3d.mnY << ".." << ex.box3d.mxY << ", " << ex.box3d.mnZ << ".." << ex.box3d.mxZ << ")");
    CHECK(got3d == ex.in3d);
    if (got3d && ex.in3d) {
      CHECK(std::fabs(bmn.x - ex.box3d.mnX) <= kGeomTol);
      CHECK(std::fabs(bmx.x - ex.box3d.mxX) <= kGeomTol);
      CHECK(std::fabs(bmn.y - ex.box3d.mnY) <= kGeomTol);
      CHECK(std::fabs(bmx.y - ex.box3d.mxY) <= kGeomTol);
      CHECK(std::fabs(bmn.z - ex.box3d.mnZ) <= kGeomTol);
      CHECK(std::fabs(bmx.z - ex.box3d.mxZ) <= kGeomTol);
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

} // namespace

TEST_CASE("EntityKindOps phase 0: every entity kind across MOVE, COPY, ROTATE, 3D ROTATE, SCALE, MIRROR, STRETCH, ERASE and extents",
          "[entity-kind-ops][phase0]") {
  const std::vector<KindRow> table = BuildTable();

  // Verify every SelectedEntity::Type enumerator accepted by the compiler-enforced exhaustive
  // switch appears in BuildTable() (plus SurveyPoint, which is not a SelectedEntity::Type).
  std::set<SelectedEntity::Type> expectedTypes;
  for (int raw = 0; raw <= 255; ++raw) {
    const auto t = static_cast<SelectedEntity::Type>(raw);
    if (IsKnownSelectedEntityType(t))
      expectedTypes.insert(t);
  }
  std::set<SelectedEntity::Type> tableTypes;
  size_t nonEntityRows = 0;
  for (const KindRow& row : table) {
    if (row.entityType.has_value()) {
      REQUIRE(IsKnownSelectedEntityType(*row.entityType));
      tableTypes.insert(*row.entityType);
    } else {
      ++nonEntityRows;
    }
  }
  REQUIRE(tableTypes == expectedTypes);
  REQUIRE(nonEntityRows == 1); // SurveyPoint

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

    CheckTransformOp(row, "STRETCH", row.stretch, [&](AppCommandState& st, std::vector<std::string>& log) {
      REQUIRE(DispatchByPrimaryForTest("stretch", st, log));
      st.selBoxWaitingSecond = true;
      st.selBoxAnchorX = static_cast<float>(kStretchBoxMaxX);
      st.selBoxAnchorY = static_cast<float>(kStretchBoxMaxY);
      st.selBoxAnchorZ = 0.f;
      // Right-to-left crossing box [14, 25] x [5, 25].
      SubmitViewportPick(st, kStretchBoxMinX, kStretchBoxMinY, log, /*subtract=*/false, /*windowMode=*/false);
      // Assert crossing-selection outcome immediately before submitting the stretch vector, so
      // selected-but-refused kinds (Alignment, PointCloud) cannot silently
      // pass if crossing-box selection regresses.
      const size_t pickedCount = st.selection.size() + st.selectedSurveyPointIndices.size();
      if (row.stretch.outcome == Outcome::BoxMissed) {
        CHECK(pickedCount == 0);
      } else {
        INFO("kind=" << row.name << " op=STRETCH crossing-pick");
        REQUIRE(pickedCount == 1);
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
