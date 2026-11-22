#include "CadCommandsInternal.hpp"
#include "draw/CadDrawInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include "HatchGeom.hpp"
#include <GoSurvey/Geometry/HatchBoundary.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Geometry/spline.hpp>
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

bool ComputeCircumcircle(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                         float* r) {
  const double dax = static_cast<double>(ax);
  const double day = static_cast<double>(ay);
  const double dbx = static_cast<double>(bx);
  const double dby = static_cast<double>(by);
  const double dcx = static_cast<double>(cx);
  const double dcy = static_cast<double>(cy);
  const double d = 2.0 * (dax * (dby - dcy) + dbx * (dcy - day) + dcx * (day - dby));
  if (std::fabs(d) < 1e-6)
    return false;
  const double a2 = dax * dax + day * day;
  const double b2 = dbx * dbx + dby * dby;
  const double c2 = dcx * dcx + dcy * dcy;
  const double ux = (a2 * (dby - dcy) + b2 * (dcy - day) + c2 * (day - dby)) / d;
  const double uy = (a2 * (dcx - dbx) + b2 * (dax - dcx) + c2 * (dbx - dax)) / d;
  const double dx = ux - dax;
  const double dy = uy - day;
  *ox = static_cast<float>(ux);
  *oy = static_cast<float>(uy);
  *r = static_cast<float>(std::sqrt(dx * dx + dy * dy));
  return true;
}

/// Place measurement text on the far side of the dimension line from the measured chord (CAD "above" the dim line).

// WorkPlaneAnchoredAt and ActiveDrawPlaneNormal moved to CadCommands.hpp as
// CadWorkPlaneAnchoredAt / CadActiveDrawPlaneNormal (REQ-312). The rubber-band preview has to
// resolve the same plane the commit will, and it lives in another translation unit.

void CommitCircle(AppCommandState& st, float cx, float cy, float cz, float r, float nx, float ny, float nz,
                  std::vector<std::string>& log) {
  // The radius is DERIVED from the distance between two points the user supplied, so it can overflow
  // float while both of those points are perfectly representable: a centre at state-plane magnitude
  // and a picked point far from it make dx*dx + dy*dy infinite, and sqrt(inf) is inf. Issue #59,
  // REQ-204 ("no coordinate is NaN or infinite") and REQ-201 (the refusal is reported, not swallowed).
  //
  // This must come BEFORE the radius-too-small test, which cannot do the job: `inf < 1e-5f` is false
  // and every comparison against NaN is false, so both slipped straight through it into the store.
  if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(r)) {
    log.push_back("Circle rejected — the center or radius is not a finite number.");
    return;
  }
  // The plane gets the same treatment, and for the same reason: a normal that is NaN or zero-length
  // does not fail loudly, it makes `ucs::FromNormal` refuse and every consumer silently fall back to
  // a different plane than the one the user drew on (REQ-201).
  if (!std::isfinite(cz) || !std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) ||
      (nx * nx + ny * ny + nz * nz) < 1e-12f) {
    log.push_back("Circle rejected — the work plane is not a valid plane.");
    return;
  }
  if (r < 1e-5f) {
    log.push_back("Circle radius too small.");
    return;
  }
  PushUndoSnapshot(st, "Circle");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    // Paper-space CIRCLE (REQ-039): centre + radius are paper inches; commit to the layout's
    // paper store, matching the shape CommitPolylineDraft uses for POLYLINE (issue #84/#86).
    // A sheet is 2D (ADR-025 (g)), so the plane never reaches it.
    L->paperCircles.push_back(cx);
    L->paperCircles.push_back(cy);
    L->paperCircles.push_back(r);
    L->paperCircleAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    // A new circle lands on the active work plane (REQ-058) — the ELEV command moves it.
    st.drawing.pushCircle(CadCircleRecord{cx, cy, cz, r});
    st.drawing.userCircleAttrs.push_back(MakeNewEntityAttrs(st));
    PushCircleNormal(st.drawing.userCircleNormals, nx, ny, nz);
  }
  BumpCadGpuCache(st);
  ResetCircleDraft(st);
  st.active = AppCommandState::Kind::None;
  log.push_back("Circle complete.");
}



bool ParseRadiusOrDiameter(const std::string& raw, float* radiusOut, std::vector<std::string>& log) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  std::string low = StringUtil::toLowerAsciiCopy(s);
  if (!low.empty() && low[0] == 'd') {
    float dia = 0.f;
    if (!ParseOneFloat(low.substr(1), &dia)) {
      log.push_back("Expected diameter after D (e.g. D 40 or D40).");
      return false;
    }
    if (dia <= 0.f) {
      log.push_back("Diameter must be positive.");
      return false;
    }
    *radiusOut = dia * 0.5f;
    return true;
  }
  if (!ParseOneFloat(s, radiusOut)) {
    log.push_back("Expected a radius (number) or D + diameter.");
    return false;
  }
  if (*radiusOut <= 0.f) {
    log.push_back("Radius must be positive.");
    return false;
  }
  return true;
}

void ExecuteCenterMarkFromSelection(AppCommandState& st, std::vector<std::string>& log) {
  struct Mark { double x, y, z, radius; };
  std::vector<Mark> marks;
  for (const SelectedEntity& entity : st.selection) {
    if (entity.type == SelectedEntity::Type::Circle && entity.index >= 0 &&
        static_cast<std::size_t>(entity.index) < st.drawing.circleCount()) {
      const CadCircleRecord c = st.drawing.circle(static_cast<std::size_t>(entity.index));
      marks.push_back({c.cx, c.cy, c.cz, c.r});
    } else if (entity.type == SelectedEntity::Type::Arc && entity.index >= 0 &&
               static_cast<std::size_t>(entity.index) < st.drawing.userArcs.size()) {
      const CadArc& arc = st.drawing.userArcs[static_cast<std::size_t>(entity.index)];
      marks.push_back({arc.cx, arc.cy, arc.z, arc.r});
    }
  }
  if (marks.empty()) { log.push_back("CENTERMARK — select one or more circles/arcs first, then run CENTERMARK."); return; }
  PushUndoSnapshot(st, "CENTERMARK");
  for (const Mark& mark : marks) {
    const double half = std::max(mark.radius * 0.18, 0.01);
    const double gap = half * 0.16;
    st.drawing.pushLine(CadLineRecord{mark.x - half, mark.y, mark.z, mark.x - gap, mark.y, mark.z});
    st.drawing.pushLine(CadLineRecord{mark.x + gap, mark.y, mark.z, mark.x + half, mark.y, mark.z});
    st.drawing.pushLine(CadLineRecord{mark.x, mark.y - half, mark.z, mark.x, mark.y - gap, mark.z});
    st.drawing.pushLine(CadLineRecord{mark.x, mark.y + gap, mark.z, mark.x, mark.y + half, mark.z});
    for (int i = 0; i < 4; ++i) st.drawing.userLineAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  log.push_back("CENTERMARK — center marks created for " + std::to_string(marks.size()) + " selected circle/arc(s).");
}

void ExecuteCenterlineFromSelection(AppCommandState& st, std::vector<std::string>& log) {
  std::vector<const SelectedEntity*> lines;
  for (const SelectedEntity& entity : st.selection)
    if (entity.type == SelectedEntity::Type::LineSeg && entity.index >= 0 &&
        static_cast<std::size_t>(entity.index) < st.drawing.lineCount()) lines.push_back(&entity);
  if (lines.size() != 2) { log.push_back("CENTERLINE — select exactly two lines first, then run CENTERLINE."); return; }
  const CadLineRecord la = st.drawing.line(static_cast<std::size_t>(lines[0]->index));
  CadLineRecord lb = st.drawing.line(static_cast<std::size_t>(lines[1]->index));
  const double same = std::hypot(la.x0 - lb.x0, la.y0 - lb.y0) + std::hypot(la.x1 - lb.x1, la.y1 - lb.y1);
  const double reverse = std::hypot(la.x0 - lb.x1, la.y0 - lb.y1) + std::hypot(la.x1 - lb.x0, la.y1 - lb.y0);
  if (reverse < same) { std::swap(lb.x0, lb.x1); std::swap(lb.y0, lb.y1); std::swap(lb.z0, lb.z1); }
  PushUndoSnapshot(st, "CENTERLINE");
  st.drawing.pushLine(CadLineRecord{(la.x0 + lb.x0) * 0.5, (la.y0 + lb.y0) * 0.5, (la.z0 + lb.z0) * 0.5,
                            (la.x1 + lb.x1) * 0.5, (la.y1 + lb.y1) * 0.5, (la.z1 + lb.z1) * 0.5});
  st.drawing.userLineAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);
  log.push_back("CENTERLINE — centerline created between the two selected lines.");
}


bool HandleCircleTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  std::string line = StringUtil::trimCopy(lineIn);

  switch (st.circlePhase) {
  case AppCommandState::CirclePhase::WaitCenterOrMode: {
    if (StringUtil::toLowerAsciiCopy(line) == "3p") {
      st.circleStyle = AppCommandState::CircleStyle::ThreePoint;
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP1;
      log.push_back("Three-point circle — specify first point.");
      return true;
    }
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.circleCx = px;
    st.circleCy = py;
    st.circleCz = CadCommitElevation(st);
    st.circlePhase = AppCommandState::CirclePhase::WaitRadius;
    log.push_back("Center set — radius (click), type value, or D + diameter.");
    return true;
  }
  case AppCommandState::CirclePhase::WaitRadius: {
    float rad = 0.f;
    if (!ParseRadiusOrDiameter(line, &rad, log))
      return false;
    // A typed radius is already measured in the work plane, so the plane itself is all that is new.
    float nx = 0.f, ny = 0.f, nz = 1.f;
    const bool flat = CadWorkPlaneIsWorldXy(st);
    if (!flat)
      CadActiveDrawPlaneNormal(st, &nx, &ny, &nz);
    CommitCircle(st, st.circleCx, st.circleCy, flat ? CadCommitElevation(st) : st.circleCz, rad, nx, ny, nz,
                 log);
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP1: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.c3p1x = px;
    st.c3p1y = py;
    st.c3p1z = CadCommitElevation(st);
    st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP2;
    log.push_back("Second point:");
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP2: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    st.c3p2x = px;
    st.c3p2y = py;
    st.c3p2z = CadCommitElevation(st);
    st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP3;
    log.push_back("Third point:");
    return true;
  }
  case AppCommandState::CirclePhase::ThreeP_WaitP3: {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    if (!CommitCircleThreePoints(st, st.c3p1x, st.c3p1y, st.c3p1z, st.c3p2x, st.c3p2y, st.c3p2z, px, py,
                                 CadCommitElevation(st), log))
      log.push_back("Points are collinear — no circle.");
    return true;
  }
  }
  return false;
}


/// \p segments + 1 world points along the part of an ellipse that is actually DRAWN.
///
/// The counterpart of \ref SampleCurveWorld for the one authored curve that is not circular. Every
/// point comes from \ref EllipseWorldPointAt, so this agrees with the renderer and the snapper by
/// construction rather than by two copies of the same arithmetic staying in step.
///
/// A whole ellipse is a closed chain, its first point repeated last. An elliptical arc is NOT: its
/// two ends are not joined, and a fence that closed it would test a chord nobody drew.
///
/// Until this walked the span through the ellipse's own plane, both selection funnels hunted for a
/// tilted ellipse flat on the ground and a part-drawn one all the way round — a 45-degree section
/// oval of radius 30 was picked for at 42.4 out while it was drawn at 30, a 12.4-unit miss no
/// tolerance could close, and the 141 degrees it does not draw selected it from empty space.
///
/// Replaced `ArcRoughBounds` / `EllipseRoughBounds`, which existed only to give the selection fence
/// a box to test a curve against. Both are gone: a fence tests the CURVE now, and the extents walk
/// (`ComputeWorldExtents`) always had its own sampling.
void SampleEllipseWorld(std::vector<ray3d::Vec3>& out, const CadEllipse& e, int segments) {
  out.clear();
  const double ma = std::hypot(static_cast<double>(e.majVx), static_cast<double>(e.majVy));
  if (ma < 1e-8 || segments < 1)
    return;
  out.reserve(static_cast<size_t>(segments) + 1u);
  for (int i = 0; i <= segments; ++i) {
    const double u = static_cast<double>(i) / static_cast<double>(segments);
    out.push_back(EllipseWorldPointAt(e, EllipseSpanAngleAt(e, u)));
  }
}

/// \param toTest Optional world->test-space mapping. Null keeps the historical plan behaviour

// ---------------------------------------------------------------------------------------------
// Curve solvers (REQ-312).
//
// The geometry a set of picks defines, separated from the act of committing it. They live at file
// scope rather than in the anonymous namespace above because the rubber-band preview calls them
// from another translation unit: it has to draw the curve the commit is going to store, and the
// only way to guarantee that is for both to ask the same function.
// ---------------------------------------------------------------------------------------------

CadCircleSolution CadSolveCircleFromRimPick(const AppCommandState& st, float cx, float cy, float cz, float px,
                                            float py, float pz) {
  CadCircleSolution s;
  s.cx = cx;
  s.cy = cy;
  const float dx = px - cx;
  const float dy = py - cy;
  if (CadWorkPlaneIsWorldXy(st)) {
    s.cz = CadCommitElevation(st);
    s.r = std::sqrt(dx * dx + dy * dy);
    return s;
  }
  CadActiveDrawPlaneNormal(st, &s.nx, &s.ny, &s.nz);
  const float dz = pz - cz;
  s.cz = cz;
  s.r = std::sqrt(dx * dx + dy * dy + dz * dz);
  return s;
}

bool CadSolveCircleThreePoints(const AppCommandState& st, float ax, float ay, float az, float bx, float by,
                               float bz, float cx, float cy, float cz, CadCircleSolution* out) {
  if (!out)
    return false;
  CadCircleSolution s;
  float ox = 0.f, oy = 0.f, r = 0.f;
  if (CadWorkPlaneIsWorldXy(st)) {
    if (!ComputeCircumcircle(ax, ay, bx, by, cx, cy, &ox, &oy, &r))
      return false;
    s.cx = ox;
    s.cy = oy;
    s.cz = CadCommitElevation(st);
    s.r = r;
    *out = s;
    return true;
  }
  const ucs::Ucs plane = CadWorkPlaneAnchoredAt(st, ax, ay, az);
  const ucs::Point2D pb = ucs::WorldToPlane(plane, {bx, by, static_cast<double>(bz)});
  const ucs::Point2D pc = ucs::WorldToPlane(plane, {cx, cy, static_cast<double>(cz)});
  if (!ComputeCircumcircle(0.f, 0.f, static_cast<float>(pb.x), static_cast<float>(pb.y),
                           static_cast<float>(pc.x), static_cast<float>(pc.y), &ox, &oy, &r))
    return false;
  const ray3d::Vec3 centre = ucs::PlaneToWorld(plane, {ox, oy});
  s.cx = static_cast<float>(centre.x);
  s.cy = static_cast<float>(centre.y);
  s.cz = static_cast<float>(centre.z);
  s.r = r;
  CadActiveDrawPlaneNormal(st, &s.nx, &s.ny, &s.nz);
  *out = s;
  return true;
}

bool CadSolveArcThreePoints(const AppCommandState& st, float ax, float ay, float az, float bx, float by, float bz,
                            float cx, float cy, float cz, CadArc* out) {
  if (!out)
    return false;
  const bool flat = CadWorkPlaneIsWorldXy(st);
  float ox = 0.f, oy = 0.f, r = 0.f;
  CadArc arc{};

  if (flat) {
    if (!ComputeCircumcircle(ax, ay, bx, by, cx, cy, &ox, &oy, &r) || r < 1e-8f)
      return false;
    double sr = 0.;
    double sw = 0.;
    ComputeArcSweepRad(ox, oy, ax, ay, bx, by, cx, cy, &sr, &sw);
    arc.cx = ox;
    arc.cy = oy;
    arc.r = r;
    arc.startRad = static_cast<float>(sr);
    arc.sweepRad = static_cast<float>(sw);
    // The work plane's elevation (REQ-058), set here rather than by the caller so the solver hands
    // back a COMPLETE arc -- the rubber preview draws what it returns and has no second chance to
    // remember which field the commit was going to fill in afterwards. Paper space overrides it to
    // zero at the commit, where that boundary belongs (ADR-025 (g)).
    arc.z = CadCommitElevation(st);
  } else {
    // Solve in the work plane's own 2D coordinates, anchored on the first pick so the numbers going
    // into the float circumcircle stay small (REQ-101). The three picks lie in that plane, so the
    // planar maths below is exactly the same maths — it is only the coordinates that change.
    const ucs::Ucs plane = CadWorkPlaneAnchoredAt(st, ax, ay, az);
    const ucs::Point2D pb = ucs::WorldToPlane(plane, {bx, by, static_cast<double>(bz)});
    const ucs::Point2D pc = ucs::WorldToPlane(plane, {cx, cy, static_cast<double>(cz)});
    if (!ComputeCircumcircle(0.f, 0.f, static_cast<float>(pb.x), static_cast<float>(pb.y),
                             static_cast<float>(pc.x), static_cast<float>(pc.y), &ox, &oy, &r) ||
        r < 1e-8f)
      return false;
    const ray3d::Vec3 centre = ucs::PlaneToWorld(plane, {ox, oy});

    // startRad and sweepRad are stored in the arc's OWN frame — `ucs::FromNormal(centre, normal)`,
    // the Arbitrary Axis Algorithm (REQ-312) — not in the UCS frame, which is generally rotated
    // about the normal relative to it. Measuring the angles in the storage frame here is what stops
    // the renderer, the DXF writer and this commit from each picking a different zero direction.
    ucs::Ucs frame;
    if (!ucs::FromNormal(centre, plane.zAxis, &frame))
      return false;
    const ucs::Point2D fa = ucs::WorldToPlane(frame, {ax, ay, static_cast<double>(az)});
    const ucs::Point2D fb = ucs::WorldToPlane(frame, {bx, by, static_cast<double>(bz)});
    const ucs::Point2D fc = ucs::WorldToPlane(frame, {cx, cy, static_cast<double>(cz)});
    double sr = 0.;
    double sw = 0.;
    ComputeArcSweepRad(0., 0., fa.x, fa.y, fb.x, fb.y, fc.x, fc.y, &sr, &sw);
    arc.cx = static_cast<float>(centre.x);
    arc.cy = static_cast<float>(centre.y);
    arc.r = r;
    arc.startRad = static_cast<float>(sr);
    arc.sweepRad = static_cast<float>(sw);
    arc.z = static_cast<float>(centre.z);
    CadActiveDrawPlaneNormal(st, &arc.nx, &arc.ny, &arc.nz);
  }

  *out = arc;
  return true;
}

// Public surface rollover entry point (REQ-089): the readout for the plan position under the cursor,

void StartLineCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  ResetSegmentAngleLock(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Line;
  st.lastCommand = AppCommandState::Kind::Line;
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  st.lineDraftSegments = 0;
  log.push_back("LINE — specify first point (click or type X,Y / X Y). ESC to cancel.");
}

void StartCircleCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Circle;
  st.lastCommand = AppCommandState::Kind::Circle;
  log.push_back(
      "CIRCLE — center + radius: click/type center, then radius (click edge), type radius, or D + diameter.");
  log.push_back("Or type 3P first for a three-point circle. ESC to cancel.");
}

void StartPolylineCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  ResetSegmentAngleLock(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Polyline;
  st.polylinePhase = AppCommandState::PolylinePhase::NeedFirstPoint;
  log.push_back("POLYLINE — click points; click the FIRST point to close, Enter to finish open. "
                "(CLOSE/CL, END still work.) A / 2P bearing lock; ortho; ESC cancels.");
}


void StartPolyline3dCommand(AppCommandState& st, std::vector<std::string>& log) {
  // REQ-085. Same draft as POLYLINE — the store is already stride-3 XYZ — with per-vertex elevation
  // entry switched on. ResetAllCadDraftTools inside StartPolylineCommand clears the flag, so it is
  // set afterwards, not before.
  StartPolylineCommand(st, log);
  log.pop_back();  // replace POLYLINE's prompt rather than printing both
  st.polylineDraft3d = true;
  log.push_back("3DPOLY — vertices carry their own elevation: type X,Y,Z (or @dx,dy,dz), or snap. "
                "X,Y alone uses the snapped point's elevation, else ELEV. Click the FIRST point to "
                "close, Enter to finish open. (CLOSE/CL, END still work.) ESC cancels.");
}

void StartArcCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Arc;
  st.arcPhase = AppCommandState::ArcPhase::WaitStart;
  log.push_back("ARC — three picks: start, point on arc, end. ESC cancels.");
}

void StartEllipseCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Ellipse;
  st.ellPhase = AppCommandState::EllipsePhase::WaitCenter;
  log.push_back("ELLIPSE — center, major axis endpoint, then minor/major ratio (0-1] on command line (Enter=0.5).");
}

void StartSplineCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Spline;
  st.lastCommand = AppCommandState::Kind::Spline;
  st.splineMethod = AppCommandState::SplineMethod::Fit;
  st.splineDraftPoints.clear();
  log.push_back("SPLINE — Fit points (curve passes through them). Type CV for control vertices. Pick or type X,Y; Close / End / Undo:");
}


void StartRectCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::Rect;
  st.lastCommand = AppCommandState::Kind::Rect;
  st.rectPhase = AppCommandState::RectPhase::WaitFirstCorner;
  log.push_back("RECT — pick the first corner (or type X,Y):");
}

// REQ-053: a rectangle is a 4-vertex CLOSED polyline, matching AutoCAD's RECTANG (which produces an
// LWPOLYLINE). Storing it that way means DXF/DWG write, grips, snaps — including the geometric center —
// and the offset/trim/join paths all work on it with no new entity type.
void CommitRectangle(AppCommandState& st, float x1, float y1, float x2, float y2,
                     std::vector<std::string>& log) {
  const float mnX = std::min(x1, x2);
  const float mxX = std::max(x1, x2);
  const float mnY = std::min(y1, y2);
  const float mxY = std::max(y1, y2);
  if (mxX - mnX < 1.e-9f || mxY - mnY < 1.e-9f) {
    log.push_back("RECT — corners give a zero-width or zero-height rectangle; pick again.");
    st.rectPhase = AppCommandState::RectPhase::WaitFirstCorner;
    return;
  }

  const float xs[4] = {mnX, mxX, mxX, mnX};
  const float ys[4] = {mnY, mnY, mxY, mxY};

  PushUndoSnapshot(st, "Rectangle");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    // Paper-space RECT (REQ-037): the corners are paper inches; commit to the layout's paper store.
    const int baseVert = L->paperPolyOffsets.empty() ? 0 : L->paperPolyOffsets.back();
    for (int i = 0; i < 4; ++i) {
      L->paperPolyVerts.push_back(xs[i]);
      L->paperPolyVerts.push_back(ys[i]);
      L->paperPolyVerts.push_back(0.f);
    }
    if (L->paperPolyOffsets.empty())
      L->paperPolyOffsets.push_back(baseVert);
    L->paperPolyOffsets.push_back(baseVert + 4);
    L->paperPolyClosed.push_back(1u);
    L->paperPolyAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    if (st.drawing.userPolylineOffsets.empty())
      st.drawing.userPolylineOffsets.push_back(0);
    const int baseVert = st.drawing.userPolylineOffsets.back();
    for (int i = 0; i < 4; ++i) {
      // Lands on the active work plane (REQ-058) — see SubmitLineVertex.
      st.drawing.pushVertex(CadPolyVertex{xs[i], ys[i], CadCommitElevation(st)});
    }
    st.drawing.userPolylineOffsets.push_back(baseVert + 4);
    SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());  // REQ-316: RECT is straight
    SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
    st.drawing.userPolylineClosed.push_back(1u);
    st.drawing.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
  }

  char msg[128];
  std::snprintf(msg, sizeof(msg), "RECT — %.6g x %.6g rectangle created.",
                static_cast<double>(mxX - mnX), static_cast<double>(mxY - mnY));
  log.push_back(msg);

  st.active = AppCommandState::Kind::None;
  ResetRectDraft(st);
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
}


void StartHatchCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.active = AppCommandState::Kind::Hatch;
  st.lastCommand = AppCommandState::Kind::Hatch;
  st.hatchPreviewValid = false;
  st.hatchPreviewLoop.clear();
  ClearCadSelection(st);
  log.push_back("HATCH — pick an internal point inside a closed area (Esc to cancel).");
}

// Gather every model boundary segment into \p out (local coordinates): line segments, polyline edges
// (closing edge when closed), and tessellated arcs/circles/ellipses. Used by the HATCH boundary trace.
static void CadCollectBoundarySegments(const AppCommandState& st, std::vector<hatchboundary::Seg>* out) {
  for (size_t li = 0; li < st.drawing.lineCount(); ++li) {
    const CadLineRecord ln = st.drawing.line(li);
    out->push_back({ln.x0, ln.y0, ln.x1, ln.y1});
  }

  const int nPoly = static_cast<int>(st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    for (int vi = v0; vi + 1 < v1; ++vi)
      out->push_back({st.drawing.vertex(vi).x,
                      st.drawing.vertex(vi).y,
                      st.drawing.vertex(static_cast<size_t>(vi + 1)).x,
                      st.drawing.vertex(static_cast<size_t>(vi + 1)).y});
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    if (closed && v1 - v0 >= 2)
      out->push_back({st.drawing.vertex(static_cast<size_t>(v1 - 1)).x,
                      st.drawing.vertex(static_cast<size_t>(v1 - 1)).y,
                      st.drawing.vertex(v0).x,
                      st.drawing.vertex(v0).y});
  }

  auto tessellate = [&](auto pointAt, int n) {
    float px = 0.f, py = 0.f;
    for (int i = 0; i <= n; ++i) {
      float x = 0.f, y = 0.f;
      pointAt(i, &x, &y);
      if (i > 0)
        out->push_back({px, py, x, y});
      px = x;
      py = y;
    }
  };
  for (size_t ci = 0; ci < st.drawing.circleCount(); ++ci) {
    const CadCircleRecord cr = st.drawing.circle(ci);
    const float cx = static_cast<float>(cr.cx), cy = static_cast<float>(cr.cy), r = static_cast<float>(cr.r);
    tessellate([&](int i, float* x, float* y) {
      const double a = 6.283185307179586 * i / 48.0;
      *x = cx + r * static_cast<float>(std::cos(a));
      *y = cy + r * static_cast<float>(std::sin(a));
    }, 48);
  }
  for (const CadArc& a : st.drawing.userArcs) {
    tessellate([&](int i, float* x, float* y) {
      const double t = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * (i / 48.0);
      *x = static_cast<float>(a.cx + a.r * static_cast<float>(std::cos(t)));
      *y = static_cast<float>(a.cy + a.r * static_cast<float>(std::sin(t)));
    }, 48);
  }
  for (const CadEllipse& el : st.drawing.userEllipses) {
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    if (ma < 1e-8)
      continue;
    const double ux = el.majVx / ma, uy = el.majVy / ma;
    const double mb = ma * static_cast<double>(el.ratio);
    tessellate([&](int i, float* x, float* y) {
      const double t = 6.283185307179586 * i / 64.0;
      const double cc = std::cos(t), ss = std::sin(t);
      *x = static_cast<float>(el.cx + static_cast<float>(ux * ma * cc - uy * mb * ss));
      *y = static_cast<float>(el.cy + static_cast<float>(uy * ma * cc + ux * mb * ss));
    }, 64);
  }
}

bool CadHatchTraceAt(const AppCommandState& st, double wx, double wy, std::vector<float>* outLoop) {
  if (!outLoop)
    return false;
  std::vector<hatchboundary::Seg> segs;
  CadCollectBoundarySegments(st, &segs);
  return hatchboundary::TraceEnclosingLoop(segs, wx, wy, outLoop);
}

bool CadHatchCommitLoop(AppCommandState& st, const std::vector<float>& loop, std::vector<std::string>& log) {
  if (loop.size() < 6)
    return false;
  PushUndoSnapshot(st, "Hatch");
  CadFilledRegion fr;
  // \p loop is the traced boundary as flat local x,y pairs; the store is interleaved x,y,z
  // (ADR-025 (a)). The work plane exists now, so the region lands on it like every other created
  // entity (REQ-058) rather than always on the datum. The tracer works in XY and returns no
  // elevations, so all vertices share one Z — a hatch is planar anyway.
  const float hatchZ = CadCommitElevation(st);
  fr.vertsXyz.reserve(loop.size() / 2 * 3);
  for (size_t i = 0; i + 1 < loop.size(); i += 2) {
    fr.vertsXyz.push_back(loop[i + 0]);
    fr.vertsXyz.push_back(loop[i + 1]);
    fr.vertsXyz.push_back(hatchZ);
  }
  fr.loopStart = {0};
  fr.patternName = st.hatchPatternName;  // "" / "SOLID" = solid; else a line pattern (ADR-018)
  fr.patternAngleDeg = st.hatchAngleDeg;
  fr.patternScale = std::max(0.01f, st.hatchScale);
  EntityAttributes at = MakeNewEntityAttrs(st);
  if (!st.hatchLayer.empty())
    at.layer = st.hatchLayer;
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", static_cast<int>(std::lround(st.hatchColorRgb[0] * 255.f)),
                static_cast<int>(std::lround(st.hatchColorRgb[1] * 255.f)),
                static_cast<int>(std::lround(st.hatchColorRgb[2] * 255.f)));
  at.color = hex;
  at.transparency = std::clamp(st.hatchTransparency01, 0.f, 1.f);
  st.drawing.cadFilledRegions.push_back(std::move(fr));
  st.drawing.cadFilledRegionAttrs.push_back(at);
  ClearCadSelection(st);
  st.selection.push_back({SelectedEntity::Type::FilledRegion,
                          static_cast<int>(st.drawing.cadFilledRegions.size()) - 1});
  BumpCadGpuCache(st);
  log.push_back("HATCH — solid fill placed.");
  return true;
}


//
// This is the UCS in the only form the application currently produces: the work plane stays
// parallel to world XY and moves in Z. AutoCAD splits the same idea across ELEV (elevation) and
// UCS (a full coordinate system); only the elevation half exists here, so it carries AutoCAD's
// name for that half rather than pretending to be a full UCS.
void StartElevCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.active = AppCommandState::Kind::Elev;
  char buf[160];
  std::snprintf(buf, sizeof(buf), "Specify new default elevation <%.4f>:  (W = back to world Z 0)",
                static_cast<double>(CadWorkPlaneElevation(st)));
  log.push_back(buf);
}

// Shared by the prompt and the inline `ELEV 12.5` form, so neither can set a value the other would
// reject (REQ-201).
bool ApplyElevValue(AppCommandState& st, double z, std::vector<std::string>& log) {
  if (!std::isfinite(z)) {
    log.push_back("ELEV — elevation must be a finite number.");
    return false;
  }
  // ELEV moves the work plane in Z and leaves the orientation alone, so under a rotated UCS it
  // raises THAT plane rather than quietly reverting to a world-parallel one (REQ-154).
  ucs::Ucs raised = st.activeUcs;
  raised.origin.z = z;
  SetActiveUcs(st, raised, log);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "Elevation = %.4f — new geometry is drawn on this plane.", z);
  log.push_back(buf);
  return true;
}

/// Import a glTF/GLB model as a REQ-063 mesh (REQ-065).
///

// placed at file scope so it is NOT itself swept into that anonymous namespace. Unqualified lookup of
// CommitCircle still finds it: an unnamed namespace injects an implicit using-directive into its
// enclosing (here, global) scope for the rest of the translation unit.
void CommitCircleExternal(AppCommandState& st, float cx, float cy, float cz, float r, float nx, float ny, float nz,
                          std::vector<std::string>& log) {
  CommitCircle(st, cx, cy, cz, r, nx, ny, nz, log);
}


bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Line)
    return false;

  // REQ-204 (`finite-coords`) / REQ-201 / issue #122: a point can be finite yet too large to store
  // without downstream geometry math overflowing `float` to inf/NaN — an OFFSET of a segment whose
  // endpoint sits near 1e38 wrote `-nan(ind)` into `userLinesFlat`. Refuse it here, at the command
  // that accepts the point, rather than making every consumer guard — the same place and spirit as
  // CIRCLE refusing a non-finite derived radius.
  if (!std::isfinite(x) || !std::isfinite(y) ||
      std::fabs(x) > static_cast<float>(CadCoord::kMaxStorableCoordinateMagnitude) ||
      std::fabs(y) > static_cast<float>(CadCoord::kMaxStorableCoordinateMagnitude)) {
    log.push_back("LINE rejected — a point is not a finite, storable coordinate.");
    return false;
  }

  if (st.linePhase == AppCommandState::LinePhase::NeedFirstPoint) {
    st.anchorX = x;
    st.anchorZ = CadCommitElevation(st);
    st.anchorY = y;
    st.linePhase = AppCommandState::LinePhase::NeedNextPoint;
    log.push_back("First point set. Next: click; X, Y; @dx,dy; [A]zimuth, [2P];");
    return true;
  }

  PushUndoSnapshot(st, "Line segment");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    // Paper-space LINE (REQ-037): anchor/x,y are paper inches; commit to the layout's paper store.
    L->paperLines.push_back(st.anchorX);
    L->paperLines.push_back(st.anchorY);
    L->paperLines.push_back(0.f);
    L->paperLines.push_back(x);
    L->paperLines.push_back(y);
    L->paperLines.push_back(0.f);
    L->paperLineAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    // New geometry lands on the active work plane (REQ-058); ELEV moves that plane, and the
    // default is world XY, so this is 0 until the user changes it.
    // Anchor's own elevation, then this end's (snap overrides ELEV).
    st.drawing.pushLine(CadLineRecord{st.anchorX, st.anchorY, st.anchorZ, x, y, CadCommitElevation(st)});
    st.drawing.userLineAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);

  st.anchorX = x;
  st.anchorZ = CadCommitElevation(st);
  st.anchorY = y;
  ++st.lineDraftSegments;
  log.push_back("Segment added — next point or ESC to finish.");
  return true;
}

// REQ-316 / ADR-047: the bulge for the segment leaving the last draft vertex, given a new point
// (x,y). In LINE mode the answer is always 0. In ARC mode the arc is tangent to the previous
// segment's end direction unless a radius or an included angle was typed for this pick.
float CadPolylineDraftBulgeForNextPoint(const AppCommandState& st, float x, float y) {
  if (!st.polylineArcMode)
    return 0.f;
  const size_t nv = st.polylineDraftVerts.size() / 3;
  if (nv < 1)
    return 0.f;
  const size_t li = nv - 1;
  const float ax = st.polylineDraftVerts[li * 3 + 0];
  const float ay = st.polylineDraftVerts[li * 3 + 1];
  const double chx = static_cast<double>(x) - ax;
  const double chy = static_cast<double>(y) - ay;
  const double chord = std::hypot(chx, chy);
  if (chord < 1e-9)
    return 0.f;
  constexpr double kPi = 3.14159265358979323846;

  // Incoming direction at the last vertex: the previous arc's end tangent, or the previous chord.
  double dx = 0.0, dy = 0.0;
  bool haveDir = false;
  if (nv >= 2) {
    const float px = st.polylineDraftVerts[(li - 1) * 3 + 0];
    const float py = st.polylineDraftVerts[(li - 1) * 3 + 1];
    const float pb = (li - 1) < st.polylineDraftBulge.size() ? st.polylineDraftBulge[li - 1] : 0.f;
    const BulgeArcSpan prev = BulgeArc(px, py, ax, ay, static_cast<double>(pb));
    if (prev.valid) {
      const double endA = prev.startAngle + prev.sweep;
      const double s = prev.sweep >= 0.0 ? 1.0 : -1.0;
      dx = -std::sin(endA) * s;
      dy = std::cos(endA) * s;
    } else {
      dx = static_cast<double>(ax) - px;
      dy = static_cast<double>(ay) - py;
    }
    const double dn = std::hypot(dx, dy);
    if (dn > 1e-12) { dx /= dn; dy /= dn; haveDir = true; }
  }

  const double side = haveDir ? (dx * chy - dy * chx) : 0.0;  // + = P is left of the incoming dir
  const double sgn = side >= 0.0 ? 1.0 : -1.0;

  double theta = 0.0;  // signed included (central) angle; + = CCW
  if (st.polylineArcAngleValid) {
    // Typed magnitude; direction follows which side of the tangent the pick is (a negative typed
    // angle flips it), matching AutoCAD's PLINE Angle option.
    const double mag = std::fabs(static_cast<double>(st.polylineArcAngleDeg)) * kPi / 180.0;
    const double flip = st.polylineArcAngleDeg < 0.f ? -1.0 : 1.0;
    theta = mag * flip * (haveDir ? sgn : 1.0);
  } else if (st.polylineArcRadius > 1e-9f) {
    const double r = static_cast<double>(st.polylineArcRadius);
    const double s = std::min(1.0, chord / (2.0 * r));
    theta = 2.0 * std::asin(s) * (haveDir ? sgn : 1.0);
  } else if (haveDir) {
    // Tangent arc: the tangent/chord angle is half the central angle.
    const double alpha = std::atan2(dx * chy - dy * chx, dx * chx + dy * chy);
    theta = 2.0 * alpha;
  } else {
    return 0.f;  // first segment, no direction, no radius/angle -> straight
  }
  if (std::fabs(theta) < 1e-9)
    return 0.f;
  return static_cast<float>(std::tan(theta / 4.0));
}

bool SubmitPolylineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Polyline)
    return false;

  using PP = AppCommandState::PolylinePhase;

  // The vertex elevation, in priority order (REQ-085): an elevation typed on this line, then the
  // ordinary rule — the snapped point's own Z, else the work plane (REQ-058). A typed Z is consumed
  // here so it cannot leak into the NEXT vertex, which would silently repeat the last elevation.
  float vz = CadCommitElevation(st);
  if (st.polylineTypedZValid) {
    vz = st.polylineTypedZRelative ? st.anchorZ + st.polylineTypedZ : st.polylineTypedZ;
    st.polylineTypedZValid = false;
    st.polylineTypedZRelative = false;
  }
  const char* who = st.polylineDraft3d ? "3DPOLY" : "POLYLINE";

  if (st.polylinePhase == PP::NeedFirstPoint) {
    st.polylineDraftVerts.clear();
    st.polylineDraftVerts.push_back(x);
    st.polylineDraftVerts.push_back(y);
    st.polylineDraftVerts.push_back(vz);
    st.polylineDraftBulge.assign(1, 0.f);  // REQ-316 / ADR-047
    st.anchorX = x;
    st.anchorZ = vz;
    st.anchorY = y;
    st.polyFirstX = x;
    st.polyFirstY = y;
    st.polyDraftSegments = 0;
    st.polylinePhase = PP::NeedNextPoint;
    if (st.polylineDraft3d) {
      // Reported for the same reason every later vertex is: the elevation is the thing 3DPOLY exists
      // to control, and it is otherwise invisible until the drawing is saved (REQ-201).
      char buf[96];
      std::snprintf(buf, sizeof(buf), "3DPOLY first vertex at elevation %.3f.", static_cast<double>(vz));
      log.push_back(buf);
    }
    log.push_back(std::string(who) +
                  " — next vertex; click the FIRST point to close, Enter to finish open "
                  "(CLOSE / END still work), or ESC.");
    return true;
  }

  // REQ-118: a vertex that lands on the STARTING vertex closes the polyline instead of being added.
  //
  // The test is coincidence with the stored first vertex, NOT "the snap system said so". CadSnap
  // returns the stored coordinate verbatim, so a snapped point compares equal here; a typed
  // coordinate or an exact click closes it too, which is what a user means by clicking the start
  // and is what a snap-gated rule would refuse whenever OSNAP is off (D-2026-08-25-j).
  //
  // The tolerance is deliberately tight — "the same point", not "near it". A near-miss click stays
  // an ordinary vertex, because without landing on the start the user has not indicated it; the
  // snap candidate (CadSnap, offered from three vertices on) is what makes landing on it easy.
  //
  // Guarded at three vertices, matching CommitPolylineDraft's own CLOSE minimum, so the second
  // vertex of a two-point draft is an ordinary vertex even if it sits on the first.
  constexpr float kCloseEps = 1e-4f;
  if (st.polylineDraftVerts.size() >= 9 && std::fabs(x - st.polyFirstX) <= kCloseEps &&
      std::fabs(y - st.polyFirstY) <= kCloseEps) {
    // REQ-316 / ADR-047: an arc-mode close bows the closing segment too.
    if (!st.polylineDraftBulge.empty())
      st.polylineDraftBulge.back() = CadPolylineDraftBulgeForNextPoint(st, st.polyFirstX, st.polyFirstY);
    CommitPolylineDraft(st, true, log);
    return true;
  }

  // REQ-316 / ADR-047: the bulge of the segment LEAVING the previous vertex, then a 0 for the new
  // one (set when the segment after it is drawn, or left 0 at commit). Consume the arc radius/angle.
  {
    const float b = CadPolylineDraftBulgeForNextPoint(st, x, y);
    if (!st.polylineDraftBulge.empty())
      st.polylineDraftBulge.back() = b;
    st.polylineArcRadius = 0.f;
    st.polylineArcAngleValid = false;
    st.polylineArcAngleDeg = 0.f;
  }
  st.polylineDraftVerts.push_back(x);
  st.polylineDraftVerts.push_back(y);
  st.polylineDraftVerts.push_back(vz);
  st.polylineDraftBulge.push_back(0.f);
  ++st.polyDraftSegments;
  st.anchorX = x;
  st.anchorZ = vz;
  st.anchorY = y;
  if (st.polylineDraft3d) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "3DPOLY vertex added at elevation %.3f.", static_cast<double>(vz));
    log.push_back(buf);
  } else {
    log.push_back("POLYLINE vertex added.");
  }
  return true;
}

// localX/localY are LOCAL storage coordinates, not world — see the declaration in CadCommands.hpp.
