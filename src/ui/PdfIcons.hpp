#pragma once

// Small vector icons for the PDF viewer's buttons, drawn with ImGui's draw list (no image files, so they stay sharp at any
// scale and take the button's text colour, including the dimmed colour of a disabled button).

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace pdfview {

enum class Icon {
  None, Prev, Next, Up, Down, ZoomOut, ZoomIn, FitWidth, FitPage, Split, Compare, Select, Text, Line, Rect, Ellipse, Leader,
  Calibrate, Length, PolyLength, Area, Angle, Check, Undo, Redo, Delete, SaveAs, Scale, Snap, Align, Find, Markup, OnePoint,
  TwoPoints, Reset, Close
};

/// Draw \p icon in the box of side \p s whose top-left corner is \p p.
inline void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 p, float s, ImU32 col) {
  const float k = s / 16.f, th = std::max(1.2f, 1.5f * k);
  const auto P = [&](float x, float y) { return ImVec2(p.x + x * k, p.y + y * k); };
  const auto line = [&](float x0, float y0, float x1, float y1) { dl->AddLine(P(x0, y0), P(x1, y1), col, th); };
  const auto poly = [&](std::initializer_list<ImVec2> pts, bool closed) {
    ImVec2 v[16];
    int n = 0;
    for (const ImVec2& q : pts)
      v[n++] = P(q.x, q.y);
    dl->AddPolyline(v, n, col, closed ? ImDrawFlags_Closed : ImDrawFlags_None, th);
  };
  const auto circle = [&](float cx, float cy, float r, bool fill) {
    if (fill)
      dl->AddCircleFilled(P(cx, cy), r * k, col, 16);
    else
      dl->AddCircle(P(cx, cy), r * k, col, 16, th);
  };
  switch (icon) {
  case Icon::Prev: poly({{10, 3}, {5, 8}, {10, 13}}, false); break;
  case Icon::Next: poly({{6, 3}, {11, 8}, {6, 13}}, false); break;
  case Icon::Up: poly({{3, 10}, {8, 5}, {13, 10}}, false); break;
  case Icon::Down: poly({{3, 6}, {8, 11}, {13, 6}}, false); break;
  case Icon::ZoomOut:
  case Icon::ZoomIn:
  case Icon::Find:
    circle(7, 7, 4.5f, false);
    line(10.5f, 10.5f, 14, 14);
    line(5, 7, 9, 7);
    if (icon != Icon::ZoomOut)
      line(7, 5, 7, 9);
    break;
  case Icon::FitWidth:
    line(2, 3, 2, 13);
    line(14, 3, 14, 13);
    line(4.5f, 8, 11.5f, 8);
    poly({{7, 5.5f}, {4.5f, 8}, {7, 10.5f}}, false);
    poly({{9, 5.5f}, {11.5f, 8}, {9, 10.5f}}, false);
    break;
  case Icon::FitPage:
    poly({{2, 6}, {2, 2}, {6, 2}}, false);
    poly({{10, 2}, {14, 2}, {14, 6}}, false);
    poly({{14, 10}, {14, 14}, {10, 14}}, false);
    poly({{6, 14}, {2, 14}, {2, 10}}, false);
    break;
  case Icon::Split:
    poly({{3, 2}, {13, 2}, {13, 14}, {3, 14}}, true);
    line(3, 8, 5.5f, 8);
    line(7, 8, 9, 8);
    line(10.5f, 8, 13, 8);
    break;
  case Icon::Compare:
    poly({{2, 3}, {10, 3}, {10, 11}, {2, 11}}, true);
    poly({{6, 6}, {14, 6}, {14, 14}, {6, 14}}, true);
    break;
  case Icon::Select: poly({{4, 2}, {4, 13}, {7, 10}, {9.5f, 14}, {11.5f, 13}, {9, 9}, {13, 9}}, true); break;
  case Icon::Text:
    line(3, 3, 13, 3);
    line(8, 3, 8, 13);
    break;
  case Icon::Line:
    line(3, 13, 13, 3);
    circle(3, 13, 1.6f, true);
    circle(13, 3, 1.6f, true);
    break;
  case Icon::Rect: poly({{2.5f, 4}, {13.5f, 4}, {13.5f, 12}, {2.5f, 12}}, true); break;
  case Icon::Ellipse: {
    ImVec2 v[24];
    for (int i = 0; i < 24; ++i) {
      const float a = static_cast<float>(i) / 24.f * 6.2831853f;
      v[i] = P(8 + 6.2f * std::cos(a), 8 + 4.3f * std::sin(a));
    }
    dl->AddPolyline(v, 24, col, ImDrawFlags_Closed, th);
    break;
  }
  case Icon::Leader:
    poly({{8, 2}, {14, 2}, {14, 8}, {8, 8}}, true);
    line(9, 7, 3, 13);
    poly({{3, 9}, {3, 13}, {7, 13}}, false);
    break;
  case Icon::Calibrate:
    circle(3.5f, 12.5f, 1.8f, true);
    circle(12.5f, 3.5f, 1.8f, true);
    line(3.5f, 12.5f, 12.5f, 3.5f);
    break;
  case Icon::Length:
    line(2, 8, 14, 8);
    line(2, 5, 2, 11);
    line(14, 5, 14, 11);
    poly({{5, 6}, {2, 8}, {5, 10}}, false);
    poly({{11, 6}, {14, 8}, {11, 10}}, false);
    break;
  case Icon::PolyLength: poly({{2, 12}, {6, 5}, {10, 10}, {14, 3}}, false); break;
  case Icon::Area: poly({{3, 12}, {4, 4}, {12, 3}, {13, 11}}, true); break;
  case Icon::Angle:
    line(2, 13, 14, 13);
    line(2, 13, 11, 3.5f);
    poly({{7, 13}, {6.5f, 11}, {5.5f, 9.5f}}, false);
    break;
  case Icon::Check: poly({{3, 8.5f}, {6.5f, 12}, {13, 4}}, false); break;
  case Icon::Undo:
    poly({{12, 13}, {12, 8}, {9.5f, 5.5f}, {4.5f, 5.5f}}, false);
    poly({{7.5f, 2.5f}, {4.5f, 5.5f}, {7.5f, 8.5f}}, false);
    break;
  case Icon::Redo:
    poly({{4, 13}, {4, 8}, {6.5f, 5.5f}, {11.5f, 5.5f}}, false);
    poly({{8.5f, 2.5f}, {11.5f, 5.5f}, {8.5f, 8.5f}}, false);
    break;
  case Icon::Delete:
  case Icon::Close:
    line(4, 4, 12, 12);
    line(12, 4, 4, 12);
    break;
  case Icon::SaveAs:
    poly({{3, 3}, {11, 3}, {13, 5}, {13, 13}, {3, 13}}, true);
    poly({{5.5f, 3}, {5.5f, 6.5f}, {10.5f, 6.5f}, {10.5f, 3}}, false);
    poly({{5.5f, 13}, {5.5f, 9.5f}, {10.5f, 9.5f}, {10.5f, 13}}, false);
    break;
  case Icon::Scale:
    poly({{1.5f, 5.5f}, {14.5f, 5.5f}, {14.5f, 11}, {1.5f, 11}}, true);
    line(4.5f, 5.5f, 4.5f, 8.5f);
    line(8, 5.5f, 8, 9);
    line(11.5f, 5.5f, 11.5f, 8.5f);
    break;
  case Icon::Snap:
    circle(8, 8, 3, false);
    line(8, 1.5f, 8, 5);
    line(8, 11, 8, 14.5f);
    line(1.5f, 8, 5, 8);
    line(11, 8, 14.5f, 8);
    break;
  case Icon::Align:
    circle(8, 8, 5, false);
    circle(8, 8, 1.3f, true);
    line(8, 1, 8, 3.5f);
    line(8, 12.5f, 8, 15);
    line(1, 8, 3.5f, 8);
    line(12.5f, 8, 15, 8);
    break;
  case Icon::Markup:
    poly({{3, 3}, {13, 3}, {13, 13}, {3, 13}}, true);
    line(5.5f, 10.5f, 10.5f, 5.5f);
    line(5.5f, 10.5f, 5.5f, 8.5f);
    break;
  case Icon::OnePoint: circle(8, 8, 3.2f, true); break;
  case Icon::TwoPoints:
    circle(3.5f, 12, 2.2f, true);
    circle(12.5f, 4, 2.2f, true);
    line(5, 10.5f, 11, 5.5f);
    break;
  case Icon::Reset: {
    ImVec2 v[20];
    int n = 0;
    for (int i = 0; i <= 17; ++i) {
      const float a = (-60.f + 300.f * static_cast<float>(i) / 17.f) * 3.14159265f / 180.f;
      v[n++] = P(8 + 5.2f * std::cos(a), 8 + 5.2f * std::sin(a));
    }
    dl->AddPolyline(v, n, col, ImDrawFlags_None, th);
    poly({{12.5f, 2.5f}, {12.5f, 6.5f}, {8.5f, 6.5f}}, false);
    break;
  }
  case Icon::None: break;
  }
}

/// A button like ImGui::Button, but with \p icon drawn before the label (the label may be empty for an icon-only button). Returns
/// true when pressed. \p tip is shown on hover (give one for an icon-only button).
inline bool IconButton(Icon icon, const char* label, const char* tip = nullptr) {
  const ImGuiStyle& st = ImGui::GetStyle();
  const float isz = ImGui::GetFontSize() * 1.05f;
  const bool hasText = label != nullptr && label[0] != '\0';
  const ImVec2 ts = hasText ? ImGui::CalcTextSize(label, nullptr, true) : ImVec2(0.f, 0.f);
  const float gap = hasText ? 6.f : 0.f;
  const ImVec2 size(st.FramePadding.x * 2.f + isz + gap + ts.x, ImGui::GetFrameHeight());
  ImGui::PushID(label != nullptr ? label : "");
  ImGui::PushID(static_cast<int>(icon));
  const bool pressed = ImGui::Button("##icobtn", size);
  ImGui::PopID();
  ImGui::PopID();
  const ImVec2 a = ImGui::GetItemRectMin();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text); // follows the dimmed text colour of a disabled button
  DrawIcon(dl, icon, ImVec2(a.x + st.FramePadding.x, a.y + (size.y - isz) * 0.5f), isz, col);
  if (hasText)
    dl->AddText(ImVec2(a.x + st.FramePadding.x + isz + gap, a.y + (size.y - ts.y) * 0.5f), col, label);
  if ((tip != nullptr || hasText) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && tip != nullptr)
    ImGui::SetTooltip("%s", tip);
  return pressed;
}

} // namespace pdfview
