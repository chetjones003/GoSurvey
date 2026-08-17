#include <catch2/catch_test_macros.hpp>

#include "headless/FuzzGenerator.hpp"
#include "headless/Minimizer.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

// REQ-204 acceptance for the generator and the minimizer.
//
// Both are pure by design — the generator takes a seed and a command list, the minimizer takes a
// predicate — so the properties that matter (reproducibility, termination, boundedness) are testable
// here without building the application, launching a process, or having a GPU.

namespace {

const std::vector<std::string>& SampleCommands() {
  static const std::vector<std::string> k = {"line", "circle", "polyline", "rect",
                                             "move", "rotate", "trim",     "zoom"};
  return k;
}

}  // namespace

// ---------------------------------------------------------------------------
// Generator: reproducibility is the whole contract
// ---------------------------------------------------------------------------

TEST_CASE("The same seed produces an identical transcript", "[fuzz][generator]") {
  const auto a = fuzzgen::Generate(41827, SampleCommands());
  const auto b = fuzzgen::Generate(41827, SampleCommands());
  REQUIRE(a == b);
  REQUIRE_FALSE(a.empty());
}

TEST_CASE("Different seeds produce different transcripts", "[fuzz][generator]") {
  // Not a hard guarantee for any particular pair, but across a spread it must hold — a generator
  // that ignored its seed would pass the reproducibility test above perfectly.
  std::set<std::vector<std::string>> distinct;
  for (std::uint64_t s = 1; s <= 12; ++s)
    distinct.insert(fuzzgen::Generate(s, SampleCommands()));
  REQUIRE(distinct.size() > 1);
}

TEST_CASE("A generated transcript starts from a known state", "[fuzz][generator]") {
  const auto lines = fuzzgen::Generate(5, SampleCommands());
  // Comments may precede it, but NEW must appear before any command: a transcript that inherits
  // state from whatever ran before is not reproducible from its seed.
  const auto newIt = std::find(lines.begin(), lines.end(), std::string("NEW"));
  REQUIRE(newIt != lines.end());
  const auto cmdIt = std::find_if(lines.begin(), lines.end(), [](const std::string& l) {
    return l.rfind("CMD ", 0) == 0;
  });
  REQUIRE(newIt < cmdIt);
}

TEST_CASE("Generation tolerates an empty command list", "[fuzz][generator]") {
  // Degenerate but reachable: if enumeration ever returns nothing, the generator must not divide by
  // zero or index an empty bag. It should still produce a valid transcript.
  const auto lines = fuzzgen::Generate(3, {});
  REQUIRE_FALSE(lines.empty());
  REQUIRE(std::find(lines.begin(), lines.end(), std::string("NEW")) != lines.end());
}

TEST_CASE("The denylist keeps slow and out-of-scope commands out", "[fuzz][generator]") {
  REQUIRE(fuzzgen::IsDeniedCommand("bench"));
  REQUIRE(fuzzgen::IsDeniedCommand("importmodel"));
  REQUIRE(fuzzgen::IsDeniedCommand("quit"));
  REQUIRE_FALSE(fuzzgen::IsDeniedCommand("line"));

  const auto kept = fuzzgen::FilterCommands({"line", "bench", "circle", "quit"});
  REQUIRE(kept == std::vector<std::string>{"line", "circle"});
}

TEST_CASE("A denied command never appears in a generated transcript", "[fuzz][generator]") {
  // The filter is applied by the caller, so this pins the contract end-to-end: filter, then
  // generate, and nothing denied survives.
  const auto commands = fuzzgen::FilterCommands({"line", "bench", "circle", "importmodel"});
  for (std::uint64_t s = 1; s <= 25; ++s) {
    for (const std::string& l : fuzzgen::Generate(s, commands)) {
      REQUIRE(l != "CMD bench");
      REQUIRE(l != "CMD importmodel");
    }
  }
}

// ---------------------------------------------------------------------------
// Minimizer: it must shrink, it must stop, and it must not lie
// ---------------------------------------------------------------------------

TEST_CASE("Minimize reduces to the lines the failure actually needs", "[fuzz][minimizer]") {
  std::vector<std::string> lines;
  lines.push_back("NEW");
  for (int i = 0; i < 30; ++i)
    lines.push_back("NOISE " + std::to_string(i));
  lines.push_back("BOOM");
  for (int i = 0; i < 30; ++i)
    lines.push_back("MORE " + std::to_string(i));

  // The failure depends on exactly one line.
  const auto stillFails = [](const std::vector<std::string>& c) {
    return std::find(c.begin(), c.end(), std::string("BOOM")) != c.end();
  };

  const minimizer::Result r = minimizer::Minimize(lines, stillFails);

  REQUIRE(stillFails(r.lines));
  REQUIRE(std::find(r.lines.begin(), r.lines.end(), std::string("BOOM")) != r.lines.end());
  // NEW is pinned, BOOM is required; nothing else should survive.
  REQUIRE(r.lines.size() == 2);
  REQUIRE(r.lines.front() == "NEW");
  REQUIRE(r.reductionRatio() > 0.9);
  REQUIRE_FALSE(r.hitAttemptCap);
}

TEST_CASE("Minimize keeps every line a conjunctive failure needs", "[fuzz][minimizer]") {
  // Two lines are both required. A minimizer that removed either would produce a reproducer that
  // does not reproduce — the worst possible output, because it looks authoritative.
  std::vector<std::string> lines = {"NEW", "a", "b", "c", "KEY1", "d", "e", "KEY2", "f", "g"};
  const auto stillFails = [](const std::vector<std::string>& c) {
    return std::find(c.begin(), c.end(), std::string("KEY1")) != c.end() &&
           std::find(c.begin(), c.end(), std::string("KEY2")) != c.end();
  };

  const minimizer::Result r = minimizer::Minimize(lines, stillFails);

  REQUIRE(stillFails(r.lines));
  REQUIRE(r.lines.size() == 3);  // NEW + KEY1 + KEY2
}

TEST_CASE("Minimize preserves the original order", "[fuzz][minimizer]") {
  std::vector<std::string> lines = {"NEW", "x", "KEY1", "y", "KEY2", "z"};
  const auto stillFails = [](const std::vector<std::string>& c) {
    return std::find(c.begin(), c.end(), std::string("KEY1")) != c.end() &&
           std::find(c.begin(), c.end(), std::string("KEY2")) != c.end();
  };
  const minimizer::Result r = minimizer::Minimize(lines, stillFails);
  const auto i1 = std::find(r.lines.begin(), r.lines.end(), std::string("KEY1"));
  const auto i2 = std::find(r.lines.begin(), r.lines.end(), std::string("KEY2"));
  REQUIRE(i1 < i2);
}

TEST_CASE("Minimize refuses a transcript that does not fail", "[fuzz][minimizer]") {
  // Guard against the most dangerous misuse: minimizing something that never failed would
  // "successfully" delete every line and emit a confident, empty reproducer.
  const std::vector<std::string> lines = {"NEW", "a", "b", "c"};
  const minimizer::Result r = minimizer::Minimize(lines, [](const std::vector<std::string>&) {
    return false;
  });
  REQUIRE(r.lines == lines);
  REQUIRE(r.attempts == 0);
  REQUIRE(r.reductionRatio() == 0.0);
}

TEST_CASE("Minimize honours its attempt cap", "[fuzz][minimizer]") {
  // A predicate that only ever accepts the full input forces the maximum number of failed removals.
  std::vector<std::string> lines;
  lines.push_back("NEW");
  for (int i = 0; i < 200; ++i)
    lines.push_back("line " + std::to_string(i));
  const size_t full = lines.size();

  minimizer::Options opt;
  opt.maxAttempts = 12;
  const minimizer::Result r = minimizer::Minimize(
      lines, [full](const std::vector<std::string>& c) { return c.size() == full; }, opt);

  REQUIRE(r.attempts <= opt.maxAttempts);
  REQUIRE(r.hitAttemptCap);
  REQUIRE(r.lines.size() == full);  // nothing could be removed, so nothing was
}

TEST_CASE("Minimize terminates on a single-line input", "[fuzz][minimizer]") {
  const std::vector<std::string> lines = {"BOOM"};
  const minimizer::Result r = minimizer::Minimize(lines, [](const std::vector<std::string>& c) {
    return !c.empty();
  });
  REQUIRE(r.lines.size() == 1);
  REQUIRE_FALSE(r.hitAttemptCap);
}

TEST_CASE("Minimize reports an honest reduction ratio", "[fuzz][minimizer]") {
  std::vector<std::string> lines = {"NEW", "a", "b", "c", "BOOM"};
  const minimizer::Result r = minimizer::Minimize(lines, [](const std::vector<std::string>& c) {
    return std::find(c.begin(), c.end(), std::string("BOOM")) != c.end();
  });
  REQUIRE(r.originalSize == 5);
  REQUIRE(r.lines.size() == 2);
  REQUIRE(r.reductionRatio() == 0.6);
}
