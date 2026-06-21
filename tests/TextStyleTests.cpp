#include <catch2/catch_test_macros.hpp>

#include "commands/TextStyle.hpp"

#include <vector>

// Pure text-style resolution tests (REQ-044 / ADR-020). These exercise the bake-on-write model:
// Assign bakes the style into a new annotation; RebakeAllForStyle re-applies a style edit to its
// referencing, non-overridden text while leaving overrides, other styles, and legacy (style-less)
// text untouched.

namespace {

TextStyle MakeStyle(const char* name, const char* font, float h, float oblique, bool bold, bool italic) {
  TextStyle s;
  s.name = name;
  s.fontFamily = font;
  s.heightInches = h;
  s.obliqueDeg = oblique;
  s.bold = bold;
  s.italic = italic;
  return s;
}

CadAnnotation MakeText(const std::string& styleName) {
  CadAnnotation a;
  a.kind = CadAnnotation::Kind::Text;
  a.text = "hello";
  a.styleName = styleName;
  return a;
}

}  // namespace

TEST_CASE("EnsureStandard creates a Standard style when missing") {
  std::vector<TextStyle> styles;
  TextStyle& s = TextStyles::EnsureStandard(styles);
  REQUIRE(styles.size() == 1);
  REQUIRE(s.name == std::string(TextStyles::kStandardName));
  // Idempotent: a second call does not duplicate it.
  TextStyles::EnsureStandard(styles);
  REQUIRE(styles.size() == 1);
}

TEST_CASE("Find locates a style by exact name, or returns nullptr") {
  std::vector<TextStyle> styles = {MakeStyle("Notes", "romans.shx", 0.1f, 0.f, false, false)};
  REQUIRE(TextStyles::Find(styles, "Notes") != nullptr);
  REQUIRE(TextStyles::Find(styles, "notes") == nullptr);  // case-sensitive, like AutoCAD style names
  REQUIRE(TextStyles::Find(styles, "Missing") == nullptr);
}

TEST_CASE("Assign bakes every property into a fresh annotation and clears overrides") {
  const TextStyle s = MakeStyle("Title", "Arial", 0.25f, 15.f, true, true);
  CadAnnotation a = MakeText("");
  a.ovHeight = true;  // a stale flag from elsewhere must be cleared by Assign
  TextStyles::Assign(a, s);
  REQUIRE(a.styleName == "Title");
  REQUIRE(a.fontFamily == "Arial");
  REQUIRE(a.plottedHeightInches == 0.25f);
  REQUIRE(a.obliqueDeg == 15.f);
  REQUIRE(a.bold);
  REQUIRE(a.italic);
  REQUIRE_FALSE(a.ovFont);
  REQUIRE_FALSE(a.ovHeight);
  REQUIRE_FALSE(a.ovOblique);
  REQUIRE_FALSE(a.ovBold);
  REQUIRE_FALSE(a.ovItalic);
}

TEST_CASE("Editing a style re-bakes referencing text except overridden properties") {
  const TextStyle before = MakeStyle("Notes", "romans.shx", 0.1f, 0.f, false, false);
  CadAnnotation a = MakeText("Notes");
  TextStyles::Assign(a, before);

  // The user overrides this text's height in Properties; font remains style-driven.
  a.ovHeight = true;
  a.plottedHeightInches = 0.5f;

  // The style is edited (font + height + bold change).
  const TextStyle after = MakeStyle("Notes", "txt.shx", 0.2f, 0.f, true, false);
  std::vector<CadAnnotation> anns = {a};
  TextStyles::RebakeAllForStyle(anns, after);

  REQUIRE(anns[0].fontFamily == "txt.shx");        // non-overridden → follows the style
  REQUIRE(anns[0].bold);                            // non-overridden → follows the style
  REQUIRE(anns[0].plottedHeightInches == 0.5f);     // overridden → keeps the per-text value
}

TEST_CASE("Re-bake leaves other styles' text and legacy style-less text untouched") {
  const TextStyle notes = MakeStyle("Notes", "romans.shx", 0.1f, 0.f, false, false);

  CadAnnotation other = MakeText("Title");
  other.fontFamily = "Arial";
  other.plottedHeightInches = 0.3f;

  CadAnnotation legacy = MakeText("");  // no style reference (older file / DXF import)
  legacy.fontFamily = "myfont.shx";
  legacy.plottedHeightInches = 0.42f;

  std::vector<CadAnnotation> anns = {other, legacy};
  const TextStyle edited = MakeStyle("Notes", "txt.shx", 0.2f, 0.f, true, false);
  TextStyles::RebakeAllForStyle(anns, edited);

  REQUIRE(anns[0].fontFamily == "Arial");          // different style → unchanged
  REQUIRE(anns[0].plottedHeightInches == 0.3f);
  REQUIRE(anns[1].fontFamily == "myfont.shx");     // legacy → unchanged
  REQUIRE(anns[1].plottedHeightInches == 0.42f);
}

TEST_CASE("Re-bake ignores non-text annotations even if they carry a matching name") {
  const TextStyle s = MakeStyle("Notes", "romans.shx", 0.1f, 0.f, false, false);
  CadAnnotation dim;
  dim.kind = CadAnnotation::Kind::DimAligned;
  dim.styleName = "Notes";
  dim.fontFamily = "dimfont.shx";
  dim.plottedHeightInches = 0.085f;
  std::vector<CadAnnotation> anns = {dim};
  TextStyles::RebakeAllForStyle(anns, s);
  REQUIRE(anns[0].fontFamily == "dimfont.shx");     // dimensions are not styleable text
  REQUIRE(anns[0].plottedHeightInches == 0.085f);
}
