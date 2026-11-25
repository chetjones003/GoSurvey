#include "CadCommandsInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "modify/CadModifySessions.hpp"
#include "engine/StepEngine.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Geometry/spline.hpp>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

/// Scale every selected solid uniformly about \p basePoint (REQ-332 / TASK-231).
///
/// **Uniform on every axis, including in plan view, where a 2D entity's elevation is left alone.**
/// That asymmetry is deliberate. `ScaleSelectionZAboutBase` runs only under a tilted UCS so that
/// plan view keeps its pre-REQ-329 "elevations untouched" behaviour byte-for-byte — but a solid
/// cannot join that carve-out: `brep::Scale` is uniform because the representation has no ellipsoid
/// and no elliptical cylinder, so "X and Y but not Z" is unrepresentable rather than merely partial.
/// And a solid has no legacy behaviour to protect, because every one of these commands refused it
/// outright until this change.
static void ScaleSelectedSolids(AppCommandState& st, const ray3d::Vec3& basePoint, float factor,
                                std::vector<std::string>& log) {
  if (factor == 1.f)
    return;
  const double k = static_cast<double>(factor);
  TransformSelectedSolids(
      st, "SCALE",
      [&](const brep::Solid& s, brep::Solid* out, brep::Problem* why) {
        return brep::Scale(s, basePoint, k, out, why);
      },
      log);
}

}  // namespace

namespace {  // reopened; see the note above ApplyTranslationToSelection
static void ApplyScaleToSelectedSurveyPoints(AppCommandState& st, float bx, float by, float sc) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix) {
    if (i < 0 || static_cast<size_t>(i) >= st.drawing.surveyPoints.size())
      continue;
    double x = st.drawing.surveyPoints[static_cast<size_t>(i)].easting;
    double y = st.drawing.surveyPoints[static_cast<size_t>(i)].northing;
    ScalePtAroundBase(bx, by, sc, &x, &y);
    st.drawing.surveyPoints[static_cast<size_t>(i)].easting = x;
    st.drawing.surveyPoints[static_cast<size_t>(i)].northing = y;
  }
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.drawing.surveyPoints.size())
      RepositionSurveyLabelMtextForPoint(st, static_cast<size_t>(i));
  }
}

// Scales one annotation about (bx,by) — shared by model-space SCALE and paper-space SCALE (#764).
}  // namespace


void ApplyScaleToSelection(AppCommandState& st, float bx, float by, float bz, float sc,
                           std::vector<std::string>& log) {
  if (!(sc > 0.f) || !std::isfinite(sc))
    return;
  DropSurfacesFromSelectionForTransform(st, "SCALE", log);
  DropPositionMarkersFromSelection(st, "SCALE", log);  // REQ-359: move/copy/erase only
  DropMultileadersFromSelection(st, "SCALE", log);     // REQ-367
  // Solids are NOT dropped any more (REQ-332, amending REQ-322 item 6), and they take the FULL 3D
  // base point — which is why this function gained `bz`, the same move REQ-322 made when it gave
  // `ApplyTranslationToSelection` a `dz`.
  //
  // A selected solid scales uniformly on every axis even in plan view, where the 2D entities beside
  // it keep their elevations (`ScaleSelectionZAboutBase` runs only under a tilted UCS). That
  // asymmetry is deliberate: `brep::Scale` is uniform because the representation has no ellipsoid,
  // so "X and Y but not Z" is unrepresentable rather than partial — and no drawing can depend on the
  // old behaviour, because SCALE refused solids outright until now. See ScaleSelectedSolids.
  ScaleSelectedSolids(st, {static_cast<double>(bx), static_cast<double>(by), static_cast<double>(bz)},
                      sc, log);
  // A pipe run's ROUTE scales in 3D, like a solid; its size does not — a 4in pipe scaled is a longer
  // 4in pipe, because the size is a catalog part (REQ-351, D-2026-09-28-f).
  if (sc != 1.f) {
    const ray3d::Vec3 base{static_cast<double>(bx), static_cast<double>(by), static_cast<double>(bz)};
    const double k = static_cast<double>(sc);
    TransformSelectedPipeRuns(
        st, [&](const ray3d::Vec3& p) { return ray3d::Add(base, ray3d::Scale(ray3d::Sub(p, base), k)); },
        /*duplicate=*/false);
    TransformSelectedAlignments(  // REQ-487.1: the plan scale; an alignment has no elevation to scale
        st, [&](alignment::Alignment& a) { return a.Scale(static_cast<double>(bx), static_cast<double>(by), k); }, /*duplicate=*/false);
  }
  std::vector<bool> lineMark(std::max<size_t>(1, st.drawing.lineCount()), false);
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::LineSeg)
      continue;
    if (e.index >= 0 && static_cast<size_t>(e.index) < lineMark.size())
      lineMark[static_cast<size_t>(e.index)] = true;
  }
  if (!lineMark.empty()) {
    for (size_t i = 0; i < lineMark.size(); ++i) {
      if (!lineMark[i])
        continue;
      size_t k = i;
      if (k < st.drawing.lineCount()) {
        ScalePtAroundBase(bx, by, sc, &st.drawing.lineComponent(k, CadLineComponent::X0), &st.drawing.lineComponent(k, CadLineComponent::Y0));
        ScalePtAroundBase(bx, by, sc, &st.drawing.lineComponent(k, CadLineComponent::X1), &st.drawing.lineComponent(k, CadLineComponent::Y1));
      }
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    const size_t ci = static_cast<size_t>(e.index);
    if (ci < st.drawing.circleCount()) {
      CadCircleRecord scaled = st.drawing.circle(ci);
      ScalePtAroundBase(bx, by, sc, &scaled.cx, &scaled.cy);
      scaled.r *= sc;
      st.drawing.setCircle(ci, scaled);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.userArcs.size())
      continue;
    CadArc& a = st.drawing.userArcs[k];
    ScalePtAroundBase(bx, by, sc, &a.cx, &a.cy);
    a.r *= sc;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.userEllipses.size())
      continue;
    CadEllipse& el = st.drawing.userEllipses[k];
    float mx = static_cast<float>(el.cx + el.majVx);
    float my = static_cast<float>(el.cy + el.majVy);
    ScalePtAroundBase(bx, by, sc, &el.cx, &el.cy);
    ScalePtAroundBase(bx, by, sc, &mx, &my);
    el.majVx = static_cast<float>(mx - el.cx);
    el.majVy = static_cast<float>(my - el.cy);
  }
  MapSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443
    double x = v.x, y = v.y;
    ScalePtAroundBase(bx, by, sc, &x, &y);
    return spline::Vec3{x, y, v.z};
  });
  MapSelectedCadPoints(st, [&](const spline::Vec3& v) {
    double x = v.x, y = v.y;
    ScalePtAroundBase(bx, by, sc, &x, &y);
    return spline::Vec3{x, y, v.z};
  });
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      continue;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    for (int vi = v0; vi < v1; ++vi)
      ScalePtAroundBase(bx, by, sc, &st.drawing.vertexComponent(vi, CadVertexComponent::X),
                        &st.drawing.vertexComponent(vi, CadVertexComponent::Y));
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.cadAnnotations.size())
      continue;
    ScaleAnnotationAroundBase(st, st.drawing.cadAnnotations[k], bx, by, sc);
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Table)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadTables.size())
      continue;
    CadTableScaleAround(&st.drawing.cadTables[static_cast<size_t>(e.index)], bx, by, sc);
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::BlockRef)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
      continue;
    CadBlockScaleAbout(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], bx, by, sc);
  }
  // PDF underlays: scale insertion point around base; multiply uniform scale factor.
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::PdfUnderlay)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.pdfAttachments.size())
      continue;
    PdfAttachment& att = st.drawing.pdfAttachments[static_cast<size_t>(e.index)];
    ScalePtAroundBase(bx, by, sc, &att.insertX, &att.insertY);
    att.scale = std::max(att.scale * sc, 1e-9f);
  }
  // Feature lines (REQ-087). Plan only — elevations are NOT scaled here, matching the polyline above;
  // `ScaleSelectionZAboutBase` (REQ-329 increment 2 (SCALE)) scales Z under a tilted UCS.
  TransformSelectedFeatureLinesInPlace(
      st, [&](auto* x, auto* y) { ScalePtAroundBase(bx, by, sc, x, y); });
  ApplyScaleToSelectedSurveyPoints(st, bx, by, sc);
  BumpCadGpuCache(st);
}

/// REQ-329 increment (SCALE): uniform scale is `p' = base + sc*(p - base)` on every axis, so once
/// `ApplyScaleToSelection` has scaled X/Y (and every size) about (bx, by), this scales the Z of the
/// same positions about \p bz. Called by \c FinishScaleCommand ONLY under a tilted UCS — plan view
/// and any plan-rotated UCS keep the pre-REQ-329 "elevations untouched" behaviour byte-for-byte,
/// because there the scale base sits on the world XY plane with the geometry and a Z pass would be
/// a no-op at best and an unasked-for change at worst.
static void ScaleSelectionZAboutBase(AppCommandState& st, float bz, float sc) {
  const auto sz = [&](auto* z) { *z = static_cast<decltype(+*z)>(bz + sc * (static_cast<double>(*z) - bz)); };
  for (const auto& e : st.selection) {
    switch (e.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.lineCount()) {
        sz(&st.drawing.lineComponent(k, CadLineComponent::Z0));
        sz(&st.drawing.lineComponent(k, CadLineComponent::Z1));
      }
      break;
    }
    case SelectedEntity::Type::Circle: {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci < st.drawing.circleCount())
        sz(&st.drawing.circleComponent(ci, CadCircleComponent::Z));
      break;
    }
    case SelectedEntity::Type::Arc: {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userArcs.size())
        sz(&st.drawing.userArcs[k].z);
      break;
    }
    case SelectedEntity::Type::Ellipse: {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userEllipses.size())
        sz(&st.drawing.userEllipses[k].z);
      break;
    }
    case SelectedEntity::Type::Spline: {  // REQ-443: elevations scale about the base elevation too
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadSplines.size())
        spline::MapPoints(&st.drawing.cadSplines[k], [&](const spline::Vec3& v) {
          return spline::Vec3{v.x, v.y, bz + static_cast<double>(sc) * (v.z - bz)};
        });
      break;
    }
    case SelectedEntity::Type::Point: {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadPoints.size())
        st.drawing.cadPoints[k].z = bz + static_cast<double>(sc) * (st.drawing.cadPoints[k].z - bz);
      break;
    }
    case SelectedEntity::Type::Polyline: {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        break;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      for (int vi = v0; vi < v1; ++vi) {
        const size_t vIdx = static_cast<size_t>(vi);
        if (vIdx < st.drawing.vertexCount())
          sz(&st.drawing.vertexComponent(vIdx, CadVertexComponent::Z));
      }
      break;
    }
    case SelectedEntity::Type::FilledRegion: {
      const size_t fk = static_cast<size_t>(e.index);
      if (fk >= st.drawing.cadFilledRegions.size())
        break;
      auto& v = st.drawing.cadFilledRegions[fk].vertsXyz;
      for (size_t i = 2; i < v.size(); i += 3)
        sz(&v[i]);
      break;
    }
    case SelectedEntity::Type::Annotation: {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadAnnotations.size())
        sz(&st.drawing.cadAnnotations[k].insZ);
      break;
    }
    case SelectedEntity::Type::Table: {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadTables.size())
        sz(&st.drawing.cadTables[k].insZ);
      break;
    }
    case SelectedEntity::Type::BlockRef: {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadBlockRefs.size())
        sz(&st.drawing.cadBlockRefs[k].xf.z);
      break;
    }
    default:
      break;
    }
  }
  // Feature lines and survey points carry their own Z too.
  ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
    for (int vi = v0; vi < v1; ++vi) {
      const size_t b = static_cast<size_t>(vi) * 3 + 2;
      if (b < st.drawing.featureLineVerts.size())
        sz(&st.drawing.featureLineVerts[b]);
    }
  });
  std::vector<int> spix = st.selectedSurveyPointIndices;
  std::sort(spix.begin(), spix.end());
  spix.erase(std::unique(spix.begin(), spix.end()), spix.end());
  for (int idx : spix) {
    if (idx >= 0 && static_cast<size_t>(idx) < st.drawing.surveyPoints.size())
      sz(&st.drawing.surveyPoints[static_cast<size_t>(idx)].elevation);
  }
  BumpCadGpuCache(st);
}


void ApplyUniformScaleAboutBase(AppCommandState& st, float bx, float by, float bz, float sc,
                                std::vector<std::string>& log) {
  ApplyScaleToSelection(st, bx, by, bz, sc, log);
  // REQ-329 increment (SCALE): a uniform scale is uniform on every axis. `ApplyScaleToSelection`
  // scales X/Y (and sizes) about the base; under a tilted UCS the geometry also lives in Z, so the
  // elevations scale about the base elevation too. Plan view and any plan-rotated UCS keep the
  // pre-REQ-329 "elevations untouched" behaviour — byte-identical.
  //
  // **Both steps are the command**, which is why they live behind one name (TASK-232): REQ-060's
  // gizmo has to produce the same coordinates as typed SCALE, and a gizmo that called only
  // `ApplyScaleToSelection` would agree in plan view and diverge under a tilted UCS.
  if (!CadWorkPlaneIsWorldXy(st))
    ScaleSelectionZAboutBase(st, bz, sc);
}


// Templated: annotation insertion points are float, dimension definition points double (issue #1668).
template <class T>
static void RotatePtForAnnotationPreview(float bx, float by, float rad, T* x, T* y) {
  const double c = std::cos(static_cast<double>(rad));
  const double s = std::sin(static_cast<double>(rad));
  const double dx = static_cast<double>(*x) - bx;
  const double dy = static_cast<double>(*y) - by;
  *x = static_cast<T>(bx + c * dx - s * dy);
  *y = static_cast<T>(by + s * dx + c * dy);
}

bool CadRotatePreviewTheta(const AppCommandState& cmd, float curX, float curY, float* outThetaRad) {
  const auto* rs = cmd.sessionAs<stepengine::RotateSession>();
  if (!rs || !outThetaRad)
    return false;
  using RP = stepengine::RotatePhase;
  if (rs->phase == RP::NeedAngleOrReference) {
    const float dx = curX - rs->baseX;
    const float dy = curY - rs->baseY;
    *outThetaRad = -std::atan2(dx, dy);
  } else if (rs->phase == RP::AfterReference_WaitAngleOrP) {
    const float thetaRef = std::atan2(rs->refY2 - rs->refY1, rs->refX2 - rs->refX1);
    *outThetaRad = std::atan2(curY - rs->baseY, curX - rs->baseX) - thetaRef;
  } else if (rs->phase == RP::AnglePoints_WaitP2) {
    *outThetaRad = RotateDeltaFromReferenceAndNewSegment(rs->refX1, rs->refY1, rs->refX2, rs->refY2,
                                                         rs->anglePt1X, rs->anglePt1Y, curX, curY);
  } else {
    return false;
  }
  return true;
}

bool CadScalePreviewFactor(const AppCommandState& cmd, float curX, float curY, float* outScale) {
  const auto* ss = cmd.sessionAs<stepengine::ScaleSession>();
  if (!ss || !outScale)
    return false;
  if (ss->phase != stepengine::ModifyPhase::NeedDestination)
    return false;
  using SP = stepengine::ScalePhase;
  if (ss->scalePhase == SP::FactorPick) {
    const float d = std::hypot(curX - ss->baseX, curY - ss->baseY);
    const float s = d / std::max(ss->refDist, 1e-20f);
    *outScale = std::max(s, 1e-6f);
    return true;
  }
  if (ss->scalePhase == SP::NewLength_WaitP2) {
    const float d = std::hypot(curX - ss->newLenP1X, curY - ss->newLenP1Y);
    const float s = d / std::max(ss->refDist, 1e-20f);
    *outScale = std::max(s, 1e-6f);
    return true;
  }
  return false;
}

static void CadAnnotationPreviewTranslated(const CadAnnotation& src, float dx, float dy, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  out->insX += dx;
  out->insY += dy;
  if (out->kind == CadAnnotation::Kind::Mtext) {
    out->boxMinX += dx;
    out->boxMinY += dy;
    out->boxMaxX += dx;
    out->boxMaxY += dy;
  } else if (out->kind == CadAnnotation::Kind::DimAligned || out->kind == CadAnnotation::Kind::DimLinear) {
    out->dimExt1X += dx;
    out->dimExt1Y += dy;
    out->dimExt2X += dx;
    out->dimExt2Y += dy;
  }
}

static void CadAnnotationPreviewRotated(const CadAnnotation& src, float bx, float by, float rad, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  CadAnnotation& a = *out;
  RotatePtForAnnotationPreview(bx, by, rad, &a.insX, &a.insY);
  if (a.kind == CadAnnotation::Kind::Text) {
    a.rotationRad += rad;
  } else if (a.kind == CadAnnotation::Kind::Mtext) {
    const float w = a.boxMaxX - a.boxMinX, h = a.boxMaxY - a.boxMinY;
    float x = a.boxMinX, y = a.boxMaxY;
    RotatePtForAnnotationPreview(bx, by, rad, &x, &y);
    a.boxMinX = x; a.boxMaxX = x + w;
    a.boxMaxY = y; a.boxMinY = y - h;
    a.rotationRad += rad;
  } else if (a.kind == CadAnnotation::Kind::DimLinear) {
    RotateCadDimLinearAroundBase(bx, by, rad, &a);
  } else if (a.kind == CadAnnotation::Kind::DimAligned) {
    RotatePtForAnnotationPreview(bx, by, rad, &a.dimExt1X, &a.dimExt1Y);
    RotatePtForAnnotationPreview(bx, by, rad, &a.dimExt2X, &a.dimExt2Y);
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      a.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
  } else {
    float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
    float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
    float mnX = xs[0];
    float mxX = xs[0];
    float mnY = ys[0];
    float mxY = ys[0];
    for (int i = 0; i < 4; ++i) {
      RotatePtForAnnotationPreview(bx, by, rad, &xs[i], &ys[i]);
      mnX = std::min(mnX, xs[i]);
      mxX = std::max(mxX, xs[i]);
      mnY = std::min(mnY, ys[i]);
      mxY = std::max(mxY, ys[i]);
    }
    a.boxMinX = mnX;
    a.boxMaxX = mxX;
    a.boxMinY = mnY;
    a.boxMaxY = mxY;
    a.insX = mnX;
    a.insY = mnY;
  }
}

static void CadAnnotationPreviewScaled(const CadAnnotation& src, float bx, float by, float sc, CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  CadAnnotation& a = *out;
  ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
  if (a.kind == CadAnnotation::Kind::Text) {
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else if (a.kind == CadAnnotation::Kind::DimLinear) {
    ScaleCadDimLinearAroundBase(bx, by, sc, &a);
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else if (a.kind == CadAnnotation::Kind::DimAligned) {
    ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
    ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
    a.dimSignedOffset *= sc;
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      a.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
  } else if (a.kind == CadAnnotation::Kind::DimAngular) {
    ScalePtAroundBase(bx, by, sc, &a.dimAngVertexX, &a.dimAngVertexY);
    ScalePtAroundBase(bx, by, sc, &a.dimExt1X, &a.dimExt1Y);
    ScalePtAroundBase(bx, by, sc, &a.dimExt2X, &a.dimExt2Y);
    a.dimSignedOffset *= sc;
    ScalePtAroundBase(bx, by, sc, &a.insX, &a.insY);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  } else {
    ScalePtAroundBase(bx, by, sc, &a.boxMinX, &a.boxMinY);
    ScalePtAroundBase(bx, by, sc, &a.boxMaxX, &a.boxMaxY);
    if (a.boxMinX > a.boxMaxX)
      std::swap(a.boxMinX, a.boxMaxX);
    if (a.boxMinY > a.boxMaxY)
      std::swap(a.boxMinY, a.boxMaxY);
    a.insX = a.boxMinX;
    a.insY = a.boxMinY;
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
  }
}

/// REQ-103 MIRROR preview. Same shape as \c CadAnnotationPreviewRotated, reflecting instead of
/// rotating — MIRRTEXT-off (D-2026-08-23-j): Text/Mtext insertion reflects, \c rotationRad is left
/// untouched so the preview shows the same "stays upright" result the committed duplicate will have.
static void CadAnnotationPreviewReflected(const CadAnnotation& src, float x0, float y0, float x1, float y1,
                                          CadAnnotation* out) {
  if (!out)
    return;
  *out = src;
  CadAnnotation& a = *out;
  if (a.kind == CadAnnotation::Kind::Text || a.kind == CadAnnotation::Kind::Mtext) {
    ReflectPtAcrossLine(x0, y0, x1, y1, &a.insX, &a.insY);
    if (a.kind == CadAnnotation::Kind::Mtext) {
      float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
      float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
      float mnX = xs[0], mxX = xs[0], mnY = ys[0], mxY = ys[0];
      for (int i = 0; i < 4; ++i) {
        ReflectPtAcrossLine(x0, y0, x1, y1, &xs[i], &ys[i]);
        mnX = std::min(mnX, xs[i]);
        mxX = std::max(mxX, xs[i]);
        mnY = std::min(mnY, ys[i]);
        mxY = std::max(mxY, ys[i]);
      }
      a.boxMinX = mnX; a.boxMaxX = mxX; a.boxMinY = mnY; a.boxMaxY = mxY;
      a.insX = mnX; a.insY = mnY;
    }
  } else if (a.kind == CadAnnotation::Kind::DimLinear) {
    ReflectCadDimLinearAcrossLine(x0, y0, x1, y1, &a);
  } else if (a.kind == CadAnnotation::Kind::DimAligned) {
    ReflectPtAcrossLine(x0, y0, x1, y1, &a.insX, &a.insY);
    ReflectPtAcrossLine(x0, y0, x1, y1, &a.dimExt1X, &a.dimExt1Y);
    ReflectPtAcrossLine(x0, y0, x1, y1, &a.dimExt2X, &a.dimExt2Y);
    float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
    if (CadDimAlignedGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
      a.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
  } else {
    ReflectPtAcrossLine(x0, y0, x1, y1, &a.insX, &a.insY);
    float xs[4] = {a.boxMinX, a.boxMaxX, a.boxMaxX, a.boxMinX};
    float ys[4] = {a.boxMinY, a.boxMinY, a.boxMaxY, a.boxMaxY};
    float mnX = xs[0], mxX = xs[0], mnY = ys[0], mxY = ys[0];
    for (int i = 0; i < 4; ++i) {
      ReflectPtAcrossLine(x0, y0, x1, y1, &xs[i], &ys[i]);
      mnX = std::min(mnX, xs[i]);
      mxX = std::max(mxX, xs[i]);
      mnY = std::min(mnY, ys[i]);
      mxY = std::max(mxY, ys[i]);
    }
    a.boxMinX = mnX; a.boxMaxX = mxX; a.boxMinY = mnY; a.boxMaxY = mxY;
    a.insX = mnX; a.insY = mnY;
  }
}

void CadAnnotationCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,
                                           std::vector<CadAnnotation>* out) {
  if (!out)
    return;
  out->clear();
  using MP = stepengine::ModifyPhase;
  using MirP = stepengine::MirrorPhase;
  if (const auto* mc = cmd.sessionAs<stepengine::MoveCopySession>()) {
    if (mc->phase != MP::NeedDestination)
      return;
    const float dx = curX - mc->baseX;
    const float dy = curY - mc->baseY;
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Annotation)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.drawing.cadAnnotations.size())
        continue;
      CadAnnotation p{};
      CadAnnotationPreviewTranslated(cmd.drawing.cadAnnotations[k], dx, dy, &p);
      out->push_back(p);
    }
    return;
  }
  if (const auto* ps = cmd.sessionAs<stepengine::PasteSession>()) {
    const float dx = curX - ps->baseX;
    const float dy = curY - ps->baseY;
    for (const auto& ann : cmd.clipboard.content.cadAnnotations) {
      CadAnnotation p{};
      CadAnnotationPreviewTranslated(ann, dx, dy, &p);
      out->push_back(p);
    }
    return;
  }
  float sc = 1.f;
  if (const auto* ss = cmd.sessionAs<stepengine::ScaleSession>()) {
    if (ss->phase != MP::NeedDestination || !CadScalePreviewFactor(cmd, curX, curY, &sc))
      return;
    const float bx = ss->baseX;
    const float by = ss->baseY;
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Annotation)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.drawing.cadAnnotations.size())
        continue;
      CadAnnotation p{};
      CadAnnotationPreviewScaled(cmd.drawing.cadAnnotations[k], bx, by, sc, &p);
      out->push_back(p);
    }
    return;
  }
  if (const auto* ms = cmd.sessionAs<stepengine::MirrorSession>()) {
    if (ms->phase != MirP::NeedP2 && ms->phase != MirP::NeedEraseAnswer)
      return;
    const float mx0 = ms->p1X, my0 = ms->p1Y;
    float mx1 = (ms->phase == MirP::NeedP2) ? curX : ms->p2X;
    float my1 = (ms->phase == MirP::NeedP2) ? curY : ms->p2Y;
    if (ms->phase == MirP::NeedP2)
      ApplyOrthoConstrainFromAnchor(cmd, mx0, my0, &mx1, &my1, cmd.orthoMode, ms->p1Z, cmd.uiCursorWorldZ);
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Annotation)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.drawing.cadAnnotations.size())
        continue;
      CadAnnotation p{};
      CadAnnotationPreviewReflected(cmd.drawing.cadAnnotations[k], mx0, my0, mx1, my1, &p);
      out->push_back(p);
    }
    return;
  }
  const auto* rs = cmd.sessionAs<stepengine::RotateSession>();
  if (!rs)
    return;
  float theta = 0.f;
  if (!CadRotatePreviewTheta(cmd, curX, curY, &theta))
    return;
  const float bx = rs->baseX;
  const float by = rs->baseY;
  for (const auto& e : cmd.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.drawing.cadAnnotations.size())
      continue;
    CadAnnotation p{};
    CadAnnotationPreviewRotated(cmd.drawing.cadAnnotations[k], bx, by, theta, &p);
    out->push_back(p);
  }
}

void CadTableCollectTransformPreviews(const AppCommandState& cmd, float curX, float curY,
                                      std::vector<CadTable>* out) {
  if (!out)
    return;
  out->clear();
  using MP = stepengine::ModifyPhase;
  using MirP = stepengine::MirrorPhase;
  auto take = [&](const CadTable& src, auto&& xform) {
    CadTable p = src;
    xform(&p);
    out->push_back(std::move(p));
  };
  if (const auto* mc = cmd.sessionAs<stepengine::MoveCopySession>()) {
    if (mc->phase != MP::NeedDestination)
      return;
    const float dx = curX - mc->baseX;
    const float dy = curY - mc->baseY;
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Table)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.drawing.cadTables.size())
        continue;
      take(cmd.drawing.cadTables[k], [&](CadTable* t) { CadTableTranslate(t, dx, dy); });
    }
    return;
  }
  if (const auto* ps = cmd.sessionAs<stepengine::PasteSession>()) {
    const float dx = curX - ps->baseX;
    const float dy = curY - ps->baseY;
    for (const auto& t : cmd.clipboard.content.cadTables)
      take(t, [&](CadTable* p) { CadTableTranslate(p, dx, dy); });
    return;
  }
  float sc = 1.f;
  if (const auto* ss = cmd.sessionAs<stepengine::ScaleSession>()) {
    if (ss->phase != MP::NeedDestination || !CadScalePreviewFactor(cmd, curX, curY, &sc))
      return;
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Table)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.drawing.cadTables.size())
        continue;
      take(cmd.drawing.cadTables[k], [&](CadTable* t) { CadTableScaleAround(t, ss->baseX, ss->baseY, sc); });
    }
    return;
  }
  if (const auto* ms = cmd.sessionAs<stepengine::MirrorSession>()) {
    if (ms->phase != MirP::NeedP2 && ms->phase != MirP::NeedEraseAnswer)
      return;
    const float mx0 = ms->p1X, my0 = ms->p1Y;
    float mx1 = (ms->phase == MirP::NeedP2) ? curX : ms->p2X;
    float my1 = (ms->phase == MirP::NeedP2) ? curY : ms->p2Y;
    if (ms->phase == MirP::NeedP2)
      ApplyOrthoConstrainFromAnchor(cmd, mx0, my0, &mx1, &my1, cmd.orthoMode, ms->p1Z, cmd.uiCursorWorldZ);
    for (const auto& e : cmd.selection) {
      if (e.type != SelectedEntity::Type::Table)
        continue;
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.drawing.cadTables.size())
        continue;
      take(cmd.drawing.cadTables[k], [&](CadTable* t) { CadTableReflectAcrossLine(t, mx0, my0, mx1, my1); });
    }
    return;
  }
  const auto* rs = cmd.sessionAs<stepengine::RotateSession>();
  if (!rs)
    return;
  float theta = 0.f;
  if (!CadRotatePreviewTheta(cmd, curX, curY, &theta))
    return;
  for (const auto& e : cmd.selection) {
    if (e.type != SelectedEntity::Type::Table)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.drawing.cadTables.size())
      continue;
    take(cmd.drawing.cadTables[k], [&](CadTable* t) { CadTableRotateAround(t, rs->baseX, rs->baseY, theta); });
  }
}


void StartScaleCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // #764: scale paper geometry
    if (st.selectedPaperEntities.empty()) {
      log.push_back("SCALE — select paper object(s) first.");
      return;
    }
    st.active = AppCommandState::Kind::None;  // paper-space edit ops are not a model command
    st.paperScalePhase = 1;
    log.push_back("SCALE — specify base point (click or type X,Y). ESC to cancel.");
    return;
  }
  ClearPendingViewportZoom(st);
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::ModifyPhase::NeedBase : stepengine::ModifyPhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::ScaleSession>(initialPhase), true);
  if (haveSel)
    log.push_back("SCALE — specify base point (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}
