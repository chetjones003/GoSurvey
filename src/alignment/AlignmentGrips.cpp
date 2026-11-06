// AlignmentGrips.cpp - grips on a selected alignment (REQ-487.1, issue #121).
//
// The grips Civil 3D shows on a tangent / Free curve / tangent alignment, with the same behaviour: a triangle at each
// PI moves it (the curve keeps its radius), circles at a curve's PC and PT slide along the tangents and its midpoint is
// a pass-through point (both change the radius, keeping tangency), and a square at a tangent's midpoint moves the
// tangent keeping its bearing. GoSurvey adds the curve's centre (along the bisector). Every drag goes through the
// constraint solver (Alignment::EditEntity / MovePi / MoveTangent), so a locked or impossible edit simply does not move.

#include "alignment/Alignment.hpp"
#include "alignment/Clothoid.hpp"

#include <cassert>
#include <cmath>

namespace alignment {
namespace {

constexpr int kKindsPerEntity = 10;

int WhichFor(int entity, GripKind kind) {
  assert(entity >= 0);
  assert(kind != GripKind::Start);
  return 1 + kKindsPerEntity * entity + static_cast<int>(kind);
}

Point2 Lerp(Point2 a, Point2 b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }

bool IsFreeArc(const Entity& e) { return e.kind == EntityKind::Arc && e.constraint == Entity::Constraint::Free; }

void AddTangentGrips(const Alignment& a, int i, std::vector<Grip>* g) {
  assert(g != nullptr);
  const std::vector<Entity>& es = a.Entities();
  const Entity& e = es[static_cast<size_t>(i)];
  assert(e.kind == EntityKind::Line);
  const Point2 start = a.GetPointAtStation(a.EntityStartStation(i));
  const Point2 end = a.GetPointAtStation(a.EntityEndStation(i));
  Point2 pi;
  int next = -1;
  const bool corner = a.PiAfterTangent(i, &pi, &next);
  const bool movablePi = corner && e.constraint == Entity::Constraint::Fixed &&
                         es[static_cast<size_t>(next)].constraint == Entity::Constraint::Fixed;
  if (movablePi) g->push_back({pi, WhichFor(i, GripKind::Pi), GripKind::Pi, i});
  const bool endIsPc = static_cast<size_t>(i) + 1 < es.size() && IsFreeArc(es[static_cast<size_t>(i) + 1]);
  if (!endIsPc && !(movablePi && next == i + 1)) g->push_back({end, WhichFor(i, GripKind::TangentEnd), GripKind::TangentEnd, i});
  if (e.constraint == Entity::Constraint::Fixed)
    g->push_back({Lerp(start, end, 0.5), WhichFor(i, GripKind::TangentMid), GripKind::TangentMid, i});
}

void AddCurveGrips(const Alignment& a, int i, std::vector<Grip>* g) {
  assert(g != nullptr);
  const Entity& e = a.Entities()[static_cast<size_t>(i)];
  assert(e.kind == EntityKind::Arc);
  CurveData cd;
  if (!a.CurveDataFor(i, &cd)) return;
  if (IsFreeArc(e)) g->push_back({cd.pc, WhichFor(i, GripKind::CurvePc), GripKind::CurvePc, i});
  g->push_back({cd.pt, WhichFor(i, GripKind::CurvePt), GripKind::CurvePt, i});
  g->push_back({a.GetPointAtStation(a.EntityStartStation(i) + e.length * 0.5), WhichFor(i, GripKind::CurveMid),
                GripKind::CurveMid, i});
  g->push_back({cd.center, WhichFor(i, GripKind::CurveCentre), GripKind::CurveCentre, i});
}

/// The radius of the circle tangent to both tangents of a Free curve that passes through `to` (the larger root).
bool PassThroughRadius(const CurveData& cd, Point2 to, double* radius) {
  assert(radius != nullptr);
  assert(cd.radius > 0.0);
  const double phi = cd.delta * 0.5;
  const double bx = cd.center.x - cd.pi.x, by = cd.center.y - cd.pi.y;
  const double bl = std::hypot(bx, by);
  if (!(bl > 0.0) || !(std::tan(phi) > 1e-9)) return false;
  const double wx = to.x - cd.pi.x, wy = to.y - cd.pi.y;
  const double wb = (wx * bx + wy * by) / bl;
  // |w - b R / cos phi|^2 = R^2  ->  tan^2(phi) R^2 - 2 (w.b) R / cos(phi) + |w|^2 = 0
  const double qa = std::tan(phi) * std::tan(phi), qb = -2.0 * wb / std::cos(phi), qc = wx * wx + wy * wy;
  const double disc = qb * qb - 4.0 * qa * qc;
  if (disc < 0.0) return false;
  *radius = (-qb + std::sqrt(disc)) / (2.0 * qa);
  return *radius > 1e-6;
}

bool DragFreeCurve(const Alignment& orig, int i, GripKind kind, Point2 to, Alignment* out) {
  assert(out != nullptr);
  assert(IsFreeArc(orig.Entities()[static_cast<size_t>(i)]));
  CurveData cd;
  if (!orig.CurveDataFor(i, &cd) || !cd.hasPI) return true;
  const double tanPhi = std::tan(cd.delta * 0.5);
  const Entity& back = orig.Entities()[static_cast<size_t>(i) - 1];
  const Entity& ahead = orig.Entities()[static_cast<size_t>(i) + 1];
  double r = 0.0;
  if (kind == GripKind::CurvePc) {  // the PC slides along the back tangent: T = R tan(delta / 2)
    r = ((cd.pi.x - to.x) * std::sin(back.startAzimuth) + (cd.pi.y - to.y) * std::cos(back.startAzimuth)) / tanPhi;
  } else if (kind == GripKind::CurvePt) {
    r = ((to.x - cd.pi.x) * std::sin(ahead.startAzimuth) + (to.y - cd.pi.y) * std::cos(ahead.startAzimuth)) / tanPhi;
  } else if (kind == GripKind::CurveMid) {
    if (!PassThroughRadius(cd, to, &r)) return true;
  } else {  // centre, along the bisector: the PI-to-centre distance is R / cos(delta / 2)
    const double bx = cd.center.x - cd.pi.x, by = cd.center.y - cd.pi.y;
    r = ((to.x - cd.pi.x) * bx + (to.y - cd.pi.y) * by) / std::hypot(bx, by) * std::cos(cd.delta * 0.5);
  }
  Alignment w = orig;
  if (r > 1e-6 && w.EditEntity(i, EditParam::CurveRadius, r)) *out = w;
  return true;
}

bool DragCurve(const Alignment& orig, int i, GripKind kind, Point2 to, Alignment* out) {
  assert(out != nullptr);
  const Entity& e = orig.Entities()[static_cast<size_t>(i)];
  assert(e.kind == EntityKind::Arc);
  if (IsFreeArc(e) && kind != GripKind::Pi) return DragFreeCurve(orig, i, kind, to, out);
  CurveData cd;
  if (!orig.CurveDataFor(i, &cd)) return true;
  Alignment w = orig;
  if (kind == GripKind::CurveMid) {  // the radius is the distance to the centre
    const double r = std::hypot(to.x - cd.center.x, to.y - cd.center.y);
    if (r > 1e-6 && w.EditEntity(i, EditParam::CurveRadius, r)) *out = w;
  } else if (kind == GripKind::CurveCentre) {  // the centre moves along the radius at the PC, which stays
    const double nx = (cd.center.x - cd.pc.x) / cd.radius, ny = (cd.center.y - cd.pc.y) / cd.radius;
    const double r = (to.x - cd.pc.x) * nx + (to.y - cd.pc.y) * ny;
    if (r > 1e-6 && w.EditEntity(i, EditParam::CurveRadius, r)) *out = w;
  } else if (kind == GripKind::CurvePt) {  // the PT slides round the circle: the sweep changes, the radius stays
    const double a0 = std::atan2(cd.pc.y - cd.center.y, cd.pc.x - cd.center.x);
    const double a1 = std::atan2(to.y - cd.center.y, to.x - cd.center.x);
    double sweep = std::fmod((e.right ? a0 - a1 : a1 - a0) + 4.0 * kPi, kTwoPi);  // right = clockwise in plan
    if (sweep < 1e-6) return true;
    const bool ok = e.definedByLength ? w.EditEntity(i, EditParam::CurveLength, e.radius * sweep)
                                      : w.EditEntity(i, EditParam::CurveDelta, sweep);
    if (ok) *out = w;
  }
  return true;
}

bool DragTangentEnd(const Alignment& orig, int i, Point2 to, Alignment* out) {
  assert(out != nullptr);
  const Entity& e = orig.Entities()[static_cast<size_t>(i)];
  assert(e.kind == EntityKind::Line);
  const double dx = to.x - e.start.x, dy = to.y - e.start.y;
  const double along = dx * std::sin(e.startAzimuth) + dy * std::cos(e.startAzimuth);
  if (e.constraint == Entity::Constraint::Fixed) {
    const double len = std::hypot(dx, dy);
    if (!(len > 1e-9)) return true;
    Alignment w = orig;
    if (w.EditEntity(i, EditParam::LineAzimuth, std::atan2(dx, dy))) {
      if (w.EditEntity(i, EditParam::LineLength, len)) *out = w;
      return true;
    }
  }
  Alignment s = orig;  // a floating or bearing-locked tangent keeps its bearing and slides its length along it
  if (along > 1e-9 && s.EditEntity(i, EditParam::LineLength, along)) *out = s;
  return true;
}

bool DragSpiral(const Alignment& orig, int i, GripKind kind, Point2 to, Alignment* out) {
  assert(out != nullptr);
  const Entity& e = orig.Entities()[static_cast<size_t>(i)];
  assert(e.kind == EntityKind::Spiral);
  Alignment w = orig;
  if (kind == GripKind::SpiralEnd) {
    const double along = (to.x - e.start.x) * std::sin(e.startAzimuth) + (to.y - e.start.y) * std::cos(e.startAzimuth);
    if (along > 1e-9 && w.EditEntity(i, EditParam::CurveLength, along)) *out = w;
    return true;
  }
  const double sMid = e.length * 0.5;
  const double staMid = orig.EntityStartStation(i) + sMid;
  const Point2 pMid = orig.GetPointAtStation(staMid);
  const double az = orig.GetBearingAtStation(staMid);
  const double kMid = SpiralKappaAt(e, sMid);
  if (!(std::fabs(kMid) > 1e-14)) return true;
  const double rInst = 1.0 / std::fabs(kMid);
  const double side = e.right ? 1.0 : -1.0;
  const Point2 center{pMid.x + side * rInst * std::cos(az), pMid.y - side * rInst * std::sin(az)};
  const double rNew = std::hypot(to.x - center.x, to.y - center.y);
  if (!(rNew > 1e-6)) return true;
  const double kEnd = 2.0 * (side / rNew) - EntityStartCurvature(e);
  if (std::fabs(kEnd) > 1e-14 && w.EditEntity(i, EditParam::CurveRadius, 1.0 / std::fabs(kEnd))) *out = w;
  return true;
}

}  // namespace

std::vector<Grip> GripPoints(const Alignment& a) {
  std::vector<Grip> g;
  if (a.Empty()) return g;
  g.push_back({a.StartPoint(), 0, GripKind::Start, -1});
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    const Entity& e = a.Entities()[i];
    const int ii = static_cast<int>(i);
    if (e.kind == EntityKind::Line) {
      AddTangentGrips(a, ii, &g);
    } else if (e.kind == EntityKind::Arc) {
      AddCurveGrips(a, ii, &g);
    } else {
      g.push_back({a.GetPointAtStation(a.EntityEndStation(ii)), WhichFor(ii, GripKind::SpiralEnd), GripKind::SpiralEnd, ii});
      g.push_back({a.GetPointAtStation(a.EntityStartStation(ii) + e.length * 0.5), WhichFor(ii, GripKind::SpiralMid),
                   GripKind::SpiralMid, ii});
    }
  }
  assert(!g.empty() && g.front().kind == GripKind::Start);
  return g;
}

bool DragGrip(const Alignment& orig, int which, Point2 to, Alignment* out) {
  if (out == nullptr) return false;
  *out = orig;
  if (orig.Empty() || which < 0) return false;
  if (which == 0) {
    const Point2 s = orig.StartPoint();
    out->Translate(to.x - s.x, to.y - s.y);
    return true;
  }
  const int i = (which - 1) / kKindsPerEntity;
  const int slot = (which - 1) % kKindsPerEntity;
  if (static_cast<size_t>(i) >= orig.Entities().size() || slot > static_cast<int>(GripKind::SpiralMid)) return false;
  const GripKind kind = static_cast<GripKind>(slot);
  const Entity& e = orig.Entities()[static_cast<size_t>(i)];
  Alignment w = orig;
  switch (kind) {
    case GripKind::TangentEnd:
      return e.kind == EntityKind::Line && DragTangentEnd(orig, i, to, out);
    case GripKind::TangentMid: {
      if (e.kind != EntityKind::Line) return false;
      const Point2 mid = orig.GetPointAtStation(orig.EntityStartStation(i) + e.length * 0.5);
      if (w.MoveTangent(i, to.x - mid.x, to.y - mid.y)) *out = w;
      return true;
    }
    case GripKind::Pi:
      if (e.kind != EntityKind::Line) return false;
      if (w.MovePi(i, to)) *out = w;
      return true;
    case GripKind::CurvePc:
    case GripKind::CurvePt:
    case GripKind::CurveMid:
    case GripKind::CurveCentre:
      return e.kind == EntityKind::Arc && DragCurve(orig, i, kind, to, out);
    case GripKind::SpiralEnd:
    case GripKind::SpiralMid:
      return e.kind == EntityKind::Spiral && DragSpiral(orig, i, kind, to, out);
    case GripKind::Start:
      return false;
  }
  return false;
}

}  // namespace alignment
