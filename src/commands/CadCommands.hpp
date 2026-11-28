#pragma once

#include <array>
#include <limits>

#include "CadEntities.hpp"
#include "HatchPat.hpp"
#include "engine/CommandSession.hpp"
#include "engine/StepEngine.hpp"
#include "modify/CadModifySessions.hpp"
#include "DrawingContent.hpp"  // ADR-0010: the fields undo frames and tabs share
#include <GoSurvey/Terrain/surfacequery.hpp>  // ISurfaceQuery, for MakeSurfaceQueryForCommands (REQ-497)
#include "GoSurvey/Survey/AlignmentModel.hpp"  // REQ-480 / ADR-075: per-drawing alignment store
#include "GoSurvey/Survey/AlignmentViewFrames.hpp"  // REQ-517: view frame groups that follow an alignment
#include "GoSurvey/Survey/AlignmentSampleLines.hpp"  // REQ-519: sample lines and their sections
#include "GoSurvey/Survey/AlignmentProfile.hpp"  // REQ-521: design vertical profiles along alignments
#include "GoSurvey/Survey/AlignmentDesign.hpp"  // REQ-502: check sets
#include "CadDimGeom.hpp"
#include "EntityId.hpp"
// TextStyles::DefaultTextStyles, for AppCommandState's textStyles seed (issue #57). Pure and
// dependency-free (string/vector/CadEntities.hpp), so this adds no cycle and no weight.
#include "TextStyle.hpp"
// SurfaceStyles::DefaultSurfaceStyles, for AppCommandState's surfaceStyles seed — same reason,
// same shape, and pure for the same reason (REQ-070 / ADR-036 (d)).
#include "SurfaceStyle.hpp"
#include <GoSurvey/Survey/AlignmentStylePublic.hpp>
#include <GoSurvey/Survey/AlignmentLabelSetPublic.hpp>
#include "DimensionStyle.hpp"
#include "MultileaderStyle.hpp"
#include "GoSurvey/Document/cadtablestyle.hpp"
#include <GoSurvey/Renderer/Camera.hpp>  // Commands -> Renderer is a downward dependency (architecture §2)
#include <GoSurvey/Renderer/SectionClip.hpp>  // GL-free; the pick and snap paths honour the live clip (REQ-341)
// The one authoritative WCS <-> UCS implementation (REQ-154). Pure and dependency-free, like
// util/ray3d beside it, so the coordinate-system rules are testable without a window.
#include <GoSurvey/Math/ucs.hpp>
#include <GoSurvey/Survey/CoordinateSystems.hpp>
#include <GoSurvey/Survey/LocalGridTransform.hpp>
// ADR-060 .gscloud out-of-core cache: EXTRACTCENTERLINE (REQ-347) keeps one open cache handle
// across hover frames rather than re-opening it every frame.
#include "GoSurvey/PointCloud/CacheStorage.hpp"
#include "DwgIo.hpp"
#include "GoSurvey/Pdf/PdfAttach.hpp"
#include "ProjectPointRules.hpp"  // projpts::Db + projpts::Rules (REQ-376, REQ-377)
#include "Project.hpp"  // gsproj::Project / LockInfo, for AppCommandState::openProjects (REQ-374)
#include "ProjectPack.hpp"  // gspack::PackPlan, for AppCommandState::projectPackPrompt (REQ-380)
#include "PaperSpace.hpp"
#include "SurveyPointActions.hpp"
#include <GoSurvey/Core/AngleFormat.hpp>
#include "GoSurvey/Survey/TraverseCalc.hpp"
#include "GoSurvey/Survey/TraverseLeastSquares.hpp"
#include "update/UpdateCheck.hpp"  // update::UpdatePrefs only — pure, no network, no <thread>
#include <GoSurvey/Terrain/tinbuild.hpp>       // TinBuildResult, for AppCommandState::SurfaceRebuildAsync (REQ-069)
#include <GoSurvey/Terrain/surfacevolume.hpp>  // SurfaceVolumeResult, for AppCommandState::VolumeDashboardState (REQ-073)
#include <GoSurvey/Terrain/surfacequery.hpp>   // SurfaceProfileSample, Quick Profile (REQ-145)
#include <GoSurvey/Terrain/watershed.hpp>      // WatershedResult, for AppCommandState::SurfaceWatershedCacheEntry (REQ-132)
#include <GoSurvey/Geometry/geom2d.hpp>        // BulgeArc, for the polyline arc-segment grips (REQ-316 / ADR-047)
// HoverDwell, for AppCommandState's surface rollover timer (REQ-089). Pure and dependency-free
// (<cmath>), and deliberately in util/ rather than beside the UI that drives it: the state lives on
// AppCommandState, and Commands may not include a UI header (architecture §11.1).
#include "util/hoverdwell.hpp"
#include "util/hoverpickgate.hpp"  // per-frame viewport hover-pick throttle (GitHub issue #166)
#include "GoSurvey/Document/cadtable.hpp"   // CadTable entity (REQ-148 / D-2026-08-28-i)
#include "GoSurvey/Document/cadtablestyle.hpp"  // table styles and point rows (REQ-474, REQ-475)
#include "GoSurvey/Document/cadblock.hpp"   // Block definitions + INSERT refs (GitHub issue #124)
#include <GoSurvey/Document/EntityRef.hpp>  // EntityKind / EntityRef (ADR-0011 / #1577 phase 1)
#include <GoSurvey/Renderer/CadSolidDisplay.hpp>   // B-rep solids + their tessellation cache (REQ-313 / ADR-045)
#include "util/solidpick.hpp"  // solidpick::Kind, for SelectedSubObject (REQ-318 / ADR-049)
// SectionPlaneExtent / SectionPlaneGrip, for the section plane's stored size and handles
// (REQ-342/339). GL-free and header-only, like every other header in this list — the reason
// `SectionClip.hpp` was written that way (ADR-002).
#include <GoSurvey/Renderer/SectionClip.hpp>
// zoomframing::FrameWorldRect, the one camera-framing implementation behind ZOOMEXTENTS, the REQ-120
// gesture, ZOOM WINDOW and the post-import fit (REQ-122). Pure and dependency-free, like the headers
// above it.
#include "ZoomFraming.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>


struct SelectedEntity {
  enum class Type : std::uint8_t {
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
    Surface = 10,
    /// Drawing TABLE (REQ-148 / D-2026-08-28-i). **Appended** after Surface so existing type values
    /// and switch order stay stable. Editable: MOVE/COPY/ROTATE/SCALE/MIRROR apply to the whole table;
    /// cells are edited in place, not as child entities.
    Table = 11,
    /// Block INSERT (GitHub issue #124). Appended after Table so existing type values stay stable.
    /// Lightweight: transform + attributes; geometry lives on the named definition.
    BlockRef = 12,
    /// B-rep solid (REQ-313 / ADR-045). Appended after BlockRef so existing type values stay stable.
    ///
    /// Every whole-object transform applies to it — MOVE (REQ-322), ROTATE and SCALE (REQ-332),
    /// COPY, MIRROR and both ARRAY forms (REQ-351) — through the kernel (`brep::Translate` /
    /// `Rotate` / `Scale` / `Mirror`), which transforms every vertex, surface frame and edge frame,
    /// never the tessellation alone. STRETCH, which moves part of an object, refuses it by name
    /// (REQ-201): no kernel operation moves part of a solid.
    Solid = 13,
    /// CadPipeRun (issue #486). Appended after Solid so existing type values stay stable. Its
    /// geometry is DERIVED from its path via auto-fillet sweeping (cadpiperun.hpp), so a transform
    /// maps the path's vertices and the pipe is re-derived (REQ-351): the same commands as Solid,
    /// with SCALE scaling the route but never the pipe's nominal size (D-2026-09-28-f). STRETCH
    /// refuses it by name, as it does a solid.
    PipeRun = 14,
    /// Point cloud (REQ-171 / ADR-042). Appended after PipeRun so existing type values stay stable.
    /// **Display-and-erase only, like Mesh** — selects, highlights, erases and reports; never
    /// grip-edited or moved by a transform command (REQ-171's stated boundary).
    PointCloud = 15,
    /// Section plane (REQ-343 amended / ADR-059 (i), GitHub issue #479 acceptance 4/8). Appended
    /// after PointCloud so existing type values stay stable.
    ///
    /// **A type tag, not a member of `AppCommandState::selection`.** The plane stays singular and
    /// its "is it selected" state stays the dedicated `AppCommandState::sectionPlaneSelected` bool
    /// ADR-059 (g)/(h) introduced — `selection` is an indexed vector over per-entity stores
    /// (`e.index` into `cadSolids`, `cadTables`, ...), and there is exactly one section plane, never
    /// zero-or-many, so giving it an `e.index` slot would be inventing storage this feature does not
    /// need. What ADR-059 (h) got wrong was concluding from that shape that the plane could not be
    /// an entity **at all** — it reversed a view-state bool's non-negotiables (no Properties report,
    /// no `internal document payload`, no undo), not its selection storage. This tag exists so the Properties panel and the
    /// transform-refusal doc comments above have a name for what a selected plane IS, the same as
    /// `Solid`/`PointCloud` do; `DrawPropertiesPanel` still branches on `sectionPlaneSelected`
    /// directly rather than scanning `selection` for it. **Display-and-erase-and-slide/flip/resize
    /// only, like Solid/PointCloud/PipeRun**: MOVE/COPY/ROTATE/SCALE/MIRROR/STRETCH/OFFSET/ALIGN
    /// never see it, because it is never in `selection` for those commands to iterate — the plane's
    /// own dedicated grip drag (slide/flip/resize) is the only way it moves, which is what REQ-343
    /// already specifies.
    SectionPlane = 16,
    /// Position Marker (REQ-359 item 3, D-2026-09-29-e). Appended so existing type values stay
    /// stable. MOVE / COPY / ERASE act on marker + label together; ROTATE / SCALE / MIRROR /
    /// STRETCH refuse it by name (its declared capabilities, ADR-0011).
    PositionMarker = 17,
    /// Multileader callout (REQ-367 / issue #619). MOVE / COPY / ERASE act on path + label together;
    /// ROTATE / SCALE / MIRROR transform it like AutoCAD does; STRETCH refuses it by name (ADR-0011).
    Multileader = 18,
    /// Spline (REQ-443 / issue #803): an editable 3D NURBS curve. Selects, erases, moves, copies,
    /// rotates, scales and mirrors; TRIM / EXTEND / OFFSET / FILLET / CHAMFER / JOIN / EXPLODE /
    /// STRETCH refuse it by name (\ref DropSplinesFromSelection).
    Spline = 19,
    /// CAD POINT (REQ-456 / issue #1019). Appended after Spline so existing type values stay stable.
    Point = 20,
    /// Horizontal alignment (REQ-480 / issue #121). Appended after Point so existing values stay stable.
    /// Phase 1: selects, highlights, erases and reports; whole-object transforms arrive with Phase 2 editing.
    Alignment = 21
  };
  Type type = Type::LineSeg;
  int index = 0; ///< Entity index in the parallel container for \p type
};

/// One CAD entity within the pick aperture, with the metrics used to break ties.
struct CadPickCandidate {
  SelectedEntity entity{};
  double distSq = 0.0;
  /// Plan view: Z at the closest point on the entity (higher = nearer the viewer looking down).
  /// Orbited view: ray parameter \p t of closest approach (smaller = nearer the camera).
  double depthKey = 0.0;
};

/// One selected FACE, EDGE or VERTEX of one solid (REQ-318 increment 2 / ADR-049, issue #148).
///
/// **Why this is not a `SelectedEntity::Type`.** A sub-object is not an entity: it has no
/// attributes, no layer, no id, and nothing that consumes `AppCommandState::selection` — MOVE,
/// DELETE, the Properties panel, DXF export, the highlight walk — can act on one until #148's
/// criteria 3-6 land. Folding it in would put a branch for it in every one of those consumers, and
/// the first one that forgot would be a sub-object silently deleted or exported. Its own store makes
/// REQ-318 item 9's "does not interfere" a structural property rather than a promise
/// (D-2026-09-04-a).
///
/// **Why the index alone is not the reference.** ADR-049 measured this: a face index keeps its
/// meaning across an edit that preserves the topology — a box's face indices survive a height
/// change, a length change, a frame translation — and loses it across one that changes the counts,
/// such as a cone frustum collapsing to an apex (4 faces to 3) or any boolean. So the index is
/// paired with the identity of the solid it came from and the reference **expires** rather than
/// re-binding to whatever now occupies that slot. A `weak_ptr` and not a raw pointer for the reason
/// `CadSolidTessellation::key` gives: a raw address can be matched by a new allocation.
struct SelectedSubObject {
  /// Index into \ref AppCommandState::cadSolids. Kept alongside \ref owner because the store is
  /// addressed by index everywhere else in this file; \ref owner is what decides validity.
  int solidIndex = -1;
  solidpick::Kind kind = solidpick::Kind::None;
  /// Index into the owning solid's `faces`, `edges` or `vertices`, per \ref kind.
  int index = -1;
  /// The solid this reference was taken from. Empty (expired) means the solid is gone; pointing at
  /// a *different* solid than `cadSolids[solidIndex]` means it was replaced by an edit, and either
  /// way the reference is dropped rather than followed.
  std::weak_ptr<const brep::Solid> owner;

  [[nodiscard]] bool sameTarget(const SelectedSubObject& o) const {
    return solidIndex == o.solidIndex && kind == o.kind && index == o.index;
  }
};

/// REQ-103 BREAK (step 4): a break point resolved onto a specific entity — the exact coordinate
/// (projected, never the raw pick), plus enough positional context to order it against a second
/// such point on the SAME entity. File-scope (not nested in AppCommandState) so the paper-space
/// free functions below, declared ahead of AppCommandState's own definition, can name it.
/// `param`: Line/open-Polyline/non-full-Arc — distance from the entity's own start, in world
/// units, in its own forward/sweep direction; meaningless for Circle/full-circle Arc, which use
/// `theta` (the raw pick angle) instead for their click-order removal math. `segIndex`: Polyline
/// only — which edge (0-based) the point lands on.
struct BreakPoint {
  float x = 0.f, y = 0.f;
  /// Elevation at the cut, interpolated along the segment the cut fell on.
  ///
  /// A break, fillet or chamfer creates a vertex that did not exist before, so its Z has to come
  /// from somewhere. `param` and `segIndex` already say exactly where along which segment the cut
  /// landed, so the honest answer is the linear interpolation between that segment's endpoints —
  /// exact on a level segment and correct on a sloped one. Before this field existed the answer was
  /// `0.f`, which is what flattened polylines to datum (issue 01).
  float z = 0.f;
  float param = 0.f;
  float theta = 0.f;
  int segIndex = -1;
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


/// Resolve stored color string + transparency to RGBA for viewport/UI (0..1). \p defaultRgb used for ByLayer.
void ResolveStoredColorForViewport(const std::string& colorStorage, float transparency, float defaultR,
                                   float defaultG, float defaultB, float* outRgba);

struct AppCommandState;

/// REQ-308 / D-2026-08-30-a: index of the first real drawing tab. drawingTabs[0] is the Start
/// screen sentinel, so drawing-tab iteration and "select the first drawing" both start here.
inline constexpr int FirstDrawingTabIndex() { return 1; }

const CadLayerRow* FindDrawingLayerRowCi(const AppCommandState& st, const std::string& layerName);

float EffectiveEntityTransparency01(const EntityAttributes& e, const CadLayerRow* layer);

float EffectiveEntityLineweightMm(const EntityAttributes& e, const CadLayerRow* layer);

std::string EffectiveEntityLinetypeNameForViewport(const EntityAttributes& e, const CadLayerRow* layer);

void ResolveEntityRgbaForViewport(const EntityAttributes& attr, const CadLayerRow* layer, float defaultR,
                                  float defaultG, float defaultB, float* outRgba);

/// After entity/layer colour resolution, apply MATERIAL diffuse override for shaded display (REQ-372).
void ApplyMaterialDiffuseForShaded(const EntityAttributes& attr, float rgba[4]);

int CadDxfLineweightEnum370FromMm(float mm);

float CadDxfLineweightMmFromEnum370(int code);


inline void ResolveEntityColorForViewport(const EntityAttributes& attr, float defaultR, float defaultG,
                                          float defaultB, float* outRgba) {
  ResolveEntityRgbaForViewport(attr, nullptr, defaultR, defaultG, defaultB, outRgba);
}


// EntityAttributes and CadAnnotation are defined in CadEntities.hpp (shared with PaperSpace.hpp).
// CadDimAlignedGeometry / CadDimLinearGeometry / CadDimAnyGeometry / CadDimAngularComputeFrame
// live in CadDimGeom.hpp (header-only for issue #110 viewport tests).

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
/// Issue #1676: where a dimension's label is drawn in model space at the current annotation scale —
/// the stored insertion point once the user has placed it, otherwise just beyond the dimension line
/// at the current text height. Grips and label drags start from here.
void CadDimModelSpaceLabelPosition(const AppCommandState& st, const CadAnnotation& a, double* x, double* y);
/// After editing extension points or dimension offset, restore text from fixed (normal, tangent) offsets vs dim mid.
void CadDimAlignedApplyInsFromLocalOffset(CadAnnotation* ann, float alongN, float alongT);
/// Recompute dimension text from current geometry (linear / aligned / angular).
void CadDimRefreshMeasurementText(CadAnnotation* ann, const DimensionStyle& style, const AngleDisplaySettings& angle);

// CadArc and CadEllipse are defined in CadEntities.hpp (shared with PaperSpace.hpp, ADR-013).

/// REQ-103 STRETCH. Recomputes \p arc's center/radius/startRad so it passes through
/// (newStartX,newStartY) and (newEndX,newEndY) while preserving the arc's original signed sweep
/// (included angle + rotational sense) — true AutoCAD-parity arc stretch. Degenerates to a pure
/// translation when both new endpoints are offset from the originals by the same amount. Returns
/// false (arc left unchanged, optional \p log gets a stated reason) if the new chord cannot
/// support the preserved sweep without the radius collapsing toward zero. Not for full-circle-sweep
/// arcs — callers route those through \ref StretchOneArc's center-only rule instead.
///
/// Inline and header-only (pure — only <cmath> and CadArc, both dependency-free, ADR-013) so this,
/// the one genuinely new piece of geometry in TASK-098, is unit-testable (tests/StretchGeomTests.cpp)
/// without linking CadCommands.cpp's whole command layer — the same reasoning `KindName` above and
/// architecture's other pure-math extractions (EntityId.cpp, curveintersect.cpp) already follow.
///
/// Hand-derived: given original endpoints P_start = C + r(cos a, sin a), P_end = C + r(cos(a+s),
/// sin(a+s)) (a=startRad, s=sweepRad), and new (possibly moved) endpoints P_start', P_end', with
/// V' = P_end' - P_start', M' = midpoint(P_start', P_end'), Rot90CW(x,y) = (y,-x):
///   C' = M' - (1/2)*cot(s/2)*Rot90CW(V')
/// preserves s exactly (verified against a worked numeric example before being trusted — see
/// spec/project.md D-2026-08-24-d and tests/StretchGeomTests.cpp's own pinned cases).
inline bool RecomputeArcFromEndpoints(CadArc& arc, float newStartX, float newStartY, float newEndX,
                                      float newEndY, std::vector<std::string>* log) {
  const float s = arc.sweepRad;
  const float halfS = 0.5f * s;
  const float sinHalf = std::sin(halfS);
  if (std::fabs(sinHalf) < 1e-6f) {
    if (log)
      log->push_back("STRETCH — arc endpoint stretch skipped (degenerate sweep); object unchanged.");
    return false;
  }
  const float vx = newEndX - newStartX, vy = newEndY - newStartY;
  const float chordLen = std::hypot(vx, vy);
  if (chordLen < 1e-4f) {
    if (log)
      log->push_back("STRETCH — arc endpoints would coincide; object left unchanged.");
    return false;
  }
  const float mx = 0.5f * (newStartX + newEndX);
  const float my = 0.5f * (newStartY + newEndY);
  const float rvx = vy, rvy = -vx;  // Rot90CW(V')
  const float cotHalf = std::cos(halfS) / sinHalf;
  const float cx = mx - 0.5f * cotHalf * rvx;
  const float cy = my - 0.5f * cotHalf * rvy;
  const float r = std::hypot(newStartX - cx, newStartY - cy);
  if (r < 1e-4f) {
    if (log)
      log->push_back("STRETCH — arc stretch would collapse its radius; object left unchanged.");
    return false;
  }
  arc.cx = cx;
  arc.cy = cy;
  arc.r = r;
  arc.startRad = std::atan2(newStartY - cy, newStartX - cx);
  // arc.sweepRad is unchanged by design — the included angle is preserved exactly.
  return true;
}
/// REQ-103 STRETCH, one arc (model or paper — same \c CadArc struct either way). Tests both
/// endpoints against [mnX,mxX]x[mnY,mxY] independently: 0/2 in-box -> no-op / whole-arc translate
/// (falls out of \ref RecomputeArcFromEndpoints automatically); exactly 1 in-box -> true partial
/// stretch. A full-circle-sweep arc is exempt (its two endpoints coincide) and instead moves as a
/// whole only when its CENTER is in-box, matching the Circle rule. Shared by the model apply, the
/// paper apply, and the live drag preview — three concrete callers.
void StretchOneArc(CadArc& arc, float mnX, float mxX, float mnY, float mxY, float dx, float dy,
                   std::vector<std::string>& log);
/// REQ-103 STRETCH, model-space apply — see the definition's comment (CadCommands.cpp) for the
/// full per-type rule. Declared here (not just in the .cpp) because the viewport-pick and typed-text
/// dispatch sites that call it appear earlier in CadCommands.cpp than its own definition.
void ApplyStretchToSelection(AppCommandState& st, float dx, float dy, float dz, float mnX, float mxX,
                             float mnY, float mxY, bool rectInUcsPlane, std::vector<std::string>& log);

// Optional batched polylines / arcs / ellipses for the viewport (nullptr = none).
struct CadExtendedGeometryInput {
  const std::vector<CadArc>* arcs = nullptr;
  const std::vector<EntityAttributes>* arcAttrs = nullptr;
  /// Plane normals for the circle store, three floats per circle (REQ-312, D-2026-08-31-f).
  ///
  /// An arc carries its normal inside `CadArc`, so `arcs` above needs no companion; a circle
  /// cannot, and the renderer has to know a circle's plane or it draws a tilted one flat. Carried
  /// here rather than as another `RenderScene` parameter for the reason this struct already
  /// states: it is exactly "the extra per-entity data the renderer needs". Null, or shorter than
  /// the circle store, means the missing entries are flat -- which is what every circle that
  /// predates REQ-312 is.
  const std::vector<float>* circleNormals = nullptr;
  const std::vector<CadEllipse>* ellipses = nullptr;
  const std::vector<EntityAttributes>* ellAttrs = nullptr;
  /// REQ-443 splines (NURBS curves), tessellated by the renderer to the view.
  const std::vector<CadSpline>* splines = nullptr;
  const std::vector<EntityAttributes>* splineAttrs = nullptr;
  /// Horizontal alignments (REQ-480/495, issue #121). Drawn from the exact geometry, sampled for display only.
  const std::vector<alignment::Alignment>* alignments = nullptr;
  /// REQ-517: view frame groups, drawn in plan as closed outlines of each frame.
  const std::vector<alignment::ViewFrameGroup>* viewFrameGroups = nullptr;
  /// REQ-519: sample lines, drawn in plan as short swaths across each alignment station.
  const std::vector<alignment::SampleLine>* sampleLines = nullptr;
  /// Named alignment styles (REQ-491); read when drawing each alignment entity.
  const std::vector<AlignmentStyle>* alignmentStyles = nullptr;
  /// Layers frozen in the paper-space viewport being drawn (REQ-028); alignments on them are not drawn (REQ-495.4).
  const std::vector<std::string>* viewportFrozenLayers = nullptr;
  /// REQ-456 CAD POINT entities.
  const std::vector<CadPoint>* points = nullptr;
  const std::vector<EntityAttributes>* pointAttrs = nullptr;
  int pdMode = 0;
  double pdSize = 0.0;
  const std::vector<double>* polylineVerts = nullptr;
  const std::vector<int>* polylineOffsets = nullptr;
  const std::vector<uint8_t>* polylineClosed = nullptr;
  const std::vector<EntityAttributes>* polylineAttrs = nullptr;
  /// REQ-316 / ADR-047: per-vertex bulge (parallel to polylineVerts, one per vertex). Null or
  /// empty means every polyline segment is straight — the pre-ADR-047 behaviour.
  const std::vector<float>* polylineBulge = nullptr;
  /// REQ-325 / ADR-053: per-vertex curve-plane normal (stride 3, parallel to polylineVerts). Null
  /// or empty means every curved segment is flat (world +Z) — the pre-ADR-053 behaviour, and what
  /// every polyline that predates it still is.
  const std::vector<float>* polylineNormal = nullptr;
  // Feature lines (REQ-087). Same four arrays, same shape — the renderer draws both through one
  // function, so a feature line cannot render differently from a polyline by accident.
  const std::vector<double>* featureLineVerts = nullptr;
  const std::vector<int>* featureLineOffsets = nullptr;
  const std::vector<uint8_t>* featureLineClosed = nullptr;
  const std::vector<EntityAttributes>* featureLineAttrs = nullptr;
  const std::vector<CadLayerRow>* drawingLayers = nullptr;
  /// Sorted stable entity ids hidden by object isolation (REQ-084 (d), ADR-034); nullptr or empty
  /// means nothing is hidden. Carried here rather than as another `RenderScene` parameter — that
  /// signature is already long, and this struct is exactly "the extra per-entity data the renderer
  /// needs", which is what the hidden set is.
  const std::vector<std::uint64_t>* hiddenEntityIds = nullptr;
  const std::vector<CadBlockDefinition>* blockDefs = nullptr;
  const std::vector<CadBlockRef>* blockRefs = nullptr;
  const std::vector<EntityAttributes>* blockRefAttrs = nullptr;
  /// Issue #622: model-tab GL block inserts scale about the insertion point when a layout viewport is
  /// current (same rule as the paper-space overlay). Null viewport or non-positive MUP = no scaling.
  const Viewport* annotativeViewport = nullptr;
  float drawingModelUnitsPerPlottedInch = 0.f;
  const std::vector<CadAnnotationScale>* annotationScales = nullptr;
  int currentAnnotationScaleIndex = -1;
  /// Patterned hatch linework, parallel to the filled regions handed to the renderer (REQ-043).
  const CadHatchDisplayGeometry* hatchDisplay = nullptr;
};

/// True when a CSR chain store (polylines, feature lines) holds at least one entity.
///
/// `offsets` is CSR: N entities need N+1 offsets, so fewer than two offsets is zero entities — and
/// an "empty" store is legitimately either `{}` or `{0}` (issue #60), which is exactly why this is a
/// named predicate rather than an `!empty()` written out at each call site.
template <class VT>
[[nodiscard]] inline bool CadChainHasEntities(const std::vector<VT>* verts,
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
/// a test that fails when someone forgets. REQ-087. REQ-443 splines and REQ-456 points belong here
/// for the same reason: a drawing that holds only that kind must still enter the draw block.
[[nodiscard]] inline bool CadExtendedHasDrawableGeometry(const CadExtendedGeometryInput& e) {
  if (e.arcs != nullptr && !e.arcs->empty())
    return true;
  if (e.ellipses != nullptr && !e.ellipses->empty())
    return true;
  if (CadChainHasEntities(e.polylineVerts, e.polylineOffsets))
    return true;
  if (CadChainHasEntities(e.featureLineVerts, e.featureLineOffsets))
    return true;
  if (e.blockRefs != nullptr && !e.blockRefs->empty())
    return true;
  if (e.splines != nullptr && !e.splines->empty())
    return true;
  if (e.alignments != nullptr && !e.alignments->empty())
    return true;
  if (e.viewFrameGroups != nullptr && !e.viewFrameGroups->empty())
    return true;
  if (e.sampleLines != nullptr && !e.sampleLines->empty())
    return true;
  if (e.points != nullptr && !e.points->empty())
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
  // Issue #1676: a dimension's model-unit text override is the same size at every scale.
  if (std::isfinite(a.dimTextHeightModel) && a.dimTextHeightModel > 0.0 &&
      (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear ||
       a.kind == CadAnnotation::Kind::DimAngular))
    return static_cast<float>(a.dimTextHeightModel);
  return a.plottedHeightInches * std::max(modelUnitsPerPlottedInch, 1.e-6f);
}

/// Visible text footprint using the frontend's font measurement when available.
/// \p measure false skips the renderer measurement and returns the cheap estimated quad, for rejecting
/// far-away text before paying for a Qt layout (hover picks run this for every annotation).
void CadAnnotationTextPickQuad(const AppCommandState& st, const CadAnnotation& a, float* xs, float* ys,
                               bool measure = true);
/// Frame scale an MTEXT is drawn at in model space (issue #1667): the annotation-scale factor for an
/// annotative MTEXT, else 1. See CadMtextDisplayFactor.
float CadMtextModelDisplayFactor(const AppCommandState& st, const CadAnnotation& a);
/// The four corners of MTEXT \p a as drawn in model space — top-left, top-right, bottom-right,
/// bottom-left — rotated with the text and scaled for an annotative MTEXT. Grips and outlines use these.
void CadMtextDrawnCorners(const AppCommandState& st, const CadAnnotation& a, float xs[4], float ys[4]);
/// MTEXT auto-height (issue #1667): the height of \p a's laid-out content at its frame width, in the stored
/// frame's units (the drawing scale). Uses AppCommandState::measureMtextHeight when \p useFrontend and it is
/// installed, else a word-wrap estimate. -1 when it cannot be measured.
float CadMtextContentHeight(const AppCommandState& st, const CadAnnotation& a, bool useFrontend = true);
/// Sets \p a's frame height to its content height, keeping the insertion point and width, as AutoCAD
/// sizes an MTEXT. Survey-point labels and non-MTEXT are untouched.
void CadMtextFitHeight(const AppCommandState& st, CadAnnotation& a, bool useFrontend = true);
/// True for an MTEXT edited through its frame grips. A survey-point label keeps its own box grips.
[[nodiscard]] inline bool CadMtextHasFrameGrips(const CadAnnotation& a) {
  return a.kind == CadAnnotation::Kind::Mtext && a.surveyPointLabelForId < 0;
}
/// The grips of a frame-gripped MTEXT: the four drawn corners (as \ref CadMtextDrawnCorners), then the
/// insertion point at index 4. Viewport grips, grip hit-testing and grip snapping all use these.
void CadMtextGripPoints(const AppCommandState& st, const CadAnnotation& a, float xs[5], float ys[5]);
/// Grip resize (issue #1667): the drawn corner opposite the dragged one stays at (\p fixedX, \p fixedY)
/// and the dragged one follows (\p cursorX, \p cursorY), measured along the text's own axes, so a
/// rotated MTEXT keeps its rotation and attachment. \p displayFactor comes from CadMtextModelDisplayFactor.
void CadMtextResizeFromGrip(CadAnnotation& a, float displayFactor, float fixedX, float fixedY, float cursorX,
                            float cursorY);
/// Axis-aligned rough bounds for hit-testing / zoom (TEXT uses estimated glyph width).
void CadAnnotationRoughBounds(const CadAnnotation& a, float modelUnitsPerPlottedInch, float* outMnX, float* outMnY,
                              float* outMxX, float* outMxY);

/// Top-most annotation under point; -1 if none. Uses pixel tolerance from viewport half-height.
/// \param ray When non-null (an orbited view), each annotation is tested where the camera ray meets
///        ITS OWN plane (z = insZ) rather than at \p wx, \p wy on the work plane (GitHub issue #564
///        §2). Null keeps the plan-view test exactly.
int PickCadAnnotationAt(float wx, float wy, const AppCommandState& cmd, float orthoHalfHeightWorld,
                        float viewportHeightPx, const ray3d::Ray* ray = nullptr);
int PickCadTableAt(float wx, float wy, const AppCommandState& cmd, float orthoHalfHeightWorld,
                   float viewportHeightPx);
void OpenTableCellEditor(AppCommandState& st, int tableIndex, int cellIndex);
void CommitTableCellEditor(AppCommandState& st, std::vector<std::string>& log);
void CancelTableCellEditor(AppCommandState& st);
void MigrateLegacyAnnotationTables(AppCommandState& st);

/// ROTATE live preview angle (rad) about the base point: the angle a pick at the cursor would commit
/// (RotateSession::onViewportPick), measured in the work plane.
bool CadRotatePreviewTheta(const AppCommandState& cmd, float curX, float curY, double* outThetaRad);
/// \c Kind::Scale, \c modifyPhase == NeedDestination — live scale from cursor (see \ref AppCommandState::scalePhase).
bool CadScalePreviewFactor(const AppCommandState& cmd, float curX, float curY, float* outScale);

/// The annotations a pick at the cursor would leave under the active MOVE/COPY, PASTE, ARRAY, SCALE,
/// MIRROR or ROTATE step: ghost text for the viewport overlay, from the same scratch copy the
/// wireframe preview draws (\ref BuildCadTransformPreviewScratch, issue #1700).
void CadAnnotationCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,
                                           std::vector<CadAnnotation>* out);


/// In-process clipboard for COPYCLIP / PASTECLIP.  Geometry stored at local (storage) coordinates.
struct CadClipboard {
  double basePtX = 0.0; ///< Bounding-box center X used as paste anchor (local space).
  double basePtY = 0.0;

  /// The copied entities and the definitions their block references depend on, in the shape a
  /// drawing holds them (ADR-0010 / #1557 phase 2). A copy from paper space stores z = 0 in
  /// `userCirclesCxCyZR`, and a paste into
  /// paper space drops z: the sheet is 2D (ADR-025 (g)), so Z collapses at that boundary.
  /// `userCircleNormals` (REQ-312) flattens to world +Z at the same boundary. A copy does not carry
  /// `userPolylineVertsNormal` (REQ-325) yet, so a pasted bulged segment on a tilted plane comes back flat.
  DrawingContent content;
  /// Source space of the copy. Text height has different units per space (model: plotted-inches × scale = model
  /// units; paper: paper inches), so a cross-space paste scales annotation height by modelUnitsPerPlottedInch.
  bool fromPaper = false;

  /// REQ-383 clauses 1 and 3: where the copy came from. A paste into a drawing of another project, or
  /// of another coordinate system / unit, is checked against these (CheckClipboardPaste).
  uint32_t    srcProjectUid = 0;        ///< 0 = a standalone drawing
  std::string srcProjectName;
  std::string srcProjectFolder;         ///< UTF-8; tells apart two open projects that share a name (issue #726)
  std::string srcZone;                 ///< CS-MAP code of the source drawing; empty = none
  double      srcMetersPerUnit = 0.0;   ///< the source drawing's unit in meters; 0 = unknown
  double      srcOriginX = 0.0, srcOriginY = 0.0;  ///< the source drawing's document origin: world = local + origin

  bool empty() const {
    // Only the entity fields count. Keep this
    // list in step with the entity fields of DrawingContent, or a new kind will look empty.
    return content.userLinesFlat.empty() && content.userCirclesCxCyZR.empty() && content.userArcs.empty() &&
           content.userEllipses.empty() && (content.userPolylineOffsets.size() <= 1) && content.cadSplines.empty() &&
           content.cadPoints.empty() && content.cadAnnotations.empty() && content.cadTables.empty() &&
           content.cadBlockRefs.empty() && content.cadFilledRegions.empty() && content.surveyPoints.empty();
  }
};

/// REQ-316 / ADR-047: keep the parallel per-vertex polyline bulge array the right length for the
/// vertex list (3 floats per vertex). Entries added here default to 0 (a straight segment). Call
/// after any operation that changes a polyline's vertex count without maintaining bulges itself.
inline void SyncPolylineBulge(std::vector<float>& bulge, std::size_t vertsFloatCount) {
  bulge.resize(vertsFloatCount / 3, 0.0f);
}

/// REQ-325 / ADR-053: keep the parallel per-vertex polyline curve-plane-normal array the right
/// length for the vertex list (stride 3, one normal per vertex). New entries default to world +Z
/// (a flat/straight segment's normal is never consulted, but a uniform default keeps every entry
/// a valid unit vector for docinvariants). Called at every site \ref SyncPolylineBulge already is —
/// the two arrays' lengths must never drift apart, the same reason bulge itself is synced there.
inline void SyncPolylineNormal(std::vector<float>& normal, std::size_t vertsFloatCount) {
  const std::size_t n = vertsFloatCount / 3;
  const std::size_t oldN = normal.size() / 3;
  normal.resize(n * 3);
  for (std::size_t i = oldN; i < n; ++i) {
    normal[i * 3] = 0.f;
    normal[i * 3 + 1] = 0.f;
    normal[i * 3 + 2] = 1.f;
  }
}

/// REQ-316 / ADR-047: grip index base for a polyline ARC segment's midpoint (bulge) grip. Vertex
/// grips are `0..vertexCount-1`; a bulge grip for segment `s` is `kPolyBulgeGripBase + s`. The base
/// is far above any realistic vertex count so the two grip families never collide.
inline constexpr int kPolyBulgeGripBase = 1 << 20;


struct ProjectSettings;  // ProjectSettings.hpp (REQ-375)
struct AddDrawingPlan;   // ProjectAddFlow.hpp (REQ-378)

/// One undo/redo frame: the drawing content plus the face-derived section plane. PDF glTexId is
/// zeroed to avoid stale GPU references. The block editor (CadBlocks.cpp, ADR-043) also uses one to
/// stash the model drawing while a block definition is open.
struct DrawingGeometrySnapshot {
  DrawingContent content;
  /// The section plane (REQ-343 amended, GitHub issue #479 acceptance 8). Mirrors
  /// `AppCommandState::viewportSectionClip`/`Offset`/`Flip`/`FrameValid`/`Frame`/`Extent` — moved
  /// into the undo-tracked snapshot (and, via `GsIo.cpp`, into `internal document payload`) so creating, deleting, sliding,
  /// flipping and resizing the plane are all undoable, and the plane survives a save/reload. Local
  /// coordinates, exactly like every other entity here (see "Local storage invariant").
  bool                sectionPlaneActive = false;
  bool                sectionPlaneFrameValid = false;
  ucs::Ucs            sectionPlaneFrame{};
  double              sectionPlaneOffset = 0.0;
  bool                sectionPlaneFlip = false;
  SectionPlaneExtent  sectionPlaneExtent{};
  std::string description;
};


/// How many previous UCSs `UCS Previous` can step back through.
///
/// Bounded rather than unbounded because the stack is pushed on every UCS change and would
/// otherwise grow for the life of the session. Ten matches the depth AutoCAD documents.
constexpr size_t kUcsPreviousDepth = 10;

/// Snapshot of all per-drawing data.  AppCommandState holds the live (active-tab) copy directly;
/// switching tabs saves the active fields here and restores the target tab's snapshot.
struct DrawingDocument {
  int currentViewportZoomScaleIndex = -1;  ///< Tab-only: which zoom preset is showing (the list is drawing content).
  int currentAnnotationScaleIndex = -1;    ///< Tab-only: which annotation scale is showing (the list is drawing content).
  double viewportPanX = 0.0;
  double viewportPanY = 0.0;
  float  viewportZoom = 1.f;
  double viewportPanZ = 0.0;          ///< Camera target elevation per tab (REQ-058).
  float  viewportAzimuthDeg = 0.f;    ///< Camera orientation per tab (REQ-058); plan view by default.
  float  viewportElevationDeg = 90.f;
  float  viewportRollDeg = 0.f;       ///< Screen roll per tab (#153); nonzero only after PLAN of a tilted UCS.
  /// Projection per tab (REQ-309). Saved and restored with the orientation above, so switching
  /// tabs cannot carry one drawing's projection into another's.
  Camera::Projection viewportProjection = Camera::Projection::Orthographic;
  float  viewportFovDeg = kDefaultFovDeg;
  /// The section clip is per TAB (REQ-341, D-2026-09-16-b): a clip set in one drawing must not hide
  /// half of another when the user switches to it, and switching back restores it. A fresh document
  /// starts with it off, which is what keeps NEW and OPEN clean. Still never written to `internal document payload`.
  bool   viewportSectionClip = false;
  double viewportSectionClipOffset = 0.0;
  bool   viewportSectionClipFlip = false;
  /// The face-derived section plane (REQ-343 amended) is per tab: it is not undone (only the undo
  /// frame's copy of it is), so the live plane is carried here like the clip above.
  bool viewportSectionClipFrameValid = false;
  ucs::Ucs viewportSectionClipFrame{};
  SectionPlaneExtent viewportSectionClipExtent{};
  /// The UCS is per drawing, not per session (REQ-154): switching tabs must not carry one drawing's
  /// coordinate frame into another's, which is the "UCS state does not leak between viewports"
  /// condition in as strong a form as a one-model-view-per-tab application can state it.
  ucs::Ucs activeUcs;
  std::vector<ucs::Ucs> ucsPrevious;
  std::string activeViewName;
  bool ucsFollow = false;
  /// Per-drawing entity-id counter (REQ-076). Saved/restored with the tab so two open drawings
  /// number independently; see AppCommandState::nextEntityId for why undo never rewinds it.
  std::uint64_t nextEntityId = 1;

  /// The drawing content (ADR-0010), saved and restored whole on a tab switch.
  DrawingContent content;

  /// Tab-only: the DWG import's lights and sun, carried per drawing and never undone.
  std::vector<CadDwgImportedLight> dwgImportedLights;
  CadDwgImportedSun dwgImportedSun{};
  bool dwgImportedSunPresent = false;
  CadDwgImportedLightList dwgImportedLightList{};
  bool dwgImportedLightListPresent = false;
  std::string dwgImportedLightListDictKey;
  /// Tab-only: the block library's recent and favourite names, per drawing.
  std::vector<std::string> blockRecent;
  std::vector<std::string> blockFavorites;

  std::vector<int>              selectedSurveyPointIndices;
  std::vector<SelectedEntity>   selection;
  /// Object isolation (REQ-084 (d)). PER TAB, and stored here for the same reason `selection` is:
  /// entity ids are unique **within a drawing**, so carrying one tab's hidden set into another
  /// would hide whatever objects happened to draw those numbers there. Never written to `internal document payload` —
  /// this struct is the in-memory tab store, not the file format.
  std::vector<std::uint64_t>    hiddenEntityIds;
  int                           activeSpaceIndex = kModelSpaceIndex;  ///< -1 = model; else index into paperLayouts.
  uint32_t cadGpuRevision  = 0;
  uint32_t savedRevision   = 0;   ///< cadGpuRevision at last save; != cadGpuRevision means unsaved changes.
  std::string filePath;           ///< Absolute path to the .dwg drawing file, empty if never saved.
  std::vector<DrawingGeometrySnapshot> undoStack;
  std::vector<DrawingGeometrySnapshot> redoStack;
};

/// Copy the active per-drawing fields from \p cmd into \p cmd.documents[idx].
void SaveDocumentToSnapshot(AppCommandState& cmd, int idx);
/// Copy \p cmd.documents[idx] back into the active fields of \p cmd.  Cancels any in-progress command.
void RestoreDocumentFromSnapshot(AppCommandState& cmd, int idx);

/// Active text style (REQ-044): the entry in \c st.drawing.textStyles named by \c st.drawing.activeTextStyleName, falling
/// back to "Standard"; nullptr only if the table is somehow empty. Used by the dropdown and the create path.
const TextStyle* ActiveTextStyle(const AppCommandState& st);
/// Set the active text style and sync the new-text default height to it, so newly drawn TEXT/MTEXT adopt
/// the style's height through the existing height plumbing (bake-on-write — ADR-020).
void SetActiveTextStyle(AppCommandState& st, const std::string& name);

#include "paperspace/CadPaperSpaceCommands.hpp"

/// REQ-302: top-level ribbon tabs, in display order. UI-layer concept, but the active index is
/// persisted app state (AppCommandState::activeRibbonTab) so it lives alongside kModelSpaceIndex-
/// style constants rather than in the UI layer, which Commands must not depend on.
constexpr int kRibbonTabHome     = 0;
constexpr int kRibbonTabInsert   = 1;
constexpr int kRibbonTabAnnotate = 2;
constexpr int kRibbonTabView     = 3;
constexpr int kRibbonTabManage   = 4;
constexpr int kRibbonTabOutput   = 5;
constexpr int kRibbonTabSurvey   = 6;
/// REQ-355 (issue #564 section 8): the solid-modelling and piping commands. A saved slot like the
/// six before it, so it takes the next index and the session-only contextual tabs move up behind it.
constexpr int kRibbonTabModeling = 7;
constexpr int kRibbonTabCount    = 8;
/// REQ-143: contextual TIN Surface tab. Not counted in \c kRibbonTabCount and not written to prefs.
constexpr int kRibbonTabSurfaceCtx = 8;
/// REQ-153: contextual SURVEY Point(s) tab. Session-only, not a prefs slot.
constexpr int kRibbonTabSurveyPointCtx = 9;
/// Contextual Block Editor tab while BEDIT is open. Not counted in \c kRibbonTabCount / prefs.
constexpr int kRibbonTabBlockEditor = 10;
/// REQ-171 (part 14): contextual Point Cloud tab. Not counted in kRibbonTabCount / prefs.
constexpr int kRibbonTabPointCloudCtx = 11;
/// REQ-359: contextual Geolocation tab, shown while the drawing is geolocated. Session-only, not a
/// prefs slot; appended after the other contextual tabs and never auto-selected.
constexpr int kRibbonTabGeolocationCtx = 12;

/// REQ-171 point-cloud vertex colour source, chosen by the user (session-global — see
/// `AppCommandState::pointCloudDisplay`).
enum class PointCloudColorScheme : std::uint8_t { Rgb = 0, Solid = 1, Elevation = 2, Intensity = 3 };

/// REQ-171 point-cloud display settings (session-global, not per-cloud — `CadPointCloud`'s payload
/// is immutable, architecture §11.5, so mutable display prefs live here instead, same reasoning as
/// `SurfaceStyle` living apart from `CadSurface`).
struct PointCloudDisplaySettings {
  float pointSizePx = 2.0f;                 // was the hardcoded glPointSize(2.0f) in ViewportRenderer
  int lodTargetPoints = 800000;             // was the file-local kTargetLodPoints constant
  PointCloudColorScheme colorScheme = PointCloudColorScheme::Rgb;
};

/// What the gizmo DOES — chosen by the user, unlike \ref CadGizmoMode which is derived (REQ-060
/// rotate/scale, TASK-232).
///
/// **Stored, and that does not contradict the note above.** `CadGizmoMode` is derived because the
/// selection already determines it, so a stored copy would be a third thing that could disagree with
/// the two selections. Nothing in a selection says whether the user wants to MOVE, TURN or RESIZE
/// it: the operation has no derivation to disagree with, so it is a setting. Different question,
/// different answer.
enum class CadGizmoOp : std::uint8_t {
  /// Three handles along the active UCS, committing through `ApplyTranslationToSelection`.
  Translate,
  /// ONE ring, about the active UCS Z, committing through \ref ApplyRotationAboutUcsZ.
  ///
  /// One and not three because **typed ROTATE is UCS-Z-only** — REQ-329 says so in as many words
  /// ("a full ROTATE3D is a separate future issue"). REQ-060 requires a handle to agree with the
  /// equivalent typed command, and rings on UCS X and Y would have no such command to agree with.
  Rotate,
  /// ONE handle, uniform, committing through \ref ApplyUniformScaleAboutBase.
  ///
  /// One and not three because typed SCALE is uniform on every axis (REQ-329) and `brep::Scale` is
  /// uniform because the representation has no ellipsoid to hold an unevenly scaled sphere
  /// (REQ-332 item 7). A per-axis handle would advertise a shape the program cannot store.
  Scale,
};

/// REQ-383 clause 6: a project whose point database changes could not be written, and the drawings open in it.
struct UnsavedProject {
  std::string              name;
  std::vector<std::string> drawings;
};

struct AppCommandState {
  DrawingContent drawing;  // saved drawing content (ADR-0010); members are reached as drawing.<member>

  /// Seeds the tables a live drawing always has. `DrawingContent` leaves them empty because undo
  /// frames and stored tabs started empty before ADR-0010 (issue #1557 phase 1a is a pure move).
  ///
  /// Layer "0" always exists, **including before anything has been loaded** (issue #57): the
  /// loader used to synthesize it while a newly created drawing had an empty table, so a new
  /// drawing was briefly in a state the rest of the code is entitled to assume cannot happen — and
  /// `save -> load -> save` was not byte-identical, breaking REQ-079's first acceptance condition.
  /// Seeding here rather than at each creation site means every route to a drawing (File > New,
  /// the headless driver, an importer, a test) gets it, with no site left to forget. Safe against
  /// loading, which `clear()`s this table before repopulating it (GsIo.cpp). The "Standard" text
  /// style (REQ-044 / ADR-020) and surface style (REQ-070 / ADR-036 (d)) had the same defect and
  /// have the same fix; the alignment style and label tables follow the same rule.
  AppCommandState() {
    drawing.drawingLayerTable = DefaultDrawingLayerTable();
    drawing.textStyles = TextStyles::DefaultTextStyles();
    drawing.surfaceStyles = SurfaceStyles::DefaultSurfaceStyles();
    drawing.alignmentStyles = AlignmentStyles::DefaultAlignmentStyles();
    drawing.alignmentLabelSets = AlignmentLabelSets::DefaultAlignmentLabelSets();
    drawing.alignmentLabelStyles = AlignmentLabelStyles::DefaultAlignmentLabelStyles();
  }

  using Kind = CadCommandKind;
  Kind active = Kind::None;
  /// ADR-0012: Active command session holding the command's own phase and scratch state.
  /// Dropped automatically on completion, Esc, or tool reset.
  std::unique_ptr<stepengine::CommandSession> activeSession;

  template <typename T>
  [[nodiscard]] T* sessionAs() {
    return activeSession ? dynamic_cast<T*>(activeSession.get()) : nullptr;
  }
  template <typename T>
  [[nodiscard]] const T* sessionAs() const {
    return activeSession ? dynamic_cast<const T*>(activeSession.get()) : nullptr;
  }

  static const char* KindName(Kind k) {
    switch (k) {
    case Kind::Line:          return "LINE";
    case Kind::Circle:        return "CIRCLE";
    case Kind::Polyline:      return "POLYLINE";
    case Kind::FeatureLine:   return "FEATURELINE";
    case Kind::Grading:       return "GRADING";
    case Kind::Arc:           return "ARC";
    case Kind::Ellipse:       return "ELLIPSE";
    case Kind::Spline:        return "SPLINE";
    case Kind::Point:         return "POINT";
    case Kind::Leader:        return "LEADER";
    case Kind::Pedit:         return "PEDIT";
    case Kind::Measure:       return "MEASURE";
    case Kind::Divide:        return "DIVIDE";
    case Kind::Area:          return "AREA";
    case Kind::List:          return "LIST";
    case Kind::DataExtract:   return "DATAEXTRACTION";
    case Kind::Table:         return "TABLE";
    case Kind::PointTable:    return "ADDPOINTTABLE";
    case Kind::StationOffsetPick: return "GETSTATIONOFFSET";
    case Kind::Text:          return "TEXT";
    case Kind::Mtext:         return "MTEXT";
    case Kind::Mleader:          return "MLEADER";
    case Kind::MleaderAddLeader: return "MLEADERADD";
    case Kind::DimAligned:    return "DIMALIGNED";
    case Kind::DimLinear:     return "DIMLINEAR";
    case Kind::DimAngular:    return "DIMANGULAR";
    case Kind::Move:          return "MOVE";
    case Kind::Copy:          return "COPY";
    case Kind::Rotate:        return "ROTATE";
    case Kind::Scale:         return "SCALE";
    case Kind::Array:         return "ARRAY";
    case Kind::Mirror:        return "MIRROR";
    case Kind::Lengthen:      return "LENGTHEN";
    case Kind::Extend:        return "EXTEND";
    case Kind::Break:         return "BREAK";
    case Kind::Stretch:       return "STRETCH";
    case Kind::Fillet:        return "FILLET";
    case Kind::Chamfer:       return "CHAMFER";
    case Kind::Delete:        return "DELETE";
    case Kind::Zoom:          return "ZOOM";
    case Kind::Join:          return "JOIN";
    case Kind::Trim:          return "TRIM";
    case Kind::Offset:        return "OFFSET";
    case Kind::IdPoint:       return "ID";
    case Kind::SurveyInverse: return "INVERSE";
    case Kind::Dist:          return "DIST";
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
    case Kind::SectionClip:   return "SECTIONCLIP";
    case Kind::SectionPlane:  return "SECTIONPLANE";
    case Kind::Orbit:         return "ORBIT";
    case Kind::Ucs:           return "UCS";
    case Kind::Plan:          return "PLAN";
    case Kind::SurfaceElevGrade:   return "SURFELEV";
    case Kind::WaterDrop:          return "WATERDROP";
    case Kind::Catchment:          return "CATCHMENT";
    case Kind::SwapTinEdge:        return "SURFSWAPEDGE";
    case Kind::AddTinPoint:        return "SURFACEADDPOINT";
    case Kind::DelTinPoint:        return "SURFACEDELPOINT";
    case Kind::MoveTinPoint:       return "SURFACEMOVEPOINT";
    case Kind::DelTinLine:         return "SURFDELLINE";
    case Kind::QuickProfile:       return "QUICKPROFILE";
    case Kind::DesignateBreakline: return "DESIGNATEBREAKLINE";
    case Kind::DesignateBoundary:  return "DESIGNATEBOUNDARY";
    case Kind::InsertBlock:        return "INSERT";
    case Kind::Solid:              return "SOLID";  // REQ-313: one Kind, all seven primitives
    case Kind::Extrude:           return "EXTRUDE";
    case Kind::Revolve:          return "REVOLVE";
    case Kind::Slice:            return "SLICE";
    case Kind::Section:          return "SECTION";
    case Kind::Loft:             return "LOFT";
    case Kind::Sweep:            return "SWEEP";
    case Kind::Boolean:          return "BOOLEAN";
    case Kind::Polysolid:          return "POLYSOLID";  // REQ-317
    case Kind::Revcloud:           return "REVCLOUD";   // REQ-444
    case Kind::PressPull:          return "PRESSPULL";
    case Kind::BConnectMode:       return "BCONNECTMODE";
    case Kind::BConnect:           return "BCONNECT";
    case Kind::BConnectEdit:       return "BCONNECTEDIT";
    case Kind::BlockFitting:       return "BLOCKFITTING";
    case Kind::PipeRun:            return "PIPERUN";
    case Kind::PipeFit:            return "PIPEFIT";
    case Kind::PipeSplit:          return "PIPESPLIT";
    case Kind::ExtractCenterline:  return "EXTRACTCENTERLINE";
    case Kind::Move3d:             return "3DMOVE";
    case Kind::Rotate3d:           return "3DROTATE";
    case Kind::Scale3d:            return "3DSCALE";
    case Kind::ChProp:             return "CHPROP";
    case Kind::MatchProp:          return "MATCHPROP";
    case Kind::LayMCur:            return "LAYMCUR";
    case Kind::GeoMarkPoint:       return "GEOMARKPOINT";
    case Kind::GeoMarkLatLong:     return "GEOMARKLATLONG";
    case Kind::GeoReorientMarker:  return "GEOREORIENTMARKER";
    case Kind::DrawingSettingsPick: return "DRAWINGSETTINGS";
    case Kind::GeoCaptureArea:     return "GEOCAPTUREAREA";
    case Kind::PadSolid:           return "PADSOLID";
    case Kind::AlignmentLayout:    return "ALIGNMENTLAYOUT";
    case Kind::AlignmentFromObjects: return "CREATEALIGNMENTFROMOBJECTS";
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

  /// Index into \ref annotationScales (drawing content) for model-space annotative display (-1 = drawing
  /// plot scale only). Tab-only: the list is drawing content, the selection is per tab.
  int currentAnnotationScaleIndex = -1;
  /// Index into \ref viewportZoomScales (drawing content). Tab-only, like the annotation index.
  int currentViewportZoomScaleIndex = -1;
  /// REQ-385: LIGHT entities and SUN object from DWG import (issue #624 lights slice).
  std::vector<CadDwgImportedLight> dwgImportedLights;
  CadDwgImportedSun dwgImportedSun{};
  bool dwgImportedSunPresent = false;
  /// REQ-386: LIGHTLIST registry from DWG import (issue #715).
  CadDwgImportedLightList dwgImportedLightList{};
  bool dwgImportedLightListPresent = false;
  std::string dwgImportedLightListDictKey;
  /// Drawing Settings window open (REQ-357). Session-only.
  bool showDrawingSettingsWindow = false;
  /// Survey point X marker: horizontal span on paper (inches) → world half-extent = 0.5 × span × MUP (not zoom).
  float surveyPointCrossSpanPlottedInches = 0.14f;
  bool surveyPointShowIdInViewport = false;
  /// Decimal places shown for survey-point coordinates (labels on the drawing
  /// and the survey points table/editor). Display only — stored values keep full
  /// precision. Configured in Settings → User Preferences → Survey points.
  int surveyPointDisplayPrecision = 4;
  /// Plotted text height (inches) for survey point ID labels when \ref surveyPointShowIdInViewport is true.
  float surveyPointLabelPlottedHeightInches = 0.10f;
  // Frontend-provided natural text measurement. Session-only; no UI toolkit types
  // cross into Commands. A null hook retains the legacy/headless measurement path.
  void (*measureAnnotationText)(float, const std::string&, const std::string&, float*, float*) = nullptr;
  /// MTEXT content height (pixels) for (text px, wire, font family, wrap width px); see CadMtextFitHeight.
  float (*measureMtextHeight)(float, const std::string&, const std::string&, float) = nullptr;
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
  /// Throttle for the per-frame hover pick above (GitHub issue #166). The pick is a full entity
  /// scan; it runs both when idle and during TRIM/EXTEND/BREAK/LENGTHEN entity selection (REQ-056).
  /// This gate lets the call site reuse the previous result while the cursor, view and geometry are
  /// unchanged, and caps the re-run rate while the cursor sweeps. See `util/hoverpickgate.hpp`.
  HoverPickGate viewportHoverPickGate{};
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
  /// snapped to — widened to `double` with the stores (ADR-054 Phase C, #442) so that identity holds
  /// at any drawing magnitude; a `float` copy here broke it above ~10,000 ft local.
  double viewportSnapPickLocalX = 0.0;
  double viewportSnapPickLocalY = 0.0;
  /// Elevation of the snapped point. An object snap yields the object's ACTUAL 3D point, so it
  /// overrides the current work-plane elevation — snapping to the end of a line on the datum while
  /// ELEV is 5 must give you that endpoint, not a point 5 above it (AutoCAD-faithful, REQ-058).
  /// Only meaningful while \ref viewportSnapPickValid.
  double viewportSnapPickLocalZ = 0.0;
  /// WHICH snap answered, as a `CadSnap::Kind` (REQ-344 amended). Only meaningful while
  /// \ref viewportSnapPickValid.
  ///
  /// Stored as an `int` because `CadSnap::Kind` lives in a Viewport header and Commands may not
  /// include one (architecture §11.1) — the same reason `objectSnapKindOverrideKind` beside it is
  /// an `int`. Added because a caller can need to know not just WHERE the snap is but whether it is
  /// a **named feature** or one of the "anywhere on the object" family: a section-plane drag
  /// honours the first and must ignore the second, or `Face` — which answers at essentially every
  /// cursor position on a solid — drags the plane across the model continuously.
  ///
  /// **-1 means "no kind was recorded", and is the value every frame starts at.** Three separate
  /// places set \ref viewportSnapPickValid and only one of them knows a `CadSnap::Kind` — the grip
  /// magnet sets the flag from a 2D grip position with no kind and no Z at all. `Kind::Endpoint` is
  /// 0, so a default-initialised field reads as a named feature and would let one of those drive an
  /// absolute plane placement through a point the user never aimed at. Starting at -1 makes the
  /// omission fail closed instead: a consumer that needs the kind checks for it.
  int viewportSnapPickKind = -1;
  /// Command-line log cache for the selectable read-only multiline (rebuilt each frame from \ref log).
  std::vector<char> commandLogCacheBytes;
  size_t commandLogLastSizeForAutoscroll = 0;

  // --- Floating command bar / fading history / F2 console (REQ-040) ---
  bool cmdLineClassicDock = false;    ///< true → legacy docked panel; false → floating bar (default). Persisted.
  bool cmdBarVisible = true;          ///< floating bar shown; × hides, Ctrl+9 restores. Persisted.
  bool cmdBarAnchorValid = false;     ///< false → place at the default bottom-left this frame. Persisted.
  float cmdBarAnchorX = 0.f;          ///< persisted floating-bar bottom-LEFT x anchor (screen px); Y is pinned to the bottom.
  float cmdBarTopYPx = 0.f;           ///< floating bar's top edge this frame (screen px); 0 = not floating/not drawn. NOT persisted — recomputed every frame.
  float cmdBarAnchorY = 0.f;          ///< (legacy/unused: the bar is always pinned to the viewport bottom).
  float cmdBarWidth = 0.f;            ///< user-resized bar width (px); 0 → default. Persisted.
  float cmdConsoleHeight = 0.f;       ///< user-resized F2 console height (px); 0 → default. Persisted.
  bool cmdConsoleOpen = false;        ///< F2 expanded console (not persisted).
  /// Frames left to force-scroll the F2 console to its newest line. >1 because the console's
  /// InputTextMultiline is a child window whose ScrollMax reflects last frame's content size —
  /// on the very frame the log grows (or the console first opens), asking to scroll to FLT_MAX
  /// clamps against the STALE (too-small) ScrollMax and lands short of the true bottom. Counting
  /// down over 2 frames covers that one-frame lag once the child's content size has caught up.
  int cmdConsoleScrollFramesRemaining = 0;
  float cmdBarFadeDelaySec = 4.f;     ///< idle seconds before recent-history lines start fading. Persisted.
  float cmdBarOpacity = 0.92f;        ///< bar / console background opacity. Persisted.
  int cmdBarHistoryLines = 3;         ///< recent log lines floated above the bar. Persisted.
  size_t cmdLogLastSizeForFade = 0;   ///< log size seen at the last fade-timer reset.
  std::vector<std::string> cmdEnteredHistory;  ///< recently submitted command-line entries (newest last; capped). Runtime only.
  /// Last Drawing1 viewport metrics (match survey MTEXT box to on-screen font scaling).
  float viewportLastSurveyLayoutOrthoHalfH = 50.f;
  float viewportLastSurveyLayoutHeightPx = 600.f;
  /// Pixel size of the model viewport; the Qt viewport publishes it every paint. Imports fit to it.
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

    /// Solids in the B-rep profile (REQ-313 / REQ-100). 0 = not the solid profile; at most one
    /// of this, \ref meshTriangleCount and \ref surfacePointCount is non-zero.
    ///
    /// A profile of its OWN rather than an assumption that the mesh number covers it, for the
    /// same reason the surface profile is not implied by the mesh one: a solid's cost is not one
    /// big indexed upload. It is N stream-uploaded batches plus N edge batches plus a per-frame
    /// cache lookup, and #120's "do not regenerate a solid's render mesh every frame" is a claim
    /// about exactly that per-frame work. Measuring it is the only way to know.
    int solidCount = 0;
    /// Total tessellated triangles across the solid scene, filled in when the scene is built.
    int solidTriangleCount = 0;
    /// REQ-495 acceptance 4: alignments in the alignment profile, each `alignmentEntityCount` entities long.
    /// 0 = not the alignment profile.
    int alignmentCount = 0;
    int alignmentEntityCount = 0;

    /// REQ-100 profile (e) (TASK-270 §14): non-empty selects the point-cloud profile. Unlike every
    /// other profile, the scene is not synthesized — it is the real file at this path, imported
    /// through the normal async `POINTCLOUDATTACH` path, exactly as REQ-100 (e) requires ("the
    /// user's own driving E57"). At most one of this, \ref solidCount, \ref meshTriangleCount and
    /// \ref surfacePointCount is non-zero/non-empty.
    std::string pointCloudPath;
    /// True from the moment `BENCH POINTCLOUD <path>` starts the import until `TickPointCloudImport`
    /// reaps it — the bench scene/orbit installs only once the import lands, since there is no
    /// scene to frame or measure before the cloud exists.
    bool pointCloudImportPending = false;
    /// `PointCloudDisplaySettings::lodTargetPoints` at the moment the run starts — report-only,
    /// read back because REQ-100 (e) budgets "points submitted to the GPU in one frame," not points
    /// in the file, and the LOD target is what actually bounds that submission.
    std::int64_t pointCloudLodTargetPoints = 0;
    /// Frames (of the TIMED portion) in which the renderer issued at least one synchronous leaf
    /// disk read — REQ-100 (e)'s "stall" obligation: reported even when p95 passes, since a stall
    /// is a separate failure mode from a slow median frame.
    int pointCloudDiskStallFrames = 0;
    /// Running max of `ViewportRenderer::PointCloudResidentLeafBytes()`, sampled once per timed
    /// frame — REQ-100 (e)'s "peak resident node-cache memory."
    std::size_t pointCloudPeakResidentLeafBytes = 0;

    std::vector<double> savedPolyVerts;
    std::vector<int> savedPolyOffsets;
    std::vector<std::uint8_t> savedPolyClosed;
    std::vector<EntityAttributes> savedPolyAttrs;
    std::vector<CadSurface> savedSurfaces;            ///< restored verbatim after a surface run
    std::vector<EntityAttributes> savedSurfaceAttrs;
    std::vector<std::shared_ptr<const CadMesh>> savedMeshes;  ///< restored verbatim after a mesh run
    std::vector<EntityAttributes> savedMeshAttrs;
    std::vector<CadSolidPtr> savedSolids;             ///< restored verbatim after a solid run
    std::vector<EntityAttributes> savedSolidAttrs;
    std::vector<alignment::Alignment> savedAlignments;  ///< restored verbatim after an alignment run
    std::vector<alignment::SampleLine> savedSampleLines;  ///< REQ-519: restored with the alignments they belong to
    /// The mesh profile forces Shaded (REQ-100 (b) measures *shaded* meshes, and REQ-064's budget
    /// condition is stated in Shaded). Saved so a bench run cannot leave the user in a style they
    /// did not choose — which would also silently invalidate ADR-026 (e)'s 2D Wireframe parity.
    VisualStyle savedVisualStyle = VisualStyle::Wireframe2D;
    float savedAzimuthDeg = 0.f;
    float savedElevationDeg = 90.f;
    float savedRollDeg = 0.f;  // #153
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
  /// Snap to the four "compass" points of a circle / arc — active-UCS X/Y projected onto the
  /// curve plane (REQ-330). AutoCAD `QUA`. Default OFF, matching AutoCAD's OSMODE (Quadrant is
  /// not one of the default running object snaps); reachable immediately via the Shift+right-click
  /// "snap once" override.
  bool objectSnapQuadrant = false;
  /// Snap to a typed displayed station on an alignment (REQ-490). Default OFF; `STATION 12+45.32`
  /// at a point prompt also resolves directly. While enabled, uses \ref objectSnapStationDisplayed when valid.
  bool objectSnapStation = false;
  bool objectSnapNode = true;  ///< REQ-490.2: drawing points and alignment PIs.
  bool objectSnapStationDisplayedValid = false;
  double objectSnapStationDisplayed = 0.0;
  /// Set when a `STATION …` point entry fails; cleared after the active command logs it (REQ-490).
  std::string alignmentStationSnapRejectReason;
  bool objectSnapPerpendicular = true;
  bool objectSnapSurveyPoint = true;
  bool objectSnapGeometricCenter = true;
  /// Snap where two objects genuinely meet in 3D (REQ-062).
  bool objectSnapIntersection = true;
  /// Snap where two objects only *appear* to meet in the current view (REQ-062). Off by default,
  /// as in AutoCAD: it fires on objects that do not touch, which is surprising unless asked for.
  bool objectSnapApparentIntersection = false;
  bool objectSnapSurface = true;
  /// Snap to the nearest REAL point of a resident point cloud (REQ-171/172, REQ-348). Default OFF
  /// (D-2026-09-18) — a dense cloud competing with every other running snap was judged more
  /// disruptive than useful by default, the same call REQ-330 made for Quadrant; reachable
  /// immediately via the Shift+right-click "snap once" override.
  bool objectSnapPointCloud = false;
  /// --- 3D Object Snap (REQ-325/#395, supersedes REQ-301) ---------------------------------------
  /// AutoCAD's "3D Object Snap" tab is a SEPARATE system from the 2D Object Snap above: its own
  /// master toggle (F4, independent of F3's `objectSnapEnabled`) and its own six per-mode toggles.
  /// REQ-301 previously argued a single `objectSnapSolid` toggle was correct because "no requirement
  /// asks to enable one [face/edge] without the other" — #395 is exactly that requirement, adding
  /// four more AutoCAD-parity modes besides, so the single toggle is retired (D-2026-09-07-a) in
  /// favor of one flag per mode. Solid Vertex/Midpoint-on-edge snapping used to ride on the 2D
  /// Endpoint/Midpoint toggles; they now live here exclusively, so 3D Object Snap really is
  /// independent of 2D Object Snap.
  bool objectSnap3dEnabled = true;        ///< Master toggle, F4.
  bool objectSnap3dVertex = true;         ///< A solid's topology vertices (was objectSnapEndpoint).
  bool objectSnap3dMidpointEdge = true;   ///< An edge's midpoint (was objectSnapMidpoint).
  bool objectSnap3dNearestFace = true;    ///< Nearest point on a face under the cursor (was objectSnapSolid/Face).
  bool objectSnap3dCenterFace = false;    ///< Centroid of a face — every SurfaceKind, including Nurbs.
  bool objectSnap3dKnot = false;          ///< A NURBS (freeform) face's knot points.
  bool objectSnap3dPerpendicular = false; ///< Foot of the perpendicular from a command reference onto a planar face.
  /// Isolines drawn around a curved solid face, per full turn (AutoCAD calls this ISOLINES).
  /// Per drawing, persisted in `internal document payload` and in user preferences like every other display setting.
  int viewportSolidIsolines = kSolidDefaultIsolines;
  /// Screen-space aperture (pixels) for object snap tolerance and related viewport picks.
  float objectSnapAperturePx = 14.f;
  /// Half-size in screen pixels for green object-snap glyphs (square / triangle / circle overlay).
  float objectSnapGlyphHalfPx = 15.f;
  /// Half-size in screen pixels for grip squares drawn on selected entities.
  float gripSizePx = 4.f;
  /// Shift+RMB snap-override menu (issue #103): once a kind is chosen, FindBest is restricted to
  /// candidates of ONLY this kind — ignoring the persistent per-type OSNAP toggles, since the whole
  /// point of the override is to reach a kind the user does not keep enabled generally — for the
  /// next viewport hover/pick, then cleared on submit / cancel (ClearPendingOneShotObjectSnap).
  bool objectSnapKindOverrideValid = false;
  /// \c static_cast<\ref CadSnap::Kind>.
  int objectSnapKindOverrideKind = 0;

  /// Applied on the next viewport zoom processing step (needs framebuffer size).
  bool pendingZoomExtents = false;
  bool pendingZoomWindow = false;
  /// REQ-399: bring the main GoSurvey window to the foreground on the next frame (the main loop owns
  /// the main window handle, so this is a request rather than a direct call from the PDF viewer window).
  bool requestMainWindowFocus = false;
  float pendingZoomMnX = 0.f;
  float pendingZoomMxX = 0.f;
  float pendingZoomMnY = 0.f;
  float pendingZoomMxY = 0.f;

#include "draw/CadDrawStateFields.hpp"

  // Solid command phase/transient state lives in stepengine::*Session (src/commands/solid/CadSolidSessions.hpp).

#include "pipe/CadPipeRevcloudStateFields.hpp"

#include "grading/CadGradingTrimStateFields.hpp"
  /// REQ-077: update-check settings (enabled, channel, skipped version, throttle anchor).
  /// Only the persisted settings live here — the in-flight worker state is `update::UpdateState`,
  /// owned by the application loop, so `AppCommandState` gains no thread and stays copyable.
  update::UpdatePrefs updatePrefs;
#include "alignment/CadAlignmentStateFields.hpp"

#include "connect/CadBlockConnectStateFields.hpp"

#include "surface/CadSurfaceStateFields.hpp"

  // --- Selection (idle box pick + move/copy/rotate) ---
  std::vector<SelectedEntity> selection;

  /// Selected faces / edges / vertices of solids (REQ-318 increment 2, issue #148).
  ///
  /// **Mutually exclusive with \ref selection**, by decision D-2026-09-04-a: a plain click clears
  /// this, a `Ctrl` click clears that. Never both at once, which is what makes #148's "sub-object
  /// selection does not interfere with whole-entity selection" hold without every consumer of
  /// \ref selection having to know this exists.
  ///
  /// **Session state, like \ref hiddenEntityIds** — not written to `internal document payload` and not carried in an undo
  /// snapshot. A selection is not part of the drawing, and a reference that expires on a topology
  /// change (\ref SelectedSubObject) would be meaningless after a reload anyway.
  std::vector<SelectedSubObject> subObjectSelection;

  /// What a `Ctrl` click WOULD take, under the cursor right now (REQ-318 item 14). Refreshed behind
  /// the same gate as the entity hover pick, and by the same query the click uses — so what lights
  /// up is what selects, by construction rather than by two code paths agreeing.
  bool subObjectHoverValid = false;
  SelectedSubObject subObjectHover;
  /// Rest timer for the rollover readout, exactly as REQ-089's surface readout uses one: the
  /// pre-highlight is immediate, the panel waits for the cursor to settle. A readout that tracks the
  /// cursor continuously covers the geometry being picked.
  HoverDwell subObjectHoverDwell{};

#include "gizmo/CadGizmoGeoPropStateFields.hpp"

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

  // Polyline: moved vertex index (into vertex accessors, not the flat array).
  int entityGripOrigPolylineXIdx = -1;
  float entityGripOrigPolyVertX = 0.f, entityGripOrigPolyVertY = 0.f;
  /// REQ-316 / ADR-047: for an arc-segment (bulge) grip drag — the global vertex index whose bulge
  /// is being dragged, and its value before the drag, so RMB / Esc restores it.
  int entityGripOrigPolyBulgeVi = -1;
  float entityGripOrigPolyBulge = 0.f;

  // Ellipse originals.
  float entityGripOrigEllMajVx = 0.f, entityGripOrigEllMajVy = 0.f;
  float entityGripOrigEllRatio = 0.f;

  /// REQ-443: the whole spline as it was when a grip drag began, so RMB / Esc restores it exactly (a Fit
  /// spline's control points are re-derived on every drag step, so there is no single coordinate to put back).
  CadSpline entityGripOrigSpline;
  alignment::Alignment entityGripOrigAlignment;  ///< REQ-487.1: the alignment as it was when the grip was grabbed
  /// REQ-492.5: a station label being dragged in the viewport. The label is moved live; the original is kept so a
  /// cancel restores it and a commit can take its undo snapshot before the move.
  bool alignLabelDragActive = false;
  bool alignLabelDragMoved = false;
  int alignLabelDragAlignment = -1;
  int alignLabelDragIndex = -1;
  alignment::StationOffsetLabel alignLabelDragOriginal;
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

  // --- Grip hover menu (REQ-356): Lengthen/Stretch for a line-endpoint grip ---
  /// Set when the drag was armed via the hover menu's "Lengthen" item rather than a plain click-drag:
  /// the live update and the typed-distance box both measure along \c entityGripLengthenDir instead of
  /// free cursor position / ORTHO, so the endpoint can only move along the line's own original angle.
  bool entityGripLengthenMode = false;
  /// Unit vector from the FIXED endpoint toward the grip being dragged, at the moment "Lengthen" was
  /// chosen — the line's own angle, frozen for the rest of this drag.
  float entityGripLengthenDirX = 0.f;
  float entityGripLengthenDirY = 0.f;
  /// Dwell timer + identity for the grip hover menu: which selected line-endpoint grip (if any) the
  /// cursor is currently resting on, used to open the Lengthen/Stretch popup after ~0.5s and to reset
  /// the timer the instant the hovered grip changes (REQ-356).
  HoverDwell gripMenuHoverDwell{};
  int gripMenuHoverEntityIndex = -1;
  int gripMenuHoverWhich = -1;
  /// The grip the popup is open for (or was last open for) — latched separately from the hover above
  /// so the popup's own content doesn't vanish the instant the cursor leaves the grip to reach the menu.
  int gripMenuPendingEntityIndex = -1;
  int gripMenuPendingWhich = -1;

#include "modify/CadModifyStateFields.hpp"

#include "inquiry/CadSurveyInquiryStateFields.hpp"

#include "blocks/CadBlockSessionStateFields.hpp"

#include "paperspace/CadPaperSpaceStateFields.hpp"
};

/// ADR-054 (a)/(b): coordinate stores are `double` until the single authorized narrowing point at
/// GPU vertex-buffer assembly (ViewportRenderer). Checked on `DrawingContent`, the one definition the
/// live state, the undo snapshot and the per-tab document share (ADR-0010), so a reintroduced `float`
/// is a compile error, not a silent ±0.008 ft regression (TASK-228 Phase D).
#define GOSURVEY_STATIC_ASSERT_DOUBLE_STORE(Type, Field)                                            \
  static_assert(std::is_same_v<decltype(Type::Field)::value_type, double>,                          \
                #Type "::" #Field " must stay double (ADR-054 (a), REQ-101)")
GOSURVEY_STATIC_ASSERT_DOUBLE_STORE(DrawingContent, userLinesFlat);
GOSURVEY_STATIC_ASSERT_DOUBLE_STORE(DrawingContent, userCirclesCxCyZR);
GOSURVEY_STATIC_ASSERT_DOUBLE_STORE(DrawingContent, userPolylineVerts);
#undef GOSURVEY_STATIC_ASSERT_DOUBLE_STORE

inline float DefaultAnnotationTextHeightWorld(const AppCommandState& st) {
  return st.drawing.defaultPlottedTextHeightInches * st.drawing.modelUnitsPerPlottedInch;
}

/// REQ-316 / ADR-047: call `fn(seg, midX, midY, midZ)` for every ARC segment of polyline `pi`, with
/// the midpoint at the arc's apex (the point at half sweep). `seg` is the 0-based segment index —
/// the same one `kPolyBulgeGripBase + seg` encodes. Straight segments are skipped. Shared by the
/// four grip sites (model + floating-viewport, draw + grab) so they cannot disagree.
template <class F>
inline void CadForEachPolylineArcMidGrip(const AppCommandState& st, int pi, F&& fn) {
  const int np = st.drawing.userPolylineOffsets.size() > 0 ? static_cast<int>(st.drawing.userPolylineOffsets.size() - 1) : 0;
  if (pi < 0 || pi >= np)
    return;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<std::size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[static_cast<std::size_t>(pi) + 1];
  const bool closed = static_cast<std::size_t>(pi) < st.drawing.userPolylineClosed.size() &&
                      st.drawing.userPolylineClosed[static_cast<std::size_t>(pi)];
  const int nseg = (v1 - v0) - 1 + (closed && (v1 - v0) >= 2 ? 1 : 0);
  for (int s = 0; s < nseg; ++s) {
    const int va = v0 + s;
    const int vb = (s == (v1 - v0) - 1) ? v0 : v0 + s + 1;  // wrap on the closing segment
    if (static_cast<std::size_t>(vb) * 3 + 2 >= st.drawing.userPolylineVerts.size())
      break;
    const float bulge = static_cast<std::size_t>(va) < st.drawing.userPolylineVertsBulge.size()
                            ? st.drawing.userPolylineVertsBulge[static_cast<std::size_t>(va)]
                            : 0.f;
    if (bulge == 0.f)
      continue;
    const std::size_t A = static_cast<std::size_t>(va), B = static_cast<std::size_t>(vb);
    const BulgeArcSpan arc = BulgeArc(st.drawing.vertex(A).x, st.drawing.vertex(A).y,
                                      st.drawing.vertex(B).x, st.drawing.vertex(B).y,
                                      static_cast<double>(bulge));
    if (!arc.valid)
      continue;
    const double mid = arc.startAngle + arc.sweep * 0.5;
    fn(s, static_cast<float>(arc.cx + arc.radius * std::cos(mid)),
       static_cast<float>(arc.cy + arc.radius * std::sin(mid)), st.drawing.vertex(A).z);
  }
}

#include "view/CadViewCommands.hpp"


// EntityKind and EntityRef live in <GoSurvey/Document/EntityRef.hpp> (ADR-0011 / #1577 phase 1).

/// Assign a stable id to every entity that lacks one (REQ-076 / ADR-027).
///
/// **Idempotent**: an entity that already has an id keeps it, always. Only `id == 0` is filled, from
/// \ref AppCommandState::nextEntityId, in a fixed array order — which is what makes assignment
/// deterministic for a legacy `internal document payload` (same file, same ids, every load).
///
/// Deliberately called only at **cold boundaries** — before an undo snapshot, before a `internal document payload` save,
/// and before a reference is taken — never per frame (architecture §11.7). Assigning at the 127
/// sites that construct an EntityAttributes was rejected: a missed site there is not a compile
/// error, it is a silently id-less entity (the ADR-025 (a) lesson).
#include "surface/CadSurfaceCommands.hpp"
#include "solid/CadSolidCommands.hpp"

#include "revcloud/CadRevcloudCommands.hpp"

#include "pipe/CadPipeCommands.hpp"



#include "pointcloud/CadPointCloudCommands.hpp"


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

/// Capture / restore the whole model-geometry set as a value (same fields the undo stack uses).
/// Public so the block editor (ADR-043) can stash the drawing while a definition is edited in place.
[[nodiscard]] DrawingGeometrySnapshot CadCaptureGeometrySnapshot(const AppCommandState& st,
                                                                 const std::string& description);
void CadRestoreGeometrySnapshot(AppCommandState& st, const DrawingGeometrySnapshot& snap);
[[nodiscard]] std::size_t CadActiveUndoStackSize(const AppCommandState& st);
void CadTruncateActiveUndoStack(AppCommandState& st, std::size_t n);
/// Undo: restore previous geometry snapshot; push current to redo stack. Returns true if an undo was performed.
bool DoUndo(AppCommandState& st, std::vector<std::string>& log);
/// Redo: restore next geometry snapshot; push current to undo stack. Returns true if a redo was performed.
bool DoRedo(AppCommandState& st, std::vector<std::string>& log);
/// True if the active tab has at least one undo frame available.
bool CanUndo(const AppCommandState& st);
/// True if the active tab has at least one redo frame available.
bool CanRedo(const AppCommandState& st);


inline void BumpCadGpuCache(AppCommandState& st) {
  ++st.cadGpuRevision;
  ++st.cadGpuCacheRevision;
}
inline void BumpCadGpuCacheOnly(AppCommandState& st) { ++st.cadGpuCacheRevision; }

#include "modify/CadModifyRestSessions.hpp"
#include "draw/CadDrawSessions.hpp"
#include "solid/CadSolidSessions.hpp"
#include "pipe/CadPipeSiteSessions.hpp"
#include "surface/CadSurfaceSessions.hpp"
#include "alignment/CadAlignmentSessions.hpp"
#include "ucs/CadUcsViewSessions.hpp"
#include "gizmo/CadGizmoGeoPropSessions.hpp"
#include "blocks/CadBlocksSessions.hpp"
#include "misc/CadMiscSessions.hpp"

/// Keeps per-entity attribute vectors sized to match geometry counts (used by Properties and select-similar).
void EnsureAttrCounts(AppCommandState& st);

#include "layers/CadLayerStyleCommands.hpp"

#include "annotation/CadAnnotationCommands.hpp"


#include "selection/CadSelectionCommands.hpp"

#include "input/CadInputParsing.hpp"

void StartLineCommand(AppCommandState& st, std::vector<std::string>& log);
void StartCircleCommand(AppCommandState& st, std::vector<std::string>& log);
void StartPolylineCommand(AppCommandState& st, std::vector<std::string>& log);

#include "featureline/CadFeatureLineCommands.hpp"

/// REQ-085: POLYLINE with per-vertex elevation entry. Shares POLYLINE's draft and `Kind` — the store
/// is already stride-3 XYZ and the two commands differ only in where a vertex's Z comes from.
void StartPolyline3dCommand(AppCommandState& st, std::vector<std::string>& log);
/// REQ-398 — GRADING: side slopes from a feature line baseline out to a surface.
void StartGradingCommand(AppCommandState& st, std::vector<std::string>& log);
void StartArcCommand(AppCommandState& st, std::vector<std::string>& log);
void StartEllipseCommand(AppCommandState& st, std::vector<std::string>& log);
void StartSplineCommand(AppCommandState& st, std::vector<std::string>& log);
void StartPointCommand(AppCommandState& st, std::vector<std::string>& log);
void StartLeaderCommand(AppCommandState& st, std::vector<std::string>& log);
void StartPeditCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMeasureCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDivideCommand(AppCommandState& st, std::vector<std::string>& log);
void StartAreaCommand(AppCommandState& st, std::vector<std::string>& log);
void StartListCommand(AppCommandState& st, std::vector<std::string>& log);
/// DATAEXTRACTION (REQ-467, issue #804): selection, typed columns, then a spreadsheet file.
void StartDataExtractCommand(AppCommandState& st, std::vector<std::string>& log);
/// TABLE (REQ-466): typed style, columns and rows, then the insertion point.
void StartTableCommand(AppCommandState& st, std::vector<std::string>& log);
/// ADDPOINTTABLE (REQ-474): opens the point table dialog in the Qt editor, or runs typed prompts.
void StartAddPointTableCommand(AppCommandState& st, std::vector<std::string>& log);
/// Continues ADDPOINTTABLE with points chosen in the dialog; returns false (and logs why) when none apply.
bool BeginAddPointTableWithPoints(AppCommandState& st, const std::string& styleName, std::vector<int> ids,
                                  std::vector<std::string>& log);
void ExecutePdModeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void ExecutePdSizeCommand(AppCommandState& st, const std::string& args, std::vector<std::string>& log);
void CommitCadPointAt(AppCommandState& st, double x, double y, double z, std::vector<std::string>& log);

/// TRIMSTATE (REQ-056): prompt for a new value, echoing the current one.
void StartTrimStateCommand(AppCommandState& st, std::vector<std::string>& log);
/// Validate and apply a TRIMSTATE value (0 or 1). False + a logged message when out of range.
bool ApplyTrimStateValue(AppCommandState& st, int value, std::vector<std::string>& log);

/// REQ-100 frame-budget benchmark. \ref StartFrameBudgetBench installs the bench scene and the
/// scripted orbit; the frame loop drives it and calls \ref FinishFrameBudgetBench, which restores
/// the user's drawing and camera and reports the p95 verdict.
bool StartFrameBudgetBench(AppCommandState& st, int segments, int frames, std::vector<std::string>& log);
void FinishFrameBudgetBench(AppCommandState& st, std::vector<std::string>& log);

/// REQ-100 profile (e) (TASK-270 §14): `BENCH POINTCLOUD <path>` cannot synthesize its scene the
/// way every other profile does — the requirement names the reference scene as "the user's own
/// driving E57," so this starts a real async import (`StartPointCloudImportAsync`) after clearing
/// the other entity stores, then defers the scripted-orbit install until the import lands.
/// \ref InstallPointCloudBenchScene is called from `TickPointCloudImport` once the import
/// completes (success installs the timed orbit; failure restores the cleared stores and reports).
bool StartPointCloudBench(AppCommandState& st, const std::string& path, std::vector<std::string>& log);
void InstallPointCloudBenchScene(AppCommandState& st, std::vector<std::string>& log);
/// ELEV — set the work-plane elevation new geometry is drawn at (REQ-058).
void StartElevCommand(AppCommandState& st, std::vector<std::string>& log);
bool ApplyElevValue(AppCommandState& st, double z, std::vector<std::string>& log);
void ApplyUcsWorld(AppCommandState& st, std::vector<std::string>& log);

#include "ucs/CadUcsCommands.hpp"

#include "draw/CadDrawCommands.hpp"


void StartTraverseEditorCommand(AppCommandState& st, std::vector<std::string>& log);
void StartOptionsCommand(AppCommandState& st, std::vector<std::string>& log);
void StartSurveyInverseCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDistCommand(AppCommandState& st, std::vector<std::string>& log);
#include "modify/CadModifyCommands.hpp"

// --- SECTIONPLANE (REQ-342 / ADR-059, GitHub issue #479) -------------------------------------

/// `SECTIONPLANE` — place the section clip plane on a solid's flat face. Opens the face-select step.
void StartSectionPlaneCommand(AppCommandState& st, std::vector<std::string>& log);
/// End the face-select step without placing anything (ESC).
void CancelSectionPlaneCommand(AppCommandState& st);
/// The one prompt the command shows, so the hint and the log cannot word it differently.
[[nodiscard]] const char* CadSectionPlanePromptText(const AppCommandState& st);

/// A typed answer to SECTIONPLANE's point prompts (a coordinate, or Enter). False when the command
/// is not running, so the dispatcher can pass the line on.
bool HandleSectionPlaneTextInput(const std::string& line, AppCommandState& st,
                                 std::vector<std::string>& log);

/// The viewport click that answers SECTIONPLANE's point prompts, in plan-space world coordinates —
/// the same shape of entry point `SECTION` uses for its own three points.
void SubmitSectionPlanePointPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// Track the cursor while SECTIONPLANE is asking for its through point, so the plane can be drawn
/// where it would land (REQ-342, 2026-09-18). Resolves the ray exactly as the click does — on the
/// geometry it hits, else on the work plane — so the preview cannot promise a plane the click would
/// not place. A no-op unless the command is at that prompt.
void UpdateSectionPlanePreview(AppCommandState& st, const ray3d::Ray& ray, const solidpick::Tolerance& tol);

/// Forget the tracked cursor: the command ended, the cursor left the viewport, or the route changed.
void ClearSectionPlanePreview(AppCommandState& st);

/// The rectangle to draw for that preview, or false when there is nothing to preview (no tracked
/// cursor, or a cursor that names no plane yet). The same rectangle, in the same place, that the
/// click will place — it is built by the same code.
[[nodiscard]] bool CadSectionPlanePreviewIndicator(const AppCommandState& st, SectionClipIndicator* out);
/// The viewport click that answers "select a flat face". Returns true when a plane was placed;
/// on any refusal the command stays open and the reason is in \p log.
bool SubmitSectionPlaneFacePick(AppCommandState& st, const ray3d::Ray& ray,
                                const solidpick::Tolerance& tol, std::vector<std::string>& log);
/// The frame the clip plane is currently built from: the face `SECTIONPLANE` was given, or the
/// active UCS when it was never given one (D-2026-09-11-b).
[[nodiscard]] ucs::Ucs CadEffectiveSectionClipFrame(const AppCommandState& st);




/// Drops any queued ZOOM EXTENTS / ZOOM WINDOW request.
void ClearPendingViewportZoom(AppCommandState& st);
/// Queues a ZOOM EXTENTS for the viewport's next paint, replacing any queued zoom. Unlike
/// StartZoomExtentsCommand it has no active-command guard: the REQ-120 middle double-click is a
/// transparent view operation and changes the camera only.
void RequestZoomExtents(AppCommandState& st);

void StartZoomExtentsCommand(AppCommandState& st, std::vector<std::string>& log);

/// Commits a circle at (\p cx,\p cy,\p cz) with radius \p r, normal (\p nx,\p ny,\p nz), local
/// coordinates (REQ-312 plane convention) - the same commit the CIRCLE command uses, exposed for
/// REQ-399's bare northing/easting candidates (the CIRCLE command's own commit function has internal
/// linkage inside CadCommands.cpp, so this is a thin externally-visible forwarder, not a rename).
void CommitCircleExternal(AppCommandState& st, float cx, float cy, float cz, float r, float nx, float ny, float nz,
                          std::vector<std::string>& log);
void StartZoomWindowCommand(AppCommandState& st, std::vector<std::string>& log);
/// PAN command (REQ-045): enters interactive pan mode — left-drag pans the active view (hand cursor);
/// Esc / Enter / right-click exits. Reuses the existing middle-drag view-pan math.
void StartPanCommand(AppCommandState& st, std::vector<std::string>& log);
/// ORBIT / Free Orbit (REQ-084 (c)): enters interactive orbit — left-drag tumbles the model view;
/// Esc / Enter / right-click exits. Model space only: a paper sheet is 2D (ADR-025 (g)).
void StartOrbitCommand(AppCommandState& st, std::vector<std::string>& log);

#include "properties/CadPropertyCommands.hpp"


#include "geo/CadGeoCommands.hpp"

#include "alignment/CadAlignmentCommands.hpp"


/// Applies pending zoom-extents or zoom-window requests using current framebuffer size.
void ProcessPendingViewportZoom(AppCommandState& st, double* panX, double* panY, float* zoom, int fbW, int fbH,
                                float viewportAspect, std::vector<std::string>& log);


/// Clears window-selection draft state and CAD entity selection only (not survey point pick).
void ClearCadSelection(AppCommandState& st);

// --- Sub-object selection (REQ-318 increment 2 / D-2026-09-04-a, issue #148) ------------------
//
// Free functions rather than members for the reason every other selection operation in this header
// is: `AppCommandState` is plain data (architecture §11), and these are testable against a
// hand-built state with no window and no document.

/// Drop every sub-object reference whose solid is gone or has been REPLACED (REQ-318 item 10).
///
/// A solid is immutable and replaced rather than edited (`CadSolidPtr` is `shared_ptr<const>`), so
/// "replaced" is exactly "the `weak_ptr` no longer locks to `cadSolids[solidIndex]`" — which covers
/// an erase, an undo, a boolean, and any direct edit, without any of them having to remember to call
/// something. Also drops references whose index no longer addresses anything on the solid it names.
///
/// Returns the number dropped, so a caller can report an expiry rather than have a selection quietly
/// shrink (REQ-201).
int ExpireSubObjectSelection(AppCommandState& st);

/// Add \p pick to the sub-object selection, or remove it when \p toggle and it is already there.
///
/// \p toggle is Shift's meaning, kept identical to Shift's meaning for entities. A plain add of a
/// sub-object already selected is a no-op rather than a duplicate.
void ToggleSubObjectSelection(AppCommandState& st, const SelectedSubObject& pick, bool toggle);

/// The nearest sub-object under \p ray across EVERY visible solid, or false for a miss.
///
/// This is where TASK-189's DEBT-1 is closed. `solidpick::PickSubObject` sees one solid at a time,
/// so its occlusion rule cannot reach across solids: a vertex hidden behind a *different* solid is
/// still a hit as far as that function knows. Ordering the per-solid answers on `Pick::rayT` — the
/// distance the query returns for exactly this purpose — is the caller's job, and this is the
/// caller.
///
/// Honours layer visibility and isolation through `SolidVisible`, so a pick cannot name geometry the
/// renderer is not drawing (the rule REQ-084 (d) already applies to the entity pick). Solids with no
/// cached tessellation are skipped rather than tessellated: a pick must not cost a tessellation
/// (REQ-318 item 7).
///
/// Honours the live section clip (REQ-341) the same way: a face, edge or vertex on the removed side
/// is not drawn, so it neither answers nor hides a solid behind it.
///
/// \p facesPickable false skips the triangles entirely — no face answers and nothing occludes —
/// which is what a 2D Wireframe view shows.
[[nodiscard]] bool PickSubObjectAcrossSolids(const AppCommandState& st, const ray3d::Ray& ray,
                                             const solidpick::Tolerance& tol, SelectedSubObject* out,
                                             solidpick::Pick* outPick = nullptr, bool facesPickable = true);

/// ONE sub-object click, whole: pick along \p ray, apply the mutual-exclusion rule, update the
/// store, and say what happened. Returns true when something was picked.
///
/// The *rule* lives here rather than in the viewport's mouse handler on purpose. What the click
/// does — the entity selection is cleared because the two are mutually exclusive (REQ-318 item 9),
/// `toggle` (Shift) removes an already-selected sub-object, and a miss CLEARS rather than arming a
/// selection fence — is behaviour a transcript has to be able to drive and assert. Left inline in
/// `CadUi.cpp` it would have been reachable only by hand, which is the shape of defect TASK-099
/// found five times over. The UI keeps exactly one decision of its own: that `Ctrl` is what asks
/// for this.
bool SubmitSubObjectPick(AppCommandState& st, const ray3d::Ray& ray, const solidpick::Tolerance& tol,
                         bool toggle, std::vector<std::string>& log);

/// What the sub-object rollover says (REQ-318 item 14) — the same four fields AutoCAD's rollover
/// shows for an object, with the sub-object's KIND as the title.
///
/// Strings, resolved here rather than at draw time, for the reason \ref SurfaceHoverRow is: the
/// readout is then purely presentational and the resolution — which is where "ByLayer" and a missing
/// attribute row have to be handled — is testable without a window.
struct SubObjectHoverRow {
  std::string title;     ///< "Solid face" / "Solid edge" / "Solid vertex", plus the index.
  std::string solid;     ///< which solid, 1-based, as the command line numbers them.
  std::string color;
  std::string layer;
  std::string linetype;
};

/// Describe \p s for the rollover. False (and \p out untouched) when the reference no longer
/// resolves — an expired reference has nothing truthful to say about a solid that is gone.
[[nodiscard]] bool BuildSubObjectHoverRow(const AppCommandState& st, const SelectedSubObject& s,
                                          SubObjectHoverRow* out);
/// Replace selection with all entities of the same kind as the first selected item (or all survey points).
/// Move the armed grip to (x, y) in local storage coordinates — the one place grip geometry is written, so
/// the mouse drag and command-line distance entry cannot drift apart. No-op when no grip is armed.
/// Callers own the undo snapshot and \ref BumpCadGpuCache.
void ApplyEntityGripPoint(AppCommandState& st, float x, float y, float z);

void SelectSimilarToCurrentSelection(AppCommandState& st, std::vector<std::string>* log);

#include "gizmo/CadGizmoCommands.hpp"

/// Every primary command name in the command registry (issue #785's regression test: a merge
/// dropped eight handlers while leaving their registry rows in place, which made the loss
/// invisible by inspection). Test-only, but cheap enough to leave built in all configurations.
std::vector<std::string> RegisteredCommandPrimariesForTest();

/// True if some handler claimed \p primary (a name from `RegisteredCommandPrimariesForTest()`).
/// False means the registry advertises a command that silently does nothing when typed.
bool DispatchByPrimaryForTest(const std::string& primary, AppCommandState& st, std::vector<std::string>& log);


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

/// REQ-370 live preview: refreshes `st.selBoxPreview` with what releasing the selection box at
/// (\p wx, \p wy) would add to the selection, using the very hit test the click uses
/// (`ComputeSelectionFromRect`), so the lit objects are the selected objects. `st.selection` and the
/// survey-point selection are left exactly as they were. Recomputed only when an input changed. Pass
/// \p cam null in plan view, like the click does. Clears the preview when no box is open.
void UpdateSelectionBoxPreview(AppCommandState& st, float wx, float wy, bool windowMode, const Camera* cam,
                               float vpW, float vpH);

void CancelActiveCommand(AppCommandState& st, std::vector<std::string>& log);
/// Clears Shift+RMB one-shot snap (call on pick submit, cancel, reset, clear geometry).
void ClearPendingOneShotObjectSnap(AppCommandState& st);
/// Called from UI when COPY survey duplicate-ID modal closes (\p applySurveyDup runs duplication).
void ApplyCopySurveyDuplicateModalResult(AppCommandState& st, bool applySurveyDup, std::vector<std::string>& log);

bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);

/// REQ-316 / ADR-047: the bulge for the segment leaving the last POLYLINE draft vertex if the next
/// point were (x,y). 0 in LINE mode; in ARC mode the arc is tangent to the previous segment unless
/// a radius or included angle was typed. Shared by the vertex-commit path and the live preview so
/// the drawn arc matches the committed one exactly.
float CadPolylineDraftBulgeForNextPoint(const AppCommandState& st, float x, float y);

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
// `pickRay` (issue #373 follow-up): the camera ray behind this click, in an orbited/non-plan model
// view — nullptr in plan view and paper space, matching PickClosestCadEntity's own convention.
// Threaded through so a RawEntityPick command (FILLET today) can hit-test the TRUE 3D distance from
// the ray to elevated geometry instead of the click's flattened work-plane intersection, which is
// nowhere near a line that does not lie on the current work plane.
void SubmitViewportPick(AppCommandState& st, double localX, double localY, std::vector<std::string>& log,
                        bool windowSelectionSubtract = false, bool fenceLeftToRightWindowMode = false,
                        const ray3d::Ray* pickRay = nullptr);

/// One click during \c ViewportClickRoute::SelectionAccumulate: toggle a hit into the growing
/// selection, or arm a selection box on empty space. Returns true when an entity/sub-object was
/// toggled; false when a box corner was armed instead (or when \p beginBoxOnMiss is false and the
/// click missed).
bool SubmitViewportSelectionAccumulateClick(AppCommandState& st, double rawLocalX, double rawLocalY,
                                            float anchorScreenX, float anchorScreenY, bool shiftModifier,
                                            bool ctrlModifier, const ray3d::Ray* pickRay,
                                            float viewportWidthPx, float viewportHeightPx, float orthoHalfH,
                                            std::vector<std::string>& log, bool beginBoxOnMiss = true);

void ProcessCommandLineSubmit(char* cmdBuf, int cmdBufSize, AppCommandState& st, std::vector<std::string>& log);
void StartAlignCommand(AppCommandState& st, std::vector<std::string>& log);
void ExecuteAlignCommand(AppCommandState& st, std::vector<std::string>& log);
/// Recompute Helmert solution from current \ref AppCommandState::alignControlPts into \ref AppCommandState::alignLastResult.
void RecalcAlignResult(AppCommandState& st);
/// Apply the last Helmert solution, generate a report tab, and close the results window.
/// \p applyScale — false strips scale (rotation + translation only; re-derives tx/ty from centroids).
void ApplyAlignCommand(AppCommandState& st, std::vector<std::string>& log, bool applyScale);
#include "pdf/CadPdfAttachCommands.hpp"

#include "clipboard/CadClipboardCommands.hpp"

std::vector<std::string> FuzzyCommandMatches(const std::string& query, int maxResults);

/// One fuzzy-match entry for the command-line autocomplete UI.
struct CommandSuggestion {
  std::string name;         ///< primary command name, uppercased (e.g. "LINE")
  std::string description;  ///< short human description shown in parentheses
  bool exactMatch = false;  ///< query exactly names the primary command or one of its aliases
};

/// Ranked fuzzy matches with descriptions, for the nanoCAD-style command picker.
std::vector<CommandSuggestion> FuzzyCommandSuggestions(const std::string& query, int maxResults);

/// What the command line should actually run for \p typedText, decided fresh against the text
/// itself rather than a suggestion computed on an earlier keystroke (GitHub issue #766: the UI's
/// persisted autocomplete highlight could survive from an unrelated earlier query, so typing the
/// exact alias ERASE ran DRAWINGSETTINGS, RENAME ran SURFACERENAME, and TABLE ran VOLTABLE). A
/// single-token \p typedText that exactly names a command or alias always resolves to that
/// command — FuzzyCommandSuggestions ranks an exact match first — so an exact name never loses to
/// a stale or merely-similar suggestion. A multi-token line (a command plus its arguments) and an
/// unrecognized single token are both returned unchanged.
std::string ResolveCommandLineSubmission(const std::string& typedText);

/// Lowercase registry primary for a command name or alias, listed or not ("l" -> "line",
/// "ts" -> "toolspace"); empty when nothing is registered under \p name. Used by contextual help.
std::string CommandPrimaryForName(const std::string& name);

/// Lowercase registry primary of the one command that runs as \p kind; empty when no command or
/// more than one does (several primitives share CadCommandKind::Solid, for example).
std::string CommandPrimaryForKind(CadCommandKind kind);

/// REQ-121 rule (3): THE prompt every object-selection step shows, in the command line and in the
/// dynamic cursor text alike. One string, reused verbatim — not a per-command sentence that happens
/// to mean the same thing, which is what produced "click two corners to window-select objects" in
/// one command, "window-select entities, then press Enter" in another, and no Enter hint at all in
/// a third.
///
/// It carries **no command name**, deliberately. AutoCAD prompts "Select objects:" whatever is
/// running, and #91 asks for that convention explicitly; a `MOVE/COPY:` prefix would also make the
/// string non-identical across commands, which is the one thing this constant exists to prevent.
/// Which command is active is already visible in the command line's own history.
inline constexpr const char* kSelectObjectsPrompt = "Select objects, ENTER to continue | ESC cancel";

/// Single prompt/footer hint query backed by the active CommandSession (ADR-0012).
std::string ActiveCommandPromptHint(const AppCommandState& st);

/// Model-space extents of everything drawable.
///
/// \p vpFilter (REQ-123, GitHub #100) restricts the sweep to what is VISIBLE THROUGH one paper-space
/// viewport: entities whose layer is frozen in it are skipped, the same test the viewport renderer
/// and the plotter already apply. **Defaults to nullptr — no filter, and every pre-existing caller
/// keeps exactly the behaviour it had.** It is deliberately a filter on VISIBILITY, not on entity
/// kind: the viewport renderer currently draws only lines / polylines / circles / arcs / survey
/// points, and encoding that gap here would freeze a renderer limitation into the extents math.
bool ComputeWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY, double* outMxY,
                         const Viewport* vpFilter = nullptr);
/// Robust extents that drop far-outlier entities (DXFs often contain stray geometry at world (0,0) such as
/// defpoints, block-insert origins, or leftover construction). On success, \p outSkipped is the number of
/// entities discarded; 0 means the answer equals \ref ComputeWorldExtents.
bool ComputeRobustWorldExtents(const AppCommandState& st, double* outMnX, double* outMxX, double* outMnY,
                               double* outMxY, int* outSkipped, const Viewport* vpFilter = nullptr);
// The camera side of zoom-extents is `zoomframing::FrameWorldRect` (ZoomFraming.hpp) — pure, shared
// by every fit path, and tested there (REQ-122).

/// The drawing's TRUE 3D extents, in storage coordinates, for framing an orbited view (GitHub issue
/// #564 §1, D-2026-09-28-d): every store ZOOM EXTENTS already sweeps, each with its elevation range,
/// plus filled regions, block references and pipe runs. Far outliers are dropped by the same
/// plan-centre rule \ref ComputeRobustWorldExtents uses. False when there is nothing to frame.
bool ComputeWorldExtents3d(const AppCommandState& st, ray3d::Vec3* outMin, ray3d::Vec3* outMax,
                           int* outSkipped);

/// The box the section-clip indicator is sized to cover (REQ-341, D-2026-09-16-b), in storage
/// coordinates: the drawing's extents as ZOOM EXTENTS measures them — every entity kind the clip can
/// cut, not only solids — with the Z range of the solids when there are any, and the active UCS
/// origin's elevation otherwise. False for an empty drawing; the caller then centres on the view.
///
/// Sized from the whole model rather than the solids alone because the clip cuts linework, meshes,
/// surfaces and PDFs too, and because the old fallback — centred on the UCS origin — sat millions of
/// feet off screen in a state-plane drawing with no solids (code review on #478, finding 8).
bool ComputeSectionClipIndicatorBounds(const AppCommandState& st, ray3d::Vec3* outMin, ray3d::Vec3* outMax);

bool ComputeCircumcircle(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                         float* r);
