#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Horizontal alignment domain object (REQ-480..484, ADR-075, issue #121).
///
/// An alignment is a station-indexed chain of EXACT entities — tangents and circular curves in
/// Phase 1 — stored as parameters, never as a sampled polyline (REQ-480.2, REQ-481). It holds no
/// elevation (REQ-480.3). Pure and dependency-free (`<cmath>`, `<vector>`, `<string>`), so it links
/// into the test target without the GUI stack.
///
/// Conventions (survey, not mathematical):
///   - Azimuth is radians CLOCKWISE from +Y (north); direction = (sin az, cos az).
///   - Offset is positive to the RIGHT facing the alignment end, negative to the left (REQ-484.2).
///   - Every entity is evaluated relative to its own start point so large state-plane coordinates
///     lose no precision (REQ-499).
///   - "Internal station" = `startStation + distance along the geometry`. "Displayed station"
///     additionally applies station equations (REQ-483.2). The two are different APIs on purpose.
namespace alignment {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

enum class AlignmentType { Centerline = 1, Offset = 2, CurbReturn = 3, Miscellaneous = 4, Rail = 5 };

struct Point2 {
  double x = 0.0;
  double y = 0.0;
};

/// Phase 1 ships Line and Arc; Spiral (clothoid) arrives in Phase 3 (REQ-481.3).
enum class EntityKind { Line, Arc };

/// One exact sub-entity. Stored parameters are the ONLY geometry; every other value is derived.
struct Entity {
  EntityKind kind = EntityKind::Line;
  Point2 start;               ///< Start coordinate, drawing-local.
  double startAzimuth = 0.0;  ///< Direction of travel at the start, radians clockwise from north.
  double length = 0.0;        ///< Arc length along the entity (> 0).
  double radius = 0.0;        ///< Arc only (> 0).
  bool right = true;          ///< Arc only: true = curves right (clockwise), false = left.
  /// Constraint state (REQ-481.6). Phase 1 stores it; the solver arrives in Phase 2 (REQ-486).
  enum class Constraint { Fixed, FloatOnPrev, FloatOnNext, Free } constraint = Constraint::Fixed;
};

/// Everything the issue asks to report about a circular curve (REQ-481.2). Derived, never stored.
struct CurveData {
  double radius = 0.0;
  double delta = 0.0;          ///< Central angle, radians (> 0).
  double arcLength = 0.0;
  double chord = 0.0;
  double tangentLength = 0.0;
  double external = 0.0;
  double midOrdinate = 0.0;
  double degreeOfCurveArc = 0.0;    ///< Degrees per 100 units of arc.
  double degreeOfCurveChord = 0.0;  ///< Degrees of the angle subtended by a 100-unit chord.
  bool right = true;
  bool hasPI = true;   ///< False when delta >= pi: the tangents never meet.
  Point2 pc;
  Point2 pi;
  Point2 pt;
  Point2 center;
};

/// A station equation (REQ-483.3). `rawBack` is the raw (pre-equation) station where it applies.
struct StationEquation {
  double rawBack = 0.0;    ///< Raw station at the equation point (startStation + distance).
  double ahead = 0.0;      ///< Displayed station immediately after the equation point.
  bool increasing = true;  ///< Increasing: ahead > back. Decreasing: ahead < back.
};

/// Standard geometry points exposed for snaps, labels and reports (REQ-490.1).
enum class GeometryPointCode {
  Begin,       ///< Alignment beginning.
  End,         ///< Alignment end.
  TanTan,      ///< Tangent-tangent intersection (a PI with no curve).
  TanCurve,    ///< PC (BC): tangent to curve.
  CurveTan,    ///< PT (EC): curve to tangent.
  CompCurveCurve,
  RevCurveCurve,
  CurveMidPt,
  CurvePI,     ///< Intersection of the two tangents a curve joins.
  CurveCenter,
  LineMidPt
};

struct GeometryPoint {
  GeometryPointCode code = GeometryPointCode::Begin;
  Point2 point;
  double internalStation = 0.0;
  int entityIndex = 0;
};

/// Result of GetStationOffset (REQ-484).
struct StationOffset {
  double internalStation = 0.0;
  double offset = 0.0;       ///< Right positive. Beyond an end it is the signed distance to that end point.
  double azimuth = 0.0;      ///< Alignment direction at the foot of the perpendicular.
  Point2 closest;
  int entityIndex = 0;
  bool outOfRange = false;   ///< Point projects beyond the start or end of the alignment.
  bool ambiguous = false;    ///< Another location was equally close (lowest station reported).
};

/// A continuity finding between two consecutive entities (REQ-482). Phase 1 reports C0 and C1.
struct ContinuityIssue {
  int afterEntity = 0;          ///< Index of the entity BEFORE the junction.
  bool c0 = true;               ///< Position continuity holds.
  bool c1 = true;               ///< Direction continuity holds.
  double gap = 0.0;             ///< End-to-start distance.
  double angleDelta = 0.0;      ///< Absolute azimuth difference, radians (0..pi).
};

struct ContinuityTolerance {
  double position = 1e-6;
  double angle = 1e-9;
};

class Alignment {
 public:
  // --- identity and metadata (REQ-480.1) ---
  std::uint64_t id = 0;
  std::string name;
  std::string description;
  std::string layer = "0";
  std::string style = "Standard";
  AlignmentType type = AlignmentType::Centerline;

  // --- station domain (REQ-483) ---
  double startingStation = 0.0;
  std::vector<StationEquation> equations;

  // --- geometry ---
  [[nodiscard]] const std::vector<Entity>& Entities() const { return entities_; }

  /// Appends a tangent. The first entity needs `start` and `azimuth`; later ones continue from the
  /// previous end and keep its heading, so the chain is C0 and C1 by construction. Returns false
  /// (nothing added) for a non-positive length.
  bool AppendTangent(double length);
  bool AppendTangent(Point2 start, double azimuth, double length);
  /// Appends a tangent from the current end TO a point. Refuses a zero-length move.
  bool AppendTangentTo(Point2 end);
  /// Appends a circular curve continuing from the previous end (tangent-continuous). `arcLength`
  /// and `radius` must be positive; a sweep >= 2*pi is refused (REQ-499).
  bool AppendCurve(double radius, double arcLength, bool right);
  bool AppendCurveByDelta(double radius, double delta, bool right);
  /// Appends an entity verbatim (used by create-from-objects and the DWG reader). Its own start is
  /// authoritative, so a non-tangent or gapped chain is representable and REQ-482 can report it.
  bool AppendRaw(const Entity& e);
  void Clear();
  /// Reverses direction and stationing of a continuous alignment (REQ-487.2). The physical geometry
  /// is unchanged. Refuses (returns false) when a C0 gap exists. Reversing twice restores it.
  /// Station equations cannot be mirrored unambiguously (open question, REQ-503), so they are CLEARED
  /// and `note` (if given) says how many were dropped.
  bool Reverse(std::string* note = nullptr);

  [[nodiscard]] bool Empty() const { return entities_.empty(); }
  [[nodiscard]] double Length() const { return length_; }
  /// Internal station at the start of entity i, and at the end of the alignment.
  [[nodiscard]] double EntityStartStation(int i) const;
  [[nodiscard]] double EntityEndStation(int i) const;
  [[nodiscard]] double StartStation() const { return startingStation; }
  [[nodiscard]] double EndStation() const { return startingStation + length_; }
  /// Last internal station with station equations applied (REQ-483: EndingStationWithEquations).
  [[nodiscard]] double EndStationWithEquations() const;
  [[nodiscard]] Point2 StartPoint() const;
  [[nodiscard]] Point2 EndPoint() const;
  [[nodiscard]] double StartAzimuth() const;
  [[nodiscard]] double EndAzimuth() const;

  // --- queries (REQ-484.1). Internal stations, clamped to the alignment. ---
  [[nodiscard]] Point2 GetPointAtStation(double internalStation) const;
  [[nodiscard]] double GetBearingAtStation(double internalStation) const;  ///< Azimuth, radians.
  [[nodiscard]] Point2 GetTangentAtStation(double internalStation) const;  ///< Unit direction vector.
  /// Signed curvature, 1/units; positive = curving right.
  [[nodiscard]] double GetCurvatureAtStation(double internalStation) const;
  /// Index of the entity containing the station; the earlier entity wins at a junction.
  [[nodiscard]] int GetEntityAtStation(double internalStation) const;
  [[nodiscard]] StationOffset GetStationOffset(double x, double y) const;
  [[nodiscard]] Point2 GetPointFromStationOffset(double internalStation, double offset,
                                                 double* azimuthOut = nullptr) const;
  [[nodiscard]] Point2 GetClosestPoint(double x, double y) const;
  [[nodiscard]] double GetDistanceAlongAlignment(double x, double y) const;  ///< Distance from start.

  // --- station equations (REQ-483) ---
  /// Adds an equation, keeping the list ordered by rawBack. Refuses one outside the alignment's raw
  /// range, a duplicate location, or a direction that contradicts increasing/decreasing.
  bool AddStationEquation(const StationEquation& eq, std::string* whyNot = nullptr);
  /// Station shown at `rawStation`. At exactly an equation point, `ahead` selects the side
  /// (true = the "ahead" value, false = the "back" value).
  [[nodiscard]] double DisplayedStation(double rawStation, bool ahead = true) const;
  /// Every raw station whose displayed value equals `displayed`, ascending. More than one result
  /// is possible across a decreasing equation (REQ-483.3).
  [[nodiscard]] std::vector<double> RawStationsForDisplayed(double displayed) const;
  /// Change to the displayed value across equation `i` (ahead - back).
  [[nodiscard]] double EquationDifference(int i) const;

  // --- derived data ---
  [[nodiscard]] bool CurveDataFor(int entityIndex, CurveData* out) const;
  [[nodiscard]] std::vector<GeometryPoint> GeometryPoints() const;
  [[nodiscard]] std::vector<ContinuityIssue> CheckContinuity(ContinuityTolerance tol = {}) const;
  [[nodiscard]] int CurveCount() const;
  /// Axis-aligned bounds of the true geometry (arcs bounded by their extreme points). False when empty.
  [[nodiscard]] bool Bounds(Point2* mn, Point2* mx) const;

  /// Display-only sampling for rendering (REQ-495.1): chord height never exceeds `maxChordHeight`.
  /// Never used by a query or export of geometry.
  [[nodiscard]] std::vector<Point2> SampleForDisplay(double maxChordHeight) const;

 private:
  void Rebuild();
  [[nodiscard]] Point2 PointInEntity(int i, double s) const;      ///< s = distance from entity start.
  [[nodiscard]] double AzimuthInEntity(int i, double s) const;
  [[nodiscard]] double SignedCurvature(const Entity& e) const;

  std::vector<Entity> entities_;
  std::vector<double> cumStart_;  ///< Distance from alignment start to each entity start (size n+1).
  double length_ = 0.0;
};

// --- Station formatting (REQ-483.4) ---
struct StationFormat {
  char delimiter = '+';     ///< '+', '-', '_', '/', 'C', 'K', or '\0' for none.
  int digitsRight = 2;      ///< Digits to the right of the delimiter before the decimals: 2 => 12+45, 3 => 1+245.
  int decimals = 2;
  std::string prefix;       ///< e.g. "STA ".
  std::string suffix;
};

[[nodiscard]] std::string FormatStation(double station, const StationFormat& fmt = {});
/// Parses "12+45.32", "STA 12+45.32", "1245.32". False on malformed input.
[[nodiscard]] bool ParseStation(const std::string& text, double* station, const StationFormat& fmt = {});

// --- Curve helpers (closed forms; shared with tests and the layout editor) ---
[[nodiscard]] CurveData ComputeCurveData(Point2 pc, double startAzimuth, double radius, double arcLength, bool right);

// --- Create from objects (REQ-485.2) ---
struct SourceSegment {
  bool isArc = false;
  Point2 a;              ///< Line: start. Arc: unused.
  Point2 b;              ///< Line: end.   Arc: unused.
  Point2 center;         ///< Arc only.
  double radius = 0.0;   ///< Arc only.
  double startAngle = 0.0;  ///< Arc only: math angle, radians CCW from +X (CAD ARC convention).
  double endAngle = 0.0;    ///< Arc only: CCW end angle.
  int sourceIndex = 0;      ///< For error messages (caller's object number).
};

struct FromObjectsOptions {
  double gapTolerance = 0.001;  ///< Larger gaps are refused (REQ-485.3).
  bool hasPick = false;         ///< Direction starts from the chain end nearest the pick.
  Point2 pick;
  bool reverse = false;         ///< The "R" prompt.
  bool addFillets = false;      ///< Fillet tangent-tangent corners (REQ-485.2).
  double filletRadius = 100.0;
};

/// Chains the segments into `out`. On failure returns false with a message naming the first
/// offending object and the gap size, and leaves `out` empty. Polyline bulge segments must be
/// passed as arcs by the caller.
[[nodiscard]] bool BuildFromSegments(const std::vector<SourceSegment>& segments, const FromObjectsOptions& opt,
                                     Alignment* out, std::string* error,
                                     std::vector<std::string>* warnings = nullptr);

/// Converts a bulge segment (A to B with DXF bulge) to a SourceSegment arc. A bulge of 0 is a line.
[[nodiscard]] SourceSegment SegmentFromBulge(Point2 a, Point2 b, double bulge, int sourceIndex);

}  // namespace alignment
