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


// -------------------------------------------------------------------------------------------
// Push/pull a planar face (REQ-319 / ADR-046 amendment (i), GitHub issue #148 Phase 5).
// -------------------------------------------------------------------------------------------


/// Every vertex index the loops of \p f use, once each.
void CollectFaceVertices(const Solid& s, const Face& f, std::vector<int>* out) {
  for (const Loop& loop : f.loops) {
    for (const EdgeUse& use : loop.uses) {
      if (use.edge < 0 || static_cast<size_t>(use.edge) >= s.edges.size())
        continue;
      const Edge& e = s.edges[static_cast<size_t>(use.edge)];
      for (int v : {e.v0, e.v1})
        if (std::find(out->begin(), out->end(), v) == out->end())
          out->push_back(v);
    }
  }
}

namespace {

/// One plane as `n · p = d`, with `n` unit length.
struct PushFacePlane {
  Vec3 n;
  double d = 0.0;
};

/// Where three planes cross, or false when they do not cross in a single point (two parallel, or
/// all three sharing a line). `det` is the scalar triple product of the normals; it vanishes in
/// exactly those cases, which is why the test is on it rather than on the angles between them.
bool ThreePlanePoint(const PushFacePlane& a, const PushFacePlane& b, const PushFacePlane& c, Vec3* out) {
  const Vec3 bc = ray3d::Cross(b.n, c.n);
  const double det = ray3d::Dot(a.n, bc);
  // Scale-relative: the normals are unit, so `det` is already dimensionless and a fixed floor is
  // meaningful here in a way it would not be for a length.
  if (!(std::fabs(det) > 1e-9))
    return false;
  const Vec3 ca = ray3d::Cross(c.n, a.n);
  const Vec3 ab = ray3d::Cross(a.n, b.n);
  *out = ray3d::Scale(ray3d::Add(ray3d::Add(ray3d::Scale(bc, a.d), ray3d::Scale(ca, b.d)),
                                 ray3d::Scale(ab, c.d)),
                      1.0 / det);
  return true;
}


/// A cylinder or cone wall beside the cap being moved, and what the move does to it.
///
/// A cap is a plane and moves by translation; the WALL beside it is not a plane and cannot be
/// intersected, so it is re-parameterised instead: a cylinder's stored height grows or shrinks, and
/// a cone's end radius follows its own slope (D-2026-09-04-c, the taper choice put to the user —
/// **keep the slope, let the radius change**, so pushing a cone's cap extends the same cone rather
/// than bending its wall).
struct CurvedWallMove {
  Vec3 axis;         ///< Unit, the wall's own +Z.
  Vec3 origin;       ///< The wall's frame origin (base centre) BEFORE the move.
  double height = 0.0;
  double radius = 0.0;   ///< At the wall's z = 0.
  double radius2 = 0.0;  ///< At z = height. Equal to \ref radius for a cylinder.
  bool movingTopEnd = true;  ///< Whether the cap being moved is the wall's +Z end.
  // Filled by Solve():
  Vec3 newOrigin;
  double newHeight = 0.0;
  double newRadius = 0.0;
  double newRadius2 = 0.0;
  double movedEndRadius = 0.0;  ///< The radius at the END THAT MOVED, after the move.
  double movedEndAxial = 0.0;   ///< Its axial coordinate measured from \ref newOrigin.
};

/// Work out the wall's new parameters for a cap moved by \p t along the wall's axis.
///
/// The wall spans `origin` to `origin + height*axis`. Whichever end the cap is, the other stays put
/// and the moving one travels by `t` — so a cylinder simply gets taller or shorter, and a cone's
/// moving end takes the radius its own slope puts there. False when that leaves no wall: zero or
/// negative height, or a radius at or through zero, which is a shape change rather than a size one.
bool SolveCurvedWall(CurvedWallMove* w, double t) {
  const double slope = (w->height > 1e-12) ? (w->radius2 - w->radius) / w->height : 0.0;
  if (w->movingTopEnd) {
    w->newOrigin = w->origin;
    w->newHeight = w->height + t;
    w->newRadius = w->radius;
    w->newRadius2 = w->radius + slope * w->newHeight;
    w->movedEndRadius = w->newRadius2;
    w->movedEndAxial = w->newHeight;
  } else {
    // The base end moves: the frame origin travels with it, and the radius there is what the same
    // slope gives at the new position. The top end and its radius are untouched, which is what
    // "keep the slope" means from the other side.
    w->newOrigin = ray3d::Add(w->origin, ray3d::Scale(w->axis, t));
    w->newHeight = w->height - t;
    w->newRadius = w->radius + slope * t;
    w->newRadius2 = w->radius2;
    w->movedEndRadius = w->newRadius;
    w->movedEndAxial = 0.0;
  }
  if (!(w->newHeight > 1e-9))
    return false;  // pushed flat or through itself
  if (!(w->newRadius > 1e-9) || !(w->newRadius2 > 1e-9))
    return false;  // a cone driven to or past its own apex: the cap would become a point
  return true;
}
}  // namespace

bool PushPullFace(const Solid& s, int faceIndex, double distance, Solid* out, Problem* outWhy) {
  const auto fail = [&](Problem p) {
    if (outWhy)
      *outWhy = p;
    return false;
  };
  if (!out)
    return fail(Problem::IndexOutOfRange);
  if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= s.faces.size())
    return fail(Problem::IndexOutOfRange);
  // Zero is refused rather than treated as a successful no-op: a command that reports it moved
  // something it did not is worse than one that declines (REQ-319 item 6).
  if (!std::isfinite(distance) || std::fabs(distance) <= 1e-12)
    return fail(Problem::PushPullDistanceZero);

  const Face& face = s.faces[static_cast<size_t>(faceIndex)];

  // --- A CYLINDER WALL: the one curved face that CAN be pushed, because it has exactly one
  // --- parameter a push corresponds to (REQ-319 increment 4 / D-2026-09-04-e).
  //
  // A wall push is not a translation, and that is the whole reason it needs its own path rather
  // than a wider tolerance somewhere: the outward normal points a different way at every point of
  // the surface, so there is no single direction to move along. What the gesture means is a RADIUS
  // change — every point moves along its own normal by the same amount, which is exactly what
  // adding to the radius does.
  //
  // A cone is deliberately NOT here. Offsetting a cone along its own normal moves both radii by
  // `d / cos(half-angle)` and leaves the apex where it was: an offset surface, not a radius change,
  // and a second decision about what the gesture should mean. Its own increment if it is ever
  // wanted.
  if (face.surface.kind == SurfaceKind::Cylinder) {
    const Surface& w0 = face.surface;
    const Vec3 axis = ray3d::Normalize(w0.frame.zAxis);
    // `inward` flips which side the material is on (REQ-314 B2a): for a hole, the outward normal
    // points AT the axis, so pushing outward makes the hole smaller. Honoured here or a positive
    // push would widen a hole while widening a boss — a sign error that reads as a working feature
    // half the time.
    const double newRadius = w0.radius + (w0.inward ? -distance : distance);
    if (!(newRadius > 1e-9))
      return fail(Problem::PushPullCurvedDegenerate);

    // Every face sharing this surface is part of the same wall — a full cylinder is stored as two
    // half-faces, and pushing one without the other would leave the solid with two different radii
    // meeting along a seam.
    std::vector<int> wallFaces;
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      const Surface& sf = s.faces[fi].surface;
      if (sf.kind != SurfaceKind::Cylinder || sf.inward != w0.inward)
        continue;
      if (std::fabs(sf.radius - w0.radius) > 1e-9 || std::fabs(sf.height - w0.height) > 1e-9)
        continue;
      if (ray3d::Length(ray3d::Sub(sf.frame.origin, w0.frame.origin)) > 1e-9)
        continue;
      if (std::fabs(std::fabs(ray3d::Dot(ray3d::Normalize(sf.frame.zAxis), axis)) - 1.0) > 1e-9)
        continue;
      wallFaces.push_back(static_cast<int>(fi));
    }
    std::vector<int> wallVerts;
    for (int fi : wallFaces)
      CollectFaceVertices(s, s.faces[static_cast<size_t>(fi)], &wallVerts);
    if (wallVerts.empty())
      return fail(Problem::FaceHasNoLoop);
    const auto onWall = [&](int v) {
      return std::find(wallVerts.begin(), wallVerts.end(), v) != wallVerts.end();
    };

    // Every other face at those corners must be a CAP — a plane square to the axis. Such a plane is
    // unchanged by the move: its boundary slides around inside it, which is exactly what growing a
    // circle within its own plane does. A slanted plane would have to be re-solved, and a second
    // curved surface would have its own parameter to argue about.
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (std::find(wallFaces.begin(), wallFaces.end(), static_cast<int>(fi)) != wallFaces.end())
        continue;
      std::vector<int> fv;
      CollectFaceVertices(s, s.faces[fi], &fv);
      if (!std::any_of(fv.begin(), fv.end(), onWall))
        continue;
      if (s.faces[fi].surface.kind != SurfaceKind::Plane)
        return fail(Problem::PushPullNeighbourCurved);
      if (std::fabs(std::fabs(ray3d::Dot(ray3d::Normalize(s.faces[fi].surface.frame.zAxis), axis)) - 1.0) >
          1e-9)
        return fail(Problem::PushPullVertexUnsolvable);  // a cut face, not a cap
    }

    Solid rw = s;
    // Each corner keeps its height along the axis and its angle around it, and takes the new
    // radius. Nothing translates: the whole point is that every point of the wall moves a different
    // way and the same amount.
    for (int v : wallVerts) {
      const Vec3 rel = ray3d::Sub(s.vertices[static_cast<size_t>(v)].p, w0.frame.origin);
      const double axial = ray3d::Dot(rel, axis);
      Vec3 radial = ray3d::Sub(rel, ray3d::Scale(axis, axial));
      const double rl = ray3d::Length(radial);
      if (!(rl > 1e-12))
        return fail(Problem::PushPullVertexUnsolvable);  // a corner on the axis has no direction
      radial = ray3d::Scale(radial, newRadius / rl);
      rw.vertices[static_cast<size_t>(v)].p =
          ray3d::Add(ray3d::Add(w0.frame.origin, ray3d::Scale(axis, axial)), radial);
    }
    // The cap boundary arcs grow with it. Their centres stay on the axis where they were — a cap's
    // PLANE does not move when the wall's radius changes, which is the difference between this and
    // the cap push, where the plane moves and the radius does not.
    for (Edge& e : rw.edges) {
      if (e.kind == CurveKind::Line || !onWall(e.v0) || !onWall(e.v1))
        continue;
      if (e.kind != CurveKind::Arc)
        return fail(Problem::PushPullNeighbourCurved);
      e.radius = newRadius;
    }
    for (int fi : wallFaces) {
      Surface& sf = rw.faces[static_cast<size_t>(fi)].surface;
      sf.radius = newRadius;
      sf.radius2 = newRadius;
    }

    rw.recipe = Recipe{};
    const Problem whyW = Validate(rw);
    if (whyW != Problem::Ok)
      return fail(Problem::PushPullResultInvalid);
    *out = std::move(rw);
    if (outWhy)
      *outWhy = Problem::Ok;
    return true;
  }

  if (face.surface.kind != SurfaceKind::Plane)
    return fail(Problem::PushPullFaceKindUnsupported);

  // The push direction is the face's own outward normal. `Surface::inward` flips which side the
  // material is on (REQ-314 B2a), and it has to be honoured here or a positive distance would grow
  // an inward-facing face the wrong way — the one place a sign error would look like a working
  // feature that pushes backwards.
  Vec3 dir = face.surface.frame.zAxis;
  if (face.surface.inward)
    dir = ray3d::Scale(dir, -1.0);
  const double dirLen = ray3d::Length(dir);
  if (!(dirLen > 1e-12))
    return fail(Problem::DegenerateFrame);
  dir = ray3d::Scale(dir, 1.0 / dirLen);

  std::vector<int> moved;
  CollectFaceVertices(s, face, &moved);
  if (moved.empty())
    return fail(Problem::FaceHasNoLoop);
  const auto isMoved = [&](int v) { return std::find(moved.begin(), moved.end(), v) != moved.end(); };

  // Every face that touches a moving corner must be a PLANE, because re-solving that corner means
  // intersecting the surfaces meeting there and a curved surface is not a plane to intersect. This
  // is the check ADR-046 amendment (i) is about — see the header for the measured cost of removing
  // it (a cylinder cap pushed 3 ft validates cleanly and reports a volume 15% wrong).
  //
  // Note what is NOT required any more: the neighbour does not have to be PARALLEL to the push. The
  // first version of this operation translated corners along the push, which only works when every
  // neighbour contains that direction — true of a box, false of a pyramid, whose faces are all
  // planes and none of which could be pushed at all. Re-solving lifted that restriction.
  std::vector<PushFacePlane> planesAt;  // reused per vertex
  // A CURVED neighbour is not automatically a refusal any more. A cylinder or cone wall whose axis
  // runs along the push is re-parameterised instead of intersected — its stored height grows, and a
  // cone's moving end takes the radius its own slope puts there. That is the "make this cylinder
  // taller" gesture, and it is the shape of every other curved case that is NOT supported: a wall
  // pushed sideways is a radius change, a sphere or a torus has no such parameter at all, and an
  // oblique axis is not a translation of anything.
  std::vector<int> curvedWallFaces;
  for (size_t fi = 0; fi < s.faces.size(); ++fi) {
    const Face& nb = s.faces[fi];
    if (nb.surface.kind == SurfaceKind::Plane)
      continue;
    std::vector<int> nbVerts;
    CollectFaceVertices(s, nb, &nbVerts);
    if (!std::any_of(nbVerts.begin(), nbVerts.end(), isMoved))
      continue;
    if (nb.surface.kind != SurfaceKind::Cylinder && nb.surface.kind != SurfaceKind::Cone)
      return fail(Problem::PushPullNeighbourCurved);
    const double axDot = ray3d::Dot(ray3d::Normalize(nb.surface.frame.zAxis), dir);
    if (std::fabs(std::fabs(axDot) - 1.0) > 1e-9)
      return fail(Problem::PushPullNeighbourCurved);  // pushing across the axis, not along it
    curvedWallFaces.push_back(static_cast<int>(fi));
  }

  if (!curvedWallFaces.empty()) {
    // Every wall touching the move must be the SAME wall — the halves a full cylinder is stored as
    // share one surface, differing only in their angular span. Two genuinely different walls at one
    // cap would each want their own re-parameterisation and could disagree about where the boundary
    // lands, so that is refused rather than resolved arbitrarily.
    const Surface& w0 = s.faces[static_cast<size_t>(curvedWallFaces.front())].surface;
    for (int fi : curvedWallFaces) {
      const Surface& sf = s.faces[static_cast<size_t>(fi)].surface;
      if (sf.kind != w0.kind || sf.inward != w0.inward ||
          std::fabs(sf.radius - w0.radius) > 1e-9 || std::fabs(sf.radius2 - w0.radius2) > 1e-9 ||
          std::fabs(sf.height - w0.height) > 1e-9 ||
          ray3d::Length(ray3d::Sub(sf.frame.origin, w0.frame.origin)) > 1e-9)
        return fail(Problem::PushPullNeighbourCurved);
    }
    // Every face at a moving corner must be the cap itself or one of those walls. A corner that also
    // touches an unrelated PLANE — a sliced cylinder — has both a plane constraint and a surface
    // one, and satisfying both is a different solve. Refused rather than silently ignoring one.
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (static_cast<int>(fi) == faceIndex ||
          std::find(curvedWallFaces.begin(), curvedWallFaces.end(), static_cast<int>(fi)) !=
              curvedWallFaces.end())
        continue;
      std::vector<int> fv;
      CollectFaceVertices(s, s.faces[fi], &fv);
      if (std::any_of(fv.begin(), fv.end(), isMoved))
        return fail(Problem::PushPullVertexUnsolvable);
    }

    CurvedWallMove wall;
    wall.axis = ray3d::Normalize(w0.frame.zAxis);
    wall.origin = w0.frame.origin;
    wall.height = w0.height;
    wall.radius = w0.radius;
    wall.radius2 = w0.radius2;
    // Which end is moving: the cap's outward normal points along the wall's axis at the top end and
    // against it at the base. `t` is the signed travel measured in the wall's own axis direction, so
    // both ends are one piece of arithmetic rather than two.
    wall.movingTopEnd = ray3d::Dot(dir, wall.axis) > 0.0;
    const double t = distance * ray3d::Dot(dir, wall.axis);
    if (!SolveCurvedWall(&wall, t))
      return fail(Problem::PushPullCurvedDegenerate);

    Solid rc = s;
    const Vec3 deltaC = ray3d::Scale(dir, distance);
    // The corners: each keeps its angular position, takes the axial coordinate of the end it is on,
    // and takes that end's NEW radius. On a cylinder the radius is unchanged and this is a pure
    // translation; on a cone the corner also moves radially, which is the taper following its slope.
    for (int v : moved) {
      const Vec3 p = s.vertices[static_cast<size_t>(v)].p;
      const Vec3 rel = ray3d::Sub(p, wall.origin);
      Vec3 radial = ray3d::Sub(rel, ray3d::Scale(wall.axis, ray3d::Dot(rel, wall.axis)));
      const double rl = ray3d::Length(radial);
      if (!(rl > 1e-12))
        return fail(Problem::PushPullVertexUnsolvable);  // a corner ON the axis has no direction
      radial = ray3d::Scale(radial, wall.movedEndRadius / rl);
      rc.vertices[static_cast<size_t>(v)].p =
          ray3d::Add(ray3d::Add(wall.newOrigin, ray3d::Scale(wall.axis, wall.movedEndAxial)), radial);
    }
    // The cap's own plane, and its boundary arcs: an arc's centre sits on the axis at the moved
    // end, and its radius is that end's radius. Both have to move or the cap's boundary stops
    // matching its own plane — the same inconsistency the plane path guards against, in the one
    // place a curve can express it.
    rc.faces[static_cast<size_t>(faceIndex)].surface.frame.origin =
        ray3d::Add(face.surface.frame.origin, deltaC);
    const Vec3 movedEndCentre =
        ray3d::Add(wall.newOrigin, ray3d::Scale(wall.axis, wall.movedEndAxial));
    for (Edge& e : rc.edges) {
      if (!isMoved(e.v0) || !isMoved(e.v1))
        continue;
      if (e.kind == CurveKind::Line)
        continue;  // a seam chord across the cap: its endpoints already moved
      if (e.kind != CurveKind::Arc)
        return fail(Problem::PushPullNeighbourCurved);
      e.frame.origin = movedEndCentre;
      e.radius = wall.movedEndRadius;
    }
    // The wall itself.
    for (int fi : curvedWallFaces) {
      Surface& sf = rc.faces[static_cast<size_t>(fi)].surface;
      sf.frame.origin = wall.newOrigin;
      sf.height = wall.newHeight;
      sf.radius = wall.newRadius;
      sf.radius2 = wall.newRadius2;
    }

    rc.recipe = Recipe{};
    const Problem whyC = Validate(rc);
    if (whyC != Problem::Ok)
      return fail(Problem::PushPullResultInvalid);
    *out = std::move(rc);
    if (outWhy)
      *outWhy = Problem::Ok;
    return true;
  }

  // The moved face's plane, translated. Every corner is then solved against THIS rather than the
  // original, which is what actually moves the face.
  const Vec3 movedOrigin = ray3d::Add(face.surface.frame.origin, ray3d::Scale(dir, distance));
  const PushFacePlane movedPlane{dir, ray3d::Dot(dir, movedOrigin)};

  Solid r = s;
  for (int v : moved) {
    // Collect the plane of every face using this vertex. The moved face contributes its NEW plane;
    // the others contribute the planes they already have, which is what keeps their own boundaries
    // on their own surfaces.
    planesAt.clear();
    planesAt.push_back(movedPlane);
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (static_cast<int>(fi) == faceIndex)
        continue;
      std::vector<int> fv;
      CollectFaceVertices(s, s.faces[fi], &fv);
      if (std::find(fv.begin(), fv.end(), v) == fv.end())
        continue;
      const Surface& sf = s.faces[fi].surface;
      Vec3 n = sf.frame.zAxis;
      const double nl = ray3d::Length(n);
      if (!(nl > 1e-12))
        return fail(Problem::DegenerateFrame);
      n = ray3d::Scale(n, 1.0 / nl);
      planesAt.push_back(PushFacePlane{n, ray3d::Dot(n, sf.frame.origin)});
    }
    if (planesAt.size() < 3)
      return fail(Problem::PushPullVertexUnsolvable);  // a corner needs three planes to be a point

    // Solve with the best-conditioned triple: the three whose normals span space most squarely.
    // Taking simply the first three would pick a near-degenerate triple on any vertex where two
    // faces happen to be nearly coplanar, and produce a corner far from where it belongs.
    Vec3 solved{};
    double bestDet = 0.0;
    bool found = false;
    for (size_t a = 0; a < planesAt.size(); ++a)
      for (size_t b = a + 1; b < planesAt.size(); ++b)
        for (size_t c = b + 1; c < planesAt.size(); ++c) {
          const double det = std::fabs(
              ray3d::Dot(planesAt[a].n, ray3d::Cross(planesAt[b].n, planesAt[c].n)));
          if (det <= bestDet)
            continue;
          Vec3 p;
          if (!ThreePlanePoint(planesAt[a], planesAt[b], planesAt[c], &p))
            continue;
          bestDet = det;
          solved = p;
          found = true;
        }
    if (!found)
      return fail(Problem::PushPullVertexUnsolvable);

    // Every OTHER plane at this corner must also pass through the answer. When more than three
    // faces meet — a pyramid's apex, four planes — moving one of them generally leaves no single
    // point satisfying all of them, and the corner would have to split into several. That is a
    // topology change and a different operation, so it is refused rather than approximated.
    //
    // Scale-relative, because a residual of a millimetre means something different on a 1 ft solid
    // than on one at state-plane magnitude.
    const double tol = 1e-9 * (1.0 + ray3d::Length(solved));
    for (const PushFacePlane& pe : planesAt)
      if (std::fabs(ray3d::Dot(pe.n, solved) - pe.d) > tol)
        return fail(Problem::PushPullVertexUnsolvable);

    r.vertices[static_cast<size_t>(v)].p = solved;
  }

  // The face's own plane travels with its boundary. Without this the corners would move and the
  // surface would stay — the very inconsistency the precondition above prevents on a neighbour,
  // committed on the moved face itself.
  r.faces[static_cast<size_t>(faceIndex)].surface.frame.origin = movedOrigin;

  // An ARC edge carries a centre of its own. One can only exist where a curved face does, and a
  // curved face touching the move was refused above — so an arc edge with both ends on the moved
  // face cannot occur here. Refused rather than assumed: an edge that curves and is silently left
  // behind is exactly the class of error this operation exists to avoid.
  for (const Edge& e : r.edges)
    if (e.kind != CurveKind::Line && isMoved(e.v0) && isMoved(e.v1))
      return fail(Problem::PushPullNeighbourCurved);

  // The recipe is DROPPED, not updated (REQ-319 item 9). A pushed box is not the box its recipe
  // describes, and a recipe that no longer describes its solid reads as authoritative while being
  // false. ADR-045 already made it optional and never consulted by validity, mass properties or
  // tessellation, so nothing downstream misses it.
  r.recipe = Recipe{};

  // ADR-046 (d): validate before returning, never mutate the input. A push far enough to collapse
  // or invert the solid is an ordinary user gesture, and this is what turns it into a refusal.
  const Problem why = Validate(r);
  if (why != Problem::Ok)
    return fail(Problem::PushPullResultInvalid);
  *out = std::move(r);
  if (outWhy)
    *outWhy = Problem::Ok;
  return true;
}

namespace {

// ---------------------------------------------------------------------------------------------
// Moving a VERTEX or an EDGE (REQ-333 / ADR-046 amendment (n), GitHub issue #148 acceptance 3).
//
// The header states what these mean and why; this is the machinery. One core does the work:
// offset some planar faces along their own normals and re-solve every corner they touch. That is
// `PushPullFace`'s algorithm with its "exactly one face moves" assumption lifted, which is the whole
// content of the generalisation — a vertex offsets THREE planes and an edge offsets TWO.
// ---------------------------------------------------------------------------------------------


/// A face's OUTWARD unit normal, honouring \ref Surface::inward. False on a degenerate frame.
bool FaceOutwardNormal(const Face& f, Vec3* out) {
  Vec3 n = f.surface.frame.zAxis;
  if (f.surface.inward)
    n = ray3d::Scale(n, -1.0);
  const double len = ray3d::Length(n);
  if (!(len > 1e-12))
    return false;
  *out = ray3d::Scale(n, 1.0 / len);
  return true;
}

/// The failure codes a caller of \ref OffsetPlanarFacesAndResolve wants reported, so the core can be
/// shared without any caller inheriting another's vocabulary. `PushPullFace` says
/// "PushPullVertexUnsolvable" and these operations say "MoveVertexNotThreePlanes" about the same
/// geometric situation, and both are right for their own user.
struct ResolveProblems {
  Problem neighbourCurved;
  Problem cornerUnsolvable;
  Problem resultInvalid;
};

/// Offset each listed planar face along its own outward normal by its distance, then re-solve every
/// corner those faces touch as the meeting point of the planes around it.
///
/// The re-solve is `PushPullFace`'s, and the two pieces that make it correct are kept: the
/// BEST-CONDITIONED triple of planes is used rather than the first three (any vertex where two faces
/// are nearly coplanar would otherwise solve from a near-degenerate triple and land far from where
/// it belongs), and every OTHER plane at the corner must then pass through the answer, because more
/// than three planes generally have no common point once one of them moves.
bool OffsetPlanarFacesAndResolve(const Solid& s, const std::vector<std::pair<int, double>>& offsets,
                                 const ResolveProblems& probs, Solid* out, Problem* outWhy) {
  const auto fail = [&](Problem p) { return Fail(p, outWhy); };
  if (!out || offsets.empty())
    return false;

  // The new plane of each offset face, and the set of corners that therefore move.
  std::vector<int> offsetFaces;
  std::vector<PushFacePlane> newPlanes;
  std::vector<Vec3> newOrigins;
  std::vector<int> moved;
  for (const auto& od : offsets) {
    const int fi = od.first;
    if (fi < 0 || static_cast<size_t>(fi) >= s.faces.size())
      return fail(Problem::IndexOutOfRange);
    const Face& f = s.faces[static_cast<size_t>(fi)];
    if (f.surface.kind != SurfaceKind::Plane)
      return fail(probs.neighbourCurved);
    Vec3 n{};
    if (!FaceOutwardNormal(f, &n))
      return fail(Problem::DegenerateFrame);
    // The frame origin TRANSLATES with the plane rather than being re-derived from (n, d). Any point
    // on the plane satisfies the equation, but a re-derived one lands at the foot of the
    // perpendicular from the world origin — arbitrarily far from the face, and at state-plane
    // magnitudes that is a large number standing in for a small one.
    const Vec3 origin = ray3d::Add(f.surface.frame.origin, ray3d::Scale(n, od.second));
    offsetFaces.push_back(fi);
    newPlanes.push_back(PushFacePlane{n, ray3d::Dot(n, origin)});
    newOrigins.push_back(origin);
    CollectFaceVertices(s, f, &moved);
  }
  std::sort(moved.begin(), moved.end());
  moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
  if (moved.empty())
    return fail(Problem::FaceHasNoLoop);

  // Every face touching a moving corner must be a PLANE: re-solving means intersecting the surfaces
  // there, and a curved surface is not a plane to intersect. Push/pull can re-parameterise a wall
  // along its own axis; a corner dragged in an arbitrary direction has no such single parameter.
  {
    std::vector<int> fv;
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      if (s.faces[fi].surface.kind == SurfaceKind::Plane)
        continue;
      fv.clear();  // `CollectFaceVertices` APPENDS
      CollectFaceVertices(s, s.faces[fi], &fv);
      for (int v : fv) {
        if (std::binary_search(moved.begin(), moved.end(), v))
          return fail(probs.neighbourCurved);
      }
    }
  }

  Solid r = s;
  std::vector<PushFacePlane> planesAt;
  std::vector<int> fv;
  for (int v : moved) {
    planesAt.clear();
    for (size_t fi = 0; fi < s.faces.size(); ++fi) {
      fv.clear();  // `CollectFaceVertices` APPENDS
      CollectFaceVertices(s, s.faces[fi], &fv);
      if (std::find(fv.begin(), fv.end(), v) == fv.end())
        continue;
      // An offset face contributes its NEW plane; every other face contributes the plane it already
      // has, which is what keeps its own boundary on its own surface.
      const auto it = std::find(offsetFaces.begin(), offsetFaces.end(), static_cast<int>(fi));
      if (it != offsetFaces.end()) {
        planesAt.push_back(newPlanes[static_cast<size_t>(it - offsetFaces.begin())]);
        continue;
      }
      const Surface& sf = s.faces[fi].surface;
      Vec3 n = sf.frame.zAxis;
      const double nl = ray3d::Length(n);
      if (!(nl > 1e-12))
        return fail(Problem::DegenerateFrame);
      n = ray3d::Scale(n, 1.0 / nl);
      planesAt.push_back(PushFacePlane{n, ray3d::Dot(n, sf.frame.origin)});
    }
    if (planesAt.size() < 3)
      return fail(probs.cornerUnsolvable);  // a corner needs three planes to be a point

    Vec3 solved{};
    double bestDet = 0.0;
    bool found = false;
    for (size_t a = 0; a < planesAt.size(); ++a)
      for (size_t b = a + 1; b < planesAt.size(); ++b)
        for (size_t c = b + 1; c < planesAt.size(); ++c) {
          const double det =
              std::fabs(ray3d::Dot(planesAt[a].n, ray3d::Cross(planesAt[b].n, planesAt[c].n)));
          if (det <= bestDet)
            continue;
          Vec3 p;
          if (!ThreePlanePoint(planesAt[a], planesAt[b], planesAt[c], &p))
            continue;
          bestDet = det;
          solved = p;
          found = true;
        }
    if (!found)
      return fail(probs.cornerUnsolvable);

    // Scale-relative, because a residual of a millimetre means something different on a 1 ft solid
    // than on one at state-plane magnitude.
    const double tol = 1e-9 * (1.0 + ray3d::Length(solved));
    for (const PushFacePlane& pe : planesAt)
      if (std::fabs(ray3d::Dot(pe.n, solved) - pe.d) > tol)
        return fail(probs.cornerUnsolvable);

    r.vertices[static_cast<size_t>(v)].p = solved;
  }

  // The offset faces' own surfaces follow their boundaries. Without this the face would report a
  // plane its own corners no longer lie on — the defect the whole precondition above exists for.
  for (size_t i = 0; i < offsetFaces.size(); ++i)
    r.faces[static_cast<size_t>(offsetFaces[i])].surface.frame.origin = newOrigins[i];

  // Dropped, not quietly updated (REQ-319 item 9): a box with a corner pulled out is no longer the
  // box its recipe describes, and a recipe that no longer describes its solid reads as authoritative
  // while being false.
  r.recipe = Recipe{};
  const Problem why = Validate(r);
  if (why != Problem::Ok)
    return fail(probs.resultInvalid);
  *out = std::move(r);
  return Succeed(outWhy);
}

constexpr ResolveProblems kMoveProblems{Problem::MoveSubObjectNeighbourCurved,
                                        Problem::MoveSubObjectCornerUnsolvable,
                                        Problem::MoveSubObjectResultInvalid};

}  // namespace

void FacesAtVertex(const Solid& s, int vertexIndex, std::vector<int>* out) {
  if (!out)
    return;
  out->clear();
  std::vector<int> fv;
  for (size_t fi = 0; fi < s.faces.size(); ++fi) {
    fv.clear();  // `CollectFaceVertices` APPENDS — a reused buffer must be emptied first
    CollectFaceVertices(s, s.faces[fi], &fv);
    if (std::find(fv.begin(), fv.end(), vertexIndex) != fv.end())
      out->push_back(static_cast<int>(fi));
  }
}

bool FacesAlongEdge(const Solid& s, int edgeIndex, int* outA, int* outB) {
  if (!outA || !outB)
    return false;
  int found[2] = {-1, -1};
  int n = 0;
  for (size_t fi = 0; fi < s.faces.size(); ++fi) {
    bool uses = false;
    for (const Loop& lp : s.faces[fi].loops) {
      for (const EdgeUse& eu : lp.uses) {
        if (eu.edge == edgeIndex) {
          uses = true;
          break;
        }
      }
      if (uses)
        break;
    }
    if (!uses)
      continue;
    if (n >= 2)
      return false;  // three faces on one edge is not a manifold solid
    found[n++] = static_cast<int>(fi);
  }
  if (n != 2)
    return false;
  *outA = found[0];
  *outB = found[1];
  return true;
}

bool MoveVertex(const Solid& s, int vertexIndex, const Vec3& delta, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (vertexIndex < 0 || static_cast<size_t>(vertexIndex) >= s.vertices.size())
    return Fail(Problem::IndexOutOfRange, outWhy);
  if (!FinitePoint(delta) || ray3d::Length(delta) <= 1e-12)
    return Fail(Problem::MoveSubObjectNoMotion, outWhy);

  std::vector<int> faces;
  FacesAtVertex(s, vertexIndex, &faces);
  // EXACTLY three, and both bounds matter. Fewer than three planes do not meet in a point at all;
  // more than three — a pyramid's apex — generally have no common point once one of them is offset,
  // so the corner would have to split into several. A topology change, and a different operation.
  if (faces.size() != 3)
    return Fail(Problem::MoveVertexNotThreePlanes, outWhy);

  std::vector<std::pair<int, double>> offsets;
  for (int fi : faces) {
    const Face& f = s.faces[static_cast<size_t>(fi)];
    if (f.surface.kind != SurfaceKind::Plane)
      return Fail(Problem::MoveSubObjectNeighbourCurved, outWhy);
    Vec3 n{};
    if (!FaceOutwardNormal(f, &n))
      return Fail(Problem::DegenerateFrame, outWhy);
    // Each plane slides by the part of the move that is perpendicular to it. Together the three
    // offsets put their intersection at exactly `p + delta`.
    offsets.push_back({fi, ray3d::Dot(delta, n)});
  }
  // Three planes whose normals do not span space cannot be steered to an arbitrary point: they
  // share a line or are parallel, and the corner is not a corner.
  {
    Vec3 n0{}, n1{}, n2{};
    if (!FaceOutwardNormal(s.faces[static_cast<size_t>(faces[0])], &n0) ||
        !FaceOutwardNormal(s.faces[static_cast<size_t>(faces[1])], &n1) ||
        !FaceOutwardNormal(s.faces[static_cast<size_t>(faces[2])], &n2))
      return Fail(Problem::DegenerateFrame, outWhy);
    if (std::fabs(ray3d::Dot(n0, ray3d::Cross(n1, n2))) < 1e-9)
      return Fail(Problem::MoveVertexNotThreePlanes, outWhy);
  }
  return OffsetPlanarFacesAndResolve(s, offsets, kMoveProblems, out, outWhy);
}

bool MoveEdge(const Solid& s, int edgeIndex, const Vec3& delta, Solid* out, Problem* outWhy) {
  if (!out)
    return false;  // a null output is a caller bug, not a user-facing reason: outWhy is left alone
  if (edgeIndex < 0 || static_cast<size_t>(edgeIndex) >= s.edges.size())
    return Fail(Problem::IndexOutOfRange, outWhy);
  if (!FinitePoint(delta) || ray3d::Length(delta) <= 1e-12)
    return Fail(Problem::MoveSubObjectNoMotion, outWhy);

  int fa = -1;
  int fb = -1;
  if (!FacesAlongEdge(s, edgeIndex, &fa, &fb))
    return Fail(Problem::MoveEdgeFacesParallel, outWhy);
  const std::vector<int> faces{fa, fb};

  Vec3 n0{}, n1{};
  const Face& f0 = s.faces[static_cast<size_t>(faces[0])];
  const Face& f1 = s.faces[static_cast<size_t>(faces[1])];
  if (f0.surface.kind != SurfaceKind::Plane || f1.surface.kind != SurfaceKind::Plane)
    return Fail(Problem::MoveSubObjectNeighbourCurved, outWhy);
  if (!FaceOutwardNormal(f0, &n0) || !FaceOutwardNormal(f1, &n1))
    return Fail(Problem::DegenerateFrame, outWhy);
  // Parallel or coplanar planes define no line for the edge to lie on, so there is nothing to move
  // the edge ALONG and no two-freedom system to solve.
  if (ray3d::Length(ray3d::Cross(n0, n1)) < 1e-9)
    return Fail(Problem::MoveEdgeFacesParallel, outWhy);

  const double t0 = ray3d::Dot(delta, n0);
  const double t1 = ray3d::Dot(delta, n1);
  // A move entirely ALONG the edge lands here: the edge direction lies in both faces, so it is
  // perpendicular to both normals and contributes nothing to either offset. It should not — an edge
  // slid along its own line is the same edge — so this is refused as "no motion" rather than
  // reported as a move that did nothing.
  if (std::fabs(t0) <= 1e-12 && std::fabs(t1) <= 1e-12)
    return Fail(Problem::MoveSubObjectNoMotion, outWhy);

  const std::vector<std::pair<int, double>> offsets{{faces[0], t0}, {faces[1], t1}};
  return OffsetPlanarFacesAndResolve(s, offsets, kMoveProblems, out, outWhy);
}

} // namespace brep
