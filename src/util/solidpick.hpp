#pragma once

/// Sub-object picking for B-rep solids — which FACE, EDGE or VERTEX is the cursor over?
/// (REQ-318 / ADR-049, GitHub issue #148 — Phase 5 of #120.)
///
/// Pure and dependency-free, for the reason `brep.hpp` and `meshgeom.*` are: no GL, no ImGui, no
/// `AppCommandState`, so the pick's precedence rules and its refusals are testable without a window
/// (the ADR-002 layering pressure, and #120's "the geometry engine should be usable without a
/// graphics context").
///
/// **What is new here and what is not.** The ray/triangle → `triFace` → `ClosestPointOnSurface`
/// pipeline is *not* new: object snapping has picked solid faces, edges and vertices since REQ-313
/// landed, in `src/viewport/CadSnap.cpp`. What that code could not do is serve a second caller —
/// its helpers were file-private, so a selection subsystem would have had to re-implement them, and
/// a second implementation of a pick is a second set of numerics that can disagree with the first
/// under one cursor. This module is that shared home, and `CadSnap` now routes through
/// `ray3d::RayTriangleIntersect` and \ref RayNearBounds here rather than keeping its own copies.
/// What is genuinely new is above the geometry: the **expiring sub-object reference** (see
/// \ref Pick::index), and precedence and occlusion as stated rules rather than snap-local behaviour.
///
/// This is also what issue #156 (UCS aligned to a picked face) was deferred onto: PR #180 chose "a
/// real sub-object selection subsystem" over a UCS-command-local ray-triangle test, so #156 becomes
/// a thin consumer of the face answer.

#include "brep.hpp"
#include "ray3d.hpp"

#include <cstdint>
#include <vector>

namespace solidpick {

using ray3d::Ray;
using ray3d::Vec3;

/// Cheap reject: could \p ray pass within \p pad of \p b at all?
///
/// Lifted out of `CadSnap.cpp` so the snap and the sub-object pick share one broad phase rather than
/// each carrying its own. Not a micro-optimisation, and the original comment is worth keeping: a
/// pick runs on HOVER, every frame, and the triangle walk is O(triangles) per solid — a few hundred
/// solids at a couple of thousand triangles each is most of a million ray-triangle tests per frame,
/// which is REQ-100's budget gone on a cursor that is not near any of them. Four compares that
/// discard a solid first is the difference.
///
/// The box is padded so a ray passing just outside a solid still reaches the triangles: a pick that
/// silently misses is worse than a slow one.
[[nodiscard]] bool RayNearBounds(const Ray& ray, const brep::Bounds& b, double pad);

/// Which kind of sub-object a pick landed on. `None` is a miss, and is the only answer that comes
/// with no coordinate.
enum class Kind : std::uint8_t { None = 0, Face = 1, Edge = 2, Vertex = 3 };

/// A stable, human-readable name — for the command line, the status bar and test failure messages.
[[nodiscard]] const char* KindName(Kind k);

/// One picked sub-object of one solid.
struct Pick {
  Kind kind = Kind::None;
  /// Index into `Solid::faces`, `::edges` or `::vertices` according to \ref kind.
  ///
  /// **An index alone is not a durable reference.** It keeps its meaning across an edit that
  /// preserves the topology — a box's face indices survive a height change, measured — and loses it
  /// across anything that changes the counts, such as a cone frustum collapsing to an apex (4 faces
  /// → 3) or any boolean. A selection that has to outlive an edit must therefore pair this index
  /// with the identity of the solid it came from, the way `CadSolidTessellation` keys its cache by
  /// `weak_ptr` rather than a raw pointer, and expire rather than silently re-bind to whatever now
  /// occupies the index.
  int index = -1;

  /// The picked point, **on** the geometry: on the face's analytic surface, on the edge's true
  /// curve, or exactly the vertex. Never on a tessellation chord — see \ref PickSubObject.
  Vec3 point;

  /// **Distance** from the ray origin to the pick, so one solid's answer can be depth-ordered
  /// against another's. A distance rather than a bare parameter because \ref PickSubObject
  /// normalizes the ray direction before using it (see there).
  double rayT = 0.0;

  [[nodiscard]] bool valid() const { return kind != Kind::None && index >= 0; }
};

/// How near the ray must pass a vertex or an edge to take it instead of the face behind it.
///
/// Both are distances in the solid's own units, and both are properly a function of the *screen*: a
/// vertex is a few pixels wide however far away it is, so the caller converts a pixel tolerance
/// into world units at the pick depth, exactly as the existing entity pick does with
/// `CadOffsetEntityPickTolWorld`. Zero disables that kind of pick.
struct Tolerance {
  double vertex = 0.0;
  double edge = 0.0;
};

/// The nearest sub-object of \p solid under \p ray, or false for a miss.
///
/// \p triVerts and \p triFaceIds are the solid's **already-built** display triangles — nine floats
/// per triangle and one face index per triangle, the `CadSolidTessellation` layout. Reusing them
/// rather than re-tessellating is deliberate on two counts: a pick must not cost a tessellation,
/// and picking the same triangles the user can see is what makes the answer match what they
/// clicked.
///
/// **Precondition: those triangles are in the solid's own STORAGE coordinates**, which per
/// `cadsolid.hpp` means X/Y local to the document origin and Z absolute — and so does \p ray. That
/// is what makes the `float` buffer adequate: local coordinates stay at model magnitude however far
/// out the document sits, which is the entire reason storage is local (REQ-101). Handing this
/// function triangles at absolute state-plane magnitude quantizes them to the `float` ULP there —
/// 0.125 ft at easting 2e6, twelve times REQ-101's tolerance.
///
/// Whether that quantization reaches the answer depends on the geometry, and the distinction is
/// worth stating because the obvious test misses it: for a ray meeting the surface head-on the
/// displacement lies along the ray, so it moves the hit's distance from the surface and the
/// projection below removes it exactly. At **oblique** incidence it moves the hit *along* the
/// surface instead, by about `d·tan(angle from normal)`, and the projection cannot see that at all —
/// it fixes distance-from-surface error, never position-along-surface error. Both cases are pinned
/// in `SolidPickTests`, at absolute magnitude and at storage magnitude, with the same ray geometry.
///
/// **The projection is not a refinement, it is the pick.** The triangle only locates the sub-object;
/// `brep::ClosestPointOnSurface` (faces) and `brep::ClosestPointOnEdge` (edges, clamped to the
/// edge's own extent, so an arc answer is on the circle) place the point. Measured on a cylinder at
/// the shipping chord tolerance, a raw triangle hit lands 0.00986 ft off the true surface: inside
/// REQ-101's ±0.01 ft, but spending 98.6% of the budget before any other error joins in. Projected,
/// the residual is at the arithmetic floor. Anything here that "simplifies" the projection away
/// would still pass a ±0.01 ft test, which is why the tests assert far tighter than that.
///
/// **The ray direction need not be unit length — it is normalized on entry.** Deliberate, and not
/// tidiness: `RayTriangleIntersect`'s parameter scales as `1/|dir|` while `ray3d::RayPointDistance`'s
/// scales as `|dir|`, so comparing a face depth against a vertex depth on a non-unit ray compares
/// two quantities a factor of `|dir|²` apart and the occlusion test below becomes meaningless. A
/// caller that builds its ray by unprojecting a near and a far point — `dir = far - near`, the
/// natural construction — would otherwise get no vertex or edge pick at all.
///
/// **Precedence is Vertex, then Edge, then Face**, each within its tolerance. Not a preference but
/// a necessity: every vertex lies on some edge and every edge on some face, so nearest-wins alone
/// would make a vertex unpickable. It is also what every CAD package does, for the same reason
/// object snapping prefers an endpoint to a nearest-point — the smaller target is the more specific
/// intent.
///
/// **An occluded vertex or edge does not win.** A candidate behind the nearest triangle hit by more
/// than its own tolerance is on the far side of the solid and is not what the cursor is over;
/// admitting it would let a click on a near face select the back silhouette. The baseline is the
/// nearest *triangle*, not the nearest usable face: a triangle whose face id is out of range still
/// proves there is a front surface there, and using the next valid face's depth instead would let
/// the back of the solid pass as unoccluded. When the ray misses every triangle there is no front
/// to be behind, so a near-miss just outside the silhouette can still take an edge or a vertex —
/// which is the click a user makes when they aim at an outline.
///
/// Refuses — writing nothing to \p out — a null \p out, an invalid ray, an empty solid, a ray that
/// cannot come within the larger tolerance of the solid's bounds, and buffers whose sizes disagree
/// (`triVerts.size() != 9 * triFaceIds.size()`), rather than reading past the end of one of them or
/// reporting a pick it cannot justify (REQ-201).
[[nodiscard]] bool PickSubObject(const brep::Solid& solid, const std::vector<float>& triVerts,
                                 const std::vector<int>& triFaceIds, const Ray& ray,
                                 const Tolerance& tol, Pick* out);

}  // namespace solidpick
