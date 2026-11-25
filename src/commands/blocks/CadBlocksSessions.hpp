#pragma once
// ADR-0012 / Issue #1578: Step-engine CommandSession subclasses for INSERT and
// block-editor connection/fitting wizards (BCONNECT, BCONNECTEDIT, BCONNECTMODE, BLOCKFITTING).

#include <GoSurvey/Document/cadblock.hpp>
#include "engine/CommandSession.hpp"

namespace stepengine {

enum class InsertBlockPhase : std::uint8_t {
  WaitDialog,
  WaitInsertPoint,
  WaitScale,
  WaitRotation,
  WaitAlignFace,       ///< Pick a planar solid face to orient the fitting (issue #475 inc3)
  WaitConnectorTarget, ///< Pick a target connection port on a placed block (issue #475 inc5)
  WaitAttributes,
};

enum class BConnectPhase : std::uint8_t {
  WaitName,
  WaitNominalSize,
  WaitRole,
  WaitEngagement,
  WaitCompatTag,
  WaitPoint,
};

enum class BConnectEditPhase : std::uint8_t {
  WaitConnName,
  WaitRemoveConfirm, ///< Only reached when the typed connection name exists.
  WaitNominalSize,
  WaitRole,
  WaitEngagement,
  WaitCompatTag,
};

enum class BConnectModePhase : std::uint8_t {
  WaitConnName,
  WaitModeName,
  WaitRemoveConfirm, ///< Only reached when the typed mode name already exists.
  WaitTarget,
  WaitRole,
  WaitEngagement,
  WaitCompatTag,
  WaitIsDefault,
};

enum class BlockFittingPhase : std::uint8_t {
  WaitPartType,
  WaitNominalSize,
  WaitPressureClass,
  WaitPartNumber,
};

class InsertBlockSession final : public CommandSession {
 public:
  explicit InsertBlockSession(InsertBlockPhase initialPhase = InsertBlockPhase::WaitDialog)
      : phase(initialPhase) {}

  InsertBlockPhase phase = InsertBlockPhase::WaitDialog;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::InsertBlock; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  void onReset(AppCommandState& st) override;
};

class BConnectSession final : public CommandSession {
 public:
  BConnectPhase phase = BConnectPhase::WaitName;
  bool awaitingFace = false;
  std::string name;
  std::string nominalSize;
  CadBlockConnectionRole rolePending = CadBlockConnectionRole::Inlet;
  float engagementPending = 0.f;
  std::string compatTagPending;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::BConnect; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class BConnectEditSession final : public CommandSession {
 public:
  BConnectEditPhase phase = BConnectEditPhase::WaitConnName;
  std::string connName;
  std::string nominalSizePending;
  CadBlockConnectionRole rolePending = CadBlockConnectionRole::Inlet;
  float engagementPending = 0.f;
  std::string compatTagPending;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::BConnectEdit; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class BConnectModeSession final : public CommandSession {
 public:
  BConnectModePhase phase = BConnectModePhase::WaitConnName;
  std::string connName;
  std::string modeName;
  bool editingExisting = false;
  CadConnectionModeTarget targetPending = CadConnectionModeTarget::GenericPort;
  CadBlockConnectionRole rolePending = CadBlockConnectionRole::Inlet;
  float engagementPending = 0.f;
  std::string compatTagPending;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::BConnectMode; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

class BlockFittingSession final : public CommandSession {
 public:
  BlockFittingPhase phase = BlockFittingPhase::WaitPartType;
  CadPipePartType partTypePending = CadPipePartType::None;
  std::string nominalSizePending;
  CadPipePressureClass pressureClassPending = CadPipePressureClass::None;
  std::string partNumberPending;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::BlockFitting; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

} // namespace stepengine
