#include "CadCommands.hpp"
#include "CadUi.hpp"
#include "ViewportRenderer.hpp"
#include "CadSnap.hpp"
#include "SurveyPoints.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <vector>

namespace {

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
  for (const auto& e : cmd.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index) * 6;
      if (k + 5 >= cmd.userLinesFlat.size())
        continue;
      for (int i = 0; i < 2; ++i) {
        hlLines->push_back(cmd.userLinesFlat[k + i * 3]);
        hlLines->push_back(cmd.userLinesFlat[k + i * 3 + 1]);
        hlLines->push_back(kLineZ);
      }
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t k = static_cast<size_t>(e.index) * 3;
      if (k + 2 >= cmd.userCirclesCxCyR.size())
        continue;
      hlCircles->push_back(cmd.userCirclesCxCyR[k]);
      hlCircles->push_back(cmd.userCirclesCxCyR[k + 1]);
      hlCircles->push_back(cmd.userCirclesCxCyR[k + 2]);
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userArcs.size())
        continue;
      AppendArcPolylineStrip(hlLines, kLineZ, cmd.userArcs[k]);
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= cmd.userEllipses.size())
        continue;
      AppendEllipsePolylineStrip(hlLines, kLineZ, cmd.userEllipses[k]);
    } else if (e.type == SelectedEntity::Type::Polyline) {
      AppendCommittedPolylineStrip(hlLines, kLineZ, cmd, e.index);
    }
  }
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

  GLFWwindow* window = glfwCreateWindow(1600, 900, "GoSurvey — CAD", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMaximizeWindow(window);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  ViewportRenderer viewport;
  if (!viewport.Init()) {
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = "gosurvey_imgui.ini";
  io.ConfigInputTextEnterKeepActive = false; // CAD shell: Enter submits without selecting-all next keystroke

  ApplyCadDarkTheme();
  if (!LoadApplicationFont())
    std::fprintf(stderr, "Calibri not found; using ImGui default font.\n");
  io.FontGlobalScale = 1.05f;

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  AppCommandState cmd;
  std::vector<std::string> cmdLog;
  cmdLog.push_back("GoSurvey CAD shell ready.");
  cmdLog.push_back(
      "LINE/L … SURVEY: CREATEPOINTS (CRTPTS), VIEWPOINTS (VWPTS), IMPORTPOINTS (IMPPTS), EXPORTPOINTS (EXPPTS), "
      "JSON database — idle: two-click select. MMB "
      "pan.");
  char cmdBuf[512]{};

  float panX = 0.f;
  float panY = 0.f;
  float zoom = 1.f;
  float curX = 0.f;
  float curY = 0.f;
  float curRawX = 0.f;
  float curRawY = 0.f;
  int fbW = 900;
  int fbH = 650;

  bool dockLayoutDone = false;
  const float ribbonH = 130.f;
  bool objectSnapEnabled = true;
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
        objectSnapEnabled = !objectSnapEnabled;
      if (ImGui::IsKeyPressed(ImGuiKey_F8, false))
        orthoEnabled = !orthoEnabled;
    }
    cmd.orthoMode = orthoEnabled;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      if (cmd.copySurveyDupModalOpen) {
        ApplyCopySurveyDuplicateModalResult(cmd, false, cmdLog);
        cmdBuf[0] = '\0';
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

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                 ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("GoSurveyHost", nullptr, hostFlags);

    if (ImGui::BeginMenuBar()) {
      DrawMainMenuBar(cmd, cmdLog);
      ImGui::EndMenuBar();
    }

    DrawRibbonBar(ribbonH, cmd, cmdLog);

    ImGuiID dockspaceId = ImGui::GetID("GoSurveyDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.f, 0.f), 0);

    if (!dockLayoutDone) {
      dockLayoutDone = true;
      SetupMainDockLayout(dockspaceId);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);

    DrawPropertiesPanel(cmd);

    CadSnap::Hit snapHit{};
    DrawDrawingViewport(viewport.ColorTexture(), cmd, cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)), &panX,
                        &panY, &zoom, &curX, &curY, &curRawX, &curRawY, &fbW, &fbH, objectSnapEnabled, &snapHit);
    cmd.uiCursorWorldX = curX;
    cmd.uiCursorWorldY = curY;
    DrawCommandLinePanel(cmdLog, cmdBuf, static_cast<int>(sizeof(cmdBuf)), cmd, curX, curY, 0.f,
                         &objectSnapEnabled, &orthoEnabled, &gridVisible);
    DrawCreatePointsPanel(cmd, cmdLog);
    DrawSettingsPanel(cmd);
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

    if (cmd.active == AppCommandState::Kind::DimAligned &&
        cmd.dimPhase == AppCommandState::DimPhase::WaitExt2)
      PushRubberSeg(rubberLines, cmd.dimE1x, cmd.dimE1y, curX, curY);

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
    BuildTransformPreview(cmd, curX, curY, &previewLines, &previewCircles);

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
      const float halfH = (1.f / std::max(zoom, 1.e-4f)) * 50.f;
      AppendAllSurveyPointMarkers(halfH, fbH, cmd.surveyPoints, &surveyMarkers);
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

    viewport.SetSize(fbW, fbH);
    viewport.RenderScene(panX, panY, zoom, fbW, fbH, cmd.userLinesFlat, cmd.userCirclesCxCyR, cmd.cadGpuRevision,
                         rubberLines, snapHit.valid ? &snapHit : nullptr, selRectPtr,
                         previewLines.empty() ? nullptr : &previewLines,
                         previewCircles.empty() ? nullptr : &previewCircles,
                         highlightLines.empty() ? nullptr : &highlightLines,
                         highlightCircles.empty() ? nullptr : &highlightCircles,
                         surveyMarkers.empty() ? nullptr : &surveyMarkers, &cmd.userLineAttrs,
                         &cmd.userCircleAttrs, &ext, gridVisible);

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
  ImGui::DestroyContext();

  viewport.Shutdown();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
