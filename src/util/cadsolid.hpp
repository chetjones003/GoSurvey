#pragma once

/// The drawing-facing half of the B-rep solid kernel (REQ-313 / ADR-045, GitHub issue #146).
///
/// The kernel in `brep.hpp` knows nothing about documents, layers or the GPU. This header is the
/// seam where a kernel solid becomes a drawing entity: the store's coordinate convention, the
/// tessellation cache, and the batches the renderer is handed. It is still pure — no GL, no ImGui,
/// no `AppCommandState` — so the cache's staleness rule stays testable without a window.

#include "brep.hpp"

#include <memory>
#include <vector>

/// A solid in the drawing.
///
/// **Coordinates are storage coordinates**, not world: X/Y are local (`world = local +
/// worldDocumentOrigin`) and Z is absolute — ADR-025 D2, the same convention every other geometry
/// store uses. The kernel is frame-agnostic, so it computes volume and area correctly in whichever
/// frame it is handed; putting the store in local coordinates is what keeps a solid at easting 2e6
/// as accurate as one at the origin (REQ-101).
///
/// Unlike every other store, these coordinates are `double` rather than `float`. Architecture §11.8's
/// float convention exists for arrays with millions of entries headed for a vertex buffer; a solid's
/// B-rep is a handful of vertices and faces, so the narrowing buys nothing and would throw away the
/// exactness the closed-form volume depends on. The *tessellation* — which really is GPU-bound and
/// really can be large — is narrowed to float, once, in \ref CadSolidTessellation.
///
/// **Held as `shared_ptr<const brep::Solid>`**, exactly as `CadMesh` and `CadTin` are and for the
/// same reason (architecture §11.5): every undo snapshot deep-copies the geometry stores, and a
/// shared immutable payload makes that a refcount bump. Immutability is the precondition, so a solid
/// is *replaced*, never edited in place.
using CadSolidPtr = std::shared_ptr<const brep::Solid>;

/// One solid's cached display geometry — the derived representation, regenerated only when the
/// solid or the tessellation quality changes, and **never** stored in the solid itself (#120:
/// "changing tessellation quality should not modify the underlying solid").
///
/// Not part of any undo snapshot, deliberately: it is derived from the solid, so snapshotting it
/// would put megabytes of regenerable triangles into the undo stack for nothing. That is exactly the
/// split ADR-036 (e) already made for the surface display cache.
struct CadSolidTessellation {
  /// Which solid this was built from. A `weak_ptr` and not a raw pointer: a raw key could be matched
  /// by a NEW solid allocated at a freed address, and the cache would then draw the wrong shape from
  /// a stale buffer — the same trap the renderer's mesh cache already avoids this way.
  std::weak_ptr<const brep::Solid> key;
  /// The chord tolerance these triangles were generated at. Part of the staleness key, so changing
  /// quality regenerates and changing nothing else does not.
  double chordTolerance = 0.0;

  /// Shaded faces: `GL_TRIANGLES`, nine floats per triangle, storage coordinates. Expanded rather
  /// than indexed — a primitive's tessellation is thousands of triangles, not millions, so the
  /// index array would save less than it costs in a second code path.
  std::vector<float> triVerts;
  /// One unit normal per triangle vertex, parallel to \ref triVerts. These are the **analytic**
  /// surface normals, not facet normals, which is what makes a tessellated cylinder shade as a
  /// cylinder instead of as a prism.
  std::vector<float> triNormals;
  /// Which `brep::Solid::faces` entry each triangle belongs to — one entry per TRIANGLE, so
  /// `triFaceIds.size() * 9 == triVerts.size()`.
  ///
  /// This is what makes a face snap land on the surface rather than on a chord: the ray test finds
  /// the triangle, this says which face that triangle is part of, and the answer is then projected
  /// onto that face's analytic surface (`brep::ClosestPointOnSurface`).
  std::vector<int> triFaceIds;
  /// The solid's real edges: `GL_LINES`, six floats per segment, storage coordinates. A solid has
  /// genuine edges — unlike a mesh, whose "edges" are artefacts of an exporter's resolution — which
  /// is why a solid can be drawn as a wireframe at all and a mesh cannot (ADR-026 (c)).
  std::vector<float> edgeVerts;

  [[nodiscard]] bool empty() const { return triVerts.empty() && edgeVerts.empty(); }
};

/// One solid's drawable geometry plus the appearance to draw it with.
///
/// **The vertex buffers are BORROWED, never owned** — they point into the entries of
/// `AppCommandState::solidDisplayCache`, which is where the generated geometry lives. The batch list
/// and the cache it points into are rebuilt together and consumed in the same frame, so a batch
/// never outlives its buffer. **Nothing may hold one across a cache refresh.** Same ownership rule,
/// and the same reason, as `SurfaceDisplayBatch`.
struct CadSolidDisplayBatch {
  const std::vector<float>* triVerts = nullptr;
  const std::vector<float>* triNormals = nullptr;
  const std::vector<float>* edgeVerts = nullptr;
  /// Resolved entity colour (REQ-048) — shading multiplies it, the wireframe edges use it directly.
  float rgba[4] = {1.f, 1.f, 1.f, 1.f};
  /// Millimetres on paper for the edges, or -1 for the renderer's default width.
  float lineweightMm = -1.f;
};

/// Everything drawn for the drawing's solids this frame.
///
/// One struct rather than three parameters on a `RenderScene` signature that is already 30 long —
/// the faces and the edges of a solid are always built together and always consumed together, which
/// is the same argument `CadSurfaceDisplayGeometry` records for itself.
struct CadSolidDisplayGeometry {
  std::vector<CadSolidDisplayBatch> solids;
  [[nodiscard]] bool empty() const { return solids.empty(); }
};

/// The chord tolerance solids are tessellated at, in drawing units.
///
/// One value, not a per-solid setting: #120 asks that quality be configurable, and a single knob is
/// what that needs today. 0.01 ft is REQ-101's own tolerance — the point at which a chord's
/// departure from the true surface is smaller than the accuracy the drawing claims anywhere else,
/// so a finer setting would be drawing detail the rest of the program does not promise.
inline constexpr double kSolidChordToleranceFt = 0.01;
