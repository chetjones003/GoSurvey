#pragma once

#include "MtextRichSpans.hpp"

#include <cstddef>
#include <string>
#include <vector>

// Pure layout + caret math for the WYSIWYG MTEXT editor (ADR-023). No ImGui, no fonts: the caller
// measures each character and fills in \c Cell::w, then wraps. That split is what makes the tricky parts —
// word wrapping, the visible-index ↔ raw-byte-offset mapping, and caret hit-testing — unit-testable, with
// the font-dependent drawing left in RichTextEdit.cpp.
namespace richtext {

/// One visible character. \c rawBegin/rawEnd bound it in the wire string, so a caret expressed as an
/// index into the cell vector converts back to a byte position the buffer can be edited at. Tag bytes are
/// never cells — that is what makes the editor WYSIWYG.
struct Cell {
  size_t rawBegin = 0;
  size_t rawEnd = 0;
  int spanIndex = -1;  ///< index into the span vector this character came from (its styling)
  int line = 0;        ///< wrapped line, filled by \ref WrapCells
  float x = 0.f;       ///< pen offset within its line, filled by \ref WrapCells
  float w = 0.f;       ///< advance width; the caller measures this before wrapping
  bool isNewline = false;
  bool isSpace = false;
};

/// Byte length of the UTF-8 character starting at \p i. A malformed lead byte counts as 1, so a bad
/// encoding costs one replacement character rather than desynchronising every offset after it.
inline size_t Utf8CharLen(const std::string& s, size_t i) {
  if (i >= s.size())
    return 0;
  const unsigned char c = static_cast<unsigned char>(s[i]);
  size_t n = 1;
  if ((c & 0x80u) == 0x00u)      n = 1;
  else if ((c & 0xE0u) == 0xC0u) n = 2;
  else if ((c & 0xF0u) == 0xE0u) n = 3;
  else if ((c & 0xF8u) == 0xF0u) n = 4;
  if (i + n > s.size())
    n = 1;  // truncated sequence at end of buffer
  return n;
}

/// Enumerate every visible character of \p spans into cells, in wire order. Widths are left at 0 for the
/// caller to measure; \ref WrapCells then assigns lines and x offsets.
inline void BuildCells(const std::string& wire, const std::vector<MtextRichSpan>& spans,
                       std::vector<Cell>* out) {
  out->clear();
  for (size_t si = 0; si < spans.size(); ++si) {
    const MtextRichSpan& sp = spans[si];
    size_t i = sp.rawBegin;
    while (i < sp.rawEnd && i < wire.size()) {
      const size_t n = Utf8CharLen(wire, i);
      if (n == 0)
        break;
      Cell c;
      c.rawBegin = i;
      c.rawEnd = i + n > sp.rawEnd ? sp.rawEnd : i + n;
      c.spanIndex = static_cast<int>(si);
      c.isNewline = (wire[i] == '\n');
      c.isSpace = (wire[i] == ' ' || wire[i] == '\t');
      out->push_back(c);
      i += n;
    }
  }
}

/// Assign \c line and \c x to every cell, breaking lines at \p maxWidth on word boundaries. A word longer
/// than the column is broken mid-word rather than overflowing forever. A non-positive \p maxWidth wraps
/// only at explicit newlines. Returns the line count, always at least 1.
inline int WrapCells(std::vector<Cell>* cells, float maxWidth) {
  const bool doWrap = maxWidth > 0.f;
  size_t k = 0;
  int line = 0;
  while (k < cells->size()) {
    const size_t lineStart = k;
    size_t end = k;
    size_t lastBreak = static_cast<size_t>(-1);  // cell index a following line could start at
    float x = 0.f;
    while (end < cells->size()) {
      const Cell& c = (*cells)[end];
      if (c.isNewline) {
        ++end;  // the newline belongs to the line it terminates
        break;
      }
      if (doWrap && x + c.w > maxWidth && end > lineStart) {
        if (lastBreak != static_cast<size_t>(-1) && lastBreak > lineStart)
          end = lastBreak;  // rewind to just after the last space
        break;
      }
      x += c.w;
      ++end;
      if (c.isSpace)
        lastBreak = end;
    }
    float pen = 0.f;
    for (size_t j = lineStart; j < end; ++j) {
      (*cells)[j].line = line;
      (*cells)[j].x = pen;
      pen += (*cells)[j].w;
    }
    ++line;
    k = end;
  }
  // A buffer ending in a newline has an empty final line the caret can sit on.
  if (!cells->empty() && cells->back().isNewline)
    ++line;
  return line < 1 ? 1 : line;
}

/// Byte offset to insert typed text at, for a caret at visible index \p caret. Insertion lands *after*
/// the preceding character's bytes, i.e. inside its run, so typed text inherits the styling to its left
/// (ADR-023 (d)) rather than escaping in front of an opening tag.
inline size_t InsertOffset(const std::vector<Cell>& cells, size_t wireSize, int caret) {
  if (cells.empty())
    return wireSize;
  if (caret <= 0)
    return cells.front().rawBegin;
  const size_t idx = static_cast<size_t>(caret);
  if (idx >= cells.size())
    return cells.back().rawEnd;
  return cells[idx - 1].rawEnd;
}

/// Tightest raw byte range covering the visible characters [\p a, \p b). Tight (rather than the insertion
/// offsets) so that wrapping a selection in tags cannot swallow a neighbouring run's closing tag.
inline void SelectionRawRange(const std::vector<Cell>& cells, int a, int b, size_t* rawA, size_t* rawB) {
  if (a > b) {
    const int t = a;
    a = b;
    b = t;
  }
  if (cells.empty() || a == b) {
    const size_t at = cells.empty() ? 0 : InsertOffset(cells, 0, a);
    *rawA = at;
    *rawB = at;
    return;
  }
  const int n = static_cast<int>(cells.size());
  if (a < 0) a = 0;
  if (b > n) b = n;
  *rawA = (a < n) ? cells[static_cast<size_t>(a)].rawBegin : cells.back().rawEnd;
  *rawB = (b > 0) ? cells[static_cast<size_t>(b - 1)].rawEnd : *rawA;
}

/// Number of cells on \p line (used to place a caret on an empty line).
inline int CaretAtLineStart(const std::vector<Cell>& cells, int line) {
  for (size_t j = 0; j < cells.size(); ++j)
    if (cells[j].line >= line)
      return static_cast<int>(j);
  return static_cast<int>(cells.size());
}

/// Caret index nearest the box-local point (\p px, \p py). Clicking past the end of a line lands before
/// its newline, not after it, so the caret stays on the line the user clicked.
inline int CaretFromPoint(const std::vector<Cell>& cells, int lineCount, float lineH, float px, float py) {
  if (cells.empty() || lineH <= 0.f)
    return 0;
  int line = static_cast<int>(py / lineH);
  if (line < 0) line = 0;
  if (line > lineCount - 1) line = lineCount - 1;

  int firstOnLine = -1, lastOnLine = -1;
  for (size_t j = 0; j < cells.size(); ++j) {
    if (cells[j].line != line)
      continue;
    if (firstOnLine < 0)
      firstOnLine = static_cast<int>(j);
    lastOnLine = static_cast<int>(j);
  }
  if (firstOnLine < 0)
    return CaretAtLineStart(cells, line);  // empty line (a trailing or doubled newline)

  for (int j = firstOnLine; j <= lastOnLine; ++j) {
    const Cell& c = cells[static_cast<size_t>(j)];
    if (c.isNewline)
      return j;  // past the last visible glyph: sit before the break
    if (px <= c.x + c.w * 0.5f)
      return j;
  }
  return lastOnLine + 1;
}

/// Word bounds around visible index \p at, for double-click selection. A run of spaces selects as a word
/// of its own, matching what a text field does.
inline void WordBounds(const std::vector<Cell>& cells, int at, int* outA, int* outB) {
  const int n = static_cast<int>(cells.size());
  if (n == 0) {
    *outA = *outB = 0;
    return;
  }
  int i = at;
  if (i >= n) i = n - 1;
  if (i < 0) i = 0;
  const bool wantSpace = cells[static_cast<size_t>(i)].isSpace;
  if (cells[static_cast<size_t>(i)].isNewline) {  // a break is its own selection
    *outA = i;
    *outB = i + 1;
    return;
  }
  int a = i, b = i + 1;
  while (a > 0) {
    const Cell& p = cells[static_cast<size_t>(a - 1)];
    if (p.isNewline || p.isSpace != wantSpace)
      break;
    --a;
  }
  while (b < n) {
    const Cell& q = cells[static_cast<size_t>(b)];
    if (q.isNewline || q.isSpace != wantSpace)
      break;
    ++b;
  }
  *outA = a;
  *outB = b;
}

}  // namespace richtext
