#pragma once
// ADR-0012 / Issue #1578 (Track B Batch 1): Active-command sessions for the remaining
// Modify family (ARRAY, LENGTHEN, EXTEND, BREAK, FILLET, CHAMFER, TRIM, TRIMSTATE,
// OFFSET, DELETE, JOIN) and ALIGN.

#include "engine/CommandSession.hpp"

#include <cstdint>
#include <vector>

namespace stepengine {

enum class ArrayType : std::uint8_t { Rectangular, Polar };

enum class ArrayPhase : std::uint8_t {
  PickSelection,
  WaitType,
  Rect_WaitColumns,
  Rect_WaitColumnSpacing,
  Rect_WaitRows,
  Rect_WaitRowSpacing,
  Rect_WaitLevels,
  Rect_WaitLevelSpacing,
  Polar_WaitCenter,
  Polar_WaitItemCount,
  Polar_WaitAngle,
  Polar_WaitRotateAnswer,
};

enum class LengthenPhase : std::uint8_t {
  WaitSelectOrMode,
  WaitDeltaValue,
  WaitPercentValue,
  WaitTotalValue,
  WaitDynamicTarget,
};

enum class ExtendPhase : std::uint8_t {
  SelectBoundaries,
  SelectTargets,
};

enum class BreakPhase : std::uint8_t {
  SelectFirstPoint,
  SelectSecondPoint,
};

enum class FilletPhase : std::uint8_t {
  WaitFirstEntity,
  WaitSecondEntity,
};

enum class ChamferPhase : std::uint8_t {
  WaitFirstEntity,
  WaitSecondEntity,
};

enum class TrimPhase : std::uint8_t {
  SelectCuttingEdges,
  CuttingLine_WaitP1,
  CuttingLine_WaitP2,
  SelectTrimTargets,
};

enum class OffsetPhase : std::uint8_t {
  WaitDistanceOrThrough,
  WaitSelectEntity,
  WaitSidePick,
  WaitThroughPick,
};

enum class AlignPhase : std::uint8_t {
  PickSelection,
  PickSrc,
  PickDst,
};

/// Active session for ARRAY (rectangular and polar, model and paper space).
class ArraySession final : public CommandSession {
 public:
  explicit ArraySession(bool paper, ArrayPhase initialPhase = ArrayPhase::PickSelection)
      : onPaper(paper), phase(initialPhase) {}

  bool onPaper = false;
  ArrayType type = ArrayType::Rectangular;
  ArrayPhase phase = ArrayPhase::PickSelection;
  int cols = 0;
  int rows = 0;
  float colSpacing = 0.f;
  float rowSpacing = 0.f;
  int levels = 1;
  float levelSpacing = 0.f;
  float anchorX = 0.f;
  float anchorY = 0.f;
  float anchorZ = 0.f;
  float centerX = 0.f;
  float centerY = 0.f;
  float centerZ = 0.f;
  int itemCount = 0;
  float fillAngleDeg = 360.f;
  bool rotateItems = true;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Array; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse ARRAY input — see command hints.";
  }
};

/// Active session for LENGTHEN.
class LengthenSession final : public CommandSession {
 public:
  LengthenSession() = default;

  LengthenPhase phase = LengthenPhase::WaitSelectOrMode;
  SelectedEntity pendingEntity{};
  bool pendingNearFirst = false;
  float pendingCurrentLength = 0.f;
  bool pendingApplyOnValue = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Lengthen; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse LENGTHEN input — see command hints.";
  }
};

/// Active session for EXTEND.
class ExtendSession final : public CommandSession {
 public:
  ExtendSession() = default;

  ExtendPhase phase = ExtendPhase::SelectBoundaries;
  std::vector<SelectedEntity> boundaries;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Extend; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

/// Active session for BREAK.
class BreakSession final : public CommandSession {
 public:
  BreakSession() = default;

  BreakPhase phase = BreakPhase::SelectFirstPoint;
  SelectedEntity entity{};
  BreakPoint p1{};

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Break; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

/// Active session for FILLET (both 2D curve fillet and prompted solid-edge fillet).
class FilletSession final : public CommandSession {
 public:
  explicit FilletSession(bool solidPrompt = false) : solidAwaitingRadius(solidPrompt) {}

  FilletPhase phase = FilletPhase::WaitFirstEntity;
  SelectedEntity firstEntity{};
  int firstPolySeg = -1;
  float firstPickX = 0.f;
  float firstPickY = 0.f;
  ray3d::Ray firstPickRay{};
  bool textAwaitingRadius = false;
  bool textAwaitingTrim = false;
  bool solidAwaitingRadius = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Fillet; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse FILLET input — see command hints.";
  }
};

/// Active session for CHAMFER (both 2D curve chamfer and prompted solid-edge chamfer).
class ChamferSession final : public CommandSession {
 public:
  explicit ChamferSession(bool solidPrompt = false) : solidAwaitingDistance(solidPrompt) {}

  ChamferPhase phase = ChamferPhase::WaitFirstEntity;
  SelectedEntity firstEntity{};
  int firstPolySeg = -1;
  float firstPickX = 0.f;
  float firstPickY = 0.f;
  bool textAwaitingFirstValue = false;
  bool textAwaitingSecondDist = false;
  bool textAwaitingAngle = false;
  bool textAwaitingTrim = false;
  bool solidAwaitingDistance = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Chamfer; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse CHAMFER input — see command hints.";
  }
};

/// Active session for TRIM (smart line trim and classic cutting-edges trim, model and paper).
class TrimSession final : public CommandSession {
 public:
  explicit TrimSession(TrimPhase initialPhase) : phase(initialPhase) {}

  TrimPhase phase = TrimPhase::SelectCuttingEdges;
  std::vector<SelectedEntity> cutters;
  std::vector<PaperEntityRef> paperCutters;
  float cutInfP1x = 0.f;
  float cutInfP1y = 0.f;
  float cutInfP1z = 0.f;
  float cutInfP2x = 0.f;
  float cutInfP2y = 0.f;
  float cutInfP2z = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Trim; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

/// Active session for TRIMSTATE prompt.
class TrimStateSession final : public CommandSession {
 public:
  TrimStateSession() = default;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::TrimState; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

/// Active session for OFFSET (model and paper space).
class OffsetSession final : public CommandSession {
 public:
  OffsetSession() = default;

  OffsetPhase phase = OffsetPhase::WaitDistanceOrThrough;
  bool entityValid = false;
  SelectedEntity entity{};
  bool hoverHighlightValid = false;
  SelectedEntity hoverEntity{};
  float typedDistance = 0.f;
  bool throughMode = false;
  int paperLayoutIndex = -1;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Offset; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

/// Active session for DELETE (selection accumulation until Enter).
class DeleteSession final : public CommandSession {
 public:
  DeleteSession() = default;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Delete; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

/// Active session for JOIN (selection accumulation until Enter).
class JoinSession final : public CommandSession {
 public:
  JoinSession() = default;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Join; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

/// Active session for ALIGN (2D Helmert similarity transformation).
class AlignSession final : public CommandSession {
 public:
  explicit AlignSession(AlignPhase initialPhase) : phase(initialPhase) {}

  AlignPhase phase = AlignPhase::PickSrc;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Align; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

} // namespace stepengine
