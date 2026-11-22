#include "CadCommandsInternal.hpp"
#include "modify/CadOffsetInternal.hpp"
#include "CadCoordinateFrame.hpp"
#include <GoSurvey/Geometry/geom2d.hpp>
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

void StartOffsetCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  if (st.active != K::None) {
    log.push_back("OFFSET — finish or cancel the active command first.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  OffsetCmd::ResetOffsetDraft(st);
  st.active = K::Offset;
  st.selBoxWaitingSecond = false;
  log.push_back("OFFSET — specify offset distance, or T for through-point mode. ESC cancels.");
}


namespace {

static void OfsClosestPtSeg(float ax, float ay, float bx, float by, float px, float py, float* qx, float* qy) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1e-18f) {
    *qx = ax;
    *qy = ay;
    return;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  *qx = ax + t * vx;
  *qy = ay + t * vy;
}

static void OfsUnitLeftNormal(float ax, float ay, float bx, float by, float* nx, float* ny) {
  float vx = bx - ax;
  float vy = by - ay;
  const float len = std::hypot(vx, vy);
  if (len < 1e-12f) {
    *nx = 0.f;
    *ny = 1.f;
    return;
  }
  vx /= len;
  vy /= len;
  *nx = -vy;
  *ny = vx;
}

static float OfsSignedSideLine(float ax, float ay, float bx, float by, float px, float py) {
  float qx = 0.f, qy = 0.f;
  OfsClosestPtSeg(ax, ay, bx, by, px, py, &qx, &qy);
  float nx = 0.f, ny = 0.f;
  OfsUnitLeftNormal(ax, ay, bx, by, &nx, &ny);
  return (px - qx) * nx + (py - qy) * ny;
}

static float OfsSignedSideCircle(float cx, float cy, float r, float px, float py) {
  const float d = std::hypot(px - cx, py - cy);
  return d - r;
}

static bool OfsLineLineIntersectInf(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy,
                                    float* ox, float* oy) {
  const float rx = bx - ax, ry = by - ay;
  const float sx = dx - cx, sy = dy - cy;
  const float det = rx * sy - ry * sx;
  if (std::fabs(det) < 1e-12f * std::max(1.f, std::hypot(rx, ry) * std::hypot(sx, sy)))
    return false;
  const float t = ((cx - ax) * sy - (cy - ay) * sx) / det;
  *ox = ax + t * rx;
  *oy = ay + t * ry;
  return true;
}

static bool TryOffsetSignedDFromCursor(const AppCommandState& st, float px, float py, float* signedDOut) {
  using OP = AppCommandState::OffsetPhase;
  using T = SelectedEntity::Type;
  if (!st.offsetEntityValid)
    return false;
  const SelectedEntity& e = st.offsetEntity;
  if (st.offsetPhase == OP::WaitSidePick) {
    if (st.offsetTypedDistance <= 0.f)
      return false;
    const float d = st.offsetTypedDistance;
    float sgn = 1.f;
    switch (e.type) {
    case T::LineSeg: {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.lineCount())
        return false;
      const float sd = OfsSignedSideLine(static_cast<float>(st.drawing.line(k).x0), static_cast<float>(st.drawing.line(k).y0), static_cast<float>(st.drawing.line(k).x1),
                                         static_cast<float>(st.drawing.line(k).y1), px, py);
      sgn = sd >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Circle: {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci >= st.drawing.circleCount())
        return false;
      const CadCircleRecord cr = st.drawing.circle(ci);
      const float cx = static_cast<float>(cr.cx);
      const float cy = static_cast<float>(cr.cy);
      const float r = static_cast<float>(cr.r);
      const float side = OfsSignedSideCircle(cx, cy, r, px, py);
      sgn = side >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Arc: {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
        return false;
      const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
      const float side = OfsSignedSideCircle(static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.r), px, py);
      sgn = side >= 0.f ? 1.f : -1.f;
      break;
    }
    case T::Ellipse:
    case T::Polyline: {
      if (e.type == T::Polyline) {
        const int pi = e.index;
        if (pi >= 0 && (static_cast<size_t>(pi)+ 1) < st.drawing.userPolylineOffsets.size()) {
          const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
          const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
          float best = 1e30f;
          float bestS = 1.f;
          for (int vi = v0; vi + 1 < v1; ++vi) {
            const float ax = static_cast<float>(st.drawing.vertex(vi).x);
            const float ay = static_cast<float>(st.drawing.vertex(vi).y);
            const float bx = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).x);
            const float by = static_cast<float>(st.drawing.vertex(static_cast<size_t>(vi + 1)).y);
            float qx = 0.f, qy = 0.f;
            OfsClosestPtSeg(ax, ay, bx, by, px, py, &qx, &qy);
            const float sd = OfsSignedSideLine(ax, ay, bx, by, px, py);
            const float dx = px - qx;
            const float dy = py - qy;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < best) {
              best = dist2;
              bestS = sd >= 0.f ? 1.f : -1.f;
            }
          }
          sgn = bestS;
        }
      } else if (e.index >= 0 && static_cast<size_t>(e.index) < st.drawing.userEllipses.size()) {
        const CadEllipse& el = st.drawing.userEllipses[static_cast<size_t>(e.index)];
        const float ma = std::hypot(el.majVx, el.majVy);
        if (ma >= 1e-8f) {
          const float ux = el.majVx / ma;
          const float uy = el.majVy / ma;
          const float pxn = -uy;
          const float pyn = ux;
          const float mb = ma * el.ratio;
          constexpr float twopi = 6.28318530718f;
          float best = 1e30f;
          float bx = static_cast<float>(el.cx), by = static_cast<float>(el.cy);
          for (int i = 0; i <= 48; ++i) {
            const float ang = twopi * static_cast<float>(i) / 48.f;
            const float c0 = std::cos(ang);
            const float s0 = std::sin(ang);
            const float ex = static_cast<float>(el.cx + ux * (ma * c0) + pxn * (mb * s0));
            const float ey = static_cast<float>(el.cy + uy * (ma * c0) + pyn * (mb * s0));
            const float dx = px - ex;
            const float dy = py - ey;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 < best) {
              best = dist2;
              bx = ex;
              by = ey;
            }
          }
          const float ox = static_cast<float>(bx - el.cx);
          const float oy = static_cast<float>(by - el.cy);
          const float inX = static_cast<float>(px - el.cx);
          const float inY = static_cast<float>(py - el.cy);
          sgn = (inX * ox + inY * oy) >= 0.f ? 1.f : -1.f;
        }
      }
      break;
    }
    default:
      return false;
    }
    *signedDOut = d * sgn;
    return true;
  }
  if (st.offsetPhase == OP::WaitThroughPick) {
    float signedD = 0.f;
    switch (e.type) {
    case T::LineSeg: {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.lineCount())
        return false;
      signedD = OfsSignedSideLine(static_cast<float>(st.drawing.line(k).x0), static_cast<float>(st.drawing.line(k).y0), static_cast<float>(st.drawing.line(k).x1),
                                  static_cast<float>(st.drawing.line(k).y1), px, py);
      break;
    }
    case T::Circle: {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci >= st.drawing.circleCount())
        return false;
      const CadCircleRecord cr = st.drawing.circle(ci);
      signedD = OfsSignedSideCircle(static_cast<float>(cr.cx), static_cast<float>(cr.cy), static_cast<float>(cr.r), px,
                                  py);
      break;
    }
    case T::Arc: {
      if (e.index < 0 || static_cast<size_t>(e.index) >= st.drawing.userArcs.size())
        return false;
      const CadArc& a = st.drawing.userArcs[static_cast<size_t>(e.index)];
      signedD = OfsSignedSideCircle(static_cast<float>(a.cx), static_cast<float>(a.cy), static_cast<float>(a.r), px, py);
      break;
    }
    default:
      return false;
    }
    if (std::fabs(signedD) < 1e-7f)
      return false;
    *signedDOut = signedD;
    return true;
  }
  return false;
}

static void AppendArcPreviewStrip(float z, const CadArc& a, std::vector<float>* lines) {
  AppendArcLineSegments(*lines, static_cast<double>(a.cx), static_cast<double>(a.cy), static_cast<double>(a.r),
                        static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), 56, z);
}

static void AppendEllipsePreviewStrip(float z, const CadEllipse& el, std::vector<float>* lines) {
  AppendEllipseLineSegments(*lines, static_cast<double>(el.cx), static_cast<double>(el.cy),
                            static_cast<double>(el.majVx), static_cast<double>(el.majVy),
                            static_cast<double>(el.ratio), 56, z);
}

static void AppendPolylineOffsetPreview(const AppCommandState& st, int pi, float signedD, float z,
                                        std::vector<float>* lines) {
  if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
    return;
  const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
  const int nv = v1 - v0;
  if (nv < 2)
    return;
  const bool closed =
      static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size() && st.drawing.userPolylineClosed[static_cast<size_t>(pi)];

  std::vector<std::pair<float, float>> v;
  v.reserve(static_cast<size_t>(nv));
  for (int i = v0; i < v1; ++i)
    v.push_back({st.drawing.vertex(i).x, st.drawing.vertex(i).y});

  const int n = static_cast<int>(v.size());
  const int nEdges = closed ? n : n - 1;
  if (nEdges < 1)
    return;

  std::vector<std::pair<float, float>> pa(static_cast<size_t>(nEdges)), pb(static_cast<size_t>(nEdges));
  for (int ei = 0; ei < nEdges; ++ei) {
    const int ia = ei;
    const int ib = closed ? (ei + 1) % n : ei + 1;
    const float ax = v[static_cast<size_t>(ia)].first;
    const float ay = v[static_cast<size_t>(ia)].second;
    const float bx = v[static_cast<size_t>(ib)].first;
    const float by = v[static_cast<size_t>(ib)].second;
    float nx = 0.f, ny = 0.f;
    OfsUnitLeftNormal(ax, ay, bx, by, &nx, &ny);
    pa[static_cast<size_t>(ei)] = {ax + nx * signedD, ay + ny * signedD};
    pb[static_cast<size_t>(ei)] = {bx + nx * signedD, by + ny * signedD};
  }

  std::vector<std::pair<float, float>> out;
  if (!closed) {
    if (nEdges == 1) {
      out.push_back(pa[0]);
      out.push_back(pb[0]);
    } else {
      out.push_back(pa[0]);
      for (int ei = 0; ei < nEdges - 1; ++ei) {
        const auto& a0 = pa[static_cast<size_t>(ei)];
        const auto& b0 = pb[static_cast<size_t>(ei)];
        const auto& a1 = pa[(static_cast<size_t>(ei)+ 1)];
        const auto& b1 = pb[(static_cast<size_t>(ei)+ 1)];
        float ix = 0.f, iy = 0.f;
        if (OfsLineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                    &ix, &iy))
          out.push_back({ix, iy});
        else {
          out.push_back({0.5f * (b0.first + a1.first), 0.5f * (b0.second + a1.second)});
        }
      }
      out.push_back(pb[(static_cast<size_t>(nEdges)- 1)]);
    }
  } else {
    out.resize(static_cast<size_t>(nEdges));
    for (int ei = 0; ei < nEdges; ++ei) {
      const int en = (ei + 1) % nEdges;
      const auto& a0 = pa[static_cast<size_t>(ei)];
      const auto& b0 = pb[static_cast<size_t>(ei)];
      const auto& a1 = pa[static_cast<size_t>(en)];
      const auto& b1 = pb[static_cast<size_t>(en)];
      float ix = 0.f, iy = 0.f;
      if (OfsLineLineIntersectInf(a0.first, a0.second, b0.first, b0.second, a1.first, a1.second, b1.first, b1.second,
                                  &ix, &iy))
        out[static_cast<size_t>(ei)] = {ix, iy};
      else
        out[static_cast<size_t>(ei)] = {0.5f * (b0.first + a1.first), 0.5f * (b0.second + a1.second)};
    }
  }
  if (out.size() < 2)
    return;
  for (size_t i = 0; i + 1 < out.size(); ++i) {
    lines->push_back(out[i].first);
    lines->push_back(out[i].second);
    lines->push_back(z);
    lines->push_back(out[i + 1].first);
    lines->push_back(out[i + 1].second);
    lines->push_back(z);
  }
  if (closed && out.size() >= 2) {
    lines->push_back(out.back().first);
    lines->push_back(out.back().second);
    lines->push_back(z);
    lines->push_back(out[0].first);
    lines->push_back(out[0].second);
    lines->push_back(z);
  }
}

} // namespace


void CadOffsetAppendLivePreview(const AppCommandState& cmd, float cursorWx, float cursorWy,
                                std::vector<float>* previewLines, std::vector<float>* previewCircles) {
  if (!previewLines || !previewCircles)
    return;
  previewLines->clear();
  previewCircles->clear();
  if (!cmd.offsetEntityValid)
    return;
  // REQ-329 increment 7: this ghost is built with flat world-XY math (the `Ofs*` helpers). Under a
  // tilted work plane the COMMITTED offset is resolved in that plane (CommitOffsetLine /
  // HandleOffsetSidePick), so a flat ghost would sit somewhere the click will not. Suppress it
  // there rather than mislead — the committed result is UCS-correct; a plane-aware preview is
  // follow-on work.
  if (!CadWorkPlaneIsWorldXy(cmd))
    return;
  float signedD = 0.f;
  if (!TryOffsetSignedDFromCursor(cmd, cursorWx, cursorWy, &signedD))
    return;

  using T = SelectedEntity::Type;
  const SelectedEntity& e = cmd.offsetEntity;
  constexpr float zl = 0.022f;

  switch (e.type) {
  case T::LineSeg: {
    const size_t k = static_cast<size_t>(e.index);
    if (k >= cmd.drawing.lineCount())
      return;
    const float x0 = static_cast<float>(cmd.drawing.line(k).x0);
    const float y0 = static_cast<float>(cmd.drawing.line(k).y0);
    const float x1 = static_cast<float>(cmd.drawing.line(k).x1);
    const float y1 = static_cast<float>(cmd.drawing.line(k).y1);
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    if (std::hypot(dx, dy) < 1e-8f)
      return;
    float nx = 0.f, ny = 0.f;
    OfsUnitLeftNormal(x0, y0, x1, y1, &nx, &ny);
    previewLines->push_back(x0 + nx * signedD);
    previewLines->push_back(y0 + ny * signedD);
    previewLines->push_back(zl);
    previewLines->push_back(x1 + nx * signedD);
    previewLines->push_back(y1 + ny * signedD);
    previewLines->push_back(zl);
    break;
  }
  case T::Circle: {
    const size_t ci = static_cast<size_t>(e.index);
    if (ci >= cmd.drawing.circleCount())
      return;
    const CadCircleRecord cc = cmd.drawing.circle(ci);
    const float cx = static_cast<float>(cc.cx);
    const float cy = static_cast<float>(cc.cy);
    const float r = static_cast<float>(cc.r);
    const float nr = r + signedD;
    if (nr <= 1e-6f)
      return;
    previewCircles->push_back(cx);
    previewCircles->push_back(cy);
    previewCircles->push_back(nr);
    break;
  }
  case T::Arc: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.drawing.userArcs.size())
      return;
    CadArc o = cmd.drawing.userArcs[static_cast<size_t>(e.index)];
    o.r += signedD;
    if (o.r <= 1e-6f)
      return;
    AppendArcPreviewStrip(zl, o, previewLines);
    break;
  }
  case T::Ellipse: {
    if (e.index < 0 || static_cast<size_t>(e.index) >= cmd.drawing.userEllipses.size())
      return;
    const CadEllipse& el0 = cmd.drawing.userEllipses[static_cast<size_t>(e.index)];
    const float ma = std::hypot(el0.majVx, el0.majVy);
    if (ma < 1e-8f || ma + signedD <= 1e-6f)
      return;
    CadEllipse el = el0;
    const float f = (ma + signedD) / ma;
    el.majVx *= f;
    el.majVy *= f;
    AppendEllipsePreviewStrip(zl, el, previewLines);
    break;
  }
  case T::Polyline:
    AppendPolylineOffsetPreview(cmd, e.index, signedD, zl, previewLines);
    break;
  default:
    break;
  }
}
