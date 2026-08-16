#include "benchscene.hpp"

#include <algorithm>
#include <cmath>

namespace benchscene {
namespace {

/// Fixed-seed integer hash used as the scene's only source of variation.
///
/// A hash of the index rather than a stateful PRNG so the geometry depends on nothing but the
/// vertex's position in the scene — no iteration order, no accumulated state, hence identical
/// output on any platform and any run. That is what makes "committed bench scene" true.
[[nodiscard]] double Wobble(int contour, int step, int salt) {
  std::uint32_t h = static_cast<std::uint32_t>(contour) * 374761393u +
                    static_cast<std::uint32_t>(step) * 668265263u + static_cast<std::uint32_t>(salt) * 2246822519u;
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return static_cast<double>(h) / 4294967295.0;  // [0, 1]
}

/// Elevation of the bench terrain at (\p x, \p y), and its two partial derivatives.
///
/// The same two-sinusoid surface `BuildSurfacePointScene` scatters its shots over. It is written
/// out again there rather than routed through this helper **on purpose**: that scene's exact bytes
/// back a recorded REQ-100 measurement (TASK-052), and re-expressing the arithmetic to save four
/// lines is not worth any chance of moving it. Two copies of one formula, with the reason stated,
/// beats a refactor that quietly invalidates a published number.
void TerrainAt(double x, double y, double* z, double* dzdx, double* dzdy) {
  *z = 100.0 + 20.0 * std::sin(x / 400.0) + 10.0 * std::cos(y / 260.0) + 6.0 * std::sin((x + y) / 900.0);
  *dzdx = (20.0 / 400.0) * std::cos(x / 400.0) + (6.0 / 900.0) * std::cos((x + y) / 900.0);
  *dzdy = -(10.0 / 260.0) * std::sin(y / 260.0) + (6.0 / 900.0) * std::cos((x + y) / 900.0);
}

constexpr int kVertsPerContour = 501;  ///< 500 segments each — long lines, like real contours.
constexpr double kPlanExtentX = 5000.0;  ///< Plan size of the modelled sheet, in feet…
constexpr double kPlanExtentY = 4000.0;  ///< …fixed, so segment count changes DENSITY, not area.
constexpr double kElevRange = 400.0;     ///< Relief across the site: enough that an orbit separates
                                         ///< the contours visibly rather than showing a flat mat.

} // namespace

int BuildContourScene(int targetSegments, std::vector<float>* verts, std::vector<int>* offsets,
                      std::vector<std::uint8_t>* closed) {
  if (!verts || !offsets || !closed)
    return 0;
  verts->clear();
  offsets->clear();
  closed->clear();
  if (targetSegments < 1)
    return 0;

  const int segsPerContour = kVertsPerContour - 1;
  const int fullContours = targetSegments / segsPerContour;
  const int remainder = targetSegments % segsPerContour;  // one short contour makes the count exact
  const int contours = fullContours + (remainder > 0 ? 1 : 0);

  verts->reserve(static_cast<size_t>(targetSegments + contours) * 3u);
  offsets->reserve(static_cast<size_t>(contours) + 1u);
  closed->reserve(static_cast<size_t>(contours));
  offsets->push_back(0);

  int emittedSegments = 0;
  int vertCursor = 0;
  for (int c = 0; c < contours; ++c) {
    const int segs = (c < fullContours) ? segsPerContour : remainder;
    const int nPts = segs + 1;
    // Each contour is an iso-elevation line: constant Z, wandering in XY.
    //
    // The contours are spread across a FIXED plan extent, so raising the segment count makes the
    // sheet denser rather than larger — which is what a real topo does, and what the benchmark has
    // to do to stay honest. An earlier version spaced them a constant 20 ft apart, which pushed
    // most of a 250k-segment scene outside the framed area: the GPU then rasterised only the part
    // on screen and 250k measured FASTER than 20k, flattering the result into meaninglessness.
    const double spanFrac = (contours > 1) ? static_cast<double>(c) / static_cast<double>(contours - 1) : 0.5;
    const double baseY = kPlanExtentY * (spanFrac - 0.5);
    const double elev = kElevRange * spanFrac;
    const double phase = Wobble(c, 0, 1) * 6.283185307179586;
    const double amp = 40.0 + 60.0 * Wobble(c, 0, 2);
    for (int i = 0; i < nPts; ++i) {
      const double t = static_cast<double>(i) / static_cast<double>(segsPerContour);
      const double x = kPlanExtentX * (t - 0.5);
      // Two sine terms plus a small per-vertex jitter: smooth enough to look like a contour,
      // irregular enough that no two contours share a bounding box or overlap exactly.
      const double y = baseY + amp * std::sin(phase + t * 9.0) + 0.35 * amp * std::sin(phase * 2.0 + t * 23.0) +
                       2.0 * (Wobble(c, i, 3) - 0.5);
      verts->push_back(static_cast<float>(x));
      verts->push_back(static_cast<float>(y));
      verts->push_back(static_cast<float>(elev));
    }
    vertCursor += nPts;
    offsets->push_back(vertCursor);
    closed->push_back(0u);
    emittedSegments += segs;
  }
  return emittedSegments;
}

double Percentile(std::vector<double> samples, double p) {
  if (samples.empty())
    return 0.0;
  std::sort(samples.begin(), samples.end());
  const double clamped = std::clamp(p, 0.0, 100.0);
  // Nearest-rank: rank = ceil(p/100 * N), 1-based, clamped into range.
  int rank = static_cast<int>(std::ceil(clamped / 100.0 * static_cast<double>(samples.size())));
  rank = std::clamp(rank, 1, static_cast<int>(samples.size()));
  return samples[static_cast<size_t>(rank - 1)];
}

FrameStats Summarize(const std::vector<double>& frameMs) {
  FrameStats s;
  s.frames = static_cast<int>(frameMs.size());
  if (frameMs.empty())
    return s;
  double sum = 0.0;
  for (double v : frameMs)
    sum += v;
  s.meanMs = sum / static_cast<double>(frameMs.size());
  s.minMs = Percentile(frameMs, 0.0);
  s.medianMs = Percentile(frameMs, 50.0);
  s.p95Ms = Percentile(frameMs, 95.0);
  s.p99Ms = Percentile(frameMs, 99.0);
  s.maxMs = Percentile(frameMs, 100.0);
  return s;
}

int BuildSurfacePointScene(int targetPoints, std::vector<float>* outXyz) {
  if (!outXyz || targetPoints <= 0)
    return 0;
  outXyz->clear();
  outXyz->reserve(static_cast<size_t>(targetPoints) * 3);

  // A jittered grid rather than pure random scatter: it fills the extent evenly, the way a real
  // topo survey does, and it cannot produce the clustered coincident points that would turn the
  // measurement into a test of de-duplication. Deterministic — a fixed seed and an integer LCG, no
  // clock and no entropy, so the same commit measures the same geometry (REQ-200 in spirit).
  const int side = static_cast<int>(std::sqrt(static_cast<double>(targetPoints))) + 1;
  const double extent = 5000.0;  // feet across, a realistic site
  const double step = extent / static_cast<double>(side);

  std::uint32_t seed = 20260815u;
  auto next01 = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<double>((seed >> 8) & 0xFFFFu) / 65535.0;
  };

  int made = 0;
  for (int i = 0; i < side && made < targetPoints; ++i) {
    for (int j = 0; j < side && made < targetPoints; ++j) {
      // Jitter stays inside the cell, so no two sites can collide.
      const double x = (static_cast<double>(i) + 0.15 + 0.7 * next01()) * step;
      const double y = (static_cast<double>(j) + 0.15 + 0.7 * next01()) * step;
      // Smooth terrain: two sinusoids, ~60 ft of relief. Contours over this are long and smooth,
      // which is what REQ-070 will eventually be measured against.
      const double z = 100.0 + 20.0 * std::sin(x / 400.0) + 10.0 * std::cos(y / 260.0) +
                       6.0 * std::sin((x + y) / 900.0);
      outXyz->push_back(static_cast<float>(x));
      outXyz->push_back(static_cast<float>(y));
      outXyz->push_back(static_cast<float>(z));
      ++made;
    }
  }
  return made;
}

int BuildMeshScene(int targetTriangles, std::vector<float>* verts, std::vector<float>* normals,
                   std::vector<std::uint32_t>* indices) {
  if (!verts || !normals || !indices || targetTriangles <= 0)
    return 0;
  verts->clear();
  normals->clear();
  indices->clear();

  // A quad grid: two triangles per cell, so `side` cells per axis gives 2 * side^2 triangles. The
  // grid spans the SAME fixed plan extent as the contour scene, so raising the triangle count makes
  // the terrain finer rather than larger — the trap TASK-039 §3 documents, where a scene that grew
  // past the viewport benchmarked faster the bigger it got.
  int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(targetTriangles) / 2.0)));
  if (side < 1)
    side = 1;
  const int cols = side + 1;  // vertices per axis
  const double stepX = kPlanExtentX / static_cast<double>(side);
  const double stepY = kPlanExtentY / static_cast<double>(side);

  verts->reserve(static_cast<size_t>(cols) * static_cast<size_t>(cols) * 3);
  normals->reserve(static_cast<size_t>(cols) * static_cast<size_t>(cols) * 3);
  indices->reserve(static_cast<size_t>(targetTriangles) * 3);

  for (int j = 0; j < cols; ++j) {
    const double y = static_cast<double>(j) * stepY;
    for (int i = 0; i < cols; ++i) {
      const double x = static_cast<double>(i) * stepX;
      double z = 0.0, dzdx = 0.0, dzdy = 0.0;
      TerrainAt(x, y, &z, &dzdx, &dzdy);
      verts->push_back(static_cast<float>(x));
      verts->push_back(static_cast<float>(y));
      verts->push_back(static_cast<float>(z));
      // Surface normal of z = f(x, y) is (-df/dx, -df/dy, 1), normalized. Never degenerate: the z
      // component is 1 before normalization, so the length is at least 1.
      const double len = std::sqrt(dzdx * dzdx + dzdy * dzdy + 1.0);
      normals->push_back(static_cast<float>(-dzdx / len));
      normals->push_back(static_cast<float>(-dzdy / len));
      normals->push_back(static_cast<float>(1.0 / len));
    }
  }

  // Emit until the target is hit exactly, rather than emitting whole cells and returning whatever
  // that came to. A profile that measures 2,000,048 triangles is not measuring the density the
  // decision named, and the difference is invisible in the reported p95.
  int made = 0;
  for (int j = 0; j < side && made < targetTriangles; ++j) {
    for (int i = 0; i < side && made < targetTriangles; ++i) {
      const std::uint32_t v00 = static_cast<std::uint32_t>(j * cols + i);
      const std::uint32_t v10 = v00 + 1;
      const std::uint32_t v01 = v00 + static_cast<std::uint32_t>(cols);
      const std::uint32_t v11 = v01 + 1;
      // Counter-clockwise seen from +Z, so the analytic normals above face the same way the winding
      // does. A back-facing half of the grid would measure a mesh half of which is not shaded.
      indices->push_back(v00);
      indices->push_back(v10);
      indices->push_back(v11);
      if (++made >= targetTriangles)
        break;
      indices->push_back(v00);
      indices->push_back(v11);
      indices->push_back(v01);
      ++made;
    }
  }
  return made;
}

} // namespace benchscene
