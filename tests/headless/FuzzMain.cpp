// `gosurvey_headless fuzz` — the REQ-204 fuzz loop.
//
// Generate a transcript from a seed, run it, and if it fails, minimize it to the shortest
// transcript that still fails THE SAME WAY, then write that out as the artifact a bug report
// carries.
//
// **Every candidate runs as a SUBPROCESS**, not in-process. That is the load-bearing decision here:
//
//  - A crash is the outcome most worth finding, and an in-process harness dies with it — taking the
//    minimizer, the report and the remaining seeds with it. A child process just exits abnormally
//    and the parent carries on.
//  - A hang is the one failure a harness cannot triage from the inside. `RunProcessAndWait` already
//    takes a timeout and terminates the child, so hang detection costs nothing extra.
//  - Each candidate starts from a genuinely fresh process, so nothing leaks between runs and a
//    reproducer that "only fails as the 40th candidate" cannot exist.
//
// The cost is a process launch per candidate, which is what the chunked minimizer in Minimizer.cpp
// is designed around.

#include "FuzzGenerator.hpp"
#include "Minimizer.hpp"

#include "CadCommands.hpp"
#include "ProcessRun.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

/// Enumerate the command registry through the only public door there is.
///
/// `kRegistry` lives in an anonymous namespace inside CadCommands.cpp, and exposing it would be a
/// public-API change — an architectural decision, which the Workshop does not get to make on its
/// own (CLAUDE.md §3). `FuzzyCommandSuggestions` is already public and already returns name +
/// description, so it is the door that exists.
///
/// A single-letter query is a subsequence match (FuzzySubsequenceScore returns >= 0 whenever every
/// query character appears in order), so 'a'..'z' unioned covers every command whose name contains
/// any ASCII letter — which is all of them. Asking for a large maxResults per letter keeps the
/// per-query ranking cap from hiding any.
std::vector<std::string> EnumerateCommands() {
  std::set<std::string> uniq;
  for (char c = 'a'; c <= 'z'; ++c) {
    const std::string q(1, c);
    for (const CommandSuggestion& s : FuzzyCommandSuggestions(q, 500))
      uniq.insert(s.name);
  }
  return std::vector<std::string>(uniq.begin(), uniq.end());
}

bool WriteLines(const fs::path& p, const std::vector<std::string>& lines) {
  std::ofstream f(p, std::ios::binary);
  if (!f)
    return false;
  for (const std::string& l : lines)
    f << l << "\n";
  return true;
}

std::string ReadFirstLine(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return std::string();
  std::string s;
  std::getline(f, s);
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
  return s;
}

/// Outcome of running one candidate transcript.
struct RunOutcome {
  bool passed = false;
  std::string signature;  ///< "pass", "invariant|<id>", "expect", "crash:<code>", "hang", ...
};

class Runner {
 public:
  Runner(std::string exePath, fs::path workDir, int timeoutMs)
      : exePath_(std::move(exePath)), workDir_(std::move(workDir)), timeoutMs_(timeoutMs) {}

  RunOutcome Run(const std::vector<std::string>& lines, const fs::path& transcriptPath,
                 const fs::path& outDir) {
    RunOutcome o;
    if (!WriteLines(transcriptPath, lines)) {
      o.signature = "harness:write-failed";
      return o;
    }

    const fs::path sigPath = transcriptPath.string() + ".sig";
    std::error_code ec;
    fs::remove(sigPath, ec);  // so a stale signature can never be mistaken for this run's

    // **Every candidate gets an EMPTY output directory.** Without this the harness produces
    // confident nonsense: candidates share `%OUT%`, so a candidate that drops the SAVEAS lines
    // still finds the previous candidate's files on disk, `EXPECT SAMEFILE` still compares them,
    // and the failure still reproduces — for the wrong reason. Observed, not hypothetical: the first
    // version of this minimizer reduced a 116-line transcript to
    //     NEW
    //     EXPECT SAMEFILE %OUT%/rt-a.dwg %OUT%/rt-b.dwg
    // and reported 98% reduction, which is a perfect score for a reproducer describing no bug at
    // all. State leaking between candidates is the way a minimizer lies to you.
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);

    int exitCode = 0;
    const bool finished = RunProcessAndWait(
        exePath_,
        {"run", transcriptPath.string(), "--out", outDir.string(), "--sig", sigPath.string()},
        workDir_.string(), timeoutMs_, &exitCode);

    if (!finished) {
      // Timed out and was terminated. REQ-204 counts this as a failure: a command that never
      // returns is a defect even though nothing crashed and no invariant fired.
      o.signature = "hang";
      return o;
    }

    if (exitCode == 0) {
      o.passed = true;
      o.signature = "pass";
      return o;
    }
    if (exitCode == 1) {
      // Clean failure: the child reported which one.
      const std::string sig = ReadFirstLine(sigPath);
      o.signature = sig.empty() ? "unknown" : sig;
      return o;
    }

    // Anything else is abnormal termination — an access violation, an assert, an abort. This is the
    // class the subprocess design exists for, and the exit code is the only thing the parent can
    // learn about it without a debugger, so it goes straight into the signature.
    char buf[64];
    std::snprintf(buf, sizeof buf, "crash:%d", exitCode);
    o.signature = buf;
    return o;
  }

 private:
  std::string exePath_;
  fs::path workDir_;
  int timeoutMs_;
};

int Usage() {
  std::fprintf(stderr,
               "usage: gosurvey_headless fuzz [--seed N | --seeds A..B] [--out <dir>]\n"
               "                              [--timeout-ms N] [--max-attempts N]\n"
               "                              [--roundtrip | --no-roundtrip]\n"
               "                              [--undo-redo | --no-undo-redo]\n"
               "\n"
               "  the gs-roundtrip oracle is ON by default since 2026-08-17; --roundtrip is still\n"
               "  accepted as a no-op, --no-roundtrip skips it\n"
               "  the undo-redo-identity oracle is ON by default since 2026-08-18; --undo-redo is\n"
               "  accepted as a no-op, --no-undo-redo skips it\n");
  return 2;
}

}  // namespace

int FuzzMain(int argc, char** argv, const char* exePath) {
  std::uint64_t seedBegin = 0;
  std::uint64_t seedEnd = 0;  // inclusive
  bool haveSeed = false;
  fs::path outDir;
  int timeoutMs = 20000;
  int maxAttempts = 400;
  fuzzgen::Options gopt;

  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--seed" && i + 1 < argc) {
      seedBegin = seedEnd = std::strtoull(argv[++i], nullptr, 10);
      haveSeed = true;
    } else if (a == "--seeds" && i + 1 < argc) {
      const std::string r = argv[++i];
      const size_t dots = r.find("..");
      if (dots == std::string::npos)
        return Usage();
      seedBegin = std::strtoull(r.substr(0, dots).c_str(), nullptr, 10);
      seedEnd = std::strtoull(r.substr(dots + 2).c_str(), nullptr, 10);
      haveSeed = true;
    } else if (a == "--out" && i + 1 < argc) {
      outDir = argv[++i];
    } else if (a == "--timeout-ms" && i + 1 < argc) {
      timeoutMs = std::atoi(argv[++i]);
    } else if (a == "--max-attempts" && i + 1 < argc) {
      maxAttempts = std::atoi(argv[++i]);
    } else if (a == "--roundtrip") {
      gopt.emitRoundTrip = true;  // now the default; still accepted so existing scripts and CI keep working
    } else if (a == "--no-roundtrip") {
      gopt.emitRoundTrip = false;
    } else if (a == "--undo-redo") {
      gopt.emitUndoRedo = true;
    } else if (a == "--no-undo-redo") {
      gopt.emitUndoRedo = false;
    } else {
      return Usage();
    }
  }
  if (!haveSeed || seedEnd < seedBegin)
    return Usage();

  if (outDir.empty())
    outDir = fs::temp_directory_path() / "gosurvey_fuzz";
  std::error_code ec;
  fs::create_directories(outDir, ec);

  const fs::path workDir = fs::current_path();
  const std::vector<std::string> all = EnumerateCommands();
  const std::vector<std::string> commands = fuzzgen::FilterCommands(all);
  if (commands.empty()) {
    std::fprintf(stderr, "fuzz: no commands enumerated — refusing to run a fuzzer with no alphabet\n");
    return 2;
  }

  Runner runner(exePath, workDir, timeoutMs);

  int failures = 0;
  int filed = 0;
  std::set<std::string> seenSignatures;  // in-run dedupe; the cross-run one is `gh issue list`

  for (std::uint64_t seed = seedBegin; seed <= seedEnd; ++seed) {
    const std::vector<std::string> lines = fuzzgen::Generate(seed, commands, gopt);

    const fs::path seedDir = outDir / ("seed-" + std::to_string(seed));
    fs::create_directories(seedDir, ec);
    const fs::path transcript = seedDir / "generated.txt";

    const RunOutcome first = runner.Run(lines, transcript, seedDir / "run");
    if (first.passed)
      continue;

    ++failures;
    std::fprintf(stderr, "seed %llu: FAIL [%s]\n", static_cast<unsigned long long>(seed),
                 first.signature.c_str());

    // Same defect as one already reported in this run: record the seed and move on rather than
    // paying for a full minimization to produce a second copy of the same reproducer.
    if (!seenSignatures.insert(first.signature).second) {
      std::fprintf(stderr, "  duplicate of a signature already minimized this run — skipping\n");
      continue;
    }

    // --- Minimize -----------------------------------------------------------------------------
    const fs::path candPath = seedDir / "candidate.txt";
    const fs::path candOut = seedDir / "cand-out";
    int candidateIndex = 0;
    const std::string wanted = first.signature;

    minimizer::Options mopt;
    mopt.maxAttempts = maxAttempts;

    const minimizer::Result m = minimizer::Minimize(
        lines,
        [&](const std::vector<std::string>& candidate) {
          ++candidateIndex;
          return runner.Run(candidate, candPath, candOut).signature == wanted;
        },
        mopt);

    const fs::path minPath = seedDir / "minimized.txt";
    std::vector<std::string> out;
    out.push_back("# Minimized reproducer — seed " + std::to_string(seed));
    out.push_back("# Signature: " + wanted);
    out.push_back("# " + std::to_string(m.originalSize) + " lines -> " +
                  std::to_string(m.lines.size()) + " (" +
                  std::to_string(static_cast<int>(m.reductionRatio() * 100.0)) + "% removed, " +
                  std::to_string(m.attempts) + " candidates" +
                  (m.hitAttemptCap ? ", HIT ATTEMPT CAP" : "") + ")");
    out.insert(out.end(), m.lines.begin(), m.lines.end());
    WriteLines(minPath, out);

    std::fprintf(stderr, "  minimized %zu -> %zu lines (%d%% removed, %d candidates%s)\n",
                 m.originalSize, m.lines.size(), static_cast<int>(m.reductionRatio() * 100.0),
                 m.attempts, m.hitAttemptCap ? ", HIT CAP" : "");
    std::fprintf(stderr, "  reproducer: %s\n", minPath.string().c_str());
    ++filed;
  }

  const std::uint64_t n = seedEnd - seedBegin + 1;
  // A clean run prints a summary and nothing else (REQ-204 acceptance). Noise per seed would make a
  // 10,000-seed run unreadable, and unreadable output is the same as no output.
  std::fprintf(stdout, "fuzz: %llu seed(s), %d command(s) in the alphabet (%zu denied), %d failure(s), %d minimized\n",
               static_cast<unsigned long long>(n), static_cast<int>(commands.size()),
               all.size() - commands.size(), failures, filed);

  return failures == 0 ? 0 : 1;
}
