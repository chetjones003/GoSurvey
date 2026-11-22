#include "CadCommandsInternal.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include "HatchGeom.hpp"
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Geometry/spline.hpp>
#include <GoSurvey/Terrain/tinbuild.hpp>
#include "GoSurvey/Document/MtextRichFormat.hpp"
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

bool TryBeginEntityGripAtLocal(AppCommandState& cmd, float lx, float ly, float tolWorld) {
  // REQ-036: grab the nearest grip of a selected entity within tolWorld of (lx,ly) in LOCAL coords. Mirrors
  // the model-space grab but with a world-distance hit test (the model path uses a screen-space test, which
  // does not work through the floating viewport transform). On a hit, arms the grip drag + stores originals.
  if (cmd.selection.empty())
    return false;
  float bestD2 = tolWorld * tolWorld;
  int bestWhich = -1;
  SelectedEntity bestSel{};
  float bestGripX = 0.f, bestGripY = 0.f;
  auto tryGrip = [&](const SelectedEntity& sel, float gx, float gy, int which) {
    const float dx = lx - gx, dy = ly - gy;
    const float d2 = dx * dx + dy * dy;
    if (d2 < bestD2) {
      bestD2 = d2;
      bestWhich = which;
      bestSel = sel;
      bestGripX = gx;  // ORTHO / typed-distance anchor for the drag (REQ-047)
      bestGripY = gy;
    }
  };
  for (const SelectedEntity& sel : cmd.selection) {
    switch (sel.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t li = static_cast<size_t>(sel.index);
      if (li < cmd.drawing.lineCount()) {
        const CadLineRecord ln = cmd.drawing.line(li);
        tryGrip(sel, static_cast<float>(ln.x0), static_cast<float>(ln.y0), 0);
        tryGrip(sel, static_cast<float>(ln.x1), static_cast<float>(ln.y1), 1);
      }
      break;
    }
    case SelectedEntity::Type::Circle: {
      const size_t ci = static_cast<size_t>(sel.index);
      if (ci < cmd.drawing.circleCount()) {
        const CadCircleRecord c = cmd.drawing.circle(ci);
        const float cx = static_cast<float>(c.cx), cy = static_cast<float>(c.cy), r = static_cast<float>(c.r);
        tryGrip(sel, cx, cy, 0);
        tryGrip(sel, cx + r, cy, 1);
      }
      break;
    }
    case SelectedEntity::Type::Polyline: {
      const int np = cmd.drawing.userPolylineOffsets.size() > 0 ? static_cast<int>(cmd.drawing.userPolylineOffsets.size() - 1) : 0;
      if (sel.index >= 0 && sel.index < np) {
        const int startV = cmd.drawing.userPolylineOffsets[static_cast<size_t>(sel.index)];
        const int endV = cmd.drawing.userPolylineOffsets[(static_cast<size_t>(sel.index)+ 1)];
        for (int vi = 0; vi < endV - startV; ++vi) {
          const size_t xIdx = (static_cast<size_t>(startV)+ vi);
          if (xIdx >= cmd.drawing.vertexCount())
            break;
          tryGrip(sel, static_cast<float>(cmd.drawing.vertex(xIdx).x), static_cast<float>(cmd.drawing.vertex(xIdx).y), vi);
        }
        CadForEachPolylineArcMidGrip(cmd, sel.index, [&](int seg, float mx, float my, float) {
          tryGrip(sel, mx, my, kPolyBulgeGripBase + seg);  // REQ-316 / ADR-047
        });
      }
      break;
    }
    case SelectedEntity::Type::Arc: {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.drawing.userArcs.size()) {
        const CadArc& a = cmd.drawing.userArcs[static_cast<size_t>(sel.index)];
        const float endRad = a.startRad + a.sweepRad;
        tryGrip(sel, static_cast<float>(a.cx), static_cast<float>(a.cy), 0);
        tryGrip(sel, static_cast<float>(a.cx + a.r * std::cos(a.startRad)), static_cast<float>(a.cy + a.r * std::sin(a.startRad)), 1);
        tryGrip(sel, static_cast<float>(a.cx + a.r * std::cos(endRad)), static_cast<float>(a.cy + a.r * std::sin(endRad)), 2);
      }
      break;
    }
    case SelectedEntity::Type::Ellipse: {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.drawing.userEllipses.size()) {
        const CadEllipse& el = cmd.drawing.userEllipses[static_cast<size_t>(sel.index)];
        // Centre, major end, minor end — through the shared helper, so this asks the same question
        // src/ui/CadUi.cpp asks when it DRAWS them (GitHub #531). They disagreed before: a tilted
        // ellipse drew no grips and still offered them here, which is an invisible handle sitting
        // where a flat ellipse's would be, deforming the section when grabbed.
        if (EllipseHasGrips(el)) {
          ray3d::Vec3 g[3];
          EllipseGripPoints(el, g);
          for (int gi = 0; gi < 3; ++gi)
            tryGrip(sel, static_cast<float>(g[gi].x), static_cast<float>(g[gi].y), gi);
        }
      }
      break;
    }
    case SelectedEntity::Type::BlockRef: {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.drawing.cadBlockRefs.size()) {
        const CadBlockRef& r = cmd.drawing.cadBlockRefs[static_cast<size_t>(sel.index)];
        const int di = CadBlockFindDef(cmd.drawing.blockDefs, r.defName);
        if (di >= 0) {
          const CadBlockDefinition& def = cmd.drawing.blockDefs[static_cast<size_t>(di)];
          const int nG = CadBlockDynGripCount(def);
          for (int g = 0; g < nG; ++g) {
            if (!CadBlockDynGripShownOnInsert(g))
              continue;
            float gx = 0.f, gy = 0.f, gz = 0.f;
            if (CadBlockDynGripWorld(def, r, g, &gx, &gy, &gz))
              tryGrip(sel, gx, gy, g);
          }
        }
      }
      break;
    }
    case SelectedEntity::Type::Spline: {  // REQ-443: fit points, or control vertices for a CV spline
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.drawing.cadSplines.size()) {
        const CadSpline& sp = cmd.drawing.cadSplines[static_cast<size_t>(sel.index)];
        for (size_t i = 0; i < spline::EditablePointCount(sp); ++i) {
          const spline::Vec3 v = spline::EditablePoint(sp, i);
          tryGrip(sel, static_cast<float>(v.x), static_cast<float>(v.y), static_cast<int>(i));
        }
      }
      break;
    }
    case SelectedEntity::Type::Point: {
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.drawing.cadPoints.size()) {
        const CadPoint& p = cmd.drawing.cadPoints[static_cast<size_t>(sel.index)];
        tryGrip(sel, static_cast<float>(p.x), static_cast<float>(p.y), 0);
      }
      break;
    }
    case SelectedEntity::Type::Alignment: {  // REQ-487.1: start, tangent ends, curve radius grips
      if (sel.index >= 0 && static_cast<size_t>(sel.index) < cmd.drawing.alignments.size()) {
        for (const alignment::Grip& g : alignment::GripPoints(cmd.drawing.alignments[static_cast<size_t>(sel.index)]))
          tryGrip(sel, static_cast<float>(g.point.x), static_cast<float>(g.point.y), g.which);
      }
      break;
    }
    default:
      break;
    }
  }
  if (bestWhich < 0)
    return false;
  PushUndoSnapshot(cmd, "Grip edit");
  if (bestSel.type == SelectedEntity::Type::BlockRef) {
    if (!CadBlockArmDynGrip(cmd, bestSel.index, bestWhich)) {
      BumpCadGpuCache(cmd);
      return true;
    }
    cmd.entityGripMoveActive = true;
    cmd.entityGripType = bestSel.type;
    cmd.entityGripEntityIndex = bestSel.index;
    cmd.entityGripWhich = bestWhich;
    cmd.entityGripAnchorX = bestGripX;
    cmd.entityGripAnchorY = bestGripY;
    cmd.entityGripTypedDistanceValid = false;
    return true;
  }
  cmd.entityGripMoveActive = true;
  cmd.entityGripType = bestSel.type;
  cmd.entityGripEntityIndex = bestSel.index;
  cmd.entityGripWhich = bestWhich;
  cmd.entityGripAnchorX = bestGripX;
  cmd.entityGripAnchorY = bestGripY;
  cmd.entityGripTypedDistanceValid = false;
  switch (bestSel.type) {  // store originals for RMB / Esc cancel
  case SelectedEntity::Type::LineSeg: {
    const CadLineRecord ln = cmd.drawing.line(static_cast<size_t>(bestSel.index));
    cmd.entityGripOrigX0 = static_cast<float>(ln.x0);
    cmd.entityGripOrigY0 = static_cast<float>(ln.y0);
    cmd.entityGripOrigX1 = static_cast<float>(ln.x1);
    cmd.entityGripOrigY1 = static_cast<float>(ln.y1);
    break;
  }
  case SelectedEntity::Type::Circle: {
    const CadCircleRecord c = cmd.drawing.circle(static_cast<size_t>(bestSel.index));
    cmd.entityGripOrigCx = static_cast<float>(c.cx);
    cmd.entityGripOrigCy = static_cast<float>(c.cy);
    cmd.entityGripOrigR = static_cast<float>(c.r);
    break;
  }
  case SelectedEntity::Type::Polyline: {
    const int startV = cmd.drawing.userPolylineOffsets[static_cast<size_t>(bestSel.index)];
    cmd.entityGripOrigPolyBulgeVi = -1;
    if (bestWhich >= kPolyBulgeGripBase) {  // REQ-316 / ADR-047: arc-segment bulge grip
      const int va = startV + (bestWhich - kPolyBulgeGripBase);
      cmd.entityGripOrigPolyBulgeVi = va;
      cmd.entityGripOrigPolyBulge = static_cast<size_t>(va) < cmd.drawing.userPolylineVertsBulge.size()
                                        ? cmd.drawing.userPolylineVertsBulge[static_cast<size_t>(va)] : 0.f;
      cmd.entityGripOrigPolylineXIdx = -1;
      break;
    }
    const size_t xIdx = (static_cast<size_t>(startV)+ bestWhich);
    cmd.entityGripOrigPolylineXIdx = static_cast<int>(xIdx);
    cmd.entityGripOrigPolyVertX = static_cast<float>(cmd.drawing.vertex(xIdx).x);
    cmd.entityGripOrigPolyVertY = static_cast<float>(cmd.drawing.vertex(xIdx).y);
    break;
  }
  case SelectedEntity::Type::Arc: {
    const CadArc& a = cmd.drawing.userArcs[static_cast<size_t>(bestSel.index)];
    cmd.entityGripOrigCx = static_cast<float>(a.cx);
    cmd.entityGripOrigCy = static_cast<float>(a.cy);
    cmd.entityGripOrigR = static_cast<float>(a.r);
    cmd.entityGripOrigStartRad = a.startRad;
    cmd.entityGripOrigSweepRad = a.sweepRad;
    break;
  }
  case SelectedEntity::Type::Ellipse: {
    const CadEllipse& el = cmd.drawing.userEllipses[static_cast<size_t>(bestSel.index)];
    cmd.entityGripOrigEllCx = static_cast<float>(el.cx);
    cmd.entityGripOrigEllCy = static_cast<float>(el.cy);
    cmd.entityGripOrigEllMajVx = el.majVx;
    cmd.entityGripOrigEllMajVy = el.majVy;
    cmd.entityGripOrigEllRatio = el.ratio;
    break;
  }
  case SelectedEntity::Type::Spline:  // REQ-443
    cmd.entityGripOrigSpline = cmd.drawing.cadSplines[static_cast<size_t>(bestSel.index)];
    break;
  case SelectedEntity::Type::Alignment:  // REQ-487.1
    cmd.entityGripOrigAlignment = cmd.drawing.alignments[static_cast<size_t>(bestSel.index)];
    break;
  default:
    break;
  }
  return true;
}


namespace {
bool SelectedEntityEqual(const SelectedEntity& a, const SelectedEntity& b) {
  return a.type == b.type && a.index == b.index;
}

}  // namespace

namespace {
///        (world XY tested against a world rect). When the camera is orbited the caller supplies a
///        projection so vertices are tested in SCREEN space, where the drag rectangle actually is
///        (REQ-058). Per-vertex projection keeps the polyline test exact rather than conservative.
/// Takes the three arrays explicitly so FEATURE LINES box-select through the identical test
/// (REQ-087): same CSR shape, so a separate copy could only drift.
template <class VT>
bool ChainHitsRect(const std::vector<int>& OFF, const std::vector<VT>& V,
                   const std::vector<uint8_t>& CLOSED, int pi, float mnX, float mxX, float mnY,
                   float mxY, bool windowMode,
                   const std::function<void(float, float, float, float*, float*)>* toTest,
                   const std::vector<float>* BULGE = nullptr) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= OFF.size())
    return false;
  const int v0 = OFF[static_cast<size_t>(pi)];
  const int v1 = OFF[(static_cast<size_t>(pi)+ 1)];
  if (v0 >= v1)
    return false;
  const bool closed =
      static_cast<size_t>(pi) < CLOSED.size() && CLOSED[static_cast<size_t>(pi)];
  const int nVert = v1 - v0;
  // Vertex in test space: identity in plan view, projected when the caller supplies a mapping.
  auto vert = [&](int vi, float* ox, float* oy) {
    const size_t k = static_cast<size_t>(vi) * 3;
    if (toTest && *toTest)
      (*toTest)(V[k], V[k + 1], V[k + 2], ox, oy);
    else {
      *ox = V[k];
      *oy = V[k + 1];
    }
  };
  if (windowMode) {
    for (int vi = v0; vi < v1; ++vi) {
      float x, y;
      vert(vi, &x, &y);
      if (!PointInsideClosedRect(x, y, mnX, mxX, mnY, mxY))
        return false;
    }
    return true;
  }
  // REQ-316 / ADR-047: a curved segment is tested against the ARC it draws, tessellated, so a
  // crossing box over the bulge (and clear of every chord) still selects the polyline.
  auto segHitsRect = [&](int va, int vb) -> bool {
    float x0, y0, x1, y1;
    vert(va, &x0, &y0);
    vert(vb, &x1, &y1);
    const float bulge = (BULGE && static_cast<size_t>(va) < BULGE->size()) ? (*BULGE)[static_cast<size_t>(va)] : 0.f;
    const BulgeArcSpan arc = (bulge != 0.f) ? BulgeArc(x0, y0, x1, y1, static_cast<double>(bulge)) : BulgeArcSpan{};
    if (!arc.valid)
      return SegIntersectsAABB(x0, y0, x1, y1, mnX, mxX, mnY, mxY);
    constexpr double kPi = 3.14159265358979323846;
    const int ns = std::clamp(static_cast<int>(std::ceil(std::fabs(arc.sweep) / (kPi / 24.0))), 2, 96);
    double px = x0, py = y0;
    for (int s = 1; s <= ns; ++s) {
      const double u = arc.startAngle + arc.sweep * (static_cast<double>(s) / ns);
      const double qx = arc.cx + arc.radius * std::cos(u);
      const double qy = arc.cy + arc.radius * std::sin(u);
      if (SegIntersectsAABB(static_cast<float>(px), static_cast<float>(py), static_cast<float>(qx),
                            static_cast<float>(qy), mnX, mxX, mnY, mxY))
        return true;
      px = qx;
      py = qy;
    }
    return false;
  };
  for (int vi = v0; vi + 1 < v1; ++vi)
    if (segHitsRect(vi, vi + 1))
      return true;
  if (closed && nVert >= 2) {
    if (segHitsRect(v1 - 1, v0))
      return true;
  }
  return false;
}

}  // namespace

void ComputeSelectionFromRect(AppCommandState& st, float xa, float ya, float za, float xb, float yb,
                              float zb, bool subtract, bool windowMode, bool includeSurveyPoints,
                              const Camera* cam, float vpW, float vpH) {
  // Under an orbited camera a screen rectangle is NOT a world-axis-aligned rectangle — it projects
  // to a rotated quad — so testing world bounds against a world AABB selects the wrong objects
  // (REQ-058). When \p cam is supplied the whole test moves to SCREEN space: the drag corners and
  // every entity's bounds are projected, and the comparisons below are unchanged.
  //
  // Projecting an entity's world bounding box gives a conservative screen box (the bound of the
  // projection, not the projection of the bound), so crossing mode can occasionally include an
  // object whose box grazes the rect. Recorded as a limitation rather than hidden — but it is now
  // a limitation of the BOX-SHAPED entities only (annotations, tables, block references, PDF
  // underlays), whose footprint really is a rectangle. Everything the user draws as an outline is
  // tested against its own geometry: lines endpoint-wise, polylines per vertex and per bulge arc,
  // and circles / arcs / ellipses along the curve itself (2026-09-03 — a curve inside a box is a
  // bad approximation in both directions, and it was selecting circles a fence never touched).
  const bool proj = cam != nullptr && vpW > 0.f && vpH > 0.f;
  auto SP = [&](float wx, float wy, float wz, float* sx, float* sy) {
    if (!proj) {
      *sx = wx;
      *sy = wy;
      return;
    }
    cam->WorldToScreen(static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz), vpW, vpH, sx, sy);
  };
  // A world box -> the screen box that bounds its projected corners.
  auto SPBox = [&](float bx0, float by0, float bx1, float by1, float* o0x, float* o0y, float* o1x, float* o1y) {
    if (!proj) {
      *o0x = bx0; *o0y = by0; *o1x = bx1; *o1y = by1;
      return;
    }
    // Inputs are copied BEFORE anything is written: callers pass the same variables as in and out
    // to transform a box in place, so writing first would clobber the corners still to be read.
    const float cxs[4] = {bx0, bx1, bx0, bx1};
    const float cys[4] = {by0, by0, by1, by1};
    float lo0x = 1e30f, lo0y = 1e30f, lo1x = -1e30f, lo1y = -1e30f;
    for (int i = 0; i < 4; ++i) {
      float ax, ay;
      SP(cxs[i], cys[i], 0.f, &ax, &ay);
      lo0x = std::min(lo0x, ax); lo1x = std::max(lo1x, ax);
      lo0y = std::min(lo0y, ay); lo1y = std::max(lo1y, ay);
    }
    *o0x = lo0x; *o0y = lo0y; *o1x = lo1x; *o1y = lo1y;
  };
  // Same mapping as \c SP, in the type PolylineHitsRect accepts so polylines test per-vertex.
  const std::function<void(float, float, float, float*, float*)> projFn =
      [&](float wx, float wy, float wz, float* sx, float* sy) { SP(wx, wy, wz, sx, sy); };
  if (proj) {  // the drag corners arrive in world coords; move them to screen too
    // At their OWN elevations, not the datum. A fence drawn on a tilted work plane (a UCS) or a
    // raised one (ELEV) does not sit at Z = 0, and projecting it as if it did puts both the drawn
    // rectangle and the region that selects at pixels the mouse never visited.
    SP(xa, ya, za, &xa, &ya);
    SP(xb, yb, zb, &xb, &yb);
  }
  float mnX = std::min(xa, xb);
  float mxX = std::max(xa, xb);
  float mnY = std::min(ya, yb);
  float mxY = std::max(ya, yb);
  const float expand = 1e-4f;
  if (mxX - mnX < expand) {
    mnX -= expand;
    mxX += expand;
  }
  if (mxY - mnY < expand) {
    mnY -= expand;
    mxY += expand;
  }

  // A CURVE is tested against the curve it draws, tessellated and projected per vertex — never
  // against a box drawn round it (user report with screenshots, 2026-09-03). A box is wrong in two
  // directions at once: a circle's enclosing world square reaches sqrt(2)*r at its corners, ~41%
  // past the rim, AND the box is solid where the curve is hollow, so a fence in the empty middle
  // hit as well. Polylines with arc segments already test the arc they draw (REQ-316 / ADR-047,
  // ChainHitsRect) — this is the same rule for the entity types that draw a curve on their own.
  //
  // \p pts are world points along the curve in order; a closed curve repeats its first point last,
  // which is what SampleCurveWorld over a full sweep produces. Window mode wants every vertex
  // inside the rect, crossing mode any SEGMENT touching it — the same two questions the line loop
  // below asks, so the rule cannot drift between an entity that curves and one that does not.
  auto CurveHitsRect = [&](const std::vector<ray3d::Vec3>& pts) -> bool {
    if (pts.size() < 2)
      return false;
    float px = 0.f;
    float py = 0.f;
    SP(static_cast<float>(pts[0].x), static_cast<float>(pts[0].y), static_cast<float>(pts[0].z), &px, &py);
    if (windowMode && !PointInsideClosedRect(px, py, mnX, mxX, mnY, mxY))
      return false;
    for (size_t i = 1; i < pts.size(); ++i) {
      float qx = 0.f;
      float qy = 0.f;
      SP(static_cast<float>(pts[i].x), static_cast<float>(pts[i].y), static_cast<float>(pts[i].z), &qx, &qy);
      if (windowMode) {
        if (!PointInsideClosedRect(qx, qy, mnX, mxX, mnY, mxY))
          return false;
      } else if (SegIntersectsAABB(px, py, qx, qy, mnX, mxX, mnY, mxY)) {
        return true;
      }
      px = qx;
      py = qy;
    }
    return windowMode;
  };
  // Segments across a sweep, matching ChainHitsRect's choice for a polyline bulge (pi/24 steps, so
  // a full circle is 48). The chord then departs the true curve by r*(1-cos(3.75 deg)) = 0.0021*r,
  // under a pixel for any circle that fits on screen — small beside the pick aperture, and small in
  // the direction that matters: it is the tessellation the renderer draws that the user aims at.
  auto CurveSegmentCount = [](double sweepRad) {
    constexpr double kPi = 3.14159265358979323846;
    return std::clamp(static_cast<int>(std::ceil(std::fabs(sweepRad) / (kPi / 24.0))), 8, 96);
  };

  std::vector<SelectedEntity> hits;
  std::vector<int> surveyHits;
  std::vector<ray3d::Vec3> curvePts;  // reused by the circle / arc / ellipse walks below
  if (st.drawing.lineStoreWellFormed()) {
    for (size_t li = 0; li < st.drawing.lineCount(); ++li) {
      const CadLineRecord ln = st.drawing.line(li);
      // Endpoint-wise projection keeps the line test EXACT in screen space (each endpoint carries
      // its own Z, so a sloped line is tested where it actually appears).
      float x0, y0, x1, y1;
      SP(static_cast<float>(ln.x0), static_cast<float>(ln.y0), static_cast<float>(ln.z0), &x0, &y0);
      SP(static_cast<float>(ln.x1), static_cast<float>(ln.y1), static_cast<float>(ln.z1), &x1, &y1);
      bool hit = false;
      if (windowMode)
        hit = PointInsideClosedRect(x0, y0, mnX, mxX, mnY, mxY) &&
              PointInsideClosedRect(x1, y1, mnX, mxX, mnY, mxY);
      else
        hit = SegIntersectsAABB(x0, y0, x1, y1, mnX, mxX, mnY, mxY);
      if (hit) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::LineSeg;
        e.index = static_cast<int>(li);
        hits.push_back(e);
      }
    }
  }
  if (st.drawing.circleStoreWellFormed()) {
    for (size_t ci = 0; ci < st.drawing.circleCount(); ++ci) {
      const CadCircleRecord cc = st.drawing.circle(ci);
      const float cx = static_cast<float>(cc.cx);
      const float cy = static_cast<float>(cc.cy);
      const float r = static_cast<float>(cc.r);
      bool hit = false;
      if (proj) {
        // Orbited: a circle projects to an ELLIPSE, so there is no analytic rim test to reach for —
        // walk the rim in the circle's own plane (REQ-312) and project each vertex. This replaced
        // the bounding box of the enclosing world square, which is what selected a circle from a
        // fence that never touched it.
        float cnx = 0.f;
        float cny = 0.f;
        float cnz = 1.f;
        CircleNormalAt(st.drawing.userCircleNormals, ci, &cnx, &cny, &cnz);
        constexpr double kTwoPi = 6.28318530717958647692;
        SampleCurveWorld(curvePts,
                         CurvePlane(static_cast<double>(cx), static_cast<double>(cy),
                                    static_cast<double>(cc.cz), static_cast<double>(cnx),
                                    static_cast<double>(cny), static_cast<double>(cnz)),
                         static_cast<double>(r), 0.0, kTwoPi, CurveSegmentCount(kTwoPi));
        hit = CurveHitsRect(curvePts);
      } else if (windowMode)
        // Plan view keeps its EXACT analytic tests. A tessellation would be a step backwards where
        // a closed form is available, and REQ-058 holds the plan path unchanged on purpose.
        hit = CircleFullyInsideRect(cx, cy, r, mnX, mxX, mnY, mxY);
      else
        hit = CircleIntersectsAABB(cx, cy, r, mnX, mxX, mnY, mxY);
      if (hit) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::Circle;
        e.index = static_cast<int>(ci);
        hits.push_back(e);
      }
    }
  }
  const size_t nAnn = st.drawing.cadAnnotations.size();
  for (size_t ai = 0; ai < nAnn; ++ai) {
    float amnX = 0.f;
    float amnY = 0.f;
    float amxX = 0.f;
    float amxY = 0.f;
    CadAnnotationRoughBounds(st.drawing.cadAnnotations[ai], st.drawing.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
    SPBox(amnX, amnY, amxX, amxY, &amnX, &amnY, &amxX, &amxY);  // screen space when orbited
    bool hit = false;
    if (windowMode)
      hit = amnX >= mnX && amxX <= mxX && amnY >= mnY && amxY <= mxY;
    else
      hit = !(amxX < mnX || amnX > mxX || amxY < mnY || amnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Annotation;
      e.index = static_cast<int>(ai);
      hits.push_back(e);
    }
  }
  for (size_t ti = 0; ti < st.drawing.cadTables.size(); ++ti) {
    float tmnX = 0.f, tmnY = 0.f, tmxX = 0.f, tmxY = 0.f;
    CadTableWorldAabb(st.drawing.cadTables[ti], &tmnX, &tmnY, &tmxX, &tmxY);
    SPBox(tmnX, tmnY, tmxX, tmxY, &tmnX, &tmnY, &tmxX, &tmxY);
    bool hit = false;
    if (windowMode)
      hit = tmnX >= mnX && tmxX <= mxX && tmnY >= mnY && tmxY <= mxY;
    else
      hit = !(tmxX < mnX || tmnX > mxX || tmxY < mnY || tmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Table;
      e.index = static_cast<int>(ti);
      hits.push_back(e);
    }
  }
  for (size_t mi = 0; mi < st.drawing.cadPositionMarkers.size(); ++mi) {  // REQ-359: marker + label
    float mmnX = 0.f, mmnY = 0.f, mmxX = 0.f, mmxY = 0.f;
    CadPositionMarkerLocalBox(st, mi, &mmnX, &mmnY, &mmxX, &mmxY);
    SPBox(mmnX, mmnY, mmxX, mmxY, &mmnX, &mmnY, &mmxX, &mmxY);
    bool hit = false;
    if (windowMode)
      hit = mmnX >= mnX && mmxX <= mxX && mmnY >= mnY && mmxY <= mxY;
    else
      hit = !(mmxX < mnX || mmnX > mxX || mmxY < mnY || mmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::PositionMarker;
      e.index = static_cast<int>(mi);
      hits.push_back(e);
    }
  }
  for (size_t li = 0; li < st.drawing.cadMultileaders.size(); ++li) {  // REQ-367: path + label
    float lmnX = 0.f, lmnY = 0.f, lmxX = 0.f, lmxY = 0.f;
    CadMultileaderLocalBox(st.drawing.cadMultileaders[li], &lmnX, &lmnY, &lmxX, &lmxY);
    SPBox(lmnX, lmnY, lmxX, lmxY, &lmnX, &lmnY, &lmxX, &lmxY);
    bool hit = false;
    if (windowMode)
      hit = lmnX >= mnX && lmxX <= mxX && lmnY >= mnY && lmxY <= mxY;
    else
      hit = !(lmxX < mnX || lmnX > mxX || lmxY < mnY || lmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Multileader;
      e.index = static_cast<int>(li);
      hits.push_back(e);
    }
  }
  for (size_t bi = 0; bi < st.drawing.cadBlockRefs.size(); ++bi) {
    float bmnX = 0.f, bmnY = 0.f, bmxX = 0.f, bmxY = 0.f;
    CadBlockWorldAabb(st.drawing.blockDefs, st.drawing.cadBlockRefs[bi], &bmnX, &bmnY, &bmxX, &bmxY);
    SPBox(bmnX, bmnY, bmxX, bmxY, &bmnX, &bmnY, &bmxX, &bmxY);
    bool hit = false;
    if (windowMode)
      hit = bmnX >= mnX && bmxX <= mxX && bmnY >= mnY && bmxY <= mxY;
    else
      hit = !(bmxX < mnX || bmnX > mxX || bmxY < mnY || bmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::BlockRef;
      e.index = static_cast<int>(bi);
      hits.push_back(e);
    }
  }
  for (size_t ai = 0; ai < st.drawing.userArcs.size(); ++ai) {
    // The arc it draws, in its own plane (REQ-312), in every view. An arc had no analytic fence
    // test to lose: it was tested against its bounding box in PLAN view as well, and a box round
    // a semicircle is mostly the empty space under it.
    const CadArc& a = st.drawing.userArcs[ai];
    if (!(a.r > 1e-12f))
      continue;
    SampleCurveWorld(curvePts, CurvePlane(a), static_cast<double>(a.r), static_cast<double>(a.startRad),
                     static_cast<double>(a.sweepRad), CurveSegmentCount(static_cast<double>(a.sweepRad)));
    if (CurveHitsRect(curvePts)) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Arc;
      e.index = static_cast<int>(ai);
      hits.push_back(e);
    }
  }
  for (size_t ei = 0; ei < st.drawing.userEllipses.size(); ++ei) {
    // The ellipse it draws, in every view — same story as the arc above. Its bounding box was the
    // worst offender of the three: an ellipse fills pi/4 of its box and nothing of its middle.
    // The count follows the DRAWN span, exactly as the arc's does, so a short elliptical arc is not
    // sampled more coarsely than the whole oval it was cut from.
    const CadEllipse& el = st.drawing.userEllipses[ei];
    SampleEllipseWorld(curvePts, el, CurveSegmentCount(std::fabs(static_cast<double>(el.sweepRad))));
    if (CurveHitsRect(curvePts)) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Ellipse;
      e.index = static_cast<int>(ei);
      hits.push_back(e);
    }
  }
  // REQ-443: a spline is tested as the curve it draws, by the same rule as the arc and ellipse above.
  for (size_t si = 0; si < st.drawing.cadSplines.size(); ++si) {
    spline::Tessellate(st.drawing.cadSplines[si], kSplineSampleTolerance, &curvePts);
    if (CurveHitsRect(curvePts)) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Spline;
      e.index = static_cast<int>(si);
      hits.push_back(e);
    }
  }
  // REQ-480: an alignment is tested as the curve it draws (sampled to a hundredth of a unit), by the same rule as
  // the arc and spline above.
  for (size_t ai = 0; ai < st.drawing.alignments.size(); ++ai) {
    if (st.drawing.alignments[ai].Empty())
      continue;
    curvePts.clear();
    for (const alignment::Point2& p : st.drawing.alignments[ai].SampleForDisplay(0.01))
      curvePts.push_back(ray3d::Vec3{p.x, p.y, 0.0});
    if (CurveHitsRect(curvePts)) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Alignment;
      e.index = static_cast<int>(ai);
      hits.push_back(e);
    }
  }
  for (size_t pi = 0; pi < st.drawing.cadPoints.size(); ++pi) {
    const CadPoint& p = st.drawing.cadPoints[pi];
    bool hit = false;
    if (windowMode)
      hit = p.x >= mnX && p.x <= mxX && p.y >= mnY && p.y <= mxY;
    else
      hit = !(p.x < mnX || p.x > mxX || p.y < mnY || p.y > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Point;
      e.index = static_cast<int>(pi);
      hits.push_back(e);
    }
  }
  const int nPoly =
      static_cast<int>(st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    if (ChainHitsRect(st.drawing.userPolylineOffsets, st.drawing.userPolylineVerts, st.drawing.userPolylineClosed, pi, mnX, mxX,
                      mnY, mxY, windowMode, proj ? &projFn : nullptr, &st.drawing.userPolylineVertsBulge)) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Polyline;
      e.index = pi;
      hits.push_back(e);
    }
  }
  // Feature lines (REQ-087) — the same test, through the same `hits` list, so a box drag treats them
  // exactly as it treats polylines. ADR-034 names this and PickClosestCadEntity as the two selection
  // funnels; both now know about feature lines.
  {
    const int nFl =
        static_cast<int>(st.drawing.featureLineOffsets.size() > 0 ? st.drawing.featureLineOffsets.size() - 1 : 0);
    for (int fi = 0; fi < nFl; ++fi) {
      if (ChainHitsRect(st.drawing.featureLineOffsets, st.drawing.featureLineVerts, st.drawing.featureLineClosed, fi, mnX, mxX,
                        mnY, mxY, windowMode, proj ? &projFn : nullptr)) {
        SelectedEntity e{};
        e.type = SelectedEntity::Type::FeatureLine;
        e.index = fi;
        hits.push_back(e);
      }
    }
  }
  // TIN surfaces (REQ-068 / ADR-036 (b)) — hit-tested by their bounding box, matching filled regions
  // and meshes rather than the per-segment chain test above. A surface is selected as a whole, so
  // there is nothing a per-triangle test would decide differently: window mode requires the whole
  // surface inside the rect and crossing requires it to overlap, and both answers come from the
  // bounds. Walking 200k triangles to reach the same conclusion would only cost the frame.
  for (size_t si = 0; si < st.drawing.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(st, si))
      continue;
    const CadTin& t = *st.drawing.cadSurfaces[si].tin;
    float smnX = 0.f, smxX = 0.f, smnY = 0.f, smxY = 0.f;
    bool first = true;
    for (size_t v = 0; v + 2 < t.vertsXyz.size(); v += 3) {
      const float vx = static_cast<float>(t.vertsXyz[v]), vy = static_cast<float>(t.vertsXyz[v + 1]);
      if (first) {
        smnX = smxX = vx;
        smnY = smxY = vy;
        first = false;
      } else {
        smnX = std::min(smnX, vx);
        smxX = std::max(smxX, vx);
        smnY = std::min(smnY, vy);
        smxY = std::max(smxY, vy);
      }
    }
    if (first)
      continue;
    SPBox(smnX, smnY, smxX, smxY, &smnX, &smnY, &smxX, &smxY);  // screen space when orbited
    const bool hit = windowMode ? (smnX >= mnX && smxX <= mxX && smnY >= mnY && smxY <= mxY)
                                : !(smxX < mnX || smnX > mxX || smxY < mnY || smnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Surface;
      e.index = static_cast<int>(si);
      hits.push_back(e);
    }
  }
  // B-rep solids (REQ-313): hit-test the ANALYTIC bounding box, the same window/crossing rule every
  // bbox-tested kind above uses.
  //
  // The analytic bounds and not the stored vertices, for the reason the extents walk gives: a sphere
  // has two stored vertices and a torus four, so a bbox built from points would enclose almost none
  // of the solid — a window drag right around a sphere would select it, and one around a quarter of
  // it would too.
  for (size_t soi = 0; soi < st.drawing.cadSolids.size(); ++soi) {
    if (!SolidVisible(st, soi))
      continue;
    const brep::Bounds bb = brep::ComputeBounds(*st.drawing.cadSolids[soi]);
    if (!bb.valid)
      continue;
    float bmnX = static_cast<float>(bb.mn.x);
    float bmnY = static_cast<float>(bb.mn.y);
    float bmxX = static_cast<float>(bb.mx.x);
    float bmxY = static_cast<float>(bb.mx.y);
    SPBox(bmnX, bmnY, bmxX, bmxY, &bmnX, &bmnY, &bmxX, &bmxY);  // screen space when orbited
    const bool hit = windowMode ? (bmnX >= mnX && bmxX <= mxX && bmnY >= mnY && bmxY <= mxY)
                                : !(bmxX < mnX || bmnX > mxX || bmxY < mnY || bmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Solid;
      e.index = static_cast<int>(soi);
      hits.push_back(e);
    }
  }

  // Point clouds (REQ-171 part 14): bbox over the bounded PREVIEW sample only, mirroring the
  // zoom-extents box walk (same reasoning — the full out-of-core cache is not resident memory).
  for (size_t pci = 0; pci < st.drawing.cadPointClouds.size(); ++pci) {
    if (!PointCloudVisible(st, pci))
      continue;
    const std::shared_ptr<const CadPointCloud>& pc = st.drawing.cadPointClouds[pci];
    if (!pc || pc->pointsXyz.size() < 3)
      continue;
    const std::vector<double>& P = pc->pointsXyz;
    float pmnX = static_cast<float>(P[0]), pmxX = pmnX, pmnY = static_cast<float>(P[1]), pmxY = pmnY;
    for (size_t v = 0; v + 2 < P.size(); v += 3) {
      pmnX = std::min(pmnX, static_cast<float>(P[v]));
      pmxX = std::max(pmxX, static_cast<float>(P[v]));
      pmnY = std::min(pmnY, static_cast<float>(P[v + 1]));
      pmxY = std::max(pmxY, static_cast<float>(P[v + 1]));
    }
    SPBox(pmnX, pmnY, pmxX, pmxY, &pmnX, &pmnY, &pmxX, &pmxY);  // screen space when orbited
    const bool hit = windowMode ? (pmnX >= mnX && pmxX <= mxX && pmnY >= mnY && pmxY <= mxY)
                                : !(pmxX < mnX || pmnX > mxX || pmxY < mnY || pmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::PointCloud;
      e.index = static_cast<int>(pci);
      hits.push_back(e);
    }
  }

  // Filled regions (REQ-042): hit-test the outer-loop bounding box, matching annotations/arcs/PDF — window
  // requires the bbox fully inside; crossing requires the bbox to intersect the rect.
  for (size_t fi = 0; fi < st.drawing.cadFilledRegions.size(); ++fi) {
    float fmnX = 0.f, fmxX = 0.f, fmnY = 0.f, fmxY = 0.f;
    if (!hatchgeom::OuterBounds(st.drawing.cadFilledRegions[fi], &fmnX, &fmnY, &fmxX, &fmxY))
      continue;
    SPBox(fmnX, fmnY, fmxX, fmxY, &fmnX, &fmnY, &fmxX, &fmxY);  // screen space when orbited
    const bool hit = windowMode ? (fmnX >= mnX && fmxX <= mxX && fmnY >= mnY && fmxY <= mxY)
                                : !(fmxX < mnX || fmnX > mxX || fmxY < mnY || fmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::FilledRegion;
      e.index = static_cast<int>(fi);
      hits.push_back(e);
    }
  }
  if (includeSurveyPoints) {
    for (size_t si = 0; si < st.drawing.surveyPoints.size(); ++si) {
      const SurveyPoint& sp = st.drawing.surveyPoints[si];
      // The point's elevation IS its Z (REQ-057), so an orbited box-select tests it where it is
      // actually drawn rather than at its plan position.
      float spx, spy;
      // Screen-space pick projection stays float (render/pick boundary, Phase C precedent) —
      // narrow the double survey-point coordinate here.
      SP(static_cast<float>(sp.easting), static_cast<float>(sp.northing), static_cast<float>(sp.elevation), &spx,
         &spy);
      const bool hitPoint = PointInsideClosedRect(spx, spy, mnX, mxX, mnY, mxY);
      bool hitLabel = false;
      const int lix = FindSurveyLabelAnnIndex(st, sp);
      if (lix >= 0) {
        const CadAnnotation& lab = st.drawing.cadAnnotations[static_cast<size_t>(lix)];
        if (lab.kind == CadAnnotation::Kind::Mtext && lab.surveyPointLabelForId == sp.id) {
          float amnX = 0.f;
          float amnY = 0.f;
          float amxX = 0.f;
          float amxY = 0.f;
          CadAnnotationRoughBounds(lab, st.drawing.modelUnitsPerPlottedInch, &amnX, &amnY, &amxX, &amxY);
          SPBox(amnX, amnY, amxX, amxY, &amnX, &amnY, &amxX, &amxY);  // screen space when orbited
          if (windowMode)
            hitLabel = amnX >= mnX && amxX <= mxX && amnY >= mnY && amxY <= mxY;
          else
            hitLabel = !(amxX < mnX || amnX > mxX || amxY < mnY || amnY > mxY);
        }
      }
      if (hitPoint || hitLabel)
        surveyHits.push_back(static_cast<int>(si));
    }
  }

  // PDF underlays: hit if the rotated bounding box intersects/is-contained-by the selection rect.
  constexpr float kPdfPi = 3.14159265f;
  for (int pi = 0; pi < static_cast<int>(st.drawing.pdfAttachments.size()); ++pi) {
    const PdfAttachment& patt = st.drawing.pdfAttachments[static_cast<size_t>(pi)];
    if (patt.pageWidthPts <= 0.f || patt.pageHeightPts <= 0.f)
      continue;
    const float W    = patt.pageWidthPts  * patt.scale;
    const float H    = patt.pageHeightPts * patt.scale;
    const float cosR = std::cos(patt.rotationDeg * kPdfPi / 180.f);
    const float sinR = std::sin(patt.rotationDeg * kPdfPi / 180.f);
    float cx[4] = {patt.insertX, patt.insertX + W * cosR, patt.insertX + W * cosR - H * sinR,
                   patt.insertX - H * sinR};
    float cy[4] = {patt.insertY, patt.insertY + W * sinR, patt.insertY + W * sinR + H * cosR,
                   patt.insertY + H * cosR};
    float pmnX = cx[0], pmxX = cx[0], pmnY = cy[0], pmxY = cy[0];
    for (int k = 1; k < 4; ++k) {
      pmnX = std::min(pmnX, cx[k]); pmxX = std::max(pmxX, cx[k]);
      pmnY = std::min(pmnY, cy[k]); pmxY = std::max(pmxY, cy[k]);
    }
    SPBox(pmnX, pmnY, pmxX, pmxY, &pmnX, &pmnY, &pmxX, &pmxY);
    bool hit = false;
    if (windowMode)
      hit = pmnX >= mnX && pmxX <= mxX && pmnY >= mnY && pmxY <= mxY;
    else
      hit = !(pmxX < mnX || pmnX > mxX || pmxY < mnY || pmnY > mxY);
    if (hit) {
      SelectedEntity e{};
      e.type  = SelectedEntity::Type::PdfUnderlay;
      e.index = pi;
      hits.push_back(e);
    }
  }

  // REQ-084 (d): drop isolated-out entities before the hits are applied, so a box drag across
  // where they used to be selects nothing. One filter here covers window and crossing, add and
  // subtract alike.
  if (!st.hiddenEntityIds.empty()) {
    hits.erase(std::remove_if(hits.begin(), hits.end(),
                              [&](const SelectedEntity& e) { return CadSelectedEntityHidden(st, e); }),
               hits.end());
  }

  if (subtract) {
    std::vector<SelectedEntity> kept;
    kept.reserve(st.selection.size());
    for (const auto& e : st.selection) {
      bool remove = false;
      for (const auto& h : hits) {
        if (SelectedEntityEqual(h, e)) {
          remove = true;
          break;
        }
      }
      if (!remove)
        kept.push_back(e);
    }
    st.selection = std::move(kept);
    auto& sv = st.selectedSurveyPointIndices;
    sv.erase(std::remove_if(sv.begin(), sv.end(),
                            [&](int ix) {
                              return std::find(surveyHits.begin(), surveyHits.end(), ix) != surveyHits.end();
                            }),
             sv.end());
  } else {
    for (const auto& h : hits) {
      bool has = false;
      for (const auto& e : st.selection) {
        if (SelectedEntityEqual(h, e)) {
          has = true;
          break;
        }
      }
      if (!has)
        st.selection.push_back(h);
    }
    for (int six : surveyHits) {
      auto& sv = st.selectedSurveyPointIndices;
      if (std::find(sv.begin(), sv.end(), six) == sv.end())
        sv.push_back(six);
    }
  }
}


void ClearCadSelection(AppCommandState& st) {
  st.selection.clear();
  // The sub-object selection goes with it. Every caller of this function means "nothing is selected
  // now" — ESC, a new drawing, a command that consumes the selection — and leaving sub-objects
  // behind would be a selection the user cannot see a reason for (REQ-318 item 9).
  st.subObjectSelection.clear();
  // And any armed gizmo drag (REQ-060): the gizmo hangs off the selection, so a selection that is
  // gone leaves a drag with nothing to move and an anchor pointing at where something used to be.
  CancelGizmoDrag(st);
  st.gizmoHoverAxis = -1;
  // The section plane's handles too (REQ-343). It is not in `selection` — it is a view state, not
  // an entity — but every caller here means "nothing is selected now", and a set of handles left
  // floating after ESC is a selection the user was told they no longer had. The PLANE itself stays
  // exactly where it is: deselecting is not turning the clip off.
  st.sectionPlaneSelected = false;
  CancelSectionPlaneGripDrag(st);
  st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  st.selBoxWaitingSecond = false;
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  CancelTableCellEditor(st);
}


void ApplyEntityGripPoint(AppCommandState& st, float x, float y, float z) {
  if (!st.entityGripMoveActive)
    return;
  const int idx = st.entityGripEntityIndex;
  if (idx < 0)
    return;
  switch (st.entityGripType) {
  case SelectedEntity::Type::LineSeg: {
    if (static_cast<size_t>(idx) >= st.drawing.lineCount())
      return;
    const size_t k = static_cast<size_t>(idx);
    if (st.entityGripLengthenMode) {
      const float x0 = static_cast<float>(st.drawing.line(k).x0);
      const float y0 = static_cast<float>(st.drawing.line(k).y0);
      const float x1 = static_cast<float>(st.drawing.line(k).x1);
      const float y1 = static_cast<float>(st.drawing.line(k).y1);
      const float fixedX = st.entityGripWhich == 0 ? x1 : x0;
      const float fixedY = st.entityGripWhich == 0 ? y1 : y0;
      float along = (x - fixedX) * st.entityGripLengthenDirX + (y - fixedY) * st.entityGripLengthenDirY;
      if (!(along > 1e-6f) || !std::isfinite(along))
        along = 1e-6f;
      x = fixedX + st.entityGripLengthenDirX * along;
      y = fixedY + st.entityGripLengthenDirY * along;
    }
    if (st.entityGripWhich == 0) {
      st.drawing.lineComponent(k, CadLineComponent::X0) = x;
      st.drawing.lineComponent(k, CadLineComponent::Y0) = y;
    } else if (st.entityGripWhich == 1) {
      st.drawing.lineComponent(k, CadLineComponent::X1) = x;
      st.drawing.lineComponent(k, CadLineComponent::Y1) = y;
    }
    return;
  }
  case SelectedEntity::Type::Alignment: {  // REQ-487.1: re-derive from the grabbed shape every move, through the solver
    if (static_cast<size_t>(idx) >= st.drawing.alignments.size())
      return;
    alignment::Alignment moved;
    (void)alignment::DragGrip(st.entityGripOrigAlignment, st.entityGripWhich, {static_cast<double>(x), static_cast<double>(y)}, &moved);
    st.drawing.alignments[static_cast<size_t>(idx)] = std::move(moved);
    std::vector<std::string> quiet;
    RefreshAlignmentOffsets(st, quiet);
    return;
  }
  case SelectedEntity::Type::Circle: {
    if (static_cast<size_t>(idx) >= st.drawing.circleCount())
      return;
    const size_t ci = static_cast<size_t>(idx);
    double& cx = st.drawing.circleComponent(ci, CadCircleComponent::X);
    double& cy = st.drawing.circleComponent(ci, CadCircleComponent::Y);
    double& r = st.drawing.circleComponent(ci, CadCircleComponent::Radius);
    if (st.entityGripWhich == 0) {
      cx = x;
      cy = y;
    } else if (st.entityGripWhich == 1) {
      r = std::hypot(x - cx, y - cy);
    }
    return;
  }
  case SelectedEntity::Type::Polyline: {
    const int np = st.drawing.userPolylineOffsets.size() > 0 ? static_cast<int>(st.drawing.userPolylineOffsets.size() - 1) : 0;
    if (idx >= np)
      return;
    const int startV = st.drawing.userPolylineOffsets[static_cast<size_t>(idx)];
    const int endV = st.drawing.userPolylineOffsets[(static_cast<size_t>(idx)+ 1)];
    // REQ-316 / ADR-047: an arc-segment midpoint grip drags the bulge so the arc passes through
    // the cursor. `which` is kPolyBulgeGripBase + segmentIndex.
    if (st.entityGripWhich >= kPolyBulgeGripBase) {
      const int seg = st.entityGripWhich - kPolyBulgeGripBase;
      const int va = startV + seg;
      const bool closed = static_cast<size_t>(idx) < st.drawing.userPolylineClosed.size() &&
                          st.drawing.userPolylineClosed[static_cast<size_t>(idx)];
      const int vb = (seg == (endV - startV) - 1 && closed) ? startV : va + 1;
      if (static_cast<size_t>(vb) >= st.drawing.vertexCount())
        return;
      SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
      SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());  // REQ-325 / ADR-053
      const double b = ArcBulgeThrough(st.drawing.vertex(va).x,
                                       st.drawing.vertex(va).y,
                                       static_cast<double>(x), static_cast<double>(y),
                                       st.drawing.vertex(vb).x,
                                       st.drawing.vertex(vb).y);
      st.drawing.userPolylineVertsBulge[static_cast<size_t>(va)] = static_cast<float>(b);
      return;
    }
    const int globalV = startV + st.entityGripWhich;
    const size_t xIdx = static_cast<size_t>(globalV);
    if (xIdx >= st.drawing.vertexCount())
      return;
    st.drawing.vertexComponent(xIdx, CadVertexComponent::X) = x;
    st.drawing.vertexComponent(xIdx, CadVertexComponent::Y) = y;
    return;
  }
  case SelectedEntity::Type::Spline: {  // REQ-443: drag one fit point / control vertex; its Z is kept
    if (static_cast<size_t>(idx) >= st.drawing.cadSplines.size() || st.entityGripWhich < 0)
      return;
    CadSpline& sp = st.drawing.cadSplines[static_cast<size_t>(idx)];
    const size_t which = static_cast<size_t>(st.entityGripWhich);
    if (which >= spline::EditablePointCount(sp))
      return;
    const spline::Vec3 old = spline::EditablePoint(sp, which);
    // A drag step that would make the fit points degenerate (two neighbours on top of each other) is
    // simply not applied, so the curve on screen is always a valid one and the last good shape stands.
    std::string why;
    (void)spline::SetEditablePoint(&sp, which, spline::Vec3{static_cast<double>(x), static_cast<double>(y), old.z}, &why);
    return;
  }
  case SelectedEntity::Type::Point: {
    if (static_cast<size_t>(idx) >= st.drawing.cadPoints.size())
      return;
    st.drawing.cadPoints[static_cast<size_t>(idx)].x = x;
    st.drawing.cadPoints[static_cast<size_t>(idx)].y = y;
    return;
  }
  case SelectedEntity::Type::Arc: {
    if (static_cast<size_t>(idx) >= st.drawing.userArcs.size())
      return;
    CadArc& a = st.drawing.userArcs[static_cast<size_t>(idx)];
    if (st.entityGripWhich == 0) {
      a.cx = x;
      a.cy = y;
    } else if (st.entityGripWhich == 1) {
      a.r = std::hypot(x - a.cx, y - a.cy);
      a.startRad = static_cast<float>(std::atan2(y - a.cy, x - a.cx));
    } else if (st.entityGripWhich == 2) {
      a.r = std::hypot(x - a.cx, y - a.cy);
      a.sweepRad = static_cast<float>(std::atan2(y - a.cy, x - a.cx) - a.startRad);
    }
    return;
  }
  case SelectedEntity::Type::Ellipse: {
    if (static_cast<size_t>(idx) >= st.drawing.userEllipses.size())
      return;
    CadEllipse& el = st.drawing.userEllipses[static_cast<size_t>(idx)];
    if (EllipseIsFlat(el)) {
      if (st.entityGripWhich == 0) {
        el.cx = x;
        el.cy = y;
      } else if (st.entityGripWhich == 1) {
        el.majVx = static_cast<float>(x - el.cx);
        el.majVy = static_cast<float>(y - el.cy);
      } else if (st.entityGripWhich == 2) {
        const float majLen2 = el.majVx * el.majVx + el.majVy * el.majVy;
        if (majLen2 < 1e-12f)
          return;
        el.ratio = static_cast<float>(std::clamp<double>(((x - el.cx) * -el.majVy + (y - el.cy) * el.majVx) / majLen2, 0.0, 1.0));
      }
      return;
    }
    // A TILTED ellipse is reshaped IN ITS OWN PLANE (GitHub #531). `majVx`/`majVy` are that plane's
    // 2D frame, never world XY, so the dragged point is carried into the plane first; the three
    // branches below are then the same arithmetic as the flat ones, one frame up.
    {
      const ucs::Ucs plane = CurvePlane(el);
      const ray3d::Vec3 w{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
      const ucs::Point2D p = ucs::WorldToPlane(plane, w);  // the plane's origin IS the centre
      if (st.entityGripWhich == 0) {
        // The centre carries the plane with it; the normal, and so the ellipse's attitude, is
        // unchanged — the same thing moving a flat ellipse's centre grip does.
        el.cx = x;
        el.cy = y;
        el.z = z;
      } else if (st.entityGripWhich == 1) {
        if (std::hypot(p.x, p.y) < 1e-12)
          return;
        el.majVx = static_cast<float>(p.x);
        el.majVy = static_cast<float>(p.y);
      } else if (st.entityGripWhich == 2) {
        const double majLen2 = static_cast<double>(el.majVx) * el.majVx + static_cast<double>(el.majVy) * el.majVy;
        if (majLen2 < 1e-12)
          return;
        el.ratio = static_cast<float>(std::clamp(
            (p.x * -static_cast<double>(el.majVy) + p.y * static_cast<double>(el.majVx)) / majLen2, 0.0, 1.0));
      }
    }
    return;
  }
  case SelectedEntity::Type::BlockRef: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.cadBlockRefs.size())
      return;
    CadBlockRef& r = st.drawing.cadBlockRefs[static_cast<size_t>(idx)];
    const int di = CadBlockFindDef(st.drawing.blockDefs, r.defName);
    if (di < 0)
      return;
    CadBlockApplyDynGripDrag(&r, st.drawing.blockDefs[static_cast<size_t>(di)], st.entityGripWhich, x, y);
    return;
  }
  default:
    return;
  }
}

// "Similar" = same object type AND same layer AND same colour, matching AutoCAD's SELECTSIMILAR with
// layer and colour in SELECTSIMILARMODE. Type alone swept up every line in the drawing, which is not a
// useful selection on a survey plan where layer *is* the classification.
namespace {

/// Layer names are compared case-insensitively and an empty name means layer "0" (the storage default),
/// so entities that differ only in how their layer was spelled still match.
bool SelectSimilarAttrsMatch(const EntityAttributes& a, const EntityAttributes& b) {
  const std::string la = StringUtil::toLowerAsciiCopy(a.layer.empty() ? std::string("0") : a.layer);
  const std::string lb = StringUtil::toLowerAsciiCopy(b.layer.empty() ? std::string("0") : b.layer);
  if (la != lb)
    return false;
  // Colour is a free-form string ("ByLayer", "Red", "#RRGGBB") — compare case-insensitively so
  // "ByLayer" and "BYLAYER" are the same colour, which is how the rest of the code treats them.
  const std::string ca = StringUtil::toLowerAsciiCopy(a.color.empty() ? std::string("ByLayer") : a.color);
  const std::string cb = StringUtil::toLowerAsciiCopy(b.color.empty() ? std::string("ByLayer") : b.color);
  return ca == cb;
}

/// Attributes of one entity, or defaults when the parallel attr array is short (older drawings).
EntityAttributes SelectSimilarAttrsOf(const AppCommandState& st, const SelectedEntity& e) {
  const auto pick = [](const std::vector<EntityAttributes>& v, int i) {
    return (i >= 0 && static_cast<size_t>(i) < v.size()) ? v[static_cast<size_t>(i)] : EntityAttributes{};
  };
  switch (e.type) {
  case SelectedEntity::Type::LineSeg:    return pick(st.drawing.userLineAttrs, e.index);
  case SelectedEntity::Type::Circle:     return pick(st.drawing.userCircleAttrs, e.index);
  case SelectedEntity::Type::Polyline:   return pick(st.drawing.userPolylineAttrs, e.index);
  case SelectedEntity::Type::Arc:        return pick(st.drawing.userArcAttrs, e.index);
  case SelectedEntity::Type::Ellipse:    return pick(st.drawing.userEllAttrs, e.index);
  case SelectedEntity::Type::Spline:     return pick(st.drawing.cadSplineAttrs, e.index);  // REQ-443
  case SelectedEntity::Type::Point:      return pick(st.drawing.cadPointAttrs, e.index);   // REQ-456
  case SelectedEntity::Type::Annotation: return pick(st.drawing.cadAnnotationAttrs, e.index);
  case SelectedEntity::Type::Table:      return pick(st.drawing.cadTableAttrs, e.index);
  case SelectedEntity::Type::BlockRef:   return pick(st.drawing.cadBlockRefAttrs, e.index);
  default:                               return EntityAttributes{};
  }
}

} // namespace

void SelectSimilarToCurrentSelection(AppCommandState& st, std::vector<std::string>* log) {
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  ClearEntityGripInteraction(st);

  if (!st.selection.empty()) {
    st.selectedSurveyPointIndices.clear();
    const SelectedEntity& lead = st.selection.front();
    const EntityAttributes want = SelectSimilarAttrsOf(st, lead);
    std::vector<SelectedEntity> next;

    // Candidate of the lead's type; keep it only when its layer and colour match the lead's.
    const auto consider = [&](SelectedEntity::Type type, int index) {
      SelectedEntity e{};
      e.type = type;
      e.index = index;
      if (SelectSimilarAttrsMatch(SelectSimilarAttrsOf(st, e), want))
        next.push_back(e);
    };

    switch (lead.type) {
    case SelectedEntity::Type::LineSeg: {
      const size_t n = st.drawing.lineCount();
      for (size_t i = 0; i < n; ++i)
        consider(SelectedEntity::Type::LineSeg, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Circle: {
      const size_t n = st.drawing.circleCount();
      for (size_t i = 0; i < n; ++i)
        consider(SelectedEntity::Type::Circle, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Polyline: {
      const int np =
          st.drawing.userPolylineOffsets.size() > 1 ? static_cast<int>(st.drawing.userPolylineOffsets.size()) - 1 : 0;
      for (int i = 0; i < np; ++i)
        consider(SelectedEntity::Type::Polyline, i);
      break;
    }
    case SelectedEntity::Type::Arc: {
      for (size_t i = 0; i < st.drawing.userArcs.size(); ++i)
        consider(SelectedEntity::Type::Arc, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Ellipse: {
      for (size_t i = 0; i < st.drawing.userEllipses.size(); ++i)
        consider(SelectedEntity::Type::Ellipse, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Spline: {  // REQ-443
      for (size_t i = 0; i < st.drawing.cadSplines.size(); ++i)
        consider(SelectedEntity::Type::Spline, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Point: {
      for (size_t i = 0; i < st.drawing.cadPoints.size(); ++i)
        consider(SelectedEntity::Type::Point, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Annotation: {
      // Annotations narrow further by annotation kind: TEXT is not similar to a dimension even on
      // one layer in one colour.
      const int lix = lead.index;
      if (lix < 0 || static_cast<size_t>(lix) >= st.drawing.cadAnnotations.size())
        break;
      const CadAnnotation::Kind wantKind = st.drawing.cadAnnotations[static_cast<size_t>(lix)].kind;
      for (size_t ai = 0; ai < st.drawing.cadAnnotations.size(); ++ai) {
        if (st.drawing.cadAnnotations[ai].kind == wantKind)
          consider(SelectedEntity::Type::Annotation, static_cast<int>(ai));
      }
      break;
    }
    case SelectedEntity::Type::Surface: {
      // REQ-068 / ADR-036 (b). Skips invisible surfaces, so SELECTSIMILAR cannot select something
      // the user cannot see — the same rule the pick funnel applies (REQ-084 (d)).
      for (size_t i = 0; i < st.drawing.cadSurfaces.size(); ++i)
        if (SurfaceVisible(st, i))
          consider(SelectedEntity::Type::Surface, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::Table: {
      for (size_t i = 0; i < st.drawing.cadTables.size(); ++i)
        consider(SelectedEntity::Type::Table, static_cast<int>(i));
      break;
    }
    case SelectedEntity::Type::BlockRef: {
      for (size_t i = 0; i < st.drawing.cadBlockRefs.size(); ++i)
        consider(SelectedEntity::Type::BlockRef, static_cast<int>(i));
      break;
    }
    default:
      break;
    }
    st.selection = std::move(next);
    EnsureAttrCounts(st);
    BumpCadGpuCache(st);
    if (log) {
      const std::string layerName = want.layer.empty() ? std::string("0") : want.layer;
      const std::string colorName = want.color.empty() ? std::string("ByLayer") : want.color;
      log->push_back("Select similar — " + std::to_string(st.selection.size()) +
                     " object(s) on layer " + layerName + ", colour " + colorName + ".");
    }
    return;
  }

  if (!st.selectedSurveyPointIndices.empty()) {
    ClearCadSelection(st);
    st.selectedSurveyPointIndices.clear();
    for (size_t i = 0; i < st.drawing.surveyPoints.size(); ++i)
      st.selectedSurveyPointIndices.push_back(static_cast<int>(i));
    for (int svi : st.selectedSurveyPointIndices) {
      if (svi >= 0 && static_cast<size_t>(svi) < st.drawing.surveyPoints.size())
        SyncSurveyPointLinkedMtextSelection(st, svi);
    }
    BumpCadGpuCache(st);
    if (log)
      log->push_back("Select similar — all " + std::to_string(st.drawing.surveyPoints.size()) + " survey point(s).");
  } else if (log)
    log->push_back("Select similar — nothing selected.");
}


void ApplySurveyPointClickSelection(AppCommandState& st, int surveyPointIndex, bool shiftModifier,
                                    std::vector<std::string>* log) {
  if (surveyPointIndex < 0 || static_cast<size_t>(surveyPointIndex) >= st.drawing.surveyPoints.size())
    return;
  auto& v = st.selectedSurveyPointIndices;
  const auto it = std::find(v.begin(), v.end(), surveyPointIndex);
  if (shiftModifier) {
    if (it != v.end()) {
      v.erase(it);
      if (log)
        log->push_back("Survey point removed from selection.");
    } else {
      v.push_back(surveyPointIndex);
      if (log)
        log->push_back("Survey point added to selection.");
    }
  } else if (it == v.end()) {
    v.push_back(surveyPointIndex);
  } else {
    v.clear();
    v.push_back(surveyPointIndex);
  }
}


namespace {

/// Depth sort key for one entity at a pick — ray \p t when orbited, closest-point Z in plan view.
double CadEntityPickDepthAtPick(const AppCommandState& st, const SelectedEntity& e, double wx, double wy,
                                const ray3d::Ray* pickRay) {
  const bool useRay = pickRay != nullptr && pickRay->valid();
  using T = SelectedEntity::Type;
  switch (e.type) {
  case T::LineSeg: {
    const size_t li = static_cast<size_t>(e.index);
    if (li >= st.drawing.lineCount())
      return 0.0;
    const CadLineRecord seg = st.drawing.line(li);
    if (useRay) {
      double t = 0.0;
      (void)ray3d::RaySegmentDistance(*pickRay, ray3d::Vec3{seg.x0, seg.y0, seg.z0},
                                      ray3d::Vec3{seg.x1, seg.y1, seg.z1}, &t);
      return t;
    }
    const double ax = seg.x0, ay = seg.y0, az = seg.z0;
    const double bx = seg.x1, by = seg.y1, bz = seg.z1;
    const double vx = bx - ax, vy = by - ay;
    const double len2 = vx * vx + vy * vy;
    if (len2 < 1e-24)
      return az;
    const double u = std::clamp(((wx - ax) * vx + (wy - ay) * vy) / len2, 0.0, 1.0);
    return az + u * (bz - az);
  }
  case T::Circle: {
    const size_t ci = static_cast<size_t>(e.index);
    if (ci >= st.drawing.circleCount())
      return 0.0;
    const CadCircleRecord cc = st.drawing.circle(ci);
    if (useRay) {
      double bestT = 1e300;
      constexpr int n = 24;
      constexpr double twopi = 6.28318530717958647692;
      const double cx = cc.cx, cy = cc.cy, cz = cc.cz, r = cc.r;
      for (int i = 0; i < n; ++i) {
        const double ang = twopi * static_cast<double>(i) / static_cast<double>(n);
        double t = 0.0;
        (void)ray3d::RayPointDistance(*pickRay, ray3d::Vec3{cx + r * std::cos(ang), cy + r * std::sin(ang), cz},
                                     &t);
        bestT = std::min(bestT, t);
      }
      return bestT;
    }
    return cc.cz;
  }
  case T::Arc: {
    if (static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
      return 0.0;
    const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
    if (useRay) {
      double bestT = 1e300;
      constexpr int n = 24;
      for (int i = 0; i <= n; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(n);
        const double ang = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * u;
        const double x = static_cast<double>(a.cx) + static_cast<double>(a.r) * std::cos(ang);
        const double y = static_cast<double>(a.cy) + static_cast<double>(a.r) * std::sin(ang);
        double t = 0.0;
        (void)ray3d::RayPointDistance(*pickRay, ray3d::Vec3{x, y, static_cast<double>(a.z)}, &t);
        bestT = std::min(bestT, t);
      }
      return bestT;
    }
    return static_cast<double>(a.z);
  }
  case T::Spline: {  // REQ-443: nearest point of its control polygon by elevation
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadSplines.size() ||
        st.drawing.cadSplines[static_cast<size_t>(e.index)].ctrl.empty())
      return 0.0;
    return st.drawing.cadSplines[static_cast<size_t>(e.index)].ctrl.front().z;
  }
  case T::Point: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.cadPoints.size())
      return 0.0;
    return st.drawing.cadPoints[static_cast<size_t>(e.index)].z;
  }
  case T::Ellipse: {
    if (static_cast<size_t>(e.index) >= st.drawing.userEllipses.size())
      return 0.0;
    const CadEllipse& el = st.drawing.userEllipses[static_cast<size_t>(e.index)];
    return static_cast<double>(el.z);
  }
  case T::Polyline: {
    if (static_cast<size_t>(e.index) + 1 >= st.drawing.userPolylineOffsets.size())
      return 0.0;
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(e.index)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(e.index)+ 1)];
    double bestDepth = useRay ? 1e300 : -1e300;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t A = static_cast<size_t>(vi), B = (static_cast<size_t>(vi)+ 1);
      if (B >= st.drawing.vertexCount())
        break;
      if (useRay) {
        double t = 0.0;
        (void)ray3d::RaySegmentDistance(
            *pickRay,
            ray3d::Vec3{st.drawing.vertex(A).x, st.drawing.vertex(A).y, st.drawing.vertex(A).z},
            ray3d::Vec3{st.drawing.vertex(B).x, st.drawing.vertex(B).y, st.drawing.vertex(B).z}, &t);
        bestDepth = std::min(bestDepth, t);
      } else {
        const double az = st.drawing.vertex(A).z, bz = st.drawing.vertex(B).z;
        bestDepth = std::max(bestDepth, std::max(az, bz));
      }
    }
    return bestDepth;
  }
  case T::FeatureLine: {
    if (static_cast<size_t>(e.index) + 1 >= st.drawing.featureLineOffsets.size())
      return 0.0;
    const int v0 = st.drawing.featureLineOffsets[static_cast<size_t>(e.index)];
    const int v1 = st.drawing.featureLineOffsets[(static_cast<size_t>(e.index)+ 1)];
    double bestDepth = useRay ? 1e300 : -1e300;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const size_t A = static_cast<size_t>(vi) * 3, B = (static_cast<size_t>(vi)+ 1) * 3;
      if (B + 2 >= st.drawing.featureLineVerts.size())
        break;
      if (useRay) {
        double t = 0.0;
        (void)ray3d::RaySegmentDistance(
            *pickRay,
            ray3d::Vec3{static_cast<float>(st.drawing.featureLineVerts[A]),
                        static_cast<float>(st.drawing.featureLineVerts[A + 1]),
                        static_cast<float>(st.drawing.featureLineVerts[A + 2])},
            ray3d::Vec3{static_cast<float>(st.drawing.featureLineVerts[B]),
                        static_cast<float>(st.drawing.featureLineVerts[B + 1]),
                        static_cast<float>(st.drawing.featureLineVerts[B + 2])},
            &t);
        bestDepth = std::min(bestDepth, t);
      } else {
        const double az = st.drawing.featureLineVerts[A + 2], bz = st.drawing.featureLineVerts[B + 2];
        bestDepth = std::max(bestDepth, std::max(az, bz));
      }
    }
    return bestDepth;
  }
  case T::Solid: {
    if (static_cast<size_t>(e.index) >= st.drawing.cadSolids.size())
      return 0.0;
    const CadSolidPtr& sp = st.drawing.cadSolids[static_cast<size_t>(e.index)];
    double bestDepth = useRay ? 1e300 : -1e300;
    for (const brep::Edge& ed : sp->edges) {
      const int steps = ed.kind == brep::CurveKind::Arc ? 12 : 1;
      ray3d::Vec3 prev = brep::EdgePointAt(*sp, ed, 0.0);
      for (int i = 1; i <= steps; ++i) {
        const ray3d::Vec3 next = brep::EdgePointAt(*sp, ed, static_cast<double>(i) / steps);
        if (useRay) {
          double t = 0.0;
          (void)ray3d::RaySegmentDistance(*pickRay, prev, next, &t);
          bestDepth = std::min(bestDepth, t);
        } else {
          bestDepth = std::max(bestDepth, std::max(prev.z, next.z));
        }
        prev = next;
      }
    }
    return bestDepth;
  }
  case T::Surface:
  case T::BlockRef:
  case T::Table:
  case T::Annotation:
  case T::FilledRegion:
  case T::PdfUnderlay:
  case T::Mesh:
  case T::PipeRun:
  case T::PointCloud:
    return static_cast<double>(e.index);
  case T::SectionPlane:
    // REQ-343 amended / ADR-059 (i), issue #479. Unreachable: nothing in this file ever builds a
    // CadPickCandidate with type SectionPlane — the plane is a type TAG only, picked and selected
    // through its own dedicated path (SubmitSectionPlaneClick / sectionPlaneSelected), never through
    // the normal PICK -> st.selection route this depth-sort key serves. Stated explicitly to keep
    // this switch -Wswitch-clean rather than leave it a silent fallthrough.
    return 0.0;
  }
  return 0.0;
}

} // namespace

bool PickCadEntityByDepth(const std::vector<CadPickCandidate>& candidates, SelectedEntity* out,
                          const ray3d::Ray* pickRay) {
  if (candidates.empty() || !out)
    return false;
  const bool useRay = pickRay != nullptr && pickRay->valid();
  const CadPickCandidate* best = &candidates.front();
  for (const CadPickCandidate& c : candidates) {
    if (useRay) {
      if (c.depthKey < best->depthKey - 1e-9)
        best = &c;
    } else if (c.depthKey > best->depthKey + 1e-9) {
      best = &c;
    }
  }
  *out = best->entity;
  return true;
}

bool PickClosestCadEntity(const AppCommandState& st, double wx, double wy, float tolWorld, SelectedEntity* out,
                          float* outDistSq, const ray3d::Ray* pickRay,
                          std::vector<CadPickCandidate>* allCandidates) {
  // \p outDistSq is optional: a caller that wants only the entity passes null. Rejecting null
  // here is what made every `UCS Object` pick answer "no object found at that point" - the
  // option could not succeed anywhere, at any zoom, on any drawing, under a green suite.
  float unusedDistSq = 0.f;
  if (!out)
    return false;
  if (!outDistSq)
    outDistSq = &unusedDistSq;
  const double tol2 = static_cast<double>(tolWorld) * static_cast<double>(tolWorld);

  // Distance metric (REQ-058). With no ray this is the historical plan-view XY distance, unchanged
  // and bit-identical. With a ray — an orbited camera — it is the true 3D distance from the
  // cursor's ray to the geometry, which is the only way to pick something that is elevated: the
  // ray crosses the work plane at one XY and the elevated entity at another, so an XY test would
  // measure against the wrong point entirely.
  //
  // Only the METRIC changes here. Entity enumeration, strides and indexing are shared by both
  // paths, so the orbited path cannot drift out of step with the plan path.
  const bool useRay = pickRay != nullptr && pickRay->valid();
  auto d2Point = [&](double px, double py, double pz) -> double {
    if (!useRay) {
      const double dx = wx - px, dy = wy - py;
      return dx * dx + dy * dy;
    }
    const double d = ray3d::RayPointDistance(*pickRay, ray3d::Vec3{px, py, pz});
    return d * d;
  };
  auto d2Segment = [&](double ax, double ay, double az, double bx, double by, double bz) -> double {
    if (!useRay)
      return PickDistSqPointSegmentD(wx, wy, ax, ay, bx, by);
    const double d = ray3d::RaySegmentDistance(*pickRay, ray3d::Vec3{ax, ay, az}, ray3d::Vec3{bx, by, bz});
    return d * d;
  };
  bool any = false;
  double best = 0.0;
  SelectedEntity bestE{};
  auto consider = [&](const SelectedEntity& e, double d2) {
    if (d2 > tol2)
      return;
    // REQ-084 (d): an isolated-out entity is invisible, so it must not answer a click either.
    // Gated at this single funnel — every entity type this function enumerates passes through it,
    // so there is no per-type gate to forget.
    if (CadSelectedEntityHidden(st, e))
      return;
    if (allCandidates)
      allCandidates->push_back({e, d2, 0.0});
    if (!any || d2 < best - 1e-12) {
      any = true;
      best = d2;
      bestE = e;
    }
  };

  if (st.drawing.lineStoreWellFormed()) {
    for (size_t i = 0; i < st.drawing.lineCount(); ++i) {
      const CadLineRecord ln = st.drawing.line(i);
      SelectedEntity e{};
      e.type = SelectedEntity::Type::LineSeg;
      e.index = static_cast<int>(i);
      const double d2 = d2Segment(ln.x0, ln.y0, ln.z0, ln.x1, ln.y1, ln.z1);
      consider(e, d2);
    }
  }
  if (st.drawing.circleStoreWellFormed()) {
    for (size_t ci = 0; ci < st.drawing.circleCount(); ++ci) {
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Circle;
      e.index = static_cast<int>(ci);
      const CadCircleRecord cr = st.drawing.circle(ci);
      const double cx = cr.cx;
      const double cy = cr.cy;
      const double cz = cr.cz;
      const double r = cr.r;
      if (!useRay) {
        // Plan view keeps the exact analytic distance-to-circumference — unchanged.
        const double d = std::hypot(wx - cx, wy - cy);
        const double dr = d - r;
        consider(e, dr * dr);
      } else {
        // Orbited: sample the circumference on the circle's own plane. A tilted circle
        // (userCircleNormals) was being sampled as if it lay flat in world XY regardless — the
        // sample ring landed nowhere near the circle actually on screen, so a cursor visibly over
        // a tilted circle in a 3D view never hit it (GUI pass, issue #486 follow-up).
        double bestD2 = 1e300;
        constexpr int n = 48;
        constexpr double twopi = 6.28318530717958647692;
        const size_t nIdx = ci * 3;
        float nx = 0.f, ny = 0.f, nz = 1.f;
        if (nIdx + 2 < st.drawing.userCircleNormals.size()) {
          nx = st.drawing.userCircleNormals[nIdx];
          ny = st.drawing.userCircleNormals[nIdx + 1];
          nz = st.drawing.userCircleNormals[nIdx + 2];
        }
        ucs::Ucs plane;
        const bool tilted = !IsFlatNormal(nx, ny, nz) &&
                            ucs::FromNormal({cx, cy, cz}, {static_cast<double>(nx), static_cast<double>(ny),
                                                            static_cast<double>(nz)}, &plane);
        for (int i = 0; i < n; ++i) {
          const double ang = twopi * static_cast<double>(i) / static_cast<double>(n);
          if (tilted) {
            const ucs::Vec3 p = ucs::PointOnPlaneCircle(plane, r, ang);
            bestD2 = std::min(bestD2, d2Point(p.x, p.y, p.z));
          } else {
            bestD2 = std::min(bestD2, d2Point(cx + r * std::cos(ang), cy + r * std::sin(ang), cz));
          }
        }
        consider(e, bestD2);
      }
    }
  }
  for (size_t ai = 0; ai < st.drawing.userArcs.size(); ++ai) {
    const CadArc& a = st.drawing.userArcs[ai];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Arc;
    e.index = static_cast<int>(ai);
    double bestD2 = 1e300;
    if (!useRay && IsFlatNormal(a.nx, a.ny, a.nz)) {
      // Exact distance to the arc (REQ-316): a hover/pick aperture is the same on a curve as a line.
      BulgeArcSpan span;
      span.valid = true;
      span.cx = a.cx;
      span.cy = a.cy;
      span.radius = a.r;
      span.startAngle = a.startRad;
      span.sweep = a.sweepRad;
      bestD2 = PointArcDistanceSq(wx, wy, span);
    } else {
      // Orbited: same tilted-plane fix as the circle branch above — a tilted arc (a.nx/ny/nz) was
      // being sampled flat in world XY, missing it entirely under an orbited view.
      ucs::Ucs plane;
      const bool tilted =
          !IsFlatNormal(a.nx, a.ny, a.nz) &&
          ucs::FromNormal({a.cx, a.cy, a.z},
                          {static_cast<double>(a.nx), static_cast<double>(a.ny), static_cast<double>(a.nz)}, &plane);
      constexpr int n = 36;
      for (int i = 0; i <= n; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(n);
        const double ang = static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad) * u;
        if (tilted) {
          const ucs::Vec3 p = ucs::PointOnPlaneCircle(plane, a.r, ang);
          bestD2 = std::min(bestD2, d2Point(p.x, p.y, p.z));
        } else {
          const double x = static_cast<double>(a.cx) + static_cast<double>(a.r) * std::cos(ang);
          const double y = static_cast<double>(a.cy) + static_cast<double>(a.r) * std::sin(ang);
          bestD2 = std::min(bestD2, d2Point(x, y, static_cast<double>(a.z)));
        }
      }
    }
    consider(e, bestD2);
  }
  for (size_t ei = 0; ei < st.drawing.userEllipses.size(); ++ei) {
    const CadEllipse& el = st.drawing.userEllipses[ei];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Ellipse;
    e.index = static_cast<int>(ei);
    const double ma = std::hypot(static_cast<double>(el.majVx), static_cast<double>(el.majVy));
    double bestD2 = 1e300;
    if (ma >= 1e-8) {
      // Through the ellipse's own plane, over the span it actually draws — the same two facts the
      // arc branch above honours, and the same ones the renderer and the snapper read. The points
      // come from `EllipseWorldPointAt` so a fourth copy of the arithmetic cannot drift out of step.
      //
      // Measured against the CHORDS between the samples, not the samples themselves. Point sampling
      // reads a click that lands squarely on the curve but between two samples as up to half a
      // sample-step away — 0.8 world units on the 30-radius section oval — which at a tight pick
      // aperture is a miss on the very curve you are pointing at. This is the same segment metric
      // the polyline branch below uses, and it makes the answer independent of the sample count.
      constexpr int n = 36;
      ray3d::Vec3 prev = EllipseWorldPointAt(el, EllipseSpanAngleAt(el, 0.0));
      for (int i = 1; i <= n; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(n);
        const ray3d::Vec3 p = EllipseWorldPointAt(el, EllipseSpanAngleAt(el, u));
        bestD2 = std::min(bestD2, d2Segment(prev.x, prev.y, prev.z, p.x, p.y, p.z));
        prev = p;
      }
    }
    consider(e, bestD2);
  }
  // REQ-443: a spline is measured against the CHORDS of its tessellation, the metric the ellipse uses,
  // so a click that lands squarely on the curve between two samples still hits it.
  {
    std::vector<ray3d::Vec3> sp;
    for (size_t si = 0; si < st.drawing.cadSplines.size(); ++si) {
      spline::Tessellate(st.drawing.cadSplines[si], kSplineSampleTolerance, &sp);
      if (sp.size() < 2)
        continue;
      SelectedEntity e{};
      e.type = SelectedEntity::Type::Spline;
      e.index = static_cast<int>(si);
      double bestD2 = 1e300;
      for (size_t i = 1; i < sp.size(); ++i)
        bestD2 = std::min(bestD2, d2Segment(sp[i - 1].x, sp[i - 1].y, sp[i - 1].z, sp[i].x, sp[i].y, sp[i].z));
      consider(e, bestD2);
    }
  }
  // REQ-480: an alignment is measured against the chords of its display sampling, the metric the spline uses.
  for (size_t ai = 0; ai < st.drawing.alignments.size(); ++ai) {
    if (st.drawing.alignments[ai].Empty())
      continue;
    const std::vector<alignment::Point2> ap = st.drawing.alignments[ai].SampleForDisplay(0.01);
    if (ap.size() < 2)
      continue;
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Alignment;
    e.index = static_cast<int>(ai);
    double bestD2 = 1e300;
    for (size_t i = 1; i < ap.size(); ++i)
      bestD2 = std::min(bestD2, d2Segment(ap[i - 1].x, ap[i - 1].y, 0.0, ap[i].x, ap[i].y, 0.0));
    consider(e, bestD2);
  }
  for (size_t pi = 0; pi < st.drawing.cadPoints.size(); ++pi) {
    const CadPoint& p = st.drawing.cadPoints[pi];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Point;
    e.index = static_cast<int>(pi);
    consider(e, d2Segment(p.x, p.y, p.z, p.x, p.y, p.z));
  }
  const int nPoly =
      static_cast<int>(st.drawing.userPolylineOffsets.size() > 0 ? st.drawing.userPolylineOffsets.size() - 1 : 0);
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const bool closed =
        static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Polyline;
    e.index = pi;
    double bestD2 = 1e300;
    // REQ-316 / ADR-047: a curved segment is hit-tested against the ARC it draws, not its chord —
    // so a click or hover on the bulge registers. Arc samples reuse the ARC entity's own metric.
    auto polySegD2 = [&](int va, int vb) -> double {
      const size_t A = static_cast<size_t>(va), B = static_cast<size_t>(vb);
      const float bulge = static_cast<size_t>(va) < st.drawing.userPolylineVertsBulge.size()
                              ? st.drawing.userPolylineVertsBulge[static_cast<size_t>(va)] : 0.f;
      auto chordD2 = [&]() {
        return d2Segment(st.drawing.vertex(A).x, st.drawing.vertex(A).y, st.drawing.vertex(A).z,
                         st.drawing.vertex(B).x, st.drawing.vertex(B).y, st.drawing.vertex(B).z);
      };
      if (bulge == 0.f)
        return chordD2();
      float nx = 0.f, ny = 0.f, nz = 1.f;
      if (static_cast<size_t>(va) * 3 + 2 < st.drawing.userPolylineVertsNormal.size()) {
        nx = st.drawing.userPolylineVertsNormal[static_cast<size_t>(va) * 3];
        ny = st.drawing.userPolylineVertsNormal[static_cast<size_t>(va) * 3 + 1];
        nz = st.drawing.userPolylineVertsNormal[static_cast<size_t>(va) * 3 + 2];
      }
      if (IsFlatNormal(nx, ny, nz)) {
        const BulgeArcSpan arc = BulgeArc(st.drawing.vertex(A).x, st.drawing.vertex(A).y,
                                          st.drawing.vertex(B).x, st.drawing.vertex(B).y,
                                          static_cast<double>(bulge));
        if (!arc.valid)
          return chordD2();
        if (!useRay)
          return PointArcDistanceSq(wx, wy, arc);  // exact — the aperture means the same on a curve as a line
        double d2 = 1e300;
        constexpr int ns = 24;
        const double za = static_cast<double>(st.drawing.vertex(A).z);
        for (int s = 0; s <= ns; ++s) {
          const double u = arc.startAngle + arc.sweep * (static_cast<double>(s) / ns);
          d2 = std::min(d2, d2Point(arc.cx + arc.radius * std::cos(u), arc.cy + arc.radius * std::sin(u), za));
        }
        return d2;
      }
      // REQ-325 / ADR-053: a tilted segment's bulge is solved in ITS OWN plane's 2D coordinates —
      // same technique as the tilted render branch above — then sampled back to world 3D, so both
      // the plan-view flattened metric (d2Point with no ray) and the true ray-to-curve metric (with
      // one) see the real tilted shape rather than a flat approximation.
      const ray3d::Vec3 p0{st.drawing.vertex(A).x, st.drawing.vertex(A).y, st.drawing.vertex(A).z};
      const ray3d::Vec3 p1{st.drawing.vertex(B).x, st.drawing.vertex(B).y, st.drawing.vertex(B).z};
      ucs::Ucs plane{};
      if (!ucs::FromNormal(p0, ray3d::Vec3{static_cast<double>(nx), static_cast<double>(ny),
                                           static_cast<double>(nz)},
                           &plane))
        return chordD2();
      const ucs::Point2D p1Local = ucs::WorldToPlane(plane, p1);
      const BulgeArcSpan arc = BulgeArc(0.0, 0.0, p1Local.x, p1Local.y, static_cast<double>(bulge));
      if (!arc.valid)
        return chordD2();
      double d2 = 1e300;
      constexpr int ns = 24;
      for (int s = 0; s <= ns; ++s) {
        const double u = arc.startAngle + arc.sweep * (static_cast<double>(s) / ns);
        const ray3d::Vec3 wp = ucs::PlaneToWorld(
            plane, ucs::Point2D{arc.cx + arc.radius * std::cos(u), arc.cy + arc.radius * std::sin(u)});
        d2 = std::min(d2, d2Point(wp.x, wp.y, wp.z));
      }
      return d2;
    };
    for (int vi = v0; vi + 1 < v1; ++vi)
      bestD2 = std::min(bestD2, polySegD2(vi, vi + 1));
    if (closed && v1 - v0 >= 2)
      bestD2 = std::min(bestD2, polySegD2(v1 - 1, v0));
    consider(e, bestD2);
  }

  // Feature lines (REQ-087) — identical segment walk, through the same `consider` funnel, which is
  // what makes a feature line compete with every other entity on distance rather than being picked
  // by a separate rule. ADR-034 relies on this being the single funnel; so does this.
  {
    const int nFl =
        static_cast<int>(st.drawing.featureLineOffsets.size() > 0 ? st.drawing.featureLineOffsets.size() - 1 : 0);
    const auto& FV = st.drawing.featureLineVerts;
    for (int fi = 0; fi < nFl; ++fi) {
      const int v0 = st.drawing.featureLineOffsets[static_cast<size_t>(fi)];
      const int v1 = st.drawing.featureLineOffsets[(static_cast<size_t>(fi)+ 1)];
      const bool closed = static_cast<size_t>(fi) < st.drawing.featureLineClosed.size() &&
                          st.drawing.featureLineClosed[static_cast<size_t>(fi)];
      SelectedEntity e{};
      e.type = SelectedEntity::Type::FeatureLine;
      e.index = fi;
      double bestD2 = 1e300;
      for (int vi = v0; vi + 1 < v1; ++vi) {
        const size_t A = static_cast<size_t>(vi) * 3, B = (static_cast<size_t>(vi)+ 1) * 3;
        if (B + 2 >= FV.size())
          break;
        bestD2 = std::min(bestD2, d2Segment(FV[A], FV[A + 1], FV[A + 2], FV[B], FV[B + 1], FV[B + 2]));
      }
      if (closed && v1 - v0 >= 2) {
        const size_t A = (static_cast<size_t>(v1)- 1) * 3, B = static_cast<size_t>(v0) * 3;
        if (A + 2 < FV.size() && B + 2 < FV.size())
          bestD2 = std::min(bestD2, d2Segment(FV[A], FV[A + 1], FV[A + 2], FV[B], FV[B + 1], FV[B + 2]));
      }
      consider(e, bestD2);
    }
  }

  // TIN surfaces (REQ-068 / ADR-036 (b)) — a click anywhere near a triangle edge selects the WHOLE
  // surface, so this competes on distance through the same `consider` funnel as everything above.
  //
  // **Layer and isolation are filtered here, not in `consider`.** Every other kind reaches
  // `consider` and is gated there by `CadSelectedEntityHidden`; a surface is gated one level earlier
  // by `SurfaceVisible`, because that predicate also carries the layer rule the renderer applies to
  // surfaces (REQ-068) and which `consider` does not know about. Filtering the whole surface once is
  // also what keeps a hidden 200k-triangle surface from costing a full walk per frame.
  //
  // **The per-triangle plan-AABB reject is load-bearing, not a micro-optimisation.** Hover runs this
  // every frame, and REQ-100's surface profile is ~200k triangles = 600k edges. Four compares that
  // discard a triangle before three distance computations is the difference between a pick that fits
  // the frame budget and one that does not. Under a ray (an orbited camera) the plan AABB does not
  // bound the ray, so the reject is skipped rather than made approximately correct — a pick that
  // silently misses is worse than a slow one.
  for (size_t si = 0; si < st.drawing.cadSurfaces.size(); ++si) {
    if (!SurfaceVisible(st, si))
      continue;
    const CadTin& t = *st.drawing.cadSurfaces[si].tin;
    const std::vector<double>& V = t.vertsXyz;
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Surface;
    e.index = static_cast<int>(si);
    double bestD2 = 1e300;
    for (size_t i = 0; i + 2 < t.indices.size(); i += 3) {
      const size_t a = static_cast<size_t>(t.indices[i]) * 3;
      const size_t b = static_cast<size_t>(t.indices[i + 1]) * 3;
      const size_t c = static_cast<size_t>(t.indices[i + 2]) * 3;
      if (a + 2 >= V.size() || b + 2 >= V.size() || c + 2 >= V.size())
        break;
      if (!useRay) {
        const double lo_x = std::min(std::min(V[a], V[b]), V[c]);
        const double hi_x = std::max(std::max(V[a], V[b]), V[c]);
        const double lo_y = std::min(std::min(V[a + 1], V[b + 1]), V[c + 1]);
        const double hi_y = std::max(std::max(V[a + 1], V[b + 1]), V[c + 1]);
        if (wx < lo_x - tolWorld || wx > hi_x + tolWorld || wy < lo_y - tolWorld || wy > hi_y + tolWorld)
          continue;
      }
      bestD2 = std::min(bestD2, d2Segment(V[a], V[a + 1], V[a + 2], V[b], V[b + 1], V[b + 2]));
      bestD2 = std::min(bestD2, d2Segment(V[b], V[b + 1], V[b + 2], V[c], V[c + 1], V[c + 2]));
      bestD2 = std::min(bestD2, d2Segment(V[c], V[c + 1], V[c + 2], V[a], V[a + 1], V[a + 2]));
    }
    consider(e, bestD2);
  }

  // B-rep solids (REQ-313 / ADR-045) — a click near any EDGE selects the whole solid, which is the
  // same "click a visible component, get the object" rule surfaces use above.
  //
  // Edges and not triangles, deliberately. A solid's edges are what is drawn in every visual style,
  // so picking them is picking what the user can actually see — and in 2D Wireframe, the default,
  // the edges are the ONLY thing on screen. Picking against the tessellation instead would make a
  // solid clickable across its whole silhouette in Shaded and nowhere at all in wireframe.
  //
  // Layer and isolation are filtered here rather than in `consider`, for the reason the surface loop
  // above gives: `SolidVisible` carries the layer rule the renderer applies, which `consider` does
  // not know about.
  for (size_t soi = 0; soi < st.drawing.cadSolids.size(); ++soi) {
    if (!SolidVisible(st, soi))
      continue;
    const CadSolidPtr& sp = st.drawing.cadSolids[soi];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Solid;
    e.index = static_cast<int>(soi);
    double bestD2 = 1e300;
    for (const brep::Edge& ed : sp->edges) {
      // A line is one segment; an arc is walked in a fixed number of chords. Fixed and not
      // tolerance-derived because this runs on hover: a solid's edge count is tens, not the 600k a
      // surface can reach, so the cost is nowhere near the frame budget and a constant keeps the
      // pick distance stable as the user zooms (REQ-100).
      const int steps = ed.kind == brep::CurveKind::Arc ? 24 : 1;
      ray3d::Vec3 prev = brep::EdgePointAt(*sp, ed, 0.0);
      for (int i = 1; i <= steps; ++i) {
        const ray3d::Vec3 next = brep::EdgePointAt(*sp, ed, static_cast<double>(i) / steps);
        bestD2 = std::min(bestD2, d2Segment(static_cast<float>(prev.x), static_cast<float>(prev.y),
                                            static_cast<float>(prev.z), static_cast<float>(next.x),
                                            static_cast<float>(next.y), static_cast<float>(next.z)));
        prev = next;
      }
    }
    consider(e, bestD2);
  }

  // Point clouds (REQ-171 part 14) — walk the bounded PREVIEW sample only (`pc->pointsXyz`, capped
  // at `kPointCloudPreviewCap`), never the full out-of-core `.gscloud` cache. Hover runs this every
  // frame; a disk read per hover frame is exactly what the LOD renderer's reselect hysteresis
  // (TASK-270 §10h/10j) was built to avoid, so picking must not reintroduce it. Same precedent as
  // the extents/zoom-extents walks, which also only look at resident points.
  for (size_t pci = 0; pci < st.drawing.cadPointClouds.size(); ++pci) {
    if (!PointCloudVisible(st, pci))
      continue;
    const std::shared_ptr<const CadPointCloud>& pc = st.drawing.cadPointClouds[pci];
    if (!pc)
      continue;
    const std::vector<double>& P = pc->pointsXyz;
    if (P.size() < 3)
      continue;
    if (!useRay) {
      double lo_x = P[0], hi_x = P[0], lo_y = P[1], hi_y = P[1];
      for (size_t i = 0; i + 2 < P.size(); i += 3) {
        lo_x = std::min(lo_x, static_cast<double>(P[i]));
        hi_x = std::max(hi_x, static_cast<double>(P[i]));
        lo_y = std::min(lo_y, static_cast<double>(P[i + 1]));
        hi_y = std::max(hi_y, static_cast<double>(P[i + 1]));
      }
      if (wx < lo_x - tolWorld || wx > hi_x + tolWorld || wy < lo_y - tolWorld || wy > hi_y + tolWorld)
        continue;
    }
    SelectedEntity e{};
    e.type = SelectedEntity::Type::PointCloud;
    e.index = static_cast<int>(pci);
    double bestD2 = 1e300;
    for (size_t i = 0; i + 2 < P.size(); i += 3)
      bestD2 = std::min(bestD2, d2Point(P[i], P[i + 1], P[i + 2]));
    consider(e, bestD2);
  }

  for (size_t ti = 0; ti < st.drawing.cadTables.size(); ++ti) {
    const CadTable& t = st.drawing.cadTables[ti];
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Table;
    e.index = static_cast<int>(ti);
    float lx = 0.f, ly = 0.f;
    CadTableWorldToLocal(t, static_cast<float>(wx), static_cast<float>(wy), &lx, &ly);
    const float tw = std::max(t.width, 1.e-3f);
    const float th = std::max(t.height, 1.e-3f);
    double td2 = 1e300;
    if (lx >= 0.f && lx <= tw && ly >= 0.f && ly <= th) {
      td2 = 0.0;
    } else {
      for (int i = 0; i < 4; ++i) {
        float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
        CadTableWorldCorner(t, i, &ax, &ay);
        CadTableWorldCorner(t, (i + 1) % 4, &bx, &by);
        td2 = std::min(td2, d2Segment(ax, ay, t.insZ, bx, by, t.insZ));
      }
    }
    consider(e, td2);
  }

  // Position Markers (REQ-359): the circle or the label box picks the one object. Plan-view test,
  // like the table above (the marker is flat on its elevation).
  for (size_t mi = 0; mi < st.drawing.cadPositionMarkers.size(); ++mi) {
    double md2 = 0.0;
    if (!CadPositionMarkerHit(st, mi, wx, wy, tolWorld, &md2))
      continue;
    SelectedEntity e{};
    e.type = SelectedEntity::Type::PositionMarker;
    e.index = static_cast<int>(mi);
    consider(e, md2);
  }

  for (size_t li = 0; li < st.drawing.cadMultileaders.size(); ++li) {
    double ld2 = 0.0;
    if (!CadMultileaderHit(st.drawing.cadMultileaders[li], wx, wy, tolWorld, &ld2))
      continue;
    SelectedEntity e{};
    e.type = SelectedEntity::Type::Multileader;
    e.index = static_cast<int>(li);
    consider(e, ld2);
  }

  for (size_t bi = 0; bi < st.drawing.cadBlockRefs.size(); ++bi) {
    SelectedEntity e{};
    e.type = SelectedEntity::Type::BlockRef;
    e.index = static_cast<int>(bi);
    std::vector<CadBlockWorldSeg> segs;
    EntityAttributes dummy{};
    if (static_cast<size_t>(bi) < st.drawing.cadBlockRefAttrs.size())
      dummy = st.drawing.cadBlockRefAttrs[bi];
    CadBlockCollectWorldLines(st.drawing.blockDefs, st.drawing.cadBlockRefs[bi], dummy, &segs);
    double bd2 = d2Segment(st.drawing.cadBlockRefs[bi].xf.x, st.drawing.cadBlockRefs[bi].xf.y, st.drawing.cadBlockRefs[bi].xf.z,
                           st.drawing.cadBlockRefs[bi].xf.x, st.drawing.cadBlockRefs[bi].xf.y, st.drawing.cadBlockRefs[bi].xf.z);
    for (const CadBlockWorldSeg& s : segs)
      bd2 = std::min(bd2, d2Segment(s.x0, s.y0, s.z0, s.x1, s.y1, s.z1));
    // A fitting block whose content is entirely a 3D solid (a piping part, not a 2D symbol) had NO
    // line segments to test above, so `bd2` fell back to the single insertion-origin point and the
    // block was effectively unclickable across its whole visible body (issue #496 follow-up). Walk
    // its solids' edges the same way the standalone-CadSolid loop above does.
    std::vector<CadBlockWorldSolid> blockSolids;
    CadBlockCollectWorldSolids(st.drawing.blockDefs, st.drawing.cadBlockRefs[bi], dummy, &blockSolids);
    for (const CadBlockWorldSolid& ws : blockSolids) {
      if (!ws.solid)
        continue;
      for (const brep::Edge& ed : ws.solid->edges) {
        const int steps = ed.kind == brep::CurveKind::Arc ? 24 : 1;
        ray3d::Vec3 prev = brep::EdgePointAt(*ws.solid, ed, 0.0);
        for (int i = 1; i <= steps; ++i) {
          const ray3d::Vec3 next = brep::EdgePointAt(*ws.solid, ed, static_cast<double>(i) / steps);
          bd2 = std::min(bd2, d2Segment(static_cast<float>(prev.x), static_cast<float>(prev.y),
                                        static_cast<float>(prev.z), static_cast<float>(next.x),
                                        static_cast<float>(next.y), static_cast<float>(next.z)));
          prev = next;
        }
      }
    }
    consider(e, bd2);
  }

  if (!any)
    return false;
  if (allCandidates) {
    for (CadPickCandidate& c : *allCandidates)
      c.depthKey = CadEntityPickDepthAtPick(st, c.entity, wx, wy, pickRay);
  }
  *out = bestE;
  *outDistSq = static_cast<float>(best);
  return true;
}

bool CadFilledRegionContainsPoint(const CadFilledRegion& fr, double x, double y) {
  return hatchgeom::ContainsPoint(fr, x, y);
}

int PickFilledRegionAt(const AppCommandState& st, double wxIn, double wyIn, const ray3d::Ray* ray) {
  int best = -1;
  double bestArea = 0.0;
  for (size_t i = 0; i < st.drawing.cadFilledRegions.size(); ++i) {
    // REQ-084 (d): an isolated-out fill is invisible and must not answer a click.
    if (!st.hiddenEntityIds.empty() && i < st.drawing.cadFilledRegionAttrs.size() &&
        CadEntityIdHidden(&st.hiddenEntityIds, st.drawing.cadFilledRegionAttrs[i].id))
      continue;
    double wx = wxIn;
    double wy = wyIn;
    if (ray) {
      // Orbited (issue #564 §2): where the ray meets this region's own plane.
      const auto& v = st.drawing.cadFilledRegions[i].vertsXyz;
      float fx = 0.f, fy = 0.f;
      if (v.size() < 3 || !RayAtElevation(*ray, static_cast<double>(v[2]), &fx, &fy))
        continue;
      wx = fx;
      wy = fy;
    }
    if (!hatchgeom::ContainsPoint(st.drawing.cadFilledRegions[i], wx, wy))
      continue;
    const double area = hatchgeom::OuterAreaAbs(st.drawing.cadFilledRegions[i]);
    if (best < 0 || area < bestArea) {
      best = static_cast<int>(i);
      bestArea = area;
    }
  }
  return best;
}


static int PickPdfUnderlayAt(const AppCommandState& st, double localX, double localY) {
  constexpr float kPdfPi = 3.14159265f;
  for (int pi = static_cast<int>(st.drawing.pdfAttachments.size()) - 1; pi >= 0; --pi) {
    const PdfAttachment& att = st.drawing.pdfAttachments[static_cast<std::size_t>(pi)];
    if (att.pageWidthPts <= 0.f || att.pageHeightPts <= 0.f)
      continue;
    const float W = att.pageWidthPts * att.scale;
    const float H = att.pageHeightPts * att.scale;
    const float cosR = std::cos(att.rotationDeg * kPdfPi / 180.f);
    const float sinR = std::sin(att.rotationDeg * kPdfPi / 180.f);
    const float dx = static_cast<float>(localX) - att.insertX;
    const float dy = static_cast<float>(localY) - att.insertY;
    const float px = dx * cosR + dy * sinR;
    const float py = -dx * sinR + dy * cosR;
    if (px >= 0.f && py >= 0.f && px <= W && py <= H)
      return pi;
  }
  return -1;
}

ViewportPickResult ResolveViewportPick(const AppCommandState& st, const ViewportPickRequest& rq) {
  ViewportPickResult r;
  const ray3d::Vec3 eo = rq.eyeRay.origin;
  const ray3d::Vec3 ed = rq.eyeRay.dir;
  const double edLen2 = ray3d::Dot(ed, ed);
  const double edLen = std::sqrt(edLen2);

  // 1. The solid under the eye ray, and how far along it (issue #564 §2). Solids yield to a survey
  //    point, as they always have.
  bool haveSolid = false;
  SelectedEntity solidE{};
  double tSolid = 0.0;
  float solidTol = 0.f;  // computed once: it sweeps the drawing's extents
  if (rq.modelSpace && rq.eyeRayValid && edLen > 1e-12 && !rq.surveyPointUnderCursor &&
      (!st.drawing.cadSolids.empty() || !st.pipeRunWorldSolids.empty())) {
    solidTol = CadOffsetEntityPickTolWorld(st);
    haveSolid = PickClosestSolidEntity(st, rq.eyeRay, solidTol, &solidE, &tSolid);
  }
  // Only a DRAWN surface hides what is behind it: in 2D Wireframe a solid is see-through (Q1).
  const bool opaque = haveSolid && st.viewportVisualStyle != VisualStyle::Wireframe2D;
  // "On the surface" counts as in front: a line drawn on a face, or the face's own edge, must still
  // win over the face. The pick tolerance is that allowance, converted to ray-parameter units.
  const double depthTolT = haveSolid ? static_cast<double>(std::max(rq.lineTol, solidTol)) / edLen : 0.0;
  const auto tAtZ = [&](double z, double* t) {
    if (std::fabs(ed.z) < 1e-12)
      return false;
    *t = (z - eo.z) / ed.z;
    return true;
  };
  const auto hiddenAtT = [&](double t) { return opaque && t > tSolid + depthTolT; };
  const auto hiddenAtZ = [&](double z) {
    double t = 0.0;
    return opaque && tAtZ(z, &t) && hiddenAtT(t);
  };
  // A linework candidate's depth on the eye ray. Orbited, its depthKey IS the parameter on this same
  // ray (the caller passes one ray as both); in plan it is the entity's Z at the closest point.
  const auto candT = [&](const CadPickCandidate& c, double* t) {
    if (rq.orbitRay) {
      *t = c.depthKey;
      return true;
    }
    return tAtZ(c.depthKey, t);
  };

  // 2. Tables, then text — their long-standing precedence over linework, unless an opaque solid is
  //    in front of them. Tables carry no elevation of their own and are drawn at Z = 0.
  if (rq.modelSpace) {
    const int tbl = PickCadTableAt(static_cast<float>(rq.rawX), static_cast<float>(rq.rawY), st, rq.orthoHalfH,
                                   rq.viewportHeightPx);
    if (tbl >= 0 && !hiddenAtZ(0.0)) {
      r.family = ViewportPickFamily::Table;
      r.entity.type = SelectedEntity::Type::Table;
      r.entity.index = tbl;
      return r;
    }
    const int ann = PickCadAnnotationAt(static_cast<float>(rq.rawX), static_cast<float>(rq.rawY), st,
                                        rq.orthoHalfH, rq.viewportHeightPx, rq.orbitRay);
    if (ann >= 0 && !hiddenAtZ(static_cast<double>(st.drawing.cadAnnotations[static_cast<size_t>(ann)].insZ))) {
      r.family = ViewportPickFamily::Annotation;
      r.entity.type = SelectedEntity::Type::Annotation;
      r.entity.index = ann;
      return r;
    }
  }

  // 3. Linework. With no solid under the cursor this is the pre-change call, answer and candidate
  //    list untouched.
  SelectedEntity hit{};
  float d2 = 0.f;
  std::vector<CadPickCandidate> cands;
  if (PickClosestCadEntity(st, rq.rawX, rq.rawY, rq.lineTol, &hit, &d2, rq.orbitRay, &cands)) {
    // Drop what an opaque solid's surface hides (none without a solid, or in 2D Wireframe).
    std::vector<CadPickCandidate> visible;
    for (const CadPickCandidate& c : cands) {
      double t = 0.0;
      if (!candT(c, &t) || !hiddenAtT(t))
        visible.push_back(c);
    }
    // The winner among what is visible: NEAREST THE EYE (the ray parameter orbited, the highest Z in
    // plan — `CadPickCandidate::depthKey`), and on a tie the nearest to the cursor. The tolerance
    // decides what is a candidate, not which one wins (issue #564 §2; the tie rule decided with the
    // user 2026-09-28, D-2026-09-28-e). Before this, the hover took the nearest-to-cursor and the
    // default click the highest, first-DRAWN on a tie — so in a flat drawing the two disagreed.
    bool lineOk = cands.empty();  // no candidate list to judge by: keep PickClosestCadEntity's answer
    double tLine = -std::numeric_limits<double>::infinity();
    if (!visible.empty()) {
      const auto nearer = [&](const CadPickCandidate& a, const CadPickCandidate& b) {
        const double da = rq.orbitRay ? a.depthKey : -a.depthKey;  // smaller = nearer the eye
        const double db = rq.orbitRay ? b.depthKey : -b.depthKey;
        if (da < db - 1e-9)
          return true;
        if (db < da - 1e-9)
          return false;
        return a.distSq < b.distSq;
      };
      auto it = visible.begin();
      for (auto k = visible.begin(); k != visible.end(); ++k)
        if (nearer(*k, *it))
          it = k;
      hit = it->entity;
      lineOk = true;
      double t = 0.0;
      if (candT(*it, &t))
        tLine = t;
    }
    if (!haveSolid && lineOk) {
      r.family = ViewportPickFamily::Linework;
      r.entity = hit;
      r.candidates = std::move(visible);
      return r;
    }
    // Nearer the eye wins; a line ON the surface (within tolerance) counts as in front of it.
    if (lineOk && tLine <= tSolid + depthTolT) {
      r.family = ViewportPickFamily::Linework;
      r.entity = hit;
      r.candidates = std::move(visible);
      return r;
    }
  }

  // 4. The solid.
  if (haveSolid) {
    r.family = ViewportPickFamily::Solid;
    r.entity = solidE;
    return r;
  }

  // 5. PDF underlays (behind linework; only when nothing closer was hit).
  if (rq.modelSpace) {
    const int pdf = PickPdfUnderlayAt(st, rq.rawX, rq.rawY);
    if (pdf >= 0 && !hiddenAtZ(0.0)) {
      r.family = ViewportPickFamily::PdfUnderlay;
      r.entity.type = SelectedEntity::Type::PdfUnderlay;
      r.entity.index = pdf;
      return r;
    }
  }

  // 6. Filled regions, lowest (REQ-042).
  const int fr = PickFilledRegionAt(st, rq.rawX, rq.rawY, rq.orbitRay);
  if (fr >= 0) {
    r.family = ViewportPickFamily::FilledRegion;
    r.entity.type = SelectedEntity::Type::FilledRegion;
    r.entity.index = fr;
  }
  return r;
}

ViewportPickResult ResolveViewportClickPick(const AppCommandState& st, const ViewportPickRequest& rq,
                                            float hoverLineTol) {
  ViewportPickRequest tight = rq;
  tight.lineTol = hoverLineTol;
  ViewportPickResult near = ResolveViewportPick(st, tight);
  ViewportPickResult wide = ResolveViewportPick(st, rq);
  if (near.family == ViewportPickFamily::None)
    return wide;
  if (near.family == ViewportPickFamily::Linework && wide.family == ViewportPickFamily::Linework)
    near.candidates = std::move(wide.candidates);
  return near;
}


void BeginSelectionBoxCorner(AppCommandState& st, float wx, float wy, float anchorScreenX,
                             float anchorScreenY) {
  AbortMtextGripInteraction(st);
  ClearDimGripInteraction(st);
  st.selBoxAnchorX = wx;
  st.selBoxAnchorY = wy;
  // The corner's ELEVATION, taken from the cursor's published work-plane Z rather than threaded
  // through all five call sites — the same seam `resolvedPointZ` uses, and for the same reason: a
  // parameter added to five signatures is a parameter one of them eventually forgets to pass.
  //
  // Zero here is what put the fence on the wrong pixels under a tilted UCS: a fence is projected to
  // screen when the view is orbited, and a projection needs all three coordinates.
  st.selBoxAnchorZ = st.uiCursorWorldZ;
  st.selBoxAnchorScreenX = anchorScreenX;
  st.selBoxAnchorScreenY = anchorScreenY;
  st.selBoxWaitingSecond = true;
}

void UpdateSelectionBoxPreview(AppCommandState& st, float wx, float wy, bool windowMode, const Camera* cam,
                               float vpW, float vpH) {
  if (!st.selBoxWaitingSecond) {
    st.selBoxPreview.clear();
    st.selBoxPreviewKeyValid = false;
    return;
  }
  const std::array<double, 11> key = {st.selBoxAnchorX,
                                      st.selBoxAnchorY,
                                      st.selBoxAnchorZ,
                                      wx,
                                      wy,
                                      st.uiCursorWorldZ,
                                      windowMode ? 1.0 : 0.0,
                                      st.viewportPanX + st.viewportPanY,
                                      st.viewportZoom,
                                      static_cast<double>(st.cadGpuRevision),
                                      static_cast<double>(st.hiddenEntityIds.size())};
  if (st.selBoxPreviewKeyValid && key == st.selBoxPreviewKey)
    return;
  st.selBoxPreviewKey = key;
  st.selBoxPreviewKeyValid = true;

  // ComputeSelectionFromRect merges into st.selection / selectedSurveyPointIndices, so run it on an
  // empty pair and put the real ones back — the preview must never change the selection.
  std::vector<SelectedEntity> savedSel;
  savedSel.swap(st.selection);
  std::vector<int> savedSurvey;
  savedSurvey.swap(st.selectedSurveyPointIndices);
  ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, st.selBoxAnchorZ, wx, wy, st.uiCursorWorldZ,
                           /*subtract=*/false, windowMode, /*includeSurveyPoints=*/false, cam, vpW, vpH);
  st.selBoxPreview.swap(st.selection);
  st.selection.swap(savedSel);
  st.selectedSurveyPointIndices.swap(savedSurvey);
}


static void ToggleAccumulateSelectedEntity(AppCommandState& st, const SelectedEntity& hit, bool shift) {
  auto it = std::find_if(st.selection.begin(), st.selection.end(), [&](const SelectedEntity& entity) {
    return entity.type == hit.type && entity.index == hit.index;
  });
  if (shift) {
    if (it != st.selection.end())
      st.selection.erase(it);
  } else if (it == st.selection.end()) {
    st.selection.push_back(hit);
  }
  EnsureAttrCounts(st);
}

bool SubmitViewportSelectionAccumulateClick(AppCommandState& st, double rawLocalX, double rawLocalY,
                                            float anchorScreenX, float anchorScreenY, bool shiftModifier,
                                            bool ctrlModifier, const ray3d::Ray* pickRay,
                                            float viewportWidthPx, float viewportHeightPx, float orthoHalfH,
                                            std::vector<std::string>& log, bool beginBoxOnMiss) {
  using K = AppCommandState::Kind;
  const bool modelSpace = st.activeSpaceIndex == kModelSpaceIndex;
  const bool gizmoMoveSubObject =
      st.active == K::Move3d && st.gizmoCmdPhase == AppCommandState::GizmoCmdPhase::SelectObjects;
  if (modelSpace && ctrlModifier &&
      ((st.active == K::PressPull &&
        st.pressPullPhase == AppCommandState::PressPullPhase::SelectTarget) ||
       gizmoMoveSubObject)) {
    AbortMtextGripInteraction(st);
    ClearDimGripInteraction(st);
    solidpick::Tolerance subTol;
    subTol.vertex = static_cast<double>(CadOffsetEntityPickTolWorld(st));
    subTol.edge = subTol.vertex;
    const Camera cam = CadViewCamera(st);
    const ray3d::Ray subRay = pickRay ? *pickRay
                                      : cam.ScreenRay(anchorScreenX, anchorScreenY, viewportWidthPx,
                                                      viewportHeightPx);
    SubmitSubObjectPick(st, subRay, subTol, shiftModifier, log);
    BumpCadGpuCache(st);
    return true;
  }

  const bool solidClickable = modelSpace && (!st.drawing.cadSolids.empty() || !st.pipeRunWorldSolids.empty());
  ViewportPickRequest rq;
  rq.rawX = rawLocalX;
  rq.rawY = rawLocalY;
  rq.orbitRay = CadViewIsPlan(st) ? nullptr : pickRay;
  rq.eyeRayValid = solidClickable;
  if (solidClickable) {
    if (pickRay)
      rq.eyeRay = *pickRay;
    else {
      const Camera cam = CadViewCamera(st);
      rq.eyeRay = cam.ScreenRay(anchorScreenX, anchorScreenY, viewportWidthPx, viewportHeightPx);
    }
  }
  rq.lineTol = CadOffsetEntityPickTolWorld(st);
  rq.orthoHalfH = orthoHalfH;
  rq.viewportHeightPx = viewportHeightPx;
  rq.modelSpace = modelSpace;
  const ViewportPickResult accPick = ResolveViewportClickPick(st, rq, CadHoverEntityPickTolWorld(st));

  bool handled = false;
  const auto toggleHit = [&](const SelectedEntity& entity) {
    ToggleAccumulateSelectedEntity(st, entity, shiftModifier);
    handled = true;
  };
  if (accPick.family == ViewportPickFamily::Table && accPick.entity.index >= 0)
    toggleHit(accPick.entity);
  else if (accPick.family == ViewportPickFamily::Annotation && accPick.entity.index >= 0)
    toggleHit(accPick.entity);
  else if (accPick.family == ViewportPickFamily::Linework)
    toggleHit(accPick.entity);
  else if (accPick.family == ViewportPickFamily::Solid)
    toggleHit(accPick.entity);
  else if (accPick.family == ViewportPickFamily::FilledRegion && accPick.entity.index >= 0)
    toggleHit(accPick.entity);
  else if (accPick.family == ViewportPickFamily::PdfUnderlay && accPick.entity.index >= 0)
    toggleHit(accPick.entity);

  if (handled) {
    if (!shiftModifier)
      CadPropCommandSelectionChanged(st, log);
    return true;
  }
  if (!beginBoxOnMiss)
    return false;
  BeginSelectionBoxCorner(st, static_cast<float>(rawLocalX), static_cast<float>(rawLocalY), anchorScreenX,
                          anchorScreenY);
  return false;
}

// REQ-101 / decision D-2026-08-17-b: establish the document origin BEFORE a typed coordinate of
// state-plane magnitude is narrowed into the float local frame.
//
// Why here, and not at the commit sites: this is the single place every typed coordinate passes
// through before any command interprets it, so one call covers LINE, POLYLINE, RECT, CIRCLE and every
// other command with no signature change and no `const` removed from the 29 ParseStoragePoint callers.
// A "parse" function that silently rebased the whole drawing would also be the wrong owner for a
// document-wide mutation.
//
// Why it fires at most once per drawing: MaybeRebaseLargeCoordinates' own guard is
// `worldDocumentOrigin != (0,0) -> return false`, so once a frame is established this is inert. That
// matters more than it looks — re-centring the origin as the drawing grew would round every stored
// coordinate through float again on each move, which is exactly the compounding drift REQ-079's
// idempotence condition forbids. One establishment, never a moving target.
