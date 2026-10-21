#pragma once

#include "MtextRichSpans.hpp"  // MtextRichSpan + MtextRichBuildSpans (pure; ADR-023)

#include <cstdint>
#include <string>
#include <vector>

/// GoSurvey MTEXT rich wire uses ASCII control tags only (UTF-8 user text allowed between tags):
///   [[b]] [[/b]]  [[i]] [[/i]]  [[u]] [[/u]]  [[caps]] [[/caps]]
///   [[color:RRGGBB]] [[/color]]  — 6-digit uppercase hex, e.g. [[color:F97316]]
/// On commit, \ref MtextRichNormalize re-serializes runs; DXF export uses \ref MtextRichFlattenToPlain.

[[nodiscard]] std::string MtextRichNormalize(const std::string& wire);

/// Build an opening color tag from 8-bit R/G/B components: [[color:RRGGBB]].
[[nodiscard]] std::string MtextRichColorTag(uint8_t r, uint8_t g, uint8_t b);

[[nodiscard]] std::string MtextRichFlattenToPlain(const std::string& wire);
