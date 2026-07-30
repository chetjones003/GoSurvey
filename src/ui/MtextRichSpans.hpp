#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// The MTEXT rich-wire parser (ADR-023), split out of MtextRichFormat so it is **pure** — no ImGui, no
// fonts — and can be unit-tested and shared. Everything that needs a draw list or font metrics stays in
// MtextRichFormat.cpp / RichTextEdit.cpp.
//
// Wire format (ASCII control tags only; UTF-8 user text between them):
//   [[b]] [[/b]]  [[i]] [[/i]]  [[u]] [[/u]]  [[caps]] [[/caps]]
//   [[color:RRGGBB]] [[/color]]   — 6 hex digits
//   [[font:Family]] [[/font]]

/// One run of user text, with **where it came from**. \c rawBegin/rawEnd are byte offsets into the wire
/// bounding this span's text; the tags that produced its styling lie outside that range. The WYSIWYG
/// editor needs this to map a caret between two visible characters back to a byte position it can insert
/// at. The run-based draw/measure paths are built on the same parse, so there is exactly one parser.
struct MtextRichSpan {
  size_t rawBegin = 0;
  size_t rawEnd = 0;  ///< one past the last byte
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool caps = false;   ///< ASCII is upper-cased at draw time; the wire keeps the user's own casing
  bool hasColor = false;
  uint32_t color = 0;  ///< 0xRRGGBB, valid only when \c hasColor
  std::string font;    ///< per-run typeface override; empty = the base font
};

namespace mtextspans_detail {

inline bool Starts(const std::string& s, size_t i, const char* tag) {
  const size_t n = std::strlen(tag);
  return i + n <= s.size() && std::memcmp(s.data() + i, tag, n) == 0;
}

/// Try to parse `[[color:RRGGBB]]` at \p i. On success sets \p outEnd past the tag and \p outRgb.
inline bool TryParseColorOpen(const std::string& s, size_t i, size_t* outEnd, uint32_t* outRgb) {
  if (i + 16 > s.size() || !Starts(s, i, "[[color:"))
    return false;
  const size_t hexStart = i + 8;
  if (hexStart + 8 > s.size() || s[hexStart + 6] != ']' || s[hexStart + 7] != ']')
    return false;
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

/// Nesting depth of each toggle, plus the colour/font stacks (innermost wins).
struct StyleDepth {
  int b = 0, i = 0, u = 0, c = 0;
  std::vector<uint32_t> colorStack;
  std::vector<std::string> fontStack;
};

}  // namespace mtextspans_detail

/// Upper-case ASCII in place (what a `[[caps]]` run displays as). Non-ASCII bytes are left alone.
inline void MtextRichApplyCapsAscii(std::string* t) {
  for (char& ch : *t)
    if (ch >= 'a' && ch <= 'z')
      ch = static_cast<char>(ch - 'a' + 'A');
}

/// Parse \p wire into text spans carrying their byte ranges and resolved styling. Unrecognised or
/// unterminated `[[…]]` sequences are treated as literal text (never dropped), so the spans' ranges
/// cover every byte of user text with no gaps and no overlap.
inline void MtextRichBuildSpans(const std::string& wire, std::vector<MtextRichSpan>* out) {
  using namespace mtextspans_detail;
  out->clear();
  StyleDepth st;
  size_t accBegin = 0;  // byte offset where the span being accumulated started
  size_t accLen = 0;    // its length so far (bytes are contiguous between flushes)
  auto flush = [&]() {
    if (accLen == 0)
      return;
    MtextRichSpan s;
    s.rawBegin = accBegin;
    s.rawEnd = accBegin + accLen;
    s.bold = st.b > 0;
    s.italic = st.i > 0;
    s.underline = st.u > 0;
    s.caps = st.c > 0;
    if (!st.colorStack.empty()) {
      s.hasColor = true;
      s.color = st.colorStack.back();
    }
    if (!st.fontStack.empty())
      s.font = st.fontStack.back();
    out->push_back(std::move(s));
    accLen = 0;
  };
  auto take = [&](size_t at) {
    if (accLen == 0)
      accBegin = at;
    ++accLen;
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
        take(i);  // an unrecognised "[[" is literal text, never dropped
        ++i;
      }
    } else {
      take(i);
      ++i;
    }
  }
  flush();
}
