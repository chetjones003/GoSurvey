#include <catch2/catch_test_macros.hpp>

#include "ui/RichTextLayout.hpp"

#include <string>
#include <vector>

// ADR-023 / REQ-051: the pure layout + caret math behind the WYSIWYG MTEXT editor.

namespace {

/// Build cells for a wire string and give every character a fixed advance, so wrapping is exact and the
/// tests say nothing about fonts.
std::vector<richtext::Cell> CellsFor(const std::string& wire, float charW = 10.f) {
  std::vector<MtextRichSpan> spans;
  MtextRichBuildSpans(wire, &spans);
  std::vector<richtext::Cell> cells;
  richtext::BuildCells(wire, spans, &cells);
  for (auto& c : cells)
    c.w = charW;
  return cells;
}

std::string VisibleText(const std::string& wire, const std::vector<richtext::Cell>& cells) {
  std::string out;
  for (const auto& c : cells)
    out += wire.substr(c.rawBegin, c.rawEnd - c.rawBegin);
  return out;
}

}  // namespace

TEST_CASE("Tag bytes never become cells: the editor shows text, not tags (ADR-023)", "[richtext]") {
  const std::string wire = "The [[b]]north[[/b]] line";
  const auto cells = CellsFor(wire);
  REQUIRE(VisibleText(wire, cells) == "The north line");
  REQUIRE(cells.size() == 14);

  // The 'n' of "north" must point at its real byte, past the [[b]] tag.
  REQUIRE(wire[cells[4].rawBegin] == 'n');
  REQUIRE(cells[4].spanIndex == 1);  // inside the bold span
  REQUIRE(cells[0].spanIndex == 0);
}

TEST_CASE("Malformed and unterminated tags stay literal without breaking offsets (ADR-023)", "[richtext]") {
  // An unknown tag and a truncated colour tag are user text; every byte must still be reachable.
  const std::string wire = "a[[zz]]b[[color:12]]c";
  const auto cells = CellsFor(wire);
  REQUIRE(VisibleText(wire, cells) == wire);
  for (size_t j = 1; j < cells.size(); ++j)
    REQUIRE(cells[j].rawBegin == cells[j - 1].rawEnd);  // contiguous, no gaps or overlap
}

TEST_CASE("Multi-byte UTF-8 is never split mid-character (ADR-023)", "[richtext]") {
  const std::string wire = "N82\xC2\xB0" "E";  // degree sign is two bytes
  const auto cells = CellsFor(wire);
  REQUIRE(cells.size() == 5);
  REQUIRE(cells[3].rawEnd - cells[3].rawBegin == 2);
  REQUIRE(VisibleText(wire, cells) == wire);
}

TEST_CASE("Text wraps at the column on a word boundary (REQ-051)", "[richtext]") {
  const std::string wire = "aaa bbb ccc";
  auto cells = CellsFor(wire);  // 10px per char
  const int lines = richtext::WrapCells(&cells, 80.f);
  REQUIRE(lines == 2);
  // "aaa bbb " is 8 chars = 80px; "ccc" moves down rather than splitting.
  REQUIRE(cells[0].line == 0);
  REQUIRE(cells[7].line == 0);
  REQUIRE(cells[8].line == 1);
  REQUIRE(cells[8].x == 0.f);  // the new line starts at the left margin
}

TEST_CASE("A word longer than the column breaks instead of overflowing (REQ-051)", "[richtext]") {
  auto cells = CellsFor("abcdefgh");  // 8 chars × 10px, column 35px
  const int lines = richtext::WrapCells(&cells, 35.f);
  REQUIRE(lines == 3);
  REQUIRE(cells[0].line == 0);
  REQUIRE(cells[3].line == 1);  // 3 chars fit per line
  REQUIRE(cells[6].line == 2);
}

TEST_CASE("A character wider than the whole column still lays out, one per line (REQ-051)", "[richtext]") {
  // Failure mode: without the "end > lineStart" guard this loops forever placing nothing.
  auto cells = CellsFor("abc", 50.f);
  const int lines = richtext::WrapCells(&cells, 20.f);
  REQUIRE(lines == 3);
  REQUIRE(cells[2].line == 2);
}

TEST_CASE("Explicit newlines break lines; a non-positive column wraps only at them (REQ-051)",
          "[richtext]") {
  auto cells = CellsFor("ab\ncd");
  int lines = richtext::WrapCells(&cells, 0.f);
  REQUIRE(lines == 2);
  REQUIRE(cells[0].line == 0);
  REQUIRE(cells[3].line == 1);  // 'c' after the newline

  // A trailing newline leaves an empty last line for the caret to sit on.
  auto trailing = CellsFor("ab\n");
  REQUIRE(richtext::WrapCells(&trailing, 0.f) == 2);
}

TEST_CASE("Empty text lays out as a single line (REQ-051)", "[richtext]") {
  auto cells = CellsFor("");
  REQUIRE(cells.empty());
  REQUIRE(richtext::WrapCells(&cells, 100.f) == 1);
}

TEST_CASE("Insertion lands inside the run to the caret's left (ADR-023)", "[richtext]") {
  const std::string wire = "ab[[b]]cd[[/b]]";
  const auto cells = CellsFor(wire);
  REQUIRE(VisibleText(wire, cells) == "abcd");

  // Caret after 'c' (visible index 3) must insert between 'c' and 'd' — inside the bold run.
  const size_t at = richtext::InsertOffset(cells, wire.size(), 3);
  REQUIRE(at == cells[2].rawEnd);
  REQUIRE(wire[at] == 'd');

  // Caret at the very end lands after 'd' but BEFORE [[/b]], so typing stays bold.
  const size_t atEnd = richtext::InsertOffset(cells, wire.size(), 4);
  REQUIRE(atEnd == cells[3].rawEnd);
  REQUIRE(wire.compare(atEnd, 6, "[[/b]]") == 0);

  // Caret at 0 sits at the first character's byte, not at byte 0 ahead of any tag.
  REQUIRE(richtext::InsertOffset(cells, wire.size(), 0) == cells[0].rawBegin);
}

TEST_CASE("Selection maps to the tightest raw byte range (ADR-023)", "[richtext]") {
  const std::string wire = "The [[b]]north[[/b]] line";
  const auto cells = CellsFor(wire);
  size_t a = 0, b = 0;
  richtext::SelectionRawRange(cells, 4, 9, &a, &b);  // "north"
  REQUIRE(wire.substr(a, b - a) == "north");

  // Reversed (dragged right-to-left) selects the same range.
  size_t a2 = 0, b2 = 0;
  richtext::SelectionRawRange(cells, 9, 4, &a2, &b2);
  REQUIRE(a2 == a);
  REQUIRE(b2 == b);

  // An empty selection is a caret: a zero-width range, never inverted.
  size_t a3 = 0, b3 = 0;
  richtext::SelectionRawRange(cells, 6, 6, &a3, &b3);
  REQUIRE(a3 == b3);
}

TEST_CASE("Out-of-range caret and selection indices clamp (REQ-051)", "[richtext]") {
  const std::string wire = "abc";
  const auto cells = CellsFor(wire);
  REQUIRE(richtext::InsertOffset(cells, wire.size(), -5) == cells[0].rawBegin);
  REQUIRE(richtext::InsertOffset(cells, wire.size(), 99) == cells.back().rawEnd);

  size_t a = 0, b = 0;
  richtext::SelectionRawRange(cells, -3, 99, &a, &b);
  REQUIRE(a == 0);
  REQUIRE(b == 3);

  // An empty buffer must not index anything.
  const std::vector<richtext::Cell> none;
  REQUIRE(richtext::InsertOffset(none, 0, 7) == 0);
  size_t ea = 9, eb = 9;
  richtext::SelectionRawRange(none, 0, 4, &ea, &eb);
  REQUIRE(ea == 0);
  REQUIRE(eb == 0);
}

TEST_CASE("Clicking finds the nearest caret position (REQ-051)", "[richtext]") {
  auto cells = CellsFor("abc\ndef");
  richtext::WrapCells(&cells, 0.f);
  const float lineH = 20.f;

  REQUIRE(richtext::CaretFromPoint(cells, 2, lineH, 2.f, 5.f) == 0);    // left of 'a'
  REQUIRE(richtext::CaretFromPoint(cells, 2, lineH, 16.f, 5.f) == 2);   // past 'b' midpoint
  REQUIRE(richtext::CaretFromPoint(cells, 2, lineH, 500.f, 5.f) == 3);  // past line end → before the \n
  REQUIRE(richtext::CaretFromPoint(cells, 2, lineH, 2.f, 25.f) == 4);   // start of line 2 ('d')
  REQUIRE(richtext::CaretFromPoint(cells, 2, lineH, 500.f, 25.f) == 7); // end of the buffer

  // Clicks above and below the text clamp to the first and last line.
  REQUIRE(richtext::CaretFromPoint(cells, 2, lineH, 2.f, -50.f) == 0);
  REQUIRE(richtext::CaretFromPoint(cells, 2, lineH, 500.f, 900.f) == 7);
}

TEST_CASE("Double-click selects a word, a space run, or a break (REQ-051)", "[richtext]") {
  auto cells = CellsFor("aa  bb");
  richtext::WrapCells(&cells, 0.f);
  int a = 0, b = 0;

  richtext::WordBounds(cells, 0, &a, &b);
  REQUIRE(a == 0);
  REQUIRE(b == 2);  // "aa"

  richtext::WordBounds(cells, 2, &a, &b);
  REQUIRE(a == 2);
  REQUIRE(b == 4);  // the run of spaces

  richtext::WordBounds(cells, 5, &a, &b);
  REQUIRE(a == 4);
  REQUIRE(b == 6);  // "bb"

  // Failure mode: an index past the end must clamp, not read off the end.
  richtext::WordBounds(cells, 99, &a, &b);
  REQUIRE(b <= static_cast<int>(cells.size()));

  const std::vector<richtext::Cell> none;
  richtext::WordBounds(none, 3, &a, &b);
  REQUIRE(a == 0);
  REQUIRE(b == 0);
}
