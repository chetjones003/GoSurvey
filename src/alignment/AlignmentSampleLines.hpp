#pragma once

#include "alignment/Alignment.hpp"
#include "util/surfacevolume.hpp"  // TinSpatialIndex

#include <cstdint>
#include <string>
#include <vector>

class ISurfaceQuery;

namespace alignment {

/// One point of a section (REQ-519.2): the plan offset from the alignment (right positive, REQ-484.2) and the ground
/// elevation there.
struct SectionPoint {
  double offset = 0.0;
  double ground = 0.0;
  /// True when the swath leaves the surface's coverage between this point and the next one. The ground line is not
  /// drawn across that uncovered stretch (REQ-519.2).
  bool gapAfter = false;
};

/// A sample line (REQ-519.1, .2). Stored with the drawing and kept in plan coordinates, so it does not move when the
/// alignment is edited (REQ-519.13). The section is sampled once, when the line is made.
struct SampleLine {
  std::uint64_t id = 0;
  std::uint64_t alignmentId = 0;  ///< The alignment the line is placed on.
  std::string name;               ///< The displayed station, for example 0+50.
  double internalStation = 0.0;   ///< Internal station along the alignment (REQ-483).
  Point2 origin;                  ///< The alignment point at the station, drawing-local.
  Point2 direction;               ///< Unit vector to the right of travel; positive offsets run this way.
  double leftWidth = 0.0;         ///< Swath to the left of the alignment, drawing units.
  double rightWidth = 0.0;        ///< Swath to the right of the alignment, drawing units.
  bool hasSection = false;        ///< False when no surface was chosen, so the line has no section.
  std::string surfaceName;        ///< The TIN the section was sampled from (when hasSection).
  std::vector<SectionPoint> section;  ///< Covered points in offset order. Empty when the surface does not cover the line.
};

/// What the typed command or the dialog asks for (REQ-519.1). Stations are displayed stations (REQ-517.6a).
struct SampleLineSettings {
  double startStation = 0.0;
  double endStation = 0.0;
  double interval = 0.0;  ///< Internal distance between lines, drawing units.
  double leftWidth = 0.0;
  double rightWidth = 0.0;
};

/// A command makes at most this many sample lines (REQ-519.10). A guard against a tiny interval over a long alignment.
inline constexpr int kMaxSampleLinesPerRun = 2000;

/// Why these settings cannot make sample lines on `a`, or an empty string when they can (REQ-519.4, .10). The message
/// names the value that failed and is written to the command log as it stands.
[[nodiscard]] std::string SampleLineSettingsRefusal(const Alignment& a, const SampleLineSettings& s);

/// The sample lines for `s` on `a` (REQ-519.1), one at each interval station from the start to the end station, named by
/// their displayed station. The lines carry no sections yet. Returns false with `why` when the settings are refused.
[[nodiscard]] bool ComputeSampleLines(const Alignment& a, const SampleLineSettings& s, const StationFormat& fmt,
                                      std::vector<SampleLine>* out, std::string* why);

/// Samples `line` against a TIN (REQ-519.2): fills `line.section`. Sample points are the line's origin and each swath end
/// inside coverage, and each point where the swath crosses a triangle edge or vertex. Each point is kept only when the
/// surface covers it, and its ground comes from `surface` (REQ-497). A surface without triangles gives no points.
void SampleSection(const std::vector<double>& vertsXyz, const std::vector<std::uint32_t>& indices,
                   const ISurfaceQuery& surface, SampleLine& line);

/// The same, testing only the triangles that `index` (built once for the TIN by BuildTinSpatialIndex) places in the cells
/// the swath's plan box touches. Every triangle that crosses the swath is among them, so the result is the same, and a long
/// station range over a large TIN no longer scans every triangle for every line (REQ-519.2).
void SampleSection(const std::vector<double>& vertsXyz, const std::vector<std::uint32_t>& indices,
                   const TinSpatialIndex& index, const ISurfaceQuery& surface, SampleLine& line);

/// Drops every sample line whose alignment is gone (REQ-519.13). Called after an alignment is removed and on load.
void SyncSampleLines(const std::vector<Alignment>& alignments, std::vector<SampleLine>& lines);

/// The smallest step of the series 1, 2, 5, 10, 20, 50, 100, ... (feet per inch, REQ-519.11) that is at least `required`.
[[nodiscard]] double NiceFeetPerInchAtLeast(double required);

/// One sheet's scales (REQ-519.11), in feet per paper inch: across the columns and down the plot.
struct SectionScale {
  double feetPerInchAcross = 10.0;
  double feetPerInchDown = 10.0;
};

/// The finest scales at which every section on a sheet fits its column (across) and the plot (down). `maxOffset` is the
/// largest distance of a section point from the alignment, and `maxDeviation` the largest distance of a point's ground from
/// the axis elevation; `halfColumnIn` and `halfPlotIn` are the room either side of the axis, already net of the clear space,
/// in paper inches. With no points on the sheet both scales are 10 ft per inch.
[[nodiscard]] SectionScale FitSectionScale(bool anyPoint, double maxOffset, double maxDeviation, double halfColumnIn,
                                           double halfPlotIn);

}  // namespace alignment
