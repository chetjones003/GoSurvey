#include "tinbuild.hpp"

#include <algorithm>
#include <cmath>

double TinOrient2D(double ax, double ay, double bx, double by, double cx, double cy) {
  return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

double TinInCircle(double ax, double ay, double bx, double by, double cx, double cy, double dx,
                   double dy) {
  // Standard determinant form, with d translated to the origin so the terms stay comparable in
  // magnitude. Positive means d lies inside the circumcircle of a CCW triangle a,b,c.
  const double adx = ax - dx, ady = ay - dy;
  const double bdx = bx - dx, bdy = by - dy;
  const double cdx = cx - dx, cdy = cy - dy;

  const double abdet = adx * bdy - bdx * ady;
  const double bcdet = bdx * cdy - cdx * bdy;
  const double cadet = cdx * ady - adx * cdy;
  const double alift = adx * adx + ady * ady;
  const double blift = bdx * bdx + bdy * bdy;
  const double clift = cdx * cdx + cdy * cdy;

  return alift * bcdet + blift * cadet + clift * abdet;
}

namespace {

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

/// A triangle plus its edge adjacency.
///
/// Vertices are counter-clockwise. Edge `i` is `(v[i], v[(i+1)%3])`, and `n[i]` is the triangle on
/// the other side of it (or \ref kNone at the hull). The adjacency is what makes insertion local:
/// without it, finding the triangles a new point invalidates means scanning all of them, which is
/// O(n²) overall — measured at ~70 s for the 100k points REQ-100 asks for.
struct Tri {
  std::uint32_t v[3] = {0, 0, 0};
  std::uint32_t n[3] = {kNone, kNone, kNone};
  bool alive = true;
};

} // namespace

TinBuildResult BuildTin(const std::vector<TinInputPoint>& points) {
  TinBuildResult r;

  // --- 1. De-duplicate plan positions -----------------------------------------------------------
  // Delaunay is undefined for coincident sites: they yield degenerate triangles or a flip loop that
  // never ends. Sorting by (x,y) groups candidates so the check is O(n log n) rather than O(n²).
  std::vector<TinInputPoint> pts = points;
  std::sort(pts.begin(), pts.end(), [](const TinInputPoint& p, const TinInputPoint& q) {
    if (p.x != q.x)
      return p.x < q.x;
    return p.y < q.y;
  });

  std::vector<TinInputPoint> uniq;
  uniq.reserve(pts.size());
  for (const TinInputPoint& p : pts) {
    bool dup = false;
    for (size_t k = uniq.size(); k-- > 0;) {
      if (p.x - uniq[k].x > kTinPlanEpsilon)
        break;  // sorted by x: nothing earlier can be within epsilon
      if (std::fabs(p.y - uniq[k].y) <= kTinPlanEpsilon) {
        dup = true;
        break;
      }
    }
    if (dup)
      ++r.duplicatesDropped;
    else
      uniq.push_back(p);
  }

  if (uniq.size() < 3) {
    r.status = TinBuildStatus::TooFewPoints;
    r.message = "Surface needs at least 3 points at distinct plan positions; got " +
                std::to_string(uniq.size()) + ".";
    return r;
  }

  // --- 2. Reject a fully collinear set ----------------------------------------------------------
  bool haveTriangle = false;
  for (size_t i = 2; i < uniq.size() && !haveTriangle; ++i)
    if (std::fabs(TinOrient2D(uniq[0].x, uniq[0].y, uniq[1].x, uniq[1].y, uniq[i].x, uniq[i].y)) > 1e-12)
      haveTriangle = true;
  if (!haveTriangle) {
    r.status = TinBuildStatus::AllCollinear;
    r.message = "Surface points are all collinear — no triangle can be formed.";
    return r;
  }

  const std::uint32_t nPts = static_cast<std::uint32_t>(uniq.size());

  // --- 3. Super-triangle ------------------------------------------------------------------------
  // Large enough that every input point is strictly inside, so insertion never special-cases the
  // hull. Its vertices sit past the real ones and any triangle still touching one at the end is
  // outside the convex hull and is dropped.
  double minX = uniq[0].x, maxX = uniq[0].x, minY = uniq[0].y, maxY = uniq[0].y;
  for (const TinInputPoint& p : uniq) {
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
  }
  const double dmax = std::max(std::max(maxX - minX, maxY - minY), 1.0);
  const double midX = 0.5 * (minX + maxX), midY = 0.5 * (minY + maxY);
  const double big = 1000.0 * dmax;

  std::vector<double> xs(nPts + 3), ys(nPts + 3);
  for (std::uint32_t i = 0; i < nPts; ++i) {
    xs[i] = uniq[i].x;
    ys[i] = uniq[i].y;
  }
  xs[nPts] = midX - big;     ys[nPts] = midY - big;
  xs[nPts + 1] = midX + big; ys[nPts + 1] = midY - big;
  xs[nPts + 2] = midX;       ys[nPts + 2] = midY + big;

  std::vector<Tri> tris;
  tris.reserve(nPts * 2 + 16);
  {
    Tri t;
    t.v[0] = nPts; t.v[1] = nPts + 1; t.v[2] = nPts + 2;
    tris.push_back(t);
  }

  // --- 4. Insertion order -----------------------------------------------------------------------
  // Points are inserted along a snaking grid traversal rather than in x-major order. Consecutive
  // insertions then land near each other, so the walk in step 5 takes a few steps instead of
  // crossing the whole triangulation — this is what turns the walk from a cost into a non-issue.
  std::vector<std::uint32_t> order(nPts);
  for (std::uint32_t i = 0; i < nPts; ++i)
    order[i] = i;
  {
    const double spanX = std::max(maxX - minX, 1e-9);
    const double spanY = std::max(maxY - minY, 1e-9);
    int cells = static_cast<int>(std::sqrt(static_cast<double>(nPts)) / 2.0) + 1;
    cells = std::max(cells, 1);
    auto cellOf = [&](std::uint32_t i, int* cx, int* cy) {
      *cx = std::min(cells - 1, static_cast<int>((xs[i] - minX) / spanX * cells));
      *cy = std::min(cells - 1, static_cast<int>((ys[i] - minY) / spanY * cells));
    };
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
      int ax, ay, bx, by;
      cellOf(a, &ax, &ay);
      cellOf(b, &bx, &by);
      if (ay != by)
        return ay < by;
      // Serpentine: reverse x on alternate rows so the end of one row is beside the start of the next.
      if (ax != bx)
        return (ay % 2 == 0) ? (ax < bx) : (ax > bx);
      return ys[a] < ys[b];
    });
  }

  // --- 5. Bowyer–Watson with adjacency ----------------------------------------------------------
  std::vector<std::uint32_t> cavity;     // triangles whose circumcircle contains the new point
  std::vector<std::uint32_t> stack;
  std::vector<char> inCavity(tris.capacity() + 16, 0);
  std::vector<std::uint32_t> bU, bV, bOut;  // boundary edge (u→v) and the triangle outside it
  std::vector<std::uint32_t> freeSlots;
  std::vector<std::uint32_t> fanByStart, fanByEnd;  // vertex → new triangle, for relinking the fan

  std::uint32_t hint = 0;
  for (std::uint32_t oi = 0; oi < nPts; ++oi) {
    const std::uint32_t ip = order[oi];
    const double px = xs[ip], py = ys[ip];

    // 5a. Locate the triangle containing p by walking across the edge it is outside of.
    std::uint32_t cur = hint;
    if (cur >= tris.size() || !tris[cur].alive) {
      cur = kNone;
      for (std::uint32_t t = 0; t < tris.size(); ++t)
        if (tris[t].alive) { cur = t; break; }
    }
    const std::uint32_t walkCap = static_cast<std::uint32_t>(tris.size()) + 16u;
    std::uint32_t steps = 0;
    while (cur != kNone && steps++ < walkCap) {
      const Tri& t = tris[cur];
      int outside = -1;
      for (int i = 0; i < 3; ++i) {
        const std::uint32_t a = t.v[i], b = t.v[(i + 1) % 3];
        if (TinOrient2D(xs[a], ys[a], xs[b], ys[b], px, py) < 0.0) {
          outside = i;
          break;
        }
      }
      if (outside < 0)
        break;  // inside (or on an edge of) this triangle
      cur = t.n[outside];
    }
    if (cur == kNone || steps >= walkCap || !tris[cur].alive) {
      // Defensive: the walk should always land, since the super-triangle contains every point. A
      // scan keeps a pathological case correct-but-slow rather than wrong.
      cur = kNone;
      for (std::uint32_t t = 0; t < tris.size() && cur == kNone; ++t) {
        if (!tris[t].alive)
          continue;
        const Tri& tt = tris[t];
        bool in = true;
        for (int i = 0; i < 3 && in; ++i) {
          const std::uint32_t a = tt.v[i], b = tt.v[(i + 1) % 3];
          if (TinOrient2D(xs[a], ys[a], xs[b], ys[b], px, py) < 0.0)
            in = false;
        }
        if (in)
          cur = t;
      }
      if (cur == kNone)
        continue;  // cannot place this point; skip it rather than corrupt the mesh
    }

    // 5b. Flood-fill the cavity: every triangle reachable across shared edges whose circumcircle
    // contains p. Local, so its cost is the size of the cavity, not the size of the mesh.
    cavity.clear();
    stack.clear();
    if (inCavity.size() < tris.size())
      inCavity.resize(tris.size() * 2 + 16, 0);
    stack.push_back(cur);
    inCavity[cur] = 1;
    cavity.push_back(cur);
    while (!stack.empty()) {
      const std::uint32_t t = stack.back();
      stack.pop_back();
      for (int i = 0; i < 3; ++i) {
        const std::uint32_t nb = tris[t].n[i];
        if (nb == kNone || inCavity[nb] || !tris[nb].alive)
          continue;
        const Tri& q = tris[nb];
        if (TinInCircle(xs[q.v[0]], ys[q.v[0]], xs[q.v[1]], ys[q.v[1]], xs[q.v[2]], ys[q.v[2]], px, py) > 0.0) {
          inCavity[nb] = 1;
          cavity.push_back(nb);
          stack.push_back(nb);
        }
      }
    }

    // 5c. Boundary of the cavity: edges whose far side is not in it.
    bU.clear(); bV.clear(); bOut.clear();
    for (std::uint32_t t : cavity) {
      for (int i = 0; i < 3; ++i) {
        const std::uint32_t nb = tris[t].n[i];
        if (nb != kNone && inCavity[nb])
          continue;
        bU.push_back(tris[t].v[i]);
        bV.push_back(tris[t].v[(i + 1) % 3]);
        bOut.push_back(nb);
      }
    }

    freeSlots.clear();
    for (std::uint32_t t : cavity) {
      tris[t].alive = false;
      inCavity[t] = 0;
      freeSlots.push_back(t);
    }

    // 5d. Re-fill: one triangle per boundary edge, fanned from p. Boundary edges are already
    // oriented CCW around the cavity, so (u, v, p) is CCW without a test.
    if (fanByStart.size() < xs.size()) {
      fanByStart.assign(xs.size(), kNone);
      fanByEnd.assign(xs.size(), kNone);
    }
    const size_t nb = bU.size();
    std::vector<std::uint32_t> made(nb);
    for (size_t k = 0; k < nb; ++k) {
      std::uint32_t slot;
      if (k < freeSlots.size()) {
        slot = freeSlots[k];
      } else {
        slot = static_cast<std::uint32_t>(tris.size());
        tris.push_back(Tri{});
        if (inCavity.size() < tris.size())
          inCavity.resize(tris.size() * 2 + 16, 0);
      }
      Tri& t = tris[slot];
      t.alive = true;
      t.v[0] = bU[k]; t.v[1] = bV[k]; t.v[2] = ip;
      t.n[0] = bOut[k];
      t.n[1] = kNone;
      t.n[2] = kNone;
      made[k] = slot;
      fanByStart[bU[k]] = slot;
      fanByEnd[bV[k]] = slot;
    }
    // Any leftover freed slots stay dead; nothing points at them, because every neighbour link into
    // the cavity is rewritten below.
    for (size_t k = nb; k < freeSlots.size(); ++k)
      tris[freeSlots[k]].alive = false;

    for (size_t k = 0; k < nb; ++k) {
      Tri& t = tris[made[k]];
      // Edge 1 is (v, p): shared with the fan triangle that starts at v.
      t.n[1] = fanByStart[bV[k]];
      // Edge 2 is (p, u): shared with the fan triangle that ends at u.
      t.n[2] = fanByEnd[bU[k]];
      // Point the outside triangle back at this one, replacing its link to the dead cavity triangle.
      if (t.n[0] != kNone) {
        Tri& out = tris[t.n[0]];
        for (int i = 0; i < 3; ++i) {
          const std::uint32_t a = out.v[i], b = out.v[(i + 1) % 3];
          if (a == bV[k] && b == bU[k]) {  // the same edge, seen from the other side
            out.n[i] = made[k];
            break;
          }
        }
      }
    }
    for (size_t k = 0; k < nb; ++k) {  // reset the scratch maps for the next insertion
      fanByStart[bU[k]] = kNone;
      fanByEnd[bV[k]] = kNone;
    }

    hint = made.empty() ? hint : made[0];
  }

  // --- 6. Strip the super-triangle and emit -----------------------------------------------------
  r.vertsXyz.reserve(uniq.size() * 3);
  for (const TinInputPoint& p : uniq) {
    r.vertsXyz.push_back(static_cast<float>(p.x));
    r.vertsXyz.push_back(static_cast<float>(p.y));
    r.vertsXyz.push_back(p.z);
  }
  for (const Tri& t : tris) {
    if (!t.alive)
      continue;
    if (t.v[0] >= nPts || t.v[1] >= nPts || t.v[2] >= nPts)
      continue;  // touches the super-triangle → outside the convex hull
    r.indices.push_back(t.v[0]);
    r.indices.push_back(t.v[1]);
    r.indices.push_back(t.v[2]);
  }

  if (r.indices.empty()) {
    // Defensive: the collinear check above should already have caught the only way this happens.
    r.status = TinBuildStatus::AllCollinear;
    r.message = "Surface points produced no triangles.";
    r.vertsXyz.clear();
    return r;
  }

  r.status = TinBuildStatus::Ok;
  if (r.duplicatesDropped > 0)
    r.message = "Dropped " + std::to_string(r.duplicatesDropped) +
                " point(s) sharing a plan position with an earlier point (first occurrence kept).";
  return r;
}
