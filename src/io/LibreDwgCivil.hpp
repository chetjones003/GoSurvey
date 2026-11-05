#pragma once

#include "commands/CadEntities.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <unordered_set>
#include <string>
#include <utility>
#include <vector>

namespace libredwgcivil {

// REQ-479 (D-2026-10-31-a): decoders for Civil 3D payloads already embedded in a DWG. They operate on
// LibreDWG-provided bytes only — no Civil 3D, AutoCAD, bridge, or out-of-process oracle at runtime.

/// AEC drawing-setup revision 0x5434d150: model units per plotted inch for Civil point symbols.
[[nodiscard]] std::optional<double> DecodeDrawingScale(const unsigned char* bytes, std::size_t bitCount);

struct PointTableRecord {
  uint32_t maximumRows = 0;
  uint32_t firstRow = 0;
  std::array<double, 16> transform{};
};

struct TableColumnRecord {
  std::u16string header;
  std::u16string expression;
  double width = 0.0;
};

struct TableStyleDisplayComponent {
  bool visible = false;
  uint32_t color = 0;
};

inline constexpr std::size_t kTableStyleDisplayCount = 22;

struct TableStyleRecord {
  std::u16string name;
  std::u16string title;
  std::vector<TableColumnRecord> columns;
  double dataTextHeight = 0.0;
  double headerTextHeight = 0.0;
  double titleTextHeight = 0.0;
  std::array<TableStyleDisplayComponent, kTableStyleDisplayCount> displayComponents{};
};

/// Plan display indices for the eleven table-style components verified on the Rapides point tables.
inline constexpr std::size_t kTableStyleDataTextDisplay = 0;
inline constexpr std::size_t kTableStyleHeaderTextDisplay = 1;
inline constexpr std::size_t kTableStyleTitleTextDisplay = 2;
inline constexpr std::size_t kTableStyleDataSeparatorDisplay = 7;
inline constexpr std::size_t kTableStyleBorderDisplay = 10;

/// Revision 1214 table styles: drawing-owned column headers/expressions and paper-space sizes.
[[nodiscard]] std::optional<TableStyleRecord> DecodeTableStyle(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart,
    std::size_t stringBitCount);

/// Civil point-table split segment: capacity, source row offset and insertion transform. The two
/// matrix records and their tagged footer must agree with the supported revision's complete layout.
[[nodiscard]] std::optional<PointTableRecord> DecodePointTable(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart);

struct PointLocation {
  int number = 0;
  CadPoint location{};
};

struct PointDisplayComponent {
  bool visible = false;
  uint32_t color = 0;
  int16_t lineweight = -2;
  double linetypeScale = 1.0;
};

struct PointStyleRecord {
  std::u16string name;
  std::array<PointDisplayComponent, 6> components;
  unsigned customMarker = 3;
  unsigned markerOverlay = 0;
  unsigned markerType = 1;
  unsigned sizeType = 0;
  unsigned orientation = 1;
  unsigned display3dType = 0;
  double rotation = 0.0;
  double size = 0.0;
  std::array<double, 3> fixedScale = {1.0, 1.0, 1.0};
  double elevation = 0.0;
  double scaleFactor = 1.0;
};

/// Civil revision 1214 point-style records. Offsets delimit the separate DWG main/string streams;
/// unsupported component headers, revisions, enum values and string encodings fail atomically.
[[nodiscard]] std::optional<PointStyleRecord> DecodePointStyle(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart,
    std::size_t stringBitCount);

/// Decodes count handles starting at the bit offset start, resolving relative handles against
/// objectHandle. Returns no value for unsupported codes, invalid or truncated data, or overflow.
[[nodiscard]] std::optional<std::vector<uint64_t>> DecodeHandles(
    const unsigned char* bytes, std::size_t bitCount, std::size_t start,
    std::size_t count, uint64_t objectHandle);

/// Reads the coordinate section of the supported Civil point-node revision. Rejects unknown
/// revisions, truncated data and unsupported record layouts without returning partial points.
[[nodiscard]] std::optional<std::vector<PointLocation>> DecodePointNodeLocations(
    const unsigned char* bytes, std::size_t bitCount);

/// Returns point-number and marker-style-handle pairs from a supported point node. handleStart is
/// a bit offset; commonHandles is the number of leading handles preceding style handles. Returns no
/// value if point records, references, or handle data are invalid or truncated.
[[nodiscard]] std::optional<std::vector<std::pair<int, uint64_t>>> DecodePointNodeMarkerStyles(
    const unsigned char* bytes, std::size_t bitCount, std::size_t handleStart,
    std::size_t commonHandles, uint64_t objectHandle);

/// Reads a Civil 3D COGO point's DWG proxy graphics and returns the center of its marker.
/// The preview is a rendering fallback owned by the DWG, so this recovers geometry without a
/// Civil 3D runtime. It does not decode the COGO point's number, descriptions, or style records.
[[nodiscard]] std::optional<CadPoint> DecodeCogoPointProxyCenter(const unsigned char* preview,
                                                                  std::size_t previewSize);

/// Reads the COGO point number from the Civil 3D custom entity payload. In the supported
/// Civil 3D class version, the payload begins with the 0x2f81 signature followed by the
/// compact DWG BL point-number field.
[[nodiscard]] std::optional<int> DecodeCogoPointNumber(const unsigned char* unknownBits,
                                                       std::size_t unknownBitCount);

/// Reads the versioned proxy-graphics stream attached to an unmodeled Civil 3D entity and returns
/// each 3D-polyline primitive as an independent vertex chain. These chains are a display fallback;
/// they are not treated as the entity's editable Civil 3D definition.
[[nodiscard]] std::optional<std::vector<std::vector<CadPoint>>> DecodeProxyGraphicPolylines(
    const unsigned char* preview, std::size_t previewSize);

struct TinSurfaceRecord {
  std::u16string name;  ///< Empty when the name record is absent or malformed.
  /// Interleaved world x,y,z of the vertices used by at least one visible triangle, in source order.
  std::vector<double> vertsXyz;
  /// Visible triangles in source order, 3 indices into `vertsXyz` each, counter-clockwise.
  std::vector<uint32_t> indices;
  /// Triangles the surface stores but does not display: the four-corner superstructure that
  /// encloses the points, and triangles hidden by the surface's boundaries.
  std::size_t hiddenTriangles = 0;
};

/// Reads the triangulation stored in a Civil 3D `AECC_TIN_SURFACE` payload (`unknown_bits`).
/// Only the revision the supplied drawings use is accepted. The vertex table and the triangle
/// table (vertex indices, neighbour indices and a visibility flag per triangle) are located by their
/// fixed record layout and validated as a unit; any inconsistency returns nothing, never a partial
/// surface. Style, boundary and edit-history records are not decoded.
[[nodiscard]] std::optional<TinSurfaceRecord> DecodeTinSurface(const unsigned char* unknownBits,
                                                               std::size_t unknownBitCount);

struct SurfaceStyleComponent {
  bool visible = false;
  uint32_t color = 0;   ///< Civil color word: method in the high byte, ACI/RGB in the low 24 bits.
  int16_t lineweight = -2;
};

struct SurfaceStyleRecord {
  std::u16string name;
  /// Plan display components in the order Points, Triangles, Boundary, Major contour, Minor contour.
  std::array<SurfaceStyleComponent, 5> components;
  double minorInterval = 0.0;
  double majorInterval = 0.0;
  /// Contour smoothing as stored: whether it is on and the smoothing factor. Both stay empty when the
  /// record's tagged values do not match the verified layout. The smoothing type is not decoded.
  std::optional<bool> smoothContours;
  std::optional<uint32_t> smoothingFactor;
};

/// Civil revision 1214 surface styles: name, the five plan display components and the contour
/// intervals, plus the smoothing flag and factor when their tagged values match the supported layout.
/// Smoothing type, label, slope and analysis settings are not decoded. The contour intervals
/// sit in a fixed sequence of ten tagged values inside the record; the record is refused unless that
/// sequence and the full set of component headers are present and valid.
[[nodiscard]] std::optional<SurfaceStyleRecord> DecodeSurfaceStyle(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart,
    std::size_t stringBitCount);

struct SurfaceElevationLabelRecord {
  CadPoint location{};  ///< World x, y of the label and its stored surface elevation in z.
};

/// Reads the location and surface elevation a Civil 3D surface elevation label stores. The point is
/// stored twice, as a raw 2D pair and as a compact 3D point; the two must agree. Only the revision
/// the supplied drawings use is accepted. The label style (text, marker, precision) is not decoded.
[[nodiscard]] std::optional<SurfaceElevationLabelRecord> DecodeSurfaceElevationLabel(
    const unsigned char* unknownBits, std::size_t unknownBitCount);

/// Reads the position and contour elevation of a Civil 3D surface contour label group. The point is
/// stored as raw doubles (x, y, z) in the label's data, and the group's proxy graphics hold the
/// label's bounding rectangle at that elevation; the stored point must be that rectangle's centre and
/// elevation. Groups without a label (empty rectangle) and any other revision are refused. The label's
/// rotation is not stored: Civil 3D derives it from the contour when it draws the label.
[[nodiscard]] std::optional<CadPoint> DecodeSurfaceContourLabel(
    const unsigned char* unknownBits, std::size_t unknownBitCount, const unsigned char* preview,
    std::size_t previewSize);

struct PointDescription {
  int number = 0;
  bool literal = false;       ///< False for a built-in description token, whose text is not stored here.
  std::u16string text;        ///< The point's raw description when `literal`.
};

/// Reads the per-point raw descriptions of the supported Civil point-node revision. Each description
/// is a (point number, token) pair; a token with the high bit set takes the next string of the
/// object's string stream, any other token is a built-in description with no stored text. Refuses
/// unknown layouts, point numbers the node does not own, and truncated string data.
[[nodiscard]] std::optional<std::vector<PointDescription>> DecodePointNodeDescriptions(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart,
    std::size_t stringBitCount);

struct PointGroupRecord {
  std::u16string name;
  uint32_t displayPriority = 0;
  bool markerStyleOverride = false;
  bool labelStyleOverride = false;
  /// Member point numbers, in stored order. Empty when `membersDecoded` is false.
  std::vector<int> members;
  bool membersDecoded = false;
};

/// Name of a revision-1214 style, from its inline string or the predefined Standard token.
[[nodiscard]] std::optional<std::u16string> DecodeStyleName(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart,
    std::size_t stringBitCount);

struct LabelStyleRecord {
  std::u16string name;
  bool visible = true;
};

/// Revision-1214 label collector name and visibility (including the inherited default).
[[nodiscard]] std::optional<LabelStyleRecord> DecodeLabelStyle(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart,
    std::size_t stringBitCount);

/// Serialized text content of a revision-1214 label text component. Property placeholders retain
/// their numeric Civil identifiers; unsupported layouts or ambiguous content tags are rejected.
[[nodiscard]] std::optional<std::u16string> DecodeLabelTextContent(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart);

/// Text height stored in drawing feet for revision-1214 label text components.
[[nodiscard]] std::optional<double> DecodeLabelTextHeight(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart);

/// Reads a Civil point group's name and member list. The name is a string-stream literal or one of
/// the three built-in group-name tokens seen in the supplied drawings. The members are stored as a
/// compact count followed by that many raw 32-bit point numbers at a fixed distance after the name
/// token; the list is accepted only when every number is one of `knownPoints`, so a group whose
/// layout differs reports `membersDecoded == false` rather than invented members.
[[nodiscard]] std::optional<PointGroupRecord> DecodePointGroup(
    const unsigned char* bytes, std::size_t bitCount, std::size_t stringStart,
    std::size_t stringBitCount, const std::unordered_set<int>& knownPoints);

/// Every handle in the handle stream that starts at `start`, up to the first entry that is not a
/// valid handle. Empty when the stream cannot be read at all.
[[nodiscard]] std::vector<uint64_t> DecodeHandleList(const unsigned char* bytes, std::size_t bitCount,
                                                     std::size_t start, uint64_t objectHandle);

// --- REQ-496.3 Civil 3D alignments (AECC_ALIGNMENT), layout verified on Civil 3D 2026 fixtures ---

/// A fixed tangent: its sub-entity id, the ids of the entities before and after it (0 = none) and the two points that
/// define its line, drawing coordinates.
struct AlignmentTangentRecord {
  uint32_t id = 0, previous = 0, next = 0;
  double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
};

/// A free curve, or a free spiral-curve-spiral group, between the tangents `previous` and `next`.
struct AlignmentCornerRecord {
  uint32_t id = 0, previous = 0, next = 0;
  double radius = 0.0;
  double spiralIn = 0.0, spiralOut = 0.0;  ///< Clothoid lengths; 0 for a plain curve.
};

struct AlignmentEquationRecord {
  double rawStationBack = 0.0;
  double stationAhead = 0.0;
  bool increasing = true;
};

struct AlignmentRecord {
  std::u16string name;
  std::optional<double> startingStation;
  std::vector<AlignmentTangentRecord> tangents;
  std::vector<AlignmentCornerRecord> corners;
  std::vector<AlignmentEquationRecord> equations;
};

/// Reads an AECC_ALIGNMENT's custom data: name (first string of the string stream), starting station, and the
/// definitions of its fixed tangents (two points each), free curves (radius) and free spiral-curve-spiral groups
/// (spiral in, radius, spiral out) with the sub-entity links between them, and its station equations. Other
/// sub-entity kinds are not decoded; the caller finds them as links that no decoded record has. Nullopt when no
/// tangent is found.
[[nodiscard]] std::optional<AlignmentRecord> DecodeAlignment(const unsigned char* bytes, std::size_t bitCount,
                                                             std::size_t stringStart, std::size_t stringBitCount);

}  // namespace libredwgcivil
