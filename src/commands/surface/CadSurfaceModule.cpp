// ADR-0012 / Issue #1578: Surface, Drainage, and Volume Report sessions & module (Batch 4).

#include "CadCommandsInternal.hpp"
#include "ProjectFiles.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include "surface/CadSurfaceInternal.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace stepengine {

// --- SurfaceElevGradeSession ---

StepDescriptor SurfaceElevGradeSession::currentStep(const AppCommandState&) const {
  if (phase == SurfaceElevPhase::WaitFirst)
    return StepDescriptor::Point("SURFELEV: Pick a point for its surface elevation | ESC cancel");
  return StepDescriptor::Point("SURFELEV: Second point for grade | ESC stops after the elevation");
}

StepSubmitResult SurfaceElevGradeSession::onText(AppCommandState& st,
                                                 const std::string& trimmedLine,
                                                 std::vector<std::string>& log) {
  float px = 0.f;
  float py = 0.f;
  if (phase == SurfaceElevPhase::WaitFirst) {
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
      log.push_back("SURFELEV — type X,Y (World) or pick a point in the drawing.");
      return StepSubmitResult::Handled;
    }
    ReportSurfaceElevationAt(st, px, py, log);
    phase = SurfaceElevPhase::WaitSecond;
    log.push_back("SURFELEV — second point for grade, or ESC to stop here.");
    return StepSubmitResult::Handled;
  }
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, true, static_cast<float>(fromX),
                         static_cast<float>(fromY))) {
    log.push_back("SURFELEV — type X,Y or @dx,dy from the first point.");
    return StepSubmitResult::Handled;
  }
  ReportSurfaceGradeTo(st, px, py, log);
  return StepSubmitResult::Handled;
}

void SurfaceElevGradeSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                             std::vector<std::string>& log) {
  if (phase == SurfaceElevPhase::WaitFirst) {
    ReportSurfaceElevationAt(st, pick.localX, pick.localY, log);
    phase = SurfaceElevPhase::WaitSecond;
    log.push_back("SURFELEV — second point for grade, or ESC to stop here.");
    return;
  }
  ReportSurfaceGradeTo(st, pick.localX, pick.localY, log);
}

void SurfaceElevGradeSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- WaterDropSession ---

StepDescriptor WaterDropSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Point("WATERDROP: Pick a plan position on the surface | ESC cancel");
}

StepSubmitResult WaterDropSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                          std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("WATERDROP — type X,Y (World) or pick a point in the drawing.");
    return StepSubmitResult::Handled;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("WATERDROP — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return StepSubmitResult::Handled;
  }
  RunWaterDropAt(st, static_cast<size_t>(si), px, py, log);
  EndSession(st);
  return StepSubmitResult::Handled;
}

void WaterDropSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                      std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("WATERDROP — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return;
  }
  RunWaterDropAt(st, static_cast<size_t>(si), pick.localX, pick.localY, log);
  EndSession(st);
}

void WaterDropSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- CatchmentSession ---

StepDescriptor CatchmentSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Point("CATCHMENT: Pick an outlet on the surface | ESC cancel");
}

StepSubmitResult CatchmentSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                          std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("CATCHMENT — type X,Y (World) or pick a point in the drawing.");
    return StepSubmitResult::Handled;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("CATCHMENT — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return StepSubmitResult::Handled;
  }
  RunCatchmentAt(st, static_cast<size_t>(si), px, py, log);
  EndSession(st);
  return StepSubmitResult::Handled;
}

void CatchmentSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                      std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("CATCHMENT — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return;
  }
  RunCatchmentAt(st, static_cast<size_t>(si), pick.localX, pick.localY, log);
  EndSession(st);
}

void CatchmentSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- SwapTinEdgeSession ---

StepDescriptor SwapTinEdgeSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Point("SURFSWAPEDGE: Pick an interior TIN edge | ESC cancel");
}

StepSubmitResult SwapTinEdgeSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                            std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("SURFSWAPEDGE — type X,Y (World) or pick a point in the drawing.");
    return StepSubmitResult::Handled;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFSWAPEDGE — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return StepSubmitResult::Handled;
  }
  CommitSurfSwapEdgeLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)],
                          static_cast<double>(px), static_cast<double>(py), log);
  EndSession(st);
  return StepSubmitResult::Handled;
}

void SwapTinEdgeSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                        std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFSWAPEDGE — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return;
  }
  CommitSurfSwapEdgeLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], pick.localX,
                          pick.localY, log);
  EndSession(st);
}

void SwapTinEdgeSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- AddTinPointSession ---

StepDescriptor AddTinPointSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Point("SURFACEADDPOINT: Pick a plan position (Z = work plane) | ESC cancel");
}

StepSubmitResult AddTinPointSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                            std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("SURFACEADDPOINT — type X,Y (World) or pick a point in the drawing.");
    return StepSubmitResult::Handled;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEADDPOINT — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return StepSubmitResult::Handled;
  }
  CommitSurfAddPointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)],
                          static_cast<double>(px), static_cast<double>(py), CadCommitElevation(st),
                          log);
  EndSession(st);
  return StepSubmitResult::Handled;
}

void AddTinPointSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                        std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEADDPOINT — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return;
  }
  CommitSurfAddPointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], pick.localX,
                          pick.localY, CadCommitElevation(st), log);
  EndSession(st);
}

void AddTinPointSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- DelTinPointSession ---

StepDescriptor DelTinPointSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Point("SURFACEDELPOINT: Pick the definition point to remove | ESC cancel");
}

StepSubmitResult DelTinPointSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                            std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("SURFACEDELPOINT — type X,Y (World) or pick a point in the drawing.");
    return StepSubmitResult::Handled;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEDELPOINT — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return StepSubmitResult::Handled;
  }
  CommitSurfDelPointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)],
                          static_cast<double>(px), static_cast<double>(py), log);
  EndSession(st);
  return StepSubmitResult::Handled;
}

void DelTinPointSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                        std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEDELPOINT — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return;
  }
  CommitSurfDelPointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], pick.localX,
                          pick.localY, log);
  EndSession(st);
}

void DelTinPointSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- MoveTinPointSession ---

StepDescriptor MoveTinPointSession::currentStep(const AppCommandState&) const {
  if (phase == MoveTinPointPhase::WaitFrom)
    return StepDescriptor::Point("SURFACEMOVEPOINT: Pick the point to move | ESC cancel");
  return StepDescriptor::Point(
      "SURFACEMOVEPOINT: Pick the new position (Z = work plane) | ESC cancel");
}

StepSubmitResult MoveTinPointSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                             std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("SURFACEMOVEPOINT — type X,Y (World) or pick a point in the drawing.");
    return StepSubmitResult::Handled;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEMOVEPOINT — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return StepSubmitResult::Handled;
  }
  if (phase == MoveTinPointPhase::WaitFrom) {
    fromX = static_cast<double>(px);
    fromY = static_cast<double>(py);
    phase = MoveTinPointPhase::WaitTo;
    log.push_back("SURFACEMOVEPOINT — pick the new position (Z = work plane).");
    return StepSubmitResult::Handled;
  }
  CommitSurfMovePointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], fromX, fromY,
                           static_cast<double>(px), static_cast<double>(py),
                           CadCommitElevation(st), log);
  EndSession(st);
  return StepSubmitResult::Handled;
}

void MoveTinPointSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                         std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFACEMOVEPOINT — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return;
  }
  if (phase == MoveTinPointPhase::WaitFrom) {
    fromX = pick.localX;
    fromY = pick.localY;
    phase = MoveTinPointPhase::WaitTo;
    log.push_back("SURFACEMOVEPOINT — pick the new position (Z = work plane).");
    return;
  }
  CommitSurfMovePointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], fromX, fromY,
                           pick.localX, pick.localY, CadCommitElevation(st), log);
  EndSession(st);
}

void MoveTinPointSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- DelTinLineSession ---

StepDescriptor DelTinLineSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Point("SURFDELLINE: Pick an interior TIN edge | ESC cancel");
}

StepSubmitResult DelTinLineSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                           std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("SURFDELLINE — type X,Y (World) or pick a point in the drawing.");
    return StepSubmitResult::Handled;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFDELLINE — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return StepSubmitResult::Handled;
  }
  CommitSurfDelLineLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)],
                         static_cast<double>(px), static_cast<double>(py), log);
  EndSession(st);
  return StepSubmitResult::Handled;
}

void DelTinLineSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                       std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("SURFDELLINE — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return;
  }
  CommitSurfDelLineLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], pick.localX,
                         pick.localY, log);
  EndSession(st);
}

void DelTinLineSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- QuickProfileSession ---

StepDescriptor QuickProfileSession::currentStep(const AppCommandState&) const {
  if (phase == QuickProfilePhase::WaitFirst)
    return StepDescriptor::Point("QUICKPROFILE: First point of the sample line | ESC cancel");
  return StepDescriptor::Point("QUICKPROFILE: Second point of the sample line | ESC cancel");
}

StepSubmitResult QuickProfileSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                             std::vector<std::string>& log) {
  float px = 0.f, py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("QUICKPROFILE — type X,Y (World) or pick a point in the drawing.");
    return StepSubmitResult::Handled;
  }
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("QUICKPROFILE — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return StepSubmitResult::Handled;
  }
  if (phase == QuickProfilePhase::WaitFirst) {
    fromX = static_cast<double>(px);
    fromY = static_cast<double>(py);
    phase = QuickProfilePhase::WaitSecond;
    log.push_back("QUICKPROFILE — second point of the sample line.");
    return StepSubmitResult::Handled;
  }
  CommitQuickProfileLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], fromX, fromY,
                          static_cast<double>(px), static_cast<double>(py), log);
  EndSession(st);
  return StepSubmitResult::Handled;
}

void QuickProfileSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                         std::vector<std::string>& log) {
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("QUICKPROFILE — no surface named \"" + surfaceName + "\".");
    EndSession(st);
    return;
  }
  if (phase == QuickProfilePhase::WaitFirst) {
    fromX = pick.localX;
    fromY = pick.localY;
    phase = QuickProfilePhase::WaitSecond;
    log.push_back("QUICKPROFILE — second point of the sample line.");
    return;
  }
  CommitQuickProfileLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], fromX, fromY,
                          pick.localX, pick.localY, log);
  EndSession(st);
}

void QuickProfileSession::onCancel(AppCommandState&, std::vector<std::string>&) {}

// --- DesignateBreaklineSession ---

StepDescriptor DesignateBreaklineSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("DESIGNATEBREAKLINE: Pick a line or polyline | ESC cancel",
                                StepClickRoute::RawEntityPick, BlankEnterPolicy::Custom);
}

StepSubmitResult DesignateBreaklineSession::onText(AppCommandState&, const std::string&,
                                                   std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

void DesignateBreaklineSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                               std::vector<std::string>& log) {
  CommitDesignateAt(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), false,
                    log);
}

void DesignateBreaklineSession::onCancel(AppCommandState& st, std::vector<std::string>&) {
  st.designateBreaklineDescription.clear();
}

// --- DesignateBoundarySession ---

StepDescriptor DesignateBoundarySession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("DESIGNATEBOUNDARY: Pick a CLOSED polyline | ESC cancel",
                                StepClickRoute::RawEntityPick, BlankEnterPolicy::Custom);
}

StepSubmitResult DesignateBoundarySession::onText(AppCommandState&, const std::string&,
                                                  std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

void DesignateBoundarySession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                              std::vector<std::string>& log) {
  CommitDesignateAt(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY), true,
                    log);
}

void DesignateBoundarySession::onCancel(AppCommandState& st, std::vector<std::string>&) {
  st.designateBoundaryName.clear();
}

// --- Registration ---

void RegisterSurfaceCommands(CommandRegistry& reg) {
  reg.Add({"surfelev", "se", "Surface elevation at a point; grade between two",
           CadCommandKind::SurfaceElevGrade, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartSurfaceElevGradeCommand(st, log);
           }});
  reg.Add({"designatebreakline", "dbl", "Add a picked line/polyline as a surface breakline",
           CadCommandKind::DesignateBreakline, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty()) {
               log.push_back("DESIGNATEBREAKLINE — usage: DESIGNATEBREAKLINE <surface name>.");
               return;
             }
             StartDesignateBreaklineCommand(st, args, log);
           }});
  reg.Add({"designatecontour", "dcon", "Add a picked line/polyline as a surface contour source",
           CadCommandKind::DesignateBreakline, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty()) {
               log.push_back("DESIGNATECONTOUR — usage: DESIGNATECONTOUR <surface name>.");
               return;
             }
             StartDesignateContourCommand(st, args, log);
           }});
  reg.Add({"designateboundary", "dbd",
           "Add a picked closed polyline as a surface boundary (outer/hide/show/clip)",
           CadCommandKind::DesignateBoundary, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             const size_t sp = args.find_last_of(" \t");
             const bool usageError = args.empty() || sp == std::string::npos;
             const std::string name =
                 usageError ? std::string() : StringUtil::trimCopy(args.substr(0, sp));
             const std::string kindWord =
                 usageError ? std::string()
                            : StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(args.substr(sp + 1)));
             CadBoundaryKind kind = CadBoundaryKind::Outer;
             const bool kindOk = kindWord == "outer" || kindWord == "hide" || kindWord == "show" ||
                                 kindWord == "clip" || kindWord == "mask";
             if (kindWord == "hide")
               kind = CadBoundaryKind::Hide;
             else if (kindWord == "show")
               kind = CadBoundaryKind::Show;
             else if (kindWord == "clip")
               kind = CadBoundaryKind::Clip;
             else if (kindWord == "mask")
               kind = CadBoundaryKind::Mask;
             if (usageError || name.empty() || !kindOk) {
               log.push_back(
                   "DESIGNATEBOUNDARY — usage: DESIGNATEBOUNDARY <surface name> <OUTER|HIDE|SHOW|CLIP|MASK>.");
               return;
             }
             StartDesignateBoundaryCommand(st, name, kind, log);
           }});
  reg.Add({"surfacecreate", "sfcreate",
           "Create a surface from point groups: SURFACECREATE <name>, <group>[, <group>…]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceCreate(st, args, log);
           }});
  reg.Add({"surfacecreategrid", "sfgrid",
           "Create a grid surface: SURFACECREATEGRID <name>, ox, oy, sx, sy, cols, rows[, z…]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceCreateGrid(st, args, log);
           }});
  reg.Add({"surfacecreatecorr", "sfcorr", "Create a corridor surface: SURFACECREATECORR <name>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceCreateCorr(st, args, log);
           }});
  reg.Add({"surfacecreatevolgrid", "sfvolgrid",
           "Grid volume surface: SURFACECREATEVOLGRID <name>, <base>, <comparison>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceCreateVolGrid(st, args, log);
           }});
  reg.Add({"surfswapedge", "sfswap",
           "Swap a TIN interior edge: SURFSWAPEDGE <surface>[, <x>, <y>]",
           CadCommandKind::SwapTinEdge, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfSwapEdge(st, args, log);
           }});
  reg.Add({"surfaceaddpoint", "sfaddpt",
           "Add a TIN definition point: SURFACEADDPOINT <surface>[, <x>, <y>, <z>]",
           CadCommandKind::AddTinPoint, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfAddPoint(st, args, log);
           }});
  reg.Add({"surfacemovepoint", "sfmovept",
           "Move a TIN definition point: SURFACEMOVEPOINT <surface>[, <x1>, <y1>, <x2>, <y2>, <z2>]",
           CadCommandKind::MoveTinPoint, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfMovePoint(st, args, log);
           }});
  reg.Add({"surfdelline", "sfdelline",
           "Delete an interior TIN edge: SURFDELLINE <surface>[, <x>, <y>]",
           CadCommandKind::DelTinLine, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfDelLine(st, args, log);
           }});
  reg.Add({"surfacedelpoint", "sfdelpt",
           "Delete nearest TIN definition point: SURFACEDELPOINT <surface>[, <x>, <y>]",
           CadCommandKind::DelTinPoint, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfDelPoint(st, args, log);
           }});
  reg.Add({"surflodlow", "", "Set a surface to Reduced viewport detail: SURFLODLOW <surface>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceLodCommand(st, args, true, log);
           }});
  reg.Add({"surflodhigh", "", "Set a surface to full viewport detail: SURFLODHIGH <surface>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceLodCommand(st, args, false, log);
           }});
  reg.Add({"movetosurface", "", "Move selected block references to a surface: MOVETOSURFACE <surface>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunMoveToSurfaceCommand(st, args, log);
           }});
  reg.Add({"imageattach", "",
           "Attach a readable model-space image: IMAGEATTACH <path>, x, y, width, height, rotation-degrees",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             const std::vector<std::string> fields = SplitCommaFields(args);
             auto number = [&](size_t i, float* out) {
               if (i >= fields.size()) return false;
               char* end = nullptr;
               const double value = std::strtod(fields[i].c_str(), &end);
               if (!end || *end != '\0' || !std::isfinite(value) || std::abs(value) > 1.e30)
                 return false;
               *out = static_cast<float>(value);
               return true;
             };
             float x = 0.f, y = 0.f, width = 0.f, height = 0.f, rotation = 0.f;
             if (fields.size() != 6 || fields[0].empty() || !number(1, &x) || !number(2, &y) ||
                 !number(3, &width) || !number(4, &height) || !number(5, &rotation))
               log.push_back(
                   "IMAGEATTACH — usage: IMAGEATTACH <path>, x, y, width, height, rotation-degrees.");
             else
               AttachModelRasterImage(
                   st, fields[0], x, y, width, height,
                   rotation * static_cast<float>(3.14159265358979323846 / 180.0), log);
           }});
  reg.Add({"surfdrape", "", "Drape an image onto a surface: SURFDRAPE <surface>, <image-id>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             const std::vector<std::string> fields = SplitCommaFields(args);
             char* end = nullptr;
             const unsigned long long id =
                 fields.size() == 2 ? std::strtoull(fields[1].c_str(), &end, 10) : 0;
             const int surface = fields.size() == 2 ? FindSurfaceIndex(st, fields[0]) : -1;
             if (surface < 0 || !end || *end != '\0' || id == 0)
               log.push_back("SURFDRAPE — usage: SURFDRAPE <surface>, <image-id>.");
             else {
               const auto image = std::find_if(
                   st.drawing.cadRasterImages.begin(), st.drawing.cadRasterImages.end(),
                   [&](const CadRasterImage& candidate) {
                     return candidate.id == static_cast<std::uint64_t>(id);
                   });
               if (image == st.drawing.cadRasterImages.end())
                 log.push_back("SURFDRAPE — no attached image has ID " + std::to_string(id) + ".");
               else
                 SetModelRasterDrape(st, static_cast<size_t>(image - st.drawing.cadRasterImages.begin()),
                                     static_cast<size_t>(surface), log);
             }
           }});
  reg.Add({"createdatashortcut", "",
           "Register a project surface source: CREATEDATASHORTCUT <surface>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             const int source = FindSurfaceIndex(st, args);
             if (source < 0)
               log.push_back("CREATEDATASHORTCUT — name an existing source surface.");
             else
               CreateSurfaceDataShortcut(st, static_cast<size_t>(source), log);
           }});
  reg.Add({"createsurfacereference", "",
           "Create a read-only project surface reference: CREATESURFACEREFERENCE <source-drawing-rel>, <source-surface-id>, <name>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             const std::vector<std::string> fields = SplitCommaFields(args);
             char* end = nullptr;
             const unsigned long long id =
                 fields.size() == 3 ? std::strtoull(fields[1].c_str(), &end, 10) : 0;
             if (!id || !end || *end != '\0' || fields[0].empty() || fields[2].empty())
               log.push_back(
                   "CREATESURFACEREFERENCE — usage: CREATESURFACEREFERENCE <source-drawing-rel>, <source-surface-id>, <name>.");
             else
               CreateSurfaceReference(st, fields[0], static_cast<std::uint64_t>(id), fields[2], log);
           }});
  reg.Add({"surfacevisibility", "surfvis",
           "Surface LOS or ZVI analysis: SURFACEVISIBILITY <surface>, LOS|ZVI, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceVisibility(st, args, log);
           }});
  reg.Add({"surfcrossbl", "",
           "List or resolve surface-local breakline crossings: SURFCROSSBL <surface>, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceCrossings(st, args, log);
           }});
  reg.Add({"surflabel", "",
           "Place spot, slope, contour or watershed labels on a surface: SURFLABEL <surface>, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceLabel(st, args, log);
           }});
  reg.Add({"surfacelegend", "",
           "Insert a surface analysis legend table: SURFACELEGEND <surface>, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceLegend(st, args, log);
           }});
  reg.Add({"createprofilefromsurface", "",
           "Sample a surface profile along an alignment: CREATEPROFILEFROMSURFACE ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunCreateSurfaceProfile(st, args, log);
           }});
  reg.Add({"profileview", "", "Insert a surface profile view: PROFILEVIEW ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceProfileView(st, args, log);
           }});
  reg.Add({"quickprofile", "qprof",
           "Quick profile along two points: QUICKPROFILE <surface>[, <x1>, <y1>, <x2>, <y2>]",
           CadCommandKind::QuickProfile, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunQuickProfile(st, args, log);
           }});
  reg.Add({"volumesurface", "volsurf",
           "Create a TIN volume surface: VOLUMESURFACE <name>, <base>, <comparison>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunVolumeSurfaceCreate(st, args, log);
           }});
  reg.Add({"surfacerename", "sfrename", "Rename a surface: SURFACERENAME <old>, <new>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceRename(st, args, log);
           }});
  reg.Add({"surfacedelete", "sfdelete", "Delete a surface: SURFACEDELETE <name>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceDelete(st, args, log);
           }});
  reg.Add({"surfacerebuild", "sfrebuild",
           "Rebuild a surface now (all surfaces if no name): SURFACEREBUILD [<name>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceRebuild(st, args, log);
           }});
  reg.Add({"surfacelist", "sflist", "List every surface and its full definition",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ReportSurfaces(st, log);
           }});
  reg.Add({"surfacestats", "sfstats", "Surface statistics: SURFACESTATS [<name>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceStats(st, args, log);
           }});
  reg.Add({"watershed", "wshed", "Watershed basins: WATERSHED [<name>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunWatershed(st, args, log);
           }});
  reg.Add({"waterdrop", "wdrop",
           "Water-drop path: WATERDROP <name>[, <x>, <y>] | WATERDROP EXTRACT",
           CadCommandKind::WaterDrop, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunWaterDropCommand(st, args, log);
           }});
  reg.Add({"catchment", "catch",
           "Catchment at an outlet: CATCHMENT <name>[, <x>, <y>] | CATCHMENT EXTRACT",
           CadCommandKind::Catchment, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunCatchmentCommand(st, args, log);
           }});
  reg.Add({"undesignate", "undes",
           "Remove one definition item: UNDESIGNATE <surface>, <BREAKLINE|BOUNDARY|POINTFILE|CONTOUR>, <n>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunUndesignate(st, args, log);
           }});
  reg.Add({"surfaceaddfile", "sfaddfile",
           "Link a point file into a surface: SURFACEADDFILE <surface>, <path>[, <layout>[, HEADER]]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceAddFile(st, args, log);
           }});
  reg.Add({"surfaceimportfile", "sfimportfile",
           "Import a linked point file into the drawing and break the link",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunSurfaceImportFile(st, args, log);
           }});
  reg.Add({"surfstyle", "ss", "Surface style editor: contours, triangles, border (REQ-070)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteSurfStyleCommand(st, args, log);
           }});
  reg.Add({"extract", "",
           "Bake a surface's displayed contours into polylines: EXTRACT <surface>[, <layer>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteExtractCommand(st, args, log);
           }});
  reg.Add({"drape", "", "Drape selected geometry onto a surface: DRAPE <surface>[, LINK]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteDrapeCommand(st, args, log);
           }});
  reg.Add({"drapelinks", "", "List entities linked to follow surface rebuilds",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExecuteDrapeLinksCommand(st, log);
           }});
  reg.Add({"volumes", "vol",
           "Cut/fill/net volume between two surfaces: VOLUMES <base>, <comparison>[, <clip id>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteVolumesCommand(st, args, log);
           }});
  reg.Add({"voldash", "",
           "Volume Dashboard: live cut/fill/net panel between two surfaces (REQ-073)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteVolDashCommand(st, args, log);
           }});
  reg.Add({"volreport", "", "Insert MTEXT of the last volume report: VOLREPORT [TABLE]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunVolReport(st, args, log);
           }});
  reg.Add({"voltable", "", "Insert a TABLE of the last volume report: VOLTABLE",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             RunVolReport(st, "table", log);
           }});
  reg.Add({"volcsv", "", "Write CSV of volume results: VOLCSV [<path>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunVolCsv(st, args, log);
           }});
}

} // namespace stepengine
