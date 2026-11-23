// ADR-0012 / Issue #1578: Solid command sessions and registry module (Batch 3).

#include "CadCommandsInternal.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace stepengine {

// --- SolidPrimitiveSession ---

StepDescriptor SolidPrimitiveSession::currentStep(const AppCommandState& st) const {
  return StepDescriptor::Custom(CadSolidPromptText(st), StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult SolidPrimitiveSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                               std::vector<std::string>& log) {
  if (!HandleSolidTextInput(trimmedLine, st, log))
    log.push_back(CadSolidPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult SolidPrimitiveSession::onBlankEnter(AppCommandState& st,
                                                     std::vector<std::string>& log) {
  (void)HandleSolidTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void SolidPrimitiveSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                           std::vector<std::string>& log) {
  SubmitSolidViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void SolidPrimitiveSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back(SolidVerbUpper(solidKind) + " canceled.");
}

// --- ExtrudeSession ---

StepDescriptor ExtrudeSession::currentStep(const AppCommandState& st) const {
  if (phase == ExtrudePhase::SelectProfiles)
    return StepDescriptor::SelectObjects(CadExtrudePromptText(st), BlankEnterPolicy::Finish, false);
  return StepDescriptor::Custom(CadExtrudePromptText(st), StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult ExtrudeSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (!HandleExtrudeTextInput(trimmedLine, st, log))
    log.push_back(CadExtrudePromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult ExtrudeSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleExtrudeTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void ExtrudeSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  if (phase == ExtrudePhase::SelectProfiles) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick, false, log);
    return;
  }
  SubmitExtrudeViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void ExtrudeSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("EXTRUDE canceled.");
}

// --- PressPullSession ---

StepDescriptor PressPullSession::currentStep(const AppCommandState& st) const {
  if (phase == PressPullPhase::SelectTarget)
    return StepDescriptor::SelectObjects(CadPressPullPromptText(st), BlankEnterPolicy::Finish, false);
  return StepDescriptor::Custom(CadPressPullPromptText(st), StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult PressPullSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                          std::vector<std::string>& log) {
  if (!HandlePressPullTextInput(trimmedLine, st, log))
    log.push_back(CadPressPullPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult PressPullSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandlePressPullTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void PressPullSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                      std::vector<std::string>& log) {
  if (phase == PressPullPhase::SelectTarget) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick, false, log);
    return;
  }
  SubmitPressPullViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void PressPullSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("PRESSPULL canceled.");
}

// --- RevolveSession ---

StepDescriptor RevolveSession::currentStep(const AppCommandState& st) const {
  switch (phase) {
  case RevolvePhase::SelectProfiles:
    return StepDescriptor::SelectObjects(CadRevolvePromptText(st), BlankEnterPolicy::Finish, false);
  case RevolvePhase::WaitAxisStart:
  case RevolvePhase::WaitAxisEnd:
    return StepDescriptor::Custom(CadRevolvePromptText(st), StepClickRoute::SnappedPointPick,
                                  BlankEnterPolicy::Custom);
  case RevolvePhase::WaitAngle:
    return StepDescriptor::Custom(CadRevolvePromptText(st), StepClickRoute::Ignore,
                                  BlankEnterPolicy::TakeDefault);
  }
  return StepDescriptor::Custom(CadRevolvePromptText(st), StepClickRoute::Ignore,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult RevolveSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (!HandleRevolveTextInput(trimmedLine, st, log))
    log.push_back(CadRevolvePromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult RevolveSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleRevolveTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void RevolveSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  if (phase == RevolvePhase::SelectProfiles) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick, false, log);
    return;
  }
  SubmitRevolveViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void RevolveSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("REVOLVE canceled.");
}

// --- SliceSession ---

StepDescriptor SliceSession::currentStep(const AppCommandState& st) const {
  if (phase == SlicePhase::SelectSolids)
    return StepDescriptor::SelectObjects(CadSlicePromptText(st), BlankEnterPolicy::Finish, false);
  return StepDescriptor::Custom(CadSlicePromptText(st), StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult SliceSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  if (!HandleSliceTextInput(trimmedLine, st, log))
    log.push_back(CadSlicePromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult SliceSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleSliceTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void SliceSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  if (phase == SlicePhase::SelectSolids) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick, false, log);
    return;
  }
  SubmitSliceViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void SliceSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("SLICE canceled.");
}

// --- SectionSession ---

StepDescriptor SectionSession::currentStep(const AppCommandState& st) const {
  if (phase == SectionPhase::SelectSolids)
    return StepDescriptor::SelectObjects(CadSectionPromptText(st), BlankEnterPolicy::Finish, false);
  return StepDescriptor::Custom(CadSectionPromptText(st), StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult SectionSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (!HandleSectionTextInput(trimmedLine, st, log))
    log.push_back(CadSectionPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult SectionSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleSectionTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void SectionSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  if (phase == SectionPhase::SelectSolids) {
    if (st.selBoxWaitingSecond)
      FinishSelectionBox(st, pick, false, log);
    return;
  }
  SubmitSectionViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void SectionSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("SECTION canceled — nothing was drawn.");
}

// --- SectionPlaneSession ---

StepDescriptor SectionPlaneSession::currentStep(const AppCommandState& st) const {
  return StepDescriptor::Custom(CadSectionPlanePromptText(st), StepClickRoute::SubObjectFacePick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult SectionPlaneSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                             std::vector<std::string>& log) {
  if (!HandleSectionPlaneTextInput(trimmedLine, st, log))
    log.push_back(CadSectionPlanePromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult SectionPlaneSession::onBlankEnter(AppCommandState& st,
                                                   std::vector<std::string>& log) {
  (void)HandleSectionPlaneTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void SectionPlaneSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("SECTIONPLANE canceled — no plane was placed.");
}

// --- LoftSession ---

StepDescriptor LoftSession::currentStep(const AppCommandState& st) const {
  return StepDescriptor::SelectObjects(CadLoftPromptText(st), BlankEnterPolicy::Finish, false);
}

StepSubmitResult LoftSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  if (!HandleLoftTextInput(trimmedLine, st, log))
    log.push_back(CadLoftPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult LoftSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleLoftTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void LoftSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>& log) {
  if (st.selBoxWaitingSecond)
    FinishSelectionBox(st, pick, false, log);
}

void LoftSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("LOFT canceled.");
}

// --- SweepSession ---

StepDescriptor SweepSession::currentStep(const AppCommandState& st) const {
  return StepDescriptor::SelectObjects(CadSweepPromptText(st), BlankEnterPolicy::Finish, false);
}

StepSubmitResult SweepSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                      std::vector<std::string>& log) {
  if (!HandleSweepTextInput(trimmedLine, st, log))
    log.push_back(CadSweepPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult SweepSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleSweepTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void SweepSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                  std::vector<std::string>& log) {
  if (st.selBoxWaitingSecond)
    FinishSelectionBox(st, pick, false, log);
}

void SweepSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("SWEEP canceled.");
}

// --- BooleanSession ---

StepDescriptor BooleanSession::currentStep(const AppCommandState& st) const {
  return StepDescriptor::SelectObjects(CadBooleanPromptText(st), BlankEnterPolicy::Finish, false);
}

StepSubmitResult BooleanSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  if (!HandleBooleanTextInput(trimmedLine, st, log))
    log.push_back(CadBooleanPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult BooleanSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleBooleanTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void BooleanSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  if (st.selBoxWaitingSecond)
    FinishSelectionBox(st, pick, false, log);
}

void BooleanSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("Command canceled.");
}

// --- PolysolidSession ---

StepDescriptor PolysolidSession::currentStep(const AppCommandState& st) const {
  if (phase == PolysolidPhase::WaitObject)
    return StepDescriptor::EntityPick(CadPolysolidPromptText(st), BlankEnterPolicy::Custom);
  return StepDescriptor::Custom(CadPolysolidPromptText(st), StepClickRoute::SnappedPointPick,
                                BlankEnterPolicy::Custom);
}

StepSubmitResult PolysolidSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                          std::vector<std::string>& log) {
  if (!HandlePolysolidTextInput(trimmedLine, st, log))
    log.push_back(CadPolysolidPromptText(st));
  return StepSubmitResult::Handled;
}

StepSubmitResult PolysolidSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandlePolysolidTextInput("", st, log);
  return StepSubmitResult::Handled;
}

void PolysolidSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                      std::vector<std::string>& log) {
  SubmitPolysolidViewportPick(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), log);
}

void PolysolidSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("POLYSOLID canceled.");
}

// --- Registry registration ---

namespace {

void RunIsolines(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::istringstream iss(args);
  std::string isoArg;
  if (iss >> isoArg) {
    char* end = nullptr;
    const long v = std::strtol(isoArg.c_str(), &end, 10);
    if (isoArg.empty() || !end || *end != '\0' || v < 0 || v > kSolidMaxIsolines) {
      log.push_back("ISOLINES — enter a whole number between 0 and " +
                    std::to_string(kSolidMaxIsolines) + " (0 draws edges only).");
      return;
    }
    st.viewportSolidIsolines = static_cast<int>(v);
    BumpCadGpuCache(st);
    log.push_back("ISOLINES = " + std::to_string(st.viewportSolidIsolines) + ".");
  } else {
    log.push_back("ISOLINES = " + std::to_string(st.viewportSolidIsolines) +
                  ". Usage: ISOLINES <0-" + std::to_string(kSolidMaxIsolines) + ">.");
  }
}

} // namespace

void RegisterSolidCommands(CommandRegistry& reg) {
  reg.Add({"box", "", "Create a box solid: BOX <X,Y[,Z]> <length> <width> <height>",
           CadCommandKind::Solid, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartSolidPrimitiveCommand(st, "box", log);
             else
               CadCreateSolidPrimitive(st, "box", args, log);
           }});
  reg.Add({"wedge", "we", "Create a wedge solid: WEDGE <X,Y[,Z]> <length> <width> <height>",
           CadCommandKind::Solid, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartSolidPrimitiveCommand(st, "wedge", log);
             else
               CadCreateSolidPrimitive(st, "wedge", args, log);
           }});
  reg.Add({"pyramid", "pyr",
           "Create a pyramid solid: PYRAMID <X,Y[,Z]> <sides> <base r> <top r> <height>",
           CadCommandKind::Solid, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartSolidPrimitiveCommand(st, "pyramid", log);
             else
               CadCreateSolidPrimitive(st, "pyramid", args, log);
           }});
  reg.Add({"cylinder", "cyl", "Create a cylinder solid: CYLINDER <X,Y[,Z]> <radius> <height>",
           CadCommandKind::Solid, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartSolidPrimitiveCommand(st, "cylinder", log);
             else
               CadCreateSolidPrimitive(st, "cylinder", args, log);
           }});
  reg.Add({"cone", "", "Create a cone solid: CONE <X,Y[,Z]> <base radius> <top radius> <height>",
           CadCommandKind::Solid, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartSolidPrimitiveCommand(st, "cone", log);
             else
               CadCreateSolidPrimitive(st, "cone", args, log);
           }});
  reg.Add({"sphere", "sph", "Create a sphere solid: SPHERE <X,Y[,Z]> <radius>",
           CadCommandKind::Solid, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartSolidPrimitiveCommand(st, "sphere", log);
             else
               CadCreateSolidPrimitive(st, "sphere", args, log);
           }});
  reg.Add({"torus", "tor", "Create a torus solid: TORUS <X,Y[,Z]> <radius> <tube radius>",
           CadCommandKind::Solid, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartSolidPrimitiveCommand(st, "torus", log);
             else
               CadCreateSolidPrimitive(st, "torus", args, log);
           }});
  reg.Add({"presspull", "pp",
           "Move a solid FACE, or turn a closed shape into a solid: PRESSPULL, select a target, then a distance",
           CadCommandKind::PressPull, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartPressPullCommand(st, log);
             else
               CadPressPull(st, args, log);
           }});
  reg.Add({"solidlist", "solids",
           "List every solid: kind, layer, volume, surface area, topology counts",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             CadReportSolids(st, log);
           }});
  reg.Add({"massprop", "massproperties, solidmassprop, massp",
           "Mass properties of every solid: volume, area, centroid, inertia tensor, principal axes (REQ-349)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             CadReportMassProperties(st, log);
           }});
  reg.Add({"section", "",
           "Cross-section of solids by a plane through three points (or the UCS), as a closed polyline",
           CadCommandKind::Section, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartSectionCommand(st, log);
           }});
  reg.Add({"sectionplane", "splane", "Place the section plane on a solid's flat face",
           CadCommandKind::SectionPlane, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartSectionPlaneCommand(st, log);
           }});
  reg.Add({"solidcheck", "scheck",
           "Check every solid (or the selection): closed, manifold, oriented, self-intersecting",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             CadCheckSolids(st, log);
           }});
  reg.Add({"polysolid", "psolid",
           "Sweep a wall along a path: POLYSOLID, then points (A arc, C close, H/W/J, O object)",
           CadCommandKind::Polysolid, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPolysolidCommand(st, log);
           }});
  reg.Add({"isolines", "",
           "Curves drawn around a curved solid face: ISOLINES [0-256], or bare to report",
           CadCommandKind::None, false, true, &RunIsolines});
  reg.Add({"extrude", "ext",
           "Extrude a selected closed polyline or circle into a solid: EXTRUDE <height>",
           CadCommandKind::Extrude, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty())
               StartExtrudeCommand(st, log);
             else
               CadExtrudeSelection(st, args, log);
           }});
  reg.Add({"revolve", "rev",
           "Revolve a selected closed polyline or circle about an axis into a solid",
           CadCommandKind::Revolve, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartRevolveCommand(st, log);
           }});
  reg.Add({"slice", "sl",
           "Cut selected solids with a plane (three points), keeping one side or both",
           CadCommandKind::Slice, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartSliceCommand(st, log);
           }});
  reg.Add({"loft", "lft",
           "Loft a solid through two or more selected closed polylines or circles, in pick order",
           CadCommandKind::Loft, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartLoftCommand(st, log);
           }});
  reg.Add({"sweep", "swp",
           "Sweep a closed profile along a line, arc or open polyline path (T twist, A path-alignment)",
           CadCommandKind::Sweep, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartSweepCommand(st, log);
           }});
  reg.Add({"union", "uni", "Combine two selected solids into one",
           CadCommandKind::Boolean, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartBooleanCommand(st, CadBooleanOp::Union, log);
           }});
  reg.Add({"subtract", "su", "Subtract the second selected solid from the first",
           CadCommandKind::Boolean, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartBooleanCommand(st, CadBooleanOp::Subtract, log);
           }});
  reg.Add({"intersect", "in", "Keep only the volume two selected solids share",
           CadCommandKind::Boolean, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartBooleanCommand(st, CadBooleanOp::Intersect, log);
           }});
}

} // namespace stepengine
