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

using GoSurvey::Document::EntityOp;
using GoSurvey::Document::SimilarityTransform;

void ApplyUniformScaleAboutBase(AppCommandState& st, float bx, float by, float bz, float sc,
                                std::vector<std::string>& log) {
  // SCALE is uniform in X, Y and Z about the base point, in plan view too, as AutoCAD's is (issue
  // #1577). REQ-060's gizmo calls this as well, so the two cannot disagree.
  if (!(sc > 0.f) || !std::isfinite(sc))
    return;
  TransformSelection(st, SimilarityTransform::UniformScale({bx, by, bz}, sc), EntityOp::Scale, "SCALE",
                     /*duplicate=*/false, log);
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
    const double dx = static_cast<double>(curX) - rs->baseX;
    const double dy = static_cast<double>(curY) - rs->baseY;
    *outThetaRad = static_cast<float>(-std::atan2(dx, dy));
  } else if (rs->phase == RP::AfterReference_WaitAngleOrP) {
    const double thetaRef = std::atan2(rs->refY2 - rs->refY1, rs->refX2 - rs->refX1);
    *outThetaRad = static_cast<float>(std::atan2(static_cast<double>(curY) - rs->baseY,
                                                 static_cast<double>(curX) - rs->baseX) -
                                      thetaRef);
  } else if (rs->phase == RP::AnglePoints_WaitP2) {
    *outThetaRad = static_cast<float>(RotateDeltaFromReferenceAndNewSegment(
        rs->refX1, rs->refY1, rs->refX2, rs->refY2, rs->anglePt1X, rs->anglePt1Y, curX, curY));
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
    CadMtextTransform(a, [&](float* x, float* y) { RotatePtForAnnotationPreview(bx, by, rad, x, y); }, rad, 1.f);
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
  if (a.kind == CadAnnotation::Kind::Mtext) {  // issue #1667: scale about the base, keep rotation/attachment
    CadMtextTransform(a, [&](float* x, float* y) { ScalePtAroundBase(bx, by, sc, x, y); }, 0.f, sc);
    a.plottedHeightInches = std::max(a.plottedHeightInches * sc, 1.e-6f);
    return;
  }
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
  if (a.kind == CadAnnotation::Kind::Mtext) {
    CadMtextTransform(a, [&](float* x, float* y) { ReflectPtAcrossLine(x0, y0, x1, y1, x, y); }, 0.f, 1.f);
  } else if (a.kind == CadAnnotation::Kind::Text) {
    ReflectPtAcrossLine(x0, y0, x1, y1, &a.insX, &a.insY);
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
