#include "CadCommandsInternal.hpp"
#include <GoSurvey/Geometry/spline.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static void CommitPolylineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log);

/// CIRCLE's centre-and-radius commit, given the centre pick and the rim pick (REQ-312).
///
/// On a flat work plane this is the pre-REQ-312 arithmetic to the bit. On a tilted one the rim pick
/// is displaced in Z as well, so the radius is the 3D distance to it — its XY projection is short by
/// cos(tilt), and on a vertical plane it collapses to nothing at all.
static void CommitCircleFromRimPick(AppCommandState& st, float cx, float cy, float cz, float px, float py,
                                    float pz, std::vector<std::string>& log) {
  const CadCircleSolution s = CadSolveCircleFromRimPick(st, cx, cy, cz, px, py, pz);
  CommitCircle(st, s.cx, s.cy, s.cz, s.r, s.nx, s.ny, s.nz, log);
}

/// CIRCLE 3P: the circle through three picks on the active work plane (REQ-312).
///
/// Returns false when the picks are collinear — in the plane, which on a tilted plane is not the
/// same question as collinear in the XY projection.
static bool CommitCircleThreePoints(AppCommandState& st, float ax, float ay, float az, float bx, float by,
                                    float bz, float cx, float cy, float cz, std::vector<std::string>& log) {
  CadCircleSolution s;
  if (!CadSolveCircleThreePoints(st, ax, ay, az, bx, by, bz, cx, cy, cz, &s))
    return false;
  CommitCircle(st, s.cx, s.cy, s.cz, s.r, s.nx, s.ny, s.nz, log);
  return true;
}

static void ComputeArcSweepRad(double ox, double oy, double ax, double ay, double bx, double by, double cx,
                               double cy, double* startRad, double* sweepRad) {
  constexpr double twopi = 6.28318530717958647692;
  auto normPos = [](double x) {
    double r = std::fmod(x, twopi);
    if (r < 0)
      r += twopi;
    return r;
  };
  const double ta = std::atan2(ay - oy, ax - ox);
  const double tb = std::atan2(by - oy, bx - ox);
  const double tc = std::atan2(cy - oy, cx - ox);
  const double arc_ab = normPos(tb - ta);
  const double arc_ac = normPos(tc - ta);
  const bool useCcw = arc_ab <= arc_ac + 1e-10;
  double sweep = useCcw ? arc_ac : arc_ac - twopi;
  if (std::fabs(sweep) < 1e-12)
    sweep = twopi;
  *startRad = ta;
  *sweepRad = sweep;
}

static void CommitArcThreePoints(AppCommandState& st, float ax, float ay, float az, float bx, float by, float bz,
                                 float cx, float cy, float cz, std::vector<std::string>& log) {
  CadArc arc{};
  if (!CadSolveArcThreePoints(st, ax, ay, az, bx, by, bz, cx, cy, cz, &arc)) {
    // One message for both refusals the solver can make: three picks that do not define an arc,
    // and a work plane that is not a plane. Either way the user sees the same outcome - no arc was
    // created - and REQ-201 asks for a refusal that is reported, which this is.
    log.push_back("ARC - points are collinear, or the work plane is not valid.");
    st.active = AppCommandState::Kind::None;
    ResetArcDraft(st);
    return;
  }

  PushUndoSnapshot(st, "Arc");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    // Paper-space ARC (REQ-039): the three points are paper inches; commit to the layout's paper
    // store. arc.z stays 0 (ADR-025 (g): always 0 in paper space), matching the shape
    // CommitPolylineDraft uses for POLYLINE (issue #84/#86). A sheet has no work plane, so the
    // normal stays world +Z there too.
    arc.z = 0.f;
    arc.nx = 0.f;
    arc.ny = 0.f;
    arc.nz = 1.f;
    L->paperArcs.push_back(arc);
    L->paperArcAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    st.drawing.userArcs.push_back(arc);  // arc.z already carries the work plane (set by the solver)
    st.drawing.userArcAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetArcDraft(st);
  log.push_back("ARC complete.");
}


static bool FinishSplineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log) {
  if (st.splineDraftPoints.size() < (closed ? 3u : 2u)) {
    log.push_back(closed ? "SPLINE — a closed spline needs at least three points."
                         : "SPLINE — an open spline needs at least two points.");
    return false;
  }
  CadSpline splineEntity;
  std::string why;
  const bool ok = st.splineMethod == AppCommandState::SplineMethod::Fit
                      ? spline::MakeFit(st.splineDraftPoints, closed, &splineEntity, &why)
                      : spline::MakeControl(st.splineDraftPoints, closed, &splineEntity, &why);
  if (!ok) {
    log.push_back("SPLINE rejected — " + why + ". The draft is unchanged.");
    return false;
  }
  PushUndoSnapshot(st, "Create spline");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    L->paperSplines.push_back(std::move(splineEntity));
    L->paperSplineAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    st.drawing.cadSplines.push_back(std::move(splineEntity));
    st.drawing.cadSplineAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  st.splineDraftPoints.clear();
  st.active = AppCommandState::Kind::None;
  log.push_back("SPLINE complete.");
  return true;
}

static bool HandleSplineTextInput(const std::string& lineIn, AppCommandState& st,
                                 std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::Spline)
    return false;
  const std::string line = StringUtil::trimCopy(lineIn);
  std::string option = line;
  std::transform(option.begin(), option.end(), option.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  if (option == "CV" || option == "CONTROL") {
    if (!st.splineDraftPoints.empty()) {
      log.push_back("SPLINE — choose Fit or CV before the first point.");
      return true;
    }
    st.splineMethod = AppCommandState::SplineMethod::Control;
    log.push_back("SPLINE — Control vertices selected. Pick or type X,Y; Close / End:");
    return true;
  }
  if (option == "FIT") {
    if (!st.splineDraftPoints.empty()) {
      log.push_back("SPLINE — choose Fit or CV before the first point.");
      return true;
    }
    st.splineMethod = AppCommandState::SplineMethod::Fit;
    log.push_back("SPLINE — Fit points selected. Pick or type X,Y; Close / End:");
    return true;
  }
  if (option == "UNDO" || option == "U") {
    if (!st.splineDraftPoints.empty())
      st.splineDraftPoints.pop_back();
    log.push_back("SPLINE — last draft point removed.");
    return true;
  }
  if (option == "CLOSE" || option == "CL") {
    (void)FinishSplineDraft(st, true, log);
    return true;
  }
  if (option == "END" || line.empty()) {
    if (st.splineDraftPoints.empty()) {
      st.active = AppCommandState::Kind::None;
      log.push_back("SPLINE ended — nothing was added.");
    } else {
      (void)FinishSplineDraft(st, false, log);
    }
    return true;
  }
  float x = 0.f, y = 0.f;
  if (!ParseStoragePoint(st, line, &x, &y, false, 0.f, 0.f)) {
    log.push_back("SPLINE — enter a point as X,Y, or choose CV, Fit, Close, End or Undo.");
    return true;
  }
  st.splineDraftPoints.push_back({x, y, CadCommitElevation(st)});
  log.push_back("SPLINE — point added. Enter another X,Y, or Close / End / Undo:");
  return true;
}


static void FinishEllipseFromRatio(AppCommandState& st, float ratio, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  using EP = AppCommandState::EllipsePhase;
  if (st.active != K::Ellipse || st.ellPhase != EP::WaitRatio)
    return;
  if (ratio <= 1e-8f || ratio > 1.f + 1e-4f) {
    log.push_back("ELLIPSE — ratio must be in (0, 1].");
    return;
  }
  const float vx0 = st.ellMajEx - st.ellCx;
  const float vy0 = st.ellMajEy - st.ellCy;
  const float ma = std::hypot(vx0, vy0);
  if (ma < 1e-8f) {
    log.push_back("ELLIPSE — major axis too short.");
    return;
  }
  CadEllipse ell{};
  ell.cx = st.ellCx;
  ell.cy = st.ellCy;
  ell.majVx = vx0;
  ell.majVy = vy0;
  ell.ratio = ratio;

  // A turned UCS stands the ellipse up in that plane (GitHub #531), the way `CommitCircle` has done
  // for circles since REQ-312. Resolved BEFORE the undo snapshot below: a refusal here must leave no
  // undo entry behind, which is the shape every other refusal in this function already has.
  const bool tilted = !ActivePaperGeometryTarget(st) && !CadWorkPlaneIsWorldXy(st);
  if (tilted) {
    float nx = 0.f, ny = 0.f, nz = 1.f;
    CadActiveDrawPlaneNormal(st, &nx, &ny, &nz);
    // The same guard `CommitCircle` carries, and for the same reason: a normal that is NaN or
    // zero-length does not fail loudly — `ucs::FromNormal` refuses and every consumer quietly falls
    // back to a different plane than the one drawn on (REQ-201, REQ-204).
    if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) ||
        (nx * nx + ny * ny + nz * nz) < 1e-12f) {
      log.push_back("ELLIPSE rejected — the work plane is not a valid plane.");
      return;
    }
    // The two picks are full 3D points, each carrying the elevation it was made at, so the major
    // axis is measured IN the plane — the only frame `majVx`/`majVy` is ever read in (see
    // `EllipseWorldPointAt`), and the same frame `SECTION` writes its tilted ellipse in.
    const ucs::Ucs plane = CurvePlane(static_cast<double>(st.ellCx), static_cast<double>(st.ellCy),
                                      static_cast<double>(st.ellCz), static_cast<double>(nx),
                                      static_cast<double>(ny), static_cast<double>(nz));
    const ucs::Point2D c2 = ucs::WorldToPlane(
        plane, ray3d::Vec3{static_cast<double>(st.ellCx), static_cast<double>(st.ellCy),
                           static_cast<double>(st.ellCz)});
    const ucs::Point2D m2 = ucs::WorldToPlane(
        plane, ray3d::Vec3{static_cast<double>(st.ellMajEx), static_cast<double>(st.ellMajEy),
                           static_cast<double>(st.ellMajEz)});
    const double mvx = m2.x - c2.x;
    const double mvy = m2.y - c2.y;
    if (std::hypot(mvx, mvy) < 1e-8) {
      // Distinct points in space that fall on the same point of the plane: the second lies along the
      // normal, straight "through" the drawing. There is no major axis to draw.
      log.push_back("ELLIPSE — major axis too short in the work plane.");
      return;
    }
    ell.z = st.ellCz;
    ell.majVx = static_cast<float>(mvx);
    ell.majVy = static_cast<float>(mvy);
    ell.nx = nx;
    ell.ny = ny;
    ell.nz = nz;
  }

  PushUndoSnapshot(st, "Ellipse");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    // Paper-space ELLIPSE (REQ-039): centre/axis are paper inches; commit to the layout's paper
    // store. ell.z stays 0 (ADR-025 (g): always 0 in paper space), matching the shape
    // CommitPolylineDraft uses for POLYLINE (issue #84/#86).
    L->paperEllipses.push_back(ell);
    L->paperEllAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    if (!tilted)
      ell.z = CadCommitElevation(st);  // lands on the active work plane (REQ-058)
    st.drawing.userEllipses.push_back(ell);
    st.drawing.userEllAttrs.push_back(MakeNewEntityAttrs(st));
  }
  BumpCadGpuCache(st);
  st.active = K::None;
  ResetEllipseDraft(st);
  log.push_back("ELLIPSE complete.");
}

static void CommitPolylineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log) {
  const size_t nvert = st.polylineDraftVerts.size() / 3;
  if (closed) {
    if (nvert < 3) {
      log.push_back("POLYLINE CLOSE — need at least three vertices.");
      return;
    }
  } else if (nvert < 2) {
    log.push_back("POLYLINE — need at least two vertices (use END to finish open).");
    return;
  }
  PushUndoSnapshot(st, closed ? "Polyline (closed)" : "Polyline");
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    // Paper-space POLYLINE (REQ-037 / REQ-039 (5)): the draft's vertices are paper inches; commit
    // to the active layout's paper store. Without this the polyline landed in the MODEL store while
    // the user was drawing on a sheet (#84), which also broke REQ-039 (6) — "none of these paper
    // edits change model geometry". Same routing `CommitRectangle` and `SubmitLineVertex` already do.
    const int baseVert = L->paperPolyOffsets.empty() ? 0 : L->paperPolyOffsets.back();
    L->paperPolyVerts.insert(L->paperPolyVerts.end(), st.polylineDraftVerts.begin(),
                             st.polylineDraftVerts.end());
    if (L->paperPolyOffsets.empty())
      L->paperPolyOffsets.push_back(baseVert);
    L->paperPolyOffsets.push_back(baseVert + static_cast<int>(nvert));
    // `closed` is carried through rather than hardcoded: paper RECT can write 1u because a rectangle
    // is always closed, but POLYLINE's CLOSE/END (and #80's click-the-start/Enter) both reach here.
    L->paperPolyClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
    L->paperPolyAttrs.push_back(MakeNewEntityAttrs(st));
  } else {
    if (st.drawing.userPolylineOffsets.empty())
      st.drawing.userPolylineOffsets.push_back(0);
    const int baseVert = st.drawing.userPolylineOffsets.back();
    st.drawing.userPolylineVerts.insert(st.drawing.userPolylineVerts.end(), st.polylineDraftVerts.begin(), st.polylineDraftVerts.end());
    st.drawing.userPolylineOffsets.push_back(baseVert + static_cast<int>(nvert));
    st.drawing.userPolylineClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
    st.drawing.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
    // REQ-316 / ADR-047: carry the draft's per-vertex bulges into the store. Only materialise the
    // parallel array once a curved segment actually exists (a straight-only drawing keeps it empty
    // so its internal document payload re-saves byte-identically).
    bool draftHasArc = false;
    for (float b : st.polylineDraftBulge)
      if (b != 0.f) { draftHasArc = true; break; }
    if (draftHasArc || !st.drawing.userPolylineVertsBulge.empty()) {
      SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
      SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
      const size_t tail = st.drawing.userPolylineVertsBulge.size() >= nvert ? st.drawing.userPolylineVertsBulge.size() - nvert : 0;
      for (size_t k = 0; k < nvert && k < st.polylineDraftBulge.size(); ++k)
        st.drawing.userPolylineVertsBulge[tail + k] = st.polylineDraftBulge[k];
    }
  }
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  ResetPolylineDraft(st);
  log.push_back(closed ? "POLYLINE closed." : "POLYLINE complete.");
}
