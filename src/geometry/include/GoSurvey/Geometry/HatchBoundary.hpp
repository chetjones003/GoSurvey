#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <vector>

// Planar boundary tracing for hatch-region construction (REQ-043, ADR-017). Given a set of boundary segments
// (lines, polyline edges, and tessellated arcs/circles supplied by the caller) and an internal seed
// point, find the SMALLEST closed loop that encloses the seed by walking the planar subdivision face
// that contains it. Returns false when no closed region contains the point (REQ-201 — the command then
// places nothing). Pure geometry, dependency-free, so it is unit-tested without the GUI.
//
// Drawn linework rarely meets only at shared endpoints: lines cross, end on the middle of another line
// (a T-junction), overlap, or overshoot a corner. So the segments are first split wherever one crosses or
// touches another, and dangling pieces (overshoots, ticks, open chains) are pruned before the walk.
// Only segments near the seed are considered, in a window that grows until the loop found fits inside it,
// so a click in a large drawing does not intersect every segment with every other.

namespace hatchboundary {

struct Seg {
  double x0, y0, x1, y1;
};

namespace detail {

struct HalfEdge {
  int to;        ///< destination node index
  double angle;  ///< atan2 of (to - from)
};

inline double NormalizeTwoPi(double a) {
  constexpr double kTwoPi = 6.283185307179586;
  while (a <= 0.0) a += kTwoPi;
  while (a > kTwoPi) a -= kTwoPi;
  return a;
}

// Even-odd point-in-polygon over an ordered loop (flat x,y pairs).
inline bool LoopContains(const std::vector<float>& loop, double px, double py) {
  const size_t n = loop.size() / 2;
  if (n < 3) return false;
  bool in = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double xi = loop[i * 2], yi = loop[i * 2 + 1];
    const double xj = loop[j * 2], yj = loop[j * 2 + 1];
    if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
      in = !in;
  }
  return in;
}

inline double LoopAreaAbs(const std::vector<float>& loop) {
  const size_t n = loop.size() / 2;
  if (n < 3) return 0.0;
  double a = 0.0;
  for (size_t i = 0, j = n - 1; i < n; j = i++)
    a += static_cast<double>(loop[j * 2]) * loop[i * 2 + 1] -
         static_cast<double>(loop[i * 2]) * loop[j * 2 + 1];
  return std::fabs(a) * 0.5;
}

// Splits every segment where another crosses it, or where another's endpoint lies on it (T-junctions and
// collinear overlaps), so each such meeting becomes a shared node. A sweep over x keeps it near-linear
// for drawn linework, where most segment pairs are far apart.
inline std::vector<Seg> SplitAtIntersections(const std::vector<Seg>& in, double tol) {
  const size_t n = in.size();
  std::vector<std::vector<double>> cuts(n);
  std::vector<size_t> order(n);
  std::iota(order.begin(), order.end(), size_t{0});
  const auto minX = [&](size_t i) { return std::min(in[i].x0, in[i].x1) - tol; };
  const auto maxX = [&](size_t i) { return std::max(in[i].x0, in[i].x1) + tol; };
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return minX(a) < minX(b); });
  // Parameter of (px,py) on the interior of s when it lies within tol of it.
  const auto onInterior = [&](const Seg& s, double px, double py, double* t) {
    const double dx = s.x1 - s.x0, dy = s.y1 - s.y0;
    const double len2 = dx * dx + dy * dy;
    if (len2 < tol * tol) return false;
    const double u = ((px - s.x0) * dx + (py - s.y0) * dy) / len2;
    const double len = std::sqrt(len2);
    if (u * len <= tol || (1.0 - u) * len <= tol) return false;  // at an end: already a shared node
    const double ex = s.x0 + u * dx - px, ey = s.y0 + u * dy - py;
    if (ex * ex + ey * ey > tol * tol) return false;
    *t = u;
    return true;
  };
  for (size_t a = 0; a < n; ++a) {
    const size_t i = order[a];
    const Seg& p = in[i];
    const double pMinY = std::min(p.y0, p.y1) - tol, pMaxY = std::max(p.y0, p.y1) + tol;
    for (size_t b = a + 1; b < n; ++b) {
      const size_t j = order[b];
      if (minX(j) > maxX(i)) break;
      const Seg& q = in[j];
      if (std::max(q.y0, q.y1) + tol < pMinY || std::min(q.y0, q.y1) - tol > pMaxY) continue;
      const double rx = p.x1 - p.x0, ry = p.y1 - p.y0, sx = q.x1 - q.x0, sy = q.y1 - q.y0;
      const double den = rx * sy - ry * sx;
      if (std::fabs(den) > 1e-12 * std::hypot(rx, ry) * std::hypot(sx, sy)) {
        const double qx = q.x0 - p.x0, qy = q.y0 - p.y0;
        const double t = (qx * sy - qy * sx) / den, u = (qx * ry - qy * rx) / den;
        if (t > 0.0 && t < 1.0 && u > 0.0 && u < 1.0) {
          cuts[i].push_back(t);
          cuts[j].push_back(u);
        }
      }
      double t = 0.0;
      if (onInterior(p, q.x0, q.y0, &t)) cuts[i].push_back(t);
      if (onInterior(p, q.x1, q.y1, &t)) cuts[i].push_back(t);
      if (onInterior(q, p.x0, p.y0, &t)) cuts[j].push_back(t);
      if (onInterior(q, p.x1, p.y1, &t)) cuts[j].push_back(t);
    }
  }
  std::vector<Seg> out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const Seg& s = in[i];
    if (cuts[i].empty()) { out.push_back(s); continue; }
    std::sort(cuts[i].begin(), cuts[i].end());
    double px = s.x0, py = s.y0;
    for (double t : cuts[i]) {
      const double x = s.x0 + t * (s.x1 - s.x0), y = s.y0 + t * (s.y1 - s.y0);
      out.push_back({px, py, x, y});
      px = x;
      py = y;
    }
    out.push_back({px, py, s.x1, s.y1});
  }
  return out;
}

// One trace over an already-chosen segment set: the face containing (px,py), or empty.
inline std::vector<float> TraceInSegments(const std::vector<Seg>& rawSegs, double px, double py, double tol) {
  const std::vector<Seg> segs = SplitAtIntersections(rawSegs, tol);
  const double inv = 1.0 / tol;

  // Nodes snap onto a tol grid; a lookup also checks the neighbouring cells, so two endpoints within tol
  // of each other merge even when rounding puts them either side of a cell boundary.
  std::vector<double> nodeX, nodeY;
  std::unordered_map<uint64_t, int> nodeOf;
  const auto cellKey = [](int64_t ix, int64_t iy) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(ix)) << 32) ^ static_cast<uint32_t>(iy);
  };
  const auto node = [&](double x, double y) -> int {
    const int64_t ix = static_cast<int64_t>(std::llround(x * inv));
    const int64_t iy = static_cast<int64_t>(std::llround(y * inv));
    for (int64_t dx = -1; dx <= 1; ++dx)
      for (int64_t dy = -1; dy <= 1; ++dy) {
        const auto it = nodeOf.find(cellKey(ix + dx, iy + dy));
        if (it != nodeOf.end() && std::fabs(nodeX[static_cast<size_t>(it->second)] - x) <= tol &&
            std::fabs(nodeY[static_cast<size_t>(it->second)] - y) <= tol)
          return it->second;
      }
    const int id = static_cast<int>(nodeX.size());
    nodeOf.emplace(cellKey(ix, iy), id);
    nodeX.push_back(x);
    nodeY.push_back(y);
    return id;
  };

  struct Edge { int a, b; bool alive; };
  std::vector<Edge> edges;
  std::vector<std::vector<int>> incident;
  for (const Seg& s : segs) {
    if (std::fabs(s.x1 - s.x0) < tol && std::fabs(s.y1 - s.y0) < tol) continue;
    const int a = node(s.x0, s.y0);
    const int b = node(s.x1, s.y1);
    if (a == b) continue;
    if (static_cast<int>(incident.size()) <= std::max(a, b)) incident.resize(static_cast<size_t>(std::max(a, b)) + 1);
    incident[static_cast<size_t>(a)].push_back(static_cast<int>(edges.size()));
    incident[static_cast<size_t>(b)].push_back(static_cast<int>(edges.size()));
    edges.push_back({a, b, true});
  }
  incident.resize(nodeX.size());

  // Prune dangling edges: an overshoot past a corner or a tick mark ends at a node no other edge reaches,
  // and the walk below would otherwise turn back along it.
  std::vector<int> degree(nodeX.size(), 0);
  for (size_t v = 0; v < nodeX.size(); ++v) degree[v] = static_cast<int>(incident[v].size());
  std::vector<int> stack;
  for (size_t v = 0; v < nodeX.size(); ++v)
    if (degree[v] == 1) stack.push_back(static_cast<int>(v));
  while (!stack.empty()) {
    const int v = stack.back();
    stack.pop_back();
    if (degree[static_cast<size_t>(v)] != 1) continue;
    for (int e : incident[static_cast<size_t>(v)]) {
      Edge& edge = edges[static_cast<size_t>(e)];
      if (!edge.alive) continue;
      edge.alive = false;
      const int other = edge.a == v ? edge.b : edge.a;
      --degree[static_cast<size_t>(v)];
      if (--degree[static_cast<size_t>(other)] == 1) stack.push_back(other);
      break;
    }
  }

  std::vector<std::vector<HalfEdge>> out(nodeX.size());
  for (const Edge& e : edges) {
    if (!e.alive) continue;
    const size_t a = static_cast<size_t>(e.a), b = static_cast<size_t>(e.b);
    out[a].push_back({e.b, std::atan2(nodeY[b] - nodeY[a], nodeX[b] - nodeX[a])});
    out[b].push_back({e.a, std::atan2(nodeY[a] - nodeY[b], nodeX[a] - nodeX[b])});
  }

  // Walk one face by always taking the tightest clockwise turn, until the starting half-edge comes round
  // again. A face may touch a node twice (two loops joined at a corner), so only the half-edge ends it.
  const size_t guard = edges.size() * 2 + 16;
  const auto trace = [&](int from, int to) -> std::vector<float> {
    std::vector<float> loop;
    int u = from, v = to;
    for (size_t step = 0; step < guard; ++step) {
      loop.push_back(static_cast<float>(nodeX[static_cast<size_t>(u)]));
      loop.push_back(static_cast<float>(nodeY[static_cast<size_t>(u)]));
      const double base = std::atan2(nodeY[static_cast<size_t>(u)] - nodeY[static_cast<size_t>(v)],
                                     nodeX[static_cast<size_t>(u)] - nodeX[static_cast<size_t>(v)]);
      const auto& cand = out[static_cast<size_t>(v)];
      int bestTo = -1;
      double bestTurn = 1e300;
      for (const HalfEdge& he : cand) {
        if (he.to == u && cand.size() > 1) continue;  // no U-turn unless it is the only way on
        const double turn = NormalizeTwoPi(base - he.angle);
        if (turn < bestTurn) { bestTurn = turn; bestTo = he.to; }
      }
      if (bestTo < 0) return {};
      u = v;
      v = bestTo;
      if (u == from && v == to) return loop;
    }
    return {};
  };

  // Start from the segments the +x ray from the seed crosses, nearest first.
  struct Cand { double xHit; int a, b; };
  std::vector<Cand> cands;
  for (const Edge& e : edges) {
    if (!e.alive) continue;
    const double x0 = nodeX[static_cast<size_t>(e.a)], y0 = nodeY[static_cast<size_t>(e.a)];
    const double x1 = nodeX[static_cast<size_t>(e.b)], y1 = nodeY[static_cast<size_t>(e.b)];
    if ((y0 > py) == (y1 > py)) continue;
    const double xHit = x0 + (py - y0) / (y1 - y0) * (x1 - x0);
    if (xHit < px) continue;
    cands.push_back({xHit, e.a, e.b});
  }
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.xHit < b.xHit; });

  std::vector<float> best;
  double bestArea = 1e300;
  for (const Cand& c : cands) {
    for (int dir = 0; dir < 2; ++dir) {
      std::vector<float> loop = dir == 0 ? trace(c.a, c.b) : trace(c.b, c.a);
      if (loop.size() < 6 || !LoopContains(loop, px, py)) continue;
      const double area = LoopAreaAbs(loop);
      if (area > 1e-12 && area < bestArea) {
        bestArea = area;
        best = std::move(loop);
      }
    }
    if (!best.empty()) break;  // the nearest crossed edge that bounds an enclosing face wins
  }
  return best;
}

}  // namespace detail

// Traces the smallest enclosing loop around (px,py). On success writes the ordered loop (flat x,y, not
// repeating the first point) to *outLoop and returns true.
inline bool TraceEnclosingLoop(const std::vector<Seg>& segs, double px, double py,
                               std::vector<float>* outLoop) {
  using namespace detail;
  if (!outLoop || segs.empty())
    return false;
  outLoop->clear();

  double mnX = 1e300, mnY = 1e300, mxX = -1e300, mxY = -1e300;
  for (const Seg& s : segs) {
    mnX = std::min({mnX, s.x0, s.x1}); mxX = std::max({mxX, s.x0, s.x1});
    mnY = std::min({mnY, s.y0, s.y1}); mxY = std::max({mxY, s.y0, s.y1});
  }
  // Snap tolerance from the overall extent so shared endpoints merge despite float rounding.
  const double diag = std::hypot(mxX - mnX, mxY - mnY);
  const double tol = std::fmax(1e-4, diag * 1e-6);

  // Every segment touching the window takes part, so a face that fits inside the window is bounded and
  // crossed by exactly the segments it would be in the whole drawing: it is the true answer. Otherwise
  // the window doubles, ending with the whole drawing.
  const double reach = std::max({px - mnX, mxX - px, py - mnY, mxY - py, tol});
  double half = std::max(diag / 256.0, tol * 64.0);
  std::size_t lastCount = static_cast<std::size_t>(-1);
  std::vector<Seg> window;
  for (;;) {
    const bool whole = half >= reach;
    const double wx0 = px - half, wx1 = px + half, wy0 = py - half, wy1 = py + half;
    window.clear();
    for (const Seg& s : segs)
      if (std::max(s.x0, s.x1) >= wx0 && std::min(s.x0, s.x1) <= wx1 &&
          std::max(s.y0, s.y1) >= wy0 && std::min(s.y0, s.y1) <= wy1)
        window.push_back(s);
    if (window.size() != lastCount || whole) {
      lastCount = window.size();
      std::vector<float> loop = TraceInSegments(window, px, py, tol);
      if (!loop.empty()) {
        bool inside = true;
        for (size_t i = 0; i + 1 < loop.size() && inside; i += 2)
          inside = loop[i] > wx0 + tol && loop[i] < wx1 - tol && loop[i + 1] > wy0 + tol && loop[i + 1] < wy1 - tol;
        if (inside || whole) {
          *outLoop = std::move(loop);
          return true;
        }
      }
    }
    if (whole) return false;
    half *= 2.0;
  }
}

}  // namespace hatchboundary
