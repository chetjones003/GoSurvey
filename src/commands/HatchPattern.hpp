#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "CadEntities.hpp"
#include "HatchGeom.hpp"
#include "HatchPat.hpp"

// Hatch line-pattern generation for the HATCH command (REQ-043, ADR-018). Given a filled region and a
// parsed .pat pattern definition (one or more line families with origin/spacing/stagger/dashes), build the
// clipped line segments that draw the pattern: each family is clipped to the region by the even-odd rule
// (holes/islands carve gaps) and split into dashes. Pure geometry, dependency-free, unit-tested without GUI.
//
// Distances from the .pat file are in pattern units and multiplied by the region's patternScale; the whole
// pattern is additionally rotated by patternAngleDeg. A segment cap bounds runaway on huge regions.

namespace hatchpattern {

constexpr int kMaxSegments = 200000;  ///< safety cap (sparse-enough scale keeps real patterns far below)

namespace detail {

// Even-odd clip of the infinite line {P = p*n + s*d} against all loop edges → sorted inside intervals in s.
inline void ClipLine(const CadFilledRegion& fr, double px, double nx, double ny, double dx, double dy,
                     std::vector<double>* crossings) {
  crossings->clear();
  for (size_t loop = 0; loop < fr.loopStart.size(); ++loop) {
    const int begin = fr.loopStart[loop];
    const int cnt = fr.loopCount(loop);
    if (cnt < 3) continue;
    for (int i = 0; i < cnt; ++i) {
      const int a = begin + i, b = begin + (i + 1) % cnt;
      const double ax = fr.verts[static_cast<size_t>(a) * 2], ay = fr.verts[static_cast<size_t>(a) * 2 + 1];
      const double bx = fr.verts[static_cast<size_t>(b) * 2], by = fr.verts[static_cast<size_t>(b) * 2 + 1];
      const double ex = bx - ax, ey = by - ay;
      const double det = ex * dy - dx * ey;
      if (std::fabs(det) < 1e-12) continue;
      const double rhsx = ax - px * nx, rhsy = ay - px * ny;
      const double s = (-rhsx * ey + ex * rhsy) / det;  // param along the family line
      const double u = (dx * rhsy - dy * rhsx) / det;   // param along the edge
      if (u >= 0.0 && u < 1.0)                           // half-open avoids double-counting shared vertices
        crossings->push_back(s);
    }
  }
  std::sort(crossings->begin(), crossings->end());
}

// Emit dashed segments of {P = p*n + s*d} over [s0,s1] given the (scaled) dash list and a phase origin.
inline void EmitDashed(double px, double nx, double ny, double dx, double dy, double s0, double s1,
                       const std::vector<double>& dashes, double period, double phase0,
                       std::vector<float>* out, int* count) {
  auto push = [&](double a, double b) {
    if (b - a < 1e-9 || *count >= kMaxSegments) return;
    out->push_back(static_cast<float>(px * nx + a * dx));
    out->push_back(static_cast<float>(px * ny + a * dy));
    out->push_back(static_cast<float>(px * nx + b * dx));
    out->push_back(static_cast<float>(px * ny + b * dy));
    ++*count;
  };
  if (dashes.empty() || period < 1e-9) {  // solid line
    push(s0, s1);
    return;
  }
  // Walk the dash cycle from the element containing s0.
  double rel = std::fmod(s0 - phase0, period);
  if (rel < 0) rel += period;
  size_t i = 0;
  double acc = 0.0;
  const size_t n = dashes.size();
  for (size_t guard = 0; guard < n + 1; ++guard) {
    const double len = std::fabs(dashes[i]);
    if (acc + len > rel) break;
    acc += len;
    i = (i + 1) % n;
  }
  double cur = s0;
  double remInElem = (acc + std::fabs(dashes[i])) - rel;
  int safety = 0;
  while (cur < s1 - 1e-9 && *count < kMaxSegments && safety++ < 1000000) {
    const double len = std::fabs(dashes[i]);
    const double take = std::min(remInElem, s1 - cur);
    if (dashes[i] > 0.0)
      push(cur, cur + take);
    else if (dashes[i] == 0.0)
      push(cur, cur + std::min(1e-3, s1 - cur));  // dot
    cur += (len < 1e-9) ? 1e-3 : take;
    i = (i + 1) % n;
    remInElem = std::fabs(dashes[i]);
  }
}

}  // namespace detail

/// Appends the clipped pattern segments (flat x0,y0,x1,y1 in the region's local coordinates) to \p outSegs,
/// generated from \p def with the region's patternScale/patternAngleDeg. Returns the number appended.
inline int BuildSegments(const CadFilledRegion& fr, const hatchpat::Def& def, std::vector<float>* outSegs) {
  if (!outSegs || fr.isSolid() || fr.loopStart.empty() || fr.verts.size() < 6 || def.lines.empty())
    return 0;
  float mnX = 0, mnY = 0, mxX = 0, mxY = 0;
  if (!hatchgeom::OuterBounds(fr, &mnX, &mnY, &mxX, &mxY))
    return 0;
  const double scale = std::max(0.01f, fr.patternScale);
  constexpr double kDeg = 3.14159265358979323846 / 180.0;
  const double rot = fr.patternAngleDeg * kDeg;
  const double cr = std::cos(rot), sr = std::sin(rot);

  int count = 0;
  std::vector<double> crossings;
  for (const hatchpat::Line& L : def.lines) {
    const double a = L.angleDeg * kDeg + rot;
    const double dx = std::cos(a), dy = std::sin(a);
    const double nx = -dy, ny = dx;
    // Origin scaled by `scale`, then rotated by the user angle about the drawing origin.
    const double sx = L.x0 * scale, sy = L.y0 * scale;
    const double ox = sx * cr - sy * sr, oy = sx * sr + sy * cr;
    const double perp = L.dy * scale;
    if (std::fabs(perp) < 1e-9) continue;  // degenerate spacing
    const double along = L.dx * scale;     // per-member stagger along the line
    std::vector<double> dashes;
    double period = 0.0;
    for (double dval : L.dashes) { dashes.push_back(dval * scale); period += std::fabs(dval) * scale; }

    // Perpendicular extent of the outer loop relative to the origin line.
    double pMin = 1e300, pMax = -1e300;
    const int oc = fr.loopCount(0);
    for (int i = 0; i < oc; ++i) {
      const double vx = fr.verts[static_cast<size_t>(i) * 2], vy = fr.verts[static_cast<size_t>(i) * 2 + 1];
      const double pp = vx * nx + vy * ny;
      pMin = std::min(pMin, pp);
      pMax = std::max(pMax, pp);
    }
    const double baseP = ox * nx + oy * ny;
    long kStart = static_cast<long>(std::floor((pMin - baseP) / std::fabs(perp))) - 1;
    long kEnd = static_cast<long>(std::ceil((pMax - baseP) / std::fabs(perp))) + 1;
    if (kEnd - kStart > 200000) continue;  // pathological density (scale too small) — skip this family
    const double uOrigin = ox * dx + oy * dy;
    for (long k = kStart; k <= kEnd && count < kMaxSegments; ++k) {
      const double p = baseP + static_cast<double>(k) * perp;
      detail::ClipLine(fr, p, nx, ny, dx, dy, &crossings);
      if (crossings.size() < 2) continue;
      const double phase0 = uOrigin + static_cast<double>(k) * along;
      for (size_t c = 0; c + 1 < crossings.size(); c += 2)
        detail::EmitDashed(p, nx, ny, dx, dy, crossings[c], crossings[c + 1], dashes, period, phase0, outSegs,
                           &count);
    }
  }
  return count;
}

}  // namespace hatchpattern
