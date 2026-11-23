#pragma once
// ADR-0012 / Issue #1578: Pipe, Revcloud, FeatureLine, Grading, and PadSolid sessions (Batch 3).

#include "engine/CommandSession.hpp"
#include "GoSurvey/Document/cadpiperun.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct AppCommandState;

namespace stepengine {

// --- REVCLOUD ---

enum class RevcloudPhase : std::uint8_t {
  WaitFirstPoint,
  WaitObject,
  WaitNextPoint,
};

struct RevcloudSession final : CommandSession {
  RevcloudPhase phase = RevcloudPhase::WaitFirstPoint;
  std::vector<double> draft;
  double z = 0.0;
  char pending = 0;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Revcloud; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- PIPERUN ---

enum class PipeRunPhase : std::uint8_t {
  WaitNominalSize,
  WaitWallThickness,
  WaitFirstPoint,
  WaitNextPoint,
};

struct PipeRunSession final : CommandSession {
  PipeRunPhase phase = PipeRunPhase::WaitNominalSize;
  std::vector<double> draftVerts;
  int liveIndex = -1;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::PipeRun; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;
};

// --- PIPEFIT ---

struct PipeFitSession final : CommandSession {
  int runIndex = -1;
  CadPipePartType partType = CadPipePartType::None;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::PipeFit; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- PIPESPLIT ---

struct PipeSplitSession final : CommandSession {
  int runIndex = -1;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::PipeSplit; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- FEATURELINE ---

struct FeatureLineSession final : CommandSession {
  std::vector<float> draftVerts;
  std::vector<std::uint8_t> draftElevPt;
  std::string draftName;

  bool pendingPoint = false;
  float pendingX = 0.f;
  float pendingY = 0.f;
  float pendingDefaultZ = 0.f;
  bool nextIsElevPoint = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::FeatureLine; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- GRADING ---

enum class GradingPhase : std::uint8_t {
  WaitBaseline,
  WaitOptions,
  WaitCutSlope,
  WaitFillSlope,
  WaitSurfaceName,
  WaitSide,
  WaitTargetKind,
  WaitTargetValue,
  WaitDistanceGrade,
};

struct GradingSession final : CommandSession {
  GradingPhase phase = GradingPhase::WaitBaseline;
  std::uint64_t baselineId = 0;
  std::uint64_t editId = 0;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Grading; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- PADSOLID ---

enum class PadSolidPhase : std::uint8_t {
  WaitBoundary,
  WaitOptions,
  WaitSurfaceName,
};

struct PadSolidSession final : CommandSession {
  PadSolidPhase phase = PadSolidPhase::WaitBoundary;
  std::uint64_t boundaryId = 0;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::PadSolid; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

} // namespace stepengine
