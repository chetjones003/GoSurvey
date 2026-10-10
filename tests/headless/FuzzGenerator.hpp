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

  /// Append the `gs-roundtrip` differential oracle at the end: save -> load -> save -> load -> save,
  /// comparing the LAST TWO saves (see FuzzGenerator.cpp for why it is the last two).
  ///
  /// **On by default since 2026-08-17**, and the history is worth keeping because it is the record of
  /// a rule being followed rather than an issue being closed.
  ///
  /// It was off for #56 (a TEXT entity saved with id 0), and the note here used to say "make this the
  /// default once #56 closed". That was replaced with a stricter rule — *flip the default when a
  /// `--roundtrip` sweep comes back clean, not when a particular issue closes* — precisely because
  /// closing #56 turned out not to be enough: a 1000-seed sweep still failed ~325 times, on #57, #60
  /// and #61. Leaving it on then would have buried every new finding under #61, which is the
  /// "noise buries signal" failure this harness exists to avoid.
  ///
  /// The rule is now satisfied on its own terms: with #56, #57, #60 fixed and #61 resolved by
  /// amending REQ-079 (decision D-2026-08-17-a) so the oracle compares B to C rather than A to B,
  /// a **1000-seed `--roundtrip` sweep reports 0 failures** — measured, not assumed. Use
  /// `--no-roundtrip` to skip it.
  bool emitRoundTrip = true;

  /// Append the `undo-redo-identity` differential oracle: save -> undo -> redo -> save, comparing
  /// the two saves byte for byte (REQ-204's first invariant, "the classic CAD defect class").
  ///
  /// The comparison is `.gs` bytes for the same reason `emitRoundTrip`'s is: `.gs` is the canonical
  /// serialization of an AppCommandState, so two states that write identical bytes are the same
  /// document by the only definition the format admits. It needs no equality predicate, and it
  /// forgives nothing — which is correct here, because undo is a restore rather than a computation
  /// and REQ-101's tolerance has no bearing on a snapshot agreeing with itself.
  ///
  /// The emitted block opens with an ANCHOR EDIT (one committed line) and asserts
  /// `EXPECT DIFFERENTFILE` across the undo before asserting `EXPECT SAMEFILE` across the redo.
  /// Both exist because the naive shape lies in both directions, and the second was caught by
  /// measurement rather than by reasoning — see the comment at the emission site in
  /// FuzzGenerator.cpp for the seed that produced it.
  ///
  /// **On by default since 2026-08-18**, under the rule \ref emitRoundTrip records: flip a default
  /// when a sweep comes back clean, not when a particular issue closes. Satisfied on its own terms
  /// — a **1000-seed `--undo-redo` sweep reports 0 failures**, measured after the anchor edit was
  /// added, not before. The sweep before it reported 2 failures which were the ORACLE'S fault, and
  /// shipping that default would have filed a bug against correct behaviour.
  ///
  /// A clean sweep here also proves the oracle is not vacuous, which is unusual and worth saying:
  /// `EXPECT DIFFERENTFILE` fails when the undo changed nothing, so 1000 passes means 1000 undos
  /// that genuinely moved the document. Use `--no-undo-redo` to skip it.
  bool emitUndoRedo = true;
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
