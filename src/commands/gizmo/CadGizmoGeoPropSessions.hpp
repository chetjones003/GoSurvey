#pragma once
// ADR-0012 / Issue #1578: Step-engine CommandSession subclasses for 3D Gizmo commands
// (3DMOVE/3DROTATE/3DSCALE), Property commands (CHPROP/MATCHPROP/LAYMCUR), and
// Geolocation commands (GEOMARKPOINT/GEOMARKLATLONG/GEOREORIENTMARKER/DRAWINGSETTINGSPICK/GEOCAPTUREAREA).

#include "CadEntities.hpp"
#include "engine/CommandSession.hpp"

namespace stepengine {

/// 3DMOVE / 3DROTATE / 3DSCALE: collecting the selection, asking for the base point the gizmo
/// sits on (D-2026-09-28-c), or showing the handles on it.
enum class GizmoCmdPhase : std::uint8_t { SelectObjects, BasePoint, Handles };

/// CHPROP / MATCHPROP / LAYMCUR active-command phase.
enum class PropCmdPhase : std::uint8_t { SelectObjects, WaitProperty, WaitValue, SelectTargets };

/// The property CHPROP is asking a value for.
enum class ChPropProperty : std::uint8_t { Color, Layer, Linetype, Lineweight };

/// GEOMARKLATLONG / GEOREORIENTMARKER / DRAWINGSETTINGSPICK / GEOCAPTUREAREA phase.
enum class GeoCmdPhase : std::uint8_t { WaitFirst, WaitSecond, Capturing };

class GizmoCmdSession final : public CommandSession {
 public:
  explicit GizmoCmdSession(CadCommandKind k) : kind_(k) {}

  GizmoCmdPhase phase = GizmoCmdPhase::SelectObjects;

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

  PropCmdPhase phase = PropCmdPhase::SelectObjects;
  ChPropProperty chPropProperty = ChPropProperty::Color;
  EntityAttributes matchPropSource;
  bool matchPropSourceHasLineStyle = false;

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

  GeoCmdPhase phase = GeoCmdPhase::WaitFirst;
  double firstA = 0.0; ///< GEOMARKLATLONG: latitude; GEOREORIENTMARKER/pick: first X (local)
  double firstB = 0.0; ///< GEOREORIENTMARKER/pick: first Y (local)

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
