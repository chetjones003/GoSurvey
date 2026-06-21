#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Planar boundary tracing for the HATCH command (REQ-043, ADR-017). Given a set of boundary segments
// (lines, polyline edges, and tessellated arcs/circles supplied by the caller) and an internal seed
// point, find the SMALLEST closed loop that encloses the seed by walking the planar subdivision face
// that contains it. Returns false when no closed region contains the point (REQ-201 — the command then
// places nothing). Pure geometry, dependency-free, so it is unit-tested without the GUI.
//
// Assumption (documented): boundary geometry meets at shared endpoints (within a snap tolerance). Edges
// that merely cross without a shared vertex are not split; such a configuration may yield "no boundary",
// which is the accepted failure mode for this first version.

namespace hatchboundary {

struct Seg {
  float x0, y0, x1, y1;
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

}  // namespace detail

// Traces the smallest enclosing loop around (px,py). On success writes the ordered loop (flat x,y, not
// repeating the first point) to *outLoop and returns true.
inline bool TraceEnclosingLoop(const std::vector<Seg>& segs, double px, double py,
                               std::vector<float>* outLoop) {
  using namespace detail;
  if (!outLoop || segs.empty())
    return false;
  outLoop->clear();

  // Snap tolerance from the overall extent so shared endpoints merge despite float rounding.
  float mnX = 1e30f, mnY = 1e30f, mxX = -1e30f, mxY = -1e30f;
  for (const Seg& s : segs) {
    mnX = std::fmin(mnX, std::fmin(s.x0, s.x1)); mxX = std::fmax(mxX, std::fmax(s.x0, s.x1));
    mnY = std::fmin(mnY, std::fmin(s.y0, s.y1)); mxY = std::fmax(mxY, std::fmax(s.y0, s.y1));
  }
  const double diag = std::hypot(static_cast<double>(mxX - mnX), static_cast<double>(mxY - mnY));
  const double tol = std::fmax(1e-4, diag * 1e-6);
  const double inv = 1.0 / tol;

  // Build nodes by snapping endpoints onto a tol grid.
  std::vector<double> nodeX, nodeY;
  std::unordered_map<uint64_t, int> nodeOf;
  auto key = [&](double x, double y) -> uint64_t {
    const int64_t ix = static_cast<int64_t>(std::llround(x * inv));
    const int64_t iy = static_cast<int64_t>(std::llround(y * inv));
    return (static_cast<uint64_t>(static_cast<uint32_t>(ix)) << 32) ^ static_cast<uint32_t>(iy);
  };
  auto node = [&](double x, double y) -> int {
    const uint64_t k = key(x, y);
    auto it = nodeOf.find(k);
    if (it != nodeOf.end()) return it->second;
    const int id = static_cast<int>(nodeX.size());
    nodeOf.emplace(k, id);
    nodeX.push_back(x);
    nodeY.push_back(y);
    return id;
  };

  // Outgoing half-edges per node (skip zero-length segments).
  std::vector<std::vector<HalfEdge>> out;
  auto ensure = [&](int n) { if (static_cast<int>(out.size()) <= n) out.resize(n + 1); };
  for (const Seg& s : segs) {
    if (std::fabs(s.x1 - s.x0) < tol && std::fabs(s.y1 - s.y0) < tol)
      continue;
    const int a = node(s.x0, s.y0);
    const int b = node(s.x1, s.y1);
    if (a == b) continue;
    ensure(a); ensure(b);
    out[a].push_back({b, std::atan2(nodeY[b] - nodeY[a], nodeX[b] - nodeX[a])});
    out[b].push_back({a, std::atan2(nodeY[a] - nodeY[b], nodeX[a] - nodeX[b])});
  }
  ensure(static_cast<int>(nodeX.size()) - 1);

  // Trace a simple cycle by always taking the tightest clockwise turn at each node. Returns the loop
  // (flat x,y, start node not repeated) or empty if it dead-ends, revisits a node (open chain), or runs
  // away. Revisiting any node other than the start means the walk is not a simple cycle.
  std::vector<char> visited(nodeX.size(), 0);
  auto trace = [&](int from, int to) -> std::vector<float> {
    std::fill(visited.begin(), visited.end(), 0);
    std::vector<float> loop;
    const size_t guard = nodeX.size() * 4 + 16;
    int u = from, v = to;
    loop.push_back(static_cast<float>(nodeX[u]));
    loop.push_back(static_cast<float>(nodeY[u]));
    visited[static_cast<size_t>(u)] = 1;
    for (size_t step = 0; step < guard; ++step) {
      if (v == from)
        return loop;  // closed the cycle (loop holds from..u, start not repeated)
      if (visited[static_cast<size_t>(v)])
        return {};  // revisited a non-start node → open chain / not a simple face
      visited[static_cast<size_t>(v)] = 1;
      loop.push_back(static_cast<float>(nodeX[v]));
      loop.push_back(static_cast<float>(nodeY[v]));
      // Direction from v back toward u, then pick the outgoing edge with the smallest clockwise turn.
      const double base = std::atan2(nodeY[u] - nodeY[v], nodeX[u] - nodeX[v]);
      const auto& cand = out[static_cast<size_t>(v)];
      int bestTo = -1;
      double bestTurn = 1e300;
      for (const HalfEdge& he : cand) {
        if (he.to == u && cand.size() > 1)
          continue;  // avoid immediate U-turn unless it's the only option (dead end)
        const double turn = NormalizeTwoPi(base - he.angle);
        if (turn < bestTurn) { bestTurn = turn; bestTo = he.to; }
      }
      if (bestTo < 0)
        return {};
      u = v;
      v = bestTo;
    }
    return {};
  };

  // Find candidate start segments: those crossed by the +x ray from (px,py). Try the closest ones first.
  struct Cand { double xHit; int a, b; };
  std::vector<Cand> cands;
  for (const Seg& s : segs) {
    const double y0 = s.y0, y1 = s.y1, x0 = s.x0, x1 = s.x1;
    if ((y0 > py) == (y1 > py))
      continue;  // does not straddle the horizontal ray
    const double t = (py - y0) / (y1 - y0);
    const double xHit = x0 + t * (x1 - x0);
    if (xHit < px)
      continue;  // hit is to the left of the seed
    cands.push_back({xHit, node(x0, y0), node(x1, y1)});
  }
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.xHit < b.xHit; });

  std::vector<float> best;
  double bestArea = 1e300;
  for (const Cand& c : cands) {
    for (int dir = 0; dir < 2; ++dir) {
      std::vector<float> loop = (dir == 0) ? trace(c.a, c.b) : trace(c.b, c.a);
      if (loop.size() < 6)
        continue;
      if (!LoopContains(loop, px, py))
        continue;
      const double area = LoopAreaAbs(loop);
      if (area > 1e-12 && area < bestArea) {
        bestArea = area;
        best = std::move(loop);
      }
    }
    if (!best.empty())
      break;  // closest straddling segment that yields an enclosing face wins
  }
  if (best.empty())
    return false;
  *outLoop = std::move(best);
  return true;
}

}  // namespace hatchboundary
