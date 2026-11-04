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
#include "AlignmentStyle.hpp"
#include "AlignmentLabelSet.hpp"
#include "alignment/AlignmentReport.hpp"
#include "SpreadsheetIo.hpp"
#include "WinFileDialogs.hpp"
#include "StringUtil.hpp"
#include "util/NumFormat.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
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
//              <name>, SPIRAL, <length>, <end radius>, LEFT|RIGHT     clothoid from tangent (start R = infinite)
//              <name>, SPIRAL, <length>, <start radius>, <end radius>, LEFT|RIGHT
void RunAlignmentAdd(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTADD - usage: ALIGNMENTADD <name>, TANGENT, <x>, <y>, <azimuth deg>, <length> | <name>, TANGENT, "
      "<length> | <name>, TANGENTTO, <x>, <y> | <name>, CURVE, <radius>, <delta deg>, LEFT|RIGHT | <name>, CURVELEN, "
      "<radius>, <arc length>, LEFT|RIGHT | <name>, SPIRAL, <length>, <end radius>, LEFT|RIGHT | <name>, SPIRAL, "
      "<length>, <start radius>, <end radius>, LEFT|RIGHT (0 radius = tangent / infinite).";
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
  } else if (kind == "spiral" && (f.size() == 5 || f.size() == 6)) {
    if (edited.Empty()) {
      log.push_back("ALIGNMENTADD - a spiral needs geometry before it; add a tangent first.");
      return;
    }
    const std::string side = Lower(f.back());
    if (side != "left" && side != "right") {
      log.push_back("ALIGNMENTADD - the spiral direction must be LEFT or RIGHT.");
      return;
    }
    double ls = 0.0, rs = 0.0, re = 0.0;
    if (f.size() == 5) {
      if (!ParseNum(f[2], &ls) || !ParseNum(f[3], &re)) {
        log.push_back(usage);
        return;
      }
      rs = 0.0;
    } else {
      if (!ParseNum(f[2], &ls) || !ParseNum(f[3], &rs) || !ParseNum(f[4], &re)) {
        log.push_back(usage);
        return;
      }
    }
    ok = edited.AppendSpiral(ls, rs, re, side == "right");
    what = "spiral";
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
                  "   Spirals: " + std::to_string(a.SpiralCount()) + "   Station equations: " +
                  std::to_string(a.equations.size()) + "   Continuity check: " +
                  alignment::ContinuityRequirementLabel(a.continuityRequirement));
    for (size_t i = 0; i < a.Entities().size(); ++i) {
      const alignment::Entity& e = a.Entities()[i];
      const int ii = static_cast<int>(i);
      std::string row = "  ENTITY " + std::to_string(i + 1) + "  " + Sta(a, a.EntityStartStation(ii)) + " - " +
                        Sta(a, a.EntityEndStation(ii)) + "  ";
      if (e.kind == alignment::EntityKind::Line) {
        row += "Tangent  bearing " + Brg(st, e.startAzimuth) + "  length " + Lin(st, e.length);
      } else if (e.kind == alignment::EntityKind::Arc) {
        alignment::CurveData cd;
        (void)a.CurveDataFor(ii, &cd);
        row += std::string("Curve ") + (e.right ? "right" : "left") + "  R " + Lin(st, cd.radius) + "  delta " +
               FormatSweptAngle(cd.delta / kDegToRad, CadAngleDisplaySettings(st)) + "  L " + Lin(st, cd.arcLength) +
               "  T " + Lin(st, cd.tangentLength) + "  C " + Lin(st, cd.chord);
      } else {
        alignment::SpiralData sd;
        (void)alignment::SpiralDataFor(ii, a, &sd);
        row += std::string("Spiral ") + (e.right ? "right" : "left") + "  Ls " + Lin(st, sd.length) + "  R " +
               Lin(st, sd.endRadius) + "  theta " + FormatSweptAngle(sd.deflection / kDegToRad, CadAngleDisplaySettings(st));
        if (sd.parameterA > 0.0) row += "  A " + Lin(st, sd.parameterA);
      }
      log.push_back(row);
    }
    for (const alignment::ContinuityIssue& c : a.CheckContinuity()) {
      if (c.FailsRequirement(a.continuityRequirement)) {
        if (!c.c0)
          log.push_back("  WARNING: gap of " + Lin(st, c.gap) + " after entity " + std::to_string(c.afterEntity + 1) +
                        " (C0).");
        else if (!c.c1)
          log.push_back("  WARNING: direction changes after entity " + std::to_string(c.afterEntity + 1) + " (C1).");
        else if (!c.c2)
          log.push_back("  WARNING: curvature mismatch of " + Lin(st, c.curvatureGap) + " after entity " +
                        std::to_string(c.afterEntity + 1) + " (C2).");
      }
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

bool ResolveAlignmentStationSnapPoint(const AppCommandState& st, double displayed, float preferLocalX,
                                      float preferLocalY, float* outLx, float* outLy) {
  if (outLx == nullptr || outLy == nullptr)
    return false;
  const alignment::Alignment* bestAl = nullptr;
  alignment::Point2 bestPt{};
  double bestDist2 = std::numeric_limits<double>::infinity();
  for (const alignment::Alignment& a : st.alignments) {
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
  if (!alignment::ParseStation(staText, &displayed)) {
    st.alignmentStationSnapRejectReason = "STATION snap â€” \"" + staText + "\" is not a station.";
    return false;
  }
  if (!ResolveAlignmentStationSnapPoint(st, displayed, baseLocalX, baseLocalY, lx, ly)) {
    st.alignmentStationSnapRejectReason =
        "STATION snap â€” station " + alignment::FormatStation(displayed) + " is not on any alignment.";
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

namespace {

alignment::Alignment* FindById(AppCommandState& st, std::uint64_t id) {
  for (alignment::Alignment& a : st.alignments) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

/// REQ-488.3: a linked offset follows its parent. Called after anything that changes a parent. A parent that no
/// longer exists detaches the offset (it keeps its geometry); an offset that can no longer be built keeps its last
/// geometry and says why.
void RefreshLinkedOffsetsImpl(AppCommandState& st, std::vector<std::string>& log) {
  for (alignment::Alignment& off : st.alignments) {
    if (off.parentId == 0) continue;
    alignment::Alignment* parent = FindById(st, off.parentId);
    if (parent == nullptr) {
      off.parentId = 0;
      log.push_back("  Offset \"" + off.name + "\" lost its parent and is now independent.");
      continue;
    }
    alignment::Alignment rebuilt = off;
    std::string why;
    if (alignment::MakeOffset(*parent, off.offsetDistance, &rebuilt, &why)) {
      rebuilt.startingStation = off.startingStation;
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

void ReportContinuity(const AppCommandState& st, const alignment::Alignment& r, std::vector<std::string>& log) {
  for (const alignment::ContinuityIssue& c : r.CheckContinuity()) {
    if (!c.FailsRequirement(r.continuityRequirement)) continue;
    if (!c.c0)
      log.push_back("  WARNING: gap of " + Lin(st, c.gap) + " after entity " + std::to_string(c.afterEntity + 1) + " (C0).");
    else if (!c.c1)
      log.push_back("  WARNING: direction changes after entity " + std::to_string(c.afterEntity + 1) + " (C1).");
    else if (!c.c2)
      log.push_back("  WARNING: curvature mismatch of " + Lin(st, c.curvatureGap) + " after entity " +
                    std::to_string(c.afterEntity + 1) + " (C2).");
  }
}

}  // namespace

void RefreshAlignmentOffsets(AppCommandState& st, std::vector<std::string>& log) { RefreshLinkedOffsetsImpl(st, log); }

// ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|DELTA|DIRECTION|CONSTRAINT, <value>
void RunAlignmentEdit(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTEDIT - usage: ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|STARTRADIUS|DELTA|DIRECTION|CONSTRAINT, "
      "<value> (DIRECTION LEFT|RIGHT; CONSTRAINT FIXED|FLOATPREV|FLOATNEXT|FREE; spiral RADIUS/STARTRADIUS 0 = tangent).";
  if (f.size() != 4) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTEDIT", f[0], log);
  if (a == nullptr || IsLinkedOffset(*a, "ALIGNMENTEDIT", log)) return;
  int idx = 0;
  if (!ParseEntityIndex(*a, "ALIGNMENTEDIT", f[1], &idx, log)) return;
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
  alignment::Alignment* target = Resolve(st, "ALIGNMENTEDIT", f[0], log);
  *target = std::move(trial);
  log.push_back("ALIGNMENTEDIT - \"" + target->name + "\" entity " + std::to_string(idx + 1) + " updated; length now " + Lin(st, target->Length()) +
                ", ending at station " + Sta(*target, target->EndStation()) + ".");
  ReportContinuity(st, *target, log);
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING, ON|OFF
void RunAlignmentLock(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() != 4) {
    log.push_back("ALIGNMENTLOCK - usage: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING, ON|OFF.");
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
  if (bit == 0 || (onoff != "on" && onoff != "off")) {
    log.push_back("ALIGNMENTLOCK - usage: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING, ON|OFF.");
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
  log.push_back("REVERSEALIGNMENT - \"" + target->name + "\" now runs from " + Sta(*target, target->StartStation()) + " (was the end) to " +
                Sta(*target, target->EndStation()) + "." + (note.empty() ? "" : " " + note + "."));
  RefreshAlignmentOffsets(st, log);
}

// ALIGNMENTSTATION <name>, START, <station>[, CLEAR]
//                  <name>, EQ, ADD, <raw station>, <ahead station>, INC|DEC
//                  <name>, EQ, REMOVE, <n>   |   <name>, EQ, LIST        - REQ-483, REQ-487.3
void RunAlignmentStation(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTSTATION - usage: ALIGNMENTSTATION <name>, START, <station>[, CLEAR] | <name>, EQ, ADD, <raw station>, <ahead station>, INC|DEC | "
      "<name>, EQ, REMOVE, <n> | <name>, EQ, LIST.";
  if (f.size() < 3) {
    log.push_back(usage);
    return;
  }
  alignment::Alignment* a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
  if (a == nullptr) return;
  const std::string sub = Lower(f[1]);
  if (sub == "start") {
    double sta = 0.0;
    if (!alignment::ParseStation(f[2], &sta)) {
      log.push_back("ALIGNMENTSTATION - \"" + f[2] + "\" is not a station.");
      return;
    }
    const bool clear = f.size() == 4 && Lower(f[3]) == "clear";
    if (!a->equations.empty() && !clear) {
      log.push_back("ALIGNMENTSTATION - changing the starting station removes this alignment's " + std::to_string(a->equations.size()) +
                    " station equation(s); repeat with , CLEAR to confirm.");
      return;
    }
    PushUndoSnapshot(st, "Set starting station");
    a = Resolve(st, "ALIGNMENTSTATION", f[0], log);
    a->equations.clear();
    a->startingStation = sta;
    log.push_back("ALIGNMENTSTATION - \"" + a->name + "\" starts at " + alignment::FormatStation(sta) + " and ends at " + Sta(*a, a->EndStation()) + ".");
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
      log.push_back("  EQUATION " + std::to_string(i + 1) + "  back " + alignment::FormatStation(a->DisplayedStation(q.rawBack, false)) + "  ahead " +
                    alignment::FormatStation(q.ahead) + "  " + (q.increasing ? "increasing" : "decreasing") + "  (raw station " +
                    alignment::FormatStation(q.rawBack) + ")");
    }
    return;
  }
  if (verb == "add" && f.size() == 6) {
    alignment::StationEquation q;
    const std::string dir = Lower(f[5]);
    if (!alignment::ParseStation(f[3], &q.rawBack) || !alignment::ParseStation(f[4], &q.ahead) || (dir != "inc" && dir != "dec")) {
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
                  alignment::FormatStation(target->EndStationWithEquations()) + ".");
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

// OFFSETALIGNMENT <parent>, <distance>[, <distance> ...]    distance: 12 / R12 = right, -12 / L12 = left
void RunOffsetAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  if (f.size() < 2) {
    log.push_back("OFFSETALIGNMENT - usage: OFFSETALIGNMENT <parent>, <distance>[, <distance>...] (R12 or 12 = right, L12 or -12 = left).");
    return;
  }
  alignment::Alignment* parent = Resolve(st, "OFFSETALIGNMENT", f[0], log);
  if (parent == nullptr) return;
  const std::uint64_t parentId = parent->id;
  const alignment::Alignment parentCopy = *parent;
  std::vector<double> dists;
  for (size_t i = 1; i < f.size(); ++i) {
    std::string t = f[i];
    double sign = 1.0;
    if (!t.empty() && (t[0] == 'L' || t[0] == 'l')) { sign = -1.0; t.erase(0, 1); }
    else if (!t.empty() && (t[0] == 'R' || t[0] == 'r')) { t.erase(0, 1); }
    double d = 0.0;
    if (!ParseNum(t, &d) || d == 0.0) {
      log.push_back("OFFSETALIGNMENT - \"" + f[i] + "\" is not a non-zero offset distance.");
      return;
    }
    dists.push_back(sign * d);
  }
  // Build everything first so one bad distance creates nothing.
  std::vector<alignment::Alignment> made;
  for (const double d : dists) {
    alignment::Alignment off;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s - %s%g", parentCopy.name.c_str(), d < 0.0 ? "L" : "R", std::fabs(d));
    off.name = buf;
    if (FindAlignment(st, off.name) >= 0) {
      log.push_back("OFFSETALIGNMENT - an alignment named \"" + off.name + "\" already exists.");
      return;
    }
    off.layer = parentCopy.layer;
    off.style = parentCopy.style;
    off.labelSet = parentCopy.labelSet;
    std::string why;
    if (!alignment::MakeOffset(parentCopy, d, &off, &why)) {
      log.push_back("OFFSETALIGNMENT - refused for " + off.name + ": " + why + ". Nothing was created.");
      return;
    }
    made.push_back(std::move(off));
  }
  PushUndoSnapshot(st, "Offset alignment");
  for (alignment::Alignment& off : made) {
    off.id = st.nextEntityId++;
    off.parentId = parentId;
    log.push_back("OFFSETALIGNMENT - created \"" + off.name + "\" (" + Lin(st, std::fabs(off.offsetDistance)) +
                  (off.offsetDistance < 0.0 ? " left" : " right") + "), length " + Lin(st, off.Length()) + ".");
    st.alignments.push_back(std::move(off));
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

// ALIGNMENTINTERSECT <a>, <b> | <a>, LINE, x1, y1, x2, y2 | <a>, CIRCLE, cx, cy, r   (WORLD coordinates) - REQ-489
void RunAlignmentIntersect(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitFields(args);
  const char* usage =
      "ALIGNMENTINTERSECT - usage: ALIGNMENTINTERSECT <alignment>, <alignment> | <alignment>, LINE, <x1>, <y1>, <x2>, <y2> | "
      "<alignment>, CIRCLE, <cx>, <cy>, <r>.";
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
  for (size_t i = 0; i < hits.size(); ++i) {
    const alignment::Intersection& h = hits[i];
    log.push_back("  " + std::to_string(i + 1) + ": X = " + Lin(st, h.point.x + CadCoord::WorldOriginX(st)) + "  Y = " +
                  Lin(st, h.point.y + CadCoord::WorldOriginY(st)) + "  Station = " + Sta(*a, h.stationA) + "  Angle = " +
                  FormatSweptAngle(h.angle / kDegToRad, CadAngleDisplaySettings(st)) + (h.tangential ? "  (tangential)" : ""));
  }
  for (const alignment::IntersectionOverlap& o : overlaps)
    log.push_back("  Coincident from station " + Sta(*a, o.fromStationA) + " to " + Sta(*a, o.toStationA) + ".");
}

// EDITALIGNMENT [<name>] - REQ-486.1. Opens the Alignment Layout table in the Qt editor; headless it lists the rows.
void RunEditAlignment(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  std::string name = Trim(args);
  if (name.empty()) {
    for (const SelectedEntity& e : st.selection) {
      if (e.type == SelectedEntity::Type::Alignment && e.index >= 0 && static_cast<size_t>(e.index) < st.alignments.size()) {
        name = st.alignments[static_cast<size_t>(e.index)].name;
        break;
      }
    }
  }
  if (name.empty() && !st.alignments.empty()) name = st.alignments.front().name;
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
  RunAlignmentList(st, name, log);  // the same rows, for a window-less run
}

SurfaceComponentStyle* AlignmentComponentByName(AlignmentStyle& s, const std::string& word, std::string* why) {
  const std::string w = Lower(word);
  if (w == "tangent" || w == "line") return &s.tangent;
  if (w == "curve" || w == "arc") return &s.curve;
  if (w == "spiral") return &s.spiral;
  if (why != nullptr)
    *why = "component must be TANGENT, CURVE or SPIRAL, not \"" + word + "\".";
  return nullptr;
}

void RunAlignmentStyle(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentStyles::EnsureStandard(st.alignmentStyles);
  const auto usage = [&]() {
    log.push_back(
        "ALIGNMENTSTYLE - usage: ALIGNMENTSTYLE LIST | NEW <style> | DELETE <style> | ASSIGN <alignment>, <style> | "
        "SHOW|HIDE <style>, <tangent|curve|spiral> | COLOR <style>, <tangent|curve|spiral>, <color>.");
  };
  const std::string trimmed = Trim(args);
  if (trimmed.empty()) {
    usage();
    return;
  }
  const std::vector<std::string> f = SplitFields(trimmed);
  const std::string verb = Lower(f[0]);
  if (verb == "list") {
    if (st.alignmentStyles.empty()) {
      log.push_back("ALIGNMENTSTYLE - no alignment styles.");
      return;
    }
    for (const AlignmentStyle& s : st.alignmentStyles)
      log.push_back("  STYLE \"" + s.name + "\"  (used by " +
                    std::to_string(AlignmentStyles::CountAlignmentsResolvingTo(st.alignments, st.alignmentStyles, s.name)) +
                    " alignment(s))");
    return;
  }
  if (verb == "new") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    const std::string name = f[1];
    if (AlignmentStyles::Find(st.alignmentStyles, name)) {
      log.push_back("ALIGNMENTSTYLE - a style named \"" + name + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    AlignmentStyle s = AlignmentStyles::StandardAlignmentStyle();
    s.name = name;
    st.alignmentStyles.push_back(std::move(s));
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
    const int users = AlignmentStyles::CountAlignmentsResolvingTo(st.alignments, st.alignmentStyles, name);
    if (users > 0) {
      log.push_back("ALIGNMENTSTYLE - \"" + name + "\" is used by " + std::to_string(users) +
                    " alignment(s) and cannot be deleted.");
      return;
    }
    const auto it = std::find_if(st.alignmentStyles.begin(), st.alignmentStyles.end(),
                                 [&](const AlignmentStyle& s) { return s.name == name; });
    if (it == st.alignmentStyles.end()) {
      log.push_back("ALIGNMENTSTYLE - no style named \"" + name + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment style");
    st.alignmentStyles.erase(it);
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTSTYLE - deleted style \"" + name + "\".");
    return;
  }
  if (verb == "assign") {
    if (f.size() < 3 || f[1].empty() || f[2].empty()) {
      usage();
      return;
    }
    alignment::Alignment* a = Resolve(st, "ALIGNMENTSTYLE", f[1], log);
    if (a == nullptr) return;
    if (!AlignmentStyles::Find(st.alignmentStyles, f[2])) {
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
    AlignmentStyle* s = AlignmentStyles::Find(st.alignmentStyles, f[1]);
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
    AlignmentStyle* s = AlignmentStyles::Find(st.alignmentStyles, f[1]);
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
  usage();
}

void RunAlignmentLabelSet(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  AlignmentLabelSets::EnsureStandard(st.alignmentLabelSets);
  const auto usage = [&]() {
    log.push_back(
        "ALIGNMENTLABELSET - usage: ALIGNMENTLABELSET LIST | NEW <set> | DELETE <set> | ASSIGN <alignment>, <set> | "
        "MAJORINTERVAL <set>, <interval> | MINORINTERVAL <set>, <interval> | SHOW|HIDE <set>, MAJOR|MINOR.");
  };
  const std::string trimmed = Trim(args);
  if (trimmed.empty()) {
    usage();
    return;
  }
  const std::vector<std::string> f = SplitFields(trimmed);
  const std::string verb = Lower(f[0]);
  if (verb == "list") {
    for (const AlignmentLabelSet& s : st.alignmentLabelSets)
      log.push_back("  LABEL SET \"" + s.name + "\"  major=" + Lin(st, s.majorInterval) + " minor=" +
                    Lin(st, s.minorInterval) +
                    "  (used by " +
                    std::to_string(AlignmentLabelSets::CountAlignmentsResolvingTo(st.alignments, st.alignmentLabelSets,
                                                                                  s.name)) +
                    " alignment(s))");
    return;
  }
  if (verb == "new") {
    if (f.size() < 2 || f[1].empty()) {
      usage();
      return;
    }
    if (AlignmentLabelSets::Find(st.alignmentLabelSets, f[1])) {
      log.push_back("ALIGNMENTLABELSET - a label set named \"" + f[1] + "\" already exists.");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    AlignmentLabelSet s = AlignmentLabelSets::StandardLabelSet();
    s.name = f[1];
    st.alignmentLabelSets.push_back(std::move(s));
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - created label set \"" + f[1] + "\" (copied from Standard).");
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
    const int users = AlignmentLabelSets::CountAlignmentsResolvingTo(st.alignments, st.alignmentLabelSets, f[1]);
    if (users > 0) {
      log.push_back("ALIGNMENTLABELSET - \"" + f[1] + "\" is used by " + std::to_string(users) +
                    " alignment(s) and cannot be deleted.");
      return;
    }
    const auto it = std::find_if(st.alignmentLabelSets.begin(), st.alignmentLabelSets.end(),
                                 [&](const AlignmentLabelSet& s) { return s.name == f[1]; });
    if (it == st.alignmentLabelSets.end()) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    PushUndoSnapshot(st, "Alignment label set");
    st.alignmentLabelSets.erase(it);
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
    if (!AlignmentLabelSets::Find(st.alignmentLabelSets, f[2])) {
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
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]);
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
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]);
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
    AlignmentLabelSet* s = AlignmentLabelSets::Find(st.alignmentLabelSets, f[1]);
    if (s == nullptr) {
      log.push_back("ALIGNMENTLABELSET - no label set named \"" + f[1] + "\".");
      return;
    }
    const std::string which = Lower(f[2]);
    PushUndoSnapshot(st, "Alignment label set");
    if (which == "major")
      s->majorStationsOn = (verb == "show");
    else if (which == "minor")
      s->minorStationsOn = (verb == "show");
    else {
      log.push_back("ALIGNMENTLABELSET - component must be MAJOR or MINOR, not \"" + f[2] + "\".");
      return;
    }
    BumpCadGpuCache(st);
    log.push_back("ALIGNMENTLABELSET - \"" + s->name + "\" " + f[2] + " " + (verb == "show" ? "shown." : "hidden."));
    return;
  }
  usage();
}

// ALIGNMENTREPORT [<name>][, TXT|CSV[, <path>]]  (REQ-494)
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

  if (st.alignments.empty()) {
    log.push_back("ALIGNMENTREPORT - this drawing has no alignments.");
    return;
  }

  std::vector<const alignment::Alignment*> targets;
  for (const alignment::Alignment& a : st.alignments) {
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

  if (exportKind.empty()) {
    for (size_t i = 0; i < targets.size(); ++i) {
      std::vector<std::string> lines;
      alignment::BuildAlignmentReportLines(*targets[i], fmt, &lines);
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
      SheetGrid::Grid part = alignment::BuildAlignmentReportCsv(*a, fmt);
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
    alignment::BuildAlignmentReportLines(*targets[i], fmt, &lines);
    for (size_t li = 0; li < lines.size(); ++li) {
      out << lines[li];
      if (li + 1 < lines.size()) out << '\n';
    }
    if (targets.size() > 1 && i + 1 < targets.size()) out << "\n---\n";
  }
  log.push_back("ALIGNMENTREPORT - report written to \"" + path + "\".");
}
