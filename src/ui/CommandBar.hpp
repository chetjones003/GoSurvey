#pragma once

// Pure helpers for the floating command bar (REQ-040) and the clickable command
// variants it renders (REQ-119). Kept free of ImGui so the fade/tail logic and the
// prompt→variants rule are unit-testable (CommandLineTests) without a UI harness.

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace cmdbar {

/// Opacity of the floating recent-history overlay (REQ-040). Fully opaque while the
/// log has been idle for less than \p fadeDelaySec, then ramps linearly to 0 over
/// \p fadeDurationSec, and stays 0 after. \p elapsedSec is the time since the last
/// log change. Result is clamped to [0, 1].
inline float HistoryAlpha(double elapsedSec, double fadeDelaySec, double fadeDurationSec) {
  if (elapsedSec <= fadeDelaySec)
    return 1.0f;
  if (fadeDurationSec <= 0.0)
    return 0.0f;
  const double t = (elapsedSec - fadeDelaySec) / fadeDurationSec;
  if (t >= 1.0)
    return 0.0f;
  return static_cast<float>(1.0 - t);
}

/// First line index to show when displaying the last \p maxLines of \p totalLines
/// (REQ-040: recent-history tail and F2 console). A non-positive \p maxLines shows
/// nothing (returns \p totalLines).
inline std::size_t LogTailStart(std::size_t totalLines, int maxLines) {
  if (maxLines <= 0)
    return totalLines;
  const std::size_t m = static_cast<std::size_t>(maxLines);
  return totalLines > m ? totalLines - m : 0;
}

// ---------------------------------------------------------------------------------------------
// REQ-119 — command variants
//
// A command variant is a keyword option a prompt offers (`Azimuth`, `3P`, `DElta`). It is
// declared by writing it into the prompt string, and this rule reads it back out. Two forms:
//
//   inline   "[A]zimuth"                        -> one link, labelled "[A]", submitting "a"
//   grouped  "[DElta/Percent/Total/DYnamic]"    -> four links, brackets and "/" plain text
//
// Nothing here decides what a command accepts — it only reads what the prompt already says.
// Keeping the shortcut IMPLIED by the text (rather than declared beside the handler) is what
// lets ~190 existing prompts opt in by markup alone, and the price is that a prompt can name a
// token its handler rejects; REQ-119 pays that with a per-token test, not with trust.
// ---------------------------------------------------------------------------------------------

/// One piece of a parsed prompt, in render order. Plain text and clickable variants share one
/// list so the renderer lays out in a single pass and the height calculation can measure the
/// very same layout it is about to draw — the two cannot disagree about where lines break.
struct PromptSegment {
  std::string text;      ///< exactly what is drawn for this piece
  std::string shortcut;  ///< what a click submits; empty unless isLink
  bool isLink = false;
};

/// ASCII-trim, so " No trim " and "No trim" yield the same shortcut.
inline std::string TrimAscii(const std::string& s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t'))
    ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
    --e;
  return s.substr(b, e - b);
}

/// The shortcut a variant's display text implies: its leading run of uppercase letters and
/// digits. This is not a new convention — it is how this codebase and AutoCAD already write
/// these prompts, and it already agrees with the handlers: `DElta`→`DE`, `Percent`→`P`,
/// `DYnamic`→`DY`, `Yes`→`Y`, `2P`→`2P`, and the awkward one, `No trim`→`N`.
///
/// Falls back to the whole trimmed text when there is no leading uppercase run, so a
/// lowercase option yields something submittable rather than an empty token that would
/// silently do nothing when clicked.
inline std::string VariantShortcut(const std::string& display) {
  const std::string d = TrimAscii(display);
  std::size_t n = 0;
  while (n < d.size()) {
    const unsigned char c = static_cast<unsigned char>(d[n]);
    if (c >= 128 || !(std::isupper(c) || std::isdigit(c)))
      break;
    ++n;
  }
  return n > 0 ? d.substr(0, n) : d;
}

/// Split \p hint into drawable segments (see the form table above). A bracket with no closing
/// `]`, and a bracket enclosing nothing submittable (`[]`, `[/]`), stay literal text rather
/// than becoming a link that submits an empty token.
inline std::vector<PromptSegment> ParsePromptSegments(const char* hint) {
  std::vector<PromptSegment> out;
  if (!hint || !hint[0])
    return out;

  const std::string h(hint);
  std::string plain;
  auto flushPlain = [&]() {
    if (plain.empty())
      return;
    out.push_back(PromptSegment{plain, std::string(), false});
    plain.clear();
  };
  auto addPlain = [&](std::string t) { out.push_back(PromptSegment{std::move(t), std::string(), false}); };

  std::size_t i = 0;
  while (i < h.size()) {
    if (h[i] == '[') {
      const std::size_t close = h.find(']', i);
      if (close != std::string::npos) {
        const std::string inner = h.substr(i + 1, close - i - 1);

        if (inner.find('/') != std::string::npos) {
          // Grouped. Split first, so a group with nothing submittable in it can fall back to
          // literal text without having emitted half a row of links already.
          std::vector<std::string> opts;
          for (std::size_t s = 0;;) {
            const std::size_t slash = inner.find('/', s);
            opts.push_back(inner.substr(s, slash == std::string::npos ? std::string::npos : slash - s));
            if (slash == std::string::npos)
              break;
            s = slash + 1;
          }
          bool anySubmittable = false;
          for (const std::string& o : opts) {
            if (!TrimAscii(o).empty())
              anySubmittable = true;
          }
          if (anySubmittable) {
            flushPlain();
            addPlain("[");
            for (std::size_t k = 0; k < opts.size(); ++k) {
              if (k != 0)
                addPlain("/");
              if (TrimAscii(opts[k]).empty())
                addPlain(opts[k]);  // an empty slot stays plain; the rest still link
              else
                out.push_back(PromptSegment{opts[k], VariantShortcut(opts[k]), true});
            }
            addPlain("]");
            i = close + 1;
            continue;
          }
        } else if (!TrimAscii(inner).empty()) {
          // Inline. The label keeps its brackets, which is what LINE's prompt already looks
          // like — this path must render `[A]zimuth, [2P]` exactly as it does today.
          flushPlain();
          out.push_back(PromptSegment{"[" + inner + "]", VariantShortcut(inner), true});
          i = close + 1;
          continue;
        }
        // Nothing submittable inside: fall through and treat the '[' as ordinary text.
      }
    }
    plain.push_back(h[i]);
    ++i;
  }
  flushPlain();
  return out;
}

} // namespace cmdbar
