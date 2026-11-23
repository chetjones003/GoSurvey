// ADR-0012 / Issue #1578: UCS, PLAN, ELEV, SECTIONCLIP, and View command module.

#include "ucs/CadUcsViewSessions.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>

namespace stepengine {

// ---------------------------------------------------------------------------
// UcsSession
// ---------------------------------------------------------------------------

StepDescriptor UcsSession::currentStep(const AppCommandState&) const {
  using UPh = UcsPhase;
  switch (phase) {
  case UPh::WaitOriginOrOption:
  case UPh::WaitRotationAngleP1:
  case UPh::WaitZAxisOrigin:
  case UPh::WaitZAxisPoint:
    return StepDescriptor::Point("", StepDynInputSpec{}, BlankEnterPolicy::Custom);
  case UPh::WaitXAxisPoint:
  case UPh::WaitXyPoint: {
    StepDynInputSpec dyn;
    dyn.pointEntry = true;
    dyn.ucsPolar = true;
    dyn.ucsPolarBase = pendingOrigin;
    return StepDescriptor::Point("", dyn, BlankEnterPolicy::Custom);
  }
  case UPh::WaitRotationAngleP2: {
    StepDynInputSpec dyn;
    dyn.pointEntry = true;
    dyn.ucsPolar = true;
    dyn.ucsPolarBase = angleBasePoint;
    return StepDescriptor::Point("", dyn, BlankEnterPolicy::Custom);
  }
  case UPh::WaitObjectPick:
    return StepDescriptor::EntityPick("", BlankEnterPolicy::Custom);
  case UPh::Idle:
  case UPh::WaitRotationAngle:
  case UPh::WaitNamedName:
    return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Custom);
  }
  return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Custom);
}

StepSubmitResult UcsSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                    std::vector<std::string>& log) {
  ProcessUcsCommandLine(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult UcsSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  ProcessUcsCommandLine(st, std::string(), log);
  return StepSubmitResult::Handled;
}

void UcsSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                std::vector<std::string>& log) {
  double ucsWx = 0.0;
  double ucsWy = 0.0;
  CadCoord::WorldFromLocal(st, static_cast<float>(pick.localX), static_cast<float>(pick.localY),
                           &ucsWx, &ucsWy);
  ProcessUcsViewportPick(st, {ucsWx, ucsWy, static_cast<double>(CadCommitElevation(st))}, log,
                         pick.pickRay);
}

void UcsSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("UCS canceled - the coordinate system is unchanged.");
}

// ---------------------------------------------------------------------------
// PlanSession
// ---------------------------------------------------------------------------

StepDescriptor PlanSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::KeywordOptions("", BlankEnterPolicy::Custom, StepClickRoute::Ignore);
}

StepSubmitResult PlanSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  ProcessPlanCommandLine(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult PlanSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  ProcessPlanCommandLine(st, std::string(), log);
  return StepSubmitResult::Handled;
}

void PlanSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("PLAN canceled - the view is unchanged.");
}

// ---------------------------------------------------------------------------
// ElevSession
// ---------------------------------------------------------------------------

StepDescriptor ElevSession::currentStep(const AppCommandState& st) const {
  char buf[128];
  std::snprintf(buf, sizeof(buf), "ELEV: New default elevation <%.4f> — [W]orld = Z 0 | ESC cancel",
                static_cast<double>(CadWorkPlaneElevation(st)));
  return StepDescriptor::Custom(buf, StepClickRoute::Ignore, BlankEnterPolicy::Custom);
}

StepSubmitResult ElevSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "Elevation unchanged (%.4f).",
                static_cast<double>(CadWorkPlaneElevation(st)));
  log.push_back(buf);
  return StepSubmitResult::Finished;
}

StepSubmitResult ElevSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                     std::vector<std::string>& log) {
  const std::string evLow = StringUtil::toLowerAsciiCopy(trimmedLine);
  if (evLow == "w" || evLow == "world") {
    ApplyUcsWorld(st, log);
    return StepSubmitResult::Finished;
  }
  double ez = 0.0;
  std::istringstream evIss(trimmedLine);
  if (!(evIss >> ez) || !(evIss >> std::ws).eof()) {
    log.push_back("ELEV — enter an elevation, or W for world (blank Enter keeps the current value).");
    return StepSubmitResult::Handled;
  }
  return ApplyElevValue(st, ez, log) ? StepSubmitResult::Finished : StepSubmitResult::Handled;
}

void ElevSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("Elevation unchanged.");
}

// ---------------------------------------------------------------------------
// SectionClipSession
// ---------------------------------------------------------------------------

StepDescriptor SectionClipSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::KeywordOptions("", BlankEnterPolicy::Custom, StepClickRoute::Ignore);
}

StepSubmitResult SectionClipSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back(SectionClipReport(st));
  return StepSubmitResult::Finished;
}

StepSubmitResult SectionClipSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                            std::vector<std::string>& log) {
  return ApplySectionClipValue(st, trimmedLine, log) ? StepSubmitResult::Finished
                                                     : StepSubmitResult::Handled;
}

void SectionClipSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back("SECTIONCLIP canceled. " + SectionClipReport(st));
}

// ---------------------------------------------------------------------------
// ZoomSession
// ---------------------------------------------------------------------------

StepDescriptor ZoomSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::SelectionBox(
      "ZOOM WINDOW: Two corners (unsnapped) — rubber previews fit area | ESC cancel",
      BlankEnterPolicy::Custom, /*includeSurvey=*/false, /*zoomRegion=*/true);
}

StepSubmitResult ZoomSession::onText(AppCommandState&, const std::string&,
                                     std::vector<std::string>& log) {
  log.push_back("ZOOM WINDOW — finish two clicks in the viewport, or ESC to cancel.");
  return StepSubmitResult::Handled;
}

void ZoomSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                 std::vector<std::string>&) {
  if (!st.selBoxWaitingSecond)
    return;
  st.pendingZoomMnX = static_cast<float>(std::min<double>(st.selBoxAnchorX, pick.localX));
  st.pendingZoomMxX = static_cast<float>(std::max<double>(st.selBoxAnchorX, pick.localX));
  st.pendingZoomMnY = static_cast<float>(std::min<double>(st.selBoxAnchorY, pick.localY));
  st.pendingZoomMxY = static_cast<float>(std::max<double>(st.selBoxAnchorY, pick.localY));
  st.selBoxWaitingSecond = false;
  st.pendingZoomWindow = true;
  EndSession(st);
}

void ZoomSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  log.push_back("ZOOM WINDOW canceled.");
}

// ---------------------------------------------------------------------------
// PanSession
// ---------------------------------------------------------------------------

StepDescriptor PanSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Finish);
}

StepSubmitResult PanSession::onText(AppCommandState&, const std::string&,
                                    std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

StepSubmitResult PanSession::onBlankEnter(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("PAN — exited.");
  return StepSubmitResult::Finished;
}

void PanSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("PAN — exited.");
}

// ---------------------------------------------------------------------------
// OrbitSession
// ---------------------------------------------------------------------------

StepDescriptor OrbitSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Finish);
}

StepSubmitResult OrbitSession::onText(AppCommandState&, const std::string&,
                                      std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

StepSubmitResult OrbitSession::onBlankEnter(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("ORBIT — exited.");
  return StepSubmitResult::Finished;
}

void OrbitSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("ORBIT — exited.");
}

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------

void RegisterUcsViewCommands(CommandRegistry& reg) {
  reg.Add({"ucs", "",
           "Define the User Coordinate System (N names one; restore/delete in the View Manager)",
           CadCommandKind::Ucs, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             StartUcsCommand(st, log);
             if (!args.empty())
               ProcessUcsCommandLine(st, args, log);
           }});

  reg.Add({"plan", "",
           "View the XY plane of a coordinate system (does not change the UCS)",
           CadCommandKind::Plan, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             StartPlanCommand(st, log);
             if (!args.empty())
               ProcessPlanCommandLine(st, args, log);
           }});

  reg.Add({"elev", "",
           "Elevation new geometry is drawn at (W = world Z 0)",
           CadCommandKind::Elev, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::istringstream iss(args);
             std::string evArg;
             if (iss >> evArg) {
               const std::string evLow = StringUtil::toLowerAsciiCopy(evArg);
               if (evLow == "w" || evLow == "world") {
                 ApplyUcsWorld(st, log);
                 return;
               }
               try {
                 size_t used = 0;
                 const double ez = std::stod(evArg, &used);
                 if (used == evArg.size()) {
                   ApplyElevValue(st, ez, log);
                   return;
                 }
               } catch (...) {
               }
               log.push_back("ELEV — usage: ELEV <elevation>, or ELEV W for world.");
               return;
             }
             StartElevCommand(st, log);
           }});

  reg.Add({"ucsfollow", "",
           "0 = changing the UCS leaves the view alone; 1 = it switches to a plan view",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::istringstream iss(args);
             std::string fArg;
             if (!(iss >> fArg)) {
               log.push_back(
                   "UCSFOLLOW = " + std::to_string(st.ucsFollow ? 1 : 0) +
                   "  (0 = changing the UCS leaves the view alone, 1 = it switches to a plan view "
                   "of the new UCS)");
               return;
             }
             if (fArg == "0" || fArg == "1") {
               st.ucsFollow = (fArg == "1");
               log.push_back(st.ucsFollow
                                 ? "UCSFOLLOW = 1 - changing the UCS now switches to a plan view of it."
                                 : "UCSFOLLOW = 0 - changing the UCS leaves the view alone.");
             } else {
               log.push_back("UCSFOLLOW — enter 0 or 1.");
             }
           }});

  reg.Add({"view", "v, ddview",
           "Named views: VIEW [Save/Restore/Delete/?] <name>, or VIEW alone for the View Manager",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ProcessViewCommandLine(st, args, log);
           }});

  reg.Add({"sectionclip", "sclip, clip",
           "Live section clip by the UCS plane: ON / OFF / FLIP / <offset>",
           CadCommandKind::SectionClip, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (!args.empty())
               ApplySectionClipValue(st, args, log);
             else
               StartSectionClipCommand(st, log);
           }});

  reg.Add({"visualstyle", "vs, vscurrent",
           "Viewport visual style: 2D / HIDDEN / SHADED",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::istringstream iss(args);
             std::string vsArg;
             if (iss >> vsArg) {
               ApplyVisualStyleValue(st, vsArg, log);
             } else {
               log.push_back(std::string("Visual style = ") +
                             VisualStyleName(st.viewportVisualStyle) +
                             ". Usage: VS 2D | HIDDEN | SHADED.");
             }
           }});

  reg.Add({"perspective", "projection, persp",
           "View projection: ON (perspective) / OFF (orthographic)",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::istringstream iss(args);
             std::string projArg;
             if (iss >> projArg) {
               ApplyProjectionValue(st, projArg, log);
             } else {
               log.push_back(std::string("Projection = ") +
                             ProjectionName(st.viewportProjection) +
                             ". Usage: PERSPECTIVE ON | OFF.");
             }
           }});

  reg.Add({"fov", "lens",
           "Perspective field of view, in degrees",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::istringstream iss(args);
             std::string fovArg;
             if (iss >> fovArg) {
               ApplyFovValue(st, fovArg, log);
             } else {
               char buf[160];
               std::snprintf(buf, sizeof(buf), "FOV = %.4g degrees. Usage: FOV <%.0f-%.0f>.",
                             static_cast<double>(st.viewportFovDeg),
                             static_cast<double>(kMinFovDeg), static_cast<double>(kMaxFovDeg));
               log.push_back(buf);
             }
           }});

  reg.Add({"crosshair3d", "cursor3d, xhair3d",
           "3D crosshair cursor showing the UCS axes: ON / OFF",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::istringstream iss(args);
             std::string chArg;
             if (iss >> chArg) {
               ApplyCrosshair3dValue(st, chArg, log);
             } else {
               log.push_back(std::string("3D crosshair = ") +
                             (st.viewportCrosshair3d ? "ON" : "OFF") +
                             ". Usage: CROSSHAIR3D ON | OFF.");
             }
           }});

  reg.Add({"zoomextents", "ze", "Zoom to drawing extents",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartZoomExtentsCommand(st, log);
           }});

  reg.Add({"zoomwindow", "zw", "Zoom to a window",
           CadCommandKind::Zoom, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartZoomWindowCommand(st, log);
           }});

  reg.Add({"pan", "p", "Pan the view (drag with the left mouse button)",
           CadCommandKind::Pan, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPanCommand(st, log);
           }});

  reg.Add({"orbit", "3dorbit, 3do", "Free orbit the model view (drag with the left mouse button)",
           CadCommandKind::Orbit, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartOrbitCommand(st, log);
           }});

  reg.Add({"regen", "re", "Regenerate the drawing",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             BumpCadGpuCache(st);
             log.push_back("REGEN — viewport caches refreshed.");
           }});
}

} // namespace stepengine
