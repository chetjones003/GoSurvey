// One transform path for MOVE, COPY, ROTATE, SCALE, MIRROR and ARRAY (ADR-0011 / issue #1577).
//
// Every whole-object transform command is "transform the selection by one similarity transform",
// and COPY-like commands are "duplicate, then transform the copies". The per-kind work lives in
// Document (GoSurvey/Document/EntityKindOps.hpp). This file maps the selection onto those
// operations, refuses what a kind cannot represent (and says why), and adds the kinds other systems
// own: survey points, alignments, PDF underlays and feature lines.

#include "CadCommandsInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "alignment/CadAlignmentCommands.hpp"

#include <GoSurvey/Document/EntityKindOps.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace doc = GoSurvey::Document;
using doc::EntityKindCapabilities;
using doc::EntityOp;
using doc::OpSupport;

std::optional<EntityKind> EntityKindForSelection(SelectedEntity::Type t) {
  using T = SelectedEntity::Type;
  switch (t) {
  case T::LineSeg:        return EntityKind::Line;
  case T::Circle:         return EntityKind::Circle;
  case T::Arc:            return EntityKind::Arc;
  case T::Ellipse:        return EntityKind::Ellipse;
  case T::Polyline:       return EntityKind::Polyline;
  case T::Annotation:     return EntityKind::Annotation;
  case T::FilledRegion:   return EntityKind::FilledRegion;
  case T::Mesh:           return EntityKind::Mesh;
  case T::FeatureLine:    return EntityKind::FeatureLine;
  case T::Surface:        return EntityKind::Surface;
  case T::Table:          return EntityKind::Table;
  case T::BlockRef:       return EntityKind::BlockRef;
  case T::Solid:          return EntityKind::Solid;
  case T::PipeRun:        return EntityKind::PipeRun;
  case T::PointCloud:     return EntityKind::PointCloud;
  case T::PositionMarker: return EntityKind::PositionMarker;
  case T::Multileader:    return EntityKind::Multileader;
  case T::Spline:         return EntityKind::Spline;
  case T::Point:          return EntityKind::Point;
  case T::PdfUnderlay:
  case T::SectionPlane:
  case T::Alignment:
    return std::nullopt;
  }
  return std::nullopt;
}

namespace {

constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;

constexpr std::string_view kPlanOnlyReason =
    "it stores no plane normal, so it cannot be tipped out of plan yet. Use a UCS with an upright Z axis.";
constexpr std::string_view kPdfMirrorReason = "an underlay's stored orientation cannot represent a reflection yet.";
constexpr std::string_view kPdfTiltReason = "an underlay lies in plan; it cannot be tipped out of plan.";
constexpr std::string_view kAlignmentTiltReason =
    "an alignment is horizontal; only a transform that keeps plan view can move it.";
constexpr std::string_view kAlignmentStretchReason =
    "stretching an alignment is not supported yet; edit its PIs instead.";

/// Why the selected entry \p t cannot take \p xf under \p op, and what to call it in the log. An
/// empty reason means it can.
struct Verdict {
  std::string_view plural;
  std::string_view reason;
};

Verdict JudgeSelected(SelectedEntity::Type t, EntityOp op, const doc::SimilarityTransform& xf) {
  using T = SelectedEntity::Type;
  if (t == T::PdfUnderlay) {
    if (op == EntityOp::Stretch)
      return {};
    if (!xf.isPlanarZ())
      return {"PDF underlay(s)", kPdfTiltReason};
    if (xf.isReflection())
      return {"PDF underlay(s)", kPdfMirrorReason};
    return {};
  }
  if (t == T::Alignment) {
    if (op == EntityOp::Stretch)
      return {"alignment(s)", kAlignmentStretchReason};
    if (!xf.isPlanarZ())
      return {"alignment(s)", kAlignmentTiltReason};
    return {};
  }
  const std::optional<EntityKind> kind = EntityKindForSelection(t);
  if (!kind)
    return {};
  const EntityKindCapabilities cap = doc::CapabilitiesForKind(*kind);
  const OpSupport support = doc::CheckOpSupport(*kind, op);
  if (!support.supported)
    return {cap.pluralName, support.refusalReason};
  if (op == EntityOp::Stretch || op == EntityOp::Erase)
    return {};
  if (*kind == EntityKind::FeatureLine)
    return xf.isPlanarZ() ? Verdict{} : Verdict{cap.pluralName, cap.tiltRotateRefusalReason};
  if (doc::HasEntityKindOps(*kind) && !doc::SupportsEntityTransform(*kind, xf)) {
    if (*kind == EntityKind::PositionMarker)
      return {cap.pluralName, cap.transformRefusalReason};
    return {cap.pluralName, kPlanOnlyReason};
  }
  return {};
}

template <class T>
void MapXY(const doc::SimilarityTransform& xf, T* x, T* y) {
  const ray3d::Vec3 q = xf.applyPoint({static_cast<double>(*x), static_cast<double>(*y), 0.0});
  *x = static_cast<T>(q.x);
  *y = static_cast<T>(q.y);
}

/// Plan rotation angle (or 2*phi for a reflection) of a transform that keeps world +Z.
double PlanAngle(const doc::SimilarityTransform& xf) { return std::atan2(xf.orthogonal(1, 0), xf.orthogonal(0, 0)); }

std::vector<int> UniqueSelectedIndices(const AppCommandState& st, SelectedEntity::Type type, size_t count) {
  std::set<int> ix;
  for (const SelectedEntity& e : st.selection)
    if (e.type == type && e.index >= 0 && static_cast<size_t>(e.index) < count)
      ix.insert(e.index);
  return {ix.begin(), ix.end()};
}

// --- PDF underlays (Pdf owns the type; its placement is an insertion, a rotation and a scale) -----

void TransformPdfUnderlay(PdfAttachment& att, const doc::SimilarityTransform& xf) {
  MapXY(xf, &att.insertX, &att.insertY);
  att.rotationDeg = static_cast<float>(static_cast<double>(att.rotationDeg) + PlanAngle(xf) * kRadToDeg);
  att.scale = std::max(static_cast<float>(static_cast<double>(att.scale) * xf.scale()), 1e-9f);
}

void TransformSelectedPdfUnderlays(AppCommandState& st, const doc::SimilarityTransform& xf, bool duplicate) {
  for (int i : UniqueSelectedIndices(st, SelectedEntity::Type::PdfUnderlay, st.drawing.pdfAttachments.size())) {
    if (!duplicate) {
      TransformPdfUnderlay(st.drawing.pdfAttachments[static_cast<size_t>(i)], xf);
      continue;
    }
    // A copy owns no GPU texture: it is rasterised again from the same file and page.
    PdfAttachment copy = st.drawing.pdfAttachments[static_cast<size_t>(i)];
    copy.glTexId = 0;
    copy.texW = 0;
    copy.texH = 0;
    copy.needsReload = true;
    TransformPdfUnderlay(copy, xf);
    st.drawing.pdfAttachments.push_back(std::move(copy));
  }
}

// --- Feature lines (Terrain's design linework, stored in DrawingContent) --------------------------

/// Every vertex of a feature line follows the transform in X, Y and Z, like the CAD linework beside
/// it, so a breakline drawn on a polyline stays on it through SCALE (issue #1577).
void TransformSelectedFeatureLines(AppCommandState& st, const doc::SimilarityTransform& xf, bool duplicate) {
  const auto mapVertex = [&](double* x, double* y, double* z) {
    const ray3d::Vec3 q = xf.applyPoint({*x, *y, *z});
    *x = q.x;
    *y = q.y;
    *z = q.z;
  };
  ForEachSelectedFeatureLine(st, [&](int fi, int v0, int v1) {
    if (duplicate) {
      const size_t before = st.drawing.featureLineVerts.size();
      AppendFeatureLineCopy(st, fi, v0, v1, [](auto*, auto*) {});
      for (size_t b = before; b + 2 < st.drawing.featureLineVerts.size(); b += 3)
        mapVertex(&st.drawing.featureLineVerts[b], &st.drawing.featureLineVerts[b + 1],
                  &st.drawing.featureLineVerts[b + 2]);
      return;
    }
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi) * 3;
      mapVertex(&st.drawing.featureLineVerts[b], &st.drawing.featureLineVerts[b + 1], &st.drawing.featureLineVerts[b + 2]);
    }
  });
}

// --- Alignments (Survey) --------------------------------------------------------------------------

/// A plan transform (\p xf keeps world +Z) applied to one alignment: the plan transform about the
/// origin, then the translation it leaves — reflect or rotate, scale, move. Alignment azimuths run
/// clockwise, so a counter-clockwise turn is negated.
bool TransformAlignment(alignment::Alignment& a, const doc::SimilarityTransform& xf) {
  const ray3d::Vec3 o = xf.applyPoint({0.0, 0.0, 0.0});
  const double angle = PlanAngle(xf);
  const double scale = xf.scale();
  if (xf.isReflection()) {
    const double phi = 0.5 * angle;
    if (!a.Mirror(0.0, 0.0, std::cos(phi), std::sin(phi)))
      return false;
  } else if (angle != 0.0) {
    a.Rotate(0.0, 0.0, -angle);
  }
  if (scale != 1.0 && !a.Scale(0.0, 0.0, scale))
    return false;
  if (o.x != 0.0 || o.y != 0.0)
    a.Translate(o.x, o.y);
  return true;
}

void TransformSelectedAlignmentsBy(AppCommandState& st, const doc::SimilarityTransform& xf, bool duplicate) {
  TransformSelectedAlignments(st, [&](alignment::Alignment& a) { return TransformAlignment(a, xf); }, duplicate);
}

// --- Survey points (Survey; their own selection list) ---------------------------------------------

void TransformSelectedSurveyPointsInPlace(AppCommandState& st, const doc::SimilarityTransform& xf) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= st.drawing.surveyPoints.size())
      continue;
    SurveyPoint& p = st.drawing.surveyPoints[static_cast<size_t>(i)];
    const ray3d::Vec3 q = xf.applyPoint({p.easting, p.northing, p.elevation});
    p.easting = q.x;
    p.northing = q.y;
    p.elevation = q.z;
  }
  for (int i : ix)
    if (i >= 0 && static_cast<size_t>(i) < st.drawing.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
}

/// The width TEXT is drawn at, measured the way picking and extents measure it.
double MeasuredTextWidth(const AppCommandState& st, const CadAnnotation& a) {
  float xs[4] = {}, ys[4] = {};
  CadAnnotationTextPickQuad(st, a, xs, ys, /*measure=*/true);
  return std::hypot(static_cast<double>(xs[1] - xs[0]), static_cast<double>(ys[1] - ys[0]));
}

/// A dimension whose size changed re-measures and re-places its text from its style.
void RefreshScaledDimension(const AppCommandState& st, CadAnnotation& a) {
  if (a.kind == CadAnnotation::Kind::DimAngular) {
    CadDimAngularSyncTextPlacement(&a, st.drawing.modelUnitsPerPlottedInch);
    CadDimRefreshMeasurementText(&a, st.drawing.activeDimensionStyle, CadAngleDisplaySettings(st));
  } else if (a.kind == CadAnnotation::Kind::DimLinear || a.kind == CadAnnotation::Kind::DimAligned) {
    CadDimRefreshMeasurementText(&a, CadDimensionStyleForAnnotation(st, a), CadAngleDisplaySettings(st));
  }
}

/// Removes the copy a failed duplicate-then-transform appended. Only a solid (the B-rep kernel can
/// refuse) or a mesh with no payload can fail once `SupportsEntityTransform` has passed.
template <class T>
void PopLast(std::vector<T>& items, std::vector<EntityAttributes>& attrs) {
  items.pop_back();
  if (attrs.size() > items.size())
    attrs.pop_back();
}

void PopFailedDuplicate(AppCommandState& st, EntityKind kind) {
  if (kind == EntityKind::Solid)
    PopLast(st.drawing.cadSolids, st.drawing.cadSolidAttrs);
  else if (kind == EntityKind::Mesh)
    PopLast(st.drawing.cadMeshes, st.drawing.cadMeshAttrs);
}

}  // namespace

size_t RefuseSelectionForTransform(AppCommandState& st, EntityOp op, const doc::SimilarityTransform& xf,
                                   const char* commandName, std::vector<std::string>& log) {
  // Grouped by message so each refused kind gets one line, in the order kinds first appear.
  std::vector<std::pair<std::string, size_t>> lines;
  std::map<std::string, size_t> lineOf;
  size_t refused = 0;
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [&](const SelectedEntity& e) {
                                      const Verdict v = JudgeSelected(e.type, op, xf);
                                      if (v.reason.empty())
                                        return false;
                                      const std::string key = std::string(v.plural) + " excluded: " + std::string(v.reason);
                                      auto it = lineOf.find(key);
                                      if (it == lineOf.end()) {
                                        lineOf.emplace(key, lines.size());
                                        lines.emplace_back(key, 1);
                                      } else {
                                        ++lines[it->second].second;
                                      }
                                      ++refused;
                                      return true;
                                    }),
                     st.selection.end());
  for (const auto& [text, count] : lines)
    log.push_back(std::string(commandName) + " — " + std::to_string(count) + " " + text);
  return refused;
}

void TransformSelection(AppCommandState& st, const doc::SimilarityTransform& xf, EntityOp op, const char* commandName,
                        bool duplicate, std::vector<std::string>& log) {
  if (!xf.valid())
    return;
  RefuseSelectionForTransform(st, op, xf, commandName, log);

  doc::EntityTransformContext ctx;
  ctx.textUnitsPerPlottedInch = st.drawing.modelUnitsPerPlottedInch;
  ctx.textWidth = [&st](const CadAnnotation& a) { return MeasuredTextWidth(st, a); };

  // Document kinds, each once, in selection order.
  std::vector<EntityRef> refs;
  std::set<std::pair<std::uint8_t, int>> seen;
  for (const SelectedEntity& e : st.selection) {
    const std::optional<EntityKind> kind = EntityKindForSelection(e.type);
    if (!kind || !doc::HasEntityKindOps(*kind) || e.index < 0)
      continue;
    if (static_cast<size_t>(e.index) >= doc::EntityCountForKind(st.drawing, *kind))
      continue;
    if (seen.insert({static_cast<std::uint8_t>(*kind), e.index}).second)
      refs.push_back({*kind, e.index});
  }

  size_t solidsUnchanged = 0;
  brep::Problem lastWhy = brep::Problem::Ok;
  bool changed = false;
  std::set<std::pair<std::uint8_t, int>> failed;  // sources left exactly as they were
  for (const EntityRef& src : refs) {
    EntityRef target = src;
    if (duplicate) {
      target = doc::DuplicateEntity(st.drawing, src);
      if (!target.valid())
        continue;
    }
    bool ok = false;
    if (target.kind == EntityKind::Solid) {
      brep::Problem why = brep::Problem::Ok;
      ok = doc::TransformSolid(st.drawing, static_cast<size_t>(target.index), xf, &why);
      if (!ok) {
        ++solidsUnchanged;
        lastWhy = why;
      }
    } else {
      ok = doc::TransformEntity(st.drawing, target, xf, ctx);
    }
    if (!ok) {
      if (duplicate)
        PopFailedDuplicate(st, target.kind);
      failed.insert({static_cast<std::uint8_t>(src.kind), src.index});
      continue;
    }
    changed = true;
    if (target.kind == EntityKind::Annotation && xf.scale() != 1.0)
      RefreshScaledDimension(st, st.drawing.cadAnnotations[static_cast<size_t>(target.index)]);
  }
  if (solidsUnchanged != 0)
    log.push_back(std::string(commandName) + " — " + std::to_string(solidsUnchanged) +
                  " solid(s) unchanged: " + brep::ProblemText(lastWhy));
  // A source that produced no result leaves the selection, so MIRROR's erase-source cannot delete an
  // object it did not copy.
  if (!failed.empty()) {
    st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                      [&](const SelectedEntity& e) {
                                        const std::optional<EntityKind> k = EntityKindForSelection(e.type);
                                        return k && failed.count({static_cast<std::uint8_t>(*k), e.index}) != 0;
                                      }),
                       st.selection.end());
  }

  // Kinds other systems own.
  TransformSelectedPdfUnderlays(st, xf, duplicate);
  TransformSelectedFeatureLines(st, xf, duplicate);
  TransformSelectedAlignmentsBy(st, xf, duplicate);
  if (!duplicate && !st.selectedSurveyPointIndices.empty()) {
    if (xf.isPlanarZ())
      TransformSelectedSurveyPointsInPlace(st, xf);
    else
      log.push_back(std::string(commandName) + " — " + std::to_string(st.selectedSurveyPointIndices.size()) +
                    " survey point(s) excluded: " + std::string(kPlanOnlyReason));
  }
  if (changed || !st.selection.empty() || !st.selectedSurveyPointIndices.empty())
    BumpCadGpuCache(st);
}

bool CadSelectionTakesTransform(SelectedEntity::Type type, EntityOp op, const doc::SimilarityTransform& xf) {
  return JudgeSelected(type, op, xf).reason.empty();
}

void CadTransformPdfUnderlayPlacement(PdfAttachment& att, const doc::SimilarityTransform& xf) {
  TransformPdfUnderlay(att, xf);
}

// --- The commit's transform, shared with the preview (issue #1700 phase 3) ------------------------

ray3d::Vec3 CadRotateAxisUnit(const AppCommandState& st) {
  // ROTATE turns about an axis parallel to the active UCS Z (REQ-329). In plan view, or under any UCS
  // merely rotated in plan, that axis is world Z exactly.
  if (CadWorkPlaneIsWorldXy(st))
    return {0.0, 0.0, 1.0};
  const ucs::Ucs u = CadActiveUcsStorage(st);
  return ray3d::Normalize(ray3d::Vec3{u.zAxis.x, u.zAxis.y, u.zAxis.z});
}

doc::SimilarityTransform CadMirrorTransform(const AppCommandState& st, float x0, float y0, float z0, float x1,
                                            float y1, float z1, bool* outLineAlongUcsZ) {
  if (outLineAlongUcsZ)
    *outLineAlongUcsZ = false;
  // The plane that holds the picked line and stands perpendicular to the work plane: a vertical plane
  // in plan view (REQ-103), a tilted one under a tilted UCS (REQ-329).
  if (CadWorkPlaneIsWorldXy(st))
    return doc::SimilarityTransform::ReflectionAcrossLineXY(x0, y0, x1, y1);
  const ray3d::Vec3 p1{static_cast<double>(x0), static_cast<double>(y0), static_cast<double>(z0)};
  const ray3d::Vec3 p2{static_cast<double>(x1), static_cast<double>(y1), static_cast<double>(z1)};
  const ucs::Ucs u = CadActiveUcsStorage(st);
  const ray3d::Vec3 normal = ray3d::Cross(ray3d::Sub(p2, p1), {u.zAxis.x, u.zAxis.y, u.zAxis.z});
  if (ray3d::Length(normal) < 1e-9) {
    if (outLineAlongUcsZ)
      *outLineAlongUcsZ = true;
    return doc::SimilarityTransform::Invalid();
  }
  return doc::SimilarityTransform::ReflectionAcrossPlane(p1, normal);
}

namespace {

constexpr int kMaxArrayPreviewInstances = 1000;

void AddRectangularArrayPlan(const AppCommandState& cmd, const stepengine::ArraySession& arr, float curX, float curY,
                             CadTransformPreviewPlan* plan) {
  using APh = stepengine::ArrayPhase;
  const ucs::Ucs frame = CadWorkPlaneAnchoredAt(cmd, arr.anchorX, arr.anchorY, arr.anchorZ);
  const ucs::Point2D cursorLocal = ucs::WorldToPlane(frame, ray3d::Vec3{curX, curY, cmd.uiCursorWorldZ});
  const int cols = std::max(arr.cols, 1);
  float colSpacing = arr.colSpacing;
  int rows = 1;
  float rowSpacing = 0.f;
  if (arr.phase == APh::Rect_WaitColumnSpacing) {
    colSpacing = static_cast<float>(cursorLocal.x);
  } else if (arr.phase == APh::Rect_WaitRowSpacing) {
    rows = std::max(arr.rows, 1);
    rowSpacing = static_cast<float>(cursorLocal.y);
  } else if (arr.phase != APh::Rect_WaitRows) {
    return;
  }
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < cols; ++c) {
      if (r == 0 && c == 0)
        continue;
      if (plan->instances.size() >= static_cast<size_t>(kMaxArrayPreviewInstances))
        return;
      // CommitArrayRectangular's own cell offset.
      float dx = 0.f, dy = 0.f, dz = 0.f;
      ArrayCellWorldDelta(cmd, arr, static_cast<float>(c) * colSpacing, static_cast<float>(r) * rowSpacing, &dx, &dy,
                          &dz);
      plan->instances.push_back(doc::SimilarityTransform::Translation({dx, dy, dz}));
    }
}

void AddPolarArrayPlan(const AppCommandState& cmd, const stepengine::ArraySession& arr, float curX, float curY,
                       CadTransformPreviewPlan* plan) {
  using APh = stepengine::ArrayPhase;
  if (arr.phase != APh::Polar_WaitAngle && arr.phase != APh::Polar_WaitRotateAnswer)
    return;
  constexpr float kPi = 3.14159265358979323846f;
  const int n = std::max(arr.itemCount, 1);
  const ray3d::Vec3 axisPoint{arr.centerX, arr.centerY, arr.centerZ};
  const ucs::Ucs ucsFrame = CadActiveUcsStorage(cmd);
  const ray3d::Vec3 axisUnit = ray3d::Normalize(ray3d::Vec3{ucsFrame.zAxis.x, ucsFrame.zAxis.y, ucsFrame.zAxis.z});
  float fillDeg = arr.fillAngleDeg;
  if (arr.phase == APh::Polar_WaitAngle) {
    const ucs::Ucs centreFrame = CadWorkPlaneAnchoredAt(cmd, arr.centerX, arr.centerY, arr.centerZ);
    const ucs::Point2D lc = ucs::WorldToPlane(centreFrame, ray3d::Vec3{curX, curY, cmd.uiCursorWorldZ});
    fillDeg = std::atan2(static_cast<float>(lc.y), static_cast<float>(lc.x)) * (180.f / kPi);
    if (fillDeg < 0.f)
      fillDeg += 360.f;
  }
  // CommitArrayPolar's own step and per-item transform.
  const bool fullTurn = std::fabs(std::fmod(fillDeg, 360.f)) < 1e-3f;
  const float fillRad = fillDeg * (kPi / 180.f);
  const float step =
      (n <= 1) ? 0.f : (fullTurn ? fillRad / static_cast<float>(n) : fillRad / static_cast<float>(n - 1));
  const int count = std::min(n, kMaxArrayPreviewInstances + 1);
  const ray3d::Vec3 anchor{arr.anchorX, arr.anchorY, arr.anchorZ};
  for (int i = 1; i < count; ++i) {
    const float ang = step * static_cast<float>(i);
    if (arr.rotateItems) {
      plan->instances.push_back(doc::SimilarityTransform::RotationAboutAxis(axisPoint, axisUnit, ang));
    } else {
      const ray3d::Vec3 rotated = ray3d::RotatePointAboutAxis(anchor, axisPoint, axisUnit, ang);
      plan->instances.push_back(doc::SimilarityTransform::Translation(ray3d::Sub(rotated, anchor)));
    }
  }
}

/// The selection types a whole-store source (the clipboard) is walked for. Pipe runs are left out:
/// their display solids exist only for the drawing's own runs.
constexpr SelectedEntity::Type kStoreSelectionTypes[] = {
    SelectedEntity::Type::LineSeg,      SelectedEntity::Type::Circle,      SelectedEntity::Type::Arc,
    SelectedEntity::Type::Ellipse,      SelectedEntity::Type::Polyline,    SelectedEntity::Type::Annotation,
    SelectedEntity::Type::FilledRegion, SelectedEntity::Type::Mesh,        SelectedEntity::Type::Table,
    SelectedEntity::Type::BlockRef,     SelectedEntity::Type::Solid,       SelectedEntity::Type::PositionMarker,
    SelectedEntity::Type::Multileader,  SelectedEntity::Type::Spline,      SelectedEntity::Type::Point};

/// Appends a copy of feature line \p fi of \p src to \p dst with every vertex mapped in X, Y and Z,
/// as \ref TransformSelectedFeatureLines maps them at commit. Returns its index in \p dst, or -1.
int CopyFeatureLineTransformed(DrawingContent& dst, const DrawingContent& src, int fi,
                               const doc::SimilarityTransform& xf) {
  if (fi < 0 || static_cast<size_t>(fi) + 1 >= src.featureLineOffsets.size())
    return -1;
  const int v0 = src.featureLineOffsets[static_cast<size_t>(fi)];
  const int v1 = src.featureLineOffsets[static_cast<size_t>(fi) + 1];
  if (v0 < 0 || v1 - v0 < 2 || static_cast<size_t>(v1) * 3 > src.featureLineVerts.size())
    return -1;
  if (dst.featureLineOffsets.empty())
    dst.featureLineOffsets.push_back(0);
  for (int vi = v0; vi < v1; ++vi) {
    const size_t b = static_cast<size_t>(vi) * 3;
    const ray3d::Vec3 q =
        xf.applyPoint({src.featureLineVerts[b], src.featureLineVerts[b + 1], src.featureLineVerts[b + 2]});
    dst.featureLineVerts.insert(dst.featureLineVerts.end(), {q.x, q.y, q.z});
  }
  dst.featureLineOffsets.push_back(dst.featureLineOffsets.back() + (v1 - v0));
  dst.featureLineClosed.push_back(static_cast<size_t>(fi) < src.featureLineClosed.size()
                                      ? src.featureLineClosed[static_cast<size_t>(fi)]
                                      : std::uint8_t{0});
  return static_cast<int>(dst.featureLineOffsets.size()) - 2;
}

}  // namespace

bool CadActiveTransformPreviewPlan(const AppCommandState& cmd, float curX, float curY, CadTransformPreviewPlan* plan) {
  if (!plan)
    return false;
  *plan = CadTransformPreviewPlan{};
  using MP = stepengine::ModifyPhase;
  using MirP = stepengine::MirrorPhase;
  using SimT = doc::SimilarityTransform;
  if (const auto* mc = cmd.sessionAs<stepengine::MoveCopySession>()) {
    if (mc->phase != MP::NeedDestination)
      return false;
    // MoveCopySession::onViewportPick: the destination is the pick, at the commit elevation.
    plan->op = mc->isCopy ? EntityOp::Copy : EntityOp::Move;
    plan->instances.push_back(
        SimT::Translation({curX - mc->baseX, curY - mc->baseY, CadCommitElevation(cmd) - mc->baseZ}));
    return true;
  }
  if (const auto* ps = cmd.sessionAs<stepengine::PasteSession>()) {
    plan->op = EntityOp::Copy;
    plan->fromClipboard = true;
    plan->instances.push_back(SimT::Translation({curX - ps->baseX, curY - ps->baseY, 0.0}));
    return true;
  }
  if (const auto* arr = cmd.sessionAs<stepengine::ArraySession>()) {
    plan->op = EntityOp::Array;
    if (arr->type == stepengine::ArrayType::Rectangular)
      AddRectangularArrayPlan(cmd, *arr, curX, curY, plan);
    else
      AddPolarArrayPlan(cmd, *arr, curX, curY, plan);
    return !plan->instances.empty();
  }
  if (const auto* ss = cmd.sessionAs<stepengine::ScaleSession>()) {
    float sc = 1.f;
    if (ss->phase != MP::NeedDestination || !CadScalePreviewFactor(cmd, curX, curY, &sc))
      return false;
    plan->op = EntityOp::Scale;
    plan->instances.push_back(SimT::UniformScale({ss->baseX, ss->baseY, ss->baseZ}, sc));
    return plan->instances.back().valid();
  }
  if (const auto* ms = cmd.sessionAs<stepengine::MirrorSession>()) {
    if (ms->phase != MirP::NeedP2 && ms->phase != MirP::NeedEraseAnswer)
      return false;
    float x1 = ms->p2X, y1 = ms->p2Y, z1 = ms->p2Z;
    if (ms->phase == MirP::NeedP2) {
      // MirrorSession::onViewportPick: ORTHO locks the line, at the commit elevation.
      x1 = curX;
      y1 = curY;
      z1 = CadCommitElevation(cmd);
      ApplyOrthoConstrainFromAnchor(cmd, ms->p1X, ms->p1Y, &x1, &y1, cmd.orthoMode, ms->p1Z, z1, &z1);
    }
    plan->op = EntityOp::Mirror;
    plan->instances.push_back(CadMirrorTransform(cmd, ms->p1X, ms->p1Y, ms->p1Z, x1, y1, z1, nullptr));
    return plan->instances.back().valid();
  }
  if (const auto* rs = cmd.sessionAs<stepengine::RotateSession>()) {
    double theta = 0.0;
    if (!CadRotatePreviewTheta(cmd, curX, curY, &theta))
      return false;
    const SimT xf = SimT::RotationAboutAxis({rs->baseX, rs->baseY, rs->baseZ}, CadRotateAxisUnit(cmd), theta);
    plan->op = xf.isPlanarZ() ? EntityOp::RotatePlan : EntityOp::Rotate3d;
    plan->instances.push_back(xf);
    return xf.valid();
  }
  return false;
}

bool CadGizmoDragPreviewPlan(const AppCommandState& cmd, CadTransformPreviewPlan* plan) {
  if (!plan)
    return false;
  *plan = CadTransformPreviewPlan{};
  // A face, edge or vertex drag edits a solid, not the selection (BuildSubObjectFaceGhost draws it).
  if (!cmd.gizmoDragActive || cmd.gizmoDragIsSubObject)
    return false;
  using SimT = doc::SimilarityTransform;
  const double d = cmd.gizmoDragDistance;
  // CommitGizmoDragImpl: a drag that leaves the operation's neutral value changes nothing.
  const double neutral = cmd.gizmoOp == CadGizmoOp::Scale ? 1.0 : 0.0;
  if (std::fabs(d - neutral) < 1.e-12)
    return false;
  const ray3d::Vec3 anchor = cmd.gizmoAnchor;
  if (cmd.gizmoOp == CadGizmoOp::Rotate) {
    // The Z ring is typed ROTATE (ApplyRotationAboutUcsZ); the X and Y rings turn about their own axis.
    const ray3d::Vec3 axis = cmd.gizmoDragAxis == 2 ? CadRotateAxisUnit(cmd) : cmd.gizmoAxisDir;
    const SimT xf = SimT::RotationAboutAxis(anchor, axis, d);
    plan->op = xf.isPlanarZ() ? EntityOp::RotatePlan : EntityOp::Rotate3d;
    plan->instances.push_back(xf);
  } else if (cmd.gizmoOp == CadGizmoOp::Scale) {
    // ApplyUniformScaleAboutBase takes the base point and the factor as float.
    plan->op = EntityOp::Scale;
    plan->instances.push_back(SimT::UniformScale(
        {static_cast<float>(anchor.x), static_cast<float>(anchor.y), static_cast<float>(anchor.z)},
        static_cast<float>(d)));
  } else {
    // ApplyTranslationToSelection takes the displacement as float.
    const ray3d::Vec3 mv = cmd.gizmoDragVec;
    plan->op = EntityOp::Move;
    plan->instances.push_back(
        SimT::Translation({static_cast<float>(mv.x), static_cast<float>(mv.y), static_cast<float>(mv.z)}));
  }
  return plan->instances.back().valid();
}

void BuildCadTransformPreviewScratch(const AppCommandState& cmd, const CadTransformPreviewPlan& plan,
                                     CadTransformPreviewScratch* out, std::optional<SelectedEntity::Type> only) {
  if (!out)
    return;
  out->drawing = DrawingContent{};
  out->entities.clear();
  out->cached.clear();
  const DrawingContent& src = plan.fromClipboard ? cmd.clipboard.content : cmd.drawing;
  out->source = &src;

  std::vector<SelectedEntity> sources;
  if (plan.fromClipboard) {
    for (SelectedEntity::Type t : kStoreSelectionTypes) {
      const std::optional<EntityKind> kind = EntityKindForSelection(t);
      const size_t n = kind ? doc::EntityCountForKind(src, *kind) : 0;
      for (size_t i = 0; i < n; ++i)
        sources.push_back(SelectedEntity{t, static_cast<int>(i)});
    }
  } else {
    std::set<std::pair<int, int>> seen;
    for (const SelectedEntity& e : cmd.selection)
      if (seen.insert({static_cast<int>(e.type), e.index}).second)
        sources.push_back(e);
  }

  doc::EntityTransformContext ctx;
  ctx.textUnitsPerPlottedInch = cmd.drawing.modelUnitsPerPlottedInch;
  ctx.textWidth = [&cmd](const CadAnnotation& a) { return MeasuredTextWidth(cmd, a); };

  for (const doc::SimilarityTransform& xf : plan.instances) {
    if (!xf.valid())
      continue;
    for (const SelectedEntity& e : sources) {
      if (only && e.type != *only)
        continue;
      // What the commit refuses, the preview leaves out (RefuseSelectionForTransform). A paste is a
      // translation, which every kind takes.
      if (!plan.fromClipboard && !CadSelectionTakesTransform(e.type, plan.op, xf))
        continue;
      if (e.type == SelectedEntity::Type::FeatureLine) {
        const int fi = CopyFeatureLineTransformed(out->drawing, src, e.index, xf);
        if (fi >= 0)
          out->entities.push_back(SelectedEntity{e.type, fi});
        continue;
      }
      if (e.type == SelectedEntity::Type::Alignment) {
        if (e.index < 0 || static_cast<size_t>(e.index) >= src.alignments.size())
          continue;
        alignment::Alignment copy = src.alignments[static_cast<size_t>(e.index)];
        if (!TransformAlignment(copy, xf))
          continue;
        out->drawing.alignments.push_back(std::move(copy));
        out->entities.push_back(SelectedEntity{e.type, static_cast<int>(out->drawing.alignments.size()) - 1});
        continue;
      }
      const std::optional<EntityKind> kind = EntityKindForSelection(e.type);
      if (!kind || !doc::HasEntityKindOps(*kind) || e.index < 0 ||
          static_cast<size_t>(e.index) >= doc::EntityCountForKind(src, *kind))
        continue;
      // Solids, pipe runs and meshes keep their display tessellation and move it with the same
      // transform (ADR-0011): rebuilding a B-rep, a swept pipe or a large mesh per frame is the
      // commit's work, not the preview's.
      if (*kind == EntityKind::Solid || *kind == EntityKind::PipeRun || *kind == EntityKind::Mesh) {
        if (doc::SupportsEntityTransform(*kind, xf))
          out->cached.push_back({e, xf});
        continue;
      }
      const EntityRef copy = doc::CopyEntityInto(out->drawing, src, EntityRef{*kind, e.index});
      if (!copy.valid() || !doc::TransformEntity(out->drawing, copy, xf, ctx))
        continue;
      if (copy.kind == EntityKind::Annotation && xf.scale() != 1.0)
        RefreshScaledDimension(cmd, out->drawing.cadAnnotations[static_cast<size_t>(copy.index)]);
      out->entities.push_back(SelectedEntity{e.type, copy.index});
    }
  }
}
