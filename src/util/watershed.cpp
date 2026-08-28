#include "watershed.hpp"

#include "surfaceanalysis.hpp"
#include "tinbuild.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace {

constexpr double kFlatGradePct = kFlatGradePctDefault;
constexpr int kMaxWalk = 1000000;

struct TriGeom {
  AnalysisTriangle plane{};
  double cx = 0.0, cy = 0.0;
  double area2d = 0.0;
};

bool LoadTri(const std::vector<float>& verts, const std::vector<std::uint32_t>& idx, int t,
             TriGeom* out) {
  if (!out)
    return false;
  const size_t n = verts.size() / 3;
  const std::uint32_t ia = idx[static_cast<size_t>(t) * 3 + 0];
  const std::uint32_t ib = idx[static_cast<size_t>(t) * 3 + 1];
  const std::uint32_t ic = idx[static_cast<size_t>(t) * 3 + 2];
  if (ia >= n || ib >= n || ic >= n)
    return false;
  AnalysisTriangle& p = out->plane;
  p.x0 = verts[static_cast<size_t>(ia) * 3 + 0];
  p.y0 = verts[static_cast<size_t>(ia) * 3 + 1];
  p.z0 = verts[static_cast<size_t>(ia) * 3 + 2];
  p.x1 = verts[static_cast<size_t>(ib) * 3 + 0];
  p.y1 = verts[static_cast<size_t>(ib) * 3 + 1];
  p.z1 = verts[static_cast<size_t>(ib) * 3 + 2];
  p.x2 = verts[static_cast<size_t>(ic) * 3 + 0];
  p.y2 = verts[static_cast<size_t>(ic) * 3 + 1];
  p.z2 = verts[static_cast<size_t>(ic) * 3 + 2];
  out->cx = (p.x0 + p.x1 + p.x2) / 3.0;
  out->cy = (p.y0 + p.y1 + p.y2) / 3.0;
  const double ux = p.x1 - p.x0, uy = p.y1 - p.y0;
  const double vx = p.x2 - p.x0, vy = p.y2 - p.y0;
  out->area2d = 0.5 * std::abs(ux * vy - uy * vx);
  return true;
}

bool RayHitsSeg(double ox, double oy, double dx, double dy, double ax, double ay, double bx,
                double by, double* tOut) {
  if (!tOut)
    return false;
  const double ex = bx - ax, ey = by - ay;
  const double det = dx * (-ey) - (-ex) * dy;
  if (std::fabs(det) < 1.0e-18)
    return false;
  const double rhsx = ax - ox, rhsy = ay - oy;
  const double t = (rhsx * (-ey) - (-ex) * rhsy) / det;
  const double u = (dx * rhsy - dy * rhsx) / det;
  if (!(t > 1.0e-9))
    return false;
  if (u < -1.0e-7 || u > 1.0 + 1.0e-7)
    return false;
  *tOut = t;
  return true;
}

void CornerXY(const AnalysisTriangle& p, int c, double* x, double* y) {
  if (c == 0) {
    *x = p.x0;
    *y = p.y0;
  } else if (c == 1) {
    *x = p.x1;
    *y = p.y1;
  } else {
    *x = p.x2;
    *y = p.y2;
  }
}

int ExitEdgeFrom(const AnalysisTriangle& p, double ox, double oy, double dx, double dy) {
  int best = -1;
  double bestT = std::numeric_limits<double>::infinity();
  for (int e = 0; e < 3; ++e) {
    double ax = 0, ay = 0, bx = 0, by = 0;
    CornerXY(p, e, &ax, &ay);
    CornerXY(p, (e + 1) % 3, &bx, &by);
    double t = 0.0;
    if (!RayHitsSeg(ox, oy, dx, dy, ax, ay, bx, by, &t))
      continue;
    if (t < bestT) {
      bestT = t;
      best = e;
    }
  }
  return best;
}

void HitOnEdge(const AnalysisTriangle& p, int e, double ox, double oy, double dx, double dy,
               double* hx, double* hy) {
  double ax = 0, ay = 0, bx = 0, by = 0;
  CornerXY(p, e, &ax, &ay);
  CornerXY(p, (e + 1) % 3, &bx, &by);
  double t = 0.0;
  if (!RayHitsSeg(ox, oy, dx, dy, ax, ay, bx, by, &t)) {
    *hx = (ax + bx) * 0.5;
    *hy = (ay + by) * 0.5;
    return;
  }
  *hx = ox + t * dx;
  *hy = oy + t * dy;
}

void BuildNeighbors(const std::vector<std::uint32_t>& indices, int nTri,
                    std::vector<int>* neigh /* 3 per tri */) {
  neigh->assign(static_cast<size_t>(nTri) * 3, -1);
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, int>> edgeOwner;
  for (int t = 0; t < nTri; ++t) {
    for (int e = 0; e < 3; ++e) {
      const std::uint32_t a = indices[static_cast<size_t>(t) * 3 + static_cast<size_t>(e)];
      const std::uint32_t b = indices[static_cast<size_t>(t) * 3 + static_cast<size_t>((e + 1) % 3)];
      const auto key = (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
      auto it = edgeOwner.find(key);
      if (it == edgeOwner.end()) {
        edgeOwner.emplace(key, std::make_pair(t, e));
        continue;
      }
      const int ot = it->second.first;
      const int oe = it->second.second;
      (*neigh)[static_cast<size_t>(t) * 3 + static_cast<size_t>(e)] = ot;
      (*neigh)[static_cast<size_t>(ot) * 3 + static_cast<size_t>(oe)] = t;
    }
  }
}

int CoveringTriangle(const std::vector<float>& verts, const std::vector<std::uint32_t>& idx, double x,
                     double y, double* zOut) {
  const int nTri = static_cast<int>(idx.size() / 3);
  for (int t = 0; t < nTri && t < kMaxWalk; ++t) {
    double z = 0.0;
    if (TinTriangleElevationAt(verts, idx[static_cast<size_t>(t) * 3 + 0],
                               idx[static_cast<size_t>(t) * 3 + 1], idx[static_cast<size_t>(t) * 3 + 2],
                               x, y, &z)) {
      if (zOut)
        *zOut = z;
      return t;
    }
  }
  return -1;
}

void CollectCovering(const std::vector<float>& verts, const std::vector<std::uint32_t>& idx, double x,
                     double y, std::vector<int>* out) {
  out->clear();
  const int nTri = static_cast<int>(idx.size() / 3);
  for (int t = 0; t < nTri && t < kMaxWalk; ++t) {
    double z = 0.0;
    if (TinTriangleElevationAt(verts, idx[static_cast<size_t>(t) * 3 + 0],
                               idx[static_cast<size_t>(t) * 3 + 1], idx[static_cast<size_t>(t) * 3 + 2],
                               x, y, &z))
      out->push_back(t);
  }
}

void PushSeg(std::vector<float>* out, double x0, double y0, double z0, double x1, double y1,
             double z1) {
  out->push_back(static_cast<float>(x0));
  out->push_back(static_cast<float>(y0));
  out->push_back(static_cast<float>(z0));
  out->push_back(static_cast<float>(x1));
  out->push_back(static_cast<float>(y1));
  out->push_back(static_cast<float>(z1));
}

} // namespace

WatershedResult ComputeWatershed(const std::vector<float>& vertsXyz,
                                 const std::vector<std::uint32_t>& indices) {
  WatershedResult r;
  const int nTri = static_cast<int>(indices.size() / 3);
  if (nTri <= 0 || vertsXyz.size() < 9) {
    r.error = "null TIN";
    return r;
  }

  std::vector<int> neigh;
  BuildNeighbors(indices, nTri, &neigh);
  r.successor.assign(static_cast<size_t>(nTri), -1);
  std::vector<DrainKind> term(static_cast<size_t>(nTri), DrainKind::Boundary);
  std::vector<double> dxn(static_cast<size_t>(nTri), 0.0);
  std::vector<double> dyn(static_cast<size_t>(nTri), 0.0);
  std::vector<double> dzn(static_cast<size_t>(nTri), 0.0);

  for (int t = 0; t < nTri; ++t) {
    TriGeom g;
    if (!LoadTri(vertsXyz, indices, t, &g))
      continue;
    double dx = 0.0, dy = 0.0;
    if (!TriangleDownhillDirection(g.plane, kFlatGradePct, &dx, &dy)) {
      term[static_cast<size_t>(t)] = DrainKind::Flat;
      dxn[static_cast<size_t>(t)] = g.cx;
      dyn[static_cast<size_t>(t)] = g.cy;
      dzn[static_cast<size_t>(t)] = TriangleCentroidZ(g.plane);
      continue;
    }
    const int e = ExitEdgeFrom(g.plane, g.cx, g.cy, dx, dy);
    if (e < 0) {
      term[static_cast<size_t>(t)] = DrainKind::Flat;
      dxn[static_cast<size_t>(t)] = g.cx;
      dyn[static_cast<size_t>(t)] = g.cy;
      dzn[static_cast<size_t>(t)] = TriangleCentroidZ(g.plane);
      continue;
    }
    double hx = 0.0, hy = 0.0;
    HitOnEdge(g.plane, e, g.cx, g.cy, dx, dy, &hx, &hy);
    double hz = 0.0;
    const std::uint32_t ia = indices[static_cast<size_t>(t) * 3 + 0];
    const std::uint32_t ib = indices[static_cast<size_t>(t) * 3 + 1];
    const std::uint32_t ic = indices[static_cast<size_t>(t) * 3 + 2];
    if (!TinTriangleElevationAt(vertsXyz, ia, ib, ic, hx, hy, &hz))
      hz = TriangleCentroidZ(g.plane);
    dxn[static_cast<size_t>(t)] = hx;
    dyn[static_cast<size_t>(t)] = hy;
    dzn[static_cast<size_t>(t)] = hz;
    const int nb = neigh[static_cast<size_t>(t) * 3 + static_cast<size_t>(e)];
    if (nb < 0)
      term[static_cast<size_t>(t)] = DrainKind::Boundary;
    else
      r.successor[static_cast<size_t>(t)] = nb;
  }

  // A 2-cycle (or longer) of successors is a pit, not a flow off the mesh.
  for (int t = 0; t < nTri; ++t) {
    const int n = r.successor[static_cast<size_t>(t)];
    if (n < 0)
      continue;
    if (r.successor[static_cast<size_t>(n)] == t) {
      term[static_cast<size_t>(t)] = DrainKind::Depression;
      term[static_cast<size_t>(n)] = DrainKind::Depression;
      r.successor[static_cast<size_t>(t)] = -1;
      r.successor[static_cast<size_t>(n)] = -1;
    }
  }
  for (int pass = 0; pass < nTri && pass < kMaxWalk; ++pass) {
    bool changed = false;
    for (int t = 0; t < nTri; ++t) {
      const int n = r.successor[static_cast<size_t>(t)];
      if (n < 0)
        continue;
      if (r.successor[static_cast<size_t>(n)] >= 0)
        continue;
      if (term[static_cast<size_t>(n)] != DrainKind::Depression)
        continue;
      // Walk into a marked pit: stop here as the same depression rather than a neighbour boundary.
      term[static_cast<size_t>(t)] = DrainKind::Depression;
      r.successor[static_cast<size_t>(t)] = -1;
      changed = true;
    }
    if (!changed)
      break;
  }

  r.triangleBasinId.assign(static_cast<size_t>(nTri), -1);
  int nextId = 0;
  for (int t = 0; t < nTri; ++t) {
    if (r.triangleBasinId[static_cast<size_t>(t)] >= 0)
      continue;
    std::vector<int> chain;
    int cur = t;
    for (int s = 0; s < nTri + 2 && s < kMaxWalk; ++s) {
      if (cur < 0)
        break;
      if (r.triangleBasinId[static_cast<size_t>(cur)] >= 0) {
        const int id = r.triangleBasinId[static_cast<size_t>(cur)];
        for (int c : chain)
          r.triangleBasinId[static_cast<size_t>(c)] = id;
        chain.clear();
        break;
      }
      bool loop = false;
      for (int c : chain) {
        if (c == cur) {
          loop = true;
          break;
        }
      }
      if (loop) {
        WatershedBasin b;
        b.id = nextId++;
        b.drain = DrainKind::Depression;
        b.drainX = dxn[static_cast<size_t>(cur)];
        b.drainY = dyn[static_cast<size_t>(cur)];
        b.drainZ = dzn[static_cast<size_t>(cur)];
        for (int c : chain)
          r.triangleBasinId[static_cast<size_t>(c)] = b.id;
        r.triangleBasinId[static_cast<size_t>(cur)] = b.id;
        r.basins.push_back(b);
        chain.clear();
        break;
      }
      chain.push_back(cur);
      if (r.successor[static_cast<size_t>(cur)] < 0) {
        WatershedBasin b;
        b.id = nextId++;
        b.drain = term[static_cast<size_t>(cur)];
        b.drainX = dxn[static_cast<size_t>(cur)];
        b.drainY = dyn[static_cast<size_t>(cur)];
        b.drainZ = dzn[static_cast<size_t>(cur)];
        for (int c : chain)
          r.triangleBasinId[static_cast<size_t>(c)] = b.id;
        r.basins.push_back(b);
        chain.clear();
        break;
      }
      cur = r.successor[static_cast<size_t>(cur)];
    }
  }

  for (WatershedBasin& b : r.basins)
    b.area2d = 0.0;
  for (int t = 0; t < nTri; ++t) {
    const int id = r.triangleBasinId[static_cast<size_t>(t)];
    if (id < 0 || id >= static_cast<int>(r.basins.size()))
      continue;
    TriGeom g;
    if (!LoadTri(vertsXyz, indices, t, &g))
      continue;
    r.basins[static_cast<size_t>(id)].area2d += g.area2d;
  }
  r.ok = true;
  return r;
}

WaterDropResult ComputeWaterDrop(const std::vector<float>& vertsXyz,
                                 const std::vector<std::uint32_t>& indices, double x, double y) {
  WaterDropResult d;
  const int nTri = static_cast<int>(indices.size() / 3);
  if (nTri <= 0 || vertsXyz.size() < 9)
    return d;
  double z = 0.0;
  int t = CoveringTriangle(vertsXyz, indices, x, y, &z);
  if (t < 0) {
    d.ok = true;
    d.outside = true;
    return d;
  }
  d.ok = true;
  std::vector<int> neigh;
  BuildNeighbors(indices, nTri, &neigh);
  int prev = -1;
  double px = x, py = y, pz = z;
  d.pathXyz.push_back(static_cast<float>(px));
  d.pathXyz.push_back(static_cast<float>(py));
  d.pathXyz.push_back(static_cast<float>(pz));
  for (int step = 0; step < nTri + 4 && step < kMaxWalk; ++step) {
    TriGeom g;
    if (!LoadTri(vertsXyz, indices, t, &g))
      break;
    double dx = 0.0, dy = 0.0;
    if (!TriangleDownhillDirection(g.plane, kFlatGradePct, &dx, &dy)) {
      d.terminal = DrainKind::Flat;
      break;
    }
    const int e = ExitEdgeFrom(g.plane, px, py, dx, dy);
    if (e < 0) {
      d.terminal = DrainKind::Flat;
      break;
    }
    double hx = 0.0, hy = 0.0;
    HitOnEdge(g.plane, e, px, py, dx, dy, &hx, &hy);
    double hz = pz;
    const std::uint32_t ia = indices[static_cast<size_t>(t) * 3 + 0];
    const std::uint32_t ib = indices[static_cast<size_t>(t) * 3 + 1];
    const std::uint32_t ic = indices[static_cast<size_t>(t) * 3 + 2];
    (void)TinTriangleElevationAt(vertsXyz, ia, ib, ic, hx, hy, &hz);
    d.pathXyz.push_back(static_cast<float>(hx));
    d.pathXyz.push_back(static_cast<float>(hy));
    d.pathXyz.push_back(static_cast<float>(hz));
    const int nb = neigh[static_cast<size_t>(t) * 3 + static_cast<size_t>(e)];
    if (nb < 0) {
      d.terminal = DrainKind::Boundary;
      break;
    }
    if (nb == prev) {
      d.terminal = DrainKind::Depression;
      break;
    }
    prev = t;
    t = nb;
    px = hx;
    py = hy;
    pz = hz;
  }
  const WatershedResult w = ComputeWatershed(vertsXyz, indices);
  int start = CoveringTriangle(vertsXyz, indices, x, y, nullptr);
  if (d.terminal != DrainKind::Depression && w.ok && start >= 0 &&
      start < static_cast<int>(w.triangleBasinId.size())) {
    const int bid = w.triangleBasinId[static_cast<size_t>(start)];
    if (bid >= 0 && bid < static_cast<int>(w.basins.size()) &&
        w.basins[static_cast<size_t>(bid)].drain == DrainKind::Depression)
      d.terminal = DrainKind::Depression;
  }
  return d;
}

CatchmentResult ComputeCatchment(const std::vector<float>& vertsXyz,
                                 const std::vector<std::uint32_t>& indices, double x, double y) {
  CatchmentResult c;
  const int nTri = static_cast<int>(indices.size() / 3);
  if (nTri <= 0 || vertsXyz.size() < 9)
    return c;
  std::vector<int> seeds;
  CollectCovering(vertsXyz, indices, x, y, &seeds);
  for (int t = 0; t < nTri && t < kMaxWalk; ++t) {
    TriGeom g;
    if (!LoadTri(vertsXyz, indices, t, &g))
      continue;
    bool onEdge = false;
    for (int e = 0; e < 3; ++e) {
      double ax = 0, ay = 0, bx = 0, by = 0;
      CornerXY(g.plane, e, &ax, &ay);
      CornerXY(g.plane, (e + 1) % 3, &bx, &by);
      const double ex = bx - ax, ey = by - ay;
      const double len2 = ex * ex + ey * ey;
      if (len2 < 1.0e-18)
        continue;
      const double tpar = ((x - ax) * ex + (y - ay) * ey) / len2;
      if (tpar < -1.0e-6 || tpar > 1.0 + 1.0e-6)
        continue;
      const double px = ax + tpar * ex, py = ay + tpar * ey;
      const double ddx = x - px, ddy = y - py;
      if (ddx * ddx + ddy * ddy <= 1.0e-6)
        onEdge = true;
    }
    if (onEdge && std::find(seeds.begin(), seeds.end(), t) == seeds.end())
      seeds.push_back(t);
  }
  if (seeds.empty()) {
    c.ok = true;
    c.outside = true;
    return c;
  }
  std::vector<int> neigh;
  BuildNeighbors(indices, nTri, &neigh);
  const size_t seed0 = seeds.size();
  for (size_t si = 0; si < seed0; ++si) {
    const int t = seeds[si];
    TriGeom g;
    if (!LoadTri(vertsXyz, indices, t, &g))
      continue;
    for (int e = 0; e < 3; ++e) {
      double ax = 0, ay = 0, bx = 0, by = 0;
      CornerXY(g.plane, e, &ax, &ay);
      CornerXY(g.plane, (e + 1) % 3, &bx, &by);
      const double ex = bx - ax, ey = by - ay;
      const double len2 = ex * ex + ey * ey;
      if (len2 < 1.0e-18)
        continue;
      const double tpar = ((x - ax) * ex + (y - ay) * ey) / len2;
      if (tpar < -1.0e-7 || tpar > 1.0 + 1.0e-7)
        continue;
      const double px = ax + tpar * ex, py = ay + tpar * ey;
      const double ddx = x - px, ddy = y - py;
      if (ddx * ddx + ddy * ddy > 1.0e-8)
        continue;
      const int nb = neigh[static_cast<size_t>(t) * 3 + static_cast<size_t>(e)];
      if (nb < 0)
        continue;
      if (std::find(seeds.begin(), seeds.end(), nb) == seeds.end())
        seeds.push_back(nb);
    }
  }
  const WatershedResult w = ComputeWatershed(vertsXyz, indices);
  if (!w.ok)
    return c;
  std::vector<int> basinIds;
  for (int t : seeds) {
    if (t < 0 || t >= static_cast<int>(w.triangleBasinId.size()))
      continue;
    const int bid = w.triangleBasinId[static_cast<size_t>(t)];
    if (bid < 0)
      continue;
    if (std::find(basinIds.begin(), basinIds.end(), bid) == basinIds.end())
      basinIds.push_back(bid);
  }
  std::vector<char> seen(static_cast<size_t>(nTri), 0);
  if (basinIds.size() > 1) {
    for (int t = 0; t < nTri; ++t) {
      const int bid = w.triangleBasinId[static_cast<size_t>(t)];
      if (std::find(basinIds.begin(), basinIds.end(), bid) == basinIds.end())
        continue;
      seen[static_cast<size_t>(t)] = 1;
      c.triangleIds.push_back(t);
    }
  } else {
    std::vector<std::vector<int>> pred(static_cast<size_t>(nTri));
    for (int t = 0; t < nTri; ++t) {
      const int n = w.successor[static_cast<size_t>(t)];
      if (n >= 0)
        pred[static_cast<size_t>(n)].push_back(t);
    }
    std::vector<int> stack = seeds;
    for (int s = 0; s < static_cast<int>(stack.size()) && s < kMaxWalk; ++s) {
      const int t = stack[static_cast<size_t>(s)];
      if (t < 0 || t >= nTri || seen[static_cast<size_t>(t)])
        continue;
      seen[static_cast<size_t>(t)] = 1;
      c.triangleIds.push_back(t);
      for (int p : pred[static_cast<size_t>(t)])
        stack.push_back(p);
    }
  }
  c.ok = true;
  c.minZ = std::numeric_limits<double>::infinity();
  c.maxZ = -std::numeric_limits<double>::infinity();
  for (int t : c.triangleIds) {
    TriGeom g;
    if (!LoadTri(vertsXyz, indices, t, &g))
      continue;
    c.area2d += g.area2d;
    c.minZ = std::min({c.minZ, g.plane.z0, g.plane.z1, g.plane.z2});
    c.maxZ = std::max({c.maxZ, g.plane.z0, g.plane.z1, g.plane.z2});
  }
  if (!(c.minZ < std::numeric_limits<double>::infinity())) {
    c.minZ = 0.0;
    c.maxZ = 0.0;
  }
  return c;
}

void AppendWatershedBasinOutlines(const WatershedResult& w, const std::vector<float>& vertsXyz,
                                  const std::vector<std::uint32_t>& indices, std::vector<float>* out) {
  if (!out || !w.ok)
    return;
  const int nTri = static_cast<int>(indices.size() / 3);
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, int>> edgeBasins;
  for (int t = 0; t < nTri; ++t) {
    const int bid = t < static_cast<int>(w.triangleBasinId.size()) ? w.triangleBasinId[static_cast<size_t>(t)] : -1;
    for (int e = 0; e < 3; ++e) {
      const std::uint32_t a = indices[static_cast<size_t>(t) * 3 + static_cast<size_t>(e)];
      const std::uint32_t b = indices[static_cast<size_t>(t) * 3 + static_cast<size_t>((e + 1) % 3)];
      const auto key = (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
      auto it = edgeBasins.find(key);
      if (it == edgeBasins.end())
        edgeBasins.emplace(key, std::make_pair(bid, -1));
      else
        it->second.second = bid;
    }
  }
  const size_t nv = vertsXyz.size() / 3;
  for (const auto& kv : edgeBasins) {
    const int b0 = kv.second.first, b1 = kv.second.second;
    if (b0 == b1 && b0 >= 0)
      continue;
    const std::uint32_t a = kv.first.first, b = kv.first.second;
    if (a >= nv || b >= nv)
      continue;
    PushSeg(out, vertsXyz[static_cast<size_t>(a) * 3 + 0], vertsXyz[static_cast<size_t>(a) * 3 + 1],
            vertsXyz[static_cast<size_t>(a) * 3 + 2], vertsXyz[static_cast<size_t>(b) * 3 + 0],
            vertsXyz[static_cast<size_t>(b) * 3 + 1], vertsXyz[static_cast<size_t>(b) * 3 + 2]);
  }
}

void AppendCatchmentBoundary(const CatchmentResult& c, const std::vector<float>& vertsXyz,
                             const std::vector<std::uint32_t>& indices, std::vector<float>* out) {
  if (!out || !c.ok || c.outside)
    return;
  std::vector<char> inSet(indices.size() / 3, 0);
  for (int t : c.triangleIds) {
    if (t >= 0 && static_cast<size_t>(t) < inSet.size())
      inSet[static_cast<size_t>(t)] = 1;
  }
  const int nTri = static_cast<int>(indices.size() / 3);
  std::map<std::pair<std::uint32_t, std::uint32_t>, int> count;
  for (int t = 0; t < nTri; ++t) {
    if (!inSet[static_cast<size_t>(t)])
      continue;
    for (int e = 0; e < 3; ++e) {
      const std::uint32_t a = indices[static_cast<size_t>(t) * 3 + static_cast<size_t>(e)];
      const std::uint32_t b = indices[static_cast<size_t>(t) * 3 + static_cast<size_t>((e + 1) % 3)];
      const auto key = (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
      count[key] += 1;
    }
  }
  const size_t nv = vertsXyz.size() / 3;
  for (const auto& kv : count) {
    if (kv.second != 1)
      continue;
    const std::uint32_t a = kv.first.first, b = kv.first.second;
    if (a >= nv || b >= nv)
      continue;
    PushSeg(out, vertsXyz[static_cast<size_t>(a) * 3 + 0], vertsXyz[static_cast<size_t>(a) * 3 + 1],
            vertsXyz[static_cast<size_t>(a) * 3 + 2], vertsXyz[static_cast<size_t>(b) * 3 + 0],
            vertsXyz[static_cast<size_t>(b) * 3 + 1], vertsXyz[static_cast<size_t>(b) * 3 + 2]);
  }
}

void AppendPathAsLines(const std::vector<float>& pathXyz, std::vector<float>* out) {
  if (!out || pathXyz.size() < 6)
    return;
  const int n = static_cast<int>(pathXyz.size() / 3);
  for (int i = 0; i + 1 < n && i < kMaxWalk; ++i) {
    const size_t a = static_cast<size_t>(i) * 3;
    const size_t b = static_cast<size_t>(i + 1) * 3;
    PushSeg(out, pathXyz[a], pathXyz[a + 1], pathXyz[a + 2], pathXyz[b], pathXyz[b + 1], pathXyz[b + 2]);
  }
}
