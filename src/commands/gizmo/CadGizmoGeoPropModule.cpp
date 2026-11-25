// ADR-0012 / Issue #1578: 3D Gizmo, Property, Selection, and Geolocation command module.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "gizmo/CadGizmoGeoPropSessions.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <sstream>
#include <string>

namespace stepengine {

// ---------------------------------------------------------------------------
// GizmoCmdSession (3DMOVE / 3DROTATE / 3DSCALE)
// ---------------------------------------------------------------------------

StepDescriptor GizmoCmdSession::currentStep(const AppCommandState&) const {
  using P = GizmoCmdPhase;
  switch (phase) {
  case P::SelectObjects:
    return StepDescriptor::SelectObjects("", BlankEnterPolicy::Custom, /*includeSurvey=*/false);
  case P::BasePoint:
    return StepDescriptor::Point("", StepDynInputSpec{}, BlankEnterPolicy::Custom);
  case P::Handles:
    return StepDescriptor::Custom("", StepClickRoute::GizmoHandlePick, BlankEnterPolicy::Custom);
  }
  return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Custom);
}

StepSubmitResult GizmoCmdSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                         std::vector<std::string>& log) {
  (void)HandleGizmoCommandTextInput(trimmedLine, st, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult GizmoCmdSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandleGizmoCommandTextInput(std::string(), st, log);
  return StepSubmitResult::Handled;
}

void GizmoCmdSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                     std::vector<std::string>& log) {
  if (phase == GizmoCmdPhase::BasePoint) {
    SubmitGizmoBasePoint(st, pick.localX, pick.localY, CadCommitElevation(st), log);
    return;
  }
  if (st.selBoxWaitingSecond)
    FinishSelectionBox(st, pick, /*includeSurvey=*/false, log);
}

void GizmoCmdSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back(std::string(AppCommandState::KindName(kind_)) + " canceled.");
  CancelGizmoDrag(st);
  st.gizmoOp = opBeforeCmd;
  st.gizmoHoverAxis = -1;
}

void GizmoCmdSession::onReset(AppCommandState& st) {
  CancelGizmoDrag(st);
  st.gizmoOp = opBeforeCmd;
  st.gizmoHoverAxis = -1;
}

// ---------------------------------------------------------------------------
// PropCmdSession (CHPROP / MATCHPROP / LAYMCUR)
// ---------------------------------------------------------------------------

StepDescriptor PropCmdSession::currentStep(const AppCommandState&) const {
  if (kind_ == CadCommandKind::ChProp && phase != PropCmdPhase::SelectObjects) {
    return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Custom);
  }
  return StepDescriptor::SelectObjects("", BlankEnterPolicy::Custom, /*includeSurvey=*/false);
}

StepSubmitResult PropCmdSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                        std::vector<std::string>& log) {
  (void)HandlePropCommandTextInput(trimmedLine, st, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult PropCmdSession::onBlankEnter(AppCommandState& st, std::vector<std::string>& log) {
  (void)HandlePropCommandTextInput(std::string(), st, log);
  return StepSubmitResult::Handled;
}

void PropCmdSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                    std::vector<std::string>& log) {
  if (st.selBoxWaitingSecond) {
    FinishSelectionBox(st, pick, /*includeSurvey=*/false, log);
    CadPropCommandSelectionChanged(st, log);
  }
}

void PropCmdSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  log.push_back(std::string(AppCommandState::KindName(kind_)) + " canceled.");
  EndPropCommand(st);
}

void PropCmdSession::onReset(AppCommandState& st) {
  if (IsPropCommandKind(st.active))
    EndPropCommand(st);
}

// ---------------------------------------------------------------------------
// GeoCmdSession
// ---------------------------------------------------------------------------

StepDescriptor GeoCmdSession::currentStep(const AppCommandState&) const {
  using K = CadCommandKind;
  using GP = GeoCmdPhase;
  switch (kind_) {
  case K::GeoMarkPoint:
    return StepDescriptor::Point("GEOMARKPOINT: Pick the position or type X,Y | ESC cancel",
                                 StepDynInputSpec{}, BlankEnterPolicy::Custom);
  case K::GeoMarkLatLong: {
    const char* hint =
        phase == GP::WaitFirst
            ? "GEOMARKLATLONG: Latitude — decimal degrees or D M S N/S (lat,long in one go) | ESC cancel"
            : "GEOMARKLATLONG: Longitude — decimal degrees (west negative) or D M S E/W | ESC cancel";
    return StepDescriptor::Text(hint, BlankEnterPolicy::Custom);
  }
  case K::DrawingSettingsPick:
    return StepDescriptor::Point(
        "DRAWINGSETTINGS: Pick for the Transformation tab or type X,Y | ESC back to the window",
        StepDynInputSpec{}, BlankEnterPolicy::Custom);
  case K::GeoReorientMarker: {
    const char* hint =
        phase == GP::WaitFirst
            ? "GEOREORIENTMARKER: Pick the design point or type X,Y | ESC cancel"
            : "GEOREORIENTMARKER: Pick a point to the north of the design point | ESC cancel";
    return StepDescriptor::Point(hint, StepDynInputSpec{}, BlankEnterPolicy::Custom);
  }
  case K::GeoCaptureArea: {
    if (phase == GP::Capturing) {
      return StepDescriptor::Custom(
          "Capture Area: gathering the map tiles | ESC cancel (nothing is kept)",
          StepClickRoute::SnappedPointPick, BlankEnterPolicy::Custom);
    }
    const char* hint = phase == GP::WaitFirst
                           ? "Capture Area: Pick the first corner or type X,Y | ESC cancel"
                           : "Capture Area: Pick the opposite corner or type X,Y | ESC cancel";
    return StepDescriptor::Point(hint, StepDynInputSpec{}, BlankEnterPolicy::Custom);
  }
  default:
    return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Custom);
  }
}

StepSubmitResult GeoCmdSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                       std::vector<std::string>& log) {
  (void)HandleGeoCommandText(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

void GeoCmdSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                   std::vector<std::string>& log) {
  if (kind_ == CadCommandKind::GeoMarkLatLong)
    return;
  SubmitGeoCommandPoint(st, pick.localX, pick.localY, log);
}

void GeoCmdSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back(std::string(AppCommandState::KindName(kind_)) + " canceled.");
}

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------

void ExecuteGizmoSettingCommand(AppCommandState& st, const std::string& args,
                                std::vector<std::string>& log) {
  if (IsGizmoCommandKind(st.active)) {
    log.push_back(std::string("GIZMO — finish or cancel ") + AppCommandState::KindName(st.active) +
                  " first.");
    return;
  }
  std::istringstream gz(StringUtil::toLowerAsciiCopy(args));
  std::string mode;
  const bool haveMode = static_cast<bool>(gz >> mode);
  const auto opName = [](CadGizmoOp o) {
    return o == CadGizmoOp::Translate ? "MOVE" : o == CadGizmoOp::Rotate ? "ROTATE" : "SCALE";
  };
  if (!haveMode) {
    log.push_back(std::string("GIZMO — currently ") +
                  (st.gizmoPersistent ? opName(st.gizmoOp) : "OFF") +
                  ". Use GIZMO MOVE, GIZMO ROTATE or GIZMO SCALE to show it on every "
                  "selection, GIZMO OFF to hide it, or 3DMOVE / 3DROTATE / 3DSCALE.");
    return;
  }
  if (mode == "off" || mode == "0" || mode == "no") {
    CancelGizmoDrag(st);
    st.gizmoPersistent = false;
    st.gizmoHoverAxis = -1;
    log.push_back("GIZMO — OFF. Selecting shows no gizmo; use 3DMOVE, 3DROTATE or 3DSCALE.");
    return;
  }
  CadGizmoOp want = st.gizmoOp;
  if (mode == "move" || mode == "m" || mode == "translate")
    want = CadGizmoOp::Translate;
  else if (mode == "rotate" || mode == "ro" || mode == "r")
    want = CadGizmoOp::Rotate;
  else if (mode == "scale" || mode == "sc" || mode == "s")
    want = CadGizmoOp::Scale;
  else {
    log.push_back("GIZMO — unknown mode '" + mode + "'. Use MOVE, ROTATE, SCALE or OFF.");
    return;
  }
  CancelGizmoDrag(st);
  st.gizmoOp = want;
  st.gizmoPersistent = true;
  st.gizmoHoverAxis = -1;
  log.push_back(std::string("GIZMO — ") + opName(want) + ".");
}

void RegisterGizmoGeoPropCommands(CommandRegistry& reg) {
  reg.Add({"gizmo", "",
           "Show the 3D gizmo on every selection: GIZMO MOVE | ROTATE | SCALE | OFF",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteGizmoSettingCommand(st, args, log);
           }});

  reg.Add({"3dmove", "",
           "Move objects with the 3D gizmo (axis handles or a typed distance)",
           CadCommandKind::Move3d, /*repeatable=*/true, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartGizmoCommand(st, AppCommandState::Kind::Move3d, log);
           }});

  reg.Add({"3drotate", "",
           "Rotate objects with the 3D gizmo (ring or a typed angle)",
           CadCommandKind::Rotate3d, /*repeatable=*/true, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartGizmoCommand(st, AppCommandState::Kind::Rotate3d, log);
           }});

  reg.Add({"3dscale", "",
           "Scale objects uniformly with the 3D gizmo (handle or a typed factor)",
           CadCommandKind::Scale3d, /*repeatable=*/true, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartGizmoCommand(st, AppCommandState::Kind::Scale3d, log);
           }});

  reg.Add({"chprop", "",
           "Change the color, layer, linetype or lineweight of selected objects",
           CadCommandKind::ChProp, /*repeatable=*/true, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartChPropCommand(st, log);
           }});

  reg.Add({"matchprop", "ma, painter",
           "Copy one object's layer, color, linetype and lineweight onto others",
           CadCommandKind::MatchProp, /*repeatable=*/true, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartMatchPropCommand(st, log);
           }});

  reg.Add({"laymcur", "",
           "Make the layer of a picked object current",
           CadCommandKind::LayMCur, /*repeatable=*/true, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartLayMCurCommand(st, log);
           }});

  reg.Add({"isolateobjects", "isolate", "Hide everything except the selection",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             IsolateSelectedObjects(st, log);
           }});

  reg.Add({"hideobjects", "", "Hide the selected objects",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             HideSelectedObjects(st, log);
           }});

  reg.Add({"unisolateobjects", "unisolate", "Show every object hidden by isolation",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             EndObjectIsolation(st, log);
           }});

  reg.Add({"quickselect", "qs", "Select by object properties",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartQuickSelectCommand(st, log);
           }});

  reg.Add({"selectsimilar", "sesim",
           "Select entities matching the current selection's type, layer and colour",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             SelectSimilarToCurrentSelection(st, &log);
           }});

  reg.Add({"select", "", "Build a selection set",
           CadCommandKind::None, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ClearPendingViewportZoom(st);
             ClearSelection(st);
             st.selBoxWaitingSecond = false;
             log.push_back(
                 "SELECT — click two corners for a window (default when no command is active).");
           }});

  reg.Add({"geomarkpoint", "",
           "Place a Position Marker at a picked point (geolocated drawing)",
           CadCommandKind::GeoMarkPoint, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartGeoMarkPointCommand(st, log);
           }});

  reg.Add({"geomarklatlong", "",
           "Place a Position Marker at a typed latitude and longitude",
           CadCommandKind::GeoMarkLatLong, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartGeoMarkLatLongCommand(st, log);
           }});

  reg.Add({"georeorientmarker", "",
           "Set the geographic marker: a design point, then north",
           CadCommandKind::GeoReorientMarker, /*repeatable=*/false, /*listed=*/true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartGeoReorientMarkerCommand(st, log);
           }});
}

} // namespace stepengine
