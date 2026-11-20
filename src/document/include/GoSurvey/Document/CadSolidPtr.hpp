#pragma once

#include <memory>

namespace brep {
struct Solid;
}

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
