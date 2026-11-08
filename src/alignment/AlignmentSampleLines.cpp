#include "alignment/AlignmentSampleLines.hpp"

#include "alignment/AlignmentViewFrames.hpp"
#include "util/surfacequery.hpp"
#include "util/surfacevolume.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace alignment {

std::string SampleLineSettingsRefusal(const Alignment& a, const SampleLineSettings& s) {
  if (!std::isfinite(s.startStation) || !std::isfinite(s.endStation)) return "a station is not a number";
  if (!(s.interval > 0.0) || !std::isfinite(s.interval)) return "the interval must be a positive number";
  if (!(s.leftWidth >= 0.0) || !(s.rightWidth >= 0.0) || !std::isfinite(s.leftWidth) || !std::isfinite(s.rightWidth))
    return "a swath width must be zero or more";
  if (s.leftWidth <= 0.0 && s.rightWidth <= 0.0) return "at least one swath width must be above zero";
  if (s.endStation < s.startStation) return "the end station is before the start station";
  if (!(a.Length() > 0.0)) return "the alignment has no length";
  return {};
}

bool ComputeSampleLines(const Alignment& a, const SampleLineSettings& s, const StationFormat& fmt,
                        std::vector<SampleLine>* out, std::string* why) {
  out->clear();
  const std::string refusal = SampleLineSettingsRefusal(a, s);
  if (!refusal.empty()) {
    if (why != nullptr) *why = refusal;
    return false;
  }
  std::string reason;
  double startInternal = 0.0, endInternal = 0.0;
  if (!ResolveDisplayedStation(a, s.startStation, &startInternal, &reason)) {
    if (why != nullptr) *why = "the start station: " + reason;
    return false;
  }
  if (!ResolveDisplayedStation(a, s.endStation, &endInternal, &reason)) {
    if (why != nullptr) *why = "the end station: " + reason;
    return false;
  }
  if (endInternal < startInternal) {
    if (why != nullptr) *why = "the end station is before the start station";
    return false;
  }
  // REQ-519.4: the range must lie on the alignment, as the view frames require (REQ-517.4).
  constexpr double kRangeEps = 1e-9;
  if (startInternal < a.StartStation() - kRangeEps || endInternal > a.EndStation() + kRangeEps) {
    if (why != nullptr) {
      char range[160];
      std::snprintf(range, sizeof(range), "the station range is outside the alignment (%.3f to %.3f)", a.StartStation(),
                    a.EndStation());
      *why = range;
    }
    return false;
  }
  // REQ-519.10: one line at each interval station, starting at the start and never past the end.
  const double countReal = std::floor((endInternal - startInternal) / s.interval + 1e-9) + 1.0;
  if (countReal > kMaxSampleLinesPerRun) {
    if (why != nullptr) {
      std::ostringstream text;
      text << "the interval would make " << std::fixed << std::setprecision(0) << countReal << " sample lines; the limit is "
           << kMaxSampleLinesPerRun << ". Use a longer interval or a shorter range";
      *why = text.str();
    }
    return false;
  }
  const int count = static_cast<int>(countReal);
  out->reserve(static_cast<size_t>(count));
  std::vector<std::string> names;
  names.reserve(static_cast<size_t>(count));
  for (int k = 0; k < count; ++k) {
    const double station = startInternal + static_cast<double>(k) * s.interval;
    SampleLine line;
    line.internalStation = station;
    line.origin = a.GetPointAtStation(station);
    const double azimuth = a.GetBearingAtStation(station);  // radians clockwise from north (REQ-484)
    line.direction = {std::cos(azimuth), -std::sin(azimuth)};  // right of travel, as GetPointFromStationOffset
    line.leftWidth = s.leftWidth;
    line.rightWidth = s.rightWidth;
    // Whole displayed stations keep their whole name (0+50); a fractional one shows the drawing's decimals.
    const double displayed = a.DisplayedStation(station, true);
    StationFormat name = fmt;
    if (std::fabs(displayed - std::round(displayed)) < 1e-6) name.decimals = 0;
    line.name = FormatStation(displayed, name);
    names.push_back(line.name);
    out->push_back(std::move(line));
  }
  // Two lines with one name could not be told apart, so a fine interval that the station format cannot show is refused.
  std::vector<std::string> sorted = names;
  std::sort(sorted.begin(), sorted.end());
  if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
    if (why != nullptr) *why = "the interval is too fine for the station format: some lines would share a name";
    out->clear();
    return false;
  }
  return true;
}

void SampleSection(const std::vector<double>& verts, const std::vector<std::uint32_t>& idx, const ISurfaceQuery& surface,
                   SampleLine& line) {
  const TinSpatialIndex index = BuildTinSpatialIndex(verts, idx);
  SampleSection(verts, idx, index, surface, line);
}

namespace {

// The triangle ordinals in the cells the box from (x0, y0) to (x1, y1) touches, each once. A triangle that crosses the
// box has its own box in one of these cells, because the index stores a triangle in every cell its box touches.
std::vector<std::uint32_t> CandidateTriangles(const TinSpatialIndex& index, double x0, double y0, double x1, double y1) {
  std::vector<std::uint32_t> out;
  if (index.empty()) return out;
  // Clamp in floating point before the cast, so a station far off the TIN cannot overflow the cell index.
  const auto colOf = [&index](double x) {
    return static_cast<int>(std::clamp((x - index.minX) / index.cellSize, 0.0, index.cols - 1.0));
  };
  const auto rowOf = [&index](double y) {
    return static_cast<int>(std::clamp((y - index.minY) / index.cellSize, 0.0, index.rows - 1.0));
  };
  const int c0 = colOf(std::min(x0, x1)), c1 = colOf(std::max(x0, x1));
  const int r0 = rowOf(std::min(y0, y1)), r1 = rowOf(std::max(y0, y1));
  for (int r = r0; r <= r1; ++r)
    for (int c = c0; c <= c1; ++c) {
      const auto& cell = index.cells[static_cast<size_t>(r) * static_cast<size_t>(index.cols) + static_cast<size_t>(c)];
      out.insert(out.end(), cell.begin(), cell.end());
    }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

}  // namespace

void SampleSection(const std::vector<double>& verts, const std::vector<std::uint32_t>& idx, const TinSpatialIndex& index,
                   const ISurfaceQuery& surface, SampleLine& line) {
  line.section.clear();
  const double left = line.leftWidth, right = line.rightWidth;
  const double ox = line.origin.x, oy = line.origin.y;
  const double nx = line.direction.x, ny = line.direction.y;

  // A candidate offset along the swath. A crossing of a triangle edge carries the ground interpolated along that edge:
  // it lies on the TIN, so it is covered and its height is exact. The origin and the swath ends have no such edge and
  // are asked of the surface.
  struct Candidate {
    double t = 0.0;
    bool hasGround = false;
    double ground = 0.0;
  };
  std::vector<Candidate> candidates{{-left, false, 0.0}, {0.0, false, 0.0}, {right, false, 0.0}};
  const std::vector<std::uint32_t> nearby = CandidateTriangles(index, ox - left * nx, oy - left * ny, ox + right * nx,
                                                              oy + right * ny);
  for (const std::uint32_t t : nearby) {
    if (3 * static_cast<size_t>(t) + 2 >= idx.size()) continue;
    for (size_t e = 0; e < 3; ++e) {
      const std::uint32_t ia = idx[3 * static_cast<size_t>(t) + e];
      const std::uint32_t ib = idx[3 * static_cast<size_t>(t) + (e + 1) % 3];
      if (3 * static_cast<size_t>(ia) + 2 >= verts.size() || 3 * static_cast<size_t>(ib) + 2 >= verts.size()) continue;
      const double ax = verts[3 * static_cast<size_t>(ia)], ay = verts[3 * static_cast<size_t>(ia) + 1];
      const double bx = verts[3 * static_cast<size_t>(ib)], by = verts[3 * static_cast<size_t>(ib) + 1];
      const double dx = bx - ax, dy = by - ay;
      // Solve O + t*N = A + s*D. Parallel edges (denominator 0) never cross the swath at a single point.
      const double denom = nx * dy - ny * dx;
      if (std::fabs(denom) < 1e-12) continue;
      const double aox = ax - ox, aoy = ay - oy;
      const double tt = (aox * dy - aoy * dx) / denom;
      const double s = (aox * ny - aoy * nx) / denom;
      if (s < -1e-9 || s > 1.0 + 1e-9) continue;
      if (tt < -left - 1e-9 || tt > right + 1e-9) continue;
      const double sc = std::clamp(s, 0.0, 1.0);
      const double za = verts[3 * static_cast<size_t>(ia) + 2], zb = verts[3 * static_cast<size_t>(ib) + 2];
      candidates.push_back({std::clamp(tt, -left, right), true, za + sc * (zb - za)});
    }
  }

  // Sort by offset, then keep one candidate per distinct offset, preferring one that carries its ground.
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& p, const Candidate& q) { return p.t < q.t; });
  for (size_t i = 0; i < candidates.size();) {
    size_t j = i + 1;
    while (j < candidates.size() && candidates[j].t - candidates[i].t < 1e-7) ++j;
    const Candidate* pick = &candidates[i];
    for (size_t k = i; k < j; ++k)
      if (candidates[k].hasGround) {
        pick = &candidates[k];
        break;
      }
    if (pick->hasGround) {
      line.section.push_back({pick->t, pick->ground});
    } else {
      double z = 0.0;
      if (surface.elevationAt(ox + pick->t * nx, oy + pick->t * ny, &z)) line.section.push_back({pick->t, z});
    }
    i = j;
  }

  // REQ-519.2: where the swath leaves coverage, the points either side stay and are marked as a break. Each stretch
  // between two kept points is checked at its midpoint: a hole in the surface lies inside such a stretch.
  for (size_t p = 0; p + 1 < line.section.size(); ++p) {
    const double mid = 0.5 * (line.section[p].offset + line.section[p + 1].offset);
    double z = 0.0;
    line.section[p].gapAfter = !surface.elevationAt(ox + mid * nx, oy + mid * ny, &z);
  }
}

void SyncSampleLines(const std::vector<Alignment>& alignments, std::vector<SampleLine>& lines) {
  lines.erase(std::remove_if(lines.begin(), lines.end(),
                             [&alignments](const SampleLine& line) {
                               return std::none_of(alignments.begin(), alignments.end(),
                                                   [&line](const Alignment& a) { return a.id == line.alignmentId; });
                             }),
              lines.end());
}

double NiceFeetPerInchAtLeast(double required) {
  if (!(required > 1.0)) return 1.0;
  for (double decade = 1.0; decade < 1e12; decade *= 10.0)
    for (double m : {1.0, 2.0, 5.0})
      if (m * decade >= required) return m * decade;
  return required;
}

SectionScale FitSectionScale(bool anyPoint, double maxOffset, double maxDeviation, double halfColumnIn, double halfPlotIn) {
  SectionScale scale;
  if (!anyPoint) return scale;
  // A tiny title block still gets a finite scale: the room is never less than 0.05 in.
  scale.feetPerInchAcross = NiceFeetPerInchAtLeast(maxOffset / std::max(halfColumnIn, 0.05));
  scale.feetPerInchDown = NiceFeetPerInchAtLeast(maxDeviation / std::max(halfPlotIn, 0.05));
  return scale;
}

}  // namespace alignment
