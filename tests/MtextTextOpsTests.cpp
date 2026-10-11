#include <catch2/catch_test_macros.hpp>

#include "ui/MtextTextOps.hpp"

#include <string>

// REQ-051 Options menu. The property that matters for every one of these: they rewrite user text and
// never the bytes of a [[...]] tag, so the wire format survives the edit intact.

TEST_CASE("Change case rewrites text but never tag bytes (REQ-051)", "[mtextops]") {
  std::string wire = "abc[[b]]def[[/b]]";
  REQUIRE(mtextops::UpperRange(wire, 0, wire.size()));
  REQUIRE(wire == "ABC[[b]]DEF[[/b]]");  // the b of [[b]] must NOT have been upper-cased

  REQUIRE(mtextops::LowerRange(wire, 0, wire.size()));
  REQUIRE(wire == "abc[[b]]def[[/b]]");
}

TEST_CASE("Change case honours a partial range (REQ-051)", "[mtextops]") {
  std::string wire = "abcdef";
  REQUIRE(mtextops::UpperRange(wire, 2, 4));
  REQUIRE(wire == "abCDef");

  // Failure modes: an inverted, empty, or out-of-bounds range changes nothing and does not read past the end.
  std::string w2 = "abc";
  REQUIRE_FALSE(mtextops::UpperRange(w2, 2, 1));
  REQUIRE_FALSE(mtextops::UpperRange(w2, 1, 1));
  REQUIRE_FALSE(mtextops::UpperRange(w2, 99, 200));
  REQUIRE(mtextops::UpperRange(w2, 0, 9999));  // clamped to the buffer, still applies
  REQUIRE(w2 == "ABC");
}

TEST_CASE("A colour tag's hex digits are not treated as text (REQ-051)", "[mtextops]") {
  // Regression guard: [[color:ffaabb]] contains letters that a naive case pass would rewrite, breaking
  // the wire (the parser wants them, but a lower-cased [[COLOR:...]] tag name would not parse at all).
  std::string wire = "x[[color:ffaabb]]y[[/color]]";
  mtextops::UpperRange(wire, 0, wire.size());
  REQUIRE(wire == "X[[color:ffaabb]]Y[[/color]]");
}

TEST_CASE("Flatten and remove-formatting strip tags, keeping the text (REQ-051)", "[mtextops]") {
  REQUIRE(mtextops::FlattenToPlain("The [[b]]north[[/b]] line") == "The north line");
  REQUIRE(mtextops::FlattenToPlain("") == "");
  REQUIRE(mtextops::FlattenToPlain("plain") == "plain");

  std::string wire = "keep [[b]]bold[[/b]] end";
  REQUIRE(mtextops::RemoveFormattingRange(wire, 5, 20));  // covers "[[b]]bold[[/b]]"
  REQUIRE(wire == "keep bold end");

  // Nothing to strip → no change reported, so the caller does not push a pointless undo entry.
  std::string plain = "no tags here";
  REQUIRE_FALSE(mtextops::RemoveFormattingRange(plain, 0, plain.size()));
  REQUIRE_FALSE(mtextops::RemoveFormattingRange(plain, 5, 2));
  REQUIRE_FALSE(mtextops::RemoveFormattingRange(plain, 0, 9999));  // out of bounds is refused, not clamped
}

TEST_CASE("Find and replace works on visible text and preserves tags (REQ-051)", "[mtextops]") {
  std::string wire = "the [[b]]cat[[/b]] and the cat";
  REQUIRE(mtextops::FindReplaceAll(wire, "cat", "dog", true) == 2);
  REQUIRE(wire == "the [[b]]dog[[/b]] and the dog");  // formatting survived the replacement
}

TEST_CASE("A match never spans a tag boundary (REQ-051)", "[mtextops]") {
  // "ab" is visually present across a[[b]]b, but replacing across the tag would destroy the markup.
  std::string wire = "a[[b]]b[[/b]]";
  REQUIRE(mtextops::FindReplaceAll(wire, "ab", "Z", true) == 0);
  REQUIRE(wire == "a[[b]]b[[/b]]");
}

TEST_CASE("Find and replace: case sensitivity and degenerate input (REQ-051)", "[mtextops]") {
  std::string wire = "Cat cat CAT";
  REQUIRE(mtextops::FindReplaceAll(wire, "cat", "dog", true) == 1);
  REQUIRE(wire == "Cat dog CAT");

  std::string w2 = "Cat cat CAT";
  REQUIRE(mtextops::FindReplaceAll(w2, "cat", "dog", false) == 3);
  REQUIRE(w2 == "dog dog dog");

  // Failure modes: an empty needle matches nothing, and a not-found needle leaves the buffer alone.
  std::string w3 = "abc";
  REQUIRE(mtextops::FindReplaceAll(w3, "", "x", true) == 0);
  REQUIRE(mtextops::FindReplaceAll(w3, "zz", "x", true) == 0);
  REQUIRE(w3 == "abc");
}

TEST_CASE("A self-containing replacement terminates (REQ-051)", "[mtextops]") {
  // Failure mode: replacing "a" with "aa" re-matches its own output forever without the guard.
  std::string wire = "a";
  const int n = mtextops::FindReplaceAll(wire, "a", "aa", true);
  REQUIRE(n >= 1);
  REQUIRE(wire.size() < 100);  // terminated rather than growing without bound
}

TEST_CASE("Autocorrect fixes a caps-lock-inverted word (REQ-051)", "[mtextops]") {
  std::string wire = "hELLO ";
  REQUIRE(mtextops::AutocorrectCapsLockWord(wire, 5));  // end of "hELLO"
  REQUIRE(wire == "Hello ");
}

TEST_CASE("Autocorrect leaves normally-typed words alone (REQ-051)", "[mtextops]") {
  // Each of these must NOT be touched: correct casing, all-caps (deliberate), a single letter,
  // a mixed word that is not the caps-lock pattern, and an out-of-range offset.
  std::string a = "Hello";
  REQUIRE_FALSE(mtextops::AutocorrectCapsLockWord(a, 5));
  std::string b = "HELLO";
  REQUIRE_FALSE(mtextops::AutocorrectCapsLockWord(b, 5));
  std::string c = "h";
  REQUIRE_FALSE(mtextops::AutocorrectCapsLockWord(c, 1));
  std::string d = "hELlO";
  REQUIRE_FALSE(mtextops::AutocorrectCapsLockWord(d, 5));
  std::string e = "hELLO";
  REQUIRE_FALSE(mtextops::AutocorrectCapsLockWord(e, 0));
  REQUIRE_FALSE(mtextops::AutocorrectCapsLockWord(e, 999));
  REQUIRE(e == "hELLO");
}

TEST_CASE("Autocorrect stops at the span it is in, never rewriting a tag (REQ-051)", "[mtextops]") {
  std::string wire = "[[b]]hELLO[[/b]]";
  REQUIRE(mtextops::AutocorrectCapsLockWord(wire, 10));  // end of "hELLO", before [[/b]]
  REQUIRE(wire == "[[b]]Hello[[/b]]");
}
