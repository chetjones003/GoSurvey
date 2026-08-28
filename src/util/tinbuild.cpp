#include "tinbuild.hpp"

#include <algorithm>
#include <cmath>
#include <deque>

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

std::vector<TinCrossingIssue> TinFindCrossingConflicts(const std::vector<TinConstraint>& constraints) {
  std::vector<TinCrossingIssue> issues;
  for (size_t i = 0; i < constraints.size(); ++i) {
    const TinConstraint& a = constraints[i];
    const double rx = a.bx - a.ax, ry = a.by - a.ay;
    for (size_t j = i + 1; j < constraints.size(); ++j) {
      const TinConstraint& b = constraints[j];
      const double sx = b.bx - b.ax, sy = b.by - b.ay;
      // Standard vector line-intersection: t along a, u along b. r x s == 0 means parallel (or
      // collinear) — no single crossing point to report a Z conflict at.
      const double rxs = rx * sy - ry * sx;
      if (std::fabs(rxs) < 1e-12)
        continue;
      const double qx = b.ax - a.ax, qy = b.ay - a.ay;
      const double t = (qx * sy - qy * sx) / rxs;
      const double u = (qx * ry - qy * rx) / rxs;
      // Strict interior crossing only: two breaklines sharing an endpoint are not a conflict, they
      // are the ordinary case of a breakline chain.
      if (t <= 0.0 || t >= 1.0 || u <= 0.0 || u >= 1.0)
        continue;
      const float za = a.az + static_cast<float>(t) * (a.bz - a.az);
      const float zb = b.az + static_cast<float>(u) * (b.bz - b.az);
      if (std::fabs(za - zb) > static_cast<float>(kTinPlanEpsilon)) {
        TinCrossingIssue issue;
        issue.constraintIndexA = i;
        issue.constraintIndexB = j;
        issue.x = a.ax + t * rx;
        issue.y = a.ay + t * ry;
        issue.zFromA = za;
        issue.zFromB = zb;
        issues.push_back(issue);
      }
    }
  }
  return issues;
}

bool TinPointInPolygon(double px, double py, const std::vector<std::pair<double, double>>& ring) {
  if (ring.size() < 3)
    return false;
  bool inside = false;
  const size_t n = ring.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const double xi = ring[i].first, yi = ring[i].second;
    const double xj = ring[j].first, yj = ring[j].second;
    if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
      inside = !inside;
  }
  return inside;
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

TinBuildResult BuildTin(const std::vector<TinInputPoint>& points, const std::vector<TinConstraint>& constraints) {
  TinBuildResult r;

  // --- 0. Fold constraint vertices into the point set --------------------------------------------
  // A breakline/boundary vertex is not required to already be one of \p points (REQ-069): it is
  // inserted as an ordinary point, carrying its own elevation, exactly like a survey shot. Points
  // come first so an existing survey point wins a tie against a coincident constraint vertex — the
  // same "first occurrence wins" rule \ref duplicatesDropped already documents, just extended to a
  // second source of points.
  std::vector<TinInputPoint> pts = points;
  pts.reserve(points.size() + constraints.size() * 2);
  for (const TinConstraint& c : constraints) {
    pts.push_back({c.ax, c.ay, c.az});
    pts.push_back({c.bx, c.by, c.bz});
  }

  // --- 1. De-duplicate plan positions -----------------------------------------------------------
  // Delaunay is undefined for coincident sites: they yield degenerate triangles or a flip loop that
  // never ends. Sorting by (x,y) groups candidates so the check is O(n log n) rather than O(n²).
  // A stable sort keeps ties in their original (points-then-constraints) order, which is what makes
  // "first occurrence wins" mean what it says rather than an arbitrary sort-order artifact.
  std::stable_sort(pts.begin(), pts.end(), [](const TinInputPoint& p, const TinInputPoint& q) {
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
        // A duplicate is expected (two shots of the same corner, a breakline vertex sitting on a
        // survey point); a duplicate whose elevation actually DISAGREES is a data conflict and is
        // counted separately so it can be reported rather than silently resolved by "first wins"
        // (REQ-069, REQ-201).
        if (std::fabs(static_cast<double>(p.z) - static_cast<double>(uniq[k].z)) > kTinPlanEpsilon)
          ++r.conflictingDuplicates;
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

  // --- 5.5 Expose constraint edges (breaklines, boundary rings) — REQ-069 ------------------------
  // Flip-based insertion (Anglada/Sloan): for each constraint edge not already present, repeatedly
  // flip whichever crossing triangle-edge currently forms a convex quad, until the constraint edge
  // itself appears. A crossing edge that is not yet flippable is simply revisited on the next pass —
  // other flips nearby make it flippable, the standard termination argument for this method. Runs
  // are bounded so a pathological input is reported unresolved rather than looping forever.
  //
  // The crossing edges are found by WALKING the corridor the segment cuts, through the adjacency
  // links, and every lookup goes through a vertex→triangle index. Both used to be passes over the
  // whole triangle array, which made enforcing one edge cost O(triangles) per flip inside a budget
  // that was itself 4x the triangle count: a constraint that could not be enforced took about four
  // seconds on a 20k-triangle surface, and a surface rebuilds on every drawing edit. Cost is now
  // proportional to the ground the breakline crosses rather than to the size of the surface.
  if (!constraints.empty()) {
    // uniq is sorted by (x,y) from step 1; every constraint endpoint was folded into pts in step 0,
    // so this recovers WHICH uniq index it landed at rather than searching for something that might
    // be absent.
    auto findUniqIndex = [&](double x, double y) -> std::uint32_t {
      auto it = std::lower_bound(uniq.begin(), uniq.end(), x - kTinPlanEpsilon,
                                 [](const TinInputPoint& p, double v) { return p.x < v; });
      for (; it != uniq.end() && it->x - x <= kTinPlanEpsilon; ++it)
        if (std::fabs(it->y - y) <= kTinPlanEpsilon)
          return static_cast<std::uint32_t>(it - uniq.begin());
      return kNone;
    };
    // One incident triangle per vertex. Everything below is then local — an edge lookup and the
    // corridor walk both cost O(vertex degree) rather than a pass over every triangle, which is what
    // keeps enforcing a breakline proportional to the ground it crosses instead of to the size of
    // the surface. Built here rather than maintained through step 5, which kills and creates
    // triangles freely; from this point on only flips happen, and each one repairs its four entries.
    std::vector<std::uint32_t> vertTri(static_cast<size_t>(nPts) + 3, kNone);
    for (std::uint32_t ti = 0; ti < tris.size(); ++ti) {
      if (!tris[ti].alive)
        continue;
      for (int k = 0; k < 3; ++k)
        vertTri[tris[ti].v[k]] = ti;
    }

    // Visits every triangle incident to \p v, passing (slot, index of v within it), stopping early
    // when \p f returns true. Crossing edge `i` — the one from v to the next vertex — pivots to the
    // next triangle around v. Real vertices are strictly inside the super-triangle, so the cycle
    // always closes rather than running off a hull edge; the guard is for a corrupt adjacency only.
    auto forEachAround = [&](std::uint32_t v, auto&& f) {
      const std::uint32_t start = vertTri[v];
      if (start == kNone)
        return;
      std::uint32_t s = start;
      for (int guard = 0; guard < 4096; ++guard) {
        int i = -1;
        for (int k = 0; k < 3; ++k)
          if (tris[s].v[k] == v) {
            i = k;
            break;
          }
        if (i < 0)
          return;  // stale entry — defensive, should not happen
        if (f(s, i))
          return;
        const std::uint32_t nxt = tris[s].n[i];
        if (nxt == kNone)
          return;
        s = nxt;
        if (s == start)
          return;  // full cycle
      }
    };

    auto edgeExists = [&](std::uint32_t a, std::uint32_t b) {
      bool found = false;
      forEachAround(a, [&](std::uint32_t s, int i) {
        if (tris[s].v[(i + 1) % 3] == b || tris[s].v[(i + 2) % 3] == b) {
          found = true;
          return true;
        }
        return false;
      });
      return found;
    };

    // Strict interior crossing of segment (p,q) against segment (u,v), by vertex index.
    auto segmentsCross = [&](std::uint32_t p, std::uint32_t q, std::uint32_t u, std::uint32_t w) {
      const double rx = xs[q] - xs[p], ry = ys[q] - ys[p];
      const double sx = xs[w] - xs[u], sy = ys[w] - ys[u];
      const double rxs = rx * sy - ry * sx;
      if (std::fabs(rxs) < 1e-12)
        return false;
      const double qx = xs[u] - xs[p], qy = ys[u] - ys[p];
      const double tt = (qx * sy - qy * sx) / rxs;
      const double uu = (qx * ry - qy * rx) / rxs;
      return tt > 0.0 && tt < 1.0 && uu > 0.0 && uu < 1.0;
    };
    // Sets tris[otherSlot]'s neighbour across the edge (fromB,fromA) — the SAME physical edge as
    // (fromA,fromB) seen from the far side, by adjacency symmetry — to \p toSlot. No-op at the hull.
    auto relink = [&](std::uint32_t otherSlot, std::uint32_t fromA, std::uint32_t fromB,
                      std::uint32_t toSlot) {
      if (otherSlot == kNone)
        return;
      Tri& o = tris[otherSlot];
      for (int k = 0; k < 3; ++k) {
        if (o.v[k] == fromB && o.v[(k + 1) % 3] == fromA) {
          o.n[k] = toSlot;
          return;
        }
      }
    };

    // Locates the triangle holding (u,v) as a DIRECTED edge: v[k] == u and v[k+1] == v.
    auto findDirectedEdge = [&](std::uint32_t u, std::uint32_t v, std::uint32_t* slotOut, int* kOut) {
      bool found = false;
      forEachAround(u, [&](std::uint32_t s, int i) {
        if (tris[s].v[(i + 1) % 3] == v) {
          *slotOut = s;
          *kOut = i;
          found = true;
          return true;
        }
        return false;
      });
      return found;
    };

    // The edges the segment (ia,ib) crosses, in order from ia, as VERTEX PAIRS — a (slot, edge
    // index) does not survive a flip, and these have to. Walks the corridor by adjacency: find the
    // triangle at ia the segment leaves through, then step across each crossed edge until the far
    // endpoint is reached. Cost is the number of crossings, not the number of triangles.
    //
    // Part E's split guarantees no vertex lies strictly on the segment, which is what makes the walk
    // unambiguous: it never has to decide what to do about passing exactly through a vertex.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> corridor;
    auto collectCorridor = [&](std::uint32_t ia, std::uint32_t ib) -> bool {
      corridor.clear();
      std::uint32_t slot = kNone;
      int kEdge = -1;
      // The segment leaves ia through the triangle whose two other vertices straddle its direction.
      forEachAround(ia, [&](std::uint32_t s, int i) {
        const std::uint32_t b = tris[s].v[(i + 1) % 3], c = tris[s].v[(i + 2) % 3];
        if (TinOrient2D(xs[ia], ys[ia], xs[b], ys[b], xs[ib], ys[ib]) > 0.0 &&
            TinOrient2D(xs[ia], ys[ia], xs[c], ys[c], xs[ib], ys[ib]) < 0.0) {
          slot = s;
          kEdge = (i + 1) % 3;  // the far edge (b,c)
          return true;
        }
        return false;
      });
      if (slot == kNone || kEdge < 0)
        return false;

      for (int guard = 0; guard < 1 << 20; ++guard) {
        const std::uint32_t b = tris[slot].v[kEdge], c = tris[slot].v[(kEdge + 1) % 3];
        corridor.push_back({b, c});
        const std::uint32_t nxt = tris[slot].n[kEdge];
        if (nxt == kNone)
          return false;  // ran off the hull: impossible between two interior vertices
        int j = -1;
        for (int k = 0; k < 3; ++k)
          if (tris[nxt].v[k] == c && tris[nxt].v[(k + 1) % 3] == b) {
            j = k;
            break;
          }
        if (j < 0)
          return false;  // adjacency inconsistency — defensive, should not happen
        const std::uint32_t z = tris[nxt].v[(j + 2) % 3];
        if (z == ib)
          return true;  // the far endpoint closes the corridor
        // Of the two remaining edges, the segment leaves through whichever it actually crosses.
        slot = nxt;
        kEdge = segmentsCross(ia, ib, b, z) ? (j + 1) % 3 : (j + 2) % 3;
      }
      return false;
    };

    // Exposes ONE edge (\p ia,\p ib) by flipping, and returns whether it ended up present.
    //
    // Sloan's queue: take a crossing edge; if its quad is convex, flip it and put the resulting
    // diagonal back only when that still crosses the segment; if it is not convex, put it back to
    // retry once neighbouring flips have opened it up. The queue only ever loses edges that stopped
    // crossing, which is what makes progress monotone.
    //
    // Re-deriving the corridor after every flip instead — the obvious approach, and the first one
    // written here — LIVELOCKS. Measured: with a five-million-flip budget the same two constraints
    // stayed unresolved and the fixture took 750 ms, because a fresh walk keeps re-proposing the
    // edge that was just flipped back. Ordering has to persist across flips, so it lives in a queue
    // rather than being recomputed.
    auto enforceEdge = [&](std::uint32_t ia, std::uint32_t ib) -> bool {
      if (edgeExists(ia, ib))
        return true;
      if (!collectCorridor(ia, ib) || corridor.empty())
        return false;

      std::deque<std::pair<std::uint32_t, std::uint32_t>> pending(corridor.begin(), corridor.end());
      // Bounded by the corridor, not by the mesh. The old bound was 4x the triangle COUNT with an
      // O(triangles) scan inside it, which is what cost about four seconds per unenforceable
      // constraint on a 20k-triangle surface.
      const long long k0 = static_cast<long long>(pending.size());
      const long long limit = std::min(64 + 16 * k0 * k0, 2000000LL);

      for (long long guard = 0; !pending.empty(); ++guard) {
        if (guard > limit)
          return false;
        const std::uint32_t u = pending.front().first, v = pending.front().second;
        pending.pop_front();
        if (u == ia || u == ib || v == ia || v == ib)
          continue;  // an edge touching either endpoint cannot "cross" the segment
        if (!segmentsCross(ia, ib, u, v))
          continue;  // an earlier flip already cleared this one out of the way

        std::uint32_t ti = kNone;
        int k = -1;
        if (!findDirectedEdge(u, v, &ti, &k))
          continue;  // the edge is gone — nothing to flip
        {
          const std::uint32_t p = tris[ti].v[(k + 2) % 3];
          const std::uint32_t otherSlot = tris[ti].n[k];
          if (otherSlot == kNone)
            continue;  // a hull edge cannot cross a segment between two interior points
          int k2 = -1;
          for (int j = 0; j < 3; ++j)
            if (tris[otherSlot].v[j] == v && tris[otherSlot].v[(j + 1) % 3] == u) { k2 = j; break; }
          if (k2 < 0)
            continue;  // adjacency inconsistency — defensive, should not happen
          const std::uint32_t q = tris[otherSlot].v[(k2 + 2) % 3];

          // Flip valid only if both new triangles would be non-inverted: exactly the condition
          // that the quad (u,q,v,p) is convex, expressed as the orientation of the two halves the
          // new diagonal p-q would cut it into. Not convex yet — retry after its neighbours move.
          if (!(TinOrient2D(xs[u], ys[u], xs[q], ys[q], xs[p], ys[p]) > 0.0 &&
                TinOrient2D(xs[q], ys[q], xs[v], ys[v], xs[p], ys[p]) > 0.0)) {
            pending.push_back({u, v});
            continue;
          }

          // Neighbours outside the quad, captured before the two slots are overwritten below.
          const std::uint32_t nUQ = tris[otherSlot].n[(k2 + 1) % 3];  // edge u->q, in t2
          const std::uint32_t nPU = tris[ti].n[(k + 2) % 3];          // edge p->u, in t1
          const std::uint32_t nQV = tris[otherSlot].n[(k2 + 2) % 3];  // edge q->v, in t2
          const std::uint32_t nVP = tris[ti].n[(k + 1) % 3];          // edge v->p, in t1

          Tri& t1 = tris[ti];
          Tri& t2 = tris[otherSlot];
          t1.v[0] = u; t1.v[1] = q; t1.v[2] = p;
          t1.n[0] = nUQ; t1.n[1] = otherSlot; t1.n[2] = nPU;
          t2.v[0] = q; t2.v[1] = v; t2.v[2] = p;
          t2.n[0] = nQV; t2.n[1] = nVP; t2.n[2] = ti;

          relink(nUQ, u, q, ti);
          relink(nPU, p, u, ti);
          relink(nQV, q, v, otherSlot);
          relink(nVP, v, p, otherSlot);

          // Repair the four vertices whose incident-triangle entry the flip could have
          // invalidated: u left t2 and v left t1, so an entry pointing at the slot it left would
          // send forEachAround into a triangle that no longer contains it. p and q are in both.
          vertTri[u] = ti;
          vertTri[p] = ti;
          vertTri[q] = ti;
          vertTri[v] = otherSlot;

          // The new diagonal takes the old edge's place in the queue only while it still stands
          // between the endpoints; otherwise this crossing is simply gone.
          if (segmentsCross(ia, ib, p, q))
            pending.push_back({p, q});
        }
      }
      return edgeExists(ia, ib);
    };

    // Vertices lying ON the current constraint, paired with their parameter along it. Declared out
    // here so the allocation is reused across constraints rather than made per segment.
    std::vector<std::pair<double, std::uint32_t>> onSegment;
    // Each constraint's chain of vertices, kept so the finished mesh can be checked against every
    // one of them after all the flipping has settled.
    std::vector<std::vector<std::uint32_t>> chains;
    chains.reserve(constraints.size());

    for (const TinConstraint& c : constraints) {
      const std::uint32_t ia = findUniqIndex(c.ax, c.ay);
      const std::uint32_t ib = findUniqIndex(c.bx, c.by);
      if (ia == kNone || ib == kNone || ia == ib)
        continue;  // zero-length, or (defensively) unresolved — step 0 guarantees resolution normally

      // --- Split the constraint at every vertex lying on it ---------------------------------------
      // No triangulation can contain an edge that passes THROUGH a third vertex, so a constraint
      // spanning one is not a single edge and asking for it as one can never succeed. It is honoured
      // as the CHAIN of edges between consecutive vertices along it instead — geometrically the same
      // breakline. Without this split the loop below could not terminate on edgeExists() and every
      // such constraint was counted unresolved, which is the ordinary surveying case: a breakline
      // drawn along a ridge THROUGH the shots that define it. Measured before this change, a
      // breakline (0,0)-(10,0) with a shot at (5,0) reported 1 unresolved, and two breaklines
      // crossing at a shared grid vertex reported 2 — while both were in fact fully present in the
      // mesh, edge for edge.
      //
      // "On" is kTinPlanEpsilon perpendicular, the same tolerance that decides two points are the
      // same site: a vertex the rest of the system cannot distinguish from the segment must not be
      // treated as off it here, or the flip loop chases an edge through a vertex it cannot cross.
      onSegment.clear();
      const double ax = xs[ia], ay = ys[ia];
      const double dx = xs[ib] - ax, dy = ys[ib] - ay;
      const double len2 = dx * dx + dy * dy;
      if (len2 > 0.0) {
        const double len = std::sqrt(len2);
        for (std::uint32_t k = 0; k < nPts; ++k) {  // real vertices only — the super-triangle is not on anything
          if (k == ia || k == ib)
            continue;
          const double px = xs[k] - ax, py = ys[k] - ay;
          const double t = (px * dx + py * dy) / len2;
          if (t <= 0.0 || t >= 1.0)
            continue;  // beyond an endpoint: on the line perhaps, but not between them
          if (std::fabs(px * dy - py * dx) / len <= kTinPlanEpsilon)
            onSegment.push_back({t, k});
        }
        std::sort(onSegment.begin(), onSegment.end());
      }

      // Enforce every link of the chain, and keep going after a failure so one bad link does not
      // silently abandon the rest of the breakline. The chain is kept for the verification pass
      // below rather than the outcome being trusted here — see there for why.
      std::vector<std::uint32_t>& chain = chains.emplace_back();
      chain.push_back(ia);
      for (const std::pair<double, std::uint32_t>& hit : onSegment) {
        if (hit.second == chain.back())
          continue;
        enforceEdge(chain.back(), hit.second);
        chain.push_back(hit.second);
      }
      if (chain.back() != ib) {
        enforceEdge(chain.back(), ib);
        chain.push_back(ib);
      }
    }

    // --- Count what is actually missing from the FINISHED mesh ------------------------------------
    // Not what each insertion reported at the time it ran. Enforcing a later constraint can flip an
    // earlier one away — two breaklines that genuinely cross cannot both be edges, and whichever is
    // inserted second wins. Trusting the insertion-time outcome therefore let a breakline vanish
    // with nothing reported at all: measured on two breaklines crossing at a non-vertex with equal
    // elevations, where TinFindCrossingConflicts stays silent by design because the elevations
    // agree. Checking the finished mesh is the only count that matches what the message claims.
    for (const std::vector<std::uint32_t>& chain : chains) {
      for (size_t i = 0; i + 1 < chain.size(); ++i) {
        if (!edgeExists(chain[i], chain[i + 1])) {
          ++r.constraintsUnresolved;
          break;  // one constraint, counted once, however many of its links are missing
        }
      }
    }
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
  if (r.duplicatesDropped > 0) {
    r.message = "Dropped " + std::to_string(r.duplicatesDropped) +
                " point(s) sharing a plan position with an earlier point (first occurrence kept).";
    if (r.conflictingDuplicates > 0)
      r.message += " " + std::to_string(r.conflictingDuplicates) +
                  " of those disagreed on elevation by more than the tolerance.";
  }
  if (r.constraintsUnresolved > 0)
    r.message += (r.message.empty() ? "" : " ") + std::to_string(r.constraintsUnresolved) +
                " constraint edge(s) could not be enforced.";
  return r;
}

/// One triangle's half of `TinElevationAt`: is (\p x, \p y) inside triangle (\p ia, \p ib, \p ic), and
/// if so, what is the plane's elevation there. Extracted so a second caller — REQ-073's
/// spatial-indexed volume query (`util/surfacevolume.cpp`) — tests the SAME containment/plane-eval
/// rule a full linear scan does, rather than a second copy that could drift from it (CLAUDE.md rule
/// 2: two present-day uses justify sharing).
bool TinTriangleElevationAt(const std::vector<float>& vertsXyz, std::uint32_t ia, std::uint32_t ib,
                            std::uint32_t ic, double x, double y, double* outZ) {
  const std::uint32_t vertexCount = static_cast<std::uint32_t>(vertsXyz.size() / 3);
  // A corrupt index would read past the vertex array; a surface loaded from a file is not
  // necessarily one we built (GsIo rejects these at load, and this is the second line of defence).
  if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount)
    return false;

  const double ax = vertsXyz[ia * 3], ay = vertsXyz[ia * 3 + 1], az = vertsXyz[ia * 3 + 2];
  const double bx = vertsXyz[ib * 3], by = vertsXyz[ib * 3 + 1], bz = vertsXyz[ib * 3 + 2];
  const double cx = vertsXyz[ic * 3], cy = vertsXyz[ic * 3 + 1], cz = vertsXyz[ic * 3 + 2];

  // Barycentric containment via the same orientation predicate the triangulation is built on, so
  // "inside a triangle" means here exactly what it meant when the triangle was made. BuildTin
  // emits counter-clockwise triangles, so all three are >= 0 for a point inside or on an edge —
  // but both signs are accepted, because a surface read from a file was not necessarily written
  // by us and a clockwise triangle should read its elevation, not report a hole.
  const double wA = TinOrient2D(bx, by, cx, cy, x, y);
  const double wB = TinOrient2D(cx, cy, ax, ay, x, y);
  const double wC = TinOrient2D(ax, ay, bx, by, x, y);
  const bool inside = (wA >= 0.0 && wB >= 0.0 && wC >= 0.0) || (wA <= 0.0 && wB <= 0.0 && wC <= 0.0);
  if (!inside)
    return false;

  // Twice the signed area. Zero means a degenerate (collinear) triangle: it covers no area, so
  // there is no plane to evaluate and dividing by it would produce an infinity that looks like an
  // elevation. Skip it and let a real triangle answer.
  const double area2 = wA + wB + wC;
  if (area2 == 0.0)
    return false;

  *outZ = (wA * az + wB * bz + wC * cz) / area2;
  return true;
}

bool TinElevationAt(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices, double x,
                    double y, double* outZ) {
  if (!outZ || indices.size() < 3 || vertsXyz.size() < 9)
    return false;

  for (size_t t = 0; t + 2 < indices.size(); t += 3) {
    if (TinTriangleElevationAt(vertsXyz, indices[t], indices[t + 1], indices[t + 2], x, y, outZ))
      return true;
  }
  return false;
}

void TinCullByBoundaries(std::vector<std::uint32_t>& indices, const std::vector<float>& vertsXyz,
                         const std::vector<TinBoundaryLoop>& loops) {
  if (loops.empty() || indices.size() < 3)
    return;
  const size_t triCount = indices.size() / 3;
  const std::uint32_t vertexCount = static_cast<std::uint32_t>(vertsXyz.size() / 3);

  // TWO independent masks, not one. An Outer ring defines the surface's EXTENT and only ever clips
  // down (multiple Outer loops intersect); Hide/Show toggle VOIDS within that extent, in definition
  // order. Sharing a single mask let a Show ring restore triangles an Outer ring had clipped —
  // pulling surface back from outside the surface's own boundary — because "restore" could not tell
  // which of the two had removed a triangle. Keeping them apart is what makes Show's documented
  // contract ("only has a visible effect where an earlier Hide removed them") literally true.
  //
  // Both start present, matching BuildTin's own convex-hull-only surface: "no boundary" is "fully
  // present".
  std::vector<char> insideOuter(triCount, 1);
  std::vector<char> insideAnyClip(triCount, 0);
  int clipLoopCount = 0;
  std::vector<char> shown(triCount, 1);

  // Hide/Show are applied strictly in \p loops order, each mutating the CURRENT void state — never
  // recomputed from scratch — which is what makes "a show boundary inside a hide restores surface
  // there" and "boundaries apply in definition order" both literally true rather than approximated.
  // Outer needs no ordering: intersection is commutative.
  for (const TinBoundaryLoop& loop : loops) {
    if (loop.ring.size() < 3)
      continue;  // degenerate ring: not enough vertices to enclose anything, skip rather than guess
    if (loop.kind == TinBoundaryKind::Clip)
      ++clipLoopCount;
    for (size_t t = 0; t < triCount; ++t) {
      const std::uint32_t a = indices[t * 3], b = indices[t * 3 + 1], c = indices[t * 3 + 2];
      if (a >= vertexCount || b >= vertexCount || c >= vertexCount)
        continue;  // corrupt index (e.g. a hand-edited .gs) — leave it as it was, do not read past the array
      const double cx = (static_cast<double>(vertsXyz[a * 3]) + vertsXyz[b * 3] + vertsXyz[c * 3]) / 3.0;
      const double cy =
          (static_cast<double>(vertsXyz[a * 3 + 1]) + vertsXyz[b * 3 + 1] + vertsXyz[c * 3 + 1]) / 3.0;
      const bool in = TinPointInPolygon(cx, cy, loop.ring);
      switch (loop.kind) {
      case TinBoundaryKind::Outer:
        if (!in)
          insideOuter[t] = 0;  // only ever clips down; multiple Outer loops intersect
        break;
      case TinBoundaryKind::Clip:
        if (in)
          insideAnyClip[t] = 1;
        break;
      case TinBoundaryKind::Hide:
      case TinBoundaryKind::Mask:
        if (in)
          shown[t] = 0;
        break;
      case TinBoundaryKind::Show:
        if (in)
          shown[t] = 1;  // undoes an earlier Hide only — never an Outer clip (see the masks above)
        break;
      }
    }
  }

  std::vector<std::uint32_t> kept;
  kept.reserve(indices.size());
  for (size_t t = 0; t < triCount; ++t) {
    const bool clipOk = clipLoopCount == 0 || insideAnyClip[t];
    if (insideOuter[t] && clipOk && shown[t]) {
      kept.push_back(indices[t * 3]);
      kept.push_back(indices[t * 3 + 1]);
      kept.push_back(indices[t * 3 + 2]);
    }
  }
  indices = std::move(kept);
}

void TinBorderEdges(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices,
                    std::vector<float>* out) {
  if (!out)
    return;
  out->clear();

  // Each undirected edge, keyed by its two vertex indices in ascending order — so the same edge
  // reached from either of its two triangles produces the same key. Sorting and scanning for
  // adjacent duplicates costs one sort instead of a hash map, and keeps the module dependency-free
  // (architecture §11.4), which is the whole reason this file includes nothing but <algorithm>,
  // <cmath> and <deque>.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
  edges.reserve(indices.size());
  const auto addEdge = [&](std::uint32_t a, std::uint32_t b) {
    edges.emplace_back(std::min(a, b), std::max(a, b));
  };
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    addEdge(indices[i], indices[i + 1]);
    addEdge(indices[i + 1], indices[i + 2]);
    addEdge(indices[i + 2], indices[i]);
  }
  std::sort(edges.begin(), edges.end());

  const size_t vcount = vertsXyz.size() / 3;
  for (size_t i = 0; i < edges.size();) {
    // How many triangles share this edge. A well-formed triangulation gives 1 (border) or 2
    // (interior); a run of 3+ means a degenerate input, and treating it as interior is deliberate —
    // an edge that many faces meet at is not an outline, and drawing it as one would present a
    // triangulation defect as a border the user might try to edit.
    size_t j = i + 1;
    while (j < edges.size() && edges[j] == edges[i])
      ++j;
    if (j - i == 1) {
      const size_t a = static_cast<size_t>(edges[i].first);
      const size_t b = static_cast<size_t>(edges[i].second);
      if (a < vcount && b < vcount) {
        out->push_back(vertsXyz[a * 3]);
        out->push_back(vertsXyz[a * 3 + 1]);
        out->push_back(vertsXyz[a * 3 + 2]);
        out->push_back(vertsXyz[b * 3]);
        out->push_back(vertsXyz[b * 3 + 1]);
        out->push_back(vertsXyz[b * 3 + 2]);
      }
    }
    i = j;
  }
}

namespace {

[[nodiscard]] double DistPointSeg2(double px, double py, double ax, double ay, double bx, double by) {
  const double vx = bx - ax, vy = by - ay;
  const double wx = px - ax, wy = py - ay;
  const double c1 = vx * wx + vy * wy;
  if (c1 <= 0.0)
    return wx * wx + wy * wy;
  const double c2 = vx * vx + vy * vy;
  if (c2 <= c1) {
    const double dx = px - bx, dy = py - by;
    return dx * dx + dy * dy;
  }
  const double t = c1 / c2;
  const double dx = px - (ax + t * vx);
  const double dy = py - (ay + t * vy);
  return dx * dx + dy * dy;
}

[[nodiscard]] std::uint32_t TriangleOpposite(const std::uint32_t* tri, std::uint32_t lo, std::uint32_t hi) {
  for (int k = 0; k < 3; ++k) {
    const std::uint32_t v = tri[k];
    if (v != lo && v != hi)
      return v;
  }
  return lo;
}

[[nodiscard]] bool FindNearestInteriorEdge(const std::vector<float>& vertsXyz,
                                           const std::vector<std::uint32_t>& indices, double x, double y,
                                           int* t0, int* t1, std::uint32_t* lo, std::uint32_t* hi) {
  if (!t0 || !t1 || !lo || !hi)
    return false;
  const int nTri = static_cast<int>(indices.size() / 3);
  if (nTri < 2 || vertsXyz.size() < 12)
    return false;

  struct Rec {
    std::uint32_t lo = 0, hi = 0;
    int tri = 0;
  };
  std::vector<Rec> recs;
  recs.reserve(static_cast<size_t>(nTri) * 3);
  for (int t = 0; t < nTri; ++t) {
    const std::uint32_t a = indices[static_cast<size_t>(t) * 3 + 0];
    const std::uint32_t b = indices[static_cast<size_t>(t) * 3 + 1];
    const std::uint32_t c = indices[static_cast<size_t>(t) * 3 + 2];
    const auto add = [&](std::uint32_t u, std::uint32_t v) {
      Rec r;
      r.lo = std::min(u, v);
      r.hi = std::max(u, v);
      r.tri = t;
      recs.push_back(r);
    };
    add(a, b);
    add(b, c);
    add(c, a);
  }
  std::sort(recs.begin(), recs.end(), [](const Rec& p, const Rec& q) {
    if (p.lo != q.lo)
      return p.lo < q.lo;
    if (p.hi != q.hi)
      return p.hi < q.hi;
    return p.tri < q.tri;
  });

  int bestT0 = -1, bestT1 = -1;
  std::uint32_t bestLo = 0, bestHi = 0;
  double bestD2 = 1.0e300;
  for (size_t i = 0; i < recs.size();) {
    size_t j = i + 1;
    while (j < recs.size() && recs[j].lo == recs[i].lo && recs[j].hi == recs[i].hi)
      ++j;
    if (j == i + 2) {
      const std::uint32_t elo = recs[i].lo, ehi = recs[i].hi;
      if (elo * 3 + 2 < vertsXyz.size() && ehi * 3 + 2 < vertsXyz.size()) {
        const double ax = vertsXyz[elo * 3], ay = vertsXyz[elo * 3 + 1];
        const double bx = vertsXyz[ehi * 3], by = vertsXyz[ehi * 3 + 1];
        const double d2 = DistPointSeg2(x, y, ax, ay, bx, by);
        if (d2 < bestD2) {
          bestD2 = d2;
          bestT0 = recs[i].tri;
          bestT1 = recs[i + 1].tri;
          bestLo = elo;
          bestHi = ehi;
        }
      }
    }
    i = j;
  }
  if (bestT0 < 0 || bestD2 > 1.0)
    return false;
  *t0 = bestT0;
  *t1 = bestT1;
  *lo = bestLo;
  *hi = bestHi;
  return true;
}

}  // namespace

bool TinSwapInteriorEdgeNear(const std::vector<float>& vertsXyz, std::vector<std::uint32_t>& indices, double x,
                             double y) {
  int bestT0 = -1, bestT1 = -1;
  std::uint32_t bestLo = 0, bestHi = 0;
  if (!FindNearestInteriorEdge(vertsXyz, indices, x, y, &bestT0, &bestT1, &bestLo, &bestHi))
    return false;

  std::uint32_t* t0 = &indices[static_cast<size_t>(bestT0) * 3];
  std::uint32_t* t1 = &indices[static_cast<size_t>(bestT1) * 3];
  const std::uint32_t b = TriangleOpposite(t0, bestLo, bestHi);
  const std::uint32_t d = TriangleOpposite(t1, bestLo, bestHi);
  if (b == d || b == bestLo || d == bestHi)
    return false;

  t0[0] = bestLo;
  t0[1] = b;
  t0[2] = d;
  t1[0] = b;
  t1[1] = bestHi;
  t1[2] = d;
  return true;
}

bool TinDeleteInteriorEdgeNear(std::vector<std::uint32_t>& indices, const std::vector<float>& vertsXyz, double x,
                               double y) {
  int bestT0 = -1, bestT1 = -1;
  std::uint32_t bestLo = 0, bestHi = 0;
  if (!FindNearestInteriorEdge(vertsXyz, indices, x, y, &bestT0, &bestT1, &bestLo, &bestHi))
    return false;
  int hi = std::max(bestT0, bestT1);
  int lo = std::min(bestT0, bestT1);
  if (hi < 0 || static_cast<size_t>(hi) * 3 + 2 >= indices.size())
    return false;
  indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(hi) * 3,
                indices.begin() + static_cast<std::ptrdiff_t>(hi) * 3 + 3);
  indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(lo) * 3,
                indices.begin() + static_cast<std::ptrdiff_t>(lo) * 3 + 3);
  (void)bestLo;
  (void)bestHi;
  return true;
}
