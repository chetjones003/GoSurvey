#pragma once
// ADR-0012 / Issue #1578: Step-engine CommandSession subclasses for 3D Gizmo commands
// (3DMOVE/3DROTATE/3DSCALE), Property commands (CHPROP/MATCHPROP/LAYMCUR), and
// Geolocation commands (GEOMARKPOINT/GEOMARKLATLONG/GEOREORIENTMARKER/DRAWINGSETTINGSPICK/GEOCAPTUREAREA).

#include "engine/CommandSession.hpp"

namespace stepengine {

class GizmoCmdSession final : public CommandSession {
 public:
  explicit GizmoCmdSession(CadCommandKind k) : kind_(k) {}

  [[nodiscard]] CadCommandKind kind() const override { return kind_; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;

 private:
  CadCommandKind kind_ = CadCommandKind::Move3d;
};

class PropCmdSession final : public CommandSession {
 public:
  explicit PropCmdSession(CadCommandKind k) : kind_(k) {}

  [[nodiscard]] CadCommandKind kind() const override { return kind_; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;

 private:
  CadCommandKind kind_ = CadCommandKind::ChProp;
};

class GeoCmdSession final : public CommandSession {
 public:
  explicit GeoCmdSession(CadCommandKind k) : kind_(k) {}

  [[nodiscard]] CadCommandKind kind() const override { return kind_; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;

 private:
  CadCommandKind kind_ = CadCommandKind::GeoMarkPoint;
};

void ExecuteGizmoSettingCommand(AppCommandState& st, const std::string& args,
                                std::vector<std::string>& log);

} // namespace stepengine
