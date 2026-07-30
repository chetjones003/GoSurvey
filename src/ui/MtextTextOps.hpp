#pragma once

#include "MtextRichSpans.hpp"

#include <cstddef>
#include <string>
#include <vector>

// Text operations behind the MTEXT editor's Options menu (REQ-051). Pure — no ImGui, no fonts — so the
// part that matters can be tested: **every one of these rewrites span text only and never the bytes of a
// `[[…]]` tag**, which is what keeps the wire format well-formed through a case change, a find-and-replace,
// or an autocorrect.
namespace mtextops {

/// Apply \p fn to each byte of user text inside [\p rawA, \p rawB), skipping tag bytes entirely.
/// Returns true if any byte changed.
template <typename Fn>
inline bool TransformSpanText(std::string& wire, size_t rawA, size_t rawB, Fn fn) {
  if (rawB <= rawA || rawA >= wire.size())
    return false;
  if (rawB > wire.size())
    rawB = wire.size();
  std::vector<MtextRichSpan> spans;
  MtextRichBuildSpans(wire, &spans);
  bool changed = false;
  for (const MtextRichSpan& s : spans) {
    const size_t b = s.rawBegin > rawA ? s.rawBegin : rawA;
    const size_t e = s.rawEnd < rawB ? s.rawEnd : rawB;
    for (size_t i = b; i < e; ++i) {
      const char before = wire[i];
      fn(wire[i]);
      if (wire[i] != before)
        changed = true;
    }
  }
  return changed;
}

inline bool UpperRange(std::string& wire, size_t rawA, size_t rawB) {
  return TransformSpanText(wire, rawA, rawB, [](char& c) {
    if (c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 'A');
  });
}

inline bool LowerRange(std::string& wire, size_t rawA, size_t rawB) {
  return TransformSpanText(wire, rawA, rawB, [](char& c) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  });
}

/// The visible text of \p wire with every formatting tag dropped.
inline std::string FlattenToPlain(const std::string& wire) {
  std::vector<MtextRichSpan> spans;
  MtextRichBuildSpans(wire, &spans);
  std::string out;
  for (const MtextRichSpan& s : spans)
    out += wire.substr(s.rawBegin, s.rawEnd - s.rawBegin);
  return out;
}

/// Replace [\p rawA, \p rawB) with the same text stripped of formatting. Returns true if it changed.
inline bool RemoveFormattingRange(std::string& wire, size_t rawA, size_t rawB) {
  if (rawB <= rawA || rawB > wire.size())
    return false;
  const std::string had = wire.substr(rawA, rawB - rawA);
  const std::string plain = FlattenToPlain(had);
  if (plain == had)
    return false;
  wire.replace(rawA, rawB - rawA, plain);
  return true;
}

/// Replace every occurrence of \p find with \p repl in the **visible** text. A match never spans a tag
/// boundary, so a search can neither see nor damage markup: "ab" will not match across `a[[b]]b`.
/// Returns how many were replaced.
inline int FindReplaceAll(std::string& wire, const std::string& find, const std::string& repl,
                          bool matchCase) {
  if (find.empty())
    return 0;
  auto eq = [&](char a, char b) {
    if (matchCase)
      return a == b;
    const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
    const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
    return la == lb;
  };
  int count = 0;
  // Re-parsing after each hit is O(n²) in the number of matches, which is nothing for an MTEXT body and
  // avoids the offset bookkeeping that a single-pass version would need to get right. The guard bounds a
  // pathological case: replacing "a" with "aa" would otherwise never terminate.
  for (int guard = 0; guard < 10000; ++guard) {
    std::vector<MtextRichSpan> spans;
    MtextRichBuildSpans(wire, &spans);
    bool hit = false;
    for (const MtextRichSpan& s : spans) {
      if (s.rawEnd - s.rawBegin < find.size())
        continue;
      for (size_t i = s.rawBegin; i + find.size() <= s.rawEnd && !hit; ++i) {
        bool m = true;
        for (size_t k = 0; k < find.size() && m; ++k)
          m = eq(wire[i + k], find[k]);
        if (!m)
          continue;
        // Skip a replacement that would immediately re-match at the same spot (find inside repl).
        wire.replace(i, find.size(), repl);
        ++count;
        hit = true;
      }
      if (hit)
        break;  // offsets are stale after the edit; re-parse before looking again
    }
    if (!hit)
      break;
    if (repl.find(find) != std::string::npos)
      break;  // self-containing replacement: one pass only, or this never terminates
  }
  return count;
}

/// "Autocorrect cAPS Lock": a word typed with Caps Lock inverted reads lower-then-upper ("hELLO"); flip
/// every letter's case so it reads "Hello". \p endRaw is the byte just past the word's last letter. Only
/// letters within a single span are considered, so tags are never rewritten. Returns true if it changed.
inline bool AutocorrectCapsLockWord(std::string& wire, size_t endRaw) {
  if (endRaw == 0 || endRaw > wire.size())
    return false;
  std::vector<MtextRichSpan> spans;
  MtextRichBuildSpans(wire, &spans);
  for (const MtextRichSpan& sp : spans) {
    if (endRaw <= sp.rawBegin || endRaw > sp.rawEnd)
      continue;
    size_t b = endRaw;
    while (b > sp.rawBegin) {
      const char c = wire[b - 1];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
        break;
      --b;
    }
    if (endRaw - b < 2)
      return false;
    if (!(wire[b] >= 'a' && wire[b] <= 'z'))
      return false;  // the first letter must be lower-case for the pattern to apply
    for (size_t i = b + 1; i < endRaw; ++i)
      if (!(wire[i] >= 'A' && wire[i] <= 'Z'))
        return false;  // and every later letter upper-case
    wire[b] = static_cast<char>(wire[b] - 'a' + 'A');
    for (size_t i = b + 1; i < endRaw; ++i)
      wire[i] = static_cast<char>(wire[i] - 'A' + 'a');
    return true;
  }
  return false;
}

}  // namespace mtextops
