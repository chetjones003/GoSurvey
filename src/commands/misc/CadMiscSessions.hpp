#pragma once
// ADR-0012 / Issue #1578: Step-engine CommandSession subclasses for PDFATTACH,
// MVIEW (PaperRectViewport), VPFREEZE, VPTHAW, and EXTRACTCENTERLINE.

#include "engine/CommandSession.hpp"

namespace stepengine {

enum class PdfAttachPhase : std::uint8_t {
  WaitDialog,       ///< Dialog is open; user browses / configures
  Building,         ///< Async rasterize running in background; dialog shows spinner
  WaitInsertPoint,  ///< User picks insertion point in viewport
  WaitScaleRef,     ///< User picks second point to define scale interactively
  WaitRotationPt,   ///< User picks rotation reference point
};

class PdfAttachSession final : public CommandSession {
 public:
  PdfAttachPhase phase = PdfAttachPhase::WaitDialog;

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
  int phase = 0;       ///< 0 = need first corner, 1 = need second
  float firstXIn = 0.f;
  float firstYIn = 0.f;
  int mode = 0;        ///< REQ-490: 0 = rectangle, 1 = awaiting closed polyline Object pick

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
