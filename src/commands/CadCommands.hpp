#pragma once

#include "CadEntities.hpp"
#include "EntityId.hpp"
// TextStyles::DefaultTextStyles, for the textStyles member's initializer (issue #57). Pure and
// dependency-free (string/vector/CadEntities.hpp), so this adds no cycle and no weight.
#include "TextStyle.hpp"
// SurfaceStyles::DefaultSurfaceStyles, for the surfaceStyles member's initializer — same reason,
// same shape, and pure for the same reason (REQ-070 / ADR-036 (d)).
#include "SurfaceStyle.hpp"
#include "render/Camera.hpp"  // Commands -> Renderer is a downward dependency (architecture §2)
#include "PdfAttach.hpp"
#include "PaperSpace.hpp"
#include "SurveyPoints.hpp"
#include "AngleFormat.hpp"
#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"
#include "update/UpdateCheck.hpp"  // update::UpdatePrefs only — pure, no network, no <thread>
#include "util/tinbuild.hpp"       // TinBuildResult, for AppCommandState::SurfaceRebuildAsync (REQ-069)
// HoverDwell, for AppCommandState's surface rollover timer (REQ-089). Pure and dependency-free
// (<cmath>), and deliberately in util/ rather than beside the UI that drives it: the state lives on
// AppCommandState, and Commands may not include a UI header (architecture §11.1).
#include "util/hoverdwell.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>


struct SelectedEntity {
  enum class Type {
    LineSeg = 0, Circle = 1, Annotation = 2, Polyline = 3, Arc = 4, Ellipse = 5, PdfUnderlay = 6,
    FilledRegion = 7, ///< Solid hatch fill (CadFilledRegion) — selectable/editable (REQ-042, ADR-016).
    Mesh = 8,         ///< Imported triangle mesh (REQ-063). Selectable and erasable, never edited.
    FeatureLine = 9,  ///< Named 3D design linework (REQ-087, ADR-035). Its own store, not a polyline.
    /// TIN surface (REQ-068, ADR-036 (b)). **Display-only, like Mesh** — it selects, highlights,
    /// erases and reports, and it never moves: a surface's geometry is DERIVED from its definition,
    /// so a drag would be undone by the next rebuild (ADR-036 alternative (5), declined by the user
    /// 2026-08-21). Every transform command refuses it with a stated reason (REQ-201) rather than
    /// silently dropping it from the operation.
    ///
    /// A click on any visible component — a triangle edge, a contour, the border — selects the whole
    /// surface. Component-level selection is forbidden by REQ-070 ("contours ... never appear in
    /// selection") and has nothing to be selected *by*: a contour is regenerated display geometry
    /// with no identity.
    Surface = 10
  };
  Type type = Type::LineSeg;
  int index = 0; ///< Entity index in the parallel container for \p type
};

// PaperEntityRef (selected paper-space entity) is defined in PaperSpace.hpp so header-only selection
// helpers can name it; CadCommands.hpp gets it via the include above (REQ-039).


/// One surface's block in the rollover readout (REQ-089) — what the panel beside the cursor says.
///
/// **Formatted text, not values, and no surface reference of any kind.** The row is latched when the
/// cursor comes to rest and read on later frames, and `cadSurfaces` compacts on erase, so carrying an
/// index would be architecture §11.9's blocking finding and carrying a pointer would be worse. Every
/// field is already the string the panel draws, which also keeps `displayLinearPrecision` and the
/// REQ-070 style fallback applied once at the query rather than re-derived per frame.
struct SurfaceHoverRow {
  std::string name;
  std::string style;      ///< the EFFECTIVE style name (REQ-070 resolution), never the stored one
  std::string layer;
  std::string elevation;  ///< already to `displayLinearPrecision`; "—" if there is no elevation
};


/// Named layer row for the layer manager (visibility / freeze / lock are stored for future viewport filtering).
struct CadLayerRow {
  std::string name;
  bool on = true;
  bool frozen = false;
  bool locked = false;
  /// Layer swatch color (same encoding as \ref EntityAttributes::color except not "ByLayer").
  std::string color = "White";
  std::string linetype = "Continuous";
  float lineweightMm = -1.f; ///< \c -1 = default (DXF layer 370 -3 on export).
  float transparency = 0.f;  ///< 0 opaque .. 1 fully transparent (layer-wide).
  bool plottable = true;     ///< when false, geometry/viewports on this layer are excluded from plots (REQ-029/030).
};


/// The layer table a brand-new drawing starts with — layer "0" and nothing else.
///
/// Layer "0" is documented as always existing, and \ref SyncDrawingLayerTableWithGeometry
/// synthesizes it when the table is empty. Issue #57: that synthesis happened on *load* but not at
/// *creation*, so a new drawing and the same drawing reopened were not the same document. The
/// definition lives here, beside \ref CadLayerRow, so the default row and the row the sync
/// synthesizes cannot drift apart.
inline std::vector<CadLayerRow> DefaultDrawingLayerTable() {
  CadLayerRow zero;
  zero.name = "0";
  return {zero};
}

/// Resolve stored color string + transparency to RGBA for viewport/UI (0..1). \p defaultRgb used for ByLayer.
void ResolveStoredColorForViewport(const std::string& colorStorage, float transparency, float defaultR,
                                   float defaultG, float defaultB, float* outRgba);

struct AppCommandState;

const CadLayerRow* FindDrawingLayerRowCi(const AppCommandState& st, const std::string& layerName);

float EffectiveEntityTransparency01(const EntityAttributes& e, const CadLayerRow* layer);

float EffectiveEntityLineweightMm(const EntityAttributes& e, const CadLayerRow* layer);

std::string EffectiveEntityLinetypeNameForViewport(const EntityAttributes& e, const CadLayerRow* layer);

void ResolveEntityRgbaForViewport(const EntityAttributes& attr, const CadLayerRow* layer, float defaultR,
                                    float defaultG, float defaultB, float* outRgba);

int CadDxfLineweightEnum370FromMm(float mm);

float CadDxfLineweightMmFromEnum370(int code);


inline void ResolveEntityColorForViewport(const EntityAttributes& attr, float defaultR, float defaultG,
                                          float defaultB, float* outRgba) {
  ResolveEntityRgbaForViewport(attr, nullptr, defaultR, defaultG, defaultB, outRgba);
}


// EntityAttributes and CadAnnotation are defined in CadEntities.hpp (shared with PaperSpace.hpp).

/// Fills dimension-line feet, unit tangent \p T along measurement, left normal \p N, and chord length. False if degenerate.
bool CadDimAlignedGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                            float* ty, float* nx, float* ny, float* measLen);

/// Horizontal linear dimension: world-X span between extension X coordinates; dimension line is horizontal.
bool CadDimLinearGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,
                          float* ty, float* nx, float* ny, float* measLen);

/// \ref CadDimAlignedGeometry or \ref CadDimLinearGeometry based on \p a.kind.
bool CadDimAnyGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx, float* ty,
                       float* nx, float* ny, float* measLen);

/// Live DIMALIGNED preview after extension points are set (\p st.dimPhase == WaitDimLinePt).
bool CadDimAlignedBuildDraft(const AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out);

/// Updates \p st.dimLinearDraftVertical from cursor vs chord midpoint unless user locked with H/V.
void CadDimLinearUpdateDraftOrientation(AppCommandState& st, float cursorWx, float cursorWy);
/// \p vertical true = measure |ΔY| (vertical dimension line); false = measure |ΔX| (horizontal dim line).
/// Locks orientation until the crosshair moves enough to clear the user lock.
void CadDimLinearApplyHVHotkey(AppCommandState& st, bool vertical, std::vector<std::string>& log);
/// Live DIMLINEAR preview (\p st.active == DimLinear, \p st.dimPhase == WaitDimLinePt).
bool CadDimLinearBuildDraft(AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out);
/// Format a positive angle (radians) as \c D°M'S" with decimal seconds (survey style).
std::string CadFormatAngleDegMinSecFromRad(float angleRad);
/// Whole-circle bearing in **degrees** clockwise from north (e.g. from \ref BearingCwNorthDegFromMathAngleRad) as
/// \c D°M'S" with decimal seconds; normalized to \c [0,360).
std::string CadFormatBearingCwNorthDegMinSec(float bearingDegClockwiseFromNorth);
/// Live DIMANGULAR preview (\p st.active == DimAngular, \p dimAngularPhase == WaitArc).
bool CadDimAngularBuildDraft(const AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out);
/// After vertex / ray / radius edits, re-place label along the angle bisector.
void CadDimAngularSyncTextPlacement(CadAnnotation* ann, float modelUnitsPerPlottedInch);
/// After editing extension points or dimension offset, restore text from fixed (normal, tangent) offsets vs dim mid.
void CadDimAlignedApplyInsFromLocalOffset(CadAnnotation* ann, float alongN, float alongT);
/// Recompute dimension text from current geometry (linear / aligned / angular).
void CadDimRefreshMeasurementText(CadAnnotation* ann, int linearPrecision, const AngleDisplaySettings& angle);

// CadArc and CadEllipse are defined in CadEntities.hpp (shared with PaperSpace.hpp, ADR-013).

/// Optional batched polylines / arcs / ellipses for the viewport (nullptr = none).
struct CadExtendedGeometryInput {
  const std::vector<CadArc>* arcs = nullptr;
  const std::vector<EntityAttributes>* arcAttrs = nullptr;
  const std::vector<CadEllipse>* ellipses = nullptr;
  const std::vector<EntityAttributes>* ellAttrs = nullptr;
  const std::vector<float>* polylineVerts = nullptr;
  const std::vector<int>* polylineOffsets = nullptr;
  const std::vector<uint8_t>* polylineClosed = nullptr;
  const std::vector<EntityAttributes>* polylineAttrs = nullptr;
  // Feature lines (REQ-087). Same four arrays, same shape — the renderer draws both through one
  // function, so a feature line cannot render differently from a polyline by accident.
  const std::vector<float>* featureLineVerts = nullptr;
  const std::vector<int>* featureLineOffsets = nullptr;
  const std::vector<uint8_t>* featureLineClosed = nullptr;
  const std::vector<EntityAttributes>* featureLineAttrs = nullptr;
  const std::vector<CadLayerRow>* drawingLayers = nullptr;
  /// Sorted stable entity ids hidden by object isolation (REQ-084 (d), ADR-034); nullptr or empty
  /// means nothing is hidden. Carried here rather than as another `RenderScene` parameter — that
  /// signature is already long, and this struct is exactly "the extra per-entity data the renderer
  /// needs", which is what the hidden set is.
  const std::vector<std::uint64_t>* hiddenEntityIds = nullptr;
};

/// True when a CSR chain store (polylines, feature lines) holds at least one entity.
///
/// `offsets` is CSR: N entities need N+1 offsets, so fewer than two offsets is zero entities — and
/// an "empty" store is legitimately either `{}` or `{0}` (issue #60), which is exactly why this is a
/// named predicate rather than an `!empty()` written out at each call site.
[[nodiscard]] inline bool CadChainHasEntities(const std::vector<float>* verts,
                                              const std::vector<int>* offsets) {
  return verts != nullptr && offsets != nullptr && offsets->size() >= 2;
}

/// True when this extended input holds anything the viewport must draw.
///
/// A named predicate rather than a condition spelled out in the renderer, because it is a **list**,
/// and a list is what this entity keeps falling out of: by 2026-08-20 a feature line had been
/// omitted from the viewport-click routing (twice), CancelActiveCommand, ResetAllCadDraftTools, the
/// rubber-band preview, and this gate — where the symptom was that a drawing containing ONLY a
/// feature line rendered nothing at all, while still hovering and selecting, because the whole
/// committed-geometry block was skipped. One predicate, one place to add the next entity kind, and
/// a test that fails when someone forgets. REQ-087.
[[nodiscard]] inline bool CadExtendedHasDrawableGeometry(const CadExtendedGeometryInput& e) {
  if (e.arcs != nullptr && !e.arcs->empty())
    return true;
  if (e.ellipses != nullptr && !e.ellipses->empty())
    return true;
  if (CadChainHasEntities(e.polylineVerts, e.polylineOffsets))
    return true;
  if (CadChainHasEntities(e.featureLineVerts, e.featureLineOffsets))
    return true;
  return false;
}

/// True when \p id is in the sorted hidden-id set. Empty set / unassigned id (0) → never hidden.
/// Inline and early-outing so the non-isolated case costs one `empty()` test per entity (REQ-100).
inline bool CadEntityIdHidden(const std::vector<std::uint64_t>* hidden, std::uint64_t id) {
  if (!hidden || hidden->empty() || id == 0)
    return false;
  return std::binary_search(hidden->begin(), hidden->end(), id);
}

/// The set ISOLATEOBJECTS hides: sorted-unique `all` minus `keep` (REQ-084 (d)).
///
/// Pure and header-inline so it is covered by tests — `CadCommands.cpp`, which owns the command
/// itself, pulls in the GUI stack and cannot be linked by the test target. Inputs need not arrive
/// sorted; the result always is, which is the invariant \ref CadEntityIdHidden's binary search
/// depends on.
inline std::vector<std::uint64_t> CadIsolationHiddenSet(std::vector<std::uint64_t> all,
                                                        std::vector<std::uint64_t> keep) {
  auto sortUnique = [](std::vector<std::uint64_t>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  };
  sortUnique(all);
  sortUnique(keep);
  std::vector<std::uint64_t> hide;
  hide.reserve(all.size());
  std::set_difference(all.begin(), all.end(), keep.begin(), keep.end(), std::back_inserter(hide));
  return hide;
}

/// Time-sensitive right-click verdict (REQ-084 (b)): true when the press was held long enough to
/// mean "shortcut menu", false when it was a quick click and therefore an ENTER. Held exactly at
/// the threshold counts as the menu, so the boundary belongs to one verdict and not to neither.
inline bool CadRightClickHoldIsMenu(double heldMs, int thresholdMs) {
  return heldMs >= static_cast<double>(thresholdMs);
}


inline float CadAnnotationHeightWorld(const CadAnnotation& a, float modelUnitsPerPlottedInch) {
  return a.plottedHeightInches * std::max(modelUnitsPerPlottedInch, 1.e-6f);
}

/// Axis-aligned rough bounds for hit-testing / zoom (TEXT uses estimated glyph width).
void CadAnnotationRoughBounds(const CadAnnotation& a, float modelUnitsPerPlottedInch, float* outMnX, float* outMnY,
                              float* outMxX, float* outMxY);

/// Top-most annotation under point; -1 if none. Uses pixel tolerance from viewport half-height.
int PickCadAnnotationAt(float wx, float wy, const AppCommandState& cmd, float orthoHalfHeightWorld,
                        float viewportHeightPx);

/// ROTATE live preview angle (rad) about \ref AppCommandState::rotateBase when cursor drives preview.
bool CadRotatePreviewTheta(const AppCommandState& cmd, float curX, float curY, float* outThetaRad);
/// \c Kind::Scale, \c modifyPhase == NeedDestination — live scale from cursor (see \ref AppCommandState::scalePhase).
bool CadScalePreviewFactor(const AppCommandState& cmd, float curX, float curY, float* outScale);

/// MOVE/COPY destination drag or ROTATE angle preview — ghost annotations for ImGui overlay.
/// New CAD entities: mirror this pattern — GL rubber (\p main.cpp) + \ref CadAnnotationCollectTransformPreviews / UI overlay (\p CadUi.cpp) + grips (\p CadUi.cpp).
void CadAnnotationCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,
                                           std::vector<CadAnnotation>* out);


/// In-process clipboard for COPYCLIP / PASTECLIP.  Geometry stored at local (storage) coordinates.
struct CadClipboard {
  float basePtX = 0.f; ///< Bounding-box center X used as paste anchor (local space).
  float basePtY = 0.f;

  std::vector<float>            lines;
  std::vector<EntityAttributes> lineAttrs;
  /// Flat cx,cy,z,r quads (REQ-057 / ADR-025 (a)). A copy from paper space stores z = 0, and a
  /// paste into paper space drops z — the sheet is 2D (ADR-025 (g)), so Z collapses at that
  /// boundary rather than silently riding along.
  std::vector<float>            circlesCxCyZR;
  std::vector<EntityAttributes> circleAttrs;
  std::vector<CadArc>           arcs;
  std::vector<EntityAttributes> arcAttrs;
  std::vector<CadEllipse>       ellipses;
  std::vector<EntityAttributes> ellAttrs;
  std::vector<int>              polyOffsets; ///< Self-contained offset table (starts with 0).
  std::vector<float>            polyVerts;
  std::vector<uint8_t>          polyClosed;
  std::vector<EntityAttributes> polyAttrs;
  std::vector<CadAnnotation>    annotations;
  std::vector<EntityAttributes> annotationAttrs;
  std::vector<CadFilledRegion>  filledRegions;     ///< Solid fills enclosed by the copy selection (REQ-038 addendum).
  std::vector<EntityAttributes> filledRegionAttrs;
  /// Source space of the copy. Text height has different units per space (model: plotted-inches × scale = model
  /// units; paper: paper inches), so a cross-space paste scales annotation height by modelUnitsPerPlottedInch.
  bool fromPaper = false;

  bool empty() const {
    return lines.empty() && circlesCxCyZR.empty() && arcs.empty() && ellipses.empty() &&
           (polyOffsets.size() <= 1) && annotations.empty() && filledRegions.empty();
  }
};


/// Geometry-only snapshot for undo/redo.  PDF glTexId is zeroed to avoid stale GPU references.
struct DrawingGeometrySnapshot {
  std::vector<float>            userLinesFlat;
  std::vector<EntityAttributes> userLineAttrs;
  std::vector<float>            userCirclesCxCyZR;
  std::vector<EntityAttributes> userCircleAttrs;
  std::vector<CadArc>           userArcs;
  std::vector<EntityAttributes> userArcAttrs;
  std::vector<CadEllipse>       userEllipses;
  std::vector<EntityAttributes> userEllAttrs;
  std::vector<int>              userPolylineOffsets;
  std::vector<float>            userPolylineVerts;
  std::vector<uint8_t>          userPolylineClosed;
  std::vector<EntityAttributes> userPolylineAttrs;
  // Feature lines (REQ-087) — their own store, never the polyline arrays (ADR-035 (g)).
  std::vector<int>                featureLineOffsets;
  std::vector<float>              featureLineVerts;
  std::vector<uint8_t>            featureLineClosed;
  std::vector<uint8_t>            featureLineElevPt;
  std::vector<CadFeatureLineInfo> featureLineInfo;
  std::vector<EntityAttributes>   featureLineAttrs;
  std::vector<CadAnnotation>    cadAnnotations;
  std::vector<EntityAttributes> cadAnnotationAttrs;
  std::vector<CadFilledRegion>  cadFilledRegions;
  std::vector<EntityAttributes> cadFilledRegionAttrs;
  /// Imported meshes (REQ-063). Shared, not copied: see CadMesh's note and architecture §11.5 as
  /// amended 2026-08-12 — a snapshot of a 2M-triangle model is a refcount bump, not ~53 MB.
  std::vector<std::shared_ptr<const CadMesh>> cadMeshes;
  std::vector<EntityAttributes> cadMeshAttrs;
  /// TIN surfaces (REQ-068). Shared payload, not copied — see CadTin and architecture §11.5.
  std::vector<CadSurface>       cadSurfaces;
  std::vector<EntityAttributes> cadSurfaceAttrs;
  std::vector<SurveyPoint>      surveyPoints;
  /// Named point groups (REQ-067). Undoable, like textStyles — creating or editing one is a
  /// single-step undo. Rules only; membership is never stored.
  std::vector<PointGroup>       pointGroups;
  std::vector<CadLayerRow>      drawingLayerTable;
  std::vector<TextStyle>        textStyles;    ///< Named text styles (REQ-044) — undoable so style edits undo.
  /// Named surface styles (REQ-070 / ADR-036 (d)) — undoable for the same reason textStyles is.
  ///
  /// The generated contours and borders these describe are **not** here and never will be: they live
  /// in `AppCommandState::surfaceDisplayCache`, which no snapshot touches (ADR-036 (e)). That split is
  /// what lets a style edit be undoable without a single contour entering the undo stack.
  std::vector<SurfaceStyle>     surfaceStyles;
  std::vector<PdfAttachment>    pdfAttachments;
  std::vector<PaperLayout>      paperLayouts;  ///< Paper layouts incl. native paper geometry (REQ-037/038) — undoable.
  double worldDocumentOriginX = 0.0;
  double worldDocumentOriginY = 0.0;
  std::string description;
};


/// Snapshot of all per-drawing data.  AppCommandState holds the live (active-tab) copy directly;
/// switching tabs saves the active fields here and restores the target tab's snapshot.
struct DrawingDocument {
  double viewportPanX = 0.0;
  double viewportPanY = 0.0;
  float  viewportZoom = 1.f;
  double viewportPanZ = 0.0;          ///< Camera target elevation per tab (REQ-058).
  float  viewportAzimuthDeg = 0.f;    ///< Camera orientation per tab (REQ-058); plan view by default.
  float  viewportElevationDeg = 90.f;
  double worldDocumentOriginX = 0.0;
  double worldDocumentOriginY = 0.0;
  /// Per-drawing entity-id counter (REQ-076). Saved/restored with the tab so two open drawings
  /// number independently; see AppCommandState::nextEntityId for why undo never rewinds it.
  std::uint64_t nextEntityId = 1;

  std::vector<float>            userLinesFlat;
  std::vector<EntityAttributes> userLineAttrs;
  std::vector<float>            userCirclesCxCyZR;
  std::vector<EntityAttributes> userCircleAttrs;
  std::vector<CadArc>           userArcs;
  std::vector<EntityAttributes> userArcAttrs;
  std::vector<CadEllipse>       userEllipses;
  std::vector<EntityAttributes> userEllAttrs;
  std::vector<int>              userPolylineOffsets;
  std::vector<float>            userPolylineVerts;
  std::vector<uint8_t>          userPolylineClosed;
  std::vector<EntityAttributes> userPolylineAttrs;
  // Feature lines (REQ-087) — their own store, never the polyline arrays (ADR-035 (g)).
  std::vector<int>                featureLineOffsets;
  std::vector<float>              featureLineVerts;
  std::vector<uint8_t>            featureLineClosed;
  std::vector<uint8_t>            featureLineElevPt;
  std::vector<CadFeatureLineInfo> featureLineInfo;
  std::vector<EntityAttributes>   featureLineAttrs;
  std::vector<CadAnnotation>    cadAnnotations;
  std::vector<EntityAttributes> cadAnnotationAttrs;
  std::vector<CadFilledRegion>  cadFilledRegions;
  std::vector<EntityAttributes> cadFilledRegionAttrs;
  std::vector<std::shared_ptr<const CadMesh>> cadMeshes;  ///< REQ-063; shared, see CadMesh's note.
  std::vector<EntityAttributes> cadMeshAttrs;
  std::vector<CadSurface>       cadSurfaces;       ///< TIN surfaces (REQ-068).
  std::vector<EntityAttributes> cadSurfaceAttrs;
  std::vector<SurveyPoint>      surveyPoints;
  std::vector<PointGroup>       pointGroups;            ///< Named point groups (REQ-067).
  std::vector<int>              selectedSurveyPointIndices;
  std::vector<CadLayerRow>      drawingLayerTable;
  std::vector<TextStyle>        textStyles;             ///< Named text styles (REQ-044).
  std::vector<SurfaceStyle>     surfaceStyles;          ///< Named surface styles (REQ-070).
  std::string                   activeTextStyleName = "Standard";  ///< Style for new TEXT/MTEXT.
  std::vector<PdfAttachment>    pdfAttachments;
  std::vector<SelectedEntity>   selection;
  /// Object isolation (REQ-084 (d)). PER TAB, and stored here for the same reason `selection` is:
  /// entity ids are unique **within a drawing**, so carrying one tab's hidden set into another
  /// would hide whatever objects happened to draw those numbers there. Never written to `.gs` —
  /// this struct is the in-memory tab store, not the file format.
  std::vector<std::uint64_t>    hiddenEntityIds;
  std::vector<PaperLayout>      paperLayouts;            ///< Paper-space layouts (REQ-025); empty = none.
  std::vector<PageSetup>        savedPageSetups;         ///< Drawing-wide named page setups.
  int                           activeSpaceIndex = kModelSpaceIndex;  ///< -1 = model; else index into paperLayouts.
  uint32_t cadGpuRevision  = 0;
  uint32_t savedRevision   = 0;   ///< cadGpuRevision at last save; != cadGpuRevision means unsaved changes.
  std::string filePath;           ///< Absolute path to the .gs file, empty if never saved.
  std::vector<DrawingGeometrySnapshot> undoStack;
  std::vector<DrawingGeometrySnapshot> redoStack;
};

/// Copy the active per-drawing fields from \p cmd into \p cmd.documents[idx].
void SaveDocumentToSnapshot(AppCommandState& cmd, int idx);
/// Copy \p cmd.documents[idx] back into the active fields of \p cmd.  Cancels any in-progress command.
void RestoreDocumentFromSnapshot(AppCommandState& cmd, int idx);

/// Active text style (REQ-044): the entry in \c st.textStyles named by \c st.activeTextStyleName, falling
/// back to "Standard"; nullptr only if the table is somehow empty. Used by the dropdown and the create path.
const TextStyle* ActiveTextStyle(const AppCommandState& st);
/// Set the active text style and sync the new-text default height to it, so newly drawn TEXT/MTEXT adopt
/// the style's height through the existing height plumbing (bake-on-write — ADR-020).
void SetActiveTextStyle(AppCommandState& st, const std::string& name);

// --- Paper space (REQ-025) ---
/// Append a new paper layout with a unique default name; returns its index.
int  AddPaperLayout(AppCommandState& cmd);
/// Delete the layout at \p idx, fixing up the active space.
void DeletePaperLayout(AppCommandState& cmd, int idx);
/// Set the active space: kModelSpaceIndex for model, else a paper-layout index (clamped).
void SetActiveSpace(AppCommandState& cmd, int spaceIndex);
/// Toggle between model space and the last/first paper layout (creating one if none exist).
void ToggleModelPaperSpace(AppCommandState& cmd);
/// Append a default viewport (centered on the model) to layout \p layoutIdx; returns its index (REQ-027).
int  AddViewport(AppCommandState& cmd, int layoutIdx);
/// Append a viewport with an explicit paper-inch rect (corners may be unordered); returns its index.
int  AddViewportRect(AppCommandState& cmd, int layoutIdx, float x0In, float y0In, float x1In, float y1In);
/// Delete viewport \p vpIdx from layout \p layoutIdx, fixing up the selection.
void DeleteViewport(AppCommandState& cmd, int layoutIdx, int vpIdx);
/// Start the rectangular-viewport command (REQ-033); requires an active paper layout.
void StartPaperRectViewportCommand(AppCommandState& cmd, std::vector<std::string>& log);

// --- Paper-space viewport selection / edit (REQ-035) ---
bool IsViewportSelected(const AppCommandState& cmd, int vi);
/// Select viewport \p vi in the active layout; \p additive toggles it within the current selection.
void SelectViewport(AppCommandState& cmd, int vi, bool additive);
void ClearViewportSelection(AppCommandState& cmd);
/// Delete all selected viewports in the active layout.
void DeleteSelectedViewports(AppCommandState& cmd, std::vector<std::string>& log);
/// Move (or, if \p copy, duplicate) the selected viewports by a paper-inch delta.
void TranslateSelectedViewports(AppCommandState& cmd, float dxIn, float dyIn, bool copy,
                                std::vector<std::string>& log);
/// Begin a two-click MOVE/COPY of the selected viewports (paper-inch base → destination).
void StartPaperMoveCopyViewports(AppCommandState& cmd, bool copy, std::vector<std::string>& log);

// --- Per-viewport layer freeze (REQ-028) ---
/// Toggle the frozen state of a layer in a viewport.
void ToggleFrozenLayerInViewport(Viewport& vp, const std::string& layerName);
/// Check if a layer is frozen in a viewport.
bool IsLayerFrozenInViewport(const Viewport& vp, const std::string& layerName);

// --- Per-viewport layer overrides UI/commands (REQ-046) ---
/// The "current viewport" the Layer Manager VP columns and VPFREEZE/VPTHAW act on: the floating
/// viewport if inside one (REQ-036), else the single selected viewport in the active paper layout,
/// else nullptr (no current viewport → VP controls disabled).
Viewport* CurrentViewport(AppCommandState& st);
/// Begin VPFREEZE / VPTHAW: pick entities in the current (floating) viewport to freeze/thaw their layers.
void StartVpFreezeCommand(AppCommandState& st, std::vector<std::string>& log);
void StartVpThawCommand(AppCommandState& st, std::vector<std::string>& log);

// --- Floating model space (REQ-036) ---
bool InFloatingModelSpace(const AppCommandState& cmd);

/// REQ-036: grab the nearest grip of a selected entity within \p tolWorld of (lx,ly) in LOCAL model coords;
/// arms the grip drag and stores originals (for the floating viewport, where the screen-space grab cannot be
/// used through the viewport transform). Returns true if a grip was grabbed.
bool TryBeginEntityGripAtLocal(AppCommandState& cmd, float lx, float ly, float tolWorld);

/// REQ-037 / ADR-009: the active layout's paper-space geometry store a draw/edit command writes to,
/// or nullptr when the command targets model space (model space active, or floating model space).
PaperLayout* ActivePaperGeometryTarget(AppCommandState& st);

// Native paper-space geometry selection + edit (REQ-037). Indices are into the ACTIVE layout's stores.
void ClearPaperEntitySelection(AppCommandState& st);
/// Topmost paper entity (text over line) within \p tolIn of (x,y) in paper inches; false if none.
bool PickPaperEntityAt(const PaperLayout& L, float x, float y, float tolIn, PaperEntityRef* out);
void TogglePaperEntitySelection(AppCommandState& st, PaperEntityRef ref, bool additive);
void DeleteSelectedPaperEntities(AppCommandState& st, std::vector<std::string>& log);
void TranslateSelectedPaperEntities(AppCommandState& st, float dxIn, float dyIn, bool copy,
                                    std::vector<std::string>& log);
void RotateSelectedPaperEntities(AppCommandState& st, float baseX, float baseY, float angRad,
                                 std::vector<std::string>& log);
/// Enter floating model space for viewport \p vpIdx of layout \p layoutIdx (edit the model through it).
void EnterFloatingModelSpace(AppCommandState& cmd, int layoutIdx, int vpIdx, std::vector<std::string>& log);
/// Save the floating view back to the viewport and return to paper space.
void ExitFloatingModelSpace(AppCommandState& cmd, std::vector<std::string>& log);

// --- Page setups (named, drawing-wide) ---
/// Ensure a built-in "Standard" page setup exists in cmd.savedPageSetups.
void EnsureStandardPageSetup(AppCommandState& cmd);
/// Copy a PageSetup's paper-size/orientation/plot fields into a layout (Set Current).
void ApplyPageSetupToLayout(PaperLayout& layout, const PageSetup& ps);
/// Snapshot a layout's current paper/plot fields into a PageSetup (for New "start with *layout*").
PageSetup PageSetupFromLayout(const PaperLayout& layout, const std::string& name);
/// Reorder/copy a layout to before \p beforeIdx (== size → move to end); copy clones it. Move-or-Copy.
void MoveOrCopyLayout(AppCommandState& cmd, int layoutIdx, int beforeIdx, bool makeCopy, std::vector<std::string>& log);

struct AppCommandState {
  enum class Kind {
    None,
    Line,
    Circle,
    Polyline,
    /// REQ-087. Its own command Kind, unlike 3DPOLY which is a mode of POLYLINE — a feature line
    /// commits to a different store, so the two cannot share a commit path.
    FeatureLine,
    Arc,
    Ellipse,
    Text,
    Mtext,
    DimAligned,
    DimLinear,
    DimAngular,
    Move,
    Copy,
    Rotate,
    Scale,
    Delete,
    Zoom,
    Join,
    Trim,
    Offset,
    IdPoint,
    /// Two-point inverse: horizontal distance and bearing (clockwise from north) between picks (World X=E, Y=N).
    SurveyInverse,
    /// REQ-074: one pick reports interpolated surface elevation, a second reports grade between them.
    SurfaceElevGrade,
    /// REQ-069: one pick designates a Line/Polyline as a breakline on a named surface.
    DesignateBreakline,
    /// REQ-069: one pick designates a closed Polyline as a boundary ring (outer/hide/show) on a named surface.
    DesignateBoundary,
    /// PDF underlay attach — opens dialog, then optionally waits for viewport picks.
    PdfAttach,
    /// 2-D Helmert (similarity) transformation from user-picked control point pairs.
    Align,
    /// Clipboard paste — cursor-following preview; one viewport click places the pasted entities.
    Paste,
    /// Paper space: create a rectangular viewport by two clicks (REQ-033).
    PaperRectViewport,
    /// HATCH: pick an internal point; trace the enclosing boundary and fill it (REQ-043).
    Hatch,
    /// PAN: interactive view pan — left-drag pans the active view; Esc/Enter/right-click exits (REQ-045).
    Pan,
    /// VPFREEZE / VPTHAW: pick entities in the current viewport; their layers freeze/thaw in it (REQ-046).
    VpFreeze,
    VpThaw,
    /// RECT: two opposite corners produce an axis-aligned rectangle, stored as a 4-vertex CLOSED polyline
    /// exactly as AutoCAD's RECTANG produces an LWPOLYLINE (REQ-053).
    Rect,
    /// TRIMSTATE: system-variable prompt waiting for a new value (REQ-056).
    TrimState,
    Elev,        ///< Set the elevation new geometry is drawn at (REQ-058).
    /// ORBIT: interactive free orbit — left-drag tumbles the model view; Esc/Enter/right-click
    /// exits (REQ-084 (c)). Deliberately shaped like \c Kind::Pan, and reuses the same
    /// Shift+middle-drag orbit math, so the shortcut menu's Free Orbit is a real command.
    Orbit,
  } active = Kind::None;

  static const char* KindName(Kind k) {
    switch (k) {
    case Kind::Line:          return "LINE";
    case Kind::Circle:        return "CIRCLE";
    case Kind::Polyline:      return "POLYLINE";
    case Kind::FeatureLine:   return "FEATURELINE";
    case Kind::Arc:           return "ARC";
    case Kind::Ellipse:       return "ELLIPSE";
    case Kind::Text:          return "TEXT";
    case Kind::Mtext:         return "MTEXT";
    case Kind::DimAligned:    return "DIMALIGNED";
    case Kind::DimLinear:     return "DIMLINEAR";
    case Kind::DimAngular:    return "DIMANGULAR";
    case Kind::Move:          return "MOVE";
    case Kind::Copy:          return "COPY";
    case Kind::Rotate:        return "ROTATE";
    case Kind::Scale:         return "SCALE";
    case Kind::Delete:        return "DELETE";
    case Kind::Zoom:          return "ZOOM";
    case Kind::Join:          return "JOIN";
    case Kind::Trim:          return "TRIM";
    case Kind::Offset:        return "OFFSET";
    case Kind::IdPoint:       return "ID";
    case Kind::SurveyInverse: return "INVERSE";
    case Kind::PdfAttach:     return "PDFATTACH";
    case Kind::Align:         return "ALIGN";
    case Kind::Paste:         return "PASTE";
    case Kind::PaperRectViewport: return "MVIEW";
    case Kind::Hatch:         return "HATCH";
    case Kind::Pan:           return "PAN";
    case Kind::VpFreeze:      return "VPFREEZE";
    case Kind::VpThaw:        return "VPTHAW";
    case Kind::Rect:          return "RECT";
    case Kind::TrimState:     return "TRIMSTATE";
    case Kind::Orbit:         return "ORBIT";
    case Kind::SurfaceElevGrade:   return "SURFELEV";
    case Kind::DesignateBreakline: return "DESIGNATEBREAKLINE";
    case Kind::DesignateBoundary:  return "DESIGNATEBOUNDARY";
    default:                  return "";
    }
  }

  /// Most recently started command; used for right-click repeat when idle.
  Kind lastCommand = Kind::None;
  /// Right-click in the drawing with no active command repeats \c lastCommand (see Settings → Drafting).
  bool rightClickRepeatLastCommand = true;

  /// Right-click behavior per context (User Preferences → Right Click Options).
  enum class RightClickDefaultMode  : uint8_t { RepeatLastCommand = 0, ShortcutMenu = 1 };
  enum class RightClickEditMode     : uint8_t { RepeatLastCommand = 0, ShortcutMenu = 1 };
  enum class RightClickCommandMode  : uint8_t { Enter = 0, ShortcutMenuAlways = 1, ShortcutMenuWhenOptions = 2 };
  RightClickDefaultMode rightClickDefaultMode   = RightClickDefaultMode::RepeatLastCommand;
  // AutoCAD ships Edit Mode on the shortcut menu: with a selection, right-click offers MOVE/COPY/ROTATE/
  // SCALE/DELETE and Select similar. Repeating the last command there hides that menu entirely.
  RightClickEditMode    rightClickEditMode      = RightClickEditMode::ShortcutMenu;
  RightClickCommandMode rightClickCommandMode   = RightClickCommandMode::Enter;

  // --- Time-sensitive right-click (REQ-084 (b)) ---
  // OFF by default: turning it on changes how EVERY right-click in the drawing is read, so an
  // existing profile must not acquire it on upgrade. While on, the hold duration — not
  // \c rightClickDefaultMode / \c rightClickCommandMode — decides the Default and Command
  // contexts, which is why the dialog greys those two groups rather than quietly ignoring them.
  bool  rightClickTimeSensitive = false;   ///< Persisted.
  /// Hold longer than this to get the shortcut menu; release sooner and it is an ENTER. Persisted.
  /// AutoCAD's default is 250 ms; clamped to a sane range on load and in the dialog.
  int   rightClickLongerClickMs = 250;
  static constexpr int kRightClickMinMs = 100;
  static constexpr int kRightClickMaxMs = 2000;
  /// Live state for the classifier — the moment the button went down, and whether this press has
  /// already been resolved. Not persisted: a press does not survive a restart.
  double rightClickPressTimeSec  = 0.0;
  bool   rightClickPressPending  = false;
  /// Right-Click Customization dialog open (Options → User Preferences). Not persisted.
  bool   showRightClickDialog    = false;

  /// Plot scale: one plotted inch equals this many drawing units (e.g. 50 for 1 inch = 50 feet).
  float modelUnitsPerPlottedInch = 50.f;
  float defaultPlottedTextHeightInches = 0.125f;

  /// Drawing unit, AutoCAD $INSUNITS code (REQ-022). A relabel only — never scales
  /// geometry. Document property: persisted in .gs and the DXF header. Only the
  /// survey-relevant codes are offered: 0=Unitless, 2=Feet, 6=Meters.
  int drawingInsUnits = 2;
  /// Survey point X marker: horizontal span on paper (inches) → world half-extent = 0.5 × span × MUP (not zoom).
  float surveyPointCrossSpanPlottedInches = 0.14f;
  bool surveyPointShowIdInViewport = false;
  /// Decimal places shown for survey-point coordinates (labels on the drawing
  /// and the survey points table/editor). Display only — stored values keep full
  /// precision. Configured in Settings → User Preferences → Survey points.
  int surveyPointDisplayPrecision = 4;
  /// Plotted text height (inches) for survey point ID labels when \ref surveyPointShowIdInViewport is true.
  float surveyPointLabelPlottedHeightInches = 0.10f;
  SurveyLabelStyleTemplates surveyLabelTemplates;
  /// Label MTEXT: east offset of the label's **left edge** from the point (plotted inches × MUP →
  /// world). The box grows east from this edge, so longer text never grows back toward the point.
  float surveyLabelOffsetEastPlottedIn = 0.35f;
  /// North offset of the label's **vertical centre** from the point (plotted inches × MUP). The box
  /// is centred on this, so the label sits beside the point rather than hanging beneath it.
  float surveyLabelOffsetNorthPlottedIn = 0.f;
  /// Legacy fixed box (plotted inches); ignored for auto-sized survey-linked MTEXT labels.
  float surveyLabelBoxWidthPlottedIn = 1.5f;
  float surveyLabelBoxHeightPlottedIn = 0.75f;
  /// Leader arrow: filled triangle base half-width (px); length = arrowPx * 2.36.
  float surveyLabelLeaderArrowPx = 5.5f;
  /// Drawing viewport: survey index under cursor (-1 if none), for hover feedback.
  int viewportHoverSurveyPointIndex = -1;
  /// Drawing viewport: CAD entity under cursor when idle (no command active), for hover highlight feedback.
  bool viewportHoverEntityValid = false;
  SelectedEntity viewportHoverEntity{};
  /// Paper layout: native paper-space entity under the cursor when idle, for hover highlight parity (REQ-039).
  bool paperHoverValid = false;
  PaperEntityRef paperHover{};

  /// Drawing viewport: the surface rollover readout (REQ-089) — how long the cursor has rested, and
  /// what to say about the surfaces under it.
  ///
  /// **The rows hold formatted text, not a surface index.** `cadSurfaces` compacts on erase, so an
  /// index latched on one frame and read on the next would designate a different surface
  /// (architecture §11.9); latching the text means there is nothing to dangle. It also means the
  /// readout survives a rebuild that replaces the triangulation underneath it, showing the numbers
  /// that were true when the cursor came to rest rather than a mixture of two moments.
  ///
  /// **Filled once per rest, never per frame** — REQ-089's last acceptance condition. \ref
  /// HoverDwell::armed is what enforces that; see `util/hoverdwell.hpp`.
  HoverDwell surfaceHoverDwell{};
  std::vector<SurfaceHoverRow> surfaceHoverRows;
  /// When true, viewport picks should use the snapped point (OSNAP) instead of the sticky-blended cursor.
  bool viewportSnapPickValid = false;
  /// **LOCAL** coordinates, not world — `world = local + worldDocumentOrigin`. These were named
  /// `...WorldX/Y/Z` until 2026-08-17 while holding local values, which is the setup for the bug class
  /// the `local-storage` invariant exists to catch (a world value stored without subtracting the
  /// origin lands the geometry a full origin away). `CadSnap::Hit` carries stored coordinates
  /// straight out of `userLinesFlat` and friends, so a snapped pick is bit-identical to the vertex it
  /// snapped to — which is also why nothing here needs widening to double.
  float viewportSnapPickLocalX = 0.f;
  float viewportSnapPickLocalY = 0.f;
  /// Elevation of the snapped point. An object snap yields the object's ACTUAL 3D point, so it
  /// overrides the current work-plane elevation — snapping to the end of a line on the datum while
  /// ELEV is 5 must give you that endpoint, not a point 5 above it (AutoCAD-faithful, REQ-058).
  /// Only meaningful while \ref viewportSnapPickValid.
  float viewportSnapPickLocalZ = 0.f;
  /// Command-line log cache for the selectable read-only multiline (rebuilt each frame from \ref log).
  std::vector<char> commandLogCacheBytes;
  size_t commandLogLastSizeForAutoscroll = 0;

  // --- Floating command bar / fading history / F2 console (REQ-040) ---
  bool cmdLineClassicDock = false;    ///< true → legacy docked panel; false → floating bar (default). Persisted.
  bool cmdBarVisible = true;          ///< floating bar shown; × hides, Ctrl+9 restores. Persisted.
  bool cmdBarAnchorValid = false;     ///< false → place at the default bottom-left this frame. Persisted.
  float cmdBarAnchorX = 0.f;          ///< persisted floating-bar bottom-LEFT x anchor (screen px); Y is pinned to the bottom.
  float cmdBarAnchorY = 0.f;          ///< (legacy/unused: the bar is always pinned to the viewport bottom).
  float cmdBarWidth = 0.f;            ///< user-resized bar width (px); 0 → default. Persisted.
  float cmdConsoleHeight = 0.f;       ///< user-resized F2 console height (px); 0 → default. Persisted.
  bool cmdConsoleOpen = false;        ///< F2 expanded console (not persisted).
  float cmdBarFadeDelaySec = 4.f;     ///< idle seconds before recent-history lines start fading. Persisted.
  float cmdBarOpacity = 0.92f;        ///< bar / console background opacity. Persisted.
  int cmdBarHistoryLines = 3;         ///< recent log lines floated above the bar. Persisted.
  double cmdLogLastChangeTime = 0.0;  ///< ImGui time of the last log append (drives the fade).
  size_t cmdLogLastSizeForFade = 0;   ///< log size seen at the last fade-timer reset.
  std::vector<std::string> cmdEnteredHistory;  ///< recently submitted command-line entries (newest last; capped). Runtime only.
  /// Last Drawing1 viewport metrics (match survey MTEXT box to on-screen font scaling).
  float viewportLastSurveyLayoutOrthoHalfH = 50.f;
  float viewportLastSurveyLayoutHeightPx = 600.f;
  int viewportLastFbW = 900;
  int viewportLastFbH = 650;
  /// Last ortho half-height / viewport height / MUP used for survey MTEXT auto-layout (re-run when zoom/size/MUP changes).
  float surveyLabelLayoutCacheHalfH = -1.f;
  float surveyLabelLayoutCacheVpHeightPx = -1.f;
  float surveyLabelLayoutCacheMup = -1.f;
  /// Viewport screen-size clamps for TEXT annotation rendering (from paper height × MUP).
  float viewportTextMinPx = 8.f;
  float viewportTextMaxPx = 160.f;
  /// Viewport clamps for MTEXT box content.
  float viewportMtextMinPx = 8.f;
  float viewportMtextMaxPx = 128.f;
  /// Viewport clamps for aligned dimension value text.
  float viewportDimTextMinPx = 8.f;
  float viewportDimTextMaxPx = 160.f;
  /// Dimension extension / dimension line stroke width in screen pixels.
  float viewportDimExtLinePx = 1.0f;
  float viewportDimDimLinePx = 1.25f;
  /// Scales arrow length derived from annotation height (1 = default).
  float viewportDimArrowScale = 1.f;
  /// Object snap master (F3, status bar OSNAP). Per-type toggles: right-click OSNAP or Settings → Object snap.
  /// One REQ-100 frame-budget run: the bench scene, the scripted orbit, and the timings.
  ///
  /// The scene is SWAPPED into the active drawing's polyline arrays and swapped back when the run
  /// ends, so a benchmark can never cost the user their drawing. That is also why the saved arrays
  /// live here rather than the bench building a document of its own — there is no "new drawing"
  /// entry point in the command layer to build one with.
  struct BenchRun {
    bool active = false;
    bool sceneInstalled = false;
    int frameIndex = 0;
    int framesTotal = 0;
    int warmupFrames = 0;   ///< Frames discarded before timing starts (shader/VBO upload, cache fill).
    int segmentCount = 0;
    double orbitDegPerFrame = 0.0;
    std::vector<double> frameMs;

    /// Points in the surface profile (REQ-100 as amended, ADR-028). 0 = the line-segment profile.
    int surfacePointCount = 0;
    int surfaceTriangleCount = 0;
    /// Contour levels the profile's style produces, and the segments they generate — REQ-100 profile
    /// (c) is defined as a **contoured** surface, so the record has to say at what interval or the
    /// number cannot be compared with the next run's.
    double surfaceMinorIntervalFt = 0.0;
    double surfaceMajorIntervalFt = 0.0;
    int surfaceContourSegs = 0;
    /// \ref AppCommandState::surfaceDisplayRegenCount at the first TIMED frame. The delta at the end
    /// is ADR-036 (e)'s acceptance: the cache must hold across the run, so this must not grow with
    /// the frame count.
    std::uint64_t regenAtStart = 0;
    bool regenBaselineTaken = false;
    std::uint64_t regenDuringRun = 0;

    /// Triangles in the shaded-mesh profile (REQ-100 (b)). 0 = not the mesh profile. At most one of
    /// this and \ref surfacePointCount is non-zero; both zero means the line-segment profile.
    int meshTriangleCount = 0;

    std::vector<float> savedPolyVerts;
    std::vector<int> savedPolyOffsets;
    std::vector<std::uint8_t> savedPolyClosed;
    std::vector<EntityAttributes> savedPolyAttrs;
    std::vector<CadSurface> savedSurfaces;            ///< restored verbatim after a surface run
    std::vector<EntityAttributes> savedSurfaceAttrs;
    std::vector<std::shared_ptr<const CadMesh>> savedMeshes;  ///< restored verbatim after a mesh run
    std::vector<EntityAttributes> savedMeshAttrs;
    /// The mesh profile forces Shaded (REQ-100 (b) measures *shaded* meshes, and REQ-064's budget
    /// condition is stated in Shaded). Saved so a bench run cannot leave the user in a style they
    /// did not choose — which would also silently invalidate ADR-026 (e)'s 2D Wireframe parity.
    VisualStyle savedVisualStyle = VisualStyle::Wireframe2D;
    float savedAzimuthDeg = 0.f;
    float savedElevationDeg = 90.f;
    float savedZoom = 1.f;
    double savedPanX = 0.0;
    double savedPanY = 0.0;
    double savedPanZ = 0.0;
  };
  BenchRun bench;

  /// Model viewport visual style (REQ-064). Default is the pre-REQ-064 renderer, exactly.
  VisualStyle viewportVisualStyle = VisualStyle::Wireframe2D;

  bool objectSnapEnabled = true;
  bool objectSnapEndpoint = true;
  bool objectSnapMidpoint = true;
  bool objectSnapCenter = true;
  bool objectSnapPerpendicular = true;
  bool objectSnapSurveyPoint = true;
  bool objectSnapGeometricCenter = true;
  /// Snap where two objects genuinely meet in 3D (REQ-062).
  bool objectSnapIntersection = true;
  /// Snap where two objects only *appear* to meet in the current view (REQ-062). Off by default,
  /// as in AutoCAD: it fires on objects that do not touch, which is surprising unless asked for.
  bool objectSnapApparentIntersection = false;
  /// Screen-space aperture (pixels) for object snap tolerance and related viewport picks.
  float objectSnapAperturePx = 14.f;
  /// Half-size in screen pixels for green object-snap glyphs (square / triangle / circle overlay).
  float objectSnapGlyphHalfPx = 15.f;
  /// Half-size in screen pixels for grip squares drawn on selected entities.
  float gripSizePx = 4.f;
  /// Shift+RMB snap menu: next viewport pick uses this world point (then cleared on submit / cancel).
  bool pendingOneShotSnapValid = false;
  float pendingOneShotSnapX = 0.f;
  float pendingOneShotSnapY = 0.f;
  /// \c static_cast<\ref CadSnap::Kind>.
  int pendingOneShotSnapKind = 0;

  /// World coordinate of local (0,0). Geometry is stored in local space for float precision.
  double worldDocumentOriginX = 0.0;
  double worldDocumentOriginY = 0.0;

  /// Applied on the next viewport zoom processing step (needs framebuffer size).
  bool pendingZoomExtents = false;
  bool pendingZoomWindow = false;
  float pendingZoomMnX = 0.f;
  float pendingZoomMxX = 0.f;
  float pendingZoomMnY = 0.f;
  float pendingZoomMxY = 0.f;

  enum class LinePhase { NeedFirstPoint, NeedNextPoint } linePhase = LinePhase::NeedFirstPoint;
  /// Segments committed in the current LINE chain; the point being specified is
  /// (lineDraftSegments + 2) once past the first point (REQ-024 ordinal prompt).
  uint32_t lineDraftSegments = 0;

  enum class PolylinePhase { NeedFirstPoint, NeedNextPoint } polylinePhase = PolylinePhase::NeedFirstPoint;

  float polyFirstX = 0.f;
  float polyFirstY = 0.f;
  uint32_t polyDraftSegments = 0;

  enum class ArcPhase { WaitStart, WaitMid, WaitEnd } arcPhase = ArcPhase::WaitStart;

  float arcAx = 0.f, arcAy = 0.f;
  float arcBx = 0.f, arcBy = 0.f;

  enum class EllipsePhase { WaitCenter, WaitMajorEnd, WaitRatio } ellPhase = EllipsePhase::WaitCenter;

  float ellCx = 0.f, ellCy = 0.f;
  float ellMajEx = 0.f, ellMajEy = 0.f;

  /// RECT (REQ-053): first corner, then the opposite corner. The second point also accepts `@dx,dy`, which
  /// is how a rectangle of an exact width and height is drawn.
  enum class RectPhase { WaitFirstCorner, WaitSecondCorner } rectPhase = RectPhase::WaitFirstCorner;

  float rectX1 = 0.f, rectY1 = 0.f;

  enum class TextCmdPhase { WaitInsertion, WaitHeight, WaitRotation, WaitString } textPhase = TextCmdPhase::WaitInsertion;

  float textInsX = 0.f, textInsY = 0.f;
  float textHeightDraft = 1.f;
  float textRotDraft = 0.f;

  enum class MtextPhase { WaitCorner1, WaitCorner2, WaitString } mtextPhase = MtextPhase::WaitCorner1;

  float mtxtX1 = 0.f, mtxtY1 = 0.f;
  float mtxtX2 = 0.f, mtxtY2 = 0.f;
  /// Multiline MTEXT editor over the box (new placement or double-click edit). Not command-line text.
  bool mtextRichEditorOpen = false;
  bool mtextRichEditorPlacement = false;
  /// Editor target (REQ-039 phase 2): when \c mtextRichEditorPaper, \c mtextRichEditorAnnIndex indexes
  /// \c paperLayouts[mtextRichEditorPaperLayout].paperTexts; otherwise it indexes \c cadAnnotations.
  bool mtextRichEditorPaper = false;
  int mtextRichEditorPaperLayout = -1;
  /// Single-line \c Kind::Text edit (no MTEXT rich tags): plain content box.
  bool mtextRichEditorPlain = false;
  int mtextRichEditorAnnIndex = -1;
  std::string mtextRichEditorBuf;
  bool mtextRichEditorFocusRequest = false;
  int mtextRichEditorCursor = 0;
  int mtextRichEditorSelStart = 0;
  int mtextRichEditorSelEnd = 0;
  bool mtextRichEditorTypingAllCaps = false;

  // --- WYSIWYG editor state (ADR-023) ---
  // The caret and selection anchor are **visible character indices** (tags are not characters); the widget
  // publishes the matching raw byte offsets into mtextRichEditorSelStart/End above, so the formatting
  // toolbar keeps working on byte ranges exactly as it did. Plain members, no new type: the ui/ widget
  // must not become a dependency of the commands layer.
  int mtextEditCaret = 0;
  int mtextEditAnchor = 0;            ///< caret == anchor means no selection
  /// The editor owns keyboard focus. Deliberately NOT ImGui's ActiveID: holding that persistently makes
  /// ImGui refuse to hover any other item, which dead-locks every other control in the application. The
  /// widget only takes ActiveID for the duration of a drag-select, exactly as a stock widget does.
  bool mtextEditFocused = false;
  bool mtextEditMouseSelecting = false;
  float mtextEditScrollY = 0.f;       ///< px scrolled when the text outgrows the box's height cap
  double mtextEditBlinkT = 0.0;       ///< ImGui time the caret last moved (restarts the blink)
  std::vector<std::string> mtextEditUndo;  ///< in-editor Ctrl+Z snapshots (drawing undo is separate)
  std::vector<std::string> mtextEditRedo;
  float mtextEditLastHeight = 0.f;    ///< height the wrapped layout used last frame (positions the box)
  /// Fix a word typed with Caps Lock inverted ("hELLO" → "Hello") when it is finished. Persisted.
  bool mtextEditAutocorrectCapsLock = false;
  // Find and Replace (the Options menu). Buffers are small fixed arrays to match the other UI text fields.
  bool mtextFindReplaceOpen = false;
  char mtextFindBuf[128] = {0};
  char mtextReplaceBuf[128] = {0};
  bool mtextFindMatchCase = false;
  std::string mtextFindStatus;        ///< "3 replaced" / "not found", shown under the fields

  // --- MTEXT "Text Formatting" panel chrome (REQ-051) ---
  // Unlike the fields above, this is not per-edit state: it survives closing the editor and is persisted
  // (UserPrefs, the \c cmdBar* pattern of REQ-040), so the panel reopens where the user left it.
  // \ref CloseMtextRichEditorUi deliberately does not touch it.
  bool mtextPanelAnchorValid = false;    ///< false → place near the MTEXT box this frame. Persisted.
  float mtextPanelAnchorX = 0.f;         ///< panel top-LEFT anchor in screen px. Persisted.
  float mtextPanelAnchorY = 0.f;
  bool mtextPanelRulerVisible = true;    ///< column ruler shown above the in-place box. Persisted.
  bool mtextPanelRow2Visible = true;     ///< second toolbar row shown (the expand control). Persisted.
  unsigned int mtextPanelRunColor = 0xFFFFFFu;  ///< last colour picked for the per-selection swatch (RGB).
  /// Panel size measured last frame (the panel auto-sizes to its content). Used to clamp the anchor and to
  /// span the caption before this frame's size is known — session-only, deliberately not persisted, since a
  /// stale size from a different font/DPI would misplace the panel for one frame.
  float mtextPanelMeasuredW = 0.f;
  float mtextPanelMeasuredH = 0.f;
  /// True while the ruler's width marker is being dragged (so the undo snapshot is pushed exactly once).
  bool mtextRulerDragActive = false;

  enum class DimPhase { WaitExt1, WaitExt2, WaitDimLinePt } dimPhase = DimPhase::WaitExt1;
  /// \c Kind::DimAngular — vertex then two ray points then arc radius pick.
  enum class DimAngularPhase { WaitVertex, WaitRay1, WaitRay2, WaitArc } dimAngularPhase = DimAngularPhase::WaitVertex;
  float dimAngVx = 0.f;
  float dimAngVy = 0.f;
  float dimE1x = 0.f, dimE1y = 0.f;
  float dimE2x = 0.f, dimE2y = 0.f;
  /// \c Kind::DimLinear, \p dimPhase == WaitDimLinePt — preview orientation (horizontal vs vertical span).
  bool dimLinearDraftVertical = false;
  /// When true, \p dimLinearDraftVertical is fixed until the crosshair moves from \p dimLinearLockCursorW*.
  bool dimLinearOrientUserLock = false;
  float dimLinearLockCursorWx = 0.f;
  float dimLinearLockCursorWy = 0.f;

  float anchorX = 0.f;
  float anchorY = 0.f;
  /// Elevation the anchor was committed at (REQ-058). Recorded per-vertex rather than taken from
  /// the work plane at commit time, because a line's two ends can legitimately differ: the anchor
  /// may have snapped to something on the datum while the far end snaps to something elevated.
  float anchorZ = 0.f;
  /// From UI — ortho constrains LINE segment picks / typed ortho distances toward cursor.
  bool orthoMode = false;
  /// Last drawing viewport cursor (world), updated each frame for LINE ortho distance entry.
  float uiCursorWorldX = 0.f;
  float uiCursorWorldY = 0.f;
  /// Drawing viewport pan/zoom (local coordinates; pan is view center in storage space).
  double viewportPanX = 0.;
  double viewportPanY = 0.;
  /// Camera target elevation (REQ-058). Pan is a 3D point once the view can tilt: dragging up in
  /// an orbited view moves the target along the camera's UP axis, which has a Z component, so a
  /// target constrained to Z = 0 cannot follow the cursor and the pan stops feeling 1:1.
  /// Always 0 in plan view, where the up axis lies in the XY plane.
  double viewportPanZ = 0.;
  float viewportZoom = 1.f;
  /// Camera orientation about the pan point (REQ-058 / ADR-025 (c)). Pan and zoom remain the
  /// single source of truth for WHERE the camera looks and HOW FAR — these two add only the
  /// direction, so a \ref Camera built by \ref CadViewCamera cannot drift from the view.
  /// Defaults are plan view, which reproduces the pre-3D pipeline exactly.
  float viewportAzimuthDeg = 0.f;
  float viewportElevationDeg = 90.f;

  /// ViewCube orientation animation (REQ-059). A face/arrow/home press sets a target and the view
  /// eases to it over \ref kViewAnimSeconds instead of snapping, so the user keeps their bearings —
  /// a hard jump makes it easy to lose track of which way the model turned. Orbiting by hand
  /// cancels any animation in flight so the drag is never fighting an interpolation.
  bool  viewAnimActive = false;
  float viewAnimFromAz = 0.f, viewAnimFromEl = 90.f;
  float viewAnimToAz = 0.f, viewAnimToEl = 90.f;
  float viewAnimT = 0.f;  ///< 0..1 progress.

  /// Active work plane / UCS (REQ-058 / ADR-025 (e)). A click resolves as ray × this plane, so it
  /// is where new geometry lands. The default — origin at Z = 0 with a +Z normal — is the world XY
  /// plane, under which every pre-3D drawing behaviour is unchanged.
  double ucsOriginX = 0.0, ucsOriginY = 0.0, ucsOriginZ = 0.0;
  double ucsNormalX = 0.0, ucsNormalY = 0.0, ucsNormalZ = 1.0;
  /// Rotation of the active coordinate system about its normal, in degrees. The ViewCube's compass
  /// letters and its square-up arrows are relative to this, so under a rotated UCS "square with
  /// north" means the UCS's north (REQ-059). 0 = the WCS, which is the only value anything sets
  /// today — the UCS command that would change it is still outstanding.
  float ucsAzimuthDeg = 0.f;
  /// Elevation of the cursor's work-plane intersection, published alongside the existing
  /// \c uiCursorWorldX/Y so readouts and future 3D-aware commands can see it.
  float uiCursorWorldZ = 0.f;
  /// Model viewport size in pixels, published by the UI each frame. The command layer needs it to
  /// project geometry to screen for box-selection under an orbited camera (REQ-058); it has no
  /// other way to know the viewport's aspect. Zero means "not yet known" — callers fall back to
  /// the plan-space test, which is correct whenever the view is unrotated anyway.
  float uiViewportWidthPx = 0.f;
  float uiViewportHeightPx = 0.f;

  /// LINE/POLYLINE — pick bearing from two reference clicks (\p AP), optional +/- adjustment, then lock.
  enum class SegmentAnglePickPhase : uint8_t { Idle, WaitP1, WaitP2, WaitAdjustOrCommit };
  SegmentAnglePickPhase segmentAnglePickPhase = SegmentAnglePickPhase::Idle;
  float segmentPickRefX1 = 0.f;
  float segmentPickRefY1 = 0.f;
  /// Draft bearing ° clockwise from north (after second pick; editable with +/- before lock).
  float segmentPickDraftBearingDeg = 0.f;

  /// LINE/POLYLINE next point: lock segment to a bearing (° clockwise from north); distance-only or click on ray.
  bool segmentAngleLockActive = false;
  float segmentLockUx = 1.f;
  float segmentLockUy = 0.f;
  /// LINE/POLYLINE: user typed \c A / \c angle alone — next line is parsed as bearing ° CW from north (blank Enter
  /// cancels).
  bool segmentAngleKeyboardAwaitBearing = false;

  /// Next stable entity id to hand out for this drawing (REQ-076 / ADR-027). Monotonic, persisted
  /// in `.gs`, and **never rewound** — in particular it is deliberately NOT part of
  /// \ref DrawingGeometrySnapshot, so draw → undo → draw gives the second entity a *new* id rather
  /// than reusing the undone one's. Reuse is the one thing an identity must not do.
  std::uint64_t nextEntityId = 1;
  /// \ref cadGpuRevision at the last \ref EnsureEntityIds sweep. The sweep early-outs when geometry
  /// has not changed since, which is what makes it safe to call unconditionally every frame — the
  /// common case is one integer compare, not a walk of every entity (architecture §11.7).
  ///
  /// Deliberately **64-bit against a 32-bit revision**, so \ref kEntityIdSweepNever can be a value
  /// the revision cannot reach. A 32-bit sentinel would collide once every 2^32 edits and skip the
  /// sweep for that drawing — rare enough to never be reproduced, and silent when it happened.
  std::uint64_t entityIdSweepRevision = kEntityIdSweepNever;

  /// Line vertices for GL: pairs (x,y,z) per endpoint; each segment is two endpoints.
  std::vector<float> userLinesFlat;
  std::vector<EntityAttributes> userLineAttrs;

  // --- Circle ---
  enum class CircleStyle { CenterRadius, ThreePoint } circleStyle = CircleStyle::CenterRadius;

  enum class CirclePhase {
    WaitCenterOrMode, ///< Pick center, or type 3P for three-point circle
    WaitRadius,       ///< Center set: radius click, number, or D + diameter
    ThreeP_WaitP1,
    ThreeP_WaitP2,
    ThreeP_WaitP3,
  } circlePhase = CirclePhase::WaitCenterOrMode;

  float circleCx = 0.f;
  float circleCy = 0.f;

  float c3p1x = 0.f, c3p1y = 0.f;
  float c3p2x = 0.f, c3p2y = 0.f;

  /// Each circle: center X, center Y, center Z, radius (world units) — stride 4 (REQ-057 /
  /// ADR-025 (a)). The centre's XYZ is contiguous so it reads like a point; the radius trails it.
  /// Z is absolute (ADR-025 D2) and the circle's plane stays parallel to XY, matching CadArc::z.
  std::vector<float> userCirclesCxCyZR;
  std::vector<EntityAttributes> userCircleAttrs;
  std::vector<CadArc> userArcs;
  std::vector<EntityAttributes> userArcAttrs;
  std::vector<CadEllipse> userEllipses;
  std::vector<EntityAttributes> userEllAttrs;
  /// Each polyline: vertex indices [\ref userPolylineOffsets[i], \ref userPolylineOffsets[i+1]); XYZ triplets in
  /// \ref userPolylineVerts.
  std::vector<int> userPolylineOffsets;
  std::vector<float> userPolylineVerts;
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
  std::vector<float> featureLineVerts;
  std::vector<uint8_t> featureLineClosed;
  std::vector<uint8_t> featureLineElevPt;
  std::vector<CadFeatureLineInfo> featureLineInfo;
  std::vector<EntityAttributes> featureLineAttrs;

  /// FEATURELINE command draft — XYZ vertices, and the elevation-point flag for each.
  std::vector<float> featureLineDraftVerts;
  std::vector<uint8_t> featureLineDraftElevPt;
  /// The name typed when FEATURELINE started, stamped on the line at commit.
  std::string featureLineDraftName;

  /// FEATURELINE two-phase vertex entry (TASK-082). A click supplies X and Y only, so the point is
  /// held here while the command prompts for its elevation — the difference between this and
  /// POLYLINE/3DPOLY, which take Z from the work plane without asking.
  ///
  /// A point that already carries an elevation (typed `X,Y,Z`) never lands here; it commits
  /// straight away, because prompting would be asking a question already answered.
  bool featureLinePendingPoint = false;
  float featureLinePendingX = 0.f;
  float featureLinePendingY = 0.f;
  /// What a bare Enter accepts: the snapped point's Z if an object snap was active, else the
  /// previous vertex's elevation, else the work plane. See TASK-082 ASSUMPTION-1.
  float featureLinePendingDefaultZ = 0.f;
  /// Armed by a bare `E` at the FEATURELINE prompt, consumed by the next point. Before clicking
  /// worked, `E` had to be followed by coordinates on the same line; with a click supplying them,
  /// the flag has to survive until the click arrives.
  bool featureLineNextIsElevPoint = false;

  /// POLYLINE command draft — XYZ vertices (two or more before commit).
  std::vector<float> polylineDraftVerts;
  /// TRIM has two modes, chosen by the \c TRIMSTATE system variable (REQ-056):
  ///   0 (default) — smart trim: two clicks draw a line across the pieces to remove, no edges to pick;
  ///   1           — classic: pick cutting edges, Enter, then click the pieces to trim.
  /// Within a run, \p T switches to picking cutting edges and \p L back to the drawn line, so either mode
  /// is reachable whatever TRIMSTATE says.
  enum class TrimPhase {
    SelectCuttingEdges,
    CuttingLine_WaitP1,
    CuttingLine_WaitP2,
    SelectTrimTargets,
  } trimPhase = TrimPhase::SelectCuttingEdges;
  /// \c TRIMSTATE: 0 = smart line trim (default), 1 = pick cutting edges first. Persisted in user prefs.
  int trimState = 0;
  /// REQ-077: update-check settings (enabled, channel, skipped version, throttle anchor).
  /// Only the persisted settings live here — the in-flight worker state is `update::UpdateState`,
  /// owned by the application loop, so `AppCommandState` gains no thread and stays copyable.
  update::UpdatePrefs updatePrefs;
  /// REQ-091: sign-in display state and UI requests. Only flags/strings live here — the in-flight
  /// worker (`auth::AuthTask`, holding a thread) is owned by the application loop, same split as
  /// `updatePrefs` above, so `AppCommandState` gains no thread and stays copyable.
  bool        authSignedIn   = false;
  bool        authBusy       = false;   ///< an interactive sign-in or silent refresh is in flight
  bool        authInteractiveBusy = false;  ///< true only while the browser-based flow is running,
                                            ///< for the "Waiting for browser..." button label
  std::string authEmail;                ///< display only; the accounts-worker is the trust boundary
  std::string authError;                ///< set only after a user-initiated sign-in attempt fails
  bool        authSignInRequested  = false;  ///< set by the Settings panel's Sign In button
  bool        authSignOutRequested = false;  ///< set by the Settings panel's Sign Out button
  /// REQ-091 (amended): the launch-time sign-in gate (DrawSignInGate) blocks the session every
  /// launch until this is true. Set true on a successful sign-in (interactive or silent) OR when
  /// there is no internet connectivity at all (same offline exception REQ-077's update gate
  /// uses) — never re-checked mid-session, so signing out later does not reopen the gate.
  bool        authGateResolved = false;
  std::vector<SelectedEntity> trimCutters;
  /// Draft endpoints while TRIM \p L waits for second point (rubber band). First shot completes trim and clears TRIM.
  float trimCutInfP1x = 0.f, trimCutInfP1y = 0.f, trimCutInfP2x = 0.f, trimCutInfP2y = 0.f;
  /// OFFSET: pick entity, then type distance + pick side, or click a through point (line / circle / arc).
  enum class OffsetPhase {
    WaitSelectEntity,
    WaitDistanceOrThrough,
    WaitSidePick,
  } offsetPhase = OffsetPhase::WaitSelectEntity;
  bool offsetEntityValid = false;
  SelectedEntity offsetEntity{};
  /// Typed offset distance (always positive); combined with side pick for sign.
  float offsetTypedDistance = 0.f;
  /// While OFFSET waits for the first pick, entity under cursor (for highlight).
  bool offsetHoverHighlightValid = false;
  SelectedEntity offsetHoverEntity{};
  /// Bumped when CAD geometry or per-entity viewport styling changes; GPU vertex caches invalidate when stale.
  uint32_t cadGpuRevision = 0;

  std::vector<CadAnnotation> cadAnnotations;
  std::vector<EntityAttributes> cadAnnotationAttrs;
  /// Solid-filled regions (ADR-011), imported from SOLID-fill HATCH; rendered filled in the overlay.
  std::vector<CadFilledRegion> cadFilledRegions;
  std::vector<EntityAttributes> cadFilledRegionAttrs;

  /// Imported triangle meshes (REQ-063 / ADR-026). Reference geometry: nothing in the command layer
  /// creates or edits one — REQ-065's glTF importer produces them and ERASE removes them.
  ///
  /// `shared_ptr<const>` so undo snapshots share the payload rather than copying it (architecture
  /// §11.5 as amended). "Editing" a mesh means replacing the pointer; never write through it.
  std::vector<std::shared_ptr<const CadMesh>> cadMeshes;
  std::vector<EntityAttributes> cadMeshAttrs;

  /// TIN surfaces (REQ-068). The heavy triangulation hangs off a shared_ptr inside each CadSurface,
  /// so copying this vector — which every undo snapshot does — is strings and refcount bumps.
  std::vector<CadSurface> cadSurfaces;
  std::vector<EntityAttributes> cadSurfaceAttrs;

  /// One in-flight background rebuild per surface currently being retriangulated (REQ-069's dynamic
  /// rebuild — architecture §8's one-shot worker pattern, its second concrete use after
  /// \ref pdfAttachAsync and the first to implement the full contract: rules 4 and 5, generation
  /// staleness and cooperative cancellation, which pdfAttachAsync's own struct does not itself carry).
  /// Heap-allocated so the atomic/thread members don't affect this struct's own copyability — the
  /// same reason \ref AsyncBuild is. Never part of \ref DrawingGeometrySnapshot or \ref
  /// DrawingDocument: a background thread is live-only state, not drawing content.
  struct SurfaceRebuildAsync {
    /// Which CadSurface this is for, by **stable entity id** (REQ-076 / ADR-036 (a)) — never by name
    /// and never by index.
    ///
    /// This was `std::string surfaceName` until surfaces gained an `EntityKind`, and the name key had
    /// two failure modes that the id does not. A **rename** while a rebuild is in flight orphaned the
    /// result: the reap looked the old name up, found nothing, discarded a completed triangulation,
    /// and — because the in-flight check also matched on name — immediately dispatched a second
    /// worker for the same surface. Worse, **erase-then-recreate under the same name** made the reap
    /// find the *new* surface and apply the *old* one's triangulation to it. An id is not reused
    /// within a drawing (REQ-076), so neither is reachable.
    std::uint64_t      surfaceId = 0;
    std::thread        thread;
    std::atomic<bool>  done{false};
    std::atomic<bool>  cancel{false}; ///< cooperative; checked before the worker starts real work
    std::uint32_t      generation = 0;  ///< cadGpuRevision at dispatch — a mismatch on completion means discard
    double             originX = 0.0, originY = 0.0;
    TinBuildResult      result;

    /// Joins the worker if it is still running. Without this, destroying a job whose thread is
    /// still joinable calls std::terminate — and the ONLY other join site (\ref TickSurfaceRebuilds)
    /// is reachable only once `done` is already set, so every path that drops a job mid-flight hit
    /// that: closing the application with a rebuild in flight aborted the process instead of
    /// exiting, and since cadGpuRevision moves on every drawing mutation, "edit then close" was
    /// enough to trigger it. Joining after the reap path's own join is a no-op (not joinable).
    ~SurfaceRebuildAsync() {
      cancel.store(true, std::memory_order_release);  // lets a not-yet-started worker return at once
      if (thread.joinable())
        thread.join();
    }
  };
  std::vector<std::unique_ptr<SurfaceRebuildAsync>> surfaceRebuildAsync;

  /// Generated display geometry for one surface — ADR-036 (e).
  ///
  /// **Not a member of \ref CadSurface, deliberately.** `cadSurfaces` is assigned wholesale into
  /// \ref DrawingDocument and into every \ref DrawingGeometrySnapshot, so a field on the surface
  /// would be deep-copied into all 50 undo frames — the exact cost this cache exists to avoid,
  /// arriving through the one door nobody thinks to check. A parallel container keyed by stable id
  /// cannot be dragged along by those assignments.
  ///
  /// **Keyed by stable entity id, not by array index** (§11 invariant 9): `cadSurfaces` compacts on
  /// erase, so an index key would start drawing one surface's border over another's triangulation
  /// after a delete.
  struct SurfaceDisplayCacheEntry {
    std::uint64_t surfaceId = 0;

    /// The triangulation this geometry was generated from — the staleness key. A rebuild REPLACES
    /// the pointer wholesale (ADR-028 (a)), so pointer identity IS triangulation identity.
    ///
    /// `weak_ptr`, not a raw pointer, for the reason the mesh GPU cache already documents: a raw
    /// pointer key can be matched by a NEW allocation at the freed address, which would silently
    /// serve one surface's geometry for another's. An expired weak_ptr simply misses and regenerates.
    /// Not a `shared_ptr` either — that would keep a superseded 7 MB triangulation alive for as long
    /// as the cache entry lived.
    std::weak_ptr<const CadTin> builtFrom;

    /// The style this geometry was generated from — the second half of the staleness key.
    ///
    /// The **resolved style by value**, not a revision counter. ADR-036 (e) names the key
    /// "(tin pointer, style revision)"; a counter has to be bumped by every route that can change the
    /// table, and undo restore, `.gs` load, tab switch and DXF import are four of them, so the one
    /// that gets forgotten leaves stale contours on screen with nothing to point at. Comparing the
    /// resolved value cannot be forgotten. A style is five small structs and two doubles, so the
    /// comparison is cheaper than the allocation it prevents.
    SurfaceStyle style;

    /// All generated geometry, each in `GL_LINES` layout: flat x,y,z, six floats per segment.
    /// Regenerated together, only on a staleness miss, and borrowed by
    /// \ref CadSurfaceDisplayGeometry rather than copied into it.
    std::vector<float> triangleEdges;  ///< Every triangulation edge (the pre-REQ-070 appearance).
    std::vector<float> borderEdges;    ///< The outline: edges belonging to one triangle (\c TinBorderEdges).
    std::vector<float> minorContours;  ///< Minor levels, excluding those that are also major.
    std::vector<float> majorContours;

    /// The style asked for more contour levels than the display path will generate, so the contour
    /// buffers above are empty for a reason that has nothing to do with the style's toggles.
    ///
    /// Carried rather than left implicit because REQ-201 forbids absorbing a refusal: an interval of
    /// 0.0001 ft over a 1,000 ft surface is a million contours, and a surface that simply stopped
    /// showing any would look like a defect. The Surface Manager reads this and says so.
    bool contoursSuppressed = false;
    /// How many levels were asked for, for that message. 0 when nothing was suppressed.
    int suppressedLevelCount = 0;
  };

  /// The display-geometry cache. Live-only: never in \ref DrawingGeometrySnapshot, never in
  /// \ref DrawingDocument, never in `.gs` — which is what makes REQ-070's "never stored in `.gs`"
  /// and "adds no entity to the drawing" structurally true rather than a rule someone must remember.
  std::vector<SurfaceDisplayCacheEntry> surfaceDisplayCache;

  /// What the renderer is handed for surfaces this frame — batches borrowing the buffers above,
  /// filtered for layer/isolation visibility and carrying each component's resolved colour and
  /// lineweight. Rebuilt in the same pass as the cache, so a batch never outlives its buffer.
  ///
  /// Live-only for the same reason the cache is: nothing here is ever snapshotted or written.
  CadSurfaceDisplayGeometry surfaceDisplayGeometry;

  /// How many times \ref RefreshSurfaceDisplayGeometry has actually REGENERATED a surface's geometry
  /// since the process started, as opposed to taking the early-out.
  ///
  /// Exists for one reason: ADR-036 (e)'s REQ-100 obligation is to prove **the cache holds across
  /// frames**, not that a single generation is fast. A profile that timed one regeneration would pass
  /// while the defect it exists to catch — regenerating 200,000 triangles' worth of contours every
  /// frame — was fully present. BENCH reports the delta over the run: one per surface means the cache
  /// held; one per frame means it did not.
  ///
  /// Live-only and never reset by a document change, so it counts work rather than describing state.
  std::uint64_t surfaceDisplayRegenCount = 0;

  // --- HATCH command (REQ-043) ---
  /// Boundary loop traced under the cursor while HATCH is active (flat local x,y); valid drives the preview.
  std::vector<float> hatchPreviewLoop;
  bool hatchPreviewValid = false;
  /// Live appearance from the HATCH ribbon (ADR-019). color/transparency/layer apply now; angle/scale are
  /// stored for the pattern render path (Phase 3). Empty pattern = SOLID.
  float hatchColorRgb[3] = {0.78f, 0.78f, 0.78f};
  float hatchTransparency01 = 0.f;
  std::string hatchLayer;            ///< empty → current layer (MakeNewEntityAttrs default)
  float hatchAngleDeg = 0.f;
  float hatchScale = 1.f;
  std::string hatchPatternName;      ///< "" or "SOLID" = solid fill; e.g. "ANSI31" for a line pattern

  // --- Selection (idle box pick + move/copy/rotate) ---
  std::vector<SelectedEntity> selection;

  /// Objects hidden by ISOLATEOBJECTS / HIDEOBJECTS, as **stable entity ids** (REQ-084 (d),
  /// ADR-034). Kept SORTED so the per-entity test is a `binary_search`; empty is the overwhelming
  /// case and every gate early-outs on it, so nothing is paid for a drawing with no isolation.
  ///
  /// Ids, not indices: erase compacts the entity arrays (architecture §11.9), so an index would
  /// come to name a different object. **Session state** — deliberately not written to `.gs`, so a
  /// drawing always opens showing everything.
  std::vector<std::uint64_t> hiddenEntityIds;

  /// Two-click axis-aligned box (world XY): first corner placed, waiting second.
  bool selBoxWaitingSecond = false;
  float selBoxAnchorX = 0.f;
  float selBoxAnchorY = 0.f;
  /// Viewport-image XY (Drawing1 content coords) at fence first corner — compares with second-click mx for
  /// window vs crossing mode.
  float selBoxAnchorScreenX = 0.f;
  float selBoxAnchorScreenY = 0.f;

  /// MTEXT box corner grips (viewport): two-click edit — fixed diagonal corner while resizing box.
  int mtextGripAnnotationIndex = -1;
  /// 0–3 = MTEXT box corners; 4 = survey-linked label center (move whole box).
  int mtextGripCorner = -1;
  float mtextGripFixedCornerX = 0.f;
  float mtextGripFixedCornerY = 0.f;
  /// True after first click on an MTEXT box grip until second click commits (or RMB / ESC cancels).
  bool mtextGripMoveActive = false;
  float mtextGripOrigBoxMinX = 0.f, mtextGripOrigBoxMaxX = 0.f, mtextGripOrigBoxMinY = 0.f,
      mtextGripOrigBoxMaxY = 0.f;
  /// World pick at MTEXT grip mousedown (whole-label drag uses delta from here).
  float mtextGripDownWorldX = 0.f;
  float mtextGripDownWorldY = 0.f;

  /// Aligned dimension grip drag (viewport): which grip on \ref dimGripAnnotationIndex.
  int dimGripAnnotationIndex = -1;
  int dimGripWhich = -1; ///< 0 ext1, 1 ext2, 2 dim foot 1, 3 dim foot 2, 4 text
  float dimGripDownWorldX = 0.f;
  float dimGripDownWorldY = 0.f;
  float dimGripOrigSignedOffset = 0.f;
  float dimGripOrigExt1X = 0.f, dimGripOrigExt1Y = 0.f, dimGripOrigExt2X = 0.f, dimGripOrigExt2Y = 0.f;
  float dimGripOrigInsX = 0.f, dimGripOrigInsY = 0.f;
  float dimGripDragNx = 0.f, dimGripDragNy = 0.f;
  /// True after first click on a dim grip until second click commits (or RMB cancels).
  bool dimGripMoveActive = false;
  /// Text position vs dimension mid in local (N,T) frame at grip pick — reapplied after ext / dim-line edits.
  float dimGripTextAlongN = 0.f;
  float dimGripTextAlongT = 0.f;

  // --- CAD ENTITY GRIPS (viewport direct edit) ---
  // When an entity is selected (single selection), its grip points become draggable in the viewport.
  // RMB cancels and restores originals.
  bool entityGripMoveActive = false;
  SelectedEntity::Type entityGripType = SelectedEntity::Type::LineSeg;
  int entityGripEntityIndex = -1;
  int entityGripWhich = -1; ///< meaning depends on type:
                             // line(0=start/1=end),
                             // circle(0=center/1=radius),
                             // polyline(vertex local idx),
                             // arc(0=center/1=start/2=end),
                             // ellipse(0=center/1=major/2=minor)

  // Originals for RMB cancel.
  float entityGripOrigX0 = 0.f, entityGripOrigY0 = 0.f, entityGripOrigX1 = 0.f, entityGripOrigY1 = 0.f; // line
  float entityGripOrigCx = 0.f, entityGripOrigCy = 0.f, entityGripOrigR = 0.f; // circle/arc
  float entityGripOrigStartRad = 0.f, entityGripOrigSweepRad = 0.f; // arc

  // Polyline: moved vertex's global index into userPolylineVerts (x coordinate).
  int entityGripOrigPolylineXIdx = -1;
  float entityGripOrigPolyVertX = 0.f, entityGripOrigPolyVertY = 0.f;

  // Ellipse originals.
  float entityGripOrigEllMajVx = 0.f, entityGripOrigEllMajVy = 0.f;
  float entityGripOrigEllRatio = 0.f;
  float entityGripOrigEllCx = 0.f, entityGripOrigEllCy = 0.f;
  float entityGripDownWorldX = 0.f; // reserved
  float entityGripDownWorldY = 0.f; // reserved

  /// Base of the active grip stretch, in local storage coordinates: the grip's position when it was armed.
  /// ORTHO snaps the dragged point onto the H/V line through it, and a typed distance runs along that axis
  /// (REQ-047). Set when the grip is armed; only meaningful while \c entityGripMoveActive.
  float entityGripAnchorX = 0.f;
  float entityGripAnchorY = 0.f;
  /// A distance typed on the command line while a grip is armed pins the grip that far along the ORTHO axis
  /// the crosshair indicates, so the cursor stops driving it until the drag is committed or canceled.
  bool  entityGripTypedDistanceValid = false;
  float entityGripTypedX = 0.f;
  float entityGripTypedY = 0.f;
  /// Distance from \c entityGripAnchor to the (ORTHO-constrained, snap-aware) point the grip is currently
  /// being dragged to. Written by the viewport drag each frame so the cursor's dynamic-input box can show
  /// the live stretch distance (REQ-024).
  float entityGripLiveDistance = 0.f;

  // --- MOVE / COPY ---
  enum class ModifyPhase { PickSelection, NeedBase, NeedDestination } modifyPhase = ModifyPhase::PickSelection;

  float modifyBaseX = 0.f;
  float modifyBaseY = 0.f;
  /// \c Kind::Scale — after base pick: reference length (world) so scale = new length / this value (or distance /
  /// base-to-cursor over this value in \c ScalePhase::FactorPick).
  float scaleRefDist = 1.f;
  /// \c Kind::Scale — sub-state while \c modifyPhase == NeedDestination (after base).
  enum class ScalePhase {
    FactorPick, ///< Default: \c scaleRefDist from selection vs base; scale = dist(base,cursor)/ref or typed factor.
    Ref_WaitP1, ///< First point of explicit reference length segment.
    Ref_WaitP2, ///< Second point sets \c scaleRefDist.
    NewLength_WaitTypedOrP1, ///< Type new length, or pick first point of new-length segment.
    NewLength_WaitP2, ///< Second pick completes scale = dist(new seg)/\c scaleRefDist.
  } scalePhase = ScalePhase::FactorPick;
  float scaleRefP1X = 0.f, scaleRefP1Y = 0.f;
  float scaleNewLenP1X = 0.f, scaleNewLenP1Y = 0.f;

  // --- ROTATE ---
  enum class RotatePhase {
    PickSelection,
    NeedBase,
    NeedAngleOrReference, ///< decimal/DMS **clockwise from north**, or R reference
    Ref_WaitP1,
    Ref_WaitP2,
    AfterReference_WaitAngleOrP, ///< numeric/DMS or "p" for new angle via two-point line (bearing)
    AnglePoints_WaitP1,
    AnglePoints_WaitP2,
  } rotatePhase = RotatePhase::PickSelection;

  float rotateBaseX = 0.f;
  float rotateBaseY = 0.f;
  float rotateRefX1 = 0.f, rotateRefY1 = 0.f;
  float rotateRefX2 = 0.f, rotateRefY2 = 0.f;
  float rotateAnglePt1X = 0.f, rotateAnglePt1Y = 0.f;
  /// After base point: \p C / \p COPY toggles rotate–copy (keep originals); cleared when rotate finishes or draft resets.
  bool rotateCopyMode = false;
  /// COPY modal: when true, duplicate survey selection by pending rotation instead of translation.
  bool pendingSurveyDupIsRotate = false;
  float pendingRotateCopyBx = 0.f, pendingRotateCopyBy = 0.f, pendingRotateCopyRad = 0.f;

  // --- Survey / COGO points (in-memory database; optional JSON file) ---
  std::vector<SurveyPoint> surveyPoints;
  /// Named point groups (REQ-067) — drawing-owned rules, resolved on demand, never cached.
  std::vector<PointGroup> pointGroups;
  CreatePointsOptions createPointsOpts;
  int createPointsNextId = 1;
  bool showCreatePointsWindow = false;
  bool showSelectionCyclingWindow = false;
  /// Stable snapshot of the selection taken when the SEL panel is opened; entities remain listed even after deselection.
  std::vector<SelectedEntity> selectionCycleEntities;
  std::vector<int> selectionCycleSurveyPoints;
  enum class SurveyInversePhase { WaitFrom, WaitTo } surveyInversePhase = SurveyInversePhase::WaitFrom;
  float surveyInverseFromX = 0.f;
  float surveyInverseFromY = 0.f;

  /// REQ-074 spot elevation / grade. The first pick is kept so the second can report grade against
  /// it; the elevations are kept per surface, by name, because a point can be covered by more than
  /// one surface (existing and proposed) and a grade must be computed within one surface, never
  /// across two (Q1, TASK-055).
  enum class SurfaceElevPhase { WaitFirst, WaitSecond } surfaceElevPhase = SurfaceElevPhase::WaitFirst;
  double surfaceElevFromX = 0.0;
  double surfaceElevFromY = 0.0;
  std::vector<std::pair<std::string, double>> surfaceElevFromZ;

  /// REQ-069: the surface DESIGNATEBREAKLINE/DESIGNATEBOUNDARY is adding to, captured when the
  /// command starts (from its inline argument) so the single pick that follows knows where to add.
  std::string designateSurfaceName;
  /// REQ-069: which ring kind DESIGNATEBOUNDARY is adding — irrelevant to DESIGNATEBREAKLINE.
  CadBoundaryKind designateBoundaryKind = CadBoundaryKind::Outer;
  /// REQ-075: the description / name the Add Breaklines and Add Boundaries dialogs collected before
  /// the pick, stamped onto the definition item on commit. Empty when the command was typed rather
  /// than started from the panel, which is the ordinary case for the command line.
  std::string designateBreaklineDescription;
  std::string designateBoundaryName;

  /// REQ-085: the active polyline draft is a **3D polyline** — vertices may carry a typed elevation.
  ///
  /// A flag on the existing POLYLINE draft rather than a `Kind` of its own. The two commands differ
  /// only in how a vertex's Z is obtained; a separate `Kind` would have to be added to both viewport
  /// dispatch lists, the status text, the prompt table and the ESC path — the twelve-site tax
  /// ADR-028 warns about, paid for no behavioural difference. `userPolylineVerts` is already
  /// stride-3 XYZ, so the STORE needs nothing.
  bool polylineDraft3d = false;
  /// Elevation peeled off a typed `x,y,z` for the vertex being submitted, consumed by
  /// \ref SubmitPolylineVertex and cleared there. Invalid means "no Z was typed" — the vertex then
  /// takes \ref CadCommitElevation, i.e. the snapped point's own Z or the work plane (REQ-058).
  bool  polylineTypedZValid = false;
  bool  polylineTypedZRelative = false;  ///< `@dx,dy,dz` — dz is relative to the previous vertex.
  float polylineTypedZ = 0.f;

  bool showViewPointsWindow = false;
  bool showSettingsWindow = false;
  bool showQuickSelectWindow = false;

  /// Quick Select filter state (QUICKSELECT / QS command).
  enum class QsApplyTo    : uint8_t { EntireDrawing = 0, CurrentSelection = 1 };
  enum class QsObjectType : uint8_t { All=0, Line, Circle, Arc, Ellipse, Polyline, Text, Mtext, DimAligned, DimLinear, DimAngular, SurveyPoint };
  enum class QsProperty   : uint8_t { Layer=0, Color, Length, Radius, Closed, Content, Id, Elevation, Easting, Northing, Description };
  enum class QsOperator   : uint8_t { Equals=0, NotEquals, LessThan, GreaterThan, SelectAll };
  enum class QsInclude    : uint8_t { Include=0, Exclude };
  QsApplyTo    qsApplyTo          = QsApplyTo::EntireDrawing;
  QsObjectType qsObjectType       = QsObjectType::All;
  QsProperty   qsProperty         = QsProperty::Layer;
  QsOperator   qsOperator         = QsOperator::Equals;
  char         qsValueBuf[256]    = {};
  QsInclude    qsIncludeMode      = QsInclude::Include;
  bool         qsAppendToExisting = false;
  /// Layer manager (LAYER / ribbon LAY). Rows are synced with geometry-used names.
  bool showLayerManagerWindow = false;
  /// Text style manager (STYLE / ribbon). Create / rename / delete / edit named text styles (REQ-044).
  bool showTextStyleManagerWindow = false;
  /// Surface Style editor (REQ-070 / ADR-036 (i)) — opened by SURFSTYLE and by the Surface Manager.
  bool showSurfaceStyleWindow = false;
  bool showPointGroupManagerWindow = false;  ///< Point Group manager (REQ-067).
  bool showSurfaceManagerWindow = false;     ///< Surfaces panel (REQ-068).
  bool showFeatureLineElevWindow = false;    ///< Feature line elevation editor (REQ-088).
  /// Which feature line the elevation editor is showing, 0-based. Held here rather than as a static
  /// in the panel so that opening the editor from a selected feature line can aim it, and so it
  /// survives the window being closed and reopened.
  int featureLineElevIndex = 0;
  /// Current layer for new geometry (ribbon combo + command defaults).
  std::string currentLayer = "0";
  /// Layer table. Layer "0" always exists, **including before anything has been loaded** (issue
  /// #57): the loader used to synthesize it while a newly created drawing had an empty table, so a
  /// new drawing was briefly in a state the rest of the code is entitled to assume cannot happen —
  /// and `save -> load -> save` was not byte-identical, breaking REQ-079's first acceptance
  /// condition. Initialising here rather than at each creation site means every route to a drawing
  /// (File > New, the headless driver, an importer, a test) gets it, with no site left to forget.
  /// Safe against loading, which `clear()`s this table before repopulating it (GsIo.cpp).
  std::vector<CadLayerRow> drawingLayerTable = DefaultDrawingLayerTable();
  /// Named text styles for this drawing (REQ-044 / ADR-020). "Standard" is always present — see the
  /// layer-table note above; it had the same defect and has the same fix.
  std::vector<TextStyle> textStyles = TextStyles::DefaultTextStyles();
  /// Named surface styles for this drawing (REQ-070 / ADR-036 (d)). "Standard" is always present, and
  /// it is initialised here rather than at each creation site for the reason the two tables above
  /// document: a drawing that reaches the renderer with an empty table would be in a state the rest of
  /// the code is entitled to assume cannot happen.
  std::vector<SurfaceStyle> surfaceStyles = SurfaceStyles::DefaultSurfaceStyles();
  /// Active text style for new TEXT/MTEXT (the STYLE dropdown). Empty resolves to "Standard".
  std::string activeTextStyleName = "Standard";
  /// Viewport CAD crosshair (Drawing1): RGB 0–1, arm length as fraction of viewport width/height, pickbox half-size in px.
  float viewportCrosshairR = 1.f;
  float viewportCrosshairG = 0.8392157f;
  float viewportCrosshairB = 0.f;
  float viewportCrosshairArmFracX = 0.03f;
  float viewportCrosshairArmFracY = 0.05f;
  float viewportCrosshairPickHalfPxX = 4.f;
  float viewportCrosshairPickHalfPxY = 4.f;
  float viewportCrosshairHairPx = 1.f;
  /// Viewport background (model-space clear color): RGB 0–1. Default #1F1F2A dark gray.
  float viewportBgR = 0.1f;
  float viewportBgG = 0.1f;
  float viewportBgB = 0.1f;

  // ---------------------------------------------------------------------------------------------------------
  // Settings (AutoCAD-style Options dialog). Live tab is preserved across opens/closes via settingsActiveTabIdx.
  // The fields below are persistent UI/render preferences accessed from DrawSettingsPanel and consumed by the
  // renderer + tessellator. Names try to mirror AutoCAD VIEWRES/Options labels so they read naturally.
  // ---------------------------------------------------------------------------------------------------------
  int settingsActiveTabIdx = 1; ///< 0=Files 1=Display 2=Open and Save 3=Plot 4=System 5=User Prefs 6=Drafting 7=3D 8=Selection 9=Profiles 10=AEC

  // Display tab — Display resolution. Wired: arcCircleSmoothness caps CircleTessellationSegmentCount.
  int displayArcCircleSmoothness = 1000;       ///< Max segments per full circle (1..20000). AutoCAD VIEWRES analog.
  int displayPolylineCurveSegments = 8;        ///< Placeholder; current pipeline uses pixel-chord adaptive segments.
  float displayRenderedObjectSmoothness = 0.5f;///< Placeholder (3D-style smoothness factor).
  int displayContourLinesPerSurface = 4;       ///< Placeholder.

  // Display tab — Display performance. Placeholders (UI only).
  bool displayPanZoomWithRaster = false;
  bool displayHighlightRasterFrameOnly = true;
  bool displayApplySolidFill = true;
  bool displayShowTextBoundaryFrameOnly = false;
  bool displayDrawTrueSilhouettes = false;

  // Display tab — Window Elements (placeholders + theme tag).
  int displayColorThemeIdx = 1; ///< 0=Dark, 1=Light.
  bool displayScrollbars = false;
  bool displayLargeToolbarButtons = false;
  bool displayResizeRibbonIcons = true;
  bool displayShowTooltips = true;
  float displayTooltipDelaySec = 1.0f;
  bool displayShowShortcutKeysInTooltips = true;
  bool displayShowExtendedTooltips = true;
  float displayExtendedTooltipDelaySec = 2.0f;
  bool displayShowRolloverTooltips = true;
  bool displayShowFileTabs = true;

  // Display tab — Layout elements (placeholders).
  bool displayLayoutAndModelTabs = true;
  bool displayPrintableArea = true;
  bool displayPaperBackground = true;
  bool displayPaperShadow = true;
  bool displayPageSetupOnNewLayouts = false;
  bool displayCreateViewportInNewLayouts = true;

  // Display tab — Crosshair size (1..100, % of viewport min axis). Mirrors AutoCAD CURSORSIZE.
  int displayCrosshairSizePct = 5;

  // Decimal places shown for all non-survey user-facing coordinate/length
  // readouts (status bar, ID/INVERSE, dimensions, properties). Display only —
  // stored values keep full precision. Owned by the Drawing Units dialog
  // (UNITS command); see REQ-020.
  int displayLinearPrecision = 4;

  // Drawing Units dialog (UNITS command) visibility. REQ-020.
  bool showUnitsWindow = false;

  // Angle DISPLAY settings owned by the Drawing Units dialog (REQ-021, ADR-004).
  // Display only: the stored/compute convention (CW from north) and angle entry
  // are unchanged. Defaults reproduce the pre-feature bearing format.
  int    angleDisplayType = 1;        ///< 0=Decimal Degrees, 1=Deg/Min/Sec, 2=Surveyor's Units
  int    angleDisplayPrecision = 1;   ///< decimals on the smallest unit (deg for DD; sec for DMS/Surveyor)
  bool   angleDisplayClockwise = true;
  double angleDisplayBaseDeg = 0.0;   ///< canonical CW-from-north degrees of the 0° direction (N=0,E=90,S=180,W=270)

  // Display tab — Zoom. Wheel zoom factor per notch (AutoCAD ZOOMFACTOR analog). 1.10 = 10% per notch.
  // Clamped 1.01..3.0 at the call site; higher = faster zoom, lower = finer control.
  float displayWheelZoomFactor = 1.15f;

  // Display tab — Fade control (placeholders).
  int displayFadeXref = 50;
  int displayFadeInPlace = 70;

  // System tab — Hardware acceleration toggle, drives MSAA + line smoothing.
  bool systemHardwareAcceleration = true;
  /// BUG-013: opt back to the integrated GPU on a hybrid laptop, for battery life in the field.
  /// Default false — a CAD application should ask for the capable GPU, and until 2026-08-15 this
  /// one asked for nothing and silently got the other one. Applied by writing Windows' own
  /// per-application preference, so it takes effect at the **next launch**, not this one.
  bool systemPreferIntegratedGpu = false;
  /// Result of the last attempt to record that preference with Windows, shown beside the checkbox.
  /// Empty until the user toggles it. It is shown rather than logged because the settings dialog is
  /// where the user is looking when they change it (REQ-201).
  std::string systemGpuPreferenceMessage;
  bool systemAutoCheckCertificationUpdate = true;
  bool systemDisplayOLETextSizeDialog = true;
  bool systemBeepOnError = false;
  bool systemAllowLongSymbolNames = true;
  bool systemAccessOnlineContent = true;
  bool systemStoreLinksIndexInDrawing = true;
  bool systemOpenTablesReadOnly = false;
  int systemLayoutRegenOption = 0; ///< 0=Regen on switch, 1=Cache model+last, 2=Cache model+all.

  // System → Graphics Performance sub-dialog.
  bool showGraphicsPerformanceDialog = false;
  bool gfxSmoothLineDisplay = true;            ///< Wired: GL_LINE_SMOOTH + MSAA when systemHardwareAcceleration on.
  bool gfxAcceleratedFontDisplay = true;       ///< Placeholder (font rasterization through ImGui is already GPU).
  int gfxVideoMemoryCachingLevel = 5;          ///< Placeholder 1..5.
  bool gfx3dFastShadedMode = true;             ///< Placeholders (no 3D pipeline).
  bool gfx3dAdvancedMaterialEffects = true;
  bool gfx3dFullShadowDisplay = true;
  bool gfx3dPerPixelLighting = true;

  /// Editable ID strings for VIEWPOINTS table rows (synced from point IDs when empty).
  std::vector<std::string> surveyPointIdBuffers;
  bool showImportPointsWindow = false;
  bool showExportPointsWindow = false;
  char surveyImportCsvPath[512]{};
  char surveyExportCsvPath[512]{};
  /// UTF-8 path to optional startup .gs (Settings → Startup). Empty = use bundled resources/default-template.gs.
  char defaultWorkspaceTemplatePathUtf8[768]{};
  /// Active UI layout stem (file resources/layouts/<stem>.ini). See View → Layout.
  char activeUiLayoutNameUtf8[64]{"default"};
  bool openSaveLayoutAsPopup = false;
  char saveLayoutAsNameBufUtf8[64]{};
  bool pendingBuiltinDockLayoutReset = false;
  int surveyImportCsvLayoutIdx = 0;
  int surveyExportCsvLayoutIdx = 0;
  bool surveyImportCsvSkipFirstRow = false;
  bool surveyExportCsvWriteHeader = true;
  bool surveyImportPreviewDirty = true;
  std::string surveyImportPreviewText;
  std::string surveyImportPreviewValidation;
  /// REQ-041: a file-level problem (missing/empty/locked/no valid rows) blocks import.
  bool surveyImportFileBlocked = true;
  /// REQ-041: rows that would import vs. rows that would be skipped (parse error / duplicate ID).
  int surveyImportValidRowCount = 0;
  int surveyImportBadRowCount = 0;
  /// REQ-041 rev 3 (BUG-014): an import has run and \ref surveyImportPreviewValidation holds its
  /// outcome, not a validation verdict. Import is blocked because the rows are already in the
  /// drawing — which is a success, so the panel must not colour it as an error. Cleared by
  /// SurveyCsvRefreshImportPreview, i.e. by any change the user makes to path/layout/header.
  bool surveyImportJustImported = false;
  std::vector<std::pair<std::string, std::string>> surveyReportTabs;
  int surveyReportSelectedTab = 0;
  bool surveyReportSelectLatestPending = false;
  /// Viewport-picked survey rows (indices into \ref surveyPoints). Additive clicks; Shift removes.
  std::vector<int> selectedSurveyPointIndices;
  /// COPY placed CAD duplicates; modal collects policy before duplicating selected survey points.
  bool copySurveyDupModalOpen = false;
  bool copySurveyDupModalOpenRequested = false;
  float pendingCopyDx = 0.f;
  float pendingCopyDy = 0.f;
  SurveyDuplicatePolicy copySurveyDuplicatePolicy = SurveyDuplicatePolicy::Renumber;
  /// DXF import merges its embedded survey points with existing ones. Points whose ID collides are held
  /// here (in WORLD coordinates) until the user resolves them via the conflict modal.
  std::vector<SurveyPoint> pendingDxfConflictPoints;
  bool dxfPointConflictModalOpen = false;
  bool dxfPointConflictModalOpenRequested = false;
  int  dxfPointConflictOffset = 0;
  /// True while the viewport command palette should mirror the command line (hover latched until idle / mouse away).
  bool viewportCmdPaletteEngaged = false;
  /// True when the viewport command palette is visible — command line defers its InputText to avoid duplicate focus.
  bool viewportDrawingHovered = false;

  // -------------------------------------------------------------------------
  // Open drawings tab bar
  // -------------------------------------------------------------------------
  struct DrawingTab {
    std::string  name;
    uint32_t     uid = 0;  ///< Stable per-tab ID used in ImGui label suffix to prevent ID collisions.
  };
  std::vector<DrawingTab>     drawingTabs{{"Drawing 1", 1u}};
  int      activeDrawingIdx   = 0;
  int      nextDrawingNumber  = 2;    ///< Auto-incremented for "Drawing N" naming.
  uint32_t nextTabUid         = 2u;   ///< Monotonically increasing; each new tab gets a unique uid.
  bool pendingDrawingTabSwitch = false; ///< Set for one frame after a programmatic tab change.
  bool pendingViewportFocus   = false; ///< Request ImGui focus on the Drawing1 window next frame.
  bool pendingPropertiesFocus = false; ///< Request ImGui focus on the Properties tab on next Begin().
  bool propertiesPanelActive  = false; ///< True when Properties is the selected tab in its dock node.
  int  prevDrawingIdx         = 0;     ///< Authoritative "last active" idx; used by main.cpp for switch detection.
  int  pendingTabErase        = -1;    ///< If >= 0, main.cpp must shut down + erase viewportRenderers[this index].
  /// Per-drawing snapshots — one entry per open tab.  Active tab's live data lives in the fields
  /// above; this vector is read/written by SaveDocumentToSnapshot / RestoreDocumentFromSnapshot.
  std::vector<DrawingDocument> documents{1};

  // --- Active-document dirty/path tracking (mirrors DrawingDocument fields for the live tab) ---
  uint32_t    activeDocSavedRevision = 0;   ///< cadGpuRevision when the active doc was last saved.
  std::string activeDocFilePath;            ///< Absolute path to the active doc's .gs file.
  int undoHistoryMaxSize = 50; ///< Maximum undo frames per drawing tab (0 = unlimited). Settings → User Preferences.

  // --- Close confirmation ---
  bool confirmCloseModal = false;  ///< Set by the main loop to open the "Unsaved Changes" dialog.
  bool closeConfirmed    = false;  ///< Set by the dialog to signal the main loop to exit.

  // --- DWG export confirmation (REQ-052) ---
  /// Set when the user has chosen a DWG save path; the dialog states what Phase 1 export drops
  /// before anything is written, because a DWG save can overwrite a drawing GoSurvey did not author.
  bool        dwgLossyExportModal = false;
  std::string dwgPendingExportPath;  ///< Destination chosen in the save dialog, written only on confirm.

  // -------------------------------------------------------------------------
  // ALIGN command state (Helmert transformation)
  // -------------------------------------------------------------------------
  enum class AlignPhase { PickSelection, PickSrc, PickDst } alignPhase = AlignPhase::PickSrc;

  struct AlignControlPt { float srcX = 0.f, srcY = 0.f, dstX = 0.f, dstY = 0.f; };
  std::vector<AlignControlPt> alignControlPts;

  struct HelmertResult {
    bool  valid = false;
    float a  = 1.f, b  = 0.f;   ///< X' = a*x - b*y + tx
    float tx = 0.f, ty = 0.f;
    float scale = 1.f;
    float rotationCwNorthDeg = 0.f;
    std::vector<float> pairResiduals; ///< per-pair distance residual (destination units)
    float rms = 0.f;
    int   nPairs = 0;
  } alignLastResult;

  bool showAlignResultsWindow = false;
  /// Snapshot of selection committed at ALIGN PickSelection → PickSrc transition.
  std::vector<SelectedEntity> alignSelectionSnapshot;
  std::vector<int> alignSurveySnapshot;
  bool alignHasSelection = false; ///< true = only transform snapshotted entities; false = all

  // -------------------------------------------------------------------------
  // PDFATTACH command state
  // -------------------------------------------------------------------------
  enum class PdfAttachPhase {
    WaitDialog,       ///< Dialog is open; user browses / configures
    Building,         ///< Async rasterize running in background; dialog shows spinner
    WaitInsertPoint,  ///< User picks insertion point in viewport
    WaitScaleRef,     ///< User picks second point to define scale interactively
    WaitRotationPt,   ///< User picks rotation reference point
  } pdfAttachPhase = PdfAttachPhase::WaitDialog;

  bool pdfAttachDialogOpen = false;
  char pdfAttachFilePath[1024]{};
  int  pdfAttachSelectedPage = 0;
  float pdfAttachInsertX  = 0.f;
  float pdfAttachInsertY  = 0.f;
  float pdfAttachScale    = 1.f;
  float pdfAttachRotDeg   = 0.f;
  /// DPI used to rasterize the final attached page texture.
  float pdfAttachRasterDpi = 150.f;
  bool pdfAttachSpecifyInsert = true;
  bool pdfAttachSpecifyScale  = false;
  bool pdfAttachSpecifyRot    = false;
  bool pdfAttachSnapLines   = true;
  bool pdfAttachSnapCircles = true;
  bool pdfAttachSnapText    = false; // text positions cause spurious endpoint snaps; disabled by default
  /// Opaque per-document draft cache (owned; freed when command ends or file changes).
  PdfDraftCache* pdfDraftCache = nullptr;

  // --- Async build (Building phase) ------------------------------------
  // Background thread rasterizes the page; main thread uploads the GL texture
  // when done.  Heap-allocated so atomic members don't affect copyability.
  struct AsyncBuild {
    std::thread           thread;
    std::atomic<bool>     done{false};
    PdfAttachPixelResult  result;
    bool                  specifyInsert = false; ///< captured at click time
  };
  std::unique_ptr<AsyncBuild> pdfAttachAsync;   ///< non-null while Building

  /// Preview attachment built during WaitInsertPoint (cursor-follows).
  PdfAttachment pdfAttachPreview;
  bool          pdfAttachPreviewReady = false;

  /// Committed PDF underlays.
  std::vector<PdfAttachment> pdfAttachments;

  // -------------------------------------------------------------------------
  // PAPER SPACE (REQ-025/026/031) — active drawing's layouts; mirrored per tab in DrawingDocument.
  std::vector<PaperLayout> paperLayouts;
  int activeSpaceIndex = kModelSpaceIndex;   ///< -1 = model space; else index into paperLayouts.
  int lastPaperLayoutIndex = 0;              ///< layout the MODEL/PAPER toggle returns to.
  int selectedViewportLayout = -1;           ///< layout owning the primary selected viewport (REQ-027), or -1.
  int selectedViewportIndex = -1;            ///< primary selected viewport (popup edit/grips), or -1.
  std::vector<int> selectedViewports;        ///< all selected viewports in the active layout (REQ-035).
  // Rectangular viewport command draft (REQ-033): 0 = need first corner, 1 = need second.
  int   paperVpPhase = 0;
  float paperVpFirstXIn = 0.f;
  float paperVpFirstYIn = 0.f;
  // Paper-space grip edit (REQ-035): grabbed grip on selectedViewportIndex (click-grab, move, click-commit).
  int   paperGripCorner = -2;                ///< -2 none, -1 move (whole viewport), 0..3 resize corner.
  // Paper-space MOVE/COPY of selected viewports (REQ-035): 0 idle, 1 need base, 2 need destination.
  int   paperMovePhase = 0;
  bool  paperMoveIsCopy = false;
  float paperMoveBaseXIn = 0.f;
  float paperMoveBaseYIn = 0.f;
  // Paper-space window selection box (REQ-035).
  bool  paperSelBoxActive = false;
  float paperSelBoxX0In = 0.f;
  float paperSelBoxY0In = 0.f;
  // Paper-space native geometry selection + edit (REQ-037, ADR-009). Indices into the ACTIVE layout's
  // paperLines (line index = segment, i.e. flat offset/6) and paperTexts. Coexists with viewport selection.
  std::vector<PaperEntityRef> selectedPaperEntities;
  // Paper-space ROTATE of selected paper entities: 0 idle, 1 need base point, 2 need rotation angle.
  int   paperRotatePhase = 0;
  float paperRotateBaseXIn = 0.f;
  float paperRotateBaseYIn = 0.f;
  // Floating model space (REQ-036): edit the model IN PLACE through a viewport. The active space stays
  // the paper layout (sheet + viewports stay visible); model edit/snap/draw is routed through the viewport.
  int    floatingViewportLayout = -1;   ///< paper layout of the floating viewport, or -1 if not floating.
  int    floatingViewportIndex = -1;    ///< viewport being edited in place, or -1.
  /// Viewport zoom lock (user request): when ON, pan/zoom always targets the sheet; when OFF and editing a
  /// viewport in place, pan/zoom adjusts that viewport's model framing (scale/center).
  bool   viewportZoomLocked = false;
  // Saved model-space view so switching Model<->Paper keeps each space's own pan/zoom (each layout saves
  // its own in PaperLayout::view*). Fixes the new layout opening on the model's zoomed-out view.
  double modelViewPanX = 0.0;
  double modelViewPanY = 0.0;
  float  modelViewZoom = 1.f;
  bool   modelViewSaved = false;

  // Page setups + layout-tab dialogs (right-click menu → Rename / Move-Copy / Page Setup Manager / Delete).
  std::vector<PageSetup> savedPageSetups;        ///< drawing-wide named page setups; "Standard" ensured.
  bool showPageSetupManager = false;
  bool showNewPageSetup     = false;
  bool showPageSetupEditor  = false;             ///< the big "Modify" page-setup editor.
  bool showMoveCopyLayout   = false;
  int  pageSetupLayoutIdx   = -1;                ///< layout the dialogs target.
  int  pageSetupManagerSel  = -1;                ///< Page Setup Manager selection: -1 = layout's current, >=0 saved idx.
  bool pageSetupDisplayOnNew = false;            ///< "Display when creating a new layout".
  int  pageSetupEditorTarget = -1;               ///< editor edits: -1 = layout's current, >=0 = saved setup idx.
  PageSetup pageSetupEditorDraft;                ///< working copy while the editor is open.
  char newPageSetupName[64] = "Setup1";
  int  newPageSetupStartWith = 3;                ///< index into the New "Start with" list.
  int  moveCopyBeforeSel    = 0;                 ///< Move-or-Copy "Before layout" selection (== count → move to end).
  bool moveCopyCreateCopy   = false;
  int  layoutRenameIdx      = -1;                ///< layout being renamed inline, or -1.
  char layoutRenameBuf[64]  = "";
  bool showViewportsWindow  = false;             ///< Viewports manager window (moved off the status bar).
  bool showBatchPlotDialog  = false;             ///< Batch-plot dialog (select layouts → multi-page PDF).
  std::vector<int> batchPlotSelected;            ///< layout indices ticked in the batch-plot dialog.

  // -------------------------------------------------------------------------
  // TRAVERSE EDITOR state
  // -------------------------------------------------------------------------
  bool showTraverseEditorWindow = false;
  TraverseData traverseData;
  /// When true, traverseData must be recomputed before the next panel draw.
  bool traverseDataDirty = true;

  /// Closure-analysis window (unadjusted vs least-squares, REQ-014).
  bool        showTraverseClosureWindow = false;
  LsaWeights  traverseLsaWeights;          ///< Editable a-priori standard errors.
  LsaResult   traverseLsaResult;           ///< Last computed adjustment.
  bool        traverseLsaComputed = false; ///< True once a result has been produced.
  bool        traverseLsaAccepted = false; ///< User accepted the LSA result.

  /// Index of the leg whose per-leg observation editor is expanded (REQ-018),
  /// or -1 when none. Accordion: at most one leg is expanded at a time.
  int         traverseExpandedLeg = -1;

  // -------------------------------------------------------------------------
  // CLIPBOARD (COPYCLIP / PASTECLIP)
  // -------------------------------------------------------------------------
  CadClipboard clipboard;
};


inline float DefaultAnnotationTextHeightWorld(const AppCommandState& st) {
  return st.defaultPlottedTextHeightInches * st.modelUnitsPerPlottedInch;
}

/// Build the model viewport's camera from the canonical view state (REQ-058 / ADR-025 (c)).
///
/// The camera is **derived, never stored**: pan is the target, zoom is the ortho half-height, and
/// only the two orientation angles are new state. Constructing it fresh at each use makes drift
/// between "the camera" and "the view" impossible. Commands → Renderer is a downward dependency
/// (architecture §2), so including the Camera value type here is legal.
///
/// `halfH = (1/zoom) * 50` reproduces the constant the renderer and the UI have always shared.
///
/// The near/far range is the Camera's own +/-100000, NOT the pre-3D `Ortho(..., -1000, 1000)`.
/// The old +/-1000 was carried over from the flat renderer, where nothing ever had a Z; once Z is
/// real it silently clips every entity above 1000 out of the view — and a surveyed site sits at an
/// elevation of a few thousand feet, so that is the whole drawing, in plan view, where Z should not
/// affect visibility at all. Widening costs nothing: depth testing is off (draw order decides), so
/// the range only ever determines what survives clipping.
inline Camera CadViewCamera(const AppCommandState& st) {
  Camera c = Camera::Plan(st.viewportPanX, st.viewportPanY,
                          (1.f / std::max(st.viewportZoom, 1.e-9f)) * 50.f);
  c.targetZ = st.viewportPanZ;
  c.azimuthDeg = st.viewportAzimuthDeg;
  c.elevationDeg = st.viewportElevationDeg;
  c.nearZ = -100000.f;
  c.farZ = 100000.f;
  return c;
}

/// True when the model view is unrotated — the case in which every pre-3D screen/world mapping,
/// pick test and snap remains exactly valid and is therefore used unchanged (REQ-058 acceptance:
/// "plan view renders pixel-comparable to the pre-change build").
inline bool CadViewIsPlan(const AppCommandState& st) {
  return std::fabs(st.viewportElevationDeg - 90.f) < 1e-4f && std::fabs(st.viewportAzimuthDeg) < 1e-4f;
}

/// Duration of a ViewCube orientation change, in seconds. Short enough not to feel sluggish, long
/// enough to read the rotation — REQ-059 requires the view to settle within 0.5 s.
inline constexpr float kViewAnimSeconds = 0.28f;

/// Begin easing the view to \p az / \p el (REQ-059). Azimuth travels the SHORT way around, so a
/// move from 350° to 45° turns 55° forward rather than 305° backward.
inline void CadStartViewAnimation(AppCommandState& st, float az, float el) {
  st.viewAnimFromAz = st.viewportAzimuthDeg;
  st.viewAnimFromEl = st.viewportElevationDeg;
  // Unwrapped target, so the lerp below cannot take the long way round (Camera::ShortestAzimuthDelta).
  st.viewAnimToAz = st.viewportAzimuthDeg + Camera::ShortestAzimuthDelta(st.viewportAzimuthDeg, az);
  st.viewAnimToEl = el;
  st.viewAnimT = 0.f;
  st.viewAnimActive = true;
}

/// Advance an in-flight view animation by \p dtSeconds. No-op when nothing is animating.
inline void CadTickViewAnimation(AppCommandState& st, float dtSeconds) {
  if (!st.viewAnimActive)
    return;
  st.viewAnimT += (dtSeconds > 0.f ? dtSeconds : 0.f) / kViewAnimSeconds;
  if (st.viewAnimT >= 1.f) {
    st.viewAnimT = 1.f;
    st.viewAnimActive = false;
  }
  const float t = st.viewAnimT;
  const float e = t * t * (3.f - 2.f * t);  // smoothstep: eases in and out, no overshoot
  float az = st.viewAnimFromAz + (st.viewAnimToAz - st.viewAnimFromAz) * e;
  while (az < 0.f)
    az += 360.f;
  while (az >= 360.f)
    az -= 360.f;
  st.viewportAzimuthDeg = az;
  st.viewportElevationDeg = st.viewAnimFromEl + (st.viewAnimToEl - st.viewAnimFromEl) * e;
}

/// Elevation at which newly drawn geometry lands — the active work plane's Z (REQ-058).
///
/// Exact while the work plane stays parallel to XY, which is all the UCS command currently
/// produces. A tilted plane would make Z vary across the plane, and the creation sites would then
/// need the click's own intersection Z (\c uiCursorWorldZ) rather than this constant — recorded so
/// the limitation is visible if tilted UCS support is ever added.
inline float CadWorkPlaneElevation(const AppCommandState& st) {
  return static_cast<float>(st.ucsOriginZ);
}

/// Elevation a click should COMMIT at: the snapped point's own Z when an object snap is active,
/// otherwise the work plane (REQ-058).
///
/// This is the AutoCAD rule — an object snap returns the object's real 3D point, so snapping to
/// the end of a line lying on the datum gives you that endpoint even when ELEV is set well above
/// it. Without the override, snapped geometry would be silently lifted to the current elevation
/// and would not touch the thing it was snapped to.
inline float CadCommitElevation(const AppCommandState& st) {
  return st.viewportSnapPickValid ? st.viewportSnapPickLocalZ : CadWorkPlaneElevation(st);
}

/// True when the work plane is the world XY plane at Z = 0 — the default, and what the status bar
/// reports as "World".
inline bool CadUcsIsWorld(const AppCommandState& st) {
  return st.ucsOriginZ == 0.0 && st.ucsOriginX == 0.0 && st.ucsOriginY == 0.0 && st.ucsNormalZ == 1.0 &&
         st.ucsNormalX == 0.0 && st.ucsNormalY == 0.0 && st.ucsAzimuthDeg == 0.f;
}

/// The active work plane (UCS) a viewport click resolves against (REQ-058 / ADR-025 (e)).
inline ray3d::Plane CadActiveWorkPlane(const AppCommandState& st) {
  ray3d::Plane p;
  p.point = {st.ucsOriginX, st.ucsOriginY, st.ucsOriginZ};
  p.normal = {st.ucsNormalX, st.ucsNormalY, st.ucsNormalZ};
  return p;
}


/// Which entity array a \ref EntityRef designates. Mirrors SelectedEntity::Type for the kinds that
/// carry an EntityAttributes, which is exactly the set REQ-076 gives an id.
enum class EntityKind : std::uint8_t {
  Line = 0, Circle, Arc, Ellipse, Polyline, Annotation, FilledRegion, Mesh,
  FeatureLine, ///< REQ-087. Appended, never inserted — the value is not persisted, but reordering
               ///< would still silently change every switch that lists kinds in order.
  /// REQ-068 / ADR-036 (a). Appended for a second, sharper reason than the one above: id assignment
  /// walks the attribute arrays in `kEntityKindsInSweepOrder`, so inserting Surface anywhere but the
  /// end would renumber every entity in every existing drawing on its next load — and REQ-069's
  /// breakline and boundary references are stored by exactly those ids. Appending is what keeps a
  /// legacy `.gs` loading with the ids it loaded with yesterday.
  Surface
};

/// The result of resolving a stable id (REQ-076): which array, and the index *at this moment*.
///
/// The index is deliberately a **return value, not something you store** — it is valid only until
/// the next erase, which is the whole reason ids exist (architecture §11.9). Store the id; resolve
/// when you need to touch the entity.
struct EntityRef {
  EntityKind kind = EntityKind::Line;
  int        index = -1;   ///< -1 = the id does not resolve (erased, or never existed).
  [[nodiscard]] bool valid() const { return index >= 0; }
};

/// Assign a stable id to every entity that lacks one (REQ-076 / ADR-027).
///
/// **Idempotent**: an entity that already has an id keeps it, always. Only `id == 0` is filled, from
/// \ref AppCommandState::nextEntityId, in a fixed array order — which is what makes assignment
/// deterministic for a legacy `.gs` (same file, same ids, every load).
///
/// Deliberately called only at **cold boundaries** — before an undo snapshot, before a `.gs` save,
/// and before a reference is taken — never per frame (architecture §11.7). Assigning at the 127
/// sites that construct an EntityAttributes was rejected: a missed site there is not a compile
/// error, it is a silently id-less entity (the ADR-025 (a) lesson).
/// Bring \ref AppCommandState::surfaceDisplayCache and \ref AppCommandState::surfaceDisplayGeometry
/// up to date — ADR-036 (e). Called once a frame, beside \ref TickSurfaceRebuilds.
///
/// Generates each style component a surface asks for — triangle edges, border, minor and major
/// contours — and leaves the buffer for a component that is switched off empty, so REQ-070's "a style
/// with triangles off and contours on draws only contours" is a property of what exists rather than
/// of what the renderer remembers to skip.
///
/// Regenerates an entry only when its staleness key `(triangulation pointer, resolved style)` has
/// moved, and **returns before allocating** when nothing has: at REQ-100's ~200k-triangle profile the
/// generation walks 600k edges, so an early-out placed after a `clear()` would still cost the frame
/// it was written to save (§11 invariant 7). Entries whose surface id no longer resolves are reaped
/// in the same pass, so an erased surface's geometry does not outlive it.
///
/// The key does **not** include the surface's definition, and that is the mechanism behind REQ-070's
/// central claim: changing a contour interval moves the style half of the key and nothing else, so
/// the triangulation is never rebuilt and the `shared_ptr` it hangs from is never even touched.
///
/// The batch assembly at the end is redone every call — it copies pointers and colours, never
/// vertices — because layer visibility and object isolation change with no surface geometry changing
/// at all.
void RefreshSurfaceDisplayGeometry(AppCommandState& st);

/// Border edges of surface \p surfaceIndex from the cache, or nullptr when it has none (never built,
/// or the cache has not caught up this frame). Six floats per segment.
/// Contour segments currently held in the display cache, across every surface (REQ-100 profile (c)).
///
/// For the BENCH record: profile (c) is defined as a CONTOURED surface, so "how many contour
/// segments were on screen" is part of what makes one run comparable with the next.
[[nodiscard]] int SurfaceDisplayContourSegs(const AppCommandState& st);

/// True when surface \p surfaceIndex asked for more contour levels than the display path generates,
/// with \p levelsAsked filled in — REQ-201, so "no contours" is never left to look like a defect.
///
/// The display pass runs once a frame with no command in flight and nowhere to log, so it records the
/// fact on the cache entry and the Surface Manager is what says it out loud.
[[nodiscard]] bool SurfaceContoursSuppressed(const AppCommandState& st, size_t surfaceIndex,
                                             int* levelsAsked);

[[nodiscard]] const std::vector<float>* SurfaceBorderEdges(const AppCommandState& st, size_t surfaceIndex);

/// Is surface \p surfaceIndex visible right now — built, on a layer that is on and not frozen, and
/// not isolated out (REQ-068, REQ-084 (d))?
///
/// **One rule, three readers.** Drawing (\ref AppendSurfaceEdgeLines), picking
/// (\ref PickClosestCadEntity) and the REQ-074 elevation readout each need it, and before this
/// existed the first two carried hand-copied versions that had already drifted apart: neither
/// consulted `hiddenEntityIds`, so an isolated-out surface stayed on screen. The point of a shared
/// predicate is that "invisible" and "unclickable" cannot disagree — which is exactly what REQ-084
/// (d) requires of every entity kind.
[[nodiscard]] bool SurfaceVisible(const AppCommandState& st, size_t surfaceIndex);

/// The rollover readout for plan position (\p x, \p y) — one row per **visible surface covering it**
/// (REQ-089). \p out is cleared first; an empty result means "no surface here", which is the signal
/// to show nothing at all.
///
/// Covering is decided per triangle by \ref TinElevationAt, which is what makes REQ-089's "outside
/// the border, in a concave notch, or inside a hide-boundary void shows no readout" true by
/// construction rather than by three separate tests: there is simply no triangle in any of those
/// places, so the surface produces no row.
///
/// **Not cheap** — the query walks every triangle of every visible surface. REQ-089 makes calling it
/// once per cursor rest an acceptance condition for exactly that reason; see `util/hoverdwell.hpp`.
void BuildSurfaceHoverRows(const AppCommandState& st, double x, double y,
                           std::vector<SurfaceHoverRow>* out);

/// Index of the surface named \p name (case-insensitive), or -1 (REQ-068).
///
/// For a name the **user** typed — a command argument, a panel selection. For a reference held
/// across time (an in-flight rebuild, a cross-object link), use \ref FindSurfaceIndexById: a name
/// can be changed, and an erased name can be taken by a different surface.
[[nodiscard]] int FindSurfaceIndex(const AppCommandState& st, const std::string& name);

/// Index of the surface whose stable entity id is \p id, or -1 when it does not resolve — erased, or
/// never assigned (REQ-076 / ADR-036 (a)). `id == 0` always returns -1: 0 means "unassigned", so
/// matching on it would resolve to whichever surface happened not to have been swept yet.
[[nodiscard]] int FindSurfaceIndexById(const AppCommandState& st, std::uint64_t id);

/// (Re)build \p surface's triangulation from its source point groups (REQ-068).
///
/// Replaces the shared pointer wholesale rather than writing through it — the condition the
/// architecture §11.5 sharing exemption rests on. Reports what it did and, on failure, leaves the
/// surface's previous triangulation untouched rather than half-replacing it (REQ-001).
///
/// Returns true when a surface was produced.
bool BuildSurfaceFromSources(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log);

/// Create a named surface from \p groupNames and build it. Returns the new surface's index, or -1
/// when the name is taken or the build produced nothing.
int CreateSurfaceFromPointGroups(AppCommandState& st, const std::string& name,
                                 const std::vector<std::string>& groupNames,
                                 std::vector<std::string>& log);

/// Erase a surface by index, keeping its attribute array in step. Callers own the undo snapshot.
void EraseSurfaceAtIndex(AppCommandState& st, size_t index);

/// REQ-069's dynamic rebuild. Called once per frame (main.cpp, beside \ref EnsureEntityIds — both
/// are revision-gated per-frame maintenance run before anything can save, reference or render a
/// surface). Reaps any completed background rebuild — applying it if \c cadGpuRevision has not moved
/// since it was dispatched, discarding it otherwise (architecture §8 rule 4: undo or a further edit
/// while it ran) — then dispatches a fresh background rebuild for every surface whose
/// \c builtAtRevision is behind the current revision and does not already have one in flight, which
/// is what makes a single command that touches N points, or N surfaces, coalesce to at most one
/// rebuild per surface rather than N.
void TickSurfaceRebuilds(AppCommandState& st, std::vector<std::string>& log);

void EnsureEntityIds(AppCommandState& st);

/// Take the next id immediately, for the caller to stamp on an entity it is creating.
///
/// For the case \ref EnsureEntityIds cannot serve: code that creates an entity **and stores a
/// reference to it in the same breath** — a survey point and its label (REQ-023) — where waiting
/// for the sweep would mean writing down a reference to id 0.
[[nodiscard]] std::uint64_t AllocEntityId(AppCommandState& st);

/// Resolve a stable id to its array and current index, or an invalid ref if the entity is gone.
///
/// Linear over the attribute arrays: ADR-027 (c) deliberately keeps **no stored id→index map**,
/// because the dominant access is "resolve a handful of ids when something rebuilds", not a
/// per-frame lookup, and a permanent map would cost sync risk for nothing (architecture §5).
/// Resolving many ids at once? Build a local map from the same arrays and throw it away after.
[[nodiscard]] EntityRef FindEntityById(const AppCommandState& st, std::uint64_t id);

/// Capture the active tab's current geometry into the undo stack; clears redo stack; trims to undoHistoryMaxSize.
void PushUndoSnapshot(AppCommandState& st, const std::string& description);
/// Undo: restore previous geometry snapshot; push current to redo stack. Returns true if an undo was performed.
bool DoUndo(AppCommandState& st, std::vector<std::string>& log);
/// Redo: restore next geometry snapshot; push current to undo stack. Returns true if a redo was performed.
bool DoRedo(AppCommandState& st, std::vector<std::string>& log);
/// True if the active tab has at least one undo frame available.
bool CanUndo(const AppCommandState& st);
/// True if the active tab has at least one redo frame available.
bool CanRedo(const AppCommandState& st);


inline void BumpCadGpuCache(AppCommandState& st) { ++st.cadGpuRevision; }

/// Keeps per-entity attribute vectors sized to match geometry counts (used by Properties and select-similar).
void EnsureAttrCounts(AppCommandState& st);

void SyncDrawingLayerTableWithGeometry(AppCommandState& st);

bool CadAddDrawingLayer(AppCommandState& st, const std::string& name, std::string* err);

bool CadRenameDrawingLayer(AppCommandState& st, const std::string& oldName, const std::string& newName, std::string* err);

bool CadDeleteDrawingLayer(AppCommandState& st, const std::string& name, std::string* err);

inline void RestoreMtextGripOriginal(AppCommandState& st) {
  if (!st.mtextGripMoveActive)
    return;
  const int aix = st.mtextGripAnnotationIndex;
  if (aix < 0 || static_cast<size_t>(aix) >= st.cadAnnotations.size())
    return;
  CadAnnotation& ann = st.cadAnnotations[static_cast<size_t>(aix)];
  if (ann.kind != CadAnnotation::Kind::Mtext)
    return;
  ann.boxMinX = st.mtextGripOrigBoxMinX;
  ann.boxMaxX = st.mtextGripOrigBoxMaxX;
  ann.boxMinY = st.mtextGripOrigBoxMinY;
  ann.boxMaxY = st.mtextGripOrigBoxMaxY;
  ann.insX = ann.boxMinX;
  ann.insY = ann.boxMinY;
}

inline void ClearMtextGripInteraction(AppCommandState& st) {
  st.mtextGripMoveActive = false;
  st.mtextGripAnnotationIndex = -1;
  st.mtextGripCorner = -1;
  st.mtextGripDownWorldX = 0.f;
  st.mtextGripDownWorldY = 0.f;
}

/// Cancel in-progress MTEXT grip edit and restore the box (selection change, new command, fence, etc.).
inline void AbortMtextGripInteraction(AppCommandState& st) {
  RestoreMtextGripOriginal(st);
  ClearMtextGripInteraction(st);
}

inline void CloseMtextRichEditorUi(AppCommandState& st) {
  st.mtextRichEditorOpen = false;
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorPaper = false;
  st.mtextRichEditorPaperLayout = -1;
  st.mtextRichEditorPlain = false;
  st.mtextRichEditorAnnIndex = -1;
  st.mtextRichEditorBuf.clear();
  st.mtextRichEditorFocusRequest = false;
  st.mtextRichEditorCursor = 0;
  st.mtextRichEditorSelStart = 0;
  st.mtextRichEditorSelEnd = 0;
  st.mtextRichEditorTypingAllCaps = false;
  st.mtextEditCaret = 0;
  st.mtextEditAnchor = 0;
  st.mtextEditFocused = false;
  st.mtextEditMouseSelecting = false;
  st.mtextEditScrollY = 0.f;
  st.mtextEditUndo.clear();
  st.mtextEditRedo.clear();
}

/// The annotation the in-place text editor is currently editing (REQ-039 phase 2): a model
/// \c cadAnnotations entry or, when \c mtextRichEditorPaper, the active paper layout's \c paperTexts
/// entry. Returns \c nullptr for placement mode (no existing object yet) or an out-of-range index.
inline CadAnnotation* MtextRichEditorTargetAnnotation(AppCommandState& st) {
  if (!st.mtextRichEditorOpen || st.mtextRichEditorPlacement)
    return nullptr;
  const int ix = st.mtextRichEditorAnnIndex;
  if (ix < 0)
    return nullptr;
  if (st.mtextRichEditorPaper) {
    if (st.mtextRichEditorPaperLayout < 0 ||
        static_cast<size_t>(st.mtextRichEditorPaperLayout) >= st.paperLayouts.size())
      return nullptr;
    PaperLayout& L = st.paperLayouts[static_cast<size_t>(st.mtextRichEditorPaperLayout)];
    if (static_cast<size_t>(ix) >= L.paperTexts.size())
      return nullptr;
    return &L.paperTexts[static_cast<size_t>(ix)];
  }
  if (static_cast<size_t>(ix) >= st.cadAnnotations.size())
    return nullptr;
  return &st.cadAnnotations[static_cast<size_t>(ix)];
}

/// The attributes parallel to \ref MtextRichEditorTargetAnnotation — the entity colour/layer/linetype row
/// of the text being edited (REQ-051's colour control writes it). Same nullptr cases, plus a parallel-vector
/// length mismatch, which is never expected but must not index out of range.
inline EntityAttributes* MtextRichEditorTargetAttrs(AppCommandState& st) {
  if (!st.mtextRichEditorOpen || st.mtextRichEditorPlacement)
    return nullptr;
  const int ix = st.mtextRichEditorAnnIndex;
  if (ix < 0)
    return nullptr;
  if (st.mtextRichEditorPaper) {
    if (st.mtextRichEditorPaperLayout < 0 ||
        static_cast<size_t>(st.mtextRichEditorPaperLayout) >= st.paperLayouts.size())
      return nullptr;
    PaperLayout& L = st.paperLayouts[static_cast<size_t>(st.mtextRichEditorPaperLayout)];
    if (static_cast<size_t>(ix) >= L.paperTextAttrs.size())
      return nullptr;
    return &L.paperTextAttrs[static_cast<size_t>(ix)];
  }
  if (static_cast<size_t>(ix) >= st.cadAnnotationAttrs.size())
    return nullptr;
  return &st.cadAnnotationAttrs[static_cast<size_t>(ix)];
}


inline void ClearDimGripInteraction(AppCommandState& st) {
  st.dimGripAnnotationIndex = -1;
  st.dimGripWhich = -1;
  st.dimGripMoveActive = false;
  st.dimGripTextAlongN = 0.f;
  st.dimGripTextAlongT = 0.f;
}


inline void ClearEntityGripInteraction(AppCommandState& st) {
  st.entityGripMoveActive = false;
  st.entityGripEntityIndex = -1;
  st.entityGripWhich = -1;
  st.entityGripDownWorldX = 0.f;
  st.entityGripDownWorldY = 0.f;
  st.entityGripTypedDistanceValid = false;
}

inline void RestoreEntityGripOriginal(AppCommandState& st) {
  if (!st.entityGripMoveActive)
    return;
  const int idx = st.entityGripEntityIndex;
  switch (st.entityGripType) {
  case SelectedEntity::Type::LineSeg: {
    if (idx < 0 || static_cast<size_t>(idx) * 6 + 5 >= st.userLinesFlat.size())
      return;
    const size_t k = static_cast<size_t>(idx) * 6;
    st.userLinesFlat[k] = st.entityGripOrigX0;
    st.userLinesFlat[k + 1] = st.entityGripOrigY0;
    st.userLinesFlat[k + 3] = st.entityGripOrigX1;
    st.userLinesFlat[k + 4] = st.entityGripOrigY1;
    break;
  }
  case SelectedEntity::Type::Circle: {
    if (idx < 0 || static_cast<size_t>(idx) * 4 + 3 >= st.userCirclesCxCyZR.size())
      return;
    const size_t k = static_cast<size_t>(idx) * 4;
    st.userCirclesCxCyZR[k] = st.entityGripOrigCx;
    st.userCirclesCxCyZR[k + 1] = st.entityGripOrigCy;
    st.userCirclesCxCyZR[k + 3] = st.entityGripOrigR;  // [k+2] is Z — a grip drag does not change it
    break;
  }
  case SelectedEntity::Type::Polyline: {
    if (st.entityGripOrigPolylineXIdx < 0)
      return;
    const size_t xIdx = static_cast<size_t>(st.entityGripOrigPolylineXIdx);
    if (xIdx + 1 >= st.userPolylineVerts.size())
      return;
    st.userPolylineVerts[xIdx] = st.entityGripOrigPolyVertX;
    st.userPolylineVerts[xIdx + 1] = st.entityGripOrigPolyVertY;
    break;
  }
  case SelectedEntity::Type::Arc: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.userArcs.size())
      return;
    CadArc& a = st.userArcs[static_cast<size_t>(idx)];
    a.cx = st.entityGripOrigCx;
    a.cy = st.entityGripOrigCy;
    a.r = st.entityGripOrigR;
    a.startRad = st.entityGripOrigStartRad;
    a.sweepRad = st.entityGripOrigSweepRad;
    break;
  }
  case SelectedEntity::Type::Ellipse: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.userEllipses.size())
      return;
    CadEllipse& el = st.userEllipses[static_cast<size_t>(idx)];
    el.cx = st.entityGripOrigEllCx;
    el.cy = st.entityGripOrigEllCy;
    el.majVx = st.entityGripOrigEllMajVx;
    el.majVy = st.entityGripOrigEllMajVy;
    el.ratio = st.entityGripOrigEllRatio;
    break;
  }
  default:
    break;
  }
}

inline void ResetSegmentAngleLock(AppCommandState& st) {
  st.segmentAngleLockActive = false;
  st.segmentLockUx = 1.f;
  st.segmentLockUy = 0.f;
  st.segmentAnglePickPhase = AppCommandState::SegmentAnglePickPhase::Idle;
  st.segmentAngleKeyboardAwaitBearing = false;
}


/// Abort bearing-from-two-points flow (\p AP) without ending LINE/POLYLINE.
void CancelSegmentAnglePick(AppCommandState& st, std::vector<std::string>* log);

/// LINE / POLYLINE (next point): \p A / \p AP / optional \p +delta on same line (° clockwise from north).
bool TryParseSegmentAngleLockCommand(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log);

/// Trim and parse absolute "x,y" / "x y" or relative "@dx,dy" when allowed.
bool ParseStoragePoint(const AppCommandState& st, const std::string& raw, float* lx, float* ly, bool allowRelative,
                       float baseLocalX, float baseLocalY);

bool ParseWorldPoint(const std::string& raw, float* ox, float* oy, bool allowRelative, float baseX, float baseY);

/// Double-precision form, and the real implementation — the float overload above narrows its result.
/// Prefer this wherever the value is destined for the local storage frame: narrowing before the
/// document origin is subtracted quantizes at world magnitude and silently violates REQ-101 (at
/// easting 2e6, `2000000.10` became `2000000.125`). See the definition for the measurement.
bool ParseWorldPointD(const std::string& raw, double* ox, double* oy, bool allowRelative, double baseX,
                      double baseY);

/// If ortho: snaps dx/dy so segment from anchor is horizontal or vertical (CAD-style).
void ApplyOrthoConstrainFromAnchor(float anchorX, float anchorY, float* wx, float* wy, bool ortho);

/// Snap pick onto anchor + t*(ux,uy). Negative \p t allowed unless \p forwardOnly.
void ApplySegmentAngleLockToWorldPick(float anchorX, float anchorY, float lockUx, float lockUy, float* wx, float* wy,
                                      bool forwardOnly);

/// ORTHO axis unit vector from the draft anchor toward the crosshair, converting the world-space crosshair
/// into local storage first (REQ-047). False if the crosshair coincides with the anchor.
/// The pure form lives in `OrthoConstrain.hpp` as \c OrthoUnitTowardPoint (both points in one frame).
bool OrthoUnitTowardUiCursorFromAnchor(const AppCommandState& st, float* ux, float* uy);
/// Trimmed input parses as exactly one float (allows negative).
bool ParseSingleFloatToken(const std::string& raw, float* out);

/// Parse decimal degrees or `NdNmNs` / `NdNm` (e.g. 45d30m10s). Returns false if invalid.
bool ParseAngleDegrees(const std::string& raw, float* degreesOut);

/// App angle convention: **north (+Y) = 0°, clockwise positive** (survey bearing).
float MathAngleRadFromBearingCwNorthDeg(float bearingDegClockwiseFromNorth);
float BearingCwNorthDegFromMathAngleRad(float mathAngleRadFromEastCcw);

/// Rotation (rad, CCW from +X / math \c atan2(dy,dx)) mapping reference segment ref1→ref2 onto new1→new2.
float RotateDeltaFromReferenceAndNewSegment(float refX1, float refY1, float refX2, float refY2,
                                             float newX1, float newY1, float newX2, float newY2);


/// Build the angle-display settings (REQ-021) from the live UNITS state.
[[nodiscard]] inline AngleDisplaySettings CadAngleDisplaySettings(const AppCommandState& st) {
  return AngleDisplaySettings{static_cast<AngleDisplayType>(st.angleDisplayType), st.angleDisplayPrecision,
                              st.angleDisplayClockwise, st.angleDisplayBaseDeg};
}

void StartLineCommand(AppCommandState& st, std::vector<std::string>& log);
void StartCircleCommand(AppCommandState& st, std::vector<std::string>& log);
void StartPolylineCommand(AppCommandState& st, std::vector<std::string>& log);

/// REQ-087: start a feature line — named 3D linework committing to its own store (ADR-035 (g)).
void StartFeatureLineCommand(AppCommandState& st, const std::string& name, std::vector<std::string>& log);

/// Append one vertex to the feature-line draft. \p isElevPoint marks it an elevation point rather
/// than a PI — geometrically it lies on the line either way (ADR-035 (a)).
bool SubmitFeatureLineVertex(AppCommandState& st, float x, float y, bool isElevPoint,
                             std::vector<std::string>& log);

/// Take an X,Y that arrived without an elevation — a viewport click, or a typed `X,Y` — and hold it
/// while FEATURELINE prompts for one (TASK-082). A typed `X,Y,Z` bypasses this and commits directly.
bool SubmitFeatureLinePoint(AppCommandState& st, float x, float y, std::vector<std::string>& log);

/// Resolve the point held by \ref SubmitFeatureLinePoint at elevation \p z and add it to the draft.
void CommitFeatureLinePendingPoint(AppCommandState& st, float z, std::vector<std::string>& log);

/// Commit the feature-line draft into the store as one entity. \p closed joins last vertex to first.
void CommitFeatureLineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log);

// REQ-088 — feature line elevation editing. The table is DERIVED, never stored (ADR-035 (e)); the
// edits write elevations back into the stride-3 vertex array. `flNumber` and `pointNumber` are
// 1-based, matching what FEATURELINELIST and FLELEV print.

/// Build feature line \p fi's elevation table: station, elevation, length ahead, grade back/ahead.
/// False if \p fi is not a feature line with at least two points. Grades are NaN where no such
/// segment exists — see FeatureLineElevRow.
bool BuildFeatureLineElevTable(const AppCommandState& st, int fi, std::vector<FeatureLineElevRow>* out);

bool SetFeatureLinePointElevation(AppCommandState& st, int flNumber, int pointNumber, float elevation,
                                  std::vector<std::string>& log);
/// Sets the grade of the segment AHEAD, which moves the NEXT point and leaves this one alone.
bool SetFeatureLineGradeAhead(AppCommandState& st, int flNumber, int pointNumber, double gradePct,
                              std::vector<std::string>& log);
/// Sets the grade of the segment BEHIND, which moves THIS point and holds the previous one.
bool SetFeatureLineGradeBack(AppCommandState& st, int flNumber, int pointNumber, double gradePct,
                             std::vector<std::string>& log);
/// Raises (or, with a negative delta, lowers) every point of the feature line as a set.
bool RaiseFeatureLineElevations(AppCommandState& st, int flNumber, float delta,
                                std::vector<std::string>& log);
/// Adds an elevation point at plan \p station, interpolating its position so it lies ON the line.
bool InsertFeatureLineElevationPoint(AppCommandState& st, int flNumber, double station, float elevation,
                                     std::vector<std::string>& log);
/// Removes an elevation point. Refuses a PI — that is geometry editing, not an elevation edit.
bool DeleteFeatureLineElevationPoint(AppCommandState& st, int flNumber, int pointNumber,
                                     std::vector<std::string>& log);

/// REQ-085: POLYLINE with per-vertex elevation entry. Shares POLYLINE's draft and `Kind` — the store
/// is already stride-3 XYZ and the two commands differ only in where a vertex's Z comes from.
void StartPolyline3dCommand(AppCommandState& st, std::vector<std::string>& log);
void StartArcCommand(AppCommandState& st, std::vector<std::string>& log);
void StartEllipseCommand(AppCommandState& st, std::vector<std::string>& log);

/// TRIMSTATE (REQ-056): prompt for a new value, echoing the current one.
void StartTrimStateCommand(AppCommandState& st, std::vector<std::string>& log);
/// Validate and apply a TRIMSTATE value (0 or 1). False + a logged message when out of range.
bool ApplyTrimStateValue(AppCommandState& st, int value, std::vector<std::string>& log);

/// REQ-100 frame-budget benchmark. \ref StartFrameBudgetBench installs the bench scene and the
/// scripted orbit; the frame loop drives it and calls \ref FinishFrameBudgetBench, which restores
/// the user's drawing and camera and reports the p95 verdict.
bool StartFrameBudgetBench(AppCommandState& st, int segments, int frames, std::vector<std::string>& log);
void FinishFrameBudgetBench(AppCommandState& st, std::vector<std::string>& log);
/// ELEV — set the work-plane elevation new geometry is drawn at (REQ-058).
void StartElevCommand(AppCommandState& st, std::vector<std::string>& log);
bool ApplyElevValue(AppCommandState& st, double z, std::vector<std::string>& log);
void ApplyUcsWorld(AppCommandState& st, std::vector<std::string>& log);

/// RECT (REQ-053): two opposite corners create an axis-aligned rectangle.
void StartRectCommand(AppCommandState& st, std::vector<std::string>& log);
/// Store the rectangle spanned by the two corners as a 4-vertex closed polyline, ending the command.
/// Degenerate corners (zero width or height) are rejected and the command restarts at the first corner.
void CommitRectangle(AppCommandState& st, float x1, float y1, float x2, float y2, std::vector<std::string>& log);
void StartTextCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMtextCommand(AppCommandState& st, std::vector<std::string>& log);
void OpenMtextRichEditorForPlacement(AppCommandState& st, std::vector<std::string>* log);
void OpenMtextRichEditorForAnnotation(AppCommandState& st, int annIndex, std::vector<std::string>* log);
void OpenPaperTextEditor(AppCommandState& st, int layoutIndex, int textIndex, std::vector<std::string>* log);
void CommitMtextRichEditor(AppCommandState& st, std::vector<std::string>& log);
void CancelMtextRichEditor(AppCommandState& st, std::vector<std::string>* log);
void StartDimAlignedCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDimLinearCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDimAngularCommand(AppCommandState& st, std::vector<std::string>& log);
void StartIdPointCommand(AppCommandState& st, std::vector<std::string>& log);

/// REQ-074: pick a point for its interpolated surface elevation; pick a second for the grade
/// between them. Reports every surface covering the pick, by name.
void StartSurfaceElevGradeCommand(AppCommandState& st, std::vector<std::string>& log);

/// REQ-069: designate one picked Line/Polyline as a breakline on the named surface, appended to its
/// definition by stable entity id. Refuses to start when \p surfaceName does not name an existing
/// surface, reported before the pick rather than after it (REQ-201).
void StartDesignateBreaklineCommand(AppCommandState& st, const std::string& surfaceName,
                                    std::vector<std::string>& log);

/// REQ-069: designate one picked CLOSED Polyline as a boundary ring of kind \p kind on the named
/// surface, applied in the order it was added. Same refuse-before-pick rule as above.
void StartDesignateBoundaryCommand(AppCommandState& st, const std::string& surfaceName, CadBoundaryKind kind,
                                   std::vector<std::string>& log);

void StartSurveyInverseCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMoveCommand(AppCommandState& st, std::vector<std::string>& log);
void StartCopyCommand(AppCommandState& st, std::vector<std::string>& log);
void StartRotateCommand(AppCommandState& st, std::vector<std::string>& log);
void StartScaleCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDeleteCommand(AppCommandState& st, std::vector<std::string>& log);
void StartJoinCommand(AppCommandState& st, std::vector<std::string>& log);
void StartQuickSelectCommand(AppCommandState& st, std::vector<std::string>& log);
void StartTrimCommand(AppCommandState& st, std::vector<std::string>& log);
void StartOffsetCommand(AppCommandState& st, std::vector<std::string>& log);
/// Re-invokes \c st.lastCommand (no-op if \c Kind::None).
void RepeatLastCommand(AppCommandState& st, std::vector<std::string>& log);

/// Removes selected entities from the drawing and clears selection. No-op if selection empty.
void EraseCadAnnotationAtIndex(AppCommandState& st, size_t annIndex);
void DeleteSelectedSurveyPoints(AppCommandState& st, std::vector<std::string>& log);
void SyncSurveyPointLinkedMtextSelection(AppCommandState& st, int surveyPointIndex);
void ApplyLinkedSurveyForAnnotationPick(AppCommandState& st, int annIndex, bool keyShift);
void ExecuteDeleteSelection(AppCommandState& st, std::vector<std::string>& log);
/// Join selected lines / polylines at coincident endpoints into polylines (window-select like DELETE).
void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log);
/// OVERKILL — remove zero-length segments, exact duplicates, collinear overlapping/contiguous lines
/// (merged into one), duplicate circles/arcs, and arcs whose circle matches an existing full circle.
/// Operates on the entire drawing immediately; no selection required.
void ExecuteOverkill(AppCommandState& st, std::vector<std::string>& log);
/// TRIM — pick cutting edges, Enter, trim clicks; or \p L then two points: draws the segment to trim (nearest edge),
/// trims once at nearest crossing (fence disambiguates), then TRIM ends.
bool SubmitTrimViewportPick(AppCommandState& st, float wx, float wy, float tolWorld, std::vector<std::string>& log);
/// Preview for TRIM \p L rubber phase; pass the drawn segment midpoint as \p pickPreview (same side rule as commit).
void CadTrimAppendCutLineRemovedPreview(const AppCommandState& st, float fenceP1x, float fenceP1y, float fenceP2x,
                                        float fenceP2y, float pickPreviewX, float pickPreviewY,
                                        std::vector<float>* previewLinesOut);
/// Closest CAD entity within tolerance (later draw order wins on tie). False if none.
/// \param pickRay When non-null AND valid, entities are measured against this world ray in 3D
///        instead of against \p wx,\p wy in plan (REQ-058). Pass it whenever the camera is
///        orbited: the ray crosses the work plane at one XY and an elevated entity at another, so
///        the plan test would measure to the wrong place. Null (the default) keeps the exact
///        pre-3D behaviour, which is what plan view continues to use.
bool PickClosestCadEntity(const AppCommandState& st, double wx, double wy, float tolWorld, SelectedEntity* out,
                          float* outDistSq, const ray3d::Ray* pickRay = nullptr);
/// True if (x,y) is inside the filled region: inside its outer loop (0) and outside every hole loop (REQ-042).
bool CadFilledRegionContainsPoint(const CadFilledRegion& fr, double x, double y);
/// HATCH command (REQ-043): begin picking an internal point.
void StartHatchCommand(AppCommandState& st, std::vector<std::string>& log);
/// Trace the smallest closed boundary enclosing (wx,wy) from existing model geometry (lines/polylines/arcs/
/// circles/ellipses). Returns the ordered loop (flat local x,y) or false when no closed region contains it.
bool CadHatchTraceAt(const AppCommandState& st, double wx, double wy, std::vector<float>* outLoop);
/// Create a solid filled region from \p loop using the live HATCH appearance; selects it. Caller-agnostic of
/// the undo snapshot? No — this pushes its own snapshot. Returns false if the loop is degenerate.
bool CadHatchCommitLoop(AppCommandState& st, const std::vector<float>& loop, std::vector<std::string>& log);
/// Index of the smallest-area filled region containing (wx,wy), or -1. Lowest pick priority (fills sit under
/// linework) — the click handler calls this only after geometry/annotation picks miss (REQ-042).
int PickFilledRegionAt(const AppCommandState& st, double wx, double wy);
/// World pick tolerance for OFFSET entity selection (geometry scale + screen aperture).
[[nodiscard]] float CadOffsetEntityPickTolWorld(const AppCommandState& st);
/// Tight world pick tolerance for the idle hover highlight: fixed small pixel aperture so the cursor must
/// visually touch the stroke at any zoom level.
[[nodiscard]] float CadHoverEntityPickTolWorld(const AppCommandState& st);
/// Live offset preview from cursor (through mode or typed distance + side); clears vectors first.
void CadOffsetAppendLivePreview(const AppCommandState& cmd, float cursorWx, float cursorWy,
                                std::vector<float>* previewLines, std::vector<float>* previewCircles);

void StartZoomExtentsCommand(AppCommandState& st, std::vector<std::string>& log);
void StartZoomWindowCommand(AppCommandState& st, std::vector<std::string>& log);
/// PAN command (REQ-045): enters interactive pan mode — left-drag pans the active view (hand cursor);
/// Esc / Enter / right-click exits. Reuses the existing middle-drag view-pan math.
void StartPanCommand(AppCommandState& st, std::vector<std::string>& log);
/// ORBIT / Free Orbit (REQ-084 (c)): enters interactive orbit — left-drag tumbles the model view;
/// Esc / Enter / right-click exits. Model space only: a paper sheet is 2D (ADR-025 (g)).
void StartOrbitCommand(AppCommandState& st, std::vector<std::string>& log);

// --- Object isolation (REQ-084 (d) / ADR-034) ----------------------------------------------
/// The attributes of a selected entity, or nullptr for a type that carries none (survey points,
/// PDF underlays) or an index that no longer resolves.
const EntityAttributes* CadEntityAttrsForSelected(const AppCommandState& st, const SelectedEntity& e);
/// True when \p e is currently isolated out. Entity types with no attributes are never hidden.
bool CadSelectedEntityHidden(const AppCommandState& st, const SelectedEntity& e);
/// ISOLATEOBJECTS — hide everything EXCEPT the current selection.
void IsolateSelectedObjects(AppCommandState& st, std::vector<std::string>& log);
/// HIDEOBJECTS — hide the current selection.
void HideSelectedObjects(AppCommandState& st, std::vector<std::string>& log);
/// UNISOLATEOBJECTS — show everything again. Reports when there was nothing hidden (REQ-201).
void EndObjectIsolation(AppCommandState& st, std::vector<std::string>& log);
/// Applies pending zoom-extents or zoom-window requests using current framebuffer size.
void ProcessPendingViewportZoom(AppCommandState& st, double* panX, double* panY, float* zoom, int fbW, int fbH,
                                float viewportAspect, std::vector<std::string>& log);


/// Clears window-selection draft state and CAD entity selection only (not survey point pick).
void ClearCadSelection(AppCommandState& st);
/// Replace selection with all entities of the same kind as the first selected item (or all survey points).
/// Move the armed grip to (x, y) in local storage coordinates — the one place grip geometry is written, so
/// the mouse drag and command-line distance entry cannot drift apart. No-op when no grip is armed.
/// Callers own the undo snapshot and \ref BumpCadGpuCache.
void ApplyEntityGripPoint(AppCommandState& st, float x, float y);

void SelectSimilarToCurrentSelection(AppCommandState& st, std::vector<std::string>* log);
/// Removes all committed CAD lines/circles and clears CAD selection (survey points unchanged).
void ClearCadGeometry(AppCommandState& st);
/// Ends active LINE/CIRCLE/MOVE/etc. draft without logging — used after DXF import.
void ResetCadToolStateToIdle(AppCommandState& st);
void ClearSelection(AppCommandState& st);
/// Toggle survey point in multi-selection (additive unless \p shiftSubtract removes).
/// Survey marker picks: plain click adds an unselected point or, if the point is already selected, reduces the
/// selection to that point only. Shift+click toggles membership (add if absent, remove if present).
void ApplySurveyPointClickSelection(AppCommandState& st, int surveyPointIndex, bool shiftModifier,
                                    std::vector<std::string>* log);
void BeginSelectionBoxCorner(AppCommandState& st, float wx, float wy, float anchorScreenX, float anchorScreenY);

void CancelActiveCommand(AppCommandState& st, std::vector<std::string>& log);
/// Clears Shift+RMB one-shot snap (call on pick submit, cancel, reset, clear geometry).
void ClearPendingOneShotObjectSnap(AppCommandState& st);
/// Called from UI when COPY survey duplicate-ID modal closes (\p applySurveyDup runs duplication).
void ApplyCopySurveyDuplicateModalResult(AppCommandState& st, bool applySurveyDup, std::vector<std::string>& log);

bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);

/// Viewport left-click during active commands.
///
/// \param localX,localY  **LOCAL** storage coordinates, NOT world — `world = local +
///   worldDocumentOrigin`. These parameters were named `worldX`/`worldY` until 2026-08-17 while every
///   caller passed local values, and the values go straight into the flat stores (see
///   `SubmitLineVertex`), so passing a genuine world coordinate here places the geometry a full
///   document origin away. That is the failure the `local-storage` document invariant exists to
///   catch, and it is worth the explicit parameter names because this is the layer's main pick entry
///   point. Pinned by `tests/headless/transcripts/regression-pick-local-coordinates.txt`.
///
///   The UI derives these from the view transform, whose pan is itself local
///   (`ApplyDocumentOriginRebase` shifts `viewportPanX/Y`), and an OSNAP overrides them with a value
///   read directly out of the geometry stores — so a snapped pick is exact and an unsnapped one is
///   bounded by the pixel it came from (REQ-101).
void SubmitViewportPick(AppCommandState& st, float localX, float localY, std::vector<std::string>& log,
                        bool windowSelectionSubtract = false, bool fenceLeftToRightWindowMode = false);

void ProcessCommandLineSubmit(char* cmdBuf, int cmdBufSize, AppCommandState& st, std::vector<std::string>& log);
void StartAlignCommand(AppCommandState& st, std::vector<std::string>& log);
void ExecuteAlignCommand(AppCommandState& st, std::vector<std::string>& log);
/// Recompute Helmert solution from current \ref AppCommandState::alignControlPts into \ref AppCommandState::alignLastResult.
void RecalcAlignResult(AppCommandState& st);
/// Apply the last Helmert solution, generate a report tab, and close the results window.
/// \p applyScale — false strips scale (rotation + translation only; re-derives tx/ty from centroids).
void ApplyAlignCommand(AppCommandState& st, std::vector<std::string>& log, bool applyScale);
void StartPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log);
/// Called from the viewport when the user clicks to place the PDF attachment.
void SubmitPdfAttachInsertPoint(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// Copy currently selected CAD entities into \p st.clipboard.  Clears any previous clipboard content.
void CopySelectionToClipboard(AppCommandState& st, std::vector<std::string>& log);
/// Begin PASTE — show cursor-following preview; next viewport click places the clipboard contents.
void StartPasteCommand(AppCommandState& st, std::vector<std::string>& log);
/// Commit a PASTE: place the clipboard at (x,y) in the ACTIVE space's coordinates (world for model, paper
/// inches for a paper layout). Routes the write + new selection by active space (REQ-038, ADR-013).
void CommitClipboardPasteAt(AppCommandState& st, float x, float y, std::vector<std::string>& log);
/// Immediately paste clipboard contents at their original (stored) coordinates without interaction.
void StartPasteOrigCommand(AppCommandState& st, std::vector<std::string>& log);
/// Release draft cache and preview texture; resets command state to idle.
void CancelPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log);
/// Convert snap-line geometry of the PDF underlay at \p pdfIndex into drawing entities on the current layer.
void VectorizePdfAttachmentLines(AppCommandState& st, int pdfIndex, std::vector<std::string>& log);

std::vector<std::string> FuzzyCommandMatches(const std::string& query, int maxResults);

/// One fuzzy-match entry for the command-line autocomplete UI.
struct CommandSuggestion {
  std::string name;         ///< primary command name, uppercased (e.g. "LINE")
  std::string description;  ///< short human description shown in parentheses
};

/// Ranked fuzzy matches with descriptions, for the nanoCAD-style command picker.
std::vector<CommandSuggestion> FuzzyCommandSuggestions(const std::string& query, int maxResults);

const char* CircleCommandFooterHint(const AppCommandState& st);
const char* ModifyCommandFooterHint(const AppCommandState& st);
const char* RotateCommandFooterHint(const AppCommandState& st);
const char* ScaleCommandFooterHint(const AppCommandState& st);
const char* DeleteCommandFooterHint(const AppCommandState& st);
const char* JoinCommandFooterHint(const AppCommandState& st);
const char* TrimCommandFooterHint(const AppCommandState& st);
const char* OffsetCommandFooterHint(const AppCommandState& st);
const char* AlignCommandFooterHint(const AppCommandState& st);
const char* ZoomCommandFooterHint(const AppCommandState& st);
const char* LineCommandFooterHint(const AppCommandState& st);
const char* DrawingExtrasFooterHint(const AppCommandState& st);

bool ComputeWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY, double* outMxY);
/// Robust extents that drop far-outlier entities (DXFs often contain stray geometry at world (0,0) such as
/// defpoints, block-insert origins, or leftover construction). On success, \p outSkipped is the number of
/// entities discarded; 0 means the answer equals \ref ComputeWorldExtents.
bool ComputeRobustWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY,
                               double* outMxY, int* outSkipped);
void ApplyViewportZoomToWorldRect(double mnX, double mxX, double mnY, double mxY, double* panX, double* panY,
                                  float* zoom, int fbW, int fbH, float viewportAspect);

bool ComputeCircumcircle(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                         float* r);

bool LoadApplicationFont();
