#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"
#include "surface/CadSurfaceInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Terrain/daylight.hpp>
#include <GoSurvey/Terrain/surfacequery.hpp>
#include <GoSurvey/Terrain/tinbuild.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/// REQ-398. Defined with the rest of GRADING, far below; declared here because this is the routing
/// site that must reach it.
void HandleGradingPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log);

// -------------------------------------------------------------------------------------------------
// REQ-398 — GRADING: side slopes from a feature line out to a surface, producing the daylight line.
//
// The arithmetic lives in util/daylight.hpp, pure and tested against hand-computed offsets. What is
// here is the command: pick a baseline, sit on an options prompt, and commit the result.
// -------------------------------------------------------------------------------------------------

namespace {

/// Parse a slope in either convention REQ-074 names, returning the RUN of run:rise.
///
///   "3:1" -> 3      "3" -> 3 (run:1)      "33.33%" -> 100/33.33
///
/// Returns false for anything that is not a slope, so the prompt can keep asking rather than grade
/// at a default the user never chose.
bool ParseSlopeRun(const std::string& text, double* outRun) {
  const std::string t = StringUtil::trimCopy(text);
  if (t.empty())
    return false;

  // Percent: the rise per hundred of run, so the run is 100/percent.
  if (t.back() == '%') {
    const std::string num = StringUtil::trimCopy(t.substr(0, t.size() - 1));
    char* end = nullptr;
    const double pct = std::strtod(num.c_str(), &end);
    if (!end || end == num.c_str() || *end != '\0' || !std::isfinite(pct) || pct <= 0.0)
      return false;
    *outRun = 100.0 / pct;
    return true;
  }

  // run:rise, the surveyor's form.
  const size_t colon = t.find(':');
  if (colon != std::string::npos) {
    const std::string runs = StringUtil::trimCopy(t.substr(0, colon));
    const std::string rises = StringUtil::trimCopy(t.substr(colon + 1));
    char* e1 = nullptr;
    char* e2 = nullptr;
    const double run = std::strtod(runs.c_str(), &e1);
    const double rise = std::strtod(rises.c_str(), &e2);
    if (!e1 || e1 == runs.c_str() || *e1 != '\0' || !e2 || e2 == rises.c_str() || *e2 != '\0')
      return false;
    if (!std::isfinite(run) || !std::isfinite(rise) || run <= 0.0 || rise <= 0.0)
      return false;
    *outRun = run / rise;
    return true;
  }

  // A bare number is the run against a rise of one, which is how "3:1" is usually spoken.
  char* end = nullptr;
  const double run = std::strtod(t.c_str(), &end);
  if (!end || end == t.c_str() || *end != '\0' || !std::isfinite(run) || run <= 0.0)
    return false;
  *outRun = run;
  return true;
}

/// Both of REQ-074's conventions from the one run, so a slope can never be described two ways.
std::string FormatSlope(double run) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.4g:1 (%.2f%%)", run, SlopePercentFromRun(run));
  return std::string(buf);
}

/// The surface GRADING will use if the user does not name one: the one it used last, else the only
/// surface in the drawing. Empty when there is a genuine choice to make.
std::string GradingDefaultSurface(const AppCommandState& st) {
  if (!st.gradingSurface.empty() && FindSurfaceIndex(st, st.gradingSurface) >= 0)
    return st.gradingSurface;
  const auto* gs = st.sessionAs<stepengine::GradingSession>();
  if (gs && gs->editId != 0)
    return std::string();
  if (st.drawing.cadSurfaces.size() == 1)
    return st.drawing.cadSurfaces[0].name;
  return std::string();
}

int GradingBaselineIndex(const AppCommandState& st) {
  const auto* gs = st.sessionAs<stepengine::GradingSession>();
  if (!gs || gs->baselineId == 0)
    return -1;
  const int ix = FindEntityIndexById(st.drawing.featureLineAttrs, gs->baselineId);
  if (ix < 0 || static_cast<size_t>(ix) + 1 >= st.drawing.featureLineOffsets.size())
    return -1;
  return ix;
}

bool GradingBaselineIsOpen(const AppCommandState& st) {
  const int i = GradingBaselineIndex(st);
  if (i < 0 || static_cast<size_t>(i) >= st.drawing.featureLineClosed.size())
    return false;
  return st.drawing.featureLineClosed[static_cast<size_t>(i)] == 0;
}

void GradingPrompt(AppCommandState& st, std::vector<std::string>& log) {
  if (auto* gs = st.sessionAs<stepengine::GradingSession>())
    gs->phase = stepengine::GradingPhase::WaitOptions;
  const std::string surf = GradingDefaultSurface(st);
  const char* target = "Surface";
  if (st.gradingTarget == CadGradingDefinition::Target::Distance) target = "Distance";
  if (st.gradingTarget == CadGradingDefinition::Target::AbsoluteElevation) target = "Absolute";
  if (st.gradingTarget == CadGradingDefinition::Target::RelativeElevation) target = "Relative";
  std::string msg = "GRADING - Enter to grade, or [Cut/Fill/Surface";
  if (GradingBaselineIsOpen(st))
    msg += "/Side";
  msg += "] or [Target] <cut " + FormatSlope(st.gradingCutRun) + ", fill " + FormatSlope(st.gradingFillRun) +
         ", " + target + (st.gradingTarget == CadGradingDefinition::Target::Surface
                               ? ", " + (surf.empty() ? std::string("no surface chosen") : surf)
                               : ", " + std::to_string(st.gradingTargetValue));
  if (GradingBaselineIsOpen(st))
    msg += st.gradingSideChosen ? (st.gradingSideLeft ? ", side left" : ", side right")
                                : ", side NOT chosen";
  msg += ">. ESC cancels.";
  log.push_back(msg);
}

std::string GradingTargetDescription(const AppCommandState& st,
                                     const CadSurface* surface) {
  switch (st.gradingTarget) {
    case CadGradingDefinition::Target::Surface:
      return surface ? "to " + surface->name : "to the selected surface";
    case CadGradingDefinition::Target::Distance:
      return "distance " + std::to_string(st.gradingTargetValue) +
             " at " + std::to_string(st.gradingDistanceGradePercent) + "% grade";
    case CadGradingDefinition::Target::AbsoluteElevation:
      return "absolute elevation " + std::to_string(st.gradingTargetValue);
    case CadGradingDefinition::Target::RelativeElevation:
      return "relative elevation change " + std::to_string(st.gradingTargetValue);
  }
  return "unknown target";
}

/// The feature line the command will grade from: the STABLE ID of the one selected, or 0.
std::uint64_t GradingSelectedFeatureLine(AppCommandState& st, int* outCount) {
  EnsureEntityIds(st);  // so a line drawn moments ago already has the id the grading will hold it by
  int n = 0;
  std::uint64_t id = 0;
  for (const SelectedEntity& e : st.selection) {
    if (e.type != SelectedEntity::Type::FeatureLine || e.index < 0)
      continue;
    if (static_cast<size_t>(e.index) >= st.drawing.featureLineAttrs.size())
      continue;
    ++n;
    id = st.drawing.featureLineAttrs[static_cast<size_t>(e.index)].id;
  }
  if (outCount)
    *outCount = n;
  return id;
}

/// Take feature line \p id as GRADING's subject (issue #997). An ordinary line becomes the baseline
/// of a new grading. A grading result is opened for EDITING instead: its stored baseline, slopes,
/// side and target come back as the prompt's values. Returns false, having said why, when the
/// result's baseline is gone - rebuilding it from nothing would be inventing a design.
bool GradingTakeFeatureLine(AppCommandState& st, std::uint64_t id, std::vector<std::string>& log) {
  auto* gs = st.sessionAs<stepengine::GradingSession>();
  if (!gs)
    return false;
  gs->editId = 0;
  const int ix = FindEntityIndexById(st.drawing.featureLineAttrs, id);
  const CadFeatureLineInfo* info =
      (ix >= 0 && static_cast<size_t>(ix) < st.drawing.featureLineInfo.size())
          ? &st.drawing.featureLineInfo[static_cast<size_t>(ix)]
          : nullptr;
  if (info == nullptr || !info->grading) {
    gs->baselineId = id;
    return true;
  }
  const CadGradingDefinition& def = *info->grading;
  if (FindEntityIndexById(st.drawing.featureLineAttrs, def.baselineId) < 0) {
    log.push_back("GRADING - the baseline of \"" + info->name +
                  "\" is gone, so it cannot be rebuilt. It is left as it is.");
    return false;
  }
  gs->editId = id;
  gs->baselineId = def.baselineId;
  st.gradingCutRun = def.cutRun;
  st.gradingFillRun = def.fillRun;
  st.gradingSideLeft = def.sideLeft;
  st.gradingTarget = def.target;
  st.gradingTargetValue = def.targetValue;
  st.gradingDistanceGradePercent = def.distanceGradePercent;
  st.gradingSideChosen = true;
  const int si = FindSurfaceIndexById(st, def.surfaceId);
  st.gradingSurface = si >= 0 ? st.drawing.cadSurfaces[static_cast<size_t>(si)].name : std::string();
  log.push_back("GRADING - editing \"" + info->name + "\". Enter rebuilds it in place.");
  if (si < 0 && def.target == CadGradingDefinition::Target::Surface)
    log.push_back("GRADING - its target surface is gone. Type S to choose another.");
  return true;
}

/// Replace feature line \p li's vertices in place, keeping its slot, id and attributes. The vertex
/// and flag arrays are cut on different strides, so both are spliced here together and every later
/// line's offset moves by the change in count.
void ReplaceFeatureLineVertices(AppCommandState& st, size_t li, const std::vector<double>& xyz,
                                const std::vector<uint8_t>& elevPt, bool closed) {
  const int a = st.drawing.featureLineOffsets[li];
  const int b = st.drawing.featureLineOffsets[li + 1];
  const int n = static_cast<int>(xyz.size() / 3);
  st.drawing.featureLineVerts.erase(st.drawing.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(a) * 3,
                            st.drawing.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(b) * 3);
  st.drawing.featureLineVerts.insert(st.drawing.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(a) * 3,
                             xyz.begin(), xyz.end());
  if (st.drawing.featureLineElevPt.size() < static_cast<size_t>(b))
    st.drawing.featureLineElevPt.resize(static_cast<size_t>(b), 0);
  st.drawing.featureLineElevPt.erase(st.drawing.featureLineElevPt.begin() + a, st.drawing.featureLineElevPt.begin() + b);
  st.drawing.featureLineElevPt.insert(st.drawing.featureLineElevPt.begin() + a, elevPt.begin(), elevPt.end());
  for (size_t k = li + 1; k < st.drawing.featureLineOffsets.size(); ++k)
    st.drawing.featureLineOffsets[k] += n - (b - a);
  st.drawing.featureLineClosed[li] = static_cast<uint8_t>(closed ? 1 : 0);
}

/// Snapshot the strips between corresponding baseline and daylight stations.
CadGradingGroupMember BakeGradingMember(const AppCommandState &st,
                                        std::uint64_t id,
                                        const ResolvedChain &baseline,
                                        const ResolvedChain &daylight) {
  CadGradingGroupMember member;
  member.gradingId = id;
  const size_t n = baseline.verts.size();
  std::vector<std::vector<double>> strips;
  strips.reserve(n);
  for (size_t i = 0; i < (baseline.closed ? n : n - 1); ++i) {
    const size_t j = (i + 1) % n;
    std::vector<double> strip;
    strip.reserve(12);
    for (const auto &v : {baseline.verts[i], baseline.verts[j],
                          daylight.verts[j], daylight.verts[i]}) {
      strip.push_back(v[0] - st.drawing.worldDocumentOriginX);
      strip.push_back(v[1] - st.drawing.worldDocumentOriginY);
      strip.push_back(v[2]);
    }
    strips.push_back(std::move(strip));
  }
  member.strips = std::make_shared<const std::vector<std::vector<double>>>(std::move(strips));
  return member;
}

void BuildGradingDaylight(AppCommandState& st, std::vector<std::string>& log) {
  const int bi = GradingBaselineIndex(st);
  if (bi < 0) {
    // REQ-076: the id resolved to nothing, which is the correct outcome for a deleted baseline —
    // far better than grading whatever entity inherited its array slot.
    log.push_back("GRADING - the baseline feature line is gone.");
    return;
  }
  const std::string surfName = GradingDefaultSurface(st);
  const int si = st.gradingTarget == CadGradingDefinition::Target::Surface
                     ? FindSurfaceIndex(st, surfName) : -1;
  const CadSurface* surf = si >= 0 ? &st.drawing.cadSurfaces[static_cast<size_t>(si)] : nullptr;
  std::unique_ptr<ISurfaceQuery> ground;
  if (st.gradingTarget == CadGradingDefinition::Target::Surface) {
    if (!surf) {
      log.push_back("GRADING - choose a surface first: type S, or T for another target.");
      return;
    }
    ground = MakeSurfaceQuery(st, *surf);
    if (!ground) {
      log.push_back("GRADING - \"" + surf->name + "\" has never been built; there is no ground to meet.");
      return;
    }
  }

  const bool closed = st.drawing.featureLineClosed[static_cast<size_t>(bi)] != 0;
  if (!closed && !st.gradingSideChosen) {
    // REQ-398: an open baseline has no intrinsic outward, so it must be told rather than guessed.
    log.push_back("GRADING - this baseline is open, so it has no outward side. Type D to choose "
                  "Left or Right.");
    return;
  }

  const int v0 = st.drawing.featureLineOffsets[static_cast<size_t>(bi)];
  const int v1 = st.drawing.featureLineOffsets[static_cast<size_t>(bi) + 1];
  const int nv = v1 - v0;
  if (nv < 2) {
    log.push_back("GRADING - the baseline has too few vertices to have a direction.");
    return;
  }

  std::vector<double> xy;
  xy.reserve(static_cast<size_t>(nv) * 2);
  for (int k = 0; k < nv; ++k) {
    xy.push_back(st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ k) * 3 + 0]);
    xy.push_back(st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ k) * 3 + 1]);
  }
  std::vector<double> nxy;
  if (!BaselineOutwardNormals(xy, closed, st.gradingSideLeft, &nxy)) {
    log.push_back("GRADING - the baseline has no two distinct points, so there is no outward.");
    return;
  }

  // The step is scaled to the surface's own triangle spacing rather than fixed, which is what keeps
  // REQ-398's stated known limit tracking the data instead of a constant.
  DaylightSearch search;
  search.stepFt = 1.0;
  search.maxOffsetFt = 1000.0;
  if (surf && surf->tin && surf->tin->indices.size() >= 3 && surf->tin->vertsXyz.size() >= 6) {
    // Representative triangle span = sqrt(plan bbox area / triangle count), halved so the march
    // cannot stride over a whole triangle. Clamped so a pathological surface cannot make the search
    // either useless or unbearably slow.
    double minX = surf->tin->vertsXyz[0], maxX = minX;
    double minY = surf->tin->vertsXyz[1], maxY = minY;
    for (size_t k = 0; k + 2 < surf->tin->vertsXyz.size(); k += 3) {
      minX = std::min(minX, surf->tin->vertsXyz[k]);
      maxX = std::max(maxX, surf->tin->vertsXyz[k]);
      minY = std::min(minY, surf->tin->vertsXyz[k + 1]);
      maxY = std::max(maxY, surf->tin->vertsXyz[k + 1]);
    }
    const size_t tris = std::max<size_t>(surf->tin->indices.size() / 3, 1);
    const double area = std::max(1.0, (maxX - minX) * (maxY - minY));
    const double triSpan = std::sqrt(area / static_cast<double>(tris));
    search.stepFt = std::clamp(triSpan * 0.5, 0.05, 10.0);
    search.maxOffsetFt = std::max(100.0, std::hypot(maxX - minX, maxY - minY));
  }

  SideSlopes slopes;
  slopes.cutRun = st.gradingCutRun;
  slopes.fillRun = st.gradingFillRun;

  std::vector<double> out;        // XYZ of the daylight line
  std::vector<uint8_t> outElev;   // every daylight point carries a real elevation, so none is a PI-only
  int cut = 0, fill = 0, onGrade = 0;
  int offSurface = 0, neverMet = 0, degenerate = 0;

  for (int k = 0; k < nv; ++k) {
    const double bx = st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ k) * 3 + 0];
    const double by = st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ k) * 3 + 1];
    const double bz = st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ k) * 3 + 2];
    const double dx = nxy[static_cast<size_t>(k) * 2 + 0];
    const double dy = nxy[static_cast<size_t>(k) * 2 + 1];
    if (dx == 0.0 && dy == 0.0) {
      ++degenerate;
      continue;
    }

    DaylightPoint d;
    if (st.gradingTarget == CadGradingDefinition::Target::Surface) {
      d = SolveDaylightPoint(*ground, bx, by, bz, dx, dy, slopes, search);
    } else {
      double offset = 0.0, z = bz;
      switch (st.gradingTarget) {
        case CadGradingDefinition::Target::Distance:
          offset = st.gradingTargetValue;
          z += offset * st.gradingDistanceGradePercent / 100.0;
          break;
        case CadGradingDefinition::Target::AbsoluteElevation: {
          z = st.gradingTargetValue;
          const double delta = z - bz;
          offset = std::abs(delta) * (delta > 0.0 ? slopes.cutRun : slopes.fillRun);
          break;
        }
        case CadGradingDefinition::Target::RelativeElevation: {
          const double delta = st.gradingTargetValue;
          z += delta;
          offset = std::abs(delta) * (delta > 0.0 ? slopes.cutRun : slopes.fillRun);
          break;
        }
        case CadGradingDefinition::Target::Surface: break;
      }
      d.daylighted = true;
      d.offset = offset;
      d.x = bx + dx * offset;
      d.y = by + dy * offset;
      d.z = z;
      if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) ||
          !std::isfinite(offset) || offset < 0.0) d.daylighted = false;
    }
    if (!d.daylighted) {
      switch (d.why) {
        case DaylightFailure::BaselineOutsideSurface:
        case DaylightFailure::SlopeLeftSurface: ++offSurface; break;
        case DaylightFailure::NeverMeets:       ++neverMet; break;
        default:                                ++degenerate; break;
      }
      continue;
    }
    if (d.offset <= 1e-9)
      ++onGrade;
    else if (d.inCut)
      ++cut;
    else
      ++fill;
    out.push_back(d.x);
    out.push_back(d.y);
    out.push_back(d.z);
    outElev.push_back(0);
  }

  const size_t got = out.size() / 3;
  const size_t need = closed ? 3u : 2u;
  if (got < need) {
    // Every reason is reported rather than collapsed into one message, because "it ran off the
    // surface" and "it can never meet the ground" call for completely different fixes.
    std::string why;
    if (offSurface > 0)
      why += " " + std::to_string(offSurface) + " ran past the surface edge (REQ-074: no elevation "
             "is invented out there).";
    if (neverMet > 0)
      why += " " + std::to_string(neverMet) +
             " could never meet the ground - it falls away at least as fast as the slope.";
    if (degenerate > 0)
      why += " " + std::to_string(degenerate) + " had an unusable slope.";
    log.push_back("GRADING - only " + std::to_string(got) + " of " + std::to_string(nv) +
                  " stations daylighted, too few for a line." + why);
    return;
  }

  // A closed baseline whose stations did not ALL daylight does not produce a closed daylight line.
  // Joining the survivors into a ring would draw a straight run across ground where no daylight was
  // ever computed, and that span is exactly what someone measures for a limit of disturbance — a
  // confident wrong number. The log already names the skipped stations, but a log is scrolled away
  // and geometry is dimensioned, so the geometry has to admit the gap itself.
  const bool skippedAny = (offSurface + neverMet + degenerate) > 0;
  const bool emitClosed = closed && !skippedAny;

  // Issue #997: the inputs travel with the result, by stable id, so it can be edited after reopen.
  EnsureEntityIds(st);
  const auto* gs = st.sessionAs<stepengine::GradingSession>();
  const std::uint64_t baselineId = gs ? gs->baselineId : 0;
  const std::uint64_t editId = gs ? gs->editId : 0;
  CadGradingDefinition def;
  def.baselineId = baselineId;
  def.surfaceId = static_cast<size_t>(si) < st.drawing.cadSurfaceAttrs.size()
                      ? st.drawing.cadSurfaceAttrs[static_cast<size_t>(si)].id
                      : 0;
  def.cutRun = slopes.cutRun;
  def.fillRun = slopes.fillRun;
  def.sideLeft = st.gradingSideLeft;
  def.target = st.gradingTarget;
  def.targetValue = st.gradingTargetValue;
  def.distanceGradePercent = st.gradingDistanceGradePercent;
  const std::string description = "REQ-398 daylight line: cut " + FormatSlope(slopes.cutRun) +
                                  ", fill " + FormatSlope(slopes.fillRun) + ", " +
                                  GradingTargetDescription(st, surf);

  std::vector<std::pair<size_t, CadSurface>> rebuiltGroups;
  if (editId != 0) {
    for (size_t i = 0; i < st.drawing.cadSurfaces.size(); ++i) {
      const auto &group = st.drawing.cadSurfaces[i];
      if (!group.gradingGroup)
        continue;
      const auto it = std::find_if(
          group.gradingMembers.begin(), group.gradingMembers.end(),
          [&](const auto &m) { return m.gradingId == editId; });
      if (it == group.gradingMembers.end())
        continue;
      if (skippedAny) {
        log.push_back("GRADING - incomplete daylight cannot rebuild its group; "
                      "nothing changed.");
        return;
      }
      ResolvedChain baseline, daylight;
      if (!ResolveDefinitionChain(st, def.baselineId, false, baseline)) {
        log.push_back("GRADING - missing group baseline; nothing changed.");
        return;
      }
      daylight.closed = emitClosed;
      for (size_t v = 0; v < out.size(); v += 3)
        daylight.verts.push_back({out[v] + st.drawing.worldDocumentOriginX,
                                  out[v + 1] + st.drawing.worldDocumentOriginY,
                                  out[v + 2]});
      if (baseline.verts.size() != daylight.verts.size() || baseline.closed != daylight.closed) {
        log.push_back("GRADING - incomplete group station correspondence; nothing changed.");
        return;
      }
      CadSurface candidate = group;
      CadGradingGroupMember rebuilt =
          BakeGradingMember(st, editId, baseline, daylight);
      if (it->infill) {
        std::vector<double> ring;
        ring.reserve(daylight.verts.size() * 3);
        for (const auto& point : daylight.verts)
          ring.insert(ring.end(), {point[0] - st.drawing.worldDocumentOriginX,
                                   point[1] - st.drawing.worldDocumentOriginY, point[2]});
        rebuilt.infill = true;
        rebuilt.strips = std::make_shared<const std::vector<std::vector<double>>>(
            std::vector<std::vector<double>>{std::move(ring)});
      }
      candidate.gradingMembers[static_cast<size_t>(
          it - group.gradingMembers.begin())] = std::move(rebuilt);
      if (!BuildSurfaceFromSources(st, candidate, log)) {
        log.push_back(
            "GRADING - group surface could not be rebuilt; nothing changed.");
        return;
      }
      rebuiltGroups.emplace_back(i, std::move(candidate));
    }
  }

  size_t resultIx = 0;
  const bool editing = editId != 0;
  if (editing) {
    const int ti = FindEntityIndexById(st.drawing.featureLineAttrs, editId);
    if (ti < 0 || static_cast<size_t>(ti) + 1 >= st.drawing.featureLineOffsets.size() ||
        static_cast<size_t>(ti) >= st.drawing.featureLineInfo.size()) {
      log.push_back("GRADING - the grading being edited is gone; nothing was changed.");
      return;
    }
    resultIx = static_cast<size_t>(ti);
    PushUndoSnapshot(st, "Edit grading");
    ReplaceFeatureLineVertices(st, resultIx, out, outElev, emitClosed);
    st.drawing.featureLineInfo[resultIx].description = description;
    st.drawing.featureLineInfo[resultIx].grading = def;
  } else {
    PushUndoSnapshot(st, "Grading daylight line");
    if (st.drawing.featureLineOffsets.empty())
      st.drawing.featureLineOffsets.push_back(0);
    const int baseVert = st.drawing.featureLineOffsets.back();
    st.drawing.featureLineVerts.insert(st.drawing.featureLineVerts.end(), out.begin(), out.end());
    st.drawing.featureLineElevPt.insert(st.drawing.featureLineElevPt.end(), outElev.begin(), outElev.end());
    st.drawing.featureLineOffsets.push_back(baseVert + static_cast<int>(got));
    st.drawing.featureLineClosed.push_back(static_cast<uint8_t>(emitClosed ? 1 : 0));

    CadFeatureLineInfo info;
    const std::string baseName = (static_cast<size_t>(bi) < st.drawing.featureLineInfo.size() &&
                                  !st.drawing.featureLineInfo[static_cast<size_t>(bi)].name.empty())
                                     ? st.drawing.featureLineInfo[static_cast<size_t>(bi)].name
                                     : std::string("Baseline");
    info.name = baseName + " Daylight";
    info.description = description;
    info.grading = def;
    st.drawing.featureLineInfo.push_back(std::move(info));
    st.drawing.featureLineAttrs.push_back(
        MakeNewObjectAttrs(st, ObjectLayerKind::FeatureLine, st.drawing.featureLineInfo.back().name));  // REQ-361
    resultIx = st.drawing.featureLineInfo.size() - 1;
  }
  BumpCadGpuCache(st);

  for (auto &entry : rebuiltGroups) {
    st.drawing.cadSurfaces[entry.first] = std::move(entry.second);
    st.drawing.cadSurfaces[entry.first].builtAtRevision = st.cadGpuRevision;
    MarkVolumeSurfacesDirtyForParent(st, st.drawing.cadSurfaces[entry.first].name);
  }

  log.push_back("GRADING - daylight line \"" + st.drawing.featureLineInfo[resultIx].name + "\" " +
                (editing ? "rebuilt" : "created") + ", " + std::to_string(got) + " of " +
                std::to_string(nv) + " stations daylighted.");
  log.push_back("GRADING - cut " + FormatSlope(slopes.cutRun) + " at " + std::to_string(cut) +
                " stations, fill " + FormatSlope(slopes.fillRun) + " at " + std::to_string(fill) +
                " stations, already on grade at " + std::to_string(onGrade) + ".");
  if (skippedAny) {
    log.push_back("GRADING - skipped " + std::to_string(offSurface) + " past the surface edge, " +
                  std::to_string(neverMet) + " that never meet the ground, " +
                  std::to_string(degenerate) + " with an unusable slope.");
    if (closed) {
      log.push_back("GRADING - the daylight line is left OPEN because of those gaps: it would "
                    "otherwise close across ground where no daylight was computed.");
    }
  }
  stepengine::EndSession(st);
}

}  // namespace

namespace {

bool SnapshotGradingMember(const AppCommandState &st, std::uint64_t id,
                           CadGradingGroupMember &member,
                           std::vector<std::string> &log) {
  const int ix = FindEntityIndexById(st.drawing.featureLineAttrs, id);
  if (ix < 0 || static_cast<size_t>(ix) >= st.drawing.featureLineInfo.size() ||
      !st.drawing.featureLineInfo[static_cast<size_t>(ix)].grading) {
    log.push_back(
        "GRADINGGROUP - select grading results, not ordinary feature lines.");
    return false;
  }
  const auto &def = *st.drawing.featureLineInfo[static_cast<size_t>(ix)].grading;
  ResolvedChain baseline, daylight;
  if (!ResolveDefinitionChain(st, def.baselineId, false, baseline) ||
      !ResolveDefinitionChain(st, id, false, daylight) ||
      baseline.verts.size() != daylight.verts.size() ||
      baseline.closed != daylight.closed) {
    log.push_back("GRADINGGROUP - missing baseline or incomplete daylight "
                  "line; rebuild the grading first.");
    return false;
  }
  member = BakeGradingMember(st, id, baseline, daylight);
  return true;
}

} // namespace

/// REQ-404: assign selected grading results to a named ordinary design surface.
/// Validate and build temporary surfaces before publishing one undoable
/// membership change.
void ExecuteGradingGroupCommand(AppCommandState &st, const std::string &args,
                                std::vector<std::string> &log) {
  const auto fields = SplitCommaFields(args);
  const std::string name = fields.empty() ? std::string() : fields[0];
  const std::string verb =
      fields.size() > 1 ? StringUtil::toLowerAsciiCopy(fields[1]) : "add";
  if (name.empty() || fields.size() > 2 ||
      (verb != "add" && verb != "remove" && verb != "list")) {
    log.push_back(
        "GRADINGGROUP - usage: GRADINGGROUP <name>[, Add/Remove/List]. Select "
        "grading results first; Add creates the group if needed.");
    return;
  }
  const int existing = FindSurfaceIndex(st, name);
  if (existing >= 0 &&
      !st.drawing.cadSurfaces[static_cast<size_t>(existing)].gradingGroup) {
    log.push_back("GRADINGGROUP - that name belongs to an ordinary surface. "
                  "Choose another name.");
    return;
  }
  if (verb == "list") {
    if (existing < 0) {
      log.push_back("GRADINGGROUP - no group named \"" + name + "\".");
      return;
    }
    const auto &group = st.drawing.cadSurfaces[static_cast<size_t>(existing)];
    log.push_back("GRADINGGROUP - \"" + name +
                  "\": " + std::to_string(group.gradingMembers.size()) +
                  " member(s). Use VOLUMES <ground surface>, " + name +
                  " for cut/fill/net.");
    for (const auto &member : group.gradingMembers) {
      const int ix = FindEntityIndexById(st.drawing.featureLineAttrs, member.gradingId);
      const std::string label =
          ix >= 0 && static_cast<size_t>(ix) < st.drawing.featureLineInfo.size()
              ? st.drawing.featureLineInfo[static_cast<size_t>(ix)].name
              : "missing grading";
      log.push_back("  " + label + " (id " + std::to_string(member.gradingId) +
                    ")");
    }
    return;
  }
  if (verb == "remove" && existing < 0) {
    log.push_back("GRADINGGROUP - no group named \"" + name + "\".");
    return;
  }
  EnsureEntityIds(st);
  std::vector<std::uint64_t> ids;
  for (const auto &e : st.selection) {
    if (e.type != SelectedEntity::Type::FeatureLine || e.index < 0 ||
        static_cast<size_t>(e.index) >= st.drawing.featureLineAttrs.size()) {
      log.push_back("GRADINGGROUP - select only grading results.");
      return;
    }
    const auto id = st.drawing.featureLineAttrs[static_cast<size_t>(e.index)].id;
    if (std::find(ids.begin(), ids.end(), id) == ids.end())
      ids.push_back(id);
  }
  if (ids.empty() && (verb == "remove" || existing >= 0)) {
    log.push_back("GRADINGGROUP - select grading results to add or remove; use "
                  "List to inspect the group.");
    return;
  }
  auto candidates = st.drawing.cadSurfaces;
  const size_t target =
      existing < 0 ? candidates.size() : static_cast<size_t>(existing);
  if (existing < 0) {
    CadSurface group;
    group.name = name;
    group.gradingGroup = true;
    candidates.push_back(std::move(group));
  }
  std::vector<size_t> changed{target};
  for (const auto id : ids) {
    CadGradingGroupMember snapshot;
    if (verb == "add") {
      bool foundBaked = false;
      for (const auto& source : candidates) {
        if (!source.gradingGroup) continue;
        const auto member = std::find_if(source.gradingMembers.begin(), source.gradingMembers.end(),
                                        [id](const auto& m) { return m.gradingId == id; });
        if (member != source.gradingMembers.end() && !member->infill) {
          snapshot = *member;
          foundBaked = true;
          break;
        }
      }
      if (!foundBaked && !SnapshotGradingMember(st, id, snapshot, log)) return;
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
      auto &group = candidates[i];
      if (!group.gradingGroup || (verb == "remove" && i != target))
        continue;
      const auto end = std::remove_if(
          group.gradingMembers.begin(), group.gradingMembers.end(),
          [id](const auto &m) { return m.gradingId == id; });
      if (end != group.gradingMembers.end()) {
        group.gradingMembers.erase(end, group.gradingMembers.end());
        if (std::find(changed.begin(), changed.end(), i) == changed.end())
          changed.push_back(i);
      }
    }
    if (verb == "add")
      candidates[target].gradingMembers.push_back(std::move(snapshot));
  }
  for (const auto i : changed) {
    if (!BuildSurfaceFromSources(st, candidates[i], log)) {
      log.push_back("GRADINGGROUP - nothing changed; the group surface could "
                    "not be built.");
      return;
    }
  }
  PushUndoSnapshot(st, "Grading group membership");
  st.drawing.cadSurfaces = std::move(candidates);
  if (existing < 0)
    st.drawing.cadSurfaceAttrs.push_back(
        MakeNewObjectAttrs(st, ObjectLayerKind::Surface, name));
  BumpCadGpuCache(st);
  for (const auto i : changed) {
    st.drawing.cadSurfaces[i].builtAtRevision = st.cadGpuRevision;
    MarkVolumeSurfacesDirtyForParent(st, st.drawing.cadSurfaces[i].name);
  }
  log.push_back("GRADINGGROUP - \"" + name + "\": " +
                std::to_string(st.drawing.cadSurfaces[target].gradingMembers.size()) +
                " member(s). Use VOLUMES <ground surface>, " + name +
                " for cut/fill/net.");
}

void ExecuteGradingInfillCommand(AppCommandState& st, const std::string& name,
                                 std::vector<std::string>& log) {
  if (name.empty()) {
    log.push_back("GRADINGINFILL - usage: GRADINGINFILL <group>. Select closed feature lines.");
    return;
  }
  int existing = FindSurfaceIndex(st, name);
  if (existing >= 0 && !st.drawing.cadSurfaces[static_cast<size_t>(existing)].gradingGroup) {
    log.push_back("GRADINGINFILL - that name belongs to an ordinary surface.");
    return;
  }
  EnsureEntityIds(st);
  std::vector<CadGradingGroupMember> infills;
  for (const auto& selected : st.selection) {
    if (selected.type != SelectedEntity::Type::FeatureLine || selected.index < 0 ||
        static_cast<size_t>(selected.index) >= st.drawing.featureLineAttrs.size() ||
        static_cast<size_t>(selected.index) >= st.drawing.featureLineClosed.size() ||
        !st.drawing.featureLineClosed[static_cast<size_t>(selected.index)]) {
      log.push_back("GRADINGINFILL - select only closed feature lines.");
      return;
    }
    const size_t ix = static_cast<size_t>(selected.index);
    const int first = st.drawing.featureLineOffsets[ix], last = st.drawing.featureLineOffsets[ix + 1];
    if (last - first < 3) {
      log.push_back("GRADINGINFILL - a boundary needs at least three vertices.");
      return;
    }
    CadGradingGroupMember member;
    member.gradingId = st.drawing.featureLineAttrs[ix].id;
    member.infill = true;
    std::vector<double> ring;
    for (int v = first; v < last; ++v) {
      const size_t p = static_cast<size_t>(v) * 3;
      const double x = st.drawing.featureLineVerts[p], y = st.drawing.featureLineVerts[p + 1], z = st.drawing.featureLineVerts[p + 2];
      if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        log.push_back("GRADINGINFILL - boundary contains a nonfinite coordinate."); return;
      }
      ring.insert(ring.end(), {x, y, z});
    }
    std::string polygonError;
    if (!IsSimpleInfillPolygon(ring, &polygonError)) {
      log.push_back("GRADINGINFILL - " + polygonError + "."); return;
    }
    member.strips = std::make_shared<const std::vector<std::vector<double>>>(
        std::vector<std::vector<double>>{std::move(ring)});
    infills.push_back(std::move(member));
  }
  if (infills.empty()) {
    log.push_back("GRADINGINFILL - select closed feature lines first."); return;
  }
  auto candidates = st.drawing.cadSurfaces;
  const size_t target = existing < 0 ? candidates.size() : static_cast<size_t>(existing);
  if (existing < 0) { CadSurface group; group.name = name; group.gradingGroup = true; candidates.push_back(std::move(group)); }
  std::vector<size_t> changed;
  for (size_t i = 0; i < candidates.size(); ++i) {
    auto& group = candidates[i];
    if (!group.gradingGroup) continue;
    const auto oldSize = group.gradingMembers.size();
    for (const auto& infill : infills)
      group.gradingMembers.erase(std::remove_if(group.gradingMembers.begin(), group.gradingMembers.end(),
        [&](const auto& m) { return m.gradingId == infill.gradingId; }), group.gradingMembers.end());
    if (group.gradingMembers.size() != oldSize) changed.push_back(i);
  }
  for (auto& infill : infills) candidates[target].gradingMembers.push_back(std::move(infill));
  if (std::find(changed.begin(), changed.end(), target) == changed.end()) changed.push_back(target);
  for (size_t i : changed) {
    if (!BuildSurfaceFromSources(st, candidates[i], log)) {
      log.push_back("GRADINGINFILL - group surface could not be built; nothing changed."); return;
    }
  }
  PushUndoSnapshot(st, "Grading group infill");
  st.drawing.cadSurfaces = std::move(candidates);
  if (existing < 0) st.drawing.cadSurfaceAttrs.push_back(MakeNewObjectAttrs(st, ObjectLayerKind::Surface, name));
  BumpCadGpuCache(st);
  for (size_t i : changed) { st.drawing.cadSurfaces[i].builtAtRevision = st.cadGpuRevision; MarkVolumeSurfacesDirtyForParent(st, st.drawing.cadSurfaces[i].name); }
  log.push_back("GRADINGINFILL - added/refreshed " + std::to_string(infills.size()) + " boundary member(s) in \"" + name + "\".");
}

void StartGradingCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  auto sess = std::make_unique<stepengine::GradingSession>();
  auto* gs = sess.get();
  stepengine::BeginSession(st, std::move(sess));
  st.gradingSideChosen = false;
  st.gradingTarget = CadGradingDefinition::Target::Surface;
  st.gradingTargetValue = 0.0;
  st.gradingDistanceGradePercent = 0.0;

  log.push_back("GRADING - groups: type Group <name>[, Add/Remove/List] for "
                "selected grading results. Type T for Surface, Distance, Absolute, or Relative targets; "
                "use GRADINGINFILL <group> on a selected closed feature line to fill its interior.");

  if (st.drawing.featureLineOffsets.size() < 2) {
    log.push_back("GRADING - the drawing has no feature lines. Draw one with FEATURELINE first: it "
                  "is the design edge the slopes come off.");
    stepengine::EndSession(st);
    return;
  }
  if (st.drawing.cadSurfaces.empty())
    log.push_back("GRADING - the drawing has no surfaces. Type T to choose Distance, Absolute, or Relative elevation, or build a surface for Surface grading.");
  int n = 0;
  const std::uint64_t id = GradingSelectedFeatureLine(st, &n);
  if (n == 1) {
    if (!GradingTakeFeatureLine(st, id, log)) {
      stepengine::EndSession(st);
      return;
    }
    GradingPrompt(st, log);
    return;
  }
  gs->phase = stepengine::GradingPhase::WaitBaseline;
  log.push_back(n > 1 ? "GRADING - select ONE feature line as the baseline. ESC cancels."
                      : "GRADING - select the feature line to grade from. ESC cancels.");
}

void ProcessGradingCommandLine(AppCommandState& st, const std::string& line,
                               std::vector<std::string>& log) {
  auto* gs = st.sessionAs<stepengine::GradingSession>();
  if (!gs)
    return;
  using P = stepengine::GradingPhase;
  const std::string tr = StringUtil::trimCopy(line);
  const std::string low = StringUtil::toLowerAsciiCopy(tr);

  if ((gs->phase == P::WaitBaseline || gs->phase == P::WaitOptions) &&
      (low == "group" || low == "g" || low.rfind("group ", 0) == 0 ||
       low.rfind("g ", 0) == 0)) {
    const size_t space = tr.find(' ');
    if (space == std::string::npos) {
      log.push_back("GRADING - type Group <name>[, Add/Remove/List] for "
                    "selected grading results.");
      return;
    }
    stepengine::EndSession(st);
    ExecuteGradingGroupCommand(st, StringUtil::trimCopy(tr.substr(space + 1)),
                               log);
    return;
  }

  switch (gs->phase) {
    case P::WaitTargetKind:
      if (low == "surface" || low == "s") st.gradingTarget = CadGradingDefinition::Target::Surface;
      else if (low == "distance" || low == "d") st.gradingTarget = CadGradingDefinition::Target::Distance;
      else if (low == "absolute" || low == "a") st.gradingTarget = CadGradingDefinition::Target::AbsoluteElevation;
      else if (low == "relative" || low == "r") st.gradingTarget = CadGradingDefinition::Target::RelativeElevation;
      else { log.push_back("GRADING - target must be Surface, Distance, Absolute, or Relative."); return; }
      if (st.gradingTarget == CadGradingDefinition::Target::Surface) { GradingPrompt(st, log); return; }
      gs->phase = P::WaitTargetValue;
      if (st.gradingTarget == CadGradingDefinition::Target::Distance)
        log.push_back("GRADING - enter horizontal distance (> 0).");
      else if (st.gradingTarget == CadGradingDefinition::Target::AbsoluteElevation)
        log.push_back("GRADING - enter absolute target elevation (finite).");
      else
        log.push_back("GRADING - enter target elevation change from the baseline (finite; signed values allowed).");
      return;
    case P::WaitTargetValue: {
      char* end = nullptr;
      const double value = std::strtod(tr.c_str(), &end);
      if (tr.empty() || end != tr.c_str() + tr.size() || !std::isfinite(value) ||
          (st.gradingTarget == CadGradingDefinition::Target::Distance && value <= 0.0)) {
        log.push_back("GRADING - enter a finite value" + std::string(st.gradingTarget == CadGradingDefinition::Target::Distance ? " greater than zero." : "."));
        return;
      }
      st.gradingTargetValue = value;
      if (st.gradingTarget == CadGradingDefinition::Target::Distance) {
        gs->phase = P::WaitDistanceGrade;
        log.push_back("GRADING - enter signed grade percent (for example -2). Zero is valid.");
      } else GradingPrompt(st, log);
      return;
    }
    case P::WaitDistanceGrade: {
      char* end = nullptr;
      const double value = std::strtod(tr.c_str(), &end);
      if (tr.empty() || end != tr.c_str() + tr.size() || !std::isfinite(value)) {
        log.push_back("GRADING - enter a finite signed grade percent."); return;
      }
      st.gradingDistanceGradePercent = value;
      GradingPrompt(st, log);
      return;
    }
    case P::WaitCutSlope:
    case P::WaitFillSlope: {
      const bool isCut = gs->phase == P::WaitCutSlope;
      if (tr.empty()) {
        GradingPrompt(st, log);
        return;
      }
      double run = 0.0;
      if (!ParseSlopeRun(tr, &run)) {
        log.push_back("GRADING - enter a slope as run:rise (3:1), a bare run (3), or a percent "
                      "(33%).");
        return;
      }
      if (isCut)
        st.gradingCutRun = run;
      else
        st.gradingFillRun = run;
      GradingPrompt(st, log);
      return;
    }
    case P::WaitSurfaceName: {
      if (tr.empty()) {
        GradingPrompt(st, log);
        return;
      }
      if (FindSurfaceIndex(st, tr) < 0) {
        std::string names;
        for (const CadSurface& s : st.drawing.cadSurfaces)
          names += (names.empty() ? "" : ", ") + s.name;
        log.push_back("GRADING - no surface named \"" + tr + "\"." +
                      (names.empty() ? "" : " Surfaces: " + names + "."));
        return;
      }
      st.gradingSurface = tr;
      st.gradingTarget = CadGradingDefinition::Target::Surface;
      GradingPrompt(st, log);
      return;
    }
    case P::WaitSide: {
      if (low == "l" || low == "left") {
        st.gradingSideLeft = true;
        st.gradingSideChosen = true;
      } else if (low == "r" || low == "right") {
        st.gradingSideLeft = false;
        st.gradingSideChosen = true;
      } else if (!tr.empty()) {
        log.push_back("GRADING - type L for Left or R for Right, looking along the baseline.");
        return;
      }
      GradingPrompt(st, log);
      return;
    }
    case P::WaitOptions: {
      if (tr.empty()) {
        BuildGradingDaylight(st, log);
        return;
      }
      if (low == "c" || low == "cut") {
        gs->phase = P::WaitCutSlope;
        log.push_back("GRADING - enter cut slope (run:rise, a bare run, or a percent) <" +
                      FormatSlope(st.gradingCutRun) + ">. Cut is where the design sits BELOW ground.");
        return;
      }
      if (low == "f" || low == "fill") {
        gs->phase = P::WaitFillSlope;
        log.push_back("GRADING - enter fill slope (run:rise, a bare run, or a percent) <" +
                      FormatSlope(st.gradingFillRun) + ">. Fill is where it sits ABOVE ground.");
        return;
      }
      if (low == "s" || low == "surface") {
        std::string names;
        for (const CadSurface& s : st.drawing.cadSurfaces)
          names += (names.empty() ? "" : ", ") + s.name;
        gs->phase = P::WaitSurfaceName;
        log.push_back(names.empty() ? "GRADING - the drawing has no surfaces to grade to."
                                    : "GRADING - enter surface name. Surfaces: " + names + ".");
        return;
      }
      if (low == "t" || low == "target") {
        gs->phase = P::WaitTargetKind;
        log.push_back("GRADING - target [Surface/Distance/Absolute/Relative].");
        return;
      }
      if (low == "p" || low == "percent") {
        gs->phase = P::WaitDistanceGrade;
        log.push_back("GRADING - enter signed grade percent (for example -2). Zero is valid.");
        return;
      }
      if (low == "d" || low == "side") {
        if (!GradingBaselineIsOpen(st)) {
          log.push_back("GRADING - this baseline is closed, so outward is away from its interior "
                        "and there is no side to choose.");
          return;
        }
        gs->phase = P::WaitSide;
        log.push_back("GRADING - grade to which side, [L]eft or [R]ight, looking along the baseline?");
        return;
      }
      double run = 0.0;
      if (ParseSlopeRun(tr, &run)) {
        st.gradingFillRun = run;
        GradingPrompt(st, log);
        return;
      }
      log.push_back("GRADING - press Enter to grade, or C, F, S, T" +
                    std::string(GradingBaselineIsOpen(st) ? " or D" : "") + " to change a setting.");
      return;
    }
    case P::WaitBaseline: {
      if (tr.empty())
        log.push_back("GRADING - still waiting for a baseline: click a feature line. ESC cancels.");
      else
        log.push_back("GRADING - \"" + tr +
                      "\" was typed while GRADING is waiting for you to CLICK a feature line. Pick "
                      "one, or press ESC to cancel.");
      return;
    }
    default:
      return;
  }
}

void HandleGradingPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  SelectedEntity hit{};
  float d2 = 0.f;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, &d2) ||
      hit.type != SelectedEntity::Type::FeatureLine) {
    log.push_back("GRADING - that is not a feature line. Pick the design edge to grade from.");
    return;
  }
  if (hit.index < 0 || static_cast<size_t>(hit.index) >= st.drawing.featureLineAttrs.size())
    return;
  EnsureEntityIds(st);
  ClearCadSelection(st);
  st.selection.push_back(hit);
  if (!GradingTakeFeatureLine(st, st.drawing.featureLineAttrs[static_cast<size_t>(hit.index)].id, log)) {
    stepengine::EndSession(st);
    return;
  }
  GradingPrompt(st, log);
}
