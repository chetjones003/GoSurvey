#include "UcsIcon.hpp"

#include <imgui.h>

#include <cmath>

#include "viewport/Crosshair3d.hpp"  // shared axis hues + the direction projection (REQ-310)

namespace ucsicon {
namespace {

// Screen-space direction of a world direction, using the camera's own screen basis. Screen Y grows
// DOWNWARD, hence the negated up term.
//
// Projecting the DIRECTION rather than two projected world points is what lets the icon sit in a
// fixed screen corner while still foreshortening correctly: an axis edge-on to the camera collapses
// to a point, which is exactly the visual cue that the axis is pointing at the viewer.
ImVec2 AxisToScreen(const Camera& cam, const ray3d::Vec3& axis, float lenPx) {
  const ray3d::Vec3 right = cam.RightWorld();
  const ray3d::Vec3 up = cam.UpWorld();
  return ImVec2(static_cast<float>(ray3d::Dot(axis, right)) * lenPx,
                -static_cast<float>(ray3d::Dot(axis, up)) * lenPx);
}

void DrawArm(ImDrawList* dl, ImVec2 root, ImVec2 delta, ImU32 col, const char* label, float scale) {
  const float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  const ImVec2 tip(root.x + delta.x, root.y + delta.y);
  // An axis pointing almost straight at the viewer projects to nearly nothing. Drawing a stub and a
  // label there would be noise, so it is omitted - its absence IS the information.
  if (len < 6.f * scale)
    return;
  dl->AddLine(root, tip, col, 2.0f * scale);

  // Arrowhead, built from the arm's own direction so it always points along the axis.
  const float ux = delta.x / len;
  const float uy = delta.y / len;
  const float head = 7.f * scale;
  const ImVec2 back(tip.x - ux * head, tip.y - uy * head);
  dl->AddTriangleFilled(tip, ImVec2(back.x - uy * head * 0.42f, back.y + ux * head * 0.42f),
                        ImVec2(back.x + uy * head * 0.42f, back.y - ux * head * 0.42f), col);

  const ImVec2 ls = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(tip.x + ux * 8.f * scale - ls.x * 0.5f, tip.y + uy * 8.f * scale - ls.y * 0.5f), col, label);
}

}  // namespace

void Draw(ImDrawList* dl, const Camera& cam, const ucs::Ucs& frame, float originX, float originY, float sizePx,
          bool isWorld) {
  (void)isWorld;
  if (!dl || sizePx < 16.f)
    return;

  // Distinct hues per axis, following the near-universal CAD convention (X red, Y green, Z blue) so
  // the icon needs no legend. Alpha is held below full so the icon never competes with the drawing.
  //
  // The hues themselves live in `crosshair3d` (REQ-310) because the 3D crosshair draws the same
  // three axes: if the icon and the cursor disagreed about which axis is green, both would be
  // worse than either alone. One definition, two consumers.
  const ImU32 colX = IM_COL32(crosshair3d::kAxisColorX.r, crosshair3d::kAxisColorX.g,
                              crosshair3d::kAxisColorX.b, 235);
  const ImU32 colY = IM_COL32(crosshair3d::kAxisColorY.r, crosshair3d::kAxisColorY.g,
                              crosshair3d::kAxisColorY.b, 235);
  const ImU32 colZ = IM_COL32(crosshair3d::kAxisColorZ.r, crosshair3d::kAxisColorZ.g,
                              crosshair3d::kAxisColorZ.b, 235);
  const ImU32 colInk = IM_COL32(210, 210, 214, 220);

  const ImVec2 root(originX, originY);
  const float arm = sizePx;
  const float scale = arm / 26.f;

  // Z first, so an in-plane X or Y arm draws over it rather than under - in plan view Z is a dot
  // under the origin and should not sit on top of the marker.
  DrawArm(dl, root, AxisToScreen(cam, frame.zAxis, arm), colZ, "Z", scale);
  DrawArm(dl, root, AxisToScreen(cam, frame.xAxis, arm), colX, "X", scale);
  DrawArm(dl, root, AxisToScreen(cam, frame.yAxis, arm), colY, "Y", scale);

  dl->AddCircleFilled(root, 3.0f * scale, colInk, 12);
}

}  // namespace ucsicon
