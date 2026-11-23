#pragma once
// ADR-0012 / Issue #1578: Solid command sessions (Batch 3).
// Holds per-command phase and transient draft/pick state for Solid primitives,
// Extrude, PressPull, Revolve, Slice, Section, SectionPlane, Loft, Sweep,
// Boolean (Union/Subtract/Intersect), and Polysolid.

#include "engine/CommandSession.hpp"
#include <GoSurvey/Geometry/brep.hpp>
#include <GoSurvey/Math/ray3d.hpp>

#include <cstdint>
#include <memory>
#include <vector>

struct AppCommandState;
struct SelectedSubObject;

namespace stepengine {

// --- Prompted solid primitives (BOX / WEDGE / PYRAMID / CYLINDER / CONE / SPHERE / TORUS) ---

enum class SolidPhase : std::uint8_t {
  WaitBasePoint,
  WaitParameters,
};

struct SolidPrimitiveSession final : CommandSession {
  static constexpr int kMaxSolidParams = 4;

  SolidPhase phase = SolidPhase::WaitBasePoint;
  brep::PrimitiveKind solidKind = brep::PrimitiveKind::None;
  ray3d::Vec3 solidBase{};
  double solidParamValue[kMaxSolidParams] = {0.0, 0.0, 0.0, 0.0};
  bool solidParamSet[kMaxSolidParams] = {false, false, false, false};

  bool solidPickValid = false;
  double solidPickA = 0.0;
  double solidPickB = 0.0;
  double solidPickAngleRad = 0.0;

  double solidBaseAngleRad = 0.0;
  bool solidInscribed = false;
  bool solidBaseIsCorner = false;
  double solidCornerDx = 0.0;
  double solidCornerDy = 0.0;
  int solidPendingParam = -1;
  bool solidPendingIsDiameter = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Solid; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- EXTRUDE ---

enum class ExtrudePhase : std::uint8_t {
  SelectProfiles,
  WaitHeight,
};

struct ExtrudeSession final : CommandSession {
  ExtrudePhase phase = ExtrudePhase::SelectProfiles;
  std::vector<brep::Profile> profiles;
  bool heightPickValid = false;
  double heightPick = 0.0;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Extrude; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- PRESSPULL ---

enum class PressPullPhase : std::uint8_t {
  SelectTarget,
  WaitDistance,
};

struct PressPullSession final : CommandSession {
  PressPullPhase phase = PressPullPhase::SelectTarget;
  bool onFace = false;
  SelectedSubObject face{};
  brep::Profile profile;
  bool distPickValid = false;
  double distPick = 0.0;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::PressPull; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- REVOLVE ---

enum class RevolvePhase : std::uint8_t {
  SelectProfiles,
  WaitAxisStart,
  WaitAxisEnd,
  WaitAngle,
};

struct RevolveSession final : CommandSession {
  RevolvePhase phase = RevolvePhase::SelectProfiles;
  std::vector<brep::Profile> profiles;
  ray3d::Vec3 axisStart{};
  bool axisStartSet = false;
  ray3d::Vec3 axisEnd{};
  double angleDeg = 360.0;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Revolve; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- SLICE ---

enum class SlicePhase : std::uint8_t {
  SelectSolids,
  WaitP1,
  WaitP2,
  WaitP3,
  WaitKeepSide,
};

struct SliceSession final : CommandSession {
  SlicePhase phase = SlicePhase::SelectSolids;
  std::vector<int> solidIndices;
  ray3d::Vec3 p1{};
  ray3d::Vec3 p2{};
  ray3d::Vec3 p3{};

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Slice; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- SECTION ---

enum class SectionPhase : std::uint8_t {
  SelectSolids,
  WaitP1,
  WaitP2,
  WaitP3,
};

struct SectionSession final : CommandSession {
  SectionPhase phase = SectionPhase::SelectSolids;
  std::vector<int> solidIndices;
  std::vector<std::weak_ptr<const brep::Solid>> solidOwners;
  ray3d::Vec3 p1{};
  ray3d::Vec3 p2{};
  ray3d::Vec3 p3{};

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Section; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- SECTIONPLANE ---

enum class SectionPlanePhase : std::uint8_t {
  PickFaceOrPoint,
  WaitThroughPoint,
};

struct SectionPlaneSession final : CommandSession {
  SectionPlanePhase phase = SectionPlanePhase::PickFaceOrPoint;
  ray3d::Vec3 p1{};
  ray3d::Vec3 previewPoint{};
  bool previewValid = false;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::SectionPlane; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- LOFT ---

enum class LoftPhase : std::uint8_t {
  SelectProfiles,
};

struct LoftSession final : CommandSession {
  LoftPhase phase = LoftPhase::SelectProfiles;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Loft; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- SWEEP ---

enum class SweepPhase : std::uint8_t {
  SelectInputs,
};

struct SweepSession final : CommandSession {
  SweepPhase phase = SweepPhase::SelectInputs;
  double twistDeg = 0.0;
  bool alignToPath = true;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Sweep; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- BOOLEAN (UNION / SUBTRACT / INTERSECT) ---

enum class BooleanPhase : std::uint8_t {
  SelectOperands,
  SelectMinuend,
  SelectSubtrahend,
};

struct BooleanSession final : CommandSession {
  BooleanPhase phase = BooleanPhase::SelectOperands;
  int op = 0;  ///< matches CadBooleanOp
  std::vector<int> minuend;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Boolean; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

// --- POLYSOLID ---

enum class PolysolidPhase : std::uint8_t {
  WaitFirstPoint,
  WaitObject,
  WaitNextPoint,
};

struct PolysolidSession final : CommandSession {
  PolysolidPhase phase = PolysolidPhase::WaitFirstPoint;
  brep::Path path;
  ray3d::Vec3 base{};
  bool arcMode = false;
  char pending = 0;

  [[nodiscard]] CadCommandKind kind() const override { return CadCommandKind::Polysolid; }
  [[nodiscard]] StepDescriptor currentStep(const AppCommandState& st) const override;
  StepSubmitResult onText(AppCommandState& st, const std::string& trimmedLine,
                          std::vector<std::string>& log) override;
  StepSubmitResult onBlankEnter(AppCommandState& st, std::vector<std::string>& log) override;
  void onViewportPick(AppCommandState& st, const StepPickInput& pick,
                      std::vector<std::string>& log) override;
  void onCancel(AppCommandState& st, std::vector<std::string>& log) override;
};

} // namespace stepengine
