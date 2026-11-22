#include "SpreadsheetIo.hpp"
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

SurfaceComponentStyle* AlignmentComponentByName(AlignmentStyle& s, const std::string& word, std::string* why) {
  const std::string w = Lower(word);
  if (w == "tangent" || w == "line") return &s.tangent;
  if (w == "curve" || w == "arc") return &s.curve;
  if (w == "spiral") return &s.spiral;
  if (why != nullptr)
    *why = "component must be TANGENT, CURVE or SPIRAL, not \"" + word + "\".";
  return nullptr;
}

namespace {

// Reads space-separated geometry point codes as the labels write them (BP, PI, TS, Mid, ...). False on an unknown one.
bool ParseGeometryCodes(const std::string& text, std::vector<std::string>* codes, std::string* bad) {
  assert(codes != nullptr && bad != nullptr);
  assert(text.size() < 100000);
  static const std::array<const char*, 14> kCodes{"BP", "EP", "PI", "PC", "PT", "PCC", "PRC",
                                                  "TS", "ST", "CS", "SC", "SS", "RS", "Mid"};
  std::istringstream words(text);
  std::string w;
  for (int guard = 0; guard < 1000 && (words >> w); ++guard) {
    const auto it = std::find_if(kCodes.begin(), kCodes.end(), [&](const char* c) { return Lower(c) == Lower(w); });
    if (it == kCodes.end()) {
      *bad = w;
      return false;
    }
    if (std::find(codes->begin(), codes->end(), *it) == codes->end()) codes->push_back(*it);
  }
  return !codes->empty();
}

// ALIGNMENTSTYLE MARKER <style>, <code>, CROSS|X|CIRCLE|SQUARE|NONE|DEFAULT[, <size>[, <color>]]  (REQ-491 Markers tab:
// shape, size and colour per geometry point code; DEFAULT returns the code to the style's cross)
void RunAlignmentStyleMarker(AppCommandState& st, const std::vector<std::string>& f, std::vector<std::string>& log) {
  assert(!f.empty());
  assert(!st.drawing.alignmentStyles.empty());
  const AlignmentStyle* current = f.size() >= 4 && f.size() <= 6 ? AlignmentStyles::Find(st.drawing.alignmentStyles, f[1]) : nullptr;
  std::vector<std::string> codes;
  std::string bad;
  if (current == nullptr || !ParseGeometryCodes(f.size() >= 3 ? f[2] : std::string{}, &codes, &bad) || codes.size() != 1) {
    log.push_back("ALIGNMENTSTYLE - usage: ALIGNMENTSTYLE MARKER <style>, <code>, CROSS|X|CIRCLE|SQUARE|NONE|DEFAULT"
                  "[, <size>[, <color>]] (one code: BP EP PI PC PT PCC PRC TS ST CS SC SS RS Mid).");
    return;
  }
  static const std::array<std::pair<const char*, AlignmentMarkerShape>, 5> kShapes{{{"cross", AlignmentMarkerShape::Cross},
      {"x", AlignmentMarkerShape::X}, {"circle", AlignmentMarkerShape::Circle}, {"square", AlignmentMarkerShape::Square},
      {"none", AlignmentMarkerShape::None}}};
  AlignmentStyle edited = *current;
  auto& markers = edited.codeMarkers;
  markers.erase(std::remove_if(markers.begin(), markers.end(), [&](const AlignmentMarkerSpec& m) { return m.code == codes[0]; }),
                markers.end());
  if (Lower(f[3]) != "default") {
    const auto shape = std::find_if(kShapes.begin(), kShapes.end(), [&](const auto& s) { return Lower(f[3]) == s.first; });
    AlignmentMarkerSpec spec{codes[0], AlignmentMarkerShape::Cross, edited.markerHalfSize, "ByLayer"};
    double size = 0.0;
    std::string color = "ByLayer";
    if (shape == kShapes.end() || (f.size() >= 5 && (!ParseNum(f[4], &size) || !(size > 0.0))) ||
        (f.size() == 6 && !CadColorStorageFromTyped(f[5], &color))) {
      log.push_back("ALIGNMENTSTYLE - MARKER needs a shape (CROSS, X, CIRCLE, SQUARE, NONE or DEFAULT), a positive size "
                    "and a colour.");
      return;
    }
    spec.shape = shape->second;
    if (f.size() >= 5) spec.halfSize = size;
    spec.color = color;
    markers.push_back(std::move(spec));
  }
  PushUndoSnapshot(st, "Alignment style");
  *AlignmentStyles::Find(st.drawing.alignmentStyles, f[1]) = std::move(edited);
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTSTYLE - \"" + f[1] + "\" " + codes[0] + " marker set to " + f[3] + ".");
}

}  // namespace

void RunAlignmentStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentStyles::EnsureStandard(st.drawing.alignmentStyles);
  const auto usage = [&]() {
    log.push_back(
        "ALIGNMENTSTYLE - usage: ALIGNMENTSTYLE LIST | NEW <style> | DELETE <style> | ASSIGN <alignment>, <style> | "
        "SHOW|HIDE <style>, <tangent|curve|spiral> | COLOR <style>, <tangent|curve|spiral>, <color> | "
        "SET <style>, TICKS|MARKERS|ARROWS|LABELS ON|OFF | SET <style>, TICKSIDE BOTH|LEFT|RIGHT | "
        "SET <style>, MAJORTICK|MINORTICK|MARKERSIZE|ARROWLENGTH <n> | SET <style>, CHECKSET <set>|NONE | "
        "CURRENT <style> | MARKER <style>, <code>, CROSS|X|CIRCLE|SQUARE|NONE|DEFAULT[, <size>[, <color>]].");
  };
  const std::string trimmed = Trim(args);
  if (trimmed.empty()) {
    usage();
    return;
  }
  const std::vector<std::string> f = SplitFields(trimmed);
  const std::string verb = Lower(f[0]);
  if (verb == "list") {
    if (st.drawing.alignmentStyles.empty()) {
      log.push_back("ALIGNMENTSTYLE - no alignment styles.");
      return;
    }
    for (const AlignmentStyle& s : st.drawing.alignmentStyles)
      log.push_back("  STYLE \"" + s.name + "\"  (used by " +
                    std::to_string(AlignmentStyles::CountAlignmentsResolvingTo(st.drawing.alignments, st.drawing.alignmentStyles, s.name)) +
                    " alignment(s))");
    return;
  }
  if (verb == "new") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    const std::string name = f[1];
    if (AlignmentStyles::Find(st.drawing.alignmentStyles, name)) {
      log.push_back("ALIGNMENTSTYLE - a style named \"" + name + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    AlignmentStyle s = AlignmentStyles::StandardAlignmentStyle();
    s.name = name;
    st.drawing.alignmentStyles.push_back(std::move(s));
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - created style \"" + name + "\" (copied from Standard).");
    return;
  }
  if (verb == "delete") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    const std::string name = f[1];
    if (name == AlignmentStyles::kStandardName) {
      log.push_back("ALIGNMENTSTYLE - \"Standard\" cannot be deleted.");
      return;
    }
    const int users = AlignmentStyles::CountAlignmentsResolvingTo(st.drawing.alignments, st.drawing.alignmentStyles, name);
    if (users > 0) {
      log.push_back("ALIGNMENTSTYLE - \"" + name + "\" is used by " + std::to_string(users) +
                    " alignment(s) and cannot be deleted.");
      return;
    }
    if (name == st.drawing.alignmentStyleCurrent) {
      log.push_back("ALIGNMENTSTYLE - \"" + name + "\" is the current style and cannot be deleted; make another current first.");
      return;
    }
    const auto it = std::find_if(st.drawing.alignmentStyles.begin(), st.drawing.alignmentStyles.end(),
                                 [&](const AlignmentStyle& s) { return s.name == name; });
    if (it == st.drawing.alignmentStyles.end()) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + name + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    st.drawing.alignmentStyles.erase(it);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - deleted style \"" + name + "\".");
    return;
  }
  if (verb == "set") {  // REQ-491: the appearance items the Markers, Display and Design tabs edit
    if (f.size() != 4) {
      usage();
      return;
    }
    const AlignmentStyle* current = AlignmentStyles::Find(st.drawing.alignmentStyles, f[1]);
    if (current == nullptr) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[1] + "\".");
      return;
    }
    AlignmentStyle edited = *current;  // validate on a copy, so a refused item changes nothing and takes no undo step
    const std::string item = Lower(f[2]), value = Lower(f[3]);
    double v = 0.0;
    const bool numberOk = ParseNum(f[3], &v) && v >= 0.0;
    bool ok = false;
    const auto setOnOff = [&](bool* target) {
      if (value != "on" && value != "off") return false;
      *target = value == "on";
      return true;
    };
    if (item == "checkset") {  // REQ-502 / D-2026-11-04-b: the design-check set the style applies by default
      const std::string want = Trim(f[3]);
      if (Lower(want) == "none") {
        edited.checkSetName.clear();
        ok = true;
      } else if (const alignment::AlignmentCheckSet* set = alignment::FindCheckSet(st.drawing.alignmentCheckSets, want)) {
        edited.checkSetName = set->name;
        ok = true;
      }
    } else if (item == "ticks") ok = setOnOff(&edited.ticks.visible);
    else if (item == "markers") ok = setOnOff(&edited.markers.visible);
    else if (item == "arrows") ok = setOnOff(&edited.arrows.visible);
    else if (item == "labels") ok = setOnOff(&edited.labelsVisible);
    else if (item == "tickside") {
      ok = true;
      if (value == "both") edited.tickSide = 0;
      else if (value == "left") edited.tickSide = 1;
      else if (value == "right") edited.tickSide = 2;
      else ok = false;
    } else if (item == "majortick" && numberOk) { edited.majorTickLength = v; ok = true; }
    else if (item == "minortick" && numberOk) { edited.minorTickLength = v; ok = true; }
    else if (item == "markersize" && numberOk) { edited.markerHalfSize = v; ok = true; }
    else if (item == "arrowlength" && numberOk) { edited.arrowLength = v; ok = true; }
    if (!ok) {
      log.push_back("ALIGNMENTSTYLE - SET item \"" + f[2] + "\" with value \"" + f[3] + "\" is not valid.");
      return;
    }
    if (edited == *current) {
      log.push_back("ALIGNMENTSTYLE - \"" + edited.name + "\" already has " + f[2] + " set to " + f[3] + ".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    *AlignmentStyles::Find(st.drawing.alignmentStyles, f[1]) = std::move(edited);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - style \"" + f[1] + "\": " + f[2] + " set to " + f[3] + ".");
    return;
  }
  if (verb == "assign") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    alignment::Alignment* a = Resolve(st, "ALIGNMENTSTYLE", f[1], log);
    if (a == nullptr) return;
    if (!AlignmentStyles::Find(st.drawing.alignmentStyles, f[2])) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[2] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    a = Resolve(st, "ALIGNMENTSTYLE", f[1], log);
    a->style = f[2];
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - alignment \"" + a->name + "\" now uses style \"" + f[2] + "\".");
    return;
  }
  if (verb == "show" || verb == "hide") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    AlignmentStyle* s = AlignmentStyles::Find(st.drawing.alignmentStyles, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[1] + "\".");
      return;
    }
    std::string why;
    SurfaceComponentStyle* comp = AlignmentComponentByName(*s, f[2], &why);
    if (comp == nullptr) {
      log.push_back("ALIGNMENTSTYLE - " + why);
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    comp->visible = (verb == "show");
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - \"" + s->name + "\" " + f[2] + " " + (comp->visible ? "shown." : "hidden."));
    return;
  }
  if (verb == "color") {
    if (f.size() < 4 || f[1].empty() || f[2].empty() || f[3].empty()) {
      usage();
      return;
    }
    AlignmentStyle* s = AlignmentStyles::Find(st.drawing.alignmentStyles, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[1] + "\".");
      return;
    }
    std::string why;
    SurfaceComponentStyle* comp = AlignmentComponentByName(*s, f[2], &why);
    if (comp == nullptr) {
      log.push_back("ALIGNMENTSTYLE - " + why);
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    comp->color = f[3];
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - \"" + s->name + "\" " + f[2] + " color set to " + f[3] + ".");
    return;
  }
  if (verb == "current" && f.size() == 2) {  // REQ-491 / REQ-478 current style
    const AlignmentStyle* s = AlignmentStyles::Find(st.drawing.alignmentStyles, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    st.drawing.alignmentStyleCurrent = f[1];
    log.push_back("ALIGNMENTSTYLE - \"" + f[1] + "\" is now the current style; new alignments use it.");
    return;
  }
  if (verb == "marker") {
    RunAlignmentStyleMarker(st, f, log);
    return;
  }
  usage();
}

namespace {

// The label-set field an item name selects (REQ-492.5), or nullptr for an unknown item.
std::string* LabelSetItemStyle(AlignmentLabelSet& s, const std::string& item) {
  const std::string which = Lower(item);
  if (which == "major") return &s.majorStyle;
  if (which == "geometry") return &s.geometryStyle;
  if (which == "speeds" || which == "speed") return &s.speedStyle;
  if (which == "equations" || which == "equation") return &s.equationStyle;
  if (which == "segments" || which == "segment") return &s.segmentStyle;
  if (which == "offsets" || which == "offset") return &s.offsetStyle;
  return nullptr;
}

// ALIGNMENTLABELSET STYLE, <set>, MAJOR|GEOMETRY|SPEEDS|EQUATIONS|SEGMENTS|OFFSETS, <label style>
void RunAlignmentLabelSetStyle(AppCommandState& st, const std::vector<std::string>& f, std::vector<std::string>& log) {
  assert(!f.empty());
  assert(!st.drawing.alignmentLabelSets.empty());
  if (f.size() < 4 || f[1].empty() || f[2].empty() || f[3].empty()) {
    log.push_back("ALIGNMENTLABELSET - usage: ALIGNMENTLABELSET STYLE, <set>, "
                  "MAJOR|GEOMETRY|SPEEDS|EQUATIONS|SEGMENTS|OFFSETS, <label style>.");
    return;
  }
  AlignmentLabelSet* s = AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[1]);
  if (s == nullptr) {
    log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
    return;
  }
  if (LabelSetItemStyle(*s, f[2]) == nullptr) {
    log.push_back("ALIGNMENTLABELSET - item must be MAJOR, GEOMETRY, SPEEDS, EQUATIONS, SEGMENTS or OFFSETS, not \"" +
                  f[2] + "\".");
    return;
  }
  if (AlignmentLabelStyles::Find(st.drawing.alignmentLabelStyles, f[3]) == nullptr) {
    log.push_back("ALIGNMENTLABELSET - no alignment label style named \"" + f[3] + "\" (see ALIGNMENTLABELSTYLE LIST).");
    return;
  }
  PushUndoSnapshot(st, "Alignment label set");
  s = AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[1]);
  *LabelSetItemStyle(*s, f[2]) = f[3];
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" " + f[2] + " labels now use label style \"" + f[3] + "\".");
}

// ALIGNMENTLABELSET GEOMETRY, <set>, CODES, <codes>|ALL | ADD, <label style>, <codes> | REMOVE, <n>   (REQ-492.1: a
// Geometry Points item may repeat with different styles, e.g. BP and EP in one and the PIs in another)
void RunAlignmentLabelSetGeometry(AppCommandState& st, const std::vector<std::string>& f, std::vector<std::string>& log) {
  assert(!f.empty());
  assert(!st.drawing.alignmentLabelSets.empty());
  const char* usage = "ALIGNMENTLABELSET - usage: ALIGNMENTLABELSET GEOMETRY, <set>, CODES, <codes>|ALL | "
                      "ADD, <label style>, <codes> | REMOVE, <item#> (codes: BP EP PI PC PT PCC PRC TS ST CS SC SS RS Mid).";
  const std::string op = f.size() >= 3 ? Lower(f[2]) : std::string{};
  const bool shapeOk = (op == "codes" && f.size() == 4) || (op == "add" && f.size() == 5) || (op == "remove" && f.size() == 4);
  const AlignmentLabelSet* current = shapeOk ? AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[1]) : nullptr;
  if (!shapeOk || current == nullptr) {
    log.push_back(shapeOk ? "ALIGNMENTLABELSET - no label set named \"" + f[1] + "\"." : std::string(usage));
    return;
  }
  AlignmentLabelSet edited = *current;
  std::vector<std::string> codes;
  std::string bad;
  if (op == "codes" && Lower(f[3]) == "all") {
    edited.geometryCodes.clear();
  } else if (op == "codes" || op == "add") {
    if (!ParseGeometryCodes(op == "add" ? f[4] : f[3], &codes, &bad)) {
      log.push_back("ALIGNMENTLABELSET - \"" + bad + "\" is not a geometry point code. " + usage);
      return;
    }
    if (op == "codes") edited.geometryCodes = codes;
    else if (AlignmentLabelStyles::Find(st.drawing.alignmentLabelStyles, f[3]) == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no alignment label style named \"" + f[3] + "\".");
      return;
    } else edited.extraGeometryItems.push_back({f[3], codes});
  } else {
    double n = 0.0;
    if (!ParseNum(f[3], &n) || n < 1.0 || n > static_cast<double>(edited.extraGeometryItems.size()) || n != std::floor(n)) {
      log.push_back("ALIGNMENTLABELSET - \"" + edited.name + "\" has " + std::to_string(edited.extraGeometryItems.size()) +
                    " further geometry item(s); \"" + f[3] + "\" is not one of them.");
      return;
    }
    edited.extraGeometryItems.erase(edited.extraGeometryItems.begin() + static_cast<std::ptrdiff_t>(n) - 1);
  }
  PushUndoSnapshot(st, "Alignment label set");
  *AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[1]) = std::move(edited);
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTLABELSET - \"" + f[1] + "\" geometry point items updated.");
}

}  // namespace

void RunAlignmentLabelSet(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentLabelSets::EnsureStandard(st.drawing.alignmentLabelSets);
  AlignmentLabelStyles::EnsureStandard(st.drawing.alignmentLabelStyles);
  const auto usage = [&]() {
    log.push_back(
        "ALIGNMENTLABELSET - usage: ALIGNMENTLABELSET LIST | NEW <set> | DELETE <set> | ASSIGN <alignment>, <set> | "
        "MAJORINTERVAL <set>, <interval> | MINORINTERVAL <set>, <interval> | "
        "SHOW|HIDE <set>, MAJOR|MINOR|EQUATIONS|GEOMETRY|SPEEDS|PROFILE|SUPERELEVATION | "
        "STYLE <set>, MAJOR|GEOMETRY|SPEEDS|EQUATIONS|SEGMENTS|OFFSETS, <label style> | "
        "GEOMETRY <set>, CODES, <codes>|ALL | GEOMETRY <set>, ADD, <label style>, <codes> | GEOMETRY <set>, REMOVE, <n>.");
  };
  const std::string trimmed = Trim(args);
  if (trimmed.empty()) {
    usage();
    return;
  }
  const std::vector<std::string> f = SplitFields(trimmed);
  const std::string verb = Lower(f[0]);
  if (verb == "list") {
    const auto joined = [](const std::vector<std::string>& codes) {
      std::string text;
      for (const std::string& c : codes) text += (text.empty() ? "" : " ") + c;
      return text.empty() ? std::string("all") : text;
    };
    for (const AlignmentLabelSet& s : st.drawing.alignmentLabelSets) {
      log.push_back("  LABEL SET \"" + s.name + "\"  major=" + Lin(st, s.majorInterval) + " minor=" +
                    Lin(st, s.minorInterval) +
                    "  (used by " +
                    std::to_string(AlignmentLabelSets::CountAlignmentsResolvingTo(st.drawing.alignments, st.drawing.alignmentLabelSets,
                                                                                  s.name)) +
                    " alignment(s))");
      log.push_back("    Geometry points 1: " + joined(s.geometryCodes) + " in \"" + s.geometryStyle + "\"");
      for (size_t k = 0; k < s.extraGeometryItems.size(); ++k)
        log.push_back("    Geometry points " + std::to_string(k + 2) + ": " + joined(s.extraGeometryItems[k].codes) +
                      " in \"" + s.extraGeometryItems[k].style + "\"");
    }
    return;
  }
  if (verb == "new") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    if (AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[1])) {
      log.push_back("ALIGNMENTLABELSET - a label set named \"" + f[1] + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    AlignmentLabelSet s = AlignmentLabelSets::StandardLabelSet();
    s.name = f[1];
    const bool metric = st.drawing.drawingInsUnits == 4 || st.drawing.drawingInsUnits == 6;
    if (metric) {  // REQ-492.2: 20 m major stations in a metric drawing; minors keep the imperial 1:5 ratio
      s.majorInterval = 20.0;
      s.minorInterval = 4.0;
    }
    st.drawing.alignmentLabelSets.push_back(std::move(s));
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - created label set \"" + f[1] + "\" (copied from Standard" +
                  (metric ? ", major 20 m, minor 4 m)." : ")."));
    return;
  }
  if (verb == "delete") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    if (f[1] == AlignmentLabelSets::kStandardName) {
      log.push_back("ALIGNMENTLABELSET - \"Standard\" cannot be deleted.");
      return;
    }
    const int users = AlignmentLabelSets::CountAlignmentsResolvingTo(st.drawing.alignments, st.drawing.alignmentLabelSets, f[1]);
    if (users > 0) {
      log.push_back("ALIGNMENTLABELSET - \"" + f[1] + "\" is used by " + std::to_string(users) +
                    " alignment(s) and cannot be deleted.");
      return;
    }
    const auto it = std::find_if(st.drawing.alignmentLabelSets.begin(), st.drawing.alignmentLabelSets.end(),
                                 [&](const AlignmentLabelSet& s) { return s.name == f[1]; });
    if (it == st.drawing.alignmentLabelSets.end()) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    st.drawing.alignmentLabelSets.erase(it);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - deleted label set \"" + f[1] + "\".");
    return;
  }
  if (verb == "assign") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    alignment::Alignment* a = Resolve(st, "ALIGNMENTLABELSET", f[1], log);
    if (a == nullptr) return;
    if (!AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[2])) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[2] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    a = Resolve(st, "ALIGNMENTLABELSET", f[1], log);
    a->labelSet = f[2];
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - alignment \"" + a->name + "\" now uses label set \"" + f[2] + "\".");
    return;
  }
  if (verb == "majorinterval") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    const double v = std::strtod(f[2].c_str(), nullptr);
    if (!(v > 0.0)) {
      log.push_back("ALIGNMENTLABELSET - major interval must be positive.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    s->majorInterval = v;
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" major interval set to " + Lin(st, v) + ".");
    return;
  }
  if (verb == "minorinterval") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    const double v = std::strtod(f[2].c_str(), nullptr);
    if (!(v > 0.0)) {
      log.push_back("ALIGNMENTLABELSET - minor interval must be positive.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    s->minorInterval = v;
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" minor interval set to " + Lin(st, v) + ".");
    return;
  }
  if (verb == "show" || verb == "hide") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.drawing.alignmentLabelSets, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    const std::string which = Lower(f[2]);
    const bool show = verb == "show";
    // REQ-492.2 / Civil 3D: minor stations are placed from major stations, so the two cannot be split apart.
    if (which == "major" && !show && s->minorStationsOn) {
      log.push_back("ALIGNMENTLABELSET - refused: \"" + s->name + "\" has minor stations shown, and they need the major stations. Hide MINOR first.");
      return;
    }
    if (which == "minor" && show && !s->majorStationsOn) {
      log.push_back("ALIGNMENTLABELSET - refused: minor stations need the major stations. Show MAJOR first.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    if (which == "major")
      s->majorStationsOn = show;
    else if (which == "minor")
      s->minorStationsOn = show;
    else if (which == "equations" || which == "equation")  // REQ-492.1 Station Equations item
      s->equationsOn = show;
    else if (which == "geometry")  // REQ-492.1 Geometry Points item (D-2026-11-04-c)
      s->geometryPointsOn = show;
    else if (which == "speeds" || which == "speed")  // REQ-492.1 Design Speeds item
      s->designSpeedsOn = show;
    else if (which == "profile")  // REQ-492.1 placeholder items
      s->profileGeometryPointsOn = show;
    else if (which == "superelevation")
      s->superelevationPointsOn = show;
    else {
      log.push_back("ALIGNMENTLABELSET - component must be MAJOR, MINOR, EQUATIONS, GEOMETRY, SPEEDS, PROFILE or "
                    "SUPERELEVATION, not \"" + f[2] + "\".");
      return;
    }
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" " + f[2] + " " + (verb == "show" ? "shown." : "hidden."));
    if (show && (which == "profile" || which == "superelevation"))
      log.push_back("  NOTE: this item is a placeholder; it labels nothing until alignments have " +
                    std::string(which == "profile" ? "profiles." : "superelevation."));
    return;
  }
  if (verb == "style") {
    RunAlignmentLabelSetStyle(st, f, log);
    return;
  }
  if (verb == "geometry") {
    RunAlignmentLabelSetGeometry(st, f, log);
    return;
  }
  usage();
}

namespace {

// ALIGNMENTLABELSTYLE SET, <style>, PRECISION|PREFIX|SUFFIX|ORIENTATION, <value>: REQ-492.5 formatting. `which` is
// lower case. Returns false with the reason logged.
bool EditAlignmentLabelFormat(AlignmentLabelStyle& s, const std::string& which, const std::string& value,
                              std::vector<std::string>& log) {
  assert(!which.empty());
  assert(!s.name.empty());
  const std::string v = Lower(value);
  if (which == "precision") {
    double d = 0.0;
    if (v == "drawing") {
      s.precision = -1;
      return true;
    }
    if (!ParseNum(value, &d) || d < 0.0 || d > 8.0 || d != std::floor(d)) {
      log.push_back("ALIGNMENTLABELSTYLE - precision must be 0 to 8 decimals, or DRAWING, not \"" + value + "\".");
      return false;
    }
    s.precision = static_cast<int>(d);
    return true;
  }
  if (which == "prefix" || which == "suffix") {
    (which == "prefix" ? s.prefix : s.suffix) = v == "none" ? std::string{} : value;
    return true;
  }
  if (which == "orientation" && (v == "object" || v == "view")) {
    s.viewOriented = v == "view";
    return true;
  }
  log.push_back("ALIGNMENTLABELSTYLE - property must be HEIGHT, COLOR, TEXTSTYLE, PRECISION, PREFIX, SUFFIX or "
                "ORIENTATION OBJECT|VIEW, not \"" + which + "\" = \"" + value + "\".");
  return false;
}

// ALIGNMENTLABELSTYLE SET, <style>, HEIGHT|COLOR|TEXTSTYLE, <value> (REQ-492.5). Returns false with the reason logged.
bool EditAlignmentLabelStyle(const AppCommandState& st, AlignmentLabelStyle& s, const std::string& item,
                             const std::string& value, std::vector<std::string>& log) {
  assert(!item.empty());
  assert(!s.name.empty());
  const std::string which = Lower(item);
  if (which == "height") {
    const double v = std::strtod(value.c_str(), nullptr);
    if (!(v > 0.0) || !std::isfinite(v)) {
      log.push_back("ALIGNMENTLABELSTYLE - height must be a positive plotted height in inches, not \"" + value + "\".");
      return false;
    }
    s.heightInches = v;
    return true;
  }
  if (which == "color" || which == "colour") {
    std::string storage;
    if (!CadColorStorageFromTyped(value, &storage)) {
      log.push_back("ALIGNMENTLABELSTYLE - \"" + value + "\" is not a colour (ByLayer, an index 1-255, a name such as "
                    "red, or #RRGGBB).");
      return false;
    }
    s.color = storage;
    return true;
  }
  if (which == "textstyle") {
    if (TextStyles::Find(st.drawing.textStyles, value) == nullptr) {
      log.push_back("ALIGNMENTLABELSTYLE - no text style named \"" + value + "\" (see STYLE).");
      return false;
    }
    s.textStyle = value;
    return true;
  }
  return EditAlignmentLabelFormat(s, which, value, log);
}

// REQ-478 pattern: Standard and a style a label set still uses cannot be deleted.
void RunAlignmentLabelStyleDelete(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  assert(!name.empty());
  assert(!st.drawing.alignmentLabelStyles.empty());
  if (name == AlignmentLabelStyles::kStandardName) {
    log.push_back("ALIGNMENTLABELSTYLE - \"Standard\" cannot be deleted.");
    return;
  }
  const auto it = std::find_if(st.drawing.alignmentLabelStyles.begin(), st.drawing.alignmentLabelStyles.end(),
                               [&](const AlignmentLabelStyle& s) { return s.name == name; });
  if (it == st.drawing.alignmentLabelStyles.end()) {
    log.push_back("ALIGNMENTLABELSTYLE - no label style named \"" + name + "\".");
    return;
  }
  const int users = AlignmentLabelStyles::CountLabelSetsUsing(st.drawing.alignmentLabelSets, name);
  if (users > 0) {
    log.push_back("ALIGNMENTLABELSTYLE - \"" + name + "\" is used by " + std::to_string(users) +
                  " label set(s) and cannot be deleted; assign another style first (ALIGNMENTLABELSET STYLE).");
    return;
  }
  PushUndoSnapshot(st, "Alignment label style");
  st.drawing.alignmentLabelStyles.erase(std::find_if(st.drawing.alignmentLabelStyles.begin(), st.drawing.alignmentLabelStyles.end(),
                                             [&](const AlignmentLabelStyle& s) { return s.name == name; }));
  log.push_back("ALIGNMENTLABELSTYLE - deleted label style \"" + name + "\".");
}

}  // namespace

// ALIGNMENTLABELSTYLE LIST | NEW <style> | DELETE <style> | SET <style>, HEIGHT|COLOR|TEXTSTYLE, <value>  (REQ-492.5)
void RunAlignmentLabelStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentLabelStyles::EnsureStandard(st.drawing.alignmentLabelStyles);
  const std::vector<std::string> f = SplitFields(Trim(args));
  const std::string verb = f.empty() ? std::string{} : Lower(f[0]);
  const auto usage = [&] {
    log.push_back("ALIGNMENTLABELSTYLE - usage: ALIGNMENTLABELSTYLE LIST | NEW, <style> | DELETE, <style> | "
                  "SET, <style>, HEIGHT|COLOR|TEXTSTYLE|PRECISION|PREFIX|SUFFIX|ORIENTATION, <value>.");
  };
  if (verb == "list") {
    for (const AlignmentLabelStyle& s : st.drawing.alignmentLabelStyles)
      log.push_back("  LABEL STYLE \"" + s.name + "\"  height=" + FormatLinear(s.heightInches, 3) + "in  colour=" +
                    CadColorDisplayLabel(s.color) + "  text style=" + s.textStyle + "  precision=" +
                    (s.precision < 0 ? std::string("drawing") : std::to_string(s.precision)) +
                    (s.prefix.empty() ? "" : "  prefix=\"" + s.prefix + "\"") +
                    (s.suffix.empty() ? "" : "  suffix=\"" + s.suffix + "\"") +
                    "  orientation=" + (s.viewOriented ? "view" : "object") + "  (used by " +
                    std::to_string(AlignmentLabelStyles::CountLabelSetsUsing(st.drawing.alignmentLabelSets, s.name)) +
                    " label set(s))");
    return;
  }
  if (f.size() < 2 || f[1].empty()) {
    usage();
    return;
  }
  if (verb == "new") {
    if (AlignmentLabelStyles::Find(st.drawing.alignmentLabelStyles, f[1]) != nullptr) {
      log.push_back("ALIGNMENTLABELSTYLE - a label style named \"" + f[1] + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label style");
    AlignmentLabelStyle s = AlignmentLabelStyles::StandardLabelStyle();
    s.name = f[1];
    st.drawing.alignmentLabelStyles.push_back(std::move(s));
    log.push_back("ALIGNMENTLABELSTYLE - created label style \"" + f[1] + "\" (copied from Standard).");
    return;
  }
  if (verb == "delete") {
    RunAlignmentLabelStyleDelete(st, f[1], log);
    return;
  }
  if (verb == "set" && f.size() >= 4) {
    const AlignmentLabelStyle* current = AlignmentLabelStyles::Find(st.drawing.alignmentLabelStyles, f[1]);
    if (current == nullptr) {
      log.push_back("ALIGNMENTLABELSTYLE - no label style named \"" + f[1] + "\".");
      return;
    }
    AlignmentLabelStyle edited = *current;
    if (!EditAlignmentLabelStyle(st, edited, f[2], Trim(f[3]), log)) return;
    PushUndoSnapshot(st, "Alignment label style");
    *AlignmentLabelStyles::Find(st.drawing.alignmentLabelStyles, f[1]) = std::move(edited);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSTYLE - \"" + f[1] + "\" " + f[2] + " set to " + Trim(f[3]) + ".");
    return;
  }
  usage();
}

// ALIGNMENTREPORT [<name>][, TXT|CSV[, <path>]]  (REQ-494)
namespace {

// REQ-502: the report names the alignment's own check set and its findings.
alignment::AlignmentReportFormat ReportFormatFor(const AppCommandState& st, const alignment::Alignment& a,
                                                 alignment::AlignmentReportFormat fmt) {
  const std::string criteriaName = AlignmentStyles::EffectiveCheckSetName(st.drawing.alignmentStyles, a);
  if (!criteriaName.empty()) fmt.checkSet = alignment::FindCheckSet(st.drawing.alignmentCheckSets, criteriaName);
  return fmt;
}

}  // namespace

void RunAlignmentReport(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  std::string filterName;
  std::string exportKind;
  std::string path;
  if (f.size() >= 2) {
    const std::string verb = Lower(f[1]);
    if (verb == "txt" || verb == "csv") {
      exportKind = verb;
      filterName = f[0];
      if (f.size() >= 3) path = Trim(f[2]);
    }
  }
  if (exportKind.empty() && !f.empty()) filterName = Trim(f[0]);

  if (st.drawing.alignments.empty()) {
    log.push_back("ALIGNMENTREPORT - this drawing has no alignments.");
    return;
  }

  std::vector<const alignment::Alignment*> targets;
  for (const alignment::Alignment& a : st.drawing.alignments) {
    if (!filterName.empty() && Lower(a.name) != Lower(filterName)) continue;
    targets.push_back(&a);
  }
  if (!filterName.empty() && targets.empty()) {
    log.push_back("ALIGNMENTREPORT - no alignment named \"" + filterName + "\".");
    return;
  }

  alignment::AlignmentReportFormat fmt;
  fmt.linearPrecision = st.displayLinearPrecision;
  fmt.angles = CadAngleDisplaySettings(st);
  fmt.station = st.drawing.alignmentStationFormat;

  if (exportKind.empty()) {
    for (size_t i = 0; i < targets.size(); ++i) {
      std::vector<std::string> lines;
      alignment::BuildAlignmentReportLines(*targets[i], ReportFormatFor(st, *targets[i], fmt), &lines);
      for (const std::string& line : lines) log.push_back(line);
      if (targets.size() > 1 && i + 1 < targets.size()) log.push_back("---");
    }
    log.push_back("ALIGNMENTREPORT - " + std::to_string(targets.size()) + " alignment(s) reported.");
    return;
  }

  if (path.empty()) {
    char buf[1024] = {};
    const char* defaultName = exportKind == "csv" ? "alignment-report.csv" : "alignment-report.txt";
    const bool picked = exportKind == "csv" ? BrowseSaveFileCsvUtf8(buf, sizeof(buf), defaultName)
                                              : BrowseSaveFileTxtUtf8(buf, sizeof(buf), defaultName);
    if (!picked || buf[0] == '\0') {
      log.push_back("ALIGNMENTREPORT - cancelled.");
      return;
    }
    path = buf;
  }

  if (exportKind == "csv") {
    SheetGrid::Grid grid;
    bool header = false;
    for (const alignment::Alignment* a : targets) {
      SheetGrid::Grid part = alignment::BuildAlignmentReportCsv(*a, ReportFormatFor(st, *a, fmt));
      if (part.empty()) continue;
      if (!header) {
        grid.push_back(part.front());
        header = true;
      }
      for (size_t r = 1; r < part.size(); ++r) grid.push_back(std::move(part[r]));
    }
    std::string err;
    if (!SpreadsheetIo::WriteCsv(path, grid, &err)) {
      log.push_back("ALIGNMENTREPORT - could not write CSV \"" + path + "\": " + err + ".");
      return;
    }
    log.push_back("ALIGNMENTREPORT - CSV written to \"" + path + "\" (" + std::to_string(grid.size() - 1) +
                  " entity row(s)).");
    return;
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    log.push_back("ALIGNMENTREPORT - could not write \"" + path + "\".");
    return;
  }
  for (size_t i = 0; i < targets.size(); ++i) {
    std::vector<std::string> lines;
    alignment::BuildAlignmentReportLines(*targets[i], ReportFormatFor(st, *targets[i], fmt), &lines);
    for (size_t li = 0; li < lines.size(); ++li) {
      out << lines[li];
      if (li + 1 < lines.size()) out << '\n';
    }
    if (targets.size() > 1 && i + 1 < targets.size()) out << "\n---\n";
  }
  log.push_back("ALIGNMENTREPORT - report written to \"" + path + "\".");
}

// ALIGNMENTSURFACE <alignment>, <surface>, STATION=<station>[, OFFSET=<offset>]   one point      - REQ-497.2
//                  <alignment>, <surface>, INTERVAL=<interval>                    sampled table   - REQ-497.3
// The ground is read from the named surface. Elevations are session data: nothing is stored on the alignment.
void RunAlignmentSurface(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTSURFACE - usage: ALIGNMENTSURFACE <alignment>, <surface>, STATION=<station>[, OFFSET=<offset>] | "
      "<alignment>, <surface>, INTERVAL=<interval>.";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTSURFACE", f[0], log);
  if (a == nullptr) return;
  const int si = FindSurfaceIndex(st, f[1]);
  if (si < 0) {
    log.push_back("ALIGNMENTSURFACE - no surface named \"" + f[1] + "\".");
    return;
  }
  std::unique_ptr<ISurfaceQuery> ground = MakeSurfaceQueryForCommands(st, st.drawing.cadSurfaces[static_cast<size_t>(si)]);
  if (!ground) {
    log.push_back("ALIGNMENTSURFACE - \"" + f[1] + "\" is not a built surface.");
    return;
  }
  std::string station, offsetText, intervalText;
  for (size_t i = 2; i < f.size(); ++i) {
    const std::string key = Lower(f[i].substr(0, f[i].find('=')));
    const std::string value = f[i].find('=') == std::string::npos ? "" : Trim(f[i].substr(f[i].find('=') + 1));
    if (key == "station") station = value;
    else if (key == "offset") offsetText = value;
    else if (key == "interval") intervalText = value;
    else {
      log.push_back(usage);
      return;
    }
  }
  const std::string unit = " (surface \"" + f[1] + "\")";
  if (!intervalText.empty()) {
    double interval = 0.0;
    if (!ParseNum(intervalText, &interval) || !(interval > 0.0)) {
      log.push_back("ALIGNMENTSURFACE - the interval must be a positive number.");
      return;
    }
    std::vector<alignment::GroundPoint> samples;
    std::string err;
    if (!alignment::SampleGround(*a, *ground, interval, 100000, &samples, &err)) {
      log.push_back("ALIGNMENTSURFACE - " + err + ".");
      return;
    }
    int withGround = 0;
    for (const alignment::GroundPoint& g : samples) {
      if (g.hasGround) ++withGround;
    }
    log.push_back("ALIGNMENTSURFACE - \"" + a->name + "\": " + std::to_string(samples.size()) + " sample(s), " +
                  std::to_string(withGround) + " with ground" + unit + ".");
    for (const alignment::GroundPoint& g : samples) {
      const std::string sta = Sta(st, *a, g.internalStation);
      log.push_back("  STA " + sta + "  " + (g.hasGround ? "EG = " + Lin(st, g.ground) : std::string("no ground")));
    }
    return;
  }
  if (station.empty()) {
    log.push_back(usage);
    return;
  }
  double displayed = 0.0, offset = 0.0;
  if (!ParseSta(st, station, &displayed)) {
    log.push_back("ALIGNMENTSURFACE - \"" + station + "\" is not a station.");
    return;
  }
  if (!offsetText.empty() && !ParseNum(offsetText, &offset)) {
    log.push_back("ALIGNMENTSURFACE - the offset must be a number.");
    return;
  }
  const std::vector<double> raws = a->RawStationsForDisplayed(displayed);
  if (raws.empty()) {
    log.push_back("ALIGNMENTSURFACE - station " + StaFmt(st, displayed) + " is outside \"" + a->name + "\".");
    return;
  }
  for (const double raw : raws) {
    const alignment::GroundPoint g = alignment::GroundAt(*a, *ground, raw, offset);
    const std::string head = "ALIGNMENTSURFACE - STA " + StaFmt(st, displayed) + "  OFF " + Lin(st, offset);
    if (g.hasGround) {
      log.push_back(head + "  EG = " + Lin(st, g.ground) + unit + ".");
    } else {
      log.push_back(head + "  no ground: the point is off the surface" + unit + ".");
    }
  }
}

// ALIGNMENTLABEL <name>, STATIONLABEL, FIXED, <x>, <y>        a point that stays put (REQ-492.4)
//                  <name>, STATIONLABEL, RELATIVE, <station>, <offset>   a station/offset that moves with the alignment
// The new label's text is reported straight away. Labels belong to the alignment: deleting it removes them (REQ-492.6).
void RunAlignmentStationLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage = "ALIGNMENTLABEL - usage: ALIGNMENTLABEL <name>, STATIONLABEL, FIXED, <x>, <y> | <name>, STATIONLABEL, RELATIVE, <station>, <offset> | <name>, STATIONLABEL, LIST | <name>, STATIONLABEL, MOVE, <number>, <x>, <y>.";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  if (a == nullptr) return;
  const std::string sub = Lower(f[2]);
  if (sub == "list" && f.size() == 3) {  // REQ-492.4: the numbers MOVE takes
    if (a->stationLabels.empty()) log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has no station labels.");
    for (size_t i = 0; i < a->stationLabels.size(); ++i) {
      const alignment::StationOffsetLabelPlacement p = alignment::PlaceStationOffsetLabel(*a, a->stationLabels[i], st.displayLinearPrecision);
      log.push_back("  LABEL " + std::to_string(i + 1) + "  " + (a->stationLabels[i].fixed ? "fixed" : "relative") + "  " + p.text +
                    (p.onAlignment ? "" : " (off the alignment)"));
    }
    return;
  }
  if (sub == "move" && f.size() == 6) {  // REQ-492.5: drag a label to a new point
    double number = 0.0, x = 0.0, y = 0.0;
    if (!ParseNum(f[3], &number) || number != std::floor(number) || number < 1.0 ||
        number > static_cast<double>(a->stationLabels.size()) || !ParseNum(f[4], &x) || !ParseNum(f[5], &y)) {
      log.push_back("ALIGNMENTLABEL - MOVE needs a label number (1 to " + std::to_string(a->stationLabels.size()) + ") and a point.");
      return;
    }
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, x, y, &lx, &ly);
    alignment::StationOffsetLabel moved = a->stationLabels[static_cast<size_t>(number) - 1];
    if (!alignment::MoveStationOffsetLabelTo(*a, &moved, {lx, ly})) {
      log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has no geometry to label.");
      return;
    }
    PushUndoSnapshot(st, "Move alignment station label");
    a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
    a->stationLabels[static_cast<size_t>(number) - 1] = moved;
    const alignment::StationOffsetLabelPlacement p = alignment::PlaceStationOffsetLabel(*a, moved, st.displayLinearPrecision);
    log.push_back("ALIGNMENTLABEL - label " + std::to_string(static_cast<int>(number)) + " moved: " + p.text + ".");
    return;
  }
  if (f.size() != 5) {
    log.push_back(usage);
    return;
  }
  if (a->Empty()) {
    log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has no geometry to label.");
    return;
  }
  alignment::StationOffsetLabel label;
  const std::string mode = sub;
  if (mode == "fixed") {
    double x = 0.0, y = 0.0;
    if (!ParseNum(f[3], &x) || !ParseNum(f[4], &y)) {
      log.push_back("ALIGNMENTLABEL - the point must be two numbers.");
      return;
    }
    label.fixed = true;
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, x, y, &lx, &ly);
    label.fixedPoint = {lx, ly};
  } else if (mode == "relative") {
    double station = 0.0, offset = 0.0;
    if (!ParseSta(st, f[3], &station) || !ParseNum(f[4], &offset)) {
      log.push_back("ALIGNMENTLABEL - the station must be a station (12+45.32) and the offset a number.");
      return;
    }
    const std::vector<double> raws = a->RawStationsForDisplayed(station);
    if (raws.empty()) {
      log.push_back("ALIGNMENTLABEL - station " + StaFmt(st, station) + " is outside \"" + a->name + "\".");
      return;
    }
    label.internalStation = raws.front();  // a station that occurs twice (REQ-483.3) takes the first place
    label.offset = offset;
  } else {
    log.push_back(usage);
    return;
  }
  PushUndoSnapshot(st, "Add alignment station label");
  a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  a->stationLabels.push_back(label);
  const alignment::StationOffsetLabelPlacement p = alignment::PlaceStationOffsetLabel(*a, label, st.displayLinearPrecision);
  log.push_back("ALIGNMENTLABEL - " + std::string(label.fixed ? "fixed" : "relative") + " station label on \"" + a->name + "\": " + p.text +
                (p.onAlignment ? "" : " (off the alignment)") + ".");
}

// ======================================================================================================
// REQ-492.5: dragging a station label in the viewport. Same shape as the entity grip drag: press arms it, move updates
// it live, release commits one undo step, Esc or a cancel puts it back.
// ======================================================================================================

bool TryBeginAlignmentLabelDrag(AppCommandState& st, float lx, float ly, float tolWorld) {
  float bestD2 = tolWorld * tolWorld;
  int bestA = -1, bestL = -1;
  for (size_t ai = 0; ai < st.drawing.alignments.size(); ++ai) {
    const alignment::Alignment& a = st.drawing.alignments[ai];
    for (size_t li = 0; li < a.stationLabels.size(); ++li) {
      const alignment::StationOffsetLabelPlacement p =
          alignment::PlaceStationOffsetLabel(a, a.stationLabels[li], st.displayLinearPrecision);
      if (!p.onAlignment) continue;
      const double dx = lx - p.point.x, dy = ly - p.point.y;
      const float d2 = static_cast<float>(dx * dx + dy * dy);
      if (d2 <= bestD2) {
        bestD2 = d2;
        bestA = static_cast<int>(ai);
        bestL = static_cast<int>(li);
      }
    }
  }
  if (bestA < 0) return false;
  st.alignLabelDragActive = true;
  st.alignLabelDragMoved = false;
  st.alignLabelDragAlignment = bestA;
  st.alignLabelDragIndex = bestL;
  st.alignLabelDragOriginal = st.drawing.alignments[static_cast<size_t>(bestA)].stationLabels[static_cast<size_t>(bestL)];
  return true;
}

void ApplyAlignmentLabelDrag(AppCommandState& st, float lx, float ly) {
  if (!st.alignLabelDragActive) return;
  if (st.alignLabelDragAlignment < 0 || static_cast<size_t>(st.alignLabelDragAlignment) >= st.drawing.alignments.size()) return;
  alignment::Alignment& a = st.drawing.alignments[static_cast<size_t>(st.alignLabelDragAlignment)];
  if (st.alignLabelDragIndex < 0 || static_cast<size_t>(st.alignLabelDragIndex) >= a.stationLabels.size()) return;
  alignment::StationOffsetLabel& label = a.stationLabels[static_cast<size_t>(st.alignLabelDragIndex)];
  if (alignment::MoveStationOffsetLabelTo(a, &label, {static_cast<double>(lx), static_cast<double>(ly)})) {
    st.alignLabelDragMoved = true;
    BumpCadGpuCache(st);
  }
}

void FinishAlignmentLabelDrag(AppCommandState& st, bool commit, std::vector<std::string>& log) {
  if (!st.alignLabelDragActive) return;
  const int ai = st.alignLabelDragAlignment, li = st.alignLabelDragIndex;
  const bool valid = ai >= 0 && static_cast<size_t>(ai) < st.drawing.alignments.size() && li >= 0 &&
                     static_cast<size_t>(li) < st.drawing.alignments[static_cast<size_t>(ai)].stationLabels.size();
  st.alignLabelDragActive = false;
  if (!valid) return;
  alignment::Alignment& a = st.drawing.alignments[static_cast<size_t>(ai)];
  auto& label = a.stationLabels[static_cast<size_t>(li)];
  if (!commit || !st.alignLabelDragMoved) {  // cancel, or a click that did not move: nothing to record
    label = st.alignLabelDragOriginal;
    BumpCadGpuCache(st);
    return;
  }
  // One undo step for the whole drag: restore the original, take the snapshot, then put the moved label back.
  const alignment::StationOffsetLabel moved = label;
  label = st.alignLabelDragOriginal;
  PushUndoSnapshot(st, "Move alignment station label");
  st.drawing.alignments[static_cast<size_t>(ai)].stationLabels[static_cast<size_t>(li)] = moved;
  BumpCadGpuCache(st);
  const alignment::StationOffsetLabelPlacement p = alignment::PlaceStationOffsetLabel(
      st.drawing.alignments[static_cast<size_t>(ai)], moved, st.displayLinearPrecision);
  log.push_back("ALIGNMENTLABEL - label " + std::to_string(li + 1) + " moved: " + p.text + ".");
}

// EDITALIGNMENTSTYLE [<style>] - REQ-491. Opens the Alignment Style editor in the Qt editor; headless it lists the style.
void RunEditAlignmentStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentStyles::EnsureStandard(st.drawing.alignmentStyles);
  std::string name = Trim(args);
  if (name.empty() && !st.drawing.alignmentStyles.empty()) name = st.drawing.alignmentStyles.front().name;
  if (AlignmentStyles::Find(st.drawing.alignmentStyles, name) == nullptr) {
    log.push_back("EDITALIGNMENTSTYLE - no style named \"" + name + "\".");
    return;
  }
  st.alignmentStyleEditorRequested = true;
  st.alignmentStyleEditorName = name;
  RunAlignmentStyle(st, "LIST", log);  // the same list, for a window-less run
}

// ======================================================================================================
// REQ-498: feature lines made from an alignment (issue #121 Phase 5).
// ======================================================================================================

namespace {

int AlignmentIndexById(const AppCommandState& st, std::uint64_t id) {
  for (size_t i = 0; i < st.drawing.alignments.size(); ++i) {
    if (st.drawing.alignments[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

// Plan vertices for one feature line, with Z from its surface or its constant (REQ-498.1). Fails with the reason when a
// station has no ground on the surface, rather than writing a zero elevation (REQ-497.1).
bool BuildFeatureLineVerts(const AppCommandState& st, const alignment::Alignment& a, const CadFeatureLineInfo& info,
                           std::vector<double>* xyz, std::string* why) {
  const std::vector<double> stations = alignment::FeatureLineStations(a, info.sampleInterval, info.chordTolerance);
  if (stations.size() < 2) {
    if (why) *why = "the alignment has too little geometry to sample";
    return false;
  }
  std::unique_ptr<ISurfaceQuery> ground;
  if (info.hasSurface) {
    const int si = FindSurfaceIndex(st, info.surfaceName);
    if (si < 0) {
      if (why) *why = "the surface \"" + info.surfaceName + "\" is not in the drawing";
      return false;
    }
    ground = MakeSurfaceQueryForCommands(st, st.drawing.cadSurfaces[static_cast<size_t>(si)]);
    if (!ground) {
      if (why) *why = "the surface \"" + info.surfaceName + "\" is not a built surface";
      return false;
    }
  }
  xyz->clear();
  for (const double s : stations) {
    const double internal = a.StartStation() + s;
    const alignment::Point2 p = a.GetPointAtStation(internal);
    double z = info.constantZ;
    if (ground) {
      const alignment::GroundPoint g = alignment::GroundAt(a, *ground, internal, 0.0);
      if (!g.hasGround) {
        if (why) {
          *why = "there is no ground on \"" + info.surfaceName + "\" at station " +
                 StaFmt(st, a.DisplayedStation(internal, true));
        }
        return false;
      }
      z = g.ground;
    }
    xyz->push_back(p.x);
    xyz->push_back(p.y);
    xyz->push_back(z);
  }
  return true;
}

// Replaces the vertices of feature line `index` in place, shifting every later line's offsets by the change in count.
void ReplaceFeatureLineVerts(AppCommandState& st, size_t index, const std::vector<double>& xyz) {
  if (index + 1 >= st.drawing.featureLineOffsets.size()) return;
  const int a = st.drawing.featureLineOffsets[index], b = st.drawing.featureLineOffsets[index + 1];
  const int oldCount = b - a, newCount = static_cast<int>(xyz.size() / 3);
  const bool flagsMatch = st.drawing.featureLineElevPt.size() == st.drawing.featureLineVerts.size() / 3;
  st.drawing.featureLineVerts.erase(st.drawing.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(a) * 3,
                            st.drawing.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(b) * 3);
  st.drawing.featureLineVerts.insert(st.drawing.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(a) * 3, xyz.begin(), xyz.end());
  if (flagsMatch) {  // the vertices are elevation points, the flag FEATURELINE gives a typed elevation
    st.drawing.featureLineElevPt.erase(st.drawing.featureLineElevPt.begin() + a, st.drawing.featureLineElevPt.begin() + b);
    st.drawing.featureLineElevPt.insert(st.drawing.featureLineElevPt.begin() + a, static_cast<size_t>(newCount), static_cast<uint8_t>(1));
  }
  const int delta = newCount - oldCount;
  for (size_t j = index + 1; j < st.drawing.featureLineOffsets.size(); ++j) st.drawing.featureLineOffsets[j] += delta;
}

}  // namespace

// REQ-498.2: rebuilds every feature line that names an alignment, and drops the link (with a log line) when that alignment
// is gone. A line whose surface or ground can no longer be read keeps its last vertices and says why.
void RefreshFeatureLineLinks(AppCommandState& st, std::vector<std::string>& log) {
  bool changed = false;
  for (size_t i = 0; i < st.drawing.featureLineInfo.size(); ++i) {
    CadFeatureLineInfo& info = st.drawing.featureLineInfo[i];
    if (info.sourceAlignmentId == 0) continue;
    const int ai = AlignmentIndexById(st, info.sourceAlignmentId);
    if (ai < 0) {
      log.push_back("  Feature line \"" + info.name + "\" lost its alignment and is now an ordinary feature line.");
      info.sourceAlignmentId = 0;
      continue;
    }
    std::vector<double> xyz;
    std::string why;
    if (!BuildFeatureLineVerts(st, st.drawing.alignments[static_cast<size_t>(ai)], info, &xyz, &why)) {
      log.push_back("  Feature line \"" + info.name + "\" was not updated: " + why + ".");
      continue;
    }
    ReplaceFeatureLineVerts(st, i, xyz);
    changed = true;
  }
  if (changed) BumpCadGpuCache(st);
}

// FEATURELINEFROMALIGNMENT <alignment>[, INTERVAL=<n>][, TOL=<n>][, SURFACE=<name> | Z=<value>]   (REQ-498.1)
void RunFeatureLineFromAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "FEATURELINEFROMALIGNMENT - usage: FEATURELINEFROMALIGNMENT <alignment>[, INTERVAL=<n>][, TOL=<n>][, SURFACE=<name> | Z=<value>].";
  if (f.empty() || f[0].empty()) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "FEATURELINEFROMALIGNMENT", f[0], log);
  if (a == nullptr) return;
  if (a->Empty()) {
    log.push_back("FEATURELINEFROMALIGNMENT - \"" + a->name + "\" has no geometry.");
    return;
  }
  CadFeatureLineInfo info;
  info.name = a->name;
  info.description = "From alignment " + a->name;
  info.sourceAlignmentId = a->id;
  for (size_t i = 1; i < f.size(); ++i) {
    const size_t eq = f[i].find('=');
    const std::string key = Lower(f[i].substr(0, eq));
    const std::string value = eq == std::string::npos ? "" : Trim(f[i].substr(eq + 1));
    double v = 0.0;
    if (key == "interval" && ParseNum(value, &v) && v > 0.0) {
      info.sampleInterval = v;
    } else if (key == "tol" && ParseNum(value, &v) && v > 0.0) {
      info.chordTolerance = v;
    } else if (key == "surface" && !value.empty()) {
      if (FindSurfaceIndex(st, value) < 0) {
        log.push_back("FEATURELINEFROMALIGNMENT - no surface named \"" + value + "\".");
        return;
      }
      info.hasSurface = true;
      info.surfaceName = value;
    } else if (key == "z" && ParseNum(value, &v)) {
      info.hasSurface = false;
      info.constantZ = v;
    } else {
      log.push_back(usage);
      return;
    }
  }
  std::vector<double> xyz;
  std::string why;
  if (!BuildFeatureLineVerts(st, *a, info, &xyz, &why)) {
    log.push_back("FEATURELINEFROMALIGNMENT - " + why + ". No feature line was made.");
    return;
  }
  PushUndoSnapshot(st, "Feature line from alignment");
  if (st.drawing.featureLineOffsets.empty()) st.drawing.featureLineOffsets.push_back(0);
  const int base = st.drawing.featureLineOffsets.back();
  st.drawing.featureLineVerts.insert(st.drawing.featureLineVerts.end(), xyz.begin(), xyz.end());
  st.drawing.featureLineElevPt.resize(st.drawing.featureLineVerts.size() / 3, static_cast<uint8_t>(1));
  st.drawing.featureLineOffsets.push_back(base + static_cast<int>(xyz.size() / 3));
  st.drawing.featureLineClosed.push_back(0);
  const std::string zText = info.hasSurface ? "Z from \"" + info.surfaceName + "\"" : std::string("constant Z");
  const size_t count = xyz.size() / 3;
  st.drawing.featureLineInfo.push_back(std::move(info));
  AppendNewObjectAttrs(st, st.drawing.featureLineAttrs, st.drawing.featureLineInfo.size(), ObjectLayerKind::FeatureLine,
                       st.drawing.featureLineInfo.back().name);  // REQ-361
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
  log.push_back("FEATURELINEFROMALIGNMENT - feature line \"" + a->name + "\" made from the alignment: " +
                std::to_string(count) + " vertices, " + zText + ".");
}

// ======================================================================================================
// REQ-500: the profile seam, and QUICKPROFILE along an alignment (issue #121 Phase 5).
// ======================================================================================================

// QUICKPROFILE <surface>, <alignment>   (REQ-500 AC3). Samples the surface along the alignment from its start to its end,
// one foot at a time (REQ-497.3), and stores the result in the same session graph the two-point command uses. Returns false
// only when no alignment has that name, so the caller can report its usage line instead.
bool RunQuickProfileAlongAlignment(AppCommandState& st, const std::string& surfaceName, const std::string& alignmentName,
                                   std::vector<std::string>& log) {
  const int ai = [&]() {
    const std::string want = Lower(alignmentName);
    for (size_t i = 0; i < st.drawing.alignments.size(); ++i) {
      if (Lower(st.drawing.alignments[i].name) == want) return static_cast<int>(i);
    }
    return -1;
  }();
  if (ai < 0) return false;
  const alignment::Alignment& a = st.drawing.alignments[static_cast<size_t>(ai)];
  const int si = FindSurfaceIndex(st, surfaceName);
  if (si < 0) {
    log.push_back("QUICKPROFILE - no surface named \"" + surfaceName + "\".");
    return true;
  }
  if (a.Empty()) {
    log.push_back("QUICKPROFILE - \"" + a.name + "\" has no geometry to sample.");
    return true;
  }
  std::unique_ptr<ISurfaceQuery> ground = MakeSurfaceQueryForCommands(st, st.drawing.cadSurfaces[static_cast<size_t>(si)]);
  if (!ground) {
    log.push_back("QUICKPROFILE - \"" + surfaceName + "\" is not a built surface.");
    return true;
  }
  std::vector<alignment::GroundPoint> groundSamples;
  std::string err;
  if (!alignment::SampleGround(a, *ground, kQuickProfileStepFt, kQuickProfileMaxSamples, &groundSamples, &err)) {
    log.push_back("QUICKPROFILE - " + err + ".");
    return true;
  }
  std::vector<SurfaceProfileSample> samples;
  samples.reserve(groundSamples.size());
  int hits = 0;
  double minZ = 0.0, maxZ = 0.0;
  for (const alignment::GroundPoint& g : groundSamples) {
    SurfaceProfileSample p;
    p.station = g.internalStation - a.StartStation();
    p.x = g.point.x;
    p.y = g.point.y;
    p.z = g.ground;
    p.onSurface = g.hasGround;
    if (p.onSurface) {
      minZ = hits == 0 ? p.z : std::min(minZ, p.z);
      maxZ = hits == 0 ? p.z : std::max(maxZ, p.z);
      ++hits;
    }
    samples.push_back(p);
  }
  if (hits == 0) {
    log.push_back("QUICKPROFILE - no samples on the surface (outside).");
    return true;
  }
  const int p = st.displayLinearPrecision;
  st.quickProfile.open = true;
  st.quickProfile.hasResult = true;
  st.quickProfile.surfaceName = surfaceName;
  st.quickProfile.length = samples.back().station;
  st.quickProfile.onSurfaceCount = hits;
  st.quickProfile.minZ = minZ;
  st.quickProfile.maxZ = maxZ;
  st.quickProfile.samples = std::move(samples);
  log.push_back("QUICKPROFILE - \"" + surfaceName + "\" along \"" + a.name + "\": length " + FormatLinear(st.quickProfile.length, p) +
                ", " + std::to_string(st.quickProfile.samples.size()) + " sample(s), " + std::to_string(hits) +
                " on surface, elev " + FormatLinear(minZ, p) + " to " + FormatLinear(maxZ, p) + ".");
  return true;
}

// ======================================================================================================
// REQ-501: alignment properties and COGO (issue #121 Phase 5).
// ======================================================================================================

// ALIGNMENTPROPS <alignment>, NAME|DESCRIPTION|STYLE|LAYER|START, <value>   (REQ-501.1)
// The editable Properties fields. Names stay unique; a style must exist; the start station cannot change while the
// alignment has station equations, because the equations are stated against the old stations (REQ-483.1).
void RunAlignmentProps(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 3) {
    log.push_back("ALIGNMENTPROPS - usage: ALIGNMENTPROPS <alignment>, NAME|DESCRIPTION|STYLE|LAYER|START, <value>.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTPROPS", f[0], log);
  if (a == nullptr) return;
  const std::string field = Lower(f[1]);
  const std::string value = Trim(f[2]);
  alignment::Alignment edited = *a;
  if (field == "name") {
    if (value.empty()) {
      log.push_back("ALIGNMENTPROPS - the name cannot be empty.");
      return;
    }
    if (FindAlignment(st, value) >= 0 && Lower(value) != Lower(a->name)) {
      log.push_back("ALIGNMENTPROPS - an alignment named \"" + value + "\" already exists; names must be unique.");
      return;
    }
    edited.name = value;
  } else if (field == "description") {
    edited.description = value;
  } else if (field == "style") {
    if (AlignmentStyles::Find(st.drawing.alignmentStyles, value) == nullptr) {
      log.push_back("ALIGNMENTPROPS - no style named \"" + value + "\".");
      return;
    }
    edited.style = value;
  } else if (field == "layer") {
    if (value.empty()) {
      log.push_back("ALIGNMENTPROPS - the layer cannot be empty.");
      return;
    }
    edited.layer = value;
  } else if (field == "start") {
    double sta = 0.0;
    if (!ParseSta(st, value, &sta)) {
      log.push_back("ALIGNMENTPROPS - \"" + value + "\" is not a station.");
      return;
    }
    if (!edited.equations.empty()) {
      log.push_back("ALIGNMENTPROPS - \"" + a->name + "\" has " + std::to_string(edited.equations.size()) +
                    " station equation(s), stated against the current stations. Remove them with ALIGNMENTSTATION first.");
      return;
    }
    edited.startingStation = sta;
  } else {
    log.push_back("ALIGNMENTPROPS - field must be NAME, DESCRIPTION, STYLE, LAYER or START, not \"" + f[1] + "\".");
    return;
  }
  const bool same = edited.name == a->name && edited.description == a->description && edited.style == a->style &&
                    edited.layer == a->layer && edited.startingStation == a->startingStation;
  if (same) {
    log.push_back("ALIGNMENTPROPS - " + f[1] + " is already \"" + value + "\".");
    return;
  }
  PushUndoSnapshot(st, "Alignment properties");
  *Resolve(st, "ALIGNMENTPROPS", f[0], log) = std::move(edited);
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTPROPS - \"" + value + "\" set as the " + f[1] + ".");
}

// REQ-502: design criteria. ALIGNMENTCHECKSET NEW|DELETE|LIST|SET manages drawing-owned check sets; ALIGNMENTCRITERIA applies
// one to an alignment; ALIGNMENTSPEED sets the design speed from a station; ALIGNMENTCHECK lists the failed checks.
