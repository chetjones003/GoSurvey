#pragma once

#include "CadEntities.hpp"
#include "CadDimGeom.hpp"
#include "EntityId.hpp"
// TextStyles::DefaultTextStyles, for the textStyles member's initializer (issue #57). Pure and
// dependency-free (string/vector/CadEntities.hpp), so this adds no cycle and no weight.
#include "TextStyle.hpp"
// SurfaceStyles::DefaultSurfaceStyles, for the surfaceStyles member's initializer — same reason,
// same shape, and pure for the same reason (REQ-070 / ADR-036 (d)).
#include "SurfaceStyle.hpp"
#include "DimensionStyle.hpp"
#include "render/Camera.hpp"  // Commands -> Renderer is a downward dependency (architecture §2)
// The one authoritative WCS <-> UCS implementation (REQ-154). Pure and dependency-free, like
// util/ray3d beside it, so the coordinate-system rules are testable without a window.
#include "util/ucs.hpp"
#include "PdfAttach.hpp"
#include "PaperSpace.hpp"
#include "SurveyPoints.hpp"
#include "AngleFormat.hpp"
#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"
#include "update/UpdateCheck.hpp"  // update::UpdatePrefs only — pure, no network, no <thread>
#include "util/tinbuild.hpp"       // TinBuildResult, for AppCommandState::SurfaceRebuildAsync (REQ-069)
#include "util/surfacevolume.hpp"  // SurfaceVolumeResult, for AppCommandState::VolumeDashboardState (REQ-073)
#include "util/surfacequery.hpp"   // SurfaceProfileSample, Quick Profile (REQ-145)
#include "util/watershed.hpp"      // WatershedResult, for AppCommandState::SurfaceWatershedCacheEntry (REQ-132)
// curveisect::Vec2/Seg/Conic + Intersect*, for FILLET's tangent-arc solve (REQ-103 step 6a) below.
// Dependency-free by its own design (curveintersect.hpp's own doc comment), so this adds no cycle.
#include "util/curveintersect.hpp"
#include "util/geom2d.hpp"        // BulgeArc, for the polyline arc-segment grips (REQ-316 / ADR-047)
// HoverDwell, for AppCommandState's surface rollover timer (REQ-089). Pure and dependency-free
// (<cmath>), and deliberately in util/ rather than beside the UI that drives it: the state lives on
// AppCommandState, and Commands may not include a UI header (architecture §11.1).
#include "util/hoverdwell.hpp"
#include "util/hoverpickgate.hpp"  // per-frame viewport hover-pick throttle (GitHub issue #166)
#include "util/cadtable.hpp"   // CadTable entity (REQ-148 / D-2026-08-28-i)
#include "util/cadblock.hpp"   // Block definitions + INSERT refs (GitHub issue #124)
#include "util/cadsolid.hpp"   // B-rep solids + their tessellation cache (REQ-313 / ADR-045)
#include "util/solidpick.hpp"  // solidpick::Kind, for SelectedSubObject (REQ-318 / ADR-049)
// zoomframing::FrameWorldRect, the one camera-framing implementation behind ZOOMEXTENTS, the REQ-120
// gesture, ZOOM WINDOW and the post-import fit (REQ-122). Pure and dependency-free, like the headers
// above it.
#include "ZoomFraming.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
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
    /// **Display-and-erase only in this increment, like Mesh** — it selects, highlights, erases and
    /// reports its volume, and no transform command moves it. That is a stated boundary rather than
    /// an oversight: moving a solid means transforming every surface frame and every arc-edge frame
    /// in its topology, which is the same class of work REQ-312 found for a single tilted arc, and
    /// it belongs with #120's Phase 5 direct-modelling requirement. Every transform command refuses
    /// a solid with a stated reason (REQ-201) rather than silently dropping it from the operation —
    /// the rule Surface already established.
    Solid = 13
  };
  Type type = Type::LineSeg;
  int index = 0; ///< Entity index in the parallel container for \p type
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

/// REQ-308 / D-2026-08-30-a: index of the first real drawing tab. drawingTabs[0] is the Start
/// screen sentinel, so drawing-tab iteration and "select the first drawing" both start here.
inline constexpr int FirstDrawingTabIndex() { return 1; }

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
/// After editing extension points or dimension offset, restore text from fixed (normal, tangent) offsets vs dim mid.
void CadDimAlignedApplyInsFromLocalOffset(CadAnnotation* ann, float alongN, float alongT);
/// Recompute dimension text from current geometry (linear / aligned / angular).
void CadDimRefreshMeasurementText(CadAnnotation* ann, int linearPrecision, const AngleDisplaySettings& angle);

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
void ApplyStretchToSelection(AppCommandState& st, float dx, float dy, float mnX, float mxX, float mnY,
                             float mxY, std::vector<std::string>& log);

// ================================================================================================
// REQ-103 FILLET (step 6a) / CHAMFER (step 6b) — pure tangent-arc / corner-point geometry.
//
// Inline and header-only (pure — only <cmath>/<vector> and curveintersect.hpp, both dependency-free)
// so this, the genuinely new geometry in this step, is unit-testable (tests/FilletGeomTests.cpp)
// without linking CadCommands.cpp's whole command layer — the same reasoning \ref
// RecomputeArcFromEndpoints above already follows for STRETCH's own new geometry.
//
// The construction: offset both picked curves by the fillet radius (a Line translated along its own
// perpendicular; an Arc/Circle's own full circle grown/shrunk by the radius) and intersect every
// combination analytically via curveisect's existing IntersectSegSeg/IntersectSegConic/
// IntersectConicConic (REQ-062/REQ-101 — never tessellated); the candidate nearest both pick points
// (summed squared distance) is chosen. Radius 0 needs no special case — every offset collapses to
// the original curve, so the "candidate center" is simply the curves' own intersection. The actual
// trim/extend mutation afterward reuses LENGTHEN's ApplyLengthenToLine/ToArc/ToPolylineEnd
// unchanged, converting a known tangent point into the `newLength` those functions already accept.
// ================================================================================================

/// One of FILLET/CHAMFER's two picked curves, reduced to only what the geometry solve needs: a
/// Line's own infinite extension (isLine=true; ax,ay/bx,by are any two distinct points on it — a
/// Polyline segment's own two endpoints work identically), or an Arc/Circle's own FULL circle
/// (isLine=false; cx,cy,r) — always the full circle, never sweep-limited, matching \ref
/// FindExtendArcTarget's own reasoning: a tangent point beyond the arc's CURRENT sweep must still
/// be found so the arc can be extended (not just trimmed within its existing span) to reach it.
struct FilletCurve {
  bool isLine = true;
  float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
  float cx = 0.f, cy = 0.f, r = 0.f;
};

/// A curveisect::Seg standing in for curve `(ax,ay)-(bx,by)`'s INFINITE extension. curveisect has no
/// infinite-line primitive (confirmed by EXTEND's own research note, CadCommands.cpp) — a generously
/// long finite query segment stands in instead, EXTEND's own `FindExtendLineTarget` technique,
/// extended in BOTH directions here (a fillet's tangent point may lie beyond either original
/// endpoint, unlike EXTEND's forward-only ray).
inline curveisect::Seg FilletLongLine(float ax, float ay, float bx, float by) {
  // Double precision throughout, matching curveisect::Vec2's own double storage — computing the
  // huge offset in float32 (the original bug, found via a real user report) loses ~0.01-0.05 units
  // of precision at typical drawing scales (a ~1e6-unit offset leaves float32 only ~7 significant
  // digits to place a coordinate that's normally two digits), which showed up as fillet arcs that
  // visibly did NOT touch their own trimmed line endpoints. Doubles keep this error near 1e-9.
  const double dax = ax, day = ay, dbx = bx, dby = by;
  const double dx = dbx - dax, dy = dby - day;
  const double len = std::hypot(dx, dy);
  if (len < 1e-9)
    return {{dax, day}, {dbx, dby}};
  const double ux = dx / len, uy = dy / len;
  constexpr double kHuge = 1.0e6;
  const double mx = 0.5 * (dax + dbx), my = 0.5 * (day + dby);
  return {{mx - ux * kHuge, my - uy * kHuge}, {mx + ux * kHuge, my + uy * kHuge}};
}

/// Exact infinite-line intersection (Cramer's rule, double precision throughout) — used for the
/// Line-Line candidate case instead of `FilletLongLine`'s "huge finite segment" approximation,
/// which curveisect's Seg-based functions still need for a Line vs Circle/Conic (no infinite-line
/// primitive exists there) but which two genuinely infinite lines never need at all: this is exact
/// regardless of the drawing's coordinate scale. False only when the two lines are parallel.
inline bool FilletLineLineIntersectInf(float ax, float ay, float bx, float by, float cx, float cy, float dx,
                                       float dy, float* outX, float* outY) {
  const double rx = static_cast<double>(bx) - ax, ry = static_cast<double>(by) - ay;
  const double sx = static_cast<double>(dx) - cx, sy = static_cast<double>(dy) - cy;
  const double det = rx * sy - ry * sx;
  if (std::fabs(det) < 1e-9 * std::max(1.0, std::hypot(rx, ry) * std::hypot(sx, sy)))
    return false;
  const double t = ((static_cast<double>(cx) - ax) * sy - (static_cast<double>(cy) - ay) * sx) / det;
  *outX = static_cast<float>(ax + t * rx);
  *outY = static_cast<float>(ay + t * ry);
  return true;
}

/// Every real candidate fillet-arc center for `c1`/`c2` offset by `radius` (up to 4 for two lines,
/// up to 8 for a line+arc or two arcs — see the section comment above for the construction).
inline void FilletCandidateCenters(const FilletCurve& c1, const FilletCurve& c2, float radius,
                                   std::vector<curveisect::Vec2>* out) {
  auto lineOffsetVariant = [](const FilletCurve& c, float signedR, float* ax, float* ay, float* bx,
                              float* by) {
    const float vx = c.bx - c.ax, vy = c.by - c.ay;
    const float len = std::hypot(vx, vy);
    float nx = 0.f, ny = 1.f;
    if (len > 1e-12f) {
      nx = -vy / len;
      ny = vx / len;
    }
    *ax = c.ax + nx * signedR;
    *ay = c.ay + ny * signedR;
    *bx = c.bx + nx * signedR;
    *by = c.by + ny * signedR;
  };
  auto lineOffsetVariantSeg = [](const FilletCurve& c, float signedR) -> curveisect::Seg {
    const float vx = c.bx - c.ax, vy = c.by - c.ay;
    const float len = std::hypot(vx, vy);
    float nx = 0.f, ny = 1.f;
    if (len > 1e-12f) {
      nx = -vy / len;
      ny = vx / len;
    }
    return FilletLongLine(c.ax + nx * signedR, c.ay + ny * signedR, c.bx + nx * signedR, c.by + ny * signedR);
  };
  const float signs[2] = {1.f, -1.f};
  if (c1.isLine && c2.isLine) {
    for (float s1 : signs) {
      float a1x = 0.f, a1y = 0.f, b1x = 0.f, b1y = 0.f;
      lineOffsetVariant(c1, radius * s1, &a1x, &a1y, &b1x, &b1y);
      for (float s2 : signs) {
        float a2x = 0.f, a2y = 0.f, b2x = 0.f, b2y = 0.f;
        lineOffsetVariant(c2, radius * s2, &a2x, &a2y, &b2x, &b2y);
        float ix = 0.f, iy = 0.f;
        if (FilletLineLineIntersectInf(a1x, a1y, b1x, b1y, a2x, a2y, b2x, b2y, &ix, &iy))
          out->push_back({ix, iy});
      }
    }
    return;
  }
  if (!c1.isLine && !c2.isLine) {
    for (float s1 : signs) {
      const float r1 = c1.r + radius * s1;
      if (r1 < 1e-6f)
        continue;
      const curveisect::Conic k1 = curveisect::MakeCircle(c1.cx, c1.cy, r1);
      for (float s2 : signs) {
        const float r2 = c2.r + radius * s2;
        if (r2 < 1e-6f)
          continue;
        const curveisect::Conic k2 = curveisect::MakeCircle(c2.cx, c2.cy, r2);
        std::vector<curveisect::Hit2> hits;
        curveisect::IntersectConicConic(k1, k2, &hits);
        for (const auto& h : hits)
          out->push_back(h.p);
      }
    }
    return;
  }
  const FilletCurve& lineC = c1.isLine ? c1 : c2;
  const FilletCurve& circC = c1.isLine ? c2 : c1;
  for (float sL : signs) {
    const curveisect::Seg l = lineOffsetVariantSeg(lineC, radius * sL);
    for (float sC : signs) {
      const float rC = circC.r + radius * sC;
      if (rC < 1e-6f)
        continue;
      const curveisect::Conic k = curveisect::MakeCircle(circC.cx, circC.cy, rC);
      std::vector<curveisect::Hit2> hits;
      curveisect::IntersectSegConic(l, k, &hits);
      for (const auto& h : hits)
        out->push_back(h.p);
    }
  }
}

/// True when `pt`, projected onto the segment from `nearX,nearY` to `farX,farY`, has NOT overshot
/// past the far endpoint (parametric t <= 1, with a small tolerance). FILLET's "radius is too
/// large" refusal (D-2026-08-25-b): a valid trim/extend target must stay within (or very near) the
/// curve's own original span from its near-the-corner endpoint out to its far endpoint — going past
/// the far endpoint means the requested radius needs more material than the curve actually has.
///
/// An EARLIER version of this same idea compared which of the two endpoints `pt` is nearer to
/// (Euclidean distance) instead of this parametric projection, and was wrong: a tangent point at
/// 60% of the way along a perfectly valid, comfortably-sized fillet is *already* nearer the far
/// endpoint than the near one (past the segment's own midpoint is completely ordinary), which that
/// version misread as "overshoot" and refused a legitimate, correctly-sized fillet — caught by a
/// regression test built from the same user report this whole check exists for, using a SECOND
/// radius on the identical geometry that should have succeeded and didn't.
inline bool FilletPointWithinSpan(float nearX, float nearY, float farX, float farY, float ptX, float ptY) {
  const double dx = static_cast<double>(farX) - nearX, dy = static_cast<double>(farY) - nearY;
  const double len2 = dx * dx + dy * dy;
  if (len2 < 1e-12)
    return true;  // near == far (degenerate); nothing meaningful to check
  const double t = ((static_cast<double>(ptX) - nearX) * dx + (static_cast<double>(ptY) - nearY) * dy) / len2;
  constexpr double kSlack = 1e-3;
  return t <= 1.0 + kSlack;
}

/// True when `pt` is on the "kept" side of Line/Polyline-segment curve `c` — the side the pick
/// indicated — measured along the curve's OWN direction from its first stored endpoint, not
/// perpendicular to it. `pt - c.ax,ay` projected onto the curve's direction must have the same sign
/// as `pick - c.ax,ay}` projected the same way. Used to filter fillet-center candidates (below) to
/// the one actually consistent with both picks, not merely the one nearest them — see that
/// function's own comment for why nearest-to-pick alone is not enough.
inline bool FilletPointOnKeptSide(const FilletCurve& c, float pickX, float pickY, float ptX, float ptY) {
  const double ux = static_cast<double>(c.bx) - c.ax, uy = static_cast<double>(c.by) - c.ay;
  const double pickProj = (static_cast<double>(pickX) - c.ax) * ux + (static_cast<double>(pickY) - c.ay) * uy;
  const double ptProj = (static_cast<double>(ptX) - c.ax) * ux + (static_cast<double>(ptY) - c.ay) * uy;
  return (pickProj >= 0.0) == (ptProj >= 0.0);
}

/// Picks the candidate fillet center (from \ref FilletCandidateCenters) nearest the two pick points
/// — summed squared distance, the deterministic tie-break generalizing every other REQ-103 step's
/// own ambiguity resolution (LENGTHEN's nearest endpoint, EXTEND's nearest boundary hit, BREAK's
/// position-ordering: "whichever interpretation is closest to what the user actually clicked").
///
/// For two Lines specifically, "nearest to pick" alone is not a reliable filter: a real user report
/// found that for a large radius relative to short line segments, the mathematically-correct
/// "inside the corner" candidate is pushed FAR from the picks (a large radius means a long tangent
/// length, by construction), while a geometrically WRONG candidate — offset to one side, not
/// filling the actual corner at all — can end up numerically closer to picks placed near the
/// visible corner, and so wins the naive tie-break, producing a bizarre, disconnected-looking
/// result rather than either a correct (if large) arc or a clean refusal. Fixed by filtering to
/// candidates on the "kept" side (`FilletPointOnKeptSide`) of BOTH lines first — hand-verified
/// against this exact case in `tests/FilletGeomTests.cpp` — and running the nearest-to-pick
/// tie-break only among those; if the filter leaves nothing (should not happen for two genuinely
/// non-parallel lines, but a real geometry library earns defensive code), the unfiltered search is
/// used rather than manufacturing a spurious refusal.
///
/// False if no real candidate exists at all (parallel/collinear lines, arcs too far apart for the
/// radius, etc.) — REQ-201, the caller states the reason.
inline bool SolveFilletCenter(const FilletCurve& c1, const FilletCurve& c2, float radius, float pick1X,
                              float pick1Y, float pick2X, float pick2Y, float* outCx, float* outCy) {
  std::vector<curveisect::Vec2> cands;
  FilletCandidateCenters(c1, c2, radius, &cands);
  if (cands.empty())
    return false;
  auto pickBest = [&](const std::vector<size_t>& idx) -> size_t {
    size_t bestI = idx[0];
    double bestD = -1.0;
    for (size_t i : idx) {
      const double dx1 = cands[i].x - pick1X, dy1 = cands[i].y - pick1Y;
      const double dx2 = cands[i].x - pick2X, dy2 = cands[i].y - pick2Y;
      const double d = dx1 * dx1 + dy1 * dy1 + dx2 * dx2 + dy2 * dy2;
      if (bestD < 0.0 || d < bestD) {
        bestD = d;
        bestI = i;
      }
    }
    return bestI;
  };
  std::vector<size_t> allIdx(cands.size());
  for (size_t i = 0; i < cands.size(); ++i)
    allIdx[i] = i;
  size_t bestI = 0;
  if (c1.isLine && c2.isLine) {
    std::vector<size_t> keptIdx;
    for (size_t i : allIdx) {
      const float cx = static_cast<float>(cands[i].x), cy = static_cast<float>(cands[i].y);
      if (FilletPointOnKeptSide(c1, pick1X, pick1Y, cx, cy) && FilletPointOnKeptSide(c2, pick2X, pick2Y, cx, cy))
        keptIdx.push_back(i);
    }
    bestI = keptIdx.empty() ? pickBest(allIdx) : pickBest(keptIdx);
  } else {
    bestI = pickBest(allIdx);
  }
  *outCx = static_cast<float>(cands[bestI].x);
  *outCy = static_cast<float>(cands[bestI].y);
  return true;
}

/// The point where the fillet arc centered at (centerX,centerY) touches Line `c` — the UNCLAMPED
/// foot of the perpendicular onto its infinite extension (OFFSET's own `ClosestPointOnSegment`
/// clamps to [0,1], wrong here: the tangent point routinely lies beyond the curve's current drawn
/// extent, which is exactly what the trim/extend step afterward corrects).
inline void FilletTangentPointOnLine(const FilletCurve& c, float centerX, float centerY, float* tx, float* ty) {
  const float vx = c.bx - c.ax, vy = c.by - c.ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *tx = c.ax;
    *ty = c.ay;
    return;
  }
  const float t = ((centerX - c.ax) * vx + (centerY - c.ay) * vy) / len2;
  *tx = c.ax + t * vx;
  *ty = c.ay + t * vy;
}

/// The point where the fillet arc centered at (centerX,centerY) touches Arc/Circle `c` — guaranteed
/// by \ref FilletCandidateCenters's own construction to already lie exactly on `c`'s own circle,
/// along the ray from `c`'s center through the fillet center.
inline void FilletTangentPointOnCircle(const FilletCurve& c, float centerX, float centerY, float* tx, float* ty) {
  const float dx = centerX - c.cx, dy = centerY - c.cy;
  const float d = std::hypot(dx, dy);
  if (d < 1e-9f) {
    *tx = c.cx + c.r;
    *ty = c.cy;
    return;
  }
  *tx = c.cx + dx / d * c.r;
  *ty = c.cy + dy / d * c.r;
}

/// CCW angular travel in [0, 2*pi) from angle `from` to angle `to`.
inline float FilletCcwAngleDelta(float from, float to) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  float d = std::fmod(to - from, kTwoPi);
  if (d < 0.f)
    d += kTwoPi;
  return d;
}

/// Converts a target tangent point (known, by \ref FilletCandidateCenters's own construction, to
/// lie on arc `(cx,cy,r,startRad,sweepRad)`'s own circle) into the ABSOLUTE new-total-arc-length
/// \ref ApplyLengthenToArc's `newLength` parameter expects, so the moving endpoint (selected by
/// `nearFirst`, the identical convention `ApplyLengthenToArc` uses) lands exactly on it. Unlike
/// EXTEND's own `FindExtendArcTarget` (additive, extend-only by construction), this computes the
/// length directly from the FIXED endpoint, so it is correct whether the fillet shortens or extends
/// the arc — verified against hand-computed cases in tests/FilletGeomTests.cpp before being trusted.
inline float FilletArcTangentPointToNewLength(float cx, float cy, float r, float startRad, float sweepRad,
                                              bool nearFirst, float targetX, float targetY) {
  const float thetaT = std::atan2(targetY - cy, targetX - cx);
  const float fixedAngle = nearFirst ? (startRad + sweepRad) : startRad;
  const bool ccw = (sweepRad >= 0.f);
  float newAbsSweep;
  if (nearFirst)
    newAbsSweep = ccw ? FilletCcwAngleDelta(thetaT, fixedAngle) : FilletCcwAngleDelta(fixedAngle, thetaT);
  else
    newAbsSweep = ccw ? FilletCcwAngleDelta(fixedAngle, thetaT) : FilletCcwAngleDelta(thetaT, fixedAngle);
  return r * newAbsSweep;
}

/// True when two lines' directions are parallel within tolerance — detects FILLET's documented
/// AutoCAD special case (two parallel lines get a semicircle regardless of the current radius
/// setting; see spec/requirements.md REQ-103 FILLET acceptance).
inline bool FilletLinesAreParallel(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
  const float v1x = bx - ax, v1y = by - ay, v2x = dx - cx, v2y = dy - cy;
  const float len1 = std::hypot(v1x, v1y), len2 = std::hypot(v2x, v2y);
  if (len1 < 1e-9f || len2 < 1e-9f)
    return false;
  const float cross = (v1x / len1) * (v2y / len2) - (v1y / len1) * (v2x / len2);
  return std::fabs(cross) < 1e-4f;
}

/// FILLET's documented AutoCAD special case: two parallel, non-collinear lines get connected by a
/// semicircle regardless of the current radius setting. `pick1X,pick1Y` chooses which end of line 1
/// anchors the semicircle (nearest that pick, returned unmoved as `anchorX,anchorY`); the
/// semicircle's OTHER endpoint is the perpendicular projection of the anchor onto line 2's infinite
/// extension (`projX,projY`) — guaranteeing exact tangency to both lines, with the radius that falls
/// out equal to exactly half the true perpendicular distance between them, matching AutoCAD's own
/// documented "radius = half the distance" rule.
inline void FilletParallelSemicircle(float l1ax, float l1ay, float l1bx, float l1by, float l2ax, float l2ay,
                                     float l2bx, float l2by, float pick1X, float pick1Y, float* anchorX,
                                     float* anchorY, float* projX, float* projY) {
  const float d0 = (pick1X - l1ax) * (pick1X - l1ax) + (pick1Y - l1ay) * (pick1Y - l1ay);
  const float d1 = (pick1X - l1bx) * (pick1X - l1bx) + (pick1Y - l1by) * (pick1Y - l1by);
  *anchorX = (d0 <= d1) ? l1ax : l1bx;
  *anchorY = (d0 <= d1) ? l1ay : l1by;
  const float vx = l2bx - l2ax, vy = l2by - l2ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *projX = l2ax;
    *projY = l2ay;
    return;
  }
  const float t = ((*anchorX - l2ax) * vx + (*anchorY - l2ay) * vy) / len2;
  *projX = l2ax + t * vx;
  *projY = l2ay + t * vy;
}

// ================================================================================================
// REQ-103 CHAMFER (step 6b) — pure corner-point geometry. Eligible curves are Line/Polyline-segment
// only (Arc excluded — no standard chamfer-to-arc geometry, matching AutoCAD's own restriction), so
// this is simpler than FILLET's tangent-arc solve: the intersection point of the two (infinite)
// curves is found by reusing `SolveFilletCenter(c1, c2, 0.f, ...)` — FILLET's own radius-0 case,
// already proven correct — then each chamfer point is a fixed distance (or distance+angle) from it.
// ================================================================================================

/// Distance/Distance mode: the chamfer point on curve `c` (a Line/Polyline-segment `FilletCurve`),
/// `dist` from the already-known intersection point `(px,py)`, signed toward whichever side
/// `pickX,pickY` is on (the side the user's pick indicates should be kept).
inline void ChamferPointAtDistance(const FilletCurve& c, float px, float py, float dist, float pickX,
                                   float pickY, float* outX, float* outY) {
  const float dx = c.bx - c.ax, dy = c.by - c.ay;
  const float len = std::hypot(dx, dy);
  if (len < 1e-9f) {
    *outX = px;
    *outY = py;
    return;
  }
  const float ux = dx / len, uy = dy / len;
  const float side = (pickX - px) * ux + (pickY - py) * uy;
  const float sign = (side >= 0.f) ? 1.f : -1.f;
  *outX = px + ux * dist * sign;
  *outY = py + uy * dist * sign;
}

/// Distance/Angle mode: from `(fromX,fromY)` (curve `c`'s own Distance/Distance-style point),
/// a ray along curve `c`'s kept direction (toward `pickX,pickY`) rotated by `angleRad`, intersected
/// with curve `other`'s infinite extension. The accepted acceptance text names only "rotated toward
/// curve 2's side," not which of the two possible rotation senses that is in this codebase's
/// coordinate convention (ASSUMPTION-1, TASK-103) — both `+angleRad` and `-angleRad` are tried, and
/// whichever intersection lands nearer `otherPickX,otherPickY` is kept, the same "nearest to the
/// pick" disambiguation `SolveFilletCenter` and every other REQ-103 ambiguity resolution already
/// use. False if neither rotation intersects `other` at all (parallel to it in both senses).
inline bool ChamferRayIntersect(const FilletCurve& c, float fromX, float fromY, float pickX, float pickY,
                                float angleRad, const FilletCurve& other, float otherPickX, float otherPickY,
                                float* outX, float* outY) {
  const float dx = c.bx - c.ax, dy = c.by - c.ay;
  const float len = std::hypot(dx, dy);
  if (len < 1e-9f)
    return false;
  float ux = dx / len, uy = dy / len;
  const float side = (pickX - fromX) * ux + (pickY - fromY) * uy;
  if (side < 0.f) {
    ux = -ux;
    uy = -uy;
  }
  const curveisect::Seg otherLong = FilletLongLine(other.ax, other.ay, other.bx, other.by);
  bool found = false;
  float bestX = 0.f, bestY = 0.f;
  double bestD = -1.0;
  for (float s : {1.f, -1.f}) {
    const float rot = angleRad * s;
    const float rx = ux * std::cos(rot) - uy * std::sin(rot);
    const float ry = ux * std::sin(rot) + uy * std::cos(rot);
    // Double precision (see FilletLongLine's own comment) — the original float32 version of this
    // exact "extend by 1e6" computation is what a real user report traced FILLET's endpoint-vs-arc
    // gap back to.
    constexpr double kHuge = 1.0e6;
    const curveisect::Seg ray{{static_cast<double>(fromX), static_cast<double>(fromY)},
                              {fromX + static_cast<double>(rx) * kHuge, fromY + static_cast<double>(ry) * kHuge}};
    std::vector<curveisect::Hit2> hits;
    curveisect::IntersectSegSeg(ray, otherLong, &hits);
    for (const auto& h : hits) {
      const double d = (h.p.x - otherPickX) * (h.p.x - otherPickX) + (h.p.y - otherPickY) * (h.p.y - otherPickY);
      if (!found || d < bestD) {
        found = true;
        bestD = d;
        bestX = static_cast<float>(h.p.x);
        bestY = static_cast<float>(h.p.y);
      }
    }
  }
  if (!found)
    return false;
  *outX = bestX;
  *outY = bestY;
  return true;
}

/// Optional batched polylines / arcs / ellipses for the viewport (nullptr = none).
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
  const std::vector<float>* polylineVerts = nullptr;
  const std::vector<int>* polylineOffsets = nullptr;
  const std::vector<uint8_t>* polylineClosed = nullptr;
  const std::vector<EntityAttributes>* polylineAttrs = nullptr;
  /// REQ-316 / ADR-047: per-vertex bulge (parallel to polylineVerts, one per vertex). Null or
  /// empty means every polyline segment is straight — the pre-ADR-047 behaviour.
  const std::vector<float>* polylineBulge = nullptr;
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
  const std::vector<CadBlockDefinition>* blockDefs = nullptr;
  const std::vector<CadBlockRef>* blockRefs = nullptr;
  const std::vector<EntityAttributes>* blockRefAttrs = nullptr;
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
  if (e.blockRefs != nullptr && !e.blockRefs->empty())
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
int PickCadTableAt(float wx, float wy, const AppCommandState& cmd, float orthoHalfHeightWorld,
                   float viewportHeightPx);
void CadTableCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,
                                      std::vector<CadTable>* out);
void OpenTableCellEditor(AppCommandState& st, int tableIndex, int cellIndex);
void CommitTableCellEditor(AppCommandState& st, std::vector<std::string>& log);
void CancelTableCellEditor(AppCommandState& st);
void MigrateLegacyAnnotationTables(AppCommandState& st);

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
  /// Circle plane normals, 3 floats each (REQ-312). A paste into paper space flattens them back
  /// to world +Z, the same boundary where z collapses -- the sheet is 2D (ADR-025 (g)).
  std::vector<float>            circleNormals;
  std::vector<CadArc>           arcs;
  std::vector<EntityAttributes> arcAttrs;
  std::vector<CadEllipse>       ellipses;
  std::vector<EntityAttributes> ellAttrs;
  std::vector<int>              polyOffsets; ///< Self-contained offset table (starts with 0).
  std::vector<float>            polyVerts;
  std::vector<float>            polyVertsBulge; ///< REQ-316 / ADR-047: per-vertex bulge, size()/3.
  std::vector<uint8_t>          polyClosed;
  std::vector<EntityAttributes> polyAttrs;
  std::vector<CadAnnotation>    annotations;
  std::vector<EntityAttributes> annotationAttrs;
  std::vector<CadTable>         tables;
  std::vector<EntityAttributes> tableAttrs;
  std::vector<CadBlockRef>      blockRefs;
  std::vector<EntityAttributes> blockRefAttrs;
  std::vector<CadFilledRegion>  filledRegions;     ///< Solid fills enclosed by the copy selection (REQ-038 addendum).
  std::vector<EntityAttributes> filledRegionAttrs;
  /// Source space of the copy. Text height has different units per space (model: plotted-inches × scale = model
  /// units; paper: paper inches), so a cross-space paste scales annotation height by modelUnitsPerPlottedInch.
  bool fromPaper = false;

  bool empty() const {
    return lines.empty() && circlesCxCyZR.empty() && arcs.empty() && ellipses.empty() &&
           (polyOffsets.size() <= 1) && annotations.empty() && tables.empty() && blockRefs.empty() &&
           filledRegions.empty();
  }
};

/// REQ-316 / ADR-047: keep the parallel per-vertex polyline bulge array the right length for the
/// vertex list (3 floats per vertex). Entries added here default to 0 (a straight segment). Call
/// after any operation that changes a polyline's vertex count without maintaining bulges itself.
inline void SyncPolylineBulge(std::vector<float>& bulge, std::size_t vertsFloatCount) {
  bulge.resize(vertsFloatCount / 3, 0.0f);
}

/// REQ-316 / ADR-047: grip index base for a polyline ARC segment's midpoint (bulge) grip. Vertex
/// grips are `0..vertexCount-1`; a bulge grip for segment `s` is `kPolyBulgeGripBase + s`. The base
/// is far above any realistic vertex count so the two grip families never collide.
inline constexpr int kPolyBulgeGripBase = 1 << 20;


/// Geometry-only snapshot for undo/redo.  PDF glTexId is zeroed to avoid stale GPU references.
struct DrawingGeometrySnapshot {
  std::vector<float>            userLinesFlat;
  std::vector<EntityAttributes> userLineAttrs;
  std::vector<float>            userCirclesCxCyZR;
  std::vector<EntityAttributes> userCircleAttrs;
  /// Circle plane normals, 3 floats each (REQ-312) - see AppCommandState::userCircleNormals.
  std::vector<float>            userCircleNormals;
  std::vector<CadArc>           userArcs;
  std::vector<EntityAttributes> userArcAttrs;
  std::vector<CadEllipse>       userEllipses;
  std::vector<EntityAttributes> userEllAttrs;
  std::vector<int>              userPolylineOffsets;
  std::vector<float>            userPolylineVerts;
  /// REQ-316 / ADR-047: per-vertex DXF bulge (tan(theta/4); 0 = straight segment leaving this
  /// vertex). Parallel to the vertex list: size() == userPolylineVerts.size() / 3.
  std::vector<float>            userPolylineVertsBulge;
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
  /// B-rep solids (REQ-313 / ADR-045). Shared, not copied — see CadSolidPtr's note.
  std::vector<CadSolidPtr>      cadSolids;
  std::vector<EntityAttributes> cadSolidAttrs;
  std::vector<CadTable>         cadTables;       ///< Drawing TABLE entities (REQ-148).
  std::vector<EntityAttributes> cadTableAttrs;
  std::vector<CadBlockDefinition> blockDefs;
  std::vector<CadBlockRef>        cadBlockRefs;
  std::vector<EntityAttributes>   cadBlockRefAttrs;
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
  DimensionStyle              dimensionStyle = DimensionStyles::Default();
  std::vector<PdfAttachment>    pdfAttachments;
  std::vector<PaperLayout>      paperLayouts;  ///< Paper layouts incl. native paper geometry (REQ-037/038) — undoable.
  double worldDocumentOriginX = 0.0;
  double worldDocumentOriginY = 0.0;
  std::string description;
};


/// A UCS saved under a name (`UCS Named Save`), persisted with the drawing (REQ-154).
///
/// Deliberately a plain name + frame pair: a named UCS *is* those two things, and giving it any
/// more (a description, an id, a per-viewport binding) would be inventing scope the requirement
/// does not have.
struct NamedUcs {
  std::string name;  ///< As typed, for display; matched case-insensitively.
  ucs::Ucs frame;
};

/// One saved view (REQ-106): where the camera is, which way it looks, and which coordinate frame
/// was active.
///
/// The UCS is part of the record, not an afterthought — REQ-106's acceptance says a named view
/// "restores camera position/target/UCS exactly". A view saved while working to a lot line is
/// useless if restoring it puts the camera back but leaves you in the world frame, because the
/// coordinates you then type mean something different from the ones you typed when you saved it.
///
/// Pan/zoom/azimuth/elevation are exactly the four things `AppCommandState` uses to build a
/// `Camera` (see CadViewCamera), so this stores the camera's inputs rather than a derived matrix —
/// one source of truth, and a saved view cannot drift from what the live view would do with the
/// same numbers.
struct NamedView {
  std::string name;  ///< As typed, for display; matched case-insensitively, like NamedUcs.
  double panX = 0.0;
  double panY = 0.0;
  double panZ = 0.0;
  float zoom = 1.f;
  float azimuthDeg = 0.f;
  float elevationDeg = 90.f;
  float rollDeg = 0.f;  ///< Screen roll (#153); nonzero only for a view saved on a tilted-UCS PLAN.
  /// Projection travels with the view (REQ-309). Without these a view saved in perspective would
  /// restore as orthographic — the same silent-mismatch reason the UCS is stored here rather than
  /// left to whatever happens to be active at restore time.
  Camera::Projection projection = Camera::Projection::Orthographic;
  float fovDeg = kDefaultFovDeg;
  ucs::Ucs ucs;
};

/// How many previous UCSs `UCS Previous` can step back through.
///
/// Bounded rather than unbounded because the stack is pushed on every UCS change and would
/// otherwise grow for the life of the session. Ten matches the depth AutoCAD documents.
constexpr size_t kUcsPreviousDepth = 10;

/// Snapshot of all per-drawing data.  AppCommandState holds the live (active-tab) copy directly;
/// switching tabs saves the active fields here and restores the target tab's snapshot.
struct DrawingDocument {
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
  /// The UCS is per drawing, not per session (REQ-154): switching tabs must not carry one drawing's
  /// coordinate frame into another's, which is the "UCS state does not leak between viewports"
  /// condition in as strong a form as a one-model-view-per-tab application can state it.
  ucs::Ucs activeUcs;
  std::vector<ucs::Ucs> ucsPrevious;
  std::vector<NamedUcs> ucsNamed;
  std::vector<NamedView> namedViews;
  std::string activeViewName;
  bool ucsFollow = false;
  double worldDocumentOriginX = 0.0;
  double worldDocumentOriginY = 0.0;
  /// Per-drawing entity-id counter (REQ-076). Saved/restored with the tab so two open drawings
  /// number independently; see AppCommandState::nextEntityId for why undo never rewinds it.
  std::uint64_t nextEntityId = 1;

  std::vector<float>            userLinesFlat;
  std::vector<EntityAttributes> userLineAttrs;
  std::vector<float>            userCirclesCxCyZR;
  std::vector<EntityAttributes> userCircleAttrs;
  /// Circle plane normals, 3 floats each (REQ-312) - see AppCommandState::userCircleNormals.
  std::vector<float>            userCircleNormals;
  std::vector<CadArc>           userArcs;
  std::vector<EntityAttributes> userArcAttrs;
  std::vector<CadEllipse>       userEllipses;
  std::vector<EntityAttributes> userEllAttrs;
  std::vector<int>              userPolylineOffsets;
  std::vector<float>            userPolylineVerts;
  /// REQ-316 / ADR-047: per-vertex DXF bulge (tan(theta/4); 0 = straight segment leaving this
  /// vertex). Parallel to the vertex list: size() == userPolylineVerts.size() / 3.
  std::vector<float>            userPolylineVertsBulge;
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
  std::vector<CadSolidPtr>      cadSolids;         ///< B-rep solids (REQ-313); shared, not copied.
  std::vector<EntityAttributes> cadSolidAttrs;
  std::vector<CadTable>         cadTables;         ///< Drawing TABLE (REQ-148).
  std::vector<EntityAttributes> cadTableAttrs;
  std::vector<CadBlockDefinition> blockDefs;
  std::vector<CadBlockRef>        cadBlockRefs;
  std::vector<EntityAttributes>   cadBlockRefAttrs;
  std::vector<SurveyPoint>      surveyPoints;
  std::vector<PointGroup>       pointGroups;            ///< Named point groups (REQ-067).
  std::vector<int>              selectedSurveyPointIndices;
  std::vector<CadLayerRow>      drawingLayerTable;
  std::vector<TextStyle>        textStyles;             ///< Named text styles (REQ-044).
  std::vector<SurfaceStyle>     surfaceStyles;          ///< Named surface styles (REQ-070).
  DimensionStyle              dimensionStyle = DimensionStyles::Default();
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
// REQ-307 (GitHub #106): Enter acting on the paper-space MOVE/COPY/DELETE selection step. See the
// definitions in CadCommands.cpp for why these are free functions rather than inline logic.
void ProcessPaperMoveWaitingSelectionEnter(AppCommandState& st, std::vector<std::string>& log);
void ProcessPaperDeleteWaitingSelectionEnter(AppCommandState& st, std::vector<std::string>& log);

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
bool PickPaperEntityAt(const PaperLayout& L, float x, float y, float tolIn, PaperEntityRef* out,
                       const std::vector<CadBlockDefinition>* blockDefs = nullptr);
void TogglePaperEntitySelection(AppCommandState& st, PaperEntityRef ref, bool additive);
void DeleteSelectedPaperEntities(AppCommandState& st, std::vector<std::string>& log);
void TranslateSelectedPaperEntities(AppCommandState& st, float dxIn, float dyIn, bool copy,
                                    std::vector<std::string>& log);
void RotateSelectedPaperEntities(AppCommandState& st, float baseX, float baseY, float angRad,
                                 std::vector<std::string>& log);
/// REQ-103 MIRROR, pure-paper-space path. Always duplicates and keeps the source (see the
/// definition's comment for why there is no erase-source toggle here).
void MirrorSelectedPaperEntities(AppCommandState& st, float x0In, float y0In, float x1In, float y1In,
                                 std::vector<std::string>& log);
/// REQ-103 LENGTHEN, pure-paper-space path. Single pick, single apply (no erase/duplicate step) —
/// see the definition's comment for the mode/value simplification.
bool ApplyLengthenToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, float pickXIn, float pickYIn,
                                std::vector<std::string>& log);
/// REQ-103 EXTEND, pure-paper-space path — unlike LENGTHEN's, needs no typed value, so it is built
/// (not simplified away) as a real two-phase pick flow; see `paperExtendPhase`'s comment.
bool ApplyExtendToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, float pickXIn, float pickYIn,
                              std::vector<std::string>& log);
/// REQ-103 BREAK, pure-paper-space path — a pure two-phase click flow like paper EXTEND, needing no
/// typed value; see `paperBreakPhase`'s comment. `p1` is the already-resolved first break point
/// (from the entity-selecting pick); this call resolves the second point at (pickXIn,pickYIn) itself.
bool ApplyBreakToPaperEntity(AppCommandState& st, const PaperEntityRef& ref, const BreakPoint& p1,
                             float pickXIn, float pickYIn, std::vector<std::string>& log);
/// REQ-103 STRETCH, pure-paper-space path. Translates every entity in \c st.selectedPaperEntities
/// by (dxIn,dyIn); if \c st.paperSelBoxLastValid, only each entity's definition points that fall
/// inside the last-captured box move (true partial stretch for Line/Polyline/Arc — Arc via
/// \c RecomputeArcFromEndpoints, shared with the model-space path), otherwise every entity
/// translates as a whole (degraded MOVE-equivalent, matching a non-crossing pickfirst set).
void ApplyStretchToPaperSelection(AppCommandState& st, float dxIn, float dyIn,
                                  std::vector<std::string>& log);
/// REQ-103 FILLET, pure-paper-space path (step 6a) — full parity with model space, a two-phase
/// click flow like paper EXTEND/BREAK needing no typed value at the pick itself (radius/trim mode
/// come from the model-space command line, the same way paper LENGTHEN reuses model-set values).
/// \p first is the entity+segment latched by the first click (\c st.paperFilletFirstEntity et al.);
/// \p second is the entity+segment resolved at the second click. Same Case A (same polyline,
/// adjacent segments) / Case B (two different curves, or a polyline's own end segment) split as the
/// model-space path, including the parallel-lines-semicircle special case.
bool ApplyFilletToPaperEntities(AppCommandState& st, const PaperEntityRef& first, int firstPolySeg,
                                float firstPickX, float firstPickY, const PaperEntityRef& second,
                                int secondPolySeg, float pickX, float pickY, std::vector<std::string>& log);
/// Paper-space equivalent of the model-space `FilletEligibility` (CadCommands.cpp) — is `ref`
/// (picked at pickX,pickY) an eligible FILLET curve, and if it's a Polyline, which edge (0-based)?
/// Declared here (not just in the .cpp) so CadUi.cpp's paper click block can resolve BOTH picks the
/// same way it already calls `PickPaperEntityAt`.
bool PaperFilletEligibility(const PaperLayout& L, const PaperEntityRef& ref, float pickX, float pickY,
                            int* outPolySeg, std::vector<std::string>& log);
/// REQ-103 CHAMFER, pure-paper-space path (step 6b) — same shape as `ApplyFilletToPaperEntities`.
bool ApplyChamferToPaperEntities(AppCommandState& st, const PaperEntityRef& first, int firstPolySeg,
                                 float firstPickX, float firstPickY, const PaperEntityRef& second,
                                 int secondPolySeg, float pickX, float pickY, std::vector<std::string>& log);
/// Paper-space equivalent of the model-space `ChamferEligibility` (CadCommands.cpp) — same shape as
/// `PaperFilletEligibility`, minus the Arc case (CHAMFER has none).
bool PaperChamferEligibility(const PaperLayout& L, const PaperEntityRef& ref, float pickX, float pickY,
                             int* outPolySeg, std::vector<std::string>& log);
/// Projects (px,py) onto model-space entity \p e, filling \p out — "the closest point ON this
/// entity", which \c PickClosestCadEntity does not answer (it names the entity and a distance).
/// BREAK's second pick commits this point, and BREAK's live preview must resolve the cursor
/// through the very same call or it previews a span other than the one about to be removed
/// (TASK-101). Declared here for the same reason \c ClosestPointOnPaperEntity below is.
bool ClosestPointOnEntity(const AppCommandState& st, const SelectedEntity& e, float px, float py,
                          BreakPoint* out);
/// Paper-space equivalent of \c ClosestPointOnEntity (CadCommands.cpp) — projects (px,py) onto
/// paper entity \p ref, filling \p out. Declared here (not just in the .cpp) so CadUi.cpp's paper
/// click block can resolve BREAK's first point the same way it already calls \c PickPaperEntityAt.
bool ClosestPointOnPaperEntity(const PaperLayout& L, const PaperEntityRef& ref, float px, float py,
                               BreakPoint* out);
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
constexpr int kRibbonTabCount    = 7;
/// REQ-143: contextual TIN Surface tab. Not counted in \c kRibbonTabCount and not written to prefs.
constexpr int kRibbonTabSurfaceCtx = 7;
/// REQ-153: contextual SURVEY Point(s) tab. Session-only, not a prefs slot.
constexpr int kRibbonTabSurveyPointCtx = 8;
/// Contextual Block Editor tab while BEDIT is open. Not counted in \c kRibbonTabCount / prefs.
constexpr int kRibbonTabBlockEditor = 9;

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
    /// ARRAY: rectangular or polar patterns of duplicated copies of the selection (REQ-305, issue #87).
    Array,
    /// MIRROR: reflect the selection across a two-point mirror line (REQ-103 step 1). Default
    /// behavior duplicates-then-reflects (source kept); an "Erase source objects?" prompt lets the
    /// user opt into replacing the source instead, mirroring AutoCAD's own default.
    Mirror,
    /// LENGTHEN: change a Line/open-Polyline/non-full-circle-Arc's length at the end nearest each
    /// pick, by DElta / Percent / Total / DYnamic (REQ-103 step 2). One entity per pick, loops back
    /// to "select object" until Enter/Esc — TRIM/OFFSET's target-picking shape, not a selection.
    Lengthen,
    /// EXTEND: select boundary edges, then stretch a Line/open-Polyline/non-full-circle-Arc's end
    /// nearest each pick out to the nearest boundary (REQ-103 step 3) — TRIM's direct inverse,
    /// copy-adapting its boundary-edge-selection shape (own state, not shared code).
    Extend,
    /// BREAK: pick an entity (the pick doubles as break point 1), then a second point; the material
    /// between them is removed (REQ-103 step 4). Eligible: Line, Circle, Arc (any sweep), open and
    /// closed Polyline. Two picks identical in position opens a closed entity at that point with
    /// nothing removed ("break at point").
    Break,
    /// STRETCH: crossing/window box-select, then base+destination; only the definition points
    /// (endpoints/vertices/center) that fell inside the box move (REQ-103 step 5). Arc endpoints
    /// stretch with true AutoCAD parity (center/radius recomputed, included angle preserved).
    /// Single-shot, one undo step for the whole apply — MOVE/ROTATE/SCALE's shape, not a loop.
    Stretch,
    /// FILLET: pick two curves (Line, non-full-circle Arc, or an open/closed Polyline segment);
    /// constructs a tangent arc between them at the persisted radius and trims/extends each to its
    /// tangent point (REQ-103 step 6a). Radius-0 and parallel-lines-semicircle are real special
    /// cases. Loops back to "select first object" until Enter/Esc — TRIM/LENGTHEN/EXTEND/BREAK's
    /// per-target shape, one undo step per fillet.
    Fillet,
    /// CHAMFER: pick two curves (Line or an open/closed Polyline segment — Arc excluded, no
    /// standard chamfer-to-arc geometry) and connect them with a straight Line at Distance/Distance
    /// or Distance/Angle from their intersection, trimming/extending each to meet it (REQ-103 step
    /// 6b). Shares FILLET's Case A (same-polyline adjacent segments) / Case B split, its
    /// `cornerTrimMode` toggle, and its per-target looping shape.
    Chamfer,
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
    /// REQ-133: one pick traces a water-drop path on a named surface.
    WaterDrop,
    /// REQ-134: one pick reports the catchment upstream of an outlet.
    Catchment,
    /// REQ-139: one pick swaps an interior TIN edge on a named surface.
    SwapTinEdge,
    /// REQ-144: one pick adds a definition vertex on a named TIN (Z = work plane).
    AddTinPoint,
    /// REQ-144: one pick deletes the nearest definition point on a named TIN.
    DelTinPoint,
    /// REQ-150: two picks move a definition point (from, then to).
    MoveTinPoint,
    /// REQ-150: one pick deletes the nearest interior TIN edge.
    DelTinLine,
    /// REQ-145: two picks sample a named surface into a session Quick Profile graph.
    QuickProfile,
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
    /// UCS: define or restore the active coordinate system (REQ-154). One command with a keyword
    /// option set, driven through \ref ucsPhase — the phases exist because several options need
    /// further picks (an origin, three points, an angle, a name) after the keyword.
    Ucs,
    /// PLAN: orient the view to the XY plane of a coordinate system, WITHOUT changing the UCS
    /// (REQ-154). Autodesk documents that distinction explicitly and it is the whole point of the
    /// command being separate from UCS.
    Plan,
    /// INSERT dialog (GitHub issue #124): pick a definition, then optional on-screen point/scale/rotation.
    InsertBlock,
    /// The seven B-rep primitives, driven as a prompted command (REQ-313 as amended): pick or type
    /// the base point, then set each named dimension by its letter — `R` radius, `H` height, and so
    /// on — before Enter creates it.
    ///
    /// **One Kind for all seven**, not seven Kinds. They differ only in which named parameters they
    /// carry, and that difference is data (\ref SolidParamSpec), not control flow — seven near-identical
    /// state machines is exactly the duplication that lets one of them quietly miss a fix.
    Solid,
    /// EXTRUDE (REQ-314 / ADR-046, GitHub #147): select closed polylines / circles, then give a
    /// height — typed, or dragged from the cursor with a live ghost. Its own Kind because it has a
    /// select-objects phase and then a height phase, neither of which the primitive `Solid` command
    /// has.
    Extrude,
    /// REVOLVE (REQ-314 / ADR-046, GitHub #147): select closed polylines / circles, pick the two
    /// ends of the revolve axis, then an angle in degrees.
    Revolve,
    /// SLICE (REQ-314 / ADR-046, GitHub #147): select solids, define a cutting plane with three
    /// points, then pick which side to keep (or both).
    Slice,
    /// LOFT (REQ-315 / ADR-048, GitHub #241): select two or more closed polylines / circles in
    /// lofting order, Enter to skin a solid through them (SurfaceKind::Nurbs side faces). One
    /// select-objects phase and nothing else — no height, no axis.
    Loft,
    /// SWEEP (REQ-315 / ADR-048, GitHub #241): select one closed profile and one line-or-arc path,
    /// Enter to sweep the profile along the path. One select-objects phase; the closed loop is the
    /// profile and the open curve is the path.
    Sweep,
    /// UNION / SUBTRACT / INTERSECT (REQ-314 / ADR-046, GitHub #147): SUBTRACT prompts twice —
    /// solids to subtract from, then solids to subtract; UNION and INTERSECT prompt once.
    Boolean,
    /// REQ-317 POLYSOLID: a wall swept along a picked path. Its OWN Kind, unlike the seven
    /// primitives that share one - a polysolid is built from a PATH rather than from a fixed set of
    /// named dimensions, so it has different state and a different state machine, and folding it
    /// into `Solid` would mean a table with a variable-length entry no other row uses.
    Polysolid,
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
    case Kind::Loft:             return "LOFT";
    case Kind::Sweep:            return "SWEEP";
    case Kind::Boolean:          return "BOOLEAN";
    case Kind::Polysolid:          return "POLYSOLID";  // REQ-317
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
  float cmdBarTopYPx = 0.f;           ///< floating bar's top edge this frame (screen px); 0 = not floating/not drawn. NOT persisted — recomputed every frame, and read by the UCS icon so it can stay clear of the bar.
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

    /// Solids in the B-rep profile (REQ-313 / REQ-100). 0 = not the solid profile; at most one
    /// of this, ef meshTriangleCount and ef surfacePointCount is non-zero.
    ///
    /// A profile of its OWN rather than an assumption that the mesh number covers it, for the
    /// same reason the surface profile is not implied by the mesh one: a solid's cost is not one
    /// big indexed upload. It is N stream-uploaded batches plus N edge batches plus a per-frame
    /// cache lookup, and #120's "do not regenerate a solid's render mesh every frame" is a claim
    /// about exactly that per-frame work. Measuring it is the only way to know.
    int solidCount = 0;
    /// Total tessellated triangles across the solid scene, filled in when the scene is built.
    int solidTriangleCount = 0;

    std::vector<float> savedPolyVerts;
    std::vector<int> savedPolyOffsets;
    std::vector<std::uint8_t> savedPolyClosed;
    std::vector<EntityAttributes> savedPolyAttrs;
    std::vector<CadSurface> savedSurfaces;            ///< restored verbatim after a surface run
    std::vector<EntityAttributes> savedSurfaceAttrs;
    std::vector<std::shared_ptr<const CadMesh>> savedMeshes;  ///< restored verbatim after a mesh run
    std::vector<EntityAttributes> savedMeshAttrs;
    std::vector<CadSolidPtr> savedSolids;             ///< restored verbatim after a solid run
    std::vector<EntityAttributes> savedSolidAttrs;
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
  bool objectSnapPerpendicular = true;
  bool objectSnapSurveyPoint = true;
  bool objectSnapGeometricCenter = true;
  /// Snap where two objects genuinely meet in 3D (REQ-062).
  bool objectSnapIntersection = true;
  /// Snap where two objects only *appear* to meet in the current view (REQ-062). Off by default,
  /// as in AutoCAD: it fires on objects that do not touch, which is surprising unless asked for.
  bool objectSnapApparentIntersection = false;
  bool objectSnapSurface = true;
  /// Snap to a B-rep solid's faces and edges (REQ-313). ONE toggle for both kinds, not two: they
  /// are the two halves of "snap to a solid", and no requirement asks to enable one without the
  /// other, so a second preference would be an unearned option (REQ-301). A solid's VERTICES and its
  /// edge MIDPOINTS answer to the ordinary Endpoint and Midpoint toggles instead — they are exactly
  /// what those snaps already mean, and a user who has Endpoint on expects a corner to snap.
  bool objectSnapSolid = true;
  /// Isolines drawn around a curved solid face, per full turn (AutoCAD calls this ISOLINES).
  /// Per drawing, persisted in `.gs` and in user preferences like every other display setting.
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
  float arcAz = 0.f, arcBz = 0.f;   ///< work-plane elevation of each pick (REQ-312), as circleCz

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
  /// POLAR tracking (issue #154, REQ-154). Mutually exclusive with \ref orthoMode, as in AutoCAD:
  /// enabling one clears the other. When on, rubber-band picks snap to the nearest polar ray —
  /// a multiple of \ref polarIncrementDeg, or one of \ref polarExtraAnglesDeg — measured in the
  /// active UCS's XY plane from its +X, so a rotated frame rotates the rays with it.
  bool polarMode = false;
  double polarIncrementDeg = 90.0;
  std::vector<double> polarExtraAnglesDeg;
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
  /// Screen roll about the view axis (GitHub #153). Zero for plan view, every ViewCube orientation
  /// and every hand orbit; set only by `PLAN` of a UCS whose Z is tilted off world +Z, so that the
  /// UCS +Y comes out up the screen. Persisted per drawing alongside azimuth/elevation.
  float viewportRollDeg = 0.f;

  /// How the view projects (REQ-309 / D-2026-08-31-g). `Camera` has implemented both projections
  /// since REQ-058; these two fields are what finally *select* between them — before them nothing
  /// in the application ever assigned `Camera::projection`, so perspective was unreachable.
  ///
  /// They sit here, beside azimuth/elevation, because they share exactly that lifetime: per
  /// drawing, saved per tab, persisted in `.gs`, restored by a named view. **Orthographic is the
  /// default everywhere**, which is what keeps REQ-058's plan-view parity guarantee intact.
  Camera::Projection viewportProjection = Camera::Projection::Orthographic;
  float viewportFovDeg = kDefaultFovDeg;  ///< Perspective vertical FOV; ignored when orthographic.

  /// ViewCube orientation animation (REQ-059). A face/arrow/home press sets a target and the view
  /// eases to it over \ref kViewAnimSeconds instead of snapping, so the user keeps their bearings —
  /// a hard jump makes it easy to lose track of which way the model turned. Orbiting by hand
  /// cancels any animation in flight so the drag is never fighting an interpolation.
  bool  viewAnimActive = false;
  float viewAnimFromAz = 0.f, viewAnimFromEl = 90.f, viewAnimFromRoll = 0.f;
  float viewAnimToAz = 0.f, viewAnimToEl = 90.f, viewAnimToRoll = 0.f;
  float viewAnimT = 0.f;  ///< 0..1 progress.

  /// The active User Coordinate System (REQ-058 / ADR-025 (e); REQ-154, GitHub #126).
  ///
  /// One frame, replacing the origin/normal/azimuth triple that stood in for it before the UCS
  /// command existed: that shape could express a plane and a spin but not a basis, so nothing could
  /// ask it "which way is UCS +X?". Its XY plane is the work plane a click resolves against, and its
  /// axes are the frame typed coordinates, ORTHO and the grid are interpreted in. The default is the
  /// WCS, under which every pre-UCS behaviour is unchanged.
  ///
  /// **It never moves geometry.** Entities stay in WCS; this only changes how the user's input is
  /// read and how coordinates are reported back.
  ucs::Ucs activeUcs;

  /// `UCS Previous` history, oldest first. Bounded by \ref kUcsPreviousDepth — AutoCAD keeps a
  /// limited stack too, and an unbounded one would grow for the life of the session.
  std::vector<ucs::Ucs> ucsPrevious;

  /// Saved UCS definitions (`UCS Named`), persisted with the drawing. "World" is reserved and is
  /// never stored here — it is always available and can never be redefined or deleted.
  std::vector<NamedUcs> ucsNamed;
  /// Saved views for this drawing (REQ-106), and which one the view currently matches. The name is
  /// empty whenever the camera has been moved since a restore — that is the "Unsaved View" the
  /// ribbon shows, and it is a statement about the CAMERA, not about whether the drawing is dirty.
  std::vector<NamedView> namedViews;
  std::string activeViewName;

  /// UCSFOLLOW: when set, any change to \ref activeUcs immediately switches the view to a PLAN view
  /// of the new UCS. 0 leaves the camera alone. Per drawing tab, which is as per-viewport as this
  /// application currently gets — there is one model view per tab (see the requirement's note).
  bool ucsFollow = false;

  /// Where the UCS command is in its prompt sequence. Several options need further picks after the
  /// keyword, and modelling that as an explicit phase (rather than as flags) keeps every prompt's
  /// valid input in one place — the same shape ARRAY and the other multi-step commands use.
  enum class UcsPhase : uint8_t {
    Idle,
    WaitOriginOrOption,  ///< the top-level prompt: a point, or one of the keywords
    WaitXAxisPoint,      ///< after an origin: a point on the new +X, or blank to accept origin-only
    WaitXyPoint,         ///< after an X point: a point in the +Y half of the XY plane, or blank
    WaitRotationAngle,   ///< X / Y / Z: degrees, right-hand rule about \ref ucsRotationAxis
    WaitZAxisOrigin,     ///< ZAxis: the origin
    WaitRotationAngleP1, ///< X/Y/Z + 2P: first point of the pair that defines the angle
    WaitRotationAngleP2, ///< X/Y/Z + 2P: second point; the angle is p1->p2 in the rotation plane
    WaitZAxisPoint,      ///< ZAxis: a point on the positive Z
    WaitObjectPick,      ///< Object: click an entity to align to
    WaitNamedName,       ///< Named: the name to save the current frame under
  } ucsPhase = UcsPhase::Idle;

  /// 'X', 'Y' or 'Z' — the axis \ref UcsPhase::WaitRotationAngle will rotate about.
  char ucsRotationAxis = 'Z';

  /// Picks accumulated across the multi-point UCS options, in WORLD coordinates (not storage-local:
  /// a coordinate frame is a world-space object, and keeping it local would make it move whenever
  /// the document origin rebased).
  ray3d::Vec3 ucsPendingOrigin{0.0, 0.0, 0.0};
  ray3d::Vec3 ucsPendingXAxisPoint{0.0, 0.0, 0.0};
  /// First of the two picks that define a rotation angle (`UCS Z` then `2P`). Kept separate from the
  /// two above because it means something different — not a corner of the new frame, but one end of
  /// a direction being measured — and sharing a field would make that read as the same thing.
  ray3d::Vec3 ucsAngleBasePoint{0.0, 0.0, 0.0};

  /// PLAN's own prompt phase. Kept separate from \ref ucsPhase so neither command can be nudged
  /// into the other's state machine.
  enum class PlanPhase : uint8_t { Idle, WaitOption, WaitNamedName } planPhase = PlanPhase::Idle;
  /// Elevation of the cursor's work-plane intersection, published alongside the existing
  /// \c uiCursorWorldX/Y so readouts and future 3D-aware commands can see it.
  float uiCursorWorldZ = 0.f;

  /// The Z of the point CURRENTLY being resolved, when that point carries its own (REQ-154).
  ///
  /// On a UCS parallel to world XY every point on the work plane shares one elevation, so the UCS
  /// origin's Z describes them all — which is why a single constant sufficed before. **A tilted UCS
  /// breaks that**: the plane's Z varies across it, and a point's elevation is a property of the
  /// point, not of the plane. Rather than teach ~29 geometry-creation sites about the UCS, the two
  /// places a point is actually resolved publish its Z here and \ref CadWorkPlaneElevation reads it.
  ///
  /// Set at point resolution (a viewport click's ray x plane hit, or a typed UCS coordinate mapped
  /// through the frame) and cleared when point entry ends, so a stale value can never leak into a
  /// later command. Under a flat UCS the published value equals the origin's Z, so this channel
  /// changes nothing there.
  bool  resolvedPointZValid = false;
  float resolvedPointZ = 0.f;
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

  // --- The prompted solid-primitive command (REQ-313 as amended) ---------------------------------

  /// The most named dimensions any primitive has (PYRAMID: sides, base radius, top radius, height).
  static constexpr int kMaxSolidParams = 4;

  enum class SolidPhase {
    WaitBasePoint,   ///< Click, or type X,Y[,Z]. The base centre — or the centre, for sphere/torus.
    WaitParameters,  ///< Base set: type a letter + value, a bare value for the next unset one, or Enter.
  } solidPhase = SolidPhase::WaitBasePoint;

  /// Which primitive is being built. `None` means no solid command is running.
  brep::PrimitiveKind solidKind = brep::PrimitiveKind::None;
  /// The base point, in STORAGE coordinates (X/Y local, Z absolute) — the same convention the store
  /// itself uses, so the commit needs no second conversion.
  ray3d::Vec3 solidBase;
  double solidParamValue[kMaxSolidParams] = {0.0, 0.0, 0.0, 0.0};
  bool solidParamSet[kMaxSolidParams] = {false, false, false, false};

  // --- What the cursor is currently worth, republished every frame -------------------------------
  //
  // Resolved in the viewport, where the pick RAY lives, and read by BOTH the live preview and the
  // click that commits it. One value, two consumers: a preview computed separately from the commit
  // is a preview that eventually shows a solid the click does not build, which is worse than no
  // preview at all.
  bool solidPickValid = false;
  double solidPickA = 0.0;      ///< radius, height, or the corner's in-plane X offset.
  double solidPickB = 0.0;      ///< the corner's in-plane Y offset (CornerXY only).
  double solidPickAngleRad = 0.0;  ///< direction from the base point — the pyramid's base rotation.

  /// PYRAMID base rotation, radians in the work plane, taken from the radius pick's direction so the
  /// base turns with the cursor the way AutoCAD's does.
  double solidBaseAngleRad = 0.0;
  /// PYRAMID: is the given radius the polygon's circumradius (inscribed in that circle) or its
  /// apothem (circumscribed about it)? AutoCAD's default is circumscribed, and `I` toggles.
  bool solidInscribed = false;
  /// BOX / WEDGE only: the base point is the first CORNER, not the centre, so the commit shifts the
  /// frame origin to the midpoint of the two corners. Signed, in the work plane's own axes — the
  /// sign is what says which way the box was dragged, which `length` and `width` cannot.
  bool solidBaseIsCorner = false;
  double solidCornerDx = 0.0;
  double solidCornerDy = 0.0;
  /// A letter typed on its own arms the parameter and waits for the value on the next line, which is
  /// what makes `R` then `4` work as well as `R 4`. -1 = nothing armed.
  int solidPendingParam = -1;
  /// The armed parameter was reached through `D`, so the next value is a DIAMETER and is halved
  /// into the radius. Halved in exactly one place, so a diameter can never reach the kernel as a
  /// radius.
  bool solidPendingIsDiameter = false;

  // --- The EXTRUDE command (REQ-314 / ADR-046, GitHub #147) --------------------------------------

  enum class ExtrudePhase {
    SelectProfiles,  ///< Accumulate a selection of closed polylines / circles; Enter confirms.
    WaitHeight,      ///< Profiles gathered: type a height, or move the cursor and click.
  } extrudePhase = ExtrudePhase::SelectProfiles;

  /// The profiles gathered when the command left \ref ExtrudePhase::SelectProfiles, in STORAGE
  /// coordinates — kept so the live ghost and the commit build from exactly the same input, the
  /// same one-source-of-truth rule the prompted solid command follows.
  std::vector<brep::Profile> extrudeProfiles;
  /// What the cursor is currently worth as a height, republished every frame from the viewport (the
  /// only place the pick ray lives). Read by the ghost and by the click that commits it. Signed
  /// along the first profile's plane normal; positive is the +normal side.
  bool extrudeHeightPickValid = false;
  double extrudeHeightPick = 0.0;

  // --- The REVOLVE command (REQ-314 / ADR-046 increment 2, GitHub #147) --------------------------

  enum class RevolvePhase {
    SelectProfiles,  ///< Accumulate a selection of closed polylines / circles; Enter confirms.
    WaitAxisStart,   ///< Pick or type the first point of the revolve axis.
    WaitAxisEnd,     ///< Pick or type the second point of the revolve axis.
    WaitAngle,       ///< Type the angle in degrees; Enter takes the default (a full turn).
  } revolvePhase = RevolvePhase::SelectProfiles;

  std::vector<brep::Profile> revolveProfiles;
  ray3d::Vec3 revolveAxisStart;
  bool revolveAxisStartSet = false;
  ray3d::Vec3 revolveAxisEnd;
  double revolveAngleDeg = 360.0;

  // --- The SLICE command (REQ-314 / ADR-046 increment 3, GitHub #147) ---------------------------

  enum class SlicePhase {
    SelectSolids,   ///< Accumulate a selection of solids; Enter confirms.
    WaitP1,         ///< First of three points that define the cutting plane.
    WaitP2,
    WaitP3,
    WaitKeepSide,   ///< Pick a point on the side to keep, or [B]oth.
  } slicePhase = SlicePhase::SelectSolids;

  // --- The LOFT command (REQ-315 / ADR-048, GitHub #241) ---------------------------------------

  enum class LoftPhase {
    SelectProfiles,  ///< Accumulate an ORDERED selection of closed polylines / circles; Enter builds.
  } loftPhase = LoftPhase::SelectProfiles;

  // --- The SWEEP command (REQ-315 / ADR-048, GitHub #241) --------------------------------------

  enum class SweepPhase {
    SelectInputs,  ///< Accumulate a closed profile and a line/arc path; Enter builds.
  } sweepPhase = SweepPhase::SelectInputs;

  double sweepTwistDeg = 0.0;      ///< SWEEP `T` keyword: constant twist over the path, degrees.
  bool sweepAlignToPath = true;    ///< SWEEP `A` keyword: stand the profile normal to the path.

  std::vector<int> sliceSolidIndices;  ///< indices into cadSolids, gathered when SelectSolids ends
  ray3d::Vec3 sliceP1;
  ray3d::Vec3 sliceP2;
  ray3d::Vec3 sliceP3;

  // --- The prompted UNION / SUBTRACT / INTERSECT command (REQ-314 / ADR-046, GitHub #147) --------

  enum class BooleanPhase {
    SelectOperands,     ///< UNION / INTERSECT: one "select objects" step.
    SelectMinuend,      ///< SUBTRACT step 1: the solids to subtract from.
    SelectSubtrahend,   ///< SUBTRACT step 2: the solids to subtract.
  } booleanPhase = BooleanPhase::SelectOperands;

  /// 0 = Union, 1 = Subtract, 2 = Intersect (matches CadBooleanOp; kept as int so the enum stays in
  /// the .cpp).
  int booleanOp = 0;
  std::vector<int> booleanMinuend;  ///< SUBTRACT: cadSolids indices gathered by step 1.
  // --- REQ-317 POLYSOLID: a wall swept along a picked path ---------------------------------------

  enum class PolysolidPhase {
    WaitFirstPoint,  ///< the start of the run, or `O` to convert something already drawn
    WaitObject,      ///< `O` was given: the next click names the entity to sweep along
    WaitNextPoint    ///< a run is under way; each further point commits a segment
  } polysolidPhase = PolysolidPhase::WaitFirstPoint;

  /// The path being drawn, in \ref polysolidBase's work-plane coordinates — the same form
  /// `brep::MakePolysolid` takes, so the command never holds a second representation of it.
  brep::Path polysolidPath;
  /// The first point, in storage coordinates: the origin of the placement frame.
  ray3d::Vec3 polysolidBase{};
  /// `A` draws arc segments and `L` straight ones, exactly as PLINE's own option does.
  bool polysolidArcMode = false;
  /// A letter typed on its own, waiting for its value on the next line (`H` then `4`, as well as
  /// `H 4`). 0 = nothing armed; otherwise the uppercase letter.
  char polysolidPending = 0;

  /// Remembered between invocations, the way AutoCAD remembers PSOLWIDTH and PSOLHEIGHT: a wall is
  /// almost always drawn at the same size as the last one, and re-typing it every time is the
  /// friction that makes a command feel wrong. Saved with the drawing.
  double polysolidWidth = 0.25;
  double polysolidHeight = 4.0;
  brep::Justify polysolidJustify = brep::Justify::Center;

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
  /// Each draft pick keeps the work-plane elevation it was made at (REQ-312).
  ///
  /// A tilted work plane makes Z vary from point to point, and a VERTICAL one makes (x, y) stop
  /// determining Z at all -- two picks on a wall can share an (x, y) and differ only in height. So
  /// the elevation cannot be recovered at commit time from the coordinates; it has to be kept with
  /// the pick that produced it. Under the WCS every one of these is the single commit elevation and
  /// nothing reads them.
  float circleCz = 0.f;
  float c3p1z = 0.f, c3p2z = 0.f;

  /// Each circle: center X, center Y, center Z, radius (world units) — stride 4 (REQ-057 /
  /// ADR-025 (a)). The centre's XYZ is contiguous so it reads like a point; the radius trails it.
  /// Z is absolute (ADR-025 D2). The circle lies in world XY unless `userCircleNormals`
  /// says otherwise (REQ-312), matching CadArc.
  std::vector<float> userCirclesCxCyZR;
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
  std::vector<float> userPolylineVerts;
  /// REQ-316 / ADR-047: per-vertex DXF bulge, parallel to userPolylineVerts (size()/3 entries).
  std::vector<float> userPolylineVertsBulge;
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
  /// REQ-316 / ADR-047: per-draft-vertex bulge, parallel to polylineDraftVerts (one per vertex;
  /// the bulge of the segment LEAVING that vertex). Same length as the vertex count.
  std::vector<float> polylineDraftBulge;
  /// REQ-316: while true, the next POLYLINE segment is a circular arc (keyword `Arc`/`A`; `Line`/`L`
  /// switches back). The arc is tangent to the previous segment unless a radius or included angle
  /// is given for the next pick.
  bool polylineArcMode = false;
  float polylineArcRadius = 0.f;      ///< REQ-316: radius for the next arc segment (0 = unset)
  float polylineArcAngleDeg = 0.f;    ///< REQ-316: included angle (deg) for the next arc segment
  bool polylineArcAngleValid = false; ///< REQ-316: an included angle was typed for the next pick
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
  /// REQ-302: which top-level ribbon tab is showing. Persisted in user prefs, same shape as
  /// \c trimState. Values match \c kRibbonTabHome.. \c kRibbonTabSurvey below; an out-of-range value
  /// loaded from a hand-edited prefs file is clamped back into range rather than left invalid.
  /// REQ-143 / REQ-153 may set this to a contextual tab for the session only.
  int activeRibbonTab = 0;
  /// Permanent tab to restore when the last selected surface is cleared (REQ-143). Session-only.
  int ribbonTabBeforeSurfaceCtx = 0;
  /// True while a surface selection has already switched (or could switch) to the contextual tab.
  bool surfaceContextualRibbonArmed = false;
  /// Permanent tab to restore when the last selected survey point is cleared (REQ-153). Session-only.
  int ribbonTabBeforeSurveyPointCtx = 0;
  /// True while a survey-point selection has armed the SURVEY Point(s) contextual tab.
  bool surveyPointContextualRibbonArmed = false;
  /// Permanent tab to restore when BEDIT closes. Session-only.
  int ribbonTabBeforeBlockEditor = 0;
  bool blockEditorContextualRibbonArmed = false;
  bool blockAuthoringPaletteOpen = false;
  int blockAuthoringPaletteTab = 0;  ///< 0 Parameters, 1 Actions, 2 Parameter Sets, 3 Constraints
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
  bool        authSignOutRequested = false;  ///< set by a Sign Out button (Settings, Start screen, or
                                             ///< the menu-bar account dropdown — REQ-091 amendment)
  bool        showAccountDetailsWindow = false;  ///< REQ-091 amendment: menu-bar "Account Details"
                                                 ///< opens a small read-only placeholder window
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

  /// B-rep solids (REQ-313 / ADR-045). `shared_ptr<const>` for the same reason meshes are: an undo
  /// snapshot shares the payload instead of copying it, and immutability is what makes that safe.
  /// A solid is REPLACED, never written through.
  std::vector<CadSolidPtr> cadSolids;
  std::vector<EntityAttributes> cadSolidAttrs;

  /// The per-solid tessellation cache (#120: "do not regenerate a solid's render mesh every
  /// frame"). Rebuilt by \ref RefreshSolidDisplayGeometry only when a solid or the tessellation
  /// quality has actually changed, and — like the surface display cache it is modelled on (ADR-036
  /// (e)) — deliberately **outside** every undo snapshot, because it is derived from the solids.
  std::vector<CadSolidTessellation> solidDisplayCache;
  /// What the renderer is handed for solids this frame: a small number of COALESCED batches, each the
  /// merged geometry of every visible solid sharing a resolved colour and edge lineweight, already
  /// filtered for layer visibility and object isolation. Turns a per-solid draw call (GitHub #194)
  /// into a per-appearance one. Rebuilt only when \ref solidDisplayAssemblySig changes.
  CadSolidDisplayGeometry solidDisplayGeometry;
  /// A fingerprint of everything \ref RefreshSolidDisplayGeometry's assembly pass reads — the visible
  /// solids in order, their cache buffer sizes, their resolved colours and lineweights, and the
  /// regen count. Unchanged means the merged buffers in \ref solidDisplayGeometry are still current
  /// and the (now vertex-copying) concatenation can be skipped — the §11 invariant 7 early-out, so
  /// an orbit that changes only the camera does not re-merge a million triangles every frame.
  std::uint64_t solidDisplayAssemblySig = 0;
  /// How many times \ref RefreshSolidDisplayGeometry has actually (re)tessellated a solid — the solid
  /// twin of \ref surfaceDisplayRegenCount. #120 asks that the render mesh not be regenerated every
  /// frame; `BENCH SOLID` takes a baseline at the first timed frame and this must not grow during a
  /// scripted orbit.
  std::uint64_t solidDisplayRegenCount = 0;

  /// Drawing TABLE entities (REQ-148 / D-2026-08-28-i). Rigid body: insertion, size, rotation, cells.
  std::vector<CadTable> cadTables;
  std::vector<EntityAttributes> cadTableAttrs;

  std::vector<CadBlockDefinition> blockDefs;
  std::vector<CadBlockRef> cadBlockRefs;
  std::vector<EntityAttributes> cadBlockRefAttrs;
  std::string blockEditorName;
  CadBlockDefinition blockEditorSnapshot;
  bool blockEditorDirty = false;
  bool blockEditPickerOpen = false;
  char blockEditPickerName[256]{};
  /// In-place block editing (REQ-107 / ADR-043). While active, the model arrays hold the
  /// definition's primitive geometry in local coords and \c blockEditModelStash holds the real
  /// drawing. Session-only — never in \ref DrawingDocument, never in `.gs`.
  bool blockEditActive = false;
  DrawingGeometrySnapshot blockEditModelStash;
  /// \c cadGpuRevision at the last clean point of the session (enter / BSAVE). A different value
  /// means unsaved edits — drives the BCLOSE Save/Don't-Save/Cancel prompt.
  std::uint32_t blockEditCleanRevision = 0;
  /// \c undoStack size when the session was entered. Session edits push snapshots of the block's
  /// geometry (not the drawing's); those are dropped on close so a later Undo cannot restore block
  /// content into model space.
  std::size_t blockEditUndoMark = 0;
  bool blockEditCloseAsked = false;   ///< UI shows the close modal while true.
  /// Camera to restore when the session closes (the view BEDIT was invoked from).
  double blockEditCamPanX = 0.0, blockEditCamPanY = 0.0, blockEditCamPanZ = 0.0;
  float  blockEditCamZoom = 1.f, blockEditCamAz = 0.f, blockEditCamEl = 90.f, blockEditCamRoll = 0.f;
  /// Debug Developer Shell (REQ-161). Default off; status-bar DEV toggles it. Release ignores it.
  bool devShellVisible = false;
  std::vector<std::string> blockRecent;
  std::vector<std::string> blockFavorites;
  std::string blockLibraryFilter;
  /// ATTDEF records collected by the last DXF/DWG import. Transient — not saved, not undo.
  std::vector<CadBlockAttrDef> importedDxfAttrDefs;

  /// In-place TABLE cell editor (viewport overlay). Not a command; Enter commits, Esc cancels.
  bool tableCellEditorOpen = false;
  int tableCellEditorIndex = -1;
  int tableCellEditorCell = -1;
  std::string tableCellEditorBuf;
  bool tableCellEditorFocusRequest = false;

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

  /// One in-flight Volume Dashboard recompute (REQ-073's 2026-08-23 amendment, TASK-095 §6 step 3) —
  /// architecture §8's one-shot-worker contract again, in the same shape \ref SurfaceRebuildAsync
  /// already has, rather than a second staleness mechanism for the same problem (D-2026-08-23-k).
  /// At most one exists at a time: there is one dashboard, not one per surface.
  struct VolumeDashboardAsync {
    std::uint64_t baseSurfaceId = 0;
    std::uint64_t comparisonSurfaceId = 0;
    std::uint64_t clipEntityId = 0;
    /// Strong references to the exact triangulations this job computes against — captured on the UI
    /// thread at dispatch, read only on the worker thread. Safe with no locking because a `CadTin` is
    /// immutable once built and only ever REPLACED, never written through (architecture §11.5): the
    /// worker's copy of the pointer cannot be invalidated by whatever the UI thread does to the
    /// surface's OWN pointer while this runs.
    std::shared_ptr<const CadTin> tinBase;
    std::shared_ptr<const CadTin> tinComparison;
    /// REQ-131 clip ring in TIN-local XY, captured at dispatch. Empty = no clip.
    std::vector<std::pair<double, double>> clipRingXy;
    /// Whether the cut/fill map was wanted AT DISPATCH — captured so toggling the map on after a
    /// mapless result already landed is detected as staleness (the landed result has no map to show).
    bool wantMap = false;
    std::thread thread;
    std::atomic<bool> done{false};
    std::atomic<bool> cancel{false};
    std::uint32_t generation = 0;  ///< cadGpuRevision at dispatch — a mismatch on completion means discard
    SurfaceVolumeResult result;
    /// REQ-073's cut/fill map — `GL_TRIANGLES` layout, populated only when \c wantMap is true (empty,
    /// not "no map": REQ-073's "shows nothing outside the common area" already covers a genuinely
    /// empty cut or fill side of a self-comparison).
    std::vector<float> cutTrianglesXyz;
    std::vector<float> fillTrianglesXyz;

    ~VolumeDashboardAsync() {
      cancel.store(true, std::memory_order_release);
      if (thread.joinable())
        thread.join();
    }
  };

  /// The Volume Dashboard's own state (REQ-073 amendment). **UI/session-only** — never in
  /// \ref DrawingGeometrySnapshot, never in \ref DrawingDocument, never in `.gs` — the amendment's
  /// own rule, matching REQ-075's Surface Manager for the same reason: this is a VIEW onto document
  /// state (which two surfaces, and their last computed comparison), not document state itself.
  struct VolumeDashboardState {
    bool open = false;
    bool showMap = false;

    /// 0 = none picked. By stable entity id (REQ-076), never by index or name — the same reason
    /// \ref SurfaceRebuildAsync keys on id rather than a name that a rename could orphan.
    std::uint64_t baseSurfaceId = 0;
    std::uint64_t comparisonSurfaceId = 0;
    /// 0 = no clip (full overlap). Otherwise a closed polyline's entity id (REQ-131).
    std::uint64_t clipEntityId = 0;

    /// The last landed result, and what it was computed FOR — a revision plus the two ids, so a
    /// change to EITHER a surface's triangulation OR the panel's own pick is detected as staleness by
    /// the same comparison (`workflow.md`/REQ-073: "the panel's surface pick changed... the in-flight
    /// result is discarded").
    bool hasResult = false;
    SurfaceVolumeResult lastResult;
    std::uint32_t resultForRevision = 0xFFFFFFFFu;
    std::uint64_t resultForBaseSurfaceId = 0;
    std::uint64_t resultForComparisonSurfaceId = 0;
    std::uint64_t resultForClipEntityId = 0;
    bool resultHasMap = false;  ///< was `showMap` on when `lastResult` was computed?
    /// REQ-073's cut/fill map for `lastResult`, `GL_TRIANGLES` layout — empty unless `resultHasMap`.
    std::vector<float> mapCutTrianglesXyz;
    std::vector<float> mapFillTrianglesXyz;

    struct AnalysisRow {
      std::string label;
      std::uint64_t baseSurfaceId = 0;
      std::uint64_t comparisonSurfaceId = 0;
      std::uint64_t clipEntityId = 0;
      SurfaceVolumeResult result;
      bool hasResult = false;
    };
    std::vector<AnalysisRow> rows;

    std::unique_ptr<VolumeDashboardAsync> job;  ///< null when nothing is in flight
  };
  VolumeDashboardState volumeDashboard;
  std::string lastVolumeReportText;  ///< Last successful VOLUMES / dashboard numbers for VOLREPORT.
  bool hasLastVolumeResult = false;
  SurfaceVolumeResult lastVolumeResult;
  std::string lastVolumeBaseName;
  std::string lastVolumeComparisonName;

  /// REQ-145 Quick Profile — session graph only, never `.gs` (same rule as Volume Dashboard).
  struct QuickProfileState {
    bool open = false;
    bool hasResult = false;
    std::string surfaceName;
    double length = 0.0;
    int onSurfaceCount = 0;
    double minZ = 0.0;
    double maxZ = 0.0;
    std::vector<SurfaceProfileSample> samples;
  };
  QuickProfileState quickProfile;

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
    struct ContourLabel {
      float x = 0.f, y = 0.f, z = 0.f;
      double level = 0.0;
    };
    std::vector<ContourLabel> contourLabels;

    /// The style asked for more contour levels than the display path will generate, so the contour
    /// buffers above are empty for a reason that has nothing to do with the style's toggles.
    ///
    /// Carried rather than left implicit because REQ-201 forbids absorbing a refusal: an interval of
    /// 0.0001 ft over a 1,000 ft surface is a million contours, and a surface that simply stopped
    /// showing any would look like a defect. The Surface Manager reads this and says so.
    bool contoursSuppressed = false;
    /// How many levels were asked for, for that message. 0 when nothing was suppressed.
    int suppressedLevelCount = 0;

    /// REQ-072 band fills, `GL_TRIANGLES` layout, one buffer per `style.bands` entry **plus one
    /// extra buffer at index `bands.size()`** for a triangle `AssignBand` placed in no band (its
    /// value fell above the table's top). That extra bucket is drawn in the surface's plain
    /// `triangles` colour rather than dropped, so a range table that does not span the surface's
    /// full elevation/slope range does not silently blank the part above it (REQ-201: reported by
    /// being visibly still there, not absorbed into nothing). Empty when `analysisMode` is `None`.
    std::vector<std::vector<float>> bandTriangleBuffers;

    /// REQ-072 slope arrows, `GL_LINES` layout (shaft + two head barbs per arrow), bucketed by
    /// `style.arrowBands` the same way \ref bandTriangleBuffers is bucketed by `style.bands` — index
    /// `arrowBands.size()` holds arrows with no matching band (the table is empty, or the arrow's
    /// grade fell above its top), drawn in the surface's own colour. Empty when `slopeArrowsOn` is
    /// false, or every triangle is flat.
    std::vector<std::vector<float>> arrowLineBuffers;
  };

  /// The display-geometry cache. Live-only: never in \ref DrawingGeometrySnapshot, never in
  /// \ref DrawingDocument, never in `.gs` — which is what makes REQ-070's "never stored in `.gs`"
  /// and "adds no entity to the drawing" structurally true rather than a rule someone must remember.
  std::vector<SurfaceDisplayCacheEntry> surfaceDisplayCache;

  /// REQ-126 / ADR-039 (c): per-surface spatial index for elevation queries. Live-only, same
  /// staleness key as the display cache (id + TIN pointer). `mutable` because SURFELEV/hover take a
  /// const state and the index is derived, not document content.
  struct SurfaceQueryCacheEntry {
    std::uint64_t surfaceId = 0;
    std::weak_ptr<const CadTin> builtFrom;
    TinSpatialIndex index;
  };
  mutable std::vector<SurfaceQueryCacheEntry> surfaceQueryCache;

  /// REQ-132…134 / ADR-039 (j): live-only drain graph and preview lines. Never in `.gs`.
  struct SurfaceWatershedCacheEntry {
    std::uint64_t surfaceId = 0;
    std::weak_ptr<const CadTin> builtFrom;
    WatershedResult analysis;
    std::vector<float> basinOutlines;
    std::vector<float> waterDropLines;
    std::vector<float> catchmentLines;
  };
  std::vector<SurfaceWatershedCacheEntry> surfaceWatershedCache;
  std::string waterDropSurfaceName;
  std::string catchmentSurfaceName;
  std::string swapEdgeSurfaceName;
  std::string addPointSurfaceName;
  std::string delPointSurfaceName;
  std::string movePointSurfaceName;
  std::string delLineSurfaceName;
  enum class MoveTinPointPhase { WaitFrom, WaitTo } moveTinPointPhase = MoveTinPointPhase::WaitFrom;
  double moveTinFromX = 0.0;
  double moveTinFromY = 0.0;
  std::vector<float> lastWaterDropPathXyz;
  std::vector<float> lastCatchmentPathXyz;
  /// GL_LINES preview rebuilt each display pass from the last* paths. Not tied to TIN identity, so
  /// a REQ-069 rebuild cannot erase a water-drop the user just computed (REQ-133).
  std::vector<float> waterDropPreviewLines;
  std::vector<float> catchmentPreviewLines;

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

  /// Selected faces / edges / vertices of solids (REQ-318 increment 2, issue #148).
  ///
  /// **Mutually exclusive with \ref selection**, by decision D-2026-09-04-a: a plain click clears
  /// this, a `Ctrl` click clears that. Never both at once, which is what makes #148's "sub-object
  /// selection does not interfere with whole-entity selection" hold without every consumer of
  /// \ref selection having to know this exists.
  ///
  /// **Session state, like \ref hiddenEntityIds** — not written to `.gs` and not carried in an undo
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

  // --- The translate gizmo (REQ-060 + GitHub issue #148 acceptance 4, Phase 5 slices 4b and 4c) ---
  //
  // ONE widget, two subjects. With entities selected it shows three handles along the active UCS
  // and commits through `ApplyTranslationToSelection`; with exactly one solid FACE selected it
  // shows one handle along that face's own normal and commits through `CadApplyPushPull`. Which of
  // the two is derived from the selection (\ref CadGizmoModeFor), never stored, because the two
  // selections are already mutually exclusive (D-2026-09-04-a) — a stored mode would be a third
  // thing that can disagree with them.
  //
  // The face mode REPLACED the separate face grip REQ-319 increment 2 first shipped: two handles
  // for one operation is worse than either, and the gizmo brings the arrow, the pre-highlight, the
  // live ghost and the shared skew-line solve the grip's screen-space dot did not have.

  /// Which axis handle the cursor is over right now, or -1. In entity mode 0/1/2 are the active
  /// UCS X, Y and Z; in face mode 0 is the face normal and there is no other.
  ///
  /// Pre-highlight only. It is refreshed by the same hit test the click uses, so the handle that
  /// lights up is the handle that grabs - the rule REQ-318's sub-object hover already follows, and
  /// for the same reason: two code paths that agree by inspection stop agreeing.
  int gizmoHoverAxis = -1;

  /// True between the click that grabs an axis handle and the click that commits or cancels it.
  ///
  /// Click-arm / click-commit rather than press-drag-release, because that is what every other grip
  /// in this viewport already does (\ref mtextGripMoveActive, \ref dimGripMoveActive) - and it is
  /// the only shape a transcript can drive, a headless run having no mouse to hold down.
  bool gizmoDragActive = false;
  int gizmoDragAxis = -1;

  /// Where the gizmo sat when the handle was grabbed, in WCS.
  ///
  /// Captured rather than recomputed per frame, and in face mode that is load-bearing: the anchor
  /// is the face's centroid, and re-deriving it mid-drag would derive it from geometry the drag is
  /// about to change.
  ray3d::Vec3 gizmoAnchor{0.0, 0.0, 0.0};
  /// The axis direction grabbed, in WCS - captured with the anchor, and for the same reason.
  ray3d::Vec3 gizmoAxisDir{1.0, 0.0, 0.0};
  /// Signed position along that axis, measured at the moment of the grab.
  ///
  /// The drag distance is the CHANGE in this, never its absolute value, so the handle does not jump
  /// to the cursor on the first mouse move - and so the anchor's own precision cannot affect the
  /// result: it appears in both terms and cancels.
  double gizmoGrabParam = 0.0;
  /// The live drag distance along \ref gizmoAxisDir, in drawing units. Zero when nothing is dragged.
  double gizmoDragDistance = 0.0;

  /// In face mode, WHICH face the armed drag is moving — captured at the grab like the anchor.
  ///
  /// The selection could be cleared or re-picked between arming and committing; the drag applies to
  /// the face the user actually grabbed, not to whatever is selected when they let go.
  bool gizmoDragIsSubObject = false;
  SelectedSubObject gizmoDragSubObject;

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
  /// Elevation of the fence's first corner — the WORK PLANE's Z there, not the datum.
  ///
  /// Carried because a fence is projected to screen under an orbited camera, and a projection needs
  /// all three coordinates. Both the drawn rectangle and the hit test used to project the two drag
  /// corners at Z = 0, which is invisible in plan view (Z does not move a plan projection) and
  /// invisible on the world XY plane at elevation zero — but as soon as the work plane is TILTED by
  /// a UCS, or merely raised by `ELEV`, the corners project to pixels the mouse is nowhere near, and
  /// the box both draws and selects in the wrong place (user report, 2026-09-01).
  float selBoxAnchorZ = 0.f;
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
  /// REQ-316 / ADR-047: for an arc-segment (bulge) grip drag — the global vertex index whose bulge
  /// is being dragged, and its value before the drag, so RMB / Esc restores it.
  int entityGripOrigPolyBulgeVi = -1;
  float entityGripOrigPolyBulge = 0.f;

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
  /// REQ-322: the base point's elevation, so a typed MOVE can carry a Z. Zero when the base was
  /// typed without one, which is every drawing that predates 3D MOVE.
  float modifyBaseZ = 0.f;
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

  // --- MIRROR (REQ-103 step 1) ---
  enum class MirrorPhase {
    PickSelection,
    NeedP1,          ///< first point of the mirror line
    NeedP2,          ///< second point of the mirror line
    NeedEraseAnswer, ///< "Erase source objects? [Yes/No] <N>", default No
  } mirrorPhase = MirrorPhase::PickSelection;

  float mirrorP1X = 0.f, mirrorP1Y = 0.f;
  float mirrorP2X = 0.f, mirrorP2Y = 0.f;
  /// COPY modal: when true, duplicate survey selection by pending reflection instead of
  /// translation/rotation. Checked ahead of \ref pendingSurveyDupIsRotate.
  bool pendingSurveyDupIsMirror = false;
  float pendingMirrorX0 = 0.f, pendingMirrorY0 = 0.f, pendingMirrorX1 = 0.f, pendingMirrorY1 = 0.f;
  /// MIRROR erase-source, survey half: the CAD-side erase already ran (or didn't) synchronously in
  /// \c FinishMirrorCommand; this says whether the ORIGINAL survey points selected must also be
  /// removed once the duplicate-policy modal resolves (the duplicate itself is necessarily deferred
  /// — the modal decides its id policy — but "erase the source" is not, so it is applied here rather
  /// than left racing the modal).
  bool mirrorEraseSourcePending = false;

  // --- ARRAY (REQ-305) ---
  enum class ArrayType { Rectangular, Polar } arrayType = ArrayType::Rectangular;
  enum class ArrayPhase {
    PickSelection,
    WaitType,               ///< "[R]ectangular / [P]olar"
    Rect_WaitColumns,       ///< typed integer only — not spatial
    Rect_WaitColumnSpacing, ///< typed number, or click (horizontal distance from the anchor)
    Rect_WaitRows,          ///< typed integer only
    Rect_WaitRowSpacing,    ///< typed number, or click (vertical distance from the anchor); commits on completion
    Polar_WaitCenter,       ///< click / typed X,Y / object snap — the normal point-input path
    Polar_WaitItemCount,    ///< typed integer only — TOTAL instances including the original
    Polar_WaitAngle,        ///< typed degrees, or click (angle from center to cursor)
    Polar_WaitRotateAnswer, ///< "[Y]es / [N]o"; commits on completion
  } arrayPhase = ArrayPhase::PickSelection;

  int arrayCols = 0;
  int arrayRows = 0;
  float arrayColSpacing = 0.f;
  float arrayRowSpacing = 0.f;
  /// Anchor for interactive column/row-spacing entry (Rect_WaitColumnSpacing/RowSpacing read the
  /// cursor's distance from this point) — the same point \ref FirstSelectionAnchorPoint computes,
  /// cached once at WaitType so it does not shift while spacing is being dragged.
  float arrayAnchorX = 0.f, arrayAnchorY = 0.f;

  float arrayCenterX = 0.f, arrayCenterY = 0.f;
  int arrayItemCount = 0;
  float arrayFillAngleDeg = 360.f;
  bool arrayRotateItems = true;

  // --- LENGTHEN (REQ-103 step 2) ---
  /// Default sub-mode is **Total**, not AutoCAD's DElta (D-2026-08-24-f). Paired with the
  /// pick-first entry, that makes the out-of-the-box interaction the one people actually reach
  /// for: pick a line, read what it measures, type what you want it to measure. DElta's "add 50
  /// to whatever this is" needs the current length as context anyway, which a bare prompt before
  /// the pick cannot give.
  enum class LengthenMode { Delta, Percent, Total, Dynamic } lengthenMode = LengthenMode::Total;
  enum class LengthenPhase {
    WaitSelectOrMode,  ///< pick an object (applies current mode+value), or type DE/P/T/DY to (re)set the mode
    WaitDeltaValue,    ///< typed signed length to add/subtract
    WaitPercentValue,  ///< typed percentage of current length (100 = unchanged)
    WaitTotalValue,    ///< typed total length
    WaitDynamicTarget, ///< object picked in Dynamic mode; waiting for the new-length pick/type
  } lengthenPhase = LengthenPhase::WaitSelectOrMode;
  float lengthenDeltaValue = 0.f;
  float lengthenPercentValue = 100.f;
  float lengthenTotalValue = 0.f;
  /// Whether a value has ever been set for the CURRENT \ref lengthenMode this session — a bare pick
  /// at \c WaitSelectOrMode before any mode value exists is refused with a prompt to type one first,
  /// rather than silently applying a stray default (REQ-201).
  bool lengthenModeValueSet = false;
  /// Dynamic mode: the object picked at \ref LengthenPhase::WaitDynamicTarget, and which of its two
  /// stored endpoints is nearest that pick — true selects the FIRST endpoint in storage order
  /// (a Line's (x0,y0); an Arc's start angle; a Polyline's first vertex in its range), false the
  /// second. One bool covers all three entity kinds because each already orders its two ends the
  /// same "first/second in storage" way.
  SelectedEntity lengthenPendingEntity{};
  bool lengthenPendingNearFirst = false;
  float lengthenPendingCurrentLength = 0.f;
  /// REQ-103 LENGTHEN, pick-first entry (TASK-100). Set when a pick arrived before the active
  /// sub-mode had a value: the entity is latched here and the mode's value prompt is opened, so
  /// the value that follows applies to THAT object immediately rather than only arming the mode.
  /// Without it, a pick with no value set was refused outright and the ribbon button was a dead
  /// end — the command could only be reached by typing DE/P/T *and* a number first.
  bool lengthenPendingApplyOnValue = false;

  // --- EXTEND (REQ-103 step 3) ---
  enum class ExtendPhase {
    SelectBoundaries, ///< picking boundary edges; Enter (needs >=1) advances to SelectTargets
    SelectTargets,    ///< picking objects to extend, one per click, loops until Enter/Esc
  } extendPhase = ExtendPhase::SelectBoundaries;
  /// Boundary edges picked so far — read as a selection while being picked (TRIM's own precedent
  /// for `trimCutters`, copy-adapted rather than shared).
  std::vector<SelectedEntity> extendBoundaries;

  // --- BREAK (REQ-103 step 4) --- (BreakPoint is file-scope; see its definition above)
  enum class BreakPhase {
    SelectFirstPoint,  ///< pick selects the entity AND supplies break point 1
    SelectSecondPoint, ///< pick supplies break point 2, projected onto the same entity; applies, loops
  } breakPhase = BreakPhase::SelectFirstPoint;
  SelectedEntity breakEntity{};
  BreakPoint breakP1{};

  // --- STRETCH (REQ-103 step 5) ---
  /// Crossing/window box captured when the box-select that started this STRETCH invocation closed
  /// (world XY, plain — not camera-projected; see REQ-103's STRETCH acceptance simplification
  /// note). Read at apply time to decide which of each selected entity's definition points move.
  /// STRETCH always runs its own fresh box-select (never consumes a pre-existing \ref selection),
  /// so this is always populated together with the selection it describes.
  float stretchRectMnX = 0.f, stretchRectMxX = 0.f, stretchRectMnY = 0.f, stretchRectMxY = 0.f;

  // --- FILLET (REQ-103 step 6a) ---
  enum class FilletPhase {
    WaitFirstEntity,  ///< pick the first curve (or type R/T to set radius/trim mode)
    WaitSecondEntity, ///< pick the second curve; applies immediately, loops back to WaitFirstEntity
  } filletPhase = FilletPhase::WaitFirstEntity;
  /// The first curve picked, latched until the second pick completes the fillet. `polySeg` is the
  /// 0-based Polyline EDGE the pick landed on (-1 for Line/Arc, where the whole entity already IS
  /// the curve) — SelectedEntity itself carries no such sub-index (confirmed by research: nothing
  /// in this codebase addresses one segment of a multi-vertex polyline today; the closest analogues
  /// are BREAK's own BreakPoint::segIndex and TRIM's TrimTargetEdge, neither shared).
  SelectedEntity filletFirstEntity{};
  int filletFirstPolySeg = -1;
  float filletFirstPickX = 0.f, filletFirstPickY = 0.f;
  /// Persisted app-level (gosurvey-user.json), like `trimState` — NOT per-drawing (D-2026-08-24-g:
  /// a generalized "system variable registry" was considered and explicitly declined here; see that
  /// decision's rationale for why). Default 0.5, matching AutoCAD's own FILLETRAD default.
  float filletRadius = 0.5f;
  /// Shared with CHAMFER (AutoCAD's own shared TRIMMODE variable — one toggle governs both
  /// commands, not two). 1 = Trim (default), 0 = No trim. Also persisted app-level.
  int cornerTrimMode = 1;
  /// Transient (not persisted): true while `HandleFilletText`/`HandleChamferText` is waiting for
  /// the NUMBER that follows a typed `R`/`T` sub-command, at `WaitFirstEntity` only.
  bool filletTextAwaitingRadius = false;
  bool filletTextAwaitingTrim = false;

  // --- CHAMFER (REQ-103 step 6b) ---
  enum class ChamferPhase { WaitFirstEntity, WaitSecondEntity } chamferPhase = ChamferPhase::WaitFirstEntity;
  /// Same shape as `filletFirstEntity`/`filletFirstPolySeg` — see that field's comment.
  SelectedEntity chamferFirstEntity{};
  int chamferFirstPolySeg = -1;
  float chamferFirstPickX = 0.f, chamferFirstPickY = 0.f;
  /// Persisted app-level (gosurvey-user.json), like `filletRadius`. Default 0.5 each, matching
  /// AutoCAD's own CHAMFERA/CHAMFERB defaults. `chamferDist1` doubles as the single Distance/Angle-
  /// mode distance (AutoCAD's own CHAMFERC).
  float chamferDist1 = 0.5f;
  float chamferDist2 = 0.5f;
  /// Persisted, degrees — AutoCAD's own CHAMFERD, stored here in degrees (not radians) since it is
  /// only ever read/written as a typed value, matching every other user-facing angle in this
  /// codebase's command layer.
  float chamferAngle = 45.f;
  /// 0 = Distance/Distance (default), 1 = Distance/Angle. Persisted.
  int chamferMode = 0;
  /// Transient (not persisted): true while `HandleChamferText` is waiting for the NUMBER that
  /// follows a typed `D`/`A` sub-command, at `WaitFirstEntity` only. `chamferTextAwaitingSecondDist`
  /// distinguishes Distance/Distance's two prompts (first distance, then second).
  bool chamferTextAwaitingFirstValue = false;
  bool chamferTextAwaitingSecondDist = false;
  bool chamferTextAwaitingAngle = false;
  bool chamferTextAwaitingTrim = false;

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
  enum class QuickProfilePhase { WaitFirst, WaitSecond } quickProfilePhase = QuickProfilePhase::WaitFirst;
  std::string quickProfileSurfaceName;
  double quickProfileFromX = 0.0;
  double quickProfileFromY = 0.0;
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
  bool designateContourSource = false;

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
  /// When set, the next Surface Style window draw selects this named row (Surface Manager Edit...).
  std::string surfaceStyleEditorFocusName;
  /// When true, the style/analysis editor uses the title "Surfaces" (Toolspace / Survey ribbon).
  bool surfaceStyleUseSurfacesTitle = false;
  bool showPointGroupManagerWindow = false;  ///< Point Group manager (REQ-067).
  /// When set, the Point Group manager selects this named group on the next draw.
  std::string pointGroupManagerFocusName;
  bool showSurfaceManagerWindow = false;     ///< Surfaces panel (REQ-068). definition edits live in Toolspace.
  /// Create Surface dialog (Toolspace Surfaces ▸ Create Surface...). Session-only.
  bool showCreateSurfaceWindow = false;
  /// Surface Properties (Toolspace named surface ▸ Surface Properties...). Session-only.
  bool showSurfacePropertiesWindow = false;
  int surfacePropertiesIndex = -1;
  bool showFeatureLineElevWindow = false;    ///< Feature line elevation editor (REQ-088).
  /// REQ-142 Toolspace (Prospector / Settings). Session-only; not written to `.gs`.
  enum class ToolspaceTab : int { Prospector = 0, Settings = 1 };
  bool showToolspaceWindow = true;
  /// View Manager (REQ-106) — the dialog half of "a VIEW command/dialog". Session-only, like the
  /// other manager windows: which panels are open is not a property of the drawing.
  bool showViewManagerWindow = false;
  /// The New View name prompt, raised from the ribbon button. Separate from the manager window so
  /// "save what I am looking at" stays one click rather than opening a dialog to find a button.
  bool showViewManagerNewPrompt = false;
  char newViewNameBuf[96] = {};
  ToolspaceTab toolspaceTab = ToolspaceTab::Prospector;
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
  /// Centralized dimension style (issue #99) — single active style used by DIMALIGNED/DIMLINEAR/DIMANGULAR.
  /// Pure value type, undoable via snapshot (like textStyles/surfaceStyles).  New dimensions bake this
  /// style's effective values; existing dimensions read the active style at draw/measure time (phase 1).
  DimensionStyle activeDimensionStyle = DimensionStyles::Default();
  DimensionStyle dimStyleDraft = DimensionStyles::Default();
  bool showDimStyleDialog = false;
  /// Viewport CAD crosshair (Drawing1): RGB 0–1, arm length as fraction of viewport width/height, pickbox half-size in px.
  float viewportCrosshairR = 1.f;
  float viewportCrosshairG = 0.8392157f;
  float viewportCrosshairB = 0.f;
  float viewportCrosshairArmFracX = 0.03f;
  float viewportCrosshairArmFracY = 0.05f;
  float viewportCrosshairPickHalfPxX = 4.f;
  float viewportCrosshairPickHalfPxY = 4.f;
  float viewportCrosshairHairPx = 1.f;
  /// 3D crosshair cursor (REQ-310): draw the active UCS's X/Y/Z axes instead of two screen-aligned
  /// arms, so the cursor shows which way the drawing plane runs under an orbited view or a rotated
  /// UCS. **Off by default** — on, the cursor changes colour and orientation, and a display change
  /// no one asked for is the one thing REQ-064 was careful to avoid when it added visual styles.
  bool viewportCrosshair3d = false;
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

  /// Frame-time diagnostic HUD (issue #166 investigation). Toggled by the `PERFHUD` command. The
  /// millisecond fields are written each frame by whoever owns that section — `perfRenderMs` in the
  /// app frame loop, the rest in the viewport draw — and read only by the overlay.
  bool   perfHudVisible = false;
  double perfFrameMs = 0.0;        ///< whole frame, wall-clock frame-to-frame
  double perfRenderMs = 0.0;       ///< the GL RenderScene call
  double perfHoverPickMs = 0.0;    ///< the viewport entity hover-pick block (issue #166)
  bool   perfHoverPickRan = false; ///< did the hover pick actually run this frame, or reuse cache
  double perfSnapMs = 0.0;         ///< the object-snap FindBest block
  double perfViewportUiMs = 0.0;   ///< the whole DrawDrawingViewport call

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
  /// UTF-8 path to optional startup .gst template (Settings → Startup). Empty = use bundled resources/default-template.gst.
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
  /// REQ-308 / D-2026-08-30-a: drawingTabs[0] is the **Start screen** — a non-closable, pinned-first
  /// sentinel that backs no document. documents[0]/viewportRenderers[0] exist for index alignment
  /// but are never meaningful. Real drawings start at FirstDrawingTabIndex().
  std::vector<DrawingTab>     drawingTabs{{"Start", 0u}, {"Drawing 1", 1u}};
  int      activeDrawingIdx   = 0;   ///< 0 = Start screen on launch.
  int      nextDrawingNumber  = 2;    ///< Auto-incremented for "Drawing N" naming.
  uint32_t nextTabUid         = 2u;   ///< Monotonically increasing; each new tab gets a unique uid.
  bool pendingDrawingTabSwitch = false; ///< Set for one frame after a programmatic tab change.
  bool pendingViewportFocus   = false; ///< Request ImGui focus on the Drawing1 window next frame.
  bool pendingPropertiesFocus = false; ///< Request ImGui focus on the Properties tab on next Begin().
  bool propertiesPanelActive  = false; ///< True when Properties is the selected tab in its dock node.
  int  prevDrawingIdx         = 0;     ///< Authoritative "last active" idx; used by main.cpp for switch detection.
  int  pendingTabErase        = -1;    ///< If >= 0, main.cpp must shut down + erase viewportRenderers[this index].
  /// REQ-308: after a drawing is opened or saved, the main loop renders it once then captures a
  /// thumbnail for the Recent list. Set together; serviced and cleared after RenderScene when
  /// pendingThumbnailTabIdx == activeDrawingIdx.
  std::string pendingThumbnailPath;
  int         pendingThumbnailTabIdx = -1;
  /// Per-drawing snapshots — one entry per open tab.  Active tab's live data lives in the fields
  /// above; this vector is read/written by SaveDocumentToSnapshot / RestoreDocumentFromSnapshot.
  std::vector<DrawingDocument> documents{2};  ///< [0] pairs with the Start sentinel tab (unused); see drawingTabs.

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

  // -------------------------------------------------------------------------
  // INSERT dialog (issue #124)
  // -------------------------------------------------------------------------
  enum class InsertBlockPhase {
    WaitDialog,
    WaitInsertPoint,
    WaitScale,
    WaitRotation,
    WaitAttributes,
  } insertBlockPhase = InsertBlockPhase::WaitDialog;

  bool insertBlockDialogOpen = false;
  char insertBlockName[256]{};
  char insertBlockPath[4096]{};
  char insertBlockAngleBuf[64]{};
  float insertBlockX = 0.f;
  float insertBlockY = 0.f;
  float insertBlockZ = 0.f;
  float insertBlockSx = 1.f;
  float insertBlockSy = 1.f;
  float insertBlockSz = 1.f;
  float insertBlockRotDeg = 0.f;
  bool insertBlockSpecifyPoint = true;
  bool insertBlockSpecifyScale = false;
  bool insertBlockSpecifyRot = true;
  bool insertBlockUniformScale = true;
  bool insertBlockExplode = false;
  bool insertBlockAttrDialogOpen = false;
  int insertBlockAttrRefIndex = -1;
  bool insertBlockAttrPaper = false;
  char insertBlockAttrBuf[8][128]{};

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
  // REQ-307 (GitHub #106): paper-space MOVE/COPY/DELETE are pick-first by default (act on whatever
  // is already selected), but starting one with NOTHING selected now opens a real selection step
  // instead of refusing — the paper-space counterpart of REQ-121's model-space treatment. Separate
  // bools rather than folding into \c paperMovePhase: that field's 1/2 values (base/destination) are
  // consulted by several `!= 0` checks meaning "a MOVE/COPY gesture is in progress", and a selecting
  // step is not that gesture yet (raw cursor, no snapped base point).
  bool  paperMoveWaitingSelection = false;
  bool  paperDeleteWaitingSelection = false;
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
  // Paper-space MIRROR of selected paper entities (REQ-103): 0 idle, 1 need first mirror-line point,
  // 2 need second point (commits immediately — no erase-source prompt in paper space; see
  // MirrorSelectedPaperEntities's comment).
  int   paperMirrorPhase = 0;
  float paperMirrorP1XIn = 0.f;
  float paperMirrorP1YIn = 0.f;
  // Paper-space LENGTHEN of native paper entities (REQ-103): 0 idle, 1 waiting to pick the next
  // object. No mode/value prompt here — the pure-paper click flow has no text-entry surface (same
  // limitation MIRROR's paper path documents), so paper-space LENGTHEN reuses whatever
  // lengthenMode/lengthenDeltaValue/etc. was last configured through the model-space command; a
  // pick before any value has ever been set is refused with a message rather than applying 0.
  int   paperLengthenPhase = 0;
  // Paper-space EXTEND of native paper entities (REQ-103 step 3, TASK-096): 0 idle, 1 collecting
  // boundary edges, 2 picking targets. Unlike MIRROR/LENGTHEN's paper paths, EXTEND needs no typed
  // value at all — only two rounds of clicking — so it is NOT simplified away; the Enter key
  // (checked alongside the existing Escape handling in the same paper-click block) advances phase
  // 1->2, the same "done picking edges" signal model-space TRIM/EXTEND get from a real Enter.
  int   paperExtendPhase = 0;
  std::vector<PaperEntityRef> paperExtendBoundaries;
  // Paper-space BREAK of native paper entities (REQ-103 step 4): 0 idle, 1 waiting for the
  // entity+first-point pick, 2 waiting for the second point (applies, loops back to 1). Pure click
  // flow like paper EXTEND — no typed value, so not simplified away.
  int   paperBreakPhase = 0;
  PaperEntityRef paperBreakEntity{};
  BreakPoint paperBreakP1{};
  // Paper-space STRETCH of native paper entities (REQ-103 step 5, TASK-098): 0 idle, 1 need base
  // point, 2 need destination. Unlike MIRROR/LENGTHEN/EXTEND/BREAK, STRETCH does NOT drive its own
  // box-select — paper box-select is ambient (any plain click starts one, `CadUi.cpp`'s
  // `closePaperSelBox`, regardless of active command) and ROTATE/SCALE already consume whatever is
  // pre-selected, so STRETCH follows that same "pre-select, then invoke" convention: it requires
  // \ref selectedPaperEntities non-empty at start, same as paper ROTATE/SCALE.
  int   paperStretchPhase = 0;
  float paperStretchBaseXIn = 0.f;
  float paperStretchBaseYIn = 0.f;
  /// True iff \ref selectedPaperEntities was populated by a box-select (not a plain click) and the
  /// rect below is that box, in paper inches. Set at the end of `closePaperSelBox` regardless of
  /// which command (if any) is active; cleared by `ClearPaperEntitySelection`/
  /// `TogglePaperEntitySelection` (the shared funnel every non-box selection change goes through),
  /// so a plain click-select never leaves a stale rect behind. When false, paper STRETCH degrades to
  /// a whole-entity translate of the current selection — matching AutoCAD's own degradation for a
  /// non-crossing pickfirst set.
  bool  paperSelBoxLastValid = false;
  // Paper-space FILLET of native paper entities (REQ-103 step 6a): 0 idle, 1 waiting for the first
  // curve pick, 2 waiting for the second (applies, loops back to 1). Pure click flow like paper
  // EXTEND/BREAK — no typed value at the pick itself; radius/trim mode are set through the
  // model-space command line, the same way paper LENGTHEN reuses model-set values.
  int   paperFilletPhase = 0;
  PaperEntityRef paperFilletFirstEntity{};
  int   paperFilletFirstPolySeg = -1;
  float paperFilletFirstPickX = 0.f;
  float paperFilletFirstPickY = 0.f;
  // Paper-space CHAMFER of native paper entities (REQ-103 step 6b): same shape as paper FILLET
  // above.
  int   paperChamferPhase = 0;
  PaperEntityRef paperChamferFirstEntity{};
  int   paperChamferFirstPolySeg = -1;
  float paperChamferFirstPickX = 0.f;
  float paperChamferFirstPickY = 0.f;
  float paperSelBoxLastMnXIn = 0.f, paperSelBoxLastMxXIn = 0.f;
  float paperSelBoxLastMnYIn = 0.f, paperSelBoxLastMxYIn = 0.f;
  // Floating model space (REQ-036): edit the model IN PLACE through a viewport. The active space stays
  // the paper layout (sheet + viewports stay visible); model edit/snap/draw is routed through the viewport.
  int    floatingViewportLayout = -1;   ///< paper layout of the floating viewport, or -1 if not floating.
  int    floatingViewportIndex = -1;    ///< viewport being edited in place, or -1.
  /// REQ-155 (issue #155): while floating model space is entered, \ref activeUcs holds that
  /// VIEWPORT's active UCS (so coordinate entry / grid / ORTHO / readout / UCSFOLLOW resolve
  /// against the viewport's frame). The drawing-scoped UCS is parked here for the duration and
  /// restored on exit. \ref CadDrawingScopedUcs reads the right one; persistence (`.gs` save,
  /// per-tab snapshot) must go through it so a save/tab-switch WHILE floating records the
  /// drawing's frame, not the viewport's. Session-only — never written to `.gs`.
  ucs::Ucs drawingActiveUcsStash;
  bool     floatingUcsSwapActive = false;
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

/// REQ-316 / ADR-047: call `fn(seg, midX, midY, midZ)` for every ARC segment of polyline `pi`, with
/// the midpoint at the arc's apex (the point at half sweep). `seg` is the 0-based segment index —
/// the same one `kPolyBulgeGripBase + seg` encodes. Straight segments are skipped. Shared by the
/// four grip sites (model + floating-viewport, draw + grab) so they cannot disagree.
template <class F>
inline void CadForEachPolylineArcMidGrip(const AppCommandState& st, int pi, F&& fn) {
  const int np = st.userPolylineOffsets.size() > 0 ? static_cast<int>(st.userPolylineOffsets.size() - 1) : 0;
  if (pi < 0 || pi >= np)
    return;
  const int v0 = st.userPolylineOffsets[static_cast<std::size_t>(pi)];
  const int v1 = st.userPolylineOffsets[static_cast<std::size_t>(pi + 1)];
  const bool closed = static_cast<std::size_t>(pi) < st.userPolylineClosed.size() &&
                      st.userPolylineClosed[static_cast<std::size_t>(pi)];
  const int nseg = (v1 - v0) - 1 + (closed && (v1 - v0) >= 2 ? 1 : 0);
  for (int s = 0; s < nseg; ++s) {
    const int va = v0 + s;
    const int vb = (s == (v1 - v0) - 1) ? v0 : v0 + s + 1;  // wrap on the closing segment
    if (static_cast<std::size_t>(vb) * 3 + 2 >= st.userPolylineVerts.size())
      break;
    const float bulge = static_cast<std::size_t>(va) < st.userPolylineVertsBulge.size()
                            ? st.userPolylineVertsBulge[static_cast<std::size_t>(va)]
                            : 0.f;
    if (bulge == 0.f)
      continue;
    const std::size_t A = static_cast<std::size_t>(va) * 3, B = static_cast<std::size_t>(vb) * 3;
    const BulgeArcSpan arc = BulgeArc(st.userPolylineVerts[A], st.userPolylineVerts[A + 1],
                                      st.userPolylineVerts[B], st.userPolylineVerts[B + 1],
                                      static_cast<double>(bulge));
    if (!arc.valid)
      continue;
    const double mid = arc.startAngle + arc.sweep * 0.5;
    fn(s, static_cast<float>(arc.cx + arc.radius * std::cos(mid)),
       static_cast<float>(arc.cy + arc.radius * std::sin(mid)), st.userPolylineVerts[A + 2]);
  }
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
  c.rollDeg = st.viewportRollDeg;  // #153: nonzero only under a tilted-UCS PLAN
  c.projection = st.viewportProjection;
  c.fovDeg = st.viewportFovDeg;
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
inline void CadStartViewAnimation(AppCommandState& st, float az, float el, float roll = 0.f) {
  st.viewAnimFromAz = st.viewportAzimuthDeg;
  st.viewAnimFromEl = st.viewportElevationDeg;
  st.viewAnimFromRoll = st.viewportRollDeg;
  // Unwrapped target, so the lerp below cannot take the long way round (Camera::ShortestAzimuthDelta).
  st.viewAnimToAz = st.viewportAzimuthDeg + Camera::ShortestAzimuthDelta(st.viewportAzimuthDeg, az);
  st.viewAnimToEl = el;
  // Roll wraps like azimuth, so ease it the short way too (#153). Callers that do not pass a roll
  // get 0 here, which returns a rolled view to upright — the correct move for every non-PLAN caller.
  st.viewAnimToRoll = st.viewportRollDeg + Camera::ShortestAzimuthDelta(st.viewportRollDeg, roll);
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
  float roll = st.viewAnimFromRoll + (st.viewAnimToRoll - st.viewAnimFromRoll) * e;
  while (roll < 0.f)
    roll += 360.f;
  while (roll >= 360.f)
    roll -= 360.f;
  st.viewportRollDeg = roll;
}

/// Elevation at which newly drawn geometry lands — the active work plane's Z (REQ-058 / REQ-154).
///
/// Two channels, in order:
///
///  1. **The resolved point's own Z**, when point entry has published one. On a tilted UCS the work
///     plane's Z *varies across the plane*, so a single constant cannot describe where a point
///     lands — the value has to come from the point that was actually resolved (the click's
///     ray x plane hit, or the typed UCS coordinate mapped through the frame).
///  2. Otherwise the UCS origin's Z, which is exactly the old behaviour.
///
/// Under any UCS parallel to world XY — every pre-UCS drawing, and every UCS that is a rotation
/// about Z — the two agree by construction, so this is a strict superset of what ELEV did rather
/// than a change to it.
inline float CadWorkPlaneElevation(const AppCommandState& st) {
  if (st.resolvedPointZValid)
    return st.resolvedPointZ;
  return static_cast<float>(st.activeUcs.origin.z);
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

/// True when the active UCS is the World Coordinate System — the default, and what the status bar
/// reports as "World".
inline bool CadUcsIsWorld(const AppCommandState& st) { return ucs::IsWorld(st.activeUcs); }

/// The DRAWING-scoped active UCS (REQ-154), correct even while a per-viewport frame is live in
/// \ref AppCommandState::activeUcs because floating model space is entered (REQ-155). Persistence —
/// the `.gs` save and the per-tab document snapshot — must read this, never `activeUcs` directly,
/// or a save/tab-switch performed while floating would record the viewport's frame as the drawing's.
inline const ucs::Ucs& CadDrawingScopedUcs(const AppCommandState& st) {
  return st.floatingUcsSwapActive ? st.drawingActiveUcsStash : st.activeUcs;
}

/// The active UCS expressed in **storage space** (local XY, absolute Z).
///
/// \ref AppCommandState::activeUcs is stored in TRUE WORLD coordinates, so that a document-origin
/// rebase — which shifts every stored coordinate to keep float precision (REQ-101) — cannot move
/// the user's coordinate frame out from under them. Nothing has to remember to shift it, because
/// there is nothing frame-relative to shift.
///
/// The camera, the picking rays and the geometry they hit all live in storage space, so anything
/// that meets a ray converts here first. Only the origin moves; a translation cannot rotate a
/// basis, so the axes pass through untouched.
inline ucs::Ucs CadActiveUcsStorage(const AppCommandState& st) {
  ucs::Ucs u = st.activeUcs;
  u.origin.x -= st.worldDocumentOriginX;
  u.origin.y -= st.worldDocumentOriginY;
  return u;
}

/// The active work plane (UCS XY) a viewport click resolves against (REQ-058 / ADR-025 (e)).
/// In storage space, because that is the space the ray is in.
inline ray3d::Plane CadActiveWorkPlane(const AppCommandState& st) { return ucs::WorkPlane(CadActiveUcsStorage(st)); }

/// True when the active work plane is parallel to world XY and faces up (REQ-312).
///
/// Every UCS that is a translation and/or a rotation about Z satisfies this - which is the whole
/// 2D survey case, and the default. It is the branch guard for the arbitrary-plane drawing paths,
/// and it is deliberately NOT `CadUcsIsWorld`: a UCS squared to a road centreline is still a
/// flat drawing, and it must keep the exact float path every existing drawing, transcript and test
/// already goes through. That is REQ-154's own reasoning for its WCS branch, applied one level out.
inline bool CadWorkPlaneIsWorldXy(const AppCommandState& st) {
  // States the condition directly rather than borrowing `ucs::PlanViewIsExact`, which this used to
  // call. That predicate answers the CAMERA's question - "can PLAN put UCS +Y up the screen
  // exactly?" - and issue #153 gave `Camera` a roll axis, after which the answer became yes for
  // EVERY valid frame. The name did not change and neither did the call site, so a tilted drawing
  // silently began taking the flat branch here: the guard inverted without a compiler error, and
  // the four REQ-312 transcripts are what caught it. A predicate named for another subsystem's
  // concern is not a safe way to ask whether a plane is parallel to world XY, so this asks.
  const ucs::Ucs& u = st.activeUcs;
  constexpr double kTol = 1e-6;
  return std::fabs(u.zAxis.x) <= kTol && std::fabs(u.zAxis.y) <= kTol && u.zAxis.z > 0.0;
}

/// The work plane, moved so its origin sits on \p ox,\p oy,\p oz (REQ-312).
///
/// Anchoring on the first pick rather than on the UCS origin keeps the 2D coordinates that come out
/// of it small. The planar maths the draw commands use (circumcircle, swept angle) runs in float,
/// and at state-plane magnitude a float has a quarter-foot of resolution - the same REQ-101
/// narrowing hazard the document origin exists to avoid, arriving through a different door.
inline ucs::Ucs CadWorkPlaneAnchoredAt(const AppCommandState& st, float ox, float oy, float oz) {
  return ucs::WithOrigin(CadActiveUcsStorage(st),
                         {static_cast<double>(ox), static_cast<double>(oy), static_cast<double>(oz)});
}

/// The normal of the plane a new curve commits into: the active UCS's Z axis (REQ-312).
inline void CadActiveDrawPlaneNormal(const AppCommandState& st, float* nx, float* ny, float* nz) {
  const ucs::Ucs u = st.activeUcs;  // a translation cannot rotate a basis, so storage vs world is moot
  if (nx)
    *nx = static_cast<float>(u.zAxis.x);
  if (ny)
    *ny = static_cast<float>(u.zAxis.y);
  if (nz)
    *nz = static_cast<float>(u.zAxis.z);
}

/// A circle solved from picks: where its centre is, how big it is, and which way its plane faces.
///
/// The return type of the CIRCLE solvers below. It exists so the geometry a set of picks defines can
/// be computed WITHOUT committing it -- the rubber-band preview needs exactly that, and computing it
/// a second way in the preview is how a preview comes to show a shape the commit does not produce.
struct CadCircleSolution {
  float cx = 0.f;
  float cy = 0.f;
  float cz = 0.f;
  float r = 0.f;
  float nx = kFlatNormalX;
  float ny = kFlatNormalY;
  float nz = kFlatNormalZ;
};

/// CIRCLE centre-and-radius: the circle a centre pick and a rim pick define on the work plane.
///
/// On a flat work plane this is the pre-REQ-312 arithmetic to the bit. On a tilted one the rim pick
/// is displaced in Z as well, so the radius is the 3D distance to it -- its XY projection is short
/// by cos(tilt), and on a vertical plane it collapses to nothing at all.
[[nodiscard]] CadCircleSolution CadSolveCircleFromRimPick(const AppCommandState& st, float cx, float cy, float cz,
                                                          float px, float py, float pz);

/// CIRCLE 3P: the circle through three picks on the active work plane.
///
/// False when the picks are collinear -- in the plane, which on a tilted plane is not the same
/// question as collinear in the XY projection.
[[nodiscard]] bool CadSolveCircleThreePoints(const AppCommandState& st, float ax, float ay, float az, float bx,
                                             float by, float bz, float cx, float cy, float cz,
                                             CadCircleSolution* out);

/// ARC 3P: the arc through three picks on the active work plane, angles measured in the arc's own
/// frame (`ucs::FromNormal`), which is where CadArc::startRad and CadArc::sweepRad live.
///
/// False when the picks are collinear. \p out is left untouched on failure. The caller decides what
/// a failure means -- the commit reports it and resets the draft, the preview just draws nothing.
[[nodiscard]] bool CadSolveArcThreePoints(const AppCommandState& st, float ax, float ay, float az, float bx,
                                          float by, float bz, float cx, float cy, float cz, CadArc* out);

/// The **camera-azimuth offset** that squares the view with the active UCS's north (REQ-059).
///
/// Negated relative to the UCS's own rotation, and that sign is not a detail to gloss: a positive
/// UCS rotation about Z turns the frame counter-clockwise, while a positive camera azimuth turns
/// screen-up clockwise (measured against `Camera`, see `ucs::PlanViewAngles`). Handing the ViewCube
/// the un-negated angle makes its compass square up the wrong way — visibly, but only under a
/// rotated UCS, which is exactly the case nobody exercises by accident.
inline float CadUcsViewAzimuthOffsetDeg(const AppCommandState& st) {
  return -ucs::AzimuthAboutWorldZDeg(st.activeUcs);
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
  Surface,
  /// REQ-148 / D-2026-08-28-i. Appended after Surface so the id sweep does not renumber legacy drawings.
  Table,
  /// GitHub issue #124. Appended after Table so the id sweep does not renumber legacy drawings.
  BlockRef,
  /// REQ-313 / ADR-045. Appended after BlockRef, for the reason Surface's note above spells out:
  /// the id sweep walks the attribute arrays in `kEntityKindsInSweepOrder`, so inserting anywhere
  /// but the end would renumber every entity in every existing drawing on its next load.
  Solid
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

// ---------------------------------------------------------------------------------------------
// B-rep solids (REQ-313 / ADR-045, GitHub issue #146).
// ---------------------------------------------------------------------------------------------

/// True when solid \p solidIndex is drawn AND clickable. One predicate for both, for the reason
/// \ref SurfaceVisible gives for itself: "invisible" and "unclickable" must not be able to disagree
/// (REQ-084 (d)).
[[nodiscard]] bool SolidVisible(const AppCommandState& st, size_t solidIndex);

/// Bring \ref AppCommandState::solidDisplayCache and \ref AppCommandState::solidDisplayGeometry up
/// to date. Called once a frame, beside \ref RefreshSurfaceDisplayGeometry.
///
/// Regenerates a solid's triangles and edges only when its staleness key — the solid pointer plus
/// the chord tolerance — has moved, which is #120's "do not regenerate a solid's render mesh every
/// frame" stated as a property of what the function does rather than as an intention. Entries whose
/// solid no longer exists are reaped in the same pass, so an erased solid's triangles do not outlive
/// it. The batch list at the end is rebuilt every call — it copies pointers and colours, never
/// vertices — because layer visibility and object isolation change with no solid changing at all.
void RefreshSolidDisplayGeometry(AppCommandState& st);

/// Create one of the seven primitives from a typed command line (REQ-313). \p verb is the
/// already-lowercased command token; \p rest is everything after it.
///
/// Every failure is reported by name and creates nothing (REQ-201): a bad dimension, a bad base
/// point, a wrong argument count, or a solid the kernel refuses to build.
void CadCreateSolidPrimitive(AppCommandState& st, const std::string& verb, const std::string& rest,
                             std::vector<std::string>& log);

/// True when \p verb names one of the seven primitive commands. Used by the command dispatch and by
/// the help registry, so the two cannot disagree about which commands exist.
[[nodiscard]] bool CadIsSolidPrimitiveVerb(const std::string& verb);

/// PRESSPULL (REQ-319) — move the one selected solid FACE along its own normal by p args feet.
///
/// Acts on the REQ-318 sub-object selection, which is what pairs the two: Ctrl+click names the face,
/// this moves it. Refuses — by name, with the document untouched — an empty or face-less selection,
/// more than one face, a distance that is not a number, and every geometric refusal `brep::PushPullFace`
/// raises. One undo step for the whole edit.
void CadPressPull(AppCommandState& st, const std::string& args, std::vector<std::string>& log);

/// Apply one push/pull and record it as a single undo step. The shared commit behind both the typed
/// `PRESSPULL` and the grip drag, so the two cannot diverge about what a push does — the same
/// single-implementation rule REQ-318 item 1 states for the pick.
///
/// Replaces the solid, re-points every sub-object reference that named it (the reference is keyed on
/// identity, and the solid has just been replaced), and logs the kernel's own sentence on a refusal
/// with the document untouched.
bool CadApplyPushPull(AppCommandState& st, const SelectedSubObject& ref, double distance,
                      std::vector<std::string>& log);

/// The face grip's anchor and slide axis: the face's centroid, and its outward normal (REQ-319
/// increment 2). False when \p ref does not resolve to a planar face of a live solid.
///
/// The centroid rather than a corner, because a grip is a handle on the *face* — a corner handle
/// would read as a vertex grip, which is a different edit (#148 criterion 3's other half, not built).
[[nodiscard]] bool CadSubObjectFaceGrip(const AppCommandState& st, const SelectedSubObject& ref,
                                        ray3d::Vec3* outAnchor, ray3d::Vec3* outAxis);

/// One named dimension of a primitive: the letter that sets it, and what to call it in a prompt.
///
/// `optional` marks a parameter the primitive can be built without — only a cone's and a pyramid's
/// top radius, which default to zero and give an apex. Everything else must be set before Enter will
/// create anything, so a half-specified solid is refused with the missing names rather than built at
/// some assumed size (REQ-201).
/// How the cursor supplies a dimension, which is what the live preview and the click both read.
///
/// `Typed` is a real answer, not a gap: a pyramid's side count and a cone's top radius have no
/// natural mouse gesture, so they stay keyword-and-default and the pick sequence skips them.
enum class SolidPickKind : std::uint8_t {
  Typed,     ///< keyword + value only; never picked.
  Radius,    ///< distance from the base point, measured IN the work plane.
  Height,    ///< signed distance along the work plane's normal, from the axis nearest the cursor ray.
  CornerXY,  ///< box / wedge: one pick sets length AND width from the opposite corner.
};

struct SolidParamSpec {
  char letter = '\0';
  const char* label = "";
  bool optional = false;
  SolidPickKind pick = SolidPickKind::Typed;
  /// What an optional parameter is worth when the user never sets it — a cone's apex (0) and a
  /// pyramid's four sides. Ignored unless \ref optional.
  double defaultValue = 0.0;
};

/// The named dimensions of \p kind, in the order a bare typed number fills them — which is also the
/// order the one-line form takes its arguments, so `CYLINDER 0,0 4 25` and `CYLINDER` / `0,0` /
/// `4` / `25` mean the same thing.
///
/// **The single table both the prompt and the commit read.** A prompt that offered a letter the
/// commit did not know, or a commit that needed a value the prompt never asked for, is the failure
/// this exists to make impossible.
[[nodiscard]] const SolidParamSpec* CadSolidParamSpecs(brep::PrimitiveKind kind, int* outCount);

/// The placement frame the prompted solid command is building in: the active UCS's orientation moved
/// to the base point. Exposed so the live preview measures its rubber line along the SAME axes the
/// solid is built on - a measuring line drawn on the world frame would drift off a tilted solid.
[[nodiscard]] ucs::Ucs CadSolidPlacementFrameFor(const AppCommandState& st);

/// Work out what the cursor is currently worth to the prompted solid command, and publish it on
/// \p st as `solidPickValid` / `solidPickA` / `solidPickB` / `solidPickAngleRad`.
///
/// \p cursorOnPlane is the cursor resolved onto the work plane, in storage coordinates.
/// \p ray is the pick ray, or null in plan view.
///
/// **Domain logic, not viewport logic**, even though the viewport is what calls it every frame: it
/// is geometry — a distance in a plane, an angle, a closest approach between a ray and an axis — and
/// putting it here is what lets a transcript drive the same resolution the mouse does. A height
/// resolved one way for the preview and another for the test would be a test of nothing.
void CadResolveSolidPick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray);

/// Index of the dimension the prompted command is currently picking — the first required one still
/// unset whose \ref SolidParamSpec::pick is not `Typed`. -1 when nothing is left to pick.
[[nodiscard]] int CadSolidCurrentPickParam(const AppCommandState& st);

/// Build the solid the prompted command currently describes.
///
/// **The one place a set of numbers becomes a shape**, called by the live preview, by the click that
/// commits a dimension, and by Enter. A preview computed separately from the commit is a preview
/// that eventually shows a solid the click does not build, which is worse than no preview at all.
///
/// \p applyPick folds `solidPickA`/`solidPickB` into the dimension currently being picked, which is
/// what makes the preview follow the cursor; false uses only what has been committed.
///
/// Returns false with \p outWhy set when the numbers so far do not describe a solid — a zero radius
/// before the cursor has moved, a dimension still missing. The preview simply draws nothing then,
/// which is the honest answer while a value is still being chosen.
[[nodiscard]] bool CadBuildSolidFromCommand(const AppCommandState& st, bool applyPick, brep::Solid* out,
                                            brep::Problem* outWhy);

/// Begin the prompted form of a primitive command: `CYLINDER` with no arguments. \p verb is the
/// already-lowercased command token.
void StartSolidPrimitiveCommand(AppCommandState& st, const std::string& verb, std::vector<std::string>& log);

/// The prompt for whatever the solid command is waiting for — the base point, or the named
/// dimensions with the ones already set shown back. Shared by the command line and the at-cursor
/// dynamic input so the two cannot say different things (REQ-304).
[[nodiscard]] std::string CadSolidPromptText(const AppCommandState& st);

/// Feed one typed line to the running solid command. Returns false when the text was not understood,
/// which leaves the command where it was rather than cancelling it.
[[nodiscard]] bool HandleSolidTextInput(const std::string& line, AppCommandState& st,
                                        std::vector<std::string>& log);

/// Feed a picked point (storage X/Y, at the current work-plane elevation) to the running solid
/// command.
void SubmitSolidViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// Clear the prompted solid command's state. Called by Esc and by every other command start, so a
/// half-built solid cannot leak into the next command.
void CancelSolidCommand(AppCommandState& st);

// --- REQ-317 POLYSOLID ---------------------------------------------------------------------------
/// Open the command: pick a start point, or `O` to sweep along something already drawn.
void StartPolysolidCommand(AppCommandState& st, std::vector<std::string>& log);
/// The prompt line, computed rather than literal: it echoes the height, width and justification in
/// force, which is what makes them discoverable without a separate report command (REQ-304).
[[nodiscard]] std::string CadPolysolidPromptText(const AppCommandState& st);
/// Handle one typed line: a coordinate, or one of `A L C U H W J O`. \return false if not consumed.
bool HandlePolysolidTextInput(const std::string& line, AppCommandState& st,
                              std::vector<std::string>& log);
/// Handle a viewport click: a path point, or — at the `O`bject prompt — the entity to sweep along.
void SubmitPolysolidViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Convert the Line / Arc / Circle / Polyline under (\p wx, \p wy) into a wall, or say why not.
void CadPolysolidConvertObjectAt(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Reset the path, keeping the remembered height, width and justification.
void CancelPolysolidCommand(AppCommandState& st);
/// The frame a polysolid is built in: the active UCS anchored at the first picked point. Exposed so
/// the viewport can put the cursor into the same plane the builder reads it from - one frame, not two.
[[nodiscard]] ucs::Ucs CadPolysolidFrameFor(const AppCommandState& st);
/// The candidate wall, optionally including the segment \p cursor is currently proposing.
///
/// ONE builder for the preview, the click that commits a point and the Enter that finishes — a
/// preview computed separately from the commit is a preview that eventually shows a wall the click
/// does not build (ADR-046 (a)).
[[nodiscard]] bool CadBuildPolysolidFromCommand(const AppCommandState& st, const ucs::Point2D* cursor,
                                                brep::Solid* out, brep::Problem* outWhy);

/// Report a solid's properties into \p log — kind, dimensions, volume, surface area, and its
/// vertex/edge/face counts. The SOLIDLIST command, and the one place those numbers are formatted.
void CadReportSolids(const AppCommandState& st, std::vector<std::string>& log);

/// EXTRUDE (REQ-314 / ADR-046, GitHub issue #147): turn each eligible entity in the current
/// selection — a closed polyline or a circle — into a B-rep solid, swept a signed height
/// perpendicular to the profile's plane. One undo step; the source entities are left in place.
/// Every failure is reported by name and stores nothing (REQ-201).
///
/// `CadExtrudeSelection` is the one-line shortcut `EXTRUDE <height>`. `StartExtrudeCommand` is the
/// prompted form a bare `EXTRUDE` opens: select objects (if none are yet), then a height that can
/// be typed or dragged from the cursor with a live ghost.
void CadExtrudeSelection(AppCommandState& st, const std::string& rest, std::vector<std::string>& log);
void StartExtrudeCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelExtrudeCommand(AppCommandState& st);

/// The prompt for whatever the EXTRUDE command is waiting for — the selection, or the height with
/// the live cursor value shown. Shared by the command line and the at-cursor dynamic input (REQ-304).
[[nodiscard]] std::string CadExtrudePromptText(const AppCommandState& st);

/// Feed one typed line to a running EXTRUDE command. Returns false when the text was not understood,
/// which leaves the command where it was rather than cancelling it.
[[nodiscard]] bool HandleExtrudeTextInput(const std::string& line, AppCommandState& st,
                                          std::vector<std::string>& log);

/// A viewport click during EXTRUDE: confirms the selection (SelectProfiles) or commits the solid at
/// the cursor-resolved height (WaitHeight).
void SubmitExtrudeViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// Resolve what the cursor is currently worth as an extrusion height and publish it on \p st as
/// `extrudeHeightPickValid` / `extrudeHeightPick`. Domain logic, not viewport logic — it is the
/// closest approach between the cursor ray and the profile's plane normal, the same geometry the
/// prompted solid command's Height pick uses. \p cursorOnPlane is storage coordinates; \p ray is
/// the pick ray, or null in plan view (where a height cannot be read off the screen).
void CadResolveExtrudePick(AppCommandState& st, const ray3d::Vec3& cursorOnPlane, const ray3d::Ray* ray);

/// The candidate solids the running EXTRUDE command describes at \p height, for the live ghost and
/// for the commit — one function, so the ghost cannot show a shape the click would not build.
/// Returns false (and clears \p out) when the numbers do not yet describe a solid.
[[nodiscard]] bool CadBuildExtrudeSolids(const AppCommandState& st, double height,
                                         std::vector<brep::Solid>* out);

// --- LOFT (REQ-315 / ADR-048, GitHub issue #241) ---------------------------------------------

/// Begin the LOFT command: select two or more closed polylines / circles **in lofting order**, then
/// Enter to skin one B-rep solid through them (`brep::Loft` — freeform `SurfaceKind::Nurbs` side
/// faces). If two or more eligible profiles are already selected, a bare `LOFT` builds immediately.
/// One undo step; the source entities are left in place; every failure is reported by name (REQ-201).
void StartLoftCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelLoftCommand(AppCommandState& st);

/// The prompt for the running LOFT command — always the "select profiles" line, with a count of what
/// is selected so far. Shared by the command line and the at-cursor dynamic input (REQ-304).
[[nodiscard]] std::string CadLoftPromptText(const AppCommandState& st);

/// Feed one typed line to a running LOFT command. An empty line (Enter) builds; anything else is not
/// understood and leaves the command running. Returns false when the command is not LOFT.
[[nodiscard]] bool HandleLoftTextInput(const std::string& line, AppCommandState& st,
                                       std::vector<std::string>& log);

/// The candidate solid the running LOFT command describes from the current selection, for the live
/// ghost and for the commit — one function, so the ghost cannot show a shape the Enter would not
/// build. Returns false when the selection does not yet hold two loftable profiles.
[[nodiscard]] bool CadBuildLoftSolid(const AppCommandState& st, brep::Solid* out);

// --- SWEEP (REQ-315 / ADR-048, GitHub issue #241) -------------------------------------------

/// Begin the SWEEP command: select **one** closed polyline / circle (the profile) and **one** line,
/// arc or open polyline (the path), then Enter to sweep the profile along the path (`brep::Sweep`).
/// The closed loop is taken as the profile and the open curve as the path, so the selection order
/// does not matter. A polyline path's per-vertex bulges become its arc segments (REQ-316); a sharp
/// corner in the path is refused by name (`brep::Problem::SweepPathCorner`). If both operands are
/// already selected, a bare `SWEEP` builds immediately. One undo step; the source entities are left
/// in place; every failure is reported by name (REQ-201).
void StartSweepCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelSweepCommand(AppCommandState& st);

/// The prompt for the running SWEEP command. Shared by the command line and the at-cursor dynamic
/// input (REQ-304).
[[nodiscard]] std::string CadSweepPromptText(const AppCommandState& st);

/// Feed one typed line to a running SWEEP command. An empty line (Enter) builds; anything else is
/// not understood. Returns false when the command is not SWEEP.
[[nodiscard]] bool HandleSweepTextInput(const std::string& line, AppCommandState& st,
                                        std::vector<std::string>& log);

/// The candidate solid the running SWEEP command describes from the current selection, for the live
/// ghost and the commit. Returns false when the selection does not yet hold a profile and a path.
[[nodiscard]] bool CadBuildSweepSolid(const AppCommandState& st, brep::Solid* out);

// --- REVOLVE (REQ-314 / ADR-046 increment 2b) -------------------------------------------------

/// Begin the prompted REVOLVE command a bare `REVOLVE` opens: select a closed polyline or circle
/// (if none is selected), then the two ends of the revolve axis, then an angle. `REVOLVE <deg>`
/// with a selection and axis is not offered — the axis needs two points.
void StartRevolveCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelRevolveCommand(AppCommandState& st);
[[nodiscard]] std::string CadRevolvePromptText(const AppCommandState& st);
[[nodiscard]] bool HandleRevolveTextInput(const std::string& line, AppCommandState& st,
                                          std::vector<std::string>& log);
void SubmitRevolveViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// The candidate solids the running REVOLVE command describes at \p angleDeg — for the live ghost
/// and the commit. False (and \p out cleared) until a profile and both axis points are set.
[[nodiscard]] bool CadBuildRevolveSolids(const AppCommandState& st, double angleDeg,
                                         std::vector<brep::Solid>* out);

// --- UNION / SUBTRACT / INTERSECT (REQ-314 / ADR-046 increment 4, B1) -----------------------

enum class CadBooleanOp { Union, Subtract, Intersect };

/// Combine the two B-rep solids in the current selection (REQ-314 B1). UNION and INTERSECT do not
/// care about order; SUBTRACT keeps the first selected solid and removes the second. Both operands
/// are replaced by the result in one undo step. A pair the kernel refuses (a curved or non-convex
/// operand, no shared volume for INTERSECT) is reported and nothing in the document changes
/// (REQ-201). Needs exactly two selected solids.
void CadBooleanSelection(AppCommandState& st, CadBooleanOp op, std::vector<std::string>& log);

/// Begin the prompted UNION / SUBTRACT / INTERSECT command. SUBTRACT prompts for the solids to
/// subtract from, then the solids to subtract; UNION and INTERSECT prompt once. Honors a
/// pre-selection as the answer to the first prompt.
void StartBooleanCommand(AppCommandState& st, CadBooleanOp op, std::vector<std::string>& log);
void CancelBooleanCommand(AppCommandState& st);
[[nodiscard]] std::string CadBooleanPromptText(const AppCommandState& st);
[[nodiscard]] bool HandleBooleanTextInput(const std::string& line, AppCommandState& st,
                                          std::vector<std::string>& log);

// --- SLICE (REQ-314 / ADR-046 increment 3b) -------------------------------------------------

/// Begin the prompted SLICE command a bare `SLICE` opens: select solids (if none are selected),
/// three points for the cutting plane, then a point on the side to keep (or `B` for both).
void StartSliceCommand(AppCommandState& st, std::vector<std::string>& log);
void CancelSliceCommand(AppCommandState& st);
[[nodiscard]] std::string CadSlicePromptText(const AppCommandState& st);
[[nodiscard]] bool HandleSliceTextInput(const std::string& line, AppCommandState& st,
                                        std::vector<std::string>& log);
void SubmitSliceViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// REQ-075: "a surface that is out of date or rebuilding is shown as such, and the state clears when
/// the rebuild lands." Shared by the Surface Manager and the Volume Dashboard (TASK-095) — both need
/// the identical current/stale/rebuilding classification for a surface, and it has no ImGui
/// dependency, so it lives here rather than in either panel.
enum class SurfaceState { Current, Stale, Rebuilding };

/// \p surfaceIndex's current/stale/rebuilding state. Both non-`Current` states are already knowable
/// without any new bookkeeping: a rebuild in flight is an entry in \c surfaceRebuildAsync, and
/// out-of-date is REQ-069's own dirty check (\c builtAtRevision vs \c cadGpuRevision).
[[nodiscard]] SurfaceState SurfaceRebuildStateOf(const AppCommandState& st, size_t surfaceIndex);

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

/// REQ-127: interpolated Z of the last covering visible surface at plan (x,y), or false if none.
[[nodiscard]] bool SurfaceSnapElevation(const AppCommandState& st, double x, double y, float* outZ);

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

/// REQ-136: named TIN volume surface (comparison minus base). Returns the new index, or -1.
int CreateSurfaceFromVolumeParents(AppCommandState& st, const std::string& name, const std::string& baseName,
                                   const std::string& comparisonName, std::vector<std::string>& log);

/// If more than one surface draws with this surface's resolved style, copy that style to a unique
/// name and point only this surface at the copy. Named-style sharing (REQ-070) stays available via
/// SURFSTYLE ASSIGN. Returns true when a copy was made.
bool DetachSurfaceStyleIfShared(AppCommandState& st, size_t surfaceIndex, std::vector<std::string>* log);

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

/// Advances the Volume Dashboard's own live recompute (REQ-073 amendment, TASK-095). Call every
/// frame, after \ref TickSurfaceRebuilds so a surface that finished rebuilding this frame is already
/// current when this checks it.
void TickVolumeDashboard(AppCommandState& st);

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
    if (st.entityGripOrigPolyBulgeVi >= 0) {  // REQ-316 / ADR-047: an arc-segment bulge grip
      if (static_cast<size_t>(st.entityGripOrigPolyBulgeVi) < st.userPolylineVertsBulge.size())
        st.userPolylineVertsBulge[static_cast<size_t>(st.entityGripOrigPolyBulgeVi)] = st.entityGripOrigPolyBulge;
      break;
    }
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
  case SelectedEntity::Type::BlockRef: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.cadBlockRefs.size())
      return;
    CadBlockRef& r = st.cadBlockRefs[static_cast<size_t>(idx)];
    CadBlockParamSet(&r, "DistNeg", st.entityGripOrigX0);
    CadBlockParamSet(&r, "DistPos", st.entityGripOrigY0);
    CadBlockParamSet(&r, "SheetOff", st.entityGripOrigX1);
    CadBlockParamSet(&r, "NorthOff", st.entityGripOrigY1);
    CadBlockParamSet(&r, "Flip", st.entityGripOrigR);
    r.xf.x = st.entityGripOrigCx;
    r.xf.y = st.entityGripOrigCy;
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
///
/// **Interpreted in the active UCS** (REQ-154): under a rotated UCS `10,0` is 10 units along the UCS
/// X axis, not the world's. Under the WCS — the default, and every drawing that predates the UCS
/// command — this is the original world-frame parse, unchanged.
bool ParseStoragePoint(AppCommandState& st, const std::string& raw, float* lx, float* ly, bool allowRelative,
                       float baseLocalX, float baseLocalY);

/// \ref ParseStoragePoint, additionally reporting the resolved point's world Z. Callers that are
/// about to commit geometry want this: on a tilted UCS the work plane's elevation varies across it,
/// so the point's own Z is the only correct answer (see AppCommandState::resolvedPointZ).
bool ParseStoragePointZ(AppCommandState& st, const std::string& raw, float* lx, float* ly, double* outWorldZ,
                        bool allowRelative, float baseLocalX, float baseLocalY);

/// Split a typed point into its two numbers and whether it carried a leading `@`, without deciding
/// which frame those numbers are in. That separation is what lets one parser serve both frames.
bool ParsePointComponents(const std::string& raw, double* a, double* b, bool* isRelative, bool allowRelative);

bool ParseWorldPoint(const std::string& raw, float* ox, float* oy, bool allowRelative, float baseX, float baseY);

/// Double-precision form, and the real implementation — the float overload above narrows its result.
/// Prefer this wherever the value is destined for the local storage frame: narrowing before the
/// document origin is subtracted quantizes at world magnitude and silently violates REQ-101 (at
/// easting 2e6, `2000000.10` became `2000000.125`). See the definition for the measurement.
bool ParseWorldPointD(const std::string& raw, double* ox, double* oy, bool allowRelative, double baseX,
                      double baseY);

/// If ortho: snaps dx/dy so segment from anchor is horizontal or vertical (CAD-style).
/// When \p ortho is false but \p st has POLAR tracking on, applies the polar snap instead — the two
/// share this one entry point so every existing ortho call site picks up polar with no change
/// (\ref AppCommandState::polarMode is mutually exclusive with ortho).
void ApplyOrthoConstrainFromAnchor(const AppCommandState& st, float anchorX, float anchorY, float* wx, float* wy,
                                   bool ortho);

/// POLAR tracking (issue #154, REQ-154): snap the world pick onto the nearest polar ray around the
/// anchor, measured in the active UCS's XY plane from +X. No-op unless \p polar and
/// \ref AppCommandState::polarMode. Called from \ref ApplyOrthoConstrainFromAnchor; exposed for the
/// preview paths that want it explicitly.
void ApplyPolarConstrainFromAnchor(const AppCommandState& st, float anchorX, float anchorY, float* wx, float* wy,
                                   bool polar);

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

// --- UCS and PLAN (REQ-154, GitHub #126) --------------------------------------------------------

/// Make \p next the active UCS: pushes the outgoing frame onto the Previous stack, honours
/// UCSFOLLOW, and reports the change.
///
/// **Every** path that changes the UCS goes through here — that is what makes "Previous" and
/// UCSFOLLOW work for options added later without each one remembering to maintain them.
/// \p pushPrevious is false only for `UCS Previous` itself, which must not push the state it is
/// popping (AutoCAD's rule: restoring a previous UCS does not add a history entry).
void SetActiveUcs(AppCommandState& st, const ucs::Ucs& next, std::vector<std::string>& log,
                  bool pushPrevious = true);

/// UCS: the top-level prompt.
void StartUcsCommand(AppCommandState& st, std::vector<std::string>& log);
/// PLAN: the view-orientation prompt.
void StartPlanCommand(AppCommandState& st, std::vector<std::string>& log);

/// Feed one typed line to whichever of UCS / PLAN is active. Returns true when the line was
/// consumed. Shared by the command line and the at-cursor dynamic input (REQ-024), so both accept
/// exactly the same keywords.
bool ProcessUcsCommandLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log);
bool ProcessPlanCommandLine(AppCommandState& st, const std::string& line, std::vector<std::string>& log);

/// Feed a viewport pick (world coordinates) to the UCS command. Returns true when consumed.
bool ProcessUcsViewportPick(AppCommandState& st, const ray3d::Vec3& worldPoint, std::vector<std::string>& log);

/// Orient the view to a PLAN view of \p frame without touching the active UCS.
void ApplyPlanViewOf(AppCommandState& st, const ucs::Ucs& frame, std::vector<std::string>& log);

/// Find a saved UCS by name, case-insensitively. Returns nullptr when there is none.
const NamedUcs* FindNamedUcs(const AppCommandState& st, const std::string& name);

/// Save / restore / delete a named UCS (REQ-154).
///
/// Only \ref SaveNamedUcs is reachable from the `UCS` command: `UCS Named` asks for a name and
/// nothing else. Restore and delete live in the View Manager, where the saved frames are listed and
/// a user can see what they are choosing between - having to recall a name you cannot see was the
/// weakest part of the old Save/Restore/Delete sub-prompt. The dialog calls these functions, so the
/// two halves still share one implementation and cannot drift.
///
/// All three refuse the reserved name "World". Restore and delete return false, and say so in
/// \p log, when no such name is saved.
void SaveNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log);
bool RestoreNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log);
bool DeleteNamedUcs(AppCommandState& st, const std::string& rawName, std::vector<std::string>& log);

/// List the saved UCS definitions to \p log - what `UCS ?` prints.
void ListNamedUcs(const AppCommandState& st, std::vector<std::string>& log);

/// A one-line description of a frame, in WORLD coordinates - the only frame a UCS can sensibly be
/// stated in. Shared by the command log and the View Manager so the two cannot describe a saved
/// frame differently.
std::string DescribeUcs(const ucs::Ucs& u);

/// Named views (REQ-106). Same shape as the UCS Named helpers above, deliberately.
const NamedView* FindNamedView(const AppCommandState& st, const std::string& name);
/// The saved view the camera is currently in, or nullptr — derived, never remembered.
const NamedView* CurrentNamedView(const AppCommandState& st);
NamedView CaptureCurrentView(const AppCommandState& st, const std::string& name);
void RestoreNamedView(AppCommandState& st, const NamedView& v, std::vector<std::string>& log);
void ListNamedViews(const AppCommandState& st, std::vector<std::string>& log);
bool ProcessViewCommandLine(AppCommandState& st, const std::string& rest, std::vector<std::string>& log);

/// Apply ORTHO in the active UCS's axes rather than the world's (REQ-047 under REQ-154).
///
/// \p anchor and \p target are world 3D points; the constrained target is written back. Under the
/// WCS this reduces exactly to the world-axis constraint the 2D path always applied.
ray3d::Vec3 ConstrainToUcsOrtho(const ucs::Ucs& frame, const ray3d::Vec3& anchor, const ray3d::Vec3& target);

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
void StartDimStyleCommand(AppCommandState& st, std::vector<std::string>& log);
void StartIdPointCommand(AppCommandState& st, std::vector<std::string>& log);

/// REQ-074: pick a point for its interpolated surface elevation; pick a second for the grade
/// between them. Reports every surface covering the pick, by name.
void StartSurfaceElevGradeCommand(AppCommandState& st, std::vector<std::string>& log);
void StartWaterDropCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartCatchmentCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfSwapEdgeCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfAddPointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfDelPointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfMovePointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfDelLineCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartQuickProfileCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);

/// REQ-069: designate one picked Line/Polyline as a breakline on the named surface, appended to its
/// definition by stable entity id. Refuses to start when \p surfaceName does not name an existing
/// surface, reported before the pick rather than after it (REQ-201).
void StartDesignateBreaklineCommand(AppCommandState& st, const std::string& surfaceName,
                                    std::vector<std::string>& log);
void StartDesignateContourCommand(AppCommandState& st, const std::string& surfaceName,
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
void StartMirrorCommand(AppCommandState& st, std::vector<std::string>& log);
void StartArrayCommand(AppCommandState& st, std::vector<std::string>& log);
void StartLengthenCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for LENGTHEN. Non-static (unlike most
/// of its cluster) purely so SubmitViewportPickImpl — inside an anonymous namespace that spans
/// this function's definition — can see it via this header; not intended for cross-TU use.
void HandleLengthenViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
void StartExtendCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for EXTEND. Non-static (unlike most of
/// its cluster) for the same anonymous-namespace/global-scope reason `HandleLengthenViewportPick`
/// is — `SubmitViewportPickImpl` needs to see it via this header.
void HandleExtendViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
void StartBreakCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for BREAK. Non-static for the same
/// anonymous-namespace/global-scope reason `HandleLengthenViewportPick`/`HandleExtendViewportPick`
/// are — `SubmitViewportPickImpl` needs to see it via this header.
void HandleBreakViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// REQ-103 step 5. Model-space branch always forces a fresh box-select (\c ModifyPhase::PickSelection),
/// even if \c st.selection is already non-empty, since STRETCH's box rectangle must be captured
/// together with the selection (unlike MOVE/COPY/ROTATE/SCALE, which happily reuse a pre-existing
/// selection). Paper-space branch instead requires \c st.selectedPaperEntities already non-empty —
/// see \c paperStretchPhase's comment for why paper space follows the opposite convention.
void StartStretchCommand(AppCommandState& st, std::vector<std::string>& log);
void StartFilletCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for FILLET. Non-static for the same
/// anonymous-namespace/global-scope reason `HandleLengthenViewportPick`/`HandleExtendViewportPick`/
/// `HandleBreakViewportPick` are — `SubmitViewportPickImpl` needs to see it via this header.
void HandleFilletViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Typed command-line handling for FILLET's R(adius)/T(rim) sub-commands (REQ-103 step 6a) — the
/// mode-letter shape LENGTHEN's own `HandleLengthenText` established, simplified: FILLET has no
/// pending-pick-awaiting-a-value latch, since R/T only ever change a persisted setting, never
/// apply to an already-picked object.
bool HandleFilletText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log);
void StartChamferCommand(AppCommandState& st, std::vector<std::string>& log);
/// Model-space + floating-model-space viewport-pick handler for CHAMFER. Non-static for the same
/// anonymous-namespace/global-scope reason `HandleFilletViewportPick` is.
void HandleChamferViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);
/// Typed command-line handling for CHAMFER's D(istance)/A(ngle)/T(rim) sub-commands (REQ-103 step
/// 6b) — same shape as `HandleFilletText`.
bool HandleChamferText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log);
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
/// \param outDistSq Optional: pass null when only the entity matters, not how near the pick was.
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
[[nodiscard]] bool PickSubObjectAcrossSolids(const AppCommandState& st, const ray3d::Ray& ray,
                                             const solidpick::Tolerance& tol, SelectedSubObject* out,
                                             solidpick::Pick* outPick = nullptr);

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
void ApplyEntityGripPoint(AppCommandState& st, float x, float y);

void SelectSimilarToCurrentSelection(AppCommandState& st, std::vector<std::string>* log);

// --- The translate gizmo (REQ-060, GitHub issue #148 Phase 5 slice 4b) --------------------------
//
// One rule decides the shape of everything below: **the gizmo commits through
// `ApplyTranslationToSelection`, the same function typed MOVE calls.** REQ-060 requires that "a
// gizmo drag and the equivalent typed command produce coordinates agreeing within REQ-101"; going
// through the one function makes that hold by construction rather than by two implementations
// happening to match, which is the failure mode a tolerance-based acceptance invites.
//
// The gizmo aligns with the ACTIVE UCS, not with world axes. Everything else in this program that
// takes a direction from the user - the grid, ORTHO, coordinate entry - follows the UCS (REQ-154),
// and in the World UCS the two are identical, so the default view is unchanged.

/// Translate the whole selection by (\p dx, \p dy, \p dz) in WCS (REQ-322). Surfaces are dropped by
/// name (REQ-201); solids move through `brep::Translate`. The caller owns the undo snapshot.
///
/// Declared here — it had been a definition private to `CadCommands.cpp` — because REQ-060's
/// acceptance is that a gizmo drag and the typed command agree, and the only way to make that a
/// property rather than a hope is for both to call this. A test that asserts the agreement has to be
/// able to name it too.
void ApplyTranslationToSelection(AppCommandState& st, float dx, float dy, float dz,
                                 std::vector<std::string>& log);

/// What the gizmo is currently acting on, derived from the selection and never stored.
///
/// The two selections are mutually exclusive already (D-2026-09-04-a), so a stored mode would be a
/// third thing that can disagree with them. `None` is the answer whenever no gizmo may be drawn —
/// which is REQ-060's third acceptance bullet and, for a face, the honest answer for a selection the
/// kernel has no operation for.
enum class CadGizmoMode {
  None,
  /// The entity selection: three handles along the active UCS, committing through
  /// `ApplyTranslationToSelection`.
  Entity,
  /// Exactly one solid FACE: ONE handle along that face's own normal, committing through
  /// `CadApplyPushPull` — so a drag and the typed `PRESSPULL` agree by construction (issue #148
  /// acceptance 4).
  ///
  /// One handle and not three, because `brep::PushPullFace` takes a distance along the face normal
  /// and nothing else. A handle along UCS X on a face whose normal is Z would advertise a move the
  /// kernel cannot make; drawing it and then refusing the drag is worse than not drawing it.
  SubObjectFace,
};

/// Which subject the gizmo has right now. \c None when no gizmo may be drawn.
[[nodiscard]] CadGizmoMode CadGizmoModeFor(const AppCommandState& st);

/// The one selected face, when \ref CadGizmoModeFor is \c SubObjectFace. False otherwise.
[[nodiscard]] bool CadGizmoSubObjectFace(const AppCommandState& st, SelectedSubObject* out);

/// Maximum gizmo axes. Three: the UCS X, Y and Z. Named so the loops below say why they are 3.
inline constexpr int kGizmoAxisCount = 3;

/// How many handles the gizmo actually has right now: 3 in entity mode, 1 in face mode, 0 for none.
[[nodiscard]] int CadGizmoAxisCountFor(const AppCommandState& st);
/// Handle length, in screen pixels, held constant at every zoom (REQ-060's "as displayed").
inline constexpr float kGizmoHandleLenPx = 70.f;
/// Grab aperture around a handle, in screen pixels.
inline constexpr float kGizmoHandleGrabPx = 7.f;

/// Where the gizmo hangs, in WCS. False when there is nothing for it to hang off.
///
/// **Entity mode:** the centre of the selection's bounding box. Its precision does not affect any
/// move — a drag distance is the change in the axis parameter between the grab and the drop, so the
/// anchor appears in both terms and cancels, and it decides only where the handles are DRAWN. That
/// is why conservative per-type bounds are good enough, and why this is deliberately not
/// \ref ComputeSelectionCentroidWorld: that answers ROTATE's different question (a pivot, in plan,
/// over ROTATE's own type set), and changing it to serve this one would change where ROTATE and
/// ARRAY turn things about.
///
/// **Face mode:** the face's centroid (\ref CadSubObjectFaceGrip), where the grip this replaced
/// already put its handle. A corner would read as a vertex grip, which is a different edit.
[[nodiscard]] bool CadGizmoAnchorWorld(const AppCommandState& st, ray3d::Vec3* out);

/// Unit direction of gizmo axis \p axis in WCS: the active UCS's X / Y / Z in entity mode, and the
/// selected face's outward normal (axis 0, the only one) in face mode.
[[nodiscard]] ray3d::Vec3 CadGizmoAxisWorld(const AppCommandState& st, int axis);

/// Handle length in drawing units for the current view - \ref kGizmoHandleLenPx converted through
/// the same ortho half-height \ref CadViewCamera builds its projection from, so the handle is the
/// stated pixel length on screen and a transcript with no window still gets a definite answer.
[[nodiscard]] float CadGizmoHandleLenWorld(const AppCommandState& st);

/// True when a gizmo should be drawn at all: \ref CadGizmoModeFor is not \c None and an anchor
/// resolves. REQ-060's third acceptance bullet ("no gizmo when the selection is empty") is this.
[[nodiscard]] bool CadGizmoVisible(const AppCommandState& st);

/// Signed position along the line (\p anchor, \p axisDir) of the point on it nearest \p ray.
///
/// The standard skew-line solve. False when the ray is within \p parallelTol of parallel to the
/// axis - looking straight down a handle, every point of it projects to the same pixel and there is
/// no distance the gesture could mean. Refusing is the honest answer; the alternative is a huge
/// number from a near-singular divide, which reads as the selection flying off the screen.
[[nodiscard]] bool CadAxisDragParam(const ray3d::Vec3& anchor, const ray3d::Vec3& axisDir,
                                    const ray3d::Ray& ray, double* outParam,
                                    double parallelTol = 1.e-6);

/// Which axis handle \p ray hits, or -1. \p tolWorld is the grab aperture in drawing units.
///
/// Nearest handle wins, measured as the true 3D distance between the ray and the handle SEGMENT -
/// so a handle pointing away from the camera, which projects to almost nothing, is hard to grab by
/// accident and the one pointing across the view is easy.
[[nodiscard]] int PickGizmoAxis(const AppCommandState& st, const ray3d::Ray& ray, double tolWorld);

/// One click on the gizmo, in the command layer where a transcript can drive it.
///
/// Grabs the handle under \p ray if one is there and nothing is armed yet; otherwise commits the
/// armed drag at the ray's position. Returns true when the click was the gizmo's - the caller must
/// then NOT treat it as an ordinary selection click.
bool SubmitGizmoClick(AppCommandState& st, const ray3d::Ray& ray, double tolWorld,
                      std::vector<std::string>& log);

/// Refresh \ref AppCommandState::gizmoDragDistance from the cursor. No-op when nothing is armed.
void UpdateGizmoDrag(AppCommandState& st, const ray3d::Ray& ray);

/// Refresh \ref AppCommandState::gizmoHoverAxis from the cursor. No-op while a drag is armed - the
/// grabbed handle stays lit, and a pre-highlight of a handle the click cannot reach is a lie.
void UpdateGizmoHover(AppCommandState& st, const ray3d::Ray& ray, double tolWorld);

/// Apply the armed drag as ONE undoable step, and disarm.
///
/// Entity mode goes through `ApplyTranslationToSelection`, the function typed MOVE calls; face mode
/// goes through `CadApplyPushPull`, the function typed `PRESSPULL` calls. Both agreements are then
/// structural rather than two implementations that happen to match today — REQ-060's second
/// acceptance bullet and issue #148's fourth, stated the same way.
///
/// False (and nothing changed) when no drag is armed, when the distance is zero, or when the kernel
/// refuses the push - in which case `CadApplyPushPull` has already logged its own sentence and the
/// document is untouched.
bool CommitGizmoDrag(AppCommandState& st, std::vector<std::string>& log);

/// Disarm without moving anything. Safe to call at any time; ESC and a right-click both do.
void CancelGizmoDrag(AppCommandState& st);
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

bool ComputeCircumcircle(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                         float* r);

bool LoadApplicationFont();
