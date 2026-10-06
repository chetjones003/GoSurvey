#pragma once

/// Right-hand vertical tab strip, shared by the palette windows that have one: the BEDIT Block
/// Authoring Palettes (its original home) and the Pipe Fittings palette (REQ-350 (b)).
///
/// Promoted out of `CadUi_BlockAuthoring.cpp`'s file-static helpers when the second palette needed
/// exactly the same strip. Two present-day call sites, which is what architecture invariant §11.4
/// asks for before anything is shared at all — and the point of sharing is that the two windows read
/// as one family rather than two near-identical tab strips drifting apart.

#include "imgui.h"

#include <algorithm>

/// Draws \p s rotated 90 degrees CCW (reads bottom-to-top), centred in a column of width \p colW and
/// height \p colH whose top-left is \p mn. Each glyph quad from the font atlas is rotated in place.
inline void CadUiVerticalText(const ImVec2& mn, float colW, float colH, const char* s) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFontBaked* baked = ImGui::GetFontBaked();
  const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
  // Measure the glyph band (perpendicular to the reading direction) so it can be centred in the column.
  float bandLo = 0.f, bandHi = 0.f, textLen = 0.f;
  for (const char* c = s; *c; ++c) {
    const ImFontGlyph* g = baked->FindGlyph(static_cast<ImWchar>(*c));
    if (!g)
      continue;
    bandLo = std::min(bandLo, g->Y0);
    bandHi = std::max(bandHi, g->Y1);
    textLen += g->AdvanceX;
  }
  ImVec2 p(mn.x + colW * 0.5f - (bandLo + bandHi) * 0.5f - 3.f, mn.y + (colH + textLen) * 0.5f);
  dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
  for (const char* c = s; *c; ++c) {
    const ImFontGlyph* g = baked->FindGlyph(static_cast<ImWchar>(*c));
    if (!g)
      continue;
    if (g->Visible) {
      dl->PrimReserve(6, 4);
      const ImVec2 a(p.x + g->Y0, p.y - g->X0);
      const ImVec2 b(p.x + g->Y0, p.y - g->X1);
      const ImVec2 cc(p.x + g->Y1, p.y - g->X1);
      const ImVec2 d(p.x + g->Y1, p.y - g->X0);
      dl->PrimQuadUV(a, b, cc, d, ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V0), ImVec2(g->U1, g->V1),
                     ImVec2(g->U0, g->V1), col);
    }
    p.y -= g->AdvanceX;
  }
  dl->PopTexture();
}

/// One tab in the strip: a tall narrow button carrying \p label sideways, highlighted when
/// `*tab == idx`, and selecting \p idx when clicked.
inline void CadUiPaletteTabButton(const char* label, int idx, int* tab) {
  const bool on = *tab == idx;
  const ImVec2 size(30.f, ImGui::CalcTextSize(label).x + 24.f);
  ImGui::PushID(idx);
  if (on)
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 78, 88, 255));
  if (ImGui::Button("##tab", size))
    *tab = idx;
  if (on)
    ImGui::PopStyleColor();
  const ImVec2 mn = ImGui::GetItemRectMin();
  CadUiVerticalText(mn, size.x, size.y, label);
  ImGui::PopID();
}
