#include <GoSurvey/Geometry/brep.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <unordered_map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>
#include "brep_internal.hpp"

namespace brep {

namespace {

// --- REQ-323 FILLET: round an edge with a rolling ball (ADR-046 amendment (j)) -------------------
//
// The first kernel operation that changes the TOPOLOGY. Amendment (i)'s `PushPullFace` moves what is
// already there, so the same faces, edges and vertices come out as went in; this one deletes an edge
// and creates a face, two edges and four vertices in its place.
//
// Every refusal below is a PRE-check, before anything is built, for the reason amendment (i)
// recorded: `Validate` sees topology and not geometry, and `SelfIntersects` is documented as not
// general, so a radius that would eat past the far side of a face cannot be caught afterwards.


/// Which faces use \p edgeIndex, and whether each traverses it reversed. Exactly two in a valid
/// solid — `EdgeNotUsedTwice` is what guarantees that, so a different count here means the caller
/// handed us something that is not a solid.
struct EdgeUsers {
  int face[2] = {-1, -1};
  int loop[2] = {-1, -1};
  int slot[2] = {-1, -1};
  bool reversed[2] = {false, false};
  int count = 0;
};

[[nodiscard]] EdgeUsers FindEdgeUsers(const Solid& s, int edgeIndex) {
  EdgeUsers u;
  for (std::size_t fi = 0; fi < s.faces.size(); ++fi) {
    const Face& f = s.faces[fi];
    for (std::size_t li = 0; li < f.loops.size(); ++li) {
      const Loop& lp = f.loops[li];
      for (std::size_t si = 0; si < lp.uses.size(); ++si) {
        if (lp.uses[si].edge != edgeIndex)
          continue;
        if (u.count < 2) {
          u.face[u.count] = static_cast<int>(fi);
          u.loop[u.count] = static_cast<int>(li);
          u.slot[u.count] = static_cast<int>(si);
          u.reversed[u.count] = lp.uses[si].reversed;
        }
        ++u.count;
      }
    }
  }
  return u;
}
}  // namespace

/// Solve the 3x3 system `dot(w, n0) = d0`, `dot(w, n1) = d1`, `dot(w, n2) = d2` by Cramer's rule.
/// False when the rows are near-dependent, which here means the two faces are parallel.
[[nodiscard]] bool SolveThreePlanes(const Vec3& n0, double d0, const Vec3& n1, double d1,
                                    const Vec3& n2, double d2, Vec3* outW) {
  const double det = ray3d::Dot(n0, ray3d::Cross(n1, n2));
  if (!(std::fabs(det) > 1e-12))
    return false;
  const Vec3 c12 = ray3d::Cross(n1, n2);
  const Vec3 c20 = ray3d::Cross(n2, n0);
  const Vec3 c01 = ray3d::Cross(n0, n1);
  *outW = ray3d::Scale(ray3d::Add(ray3d::Add(ray3d::Scale(c12, d0), ray3d::Scale(c20, d1)),
                                  ray3d::Scale(c01, d2)),
                       1.0 / det);
  return true;
}

/// Every distinct vertex a face's loops touch.
[[nodiscard]] std::vector<int> FaceVertexSet(const Solid& s, const Face& f) {
  std::vector<int> out;
  CollectFaceVertices(s, f, &out);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

/// Drop the vertices and edges nothing uses any more, and re-point every index that survives.
///
/// Its own pass rather than bookkeeping woven through the edit: the edit is much easier to read when
/// it may leave `v0`, `v1` and the old edge behind, and `UnusedVertex` is a `Validate` failure, so
/// forgetting the sweep fails loudly rather than quietly.
void CompactUnused(Solid* s) {
  std::vector<bool> edgeUsed(s->edges.size(), false);
  for (const Face& f : s->faces)
    for (const Loop& lp : f.loops)
      for (const EdgeUse& u : lp.uses)
        if (u.edge >= 0 && static_cast<std::size_t>(u.edge) < edgeUsed.size())
          edgeUsed[static_cast<std::size_t>(u.edge)] = true;

  std::vector<bool> vertUsed(s->vertices.size(), false);
  for (std::size_t ei = 0; ei < s->edges.size(); ++ei) {
    if (!edgeUsed[ei])
      continue;
    for (const int v : {s->edges[ei].v0, s->edges[ei].v1})
      if (v >= 0 && static_cast<std::size_t>(v) < vertUsed.size())
        vertUsed[static_cast<std::size_t>(v)] = true;
  }

  std::vector<int> vertMap(s->vertices.size(), -1);
  std::vector<Vertex> verts;
  for (std::size_t i = 0; i < s->vertices.size(); ++i) {
    if (!vertUsed[i])
      continue;
    vertMap[i] = static_cast<int>(verts.size());
    verts.push_back(s->vertices[i]);
  }
  std::vector<int> edgeMap(s->edges.size(), -1);
  std::vector<Edge> edges;
  for (std::size_t i = 0; i < s->edges.size(); ++i) {
    if (!edgeUsed[i])
      continue;
    edgeMap[i] = static_cast<int>(edges.size());
    Edge e = s->edges[i];
    e.v0 = vertMap[static_cast<std::size_t>(e.v0)];
    e.v1 = vertMap[static_cast<std::size_t>(e.v1)];
    edges.push_back(std::move(e));
  }
  for (Face& f : s->faces)
    for (Loop& lp : f.loops)
      for (EdgeUse& u : lp.uses)
        u.edge = edgeMap[static_cast<std::size_t>(u.edge)];
  // Shells index faces, and no face is removed here, so they need no remapping pass.
  s->vertices = std::move(verts);
  s->edges = std::move(edges);
}


// Both public entry points are one call into the general builder below. They were two separate
// implementations while increment 1 could only round one edge at a time; increment 2's corner patch
// made the sequential form impossible - after the first fillet the shared vertex is gone, so the
// second arrives at a CYLINDER rather than a plane - so the builder does every requested edge in one
// pass and a single edge is simply the case with no corners. That also retired increment 1's
// position-based re-lookup (TASK-210 DEBT-1), which existed only to survive the compaction between
bool FilletEdgesGeneral(const Solid& s, const std::vector<int>& edgeIndices, double radius,
                        Solid* out, Problem* outWhy);

// sequential passes.
bool FilletEdge(const Solid& s, int edgeIndex, double radius, Solid* out, Problem* outWhy) {
  return FilletEdgesGeneral(s, std::vector<int>{edgeIndex}, radius, out, outWhy);
}

bool FilletEdges(const Solid& s, const std::vector<int>& edgeIndices, double radius, Solid* out,
                 Problem* outWhy) {
  return FilletEdgesGeneral(s, edgeIndices, radius, out, outWhy);
}

namespace {

// --- REQ-323 increment 2: edge CHAINS, and the spherical patch where their fillets meet ----------
//
// Increment 1 rounded one edge at a time and refused any two that shared a corner. This is the
// construction that lifts that, and it replaces the sequential one rather than looping it: after the
// first fillet the corner vertex is gone, so a second fillet arriving at it has a CYLINDER for a
// neighbour rather than a plane, and no amount of iterating gets past that. Everything below is
// built in one pass from the original solid.
//
// The geometry that makes it work, and it is worth stating because it is what keeps every number
// closed-form:
//
//   * The corner ball touches all three faces, so its centre `c` is at distance `r` from all three
//     planes — one 3x3 solve, the same one a single edge uses with two planes and the edge direction.
//   * **Each of the three cylinder axes passes through `c`.** An axis is the locus of points at
//     distance `r` from its own two planes, and `c` is at distance `r` from all three. So the
//     cylinders do not merely approach the corner, they terminate exactly at it.
//   * A cylinder of radius `r` whose axis passes through `c`, and a sphere of radius `r` centred on
//     `c`, meet exactly on the GREAT circle in the plane through `c` perpendicular to that axis.
//     So each boundary between a fillet and the corner patch is a great-circle arc, and the patch is
//     a spherical triangle whose corners are `c + r*n_i` — the same tangent points the three fillets
//     already land on.
//   * When the three normals are mutually perpendicular that triangle is an OCTANT, which in the
//     frame `(x, y, z) = (n1, n2, n3)` is exactly the parameter rectangle `u, v in [0, pi/2]`.
//     That is why increment 2 asks for orthogonality: a general spherical triangle is bounded by
//     three great circles that are not iso-parameter lines, so it would need REQ-321's `paramLoops`
//     and with them a numerically integrated area in place of the closed form.


/// One requested edge, and everything derived from it.
struct FilletEdgePlan {
  int edge = -1;
  int face[2] = {-1, -1};      ///< [0] = A, [1] = B, in `FindEdgeUsers` order.
  bool rev[2] = {false, false};
  int loop[2] = {-1, -1};
  int slot[2] = {-1, -1};
  Vec3 t{};                    ///< unit, from `v[0]` toward `v[1]`
  Vec3 n[2]{};                 ///< the two faces' outward normals
  Vec3 u[2]{};                 ///< in-face unit perpendicular to the edge, INTO the material
  double setback = 0.0;        ///< r / tan(theta/2): how far the fillet cuts into each face
  double length = 0.0;
  int v[2] = {-1, -1};         ///< the original endpoints
  Vec3 axisPt[2]{};            ///< where the fillet's axis sits at each end
  int tan[2] = {-1, -1};       ///< the two tangent-line edges, stored end0 -> end1
  int endArc[2] = {-1, -1};    ///< the arc closing each end, stored A-side -> B-side
  bool cylArcRev[2] = {false, false};  ///< how the CYLINDER used each of those arcs
};

/// A vertex where all three edges are being filleted.
struct FilletCornerPlan {
  int vertex = -1;
  Vec3 centre{};
  int face[3] = {-1, -1, -1};
  int arc[3] = {-1, -1, -1};
  bool arcRevForSphere[3] = {false, false, false};
};
}  // namespace

[[nodiscard]] EdgeUseTable BuildEdgeUseTable(const Solid& s) {
  EdgeUseTable t;
  t.face.assign(s.edges.size(), {-1, -1});
  t.loop.assign(s.edges.size(), {-1, -1});
  t.slot.assign(s.edges.size(), {-1, -1});
  t.rev.assign(s.edges.size(), {false, false});
  t.count.assign(s.edges.size(), 0);
  for (std::size_t fi = 0; fi < s.faces.size(); ++fi)
    for (std::size_t li = 0; li < s.faces[fi].loops.size(); ++li) {
      const Loop& lp = s.faces[fi].loops[li];
      for (std::size_t si = 0; si < lp.uses.size(); ++si) {
        const int ei = lp.uses[si].edge;
        if (ei < 0 || static_cast<std::size_t>(ei) >= s.edges.size())
          continue;
        const std::size_t e = static_cast<std::size_t>(ei);
        if (t.count[e] < 2) {
          t.face[e][t.count[e]] = static_cast<int>(fi);
          t.loop[e][t.count[e]] = static_cast<int>(li);
          t.slot[e][t.count[e]] = static_cast<int>(si);
          t.rev[e][t.count[e]] = lp.uses[si].reversed;
        }
        ++t.count[e];
      }
    }
  return t;
}

/// Does the whole REQUEST fit — not merely each edge of it one at a time?
///
/// **ADR-046 amendment (l), and this exists because the original precondition was measured one edge
/// at a time.** Amendment (i)'s rule — a modifying operation owns its own precondition, because
/// `Validate` checks topology and not geometry — was implemented as "for this edge, does the setback
/// reach the far boundary of each adjacent face?". That question is answered against the ORIGINAL
/// face, so it sees nothing of what the other requested edges take out of the same face, and nothing
/// of one edge's two ends eating each other at their corners. Both let a request through whose
/// blends overlap, and the result is a self-intersecting solid that `Validate` accepts and whose
/// mass properties are meaningless. On a 20 x 10 x 8 box, `FILLET 6` and `CHAMFER 6` on the two
/// 20-long top edges — 10 apart across a 10-wide face — were both accepted.
///
/// Two things have to hold, and they are separate statements rather than one:
///
///  1. **An edge must survive its own two ends.** At a corner the blend runs `consumed` along the
///     edge before it begins, so the blend face is `length - consumed[0] - consumed[1]` long and
///     that has to be positive.
///  2. **Two requested edges bounding one face must not cut into each other.** Their strips are
///     `setback` and `setback'` wide, so the room between them has to exceed the sum. **ADJACENT
///     edges are exempt**: their cuts are *meant* to meet, at the shared corner vertex, and (1) is
///     what covers their interaction instead.
///
/// Equality is refused in both, for the reason the original check already gave: at the limit the
/// face does not become thin, it vanishes.
///
/// (2) is measured from the nearer of the other edge's two endpoints, which is exact for the
/// parallel case that a box, wedge or prism actually produces. For two edges of a face that are
/// neither parallel nor adjacent — reachable only on a face with more than four sides — it is
/// conservative rather than exact, and errs toward refusing something buildable. That direction is
/// deliberate: the failure being fixed here is the other one.
[[nodiscard]] BlendFit BlendsFit(const std::vector<BlendSpan>& spans) {
  constexpr double kEps = 1e-9;

  for (const BlendSpan& a : spans)
    if (!(a.consumed[0] + a.consumed[1] < a.length - kEps))
      return BlendFit::EdgeTooShort;

  for (std::size_t ia = 0; ia < spans.size(); ++ia)
    for (std::size_t ib = 0; ib < spans.size(); ++ib) {
      if (ia == ib)
        continue;
      const BlendSpan& a = spans[ia];
      const BlendSpan& b = spans[ib];
      if (a.v[0] == b.v[0] || a.v[0] == b.v[1] || a.v[1] == b.v[0] || a.v[1] == b.v[1])
        continue;  // adjacent: their cuts meet at the shared corner by construction
      for (int ka = 0; ka < 2; ++ka)
        for (int kb = 0; kb < 2; ++kb) {
          if (b.face[kb] != a.face[ka])
            continue;
          // How far the shared face still reaches from `a`, once `b` has taken its own strip.
          const double reach =
              std::min(ray3d::Dot(ray3d::Sub(b.p[0], a.p[0]), a.u[ka]),
                       ray3d::Dot(ray3d::Sub(b.p[1], a.p[0]), a.u[ka])) +
              b.setback * ray3d::Dot(b.u[kb], a.u[ka]);
          if (!(a.setback < reach - kEps))
            return BlendFit::Overlaps;
        }
    }
  return BlendFit::Ok;
}



bool FilletEdgesGeneral(const Solid& s, const std::vector<int>& edgeIndices, double radius,
                        Solid* out, Problem* outWhy) {
  if (!out)
    return false;
  const auto fail = [&](Problem why) { return Fail(why, outWhy); };
  if (!AllFinite({radius}))
    return fail(Problem::NonFiniteParameter);
  if (!(radius > 0.0))
    return fail(Problem::FilletRadiusNotPositive);
  if (edgeIndices.empty())
    return fail(Problem::IndexOutOfRange);

  const EdgeUseTable uses = BuildEdgeUseTable(s);
  std::vector<int> req = edgeIndices;
  std::sort(req.begin(), req.end());
  if (std::adjacent_find(req.begin(), req.end()) != req.end())
    return fail(Problem::IndexOutOfRange);  // the same edge twice is a caller mistake, not a shape
  for (const int ei : req)
    if (ei < 0 || static_cast<std::size_t>(ei) >= s.edges.size())
      return fail(Problem::IndexOutOfRange);

  const auto isRequested = [&](int ei) {
    return std::binary_search(req.begin(), req.end(), ei);
  };

  // --- Per-edge validation and geometry, exactly increment 1's ------------------------------------
  std::vector<FilletEdgePlan> plans;
  plans.reserve(req.size());
  for (const int ei : req) {
    const std::size_t e = static_cast<std::size_t>(ei);
    if (uses.count[e] != 2)
      return fail(Problem::EdgeNotUsedTwice);
    const Edge& ed = s.edges[e];
    if (ed.kind != CurveKind::Line)
      return fail(Problem::FilletEdgeNotLine);

    FilletEdgePlan p;
    p.edge = ei;
    for (int k = 0; k < 2; ++k) {
      p.face[k] = uses.face[e][static_cast<std::size_t>(k)];
      p.loop[k] = uses.loop[e][static_cast<std::size_t>(k)];
      p.slot[k] = uses.slot[e][static_cast<std::size_t>(k)];
      p.rev[k] = uses.rev[e][static_cast<std::size_t>(k)];
    }
    const Face& fA = s.faces[static_cast<std::size_t>(p.face[0])];
    const Face& fB = s.faces[static_cast<std::size_t>(p.face[1])];
    if (fA.surface.kind != SurfaceKind::Plane || fB.surface.kind != SurfaceKind::Plane)
      return fail(Problem::FilletFaceNotPlanar);
    p.v[0] = ed.v0;
    p.v[1] = ed.v1;
    const Vec3 p0 = s.vertices[static_cast<std::size_t>(ed.v0)].p;
    const Vec3 p1 = s.vertices[static_cast<std::size_t>(ed.v1)].p;
    const double len = ray3d::Length(ray3d::Sub(p1, p0));
    if (!(len > 1e-12))
      return fail(Problem::DegenerateEdge);
    p.t = ray3d::Scale(ray3d::Sub(p1, p0), 1.0 / len);
    p.n[0] = ray3d::Normalize(fA.surface.frame.zAxis);
    p.n[1] = ray3d::Normalize(fB.surface.frame.zAxis);
    if (std::fabs(ray3d::Dot(p.n[0], p.n[1])) > 1.0 - 1e-9)
      return fail(Problem::FilletFacesParallel);

    const Vec3 dirA = p.rev[0] ? ray3d::Scale(p.t, -1.0) : p.t;
    const Vec3 dirB = p.rev[1] ? ray3d::Scale(p.t, -1.0) : p.t;
    const Vec3 uA = ray3d::Normalize(ray3d::Cross(p.n[0], dirA));
    const Vec3 uB = ray3d::Normalize(ray3d::Cross(p.n[1], dirB));
    if (!(ray3d::Dot(uA, p.n[1]) < -1e-9))
      return fail(Problem::FilletEdgeConcave);
    p.u[0] = uA;
    p.u[1] = uB;
    p.length = len;

    const double cosTheta = std::clamp(ray3d::Dot(uA, uB), -1.0, 1.0);
    const double halfT = 0.5 * std::acos(cosTheta);
    if (!(std::tan(halfT) > 1e-12))
      return fail(Problem::FilletFacesParallel);
    const double setback = radius / std::tan(halfT);
    p.setback = setback;
    const auto reach = [&](const Face& f, const Vec3& dir) {
      double m = 0.0;
      for (const int vi : FaceVertexSet(s, f))
        m = std::max(m, ray3d::Dot(ray3d::Sub(s.vertices[static_cast<std::size_t>(vi)].p, p0), dir));
      return m;
    };
    if (!(setback < reach(fA, uA) - 1e-9) || !(setback < reach(fB, uB) - 1e-9))
      return fail(Problem::FilletRadiusTooLarge);

    // The axis offset from the edge line: distance `radius` from each plane, on the material side.
    Vec3 w{};
    if (!SolveThreePlanes(p.n[0], -radius, p.n[1], -radius, p.t, 0.0, &w))
      return fail(Problem::FilletFacesParallel);
    p.axisPt[0] = ray3d::Add(p0, w);
    p.axisPt[1] = ray3d::Add(p1, w);
    plans.push_back(p);
  }

  // --- Classify every vertex a requested edge touches ---------------------------------------------
  std::map<int, std::vector<int>> atVertex;  // original vertex -> requested plan slots
  for (std::size_t i = 0; i < plans.size(); ++i)
    for (const int vk : plans[i].v)
      atVertex[vk].push_back(static_cast<int>(i));

  std::vector<int> degree(s.vertices.size(), 0);
  for (const Edge& ed : s.edges) {
    ++degree[static_cast<std::size_t>(ed.v0)];
    ++degree[static_cast<std::size_t>(ed.v1)];
  }

  std::vector<FilletCornerPlan> corners;
  std::map<int, int> cornerOfVertex;  // original vertex -> index into `corners`
  for (const auto& [vk, slots] : atVertex) {
    if (degree[static_cast<std::size_t>(vk)] != 3)
      return fail(Problem::FilletVertexNotSimple);
    if (slots.size() == 2) {
      // The ball would have to run off a rounded edge onto one staying sharp - a setback blend, and
      // a different construction from the corner patch (REQ-323 increment 2's own boundary).
      return fail(Problem::FilletCornerPartial);
    }
    if (slots.size() != 3)
      continue;  // an open end; handled with the planar end face, exactly as increment 1 does

    FilletCornerPlan c;
    c.vertex = vk;
    // The three faces at the corner are the three faces of the three edges, each appearing twice.
    std::vector<int> fs;
    for (const int si : slots)
      for (const int f : plans[static_cast<std::size_t>(si)].face)
        fs.push_back(f);
    std::sort(fs.begin(), fs.end());
    fs.erase(std::unique(fs.begin(), fs.end()), fs.end());
    if (fs.size() != 3)
      return fail(Problem::FilletVertexNotSimple);
    Vec3 nrm[3];
    for (int i = 0; i < 3; ++i) {
      c.face[i] = fs[static_cast<std::size_t>(i)];
      const Face& f = s.faces[static_cast<std::size_t>(c.face[i])];
      if (f.surface.kind != SurfaceKind::Plane)
        return fail(Problem::FilletFaceNotPlanar);
      nrm[i] = ray3d::Normalize(f.surface.frame.zAxis);
    }
    // Orthogonal, so the patch is an octant and stays an iso-rectangle - see the note above.
    for (int i = 0; i < 3; ++i)
      if (std::fabs(ray3d::Dot(nrm[i], nrm[(i + 1) % 3])) > 1e-9)
        return fail(Problem::FilletCornerNotOrthogonal);

    Vec3 w{};
    if (!SolveThreePlanes(nrm[0], -radius, nrm[1], -radius, nrm[2], -radius, &w))
      return fail(Problem::FilletCornerNotOrthogonal);
    c.centre = ray3d::Add(s.vertices[static_cast<std::size_t>(vk)].p, w);
    cornerOfVertex[vk] = static_cast<int>(corners.size());
    corners.push_back(c);
  }

  // At a corner the fillet axis passes through the ball's centre, so every edge meeting there ends
  // exactly on it. This is the one place the corner changes an edge's own geometry.
  for (FilletEdgePlan& p : plans)
    for (int k = 0; k < 2; ++k) {
      const auto it = cornerOfVertex.find(p.v[k]);
      if (it != cornerOfVertex.end())
        p.axisPt[k] = corners[static_cast<std::size_t>(it->second)].centre;
    }


  // Does the whole REQUEST fit? The per-edge check above asks only whether THIS radius reaches the
  // far side of THIS edge's own faces, measured against the ORIGINAL solid - so it sees nothing of
  // what the other requested edges take out of the same face, and nothing of one edge's two ends
  // eating each other. ADR-046 amendment (l), and the case that forced it: `FILLET 6` on the two
  // 20-long top edges of a 20 x 10 x 8 box (10 apart across a 10-wide face) was ACCEPTED and
  // returned a self-intersecting solid reporting volume 1290.97336.
  //
  // Here rather than in the per-edge loop because it needs every edge's setback at once, and after
  // the axis points have been moved onto the corner centres because that is what makes `consumed`
  // fall out: `dot(axisPt[k] - p[k], t)` is zero at an open end and the corner's own bite at a
  // corner, with no second rule.
  {
    std::vector<BlendSpan> spans;
    spans.reserve(plans.size());
    for (const FilletEdgePlan& p : plans) {
      BlendSpan b;
      for (int k = 0; k < 2; ++k) {
        b.v[k] = p.v[k];
        b.p[k] = s.vertices[static_cast<std::size_t>(p.v[k])].p;
        b.face[k] = p.face[k];
        b.u[k] = p.u[k];
      }
      b.t = p.t;
      b.length = p.length;
      b.setback = p.setback;
      for (int k = 0; k < 2; ++k)
        b.consumed[k] = std::fabs(ray3d::Dot(ray3d::Sub(p.axisPt[k], b.p[k]), p.t));
      spans.push_back(b);
    }
    switch (BlendsFit(spans)) {
    case BlendFit::EdgeTooShort:
      return fail(Problem::FilletEdgeTooShortForItsCorners);
    case BlendFit::Overlaps:
      return fail(Problem::FilletRadiusOverlapsAnother);
    case BlendFit::Ok:
      break;
    }
  }
  // Open ends keep increment 1's rule: the one other face there must be planar and square to the
  // edge, so the arc closing the fillet is a circle rather than an ellipse.
  for (const auto& [vk, slots] : atVertex) {
    if (slots.size() != 1)
      continue;
    const FilletEdgePlan& p = plans[static_cast<std::size_t>(slots.front())];
    int endFace = -1;
    for (std::size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (static_cast<int>(fi) == p.face[0] || static_cast<int>(fi) == p.face[1])
        continue;
      const std::vector<int> vs = FaceVertexSet(s, s.faces[fi]);
      if (!std::binary_search(vs.begin(), vs.end(), vk))
        continue;
      if (endFace >= 0)
        return fail(Problem::FilletVertexNotSimple);
      endFace = static_cast<int>(fi);
    }
    if (endFace < 0)
      return fail(Problem::FilletVertexNotSimple);
    const Face& ef = s.faces[static_cast<std::size_t>(endFace)];
    if (ef.surface.kind != SurfaceKind::Plane)
      return fail(Problem::FilletEndFaceUnsupported);
    if (std::fabs(std::fabs(ray3d::Dot(ray3d::Normalize(ef.surface.frame.zAxis), p.t)) - 1.0) > 1e-9)
      return fail(Problem::FilletEndFaceUnsupported);
  }

  // --- Build ---------------------------------------------------------------------------------------
  Solid r = s;
  // Every new vertex is named by (original vertex, original face): the point where the fillet lands
  // on that face. At a corner three faces give three points; at an open end only the edge's own two
  // faces do, and the third face's corner is closed by the arc instead.
  std::map<std::pair<int, int>, int> tangentVert;
  const auto tangentPoint = [&](const FilletEdgePlan& p, int k, int which) {
    const std::pair<int, int> key{p.v[k], p.face[which]};
    const auto it = tangentVert.find(key);
    if (it != tangentVert.end())
      return it->second;
    const int id = AddVertex(&r, ray3d::Add(p.axisPt[k], ray3d::Scale(p.n[which], radius)));
    tangentVert.emplace(key, id);
    return id;
  };

  for (FilletEdgePlan& p : plans) {
    const int a0 = tangentPoint(p, 0, 0);
    const int a1 = tangentPoint(p, 1, 0);
    const int b0 = tangentPoint(p, 0, 1);
    const int b1 = tangentPoint(p, 1, 1);
    p.tan[0] = AddLine(&r, a0, a1);
    p.tan[1] = AddLine(&r, b0, b1);
    // Which way round the cylinder the fillet runs, in the frame whose X points at the face-A
    // tangent line: face A sits at 0 and face B at `phi`, which is +-(pi - theta).
    const Vec3 frameX = p.n[0];
    const Vec3 frameY = ray3d::Normalize(ray3d::Cross(p.t, p.n[0]));
    const double phi = std::atan2(ray3d::Dot(p.n[1], frameY), ray3d::Dot(p.n[1], frameX));
    const Vec3 arcNormal = phi > 0.0 ? p.t : ray3d::Scale(p.t, -1.0);
    const double arcSweep = std::fabs(phi);
    // ONE rule for both kinds of end: an arc of radius `radius` about the axis point, from the
    // face-A tangent point to the face-B one. At an open end that axis point is the edge's own
    // offset; at a corner it is the ball's centre. Nothing else differs.
    p.endArc[0] = AddArc(&r, a0, b0, p.axisPt[0], arcNormal, arcSweep);
    p.endArc[1] = AddArc(&r, a1, b1, p.axisPt[1], arcNormal, arcSweep);
  }

  // Unrequested edges ending on a touched vertex follow the face they share with the fillet. A
  // corner has no such edges - all three of its edges are requested - so this only ever fires at an
  // open end, where exactly one of the edge's two faces is the fillet's.
  for (std::size_t ei = 0; ei < r.edges.size(); ++ei) {
    if (isRequested(static_cast<int>(ei)) || ei >= uses.count.size())
      continue;
    Edge& x = r.edges[ei];
    for (int endSel = 0; endSel < 2; ++endSel) {
      const int vk = endSel == 0 ? x.v0 : x.v1;
      const auto it = atVertex.find(vk);
      if (it == atVertex.end() || it->second.size() != 1)
        continue;
      const FilletEdgePlan& p = plans[static_cast<std::size_t>(it->second.front())];
      int repl = -1;
      for (int which = 0; which < 2 && repl < 0; ++which)
        for (const int xf : uses.face[ei])
          if (xf == p.face[which]) {
            const auto vt = tangentVert.find({vk, p.face[which]});
            if (vt != tangentVert.end())
              repl = vt->second;
            break;
          }
      if (repl < 0)
        continue;
      if (endSel == 0)
        x.v0 = repl;
      else
        x.v1 = repl;
    }
    // A shortened arc keeps its centre and radius; only its start direction is re-anchored.
    if (x.kind == CurveKind::Arc || x.kind == CurveKind::Ellipse) {
      const Vec3 toStart = ray3d::Sub(r.vertices[static_cast<std::size_t>(x.v0)].p, x.frame.origin);
      const Vec3 z = ray3d::Normalize(x.frame.zAxis);
      const Vec3 nx = ray3d::Sub(toStart, ray3d::Scale(z, ray3d::Dot(toStart, z)));
      if (ray3d::Length(nx) > 1e-12) {
        x.frame.xAxis = ray3d::Normalize(nx);
        x.frame.yAxis = ray3d::Normalize(ray3d::Cross(z, x.frame.xAxis));
      }
    }
  }

  // Each requested edge becomes its face's tangent line, keeping the sense the loop already had.
  for (const FilletEdgePlan& p : plans)
    for (int which = 0; which < 2; ++which)
      r.faces[static_cast<std::size_t>(p.face[which])]
          .loops[static_cast<std::size_t>(p.loop[which])]
          .uses[static_cast<std::size_t>(p.slot[which])] = EdgeUse{p.tan[which], p.rev[which]};

  // Close whatever gaps that left. At a corner both edges of a face are requested and their tangent
  // lines already meet, so nothing is inserted; at an open end the third face gets the arc.
  const auto useFrom = [&](const EdgeUse& u) {
    const Edge& x = r.edges[static_cast<std::size_t>(u.edge)];
    return u.reversed ? x.v1 : x.v0;
  };
  const auto useTo = [&](const EdgeUse& u) {
    const Edge& x = r.edges[static_cast<std::size_t>(u.edge)];
    return u.reversed ? x.v0 : x.v1;
  };
  std::map<std::pair<int, int>, int> arcByEnds;
  for (const FilletEdgePlan& p : plans)
    for (int k = 0; k < 2; ++k) {
      const Edge& a = r.edges[static_cast<std::size_t>(p.endArc[k])];
      arcByEnds[{std::min(a.v0, a.v1), std::max(a.v0, a.v1)}] = p.endArc[k];
    }
  for (Face& f : r.faces)
    for (Loop& lp : f.loops) {
      for (std::size_t i = 0; i < lp.uses.size(); ++i) {
        const std::size_t j = (i + 1) % lp.uses.size();
        const int to = useTo(lp.uses[i]);
        const int from = useFrom(lp.uses[j]);
        if (to == from)
          continue;
        const auto it = arcByEnds.find({std::min(to, from), std::max(to, from)});
        if (it == arcByEnds.end())
          continue;  // not a gap this fillet opened; Validate will report it
        const Edge& a = r.edges[static_cast<std::size_t>(it->second)];
        lp.uses.insert(lp.uses.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                       EdgeUse{it->second, to == a.v1});
        ++i;  // step over what was just inserted
      }
    }

  // The cylinders. The ring takes the OPPOSITE sense to face A's use of the tangent line, which is
  // what makes every edge used once each way; the rest of it follows from where that leaves off.
  for (FilletEdgePlan& p : plans) {
    const int kEnd = p.rev[0] ? 1 : 0;
    const int kFar = 1 - kEnd;
    Loop lp;
    lp.uses.push_back(EdgeUse{p.tan[0], !p.rev[0]});
    lp.uses.push_back(EdgeUse{p.endArc[kEnd], false});
    lp.uses.push_back(EdgeUse{p.tan[1], kEnd == 1});
    lp.uses.push_back(EdgeUse{p.endArc[kFar], true});
    p.cylArcRev[kEnd] = false;
    p.cylArcRev[kFar] = true;

    const Vec3 frameX = p.n[0];
    const double phi = std::atan2(ray3d::Dot(p.n[1], ray3d::Normalize(ray3d::Cross(p.t, p.n[0]))),
                                  ray3d::Dot(p.n[1], frameX));
    const Vec3 axisZ = phi > 0.0 ? p.t : ray3d::Scale(p.t, -1.0);
    Face f;
    f.surface.kind = SurfaceKind::Cylinder;
    f.surface.radius = radius;
    f.surface.height =
        ray3d::Length(ray3d::Sub(p.axisPt[1], p.axisPt[0]));  // shorter than the edge at a corner
    f.surface.frame.zAxis = axisZ;
    f.surface.frame.xAxis = frameX;
    f.surface.frame.yAxis = ray3d::Normalize(ray3d::Cross(axisZ, frameX));
    f.surface.frame.origin = phi > 0.0 ? p.axisPt[0] : p.axisPt[1];
    f.uStart = 0.0;
    f.uEnd = std::fabs(phi);
    f.loops.push_back(std::move(lp));
    r.faces.push_back(std::move(f));
    if (!r.shells.empty())
      r.shells.front().faces.push_back(static_cast<int>(r.faces.size()) - 1);
  }

  // The corner patches. Three great-circle arcs, each shared with one cylinder and used the other
  // way round here — which is what keeps the shell orientable without a second rule.
  for (FilletCornerPlan& c : corners) {
    int found = 0;
    for (const FilletEdgePlan& p : plans)
      for (int k = 0; k < 2; ++k) {
        if (p.v[k] != c.vertex)
          continue;
        if (found >= 3)
          return fail(Problem::FilletVertexNotSimple);
        c.arc[found] = p.endArc[k];
        c.arcRevForSphere[found] = !p.cylArcRev[k];
        ++found;
      }
    if (found != 3)
      return fail(Problem::FilletVertexNotSimple);

    // Chain the three into a ring head-to-tail rather than assuming the order they were collected.
    Loop lp;
    std::array<bool, 3> used{false, false, false};
    int at = -1;
    for (int step = 0; step < 3; ++step) {
      int pick = -1;
      for (int i = 0; i < 3 && pick < 0; ++i) {
        if (used[static_cast<std::size_t>(i)])
          continue;
        const Edge& a = r.edges[static_cast<std::size_t>(c.arc[i])];
        const int from = c.arcRevForSphere[i] ? a.v1 : a.v0;
        if (at < 0 || from == at)
          pick = i;
      }
      if (pick < 0)
        return fail(Problem::FilletResultInvalid);
      used[static_cast<std::size_t>(pick)] = true;
      const Edge& a = r.edges[static_cast<std::size_t>(c.arc[pick])];
      at = c.arcRevForSphere[pick] ? a.v0 : a.v1;
      lp.uses.push_back(EdgeUse{c.arc[pick], c.arcRevForSphere[pick]});
    }

    // The octant, in the frame its own three normals define. `n0 x n1 = n2` makes it right-handed;
    // if the three came out the other way round, swapping two of them fixes it without moving the
    // patch. In that frame the three tangent points sit at (u,v) = (0,0), (pi/2,0) and the pole, so
    // the patch IS the rectangle [0, pi/2] x [0, pi/2] and its area stays closed-form.
    Vec3 nx = ray3d::Normalize(s.faces[static_cast<std::size_t>(c.face[0])].surface.frame.zAxis);
    Vec3 ny = ray3d::Normalize(s.faces[static_cast<std::size_t>(c.face[1])].surface.frame.zAxis);
    const Vec3 nz = ray3d::Normalize(s.faces[static_cast<std::size_t>(c.face[2])].surface.frame.zAxis);
    if (ray3d::Dot(ray3d::Cross(nx, ny), nz) < 0.0)
      std::swap(nx, ny);
    Face f;
    f.surface.kind = SurfaceKind::Sphere;
    f.surface.radius = radius;
    f.surface.frame.origin = c.centre;
    f.surface.frame.xAxis = nx;
    f.surface.frame.yAxis = ny;
    f.surface.frame.zAxis = nz;
    f.uStart = 0.0;
    f.uEnd = kHalfPi;
    f.vStart = 0.0;
    f.vEnd = kHalfPi;
    f.loops.push_back(std::move(lp));
    r.faces.push_back(std::move(f));
    if (!r.shells.empty())
      r.shells.front().faces.push_back(static_cast<int>(r.faces.size()) - 1);
  }

  CompactUnused(&r);
  r.recipe = Recipe{};
  const Problem why = Validate(r);
  if (why != Problem::Ok)
    return fail(Problem::FilletResultInvalid);
  *out = std::move(r);
  if (outWhy)
    *outWhy = Problem::Ok;
  return true;
}

} // namespace brep
