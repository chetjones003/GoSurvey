#include "TransformPreview.hpp"

#include "CadCommands.hpp"
#include "geom2d.hpp"

#include <cmath>
#include <vector>

namespace {

void rotatePreviewPt(float baseX, float baseY, float angleRad, float* inOutX, float* inOutY) {
  const float c = std::cos(angleRad);
  const float s = std::sin(angleRad);
  const float dx = *inOutX - baseX;
  const float dy = *inOutY - baseY;
  *inOutX = baseX + c * dx - s * dy;
  *inOutY = baseY + s * dx + c * dy;
}

void scalePreviewPt(float baseX, float baseY, float scale, float* inOutX, float* inOutY) {
  *inOutX = baseX + scale * (*inOutX - baseX);
  *inOutY = baseY + scale * (*inOutY - baseY);
}

void appendArcPolylineStrip(std::vector<float>* out, float z, const CadArc& a, int n) {
  AppendArcLineSegments(*out, static_cast<double>(a.cx), static_cast<double>(a.cy), static_cast<double>(a.r),
                        static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), n, z);
}

void appendEllipsePolylineStrip(std::vector<float>* out, float z, const CadEllipse& el, int n) {
  AppendEllipseLineSegments(*out, static_cast<double>(el.cx), static_cast<double>(el.cy),
                            static_cast<double>(el.majVx), static_cast<double>(el.majVy),
                            static_cast<double>(el.ratio), n, z);
}

void appendCommittedPolylineStrip(std::vector<float>* out, float z, const AppCommandState& cmd, int pi) {
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

} // namespace

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
        appendArcPolylineStrip(prevLines, 0.f, a, 48);
      } else if (e.type == SelectedEntity::Type::Ellipse) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userEllipses.size())
          continue;
        CadEllipse el = cmd.userEllipses[k];
        el.cx += dx;
        el.cy += dy;
        appendEllipsePolylineStrip(prevLines, 0.f, el, 56);
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
          scalePreviewPt(bx, by, sc, &x, &y);
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
        scalePreviewPt(bx, by, sc, &x, &y);
        r *= sc;
        prevCircles->push_back(x);
        prevCircles->push_back(y);
        prevCircles->push_back(r);
      } else if (e.type == SelectedEntity::Type::Arc) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userArcs.size())
          continue;
        CadArc a = cmd.userArcs[k];
        scalePreviewPt(bx, by, sc, &a.cx, &a.cy);
        a.r *= sc;
        appendArcPolylineStrip(prevLines, 0.f, a, 48);
      } else if (e.type == SelectedEntity::Type::Ellipse) {
        const size_t k = static_cast<size_t>(e.index);
        if (k >= cmd.userEllipses.size())
          continue;
        CadEllipse el = cmd.userEllipses[k];
        float mx = el.cx + el.majVx;
        float my = el.cy + el.majVy;
        scalePreviewPt(bx, by, sc, &el.cx, &el.cy);
        scalePreviewPt(bx, by, sc, &mx, &my);
        el.majVx = mx - el.cx;
        el.majVy = my - el.cy;
        appendEllipsePolylineStrip(prevLines, 0.f, el, 56);
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
          scalePreviewPt(bx, by, sc, &x0, &y0);
          scalePreviewPt(bx, by, sc, &x1, &y1);
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
          scalePreviewPt(bx, by, sc, &x0, &y0);
          scalePreviewPt(bx, by, sc, &x1, &y1);
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
        rotatePreviewPt(bx, by, theta, &x, &y);
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
      rotatePreviewPt(bx, by, theta, &x, &y);
      prevCircles->push_back(x);
      prevCircles->push_back(y);
      prevCircles->push_back(cmd.userCirclesCxCyR[k + 2]);
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userArcs.size())
        continue;
      CadArc a = cmd.userArcs[k];
      rotatePreviewPt(bx, by, theta, &a.cx, &a.cy);
      a.startRad += theta;
      appendArcPolylineStrip(prevLines, 0.f, a, 48);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userEllipses.size())
        continue;
      CadEllipse el = cmd.userEllipses[k];
      float mx = el.cx + el.majVx;
      float my = el.cy + el.majVy;
      rotatePreviewPt(bx, by, theta, &el.cx, &el.cy);
      rotatePreviewPt(bx, by, theta, &mx, &my);
      el.majVx = mx - el.cx;
      el.majVy = my - el.cy;
      appendEllipsePolylineStrip(prevLines, 0.f, el, 56);
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
        rotatePreviewPt(bx, by, theta, &x0, &y0);
        rotatePreviewPt(bx, by, theta, &x1, &y1);
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
        rotatePreviewPt(bx, by, theta, &x0, &y0);
        rotatePreviewPt(bx, by, theta, &x1, &y1);
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
      appendArcPolylineStrip(hlLines, kLineZ, cmd.userArcs[k], 48);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userEllipses.size())
        return;
      appendEllipsePolylineStrip(hlLines, kLineZ, cmd.userEllipses[k], 56);
    } else if (e.type == SelectedEntity::Type::Polyline) {
      appendCommittedPolylineStrip(hlLines, kLineZ, cmd, e.index);
    }
  };
  for (const auto& e : cmd.selection)
    appendOne(e);
  if (cmd.active == AppCommandState::Kind::Offset && cmd.offsetPhase == AppCommandState::OffsetPhase::WaitSelectEntity &&
      cmd.offsetHoverHighlightValid)
    appendOne(cmd.offsetHoverEntity);
}
