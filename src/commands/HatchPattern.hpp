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

// One boundary edge, measured in a family's frame: p is the perpendicular offset (the family's line index
// direction), s the position along the family lines.
struct Edge {
  double pLo = 0.0, pHi = 0.0;  ///< the edge's p range, pLo <= pHi
  double pa = 0.0, sa = 0.0;    ///< first end
  double pb = 0.0, sb = 0.0;    ///< second end
};

// Every loop edge of \p fr in the frame (n, d), sorted by pLo so a sweep over increasing p only ever
// looks at the edges that can cross the current line (the active list), not all of them.
inline void BuildEdgeTable(const CadFilledRegion& fr, double nx, double ny, double dx, double dy,
                           std::vector<Edge>* edges) {
  edges->clear();
  for (size_t loop = 0; loop < fr.loopStart.size(); ++loop) {
    const int begin = fr.loopStart[loop];
    const int cnt = fr.loopCount(loop);
    if (cnt < 3) continue;
    for (int i = 0; i < cnt; ++i) {
      const int a = begin + i, b = begin + (i + 1) % cnt;
      const double ax = fr.vertsXyz[static_cast<size_t>(a) * 3], ay = fr.vertsXyz[static_cast<size_t>(a) * 3 + 1];
      const double bx = fr.vertsXyz[static_cast<size_t>(b) * 3], by = fr.vertsXyz[static_cast<size_t>(b) * 3 + 1];
      Edge e;
      e.pa = ax * nx + ay * ny; e.sa = ax * dx + ay * dy;
      e.pb = bx * nx + by * ny; e.sb = bx * dx + by * dy;
      if (std::fabs(e.pb - e.pa) < 1e-12) continue;  // parallel to the family: never crossed
      e.pLo = std::min(e.pa, e.pb); e.pHi = std::max(e.pa, e.pb);
      edges->push_back(e);
    }
  }
  std::sort(edges->begin(), edges->end(), [](const Edge& l, const Edge& r) { return l.pLo < r.pLo; });
}

// The sorted crossings of the family line at offset \p p with the active edges. Each edge counts on the
// half-open range [first end, second end), which is what keeps a line through a shared vertex from counting
// it twice (the even-odd rule needs exactly one crossing there).
inline void Crossings(const std::vector<const Edge*>& active, double p, std::vector<double>* crossings) {
  crossings->clear();
  for (const Edge* e : active) {
    const double t = (p - e->pa) / (e->pb - e->pa);
    if (t >= 0.0 && t < 1.0)
      crossings->push_back(e->sa + t * (e->sb - e->sa));
  }
  std::sort(crossings->begin(), crossings->end());
}

// Emit dashed segments of {P = p*n + s*d} over [s0,s1] given the (scaled) dash list and a phase origin.
inline void EmitDashed(double px, double nx, double ny, double dx, double dy, double s0, double s1,
                       const std::vector<double>& dashes, double period, double phase0,
                       std::vector<double>* out, int* count) {
  auto push = [&](double a, double b) {
    if (b - a < 1e-9 || *count >= kMaxSegments) return;
    out->push_back(px * nx + a * dx);
    out->push_back(px * ny + a * dy);
    out->push_back(px * nx + b * dx);
    out->push_back(px * ny + b * dy);
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

/// Appends the clipped pattern segments (flat x0,y0,x1,y1 in the region's local coordinates, kept in double as
/// every coordinate store is until the GPU narrowing point, ADR-054) to \p outSegs,
/// generated from \p def with the region's patternScale/patternAngleDeg. Returns the number appended.
inline int BuildSegments(const CadFilledRegion& fr, const hatchpat::Def& def, std::vector<double>* outSegs) {
  if (!outSegs || fr.isSolid() || fr.loopStart.empty() || fr.vertsXyz.size() < 9 || def.lines.empty())
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
  std::vector<detail::Edge> edges;
  std::vector<const detail::Edge*> active;
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

    detail::BuildEdgeTable(fr, nx, ny, dx, dy, &edges);
    if (edges.empty()) continue;
    double pMin = edges.front().pLo, pMax = pMin;
    for (const detail::Edge& e : edges) pMax = std::max(pMax, e.pHi);
    const double baseP = ox * nx + oy * ny;
    const double step = std::fabs(perp);
    const long kStart = static_cast<long>(std::floor((pMin - baseP) / step)) - 1;
    const long kEnd = static_cast<long>(std::ceil((pMax - baseP) / step)) + 1;
    if (kEnd - kStart > 200000) continue;  // pathological density (scale too small) — skip this family
    const double uOrigin = ox * dx + oy * dy;
    // Sweep the family lines in increasing p, admitting edges as the sweep reaches them and retiring the ones
    // it has passed, so each line tests only the edges that span it.
    std::size_t next = 0;
    active.clear();
    for (long k = kStart; k <= kEnd && count < kMaxSegments; ++k) {
      const double p = baseP + static_cast<double>(k) * step;
      while (next < edges.size() && edges[next].pLo <= p) active.push_back(&edges[next++]);
      active.erase(std::remove_if(active.begin(), active.end(), [p](const detail::Edge* e) { return e->pHi < p; }),
                   active.end());
      if (active.size() < 2) continue;
      detail::Crossings(active, p, &crossings);
      if (crossings.size() < 2) continue;
      // The stagger follows the line's index from the pattern origin, whichever way the sweep runs.
      const long member = perp >= 0.0 ? k : -k;
      const double phase0 = uOrigin + static_cast<double>(member) * along;
      for (size_t c = 0; c + 1 < crossings.size(); c += 2)
        detail::EmitDashed(p, nx, ny, dx, dy, crossings[c], crossings[c + 1], dashes, period, phase0, outSegs,
                           &count);
    }
  }
  return count;
}

}  // namespace hatchpattern
