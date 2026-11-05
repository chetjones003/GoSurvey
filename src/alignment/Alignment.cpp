#include "alignment/Alignment.hpp"
#include "alignment/Clothoid.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>

namespace alignment {
namespace {

double NormAz(double a) {
  a = std::fmod(a, kTwoPi);
  if (a < 0.0) a += kTwoPi;
  return a;
}

/// sin(x)/x without the cancellation near zero (long, shallow curves; REQ-499).
double Sinc(double x) {
  if (std::fabs(x) < 1e-4) {
    const double x2 = x * x;
    return 1.0 - x2 / 6.0 + x2 * x2 / 120.0;
  }
  return std::sin(x) / x;
}

Point2 Add(Point2 a, double dx, double dy) { return {a.x + dx, a.y + dy}; }

double Dist(Point2 a, Point2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

/// Displacement from an entity start after travelling `s` along it, relative to the start.
Point2 LocalDisplacement(const Entity& e, double s) {
  if (e.kind == EntityKind::Line) return {s * std::sin(e.startAzimuth), s * std::cos(e.startAzimuth)};
  if (e.kind == EntityKind::Spiral) return SpiralDisplacementLocal(e, s);
  const double theta = (e.right ? 1.0 : -1.0) * s / e.radius;
  const double chord = s * Sinc(theta * 0.5);
  const double mid = e.startAzimuth + theta * 0.5;
  return {chord * std::sin(mid), chord * std::cos(mid)};
}

}  // namespace

bool ContinuityIssue::FailsRequirement(ContinuityRequirement level) const {
  if (level == ContinuityRequirement::C0) return !c0;
  if (!c0) return true;
  if (level == ContinuityRequirement::C1) return !c1;
  if (!c1) return true;
  return !c2;
}

// ---------------------------------------------------------------- building

void Alignment::Rebuild() {
  cumStart_.assign(entities_.size() + 1, 0.0);
  for (size_t i = 0; i < entities_.size(); ++i) cumStart_[i + 1] = cumStart_[i] + entities_[i].length;
  length_ = cumStart_.back();
}

void Alignment::Clear() {
  entities_.clear();
  Rebuild();
}

double Alignment::SignedCurvature(const Entity& e) const { return SignedCurvatureAt(e, 0.0); }

double Alignment::SignedCurvatureAt(const Entity& e, double s) const {
  if (e.kind == EntityKind::Line) return 0.0;
  if (e.kind == EntityKind::Arc) return (e.right ? 1.0 : -1.0) / e.radius;
  return SpiralKappaAt(e, s);
}

Point2 Alignment::PointInEntity(int i, double s) const {
  const Entity& e = entities_[static_cast<size_t>(i)];
  const Point2 d = LocalDisplacement(e, s);
  return Add(e.start, d.x, d.y);
}

double Alignment::AzimuthInEntity(int i, double s) const {
  const Entity& e = entities_[static_cast<size_t>(i)];
  if (e.kind == EntityKind::Spiral) return NormAz(e.startAzimuth + SpiralHeadingDelta(e, s));
  return NormAz(e.startAzimuth + SignedCurvatureAt(e, s) * s);
}

bool Alignment::AppendRaw(const Entity& e) {
  if (!(e.length > 0.0) || !std::isfinite(e.length) || !std::isfinite(e.start.x) || !std::isfinite(e.start.y) ||
      !std::isfinite(e.startAzimuth))
    return false;
  if (e.kind == EntityKind::Arc) {
    if (!(e.radius > 0.0) || !std::isfinite(e.radius)) return false;
    if (e.length / e.radius >= kTwoPi) return false;  // a full circle is not an alignment curve
  }
  if (e.kind == EntityKind::Spiral) {
    if (!std::isfinite(e.startRadius) || !std::isfinite(e.radius)) return false;
    const double k0 = EntityStartCurvature(e);
    const double k1 = EntityEndCurvature(e);
    if (std::fabs(k1 - k0) < 1e-15) return false;  // zero curvature change is a tangent, not a spiral
  }
  Entity c = e;
  c.startAzimuth = NormAz(c.startAzimuth);
  entities_.push_back(c);
  if (cumStart_.size() == entities_.size()) {  // the index is current: extend it, so building n entities is O(n)
    cumStart_.push_back(cumStart_.back() + c.length);
    length_ = cumStart_.back();
  } else {
    Rebuild();
  }
  assert(cumStart_.size() == entities_.size() + 1);
  return true;
}

bool Alignment::AppendTangent(Point2 start, double azimuth, double length) {
  Entity e;
  e.kind = EntityKind::Line;
  e.start = start;
  e.startAzimuth = azimuth;
  e.length = length;
  return AppendRaw(e);
}

bool Alignment::AppendTangent(double length) {
  if (entities_.empty()) return false;  // the first entity needs an explicit start and azimuth
  Entity e;
  e.kind = EntityKind::Line;
  e.start = EndPoint();
  e.startAzimuth = EndAzimuth();
  e.length = length;
  e.constraint = Entity::Constraint::FloatOnPrev;  // continues tangent-continuously, so it follows what precedes it
  return AppendRaw(e);
}

bool Alignment::AppendTangentTo(Point2 end) {
  if (entities_.empty()) return false;
  const Point2 from = EndPoint();
  const double dx = end.x - from.x;
  const double dy = end.y - from.y;
  const double len = std::hypot(dx, dy);
  if (!(len > 0.0)) return false;
  return AppendTangent(from, std::atan2(dx, dy), len);
}

bool Alignment::AppendCurve(double radius, double arcLength, bool right) {
  if (entities_.empty()) return false;
  Entity e;
  e.kind = EntityKind::Arc;
  e.start = EndPoint();
  e.startAzimuth = EndAzimuth();
  e.radius = radius;
  e.length = arcLength;
  e.right = right;
  e.constraint = Entity::Constraint::FloatOnPrev;
  e.definedByLength = true;
  return AppendRaw(e);
}

bool Alignment::AppendCurveByDelta(double radius, double delta, bool right) {
  if (!AppendCurve(radius, radius * delta, right)) return false;
  entities_.back().definedByLength = false;
  return true;
}

bool Alignment::AppendSpiral(double length, double startRadius, double endRadius, bool right) {
  if (entities_.empty() || !(length > 0.0)) return false;
  Entity e;
  e.kind = EntityKind::Spiral;
  e.start = EndPoint();
  e.startAzimuth = EndAzimuth();
  e.length = length;
  e.startRadius = startRadius;
  e.radius = endRadius;
  e.right = right;
  e.constraint = Entity::Constraint::FloatOnPrev;
  return AppendRaw(e);
}

bool Alignment::Reverse(std::string* note) {
  if (entities_.empty()) return false;
  for (const ContinuityIssue& c : CheckContinuity()) {
    if (!c.c0) return false;
  }
  hasReference = false;  // the new start is the reference point; the starting station is kept (D-2026-11-04-n)
  referenceDistance = 0.0;
  std::vector<Entity> rev;
  rev.reserve(entities_.size());
  for (int i = static_cast<int>(entities_.size()) - 1; i >= 0; --i) {
    Entity e = entities_[static_cast<size_t>(i)];
    const Point2 end = PointInEntity(i, e.length);
    const double endAz = AzimuthInEntity(i, e.length);
    e.start = end;
    e.startAzimuth = NormAz(endAz + kPi);
    if (e.kind == EntityKind::Arc) e.right = !e.right;
    if (e.kind == EntityKind::Spiral) {
      e.right = !e.right;
      std::swap(e.startRadius, e.radius);
    }
    // What followed its predecessor now follows its successor, and vice versa.
    if (e.constraint == Entity::Constraint::FloatOnPrev) e.constraint = Entity::Constraint::FloatOnNext;
    else if (e.constraint == Entity::Constraint::FloatOnNext) e.constraint = Entity::Constraint::FloatOnPrev;
    rev.push_back(e);
  }
  entities_ = std::move(rev);
  Rebuild();
  std::string said;
  if (!equations.empty()) said = std::to_string(equations.size()) + " station equation(s) cleared by the reversal";
  equations.clear();
  // REQ-487.2: what hangs off the stationing follows it. A station S becomes start + L - (S - start); right is left.
  const double flipAbout = 2.0 * startingStation + length_;
  for (StationOffsetLabel& l : stationLabels) {
    if (l.fixed) continue;  // a fixed label keeps its plan point and re-reads its station
    l.internalStation = flipAbout - l.internalStation;
    l.offset = -l.offset;
  }
  for (double& s : segmentLabelStations) s = flipAbout - s;  // a single segment label stays on its segment
  if (!designSpeeds.empty()) {  // each speed applied from its station to the next; reversed, from the other end
    std::vector<DesignSpeed> flipped;
    flipped.reserve(designSpeeds.size());
    for (size_t k = 0; k < designSpeeds.size(); ++k) {
      const double to = k + 1 < designSpeeds.size() ? designSpeeds[k + 1].internalStation : EndStation();
      flipped.push_back({flipAbout - to, designSpeeds[k].speed});
    }
    if (designSpeeds.front().internalStation > startingStation + 1e-9)
      said += std::string(said.empty() ? "" : "; ") + "the stretch that had no design speed now takes the speed before it";
    designSpeeds.assign(flipped.rbegin(), flipped.rend());
  }
  if (note != nullptr) *note = said;
  return true;
}

// ---------------------------------------------------------------- basic data

double Alignment::EntityStartStation(int i) const { return startingStation + cumStart_[static_cast<size_t>(i)]; }
double Alignment::EntityEndStation(int i) const { return startingStation + cumStart_[static_cast<size_t>(i) + 1]; }

Point2 Alignment::StartPoint() const { return entities_.empty() ? Point2{} : entities_.front().start; }
Point2 Alignment::EndPoint() const {
  if (entities_.empty()) return {};
  const int last = static_cast<int>(entities_.size()) - 1;
  return PointInEntity(last, entities_.back().length);
}
double Alignment::StartAzimuth() const { return entities_.empty() ? 0.0 : entities_.front().startAzimuth; }
double Alignment::EndAzimuth() const {
  if (entities_.empty()) return 0.0;
  const int last = static_cast<int>(entities_.size()) - 1;
  return AzimuthInEntity(last, entities_.back().length);
}

int Alignment::CurveCount() const {
  int n = 0;
  for (const Entity& e : entities_) n += e.kind == EntityKind::Arc ? 1 : 0;
  return n;
}

int Alignment::SpiralCount() const {
  int n = 0;
  for (const Entity& e : entities_) n += e.kind == EntityKind::Spiral ? 1 : 0;
  return n;
}

// ---------------------------------------------------------------- queries

int Alignment::GetEntityAtStation(double internalStation) const {
  int steps = 0;
  return EntityAtStationCounted(internalStation, &steps);
}

int Alignment::EntityAtStationCounted(double internalStation, int* steps) const {
  assert(steps != nullptr);
  assert(cumStart_.size() == entities_.size() + 1 || entities_.empty());
  *steps = 0;
  if (entities_.empty()) return -1;
  const double d = std::clamp(internalStation - startingStation, 0.0, length_);
  // First entity whose END is at or beyond d: the earlier entity wins at a junction. Binary search over the entity
  // end distances (REQ-484.5); `steps` counts the entities it looks at.
  size_t lo = 0, hi = entities_.size() - 1;
  constexpr int kMaxSteps = 64;  // log2 of any vector size
  while (lo < hi && *steps < kMaxSteps) {
    const size_t mid = lo + (hi - lo) / 2;
    ++*steps;
    if (cumStart_[mid + 1] < d) lo = mid + 1;
    else hi = mid;
  }
  return static_cast<int>(lo);
}

Point2 Alignment::GetPointAtStation(double internalStation) const {
  const int i = GetEntityAtStation(internalStation);
  if (i < 0) return {};
  const double d = std::clamp(internalStation - startingStation, 0.0, length_);
  return PointInEntity(i, std::clamp(d - cumStart_[static_cast<size_t>(i)], 0.0, entities_[static_cast<size_t>(i)].length));
}

double Alignment::GetBearingAtStation(double internalStation) const {
  const int i = GetEntityAtStation(internalStation);
  if (i < 0) return 0.0;
  const double d = std::clamp(internalStation - startingStation, 0.0, length_);
  return AzimuthInEntity(i, std::clamp(d - cumStart_[static_cast<size_t>(i)], 0.0, entities_[static_cast<size_t>(i)].length));
}

Point2 Alignment::GetTangentAtStation(double internalStation) const {
  const double az = GetBearingAtStation(internalStation);
  return {std::sin(az), std::cos(az)};
}

double Alignment::GetCurvatureAtStation(double internalStation) const {
  const int i = GetEntityAtStation(internalStation);
  if (i < 0) return 0.0;
  const double d = std::clamp(internalStation - startingStation, 0.0, length_);
  const double s =
      std::clamp(d - cumStart_[static_cast<size_t>(i)], 0.0, entities_[static_cast<size_t>(i)].length);
  return SignedCurvatureAt(entities_[static_cast<size_t>(i)], s);
}

Point2 Alignment::GetPointFromStationOffset(double internalStation, double offset, double* azimuthOut) const {
  const Point2 p = GetPointAtStation(internalStation);
  const double az = GetBearingAtStation(internalStation);
  if (azimuthOut != nullptr) *azimuthOut = az;
  return {p.x + offset * std::cos(az), p.y - offset * std::sin(az)};  // right of travel = (cos az, -sin az)
}

namespace {

struct Candidate {
  double dist = std::numeric_limits<double>::infinity();
  double s = 0.0;       ///< Distance from entity start.
  double offset = 0.0;  ///< Right positive.
  bool clampedStart = false;
  bool clampedEnd = false;
};

struct SpiralFootResidual {
  double along = 0.0;
  double offset = 0.0;
  double derivative = 0.0;
};

/// The stationarity residual for squared distance to a spiral and its curvature-aware derivative.
SpiralFootResidual EvaluateSpiralFoot(const Entity& e, double px, double py, double s) {
  const Point2 f = LocalDisplacement(e, s);
  const double az = e.startAzimuth + SpiralHeadingDelta(e, s);
  const double fx = px - f.x;
  const double fy = py - f.y;
  const double offset = fx * std::cos(az) - fy * std::sin(az);
  return {fx * std::sin(az) + fy * std::cos(az), offset, -1.0 + SpiralKappaAt(e, s) * offset};
}

/// Refines a grid minimum only when the interval brackets a perpendicular foot. The bracketed Newton step uses the
/// derivative -1 + curvature * offset; bisection keeps it convergent near high offsets and inflection cases.
double RefineSpiralFoot(const Entity& e, double px, double py, double lo, double hi, double s) {
  assert(e.kind == EntityKind::Spiral);
  assert(e.length > 0.0);
  assert(lo <= s && s <= hi);
  if (!(EvaluateSpiralFoot(e, px, py, lo).along >= 0.0 && EvaluateSpiralFoot(e, px, py, hi).along <= 0.0)) return s;

  constexpr int kMaxSteps = 32;
  const double tolerance = 1e-12 * std::max(1.0, e.length);
  for (int k = 0; k < kMaxSteps; ++k) {
    const SpiralFootResidual residual = EvaluateSpiralFoot(e, px, py, s);
    if (std::fabs(residual.along) <= tolerance || hi - lo <= tolerance) break;

    if (residual.along > 0.0)
      lo = s;
    else
      hi = s;

    double next = s - residual.along / residual.derivative;
    if (!std::isfinite(next) || !(next > lo && next < hi)) next = lo + (hi - lo) * 0.5;
    if (next == s) break;
    s = next;
  }
  return s;
}

}  // namespace

StationOffset Alignment::GetStationOffset(double x, double y) const {
  StationOffset r;
  if (entities_.empty()) return r;
  Candidate best;
  int bestIdx = 0;
  bool ambiguous = false;
  double bestAlong = 0.0;

  for (size_t i = 0; i < entities_.size(); ++i) {
    const Entity& e = entities_[i];
    const double px = x - e.start.x;  // local to the entity start (REQ-499)
    const double py = y - e.start.y;
    Candidate c;
    const double sinA = std::sin(e.startAzimuth);
    const double cosA = std::cos(e.startAzimuth);
    if (e.kind == EntityKind::Line) {
      double t = px * sinA + py * cosA;
      // A projection within rounding of an end is on the end, not beyond it (out-of-range stays honest).
      constexpr double kEnd = 1e-9;
      if (t < 0.0) {
        c.clampedStart = t < -kEnd;
        t = 0.0;
      } else if (t > e.length) {
        c.clampedEnd = t > e.length + kEnd;
        t = e.length;
      }
      const double fx = px - t * sinA;
      const double fy = py - t * cosA;
      c.s = t;
      c.dist = std::hypot(fx, fy);
      const double side = fx * cosA - fy * sinA;  // dot with right = (cos, -sin)
      c.offset = (c.clampedStart || c.clampedEnd) ? (side < 0.0 ? -c.dist : c.dist) : side;
    } else if (e.kind == EntityKind::Arc) {
      const double sgn = e.right ? 1.0 : -1.0;
      const double cx = sgn * e.radius * cosA;  // centre relative to start
      const double cy = -sgn * e.radius * sinA;
      const double ux = px - cx;
      const double uy = py - cy;
      const double rho = std::hypot(ux, uy);
      const double sweep = e.length / e.radius;
      double s;
      if (rho == 0.0) {
        s = 0.0;  // the centre is equidistant from every point; take the start
        c.clampedStart = true;
      } else {
        const double angPoint = std::atan2(ux, uy);
        const double angStart = std::atan2(-cx, -cy);
        double phi = sgn * (angPoint - angStart);
        phi = std::fmod(phi, kTwoPi);
        if (phi < 0.0) phi += kTwoPi;
        constexpr double kAng = 1e-12;
        if (phi >= kTwoPi - kAng) phi = 0.0;
        if (phi <= sweep + kAng) {
          s = std::min(phi * e.radius, e.length);
        } else if (phi - sweep < kTwoPi - phi) {
          s = e.length;
          c.clampedEnd = true;
        } else {
          s = 0.0;
          c.clampedStart = true;
        }
      }
      c.s = s;
      if (!(c.clampedStart || c.clampedEnd)) {  // interior, or within rounding of an end
        c.dist = std::fabs(e.radius - rho);
        c.offset = e.right ? (e.radius - rho) : (rho - e.radius);
      } else {
        const Point2 f = LocalDisplacement(e, s);
        const double az = e.startAzimuth + sgn * s / e.radius;
        const double fx = px - f.x;
        const double fy = py - f.y;
        c.dist = std::hypot(fx, fy);
        const double side = fx * std::cos(az) - fy * std::sin(az);
        c.offset = side < 0.0 ? -c.dist : c.dist;
      }
    } else {
      assert(e.kind == EntityKind::Spiral);
      constexpr int kCoarse = 64;
      double sBest = 0.0;
      double dBest = std::numeric_limits<double>::infinity();
      for (int k = 0; k <= kCoarse; ++k) {
        const double s = e.length * static_cast<double>(k) / static_cast<double>(kCoarse);
        const Point2 f = LocalDisplacement(e, s);
        const double d = std::hypot(px - f.x, py - f.y);
        if (d < dBest) {
          dBest = d;
          sBest = s;
        }
      }
      const double ds = e.length / static_cast<double>(kCoarse);
      const double sLo = std::max(0.0, sBest - ds);
      const double sHi = std::min(e.length, sBest + ds);
      constexpr int kFine = 32;
      for (int k = 0; k <= kFine; ++k) {
        const double s = sLo + (sHi - sLo) * static_cast<double>(k) / static_cast<double>(kFine);
        const Point2 f = LocalDisplacement(e, s);
        const double d = std::hypot(px - f.x, py - f.y);
        if (d < dBest) {
          dBest = d;
          sBest = s;
        }
      }
      // The grid finds the right stretch to ~L/1000; Newton steps then find the foot of the perpendicular exactly.
      const double sFoot = RefineSpiralFoot(e, px, py, sLo, sHi, sBest);
      const Point2 fFoot = LocalDisplacement(e, sFoot);
      const double dFoot = std::hypot(px - fFoot.x, py - fFoot.y);
      if (dFoot <= dBest) {
        dBest = dFoot;
        sBest = sFoot;
      }
      constexpr double kEnd = 1e-9;
      const SpiralFootResidual endpoint = EvaluateSpiralFoot(e, px, py, sBest);
      if (sBest <= kEnd) c.clampedStart = endpoint.along < -kEnd;
      if (sBest >= e.length - kEnd) c.clampedEnd = endpoint.along > kEnd;
      c.s = sBest;
      c.dist = dBest;
      if (!(c.clampedStart || c.clampedEnd)) {
        const double az = e.startAzimuth + SpiralHeadingDelta(e, sBest);
        const Point2 f = LocalDisplacement(e, sBest);
        const double fx = px - f.x;
        const double fy = py - f.y;
        const double side = fx * std::cos(az) - fy * std::sin(az);
        c.offset = side < 0.0 ? -c.dist : c.dist;
      } else {
        const Point2 f = LocalDisplacement(e, sBest);
        const double az = e.startAzimuth + SpiralHeadingDelta(e, sBest);
        const double fx = px - f.x;
        const double fy = py - f.y;
        const double side = fx * std::cos(az) - fy * std::sin(az);
        c.offset = side < 0.0 ? -c.dist : c.dist;
      }
    }

    const double along = cumStart_[i] + c.s;
    if (c.dist < best.dist - 1e-12) {
      best = c;
      bestIdx = static_cast<int>(i);
      bestAlong = along;
      ambiguous = false;
    } else if (std::fabs(c.dist - best.dist) <= 1e-9 && std::fabs(along - bestAlong) > 1e-6) {
      ambiguous = true;  // equally close elsewhere; the lower station (found first) is kept
    }
  }

  r.entityIndex = bestIdx;
  r.internalStation = startingStation + bestAlong;
  r.offset = best.offset;
  r.azimuth = AzimuthInEntity(bestIdx, best.s);
  r.closest = PointInEntity(bestIdx, best.s);
  r.ambiguous = ambiguous;
  r.outOfRange = (bestIdx == 0 && best.clampedStart) ||
                 (bestIdx == static_cast<int>(entities_.size()) - 1 && best.clampedEnd);
  return r;
}

StationOffset Alignment::GetStationOffset(double x, double y, bool extrapolate) const {
  StationOffset r = GetStationOffset(x, y);
  if (!extrapolate || !r.outOfRange || entities_.empty()) return r;
  assert(std::isfinite(x) && std::isfinite(y));
  const bool atStart = std::fabs(r.internalStation - StartStation()) < std::fabs(r.internalStation - EndStation());
  const Point2 end = atStart ? StartPoint() : EndPoint();
  const double az = atStart ? StartAzimuth() : EndAzimuth();
  const double dx = x - end.x, dy = y - end.y;
  const double along = dx * std::sin(az) + dy * std::cos(az);  // negative before the start, positive past the end
  r.internalStation = (atStart ? StartStation() : EndStation()) + along;
  r.offset = dx * std::cos(az) - dy * std::sin(az);  // right of travel = (cos az, -sin az)
  r.closest = {end.x + along * std::sin(az), end.y + along * std::cos(az)};
  r.azimuth = az;
  assert(r.outOfRange);
  return r;
}

Point2 Alignment::GetClosestPoint(double x, double y) const { return GetStationOffset(x, y).closest; }

double Alignment::GetDistanceAlongAlignment(double x, double y) const {
  return GetStationOffset(x, y).internalStation - startingStation;
}

// ---------------------------------------------------------------- station equations

double Alignment::EquationDifference(int i) const {
  // diff_i = ahead_i - back_i, where back_i is the displayed station just before equation i.
  double offset = 0.0;
  for (int j = 0; j <= i; ++j) {
    const StationEquation& q = equations[static_cast<size_t>(j)];
    const double back = q.rawBack + offset;
    const double diff = q.ahead - back;
    if (j == i) return diff;
    offset += diff;
  }
  return 0.0;
}

bool Alignment::AddStationEquation(const StationEquation& eq, std::string* whyNot) {
  auto fail = [&](const char* why) {
    if (whyNot != nullptr) *whyNot = why;
    return false;
  };
  if (entities_.empty()) return fail("the alignment has no geometry");
  if (!std::isfinite(eq.rawBack) || !std::isfinite(eq.ahead)) return fail("station is not a number");
  if (eq.rawBack < StartStation() - 1e-9 || eq.rawBack > EndStation() + 1e-9)
    return fail("the equation lies outside the alignment's station range");
  for (const StationEquation& q : equations) {
    if (std::fabs(q.rawBack - eq.rawBack) < 1e-9) return fail("an equation already exists at that station");
  }
  const double back = DisplayedStation(eq.rawBack, false);
  if (eq.increasing && eq.ahead < back) return fail("an increasing equation must have ahead >= back");
  if (!eq.increasing && eq.ahead > back) return fail("a decreasing equation must have ahead <= back");
  const auto it = std::lower_bound(equations.begin(), equations.end(), eq.rawBack,
                                   [](const StationEquation& q, double v) { return q.rawBack < v; });
  equations.insert(it, eq);
  return true;
}

double Alignment::DisplayedStation(double rawStation, bool ahead) const {
  double offset = 0.0;
  // "At" an equation is rounding only: stations 1e-9 apart either side of it are distinct (REQ-483 AC4).
  const double atTolerance = std::max(1e-11, 4.0 * std::numeric_limits<double>::epsilon() * std::fabs(rawStation));
  for (size_t j = 0; j < equations.size(); ++j) {
    const StationEquation& q = equations[j];
    const bool atEq = std::fabs(rawStation - q.rawBack) < atTolerance;
    if (rawStation < q.rawBack && !atEq) break;
    if (atEq && !ahead) break;
    offset += EquationDifference(static_cast<int>(j));
  }
  return rawStation + offset;
}

double Alignment::EndStationWithEquations() const { return DisplayedStation(EndStation(), true); }

std::vector<double> Alignment::RawStationsForDisplayed(double displayed) const {
  std::vector<double> out;
  if (entities_.empty()) return out;
  const double tol = 1e-9;
  double offset = 0.0;
  for (size_t k = 0; k <= equations.size(); ++k) {
    const double zoneStart = k == 0 ? StartStation() : equations[k - 1].rawBack;
    const double zoneEnd = k == equations.size() ? EndStation() : equations[k].rawBack;
    const double raw = displayed - offset;
    if (raw >= zoneStart - tol && raw <= zoneEnd + tol) {
      const double v = std::clamp(raw, zoneStart, zoneEnd);
      if (out.empty() || std::fabs(out.back() - v) > tol) out.push_back(v);
    }
    if (k < equations.size()) offset += EquationDifference(static_cast<int>(k));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end(), [&](double a, double b) { return std::fabs(a - b) <= tol; }),
            out.end());
  return out;
}

// ---------------------------------------------------------------- curve data

CurveData ComputeCurveData(Point2 pc, double startAzimuth, double radius, double arcLength, bool right) {
  CurveData d;
  d.radius = radius;
  d.arcLength = arcLength;
  d.right = right;
  d.delta = arcLength / radius;
  const double half = d.delta * 0.5;
  d.chord = 2.0 * radius * std::sin(half);
  d.hasPI = d.delta < kPi - 1e-9;
  d.tangentLength = d.hasPI ? radius * std::tan(half) : std::numeric_limits<double>::infinity();
  d.external = d.hasPI ? radius * (1.0 / std::cos(half) - 1.0) : std::numeric_limits<double>::infinity();
  d.midOrdinate = radius * (1.0 - std::cos(half));
  d.degreeOfCurveArc = (180.0 / kPi) * 100.0 / radius;
  d.degreeOfCurveChord = radius >= 50.0 ? 2.0 * std::asin(50.0 / radius) * (180.0 / kPi) : 0.0;
  const double sgn = right ? 1.0 : -1.0;
  d.pc = pc;
  d.pi = d.hasPI ? Point2{pc.x + d.tangentLength * std::sin(startAzimuth), pc.y + d.tangentLength * std::cos(startAzimuth)}
                 : pc;
  const double mid = startAzimuth + sgn * half;
  d.pt = {pc.x + d.chord * std::sin(mid), pc.y + d.chord * std::cos(mid)};
  d.center = {pc.x + sgn * radius * std::cos(startAzimuth), pc.y - sgn * radius * std::sin(startAzimuth)};
  return d;
}

bool Alignment::CurveDataFor(int entityIndex, CurveData* out) const {
  if (entityIndex < 0 || static_cast<size_t>(entityIndex) >= entities_.size()) return false;
  const Entity& e = entities_[static_cast<size_t>(entityIndex)];
  if (e.kind != EntityKind::Arc) return false;
  if (out != nullptr) *out = ComputeCurveData(e.start, e.startAzimuth, e.radius, e.length, e.right);
  return true;
}

std::vector<GeometryPoint> Alignment::GeometryPoints() const {
  std::vector<GeometryPoint> pts;
  if (entities_.empty()) return pts;
  auto push = [&](GeometryPointCode code, Point2 p, double station, int idx) {
    pts.push_back({code, p, station, idx});
  };
  const int n = static_cast<int>(entities_.size());
  push(GeometryPointCode::Begin, StartPoint(), StartStation(), 0);
  for (int i = 0; i < n; ++i) {
    const Entity& e = entities_[static_cast<size_t>(i)];
    const Entity* prev = i > 0 ? &entities_[static_cast<size_t>(i) - 1] : nullptr;
    const Entity* next = i + 1 < n ? &entities_[static_cast<size_t>(i) + 1] : nullptr;
    if (e.kind == EntityKind::Line) {
      if (prev != nullptr && prev->kind == EntityKind::Line) push(GeometryPointCode::TanTan, e.start, EntityStartStation(i), i);
      push(GeometryPointCode::LineMidPt, PointInEntity(i, e.length * 0.5), EntityStartStation(i) + e.length * 0.5, i);
    } else if (e.kind == EntityKind::Arc) {
      GeometryPointCode startCode = GeometryPointCode::TanCurve;
      if (prev != nullptr && prev->kind == EntityKind::Arc)
        startCode = prev->right == e.right ? GeometryPointCode::CompCurveCurve : GeometryPointCode::RevCurveCurve;
      push(startCode, e.start, EntityStartStation(i), i);
      CurveData cd;
      (void)CurveDataFor(i, &cd);
      if (cd.hasPI) push(GeometryPointCode::CurvePI, cd.pi, EntityStartStation(i), i);
      push(GeometryPointCode::CurveCenter, cd.center, EntityStartStation(i), i);
      push(GeometryPointCode::CurveMidPt, PointInEntity(i, e.length * 0.5), EntityStartStation(i) + e.length * 0.5, i);
      if (next == nullptr || next->kind == EntityKind::Line)
        push(GeometryPointCode::CurveTan, PointInEntity(i, e.length), EntityEndStation(i), i);
    } else if (e.kind == EntityKind::Spiral) {
      GeometryPointCode startCode = GeometryPointCode::TanSpiral;
      if (prev != nullptr) {
        if (prev->kind == EntityKind::Arc)
          startCode = GeometryPointCode::CurveSpiral;
        else if (prev->kind == EntityKind::Spiral)
          startCode = prev->right == e.right ? GeometryPointCode::SpiralSpiral : GeometryPointCode::RevSpiral;
      }
      push(startCode, e.start, EntityStartStation(i), i);
      SpiralData sd;
      if (SpiralDataFor(i, *this, &sd) && sd.tangentEnd) push(GeometryPointCode::SpiralPI, sd.spi, EntityStartStation(i), i);
      if (next == nullptr || next->kind == EntityKind::Line)
        push(GeometryPointCode::SpiralTan, PointInEntity(i, e.length), EntityEndStation(i), i);
      else if (next != nullptr && next->kind == EntityKind::Arc)
        push(GeometryPointCode::SpiralCurve, PointInEntity(i, e.length), EntityEndStation(i), i);
    }
  }
  push(GeometryPointCode::End, EndPoint(), EndStation(), n - 1);
  return pts;
}

std::vector<ContinuityIssue> Alignment::CheckContinuity(ContinuityTolerance tol) const {
  std::vector<ContinuityIssue> out;
  for (size_t i = 0; i + 1 < entities_.size(); ++i) {
    ContinuityIssue c;
    c.afterEntity = static_cast<int>(i);
    const Entity& left = entities_[i];
    const Entity& right = entities_[i + 1];
    const Point2 end = PointInEntity(static_cast<int>(i), left.length);
    c.gap = Dist(end, right.start);
    const double azEnd = AzimuthInEntity(static_cast<int>(i), left.length);
    double d = std::fabs(NormAz(azEnd) - NormAz(right.startAzimuth));
    if (d > kPi) d = kTwoPi - d;
    c.angleDelta = d;
    c.c0 = c.gap <= tol.position;
    c.c1 = c.angleDelta <= tol.angle;
    const bool spiralJunction = left.kind == EntityKind::Spiral || right.kind == EntityKind::Spiral;
    if (spiralJunction) {
      const double kEnd = EntityEndCurvature(left);
      const double kStart = EntityStartCurvature(right);
      c.curvatureGap = std::fabs(kEnd - kStart);
      c.c2 = c.curvatureGap <= tol.curvature;
    } else {
      c.c2 = true;
    }
    // REQ-482 AC3: a reverse curve changes the sign of the curvature; informational, never a failure.
    const double kLeft = SignedCurvatureAt(left, left.length), kRight = SignedCurvatureAt(right, 0.0);
    c.curvatureSignChange = std::fabs(kLeft) > tol.curvature && std::fabs(kRight) > tol.curvature && kLeft * kRight < 0.0;
    out.push_back(c);
  }
  return out;
}

bool Alignment::Bounds(Point2* mn, Point2* mx) const {
  if (entities_.empty()) return false;
  double x0 = std::numeric_limits<double>::infinity(), y0 = x0;
  double x1 = -x0, y1 = -x0;
  auto take = [&](Point2 p) {
    x0 = std::min(x0, p.x);
    y0 = std::min(y0, p.y);
    x1 = std::max(x1, p.x);
    y1 = std::max(y1, p.y);
  };
  for (size_t i = 0; i < entities_.size(); ++i) {
    const Entity& e = entities_[i];
    take(e.start);
    take(PointInEntity(static_cast<int>(i), e.length));
    if (e.kind == EntityKind::Arc) {
      CurveData cd;
      (void)CurveDataFor(static_cast<int>(i), &cd);
      const double sgn = e.right ? 1.0 : -1.0;
      const double a0 = std::atan2(e.start.x - cd.center.x, e.start.y - cd.center.y);
      const double sweep = e.length / e.radius;
      for (int q = 0; q < 4; ++q) {  // the four cardinal azimuths about the centre
        const double target = q * kPi * 0.5;
        double phi = std::fmod(sgn * (target - a0), kTwoPi);
        if (phi < 0.0) phi += kTwoPi;
        if (phi <= sweep)
          take({cd.center.x + e.radius * std::sin(target), cd.center.y + e.radius * std::cos(target)});
      }
      continue;
    }
    if (e.kind != EntityKind::Spiral) continue;

    // A clothoid's heading is quadratic in station. Its X/Y extrema occur where that heading
    // reaches a cardinal azimuth; test every root, splitting at an inflection if curvature changes sign.
    const double k0 = SpiralKappaStart(e);
    const double dk = (SpiralKappaEnd(e) - k0) / e.length;
    const auto heading = [&](double s) { return e.startAzimuth + k0 * s + 0.5 * dk * s * s; };
    std::array<double, 3> cuts{0.0, e.length, e.length};
    size_t cutCount = 2;
    if (dk != 0.0) {
      const double inflection = -k0 / dk;
      if (inflection > 0.0 && inflection < e.length) {
        cuts[1] = inflection;
        cuts[2] = e.length;
        cutCount = 3;
      }
    }
    bool conservativeBounds = false;
    size_t roots = 0;
    for (size_t segment = 0; segment + 1 < cutCount; ++segment) {
      const double a = cuts[segment], b = cuts[segment + 1];
      const double ha = heading(a), hb = heading(b);
      if (!std::isfinite(ha) || !std::isfinite(hb)) {
        conservativeBounds = true;
        break;
      }
      const double low = std::min(ha, hb), high = std::max(ha, hb);
      const double first = std::floor(low / (kPi * 0.5)) + 1.0;
      const double last = std::ceil(high / (kPi * 0.5)) - 1.0;
      if (!std::isfinite(first) || !std::isfinite(last) || std::fabs(first) > 9.0e15 || std::fabs(last) > 9.0e15) {
        conservativeBounds = true;
        break;
      }
      if (last < first) continue;
      if (last - first + 1.0 > 65536.0) {
        conservativeBounds = true;
        break;
      }
      for (double ordinal = first; ordinal <= last; ordinal += 1.0) {
        const double target = ordinal * (kPi * 0.5);
        double lo = a, hi = b;
        const bool increasing = hb > ha;
        for (int iteration = 0; iteration < 56; ++iteration) {
          const double mid = lo + (hi - lo) * 0.5;
          if ((heading(mid) < target) == increasing)
            lo = mid;
          else
            hi = mid;
        }
        take(PointInEntity(static_cast<int>(i), lo + (hi - lo) * 0.5));
        if (++roots > 65536) {
          conservativeBounds = true;
          break;
        }
      }
      if (conservativeBounds) break;
    }
    if (conservativeBounds) {
      // The path cannot travel farther than its arc length from its start. This safe fallback
      // avoids unbounded work on a numerically valid spiral with an extreme number of turns.
      take({e.start.x - e.length, e.start.y - e.length});
      take({e.start.x + e.length, e.start.y + e.length});
    }
  }
  if (mn != nullptr) *mn = {x0, y0};
  if (mx != nullptr) *mx = {x1, y1};
  return true;
}

void Alignment::AppendDisplaySamples(int entityIndex, double maxChordHeight, std::vector<Point2>* out) const {
  assert(out != nullptr);
  assert(entityIndex >= 0 && static_cast<size_t>(entityIndex) < entities_.size());
  constexpr int kMaxSteps = 1 << 16;  // a display bound: past this the chords are far below a pixel anyway
  const Entity& e = entities_[static_cast<size_t>(entityIndex)];
  const double h = std::max(maxChordHeight, 1e-9);
  if (e.kind == EntityKind::Line) {
    out->push_back(PointInEntity(entityIndex, e.length));
    return;
  }
  if (e.kind == EntityKind::Arc) {
    const double dTheta = 2.0 * std::acos(1.0 - std::min(h, e.radius * 0.5) / e.radius);
    const int steps = std::clamp(static_cast<int>(std::ceil((e.length / e.radius) / dTheta)), 1, kMaxSteps);
    for (int k = 1; k <= steps; ++k) out->push_back(PointInEntity(entityIndex, e.length * k / steps));
    return;
  }
  // REQ-495.1 adaptive subdivision: a chord of length ds where the curvature is at most k sags by about k ds^2 / 8, so
  // each step takes the largest ds that keeps that within `h` at the higher curvature of its two ends (a clothoid's
  // curvature is linear in length, so the larger end bounds the whole step).
  const double k0 = e.startRadius > 0.0 ? 1.0 / e.startRadius : 0.0, k1 = e.radius > 0.0 ? 1.0 / e.radius : 0.0;
  const auto curvature = [&](double s) { return k0 + (k1 - k0) * std::clamp(s / e.length, 0.0, 1.0); };
  const double floorStep = e.length / kMaxSteps;
  double s = 0.0;
  for (int k = 0; k < kMaxSteps && s < e.length; ++k) {
    const double trial = std::sqrt(8.0 * h / std::max(curvature(s), 1e-12));
    const double kHi = std::max(curvature(s), curvature(s + trial));
    s = std::min(e.length, s + std::max(floorStep, std::sqrt(8.0 * h / std::max(kHi, 1e-12))));
    out->push_back(PointInEntity(entityIndex, s));
  }
  if (s < e.length) out->push_back(PointInEntity(entityIndex, e.length));
}

std::vector<Point2> Alignment::SampleForDisplay(double maxChordHeight) const {
  std::vector<Point2> out;
  if (entities_.empty()) return out;
  out.push_back(entities_.front().start);
  for (size_t i = 0; i < entities_.size(); ++i) AppendDisplaySamples(static_cast<int>(i), maxChordHeight, &out);
  return out;
}

std::vector<Point2> Alignment::SampleEntityForDisplay(int entityIndex, double maxChordHeight) const {
  std::vector<Point2> out;
  if (entityIndex < 0 || static_cast<size_t>(entityIndex) >= entities_.size()) return out;
  out.push_back(entities_[static_cast<size_t>(entityIndex)].start);
  AppendDisplaySamples(entityIndex, maxChordHeight, &out);
  return out;
}

// ---------------------------------------------------------------- station formatting

std::string FormatStation(double station, const StationFormat& fmt) {
  const bool neg = station < 0.0;
  const double v = std::fabs(station);
  const int dec = std::clamp(fmt.decimals, 0, 9);
  std::string body;
  if (fmt.delimiter == '\0') {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", dec, v);
    body = buf;
  } else {
    const int digits = std::clamp(fmt.digitsRight, 1, 9);
    double scale = 1.0;
    for (int i = 0; i < dec; ++i) scale *= 10.0;
    double minorBase = 1.0;
    for (int i = 0; i < digits; ++i) minorBase *= 10.0;
    const long long n = std::llround(v * scale);
    const long long base = static_cast<long long>(minorBase * scale);
    const long long major = n / base;
    const long long minor = n % base;
    const long long whole = minor / static_cast<long long>(scale);
    const long long frac = minor % static_cast<long long>(scale);
    char buf[96];
    if (dec > 0)
      std::snprintf(buf, sizeof buf, "%lld%c%0*lld.%0*lld", major, fmt.delimiter, digits, whole, dec, frac);
    else
      std::snprintf(buf, sizeof buf, "%lld%c%0*lld", major, fmt.delimiter, digits, whole);
    body = buf;
  }
  return fmt.prefix + (neg ? "-" : "") + body + fmt.suffix;
}

bool ParseStation(const std::string& text, double* station, const StationFormat& fmt) {
  size_t b = 0, e = text.size();
  while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
  std::string s = text.substr(b, e - b);
  if (!fmt.prefix.empty() && s.rfind(fmt.prefix, 0) == 0) s.erase(0, fmt.prefix.size());
  if (s.size() >= 3 && (s[0] == 'S' || s[0] == 's') && (s[1] == 'T' || s[1] == 't') && (s[2] == 'A' || s[2] == 'a')) {
    s.erase(0, 3);
  }
  if (!fmt.suffix.empty() && s.size() >= fmt.suffix.size() &&
      s.compare(s.size() - fmt.suffix.size(), fmt.suffix.size(), fmt.suffix) == 0)
    s.erase(s.size() - fmt.suffix.size());
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  s.erase(0, i);
  if (s.empty()) return false;
  bool neg = false;
  if (s[0] == '-') {
    neg = true;
    s.erase(0, 1);
  }
  if (s.empty()) return false;
  const int digits = std::clamp(fmt.digitsRight, 1, 9);
  size_t dp = std::string::npos;
  for (size_t k = 0; k < s.size(); ++k) {
    const char c = s[k];
    if (c == '+' || (fmt.delimiter != '\0' && fmt.delimiter != '-' && c == fmt.delimiter) ||
        (fmt.delimiter == '-' && c == '-')) {
      dp = k;
      break;
    }
  }
  double value = 0.0;
  char* endp = nullptr;
  if (dp == std::string::npos) {
    value = std::strtod(s.c_str(), &endp);
    if (endp == s.c_str() || *endp != '\0') return false;
  } else {
    const std::string majorStr = s.substr(0, dp);
    const std::string minorStr = s.substr(dp + 1);
    if (majorStr.empty() || minorStr.empty()) return false;
    for (char c : majorStr) {
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    const double major = std::strtod(majorStr.c_str(), nullptr);
    const double minor = std::strtod(minorStr.c_str(), &endp);
    if (endp == minorStr.c_str() || *endp != '\0' || minor < 0.0) return false;
    double minorBase = 1.0;
    for (int k = 0; k < digits; ++k) minorBase *= 10.0;
    if (minor >= minorBase) return false;
    value = major * minorBase + minor;
  }
  if (!std::isfinite(value)) return false;
  if (station != nullptr) *station = neg ? -value : value;
  return true;
}

// ---------------------------------------------------------------- create from objects

SourceSegment SegmentFromBulge(Point2 a, Point2 b, double bulge, int sourceIndex) {
  SourceSegment s;
  s.sourceIndex = sourceIndex;
  s.a = a;
  s.b = b;
  if (std::fabs(bulge) < 1e-12) return s;
  const double theta = 4.0 * std::atan(std::fabs(bulge));  // included angle
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double c = std::hypot(dx, dy);
  const double r = c / (2.0 * std::sin(theta * 0.5));
  const double d = (c * 0.5) / std::tan(theta * 0.5);  // signed distance from chord midpoint to centre
  const double lx = -dy / c, ly = dx / c;               // left normal of a->b
  const double side = bulge > 0.0 ? 1.0 : -1.0;
  s.isArc = true;
  s.radius = r;
  s.center = {(a.x + b.x) * 0.5 + side * lx * d, (a.y + b.y) * 0.5 + side * ly * d};
  if (bulge > 0.0) {
    s.startAngle = std::atan2(a.y - s.center.y, a.x - s.center.x);
  } else {
    s.startAngle = std::atan2(b.y - s.center.y, b.x - s.center.x);  // CW a->b == CCW b->a
  }
  s.endAngle = s.startAngle + theta;
  return s;
}

namespace {

struct Oriented {
  Point2 from;
  Point2 to;
  int source = 0;
  bool isArc = false;
  const SourceSegment* seg = nullptr;
  bool forward = true;
};

Point2 ArcEnd(const SourceSegment& s, bool atEnd) {
  const double a = atEnd ? s.endAngle : s.startAngle;
  return {s.center.x + s.radius * std::cos(a), s.center.y + s.radius * std::sin(a)};
}

std::string Num(double v) {
  char b[48];
  std::snprintf(b, sizeof b, "%g", v);
  return b;
}

/// Length over which two source segments lie on top of each other (REQ-485.3 self-overlap); 0 when they only touch or
/// cross. Lines overlap when collinear; arcs when they share a circle. A line and an arc cannot overlap over a length.
double OverlapLength(const SourceSegment& a, const SourceSegment& b, double tol) {
  assert(tol > 0.0);
  assert(a.isArc || b.isArc || Dist(a.a, a.b) > 0.0);
  if (a.isArc != b.isArc) return 0.0;
  if (!a.isArc) {
    const double len = Dist(a.a, a.b);
    const double ux = (a.b.x - a.a.x) / len, uy = (a.b.y - a.a.y) / len;
    auto off = [&](Point2 p) { return std::fabs((p.x - a.a.x) * uy - (p.y - a.a.y) * ux); };
    if (off(b.a) > tol || off(b.b) > tol) return 0.0;
    auto along = [&](Point2 p) { return (p.x - a.a.x) * ux + (p.y - a.a.y) * uy; };
    const double lo = std::max(0.0, std::min(along(b.a), along(b.b)));
    const double hi = std::min(len, std::max(along(b.a), along(b.b)));
    return std::max(0.0, hi - lo);
  }
  if (Dist(a.center, b.center) > tol || std::fabs(a.radius - b.radius) > tol) return 0.0;
  // Angular intervals [start, start+sweep], compared on the circle by shifting b next to a.
  const double sweepA = std::fmod(a.endAngle - a.startAngle + kTwoPi * 4.0, kTwoPi);
  const double sweepB = std::fmod(b.endAngle - b.startAngle + kTwoPi * 4.0, kTwoPi);
  const double shift = std::fmod(b.startAngle - a.startAngle + kTwoPi * 4.0, kTwoPi);
  const double first = std::max(0.0, std::min(sweepA, shift + sweepB) - shift);
  const double wrapped = std::max(0.0, std::min(sweepA, shift + sweepB - kTwoPi));
  return a.radius * (first + wrapped);
}

/// First pair of segments that overlap by more than the tolerance; false when none.
bool FindSelfOverlap(const std::vector<SourceSegment>& segments, double tol, std::string* message) {
  assert(message != nullptr);
  assert(tol > 0.0);
  for (size_t i = 0; i < segments.size(); ++i) {
    for (size_t j = i + 1; j < segments.size(); ++j) {
      const double len = OverlapLength(segments[i], segments[j], tol);
      if (len <= tol) continue;
      *message = "objects " + std::to_string(segments[i].sourceIndex) + " and " +
                 std::to_string(segments[j].sourceIndex) + " overlap for " + Num(len) +
                 "; an alignment cannot run over itself";
      return true;
    }
  }
  return false;
}

}  // namespace

bool BuildFromSegments(const std::vector<SourceSegment>& segments, const FromObjectsOptions& opt, Alignment* out,
                       std::string* error, std::vector<std::string>* warnings) {
  auto fail = [&](const std::string& m) {
    if (error != nullptr) *error = m;
    if (out != nullptr) out->Clear();
    return false;
  };
  if (out == nullptr) return false;
  out->Clear();
  if (segments.empty()) return fail("nothing selected to convert");

  // Endpoints per segment, and rejection of degenerate input.
  struct Ends {
    Point2 p0, p1;
  };
  std::vector<Ends> ends(segments.size());
  for (size_t i = 0; i < segments.size(); ++i) {
    const SourceSegment& s = segments[i];
    if (s.isArc) {
      double sweep = std::fmod(s.endAngle - s.startAngle, kTwoPi);
      if (sweep < 0.0) sweep += kTwoPi;
      if (!(s.radius > 0.0) || sweep < 1e-12)
        return fail("object " + std::to_string(s.sourceIndex) + " is a zero-length or invalid arc");
      ends[i] = {ArcEnd(s, false), ArcEnd(s, true)};
    } else {
      if (Dist(s.a, s.b) <= opt.gapTolerance * 1e-3)
        return fail("object " + std::to_string(s.sourceIndex) + " is a zero-length segment");
      ends[i] = {s.a, s.b};
    }
  }
  std::string overlap;
  if (FindSelfOverlap(segments, std::max(opt.gapTolerance, 1e-9), &overlap)) return fail(overlap);

  const double tol = opt.gapTolerance;
  const size_t n = segments.size();
  // Free chain ends: an endpoint with no other segment's endpoint within tolerance.
  struct EndRef {
    size_t seg;
    bool atEnd;
  };
  std::vector<EndRef> freeEnds;
  for (size_t i = 0; i < n; ++i) {
    for (int w = 0; w < 2; ++w) {
      const Point2 p = w == 0 ? ends[i].p0 : ends[i].p1;
      bool shared = false;
      for (size_t j = 0; j < n && !shared; ++j) {
        if (j == i) continue;
        shared = Dist(p, ends[j].p0) <= tol || Dist(p, ends[j].p1) <= tol;
      }
      if (!shared) freeEnds.push_back({i, w == 1});
    }
  }
  // Choose the start: nearest free end to the pick, else the first free end, else (closed loop) the first segment.
  EndRef startRef{0, false};
  if (!freeEnds.empty()) {
    startRef = freeEnds.front();
    if (opt.hasPick) {
      double bestD = std::numeric_limits<double>::infinity();
      for (const EndRef& f : freeEnds) {
        const Point2 p = f.atEnd ? ends[f.seg].p1 : ends[f.seg].p0;
        const double d = Dist(p, opt.pick);
        if (d < bestD) {
          bestD = d;
          startRef = f;
        }
      }
    }
  } else if (opt.hasPick) {
    double bestD = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < n; ++i)
      for (int w = 0; w < 2; ++w) {
        const Point2 p = w == 0 ? ends[i].p0 : ends[i].p1;
        const double d = Dist(p, opt.pick);
        if (d < bestD) {
          bestD = d;
          startRef = {i, w == 1};
        }
      }
  }

  std::vector<Oriented> chain;
  std::vector<char> used(n, 0);
  auto orient = [&](size_t i, bool reversed) {
    Oriented o;
    o.source = segments[i].sourceIndex;
    o.isArc = segments[i].isArc;
    o.seg = &segments[i];
    o.forward = !reversed;
    o.from = reversed ? ends[i].p1 : ends[i].p0;
    o.to = reversed ? ends[i].p0 : ends[i].p1;
    return o;
  };
  chain.push_back(orient(startRef.seg, startRef.atEnd));  // starting at the END means the segment is reversed
  used[startRef.seg] = 1;
  for (size_t placed = 1; placed < n; ++placed) {
    const Point2 tail = chain.back().to;
    double bestD = std::numeric_limits<double>::infinity();
    size_t bi = n;
    bool brev = false;
    for (size_t j = 0; j < n; ++j) {
      if (used[j]) continue;
      const double d0 = Dist(tail, ends[j].p0);
      const double d1 = Dist(tail, ends[j].p1);
      if (d0 < bestD) {
        bestD = d0;
        bi = j;
        brev = false;
      }
      if (d1 < bestD) {
        bestD = d1;
        bi = j;
        brev = true;
      }
    }
    if (bi == n || bestD > tol) {
      // Name the smallest gap between what was chained and what was not, and the objects on either side of it.
      double gap = std::numeric_limits<double>::infinity();
      size_t from = 0, to = 0;
      for (size_t u = 0; u < n; ++u) {
        for (size_t v = 0; v < n && used[u]; ++v) {
          if (used[v]) continue;
          for (const Point2 p : {ends[u].p0, ends[u].p1}) {
            for (const Point2 q : {ends[v].p0, ends[v].p1}) {
              if (Dist(p, q) < gap) {
                gap = Dist(p, q);
                from = u;
                to = v;
              }
            }
          }
        }
      }
      if (gap <= tol)
        return fail("objects " + std::to_string(segments[from].sourceIndex) + " and " +
                    std::to_string(segments[to].sourceIndex) + " branch from the same point; the selection must be "
                    "a single chain");
      return fail("objects " + std::to_string(segments[from].sourceIndex) + " and " +
                  std::to_string(segments[to].sourceIndex) + " are not connected: gap of " + Num(gap) +
                  " exceeds the tolerance " + Num(tol));
    }
    chain.push_back(orient(bi, brev));
    used[bi] = 1;
  }

  // Convert the ordered, oriented chain to exact entities. Gaps within the tolerance are joined (REQ-485.3): a line
  // starts where the previous entity ended and ends where a following arc starts; an arc keeps its own geometry.
  for (size_t k = 0; k < chain.size(); ++k) {
    const Oriented& o = chain[k];
    Entity e;
    e.start = o.from;
    if (!o.isArc) {
      const Point2 from = out->Empty() ? o.from : out->EndPoint();
      const Point2 to = (k + 1 < chain.size() && chain[k + 1].isArc) ? chain[k + 1].from : o.to;
      e.start = from;
      const double dx = to.x - from.x, dy = to.y - from.y;
      e.kind = EntityKind::Line;
      e.startAzimuth = std::atan2(dx, dy);
      e.length = std::hypot(dx, dy);
    } else {
      const SourceSegment& s = *o.seg;
      double sweep = std::fmod(s.endAngle - s.startAngle, kTwoPi);
      if (sweep < 0.0) sweep += kTwoPi;
      e.kind = EntityKind::Arc;
      e.radius = s.radius;
      e.length = s.radius * sweep;
      if (o.forward) {  // CCW travel = left turn
        e.right = false;
        e.startAzimuth = std::atan2(-std::sin(s.startAngle), std::cos(s.startAngle));
      } else {  // traversed from the CCW end back to its start = clockwise = right turn
        e.right = true;
        e.startAzimuth = std::atan2(std::sin(s.endAngle), -std::cos(s.endAngle));
      }
    }
    if (!out->AppendRaw(e)) return fail("object " + std::to_string(o.source) + " could not be converted");
  }

  if (opt.addFillets) {
    // Fillet each line-line corner with the requested radius. Like Civil 3D (D-2026-11-04-i), a curve whose tangent
    // distance would exceed 40 % of the shorter of its two tangents gets the radius that uses exactly 40 %, so two
    // corners never compete for one tangent; the smaller radius is reported.
    constexpr double kMaxTangentShare = 0.4;
    const std::vector<Entity> src = out->Entities();
    std::vector<double> trimStart(src.size(), 0.0), trimEnd(src.size(), 0.0);
    std::vector<char> filleted(src.size(), 0);
    std::vector<double> deflection(src.size(), 0.0), radius(src.size(), opt.filletRadius);
    for (size_t i = 0; i + 1 < src.size(); ++i) {
      if (src[i].kind != EntityKind::Line || src[i + 1].kind != EntityKind::Line) continue;
      double d = std::fmod(src[i + 1].startAzimuth - src[i].startAzimuth, kTwoPi);
      if (d > kPi) d -= kTwoPi;
      if (d < -kPi) d += kTwoPi;
      if (std::fabs(d) < 1e-9) continue;
      const std::string corner = "corner after object " + std::to_string(chain[i].source);
      if (std::fabs(d) >= kPi - 1e-9) {  // the line doubles back: no curve can join it
        if (warnings != nullptr) warnings->push_back(corner + " turns back on itself; left as a PI");
        continue;
      }
      const double halfTan = std::tan(std::fabs(d) * 0.5);
      const double tMax = kMaxTangentShare * std::min(src[i].length, src[i + 1].length);
      double t = opt.filletRadius * halfTan;
      if (t > tMax) {
        t = tMax;
        radius[i] = tMax / halfTan;
        if (warnings != nullptr)
          warnings->push_back(corner + " is too tight for radius " + Num(opt.filletRadius) + "; used radius " +
                              Num(radius[i]));
      }
      trimEnd[i] = t;
      trimStart[i + 1] = t;
      filleted[i] = 1;
      deflection[i] = d;
    }
    Alignment rebuilt;
    for (size_t i = 0; i < src.size(); ++i) {
      Entity e = src[i];
      if (e.kind == EntityKind::Line) {
        e.start = {e.start.x + trimStart[i] * std::sin(e.startAzimuth), e.start.y + trimStart[i] * std::cos(e.startAzimuth)};
        e.length = src[i].length - trimStart[i] - trimEnd[i];
      }
      if (!rebuilt.AppendRaw(e)) return fail("fillet left a zero-length segment");
      if (filleted[i]) {
        const bool right = deflection[i] > 0.0;
        if (!rebuilt.AppendCurveByDelta(radius[i], std::fabs(deflection[i]), right))
          return fail("fillet could not be created");
      }
    }
    *out = std::move(rebuilt);
  }

  if (opt.reverse && !out->Reverse(nullptr)) return fail("the chain could not be reversed (it has a gap)");
  return true;
}

bool SpiralDataFor(int entityIndex, const Alignment& a, SpiralData* out) {
  if (out == nullptr) return false;
  if (entityIndex < 0 || static_cast<size_t>(entityIndex) >= a.Entities().size()) return false;
  const Entity& e = a.Entities()[static_cast<size_t>(entityIndex)];
  if (e.kind != EntityKind::Spiral) return false;
  out->length = e.length;
  out->startRadius = e.startRadius;
  out->endRadius = e.radius;
  out->right = e.right;
  out->deflection = SpiralHeadingDelta(e, e.length);
  const double curvatureChange = std::fabs(SpiralKappaEnd(e) - SpiralKappaStart(e));
  if (curvatureChange > 0.0) out->parameterA = std::sqrt(e.length / curvatureChange);
  const double rFinite = IsInfiniteSpiralRadius(e.radius) ? e.startRadius : e.radius;
  if (IsInfiniteSpiralRadius(rFinite)) return true;
  const bool fromStart = IsInfiniteSpiralRadius(e.startRadius);
  if (!fromStart && !IsInfiniteSpiralRadius(e.radius)) return true;  // between two curves: no tangent end
  // Measure from the tangent end towards the curve end, along the tangent's direction of travel into the spiral.
  const double s0 = a.EntityStartStation(entityIndex), s1 = a.EntityEndStation(entityIndex);
  const Point2 origin = a.GetPointAtStation(fromStart ? s0 : s1);
  const Point2 curveEnd = a.GetPointAtStation(fromStart ? s1 : s0);
  const double az = fromStart ? e.startAzimuth : NormAz(a.GetBearingAtStation(s1) + kPi);
  const double ux = std::sin(az), uy = std::cos(az);
  const double dx = curveEnd.x - origin.x, dy = curveEnd.y - origin.y;
  const double theta = std::fabs(out->deflection);
  if (!(theta > 0.0 && theta < kPi) || !std::isfinite(theta)) return true;
  out->tangentEnd = true;
  out->totalX = dx * ux + dy * uy;
  out->totalY = std::fabs(dx * uy - dy * ux);
  out->p = out->totalY - rFinite * (1.0 - std::cos(theta));
  out->k = out->totalX - rFinite * std::sin(theta);
  out->longTangent = out->totalX - out->totalY / std::tan(theta);
  out->shortTangent = out->totalY / std::sin(theta);
  out->spi = {origin.x + out->longTangent * ux, origin.y + out->longTangent * uy};
  out->spiAngle = theta;
  return true;
}

const char* ContinuityRequirementLabel(ContinuityRequirement level) {
  switch (level) {
    case ContinuityRequirement::C0:
      return "C0";
    case ContinuityRequirement::C1:
      return "C1";
    case ContinuityRequirement::C2:
      return "C2";
  }
  return "C1";
}

bool ParseContinuityRequirement(const std::string_view text, ContinuityRequirement* out) {
  if (out == nullptr) return false;
  if (text.size() == 2 && (text[0] == 'c' || text[0] == 'C')) {
    if (text[1] == '0') {
      *out = ContinuityRequirement::C0;
      return true;
    }
    if (text[1] == '1') {
      *out = ContinuityRequirement::C1;
      return true;
    }
    if (text[1] == '2') {
      *out = ContinuityRequirement::C2;
      return true;
    }
  }
  return false;
}

}  // namespace alignment

namespace alignment {

// REQ-500: no profile feature exists yet, so every vertical query answers "no profile" and returns no value. The
// profile-id list is the seam a profile feature will fill in later.
Alignment::VerticalResult Alignment::GetElevationAtStation(double) const {
  VerticalResult r;
  r.reason = profileIds.empty() ? "no profile" : "profile reading is not built yet";
  return r;
}

Alignment::VerticalResult Alignment::GetProfilePoint(double) const {
  VerticalResult r;
  r.reason = profileIds.empty() ? "no profile" : "profile reading is not built yet";
  return r;
}

Alignment::VerticalResult Alignment::GetVerticalGrade(double) const {
  VerticalResult r;
  r.reason = profileIds.empty() ? "no profile" : "profile reading is not built yet";
  return r;
}

}  // namespace alignment
