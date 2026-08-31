#include "FuzzGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>

namespace fuzzgen {
namespace {

// ---------------------------------------------------------------------------
// The denylist. Each entry costs coverage, so each one states what it would otherwise do.
// ---------------------------------------------------------------------------
const char* const kDenied[] = {
    // Runs a frame-budget benchmark that builds up to millions of triangles. Minutes per call, and
    // it measures rather than mutates — nothing to find, everything to wait for (REQ-100).
    "bench",
    // Spawn an external DWG converter process or read a model file. The converter is a third-party
    // executable; fuzzing it is neither in scope nor ours to fix, and a missing one just times out.
    "importmodel", "gltf", "import3d", "dwgin", "dwgout",
    // Rasterise or plot to PDF. Headless links the PdfAttach seam, which refuses by design
    // (ADR-031 (b′)), so these can only ever exercise the refusal path.
    "plot", "batchplot", "pdfattach", "pdfin",
    // End the session. A fuzz run that quits on action 3 tests nothing.
    "quit", "exit", "close", "new", "open",
};

bool EqualsAscii(const std::string& a, const char* b) {
  size_t i = 0;
  for (; i < a.size(); ++i) {
    if (b[i] == '\0')
      return false;
    char x = a[i];
    if (x >= 'A' && x <= 'Z')
      x = static_cast<char>(x - 'A' + 'a');
    char y = b[i];
    if (y >= 'A' && y <= 'Z')
      y = static_cast<char>(y - 'A' + 'a');
    if (x != y)
      return false;
  }
  return b[i] == '\0';
}

/// Commands that consume a sequence of points and want a terminator.
bool IsPointCommand(const std::string& n) {
  static const char* const kPointy[] = {"line",       "polyline", "rect",      "arc",
                                        "ellipse",    "circle",   "hatch",     "dimaligned",
                                        "dimlinear",  "dimangular", "offset",  "trim",
                                        "move",       "copy",     "rotate",    "scale",
                                        "align",      "join",     "idpoint",   "surveyinverse"};
  for (const char* p : kPointy)
    if (EqualsAscii(n, p))
      return true;
  return false;
}

/// Commands whose natural terminator is END rather than ESC (POLYLINE commits on END).
bool WantsEndTerminator(const std::string& n) {
  return EqualsAscii(n, "polyline");
}

/// How often a command should be chosen, relative to 1 for anything unlisted. Drawing and modifying
/// is where state changes, so that is where the budget goes; a command that only opens a panel is
/// shallow headless by construction and gets no boost.
int WeightFor(const std::string& n) {
  if (EqualsAscii(n, "line") || EqualsAscii(n, "polyline") || EqualsAscii(n, "circle") ||
      EqualsAscii(n, "rect") || EqualsAscii(n, "arc") || EqualsAscii(n, "ellipse"))
    return 8;
  if (EqualsAscii(n, "move") || EqualsAscii(n, "copy") || EqualsAscii(n, "rotate") ||
      EqualsAscii(n, "scale") || EqualsAscii(n, "erase") || EqualsAscii(n, "delete") ||
      EqualsAscii(n, "trim") || EqualsAscii(n, "offset") || EqualsAscii(n, "join"))
    return 5;
  if (EqualsAscii(n, "text") || EqualsAscii(n, "mtext") || EqualsAscii(n, "hatch"))
    return 3;
  return 1;
}

struct Rng {
  std::mt19937_64 g;
  explicit Rng(std::uint64_t seed) : g(seed) {}

  /// Uniform in [lo, hi].
  int Int(int lo, int hi) {
    if (hi <= lo)
      return lo;
    return lo + static_cast<int>(g() % static_cast<std::uint64_t>(hi - lo + 1));
  }
  double Unit() {
    // 53-bit mantissa's worth of bits, so the value does not depend on the platform's double
    // distribution implementation — determinism matters more here than distribution quality.
    return static_cast<double>(g() >> 11) / 9007199254740992.0;
  }
  bool Chance(double p) { return Unit() < p; }
};

std::string Fmt(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.6g", v);
  return buf;
}

/// The hostile ladder. Every rung is a value that has broken CAD software before: the huge ones are
/// state-plane magnitudes (this project has been bitten by them), the tiny ones are denormals that
/// make normalisation divide by ~zero, and NaN/inf are what degenerate geometry produces on the way
/// to being stored.
double HostileScalar(Rng& r) {
  switch (r.Int(0, 9)) {
  case 0: return 0.0;
  case 1: return -0.0;
  case 2: return 1.0e12;             // state-plane easting
  case 3: return -1.0e12;
  case 4: return 1.0e-38;            // near-denormal
  case 5: return 1.0e38;             // near float max
  case 6: return std::nan("");
  case 7: return std::numeric_limits<double>::infinity();
  case 8: return -std::numeric_limits<double>::infinity();
  default: return 1.0e7;
  }
}

double PlainScalar(Rng& r) {
  return static_cast<double>(r.Int(-500, 500)) + (r.Chance(0.5) ? 0.25 : 0.0);
}

std::string Point(Rng& r, const Options& opt) {
  const bool hostile = r.Chance(opt.hostileChance);
  const double x = hostile ? HostileScalar(r) : PlainScalar(r);
  const double y = hostile ? HostileScalar(r) : PlainScalar(r);
  if (r.Chance(0.15))
    return "@" + Fmt(x) + "," + Fmt(y);  // relative point
  return Fmt(x) + "," + Fmt(y);
}

}  // namespace

bool IsDeniedCommand(const std::string& lowerName) {
  for (const char* d : kDenied)
    if (EqualsAscii(lowerName, d))
      return true;
  return false;
}

std::vector<std::string> FilterCommands(const std::vector<std::string>& all) {
  std::vector<std::string> keep;
  keep.reserve(all.size());
  for (const std::string& c : all)
    if (!IsDeniedCommand(c))
      keep.push_back(c);
  return keep;
}

std::vector<std::string> Generate(std::uint64_t seed, const std::vector<std::string>& commands,
                                  const Options& opt) {
  Rng r(seed);

  // Build the weighted pick table once, so the weighting costs nothing per draw and — more
  // importantly — so the sequence of rng() calls does not depend on how the table is searched.
  std::vector<size_t> bag;
  for (size_t i = 0; i < commands.size(); ++i) {
    const int w = WeightFor(commands[i]);
    for (int k = 0; k < w; ++k)
      bag.push_back(i);
  }

  std::vector<std::string> lines;
  lines.push_back("# gosurvey_headless fuzz --seed " + std::to_string(seed));
  lines.push_back("# Generated. Do not edit; regenerate from the seed, or use the minimized form.");
  lines.push_back("NEW");

  // --- Prelude: build a real drawing before anything random happens -------------------------
  //
  // Measured, not assumed: without this, a 50-line generated transcript produced ONE entity. Nearly
  // every modify command (MOVE, ROTATE, TRIM, OFFSET, ERASE, JOIN) is a no-op with nothing selected
  // and nothing to select, so the fuzzer spent its whole budget bouncing off commands that declined
  // to do anything. A fuzzer that reaches no interesting state finds nothing and still looks like it
  // is working, which is the most expensive way to be wrong.
  //
  // Fixed rather than random, and identical for every seed: it is scaffolding, not subject matter.
  // Keeping it constant means a minimized reproducer's prelude lines are the same lines every time,
  // so they are easy to recognise and — when irrelevant — easy for the minimizer to delete.
  if (opt.emitPrelude) {
    const char* const kPrelude[] = {
        "CMD LINE",   "CMD 0,0",     "CMD 100,0",  "CMD 100,80", "CMD 0,80",   "CMD 0,0",   "ESC",
        "CMD CIRCLE", "CMD 50,40",   "CMD 25",     "ESC",
        "CMD CIRCLE", "CMD 150,40",  "CMD 30",     "ESC",
        "CMD POLYLINE", "CMD 0,120", "CMD 60,160", "CMD 120,120", "CMD 180,160", "CMD END",
        "CMD TEXT",   "CMD 10,200",  "CMD 5",      "CMD 0",      "CMD FUZZ",   "ESC",
    };
    for (const char* p : kPrelude)
      lines.emplace_back(p);
  }

  const int actions = r.Int(opt.minActions, opt.maxActions);
  for (int a = 0; a < actions; ++a) {
    const int roll = r.Int(0, 99);

    if (roll < 62 && !bag.empty()) {
      const std::string& name = commands[bag[static_cast<size_t>(r.Int(0, static_cast<int>(bag.size()) - 1))]];
      lines.push_back("CMD " + name);

      if (IsPointCommand(name)) {
        const int pts = r.Int(1, 5);
        for (int p = 0; p < pts; ++p)
          lines.push_back("CMD " + Point(r, opt));

        if (r.Chance(opt.abandonChance)) {
          // Walk away mid-command. The next action runs with this one still live, which is exactly
          // how a real session gets into a state nobody designed for.
        } else if (WantsEndTerminator(name)) {
          lines.push_back("CMD END");
        } else {
          lines.push_back("ESC");
        }
      } else {
        // Not a point command: feed it a scalar or a word and then leave. Some will reject the
        // input, which is fine — REQ-201 says they must say so, and `command-logged` checks it.
        if (r.Chance(0.6))
          lines.push_back("CMD " + Fmt(r.Chance(opt.hostileChance) ? HostileScalar(r)
                                                                  : PlainScalar(r)));
        if (r.Chance(0.5))
          lines.push_back("ESC");
      }
    } else if (roll < 72) {
      lines.push_back("ESC");
    } else if (roll < 84) {
      lines.push_back("UNDO");
    } else if (roll < 92) {
      lines.push_back("REDO");
    } else if (roll < 98) {
      // Two thirds of picks aim AT the prelude geometry rather than into empty space. A pick that
      // hits nothing clears the selection, so purely random picks kept the drawing permanently
      // unselected and every modify command downstream inert — the same yield problem the prelude
      // fixes, one level up.
      std::string px;
      std::string py;
      if (opt.emitPrelude && r.Chance(0.66)) {
        static const double kOn[][2] = {
            {50.0, 0.0},    // midpoint of the bottom line
            {100.0, 40.0},  // right edge of the box
            {50.0, 15.0},   // on the first circle's rim
            {150.0, 10.0},  // on the second circle's rim
            {60.0, 160.0},  // a polyline vertex
            {120.0, 120.0}, // another polyline vertex
            {10.0, 200.0},  // the text insertion point
        };
        const int k = r.Int(0, static_cast<int>(std::size(kOn)) - 1);
        px = Fmt(kOn[k][0]);
        py = Fmt(kOn[k][1]);
      } else {
        px = Fmt(PlainScalar(r));
        py = Fmt(PlainScalar(r));
      }
      lines.push_back("PICK " + px + " " + py + (r.Chance(0.2) ? " SUBTRACT" : ""));
    } else {
      lines.push_back("CHECK ALL");
    }
  }

  lines.push_back("CHECK ALL");

  if (opt.emitUndoRedo) {
    // The ANCHOR EDIT is not decoration — it is what stops this oracle from lying, and the first
    // version of it did lie. Measured on seed 260 of the first 1000-seed sweep:
    //
    //   UNDO at the BOTTOM of the undo stack is a no-op, and REDO afterwards is NOT.
    //
    // A fuzzed transcript issues undos at random, so it frequently arrives here with the stack
    // already exhausted and a redo stack that is not. `SAVEAS` / `UNDO` / `REDO` / `SAVEAS` then
    // moves the document FORWARD by one operation and the two files differ — which is correct
    // editor behaviour (AutoCAD does the same) reported as a defect. That is a false-positive
    // oracle, and docs/fuzz-harness.md §9 is explicit that a false positive is worse than no
    // oracle: it files garbage and teaches everyone to ignore the harness.
    //
    // Committing one known entity immediately before the save fixes it at the root rather than by
    // tolerance. It guarantees the undo stack is non-empty and that its top entry is exactly this
    // edit, so UNDO must remove it and REDO must put it back. The commit also clears the redo
    // stack, so REDO cannot reach anything older.
    //
    // The TYPE varies with the seed but the COORDINATES do not, and the split is deliberate. Each
    // entity type has its own snapshot and restore path, which is where "an edit not fully captured
    // by the snapshot" actually lives, so testing only LINE would leave most of the defect class
    // unreached. Coordinates stay plain because the anchor's whole job is to be a GUARANTEED commit:
    // a hostile value that the command legitimately refuses would leave the undo stack untouched and
    // put the false positive straight back.
    const int anchor = r.Int(0, 3);
    const double ax = PlainScalar(r);
    const double ay = PlainScalar(r);
    if (anchor == 0) {
      lines.push_back("CMD LINE");
      lines.push_back("CMD " + Fmt(ax) + "," + Fmt(ay));
      lines.push_back("CMD " + Fmt(ax + 100.0) + "," + Fmt(ay));
      lines.push_back("ESC");
    } else if (anchor == 1) {
      lines.push_back("CMD CIRCLE");
      lines.push_back("CMD " + Fmt(ax) + "," + Fmt(ay));
      lines.push_back("CMD 12.5");
      lines.push_back("ESC");
    } else if (anchor == 2) {
      lines.push_back("CMD POLYLINE");
      lines.push_back("CMD " + Fmt(ax) + "," + Fmt(ay));
      lines.push_back("CMD " + Fmt(ax + 40.0) + "," + Fmt(ay + 30.0));
      lines.push_back("CMD " + Fmt(ax + 80.0) + "," + Fmt(ay));
      lines.push_back("CMD END");
    } else {
      lines.push_back("CMD TEXT");
      lines.push_back("CMD " + Fmt(ax) + "," + Fmt(ay));
      lines.push_back("CMD 2.5");
      lines.push_back("CMD 0");
      lines.push_back("CMD ANCHOR");
      lines.push_back("ESC");
    }

    // Distinct filenames from the round-trip block below, and never reused between the two oracles.
    // docs/fuzz-harness.md §8 records why: candidates that share an output path let a minimized
    // transcript "fail" on a file the previous candidate left behind, which is how a minimizer
    // produces a confident lie. Two oracles in one transcript are the same hazard at a smaller
    // scale.
    lines.push_back("SAVEAS %OUT%/ur-a.dwg");
    lines.push_back("UNDO");
    lines.push_back("CHECK ALL");
    lines.push_back("SAVEAS %OUT%/ur-mid.dwg");

    // Two-sided: prove the document MOVED before proving it came back. Without this line the whole
    // block passes on a no-op undo, which is exactly how the first version of this oracle managed
    // to be both green and meaningless.
    lines.push_back("EXPECT DIFFERENTFILE %OUT%/ur-a.dwg %OUT%/ur-mid.dwg");

    lines.push_back("REDO");
    lines.push_back("CHECK ALL");
    lines.push_back("SAVEAS %OUT%/ur-b.dwg");
    lines.push_back("EXPECT SAMEFILE %OUT%/ur-a.dwg %OUT%/ur-b.dwg");
  }

  if (opt.emitRoundTrip) {
    // TWO round trips, and the comparison is B vs C rather than A vs B (REQ-079 as amended
    // 2026-08-17, decision D-2026-08-17-a). Loading a drawing whose coordinates are of state-plane
    // magnitude NORMALIZES it — the document origin moves to the extents midpoint and every local
    // coordinate rebases — so the first resave legitimately differs from the file that was read.
    // That normalization is idempotent: the second load finds the origin already set and does
    // nothing, so B and C must match exactly. Comparing B to C therefore still catches every defect
    // A-vs-B caught (a field written but not read, or read but not written) while no longer failing
    // on the one transformation the format is entitled to perform. Issue #61.
    lines.push_back("SAVEAS %OUT%/rt-a.dwg");
    lines.push_back("OPEN %OUT%/rt-a.dwg");
    lines.push_back("CHECK ALL");
    lines.push_back("SAVEAS %OUT%/rt-b.dwg");
    lines.push_back("OPEN %OUT%/rt-b.dwg");
    lines.push_back("CHECK ALL");
    lines.push_back("SAVEAS %OUT%/rt-c.dwg");
    lines.push_back("EXPECT SAMEFILE %OUT%/rt-b.dwg %OUT%/rt-c.dwg");
  }

  return lines;
}

}  // namespace fuzzgen
