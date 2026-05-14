#include "ViewportRenderer.hpp"

#include "CadLinetype.hpp"
#include "CadSnap.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
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

const CadLayerRow* LookupLayerRowCi(const std::vector<CadLayerRow>* layers, const std::string& layerName) {
  if (!layers)
    return nullptr;
  for (const auto& r : *layers) {
    if (r.name.size() != layerName.size())
      continue;
    bool eq = true;
    for (size_t i = 0; i < r.name.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(r.name[i])) !=
          std::tolower(static_cast<unsigned char>(layerName[i]))) {
        eq = false;
        break;
      }
    }
    if (eq)
      return &r;
  }
  return nullptr;
}

float LineweightMmToDevicePx(float mm) {
  return std::clamp(0.65f + mm * 5.25f, 1.f, 16.f);
}

void AppendPolylineEdgesVc(std::vector<float>& out, const CadExtendedGeometryInput& eg, float z, float defR,
                           float defG, float defB, float dashPatScale) {
  const auto* V = eg.polylineVerts;
  const auto* O = eg.polylineOffsets;
  const auto* Cl = eg.polylineClosed;
  const auto* At = eg.polylineAttrs;
  if (!V || !O || O->size() < 2)
    return;
  const int np = static_cast<int>(O->size()) - 1;
  for (int pi = 0; pi < np; ++pi) {
    EntityAttributes attr{};
    if (At && static_cast<size_t>(pi) < At->size())
      attr = (*At)[static_cast<size_t>(pi)];
    const CadLayerRow* lr = LookupLayerRowCi(eg.drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
    float rgba[4];
    ResolveEntityRgbaForViewport(attr, lr, defR, defG, defB, rgba);
    const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
    const int v0 = (*O)[static_cast<size_t>(pi)];
    const int v1 = (*O)[static_cast<size_t>(pi + 1)];
    if (v0 >= v1)
      continue;
    const bool closed =
        Cl && static_cast<size_t>(pi) < Cl->size() && (*Cl)[static_cast<size_t>(pi)] != 0;
    const int nv = v1 - v0;
    if (nv < 2)
      continue;
    std::vector<float> xy(static_cast<size_t>(nv * 2));
    for (int k = 0; k < nv; ++k) {
      const int vi = v0 + k;
      xy[static_cast<size_t>(k * 2)] = (*V)[static_cast<size_t>(vi * 3 + 0)];
      xy[static_cast<size_t>(k * 2 + 1)] = (*V)[static_cast<size_t>(vi * 3 + 1)];
    }
    CadTessellateLinetypeChainVc(xy.data(), nv, z, closed, lt, dashPatScale, rgba, &out);
  }
}

void AppendArcVcDashed(std::vector<float>& out, const CadArc& a, int n, float z, float dashPatScale,
                       const EntityAttributes& attr, const CadLayerRow* lr, float defR, float defG, float defB) {
  if (a.r <= 1e-6f || n < 2)
    return;
  float rgba[4];
  ResolveEntityRgbaForViewport(attr, lr, defR, defG, defB, rgba);
  const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
  std::vector<float> xy(static_cast<size_t>((static_cast<size_t>(n) + 1u) * 2u));
  for (int i = 0; i <= n; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(n);
    const float ang = a.startRad + a.sweepRad * u;
    xy[static_cast<size_t>(i * 2)] = a.cx + a.r * std::cos(ang);
    xy[static_cast<size_t>(i * 2 + 1)] = a.cy + a.r * std::sin(ang);
  }
  CadTessellateLinetypeChainVc(xy.data(), n + 1, z, false, lt, dashPatScale, rgba, &out);
}

void AppendEllipseVcDashed(std::vector<float>& out, const CadEllipse& el, int n, float z, float dashPatScale,
                           const EntityAttributes& attr, const CadLayerRow* lr, float defR, float defG, float defB) {
  const float ma = std::hypot(el.majVx, el.majVy);
  if (ma < 1e-8f || n < 3)
    return;
  float rgba[4];
  ResolveEntityRgbaForViewport(attr, lr, defR, defG, defB, rgba);
  const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
  const float ux = el.majVx / ma;
  const float uy = el.majVy / ma;
  const float px = -uy;
  const float py = ux;
  const float mb = ma * el.ratio;
  constexpr float twopi = 6.28318530718f;
  std::vector<float> xy(static_cast<size_t>((static_cast<size_t>(n) + 1u) * 2u));
  for (int i = 0; i <= n; ++i) {
    const float u = twopi * static_cast<float>(i) / static_cast<float>(n);
    const float c0 = std::cos(u);
    const float s0 = std::sin(u);
    xy[static_cast<size_t>(i * 2)] = el.cx + ux * (ma * c0) + px * (mb * s0);
    xy[static_cast<size_t>(i * 2 + 1)] = el.cy + uy * (ma * c0) + py * (mb * s0);
  }
  CadTessellateLinetypeChainVc(xy.data(), n + 1, z, true, lt, dashPatScale, rgba, &out);
}

void AppendCircleVcDashed(std::vector<float>& out, float cx, float cy, float r, int segments, float z,
                          float dashPatScale, const EntityAttributes& attr, const CadLayerRow* lr, float defR,
                          float defG, float defB) {
  if (r <= 1e-6f)
    return;
  float rgba[4];
  ResolveEntityRgbaForViewport(attr, lr, defR, defG, defB, rgba);
  const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
  const float twoPi = 6.28318530718f;
  std::vector<float> xy(static_cast<size_t>((static_cast<size_t>(segments) + 1u) * 2u));
  for (int i = 0; i <= segments; ++i) {
    const float t = twoPi * static_cast<float>(i) / static_cast<float>(segments);
    xy[static_cast<size_t>(i * 2)] = cx + r * std::cos(t);
    xy[static_cast<size_t>(i * 2 + 1)] = cy + r * std::sin(t);
  }
  CadTessellateLinetypeChainVc(xy.data(), segments + 1, z, true, lt, dashPatScale, rgba, &out);
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

/// Two diagonal segments (×); \p halfDiag is half the segment length along each diagonal from center.
void AppendSnapDiagonalCross(std::vector<float>& out, float cx, float cy, float z, float halfDiag) {
  const float h = halfDiag;
  out.insert(out.end(),
             {cx - h, cy - h, z, cx + h, cy + h, z, cx - h, cy + h, z, cx + h, cy - h, z});
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

void BuildSnapOverlayLines(const CadSnap::Hit& snap, float halfWorld, int fbHeight, float glyphHalfPx,
                           std::vector<float>& out) {
  if (!snap.valid)
    return;
  const float zSnap = 0.045f;
  const float mh = std::clamp(glyphHalfPx, 3.f, 48.f) * (2.f * halfWorld) / static_cast<float>(std::max(fbHeight, 1));
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
  case CadSnap::Kind::SurveyCenter: {
    const float R = mh * 0.62f;
    AppendCircleLineApprox(out, snap.x, snap.y, R, 28, zSnap);
    // × slightly larger than the circle (tips past radius R in diagonal directions).
    AppendSnapDiagonalCross(out, snap.x, snap.y, zSnap, R * 0.78f);
    break;
  }
  case CadSnap::Kind::GeometricCenter:
    AppendSnapSquareOutline(out, snap.x, snap.y, zSnap, mh);
    AppendSnapDiagonalCross(out, snap.x, snap.y, zSnap, mh * 0.42f);
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

void ViewportRenderer::DestroyMultisamplePass() {
  if (msColorRbo_) {
    glDeleteRenderbuffers(1, &msColorRbo_);
    msColorRbo_ = 0;
  }
  if (msDepthRbo_) {
    glDeleteRenderbuffers(1, &msDepthRbo_);
    msDepthRbo_ = 0;
  }
  if (msFbo_) {
    glDeleteFramebuffers(1, &msFbo_);
    msFbo_ = 0;
  }
  msFbW_ = msFbH_ = 0;
  msaaAvailable_ = false;
}

bool ViewportRenderer::EnsureMultisamplePass(int w, int h) {
  if (w <= 0 || h <= 0)
    return false;
  if (msaaAvailable_ && msFbo_ && w == msFbW_ && h == msFbH_)
    return true;

  DestroyMultisamplePass();

  GLint samples = 0;
  glGetIntegerv(GL_MAX_SAMPLES, &samples);
  GLint n = 4;
  if (samples < 2)
    return false;
  if (samples >= 8)
    n = 8;
  else if (samples >= 4)
    n = 4;
  else
    n = 2;

  glGenFramebuffers(1, &msFbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);

  glGenRenderbuffers(1, &msColorRbo_);
  glBindRenderbuffer(GL_RENDERBUFFER, msColorRbo_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_RGBA8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColorRbo_);

  glGenRenderbuffers(1, &msDepthRbo_);
  glBindRenderbuffer(GL_RENDERBUFFER, msDepthRbo_);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_DEPTH_COMPONENT24, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepthRbo_);

  const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  if (st != GL_FRAMEBUFFER_COMPLETE) {
    DestroyMultisamplePass();
    return false;
  }

  msFbW_ = w;
  msFbH_ = h;
  msaaAvailable_ = true;
  return true;
}

void ViewportRenderer::DestroyFramebuffer() {
  DestroyMultisamplePass();
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
                                   const CadSnap::Hit* snapOverlay, float snapGlyphHalfPx,
                                   const float* selectionFillRect, const std::vector<float>* previewLines,
                                   const std::vector<float>* previewCircles, const std::vector<float>* highlightLines, const std::vector<float>* highlightCircles,
                                   const std::vector<float>* surveyMarkers,
                                   const std::vector<EntityAttributes>* lineEntityAttrs,
                                   const std::vector<EntityAttributes>* circleEntityAttrs,
                                   const CadExtendedGeometryInput* extended, bool showGrid,
                                   const std::vector<CadLayerRow>* drawingLayers) {
  if (!EnsureFramebuffer(fbWidth, fbHeight))
    return;

  const bool useMsaa = EnsureMultisamplePass(fbW_, fbH_);
  glBindFramebuffer(GL_FRAMEBUFFER, (useMsaa && msFbo_) ? msFbo_ : fbo_);
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

  constexpr GLfloat kLwMain = 1.35f;
  constexpr GLfloat kLwHiLine = 2.65f;
  constexpr GLfloat kLwHiCirc = 2.45f;
  constexpr GLfloat kLwFence = 1.1f;
  constexpr GLfloat kLwSurvey = 1.65f;
  constexpr GLfloat kLwSnap = 1.35f;
  constexpr GLfloat kLwGizmo = 1.1f;

  GLint locMvp = glGetUniformLocation(lineProgram_, "uMVP");
  GLint locCol = glGetUniformLocation(lineProgram_, "uColor");

  glUseProgram(gridProgram_);
  glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);

  // --- Grid: centered on view, step scales with zoom (stable in world space) ---
  if (showGrid) {
    auto niceStep = [](float worldSpan) -> float {
      float s = worldSpan / 20.f;
      if (s < 1e-9f)
        s = 1e-9f;
      const float p = std::pow(10.f, std::floor(std::log10(s)));
      const float m = s / p;
      if (m < 1.5f)
        return p;
      if (m < 3.5f)
        return 2.f * p;
      if (m < 7.f)
        return 5.f * p;
      return 10.f * p;
    };
    const float gx = panX;
    const float gy = panY;
    const float step = niceStep(std::max(halfW, halfH) * 2.f);
    const float spanW = halfW * 2.15f + step * 2.f;
    const float spanH = halfH * 2.15f + step * 2.f;
    const int rawNi = static_cast<int>(std::ceil(spanW / std::max(step, 1e-12f))) + 2;
    const int ni = std::min(512, std::max(4, rawNi));
    std::vector<float> gridVerts;
    const float gz = -0.02f;
    for (int i = -ni; i <= ni; ++i) {
      const float x = gx + static_cast<float>(i) * step;
      gridVerts.push_back(x);
      gridVerts.push_back(gy - spanH);
      gridVerts.push_back(gz);
      gridVerts.push_back(x);
      gridVerts.push_back(gy + spanH);
      gridVerts.push_back(gz);
    }
    for (int i = -ni; i <= ni; ++i) {
      const float y = gy + static_cast<float>(i) * step;
      gridVerts.push_back(gx - spanW);
      gridVerts.push_back(y);
      gridVerts.push_back(gz);
      gridVerts.push_back(gx + spanW);
      gridVerts.push_back(y);
      gridVerts.push_back(gz);
    }
    gridVertexCount_ = static_cast<int>(gridVerts.size() / 3);

    glBindVertexArray(vaoGrid_);
    glBindBuffer(GL_ARRAY_BUFFER, vboGrid_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(gridVerts.size() * sizeof(float)),
                 gridVerts.empty() ? nullptr : gridVerts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(locCol, 0.14f, 0.14f, 0.15f, 0.28f);
    glDrawArrays(GL_LINES, 0, gridVertexCount_);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
  }

  glBindBuffer(GL_ARRAY_BUFFER, vboLines_);
  glBindVertexArray(vaoLines_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
  glLineWidth(kLwMain);

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
      vcLineBatches_.clear();
      vcCircleBatches_.clear();
      const float dashPatScale = std::max(halfW, halfH) * 0.045f;

      int lineVertTotal = 0;
      int lineBatchStart = 0;
      float lineBatchPx = -1.f;

      auto maybeSplitLineBatch = [&](int vertsBefore, float nextPx) {
        if (lineBatchPx < 0.f) {
          lineBatchPx = nextPx;
          return;
        }
        if (vertsBefore > lineBatchStart && std::fabs(nextPx - lineBatchPx) > 0.25f) {
        vcLineBatches_.push_back(VcLineBatch{lineBatchStart, vertsBefore - lineBatchStart, lineBatchPx});
          lineBatchStart = vertsBefore;
          lineBatchPx = nextPx;
        }
      };

      auto appendUserLineSeg = [&](const EntityAttributes& attr, float x0, float y0, float z0, float x1, float y1,
                                   float z1, float dr, float dg, float db) {
        const CadLayerRow* lr = LookupLayerRowCi(drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
        const int vertsBefore = static_cast<int>(cpuVcLines_.size() / 7);
        float rgba[4];
        ResolveEntityRgbaForViewport(attr, lr, dr, dg, db, rgba);
        const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
        const float lwMm = EffectiveEntityLineweightMm(attr, lr);
        const float pxw = LineweightMmToDevicePx(lwMm);
        maybeSplitLineBatch(vertsBefore, pxw);
        CadTessellateLinetypeSegmentVc(x0, y0, z0, x1, y1, z1, lt, dashPatScale, rgba, &cpuVcLines_);
        lineVertTotal = static_cast<int>(cpuVcLines_.size() / 7);
      };

      if (hasLines) {
        const size_t nSeg = userLines.size() / 6;
        for (size_t i = 0; i < nSeg; ++i) {
          EntityAttributes attr{};
          if (lineEntityAttrs && i < lineEntityAttrs->size())
            attr = (*lineEntityAttrs)[i];
          appendUserLineSeg(attr, userLines[i * 6 + 0], userLines[i * 6 + 1], userLines[i * 6 + 2],
                            userLines[i * 6 + 3], userLines[i * 6 + 4], userLines[i * 6 + 5], kLineDefaultR,
                            kLineDefaultG, kLineDefaultB);
        }
      }
      if (lineVertTotal > lineBatchStart && lineBatchPx >= 0.f)
        vcLineBatches_.push_back(VcLineBatch{lineBatchStart, lineVertTotal - lineBatchStart, lineBatchPx});

      if (extended) {
        lineBatchStart = lineVertTotal;
        lineBatchPx = -1.f;
        if (extended->arcs) {
          for (size_t i = 0; i < extended->arcs->size(); ++i) {
            EntityAttributes attr{};
            if (extended->arcAttrs && i < extended->arcAttrs->size())
              attr = (*extended->arcAttrs)[i];
            const CadLayerRow* lr = LookupLayerRowCi(drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
            const int vb = static_cast<int>(cpuVcLines_.size() / 7);
            const float lwMm = EffectiveEntityLineweightMm(attr, lr);
            maybeSplitLineBatch(vb, LineweightMmToDevicePx(lwMm));
            AppendArcVcDashed(cpuVcLines_, (*extended->arcs)[i], 48, 0.f, dashPatScale, attr, lr, kLineDefaultR,
                              kLineDefaultG, kLineDefaultB);
            lineVertTotal = static_cast<int>(cpuVcLines_.size() / 7);
          }
        }
        if (extended->ellipses) {
          for (size_t i = 0; i < extended->ellipses->size(); ++i) {
            EntityAttributes attr{};
            if (extended->ellAttrs && i < extended->ellAttrs->size())
              attr = (*extended->ellAttrs)[i];
            const CadLayerRow* lr = LookupLayerRowCi(drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
            const int vb = static_cast<int>(cpuVcLines_.size() / 7);
            maybeSplitLineBatch(vb, LineweightMmToDevicePx(EffectiveEntityLineweightMm(attr, lr)));
            AppendEllipseVcDashed(cpuVcLines_, (*extended->ellipses)[i], 72, 0.f, dashPatScale, attr, lr,
                                   kLineDefaultR, kLineDefaultG, kLineDefaultB);
            lineVertTotal = static_cast<int>(cpuVcLines_.size() / 7);
          }
        }
        if (extended->polylineVerts && extended->polylineOffsets) {
          const int vb = static_cast<int>(cpuVcLines_.size() / 7);
          // Polylines: split batch using first segment's weight (per-entity variation not batched here).
          EntityAttributes attr0{};
          if (extended->polylineAttrs && extended->polylineOffsets->size() >= 2 &&
              !extended->polylineAttrs->empty())
            attr0 = (*extended->polylineAttrs)[0];
          const CadLayerRow* lr0 =
              LookupLayerRowCi(drawingLayers, attr0.layer.empty() ? std::string("0") : attr0.layer);
          maybeSplitLineBatch(vb, LineweightMmToDevicePx(EffectiveEntityLineweightMm(attr0, lr0)));
          AppendPolylineEdgesVc(cpuVcLines_, *extended, 0.f, kLineDefaultR, kLineDefaultG, kLineDefaultB,
                                dashPatScale);
          lineVertTotal = static_cast<int>(cpuVcLines_.size() / 7);
        }
        if (lineVertTotal > lineBatchStart && lineBatchPx >= 0.f)
          vcLineBatches_.push_back(VcLineBatch{lineBatchStart, lineVertTotal - lineBatchStart, lineBatchPx});
      }

      if (hasCircles) {
        int circVert = 0;
        int circBatchStart = 0;
        float circBatchPx = -1.f;
        auto maybeSplitCirc = [&](int vertsBefore, float nextPx) {
          if (circBatchPx < 0.f) {
            circBatchPx = nextPx;
            return;
          }
          if (vertsBefore > circBatchStart && std::fabs(nextPx - circBatchPx) > 0.25f) {
            vcCircleBatches_.push_back(VcLineBatch{circBatchStart, vertsBefore - circBatchStart, circBatchPx});
            circBatchStart = vertsBefore;
            circBatchPx = nextPx;
          }
        };
        const size_t nCirc = circlesCxCyR.size() / 3;
        for (size_t ci = 0; ci < nCirc; ++ci) {
          EntityAttributes attr{};
          if (circleEntityAttrs && ci < circleEntityAttrs->size())
            attr = (*circleEntityAttrs)[ci];
          const CadLayerRow* lr = LookupLayerRowCi(drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
          const int vb = static_cast<int>(cpuVcCircles_.size() / 7);
          const float lwMm = EffectiveEntityLineweightMm(attr, lr);
          maybeSplitCirc(vb, LineweightMmToDevicePx(lwMm));
          AppendCircleVcDashed(cpuVcCircles_, circlesCxCyR[ci * 3], circlesCxCyR[ci * 3 + 1], circlesCxCyR[ci * 3 + 2],
                               72, 0.f, dashPatScale, attr, lr, kCircDefaultR, kCircDefaultG, kCircDefaultB);
          circVert = static_cast<int>(cpuVcCircles_.size() / 7);
        }
        if (circVert > circBatchStart && circBatchPx >= 0.f)
          vcCircleBatches_.push_back(VcLineBatch{circBatchStart, circVert - circBatchStart, circBatchPx});
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
    if (!cpuVcLines_.empty()) {
      glBindVertexArray(vaoVcLines_);
      if (vcLineBatches_.empty())
        vcLineBatches_.push_back(
            VcLineBatch{0, static_cast<int>(cpuVcLines_.size() / 7), LineweightMmToDevicePx(0.18f)});
      for (const auto& b : vcLineBatches_) {
        if (b.count <= 0)
          continue;
        glLineWidth(b.widthPx);
        glDrawArrays(GL_LINES, static_cast<GLsizei>(b.first), static_cast<GLsizei>(b.count));
      }
    }
    if (!cpuVcCircles_.empty()) {
      glBindVertexArray(vaoVcCircles_);
      if (vcCircleBatches_.empty())
        vcCircleBatches_.push_back(
            VcLineBatch{0, static_cast<int>(cpuVcCircles_.size() / 7), LineweightMmToDevicePx(0.18f)});
      for (const auto& b : vcCircleBatches_) {
        if (b.count <= 0)
          continue;
        glLineWidth(b.widthPx);
        glDrawArrays(GL_LINES, static_cast<GLsizei>(b.first), static_cast<GLsizei>(b.count));
      }
    }
    glBindVertexArray(0);
    glUseProgram(lineProgram_);
  }

  glBindBuffer(GL_ARRAY_BUFFER, vboLines_);
  glBindVertexArray(vaoLines_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);

  glDisable(GL_BLEND);
  glLineWidth(kLwMain);

  // --- Selection highlight (accent stroke on top of committed geometry) ---
  if (highlightLines && !highlightLines->empty() && highlightLines->size() % 6 == 0) {
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUniform4f(locCol, 1.f, 0.92f, 0.15f, 1.f);
    glLineWidth(kLwHiLine);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(highlightLines->size() * sizeof(float)),
                 highlightLines->data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(highlightLines->size() / 3));
    glLineWidth(kLwMain);
  }
  if (highlightCircles && !highlightCircles->empty() && highlightCircles->size() % 3 == 0) {
    std::vector<float> hlCircGeom;
    for (size_t i = 0; i + 2 < highlightCircles->size(); i += 3)
      AppendCircleLineApprox(hlCircGeom, (*highlightCircles)[i], (*highlightCircles)[i + 1],
                             (*highlightCircles)[i + 2], 80, 0.018f);
    if (!hlCircGeom.empty()) {
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glUniform4f(locCol, 1.f, 0.88f, 0.22f, 1.f);
      glLineWidth(kLwHiCirc);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(hlCircGeom.size() * sizeof(float)), hlCircGeom.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(hlCircGeom.size() / 3));
      glLineWidth(kLwMain);
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
    glLineWidth(kLwFence);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lineGeom.size() * sizeof(float)), lineGeom.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineGeom.size() / 3));
    glDisable(GL_BLEND);
    glLineWidth(kLwMain);
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
    glLineWidth(kLwSurvey);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(surveyMarkers->size() * sizeof(float)),
                 surveyMarkers->data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(surveyMarkers->size() / 3));
    glLineWidth(kLwMain);
  }

  // --- Object snap glyph (green, screen-stable size) ---
  if (snapOverlay && snapOverlay->valid) {
    std::vector<float> snapGeom;
    BuildSnapOverlayLines(*snapOverlay, halfH, fbH_, snapGlyphHalfPx, snapGeom);
    if (!snapGeom.empty()) {
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glUniform4f(locCol, 0.15f, 0.92f, 0.38f, 1.f);
      glLineWidth(kLwSnap);
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

  glLineWidth(kLwGizmo);
  glUniform4f(locCol, 0.9f, 0.2f, 0.2f, 1.f);
  glDrawArrays(GL_LINES, 0, 2);
  glUniform4f(locCol, 0.2f, 0.85f, 0.35f, 1.f);
  glDrawArrays(GL_LINES, 2, 2);
  glUniform4f(locCol, 0.25f, 0.55f, 1.f, 1.f);
  glDrawArrays(GL_LINES, 4, 2);
  glLineWidth(kLwMain);

  glBindVertexArray(0);
  glUseProgram(0);
  glDepthMask(GL_TRUE);

  if (useMsaa && msFbo_ && fbo_) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    glBlitFramebuffer(0, 0, fbW_, fbH_, 0, 0, fbW_, fbH_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
