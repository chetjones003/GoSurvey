#include "MtextRichFormat.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

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
