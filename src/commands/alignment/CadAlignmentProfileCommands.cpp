#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "alignment/CadAlignmentInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Survey/AlignmentStylePublic.hpp>
#include <GoSurvey/Survey/AlignmentLabelSetPublic.hpp>
#include "CadColor.hpp"
#include "TextStyle.hpp"
#include "GoSurvey/Survey/AlignmentReportModel.hpp"
#include "GoSurvey/Survey/AlignmentGroundModel.hpp"
#include <GoSurvey/Terrain/FeatureLineStations.hpp>
#include "GoSurvey/Survey/AlignmentTables.hpp"
#include "GoSurvey/Survey/AlignmentLabels.hpp"
#include "GoSurvey/Survey/AlignmentDesign.hpp"
#include "GoSurvey/Survey/AlignmentViewFrames.hpp"
#include "PaperSpace.hpp"
#include "ViewportZoomScale.hpp"
#include "GoSurvey/Document/cadtable.hpp"
#include "GoSurvey/Document/cadtablestyle.hpp"
#include <GoSurvey/UI/FileDialogs.hpp>
#include <GoSurvey/Core/StringUtil.hpp>
#include <GoSurvey/Core/NumFormat.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr double kProfileStationTol = 1e-6;

// Index of the profile named \p name (case-insensitive), or -1.
int FindProfileIndex(const AppCommandState& st, const std::string& name) {
  const std::string key = Lower(Trim(name));
  for (size_t i = 0; i < st.drawing.profiles.size(); ++i)
    if (Lower(st.drawing.profiles[i].name) == key) return static_cast<int>(i);
  return -1;
}

// The alignment with stable id \p id, or nullptr when it has been deleted (REQ-521.7).
const alignment::Alignment* AlignmentById(const AppCommandState& st, std::uint64_t id) {
  for (const alignment::Alignment& a : st.drawing.alignments)
    if (a.id == id) return &a;
  return nullptr;
}

// The internal station for the displayed station \p displayed on \p a (REQ-517.6a), which must lie on the alignment.
// False with `why` when it is not exactly one station or is outside the alignment.
bool InternalStationOnAlignment(const AppCommandState& st, const alignment::Alignment& a, double displayed,
                                double* internal, std::string* why) {
  if (!alignment::ResolveDisplayedStation(a, displayed, internal, why)) return false;
  if (*internal < a.StartStation() - kProfileStationTol || *internal > a.EndStation() + kProfileStationTol) {
    *why = "the station " + StaFmt(st, displayed) + " is outside the alignment (" + Sta(st, a, a.StartStation()) +
           " to " + Sta(st, a, a.EndStation()) + ")";
    return false;
  }
  return true;
}

// The grade as a percent, two decimals (rise over run x 100).
std::string GradePercent(double grade) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.2f%%", grade * 100.0);
  return buf;
}

}  // namespace

// CREATEDESIGNPROFILE <alignment>, <profile name> (REQ-521.6): an empty profile on the alignment. One undo step.
void RunCreateDesignProfile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("CREATEDESIGNPROFILE - usage: CREATEDESIGNPROFILE <alignment>, <profile name>");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {  // REQ-521.6: a read-only project refuses before any change
    log.push_back("CREATEDESIGNPROFILE - the project is read-only; no profile was created.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "CREATEDESIGNPROFILE", f[0], log);
  if (a == nullptr) return;
  const std::string name = Trim(f[1]);
  if (name.empty()) {
    log.push_back("CREATEDESIGNPROFILE - a profile name is required. No profile was created.");
    return;
  }
  if (FindProfileIndex(st, name) >= 0) {
    log.push_back("CREATEDESIGNPROFILE - a profile named \"" + name + "\" already exists. No profile was created.");
    return;
  }
  const std::uint64_t alignmentId = a->id;
  const std::string alignmentName = a->name;
  PushUndoSnapshot(st, "Create design profile");
  alignment::VerticalProfile p;
  p.id = st.nextEntityId++;
  p.name = name;
  p.alignmentId = alignmentId;
  st.drawing.profiles.push_back(std::move(p));
  log.push_back("CREATEDESIGNPROFILE - profile \"" + name + "\" on \"" + alignmentName + "\" created with no PVIs.");
}

// PROFILEPVI <profile>, <station>, <elevation>[, <curve length>] (REQ-521.6): adds the PVI at that station, or changes the
// elevation and curve length of the PVI already there. Leaving out the curve length means no curve. One undo step.
void RunProfilePvi(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() < 3 || f.size() > 4) {
    log.push_back("PROFILEPVI - usage: PROFILEPVI <profile>, <station>, <elevation>[, <curve length>]");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {
    log.push_back("PROFILEPVI - the project is read-only; no PVI was changed.");
    return;
  }
  const int pi = FindProfileIndex(st, f[0]);
  if (pi < 0) {
    log.push_back("PROFILEPVI - no profile named \"" + Trim(f[0]) + "\". No PVI was changed.");
    return;
  }
  const std::string profileName = st.drawing.profiles[static_cast<size_t>(pi)].name;
  const alignment::Alignment* a = AlignmentById(st, st.drawing.profiles[static_cast<size_t>(pi)].alignmentId);
  if (a == nullptr) {
    log.push_back("PROFILEPVI - profile \"" + profileName + "\" has no alignment (alignment missing). No PVI was changed.");
    return;
  }
  double displayed = 0.0, elevation = 0.0, curveLength = 0.0;
  if (!ParseSta(st, f[1], &displayed) || !ParseNum(Trim(f[2]), &elevation) ||
      (f.size() == 4 && !ParseNum(Trim(f[3]), &curveLength))) {
    log.push_back("PROFILEPVI - a station, elevation or curve length is not understood. No PVI was changed.");
    return;
  }
  double station = 0.0;
  std::string why;
  if (!InternalStationOnAlignment(st, *a, displayed, &station, &why)) {
    log.push_back("PROFILEPVI - " + why + ". No PVI was changed.");
    return;
  }

  alignment::VerticalProfile candidate = st.drawing.profiles[static_cast<size_t>(pi)];
  bool replaced = false;
  for (alignment::VerticalPoint& q : candidate.points) {
    if (std::abs(q.station - station) <= kProfileStationTol) {  // edit: keep the PVI's station
      q.elevation = elevation;
      q.curveLength = curveLength;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    const auto pos = std::find_if(candidate.points.begin(), candidate.points.end(),
                                  [station](const alignment::VerticalPoint& q) { return q.station > station; });
    candidate.points.insert(pos, alignment::VerticalPoint{station, elevation, curveLength});
  }
  const std::string refusal = alignment::ProfileGeometryRefusal(candidate);
  if (!refusal.empty()) {  // REQ-521.3: refused input changes nothing
    log.push_back("PROFILEPVI - refused: " + refusal + ". No PVI was changed.");
    return;
  }

  const std::string staText = Sta(st, *a, station);
  PushUndoSnapshot(st, replaced ? "Edit profile PVI" : "Add profile PVI");
  st.drawing.profiles[static_cast<size_t>(pi)].points = std::move(candidate.points);
  log.push_back("PROFILEPVI - profile \"" + profileName + "\": PVI at " + staText + (replaced ? " changed" : " added") +
                " (elevation " + Lin(st, elevation) +
                (curveLength > 0.0 ? ", curve length " + Lin(st, curveLength) : std::string(", no curve")) + ").");
}

// PROFILEDELPVI <profile>, <station> (REQ-521.6): removes the PVI at that station. Refused when the rest is not valid.
void RunProfileDelPvi(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("PROFILEDELPVI - usage: PROFILEDELPVI <profile>, <station>");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {
    log.push_back("PROFILEDELPVI - the project is read-only; no PVI was removed.");
    return;
  }
  const int pi = FindProfileIndex(st, f[0]);
  if (pi < 0) {
    log.push_back("PROFILEDELPVI - no profile named \"" + Trim(f[0]) + "\". No PVI was removed.");
    return;
  }
  const std::string profileName = st.drawing.profiles[static_cast<size_t>(pi)].name;
  const alignment::Alignment* a = AlignmentById(st, st.drawing.profiles[static_cast<size_t>(pi)].alignmentId);
  if (a == nullptr) {
    log.push_back("PROFILEDELPVI - profile \"" + profileName + "\" has no alignment (alignment missing). No PVI was removed.");
    return;
  }
  double displayed = 0.0, station = 0.0;
  std::string why;
  // Removal matches the PVI by its station, so a PVI left beyond a shortened alignment can still be removed (REQ-521.6),
  // unless the alignment has station equations: then the gap reason refuses it (REQ-521 item 6).
  if (!ParseSta(st, f[1], &displayed) || !alignment::ResolveDisplayedStation(*a, displayed, &station, &why)) {
    log.push_back("PROFILEDELPVI - " + (why.empty() ? std::string("the station is not understood") : why) +
                  ". No PVI was removed.");
    return;
  }
  alignment::VerticalProfile candidate = st.drawing.profiles[static_cast<size_t>(pi)];
  const auto found = std::find_if(candidate.points.begin(), candidate.points.end(), [station](const alignment::VerticalPoint& q) {
    return std::abs(q.station - station) <= kProfileStationTol;
  });
  if (found == candidate.points.end()) {
    log.push_back("PROFILEDELPVI - profile \"" + profileName + "\" has no PVI at " + Sta(st, *a, station) +
                  ". No PVI was removed.");
    return;
  }
  candidate.points.erase(found);
  const std::string refusal = alignment::ProfileGeometryRefusal(candidate);
  if (!refusal.empty()) {
    log.push_back("PROFILEDELPVI - refused: " + refusal + ". No PVI was removed.");
    return;
  }
  const std::string staText = Sta(st, *a, station);
  PushUndoSnapshot(st, "Delete profile PVI");
  st.drawing.profiles[static_cast<size_t>(pi)].points = std::move(candidate.points);
  log.push_back("PROFILEDELPVI - profile \"" + profileName + "\": PVI at " + staText + " removed.");
}

// PROFILEQUERY <profile>, <station> (REQ-521.4, .5): elevation, grade and the derived 3D design point at a displayed
// station. Refused, with the reason, when the profile's alignment is missing.
void RunProfileQuery(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("PROFILEQUERY - usage: PROFILEQUERY <profile>, <station>");
    return;
  }
  const int pi = FindProfileIndex(st, f[0]);
  if (pi < 0) {
    log.push_back("PROFILEQUERY - no profile named \"" + Trim(f[0]) + "\".");
    return;
  }
  const alignment::VerticalProfile& p = st.drawing.profiles[static_cast<size_t>(pi)];
  const alignment::Alignment* a = AlignmentById(st, p.alignmentId);
  if (a == nullptr) {
    log.push_back("PROFILEQUERY - profile \"" + p.name + "\" has no alignment (alignment missing), so it has no design path.");
    return;
  }
  double displayed = 0.0, station = 0.0;
  std::string why;
  // REQ-521.4: the profile's range answers first ("outside the profile"). A displayed station the alignment cannot resolve
  // (a station-equation gap) is refused by ResolveDisplayedStation before that; DesignPathPoint checks the alignment range.
  if (!ParseSta(st, f[1], &displayed) || !alignment::ResolveDisplayedStation(*a, displayed, &station, &why)) {
    log.push_back("PROFILEQUERY - " + (why.empty() ? std::string("the station is not understood") : why) + ".");
    return;
  }
  const alignment::ProfileSample sample = alignment::ProfileAtStation(p, station);
  if (!sample.hasValue) {
    log.push_back("PROFILEQUERY - profile \"" + p.name + "\" at " + Sta(st, *a, station) + ": no value (" + sample.reason + ").");
    return;
  }
  alignment::DesignPoint pt;
  if (!alignment::DesignPathPoint(*a, p, station, &pt, &why)) {
    log.push_back("PROFILEQUERY - profile \"" + p.name + "\" at " + Sta(st, *a, station) + ": " + why + ".");
    return;
  }
  log.push_back("PROFILEQUERY - profile \"" + p.name + "\" at " + Sta(st, *a, station) + ": elevation " +
                Lin(st, sample.elevation) + ", grade " + GradePercent(sample.grade) + ". Design point (" + Lin(st, pt.x) +
                ", " + Lin(st, pt.y) + ", " + Lin(st, pt.z) + ").");
}

// PROFILELIST (REQ-521.7): every profile, its alignment and its PVI count. A profile whose alignment is gone says so.
void RunProfileList(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  if (!Trim(args).empty()) {
    log.push_back("PROFILELIST - usage: PROFILELIST");
    return;
  }
  if (st.drawing.profiles.empty()) {
    log.push_back("PROFILELIST - no profiles in this drawing.");
    return;
  }
  for (const alignment::VerticalProfile& p : st.drawing.profiles) {
    const alignment::Alignment* a = AlignmentById(st, p.alignmentId);
    std::string line = "PROFILELIST - \"" + p.name + "\" on " +
                       (a != nullptr ? "\"" + a->name + "\"" : std::string("alignment missing")) + ", " +
                       std::to_string(p.points.size()) + " PVI(s)";
    const std::string refusal = alignment::ProfileGeometryRefusal(p);
    if (!refusal.empty()) line += "; geometry not valid: " + refusal;
    log.push_back(line + ".");
  }
}

// PROFILEDELETE <profile> (REQ-521.6): deletes the profile. One undo step.
void RunProfileDelete(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 1) {
    log.push_back("PROFILEDELETE - usage: PROFILEDELETE <profile>");
    return;
  }
  if (ActiveProjectPointsReadOnly(st)) {
    log.push_back("PROFILEDELETE - the project is read-only; no profile was deleted.");
    return;
  }
  const int pi = FindProfileIndex(st, f[0]);
  if (pi < 0) {
    log.push_back("PROFILEDELETE - no profile named \"" + Trim(f[0]) + "\".");
    return;
  }
  const std::string name = st.drawing.profiles[static_cast<size_t>(pi)].name;
  PushUndoSnapshot(st, "Delete design profile");
  st.drawing.profiles.erase(st.drawing.profiles.begin() + pi);
  log.push_back("PROFILEDELETE - profile \"" + name + "\" deleted.");
}

// GETSTATIONOFFSET <alignment>, <x>, <y>[, SURFACE=<surface>]  (WORLD x/y) - REQ-484.4
void RunGetStationOffset(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::vector<std::string> f = SplitFields(args);
  std::string surfaceName;  // REQ-484.4 / REQ-497: the ground elevation at the point, from a chosen surface
  if (!f.empty() && Lower(f.back()).rfind("surface=", 0) == 0) {
    surfaceName = Trim(f.back().substr(8));
    f.pop_back();
  }
  double x = 0.0, y = 0.0;
  if (f.size() == 1 && !f[0].empty()) {  // REQ-484.4 pick: each click reports, until ESC
    const alignment::Alignment* a = Resolve(st, "GETSTATIONOFFSET", f[0], log);
    if (a == nullptr) return;
    if (st.active != AppCommandState::Kind::None) {
      log.push_back("GETSTATIONOFFSET - finish or cancel the active command first.");
      return;
    }
    auto sess = std::make_unique<stepengine::StationOffsetPickSession>();
    sess->pickArgs = a->name + (surfaceName.empty() ? std::string{} : ", SURFACE=" + surfaceName);
    stepengine::BeginSession(st, std::move(sess), false);
    log.push_back("GETSTATIONOFFSET - pick a point (or type X,Y) to report its station and offset on \"" + a->name +
                  "\". ESC ends.");
    return;
  }
  if (f.size() == 3 && Lower(f[1]) == "point") {  // REQ-501.3: a survey point, by its point number
    double number = 0.0;
    if (!ParseNum(f[2], &number)) {
      log.push_back("GETSTATIONOFFSET - the point number must be a number.");
      return;
    }
    const auto point = std::find_if(st.drawing.surveyPoints.begin(), st.drawing.surveyPoints.end(),
                                    [&](const SurveyPoint& p) { return p.id == static_cast<int>(number); });
    if (point == st.drawing.surveyPoints.end()) {
      log.push_back("GETSTATIONOFFSET - no survey point numbered " + f[2] + ".");
      return;
    }
    x = point->easting;  // the survey point's coordinates are world, and so is the query
    y = point->northing;
  } else if (f.size() != 3 || !ParseNum(f[1], &x) || !ParseNum(f[2], &y)) {
    log.push_back("GETSTATIONOFFSET - usage: GETSTATIONOFFSET <alignment>[, <x>, <y> | , POINT, <number>]"
                  " [, SURFACE=<surface>] (the alignment alone picks points until ESC).");
    return;
  }

  alignment::Alignment* a = Resolve(st, "GETSTATIONOFFSET", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("GETSTATIONOFFSET - \"" + a->name + "\" has no geometry.");
    return;
  }
  double lx = 0.0, ly = 0.0;
  CadCoord::LocalFromWorld(st, x, y, &lx, &ly);
  const alignment::StationOffset so = a->GetStationOffset(lx, ly);
  std::string line = "GETSTATIONOFFSET - \"" + a->name + "\": Station = " + Sta(st, *a, so.internalStation) +
                     "  Offset = " + Lin(st, so.offset) + (so.offset < 0.0 ? " (left)" : so.offset > 0.0 ? " (right)" : "") +
                     "  Bearing = " + Brg(st, so.azimuth) + ".";
  if (so.outOfRange) line += " The point lies beyond the end of the alignment; the nearest end is reported.";
  if (so.ambiguous) line += " Another location is equally close; the lowest station is reported.";
  if (surfaceName.empty()) {
    line += " Elevation: no profile.";  // REQ-500: profiles are a separate feature
  } else {
    const int si = FindSurfaceIndex(st, surfaceName);
    std::unique_ptr<ISurfaceQuery> ground =
        si < 0 ? nullptr : MakeSurfaceQueryForCommands(st, st.drawing.cadSurfaces[static_cast<size_t>(si)]);
    double z = 0.0;
    if (si < 0) line += " There is no surface named \"" + surfaceName + "\".";
    else if (!ground) line += " \"" + surfaceName + "\" is not a built surface.";
    else if (ground->elevationAt(lx, ly, &z)) line += " Elevation = " + Lin(st, z) + " (surface \"" + surfaceName + "\").";
    else line += " Elevation: the point is off the surface \"" + surfaceName + "\".";
  }
  log.push_back(line);
}

void CommitStationOffsetPick(AppCommandState& st, double lx, double ly, std::vector<std::string>& log) {
  const auto* sess = st.sessionAs<stepengine::StationOffsetPickSession>();
  if (!sess) return;
  assert(std::isfinite(lx) && std::isfinite(ly));
  double wx = 0.0, wy = 0.0;
  CadCoord::WorldFromLocal(st, static_cast<float>(lx), static_cast<float>(ly), &wx, &wy);
  std::array<char, 64> sx{}, sy{};
  std::snprintf(sx.data(), sx.size(), "%.17g", wx);
  std::snprintf(sy.data(), sy.size(), "%.17g", wy);
  const std::vector<std::string> f = SplitFields(sess->pickArgs);
  std::string args = f.empty() ? std::string{} : f[0];
  args += std::string(", ") + sx.data() + ", " + sy.data();
  for (size_t i = 1; i < f.size(); ++i) args += ", " + f[i];
  RunGetStationOffset(st, args, log);  // the command stays active for the next pick
}

// GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset>  - REQ-484.4
void RunGetPointFromStationOffset(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  double displayed = 0.0, offset = 0.0;
  if (f.size() != 3 || !ParseSta(st, f[1], &displayed) || !ParseNum(f[2], &offset)) {
    log.push_back("GETPOINTFROMSTATIONOFFSET - usage: GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset> (station like 12+45.32).");
    return;
  }
  alignment::Alignment* a = Resolve(st, "GETPOINTFROMSTATIONOFFSET", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("GETPOINTFROMSTATIONOFFSET - \"" + a->name + "\" has no geometry.");
    return;
  }
  const std::vector<double> raws = a->RawStationsForDisplayed(displayed);
  if (raws.empty()) {
    log.push_back("GETPOINTFROMSTATIONOFFSET - station " + StaFmt(st, displayed) + " is outside \"" + a->name +
                  "\" (" + Sta(st, *a, a->StartStation()) + " to " + StaFmt(st, a->EndStationWithEquations()) + ").");
    return;
  }
  for (const double raw : raws) {
    double az = 0.0;
    const alignment::Point2 p = a->GetPointFromStationOffset(raw, offset, &az);
    log.push_back("GETPOINTFROMSTATIONOFFSET - \"" + a->name + "\": X = " + Lin(st, p.x + CadCoord::WorldOriginX(st)) +
                  "  Y = " + Lin(st, p.y + CadCoord::WorldOriginY(st)) + "  Bearing = " + Brg(st, az) +
                  (raws.size() > 1 ? "  (station occurs " + std::to_string(raws.size()) + " times; this is the one at internal station " + Lin(st, raw) + ")" : ""));
  }
}

namespace {

/// "<name>  STA 25+42.35  OFF -12.40  BRG <bearing>": the cursor's place on one alignment (REQ-501.2).
std::string ReadoutText(const AppCommandState& st, const alignment::Alignment& a, const alignment::StationOffset& so) {
  assert(!a.Empty());
  assert(std::isfinite(so.internalStation));
  return a.name + "  STA " + StaFmt(st, a.DisplayedStation(so.internalStation, true)) + "  OFF " +
         FormatLinear(so.offset, st.displayLinearPrecision) + "  BRG " + Brg(st, so.azimuth);
}

}  // namespace

std::string AlignmentCursorReadout(const AppCommandState& st, double lx, double ly, double maxOffset) {
  // REQ-487.1: while a grip drags an alignment, the readout follows the cursor on that alignment as it was grabbed,
  // however far away, so the station, offset and bearing say where the grip is being taken.
  if (st.entityGripMoveActive && st.entityGripType == SelectedEntity::Type::Alignment &&
      !st.entityGripOrigAlignment.Empty()) {
    const alignment::Alignment& g = st.entityGripOrigAlignment;
    return ReadoutText(st, g, g.GetStationOffset(lx, ly));
  }
  const alignment::Alignment* best = nullptr;
  alignment::StationOffset bestSo;
  double bestAbs = maxOffset;
  for (const alignment::Alignment& a : st.drawing.alignments) {
    if (a.Empty()) continue;
    const alignment::StationOffset so = a.GetStationOffset(lx, ly);
    if (std::fabs(so.offset) <= bestAbs) {
      bestAbs = std::fabs(so.offset);
      best = &a;
      bestSo = so;
    }
  }
  if (best == nullptr) return {};
  return ReadoutText(st, *best, bestSo);
}

bool ResolveAlignmentStationSnapPoint(const AppCommandState& st, double displayed, float preferLocalX,
                                      float preferLocalY, float* outLx, float* outLy) {
  if (outLx == nullptr || outLy == nullptr)
    return false;
  const alignment::Alignment* bestAl = nullptr;
  alignment::Point2 bestPt{};
  double bestDist2 = std::numeric_limits<double>::infinity();
  for (const alignment::Alignment& a : st.drawing.alignments) {
    if (a.Empty())
      continue;
    const std::vector<double> raws = a.RawStationsForDisplayed(displayed);
    for (const double raw : raws) {
      const alignment::Point2 p = a.GetPointAtStation(raw);
      float lx = 0.f;
      float ly = 0.f;
      double wx = 0.0;
      double wy = 0.0;
      CadCoord::WorldFromLocal(st, p.x, p.y, &wx, &wy);
      CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
      const double dx = static_cast<double>(lx) - static_cast<double>(preferLocalX);
      const double dy = static_cast<double>(ly) - static_cast<double>(preferLocalY);
      const double d2 = dx * dx + dy * dy;
      if (bestAl == nullptr || d2 < bestDist2) {
        bestAl = &a;
        bestPt = p;
        bestDist2 = d2;
      }
    }
  }
  if (bestAl == nullptr)
    return false;
  double wx = 0.0;
  double wy = 0.0;
  CadCoord::WorldFromLocal(st, bestPt.x, bestPt.y, &wx, &wy);
  CadCoord::LocalFromWorld(st, wx, wy, outLx, outLy);
  return std::isfinite(*outLx) && std::isfinite(*outLy);
}

bool LooksLikeAlignmentStationSnapInput(const std::string& raw) {
  const std::string trimmed = StringUtil::trimCopy(raw);
  if (trimmed.size() < 7)
    return false;
  const std::string low = Lower(trimmed);
  return low.rfind("station", 0) == 0;
}

bool TryParseAlignmentStationSnapStoragePoint(AppCommandState& st, const std::string& raw, float* lx, float* ly,
                                              double* outWorldZ, bool /*allowRelative*/, float baseLocalX,
                                              float baseLocalY) {
  st.alignmentStationSnapRejectReason.clear();
  if (!LooksLikeAlignmentStationSnapInput(raw))
    return false;
  if (lx == nullptr || ly == nullptr) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” internal error (null output).";
    return false;
  }
  std::string trimmed = StringUtil::trimCopy(raw);
  const std::string low = Lower(trimmed);
  if (low.size() < 7) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” usage: STATION 12+45.32 (displayed station on an alignment).";
    return false;
  }
  size_t i = 7;
  while (i < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i])) != 0)
    ++i;
  if (i < trimmed.size() && (trimmed[i] == ',' || trimmed[i] == '='))
    ++i;
  while (i < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[i])) != 0)
    ++i;
  const std::string staText = StringUtil::trimCopy(trimmed.substr(i));
  if (staText.empty()) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” missing station value (try 12+45.32).";
    return false;
  }
  double displayed = 0.0;
  if (!ParseSta(st, staText, &displayed)) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” \"" + staText + "\" is not a station.";
    return false;
  }
  if (!ResolveAlignmentStationSnapPoint(st, displayed, baseLocalX, baseLocalY, lx, ly)) {
    st.alignmentStationSnapRejectReason =
        "STATION snap â€” station " + StaFmt(st, displayed) + " is not on any alignment.";
    return false;
  }
  st.objectSnapStationDisplayedValid = true;
  st.objectSnapStationDisplayed = displayed;
  if (outWorldZ != nullptr)
    *outWorldZ = static_cast<double>(CadWorkPlaneElevation(st));
  st.resolvedPointZValid = true;
  st.resolvedPointZ = static_cast<float>(CadWorkPlaneElevation(st));
  st.resolvedPointZTyped = false;
  st.viewportSnapPickValid = false;
  return true;
}

// ======================================================================================================
// Phase 2 (REQ-486..489): editing, constraints, station control, reverse, offsets, intersections.
// ======================================================================================================
