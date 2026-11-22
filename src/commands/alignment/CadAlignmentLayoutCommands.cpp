#include "GsIo.hpp"
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

int FindCheckSetIndex(const AppCommandState& st, const std::string& name) {
  for (size_t i = 0; i < st.drawing.alignmentCheckSets.size(); ++i) {
    if (Lower(st.drawing.alignmentCheckSets[i].name) == Lower(name)) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

// ALIGNMENTCHECKSET NEW, <name>[, ROAD|RAIL]
// ALIGNMENTCHECKSET DELETE, <name>
// ALIGNMENTCHECKSET LIST
// ALIGNMENTCHECKSET SET, <name>, RADIUS, <speed>, <minRadius>   (minRadius 0 removes the row for that speed)
// ALIGNMENTCHECKSET SET, <name>, MINTANGENT|MINSPIRAL|MINA|MAXA|MINLINE|MAXDEFLECTION|SUPERELEVATION|FRICTION|SIGHT|CLEARANCE, <value>
//   (0 switches the check off; SUPERELEVATION and FRICTION are REQ-522 rates, SIGHT and CLEARANCE are REQ-522 feet)
void RunAlignmentCheckSet(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTCHECKSET - usage: ALIGNMENTCHECKSET NEW, <name>[, ROAD|RAIL] | DELETE, <name> | LIST | "
      "SET, <name>, RADIUS, <speed>, <minRadius> | SET, <name>, MINTANGENT|MINSPIRAL|MINA|MAXA|MINLINE|MAXDEFLECTION|"
      "SUPERELEVATION|FRICTION|SIGHT|CLEARANCE, <value> | LOAD, <file> | SAVE, <name>, <file>.";
  if (f.empty()) {
    log.push_back(usage);
    return;
  }
  const std::string verb = Lower(f[0]);
  if (verb == "list") {
    if (st.drawing.alignmentCheckSets.empty()) {
      log.push_back("ALIGNMENTCHECKSET - no check sets. Make one with ALIGNMENTCHECKSET NEW, <name>.");
      return;
    }
    for (const alignment::AlignmentCheckSet& s : st.drawing.alignmentCheckSets) {
      log.push_back("ALIGNMENTCHECKSET: " + s.name + "  [" + (s.kind == alignment::CheckSetKind::Rail ? "rail" : "road") +
                    ", " + std::to_string(s.radiusTable.size()) + " radius row(s), min tangent " +
                    FormatLinear(s.minTangent, st.displayLinearPrecision) + ", min spiral " +
                    FormatLinear(s.minSpiralLength, st.displayLinearPrecision) + ", A " +
                    FormatLinear(s.minA, st.displayLinearPrecision) + " to " +
                    FormatLinear(s.maxA, st.displayLinearPrecision) + "]");
    }
    return;
  }
  if (verb == "new") {
    if (f.size() < 2 || Trim(f[1]).empty()) {
      log.push_back("ALIGNMENTCHECKSET - the new set needs a name.");
      return;
    }
    const std::string name = Trim(f[1]);
    if (FindCheckSetIndex(st, name) >= 0) {
      log.push_back("ALIGNMENTCHECKSET - a check set named \"" + name + "\" already exists.");
      return;
    }
    alignment::AlignmentCheckSet made;
    made.name = name;
    if (f.size() >= 3) {
      const std::string kind = Lower(Trim(f[2]));
      if (kind == "rail") {
        made.kind = alignment::CheckSetKind::Rail;
      } else if (kind != "road") {
        log.push_back("ALIGNMENTCHECKSET - kind must be ROAD or RAIL, not \"" + Trim(f[2]) + "\".");
        return;
      }
    }
    PushUndoSnapshot(st, "New check set");
    st.drawing.alignmentCheckSets.push_back(made);
    log.push_back("ALIGNMENTCHECKSET - created \"" + name + "\" (" +
                  (made.kind == alignment::CheckSetKind::Rail ? "rail" : "road") + ").");
    return;
  }
  if (verb == "delete") {
    if (f.size() != 2) {
      log.push_back("ALIGNMENTCHECKSET - usage: ALIGNMENTCHECKSET DELETE, <name>.");
      return;
    }
    const int idx = FindCheckSetIndex(st, f[1]);
    if (idx < 0) {
      log.push_back("ALIGNMENTCHECKSET - no check set named \"" + Trim(f[1]) + "\".");
      return;
    }
    const std::string name = st.drawing.alignmentCheckSets[static_cast<size_t>(idx)].name;
    PushUndoSnapshot(st, "Delete check set");
    st.drawing.alignmentCheckSets.erase(st.drawing.alignmentCheckSets.begin() + idx);
    int cleared = 0;
    for (AlignmentStyle& style : st.drawing.alignmentStyles) {
      if (Lower(style.checkSetName) == Lower(name)) style.checkSetName.clear();
    }
    for (alignment::Alignment& a : st.drawing.alignments) {
      if (Lower(a.checkSetName) == Lower(name)) {
        a.checkSetName.clear();
        ++cleared;
      }
    }
    log.push_back("ALIGNMENTCHECKSET - deleted \"" + name + "\"" +
                  (cleared > 0 ? "; " + std::to_string(cleared) + " alignment(s) no longer have criteria." : "."));
    return;
  }
  if (verb == "set") {
    if (f.size() < 4) {
      log.push_back(usage);
      return;
    }
    const int idx = FindCheckSetIndex(st, f[1]);
    if (idx < 0) {
      log.push_back("ALIGNMENTCHECKSET - no check set named \"" + Trim(f[1]) + "\".");
      return;
    }
    alignment::AlignmentCheckSet edited = st.drawing.alignmentCheckSets[static_cast<size_t>(idx)];
    const std::string what = Lower(Trim(f[2]));
    if (what == "radius") {
      double speed = 0.0, minRadius = 0.0;
      if (f.size() != 5 || !ParseNum(f[3], &speed) || !ParseNum(f[4], &minRadius) || speed < 0.0 || minRadius < 0.0) {
        log.push_back("ALIGNMENTCHECKSET - usage: SET, <name>, RADIUS, <speed>, <minRadius> (both zero or more).");
        return;
      }
      auto& table = edited.radiusTable;
      table.erase(std::remove_if(table.begin(), table.end(),
                                 [speed](const alignment::RadiusRequirement& r) { return std::fabs(r.speed - speed) < 1e-9; }),
                  table.end());
      if (minRadius > 0.0) table.push_back({speed, minRadius});
      std::sort(table.begin(), table.end(),
                [](const alignment::RadiusRequirement& x, const alignment::RadiusRequirement& y) { return x.speed < y.speed; });
    } else if (what == "mintangent" || what == "minspiral" || what == "mina" || what == "maxa" ||
               what == "minline" || what == "maxdeflection" || what == "superelevation" || what == "friction" ||
               what == "sight" || what == "clearance") {
      double value = 0.0;
      if (f.size() != 4 || !ParseNum(f[3], &value) || value < 0.0) {
        log.push_back("ALIGNMENTCHECKSET - " + what + " takes one value, zero or more.");
        return;
      }
      if (what == "mintangent") edited.minTangent = value;
      else if (what == "minspiral") edited.minSpiralLength = value;
      else if (what == "mina") edited.minA = value;
      else if (what == "maxa") edited.maxA = value;
      else if (what == "minline") edited.minLineLength = value;
      else if (what == "maxdeflection") edited.maxTangentDeflection = value;  // degrees, at a curveless PI
      else if (what == "superelevation") edited.maxSuperelevation = value;  // REQ-522: maximum rate, ft/ft
      else if (what == "friction") edited.sideFriction = value;              // REQ-522: side friction factor f
      else if (what == "clearance") edited.sightClearance = value;           // REQ-522: lateral sight clearance, ft
      else edited.minSightDistance = value;  // REQ-522: minimum sight distance, ft
    } else {
      log.push_back(usage);
      return;
    }
    if (edited == st.drawing.alignmentCheckSets[static_cast<size_t>(idx)]) {
      log.push_back("ALIGNMENTCHECKSET - nothing changed.");
      return;
    }
    PushUndoSnapshot(st, "Edit check set");
    st.drawing.alignmentCheckSets[static_cast<size_t>(idx)] = std::move(edited);
    log.push_back("ALIGNMENTCHECKSET - \"" + st.drawing.alignmentCheckSets[static_cast<size_t>(idx)].name + "\" updated.");
    return;
  }
  if (verb == "load" && f.size() == 2) {  // REQ-502: reference values loaded from a user file
    std::ifstream in(Trim(f[1]), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<alignment::AlignmentCheckSet> loaded;
    std::string err = "the file could not be read";
    if (!in.good() && text.empty()) {
      log.push_back("ALIGNMENTCHECKSET - \"" + Trim(f[1]) + "\": " + err + ".");
      return;
    }
    if (!AlignmentCheckSetsFromJsonText(text, &loaded, &err) || loaded.empty()) {
      log.push_back("ALIGNMENTCHECKSET - \"" + Trim(f[1]) + "\": " + (loaded.empty() && err.empty() ? "no check sets" : err) + ".");
      return;
    }
    PushUndoSnapshot(st, "Load check sets");
    int added = 0, replaced = 0;
    for (alignment::AlignmentCheckSet& s : loaded) {
      const int at = FindCheckSetIndex(st, s.name);
      if (at >= 0) {
        st.drawing.alignmentCheckSets[static_cast<size_t>(at)] = std::move(s);
        ++replaced;
      } else {
        st.drawing.alignmentCheckSets.push_back(std::move(s));
        ++added;
      }
    }
    log.push_back("ALIGNMENTCHECKSET - loaded " + std::to_string(added) + " new and " + std::to_string(replaced) +
                  " replaced check set(s) from \"" + Trim(f[1]) + "\".");
    return;
  }
  if (verb == "save" && f.size() == 3) {
    const int idx = FindCheckSetIndex(st, f[1]);
    if (idx < 0) {
      log.push_back("ALIGNMENTCHECKSET - no check set named \"" + Trim(f[1]) + "\".");
      return;
    }
    std::ofstream out(Trim(f[2]), std::ios::binary | std::ios::trunc);
    out << AlignmentCheckSetsToJsonText({st.drawing.alignmentCheckSets[static_cast<size_t>(idx)]});
    if (!out.good()) {
      log.push_back("ALIGNMENTCHECKSET - could not write \"" + Trim(f[2]) + "\".");
      return;
    }
    log.push_back("ALIGNMENTCHECKSET - \"" + st.drawing.alignmentCheckSets[static_cast<size_t>(idx)].name + "\" saved to \"" +
                  Trim(f[2]) + "\".");
    return;
  }
  log.push_back(usage);
}

// ALIGNMENTCRITERIA <alignment>, <set>|NONE
void RunAlignmentCriteria(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("ALIGNMENTCRITERIA - usage: ALIGNMENTCRITERIA <alignment>, <set>|NONE.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "ALIGNMENTCRITERIA", f[0], log);
  if (a == nullptr) return;
  const size_t ai = static_cast<size_t>(FindAlignment(st, a->name));
  const std::string want = Trim(f[1]);
  std::string setName;
  if (Lower(want) != "none") {
    const int idx = FindCheckSetIndex(st, want);
    if (idx < 0) {
      log.push_back("ALIGNMENTCRITERIA - no check set named \"" + want + "\".");
      return;
    }
    const alignment::AlignmentCheckSet& set = st.drawing.alignmentCheckSets[static_cast<size_t>(idx)];
    std::string why;
    if (!alignment::CriteriaAllowed(a->type, set.kind, &why)) {
      log.push_back("ALIGNMENTCRITERIA - " + why + ".");
      return;
    }
    setName = set.name;
  }
  if (setName == st.drawing.alignments[ai].checkSetName) {
    log.push_back("ALIGNMENTCRITERIA - \"" + a->name + "\" already uses " +
                  (setName.empty() ? std::string("no criteria.") : "\"" + setName + "\"."));
    return;
  }
  PushUndoSnapshot(st, "Alignment criteria");
  st.drawing.alignments[ai].checkSetName = setName;
  log.push_back("ALIGNMENTCRITERIA - \"" + st.drawing.alignments[ai].name + "\" " +
                (setName.empty() ? std::string("has no design criteria.") : "is checked against \"" + setName + "\"."));
}

// ALIGNMENTSPEED <alignment>, <station>, <speed>|CLEAR
void RunAlignmentSpeed(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 3) {
    log.push_back("ALIGNMENTSPEED - usage: ALIGNMENTSPEED <alignment>, <station>, <speed>|CLEAR.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "ALIGNMENTSPEED", f[0], log);
  if (a == nullptr) return;
  double displayed = 0.0;
  if (!ParseSta(st, f[1], &displayed)) {
    log.push_back("ALIGNMENTSPEED - \"" + f[1] + "\" is not a station.");
    return;
  }
  const std::string value = Trim(f[2]);
  const bool clear = Lower(value) == "clear";
  double speed = 0.0;
  if (!clear && (!ParseNum(value, &speed) || speed <= 0.0)) {
    log.push_back("ALIGNMENTSPEED - the speed must be a number above zero, or CLEAR.");
    return;
  }
  // Design speeds are stored at internal stations, like every other station on the alignment.
  const std::vector<double> raws = a->RawStationsForDisplayed(displayed);
  if (raws.empty()) {
    log.push_back("ALIGNMENTSPEED - station " + StaFmt(st, displayed) + " is outside \"" + a->name + "\".");
    return;
  }
  const double internal = raws.front();
  const size_t ai = static_cast<size_t>(FindAlignment(st, a->name));
  const auto sameStation = [internal](const alignment::DesignSpeed& d) { return std::fabs(d.internalStation - internal) < 1e-6; };
  const auto& current = st.drawing.alignments[ai].designSpeeds;
  const auto found = std::find_if(current.begin(), current.end(), sameStation);
  if (clear) {
    if (found == current.end()) {
      log.push_back("ALIGNMENTSPEED - no design speed is set at " + StaFmt(st, displayed) + ".");
      return;
    }
    PushUndoSnapshot(st, "Clear design speed");
    auto& after = st.drawing.alignments[ai].designSpeeds;
    after.erase(std::remove_if(after.begin(), after.end(), sameStation), after.end());
    log.push_back("ALIGNMENTSPEED - design speed cleared at " + StaFmt(st, displayed) + ".");
    return;
  }
  if (found != current.end() && found->speed == speed) {
    log.push_back("ALIGNMENTSPEED - the design speed at " + StaFmt(st, displayed) + " is already " +
                  FormatLinear(speed, 2) + ".");
    return;
  }
  PushUndoSnapshot(st, "Set design speed");
  auto& after = st.drawing.alignments[ai].designSpeeds;
  const auto again = std::find_if(after.begin(), after.end(), sameStation);
  if (again != after.end()) {
    again->speed = speed;
  } else {
    after.push_back({internal, speed});
  }
  std::sort(after.begin(), after.end(), [](const alignment::DesignSpeed& x, const alignment::DesignSpeed& y) {
    return x.internalStation < y.internalStation;
  });
  log.push_back("ALIGNMENTSPEED - design speed " + FormatLinear(speed, 2) + " from station " +
                StaFmt(st, displayed) + ".");
}

// ALIGNMENTCHECK <alignment>: every failed check against the alignment's criteria, in travel order.
void RunAlignmentCheck(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 1) {
    log.push_back("ALIGNMENTCHECK - usage: ALIGNMENTCHECK <alignment>.");
    return;
  }
  const alignment::Alignment* a = Resolve(st, "ALIGNMENTCHECK", f[0], log);
  if (a == nullptr) return;
  const std::string criteriaName = AlignmentStyles::EffectiveCheckSetName(st.drawing.alignmentStyles, *a);
  if (criteriaName.empty()) {
    log.push_back("ALIGNMENTCHECK - \"" + a->name + "\" has no design criteria. Apply a set with ALIGNMENTCRITERIA.");
    return;
  }
  const int idx = FindCheckSetIndex(st, criteriaName);
  if (idx < 0) {
    log.push_back("ALIGNMENTCHECK - the check set \"" + criteriaName + "\" is missing.");
    return;
  }
  const alignment::AlignmentCheckSet& set = st.drawing.alignmentCheckSets[static_cast<size_t>(idx)];
  const std::vector<alignment::DesignFinding> findings = alignment::CheckDesign(*a, set, st.displayLinearPrecision);
  if (findings.empty()) {
    log.push_back("ALIGNMENTCHECK - \"" + a->name + "\" meets \"" + set.name + "\".");
    return;
  }
  log.push_back("ALIGNMENTCHECK - " + std::to_string(findings.size()) + " finding(s) for \"" + a->name + "\" against \"" +
                set.name + "\":");
  for (const alignment::DesignFinding& fd : findings) {
    log.push_back("  Entity " + std::to_string(fd.entity) + ": " + fd.text);
  }
}

// ALIGNMENTINVERSE <alignment>, STA, <station1>, <station2>[, CHORD]     stations as displayed (12+45.32)
//                  <alignment>, PT, <x1>, <y1>, <x2>, <y2>[, CHORD]     points, projected onto the alignment (REQ-501.3)
// Reports the distance along the alignment between the two stations (the arc length for a curve). CHORD also gives the
// straight distance between the two points, which the spec's option asks for.
void RunAlignmentInverse(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTINVERSE - usage: ALIGNMENTINVERSE <alignment>, STA, <station1>, <station2>[, CHORD] | "
      "<alignment>, PT, <x1>, <y1>, <x2>, <y2>[, CHORD].";
  if (f.size() < 4) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTINVERSE", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("ALIGNMENTINVERSE - \"" + a->name + "\" has no geometry.");
    return;
  }
  const std::string mode = Lower(f[1]);
  const bool chordWanted = Lower(f.back()) == "chord";
  double internal1 = 0.0, internal2 = 0.0;
  double px1 = 0.0, py1 = 0.0, px2 = 0.0, py2 = 0.0;
  if (mode == "sta" && (f.size() == 4 || (f.size() == 5 && chordWanted))) {
    double s1 = 0.0, s2 = 0.0;
    if (!ParseSta(st, f[2], &s1) || !ParseSta(st, f[3], &s2)) {
      log.push_back("ALIGNMENTINVERSE - the stations must be stations (12+45.32).");
      return;
    }
    const std::vector<double> r1 = a->RawStationsForDisplayed(s1), r2 = a->RawStationsForDisplayed(s2);
    if (r1.empty() || r2.empty()) {
      log.push_back("ALIGNMENTINVERSE - a station is outside \"" + a->name + "\".");
      return;
    }
    internal1 = r1.front();
    internal2 = r2.front();
    const alignment::Point2 p1 = a->GetPointAtStation(internal1), p2 = a->GetPointAtStation(internal2);
    px1 = p1.x; py1 = p1.y; px2 = p2.x; py2 = p2.y;
  } else if (mode == "pt" && (f.size() == 6 || (f.size() == 7 && chordWanted))) {
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    if (!ParseNum(f[2], &x1) || !ParseNum(f[3], &y1) || !ParseNum(f[4], &x2) || !ParseNum(f[5], &y2)) {
      log.push_back("ALIGNMENTINVERSE - the points must be two numbers each.");
      return;
    }
    double lx1 = 0.0, ly1 = 0.0, lx2 = 0.0, ly2 = 0.0;
    CadCoord::LocalFromWorld(st, x1, y1, &lx1, &ly1);
    CadCoord::LocalFromWorld(st, x2, y2, &lx2, &ly2);
    internal1 = a->GetStationOffset(lx1, ly1).internalStation;
    internal2 = a->GetStationOffset(lx2, ly2).internalStation;
    const alignment::Point2 p1 = a->GetPointAtStation(internal1), p2 = a->GetPointAtStation(internal2);
    px1 = p1.x; py1 = p1.y; px2 = p2.x; py2 = p2.y;
  } else {
    log.push_back(usage);
    return;
  }
  const double arc = std::fabs(internal2 - internal1);
  const double chord = std::hypot(px2 - px1, py2 - py1);
  const int p = st.displayLinearPrecision;
  std::string line = "ALIGNMENTINVERSE - \"" + a->name + "\" from station " +
                     StaFmt(st, a->DisplayedStation(internal1, true)) + " to " +
                     StaFmt(st, a->DisplayedStation(internal2, true)) + ": distance along " + FormatLinear(arc, p);
  if (chordWanted) line += ", chord " + FormatLinear(chord, p);
  log.push_back(line + ".");
}

// ALIGNMENTBEARING <alignment>, <station>   the direction of travel at a station (REQ-501.3)
void RunAlignmentBearing(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("ALIGNMENTBEARING - usage: ALIGNMENTBEARING <alignment>, <station>.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTBEARING", f[0], log);
  if (a == nullptr) return;
  double displayed = 0.0;
  if (!ParseSta(st, f[1], &displayed)) {
    log.push_back("ALIGNMENTBEARING - \"" + f[1] + "\" is not a station.");
    return;
  }
  const std::vector<double> raws = a->RawStationsForDisplayed(displayed);
  if (raws.empty()) {
    log.push_back("ALIGNMENTBEARING - station " + StaFmt(st, displayed) + " is outside \"" + a->name + "\".");
    return;
  }
  const double az = a->GetBearingAtStation(raws.front());
  log.push_back("ALIGNMENTBEARING - \"" + a->name + "\" at " + StaFmt(st, displayed) + ": " +
                FormatBearing(az * 180.0 / alignment::kPi, CadAngleDisplaySettings(st)) + ".");
}

// ALIGNMENTDISTANCE <alignment>, <x>, <y>   the distance along the alignment to the point nearest (x, y) (REQ-501.3)
void RunAlignmentDistance(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  double x = 0.0, y = 0.0;
  if (f.size() != 3 || !ParseNum(f[1], &x) || !ParseNum(f[2], &y)) {
    log.push_back("ALIGNMENTDISTANCE - usage: ALIGNMENTDISTANCE <alignment>, <x>, <y>.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTDISTANCE", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("ALIGNMENTDISTANCE - \"" + a->name + "\" has no geometry.");
    return;
  }
  double lx = 0.0, ly = 0.0;
  CadCoord::LocalFromWorld(st, x, y, &lx, &ly);
  const alignment::StationOffset so = a->GetStationOffset(lx, ly);
  log.push_back("ALIGNMENTDISTANCE - \"" + a->name + "\": " + FormatLinear(a->GetDistanceAlongAlignment(lx, ly), st.displayLinearPrecision) +
                " along from the start, nearest at station " + StaFmt(st, a->DisplayedStation(so.internalStation, true)) +
                (so.outOfRange ? " (beyond an end)" : "") + ".");
}

// ----------------------------------------------------------------------------------------------------------------
// REQ-485: create alignments. The Create Alignment dialogs submit everything they collect as KEY=value fields, so the
// dialog, the command line and a transcript all go through one typed, undoable command.
// ----------------------------------------------------------------------------------------------------------------
namespace {

bool ParseAlignmentType(const std::string& text, alignment::AlignmentType* out) {
  assert(out != nullptr);
  std::string t;
  for (const char c : Lower(text)) {
    if (c != ' ' && c != '_' && c != '-') t.push_back(c);
  }
  assert(t.size() <= text.size());
  using T = alignment::AlignmentType;
  if (t == "centerline" || t == "centreline") *out = T::Centerline;
  else if (t == "offset") *out = T::Offset;
  else if (t == "curbreturn") *out = T::CurbReturn;
  else if (t == "miscellaneous" || t == "misc") *out = T::Miscellaneous;
  else if (t == "rail") *out = T::Rail;
  else return false;
  return true;
}

struct CreateOptions {
  std::optional<alignment::AlignmentType> type;
  std::optional<std::string> description, layer, style, labelSet, criteria;
  std::optional<double> start, speed, fillet, tolerance;
  std::optional<alignment::Point2> pick;  ///< WORLD coordinates of the first pick.
  bool erase = false;
  bool reverse = false;
};

enum class OptionParse { Positional, Taken, Bad };

/// "x y" (WORLD), the form a picked point takes inside one comma-separated field.
bool ParseSpacedPoint(const std::string& text, alignment::Point2* out) {
  assert(out != nullptr);
  std::istringstream in(text);
  std::string xs, ys, extra;
  in >> xs >> ys;
  const bool ok = !(in >> extra) && ParseNum(xs, &out->x) && ParseNum(ys, &out->y);
  assert(!ok || (std::isfinite(out->x) && std::isfinite(out->y)));
  return ok;
}

OptionParse ParseNumberOption(const std::string& key, const std::string& val, bool fromObjects, CreateOptions* o,
                              std::string* why) {
  assert(o != nullptr && why != nullptr);
  assert(!key.empty());
  double d = 0.0;
  if (key == "start") {
    if (!alignment::ParseStation(val, &d)) *why = "\"" + val + "\" is not a station (try 5+00 or 500)";
    else o->start = d;
  } else if (key == "speed") {
    if (!ParseNum(val, &d) || d <= 0.0) *why = "the design speed must be a number above zero";
    else o->speed = d;
  } else if (fromObjects && key == "fillet") {
    if (!ParseNum(val, &d) || d <= 0.0) *why = "the curve radius must be a number above zero";
    else o->fillet = d;
  } else if (fromObjects && key == "tol") {
    if (!ParseNum(val, &d) || d <= 0.0 || d > 1.0) *why = "the gap tolerance must be above 0 and at most 1.0";
    else o->tolerance = d;
  } else if (fromObjects && key == "pick") {
    alignment::Point2 p;
    if (!ParseSpacedPoint(val, &p)) *why = "PICK takes a point as \"<x> <y>\"";
    else o->pick = p;
  } else {
    return OptionParse::Positional;
  }
  return why->empty() ? OptionParse::Taken : OptionParse::Bad;
}

OptionParse ParseCreateOption(const std::string& field, bool fromObjects, CreateOptions* o, std::string* why) {
  assert(o != nullptr && why != nullptr);
  why->clear();
  const size_t eq = field.find('=');
  const std::string key = Lower(Trim(field.substr(0, eq)));
  if (eq == std::string::npos) {
    if (fromObjects && (key == "erase" || key == "reverse" || key == "r")) {
      (key == "erase" ? o->erase : o->reverse) = true;
      return OptionParse::Taken;
    }
    return OptionParse::Positional;
  }
  const std::string val = Trim(field.substr(eq + 1));
  if (key == "type") {
    alignment::AlignmentType t = alignment::AlignmentType::Centerline;
    if (!ParseAlignmentType(val, &t)) {
      *why = "\"" + val + "\" is not an alignment type (Centerline, Offset, Curb Return, Miscellaneous or Rail)";
      return OptionParse::Bad;
    }
    o->type = t;
  } else if (key == "description") o->description = val;
  else if (key == "layer") o->layer = val;
  else if (key == "style") o->style = val;
  else if (key == "labelset") o->labelSet = val;
  else if (key == "criteria") o->criteria = val;
  else if (ParseNumberOption(key, val, fromObjects, o, why) == OptionParse::Positional) {
    *why = "unrecognised option \"" + field + "\"";
    return OptionParse::Bad;
  }
  return why->empty() ? OptionParse::Taken : OptionParse::Bad;
}

/// Splits the fields of a create command into positional values and KEY=value options.
bool SplitCreateFields(const std::string& args, bool fromObjects, std::vector<std::string>* positional,
                       CreateOptions* o, std::string* why) {
  assert(positional != nullptr && o != nullptr && why != nullptr);
  const std::vector<std::string> fields = SplitFields(args);
  for (const std::string& f : fields) {
    const OptionParse r = ParseCreateOption(f, fromObjects, o, why);
    if (r == OptionParse::Bad) return false;
    if (r == OptionParse::Positional) positional->push_back(f);
  }
  assert(positional->size() <= fields.size());
  return true;
}

/// Applies the dialog's fields to a new alignment; false names the first one that cannot be used.
bool ApplyCreateOptions(AppCommandState& st, const CreateOptions& o, alignment::Alignment* a, std::string* why) {
  assert(a != nullptr && why != nullptr);
  assert(!a->name.empty());
  if (o.type) a->type = *o.type;
  if (o.description) a->description = *o.description;
  if (o.layer && !o.layer->empty()) a->layer = *o.layer;
  if (o.style && !o.style->empty()) {
    if (AlignmentStyles::Find(st.drawing.alignmentStyles, *o.style) == nullptr) {
      *why = "no alignment style named \"" + *o.style + "\"";
      return false;
    }
    a->style = *o.style;
  }
  if (o.labelSet && !o.labelSet->empty()) {
    if (AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, *o.labelSet) == nullptr) {
      *why = "no alignment label set named \"" + *o.labelSet + "\"";
      return false;
    }
    a->labelSet = *o.labelSet;
  }
  if (o.start) a->startingStation = *o.start;
  if (o.criteria && !o.criteria->empty() && Lower(*o.criteria) != "none") {
    const int idx = FindCheckSetIndex(st, *o.criteria);
    if (idx < 0) {
      *why = "no check set named \"" + *o.criteria + "\"";
      return false;
    }
    const alignment::AlignmentCheckSet& set = st.drawing.alignmentCheckSets[static_cast<size_t>(idx)];
    if (!alignment::CriteriaAllowed(a->type, set.kind, why)) return false;
    a->checkSetName = set.name;
  }
  if (o.speed) {
    alignment::DesignSpeed speed;
    speed.internalStation = a->startingStation;
    speed.speed = *o.speed;
    a->designSpeeds = {speed};
  }
  return true;
}

alignment::Alignment NewAlignment(const AppCommandState& st, const std::string& name) {
  assert(!name.empty());
  alignment::Alignment a;
  a.name = name;
  a.layer = MakeNewEntityAttrs(st).layer;
  if (a.layer.empty()) a.layer = "0";
  if (AlignmentStyles::Find(st.drawing.alignmentStyles, st.drawing.alignmentStyleCurrent) != nullptr) a.style = st.drawing.alignmentStyleCurrent;
  assert(!a.layer.empty());
  return a;
}

}  // namespace

void RunAlignmentMeta(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  assert(st.drawing.alignments.size() < st.drawing.alignments.max_size());
  assert(args.size() < 65536);
  const std::vector<std::string> f = SplitFields(args);
  const char* usage = "ALIGNMENTMETA - usage: ALIGNMENTMETA <alignment>, SET, <name>, <value> | <alignment>, REMOVE, <name> | "
                      "<alignment>, LIST.";
  if (f.size() < 2) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTMETA", f[0], log);
  if (a == nullptr) return;
  const std::string verb = Lower(f[1]);
  auto find = [&](const std::string& key) {
    return std::find_if(a->metadata.begin(), a->metadata.end(), [&](const auto& kv) { return Lower(kv.first) == Lower(key); });
  };
  if (verb == "list" && f.size() == 2) {
    if (a->metadata.empty()) log.push_back("ALIGNMENTMETA - \"" + a->name + "\" has no metadata.");
    for (const auto& [key, value] : a->metadata) log.push_back("  " + key + " = " + value);
    return;
  }
  if (verb == "set" && f.size() == 4 && !Trim(f[2]).empty()) {
    PushUndoSnapshot(st, "Alignment metadata");
    a = Resolve(st, "ALIGNMENTMETA", f[0], log);
    const auto it = find(Trim(f[2]));
    if (it != a->metadata.end()) it->second = f[3];
    else a->metadata.emplace_back(Trim(f[2]), f[3]);
    log.push_back("ALIGNMENTMETA - \"" + a->name + "\": " + Trim(f[2]) + " = " + f[3] + ".");
    return;
  }
  if (verb == "remove" && f.size() == 3) {
    if (find(Trim(f[2])) == a->metadata.end()) {
      log.push_back("ALIGNMENTMETA - \"" + a->name + "\" has no \"" + Trim(f[2]) + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment metadata");
    a = Resolve(st, "ALIGNMENTMETA", f[0], log);
    a->metadata.erase(find(Trim(f[2])));
    log.push_back("ALIGNMENTMETA - \"" + Trim(f[2]) + "\" removed from \"" + a->name + "\".");
    return;
  }
  log.push_back(usage);
}

void RunStationFormat(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  assert(st.drawing.alignmentStationFormat.decimals >= 0);
  assert(args.size() < 4096);
  const std::vector<std::string> f = SplitFields(args);
  alignment::StationFormat fmt = st.drawing.alignmentStationFormat;
  const char* usage = "STATIONFORMAT - usage: STATIONFORMAT [DELIMITER=+|-|_|/|C|K|NONE][, DIGITS=1..6][, DECIMALS=0..6]"
                      "[, PREFIX=<text>][, SUFFIX=<text>] | DEFAULT.";
  if (f.size() == 1 && Lower(Trim(f[0])) == "default") fmt = alignment::StationFormat{};
  for (const std::string& field : f) {
    if (Trim(field).empty() || Lower(Trim(field)) == "default") continue;
    const size_t eq = field.find('=');
    if (eq == std::string::npos) {
      log.push_back(usage);
      return;
    }
    const std::string key = Lower(Trim(field.substr(0, eq)));
    const std::string value = field.substr(eq + 1);  // prefix and suffix keep their spaces ("STA ")
    double n = 0.0;
    if (key == "delimiter" || key == "delim") {
      const std::string v = Trim(value);
      if (Lower(v) == "none") fmt.delimiter = '\0';
      else if (v.size() == 1 && std::string("+-_/CcKk").find(v[0]) != std::string::npos)
        fmt.delimiter = static_cast<char>(std::toupper(static_cast<unsigned char>(v[0])));
      else {
        log.push_back("STATIONFORMAT - the delimiter must be +, -, _, /, C, K or NONE.");
        return;
      }
    } else if (key == "digits" && ParseNum(value, &n) && n >= 1 && n <= 6 && n == std::floor(n)) {
      fmt.digitsRight = static_cast<int>(n);
    } else if (key == "decimals" && ParseNum(value, &n) && n >= 0 && n <= 6 && n == std::floor(n)) {
      fmt.decimals = static_cast<int>(n);
    } else if (key == "prefix") {
      fmt.prefix = value;
    } else if (key == "suffix") {
      fmt.suffix = value;
    } else {
      log.push_back(usage);
      return;
    }
  }
  if (!f.empty() && !(f.size() == 1 && Trim(f[0]).empty())) {
    PushUndoSnapshot(st, "Station format");
    st.drawing.alignmentStationFormat = fmt;
  }
  log.push_back("STATIONFORMAT - stations are written like " + StaFmt(st, 1245.32) + " (delimiter " +
                (st.drawing.alignmentStationFormat.delimiter == '\0' ? std::string("none")
                                                             : std::string(1, st.drawing.alignmentStationFormat.delimiter)) +
                ", " + std::to_string(st.drawing.alignmentStationFormat.digitsRight) + " digit(s) after it, " +
                std::to_string(st.drawing.alignmentStationFormat.decimals) + " decimal(s)).");
}

std::string UniqueAlignmentName(const AppCommandState& st, const std::string& base) {
  assert(!base.empty());
  assert(st.drawing.alignments.size() < st.drawing.alignments.max_size());
  if (FindAlignment(st, base) < 0) return base;
  for (size_t n = 2; n <= st.drawing.alignments.size() + 2; ++n) {  // one of these is free: there are only so many names
    const std::string candidate = base + " (" + std::to_string(n) + ")";
    if (FindAlignment(st, candidate) < 0) return candidate;
  }
  return base + " (" + std::to_string(st.drawing.alignments.size() + 3) + ")";
}

std::string NextAlignmentName(const AppCommandState& st) {
  assert(st.drawing.alignments.size() < st.drawing.alignments.max_size());
  for (size_t n = 1; n <= st.drawing.alignments.size() + 1; ++n) {
    const std::string name = "Alignment - (" + std::to_string(n) + ")";
    if (FindAlignment(st, name) < 0) return name;
  }
  assert(false && "a free counter always exists among n+1 candidates");
  return "Alignment";
}

double EffectiveAlignmentCurveRadius(const AppCommandState& st) {
  assert(st.alignmentCurveSettings.radius >= 0.0);
  if (st.alignmentCurveSettings.radius > 0.0) return st.alignmentCurveSettings.radius;
  // REQ-485.2: 100 ft, or 30 m in a metric drawing ($INSUNITS 6 metres, 4 millimetres).
  const double r = st.drawing.drawingInsUnits == 6 ? 30.0 : st.drawing.drawingInsUnits == 4 ? 30000.0 : 100.0;
  assert(r > 0.0);
  return r;
}

// ALIGNMENT <name>[, <description>[, <starting station>[, <layer>[, <style>]]]][, TYPE=][, LABELSET=][, CRITERIA=]
//   [, SPEED=]  (REQ-480, REQ-485.1). With no arguments the Qt editor opens the Create Alignment dialog.
void RunAlignmentCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  assert(st.drawing.alignments.size() < st.drawing.alignments.max_size());
  if (Trim(args).empty() && st.createAlignmentDialogAvailable) {
    st.createAlignmentDialogRequested = true;
    log.push_back("ALIGNMENT - opening the Create Alignment dialog.");
    return;
  }
  CreateOptions o;
  std::vector<std::string> pos;
  std::string why;
  if (!SplitCreateFields(args, false, &pos, &o, &why)) {
    log.push_back("ALIGNMENT - " + why + ".");
    return;
  }
  if (pos.empty() || pos[0].empty() || pos.size() > 5) {
    log.push_back("ALIGNMENT - usage: ALIGNMENT <name>[, <description>[, <starting station>[, <layer>[, <style>]]]]"
                  "[, TYPE=<type>][, LABELSET=<set>][, CRITERIA=<check set>][, SPEED=<design speed>].");
    return;
  }
  if (FindAlignment(st, pos[0]) >= 0) {
    log.push_back("ALIGNMENT - an alignment named \"" + pos[0] + "\" already exists; names must be unique.");
    return;
  }
  if (pos.size() > 1 && !o.description) o.description = pos[1];
  if (pos.size() > 2 && !pos[2].empty() && ParseNumberOption("start", pos[2], false, &o, &why) != OptionParse::Taken) {
    log.push_back("ALIGNMENT - " + why + ".");
    return;
  }
  if (pos.size() > 3 && !pos[3].empty()) o.layer = pos[3];
  if (pos.size() > 4 && !pos[4].empty()) o.style = pos[4];
  alignment::Alignment a = NewAlignment(st, pos[0]);
  if (!ApplyCreateOptions(st, o, &a, &why)) {
    log.push_back("ALIGNMENT - " + why + ". No alignment was created.");
    return;
  }
  PushUndoSnapshot(st, "Create alignment");
  a.id = st.nextEntityId++;
  st.drawing.alignments.push_back(std::move(a));
  const alignment::Alignment& made = st.drawing.alignments.back();
  log.push_back("ALIGNMENT - created \"" + made.name + "\" (" + TypeName(made.type) + ", starting station " +
                StaFmt(st, made.startingStation) + "). Lay it out with ALIGNMENTLAYOUT or ALIGNMENTADD.");
}

namespace {

/// What an unusable source is, for the refusal (REQ-485.3 / REQ-201).
const char* SourceKindName(SelectedEntity::Type t) {
  using T = SelectedEntity::Type;
  switch (t) {
    case T::Circle: return "a circle";
    case T::Ellipse: return "an ellipse";
    case T::Spline: return "a spline";
    case T::Annotation: return "text or a dimension";
    case T::BlockRef: return "a block reference";
    case T::Point: return "a point";
    case T::Alignment: return "an alignment";
    case T::Solid: return "a solid";
    default: return "not a line, arc, polyline or feature line";
  }
}

/// Plan segments of one selected source. Z is ignored (an alignment is plan geometry, REQ-480.3) but `zSeen` reports
/// it; a vertical line, a tilted arc, a 3D polyline and anything that is not linework are refused by name.
bool AddSourceSegments(const AppCommandState& st, const SelectedEntity& se, int srcNo,
                       std::vector<alignment::SourceSegment>* segs, bool* zSeen, std::string* why) {
  assert(segs != nullptr && zSeen != nullptr && why != nullptr);
  assert(srcNo > 0);
  using T = SelectedEntity::Type;
  const size_t ix = static_cast<size_t>(se.index);
  const std::string obj = "object " + std::to_string(srcNo);
  if (se.type == T::LineSeg && se.index >= 0 && ix < st.drawing.lineCount()) {
    const CadLineRecord v = st.drawing.line(ix);
    if (std::hypot(v.x1 - v.x0, v.y1 - v.y0) <= 1e-9 && std::fabs(v.z1 - v.z0) > 1e-9) {
      *why = obj + " is a vertical line; it has no length in plan";
      return false;
    }
    alignment::SourceSegment s;
    s.a = {v.x0, v.y0};
    s.b = {v.x1, v.y1};
    s.sourceIndex = srcNo;
    segs->push_back(s);
    *zSeen = *zSeen || v.z0 != 0.0 || v.z1 != 0.0;
    return true;
  }
  if (se.type == T::Arc && se.index >= 0 && ix < st.drawing.userArcs.size()) {
    const CadArc& ar = st.drawing.userArcs[ix];
    if (std::fabs(ar.nx) > 1e-6 || std::fabs(ar.ny) > 1e-6) {
      *why = obj + " is an arc that is not in plan (its plane is tilted)";
      return false;
    }
    alignment::SourceSegment s;
    s.isArc = true;
    s.center = {ar.cx, ar.cy};
    s.radius = ar.r;
    const double a0 = ar.startRad, sw = ar.sweepRad;
    s.startAngle = sw >= 0.0 ? a0 : a0 + sw;
    s.endAngle = s.startAngle + std::fabs(sw);
    s.sourceIndex = srcNo;
    segs->push_back(s);
    *zSeen = *zSeen || ar.z != 0.0;
    return true;
  }
  *why = obj + " is " + SourceKindName(se.type);
  return false;
}

/// Polyline and feature-line sources: one segment per span (a bulge span becomes a true arc).
bool AddVertexSourceSegments(const AppCommandState& st, const SelectedEntity& se, int srcNo,
                             std::vector<alignment::SourceSegment>* segs, bool* zSeen, std::string* why) {
  assert(segs != nullptr && zSeen != nullptr && why != nullptr);
  assert(se.type == SelectedEntity::Type::Polyline || se.type == SelectedEntity::Type::FeatureLine);
  const bool feature = se.type == SelectedEntity::Type::FeatureLine;
  const std::vector<int>& offsets = feature ? st.drawing.featureLineOffsets : st.drawing.userPolylineOffsets;
  const size_t ix = static_cast<size_t>(se.index);
  if (se.index < 0 || ix + 1 >= offsets.size()) {
    *why = "object " + std::to_string(srcNo) + " no longer exists";
    return false;
  }
  const int v0 = offsets[ix], cnt = offsets[ix + 1] - offsets[ix];
  const bool closed = feature ? (ix < st.drawing.featureLineClosed.size() && st.drawing.featureLineClosed[ix] != 0)
                              : (ix < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[ix] != 0);
  auto vert = [&](int k, int c) {
    const size_t vi = static_cast<size_t>(v0 + k);
    return feature ? st.drawing.featureLineVerts[vi * 3 + static_cast<size_t>(c)] : (c == 0 ? st.drawing.vertex(vi).x : c == 1 ? st.drawing.vertex(vi).y : st.drawing.vertex(vi).z);
  };
  double zMin = std::numeric_limits<double>::infinity(), zMax = -zMin;
  for (int k = 0; k < cnt; ++k) {
    zMin = std::min(zMin, vert(k, 2));
    zMax = std::max(zMax, vert(k, 2));
  }
  if (!feature && cnt > 0 && zMax - zMin > 1e-9) {  // REQ-485.3: a 3D polyline is non-planar; a feature line is not
    *why = "object " + std::to_string(srcNo) + " is a 3D polyline (its vertices are at different elevations)";
    return false;
  }
  *zSeen = *zSeen || (cnt > 0 && (zMin != 0.0 || zMax != 0.0));
  for (int k = 0; k + 1 < cnt + (closed ? 1 : 0); ++k) {
    const int k1 = (k + 1) % cnt;
    const alignment::Point2 p0{vert(k, 0), vert(k, 1)}, p1{vert(k1, 0), vert(k1, 1)};
    const size_t bi = static_cast<size_t>(v0 + k);
    const double bulge = !feature && bi < st.drawing.userPolylineVertsBulge.size() ? st.drawing.userPolylineVertsBulge[bi] : 0.0;
    segs->push_back(alignment::SegmentFromBulge(p0, p1, bulge, srcNo));
  }
  return true;
}

/// The selection as source segments (REQ-485.2). False names the first object that cannot be used.
bool GatherSourceSegments(const AppCommandState& st, std::vector<alignment::SourceSegment>* segs, bool* zSeen,
                          std::string* why) {
  assert(segs != nullptr && zSeen != nullptr && why != nullptr);
  assert(segs->empty());
  int n = 0;
  for (const SelectedEntity& se : st.selection) {
    const int srcNo = ++n;
    const bool vertexSource = se.type == SelectedEntity::Type::Polyline || se.type == SelectedEntity::Type::FeatureLine;
    const bool ok = vertexSource ? AddVertexSourceSegments(st, se, srcNo, segs, zSeen, why)
                                 : AddSourceSegments(st, se, srcNo, segs, zSeen, why);
    if (!ok) return false;
  }
  return true;
}

/// Adds the alignment in one undo step. With ERASE the sources are deleted first; the delete's snapshot is the step,
/// so one UNDO restores them and removes the alignment together (REQ-485 acceptance 6).
void CommitFromObjects(AppCommandState& st, alignment::Alignment made, bool erase) {
  assert(!made.name.empty());
  assert(!made.Empty());
  if (erase && !st.selection.empty()) {
    std::vector<std::string> deleteLog;  // this command reports the result, not the delete
    ExecuteDeleteSelection(st, deleteLog);
    const int idx = st.activeDrawingIdx;
    if (idx >= 0 && static_cast<size_t>(idx) < st.documents.size() && !st.documents[static_cast<size_t>(idx)].undoStack.empty())
      st.documents[static_cast<size_t>(idx)].undoStack.back().description = "Create alignment from objects";
  } else {
    PushUndoSnapshot(st, "Create alignment from objects");
  }
  made.id = st.nextEntityId++;
  st.drawing.alignments.push_back(std::move(made));
  st.selection.clear();
  BumpCadGpuCache(st);
}

void StartAlignmentFromObjects(AppCommandState& st, std::vector<std::string>& log);

}  // namespace

// CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE][, TOL=<gap>][, ERASE]
//   [, PICK=<x> <y>][, TYPE=][, DESCRIPTION=][, LAYER=][, STYLE=][, LABELSET=][, CRITERIA=][, SPEED=]
// Converts the SELECTED lines, arcs, 2D polylines and feature lines (REQ-485.2). Line -> Tangent, Arc -> Curve, a
// polyline bulge segment -> Curve; never a vertex approximation. With no arguments the objects are picked first.
void RunCreateAlignmentFromObjects(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  assert(st.drawing.alignments.size() < st.drawing.alignments.max_size());
  if (Trim(args).empty()) {
    StartAlignmentFromObjects(st, log);
    return;
  }
  const std::string cmd = "CREATEALIGNMENTFROMOBJECTS - ";
  CreateOptions o;
  std::vector<std::string> pos;
  std::string why;
  if (!SplitCreateFields(args, true, &pos, &o, &why)) {
    log.push_back(cmd + why + ".");
    return;
  }
  if (pos.size() != 1 || pos[0].empty()) {
    log.push_back(cmd + "usage: CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE]"
                        "[, TOL=<gap>][, ERASE][, PICK=<x> <y>] (select the objects first), or no arguments to pick them.");
    return;
  }
  if (FindAlignment(st, pos[0]) >= 0) {
    log.push_back(cmd + "an alignment named \"" + pos[0] + "\" already exists; names must be unique.");
    return;
  }
  if (st.selection.empty()) {
    log.push_back(cmd + "select the lines, arcs or polylines to convert first.");
    return;
  }
  std::vector<alignment::SourceSegment> segs;
  bool zSeen = false;
  if (!GatherSourceSegments(st, &segs, &zSeen, &why)) {
    log.push_back(cmd + why + ". No alignment was created.");
    return;
  }
  alignment::FromObjectsOptions opt;
  opt.reverse = o.reverse;
  opt.addFillets = o.fillet.has_value();
  opt.filletRadius = o.fillet.value_or(opt.filletRadius);
  opt.gapTolerance = o.tolerance.value_or(opt.gapTolerance);
  if (o.pick) {
    opt.hasPick = true;
    CadCoord::LocalFromWorld(st, o.pick->x, o.pick->y, &opt.pick.x, &opt.pick.y);
  }
  alignment::Alignment made;
  std::vector<std::string> warn;
  if (!alignment::BuildFromSegments(segs, opt, &made, &why, &warn)) {
    log.push_back(cmd + why + ". No alignment was created.");
    return;
  }
  const std::vector<alignment::Entity> geometry = made.Entities();
  made = NewAlignment(st, pos[0]);
  for (const alignment::Entity& e : geometry) (void)made.AppendRaw(e);
  if (!ApplyCreateOptions(st, o, &made, &why)) {
    log.push_back(cmd + why + ". No alignment was created.");
    return;
  }
  const size_t sources = st.selection.size();
  CommitFromObjects(st, std::move(made), o.erase);
  const alignment::Alignment& r = st.drawing.alignments.back();
  if (zSeen) log.push_back(cmd + "elevations were ignored; an alignment is plan geometry.");
  for (const std::string& w : warn) log.push_back(cmd + w + ".");
  log.push_back(cmd + "created \"" + r.name + "\": " + std::to_string(r.Entities().size()) + " entities (" +
                std::to_string(r.CurveCount()) + " curves), length " + Lin(st, r.Length()) + ", starting at station " +
                StaFmt(st, r.startingStation) + "." +
                (o.erase ? " " + std::to_string(sources) + " source object(s) erased." : ""));
  for (const alignment::ContinuityIssue& c : r.CheckContinuity()) {
    if (!c.c1 && c.c0) log.push_back("  Note: direction changes after entity " + std::to_string(c.afterEntity + 1) + " (not tangent).");
  }
}

// ----------------------------------------------------------------------------------------------------------------
// REQ-485 interactive: ALIGNMENTLAYOUT (Civil 3D's Tangent-Tangent tools) and CREATEALIGNMENTFROMOBJECTS picking.
// ----------------------------------------------------------------------------------------------------------------
namespace {

std::string FormatWorldNumber(double v) {
  std::array<char, 40> b{};
  std::snprintf(b.data(), b.size(), "%.10f", v);
  assert(std::isfinite(v));
  assert(b[0] != '\0');
  return b.data();
}

bool BeginInteractive(AppCommandState& st, AppCommandState::Kind k, const char* name, std::vector<std::string>& log) {
  assert(k != AppCommandState::Kind::None);
  assert(name != nullptr);
  if (st.active != AppCommandState::Kind::None) {
    log.push_back(std::string(name) + " - finish or cancel the active command first.");
    return false;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  st.active = k;
  st.lastCommand = k;
  return true;
}

/// Rebuilds the alignment being laid out from the session's base and PIs (REQ-485.1). The first change takes the
/// session's one undo snapshot. False (alignment unchanged) when the PIs cannot form an alignment.
bool RebuildLayout(AppCommandState& st, std::vector<std::string>& log) {
  AppCommandState::AlignmentLayoutSession& s = st.alignmentLayout;
  assert(!s.name.empty());
  const int ix = FindAlignment(st, s.name);
  if (ix < 0) return false;
  alignment::Alignment result = s.base;
  if (s.pis.size() >= 2) {
    alignment::CurveSettings cs = st.alignmentCurveSettings;
    cs.radius = EffectiveAlignmentCurveRadius(st);
    std::vector<alignment::Entity> keep = s.base.Entities();
    std::vector<alignment::Point2> pis = s.pis;
    // With curves, the base's last tangent becomes the first leg so the joining PI gets a curve too.
    if (cs.addCurves && !keep.empty() && keep.back().kind == alignment::EntityKind::Line) {
      pis.insert(pis.begin(), keep.back().start);
      keep.pop_back();
    }
    alignment::Alignment tail;
    std::string err;
    std::vector<std::string> warn;
    if (!alignment::BuildFromPis(pis, cs, &tail, &err, &warn)) {
      log.push_back("ALIGNMENTLAYOUT - " + err + ".");
      return false;
    }
    result.Clear();
    for (const alignment::Entity& e : keep) (void)result.AppendRaw(e);
    for (const alignment::Entity& e : tail.Entities()) (void)result.AppendRaw(e);
    for (size_t w = s.warningsShown; w < warn.size(); ++w) log.push_back("ALIGNMENTLAYOUT - " + warn[w] + ".");
    s.warningsShown = warn.size();
  }
  if (!s.snapshotTaken) {
    PushUndoSnapshot(st, "Alignment layout");
    s.snapshotTaken = true;
  }
  st.drawing.alignments[static_cast<size_t>(ix)] = std::move(result);
  BumpCadGpuCache(st);
  assert(st.drawing.alignments[static_cast<size_t>(ix)].name == s.name);
  return true;
}

void FinishLayout(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::AlignmentLayout);
  const int ix = FindAlignment(st, st.alignmentLayout.name);
  st.active = AppCommandState::Kind::None;
  if (ix < 0) return;
  const alignment::Alignment& a = st.drawing.alignments[static_cast<size_t>(ix)];
  assert(a.name == st.alignmentLayout.name);
  if (a.Empty()) {
    log.push_back("ALIGNMENTLAYOUT - \"" + a.name + "\" has no geometry yet.");
    return;
  }
  log.push_back("ALIGNMENTLAYOUT - \"" + a.name + "\": " + std::to_string(a.Entities().size()) + " entities (" +
                std::to_string(a.CurveCount()) + " curves, " + std::to_string(a.SpiralCount()) + " spirals), length " +
                Lin(st, a.Length()) + ", ending at station " + Sta(st, a, a.EndStation()) + ".");
}

bool SubmitLayoutPoint(AppCommandState& st, alignment::Point2 p, std::vector<std::string>& log) {
  AppCommandState::AlignmentLayoutSession& s = st.alignmentLayout;
  assert(st.active == AppCommandState::Kind::AlignmentLayout);
  assert(std::isfinite(p.x) && std::isfinite(p.y));
  if (s.fixedDirection && s.pis.size() == 1) {  // the dialog's starting direction holds the first tangent
    const alignment::Point2 dir{std::sin(s.directionAzimuth), std::cos(s.directionAzimuth)};
    const double t = (p.x - s.pis[0].x) * dir.x + (p.y - s.pis[0].y) * dir.y;
    if (!(t > 1e-9)) {
      log.push_back("ALIGNMENTLAYOUT - the point is behind the starting direction; pick ahead of the start point.");
      return false;
    }
    p = {s.pis[0].x + dir.x * t, s.pis[0].y + dir.y * t};
  }
  if (!s.pis.empty() && std::hypot(p.x - s.pis.back().x, p.y - s.pis.back().y) <= 1e-9) {
    log.push_back("ALIGNMENTLAYOUT - that is the previous point; pick a different one.");
    return false;
  }
  s.pis.push_back(p);
  if (!RebuildLayout(st, log)) {
    s.pis.pop_back();
    return false;
  }
  return true;
}

/// The from-objects result for the current picks, for the direction prompt. False carries the refusal.
bool PreviewFromObjects(const AppCommandState& st, alignment::Alignment* out, std::string* why) {
  assert(out != nullptr && why != nullptr);
  const AppCommandState::AlignmentFromObjectsPick& p = st.alignmentFromObjects;
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  std::vector<alignment::SourceSegment> segs;
  bool zSeen = false;
  if (!GatherSourceSegments(st, &segs, &zSeen, why)) return false;
  alignment::FromObjectsOptions opt;
  opt.hasPick = p.hasPick;
  opt.pick = {p.pickX, p.pickY};
  opt.reverse = p.reverse;
  return alignment::BuildFromSegments(segs, opt, out, why);
}

void PromptDirection(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  st.alignmentFromObjects.phase = AppCommandState::AlignmentFromObjectsPick::Phase::WaitDirection;
  alignment::Alignment preview;
  std::string why;
  if (!PreviewFromObjects(st, &preview, &why)) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - " + why + ". No alignment was created.");
    st.active = AppCommandState::Kind::None;
    st.selection.clear();
    return;
  }
  const alignment::Point2 s = preview.StartPoint();
  assert(!preview.Empty());
  log.push_back("CREATEALIGNMENTFROMOBJECTS - the alignment starts at " + Lin(st, s.x + CadCoord::WorldOriginX(st)) +
                ", " + Lin(st, s.y + CadCoord::WorldOriginY(st)) + " heading " + Brg(st, preview.StartAzimuth()) +
                ". Press Enter to accept the direction or [Reverse]:");
}

void FinishFromObjectsPicks(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  assert(!st.selection.empty());
  st.active = AppCommandState::Kind::None;
  if (st.createAlignmentDialogAvailable) {  // the dialog collects the name and options, then submits the command
    st.alignmentFromObjectsDialogRequested = true;
    return;
  }
  RunCreateAlignmentFromObjects(st, NextAlignmentName(st) + AlignmentFromObjectsPickArgs(st), log);
}

bool HandleFromObjectsText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  using P = AppCommandState::AlignmentFromObjectsPick::Phase;
  AppCommandState::AlignmentFromObjectsPick& p = st.alignmentFromObjects;
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  const std::string t = Lower(Trim(line));
  assert(t.size() <= line.size());
  if (p.phase == P::WaitDirection) {
    if (t.empty()) {
      FinishFromObjectsPicks(st, log);
    } else if (t == "r" || t == "reverse") {
      p.reverse = !p.reverse;
      PromptDirection(st, log);
    } else {
      log.push_back("CREATEALIGNMENTFROMOBJECTS - press Enter to accept the direction, or R to reverse it.");
    }
    return true;
  }
  if (!t.empty()) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - pick lines, arcs, polylines or feature lines in the drawing; Enter when done.");
  } else if (st.selection.empty()) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - nothing is selected yet; pick the first line, arc or polyline, or ESC.");
  } else {
    PromptDirection(st, log);
  }
  return true;
}

/// A typed point for the layout: "x,y" (WORLD) or "@dx,dy" from the previous PI.
bool ParseLayoutPoint(const AppCommandState& st, const std::string& text, alignment::Point2* local) {
  assert(local != nullptr);
  const std::string t = Trim(text);
  const bool relative = !t.empty() && t[0] == '@';
  const std::vector<std::string> f = SplitFields(relative ? t.substr(1) : t);
  double x = 0.0, y = 0.0;
  if (f.size() != 2 || !ParseNum(f[0], &x) || !ParseNum(f[1], &y)) return false;
  if (relative) {
    if (st.alignmentLayout.pis.empty()) return false;
    *local = {st.alignmentLayout.pis.back().x + x, st.alignmentLayout.pis.back().y + y};
  } else {
    CadCoord::LocalFromWorld(st, x, y, &local->x, &local->y);
  }
  assert(std::isfinite(local->x) && std::isfinite(local->y));
  return true;
}

bool HandleLayoutText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  AppCommandState::AlignmentLayoutSession& s = st.alignmentLayout;
  assert(st.active == AppCommandState::Kind::AlignmentLayout);
  const std::string t = Lower(Trim(line));
  assert(t.size() <= line.size());
  if (t.empty()) {
    FinishLayout(st, log);
    return true;
  }
  if (t == "u" || t == "undo") {
    const size_t keep = s.base.Empty() ? 0 : 1;  // a continued alignment keeps its end point as the first PI
    if (s.pis.size() <= keep) {
      log.push_back("ALIGNMENTLAYOUT - nothing to undo.");
      return true;
    }
    s.pis.pop_back();
    (void)RebuildLayout(st, log);
    log.push_back(s.pis.empty() ? "ALIGNMENTLAYOUT - specify the start point:" : "ALIGNMENTLAYOUT - specify the next point or [Undo]:");
    return true;
  }
  alignment::Point2 p;
  if (!ParseLayoutPoint(st, line, &p)) {
    log.push_back("ALIGNMENTLAYOUT - pick a point or type X,Y or @dx,dy; U undoes the last point, Enter finishes.");
    return true;
  }
  (void)SubmitLayoutPoint(st, p, log);
  return true;
}

void StartAlignmentFromObjects(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.drawing.alignments.size() < st.drawing.alignments.max_size());
  const std::vector<SelectedEntity> preselected = st.selection;
  if (!BeginInteractive(st, AppCommandState::Kind::AlignmentFromObjects, "CREATEALIGNMENTFROMOBJECTS", log)) return;
  st.alignmentFromObjects = {};
  st.selection = preselected;
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  if (!st.selection.empty()) {  // pick-first: the selection is the source; its first free end starts the alignment
    PromptDirection(st, log);
    return;
  }
  log.push_back("CREATEALIGNMENTFROMOBJECTS - select the first line/arc/polyline (the alignment starts at the end "
                "nearest the pick):");
}

bool SubmitFromObjectsPick(AppCommandState& st, double lx, double ly, std::vector<std::string>& log) {
  using P = AppCommandState::AlignmentFromObjectsPick::Phase;
  AppCommandState::AlignmentFromObjectsPick& p = st.alignmentFromObjects;
  assert(st.active == AppCommandState::Kind::AlignmentFromObjects);
  if (p.phase == P::WaitDirection) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - press Enter to accept the direction, or R to reverse it.");
    return false;
  }
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, lx, ly, CadOffsetEntityPickTolWorld(st), &hit, &d2)) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - nothing under the cursor; pick a line, arc, polyline or feature line.");
    return false;
  }
  using T = SelectedEntity::Type;
  if (hit.type != T::LineSeg && hit.type != T::Arc && hit.type != T::Polyline && hit.type != T::FeatureLine) {
    log.push_back(std::string("CREATEALIGNMENTFROMOBJECTS - that is ") + SourceKindName(hit.type) +
                  "; an alignment is made from lines, arcs, polylines and feature lines.");
    return false;
  }
  for (const SelectedEntity& e : st.selection) {
    if (e.type == hit.type && e.index == hit.index) {
      log.push_back("CREATEALIGNMENTFROMOBJECTS - already selected; pick another or press Enter.");
      return false;
    }
  }
  st.selection.push_back(hit);
  if (p.phase == P::WaitFirst) {
    p.hasPick = true;
    p.pickX = lx;
    p.pickY = ly;
    p.phase = P::WaitMore;
  }
  assert(!st.selection.empty());
  log.push_back("CREATEALIGNMENTFROMOBJECTS - " + std::to_string(st.selection.size()) +
                " selected. Select lines/arcs or polylines to create the alignment, Enter when done:");
  return true;
}

}  // namespace

bool AlignmentFromObjectsPreview(const AppCommandState& st, alignment::Alignment* out) {
  assert(out != nullptr);
  if (st.active != AppCommandState::Kind::AlignmentFromObjects ||
      st.alignmentFromObjects.phase != AppCommandState::AlignmentFromObjectsPick::Phase::WaitDirection)
    return false;
  std::string why;
  const bool ok = PreviewFromObjects(st, out, &why);
  assert(!ok || !out->Empty());
  return ok;
}

std::string AlignmentFromObjectsPickArgs(const AppCommandState& st) {
  const AppCommandState::AlignmentFromObjectsPick& p = st.alignmentFromObjects;
  std::string out;
  if (p.hasPick)
    out += ", PICK=" + FormatWorldNumber(p.pickX + CadCoord::WorldOriginX(st)) + " " +
           FormatWorldNumber(p.pickY + CadCoord::WorldOriginY(st));
  if (p.reverse) out += ", REVERSE";
  assert(out.size() < 200);
  assert(!p.reverse || !out.empty());
  return out;
}

// ALIGNMENTLAYOUT <name>[, CURVES|NOCURVES][, FROM=<x> <y>][, DIRECTION=<azimuth deg>]   (REQ-485.1)
// Tangent-Tangent layout: each click or typed point is a PI; with curves every interior PI gets the Curve and Spiral
// Settings curve. An alignment that already has geometry is continued from its end. U takes back a PI; Enter or ESC
// finishes and keeps what was built; one UNDO afterwards removes the whole session.
void RunAlignmentLayout(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  assert(!f.empty());
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLAYOUT", f[0], log);
  if (a == nullptr) return;
  std::optional<alignment::Point2> from;
  std::optional<double> direction;
  std::optional<bool> curves;
  for (size_t i = 1; i < f.size(); ++i) {
    const std::string key = Lower(Trim(f[i].substr(0, f[i].find('='))));
    const std::string val = f[i].find('=') == std::string::npos ? std::string() : Trim(f[i].substr(f[i].find('=') + 1));
    alignment::Point2 p;
    double d = 0.0;
    if (key == "curves" || key == "nocurves") curves = key == "curves";
    else if (key == "from" && ParseSpacedPoint(val, &p)) from = p;
    else if (key == "direction" && ParseNum(val, &d)) direction = d * kDegToRad;
    else {
      log.push_back("ALIGNMENTLAYOUT - usage: ALIGNMENTLAYOUT <name>[, CURVES|NOCURVES][, FROM=<x> <y>][, DIRECTION=<azimuth deg>].");
      return;
    }
  }
  const std::string name = a->name;
  const alignment::Alignment base = *a;
  if (!BeginInteractive(st, AppCommandState::Kind::AlignmentLayout, "ALIGNMENTLAYOUT", log)) return;
  st.alignmentLayout = {};
  st.alignmentLayout.name = name;
  st.alignmentLayout.base = base;
  st.alignmentLayoutToolsName = name;
  if (curves) st.alignmentCurveSettings.addCurves = *curves;
  const char* mode = st.alignmentCurveSettings.addCurves ? "with curves" : "no curves";
  if (!base.Empty()) {
    st.alignmentLayout.pis = {base.EndPoint()};
    log.push_back("ALIGNMENTLAYOUT - Tangent-Tangent (" + std::string(mode) + ") continues \"" + name + "\" from station " +
                  Sta(st, base, base.EndStation()) + ". Specify the next point or [Undo]:");
    return;
  }
  if (from) {
    alignment::Point2 lp;
    CadCoord::LocalFromWorld(st, from->x, from->y, &lp.x, &lp.y);
    st.alignmentLayout.pis = {lp};
    st.alignmentLayout.fixedDirection = direction.has_value();
    st.alignmentLayout.directionAzimuth = direction.value_or(0.0);
  }
  log.push_back("ALIGNMENTLAYOUT - Tangent-Tangent (" + std::string(mode) + ", radius " +
                Lin(st, EffectiveAlignmentCurveRadius(st)) + ") for \"" + name + "\". " +
                (from ? "Specify the next point or [Undo]:" : "Specify the start point:"));
}

// ALIGNMENTCURVESETTINGS [RADIUS=<r>|DEFAULT][, SPIRALIN=<length>|OFF][, SPIRALOUT=<length>|OFF]   (REQ-485.1)
void RunAlignmentCurveSettings(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  alignment::CurveSettings cs = st.alignmentCurveSettings;
  assert(cs.radius >= 0.0);
  for (const std::string& field : SplitFields(args)) {
    if (field.empty()) continue;
    const size_t eq = field.find('=');
    const std::string key = Lower(Trim(field.substr(0, eq)));
    const std::string val = eq == std::string::npos ? std::string() : Lower(Trim(field.substr(eq + 1)));
    double d = 0.0;
    const bool off = val == "off" || val == "0";
    if (key == "radius" && (val == "default" || (ParseNum(val, &d) && d > 0.0))) cs.radius = val == "default" ? 0.0 : d;
    else if (key == "spiralin" && (off || (ParseNum(val, &d) && d > 0.0))) cs.spiralInLength = off ? 0.0 : d;
    else if (key == "spiralout" && (off || (ParseNum(val, &d) && d > 0.0))) cs.spiralOutLength = off ? 0.0 : d;
    else {
      log.push_back("ALIGNMENTCURVESETTINGS - usage: ALIGNMENTCURVESETTINGS [RADIUS=<r>|DEFAULT][, SPIRALIN=<length>|OFF]"
                    "[, SPIRALOUT=<length>|OFF].");
      return;
    }
  }
  st.alignmentCurveSettings = cs;
  const double r = EffectiveAlignmentCurveRadius(st);
  assert(r > 0.0);
  auto spiral = [&](double len) { return len > 0.0 ? Lin(st, len) + " (A " + Lin(st, std::sqrt(r * len)) + ")" : std::string("off"); };
  log.push_back("ALIGNMENTCURVESETTINGS - clothoid; curve radius " + Lin(st, r) + (cs.radius > 0.0 ? "" : " (default)") +
                ", spiral in " + spiral(cs.spiralInLength) + ", spiral out " + spiral(cs.spiralOutLength) + ".");
}

bool SubmitAlignmentCommandPoint(AppCommandState& st, double localX, double localY, std::vector<std::string>& log) {
  assert(std::isfinite(localX) && std::isfinite(localY));
  if (st.active == AppCommandState::Kind::AlignmentLayout) return SubmitLayoutPoint(st, {localX, localY}, log);
  if (st.active == AppCommandState::Kind::AlignmentFromObjects) return SubmitFromObjectsPick(st, localX, localY, log);
  assert(st.active != AppCommandState::Kind::AlignmentLayout);
  return false;
}

bool HandleAlignmentCommandText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  assert(line.size() < line.max_size());
  if (st.active == AppCommandState::Kind::AlignmentLayout) return HandleLayoutText(st, line, log);
  if (st.active == AppCommandState::Kind::AlignmentFromObjects) return HandleFromObjectsText(st, line, log);
  assert(st.active != AppCommandState::Kind::AlignmentFromObjects);
  return false;
}

void CancelAlignmentCommand(AppCommandState& st, std::vector<std::string>& log) {
  assert(st.active == AppCommandState::Kind::AlignmentLayout || st.active == AppCommandState::Kind::AlignmentFromObjects);
  if (st.active == AppCommandState::Kind::AlignmentLayout) {
    FinishLayout(st, log);  // Civil 3D keeps what was laid out
    return;
  }
  st.active = AppCommandState::Kind::None;
  st.selection.clear();
  st.alignmentFromObjects = {};
  assert(st.active == AppCommandState::Kind::None);
  log.push_back("CREATEALIGNMENTFROMOBJECTS canceled - nothing was created.");
}

const char* AlignmentCommandPrompt(const AppCommandState& st) {
  using P = AppCommandState::AlignmentFromObjectsPick::Phase;
  if (st.active == AppCommandState::Kind::AlignmentLayout)
    return st.alignmentLayout.pis.empty()
               ? "ALIGNMENTLAYOUT: Specify start point (click or X,Y) | ESC ends"
               : "ALIGNMENTLAYOUT: Specify next point or [Undo] (click, X,Y or @dx,dy) | Enter or ESC ends";
  if (st.active != AppCommandState::Kind::AlignmentFromObjects) return nullptr;
  switch (st.alignmentFromObjects.phase) {
    case P::WaitFirst: return "CREATEALIGNMENTFROMOBJECTS: Select the first line/arc/polyline | ESC cancels";
    case P::WaitMore: return "CREATEALIGNMENTFROMOBJECTS: Select lines/arcs or polylines to create alignment | Enter when done";
    case P::WaitDirection: return "CREATEALIGNMENTFROMOBJECTS: Press Enter to accept alignment direction or [Reverse]";
  }
  return nullptr;
}
