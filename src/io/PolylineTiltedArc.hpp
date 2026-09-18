#pragma once

#include "commands/CadEntities.hpp"
#include "CadCoordinateFrame.hpp"
#include "util/geom2d.hpp"
#include "util/ray3d.hpp"

#include <cmath>

// REQ-325 / ADR-053 — shared construction of the CadArc that a tilted curved
// polyline segment draws. This is the ONE place the ad-hoc frame +
// BulgeArc + canonical-frame re-measurement lives, so DXF and DWG
// export cannot disagree about where a tilted segment goes (issue #373's
// FILLET fix, REQ-325 increments 1-3, and DxfIo's split-on-export all use
// the same steps). See also src/io/DxfIo.cpp:buildTiltedSegmentArc.
inline bool BuildTiltedPolylineSegmentArc(const ray3d::Vec3& pA, const ray3d::Vec3& pB,
                                          double bulge, const ray3d::Vec3& normal,
                                          CadArc* out) {
  if (out == nullptr)
    return false;
  ucs::Ucs plane{};
  if (!ucs::FromNormal(pA, normal, &plane))
    return false;
  const ucs::Point2D p1Local = ucs::WorldToPlane(plane, pB);
  const BulgeArcSpan arc = BulgeArc(0.0, 0.0, p1Local.x, p1Local.y, bulge);
  if (!arc.valid)
    return false;
  const ray3d::Vec3 centerWorld = ucs::PlaneToWorld(plane, ucs::Point2D{arc.cx, arc.cy});
  ucs::Ucs canon{};
  if (!ucs::FromNormal(centerWorld, normal, &canon))
    return false;
  const ucs::Point2D sLocal = ucs::WorldToPlane(canon, pA);
  const ucs::Point2D eLocal = ucs::WorldToPlane(canon, pB);
  const float thetaA = static_cast<float>(std::atan2(sLocal.y, sLocal.x));
  const float thetaB = static_cast<float>(std::atan2(eLocal.y, eLocal.x));
  constexpr float kTwoPi = 6.28318530717958647692f;
  float sweep = thetaB - thetaA;
  if (bulge >= 0.0) {
    while (sweep < 0.f)
      sweep += kTwoPi;
  } else {
    while (sweep > 0.f)
      sweep -= kTwoPi;
  }
  out->cx = static_cast<float>(centerWorld.x);
  out->cy = static_cast<float>(centerWorld.y);
  out->z = static_cast<float>(centerWorld.z);
  out->r = static_cast<float>(arc.radius);
  out->startRad = thetaA;
  out->sweepRad = sweep;
  out->nx = static_cast<float>(normal.x);
  out->ny = static_cast<float>(normal.y);
  out->nz = static_cast<float>(normal.z);
  return true;
}
