#pragma once

#include <cstdint>
#include <vector>

/// The REQ-100 benchmark scene and the statistics used to judge it.
///
/// Dependency-free (only `<vector>`/`<cstdint>` here, `<cmath>` in the .cpp) so the test target
/// links it directly. The scene is generated rather than committed as a data file: 250,000 segments
/// is ~1.5 million coordinates, which as `.gs` JSON is a >100 MB artifact in the repository. A
/// deterministic generator IS a committed bench scene — same commit, same bytes, same geometry —
/// and it is diffable, which a 100 MB blob is not.
namespace benchscene {

/// Builds a contour-like polyline scene: many long, smooth, iso-elevation lines.
///
/// This shape is chosen because REQ-100's 250k figure is explicitly "the density of a real topo
/// with contours". Contours are the right stress: they are long polylines (so they exercise the
/// per-vertex-Z path added in TASK-036), each sits at a constant elevation (so an orbit visibly
/// separates them, defeating any plan-view culling), and they carry no text or hatch to flatter the
/// measurement.
///
/// Deterministic for a given \p targetSegments: no clock, no RNG seeding from entropy, no
/// floating-point accumulation that depends on order. Re-running yields identical arrays.
///
/// \param verts    filled with interleaved x,y,z triples (the model's own layout, ADR-025 (a))
/// \param offsets  filled with polyline start indices, `size() == polylines + 1`
/// \param closed   filled with one 0 per polyline (contours are open)
/// \returns the exact number of line segments produced — equal to \p targetSegments when it is at
///          least one segment, so the benchmark measures the density the requirement names rather
///          than whatever a rounded division happened to produce.
int BuildContourScene(int targetSegments, std::vector<float>* verts, std::vector<int>* offsets,
                      std::vector<std::uint8_t>* closed);

/// Builds the point set for the REQ-100 **surface** cost profile: \p targetPoints scattered shots
/// over a smooth undulating terrain, ready to hand to `BuildTin`.
///
/// A surface is its own profile because its per-frame cost does not follow from either of the other
/// two (ADR-028, REQ-100 as amended): its triangle edges are regenerated display geometry, not
/// stored line segments, so neither the 250k-segment case nor the shaded-mesh case measures it.
/// 100,000 points is the density REQ-100 names — a large but ordinary topo — and yields ~200,000
/// triangles, i.e. ~600,000 edges under the default triangle display.
///
/// The terrain is a sum of two sinusoids rather than noise: it is smooth (so contouring it later
/// produces realistic long contours rather than confetti), deterministic, and free of the
/// degenerate coincident points that would make the measurement about de-duplication.
///
/// \param outXyz filled with interleaved x,y,z triples.
/// \returns the number of points produced.
int BuildSurfacePointScene(int targetPoints, std::vector<float>* outXyz);

/// Builds the triangle mesh for the REQ-100 **shaded mesh** cost profile: a curved terrain surface
/// of \p targetTriangles triangles, ready to become a `CadMesh`.
///
/// Meshes are their own profile because a shaded triangle costs nothing like a line segment: it
/// carries a normal, it is depth-tested (ADR-026 (e)), and it occludes the geometry behind it.
/// 2,000,000 triangles is the density REQ-100 (b) names — the fixture TASK-041 validated the mesh
/// path against, so this profile has a known-good comparison point rather than a fresh guess.
///
/// The surface is the **same two-sinusoid terrain** \ref BuildSurfacePointScene uses, for two
/// reasons: it is genuinely curved, so the profile measures real shading and self-occlusion under
/// orbit rather than one flat gradient band; and it makes the mesh and surface profiles directly
/// comparable at a matched size, which is the only way to see what the surface's regenerated edges
/// actually cost.
///
/// Normals are computed **analytically** from the terrain's partial derivatives rather than
/// averaged from adjacent faces: exact, independent of iteration order, and free of the
/// accumulation that would make "byte-identical across runs" a property of the summation order
/// instead of the geometry.
///
/// \param verts    filled with interleaved x,y,z triples (architecture §11.8)
/// \param normals  filled with one unit normal per vertex, parallel to \p verts
/// \param indices  filled with a triangle list, `size() == 3 * triangles`
/// \returns the exact number of triangles produced — equal to \p targetTriangles for any positive
///          value, for the same reason \ref BuildContourScene returns its segment count exactly:
///          the benchmark must measure the density the requirement names.
int BuildMeshScene(int targetTriangles, std::vector<float>* verts, std::vector<float>* normals,
                   std::vector<std::uint32_t>* indices);

/// Nearest-rank percentile of \p samples, \p p in [0, 100]. Takes the vector by value: it sorts.
///
/// Nearest-rank rather than an interpolating definition because the requirement is about a frame
/// that actually happened — "the 95th-percentile frame" is a real frame with a real duration, not a
/// blend of two neighbouring ones.
[[nodiscard]] double Percentile(std::vector<double> samples, double p);

/// Summary of one bench run.
struct FrameStats {
  int frames = 0;
  double minMs = 0.0;
  double medianMs = 0.0;
  double p95Ms = 0.0;   ///< The figure REQ-100 is judged on.
  double p99Ms = 0.0;
  double maxMs = 0.0;
  double meanMs = 0.0;
};

[[nodiscard]] FrameStats Summarize(const std::vector<double>& frameMs);

} // namespace benchscene
