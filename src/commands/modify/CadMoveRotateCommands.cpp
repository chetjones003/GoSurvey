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

float RotateDeltaFromReferenceAndNewSegment(float refX1, float refY1, float refX2, float refY2,
                                             float newX1, float newY1, float newX2, float newY2) {
  const float thetaRef = std::atan2(refY2 - refY1, refX2 - refX1);
  const float thetaNew = std::atan2(newY2 - newY1, newX2 - newX1);
  return thetaNew - thetaRef;
}


/// Remove TIN surfaces from the selection before a transform runs, and **say so** (REQ-201).
///
/// ADR-036 (b)/(c): a surface is display-only. Its geometry is derived from its definition, so a
/// translated surface would be silently un-translated by the next rebuild — a transform that appears
/// to work and then quietly undoes itself is worse than one that declines.
///
/// The refusal is spoken rather than performed by omission. Every Apply* funnel below simply skips
/// entity types it does not handle, so without this the user would drag a selection containing a
/// surface, watch everything else move, and be told nothing about why one object stayed put. That
/// silence is the exact failure mode ADR-035 (g) was written about.
///
/// Called from the funnels rather than from the Start* commands because a surface can be added to
/// the selection by a window drag AFTER the command starts.
void DropSurfacesFromSelectionForTransform(AppCommandState& st, const char* commandName,
                                           std::vector<std::string>& log) {
  const size_t before = st.selection.size();
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [](const SelectedEntity& e) {
                                      return e.type == SelectedEntity::Type::Surface;
                                    }),
                     st.selection.end());
  const size_t dropped = before - st.selection.size();
  if (dropped == 0)
    return;
  log.push_back(std::string(commandName) + " — " + std::to_string(dropped) + " surface(s) excluded: a surface's" +
                " shape comes from its definition, so moving it would be undone by the next rebuild." +
                " Edit its definition in the Surfaces panel instead.");
}

/// Drop B-rep solids and pipe runs from a selection a command can only apply to PART of an object,
/// and SAY SO (REQ-201). Since REQ-351 the one caller is STRETCH: every whole-object transform —
/// MOVE, COPY, ROTATE, SCALE, MIRROR, ARRAY — now takes both. STRETCH moves the vertices inside its
/// window and leaves the rest, and no kernel operation moves part of a solid; a pipe run could be
/// stretched by its path vertices, but that is a new behaviour no requirement asks for yet.
///
/// A separate function from \ref DropSurfacesFromSelectionForTransform because the reasons differ,
/// and a user who stretched a surface and a solid together deserves to be told which was which.
void DropSolidsFromSelectionForTransform(AppCommandState& st, const char* commandName,
                                         std::vector<std::string>& log) {
  size_t solids = 0, pipeRuns = 0;
  st.selection.erase(std::remove_if(st.selection.begin(), st.selection.end(),
                                    [&](const SelectedEntity& e) {
                                      if (e.type == SelectedEntity::Type::Solid) { ++solids; return true; }
                                      if (e.type == SelectedEntity::Type::PipeRun) { ++pipeRuns; return true; }
                                      return false;
                                    }),
                     st.selection.end());
  if (solids != 0)
    log.push_back(std::string(commandName) + " — " + std::to_string(solids) +
                  " solid(s) excluded: a solid can only be moved whole. Use MOVE, or edit its faces"
                  " and edges directly.");
  if (pipeRuns != 0)
    log.push_back(std::string(commandName) + " — " + std::to_string(pipeRuns) +
                  " pipe run(s) excluded: a pipe run can only be moved whole. Use MOVE.");
}


void ApplyRotationToSelection(AppCommandState& st, float bx, float by, float rad, std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "ROTATE", log);
  DropPositionMarkersFromSelection(st, "ROTATE", log);  // REQ-359: move/copy/erase only
  DropMultileadersFromSelection(st, "ROTATE", log);     // REQ-367
  // Solids are NOT dropped any more (REQ-332, amending REQ-322 item 6): `brep::Rotate` turns one
  // completely — every vertex, every surface frame's AXES as well as its origin, every arc-edge
  // frame — which is the work item 6 named as "a separate requirement" and REQ-328/REQ-332 supplied.
  // This is the plan / plan-rotated-UCS path, so the axis is world Z through the base point; the
  // axis point's own Z is irrelevant to a rotation about a vertical line.
  RotateSelectedSolids(st, {static_cast<double>(bx), static_cast<double>(by), 0.0}, {0.0, 0.0, 1.0},
                       rad, log);
  RotateSelectedPipeRuns(st, {static_cast<double>(bx), static_cast<double>(by), 0.0}, {0.0, 0.0, 1.0},
                         rad, /*duplicate=*/false);  // REQ-351
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
      if (i < st.drawing.lineCount()) {
        CadLineRecord ln = st.drawing.line(i);
        RotateAroundBase(bx, by, rad, &ln.x0, &ln.y0);
        RotateAroundBase(bx, by, rad, &ln.x1, &ln.y1);
        st.drawing.setLine(i, ln);
      }
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    const size_t ci = static_cast<size_t>(e.index);
    if (ci < st.drawing.circleCount()) {
      CadCircleRecord rotated = st.drawing.circle(ci);
      RotateAroundBase(bx, by, rad, &rotated.cx, &rotated.cy);
      st.drawing.setCircle(ci, rotated);
      // REQ-312: the plane turns with the circle. This function rotates the selection IN PLACE and
      // was missed when the normal was threaded through the copying paths -- with every normal +Z
      // there was nothing here to see, and a tilted circle rotated by ROTATESEL kept the plane it
      // used to be in while its centre moved to a place that plane no longer explains.
      const size_t n = static_cast<size_t>(e.index) * 3;
      if (n + 2 < st.drawing.userCircleNormals.size())
        RotateNormalAboutZ(rad, &st.drawing.userCircleNormals[n], &st.drawing.userCircleNormals[n + 1]);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.userArcs.size())
      continue;
    CadArc& a = st.drawing.userArcs[k];
    ray3d::Vec3 startPt = CurveWorldPointOnArc(a, static_cast<double>(a.startRad));
    float spx = static_cast<float>(startPt.x);
    float spy = static_cast<float>(startPt.y);
    RotateAroundBase(bx, by, rad, &spx, &spy);
    startPt.x = static_cast<double>(spx);
    startPt.y = static_cast<double>(spy);
    RotateAroundBase(bx, by, rad, &a.cx, &a.cy);
    a.startRad += rad;
    RotateNormalAboutZ(rad, &a.nx, &a.ny);  // REQ-312, same omission as the circles above
    CadReanchorArcStart(&a, startPt);
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
    RotateAroundBase(bx, by, rad, &el.cx, &el.cy);
    RotateAroundBase(bx, by, rad, &mx, &my);
    el.majVx = static_cast<float>(mx - el.cx);
    el.majVy = static_cast<float>(my - el.cy);
  }
  MapSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443
    double x = v.x, y = v.y;
    RotateAroundBase(bx, by, rad, &x, &y);
    return spline::Vec3{x, y, v.z};
  });
  MapSelectedCadPoints(st, [&](const spline::Vec3& v) {
    double x = v.x, y = v.y;
    RotateAroundBase(bx, by, rad, &x, &y);
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
    for (int vi = v0; vi < v1; ++vi) {
      RotateAroundBase(bx, by, rad, &st.drawing.vertexComponent(vi, CadVertexComponent::X),
                       &st.drawing.vertexComponent(vi, CadVertexComponent::Y));
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.drawing.cadAnnotations[k];
    RotateAroundBase(bx, by, rad, &a.insX, &a.insY);
    if (a.kind == CadAnnotation::Kind::Text) {
      a.rotationRad += rad;
    } else if (a.kind == CadAnnotation::Kind::Mtext) {
      RotateMtextAroundBase(bx, by, rad, &a);
    } else if (a.kind == CadAnnotation::Kind::DimLinear) {
      RotateCadDimLinearAroundBase(bx, by, rad, &a);
    } else if (a.kind == CadAnnotation::Kind::DimAligned) {
      RotateAroundBase(bx, by, rad, &a.dimExt1X, &a.dimExt1Y);
      RotateAroundBase(bx, by, rad, &a.dimExt2X, &a.dimExt2Y);
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
        RotateAroundBase(bx, by, rad, &xs[i], &ys[i]);
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
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Table)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadTables.size())
      continue;
    CadTableRotateAround(&st.drawing.cadTables[static_cast<size_t>(e.index)], bx, by, rad);
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::BlockRef)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
      continue;
    CadBlockRotateZ(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], bx, by, rad);
  }
  // PDF underlays: rotate insertion point around base; accumulate rotation angle.
  constexpr float kPdfRadToDeg = 180.f / 3.14159265f;
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::PdfUnderlay)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.pdfAttachments.size())
      continue;
    PdfAttachment& att = st.drawing.pdfAttachments[static_cast<size_t>(e.index)];
    RotateAroundBase(bx, by, rad, &att.insertX, &att.insertY);
    att.rotationDeg += rad * kPdfRadToDeg;
  }
  // Feature lines (REQ-087) — every vertex, PIs and elevation points alike, so the elevation points
  // stay on the line (ADR-035 (b)).
  TransformSelectedFeatureLinesInPlace(
      st, [&](auto* x, auto* y) { RotateAroundBase(bx, by, rad, x, y); });
  ApplyRotationToSelectedSurveyPoints(st, bx, by, rad);
  BumpCadGpuCache(st);
}

/// REQ-329 increment 2 (GitHub issue #402): rotate the selection IN PLACE about the LINE through
/// \p axisPoint with unit direction \p axisUnit by \p angleRad — the in-place counterpart of
/// \c RotateSelectionAboutAxis (which duplicates). Only ever reached with a genuinely TILTED axis:
/// \c FinishRotateCommand routes a world-Z-parallel axis (plan view, or any UCS merely rotated in
/// plan) to the unchanged \c ApplyRotationToSelection, so the pre-REQ-329 result is byte-identical
/// for every case that already worked.
///
/// Entity support matches REQ-328's own tilted-axis set: Line / Circle / Arc / Polyline /
/// FilledRegion rotate fully in 3D (a bare vertex has no orientation to preserve; Circle/Arc also
/// rotate their stored plane normal, and an Arc re-anchors its start). Ellipse / Annotation / Table
/// / BlockRef / PDF underlay / feature line / survey point are REFUSED by name — none stores a plane
/// normal (the survey point has no 3D-rotate path yet), so tipping one out of world/UCS XY has no
/// representable result today. Surfaces are dropped by the shared helper first; solids turn through
/// `brep::Rotate` (REQ-332) and pipe runs by their path (REQ-351), exactly as in
/// \c ApplyRotationToSelection.
static void RotateSelectionInPlaceAboutAxis(AppCommandState& st, const ray3d::Vec3& axisPoint,
                                            const ray3d::Vec3& axisUnit, float angleRad,
                                            std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "ROTATE", log);
  DropPositionMarkersFromSelection(st, "ROTATE", log);  // REQ-359: move/copy/erase only
  DropMultileadersFromSelection(st, "ROTATE", log);     // REQ-367
  // The tilted-UCS twin of the branch in `ApplyRotationToSelection`: same kernel call, but about the
  // UCS Z axis this function was already given rather than world Z (REQ-332, amending REQ-322 item 6).
  RotateSelectedSolids(st, axisPoint, axisUnit, angleRad, log);
  RotateSelectedPipeRuns(st, axisPoint, axisUnit, angleRad, /*duplicate=*/false);  // REQ-351
  const double rad = static_cast<double>(angleRad);
  const auto rotPt = [&](float x, float y, float z) -> ray3d::Vec3 {
    return ray3d::RotatePointAboutAxis({x, y, z}, axisPoint, axisUnit, rad);
  };
  const auto rotDir = [&](float x, float y, float z) -> ray3d::Vec3 {
    return ray3d::RotateVectorAboutAxis({x, y, z}, axisUnit, rad);
  };
  MapSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443
    return ray3d::RotatePointAboutAxis(v, axisPoint, axisUnit, rad);
  });
  MapSelectedCadPoints(st, [&](const spline::Vec3& v) {
    return ray3d::RotatePointAboutAxis(v, axisPoint, axisUnit, rad);
  });
  size_t refused = 0;

  for (const auto& e : st.selection) {
    switch (e.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.lineCount())
        break;
      const ray3d::Vec3 p0 = rotPt(static_cast<float>(st.drawing.line(k).x0), static_cast<float>(st.drawing.line(k).y0), static_cast<float>(st.drawing.line(k).z0));
      const ray3d::Vec3 p1 = rotPt(static_cast<float>(st.drawing.line(k).x1), static_cast<float>(st.drawing.line(k).y1), static_cast<float>(st.drawing.line(k).z1));
      st.drawing.lineComponent(k, CadLineComponent::X0) = static_cast<float>(p0.x);
      st.drawing.lineComponent(k, CadLineComponent::Y0) = static_cast<float>(p0.y);
      st.drawing.lineComponent(k, CadLineComponent::Z0) = static_cast<float>(p0.z);
      st.drawing.lineComponent(k, CadLineComponent::X1) = static_cast<float>(p1.x);
      st.drawing.lineComponent(k, CadLineComponent::Y1) = static_cast<float>(p1.y);
      st.drawing.lineComponent(k, CadLineComponent::Z1) = static_cast<float>(p1.z);
      break;
    }
    case SelectedEntity::Type::Circle: {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci >= st.drawing.circleCount())
        break;
      const CadCircleRecord cc = st.drawing.circle(ci);
      const ray3d::Vec3 c = rotPt(static_cast<float>(cc.cx), static_cast<float>(cc.cy), static_cast<float>(cc.cz));
      st.drawing.setCircle(ci, CadCircleRecord{c.x, c.y, c.z, cc.r});
      float nx = 0.f, ny = 0.f, nz = 1.f;
      CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(e.index), &nx, &ny, &nz);
      const ray3d::Vec3 n = rotDir(nx, ny, nz);
      const size_t nk = static_cast<size_t>(e.index) * 3;
      if (nk + 2 < st.drawing.userCircleNormals.size()) {
        st.drawing.userCircleNormals[nk] = static_cast<float>(n.x);
        st.drawing.userCircleNormals[nk + 1] = static_cast<float>(n.y);
        st.drawing.userCircleNormals[nk + 2] = static_cast<float>(n.z);
      }
      break;
    }
    case SelectedEntity::Type::Arc: {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.userArcs.size())
        break;
      CadArc& a = st.drawing.userArcs[k];
      const ray3d::Vec3 startWorld = CurveWorldPointOnArc(a, static_cast<double>(a.startRad));
      const ray3d::Vec3 startRot = rotPt(static_cast<float>(startWorld.x), static_cast<float>(startWorld.y),
                                         static_cast<float>(startWorld.z));
      const ray3d::Vec3 c = rotPt(static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.z));
      const ray3d::Vec3 n = rotDir(a.nx, a.ny, a.nz);
      a.cx = static_cast<float>(c.x);
      a.cy = static_cast<float>(c.y);
      a.z = static_cast<float>(c.z);
      a.nx = static_cast<float>(n.x);
      a.ny = static_cast<float>(n.y);
      a.nz = static_cast<float>(n.z);
      CadReanchorArcStart(&a, startRot);
      break;
    }
    case SelectedEntity::Type::Polyline: {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        break;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      for (int vi = v0; vi < v1; ++vi) {
        const size_t b = static_cast<size_t>(vi);
        if (b >= st.drawing.vertexCount())
          break;
        const ray3d::Vec3 p = rotPt(static_cast<float>(st.drawing.vertex(b).x), static_cast<float>(st.drawing.vertex(b).y), static_cast<float>(st.drawing.vertex(b).z));
        st.drawing.vertexComponent(b, CadVertexComponent::X) = static_cast<float>(p.x);
        st.drawing.vertexComponent(b, CadVertexComponent::Y) = static_cast<float>(p.y);
        st.drawing.vertexComponent(b, CadVertexComponent::Z) = static_cast<float>(p.z);
      }
      break;
    }
    case SelectedEntity::Type::FilledRegion: {
      const size_t fk = static_cast<size_t>(e.index);
      if (fk >= st.drawing.cadFilledRegions.size())
        break;
      CadFilledRegion& fr = st.drawing.cadFilledRegions[fk];
      for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3) {
        const ray3d::Vec3 p = rotPt(static_cast<float>(fr.vertsXyz[v]), static_cast<float>(fr.vertsXyz[v + 1]), static_cast<float>(fr.vertsXyz[v + 2]));
        fr.vertsXyz[v] = static_cast<float>(p.x);
        fr.vertsXyz[v + 1] = static_cast<float>(p.y);
        fr.vertsXyz[v + 2] = static_cast<float>(p.z);
      }
      break;
    }
    case SelectedEntity::Type::Ellipse:
    case SelectedEntity::Type::Annotation:
    case SelectedEntity::Type::Table:
    case SelectedEntity::Type::BlockRef:
    case SelectedEntity::Type::PdfUnderlay:
    case SelectedEntity::Type::FeatureLine:
      ++refused;
      break;
    default:
      break;
    }
  }
  // Survey points ride a separate selection list, not `st.selection`.
  refused += st.selectedSurveyPointIndices.size();

  if (refused > 0)
    log.push_back("ROTATE — " + std::to_string(refused) +
                  " entity(ies) excluded: text/table/block/PDF/feature-line/ellipse/survey-point"
                  " rotation about a tilted axis is not supported yet (REQ-328) — none stores a"
                  " plane normal to tip. Rotate about a UCS with an upright Z axis.");
  BumpCadGpuCache(st);
}

/// Move every selected solid by (dx, dy, dz), replacing each rather than editing it (REQ-322).
///
/// `brep::Translate` and not a per-field sweep here, and its own header says why: it moves every
/// vertex, every arc edge's centre, every face's surface origin, a NURBS patch's control points and
/// the recipe's placement frame, and *"open-coded at a call site, adding a field to `Surface` later
/// would silently miss it, and a solid that half-moved is not a shape at all."*
///
/// Replaced, never edited: `CadSolidPtr` is `shared_ptr<const brep::Solid>` precisely so an undo
/// snapshot is a refcount bump, which makes immutability the precondition for undo working at all.
static void TranslateSelectedSolids(AppCommandState& st, float dx, float dy, float dz) {
  if (dx == 0.f && dy == 0.f && dz == 0.f)
    return;
  // De-duplicated: a selection should not hold the same solid twice, and translating one twice
  // would move it twice as far — a defect that only shows up on a drawing where it happened.
  std::set<int> seen;
  for (const SelectedEntity& e : st.selection) {
    if (e.type != SelectedEntity::Type::Solid || e.index < 0 ||
        static_cast<size_t>(e.index) >= st.drawing.cadSolids.size())
      continue;
    if (!seen.insert(e.index).second)
      continue;
    const CadSolidPtr& sp = st.drawing.cadSolids[static_cast<size_t>(e.index)];
    if (!sp)
      continue;
    st.drawing.cadSolids[static_cast<size_t>(e.index)] = std::make_shared<const brep::Solid>(
        brep::Translate(*sp, ray3d::Vec3{static_cast<double>(dx), static_cast<double>(dy),
                                         static_cast<double>(dz)}));
  }
}

// The surrounding anonymous namespace is closed for this ONE function and reopened after it.
//
// It is public API now (declared in `CadCommands.hpp`): REQ-060 requires that a gizmo drag and the
// equivalent typed command produce the same coordinates, and the only way to make that a property
// rather than a hope is for both to call this — which means the gizmo's own translation unit, and
// the test that asserts the agreement, both have to be able to name it. Internal linkage said the
// opposite: "nothing outside this file may depend on this".

void ApplyTranslationToSelection(AppCommandState& st, float dx, float dy, float dz,
                                std::vector<std::string>& log) {
  DropSurfacesFromSelectionForTransform(st, "MOVE", log);
  // Solids are NOT dropped any more (REQ-322): `brep::Translate` moves one completely, and doing so
  // is the whole reason this function gained a Z. Pipe runs move with them (REQ-351).
  TranslateSelectedSolids(st, dx, dy, dz);
  if (dx != 0.f || dy != 0.f || dz != 0.f)
    TranslateSelectedPipeRuns(st, dx, dy, dz, /*duplicate=*/false);
  std::vector<bool> lineMark(std::max<size_t>(1, st.drawing.lineCount()), false);
  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg && e.index >= 0 &&
        static_cast<size_t>(e.index) < lineMark.size())
      lineMark[static_cast<size_t>(e.index)] = true;
  }
  for (size_t i = 0; i < lineMark.size(); ++i) {
    if (!lineMark[i])
      continue;
    size_t k = i;
    if (k < st.drawing.lineCount()) {
      st.drawing.lineComponent(k, CadLineComponent::X0) += dx;
      st.drawing.lineComponent(k, CadLineComponent::Y0) += dy;
      st.drawing.lineComponent(k, CadLineComponent::Z0) += dz;
      st.drawing.lineComponent(k, CadLineComponent::X1) += dx;
      st.drawing.lineComponent(k, CadLineComponent::Y1) += dy;
      st.drawing.lineComponent(k, CadLineComponent::Z1) += dz;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Circle)
      continue;
    const size_t ci = static_cast<size_t>(e.index);
    if (ci < st.drawing.circleCount()) {
      CadCircleRecord moved = st.drawing.circle(ci);
      moved.cx += dx;
      moved.cy += dy;
      moved.cz += dz;  // cx, cy, Z, r - the plane the circle sits in
      st.drawing.setCircle(ci, moved);
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Arc)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.userArcs.size())
      continue;
    st.drawing.userArcs[k].cx += dx;
    st.drawing.userArcs[k].cy += dy;
    st.drawing.userArcs[k].z += dz;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Ellipse)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.userEllipses.size())
      continue;
    st.drawing.userEllipses[k].cx += dx;
    st.drawing.userEllipses[k].cy += dy;
    st.drawing.userEllipses[k].z += dz;
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Polyline)
      continue;
    const int pi = e.index;
    if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
      continue;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    for (int vi = v0; vi < v1; ++vi) {
      st.drawing.vertexComponent(vi, CadVertexComponent::X) += dx;
      st.drawing.vertexComponent(vi, CadVertexComponent::Y) += dy;
      st.drawing.vertexComponent(vi, CadVertexComponent::Z) += dz;
    }
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Annotation)
      continue;
    const size_t k = static_cast<size_t>(e.index);
    if (k >= st.drawing.cadAnnotations.size())
      continue;
    CadAnnotation& a = st.drawing.cadAnnotations[k];
    a.insX += dx;
    a.insY += dy;
    a.insZ += dz;  // REQ-322: text and dimensions carry an elevation like everything else
    if (a.kind == CadAnnotation::Kind::Mtext) {
      a.boxMinX += dx;
      a.boxMinY += dy;
      a.boxMaxX += dx;
      a.boxMaxY += dy;
    } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      a.dimExt1X += dx;
      a.dimExt1Y += dy;
      a.dimExt2X += dx;
      a.dimExt2Y += dy;
    }
  }
  // PDF underlays: shift insertion point.
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::PdfUnderlay)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.pdfAttachments.size())
      continue;
    st.drawing.pdfAttachments[static_cast<size_t>(e.index)].insertX += dx;
    st.drawing.pdfAttachments[static_cast<size_t>(e.index)].insertY += dy;
  }
  // Filled regions (REQ-042): translate every loop vertex.
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::FilledRegion)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadFilledRegions.size())
      continue;
    hatchgeom::Translate(st.drawing.cadFilledRegions[static_cast<size_t>(e.index)], dx, dy, dz);
  }
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::Table)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadTables.size())
      continue;
    CadTableTranslate(&st.drawing.cadTables[static_cast<size_t>(e.index)], dx, dy);
  }
  for (const auto& e : st.selection) {  // REQ-359: marker and label move together
    if (e.type != SelectedEntity::Type::PositionMarker)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadPositionMarkers.size())
      continue;
    CadPositionMarkerTranslate(&st.drawing.cadPositionMarkers[static_cast<size_t>(e.index)], dx, dy, dz);
  }
  for (const auto& e : st.selection) {  // REQ-367: leader path and label move together
    if (e.type != SelectedEntity::Type::Multileader)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadMultileaders.size())
      continue;
    CadMultileaderTranslate(&st.drawing.cadMultileaders[static_cast<size_t>(e.index)], dx, dy, dz);
  }
  MapSelectedSplines(st, [&](const spline::Vec3& v) {  // REQ-443: a spline moves by moving its points
    return spline::Vec3{v.x + dx, v.y + dy, v.z + dz};
  });
  MapSelectedCadPoints(st, [&](const spline::Vec3& v) {
    return spline::Vec3{v.x + dx, v.y + dy, v.z + dz};
  });
  for (const auto& e : st.selection) {
    if (e.type != SelectedEntity::Type::BlockRef)
      continue;
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadBlockRefs.size())
      continue;
    // REQ-320 item 1 names "a block reference's insertion" explicitly; dz must ride along with
    // dx/dy exactly as it does for every other entity type in this function.
    CadBlockTranslate(&st.drawing.cadBlockRefs[static_cast<size_t>(e.index)], dx, dy, dz);
  }
  // Feature lines (REQ-087) — see ApplyRotationToSelection.
  TransformSelectedFeatureLinesInPlace(
      st, [&](auto* x, auto* y) {
    *x += dx;
    *y += dy;
  });
  // A feature line is 3D design linework (REQ-087), so its elevations move too. Its own pass rather
  // than a wider lambda: `TransformSelectedFeatureLinesInPlace` is shared with ROTATE, SCALE and
  // MIRROR, which are deliberately plan-only, and widening the shared helper would hand them a Z
  // they have no business with.
  if (dz != 0.f) {
    ForEachSelectedFeatureLine(st, [&](int /*fi*/, int v0, int v1) {
      for (int vi = v0; vi < v1; ++vi)
        st.drawing.featureLineVerts[static_cast<size_t>(vi) * 3 + 2] += dz;
    });
  }
  ApplyTranslationToSelectedSurveyPoints(st, dx, dy);
  BumpCadGpuCache(st);
}


bool ParseAngleDegreesInternal(const std::string& raw, float* degreesOut) {
  std::string s = StringUtil::trimCopy(raw);
  if (s.empty())
    return false;
  std::string low = StringUtil::toLowerAsciiCopy(s);
  bool neg = false;
  if (!low.empty() && low[0] == '-') {
    neg = true;
    low = StringUtil::trimCopy(low.substr(1));
  }
  if (!low.empty() && low[0] == '+')
    low = StringUtil::trimCopy(low.substr(1));
  float deg = 0.f;
  float min = 0.f;
  float sec = 0.f;
  size_t pd = low.find('d');
  if (pd != std::string::npos) {
    if (!(std::istringstream(low.substr(0, pd)) >> deg))
      return false;
    std::string rest = low.substr(pd + 1);
    size_t pm = rest.find('m');
    if (pm != std::string::npos) {
      if (!(std::istringstream(rest.substr(0, pm)) >> min))
        return false;
      std::string rest2 = rest.substr(pm + 1);
      size_t ps = rest2.find('s');
      if (ps != std::string::npos) {
        if (!(std::istringstream(rest2.substr(0, ps)) >> sec))
          return false;
      } else {
        if (!(std::istringstream(rest2) >> sec))
          sec = 0.f;
      }
    } else {
      if (!(std::istringstream(rest) >> min))
        min = 0.f;
    }
    *degreesOut = (neg ? -1.f : 1.f) * (deg + min / 60.f + sec / 3600.f);
    return true;
  }
  if (!(std::istringstream(low) >> deg))
    return false;
  *degreesOut = neg ? -deg : deg;
  return true;
}

/// Peel an optional third component off a typed point, returning the `X,Y` part and the elevation.
///
/// The same shape `ParseSolidBasePoint` uses, and for the same reason: the shared 2D parser reads
/// two numbers and ignores the rest, so `1,2,3` would otherwise land silently at (1,2) with the
/// elevation dropped on the floor. Strict about the field count for the same reason.
///
/// An omitted Z is zero and `*outHasZ` is false, so every existing drawing, transcript and habit
/// behaves exactly as it did (REQ-322 item 4).


void ApplyRotationAboutUcsZ(AppCommandState& st, float bx, float by, float bz, float rad,
                            std::vector<std::string>& log) {
  // REQ-329 increment 2: ROTATE turns the selection about an axis through the base point PARALLEL
  // TO THE ACTIVE UCS Z. When that axis is world-Z-parallel — plan view, or any UCS merely
  // rotated/translated in plan — this is identical to the old world-Z rotation through (bx, by), so
  // the pre-REQ-329 path runs unchanged and the result is byte-identical. Only a genuinely tilted
  // UCS takes the axis-aware branch.
  if (!CadWorkPlaneIsWorldXy(st)) {
    const ucs::Ucs u = CadActiveUcsStorage(st);
    const ray3d::Vec3 axisUnit = ray3d::Normalize(ray3d::Vec3{u.zAxis.x, u.zAxis.y, u.zAxis.z});
    RotateSelectionInPlaceAboutAxis(
        st, {static_cast<double>(bx), static_cast<double>(by), static_cast<double>(bz)}, axisUnit,
        rad, log);
    return;
  }
  ApplyRotationToSelection(st, bx, by, rad, log);
}

void ApplyRotationAboutAxis(AppCommandState& st, const ray3d::Vec3& axisPoint,
                            const ray3d::Vec3& axisUnit, float rad, std::vector<std::string>& log) {
  // The in-place arbitrary-axis turn typed ROTATE already uses under a tilted UCS — so every type it
  // turns, it turns here, and every type it refuses by name (REQ-201) is refused here too.
  RotateSelectionInPlaceAboutAxis(st, axisPoint, ray3d::Normalize(axisUnit), rad, log);
}

void StartMoveCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-035: move viewports
    StartPaperMoveCopyViewports(st, /*copy=*/false, log);
    return;
  }
  ClearPendingViewportZoom(st);
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::ModifyPhase::NeedBase : stepengine::ModifyPhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::MoveCopySession>(false, initialPhase), true);
  if (haveSel)
    log.push_back("MOVE — specify base point (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}

void StartCopyCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-035: copy viewports
    StartPaperMoveCopyViewports(st, /*copy=*/true, log);
    return;
  }
  ClearPendingViewportZoom(st);
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::ModifyPhase::NeedBase : stepengine::ModifyPhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::MoveCopySession>(true, initialPhase), true);
  if (haveSel)
    log.push_back("COPY — specify base point (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}

void StartRotateCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {  // REQ-037: rotate paper geometry
    if (st.selectedPaperEntities.empty()) {
      log.push_back("ROTATE — select paper object(s) first.");
      return;
    }
    st.active = AppCommandState::Kind::None;  // paper-space edit ops are not a model command
    st.paperRotatePhase = 1;
    log.push_back("ROTATE — specify base point (click or type X,Y). ESC to cancel.");
    return;
  }
  ClearPendingViewportZoom(st);
  st.pendingSurveyDupIsRotate = false;
  const bool haveSel = !st.selection.empty() || !st.selectedSurveyPointIndices.empty();
  const auto initialPhase =
      haveSel ? stepengine::RotatePhase::NeedBase : stepengine::RotatePhase::PickSelection;
  stepengine::BeginSession(st, std::make_unique<stepengine::RotateSession>(initialPhase), true);
  if (haveSel)
    log.push_back("ROTATE — specify base point (click or type X,Y). ESC to cancel.");
  else
    log.push_back(kSelectObjectsPrompt);
}
