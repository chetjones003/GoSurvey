#include "CadRubberPreview.hpp"

#include "CadCommands.hpp"

#include <algorithm>
#include <cmath>

void PushRubberSeg(std::vector<float>& o, float x0, float y0, float x1, float y1) {
  o.push_back(x0);
  o.push_back(y0);
  o.push_back(0.f);
  o.push_back(x1);
  o.push_back(y1);
  o.push_back(0.f);
}

void AppendWorldRectRubber(std::vector<float>& o, float xa, float ya, float xb, float yb) {
  const float mnX = std::min(xa, xb);
  const float mxX = std::max(xa, xb);
  const float mnY = std::min(ya, yb);
  const float mxY = std::max(ya, yb);
  PushRubberSeg(o, mnX, mnY, mxX, mnY);
  PushRubberSeg(o, mxX, mnY, mxX, mxY);
  PushRubberSeg(o, mxX, mxY, mnX, mxY);
  PushRubberSeg(o, mnX, mxY, mnX, mnY);
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

void AppendArcRubber(std::vector<float>& out, float ax, float ay, float bx, float by, float cx, float cy) {
  float ox = 0.f, oy = 0.f, r = 0.f;
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
    double t0 = sr + sweep * static_cast<double>(i) / static_cast<double>(nseg);
    double t1 = sr + sweep * static_cast<double>(i + 1) / static_cast<double>(nseg);
    float x0 = static_cast<float>(ox + r * std::cos(t0));
    float y0 = static_cast<float>(oy + r * std::sin(t0));
    float x1 = static_cast<float>(ox + r * std::cos(t1));
    float y1 = static_cast<float>(oy + r * std::sin(t1));
    PushRubberSeg(out, x0, y0, x1, y1);
  }
}

void AppendCircleRubber(std::vector<float>& out, float cx, float cy, float r, int segments = 72) {
  if (r <= 1e-6f)
    return;
  const float twoPi = 6.28318530718f;
  for (int i = 0; i < segments; ++i) {
    const float t0 = twoPi * static_cast<float>(i) / static_cast<float>(segments);
    const float t1 = twoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
    out.push_back(cx + r * std::cos(t0));
    out.push_back(cy + r * std::sin(t0));
    out.push_back(0.f);
    out.push_back(cx + r * std::cos(t1));
    out.push_back(cy + r * std::sin(t1));
    out.push_back(0.f);
  }
}

} // namespace

void AppendCadDraftRubberLines(const AppCommandState& cmd, float curX, float curY, bool orthoEnabled,
                                std::vector<float>& rubberLines) {
  if (cmd.active == AppCommandState::Kind::Line && cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (cmd.segmentAnglePickPhase == SAP::WaitP2)
      PushRubberSeg(rubberLines, cmd.segmentPickRefX1, cmd.segmentPickRefY1, curX, curY);
    else if (cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
      const float th = MathAngleRadFromBearingCwNorthDeg(cmd.segmentPickDraftBearingDeg);
      const float ux = std::cos(th);
      const float uy = std::sin(th);
      float lx = curX;
      float ly = curY;
      ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
      PushRubberSeg(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly);
    } else {
      float lx = curX;
      float ly = curY;
      if (cmd.segmentAngleLockActive)
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, cmd.segmentLockUx, cmd.segmentLockUy, &lx, &ly,
                                         false);
      else
        ApplyOrthoConstrainFromAnchor(cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled);
      PushRubberSeg(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly);
    }
  }

  if (cmd.active == AppCommandState::Kind::Polyline &&
      cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint) {
    float lx = curX;
    float ly = curY;
    using SAP = AppCommandState::SegmentAnglePickPhase;
    const auto& d = cmd.polylineDraftVerts;
    for (size_t i = 0; i + 5 < d.size(); i += 3)
      PushRubberSeg(rubberLines, d[i], d[i + 1], d[i + 3], d[i + 4]);

    if (cmd.segmentAnglePickPhase == SAP::WaitP2)
      PushRubberSeg(rubberLines, cmd.segmentPickRefX1, cmd.segmentPickRefY1, curX, curY);
    else if (cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
      const float th = MathAngleRadFromBearingCwNorthDeg(cmd.segmentPickDraftBearingDeg);
      const float ux = std::cos(th);
      const float uy = std::sin(th);
      lx = curX;
      ly = curY;
      ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
      PushRubberSeg(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly);
    } else {
      if (cmd.segmentAngleLockActive)
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, cmd.segmentLockUx, cmd.segmentLockUy, &lx, &ly,
                                         false);
      else
        ApplyOrthoConstrainFromAnchor(cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled);
      PushRubberSeg(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly);
    }
  }

  if (cmd.active == AppCommandState::Kind::Arc) {
    using AP = AppCommandState::ArcPhase;
    if (cmd.arcPhase == AP::WaitMid)
      PushRubberSeg(rubberLines, cmd.arcAx, cmd.arcAy, curX, curY);
    else if (cmd.arcPhase == AP::WaitEnd)
      AppendArcRubber(rubberLines, cmd.arcAx, cmd.arcAy, cmd.arcBx, cmd.arcBy, curX, curY);
  }

  if (cmd.active == AppCommandState::Kind::Ellipse && cmd.ellPhase == AppCommandState::EllipsePhase::WaitMajorEnd)
    PushRubberSeg(rubberLines, cmd.ellCx, cmd.ellCy, curX, curY);

  if ((cmd.active == AppCommandState::Kind::DimAligned || cmd.active == AppCommandState::Kind::DimLinear) &&
      cmd.dimPhase == AppCommandState::DimPhase::WaitExt2)
    PushRubberSeg(rubberLines, cmd.dimE1x, cmd.dimE1y, curX, curY);

  if (cmd.active == AppCommandState::Kind::SurveyInverse &&
      cmd.surveyInversePhase == AppCommandState::SurveyInversePhase::WaitTo)
    PushRubberSeg(rubberLines, cmd.surveyInverseFromX, cmd.surveyInverseFromY, curX, curY);

  if (cmd.active == AppCommandState::Kind::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    if (cmd.dimAngularPhase == DAP::WaitRay1)
      PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, curX, curY);
    else if (cmd.dimAngularPhase == DAP::WaitRay2) {
      PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE1x, cmd.dimE1y);
      PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, curX, curY);
    } else if (cmd.dimAngularPhase == DAP::WaitArc) {
      PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE1x, cmd.dimE1y);
      PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE2x, cmd.dimE2y);
    }
  }

  if (cmd.active == AppCommandState::Kind::Circle) {
    using CP = AppCommandState::CirclePhase;
    if (cmd.circlePhase == CP::WaitRadius) {
      const float dx = curX - cmd.circleCx;
      const float dy = curY - cmd.circleCy;
      AppendCircleRubber(rubberLines, cmd.circleCx, cmd.circleCy, std::sqrt(dx * dx + dy * dy));
    } else if (cmd.circlePhase == CP::ThreeP_WaitP2) {
      const float dx = curX - cmd.c3p1x;
      const float dy = curY - cmd.c3p1y;
      const float chord = std::sqrt(dx * dx + dy * dy);
      const float rPrev = 0.5f * chord;
      if (rPrev > 1e-6f)
        AppendCircleRubber(rubberLines, (cmd.c3p1x + curX) * 0.5f, (cmd.c3p1y + curY) * 0.5f, rPrev);
    } else if (cmd.circlePhase == CP::ThreeP_WaitP3) {
      float ox = 0.f;
      float oy = 0.f;
      float rCirc = 0.f;
      if (ComputeCircumcircle(cmd.c3p1x, cmd.c3p1y, cmd.c3p2x, cmd.c3p2y, curX, curY, &ox, &oy, &rCirc))
        AppendCircleRubber(rubberLines, ox, oy, rCirc);
    }
  }

  if (cmd.active == AppCommandState::Kind::Mtext) {
    using MPtxt = AppCommandState::MtextPhase;
    if (cmd.mtextPhase == MPtxt::WaitCorner2) {
      float lx = curX;
      float ly = curY;
      ApplyOrthoConstrainFromAnchor(cmd.mtxtX1, cmd.mtxtY1, &lx, &ly, orthoEnabled);
      AppendWorldRectRubber(rubberLines, cmd.mtxtX1, cmd.mtxtY1, lx, ly);
    } else if (cmd.mtextPhase == MPtxt::WaitString)
      AppendWorldRectRubber(rubberLines, cmd.mtxtX1, cmd.mtxtY1, cmd.mtxtX2, cmd.mtxtY2);
  }
}
