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

alignment::Alignment* FindById(AppCommandState& st, std::uint64_t id) {
  for (alignment::Alignment& a : st.drawing.alignments) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

/// REQ-488.3: a linked offset follows its parent. Called after anything that changes a parent. A parent that no
/// longer exists detaches the offset (it keeps its geometry); an offset that can no longer be built keeps its last
/// geometry and says why.
void RefreshLinkedOffsetsImpl(AppCommandState& st, std::vector<std::string>& log) {
  for (alignment::Alignment& off : st.drawing.alignments) {
    if (off.parentId == 0) continue;
    alignment::Alignment* parent = FindById(st, off.parentId);
    if (parent == nullptr) {
      off.parentId = 0;
      log.push_back("  Offset \"" + off.name + "\" lost its parent and is now independent.");
      continue;
    }
    alignment::Alignment rebuilt = off;
    std::string why;
    alignment::Alignment source = *parent;
    const bool ranged = off.HasOffsetRange();
    if ((!ranged || parent->ExtractRange(off.offsetFromStation, off.offsetToStation, &source, &why)) &&
        alignment::MakeOffset(source, off.offsetDistance, &rebuilt, &why)) {
      rebuilt.startingStation = off.startingStation;
      rebuilt.parentId = off.parentId;  // an extracted range carries no id of its own
      rebuilt.offsetFromStation = off.offsetFromStation;
      rebuilt.offsetToStation = off.offsetToStation;
      off = std::move(rebuilt);
    } else {
      log.push_back("  Offset \"" + off.name + "\" could not follow \"" + parent->name + "\": " + why + ".");
    }
  }
}

/// Parses "<entity#>" (1-based) into a 0-based index within `a`, or reports.
bool ParseEntityIndex(const alignment::Alignment& a, const std::string& cmd, const std::string& text, int* out,
                      std::vector<std::string>& log) {
  double v = 0.0;
  if (!ParseNum(text, &v) || v != std::floor(v) || v < 1.0 || v > static_cast<double>(a.Entities().size())) {
    log.push_back(cmd + " - \"" + text + "\" is not an entity number (1 to " + std::to_string(a.Entities().size()) + ").");
    return false;
  }
  *out = static_cast<int>(v) - 1;
  return true;
}

bool IsLinkedOffset(const alignment::Alignment& a, const std::string& cmd, std::vector<std::string>& log) {
  if (a.parentId == 0) return false;
  log.push_back(cmd + " - \"" + a.name + "\" is an offset linked to its parent; edit the parent, or ALIGNMENTDETACH it first.");
  return true;
}

/// REQ-483.5: equations a geometry edit left beyond the new end are listed by name; UNDO brings them back.
void ReportDroppedEquations(const AppCommandState& st, const alignment::Alignment& before, const alignment::Alignment& after,
                            std::vector<std::string>& log) {
  assert(after.equations.size() <= before.equations.size() + after.equations.size());
  assert(before.equations.size() < 100000);
  for (const alignment::StationEquation& q : before.equations) {
    const bool kept = std::any_of(after.equations.begin(), after.equations.end(),
                                  [&](const alignment::StationEquation& o) { return o.rawBack == q.rawBack; });
    if (!kept)
      log.push_back("  Station equation at raw station " + StaFmt(st, q.rawBack) + " (ahead " + StaFmt(st, q.ahead) +
                    ") now lies beyond the end of \"" + after.name + "\" and was removed; UNDO restores it.");
  }
}

}  // namespace

// ======================================================================================================
// Phase 4 (REQ-492.3, REQ-493): segment labelling and drawing tables.
// ======================================================================================================

namespace {

const char* TableKindLabel(alignment::AlignmentTableKind k) {
  switch (k) {
    case alignment::AlignmentTableKind::Line: return "Line";
    case alignment::AlignmentTableKind::Curve: return "Curve";
    case alignment::AlignmentTableKind::Spiral: return "Spiral";
    case alignment::AlignmentTableKind::Segment: return "Segment";
    case alignment::AlignmentTableKind::StationEquation: return "Station equation";
  }
  return "?";
}

bool ParseTableKind(const std::string& word, alignment::AlignmentTableKind* k) {
  if (word == "line") *k = alignment::AlignmentTableKind::Line;
  else if (word == "curve") *k = alignment::AlignmentTableKind::Curve;
  else if (word == "spiral") *k = alignment::AlignmentTableKind::Spiral;
  else if (word == "segment" || word == "segments") *k = alignment::AlignmentTableKind::Segment;
  else if (word == "equation" || word == "equations") *k = alignment::AlignmentTableKind::StationEquation;
  else return false;
  return true;
}

/// REQ-493.2: rewrites every DYNAMIC table from its alignment's current geometry. A static table is a frozen snapshot and
/// is never touched. A table whose alignment can no longer be tabulated (labels removed) keeps its last text and says so.
void RefreshDynamicTables(AppCommandState& st, std::vector<std::string>& log) {
  bool any = false;
  for (const alignment::Alignment& a : st.drawing.alignments) {
    for (const alignment::AlignmentTableLink& link : a.tables) {
      if (!link.dynamic) continue;
      const int ti = FindEntityIndexById(st.drawing.cadTableAttrs, link.tableId);
      if (ti < 0 || static_cast<size_t>(ti) >= st.drawing.cadTables.size()) continue;
      alignment::AlignmentTableText text;
      std::string why;
      if (!alignment::BuildAlignmentTableText(a, link.kind, st.displayLinearPrecision, CadAngleDisplaySettings(st), &text, &why, st.drawing.alignmentStationFormat)) {
        log.push_back("  Table for \"" + a.name + "\" was not updated: " + why + ".");
        continue;
      }
      if (link.firstRow != 0 || link.rowCount != 0) text = alignment::SliceTableRows(text, link.firstRow, link.rowCount);
      CadTable& tbl = st.drawing.cadTables[static_cast<size_t>(ti)];
      tbl.cols = text.cols;
      tbl.cells = text.cells;
      CadTableFitToContent(&tbl, st.drawing.modelUnitsPerPlottedInch);
      any = true;
    }
  }
  if (any) BumpCadGpuCache(st);
}

}  // namespace

namespace {

// ALIGNMENTLABEL <name>, SEGMENT, <entity#>: REQ-492.3 Single Segment label on one picked segment.
void RunAlignmentSingleSegmentLabel(AppCommandState& st, const std::vector<std::string>& f, std::vector<std::string>& log) {
  assert(f.size() == 3);
  assert(st.drawing.alignments.size() < 100000000);
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  if (a == nullptr) return;
  double n = 0.0;
  const int count = static_cast<int>(a->Entities().size());
  if (!ParseNum(f[2], &n) || n != std::floor(n) || n < 1.0 || n > static_cast<double>(count)) {
    log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has entities 1 to " + std::to_string(count) + "; \"" + f[2] +
                  "\" is not one of them.");
    return;
  }
  const int i = static_cast<int>(n) - 1;
  if (alignment::SegmentIsLabelled(*a, i)) {
    log.push_back("ALIGNMENTLABEL - entity " + std::to_string(i + 1) + " of \"" + a->name + "\" is already labelled.");
    return;
  }
  PushUndoSnapshot(st, "Label alignment segment");
  a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  a->segmentLabelStations.push_back(0.5 * (a->EntityStartStation(i) + a->EntityEndStation(i)));
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTLABEL - entity " + std::to_string(i + 1) + " of \"" + a->name + "\" labelled.");
}

}  // namespace

// ALIGNMENTLABEL <name>, SEGMENTS | SEGMENT, <entity#> | CLEAR   (REQ-492.3 Multiple and Single Segment labels;
// REQ-493.3: a line, curve, spiral or segment table lists the labelled segments)
void RunAlignmentStationLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log);

void RunAlignmentLabel(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() >= 2 && Lower(f[1]) == "stationlabel") {  // REQ-492.4
    RunAlignmentStationLabel(st, args, log);
    return;
  }
  const char* usage = "ALIGNMENTLABEL - usage: ALIGNMENTLABEL <name>, SEGMENTS | SEGMENT, <entity#> | CLEAR.";
  const std::string what = f.size() >= 2 ? Lower(f[1]) : std::string{};
  if (what == "segment" && f.size() == 3) {
    RunAlignmentSingleSegmentLabel(st, f, log);
    return;
  }
  if (f.size() != 2 || (what != "segments" && what != "clear")) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  if (a == nullptr) return;
  const bool on = what == "segments";
  if (on && a->Empty()) {
    log.push_back("ALIGNMENTLABEL - \"" + a->name + "\" has no geometry to label.");
    return;
  }
  if (a->segmentLabels == on && (on || a->segmentLabelStations.empty())) {
    log.push_back("ALIGNMENTLABEL - the segments of \"" + a->name + "\" are already " + (on ? "labelled." : "unlabelled."));
    return;
  }
  PushUndoSnapshot(st, on ? "Label alignment segments" : "Clear alignment segment labels");
  a = Resolve(st, "ALIGNMENTLABEL", f[0], log);
  a->segmentLabels = on;
  a->segmentLabelStations.clear();  // Multiple Segment covers every segment; CLEAR removes the single ones too
  log.push_back(on ? "ALIGNMENTLABEL - segments of \"" + a->name + "\" labelled; line, curve and segment tables can now be made."
                   : "ALIGNMENTLABEL - segment labels of \"" + a->name + "\" cleared. Existing line and curve tables stop updating.");
}

namespace {

struct AlignmentTableOptions {
  bool dynamic = true;
  int splitRows = 0;  ///< 0 = one table
  std::string style;
  bool hasPoint = false;
  double wx = 0.0, wy = 0.0;
};

/// Reads `[STATIC|DYNAMIC][, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>]` in any order. False (with a message) on bad input.
bool ParseAlignmentTableOptions(const std::vector<std::string>& f, AlignmentTableOptions* o, std::vector<std::string>& log) {
  assert(o != nullptr);
  assert(f.size() >= 2);
  std::vector<std::string> numbers;
  for (size_t i = 2; i < f.size(); ++i) {
    const std::string w = Lower(f[i]);
    if (w == "static" || w == "dynamic") {
      o->dynamic = w == "dynamic";
    } else if (w.rfind("split=", 0) == 0) {
      double rows = 0.0;
      if (!ParseNum(f[i].substr(6), &rows) || rows < 1.0 || rows > 10000.0 || rows != std::floor(rows)) {
        log.push_back("ALIGNMENTTABLE - SPLIT= needs a whole number of data rows per table, 1 or more.");
        return false;
      }
      o->splitRows = static_cast<int>(rows);
    } else if (w.rfind("style=", 0) == 0) {
      o->style = f[i].substr(6);
    } else {
      numbers.push_back(f[i]);
    }
  }
  if (numbers.empty()) return true;
  if (numbers.size() != 2 || !ParseNum(numbers[0], &o->wx) || !ParseNum(numbers[1], &o->wy)) {
    log.push_back("ALIGNMENTTABLE - the insertion point must be two numbers.");
    return false;
  }
  o->hasPoint = true;
  return true;
}

/// The data table style a new alignment table uses: STYLE= when given, else the current table style, else Standard
/// (REQ-493.1: the table style is a REQ-478 table style). Null when STYLE= names no data table style.
const CadTableStyle* AlignmentTableStyle(const AppCommandState& st, const std::string& requested) {
  assert(st.drawing.tableStyles.size() < 100000);
  assert(requested.size() < 4096);
  if (!requested.empty()) {
    const CadTableStyle* s = CadTableFindStyle(st.drawing.tableStyles, requested);
    return s != nullptr && !s->point ? s : nullptr;
  }
  const CadTableStyle* current = CadTableFindStyle(st.drawing.tableStyles, st.tablePendingStyle);
  return current != nullptr && !current->point ? current : CadTableFindStyle(st.drawing.tableStyles, "Standard");
}

/// Adds one drawing TABLE holding `text` with its top-left at drawing-local (lx, ly); returns its stable id.
std::uint64_t InsertAlignmentCadTable(AppCommandState& st, const alignment::AlignmentTableText& text,
                                      const CadTableStyle* style, double lx, double ly, float* width) {
  assert(text.cols > 0);
  assert(width != nullptr);
  CadTable tbl;
  tbl.cols = text.cols;
  tbl.cells = text.cells;
  if (const TextStyle* ts = ActiveTextStyle(st)) tbl.fontFamily = ts->fontFamily;
  tbl.plottedHeightInches = st.drawing.defaultPlottedTextHeightInches;
  if (style != nullptr) {
    tbl.styleName = style->name;
    tbl.plottedHeightInches = style->textHeightInches;
    if (const TextStyle* ts = TextStyles::Find(st.drawing.textStyles, style->textStyleName)) tbl.fontFamily = ts->fontFamily;
  }
  CadTableFitToContent(&tbl, st.drawing.modelUnitsPerPlottedInch);
  tbl.insX = static_cast<float>(lx);
  tbl.insY = static_cast<float>(ly) + tbl.height;  // top-left, body below it, as VOLTABLE places its table
  tbl.insZ = CadCommitElevation(st);
  *width = tbl.width;
  st.drawing.cadTables.push_back(std::move(tbl));
  AppendNewObjectAttrs(st, st.drawing.cadTableAttrs, st.drawing.cadTables.size(), ObjectLayerKind::Table, {});  // REQ-361
  EnsureAttrCounts(st);
  st.drawing.cadTableAttrs.back().id = st.nextEntityId++;  // the link needs a stable id now, not at the next sweep (REQ-076)
  return st.drawing.cadTableAttrs.back().id;
}

}  // namespace

// ALIGNMENTTABLE <name>, LINE|CURVE|SPIRAL|SEGMENT|EQUATIONS[, STATIC|DYNAMIC][, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>]
// (REQ-493.1 / .2). Inserts a drawing TABLE at the world point (top-left), default the drawing origin. DYNAMIC (the
// default) rebuilds the table whenever the alignment changes; STATIC is a frozen snapshot. SPLIT= puts at most that many
// data rows in each table and sets the parts side by side, each with the header, as Civil 3D's Split Table does.
void RunAlignmentTable(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage = "ALIGNMENTTABLE - usage: ALIGNMENTTABLE <name>, LINE|CURVE|SPIRAL|SEGMENT|EQUATIONS[, STATIC|DYNAMIC]"
                      "[, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>].";
  alignment::AlignmentTableKind kind{};
  if (f.size() < 2 || !ParseTableKind(Lower(f[1]), &kind)) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTTABLE", f[0], log);
  AlignmentTableOptions opt;
  if (a == nullptr || !ParseAlignmentTableOptions(f, &opt, log)) return;
  const CadTableStyle* style = AlignmentTableStyle(st, opt.style);
  if (!opt.style.empty() && style == nullptr) {
    log.push_back("ALIGNMENTTABLE - no data table style named \"" + opt.style + "\". No table was made.");
    return;
  }
  alignment::AlignmentTableText text;
  std::string why;
  if (!alignment::BuildAlignmentTableText(*a, kind, st.displayLinearPrecision, CadAngleDisplaySettings(st), &text, &why, st.drawing.alignmentStationFormat)) {
    log.push_back("ALIGNMENTTABLE - " + why + ". No table was made.");
    return;
  }
  const int rows = static_cast<int>(text.cells.size() / static_cast<size_t>(text.cols)) - 1;
  const int parts = opt.splitRows > 0 ? std::max(1, (rows + opt.splitRows - 1) / opt.splitRows) : 1;
  PushUndoSnapshot(st, "Insert alignment table");
  a = Resolve(st, "ALIGNMENTTABLE", f[0], log);
  double lx = 0.0, ly = 0.0;
  if (opt.hasPoint) CadCoord::LocalFromWorld(st, opt.wx, opt.wy, &lx, &ly);
  for (int part = 0; part < parts && part < 10000; ++part) {
    const int first = part * opt.splitRows;
    const int count = part + 1 < parts ? opt.splitRows : 0;  // the last part takes the rest, so it can grow
    float width = 0.0f;
    const alignment::AlignmentTableText slice = parts > 1 ? alignment::SliceTableRows(text, first, count) : text;
    const std::uint64_t id = InsertAlignmentCadTable(st, slice, style, lx, ly, &width);
    a->tables.push_back({id, kind, opt.dynamic, parts > 1 ? first : 0, parts > 1 ? count : 0});
    lx += static_cast<double>(width) * 1.05;  // Civil 3D tiles split parts across, a small gap apart
  }
  BumpCadGpuCache(st);
  log.push_back("ALIGNMENTTABLE - " + std::string(TableKindLabel(kind)) + " table for \"" + a->name + "\" inserted (" +
                (opt.dynamic ? "dynamic" : "static") + ", " + std::to_string(rows) + " row(s)" +
                (parts > 1 ? " in " + std::to_string(parts) + " tables" : std::string()) + ", style " +
                (style != nullptr ? style->name : std::string("Standard")) + ").");
}

void RefreshAlignmentOffsetsOnly(AppCommandState& st, std::vector<std::string>& log) {
  RefreshLinkedOffsetsImpl(st, log);
  RefreshDynamicTables(st, log);  // REQ-493.2: dynamic tables follow the geometry
  RefreshViewFrameGroups(st, log);  // REQ-517.8: view frame groups regenerate with the alignment
}

void RefreshAlignmentOffsets(AppCommandState& st, std::vector<std::string>& log) {
  RefreshAlignmentOffsetsOnly(st, log);
  RefreshFeatureLineLinks(st, log);  // REQ-498.2: feature lines made from an alignment follow it
  for (const alignment::Alignment& alignment : st.drawing.alignments)
    RefreshSurfaceProfilesForAlignment(st, alignment.id, log);
}

// ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|DELTA|DIRECTION|CONSTRAINT, <value>
void RunAlignmentEdit(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTEDIT - usage: ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|STARTRADIUS|DELTA|DIRECTION|CONSTRAINT|"
      "DEGREE|TANGENTLENGTH|CHORD|EXTERNAL|MIDORDINATE|A, <value> | <name>, <entity#>, THROUGH, <x>, <y> "
      "(DIRECTION LEFT|RIGHT; CONSTRAINT FIXED|FLOATPREV|FLOATNEXT|FREE; spiral RADIUS/STARTRADIUS 0 = tangent).";
  const bool through = f.size() == 5 && Lower(f[2]) == "through";
  if (f.size() != 4 && !through) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTEDIT", f[0], log);
  if (a == nullptr || IsLinkedOffset(*a, "ALIGNMENTEDIT", log)) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTEDIT", f[1], &idx, log)) return;
  if (through) {  // REQ-481.5a Curve through a point
    double wx = 0.0, wy = 0.0, lx = 0.0, ly = 0.0;
    if (!ParseNum(f[3], &wx) || !ParseNum(f[4], &wy)) {
      log.push_back("ALIGNMENTEDIT - THROUGH needs the point as two numbers, <x>, <y>.");
      return;
    }
    CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
    alignment::Alignment trial = *a;
    std::string why;
    if (!trial.EditCurveThroughPoint(idx, {lx, ly}, &why)) {
      log.push_back("ALIGNMENTEDIT - refused: " + why + ". Nothing changed.");
      return;
    }
    PushUndoSnapshot(st, "Edit alignment");
    *Resolve(st, "ALIGNMENTEDIT", f[0], log) = std::move(trial);
    log.push_back("ALIGNMENTEDIT - entity " + f[1] + " of \"" + f[0] + "\" now passes through (" + f[3] + ", " + f[4] + ").");
    RefreshAlignmentOffsets(st, log);
    return;
  }
  const alignment::EntityKind kind = a->Entities()[static_cast<size_t>(idx)].kind;
  const bool isArc = kind == alignment::EntityKind::Arc;
  const bool isSpiral = kind == alignment::EntityKind::Spiral;
  const std::string what = Lower(f[2]);
  alignment::EditParam param{};
  double v = 0.0;
  if (what == "bearing" || what == "azimuth") {
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the bearing must be a number of degrees (azimuth from north)."); return; }
    param = alignment::EditParam::LineAzimuth;
    v *= kDegToRad;
  } else if (what == "length") {
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the length must be a number."); return; }
    param = (isArc || isSpiral) ? alignment::EditParam::CurveLength : alignment::EditParam::LineLength;
  } else if (what == "radius") {
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the radius must be a number."); return; }
    param = alignment::EditParam::CurveRadius;
  } else if (what == "startradius" || what == "rs") {
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the start radius must be a number (0 = tangent)."); return; }
    param = alignment::EditParam::SpiralStartRadius;
  } else if (what == "delta") {
    if (isSpiral) {
      log.push_back("ALIGNMENTEDIT - a spiral has no delta; edit LENGTH or RADIUS instead.");
      return;
    }
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the delta must be a number of degrees."); return; }
    param = alignment::EditParam::CurveDelta;
    v *= kDegToRad;
  } else if (what == "direction") {
    const std::string d = Lower(f[3]);
    if (d != "left" && d != "right") { log.push_back("ALIGNMENTEDIT - the direction must be LEFT or RIGHT."); return; }
    param = alignment::EditParam::CurveDirection;
    v = d == "right" ? 1.0 : 0.0;
  } else if (what == "constraint") {
    const std::string c = Lower(f[3]);
    param = alignment::EditParam::Constraint;
    if (c == "fixed") v = 0.0;
    else if (c == "floatprev" || c == "floatonprev") v = 1.0;
    else if (c == "floatnext" || c == "floatonnext") v = 2.0;
    else if (c == "free") v = 3.0;
    else { log.push_back("ALIGNMENTEDIT - the constraint must be FIXED, FLOATPREV, FLOATNEXT or FREE."); return; }
  } else if (what == "degree" || what == "tangentlength" || what == "chord" || what == "external" ||
             what == "midordinate" || what == "middleordinate" || what == "a") {  // REQ-481.5a definitions
    if (!ParseNum(f[3], &v)) { log.push_back("ALIGNMENTEDIT - the " + f[2] + " must be a number."); return; }
    param = what == "degree"          ? alignment::EditParam::CurveDegree
            : what == "tangentlength" ? alignment::EditParam::CurveTangent
            : what == "chord"         ? alignment::EditParam::CurveChord
            : what == "external"      ? alignment::EditParam::CurveExternal
            : what == "a"             ? alignment::EditParam::SpiralA
                                      : alignment::EditParam::CurveMiddleOrdinate;
  } else {
    log.push_back(usage);
    return;
  }
  alignment::Alignment trial = *a;
  std::string why;
  if (!trial.EditEntity(idx, param, v, &why)) {
    log.push_back("ALIGNMENTEDIT - refused: " + why + ". Nothing changed.");
    return;
  }
  PushUndoSnapshot(st, "Edit alignment");
  ReportDroppedEquations(st, *Resolve(st, "ALIGNMENTEDIT", f[0], log), trial, log);
  alignment::Alignment* target = Resolve(st, "ALIGNMENTEDIT", f[0], log);
  *target = std::move(trial);
  log.push_back("ALIGNMENTEDIT - \"" + target->name + "\" entity " + std::to_string(idx + 1) + " updated; length now " + Lin(st, target->Length()) +
                ", ending at station " + Sta(st, *target, target->EndStation()) + ".");
  ReportContinuity(st, *target, log);
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF
void RunAlignmentLock(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 4) {
    log.push_back("ALIGNMENTLOCK - usage: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTLOCK", f[0], log);
  if (a == nullptr) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTLOCK", f[1], &idx, log)) return;
  const std::string what = Lower(f[2]), onoff = Lower(f[3]);
  unsigned char bit = 0;
  if (what == "radius") bit = alignment::Entity::kLockRadius;
  else if (what == "length") bit = alignment::Entity::kLockLength;
  else if (what == "bearing") bit = alignment::Entity::kLockBearing;
  else if (what == "station") bit = alignment::Entity::kLockStation;  // REQ-486.3: its start station stays put
  if (bit == 0 || (onoff != "on" && onoff != "off")) {
    log.push_back("ALIGNMENTLOCK - usage: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF.");
    return;
  }
  unsigned char mask = a->Entities()[static_cast<size_t>(idx)].locks;
  mask = onoff == "on" ? static_cast<unsigned char>(mask | bit) : static_cast<unsigned char>(mask & ~bit);
  PushUndoSnapshot(st, "Lock alignment parameter");
  a = Resolve(st, "ALIGNMENTLOCK", f[0], log);
  std::string why;
  (void)a->EditEntity(idx, alignment::EditParam::Locks, static_cast<double>(mask), &why);
  log.push_back("ALIGNMENTLOCK - entity " + std::to_string(idx + 1) + " of \"" + a->name + "\": " + what + (onoff == "on" ? " locked." : " unlocked."));
}

// ALIGNMENTINSERT <name>, <after entity#>, TANGENT, <length> | CURVE, <radius>, <delta deg>, LEFT|RIGHT
//                 | SPIRAL, <length>, <end radius>, LEFT|RIGHT | SPIRAL, <length>, <start R>, <end R>, LEFT|RIGHT
void RunAlignmentInsert(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTINSERT - usage: ALIGNMENTINSERT <name>, <after entity#>, TANGENT, <length> | CURVE, <radius>, <delta deg>, "
      "LEFT|RIGHT | SPIRAL, <length>, <end radius>, LEFT|RIGHT | SPIRAL, <length>, <start radius>, <end radius>, LEFT|RIGHT.";
  if (f.size() < 4) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTINSERT", f[0], log);
  if (a == nullptr || IsLinkedOffset(*a, "ALIGNMENTINSERT", log)) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTINSERT", f[1], &idx, log)) return;
  alignment::Alignment trial = *a;
  std::string why;
  bool ok = false;
  double v1 = 0.0, v2 = 0.0;
  const std::string kind = Lower(f[2]);
  if (kind == "tangent" && f.size() == 4 && ParseNum(f[3], &v1)) {
    ok = trial.InsertTangentAfter(idx, v1, &why);
  } else if (kind == "curve" && f.size() == 6 && ParseNum(f[3], &v1) && ParseNum(f[4], &v2) &&
             (Lower(f[5]) == "left" || Lower(f[5]) == "right")) {
    ok = trial.InsertCurveAfter(idx, v1, v1 * v2 * kDegToRad, Lower(f[5]) == "right", &why);
  } else if (kind == "spiral" && (f.size() == 5 || f.size() == 7)) {
    const std::string side = Lower(f.back());
    if (side != "left" && side != "right") {
      log.push_back(usage);
      return;
    }
    double ls = 0.0, rs = 0.0, re = 0.0;
    if (f.size() == 5) {
      if (!ParseNum(f[3], &ls) || !ParseNum(f[4], &re)) {
        log.push_back(usage);
        return;
      }
    } else {
      if (!ParseNum(f[3], &ls) || !ParseNum(f[4], &rs) || !ParseNum(f[5], &re)) {
        log.push_back(usage);
        return;
      }
    }
    ok = trial.InsertSpiralAfter(idx, ls, rs, re, side == "right", &why);
  } else {
    log.push_back(usage);
    return;
  }
  if (!ok) {
    log.push_back("ALIGNMENTINSERT - refused: " + why + ". Nothing changed.");
    return;
  }
  PushUndoSnapshot(st, "Insert alignment entity");
  ReportDroppedEquations(st, *Resolve(st, "ALIGNMENTINSERT", f[0], log), trial, log);
  alignment::Alignment* target = Resolve(st, "ALIGNMENTINSERT", f[0], log);
  *target = std::move(trial);
  log.push_back("ALIGNMENTINSERT - \"" + target->name + "\" now has " + std::to_string(target->Entities().size()) + " entities, length " +
                Lin(st, target->Length()) + ".");
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTREMOVE <name>, <entity#>
void RunAlignmentRemove(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("ALIGNMENTREMOVE - usage: ALIGNMENTREMOVE <name>, <entity#>.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTREMOVE", f[0], log);
  if (a == nullptr || IsLinkedOffset(*a, "ALIGNMENTREMOVE", log)) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTREMOVE", f[1], &idx, log)) return;
  alignment::Alignment trial = *a;
  std::string why;
  if (!trial.DeleteEntity(idx, &why)) {
    log.push_back("ALIGNMENTREMOVE - refused: " + why + ". Nothing changed.");
    return;
  }
  PushUndoSnapshot(st, "Remove alignment entity");
  ReportDroppedEquations(st, *Resolve(st, "ALIGNMENTREMOVE", f[0], log), trial, log);
  alignment::Alignment* target = Resolve(st, "ALIGNMENTREMOVE", f[0], log);
  *target = std::move(trial);
  log.push_back("ALIGNMENTREMOVE - entity " + std::to_string(idx + 1) + " removed; \"" + target->name + "\" has " +
                std::to_string(target->Entities().size()) + " entities.");
  ReportContinuity(st, *target, log);
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTCONTINUITY <name>, C0|C1|C2 | <name>, LIST   - REQ-482
void RunAlignmentContinuity(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 2) {
    log.push_back("ALIGNMENTCONTINUITY - usage: ALIGNMENTCONTINUITY <name>, C0|C1|C2 | <name>, LIST.");
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTCONTINUITY", f[0], log);
  if (a == nullptr) return;
  const std::string sub = Lower(f[1]);
  if (sub == "list") {
    log.push_back("ALIGNMENTCONTINUITY - \"" + a->name + "\" requires " +
                  std::string(alignment::ContinuityRequirementLabel(a->continuityRequirement)) + " at junctions.");
    int warnings = 0;
    for (const alignment::ContinuityIssue& c : a->CheckContinuity()) {
      if (c.FailsRequirement(a->continuityRequirement)) ++warnings;
    }
    ReportContinuity(st, *a, log);
    if (warnings == 0) log.push_back("  No continuity warnings at this level.");
    return;
  }
  alignment::ContinuityRequirement level = alignment::ContinuityRequirement::C1;
  if (!alignment::ParseContinuityRequirement(sub, &level)) {
    log.push_back("ALIGNMENTCONTINUITY - the level must be C0, C1 or C2.");
    return;
  }
  PushUndoSnapshot(st, "Set alignment continuity requirement");
  a = Resolve(st, "ALIGNMENTCONTINUITY", f[0], log);
  a->continuityRequirement = level;
  log.push_back("ALIGNMENTCONTINUITY - \"" + a->name + "\" now checks " +
                std::string(alignment::ContinuityRequirementLabel(level)) + " at junctions.");
  ReportContinuity(st, *a, log);
}

// REVERSEALIGNMENT <name>  (also REVERSEALIGNDIRECTION) - REQ-487.2
void RunReverseAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  alignment::Alignment* a = Resolve(st, "REVERSEALIGNMENT", name, log);
  if (a == nullptr || IsLinkedOffset(*a, "REVERSEALIGNMENT", log)) return;
  alignment::Alignment trial = *a;
  std::string note;
  if (!trial.Reverse(&note)) {
    log.push_back("REVERSEALIGNMENT - refused: the alignment is empty or has a gap (it must be continuous). Nothing changed.");
    return;
  }
  PushUndoSnapshot(st, "Reverse alignment");
  alignment::Alignment* target = Resolve(st, "REVERSEALIGNMENT", name, log);
  *target = std::move(trial);
  const std::uint64_t parentId = target->id;
  log.push_back("REVERSEALIGNMENT - \"" + target->name + "\" now runs from " + Sta(st, *target, target->StartStation()) + " (was the end) to " +
                Sta(st, *target, target->EndStation()) + "." + (note.empty() ? "" : " " + note + "."));
  // REQ-487.2: a linked offset keeps its side on the ground; facing the other way, right becomes left.
  int followed = 0;
  for (alignment::Alignment& child : st.drawing.alignments) {
    if (child.parentId != parentId || parentId == 0) continue;
    child.offsetDistance = -child.offsetDistance;
    ++followed;
  }
  if (followed > 0) log.push_back("REVERSEALIGNMENT - " + std::to_string(followed) + " offset alignment(s) reversed with it.");
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTSTATION <name>, START, <station>[, CLEAR]
//                  <name>, EQ, ADD, <raw station>, <ahead station>, INC|DEC
//                  <name>, EQ, REMOVE, <n>   |   <name>, EQ, LIST        - REQ-483, REQ-487.3
void RunAlignmentStation(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTSTATION - usage: ALIGNMENTSTATION <name>, START, <station>[, CLEAR] | <name>, EQ, ADD, <raw station>, <ahead station>, INC|DEC | "
      "<name>, EQ, EDIT, <n>, <raw station>, <ahead station>, INC|DEC | <name>, EQ, REMOVE, <n> | <name>, EQ, LIST | "
      "<name>, REFERENCE, <x>, <y>, <station>[, CLEAR].";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
  if (a == nullptr) return;
  const std::string sub = Lower(f[1]);
  if (sub == "start") {
    double sta = 0.0;
    if (!ParseSta(st, f[2], &sta)) {
      log.push_back("ALIGNMENTSTATION - \"" + f[2] + "\" is not a station.");
      return;
    }
    const bool clear = f.size() == 4 && Lower(f[3]) == "clear";
    if ((!a->equations.empty() || !a->designSpeeds.empty()) && !clear) {  // REQ-483.1: equations and design speeds
      log.push_back("ALIGNMENTSTATION - changing the starting station removes this alignment's " + std::to_string(a->equations.size()) +
                    " station equation(s) and " + std::to_string(a->designSpeeds.size()) +
                    " design speed(s); repeat with , CLEAR to confirm.");
      return;
    }
    PushUndoSnapshot(st, "Set starting station");
    a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    a->equations.clear();
    a->designSpeeds.clear();
    a->startingStation = sta;
    a->hasReference = false;  // the start is the reference point again
    a->referenceDistance = 0.0;
    log.push_back("ALIGNMENTSTATION - \"" + a->name + "\" starts at " + StaFmt(st, sta) + " and ends at " + Sta(st, *a, a->EndStation()) + ".");
    return;
  }
  if (sub == "reference" && (f.size() == 5 || f.size() == 6)) {  // REQ-483.1: <x>, <y>, <station>[, CLEAR]
    double wx = 0.0, wy = 0.0, sta = 0.0;
    if (!ParseNum(f[2], &wx) || !ParseNum(f[3], &wy) || !ParseSta(st, f[4], &sta)) {
      log.push_back(usage);
      return;
    }
    if (a->Empty()) {
      log.push_back("ALIGNMENTSTATION - \"" + a->name + "\" has no geometry to put a reference point on.");
      return;
    }
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, wx, wy, &lx, &ly);
    const alignment::StationOffset so = a->GetStationOffset(lx, ly);
    if (so.outOfRange) {
      log.push_back("ALIGNMENTSTATION - the reference point must lie along \"" + a->name + "\", not beyond its ends.");
      return;
    }
    const bool clear = f.size() == 6 && Lower(f[5]) == "clear";
    if ((!a->equations.empty() || !a->designSpeeds.empty()) && !clear) {
      log.push_back("ALIGNMENTSTATION - changing the reference point removes this alignment's " +
                    std::to_string(a->equations.size()) + " station equation(s) and " +
                    std::to_string(a->designSpeeds.size()) + " design speed(s); repeat with , CLEAR to confirm.");
      return;
    }
    const double along = so.internalStation - a->startingStation;
    PushUndoSnapshot(st, "Set reference point");
    a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    a->equations.clear();
    a->designSpeeds.clear();
    a->hasReference = true;
    a->referenceDistance = along;
    a->startingStation = sta - along;
    log.push_back("ALIGNMENTSTATION - the point " + Lin(st, along) + " along \"" + a->name + "\" is station " +
                  StaFmt(st, sta) + "; it now starts at " + StaFmt(st, a->startingStation) + ".");
    return;
  }
  if (sub != "eq" && sub != "equation") {
    log.push_back(usage);
    return;
  }
  const std::string verb = Lower(f[2]);
  if (verb == "list") {
    if (a->equations.empty()) log.push_back("ALIGNMENTSTATION - \"" + a->name + "\" has no station equations.");
    for (size_t i = 0; i < a->equations.size(); ++i) {
      const alignment::StationEquation& q = a->equations[i];
      log.push_back("  EQUATION " + std::to_string(i + 1) + "  back " + StaFmt(st, a->DisplayedStation(q.rawBack, false)) + "  ahead " +
                    StaFmt(st, q.ahead) + "  " + (q.increasing ? "increasing" : "decreasing") + "  (raw station " +
                    StaFmt(st, q.rawBack) + ")");
    }
    return;
  }
  if (verb == "add" && f.size() == 6) {
    alignment::StationEquation q;
    const std::string dir = Lower(f[5]);
    if (!ParseSta(st, f[3], &q.rawBack) || !ParseSta(st, f[4], &q.ahead) || (dir != "inc" && dir != "dec")) {
      log.push_back(usage);
      return;
    }
    q.increasing = dir == "inc";
    alignment::Alignment trial = *a;
    std::string why;
    if (!trial.AddStationEquation(q, &why)) {
      log.push_back("ALIGNMENTSTATION - refused: " + why + ".");
      return;
    }
    PushUndoSnapshot(st, "Add station equation");
    alignment::Alignment* target = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    *target = std::move(trial);
    log.push_back("ALIGNMENTSTATION - equation added; \"" + target->name + "\" now ends at station " +
                  StaFmt(st, target->EndStationWithEquations()) + ".");
    return;
  }
  if (verb == "edit" && f.size() == 7) {  // REQ-487.3: replace equation n, refused as a whole if the new one does not fit
    double n = 0.0;
    alignment::StationEquation q;
    const std::string dir = Lower(f[6]);
    if (!ParseNum(f[3], &n) || n < 1.0 || n > static_cast<double>(a->equations.size()) || n != std::floor(n)) {
      log.push_back("ALIGNMENTSTATION - \"" + f[3] + "\" is not an equation number (1 to " + std::to_string(a->equations.size()) + ").");
      return;
    }
    if (!ParseSta(st, f[4], &q.rawBack) || !ParseSta(st, f[5], &q.ahead) || (dir != "inc" && dir != "dec")) {
      log.push_back(usage);
      return;
    }
    q.increasing = dir == "inc";
    alignment::Alignment trial = *a;
    trial.equations.erase(trial.equations.begin() + static_cast<std::ptrdiff_t>(n) - 1);
    std::string why;
    if (!trial.AddStationEquation(q, &why)) {
      log.push_back("ALIGNMENTSTATION - refused: " + why + "; equation " + f[3] + " is unchanged.");
      return;
    }
    PushUndoSnapshot(st, "Edit station equation");
    alignment::Alignment* target = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    *target = std::move(trial);
    log.push_back("ALIGNMENTSTATION - equation " + f[3] + " changed; \"" + target->name + "\" now ends at station " +
                  StaFmt(st, target->EndStationWithEquations()) + ".");
    return;
  }
  if (verb == "remove" && f.size() == 4) {
    double n = 0.0;
    if (!ParseNum(f[3], &n) || n < 1.0 || n > static_cast<double>(a->equations.size()) || n != std::floor(n)) {
      log.push_back("ALIGNMENTSTATION - \"" + f[3] + "\" is not an equation number (1 to " + std::to_string(a->equations.size()) + ").");
      return;
    }
    PushUndoSnapshot(st, "Remove station equation");
    a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    a->equations.erase(a->equations.begin() + static_cast<std::ptrdiff_t>(n) - 1);
    log.push_back("ALIGNMENTSTATION - equation " + std::to_string(static_cast<int>(n)) + " removed.");
    return;
  }
  log.push_back(usage);
}

namespace {

struct OffsetRequest {
  std::vector<double> distances;  ///< Signed, right positive.
  std::vector<int> increment;     ///< 1-based number of each distance on its side (the template's {n}).
  std::string nameTemplate;       ///< Empty: "<parent> - L12".
  std::string from, to;           ///< Station range on the parent; empty = its whole length.
};

/// "3x12" (three offsets 12 apart) or "12" (one) for one side; false for anything else.
bool ParseOffsetSide(const std::string& text, double sign, OffsetRequest* r) {
  assert(r != nullptr);
  assert(sign == 1.0 || sign == -1.0);
  const size_t x = Lower(text).find('x');
  double count = 1.0, step = 0.0;
  if (x == std::string::npos ? !ParseNum(text, &step) : !ParseNum(text.substr(0, x), &count) || !ParseNum(text.substr(x + 1), &step))
    return false;
  constexpr double kMaxPerSide = 100.0;
  if (!(step > 0.0) || count < 1.0 || count > kMaxPerSide || count != std::floor(count)) return false;
  for (int n = 1; n <= static_cast<int>(count); ++n) {
    r->distances.push_back(sign * step * n);
    r->increment.push_back(n);
  }
  return true;
}

/// Reads the distance list (12 / R12 right, -12 / L12 left) and the LEFT= / RIGHT= / NAME= / FROM= / TO= fields.
bool ParseOffsetRequest(const std::vector<std::string>& f, OffsetRequest* r, std::string* bad) {
  assert(r != nullptr && bad != nullptr);
  assert(!f.empty());
  for (size_t i = 1; i < f.size(); ++i) {
    const size_t eq = f[i].find('=');
    const std::string key = eq == std::string::npos ? std::string() : Lower(Trim(f[i].substr(0, eq)));
    const std::string value = eq == std::string::npos ? Trim(f[i]) : Trim(f[i].substr(eq + 1));
    *bad = f[i];
    if (key == "left" || key == "right") {
      if (!ParseOffsetSide(value, key == "left" ? -1.0 : 1.0, r)) return false;
    } else if (key == "name") {
      r->nameTemplate = value;
    } else if (key == "from" || key == "to") {
      (key == "from" ? r->from : r->to) = value;
    } else if (key.empty()) {
      std::string t = value;
      double sign = 1.0, d = 0.0;
      if (!t.empty() && (t[0] == 'L' || t[0] == 'l')) { sign = -1.0; t.erase(0, 1); }
      else if (!t.empty() && (t[0] == 'R' || t[0] == 'r')) { t.erase(0, 1); }
      if (!ParseNum(t, &d) || d == 0.0) return false;
      r->distances.push_back(sign * d);
      r->increment.push_back(1 + static_cast<int>(std::count_if(r->distances.begin(), r->distances.end() - 1,
                                                                [&](double o) { return (o < 0.0) == (sign * d < 0.0); })));
    } else {
      return false;
    }
  }
  bad->clear();
  return !r->distances.empty();
}

/// The template with {parent}, {side} (Left / Right), {offset} and {n} filled in; or "<parent> - L12".
std::string OffsetName(const std::string& tmpl, const std::string& parent, double d, int n) {
  assert(std::isfinite(d) && d != 0.0);
  assert(n >= 1);
  std::array<char, 48> num{};
  std::snprintf(num.data(), num.size(), "%g", std::fabs(d));
  if (tmpl.empty()) return parent + " - " + (d < 0.0 ? "L" : "R") + num.data();
  std::string out = tmpl;
  const std::array<std::pair<const char*, std::string>, 4> tokens = {{{"{parent}", parent},
                                                                      {"{side}", d < 0.0 ? "Left" : "Right"},
                                                                      {"{offset}", num.data()},
                                                                      {"{n}", std::to_string(n)}}};
  for (const auto& [token, value] : tokens) {
    for (size_t at = out.find(token), guard = 0; at != std::string::npos && guard < 64; at = out.find(token), ++guard)
      out.replace(at, std::strlen(token), value);
  }
  return out;
}

}  // namespace

// OFFSETALIGNMENT <parent>, <distance>[, <distance> ...]    distance: 12 / R12 = right, -12 / L12 = left
//   [, LEFT=<count>x<increment>][, RIGHT=<count>x<increment>][, NAME=<template>][, FROM=<station>][, TO=<station>]
//   REQ-488.1: template tokens {parent} {side} {offset} {n}; the range limits the offsets to part of the parent.
void RunOffsetAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage = "OFFSETALIGNMENT - usage: OFFSETALIGNMENT <parent>, <distance>[, <distance>...] (R12 or 12 = right, "
                      "L12 or -12 = left) [, LEFT=<count>x<increment>][, RIGHT=<count>x<increment>][, NAME=<template>]"
                      "[, FROM=<station>][, TO=<station>].";
  if (f.size() < 2) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* parent = Resolve(st, "OFFSETALIGNMENT", f[0], log);
  if (parent == nullptr) return;
  const std::uint64_t parentId = parent->id;
  const alignment::Alignment parentCopy = *parent;
  OffsetRequest req;
  std::string bad;
  if (!ParseOffsetRequest(f, &req, &bad)) {
    log.push_back(bad.empty() ? std::string(usage) : "OFFSETALIGNMENT - \"" + bad + "\" is not a non-zero offset distance or field.");
    return;
  }
  alignment::Alignment source = parentCopy;  // the parent, or the part of it in the station range
  double from = 0.0, to = 0.0;
  if (!req.from.empty() || !req.to.empty()) {
    double shownFrom = 0.0, shownTo = 0.0;
    const bool fromOk = req.from.empty() || ParseSta(st, req.from, &shownFrom);
    const bool toOk = req.to.empty() || ParseSta(st, req.to, &shownTo);
    const std::vector<double> rawFrom = req.from.empty() ? std::vector<double>{parentCopy.StartStation()}
                                                         : parentCopy.RawStationsForDisplayed(shownFrom);
    const std::vector<double> rawTo = req.to.empty() ? std::vector<double>{parentCopy.EndStation()}
                                                     : parentCopy.RawStationsForDisplayed(shownTo);
    std::string why = "a station is outside \"" + parentCopy.name + "\"";
    if (!fromOk || !toOk || rawFrom.empty() || rawTo.empty() ||
        !parentCopy.ExtractRange(rawFrom.front(), rawTo.back(), &source, &why)) {
      log.push_back("OFFSETALIGNMENT - the station range is not usable: " + why + ".");
      return;
    }
    source.id = parentCopy.id;
    from = rawFrom.front();
    to = rawTo.back();
  }
  // Build everything first so one bad distance creates nothing.
  std::vector<alignment::Alignment> made;
  for (size_t k = 0; k < req.distances.size(); ++k) {
    const double d = req.distances[k];
    alignment::Alignment off;
    off.name = OffsetName(req.nameTemplate, parentCopy.name, d, req.increment[k]);
    const bool duplicate = std::any_of(made.begin(), made.end(), [&](const alignment::Alignment& m) {
      return Lower(m.name) == Lower(off.name);
    });
    if (off.name.empty() || duplicate || FindAlignment(st, off.name) >= 0) {
      log.push_back("OFFSETALIGNMENT - an alignment named \"" + off.name + "\" already exists; nothing was created.");
      return;
    }
    off.layer = parentCopy.layer;
    off.style = parentCopy.style;
    off.labelSet = parentCopy.labelSet;
    std::string why;
    if (!alignment::MakeOffset(source, d, &off, &why)) {
      log.push_back("OFFSETALIGNMENT - refused for " + off.name + ": " + why + ". Nothing was created.");
      return;
    }
    off.startingStation = source.startingStation;
    off.offsetFromStation = from;
    off.offsetToStation = to;
    made.push_back(std::move(off));
  }
  PushUndoSnapshot(st, "Offset alignment");
  for (alignment::Alignment& off : made) {
    off.id = st.nextEntityId++;
    off.parentId = parentId;
    log.push_back("OFFSETALIGNMENT - created \"" + off.name + "\" (" + Lin(st, std::fabs(off.offsetDistance)) +
                  (off.offsetDistance < 0.0 ? " left" : " right") + "), length " + Lin(st, off.Length()) + ".");
    st.drawing.alignments.push_back(std::move(off));
  }
}

// ALIGNMENTDETACH <name> - REQ-488.3
void RunAlignmentDetach(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  alignment::Alignment* a = Resolve(st, "ALIGNMENTDETACH", name, log);
  if (a == nullptr) return;
  if (a->parentId == 0) {
    log.push_back("ALIGNMENTDETACH - \"" + a->name + "\" is not linked to a parent.");
    return;
  }
  PushUndoSnapshot(st, "Detach alignment");
  a = Resolve(st, "ALIGNMENTDETACH", name, log);
  a->parentId = 0;
  log.push_back("ALIGNMENTDETACH - \"" + a->name + "\" is now independent and can be edited.");
}

// ALIGNMENTINTERSECT <a>, <b> | <a>, LINE, x1, y1, x2, y2 | <a>, CIRCLE, cx, cy, r | <a>, ARC, cx, cy, r, start, end
//   [, POINT]   (WORLD coordinates; arc angles in degrees counter-clockwise from east) - REQ-489
void RunAlignmentIntersect(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTINTERSECT - usage: ALIGNMENTINTERSECT <alignment>, <alignment> | <alignment>, LINE, <x1>, <y1>, <x2>, <y2> | "
      "<alignment>, CIRCLE, <cx>, <cy>, <r> | <alignment>, ARC, <cx>, <cy>, <r>, <start angle>, <end angle> [, POINT].";
  const bool placePoints = !f.empty() && Lower(f.back()) == "point";  // REQ-489: place a point at each intersection
  if (placePoints) f.pop_back();
  if (f.size() < 2) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTINTERSECT", f[0], log);
  if (a == nullptr) return;
  alignment::Alignment other;
  std::string otherName;
  const std::string kind = Lower(f[1]);
  double v[5] = {};
  auto num = [&](size_t from, size_t n) {
    if (f.size() != from + n) return false;
    for (size_t i = 0; i < n; ++i) {
      if (!ParseNum(f[from + i], &v[i])) return false;
    }
    return true;
  };
  if (kind == "line" && num(2, 4)) {
    double ax = 0, ay = 0, bx = 0, by = 0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &ax, &ay);
    CadCoord::LocalFromWorld(st, v[2], v[3], &bx, &by);
    other = alignment::MakeLineObject({ax, ay}, {bx, by});
    otherName = "the line";
  } else if (kind == "circle" && num(2, 3) && v[2] > 0.0) {
    double cx = 0, cy = 0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &cx, &cy);
    other = alignment::MakeCircleObject({cx, cy}, v[2], 0.0, alignment::kTwoPi - 1e-9);
    otherName = "the circle";
  } else if (kind == "arc" && num(2, 5) && v[2] > 0.0) {
    double cx = 0, cy = 0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &cx, &cy);
    double sweep = std::fmod(v[4] - v[3], 360.0);
    if (sweep <= 0.0) sweep += 360.0;
    other = alignment::MakeCircleObject({cx, cy}, v[2], v[3] * kDegToRad, std::min(sweep * kDegToRad, alignment::kTwoPi - 1e-9));
    otherName = "the arc";
  } else if (f.size() == 2) {
    alignment::Alignment* b = Resolve(st, "ALIGNMENTINTERSECT", f[1], log);
    if (b == nullptr) return;
    other = *b;
    otherName = "\"" + b->name + "\"";
  } else {
    log.push_back(usage);
    return;
  }
  if (a->Empty() || other.Empty()) {
    log.push_back("ALIGNMENTINTERSECT - both objects need geometry.");
    return;
  }
  std::vector<alignment::IntersectionOverlap> overlaps;
  const std::vector<alignment::Intersection> hits = alignment::IntersectAlignments(*a, other, &overlaps);
  log.push_back("ALIGNMENTINTERSECT - \"" + a->name + "\" and " + otherName + ": " + std::to_string(hits.size()) + " intersection(s).");
  const bool otherIsAlignment = f.size() == 2;
  for (size_t i = 0; i < hits.size(); ++i) {
    const alignment::Intersection& h = hits[i];
    // The point lies on both, so its offset from each is zero; the station is given on each alignment (REQ-489).
    log.push_back("  " + std::to_string(i + 1) + ": X = " + Lin(st, h.point.x + CadCoord::WorldOriginX(st)) + "  Y = " +
                  Lin(st, h.point.y + CadCoord::WorldOriginY(st)) + "  Station = " + Sta(st, *a, h.stationA) + "  Offset = " +
                  Lin(st, 0.0) + (otherIsAlignment ? "  Station on " + otherName + " = " + Sta(st, other, h.stationB) : "") +
                  "  Angle = " + FormatSweptAngle(h.angle / kDegToRad, CadAngleDisplaySettings(st)) +
                  (h.tangential ? "  (tangential)" : ""));
  }
  for (const alignment::IntersectionOverlap& o : overlaps)
    log.push_back("  Coincident from station " + Sta(st, *a, o.fromStationA) + " to " + Sta(st, *a, o.toStationA) + ".");
  if (placePoints && !hits.empty()) {
    PushUndoSnapshot(st, "Alignment intersection points");
    for (const alignment::Intersection& h : hits) {
      CadPoint p{};
      p.x = h.point.x;
      p.y = h.point.y;
      st.drawing.cadPoints.push_back(p);
      st.drawing.cadPointAttrs.push_back(MakeNewEntityAttrs(st));
    }
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTINTERSECT - " + std::to_string(hits.size()) + " point(s) placed.");
  }
}

// EDITALIGNMENT [<name>] - REQ-486.1. Opens the Alignment Layout table in the Qt editor; headless it lists the rows.
void RunEditAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::string name = Trim(args);
  if (name.empty()) {
    for (const SelectedEntity& e : st.selection) {
      if (e.type == SelectedEntity::Type::Alignment && e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.alignments.size()) {
        name = st.drawing.alignments[static_cast<size_t>(e.index)].name;
        break;
      }
    }
  }
  if (name.empty() && !st.drawing.alignments.empty()) name = st.drawing.alignments.front().name;
  if (name.empty()) {
    log.push_back("EDITALIGNMENT - this drawing has no alignments.");
    return;
  }
  if (FindAlignment(st, name) < 0) {
    log.push_back("EDITALIGNMENT - no alignment named \"" + name + "\".");
    return;
  }
  st.alignmentLayoutRequested = true;
  st.alignmentLayoutName = name;
  // As in Civil 3D, the alignment being edited is the selection, so Properties and the Alignment ribbon tab show it.
  st.selection = {{SelectedEntity::Type::Alignment, FindAlignment(st, name)}};
  RunAlignmentList(st, name, log);  // the same rows, for a window-less run
}
