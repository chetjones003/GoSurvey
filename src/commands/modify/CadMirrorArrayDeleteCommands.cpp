#include "CadCommandsInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "modify/CadModifySessions.hpp"
#include "engine/StepEngine.hpp"
#include "surface/CadSurfaceInternal.hpp"
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
/// REQ-103 MIRROR. Drops the three entity kinds a mirror cannot represent, and says why (REQ-201)
/// rather than silently doing nothing to them: `FilledRegion` (hatches) — already out of scope for
/// rotate/scale/mirror, requirements.md's REQ-042 fills note; `Mesh` — never edited by any existing
/// transform (REQ-063, reference-only geometry); `PdfUnderlay` — its stored orientation is a scalar
/// rotation-degrees/scale pair with no way to represent a reflection (a "flip" field would be new
/// scope, not this task's). Surfaces are handled separately by the existing
/// \ref DropSurfacesFromSelectionForTransform, reused as-is.
static void DropMirrorUnsupportedFromSelection(AppCommandState& st, std::vector<std::string>& log) {
  size_t filled = 0, mesh = 0, pdf = 0;
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [&](const SelectedEntity& e) {
                                      if (e.type == SelectedEntity::Type::FilledRegion) { ++filled; return true; }
                                      if (e.type == SelectedEntity::Type::Mesh) { ++mesh; return true; }
                                      if (e.type == SelectedEntity::Type::PdfUnderlay) { ++pdf; return true; }
                                      return false;
                                    }),
                     st.selection.end());
  if (filled)
    log.push_back("MIRROR — " + std::to_string(filled) + " hatch fill(s) excluded: mirroring solid" +
                  " fills is not supported yet.");
  if (mesh)
    log.push_back("MIRROR — " + std::to_string(mesh) + " imported model(s) excluded: imported meshes" +
                  " are reference-only geometry and are never edited.");
  if (pdf)
    log.push_back("MIRROR — " + std::to_string(pdf) + " PDF underlay(s) excluded: an underlay's stored" +
                  " orientation cannot represent a reflection yet.");
}
} // namespace

namespace {
/// REQ-103 MIRROR. Same shape as \c DuplicateCadSelectionRotated — builds new entities reflected
/// across the line through (x0,y0)-(x1,y1) and appends them, never mutating the source selection in
/// place. MIRROR's default behavior always duplicates (erase-source, if requested, runs separately
/// afterward against the original selection — see \c FinishMirrorCommand), unlike ROTATE/SCALE
/// where duplication is the opt-in "copy" mode.
static void DuplicateCadSelectionReflected(AppCommandState& st, float x0, float y0, float x1, float y1,
                                           std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "MIRROR", log);
  DropPositionMarkersFromSelection(st, "MIRROR", log);  // REQ-359: move/copy/erase only
  DropMultileadersFromSelection(st, "MIRROR", log);     // REQ-367
  DropMirrorUnsupportedFromSelection(st, log);
  // REQ-351: solids and pipe runs are mirrored across the vertical plane that contains the line.
  {
    const ray3d::Vec3 dir{static_cast<double>(x1) - x0, static_cast<double>(y1) - y0, 0.0};
    const double len = ray3d::Length(dir);
    const ray3d::Vec3 normal = len > 0.0 ? ray3d::Vec3{-dir.y / len, dir.x / len, 0.0} : ray3d::Vec3{};
    MirrorSelectedSolidsAndPipeRuns(st, {static_cast<double>(x0), static_cast<double>(y0), 0.0}, normal, log);
    if (len > 0.0) {
      const ray3d::Vec3 planePoint{static_cast<double>(x0), static_cast<double>(y0), 0.0};
      DuplicateSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443
        return ray3d::ReflectPointAcrossPlane(v, planePoint, normal);
      });
      DuplicateSelectedCadPoints(st, [&](const spline::Vec3& v) {
        return ray3d::ReflectPointAcrossPlane(v, planePoint, normal);
      });
    }
  }

  const size_t polyVertsBefore = st.drawing.userPolylineVerts.size();
  const size_t featureVertsBefore = st.drawing.featureLineVerts.size();
  std::vector<float> newLines;
  std::vector<float> newCircles;
  std::vector<EntityAttributes> newLineAttrs;
  std::vector<EntityAttributes> newCircleAttrs;
  std::vector<float> newCircleNormals;   // REQ-312, one 3-float normal per new circle
  std::vector<CadAnnotation> newAnn;
  std::vector<EntityAttributes> newAnnAttrs;
  std::vector<CadTable> newTables;
  std::vector<EntityAttributes> newTableAttrs;
  std::vector<CadBlockRef> newBlockRefs;
  std::vector<EntityAttributes> newBlockRefAttrs;
  std::vector<CadArc> newArcs;
  std::vector<EntityAttributes> newArcAttrs;
  std::vector<CadEllipse> newEll;
  std::vector<EntityAttributes> newEllAttrs;

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.lineCount()) {
        float lx0 = static_cast<float>(st.drawing.line(k).x0);
        float ly0 = static_cast<float>(st.drawing.line(k).y0);
        float lz0 = static_cast<float>(st.drawing.line(k).z0);
        float lx1 = static_cast<float>(st.drawing.line(k).x1);
        float ly1 = static_cast<float>(st.drawing.line(k).y1);
        float lz1 = static_cast<float>(st.drawing.line(k).z1);
        ReflectPtAcrossLine(x0, y0, x1, y1, &lx0, &ly0);
        ReflectPtAcrossLine(x0, y0, x1, y1, &lx1, &ly1);
        newLines.push_back(lx0);
        newLines.push_back(ly0);
        newLines.push_back(lz0);
        newLines.push_back(lx1);
        newLines.push_back(ly1);
        newLines.push_back(lz1);
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userLineAttrs.size())
          a = st.drawing.userLineAttrs[static_cast<size_t>(e.index)];
        newLineAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      size_t k = static_cast<size_t>(e.index) * 4;
      size_t ci = static_cast<size_t>(e.index);
      if (ci < st.drawing.circleCount()) {
        const CadCircleRecord c = st.drawing.circle(ci);
        float cx = static_cast<float>(c.cx);
        float cy = static_cast<float>(c.cy);
        float r = static_cast<float>(c.r);
        ReflectPtAcrossLine(x0, y0, x1, y1, &cx, &cy);
        newCircles.push_back(cx);
        newCircles.push_back(cy);
        newCircles.push_back(static_cast<float>(c.cz));  // z — reflection is about a vertical plane
        newCircles.push_back(r);                            // radius is preserved (isometry)
        EntityAttributes a{};
        if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userCircleAttrs.size())
          a = st.drawing.userCircleAttrs[static_cast<size_t>(e.index)];
        newCircleAttrs.push_back(DuplicatedEntityAttrs(a));
        float nnx = 0.f, nny = 0.f, nnz = 1.f;
        CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(e.index), &nnx, &nny, &nnz);
        ReflectNormalAcrossLine(x0, y0, x1, y1, &nnx, &nny);   // REQ-312: the plane mirrors too
        PushCircleNormal(newCircleNormals, nnx, nny, nnz);
      }
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.cadAnnotations.size()) {
        CadAnnotation c = st.drawing.cadAnnotations[k];
        c.surveyPointLabelForId = -1;
        if (c.kind == CadAnnotation::Kind::Text || c.kind == CadAnnotation::Kind::Mtext) {
          // MIRRTEXT-off (design decision, D-2026-08-23-j): insertion point reflects, glyphs stay
          // upright — rotationRad is deliberately left untouched.
          ReflectPtAcrossLine(x0, y0, x1, y1, &c.insX, &c.insY);
          if (c.kind == CadAnnotation::Kind::Mtext) {
            float xs[4] = {c.boxMinX, c.boxMaxX, c.boxMaxX, c.boxMinX};
            float ys[4] = {c.boxMinY, c.boxMinY, c.boxMaxY, c.boxMaxY};
            float mnX = xs[0], mxX = xs[0], mnY = ys[0], mxY = ys[0];
            for (int i = 0; i < 4; ++i) {
              ReflectPtAcrossLine(x0, y0, x1, y1, &xs[i], &ys[i]);
              mnX = std::min(mnX, xs[i]);
              mxX = std::max(mxX, xs[i]);
              mnY = std::min(mnY, ys[i]);
              mxY = std::max(mxY, ys[i]);
            }
            c.boxMinX = mnX;
            c.boxMaxX = mxX;
            c.boxMinY = mnY;
            c.boxMaxY = mxY;
            c.insX = mnX;
            c.insY = mnY;
          }
        } else if (c.kind == CadAnnotation::Kind::DimLinear) {
          ReflectCadDimLinearAcrossLine(x0, y0, x1, y1, &c);
        } else if (c.kind == CadAnnotation::Kind::DimAligned) {
          ReflectPtAcrossLine(x0, y0, x1, y1, &c.insX, &c.insY);
          ReflectPtAcrossLine(x0, y0, x1, y1, &c.dimExt1X, &c.dimExt1Y);
          ReflectPtAcrossLine(x0, y0, x1, y1, &c.dimExt2X, &c.dimExt2Y);
          float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, ml = 0.f;
          if (CadDimAlignedGeometry(c, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &ml))
            c.rotationRad = CadDimReadableTextRotation(std::atan2(ty, tx));
        } else {
          // Generic fallback (DimAngular and anything else): reflect insertion point + AABB
          // corners and re-fit — the same weaker path ROTATE takes for these kinds.
          ReflectPtAcrossLine(x0, y0, x1, y1, &c.insX, &c.insY);
          float xs[4] = {c.boxMinX, c.boxMaxX, c.boxMaxX, c.boxMinX};
          float ys[4] = {c.boxMinY, c.boxMinY, c.boxMaxY, c.boxMaxY};
          float mnX = xs[0], mxX = xs[0], mnY = ys[0], mxY = ys[0];
          for (int i = 0; i < 4; ++i) {
            ReflectPtAcrossLine(x0, y0, x1, y1, &xs[i], &ys[i]);
            mnX = std::min(mnX, xs[i]);
            mxX = std::max(mxX, xs[i]);
            mnY = std::min(mnY, ys[i]);
            mxY = std::max(mxY, ys[i]);
          }
          c.boxMinX = mnX;
          c.boxMaxX = mxX;
          c.boxMinY = mnY;
          c.boxMaxY = mxY;
          c.insX = mnX;
          c.insY = mnY;
        }
        newAnn.push_back(std::move(c));
        EntityAttributes a{};
        if (k < st.drawing.cadAnnotationAttrs.size())
          a = st.drawing.cadAnnotationAttrs[k];
        newAnnAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Table) {
      const size_t tk = static_cast<size_t>(e.index);
      if (tk < st.drawing.cadTables.size()) {
        CadTable c = st.drawing.cadTables[tk];
        CadTableReflectAcrossLine(&c, x0, y0, x1, y1);
        newTables.push_back(std::move(c));
        EntityAttributes a{};
        if (tk < st.drawing.cadTableAttrs.size())
          a = st.drawing.cadTableAttrs[tk];
        newTableAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      const size_t bk = static_cast<size_t>(e.index);
      if (bk < st.drawing.cadBlockRefs.size()) {
        CadBlockRef c = st.drawing.cadBlockRefs[bk];
        CadBlockMirror(&c, x0, y0, x1, y1);
        newBlockRefs.push_back(std::move(c));
        EntityAttributes a{};
        if (bk < st.drawing.cadBlockRefAttrs.size())
          a = st.drawing.cadBlockRefAttrs[bk];
        newBlockRefAttrs.push_back(DuplicatedEntityAttrs(a));
      }
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userArcs.size()) {
        CadArc a = st.drawing.userArcs[k];
        // A reflection reverses handedness (CCW becomes CW): reflecting the center alone does not
        // say which way the arc now sweeps. Reflect the OLD END angle into the NEW START angle and
        // keep sweepRad's magnitude — this is the CCW-from-start arc that covers the identical
        // reflected point set (verified by hand for axis-aligned and symmetric cases; see TASK-094).
        // Where the far end lands, taken through the arc's OWN plane before anything moves. A
        // mirror line lies in plan, so it reflects X and Y and leaves Z alone.
        ray3d::Vec3 farEnd = CurveWorldPointOnArc(a, static_cast<double>(a.startRad) +
                                                         static_cast<double>(a.sweepRad));
        float fex = static_cast<float>(farEnd.x);
        float fey = static_cast<float>(farEnd.y);
        ReflectPtAcrossLine(x0, y0, x1, y1, &fex, &fey);
        farEnd.x = static_cast<double>(fex);
        farEnd.y = static_cast<double>(fey);
        const float newStart = ReflectAngleAcrossLine(x0, y0, x1, y1, a.startRad + a.sweepRad);
        ReflectPtAcrossLine(x0, y0, x1, y1, &a.cx, &a.cy);
        a.startRad = newStart;
        ReflectNormalAcrossLine(x0, y0, x1, y1, &a.nx, &a.ny);   // REQ-312: so does the plane
        CadReanchorArcStart(&a, farEnd);  // REQ-312: a no-op on a flat arc, so newStart stands there
        newArcs.push_back(a);
        EntityAttributes at{};
        if (k < st.drawing.userArcAttrs.size())
          at = st.drawing.userArcAttrs[k];
        newArcAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k < st.drawing.userEllipses.size()) {
        CadEllipse el = st.drawing.userEllipses[k];
        float mx = static_cast<float>(el.cx + el.majVx);
        float my = static_cast<float>(el.cy + el.majVy);
        ReflectPtAcrossLine(x0, y0, x1, y1, &el.cx, &el.cy);
        ReflectPtAcrossLine(x0, y0, x1, y1, &mx, &my);
        el.majVx = static_cast<float>(mx - el.cx);
        el.majVy = static_cast<float>(my - el.cy);
        newEll.push_back(el);
        EntityAttributes at{};
        if (k < st.drawing.userEllAttrs.size())
          at = st.drawing.userEllAttrs[k];
        newEllAttrs.push_back(DuplicatedEntityAttrs(at));
      }
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (st.drawing.userPolylineOffsets.empty())
        st.drawing.userPolylineOffsets.push_back(0);
      const int baseVert = st.drawing.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        float px = static_cast<float>(st.drawing.vertex(vi).x);
        float py = static_cast<float>(st.drawing.vertex(vi).y);
        float pz = static_cast<float>(st.drawing.vertex(vi).z);
        ReflectPtAcrossLine(x0, y0, x1, y1, &px, &py);
        st.drawing.pushVertex(CadPolyVertex{px, py, pz});
      }
      st.drawing.userPolylineOffsets.push_back(baseVert + nv);
      // REQ-316 / ADR-047: Inc 1 flattens a copied arc polyline to straight (bulges default 0);
      // arc-aware MOVE/COPY/ROTATE/MIRROR is Inc 3.
      SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
      SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
      uint8_t cl = 0;
      if (static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size())
        cl = st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
      st.drawing.userPolylineClosed.push_back(cl);
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size())
        at = st.drawing.userPolylineAttrs[static_cast<size_t>(pi)];
      st.drawing.userPolylineAttrs.push_back(DuplicatedEntityAttrs(at));
    }
  }
  st.drawing.userLinesFlat.insert(st.drawing.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.drawing.userCirclesCxCyZR.insert(st.drawing.userCirclesCxCyZR.end(), newCircles.begin(), newCircles.end());
  st.drawing.userLineAttrs.insert(st.drawing.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.drawing.userCircleAttrs.insert(st.drawing.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.drawing.userCircleNormals.insert(st.drawing.userCircleNormals.end(), newCircleNormals.begin(),
                              newCircleNormals.end());
  st.drawing.cadAnnotations.insert(st.drawing.cadAnnotations.end(), newAnn.begin(), newAnn.end());
  st.drawing.cadAnnotationAttrs.insert(st.drawing.cadAnnotationAttrs.end(), newAnnAttrs.begin(), newAnnAttrs.end());
  st.drawing.cadTables.insert(st.drawing.cadTables.end(), newTables.begin(), newTables.end());
  st.drawing.cadTableAttrs.insert(st.drawing.cadTableAttrs.end(), newTableAttrs.begin(), newTableAttrs.end());
  st.drawing.cadBlockRefs.insert(st.drawing.cadBlockRefs.end(), newBlockRefs.begin(), newBlockRefs.end());
  st.drawing.cadBlockRefAttrs.insert(st.drawing.cadBlockRefAttrs.end(), newBlockRefAttrs.begin(), newBlockRefAttrs.end());
  st.drawing.userArcs.insert(st.drawing.userArcs.end(), newArcs.begin(), newArcs.end());
  st.drawing.userArcAttrs.insert(st.drawing.userArcAttrs.end(), newArcAttrs.begin(), newArcAttrs.end());
  st.drawing.userEllipses.insert(st.drawing.userEllipses.end(), newEll.begin(), newEll.end());
  st.drawing.userEllAttrs.insert(st.drawing.userEllAttrs.end(), newEllAttrs.begin(), newEllAttrs.end());

  // Feature lines (REQ-087) — same append as the rotated/translated paths, differing only in the
  // point function, which is exactly why all three go through AppendFeatureLineCopy.
  ForEachSelectedFeatureLine(st, [&](int fi, int v0, int v1) {
    AppendFeatureLineCopy(st, fi, v0, v1,
                          [&](float* x, float* y) { ReflectPtAcrossLine(x0, y0, x1, y1, x, y); });
  });

  if (!newLines.empty() || !newCircles.empty() || !newAnn.empty() || !newArcs.empty() || !newEll.empty() ||
      !newBlockRefs.empty() ||
      st.drawing.userPolylineVerts.size() != polyVertsBefore ||
      st.drawing.featureLineVerts.size() != featureVertsBefore)
    BumpCadGpuCache(st);
}
} // namespace

/// entity type that primitive supports. \p arrayRotateItems == false needs every copy to KEEP the
/// source orientation while still landing on the circle: reusing the existing selection-centroid
/// helper (\c ComputeSelectionCentroidWorld, already used by ROTATE's reference path) as a single
/// rigid-body anchor, rotating THAT ONE POINT about the axis, and translating the whole selection by
/// the resulting 3D delta — this path has no per-entity-type orientation gap at all (nothing's
/// orientation changes), so it already worked under a tilted UCS before REQ-328 existed.
static void CommitArrayPolar(AppCommandState& st, std::vector<std::string>& log) {
  const auto* arr = st.sessionAs<stepengine::ArraySession>();
  if (!arr)
    return;
  constexpr float kTwoPi = 6.28318530717958647692f;
  const int n = std::max(arr->itemCount, 1);
  const float fillAngleDeg = arr->fillAngleDeg;
  const bool fullTurn = std::fabs(std::fmod(fillAngleDeg, 360.f)) < 1e-3f && n > 0;
  const float fillRad = fillAngleDeg * (kTwoPi / 360.f);
  const float step = (n <= 1) ? 0.f : (fullTurn ? fillRad / static_cast<float>(n)
                                                : fillRad / static_cast<float>(n - 1));
  const float anchorX = arr->anchorX, anchorY = arr->anchorY, anchorZ = arr->anchorZ;
  const float centerX = arr->centerX, centerY = arr->centerY, centerZ = arr->centerZ;
  const bool rotateItems = arr->rotateItems;
  const bool onPaper = arr->onPaper;
  if (onPaper) {
    const std::vector<PaperEntityRef> source = st.selectedPaperEntities;
    std::vector<std::string> transformLog;
    PushUndoSnapshot(st, "Array-Polar");
    for (int i = 1; i < n; ++i) {
      const float angle = step * static_cast<float>(i);
      const float dx = anchorX - centerX;
      const float dy = anchorY - centerY;
      const float targetX = centerX + dx * std::cos(angle) - dy * std::sin(angle);
      const float targetY = centerY + dx * std::sin(angle) + dy * std::cos(angle);
      st.selectedPaperEntities = source;
      if (rotateItems) {
        TranslateSelectedPaperEntities(st, 0.f, 0.f, true, transformLog, false);
        RotateSelectedPaperEntities(st, centerX, centerY, angle, transformLog, false);
      } else {
        TranslateSelectedPaperEntities(st, targetX - anchorX, targetY - anchorY, true, transformLog, false);
      }
    }
    st.selectedPaperEntities = source;
    FinishArrayCommand(st, log, n, "polar");
    return;
  }
  const ray3d::Vec3 axisPoint{static_cast<double>(centerX), static_cast<double>(centerY),
                              static_cast<double>(centerZ)};
  const ucs::Ucs frame = CadActiveUcsStorage(st);
  const ray3d::Vec3 axisUnit = ray3d::Normalize(ray3d::Vec3{frame.zAxis.x, frame.zAxis.y, frame.zAxis.z});
  PushUndoSnapshot(st, "Array-Polar");
  for (int i = 1; i < n; ++i) {
    const float ang = step * static_cast<float>(i);
    if (rotateItems) {
      RotateSelectionAboutAxis(st, axisPoint, axisUnit, ang, log);
    } else {
      const ray3d::Vec3 rotated = ray3d::RotatePointAboutAxis({anchorX, anchorY, anchorZ}, axisPoint, axisUnit, ang);
      DuplicateCadSelectionTranslated(st, static_cast<float>(rotated.x) - anchorX,
                                     static_cast<float>(rotated.y) - anchorY,
                                     static_cast<float>(rotated.z) - anchorZ);
    }
  }
  FinishArrayCommand(st, log, n, "polar");
}

bool HandleArrayText(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log) {
  auto* arr = st.sessionAs<stepengine::ArraySession>();
  if (!arr)
    return false;
  using AP = stepengine::ArrayPhase;
  using AT = stepengine::ArrayType;
  const std::string line = StringUtil::trimCopy(lineIn);
  const std::string low = StringUtil::toLowerAsciiCopy(line);

  if (arr->phase == AP::WaitType) {
    if (low == "r" || low == "rectangular") {
      arr->type = AT::Rectangular;
      arr->phase = AP::Rect_WaitColumns;
      log.push_back("ARRAY Rectangular — number of columns:");
      return true;
    }
    if (low == "p" || low == "polar") {
      arr->type = AT::Polar;
      arr->phase = AP::Polar_WaitCenter;
      log.push_back("ARRAY Polar — specify center point:");
      return true;
    }
    return false;
  }

  if (arr->phase == AP::Rect_WaitColumns) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v >= 1.f) || !std::isfinite(v)) {
      log.push_back("ARRAY Rectangular — number of columns must be a positive whole number.");
      return false;
    }
    arr->cols = static_cast<int>(std::lround(v));
    arr->phase = AP::Rect_WaitColumnSpacing;
    log.push_back("ARRAY Rectangular — column spacing (type a distance, or click):");
    return true;
  }
  if (arr->phase == AP::Rect_WaitColumnSpacing) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !std::isfinite(v)) {
      log.push_back("ARRAY Rectangular — column spacing must be a finite number (may be negative).");
      return false;
    }
    arr->colSpacing = v;
    arr->phase = AP::Rect_WaitRows;
    log.push_back("ARRAY Rectangular — number of rows:");
    return true;
  }
  if (arr->phase == AP::Rect_WaitRows) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v >= 1.f) || !std::isfinite(v)) {
      log.push_back("ARRAY Rectangular — number of rows must be a positive whole number.");
      return false;
    }
    arr->rows = static_cast<int>(std::lround(v));
    arr->phase = AP::Rect_WaitRowSpacing;
    log.push_back("ARRAY Rectangular — row spacing (type a distance, or click):");
    return true;
  }
  if (arr->phase == AP::Rect_WaitRowSpacing) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !std::isfinite(v)) {
      log.push_back("ARRAY Rectangular — row spacing must be a finite number (may be negative).");
      return false;
    }
    arr->rowSpacing = v;
    if (arr->onPaper) {
      arr->levels = 1;
      CommitArrayRectangular(st, log);
      return true;
    }
    arr->phase = AP::Rect_WaitLevels;
    log.push_back("ARRAY Rectangular — number of levels <1 = 2D>:");
    return true;
  }
  if (arr->phase == AP::Rect_WaitLevels) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v >= 1.f) || !std::isfinite(v)) {
      log.push_back("ARRAY Rectangular — number of levels must be a positive whole number (Enter for 1 = 2D).");
      return false;
    }
    arr->levels = static_cast<int>(std::lround(v));
    if (arr->levels <= 1) {
      CommitArrayRectangular(st, log);
      return true;
    }
    arr->phase = AP::Rect_WaitLevelSpacing;
    log.push_back("ARRAY Rectangular — level spacing (type a distance):");
    return true;
  }
  if (arr->phase == AP::Rect_WaitLevelSpacing) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !std::isfinite(v)) {
      log.push_back("ARRAY Rectangular — level spacing must be a finite number (may be negative).");
      return false;
    }
    arr->levelSpacing = v;
    CommitArrayRectangular(st, log);
    return true;
  }

  if (arr->phase == AP::Polar_WaitCenter) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f))
      return false;
    arr->centerX = px;
    arr->centerY = py;
    arr->centerZ = CadCommitElevation(st);
    arr->phase = AP::Polar_WaitItemCount;
    log.push_back("ARRAY Polar — number of items (total, including the original):");
    return true;
  }
  if (arr->phase == AP::Polar_WaitItemCount) {
    float v = 0.f;
    if (!ParseOneFloat(line, &v) || !(v >= 1.f) || !std::isfinite(v)) {
      log.push_back("ARRAY Polar — number of items must be a positive whole number.");
      return false;
    }
    arr->itemCount = static_cast<int>(std::lround(v));
    arr->phase = AP::Polar_WaitAngle;
    log.push_back("ARRAY Polar — angle to fill in degrees (type, or click) <360>:");
    return true;
  }
  if (arr->phase == AP::Polar_WaitAngle) {
    float deg = 0.f;
    if (!ParseAngleDegreesInternal(line, &deg) || !std::isfinite(deg)) {
      log.push_back("ARRAY Polar — angle to fill must be a finite number of degrees.");
      return false;
    }
    arr->fillAngleDeg = deg;
    arr->phase = AP::Polar_WaitRotateAnswer;
    log.push_back("ARRAY Polar — rotate items? [Y]es / [N]o:");
    return true;
  }
  if (arr->phase == AP::Polar_WaitRotateAnswer) {
    if (low == "y" || low == "yes") {
      arr->rotateItems = true;
      CommitArrayPolar(st, log);
      return true;
    }
    if (low == "n" || low == "no") {
      arr->rotateItems = false;
      CommitArrayPolar(st, log);
      return true;
    }
    return false;
  }

  return false;
}


/// one standalone entity per segment: a LINE for a straight segment, an ARC for a bulge (curved)
/// segment — flat or tilted (REQ-316 / ADR-047, REQ-325 / ADR-053), never tessellated to chords. Each
/// output entity keeps the source polyline's own per-vertex Z (REQ-057) and a copy of its layer /
/// colour / linetype attributes with a fresh id (REQ-076 / ADR-027). A closed polyline also emits its
/// implied closing segment. Non-polyline, non-block selected entities are left untouched and named to
/// \p log (REQ-201); block references are decomposed separately by the caller. Returns the number of
/// polylines exploded. The caller owns the undo snapshot, the id sweep and the GPU-cache bump.
int ExplodeSelectedPolylines(AppCommandState& st, std::vector<std::string>& log) {
  std::vector<int> polyIdx;
  std::set<std::string> otherKinds;
  for (const SelectedEntity& e : st.selection) {
    using T = SelectedEntity::Type;
    switch (e.type) {
      case T::Polyline:
        if (e.index >= 0)
          polyIdx.push_back(e.index);
        break;
      case T::BlockRef:
        break;  // the caller's ExplodeRef path handles these
      case T::LineSeg:      otherKinds.insert("line"); break;
      case T::Circle:       otherKinds.insert("circle"); break;
      case T::Arc:          otherKinds.insert("arc"); break;
      case T::Ellipse:      otherKinds.insert("ellipse"); break;
      case T::Spline:       otherKinds.insert("spline"); break;  // REQ-443
      case T::Point:        otherKinds.insert("point"); break;   // REQ-456
      case T::PositionMarker: otherKinds.insert("position marker"); break;  // -Wswitch with Point
      case T::Multileader:    otherKinds.insert("multileader"); break;      // -Wswitch with Point
      case T::Annotation:   otherKinds.insert("text"); break;
      case T::FilledRegion: otherKinds.insert("hatch"); break;
      case T::Mesh:         otherKinds.insert("mesh"); break;
      case T::FeatureLine:  otherKinds.insert("feature line"); break;
      case T::Surface:      otherKinds.insert("surface"); break;
      case T::Table:        otherKinds.insert("table"); break;
      case T::Solid:        otherKinds.insert("solid"); break;
      case T::PdfUnderlay:  otherKinds.insert("PDF underlay"); break;
      case T::PipeRun:      otherKinds.insert("pipe run"); break;
      case T::PointCloud:   otherKinds.insert("point cloud"); break;
      // REQ-343 amended / ADR-059 (i), issue #479. Unreachable here: the section plane is never an
      // entry in `st.selection` (a type TAG only; its own `sectionPlaneSelected` bool tracks
      // selection instead). Case added explicitly to keep this switch -Wswitch-clean.
      case T::SectionPlane: otherKinds.insert("section plane"); break;
    }
  }
  std::sort(polyIdx.begin(), polyIdx.end());
  polyIdx.erase(std::unique(polyIdx.begin(), polyIdx.end()), polyIdx.end());

  for (const std::string& k : otherKinds)
    log.push_back("EXPLODE — " + k + " skipped: not decomposable into line/arc segments (REQ-201).");

  if (polyIdx.empty())
    return 0;

  // Model space only, matching the existing INSERT-explode paper-space note — the paper store is a
  // separate PaperLayout, not st.userPolyline* (ADR-009/013).
  if (ActivePaperGeometryTarget(st) != nullptr) {
    log.push_back("EXPLODE — polyline explode is model space only.");
    return 0;
  }

  struct OutLine {
    float x0, y0, z0, x1, y1, z1;
    EntityAttributes at;
  };
  struct OutArc {
    CadArc a;
    EntityAttributes at;
  };
  std::vector<OutLine> outLines;
  std::vector<OutArc> outArcs;

  const int np = static_cast<int>(st.drawing.userPolylineOffsets.size()) - 1;

  auto vertAt = [&](int vi) -> ray3d::Vec3 {
    const size_t k = static_cast<size_t>(vi);
    return ray3d::Vec3{static_cast<double>(st.drawing.vertex(k).x),
                       static_cast<double>(st.drawing.vertex(k).y),
                       static_cast<double>(st.drawing.vertex(k).z)};
  };
  auto bulgeAt = [&](int vi) -> float {
    return static_cast<size_t>(vi) < st.drawing.userPolylineVertsBulge.size()
               ? st.drawing.userPolylineVertsBulge[static_cast<size_t>(vi)]
               : 0.f;
  };
  auto normalAt = [&](int vi, float* nx, float* ny, float* nz) {
    const size_t k = static_cast<size_t>(vi) * 3;
    if (k + 2 < st.drawing.userPolylineVertsNormal.size()) {
      *nx = st.drawing.userPolylineVertsNormal[k];
      *ny = st.drawing.userPolylineVertsNormal[k + 1];
      *nz = st.drawing.userPolylineVertsNormal[k + 2];
    } else {
      *nx = 0.f; *ny = 0.f; *nz = 1.f;
    }
  };

  auto pushLine = [&](const ray3d::Vec3& A, const ray3d::Vec3& B, const EntityAttributes& at) {
    outLines.push_back({static_cast<float>(A.x), static_cast<float>(A.y), static_cast<float>(A.z),
                        static_cast<float>(B.x), static_cast<float>(B.y), static_cast<float>(B.z),
                        DuplicatedEntityAttrs(at)});
  };

  // One polyline segment A->B carrying `bulge` in the plane `n` becomes a LINE (bulge ~ 0, or the
  // bulge/tilt math degenerates) or an ARC. The tilted-arc construction is the same one issue #373's
  // 3D FILLET solve, REQ-325's render/pick increments and DxfIo's split-on-export all use: an ad-hoc
  // frame at A gives the true world centre, then the arc's own CANONICAL frame re-derives the angles,
  // because that is the frame every reader of CadArc::startRad/sweepRad works in.
  auto emitSegment = [&](const ray3d::Vec3& A, const ray3d::Vec3& B, float bulge, float nx, float ny,
                         float nz, const EntityAttributes& at) {
    if (std::fabs(bulge) <= 1e-9f) {
      pushLine(A, B, at);
      return;
    }
    CadArc arc{};
    if (IsFlatNormal(nx, ny, nz)) {
      // A flat-normal bulge whose ends sit at different elevations is inconsistent data (a flat arc
      // lies in one Z plane) — keep the true 3D endpoints as a straight segment rather than pick one.
      if (std::fabs(A.z - B.z) > 1e-6) {
        pushLine(A, B, at);
        return;
      }
      const BulgeArcSpan s = BulgeArc(A.x, A.y, B.x, B.y, static_cast<double>(bulge));
      if (!s.valid) {
        pushLine(A, B, at);
        return;
      }
      arc.cx = static_cast<float>(s.cx);
      arc.cy = static_cast<float>(s.cy);
      arc.r = static_cast<float>(s.radius);
      arc.z = static_cast<float>(A.z);  // flat arc: z is the plane elevation, shared by both ends
      arc.startRad = static_cast<float>(s.startAngle);
      arc.sweepRad = static_cast<float>(s.sweep);
      arc.nx = 0.f; arc.ny = 0.f; arc.nz = 1.f;
    } else {
      const ray3d::Vec3 nrm{static_cast<double>(nx), static_cast<double>(ny), static_cast<double>(nz)};
      ucs::Ucs plane{};
      if (!ucs::FromNormal(A, nrm, &plane)) {
        pushLine(A, B, at);
        return;
      }
      const ucs::Point2D bLocal = ucs::WorldToPlane(plane, B);
      const BulgeArcSpan s = BulgeArc(0.0, 0.0, bLocal.x, bLocal.y, static_cast<double>(bulge));
      if (!s.valid) {
        pushLine(A, B, at);
        return;
      }
      const ray3d::Vec3 centreWorld = ucs::PlaneToWorld(plane, ucs::Point2D{s.cx, s.cy});
      ucs::Ucs canon{};
      if (!ucs::FromNormal(centreWorld, nrm, &canon)) {
        pushLine(A, B, at);
        return;
      }
      const ucs::Point2D sLocal = ucs::WorldToPlane(canon, A);
      const ucs::Point2D eLocal = ucs::WorldToPlane(canon, B);
      const double thetaA = std::atan2(sLocal.y, sLocal.x);
      const double thetaB = std::atan2(eLocal.y, eLocal.x);
      constexpr double kTwoPi = 6.28318530717958647692;
      double sweep = thetaB - thetaA;
      if (bulge >= 0.f) {
        while (sweep < 0.0) sweep += kTwoPi;
      } else {
        while (sweep > 0.0) sweep -= kTwoPi;
      }
      arc.cx = static_cast<float>(centreWorld.x);
      arc.cy = static_cast<float>(centreWorld.y);
      arc.z = static_cast<float>(centreWorld.z);
      arc.r = static_cast<float>(s.radius);
      arc.startRad = static_cast<float>(thetaA);
      arc.sweepRad = static_cast<float>(sweep);
      arc.nx = nx; arc.ny = ny; arc.nz = nz;
    }
    outArcs.push_back({arc, DuplicatedEntityAttrs(at)});
  };

  // Read every segment out FIRST — the erase loop below rewrites the polyline arrays.
  for (int pi : polyIdx) {
    if (pi < 0 || pi >= np)
      continue;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    if (v1 - v0 < 2)
      continue;  // a degenerate 1-vertex polyline has no segment to make
    const EntityAttributes at = static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size()
                                    ? st.drawing.userPolylineAttrs[static_cast<size_t>(pi)]
                                    : MakeNewEntityAttrs(st);
    for (int vi = v0; vi + 1 < v1; ++vi) {
      float nx, ny, nz;
      normalAt(vi, &nx, &ny, &nz);
      emitSegment(vertAt(vi), vertAt(vi + 1), bulgeAt(vi), nx, ny, nz, at);
    }
    const bool closed = static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() &&
                        st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    if (closed && v1 - v0 >= 3) {
      // A closed polyline may or may not already repeat its first vertex at the end; only add the
      // implied closing segment when the last vertex is genuinely distinct from the first.
      const ray3d::Vec3 last = vertAt(v1 - 1);
      const ray3d::Vec3 first = vertAt(v0);
      if (ray3d::Length(ray3d::Sub(last, first)) > 1e-6) {
        float nx, ny, nz;
        normalAt(v1 - 1, &nx, &ny, &nz);
        emitSegment(last, first, bulgeAt(v1 - 1), nx, ny, nz, at);
      }
    }
  }

  // Erase the source polylines high index first, so each removal leaves the lower ones addressable.
  for (int i = static_cast<int>(polyIdx.size()) - 1; i >= 0; --i)
    ErasePolylineByIndex(st, polyIdx[static_cast<size_t>(i)]);

  for (const OutLine& L : outLines) {
    st.drawing.pushLine(CadLineRecord{L.x0, L.y0, L.z0, L.x1, L.y1, L.z1});
    st.drawing.userLineAttrs.push_back(L.at);
  }
  for (const OutArc& A : outArcs) {
    st.drawing.userArcs.push_back(A.a);
    st.drawing.userArcAttrs.push_back(A.at);
  }
  return static_cast<int>(polyIdx.size());
}

/// REQ-087. The polyline eraser's twin, with one extra array: the per-VERTEX elevation-point flags
/// are cut over the same [3a, 3b) span the vertices are — in flags, that is [a, b), not the triplet
/// range. Getting that wrong would leave the flag array the right length overall while shifting every
/// flag after the erased line by a vertex or three, which is silent (ADR-035 (a)).
static void EraseFeatureLineByIndex(AppCommandState& st, int fi) {
  if (fi < 0 || (static_cast<size_t>(fi)+ 1) >= st.drawing.featureLineOffsets.size())
    return;
  const int nfl = static_cast<int>(st.drawing.featureLineOffsets.size()) - 1;
  std::vector<int> nvPer(static_cast<size_t>(nfl));
  for (int i = 0; i < nfl; ++i)
    nvPer[static_cast<size_t>(i)] =
        st.drawing.featureLineOffsets[(static_cast<size_t>(i)+ 1)] - st.drawing.featureLineOffsets[static_cast<size_t>(i)];
  const int a = st.drawing.featureLineOffsets[static_cast<size_t>(fi)];
  const int b = st.drawing.featureLineOffsets[(static_cast<size_t>(fi)+ 1)];
  if ((static_cast<size_t>(3)* b) <= st.drawing.featureLineVerts.size())
    st.drawing.featureLineVerts.erase(st.drawing.featureLineVerts.begin() + (static_cast<std::ptrdiff_t>(3)* a),
                              st.drawing.featureLineVerts.begin() + (static_cast<std::ptrdiff_t>(3)* b));
  if (static_cast<size_t>(b) <= st.drawing.featureLineElevPt.size())
    st.drawing.featureLineElevPt.erase(st.drawing.featureLineElevPt.begin() + static_cast<std::ptrdiff_t>(a),
                               st.drawing.featureLineElevPt.begin() + static_cast<std::ptrdiff_t>(b));
  std::vector<int> newOff;
  newOff.reserve((static_cast<size_t>(std::max(0, nfl - 1))+ 1));
  newOff.push_back(0);
  int run = 0;
  for (int i = 0; i < nfl; ++i) {
    if (i == fi)
      continue;
    run += nvPer[static_cast<size_t>(i)];
    newOff.push_back(run);
  }
  // Same rule as polylines, and for the same reason (issue #60): zero feature lines is spelled as an
  // EMPTY table, never `{0}`. The invariant added for this store checks it too.
  if (newOff.size() == 1)
    newOff.clear();
  st.drawing.featureLineOffsets = std::move(newOff);
  if (static_cast<size_t>(fi) < st.drawing.featureLineClosed.size())
    st.drawing.featureLineClosed.erase(st.drawing.featureLineClosed.begin() + static_cast<std::ptrdiff_t>(fi));
  if (static_cast<size_t>(fi) < st.drawing.featureLineInfo.size())
    st.drawing.featureLineInfo.erase(st.drawing.featureLineInfo.begin() + static_cast<std::ptrdiff_t>(fi));
  if (static_cast<size_t>(fi) < st.drawing.featureLineAttrs.size())
    st.drawing.featureLineAttrs.erase(st.drawing.featureLineAttrs.begin() + static_cast<std::ptrdiff_t>(fi));
}

void EraseCadAnnotationAtIndex(AppCommandState& st, size_t annIndex) {
  if (annIndex >= st.drawing.cadAnnotations.size())
    return;
  // Clear the owning point's link. There is deliberately **no renumbering pass** here any more:
  // survey points reference their label by stable id (REQ-076 / architecture §11.9), so erasing an
  // annotation cannot change what any other point's link means. The loop this replaces walked every
  // survey point decrementing indices, and it was the evidence that index references do not scale —
  // one reference pair, ~46 maintenance sites (ADR-027 Context).
  const std::uint64_t doomedId = annIndex < st.drawing.cadAnnotationAttrs.size()
                                     ? st.drawing.cadAnnotationAttrs[annIndex].id
                                     : 0;
  if (doomedId != 0)
    for (SurveyPoint& q : st.drawing.surveyPoints)
      if (q.labelMtextAnnId == doomedId)
        q.labelMtextAnnId = 0;
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [&](const SelectedEntity& e) {
                                      return e.type == SelectedEntity::Type::Annotation &&
                                             e.index == static_cast<int>(annIndex);
                                    }),
                     st.selection.end());
  for (SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::Annotation && e.index > static_cast<int>(annIndex))
      --e.index;
  }
  st.drawing.cadAnnotations.erase(st.drawing.cadAnnotations.begin() + static_cast<std::ptrdiff_t>(annIndex));
  if (annIndex < st.drawing.cadAnnotationAttrs.size())
    st.drawing.cadAnnotationAttrs.erase(st.drawing.cadAnnotationAttrs.begin() + static_cast<std::ptrdiff_t>(annIndex));
}

void EraseCadTableAtIndex(AppCommandState& st, size_t tableIndex) {
  if (tableIndex >= st.drawing.cadTables.size())
    return;
  if (st.tableCellEditorOpen && st.tableCellEditorIndex == static_cast<int>(tableIndex))
    CancelTableCellEditor(st);
  else if (st.tableCellEditorOpen && st.tableCellEditorIndex > static_cast<int>(tableIndex))
    --st.tableCellEditorIndex;
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [&](const SelectedEntity& e) {
                                      return e.type == SelectedEntity::Type::Table &&
                                             e.index == static_cast<int>(tableIndex);
                                    }),
                     st.selection.end());
  for (SelectedEntity& e : st.selection) {
    if (e.type == SelectedEntity::Type::Table && e.index > static_cast<int>(tableIndex))
      --e.index;
  }
  st.drawing.cadTables.erase(st.drawing.cadTables.begin() + static_cast<std::ptrdiff_t>(tableIndex));
  if (tableIndex < st.drawing.cadTableAttrs.size())
    st.drawing.cadTableAttrs.erase(st.drawing.cadTableAttrs.begin() + static_cast<std::ptrdiff_t>(tableIndex));
}

void DeleteSelectedSurveyPoints(AppCommandState& st, std::vector<std::string>& log) {
  if (st.selectedSurveyPointIndices.empty())
    return;
  PushUndoSnapshot(st, "Delete survey points");
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end(), std::greater<int>());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  size_t n = 0;
  for (int i : ix) {
    if (i >= 0 && static_cast<size_t>(i) < st.drawing.surveyPoints.size()) {
      RemoveSurveyPointAt(st, static_cast<size_t>(i));
      ++n;
    }
  }
  st.selectedSurveyPointIndices.clear();
  if (n > 0) {
    BumpCadGpuCache(st);
    log.push_back("Deleted " + std::to_string(n) + " survey point(s).");
  }
}

void SyncSurveyPointLinkedMtextSelection(AppCommandState& st, int surveyPointIndex) {
  if (surveyPointIndex < 0 || static_cast<size_t>(surveyPointIndex) >= st.drawing.surveyPoints.size())
    return;
  const bool selected =
      std::find(st.selectedSurveyPointIndices.begin(), st.selectedSurveyPointIndices.end(), surveyPointIndex) !=
      st.selectedSurveyPointIndices.end();
  const int annIx = FindSurveyLabelAnnIndex(st, st.drawing.surveyPoints[static_cast<size_t>(surveyPointIndex)]);
  if (annIx < 0)
    return;
  const auto hasAnnSel = [&]() {
    return std::find_if(st.selection.begin(), st.selection.end(), [&](const SelectedEntity& e) {
             return e.type == SelectedEntity::Type::Annotation && e.index == annIx;
           }) != st.selection.end();
  };
  if (selected) {
    if (!hasAnnSel()) {
      SelectedEntity se{};
      se.type = SelectedEntity::Type::Annotation;
      se.index = annIx;
      st.selection.push_back(se);
    }
  } else {
    st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                      [&](const SelectedEntity& e) {
                                        return e.type == SelectedEntity::Type::Annotation && e.index == annIx;
                                      }),
                       st.selection.end());
  }
}

void ApplyLinkedSurveyForAnnotationPick(AppCommandState& st, int annIndex, bool keyShift) {
  if (annIndex < 0 || static_cast<size_t>(annIndex) >= st.drawing.cadAnnotations.size())
    return;
  const CadAnnotation& a = st.drawing.cadAnnotations[static_cast<size_t>(annIndex)];
  if (a.kind != CadAnnotation::Kind::Mtext || a.surveyPointLabelForId < 0)
    return;
  const int spi = SurveyPointIndexForId(st, a.surveyPointLabelForId);
  if (spi < 0)
    return;
  auto& sv = st.selectedSurveyPointIndices;
  const auto sit = std::find(sv.begin(), sv.end(), spi);
  if (keyShift) {
    if (sit != sv.end())
      sv.erase(sit);
    else
      sv.push_back(spi);
  } else {
    sv.clear();
    sv.push_back(spi);
  }
}

/// Remove every selected solid and pipe run (and a deleted run's place in its piping network).
/// Shared by ERASE and MIRROR's erase-source (REQ-351), which each push their own one undo
/// snapshot before calling it.
static void EraseSelectedSolidsAndPipeRuns(AppCommandState& st) {
  // B-rep solids (REQ-313) — the caller has already pushed one snapshot for this whole erase, so
  // removing the pointer here is that one undo step, exactly as it is for a mesh in ERASE. The
  // tessellation cache is NOT touched: its entries key on a weak_ptr, so the erased solid's entry
  // simply expires and is reaped by the next refresh.
  std::set<int> solidIx;
  const size_t nSolid = st.drawing.cadSolids.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::Solid && e.index >= 0 && static_cast<size_t>(e.index) < nSolid)
      solidIx.insert(e.index);
  }
  std::vector<int> sov(solidIx.begin(), solidIx.end());
  std::sort(sov.begin(), sov.end(), std::greater<int>());
  for (int idx : sov) {
    st.drawing.cadSolids.erase(st.drawing.cadSolids.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.cadSolidAttrs.size())
      st.drawing.cadSolidAttrs.erase(st.drawing.cadSolidAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  // Pipe runs (issue #486) — same shape as the B-rep solids just above: display-and-erase only,
  // the caller's one undo snapshot already covers this removal.
  std::set<int> pipeRunIx;
  const size_t nPipeRun = st.drawing.cadPipeRuns.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::PipeRun && e.index >= 0 && static_cast<size_t>(e.index) < nPipeRun)
      pipeRunIx.insert(e.index);
  }
  std::vector<int> prv(pipeRunIx.begin(), pipeRunIx.end());
  std::sort(prv.begin(), prv.end(), std::greater<int>());
  for (int idx : prv) {
    st.drawing.cadPipeRuns.erase(st.drawing.cadPipeRuns.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.cadPipeRunAttrs.size())
      st.drawing.cadPipeRunAttrs.erase(st.drawing.cadPipeRunAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
    // Piping networks (issue #486 increment B3 / REQ-345) reference cadPipeRuns by index — a
    // deleted run drops out of whichever network held it, and every index above it shifts down by
    // one, the same reindexing a std::vector erase itself just did to cadPipeRuns.
    for (CadPipingSystem& sys : st.drawing.cadPipingSystems) {
      auto& runs = sys.pipeRunIndices;
      runs.erase(std::remove(runs.begin(), runs.end(), idx), runs.end());
      for (int& ri : runs) {
        if (ri > idx)
          --ri;
      }
    }
  }
  // Alignments (REQ-480): erased with the solids and pipe runs, so MIRROR's delete-source takes them too. The
  // caller's one undo snapshot already covers this removal. Offsets of an erased parent become independent.
  {
    std::set<int> alignIx;
    for (const auto& e : st.selection) {
      if (e.type == SelectedEntity::Type::Alignment && e.index >= 0 &&
          static_cast<size_t>(e.index) < st.drawing.alignments.size())
        alignIx.insert(e.index);
    }
    for (auto it = alignIx.rbegin(); it != alignIx.rend(); ++it)
      st.drawing.alignments.erase(st.drawing.alignments.begin() + static_cast<std::ptrdiff_t>(*it));
    if (!alignIx.empty()) {
      std::vector<std::string> quiet;
      RefreshAlignmentOffsetsOnly(st, quiet);  // feature links are checked by ExecuteDeleteSelection, which logs drops
      BumpCadGpuCache(st);  // the plan drops the erased alignment and its sample lines and frames at once
    }
  }
}

void ExecuteDeleteSelection(AppCommandState& st, std::vector<std::string>& log) {
  if (st.selection.empty())
    return;
  EnsureEntityIds(st);
  std::vector<std::uint64_t> erasedFeatureIds;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::FeatureLine && e.index >= 0 &&
        static_cast<size_t>(e.index) < st.drawing.featureLineAttrs.size())
      erasedFeatureIds.push_back(st.drawing.featureLineAttrs[static_cast<size_t>(e.index)].id);
  std::vector<std::pair<size_t, CadSurface>> rebuiltGroups;
  std::vector<std::string> rebuiltGroupNames;
  for (size_t i = 0; i < st.drawing.cadSurfaces.size(); ++i) {
    const auto& source = st.drawing.cadSurfaces[i];
    if (!source.gradingGroup) continue;
    CadSurface candidate = source;
    const auto end = std::remove_if(candidate.gradingMembers.begin(), candidate.gradingMembers.end(),
      [&](const auto& member) { return std::find(erasedFeatureIds.begin(), erasedFeatureIds.end(), member.gradingId) != erasedFeatureIds.end(); });
    if (end == candidate.gradingMembers.end()) continue;
    candidate.gradingMembers.erase(end, candidate.gradingMembers.end());
    if (!BuildSurfaceFromSources(st, candidate, log)) {
      log.push_back("ERASE - group contribution could not be rebuilt; nothing was deleted.");
      return;
    }
    rebuiltGroups.emplace_back(i, std::move(candidate));
    rebuiltGroupNames.push_back(source.name);
  }
  PushUndoSnapshot(st, "Delete");
  for (auto& [index, candidate] : rebuiltGroups) st.drawing.cadSurfaces[index] = std::move(candidate);
  std::set<int> lineIx;
  std::set<int> circIx;
  std::set<int> annIx;
  std::set<int> tableIx;
  std::set<int> blockIx;
  std::set<int> arcIx;
  std::set<int> ellIx;
  std::set<int> polyIx;
  std::set<int> flIx;  // REQ-087
  std::set<int> markerIx;  // REQ-359
  std::set<int> multileaderIx;  // REQ-367
  const size_t nLines = st.drawing.lineCount();
  const size_t nCirc = st.drawing.circleCount();
  const size_t nAnn = st.drawing.cadAnnotations.size();
  const size_t nArc = st.drawing.userArcs.size();
  const size_t nEll = st.drawing.userEllipses.size();
  const size_t nPoly = st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0;
  const size_t nFl = st.drawing.featureLineOffsets.size() > 0 ? st.drawing.featureLineOffsets.size() - 1 : 0;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg && e.index >= 0 && static_cast<size_t>(e.index) < nLines)
      lineIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Circle && e.index >= 0 && static_cast<size_t>(e.index) < nCirc)
      circIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Annotation && e.index >= 0 && static_cast<size_t>(e.index) < nAnn)
      annIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Table && e.index >= 0 &&
             static_cast<size_t>(e.index) < st.drawing.cadTables.size())
      tableIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::BlockRef && e.index >= 0 &&
             static_cast<size_t>(e.index) < st.drawing.cadBlockRefs.size())
      blockIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Arc && e.index >= 0 && static_cast<size_t>(e.index) < nArc)
      arcIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Ellipse && e.index >= 0 && static_cast<size_t>(e.index) < nEll)
      ellIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Polyline && e.index >= 0 && static_cast<size_t>(e.index) < nPoly)
      polyIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::FeatureLine && e.index >= 0 &&
             static_cast<size_t>(e.index) < nFl)
      flIx.insert(e.index);  // REQ-087
    else if (e.type == SelectedEntity::Type::PositionMarker && e.index >= 0 &&
             static_cast<size_t>(e.index) < st.drawing.cadPositionMarkers.size())
      markerIx.insert(e.index);  // REQ-359: the marker and its label are one object
    else if (e.type == SelectedEntity::Type::Multileader && e.index >= 0 &&
             static_cast<size_t>(e.index) < st.drawing.cadMultileaders.size())
      multileaderIx.insert(e.index);  // REQ-367: path + label are one object
  }

  std::vector<int> pv(polyIx.begin(), polyIx.end());
  std::sort(pv.begin(), pv.end(), std::greater<int>());
  for (int idx : pv)
    ErasePolylineByIndex(st, idx);

  // REQ-087. Highest index first, like every other compacting erase here, so an earlier removal
  // cannot shift an index that has not been used yet.
  std::vector<int> flv(flIx.begin(), flIx.end());
  std::sort(flv.begin(), flv.end(), std::greater<int>());
  for (int idx : flv)
    EraseFeatureLineByIndex(st, idx);

  std::vector<int> lv(lineIx.begin(), lineIx.end());
  std::sort(lv.begin(), lv.end(), std::greater<int>());
  for (int idx : lv) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.lineCount())
      continue;
    st.drawing.eraseLine(k);
    if (static_cast<size_t>(idx) < st.drawing.userLineAttrs.size())
      st.drawing.userLineAttrs.erase(st.drawing.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> cv(circIx.begin(), circIx.end());
  std::sort(cv.begin(), cv.end(), std::greater<int>());
  for (int idx : cv) {
    if (static_cast<size_t>(idx) >= st.drawing.circleCount())
      continue;
    // One whole record (stride 4) comes out. Removing three floats left the array a non-multiple of
    // the stride and shifted every later circle by one slot (issue #62); eraseCircle removes all four.
    st.drawing.eraseCircle(static_cast<size_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.userCircleAttrs.size())
      st.drawing.userCircleAttrs.erase(st.drawing.userCircleAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
    EraseCircleNormal(st.drawing.userCircleNormals, static_cast<size_t>(idx));
  }

  std::vector<int> av(annIx.begin(), annIx.end());
  std::sort(av.begin(), av.end(), std::greater<int>());
  for (int idx : av)
    EraseCadAnnotationAtIndex(st, static_cast<size_t>(idx));

  std::vector<int> tv(tableIx.begin(), tableIx.end());
  std::sort(tv.begin(), tv.end(), std::greater<int>());
  for (int idx : tv)
    EraseCadTableAtIndex(st, static_cast<size_t>(idx));

  std::vector<int> bv(blockIx.begin(), blockIx.end());
  std::sort(bv.begin(), bv.end(), std::greater<int>());
  for (int idx : bv) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.cadBlockRefs.size())
      continue;
    st.drawing.cadBlockRefs.erase(st.drawing.cadBlockRefs.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.drawing.cadBlockRefAttrs.size())
      st.drawing.cadBlockRefAttrs.erase(st.drawing.cadBlockRefAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> arv(arcIx.begin(), arcIx.end());
  std::sort(arv.begin(), arv.end(), std::greater<int>());
  for (int idx : arv) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.userArcs.size())
      continue;
    st.drawing.userArcs.erase(st.drawing.userArcs.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.drawing.userArcAttrs.size())
      st.drawing.userArcAttrs.erase(st.drawing.userArcAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> ev(ellIx.begin(), ellIx.end());
  std::sort(ev.begin(), ev.end(), std::greater<int>());
  for (int idx : ev) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.userEllipses.size())
      continue;
    st.drawing.userEllipses.erase(st.drawing.userEllipses.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.drawing.userEllAttrs.size())
      st.drawing.userEllAttrs.erase(st.drawing.userEllAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }
  EraseSelectedSplines(st);  // REQ-443
  EraseSelectedCadPoints(st);

  // Filled regions (REQ-042): erase from cadFilledRegions + parallel attrs, highest index first.
  std::set<int> fillIx;
  const size_t nFill = st.drawing.cadFilledRegions.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::FilledRegion && e.index >= 0 && static_cast<size_t>(e.index) < nFill)
      fillIx.insert(e.index);
  }
  std::vector<int> fv(fillIx.begin(), fillIx.end());
  std::sort(fv.begin(), fv.end(), std::greater<int>());
  for (int idx : fv) {
    st.drawing.cadFilledRegions.erase(st.drawing.cadFilledRegions.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.cadFilledRegionAttrs.size())
      st.drawing.cadFilledRegionAttrs.erase(st.drawing.cadFilledRegionAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  // Imported meshes (REQ-063: "erasing a mesh is undoable in one step" — the caller has already
  // pushed one snapshot for this whole erase, so removing the pointer here is that one step).
  std::set<int> meshIx;
  const size_t nMesh = st.drawing.cadMeshes.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::Mesh && e.index >= 0 && static_cast<size_t>(e.index) < nMesh)
      meshIx.insert(e.index);
  }
  std::vector<int> mv(meshIx.begin(), meshIx.end());
  std::sort(mv.begin(), mv.end(), std::greater<int>());
  for (int idx : mv) {
    st.drawing.cadMeshes.erase(st.drawing.cadMeshes.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.cadMeshAttrs.size())
      st.drawing.cadMeshAttrs.erase(st.drawing.cadMeshAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  // Point clouds (REQ-171: "erase + undo is one step" — same shape as the mesh erase above).
  std::set<int> pointCloudIx;
  const size_t nPointCloud = st.drawing.cadPointClouds.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::PointCloud && e.index >= 0 &&
        static_cast<size_t>(e.index) < nPointCloud)
      pointCloudIx.insert(e.index);
  }
  std::vector<int> pcv(pointCloudIx.begin(), pointCloudIx.end());
  std::sort(pcv.begin(), pcv.end(), std::greater<int>());
  for (int idx : pcv) {
    st.drawing.cadPointClouds.erase(st.drawing.cadPointClouds.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.cadPointCloudAttrs.size())
      st.drawing.cadPointCloudAttrs.erase(st.drawing.cadPointCloudAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  EraseSelectedSolidsAndPipeRuns(st);  // REQ-313 / issue #486; shared with MIRROR's erase-source
  RefreshFeatureLineLinks(st, log);    // REQ-498.2: a feature line whose alignment was just erased says so here


  // TIN surfaces (REQ-068: "erasing a surface is undoable in one step" — the caller has already
  // pushed one snapshot for this whole erase, so removing it here is that one step).
  //
  // Goes through EraseSurfaceAtIndex rather than erasing the two vectors inline like the blocks
  // above, so there is one erase path to keep correct. An in-flight rebuild (REQ-069) needs no
  // cancellation here: the job is keyed on the surface's stable id (ADR-036 (a)), that id is never
  // reused (REQ-076), so when the worker finishes `FindSurfaceIndexById` returns -1 and the result is
  // discarded — REQ-069's own rule, reached without a second mechanism.
  {
    std::set<int> surfIx;
    const size_t nSurf = st.drawing.cadSurfaces.size();
    for (const auto& e : st.selection) {
      if (e.type == SelectedEntity::Type::Surface && e.index >= 0 && static_cast<size_t>(e.index) < nSurf)
        surfIx.insert(e.index);
    }
    std::vector<int> sv(surfIx.begin(), surfIx.end());
    std::sort(sv.begin(), sv.end(), std::greater<int>());
    for (int idx : sv)
      EraseSurfaceAtIndex(st, static_cast<size_t>(idx));
  }

  // PDF underlays: release GL texture and erase from highest index downward.
  std::set<int> pdfIx;
  const size_t nPdf = st.drawing.pdfAttachments.size();
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::PdfUnderlay && e.index >= 0 &&
        static_cast<size_t>(e.index) < nPdf)
      pdfIx.insert(e.index);
  }
  std::vector<int> pdfv(pdfIx.begin(), pdfIx.end());
  std::sort(pdfv.begin(), pdfv.end(), std::greater<int>());
  for (int idx : pdfv) {
    PdfAttach_ReleaseTexture(st.drawing.pdfAttachments[static_cast<size_t>(idx)]);
    st.drawing.pdfAttachments.erase(st.drawing.pdfAttachments.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  // Position Markers (REQ-359), highest index first so an earlier erase never shifts a later one.
  for (auto it = markerIx.rbegin(); it != markerIx.rend(); ++it) {
    const auto at = static_cast<std::ptrdiff_t>(*it);
    st.drawing.cadPositionMarkers.erase(st.drawing.cadPositionMarkers.begin() + at);
    if (static_cast<size_t>(*it) < st.drawing.cadPositionMarkerAttrs.size())
      st.drawing.cadPositionMarkerAttrs.erase(st.drawing.cadPositionMarkerAttrs.begin() + at);
  }
  if (!markerIx.empty() && st.mtextRichEditorMarkerIndex >= 0)
    CloseMtextRichEditorUi(st);  // its target may be gone or renumbered

  for (auto it = multileaderIx.rbegin(); it != multileaderIx.rend(); ++it) {
    const auto at = static_cast<std::ptrdiff_t>(*it);
    st.drawing.cadMultileaders.erase(st.drawing.cadMultileaders.begin() + at);
    if (static_cast<size_t>(*it) < st.drawing.cadMultileaderAttrs.size())
      st.drawing.cadMultileaderAttrs.erase(st.drawing.cadMultileaderAttrs.begin() + at);
  }

  const size_t nDel = lineIx.size() + circIx.size() + annIx.size() + tableIx.size() + arcIx.size() + ellIx.size() +
                        polyIx.size() + flIx.size() + pdfIx.size() + fillIx.size() + meshIx.size() + markerIx.size() +
                      multileaderIx.size();
  st.selection.clear();
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  if (nDel > 0) {
    BumpCadGpuCache(st);
    for (const auto& name : rebuiltGroupNames) {
      const int index = FindSurfaceIndex(st, name);
      if (index >= 0) {
        st.drawing.cadSurfaces[static_cast<size_t>(index)].builtAtRevision = st.cadGpuRevision;
        MarkVolumeSurfacesDirtyForParent(st, name);
      }
    }
    log.push_back("Deleted " + std::to_string(nDel) + " object(s).");
  }
}

/// REQ-103 MIRROR erase-source step. The same per-store erase \c ExecuteDeleteSelection performs,
/// minus its own \c PushUndoSnapshot — \c FinishMirrorCommand already pushed one snapshot for the
/// whole mirror(+erase) operation, satisfying REQ-103's "one undo step" condition — and minus the
/// FilledRegion/Mesh/Surface/PdfUnderlay blocks, which cannot appear here: by the time this runs,
/// \c st.selection is what survived \c DuplicateCadSelectionReflected's exclusion filtering (those
/// four kinds already stripped, with a logged reason).
static void EraseMirroredSourceNoUndo(AppCommandState& st) {
  std::set<int> lineIx, circIx, annIx, arcIx, ellIx, polyIx, flIx, tableIx, blockIx;
  const size_t nLines = st.drawing.lineCount();
  const size_t nCirc = st.drawing.circleCount();
  const size_t nAnn = st.drawing.cadAnnotations.size();
  const size_t nArc = st.drawing.userArcs.size();
  const size_t nEll = st.drawing.userEllipses.size();
  const size_t nPoly = st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0;
  const size_t nFl = st.drawing.featureLineOffsets.size() > 0 ? st.drawing.featureLineOffsets.size() - 1 : 0;
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg && e.index >= 0 && static_cast<size_t>(e.index) < nLines)
      lineIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Circle && e.index >= 0 && static_cast<size_t>(e.index) < nCirc)
      circIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Annotation && e.index >= 0 && static_cast<size_t>(e.index) < nAnn)
      annIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Arc && e.index >= 0 && static_cast<size_t>(e.index) < nArc)
      arcIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Ellipse && e.index >= 0 && static_cast<size_t>(e.index) < nEll)
      ellIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::Polyline && e.index >= 0 && static_cast<size_t>(e.index) < nPoly)
      polyIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::FeatureLine && e.index >= 0 && static_cast<size_t>(e.index) < nFl)
      flIx.insert(e.index);
    // Tables and block references (a pipe fitting is one) are mirrored by the flat path, so
    // erase-source must remove their originals too (REQ-351).
    else if (e.type == SelectedEntity::Type::Table && e.index >= 0 &&
             static_cast<size_t>(e.index) < st.drawing.cadTables.size())
      tableIx.insert(e.index);
    else if (e.type == SelectedEntity::Type::BlockRef && e.index >= 0 &&
             static_cast<size_t>(e.index) < st.drawing.cadBlockRefs.size())
      blockIx.insert(e.index);
  }

  std::vector<int> pv(polyIx.begin(), polyIx.end());
  std::sort(pv.begin(), pv.end(), std::greater<int>());
  for (int idx : pv)
    ErasePolylineByIndex(st, idx);

  std::vector<int> flv(flIx.begin(), flIx.end());
  std::sort(flv.begin(), flv.end(), std::greater<int>());
  for (int idx : flv)
    EraseFeatureLineByIndex(st, idx);

  // REQ-351: MIRROR now mirrors solids and pipe runs, so erase-source must remove their originals.
  EraseSelectedSolidsAndPipeRuns(st);

  std::vector<int> tv(tableIx.begin(), tableIx.end());
  std::sort(tv.begin(), tv.end(), std::greater<int>());
  for (int idx : tv)
    EraseCadTableAtIndex(st, static_cast<size_t>(idx));

  std::vector<int> bv(blockIx.begin(), blockIx.end());
  std::sort(bv.begin(), bv.end(), std::greater<int>());
  for (int idx : bv) {
    st.drawing.cadBlockRefs.erase(st.drawing.cadBlockRefs.begin() + static_cast<std::ptrdiff_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.cadBlockRefAttrs.size())
      st.drawing.cadBlockRefAttrs.erase(st.drawing.cadBlockRefAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> lv(lineIx.begin(), lineIx.end());
  std::sort(lv.begin(), lv.end(), std::greater<int>());
  for (int idx : lv) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.lineCount())
      continue;
    st.drawing.eraseLine(k);
    if (static_cast<size_t>(idx) < st.drawing.userLineAttrs.size())
      st.drawing.userLineAttrs.erase(st.drawing.userLineAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> cv(circIx.begin(), circIx.end());
  std::sort(cv.begin(), cv.end(), std::greater<int>());
  for (int idx : cv) {
    if (static_cast<size_t>(idx) >= st.drawing.circleCount())
      continue;
    st.drawing.eraseCircle(static_cast<size_t>(idx));
    if (static_cast<size_t>(idx) < st.drawing.userCircleAttrs.size())
      st.drawing.userCircleAttrs.erase(st.drawing.userCircleAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
    EraseCircleNormal(st.drawing.userCircleNormals, static_cast<size_t>(idx));
  }

  std::vector<int> av(annIx.begin(), annIx.end());
  std::sort(av.begin(), av.end(), std::greater<int>());
  for (int idx : av)
    EraseCadAnnotationAtIndex(st, static_cast<size_t>(idx));

  std::vector<int> arv(arcIx.begin(), arcIx.end());
  std::sort(arv.begin(), arv.end(), std::greater<int>());
  for (int idx : arv) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.userArcs.size())
      continue;
    st.drawing.userArcs.erase(st.drawing.userArcs.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.drawing.userArcAttrs.size())
      st.drawing.userArcAttrs.erase(st.drawing.userArcAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }

  std::vector<int> ev(ellIx.begin(), ellIx.end());
  std::sort(ev.begin(), ev.end(), std::greater<int>());
  for (int idx : ev) {
    const size_t k = static_cast<size_t>(idx);
    if (k >= st.drawing.userEllipses.size())
      continue;
    st.drawing.userEllipses.erase(st.drawing.userEllipses.begin() + static_cast<std::ptrdiff_t>(idx));
    if (k < st.drawing.userEllAttrs.size())
      st.drawing.userEllAttrs.erase(st.drawing.userEllAttrs.begin() + static_cast<std::ptrdiff_t>(idx));
  }
  EraseSelectedSplines(st);  // REQ-443
  EraseSelectedCadPoints(st);

  st.selection.clear();
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  BumpCadGpuCache(st);
}

/// REQ-103 MIRROR erase-source step, survey-point half. Same loop \c DeleteSelectedSurveyPoints
/// performs, minus its own \c PushUndoSnapshot (see \ref EraseMirroredSourceNoUndo).
static void EraseSelectedSurveyPointsNoUndo(AppCommandState& st) {
  std::vector<int> ix = st.selectedSurveyPointIndices;
  std::sort(ix.begin(), ix.end(), std::greater<int>());
  ix.erase(std::unique(ix.begin(), ix.end()), ix.end());
  for (int i : ix)
    if (i >= 0 && static_cast<size_t>(i) < st.drawing.surveyPoints.size())
      RemoveSurveyPointAt(st, static_cast<size_t>(i));
  st.selectedSurveyPointIndices.clear();
}

/// REQ-329 increment 5 (GitHub issue #402): mirror the selection across the PLANE that contains the
/// picked line and is perpendicular to the active UCS plane (its normal = line direction x UCS Z).
/// Appends the reflected duplicates the way \c DuplicateCadSelectionReflected does, and leaves
/// \c st.selection holding the mirrored sources so \c FinishMirrorCommand's erase-source path is
/// unchanged.
///
/// Entity set: Line / Circle (+ plane normal) / Polyline rotate fully in 3D — a bare vertex has no
/// orientation to preserve, and a circle's normal is a direction. Arc (reflection reverses arc
/// handedness — its own geometry problem), Ellipse / Annotation / Table / BlockRef / feature line
/// (no stored plane normal, or a 2D-only duplication helper — the exact boundary REQ-328 item 2
/// drew) and survey points (the 2D duplicate-ID modal) are REFUSED by name under a tilted mirror
/// plane. Solids (`brep::Mirror`) and pipe runs (their path) are mirrored across any plane (REQ-351).
/// Under the World UCS (and any plan-rotated UCS) `FinishMirrorCommand` never calls this — the flat
/// `DuplicateCadSelectionReflected` runs unchanged.
static void DuplicateCadSelectionReflectedAcrossPlane(AppCommandState& st, const ray3d::Vec3& planePt,
                                                      const ray3d::Vec3& planeUnit,
                                                      std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "MIRROR", log);
  DropPositionMarkersFromSelection(st, "MIRROR", log);  // REQ-359: move/copy/erase only
  DropMultileadersFromSelection(st, "MIRROR", log);     // REQ-367
  DropMirrorUnsupportedFromSelection(st, log);  // FilledRegion / Mesh / PdfUnderlay
  MirrorSelectedSolidsAndPipeRuns(st, planePt, planeUnit, log);  // REQ-351
  const auto rp = [&](float x, float y, float z) {
    return ray3d::ReflectPointAcrossPlane({x, y, z}, planePt, planeUnit);
  };
  const auto rv = [&](float x, float y, float z) {
    return ray3d::ReflectVectorAcrossPlane({x, y, z}, planeUnit);
  };
  DuplicateSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443
    return ray3d::ReflectPointAcrossPlane(v, planePt, planeUnit);
  });
  DuplicateSelectedCadPoints(st, [&](const spline::Vec3& v) {
    return ray3d::ReflectPointAcrossPlane(v, planePt, planeUnit);
  });
  const size_t polyVertsBefore = st.drawing.userPolylineVerts.size();
  std::vector<float> newLines, newCircles, newCircleNormals;
  std::vector<EntityAttributes> newLineAttrs, newCircleAttrs;
  size_t refused = 0;

  for (const auto& e : st.selection) {
    switch (e.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.lineCount())
        break;
      const ray3d::Vec3 p0 = rp(static_cast<float>(st.drawing.line(k).x0), static_cast<float>(st.drawing.line(k).y0), static_cast<float>(st.drawing.line(k).z0));
      const ray3d::Vec3 p1 = rp(static_cast<float>(st.drawing.line(k).x1), static_cast<float>(st.drawing.line(k).y1), static_cast<float>(st.drawing.line(k).z1));
      for (const ray3d::Vec3& p : {p0, p1}) {
        newLines.push_back(static_cast<float>(p.x));
        newLines.push_back(static_cast<float>(p.y));
        newLines.push_back(static_cast<float>(p.z));
      }
      EntityAttributes a{};
      if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userLineAttrs.size())
        a = st.drawing.userLineAttrs[static_cast<size_t>(e.index)];
      newLineAttrs.push_back(DuplicatedEntityAttrs(a));
      break;
    }
    case SelectedEntity::Type::Circle: {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci >= st.drawing.circleCount())
        break;
      const CadCircleRecord cc = st.drawing.circle(ci);
      const ray3d::Vec3 c = rp(static_cast<float>(cc.cx), static_cast<float>(cc.cy), static_cast<float>(cc.cz));
      newCircles.push_back(static_cast<float>(c.x));
      newCircles.push_back(static_cast<float>(c.y));
      newCircles.push_back(static_cast<float>(c.z));
      newCircles.push_back(static_cast<float>(cc.r));  // radius preserved (an isometry)
      EntityAttributes a{};
      if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userCircleAttrs.size())
        a = st.drawing.userCircleAttrs[static_cast<size_t>(e.index)];
      newCircleAttrs.push_back(DuplicatedEntityAttrs(a));
      float nx = 0.f, ny = 0.f, nz = 1.f;
      CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(e.index), &nx, &ny, &nz);
      const ray3d::Vec3 n = rv(nx, ny, nz);
      PushCircleNormal(newCircleNormals, static_cast<float>(n.x), static_cast<float>(n.y),
                       static_cast<float>(n.z));
      break;
    }
    case SelectedEntity::Type::Polyline: {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        break;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      if (v1 - v0 < 2)
        break;
      if (st.drawing.userPolylineOffsets.empty())
        st.drawing.userPolylineOffsets.push_back(0);
      const int baseVert = st.drawing.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        const size_t b = static_cast<size_t>(vi);
        const ray3d::Vec3 p = rp(static_cast<float>(st.drawing.vertex(b).x), static_cast<float>(st.drawing.vertex(b).y), static_cast<float>(st.drawing.vertex(b).z));
        st.drawing.pushVertex(CadPolyVertex{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
      }
      st.drawing.userPolylineOffsets.push_back(baseVert + (v1 - v0));
      SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
      SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
      st.drawing.userPolylineClosed.push_back(static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size()
                                          ? st.drawing.userPolylineClosed[static_cast<size_t>(pi)]
                                          : static_cast<uint8_t>(0));
      EntityAttributes at{};
      if (static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size())
        at = st.drawing.userPolylineAttrs[static_cast<size_t>(pi)];
      st.drawing.userPolylineAttrs.push_back(DuplicatedEntityAttrs(at));
      break;
    }
    default:
      break;
    }
  }
  // Types this pass cannot reflect across a tilted plane are removed from the selection (not just
  // counted) so FinishMirrorCommand's erase-source path leaves them untouched — the same contract
  // DropMirrorUnsupportedFromSelection keeps for FilledRegion/Mesh/PDF.
  const size_t beforeErase = st.selection.size();
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [](const SelectedEntity& e) {
                                      return e.type == SelectedEntity::Type::Arc ||
                                             e.type == SelectedEntity::Type::Ellipse ||
                                             e.type == SelectedEntity::Type::Annotation ||
                                             e.type == SelectedEntity::Type::Table ||
                                             e.type == SelectedEntity::Type::BlockRef ||
                                             e.type == SelectedEntity::Type::FeatureLine;
                                    }),
                     st.selection.end());
  refused += beforeErase - st.selection.size();
  refused += st.selectedSurveyPointIndices.size();

  st.drawing.userLinesFlat.insert(st.drawing.userLinesFlat.end(), newLines.begin(), newLines.end());
  st.drawing.userLineAttrs.insert(st.drawing.userLineAttrs.end(), newLineAttrs.begin(), newLineAttrs.end());
  st.drawing.userCirclesCxCyZR.insert(st.drawing.userCirclesCxCyZR.end(), newCircles.begin(), newCircles.end());
  st.drawing.userCircleAttrs.insert(st.drawing.userCircleAttrs.end(), newCircleAttrs.begin(), newCircleAttrs.end());
  st.drawing.userCircleNormals.insert(st.drawing.userCircleNormals.end(), newCircleNormals.begin(), newCircleNormals.end());

  if (refused > 0)
    log.push_back("MIRROR — " + std::to_string(refused) +
                  " entity(ies) excluded: arc/ellipse/text/table/block/feature-line/survey-point"
                  " reflection across a tilted mirror plane is not supported yet (REQ-328) — reflect"
                  " across a mirror line on a work plane with an upright Z axis.");
  if (!newLines.empty() || !newCircles.empty() || st.drawing.userPolylineVerts.size() != polyVertsBefore)
    BumpCadGpuCache(st);
}

void FinishMirrorTransform(AppCommandState& st, float x0, float y0, float z0, float x1, float y1, float z1,
                           bool eraseSource, std::vector<std::string>& log) {
  PushUndoSnapshot(st, eraseSource ? "Mirror-erase" : "Mirror");
  const bool hadSurveySelection =
      !st.selectedSurveyPointIndices.empty() && CadWorkPlaneIsWorldXy(st);
  if (CadWorkPlaneIsWorldXy(st)) {
    DuplicateCadSelectionReflected(st, x0, y0, x1, y1, log);
  } else {
    const ray3d::Vec3 p1{static_cast<double>(x0), static_cast<double>(y0), static_cast<double>(z0)};
    const ray3d::Vec3 p2{static_cast<double>(x1), static_cast<double>(y1), static_cast<double>(z1)};
    const ucs::Ucs u = CadActiveUcsStorage(st);
    const ray3d::Vec3 normal = ray3d::Cross(ray3d::Sub(p2, p1), {u.zAxis.x, u.zAxis.y, u.zAxis.z});
    if (ray3d::Length(normal) < 1e-9) {
      log.push_back("MIRROR — the mirror line must lie in the work plane (it is parallel to the UCS Z"
                    " axis here); pick two points on the plane.");
      stepengine::EndSession(st);
      return;
    }
    DuplicateCadSelectionReflectedAcrossPlane(st, p1, ray3d::Normalize(normal), log);
  }
  if (eraseSource && !st.selection.empty())
    EraseMirroredSourceNoUndo(st);
  stepengine::EndSession(st);
  if (hadSurveySelection) {
    st.pendingSurveyDupIsMirror = true;
    st.pendingMirrorX0 = x0;
    st.pendingMirrorY0 = y0;
    st.pendingMirrorX1 = x1;
    st.pendingMirrorY1 = y1;
    st.mirrorEraseSourcePending = eraseSource;
    st.copySurveyDupModalOpen = true;
    st.copySurveyDupModalOpenRequested = true;
    log.push_back("MIRROR — CAD duplicated; choose survey ID policy.");
  } else {
    log.push_back(eraseSource ? "MIRROR complete (source erased)." : "MIRROR complete.");
  }
}


void StartDeleteCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // paper space: geometry + viewports
    const bool hadEntities = !st.selectedPaperEntities.empty();
    const bool hadViewports = !st.selectedViewports.empty();
    if (hadEntities)
      DeleteSelectedPaperEntities(st, log);  // REQ-037
    if (hadViewports)
      DeleteSelectedViewports(st, log);  // REQ-035
    if (!hadEntities && !hadViewports) {
      // REQ-307 (GitHub #106): nothing pre-selected — open a real selection step instead of
      // refusing, mirroring model-space DELETE's own accumulate-until-Enter shape (REQ-121).
      st.paperDeleteWaitingSelection = true;
      log.push_back("DELETE — click objects or drag a selection window (Enter when done) to erase them. "
                    "ESC cancels.");
    }
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  // A SELECTED SECTION PLANE is what DELETE means first (REQ-343 amended, user report 2026-09-15:
  // "it will not let me use the delete command or button ... to delete it").
  //
  // It is not in `st.selection` — ADR-059 (h) keeps a view state out of that vector so no consumer
  // of it needs a branch for one — and the consequence, unnoticed until someone tried it, is that
  // DELETE walked straight past a plane the user could plainly see was selected and opened a
  // "click objects" prompt instead. The flag has to be tested somewhere; here is the only place
  // that means "erase what is selected".
  //
  // Deleting it turns the clip OFF rather than erasing geometry, because there is no geometry: the
  // plane IS the clip. It DOES now make an undo entry (REQ-343 amended, issue #479 acceptance 8) —
  // the plane is a real entity with real persistence now, and a DELETE a user cannot UNDO would be
  // the odd one out among every other DELETE in this file.
  //
  // Before the survey-point branch, and above the selection branch, because the two are mutually
  // exclusive in practice: selecting the plane clears the entity selection and vice versa. If both
  // were somehow live, the plane is the thing the user was last working with and the thing whose
  // handles are on screen.
  if (st.sectionPlaneSelected) {
    PushUndoSnapshot(st, "Delete section plane");
    st.sectionPlaneSelected = false;
    CancelSectionPlaneGripDrag(st);
    st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
    st.viewportSectionClip = false;
    // The frame and the stretched size go too. Keeping them would leave a plane that reappears in
    // its old place on the next `SECTIONCLIP ON` — which is not what "delete" means anywhere else.
    st.viewportSectionClipFrameValid = false;
    st.viewportSectionClipExtent = SectionPlaneExtent{};
    st.viewportSectionClipOffset = 0.0;
    st.viewportSectionClipFlip = false;
    log.push_back("Section plane deleted — the clip is off and the whole model is visible again.");
    return;
  }
  // Survey points take priority: deleting a point also removes its linked label.
  // Checking selection first caused the label annotation to be deleted on the first
  // keypress (since SyncSurveyPointLinkedMtextSelection adds the label to st.selection),
  // requiring a second Delete to remove the point itself.
  if (!st.selectedSurveyPointIndices.empty()) {
    DeleteSelectedSurveyPoints(st, log);
    if (!st.selection.empty())
      ExecuteDeleteSelection(st, log);
    return;
  }
  if (!st.selection.empty()) {
    ExecuteDeleteSelection(st, log);
    return;
  }
  stepengine::BeginSession(st, std::make_unique<stepengine::DeleteSession>(), false);
  log.push_back("DELETE — click objects or drag a selection window (Enter when done) to erase them. "
                "ESC cancels.");
}

// CommitCircle (REQ-312) has internal linkage (defined inside an anonymous namespace further up this
// file, around the CIRCLE command); this is a true external forwarder for REQ-399's bare N/E candidates,

void StartMirrorCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-037: mirror paper geometry
    if (st.selectedPaperEntities.empty()) {
      log.push_back("MIRROR — select paper object(s) first.");
      return;
    }
    st.active = AppCommandState::Kind::None;  // paper-space edit ops are not a model command
    st.paperMirrorPhase = 1;
    log.push_back("MIRROR — specify first point of mirror line (click or type X,Y). ESC to cancel.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.pendingSurveyDupIsMirror = false;
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::MirrorPhase::NeedP1 : stepengine::MirrorPhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::MirrorSession>(initialPhase), true);
  if (haveSel)
    log.push_back("MIRROR — specify first point of mirror line (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}

/// REQ-305/483. Mirrors MOVE/COPY/ROTATE's Start* shape: a pre-existing selection skips straight
/// to choosing the array type, otherwise the command opens with click-or-window selection. Paper
/// arrays keep their selection and generated geometry in the active layout's native stores.
void StartArrayCommand(AppCommandState& st, std::vector<std::string>& log) {
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  const bool onPaper = ActivePaperGeometryTarget(st) != nullptr;
  if (onPaper) {
    const bool haveSel = !st.selectedPaperEntities.empty();
    const auto initialPhase =
        haveSel ? stepengine::ArrayPhase::WaitType : stepengine::ArrayPhase::PickSelection;
    stepengine::BeginSession(st, std::make_unique<stepengine::ArraySession>(true, initialPhase), true);
    if (haveSel) {
      SetArrayAnchorFromPaperSelection(st);
      log.push_back("ARRAY — select array type: [R]ectangular / [P]olar:");
    } else {
      log.push_back("ARRAY — click paper objects or drag a selection window, then press Enter to choose type.");
    }
    return;
  }
  DropArrayUnsupportedFromSelection(st, log);
  const bool haveSel = !st.selection.empty();
  const auto initialPhase =
      haveSel ? stepengine::ArrayPhase::WaitType : stepengine::ArrayPhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::ArraySession>(false, initialPhase), true);
  if (haveSel) {
    SetArrayAnchorFromSelection(st);
    log.push_back("ARRAY — select array type: [R]ectangular / [P]olar:");
  } else {
    log.push_back(kSelectObjectsPrompt);
  }
}


void ApplyCopySurveyDuplicateModalResult(AppCommandState& st, bool applySurveyDup, std::vector<std::string>& log) {
  if (!st.copySurveyDupModalOpen)
    return;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  const bool wasMirrorDup = st.pendingSurveyDupIsMirror;
  const bool wasRotateDup = st.pendingSurveyDupIsRotate;
  const bool mirrorEraseSource = st.mirrorEraseSourcePending;
  st.pendingSurveyDupIsMirror = false;
  st.pendingSurveyDupIsRotate = false;
  st.mirrorEraseSourcePending = false;
  if (applySurveyDup) {
    if (wasMirrorDup) {
      // The CAD-side erase already ran synchronously in FinishMirrorCommand; the survey-point half
      // waits for this modal because the duplicate's id policy is a user choice. Erasing the
      // ORIGINAL points only when the duplicate is actually applied (not on skip) — declining the
      // duplicate is treated as declining the whole mirror-for-survey-points step, not "erase with
      // nothing to replace it".
      DuplicateSelectedSurveyPointsReflected(st, st.pendingMirrorX0, st.pendingMirrorY0, st.pendingMirrorX1,
                                             st.pendingMirrorY1, st.copySurveyDuplicatePolicy, log);
      if (mirrorEraseSource)
        EraseSelectedSurveyPointsNoUndo(st);
    } else if (wasRotateDup)
      DuplicateSelectedSurveyPointsRotated(st, st.pendingRotateCopyBx, st.pendingRotateCopyBy, st.pendingRotateCopyRad,
                                           st.copySurveyDuplicatePolicy, log);
    else
      DuplicateSelectedSurveyPointsTranslated(st, st.pendingCopyDx, st.pendingCopyDy, st.pendingCopyDz,
                                              st.copySurveyDuplicatePolicy,
                                              log);
  } else if (wasMirrorDup)
    log.push_back("MIRROR COPY survey — skipped (CAD copy kept).");
  else if (wasRotateDup)
    log.push_back("ROTATE COPY survey — skipped (CAD copy kept).");
  else
    log.push_back("COPY survey — skipped (CAD copy kept).");
}
