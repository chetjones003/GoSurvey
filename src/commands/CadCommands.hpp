#pragma once

#include "PdfAttach.hpp"
#include "SurveyPoints.hpp"
#include "AngleFormat.hpp"
#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>


struct SelectedEntity {
  enum class Type { LineSeg = 0, Circle = 1, Annotation = 2, Polyline = 3, Arc = 4, Ellipse = 5, PdfUnderlay = 6 };
  Type type = Type::LineSeg;
  int index = 0; ///< Entity index in the parallel container for \p type
};


/// Display / CAD metadata stored per entity (parallel index to line segments or circles).
struct EntityAttributes {
  std::string layer = "0";
  std::string color = "ByLayer";
  /// "ByLayer", "ByBlock", or a linetype table name such as "Continuous" / "DASHED" / "CENTER".
  std::string linetype = "ByLayer";
  /// Millimetres on paper; \c -1.f means ByLayer (DXF 370 = -1).
  float lineweightMm = -1.f;
  /// 0 = opaque, 1 = fully transparent; \c -1.f means ByLayer.
  float transparency = -1.f;
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
};


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


/// Single-line TEXT, MTEXT box, or aligned linear dimension drawn over the viewport (world coordinates).
struct CadAnnotation {
  enum class Kind { Text = 0, Mtext = 1, DimAligned = 2, DimLinear = 3, DimAngular = 4 };
  Kind kind = Kind::Text;
  float insX = 0.f;
  float insY = 0.f;
  /// Text height in plotted inches (constant on sheet); model height = this × drawing scale.
  float plottedHeightInches = 0.125f;
  /// CCW from +X (math \c atan2); UI shows clockwise-from-north via \c BearingCwNorthDegFromMathAngleRad.
  float rotationRad = 0.f;
  std::string text;
  float boxMinX = 0.f, boxMinY = 0.f, boxMaxX = 0.f, boxMaxY = 0.f;
  /// \c Kind::DimAligned / \c DimLinear / \c DimAngular — extension or ray points (on measured geometry).
  float dimExt1X = 0.f, dimExt1Y = 0.f, dimExt2X = 0.f, dimExt2Y = 0.f;
  /// \c Kind::DimAngular — vertex (center) of the measured angle.
  float dimAngVertexX = 0.f;
  float dimAngVertexY = 0.f;
  /// Signed distance from chord midpoint to dimension line: aligned uses N=(-T.y,T.x) with T=normalize(e2-e1); linear uses N=(0,1) for horizontal span or N=(1,0) for vertical span; angular uses arc radius (world units).
  float dimSignedOffset = 0.f;
  /// \c Kind::DimLinear only — if true, measures |Y2−Y1| with a vertical dimension line; if false, |X2−X1| with horizontal dim line.
  bool dimLinearVertical = false;
  /// If >= 0, this MTEXT is the viewport label for \c surveyPoints[this index] (bidirectional link).
  int surveyPointLabelFor = -1;
  /// When true, the label was manually dragged and these world-unit offsets from the point override the global defaults.
  bool surveyLabelHasUserOffset = false;
  float surveyLabelUserOffsetEast = 0.f;
  float surveyLabelUserOffsetNorth = 0.f;
};


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

/// Committed 3-point arc (circumcircle + start/sweep in radians from +X).
struct CadArc {
  float cx = 0.f;
  float cy = 0.f;
  float r = 0.f;
  float startRad = 0.f;
  float sweepRad = 0.f;
};

/// Axis-aligned ellipse: center + major-axis vector (semi-major length = |majV|) + minor/major ratio (0,1].
struct CadEllipse {
  float cx = 0.f;
  float cy = 0.f;
  float majVx = 1.f;
  float majVy = 0.f;
  float ratio = 0.5f;
};

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
  const std::vector<CadLayerRow>* drawingLayers = nullptr;
};


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
  std::vector<float>            circles;
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

  bool empty() const {
    return lines.empty() && circles.empty() && arcs.empty() && ellipses.empty() &&
           (polyOffsets.size() <= 1) && annotations.empty();
  }
};


/// Geometry-only snapshot for undo/redo.  PDF glTexId is zeroed to avoid stale GPU references.
struct DrawingGeometrySnapshot {
  std::vector<float>            userLinesFlat;
  std::vector<EntityAttributes> userLineAttrs;
  std::vector<float>            userCirclesCxCyR;
  std::vector<EntityAttributes> userCircleAttrs;
  std::vector<CadArc>           userArcs;
  std::vector<EntityAttributes> userArcAttrs;
  std::vector<CadEllipse>       userEllipses;
  std::vector<EntityAttributes> userEllAttrs;
  std::vector<int>              userPolylineOffsets;
  std::vector<float>            userPolylineVerts;
  std::vector<uint8_t>          userPolylineClosed;
  std::vector<EntityAttributes> userPolylineAttrs;
  std::vector<CadAnnotation>    cadAnnotations;
  std::vector<EntityAttributes> cadAnnotationAttrs;
  std::vector<SurveyPoint>      surveyPoints;
  std::vector<CadLayerRow>      drawingLayerTable;
  std::vector<PdfAttachment>    pdfAttachments;
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
  double worldDocumentOriginX = 0.0;
  double worldDocumentOriginY = 0.0;

  std::vector<float>            userLinesFlat;
  std::vector<EntityAttributes> userLineAttrs;
  std::vector<float>            userCirclesCxCyR;
  std::vector<EntityAttributes> userCircleAttrs;
  std::vector<CadArc>           userArcs;
  std::vector<EntityAttributes> userArcAttrs;
  std::vector<CadEllipse>       userEllipses;
  std::vector<EntityAttributes> userEllAttrs;
  std::vector<int>              userPolylineOffsets;
  std::vector<float>            userPolylineVerts;
  std::vector<uint8_t>          userPolylineClosed;
  std::vector<EntityAttributes> userPolylineAttrs;
  std::vector<CadAnnotation>    cadAnnotations;
  std::vector<EntityAttributes> cadAnnotationAttrs;
  std::vector<SurveyPoint>      surveyPoints;
  std::vector<int>              selectedSurveyPointIndices;
  std::vector<CadLayerRow>      drawingLayerTable;
  std::vector<PdfAttachment>    pdfAttachments;
  std::vector<SelectedEntity>   selection;
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

struct AppCommandState {
  enum class Kind {
    None,
    Line,
    Circle,
    Polyline,
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
    /// PDF underlay attach — opens dialog, then optionally waits for viewport picks.
    PdfAttach,
    /// 2-D Helmert (similarity) transformation from user-picked control point pairs.
    Align,
    /// Clipboard paste — cursor-following preview; one viewport click places the pasted entities.
    Paste,
  } active = Kind::None;

  static const char* KindName(Kind k) {
    switch (k) {
    case Kind::Line:          return "LINE";
    case Kind::Circle:        return "CIRCLE";
    case Kind::Polyline:      return "POLYLINE";
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
  RightClickEditMode    rightClickEditMode      = RightClickEditMode::RepeatLastCommand;
  RightClickCommandMode rightClickCommandMode   = RightClickCommandMode::Enter;

  /// Plot scale: one plotted inch equals this many drawing units (e.g. 50 for 1 inch = 50 feet).
  float modelUnitsPerPlottedInch = 50.f;
  float defaultPlottedTextHeightInches = 0.125f;
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
  /// Label MTEXT: east offset of label **centerline** from point (plotted inches × MUP → world).
  float surveyLabelOffsetEastPlottedIn = 0.35f;
  /// Optional north shift of label vertical center from point (plotted inches × MUP).
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
  /// When true, viewport picks should use the snapped world point (OSNAP) instead of the sticky-blended cursor.
  bool viewportSnapPickValid = false;
  float viewportSnapPickWorldX = 0.f;
  float viewportSnapPickWorldY = 0.f;
  /// Command-line log cache for the selectable read-only multiline (rebuilt each frame from \ref log).
  std::vector<char> commandLogCacheBytes;
  size_t commandLogLastSizeForAutoscroll = 0;
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
  bool objectSnapEnabled = true;
  bool objectSnapEndpoint = true;
  bool objectSnapMidpoint = true;
  bool objectSnapCenter = true;
  bool objectSnapPerpendicular = true;
  bool objectSnapSurveyPoint = true;
  bool objectSnapGeometricCenter = true;
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
  int mtextRichEditorAnnIndex = -1;
  std::string mtextRichEditorBuf;
  bool mtextRichEditorFocusRequest = false;
  int mtextRichEditorCursor = 0;
  int mtextRichEditorSelStart = 0;
  int mtextRichEditorSelEnd = 0;
  bool mtextRichEditorTypingAllCaps = false;

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
  /// From UI — ortho constrains LINE segment picks / typed ortho distances toward cursor.
  bool orthoMode = false;
  /// Last drawing viewport cursor (world), updated each frame for LINE ortho distance entry.
  float uiCursorWorldX = 0.f;
  float uiCursorWorldY = 0.f;
  /// Drawing viewport pan/zoom (local coordinates; pan is view center in storage space).
  double viewportPanX = 0.;
  double viewportPanY = 0.;
  float viewportZoom = 1.f;

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

  /// Each circle: center X, center Y, radius (world units).
  std::vector<float> userCirclesCxCyR;
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
  /// POLYLINE command draft — XYZ vertices (two or more before commit).
  std::vector<float> polylineDraftVerts;
  /// Civil 3D / AutoCAD-style TRIM: cutting edges first, then trim clicks (Enter advances / finishes).
  /// Alternative: type \p L on command line, then two points define an infinite cutting line (rubber + preview).
  enum class TrimPhase {
    SelectCuttingEdges,
    CuttingLine_WaitP1,
    CuttingLine_WaitP2,
    SelectTrimTargets,
  } trimPhase = TrimPhase::SelectCuttingEdges;
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

  // --- Selection (idle box pick + move/copy/rotate) ---
  std::vector<SelectedEntity> selection;

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
  /// Current layer for new geometry (ribbon combo + command defaults).
  std::string currentLayer = "0";
  std::vector<CadLayerRow> drawingLayerTable;
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
  float viewportBgR = 0.1215686f;
  float viewportBgG = 0.1215686f;
  float viewportBgB = 0.1647059f;

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
  st.mtextRichEditorAnnIndex = -1;
  st.mtextRichEditorBuf.clear();
  st.mtextRichEditorFocusRequest = false;
  st.mtextRichEditorCursor = 0;
  st.mtextRichEditorSelStart = 0;
  st.mtextRichEditorSelEnd = 0;
  st.mtextRichEditorTypingAllCaps = false;
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
    if (idx < 0 || static_cast<size_t>(idx) * 3 + 2 >= st.userCirclesCxCyR.size())
      return;
    const size_t k = static_cast<size_t>(idx) * 3;
    st.userCirclesCxCyR[k] = st.entityGripOrigCx;
    st.userCirclesCxCyR[k + 1] = st.entityGripOrigCy;
    st.userCirclesCxCyR[k + 2] = st.entityGripOrigR;
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

/// If ortho: snaps dx/dy so segment from anchor is horizontal or vertical (CAD-style).
void ApplyOrthoConstrainFromAnchor(float anchorX, float anchorY, float* wx, float* wy, bool ortho);

/// Snap pick onto anchor + t*(ux,uy). Negative \p t allowed unless \p forwardOnly.
void ApplySegmentAngleLockToWorldPick(float anchorX, float anchorY, float lockUx, float lockUy, float* wx, float* wy,
                                      bool forwardOnly);

/// Unit axis (-U,+U,+X,+Y) from anchor toward (targetX, targetY). False if target coincides with anchor.
bool OrthoUnitTowardPoint(float anchorX, float anchorY, float targetX, float targetY, float* ux, float* uy);
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
void StartArcCommand(AppCommandState& st, std::vector<std::string>& log);
void StartEllipseCommand(AppCommandState& st, std::vector<std::string>& log);
void StartTextCommand(AppCommandState& st, std::vector<std::string>& log);
void StartMtextCommand(AppCommandState& st, std::vector<std::string>& log);
void OpenMtextRichEditorForPlacement(AppCommandState& st, std::vector<std::string>* log);
void OpenMtextRichEditorForAnnotation(AppCommandState& st, int annIndex, std::vector<std::string>* log);
void CommitMtextRichEditor(AppCommandState& st, std::vector<std::string>& log);
void CancelMtextRichEditor(AppCommandState& st, std::vector<std::string>* log);
void StartDimAlignedCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDimLinearCommand(AppCommandState& st, std::vector<std::string>& log);
void StartDimAngularCommand(AppCommandState& st, std::vector<std::string>& log);
void StartIdPointCommand(AppCommandState& st, std::vector<std::string>& log);
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
bool PickClosestCadEntity(const AppCommandState& st, double wx, double wy, float tolWorld, SelectedEntity* out,
                          float* outDistSq);
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
/// Applies pending zoom-extents or zoom-window requests using current framebuffer size.
void ProcessPendingViewportZoom(AppCommandState& st, double* panX, double* panY, float* zoom, int fbW, int fbH,
                                float viewportAspect, std::vector<std::string>& log);


/// Clears window-selection draft state and CAD entity selection only (not survey point pick).
void ClearCadSelection(AppCommandState& st);
/// Replace selection with all entities of the same kind as the first selected item (or all survey points).
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
void SubmitViewportPick(AppCommandState& st, float worldX, float worldY, std::vector<std::string>& log,
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
