#include "UcsIcon.hpp"

#include <imgui.h>

#include <cmath>

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

void DrawArm(ImDrawList* dl, ImVec2 root, ImVec2 delta, ImU32 col, const char* label) {
  const float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
  const ImVec2 tip(root.x + delta.x, root.y + delta.y);
  // An axis pointing almost straight at the viewer projects to nearly nothing. Drawing a stub and a
  // label there would be noise, so it is omitted - its absence IS the information.
  if (len < 6.f)
    return;
  dl->AddLine(root, tip, col, 2.0f);

  // Arrowhead, built from the arm's own direction so it always points along the axis.
  const float ux = delta.x / len;
  const float uy = delta.y / len;
  const float head = 7.f;
  const ImVec2 back(tip.x - ux * head, tip.y - uy * head);
  dl->AddTriangleFilled(tip, ImVec2(back.x - uy * head * 0.42f, back.y + ux * head * 0.42f),
                        ImVec2(back.x + uy * head * 0.42f, back.y - ux * head * 0.42f), col);

  const ImVec2 ls = ImGui::CalcTextSize(label);
  dl->AddText(ImVec2(tip.x + ux * 8.f - ls.x * 0.5f, tip.y + uy * 8.f - ls.y * 0.5f), col, label);
}

}  // namespace

void Draw(ImDrawList* dl, const Camera& cam, const ucs::Ucs& frame, float originX, float originY, float sizePx,
          bool isWorld) {
  if (!dl || sizePx < 16.f)
    return;

  // Distinct hues per axis, following the near-universal CAD convention (X red, Y green, Z blue) so
  // the icon needs no legend. Alpha is held below full so the icon never competes with the drawing.
  const ImU32 colX = IM_COL32(226, 96, 96, 235);
  const ImU32 colY = IM_COL32(112, 198, 112, 235);
  const ImU32 colZ = IM_COL32(110, 150, 235, 235);
  const ImU32 colInk = IM_COL32(210, 210, 214, 220);

  const ImVec2 root(originX, originY);
  const float arm = sizePx;

  // Z first, so an in-plane X or Y arm draws over it rather than under - in plan view Z is a dot
  // under the origin and should not sit on top of the marker.
  DrawArm(dl, root, AxisToScreen(cam, frame.zAxis, arm), colZ, "Z");
  DrawArm(dl, root, AxisToScreen(cam, frame.xAxis, arm), colX, "X");
  DrawArm(dl, root, AxisToScreen(cam, frame.yAxis, arm), colY, "Y");

  dl->AddCircleFilled(root, 3.0f, colInk, 12);
  if (isWorld) {
    // AutoCAD's convention: a "W" at the origin means the frame is the World Coordinate System.
    // Its absence is how a user tells at a glance that they are working in a UCS.
    const ImVec2 ws = ImGui::CalcTextSize("W");
    dl->AddText(ImVec2(root.x - ws.x - 5.f, root.y + 3.f), colInk, "W");
  }
}

}  // namespace ucsicon
