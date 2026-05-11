#include "MtextRichFormat.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

struct StyleDepth {
  int b = 0;
  int i = 0;
  int u = 0;
  int c = 0;
};

struct RichRun {
  std::string text;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool caps = false;
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
    outRuns->push_back(std::move(r));
    acc.clear();
  };

  for (size_t i = 0; i < wire.size();) {
    if (wire[i] == '[' && i + 1 < wire.size() && wire[i + 1] == '[') {
      bool hit = false;
      if (Starts(wire, i, "[[b]]")) {
        flush();
        ++st.b;
        i += 5;
        hit = true;
      } else if (Starts(wire, i, "[[/b]]")) {
        flush();
        st.b = std::max(0, st.b - 1);
        i += 6;
        hit = true;
      } else if (Starts(wire, i, "[[i]]")) {
        flush();
        ++st.i;
        i += 5;
        hit = true;
      } else if (Starts(wire, i, "[[/i]]")) {
        flush();
        st.i = std::max(0, st.i - 1);
        i += 6;
        hit = true;
      } else if (Starts(wire, i, "[[u]]")) {
        flush();
        ++st.u;
        i += 5;
        hit = true;
      } else if (Starts(wire, i, "[[/u]]")) {
        flush();
        st.u = std::max(0, st.u - 1);
        i += 6;
        hit = true;
      } else if (Starts(wire, i, "[[caps]]")) {
        flush();
        ++st.c;
        i += 8;
        hit = true;
      } else if (Starts(wire, i, "[[/caps]]")) {
        flush();
        st.c = std::max(0, st.c - 1);
        i += 9;
        hit = true;
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
    if (r.bold)
      *out += "[[b]]";
    if (r.italic)
      *out += "[[i]]";
    if (r.underline)
      *out += "[[u]]";
    if (r.caps)
      *out += "[[caps]]";
    *out += r.text;
    if (r.caps)
      *out += "[[/caps]]";
    if (r.underline)
      *out += "[[/u]]";
    if (r.italic)
      *out += "[[/i]]";
    if (r.bold)
      *out += "[[/b]]";
  }
}

static float RichWrappedLayoutCore(ImDrawList* dl, ImFont* font, float fontPx, ImVec2 origin, float maxWidth,
                                   ImU32 baseRgb, const std::string& wire) {
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

  auto segColor = [&](const RichRun& r) -> ImU32 {
    ImVec4 fc = ImGui::ColorConvertU32ToFloat4(baseRgb);
    if (r.bold) {
      fc.x = std::min(1.f, fc.x + 0.09f);
      fc.y = std::min(1.f, fc.y + 0.09f);
      fc.z = std::min(1.f, fc.z + 0.09f);
    }
    if (r.italic) {
      fc.x = std::max(0.f, fc.x - 0.08f);
      fc.y = std::max(0.f, fc.y - 0.05f);
      fc.z = std::min(1.f, fc.z + 0.12f);
    }
    return ImGui::ColorConvertFloat4ToU32(fc);
  };

  float maxInkY = origin.y + lineH * 0.2f;

  for (const RichRun& r : runs) {
    const char* s = r.text.c_str();
    const char* end = s + r.text.size();
    std::string disp = r.text;
    if (r.caps)
      ApplyCapsAscii(&disp);
    s = disp.c_str();
    end = s + disp.size();
    const ImU32 col = segColor(r);
    while (s < end) {
      if (*s == '\n') {
        pen.x = x0;
        pen.y += lineH;
        ++s;
        continue;
      }
      const char* wend = s;
      while (wend < end && *wend != ' ' && *wend != '\n')
        ++wend;
      if (wend > s) {
        ImVec2 sz = font->CalcTextSizeA(fontPx, FLT_MAX, 0.f, s, wend);
        if (pen.x + sz.x > xMax && pen.x > x0 + 0.5f) {
          pen.x = x0;
          pen.y += lineH;
        }
        if (dl) {
          if (r.bold) {
            dl->AddText(font, fontPx, pen + ImVec2(0.55f, 0.f), col, s, wend);
            dl->AddText(font, fontPx, pen + ImVec2(-0.55f, 0.f), col, s, wend);
          }
          if (r.italic)
            dl->AddText(font, fontPx, pen + ImVec2(0.4f, 0.f), col, s, wend);
          dl->AddText(font, fontPx, pen, col, s, wend);
          if (r.underline) {
            const float uy = pen.y + sz.y + 0.5f;
            dl->AddLine(ImVec2(pen.x, uy), ImVec2(pen.x + sz.x, uy), col, 1.f);
          }
        }
        pen.x += sz.x;
        maxInkY = std::max(maxInkY, pen.y + sz.y);
        s = wend;
      }
      if (s < end && *s == ' ') {
        ImVec2 sp = font->CalcTextSizeA(fontPx, FLT_MAX, 0.f, s, s + 1);
        if (pen.x + sp.x > xMax && pen.x > x0 + 0.5f) {
          pen.x = x0;
          pen.y += lineH;
        }
        if (dl)
          dl->AddText(font, fontPx, pen, col, s, s + 1);
        pen.x += sp.x;
        maxInkY = std::max(maxInkY, pen.y + sp.y);
        ++s;
      }
    }
  }
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

void MtextRichDrawWrapped(ImDrawList* dl, ImFont* font, float fontPx, ImVec2 origin, float maxWidth, ImU32 baseRgb,
                          const std::string& wire) {
  if (!dl)
    return;
  RichWrappedLayoutCore(dl, font, fontPx, origin, maxWidth, baseRgb, wire);
}

float MtextRichWrappedHeight(ImFont* font, float fontPx, float maxWidth, const std::string& wire) {
  return RichWrappedLayoutCore(nullptr, font, fontPx, ImVec2(0.f, 0.f), maxWidth, IM_COL32_WHITE, wire);
}
