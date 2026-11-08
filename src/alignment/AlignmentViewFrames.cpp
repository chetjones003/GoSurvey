#include "alignment/AlignmentViewFrames.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace alignment {
namespace {

// Point and unit direction at any internal station. Stations past either end follow the end's tangent in a straight
// line, so the last frame of a group keeps its full length beyond the alignment (REQ-517.5).
struct PlanPose {
  Point2 point;
  Point2 dir;  ///< Unit direction of travel.
};

PlanPose PoseAt(const Alignment& a, double station) {
  const double start = a.StartStation();
  const double end = a.EndStation();
  if (station > end) {
    const Point2 dir = a.GetTangentAtStation(end);
    const Point2 p = a.EndPoint();
    const double d = station - end;
    return {{p.x + dir.x * d, p.y + dir.y * d}, dir};
  }
  if (station < start) {
    const Point2 dir = a.GetTangentAtStation(start);
    const Point2 p = a.StartPoint();
    const double d = station - start;
    return {{p.x + dir.x * d, p.y + dir.y * d}, dir};
  }
  return {a.GetPointAtStation(station), a.GetTangentAtStation(station)};
}

// Across-alignment offset range for a placement, right positive (REQ-517.4).
void AcrossRange(ViewFramePlacement p, double width, double* nearOff, double* farOff) {
  switch (p) {
    case ViewFramePlacement::Left:  *nearOff = 0.0;        *farOff = width;      break;
    case ViewFramePlacement::Right: *nearOff = -width;     *farOff = 0.0;        break;
    case ViewFramePlacement::Centred:
    default:                        *nearOff = -width / 2; *farOff = width / 2;  break;
  }
}

double FrameLength(const ViewFrameSettings& s) { return s.sheetWidthIn * s.groundPerInch; }
double FrameWidth(const ViewFrameSettings& s) { return s.sheetHeightIn * s.groundPerInch; }

}  // namespace

std::string ViewFrameSettingsRefusal(const Alignment& a, const ViewFrameSettings& s) {
  char buf[256];
  if (a.Empty())
    return "the alignment has no geometry";
  if (!std::isfinite(s.startStation) || !std::isfinite(s.endStation) || !std::isfinite(s.sheetWidthIn) ||
      !std::isfinite(s.sheetHeightIn) || !std::isfinite(s.groundPerInch) || !std::isfinite(s.overlap))
    return "a value is not a finite number";
  if (!(s.endStation > s.startStation))
    return "the station range is empty or reversed";
  if (s.startStation < a.StartStation() || s.endStation > a.EndStation()) {
    std::snprintf(buf, sizeof(buf), "the station range is outside the alignment (%.3f to %.3f)", a.StartStation(),
                  a.EndStation());
    return buf;
  }
  if (!(s.sheetWidthIn > 0.0) || !(s.sheetHeightIn > 0.0))
    return "the sheet size is not known";
  if (!(s.groundPerInch > 0.0) || !std::isfinite(s.groundPerInch))
    return "the scale is not known";
  if (!(s.overlap >= 0.0) || !std::isfinite(s.overlap))
    return "the overlap must be zero or more";
  if (FrameLength(s) > kMaxFrameExtent || FrameWidth(s) > kMaxFrameExtent)
    return "the frame would be larger than " + std::to_string(static_cast<long long>(kMaxFrameExtent)) + " units";
  if (s.overlap >= FrameLength(s))
    return "the overlap is not less than the frame length";
  // The first frame covers one frame length; each further frame adds one step (REQ-517.5).
  const double length = FrameLength(s);
  const double step = length - s.overlap;
  const double span = s.endStation - s.startStation;
  const double frames = span <= length ? 1.0 : 1.0 + std::ceil((span - length) / step);
  if (frames > kMaxViewFramesPerGroup)
    return "the range would make more than " + std::to_string(kMaxViewFramesPerGroup) + " frames";
  return {};
}

bool ResolveDisplayedStation(const Alignment& a, double displayed, double* internal, std::string* why) {
  if (a.equations.empty()) {
    *internal = displayed;
    return true;
  }
  // Within each piece between equation points, displayed = internal + c. Collect the pieces' cut points first.
  std::vector<double> cuts{a.StartStation(), a.EndStation()};
  for (const StationEquation& q : a.equations) {
    if (q.rawBack > a.StartStation() && q.rawBack < a.EndStation()) cuts.push_back(q.rawBack);
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
  constexpr double kEps = 1e-9;
  std::vector<double> matches;
  for (size_t i = 0; i + 1 < cuts.size(); ++i) {
    const double lo = cuts[i];
    const double hi = cuts[i + 1];
    const double c = a.DisplayedStation(0.5 * (lo + hi), true) - 0.5 * (lo + hi);
    const double dLo = lo + c;
    const double dHi = hi + c;
    if (displayed >= std::min(dLo, dHi) - kEps && displayed <= std::max(dLo, dHi) + kEps) {
      const double candidate = displayed - c;
      bool seen = false;
      for (double m : matches) seen = seen || std::fabs(m - candidate) <= kEps;
      if (!seen) matches.push_back(candidate);
    }
  }
  // A station equation sitting exactly on a boundary, including at either end, has no interval on one side. Test each
  // boundary point directly: its displayed station before and after the equation both name it.
  for (double x : cuts) {
    if (std::fabs(a.DisplayedStation(x, true) - displayed) > kEps && std::fabs(a.DisplayedStation(x, false) - displayed) > kEps)
      continue;
    bool seen = false;
    for (double m : matches) seen = seen || std::fabs(m - x) <= kEps;
    if (!seen) matches.push_back(x);
  }
  if (matches.empty()) {
    if (why != nullptr) *why = "a station is not on the alignment (it falls in a gap made by a station equation)";
    return false;
  }
  if (matches.size() > 1) {
    if (why != nullptr) *why = "a station is ambiguous (a station equation makes it occur more than once)";
    return false;
  }
  *internal = matches[0];
  return true;
}

std::vector<ViewFrame> ComputeViewFrames(const Alignment& a, const ViewFrameSettings& s) {
  std::vector<ViewFrame> out;
  if (!ViewFrameSettingsRefusal(a, s).empty())
    return out;
  const double length = FrameLength(s);
  const double width = FrameWidth(s);
  const double step = length - s.overlap;
  double nearOff = 0.0;
  double farOff = 0.0;
  AcrossRange(s.placement, width, &nearOff, &farOff);
  // Frame i starts at start + i * step, so no rounding drift builds up across a long alignment (REQ-517.5).
  for (int i = 0;; ++i) {
    ViewFrame f;
    f.name = "VF-" + std::to_string(i + 1);
    f.startStation = s.startStation + i * step;
    f.endStation = f.startStation + length;
    f.length = length;
    f.width = width;
    f.centreStation = f.startStation + length / 2.0;
    const PlanPose c = PoseAt(a, f.centreStation);
    // Right of travel, for direction (dx, dy) in x-east, y-north: (dy, -dx). A straight rectangle, oriented on
    // the centre station's tangent (REQ-517.4).
    const Point2 right{c.dir.y, -c.dir.x};
    f.centre = c.point;
    auto at = [&](double along, double across) {
      return Point2{c.point.x + c.dir.x * along + right.x * across, c.point.y + c.dir.y * along + right.y * across};
    };
    const double half = length / 2.0;
    f.corner[0] = at(-half, nearOff);
    f.corner[1] = at(half, nearOff);
    f.corner[2] = at(half, farOff);
    f.corner[3] = at(-half, farOff);
    out.push_back(std::move(f));
    if (f.endStation >= s.endStation)
      break;
  }
  return out;
}

std::string RegenerateViewFrameGroup(const Alignment& a, ViewFrameGroup& group) {
  std::string why = ViewFrameSettingsRefusal(a, group.settings);
  if (!why.empty()) {
    // Frames are derived and never saved, so a group that no longer fits shows none: memory then matches a reopened file.
    group.frames.clear();
    return why;
  }
  group.frames = ComputeViewFrames(a, group.settings);
  return {};
}

void SyncViewFrameGroups(const std::vector<Alignment>& alignments, std::vector<ViewFrameGroup>& groups,
                         std::vector<std::string>* notes) {
  std::vector<ViewFrameGroup> kept;
  kept.reserve(groups.size());
  for (ViewFrameGroup& g : groups) {
    const Alignment* owner = nullptr;
    for (const Alignment& a : alignments) {
      if (a.id == g.alignmentId) {
        owner = &a;
        break;
      }
    }
    if (owner == nullptr)
      continue;  // REQ-517.9: deleting the alignment deletes its groups
    const std::string why = RegenerateViewFrameGroup(*owner, g);
    if (!why.empty() && notes != nullptr)
      notes->push_back("VIEWFRAMES - group \"" + g.name + "\" shows no frames: " + why + ".");
    kept.push_back(std::move(g));
  }
  groups = std::move(kept);
}

}  // namespace alignment
