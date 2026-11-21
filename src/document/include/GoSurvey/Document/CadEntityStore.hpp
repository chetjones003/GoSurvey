#pragma once

/// The entity store (GLOSSARY.md, ADR-0010 / issue #1557 phase 3): the block-capable CAD entity
/// collections, held in one value so a block definition and a drawing hold their entities the same
/// way. \ref CadBlockContent stores block-local coordinates in it; \ref DrawingContent inherits it
/// for model-space coordinates, so the field names and parallel-array rules are defined once.

#include "CadEntities.hpp"
#include <GoSurvey/Document/CadSolidPtr.hpp>

#include <cstdint>
#include <memory>
#include <vector>

struct CadEntityStore {
  /// Line vertices for GL: pairs (x,y,z) per endpoint; each segment is two endpoints.
  std::vector<double> userLinesFlat;
  std::vector<EntityAttributes> userLineAttrs;
  /// Each circle: center X, center Y, center Z, radius (world units) — stride 4 (REQ-057 /
  /// ADR-025 (a)). The centre's XYZ is contiguous so it reads like a point; the radius trails it.
  /// Z is absolute (ADR-025 D2). The circle lies in world XY unless `userCircleNormals`
  /// says otherwise (REQ-312), matching CadArc.
  std::vector<double> userCirclesCxCyZR;
  std::vector<EntityAttributes> userCircleAttrs;
  /// Plane normal per circle, 3 floats each (REQ-312) - parallel to `userCirclesCxCyZR` the way
  /// `userCircleAttrs` already is, and maintained at the same sites. A side-car rather than a
  /// wider stride because that 4-float stride is read directly at roughly 300 call sites and a
  /// stride mistake in a flat float array is silent (D-2026-08-31-f). World +Z is the default,
  /// which is every circle that existed before this field; `docinvariants` checks the count, so a
  /// desynchronised insert or erase fails loudly rather than mis-orienting a circle (REQ-204).
  std::vector<float> userCircleNormals;
  std::vector<CadArc> userArcs;
  std::vector<EntityAttributes> userArcAttrs;
  std::vector<CadEllipse> userEllipses;
  std::vector<EntityAttributes> userEllAttrs;
  /// Each polyline: vertex indices [\ref userPolylineOffsets[i], \ref userPolylineOffsets[i+1]); XYZ triplets in
  /// \ref userPolylineVerts.
  std::vector<int> userPolylineOffsets;
  std::vector<double> userPolylineVerts;
  /// REQ-316 / ADR-047: per-vertex DXF bulge (tan(theta/4); 0 = straight segment leaving this
  /// vertex). Parallel to the vertex list: size() == userPolylineVerts.size() / 3.
  std::vector<float> userPolylineVertsBulge;
  std::vector<uint8_t> userPolylineClosed;
  std::vector<EntityAttributes> userPolylineAttrs;

  std::vector<CadAnnotation> cadAnnotations;
  std::vector<EntityAttributes> cadAnnotationAttrs;
  /// Imported triangle meshes (REQ-063 / ADR-026). `shared_ptr<const>` so undo snapshots share the
  /// payload rather than copying it (architecture §11.5 as amended). "Editing" a mesh means
  /// replacing the pointer; never write through it.
  std::vector<std::shared_ptr<const CadMesh>> cadMeshes;
  std::vector<EntityAttributes> cadMeshAttrs;
  /// B-rep solids (REQ-313 / ADR-045, REQ-320 / ADR-051). `shared_ptr<const>` for the same reason
  /// meshes are: an undo snapshot shares the payload instead of copying it. A solid is REPLACED,
  /// never written through.
  std::vector<CadSolidPtr> cadSolids;
  std::vector<EntityAttributes> cadSolidAttrs;
};
