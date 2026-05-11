#pragma once

#include <imgui.h>

#include <string>

struct ImDrawList;
struct ImFont;

/// GoSurvey MTEXT rich wire uses ASCII control tags only (UTF-8 user text allowed between tags):
///   [[b]] [[/b]]  [[i]] [[/i]]  [[u]] [[/u]]  [[caps]] [[/caps]]
/// On commit, \ref MtextRichNormalize re-serializes runs; DXF export uses \ref MtextRichFlattenToPlain.

[[nodiscard]] std::string MtextRichNormalize(const std::string& wire);

[[nodiscard]] std::string MtextRichFlattenToPlain(const std::string& wire);

/// WYSIWYG layout: draws wrapped rich text at \p origin with max line width \p maxWidth.
void MtextRichDrawWrapped(ImDrawList* dl, ImFont* font, float fontPx, ImVec2 origin, float maxWidth, ImU32 baseRgb,
                          const std::string& wire);

/// Vertical size (px) of the same wrapped layout as \ref MtextRichDrawWrapped (no drawing).
[[nodiscard]] float MtextRichWrappedHeight(ImFont* font, float fontPx, float maxWidth, const std::string& wire);
