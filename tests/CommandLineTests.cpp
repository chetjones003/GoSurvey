#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/CommandBar.hpp"

using Catch::Approx;

// REQ-040: the recent-history overlay is fully opaque until the idle delay, then
// fades to zero over the fade duration.
TEST_CASE("Command-bar history fade alpha", "[commandline]") {
  const double delay = 4.0;
  const double dur = 1.0;

  // Before and at the delay: fully opaque.
  REQUIRE(cmdbar::HistoryAlpha(0.0, delay, dur) == Approx(1.0f));
  REQUIRE(cmdbar::HistoryAlpha(4.0, delay, dur) == Approx(1.0f));

  // Mid-fade: linear ramp down.
  REQUIRE(cmdbar::HistoryAlpha(4.5, delay, dur) == Approx(0.5f));

  // At/after the end of the fade: fully transparent.
  REQUIRE(cmdbar::HistoryAlpha(5.0, delay, dur) == Approx(0.0f));
  REQUIRE(cmdbar::HistoryAlpha(9.0, delay, dur) == Approx(0.0f));

  // Failure mode: a zero-length fade snaps to 0 the instant the delay passes.
  REQUIRE(cmdbar::HistoryAlpha(4.0, delay, 0.0) == Approx(1.0f));
  REQUIRE(cmdbar::HistoryAlpha(4.0001, delay, 0.0) == Approx(0.0f));
}

// REQ-040: the recent-tail / F2-console start index shows the last N lines.
TEST_CASE("Command-log tail start index", "[commandline]") {
  // Fewer lines than the window: show everything from the top.
  REQUIRE(cmdbar::LogTailStart(2, 3) == 0);
  REQUIRE(cmdbar::LogTailStart(3, 3) == 0);

  // More lines than the window: skip the oldest.
  REQUIRE(cmdbar::LogTailStart(10, 3) == 7);

  // Empty log.
  REQUIRE(cmdbar::LogTailStart(0, 3) == 0);

  // Failure mode: a non-positive window shows nothing (start == total).
  REQUIRE(cmdbar::LogTailStart(10, 0) == 10);
  REQUIRE(cmdbar::LogTailStart(10, -1) == 10);
}

// ---------------------------------------------------------------------------------------------
// REQ-119 — the prompt→variants rule.
// ---------------------------------------------------------------------------------------------

namespace {

// The links a prompt yields, as "label>shortcut" pairs — the two things a click depends on.
std::vector<std::string> LinksOf(const char* hint) {
  std::vector<std::string> v;
  for (const cmdbar::PromptSegment& s : cmdbar::ParsePromptSegments(hint)) {
    if (s.isLink)
      v.push_back(s.text + ">" + s.shortcut);
  }
  return v;
}

// Everything the user sees, links included, concatenated back. Nothing may be lost or
// duplicated by parsing: what is drawn must still read as the prompt that was written.
std::string RoundTrip(const char* hint) {
  std::string s;
  for (const cmdbar::PromptSegment& p : cmdbar::ParsePromptSegments(hint))
    s += p.text;
  return s;
}

} // namespace

TEST_CASE("Command variant shortcut is the leading uppercase run", "[commandline][req119]") {
  // The convention as the codebase already writes it.
  REQUIRE(cmdbar::VariantShortcut("DElta") == "DE");
  REQUIRE(cmdbar::VariantShortcut("Percent") == "P");
  REQUIRE(cmdbar::VariantShortcut("Total") == "T");
  REQUIRE(cmdbar::VariantShortcut("DYnamic") == "DY");
  REQUIRE(cmdbar::VariantShortcut("Yes") == "Y");
  REQUIRE(cmdbar::VariantShortcut("No") == "N");
  REQUIRE(cmdbar::VariantShortcut("Radius") == "R");

  // Digits count, so LINE's two-point-bearing option survives.
  REQUIRE(cmdbar::VariantShortcut("2P") == "2P");
  REQUIRE(cmdbar::VariantShortcut("3P") == "3P");
  REQUIRE(cmdbar::VariantShortcut("A") == "A");

  // The awkward one: a space inside the display text. FILLET/CHAMFER accept "n" for this
  // (CadCommands.cpp:12436), which is exactly the leading run.
  REQUIRE(cmdbar::VariantShortcut("No trim") == "N");
  REQUIRE(cmdbar::VariantShortcut(" No trim ") == "N");

  // Failure mode: no leading uppercase run at all. Falling back to the whole trimmed text
  // keeps the token submittable — an empty shortcut would make the link silently inert.
  REQUIRE(cmdbar::VariantShortcut("through") == "through");
  REQUIRE(cmdbar::VariantShortcut("") == "");
}

TEST_CASE("Inline command variants parse as today's LINE prompt", "[commandline][req119]") {
  // The reference prompt. This must not change: REQ-040 Acceptance (7) promises it keeps working.
  const char* line = "Next: click; X, Y; @dx,dy; [A]zimuth, [2P];";
  REQUIRE(LinksOf(line) == std::vector<std::string>{"[A]>A", "[2P]>2P"});
  REQUIRE(RoundTrip(line) == line);

  // The label keeps its brackets; the trailing lowercase stays plain text beside it.
  const std::vector<cmdbar::PromptSegment> segs = cmdbar::ParsePromptSegments(line);
  REQUIRE(segs.size() == 5);
  REQUIRE(segs[0].text == "Next: click; X, Y; @dx,dy; ");
  REQUIRE_FALSE(segs[0].isLink);
  REQUIRE(segs[1].text == "[A]");
  REQUIRE(segs[1].isLink);
  REQUIRE(segs[2].text == "zimuth, ");
  REQUIRE_FALSE(segs[2].isLink);
  REQUIRE(segs[3].text == "[2P]");
  REQUIRE(segs[3].isLink);
}

TEST_CASE("Grouped command variants each become their own link", "[commandline][req119]") {
  // The defect REQ-119 exists to fix. Before: ONE link submitting "delta/percent/total/dynamic",
  // which TryLengthenModeToggle rejects. After: four links, each a token it accepts.
  const char* lengthen = "LENGTHEN — select object, or [DElta/Percent/Total/DYnamic]:";
  REQUIRE(LinksOf(lengthen) ==
          std::vector<std::string>{"DElta>DE", "Percent>P", "Total>T", "DYnamic>DY"});
  REQUIRE(RoundTrip(lengthen) == lengthen);

  // Same defect, MIRROR. Before: one link submitting "yes/no", which the command rejects.
  const char* mirror = "Erase source objects? [Yes/No] <N>:";
  REQUIRE(LinksOf(mirror) == std::vector<std::string>{"Yes>Y", "No>N"});
  REQUIRE(RoundTrip(mirror) == mirror);

  // Brackets and separators stay plain, so the prompt still reads as AutoCAD writes it.
  const std::vector<cmdbar::PromptSegment> segs = cmdbar::ParsePromptSegments(mirror);
  REQUIRE(segs[1].text == "[");
  REQUIRE_FALSE(segs[1].isLink);
  REQUIRE(segs[3].text == "/");
  REQUIRE_FALSE(segs[3].isLink);
  REQUIRE(segs[5].text == "]");
  REQUIRE_FALSE(segs[5].isLink);

  // FILLET/CHAMFER prompt this way too (reachable once REQ-119 increment 2 gives them a
  // live prompt entry); the same rule already yields the tokens their handlers accept.
  REQUIRE(LinksOf("FILLET — select first object or [Radius/Trim] ") ==
          std::vector<std::string>{"Radius>R", "Trim>T"});
  REQUIRE(LinksOf("FILLET — Enter Trim mode option [Trim/No trim] <Trim>:") ==
          std::vector<std::string>{"Trim>T", "No trim>N"});
  REQUIRE(LinksOf("CHAMFER — select first object or [Distance/Angle/Trim] ") ==
          std::vector<std::string>{"Distance>D", "Angle>A", "Trim>T"});
}

TEST_CASE("Malformed command-variant markup stays literal text", "[commandline][req119]") {
  // Failure mode 1: no closing bracket. The '[' is ordinary text, and nothing is lost.
  REQUIRE(LinksOf("LINE: [Azimuth without a close").empty());
  REQUIRE(RoundTrip("LINE: [Azimuth without a close") == "LINE: [Azimuth without a close");

  // Failure mode 2: nothing submittable inside. A link here would submit an empty token and
  // do nothing when clicked, which is worse than plain text because it looks actionable.
  REQUIRE(LinksOf("empty [] group").empty());
  REQUIRE(RoundTrip("empty [] group") == "empty [] group");
  REQUIRE(LinksOf("slashes [/] only").empty());
  REQUIRE(RoundTrip("slashes [/] only") == "slashes [/] only");
  REQUIRE(LinksOf("spaces [ ] only").empty());
  REQUIRE(RoundTrip("spaces [ ] only") == "spaces [ ] only");

  // Failure mode 3: a partly-empty group still links the options that ARE submittable,
  // rather than throwing the whole group away.
  REQUIRE(LinksOf("[Yes/]") == std::vector<std::string>{"Yes>Y"});
  REQUIRE(RoundTrip("[Yes/]") == "[Yes/]");

  // Empty and null inputs.
  REQUIRE(cmdbar::ParsePromptSegments("").empty());
  REQUIRE(cmdbar::ParsePromptSegments(nullptr).empty());

  // A prompt with no markup at all round-trips as one plain segment.
  const char* plain = "ARC: Start point | ESC cancel";
  REQUIRE(LinksOf(plain).empty());
  REQUIRE(cmdbar::ParsePromptSegments(plain).size() == 1);
  REQUIRE(RoundTrip(plain) == plain);
}
