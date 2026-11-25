// ADR-0012 / Issue #1578: Alignment, Profile, ViewFrame, Sheet, SampleLine module (Batch 4).

#include "CadCommandsInternal.hpp"
#include "alignment/CadAlignmentCommands.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <string>
#include <vector>

namespace stepengine {

// --- AlignmentLayoutSession ---

StepDescriptor AlignmentLayoutSession::currentStep(const AppCommandState&) const {
  const char* p = pis.empty()
                      ? "ALIGNMENTLAYOUT: Specify start point (click or X,Y) | ESC ends"
                      : "ALIGNMENTLAYOUT: Specify next point or [Undo] (click, X,Y or @dx,dy) | Enter or ESC ends";
  return StepDescriptor::Point(p, StepDynInputSpec{}, BlankEnterPolicy::Custom);
}

StepSubmitResult AlignmentLayoutSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                                std::vector<std::string>& log) {
  (void)HandleAlignmentLayoutText(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult AlignmentLayoutSession::onBlankEnter(AppCommandState& st,
                                                      std::vector<std::string>& log) {
  (void)HandleAlignmentLayoutText(st, "", log);
  return StepSubmitResult::Handled;
}

void AlignmentLayoutSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                            std::vector<std::string>& log) {
  (void)SubmitAlignmentLayoutPoint(st, pick.localX, pick.localY, log);
}

void AlignmentLayoutSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  CancelAlignmentLayout(st, name, log);
}

// --- AlignmentFromObjectsSession ---

StepDescriptor AlignmentFromObjectsSession::currentStep(const AppCommandState&) const {
  using P = AlignmentFromObjectsPhase;
  const char* prompt = "";
  switch (phase) {
    case P::WaitFirst:
      prompt = "CREATEALIGNMENTFROMOBJECTS: Select the first line/arc/polyline | ESC cancels";
      break;
    case P::WaitMore:
      prompt = "CREATEALIGNMENTFROMOBJECTS: Select lines/arcs or polylines to create alignment | Enter when done";
      break;
    case P::WaitDirection:
      prompt = "CREATEALIGNMENTFROMOBJECTS: Press Enter to accept alignment direction or [Reverse]";
      break;
  }
  if (phase == P::WaitDirection)
    return StepDescriptor::Custom(prompt, StepClickRoute::Ignore, BlankEnterPolicy::Custom);
  return StepDescriptor::Custom(prompt, StepClickRoute::RawEntityPick, BlankEnterPolicy::Custom);
}

StepSubmitResult AlignmentFromObjectsSession::onText(AppCommandState& st,
                                                     const std::string& trimmedLine,
                                                     std::vector<std::string>& log) {
  (void)HandleAlignmentFromObjectsText(st, trimmedLine, log);
  return StepSubmitResult::Handled;
}

StepSubmitResult AlignmentFromObjectsSession::onBlankEnter(AppCommandState& st,
                                                           std::vector<std::string>& log) {
  (void)HandleAlignmentFromObjectsText(st, "", log);
  return StepSubmitResult::Handled;
}

void AlignmentFromObjectsSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                                 std::vector<std::string>& log) {
  (void)SubmitAlignmentFromObjectsPick(st, pick.localX, pick.localY, log);
}

void AlignmentFromObjectsSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  CancelAlignmentFromObjects(st, log);
}

// --- StationOffsetPickSession ---

StepDescriptor StationOffsetPickSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Point(
      "GETSTATIONOFFSET: Pick point (OSNAP when enabled) or type X,Y — reports station and offset | ESC ends",
      StepDynInputSpec{}, BlankEnterPolicy::Custom);
}

StepSubmitResult StationOffsetPickSession::onText(AppCommandState& st,
                                                  const std::string& trimmedLine,
                                                  std::vector<std::string>& log) {
  float px = 0.f;
  float py = 0.f;
  if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
    log.push_back("GETSTATIONOFFSET - pick in the viewport or type X,Y; ESC ends.");
    return StepSubmitResult::Handled;
  }
  CommitStationOffsetPick(st, px, py, log);
  return StepSubmitResult::Handled;
}

void StationOffsetPickSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                              std::vector<std::string>& log) {
  CommitStationOffsetPick(st, pick.localX, pick.localY, log);
}

void StationOffsetPickSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("GETSTATIONOFFSET ended.");
}

// --- Registration ---

void RegisterAlignmentCommands(CommandRegistry& reg) {
  reg.Add({"alignment", "createalignment, createalignmentlayout",
           "Create an alignment (no arguments: the Create Alignment dialog): ALIGNMENT <name>[, <description>[, <station>[, <layer>[, <style>]]]][, TYPE=][, LABELSET=]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentCreate(st, args, log);
           }});
  reg.Add({"alignmentadd", "",
           "Add tangent, curve or spiral: ALIGNMENTADD <name>, TANGENT|TANGENTTO|CURVE|CURVELEN|SPIRAL, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentAdd(st, args, log);
           }});
  reg.Add({"alignmentlist", "",
           "Report alignments, stations and entities: ALIGNMENTLIST [<name>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentList(st, args, log);
           }});
  reg.Add({"alignmentdelete", "",
           "Delete an alignment: ALIGNMENTDELETE <name>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentDelete(st, args, log);
           }});
  reg.Add({"createviewframes", "",
           "Create view frames along an alignment: CREATEVIEWFRAMES <alignment>, <start station>, <end station>, <sheet>, <scale or AUTO>, <overlap>[, CENTRED|LEFT|RIGHT]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunCreateViewFrames(st, args, log);
           }});
  reg.Add({"viewframedelete", "",
           "Delete an alignment's view frame groups: VIEWFRAMEDELETE <alignment>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunViewFrameDelete(st, args, log);
           }});
  reg.Add({"createsheets", "",
           "Create paper sheets from view frames: CREATESHEETS <alignment>[, <frames>] (frames: ALL or names such as VF-1 VF-3)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunCreateSheets(st, args, log);
           }});
  reg.Add({"createsamplelines", "",
           "Create sample lines along an alignment: CREATESAMPLELINES <alignment>, <start station>, <end station>, <interval>, <left swath>, <right swath>[, <surface>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunCreateSampleLines(st, args, log);
           }});
  reg.Add({"createsectionsheets", "",
           "Create section sheets from sample lines: CREATESECTIONSHEETS <alignment>, <sample lines>, <sections per sheet>, <axis elevation>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunCreateSectionSheets(st, args, log);
           }});
  reg.Add({"createdesignprofile", "",
           "Create an empty design profile on an alignment: CREATEDESIGNPROFILE <alignment>, <profile name>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunCreateDesignProfile(st, args, log);
           }});
  reg.Add({"profilepvi", "",
           "Add or edit a vertical point of a design profile: PROFILEPVI <profile>, <station>, <elevation>[, <curve length>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunProfilePvi(st, args, log);
           }});
  reg.Add({"profiledelpvi", "",
           "Remove a vertical point from a design profile: PROFILEDELPVI <profile>, <station>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunProfileDelPvi(st, args, log);
           }});
  reg.Add({"profilequery", "",
           "Report a design profile at a station: PROFILEQUERY <profile>, <station>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunProfileQuery(st, args, log);
           }});
  reg.Add({"profilelist", "",
           "List the design profiles in the drawing: PROFILELIST",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunProfileList(st, args, log);
           }});
  reg.Add({"profiledelete", "",
           "Delete a design profile: PROFILEDELETE <profile>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunProfileDelete(st, args, log);
           }});
  reg.Add({"getstationoffset", "",
           "Station and offset of a point: GETSTATIONOFFSET <alignment>[, <x>, <y> | , POINT, <number>][, SURFACE=<surface>] (alignment alone: pick until ESC)",
           CadCommandKind::StationOffsetPick, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunGetStationOffset(st, args, log);
           }});
  reg.Add({"getpointfromstationoffset", "",
           "Point at a station and offset: GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunGetPointFromStationOffset(st, args, log);
           }});
  reg.Add({"createalignmentfromobjects", "createalignmententities",
           "Alignment from lines/arcs/polylines: CREATEALIGNMENTFROMOBJECTS (pick them) | <name>[, START=][, FILLET=][, REVERSE][, ERASE]",
           CadCommandKind::AlignmentFromObjects, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunCreateAlignmentFromObjects(st, args, log);
           }});
  reg.Add({"alignmentlayout", "",
           "Lay out an alignment by PIs: ALIGNMENTLAYOUT <name>[, CURVES|NOCURVES][, FROM=<x> <y>][, DIRECTION=<deg>]",
           CadCommandKind::AlignmentLayout, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentLayout(st, args, log);
           }});
  reg.Add({"alignmentcurvesettings", "",
           "Curve and spiral settings for the layout: ALIGNMENTCURVESETTINGS [RADIUS=][, SPIRALIN=][, SPIRALOUT=]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentCurveSettings(st, args, log);
           }});
  reg.Add({"alignmentmeta", "",
           "Alignment metadata: ALIGNMENTMETA <alignment>, SET, <name>, <value> | REMOVE, <name> | LIST",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentMeta(st, args, log);
           }});
  reg.Add({"stationformat", "",
           "How stations are written: STATIONFORMAT [DELIMITER=+|-|_|/|C|K|NONE][, DIGITS=][, DECIMALS=][, PREFIX=][, SUFFIX=] | DEFAULT",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunStationFormat(st, args, log);
           }});
  reg.Add({"alignmentedit", "",
           "Edit an alignment parameter: ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|STARTRADIUS|DELTA|DIRECTION|CONSTRAINT|DEGREE|TANGENTLENGTH|CHORD|EXTERNAL|MIDORDINATE|A, <value> | THROUGH, <x>, <y>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentEdit(st, args, log);
           }});
  reg.Add({"alignmentlock", "",
           "Lock a parameter: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentLock(st, args, log);
           }});
  reg.Add({"alignmentinsert", "",
           "Insert a tangent, curve or spiral: ALIGNMENTINSERT <name>, <after#>, TANGENT|CURVE|SPIRAL, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentInsert(st, args, log);
           }});
  reg.Add({"alignmentremove", "",
           "Remove an entity: ALIGNMENTREMOVE <name>, <entity#>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentRemove(st, args, log);
           }});
  reg.Add({"alignmentcontinuity", "",
           "Set or list continuity checks: ALIGNMENTCONTINUITY <name>, C0|C1|C2 | LIST",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentContinuity(st, args, log);
           }});
  reg.Add({"reversealignment", "reversealigndirection",
           "Reverse alignment direction and stationing: REVERSEALIGNMENT <name>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunReverseAlignment(st, args, log);
           }});
  reg.Add({"alignmentstation", "",
           "Starting station, reference point and station equations: ALIGNMENTSTATION <name>, START|REFERENCE|EQ, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentStation(st, args, log);
           }});
  reg.Add({"alignmentstyle", "",
           "Alignment display styles: ALIGNMENTSTYLE LIST|NEW|DELETE|ASSIGN|SHOW|HIDE|COLOR|SET|CURRENT|MARKER, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentStyle(st, args, log);
           }});
  reg.Add({"alignmentlabelset", "",
           "Alignment label sets: ALIGNMENTLABELSET LIST|NEW|DELETE|ASSIGN|MAJORINTERVAL|MINORINTERVAL|SHOW|HIDE|STYLE|GEOMETRY, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentLabelSet(st, args, log);
           }});
  reg.Add({"alignmentlabelstyle", "",
           "Alignment label styles (text style, height, colour, precision, prefix/suffix, orientation): ALIGNMENTLABELSTYLE LIST|NEW|DELETE|SET, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentLabelStyle(st, args, log);
           }});
  reg.Add({"alignmentreport", "",
           "Alignment report (text/CSV): ALIGNMENTREPORT [<name>][, TXT|CSV[, <path>]]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentReport(st, args, log);
           }});
  reg.Add({"offsetalignment", "",
           "Create offset alignments: OFFSETALIGNMENT <parent>, <distance>[, <distance>...] (L12 left, R12 right)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunOffsetAlignment(st, args, log);
           }});
  reg.Add({"alignmentdetach", "",
           "Make a linked offset independent: ALIGNMENTDETACH <name>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentDetach(st, args, log);
           }});
  reg.Add({"alignmentintersect", "",
           "Intersections: ALIGNMENTINTERSECT <a>, <b> | <a>, LINE, ... | <a>, CIRCLE, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentIntersect(st, args, log);
           }});
  reg.Add({"editalignment", "",
           "Open the Alignment Layout table: EDITALIGNMENT [<name>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunEditAlignment(st, args, log);
           }});
  reg.Add({"alignmentsurface", "",
           "Ground elevation along an alignment: ALIGNMENTSURFACE <alignment>, <surface>, STATION=<sta>[, OFFSET=<off>] | INTERVAL=<n>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentSurface(st, args, log);
           }});
  reg.Add({"alignmentlabel", "",
           "Label an alignment's segments (needed for tables): ALIGNMENTLABEL <name>, SEGMENTS | SEGMENT, <entity#> | CLEAR",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentLabel(st, args, log);
           }});
  reg.Add({"featurelinefromalignment", "",
           "Feature line from an alignment: FEATURELINEFROMALIGNMENT <alignment>[, INTERVAL=<n>][, TOL=<n>][, SURFACE=<name> | Z=<value>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunFeatureLineFromAlignment(st, args, log);
           }});
  reg.Add({"alignmentprops", "",
           "Alignment properties: ALIGNMENTPROPS <alignment>, NAME|DESCRIPTION|STYLE|LAYER|START, <value>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentProps(st, args, log);
           }});
  reg.Add({"alignmentinverse", "",
           "Distance along an alignment: ALIGNMENTINVERSE <alignment>, STA|PT, ... [, CHORD]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentInverse(st, args, log);
           }});
  reg.Add({"alignmentbearing", "",
           "Bearing at a station: ALIGNMENTBEARING <alignment>, <station>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentBearing(st, args, log);
           }});
  reg.Add({"alignmentdistance", "",
           "Distance along an alignment to a point: ALIGNMENTDISTANCE <alignment>, <x>, <y>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentDistance(st, args, log);
           }});
  reg.Add({"alignmentcheckset", "",
           "Design check sets: ALIGNMENTCHECKSET NEW|DELETE|LIST|SET|LOAD|SAVE, ...",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentCheckSet(st, args, log);
           }});
  reg.Add({"alignmentcriteria", "",
           "Apply a check set to an alignment: ALIGNMENTCRITERIA <alignment>, <set>|NONE",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentCriteria(st, args, log);
           }});
  reg.Add({"alignmentspeed", "",
           "Design speed from a station: ALIGNMENTSPEED <alignment>, <station>, <speed>|CLEAR",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentSpeed(st, args, log);
           }});
  reg.Add({"alignmentcheck", "",
           "Check an alignment against its design criteria: ALIGNMENTCHECK <alignment>",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentCheck(st, args, log);
           }});
  reg.Add({"editalignmentstyle", "",
           "Open the Alignment Style editor: EDITALIGNMENTSTYLE [<style>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunEditAlignmentStyle(st, args, log);
           }});
  reg.Add({"alignmenttable", "",
           "Insert a line, curve, spiral, segment or equation table: ALIGNMENTTABLE <name>, LINE|CURVE|SPIRAL|SEGMENT|EQUATIONS[, STATIC|DYNAMIC][, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             RunAlignmentTable(st, args, log);
           }});
}

} // namespace stepengine
