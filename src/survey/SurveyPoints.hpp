#pragma once

#include <cctype>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "PointGroupRule.hpp"

enum class SurveyPointLabelStyle : uint8_t {
  None = 0,
  NumberDesc,
  NumberOnly,
  DescOnly,
  NumberElev,
  NumberElevDesc,
  NumberNorthEast,
  NorthEast,
  NumberNorthEastElev,
};

/// Placeholders: {id} {desc} {elev} {north} {east} — use a real newline in the string for line breaks.
struct SurveyLabelStyleTemplates {
  std::string numberDesc = "{id}\n{desc}";
  std::string numberOnly = "{id}";
  std::string descOnly = "{desc}";
  std::string numberElev = "{id}\nZ={elev}";
  std::string numberElevDesc = "{id}\nZ={elev}\n{desc}";
  std::string numberNorthEast = "{id}\nN={north}\nE={east}";
  std::string northEast = "N={north}\nE={east}";
  std::string numberNorthEastElev = "{id}\nN={north}\nE={east}\nZ={elev}";
};

/// Drawing-owned survey-point style data carried by GoSurvey's DWG payload. Civil 3D point style
/// values that have a GoSurvey equivalent map into this definition; GoSurvey writes supported
/// AutoCAD entities/style records rather than authoring Civil 3D custom objects.
struct SurveyPointStyleComponent {
  bool visible = true;
  std::string color = "ByLayer";
  std::string layer;
  std::string linetype = "ByLayer";
  int lineweight = -2;
  double linetypeScale = 1.0;
};

struct SurveyPointStyleDefinition {
  std::string name = "Standard";
  float crossSpanPlottedInches = 0.14f;
  std::string color = "ByLayer";
  std::string labelStyleName = "Standard";
  /// Civil point-style display components map independently; a hidden component stays hidden.
  bool markerVisible = true;
  bool labelVisible = true;
  /// Annotative (D-2026-11-06-a): in model space the marker is sized from the current annotation scale.
  /// Survey points are annotative by default, and a drawing saved before this field loads as annotative.
  bool annotative = true;
  bool hasExplicitMarkerGeometry = false;
  unsigned markerType = 1;  // AutoCAD point=0, custom=1, named block=2.
  unsigned customMarker = 3;  // Dot, blank, plus, X, vertical line.
  unsigned markerOverlay = 0;  // Circle=1, square=2, both=3.
  unsigned markerSizeType = 0;  // Paper size, screen percentage, model size, fixed block scale.
  unsigned markerOrientation = 1;
  unsigned markerDisplay3dType = 0;
  double markerSize = 0.0;
  double markerRotationRad = 0.0;
  std::array<double, 3> markerFixedScale = {1.0, 1.0, 1.0};
  double markerElevation = 0.0;
  double markerScaleFactor = 1.0;
  std::string markerBlockName;
  /// Plan marker/label, model marker/label and section marker/label display components.
  std::array<SurveyPointStyleComponent, 6> displayComponents;
};

inline const SurveyPointStyleDefinition* FindSurveyPointStyle(
    const std::vector<SurveyPointStyleDefinition>& styles, const std::string& name) {
  for (const auto& style : styles) {
    if (style.name.size() != name.size()) continue;
    bool equal = true;
    for (std::size_t i = 0; i < name.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(style.name[i])) !=
          std::tolower(static_cast<unsigned char>(name[i]))) {
        equal = false;
        break;
      }
    }
    if (equal) return &style;
  }
  return nullptr;
}

/// Named point-label style data. The format selector on each point remains for legacy compatibility;
/// these templates and text properties travel with the drawing style record.
struct SurveyPointLabelStyleDefinition {
  std::string name = "Standard";
  SurveyLabelStyleTemplates templates;
  std::string fontFamily;
  float textHeightPlottedInches = 0.10f;
  std::string color = "ByLayer";
  bool visible = true;
  /// Civil's original component expressions, retained independently of the legacy format selector.
  /// Numeric property identifiers and formatting modifiers are drawing data, not display names.
  std::vector<std::string> civilTextContents;
};

inline std::vector<SurveyPointStyleDefinition> DefaultSurveyPointStyles() {
  return {SurveyPointStyleDefinition{}};
}

inline std::vector<SurveyPointLabelStyleDefinition> DefaultSurveyPointLabelStyles() {
  return {SurveyPointLabelStyleDefinition{}};
}

struct SurveyPoint {
  int id = 0;
  double easting = 0.0;
  double northing = 0.0;
  double elevation = 0.0;
  std::string description;
  /// The field code as collected, never rewritten by an edit to \ref description (REQ-066).
  ///
  /// The two are independent on purpose: a crew codes `EG`, the office expands or edits the
  /// description, and a point group keyed on the raw code must still match afterwards (REQ-067).
  /// **Empty means "this point predates REQ-066"** — every drawing written before it, and every
  /// pre-REQ-066 DXF — and consumers fall back to \ref description rather than skipping the point.
  std::string rawDescription;
  std::string layer;
  SurveyPointLabelStyle labelStyle = SurveyPointLabelStyle::NumberDesc;
  std::string styleName = "Standard";
  std::string labelStyleName = "Standard";
  /// Stable id (\ref EntityAttributes::id) of this point's linked MTEXT label, or 0 for none.
  ///
  /// **An id, not an index** (REQ-076 / architecture §11.9). It was `labelMtextAnnIndex`, and
  /// keeping it correct cost a decrement loop inside `EraseCadAnnotationAtIndex` — erasing any
  /// annotation had to walk every survey point and renumber. That loop is deleted; an id needs no
  /// fix-up because it never means anything but the entity it was issued to.
  std::uint64_t labelMtextAnnId = 0;
};

inline bool SurveyPointMarkerVisible(const SurveyPoint& point,
                                     const std::vector<SurveyPointStyleDefinition>& styles) {
  const auto* style = FindSurveyPointStyle(styles, point.styleName);
  return style == nullptr || style->markerVisible;
}

inline bool SurveyPointLabelVisible(const SurveyPoint& point,
                                    const std::vector<SurveyPointStyleDefinition>& styles) {
  const auto* style = FindSurveyPointStyle(styles, point.styleName);
  return style == nullptr || style->labelVisible;
}

inline bool SurveyPointNamedLabelVisible(const SurveyPoint& point,
                                        const std::vector<SurveyPointLabelStyleDefinition>& styles) {
  for (const auto& style : styles)
    if (style.name == point.labelStyleName) return style.visible;
  return true;
}
enum class SurveyDuplicatePolicy { Notify, Renumber, Merge, Overwrite };

struct CreatePointsOptions {
  int startNumber = 1;
  bool sequentialNumbering = true;
  int pointNumberOffset = 1;
  int sequenceNumbersFrom = 1;
  /// The layer typed in Create Points. Empty = the drawing's Object Layers Survey point layer
  /// (REQ-361); ignored while that row is Locked.
  std::string layer;
  std::string defaultDescription;
  float defaultElevation = 0.f;
  SurveyDuplicatePolicy duplicatePolicy = SurveyDuplicatePolicy::Notify;
};

/// Half of horizontal span of the X in world units from paper span (inches) and plot scale (MUP).

float SurveyPointCrossHalfWorldFromPaper(float crossSpanPlottedInches, float modelUnitsPerPlottedInch);

/// Camera right/up as world-space unit vectors, used to build screen-facing markers (REQ-058).
///
/// Passed in as plain components rather than as a `Camera`: survey is a domain module and the
/// camera lives in the renderer, so taking one here would invert the layering (architecture §11).
/// The default is world +X / +Y, which is exactly plan view — so an un-plumbed caller keeps the
/// pre-3D geometry.
struct MarkerBillboardBasis {
  float rightX = 1.f, rightY = 0.f, rightZ = 0.f;
  float upX = 0.f, upY = 1.f, upZ = 0.f;
};

/// Appends two GL_LINES segments (x,y,z triplets) forming an X centered at the point.
///
/// The X is built in the \p basis plane, not in world XY: it is a marker, not geometry, so it must
/// stay readable at any orientation instead of foreshortening to an edge near a horizontal view
/// (REQ-058 / GAP-2).
void AppendSurveyPointCrossVertices(double easting, double northing, double elevationZ, float halfExtentWorld,
                                    std::vector<float>* outLines, const MarkerBillboardBasis& basis = {});

void AppendAllSurveyPointMarkers(float crossHalfWorld, const std::vector<SurveyPoint>& pts,
                                 std::vector<float>* outLines, const MarkerBillboardBasis& basis = {});

/// \p worldOriginX/Y are added to the point's local easting/northing so {north}/{east} render world
/// (state-plane) coordinates rather than the internal local-space values.
[[nodiscard]] std::string FormatSurveyPointLabelPlain(const SurveyPoint& p, SurveyPointLabelStyle style,
                                                      const SurveyLabelStyleTemplates& templates, int precision,
                                                      double worldOriginX = 0.0, double worldOriginY = 0.0);
