#pragma once
// ADR-0012 / Issue #1578: Step-engine CommandSession subclasses for PDFATTACH,
// MVIEW (PaperRectViewport), VPFREEZE, VPTHAW, and EXTRACTCENTERLINE.

#include "engine/CommandSession.hpp"

namespace stepengine {

class PdfAttachSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::PdfAttach; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;
};

class PaperRectViewportSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::PaperRectViewport; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class VpFreezeSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::VpFreeze; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class VpThawSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::VpThaw; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class ExtractCenterlineSession final : public CommandSession {
 public:
  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::ExtractCenterline; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

} // namespace stepengine
