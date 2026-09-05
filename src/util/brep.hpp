#pragma once

#include "nurbs.hpp"
#include "ucs.hpp"

#include <cstdint>
#include <vector>

/// The boundary-representation solid kernel (REQ-313 / ADR-045, GitHub issue #146 — Phase 3 of #120).
///
/// Pure and dependency-free — no GL, no ImGui, no CAD session state, no `AppCommandState` — so every
/// primitive, every validity invariant and every mass property is unit-testable without a window.
/// That is the same ADR-002 layering pressure that already governs `ray3d`, `ucs` and `Camera`, and
/// issue #120 states it as an explicit architectural constraint: *"the geometry engine should be
/// usable without a graphics context."*
///
/// **A face carries an analytic surface, not a bag of triangles.** A whole sphere is ONE face whose
/// surface is a sphere; a cylinder side is one face per half. That is what makes the volume of a
/// sphere exact rather than a facet count, keeps a saved solid small, and keeps tessellation a
/// *derived* representation the display may regenerate at any quality without touching the solid
/// (#120: "changing tessellation quality should not modify the underlying solid").
///
/// Everything here is `double` and frame-agnostic: the kernel never learns about the document
/// origin. The caller decides which frame it hands in, and the narrowing to `float` local storage
/// happens once, above this layer, at local magnitude (REQ-101).
///
/// Convention matches the rest of the codebase: +X east, +Y north, +Z up, right-handed.
namespace brep {

using ray3d::Vec3;

// ---------------------------------------------------------------------------------------------
// Geometry carriers. A face's surface and an edge's curve are *described*, never faceted.
// ---------------------------------------------------------------------------------------------

/// The surface a face lies on. `Nurbs` (REQ-315 / ADR-048, D-2026-09-03-b) is the one **freeform**
/// kind — a rational tensor-product B-spline patch carried in \ref Surface::patch — that sweep and
/// loft raise over their profiles. Its volume and area are computed by numerical quadrature rather
/// than a closed form (ADR-045 (b) as widened), and it is the first surface kind an older `.gs`
/// reader cannot tolerate, so it bumps `kGsFormatVersion` to 4.
enum class SurfaceKind : std::uint8_t { Plane, Cylinder, Cone, Sphere, Torus, Nurbs };

/// The surface a \ref Face lies on, plus the frame it is expressed in.
///
/// `frame.zAxis` is always the surface's axis, and for \ref SurfaceKind::Plane it is the face's
/// **outward** normal — so a box's bottom face carries a frame whose Z points down. There is no
/// separate "reversed" flag: a face's outward direction is a property of the surface it was built
/// with, which is one fewer thing that can disagree with the topology.
struct Surface {
  SurfaceKind kind = SurfaceKind::Plane;

  /// Plane:    origin lies on the plane, Z is the outward normal.
  /// Cylinder: origin is the base centre, Z is the axis (outward is +radial).
  /// Cone:     origin is the base centre, Z is the axis; \ref radius at z=0, \ref radius2 at z=h.
  /// Sphere:   origin is the centre, Z is the pole axis.
  /// Torus:    origin is the centre, Z is the axis of revolution.
  ucs::Ucs frame;

  double radius = 0.0;   ///< Cylinder r; Cone base r; Sphere R; Torus **major** R. Unused for Plane.
  double radius2 = 0.0;  ///< Cone top r; Torus **minor** r. Unused otherwise.
  double height = 0.0;   ///< Cylinder / Cone height along +Z from the frame origin. Unused otherwise.

  /// **Freeform** surface for \ref SurfaceKind::Nurbs (REQ-315 / ADR-048) — a rational tensor-product
  /// B-spline patch, in the solid's own storage coordinates like every vertex (so \ref Translate
  /// moves its control points and `.gs` stores them directly). Empty and unused for every analytic
  /// kind. \ref Face::uStart / uEnd / vStart / vEnd carry the patch **parameter** rectangle the face
  /// occupies, in place of the angular spans the analytic kinds put there.
  nurbs::Patch patch;

  /// **Inward-facing** curved face (REQ-314 B2a / ADR-045 (d) amendment, D-2026-09-02-c): the face's
  /// material is on the **−normal** side — −radial for Cylinder/Cone/Sphere/Torus. This is the wall
  /// of a bore a Boolean SUBTRACT left behind. The seven primitives never set it, and it is
  /// meaningless for a Plane (a plane's normal already points wherever the topology needs). When set,
  /// the normal evaluators negate, the tessellator reverses winding, and the volume integrand flips
  /// sign, so the face correctly subtracts the void it bounds.
  bool inward = false;
};

/// The analytic curve an edge lies on. `Ellipse` was added in REQ-314 B2b-1 (D-2026-09-02-h) — an
/// oblique plane cutting a cylinder meets it along one. `Intersection` (REQ-314 B2b-2,
/// D-2026-09-03-a) is the general procedural curve where two surfaces cross — a quartic that has no
/// closed form: it carries the two surfaces (see \ref Edge::isectSurfaces) and is evaluated by
/// marching along it.
enum class CurveKind : std::uint8_t { Line, Arc, Ellipse, Intersection };

/// An edge of the solid, referenced by index from the loops that use it.
///
/// A **full-circle** edge (a cap rim that was not split by a seam) has `v0 == v1` and
/// `|sweep| == 2*pi`. Every primitive below splits its rims at a seam instead, so full-circle edges
/// are not produced here — but the area maths handles them, because a Phase 4 boolean can.
struct Edge {
  CurveKind kind = CurveKind::Line;
  int v0 = 0;  ///< Index into \ref Solid::vertices — the edge's start.
  int v1 = 0;  ///< Index into \ref Solid::vertices — the edge's end.

  /// Arc only: origin is the centre, Z is the arc's normal, X points from the centre toward `v0`.
  /// Built by `ucs::FromNormal`-style construction, so an edge's parametrisation is the one
  /// `ucs::PointOnPlaneCircle` gives every other curve in the project (REQ-311/REQ-312) — the
  /// tessellator, the validity check and a future renderer cannot disagree about which way it winds.
  /// Arc: origin is the centre, Z is the arc's normal, X points from the centre toward `v0`.
  /// Ellipse: origin is the centre, X is the **semi-major** direction, Y the **semi-minor**, Z the
  /// ellipse-plane normal. The edge starts at the ellipse parameter of `v0` and runs \ref sweep.
  /// Intersection: `frame.origin` is an **on-curve witness point** near the parametric middle — it
  /// fixes which of the two ways round the marching evaluator runs from `v0` to `v1`, and seeds the
  /// Newton correction. The axes are unused. `Translate` / `PlaceInFrame` move it like any frame.
  ucs::Ucs frame;
  double radius = 0.0;   ///< Arc: radius. Ellipse: semi-major axis `a`.
  double radius2 = 0.0;  ///< Ellipse: semi-minor axis `b`. Unused for Line / Arc.
  double sweep = 0.0;    ///< Arc / Ellipse: signed parameter sweep about `frame.zAxis`, CCW positive.

  /// Intersection only: **exactly two** surfaces whose crossing this edge lies on (copies, so the
  /// edge survives `Translate` and `.gs` on its own). Empty for every other `CurveKind`.
  std::vector<Surface> isectSurfaces;
};

/// One directed use of an \ref Edge by a \ref Loop.
///
/// `reversed` traverses the edge from `v1` to `v0`. A solid is manifold and orientable exactly when
/// every edge is used **twice, once in each direction** — which is the single most useful invariant
/// in \ref Validate, because a shell that fails it is not a solid at all.
struct EdgeUse {
  int edge = 0;
  bool reversed = false;
};

/// A closed, ordered ring of edge uses bounding part of a face.
struct Loop {
  std::vector<EdgeUse> uses;
};

/// A bounded region of a \ref Surface.
///
/// `loops[0]` is the outer boundary; any further loops are holes. The parametric span is carried
/// explicitly rather than re-derived from the loop: the loop says where the boundary runs, the span
/// says which side of a seam the face occupies, and on a closed surface (a sphere, a torus) the loop
/// alone cannot answer that.
struct Face {
  Surface surface;

  /// Angular span about `surface.frame.zAxis`, radians. Cylinder/Cone/Sphere: longitude.
  /// Torus: the angle around the axis of revolution. Unused for Plane.
  double uStart = 0.0;
  double uEnd = 0.0;

  /// Sphere: latitude, `-pi/2` (south pole) to `+pi/2`. Torus: the angle around the tube.
  /// Unused for Plane, Cylinder and Cone.
  double vStart = 0.0;
  double vEnd = 0.0;

  std::vector<Loop> loops;
};

/// A closed, oriented set of faces. Every primitive below has exactly one; a Phase 4 subtraction
/// that leaves a void inside a solid is what makes a second one necessary, so the level exists in
/// the model the requirement names rather than being flattened away now and re-added later.
struct Shell {
  std::vector<int> faces;  ///< Indices into \ref Solid::faces.
};

struct Vertex {
  Vec3 p;
};

// ---------------------------------------------------------------------------------------------
// The recipe: what a primitive was built from.
// ---------------------------------------------------------------------------------------------

enum class PrimitiveKind : std::uint8_t {
  None, Box, Wedge, Pyramid, Cylinder, Cone, Sphere, Torus,
  /// REQ-317. Appended, never inserted: the value is written into `.gs`.
  Polysolid
};

/// Canonical name, for the Properties panel, the command line and the log. Never returns null.
[[nodiscard]] const char* PrimitiveKindName(PrimitiveKind k);

// ---------------------------------------------------------------------------------------------
// REQ-317: the path a polysolid is swept along.
// ---------------------------------------------------------------------------------------------

/// One segment of a \ref Path, running from the previous point to \ref end.
///
/// A straight segment has `sweep == 0`. A curved one carries the **signed included angle** of a
/// circular arc, CCW positive about the path frame's +Z — so the centre and radius are derived from
/// the chord and the sweep rather than stored, and a segment cannot hold a centre that disagrees
/// with its own endpoints.
struct PathSeg {
  ucs::Point2D end{};
  double sweep = 0.0;
};

/// A chain of straight and circular segments in the XY plane of a frame. The profile GoSurvey
/// sweeps; not a document entity, so the kernel never learns what a `CadPolyline` is (ADR-048 (a)).
struct Path {
  ucs::Point2D start{};
  std::vector<PathSeg> segs;
  /// A closed path's last segment ends at \ref start, and the wall has no end caps.
  bool closed = false;
};

/// Which side of the picked line the wall sits on (REQ-317). "Left" is the left of the direction of
/// travel, so it depends on the order the points were picked — which is what AutoCAD does too.
enum class Justify : std::uint8_t { Left, Center, Right };

/// The parameters a primitive was created from, kept alongside the topology it produced.
///
/// This is *not* the geometry — \ref Validate, \ref ComputeMassProperties and \ref Tessellate all
/// read the topology and never the recipe, so a recipe that disagreed with its solid could not
/// silently change an answer. It exists so the Properties panel can report "Radius 12" rather than
/// "one cylindrical face", and so a future parametric edit has something to regenerate from
/// (#120's parametric-modelling section asks that the architecture not preclude it).
///
/// A solid produced by an operation that is not one of the seven primitives — a Phase 4 boolean —
/// carries \ref PrimitiveKind::None and no parameters. That is the case the recipe cannot describe,
/// and it is why the topology, not the recipe, is the stored truth.
struct Recipe {
  PrimitiveKind kind = PrimitiveKind::None;
  ucs::Ucs frame;        ///< Placement. Origin is the base centre, except Sphere/Torus (the centre).
  double length = 0.0;   ///< Box, Wedge: extent along frame X.
  double width = 0.0;    ///< Box, Wedge: extent along frame Y.
  double height = 0.0;   ///< Box, Wedge, Pyramid, Cylinder, Cone: extent along frame +Z.
  double radius = 0.0;   ///< Pyramid base circumradius; Cylinder r; Cone base r; Sphere R; Torus major R.
  double radius2 = 0.0;  ///< Pyramid top circumradius; Cone top r; Torus minor r.
  int sides = 0;         ///< Pyramid only: number of base sides.

  /// Polysolid only: the path it was swept along, in \ref frame's plane, plus how it was justified.
  /// The one recipe field whose length is not fixed. Like every other recipe field it is
  /// description and never truth — nothing in validity, mass properties or tessellation reads it
  /// (ADR-050 (f)).
  Path path;
  Justify justify = Justify::Center;
};

// ---------------------------------------------------------------------------------------------
// The solid.
// ---------------------------------------------------------------------------------------------

/// A boundary-represented solid: shells of faces, faces bounded by loops of edges, edges spanning
/// vertices — the hierarchy issue #146 names, with an analytic surface on every face.
struct Solid {
  std::vector<Vertex> vertices;
  std::vector<Edge> edges;
  std::vector<Face> faces;
  std::vector<Shell> shells;
  Recipe recipe;
};

// ---------------------------------------------------------------------------------------------
// Failure reasons. Nothing here is repaired silently: a solid is built or it is refused with a
// reason the user can read (REQ-201).
// ---------------------------------------------------------------------------------------------

enum class Problem {
  Ok = 0,

  // --- Construction: bad parameters, caught before any topology is built. ---
  NonFiniteParameter,        ///< A NaN or infinity reached a dimension.
  NonPositiveLength,
  NonPositiveWidth,
  NonPositiveHeight,
  NonPositiveRadius,
  NegativeTopRadius,         ///< A cone / pyramid top radius below zero.
  TopRadiusNotBelowBase,     ///< A cone / pyramid whose top is at least as wide as its base.
  /// A torus whose tube radius EQUALS its ring radius. Only the equal case: the inner equator
  /// collapses to a point there, so the topology has a zero-length edge and is not a solid at all.
  /// A tube LARGER than the ring is legal and self-intersecting - see ADR-045 (f) as amended.
  MinorRadiusEqualsMajor,
  SideCountOutOfRange,       ///< A pyramid with fewer than 3 or more than kMaxPyramidSides sides.
  DegenerateFrame,           ///< The placement frame is not right-handed orthonormal.

  // --- REQ-317 POLYSOLID: a path that does not describe a wall. Each is refused, never repaired;
  //     see ADR-050 (c) for why approximating them would be worse than declining. ---
  PathTooShort,              ///< Fewer than two points, or a closed path with fewer than two segments.
  PathSegmentDegenerate,     ///< A repeated point, or an arc with no sweep or no radius.
  PolysolidCornerCollapsed,  ///< A bend so sharp, or a segment so short, that the inner offset runs
                             ///< back past itself — there is no wall there to build.
  PolysolidCurveTooTight,    ///< An arc whose inner offset radius reaches zero: the wall would turn
                             ///< inside out around the curve.
  /// A path crossing its own run, so the wall would enclose part of the ground twice and its volume
  /// would count that part twice. Detected exactly for straight-segment paths; see ADR-050 (c).
  PolysolidPathSelfIntersects,

  // --- Validation: the topology itself is wrong. ---
  NoShell,
  EmptyShell,
  IndexOutOfRange,              ///< A loop, edge or shell addresses something that does not exist.
  LoopNotClosed,                ///< Consecutive edge uses do not share a vertex, or the ring does not close.
  EmptyLoop,
  EdgeNotUsedTwice,             ///< A non-manifold or open shell: an edge bounding one face, or three.
  EdgeOrientationInconsistent,  ///< Both uses of an edge run the same way — the shell is not orientable.
  FaceHasNoLoop,
  DegenerateFace,               ///< A face whose area rounds to nothing.
  DegenerateEdge,               ///< A zero-length line, or an arc with no radius or no sweep.
  NonFiniteCoordinate,
  NotClosed,                    ///< The shell encloses no positive volume, so it is not a solid.
  UnusedVertex,


  // --- Tessellation. ---
  PlaneFaceNotSimple,  ///< A flat face with holes, which the fan triangulation below cannot handle.
  NonPositiveTolerance,

  // --- Feature operations (REQ-314 / ADR-046). ---
  NonPositiveDistance,       ///< An extrusion distance that is zero or not finite.
  ProfileMalformed,          ///< A profile whose vertex and edge counts disagree.
  ProfileTooFewEdges,        ///< A profile of fewer than two edges — it bounds no area.
  ProfilePointOffPlane,      ///< A profile vertex or arc centre that is not on the profile plane.
  ProfileArcRadiusMismatch,  ///< A profile arc whose two endpoints are not equidistant from its centre.
  ProfileSelfIntersects,     ///< A profile loop that crosses itself.

  /// A profile arc that curves inward (a reflex bulge). The face it sweeps has its outward normal
  /// pointing toward the cylinder axis, which needs `Surface::inward`.
  ///
  /// **Extrude no longer raises this** - REQ-314 as amended builds that face. LOFT and SWEEP still
  /// do: neither has been taught to carry an inward wall through its own surface construction, so
  /// they refuse by name rather than build a solid whose material is on the wrong side of a face
  /// (REQ-201).
  ProfileArcReflex,

  // --- Revolve (REQ-314 increment 2). ---
  NonPositiveAngle,           ///< A revolve angle that is zero, not finite, or beyond a full turn.
  RevolveAxisDegenerate,      ///< A revolve axis whose direction is zero or not finite.
  RevolveAxisNotInPlane,      ///< A revolve axis that does not lie in the profile plane.
  RevolveProfileCrossesAxis,  ///< A profile that straddles the axis — the revolved solid would pass through itself.
  /// A revolved profile that does not reach the axis, or touches it in more than one place. Increment
  /// 2a builds a solid filled from the axis out to a single-valued outer curve, so an inner face
  /// (one whose material is on its +radial side) cannot arise — and a hollow revolve is a boolean
  /// SUBTRACT, not a profile shape.
  RevolveProfileMissesAxis,
  RevolveArcInProfile,        ///< An arc edge in a revolved profile (increment 2b — sphere / torus portions).

  // --- Slice (REQ-314 increment 3). ---
  SliceDegeneratePlane,  ///< A slicing plane whose normal is zero or not finite.
  SlicePlaneMissesSolid, ///< The plane does not pass through the solid — nothing to cut.
  SliceCurvedFace,       ///< The solid has a curved face; increment 3a slices planar-faced solids only.
  SliceResultComplex,    ///< The cut cross-section is not a single loop, or a side splits into pieces.

  // --- Booleans (REQ-314 increment 4, B1). ---
  /// A curved operand pair B1 cannot combine: a curved SUBTRACT (the hole wall faces inward, which
  /// \ref Surface cannot express — B2, per D-2026-09-02-b), a cone / sphere / torus operand, or a
  /// cylinder that only partly penetrates the other solid.
  BooleanCurvedFace,
  BooleanNonConvex,     ///< An operand is not convex; B1 combines convex solids only (B2 is general).
  /// A cylinder set at an angle to the other solid's faces — the two would meet along an ellipse,
  /// which needs the general Boolean (increment B2). Named so the refusal identifies the surface pair.
  BooleanObliqueCylinder,
  BooleanEmptyResult,   ///< The operation produces nothing (an INTERSECT of disjoint solids).
  BooleanResultInvalid, ///< The stitched result did not pass validation — refused rather than stored.

  // --- Loft (REQ-315 / ADR-048, GitHub issue #241). ---
  LoftNeedsTwoProfiles,  ///< Fewer than two profiles were given — a loft skins between profiles.
  /// Two consecutive profiles do not match edge-for-edge: a different edge count, or a corresponding
  /// pair where one edge is straight and the other an arc, or two arcs of unequal sweep. A
  /// divided-profile or point-capped loft is out of scope (REQ-315).
  LoftProfileMismatch,

  // --- Sweep (REQ-315 / ADR-048, GitHub issue #241). ---
  SweepPathDegenerate,        ///< A zero-length line path, or an arc path with no radius / no sweep.
  SweepProfileTouchesAxis,    ///< An arc-path sweep whose profile reaches the path's axis of curvature.
  /// The path has a **sharp corner** touching an arc segment. A straight-to-straight corner is
  /// mitred instead (REQ-315 2026-09-04); a corner touching an arc segment would need a trimmed
  /// NURBS patch — out of scope (REQ-315) — so it is still refused here, this increment.
  SweepPathCorner,
  /// A straight-to-straight sharp corner, but the profile has an arc edge — mitring would shear it
  /// into a non-circular curve, which this increment does not build (REQ-315 2026-09-04). A
  /// polygonal (all-straight-edge) profile mitres; a profile with any arc edge does not, yet.
  SweepMitreProfileArc,
  /// A straight-to-straight corner too sharp to mitre — the two segments fold back on themselves
  /// closely enough that the bisector (mitre) plane is degenerate (REQ-315 2026-09-04).
  SweepMitreCollapsed,
  /// A nonzero twist on a **closed** path (REQ-315 2026-09-04): geometrically inconsistent rather
  /// than an increment boundary — the seam ring is one ring, and a linear twist from 0 at the start
  /// to a nonzero angle at the end would need it to carry two different orientations at once.
  SweepUnsupportedOption,
  /// A nonzero twist combined with an **arc** path segment (REQ-315 2026-09-04): within an arc band,
  /// a profile vertex's true trajectory under a continuously varying twist compounds the path's own
  /// rotation with the twist's, which the arc band's rail/patch construction (a plain circular arc, or
  /// an exact rational revolve) cannot represent — the same category of gap as a mitred corner
  /// touching an arc segment, deferred for the same reason rather than built silently wrong. Twist on
  /// an all-straight (possibly multi-segment) path is unaffected.
  SweepTwistNeedsStraightPath,

  // --- Push/pull (REQ-319 / ADR-046 amendment (i), GitHub issue #148 Phase 5). ---
  /// This kind of face cannot be moved. A PLANE translates and a CYLINDER wall changes radius; a
  /// cone, sphere, torus or NURBS wall has no single parameter a push corresponds to - offsetting a
  /// cone along its own normal moves both radii by `d / cos(half-angle)`, which is an offset rather
  /// than the radius change the gesture reads as.
  PushPullFaceKindUnsupported,
  PushPullDistanceZero,     ///< A zero (or non-finite) distance: nothing to do, and saying otherwise lies.
  /// A face meeting the one being moved is CURVED. A corner of the moved face is re-solved as the
  /// meeting point of the planes around it, and a curved surface is not a plane to intersect: the
  /// cylinder wall beside a cap would have to change its stored radius or height, which is a
  /// different edit (its own increment).
  ///
  /// Checked before anything is built, because \ref Validate cannot catch the result afterwards:
  /// Validate tests topology and degeneracy and has no check that a face's vertices lie on that
  /// face's surface, so a solid whose wall no longer matches its boundary would pass, tessellate
  /// from one geometry and integrate its volume from another (ADR-046 amendment (i)).
  PushPullNeighbourCurved,
  /// A corner of the moved face cannot be re-solved: the planes meeting there are parallel or
  /// otherwise do not cross in a single point, or MORE than three faces meet there and moving one
  /// of them leaves no point satisfying all of them — the corner would have to split into several,
  /// which changes the topology and is a different operation.
  PushPullVertexUnsolvable,
  /// Moving this cap would collapse or invert the wall beside it: a cylinder pushed to zero height,
  /// or a cone whose moving end passes through its own apex. Refused before building rather than
  /// left to \ref Validate, so the reason names the wall rather than the whole solid.
  PushPullCurvedDegenerate,
  PushPullResultInvalid,    ///< The moved solid did not validate — collapsed, inverted or degenerate.
};

/// A short, user-facing sentence for \p p. Never returns null.
[[nodiscard]] const char* ProblemText(Problem p);

/// A pyramid past this many sides is a cylinder drawn the slow way, and each side costs four
/// vertices of stored topology. AutoCAD's own PYRAMID caps at 32; the extra headroom here is so a
/// solid arriving from a tool with a larger cap is refused rather than silently truncated.
inline constexpr int kMaxPyramidSides = 64;

// ---------------------------------------------------------------------------------------------
// The seven primitives. Each returns false and writes \p outWhy rather than producing a solid that
// is merely nearly right — an invalid solid stored is a defect that surfaces much later, in a
// boolean or a volume report, far from the command that caused it.
//
// \p frame must be right-handed orthonormal (`ucs::IsRightHandedOrthonormal`). Its origin is the
// centre of the base, except for Sphere and Torus where it is the centre of the solid.
// ---------------------------------------------------------------------------------------------

/// A rectangular box: \p length along frame X, \p width along frame Y, \p height along frame +Z,
/// centred on the frame origin in X and Y and rising from it in Z.
[[nodiscard]] bool MakeBox(const ucs::Ucs& frame, double length, double width, double height, Solid* out,
                           Problem* outWhy);

/// A right triangular prism: the full \p height at frame `x = -length/2`, falling to zero at
/// `x = +length/2`. The same shape and orientation AutoCAD's WEDGE produces.
[[nodiscard]] bool MakeWedge(const ucs::Ucs& frame, double length, double width, double height, Solid* out,
                             Problem* outWhy);

/// A pyramid on a regular \p sides-gon of circumradius \p baseRadius, rising \p height along frame
/// +Z to an apex (\p topRadius zero) or to a smaller regular polygon (a frustum).
[[nodiscard]] bool MakePyramid(const ucs::Ucs& frame, int sides, double baseRadius, double topRadius,
                               double height, Solid* out, Problem* outWhy);

/// A right circular cylinder of \p radius rising \p height along frame +Z.
[[nodiscard]] bool MakeCylinder(const ucs::Ucs& frame, double radius, double height, Solid* out,
                                Problem* outWhy);

/// A right circular cone of \p baseRadius rising \p height along frame +Z to an apex
/// (\p topRadius zero) or to a smaller circle (a truncated cone / frustum).
[[nodiscard]] bool MakeCone(const ucs::Ucs& frame, double baseRadius, double topRadius, double height,
                            Solid* out, Problem* outWhy);

/// A sphere of \p radius centred on the frame origin.
[[nodiscard]] bool MakeSphere(const ucs::Ucs& frame, double radius, Solid* out, Problem* outWhy);

/// A torus of \p majorRadius (centre to tube centre) and \p minorRadius (the tube), centred on the
/// frame origin with the frame Z as its axis of revolution.
/// REQ-317: a wall of \p width and \p height swept along \p path, mitred at every corner.
///
/// \p path lies in \p frame's XY plane and the wall rises along +Z. \p justify says which side of
/// the path the wall sits on: `Center` splits the width evenly, `Left` puts the path on the wall's
/// left edge, `Right` on its right — left and right of the direction of travel.
///
/// The result is ONE solid, not a run of boxes: the path is offset to each side by the half-width
/// and adjacent offsets are intersected, so a corner is mitred and counted once. Straight runs give
/// planar side faces and curved runs cylindrical ones. A corner that cannot be mitred — too sharp,
/// too short, or an arc whose inner offset would reach zero radius — is refused by name rather than
/// approximated (ADR-050 (b), (c)).
[[nodiscard]] bool MakePolysolid(const ucs::Ucs& frame, const Path& path, double width, double height,
                                 Justify justify, Solid* out, Problem* outWhy);

[[nodiscard]] bool MakeTorus(const ucs::Ucs& frame, double majorRadius, double minorRadius, Solid* out,
                             Problem* outWhy);

// ---------------------------------------------------------------------------------------------
// Feature operations (REQ-314 / ADR-046, GitHub issue #147 — Phase 4 of #120).
//
// A feature operation turns a drawn profile into a solid. Every face it produces is still one of
// the five \ref SurfaceKind values above and every edge still a line or an arc, so the kernel needs
// no new geometry carrier: a straight profile edge sweeps a plane, a circular-arc edge sweeps a
// cylinder. Extrude is increment 1; revolve, slice and the analytic booleans follow in the order
// ADR-046 lists.
// ---------------------------------------------------------------------------------------------

/// One edge of a \ref Profile: the span from `vertices[i]` to `vertices[(i + 1) % n]`.
struct ProfileEdge {
  bool arc = false;    ///< false — a straight chord. true — a circular arc.
  Vec3 centre;         ///< Arc only: the arc centre, on the profile plane.
  double sweep = 0.0;  ///< Arc only: signed sweep about the profile-plane normal (`plane.zAxis`),
                       ///< CCW positive, `0 < |sweep| < 2*pi`.
};

/// A single closed, planar loop of straight and circular-arc edges — the input to \ref Extrude, and
/// (from increment 2) to \ref Revolve.
///
/// `vertices` are the corner points in order; edge `i` runs from `vertices[i]` to
/// `vertices[(i + 1) % n]`, so the loop closes implicitly — there is no separate "is it closed"
/// field to disagree with the geometry. A full circle is expressed the way the cylinder builder
/// expresses its rims: two opposite vertices and two half-turn arc edges. Every vertex and every
/// arc centre must lie on `plane` within a scale-relative tolerance; `plane.zAxis` is the loop
/// normal and fixes what "CCW" means, but the builder accepts either winding and orients the
/// result itself.
struct Profile {
  ucs::Ucs plane;
  std::vector<Vec3> vertices;
  std::vector<ProfileEdge> edges;  ///< Exactly `vertices.size()` of them.
};

/// Extrude \p profile perpendicular to its own plane by \p distance and return the solid in \p out.
///
/// The sign of \p distance picks which side of the plane the solid rises on; its magnitude is the
/// height. A straight profile edge becomes a \ref SurfaceKind::Plane face, a circular arc becomes a
/// \ref SurfaceKind::Cylinder face, and two cap faces close the ends.
///
/// This is increment 1 of REQ-314: one loop, no taper, the sweep always along the plane normal.
/// Nothing is stored unless the result passes \ref Validate (REQ-201). Refuses — by name, never by
/// a silent repair — a \p distance that is zero or not finite (\ref Problem::NonPositiveDistance),
/// a profile whose vertex and edge counts disagree (\ref Problem::ProfileMalformed) or that has
/// fewer than two edges (\ref Problem::ProfileTooFewEdges), a vertex or arc centre off the plane
/// (\ref Problem::ProfilePointOffPlane), an arc whose endpoints are not equidistant from its centre
/// (\ref Problem::ProfileArcRadiusMismatch), a loop that crosses itself
/// (\ref Problem::ProfileSelfIntersects), and a degenerate placement frame
/// (\ref Problem::DegenerateFrame).
[[nodiscard]] bool Extrude(const Profile& profile, double distance, Solid* out, Problem* outWhy);

/// Revolve \p profile about the axis through \p axisPoint in direction \p axisDir, through
/// \p angleRad radians (signed; the sign is the sweep sense about \p axisDir, `0 < |angleRad| <=
/// 2*pi`), and return the solid in \p out.
///
/// The axis **must lie in the profile's plane**, and the profile **must not cross it** (touching is
/// fine — that is how a pole is formed). A straight profile edge sweeps a \ref SurfaceKind::Plane
/// (edge perpendicular to the axis), a \ref SurfaceKind::Cylinder (parallel), or a
/// \ref SurfaceKind::Cone (skew). A partial revolve closes with two planar cap faces; a full revolve
/// closes on itself.
///
/// This is increment 2 of REQ-314: straight profile edges only. An **arc** edge is refused
/// (\ref Problem::RevolveArcInProfile) — a revolved arc sweeps a sphere or torus portion, which is
/// increment 2b. Nothing is stored unless the result passes \ref Validate (REQ-201). Also refuses a
/// degenerate axis (\ref Problem::RevolveAxisDegenerate), an axis off the plane
/// (\ref Problem::RevolveAxisNotInPlane), a profile that straddles the axis
/// (\ref Problem::RevolveProfileCrossesAxis), and a bad angle (\ref Problem::NonPositiveAngle).
[[nodiscard]] bool Revolve(const Profile& profile, const Vec3& axisPoint, const Vec3& axisDir,
                           double angleRad, Solid* out, Problem* outWhy);

/// Skin a closed solid between \p profiles — two or more closed, planar loops of line / arc edges —
/// and return it in \p out (REQ-315 / ADR-048, GitHub issue #241).
///
/// The profiles are matched **edge-for-edge in order** and must have the **same edge count**;
/// corresponding edges must agree on straight-vs-arc, and corresponding arcs on sweep. Each
/// corresponding edge pair, between each consecutive profile pair, spans one \ref SurfaceKind::Nurbs
/// face — ruled (degree 1) where the edge is straight, rational (degree 2) where it is an arc — and
/// the two end profiles cap the solid as planar faces. Two identical profiles offset along the
/// normal reproduce \ref Extrude; a straight taper between two similar profiles reproduces a frustum.
///
/// This is the loft increment of REQ-315 (sweep follows). One outer loop per profile — a
/// divided-profile, multi-loop or point-capped loft is out of scope. Nothing is stored unless the
/// result passes \ref Validate (REQ-201); \p out is left untouched on failure. Refuses — by name —
/// fewer than two profiles (\ref Problem::LoftNeedsTwoProfiles), profiles that do not match
/// edge-for-edge (\ref Problem::LoftProfileMismatch), a malformed or too-small profile
/// (\ref Problem::ProfileMalformed, \ref Problem::ProfileTooFewEdges), a vertex or arc centre off its
/// profile plane (\ref Problem::ProfilePointOffPlane), a self-crossing profile
/// (\ref Problem::ProfileSelfIntersects), a reflex profile arc (\ref Problem::ProfileArcReflex), and
/// a degenerate profile frame (\ref Problem::DegenerateFrame). The result carries no recipe.
[[nodiscard]] bool Loft(const std::vector<Profile>& profiles, Solid* out, Problem* outWhy);

/// One segment of a \ref SweepPath: the span from `points[k]` to `points[k+1]`.
struct SweepSegment {
  bool arc = false;    ///< false — a straight span. true — a circular arc.
  Vec3 centre;         ///< Arc only: the arc centre; both segment endpoints are equidistant from it.
  Vec3 normal;         ///< Arc only: unit axis the arc turns about; `+sweep` is CCW around it.
  double sweep = 0.0;  ///< Arc only: signed sweep about \ref normal, `0 < |sweep| < 2*pi`.
};

/// A **sweep path** for \ref Sweep (REQ-315 / ADR-048): a chain of straight and circular-arc
/// segments. `points[0]` is where the profile starts; segment `k` runs `points[k] → points[k+1]`,
/// so `segments.size()` is `points.size() - 1`. A single segment is the common case (a line or an
/// arc); several segments form a bulge polyline, most often **tangent-continuous**. A tangent
/// discontinuity at a joint where both adjoining segments are straight is a **mitred corner**
/// (REQ-315 2026-09-04) rather than a refusal; touching an arc segment it is still refused by name
/// (a trimmed NURBS patch, out of scope). The path is usually **open**
/// (`points[0] != points.back()`); it may also be **closed** — `points[0] == points.back()`, a full
/// circle when there is one arc segment — which \ref Sweep treats specially (no end caps, REQ-315
/// 2026-09-04).
struct SweepPath {
  std::vector<Vec3> points;
  std::vector<SweepSegment> segments;
};

/// How \ref Sweep carries the profile's orientation along the path.
struct SweepOptions {
  /// A constant twist, applied about the (moving, if \ref alignToPath) path tangent, accumulating
  /// **proportionally to distance travelled** — 0 at the path start, \ref twistRad at the end, with
  /// each segment's share of that being its own length (a straight segment's chord, an arc segment's
  /// `radius * |sweep|`) divided by the path's total length (REQ-315 2026-09-04). Refused on a closed
  /// path (\ref Problem::SweepUnsupportedOption): the seam ring cannot carry two different end-of-
  /// path orientations at once.
  double twistRad = 0.0;
  /// true — the profile's plane normal follows the path tangent (a rotation-minimizing frame; on a
  /// planar arc this is the frame that does not spin about the tangent). false — the profile keeps
  /// its original world orientation and is only translated along the path, straight or curved
  /// (REQ-315 2026-09-04). **Not checked**: on a curved path this carries no rotation-minimizing
  /// guarantee against the swept envelope folding over itself, and \ref Sweep does not detect it — a
  /// profile too large, or a path too tightly curved, for this option can build a solid that occupies
  /// the same space twice. A known, documented limitation, not a checked-and-refused case.
  bool alignToPath = true;
};

/// Sweep one closed planar \p profile along \p path and return the solid in \p out (REQ-315 /
/// ADR-048, GitHub issue #241).
///
/// One band per path segment (\ref Loft's topology, chained): each profile edge sweeps one
/// \ref SurfaceKind::Nurbs face per segment — ruled along a straight segment, an exact rational
/// revolution along an arc segment — and the profile caps the two path ends as planar faces. The
/// profile's orientation is carried along the path by a rotation-minimizing frame (parallel
/// transport; on a circular arc the frame simply turns with the arc plane). A **single straight
/// segment reproduces \ref Extrude**; a **single arc segment (twist 0, aligned) reproduces
/// \ref Revolve** — asserted in tests.
///
/// A **closed path** — a single full-circle arc segment, or a multi-segment path whose last point
/// coincides with its first — builds with no end caps; the first and last cross-section rings are
/// the same ring (REQ-315 2026-09-04), mirroring \ref Revolve's own full-turn treatment. Its closing
/// seam is checked for tangent continuity the same way an interior joint is — unless it mitres
/// (below), in which case the continuity check does not apply.
///
/// A **mitred corner** (REQ-315 2026-09-04, GitHub issue #259): where two adjoining **straight**
/// segments meet at a tangent discontinuity, and the profile is **polygonal** (no arc edge), the
/// shared ring at the joint is built on the plane bisecting the two tangents — one straight cut
/// through both legs, matching a mitred pipe or duct joint (no gap, no overlap). This also applies at
/// a closed path's own closing seam. Refused — by name, not silently built as an unmitred or
/// collapsed joint — when: the corner touches an **arc** segment (\ref Problem::SweepPathCorner) —
/// mitring that would need a trimmed NURBS patch, out of scope (REQ-315); the profile has an arc edge
/// (\ref Problem::SweepMitreProfileArc) — shearing a circular edge onto an oblique plane makes an
/// ellipse, not built this increment; or the corner is too sharp to mitre, near a full reversal
/// (\ref Problem::SweepMitreCollapsed).
///
/// \ref SweepOptions::twistRad and \ref SweepOptions::alignToPath both work on **any** path — curved,
/// multi-segment, mitred — not only a single straight segment (REQ-315 2026-09-04). Twist
/// accumulates proportionally to distance travelled; a nonzero twist is refused on a **closed** path
/// (\ref Problem::SweepUnsupportedOption) as geometrically inconsistent, not an increment boundary.
/// Fixed orientation (`alignToPath = false`) carries the profile's original axes unrotated through
/// every segment; on a curved path this has no rotation-minimizing guarantee against the swept
/// envelope folding over itself, and it is **not checked** — `SelfIntersects` is a narrow,
/// torus-specific check (ADR-045 (f)), not a general overlap detector, and a real one is a separate
/// undertaking. A profile too large, or a path too tightly curved, for this option can build a solid
/// that occupies the same space twice; a known, documented limitation (REQ-315 2026-09-04).
///
/// Nothing is stored unless the result passes \ref Validate (REQ-201); \p out is left untouched on
/// failure. Refuses — by name — a malformed or degenerate path (\ref Problem::SweepPathDegenerate),
/// a profile that reaches an arc segment's axis (\ref Problem::SweepProfileTouchesAxis), and the same
/// malformed / non-planar / self-crossing / reflex-arc profiles \ref Loft refuses. No recipe.
[[nodiscard]] bool Sweep(const Profile& profile, const SweepPath& path, const SweepOptions& options,
                         Solid* out, Problem* outWhy);

/// Which side (or sides) of the cut \ref Slice keeps. "Above" is the `+planeNormal` side.
enum class SliceKeep : std::uint8_t { Above, Below, Both };

/// Cut \p solid by the unbounded plane through \p planePoint with unit \p planeNormal, and write the
/// kept piece(s) to \p outAbove and/or \p outBelow (either may be null, and one is left untouched
/// when \p keep is a single side). Each kept piece is a valid closed solid: the cut adds one new
/// planar face bounded by the plane's intersection with the solid's faces.
///
/// Increment 3 of REQ-314. **Planar-faced solids only** (a box, a straight extrusion, a revolve of a
/// rectilinear profile) — a curved face is refused (\ref Problem::SliceCurvedFace), as an oblique
/// plane through a cylinder cuts an ellipse, which the kernel's `{Line, Arc}` curves cannot hold;
/// that case arrives with the analytic Booleans. A plane that misses the solid, or one that would
/// split a kept side into disjoint pieces, is refused rather than producing a sliver
/// (\ref Problem::SlicePlaneMissesSolid, \ref Problem::SliceResultComplex). Nothing is written unless
/// every kept piece passes \ref Validate (REQ-201). The results carry no recipe.
[[nodiscard]] bool Slice(const Solid& solid, const Vec3& planePoint, const Vec3& planeNormal,
                         SliceKeep keep, Solid* outAbove, Solid* outBelow, Problem* outWhy);

/// Boolean combination of two solids (REQ-314 increment 4 / ADR-046 — the B1 subset). The result is
/// written to \p out as one or more solids: usually one, but a UNION of solids that do not touch is
/// two, and a SUBTRACT that splits its operand is several. Nothing is written unless every piece
/// passes \ref Validate (REQ-201); \p out is left untouched on failure.
///
/// **B1 combines convex, planar-faced solids** — a box, a wedge, a pyramid, a convex extrusion. A
/// curved face (\ref Problem::BooleanCurvedFace) or a non-convex operand
/// (\ref Problem::BooleanNonConvex) is refused: those need the general analytic intersection curve
/// of B2. An INTERSECT with no common volume reports \ref Problem::BooleanEmptyResult. The results
/// carry no recipe.
[[nodiscard]] bool BooleanUnion(const Solid& a, const Solid& b, std::vector<Solid>* out, Problem* outWhy);
[[nodiscard]] bool BooleanSubtract(const Solid& a, const Solid& b, std::vector<Solid>* out, Problem* outWhy);
[[nodiscard]] bool BooleanIntersect(const Solid& a, const Solid& b, std::vector<Solid>* out, Problem* outWhy);

// ---------------------------------------------------------------------------------------------
// Validity.
// ---------------------------------------------------------------------------------------------

/// Full structural check of \p s: index ranges, closed loops, every edge used exactly twice in
/// opposite directions (manifold **and** orientable), no degenerate edge or face, finite
/// coordinates, and a positive enclosed volume (so the shell faces outward).
///
/// **Self-intersection is not tested here**, and that is a deliberate boundary rather than an
/// oversight. For the seven primitives, the only way to build a self-intersecting shell is a bad
/// parameter — a torus whose tube swallows its own axis — and each is refused at construction by
/// the `Problem` values above. A general surface-surface intersection test belongs with the Phase 4
/// booleans, which are the first operation that can actually produce one.
[[nodiscard]] Problem Validate(const Solid& s);

/// Convenience predicate over \ref Validate.
[[nodiscard]] inline bool IsValid(const Solid& s) { return Validate(s) == Problem::Ok; }

/// True when \p s passes through itself.
///
/// One case exists today and the check names it rather than pretending to be general: a torus whose
/// tube radius exceeds its ring radius, which AutoCAD builds and users draw on purpose (ADR-045 (f)
/// as amended). Such a solid is perfectly valid topology — manifold, orientable, closed — and draws
/// correctly; what it is NOT is a body whose closed-form volume and area mean anything, because the
/// surface encloses part of space twice. \ref ComputeMassProperties therefore reports it as
/// unavailable rather than returning a plausible wrong number (REQ-201).
///
/// Read off the FACE's surface, not off the recipe, so a solid that arrived from a `.gs` or from a
/// future operation is judged on the geometry it actually has (ADR-045 (c)).
[[nodiscard]] bool SelfIntersects(const Solid& s);

/// Euler characteristic `V - E + F` of the solid's topology: 2 for a sphere-like solid, 0 for a
/// torus. Reported for the Properties panel and for tests; not a validity criterion, because the
/// genus is a legitimate property of a solid rather than a fault.
[[nodiscard]] int EulerCharacteristic(const Solid& s);

// ---------------------------------------------------------------------------------------------
// Mass properties.
// ---------------------------------------------------------------------------------------------

struct MassProperties {
  bool valid = false;
  double volume = 0.0;
  double surfaceArea = 0.0;
};

/// Exact volume and surface area of \p s, integrated over its **analytic** faces — not summed from
/// triangles. A sphere reports `4/3 pi r^3` because that is what the integral over its one spherical
/// face comes to, so the answer does not move when the display tessellation changes.
///
/// Volume uses the divergence theorem in the rotation-invariant form
/// `V = (1/3) * closed-surface-integral of (p - q) . n dA`, with `q` the mean of the solid's
/// vertices. Referencing every term to a point on the solid is what keeps this stable at survey
/// coordinate magnitudes (REQ-101): the integrands stay at model scale even when the model sits at
/// easting 2e6, so no term is a difference of two large nearly-equal numbers.
///
/// `valid` is false — and both figures zero — when \p s does not pass \ref Validate.
[[nodiscard]] MassProperties ComputeMassProperties(const Solid& s);

// ---------------------------------------------------------------------------------------------
// Bounds.
// ---------------------------------------------------------------------------------------------

struct Bounds {
  bool valid = false;
  Vec3 mn;
  Vec3 mx;
};

/// Axis-aligned bounds of \p s. **Conservative**: never smaller than the true footprint, sometimes
/// larger, because a curved face contributes the bounds of its whole surface of revolution rather
/// than of its swept patch. That is the same trade REQ-312 already made for a tilted circle's
/// bounds, and for the same reason — a box that is too small clips geometry out of zoom extents and
/// out of selection, while one that is too large only costs a little empty screen.
[[nodiscard]] Bounds ComputeBounds(const Solid& s);

// ---------------------------------------------------------------------------------------------
// Tessellation — a derived representation, regenerated at will, never stored in the solid.
// ---------------------------------------------------------------------------------------------

/// Triangles for display. Positions and normals are `double` and in the solid's own frame; the
/// narrowing to the `float` local storage the GPU wants happens above this layer, once, where the
/// document origin is known (REQ-101).
///
/// Vertices are **not** welded across faces: a solid's edges are creases, and sharing a vertex
/// between a cap and a cylinder side would smooth a corner that is genuinely sharp. Within a curved
/// face the normals are the true analytic surface normals, so a tessellated cylinder shades as a
/// cylinder rather than as a prism.
struct Tessellation {
  std::vector<double> vertsXyz;
  std::vector<double> normalsXyz;
  std::vector<std::uint32_t> indices;

  /// Which \ref Solid::faces entry each triangle came from — one entry per triangle, parallel to
  /// `indices` in threes.
  ///
  /// This is what lets a ray test done against the *triangles* report an answer on the *surface*:
  /// pick the nearest triangle, look up its face, then project the hit onto that face's analytic
  /// surface with \ref ClosestPointOnSurface. Without it a face snap would return a point on the
  /// chord rather than on the cylinder, which is wrong by the sagitta — small, plausible, and
  /// exactly the kind of error that survives a screenshot review.
  std::vector<int> triFace;

  [[nodiscard]] int vertexCount() const { return static_cast<int>(vertsXyz.size() / 3); }
  [[nodiscard]] int triangleCount() const { return static_cast<int>(indices.size() / 3); }
};

/// Tessellate \p s so that no chord departs from the true surface by more than \p chordTolerance
/// (in drawing units). Smaller is finer. Plane faces are exact at any tolerance.
///
/// Refuses a non-positive tolerance rather than dividing by it (REQ-201), and refuses a solid that
/// does not validate — drawing an invalid solid is how a topology fault reaches the screen looking
/// plausible.
///
/// Plane faces are triangulated as a fan from the loop's centroid, which is correct for a convex
/// outer loop with no holes. Every one of the seven primitives produces only such faces; a general
/// polygon triangulation is Phase 4's problem, when a boolean first produces a face that needs one,
/// and until then it would be an abstraction with no call site.
[[nodiscard]] bool Tessellate(const Solid& s, double chordTolerance, Tessellation* out, Problem* outWhy);

/// The solid's **edges** as line segments, at the same chord tolerance: six doubles per segment
/// (both endpoints), the `GL_LINES` layout the rest of the project uses.
///
/// A solid has real edges, which is the whole reason it can be drawn as a wireframe at all where an
/// imported mesh cannot (ADR-026 (c) — a mesh's "edges" are artefacts of an exporter's resolution).
/// Lives here rather than in the display layer so the chord rule is written down once: an edge and
/// the face it bounds must be subdivided by the same rule, or the wireframe visibly floats off the
/// shading it outlines.
/// **Isolines**: extra curves drawn ACROSS a curved face so it reads as curved in a wireframe view.
///
/// A solid's edges alone are a poor picture of it. A cylinder's edges are two rims and two seams, so
/// in wireframe it looks like two circles joined by two lines; a sphere's are two meridians, which
/// is a lens rather than a ball. Every CAD package draws these, and AutoCAD calls the count
/// `ISOLINES` — this is that, and \p isolineCount is that number, measured **around a full turn** so
/// a face that is half the solid gets half of them.
///
/// They are placed on a grid fixed to the surface's own frame rather than to each face's span, which
/// is what stops one landing on top of a seam edge and what keeps them evenly spaced around the
/// whole solid instead of bunching where two faces meet.
///
/// Which directions get them is per surface kind, and follows what the shape needs rather than a
/// rule applied blindly: a cylinder and a cone get lines ALONG the axis only (rings around them
/// would be read as edges that are not there); a sphere gets meridians and latitude circles; a torus
/// gets circles round the tube and round the ring. A plane gets none — it is flat, and its boundary
/// already says everything about it.
[[nodiscard]] bool TessellateIsolines(const Solid& s, int isolineCount, double chordTolerance,
                                      std::vector<double>* out, Problem* outWhy);

[[nodiscard]] bool TessellateEdges(const Solid& s, double chordTolerance, std::vector<double>* out,
                                   Problem* outWhy);

/// The point at parameter \p t in [0,1] along \p e, walking from `v0` to `v1`. The one place an
/// edge's parametrisation is written down, so the tessellator and the validity check cannot
/// disagree about where an arc runs.
[[nodiscard]] Vec3 EdgePointAt(const Solid& s, const Edge& e, double t);

// ---------------------------------------------------------------------------------------------
// Closest-point queries. These are what object snapping is built on: a snap must return a point
// that lies **on** the geometry, and for a curved face that means on the surface, not on the chord
// the tessellator drew across it.
// ---------------------------------------------------------------------------------------------

/// The point on \p sf's *unbounded* analytic surface nearest \p p.
///
/// Unbounded deliberately: the caller has already decided which face it is asking about (by ray
/// testing that face's triangles), so re-imposing the parametric bounds here could only move the
/// answer off the face the user is pointing at. Returns \p p unchanged where the nearest point is
/// undefined — a point exactly on a cylinder's axis, or at a sphere's centre — rather than
/// returning a NaN or picking a direction arbitrarily.
[[nodiscard]] Vec3 ClosestPointOnSurface(const Surface& sf, const Vec3& p);

/// The point on \p e nearest \p p, clamped to the edge's own extent — so the answer is on the edge
/// itself, never on the infinite line or full circle it lies along.
[[nodiscard]] Vec3 ClosestPointOnEdge(const Solid& s, const Edge& e, const Vec3& p);

/// \p s moved by \p delta, leaving its shape and orientation alone.
///
/// **Lives here because only this header knows every place a coordinate hides in a `Solid`** — the
/// vertices, each arc edge's centre, each face's surface origin, and the recipe's placement frame.
/// Open-coded at a call site, adding a field to \ref Surface later would silently miss it, and a
/// solid that half-moved is not a shape at all.
///
/// The axes are directions and the radii are lengths, so neither moves. The first caller is the
/// document-origin rebase (REQ-101), where a store that does not follow the origin is a solid that
/// silently jumps by the origin's whole magnitude.
[[nodiscard]] Solid Translate(const Solid& s, const Vec3& delta);

/// Move face \p faceIndex of \p s along its own outward normal by \p distance (REQ-319).
///
/// **The first operation in this kernel that EDITS a solid.** Everything before it builds: from a
/// profile (extrude, revolve, loft, sweep), from two solids (the Booleans), or by cutting one
/// (slice). This takes a solid and returns a changed one — and, like all of them, it computes into
/// a fresh `Solid` and validates before returning, never mutating its input (ADR-046 (d)). That is
/// not merely tidiness: `CadSolidPtr` is `shared_ptr<const Solid>` so undo snapshots are a refcount
/// bump, which makes immutability the precondition for undo working at all.
///
/// **What moves, and how.** The face's own plane translates. Every corner of it is then **re-solved
/// as the point where the planes of the faces meeting there now cross** — not translated along the
/// push. That distinction is the whole generality of the operation:
///
/// - on a **box**, sliding a corner straight up a vertical wall and intersecting three planes give
///   the same point, so the two agree;
/// - on a **pyramid or a wedge**, they do not. Every neighbour is slanted, so a corner slid straight
///   up leaves the sloping face it is supposed to sit on. Re-solving puts it exactly where the
///   slope, its other neighbour and the moved face now meet — which is what "push the base of a
///   pyramid" means, and what translation could only refuse.
///
/// A neighbouring face keeps its surface untouched throughout: its own vertices are re-solved *onto*
/// it, so it stays a plane its boundary actually lies on. That is the property `Validate` does not
/// check and this operation therefore guarantees itself.
///
/// **Refuses, by name and before building anything:**
/// - \ref Problem::PushPullFaceKindUnsupported — a curved wall. Moving a cylinder's wall is a radius
///   change and a different geometry problem: the caps' boundary arcs must be re-solved, not
///   translated. Its own increment.
/// - \ref Problem::PushPullDistanceZero — zero or non-finite. Reporting success for a move that did
///   not happen is worse than declining.
/// - \ref Problem::PushPullNeighbourCurved — a curved face meets the moved one. **This is the
///   load-bearing refusal**, and it was measured rather than argued. A curved surface is not a plane
///   to intersect, and \ref Validate cannot be relied on to notice the result: it checks topology
///   and degeneracy and has no test that a face's vertices lie on their own surface. With this check
///   removed, pushing a **cylinder's cap** by 3 builds a solid that Validate passes as **Ok** and
///   whose analytic volume comes out **863.938 against a true 1021.02** — 15% wrong, because the
///   wall still reports `height = 10` while its boundary sits at 13. See ADR-046 amendment (i).
/// - \ref Problem::PushPullVertexUnsolvable — a corner whose planes do not cross in one point, or
///   one where more than three faces meet and moving one leaves no point satisfying all of them.
///   Pushing a **pyramid's side face** is the honest example: its corners include the apex, where
///   four planes meet, and moving one of them would split that apex into several points. A topology
///   change, and a different operation.
/// - \ref Problem::PushPullResultInvalid — the moved solid failed validation: pushed so far it
///   collapsed, inverted, or degenerated a face. A real gesture, not a hypothetical.
///
/// **The result carries no recipe.** A pushed box is not the box its recipe describes, and a recipe
/// that no longer describes its solid reads as authoritative while being false (REQ-319 item 6).
/// **The topology is unchanged** — same vertex, edge and face counts, same indices — which is what
/// lets a REQ-318 sub-object reference survive the edit rather than expire (ADR-049).
[[nodiscard]] bool PushPullFace(const Solid& s, int faceIndex, double distance, Solid* out,
                                Problem* outWhy);

} // namespace brep
