#pragma once

/// Entity handles, capability declarations, 3D bounds and 3D similarity transforms for
/// Document-owned entity-kind operations (ADR-0011 / GitHub issue #1577 phase 1).
///
/// Pure and dependency-free beyond `GoSurvey::Math`: no `CadEntities.hpp`, no `CadCommands.hpp`,
/// no Qt, no GL. Consumers linking only `GoSurvey::Document` can use every symbol declared here.

#include <GoSurvey/Math/ray3d.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

/// Which entity array a \ref EntityRef designates. Mirrors SelectedEntity::Type for the kinds that
/// carry an EntityAttributes, which is exactly the set REQ-076 gives an id.
///
/// Moved from `CadCommands.hpp` into Document (ADR-0011 / #1577 phase 1). Kept at global scope so
/// existing call sites stay source-compatible; also re-exported inside `GoSurvey::Document`.
enum class EntityKind : std::uint8_t {
  Line = 0,
  Circle,
  Arc,
  Ellipse,
  Polyline,
  Annotation,
  FilledRegion,
  Mesh,
  FeatureLine,  ///< REQ-087. Appended, never inserted — the value is not persisted, but reordering
                ///< would still silently change every switch that lists kinds in order.
  /// REQ-068 / ADR-036 (a). Appended for a second, sharper reason than the one above: id assignment
  /// walks the attribute arrays in `kEntityKindsInSweepOrder`, so inserting Surface anywhere but the
  /// end would renumber every entity in every existing drawing on its next load — and REQ-069's
  /// breakline and boundary references are stored by exactly those ids. Appending is what keeps a
  /// legacy `internal document payload` loading with the ids it loaded with yesterday.
  Surface,
  /// REQ-148 / D-2026-08-28-i. Appended after Surface so the id sweep does not renumber legacy drawings.
  Table,
  /// GitHub issue #124. Appended after Table so the id sweep does not renumber legacy drawings.
  BlockRef,
  /// REQ-313 / ADR-045. Appended after BlockRef, for the reason Surface's note above spells out:
  /// the id sweep walks the attribute arrays in `kEntityKindsInSweepOrder`, so inserting anywhere
  /// but the end would renumber every entity in every existing drawing on its next load.
  Solid,
  /// REQ-171 / ADR-042. Appended after Solid, for the same reason: inserting anywhere but the end
  /// would renumber every entity in every existing drawing on its next load.
  PointCloud,
  /// REQ-359 / D-2026-09-29-e. Appended after PointCloud for the same id-sweep reason.
  PositionMarker,
  /// REQ-367 / issue #619. Appended after PositionMarker for the same id-sweep reason.
  Multileader,
  /// REQ-443 / issue #803. Appended after Multileader for the same id-sweep reason.
  Spline,
  /// REQ-456 / issue #1019. Appended after Spline for the same id-sweep reason.
  Point,
  /// Issue #486 / ADR-0011. Appended after Point so existing enumerators and id-sweep order are untouched.
  PipeRun
};

/// The result of resolving a stable id (REQ-076), and the handle Document entity-kind operations
/// accept (ADR-0011): which array, and the index *at this moment*.
///
/// The index is deliberately a **return value or transient handle, not something you persist** —
/// it is valid only until the next erase, which is the whole reason ids exist (architecture §11.9).
/// Store the id; resolve when you need to touch the entity.
struct EntityRef {
  EntityKind kind = EntityKind::Line;
  int index = -1;  ///< -1 = invalid / does not resolve (erased, or never existed).

  [[nodiscard]] constexpr bool valid() const { return index >= 0; }
  [[nodiscard]] constexpr bool operator==(const EntityRef& o) const {
    return kind == o.kind && index == o.index;
  }
  [[nodiscard]] constexpr bool operator!=(const EntityRef& o) const { return !(*this == o); }
};

namespace GoSurvey::Document {

using ::EntityKind;
using ::EntityRef;

/// Every enumerator of \ref EntityKind in declaration order.
inline constexpr std::array<EntityKind, 19> kAllEntityKinds = {
    EntityKind::Line,        EntityKind::Circle,         EntityKind::Arc,        EntityKind::Ellipse,
    EntityKind::Polyline,    EntityKind::Annotation,     EntityKind::FilledRegion, EntityKind::Mesh,
    EntityKind::FeatureLine, EntityKind::Surface,        EntityKind::Table,      EntityKind::BlockRef,
    EntityKind::Solid,       EntityKind::PointCloud,     EntityKind::PositionMarker,
    EntityKind::Multileader, EntityKind::Spline,         EntityKind::Point,      EntityKind::PipeRun};

/// Every \ref EntityKind whose storage lives in `CadDrawingEntityStore` (Document-owned).
/// Non-Document kinds (`FeatureLine`, `Surface`, `PointCloud`) stay in `DrawingContent` and are
/// added at the Commands boundary.
inline constexpr std::array<EntityKind, 16> kDocumentEntityKinds = {
    EntityKind::Line,           EntityKind::Circle,       EntityKind::Arc,         EntityKind::Ellipse,
    EntityKind::Polyline,       EntityKind::Annotation,   EntityKind::FilledRegion, EntityKind::Mesh,
    EntityKind::Table,          EntityKind::BlockRef,     EntityKind::Solid,       EntityKind::PositionMarker,
    EntityKind::Multileader,    EntityKind::Spline,       EntityKind::Point,       EntityKind::PipeRun};

/// Every \ref EntityKind whose storage lives in the block-capable core `CadEntityStore`.
inline constexpr std::array<EntityKind, 8> kBlockStoreEntityKinds = {
    EntityKind::Line,     EntityKind::Circle,     EntityKind::Arc,  EntityKind::Ellipse,
    EntityKind::Polyline, EntityKind::Annotation, EntityKind::Mesh, EntityKind::Solid};

/// Operations whose per-kind support is declared in \ref EntityKindCapabilities (ADR-0011).
enum class EntityOp : std::uint8_t {
  Move = 0,
  Copy,
  RotatePlan,
  Rotate3d,
  Scale,
  Mirror,
  Stretch,
  Array,
  Erase
};

/// Declared operation support and stated refusal reasons for one \ref EntityKind (ADR-0011).
///
/// A kind that does not support an operation MUST provide a non-empty refusal reason so callers
/// never skip it silently (REQ-201). Enforced at compile time below.
struct EntityKindCapabilities {
  std::string_view displayName;
  std::string_view pluralName;
  bool inEntityStore = false;
  bool inBlockStore = false;
  bool movable = false;
  bool duplicable = false;
  bool planRotatable = false;
  bool tiltRotatable = false;
  bool scalable = false;
  bool mirrorable = false;
  bool stretchable = false;
  bool erasable = false;
  std::string_view transformRefusalReason;
  std::string_view tiltRotateRefusalReason;
  std::string_view mirrorRefusalReason;
  std::string_view stretchRefusalReason;
};

/// Result of asking whether an \ref EntityKind supports an \ref EntityOp.
struct OpSupport {
  bool supported = false;
  std::string_view refusalReason;  ///< Non-empty whenever `!supported`.
};

[[nodiscard]] constexpr EntityKindCapabilities CapabilitiesForKind(EntityKind kind) {
  constexpr std::string_view kTiltedPlaneReason =
      "rotation about a tilted axis is not supported yet — none stores a plane normal to tip. "
      "Rotate about a UCS with an upright Z axis.";
  constexpr std::string_view kPositionMarkerReason =
      "a Position Marker can only be moved, copied or erased.";
  constexpr std::string_view kNoPlaneNormalReason =
      "it stores no plane normal, so it cannot be tipped out of plan yet. Use a UCS with an upright Z axis.";
  constexpr std::string_view kSurfaceReason =
      "a surface's shape comes from its definition, so moving it would be undone by the next rebuild. "
      "Edit its definition in the Surfaces panel instead.";
  constexpr std::string_view kPointCloudReason =
      "point clouds are reference-only geometry and cannot be transformed.";

  switch (kind) {
  case EntityKind::Line:
    return {"line", "line(s)", true, true, true, true, true, true, true, true, true, true, {}, {}, {}, {}};
  case EntityKind::Circle:
    return {"circle", "circle(s)", true, true, true, true, true, true, true, true, true, true, {}, {}, {}, {}};
  case EntityKind::Arc:
    return {"arc", "arc(s)", true, true, true, true, true, true, true, true, true, true, {}, {}, {}, {}};
  case EntityKind::Ellipse:
    return {"ellipse", "ellipse(s)", true, true, true, true, true, true, true, true, true, true, {}, {}, {}, {}};
  case EntityKind::Polyline:
    return {"polyline", "polyline(s)", true, true, true, true, true, true, true, true, true, true,
            {}, {}, {}, {}};
  case EntityKind::Annotation:
    return {"annotation", "annotation(s)", true, true, true, true, true, false, true, true, true, true,
            {}, kNoPlaneNormalReason, {}, {}};
  case EntityKind::FilledRegion:
    return {"hatch fill", "hatch fill(s)", true, false, true, true, true, true, true, true, true, true, {}, {}, {}, {}};
  case EntityKind::Mesh:
    return {"imported model", "imported model(s)", true, true, true, true, true, true, true, true, false, true,
            {}, {}, {}, "an imported model can only be moved whole. Use MOVE."};
  case EntityKind::FeatureLine:
    return {"feature line", "feature line(s)", false, false, true, true, true, false, true, true, true, true,
            {}, kTiltedPlaneReason, {}, {}};
  case EntityKind::Surface:
    return {"surface", "surface(s)", false, false, false, false, false, false, false, false, false, true,
            kSurfaceReason, kSurfaceReason, kSurfaceReason, kSurfaceReason};
  case EntityKind::Table:
    return {"table", "table(s)", true, false, true, true, true, false, true, true, true, true,
            {}, kNoPlaneNormalReason, {}, {}};
  case EntityKind::BlockRef:
    return {"block reference", "block reference(s)", true, false, true, true, true, true, true, true, false, true,
            {}, {}, {},
            "a block reference can only be moved whole; explode or edit the block definition to stretch it."};
  case EntityKind::Solid:
    return {"solid", "solid(s)", true, true, true, true, true, true, true, true, false, true,
            {}, {}, {},
            "a solid can only be moved whole. Use MOVE, or edit its faces and edges directly."};
  case EntityKind::PointCloud:
    return {"point cloud", "point cloud(s)", false, false, false, false, false, false, false, false, false, true,
            kPointCloudReason, kPointCloudReason, kPointCloudReason, kPointCloudReason};
  case EntityKind::PositionMarker:
    return {"Position Marker", "Position Marker(s)", true, false, true, true, false, false, false, false, false, true,
            kPositionMarkerReason, kPositionMarkerReason, kPositionMarkerReason, kPositionMarkerReason};
  case EntityKind::Multileader:
    return {"Multileader", "Multileader(s)", true, false, true, true, true, false, true, true, false, true,
            {}, kNoPlaneNormalReason, {}, "stretching a Multileader is not supported yet; move its grips instead."};
  case EntityKind::Spline:
    return {"spline", "spline(s)", true, false, true, true, true, true, true, true, false, true,
            {}, {}, {},
            "splines cannot be stretched with STRETCH; edit control or fit points instead."};
  case EntityKind::Point:
    return {"point", "point(s)", true, false, true, true, true, true, true, true, true, true, {}, {}, {}, {}};
  case EntityKind::PipeRun:
    return {"pipe run", "pipe run(s)", true, false, true, true, true, true, true, true, false, true,
            {}, {}, {},
            "a pipe run can only be moved whole. Use MOVE."};
  }
  return {"entity", "entity(ies)", false, false, false, false, false, false, false, false, false, false,
          "unknown entity kind.", "unknown entity kind.", "unknown entity kind.", "unknown entity kind."};
}

[[nodiscard]] constexpr OpSupport CheckOpSupport(EntityKind kind, EntityOp op) {
  const EntityKindCapabilities cap = CapabilitiesForKind(kind);
  auto pickReason = [](std::string_view primary, std::string_view fallback) -> std::string_view {
    return !primary.empty() ? primary : fallback;
  };
  switch (op) {
  case EntityOp::Move:
    return cap.movable ? OpSupport{true, {}} : OpSupport{false, cap.transformRefusalReason};
  case EntityOp::Copy:
    return (cap.movable && cap.duplicable) ? OpSupport{true, {}}
                                           : OpSupport{false, cap.transformRefusalReason};
  case EntityOp::RotatePlan:
    return cap.planRotatable ? OpSupport{true, {}} : OpSupport{false, cap.transformRefusalReason};
  case EntityOp::Rotate3d:
    return cap.tiltRotatable
               ? OpSupport{true, {}}
               : OpSupport{false, pickReason(cap.tiltRotateRefusalReason, cap.transformRefusalReason)};
  case EntityOp::Scale:
    return cap.scalable ? OpSupport{true, {}} : OpSupport{false, cap.transformRefusalReason};
  case EntityOp::Mirror:
    return cap.mirrorable
               ? OpSupport{true, {}}
               : OpSupport{false, pickReason(cap.mirrorRefusalReason, cap.transformRefusalReason)};
  case EntityOp::Stretch:
    return cap.stretchable
               ? OpSupport{true, {}}
               : OpSupport{false, pickReason(cap.stretchRefusalReason, cap.transformRefusalReason)};
  case EntityOp::Array:
    return (cap.movable && cap.duplicable && cap.planRotatable)
               ? OpSupport{true, {}}
               : OpSupport{false, cap.transformRefusalReason};
  case EntityOp::Erase:
    return cap.erasable ? OpSupport{true, {}} : OpSupport{false, cap.transformRefusalReason};
  }
  return {false, "unknown operation."};
}

namespace detail {

[[nodiscard]] constexpr bool AllCapabilitiesHaveRefusalReasons() {
  constexpr std::array<EntityOp, 9> kOps = {
      EntityOp::Move,   EntityOp::Copy,    EntityOp::RotatePlan, EntityOp::Rotate3d, EntityOp::Scale,
      EntityOp::Mirror, EntityOp::Stretch, EntityOp::Array,      EntityOp::Erase};
  for (EntityKind k : kAllEntityKinds) {
    const EntityKindCapabilities cap = CapabilitiesForKind(k);
    if (cap.displayName.empty() || cap.pluralName.empty())
      return false;
    for (EntityOp op : kOps) {
      const OpSupport s = CheckOpSupport(k, op);
      if (!s.supported && s.refusalReason.empty())
        return false;
    }
  }
  return true;
}

static_assert(AllCapabilitiesHaveRefusalReasons(),
              "Every unsupported (EntityKind, EntityOp) pair must declare a non-empty refusal reason.");

[[nodiscard]] inline bool VecFinite(const ray3d::Vec3& v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace detail

/// Axis-aligned 3D bounding box in `double` world/storage coordinates.
struct EntityBounds3d {
  bool valid = false;
  ray3d::Vec3 min{0.0, 0.0, 0.0};
  ray3d::Vec3 max{0.0, 0.0, 0.0};

  void expand(const ray3d::Vec3& p) {
    if (!detail::VecFinite(p))
      return;
    if (!valid) {
      min = max = p;
      valid = true;
      return;
    }
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    min.z = std::min(min.z, p.z);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
    max.z = std::max(max.z, p.z);
  }

  void merge(const EntityBounds3d& other) {
    if (!other.valid)
      return;
    expand(other.min);
    expand(other.max);
  }

  [[nodiscard]] ray3d::Vec3 center() const {
    if (!valid)
      return {0.0, 0.0, 0.0};
    return {0.5 * (min.x + max.x), 0.5 * (min.y + max.y), 0.5 * (min.z + max.z)};
  }
};

/// 3D similarity transform in `double`: rotation about any axis, uniform positive scale,
/// translation, and reflection across a plane (ADR-0011, GLOSSARY.md).
///
/// Representation:
///   T(p) = pivot + scale * Q * (p - pivot) + translation
/// where `scale > 0` and `Q` is an orthogonal 3x3 matrix (`Q^T Q = I`, `det(Q) = +1` for proper
/// motions or `-1` for reflections).
///
/// Keeping `pivot` explicit preserves precision at state-plane coordinate magnitudes (~2e6 ft)
/// without catastrophic cancellation. Shear and non-uniform scale are unrepresentable by
/// construction: there is no arbitrary matrix constructor.
class SimilarityTransform {
 public:
  SimilarityTransform() = default;

  [[nodiscard]] static SimilarityTransform Identity() { return SimilarityTransform{}; }

  [[nodiscard]] static SimilarityTransform Invalid() {
    SimilarityTransform xf;
    xf.valid_ = false;
    return xf;
  }

  [[nodiscard]] static SimilarityTransform Translation(const ray3d::Vec3& delta) {
    if (!detail::VecFinite(delta))
      return Invalid();
    SimilarityTransform xf;
    xf.translation_ = delta;
    return xf;
  }

  [[nodiscard]] static SimilarityTransform RotationAboutAxis(const ray3d::Vec3& axisPoint,
                                                             const ray3d::Vec3& axis,
                                                             double angleRad) {
    if (!detail::VecFinite(axisPoint) || !detail::VecFinite(axis) || !std::isfinite(angleRad))
      return Invalid();
    const ray3d::Vec3 u = ray3d::Normalize(axis);
    if (ray3d::Length(u) < 0.5)
      return Invalid();

    SimilarityTransform xf;
    xf.pivot_ = axisPoint;
    if (angleRad == 0.0)
      return xf;

    // Rodrigues rotation matrix R(u, theta): columns are R(e_0), R(e_1), R(e_2).
    const ray3d::Vec3 c0 = ray3d::RotateVectorAboutAxis({1.0, 0.0, 0.0}, u, angleRad);
    const ray3d::Vec3 c1 = ray3d::RotateVectorAboutAxis({0.0, 1.0, 0.0}, u, angleRad);
    const ray3d::Vec3 c2 = ray3d::RotateVectorAboutAxis({0.0, 0.0, 1.0}, u, angleRad);
    xf.setColumns(c0, c1, c2);
    xf.identityRot_ = isMatrixIdentity(xf.q_);
    return xf;
  }

  [[nodiscard]] static SimilarityTransform RotationZ(double bx, double by, double angleRad) {
    return RotationAboutAxis({bx, by, 0.0}, {0.0, 0.0, 1.0}, angleRad);
  }

  [[nodiscard]] static SimilarityTransform UniformScale(const ray3d::Vec3& basePoint, double factor) {
    if (!detail::VecFinite(basePoint) || !std::isfinite(factor) || !(factor > 0.0))
      return Invalid();
    SimilarityTransform xf;
    xf.pivot_ = basePoint;
    xf.scale_ = factor;
    return xf;
  }

  [[nodiscard]] static SimilarityTransform ReflectionAcrossPlane(const ray3d::Vec3& planePoint,
                                                                 const ray3d::Vec3& planeNormal) {
    if (!detail::VecFinite(planePoint) || !detail::VecFinite(planeNormal))
      return Invalid();
    const ray3d::Vec3 n = ray3d::Normalize(planeNormal);
    if (ray3d::Length(n) < 0.5)
      return Invalid();

    SimilarityTransform xf;
    xf.pivot_ = planePoint;
    xf.reflected_ = true;
    xf.identityRot_ = false;
    // Householder matrix H = I - 2 n n^T.
    const ray3d::Vec3 c0 = ray3d::ReflectVectorAcrossPlane({1.0, 0.0, 0.0}, n);
    const ray3d::Vec3 c1 = ray3d::ReflectVectorAcrossPlane({0.0, 1.0, 0.0}, n);
    const ray3d::Vec3 c2 = ray3d::ReflectVectorAcrossPlane({0.0, 0.0, 1.0}, n);
    xf.setColumns(c0, c1, c2);
    return xf;
  }

  /// Vertical mirror plane containing the 2D line `(x0, y0) -> (x1, y1)`.
  [[nodiscard]] static SimilarityTransform ReflectionAcrossLineXY(double x0, double y0, double x1,
                                                                  double y1) {
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
      return Invalid();
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double len = std::hypot(dx, dy);
    if (!(len > 1e-12))
      return Invalid();
    return ReflectionAcrossPlane({x0, y0, 0.0}, {-dy / len, dx / len, 0.0});
  }

  /// Composition `outer(inner(p))`.
  [[nodiscard]] static SimilarityTransform Compose(const SimilarityTransform& outer,
                                                   const SimilarityTransform& inner) {
    if (!outer.valid_ || !inner.valid_)
      return Invalid();
    const double newScale = outer.scale_ * inner.scale_;
    if (!std::isfinite(newScale) || !(newScale > 0.0))
      return Invalid();

    SimilarityTransform out;
    out.pivot_ = inner.pivot_;
    out.scale_ = newScale;
    out.reflected_ = (outer.reflected_ != inner.reflected_);
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        out.q_[r][c] = outer.q_[r][0] * inner.q_[0][c] + outer.q_[r][1] * inner.q_[1][c] +
                       outer.q_[r][2] * inner.q_[2][c];
      }
    }
    // outer(inner(p)) = p2 + s2 Q2 (p1 + s1 Q1 (p - p1) + t1 - p2) + t2
    //                 = p1 + (s2 s1)(Q2 Q1)(p - p1) + [(p2 - p1) + s2 Q2 ((p1 - p2) + t1) + t2]
    const ray3d::Vec3 dp = ray3d::Sub(inner.pivot_, outer.pivot_);
    const ray3d::Vec3 shifted = ray3d::Add(dp, inner.translation_);
    const ray3d::Vec3 mapped = ray3d::Scale(outer.mulQ(shifted), outer.scale_);
    out.translation_ = ray3d::Add(ray3d::Sub(outer.pivot_, inner.pivot_),
                                  ray3d::Add(mapped, outer.translation_));
    if (!detail::VecFinite(out.translation_))
      return Invalid();
    out.identityRot_ = !out.reflected_ && isMatrixIdentity(out.q_);
    return out;
  }

  [[nodiscard]] SimilarityTransform inverse() const {
    if (!valid_)
      return Invalid();
    const double invScale = 1.0 / scale_;
    if (!std::isfinite(invScale) || !(invScale > 0.0))
      return Invalid();

    SimilarityTransform inv;
    inv.pivot_ = pivot_;
    inv.scale_ = invScale;
    inv.reflected_ = reflected_;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        inv.q_[r][c] = q_[c][r];  // Q^-1 = Q^T
    inv.translation_ = ray3d::Scale(inv.mulQ(translation_), -invScale);
    if (!detail::VecFinite(inv.translation_))
      return Invalid();
    inv.identityRot_ = !inv.reflected_ && isMatrixIdentity(inv.q_);
    return inv;
  }

  [[nodiscard]] bool valid() const { return valid_; }
  [[nodiscard]] double scale() const { return scale_; }
  [[nodiscard]] bool isReflection() const { return reflected_; }
  [[nodiscard]] bool isIdentityRotation() const { return identityRot_; }
  [[nodiscard]] bool isTranslationOnly() const { return identityRot_ && scale_ == 1.0; }
  [[nodiscard]] bool isIdentity(double tol = 1e-12) const {
    return valid_ && identityRot_ && std::fabs(scale_ - 1.0) <= tol &&
           ray3d::Length(translation_) <= tol;
  }

  /// True when the orthogonal part preserves world +Z (within \p tol) — i.e., pure translation,
  /// uniform scale, plan rotation about world Z, or reflection across a vertical plane.
  [[nodiscard]] bool isPlanarZ(double tol = 1e-9) const {
    if (!valid_)
      return false;
    return std::fabs(q_[0][2]) <= tol && std::fabs(q_[1][2]) <= tol &&
           std::fabs(q_[2][2] - 1.0) <= tol;
  }

  /// Transform a 3D point: `pivot + scale * Q * (p - pivot) + translation`.
  [[nodiscard]] ray3d::Vec3 applyPoint(const ray3d::Vec3& p) const {
    const ray3d::Vec3 d = ray3d::Sub(p, pivot_);
    return ray3d::Add(pivot_, ray3d::Add(ray3d::Scale(mulQ(d), scale_), translation_));
  }

  /// Transform a 3D displacement or axis vector (scales and rotates/reflects; ignores translation).
  [[nodiscard]] ray3d::Vec3 applyVector(const ray3d::Vec3& v) const {
    return ray3d::Scale(mulQ(v), scale_);
  }

  /// Transform a unit direction or plane normal (rotates/reflects and normalizes; ignores scale
  /// and translation). Returns `{0,0,0}` on a degenerate input.
  [[nodiscard]] ray3d::Vec3 applyUnitDirection(const ray3d::Vec3& u) const {
    return ray3d::Normalize(mulQ(u));
  }

  /// Entry (\p row, \p col) of the orthogonal part `Q` (both in `[0, 2]`). Entity-kind operations read
  /// it to carry an orientation that is not a point — an insert's Euler angles, a text angle in plan.
  [[nodiscard]] double orthogonal(int row, int col) const { return q_[row][col]; }

  /// Determinant of the orthogonal part `Q`: `+1` for proper motions, `-1` for reflections.
  [[nodiscard]] double orthogonalDeterminant() const {
    return q_[0][0] * (q_[1][1] * q_[2][2] - q_[1][2] * q_[2][1]) -
           q_[0][1] * (q_[1][0] * q_[2][2] - q_[1][2] * q_[2][0]) +
           q_[0][2] * (q_[1][0] * q_[2][1] - q_[1][1] * q_[2][0]);
  }

  /// Signed volume scale factor `det(s * Q) = s^3 * det(Q)`.
  [[nodiscard]] double determinant() const {
    return scale_ * scale_ * scale_ * (reflected_ ? -1.0 : 1.0);
  }

 private:
  [[nodiscard]] ray3d::Vec3 mulQ(const ray3d::Vec3& v) const {
    return {q_[0][0] * v.x + q_[0][1] * v.y + q_[0][2] * v.z,
            q_[1][0] * v.x + q_[1][1] * v.y + q_[1][2] * v.z,
            q_[2][0] * v.x + q_[2][1] * v.y + q_[2][2] * v.z};
  }

  void setColumns(const ray3d::Vec3& c0, const ray3d::Vec3& c1, const ray3d::Vec3& c2) {
    q_[0][0] = c0.x; q_[0][1] = c1.x; q_[0][2] = c2.x;
    q_[1][0] = c0.y; q_[1][1] = c1.y; q_[1][2] = c2.y;
    q_[2][0] = c0.z; q_[2][1] = c1.z; q_[2][2] = c2.z;
  }

  [[nodiscard]] static bool isMatrixIdentity(const double m[3][3], double tol = 1e-12) {
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        if (std::fabs(m[r][c] - (r == c ? 1.0 : 0.0)) > tol)
          return false;
    return true;
  }

  bool valid_ = true;
  bool reflected_ = false;
  bool identityRot_ = true;
  double scale_ = 1.0;
  double q_[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
  ray3d::Vec3 pivot_{0.0, 0.0, 0.0};
  ray3d::Vec3 translation_{0.0, 0.0, 0.0};
};

}  // namespace GoSurvey::Document
