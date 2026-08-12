#include "ViewCube.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace viewcube {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg = kPi / 180.f;

// The six cube faces, in the drawing's coordinate convention: +X east, +Y north, +Z up.
// `az`/`el` are the camera orientation that looks squarely AT each face.
struct Face {
  float nx, ny, nz;
  const char* label;
  float az, el;
};

// Elevation +90 looks straight down (plan); azimuth 0 looks north. See Camera::ViewRotation.
constexpr Face kFaces[6] = {
    {0.f, 0.f, 1.f, "TOP", 0.f, 90.f},     {0.f, 0.f, -1.f, "BOTTOM", 0.f, -90.f},
    {0.f, -1.f, 0.f, "FRONT", 0.f, 0.f},   {0.f, 1.f, 0.f, "BACK", 180.f, 0.f},
    {-1.f, 0.f, 0.f, "LEFT", 270.f, 0.f},  {1.f, 0.f, 0.f, "RIGHT", 90.f, 0.f},
};

void FaceCorners(const Face& f, float out[4][3]) {
  const float ax[3] = {std::fabs(f.nz) > 0.5f ? 1.f : 0.f, 0.f, std::fabs(f.nz) > 0.5f ? 0.f : 1.f};
  const float ux = ax[1] * f.nz - ax[2] * f.ny;
  const float uy = ax[2] * f.nx - ax[0] * f.nz;
  const float uz = ax[0] * f.ny - ax[1] * f.nx;
  const float vx = f.ny * uz - f.nz * uy;
  const float vy = f.nz * ux - f.nx * uz;
  const float vz = f.nx * uy - f.ny * ux;
  const float signs[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
  for (int i = 0; i < 4; ++i) {
    out[i][0] = f.nx + signs[i][0] * ux + signs[i][1] * vx;
    out[i][1] = f.ny + signs[i][0] * uy + signs[i][1] * vy;
    out[i][2] = f.nz + signs[i][0] * uz + signs[i][1] * vz;
  }
}

bool PointInQuad(const ImVec2 q[4], float px, float py) {
  bool pos = false, neg = false;
  for (int i = 0; i < 4; ++i) {
    const ImVec2& a = q[i];
    const ImVec2& b = q[(i + 1) % 4];
    const float cross = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
    if (cross > 0.f) pos = true;
    else if (cross < 0.f) neg = true;
    if (pos && neg)
      return false;
  }
  return true;
}

bool InCircle(float px, float py, float cx, float cy, float r) {
  const float dx = px - cx, dy = py - cy;
  return dx * dx + dy * dy <= r * r;
}

/// Text centred at (cx,cy), shrunk to fit \p maxW so long labels like BOTTOM stay inside their
/// face instead of being dropped.
void CenteredLabel(ImDrawList* dl, float cx, float cy, const char* s, ImU32 col, float maxW) {
  ImFont* font = ImGui::GetFont();
  const float base = ImGui::GetFontSize();
  const ImVec2 natural = ImGui::CalcTextSize(s);
  float size = base;
  if (natural.x > maxW && natural.x > 1.f)
    size = std::max(7.f, base * (maxW / natural.x));
  const float w = natural.x * (size / base);
  const float h = natural.y * (size / base);
  dl->AddText(font, size, ImVec2(cx - w * 0.5f, cy - h * 0.5f), col, s);
}

// The rotation control sits CONCENTRIC with the compass ring — it follows the cube's own arc — as
// two SEPARATE arrows in the upper-right, mirrored about `kArrowMidDeg` with a gap between them.
// The left one turns the view counter-clockwise, the right one clockwise.
constexpr float kArrowMidDeg = 42.f;   // both arrows straddle this bearing on the ring
constexpr float kArrowGapDeg = 9.f;    // half-gap between the two arcs
constexpr float kArrowSpanDeg = 30.f;  // angular length of each arc, head included
constexpr float kArrowRingScale = 1.22f;  // radius relative to the compass ring

/// One rotation arrow: a short arc concentric with the ring plus a solid head at its leading end.
///
/// \param a0   angle where the arc begins (the end nearer the gap).
/// \param ccw  true = the arrow turns counter-clockwise, so it sweeps toward increasing angle.
///
/// Screen y is flipped when plotting (`cy - sin a`), so increasing `a` sweeps counter-clockwise on
/// screen; the CCW tangent is `(-sin a, -cos a)` and the CW tangent its negation.
void RotationArrow(ImDrawList* dl, float cx, float cy, float r, float a0Deg, bool ccw, bool hot) {
  const ImU32 col = hot ? IM_COL32(130, 180, 245, 255) : IM_COL32(206, 210, 218, 255);
  const float dir = ccw ? 1.f : -1.f;
  const float headBack = 7.5f;  // degrees of arc given over to the head — the rest is line
  const float a0 = a0Deg * kDeg;
  const float aTip = (a0Deg + dir * kArrowSpanDeg) * kDeg;
  const float aNeck = (a0Deg + dir * (kArrowSpanDeg - headBack)) * kDeg;

  constexpr int kSeg = 14;
  dl->PathClear();
  for (int i = 0; i <= kSeg; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSeg);
    const float a = a0 + (aNeck - a0) * t;
    dl->PathLineTo(ImVec2(cx + std::cos(a) * r, cy - std::sin(a) * r));
  }
  dl->PathStroke(col, 0, 2.8f);

  // Head: tip on the arc at aTip, base straddling the arc at aNeck.
  const float tipX = cx + std::cos(aTip) * r;
  const float tipY = cy - std::sin(aTip) * r;
  const float nx = std::cos(aNeck), ny = -std::sin(aNeck);
  const float wid = r * 0.10f;
  dl->AddTriangleFilled(ImVec2(tipX, tipY), ImVec2(cx + nx * (r - wid), cy + ny * (r - wid)),
                        ImVec2(cx + nx * (r + wid), cy + ny * (r + wid)), col);
}

/// Small house glyph for the home button.
void HomeGlyph(ImDrawList* dl, float cx, float cy, float r, bool hot) {
  const ImU32 col = hot ? IM_COL32(120, 170, 235, 255) : IM_COL32(196, 200, 208, 255);
  const float w = r * 0.95f;
  dl->AddTriangleFilled(ImVec2(cx, cy - r), ImVec2(cx - w, cy), ImVec2(cx + w, cy), col);      // roof
  dl->AddRectFilled(ImVec2(cx - w * 0.62f, cy), ImVec2(cx + w * 0.62f, cy + r * 0.85f), col);  // body
}

}  // namespace

Result Draw(ImDrawList* dl, const Camera& cam, float originX, float originY, float sizePx, float mouseX,
            float mouseY, bool clicked, float ucsAzimuthOffsetDeg) {
  Result res;
  res.azimuthDeg = cam.azimuthDeg;
  res.elevationDeg = cam.elevationDeg;
  if (!dl || sizePx < 40.f)
    return res;

  const float cx = originX + sizePx * 0.5f;
  const float cy = originY + sizePx * 0.55f;  // low enough that the arrow tips clear the top edge
  const float ringR = sizePx * 0.37f;
  const float cubeScale = sizePx * 0.155f;

  res.hovered = (mouseX >= originX && mouseX <= originX + sizePx && mouseY >= originY &&
                 mouseY <= originY + sizePx);

  float R[16];
  cam.ViewRotation(R);
  auto proj = [&](float wx, float wy, float wz, float* sx, float* sy, float* depth) {
    const float camX = R[0] * wx + R[4] * wy + R[8] * wz;
    const float camY = R[1] * wx + R[5] * wy + R[9] * wz;
    const float camZ = R[2] * wx + R[6] * wy + R[10] * wz;
    *sx = cx + camX * cubeScale;
    *sy = cy - camY * cubeScale;
    *depth = camZ;
  };

  // ---- top-row controls: home (left) and the two rotation arrows (right) ----------------------
  const float ctlR = sizePx * 0.075f;
  const float homeX = originX + ctlR + 4.f;
  const float homeY = originY + ctlR + 4.f;
  const bool homeHot = InCircle(mouseX, mouseY, homeX, homeY, ctlR * 1.7f);

  // The arrows are CONCENTRIC with the compass ring, so the hit region is an annulus around that
  // same centre, limited to the arc band each arrow occupies. Splitting by bearing (rather than by
  // screen x, as an off-centre control would) is what keeps the target under the drawn arrow.
  const float arrR = ringR * kArrowRingScale;
  const float adx = mouseX - cx, ady = cy - mouseY;  // ady flipped to math orientation
  const float adist = std::sqrt(adx * adx + ady * ady);
  float aDeg = std::atan2(ady, adx) / kDeg;
  if (aDeg < 0.f)
    aDeg += 360.f;
  const float band = ringR * 0.20f;
  const bool inBand = adist > arrR - band && adist < arrR + band;
  const bool arrCcwHot = inBand && aDeg > kArrowMidDeg + kArrowGapDeg - 6.f &&
                         aDeg < kArrowMidDeg + kArrowGapDeg + kArrowSpanDeg + 6.f;
  const bool arrCwHot = inBand && aDeg < kArrowMidDeg - kArrowGapDeg + 6.f &&
                        aDeg > kArrowMidDeg - kArrowGapDeg - kArrowSpanDeg - 6.f;

  HomeGlyph(dl, homeX, homeY, ctlR, homeHot);
  RotationArrow(dl, cx, cy, arrR, kArrowMidDeg + kArrowGapDeg, /*ccw=*/true, arrCcwHot);
  RotationArrow(dl, cx, cy, arrR, kArrowMidDeg - kArrowGapDeg, /*ccw=*/false, arrCwHot);

  // ---- compass ring ----------------------------------------------------------------------------
  dl->AddCircleFilled(ImVec2(cx, cy), ringR, IM_COL32(48, 52, 60, 210), 48);
  dl->AddCircle(ImVec2(cx, cy), ringR, IM_COL32(96, 102, 112, 255), 48, 1.5f);

  // Compass letters sit where their direction actually projects, so they stay truthful at any
  // orientation (they converge as the view goes edge-on rather than lying about where north is).
  // Directions are expressed in the ACTIVE coordinate system: under a rotated UCS, "N" points
  // along the UCS's north, which is what the rotation arrows below square up to.
  struct Compass { float deg; const char* s; };
  const Compass kCompass[4] = {{0.f, "N"}, {90.f, "E"}, {180.f, "S"}, {270.f, "W"}};
  for (const Compass& c : kCompass) {
    const float a = (c.deg + ucsAzimuthOffsetDeg) * kDeg;
    const float wx = std::sin(a);  // 0 deg = +Y (north), 90 deg = +X (east)
    const float wy = std::cos(a);
    const float camX = R[0] * wx + R[4] * wy;
    const float camY = R[1] * wx + R[5] * wy;
    const float len = std::sqrt(camX * camX + camY * camY);
    if (len < 1e-4f)
      continue;  // edge-on: no stable place for the letter
    const float lx = cx + (camX / len) * (ringR * 0.80f);
    const float ly = cy - (camY / len) * (ringR * 0.80f);
    const ImVec2 ts = ImGui::CalcTextSize(c.s);
    dl->AddText(ImVec2(lx - ts.x * 0.5f, ly - ts.y * 0.5f), IM_COL32(222, 226, 234, 255), c.s);
  }

  // ---- cube --------------------------------------------------------------------------------------
  struct Drawn {
    int face;
    float depth;
    ImVec2 pts[4];
    bool visible;
  };
  Drawn drawn[6];
  for (int i = 0; i < 6; ++i) {
    const Face& f = kFaces[i];
    float corners[4][3];
    FaceCorners(f, corners);
    Drawn& d = drawn[i];
    d.face = i;
    d.depth = 0.f;
    for (int k = 0; k < 4; ++k) {
      float sx, sy, dz;
      proj(corners[k][0], corners[k][1], corners[k][2], &sx, &sy, &dz);
      d.pts[k] = ImVec2(sx, sy);
      d.depth += dz;
    }
    d.depth *= 0.25f;
    const float nz = R[2] * f.nx + R[6] * f.ny + R[10] * f.nz;
    d.visible = nz > 0.01f;
  }
  std::sort(drawn, drawn + 6, [](const Drawn& a, const Drawn& b) { return a.depth < b.depth; });

  int hoverFace = -1;
  for (int i = 5; i >= 0; --i) {
    if (!drawn[i].visible)
      continue;
    if (PointInQuad(drawn[i].pts, mouseX, mouseY)) {
      hoverFace = drawn[i].face;
      break;
    }
  }

  for (int i = 0; i < 6; ++i) {
    const Drawn& d = drawn[i];
    if (!d.visible)
      continue;
    const bool hot = d.face == hoverFace;
    dl->AddConvexPolyFilled(d.pts, 4, hot ? IM_COL32(120, 150, 190, 245) : IM_COL32(150, 154, 162, 235));
    dl->AddPolyline(d.pts, 4, IM_COL32(70, 74, 82, 255), ImDrawFlags_Closed, 1.4f);

    // EVERY visible face is labelled, shrunk to fit its projected width. Dropping labels that did
    // not fit (the first implementation) meant BOTTOM and FRONT silently never appeared.
    const Face& f = kFaces[d.face];
    float lx = 0.f, ly = 0.f, mnX = 1e30f, mxX = -1e30f;
    for (int k = 0; k < 4; ++k) {
      lx += d.pts[k].x;
      ly += d.pts[k].y;
      mnX = std::min(mnX, d.pts[k].x);
      mxX = std::max(mxX, d.pts[k].x);
    }
    lx *= 0.25f;
    ly *= 0.25f;
    const float availW = (mxX - mnX) * 0.86f;
    if (availW > 8.f)
      CenteredLabel(dl, lx, ly, f.label, IM_COL32(28, 30, 36, 255), availW);
  }

  // ---- interaction --------------------------------------------------------------------------------
  if (clicked) {
    if (homeHot) {
      // Home = SW isometric: camera in the south-west looking north-east and down, the AutoCAD
      // default 3D view. Relative to the active coordinate system.
      res.changed = true;
      res.azimuthDeg = 45.f + ucsAzimuthOffsetDeg;
      res.elevationDeg = kIsometricElevationDeg;
    } else if (arrCcwHot || arrCwHot) {
      // Square up with the next compass direction (N/E/S/W) of the ACTIVE coordinate system, in
      // the arrow's direction. Snapping to the next multiple of 90 — rather than adding 90 — means
      // an off-angle view lands square on the first press instead of staying off by the remainder.
      const float rel = cam.azimuthDeg - ucsAzimuthOffsetDeg;
      const float q = rel / 90.f;
      constexpr float kEps = 1e-4f;
      // Strictly the next / previous quarter turn, so a press always moves even when already square.
      const float snapped = arrCwHot ? (std::floor(q + kEps) + 1.f) * 90.f : (std::ceil(q - kEps) - 1.f) * 90.f;
      float az = snapped + ucsAzimuthOffsetDeg;
      while (az < 0.f) az += 360.f;
      while (az >= 360.f) az -= 360.f;
      res.changed = true;
      res.azimuthDeg = az;
      res.elevationDeg = cam.elevationDeg;  // squaring up is a rotation only; the tilt is kept
    } else if (hoverFace >= 0) {
      res.changed = true;
      res.azimuthDeg = kFaces[hoverFace].az + ucsAzimuthOffsetDeg;
      res.elevationDeg = kFaces[hoverFace].el;
    }
  }
  return res;
}

}  // namespace viewcube
