#pragma once

// Delta-debugging minimizer for failing transcripts (REQ-204).
//
// The seed is not the bug artifact — the minimized transcript is. A seed stops reproducing the
// moment the generator changes, and the generator will change; a 6-line transcript keeps working
// and is something a person can read in one glance.
//
// PURE: the caller supplies a predicate that answers "does this candidate still fail the SAME way?".
// The algorithm never runs a process, opens a file, or knows what a transcript means, which is what
// lets the Catch2 suite verify termination and boundedness without a build of the application.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace minimizer {

/// Answers whether a candidate transcript still reproduces the original failure with the same
/// signature. Matching on the SIGNATURE and not merely on "it failed" is what stops minimization
/// sliding onto a different, easier bug — the standard failure mode of naive delta debugging, which
/// produces a tidy reproducer for a defect nobody was investigating.
using StillFailsFn = std::function<bool(const std::vector<std::string>& candidate)>;

struct Result {
  std::vector<std::string> lines;
  int attempts = 0;        ///< Candidates evaluated (each is one predicate call).
  size_t originalSize = 0;
  bool hitAttemptCap = false;  ///< True when the cap stopped it before it reached a local minimum.

  /// Fraction of lines removed, 0..1. Reported because a minimizer that quietly achieves nothing
  /// looks identical to one that works, right up until someone reads the "minimized" 200-line file.
  double reductionRatio() const {
    if (originalSize == 0)
      return 0.0;
    return 1.0 - static_cast<double>(lines.size()) / static_cast<double>(originalSize);
  }
};

struct Options {
  /// Hard ceiling on predicate calls. Each call is a process launch in real use, so this is the
  /// difference between a minute and an afternoon. Termination does not depend on it — the
  /// algorithm always converges — but a bound makes the worst case predictable.
  int maxAttempts = 400;

  /// Lines that are never removed, however irrelevant they look. Removing `NEW` would change what
  /// every following line means, so a candidate without it is not a smaller version of the same
  /// bug — it is a different transcript.
  bool keepLeadingNew = true;
};

/// Reduce \p lines to a locally minimal subset that still satisfies \p stillFails.
///
/// Assumes \p stillFails(lines) is already true; if it is not, the input is returned unchanged with
/// `attempts == 0`, because there is no failure to preserve and shrinking would be meaningless.
Result Minimize(const std::vector<std::string>& lines, const StillFailsFn& stillFails,
                const Options& opt = Options{});

}  // namespace minimizer
