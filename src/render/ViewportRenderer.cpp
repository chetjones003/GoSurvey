#include "ViewportRenderer.hpp"

#include "CadLinetype.hpp"
#include "CadSnap.hpp"
#include "geom2d.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
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

// Diffuse-lit triangles for the Shaded visual style (REQ-064 / ADR-026 (f)).
//
// A headlight: the light direction IS the view direction, so a surface facing the camera is fully
// lit and one turning edge-on falls to ambient. That is what makes an orbit read as shape rather
// than as a flat silhouette, and it needs no light position, no scene lighting model and no
// material system — the anti-requirement in ADR-025 (c) still holds.
//
// `abs(dot(N, V))` deliberately, not `max(dot(N, V), 0)`: lighting is TWO-SIDED. Imported meshes
// routinely have inconsistent triangle winding, and open surfaces (a hatch, a wall) are legitimately
// viewed from behind. One-sided lighting renders those faces black, which reads as a hole in the
// model rather than as a back face.
/// Ambient floor for Shaded. Not zero: a surface turned edge-on should read as dark, not as a hole,
/// and geometry that has fallen out of the light must still be pickable by eye.
constexpr float kShadedAmbient = 0.25f;

const char* kShadedVs = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uMVP;
out vec3 vNormal;
void main() {
  gl_Position = uMVP * vec4(aPos, 1.0);
  vNormal = aNormal;
}
)";

const char* kShadedFs = R"(#version 330 core
in vec3 vNormal;
uniform vec4 uColor;
uniform vec3 uViewDir;   // world-space direction the camera looks ALONG
uniform float uAmbient;
out vec4 FragColor;
void main() {
  vec3 n = normalize(vNormal);
  float d = abs(dot(n, normalize(uViewDir)));
  float i = uAmbient + (1.0 - uAmbient) * d;
  FragColor = vec4(uColor.rgb * i, uColor.a);
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

/// A polyline is the one committed type whose vertices each carry their own elevation (REQ-057), so
/// it is tessellated against a per-vertex Z array rather than one flat chain elevation. Passing a
/// single z here drew a polyline up a slope — or one imported from DXF with per-vertex group-38
/// elevations — as if it were level, in every view (REQ-058).
/// Takes the four arrays explicitly rather than reading `eg.polyline*`, so the identical code draws
/// FEATURE LINES too (REQ-087): their store has the same CSR shape, and a feature line is drawn like
/// any other 3D chain. The elevation-point flag is deliberately not consulted — an elevation point
/// lies on the line, so including it changes nothing about the plan shape (ADR-035 (a)).
void AppendChainEdgesVc(std::vector<float>& out, const CadExtendedGeometryInput& eg,
                        const std::vector<float>* V, const std::vector<int>* O,
                        const std::vector<uint8_t>* Cl, const std::vector<EntityAttributes>* At,
                        float defR, float defG, float defB, float dashPatScale, double viewAnchorX,
                        double viewAnchorY) {
  if (!V || !O || O->size() < 2)
    return;
  const int np = static_cast<int>(O->size()) - 1;
  for (int pi = 0; pi < np; ++pi) {
    EntityAttributes attr{};
    if (At && static_cast<size_t>(pi) < At->size())
      attr = (*At)[static_cast<size_t>(pi)];
    // REQ-084 (d): a polyline isolated out is not drawn. Read from `eg` rather than a parameter —
    // the hidden set travels with the extended geometry it describes.
    if (CadEntityIdHidden(eg.hiddenEntityIds, attr.id))
      continue;
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
    // REQ-316 / ADR-047: a segment whose leaving vertex carries a non-zero bulge is a circular arc,
    // expanded here into a fan of chord points so the existing linetype-chain path draws it as a
    // curve. Only applies to the polyline store (feature lines pass a different V and no bulges).
    const std::vector<float>* B =
        (V == eg.polylineVerts && eg.polylineBulge && !eg.polylineBulge->empty()) ? eg.polylineBulge : nullptr;
    std::vector<float> xy;
    std::vector<float> zs;
    xy.reserve(static_cast<size_t>(nv * 2));
    zs.reserve(static_cast<size_t>(nv));
    auto pushViewRel = [&](double wx, double wy, float z) {
      float rx = 0.f, ry = 0.f;
      WorldToViewRelativeFloat(wx, wy, viewAnchorX, viewAnchorY, &rx, &ry);
      xy.push_back(rx);
      xy.push_back(ry);
      zs.push_back(z);
    };
    const int lastK = closed ? nv : nv - 1;  // closed adds the wrap segment nv-1 -> 0
    for (int k = 0; k < nv; ++k) {
      const int vi = v0 + k;
      const double wx0 = static_cast<double>((*V)[static_cast<size_t>(vi * 3 + 0)]);
      const double wy0 = static_cast<double>((*V)[static_cast<size_t>(vi * 3 + 1)]);
      const float z0 = (*V)[static_cast<size_t>(vi * 3 + 2)];  // absolute, not view-relative (ADR-025 D2)
      pushViewRel(wx0, wy0, z0);
      if (k >= lastK)
        continue;
      const float bulge = B ? (*B)[static_cast<size_t>(vi)] : 0.f;
      if (bulge == 0.f)
        continue;
      const int nk = (k + 1) % nv;
      const double wx1 = static_cast<double>((*V)[static_cast<size_t>((v0 + nk) * 3 + 0)]);
      const double wy1 = static_cast<double>((*V)[static_cast<size_t>((v0 + nk) * 3 + 1)]);
      const BulgeArcSpan arc = BulgeArc(wx0, wy0, wx1, wy1, static_cast<double>(bulge));
      if (!arc.valid)
        continue;
      constexpr double kPi = 3.14159265358979323846;
      const int nseg = std::clamp(static_cast<int>(std::ceil(std::fabs(arc.sweep) / (kPi / 24.0))), 2, 96);
      for (int s = 1; s < nseg; ++s) {  // interior points only; endpoints are the polyline vertices
        const double u = arc.startAngle + arc.sweep * (static_cast<double>(s) / nseg);
        pushViewRel(arc.cx + arc.radius * std::cos(u), arc.cy + arc.radius * std::sin(u), z0);
      }
    }
    const int chainN = static_cast<int>(zs.size());
    CadTessellateLinetypeChainVc(xy.data(), chainN, 0.f, closed, lt, dashPatScale, rgba, &out, zs.data());
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
  // A tilted arc (REQ-312) leaves the XY plane, so no single elevation describes it: each sample
  // carries its own Z and the chain is dashed against the per-vertex array a sloped POLYLINE
  // already uses. Sampled through CurvePointAt, so the drawn curve is the curve the snap picks
  // and the DXF writer emits, not a fourth opinion about where it goes.
  if (!IsFlatNormal(a.nx, a.ny, a.nz)) {
    const ucs::Ucs plane = CurvePlane(a);
    std::vector<float> zs(static_cast<size_t>(n) + 1u);
    for (int i = 0; i <= n; ++i) {
      const ray3d::Vec3 p = CurvePointAt(
          plane, dr, CurveSampleAngle(static_cast<double>(a.startRad), static_cast<double>(a.sweepRad), i, n));
      xy[static_cast<size_t>(i * 2)] = static_cast<float>(p.x - viewAnchorX);
      xy[static_cast<size_t>(i * 2 + 1)] = static_cast<float>(p.y - viewAnchorY);
      zs[static_cast<size_t>(i)] = static_cast<float>(p.z);
    }
    CadTessellateLinetypeChainVc(xy.data(), n + 1, z, false, lt, dashPatScale, rgba, &out, zs.data());
    return;
  }
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
                          float defG, float defB, double viewAnchorX, double viewAnchorY, float nx = kFlatNormalX,
                          float ny = kFlatNormalY, float nz = kFlatNormalZ) {
  if (r <= 1e-6f || segments < 1)
    return;
  float rgba[4];
  ResolveEntityRgbaForViewport(attr, lr, defR, defG, defB, rgba);
  const std::string lt = EffectiveEntityLinetypeNameForViewport(attr, lr);
  const double dcx = static_cast<double>(cx);
  const double dcy = static_cast<double>(cy);
  const double dr = static_cast<double>(r);
  constexpr double kTwoPi = 6.283185307179586;
  std::vector<float> xy(static_cast<size_t>((static_cast<size_t>(segments) + 1u) * 2u));
  // Tilted (REQ-312): same per-vertex-Z chain as a tilted arc, sampled through the same
  // parametrisation. The circle's plane is built from its centre and normal, so the ring closes
  // where the frame says it does rather than where a flat projection would put it.
  if (!IsFlatNormal(nx, ny, nz)) {
    const ucs::Ucs plane =
        CurvePlane(dcx, dcy, static_cast<double>(z), static_cast<double>(nx), static_cast<double>(ny),
                   static_cast<double>(nz));
    std::vector<float> zs(static_cast<size_t>(segments) + 1u);
    for (int i = 0; i <= segments; ++i) {
      const ray3d::Vec3 p = CurvePointAt(plane, dr, CurveSampleAngle(0.0, kTwoPi, i, segments));
      xy[static_cast<size_t>(i * 2)] = static_cast<float>(p.x - viewAnchorX);
      xy[static_cast<size_t>(i * 2 + 1)] = static_cast<float>(p.y - viewAnchorY);
      zs[static_cast<size_t>(i)] = static_cast<float>(p.z);
    }
    CadTessellateLinetypeChainVc(xy.data(), segments + 1, z, true, lt, dashPatScale, rgba, &out, zs.data());
    return;
  }
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

/// A snap glyph's drawing frame: the point it marks, plus the camera's right/up axes in world
/// coordinates (REQ-058).
///
/// Snap glyphs are UI markers, not geometry. Built as flat world-XY shapes they lie in the work
/// plane, so an orbit foreshortens them and a near-horizontal view collapses them to an unreadable
/// edge — GAP-2. Building each one as `centre + right*u + up*v` makes it face the viewer at any
/// orientation while keeping the constant pixel size `glyphHalfPx` already gives it.
///
/// \c cx,\c cy are view-relative XY and \c cz is absolute world Z (the mixed convention the whole
/// overlay pipeline uses). Adding a world-space offset is valid in that frame because the
/// view-relative transform is a pure XY translation.
struct SnapGlyphFrame {
  float cx = 0.f, cy = 0.f, cz = 0.f;
  ray3d::Vec3 right{1.0, 0.0, 0.0};
  ray3d::Vec3 up{0.0, 1.0, 0.0};

  /// One segment between two points given in glyph-plane offsets (\p u across, \p v up).
  void seg(std::vector<float>& out, float u0, float v0, float u1, float v1) const {
    auto emit = [&](float u, float v) {
      out.push_back(cx + static_cast<float>(right.x) * u + static_cast<float>(up.x) * v);
      out.push_back(cy + static_cast<float>(right.y) * u + static_cast<float>(up.y) * v);
      out.push_back(cz + static_cast<float>(right.z) * u + static_cast<float>(up.z) * v);
    };
    emit(u0, v0);
    emit(u1, v1);
  }
};

void AppendSnapSquareOutline(std::vector<float>& out, const SnapGlyphFrame& f, float h) {
  f.seg(out, -h, -h, h, -h);
  f.seg(out, h, -h, h, h);
  f.seg(out, h, h, -h, h);
  f.seg(out, -h, h, -h, -h);
}

void AppendSnapTriangleOutline(std::vector<float>& out, const SnapGlyphFrame& f, float h) {
  f.seg(out, 0.f, h, -h, -h);
  f.seg(out, -h, -h, h, -h);
  f.seg(out, h, -h, 0.f, h);
}

void AppendSnapCrossInSquare(std::vector<float>& out, const SnapGlyphFrame& f, float h) {
  f.seg(out, -h, 0.f, h, 0.f);
  f.seg(out, 0.f, -h, 0.f, h);
}

/// Two diagonal segments (×); \p halfDiag is half the segment length along each diagonal from center.
void AppendSnapDiagonalCross(std::vector<float>& out, const SnapGlyphFrame& f, float halfDiag) {
  const float h = halfDiag;
  f.seg(out, -h, -h, h, h);
  f.seg(out, -h, h, h, -h);
}

/// Diamond outline — the apparent-intersection glyph's frame (REQ-062). Distinct at a glance from
/// the endpoint square and the geometric-centre square-plus-cross.
void AppendSnapDiamondOutline(std::vector<float>& out, const SnapGlyphFrame& f, float h) {
  f.seg(out, 0.f, h, h, 0.f);
  f.seg(out, h, 0.f, 0.f, -h);
  f.seg(out, 0.f, -h, -h, 0.f);
  f.seg(out, -h, 0.f, 0.f, h);
}

/// Closed circle approximation in the glyph's own plane, so it stays a circle rather than
/// flattening to an ellipse under orbit.
void AppendSnapCircle(std::vector<float>& out, const SnapGlyphFrame& f, float r, int segments) {
  if (r <= 0.f || segments < 3)
    return;
  constexpr double kTwoPi = 6.283185307179586;
  for (int i = 0; i < segments; ++i) {
    const double t0 = kTwoPi * static_cast<double>(i) / static_cast<double>(segments);
    const double t1 = kTwoPi * static_cast<double>(i + 1) / static_cast<double>(segments);
    f.seg(out, r * static_cast<float>(std::cos(t0)), r * static_cast<float>(std::sin(t0)),
          r * static_cast<float>(std::cos(t1)), r * static_cast<float>(std::sin(t1)));
  }
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

void BuildSnapOverlayLines(const CadSnap::Hit& snap, const Camera& cam, float halfWorld, int fbHeight,
                           float glyphHalfPx, double viewAnchorX, double viewAnchorY, std::vector<float>& out) {
  if (!snap.valid)
    return;
  SnapGlyphFrame f;
  WorldToViewRelativeFloat(static_cast<double>(snap.x), static_cast<double>(snap.y), viewAnchorX, viewAnchorY, &f.cx,
                           &f.cy);
  // The glyph sits at the snapped point's own elevation, with a hair of lift so it still draws
  // over coincident geometry (REQ-057/058). Pinning it to a constant put the marker on the datum
  // while the point it marks was elevated, so it drifted away from the geometry under orbit.
  f.cz = snap.z + 0.045f;
  // Screen-facing, not work-plane-aligned (REQ-058 / GAP-2). In plan view right/up are world +X/+Y,
  // so every glyph below is built from exactly the offsets it used before.
  f.right = cam.RightWorld();
  f.up = cam.UpWorld();
  const float mh = std::clamp(glyphHalfPx, 3.f, 48.f) * (2.f * halfWorld) / static_cast<float>(std::max(fbHeight, 1));
  const int snapCircSegs = std::max(16, static_cast<int>(mh * 40.f));
  switch (snap.kind) {
  case CadSnap::Kind::Endpoint:
    AppendSnapSquareOutline(out, f, mh);
    break;
  case CadSnap::Kind::Midpoint:
    AppendSnapTriangleOutline(out, f, mh);
    break;
  case CadSnap::Kind::Center:
    AppendSnapCircle(out, f, mh * 0.85f, snapCircSegs);
    break;
  case CadSnap::Kind::SurveyCenter: {
    const float R = mh * 0.62f;
    AppendSnapCircle(out, f, R, snapCircSegs);
    // × slightly larger than the circle (tips past radius R in diagonal directions).
    AppendSnapDiagonalCross(out, f, R * 0.78f);
    break;
  }
  case CadSnap::Kind::GeometricCenter:
    AppendSnapSquareOutline(out, f, mh);
    AppendSnapDiagonalCross(out, f, mh * 0.42f);
    break;
  case CadSnap::Kind::Perpendicular:
    AppendSnapSquareOutline(out, f, mh);
    AppendSnapCrossInSquare(out, f, mh * 0.55f);
    break;
  case CadSnap::Kind::Intersection:
    // A plain X — the objects genuinely cross here (REQ-062).
    AppendSnapDiagonalCross(out, f, mh);
    break;
  case CadSnap::Kind::ApparentIntersection:
    AppendSnapDiagonalCross(out, f, mh * 0.62f);
    AppendSnapDiamondOutline(out, f, mh);
    break;
  case CadSnap::Kind::Surface:
    AppendSnapDiamondOutline(out, f, mh);
    break;
  // A solid's edge and face get glyphs of their own rather than borrowing the surface diamond: they
  // are the only two snaps that can land on the SAME pixel as each other, so a shared glyph would
  // leave the user unable to tell which one they are about to commit (REQ-313).
  case CadSnap::Kind::Edge:
    AppendSnapSquareOutline(out, f, mh * 0.55f);
    break;
  case CadSnap::Kind::Face:
    AppendSnapDiamondOutline(out, f, mh * 0.62f);
    AppendSnapSquareOutline(out, f, mh);
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

void ViewportRenderer::ReleaseMeshGpu() {
  for (MeshGpuEntry& e : meshGpu_) {
    if (e.ebo) glDeleteBuffers(1, &e.ebo);
    if (e.vbo) glDeleteBuffers(1, &e.vbo);
    if (e.vao) glDeleteVertexArrays(1, &e.vao);
  }
  meshGpu_.clear();
}

void ViewportRenderer::ReleaseSolidGpu() {
  for (SolidGpuBatch& e : solidGpu_) {
    if (e.faceVbo) glDeleteBuffers(1, &e.faceVbo);
    if (e.faceVao) glDeleteVertexArrays(1, &e.faceVao);
    if (e.edgeVbo) glDeleteBuffers(1, &e.edgeVbo);
    if (e.edgeVao) glDeleteVertexArrays(1, &e.edgeVao);
  }
  solidGpu_.clear();
  solidGpuSig_ = 0;
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

  GLuint shVs = CompileShader(GL_VERTEX_SHADER, kShadedVs);
  GLuint shFs = CompileShader(GL_FRAGMENT_SHADER, kShadedFs);
  if (!shVs || !shFs)
    return false;
  shadedProgram_ = LinkProgram(shVs, shFs);
  if (!shadedProgram_)
    return false;
  glGenVertexArrays(1, &vaoShaded_);
  glGenBuffers(1, &vboShaded_);
  glBindVertexArray(vaoShaded_);
  glBindBuffer(GL_ARRAY_BUFFER, vboShaded_);
  {
    const GLsizei shStride = static_cast<GLsizei>(6 * sizeof(float));  // xyz + normal
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, shStride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, shStride, reinterpret_cast<const void*>(sizeof(float) * 3));
  }
  glBindVertexArray(0);

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
  ReleaseMeshGpu();
  ReleaseSolidGpu();
  if (shadedProgram_) {
    glDeleteProgram(shadedProgram_);
    shadedProgram_ = 0;
  }
  if (vaoShaded_) {
    glDeleteVertexArrays(1, &vaoShaded_);
    vaoShaded_ = 0;
  }
  if (vboShaded_) {
    glDeleteBuffers(1, &vboShaded_);
    vboShaded_ = 0;
  }
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
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_DEPTH24_STENCIL8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msDepthRbo_);

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
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo_);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return status == GL_FRAMEBUFFER_COMPLETE;
}

void ViewportRenderer::SetSize(int width, int height) {
  EnsureFramebuffer(width, height);
}

void ViewportRenderer::RenderScene(const Camera& cam, int fbWidth, int fbHeight,
                                   const std::vector<float>& userLines, const std::vector<float>& circlesCxCyZR,
                                   std::uint32_t cadGpuRevision, const std::vector<float>& rubberLines,
                                   const CadSnap::Hit* snapOverlay, float snapGlyphHalfPx,
                                   const std::vector<float>* previewLines,
                                   const std::vector<float>* previewCircles, const std::vector<float>* highlightLines,
                                   const std::vector<float>* highlightCircles, const std::vector<float>* hoverLines,
                                   const std::vector<float>* hoverCircles, const std::vector<float>* surveyMarkers,
                                   const std::vector<EntityAttributes>* lineEntityAttrs,
                                   const std::vector<EntityAttributes>* circleEntityAttrs,
                                   const CadExtendedGeometryInput* extended, bool showGrid,
                                   const std::vector<CadLayerRow>* drawingLayers, const RenderTuning& tuning,
                                   const std::vector<PdfAttachment>* pdfAttachments,
                                   int activeSpaceIndex,
                                   const std::vector<CadFilledRegion>* filledRegions,
                                   const std::vector<EntityAttributes>* filledRegionAttrs,
                                   const std::vector<std::shared_ptr<const CadMesh>>* meshes,
                                   const std::vector<EntityAttributes>* meshAttrs,
                                   const CadSolidDisplayGeometry* solidGeometry,
                                   const CadSurfaceDisplayGeometry* surfaceGeometry,
                                   const VolumeMapDisplayGeometry* volumeMap,
                                   const std::vector<float>* removalLines,
                                   const std::vector<float>* removalMarkers, const ucs::Ucs* gridFrame,
                                   const CadSubObjectOverlay* subObjectOverlay,
                                   const CadGizmoOverlay* gizmoOverlay) {
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

  // --- Visual style (REQ-064 / ADR-026 (e)) ------------------------------------------------------
  // 2D Wireframe takes exactly the path it always took: depth test off, depth writes off, draw order
  // decides. That is not a style implemented in terms of the new system — it IS the old code path,
  // which is what makes the pixel-parity acceptance condition hold by construction rather than by
  // inspection. The draw ORDER below is untouched for every style, for the same reason.
  const bool depthOn = tuning.visualStyle != VisualStyle::Wireframe2D;
  const bool shadeSurfaces = tuning.visualStyle == VisualStyle::Shaded;
  // Enables depth test + writes for the passes that represent geometry at a real elevation.
  const auto depthForGeometry = [&]() {
    if (depthOn) {
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LEQUAL);
      glDepthMask(GL_TRUE);
    } else {
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
    }
  };
  // Overlays are UI, never occluded: a selection highlight that hides behind the object it is
  // highlighting is a bug, and a snap marker you cannot see is worse than none.
  const auto depthForOverlay = [&]() {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
  };
  depthForGeometry();

  glClearColor(tuning.bgR, tuning.bgG, tuning.bgB, 1.f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  // Paper space: the ImGui overlay in CadUi::DrawDrawingViewport renders the sheet, viewport rects,
  // and model geometry clipped+scaled inside each viewport, all pan/zoom-aware (via its w2s transform).
  // We leave the GL texture as the cleared background and let that overlay draw on top.
  //
  // NOTE: A GL per-viewport scissor pass (RenderPaperSpace) was prototyped here, but it used a fixed
  // full-framebuffer ortho with no pan/zoom, so its sheet/border could never align with the overlay —
  // it appeared as a large blue box anchored to the screen. It is disabled until it can be made
  // pan/zoom-aware and share the overlay's paper→screen mapping. See TASK-008.
  if (activeSpaceIndex >= 0) {
    // Skip GL geometry; the cleared framebuffer is the paper-space background.
    goto finish_render;
  }

  // Model-space geometry, in its own block scope so that `finish_render` sits OUTSIDE the scope
  // of everything declared below.
  //
  // This brace is load-bearing, not cosmetic. Jumping over a declaration *into its scope* is
  // ill-formed C++ ([stmt.dcl]/3) — MSVC rejects it with ~50 × C2362 here, and clang merely
  // accepts what it should not, which is why this stood for so long: the project is built with
  // clang, so no one ever saw it. Jumping to a label outside the scope, as this now does, is
  // legal for both. The epilogue reads only `useMsaa`, `fbo_`, `msFbo_`, `fbW_`, `fbH_`, all
  // declared above the jump, so nothing it needs lives inside this block.
  {
  const float aspect = static_cast<float>(fbW_) / static_cast<float>(std::max(fbH_, 1));
  // Zoom clamp here matches the wheel/MMB pan clamps in DrawDrawingViewport: wide enough for million-unit drawings
  // without quantizing halfHd at extreme zooms.
  // Pan and zoom are carried by the camera now: the target is the pan point and orthoHalfH is the
  // zoom expressed directly. These locals keep the rest of the function unchanged.
  const double panX = cam.targetX;
  const double panY = cam.targetY;
  const double halfHd = static_cast<double>(cam.orthoHalfH);
  const double halfWd = halfHd * static_cast<double>(aspect);
  const float halfH = static_cast<float>(halfHd);
  const float halfW = static_cast<float>(halfWd);
  const double viewAnchorX = panX;
  const double viewAnchorY = panY;
  float proj[16];
  // Near/far come from the camera, not the pre-3D literal +/-1000. That literal was harmless while
  // nothing had a Z; with real elevations it clips every entity above 1000 out of the view, even in
  // plan view where Z cannot affect what is on screen — and a surveyed site sits a few thousand feet
  // up, so that is the entire drawing. Depth testing is off (draw order decides), so a wide range
  // costs nothing.
  Ortho(-halfW, halfW, -halfH, halfH, cam.nearZ, cam.farZ, proj);

  // The camera rotation (REQ-058). Identity in plan view, so the composed matrices below are
  // bit-identical to the pre-3D pipeline until the user actually orbits.
  float viewRot[16];
  cam.ViewRotation(viewRot);

  float model[16];
  // Vertices arrive with XY relative to the view anchor but Z ABSOLUTE, so the camera's target
  // elevation has to be subtracted here — the anchoring only ever covered X and Y. Without it the
  // GL geometry and the ImGui overlay (which projects through Camera::WorldToScreen, and that DOES
  // subtract targetZ) drift apart vertically as soon as the view is panned while orbited, so the
  // crosshair and snap glyph stop landing on the lines they belong to.
  const float panZf = static_cast<float>(cam.targetZ);
  TranslateMat(0.f, 0.f, -panZf, model);

  // MVP = Proj · R · Model. The rotation sits between the projection and every world-space
  // translation, which is what keeps the anchor/pan offset applied in WORLD space (FINDING-3):
  // reversing these two makes geometry swim during orbit at state-plane coordinates.
  float projRot[16];
  MulMat4(proj, viewRot, projRot);

  float mvp[16];
  MulMat4(projRot, model, mvp);

  constexpr GLfloat kLwMain = 1.35f;
  constexpr GLfloat kLwHiLine = 2.65f;
  constexpr GLfloat kLwHiCirc = 2.45f;
  constexpr GLfloat kLwSurvey = 1.65f;
  constexpr GLfloat kLwSnap = 1.35f;
  constexpr GLfloat kLwGizmo = 1.1f;
  // REQ-318 item 14: a hovered FACE reads purple, where a hovered edge or vertex reads the ordinary
  // hover blue. Three sub-object kinds share one cursor and precedence decides between them within
  // a few pixels, so telling them apart has to be possible at a glance rather than by reading the
  // command line (user request, 2026-09-04). Named once here because the fill and the outline must
  // be the same colour or they read as two different things.
  constexpr GLfloat kSubFaceHoverR = 0.72f;
  constexpr GLfloat kSubFaceHoverG = 0.45f;
  constexpr GLfloat kSubFaceHoverB = 1.f;

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
    // A UCS grid is generated in the frame's OWN XY and mapped out to world, so its lines run along
    // the UCS axes and lie in the UCS plane rather than being a world-XY grid seen from an angle
    // (REQ-154). Each vertex carries its real Z, which a tilted plane needs and the flat path does
    // not — hence the separate push rather than a shared one with a constant Z.
    auto pushUcsGridSeg = [&](const ucs::Ucs& f, double u0, double v0, double u1, double v1) {
      const ray3d::Vec3 a = ucs::UcsToWorld(f, {u0, v0, 0.0});
      const ray3d::Vec3 b = ucs::UcsToWorld(f, {u1, v1, 0.0});
      float rx0 = 0.f;
      float ry0 = 0.f;
      float rx1 = 0.f;
      float ry1 = 0.f;
      WorldToViewRelativeFloat(a.x, a.y, viewAnchorX, viewAnchorY, &rx0, &ry0);
      WorldToViewRelativeFloat(b.x, b.y, viewAnchorX, viewAnchorY, &rx1, &ry1);
      gridVerts.push_back(rx0);
      gridVerts.push_back(ry0);
      gridVerts.push_back(static_cast<float>(a.z) + gz);
      gridVerts.push_back(rx1);
      gridVerts.push_back(ry1);
      gridVerts.push_back(static_cast<float>(b.z) + gz);
    };

    if (gridFrame && !ucs::IsWorld(*gridFrame)) {
      // Anchor the grid to the view centre projected INTO the frame, so panning still slides the
      // grid with the drawing instead of leaving it stranded around the UCS origin.
      const ray3d::Vec3 anchorUcs = ucs::WorldToUcs(*gridFrame, {viewAnchorX, viewAnchorY, gridFrame->origin.z});
      const double ou = std::floor(anchorUcs.x / stepD) * stepD;
      const double ov = std::floor(anchorUcs.y / stepD) * stepD;
      for (int i = -ni; i <= ni; ++i) {
        const double u = ou + static_cast<double>(i) * stepD;
        pushUcsGridSeg(*gridFrame, u, ov - static_cast<double>(spanH), u, ov + static_cast<double>(spanH));
      }
      for (int i = -ni; i <= ni; ++i) {
        const double v = ov + static_cast<double>(i) * stepD;
        pushUcsGridSeg(*gridFrame, ou - static_cast<double>(spanW), v, ou + static_cast<double>(spanW), v);
      }
    } else {
      for (int i = -ni; i <= ni; ++i) {
        const double x = originX + static_cast<double>(i) * stepD;
        pushGridSeg(x, viewAnchorY - static_cast<double>(spanH), x, viewAnchorY + static_cast<double>(spanH));
      }
      for (int i = -ni; i <= ni; ++i) {
        const double y = originY + static_cast<double>(i) * stepD;
        pushGridSeg(viewAnchorX - static_cast<double>(spanW), y, viewAnchorX + static_cast<double>(spanW), y);
      }
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

  // --- Imported meshes (REQ-063) -----------------------------------------------------------------
  // Drawn only in Shaded: a two-million-triangle model rendered as edges is a black rectangle, and
  // no requirement asks for a mesh wireframe. In the wireframe styles a mesh is simply not drawn,
  // which is stated in the UI rather than left to be discovered.
  //
  // Placed before the linework so CAD geometry drawn at the same elevation reads on top of a
  // surface rather than being z-fought by it — the same reasoning that puts filled regions under
  // the linework below.
  if (shadeSurfaces && meshes && !meshes->empty()) {
    // Evict entries whose mesh has been erased. The weak_ptr is what makes this safe: a raw pointer
    // key could be matched by a NEW mesh allocated at the freed address, which would draw the wrong
    // geometry from a stale buffer.
    for (size_t i = 0; i < meshGpu_.size();) {
      if (meshGpu_[i].mesh.expired()) {
        if (meshGpu_[i].ebo) glDeleteBuffers(1, &meshGpu_[i].ebo);
        if (meshGpu_[i].vbo) glDeleteBuffers(1, &meshGpu_[i].vbo);
        if (meshGpu_[i].vao) glDeleteVertexArrays(1, &meshGpu_[i].vao);
        meshGpu_.erase(meshGpu_.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        ++i;
      }
    }

    glUseProgram(shadedProgram_);
    const ray3d::Vec3 fwd = cam.ForwardWorld();
    glUniform3f(glGetUniformLocation(shadedProgram_, "uViewDir"), static_cast<float>(fwd.x),
                static_cast<float>(fwd.y), static_cast<float>(fwd.z));
    glUniform1f(glGetUniformLocation(shadedProgram_, "uAmbient"), kShadedAmbient);
    const GLint locShColor = glGetUniformLocation(shadedProgram_, "uColor");
    const GLint locShMvp = glGetUniformLocation(shadedProgram_, "uMVP");
    glDisable(GL_BLEND);

    // Same drift budget as the linework cache: vertices are stored relative to the anchor they were
    // built with, and the residual pan is absorbed by the MVP until it grows large enough to matter
    // for float precision.
    const double meshAnchorDriftBudget = std::max(halfHd * 0.5, 1.e-12);

    for (size_t mi = 0; mi < meshes->size(); ++mi) {
      const std::shared_ptr<const CadMesh>& mp = (*meshes)[mi];
      if (!mp || mp->indices.empty() || mp->vertsXyz.empty())
        continue;
      // Layer visibility: a mesh on an off or frozen layer is not drawn (REQ-063 acceptance).
      const EntityAttributes* attr =
          (meshAttrs && mi < meshAttrs->size()) ? &(*meshAttrs)[mi] : nullptr;
      const CadLayerRow* lr =
          attr ? LookupLayerRowCi(drawingLayers, attr->layer.empty() ? std::string("0") : attr->layer) : nullptr;
      if (lr && (!lr->on || lr->frozen))
        continue;
      // REQ-084 (d): a mesh isolated out is not drawn.
      if (attr && CadEntityIdHidden(extended ? extended->hiddenEntityIds : nullptr, attr->id))
        continue;

      MeshGpuEntry* entry = nullptr;
      for (MeshGpuEntry& e : meshGpu_) {
        if (e.mesh.lock().get() == mp.get()) {
          entry = &e;
          break;
        }
      }
      if (!entry) {
        meshGpu_.push_back(MeshGpuEntry{});
        entry = &meshGpu_.back();
        entry->mesh = mp;
        glGenVertexArrays(1, &entry->vao);
        glGenBuffers(1, &entry->vbo);
        glGenBuffers(1, &entry->ebo);
        glBindVertexArray(entry->vao);
        // Indices never depend on the anchor, so they are uploaded exactly once per mesh.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry->ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mp->indices.size() * sizeof(std::uint32_t)),
                     mp->indices.data(), GL_STATIC_DRAW);
        entry->indexCount = static_cast<int>(mp->indices.size());
        glBindBuffer(GL_ARRAY_BUFFER, entry->vbo);
        const GLsizei shStride = static_cast<GLsizei>(6 * sizeof(float));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, shStride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, shStride, reinterpret_cast<const void*>(sizeof(float) * 3));
        glBindVertexArray(0);
        entry->anchorX = std::numeric_limits<double>::quiet_NaN();  // force the vertex upload below
      }

      const bool anchorStale = !(std::fabs(viewAnchorX - entry->anchorX) <= meshAnchorDriftBudget &&
                                 std::fabs(viewAnchorY - entry->anchorY) <= meshAnchorDriftBudget);
      if (anchorStale) {
        // ONE vertex per vertex — not one per index. For the 2M-triangle case that is 1M vertices
        // (24 MB) uploaded when the anchor moves, in place of 6M expanded vertices (144 MB) every
        // single frame. The indexed draw is what the index array was for.
        cpuShadedTris_.clear();
        cpuShadedTris_.resize(mp->vertsXyz.size() * 2);  // 6 floats per vertex
        const bool haveNormals = mp->normalsXyz.size() == mp->vertsXyz.size();
        const size_t vcount = mp->vertsXyz.size() / 3;
        for (size_t v = 0; v < vcount; ++v) {
          float rx = 0.f;
          float ry = 0.f;
          WorldToViewRelativeFloat(static_cast<double>(mp->vertsXyz[v * 3]),
                                   static_cast<double>(mp->vertsXyz[v * 3 + 1]), viewAnchorX, viewAnchorY, &rx, &ry);
          float* o = &cpuShadedTris_[v * 6];
          o[0] = rx;
          o[1] = ry;
          o[2] = mp->vertsXyz[v * 3 + 2];  // Z is absolute (ADR-025 D2)
          o[3] = haveNormals ? mp->normalsXyz[v * 3] : 0.f;
          o[4] = haveNormals ? mp->normalsXyz[v * 3 + 1] : 0.f;
          o[5] = haveNormals ? mp->normalsXyz[v * 3 + 2] : 1.f;
        }
        glBindBuffer(GL_ARRAY_BUFFER, entry->vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuShadedTris_.size() * sizeof(float)),
                     cpuShadedTris_.data(), GL_STATIC_DRAW);
        entry->anchorX = viewAnchorX;
        entry->anchorY = viewAnchorY;
      }

      // The cached vertices are relative to entry->anchor; absorb the difference in the MVP, before
      // the view rotation — the composition CameraTests pins down.
      float meshModel[16];
      TranslateMat(static_cast<float>(entry->anchorX - panX), static_cast<float>(entry->anchorY - panY), -panZf,
                   meshModel);
      float meshMvp[16];
      MulMat4(projRot, meshModel, meshMvp);
      glUniformMatrix4fv(locShMvp, 1, GL_FALSE, meshMvp);

      glBindVertexArray(entry->vao);
      for (const CadMeshPart& part : mp->parts) {
        const int begin = std::max(0, part.indexBegin);
        const int count = std::max(0, std::min(part.indexCount, entry->indexCount - begin));
        if (count <= 0)
          continue;
        // The part's own colour, unless the entity/layer overrides it — a mesh obeys layer colour
        // like anything else, and falls back to what the source model declared.
        float rgba[4] = {part.r, part.g, part.b, 1.f};
        if (attr && !(attr->color.empty() || attr->color == "ByLayer"))
          ResolveEntityRgbaForViewport(*attr, lr, part.r, part.g, part.b, rgba);
        glUniform4f(locShColor, rgba[0], rgba[1], rgba[2], 1.f);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(static_cast<std::uintptr_t>(begin) * sizeof(std::uint32_t)));
      }
    }
    glBindVertexArray(0);
    glUseProgram(lineProgram_);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
  }

  // --- B-rep solids (REQ-313 / ADR-045) ------------------------------------------------------------
  // Drawn in EVERY visual style, which is the opposite of the mesh rule above and for the reason
  // ADR-026 (c) records: a solid HAS real edges, where a mesh's "edges" are artefacts of whatever
  // resolution an exporter chose. What each style means for a solid:
  //
  //   2D Wireframe  edges only, depth test off — every edge visible, the pre-3D reading.
  //   Hidden        the faces go into the DEPTH buffer with colour writes OFF, then the edges on
  //                 top. That is real hidden-line removal; without the depth-only pass "Hidden"
  //                 would mean nothing for a solid, because there would be nothing to hide behind.
  //   Shaded        lit faces, then the edges on top.
  //
  // The polygon offset is load-bearing, not a tweak: an edge lies EXACTLY on the face it bounds, so
  // without a depth bias half of every silhouette drops out in speckles as the two z-fight.
  //
  // Placed with the meshes, before the linework, so CAD geometry at the same elevation reads on top
  // of a solid rather than being z-fought by it.
  if (solidGeometry && !solidGeometry->empty()) {
    // Persistent GPU residency for the coalesced solid batches (GitHub issue #194). Batching the
    // draw calls was not enough for REQ-100 profile (d): the per-frame CPU vertex transform + stream
    // upload cost ~38 ms at 400 solids on its own. The fix is the mesh path's fix — upload once,
    // then let the MVP do the work every frame — keyed on the assembly signature (the batch list is
    // immutable within one signature) with the same view-anchor-drift re-upload as the mesh cache.
    const size_t nBatches = solidGeometry->solids.size();
    const bool rebuild =
        solidGpuSig_ != solidGeometry->assemblySig || solidGpu_.size() != nBatches;
    if (rebuild) {
      ReleaseSolidGpu();
      solidGpu_.resize(nBatches);
      for (size_t i = 0; i < nBatches; ++i) {
        const CadSolidDisplayBatch& b = solidGeometry->solids[i];
        SolidGpuBatch& e = solidGpu_[i];
        std::memcpy(e.rgba, b.rgba, sizeof(e.rgba));
        e.lineweightMm = b.lineweightMm;
        e.faceVertCount = (!b.triVerts.empty() && b.triVerts.size() % 9 == 0)
                              ? static_cast<int>(b.triVerts.size() / 3)
                              : 0;
        e.edgeVertCount = (!b.edgeVerts.empty() && b.edgeVerts.size() % 6 == 0)
                              ? static_cast<int>(b.edgeVerts.size() / 3)
                              : 0;
        if (e.faceVertCount > 0) {
          glGenVertexArrays(1, &e.faceVao);
          glGenBuffers(1, &e.faceVbo);
          glBindVertexArray(e.faceVao);
          glBindBuffer(GL_ARRAY_BUFFER, e.faceVbo);
          const GLsizei shStride = static_cast<GLsizei>(6 * sizeof(float));
          glEnableVertexAttribArray(0);
          glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, shStride, nullptr);
          glEnableVertexAttribArray(1);
          glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, shStride,
                                reinterpret_cast<const void*>(sizeof(float) * 3));
        }
        if (e.edgeVertCount > 0) {
          glGenVertexArrays(1, &e.edgeVao);
          glGenBuffers(1, &e.edgeVbo);
          glBindVertexArray(e.edgeVao);
          glBindBuffer(GL_ARRAY_BUFFER, e.edgeVbo);
          glEnableVertexAttribArray(0);
          glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(3 * sizeof(float)), nullptr);
        }
        glBindVertexArray(0);
        e.anchorX = std::numeric_limits<double>::quiet_NaN();  // force the vertex upload below
      }
      solidGpuSig_ = solidGeometry->assemblySig;
    }

    // Same drift budget as the mesh/linework caches: vertices are stored relative to the anchor they
    // were built with, and the residual pan is absorbed by the MVP until it grows large enough to
    // matter for float precision. An orbit changes only the camera rotation, so the anchor holds and
    // this loop uploads nothing.
    const double solidDriftBudget = std::max(halfHd * 0.5, 1.e-12);
    std::vector<float> solidRel;
    for (size_t i = 0; i < solidGpu_.size(); ++i) {
      SolidGpuBatch& e = solidGpu_[i];
      const bool anchorStale = !(std::fabs(viewAnchorX - e.anchorX) <= solidDriftBudget &&
                                 std::fabs(viewAnchorY - e.anchorY) <= solidDriftBudget);
      if (!anchorStale)
        continue;
      const CadSolidDisplayBatch& b = solidGeometry->solids[i];
      if (e.faceVertCount > 0) {
        const bool haveNormals = b.triNormals.size() == b.triVerts.size();
        cpuShadedTris_.clear();
        cpuShadedTris_.resize(static_cast<size_t>(e.faceVertCount) * 6);
        for (int v = 0; v < e.faceVertCount; ++v) {
          float rx = 0.f;
          float ry = 0.f;
          WorldToViewRelativeFloat(static_cast<double>(b.triVerts[static_cast<size_t>(v) * 3]),
                                   static_cast<double>(b.triVerts[static_cast<size_t>(v) * 3 + 1]),
                                   viewAnchorX, viewAnchorY, &rx, &ry);
          float* o = &cpuShadedTris_[static_cast<size_t>(v) * 6];
          o[0] = rx;
          o[1] = ry;
          o[2] = b.triVerts[static_cast<size_t>(v) * 3 + 2];  // Z is absolute (ADR-025 D2)
          o[3] = haveNormals ? b.triNormals[static_cast<size_t>(v) * 3] : 0.f;
          o[4] = haveNormals ? b.triNormals[static_cast<size_t>(v) * 3 + 1] : 0.f;
          o[5] = haveNormals ? b.triNormals[static_cast<size_t>(v) * 3 + 2] : 1.f;
        }
        glBindBuffer(GL_ARRAY_BUFFER, e.faceVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuShadedTris_.size() * sizeof(float)),
                     cpuShadedTris_.data(), GL_STATIC_DRAW);
      }
      if (e.edgeVertCount > 0) {
        ConvertLineVertsWorldToView(b.edgeVerts, viewAnchorX, viewAnchorY, &solidRel);
        glBindBuffer(GL_ARRAY_BUFFER, e.edgeVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(solidRel.size() * sizeof(float)),
                     solidRel.data(), GL_STATIC_DRAW);
      }
      e.anchorX = viewAnchorX;
      e.anchorY = viewAnchorY;
    }

    // Faces. Depth-on styles only: 2D Wireframe draws no faces (there would be nothing to hide
    // behind). Hidden occludes without painting; Shaded lights them.
    if (depthOn) {
      glUseProgram(shadedProgram_);
      const ray3d::Vec3 solidFwd = cam.ForwardWorld();
      glUniform3f(glGetUniformLocation(shadedProgram_, "uViewDir"), static_cast<float>(solidFwd.x),
                  static_cast<float>(solidFwd.y), static_cast<float>(solidFwd.z));
      glUniform1f(glGetUniformLocation(shadedProgram_, "uAmbient"), kShadedAmbient);
      const GLint locSolidColor = glGetUniformLocation(shadedProgram_, "uColor");
      const GLint locSolidMvp = glGetUniformLocation(shadedProgram_, "uMVP");
      glDisable(GL_BLEND);
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.f, 1.f);
      if (!shadeSurfaces)
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      for (const SolidGpuBatch& e : solidGpu_) {
        if (e.faceVertCount <= 0)
          continue;
        float solidModel[16];
        TranslateMat(static_cast<float>(e.anchorX - panX), static_cast<float>(e.anchorY - panY), -panZf,
                     solidModel);
        float solidMvp[16];
        MulMat4(projRot, solidModel, solidMvp);
        glUniformMatrix4fv(locSolidMvp, 1, GL_FALSE, solidMvp);
        glUniform4f(locSolidColor, e.rgba[0], e.rgba[1], e.rgba[2], 1.f);
        glBindVertexArray(e.faceVao);
        glDrawArrays(GL_TRIANGLES, 0, e.faceVertCount);
      }
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDisable(GL_POLYGON_OFFSET_FILL);
      glBindVertexArray(0);
    }

    // The edges, in every style — including 2D Wireframe, where they are the only thing a solid
    // draws at all.
    {
      glUseProgram(lineProgram_);
      for (const SolidGpuBatch& e : solidGpu_) {
        if (e.edgeVertCount <= 0)
          continue;
        float solidModel[16];
        TranslateMat(static_cast<float>(e.anchorX - panX), static_cast<float>(e.anchorY - panY), -panZf,
                     solidModel);
        float solidMvp[16];
        MulMat4(projRot, solidModel, solidMvp);
        glUniformMatrix4fv(locMvp, 1, GL_FALSE, solidMvp);
        glUniform4f(locCol, e.rgba[0], e.rgba[1], e.rgba[2], e.rgba[3]);
        glLineWidth(e.lineweightMm >= 0.f ? LineweightMmToDevicePx(e.lineweightMm) : kLwMain);
        glBindVertexArray(e.edgeVao);
        glDrawArrays(GL_LINES, 0, e.edgeVertCount);
      }
      glLineWidth(kLwMain);
      glBindVertexArray(0);
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);  // restore the shared MVP for later line passes
    }
  } else if (!solidGpu_.empty()) {
    // No solids to draw this frame (all erased, all hidden, or the drawing was replaced). Free the
    // GPU buffers rather than hold megabytes of a closed drawing's solids until the next rebuild.
    ReleaseSolidGpu();
  }

  // --- Solid-filled regions (ADR-011): even-odd stencil fill, drawn under the linework so it is plottable
  // and behind outlines. Each loop is fan-triangulated and INVERTed into the stencil (concave + holes via
  // even-odd parity); a covering quad then fills where parity is odd and resets the stencil to 0. ---
  if (filledRegions && !filledRegions->empty()) {
    glUseProgram(lineProgram_);
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glBindBuffer(GL_ARRAY_BUFFER, vboLines_);
    glBindVertexArray(vaoLines_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
    glDisable(GL_BLEND);
    glEnable(GL_STENCIL_TEST);
    std::vector<float> fan;
    for (size_t fi = 0; fi < filledRegions->size(); ++fi) {
      const CadFilledRegion& fr = (*filledRegions)[fi];
      if (fr.loopStart.empty() || fr.vertsXyz.size() < 9)  // < 3 vertices × 3 floats
        continue;
      if (!fr.isSolid())
        continue;  // line-pattern hatches are drawn as clipped lines in the ImGui overlay (REQ-043)
      // REQ-084 (d): a fill isolated out is not drawn.
      if (filledRegionAttrs && fi < filledRegionAttrs->size() &&
          CadEntityIdHidden(extended ? extended->hiddenEntityIds : nullptr, (*filledRegionAttrs)[fi].id))
        continue;
      fan.clear();
      double mnx = 1e300, mxx = -1e300, mny = 1e300, mxy = -1e300;
      // The stencil cover quad spans the region's XY bounds and so needs one elevation; a filled
      // region is planar in practice, so the mean vertex Z is that plane and is exact whenever the
      // loops share an elevation (REQ-058).
      double zSum = 0.;
      int zCount = 0;
      auto vx = [&](int p) { return fr.vertsXyz[static_cast<size_t>(p) * 3 + 0]; };
      auto vy = [&](int p) { return fr.vertsXyz[static_cast<size_t>(p) * 3 + 1]; };
      auto vz = [&](int p) { return fr.vertsXyz[static_cast<size_t>(p) * 3 + 2]; };
      for (size_t li = 0; li < fr.loopStart.size(); ++li) {
        const int begin = fr.loopStart[li];
        const int cnt = fr.loopCount(li);
        if (cnt < 3)
          continue;
        for (int v = 1; v + 1 < cnt; ++v) {
          fan.push_back(vx(begin) - static_cast<float>(viewAnchorX));
          fan.push_back(vy(begin) - static_cast<float>(viewAnchorY));
          fan.push_back(vz(begin));
          fan.push_back(vx(begin + v) - static_cast<float>(viewAnchorX));
          fan.push_back(vy(begin + v) - static_cast<float>(viewAnchorY));
          fan.push_back(vz(begin + v));
          fan.push_back(vx(begin + v + 1) - static_cast<float>(viewAnchorX));
          fan.push_back(vy(begin + v + 1) - static_cast<float>(viewAnchorY));
          fan.push_back(vz(begin + v + 1));
        }
        for (int v = 0; v < cnt; ++v) {
          mnx = std::min(mnx, static_cast<double>(vx(begin + v)));
          mxx = std::max(mxx, static_cast<double>(vx(begin + v)));
          mny = std::min(mny, static_cast<double>(vy(begin + v)));
          mxy = std::max(mxy, static_cast<double>(vy(begin + v)));
          zSum += static_cast<double>(vz(begin + v));
          ++zCount;
        }
      }
      if (fan.empty())
        continue;
      // Pass 1: parity into stencil (no color write).
      //
      // Depth WRITES are off for this pass even when depth testing is on (REQ-064). The fan covers
      // the whole loop including area that even-odd parity will subtract — the holes in a hatch with
      // islands — so letting it write depth would occlude geometry seen through those holes. Only
      // pass 2, which the stencil confines to the region's true area, writes depth.
      const GLboolean wantDepthWrite = depthOn ? GL_TRUE : GL_FALSE;
      glDepthMask(GL_FALSE);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      glStencilFunc(GL_ALWAYS, 0, 0xFF);
      glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(fan.size() * sizeof(float)), fan.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(fan.size() / 3));
      glDepthMask(wantDepthWrite);
      // Pass 2: fill the odd-parity region, resetting stencil to 0 as the covering quad draws.
      float rgba[4] = {0.85f, 0.85f, 0.85f, 1.f};
      if (filledRegionAttrs && fi < filledRegionAttrs->size()) {
        const EntityAttributes& attr = (*filledRegionAttrs)[fi];
        const CadLayerRow* lr = LookupLayerRowCi(drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
        ResolveEntityRgbaForViewport(attr, lr, 0.85f, 0.85f, 0.85f, rgba);
      }
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
      glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
      const float qx0 = static_cast<float>(mnx - viewAnchorX), qy0 = static_cast<float>(mny - viewAnchorY);
      const float qx1 = static_cast<float>(mxx - viewAnchorX), qy1 = static_cast<float>(mxy - viewAnchorY);
      const float qz = (zCount > 0) ? static_cast<float>(zSum / static_cast<double>(zCount)) : 0.f;

      if (shadeSurfaces) {
        // Shaded: the same stencil-confined quad, through the lit program. A filled region is
        // planar and parallel to XY (ADR-025), so its normal is +Z — which is why this dims as the
        // view orbits toward edge-on, and is the visible proof that lighting is live before REQ-063
        // brings surfaces with varied normals.
        const float sq[36] = {qx0, qy0, qz, 0.f, 0.f, 1.f, qx1, qy0, qz, 0.f, 0.f, 1.f,
                              qx1, qy1, qz, 0.f, 0.f, 1.f, qx0, qy0, qz, 0.f, 0.f, 1.f,
                              qx1, qy1, qz, 0.f, 0.f, 1.f, qx0, qy1, qz, 0.f, 0.f, 1.f};
        glUseProgram(shadedProgram_);
        glUniformMatrix4fv(glGetUniformLocation(shadedProgram_, "uMVP"), 1, GL_FALSE, mvp);
        glUniform4f(glGetUniformLocation(shadedProgram_, "uColor"), rgba[0], rgba[1], rgba[2], 1.f);
        const ray3d::Vec3 fwd = cam.ForwardWorld();
        glUniform3f(glGetUniformLocation(shadedProgram_, "uViewDir"), static_cast<float>(fwd.x),
                    static_cast<float>(fwd.y), static_cast<float>(fwd.z));
        glUniform1f(glGetUniformLocation(shadedProgram_, "uAmbient"), kShadedAmbient);
        glBindVertexArray(vaoShaded_);
        glBindBuffer(GL_ARRAY_BUFFER, vboShaded_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(sq), sq, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // Back to the flat program and its buffer for the next region's parity pass.
        glUseProgram(lineProgram_);
        glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
        glBindBuffer(GL_ARRAY_BUFFER, vboLines_);
        glBindVertexArray(vaoLines_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, nullptr);
      } else {
        glUniform4f(locCol, rgba[0], rgba[1], rgba[2], 1.f);
        const float quad[18] = {qx0, qy0, qz, qx1, qy0, qz, qx1, qy1, qz,
                                qx0, qy0, qz, qx1, qy1, qz, qx0, qy1, qz};
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
      }
    }
    glDisable(GL_STENCIL_TEST);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindVertexArray(0);
  }

  glUseProgram(lineProgram_);
  glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
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
  const bool hasCircles = !circlesCxCyZR.empty() && circlesCxCyZR.size() % 4 == 0;
  // REQ-087: this list omitted feature lines, so a drawing holding ONLY a feature line skipped the
  // entire committed-geometry block below and rendered nothing — while still hovering, selecting
  // and zooming to extents, because those are different paths. Now one predicate, tested.
  const bool hasExt = extended && CadExtendedHasDrawableGeometry(*extended);
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

      // REQ-084 (d) / ADR-034: objects isolated out are not drawn. One pointer feeds every append
      // loop below; when nothing is isolated the set is empty and each test is one `empty()` check,
      // so the REQ-100 budget is untouched.
      const std::vector<std::uint64_t>* const hiddenIds = extended ? extended->hiddenEntityIds : nullptr;

      auto appendUserLineSeg = [&](const EntityAttributes& attr, float x0, float y0, float z0, float x1, float y1,
                                   float z1, float dr, float dg, float db) {
        if (CadEntityIdHidden(hiddenIds, attr.id))  // REQ-084 (d)
          return;
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
            if (CadEntityIdHidden(hiddenIds, attr.id))  // REQ-084 (d)
              continue;
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
            // The arc's own elevation, not 0 — a curve drawn on an ELEV work plane must render on
            // that plane, exactly as line segments already do (REQ-058).
            AppendArcVcDashed(cpuVcLines_, (*extended->arcs)[i], arcSegs, a.z, dashPatScale, attr, lr, kLineDefaultR,
                              kLineDefaultG, kLineDefaultB, viewAnchorX, viewAnchorY);
            lineVertTotal = static_cast<int>(cpuVcLines_.size() / 7);
          }
        }
        if (extended->ellipses) {
          for (size_t i = 0; i < extended->ellipses->size(); ++i) {
            EntityAttributes attr{};
            if (extended->ellAttrs && i < extended->ellAttrs->size())
              attr = (*extended->ellAttrs)[i];
            if (CadEntityIdHidden(hiddenIds, attr.id))  // REQ-084 (d)
              continue;
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
            AppendEllipseVcDashed(cpuVcLines_, (*extended->ellipses)[i], ellSegs, e.z, dashPatScale, attr, lr,
                                   kLineDefaultR, kLineDefaultG, kLineDefaultB, viewAnchorX, viewAnchorY);
            lineVertTotal = static_cast<int>(cpuVcLines_.size() / 7);
          }
        }
        // Polylines and feature lines: the same CSR shape and the same tessellation, but each gated
        // on ITS OWN store. The feature-line append used to be nested inside the polyline block, so
        // it inherited whether polylines existed — a coupling with no reason behind it and a second
        // way for feature lines to vanish. REQ-087.
        const auto appendChainStore = [&](const std::vector<float>* V, const std::vector<int>* O,
                                          const std::vector<uint8_t>* Cl,
                                          const std::vector<EntityAttributes>* At) {
          if (!CadChainHasEntities(V, O))
            return;
          const int vb = static_cast<int>(cpuVcLines_.size() / 7);
          // Split the batch on the FIRST entity's weight; per-entity variation is not batched here.
          EntityAttributes attr0{};
          if (At && !At->empty())
            attr0 = (*At)[0];
          const CadLayerRow* lr0 =
              LookupLayerRowCi(drawingLayers, attr0.layer.empty() ? std::string("0") : attr0.layer);
          maybeSplitLineBatch(vb, LineweightMmToDevicePx(EffectiveEntityLineweightMm(attr0, lr0)));
          AppendChainEdgesVc(cpuVcLines_, *extended, V, O, Cl, At, kLineDefaultR, kLineDefaultG,
                             kLineDefaultB, dashPatScale, viewAnchorX, viewAnchorY);
          lineVertTotal = static_cast<int>(cpuVcLines_.size() / 7);
        };
        appendChainStore(extended->polylineVerts, extended->polylineOffsets, extended->polylineClosed,
                         extended->polylineAttrs);
        appendChainStore(extended->featureLineVerts, extended->featureLineOffsets,
                         extended->featureLineClosed, extended->featureLineAttrs);
        if (extended->blockRefs && extended->blockDefs) {
          for (size_t bi = 0; bi < extended->blockRefs->size(); ++bi) {
            EntityAttributes ia{};
            if (extended->blockRefAttrs && bi < extended->blockRefAttrs->size())
              ia = (*extended->blockRefAttrs)[bi];
            if (CadEntityIdHidden(hiddenIds, ia.id))
              continue;
            std::vector<CadBlockWorldSeg> segs;
            CadBlockCollectWorldLines(*extended->blockDefs, (*extended->blockRefs)[bi], ia, &segs);
            for (const CadBlockWorldSeg& s : segs)
              appendUserLineSeg(s.attr, s.x0, s.y0, s.z0, s.x1, s.y1, s.z1, kLineDefaultR, kLineDefaultG,
                                kLineDefaultB);
          }
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
        const size_t nCirc = circlesCxCyZR.size() / 4;
        for (size_t ci = 0; ci < nCirc; ++ci) {
          EntityAttributes attr{};
          if (circleEntityAttrs && ci < circleEntityAttrs->size())
            attr = (*circleEntityAttrs)[ci];
          if (CadEntityIdHidden(hiddenIds, attr.id))  // REQ-084 (d)
            continue;
          const CadLayerRow* lr = LookupLayerRowCi(drawingLayers, attr.layer.empty() ? std::string("0") : attr.layer);
          const int vb = static_cast<int>(cpuVcCircles_.size() / 7);
          const float lwMm = EffectiveEntityLineweightMm(attr, lr);
          maybeSplitCirc(vb, LineweightMmToDevicePx(lwMm));
          const float cr = circlesCxCyZR[ci * 4 + 3];
          const int circSegs = CircleTessellationSegmentCount(static_cast<double>(cr), static_cast<double>(halfH),
                                                              fbHeight, tuning.arcCircleSmoothnessCap);
          // The circle's plane (REQ-312). Absent side-car means flat, which is every circle drawn
          // before the normal existed.
          float cnx = kFlatNormalX;
          float cny = kFlatNormalY;
          float cnz = kFlatNormalZ;
          if (extended && extended->circleNormals)
            CircleNormalAt(*extended->circleNormals, ci, &cnx, &cny, &cnz);
          AppendCircleVcDashed(cpuVcCircles_, circlesCxCyZR[ci * 4], circlesCxCyZR[ci * 4 + 1], cr,
                               circSegs, circlesCxCyZR[ci * 4 + 2], dashPatScale, attr, lr, kCircDefaultR,
                               kCircDefaultG, kCircDefaultB, viewAnchorX, viewAnchorY, cnx, cny, cnz);
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
    TranslateMat(static_cast<float>(cachedViewAnchorX_ - panX), static_cast<float>(cachedViewAnchorY_ - panY),
                 -panZf, cachedModel);  // Z is absolute in the cache too — see the `model` note above
    float cachedMvp[16];
    // Proj · R · Translate — the anchor offset is a WORLD-space correction, so it must be applied
    // before the camera rotation. This is the exact composition asserted by the
    // "Anchor offset composes before the view rotation" test in CameraTests.
    MulMat4(projRot, cachedModel, cachedMvp);
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

  // ============================================================================================
  // From here down everything is OVERLAY — hover, selection, rubber, window-select, transform
  // previews, survey markers, the snap glyph and the gizmo. None of it is depth-tested in any style
  // (REQ-064): it is UI drawn on top of the model, and the draw order that has always decided its
  // layering keeps deciding it.
  //
  // Survey markers are in this group deliberately. They are billboarded, constant-pixel-size
  // markers (TASK-037/GAP-2) — a UI presentation of a point, not a solid at that point — and a
  // surveyor looking for a point behind a shaded surface still needs to see it. AutoCAD occludes
  // POINT objects; we do not, because ours are screen-facing markers. Revisit if that reads wrong
  // once meshes land (REQ-063), when there will be real surfaces to hide behind.
  // ============================================================================================
  depthForOverlay();

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
  if (hoverCircles && !hoverCircles->empty() && hoverCircles->size() % 4 == 0) {
    std::vector<float> hvCircGeom;
    for (size_t i = 0; i + 3 < hoverCircles->size(); i += 4) {
      const float hr = (*hoverCircles)[i + 3];
      const int hvSegs = CircleTessellationSegmentCount(static_cast<double>(hr), static_cast<double>(halfH), fbHeight,
                                                        tuning.arcCircleSmoothnessCap);
      // [i+2] is the circle's elevation. The old literal was a depth-order bias from the flat
      // renderer; depth testing is off (draw order decides), so it only ever misplaced the ring
      // once the view could tilt.
      AppendCircleLineApprox(hvCircGeom, (*hoverCircles)[i], (*hoverCircles)[i + 1], hr, hvSegs,
                             (*hoverCircles)[i + 2], viewAnchorX, viewAnchorY);
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

  // --- Sub-object face tint (REQ-318 item 11) — THE ONE DEPTH-TESTED OVERLAY -------------------
  //
  // The block comment above states the rule this deliberately breaks, so the exception is written
  // where someone changing the rule will read it. A selected FACE of a solid is a patch of a closed
  // volume, not a stroke of 2D linework: drawn never-occluded, a face on the far side glows through
  // the body and reads as being on the near side. The sub-object selection's edges and vertices are
  // NOT here — they arrive through `highlightLines` below and keep the never-occluded treatment,
  // because a line one pixel wide sunk into the surface it lies on is simply gone (D-2026-09-04-a).
  //
  // In 2D Wireframe no solid faces are drawn and nothing has written depth, so every fragment
  // passes GL_LEQUAL against the cleared buffer and the tint draws. That is the intent, not an
  // accident of the state: in the default style the tint is the only way a face selection is
  // visible at all.
  if (subObjectOverlay && !subObjectOverlay->empty()) {
    std::vector<float> subTriRel;
    const auto drawTint = [&](const std::vector<float>& tris, float r, float g, float b, float a) {
      if (tris.empty() || tris.size() % 9 != 0)
        return;
      ConvertLineVertsWorldToView(tris, viewAnchorX, viewAnchorY, &subTriRel);
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glUniform4f(locCol, r, g, b, a);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(subTriRel.size() * sizeof(float)),
                   subTriRel.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(subTriRel.size() / 3));
    };
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);  // tint the face; do not become the surface for anything drawn after it
    // Pulled toward the viewer, or the tint and the face it covers are the same depth and the
    // result is z-fighting speckle rather than a highlight.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.f, -1.f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Hover FIRST, so a selected face drawn over a hovered one wins — the same "selection always
    // wins" ordering the hover and highlight line channels use a few lines above. In practice the
    // two never overlap (BuildSubObjectHoverHighlight emits nothing for an already-selected
    // sub-object); the order is what makes that a belt rather than the only brace.
    //
    // PURPLE for a face, against the blue an edge or a vertex gets, so the three kinds are told
    // apart at a glance rather than by reading the command line (user request, 2026-09-04).
    drawTint(subObjectOverlay->hoverFaceTris, kSubFaceHoverR, kSubFaceHoverG, kSubFaceHoverB, 0.30f);
    // The selection accent, translucent: opaque would hide the shading that says which way the face
    // turns, and on a curved face that shading is how the user reads the shape they just picked.
    drawTint(subObjectOverlay->selectedFaceTris, 1.f, 0.92f, 0.15f, 0.42f);
    glDisable(GL_BLEND);
    glPolygonOffset(0.f, 0.f);
    glDisable(GL_POLYGON_OFFSET_FILL);
    depthForOverlay();  // back to the rule for everything below

    // The face BOUNDARY, and this is the half that actually reads. A translucent fill tints
    // whatever is behind it, and in 2D Wireframe — the default — there is nothing behind it: solids
    // draw no faces there, so the wash lands on the empty viewport and comes out near black. The
    // outline is what makes a face selection visible at all in the style users spend most of their
    // time in, and it is how every CAD package shows this.
    //
    // NOT depth-tested, unlike the fill: it is linework, one pixel wide, and the rule the fill has
    // to break is the rule this obeys — sunk into the surface it traces, it would disappear.
    const auto drawFaceEdges = [&](const std::vector<float>& segs, float r, float g, float b) {
      if (segs.empty() || segs.size() % 6 != 0)
        return;
      ConvertLineVertsWorldToView(segs, viewAnchorX, viewAnchorY, &subTriRel);
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glUniform4f(locCol, r, g, b, 1.f);
      glLineWidth(kLwHiLine);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(subTriRel.size() * sizeof(float)),
                   subTriRel.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(subTriRel.size() / 3));
      glLineWidth(kLwMain);
    };
    drawFaceEdges(subObjectOverlay->hoverFaceEdges, kSubFaceHoverR, kSubFaceHoverG, kSubFaceHoverB);
    drawFaceEdges(subObjectOverlay->selectedFaceEdges, 1.f, 0.92f, 0.15f);
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
  if (highlightCircles && !highlightCircles->empty() && highlightCircles->size() % 4 == 0) {
    std::vector<float> hlCircGeom;
    for (size_t i = 0; i + 3 < highlightCircles->size(); i += 4) {
      const float hr = (*highlightCircles)[i + 3];
      const int hlSegs = CircleTessellationSegmentCount(static_cast<double>(hr), static_cast<double>(halfH), fbHeight,
                                                        tuning.arcCircleSmoothnessCap);
      AppendCircleLineApprox(hlCircGeom, (*highlightCircles)[i], (*highlightCircles)[i + 1], hr, hlSegs,
                             (*highlightCircles)[i + 2], viewAnchorX, viewAnchorY);
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
  // The selection box used to be drawn here, as a world-space rectangle on the XY plane. It moved to
  // a screen-space ImGui overlay in CadUi (TASK-047): world geometry projects to a parallelogram
  // once the view is orbited, and the hit test forms an axis-aligned SCREEN rectangle from the two
  // drag corners — so the drawn box and the region that selects disagreed in every non-plan view.

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

  // --- Material a pending edit will REMOVE (REQ-103 BREAK) ---
  //
  // Opaque, heavier than the geometry, and in a warning colour, because unlike every other preview
  // above this one is drawn directly over the object it describes. Painted after the transform
  // batch so it wins where the two overlap.
  {
    const auto drawRemoval = [&](const std::vector<float>* v, GLfloat width) {
      if (!v || v->empty() || v->size() % 6 != 0)
        return;
      std::vector<float> rel;
      ConvertLineVertsWorldToView(*v, viewAnchorX, viewAnchorY, &rel);
      glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
      glUniform4f(locCol, 0.95f, 0.27f, 0.22f, 1.f);
      glLineWidth(width);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(rel.size() * sizeof(float)), rel.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(rel.size() / 3));
    };
    drawRemoval(removalLines, kLwHiLine);
    drawRemoval(removalMarkers, kLwHiLine);
    if ((removalLines && !removalLines->empty()) || (removalMarkers && !removalMarkers->empty()))
      glLineWidth(kLwMain);
  }

  if (previewCircles && !previewCircles->empty() && previewCircles->size() % 4 == 0) {
    std::vector<float> circleGeom;
    for (size_t i = 0; i + 3 < previewCircles->size(); i += 4) {
      const float pr = (*previewCircles)[i + 3];
      const int prevSegs =
          CircleTessellationSegmentCount(static_cast<double>(pr), halfHd, fbHeight, tuning.arcCircleSmoothnessCap);
      AppendCircleLineApprox(circleGeom, (*previewCircles)[i], (*previewCircles)[i + 1], pr, prevSegs,
                             (*previewCircles)[i + 2], viewAnchorX, viewAnchorY);
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

  // --- REQ-072 band fills (ADR-036 (g)) ---
  // Drawn FIRST, before any surface linework, so the wireframe/contours/border/arrows below all read
  // on top of the opaque interior. One draw call per band, on the same unlit `lineProgram_` used
  // everywhere else in this pass: a two-sided-lit shaded program would stop a triangle from
  // displaying the colour its band prescribes, making the on-screen legend a lie (ADR-036 (g) amends
  // ADR-028 (h) on exactly this ground).
  if (surfaceGeometry && !surfaceGeometry->bandTriangles.empty()) {
    std::vector<float> bandRel;
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUseProgram(lineProgram_);
    for (const SurfaceTriangleBatch& tb : surfaceGeometry->bandTriangles) {
      if (!tb.verts || tb.verts->empty() || tb.verts->size() % 9 != 0)
        continue;
      ConvertLineVertsWorldToView(*tb.verts, viewAnchorX, viewAnchorY, &bandRel);
      glUniform4f(locCol, tb.rgba[0], tb.rgba[1], tb.rgba[2], tb.rgba[3]);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bandRel.size() * sizeof(float)), bandRel.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(bandRel.size() / 3));
    }
  }

  // --- REQ-073 amendment: Volume Dashboard cut/fill map (TASK-095 §6 step 5) ---
  // Drawn over the band fills (if any — a surface can be both banded AND part of a volume
  // comparison) and under the surface linework below, on the same unlit program and for the same
  // reason ADR-036 (g) gives: an opaque comparison colour has to read as the colour it is, not as
  // whatever a lit shader's angle-dependent shading turned it into.
  if (volumeMap && !volumeMap->empty()) {
    std::vector<float> mapRel;
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
    glUseProgram(lineProgram_);
    for (const SurfaceTriangleBatch* tb : {&volumeMap->cut, &volumeMap->fill}) {
      if (!tb->verts || tb->verts->empty() || tb->verts->size() % 9 != 0)
        continue;
      ConvertLineVertsWorldToView(*tb->verts, viewAnchorX, viewAnchorY, &mapRel);
      glUniform4f(locCol, tb->rgba[0], tb->rgba[1], tb->rgba[2], tb->rgba[3]);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mapRel.size() * sizeof(float)), mapRel.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mapRel.size() / 3));
    }
  }

  // --- Generated surface display geometry (REQ-068 / REQ-070 / REQ-072 arrows) ---
  // Drawn before the survey markers so a point's X stays readable on top of its own surface, and in
  // the order the caller assembled: triangles, contours, border, then REQ-072's slope arrows on top.
  //
  // One draw call per batch. A batch is a whole component of a whole surface, so a drawing with two
  // surfaces costs at most a handful — nowhere near enough to be worth interleaving into the vertex-
  // coloured path, and this way the colour a style names is the colour that is set.
  if (surfaceGeometry) {
    std::vector<float> surfRel;
    bool programBound = false;
    for (const SurfaceDisplayBatch& b : surfaceGeometry->lines) {
      if (!b.verts || b.verts->empty() || b.verts->size() % 6 != 0)
        continue;
      ConvertLineVertsWorldToView(*b.verts, viewAnchorX, viewAnchorY, &surfRel);
      if (!programBound) {
        glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);
        glUseProgram(lineProgram_);
        programBound = true;
      }
      glUniform4f(locCol, b.rgba[0], b.rgba[1], b.rgba[2], b.rgba[3]);
      // -1 mm means "no width of its own" all the way down the ByLayer chain, which lands on the
      // same default weight every other line in the drawing gets.
      glLineWidth(b.lineweightMm >= 0.f ? LineweightMmToDevicePx(b.lineweightMm) : kLwMain);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(surfRel.size() * sizeof(float)), surfRel.data(),
                   GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(surfRel.size() / 3));
    }
    if (programBound)
      glLineWidth(kLwMain);  // restore, so the overlay passes below inherit the shared default
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
    BuildSnapOverlayLines(*snapOverlay, cam, halfH, fbH_, snapGlyphHalfPx, viewAnchorX, viewAnchorY, snapGeom);
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

  // --- The translate gizmo (REQ-060, GitHub issue #148 Phase 5 slice 4b) --------------------------
  //
  // LAST, so it sits on top of everything including the corner axis triad above: it is the one
  // overlay the user is about to click, and a handle hidden behind the geometry it manipulates is
  // not a handle. Never depth-tested, for the same reason.
  //
  // The three colours are the triad's OWN colours, taken literally from the block above rather than
  // chosen again here: the corner icon has been telling this user which way X, Y and Z point since
  // long before there was a gizmo, and two widgets disagreeing about that would be worse than
  // either being wrong.
  if (gizmoOverlay && !gizmoOverlay->empty()) {
    depthForOverlay();
    glUniformMatrix4fv(locMvp, 1, GL_FALSE, mvp);  // back to WORLD space, off the triad's screen matrix
    std::vector<float> gizRel;
    const auto drawGizmo = [&](const std::vector<float>& segs, float r, float g, float b, float w) {
      if (segs.empty() || segs.size() % 6 != 0)
        return;
      ConvertLineVertsWorldToView(segs, viewAnchorX, viewAnchorY, &gizRel);
      glUniform4f(locCol, r, g, b, 1.f);
      glLineWidth(w);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(gizRel.size() * sizeof(float)),
                   gizRel.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gizRel.size() / 3));
    };
    // The drag track first, thin, so the handles draw over it.
    drawGizmo(gizmoOverlay->guide, 0.55f, 0.55f, 0.6f, kLwMain);
    static const float kAxisRgb[3][3] = {
        {0.9f, 0.2f, 0.2f}, {0.2f, 0.85f, 0.35f}, {0.25f, 0.55f, 1.f}};
    for (int a = 0; a < 3; ++a) {
      // A hot handle takes the selection accent rather than a brighter version of its own colour:
      // "this is what the click will take" is the same statement a highlight makes everywhere else
      // in this viewport, and it should look the same wherever it is made.
      if (gizmoOverlay->hot[a])
        drawGizmo(gizmoOverlay->axis[a], 1.f, 0.92f, 0.15f, kLwGizmo + 1.f);
      else if (gizmoOverlay->faceMode)
        // The single face-normal handle takes the PURPLE a selected face already wears, not the X
        // handle's red: it is not X, and a widget that said it was would be lying about the one
        // thing it exists to communicate (issue #148 acceptance 4).
        drawGizmo(gizmoOverlay->axis[a], kSubFaceHoverR, kSubFaceHoverG, kSubFaceHoverB, kLwGizmo);
      else
        drawGizmo(gizmoOverlay->axis[a], kAxisRgb[a][0], kAxisRgb[a][1], kAxisRgb[a][2], kLwGizmo);
    }
    glLineWidth(kLwMain);
  }
  }  // end model-space geometry scope (see the note at its opening brace)

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

// REQ-308 / D-2026-08-30-c — write the current resolved viewport image as a 24-bit BMP thumbnail,
// downscaled so the longer side is at most \p maxDim. Reads from fbo_ (colorTex_ is its colour
// attachment), which holds the last RenderScene output — so the caller invokes this right after
// RenderScene for the drawing it wants pictured. Best-effort: returns false on any failure and
// writes nothing. This is the only gl* path for the feature; keeping it here holds invariant §11.6.
bool ViewportRenderer::CaptureThumbnailBmp(const char* pathUtf8, int maxDim) const {
  if (!pathUtf8 || !pathUtf8[0] || fbo_ == 0 || fbW_ < 4 || fbH_ < 4 || maxDim < 8)
    return false;

  const int srcW = fbW_;
  const int srcH = fbH_;
  std::vector<unsigned char> src(static_cast<size_t>(srcW) * static_cast<size_t>(srcH) * 4u);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, srcW, srcH, GL_RGBA, GL_UNSIGNED_BYTE, src.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

  // Target size: preserve aspect, longer side == maxDim (never upscale).
  double scale = static_cast<double>(maxDim) / std::max(srcW, srcH);
  if (scale > 1.0)
    scale = 1.0;
  const int dstW = std::max(1, static_cast<int>(srcW * scale));
  const int dstH = std::max(1, static_cast<int>(srcH * scale));

  // Bottom-up 24-bit BGR rows, DWORD-aligned — the exact layout a Windows BMP stores, and the same
  // bottom-up order glReadPixels already returns, so no vertical flip is needed.
  const int rowB = (dstW * 3 + 3) & ~3;
  std::vector<unsigned char> bmp(static_cast<size_t>(rowB) * static_cast<size_t>(dstH), 0);
  for (int dy = 0; dy < dstH; ++dy) {
    const int sy = std::min(srcH - 1, static_cast<int>(dy / scale));
    unsigned char* dstRow = bmp.data() + static_cast<size_t>(dy) * rowB;
    for (int dx = 0; dx < dstW; ++dx) {
      const int sx = std::min(srcW - 1, static_cast<int>(dx / scale));
      const unsigned char* s = src.data() + (static_cast<size_t>(sy) * srcW + sx) * 4u;
      dstRow[dx * 3 + 0] = s[2];  // B
      dstRow[dx * 3 + 1] = s[1];  // G
      dstRow[dx * 3 + 2] = s[0];  // R
    }
  }

  std::FILE* f = nullptr;
#if defined(_WIN32)
  if (fopen_s(&f, pathUtf8, "wb") != 0)
    f = nullptr;
#else
  f = std::fopen(pathUtf8, "wb");
#endif
  if (!f)
    return false;

  const unsigned int dataSz = static_cast<unsigned int>(bmp.size());
  const unsigned int fileSz = 54u + dataSz;
  unsigned char hdr[54] = {};
  hdr[0] = 'B';
  hdr[1] = 'M';
  std::memcpy(&hdr[2], &fileSz, 4);
  hdr[10] = 54;
  hdr[14] = 40;
  const int w = dstW, h = dstH;
  std::memcpy(&hdr[18], &w, 4);
  std::memcpy(&hdr[22], &h, 4);
  hdr[26] = 1;
  hdr[28] = 24;
  std::memcpy(&hdr[34], &dataSz, 4);
  const bool ok = std::fwrite(hdr, 1, 54, f) == 54 &&
                  std::fwrite(bmp.data(), 1, bmp.size(), f) == bmp.size();
  std::fclose(f);
  return ok;
}
