#include "MtextRichFormat.hpp"
#include "FontRegistry.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct StyleDepth {
  int b = 0;
  int i = 0;
  int u = 0;
  int c = 0;
  // Color stack: each entry is a packed 0xRRGGBB value (no alpha).
  std::vector<uint32_t> colorStack;
  // Font-family stack: each entry a TrueType family / SHX name; empty stack = base font.
  std::vector<std::string> fontStack;
};

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

bool Starts(const std::string& s, size_t i, const char* tag) {
  const size_t n = std::strlen(tag);
  return i + n <= s.size() && std::memcmp(s.data() + i, tag, n) == 0;
}

void ApplyCapsAscii(std::string* t) {
  for (char& ch : *t) {
    if (ch >= 'a' && ch <= 'z')
      ch = static_cast<char>(ch - 'a' + 'A');
  }
}

// Try to parse [[color:RRGGBB]] at position i. Returns true and advances i past the tag.
bool TryParseColorOpen(const std::string& s, size_t i, size_t* outEnd, uint32_t* outRgb) {
  // Minimum: [[color:RRGGBB]] = 16 chars
  if (i + 16 > s.size())
    return false;
  if (!Starts(s, i, "[[color:"))
    return false;
  // Expect 6 hex digits then "]]"
  const size_t hexStart = i + 8;
  if (hexStart + 8 > s.size())
    return false;
  if (s[hexStart + 6] != ']' || s[hexStart + 7] != ']')
    return false;
  // Parse the 6 hex digits
  uint32_t rgb = 0;
  for (int k = 0; k < 6; ++k) {
    const char c = s[hexStart + static_cast<size_t>(k)];
    uint32_t nibble;
    if (c >= '0' && c <= '9')      nibble = static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') nibble = static_cast<uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') nibble = static_cast<uint32_t>(c - 'A' + 10);
    else return false;
    rgb = (rgb << 4) | nibble;
  }
  *outEnd = hexStart + 8;
  *outRgb = rgb;
  return true;
}

void BuildRuns(const std::string& wire, std::vector<RichRun>* outRuns) {
  outRuns->clear();
  StyleDepth st;
  std::string acc;
  auto flush = [&]() {
    if (acc.empty())
      return;
    RichRun r;
    r.text = acc;
    r.bold = st.b > 0;
    r.italic = st.i > 0;
    r.underline = st.u > 0;
    r.caps = st.c > 0;
    if (!st.colorStack.empty()) {
      r.hasColorOverride = true;
      r.colorOverride = st.colorStack.back();
    }
    if (!st.fontStack.empty())
      r.font = st.fontStack.back();
    outRuns->push_back(std::move(r));
    acc.clear();
  };

  for (size_t i = 0; i < wire.size();) {
    if (wire[i] == '[' && i + 1 < wire.size() && wire[i + 1] == '[') {
      bool hit = false;
      if (Starts(wire, i, "[[b]]"))          { flush(); ++st.b; i += 5; hit = true; }
      else if (Starts(wire, i, "[[/b]]"))    { flush(); st.b = std::max(0, st.b - 1); i += 6; hit = true; }
      else if (Starts(wire, i, "[[i]]"))     { flush(); ++st.i; i += 5; hit = true; }
      else if (Starts(wire, i, "[[/i]]"))    { flush(); st.i = std::max(0, st.i - 1); i += 6; hit = true; }
      else if (Starts(wire, i, "[[u]]"))     { flush(); ++st.u; i += 5; hit = true; }
      else if (Starts(wire, i, "[[/u]]"))    { flush(); st.u = std::max(0, st.u - 1); i += 6; hit = true; }
      else if (Starts(wire, i, "[[caps]]"))  { flush(); ++st.c; i += 8; hit = true; }
      else if (Starts(wire, i, "[[/caps]]")) { flush(); st.c = std::max(0, st.c - 1); i += 9; hit = true; }
      else if (Starts(wire, i, "[[/color]]")) {
        flush();
        if (!st.colorStack.empty())
          st.colorStack.pop_back();
        i += 10;
        hit = true;
      } else if (Starts(wire, i, "[[/font]]")) {
        flush();
        if (!st.fontStack.empty())
          st.fontStack.pop_back();
        i += 9;
        hit = true;
      } else if (Starts(wire, i, "[[font:")) {
        const size_t nameStart = i + 7;
        const size_t close = wire.find("]]", nameStart);
        if (close != std::string::npos) {
          flush();
          st.fontStack.push_back(wire.substr(nameStart, close - nameStart));
          i = close + 2;
          hit = true;
        }
      } else {
        size_t afterTag = 0;
        uint32_t rgb = 0;
        if (TryParseColorOpen(wire, i, &afterTag, &rgb)) {
          flush();
          st.colorStack.push_back(rgb);
          i = afterTag;
          hit = true;
        }
      }
      if (!hit) {
        acc += wire[i];
        ++i;
      }
    } else {
      acc += wire[i];
      ++i;
    }
  }
  flush();
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
  ImVec2 pen = origin;
  const float x0 = origin.x;
  const float xMax = origin.x + std::max(8.f, maxWidth);
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

  float maxInkY = origin.y + lineH * 0.2f;
  const float uThick = std::max(1.f, fontPx * 0.06f);

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
    ImFont* rf = fam.empty() ? font : FontReg::Resolve(fam, r.bold, r.italic, &realBold, &realItalic);
    if (!rf)
      rf = font;
    const bool fauxBold = r.bold && !realBold;
    const bool fauxItalic = r.italic && !realItalic;
    auto drawSeg = [&](const char* a, const char* b, ImVec2 at) {
      if (!dl)
        return;
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
        ImVec2 sz = rf->CalcTextSizeA(fontPx, FLT_MAX, 0.f, s, wend);
        if (pen.x + sz.x > xMax && pen.x > x0 + 0.5f) {
          pen.x = x0;
          pen.y += lineH;
        }
        drawSeg(s, wend, pen);
        if (dl && r.underline)
          dl->AddLine(ImVec2(pen.x, pen.y + sz.y + 0.5f), ImVec2(pen.x + sz.x, pen.y + sz.y + 0.5f), col, uThick);
        pen.x += sz.x;
        maxInkY = std::max(maxInkY, pen.y + sz.y);
        s = wend;
      }
      if (s < end && *s == ' ') {
        ImVec2 sp = rf->CalcTextSizeA(fontPx, FLT_MAX, 0.f, s, s + 1);
        if (pen.x + sp.x > xMax && pen.x > x0 + 0.5f) {
          pen.x = x0;
          pen.y += lineH;
        }
        if (dl && r.underline)
          dl->AddLine(ImVec2(pen.x, pen.y + sp.y + 0.5f), ImVec2(pen.x + sp.x, pen.y + sp.y + 0.5f), col, uThick);
        drawSeg(s, s + 1, pen);
        pen.x += sp.x;
        maxInkY = std::max(maxInkY, pen.y + sp.y);
        ++s;
      }
    }
  }
  if (outMaxContentWidthPx)
    *outMaxContentWidthPx = std::max(*outMaxContentWidthPx, pen.x - lineStartX);
  return std::max(maxInkY - origin.y, lineH);
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
