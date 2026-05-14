#include "CadCommands.hpp"
#include "CadUi.hpp"
#include "ViewportRenderer.hpp"
#include "CadSnap.hpp"
#include "SurveyPoints.hpp"
#include "AppIcon.hpp"
#include "GsIo.hpp"
#include "SplashScreen.hpp"
#include "UserPrefs.hpp"
#include "ImGuiLayout.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

static void TryLoadStartupWorkspaceTemplate(AppCommandState& cmd, std::vector<std::string>& cmdLog) {
  namespace fs = std::filesystem;
  auto appendLines = [&](std::vector<std::string>& lines) {
    for (auto& s : lines)
      cmdLog.push_back(std::move(s));
  };
  auto tryLoadPath = [&](const fs::path& p) -> bool {
    if (p.empty() || !fs::exists(p))
      return false;
    std::vector<std::string> boot;
    const std::string u8 = p.u8string();
    if (!LoadGoSurveyFile(cmd, u8.c_str(), boot))
      return false;
    appendLines(boot);
    return true;
  };

  if (cmd.defaultWorkspaceTemplatePathUtf8[0] != '\0') {
    const fs::path custom(cmd.defaultWorkspaceTemplatePathUtf8);
    if (!custom.empty() && fs::exists(custom)) {
      std::vector<std::string> boot;
      const std::string u8 = custom.u8string();
      if (LoadGoSurveyFile(cmd, u8.c_str(), boot)) {
        appendLines(boot);
        return;
      }
      appendLines(boot);
      cmdLog.push_back("Startup: custom template failed to load; trying bundled default-template.gs.");
    } else {
      cmdLog.push_back("Startup: custom template path not found; trying bundled default-template.gs.");
    }
  }

  if (tryLoadPath(ResolveDefaultWorkspaceTemplateGsPath()))
    return;
  cmdLog.push_back("Startup: bundled default-template.gs not found; starting with an empty drawing.");
}

void PushRubberSeg(std::vector<float>& o, float x0, float y0, float x1, float y1) {
  o.push_back(x0);
  o.push_back(y0);
  o.push_back(0.f);
  o.push_back(x1);
  o.push_back(y1);
  o.push_back(0.f);
}

void AppendWorldRectRubber(std::vector<float>& o, float xa, float ya, float xb, float yb) {
  const float mnX = std::min(xa, xb);
  const float mxX = std::max(xa, xb);
  const float mnY = std::min(ya, yb);
  const float mxY = std::max(ya, yb);
  PushRubberSeg(o, mnX, mnY, mxX, mnY);
  PushRubberSeg(o, mxX, mnY, mxX, mxY);
  PushRubberSeg(o, mxX, mxY, mnX, mxY);
  PushRubberSeg(o, mnX, mxY, mnX, mnY);
}

bool ComputeCircumcircleRubber(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                               float* r);

void AppendArcRubber(std::vector<float>& out, float ax, float ay, float bx, float by, float cx, float cy) {
  float ox = 0.f, oy = 0.f, r = 0.f;
  if (!ComputeCircumcircleRubber(ax, ay, bx, by, cx, cy, &ox, &oy, &r) || r <= 1e-6f)
    return;
  constexpr double twopi = 6.28318530717958647692;
  auto normPos = [](double x) {
    double t = std::fmod(x, twopi);
    if (t < 0)
      t += twopi;
    return t;
  };
  const double ta = std::atan2(static_cast<double>(ay - oy), static_cast<double>(ax - ox));
  const double tb = std::atan2(static_cast<double>(by - oy), static_cast<double>(bx - ox));
  const double tc = std::atan2(static_cast<double>(cy - oy), static_cast<double>(cx - ox));
  const double arc_ab = normPos(tb - ta);
  const double arc_ac = normPos(tc - ta);
  const bool useCcw = arc_ab <= arc_ac + 1e-10;
  double sweep = useCcw ? arc_ac : arc_ac - twopi;
  if (std::fabs(sweep) < 1e-12)
    sweep = twopi;
  const double sr = ta;
  const int nseg = 36;
  for (int i = 0; i < nseg; ++i) {
    double t0 = sr + sweep * static_cast<double>(i) / static_cast<double>(nseg);
    double t1 = sr + sweep * static_cast<double>(i + 1) / static_cast<double>(nseg);
    float x0 = static_cast<float>(ox + r * std::cos(t0));
    float y0 = static_cast<float>(oy + r * std::sin(t0));
    float x1 = static_cast<float>(ox + r * std::cos(t1));
    float y1 = static_cast<float>(oy + r * std::sin(t1));
    PushRubberSeg(out, x0, y0, x1, y1);
  }
}

bool ComputeCircumcircleRubber(float ax, float ay, float bx, float by, float cx, float cy, float* ox, float* oy,
                               float* r) {
  const float d = 2.f * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
  if (std::fabs(d) < 1e-6f)
    return false;
  const float a2 = ax * ax + ay * ay;
  const float b2 = bx * bx + by * by;
  const float c2 = cx * cx + cy * cy;
  const float ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
  const float uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
  const float dx = ux - ax;
  const float dy = uy - ay;
  *ox = ux;
  *oy = uy;
  *r = std::sqrt(dx * dx + dy * dy);
  return true;
}

void AppendCircleRubber(std::vector<float>& out, float cx, float cy, float r, int segments = 72) {
  if (r <= 1e-6f)
    return;
  const float twoPi = 6.28318530718f;
  for (int i = 0; i < segments; ++i) {
    const float t0 = twoPi * static_cast<float>(i) / static_cast<float>(segments);
    const float t1 = twoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
    out.push_back(cx + r * std::cos(t0));
    out.push_back(cy + r * std::sin(t0));
    out.push_back(0.f);
    out.push_back(cx + r * std::cos(t1));
    out.push_back(cy + r * std::sin(t1));
    out.push_back(0.f);
  }
}

void RotatePreviewPt(float bx, float by, float rad, float* x, float* y) {
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  const float dx = *x - bx;
  const float dy = *y - by;
  *x = bx + c * dx - s * dy;
  *y = by + s * dx + c * dy;
}

void ScalePreviewPt(float bx, float by, float sc, float* x, float* y) {
  *x = bx + sc * (*x - bx);
  *y = by + sc * (*y - by);
}

void AppendArcPolylineStrip(std::vector<float>* out, float z, const CadArc& a, int n = 48) {
  for (int i = 0; i < n; ++i) {
    const float u0 = static_cast<float>(i) / static_cast<float>(n);
    const float u1 = static_cast<float>(i + 1) / static_cast<float>(n);
    const float t0 = a.startRad + a.sweepRad * u0;
    const float t1 = a.startRad + a.sweepRad * u1;
    const float x0 = a.cx + a.r * std::cos(t0);
    const float y0 = a.cy + a.r * std::sin(t0);
    const float x1 = a.cx + a.r * std::cos(t1);
    const float y1 = a.cy + a.r * std::sin(t1);
    out->push_back(x0);
    out->push_back(y0);
    out->push_back(z);
    out->push_back(x1);
    out->push_back(y1);
    out->push_back(z);
  }
}

void AppendEllipsePolylineStrip(std::vector<float>* out, float z, const CadEllipse& el, int n = 56) {
  const float ma = std::hypot(el.majVx, el.majVy);
  if (ma < 1e-8f)
    return;
  const float ux = el.majVx / ma;
  const float uy = el.majVy / ma;
  const float px = -uy;
  const float py = ux;
  const float mb = ma * el.ratio;
  constexpr float twopi = 6.28318530718f;
  for (int i = 0; i < n; ++i) {
    const float u0 = twopi * static_cast<float>(i) / static_cast<float>(n);
    const float u1 = twopi * static_cast<float>(i + 1) / static_cast<float>(n);
    const float c0 = std::cos(u0);
    const float s0 = std::sin(u0);
    const float c1 = std::cos(u1);
    const float s1 = std::sin(u1);
    const float x0 = el.cx + ux * (ma * c0) + px * (mb * s0);
    const float y0 = el.cy + uy * (ma * c0) + py * (mb * s0);
    const float x1 = el.cx + ux * (ma * c1) + px * (mb * s1);
    const float y1 = el.cy + uy * (ma * c1) + py * (mb * s1);
    out->push_back(x0);
    out->push_back(y0);
    out->push_back(z);
    out->push_back(x1);
    out->push_back(y1);
    out->push_back(z);
  }
}

void AppendCommittedPolylineStrip(std::vector<float>* out, float z, const AppCommandState& cmd, int pi) {
  if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
    return;
  const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
  const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
  const bool closed =
      static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
  for (int vi = v0; vi + 1 < v1; ++vi) {
    const float x0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3)];
    const float y0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
    const float x1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
    const float y1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
    out->push_back(x0);
    out->push_back(y0);
    out->push_back(z);
    out->push_back(x1);
    out->push_back(y1);
    out->push_back(z);
  }
  if (closed && v1 - v0 >= 2) {
    const float x0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
    const float y0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
    const float x1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)];
    const float y1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
    out->push_back(x0);
    out->push_back(y0);
    out->push_back(z);
    out->push_back(x1);
    out->push_back(y1);
    out->push_back(z);
  }
}

/// Translucent preview for MOVE/COPY (destination pick) and ROTATE (angle from cursor vs reference).
void BuildTransformPreview(const AppCommandState& cmd, float curX, float curY, std::vector<float>* prevLines,
                           std::vector<float>* prevCircles) {
  prevLines->clear();
  prevCircles->clear();
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using OP = AppCommandState::OffsetPhase;

  if (cmd.active == K::Offset && cmd.offsetEntityValid &&
      (cmd.offsetPhase == OP::WaitDistanceOrThrough || cmd.offsetPhase == OP::WaitSidePick)) {
    CadOffsetAppendLivePreview(cmd, curX, curY, prevLines, prevCircles);
    return;
  }

  if (cmd.active == K::Move || cmd.active == K::Copy) {
    if (cmd.modifyPhase != MP::NeedDestination)
      return;
    const float dx = curX - cmd.modifyBaseX;
    const float dy = curY - cmd.modifyBaseY;
    for (const auto& e : cmd.selection) {
      if (e.type == SelectedEntity::Type::LineSeg) {
        const size_t k = static_cast<size_t>(e.index) * 6;
        if (k + 5 >= cmd.userLinesFlat.size())
          continue;
        for (int i = 0; i < 2; ++i) {
          prevLines->push_back(cmd.userLinesFlat[k + i * 3] + dx);
          prevLines->push_back(cmd.userLinesFlat[k + i * 3 + 1] + dy);
          prevLines->push_back(0.f);
        }
      } else if (e.type == SelectedEntity::Type::Circle) {
        const size_t k = static_cast<size_t>(e.index) * 3;
        if (k + 2 >= cmd.userCirclesCxCyR.size())
          continue;
        prevCircles->push_back(cmd.userCirclesCxCyR[k] + dx);
        prevCircles->push_back(cmd.userCirclesCxCyR[k + 1] + dy);
        prevCircles->push_back(cmd.userCirclesCxCyR[k + 2]);
      } else if (e.type == SelectedEntity::Type::Arc) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userArcs.size())
          continue;
        CadArc a = cmd.userArcs[k];
        a.cx += dx;
        a.cy += dy;
        AppendArcPolylineStrip(prevLines, 0.f, a);
      } else if (e.type == SelectedEntity::Type::Ellipse) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userEllipses.size())
          continue;
        CadEllipse el = cmd.userEllipses[k];
        el.cx += dx;
        el.cy += dy;
        AppendEllipsePolylineStrip(prevLines, 0.f, el);
      } else if (e.type == SelectedEntity::Type::Polyline) {
        const int pi = e.index;
        if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
          continue;
        const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
        const bool closed =
            static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
        for (int vi = v0; vi + 1 < v1; ++vi) {
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3)] + dx);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)] + dy);
          prevLines->push_back(0.f);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)] + dx);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)] + dy);
          prevLines->push_back(0.f);
        }
        if (closed && v1 - v0 >= 2) {
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)] + dx);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)] + dy);
          prevLines->push_back(0.f);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)] + dx);
          prevLines->push_back(cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)] + dy);
          prevLines->push_back(0.f);
        }
      }
    }
    return;
  }

  if (cmd.active == K::Scale && cmd.modifyPhase == MP::NeedDestination) {
    using SP = AppCommandState::ScalePhase;
    if (cmd.scalePhase == SP::Ref_WaitP2) {
      prevLines->push_back(cmd.scaleRefP1X);
      prevLines->push_back(cmd.scaleRefP1Y);
      prevLines->push_back(0.f);
      prevLines->push_back(curX);
      prevLines->push_back(curY);
      prevLines->push_back(0.f);
      return;
    }
    if (cmd.scalePhase == SP::NewLength_WaitP2) {
      prevLines->push_back(cmd.scaleNewLenP1X);
      prevLines->push_back(cmd.scaleNewLenP1Y);
      prevLines->push_back(0.f);
      prevLines->push_back(curX);
      prevLines->push_back(curY);
      prevLines->push_back(0.f);
    }
    float sc = 1.f;
    if (!CadScalePreviewFactor(cmd, curX, curY, &sc))
      return;
    const float bx = cmd.modifyBaseX;
    const float by = cmd.modifyBaseY;
    for (const auto& e : cmd.selection) {
      if (e.type == SelectedEntity::Type::LineSeg) {
        const size_t k = static_cast<size_t>(e.index) * 6;
        if (k + 5 >= cmd.userLinesFlat.size())
          continue;
        for (int i = 0; i < 2; ++i) {
          float x = cmd.userLinesFlat[k + i * 3];
          float y = cmd.userLinesFlat[k + i * 3 + 1];
          ScalePreviewPt(bx, by, sc, &x, &y);
          prevLines->push_back(x);
          prevLines->push_back(y);
          prevLines->push_back(0.f);
        }
      } else if (e.type == SelectedEntity::Type::Circle) {
        const size_t k = static_cast<size_t>(e.index) * 3;
        if (k + 2 >= cmd.userCirclesCxCyR.size())
          continue;
        float x = cmd.userCirclesCxCyR[k];
        float y = cmd.userCirclesCxCyR[k + 1];
        float r = cmd.userCirclesCxCyR[k + 2];
        ScalePreviewPt(bx, by, sc, &x, &y);
        r *= sc;
        prevCircles->push_back(x);
        prevCircles->push_back(y);
        prevCircles->push_back(r);
      } else if (e.type == SelectedEntity::Type::Arc) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userArcs.size())
          continue;
        CadArc a = cmd.userArcs[k];
        ScalePreviewPt(bx, by, sc, &a.cx, &a.cy);
        a.r *= sc;
        AppendArcPolylineStrip(prevLines, 0.f, a);
      } else if (e.type == SelectedEntity::Type::Ellipse) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userEllipses.size())
          continue;
        CadEllipse el = cmd.userEllipses[k];
        float mx = el.cx + el.majVx;
        float my = el.cy + el.majVy;
        ScalePreviewPt(bx, by, sc, &el.cx, &el.cy);
        ScalePreviewPt(bx, by, sc, &mx, &my);
        el.majVx = mx - el.cx;
        el.majVy = my - el.cy;
        AppendEllipsePolylineStrip(prevLines, 0.f, el);
      } else if (e.type == SelectedEntity::Type::Polyline) {
        const int pi = e.index;
        if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
          continue;
        const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
        const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
        const bool closed =
            static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
        for (int vi = v0; vi + 1 < v1; ++vi) {
          float x0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3)];
          float y0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
          float x1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
          float y1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
          ScalePreviewPt(bx, by, sc, &x0, &y0);
          ScalePreviewPt(bx, by, sc, &x1, &y1);
          prevLines->push_back(x0);
          prevLines->push_back(y0);
          prevLines->push_back(0.f);
          prevLines->push_back(x1);
          prevLines->push_back(y1);
          prevLines->push_back(0.f);
        }
        if (closed && v1 - v0 >= 2) {
          float x0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
          float y0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
          float x1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)];
          float y1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
          ScalePreviewPt(bx, by, sc, &x0, &y0);
          ScalePreviewPt(bx, by, sc, &x1, &y1);
          prevLines->push_back(x0);
          prevLines->push_back(y0);
          prevLines->push_back(0.f);
          prevLines->push_back(x1);
          prevLines->push_back(y1);
          prevLines->push_back(0.f);
        }
      }
    }
    return;
  }

  if (cmd.active != K::Rotate)
    return;

  float theta = 0.f;
  if (!CadRotatePreviewTheta(cmd, curX, curY, &theta))
    return;

  const float bx = cmd.rotateBaseX;
  const float by = cmd.rotateBaseY;
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size())
        continue;
      for (int i = 0; i < 2; ++i) {
        float x = cmd.userLinesFlat[k + i * 3];
        float y = cmd.userLinesFlat[k + i * 3 + 1];
        RotatePreviewPt(bx, by, theta, &x, &y);
        prevLines->push_back(x);
        prevLines->push_back(y);
        prevLines->push_back(0.f);
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size())
        continue;
      float x = cmd.userCirclesCxCyR[k];
      float y = cmd.userCirclesCxCyR[k + 1];
      RotatePreviewPt(bx, by, theta, &x, &y);
      prevCircles->push_back(x);
      prevCircles->push_back(y);
      prevCircles->push_back(cmd.userCirclesCxCyR[k + 2]);
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userArcs.size())
        continue;
      CadArc a = cmd.userArcs[k];
      RotatePreviewPt(bx, by, theta, &a.cx, &a.cy);
      a.startRad += theta;
      AppendArcPolylineStrip(prevLines, 0.f, a);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userEllipses.size())
        continue;
      CadEllipse el = cmd.userEllipses[k];
      float mx = el.cx + el.majVx;
      float my = el.cy + el.majVy;
      RotatePreviewPt(bx, by, theta, &el.cx, &el.cy);
      RotatePreviewPt(bx, by, theta, &mx, &my);
      el.majVx = mx - el.cx;
      el.majVy = my - el.cy;
      AppendEllipsePolylineStrip(prevLines, 0.f, el);
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || static_cast<size_t>(pi + 1) >= cmd.userPolylineOffsets.size())
        continue;
      const int v0 = cmd.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = cmd.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      const bool closed =
          static_cast<size_t>(pi) < cmd.userPolylineClosed.size() && cmd.userPolylineClosed[static_cast<size_t>(pi)];
      for (int vi = v0; vi + 1 < v1; ++vi) {
        float x0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3)];
        float y0 = cmd.userPolylineVerts[static_cast<size_t>(vi * 3 + 1)];
        float x1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3)];
        float y1 = cmd.userPolylineVerts[static_cast<size_t>((vi + 1) * 3 + 1)];
        RotatePreviewPt(bx, by, theta, &x0, &y0);
        RotatePreviewPt(bx, by, theta, &x1, &y1);
        prevLines->push_back(x0);
        prevLines->push_back(y0);
        prevLines->push_back(0.f);
        prevLines->push_back(x1);
        prevLines->push_back(y1);
        prevLines->push_back(0.f);
      }
      if (closed && v1 - v0 >= 2) {
        float x0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3)];
        float y0 = cmd.userPolylineVerts[static_cast<size_t>((v1 - 1) * 3 + 1)];
        float x1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3)];
        float y1 = cmd.userPolylineVerts[static_cast<size_t>(v0 * 3 + 1)];
        RotatePreviewPt(bx, by, theta, &x0, &y0);
        RotatePreviewPt(bx, by, theta, &x1, &y1);
        prevLines->push_back(x0);
        prevLines->push_back(y0);
        prevLines->push_back(0.f);
        prevLines->push_back(x1);
        prevLines->push_back(y1);
        prevLines->push_back(0.f);
      }
    }
  }
}

void BuildSelectionHighlight(const AppCommandState& cmd, std::vector<float>* hlLines,
                             std::vector<float>* hlCircles) {
  hlLines->clear();
  hlCircles->clear();
  constexpr float kLineZ = 0.012f;
  const auto appendOne = [&](const SelectedEntity& e) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size())
        return;
      for (int i = 0; i < 2; ++i) {
        hlLines->push_back(cmd.userLinesFlat[k + i * 3]);
        hlLines->push_back(cmd.userLinesFlat[k + i * 3 + 1]);
        hlLines->push_back(kLineZ);
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size())
        return;
      hlCircles->push_back(cmd.userCirclesCxCyR[k]);
      hlCircles->push_back(cmd.userCirclesCxCyR[k + 1]);
      hlCircles->push_back(cmd.userCirclesCxCyR[k + 2]);
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userArcs.size())
        return;
      AppendArcPolylineStrip(hlLines, kLineZ, cmd.userArcs[k]);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userEllipses.size())
        return;
      AppendEllipsePolylineStrip(hlLines, kLineZ, cmd.userEllipses[k]);
    } else if (e.type == SelectedEntity::Type::Polyline) {
      AppendCommittedPolylineStrip(hlLines, kLineZ, cmd, e.index);
    }
  };
  for (const auto& e : cmd.selection)
    appendOne(e);
  if (cmd.active == AppCommandState::Kind::Offset && cmd.offsetPhase == AppCommandState::OffsetPhase::WaitSelectEntity &&
      cmd.offsetHoverHighlightValid)
    appendOne(cmd.offsetHoverEntity);
}

} // namespace

static void GlfwErrorCallback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int main() {
  glfwSetErrorCallback(GlfwErrorCallback);
  if (!glfwInit())
    return 1;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GlfwApplySplashStageWindowHints();

  GLFWwindow* window = glfwCreateWindow(1600, 900, "GoSurvey", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwDefaultWindowHints();
  glfwMaximizeWindow(window);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  ViewportRenderer viewport;
  if (!viewport.Init()) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  AppLogoGpu appLogo{};
  {
    namespace fs = std::filesystem;
    const fs::path iconPath = ResolveAppLogoPngPath();
    if (!iconPath.empty() && LoadAppLogoFromPngFile(window, iconPath, &appLogo, true))
      CadUiSetMenuBarLogo((ImTextureID)(intptr_t)(uintptr_t)appLogo.texture, static_cast<float>(appLogo.width),
                          static_cast<float>(appLogo.height));
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigInputTextEnterKeepActive = false; // CAD shell: Enter submits without selecting-all next keystroke

  ApplyCadDarkTheme();
  if (!LoadApplicationFont())
    std::fprintf(stderr, "Calibri not found; using ImGui default font.\n");
  io.FontGlobalScale = 1.05f;

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  //RunStartupSplash(window, 1.0);
  GlfwApplyMainStageWindowChrome(window);

  AppCommandState cmd;
  LoadUserStartupPrefs(cmd);
  const bool haveSavedDockIni = ImGuiLayout_ConfigureIniPath(cmd);
  std::vector<std::string> cmdLog;
  cmdLog.push_back("GoSurvey CAD shell ready.");
  cmdLog.push_back(
      "LINE/L … SURVEY: CREATEPOINTS (CRTPTS), VIEWPOINTS (VWPTS), IMPORTPOINTS (IMPPTS), EXPORTPOINTS (EXPPTS), "
      "INVERSE (INV), "
      "JSON database — idle: two-click select. MMB "
      "pan.");
  TryLoadStartupWorkspaceTemplate(cmd, cmdLog);
  char cmdBuf[4096]{};

  float panX = 0.f;
  float panY = 0.f;
  float zoom = 1.f;
  float curX = 0.f;
  float curY = 0.f;
  float curRawX = 0.f;
  float curRawY = 0.f;
  int fbW = 900;
  int fbH = 650;

  bool dockLayoutDone = haveSavedDockIni;
  const float ribbonH = 130.f;
  bool orthoEnabled = true;
  bool gridVisible = true;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& ioFrame = ImGui::GetIO();
    if (!ioFrame.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_F3, false))
        cmd.objectSnapEnabled = !cmd.objectSnapEnabled;
      if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
        orthoEnabled = !orthoEnabled;
    }
    cmd.orthoMode = orthoEnabled;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      if (cmd.copySurveyDupModalOpen) {
        ApplyCopySurveyDuplicateModalResult(cmd, false, cmdLog);
        cmdBuf[0] = '\0';
      } else if (cmd.mtextRichEditorOpen) {
        CancelMtextRichEditor(cmd, &cmdLog);
        cmdBuf[0] = '\0';
      } else if (cmd.mtextGripMoveActive) {
        AbortMtextGripInteraction(cmd);
        BumpCadGpuCache(cmd);
        cmdBuf[0] = '\0';
        cmdLog.push_back("MTEXT grip edit canceled.");
      } else if (cmd.entityGripMoveActive) {
        RestoreEntityGripOriginal(cmd);
        ClearEntityGripInteraction(cmd);
        BumpCadGpuCache(cmd);
        cmdBuf[0] = '\0';
        cmdLog.push_back("Grip edit canceled.");
      } else if (cmd.active != AppCommandState::Kind::None) {
        using SAP = AppCommandState::SegmentAnglePickPhase;
        if ((cmd.active == AppCommandState::Kind::Line || cmd.active == AppCommandState::Kind::Polyline) &&
            cmd.segmentAnglePickPhase != SAP::Idle)
          CancelSegmentAnglePick(cmd, &cmdLog);
        else
          CancelActiveCommand(cmd, cmdLog);
        cmdBuf[0] = '\0';
      } else {
        const bool hadCreatePtsUi = cmd.showCreatePointsWindow || cmd.createPointsPlacementActive;
        cmd.showCreatePointsWindow = false;
        cmd.createPointsPlacementActive = false;
        ClearSelection(cmd);
        cmd.pendingZoomExtents = false;
        cmd.pendingZoomWindow = false;
        cmdBuf[0] = '\0';
        cmdLog.push_back(hadCreatePtsUi ? "Selection cleared; CREATEPOINTS closed." : "Selection cleared.");
      }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !ImGui::GetIO().WantTextInput) {
      StartDeleteCommand(cmd, cmdLog);
    }

    const ImGuiViewport* mainVp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainVp->WorkPos);
    ImGui::SetNextWindowSize(mainVp->WorkSize);
    ImGui::SetNextWindowViewport(mainVp->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
#if !defined(_WIN32)
    hostFlags |= ImGuiWindowFlags_MenuBar;
#endif

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("GoSurveyHost", nullptr, hostFlags);

    DrawMainWindowTitleBar(window);

#if defined(_WIN32)
    {
      const float menuStripH = ImGui::GetFrameHeight() + 6.f;
      ImGui::BeginChild("##GoSurveyMenuHost", ImVec2(0.f, menuStripH), false,
                        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      if (ImGui::BeginMenuBar()) {
        DrawMainMenuBar(cmd, cmdLog);
        ImGui::EndMenuBar();
      }
      ImGui::EndChild();
    }
#else
    if (ImGui::BeginMenuBar()) {
      DrawMainMenuBar(cmd, cmdLog);
      ImGui::EndMenuBar();
    }
#endif

    DrawRibbonBar(ribbonH, cmd, cmdLog);

    const float cadStatusH = CadStatusBarStripHeightPx();
    const float dockWrapH = std::max(1.f, ImGui::GetContentRegionAvail().y - cadStatusH);
    ImGui::BeginChild("##GoSurveyDockWrap", ImVec2(0.f, dockWrapH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 dockHostSize = ImGui::GetContentRegionAvail();
    ImGuiID dockspaceId = ImGui::GetID("GoSurveyDockSpace");
    ImGui::DockSpace(dockspaceId, dockHostSize, 0);

    if (cmd.pendingBuiltinDockLayoutReset) {
      cmd.pendingBuiltinDockLayoutReset = false;
      SetupMainDockLayout(dockspaceId, dockHostSize);
      ImGuiIO& ioDock = ImGui::GetIO();
      if (ioDock.IniFilename && ioDock.IniFilename[0])
        ImGui::SaveIniSettingsToDisk(ioDock.IniFilename);
      dockLayoutDone = true;
      cmdLog.push_back("UI layout: reset to built-in dock split (saved to current layout .ini).");
    } else if (!dockLayoutDone) {
      dockLayoutDone = true;
      SetupMainDockLayout(dockspaceId, dockHostSize);
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar(3);

    DrawPropertiesPanel(cmd, &cmdLog);

    CadSnap::Hit snapHit{};
    DrawDrawingViewport(viewport.ColorTexture(), cmd, cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)), &panX,
                        &panY, &zoom, &curX, &curY, &curRawX, &curRawY, &fbW, &fbH, &snapHit);
    cmd.uiCursorWorldX = curX;
    cmd.uiCursorWorldY = curY;
    {
      ImGuiIO& ioDim = ImGui::GetIO();
      if (!ioDim.WantTextInput && cmd.active == AppCommandState::Kind::DimLinear &&
          cmd.dimPhase == AppCommandState::DimPhase::WaitDimLinePt) {
        if (ImGui::IsKeyPressed(ImGuiKey_H, false))
          CadDimLinearApplyHVHotkey(cmd, false, cmdLog);
        else if (ImGui::IsKeyPressed(ImGuiKey_V, false))
          CadDimLinearApplyHVHotkey(cmd, true, cmdLog);
      }
    }
    DrawCommandLinePanel(cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)), cmd);
    DrawCadStatusBarStrip(cmd, curX, curY, 0.f, &orthoEnabled, &gridVisible);

    // LINE/POLYLINE AP: after two picks the bottom command InputText is hidden — Enter must still lock bearing.
    // Keyboard-only "A" then bearing: Enter with empty buffer cancels awaiting mode when no text field is focused.
    {
      ImGuiIO& ioEnter = ImGui::GetIO();
      const bool enterDown =
          ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
      if (enterDown && !ioEnter.WantTextInput) {
        using SAP = AppCommandState::SegmentAnglePickPhase;
        using LP = AppCommandState::LinePhase;
        using PP = AppCommandState::PolylinePhase;
        const bool lineNext =
            cmd.active == AppCommandState::Kind::Line && cmd.linePhase == LP::NeedNextPoint;
        const bool polyNext =
            cmd.active == AppCommandState::Kind::Polyline && cmd.polylinePhase == PP::NeedNextPoint;
        const bool apCommit =
            (lineNext || polyNext) && cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit;
        const bool kbAwaitBearing =
            (lineNext || polyNext) && cmd.segmentAngleKeyboardAwaitBearing;
        if (apCommit || kbAwaitBearing)
          ProcessCommandLineSubmit(cmdBuf, static_cast<int>(sizeof(cmdBuf)), cmd, cmdLog);
      }
    }

    DrawCreatePointsPanel(cmd, cmdLog);
    DrawSettingsPanel(cmd, &cmdLog);
    ImGuiLayout_DrawLayoutPopups(cmd, cmdLog);
    DrawLayerManagerWindow(cmd, &cmdLog);
    DrawViewPointsPanel(cmd, cmdLog);
    DrawImportPointsPanel(cmd, cmdLog);
    DrawExportPointsPanel(cmd, cmdLog);
    DrawSurveyReportsPanel(cmd);
    DrawCopySurveyDuplicateModal(cmd, cmdLog);

    ProcessPendingViewportZoom(cmd, &panX, &panY, &zoom, fbW, fbH, cmdLog);

    std::vector<float> rubberLines;
    if (cmd.active == AppCommandState::Kind::Line &&
        cmd.linePhase == AppCommandState::LinePhase::NeedNextPoint) {
      using SAP = AppCommandState::SegmentAnglePickPhase;
      if (cmd.segmentAnglePickPhase == SAP::WaitP2)
        PushRubberSeg(rubberLines, cmd.segmentPickRefX1, cmd.segmentPickRefY1, curX, curY);
      else if (cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
        const float th = MathAngleRadFromBearingCwNorthDeg(cmd.segmentPickDraftBearingDeg);
        const float ux = std::cos(th);
        const float uy = std::sin(th);
        float lx = curX;
        float ly = curY;
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
        PushRubberSeg(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly);
      } else {
        float lx = curX;
        float ly = curY;
        if (cmd.segmentAngleLockActive)
          ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, cmd.segmentLockUx, cmd.segmentLockUy, &lx, &ly,
                                            false);
        else
          ApplyOrthoConstrainFromAnchor(cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled);
        PushRubberSeg(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly);
      }
    }

    if (cmd.active == AppCommandState::Kind::Polyline &&
        cmd.polylinePhase == AppCommandState::PolylinePhase::NeedNextPoint) {
      float lx = curX;
      float ly = curY;
      using SAP = AppCommandState::SegmentAnglePickPhase;
      const auto& d = cmd.polylineDraftVerts;
      for (size_t i = 0; i + 5 < d.size(); i += 3)
        PushRubberSeg(rubberLines, d[i], d[i + 1], d[i + 3], d[i + 4]);

      if (cmd.segmentAnglePickPhase == SAP::WaitP2)
        PushRubberSeg(rubberLines, cmd.segmentPickRefX1, cmd.segmentPickRefY1, curX, curY);
      else if (cmd.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
        const float th = MathAngleRadFromBearingCwNorthDeg(cmd.segmentPickDraftBearingDeg);
        const float ux = std::cos(th);
        const float uy = std::sin(th);
        lx = curX;
        ly = curY;
        ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, ux, uy, &lx, &ly, false);
        PushRubberSeg(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly);
      } else {
        if (cmd.segmentAngleLockActive)
          ApplySegmentAngleLockToWorldPick(cmd.anchorX, cmd.anchorY, cmd.segmentLockUx, cmd.segmentLockUy, &lx, &ly,
                                            false);
        else
          ApplyOrthoConstrainFromAnchor(cmd.anchorX, cmd.anchorY, &lx, &ly, orthoEnabled);
        PushRubberSeg(rubberLines, cmd.anchorX, cmd.anchorY, lx, ly);
      }
    }

    if (cmd.active == AppCommandState::Kind::Arc) {
      using AP = AppCommandState::ArcPhase;
      if (cmd.arcPhase == AP::WaitMid)
        PushRubberSeg(rubberLines, cmd.arcAx, cmd.arcAy, curX, curY);
      else if (cmd.arcPhase == AP::WaitEnd)
        AppendArcRubber(rubberLines, cmd.arcAx, cmd.arcAy, cmd.arcBx, cmd.arcBy, curX, curY);
    }

    if (cmd.active == AppCommandState::Kind::Ellipse &&
        cmd.ellPhase == AppCommandState::EllipsePhase::WaitMajorEnd)
      PushRubberSeg(rubberLines, cmd.ellCx, cmd.ellCy, curX, curY);

    if ((cmd.active == AppCommandState::Kind::DimAligned || cmd.active == AppCommandState::Kind::DimLinear) &&
        cmd.dimPhase == AppCommandState::DimPhase::WaitExt2)
      PushRubberSeg(rubberLines, cmd.dimE1x, cmd.dimE1y, curX, curY);

    if (cmd.active == AppCommandState::Kind::SurveyInverse &&
        cmd.surveyInversePhase == AppCommandState::SurveyInversePhase::WaitTo)
      PushRubberSeg(rubberLines, cmd.surveyInverseFromX, cmd.surveyInverseFromY, curX, curY);
    if (cmd.active == AppCommandState::Kind::DimAngular) {
      using DAP = AppCommandState::DimAngularPhase;
      if (cmd.dimAngularPhase == DAP::WaitRay1)
        PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, curX, curY);
      else if (cmd.dimAngularPhase == DAP::WaitRay2) {
        PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE1x, cmd.dimE1y);
        PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, curX, curY);
      } else if (cmd.dimAngularPhase == DAP::WaitArc) {
        PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE1x, cmd.dimE1y);
        PushRubberSeg(rubberLines, cmd.dimAngVx, cmd.dimAngVy, cmd.dimE2x, cmd.dimE2y);
      }
    }

    if (cmd.active == AppCommandState::Kind::Circle) {
      using CP = AppCommandState::CirclePhase;
      if (cmd.circlePhase == CP::WaitRadius) {
        const float dx = curX - cmd.circleCx;
        const float dy = curY - cmd.circleCy;
        AppendCircleRubber(rubberLines, cmd.circleCx, cmd.circleCy, std::sqrt(dx * dx + dy * dy));
      } else if (cmd.circlePhase == CP::ThreeP_WaitP2) {
        const float dx = curX - cmd.c3p1x;
        const float dy = curY - cmd.c3p1y;
        const float chord = std::sqrt(dx * dx + dy * dy);
        const float rPrev = 0.5f * chord;
        if (rPrev > 1e-6f)
          AppendCircleRubber(rubberLines, (cmd.c3p1x + curX) * 0.5f, (cmd.c3p1y + curY) * 0.5f, rPrev);
      } else if (cmd.circlePhase == CP::ThreeP_WaitP3) {
        float ox = 0.f;
        float oy = 0.f;
        float rCirc = 0.f;
        if (ComputeCircumcircle(cmd.c3p1x, cmd.c3p1y, cmd.c3p2x, cmd.c3p2y, curX, curY, &ox, &oy, &rCirc))
          AppendCircleRubber(rubberLines, ox, oy, rCirc);
      }
    }

    if (cmd.active == AppCommandState::Kind::Mtext) {
      using MPtxt = AppCommandState::MtextPhase;
      if (cmd.mtextPhase == MPtxt::WaitCorner2) {
        float lx = curX;
        float ly = curY;
        ApplyOrthoConstrainFromAnchor(cmd.mtxtX1, cmd.mtxtY1, &lx, &ly, orthoEnabled);
        AppendWorldRectRubber(rubberLines, cmd.mtxtX1, cmd.mtxtY1, lx, ly);
      } else if (cmd.mtextPhase == MPtxt::WaitString)
        AppendWorldRectRubber(rubberLines, cmd.mtxtX1, cmd.mtxtY1, cmd.mtxtX2, cmd.mtxtY2);
    }

    float selRectBuf[4]{};
    const float* selRectPtr = nullptr;
    if (cmd.selBoxWaitingSecond) {
      selRectBuf[0] = std::min(cmd.selBoxAnchorX, curRawX);
      selRectBuf[1] = std::max(cmd.selBoxAnchorX, curRawX);
      selRectBuf[2] = std::min(cmd.selBoxAnchorY, curRawY);
      selRectBuf[3] = std::max(cmd.selBoxAnchorY, curRawY);
      selRectPtr = selRectBuf;
    }

    std::vector<float> previewLines;
    std::vector<float> previewCircles;
    const float previewCx = cmd.active == AppCommandState::Kind::Offset ? curRawX : curX;
    const float previewCy = cmd.active == AppCommandState::Kind::Offset ? curRawY : curY;
    BuildTransformPreview(cmd, previewCx, previewCy, &previewLines, &previewCircles);

    if (cmd.active == AppCommandState::Kind::Trim &&
        cmd.trimPhase == AppCommandState::TrimPhase::CuttingLine_WaitP2) {
      float lx = curX;
      float ly = curY;
      ApplyOrthoConstrainFromAnchor(cmd.trimCutInfP1x, cmd.trimCutInfP1y, &lx, &ly, orthoEnabled);
      PushRubberSeg(rubberLines, cmd.trimCutInfP1x, cmd.trimCutInfP1y, lx, ly);
      const float midx = (cmd.trimCutInfP1x + lx) * 0.5f;
      const float midy = (cmd.trimCutInfP1y + ly) * 0.5f;
      CadTrimAppendCutLineRemovedPreview(cmd, cmd.trimCutInfP1x, cmd.trimCutInfP1y, lx, ly, midx, midy, &previewLines);
    }

    std::vector<float> highlightLines;
    std::vector<float> highlightCircles;
    BuildSelectionHighlight(cmd, &highlightLines, &highlightCircles);

    std::vector<float> surveyMarkers;
    if (!cmd.surveyPoints.empty()) {
      const float surveyCrossHalf =
          SurveyPointCrossHalfWorldFromPaper(cmd.surveyPointCrossSpanPlottedInches, cmd.modelUnitsPerPlottedInch);
      AppendAllSurveyPointMarkers(surveyCrossHalf, cmd.surveyPoints, &surveyMarkers);
    }

    CadExtendedGeometryInput ext{};
    ext.arcs = &cmd.userArcs;
    ext.arcAttrs = &cmd.userArcAttrs;
    ext.ellipses = &cmd.userEllipses;
    ext.ellAttrs = &cmd.userEllAttrs;
    ext.polylineVerts = &cmd.userPolylineVerts;
    ext.polylineOffsets = &cmd.userPolylineOffsets;
    ext.polylineClosed = &cmd.userPolylineClosed;
    ext.polylineAttrs = &cmd.userPolylineAttrs;
    ext.drawingLayers = &cmd.drawingLayerTable;

    viewport.SetSize(fbW, fbH);
    viewport.RenderScene(panX, panY, zoom, fbW, fbH, cmd.userLinesFlat, cmd.userCirclesCxCyR, cmd.cadGpuRevision,
                         rubberLines, snapHit.valid ? &snapHit : nullptr,
                         std::clamp(cmd.objectSnapGlyphHalfPx, 3.f, 48.f), selRectPtr,
                         previewLines.empty() ? nullptr : &previewLines,
                         previewCircles.empty() ? nullptr : &previewCircles,
                         highlightLines.empty() ? nullptr : &highlightLines,
                         highlightCircles.empty() ? nullptr : &highlightCircles,
                         surveyMarkers.empty() ? nullptr : &surveyMarkers, &cmd.userLineAttrs,
                         &cmd.userCircleAttrs, &ext, gridVisible, &cmd.drawingLayerTable);

    ImGuiLayout_CommitDeferredIniLoadIfNeeded();
    ImGui::Render();
    int displayW = 0;
    int displayH = 0;
    glfwGetFramebufferSize(window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    glClearColor(0.06f, 0.06f, 0.07f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  CadUiClearMenuBarLogo();
  DestroyAppLogoGpu(&appLogo);
  ImGui::DestroyContext();

  viewport.Shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
