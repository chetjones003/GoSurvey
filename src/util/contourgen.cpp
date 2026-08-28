#include "contourgen.hpp"

#include <algorithm>
#include <cmath>

namespace {

/// An undirected triangle edge as one sortable key: its two vertex indices, low half first. Two
/// triangles sharing an edge produce the same key from either side, which is the whole basis of
/// chaining here — see the header on why the join is topological rather than geometric.
std::uint64_t EdgeKey(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t lo = a < b ? a : b;
  const std::uint32_t hi = a < b ? b : a;
  return (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);
}

/// One end of a contour segment: where the level crosses, and which triangle edge it crosses on.
struct Crossing {
  std::uint64_t edge = 0;
  double x = 0.0, y = 0.0, z = 0.0;
};

/// The piece of one contour level lying inside one triangle. Two ends, always on two different
/// edges of that triangle.
struct Segment {
  Crossing a, b;
};

/// Orders and searches the incidence list by edge key alone, ignoring the segment index paired with
/// it, so `equal_range` yields every segment end sitting on a given edge.
struct ByEdge {
  bool operator()(const std::pair<std::uint64_t, int>& p, std::uint64_t e) const { return p.first < e; }
  bool operator()(std::uint64_t e, const std::pair<std::uint64_t, int>& p) const { return e < p.first; }
};

/// The end of \p s lying on \p edge.
const Crossing& EndOn(const Segment& s, std::uint64_t edge) {
  return s.a.edge == edge ? s.a : s.b;
}

} // namespace

void GenerateContours(const std::vector<float>& vertsXyz, const std::vector<std::uint32_t>& indices,
                      const std::vector<double>& levels, ContourResult* out) {
  if (!out)
    return;
  out->vertsXyz.clear();
  out->offsets.clear();
  out->levels.clear();
  out->closed.clear();
  if (indices.size() < 3 || levels.empty())
    return;

  // Normalised once, so the caller may pass its levels in any order and get the same contours in the
  // same order back — and so the per-triangle range search below can be a binary search.
  std::vector<double> sorted(levels);
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

  const size_t vcount = vertsXyz.size() / 3;

  // Segments are bucketed per level rather than accumulated in one list. Two contours at different
  // elevations can cross the same triangle edge, and chaining them together would weld two separate
  // contours into one — a diagonal streak across the surface, which is what a shared-edge join
  // without this separation produces.
  std::vector<std::vector<Segment>> byLevel(sorted.size());

  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const std::uint32_t iv[3] = {indices[i], indices[i + 1], indices[i + 2]};
    if (iv[0] >= vcount || iv[1] >= vcount || iv[2] >= vcount)
      continue;  // Defensive, exactly as TinBorderEdges is: a malformed index is skipped, not trusted.

    const double z[3] = {static_cast<double>(vertsXyz[iv[0] * 3 + 2]),
                         static_cast<double>(vertsXyz[iv[1] * 3 + 2]),
                         static_cast<double>(vertsXyz[iv[2] * 3 + 2])};
    const double zmin = std::min({z[0], z[1], z[2]});
    const double zmax = std::max({z[0], z[1], z[2]});

    // A level cuts this triangle exactly when zmin < level <= zmax — some vertex strictly below, some
    // at or above, the tie rule putting "equal" above. The asymmetry is the rule, not an off-by-one.
    const auto firstLevel = std::upper_bound(sorted.begin(), sorted.end(), zmin);
    const auto lastLevel = std::upper_bound(sorted.begin(), sorted.end(), zmax);

    for (auto it = firstLevel; it != lastLevel; ++it) {
      const double level = *it;

      const bool above[3] = {z[0] >= level, z[1] >= level, z[2] >= level};
      const int aboveCount = (above[0] ? 1 : 0) + (above[1] ? 1 : 0) + (above[2] ? 1 : 0);

      // The range search guarantees one or two vertices above, so exactly one vertex is alone on its
      // side of the level and the level crosses the two edges meeting at it. That is the whole of
      // marching triangles: no third case survives the tie rule.
      const int lone = aboveCount == 1 ? (above[0] ? 0 : (above[1] ? 1 : 2))
                                       : (!above[0] ? 0 : (!above[1] ? 1 : 2));
      const int other[2] = {(lone + 1) % 3, (lone + 2) % 3};

      // belowIdx is strictly below the level and aboveIdx at or above it, so the denominator is
      // strictly positive — the tie rule is what removes the division by zero, rather than an epsilon.
      const auto crossing = [&](int belowIdx, int aboveIdx) {
        const std::uint32_t vb = iv[belowIdx], va = iv[aboveIdx];
        Crossing c;
        c.edge = EdgeKey(vb, va);
        // A contour vertex sits at its level by definition, so the elevation is taken rather than
        // interpolated: interpolating it would land within rounding of the level instead of on it.
        c.z = level;
        const double t = (level - z[belowIdx]) / (z[aboveIdx] - z[belowIdx]);
        if (t >= 1.0) {
          // t reaches 1 only when the upper vertex's own Z equals the level. Snapping to that
          // vertex's coordinates rather than evaluating the lerp there makes every triangle meeting
          // at it produce the *identical* point, which is what keeps a contour through a vertex
          // closed (ASSUMPTION-1).
          c.x = static_cast<double>(vertsXyz[va * 3]);
          c.y = static_cast<double>(vertsXyz[va * 3 + 1]);
        } else {
          const double bx = static_cast<double>(vertsXyz[vb * 3]);
          const double by = static_cast<double>(vertsXyz[vb * 3 + 1]);
          const double ax = static_cast<double>(vertsXyz[va * 3]);
          const double ay = static_cast<double>(vertsXyz[va * 3 + 1]);
          c.x = bx + (ax - bx) * t;
          c.y = by + (ay - by) * t;
        }
        return c;
      };

      Segment seg;
      if (aboveCount == 1) {
        seg.a = crossing(other[0], lone);
        seg.b = crossing(other[1], lone);
      } else {
        seg.a = crossing(lone, other[0]);
        seg.b = crossing(lone, other[1]);
      }
      byLevel[static_cast<size_t>(it - sorted.begin())].push_back(seg);
    }
  }

  for (size_t li = 0; li < sorted.size(); ++li) {
    const std::vector<Segment>& segs = byLevel[li];
    if (segs.empty())
      continue;

    // Every segment end, keyed by the triangle edge it lies on, sorted so the ends meeting at an edge
    // are adjacent. A sorted vector and a binary search rather than a hash map: it keeps the module
    // dependency-free (architecture §11.4) and matches TinBorderEdges next door.
    std::vector<std::pair<std::uint64_t, int>> incidence;
    incidence.reserve(segs.size() * 2);
    for (int s = 0; s < static_cast<int>(segs.size()); ++s) {
      incidence.emplace_back(segs[static_cast<size_t>(s)].a.edge, s);
      incidence.emplace_back(segs[static_cast<size_t>(s)].b.edge, s);
    }
    std::sort(incidence.begin(), incidence.end());

    const auto endsOn = [&](std::uint64_t edge) {
      return std::equal_range(incidence.begin(), incidence.end(), edge, ByEdge{});
    };

    std::vector<std::uint8_t> visited(segs.size(), 0);

    const auto emit = [&](const Crossing& c) {
      out->vertsXyz.push_back(static_cast<float>(c.x));
      out->vertsXyz.push_back(static_cast<float>(c.y));
      out->vertsXyz.push_back(static_cast<float>(c.z));
    };

    // Follows one contour from startSeg, entered at startEdge, until it closes or runs out.
    const auto walk = [&](int startSeg, std::uint64_t startEdge) {
      const int firstVertex = static_cast<int>(out->vertsXyz.size() / 3);
      if (out->offsets.empty())
        out->offsets.push_back(firstVertex);

      visited[static_cast<size_t>(startSeg)] = 1;
      int s = startSeg;
      std::uint64_t enter = startEdge;
      emit(EndOn(segs[static_cast<size_t>(s)], enter));

      bool isClosed = false;
      while (true) {
        const Segment& cur = segs[static_cast<size_t>(s)];
        const std::uint64_t exit = cur.a.edge == enter ? cur.b.edge : cur.a.edge;

        int next = -1;
        const auto ends = endsOn(exit);
        for (auto it = ends.first; it != ends.second; ++it) {
          if (it->second != s && !visited[static_cast<size_t>(it->second)]) {
            next = it->second;
            break;
          }
        }

        if (next < 0) {
          // Arriving back at the edge the walk started from means the contour closed; the point
          // there is already the first vertex, so it is not emitted twice. Anywhere else means the
          // contour ran off the surface border or into a REQ-069 void, and that last point is real.
          if (exit == startEdge)
            isClosed = true;
          else
            emit(EndOn(cur, exit));
          break;
        }

        emit(EndOn(cur, exit));
        visited[static_cast<size_t>(next)] = 1;
        enter = exit;
        s = next;
      }

      out->offsets.push_back(static_cast<int>(out->vertsXyz.size() / 3));
      out->levels.push_back(sorted[li]);
      out->closed.push_back(isClosed ? 1 : 0);
    };

    // Open contours first, each from the end no second segment continues, so it is emitted from its
    // start rather than from wherever the middle happened to be reached. What is left after that is
    // closed by construction — every end of it has a continuation.
    for (int s = 0; s < static_cast<int>(segs.size()); ++s) {
      if (visited[static_cast<size_t>(s)])
        continue;
      const auto atA = endsOn(segs[static_cast<size_t>(s)].a.edge);
      const auto atB = endsOn(segs[static_cast<size_t>(s)].b.edge);
      if (atA.second - atA.first == 1)
        walk(s, segs[static_cast<size_t>(s)].a.edge);
      else if (atB.second - atB.first == 1)
        walk(s, segs[static_cast<size_t>(s)].b.edge);
    }
    for (int s = 0; s < static_cast<int>(segs.size()); ++s) {
      if (!visited[static_cast<size_t>(s)])
        walk(s, segs[static_cast<size_t>(s)].a.edge);
    }
  }
}

void ContourLevels(double minZ, double maxZ, double interval, std::vector<double>* out) {
  if (!out)
    return;
  out->clear();
  if (!std::isfinite(minZ) || !std::isfinite(maxZ) || !std::isfinite(interval) || interval <= 0.0 ||
      maxZ < minZ)
    return;

  // Measured from elevation zero, not from the surface's low point — see the header.
  const double firstStep = std::ceil(minZ / interval);
  const double lastStep = std::floor(maxZ / interval);
  if (firstStep > lastStep)
    return;

  // A hint, not a limit: bounding the interval against the surface's range is the caller's job
  // (REQ-070's rejection happens where the value was typed), so a wild interval must still produce
  // the levels it asks for rather than a quietly truncated list.
  const double count = lastStep - firstStep + 1.0;
  if (count < 1.0e6)
    out->reserve(static_cast<size_t>(count));

  for (double step = firstStep; step <= lastStep; step += 1.0)
    out->push_back(step * interval);
}

namespace {

void ChaikinPassOpen(const std::vector<float>& in, std::vector<float>* out) {
  out->clear();
  const int n = static_cast<int>(in.size() / 3);
  if (n < 2)
    return;
  auto push = [&](double x, double y, double z) {
    out->push_back(static_cast<float>(x));
    out->push_back(static_cast<float>(y));
    out->push_back(static_cast<float>(z));
  };
  push(in[0], in[1], in[2]);
  for (int i = 0; i + 1 < n; ++i) {
    const double ax = in[static_cast<size_t>(i) * 3 + 0];
    const double ay = in[static_cast<size_t>(i) * 3 + 1];
    const double az = in[static_cast<size_t>(i) * 3 + 2];
    const double bx = in[static_cast<size_t>(i + 1) * 3 + 0];
    const double by = in[static_cast<size_t>(i + 1) * 3 + 1];
    const double bz = in[static_cast<size_t>(i + 1) * 3 + 2];
    push(0.75 * ax + 0.25 * bx, 0.75 * ay + 0.25 * by, 0.75 * az + 0.25 * bz);
    push(0.25 * ax + 0.75 * bx, 0.25 * ay + 0.75 * by, 0.25 * az + 0.75 * bz);
  }
  push(in[static_cast<size_t>(n - 1) * 3 + 0], in[static_cast<size_t>(n - 1) * 3 + 1],
       in[static_cast<size_t>(n - 1) * 3 + 2]);
}

void ChaikinPassClosed(const std::vector<float>& in, std::vector<float>* out) {
  out->clear();
  const int n = static_cast<int>(in.size() / 3);
  if (n < 3)
    return;
  auto at = [&](int i, int c) {
    const int k = ((i % n) + n) % n;
    return static_cast<double>(in[static_cast<size_t>(k) * 3 + static_cast<size_t>(c)]);
  };
  auto push = [&](double x, double y, double z) {
    out->push_back(static_cast<float>(x));
    out->push_back(static_cast<float>(y));
    out->push_back(static_cast<float>(z));
  };
  for (int i = 0; i < n; ++i) {
    const double ax = at(i, 0), ay = at(i, 1), az = at(i, 2);
    const double bx = at(i + 1, 0), by = at(i + 1, 1), bz = at(i + 1, 2);
    push(0.75 * ax + 0.25 * bx, 0.75 * ay + 0.25 * by, 0.75 * az + 0.25 * bz);
    push(0.25 * ax + 0.75 * bx, 0.25 * ay + 0.75 * by, 0.25 * az + 0.75 * bz);
  }
}

}  // namespace

void SmoothContoursChaikin(ContourResult* io, int passes) {
  if (!io || passes <= 0)
    return;
  if (passes > 5)
    passes = 5;
  ContourResult next;
  next.offsets.push_back(0);
  for (int c = 0; c < io->contourCount(); ++c) {
    const int begin = io->offsets[static_cast<size_t>(c)];
    const int end = io->offsets[static_cast<size_t>(c) + 1];
    std::vector<float> poly;
    for (int v = begin; v < end; ++v) {
      poly.push_back(io->vertsXyz[static_cast<size_t>(v) * 3 + 0]);
      poly.push_back(io->vertsXyz[static_cast<size_t>(v) * 3 + 1]);
      poly.push_back(io->vertsXyz[static_cast<size_t>(v) * 3 + 2]);
    }
    std::vector<float> a = std::move(poly), b;
    for (int p = 0; p < passes; ++p) {
      if (c < static_cast<int>(io->closed.size()) && io->closed[static_cast<size_t>(c)] != 0)
        ChaikinPassClosed(a, &b);
      else
        ChaikinPassOpen(a, &b);
      a.swap(b);
    }
    next.vertsXyz.insert(next.vertsXyz.end(), a.begin(), a.end());
    next.offsets.push_back(static_cast<int>(next.vertsXyz.size() / 3));
    if (c < static_cast<int>(io->levels.size()))
      next.levels.push_back(io->levels[static_cast<size_t>(c)]);
    if (c < static_cast<int>(io->closed.size()))
      next.closed.push_back(io->closed[static_cast<size_t>(c)]);
  }
  *io = std::move(next);
}

void CollectContourLabels(const ContourResult& major, double spacingFt, std::vector<ContourLabelPoint>* out) {
  if (!out)
    return;
  out->clear();
  if (!(spacingFt > 0.0))
    return;
  for (int c = 0; c < major.contourCount(); ++c) {
    const int begin = major.offsets[static_cast<size_t>(c)];
    const int end = major.offsets[static_cast<size_t>(c) + 1];
    if (end - begin < 2)
      continue;
    double accum = 0.0;
    double nextAt = 0.0;
    const double level = c < static_cast<int>(major.levels.size()) ? major.levels[static_cast<size_t>(c)] : 0.0;
    for (int v = begin; v + 1 < end; ++v) {
      const double x0 = major.vertsXyz[static_cast<size_t>(v) * 3 + 0];
      const double y0 = major.vertsXyz[static_cast<size_t>(v) * 3 + 1];
      const double z0 = major.vertsXyz[static_cast<size_t>(v) * 3 + 2];
      const double x1 = major.vertsXyz[static_cast<size_t>(v + 1) * 3 + 0];
      const double y1 = major.vertsXyz[static_cast<size_t>(v + 1) * 3 + 1];
      const double z1 = major.vertsXyz[static_cast<size_t>(v + 1) * 3 + 2];
      const double seg = std::hypot(x1 - x0, y1 - y0);
      if (seg <= 0.0)
        continue;
      double remain = seg;
      double used = 0.0;
      while (accum + remain >= nextAt - 1.0e-9) {
        const double need = nextAt - accum;
        if (need < 0.0)
          break;
        const double t = (need <= 0.0) ? 0.0 : (need / seg);
        if (t > 1.0)
          break;
        ContourLabelPoint p;
        p.x = static_cast<float>(x0 + t * (x1 - x0));
        p.y = static_cast<float>(y0 + t * (y1 - y0));
        p.z = static_cast<float>(z0 + t * (z1 - z0));
        p.level = level;
        out->push_back(p);
        nextAt += spacingFt;
        used = need;
        if (need >= remain)
          break;
      }
      accum += seg;
      (void)used;
    }
  }
}
