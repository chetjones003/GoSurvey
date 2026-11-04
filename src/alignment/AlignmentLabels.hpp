#pragma once

#include "Alignment.hpp"
#include "util/AngleFormat.hpp"

#include <string>
#include <vector>

namespace alignment {

/// One major or minor station tick label along an alignment (REQ-492.2).
struct StationTickLabel {
  double rawStation = 0.0;
  double displayedStation = 0.0;
  bool major = false;
};

/// Collects major and minor station labels using displayed-station spacing. Major ticks sit at
/// `majorInterval` from the alignment start (excluding the start/end themselves). Minor ticks sit
/// every `minorInterval` but skip majors and skip ticks closer than one minor interval to the end.
[[nodiscard]] std::vector<StationTickLabel> CollectMajorMinorStationTicks(const Alignment& al,
                                                                          double majorInterval,
                                                                          double minorInterval,
                                                                          bool wantMajor, bool wantMinor);

/// One equation label (REQ-492.2 / REQ-492 AC2): where a station equation sits and the displayed stations either side of it.
struct EquationLabel {
  double rawStation = 0.0;       ///< Internal station of the equation point.
  double backDisplayed = 0.0;    ///< Displayed station just before it (the back value).
  double aheadDisplayed = 0.0;   ///< Displayed station just after it (the ahead value).
  bool increasing = true;
};

/// Equation labels in travel order. Their displayed values follow every equation before them (REQ-483).
[[nodiscard]] std::vector<EquationLabel> CollectEquationLabels(const Alignment& al);

/// The text and plan point of one station/offset label now (REQ-492.4). A fixed label reports the station and offset of its
/// stored point on the current geometry; a relative label reports its stored station and offset, placed on the current geometry.
struct StationOffsetLabelPlacement {
  Point2 point;
  double internalStation = 0.0;
  double offset = 0.0;
  bool onAlignment = true;  ///< False when a fixed point projects beyond an end of the alignment, or a relative station is off it.
  std::string text;         ///< "12+45.32  OFF -15.42" in the drawing's precision.
};
[[nodiscard]] StationOffsetLabelPlacement PlaceStationOffsetLabel(const Alignment& al, const StationOffsetLabel& label,
                                                                  int linearPrecision);

/// The abbreviation a geometry-point label shows for a code (D-2026-11-04-c), or "" when the code is not labelled
/// (line midpoint, curve centre, curve PI).
[[nodiscard]] const char* GeometryPointAbbreviation(GeometryPointCode code);

/// One geometry-point label (REQ-492.1 Geometry Points item): where it sits and its text, e.g. "PC: 5+90.00".
struct GeometryPointLabel {
  Point2 point;
  double internalStation = 0.0;
  GeometryPointCode code = GeometryPointCode::Begin;
  std::string text;
};
[[nodiscard]] std::vector<GeometryPointLabel> CollectGeometryPointLabels(const Alignment& al);

/// One design-speed label (REQ-492.1 Design Speeds item), e.g. "STA 5+00.00 DESIGN SPEED = 30 mi/h".
struct DesignSpeedLabel {
  double internalStation = 0.0;
  std::string text;
};
[[nodiscard]] std::vector<DesignSpeedLabel> CollectDesignSpeedLabels(const Alignment& al, bool metric);

/// Segment label lines (REQ-492.3, stacked per REQ-492.5). Line: bearing, then length. Spiral: A, Ls, then the start and
/// end radius (∞ = tangent). Curves use CurveLabelLines (AlignmentTables.hpp).
[[nodiscard]] std::vector<std::string> LineLabelLines(const Entity& line, int linearPrecision,
                                                      const AngleDisplaySettings& angles);
[[nodiscard]] std::vector<std::string> SpiralLabelLines(const SpiralData& spiral, int linearPrecision);

/// Alignment label layout (REQ-492.5, D-2026-11-04-c). Labels are drawing text of a plotted height, as Civil 3D's
/// annotative labels are; every distance below is in text heights so the viewport and ZOOM EXTENTS agree on where a
/// label reaches.
inline constexpr double kLabelPlottedHeightInches = 0.1;
inline constexpr double kLeaderGapHeights = 0.5;        ///< From the point to the start of the leader line.
inline constexpr double kLeaderTextStartHeights = 1.0;  ///< From the point to the first character.
inline constexpr double kLeaderOvershootHeights = 0.5;  ///< Leader line past the last character.
inline constexpr double kLabelCharWidthHeights = 0.7;   ///< Generous mean glyph advance: extents only, never drawing.

/// How far a leader label (geometry point, design speed) reaches from its point, in drawing units.
[[nodiscard]] double LeaderLabelReach(const std::string& text, double textHeight);

/// What the label set asks an alignment to label, for its labelled bounds.
struct AlignmentLabelItems {
  bool geometryPoints = false;
  bool designSpeeds = false;
  bool metric = false;
};
/// The plan box of an alignment and its labels: the geometry grown by two text heights (station, segment and
/// equation text sit beside the line) and each leader label's far end. False for an empty alignment.
[[nodiscard]] bool LabelledBounds(const Alignment& al, const AlignmentLabelItems& items, double textHeight, Point2* mn,
                                  Point2* mx);

/// Moves a label to a plan point (REQ-492.5, drag). A fixed label takes the point as it is; a relative label takes the
/// station and offset of the nearest point on the alignment. False when the point has no meaning (empty alignment).
[[nodiscard]] bool MoveStationOffsetLabelTo(const Alignment& al, StationOffsetLabel* label, Point2 point);

}  // namespace alignment
