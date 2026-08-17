#pragma once

// Structure-aware transcript generation (REQ-204).
//
// PURE by construction: it takes a seed and a command list and returns transcript lines. It reads no
// clock, touches no file, and calls nothing in the Commands layer — which is what lets the Catch2
// suite test it without linking the application, and what makes `--seed N` reproducible forever.
//
// The command list is a PARAMETER rather than something this module fetches for itself. That is
// deliberate twice over: it keeps the generator testable against a fixed list, and it keeps the
// generator from needing access to `kRegistry`, which lives in an anonymous namespace inside
// CadCommands.cpp and is not the Workshop's to expose.
//
// Why structure-aware rather than byte-mutating: the command line is a grammar, and a byte mutator
// would spend its whole budget rediscovering that `LINE` is a word before it ever reached a second
// command. Byte mutation is the right shape for the *file parsers*, which is a separate target
// (docs/fuzz-harness.md §8 stage 6).

#include <cstdint>
#include <string>
#include <vector>

namespace fuzzgen {

struct Options {
  /// Transcript length, in generated actions (each may emit several lines).
  int minActions = 12;
  int maxActions = 48;

  /// Chance in [0,1] that an argument is drawn from the hostile ladder rather than a plain
  /// coordinate. Hostile values are the point of the exercise, but a transcript made only of NaNs
  /// never builds enough geometry to reach the interesting states.
  double hostileChance = 0.25;

  /// Chance that a command sequence is deliberately abandoned mid-way (no terminator), which is how
  /// a real user leaves a command and a common source of stuck state.
  double abandonChance = 0.15;

  /// Emit the fixed prelude that builds a base drawing before the random actions begin.
  ///
  /// On by default because without it the fuzzer barely reaches any state worth checking: modify
  /// commands need geometry to modify and something selected to act on, and a random transcript
  /// starting from an empty drawing produces neither. Off only for tests that want to inspect the
  /// generated actions alone.
  bool emitPrelude = true;

  /// Append the `gs-roundtrip` differential oracle (save -> load -> save -> compare) at the end.
  ///
  /// **Off by default, on purpose** — still, after #56 and #57 were fixed.
  ///
  /// The original reason was #56 (a TEXT entity saved with id 0), and the note here said to make
  /// this the default once #56 closed. Measuring rather than assuming: with #56 and #57 fixed, a
  /// 1000-seed sweep still fails ~325 times, on two further defects — #61 (large coordinates break
  /// resave idempotence) and #60 (erasing the last polyline writes a `.gs` that cannot be
  /// reopened). Leaving it on would bury every new finding under #61, which is the exact
  /// "noise buries signal" failure this harness exists to avoid.
  ///
  /// Turn it on with `--roundtrip` to work on those two deliberately. Flip the default when a
  /// `--roundtrip` sweep comes back clean — not when a particular issue closes.
  bool emitRoundTrip = false;
};

/// Commands the generator must never emit, with the reason. Exposed so a caller can report it and
/// so the test can assert the filter actually applies.
///
/// A DENYLIST, not an allowlist: a command added to GoSurvey tomorrow should be fuzzed by default.
/// An allowlist would silently exclude every new command, which is the failure mode that makes a
/// fuzzer quietly stop finding things.
bool IsDeniedCommand(const std::string& lowerName);

/// Filter \p all through \ref IsDeniedCommand, preserving order.
std::vector<std::string> FilterCommands(const std::vector<std::string>& all);

/// Generate a transcript. \p commands should already be filtered; passing an empty list yields a
/// transcript that still exercises NEW/UNDO/REDO/ESC/PICK rather than nothing at all.
///
/// The same (seed, commands, options) always produces the same lines — REQ-204's first acceptance
/// condition, pinned by a test.
std::vector<std::string> Generate(std::uint64_t seed, const std::vector<std::string>& commands,
                                  const Options& opt = Options{});

}  // namespace fuzzgen
