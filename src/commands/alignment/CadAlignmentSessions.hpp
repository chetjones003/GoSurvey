#pragma once

#include "GoSurvey/Survey/AlignmentModel.hpp"
#include "engine/CommandSession.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stepengine {

class AlignmentLayoutSession final : public CommandSession {
public:
  std::string name;
  std::vector<alignment::Point2> pis;
  alignment::Alignment base;
  bool snapshotTaken = false;
  bool fixedDirection = false;
  double directionAzimuth = 0.0;
  size_t warningsShown = 0;

  CadCommandKind kind() const override { return CadCommandKind::AlignmentLayout; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

enum class AlignmentFromObjectsPhase : std::uint8_t { WaitFirst, WaitMore, WaitDirection };

class AlignmentFromObjectsSession final : public CommandSession {
public:
  AlignmentFromObjectsPhase phase = AlignmentFromObjectsPhase::WaitFirst;
  bool hasPick = false;
  double pickX = 0.0;
  double pickY = 0.0;
  bool reverse = false;

  CadCommandKind kind() const override { return CadCommandKind::AlignmentFromObjects; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class StationOffsetPickSession final : public CommandSession {
public:
  std::string pickArgs;

  CadCommandKind kind() const override { return CadCommandKind::StationOffsetPick; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

} // namespace stepengine
