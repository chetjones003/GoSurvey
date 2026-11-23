#pragma once
// ADR-0012 / Issue #1578: Step-engine CommandSession subclasses for UCS, PLAN, ELEV,
// SECTIONCLIP, ZOOM WINDOW, PAN, and ORBIT.

#include "engine/CommandSession.hpp"

namespace stepengine {

class UcsSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Ucs; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;
};

class PlanSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Plan; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;
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
