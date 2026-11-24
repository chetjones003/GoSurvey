#pragma once
// ADR-0012 / Issue #1578: Step-engine CommandSession subclasses for UCS, PLAN, ELEV,
// SECTIONCLIP, ZOOM WINDOW, PAN, and ORBIT.

#include "engine/CommandSession.hpp"

namespace stepengine {

enum class UcsPhase : std::uint8_t {
  Idle,
  WaitOriginOrOption,  ///< top-level prompt: a point, or one of the keywords
  WaitXAxisPoint,      ///< after an origin: a point on the new +X, or blank to accept origin-only
  WaitXyPoint,         ///< after an X point: a point in the +Y half of the XY plane, or blank
  WaitRotationAngle,   ///< X / Y / Z: degrees, right-hand rule about rotationAxis
  WaitZAxisOrigin,     ///< ZAxis: the origin
  WaitRotationAngleP1, ///< X/Y/Z + 2P: first point of the pair that defines the angle
  WaitRotationAngleP2, ///< X/Y/Z + 2P: second point; the angle is p1->p2 in the rotation plane
  WaitZAxisPoint,      ///< ZAxis: a point on the positive Z
  WaitObjectPick,      ///< Object: click an entity to align to
  WaitNamedName,       ///< Named: the name to save the current frame under
};

enum class PlanPhase : std::uint8_t { Idle, WaitOption, WaitNamedName };

class UcsSession final : public CommandSession {
 public:
  UcsPhase phase = UcsPhase::WaitOriginOrOption;
  char rotationAxis = 'Z';
  ray3d::Vec3 pendingOrigin{0.0, 0.0, 0.0};
  ray3d::Vec3 pendingXAxisPoint{0.0, 0.0, 0.0};
  ray3d::Vec3 angleBasePoint{0.0, 0.0, 0.0};

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Ucs; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class PlanSession final : public CommandSession {
 public:
  PlanPhase phase = PlanPhase::WaitOption;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Plan; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class ElevSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Elev; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class SectionClipSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::SectionClip; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class ZoomSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Zoom; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class PanSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Pan; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class OrbitSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Orbit; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

} // namespace stepengine
