#pragma once
// ADR-0012 / Issue #1578: Shared step engine contracts and active-command session base.
// Every interactive command owns a CommandSession subclass that holds its own phase and
// scratch state; the session is dropped automatically on finish or Esc.

#include <GoSurvey/Math/ray3d.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct AppCommandState;

/// Canonical command kind identifier (aliased as AppCommandState::Kind for source compatibility).
enum class CadCommandKind : std::uint8_t {
  None,
  Line,
  Circle,
  Polyline,
  FeatureLine,
  Grading,
  Arc,
  Ellipse,
  Text,
  Mtext,
  Mleader,
  MleaderAddLeader,
  DimAligned,
  DimLinear,
  DimAngular,
  Move,
  Copy,
  Rotate,
  Scale,
  Array,
  Mirror,
  Lengthen,
  Extend,
  Break,
  Stretch,
  Fillet,
  Chamfer,
  Delete,
  Zoom,
  Join,
  Trim,
  Offset,
  IdPoint,
  SurveyInverse,
  Dist,
  SurfaceElevGrade,
  WaterDrop,
  Catchment,
  SwapTinEdge,
  AddTinPoint,
  DelTinPoint,
  MoveTinPoint,
  DelTinLine,
  QuickProfile,
  DesignateBreakline,
  DesignateBoundary,
  PdfAttach,
  Align,
  Paste,
  PaperRectViewport,
  Hatch,
  Pan,
  VpFreeze,
  VpThaw,
  Rect,
  TrimState,
  SectionClip,
  SectionPlane,
  Elev,
  Orbit,
  Ucs,
  Plan,
  InsertBlock,
  Solid,
  Extrude,
  Revolve,
  Slice,
  Section,
  Loft,
  Sweep,
  Boolean,
  Polysolid,
  Revcloud,
  PressPull,
  BConnectMode,
  BConnect,
  BConnectEdit,
  BlockFitting,
  PipeRun,
  PipeFit,
  PipeSplit,
  ExtractCenterline,
  Move3d,
  Rotate3d,
  Scale3d,
  ChProp,
  MatchProp,
  LayMCur,
  GeoMarkPoint,
  GeoMarkLatLong,
  GeoReorientMarker,
  DrawingSettingsPick,
  GeoCaptureArea,
  PadSolid,
  Spline,
  Point,
  Leader,
  Pedit,
  Measure,
  Divide,
  Area,
  List,
  DataExtract,
  Table,
  PointTable,
  AlignmentLayout,
  AlignmentFromObjects,
  StationOffsetPick,
};

namespace stepengine {

/// The seven standard prompt step kinds defined by ADR-0012, plus Custom for
/// specialized viewport-driven steps (gizmo handles, solid face pick, drag-only).
enum class StepKind : std::uint8_t {
  SelectObjects,
  Point,
  Distance,
  Angle,
  KeywordOptions,
  Text,
  EntityPick,
  Custom,
};

/// What a blank Enter does on the current step (ADR-0012).
/// One Enter is always one submit.
enum class BlankEnterPolicy : std::uint8_t {
  Finish,      ///< Completes the current step or ends the command.
  TakeDefault, ///< Applies the step's declared default value.
  RepeatLast,  ///< Repeats the last accepted value for this step.
  Custom,      ///< Handled directly by the session's onBlankEnter().
};

/// Click routing reported by the step engine to Interaction (1:1 mirror of ViewportClickRoute
/// without introducing a Commands -> Interaction dependency).
enum class StepClickRoute : std::uint8_t {
  Ignore,
  IdleSelection,
  SelectionBox,
  SelectionAccumulate,
  RawEntityPick,
  SnappedPointPick,
  TrimPick,
  HatchPick,
  PdfAttachInsertPoint,
  InsertBlockPick,
  InsertBlockAlignFacePick,
  BconnectFacePick,
  ExtractCenterlinePick,
  SubObjectFacePick,
  GizmoHandlePick,
};

/// Dynamic-input metadata declared by the active step.
struct StepDynInputSpec {
  bool pointEntry = false;
  bool ucsPolar = false;
  ray3d::Vec3 ucsPolarBase{};
  bool haveBase = false;
  /// When true, basePoint is already in true world coordinates; otherwise local storage coordinates.
  bool baseIsWorld = false;
  ray3d::Vec3 basePoint{};
  /// True to open in Distance < Angle relative-polar mode (e.g. LINE/POLYLINE/PIPERUN next point).
  bool initialRelativePolar = false;
  /// True when a bare number commits along the command's own direction (ORTHO or PIPERUN compass).
  bool directDistance = false;
};

/// Complete declarative description of the active command's current prompt step.
/// Drives click routing, selection-box raw-world mode, dynamic input, and prompt/footer text.
struct StepDescriptor {
  StepKind kind = StepKind::Custom;
  BlankEnterPolicy blankEnter = BlankEnterPolicy::Custom;
  StepClickRoute clickRoute = StepClickRoute::Ignore;
  bool rawWorldForSelectionRect = false;
  bool includeSurveyInSelectionBox = false;
  /// True only for ZOOM WINDOW's region fence (not an object-selection step per REQ-121).
  bool isZoomRegionBox = false;
  StepDynInputSpec dynInput{};
  std::string prompt;

  static StepDescriptor SelectObjects(std::string promptText,
                                      BlankEnterPolicy enter = BlankEnterPolicy::Finish,
                                      bool includeSurvey = false) {
    StepDescriptor d;
    d.kind = StepKind::SelectObjects;
    d.blankEnter = enter;
    d.clickRoute = StepClickRoute::SelectionAccumulate;
    d.rawWorldForSelectionRect = true;
    d.includeSurveyInSelectionBox = includeSurvey;
    d.prompt = std::move(promptText);
    return d;
  }

  static StepDescriptor SelectionBox(std::string promptText,
                                     BlankEnterPolicy enter = BlankEnterPolicy::Custom,
                                     bool includeSurvey = false,
                                     bool zoomRegion = false) {
    StepDescriptor d;
    d.kind = StepKind::SelectObjects;
    d.blankEnter = enter;
    d.clickRoute = StepClickRoute::SelectionBox;
    d.rawWorldForSelectionRect = true;
    d.includeSurveyInSelectionBox = includeSurvey;
    d.isZoomRegionBox = zoomRegion;
    d.prompt = std::move(promptText);
    return d;
  }

  static StepDescriptor Point(std::string promptText,
                              StepDynInputSpec dyn = {},
                              BlankEnterPolicy enter = BlankEnterPolicy::Custom) {
    StepDescriptor d;
    d.kind = StepKind::Point;
    d.blankEnter = enter;
    d.clickRoute = StepClickRoute::SnappedPointPick;
    dyn.pointEntry = true;
    d.dynInput = dyn;
    d.prompt = std::move(promptText);
    return d;
  }

  static StepDescriptor Distance(std::string promptText,
                                 BlankEnterPolicy enter = BlankEnterPolicy::TakeDefault,
                                 StepClickRoute route = StepClickRoute::Ignore,
                                 StepDynInputSpec dyn = {}) {
    StepDescriptor d;
    d.kind = StepKind::Distance;
    d.blankEnter = enter;
    d.clickRoute = route;
    d.dynInput = dyn;
    d.prompt = std::move(promptText);
    return d;
  }

  static StepDescriptor Angle(std::string promptText,
                              BlankEnterPolicy enter = BlankEnterPolicy::TakeDefault,
                              StepClickRoute route = StepClickRoute::Ignore,
                              StepDynInputSpec dyn = {}) {
    StepDescriptor d;
    d.kind = StepKind::Angle;
    d.blankEnter = enter;
    d.clickRoute = route;
    d.dynInput = dyn;
    d.prompt = std::move(promptText);
    return d;
  }

  static StepDescriptor KeywordOptions(std::string promptText,
                                       BlankEnterPolicy enter = BlankEnterPolicy::TakeDefault,
                                       StepClickRoute route = StepClickRoute::Ignore) {
    StepDescriptor d;
    d.kind = StepKind::KeywordOptions;
    d.blankEnter = enter;
    d.clickRoute = route;
    d.prompt = std::move(promptText);
    return d;
  }

  static StepDescriptor Text(std::string promptText,
                             BlankEnterPolicy enter = BlankEnterPolicy::TakeDefault) {
    StepDescriptor d;
    d.kind = StepKind::Text;
    d.blankEnter = enter;
    d.clickRoute = StepClickRoute::Ignore;
    d.prompt = std::move(promptText);
    return d;
  }

  static StepDescriptor EntityPick(std::string promptText,
                                   BlankEnterPolicy enter = BlankEnterPolicy::Finish) {
    StepDescriptor d;
    d.kind = StepKind::EntityPick;
    d.blankEnter = enter;
    d.clickRoute = StepClickRoute::RawEntityPick;
    d.prompt = std::move(promptText);
    return d;
  }

  static StepDescriptor Custom(std::string promptText,
                               StepClickRoute route,
                               BlankEnterPolicy enter = BlankEnterPolicy::Custom,
                               bool rawWorldSelRect = false,
                               bool includeSurvey = false,
                               StepDynInputSpec dyn = {}) {
    StepDescriptor d;
    d.kind = StepKind::Custom;
    d.blankEnter = enter;
    d.clickRoute = route;
    d.rawWorldForSelectionRect = rawWorldSelRect;
    d.includeSurveyInSelectionBox = includeSurvey;
    d.dynInput = dyn;
    d.prompt = std::move(promptText);
    return d;
  }
};

/// Viewport pick event handed to the active session.
struct StepPickInput {
  double localX = 0.0;
  double localY = 0.0;
  bool windowSelectionSubtract = false;
  bool fenceLeftToRightWindowMode = false;
  const ray3d::Ray* pickRay = nullptr;
};

/// Result of submitting one command-line entry (one Enter = one submit).
enum class StepSubmitResult : std::uint8_t {
  Handled,  ///< Consumed; session stays active (unless it ended itself).
  Finished, ///< Command completed normally; engine drops the session.
  Unparsed, ///< Rejected as unparseable; engine runs ReportUnparsedCommandInput.
};

/// Base class for every active command session.
/// Holds the command's own state struct (phase + transient scratch fields) and is
/// destroyed automatically on normal completion, Esc, or tool reset.
struct CommandSession {
  virtual ~CommandSession() = default;

  [[nodiscard]] virtual CadCommandKind kind() const = 0;
  [[nodiscard]] virtual StepDescriptor currentStep(const AppCommandState& st) const = 0;

  virtual StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                                  std::vector<std::string>& log) = 0;
  virtual StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log);
  virtual void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                              std::vector<std::string>& log);
  virtual void onCancel(AppCommandState& st, std::vector<std::string>& log) = 0;
  /// Silent teardown called on tool reset / drawing clear / command replacement (no log output).
  virtual void onReset(AppCommandState& st) { (void)st; }
  /// Fallback message for ReportUnparsedCommandInput when onText returns Unparsed without logging.
  [[nodiscard]] virtual const char* unparsedFallback() const { return nullptr; }
};

/// One keyword choice in a KeywordOptions step.
struct KeywordOption {
  std::string canonical;            ///< Canonical value delivered to callback (e.g. "Yes").
  std::vector<std::string> tokens;  ///< Accepted tokens, case-insensitive (e.g. {"y", "yes"}).
};

/// Typed value parsed and delivered by ScriptedStepSession for any of the 7 standard StepKinds.
struct StepValue {
  StepKind kind = StepKind::Text;
  ray3d::Vec3 point{};      ///< Point (storage local X/Y, world Z) or EntityPick location.
  double scalar = 0.0;      ///< Distance or Angle (degrees CW from north / parsed scalar).
  double angleRad = 0.0;    ///< Angle in radians (for StepKind::Angle).
  std::string text;         ///< Keyword canonical name or Text string.
  bool fromBlankEnter = false;
};

/// Declarative definition of one step in a ScriptedStepSession.
struct ScriptedStepSpec {
  StepKind kind = StepKind::Text;
  BlankEnterPolicy blankEnter = BlankEnterPolicy::Finish;
  std::string prompt;
  std::string verbUpper = "COMMAND";

  // SelectObjects options
  bool requireNonEmptySelection = true;
  bool includeSurveyInSelectionBox = false;
  bool boxOnlySelection = false;

  // Point options
  bool allowRelativePoint = false;
  bool hasPointBase = false;
  ray3d::Vec3 pointBaseLocal{};
  std::optional<ray3d::Vec3> defaultPoint;

  // Distance options
  bool allowZeroDistance = false;
  bool allowNegativeDistance = false;
  bool allowPointPickForDistance = false;
  std::optional<double> defaultDistance;
  double* lastDistanceSlot = nullptr; ///< Optional external memory for BlankEnterPolicy::RepeatLast.

  // Angle options
  bool allowPointPickForAngle = false;
  std::optional<double> defaultAngleDeg;
  double* lastAngleDegSlot = nullptr; ///< Optional external memory for BlankEnterPolicy::RepeatLast.

  // KeywordOptions options
  std::vector<KeywordOption> keywords;
  std::string defaultKeyword;
  std::string* lastKeywordSlot = nullptr;

  // Text options
  std::string defaultText;
  std::string* lastTextSlot = nullptr;

  // Callbacks
  /// Return true to accept and advance to the next step; false to stay on this step.
  std::function<bool(AppCommandState&, const StepValue&, std::vector<std::string>&)> onAccept;
};

/// Declarative multi-step CommandSession supporting all 7 ADR-0012 step kinds and all
/// BlankEnterPolicy modes. Used directly by scripted step tests and simple multi-step commands.
class ScriptedStepSession final : public CommandSession {
 public:
  ScriptedStepSession(CadCommandKind k, std::string cancelMessage,
                      std::vector<ScriptedStepSpec> steps,
                      std::function<void(AppCommandState&, std::vector<std::string>&)> onComplete = {},
                      std::function<void(AppCommandState&, std::vector<std::string>&)> onCancelFn = {});

  [[nodiscard]] CadCommandKind kind() const override { return kind_; }
  [[nodiscard]] std::size_t currentIndex() const { return index_; }
  [[nodiscard]] bool completed() const { return completed_; }
  [[nodiscard]] bool cancelled() const { return cancelled_; }
  [[nodiscard]] const std::vector<StepValue>& acceptedValues() const { return accepted_; }

  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override { return "Could not parse input."; }

 private:
  StepSubmitResult AcceptAndAdvance(AppCommandState& st, StepValue value,
                                    std::vector<std::string>& log);

  CadCommandKind kind_ = CadCommandKind::None;
  std::string cancelMessage_;
  std::vector<ScriptedStepSpec> steps_;
  std::function<void(AppCommandState&, std::vector<std::string>&)> onComplete_;
  std::function<void(AppCommandState&, std::vector<std::string>&)> onCancelFn_;
  std::size_t index_ = 0;
  bool completed_ = false;
  bool cancelled_ = false;
  std::vector<StepValue> accepted_;
  std::optional<StepValue> lastAccepted_;
};

} // namespace stepengine
