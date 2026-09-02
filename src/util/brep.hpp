#pragma once

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

/// The analytic surface a face lies on.
enum class SurfaceKind : std::uint8_t { Plane, Cylinder, Cone, Sphere, Torus };

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
};

/// The analytic curve an edge lies on.
enum class CurveKind : std::uint8_t { Line, Arc };

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
  ucs::Ucs frame;
  double radius = 0.0;  ///< Arc only.
  double sweep = 0.0;   ///< Arc only: signed sweep about `frame.zAxis`, CCW positive.
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

enum class PrimitiveKind : std::uint8_t { None, Box, Wedge, Pyramid, Cylinder, Cone, Sphere, Torus };

/// Canonical name, for the Properties panel, the command line and the log. Never returns null.
[[nodiscard]] const char* PrimitiveKindName(PrimitiveKind k);

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
  /// A profile arc that curves inward (a reflex bulge). The face it would sweep has its outward
  /// normal pointing toward the cylinder axis, which \ref Surface has no way to express — there is
  /// no "reversed" flag. Supported once the booleans force a general answer to inward-curving faces.
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
  BooleanResultInvalid  ///< The stitched result did not pass validation — refused rather than stored.
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

} // namespace brep
