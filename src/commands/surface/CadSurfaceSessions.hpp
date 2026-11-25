#pragma once

#include "CadEntities.hpp"
#include "engine/CommandSession.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace stepengine {

enum class SurfaceElevPhase : std::uint8_t { WaitFirst, WaitSecond };

class SurfaceElevGradeSession final : public CommandSession {
public:
  SurfaceElevPhase phase = SurfaceElevPhase::WaitFirst;
  double fromX = 0.0;
  double fromY = 0.0;
  std::vector<std::pair<std::string, double>> fromZ;

  CadCommandKind kind() const override { return CadCommandKind::SurfaceElevGrade; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class WaterDropSession final : public CommandSession {
public:
  std::string surfaceName;

  CadCommandKind kind() const override { return CadCommandKind::WaterDrop; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class CatchmentSession final : public CommandSession {
public:
  std::string surfaceName;

  CadCommandKind kind() const override { return CadCommandKind::Catchment; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class SwapTinEdgeSession final : public CommandSession {
public:
  std::string surfaceName;

  CadCommandKind kind() const override { return CadCommandKind::SwapTinEdge; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class AddTinPointSession final : public CommandSession {
public:
  std::string surfaceName;

  CadCommandKind kind() const override { return CadCommandKind::AddTinPoint; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class DelTinPointSession final : public CommandSession {
public:
  std::string surfaceName;

  CadCommandKind kind() const override { return CadCommandKind::DelTinPoint; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

enum class MoveTinPointPhase : std::uint8_t { WaitFrom, WaitTo };

class MoveTinPointSession final : public CommandSession {
public:
  MoveTinPointPhase phase = MoveTinPointPhase::WaitFrom;
  std::string surfaceName;
  double fromX = 0.0;
  double fromY = 0.0;

  CadCommandKind kind() const override { return CadCommandKind::MoveTinPoint; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class DelTinLineSession final : public CommandSession {
public:
  std::string surfaceName;

  CadCommandKind kind() const override { return CadCommandKind::DelTinLine; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

enum class QuickProfilePhase : std::uint8_t { WaitFirst, WaitSecond };

class QuickProfileSession final : public CommandSession {
public:
  QuickProfilePhase phase = QuickProfilePhase::WaitFirst;
  std::string surfaceName;
  double fromX = 0.0;
  double fromY = 0.0;

  CadCommandKind kind() const override { return CadCommandKind::QuickProfile; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class DesignateBreaklineSession final : public CommandSession {
public:
  std::string surfaceName;
  bool contourSource = false;

  CadCommandKind kind() const override { return CadCommandKind::DesignateBreakline; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class DesignateBoundarySession final : public CommandSession {
public:
  std::string surfaceName;
  CadBoundaryKind boundaryKind = CadBoundaryKind::Outer;

  CadCommandKind kind() const override { return CadCommandKind::DesignateBoundary; }
  StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

} // namespace stepengine
