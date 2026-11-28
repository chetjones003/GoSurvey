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

void TransformSelectedAlignmentsBy(AppCommandState& st, const doc::SimilarityTransform& xf, bool duplicate) {
  const ray3d::Vec3 o = xf.applyPoint({0.0, 0.0, 0.0});
  const double angle = PlanAngle(xf);
  const double scale = xf.scale();
  const bool reflected = xf.isReflection();
  TransformSelectedAlignments(
      st,
      [&](alignment::Alignment& a) {
        // The plan transform about the origin, then the translation it leaves: reflect or rotate,
        // scale, move. Alignment azimuths run clockwise, so a counter-clockwise turn is negated.
        if (reflected) {
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
      },
      duplicate);
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
void RefreshScaledDimension(AppCommandState& st, CadAnnotation& a) {
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
