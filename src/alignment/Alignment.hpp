#pragma once

#include <cstdint>
#include <string>
#include <string_view>
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

/// Phase 1 ships Line and Arc; clothoid Spiral in Phase 3 (REQ-481.3).
enum class EntityKind { Line, Arc, Spiral };

/// One exact sub-entity. Stored parameters are the ONLY geometry; every other value is derived.
struct Entity {
  EntityKind kind = EntityKind::Line;
  Point2 start;               ///< Start coordinate, drawing-local.
  double startAzimuth = 0.0;  ///< Direction of travel at the start, radians clockwise from north.
  double length = 0.0;        ///< Arc length along the entity (> 0).
  double radius = 0.0;        ///< Arc: curvature radius (> 0). Spiral: end radius (0 = tangent / infinite).
  double startRadius = 0.0;   ///< Spiral only: start radius (0 = tangent / infinite).
  bool right = true;          ///< Arc and spiral: true = curves right (clockwise), false = left.
  /// Constraint state (REQ-481.6, REQ-486.2). Fixed keeps its own parameters; FloatOnPrev / FloatOnNext stay tangent to
  /// the entity they are attached to and move with it; Free (an arc between two tangents) re-solves between its
  /// neighbours. The first entity is Fixed. The integer values are persisted - never reorder.
  enum class Constraint { Fixed = 0, FloatOnPrev = 1, FloatOnNext = 2, Free = 3 } constraint = Constraint::Fixed;
  /// REQ-486.3 parameter locks: an edit that would change a locked value is refused, naming the row.
  static constexpr unsigned char kLockRadius = 1;   ///< Arc radius.
  static constexpr unsigned char kLockLength = 2;   ///< Line length or arc length.
  static constexpr unsigned char kLockBearing = 4;  ///< Line start azimuth.
  unsigned char locks = 0;
};

/// Which stored parameter an edit changes (REQ-486.1). Derived values (delta, chord, ...) are edited by
/// converting to one of these, exactly as the layout parameters table does.
enum class EditParam {
  LineAzimuth,
  LineLength,
  CurveRadius,
  CurveLength,
  CurveDelta,
  CurveDirection,
  SpiralStartRadius,
  Constraint,
  Locks
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
  TanSpiral,   ///< TS: tangent to spiral.
  SpiralTan,   ///< ST: spiral to tangent.
  CurveSpiral, ///< CS: curve to spiral.
  SpiralCurve, ///< SC: spiral to curve.
  SpiralSpiral,///< SS: spiral to spiral.
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

/// Selected continuity requirement for warnings (REQ-482). Default C1.
enum class ContinuityRequirement { C0 = 0, C1 = 1, C2 = 2 };

/// A continuity finding between two consecutive entities (REQ-482).
struct ContinuityIssue {
  int afterEntity = 0;          ///< Index of the entity BEFORE the junction.
  bool c0 = true;               ///< Position continuity holds.
  bool c1 = true;               ///< Direction continuity holds.
  bool c2 = true;               ///< Curvature continuity (only meaningful when a spiral is at the junction).
  double gap = 0.0;             ///< End-to-start distance.
  double angleDelta = 0.0;      ///< Absolute azimuth difference, radians (0..pi).
  double curvatureGap = 0.0;    ///< |κ_end − κ_start| at the junction.
  /// True when this junction fails the alignment's `continuityRequirement` (C2 checks only spiral junctions).
  [[nodiscard]] bool FailsRequirement(ContinuityRequirement level) const;
};

struct ContinuityTolerance {
  double position = 1e-6;
  double angle = 1e-9;
  double curvature = 1e-9;
};

class Alignment {
 public:
  // --- identity and metadata (REQ-480.1) ---
  std::uint64_t id = 0;
  std::string name;
  std::string description;
  std::string layer = "0";
  std::string style = "Standard";
  std::string labelSet = "Standard";
  AlignmentType type = AlignmentType::Centerline;
  ContinuityRequirement continuityRequirement = ContinuityRequirement::C1;

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
  /// Clothoid spiral continuing from the previous end. `startRadius` / `radius` use 0 for infinite (tangent).
  bool AppendSpiral(double length, double startRadius, double endRadius, bool right);
  /// Appends an entity verbatim (used by create-from-objects and the DWG reader). Its own start is
  /// authoritative, so a non-tangent or gapped chain is representable and REQ-482 can report it.
  bool AppendRaw(const Entity& e);
  void Clear();
  // --- REQ-486 / 487: constraint-aware editing. Each call works on a copy and commits only if the whole chain
  //     re-solves, so a refused edit changes nothing. `whyNot` names the row and the reason. ---
  /// Edits one parameter of entity `i` and re-solves the chain: FloatOnPrev entities after it follow, FloatOnNext
  /// entities before it follow, Free arcs re-fit between their tangents. Editing a radius keeps the arc length;
  /// editing a delta keeps the radius (GoSurvey rules). Derived (floating) azimuths cannot be edited directly.
  bool EditEntity(int i, EditParam param, double value, std::string* whyNot = nullptr);
  /// Inserts a tangent / curve after entity `i` (continuing tangent-continuously, FloatOnPrev) and shifts what follows.
  bool InsertTangentAfter(int i, double length, std::string* whyNot = nullptr);
  bool InsertCurveAfter(int i, double radius, double arcLength, bool right, std::string* whyNot = nullptr);
  bool InsertSpiralAfter(int i, double length, double startRadius, double endRadius, bool right,
                         std::string* whyNot = nullptr);
  /// Deletes entity `i`; a following floating entity re-attaches to the one before it.
  bool DeleteEntity(int i, std::string* whyNot = nullptr);
  /// Whole-object transforms (REQ-487.1). Rotate is clockwise radians, like azimuths. Mirror reflects across the line
  /// through (ax,ay)-(bx,by) and swaps curve handedness. Scale refuses a non-positive factor.
  void Translate(double dx, double dy);
  void Rotate(double cx, double cy, double clockwiseRadians);
  bool Scale(double cx, double cy, double factor);
  bool Mirror(double ax, double ay, double bx, double by);
  /// Station equations beyond the end of the geometry after an edit (REQ-483.5). Removed; returns how many.
  int PruneOrphanEquations();

  // --- REQ-488: an Offset alignment is real geometry linked to its parent by id. `parentId` 0 = independent. ---
  std::uint64_t parentId = 0;
  double offsetDistance = 0.0;  ///< Signed, right of the parent positive.

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
  [[nodiscard]] int SpiralCount() const;
  /// Axis-aligned bounds of the true geometry (arcs bounded by their extreme points). False when empty.
  [[nodiscard]] bool Bounds(Point2* mn, Point2* mx) const;

  /// Display-only sampling for rendering (REQ-495.1): chord height never exceeds `maxChordHeight`.
  /// Never used by a query or export of geometry.
  [[nodiscard]] std::vector<Point2> SampleForDisplay(double maxChordHeight) const;
  /// Samples one entity for viewport display (REQ-491 per-segment styling).
  [[nodiscard]] std::vector<Point2> SampleEntityForDisplay(int entityIndex, double maxChordHeight) const;

 private:
  void Rebuild();
  /// Re-solves every floating / free relationship in the chain (REQ-486.2). False names the failing row.
  bool Resolve(std::string* whyNot);
  [[nodiscard]] Point2 PointInEntity(int i, double s) const;      ///< s = distance from entity start.
  [[nodiscard]] double AzimuthInEntity(int i, double s) const;
  [[nodiscard]] double SignedCurvature(const Entity& e) const;
  [[nodiscard]] double SignedCurvatureAt(const Entity& e, double s) const;

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

/// Derived clothoid values (REQ-481.3). `A` is √(R·Ls) when one side is tangent; deflection θ = Ls/(2R) in that case.
struct SpiralData {
  double length = 0.0;
  double startRadius = 0.0;
  double endRadius = 0.0;
  double parameterA = 0.0;
  double deflection = 0.0;  ///< Total heading change, radians.
  bool right = true;
};
[[nodiscard]] bool SpiralDataFor(int entityIndex, const Alignment& a, SpiralData* out);

[[nodiscard]] const char* ContinuityRequirementLabel(ContinuityRequirement level);
[[nodiscard]] bool ParseContinuityRequirement(std::string_view text, ContinuityRequirement* out);

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

// --- REQ-487.1 grips ---
/// One grip on a selected alignment. `which` is stable across a drag: 0 = the start point (moves the whole alignment),
/// 1 + 2*i = the END of tangent i (sets its length, and its bearing when it is Fixed), 2 + 2*i = the radius grip at the
/// MIDDLE of curve i (sets its radius).
struct Grip {
  Point2 point;
  int which = 0;
};
[[nodiscard]] std::vector<Grip> GripPoints(const Alignment& a);
/// Applies a drag of grip `which` to `to`, starting from the unmodified `orig`. Goes through the same solver as the layout
/// table, so a locked or underivable parameter simply does not move (the drag keeps the last valid shape). Returns false
/// when the grip does not exist; `out` is always a valid alignment (orig when nothing applies).
[[nodiscard]] bool DragGrip(const Alignment& orig, int which, Point2 to, Alignment* out);

// --- REQ-488 offsets ---
/// Builds the alignment `parent` offset by `distance` (right positive) into `out`: tangents become parallel tangents, curves
/// concentric (radius -/+ offset). The parent must be continuous (C0) and tangent-continuous (C1) - Autodesk trims or
/// extends at kinks; GoSurvey refuses rather than guess - and may not contain a curve whose offset radius would be <= 0.
/// `out` keeps its own name/layer/style/id; type becomes Offset and the link fields are set.
[[nodiscard]] bool MakeOffset(const Alignment& parent, double distance, Alignment* out, std::string* whyNot);

// --- REQ-489 intersections ---
struct Intersection {
  Point2 point;
  double stationA = 0.0;  ///< Internal station on the first alignment.
  double stationB = 0.0;  ///< Internal station on the second.
  double angle = 0.0;     ///< Crossing angle, radians 0..pi/2 (acute).
  bool tangential = false;
};
struct IntersectionOverlap {
  double fromStationA = 0.0, toStationA = 0.0;  ///< Coincident range along the first alignment.
};
/// Every intersection of two alignments (tangent/tangent, tangent/curve, curve/curve, touching). Coincident collinear
/// tangents are reported as ranges, not points.
[[nodiscard]] std::vector<Intersection> IntersectAlignments(const Alignment& a, const Alignment& b,
                                                            std::vector<IntersectionOverlap>* overlaps = nullptr);
/// Convenience for alignment/line and alignment/curve: wraps the other object as a one-entity alignment.
[[nodiscard]] Alignment MakeLineObject(Point2 a, Point2 b);
[[nodiscard]] Alignment MakeCircleObject(Point2 center, double radius, double startAngleCcw, double sweepCcw);

}  // namespace alignment
