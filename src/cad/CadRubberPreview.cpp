#include "CadRubberPreview.hpp"

#include "CadCommands.hpp"
#include "geom2d.hpp"

#include <algorithm>
#include <cmath>

void PushRubberSegViewRel(std::vector<float>& o, double x0, double y0, double x1, double y1, double /*anchorX*/,
                          double /*anchorY*/) {
  o.push_back(static_cast<float>(x0));
  o.push_back(static_cast<float>(y0));
  o.push_back(0.f);
  o.push_back(static_cast<float>(x1));
  o.push_back(static_cast<float>(y1));
  o.push_back(0.f);
}

void AppendWorldRectRubberViewRel(std::vector<float>& o, float xa, float ya, float xb, float yb, double anchorX,
                                  double anchorY) {
  const float mnX = std::min(xa, xb);
  const float mxX = std::max(xa, xb);
  const float mnY = std::min(ya, yb);
  const float mxY = std::max(ya, yb);
  PushRubberSegViewRel(o, mnX, mnY, mxX, mnY, anchorX, anchorY);
  PushRubberSegViewRel(o, mxX, mnY, mxX, mxY, anchorX, anchorY);
  PushRubberSegViewRel(o, mxX, mxY, mnX, mxY, anchorX, anchorY);
  PushRubberSegViewRel(o, mnX, mxY, mnX, mnY, anchorX, anchorY);
}

namespace {

bool ComputeCircumcircleRubber(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                               float* r) {
  const float d = 2.f * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
  if (std::fabs(d) < 1e-6f)
    return false;
  const float a2 = ax * ax + ay * ay;
  const float b2 = bx * bx + by * by;
  const float c2 = cx * cx + cy * cy;
  const float ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
  const float uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
  const float dx = ux - ax;
  const float dy = uy - ay;
  *ox = ux;
  *oy = uy;
  *r = std::sqrt(dx * dx + dy * dy);
  return true;
}

void AppendArcRubberWorld(std::vector<float>& out, float ax, float ay, float bx, float by, float cx, float cy) {
  float ox = 0.f;
  float oy = 0.f;
  float r = 0.f;
  if (!ComputeCircumcircleRubber(ax, ay, bx, by, cx, cy, &ox, &oy, &r) || r <= 1e-6f)
    return;
  constexpr double twopi = 6.28318530717958647692;
  auto normPos = [](double x) {
    double t = std::fmod(x, twopi);
    if (t < 0)
      t += twopi;
    return t;
  };
  const double ta = std::atan2(static_cast<double>(ay - oy), static_cast<double>(ax - ox));
  const double tb = std::atan2(static_cast<double>(by - oy), static_cast<double>(bx - ox));
  const double tc = std::atan2(static_cast<double>(cy - oy), static_cast<double>(cx - ox));
  const double arc_ab = normPos(tb - ta);
  const double arc_ac = normPos(tc - ta);
  const bool useCcw = arc_ab <= arc_ac + 1e-10;
  double sweep = useCcw ? arc_ac : arc_ac - twopi;
  if (std::fabs(sweep) < 1e-12)
    sweep = twopi;
  const double sr = ta;
  const int nseg = 36;
  for (int i = 0; i < nseg; ++i) {
    const double t0 = sr + sweep * static_cast<double>(i) / static_cast<double>(nseg);
    const double t1 = sr + sweep * static_cast<double>(i + 1) / static_cast<double>(nseg);
    double wx0 = 0.;
    double wy0 = 0.;
    double wx1 = 0.;
    double wy1 = 0.;
    CirclePointWorld(ox, oy, r, t0, &wx0, &wy0);
    CirclePointWorld(ox, oy, r, t1, &wx1, &wy1);
    PushRubberSegViewRel(out, wx0, wy0, wx1, wy1, 0., 0.);
  }
}

void AppendCircleRubberWorld(std::vector<float>& out, float cx, float cy, float r, float orthoHalfH, int fbHeightPx) {
  if (r <= 1e-6f)
    return;
  const int segments =
      CircleTessellationSegmentCount(static_cast<double>(r), static_cast<double>(orthoHalfH), fbHeightPx);
  const double dcx = static_cast<double>(cx);
  const double dcy = static_cast<double>(cy);
  const double dr = static_cast<double>(r);
  constexpr double kTwoPi = 6.283185307179586;
  for (int i = 0; i < segments; ++i) {
    const double t0 = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
    const double t1 = kTwoPi * static_cast<double>(i + 1) / static_cast<double>(segments);
    double wx0 = 0.;
    double wy0 = 0.;
    double wx1 = 0.;
    double wy1 = 0.;
    CirclePointWorld(dcx, dcy, dr, t0, &wx0, &wy0);
    CirclePointWorld(dcx, dcy, dr, t1, &wx1, &wy1);
    PushRubberSegViewRel(out, wx0, wy0, wx1, wy1, 0., 0.);
  }
}

} // namespace

void AppendCadDraftRubberLines(const AppCommandState& cmd, double curX, double curY, bool orthoEnabled,
                               double /*viewAnchorX*/, double /*viewAnchorY*/, float orthoHalfH, int fbHeightPx,
                               std::vector<float>& rubberLines) {
  const float curXf = static_cast<float>(curX);
  const float curYf = static_cast<float>(curY);

  if (cmd.active == AppCommandState::Kind::Line && cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.segmentAnglePickPhase == SAP::WaitP2)
      PushRubberSegViewRel(rubberLines, cmd.segmentPickRefX1, cmd.segmentPickRefY1, curXf, curYf, 0., 0.);
    else if (cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
      const float th = MathAngleRadFromBearingCwNorthDeg(cmd.segmentPickDraftBearingDeg);
      const float ux = std::cos(th);
      const float uy = std::sin(th);
      float lx = curXf;
      float ly = curYf;
      ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0.);
    } else {
      float lx = curXf;
      float ly = curYf;
      if (cmd.segmentAngleLockActive)
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, cmd.segmentLockUx, cmd.segmentLockUy, &lx, &ly,
                                         false);
      else
        ApplyOrthoConstrainFromAnchor(cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0.);
    }
  }

  if (cmd.active == AppCommandState::Kind::Polyline &&
      cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint) {
    float lx = curXf;
    float ly = curYf;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    const auto& d = cmd.polylineDraftVerts;
    for (size_t i = 0; i + 5 < d.size(); i += 3)
      PushRubberSegViewRel(rubberLines, d[i], d[i + 1], d[i + 3], d[i + 4], 0., 0.);

    if (cmd.segmentAnglePickPhase == SAP::WaitP2)
      PushRubberSegViewRel(rubberLines, cmd.segmentPickRefX1, cmd.segmentPickRefY1, curXf, curYf, 0., 0.);
    else if (cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
      const float th = MathAngleRadFromBearingCwNorthDeg(cmd.segmentPickDraftBearingDeg);
      const float ux = std::cos(th);
      const float uy = std::sin(th);
      lx = curXf;
      ly = curYf;
      ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0.);
    } else {
      if (cmd.segmentAngleLockActive)
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, cmd.segmentLockUx, cmd.segmentLockUy, &lx, &ly,
                                         false);
      else
        ApplyOrthoConstrainFromAnchor(cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled);
      PushRubberSegViewRel(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly, 0., 0.);
    }
  }

  if (cmd.active == AppCommandState::Kind::Arc) {
    using AP = AppCommandState::ArcPhase;
    if (cmd.arcPhase == AP::WaitMid)
      PushRubberSegViewRel(rubberLines, cmd.arcAx, cmd.arcAy, curXf, curYf, 0., 0.);
    else if (cmd.arcPhase == AP::WaitEnd)
      AppendArcRubberWorld(rubberLines, cmd.arcAx, cmd.arcAy, cmd.arcBx, cmd.arcBy, curXf, curYf);
  }

  if (cmd.active == AppCommandState::Kind::Ellipse && cmd.ellPhase == AppCommandState::EllipsePhase::WaitMajorEnd)
    PushRubberSegViewRel(rubberLines, cmd.ellCx, cmd.ellCy, curXf, curYf, 0., 0.);

  if ((cmd.active == AppCommandState::Kind::DimAligned || cmd.active == AppCommandState::Kind::DimLinear) &&
      cmd.dimPhase == AppCommandState::DimPhase::WaitExt2)
    PushRubberSegViewRel(rubberLines, cmd.dimE1x, cmd.dimE1y, curXf, curYf, 0., 0.);

  if (cmd.active == AppCommandState::Kind::SurveyInverse &&
      cmd.surveyInversePhase == AppCommandState::SurveyInversePhase::WaitTo)
    PushRubberSegViewRel(rubberLines, cmd.surveyInverseFromX, cmd.surveyInverseFromY, curXf, curYf, 0., 0.);

  if (cmd.active == AppCommandState::Kind::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    if (cmd.dimAngularPhase == DAP::WaitRay1)
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, curXf, curYf, 0., 0.);
    else if (cmd.dimAngularPhase == DAP::WaitRay2) {
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE1x, cmd.dimE1y, 0., 0.);
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, curXf, curYf, 0., 0.);
    } else if (cmd.dimAngularPhase == DAP::WaitArc) {
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE1x, cmd.dimE1y, 0., 0.);
      PushRubberSegViewRel(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE2x, cmd.dimE2y, 0., 0.);
    }
  }

  if (cmd.active == AppCommandState::Kind::Circle) {
    using CP = AppCommandState::CirclePhase;
    if (cmd.circlePhase == CP::WaitRadius) {
      const float dx = curXf - cmd.circleCx;
      const float dy = curYf - cmd.circleCy;
      AppendCircleRubberWorld(rubberLines, cmd.circleCx, cmd.circleCy, std::sqrt(dx * dx + dy * dy), orthoHalfH,
                              fbHeightPx);
    } else if (cmd.circlePhase == CP::ThreeP_WaitP2) {
      const float dx = curXf - cmd.c3p1x;
      const float dy = curYf - cmd.c3p1y;
      const float chord = std::sqrt(dx * dx + dy * dy);
      const float rPrev = 0.5f * chord;
      if (rPrev > 1e-6f)
        AppendCircleRubberWorld(rubberLines, (cmd.c3p1x + curXf) * 0.5f, (cmd.c3p1y + curYf) * 0.5f, rPrev, orthoHalfH,
                                fbHeightPx);
    } else if (cmd.circlePhase == CP::ThreeP_WaitP3) {
      float ox = 0.f;
      float oy = 0.f;
      float rCirc = 0.f;
      if (ComputeCircumcircle(cmd.c3p1x, cmd.c3p1y, cmd.c3p2x, cmd.c3p2y, curXf, curYf, &ox, &oy, &rCirc))
        AppendCircleRubberWorld(rubberLines, ox, oy, rCirc, orthoHalfH, fbHeightPx);
    }
  }

  if (cmd.active == AppCommandState::Kind::Mtext) {
    using MPtxt = AppCommandState::MtextPhase;
    if (cmd.mtextPhase == MPtxt::WaitCorner2) {
      float lx = curXf;
      float ly = curYf;
      ApplyOrthoConstrainFromAnchor(cmd.mtxtX1, cmd.mtxtY1, &lx, &ly, orthoEnabled);
      AppendWorldRectRubberViewRel(rubberLines, cmd.mtxtX1, cmd.mtxtY1, lx, ly, 0., 0.);
    } else if (cmd.mtextPhase == MPtxt::WaitString)
      AppendWorldRectRubberViewRel(rubberLines, cmd.mtxtX1, cmd.mtxtY1, cmd.mtxtX2, cmd.mtxtY2, 0., 0.);
  }
}
