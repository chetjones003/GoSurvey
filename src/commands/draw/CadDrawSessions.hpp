#pragma once
// ADR-0012 / Issue #1578 (Track B Batch 2): Active-command sessions for the Draw,
// Annotation, Inquiry, and DrawInquiry command families.

#include "engine/CommandSession.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace stepengine {

enum class LinePhase : std::uint8_t { NeedFirstPoint, NeedNextPoint };
enum class PolylinePhase : std::uint8_t { NeedFirstPoint, NeedNextPoint };
enum class SegmentAnglePickPhase : std::uint8_t { Idle, WaitP1, WaitP2, WaitAdjustOrCommit };
enum class CircleStyle : std::uint8_t { CenterRadius, ThreePoint };
enum class CirclePhase : std::uint8_t {
  WaitCenterOrMode,
  WaitRadius,
  ThreeP_WaitP1,
  ThreeP_WaitP2,
  ThreeP_WaitP3,
};
enum class ArcPhase : std::uint8_t { WaitStart, WaitMid, WaitEnd };
enum class EllipsePhase : std::uint8_t { WaitCenter, WaitMajorEnd, WaitRatio };
enum class RectPhase : std::uint8_t { WaitFirstCorner, WaitSecondCorner };
enum class SplineMethod : std::uint8_t { Fit, Control };
enum class TextCmdPhase : std::uint8_t { WaitInsertion, WaitHeight, WaitRotation, WaitString };
enum class MtextPhase : std::uint8_t { WaitCorner1, WaitCorner2, WaitString };
enum class MleaderPhase : std::uint8_t { WaitArrowTip, WaitLanding, WaitLabel };
enum class DimPhase : std::uint8_t { WaitExt1, WaitExt2, WaitDimLinePt };
enum class DimAngularPhase : std::uint8_t { WaitVertex, WaitRay1, WaitRay2, WaitArc };
enum class SurveyInversePhase : std::uint8_t { WaitFrom, WaitTo };
enum class DistPhase : std::uint8_t { WaitFrom, WaitTo };
enum class LeaderAnnotation : std::uint8_t { Mtext, None };
enum class LeaderPathStyle : std::uint8_t { Straight, Spline };
enum class LeaderPhase : std::uint8_t { WaitFirst, WaitNext, WaitAnnotation, WaitFormat };
enum class PeditPhase : std::uint8_t {
  WaitObject,
  WaitOption,
  WaitVertex,
  WaitJoin,
  WaitVertexMove,
  WaitVertexInsert,
};
enum class MeasurePhase : std::uint8_t { WaitObject, WaitValue };
enum class AreaPhase : std::uint8_t { WaitFirst, WaitNext, WaitObject };

/// Shared bearing-lock / 2-point bearing-pick state owned by LineSession and PolylineSession.
struct SegmentAngleState {
  SegmentAnglePickPhase pickPhase = SegmentAnglePickPhase::Idle;
  float pickRefX1 = 0.f;
  float pickRefY1 = 0.f;
  float pickDraftBearingDeg = 0.f;
  bool lockActive = false;
  float lockUx = 1.f;
  float lockUy = 0.f;
  bool keyboardAwaitBearing = false;

  void reset() {
    lockActive = false;
    lockUx = 1.f;
    lockUy = 0.f;
    pickPhase = SegmentAnglePickPhase::Idle;
    keyboardAwaitBearing = false;
  }
};

class LineSession final : public CommandSession {
 public:
  LinePhase phase = LinePhase::NeedFirstPoint;
  std::uint32_t draftSegments = 0;
  SegmentAngleState angle;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Line; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class PolylineSession final : public CommandSession {
 public:
  explicit PolylineSession(bool is3d = false) : draft3d(is3d) {}

  PolylinePhase phase = PolylinePhase::NeedFirstPoint;
  bool draft3d = false;
  float firstX = 0.f;
  float firstY = 0.f;
  std::uint32_t draftSegments = 0;
  std::vector<float> draftVerts;
  std::vector<float> draftBulge;
  bool arcMode = false;
  float arcRadius = 0.f;
  float arcAngleDeg = 0.f;
  bool arcAngleValid = false;
  SegmentAngleState angle;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Polyline; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;
};

class CircleSession final : public CommandSession {
 public:
  CircleStyle style = CircleStyle::CenterRadius;
  CirclePhase phase = CirclePhase::WaitCenterOrMode;
  float cx = 0.f, cy = 0.f, cz = 0.f;
  float c3p1x = 0.f, c3p1y = 0.f, c3p1z = 0.f;
  float c3p2x = 0.f, c3p2y = 0.f, c3p2z = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Circle; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse input for current CIRCLE step — see hint below.";
  }
};

class ArcSession final : public CommandSession {
 public:
  ArcPhase phase = ArcPhase::WaitStart;
  float ax = 0.f, ay = 0.f, az = 0.f;
  float bx = 0.f, by = 0.f, bz = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Arc; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class EllipseSession final : public CommandSession {
 public:
  EllipsePhase phase = EllipsePhase::WaitCenter;
  float cx = 0.f, cy = 0.f, cz = 0.f;
  float majEx = 0.f, majEy = 0.f, majEz = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Ellipse; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class RectSession final : public CommandSession {
 public:
  RectPhase phase = RectPhase::WaitFirstCorner;
  float x1 = 0.f, y1 = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Rect; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class SplineSession final : public CommandSession {
 public:
  SplineMethod method = SplineMethod::Fit;
  std::vector<spline::Vec3> draftPoints;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Spline; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class HatchSession final : public CommandSession {
 public:
  /// Boundary loop traced under the cursor while HATCH is active (flat local x,y); valid drives the preview.
  std::vector<float> previewLoop;
  bool previewValid = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Hatch; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class TextSession final : public CommandSession {
 public:
  explicit TextSession(float defaultHeight = 1.f) : heightDraft(defaultHeight) {}

  TextCmdPhase phase = TextCmdPhase::WaitInsertion;
  float insX = 0.f, insY = 0.f;
  float heightDraft = 1.f;
  float rotDraft = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Text; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class MtextSession final : public CommandSession {
 public:
  MtextPhase phase = MtextPhase::WaitCorner1;
  float x1 = 0.f, y1 = 0.f;
  float x2 = 0.f, y2 = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Mtext; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;
};

class MleaderSession final : public CommandSession {
 public:
  MleaderPhase phase = MleaderPhase::WaitArrowTip;
  float tipX = 0.f, tipY = 0.f, tipZ = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Mleader; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;
};

class MleaderAddLeaderSession final : public CommandSession {
 public:
  explicit MleaderAddLeaderSession(int idx = -1) : editIndex(idx) {}

  int editIndex = -1;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::MleaderAddLeader; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class DimSession final : public CommandSession {
 public:
  explicit DimSession(bool isLinear = false) : linear(isLinear) {}

  bool linear = false;
  DimPhase phase = DimPhase::WaitExt1;
  // Double (issue #1668): the measured points; a float near 5,000 ft is ~0.0005 ft coarse.
  double e1x = 0.0, e1y = 0.0;
  double e2x = 0.0, e2y = 0.0;
  bool linearDraftVertical = false;
  bool linearOrientUserLock = false;
  float lockCursorWx = 0.f, lockCursorWy = 0.f;

  void resetDraft() {
    phase = DimPhase::WaitExt1;
    e1x = e1y = e2x = e2y = 0.0;
    linearDraftVertical = false;
    linearOrientUserLock = false;
    lockCursorWx = lockCursorWy = 0.f;
  }

  [[nodiscard]] CadCommandKind kind() const override {
    return linear ? CadCommandKind::DimLinear : CadCommandKind::DimAligned;
  }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class DimAngularSession final : public CommandSession {
 public:
  DimAngularPhase phase = DimAngularPhase::WaitVertex;
  // Double (issue #1668): in float these picks put a minute of arc of error into the angle.
  double vx = 0.0, vy = 0.0;
  double e1x = 0.0, e1y = 0.0;
  double e2x = 0.0, e2y = 0.0;

  void resetDraft() {
    phase = DimAngularPhase::WaitVertex;
    vx = vy = 0.0;
    e1x = e1y = e2x = e2y = 0.0;
  }

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::DimAngular; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class IdPointSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::IdPoint; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class SurveyInverseSession final : public CommandSession {
 public:
  SurveyInversePhase phase = SurveyInversePhase::WaitFrom;
  float fromX = 0.f, fromY = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::SurveyInverse; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class DistSession final : public CommandSession {
 public:
  DistPhase phase = DistPhase::WaitFrom;
  float fromX = 0.f, fromY = 0.f, fromZ = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Dist; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class PointSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Point; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class LeaderSession final : public CommandSession {
 public:
  LeaderAnnotation annotation = LeaderAnnotation::Mtext;
  LeaderPathStyle pathStyle = LeaderPathStyle::Straight;
  bool showArrow = true;
  LeaderPhase phase = LeaderPhase::WaitFirst;
  bool finishAfterAnnotation = false;
  std::vector<spline::Vec3> draftPoints;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Leader; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class PeditSession final : public CommandSession {
 public:
  PeditPhase phase = PeditPhase::WaitObject;
  int index = -1;
  int vertex = 0;
  bool paper = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Pedit; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class MeasureDivideSession final : public CommandSession {
 public:
  explicit MeasureDivideSession(bool isDivide = false) : divide(isDivide) {}

  bool divide = false;
  MeasurePhase phase = MeasurePhase::WaitObject;
  SelectedEntity target{};
  bool paper = false;
  PaperEntityRef paperTarget{PaperEntityRef::Type::Line, -1};
  double pickX = 0.0;
  double pickY = 0.0;

  [[nodiscard]] CadCommandKind kind() const override {
    return divide ? CadCommandKind::Divide : CadCommandKind::Measure;
  }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class AreaSession final : public CommandSession {
 public:
  AreaPhase phase = AreaPhase::WaitFirst;
  std::vector<spline::Vec3> draftPoints;
  double running = 0.0;
  bool subtract = false;
  bool accumulate = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Area; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class ListSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::List; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class DataExtractSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::DataExtract; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class TableSession final : public CommandSession {
 public:
  int stage = 0;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Table; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class PointTableSession final : public CommandSession {
 public:
  int stage = 0;
  std::string style;
  std::vector<int> ids;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::PointTable; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

inline SegmentAngleState* ActiveSegmentAngleState(AppCommandState& st) {
  if (auto* ln = st.sessionAs<LineSession>())
    return &ln->angle;
  if (auto* pl = st.sessionAs<PolylineSession>())
    return &pl->angle;
  return nullptr;
}

inline const SegmentAngleState* ActiveSegmentAngleState(const AppCommandState& st) {
  if (const auto* ln = st.sessionAs<LineSession>())
    return &ln->angle;
  if (const auto* pl = st.sessionAs<PolylineSession>())
    return &pl->angle;
  return nullptr;
}

} // namespace stepengine
