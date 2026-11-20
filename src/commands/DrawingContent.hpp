#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "CadEntities.hpp"
#include "CadLayerRow.hpp"
#include "DimensionStyle.hpp"
#include "DrawingSettings.hpp"
#include "MultileaderStyle.hpp"
#include "PaperSpace.hpp"
#include "GoSurvey/Document/CadSolidPtr.hpp"
#include "GoSurvey/Document/cadblock.hpp"
#include "GoSurvey/Document/cadtable.hpp"
#include "GoSurvey/Document/cadtablestyle.hpp"
#include "GoSurvey/Pdf/PdfAttach.hpp"
#include "GoSurvey/Survey/AlignmentDesign.hpp"
#include "GoSurvey/Survey/AlignmentModel.hpp"
#include "GoSurvey/Survey/AlignmentProfile.hpp"
#include "GoSurvey/Survey/AlignmentSampleLines.hpp"
#include "GoSurvey/Survey/AlignmentStyleData.hpp"
#include "GoSurvey/Survey/AlignmentViewFrames.hpp"
#include "GoSurvey/Survey/SurveyPointRecords.hpp"

/// Drawing content (GLOSSARY.md, ADR-0010): the fields an undo frame and a tab's stored document
/// both hold, defined once. Undo stores it in \ref DrawingGeometrySnapshot, a tab stores it in
/// \ref DrawingDocument, and \ref AppCommandState inherits it for the live drawing, so copying
/// drawing content is one assignment instead of a hand-written field list.
///
/// Phase 1a of issue #1557: exactly the fields those two structs shared, moved without behaviour
/// change. The defaults are the snapshot's and the document's; `AppCommandState`'s constructor
/// seeds the tables a live drawing always has (layer "0", "Standard" styles).
struct DrawingContent {
  // --- Model-space entities ---------------------------------------------------------------------

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
  /// REQ-325 / ADR-053: the plane of the (bulge-curved) segment leaving this vertex, 3 floats each
  /// (stride 3, parallel to userPolylineVerts). Consulted only when the paired bulge is non-zero;
  /// omitted from persistence when every entry is world +Z, the same additive/byte-identical-legacy
  /// rule REQ-312 used for userCircleNormals.
  std::vector<float> userPolylineVertsNormal;
  std::vector<uint8_t> userPolylineClosed;
  std::vector<EntityAttributes> userPolylineAttrs;

  /// Feature lines (REQ-087, ADR-035) — named 3D design linework, in their own store rather than
  /// the polyline arrays, so a feature line is never mistaken for a polyline at the type level.
  ///
  /// Same CSR shape as polylines: line `i` owns vertices
  /// [`featureLineOffsets[i]`, `featureLineOffsets[i+1]`), XYZ triplets in \ref featureLineVerts.
  ///
  /// \ref featureLineElevPt is per VERTEX, not per line: 1 marks an **elevation point** — a vertex
  /// that carries an elevation but is not a PI (a corner). It lies on the line by construction, so
  /// plan geometry is identical whether or not it is counted, and only PI-level operations
  /// (insert/delete PI, grips) consult the flag (ADR-035 (a)). The obligation that buys: moving a PI
  /// must re-project the elevation points on its adjacent segments, or the line grows a visible kink
  /// (ADR-035 (b)).
  std::vector<int> featureLineOffsets;
  std::vector<double> featureLineVerts;
  std::vector<uint8_t> featureLineClosed;
  std::vector<uint8_t> featureLineElevPt;
  std::vector<CadFeatureLineInfo> featureLineInfo;
  std::vector<CadGradingPreset> gradingPresets;
  std::vector<EntityAttributes> featureLineAttrs;

  std::vector<CadAnnotation> cadAnnotations;
  std::vector<EntityAttributes> cadAnnotationAttrs;
  /// Solid-filled regions (ADR-011), imported from SOLID-fill HATCH; rendered filled in the overlay.
  std::vector<CadFilledRegion> cadFilledRegions;
  std::vector<EntityAttributes> cadFilledRegionAttrs;

  /// Imported triangle meshes (REQ-063 / ADR-026). Reference geometry: nothing in the command layer
  /// creates or edits one — REQ-065's glTF importer produces them and ERASE removes them.
  ///
  /// `shared_ptr<const>` so undo snapshots share the payload rather than copying it (architecture
  /// §11.5 as amended): a snapshot of a 2M-triangle model is a refcount bump, not ~53 MB.
  /// "Editing" a mesh means replacing the pointer; never write through it.
  std::vector<std::shared_ptr<const CadMesh>> cadMeshes;
  std::vector<EntityAttributes> cadMeshAttrs;
  /// Point clouds (REQ-171 / ADR-042). Reference geometry: nothing in the command layer creates or
  /// edits one — REQ-172's E57/PTS/PTX/LAS/LAZ importers produce them and ERASE removes them.
  ///
  /// `shared_ptr<const>` so undo snapshots share the payload rather than copying it (architecture
  /// §11.5), exactly as \ref cadMeshes above.
  std::vector<std::shared_ptr<const CadPointCloud>> cadPointClouds;
  std::vector<EntityAttributes> cadPointCloudAttrs;
  /// TIN surfaces (REQ-068). The heavy triangulation hangs off a shared_ptr inside each CadSurface,
  /// so copying this vector — which every undo snapshot does — is strings and refcount bumps.
  std::vector<CadSurface> cadSurfaces;
  std::vector<EntityAttributes> cadSurfaceAttrs;
  /// B-rep solids (REQ-313 / ADR-045). `shared_ptr<const>` for the same reason meshes are: an undo
  /// snapshot shares the payload instead of copying it, and immutability is what makes that safe.
  /// A solid is REPLACED, never written through.
  std::vector<CadSolidPtr> cadSolids;
  std::vector<EntityAttributes> cadSolidAttrs;
  /// Drawing TABLE entities (REQ-148 / D-2026-08-28-i). Rigid body: insertion, size, rotation, cells.
  std::vector<CadTable> cadTables;
  std::vector<EntityAttributes> cadTableAttrs;
  /// Table styles of this drawing (REQ-475). The built-in set; style editing is a follow-up.
  std::vector<CadTableStyle> tableStyles = CadTableBuiltinStyles();
  std::vector<CadPositionMarker> cadPositionMarkers;  ///< REQ-359 / D-2026-09-29-e
  std::vector<EntityAttributes> cadPositionMarkerAttrs;
  std::vector<CadMultileader> cadMultileaders;  ///< REQ-367 / issue #619
  std::vector<EntityAttributes> cadMultileaderAttrs;
  std::vector<CadSpline> cadSplines;  ///< REQ-443 / issue #803
  std::vector<EntityAttributes> cadSplineAttrs;
  std::vector<CadPoint> cadPoints;  ///< REQ-456 / issue #1019
  std::vector<EntityAttributes> cadPointAttrs;
  int pdMode = 0;       ///< AutoCAD PDMODE for CAD POINT display
  double pdSize = 0.0;  ///< AutoCAD PDSIZE; 0 = 5% of view

  /// Pipe runs (issue #486 increment B1 / REQ-345). The entity IS the path (topology); the swept
  /// solids (`AppCommandState::pipeRunWorldSolids`) are derived display data, rebuilt from
  /// \ref cadPipeRuns whenever it changes and never persisted — the same "piping owns topology,
  /// blocks own geometry" split CadPipeRun's own doc comment explains, and the same shape
  /// `blockRefWorldSolids` already uses for a derived solid array.
  std::vector<CadPipeRun> cadPipeRuns;
  std::vector<EntityAttributes> cadPipeRunAttrs;
  std::vector<alignment::Alignment> alignments;  ///< REQ-480: horizontal alignments, drawing-local doubles
  std::vector<alignment::ViewFrameGroup> viewFrameGroups;  ///< REQ-517: view frame groups that follow an alignment
  std::vector<alignment::SampleLine> sampleLines;  ///< REQ-519: sample lines and their sections (persist with the drawing)
  std::vector<alignment::VerticalProfile> profiles;  ///< REQ-521: design vertical profiles (persist with the drawing)
  std::vector<CadSurfaceProfile> surfaceProfiles; ///< REQ-513 saved existing-ground profiles
  /// Named piping networks (issue #486 increment B3 / REQ-345) — see `CadPipingSystem`'s own doc
  /// comment (CadEntities.hpp) for the ownership model. Metadata only, like `cadPipeRuns` itself;
  /// a network's indices are only meaningful against the drawing's OWN `cadPipeRuns`, so it travels
  /// with it. Persisted in `internal document payload` (additive array, ADR-020 (d)).
  std::vector<CadPipingSystem> cadPipingSystems;

  std::vector<CadBlockDefinition> blockDefs;
  std::vector<CadBlockRef> cadBlockRefs;
  std::vector<EntityAttributes> cadBlockRefAttrs;

  // --- Survey / COGO points ----------------------------------------------------------------------

  std::vector<SurveyPoint> surveyPoints;
  std::vector<SurveyPointStyleDefinition> surveyPointStyles = DefaultSurveyPointStyles();
  std::vector<SurveyPointLabelStyleDefinition> surveyPointLabelStyles = DefaultSurveyPointLabelStyles();
  /// Named point groups (REQ-067) — drawing-owned rules, resolved on demand, never cached.
  /// Undoable, like textStyles: creating or editing one is a single-step undo.
  std::vector<PointGroup> pointGroups;

  // --- Layers and styles -------------------------------------------------------------------------

  /// Layer table. Empty here; `AppCommandState` seeds layer "0" (issue #57).
  std::vector<CadLayerRow> drawingLayerTable;
  /// Named text styles (REQ-044 / ADR-020) — undoable so style edits undo.
  std::vector<TextStyle> textStyles;
  /// Named surface styles (REQ-070 / ADR-036 (d)) — undoable for the same reason textStyles is.
  ///
  /// The generated contours and borders these describe are **not** here and never will be: they live
  /// in `AppCommandState::surfaceDisplayCache`, which no snapshot touches (ADR-036 (e)). That split is
  /// what lets a style edit be undoable without a single contour entering the undo stack.
  std::vector<SurfaceStyle> surfaceStyles;
  std::vector<AlignmentStyle> alignmentStyles;
  std::vector<alignment::AlignmentCheckSet> alignmentCheckSets;  ///< REQ-502: drawing-owned check sets
  std::vector<AlignmentLabelSet> alignmentLabelSets;  ///< Named alignment label sets (REQ-492).
  std::vector<AlignmentLabelStyle> alignmentLabelStyles;  ///< REQ-492.5
  /// Centralized dimension style (issue #99) — single active style used by DIMALIGNED/DIMLINEAR/DIMANGULAR.
  /// Pure value type, undoable via snapshot (like textStyles/surfaceStyles).  New dimensions bake this
  /// style's effective values; existing dimensions read the active style at draw/measure time (phase 1).
  DimensionStyle activeDimensionStyle = DimensionStyles::Default();
  std::vector<DimensionStyle> dimensionStyles = DimensionStyles::DefaultStyles();
  MultileaderStyle activeMultileaderStyle = MultileaderStyles::Default();
  std::vector<MultileaderStyle> multileaderStyles = MultileaderStyles::DefaultStyles();

  // --- Underlays and paper space -----------------------------------------------------------------

  /// Committed PDF underlays. An undo frame zeroes each `glTexId` so a restore never references a
  /// freed GPU texture.
  std::vector<PdfAttachment> pdfAttachments;
  /// Paper layouts incl. native paper geometry (REQ-025/037/038) — undoable.
  std::vector<PaperLayout> paperLayouts;
  std::vector<CadRasterImage> cadRasterImages;  ///< model-space raster underlays (REQ-511)

  // --- Drawing properties ------------------------------------------------------------------------

  /// World coordinate of local (0,0). Geometry is stored in local space for float precision.
  double worldDocumentOriginX = 0.0;
  double worldDocumentOriginY = 0.0;
  /// Drawing unit, AutoCAD $INSUNITS code (REQ-022). A relabel only — never scales
  /// geometry. Document property: persisted in the drawing and the DXF header. Offered codes:
  /// 0=Unitless, 1=Inches, 2=Feet, 4=Millimeters, 6=Meters (REQ-357). Per tab and undoable.
  int drawingInsUnits = 2;
  /// Plot scale: one plotted inch equals this many drawing units (e.g. 50 for 1 inch = 50 feet).
  /// Per tab and undoable (REQ-357).
  float modelUnitsPerPlottedInch = 50.f;
  /// The drawing's Drawing Settings (REQ-357): per tab, undoable, saved in the trailer JSON.
  DrawingSettings drawingSettings;
};
