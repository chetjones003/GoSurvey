#pragma once

#include "ShxFont.hpp"

#include <imgui.h>

#include <string>

// UI draw adapter for SHX stroke fonts (ADR-022): the pure glyph geometry lives in the lower-layer
// font/ShxFont module; this adapter draws those strokes into an ImGui draw list. Kept in the UI layer
// because it depends on imgui — IO/plot builds its own strokes from the same font module instead.
namespace Shx {

/// Draw \p text with \p font as strokes. \p baseline is the screen-space baseline-left point; the glyph
/// cap height maps to \p capPx; \p rotRad rotates CCW about \p baseline (screen y grows downward).
void DrawText(ImDrawList* dl, Font& font, ImVec2 baseline, float capPx, float rotRad, ImU32 col,
              const std::string& text, float thicknessPx);

}  // namespace Shx
