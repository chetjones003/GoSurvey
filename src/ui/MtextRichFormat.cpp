#include "MtextRichFormat.hpp"
#include "FontRegistry.hpp"
#include "ShxDraw.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// True when a font name refers to an SHX stroke font. Deliberately a local copy of the same predicate
// in CadUi.cpp (and PdfPlot.cpp): hoisting it into the Shx module would change that module's public
// API, which is an architectural decision the Workshop may not take on its own. See the task log.
bool IsShxFontName(const std::string& s) {
  if (s.size() < 4)
    return false;
  std::string ext = s.substr(s.size() - 4);
  for (char& c : ext)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".shx";
}

struct RichRun {
  std::string text;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool caps = false;
  bool hasColorOverride = false;
  uint32_t colorOverride = 0; // 0xRRGGBB
  std::string font;           // per-run typeface override; empty = base font
};


inline void ApplyCapsAscii(std::string* t) { MtextRichApplyCapsAscii(t); }

void BuildRuns(const std::string& wire, std::vector<RichRun>* outRuns) {
  // One parser (ADR-023): runs are the spans plus their text, so the editor and the draw/measure paths
  // can never disagree about where a tag begins.
  std::vector<MtextRichSpan> spans;
  MtextRichBuildSpans(wire, &spans);
  outRuns->clear();
  outRuns->reserve(spans.size());
  for (const MtextRichSpan& s : spans) {
    RichRun r;
    r.text = wire.substr(s.rawBegin, s.rawEnd - s.rawBegin);
    r.bold = s.bold;
    r.italic = s.italic;
    r.underline = s.underline;
    r.caps = s.caps;
    r.hasColorOverride = s.hasColor;
    r.colorOverride = s.color;
    r.font = s.font;
    outRuns->push_back(std::move(r));
  }
}


void SerializeRuns(const std::vector<RichRun>& runs, std::string* out) {
  out->clear();
  for (const RichRun& r : runs) {
    if (!r.font.empty())
      *out += "[[font:" + r.font + "]]";
    if (r.hasColorOverride) {
      char buf[20];
      std::snprintf(buf, sizeof(buf), "[[color:%06X]]", r.colorOverride);
      *out += buf;
    }
    if (r.bold)    *out += "[[b]]";
    if (r.italic)  *out += "[[i]]";
    if (r.underline) *out += "[[u]]";
    if (r.caps)    *out += "[[caps]]";
    *out += r.text;
    if (r.caps)    *out += "[[/caps]]";
    if (r.underline) *out += "[[/u]]";
    if (r.italic)  *out += "[[/i]]";
    if (r.bold)    *out += "[[/b]]";
    if (r.hasColorOverride)
      *out += "[[/color]]";
    if (!r.font.empty())
      *out += "[[/font]]";
  }
}

static float RichWrappedLayoutCore(ImDrawList* dl, ImFont* font, float fontPx, ImVec2 origin, float maxWidth,
                                   ImU32 baseRgb, const std::string& wire, const std::string& baseFontFamily,
                                   float* outMaxContentWidthPx) {
  if (outMaxContentWidthPx)
    *outMaxContentWidthPx = 0.f;
  if (!font || maxWidth < 4.f)
    return fontPx * 1.22f;
  std::vector<RichRun> runs;
  BuildRuns(wire, &runs);
  if (runs.empty())
    return fontPx * 1.22f;

  const float lineH = fontPx * 1.22f;
  // The pen walks in LOCAL coordinates (0,0 is the origin); `origin` is added back only where a segment is
  // actually drawn. It used to walk in screen coordinates, which meant the wrap test `pen.x + w > xMax`
  // summed the run widths starting from a large base while the caller's measured natural width summed the
  // same widths starting from zero — the two answers differed by a few ULPs. A survey-point label wraps at
  // exactly its own natural width, so that tiny difference was the entire margin: the last word dropped to
  // a second line at some zoom levels and not others. Local coordinates make the measure pass and the draw
  // pass perform bit-identical arithmetic, so a line that measured as fitting always fits.
  ImVec2 pen(0.f, 0.f);
  const float x0 = 0.f;
  const float xMax = std::max(8.f, maxWidth);
  float lineStartX = pen.x;

  auto segColor = [&](const RichRun& r) -> ImU32 {
    if (!r.hasColorOverride)
      return baseRgb;
    const uint32_t rgb = r.colorOverride;
    ImVec4 fc;
    fc.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.f;
    fc.y = static_cast<float>((rgb >>  8) & 0xFF) / 255.f;
    fc.z = static_cast<float>( rgb        & 0xFF) / 255.f;
    fc.w = 1.f;
    return ImGui::ColorConvertFloat4ToU32(fc);
  };

  float maxInkY = lineH * 0.2f;
  const float uThick = std::max(1.f, fontPx * 0.06f);
  std::string shxBuf;  // reused across runs — the Shx entry points take std::string, not a char range

  for (const RichRun& r : runs) {
    std::string disp = r.text;
    if (r.caps)
      ApplyCapsAscii(&disp);
    const char* s = disp.c_str();
    const char* end = s + disp.size();
    const ImU32 col = segColor(r);
    // Resolve the run typeface (per-run [[font:…]] → base family → fallback) and bold/italic; missing
    // bold/italic variants fall back to faux double-strike / nudge.
    const std::string& fam = !r.font.empty() ? r.font : baseFontFamily;
    bool realBold = false, realItalic = false;
    // An SHX run draws from the real .shx as strokes, exactly as whole-object SHX text already does.
    // Without this branch a [[font:romans.shx]] run fell through to FontReg, which substitutes a
    // TrueType — so picking an SHX font for a *selection* silently produced Arial/Times/Consolas.
    Shx::Font* sf = IsShxFontName(fam) ? Shx::Resolve(fam) : nullptr;
    if (sf && !sf->valid())
      sf = nullptr;
    ImFont* rf = fam.empty() ? font : FontReg::Resolve(fam, r.bold, r.italic, &realBold, &realItalic);
    if (!rf)
      rf = font;
    const bool fauxBold = r.bold && !realBold;
    const bool fauxItalic = r.italic && !realItalic;
    // Measure through the same font the run will be drawn with, so wrapping, the reported content
    // width and the box height all agree with the glyphs actually on screen.
    auto measure = [&](const char* a, const char* b) -> ImVec2 {
      if (sf) {
        shxBuf.assign(a, b);
        return ImVec2(Shx::MeasureWidthPx(*sf, shxBuf, fontPx), fontPx);
      }
      return rf->CalcTextSizeA(fontPx, FLT_MAX, 0.f, a, b);
    };
    auto drawSeg = [&](const char* a, const char* b, ImVec2 at) {
      if (!dl)
        return;
      if (sf) {
        // Shx::DrawText is baseline-anchored; the layout pen is a top-left one cap-height above it,
        // the same relationship the whole-object SHX path uses.
        shxBuf.assign(a, b);
        Shx::DrawText(dl, *sf, ImVec2(at.x, at.y + fontPx), fontPx, 0.f, col, shxBuf,
                      std::max(1.f, fontPx * 0.05f));
        return;
      }
      if (fauxBold) {
        dl->AddText(rf, fontPx, ImVec2(at.x + 0.55f, at.y), col, a, b);
        dl->AddText(rf, fontPx, ImVec2(at.x - 0.55f, at.y), col, a, b);
      }
      if (fauxItalic)
        dl->AddText(rf, fontPx, ImVec2(at.x + 0.4f, at.y), col, a, b);
      dl->AddText(rf, fontPx, at, col, a, b);
    };
    while (s < end) {
      if (*s == '\n') {
        if (outMaxContentWidthPx)
          *outMaxContentWidthPx = std::max(*outMaxContentWidthPx, pen.x - lineStartX);
        pen.x = x0;
        lineStartX = pen.x;
        pen.y += lineH;
        ++s;
        continue;
      }
      const char* wend = s;
      while (wend < end && *wend != ' ' && *wend != '\n')
        ++wend;
      if (wend > s) {
        const ImVec2 sz = measure(s, wend);
        if (pen.x + sz.x > xMax && pen.x > x0 + 0.5f) {
          pen.x = x0;
          pen.y += lineH;
        }
        const ImVec2 at(origin.x + pen.x, origin.y + pen.y);
        drawSeg(s, wend, at);
        if (dl && r.underline)
          dl->AddLine(ImVec2(at.x, at.y + sz.y + 0.5f), ImVec2(at.x + sz.x, at.y + sz.y + 0.5f), col, uThick);
        pen.x += sz.x;
        maxInkY = std::max(maxInkY, pen.y + sz.y);
        s = wend;
      }
      if (s < end && *s == ' ') {
        const ImVec2 sp = measure(s, s + 1);
        if (pen.x + sp.x > xMax && pen.x > x0 + 0.5f) {
          pen.x = x0;
          pen.y += lineH;
        }
        const ImVec2 at(origin.x + pen.x, origin.y + pen.y);
        if (dl && r.underline)
          dl->AddLine(ImVec2(at.x, at.y + sp.y + 0.5f), ImVec2(at.x + sp.x, at.y + sp.y + 0.5f), col, uThick);
        drawSeg(s, s + 1, at);
        pen.x += sp.x;
        maxInkY = std::max(maxInkY, pen.y + sp.y);
        ++s;
      }
    }
  }
  if (outMaxContentWidthPx)
    *outMaxContentWidthPx = std::max(*outMaxContentWidthPx, pen.x - lineStartX);
  return std::max(maxInkY, lineH);
}

} // namespace

std::string MtextRichNormalize(const std::string& wire) {
  std::vector<RichRun> runs;
  BuildRuns(wire, &runs);
  std::string o;
  SerializeRuns(runs, &o);
  return o;
}

std::string MtextRichFlattenToPlain(const std::string& wire) {
  std::vector<RichRun> runs;
  BuildRuns(wire, &runs);
  std::string o;
  for (const RichRun& r : runs) {
    std::string t = r.text;
    if (r.caps)
      ApplyCapsAscii(&t);
    o += t;
  }
  return o;
}

std::string MtextRichColorTag(uint8_t r, uint8_t g, uint8_t b) {
  char buf[20];
  std::snprintf(buf, sizeof(buf), "[[color:%02X%02X%02X]]",
                static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>(b));
  return buf;
}

void MtextRichDrawWrapped(ImDrawList* dl, ImFont* font, float fontPx, ImVec2 origin, float maxWidth, ImU32 baseRgb,
                          const std::string& wire, const std::string& baseFontFamily) {
  if (!dl)
    return;
  RichWrappedLayoutCore(dl, font, fontPx, origin, maxWidth, baseRgb, wire, baseFontFamily, nullptr);
}

float MtextRichWrappedHeight(ImFont* font, float fontPx, float maxWidth, const std::string& wire,
                             const std::string& baseFontFamily) {
  return RichWrappedLayoutCore(nullptr, font, fontPx, ImVec2(0.f, 0.f), maxWidth, IM_COL32_WHITE, wire,
                               baseFontFamily, nullptr);
}

void MtextRichNaturalContentPx(ImFont* font, float fontPx, const std::string& wire, float* outW, float* outH,
                               const std::string& baseFontFamily) {
  if (!outW || !outH)
    return;
  if (!font || fontPx < 1.f) {
    *outW = 8.f;
    *outH = std::max(fontPx * 1.22f, 4.f);
    return;
  }
  float maxW = 0.f;
  *outH = RichWrappedLayoutCore(nullptr, font, fontPx, ImVec2(0.f, 0.f), 1.e9f, IM_COL32_WHITE, wire,
                                baseFontFamily, &maxW);
  *outW = std::max(maxW, 8.f);
}
