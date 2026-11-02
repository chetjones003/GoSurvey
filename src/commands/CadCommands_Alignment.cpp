// CadCommands_Alignment.cpp - the alignment commands (REQ-480, 484, 485, 501; issue #121 Phase 1).
//
// Same TU conventions as CadCommands_Geo.cpp: entry points are declared in CadCommands.hpp and the
// geometry itself lives in alignment/Alignment.cpp (pure, no session state). This file only parses
// typed arguments, converts WORLD input to the drawing-local doubles alignments are stored in
// (world = local + worldDocumentOrigin, always computed in double - REQ-499), and reports.
//
// Phase 1 builds geometry from typed arguments and from selected objects. The interactive layout
// toolbar and the Alignment Layout table are REQ-486 (Phase 2); nothing here pretends to be them.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include "util/NumFormat.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr double kDegToRad = alignment::kPi / 180.0;

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::vector<std::string> SplitFields(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t comma = s.find(',', start);
    out.push_back(Trim(s.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

bool ParseNum(const std::string& s, double* v) {
  if (s.empty()) return false;
  char* end = nullptr;
  const double d = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0' || !std::isfinite(d)) return false;
  *v = d;
  return true;
}

int FindAlignment(const AppCommandState& st, const std::string& name) {
  const std::string want = Lower(name);
  for (size_t i = 0; i < st.alignments.size(); ++i) {
    if (Lower(st.alignments[i].name) == want) return static_cast<int>(i);
  }
  return -1;
}

const char* TypeName(alignment::AlignmentType t) {
  switch (t) {
    case alignment::AlignmentType::Centerline: return "Centerline";
    case alignment::AlignmentType::Offset: return "Offset";
    case alignment::AlignmentType::CurbReturn: return "Curb Return";
    case alignment::AlignmentType::Miscellaneous: return "Miscellaneous";
    case alignment::AlignmentType::Rail: return "Rail";
  }
  return "?";
}

std::string Sta(const alignment::Alignment& a, double internalStation) {
  return alignment::FormatStation(a.DisplayedStation(internalStation, true));
}

std::string Lin(const AppCommandState& st, double v) { return FormatLinear(v, st.displayLinearPrecision); }

std::string Brg(const AppCommandState& st, double azimuthRad) {
  return FormatBearing(azimuthRad / kDegToRad, CadAngleDisplaySettings(st));
}

/// Resolves the alignment named in \p field, or reports and returns nullptr.
alignment::Alignment* Resolve(AppCommandState& st, const std::string& cmd, const std::string& field,
                              std::vector<std::string>& log) {
  if (field.empty()) {
    log.push_back(cmd + " - an alignment name is required.");
    return nullptr;
  }
  const int ix = FindAlignment(st, field);
  if (ix < 0) {
    log.push_back(cmd + " - no alignment named \"" + field + "\".");
    return nullptr;
  }
  return &st.alignments[static_cast<size_t>(ix)];
}

}  // namespace

// ALIGNMENT <name>[, <description>[, <starting station>[, <layer>[, <style>]]]]  (REQ-480, REQ-485.1)
void RunAlignmentCreate(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.empty() || f[0].empty() || f.size() > 5) {
    log.push_back("ALIGNMENT - usage: ALIGNMENT <name>[, <description>[, <starting station>[, <layer>[, <style>]]]].");
    return;
  }
  if (FindAlignment(st, f[0]) >= 0) {
    log.push_back("ALIGNMENT - an alignment named \"" + f[0] + "\" already exists; names must be unique.");
    return;
  }
  alignment::Alignment a;
  a.name = f[0];
  if (f.size() > 1) a.description = f[1];
  if (f.size() > 2 && !f[2].empty()) {
    double sta = 0.0;
    if (!alignment::ParseStation(f[2], &sta)) {
      log.push_back("ALIGNMENT - \"" + f[2] + "\" is not a station (try 5+00 or 500).");
      return;
    }
    a.startingStation = sta;
  }
  a.layer = (f.size() > 3 && !f[3].empty()) ? f[3] : MakeNewEntityAttrs(st).layer;
  if (a.layer.empty()) a.layer = "0";
  if (f.size() > 4 && !f[4].empty()) a.style = f[4];
  PushUndoSnapshot(st, "Create alignment");
  a.id = st.nextEntityId++;
  st.alignments.push_back(std::move(a));
  const alignment::Alignment& made = st.alignments.back();
  log.push_back("ALIGNMENT - created \"" + made.name + "\" (" + TypeName(made.type) + ", starting station " +
                alignment::FormatStation(made.startingStation) + "). Add geometry with ALIGNMENTADD.");
}

// ALIGNMENTADD <name>, TANGENT, <x>, <y>, <azimuth deg>, <length>     first tangent, WORLD x/y
//              <name>, TANGENT, <length>                              continues tangent-continuous
//              <name>, TANGENTTO, <x>, <y>                            tangent to a WORLD point
//              <name>, CURVE, <radius>, <delta deg>, LEFT|RIGHT       circular curve by central angle
//              <name>, CURVELEN, <radius>, <arc length>, LEFT|RIGHT   circular curve by arc length
void RunAlignmentAdd(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTADD - usage: ALIGNMENTADD <name>, TANGENT, <x>, <y>, <azimuth deg>, <length> | <name>, TANGENT, "
      "<length> | <name>, TANGENTTO, <x>, <y> | <name>, CURVE, <radius>, <delta deg>, LEFT|RIGHT | <name>, CURVELEN, "
      "<radius>, <arc length>, LEFT|RIGHT.";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTADD", f[0], log);
  if (a == nullptr) return;
  const std::string kind = Lower(f[1]);
  alignment::Alignment edited = *a;
  bool ok = false;
  std::string what;
  double v[4] = {};
  auto nums = [&](size_t from, size_t count) {
    if (f.size() < from + count) return false;
    for (size_t i = 0; i < count; ++i) {
      if (!ParseNum(f[from + i], &v[i])) return false;
    }
    return true;
  };
  if (kind == "tangent" && f.size() == 6) {
    if (!edited.Empty()) {
      log.push_back("ALIGNMENTADD - \"" + a->name + "\" already has geometry; use TANGENT, <length> to continue it.");
      return;
    }
    if (!nums(2, 4)) {
      log.push_back(usage);
      return;
    }
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &lx, &ly);
    ok = edited.AppendTangent({lx, ly}, v[2] * kDegToRad, v[3]);
    what = "tangent";
  } else if (kind == "tangent" && f.size() == 3) {
    if (!nums(2, 1)) {
      log.push_back(usage);
      return;
    }
    if (edited.Empty()) {
      log.push_back("ALIGNMENTADD - the first tangent needs a start point and azimuth: <name>, TANGENT, <x>, <y>, <azimuth deg>, <length>.");
      return;
    }
    ok = edited.AppendTangent(v[0]);
    what = "tangent";
  } else if (kind == "tangentto" && f.size() == 4) {
    if (!nums(2, 2)) {
      log.push_back(usage);
      return;
    }
    if (edited.Empty()) {
      log.push_back("ALIGNMENTADD - the first tangent needs a start point and azimuth, not TANGENTTO.");
      return;
    }
    double lx = 0.0, ly = 0.0;
    CadCoord::LocalFromWorld(st, v[0], v[1], &lx, &ly);
    ok = edited.AppendTangentTo({lx, ly});
    what = "tangent";
  } else if ((kind == "curve" || kind == "curvelen") && f.size() == 5) {
    if (!nums(2, 2)) {
      log.push_back(usage);
      return;
    }
    const std::string side = Lower(f[4]);
    if (side != "left" && side != "right") {
      log.push_back("ALIGNMENTADD - the curve direction must be LEFT or RIGHT.");
      return;
    }
    if (edited.Empty()) {
      log.push_back("ALIGNMENTADD - a curve needs a tangent before it; add the first tangent first.");
      return;
    }
    ok = kind == "curve" ? edited.AppendCurveByDelta(v[0], v[1] * kDegToRad, side == "right")
                         : edited.AppendCurve(v[0], v[1], side == "right");
    what = "curve";
  } else {
    log.push_back(usage);
    return;
  }
  if (!ok) {
    log.push_back("ALIGNMENTADD - the " + what +
                  " was refused: lengths and radii must be positive, a curve must sweep less than 360 degrees, "
                  "and a tangent must have a non-zero length.");
    return;
  }
  PushUndoSnapshot(st, "Add alignment geometry");
  // PushUndoSnapshot copied the pre-edit state; re-resolve in case it reallocated nothing but be exact.
  const int ix = FindAlignment(st, f[0]);
  st.alignments[static_cast<size_t>(ix)] = std::move(edited);
  const alignment::Alignment& r = st.alignments[static_cast<size_t>(ix)];
  log.push_back("ALIGNMENTADD - \"" + r.name + "\" now has " + std::to_string(r.Entities().size()) +
                " entities, length " + Lin(st, r.Length()) + ", ending at station " + Sta(r, r.EndStation()) + ".");
}

// ALIGNMENTLIST [<name>]  (REQ-501.1 properties, REQ-494 entity list)
void RunAlignmentList(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  if (st.alignments.empty()) {
    log.push_back("ALIGNMENTLIST - this drawing has no alignments.");
    return;
  }
  for (const alignment::Alignment& a : st.alignments) {
    if (!name.empty() && Lower(a.name) != Lower(name)) continue;
    log.push_back("ALIGNMENT: " + a.name + "  [" + TypeName(a.type) + ", layer " + a.layer + ", style " + a.style + "]");
    if (!a.description.empty()) log.push_back("  Description:   " + a.description);
    if (a.Empty()) {
      log.push_back("  (no geometry yet)");
      continue;
    }
    double wsx = 0.0, wsy = 0.0, wex = 0.0, wey = 0.0;
    wsx = a.StartPoint().x + CadCoord::WorldOriginX(st);
    wsy = a.StartPoint().y + CadCoord::WorldOriginY(st);
    wex = a.EndPoint().x + CadCoord::WorldOriginX(st);
    wey = a.EndPoint().y + CadCoord::WorldOriginY(st);
    log.push_back("  Start station: " + Sta(a, a.StartStation()) + "   End station: " +
                  alignment::FormatStation(a.EndStationWithEquations()) + "   Length: " + Lin(st, a.Length()));
    log.push_back("  Start: " + Lin(st, wsx) + ", " + Lin(st, wsy) + "  bearing " + Brg(st, a.StartAzimuth()));
    log.push_back("  End:   " + Lin(st, wex) + ", " + Lin(st, wey) + "  bearing " + Brg(st, a.EndAzimuth()));
    log.push_back("  Entities: " + std::to_string(a.Entities().size()) + "   Curves: " + std::to_string(a.CurveCount()) +
                  "   Spirals: 0   Station equations: " + std::to_string(a.equations.size()));
    for (size_t i = 0; i < a.Entities().size(); ++i) {
      const alignment::Entity& e = a.Entities()[i];
      const int ii = static_cast<int>(i);
      std::string row = "  ENTITY " + std::to_string(i + 1) + "  " + Sta(a, a.EntityStartStation(ii)) + " - " +
                        Sta(a, a.EntityEndStation(ii)) + "  ";
      if (e.kind == alignment::EntityKind::Line) {
        row += "Tangent  bearing " + Brg(st, e.startAzimuth) + "  length " + Lin(st, e.length);
      } else {
        alignment::CurveData cd;
        (void)a.CurveDataFor(ii, &cd);
        row += std::string("Curve ") + (e.right ? "right" : "left") + "  R " + Lin(st, cd.radius) + "  delta " +
               FormatSweptAngle(cd.delta / kDegToRad, CadAngleDisplaySettings(st)) + "  L " + Lin(st, cd.arcLength) +
               "  T " + Lin(st, cd.tangentLength) + "  C " + Lin(st, cd.chord);
      }
      log.push_back(row);
    }
    for (const alignment::ContinuityIssue& c : a.CheckContinuity()) {
      if (!c.c0) log.push_back("  WARNING: gap of " + Lin(st, c.gap) + " after entity " + std::to_string(c.afterEntity + 1) + " (C0).");
      else if (!c.c1) log.push_back("  WARNING: direction changes after entity " + std::to_string(c.afterEntity + 1) + " (C1).");
    }
  }
}

// ALIGNMENTDELETE <name>
void RunAlignmentDelete(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::string name = Trim(args);
  const int ix = FindAlignment(st, name);
  if (ix < 0) {
    log.push_back("ALIGNMENTDELETE - no alignment named \"" + name + "\".");
    return;
  }
  PushUndoSnapshot(st, "Delete alignment");
  st.alignments.erase(st.alignments.begin() + ix);
  log.push_back("ALIGNMENTDELETE - removed \"" + name + "\".");
}

// GETSTATIONOFFSET <alignment>, <x>, <y>  (WORLD x/y) - REQ-484.4
void RunGetStationOffset(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  double x = 0.0, y = 0.0;
  if (f.size() != 3 || !ParseNum(f[1], &x) || !ParseNum(f[2], &y)) {
    log.push_back("GETSTATIONOFFSET - usage: GETSTATIONOFFSET <alignment>, <x>, <y>.");
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
  std::string line = "GETSTATIONOFFSET - \"" + a->name + "\": Station = " + Sta(*a, so.internalStation) +
                     "  Offset = " + Lin(st, so.offset) + (so.offset < 0.0 ? " (left)" : so.offset > 0.0 ? " (right)" : "") +
                     "  Bearing = " + Brg(st, so.azimuth) + ".";
  if (so.outOfRange) line += " The point lies beyond the end of the alignment; the nearest end is reported.";
  if (so.ambiguous) line += " Another location is equally close; the lowest station is reported.";
  line += " Elevation: no profile.";  // REQ-500: profiles are a separate feature
  log.push_back(line);
}

// GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset>  - REQ-484.4
void RunGetPointFromStationOffset(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  double displayed = 0.0, offset = 0.0;
  if (f.size() != 3 || !alignment::ParseStation(f[1], &displayed) || !ParseNum(f[2], &offset)) {
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
    log.push_back("GETPOINTFROMSTATIONOFFSET - station " + alignment::FormatStation(displayed) + " is outside \"" + a->name +
                  "\" (" + Sta(*a, a->StartStation()) + " to " + alignment::FormatStation(a->EndStationWithEquations()) + ").");
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

// CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE][, TOL=<gap>]
// Converts the SELECTED lines, arcs and 2D polylines (REQ-485.2). Line -> Tangent, Arc -> Curve, a polyline
// bulge segment -> Curve; never a vertex approximation. Z is ignored.
void RunCreateAlignmentFromObjects(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.empty() || f[0].empty()) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - usage: CREATEALIGNMENTFROMOBJECTS <name>[, START=<station>][, FILLET=<radius>][, REVERSE][, TOL=<gap>] "
                  "(select the lines, arcs and polylines first).");
    return;
  }
  if (FindAlignment(st, f[0]) >= 0) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - an alignment named \"" + f[0] + "\" already exists; names must be unique.");
    return;
  }
  alignment::FromObjectsOptions opt;
  double startStation = 0.0;
  for (size_t i = 1; i < f.size(); ++i) {
    const std::string t = Lower(f[i]);
    const size_t eq = t.find('=');
    const std::string key = t.substr(0, eq);
    const std::string val = eq == std::string::npos ? "" : f[i].substr(eq + 1);
    double d = 0.0;
    if (key == "reverse" || key == "r") {
      opt.reverse = true;
    } else if (key == "start" && alignment::ParseStation(val, &startStation)) {
    } else if (key == "fillet" && ParseNum(val, &d) && d > 0.0) {
      opt.addFillets = true;
      opt.filletRadius = d;
    } else if (key == "tol" && ParseNum(val, &d) && d > 0.0 && d <= 1.0) {
      opt.gapTolerance = d;
    } else {
      log.push_back("CREATEALIGNMENTFROMOBJECTS - unrecognised option \"" + f[i] + "\".");
      return;
    }
  }
  if (st.selection.empty()) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - select the lines, arcs or polylines to convert first.");
    return;
  }

  std::vector<alignment::SourceSegment> segs;
  int n = 0;
  std::string refusal;
  for (const SelectedEntity& se : st.selection) {
    const int srcNo = ++n;
    using T = SelectedEntity::Type;
    const size_t ix = static_cast<size_t>(se.index);
    if (se.type == T::LineSeg) {
      if (ix * 6 + 5 >= st.userLinesFlat.size()) continue;
      alignment::SourceSegment s;
      s.a = {st.userLinesFlat[ix * 6], st.userLinesFlat[ix * 6 + 1]};
      s.b = {st.userLinesFlat[ix * 6 + 3], st.userLinesFlat[ix * 6 + 4]};
      s.sourceIndex = srcNo;
      segs.push_back(s);
    } else if (se.type == T::Arc) {
      if (ix >= st.userArcs.size()) continue;
      const CadArc& ar = st.userArcs[ix];
      if (!(ar.r > 0.0)) continue;
      alignment::SourceSegment s;
      s.isArc = true;
      s.center = {ar.cx, ar.cy};
      s.radius = ar.r;
      const double a0 = ar.startRad, sw = ar.sweepRad;
      s.startAngle = sw >= 0.0 ? a0 : a0 + sw;
      s.endAngle = s.startAngle + std::fabs(sw);
      s.sourceIndex = srcNo;
      segs.push_back(s);
    } else if (se.type == T::Polyline) {
      if (ix + 1 >= st.userPolylineOffsets.size()) continue;
      const int v0 = st.userPolylineOffsets[ix], v1 = st.userPolylineOffsets[ix + 1];
      const bool closed = ix < st.userPolylineClosed.size() && st.userPolylineClosed[ix] != 0;
      const int cnt = v1 - v0;
      for (int k = 0; k < cnt - 1 + (closed ? 1 : 0); ++k) {
        const size_t i0 = static_cast<size_t>(v0 + k), i1 = static_cast<size_t>(v0 + (k + 1) % cnt);
        const alignment::Point2 p0{st.userPolylineVerts[i0 * 3], st.userPolylineVerts[i0 * 3 + 1]};
        const alignment::Point2 p1{st.userPolylineVerts[i1 * 3], st.userPolylineVerts[i1 * 3 + 1]};
        const double bulge = i0 < st.userPolylineVertsBulge.size() ? static_cast<double>(st.userPolylineVertsBulge[i0]) : 0.0;
        segs.push_back(alignment::SegmentFromBulge(p0, p1, bulge, srcNo));
      }
    } else if (refusal.empty()) {
      refusal = "object " + std::to_string(srcNo) + " is not a line, arc or 2D polyline and was skipped";
    }
  }
  if (!refusal.empty()) log.push_back("CREATEALIGNMENTFROMOBJECTS - note: " + refusal + ".");
  alignment::Alignment made;
  std::string err;
  std::vector<std::string> warn;
  if (!alignment::BuildFromSegments(segs, opt, &made, &err, &warn)) {
    log.push_back("CREATEALIGNMENTFROMOBJECTS - " + err + ". No alignment was created.");
    return;
  }
  made.name = f[0];
  made.startingStation = startStation;
  made.layer = MakeNewEntityAttrs(st).layer;
  if (made.layer.empty()) made.layer = "0";
  PushUndoSnapshot(st, "Create alignment from objects");
  made.id = st.nextEntityId++;
  st.alignments.push_back(std::move(made));
  const alignment::Alignment& r = st.alignments.back();
  for (const std::string& w : warn) log.push_back("CREATEALIGNMENTFROMOBJECTS - " + w + ".");
  log.push_back("CREATEALIGNMENTFROMOBJECTS - created \"" + r.name + "\": " + std::to_string(r.Entities().size()) +
                " entities (" + std::to_string(r.CurveCount()) + " curves), length " + Lin(st, r.Length()) + ".");
  for (const alignment::ContinuityIssue& c : r.CheckContinuity()) {
    if (!c.c1 && c.c0) log.push_back("  Note: direction changes after entity " + std::to_string(c.afterEntity + 1) + " (not tangent).");
  }
}

std::string AlignmentCursorReadout(const AppCommandState& st, double lx, double ly, double maxOffset) {
  const alignment::Alignment* best = nullptr;
  alignment::StationOffset bestSo;
  double bestAbs = maxOffset;
  for (const alignment::Alignment& a : st.alignments) {
    if (a.Empty()) continue;
    const alignment::StationOffset so = a.GetStationOffset(lx, ly);
    if (std::fabs(so.offset) <= bestAbs) {
      bestAbs = std::fabs(so.offset);
      best = &a;
      bestSo = so;
    }
  }
  if (best == nullptr) return {};
  return "STA " + alignment::FormatStation(best->DisplayedStation(bestSo.internalStation, true)) + "  OFF " +
         FormatLinear(bestSo.offset, st.displayLinearPrecision) + "  " + Brg(st, bestSo.azimuth);
}
