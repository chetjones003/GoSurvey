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

// --- REQ-331: CHAMFER, the flat bevel (ADR-046 amendment (k)) ------------------------------------
//
// The chamfer has exactly the fillet's TOPOLOGY delta and none of its geometry, which is why this
// is a sibling of the builder above rather than a mode of it. `V + 2`, `E + 3`, `F + 1` per edge,
// box 8/12/6 -> 10/15/7 — but a `Plane` where the fillet builds a `Cylinder`, and a `Line` where it
// builds an `Arc`.
//
// Three things are genuinely simpler here, and each is a place the fillet had to work:
//
//   * **There is no model to convert.** The fillet states "a rolling ball" because the model decides
//     the setback, `d = r / tan(theta/2)`. A chamfer's input IS the setback, at every dihedral.
//     Where the fillet lands is derived; where the chamfer lands is given.
//   * **The bevel plane is the plane through the two cut lines**, and its outward normal is
//     `-normalize(uA + uB)` — the in-face perpendiculars the precondition block already computes
//     (they are what the concavity test is built on). Nothing new is derived.
//   * **A corner is a VERTEX, not a facet.** Three planes in general position meet at exactly one
//     point; three cylinders do not, which is precisely why the fillet needs a spherical patch to
//     close the gap it leaves. So an orthogonal corner gains one vertex, three edges and NO face.
//     The visible consequence is that each bevel face is a HEXAGON — a rectangle with a V-notch
//     bitten out of each end by its two neighbours — which is a six-vertex straight-edged loop on a
//     plane, and `PlaneFaceArea` integrates over loops already.
//
// Orthogonality is still required at a corner, but NOT for the fillet's reason. There is no patch to
// parametrise here; what fails off-square is the TANGENT VERTEX. The point where two bevels' cut
// lines meet inside a face they share is `p + d*u1 + d*u2` only when `u1` and `u2` are
// perpendicular, which is what mutual orthogonality of the three faces buys.
//
// An oblique end face is refused, and that was measured rather than predicted: a plane does cut a
// plane in a straight line at any angle, but the tangent point `p + d*u` only LIES on the end face
// when that face is square to the edge (`u` is perpendicular to the edge, so the point keeps its
// position along it). Off-square the tangent lines would have to be trimmed to the bevel-plane cut
// instead, which is a second construction. See ADR-046 amendment (k)(5).
//
// One pass over the original solid, per amendment (j)(4) and for the identical reason: after the
// first chamfer the shared vertex is gone.


/// One requested edge, and everything derived from it. The fillet's twin, minus the axis (a bevel
/// has no axis) and with a straight end edge in place of the arc.
struct ChamferEdgePlan {
  int edge = -1;
  int face[2] = {-1, -1};      ///< [0] = A, [1] = B, in `BuildEdgeUseTable` order.
  bool rev[2] = {false, false};
  int loop[2] = {-1, -1};
  int slot[2] = {-1, -1};
  Vec3 t{};                    ///< unit, from `v[0]` toward `v[1]`
  Vec3 n[2]{};                 ///< the two faces' outward normals
  Vec3 u[2]{};                 ///< in-face unit perpendicular to the edge, INTO the material
  int v[2] = {-1, -1};         ///< the original endpoints
  int tan[2] = {-1, -1};       ///< the two cut-line edges, stored end0 -> end1
  int endEdge[2] = {-1, -1};   ///< an open end's closing line, A-side -> B-side; -1 at a corner
  Vec3 planeN{};               ///< the bevel's outward normal
  double planeD = 0.0;         ///< `planeN . x = planeD`
};

/// A vertex where all three edges are being chamfered. Unlike the fillet's, it carries no face —
/// the three bevel planes meet at `point`, so there is nothing to close.
struct ChamferCornerPlan {
  int vertex = -1;
  Vec3 point{};
};

}  // namespace

bool ChamferEdgesGeneral(const Solid& s, const std::vector<int>& edgeIndices, double distance,
                         Solid* out, Problem* outWhy) {
  if (!out)
    return false;
  const auto fail = [&](Problem why) { return Fail(why, outWhy); };
  if (!AllFinite({distance}))
    return fail(Problem::NonFiniteParameter);
  if (!(distance > 0.0))
    return fail(Problem::ChamferDistanceNotPositive);
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

  const auto isRequested = [&](int ei) { return std::binary_search(req.begin(), req.end(), ei); };

  // --- Per-edge validation and geometry ------------------------------------------------------------
  std::vector<ChamferEdgePlan> plans;
  plans.reserve(req.size());
  for (const int ei : req) {
    const std::size_t e = static_cast<std::size_t>(ei);
    if (uses.count[e] != 2)
      return fail(Problem::EdgeNotUsedTwice);
    const Edge& ed = s.edges[e];
    if (ed.kind != CurveKind::Line)
      return fail(Problem::ChamferEdgeNotLine);

    ChamferEdgePlan p;
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
      return fail(Problem::ChamferFaceNotPlanar);
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
      return fail(Problem::ChamferFacesParallel);

    // The in-face perpendiculars, pointing into each face's material. Same construction the fillet's
    // precondition uses, and here they carry the whole bevel: the cut lines sit `distance` along
    // them, and their sum fixes the plane.
    const Vec3 dirA = p.rev[0] ? ray3d::Scale(p.t, -1.0) : p.t;
    const Vec3 dirB = p.rev[1] ? ray3d::Scale(p.t, -1.0) : p.t;
    p.u[0] = ray3d::Normalize(ray3d::Cross(p.n[0], dirA));
    p.u[1] = ray3d::Normalize(ray3d::Cross(p.n[1], dirB));
    if (!(ray3d::Dot(p.u[0], p.n[1]) < -1e-9))
      return fail(Problem::ChamferEdgeConcave);

    // The precondition, per amendment (i): the cut has to stay inside each adjacent face. No trig —
    // `distance` IS the setback (amendment (k)(2)), where the fillet has to convert first.
    const auto reach = [&](const Face& f, const Vec3& dir) {
      double m = 0.0;
      for (const int vi : FaceVertexSet(s, f))
        m = std::max(m, ray3d::Dot(ray3d::Sub(s.vertices[static_cast<std::size_t>(vi)].p, p0), dir));
      return m;
    };
    if (!(distance < reach(fA, p.u[0]) - 1e-9) || !(distance < reach(fB, p.u[1]) - 1e-9))
      return fail(Problem::ChamferDistanceTooLarge);

    // The bevel plane: through both cut lines, so its outward normal bisects the two faces' outward
    // normals. `u` points INTO the material, hence the negation.
    const Vec3 bis = ray3d::Add(p.u[0], p.u[1]);
    if (!(ray3d::Length(bis) > 1e-12))
      return fail(Problem::ChamferFacesParallel);
    p.planeN = ray3d::Scale(ray3d::Normalize(bis), -1.0);
    p.planeD = ray3d::Dot(p.planeN, ray3d::Add(p0, ray3d::Scale(p.u[0], distance)));
    plans.push_back(p);
  }

  // --- Classify every vertex a requested edge touches ----------------------------------------------
  std::map<int, std::vector<int>> atVertex;  // original vertex -> requested plan slots
  for (std::size_t i = 0; i < plans.size(); ++i)
    for (const int vk : plans[i].v)
      atVertex[vk].push_back(static_cast<int>(i));

  std::vector<int> degree(s.vertices.size(), 0);
  for (const Edge& ed : s.edges) {
    ++degree[static_cast<std::size_t>(ed.v0)];
    ++degree[static_cast<std::size_t>(ed.v1)];
  }

  std::vector<ChamferCornerPlan> corners;
  for (const auto& [vk, slots] : atVertex) {
    if (degree[static_cast<std::size_t>(vk)] != 3)
      return fail(Problem::ChamferVertexNotSimple);
    if (slots.size() == 2) {
      // The bevel would have to run off onto an edge staying sharp — a setback blend, and a
      // different construction. Same boundary the fillet draws (REQ-331 item 8).
      return fail(Problem::ChamferCornerPartial);
    }
    if (slots.size() != 3)
      continue;  // an open end; closed by a straight edge against the planar end face

    // Orthogonality, and NOT for the fillet's reason: there is no patch here. What needs it is the
    // tangent vertex below, which is the meet of two cut lines only when their `u` vectors are
    // perpendicular (amendment (k)(4)).
    std::vector<int> fs;
    for (const int si : slots)
      for (const int f : plans[static_cast<std::size_t>(si)].face)
        fs.push_back(f);
    std::sort(fs.begin(), fs.end());
    fs.erase(std::unique(fs.begin(), fs.end()), fs.end());
    if (fs.size() != 3)
      return fail(Problem::ChamferVertexNotSimple);
    Vec3 nrm[3];
    for (int i = 0; i < 3; ++i) {
      const Face& f = s.faces[static_cast<std::size_t>(fs[static_cast<std::size_t>(i)])];
      if (f.surface.kind != SurfaceKind::Plane)
        return fail(Problem::ChamferFaceNotPlanar);
      nrm[i] = ray3d::Normalize(f.surface.frame.zAxis);
    }
    for (int i = 0; i < 3; ++i)
      if (std::fabs(ray3d::Dot(nrm[i], nrm[(i + 1) % 3])) > 1e-9)
        return fail(Problem::ChamferCornerNotOrthogonal);

    // Where the three bevels meet. Three planes, one point — the whole reason this corner needs no
    // face where the fillet needs an octant.
    const ChamferEdgePlan& q0 = plans[static_cast<std::size_t>(slots[0])];
    const ChamferEdgePlan& q1 = plans[static_cast<std::size_t>(slots[1])];
    const ChamferEdgePlan& q2 = plans[static_cast<std::size_t>(slots[2])];
    ChamferCornerPlan c;
    c.vertex = vk;
    if (!SolveThreePlanes(q0.planeN, q0.planeD, q1.planeN, q1.planeD, q2.planeN, q2.planeD, &c.point))
      return fail(Problem::ChamferCornerNotOrthogonal);
    corners.push_back(c);
  }

  // Does the whole REQUEST fit? Same question, same answer and the same helper as the fillet's —
  // ADR-046 amendment (l). The per-edge check above measures this distance against the ORIGINAL
  // face, so it sees neither what another requested edge takes out of that face nor one edge's two
  // ends eating each other. `CHAMFER 6` on the two 20-long top edges of a 20 x 10 x 8 box was
  // ACCEPTED and returned a self-intersecting solid reporting volume 880.
  //
  // What a corner takes off an edge's own LENGTH is exactly `distance`: the cut vertex there is
  // `p + distance * (u_this + u_other)`, and `u_other` runs along this edge, which is what the
  // orthogonality check a few lines above guarantees.
  {
    std::vector<int> cornerAt;
    for (const ChamferCornerPlan& c : corners)
      cornerAt.push_back(c.vertex);
    std::sort(cornerAt.begin(), cornerAt.end());

    std::vector<BlendSpan> spans;
    spans.reserve(plans.size());
    for (const ChamferEdgePlan& p : plans) {
      BlendSpan b;
      for (int k = 0; k < 2; ++k) {
        b.v[k] = p.v[k];
        b.p[k] = s.vertices[static_cast<std::size_t>(p.v[k])].p;
        b.face[k] = p.face[k];
        b.u[k] = p.u[k];
        b.consumed[k] =
            std::binary_search(cornerAt.begin(), cornerAt.end(), p.v[k]) ? distance : 0.0;
      }
      b.t = p.t;
      b.length = ray3d::Length(ray3d::Sub(b.p[1], b.p[0]));
      b.setback = distance;
      spans.push_back(b);
    }
    switch (BlendsFit(spans)) {
    case BlendFit::EdgeTooShort:
      return fail(Problem::ChamferEdgeTooShortForItsCorners);
    case BlendFit::Overlaps:
      return fail(Problem::ChamferDistanceOverlapsAnother);
    case BlendFit::Ok:
      break;
    }
  }

  // Open ends keep the fillet's rule, and for a reason that had to be worked rather than assumed
  // (amendment (k)(5)): the tangent point `p + distance*u` sits on the end face only while that face
  // is square to the edge.
  for (const auto& [vk, slots] : atVertex) {
    if (slots.size() != 1)
      continue;
    const ChamferEdgePlan& p = plans[static_cast<std::size_t>(slots.front())];
    int endFace = -1;
    for (std::size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (static_cast<int>(fi) == p.face[0] || static_cast<int>(fi) == p.face[1])
        continue;
      const std::vector<int> vs = FaceVertexSet(s, s.faces[fi]);
      if (!std::binary_search(vs.begin(), vs.end(), vk))
        continue;
      if (endFace >= 0)
        return fail(Problem::ChamferVertexNotSimple);
      endFace = static_cast<int>(fi);
    }
    if (endFace < 0)
      return fail(Problem::ChamferVertexNotSimple);
    const Face& ef = s.faces[static_cast<std::size_t>(endFace)];
    if (ef.surface.kind != SurfaceKind::Plane)
      return fail(Problem::ChamferEndFaceUnsupported);
    if (std::fabs(std::fabs(ray3d::Dot(ray3d::Normalize(ef.surface.frame.zAxis), p.t)) - 1.0) > 1e-9)
      return fail(Problem::ChamferEndFaceUnsupported);
  }

  // --- Build ----------------------------------------------------------------------------------------
  Solid r = s;

  // Every cut-line endpoint is named by (original vertex, original face). At an OPEN end it is
  // `p + distance*u` for this edge's own `u` in that face. At a CORNER two chamfered edges share the
  // face, and the point is where their two cut lines meet: `p + distance*(u1 + u2)`, which is that
  // meet exactly while the two are perpendicular — the orthogonality check above.
  std::map<std::pair<int, int>, int> cutVert;
  for (const auto& [vk, slots] : atVertex) {
    const Vec3 pv = s.vertices[static_cast<std::size_t>(vk)].p;
    std::map<int, Vec3> offsetByFace;
    for (const int si : slots) {
      const ChamferEdgePlan& p = plans[static_cast<std::size_t>(si)];
      for (int which = 0; which < 2; ++which) {
        Vec3& acc = offsetByFace[p.face[which]];
        acc = ray3d::Add(acc, ray3d::Scale(p.u[which], distance));
      }
    }
    for (const auto& [fi, off] : offsetByFace)
      cutVert.emplace(std::pair<int, int>{vk, fi}, AddVertex(&r, ray3d::Add(pv, off)));
  }

  // Each corner's meeting point, and the three edges that run out to the faces around it.
  std::map<int, int> cornerVert;                    // original vertex -> new vertex
  std::map<std::pair<int, int>, int> cornerSpoke;   // (original vertex, face) -> edge
  for (const ChamferCornerPlan& c : corners) {
    const int kv = AddVertex(&r, c.point);
    cornerVert[c.vertex] = kv;
    for (const int si : atVertex[c.vertex]) {
      const ChamferEdgePlan& p = plans[static_cast<std::size_t>(si)];
      for (const int fi : p.face) {
        const std::pair<int, int> key{c.vertex, fi};
        if (cornerSpoke.count(key))
          continue;
        cornerSpoke[key] = AddLine(&r, kv, cutVert.at(key));
      }
    }
  }

  // The cut lines, and an open end's closing edge.
  for (ChamferEdgePlan& p : plans) {
    for (int which = 0; which < 2; ++which)
      p.tan[which] = AddLine(&r, cutVert.at({p.v[0], p.face[which]}),
                             cutVert.at({p.v[1], p.face[which]}));
    for (int k = 0; k < 2; ++k)
      if (!cornerVert.count(p.v[k]))
        p.endEdge[k] = AddLine(&r, cutVert.at({p.v[k], p.face[0]}), cutVert.at({p.v[k], p.face[1]}));
  }

  // Unrequested edges ending on a touched vertex follow the face they share with the bevel. A corner
  // has no such edges — all three of its edges are requested — so this only fires at an open end.
  for (std::size_t ei = 0; ei < r.edges.size(); ++ei) {
    if (isRequested(static_cast<int>(ei)) || ei >= uses.count.size())
      continue;
    Edge& x = r.edges[ei];
    for (int endSel = 0; endSel < 2; ++endSel) {
      const int vk = endSel == 0 ? x.v0 : x.v1;
      const auto it = atVertex.find(vk);
      if (it == atVertex.end() || it->second.size() != 1)
        continue;
      const ChamferEdgePlan& p = plans[static_cast<std::size_t>(it->second.front())];
      int repl = -1;
      for (int which = 0; which < 2 && repl < 0; ++which)
        for (const int xf : uses.face[ei])
          if (xf == p.face[which]) {
            const auto vt = cutVert.find({vk, p.face[which]});
            if (vt != cutVert.end())
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

  // Each requested edge becomes its face's cut line, keeping the sense the loop already had.
  for (const ChamferEdgePlan& p : plans)
    for (int which = 0; which < 2; ++which)
      r.faces[static_cast<std::size_t>(p.face[which])]
          .loops[static_cast<std::size_t>(p.loop[which])]
          .uses[static_cast<std::size_t>(p.slot[which])] = EdgeUse{p.tan[which], p.rev[which]};

  // Close whatever gaps that left in the ORIGINAL faces. At a corner the two cut lines in a shared
  // face already meet at their common vertex, so nothing is inserted; at an open end the third face
  // gets the closing edge. Same shape as the fillet's arc insertion, with a line.
  const auto useFrom = [&](const EdgeUse& u) {
    const Edge& x = r.edges[static_cast<std::size_t>(u.edge)];
    return u.reversed ? x.v1 : x.v0;
  };
  const auto useTo = [&](const EdgeUse& u) {
    const Edge& x = r.edges[static_cast<std::size_t>(u.edge)];
    return u.reversed ? x.v0 : x.v1;
  };
  std::map<std::pair<int, int>, int> endByEnds;
  for (const ChamferEdgePlan& p : plans)
    for (const int ee : p.endEdge) {
      if (ee < 0)
        continue;
      const Edge& a = r.edges[static_cast<std::size_t>(ee)];
      endByEnds[{std::min(a.v0, a.v1), std::max(a.v0, a.v1)}] = ee;
    }
  for (Face& f : r.faces)
    for (Loop& lp : f.loops)
      for (std::size_t i = 0; i < lp.uses.size(); ++i) {
        const std::size_t j = (i + 1) % lp.uses.size();
        const int to = useTo(lp.uses[i]);
        const int from = useFrom(lp.uses[j]);
        if (to == from)
          continue;
        const auto it = endByEnds.find({std::min(to, from), std::max(to, from)});
        if (it == endByEnds.end())
          continue;  // not a gap this chamfer opened; Validate will report it
        const Edge& a = r.edges[static_cast<std::size_t>(it->second)];
        lp.uses.insert(lp.uses.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                       EdgeUse{it->second, to == a.v1});
        ++i;  // step over what was just inserted
      }

  // The bevels. Four uses at a plain edge, six where a corner V-notches an end: cut line A out,
  // whatever closes end 1, cut line B back, whatever closes end 0. The ring takes the OPPOSITE sense
  // to face A's use of its cut line, which is what leaves every edge used once each way.
  for (const ChamferEdgePlan& p : plans) {
    Loop lp;
    const int kEnd = p.rev[0] ? 1 : 0;  // the endpoint face A's cut line is walked TOWARD
    const int kFar = 1 - kEnd;
    // Close endpoint `k`, walking from face A's cut point to face B's when `aToB`, or back the
    // other way when not.
    const auto closeEnd = [&](int k, bool aToB) {
      if (!cornerVert.count(p.v[k])) {
        lp.uses.push_back(EdgeUse{p.endEdge[k], !aToB});  // stored A-side -> B-side
        return;
      }
      // A corner replaces that single edge with two spokes through the meeting point, which is what
      // makes this face a hexagon. Each spoke is stored corner -> face, so the leg TOWARD the corner
      // is reversed and the leg away from it is not.
      const int from = aToB ? p.face[0] : p.face[1];
      const int to = aToB ? p.face[1] : p.face[0];
      lp.uses.push_back(EdgeUse{cornerSpoke.at({p.v[k], from}), true});
      lp.uses.push_back(EdgeUse{cornerSpoke.at({p.v[k], to}), false});
    };
    lp.uses.push_back(EdgeUse{p.tan[0], !p.rev[0]});
    closeEnd(kEnd, true);
    lp.uses.push_back(EdgeUse{p.tan[1], kEnd == 1});
    closeEnd(kFar, false);

    Face f;
    f.surface = PlaneSurface(r.vertices[static_cast<std::size_t>(cutVert.at({p.v[0], p.face[0]}))].p,
                             p.planeN);
    f.loops.push_back(std::move(lp));
    r.faces.push_back(std::move(f));
    if (!r.shells.empty())
      r.shells.front().faces.push_back(static_cast<int>(r.faces.size()) - 1);
  }

  CompactUnused(&r);
  r.recipe = Recipe{};
  const Problem why = Validate(r);
  if (why != Problem::Ok)
    return fail(Problem::ChamferResultInvalid);
  *out = std::move(r);
  if (outWhy)
    *outWhy = Problem::Ok;
  return true;
}

bool ChamferEdge(const Solid& s, int edgeIndex, double distance, Solid* out, Problem* outWhy) {
  return ChamferEdgesGeneral(s, std::vector<int>{edgeIndex}, distance, out, outWhy);
}

bool ChamferEdges(const Solid& s, const std::vector<int>& edgeIndices, double distance, Solid* out,
                  Problem* outWhy) {
  return ChamferEdgesGeneral(s, edgeIndices, distance, out, outWhy);
}

} // namespace brep
