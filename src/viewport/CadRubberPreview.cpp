#include "CadRubberPreview.hpp"

#include "CadBlocks.hpp"
#include "CadCommands.hpp"
#include "CadCoordinateFrame.hpp"
#include "geom2d.hpp"

#include <vector>

#include <algorithm>
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

void AppendCadDraftRubberLines(const AppCommandState& cmd, double curX, double curY, bool orthoEnabled,
                               double /*viewAnchorX*/, double /*viewAnchorY*/, float orthoHalfH, int fbHeightPx,
                               std::vector<float>& rubberLines) {
  const float curXf = static_cast<float>(curX);
  const float curYf = static_cast<float>(curY);
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

  if (cmd.active == AppCommandState::Kind::Line && cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.segmentAnglePickPhase == SAP::WaitP2)
      PushRubberSegViewRel(rubberLines, cmd.segmentPickRefX1, cmd.segmentPickRefY1, curXf, curYf, 0., 0.,
                           zc, zc);
    else if (cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
      const float th = MathAngleRadFromBearingCwNorthDeg(cmd.segmentPickDraftBearingDeg);
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
      if (cmd.segmentAngleLockActive)
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, cmd.segmentLockUx, cmd.segmentLockUy, &lx, &ly,
                                         false);
      else
        ApplyOrthoConstrainFromAnchor(cmd, cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0., cmd.anchorZ,
                           zc);  // preview at the elevation it will commit to
    }
  }

  if (cmd.active == AppCommandState::Kind::Polyline &&
      cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint) {
    float lx = curXf;
    float ly = curYf;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    const auto& d = cmd.polylineDraftVerts;
    for (size_t i = 0; i + 5 < d.size(); i += 3)
      PushRubberSegViewRel(rubberLines, d[i], d[i + 1], d[i + 3], d[i + 4], 0., 0., d[i + 2], d[i + 5]);

    if (cmd.segmentAnglePickPhase == SAP::WaitP2)
      PushRubberSegViewRel(rubberLines, cmd.segmentPickRefX1, cmd.segmentPickRefY1, curXf, curYf, 0., 0.,
                           zc, zc);
    else if (cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
      const float th = MathAngleRadFromBearingCwNorthDeg(cmd.segmentPickDraftBearingDeg);
      const float ux = std::cos(th);
      const float uy = std::sin(th);
      lx = curXf;
      ly = curYf;
      ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0., cmd.anchorZ,
                           zc);
    } else {
      if (cmd.segmentAngleLockActive)
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, cmd.segmentLockUx, cmd.segmentLockUy, &lx, &ly,
                                         false);
      else
        ApplyOrthoConstrainFromAnchor(cmd, cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0., cmd.anchorZ,
                           zc);
    }
  }

  // FEATURELINE (REQ-087 / TASK-082 BUG-2): the draft had NO preview at all — nothing in
  // viewport/, render/ or ui/ read featureLineDraftVerts. That was survivable while the only way to
  // place a point was to type it; once a click places one, drawing with no preview means picking
  // blind, so the click fix is not worth having without this.
  //
  // Each segment is drawn at its vertices' own elevations, as the polyline draft above is, so a
  // feature line being drawn up a grade previews as the 3D chain it will become rather than flat.
  if (cmd.active == AppCommandState::Kind::FeatureLine) {
    const auto& d = cmd.featureLineDraftVerts;
    for (size_t i = 0; i + 5 < d.size(); i += 3)
      PushRubberSegViewRel(rubberLines, d[i], d[i + 1], d[i + 3], d[i + 4], 0., 0., d[i + 2], d[i + 5]);

    if (!d.empty()) {
      const float ax = d[d.size() - 3];
      const float ay = d[d.size() - 2];
      const float az = d[d.size() - 1];
      if (cmd.featureLinePendingPoint) {
        // An elevation is owed, so the point is FIXED where it was clicked — rubber-banding to the
        // cursor here would suggest it is still being placed, and moving the mouse while typing an
        // elevation would appear to move the point.
        PushRubberSegViewRel(rubberLines, ax, ay, cmd.featureLinePendingX, cmd.featureLinePendingY,
                             0., 0., az, cmd.featureLinePendingDefaultZ);
      } else {
        float lx = curXf;
        float ly = curYf;
        ApplyOrthoConstrainFromAnchor(cmd, ax, ay, &lx, &ly, orthoEnabled);
        PushRubberSegViewRel(rubberLines, ax, ay, lx, ly, 0., 0., az, zc);
      }
    }
  }

  // RECT (REQ-053): after the first corner, rubber-band the axis-aligned rectangle to the cursor. ORTHO is
  // deliberately NOT applied — it would collapse the rectangle to a line, and the shape is already
  // axis-aligned, which is what ORTHO is for.
  if (cmd.active == AppCommandState::Kind::Rect &&
      cmd.rectPhase == AppCommandState::RectPhase::WaitSecondCorner)
    AppendWorldRectRubberViewRel(rubberLines, cmd.rectX1, cmd.rectY1, curXf, curYf, 0., 0., zc);

  if (cmd.active == AppCommandState::Kind::Arc) {
    using AP = AppCommandState::ArcPhase;
    if (cmd.arcPhase == AP::WaitMid)
      // On a tilted work plane the first pick's own elevation is not the cursor's: two picks on a
      // wall differ only in height, and drawing both ends at zc would flatten the construction line
      // onto one contour of the wall.
      PushRubberSegViewRel(rubberLines, cmd.arcAx, cmd.arcAy, curXf, curYf, 0., 0.,
                           planeIsFlat ? zc : cmd.arcAz, zc);
    else if (cmd.arcPhase == AP::WaitEnd) {
      CadArc a{};
      if (CadSolveArcThreePoints(cmd, cmd.arcAx, cmd.arcAy, cmd.arcAz, cmd.arcBx, cmd.arcBy, cmd.arcBz, curXf,
                                 curYf, zc, &a))
        AppendCurveRubber(rubberLines, CurvePlane(a), static_cast<double>(a.r), static_cast<double>(a.startRad),
                          static_cast<double>(a.sweepRad), orthoHalfH, fbHeightPx,
                          cmd.displayArcCircleSmoothness);
    }
  }

  if (cmd.active == AppCommandState::Kind::Ellipse && cmd.ellPhase == AppCommandState::EllipsePhase::WaitMajorEnd)
    PushRubberSegViewRel(rubberLines, cmd.ellCx, cmd.ellCy, curXf, curYf, 0., 0., zc, zc);

  if ((cmd.active == AppCommandState::Kind::DimAligned || cmd.active == AppCommandState::Kind::DimLinear) &&
      cmd.dimPhase == AppCommandState::DimPhase::WaitExt2)
    PushRubberSegViewRel(rubberLines, cmd.dimE1x, cmd.dimE1y, curXf, curYf, 0., 0., zc, zc);

  if (cmd.active == AppCommandState::Kind::SurveyInverse &&
      cmd.surveyInversePhase == AppCommandState::SurveyInversePhase::WaitTo)
    PushRubberSegViewRel(rubberLines, cmd.surveyInverseFromX, cmd.surveyInverseFromY, curXf, curYf, 0., 0., zc, zc);

  if (cmd.active == AppCommandState::Kind::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    if (cmd.dimAngularPhase == DAP::WaitRay1)
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, curXf, curYf, 0., 0., zc, zc);
    else if (cmd.dimAngularPhase == DAP::WaitRay2) {
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE1x, cmd.dimE1y, 0., 0., zc, zc);
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, curXf, curYf, 0., 0., zc, zc);
    } else if (cmd.dimAngularPhase == DAP::WaitArc) {
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE1x, cmd.dimE1y, 0., 0., zc, zc);
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE2x, cmd.dimE2y, 0., 0., zc, zc);
    }
  }

  if (cmd.active == AppCommandState::Kind::Circle) {
    using CP = AppCommandState::CirclePhase;
    if (cmd.circlePhase == CP::WaitRadius) {
      // Solved by the function CIRCLE itself commits with, so the ring under the cursor is the ring
      // that lands -- including the radius, which on a tilted plane is the 3D distance to the rim
      // pick and not its XY projection (REQ-312).
      AppendCircleSolutionRubber(
          rubberLines,
          CadSolveCircleFromRimPick(cmd, cmd.circleCx, cmd.circleCy, cmd.circleCz, curXf, curYf, zc), orthoHalfH,
          fbHeightPx, cmd.displayArcCircleSmoothness);
    } else if (cmd.circlePhase == CP::ThreeP_WaitP2) {
      // Two picks so far, so the preview is the circle on the diameter between them: centre at their
      // midpoint IN SPACE, radius the distance from there to the cursor. Routed through the rim-pick
      // solver, which is where the work-plane normal and the 3D radius come from.
      const float mx = (cmd.c3p1x + curXf) * 0.5f;
      const float my = (cmd.c3p1y + curYf) * 0.5f;
      const float mz = (cmd.c3p1z + zc) * 0.5f;
      const CadCircleSolution s = CadSolveCircleFromRimPick(cmd, mx, my, mz, curXf, curYf, zc);
      if (s.r > 1e-6f)
        AppendCircleSolutionRubber(rubberLines, s, orthoHalfH, fbHeightPx, cmd.displayArcCircleSmoothness);
    } else if (cmd.circlePhase == CP::ThreeP_WaitP3) {
      CadCircleSolution s;
      if (CadSolveCircleThreePoints(cmd, cmd.c3p1x, cmd.c3p1y, cmd.c3p1z, cmd.c3p2x, cmd.c3p2y, cmd.c3p2z, curXf,
                                    curYf, zc, &s))
        AppendCircleSolutionRubber(rubberLines, s, orthoHalfH, fbHeightPx, cmd.displayArcCircleSmoothness);
    }
  }

  if (cmd.active == AppCommandState::Kind::Mtext) {
    using MPtxt = AppCommandState::MtextPhase;
    if (cmd.mtextPhase == MPtxt::WaitCorner2) {
      float lx = curXf;
      float ly = curYf;
      ApplyOrthoConstrainFromAnchor(cmd, cmd.mtxtX1, cmd.mtxtY1, &lx, &ly, orthoEnabled);
      AppendWorldRectRubberViewRel(rubberLines, cmd.mtxtX1, cmd.mtxtY1, lx, ly, 0., 0., zc);
    } else if (cmd.mtextPhase == MPtxt::WaitString)
      AppendWorldRectRubberViewRel(rubberLines, cmd.mtxtX1, cmd.mtxtY1, cmd.mtxtX2, cmd.mtxtY2, 0., 0.,
                                   zc);
  }

  // UCS axis preview (REQ-154). At the two axis prompts, draw the frame the cursor would produce —
  // the rubber from the origin out to the cursor, plus the perpendicular the frame implies — so the
  // user sees the resulting axes before committing rather than after.
  //
  // The stores are LOCAL, the UCS is WORLD (see CadActiveUcsStorage), so the pending picks are
  // converted down here rather than the rubber being built in world and converted wholesale.
  if (cmd.active == AppCommandState::Kind::Ucs) {
    using UPh = AppCommandState::UcsPhase;
    auto localOf = [&](const ray3d::Vec3& world, float* lx, float* ly) {
      CadCoord::LocalFromWorld(cmd, world.x, world.y, lx, ly);
    };
    if (cmd.ucsPhase == UPh::WaitXAxisPoint || cmd.ucsPhase == UPh::WaitXyPoint) {
      float ox = 0.f, oy = 0.f;
      localOf(cmd.ucsPendingOrigin, &ox, &oy);
      if (cmd.ucsPhase == UPh::WaitXAxisPoint) {
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
        localOf(cmd.ucsPendingXAxisPoint, &xx, &xy);
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
    } else if (cmd.ucsPhase == UPh::WaitRotationAngleP1 || cmd.ucsPhase == UPh::WaitRotationAngleP2) {
      // `2P`: nothing to draw until the first point is down, then the rubber IS the direction whose
      // angle is being measured.
      if (cmd.ucsPhase == UPh::WaitRotationAngleP2) {
        float bx = 0.f, by = 0.f;
        localOf(cmd.ucsAngleBasePoint, &bx, &by);
        PushRubberSegViewRel(rubberLines, bx, by, curXf, curYf, 0., 0., zc, zc);
      }
    } else if (cmd.ucsPhase == UPh::WaitZAxisPoint) {
      float ox = 0.f, oy = 0.f;
      localOf(cmd.ucsPendingOrigin, &ox, &oy);
      PushRubberSegViewRel(rubberLines, ox, oy, curXf, curYf, 0., 0., zc, zc);
    }
  }

  if (cmd.active == AppCommandState::Kind::InsertBlock) {
    using IPh = AppCommandState::InsertBlockPhase;
    if (cmd.insertBlockPhase == IPh::WaitInsertPoint || cmd.insertBlockPhase == IPh::WaitScale ||
        cmd.insertBlockPhase == IPh::WaitRotation) {
      // Drag indicator from the fixed insertion point to the cursor during the scale/rotation picks.
      if (cmd.insertBlockPhase != IPh::WaitInsertPoint) {
        float lx = curXf;
        float ly = curYf;
        ApplyOrthoConstrainFromAnchor(cmd, cmd.insertBlockX, cmd.insertBlockY, &lx, &ly, orthoEnabled);
        PushRubberSegViewRel(rubberLines, cmd.insertBlockX, cmd.insertBlockY, lx, ly, 0., 0., zc, zc);
      }
      // Live ghost of the block at the transform this pick would commit (REQ-107, D-2026-08-29-i).
      CadBlockXform gxf;
      if (CadBlockInsertPreviewXform(cmd, curXf, curYf, &gxf)) {
        CadBlockRef ghost;
        ghost.defName = cmd.insertBlockName;
        ghost.xf = gxf;
        std::vector<CadBlockWorldSeg> segs;
        CadBlockCollectWorldLines(cmd.blockDefs, ghost, EntityAttributes{}, &segs);
        for (const CadBlockWorldSeg& s : segs)
          PushRubberSegViewRel(rubberLines, s.x0, s.y0, s.x1, s.y1, 0., 0., s.z0, s.z1);
      }
    }
  }
}
