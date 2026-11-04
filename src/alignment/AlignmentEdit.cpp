// AlignmentEdit.cpp - constraint-aware editing, whole-object transforms, offsets and intersections
// (REQ-486..489, issue #121 Phase 2). Split from Alignment.cpp so each file stays one concern.
#include "alignment/Alignment.hpp"
#include "alignment/Clothoid.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>

namespace alignment {
namespace {

double NormAz(double a) {
  a = std::fmod(a, kTwoPi);
  if (a < 0.0) a += kTwoPi;
  return a;
}

double Sinc(double x) {
  if (std::fabs(x) < 1e-4) {
    const double x2 = x * x;
    return 1.0 - x2 / 6.0 + x2 * x2 / 120.0;
  }
  return std::sin(x) / x;
}

double Curvature(const Entity& e) {
  if (e.kind == EntityKind::Line) return 0.0;
  if (e.kind == EntityKind::Arc) return (e.right ? 1.0 : -1.0) / e.radius;
  return SpiralKappaStart(e);
}

double HeadingChange(const Entity& e) {
  if (e.kind == EntityKind::Line) return 0.0;
  if (e.kind == EntityKind::Arc) return Curvature(e) * e.length;
  return SpiralHeadingDelta(e, e.length);
}

Point2 Displacement(const Entity& e, double s) {
  if (e.kind == EntityKind::Line) return {s * std::sin(e.startAzimuth), s * std::cos(e.startAzimuth)};
  if (e.kind == EntityKind::Spiral) return SpiralDisplacementLocal(e, s);
  const double theta = Curvature(e) * s;
  const double chord = s * Sinc(theta * 0.5);
  const double mid = e.startAzimuth + theta * 0.5;
  return {chord * std::sin(mid), chord * std::cos(mid)};
}

Point2 EndOf(const Entity& e) {
  const Point2 d = Displacement(e, e.length);
  return {e.start.x + d.x, e.start.y + d.y};
}

double EndAzOf(const Entity& e) { return NormAz(e.startAzimuth + HeadingChange(e)); }

bool Valid(const Entity& e) {
  if (!(e.length > 0.0) || !std::isfinite(e.length) || !std::isfinite(e.start.x) || !std::isfinite(e.start.y) ||
      !std::isfinite(e.startAzimuth))
    return false;
  if (e.kind == EntityKind::Arc && (!(e.radius > 0.0) || !std::isfinite(e.radius) || e.length / e.radius >= kTwoPi))
    return false;
  if (e.kind == EntityKind::Spiral) {
    if (!std::isfinite(e.startRadius) || !std::isfinite(e.radius)) return false;
    if (std::fabs(EntityEndCurvature(e) - EntityStartCurvature(e)) < 1e-15) return false;
  }
  return true;
}

std::string Row(int i) { return "entity " + std::to_string(i + 1); }

}  // namespace

// ---------------------------------------------------------------- solver

bool Alignment::Resolve(std::string* whyNot) {
  const int n = static_cast<int>(entities_.size());
  auto fail = [&](const std::string& m) {
    if (whyNot != nullptr) *whyNot = m;
    return false;
  };
  const auto forwardPass = [&] {
    for (int j = 1; j < n; ++j) {
      Entity& e = entities_[static_cast<size_t>(j)];
      if (e.constraint != Entity::Constraint::FloatOnPrev) continue;
      const Entity& p = entities_[static_cast<size_t>(j) - 1];
      e.start = EndOf(p);
      e.startAzimuth = EndAzOf(p);
    }
  };
  const auto backwardPass = [&] {
    for (int j = n - 2; j >= 0; --j) {
      Entity& e = entities_[static_cast<size_t>(j)];
      if (e.constraint != Entity::Constraint::FloatOnNext) continue;
      const Entity& nx = entities_[static_cast<size_t>(j) + 1];
      e.startAzimuth = NormAz(nx.startAzimuth - HeadingChange(e));
      const Point2 d = Displacement(e, e.length);
      e.start = {nx.start.x - d.x, nx.start.y - d.y};
    }
  };
  forwardPass();
  backwardPass();
  for (int j = 0; j < n; ++j) {
    Entity& arc = entities_[static_cast<size_t>(j)];
    if (arc.constraint != Entity::Constraint::Free) continue;
    if (arc.kind != EntityKind::Arc) return fail(Row(j) + ": only a curve can be Free");
    if (j < 1 || j > n - 2 || entities_[static_cast<size_t>(j) - 1].kind != EntityKind::Line ||
        entities_[static_cast<size_t>(j) + 1].kind != EntityKind::Line)
      return fail(Row(j) + ": a Free curve must lie between two tangents");
    Entity& l1 = entities_[static_cast<size_t>(j) - 1];
    Entity& l2 = entities_[static_cast<size_t>(j) + 1];
    if (l1.constraint == Entity::Constraint::FloatOnNext || l1.constraint == Entity::Constraint::Free ||
        l2.constraint == Entity::Constraint::FloatOnPrev || l2.constraint == Entity::Constraint::Free)
      return fail(Row(j) + ", " + Row(j - 1) + " and " + Row(j + 1) +
                  ": a Free curve needs a Fixed or following tangent before it and a Fixed or leading tangent after it");
    const double az1 = l1.startAzimuth, az2 = l2.startAzimuth;
    const double d1x = std::sin(az1), d1y = std::cos(az1), d2x = std::sin(az2), d2y = std::cos(az2);
    const Point2 e2 = EndOf(l2);  // the second tangent keeps its end
    const double cross = d1x * d2y - d1y * d2x;
    if (std::fabs(cross) < 1e-12)
      return fail(Row(j) + ": its tangents (" + Row(j - 1) + " and " + Row(j + 1) + ") are parallel, so no curve fits between them");
    // start1 + t*d1 = e2 - u*d2
    const double rx = e2.x - l1.start.x, ry = e2.y - l1.start.y;
    const double t = (rx * d2y - ry * d2x) / cross;
    const double u = (d1x * ry - d1y * rx) / cross;  // t*d1 + u*d2 = e2 - start1
    double defl = std::fmod(az2 - az1, kTwoPi);
    if (defl > kPi) defl -= kTwoPi;
    if (defl < -kPi) defl += kTwoPi;
    const double T = arc.radius * std::tan(std::fabs(defl) * 0.5);
    if (!(t > 0.0) || !(u > 0.0) || !(t - T > 1e-9) || !(u - T > 1e-9))
      return fail(Row(j) + ": the curve (tangent length " + std::to_string(T) + ") does not fit between its tangents, " +
                  Row(j - 1) + " and " + Row(j + 1));
    const Point2 pi{l1.start.x + t * d1x, l1.start.y + t * d1y};
    l1.length = t - T;
    arc.start = {pi.x - T * d1x, pi.y - T * d1y};
    arc.startAzimuth = az1;
    arc.right = defl > 0.0;
    arc.length = arc.radius * std::fabs(defl);
    l2.start = {pi.x + T * d2x, pi.y + T * d2y};
    l2.length = u - T;
  }
  forwardPass();
  for (int j = 0; j < n; ++j) {
    if (!Valid(entities_[static_cast<size_t>(j)])) return fail(Row(j) + " would have a non-positive length or radius, or sweep a full circle");
  }
  return true;
}

bool Alignment::EditEntity(int i, EditParam param, double value, std::string* whyNot) {
  auto fail = [&](const std::string& m) {
    if (whyNot != nullptr) *whyNot = m;
    return false;
  };
  if (i < 0 || static_cast<size_t>(i) >= entities_.size()) return fail("no such entity");
  if (!std::isfinite(value)) return fail("the value is not a number");
  Alignment work = *this;
  Entity& e = work.entities_[static_cast<size_t>(i)];
  const bool arc = e.kind == EntityKind::Arc;
  const bool spiral = e.kind == EntityKind::Spiral;
  const std::string row = Row(i);
  switch (param) {
    case EditParam::LineAzimuth:
      if (arc || spiral) return fail(row + " is not a tangent");
      if (e.constraint != Entity::Constraint::Fixed)
        return fail(row + "'s bearing is derived from the entity it is attached to; make it Fixed to edit the bearing");
      if ((e.locks & Entity::kLockBearing) != 0) return fail(row + " has a fixed bearing");
      e.startAzimuth = NormAz(value);
      break;
    case EditParam::LineLength:
      if (arc) return fail(row + " is a curve, not a tangent");
      if (spiral) return fail(row + " is a spiral; edit its length with LENGTH on that row");
      if ((e.locks & Entity::kLockLength) != 0) return fail(row + " has a fixed length");
      if (!(value > 0.0)) return fail("the length must be positive");
      e.length = value;
      break;
    case EditParam::CurveRadius:
      if (!arc && !spiral) return fail(row + " is a tangent, not a curve or spiral");
      if ((e.locks & Entity::kLockRadius) != 0) return fail(row + " has a fixed radius");
      if (value < 0.0 || !std::isfinite(value)) return fail("the radius must be zero (tangent) or positive");
      if (arc && !(value > 0.0)) return fail("the radius must be positive");
      // D-2026-11-04-f: a curve keeps what it was defined by. A radius-and-delta curve keeps its delta, so its length
      // follows the radius; a radius-and-length curve keeps its length; a Free curve takes its delta from its tangents.
      if (arc && !e.definedByLength && e.constraint != Entity::Constraint::Free) {
        if ((e.locks & Entity::kLockLength) != 0)
          return fail(row + " has a fixed length, and keeping its delta would change it; unlock the length first");
        e.length = value * (e.length / e.radius);
      }
      e.radius = value;
      break;
    case EditParam::CurveLength:
      if (!arc && !spiral) return fail(row + " is a tangent, not a curve or spiral");
      if (arc && e.constraint == Entity::Constraint::Free)
        return fail(row + " is a Free curve: its length comes from its radius and its two tangents; edit the radius");
      if ((e.locks & Entity::kLockLength) != 0) return fail(row + " has a fixed length");
      if (!(value > 0.0)) return fail("the length must be positive");
      e.length = value;
      if (arc) e.definedByLength = true;  // D-2026-11-04-g: the curve is now defined by radius and this length
      break;
    case EditParam::CurveDelta:
      if (!arc) return fail(row + " is not a circular curve");
      if (e.constraint == Entity::Constraint::Free)
        return fail(row + " is a Free curve: its delta comes from its two tangents; edit a tangent's bearing");
      if ((e.locks & Entity::kLockLength) != 0) return fail(row + " has a fixed length, so its delta cannot change");
      if (!(value > 0.0) || value >= kTwoPi) return fail("the delta must be between 0 and 360 degrees");
      e.length = e.radius * value;  // the radius is kept
      e.definedByLength = false;    // D-2026-11-04-g: the curve is now defined by radius and this delta
      break;
    case EditParam::CurveDirection:
      if (!arc && !spiral) return fail(row + " is a tangent, not a curve or spiral");
      if (arc && e.constraint == Entity::Constraint::Free)
        return fail(row + " is a Free curve: it turns the way its two tangents turn");
      e.right = value != 0.0;
      break;
    case EditParam::SpiralStartRadius:
      if (!spiral) return fail(row + " is not a spiral");
      if ((e.locks & Entity::kLockRadius) != 0) return fail(row + " has a fixed radius");
      if (value < 0.0 || !std::isfinite(value)) return fail("the start radius must be zero (tangent) or positive");
      e.startRadius = value;
      break;
    case EditParam::Constraint: {
      const int c = static_cast<int>(value);
      if (c < 0 || c > 3) return fail("unknown constraint state");
      const auto cs = static_cast<Entity::Constraint>(c);
      if (i == 0 && cs != Entity::Constraint::Fixed && cs != Entity::Constraint::FloatOnNext)
        return fail(row + " is the first entity and has nothing before it to follow");
      if (i == static_cast<int>(entities_.size()) - 1 && cs != Entity::Constraint::Fixed && cs != Entity::Constraint::FloatOnPrev)
        return fail(row + " is the last entity and has nothing after it to follow");
      e.constraint = cs;
      break;
    }
    case EditParam::Locks:
      e.locks = static_cast<unsigned char>(static_cast<int>(value) & Entity::kLockMask);
      work.entities_[static_cast<size_t>(i)] = e;
      *this = std::move(work);  // locks never move geometry
      return true;
  }
  std::string why;
  if (!work.Resolve(&why)) return fail(why);
  work.Rebuild();
  if (!work.StationLocksHold(*this, -1, -1, &why)) return fail(why);
  *this = std::move(work);
  PruneOrphanEquations();
  return true;
}

bool ParameterEditable(const Entity& e, EditParam param) {
  if (param == EditParam::Constraint || param == EditParam::Locks) return true;
  const bool free = e.constraint == Entity::Constraint::Free;
  switch (e.kind) {
    case EntityKind::Line:
      return param == EditParam::LineLength || (param == EditParam::LineAzimuth && e.constraint == Entity::Constraint::Fixed);
    case EntityKind::Arc:
      if (param == EditParam::CurveRadius) return true;
      if (free) return false;  // its length, delta and direction come from its two tangents
      return param == EditParam::CurveDirection || (param == EditParam::CurveLength && e.definedByLength) ||
             (param == EditParam::CurveDelta && !e.definedByLength);
    case EntityKind::Spiral:
      return param == EditParam::CurveLength || param == EditParam::CurveRadius || param == EditParam::SpiralStartRadius;
  }
  return false;
}

const char* ParameterConstraintName(const Entity& e) {
  switch (e.kind) {
    case EntityKind::Line: return e.constraint == Entity::Constraint::Fixed ? "Length, bearing" : "Length";
    case EntityKind::Arc:
      if (e.constraint == Entity::Constraint::Free) return "Radius (between tangents)";
      return e.definedByLength ? "Radius, length" : "Radius, delta";
    case EntityKind::Spiral: return "Length, radii";
  }
  return "";
}

bool Alignment::StationLocksHold(const Alignment& before, int inserted, int removed, std::string* whyNot) const {
  assert(inserted < 0 || removed < 0);
  assert(inserted <= static_cast<int>(entities_.size()));
  std::string moved;
  const int n = static_cast<int>(before.entities_.size());
  for (int j = 0; j < n; ++j) {
    if ((before.entities_[static_cast<size_t>(j)].locks & Entity::kLockStation) == 0) continue;
    if (j == removed) {
      if (whyNot != nullptr) *whyNot = Row(j) + " has a fixed station and cannot be removed; unlock its station first";
      return false;
    }
    const int k = (inserted >= 0 && j >= inserted) ? j + 1 : (removed >= 0 && j > removed) ? j - 1 : j;
    if (k < 0 || k >= static_cast<int>(entities_.size())) continue;
    const double was = before.EntityStartStation(j);
    const double now = EntityStartStation(k);
    if (std::fabs(now - was) <= 1e-9) continue;
    moved += (moved.empty() ? "" : "; ") + Row(j) + " would move from " + FormatStation(before.DisplayedStation(was, true)) +
             " to " + FormatStation(DisplayedStation(now, true));
  }
  if (moved.empty()) return true;
  if (whyNot != nullptr) *whyNot = "a fixed station would move (" + moved + ")";
  return false;
}

bool Alignment::InsertTangentAfter(int i, double length, std::string* whyNot) {
  auto fail = [&](const char* m) {
    if (whyNot != nullptr) *whyNot = m;
    return false;
  };
  if (i < 0 || static_cast<size_t>(i) >= entities_.size()) return fail("no such entity");
  if (!(length > 0.0)) return fail("the length must be positive");
  Alignment work = *this;
  Entity e;
  e.kind = EntityKind::Line;
  e.length = length;
  e.constraint = Entity::Constraint::FloatOnPrev;
  work.entities_.insert(work.entities_.begin() + i + 1, e);
  std::string why;
  if (!work.Resolve(&why)) return fail(why.c_str());
  work.Rebuild();
  if (!work.StationLocksHold(*this, i + 1, -1, &why)) return fail(why.c_str());
  *this = std::move(work);
  PruneOrphanEquations();
  return true;
}

bool Alignment::InsertCurveAfter(int i, double radius, double arcLength, bool right, std::string* whyNot) {
  auto fail = [&](const char* m) {
    if (whyNot != nullptr) *whyNot = m;
    return false;
  };
  if (i < 0 || static_cast<size_t>(i) >= entities_.size()) return fail("no such entity");
  if (!(radius > 0.0) || !(arcLength > 0.0)) return fail("the radius and length must be positive");
  Alignment work = *this;
  Entity e;
  e.kind = EntityKind::Arc;
  e.radius = radius;
  e.length = arcLength;
  e.right = right;
  e.constraint = Entity::Constraint::FloatOnPrev;
  e.definedByLength = false;  // ALIGNMENTINSERT enters a curve by radius and delta (D-2026-11-04-f)
  work.entities_.insert(work.entities_.begin() + i + 1, e);
  std::string why;
  if (!work.Resolve(&why)) return fail(why.c_str());
  work.Rebuild();
  if (!work.StationLocksHold(*this, i + 1, -1, &why)) return fail(why.c_str());
  *this = std::move(work);
  PruneOrphanEquations();
  return true;
}

bool Alignment::InsertSpiralAfter(int i, double length, double startRadius, double endRadius, bool right, std::string* whyNot) {
  auto fail = [&](const char* m) {
    if (whyNot != nullptr) *whyNot = m;
    return false;
  };
  if (i < 0 || static_cast<size_t>(i) >= entities_.size()) return fail("no such entity");
  if (!(length > 0.0)) return fail("the length must be positive");
  Alignment work = *this;
  Entity e;
  e.kind = EntityKind::Spiral;
  e.length = length;
  e.startRadius = startRadius;
  e.radius = endRadius;
  e.right = right;
  e.constraint = Entity::Constraint::FloatOnPrev;
  work.entities_.insert(work.entities_.begin() + i + 1, e);
  std::string why;
  if (!work.Resolve(&why)) return fail(why.c_str());
  work.Rebuild();
  if (!work.StationLocksHold(*this, i + 1, -1, &why)) return fail(why.c_str());
  *this = std::move(work);
  PruneOrphanEquations();
  return true;
}

bool Alignment::DeleteEntity(int i, std::string* whyNot) {
  if (i < 0 || static_cast<size_t>(i) >= entities_.size()) {
    if (whyNot != nullptr) *whyNot = "no such entity";
    return false;
  }
  Alignment work = *this;
  work.entities_.erase(work.entities_.begin() + i);
  if (!work.entities_.empty()) {
    // The new first entity cannot follow something that no longer precedes it; it keeps the position it solved to.
    if (work.entities_.front().constraint == Entity::Constraint::FloatOnPrev)
      work.entities_.front().constraint = Entity::Constraint::Fixed;
    if (work.entities_.back().constraint == Entity::Constraint::FloatOnNext)
      work.entities_.back().constraint = Entity::Constraint::Fixed;
  }
  std::string why;
  if (!work.Resolve(&why)) {
    if (whyNot != nullptr) *whyNot = why;
    return false;
  }
  work.Rebuild();
  if (!work.StationLocksHold(*this, -1, i, &why)) {
    if (whyNot != nullptr) *whyNot = why;
    return false;
  }
  *this = std::move(work);
  PruneOrphanEquations();
  return true;
}

int Alignment::PruneOrphanEquations() {
  int removed = 0;
  for (auto it = equations.begin(); it != equations.end();) {
    if (it->rawBack > EndStation() + 1e-9 || entities_.empty()) {
      it = equations.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  return removed;
}

// ---------------------------------------------------------------- transforms

void Alignment::Translate(double dx, double dy) {
  for (Entity& e : entities_) {
    e.start.x += dx;
    e.start.y += dy;
  }
}

void Alignment::Rotate(double cx, double cy, double cw) {
  const double c = std::cos(cw), s = std::sin(cw);  // clockwise rotation of (x,y) with y north
  for (Entity& e : entities_) {
    const double x = e.start.x - cx, y = e.start.y - cy;
    e.start = {cx + x * c + y * s, cy - x * s + y * c};
    e.startAzimuth = NormAz(e.startAzimuth + cw);
  }
}

bool Alignment::Scale(double cx, double cy, double f) {
  if (!(f > 0.0) || !std::isfinite(f)) return false;
  for (Entity& e : entities_) {
    e.start = {cx + (e.start.x - cx) * f, cy + (e.start.y - cy) * f};
    e.length *= f;
    if (e.kind == EntityKind::Arc) e.radius *= f;
    if (e.kind == EntityKind::Spiral) {
      if (!IsInfiniteSpiralRadius(e.startRadius)) e.startRadius *= f;
      if (!IsInfiniteSpiralRadius(e.radius)) e.radius *= f;
    }
  }
  Rebuild();
  for (StationEquation& q : equations) {  // distances scale, so raw stations measured from the start do too
    q.rawBack = startingStation + (q.rawBack - startingStation) * f;
  }
  return true;
}

bool Alignment::Mirror(double ax, double ay, double bx, double by) {
  const double dx = bx - ax, dy = by - ay;
  const double len = std::hypot(dx, dy);
  if (!(len > 0.0)) return false;
  const double ux = dx / len, uy = dy / len;
  const auto reflect = [&](double x, double y) {
    const double rx = x - ax, ry = y - ay;
    const double dot = rx * ux + ry * uy;
    return Point2{ax + 2.0 * dot * ux - rx, ay + 2.0 * dot * uy - ry};
  };
  for (Entity& e : entities_) {
    e.start = reflect(e.start.x, e.start.y);
    const double vx = std::sin(e.startAzimuth), vy = std::cos(e.startAzimuth);
    const double dot = vx * ux + vy * uy;
    const double mx = 2.0 * dot * ux - vx, my = 2.0 * dot * uy - vy;  // reflected direction
    e.startAzimuth = NormAz(std::atan2(mx, my));
    if (e.kind == EntityKind::Arc) e.right = !e.right;
    if (e.kind == EntityKind::Spiral) {
      e.right = !e.right;
      std::swap(e.startRadius, e.radius);
    }
  }
  return true;
}


// ---------------------------------------------------------------- grips (REQ-487.1)

std::vector<Grip> GripPoints(const Alignment& a) {
  std::vector<Grip> g;
  if (a.Empty()) return g;
  g.push_back({a.StartPoint(), 0});
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    const Entity& e = a.Entities()[i];
    const int ii = static_cast<int>(i);
    if (e.kind == EntityKind::Line || e.kind == EntityKind::Spiral) {
      g.push_back({a.GetPointAtStation(a.EntityEndStation(ii)), 1 + 2 * static_cast<int>(i)});
    }
    if (e.kind == EntityKind::Arc || e.kind == EntityKind::Spiral) {
      g.push_back({a.GetPointAtStation(a.EntityStartStation(ii) + e.length * 0.5), 2 + 2 * static_cast<int>(i)});
    }
  }
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
  const int i = (which - 1) / 2;
  if (i < 0 || static_cast<size_t>(i) >= orig.Entities().size()) return false;
  const Entity& e = orig.Entities()[static_cast<size_t>(i)];
  const bool isEnd = (which % 2) == 1;
  if (isEnd && e.kind == EntityKind::Line) {
    const double dx = to.x - e.start.x, dy = to.y - e.start.y;
    Alignment w = orig;
    if (e.constraint == Entity::Constraint::Fixed) {
      const double len = std::hypot(dx, dy);
      if (!(len > 1e-9)) return true;
      if (w.EditEntity(i, EditParam::LineAzimuth, std::atan2(dx, dy))) {
        if (w.EditEntity(i, EditParam::LineLength, len)) *out = w;
      } else {
        // Bearing locked or underivable: slide along the existing bearing instead.
        const double along = dx * std::sin(e.startAzimuth) + dy * std::cos(e.startAzimuth);
        Alignment s = orig;
        if (along > 1e-9 && s.EditEntity(i, EditParam::LineLength, along)) *out = s;
      }
    } else {
      const double along = dx * std::sin(e.startAzimuth) + dy * std::cos(e.startAzimuth);  // a floating tangent keeps its bearing
      Alignment s = orig;
      if (along > 1e-9 && s.EditEntity(i, EditParam::LineLength, along)) *out = s;
    }
    return true;
  }
  if (isEnd && e.kind == EntityKind::Spiral) {
    const double along = (to.x - e.start.x) * std::sin(e.startAzimuth) + (to.y - e.start.y) * std::cos(e.startAzimuth);
    Alignment w = orig;
    if (along > 1e-9 && w.EditEntity(i, EditParam::CurveLength, along)) *out = w;
    return true;
  }
  if (!isEnd && e.kind == EntityKind::Spiral) {
    const double sMid = e.length * 0.5;
    const double staMid = orig.EntityStartStation(i) + sMid;
    const Point2 pMid = orig.GetPointAtStation(staMid);
    const double az = orig.GetBearingAtStation(staMid);
    const double kMidCur = SpiralKappaAt(e, sMid);
    if (!(std::fabs(kMidCur) > 1e-14)) return true;
    const double rInst = 1.0 / std::fabs(kMidCur);
    const double rx = std::cos(az);
    const double ry = -std::sin(az);
    const Point2 center =
        e.right ? Point2{pMid.x + rInst * rx, pMid.y + rInst * ry} : Point2{pMid.x - rInst * rx, pMid.y + rInst * ry};
    const double rNew = std::hypot(to.x - center.x, to.y - center.y);
    if (!(rNew > 1e-6)) return true;
    const double sign = e.right ? 1.0 : -1.0;
    const double kEnd = 2.0 * (sign / rNew) - EntityStartCurvature(e);
    if (!(std::fabs(kEnd) > 1e-14)) return true;
    Alignment w = orig;
    if (w.EditEntity(i, EditParam::CurveRadius, 1.0 / std::fabs(kEnd))) *out = w;
    return true;
  }
  if (!isEnd && e.kind == EntityKind::Arc) {
    CurveData cd;
    (void)orig.CurveDataFor(i, &cd);
    const double r = std::hypot(to.x - cd.center.x, to.y - cd.center.y);
    Alignment w = orig;
    if (r > 1e-6 && w.EditEntity(i, EditParam::CurveRadius, r)) *out = w;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------- offsets (REQ-488)

bool MakeOffset(const Alignment& parent, double distance, Alignment* out, std::string* whyNot) {
  auto fail = [&](const std::string& m) {
    if (whyNot != nullptr) *whyNot = m;
    return false;
  };
  if (out == nullptr) return false;
  if (parent.Empty()) return fail("the parent alignment has no geometry");
  if (!std::isfinite(distance) || distance == 0.0) return fail("the offset distance must be a non-zero number");
  for (size_t si = 0; si < parent.Entities().size(); ++si) {
    if (parent.Entities()[si].kind == EntityKind::Spiral)
      return fail("entity " + std::to_string(si + 1) +
                  " is a spiral; GoSurvey does not offset alignments that contain spirals");
  }
  for (const ContinuityIssue& c : parent.CheckContinuity()) {
    if (!c.c0) return fail("the parent has a gap after entity " + std::to_string(c.afterEntity + 1) + "; an offset needs a continuous alignment");
    if (!c.c1)
      return fail("the parent changes direction after entity " + std::to_string(c.afterEntity + 1) +
                  " (not tangent); GoSurvey does not trim or extend offset corners");
  }
  Alignment made;
  for (size_t i = 0; i < parent.Entities().size(); ++i) {
    Entity e = parent.Entities()[i];
    const double az = e.startAzimuth;
    const double rx = std::cos(az), ry = -std::sin(az);  // right of travel
    e.start = {e.start.x + distance * rx, e.start.y + distance * ry};
    if (e.kind == EntityKind::Arc) {
      // Right (clockwise) curves have their centre on the right: a right offset moves toward the centre.
      const double newR = e.right ? e.radius - distance : e.radius + distance;
      if (!(newR > 0.0))
        return fail("entity " + std::to_string(i + 1) + " is a curve of radius " + std::to_string(e.radius) +
                    "; an offset of " + std::to_string(distance) + " would collapse it");
      const double delta = e.length / e.radius;
      e.radius = newR;
      e.length = newR * delta;
    } else if (e.kind == EntityKind::Spiral) {
      return fail("entity " + std::to_string(i + 1) + " is a spiral; GoSurvey does not offset spirals");
    }
    e.locks = 0;
    e.constraint = i == 0 ? Entity::Constraint::Fixed : Entity::Constraint::FloatOnPrev;
    if (!made.AppendRaw(e)) return fail("entity " + std::to_string(i + 1) + " could not be offset");
  }
  made.id = out->id;
  made.name = out->name;
  made.description = out->description;
  made.layer = out->layer;
  made.style = out->style;
  made.type = AlignmentType::Offset;
  made.startingStation = parent.startingStation;  // GoSurvey rule: stationing starts where the parent's does
  made.parentId = parent.id;
  made.offsetDistance = distance;
  *out = std::move(made);
  return true;
}

// ---------------------------------------------------------------- intersections (REQ-489)

Alignment MakeLineObject(Point2 a, Point2 b) {
  Alignment o;
  const double dx = b.x - a.x, dy = b.y - a.y;
  (void)o.AppendTangent(a, std::atan2(dx, dy), std::hypot(dx, dy));
  return o;
}

Alignment MakeCircleObject(Point2 c, double r, double startAngleCcw, double sweepCcw) {
  Alignment o;
  Entity e;
  e.kind = EntityKind::Arc;
  e.radius = r;
  e.length = r * std::fabs(sweepCcw);
  const double a0 = sweepCcw >= 0.0 ? startAngleCcw : startAngleCcw + sweepCcw;  // always run the arc CCW from a0
  e.start = {c.x + r * std::cos(a0), c.y + r * std::sin(a0)};
  e.right = false;
  e.startAzimuth = NormAz(std::atan2(-std::sin(a0), std::cos(a0)));
  if (sweepCcw < 0.0) {  // clockwise: start at the CCW end, run right
    const double a1 = a0 + std::fabs(sweepCcw);
    e.start = {c.x + r * std::cos(a1), c.y + r * std::sin(a1)};
    e.right = true;
    e.startAzimuth = NormAz(std::atan2(std::sin(a1), -std::cos(a1)));
  }
  (void)o.AppendRaw(e);
  return o;
}

namespace {

struct Cand {
  Point2 p;
  double sA = 0.0;  ///< Distance along entity A.
  double sB = 0.0;
  bool tangential = false;
};

/// Distance along an arc entity (from its start) of a point known to lie on its circle; -1 if outside the sweep.
double ArcParam(const Entity& e, Point2 p, Point2 centre) {
  const double sgn = e.right ? 1.0 : -1.0;
  const double a0 = std::atan2(e.start.x - centre.x, e.start.y - centre.y);
  const double ap = std::atan2(p.x - centre.x, p.y - centre.y);
  double phi = std::fmod(sgn * (ap - a0), kTwoPi);
  if (phi < 0.0) phi += kTwoPi;
  const double sweep = e.length / e.radius;
  constexpr double kTol = 1e-9;
  if (phi >= kTwoPi - kTol) phi = 0.0;
  if (phi > sweep + kTol) return -1.0;
  return std::min(phi * e.radius, e.length);
}

Point2 CentreOf(const Entity& e) {
  const double sgn = e.right ? 1.0 : -1.0;
  return {e.start.x + sgn * e.radius * std::cos(e.startAzimuth), e.start.y - sgn * e.radius * std::sin(e.startAzimuth)};
}

void LineLine(const Entity& a, const Entity& b, std::vector<Cand>* out, std::vector<IntersectionOverlap>* overlaps, double baseA) {
  const double d1x = std::sin(a.startAzimuth), d1y = std::cos(a.startAzimuth);
  const double d2x = std::sin(b.startAzimuth), d2y = std::cos(b.startAzimuth);
  const double cross = d1x * d2y - d1y * d2x;
  const double rx = b.start.x - a.start.x, ry = b.start.y - a.start.y;
  constexpr double kTol = 1e-9;
  if (std::fabs(cross) < 1e-12) {
    if (std::fabs(rx * d1y - ry * d1x) > 1e-7) return;  // parallel, not collinear
    // Collinear: project B's ends onto A and report the shared range.
    const double t0 = rx * d1x + ry * d1y;
    const double t1 = t0 + b.length * (d1x * d2x + d1y * d2y);
    const double lo = std::max(0.0, std::min(t0, t1)), hi = std::min(a.length, std::max(t0, t1));
    if (hi - lo > kTol && overlaps != nullptr) overlaps->push_back({baseA + lo, baseA + hi});
    if (std::fabs(hi - lo) <= kTol && hi >= -kTol && lo <= a.length + kTol) {  // touch at one end
      Cand c;
      c.sA = lo;
      c.p = {a.start.x + lo * d1x, a.start.y + lo * d1y};
      c.sB = std::hypot(c.p.x - b.start.x, c.p.y - b.start.y);
      out->push_back(c);
    }
    return;
  }
  const double t = (rx * d2y - ry * d2x) / cross;
  const double u = (rx * d1y - ry * d1x) / cross;
  if (t < -kTol || t > a.length + kTol || u < -kTol || u > b.length + kTol) return;
  Cand c;
  c.sA = std::clamp(t, 0.0, a.length);
  c.sB = std::clamp(u, 0.0, b.length);
  c.p = {a.start.x + t * d1x, a.start.y + t * d1y};
  out->push_back(c);
}

void LineArc(const Entity& line, const Entity& arc, std::vector<Cand>* out, bool lineIsA) {
  const Point2 c = CentreOf(arc);
  const double dx = std::sin(line.startAzimuth), dy = std::cos(line.startAzimuth);
  const double fx = line.start.x - c.x, fy = line.start.y - c.y;
  const double b = fx * dx + fy * dy;
  const double disc = b * b - (fx * fx + fy * fy - arc.radius * arc.radius);
  constexpr double kTol = 1e-9;
  if (disc < -1e-9 * std::max(1.0, arc.radius * arc.radius)) return;
  const double root = std::sqrt(std::max(0.0, disc));
  const bool tangential = root < 1e-6 * std::max(1.0, arc.radius);
  for (const double t : tangential ? std::vector<double>{-b} : std::vector<double>{-b - root, -b + root}) {
    if (t < -kTol || t > line.length + kTol) continue;
    const Point2 p{line.start.x + t * dx, line.start.y + t * dy};
    const double sArc = ArcParam(arc, p, c);
    if (sArc < 0.0) continue;
    Cand cd;
    cd.p = p;
    cd.tangential = tangential;
    const double sLine = std::clamp(t, 0.0, line.length);
    cd.sA = lineIsA ? sLine : sArc;
    cd.sB = lineIsA ? sArc : sLine;
    out->push_back(cd);
  }
}

void ArcArc(const Entity& a, const Entity& b, std::vector<Cand>* out) {
  const Point2 ca = CentreOf(a), cb = CentreOf(b);
  const double dx = cb.x - ca.x, dy = cb.y - ca.y;
  const double d = std::hypot(dx, dy);
  const double r1 = a.radius, r2 = b.radius;
  if (d < 1e-12) return;  // concentric: no isolated points
  const double tolR = 1e-9 * std::max(r1, r2);
  if (d > r1 + r2 + tolR || d < std::fabs(r1 - r2) - tolR) return;
  const double x = (d * d + r1 * r1 - r2 * r2) / (2.0 * d);
  const double h2 = r1 * r1 - x * x;
  const bool tangential = h2 < 1e-12 * std::max(1.0, r1 * r1);
  const double h = std::sqrt(std::max(0.0, h2));
  const double ux = dx / d, uy = dy / d;
  const Point2 mid{ca.x + x * ux, ca.y + x * uy};
  const Point2 pts[2] = {{mid.x - h * uy, mid.y + h * ux}, {mid.x + h * uy, mid.y - h * ux}};
  for (int k = 0; k < (tangential ? 1 : 2); ++k) {
    const double sa = ArcParam(a, pts[k], ca), sb = ArcParam(b, pts[k], cb);
    if (sa < 0.0 || sb < 0.0) continue;
    Cand c;
    c.p = pts[k];
    c.sA = sa;
    c.sB = sb;
    c.tangential = tangential;
    out->push_back(c);
  }
}

}  // namespace

std::vector<Intersection> IntersectAlignments(const Alignment& a, const Alignment& b,
                                              std::vector<IntersectionOverlap>* overlaps) {
  std::vector<Intersection> result;
  for (size_t i = 0; i < a.Entities().size(); ++i) {
    const Entity& ea = a.Entities()[i];
    const double baseA = a.EntityStartStation(static_cast<int>(i));
    for (size_t j = 0; j < b.Entities().size(); ++j) {
      const Entity& eb = b.Entities()[j];
      const double baseB = b.EntityStartStation(static_cast<int>(j));
      std::vector<Cand> cs;
      if (ea.kind == EntityKind::Line && eb.kind == EntityKind::Line) LineLine(ea, eb, &cs, overlaps, baseA);
      else if (ea.kind == EntityKind::Line) LineArc(ea, eb, &cs, true);
      else if (eb.kind == EntityKind::Line) LineArc(eb, ea, &cs, false);
      else ArcArc(ea, eb, &cs);
      for (const Cand& c : cs) {
        Intersection it;
        it.point = c.p;
        it.stationA = baseA + c.sA;
        it.stationB = baseB + c.sB;
        it.tangential = c.tangential;
        const double azA = a.GetBearingAtStation(it.stationA), azB = b.GetBearingAtStation(it.stationB);
        double ang = std::fabs(NormAz(azA) - NormAz(azB));
        if (ang > kPi) ang = kTwoPi - ang;
        if (ang > kPi * 0.5) ang = kPi - ang;
        it.angle = ang;
        // The same crossing found at a junction of two entities is one intersection.
        bool dup = false;
        for (const Intersection& q : result) {
          if (std::hypot(q.point.x - it.point.x, q.point.y - it.point.y) < 1e-7) {
            dup = true;
            break;
          }
        }
        if (!dup) result.push_back(it);
      }
    }
  }
  std::sort(result.begin(), result.end(), [](const Intersection& x, const Intersection& y) { return x.stationA < y.stationA; });
  return result;
}

}  // namespace alignment
