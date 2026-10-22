#pragma once
// REQ-059: the ViewCube's geometry, hit-testing and click targets. Pure and header-only (Camera math
// only, no Qt, no GL) so the whole widget can be unit-tested without a window; the Qt viewport only
// paints what BuildScene returns and forwards presses to HitTest / TargetFor.
#include "Camera.hpp"

#include <algorithm>
#include <cmath>

namespace viewcube {

inline constexpr float kSizePx = 148.f;  ///< Edge of the widget's square.
inline constexpr float kPadPx = 10.f;    ///< Gap to the viewport's top and right edges.
/// Elevation of the SW isometric home view, atan(1/sqrt(2)): all three axes foreshorten equally.
inline constexpr float kIsometricElevationDeg = 35.26438968f;
inline constexpr float kHomeAzimuthDeg = 45.f;

struct Point {
  float x = 0.f, y = 0.f;
};

/// One cube face and the camera orientation that looks squarely at it (+X east, +Y north, +Z up).
/// Camera azimuth places the eye at (-sin az, -cos az): az 0 is south (FRONT), 90 is west (LEFT, eye
/// on the -X side), 180 is north (BACK), 270 is east (RIGHT). Matches ucs::OrthographicPresets.
struct Face {
  float nx, ny, nz;
  const char* label;
  float azimuthDeg, elevationDeg;
};

inline constexpr Face kFaces[6] = {
    {0.f, 0.f, 1.f, "TOP", 0.f, 90.f},     {0.f, 0.f, -1.f, "BOTTOM", 0.f, -90.f},
    {0.f, -1.f, 0.f, "FRONT", 0.f, 0.f},   {0.f, 1.f, 0.f, "BACK", 180.f, 0.f},
    {-1.f, 0.f, 0.f, "LEFT", 90.f, 0.f},   {1.f, 0.f, 0.f, "RIGHT", 270.f, 0.f},
};

// The two rotation arrows are concentric with the compass ring: separate arcs mirrored about
// kArrowMidDeg in the upper right. Bearings are math-oriented (counter-clockwise from screen right).
inline constexpr float kArrowMidDeg = 42.f;
inline constexpr float kArrowGapDeg = 9.f;   ///< Half the gap between the two arcs.
inline constexpr float kArrowSpanDeg = 30.f; ///< Angular length of one arc, head included.
inline constexpr float kArrowRingScale = 1.22f;
inline constexpr float kArrowHitPadDeg = 6.f;

struct QuadFace {
  int face = 0;  ///< Index into kFaces.
  Point pts[4];
  float depth = 0.f;
  bool visible = false;   ///< Faces away from the camera are not drawn.
  bool labelled = false;  ///< Facing enough to carry its label (not a grazing sliver).
  Point labelAt;
  float labelMaxWidth = 0.f;  ///< Label must be shrunk to fit this many pixels, never omitted.
};

struct CompassLabel {
  const char* text = "";
  Point at;
  bool shown = false;  ///< False when two bearings collapse onto one spot (edge-on views).
  bool behind = false; ///< On the far side of the cube: painted before the faces so the cube hides it.
};

struct Scene {
  float originX = 0.f, originY = 0.f, sizePx = kSizePx;
  Point center;
  float ringRadius = 0.f, arrowRadius = 0.f, arrowBand = 0.f;
  Point home;
  float homeRadius = 0.f;
  static constexpr int kRingSegments = 48;
  Point ring[kRingSegments];   ///< Compass ring, tilted with the camera.
  QuadFace quads[6];           ///< Sorted far to near, so painting in order is correct.
  CompassLabel compass[4];
  float ucsAzimuthOffsetDeg = 0.f;
  float cameraAzimuthDeg = 0.f, cameraElevationDeg = 90.f;
};

/// Top-left corner of the widget in a viewport \p viewportWidthPx wide.
[[nodiscard]] inline Point OriginFor(float viewportWidthPx) { return {viewportWidthPx - kSizePx - kPadPx, kPadPx}; }

[[nodiscard]] inline bool Contains(const Scene& s, float x, float y) {
  return x >= s.originX && x <= s.originX + s.sizePx && y >= s.originY && y <= s.originY + s.sizePx;
}

namespace detail {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDeg = kPi / 180.f;

inline void FaceCorners(const Face& f, float out[4][3]) {
  const bool vertical = std::fabs(f.nz) > 0.5f;
  const float ax[3] = {vertical ? 1.f : 0.f, 0.f, vertical ? 0.f : 1.f};
  const float ux = ax[1] * f.nz - ax[2] * f.ny, uy = ax[2] * f.nx - ax[0] * f.nz, uz = ax[0] * f.ny - ax[1] * f.nx;
  const float vx = f.ny * uz - f.nz * uy, vy = f.nz * ux - f.nx * uz, vz = f.nx * uy - f.ny * ux;
  const float signs[4][2] = {{-1.f, -1.f}, {1.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}};
  for (int i = 0; i < 4; ++i) {
    out[i][0] = f.nx + signs[i][0] * ux + signs[i][1] * vx;
    out[i][1] = f.ny + signs[i][0] * uy + signs[i][1] * vy;
    out[i][2] = f.nz + signs[i][0] * uz + signs[i][1] * vz;
  }
}

inline bool PointInQuad(const Point q[4], float px, float py) {
  bool pos = false, neg = false;
  for (int i = 0; i < 4; ++i) {
    const Point& a = q[i];
    const Point& b = q[(i + 1) % 4];
    const float cross = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
    if (cross > 0.f) pos = true;
    else if (cross < 0.f) neg = true;
    if (pos && neg) return false;
  }
  return true;
}

inline float WrapDeg(float deg) {
  while (deg < 0.f) deg += 360.f;
  while (deg >= 360.f) deg -= 360.f;
  return deg;
}

}  // namespace detail

/// Lay the widget out for \p cam. \p ucsAzimuthOffsetDeg is CadUcsViewAzimuthOffsetDeg: the compass
/// letters and the square-up arrows are relative to the active coordinate system, not the world.
[[nodiscard]] inline Scene BuildScene(const Camera& cam, float originX, float originY, float ucsAzimuthOffsetDeg = 0.f,
                                      float sizePx = kSizePx) {
  using namespace detail;
  Scene s;
  s.originX = originX; s.originY = originY; s.sizePx = sizePx;
  s.ucsAzimuthOffsetDeg = ucsAzimuthOffsetDeg;
  s.cameraAzimuthDeg = cam.azimuthDeg; s.cameraElevationDeg = cam.elevationDeg;
  const float cx = originX + sizePx * 0.5f;
  const float cy = originY + sizePx * 0.55f;  // low enough that the arrow tips clear the top edge
  s.center = {cx, cy};
  s.ringRadius = sizePx * 0.37f;
  s.arrowRadius = s.ringRadius * kArrowRingScale;
  s.arrowBand = s.ringRadius * 0.20f;
  const float cubeScale = sizePx * 0.155f;
  s.homeRadius = sizePx * 0.075f;
  s.home = {originX + s.homeRadius + 4.f, originY + s.homeRadius + 4.f};

  float R[16];
  cam.ViewRotation(R);
  auto proj = [&](float wx, float wy, float wz, float* sx, float* sy, float* depth) {
    *sx = cx + (R[0] * wx + R[4] * wy + R[8] * wz) * cubeScale;
    *sy = cy - (R[1] * wx + R[5] * wy + R[9] * wz) * cubeScale;
    *depth = R[2] * wx + R[6] * wy + R[10] * wz;
  };

  // Compass ring: a circle on the cube's base plane, so it tilts with the view.
  const float ucsRad = ucsAzimuthOffsetDeg * kDeg;
  const float groundR = s.ringRadius / cubeScale;
  constexpr float kGroundZ = -0.92f;
  for (int i = 0; i < Scene::kRingSegments; ++i) {
    const float a = ucsRad + static_cast<float>(i) / Scene::kRingSegments * 2.f * kPi;
    float depth = 0.f;
    proj(groundR * std::sin(a), groundR * std::cos(a), kGroundZ, &s.ring[i].x, &s.ring[i].y, &depth);
  }

  QuadFace quads[6];
  for (int i = 0; i < 6; ++i) {
    const Face& f = kFaces[i];
    float corners[4][3];
    FaceCorners(f, corners);
    QuadFace& q = quads[i];
    q.face = i;
    float mnX = 1e30f, mxX = -1e30f;
    for (int k = 0; k < 4; ++k) {
      float depth = 0.f;
      proj(corners[k][0], corners[k][1], corners[k][2], &q.pts[k].x, &q.pts[k].y, &depth);
      q.depth += depth * 0.25f;
      q.labelAt.x += q.pts[k].x * 0.25f;
      q.labelAt.y += q.pts[k].y * 0.25f;
      mnX = std::min(mnX, q.pts[k].x);
      mxX = std::max(mxX, q.pts[k].x);
    }
    const float facing = R[2] * f.nx + R[6] * f.ny + R[10] * f.nz;
    q.visible = facing > 0.01f;
    q.labelMaxWidth = (mxX - mnX) * 0.86f;
    q.labelled = q.visible && facing > 0.28f && q.labelMaxWidth > 8.f;
  }
  std::sort(quads, quads + 6, [](const QuadFace& a, const QuadFace& b) { return a.depth < b.depth; });
  std::copy(quads, quads + 6, s.quads);

  // Compass letters on the ring; when two collapse onto one spot (edge-on) the nearer one wins.
  const char* const names[4] = {"N", "E", "S", "W"};
  struct Item { CompassLabel label; float depth; } items[4];
  for (int i = 0; i < 4; ++i) {
    const float a = ucsRad + static_cast<float>(i) * 90.f * kDeg;
    items[i].label.text = names[i];
    proj(groundR * std::sin(a), groundR * std::cos(a), kGroundZ, &items[i].label.at.x, &items[i].label.at.y,
         &items[i].depth);
    items[i].label.shown = true;
    items[i].label.behind = items[i].depth < 0.f;
  }
  std::sort(items, items + 4, [](const Item& a, const Item& b) { return a.depth > b.depth; });
  constexpr float kStackPx = 10.f;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < i && items[i].label.shown; ++j) {
      const float dx = items[i].label.at.x - items[j].label.at.x, dy = items[i].label.at.y - items[j].label.at.y;
      if (items[j].label.shown && dx * dx + dy * dy < kStackPx * kStackPx) items[i].label.shown = false;
    }
  for (int i = 0; i < 4; ++i) s.compass[i] = items[i].label;
  return s;
}

enum class Part { None, Face, TurnCcw, TurnCw, Home };

struct Hit {
  Part part = Part::None;
  int face = -1;  ///< Index into kFaces when part == Face.
};

/// Which control sits under (\p x, \p y). Home and the arrows win over the cube they overlap.
[[nodiscard]] inline Hit HitTest(const Scene& s, float x, float y) {
  if (!Contains(s, x, y)) return {};
  const float hdx = x - s.home.x, hdy = y - s.home.y;
  const float homeReach = s.homeRadius * 1.7f;
  if (hdx * hdx + hdy * hdy <= homeReach * homeReach) return {Part::Home, -1};
  // The arrows are concentric with the ring, so the hit region is an annulus split by bearing.
  const float dx = x - s.center.x, dy = s.center.y - y;
  const float dist = std::sqrt(dx * dx + dy * dy);
  if (dist > s.arrowRadius - s.arrowBand && dist < s.arrowRadius + s.arrowBand) {
    const float deg = detail::WrapDeg(std::atan2(dy, dx) / detail::kDeg);
    const float ccwNear = kArrowMidDeg + kArrowGapDeg;
    const float cwNear = kArrowMidDeg - kArrowGapDeg;
    if (deg > ccwNear - kArrowHitPadDeg && deg < ccwNear + kArrowSpanDeg + kArrowHitPadDeg) return {Part::TurnCcw, -1};
    if (deg < cwNear + kArrowHitPadDeg && deg > cwNear - kArrowSpanDeg - kArrowHitPadDeg) return {Part::TurnCw, -1};
  }
  for (int i = 5; i >= 0; --i)
    if (s.quads[i].visible && detail::PointInQuad(s.quads[i].pts, x, y)) return {Part::Face, s.quads[i].face};
  return {};
}

struct Target {
  float azimuthDeg = 0.f, elevationDeg = 90.f;
};

/// The orientation a click on \p hit asks for. False for Part::None. Home is the SW isometric view;
/// a face is its standard view; an arrow squares up with the next compass direction of the active
/// coordinate system (strictly next, so it moves even when already square) and keeps the elevation.
[[nodiscard]] inline bool TargetFor(const Hit& hit, const Camera& cam, float ucsAzimuthOffsetDeg, Target* out) {
  switch (hit.part) {
    case Part::Home:
      *out = {detail::WrapDeg(kHomeAzimuthDeg + ucsAzimuthOffsetDeg), kIsometricElevationDeg};
      return true;
    case Part::Face:
      if (hit.face < 0 || hit.face >= 6) return false;
      *out = {detail::WrapDeg(kFaces[hit.face].azimuthDeg + ucsAzimuthOffsetDeg), kFaces[hit.face].elevationDeg};
      return true;
    case Part::TurnCw:
    case Part::TurnCcw: {
      constexpr float kEps = 1e-4f;
      const float q = (cam.azimuthDeg - ucsAzimuthOffsetDeg) / 90.f;
      const float snapped = hit.part == Part::TurnCw ? (std::floor(q + kEps) + 1.f) * 90.f
                                                     : (std::ceil(q - kEps) - 1.f) * 90.f;
      *out = {detail::WrapDeg(snapped + ucsAzimuthOffsetDeg), cam.elevationDeg};
      return true;
    }
    case Part::None: break;
  }
  return false;
}

}  // namespace viewcube
