#include "ViewportRenderer.hpp"

#include "CadLinetype.hpp"
#include "CadSnap.hpp"
#include "PaperSpace.hpp"
#include "geom2d.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <array>
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

// Textured quad — used for PDF underlay rendering
const char* kTexVs = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main() {
  gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
  vUV = aUV;
}
)";

const char* kTexFs = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform float uAlpha;
uniform float uTransparentBg;  // 1.0 = filter background out, 0.0 = full raster
uniform float uDarkBg;         // 1.0 = dark-background PDF (filter black), 0.0 = light-background
out vec4 FragColor;
void main() {
  vec4  s     = texture(uTex, vUV);
  vec3  col   = s.rgb;
  float alpha = s.a * uAlpha;

  if (uTransparentBg > 0.5) {
    if (uDarkBg > 0.5) {
      // Dark-background PDF (e.g. CAD DWG export): background = black.
      // contentA = how far the pixel is from pure black.
      // Un-premultiply the black tint so line colors are restored to full saturation.
      float contentA = max(max(s.r, s.g), s.b);
      float fadeA    = smoothstep(0.10, 0.35, contentA);
      col   = contentA > 0.01 ? clamp(s.rgb / contentA, 0.0, 1.0) : s.rgb;
      alpha = s.a * uAlpha * fadeA;
    } else {
      // Light-background PDF (e.g. white-paper scan/print): background = white.
      // Un-premultiply the white tint so line colors are restored to full saturation.
      float bgMix    = min(min(s.r, s.g), s.b);   // white fraction blended into pixel
      float contentA = 1.0 - bgMix;               // 0 = pure white bg, 1 = full content
      float fadeA    = smoothstep(0.05, 0.30, contentA);
      col   = contentA > 0.05 ? clamp((s.rgb - bgMix) / contentA, 0.0, 1.0) : s.rgb;
      // Boost dark/gray content so it's visible on a dark-theme CAD viewport.
      // Uses max-channel as a proxy for "how coloured is this pixel":
      //   - Black/gray lines (maxC≈0): lifted toward white so they're readable.
      //   - Saturated fills (maxC≈1): boost≈0, colour preserved exactly.
      float maxC = max(max(col.r, col.g), col.b);
      col = clamp(col + vec3((1.0 - maxC) * 0.85), 0.0, 1.0);
      alpha = s.a * uAlpha * fadeA;
    }
  }

  FragColor = vec4(col, alpha);
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

void AppendCircleLineApprox(std::vector<float>& out, float cx, float cy, float r, int segments, float z,
                            double viewAnchorX, double viewAnchorY) {
  if (r <= 1e-6f || segments < 1)
    return;
  const double dcx = static_cast<double>(cx);
  const double dcy = static_cast<double>(cy);
  const double dr = static_cast<double>(r);
  constexpr double kTwoPi = 6.283185307179586;
  for (int i = 0; i < segments; ++i) {
    const double t0 = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
    const double t1 = kTwoPi * static_cast<double>(i + 1) / static_cast<double>(segments);
    float rx = 0.f;
    float ry = 0.f;
    CirclePointViewRel(dcx, dcy, viewAnchorX, viewAnchorY, dr, t0, &rx, &ry);
    out.push_back(rx);
    out.push_back(ry);
    out.push_back(z);
    CirclePointViewRel(dcx, dcy, viewAnchorX, viewAnchorY, dr, t1, &rx, &ry);
    out.push_back(rx);
    out.push_back(ry);
    out.push_back(z);
  }
}

void AppendCircleLineApproxViewRel(std::vector<float>& out, float viewCx, float viewCy, float r, int segments,
                                   float z) {
  if (r <= 1e-6f || segments < 1)
    return;
  const double dr = static_cast<double>(r);
  const double dcx = static_cast<double>(viewCx);
  const double dcy = static_cast<double>(viewCy);
  constexpr double kTwoPi = 6.283185307179586;
  for (int i = 0; i < segments; ++i) {
    const double t0 = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
    const double t1 = kTwoPi * static_cast<double>(i + 1) / static_cast<double>(segments);
    const double c0 = std::cos(t0);
    const double s0 = std::sin(t0);
    const double c1 = std::cos(t1);
    const double s1 = std::sin(t1);
    out.push_back(static_cast<float>(dcx + dr * c0));
    out.push_back(static_cast<float>(dcy + dr * s0));
    out.push_back(z);
    out.push_back(static_cast<float>(dcx + dr * c1));
    out.push_back(static_cast<float>(dcy + dr * s1));
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
                           float defG, float defB, float dashPatScale, double viewAnchorX, double viewAnchorY) {
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
      WorldToViewRelativeFloat(static_cast<double>((*V)[static_cast<size_t>(vi * 3 + 0)]),
                               static_cast<double>((*V)[static_cast<size_t>(vi * 3 + 1)]), viewAnchorX,
                               viewAnchorY, &xy[static_cast<size_t>(k * 2)], &xy[static_cast<size_t>(k * 2 + 1)]);
    }
    CadTessellateLinetypeChainVc(xy.data(), nv, z, closed, lt, dashPatScale, rgba, &out);
  }
}

void AppendArcVcDashed(std::vector<float>& out, const CadArc& a, int n, float z, float dashPatScale,
                       const EntityAttributes& attr, const CadLayerRow* lr, float defR, float defG, float defB,
                       double viewAnchorX, double viewAnchorY) {
  if (a.r <= 1e-6f || n < 2)
    return;
  float rgba[4];
  ResolveEntityRgbaForViewport(attr, lr, defR, defG, defB, rgba);
  const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
  const double dcx = static_cast<double>(a.cx);
  const double dcy = static_cast<double>(a.cy);
  const double dr = static_cast<double>(a.r);
  const double rcx = dcx - viewAnchorX;
  const double rcy = dcy - viewAnchorY;
  std::vector<float> xy(static_cast<size_t>((static_cast<size_t>(n) + 1u) * 2u));
  for (int i = 0; i <= n; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(n);
    const double ang = static_cast<double>(a.startRad + a.sweepRad * u);
    const double c = std::cos(ang);
    const double s = std::sin(ang);
    xy[static_cast<size_t>(i * 2)] = static_cast<float>(rcx + dr * c);
    xy[static_cast<size_t>(i * 2 + 1)] = static_cast<float>(rcy + dr * s);
  }
  CadTessellateLinetypeChainVc(xy.data(), n + 1, z, false, lt, dashPatScale, rgba, &out);
}

void AppendEllipseVcDashed(std::vector<float>& out, const CadEllipse& el, int n, float z, float dashPatScale,
                           const EntityAttributes& attr, const CadLayerRow* lr, float defR, float defG, float defB,
                           double viewAnchorX, double viewAnchorY) {
  const float ma = std::hypot(el.majVx, el.majVy);
  if (ma < 1e-8f || n < 3)
    return;
  float rgba[4];
  ResolveEntityRgbaForViewport(attr, lr, defR, defG, defB, rgba);
  const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
  const double dcx = static_cast<double>(el.cx);
  const double dcy = static_cast<double>(el.cy);
  const double rcx = dcx - viewAnchorX;
  const double rcy = dcy - viewAnchorY;
  const double ux = static_cast<double>(el.majVx / ma);
  const double uy = static_cast<double>(el.majVy / ma);
  const double px = -uy;
  const double py = ux;
  const double dma = static_cast<double>(ma);
  const double dmb = dma * static_cast<double>(el.ratio);
  constexpr double kTwoPi = 6.283185307179586;
  std::vector<float> xy(static_cast<size_t>((static_cast<size_t>(n) + 1u) * 2u));
  for (int i = 0; i <= n; ++i) {
    const double u = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
    const double c0 = std::cos(u);
    const double s0 = std::sin(u);
    xy[static_cast<size_t>(i * 2)] = static_cast<float>(rcx + ux * (dma * c0) + px * (dmb * s0));
    xy[static_cast<size_t>(i * 2 + 1)] = static_cast<float>(rcy + uy * (dma * c0) + py * (dmb * s0));
  }
  CadTessellateLinetypeChainVc(xy.data(), n + 1, z, true, lt, dashPatScale, rgba, &out);
}

void AppendCircleVcDashed(std::vector<float>& out, float cx, float cy, float r, int segments, float z,
                          float dashPatScale, const EntityAttributes& attr, const CadLayerRow* lr, float defR,
                          float defG, float defB, double viewAnchorX, double viewAnchorY) {
  if (r <= 1e-6f)
    return;
  float rgba[4];
  ResolveEntityRgbaForViewport(attr, lr, defR, defG, defB, rgba);
  const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
  const double dcx = static_cast<double>(cx);
  const double dcy = static_cast<double>(cy);
  const double dr = static_cast<double>(r);
  constexpr double kTwoPi = 6.283185307179586;
  std::vector<float> xy(static_cast<size_t>((static_cast<size_t>(segments) + 1u) * 2u));
  for (int i = 0; i <= segments; ++i) {
    const double t = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
    float rx = 0.f;
    float ry = 0.f;
    CirclePointViewRel(dcx, dcy, viewAnchorX, viewAnchorY, dr, t, &rx, &ry);
    xy[static_cast<size_t>(i * 2)] = rx;
    xy[static_cast<size_t>(i * 2 + 1)] = ry;
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

void AppendWorldRectOutline(std::vector<float>& o, float xa, float ya, float xb, float yb, float z, double viewAnchorX,
                            double viewAnchorY) {
  const float mnX = std::min(xa, xb);
  const float mxX = std::max(xa, xb);
  const float mnY = std::min(ya, yb);
  const float mxY = std::max(ya, yb);
  auto seg = [&](float x0, float y0, float x1, float y1) {
    float rx0 = 0.f;
    float ry0 = 0.f;
    float rx1 = 0.f;
    float ry1 = 0.f;
    WorldToViewRelativeFloat(static_cast<double>(x0), static_cast<double>(y0), viewAnchorX, viewAnchorY, &rx0, &ry0);
    WorldToViewRelativeFloat(static_cast<double>(x1), static_cast<double>(y1), viewAnchorX, viewAnchorY, &rx1, &ry1);
    o.push_back(rx0);
    o.push_back(ry0);
    o.push_back(z);
    o.push_back(rx1);
    o.push_back(ry1);
    o.push_back(z);
  };
  seg(mnX, mnY, mxX, mnY);
  seg(mxX, mnY, mxX, mxY);
  seg(mxX, mxY, mnX, mxY);
  seg(mnX, mxY, mnX, mnY);
}

void AppendWorldRectFillTris(std::vector<float>& o, float xa, float ya, float xb, float yb, float z, double viewAnchorX,
                             double viewAnchorY) {
  const float mnX = std::min(xa, xb);
  const float mxX = std::max(xa, xb);
  const float mnY = std::min(ya, yb);
  const float mxY = std::max(ya, yb);
  float c[4][2];
  const float corners[4][2] = {{mnX, mnY}, {mxX, mnY}, {mxX, mxY}, {mnX, mxY}};
  for (int i = 0; i < 4; ++i)
    WorldToViewRelativeFloat(static_cast<double>(corners[i][0]), static_cast<double>(corners[i][1]), viewAnchorX,
                             viewAnchorY, &c[i][0], &c[i][1]);
  const float tri[] = {
      c[0][0], c[0][1], z, c[1][0], c[1][1], z, c[2][0], c[2][1], z,
      c[0][0], c[0][1], z, c[2][0], c[2][1], z, c[3][0], c[3][1], z,
  };
  o.insert(o.end(), tri, tri + sizeof(tri) / sizeof(tri[0]));
}

void ConvertLineVertsWorldToView(const std::vector<float>& world, double viewAnchorX, double viewAnchorY,
                                 std::vector<float>* rel) {
  rel->clear();
  rel->reserve(world.size());
  for (size_t i = 0; i + 2 < world.size(); i += 3) {
    float rx = 0.f;
    float ry = 0.f;
    WorldToViewRelativeFloat(static_cast<double>(world[i]), static_cast<double>(world[i + 1]), viewAnchorX, viewAnchorY,
                             &rx, &ry);
    rel->push_back(rx);
    rel->push_back(ry);
    rel->push_back(world[i + 2]);
  }
}

void BuildSnapOverlayLines(const CadSnap::Hit& snap, float halfWorld, int fbHeight, float glyphHalfPx,
                           double viewAnchorX, double viewAnchorY, std::vector<float>& out) {
  if (!snap.valid)
    return;
  float sx = 0.f;
  float sy = 0.f;
  WorldToViewRelativeFloat(static_cast<double>(snap.x), static_cast<double>(snap.y), viewAnchorX, viewAnchorY, &sx,
                           &sy);
  const float zSnap = 0.045f;
  const float mh = std::clamp(glyphHalfPx, 3.f, 48.f) * (2.f * halfWorld) / static_cast<float>(std::max(fbHeight, 1));
  const int snapCircSegs = std::max(16, static_cast<int>(mh * 40.f));
  switch (snap.kind) {
  case CadSnap::Kind::Endpoint:
    AppendSnapSquareOutline(out, sx, sy, zSnap, mh);
    break;
  case CadSnap::Kind::Midpoint:
    AppendSnapTriangleOutline(out, sx, sy, zSnap, mh);
    break;
  case CadSnap::Kind::Center:
    AppendCircleLineApproxViewRel(out, sx, sy, mh * 0.85f, snapCircSegs, zSnap);
    break;
  case CadSnap::Kind::SurveyCenter: {
    const float R = mh * 0.62f;
    AppendCircleLineApproxViewRel(out, sx, sy, R, snapCircSegs, zSnap);
    // × slightly larger than the circle (tips past radius R in diagonal directions).
    AppendSnapDiagonalCross(out, sx, sy, zSnap, R * 0.78f);
    break;
  }
  case CadSnap::Kind::GeometricCenter:
    AppendSnapSquareOutline(out, sx, sy, zSnap, mh);
    AppendSnapDiagonalCross(out, sx, sy, zSnap, mh * 0.42f);
    break;
  case CadSnap::Kind::Perpendicular:
    AppendSnapSquareOutline(out, sx, sy, zSnap, mh);
    AppendSnapCrossInSquare(out, sx, sy, zSnap, mh * 0.55f);
    break;
  case CadSnap::Kind::Grip:
    break; // grip snap is silent — no glyph drawn
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

  // Textured quad for PDF underlays
  GLuint texVs = CompileShader(GL_VERTEX_SHADER,   kTexVs);
  GLuint texFs = CompileShader(GL_FRAGMENT_SHADER, kTexFs);
  if (texVs && texFs) {
    texProgram_ = LinkProgram(texVs, texFs);
    if (texProgram_) {
      glGenVertexArrays(1, &vaoTex_);
      glGenBuffers(1, &vboTex_);
      glBindVertexArray(vaoTex_);
      glBindBuffer(GL_ARRAY_BUFFER, vboTex_);
      // layout: vec2 pos + vec2 uv (4 floats per vertex, 6 verts per quad — streamed each frame)
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                             reinterpret_cast<const void*>(2 * sizeof(float)));
      glBindVertexArray(0);
    }
  }

  return true;
}

void ViewportRenderer::DestroyShader() {
  if (vboTex_) { glDeleteBuffers(1, &vboTex_); vboTex_ = 0; }
  if (vaoTex_) { glDeleteVertexArrays(1, &vaoTex_); vaoTex_ = 0; }
  if (texProgram_) { glDeleteProgram(texProgram_); texProgram_ = 0; }
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

void ViewportRenderer::RenderPaperSpace(int fbWidth, int fbHeight, const std::vector<float>& userLines,
                        const std::vector<float>& circlesCxCyR, std::uint32_t cadGpuRevision,
                        const std::vector<float>& rubberLines, const CadSnap::Hit* snapOverlay,
                        float snapGlyphHalfPx, const std::vector<float>* highlightLines,
                        const std::vector<float>* highlightCircles, const std::vector<float>* surveyMarkers,
                        const std::vector<EntityAttributes>* lineEntityAttrs,
                        const std::vector<EntityAttributes>* circleEntityAttrs,
                        const CadExtendedGeometryInput* extended, const std::vector<CadLayerRow>* drawingLayers,
                        const RenderTuning& tuning, const PaperLayout& layout, double worldDocOriginX,
                        double worldDocOriginY, float modelUnitsPerPlottedInch) {
  // REQ-034 Phase 1: Render paper space with per-viewport GL scissor clipping.
  const float sheetW = layout.sheetWidthIn();
  const float sheetH = layout.sheetHeightIn();

  // Paper-space projection: ortho from (0,0) to (sheetW, sheetH) in inches.
  float paperProj[16];
  Ortho(0.f, sheetW, 0.f, sheetH, -1000.f, 1000.f, paperProj);

  float paperModel[16];
  TranslateMat(0.f, 0.f, 0.f, paperModel);

  float paperMvp[16];
  MulMat4(paperProj, paperModel, paperMvp);

  if (!EnsureShader() || !lineProgram_ || !vaoLines_ || !vboLines_)
    return;

  glUseProgram(lineProgram_);
  GLint locMvp = glGetUniformLocation(lineProgram_, "uMVP");
  GLint locCol = glGetUniformLocation(lineProgram_, "uColor");
  glBindVertexArray(vaoLines_);
  glBindBuffer(GL_ARRAY_BUFFER, vboLines_);

  // Draw sheet outline.
  const float sheetOutlineVerts[] = {
    0.f, 0.f, 0.f, sheetW, 0.f, 0.f,
    sheetW, 0.f, 0.f, sheetW, sheetH, 0.f,
    sheetW, sheetH, 0.f, 0.f, sheetH, 0.f,
    0.f, sheetH, 0.f, 0.f, 0.f, 0.f,
  };
  glUniformMatrix4fv(locMvp, 1, GL_FALSE, paperMvp);
  glUniform4f(locCol, 0.4f, 0.4f, 0.4f, 1.f);
  glLineWidth(1.5f);
  glBufferData(GL_ARRAY_BUFFER, sizeof(sheetOutlineVerts), sheetOutlineVerts, GL_STREAM_DRAW);
  glDrawArrays(GL_LINES, 0, 12);

  // REQ-034 Phase 1: Render model geometry for each viewport with scissor clipping.
  const float pxPerInch = static_cast<float>(fbW_) / sheetW;
  constexpr GLfloat kLwModel = 1.0f;
  constexpr GLfloat kLwVpBorder = 1.2f;

  for (size_t vi = 0; vi < layout.viewports.size(); ++vi) {
    const Viewport& vp = layout.viewports[vi];

    // Compute scissors rect in screen space (GL origin at bottom-left).
    const int scissorX = static_cast<int>(vp.paperXIn * pxPerInch);
    const int scissorY = static_cast<int>((sheetH - vp.paperYIn - vp.paperHIn) * pxPerInch);
    const int scissorW = static_cast<int>(vp.paperWIn * pxPerInch);
    const int scissorH = static_cast<int>(vp.paperHIn * pxPerInch);

    glEnable(GL_SCISSOR_TEST);
    glScissor(scissorX, scissorY, scissorW, scissorH);

    // Compute per-viewport model matrix: model (world coords) → paper inches.
    // This transform accounts for:
    //   1. World origin offset (geometry stored in LOCAL = world - origin)
    //   2. Viewport center (model point that maps to viewport center in paper)
    //   3. Viewport scale (model units per paper inch)
    // Result: paper-space coordinates in inches, which are then mapped to screen by paperProj.

    const float s = vp.safeScale();  // model units per paper inch
    const float vpCenterPaperX = vp.paperXIn + vp.paperWIn * 0.5f;
    const float vpCenterPaperY = vp.paperYIn + vp.paperHIn * 0.5f;

    // Scale matrix: 1/scale (model units to paper inches)
    float scale[16];
    {
      scale[0] = 1.f / s; scale[1] = 0.f; scale[2] = 0.f; scale[3] = 0.f;
      scale[4] = 0.f; scale[5] = 1.f / s; scale[6] = 0.f; scale[7] = 0.f;
      scale[8] = 0.f; scale[9] = 0.f; scale[10] = 1.f; scale[11] = 0.f;
      scale[12] = 0.f; scale[13] = 0.f; scale[14] = 0.f; scale[15] = 1.f;
    }

    // Translation: center viewport on model point; account for world origin.
    float trans[16];
    {
      const float tx = vpCenterPaperX - static_cast<float>(vp.modelCenterX - worldDocOriginX) / s;
      const float ty = vpCenterPaperY - static_cast<float>(vp.modelCenterY - worldDocOriginY) / s;
      trans[0] = 1.f; trans[1] = 0.f; trans[2] = 0.f; trans[3] = 0.f;
      trans[4] = 0.f; trans[5] = 1.f; trans[6] = 0.f; trans[7] = 0.f;
      trans[8] = 0.f; trans[9] = 0.f; trans[10] = 1.f; trans[11] = 0.f;
      trans[12] = tx; trans[13] = ty; trans[14] = 0.f; trans[15] = 1.f;
    }

    // Combined: trans * scale (apply scale first, then translate).
    float vpModel[16];
    MulMat4(trans, scale, vpModel);

    // Full MVP for this viewport.
    float vpMvp[16];
    MulMat4(paperProj, vpModel, vpMvp);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, vpMvp);

    // Render model geometry with viewport clipping.
    glUniform4f(locCol, 0.25f, 0.25f, 0.3f, 1.f);  // Dark gray for model.
    glLineWidth(kLwModel);

    // Render lines.
    if (!userLines.empty() && (lineEntityAttrs == nullptr || lineEntityAttrs->size() * 6 == userLines.size())) {
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(userLines.size() * sizeof(float)), userLines.data(),
                   GL_STATIC_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(userLines.size() / 3));
    }

    // Render circles.
    if (!circlesCxCyR.empty()) {
      for (size_t i = 0; i + 2 < circlesCxCyR.size(); i += 3) {
        const float cx = circlesCxCyR[i];
        const float cy = circlesCxCyR[i + 1];
        const float r = circlesCxCyR[i + 2];
        constexpr int kCircleSegs = 32;
        std::vector<float> circleVerts;
        for (int seg = 0; seg < kCircleSegs; ++seg) {
          const float angle0 = 2.f * 3.14159265f * seg / kCircleSegs;
          const float angle1 = 2.f * 3.14159265f * (seg + 1) / kCircleSegs;
          circleVerts.push_back(cx + r * std::cos(angle0));
          circleVerts.push_back(cy + r * std::sin(angle0));
          circleVerts.push_back(0.f);
          circleVerts.push_back(cx + r * std::cos(angle1));
          circleVerts.push_back(cy + r * std::sin(angle1));
          circleVerts.push_back(0.f);
        }
        if (!circleVerts.empty()) {
          glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(circleVerts.size() * sizeof(float)),
                       circleVerts.data(), GL_STREAM_DRAW);
          glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(circleVerts.size() / 3));
        }
      }
    }

    glDisable(GL_SCISSOR_TEST);
  }

  // Render viewport borders (paper-space coordinates, no scissor clipping).
  glUniform4f(locCol, 0.3f, 0.5f, 0.8f, 1.f);  // Blue for viewport borders.
  glLineWidth(kLwVpBorder);
  glUniformMatrix4fv(locMvp, 1, GL_FALSE, paperMvp);  // Use paper projection only (no model transform).
  for (size_t vi = 0; vi < layout.viewports.size(); ++vi) {
    const Viewport& vp = layout.viewports[vi];
    const float vpRectVerts[] = {
      vp.paperXIn, vp.paperYIn, 0.f,
      vp.paperXIn + vp.paperWIn, vp.paperYIn, 0.f,
      vp.paperXIn + vp.paperWIn, vp.paperYIn + vp.paperHIn, 0.f,
      vp.paperXIn, vp.paperYIn + vp.paperHIn, 0.f,
      vp.paperXIn, vp.paperYIn, 0.f,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(vpRectVerts), vpRectVerts, GL_STREAM_DRAW);
    glDrawArrays(GL_LINE_STRIP, 0, 5);
  }

  glLineWidth(1.35f);
  glBindVertexArray(0);
}

void ViewportRenderer::RenderScene(double panX, double panY, float zoom, int fbWidth, int fbHeight,
                                   const std::vector<float>& userLines, const std::vector<float>& circlesCxCyR,
                                   std::uint32_t cadGpuRevision, const std::vector<float>& rubberLines,
                                   const CadSnap::Hit* snapOverlay, float snapGlyphHalfPx,
                                   const float* selectionFillRect, const std::vector<float>* previewLines,
                                   const std::vector<float>* previewCircles, const std::vector<float>* highlightLines,
                                   const std::vector<float>* highlightCircles, const std::vector<float>* hoverLines,
                                   const std::vector<float>* hoverCircles, const std::vector<float>* surveyMarkers,
                                   const std::vector<EntityAttributes>* lineEntityAttrs,
                                   const std::vector<EntityAttributes>* circleEntityAttrs,
                                   const CadExtendedGeometryInput* extended, bool showGrid,
                                   const std::vector<CadLayerRow>* drawingLayers, const RenderTuning& tuning,
                                   const std::vector<PdfAttachment>* pdfAttachments,
                                   int activeSpaceIndex, const void* paperLayoutsPtr,
                                   double worldDocOriginX, double worldDocOriginY, float modelUnitsPerPlottedInch) {
  if (!EnsureFramebuffer(fbWidth, fbHeight))
    return;

  // MSAA path is gated by "Hardware Acceleration" + "Smooth line display" (Settings → System → Graphics Performance).
  const bool wantMsaa = tuning.hardwareAcceleration && tuning.smoothLineDisplay;
  const bool useMsaa = wantMsaa && EnsureMultisamplePass(fbW_, fbH_);
  if (!wantMsaa && msFbo_)
    DestroyMultisamplePass();
  // GL_LINE_SMOOTH is deprecated in core but supported by most drivers; it provides line antialiasing in the
  // non-multisampled path. Gated by both toggles to match AutoCAD-style behavior.
  if (tuning.hardwareAcceleration && tuning.smoothLineDisplay) {
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  } else {
    glDisable(GL_LINE_SMOOTH);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, (useMsaa && msFbo_) ? msFbo_ : fbo_);
  glViewport(0, 0, fbW_, fbH_);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);

  glClearColor(tuning.bgR, tuning.bgG, tuning.bgB, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // REQ-034 Phase 1: Paper-space rendering via GL scissor pass. If in paper space, render viewports
  // instead of model space. This replaces the ImGui overlay path with GL rendering.
  const bool inPaperSpace = (activeSpaceIndex >= 0);
  if (inPaperSpace && paperLayoutsPtr) {
    const auto& paperLayouts = *static_cast<const std::vector<PaperLayout>*>(paperLayoutsPtr);
    if (activeSpaceIndex < static_cast<int>(paperLayouts.size())) {
      RenderPaperSpace(fbWidth, fbHeight, userLines, circlesCxCyR, cadGpuRevision, rubberLines,
                       snapOverlay, snapGlyphHalfPx, highlightLines, highlightCircles, surveyMarkers,
                       lineEntityAttrs, circleEntityAttrs, extended, drawingLayers, tuning,
                       paperLayouts[activeSpaceIndex], worldDocOriginX, worldDocOriginY,
                       modelUnitsPerPlottedInch);
      // Paper-space rendering is complete; skip model-space rendering.
      goto finish_render;
    }
  }

  const float aspect = static_cast<float>(fbW_) / static_cast<float>(std::max(fbH_, 1));
  // Zoom clamp here matches the wheel/MMB pan clamps in DrawDrawingViewport: wide enough for million-unit drawings
  // without quantizing halfHd at extreme zooms.
  const double halfHd = (1.0 / std::max(static_cast<double>(zoom), 1.e-9)) * 50.0;
  const double halfWd = halfHd * static_cast<double>(aspect);
  const float halfH = static_cast<float>(halfHd);
  const float halfW = static_cast<float>(halfWd);
  const double viewAnchorX = panX;
  const double viewAnchorY = panY;
  float proj[16];
  Ortho(-halfW, halfW, -halfH, halfH, -1000.f, 1000.f, proj);

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

  // --- PDF underlays (rendered first, behind all CAD geometry) ---
  if (pdfAttachments && !pdfAttachments->empty() && texProgram_ && vaoTex_ && vboTex_) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(texProgram_);
    GLint texMvpLoc = glGetUniformLocation(texProgram_, "uMVP");
    GLint texSampLoc = glGetUniformLocation(texProgram_, "uTex");
    glUniformMatrix4fv(texMvpLoc, 1, GL_FALSE, mvp);
    glUniform1i(texSampLoc, 0);
    GLint texAlphaLoc        = glGetUniformLocation(texProgram_, "uAlpha");
    GLint texTransparentBgLoc = glGetUniformLocation(texProgram_, "uTransparentBg");
    GLint texDarkBgLoc        = glGetUniformLocation(texProgram_, "uDarkBg");
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vaoTex_);
    glBindBuffer(GL_ARRAY_BUFFER, vboTex_);

    for (const PdfAttachment& att : *pdfAttachments) {
      if (!att.glTexId || att.pageWidthPts <= 0.f || att.pageHeightPts <= 0.f)
        continue;
      glUniform1f(texAlphaLoc,         std::clamp(att.fade, 0.f, 1.f));
      glUniform1f(texTransparentBgLoc, att.showBackground ? 0.f : 1.f);
      glUniform1f(texDarkBgLoc,        att.snapVisDark ? 1.f : 0.f);

      const float cosR = std::cos(att.rotationDeg * 3.14159265f / 180.f);
      const float sinR = std::sin(att.rotationDeg * 3.14159265f / 180.f);
      const float W    = att.pageWidthPts  * att.scale;
      const float H    = att.pageHeightPts * att.scale;

      // Four corners in local space; UV maps (0,0)=BL to (1,1)=TR.
      auto corner = [&](float px, float py, float u, float v) -> std::array<float, 4> {
        const float lx = att.insertX + px * cosR - py * sinR;
        const float ly = att.insertY + px * sinR + py * cosR;
        // Shift into view-relative space (subtract anchor).
        return {static_cast<float>(lx - viewAnchorX),
                static_cast<float>(ly - viewAnchorY), u, v};
      };

      auto bl = corner(0.f, 0.f, 0.f, 0.f);
      auto br = corner(W,   0.f,  1.f, 0.f);
      auto tr = corner(W,   H,    1.f, 1.f);
      auto tl = corner(0.f, H,    0.f, 1.f);

      // Two triangles: BL-BR-TR, BL-TR-TL
      float verts[24] = {
        bl[0], bl[1], bl[2], bl[3],
        br[0], br[1], br[2], br[3],
        tr[0], tr[1], tr[2], tr[3],
        bl[0], bl[1], bl[2], bl[3],
        tr[0], tr[1], tr[2], tr[3],
        tl[0], tl[1], tl[2], tl[3],
      };
      glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
      glBindTexture(GL_TEXTURE_2D, att.glTexId);
      glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
  }

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
    const float step = niceStep(std::max(halfW, halfH) * 2.f);
    const float spanW = halfW * 2.15f + step * 2.f;
    const float spanH = halfH * 2.15f + step * 2.f;
    const int rawNi = static_cast<int>(std::ceil(spanW / std::max(step, 1e-12f))) + 2;
    const int ni = std::min(512, std::max(4, rawNi));
    const double stepD = static_cast<double>(step);
    const double originX = std::floor(viewAnchorX / stepD) * stepD;
    const double originY = std::floor(viewAnchorY / stepD) * stepD;
    std::vector<float> gridVerts;
    const float gz = -0.02f;
    auto pushGridSeg = [&](double wx0, double wy0, double wx1, double wy1) {
      float rx0 = 0.f;
      float ry0 = 0.f;
      float rx1 = 0.f;
      float ry1 = 0.f;
      WorldToViewRelativeFloat(wx0, wy0, viewAnchorX, viewAnchorY, &rx0, &ry0);
      WorldToViewRelativeFloat(wx1, wy1, viewAnchorX, viewAnchorY, &rx1, &ry1);
      gridVerts.push_back(rx0);
      gridVerts.push_back(ry0);
      gridVerts.push_back(gz);
      gridVerts.push_back(rx1);
      gridVerts.push_back(ry1);
      gridVerts.push_back(gz);
    };
    for (int i = -ni; i <= ni; ++i) {
      const double x = originX + static_cast<double>(i) * stepD;
      pushGridSeg(x, viewAnchorY - static_cast<double>(spanH), x, viewAnchorY + static_cast<double>(spanH));
    }
    for (int i = -ni; i <= ni; ++i) {
      const double y = originY + static_cast<double>(i) * stepD;
      pushGridSeg(viewAnchorX - static_cast<double>(spanW), y, viewAnchorX + static_cast<double>(spanW), y);
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
    // Drift budget for the anchor: cached vertex magnitudes can grow to roughly halfHd + drift. Letting drift reach
    // halfHd * 0.5 keeps view-relative coordinates well within float precision while letting pan move freely without
    // rebuilding the (potentially expensive) circle/arc tessellation cache. The visible model offset is applied via
    // the per-frame MVP translation below, so the cache stays geometrically valid until drift exceeds the budget.
    const double anchorDriftBudget = std::max(halfHd * 0.5, 1.e-12);
    const bool viewAnchorChanged =
        std::fabs(panX - cachedViewAnchorX_) > anchorDriftBudget ||
        std::fabs(panY - cachedViewAnchorY_) > anchorDriftBudget;
    const bool viewScaleChanged =
        cachedHalfHd_ < 0. ||
        fbHeight != cachedFbHeight_ ||
        std::fabs(halfHd - cachedHalfHd_) > std::max(cachedHalfHd_ * 0.001, 1.e-12);
    if (cadGpuRevision != cachedCadGpuRevision_ || viewAnchorChanged || viewScaleChanged) {
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
        float rx0 = 0.f;
        float ry0 = 0.f;
        float rx1 = 0.f;
        float ry1 = 0.f;
        WorldToViewRelativeFloat(static_cast<double>(x0), static_cast<double>(y0), viewAnchorX, viewAnchorY, &rx0,
                                 &ry0);
        WorldToViewRelativeFloat(static_cast<double>(x1), static_cast<double>(y1), viewAnchorX, viewAnchorY, &rx1,
                                 &ry1);
        CadTessellateLinetypeSegmentVc(rx0, ry0, z0, rx1, ry1, z1, lt, dashPatScale, rgba, &cpuVcLines_);
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
            // Arcs share the same chord-pixel target as full circles. The cap is the user-facing
            // "Arc and circle smoothness" Display setting (sweep-scaled so a 90° arc gets ~1/4 of the segments).
            const auto& a = (*extended->arcs)[i];
            const double sweepFrac =
                std::clamp(std::fabs(static_cast<double>(a.sweepRad)) / 6.283185307179586, 0.05, 1.0);
            const int arcCap = std::max(8, static_cast<int>(std::ceil(tuning.arcCircleSmoothnessCap * sweepFrac)));
            const int arcSegs = std::max(
                8, CircleTessellationSegmentCount(static_cast<double>(a.r), static_cast<double>(halfH), fbHeight,
                                                  arcCap));
            AppendArcVcDashed(cpuVcLines_, (*extended->arcs)[i], arcSegs, 0.f, dashPatScale, attr, lr, kLineDefaultR,
                              kLineDefaultG, kLineDefaultB, viewAnchorX, viewAnchorY);
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
            // Ellipses scale segment count with the major semi-axis (worst-case chord length).
            const auto& e = (*extended->ellipses)[i];
            const double majLen =
                std::sqrt(static_cast<double>(e.majVx) * e.majVx + static_cast<double>(e.majVy) * e.majVy);
            const int ellSegs = std::max(
                16, CircleTessellationSegmentCount(majLen, static_cast<double>(halfH), fbHeight,
                                                   tuning.arcCircleSmoothnessCap));
            AppendEllipseVcDashed(cpuVcLines_, (*extended->ellipses)[i], ellSegs, 0.f, dashPatScale, attr, lr,
                                   kLineDefaultR, kLineDefaultG, kLineDefaultB, viewAnchorX, viewAnchorY);
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
                                dashPatScale, viewAnchorX, viewAnchorY);
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
          const float cr = circlesCxCyR[ci * 3 + 2];
          const int circSegs = CircleTessellationSegmentCount(static_cast<double>(cr), static_cast<double>(halfH),
                                                              fbHeight, tuning.arcCircleSmoothnessCap);
          AppendCircleVcDashed(cpuVcCircles_, circlesCxCyR[ci * 3], circlesCxCyR[ci * 3 + 1], cr,
                               circSegs, 0.f, dashPatScale, attr, lr, kCircDefaultR, kCircDefaultG, kCircDefaultB,
                               viewAnchorX, viewAnchorY);
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
      cachedViewAnchorX_ = panX;
      cachedViewAnchorY_ = panY;
      cachedHalfHd_ = halfHd;
      cachedFbHeight_ = fbHeight;
    }

    glUseProgram(vcLineProgram_);
    GLint locVcMvp = glGetUniformLocation(vcLineProgram_, "uMVP");
    // Cached vertex coords are stored relative to cachedViewAnchor (see rebuild block above). The current frame's
    // camera anchor is panX/Y, so we translate by (cachedAnchor - panX/Y) to land each cached vertex at the correct
    // on-screen position. When the cache was rebuilt this frame this offset is zero; otherwise it absorbs all of the
    // accumulated pan without touching the vertex buffer or re-tessellating curves.
    float cachedModel[16];
    TranslateMat(static_cast<float>(cachedViewAnchorX_ - panX), static_cast<float>(cachedViewAnchorY_ - panY), 0.f,
                 cachedModel);
    float cachedMvp[16];
    MulMat4(proj, cachedModel, cachedMvp);
    glUniformMatrix4fv(locVcMvp, 1, GL_FALSE, cachedMvp);
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

  // --- Hover highlight (subtle blue stroke drawn before selection so selection always wins) ---
  if (hoverLines && !hoverLines->empty() && hoverLines->size() % 6 == 0) {
    std::vector<float> hvLineRel;
    ConvertLineVertsWorldToView(*hoverLines, viewAnchorX, viewAnchorY, &hvLineRel);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUniform4f(locCol, 0.45f, 0.72f, 1.f, 1.f);
    glLineWidth(kLwHiLine * 0.72f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(hvLineRel.size() * sizeof(float)), hvLineRel.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(hvLineRel.size() / 3));
    glLineWidth(kLwMain);
  }
  if (hoverCircles && !hoverCircles->empty() && hoverCircles->size() % 3 == 0) {
    std::vector<float> hvCircGeom;
    for (size_t i = 0; i + 2 < hoverCircles->size(); i += 3) {
      const float hr = (*hoverCircles)[i + 2];
      const int hvSegs = CircleTessellationSegmentCount(static_cast<double>(hr), static_cast<double>(halfH), fbHeight,
                                                        tuning.arcCircleSmoothnessCap);
      AppendCircleLineApprox(hvCircGeom, (*hoverCircles)[i], (*hoverCircles)[i + 1], hr, hvSegs, 0.017f,
                             viewAnchorX, viewAnchorY);
    }
    if (!hvCircGeom.empty()) {
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glUniform4f(locCol, 0.45f, 0.72f, 1.f, 1.f);
      glLineWidth(kLwHiCirc * 0.72f);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(hvCircGeom.size() * sizeof(float)), hvCircGeom.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(hvCircGeom.size() / 3));
      glLineWidth(kLwMain);
    }
  }

  // --- Selection highlight (accent stroke on top of committed geometry) ---
  if (highlightLines && !highlightLines->empty() && highlightLines->size() % 6 == 0) {
    std::vector<float> hlLineRel;
    ConvertLineVertsWorldToView(*highlightLines, viewAnchorX, viewAnchorY, &hlLineRel);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUniform4f(locCol, 1.f, 0.92f, 0.15f, 1.f);
    glLineWidth(kLwHiLine);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(hlLineRel.size() * sizeof(float)), hlLineRel.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(hlLineRel.size() / 3));
    glLineWidth(kLwMain);
  }
  if (highlightCircles && !highlightCircles->empty() && highlightCircles->size() % 3 == 0) {
    std::vector<float> hlCircGeom;
    for (size_t i = 0; i + 2 < highlightCircles->size(); i += 3) {
      const float hr = (*highlightCircles)[i + 2];
      const int hlSegs = CircleTessellationSegmentCount(static_cast<double>(hr), static_cast<double>(halfH), fbHeight,
                                                        tuning.arcCircleSmoothnessCap);
      AppendCircleLineApprox(hlCircGeom, (*highlightCircles)[i], (*highlightCircles)[i + 1], hr, hlSegs, 0.018f,
                             viewAnchorX, viewAnchorY);
    }
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
    std::vector<float> rubberRel;
    ConvertLineVertsWorldToView(rubberLines, viewAnchorX, viewAnchorY, &rubberRel);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUniform4f(locCol, 1.f, 0.85f, 0.2f, 1.f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(rubberRel.size() * sizeof(float)), rubberRel.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(rubberRel.size() / 3));
  }

  // --- Window selection preview (semi-transparent fill + outline) ---
  if (selectionFillRect) {
    const float xa = selectionFillRect[0];
    const float xb = selectionFillRect[1];
    const float ya = selectionFillRect[2];
    const float yb = selectionFillRect[3];
    std::vector<float> fillGeom;
    std::vector<float> lineGeom;
    AppendWorldRectFillTris(fillGeom, xa, ya, xb, yb, 0.035f, viewAnchorX, viewAnchorY);
    AppendWorldRectOutline(lineGeom, xa, ya, xb, yb, 0.036f, viewAnchorX, viewAnchorY);
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
    std::vector<float> previewLineRel;
    ConvertLineVertsWorldToView(*previewLines, viewAnchorX, viewAnchorY, &previewLineRel);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(locCol, 1.f, 0.88f, 0.35f, 0.55f);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(previewLineRel.size() * sizeof(float)),
                 previewLineRel.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(previewLineRel.size() / 3));
    glDisable(GL_BLEND);
  }
  if (previewCircles && !previewCircles->empty() && previewCircles->size() % 3 == 0) {
    std::vector<float> circleGeom;
    for (size_t i = 0; i + 2 < previewCircles->size(); i += 3) {
      const float pr = (*previewCircles)[i + 2];
      const int prevSegs =
          CircleTessellationSegmentCount(static_cast<double>(pr), halfHd, fbHeight, tuning.arcCircleSmoothnessCap);
      AppendCircleLineApprox(circleGeom, (*previewCircles)[i], (*previewCircles)[i + 1], pr, prevSegs, 0.032f,
                             viewAnchorX, viewAnchorY);
    }
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
    std::vector<float> surveyRel;
    ConvertLineVertsWorldToView(*surveyMarkers, viewAnchorX, viewAnchorY, &surveyRel);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUseProgram(lineProgram_);
    glUniform4f(locCol, 1.f, 0.48f, 0.12f, 1.f);
    glLineWidth(kLwSurvey);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(surveyRel.size() * sizeof(float)), surveyRel.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(surveyRel.size() / 3));
    glLineWidth(kLwMain);
  }

  // --- Object snap glyph (green, screen-stable size) ---
  if (snapOverlay && snapOverlay->valid) {
    std::vector<float> snapGeom;
    BuildSnapOverlayLines(*snapOverlay, halfH, fbH_, snapGlyphHalfPx, viewAnchorX, viewAnchorY, snapGeom);
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

finish_render:
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
