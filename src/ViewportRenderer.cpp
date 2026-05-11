#include "ViewportRenderer.hpp"

#include "CadSnap.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

const char* kLineVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kLineFs = R"(#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() {
  FragColor = uColor;
}
)";

const char* kLineVcVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uMVP;
out vec4 vColor;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
  vColor = aColor;
}
)";

const char* kLineVcFs = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() {
  FragColor = vColor;
}
)";

GLuint CompileShader(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint LinkProgram(GLuint vs, GLuint fs) {
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  glDetachShader(p, vs);
  glDetachShader(p, fs);
  glDeleteShader(vs);
  glDeleteShader(fs);
  if (!ok) {
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

void MulMat4(const float* a, const float* b, float* out) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                       a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
}

void TranslateMat(float tx, float ty, float tz, float* m) {
  std::memset(m, 0, sizeof(float) * 16);
  m[0] = m[5] = m[10] = m[15] = 1.f;
  m[12] = tx;
  m[13] = ty;
  m[14] = tz;
}

void AppendCircleLineApprox(std::vector<float>& out, float cx, float cy, float r, int segments, float z) {
  if (r <= 1e-6f)
    return;
  const float twoPi = 6.28318530718f;
  for (int i = 0; i < segments; ++i) {
    const float t0 = twoPi * static_cast<float>(i) / static_cast<float>(segments);
    const float t1 = twoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
    out.push_back(cx + r * std::cos(t0));
    out.push_back(cy + r * std::sin(t0));
    out.push_back(z);
    out.push_back(cx + r * std::cos(t1));
    out.push_back(cy + r * std::sin(t1));
    out.push_back(z);
  }
}

void AppendCircleLineApproxVc(std::vector<float>& out, float cx, float cy, float r, int segments, float z,
                              const float* rgba) {
  if (r <= 1e-6f)
    return;
  const float twoPi = 6.28318530718f;
  for (int i = 0; i < segments; ++i) {
    const float t0 = twoPi * static_cast<float>(i) / static_cast<float>(segments);
    const float t1 = twoPi * static_cast<float>(i + 1) / static_cast<float>(segments);
    const float x0 = cx + r * std::cos(t0);
    const float y0 = cy + r * std::sin(t0);
    const float x1 = cx + r * std::cos(t1);
    const float y1 = cy + r * std::sin(t1);
    out.push_back(x0);
    out.push_back(y0);
    out.push_back(z);
    out.push_back(rgba[0]);
    out.push_back(rgba[1]);
    out.push_back(rgba[2]);
    out.push_back(rgba[3]);
    out.push_back(x1);
    out.push_back(y1);
    out.push_back(z);
    out.push_back(rgba[0]);
    out.push_back(rgba[1]);
    out.push_back(rgba[2]);
    out.push_back(rgba[3]);
  }
}

void AppendArcLineApproxVc(std::vector<float>& out, const CadArc& a, int segments, float z,
                           const float* rgba) {
  if (a.r <= 1e-6f || segments < 2)
    return;
  for (int i = 0; i < segments; ++i) {
    const float u0 = static_cast<float>(i) / static_cast<float>(segments);
    const float u1 = static_cast<float>(i + 1) / static_cast<float>(segments);
    const float t0 = a.startRad + a.sweepRad * u0;
    const float t1 = a.startRad + a.sweepRad * u1;
    const float x0 = a.cx + a.r * std::cos(t0);
    const float y0 = a.cy + a.r * std::sin(t0);
    const float x1 = a.cx + a.r * std::cos(t1);
    const float y1 = a.cy + a.r * std::sin(t1);
    out.push_back(x0);
    out.push_back(y0);
    out.push_back(z);
    out.push_back(rgba[0]);
    out.push_back(rgba[1]);
    out.push_back(rgba[2]);
    out.push_back(rgba[3]);
    out.push_back(x1);
    out.push_back(y1);
    out.push_back(z);
    out.push_back(rgba[0]);
    out.push_back(rgba[1]);
    out.push_back(rgba[2]);
    out.push_back(rgba[3]);
  }
}

void AppendEllipseLineApproxVc(std::vector<float>& out, const CadEllipse& el, int segments, float z,
                               const float* rgba) {
  const float ma = std::hypot(el.majVx, el.majVy);
  if (ma < 1e-8f || segments < 3)
    return;
  const float ux = el.majVx / ma;
  const float uy = el.majVy / ma;
  const float px = -uy;
  const float py = ux;
  const float mb = ma * el.ratio;
  constexpr float twopi = 6.28318530718f;
  for (int i = 0; i < segments; ++i) {
    const float u0 = twopi * static_cast<float>(i) / static_cast<float>(segments);
    const float u1 = twopi * static_cast<float>(i + 1) / static_cast<float>(segments);
    const float c0 = std::cos(u0);
    const float s0 = std::sin(u0);
    const float c1 = std::cos(u1);
    const float s1 = std::sin(u1);
    const float x0 = el.cx + ux * (ma * c0) + px * (mb * s0);
    const float y0 = el.cy + uy * (ma * c0) + py * (mb * s0);
    const float x1 = el.cx + ux * (ma * c1) + px * (mb * s1);
    const float y1 = el.cy + uy * (ma * c1) + py * (mb * s1);
    out.push_back(x0);
    out.push_back(y0);
    out.push_back(z);
    out.push_back(rgba[0]);
    out.push_back(rgba[1]);
    out.push_back(rgba[2]);
    out.push_back(rgba[3]);
    out.push_back(x1);
    out.push_back(y1);
    out.push_back(z);
    out.push_back(rgba[0]);
    out.push_back(rgba[1]);
    out.push_back(rgba[2]);
    out.push_back(rgba[3]);
  }
}

void AppendPolylineEdgesVc(std::vector<float>& out, const CadExtendedGeometryInput& eg, float z, float defR,
                           float defG, float defB) {
  const auto* V = eg.polylineVerts;
  const auto* O = eg.polylineOffsets;
  const auto* Cl = eg.polylineClosed;
  const auto* At = eg.polylineAttrs;
  if (!V || !O || O->size() < 2)
    return;
  const int np = static_cast<int>(O->size()) - 1;
  auto edge = [&](float x0, float y0, float x1, float y1, const float* rgba) {
    out.push_back(x0);
    out.push_back(y0);
    out.push_back(z);
    out.push_back(rgba[0]);
    out.push_back(rgba[1]);
    out.push_back(rgba[2]);
    out.push_back(rgba[3]);
    out.push_back(x1);
    out.push_back(y1);
    out.push_back(z);
    out.push_back(rgba[0]);
    out.push_back(rgba[1]);
    out.push_back(rgba[2]);
    out.push_back(rgba[3]);
  };
  for (int pi = 0; pi < np; ++pi) {
    EntityAttributes attr{};
    if (At && static_cast<size_t>(pi) < At->size())
      attr = (*At)[static_cast<size_t>(pi)];
    float rgba[4];
    ResolveEntityColorForViewport(attr, defR, defG, defB, rgba);
    const int v0 = (*O)[static_cast<size_t>(pi)];
    const int v1 = (*O)[static_cast<size_t>(pi + 1)];
    if (v0 >= v1)
      continue;
    const bool closed =
        Cl && static_cast<size_t>(pi) < Cl->size() && (*Cl)[static_cast<size_t>(pi)] != 0;
    for (int vi = v0; vi + 1 < v1; ++vi) {
      const float x0 = (*V)[static_cast<size_t>(vi * 3 + 0)];
      const float y0 = (*V)[static_cast<size_t>(vi * 3 + 1)];
      const float x1 = (*V)[static_cast<size_t>((vi + 1) * 3 + 0)];
      const float y1 = (*V)[static_cast<size_t>((vi + 1) * 3 + 1)];
      edge(x0, y0, x1, y1, rgba);
    }
    if (closed && v1 - v0 >= 2) {
      const float x0 = (*V)[static_cast<size_t>((v1 - 1) * 3 + 0)];
      const float y0 = (*V)[static_cast<size_t>((v1 - 1) * 3 + 1)];
      const float x1 = (*V)[static_cast<size_t>(v0 * 3 + 0)];
      const float y1 = (*V)[static_cast<size_t>(v0 * 3 + 1)];
      edge(x0, y0, x1, y1, rgba);
    }
  }
}

void AppendSnapSquareOutline(std::vector<float>& out, float cx, float cy, float z, float h) {
  auto seg = [&](float x0, float y0, float x1, float y1) {
    out.push_back(x0);
    out.push_back(y0);
    out.push_back(z);
    out.push_back(x1);
    out.push_back(y1);
    out.push_back(z);
  };
  seg(cx - h, cy - h, cx + h, cy - h);
  seg(cx + h, cy - h, cx + h, cy + h);
  seg(cx + h, cy + h, cx - h, cy + h);
  seg(cx - h, cy + h, cx - h, cy - h);
}

void AppendSnapTriangleOutline(std::vector<float>& out, float cx, float cy, float z, float h) {
  auto seg = [&](float x0, float y0, float x1, float y1) {
    out.push_back(x0);
    out.push_back(y0);
    out.push_back(z);
    out.push_back(x1);
    out.push_back(y1);
    out.push_back(z);
  };
  seg(cx, cy + h, cx - h, cy - h);
  seg(cx - h, cy - h, cx + h, cy - h);
  seg(cx + h, cy - h, cx, cy + h);
}

void AppendSnapCrossInSquare(std::vector<float>& out, float cx, float cy, float z, float h) {
  out.insert(out.end(),
             {cx - h, cy, z, cx + h, cy, z, cx, cy - h, z, cx, cy + h, z});
}

void AppendWorldRectOutline(std::vector<float>& o, float xa, float ya, float xb, float yb, float z) {
  const float mnX = std::min(xa, xb);
  const float mxX = std::max(xa, xb);
  const float mnY = std::min(ya, yb);
  const float mxY = std::max(ya, yb);
  auto seg = [&](float x0, float y0, float x1, float y1) {
    o.push_back(x0);
    o.push_back(y0);
    o.push_back(z);
    o.push_back(x1);
    o.push_back(y1);
    o.push_back(z);
  };
  seg(mnX, mnY, mxX, mnY);
  seg(mxX, mnY, mxX, mxY);
  seg(mxX, mxY, mnX, mxY);
  seg(mnX, mxY, mnX, mnY);
}

void AppendWorldRectFillTris(std::vector<float>& o, float xa, float ya, float xb, float yb, float z) {
  const float mnX = std::min(xa, xb);
  const float mxX = std::max(xa, xb);
  const float mnY = std::min(ya, yb);
  const float mxY = std::max(ya, yb);
  const float tri[] = {
      mnX, mnY, z, mxX, mnY, z, mxX, mxY, z, mnX, mnY, z, mxX, mxY, z, mnX, mxY, z,
  };
  o.insert(o.end(), tri, tri + sizeof(tri) / sizeof(tri[0]));
}

void BuildSnapOverlayLines(const CadSnap::Hit& snap, float halfWorld, int fbHeight, std::vector<float>& out) {
  if (!snap.valid)
    return;
  const float zSnap = 0.045f;
  const float mh =
      7.f * (2.f * halfWorld) / static_cast<float>(std::max(fbHeight, 1)); // ~7 px half-extent
  switch (snap.kind) {
  case CadSnap::Kind::Endpoint:
    AppendSnapSquareOutline(out, snap.x, snap.y, zSnap, mh);
    break;
  case CadSnap::Kind::Midpoint:
    AppendSnapTriangleOutline(out, snap.x, snap.y, zSnap, mh);
    break;
  case CadSnap::Kind::Center:
    AppendCircleLineApprox(out, snap.x, snap.y, mh * 0.85f, 28, zSnap);
    break;
  case CadSnap::Kind::Perpendicular:
    AppendSnapSquareOutline(out, snap.x, snap.y, zSnap, mh);
    AppendSnapCrossInSquare(out, snap.x, snap.y, zSnap, mh * 0.55f);
    break;
  }
}

} // namespace

bool ViewportRenderer::Init() {
  if (glewInit() != GLEW_OK)
    return false;
  // GLEW fires GL_INVALID_ENUM on core contexts; clear once (common workaround).
  glGetError();
  return EnsureShader();
}

void ViewportRenderer::Shutdown() {
  DestroyFramebuffer();
  DestroyShader();
}

void ViewportRenderer::Ortho(float left, float right, float bottom, float top, float nearp, float farp,
                             float* m) {
  std::memset(m, 0, sizeof(float) * 16);
  // Column-major (OpenGL): columns 0–2 are scale/shear; column 3 is translation.
  m[0] = 2.f / (right - left);
  m[5] = 2.f / (top - bottom);
  m[10] = -2.f / (farp - nearp);
  m[12] = -(right + left) / (right - left);
  m[13] = -(top + bottom) / (top - bottom);
  m[14] = -(farp + nearp) / (farp - nearp);
  m[15] = 1.f;
}

bool ViewportRenderer::EnsureShader() {
  if (lineProgram_)
    return true;

  GLuint vs = CompileShader(GL_VERTEX_SHADER, kLineVs);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kLineFs);
  if (!vs || !fs)
    return false;
  lineProgram_ = LinkProgram(vs, fs);
  if (!lineProgram_)
    return false;

  gridProgram_ = lineProgram_;

  GLuint vcVs = CompileShader(GL_VERTEX_SHADER, kLineVcVs);
  GLuint vcFs = CompileShader(GL_FRAGMENT_SHADER, kLineVcFs);
  if (!vcVs || !vcFs)
    return false;
  vcLineProgram_ = LinkProgram(vcVs, vcFs);
  if (!vcLineProgram_)
    return false;

  glGenVertexArrays(1, &vaoLines_);
  glGenBuffers(1, &vboLines_);

  glGenVertexArrays(1, &vaoVcLines_);
  glGenBuffers(1, &vboVcLines_);
  glBindVertexArray(vaoVcLines_);
  glBindBuffer(GL_ARRAY_BUFFER, vboVcLines_);
  const GLsizei vcStride = static_cast<GLsizei>(7 * sizeof(float));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vcStride, nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, vcStride, reinterpret_cast<const void*>(sizeof(float) * 3));
  glBindVertexArray(0);

  glGenVertexArrays(1, &vaoVcCircles_);
  glGenBuffers(1, &vboVcCircles_);
  glBindVertexArray(vaoVcCircles_);
  glBindBuffer(GL_ARRAY_BUFFER, vboVcCircles_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vcStride, nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, vcStride, reinterpret_cast<const void*>(sizeof(float) * 3));
  glBindVertexArray(0);

  glGenVertexArrays(1, &vaoGrid_);
  glGenBuffers(1, &vboGrid_);

  return true;
}

void ViewportRenderer::DestroyShader() {
  if (vboGrid_) {
    glDeleteBuffers(1, &vboGrid_);
    vboGrid_ = 0;
  }
  if (vaoGrid_) {
    glDeleteVertexArrays(1, &vaoGrid_);
    vaoGrid_ = 0;
  }
  if (vboVcCircles_) {
    glDeleteBuffers(1, &vboVcCircles_);
    vboVcCircles_ = 0;
  }
  if (vaoVcCircles_) {
    glDeleteVertexArrays(1, &vaoVcCircles_);
    vaoVcCircles_ = 0;
  }
  if (vboVcLines_) {
    glDeleteBuffers(1, &vboVcLines_);
    vboVcLines_ = 0;
  }
  if (vaoVcLines_) {
    glDeleteVertexArrays(1, &vaoVcLines_);
    vaoVcLines_ = 0;
  }
  if (vboLines_) {
    glDeleteBuffers(1, &vboLines_);
    vboLines_ = 0;
  }
  if (vaoLines_) {
    glDeleteVertexArrays(1, &vaoLines_);
    vaoLines_ = 0;
  }
  gridProgram_ = 0;
  if (vcLineProgram_) {
    glDeleteProgram(vcLineProgram_);
    vcLineProgram_ = 0;
  }
  if (lineProgram_) {
    glDeleteProgram(lineProgram_);
    lineProgram_ = 0;
  }
}

void ViewportRenderer::DestroyFramebuffer() {
  if (colorTex_) {
    glDeleteTextures(1, &colorTex_);
    colorTex_ = 0;
  }
  if (rbo_) {
    glDeleteRenderbuffers(1, &rbo_);
    rbo_ = 0;
  }
  if (fbo_) {
    glDeleteFramebuffers(1, &fbo_);
    fbo_ = 0;
  }
  fbW_ = fbH_ = 0;
}

bool ViewportRenderer::EnsureFramebuffer(int w, int h) {
  if (w <= 0 || h <= 0)
    return false;
  if (fbo_ && w == fbW_ && h == fbH_)
    return true;

  DestroyFramebuffer();
  fbW_ = w;
  fbH_ = h;

  glGenFramebuffers(1, &fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

  glGenTextures(1, &colorTex_);
  glBindTexture(GL_TEXTURE_2D, colorTex_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);

  glGenRenderbuffers(1, &rbo_);
  glBindRenderbuffer(GL_RENDERBUFFER, rbo_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo_);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return status == GL_FRAMEBUFFER_COMPLETE;
}

void ViewportRenderer::SetSize(int width, int height) {
  EnsureFramebuffer(width, height);
}

void ViewportRenderer::RenderScene(float panX, float panY, float zoom, int fbWidth, int fbHeight,
                                   const std::vector<float>& userLines, const std::vector<float>& circlesCxCyR,
                                   std::uint32_t cadGpuRevision, const std::vector<float>& rubberLines,
                                   const CadSnap::Hit* snapOverlay, const float* selectionFillRect,
                                   const std::vector<float>* previewLines, const std::vector<float>* previewCircles,
                                   const std::vector<float>* highlightLines, const std::vector<float>* highlightCircles,
                                   const std::vector<float>* surveyMarkers,
                                   const std::vector<EntityAttributes>* lineEntityAttrs,
                                   const std::vector<EntityAttributes>* circleEntityAttrs,
                                   const CadExtendedGeometryInput* extended, bool showGrid) {
  if (!EnsureFramebuffer(fbWidth, fbHeight))
    return;

  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, fbW_, fbH_);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);

  glClearColor(0.f, 0.f, 0.f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  const float aspect = static_cast<float>(fbW_) / static_cast<float>(std::max(fbH_, 1));
  const float halfH = (1.f / std::max(zoom, 1.e-4f)) * 50.f;
  const float halfW = halfH * aspect;
  float proj[16];
  Ortho(-halfW + panX, halfW + panX, -halfH + panY, halfH + panY, -1000.f, 1000.f, proj);

  float model[16];
  TranslateMat(0.f, 0.f, 0.f, model);

  float mvp[16];
  MulMat4(proj, model, mvp);

  GLint locMvp = glGetUniformLocation(lineProgram_, "uMVP");
  GLint locCol = glGetUniformLocation(lineProgram_, "uColor");

  glUseProgram(gridProgram_);
  glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);

  // --- Grid (drawn first; translucent, painter order keeps it behind geometry) ---
  if (showGrid) {
    if (gridVertexCount_ == 0) {
      std::vector<float> gridVerts;
      const float step = 10.f;
      const int n = 80;
      const float gz = -0.02f;
      for (int i = -n; i <= n; ++i) {
        float x = static_cast<float>(i) * step;
        gridVerts.push_back(x);
        gridVerts.push_back(static_cast<float>(-n) * step);
        gridVerts.push_back(gz);
        gridVerts.push_back(x);
        gridVerts.push_back(static_cast<float>(n) * step);
        gridVerts.push_back(gz);

        float y = static_cast<float>(i) * step;
        gridVerts.push_back(static_cast<float>(-n) * step);
        gridVerts.push_back(y);
        gridVerts.push_back(gz);
        gridVerts.push_back(static_cast<float>(n) * step);
        gridVerts.push_back(y);
        gridVerts.push_back(gz);
      }
      gridVertexCount_ = static_cast<int>(gridVerts.size() / 3);

      glBindVertexArray(vaoGrid_);
      glBindBuffer(GL_ARRAY_BUFFER, vboGrid_);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(gridVerts.size() * sizeof(float)),
                   gridVerts.data(), GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
      glBindVertexArray(0);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(locCol, 0.14f, 0.14f, 0.15f, 0.28f);
    glBindVertexArray(vaoGrid_);
    glDrawArrays(GL_LINES, 0, gridVertexCount_);
    glDisable(GL_BLEND);
  }

  glBindBuffer(GL_ARRAY_BUFFER, vboLines_);
  glBindVertexArray(vaoLines_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
  glLineWidth(2.f);

  constexpr float kLineDefaultR = 0.35f;
  constexpr float kLineDefaultG = 0.95f;
  constexpr float kLineDefaultB = 1.f;
  constexpr float kCircDefaultR = 0.92f;
  constexpr float kCircDefaultG = 0.55f;
  constexpr float kCircDefaultB = 1.f;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // --- Committed lines + circles (single batched draw each; per-vertex color shader; GPU cache keyed by cadGpuRevision)
  const bool hasLines = !userLines.empty() && userLines.size() % 6 == 0;
  const bool hasCircles = !circlesCxCyR.empty() && circlesCxCyR.size() % 3 == 0;
  const bool hasExt =
      extended &&
      (((extended->arcs != nullptr) && !extended->arcs->empty()) ||
       ((extended->ellipses != nullptr) && !extended->ellipses->empty()) ||
       (extended->polylineVerts != nullptr && extended->polylineOffsets != nullptr &&
        extended->polylineOffsets->size() >= 2));
  if (hasLines || hasCircles || hasExt) {
    if (cadGpuRevision != cachedCadGpuRevision_) {
      cpuVcLines_.clear();
      cpuVcCircles_.clear();
      if (hasLines) {
        const size_t nSeg = userLines.size() / 6;
        cpuVcLines_.resize(nSeg * 14);
        for (size_t i = 0; i < nSeg; ++i) {
          EntityAttributes attr{};
          if (lineEntityAttrs && i < lineEntityAttrs->size())
            attr = (*lineEntityAttrs)[i];
          float rgba[4];
          ResolveEntityColorForViewport(attr, kLineDefaultR, kLineDefaultG, kLineDefaultB, rgba);
          float* dst = &cpuVcLines_[i * 14];
          dst[0] = userLines[i * 6 + 0];
          dst[1] = userLines[i * 6 + 1];
          dst[2] = userLines[i * 6 + 2];
          dst[3] = rgba[0];
          dst[4] = rgba[1];
          dst[5] = rgba[2];
          dst[6] = rgba[3];
          dst[7] = userLines[i * 6 + 3];
          dst[8] = userLines[i * 6 + 4];
          dst[9] = userLines[i * 6 + 5];
          dst[10] = rgba[0];
          dst[11] = rgba[1];
          dst[12] = rgba[2];
          dst[13] = rgba[3];
        }
      }
      if (extended) {
        if (extended->arcs) {
          for (size_t i = 0; i < extended->arcs->size(); ++i) {
            EntityAttributes attr{};
            if (extended->arcAttrs && i < extended->arcAttrs->size())
              attr = (*extended->arcAttrs)[i];
            float rgba[4];
            ResolveEntityColorForViewport(attr, kLineDefaultR, kLineDefaultG, kLineDefaultB, rgba);
            AppendArcLineApproxVc(cpuVcLines_, (*extended->arcs)[i], 48, 0.f, rgba);
          }
        }
        if (extended->ellipses) {
          for (size_t i = 0; i < extended->ellipses->size(); ++i) {
            EntityAttributes attr{};
            if (extended->ellAttrs && i < extended->ellAttrs->size())
              attr = (*extended->ellAttrs)[i];
            float rgba[4];
            ResolveEntityColorForViewport(attr, kLineDefaultR, kLineDefaultG, kLineDefaultB, rgba);
            AppendEllipseLineApproxVc(cpuVcLines_, (*extended->ellipses)[i], 72, 0.f, rgba);
          }
        }
        if (extended->polylineVerts && extended->polylineOffsets) {
          AppendPolylineEdgesVc(cpuVcLines_, *extended, 0.f, kLineDefaultR, kLineDefaultG, kLineDefaultB);
        }
      }
      if (hasCircles) {
        const size_t nCirc = circlesCxCyR.size() / 3;
        for (size_t ci = 0; ci < nCirc; ++ci) {
          EntityAttributes attr{};
          if (circleEntityAttrs && ci < circleEntityAttrs->size())
            attr = (*circleEntityAttrs)[ci];
          float rgba[4];
          ResolveEntityColorForViewport(attr, kCircDefaultR, kCircDefaultG, kCircDefaultB, rgba);
          AppendCircleLineApproxVc(cpuVcCircles_, circlesCxCyR[ci * 3], circlesCxCyR[ci * 3 + 1],
                                   circlesCxCyR[ci * 3 + 2], 72, 0.f, rgba);
        }
      }
      glBindBuffer(GL_ARRAY_BUFFER, vboVcLines_);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuVcLines_.size() * sizeof(float)),
                   cpuVcLines_.empty() ? nullptr : cpuVcLines_.data(), GL_DYNAMIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, vboVcCircles_);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuVcCircles_.size() * sizeof(float)),
                   cpuVcCircles_.empty() ? nullptr : cpuVcCircles_.data(), GL_DYNAMIC_DRAW);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      cachedCadGpuRevision_ = cadGpuRevision;
    }

    glUseProgram(vcLineProgram_);
    GLint locVcMvp = glGetUniformLocation(vcLineProgram_, "uMVP");
    glUniformMatrix4fv(locVcMvp, 1, GL_FALSE, mvp);
    glLineWidth(2.f);
    if (!cpuVcLines_.empty()) {
      glBindVertexArray(vaoVcLines_);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(cpuVcLines_.size() / 7));
    }
    if (!cpuVcCircles_.empty()) {
      glBindVertexArray(vaoVcCircles_);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(cpuVcCircles_.size() / 7));
    }
    glBindVertexArray(0);
    glUseProgram(lineProgram_);
  }

  glBindBuffer(GL_ARRAY_BUFFER, vboLines_);
  glBindVertexArray(vaoLines_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);

  glDisable(GL_BLEND);

  // --- Selection highlight (accent stroke on top of committed geometry) ---
  if (highlightLines && !highlightLines->empty() && highlightLines->size() % 6 == 0) {
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUniform4f(locCol, 1.f, 0.92f, 0.15f, 1.f);
    glLineWidth(3.75f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(highlightLines->size() * sizeof(float)),
                 highlightLines->data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(highlightLines->size() / 3));
    glLineWidth(2.f);
  }
  if (highlightCircles && !highlightCircles->empty() && highlightCircles->size() % 3 == 0) {
    std::vector<float> hlCircGeom;
    for (size_t i = 0; i + 2 < highlightCircles->size(); i += 3)
      AppendCircleLineApprox(hlCircGeom, (*highlightCircles)[i], (*highlightCircles)[i + 1],
                             (*highlightCircles)[i + 2], 80, 0.018f);
    if (!hlCircGeom.empty()) {
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glUniform4f(locCol, 1.f, 0.88f, 0.22f, 1.f);
      glLineWidth(3.5f);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(hlCircGeom.size() * sizeof(float)), hlCircGeom.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(hlCircGeom.size() / 3));
      glLineWidth(2.f);
    }
  }

  // --- Rubber previews (LINE segment + CIRCLE construction aids) ---
  if (!rubberLines.empty() && rubberLines.size() % 6 == 0) {
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUniform4f(locCol, 1.f, 0.85f, 0.2f, 1.f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(rubberLines.size() * sizeof(float)),
                 rubberLines.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(rubberLines.size() / 3));
  }

  // --- Window selection preview (semi-transparent fill + outline) ---
  if (selectionFillRect) {
    const float xa = selectionFillRect[0];
    const float xb = selectionFillRect[1];
    const float ya = selectionFillRect[2];
    const float yb = selectionFillRect[3];
    std::vector<float> fillGeom;
    std::vector<float> lineGeom;
    AppendWorldRectFillTris(fillGeom, xa, ya, xb, yb, 0.035f);
    AppendWorldRectOutline(lineGeom, xa, ya, xb, yb, 0.036f);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!fillGeom.empty()) {
      glUniform4f(locCol, 0.25f, 0.55f, 1.f, 0.22f);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fillGeom.size() * sizeof(float)), fillGeom.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fillGeom.size() / 3));
    }
    glUniform4f(locCol, 0.45f, 0.78f, 1.f, 0.9f);
    glLineWidth(1.25f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lineGeom.size() * sizeof(float)), lineGeom.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineGeom.size() / 3));
    glDisable(GL_BLEND);
    glLineWidth(2.f);
  }

  // --- Move/copy/rotate preview geometry ---
  if (previewLines && !previewLines->empty() && previewLines->size() % 6 == 0) {
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(locCol, 1.f, 0.88f, 0.35f, 0.55f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(previewLines->size() * sizeof(float)),
                 previewLines->data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(previewLines->size() / 3));
    glDisable(GL_BLEND);
  }
  if (previewCircles && !previewCircles->empty() && previewCircles->size() % 3 == 0) {
    std::vector<float> circleGeom;
    for (size_t i = 0; i + 2 < previewCircles->size(); i += 3)
      AppendCircleLineApprox(circleGeom, (*previewCircles)[i], (*previewCircles)[i + 1], (*previewCircles)[i + 2],
                             72, 0.032f);
    if (!circleGeom.empty()) {
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glUniform4f(locCol, 1.f, 0.55f, 0.95f, 0.55f);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(circleGeom.size() * sizeof(float)), circleGeom.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(circleGeom.size() / 3));
      glDisable(GL_BLEND);
    }
  }

  // --- Survey points (X markers, apparent size ~constant on screen) ---
  if (surveyMarkers && !surveyMarkers->empty() && surveyMarkers->size() % 6 == 0) {
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUseProgram(lineProgram_);
    glUniform4f(locCol, 1.f, 0.48f, 0.12f, 1.f);
    glLineWidth(2.35f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(surveyMarkers->size() * sizeof(float)),
                 surveyMarkers->data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(surveyMarkers->size() / 3));
    glLineWidth(2.f);
  }

  // --- Object snap glyph (green, screen-stable size) ---
  if (snapOverlay && snapOverlay->valid) {
    std::vector<float> snapGeom;
    BuildSnapOverlayLines(*snapOverlay, halfH, fbH_, snapGeom);
    if (!snapGeom.empty()) {
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glUniform4f(locCol, 0.15f, 0.92f, 0.38f, 1.f);
      glLineWidth(2.f);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(snapGeom.size() * sizeof(float)), snapGeom.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(snapGeom.size() / 3));
    }
  }

  // --- Axes gizmo (screen-fixed pixels: ignores pan/zoom) ---
  float overlayProj[16];
  Ortho(0.f, static_cast<float>(fbW_), 0.f, static_cast<float>(fbH_), -1000.f, 1000.f, overlayProj);
  constexpr float kGizmoMarginPx = 5.f;
  constexpr float kAxisLenPx = 70.f;
  float gizmoModel[16];
  TranslateMat(kGizmoMarginPx, kGizmoMarginPx, 0.f, gizmoModel);
  float gizmoMvp[16];
  MulMat4(overlayProj, gizmoModel, gizmoMvp);
  glUniformMatrix4fv(locMvp, 1, GL_FALSE, gizmoMvp);

  const float axisVerts[] = {
      0.f, 0.f, 0.f, kAxisLenPx, 0.f, 0.f,
      0.f, 0.f, 0.f, 0.f, kAxisLenPx, 0.f,
      0.f, 0.f, 0.f, 0.f, 0.f, kAxisLenPx,
  };
  glBufferData(GL_ARRAY_BUFFER, sizeof(axisVerts), axisVerts, GL_STREAM_DRAW);

  glLineWidth(1.25f);
  glUniform4f(locCol, 0.9f, 0.2f, 0.2f, 1.f);
  glDrawArrays(GL_LINES, 0, 2);
  glUniform4f(locCol, 0.2f, 0.85f, 0.35f, 1.f);
  glDrawArrays(GL_LINES, 2, 2);
  glUniform4f(locCol, 0.25f, 0.55f, 1.f, 1.f);
  glDrawArrays(GL_LINES, 4, 2);
  glLineWidth(2.f);

  glBindVertexArray(0);
  glUseProgram(0);
  glDepthMask(GL_TRUE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
