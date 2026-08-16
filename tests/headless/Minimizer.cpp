#include "Minimizer.hpp"

#include <algorithm>

namespace minimizer {
namespace {

/// True when a line must survive every candidate regardless of relevance.
bool IsPinned(const std::string& line, const Options& opt) {
  if (!opt.keepLeadingNew)
    return false;
  return line == "NEW";
}

/// Build a candidate that omits [begin, end) of \p src, keeping pinned lines wherever they fall.
std::vector<std::string> WithChunkRemoved(const std::vector<std::string>& src, size_t begin,
                                          size_t end, const Options& opt) {
  std::vector<std::string> out;
  out.reserve(src.size());
  for (size_t i = 0; i < src.size(); ++i) {
    if (i >= begin && i < end && !IsPinned(src[i], opt))
      continue;
    out.push_back(src[i]);
  }
  return out;
}

}  // namespace

Result Minimize(const std::vector<std::string>& lines, const StillFailsFn& stillFails,
                const Options& opt) {
  Result res;
  res.lines = lines;
  res.originalSize = lines.size();

  if (!stillFails) {
    return res;
  }

  // Guard: the caller is supposed to hand us a transcript that already fails. If it does not, there
  // is nothing to preserve and every "successful" removal would be meaningless — so refuse rather
  // than emit a confidently minimized file describing no bug at all.
  ++res.attempts;
  if (!stillFails(res.lines)) {
    res.attempts = 0;
    return res;
  }

  // Classic ddmin over line ranges: try removing large chunks first, halve when a whole pass
  // achieves nothing, stop when single-line removals stop helping. Chunks first matters — most of a
  // generated transcript is irrelevant, and removing it one line at a time would cost a process
  // launch per line.
  size_t chunk = std::max<size_t>(res.lines.size() / 2, 1);
  while (true) {
    bool removedAny = false;

    for (size_t begin = 0; begin < res.lines.size();) {
      if (res.attempts >= opt.maxAttempts) {
        res.hitAttemptCap = true;
        return res;
      }

      const size_t end = std::min(begin + chunk, res.lines.size());
      std::vector<std::string> candidate = WithChunkRemoved(res.lines, begin, end, opt);

      // A candidate identical to the current best (every line in the range was pinned) tells us
      // nothing and would cost a process launch, so skip it without counting an attempt.
      if (candidate.size() == res.lines.size()) {
        begin = end;
        continue;
      }

      ++res.attempts;
      if (stillFails(candidate)) {
        res.lines = std::move(candidate);
        removedAny = true;
        // Do NOT advance `begin`: the lines that shifted into this position are unexamined, and
        // stepping past them is how a minimizer silently leaves half its work undone.
      } else {
        begin = end;
      }
    }

    if (!removedAny) {
      if (chunk == 1)
        break;  // local minimum: no single line can be dropped
      chunk = std::max<size_t>(chunk / 2, 1);
    }
  }

  return res;
}

}  // namespace minimizer
