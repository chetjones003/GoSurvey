#pragma once



#include "SurveyPoints.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>



struct SelectedEntity {

  enum class Type { LineSeg = 0, Circle = 1, Annotation = 2, Polyline = 3, Arc = 4, Ellipse = 5 };

  Type type = Type::LineSeg;

  int index = 0; ///< Entity index in the parallel container for \p type

};



/// Display / CAD metadata stored per entity (parallel index to line segments or circles).

struct EntityAttributes {

  std::string layer = "0";

  std::string color = "ByLayer";

  std::string linetype = "Continuous";

  float lineweightMm = 0.18f;

  float transparency = 0.f; ///< 0 = opaque, 1 = fully transparent

};



/// Resolve stored color string + transparency to RGBA for viewport/UI (0..1). \p defaultRgb used for ByLayer.

void ResolveStoredColorForViewport(const std::string& colorStorage, float transparency, float defaultR,

                                   float defaultG, float defaultB, float* outRgba);



inline void ResolveEntityColorForViewport(const EntityAttributes& attr, float defaultR, float defaultG,

                                          float defaultB, float* outRgba) {

  ResolveStoredColorForViewport(attr.color, attr.transparency, defaultR, defaultG, defaultB, outRgba);

}



/// Single-line TEXT, MTEXT box, or aligned linear dimension drawn over the viewport (world coordinates).

struct CadAnnotation {

  enum class Kind { Text = 0, Mtext = 1, DimAligned = 2 };

  Kind kind = Kind::Text;

  float insX = 0.f;

  float insY = 0.f;

  /// Text height in plotted inches (constant on sheet); model height = this × drawing scale.

  float plottedHeightInches = 0.125f;

  /// CCW from +X (math \c atan2); UI shows clockwise-from-north via \c BearingCwNorthDegFromMathAngleRad.

  float rotationRad = 0.f;

  std::string text;

  float boxMinX = 0.f, boxMinY = 0.f, boxMaxX = 0.f, boxMaxY = 0.f;

  /// \c Kind::DimAligned — extension definition points (on measured geometry).

  float dimExt1X = 0.f, dimExt1Y = 0.f, dimExt2X = 0.f, dimExt2Y = 0.f;

  /// Signed perpendicular distance from chord midpoint to dimension line along N = (-T.y, T.x), T = normalize(e2-e1).

  float dimSignedOffset = 0.f;

};



/// Fills dimension-line feet, unit tangent \p T along measurement, left normal \p N, and chord length. False if degenerate.

bool CadDimAlignedGeometry(const CadAnnotation& a, float* sx1, float* sy1, float* sx2, float* sy2, float* tx,

                            float* ty, float* nx, float* ny, float* measLen);



/// Live DIMALIGNED preview after extension points are set (\p st.dimPhase == WaitDimLinePt).

bool CadDimAlignedBuildDraft(const AppCommandState& st, float cursorWx, float cursorWy, CadAnnotation* out);



/// After editing extension points or dimension offset, restore text from fixed (normal, tangent) offsets vs dim mid.

void CadDimAlignedApplyInsFromLocalOffset(CadAnnotation* ann, float alongN, float alongT);





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



/// MOVE/COPY destination drag or ROTATE angle preview — ghost annotations for ImGui overlay.

/// New CAD entities: mirror this pattern — GL rubber (\p main.cpp) + \ref CadAnnotationCollectTransformPreviews / UI overlay (\p CadUi.cpp) + grips (\p CadUi.cpp).

void CadAnnotationCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,

                                           std::vector<CadAnnotation>* out);



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

    Move,

    Copy,

    Rotate,

    Delete,

    Zoom,

    Join,

    Trim

  } active = Kind::None;



  /// Plot scale: one plotted inch equals this many drawing units (e.g. 50 for 1 inch = 50 feet).

  float modelUnitsPerPlottedInch = 50.f;

  float defaultPlottedTextHeightInches = 0.125f;



  /// World XY added on DXF export when CAD was shifted to a local origin for rendering precision (large imports).

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



  enum class DimPhase { WaitExt1, WaitExt2, WaitDimLinePt } dimPhase = DimPhase::WaitExt1;



  float dimE1x = 0.f, dimE1y = 0.f;

  float dimE2x = 0.f, dimE2y = 0.f;



  float anchorX = 0.f;

  float anchorY = 0.f;

  /// From UI — ortho constrains LINE segment picks / typed ortho distances toward cursor.

  bool orthoMode = false;

  /// Last drawing viewport cursor (world), updated each frame for LINE ortho distance entry.

  float uiCursorWorldX = 0.f;

  float uiCursorWorldY = 0.f;



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



  /// MTEXT corner grip drag (viewport): fixed diagonal corner while resizing box.

  int mtextGripAnnotationIndex = -1;

  int mtextGripCorner = -1;

  float mtextGripFixedCornerX = 0.f;

  float mtextGripFixedCornerY = 0.f;



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



  // --- MOVE / COPY ---

  enum class ModifyPhase { PickSelection, NeedBase, NeedDestination } modifyPhase = ModifyPhase::PickSelection;



  float modifyBaseX = 0.f;

  float modifyBaseY = 0.f;



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

  bool showViewPointsWindow = false;

  bool showSettingsWindow = false;

  /// Viewport CAD crosshair (Drawing1): RGB 0–1, arm length as fraction of viewport width/height, pickbox half-size in px.
  float viewportCrosshairR = 1.f;
  float viewportCrosshairG = 0.8392157f;
  float viewportCrosshairB = 0.f;
  float viewportCrosshairArmFracX = 0.03f;
  float viewportCrosshairArmFracY = 0.05f;
  float viewportCrosshairPickHalfPxX = 4.f;
  float viewportCrosshairPickHalfPxY = 4.f;
  float viewportCrosshairHairPx = 1.f;

  bool createPointsPlacementActive = false;

  /// Editable ID strings for VIEWPOINTS table rows (synced from point IDs when empty).
  std::vector<std::string> surveyPointIdBuffers;

  bool showImportPointsWindow = false;

  bool showExportPointsWindow = false;

  char surveyImportCsvPath[512]{};

  char surveyExportCsvPath[512]{};

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

};



inline float DefaultAnnotationTextHeightWorld(const AppCommandState& st) {

  return st.defaultPlottedTextHeightInches * st.modelUnitsPerPlottedInch;

}



inline void BumpCadGpuCache(AppCommandState& st) { ++st.cadGpuRevision; }



inline void ClearMtextGripInteraction(AppCommandState& st) {

  st.mtextGripAnnotationIndex = -1;

  st.mtextGripCorner = -1;

}



inline void ClearDimGripInteraction(AppCommandState& st) {

  st.dimGripAnnotationIndex = -1;

  st.dimGripWhich = -1;

  st.dimGripMoveActive = false;

  st.dimGripTextAlongN = 0.f;

  st.dimGripTextAlongT = 0.f;

}



inline void ResetSegmentAngleLock(AppCommandState& st) {

  st.segmentAngleLockActive = false;

  st.segmentLockUx = 1.f;

  st.segmentLockUy = 0.f;

  st.segmentAnglePickPhase = AppCommandState::SegmentAnglePickPhase::Idle;

}



/// Abort bearing-from-two-points flow (\p AP) without ending LINE/POLYLINE.

void CancelSegmentAnglePick(AppCommandState& st, std::vector<std::string>* log);



/// LINE / POLYLINE (next point): \p A / \p AP / optional \p +delta on same line (° clockwise from north).

bool TryParseSegmentAngleLockCommand(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log);



/// Trim and parse absolute "x,y" / "x y" or relative "@dx,dy" when allowed.

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



void StartLineCommand(AppCommandState& st, std::vector<std::string>& log);

void StartCircleCommand(AppCommandState& st, std::vector<std::string>& log);

void StartPolylineCommand(AppCommandState& st, std::vector<std::string>& log);

void StartArcCommand(AppCommandState& st, std::vector<std::string>& log);

void StartEllipseCommand(AppCommandState& st, std::vector<std::string>& log);

void StartTextCommand(AppCommandState& st, std::vector<std::string>& log);

void StartMtextCommand(AppCommandState& st, std::vector<std::string>& log);

void StartDimAlignedCommand(AppCommandState& st, std::vector<std::string>& log);

void StartMoveCommand(AppCommandState& st, std::vector<std::string>& log);

void StartCopyCommand(AppCommandState& st, std::vector<std::string>& log);

void StartRotateCommand(AppCommandState& st, std::vector<std::string>& log);

void StartDeleteCommand(AppCommandState& st, std::vector<std::string>& log);

void StartJoinCommand(AppCommandState& st, std::vector<std::string>& log);

void StartTrimCommand(AppCommandState& st, std::vector<std::string>& log);

/// Removes selected entities from the drawing and clears selection. No-op if selection empty.

void ExecuteDeleteSelection(AppCommandState& st, std::vector<std::string>& log);

/// Join selected lines / polylines at coincident endpoints into polylines (window-select like DELETE).

void ExecuteJoinSelection(AppCommandState& st, std::vector<std::string>& log);

/// TRIM — pick cutting edges, Enter, trim clicks; or \p L then two points: draws the segment to trim (nearest edge),
/// trims once at nearest crossing (fence disambiguates), then TRIM ends.
bool SubmitTrimViewportPick(AppCommandState& st, float wx, float wy, float tolWorld, std::vector<std::string>& log);

/// Preview for TRIM \p L rubber phase; pass the drawn segment midpoint as \p pickPreview (same side rule as commit).
void CadTrimAppendCutLineRemovedPreview(const AppCommandState& st, float fenceP1x, float fenceP1y, float fenceP2x,
                                        float fenceP2y, float pickPreviewX, float pickPreviewY,
                                        std::vector<float>* previewLinesOut);

/// Closest CAD entity within tolerance (later draw order wins on tie). False if none.

bool PickClosestCadEntity(const AppCommandState& st, float wx, float wy, float tolWorld, SelectedEntity* out,
                          float* outDistSq);

void StartZoomExtentsCommand(AppCommandState& st, std::vector<std::string>& log);

void StartZoomWindowCommand(AppCommandState& st, std::vector<std::string>& log);

/// Applies pending zoom-extents or zoom-window requests using current framebuffer size.

void ProcessPendingViewportZoom(AppCommandState& st, float* panX, float* panY, float* zoom, int fbW, int fbH,

                                std::vector<std::string>& log);



/// Clears window-selection draft state and CAD entity selection only (not survey point pick).
void ClearCadSelection(AppCommandState& st);

/// Removes all committed CAD lines/circles and clears CAD selection (survey points unchanged).
void ClearCadGeometry(AppCommandState& st);

/// Ends active LINE/CIRCLE/MOVE/etc. draft without logging — used after DXF import.
void ResetCadToolStateToIdle(AppCommandState& st);

void ClearSelection(AppCommandState& st);

/// Toggle survey point in multi-selection (additive unless \p shiftSubtract removes).
void ApplySurveyPointClickSelection(AppCommandState& st, int surveyPointIndex, bool shiftSubtract,
                                    std::vector<std::string>* log);

void BeginSelectionBoxCorner(AppCommandState& st, float wx, float wy, float anchorScreenX, float anchorScreenY);



void CancelActiveCommand(AppCommandState& st, std::vector<std::string>& log);

/// Called from UI when COPY survey duplicate-ID modal closes (\p applySurveyDup runs duplication).
void ApplyCopySurveyDuplicateModalResult(AppCommandState& st, bool applySurveyDup, std::vector<std::string>& log);



bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);



/// Viewport left-click during active commands.

void SubmitViewportPick(AppCommandState& st, float worldX, float worldY, std::vector<std::string>& log,

                        bool windowSelectionSubtract = false, bool fenceLeftToRightWindowMode = false);



void ProcessCommandLineSubmit(char* cmdBuf, int cmdBufSize, AppCommandState& st, std::vector<std::string>& log);



std::vector<std::string> FuzzyCommandMatches(const std::string& query, int maxResults);



const char* CircleCommandFooterHint(const AppCommandState& st);

const char* ModifyCommandFooterHint(const AppCommandState& st);

const char* RotateCommandFooterHint(const AppCommandState& st);

const char* DeleteCommandFooterHint(const AppCommandState& st);

const char* JoinCommandFooterHint(const AppCommandState& st);

const char* TrimCommandFooterHint(const AppCommandState& st);

const char* ZoomCommandFooterHint(const AppCommandState& st);

const char* LineCommandFooterHint(const AppCommandState& st);

const char* DrawingExtrasFooterHint(const AppCommandState& st);

bool ComputeWorldExtents(const AppCommandState& st, float* outMnX, float* outMxX, float* outMnY, float* outMxY);

void ApplyViewportZoomToWorldRect(float mnX, float mxX, float mnY, float mxY, float* panX, float* panY,

                                  float* zoom, int fbW, int fbH);



bool ComputeCircumcircle(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,

                         float* r);



bool LoadApplicationFont();


