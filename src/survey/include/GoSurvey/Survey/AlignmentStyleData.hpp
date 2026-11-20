#pragma once

#include <GoSurvey/Document/AppearanceComponent.hpp>
#include <string>
#include <vector>

/// Named alignment display style (REQ-491): tangent, curve and spiral line appearance; resolved on read.
/// REQ-491: how one geometry point code (BP, PI, TS, ... as the labels write them) is marked.
enum class AlignmentMarkerShape : unsigned char { Cross = 0, X = 1, Circle = 2, Square = 3, None = 4 };
struct AlignmentMarkerSpec {
  std::string code;
  AlignmentMarkerShape shape = AlignmentMarkerShape::Cross;
  double halfSize = 1.5;          ///< Drawing units.
  std::string color = "ByLayer";  ///< CAD colour storage; ByLayer = the style's marker colour.

  bool operator==(const AlignmentMarkerSpec& o) const {
    return code == o.code && shape == o.shape && halfSize == o.halfSize && color == o.color;
  }
};

struct AlignmentStyle {
  std::string name;
  SurfaceComponentStyle tangent;
  SurfaceComponentStyle curve;
  SurfaceComponentStyle spiral;
  /// REQ-491: station tick marks across the alignment at its label-set stations. Length in drawing units (major, minor).
  SurfaceComponentStyle ticks;
  double majorTickLength = 8.0;
  double minorTickLength = 4.0;
  int tickSide = 0;  ///< 0 = both sides of the alignment, 1 = left, 2 = right (REQ-491 "offset side").
  /// REQ-491: small crosses at the geometry points (TS, SC, PC, PT, PI and so on), half-size in drawing units.
  SurfaceComponentStyle markers;
  double markerHalfSize = 1.5;
  /// REQ-491: an arrow at the middle of the alignment showing its direction. Off by default.
  SurfaceComponentStyle arrows;
  double arrowLength = 10.0;
  /// REQ-491 "label visibility": station, equation and station/offset labels draw only when this is on.
  bool labelsVisible = true;
  /// REQ-502 / D-2026-11-04-b: the design-check set this style applies to its alignments. Empty = none.
  /// An alignment's own check set overrides it.
  std::string checkSetName;
  /// REQ-491 Markers tab: a marker for particular geometry point codes; codes without one use the cross above.
  std::vector<AlignmentMarkerSpec> codeMarkers;

  bool operator==(const AlignmentStyle& o) const {
    return name == o.name && tangent == o.tangent && curve == o.curve && spiral == o.spiral && ticks == o.ticks &&
           majorTickLength == o.majorTickLength && minorTickLength == o.minorTickLength && tickSide == o.tickSide &&
           markers == o.markers && markerHalfSize == o.markerHalfSize && arrows == o.arrows &&
           arrowLength == o.arrowLength && labelsVisible == o.labelsVisible &&
           checkSetName == o.checkSetName && codeMarkers == o.codeMarkers;
  }
  bool operator!=(const AlignmentStyle& o) const { return !(*this == o); }
};

/// Named alignment label style (REQ-492.5): how one kind of alignment label is written. Built-in "Standard".
struct AlignmentLabelStyle {
  std::string name;
  std::string textStyle = "Standard";  ///< A drawing text style (REQ-044) giving the font. Missing reads as Standard.
  double heightInches = 0.1;           ///< Plotted text height; drawing units = this × the plot scale.
  std::string color = "ByLayer";       ///< CAD colour storage. ByLayer = the alignment's layer colour.
  /// REQ-492.5 formatting: decimals of the numbers and stations the label writes; -1 = the drawing's.
  int precision = -1;
  std::string prefix;  ///< Written before each line of the label.
  std::string suffix;  ///< Written after each line of the label.
  /// REQ-492.5 orientation, Civil 3D's Orientation Reference: false = Object (along the alignment), true = View
  /// (level on the screen).
  bool viewOriented = false;

  bool operator==(const AlignmentLabelStyle& o) const {
    return name == o.name && textStyle == o.textStyle && heightInches == o.heightInches && color == o.color &&
           precision == o.precision && prefix == o.prefix && suffix == o.suffix && viewOriented == o.viewOriented;
  }
  bool operator!=(const AlignmentLabelStyle& o) const { return !(*this == o); }
};

/// A further Geometry Points item of a label set (REQ-492.1): the geometry point codes it labels (BP, PI, TS, ...) in
/// its own label style, e.g. BP and EP in one style and PIs in another.
struct AlignmentGeometryLabelItem {
  std::string style = "Standard";
  std::vector<std::string> codes;

  bool operator==(const AlignmentGeometryLabelItem& o) const { return style == o.style && codes == o.codes; }
};

/// Named alignment label set (REQ-492): station tick intervals and visibility (first slice).
struct AlignmentLabelSet {
  std::string name;
  bool majorStationsOn = true;
  bool minorStationsOn = true;
  double majorInterval = 100.0;
  double minorInterval = 20.0;
  bool equationsOn = true;  ///< REQ-492.1 Station Equations item: an equation label at each equation.
  bool geometryPointsOn = true;  ///< REQ-492.1 Geometry Points item (D-2026-11-04-c).
  bool designSpeedsOn = true;    ///< REQ-492.1 Design Speeds item (D-2026-11-04-c).
  /// REQ-492.5: the label style of each item. Segment and station/offset labels belong to the alignment but take their
  /// style from its label set (GoSurvey rule, D-2026-11-04-e).
  std::string majorStyle = "Standard";
  std::string geometryStyle = "Standard";
  std::string speedStyle = "Standard";
  std::string equationStyle = "Standard";
  std::string segmentStyle = "Standard";
  std::string offsetStyle = "Standard";
  /// REQ-492.1: the codes the first Geometry Points item labels (empty = every code), and any further items.
  std::vector<std::string> geometryCodes;
  std::vector<AlignmentGeometryLabelItem> extraGeometryItems;
  /// REQ-492.1 placeholder items: kept in the set, drawing nothing until profiles and superelevation exist.
  bool profileGeometryPointsOn = false;
  bool superelevationPointsOn = false;

  bool operator==(const AlignmentLabelSet& o) const {
    return name == o.name && majorStationsOn == o.majorStationsOn && minorStationsOn == o.minorStationsOn &&
           majorInterval == o.majorInterval && minorInterval == o.minorInterval && equationsOn == o.equationsOn &&
           geometryPointsOn == o.geometryPointsOn && designSpeedsOn == o.designSpeedsOn && majorStyle == o.majorStyle &&
           geometryStyle == o.geometryStyle && speedStyle == o.speedStyle && equationStyle == o.equationStyle &&
           segmentStyle == o.segmentStyle && offsetStyle == o.offsetStyle && geometryCodes == o.geometryCodes &&
           extraGeometryItems == o.extraGeometryItems && profileGeometryPointsOn == o.profileGeometryPointsOn &&
           superelevationPointsOn == o.superelevationPointsOn;
  }
  bool operator!=(const AlignmentLabelSet& o) const { return !(*this == o); }
};
