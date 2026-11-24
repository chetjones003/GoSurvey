#include "CadRubberPreview.hpp"

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "CadDimStroke.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Geometry/geom2d.hpp>
#include "GoSurvey/Document/cadpiperun.hpp"  // CadBuildPipeRunSolids (issue #486 increment B2 / REQ-345)

#include <vector>

#include <algorithm>
#include <chrono>
#include <cmath>

void PushRubberSegViewRel(std::vector<float>& o, double x0, double y0, double x1, double y1, double /*anchorX*/,
                          double /*anchorY*/, float z0, float z1) {
  o.push_back(static_cast<float>(x0));
  o.push_back(static_cast<float>(y0));
  o.push_back(z0);
  o.push_back(static_cast<float>(x1));
  o.push_back(static_cast<float>(y1));
  o.push_back(z1);
}

void AppendWorldRectRubberViewRel(std::vector<float>& o, float xa, float ya, float xb, float yb, double anchorX,
                                  double anchorY, float z) {
  const float mnX = std::min(xa, xb);
  const float mxX = std::max(xa, xb);
  const float mnY = std::min(ya, yb);
  const float mxY = std::max(ya, yb);
  PushRubberSegViewRel(o, mnX, mnY, mxX, mnY, anchorX, anchorY, z, z);
  PushRubberSegViewRel(o, mxX, mnY, mxX, mxY, anchorX, anchorY, z, z);
  PushRubberSegViewRel(o, mxX, mxY, mnX, mxY, anchorX, anchorY, z, z);
  PushRubberSegViewRel(o, mnX, mxY, mnX, mnY, anchorX, anchorY, z, z);
}

namespace {

/// A curve as rubber-band segments, walked in the plane it will COMMIT into (REQ-312).
///
/// The preview and the commit now share their geometry: the caller solves the picks with the same
/// CadSolve* function the commit calls, and this only turns that answer into segments. What used to
/// be here -- a second circumcircle, a second sweep rule and a second XY tessellation -- was a
/// parallel implementation of the commit, and the file's own note at `commitCurX` explains what
/// that costs: a preview that draws a shape the commit does not produce.
void AppendCurveRubber(std::vector<float>& out, const ucs::Ucs& plane, double r, double startRad,
                       double sweepRad, float orthoHalfH, int fbHeightPx, int maxSegmentCap) {
  if (!(r > 1e-6))
    return;
  constexpr double kTwoPi = 6.283185307179586;
  // Sweep-scaled cap, so the chord-pixel target matches the cached arc/circle tessellation.
  const double sweepFrac = std::clamp(std::fabs(sweepRad) / kTwoPi, 0.05, 1.0);
  const int cap = std::max(8, static_cast<int>(std::ceil(static_cast<double>(maxSegmentCap) * sweepFrac)));
  const int n = std::max(8, CircleTessellationSegmentCount(r, static_cast<double>(orthoHalfH), fbHeightPx, cap));
  AppendCurveWorldSegs(out, plane, r, startRad, sweepRad, n);
}

/// The plane a solved circle lies in.
[[nodiscard]] ucs::Ucs CircleSolutionPlane(const CadCircleSolution& s) {
  return CurvePlane(static_cast<double>(s.cx), static_cast<double>(s.cy), static_cast<double>(s.cz),
                    static_cast<double>(s.nx), static_cast<double>(s.ny), static_cast<double>(s.nz));
}

/// A solved circle as rubber-band segments.
void AppendCircleSolutionRubber(std::vector<float>& out, const CadCircleSolution& s, float orthoHalfH,
                                int fbHeightPx, int maxSegmentCap) {
  constexpr double kTwoPi = 6.283185307179586;
  AppendCurveRubber(out, CircleSolutionPlane(s), static_cast<double>(s.r), 0.0, kTwoPi, orthoHalfH, fbHeightPx,
                    maxSegmentCap);
}

} // namespace

namespace {

/// The BASE outline of the solid being drawn, for the phases where the whole solid is not yet
/// determined — a cylinder's circle before its height is known, a box's rectangle before it is
/// raised, a pyramid's polygon as it turns with the cursor.
///
/// This is what AutoCAD shows at those prompts, and it is the half of the preview that cannot come
/// from `CadBuildSolidFromCommand`: there is no solid to build yet. The base is still fully
/// determined, so drawing it is honest — it is the part the cursor has decided.
void AppendSolidBaseProfile(const AppCommandState& cmd, std::vector<float>& out) {
  const auto* ss = cmd.sessionAs<stepengine::SolidPrimitiveSession>();
  const int picking = CadSolidCurrentPickParam(cmd);
  if (!ss || picking < 0 || !ss->solidPickValid)
    return;
  int specCount = 0;
  const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &specCount);
  const ucs::Ucs f = CadSolidPlacementFrameFor(cmd);
  const ray3d::Vec3 b = ss->solidBase;

  auto seg = [&](const ray3d::Vec3& p, const ray3d::Vec3& q) {
    PushRubberSegViewRel(out, p.x, p.y, q.x, q.y, 0., 0., static_cast<float>(p.z),
                         static_cast<float>(q.z));
  };
  auto inPlane = [&](double u, double v) {
    return ray3d::Add(b, ray3d::Add(ray3d::Scale(f.xAxis, u), ray3d::Scale(f.yAxis, v)));
  };

  if (specs[picking].pick == SolidPickKind::CornerXY) {
    const double dx = ss->solidPickA;
    const double dy = ss->solidPickB;
    const ray3d::Vec3 p00 = inPlane(0.0, 0.0);
    const ray3d::Vec3 p10 = inPlane(dx, 0.0);
    const ray3d::Vec3 p11 = inPlane(dx, dy);
    const ray3d::Vec3 p01 = inPlane(0.0, dy);
    seg(p00, p10);
    seg(p10, p11);
    seg(p11, p01);
    seg(p01, p00);
    return;
  }
  if (specs[picking].pick != SolidPickKind::Radius)
    return;

  const double r = ss->solidPickA;
  if (!(r > 1e-9))
    return;
  int sides = 0;
  double startAngle = 0.0;
  if (ss->solidKind == brep::PrimitiveKind::Pyramid) {
    sides = ss->solidParamSet[0] ? static_cast<int>(ss->solidParamValue[0]) : 4;
    if (sides < 3 || sides > brep::kMaxPyramidSides)
      sides = 4;
    startAngle = ss->solidPickAngleRad;
  } else {
    sides = 64;
  }
  double drawR = r;
  if (ss->solidKind == brep::PrimitiveKind::Pyramid && !ss->solidInscribed) {
    const double k = std::cos(3.14159265358979323846 / static_cast<double>(sides));
    if (k > 1e-9)
      drawR = r / k;
  }
  ray3d::Vec3 prev = inPlane(drawR * std::cos(startAngle), drawR * std::sin(startAngle));
  for (int i = 1; i <= sides; ++i) {
    const double a = startAngle + 2.0 * 3.14159265358979323846 * static_cast<double>(i) /
                                      static_cast<double>(sides);
    const ray3d::Vec3 cur = inPlane(drawR * std::cos(a), drawR * std::sin(a));
    seg(prev, cur);
    prev = cur;
  }
}

} // namespace

void AppendCadDraftRubberLines(AppCommandState& cmd, double curX, double curY, bool orthoEnabled,
                               double /*viewAnchorX*/, double /*viewAnchorY*/, float orthoHalfH, int fbHeightPx,
                               std::vector<float>& rubberLines) {
  const float curXf = static_cast<float>(curX);
  const float curYf = static_cast<float>(curY);
  // Preview the same dimension geometry the command will commit. Keeping this in the shared draft
  // pass ensures cursor movement, snap results, and rubber-band rendering use the same point.
  CadAnnotation dimensionDraft;
  bool hasDimensionDraft = false;
  switch (cmd.active) {
    case AppCommandState::Kind::DimLinear:
      hasDimensionDraft = CadDimLinearBuildDraft(cmd, curXf, curYf, &dimensionDraft);
      break;
    case AppCommandState::Kind::DimAligned:
      hasDimensionDraft = CadDimAlignedBuildDraft(cmd, curXf, curYf, &dimensionDraft);
      break;
    case AppCommandState::Kind::DimAngular:
      hasDimensionDraft = CadDimAngularBuildDraft(cmd, curXf, curYf, &dimensionDraft);
      break;
    default:
      break;
  }
  if (hasDimensionDraft) {
    CadDimStrokeParams params;
    params.modelUnitsPerPlottedInch = cmd.drawing.modelUnitsPerPlottedInch;
    params.arrowSizeInches = cmd.drawing.activeDimensionStyle.arrowSizeInches;
    params.arrowScale = cmd.viewportDimArrowScale;
    params.arrowType = cmd.drawing.activeDimensionStyle.arrowType;
    CadDimWorldStrokes strokes;
    if (CadDimBuildWorldStrokes(dimensionDraft, params, &strokes)) {
      for (const auto& s : strokes.segs)
        PushRubberSegViewRel(rubberLines, s.x0, s.y0, s.x1, s.y1, 0., 0., dimensionDraft.insZ,
                             dimensionDraft.insZ);
      for (const auto& a : strokes.arrows) {
        PushRubberSegViewRel(rubberLines, a.x0, a.y0, a.x1, a.y1, 0., 0., dimensionDraft.insZ,
                             dimensionDraft.insZ);
        PushRubberSegViewRel(rubberLines, a.x1, a.y1, a.x2, a.y2, 0., 0., dimensionDraft.insZ,
                             dimensionDraft.insZ);
        PushRubberSegViewRel(rubberLines, a.x2, a.y2, a.x0, a.y0, 0., 0., dimensionDraft.insZ,
                             dimensionDraft.insZ);
      }
    }
  }
  // The elevation this command will COMMIT at (REQ-058). A rubber drawn on the datum while the
  // geometry lands on the work plane reads as correct in plan view and is plainly wrong the moment
  // the view tilts — the same class of defect as TASK-035 DEFECT-C.
  //
  // LINE and POLYLINE take their far end from here and their near end from cmd.anchorZ, because a
  // segment genuinely spans two elevations. ARC and ELLIPSE are planar entities committed at ONE
  // elevation (CadArc::z / CadEllipse::z), and their draft state keeps no Z for the first pick, so
  // both ends of their construction rubber sit on the plane the entity will land on.
  const float zc = CadCommitElevation(cmd);
  // Whether the work plane is world XY (REQ-312). Under it, every construction rubber keeps the
  // single-elevation behaviour above; off it, a pick's own Z is the only thing that says where on
  // the plane it sat.
  const bool planeIsFlat = CadWorkPlaneIsWorldXy(cmd);

  // REQ-485.2: at the Reverse prompt, an arrow along each piece shows which way the alignment will run, as Civil 3D
  // draws them. Sized to the view so they read at any zoom.
  if (cmd.active == AppCommandState::Kind::AlignmentFromObjects) {
    alignment::Alignment preview;
    if (AlignmentFromObjectsPreview(cmd, &preview)) {
      const double s = std::max(1e-6, static_cast<double>(orthoHalfH) * 0.035);
      for (size_t i = 0; i < preview.Entities().size(); ++i) {
        const double mid = preview.EntityStartStation(static_cast<int>(i)) + preview.Entities()[i].length * 0.5;
        const alignment::Point2 p = preview.GetPointAtStation(mid);
        const double az = preview.GetBearingAtStation(mid);
        const double dx = std::sin(az) * s, dy = std::cos(az) * s;  // along travel
        const double nx = std::cos(az) * s * 0.6, ny = -std::sin(az) * s * 0.6;  // across it
        const double tx = p.x + dx, ty = p.y + dy;
        PushRubberSegViewRel(rubberLines, p.x - dx, p.y - dy, tx, ty, 0., 0., zc, zc);
        PushRubberSegViewRel(rubberLines, p.x - dx * 0.2 + nx, p.y - dy * 0.2 + ny, tx, ty, 0., 0., zc, zc);
        PushRubberSegViewRel(rubberLines, p.x - dx * 0.2 - nx, p.y - dy * 0.2 - ny, tx, ty, 0., 0., zc, zc);
      }
    }
  }

  // REQ-487.1: dragging an alignment grip, a rubber line from where the grip was to the cursor, as Civil 3D draws.
  if (cmd.entityGripMoveActive && cmd.entityGripType == SelectedEntity::Type::Alignment)
    PushRubberSegViewRel(rubberLines, cmd.entityGripAnchorX, cmd.entityGripAnchorY, curX, curY, 0., 0., zc, zc);

  // REQ-485.1: the next tangent of an alignment layout, from the last PI to the cursor (along the starting direction
  // when the dialog fixed one).
  if (const auto* al = cmd.sessionAs<stepengine::AlignmentLayoutSession>(); al && !al->pis.empty()) {
    const alignment::Point2 last = al->pis.back();
    double tx = curX, ty = curY;
    if (al->fixedDirection && al->pis.size() == 1) {
      const double ux = std::sin(al->directionAzimuth), uy = std::cos(al->directionAzimuth);
      const double t = std::max(0.0, (curX - last.x) * ux + (curY - last.y) * uy);
      tx = last.x + ux * t;
      ty = last.y + uy * t;
    }
    PushRubberSegViewRel(rubberLines, last.x, last.y, tx, ty, 0., 0., zc, zc);
  }

  if (const auto* ls = cmd.sessionAs<stepengine::LineSession>();
      ls && ls->phase == stepengine::LinePhase::NeedNextPoint) {
    using SAP = stepengine::SegmentAnglePickPhase;
    if (ls->angle.pickPhase == SAP::WaitP2)
      PushRubberSegViewRel(rubberLines, ls->angle.pickRefX1, ls->angle.pickRefY1, curXf, curYf, 0., 0.,
                           zc, zc);
    else if (ls->angle.pickPhase == SAP::WaitAdjustOrCommit) {
      const float th = MathAngleRadFromBearingCwNorthDeg(ls->angle.pickDraftBearingDeg);
      const float ux = std::cos(th);
      const float uy = std::sin(th);
      float lx = curXf;
      float ly = curYf;
      ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0., cmd.anchorZ,
                           zc);
    } else {
      float lx = curXf;
      float ly = curYf;
      float lz = zc;
      if (ls->angle.lockActive)
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ls->angle.lockUx, ls->angle.lockUy, &lx, &ly,
                                         false);
      else
        // lz picks up the ortho-locked Z (issue #371 follow-up) when squaring to a Front/Left/Right-
        // style UCS's vertical axis — without it the preview kept rendering at the cursor's raw
        // (unlocked) elevation even though lx/ly reported a squared point.
        ApplyOrthoConstrainFromAnchor(cmd, cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled, cmd.anchorZ,
                                      cmd.uiCursorWorldZ, &lz);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0., cmd.anchorZ,
                           lz);  // preview at the elevation it will commit to
    }
  }

  if (const auto* pls = cmd.sessionAs<stepengine::PolylineSession>();
      pls && pls->phase == stepengine::PolylinePhase::NeedNextPoint) {
    float lx = curXf;
    float ly = curYf;
    using SAP = stepengine::SegmentAnglePickPhase;
    const auto& d = pls->draftVerts;
    // REQ-316 / ADR-047: preview an arc-mode segment as a curve, not a chord — both the segments
    // already committed to the draft and the one being rubber-banded to the cursor.
    auto pushMaybeArc = [&](double x0, double y0, double x1, double y1, double z0, double z1, float bulge) {
      const BulgeArcSpan a = (bulge != 0.f) ? BulgeArc(x0, y0, x1, y1, static_cast<double>(bulge)) : BulgeArcSpan{};
      if (!a.valid) {
        PushRubberSegViewRel(rubberLines, x0, y0, x1, y1, 0., 0., static_cast<float>(z0), static_cast<float>(z1));
        return;
      }
      constexpr double kPi = 3.14159265358979323846;
      const int n = std::clamp(static_cast<int>(std::ceil(std::fabs(a.sweep) / (kPi / 24.0))), 2, 96);
      double px = x0, py = y0;
      for (int s = 1; s <= n; ++s) {
        const double u = a.startAngle + a.sweep * (static_cast<double>(s) / n);
        const double qx = a.cx + a.radius * std::cos(u);
        const double qy = a.cy + a.radius * std::sin(u);
        PushRubberSegViewRel(rubberLines, px, py, qx, qy, 0., 0., static_cast<float>(z0), static_cast<float>(z1));
        px = qx;
        py = qy;
      }
    };
    for (size_t i = 0, seg = 0; i + 5 < d.size(); i += 3, ++seg) {
      const float b = seg < pls->draftBulge.size() ? pls->draftBulge[seg] : 0.f;
      pushMaybeArc(d[i], d[i + 1], d[i + 3], d[i + 4], d[i + 2], d[i + 5], b);
    }

    if (pls->angle.pickPhase == SAP::WaitP2)
      PushRubberSegViewRel(rubberLines, pls->angle.pickRefX1, pls->angle.pickRefY1, curXf, curYf, 0., 0.,
                           zc, zc);
    else if (pls->angle.pickPhase == SAP::WaitAdjustOrCommit) {
      const float th = MathAngleRadFromBearingCwNorthDeg(pls->angle.pickDraftBearingDeg);
      const float ux = std::cos(th);
      const float uy = std::sin(th);
      lx = curXf;
      ly = curYf;
      ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0., cmd.anchorZ,
                           zc);
    } else {
      float lz = zc;
      if (pls->angle.lockActive)
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, pls->angle.lockUx, pls->angle.lockUy, &lx, &ly,
                                         false);
      else
        // lz picks up the ortho-locked Z (issue #371 follow-up) — see the LINE branch above.
        ApplyOrthoConstrainFromAnchor(cmd, cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled, cmd.anchorZ,
                                      cmd.uiCursorWorldZ, &lz);
      // REQ-316 / ADR-047: in ARC mode the rubber-band to the cursor previews the pending arc,
      // computed by the SAME function the commit uses so what is drawn is what will be committed.
      const float pendBulge =
          pls->arcMode ? CadPolylineDraftBulgeForNextPoint(cmd, lx, ly) : 0.f;
      pushMaybeArc(cmd.anchorX, cmd.anchorY, lx, ly, cmd.anchorZ, lz, pendBulge);
    }
  }

  // FEATURELINE (REQ-087 / TASK-082 BUG-2): the draft had NO preview at all — nothing in
  // viewport/, render/ or ui/ read featureLineDraftVerts. That was survivable while the only way to
  // place a point was to type it; once a click places one, drawing with no preview means picking
  // blind, so the click fix is not worth having without this.
  //
  // Each segment is drawn at its vertices' own elevations, as the polyline draft above is, so a
  // feature line being drawn up a grade previews as the 3D chain it will become rather than flat.
  if (const auto* fls = cmd.sessionAs<stepengine::FeatureLineSession>()) {
    const auto& d = fls->draftVerts;
    for (size_t i = 0; i + 5 < d.size(); i += 3)
      PushRubberSegViewRel(rubberLines, d[i], d[i + 1], d[i + 3], d[i + 4], 0., 0., d[i + 2], d[i + 5]);

    if (!d.empty()) {
      const float ax = d[d.size() - 3];
      const float ay = d[d.size() - 2];
      const float az = d[d.size() - 1];
      if (fls->pendingPoint) {
        PushRubberSegViewRel(rubberLines, ax, ay, fls->pendingX, fls->pendingY,
                             0., 0., az, fls->pendingDefaultZ);
      } else {
        float lx = curXf;
        float ly = curYf;
        float lz = zc;
        ApplyOrthoConstrainFromAnchor(cmd, ax, ay, &lx, &ly, orthoEnabled, az, cmd.uiCursorWorldZ, &lz);
        PushRubberSegViewRel(rubberLines, ax, ay, lx, ly, 0., 0., az, lz);
      }
    }
  }

  // RECT (REQ-053): after the first corner, rubber-band the axis-aligned rectangle to the cursor. ORTHO is
  // deliberately NOT applied — it would collapse the rectangle to a line, and the shape is already
  // axis-aligned, which is what ORTHO is for.
  if (const auto* rs = cmd.sessionAs<stepengine::RectSession>();
      rs && rs->phase == stepengine::RectPhase::WaitSecondCorner)
    AppendWorldRectRubberViewRel(rubberLines, rs->x1, rs->y1, curXf, curYf, 0., 0., zc);

  if (const auto* as = cmd.sessionAs<stepengine::ArcSession>()) {
    using AP = stepengine::ArcPhase;
    if (as->phase == AP::WaitMid)
      // On a tilted work plane the first pick's own elevation is not the cursor's: two picks on a
      // wall differ only in height, and drawing both ends at zc would flatten the construction line
      // onto one contour of the wall.
      PushRubberSegViewRel(rubberLines, as->ax, as->ay, curXf, curYf, 0., 0.,
                           planeIsFlat ? zc : as->az, zc);
    else if (as->phase == AP::WaitEnd) {
      CadArc a{};
      if (CadSolveArcThreePoints(cmd, as->ax, as->ay, as->az, as->bx, as->by, as->bz, curXf,
                                 curYf, zc, &a))
        AppendCurveRubber(rubberLines, CurvePlane(a), static_cast<double>(a.r), static_cast<double>(a.startRad),
                          static_cast<double>(a.sweepRad), orthoHalfH, fbHeightPx,
                          cmd.displayArcCircleSmoothness);
    }
  }

  if (const auto* es = cmd.sessionAs<stepengine::EllipseSession>();
      es && es->phase == stepengine::EllipsePhase::WaitMajorEnd)
    PushRubberSegViewRel(rubberLines, es->cx, es->cy, curXf, curYf, 0., 0., zc, zc);

  if (const auto* ds = cmd.sessionAs<stepengine::DimSession>();
      ds && ds->phase == stepengine::DimPhase::WaitExt2)
    PushRubberSegViewRel(rubberLines, ds->e1x, ds->e1y, curXf, curYf, 0., 0., zc, zc);

  if (const auto* is = cmd.sessionAs<stepengine::SurveyInverseSession>();
      is && is->phase == stepengine::SurveyInversePhase::WaitTo)
    PushRubberSegViewRel(rubberLines, is->fromX, is->fromY, curXf, curYf, 0., 0., zc, zc);

  if (const auto* dists = cmd.sessionAs<stepengine::DistSession>();
      dists && dists->phase == stepengine::DistPhase::WaitTo)
    PushRubberSegViewRel(rubberLines, dists->fromX, dists->fromY, curXf, curYf, 0., 0., dists->fromZ, zc);

  if (const auto* das = cmd.sessionAs<stepengine::DimAngularSession>()) {
    using DAP = stepengine::DimAngularPhase;
    if (das->phase == DAP::WaitRay1)
      PushRubberSegViewRel(rubberLines, das->vx, das->vy, curXf, curYf, 0., 0., zc, zc);
    else if (das->phase == DAP::WaitRay2) {
      PushRubberSegViewRel(rubberLines, das->vx, das->vy, das->e1x, das->e1y, 0., 0., zc, zc);
      PushRubberSegViewRel(rubberLines, das->vx, das->vy, curXf, curYf, 0., 0., zc, zc);
    } else if (das->phase == DAP::WaitArc) {
      PushRubberSegViewRel(rubberLines, das->vx, das->vy, das->e1x, das->e1y, 0., 0., zc, zc);
      PushRubberSegViewRel(rubberLines, das->vx, das->vy, das->e2x, das->e2y, 0., 0., zc, zc);
    }
  }

  if (const auto* cs = cmd.sessionAs<stepengine::CircleSession>()) {
    using CP = stepengine::CirclePhase;
    if (cs->phase == CP::WaitRadius) {
      // Solved by the function CIRCLE itself commits with, so the ring under the cursor is the ring
      // that lands -- including the radius, which on a tilted plane is the 3D distance to the rim
      // pick and not its XY projection (REQ-312).
      AppendCircleSolutionRubber(
          rubberLines,
          CadSolveCircleFromRimPick(cmd, cs->cx, cs->cy, cs->cz, curXf, curYf, zc), orthoHalfH,
          fbHeightPx, cmd.displayArcCircleSmoothness);
    } else if (cs->phase == CP::ThreeP_WaitP2) {
      // Two picks so far, so the preview is the circle on the diameter between them: centre at their
      // midpoint IN SPACE, radius the distance from there to the cursor. Routed through the rim-pick
      // solver, which is where the work-plane normal and the 3D radius come from.
      const float mx = (cs->c3p1x + curXf) * 0.5f;
      const float my = (cs->c3p1y + curYf) * 0.5f;
      const float mz = (cs->c3p1z + zc) * 0.5f;
      const CadCircleSolution s = CadSolveCircleFromRimPick(cmd, mx, my, mz, curXf, curYf, zc);
      if (s.r > 1e-6f)
        AppendCircleSolutionRubber(rubberLines, s, orthoHalfH, fbHeightPx, cmd.displayArcCircleSmoothness);
    } else if (cs->phase == CP::ThreeP_WaitP3) {
      CadCircleSolution s;
      if (CadSolveCircleThreePoints(cmd, cs->c3p1x, cs->c3p1y, cs->c3p1z, cs->c3p2x, cs->c3p2y, cs->c3p2z, curXf,
                                    curYf, zc, &s))
        AppendCircleSolutionRubber(rubberLines, s, orthoHalfH, fbHeightPx, cmd.displayArcCircleSmoothness);
    }
  }

  if (const auto* mls = cmd.sessionAs<stepengine::MleaderSession>()) {
    using MLp = stepengine::MleaderPhase;
    if (mls->phase == MLp::WaitLanding)
      PushRubberSegViewRel(rubberLines, mls->tipX, mls->tipY, curXf, curYf, 0., 0., mls->tipZ,
                           zc);
    else if (mls->phase == MLp::WaitLabel && cmd.drawing.cadMultileaders.size() > 0) {
      const CadMultileader& ml = cmd.drawing.cadMultileaders.back();
      if (ml.pathXyz.size() >= 6)
        PushRubberSegViewRel(rubberLines, ml.pathXyz[0], ml.pathXyz[1], ml.pathXyz[3], ml.pathXyz[4], 0., 0.,
                             ml.pathXyz[2], ml.pathXyz[5]);
    }
  }

  if (const auto* mts = cmd.sessionAs<stepengine::MtextSession>()) {
    using MPtxt = stepengine::MtextPhase;
    if (mts->phase == MPtxt::WaitCorner2) {
      float lx = curXf;
      float ly = curYf;
      // A box needs both coordinates; ORTHO constrains segments, not opposite corners.
      AppendWorldRectRubberViewRel(rubberLines, mts->x1, mts->y1, lx, ly, 0., 0., zc);
    } else if (mts->phase == MPtxt::WaitString)
      AppendWorldRectRubberViewRel(rubberLines, mts->x1, mts->y1, mts->x2, mts->y2, 0., 0.,
                                   zc);
  }

  // UCS axis preview (REQ-154). At the two axis prompts, draw the frame the cursor would produce —
  // the rubber from the origin out to the cursor, plus the perpendicular the frame implies — so the
  // user sees the resulting axes before committing rather than after.
  //
  // The stores are LOCAL, the UCS is WORLD (see CadActiveUcsStorage), so the pending picks are
  // converted down here rather than the rubber being built in world and converted wholesale.
  if (const auto* us = cmd.sessionAs<stepengine::UcsSession>()) {
    using UPh = stepengine::UcsPhase;
    auto localOf = [&](const ray3d::Vec3& world, float* lx, float* ly) {
      CadCoord::LocalFromWorld(cmd, world.x, world.y, lx, ly);
    };
    if (us->phase == UPh::WaitXAxisPoint || us->phase == UPh::WaitXyPoint) {
      float ox = 0.f, oy = 0.f;
      localOf(us->pendingOrigin, &ox, &oy);
      if (us->phase == UPh::WaitXAxisPoint) {
        // One rubber: origin to cursor, which IS the X axis being defined.
        PushRubberSegViewRel(rubberLines, ox, oy, curXf, curYf, 0., 0., zc, zc);
      } else {
        // The X axis is settled, so it is drawn at its own length from the origin. The second arm
        // is the Y AXIS THE CURSOR IS CHOOSING — not a rubber to the cursor itself.
        //
        // That distinction is the whole point: FromThreePoints takes the PERPENDICULAR component of
        // the third pick as +Y, so the frame that commits depends only on which SIDE of the X axis
        // the cursor is on, not how far along it. Drawing a line to the cursor would show an arm
        // that swings as the cursor slides parallel to X while the committed frame does not move at
        // all, and would give no hint that crossing the axis flips the frame over. Projecting out
        // the perpendicular makes the preview flip exactly when the result flips.
        float xx = 0.f, xy = 0.f;
        localOf(us->pendingXAxisPoint, &xx, &xy);
        const float axx = xx - ox, axy = xy - oy;
        const float alen = std::hypot(axx, axy);
        PushRubberSegViewRel(rubberLines, ox, oy, xx, xy, 0., 0., zc, zc);
        if (alen > 1e-9f) {
          const float ux = axx / alen, uy = axy / alen;
          const float vx = curXf - ox, vy = curYf - oy;
          // Component of the cursor offset perpendicular to X: this is +Y, sign included.
          const float dot = vx * ux + vy * uy;
          float px = vx - dot * ux, py = vy - dot * uy;
          const float plen = std::hypot(px, py);
          // Dead on the axis defines no plane, so nothing is drawn rather than an arbitrary arm —
          // the same case FromThreePoints refuses as collinear.
          if (plen > 1e-6f) {
            px /= plen;
            py /= plen;
            // Same length as the X arm, so the two read as one frame rather than as a long axis and
            // a rubber band that happens to be near it.
            PushRubberSegViewRel(rubberLines, ox, oy, ox + px * alen, oy + py * alen, 0., 0., zc, zc);
          }
        }
      }
    } else if (us->phase == UPh::WaitRotationAngleP1 || us->phase == UPh::WaitRotationAngleP2) {
      // `2P`: nothing to draw until the first point is down, then the rubber IS the direction whose
      // angle is being measured.
      if (us->phase == UPh::WaitRotationAngleP2) {
        float bx = 0.f, by = 0.f;
        localOf(us->angleBasePoint, &bx, &by);
        PushRubberSegViewRel(rubberLines, bx, by, curXf, curYf, 0., 0., zc, zc);
      }
    } else if (us->phase == UPh::WaitZAxisPoint) {
      float ox = 0.f, oy = 0.f;
      localOf(us->pendingOrigin, &ox, &oy);
      PushRubberSegViewRel(rubberLines, ox, oy, curXf, curYf, 0., 0., zc, zc);
    }
  }

  if (const auto* pdfSess = cmd.sessionAs<stepengine::PdfAttachSession>();
      pdfSess && pdfSess->phase == stepengine::PdfAttachPhase::WaitInsertPoint &&
      cmd.pdfAttachPreviewReady && cmd.pdfAttachPreview.pageWidthPts > 0.f &&
      cmd.pdfAttachPreview.pageHeightPts > 0.f) {
    const float cosR = std::cos(cmd.pdfAttachRotDeg * 3.14159265f / 180.f);
    const float sinR = std::sin(cmd.pdfAttachRotDeg * 3.14159265f / 180.f);
    const float W = cmd.pdfAttachPreview.pageWidthPts * cmd.pdfAttachScale;
    const float H = cmd.pdfAttachPreview.pageHeightPts * cmd.pdfAttachScale;
    const float corners[4][2] = {{0.f, 0.f}, {W, 0.f}, {W, H}, {0.f, H}};
    float xs[4]{};
    float ys[4]{};
    for (int i = 0; i < 4; ++i) {
      const float px = corners[i][0];
      const float py = corners[i][1];
      xs[i] = curXf + px * cosR - py * sinR;
      ys[i] = curYf + px * sinR + py * cosR;
    }
    for (int i = 0; i < 4; ++i) {
      const int j = (i + 1) % 4;
      PushRubberSegViewRel(rubberLines, xs[i], ys[i], xs[j], ys[j], 0., 0., zc, zc);
    }
  }

  if (const auto* is = cmd.sessionAs<stepengine::InsertBlockSession>()) {
    using IPh = stepengine::InsertBlockPhase;
    if (is->phase == IPh::WaitInsertPoint || is->phase == IPh::WaitScale ||
        is->phase == IPh::WaitRotation || is->phase == IPh::WaitAlignFace) {
      float pickZ = zc;
      if (is->phase == IPh::WaitInsertPoint || is->phase == IPh::WaitScale) {
        pickZ = cmd.uiCursorWorldZ;
        if (cmd.viewportSnapPickValid)
          pickZ = cmd.viewportSnapPickLocalZ;
      }
      // Drag indicator from the fixed insertion point to the cursor during scale/rotation/align picks.
      if (is->phase != IPh::WaitInsertPoint) {
        float lx = curXf;
        float ly = curYf;
        float lz = pickZ;
        ApplyOrthoConstrainFromAnchor(cmd, cmd.insertBlockX, cmd.insertBlockY, &lx, &ly, orthoEnabled,
                                      cmd.insertBlockZ, cmd.uiCursorWorldZ, &lz);
        PushRubberSegViewRel(rubberLines, cmd.insertBlockX, cmd.insertBlockY, lx, ly, 0., 0.,
                             cmd.insertBlockZ, lz);
      }
      // Live ghost: 2D linework + B-rep wireframe edges at the committed transform (issue #475 inc3).
      CadBlockXform gxf;
      if (CadBlockInsertPreviewXform(cmd, curXf, curYf, pickZ, &gxf))
        AppendInsertBlockGhostRubber(cmd, gxf, rubberLines);
    }
  }

  // --- The prompted solid primitives (REQ-313 as amended) -----------------------------------------
  //
  // The whole candidate solid, drawn as its own wireframe, rebuilt from the cursor every frame. Not
  // a bespoke ghost per primitive: `CadBuildSolidFromCommand` is the same function the click and
  // Enter commit through, and `brep::TessellateEdges` is the same edge walk the finished solid is
  // drawn with. So the preview cannot show a shape the commit would not build, and it cannot draw
  // its edges by a different rule than the real thing — the two failure modes a hand-written ghost
  // has, and the reason there is no hand-written ghost here.
  //
  // A solid that does not build yet — a radius still zero before the cursor has moved — draws
  // nothing, which is the honest answer while a value is still being chosen.
  if (const auto* ss = cmd.sessionAs<stepengine::SolidPrimitiveSession>();
      ss && ss->phase == stepengine::SolidPhase::WaitParameters) {
    brep::Solid ghost;
    brep::Problem why = brep::Problem::Ok;
    if (!CadBuildSolidFromCommand(cmd, /*applyPick=*/true, &ghost, &why)) {
      AppendSolidBaseProfile(cmd, rubberLines);
    } else {
      std::vector<double> segs;
      if (brep::TessellateEdges(ghost, kSolidChordToleranceFt, &segs, &why)) {
        for (std::size_t i = 0; i + 5 < segs.size(); i += 6)
          PushRubberSegViewRel(rubberLines, segs[i], segs[i + 1], segs[i + 3], segs[i + 4], 0., 0.,
                               static_cast<float>(segs[i + 2]), static_cast<float>(segs[i + 5]));
      }
    }
    const int picking = CadSolidCurrentPickParam(cmd);
    if (ss->solidPickValid && picking >= 0) {
      int specCount = 0;
      const SolidParamSpec* specs = CadSolidParamSpecs(ss->solidKind, &specCount);
      const ucs::Ucs f = CadSolidPlacementFrameFor(cmd);
      const ray3d::Vec3 b = ss->solidBase;
      ray3d::Vec3 tip = b;
      if (specs[picking].pick == SolidPickKind::Radius) {
        tip = ray3d::Add(b, ray3d::Scale(ray3d::Add(ray3d::Scale(f.xAxis, std::cos(ss->solidPickAngleRad)),
                                                    ray3d::Scale(f.yAxis, std::sin(ss->solidPickAngleRad))),
                                         ss->solidPickA));
      } else if (specs[picking].pick == SolidPickKind::Height) {
        tip = ray3d::Add(b, ray3d::Scale(f.zAxis, ss->solidPickA));
      } else {
        tip = ray3d::Add(b, ray3d::Add(ray3d::Scale(f.xAxis, ss->solidPickA),
                                       ray3d::Scale(f.yAxis, ss->solidPickB)));
      }
      PushRubberSegViewRel(rubberLines, b.x, b.y, tip.x, tip.y, 0., 0., static_cast<float>(b.z),
                           static_cast<float>(tip.z));
    }
  }

  if (const auto* es = cmd.sessionAs<stepengine::ExtrudeSession>();
      es && es->phase == stepengine::ExtrudePhase::WaitHeight && es->heightPickValid) {
    std::vector<brep::Solid> ghosts;
    if (CadBuildExtrudeSolids(cmd, es->heightPick, &ghosts)) {
      for (const brep::Solid& g : ghosts) {
        std::vector<double> segs;
        brep::Problem why = brep::Problem::Ok;
        if (brep::TessellateEdges(g, kSolidChordToleranceFt, &segs, &why)) {
          for (std::size_t i = 0; i + 5 < segs.size(); i += 6)
            PushRubberSegViewRel(rubberLines, segs[i], segs[i + 1], segs[i + 3], segs[i + 4], 0., 0.,
                                 static_cast<float>(segs[i + 2]), static_cast<float>(segs[i + 5]));
        }
      }
    }
    if (!es->profiles.empty()) {
      const ucs::Ucs& pl = es->profiles.front().plane;
      const ray3d::Vec3 tip = ray3d::Add(pl.origin, ray3d::Scale(pl.zAxis, es->heightPick));
      PushRubberSegViewRel(rubberLines, pl.origin.x, pl.origin.y, tip.x, tip.y, 0., 0.,
                           static_cast<float>(pl.origin.z), static_cast<float>(tip.z));
    }
  }

  if (const auto* pps = cmd.sessionAs<stepengine::PressPullSession>();
      pps && pps->phase == stepengine::PressPullPhase::WaitDistance && pps->distPickValid) {
    brep::Solid ghost;
    if (CadBuildPressPullSolid(cmd, pps->distPick, &ghost)) {
      std::vector<double> segs;
      brep::Problem why = brep::Problem::Ok;
      if (brep::TessellateEdges(ghost, kSolidChordToleranceFt, &segs, &why)) {
        for (std::size_t i = 0; i + 5 < segs.size(); i += 6)
          PushRubberSegViewRel(rubberLines, segs[i], segs[i + 1], segs[i + 3], segs[i + 4], 0., 0.,
                               static_cast<float>(segs[i + 2]), static_cast<float>(segs[i + 5]));
      }
    }
    ray3d::Vec3 anchor{};
    ray3d::Vec3 axis{};
    bool haveAxis = false;
    if (pps->onFace) {
      haveAxis = CadSubObjectFaceGrip(cmd, pps->face, &anchor, &axis);
    } else {
      anchor = pps->profile.plane.origin;
      axis = pps->profile.plane.zAxis;
      haveAxis = true;
    }
    if (haveAxis) {
      const ray3d::Vec3 tip = ray3d::Add(anchor, ray3d::Scale(axis, pps->distPick));
      PushRubberSegViewRel(rubberLines, anchor.x, anchor.y, tip.x, tip.y, 0., 0.,
                           static_cast<float>(anchor.z), static_cast<float>(tip.z));
    }
  }

  if (const auto* rvs = cmd.sessionAs<stepengine::RevolveSession>()) {
    if (rvs->axisStartSet && rvs->phase != stepengine::RevolvePhase::WaitAxisStart) {
      const ray3d::Vec3& a = rvs->axisStart;
      const ray3d::Vec3& b = rvs->axisEnd;
      PushRubberSegViewRel(rubberLines, a.x, a.y, b.x, b.y, 0., 0., static_cast<float>(a.z),
                           static_cast<float>(b.z));
    }
    if (rvs->phase == stepengine::RevolvePhase::WaitAngle) {
      std::vector<brep::Solid> ghosts;
      if (CadBuildRevolveSolids(cmd, rvs->angleDeg, &ghosts)) {
        for (const brep::Solid& g : ghosts) {
          std::vector<double> segs;
          brep::Problem why = brep::Problem::Ok;
          if (brep::TessellateEdges(g, kSolidChordToleranceFt, &segs, &why)) {
            for (std::size_t i = 0; i + 5 < segs.size(); i += 6)
              PushRubberSegViewRel(rubberLines, segs[i], segs[i + 1], segs[i + 3], segs[i + 4], 0., 0.,
                                   static_cast<float>(segs[i + 2]), static_cast<float>(segs[i + 5]));
          }
        }
      }
    }
  }

  if (const auto* ls = cmd.sessionAs<stepengine::LoftSession>();
      ls && ls->phase == stepengine::LoftPhase::SelectProfiles) {
    brep::Solid g;
    if (CadBuildLoftSolid(cmd, &g)) {
      std::vector<double> segs;
      brep::Problem why = brep::Problem::Ok;
      if (brep::TessellateEdges(g, kSolidChordToleranceFt, &segs, &why)) {
        for (std::size_t i = 0; i + 5 < segs.size(); i += 6)
          PushRubberSegViewRel(rubberLines, segs[i], segs[i + 1], segs[i + 3], segs[i + 4], 0., 0.,
                               static_cast<float>(segs[i + 2]), static_cast<float>(segs[i + 5]));
      }
    }
  }

  if (const auto* sws = cmd.sessionAs<stepengine::SweepSession>();
      sws && sws->phase == stepengine::SweepPhase::SelectInputs) {
    brep::Solid g;
    if (CadBuildSweepSolid(cmd, &g)) {
      std::vector<double> segs;
      brep::Problem why = brep::Problem::Ok;
      if (brep::TessellateEdges(g, kSolidChordToleranceFt, &segs, &why)) {
        for (std::size_t i = 0; i + 5 < segs.size(); i += 6)
          PushRubberSegViewRel(rubberLines, segs[i], segs[i + 1], segs[i + 3], segs[i + 4], 0., 0.,
                               static_cast<float>(segs[i + 2]), static_cast<float>(segs[i + 5]));
      }
    }
  }

  if (const auto* sls = cmd.sessionAs<stepengine::SliceSession>()) {
    using SP = stepengine::SlicePhase;
    auto seg = [&](const ray3d::Vec3& a, const ray3d::Vec3& b) {
      PushRubberSegViewRel(rubberLines, a.x, a.y, b.x, b.y, 0., 0., static_cast<float>(a.z),
                           static_cast<float>(b.z));
    };
    if (sls->phase == SP::WaitP3 || sls->phase == SP::WaitKeepSide)
      seg(sls->p1, sls->p2);
    if (sls->phase == SP::WaitKeepSide) {
      seg(sls->p2, sls->p3);
      seg(sls->p3, sls->p1);
    }
  }

  if (const auto* ps = cmd.sessionAs<stepengine::PolysolidSession>();
      ps && ps->phase == stepengine::PolysolidPhase::WaitNextPoint) {
    const ucs::Point2D cursor = ucs::WorldToPlane(
        CadPolysolidFrameFor(cmd), ray3d::Vec3{curX, curY, CadCommitElevation(cmd)});
    brep::Solid ghost;
    brep::Problem why = brep::Problem::Ok;
    if (CadBuildPolysolidFromCommand(cmd, &cursor, &ghost, &why)) {
      std::vector<double> segs;
      if (brep::TessellateEdges(ghost, kSolidChordToleranceFt, &segs, &why)) {
        for (std::size_t i = 0; i + 5 < segs.size(); i += 6)
          PushRubberSegViewRel(rubberLines, segs[i], segs[i + 1], segs[i + 3], segs[i + 4], 0., 0.,
                               static_cast<float>(segs[i + 2]), static_cast<float>(segs[i + 5]));
      }
    }
  }

  if (const auto* rcs = cmd.sessionAs<stepengine::RevcloudSession>();
      rcs && rcs->phase == stepengine::RevcloudPhase::WaitNextPoint) {
    const float rz = static_cast<float>(rcs->z);
    std::vector<float> bulge;
    const std::vector<double> xy = CadRevcloudPreviewPolyline(cmd, curX, curY, &bulge);
    if (xy.empty()) {
      const auto& dr = rcs->draft;
      for (std::size_t i = 0; i + 3 < dr.size(); i += 2)
        PushRubberSegViewRel(rubberLines, dr[i], dr[i + 1], dr[i + 2], dr[i + 3], 0., 0., rz, rz);
      if (dr.size() >= 2)
        PushRubberSegViewRel(rubberLines, dr[dr.size() - 2], dr.back(), curX, curY, 0., 0., rz, rz);
    } else {
      constexpr double kPi = 3.14159265358979323846;
      const std::size_t nv = bulge.size();
      for (std::size_t i = 0; i < nv; ++i) {
        const std::size_t j = (i + 1) % nv;
        const double x0 = xy[i * 2], y0 = xy[i * 2 + 1], x1 = xy[j * 2], y1 = xy[j * 2 + 1];
        const BulgeArcSpan a = BulgeArc(x0, y0, x1, y1, static_cast<double>(bulge[i]));
        if (!a.valid) {
          PushRubberSegViewRel(rubberLines, x0, y0, x1, y1, 0., 0., rz, rz);
          continue;
        }
        const int n = std::clamp(static_cast<int>(std::ceil(std::fabs(a.sweep) / (kPi / 12.0))), 2, 24);
        double px = x0, py = y0;
        for (int s = 1; s <= n; ++s) {
          const double u = a.startAngle + a.sweep * (static_cast<double>(s) / n);
          const double qx = a.cx + a.radius * std::cos(u);
          const double qy = a.cy + a.radius * std::sin(u);
          PushRubberSegViewRel(rubberLines, px, py, qx, qy, 0., 0., rz, rz);
          px = qx;
          py = qy;
        }
      }
    }
  }

  if (const auto* prs = cmd.sessionAs<stepengine::PipeRunSession>();
      prs && prs->phase == stepengine::PipeRunPhase::WaitNextPoint) {
    double gx = curX;
    double gy = curY;
    double gz = static_cast<double>(CadCommitElevation(cmd));
    const std::size_t n = prs->draftVerts.size();
    if (n >= 3) {
      const float lastX = static_cast<float>(prs->draftVerts[n - 3]);
      const float lastY = static_cast<float>(prs->draftVerts[n - 2]);
      const float lastZ = static_cast<float>(prs->draftVerts[n - 1]);
      float wx = static_cast<float>(curX);
      float wy = static_cast<float>(curY);
      float wz = static_cast<float>(gz);
      const float targetZ = static_cast<float>(gz);
      if (!cmd.viewportSnapPickValid)
        ApplyPipeRunCompassFromAnchor(cmd, lastX, lastY, &wx, &wy, /*compass=*/true, lastZ, targetZ, &wz);
      gx = static_cast<double>(wx);
      gy = static_cast<double>(wy);
      gz = static_cast<double>(wz);
    }
    CadPipeRun ghostRun;
    const std::size_t draftN = prs->draftVerts.size();
    if (draftN >= 3) {
      ghostRun.vertsXyz.push_back(prs->draftVerts[draftN - 3]);
      ghostRun.vertsXyz.push_back(prs->draftVerts[draftN - 2]);
      ghostRun.vertsXyz.push_back(prs->draftVerts[draftN - 1]);
    }
    ghostRun.vertsXyz.push_back(gx);
    ghostRun.vertsXyz.push_back(gy);
    ghostRun.vertsXyz.push_back(gz);
    ghostRun.nominalSize = cmd.pipeRunNominalSize;
    ghostRun.wallThicknessIn = cmd.pipeRunWallThicknessIn;
    const auto ghostT0 = std::chrono::steady_clock::now();
    std::vector<CadSolidPtr> ghostSolids;
    if (CadBuildPipeRunSolids(ghostRun, &ghostSolids)) {
      brep::Problem why = brep::Problem::Ok;
      for (const CadSolidPtr& gs : ghostSolids) {
        if (!gs)
          continue;
        std::vector<double> segs;
        if (brep::TessellateEdges(*gs, kSolidChordToleranceFt, &segs, &why)) {
          for (std::size_t i = 0; i + 5 < segs.size(); i += 6)
            PushRubberSegViewRel(rubberLines, segs[i], segs[i + 1], segs[i + 3], segs[i + 4], 0., 0.,
                                 static_cast<float>(segs[i + 2]), static_cast<float>(segs[i + 5]));
        }
      }
    }
    const std::chrono::duration<double, std::milli> ghostMs = std::chrono::steady_clock::now() - ghostT0;
    cmd.pipeRunPerf.ghost.Add(ghostMs.count());
  }

  // EXTRACTCENTERLINE (REQ-347): the preview is exactly the hover's already-computed fit — no
  // cursor-driven construction, unlike every command above it, so this is the one branch that reads
  // no local pick state at all.
  if (cmd.active == AppCommandState::Kind::ExtractCenterline && cmd.extractCenterlineHoverValid) {
    PushRubberSegViewRel(rubberLines, cmd.extractCenterlineP0X, cmd.extractCenterlineP0Y,
                         cmd.extractCenterlineP1X, cmd.extractCenterlineP1Y, 0., 0.,
                         static_cast<float>(cmd.extractCenterlineP0Z),
                         static_cast<float>(cmd.extractCenterlineP1Z));
  }
}
