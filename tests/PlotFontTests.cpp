#include <catch2/catch_test_macros.hpp>

#include "io/PlotFont.hpp"

// REQ-049: pure font-name/encoding helpers behind the plot's TrueType text path.

TEST_CASE("Aliased families map to their real Windows file name first (REQ-049)", "[plotfont]") {
  // Case-insensitive; the alias .ttf is the first candidate, the de-spaced form is the fallback.
  auto arial = plotfont::TtfCandidates("Arial");
  REQUIRE(arial.size() >= 1);
  REQUIRE(arial.front() == "arial.ttf");

  auto times = plotfont::TtfCandidates("Times New Roman");
  REQUIRE(times.front() == "times.ttf");

  auto courier = plotfont::TtfCandidates("COURIER NEW");
  REQUIRE(courier.front() == "cour.ttf");
}

TEST_CASE("Unknown families fall back to a de-spaced <family>.ttf candidate (REQ-049)", "[plotfont]") {
  auto c = plotfont::TtfCandidates("My Custom Font");
  REQUIRE(c.size() == 1);
  REQUIRE(c.front() == "mycustomfont.ttf");

  // A single-word unknown family still yields one candidate.
  auto v = plotfont::TtfCandidates("Verdana");  // aliased AND de-spaced both resolve to verdana.ttf
  REQUIRE(v.front() == "verdana.ttf");

  // Empty family yields no candidate (nothing to probe → caller substitutes).
  REQUIRE(plotfont::TtfCandidates("").empty());
}

TEST_CASE("Standard-font substitution picks serif/mono/sans by keyword (REQ-049)", "[plotfont]") {
  REQUIRE(std::string(plotfont::StandardSubstitute("Times New Roman")) == "Times-Roman");
  REQUIRE(std::string(plotfont::StandardSubstitute("Georgia")) == "Times-Roman");
  REQUIRE(std::string(plotfont::StandardSubstitute("Courier New")) == "Courier");
  REQUIRE(std::string(plotfont::StandardSubstitute("Consolas")) == "Courier");
  REQUIRE(std::string(plotfont::StandardSubstitute("Arial")) == "Helvetica");
  REQUIRE(std::string(plotfont::StandardSubstitute("Whatever Sans")) == "Helvetica");
}

TEST_CASE("UTF-8 to UTF-16 encodes ASCII, multibyte, and is null-terminated (REQ-049)", "[plotfont]") {
  auto ascii = plotfont::Utf8ToUtf16("AB");
  REQUIRE(ascii.size() == 3);  // 'A','B',NUL
  REQUIRE(ascii[0] == static_cast<unsigned short>('A'));
  REQUIRE(ascii[1] == static_cast<unsigned short>('B'));
  REQUIRE(ascii[2] == 0);

  // U+00E9 'é' = 0xC3 0xA9 (2-byte) → one UTF-16 unit 0x00E9.
  auto acc = plotfont::Utf8ToUtf16("\xC3\xA9");
  REQUIRE(acc.size() == 2);
  REQUIRE(acc[0] == 0x00E9);
  REQUIRE(acc[1] == 0);

  // U+20AC '€' = 0xE2 0x82 0xAC (3-byte) → one UTF-16 unit 0x20AC.
  auto euro = plotfont::Utf8ToUtf16("\xE2\x82\xAC");
  REQUIRE(euro.size() == 2);
  REQUIRE(euro[0] == 0x20AC);

  // A stray continuation byte becomes U+FFFD, not a crash or silent drop.
  auto bad = plotfont::Utf8ToUtf16("\xFF");
  REQUIRE(bad.size() == 2);
  REQUIRE(bad[0] == 0xFFFD);

  auto empty = plotfont::Utf8ToUtf16("");
  REQUIRE(empty.size() == 1);
  REQUIRE(empty[0] == 0);
}
