#pragma once
// ADR-0012 / Issue #1578 (Track B1 Pilot): Active-command sessions for the Modify family
// (MOVE, COPY, ROTATE, SCALE, MIRROR, STRETCH) and PASTE.
// Each session owns its phase enum and transient scratch fields, dropped automatically on finish or Esc.

#include "engine/CommandSession.hpp"

#include <cstdint>

namespace stepengine {

enum class ModifyPhase : std::uint8_t {
  PickSelection,
  NeedBase,
  NeedDestination,
};

enum class ScalePhase : std::uint8_t {
  FactorPick,
  Ref_WaitP1,
  Ref_WaitP2,
  NewLength_WaitTypedOrP1,
  NewLength_WaitP2,
};

enum class RotatePhase : std::uint8_t {
  PickSelection,
  NeedBase,
  NeedAngleOrReference,
  Ref_WaitP1,
  Ref_WaitP2,
  AfterReference_WaitAngleOrP,
  AnglePoints_WaitP1,
  AnglePoints_WaitP2,
};

enum class MirrorPhase : std::uint8_t {
  PickSelection,
  NeedP1,
  NeedP2,
  NeedEraseAnswer,
};

/// Active session for MOVE and COPY.
class MoveCopySession final : public CommandSession {
 public:
  explicit MoveCopySession(bool copy, ModifyPhase initialPhase)
      : isCopy(copy), phase(initialPhase) {}

  bool isCopy = false;
  ModifyPhase phase = ModifyPhase::PickSelection;
  float baseX = 0.f;
  float baseY = 0.f;
  float baseZ = 0.f;

  [[nodiscard]] CadCommandKind kind() const override {
    return isCopy ? CadCommandKind::Copy : CadCommandKind::Move;
  }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse MOVE/COPY input — use X,Y or @dx,dy from base.";
  }
};

/// Active session for STRETCH.
class StretchSession final : public CommandSession {
 public:
  StretchSession() = default;

  ModifyPhase phase = ModifyPhase::PickSelection;
  float baseX = 0.f;
  float baseY = 0.f;
  float baseZ = 0.f;
  float rectMnX = 0.f;
  float rectMxX = 0.f;
  float rectMnY = 0.f;
  float rectMxY = 0.f;
  bool rectInUcsPlane = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Stretch; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse STRETCH input — use X,Y or @dx,dy from base.";
  }
};

/// Active session for interactive PASTE destination placement.
class PasteSession final : public CommandSession {
 public:
  PasteSession(float bx, float by) : baseX(bx), baseY(by) {}

  float baseX = 0.f;
  float baseY = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Paste; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

/// Active session for SCALE.
class ScaleSession final : public CommandSession {
 public:
  explicit ScaleSession(ModifyPhase initialPhase) : phase(initialPhase) {}

  ModifyPhase phase = ModifyPhase::PickSelection;
  ScalePhase scalePhase = ScalePhase::FactorPick;
  float baseX = 0.f;
  float baseY = 0.f;
  float baseZ = 0.f;
  float refDist = 1.f;
  float refP1X = 0.f;
  float refP1Y = 0.f;
  float newLenP1X = 0.f;
  float newLenP1Y = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Scale; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse SCALE input — see command hints (base X,Y; factor; R + reference/new length).";
  }
};

/// Active session for ROTATE.
class RotateSession final : public CommandSession {
 public:
  explicit RotateSession(RotatePhase initialPhase) : phase(initialPhase) {}

  RotatePhase phase = RotatePhase::PickSelection;
  float baseX = 0.f;
  float baseY = 0.f;
  float baseZ = 0.f;
  float refX1 = 0.f;
  float refY1 = 0.f;
  float refX2 = 0.f;
  float refY2 = 0.f;
  float anglePt1X = 0.f;
  float anglePt1Y = 0.f;
  bool copyMode = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Rotate; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse ROTATE input — see command hints.";
  }
};

/// Active session for MIRROR.
class MirrorSession final : public CommandSession {
 public:
  explicit MirrorSession(MirrorPhase initialPhase) : phase(initialPhase) {}

  MirrorPhase phase = MirrorPhase::PickSelection;
  float p1X = 0.f;
  float p1Y = 0.f;
  float p1Z = 0.f;
  float p2X = 0.f;
  float p2Y = 0.f;
  float p2Z = 0.f;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Mirror; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
  [[nodiscard]] const char* unparsedFallback() const override {
    return "Could not parse MIRROR input — see command hints.";
  }
};

} // namespace stepengine
