// gosurvey_headless — the REQ-203 transcript driver.
//
// Executes a transcript (a text file of command-line submissions and viewport picks) against a real
// AppCommandState, with no window, no GPU and no display, and reports what the drawing became.
// The grammar is documented in docs/fuzz-harness.md §2.
//
// Two things about this program are deliberate and easy to undo by accident:
//
//  1. **It drives the SAME entry points the GUI drives** — ProcessCommandLineSubmit and
//     SubmitViewportPick — rather than calling command internals. A driver that reached past the
//     command line would test a path no user can take, and would keep passing after the real one
//     broke.
//  2. **It creates an ImGui context and loads the application's font** (ADR-031 (c′)). Loading a
//     `.gs` measures survey label text and stores the result as geometry, so a driver without a
//     font would silently produce different geometry from the GUI — which is exactly the class of
//     difference REQ-203's "save a .gs and diff" condition exists to detect.

#include "CadCommands.hpp"
#include "DxfIo.hpp"
#include "GsIo.hpp"
#include "HeadlessFileDialogs.hpp"
#include "docinvariants.hpp"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Small text helpers. Deliberately local: a transcript is a test fixture, not a file format
// (docs/fuzz-harness.md §2), so its parsing has no business growing a shared utility.
// ---------------------------------------------------------------------------

std::string Trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r'))
    ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
    --e;
  return s.substr(b, e - b);
}

std::string UpperAscii(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 'A');
  return s;
}

/// Split off the first whitespace-delimited word, returning it and leaving the remainder in \p rest.
std::string FirstWord(const std::string& line, std::string* rest) {
  const size_t sp = line.find_first_of(" \t");
  if (sp == std::string::npos) {
    *rest = std::string();
    return line;
  }
  *rest = Trim(line.substr(sp + 1));
  return line.substr(0, sp);
}

/// `userPolylineOffsets` is CSR: polyline i spans [offsets[i], offsets[i+1]), so N polylines need
/// N+1 offsets. Counting the array directly is off by one — see docinvariants.cpp for the same note.
size_t PolylineCountOf(const AppCommandState& st) {
  return st.userPolylineOffsets.empty() ? 0 : st.userPolylineOffsets.size() - 1;
}

/// Escape a string for a JSON scalar. Small on purpose — the driver emits JSON, it does not parse it.
std::string JsonEscape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"':  o += "\\\""; break;
    case '\\': o += "\\\\"; break;
    case '\n': o += "\\n";  break;
    case '\r': o += "\\r";  break;
    case '\t': o += "\\t";  break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", c);
        o += buf;
      } else {
        o += c;
      }
    }
  }
  return o;
}

// ---------------------------------------------------------------------------
// Run state
// ---------------------------------------------------------------------------

struct Failure {
  std::string reason;   ///< "invariant" | "expect" | "parse" | "io"
  std::string detail;
  int stepIndex = -1;   ///< 0-based index of the executed step
  int sourceLine = -1;  ///< 1-based line in the transcript
};

struct Run {
  AppCommandState st;
  std::vector<std::string> log;
  std::filesystem::path outDir;
  int stepIndex = 0;
  bool checkEveryStep = true;
  std::vector<Failure> failures;

  /// Log length before the current step, so a step's own output can be isolated (REQ-201 checks).
  size_t logMarkBeforeStep = 0;
};

/// Expand %OUT% to the run's temp directory. Transcripts must never write into the source tree
/// (CON-07 / REQ-200), and a fuzz run writes a lot of files.
std::string ExpandVars(const Run& run, const std::string& s) {
  const std::string token = "%OUT%";
  std::string o = s;
  for (size_t p = o.find(token); p != std::string::npos; p = o.find(token, p)) {
    const std::string rep = run.outDir.string();
    o.replace(p, token.size(), rep);
    p += rep.size();
  }
  return o;
}

void Fail(Run& run, const char* reason, std::string detail, int sourceLine) {
  run.failures.push_back(Failure{reason, std::move(detail), run.stepIndex, sourceLine});
}

/// A signature that identifies WHICH DEFECT this is, stable across minimization.
///
/// Deliberately excludes the step index, the source line, and the numbers inside the detail string:
/// every one of those changes when a line is removed, and a signature that changes under
/// minimization makes the minimizer chase its own tail — each shrink looks like a different bug, so
/// nothing is ever removed. What remains is the failure class plus, for an invariant, its stable id.
std::string FailureSignature(const Run& run) {
  if (run.failures.empty())
    return "pass";
  const Failure& f = run.failures.front();

  // Details are written as "<kind>: <specifics>" — an invariant id, or the EXPECT check that fired.
  // Keep the kind, drop the specifics: the kind identifies the defect and survives minimization,
  // while the specifics carry indices and byte offsets that shift as lines are removed.
  //
  // The reason alone is too coarse to deduplicate on. Every EXPECT failure would share the
  // signature "expect", so two unrelated defects found in one run would look like one, and the
  // second would be silently discarded as a duplicate — a fuzzer losing findings without saying so.
  const size_t colon = f.detail.find(':');
  if (colon != std::string::npos && colon > 0)
    return f.reason + "|" + f.detail.substr(0, colon);
  return f.reason;
}

/// Run the invariant set and record every violation as a failure.
void CheckInvariants(Run& run, int sourceLine) {
  std::vector<InvariantViolation> v;
  CheckDocumentInvariants(run.st, &v);
  for (const InvariantViolation& iv : v)
    Fail(run, "invariant", std::string(iv.name) + ": " + iv.detail, sourceLine);
}

/// What the application's frame loop does to the document between user actions, minus the drawing.
/// EnsureEntityIds is the load-bearing part: main.cpp calls it every frame (main.cpp:518) before
/// anything can save or reference an entity, so a driver that skipped it would present every
/// freshly created entity as id-less and diverge from the GUI on the first save.
void TickFrame(Run& run) {
  EnsureEntityIds(run.st);
}

// ---------------------------------------------------------------------------
// Step execution
// ---------------------------------------------------------------------------

bool ExecuteStep(Run& run, const std::string& raw, int sourceLine) {
  std::string rest;
  const std::string verbRaw = FirstWord(raw, &rest);
  const std::string verb = UpperAscii(verbRaw);

  run.logMarkBeforeStep = run.log.size();

  if (verb == "NEW") {
    run.st = AppCommandState{};
    run.log.push_back("[driver] NEW");
  } else if (verb == "OPEN") {
    const std::string path = ExpandVars(run, rest);
    if (!LoadGoSurveyFile(run.st, path.c_str(), run.log)) {
      Fail(run, "io", "OPEN failed: " + path, sourceLine);
      return false;
    }
  } else if (verb == "SAVEAS") {
    const std::string path = ExpandVars(run, rest);
    if (!SaveGoSurveyFile(run.st, path.c_str(), run.log)) {
      Fail(run, "io", "SAVEAS failed: " + path, sourceLine);
      return false;
    }
  } else if (verb == "EXPORT" || verb == "IMPORT") {
    // EXPORT <FORMAT> <path> / IMPORT <FORMAT> <path> — the interchange formats, as distinct from
    // OPEN/SAVEAS which are the drawing's own `.gs`. Two words rather than an `EXPORTDXF` verb so
    // the parser-fuzzing stage can add GLTF and STL without inventing a verb each
    // (docs/fuzz-harness.md §8 stage 6).
    //
    // These are what make REQ-204's `dxf-export-stable` oracle expressible in a transcript, and
    // therefore reachable by the minimizer, which is the property that decides whether a finding
    // arrives as a reproducer or as a seed number.
    std::string pathRaw;
    const std::string fmt = UpperAscii(FirstWord(rest, &pathRaw));
    const std::string path = ExpandVars(run, Trim(pathRaw));
    if (path.empty()) {
      Fail(run, "parse", verb + " " + fmt + " needs a path", sourceLine);
      return false;
    }
    if (fmt != "DXF") {
      Fail(run, "parse", verb + ": unsupported format " + fmt + " (expected DXF)", sourceLine);
      return false;
    }
    const bool ok = (verb == "EXPORT") ? ExportDxfFile(run.st, path.c_str(), run.log)
                                       : ImportDxfFile(run.st, path.c_str(), run.log);
    if (!ok) {
      Fail(run, "io", verb + " " + fmt + " failed: " + path, sourceLine);
      return false;
    }
  } else if (verb == "DIALOG") {
    std::string arg;
    const std::string kind = UpperAscii(FirstWord(rest, &arg));
    if (kind == "CANCEL") {
      headless::QueueDialogCancel();
    } else if (kind == "OPEN" || kind == "SAVE") {
      headless::QueueDialogAnswer(ExpandVars(run, arg));
    } else {
      Fail(run, "parse", "DIALOG expects OPEN <path> | SAVE <path> | CANCEL, got: " + rest,
           sourceLine);
      return false;
    }
  } else if (verb == "CMD") {
    // `CMD` with no argument is a bare Enter, which is how half the commands terminate — an empty
    // argument is meaningful here, never a no-op.
    char buf[1024];
    const std::string text = ExpandVars(run, rest);
    if (text.size() + 1 > sizeof buf) {
      Fail(run, "parse", "CMD argument longer than the command buffer", sourceLine);
      return false;
    }
    std::memcpy(buf, text.c_str(), text.size() + 1);
    ProcessCommandLineSubmit(buf, static_cast<int>(sizeof buf), run.st, run.log);
  } else if (verb == "PICK") {
    std::istringstream is(rest);
    float x = 0.f;
    float y = 0.f;
    if (!(is >> x >> y)) {
      Fail(run, "parse", "PICK expects two world coordinates, got: " + rest, sourceLine);
      return false;
    }
    // The two optional flags are SubmitViewportPick's own parameters, named after what they do
    // rather than after the keys that happen to produce them in the GUI — a transcript saying
    // "SHIFT" would be asserting a key binding, which is not what the driver controls.
    std::string mod;
    bool windowSelectionSubtract = false;
    bool fenceLeftToRightWindowMode = false;
    while (is >> mod) {
      const std::string m = UpperAscii(mod);
      if (m == "SUBTRACT")
        windowSelectionSubtract = true;
      else if (m == "CROSSING")
        fenceLeftToRightWindowMode = true;
      else {
        Fail(run, "parse", "PICK: unknown modifier " + mod + " (expected SUBTRACT or CROSSING)",
             sourceLine);
        return false;
      }
    }
    SubmitViewportPick(run.st, x, y, run.log, windowSelectionSubtract, fenceLeftToRightWindowMode);
  } else if (verb == "BOX") {
    // BOX <x0> <y0> <x1> <y1> [WINDOW] [SUBTRACT] — a box selection from two world corners.
    //
    // PICK alone cannot express one: the FIRST corner is armed by the viewport's mouse handler
    // (BeginSelectionBoxCorner), not by SubmitViewportPick, so a transcript that picked twice
    // would arm nothing and then close a box that was never opened. Arming here writes the two
    // public draft fields the viewport writes and hands the second corner to the same
    // SubmitViewportPick the GUI calls — the selection itself still runs through product code.
    //
    // Default is CROSSING (touching selects), which is what the drag direction decides in the GUI.
    std::istringstream is(rest);
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    if (!(is >> x0 >> y0 >> x1 >> y1)) {
      Fail(run, "parse", "BOX expects four world coordinates, got: " + rest, sourceLine);
      return false;
    }
    std::string mod;
    bool subtract = false;
    bool windowMode = false;
    while (is >> mod) {
      const std::string m = UpperAscii(mod);
      if (m == "SUBTRACT")
        subtract = true;
      else if (m == "WINDOW")
        windowMode = true;
      else {
        Fail(run, "parse", "BOX: unknown modifier " + mod + " (expected WINDOW or SUBTRACT)", sourceLine);
        return false;
      }
    }
    run.st.selBoxWaitingSecond = true;
    run.st.selBoxAnchorX = x0;
    run.st.selBoxAnchorY = y0;
    SubmitViewportPick(run.st, x1, y1, run.log, subtract, windowMode);
  } else if (verb == "TRIMPICK") {
    // TRIMPICK <x> <y> — one object pick while TRIM is active.
    //
    // PICK cannot express this. TRIM is the one pick-driven command whose clicks do NOT go through
    // SubmitViewportPick: src/ui/CadUi.cpp routes them straight to SubmitTrimViewportPick and never
    // reaches the shared path, so a transcript using PICK during TRIM silently does nothing at all.
    // Every other pick-driven command — OFFSET included — is reachable with PICK.
    //
    // That asymmetry is a REQ-203 gap in TRIM itself, not something this verb fixes: TRIM's whole
    // behaviour, for every entity type, is undrivable without it. Rerouting the input belongs in a
    // task that owns TRIM. Until then this verb hands the click to the same entry point the GUI
    // calls, exactly as BOX above arms the selection-box fields the viewport would arm.
    std::istringstream is(rest);
    float x = 0.f;
    float y = 0.f;
    if (!(is >> x >> y)) {
      Fail(run, "parse", "TRIMPICK expects two world coordinates, got: " + rest, sourceLine);
      return false;
    }
    if (run.st.active != AppCommandState::Kind::Trim) {
      Fail(run, "state", "TRIMPICK requires TRIM to be the active command", sourceLine);
      return false;
    }
    // The GUI derives this from the viewport height and the snap aperture; a transcript has no
    // viewport, so it uses a fixed world tolerance. Picks in transcripts are placed ON the object.
    SubmitTrimViewportPick(run.st, x, y, 1.f, run.log);
  } else if (verb == "ESC") {
    CancelActiveCommand(run.st, run.log);
  } else if (verb == "UNDO") {
    DoUndo(run.st, run.log);
  } else if (verb == "REDO") {
    DoRedo(run.st, run.log);
  } else if (verb == "CHECK") {
    CheckInvariants(run, sourceLine);
  } else if (verb == "EXPECT") {
    std::string arg;
    const std::string what = UpperAscii(FirstWord(rest, &arg));
    if (what == "SAMEFILE" || what == "DIFFERENTFILE") {
      const bool wantSame = (what == "SAMEFILE");
      // EXPECT SAMEFILE <a> <b> — byte comparison. This is what makes the `gs-roundtrip` oracle
      // expressible in a transcript rather than needing a separate build-system step, which in turn
      // is what lets the fuzzer generate it.
      //
      // EXPECT DIFFERENTFILE <a> <b> is its counterpart, and it exists for one reason: an oracle
      // shaped "do something, undo it, compare" PASSES TRIVIALLY when the something did not happen.
      // A check that cannot fail reports success forever, which is the failure mode this harness has
      // already been bitten by twice (docs/fuzz-harness.md §8). DIFFERENTFILE is how a generated
      // transcript asserts that the document actually MOVED before asserting that it came back.
      std::istringstream fs2(ExpandVars(run, arg));
      std::string pa;
      std::string pb;
      if (!(fs2 >> pa >> pb)) {
        Fail(run, "parse", "EXPECT " + what + " needs two paths", sourceLine);
        return false;
      }
      std::ifstream fa(pa, std::ios::binary);
      std::ifstream fb(pb, std::ios::binary);
      if (!fa || !fb) {
        Fail(run, "io", "EXPECT " + what + ": cannot open " + (!fa ? pa : pb), sourceLine);
        return false;
      }
      const std::string sa((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
      const std::string sb((std::istreambuf_iterator<char>(fb)), std::istreambuf_iterator<char>());
      if (wantSame && sa != sb) {
        // Report the first differing offset: on a JSON document that is usually enough to name the
        // field, and it keeps the failure line short enough to read in a summary.
        size_t off = 0;
        while (off < sa.size() && off < sb.size() && sa[off] == sb[off])
          ++off;
        Fail(run, "expect",
             "SAMEFILE: files differ at byte " + std::to_string(off) + " (" +
                 std::to_string(sa.size()) + " vs " + std::to_string(sb.size()) + " bytes)",
             sourceLine);
        return false;
      }
      if (!wantSame && sa == sb) {
        Fail(run, "expect",
             "DIFFERENTFILE: files are identical (" + std::to_string(sa.size()) +
                 " bytes) — the step between them changed nothing, so any check that follows is "
                 "vacuous",
             sourceLine);
        return false;
      }
    } else if (what == "LOG") {
      // EXPECT LOG "text" — substring match over the whole log, so a transcript can assert that a
      // command reported something (REQ-201) without depending on the exact wording around it.
      std::string needle = Trim(arg);
      if (needle.size() >= 2 && needle.front() == '"' && needle.back() == '"')
        needle = needle.substr(1, needle.size() - 2);
      bool found = false;
      for (const std::string& l : run.log) {
        if (l.find(needle) != std::string::npos) {
          found = true;
          break;
        }
      }
      if (!found) {
        Fail(run, "expect", "no log line contains: " + needle, sourceLine);
        return false;
      }
    } else {
      long want = 0;
      std::istringstream is(arg);
      if (!(is >> want)) {
        Fail(run, "parse", "EXPECT " + what + " needs a count", sourceLine);
        return false;
      }
      long got = -1;
      if (what == "LINES")
        got = static_cast<long>(run.st.userLinesFlat.size() / 6);
      else if (what == "CIRCLES")
        got = static_cast<long>(run.st.userCirclesCxCyZR.size() / 4);
      else if (what == "POLYLINES")
        got = static_cast<long>(PolylineCountOf(run.st));
      else if (what == "ARCS")
        got = static_cast<long>(run.st.userArcs.size());
      else if (what == "ELLIPSES")
        got = static_cast<long>(run.st.userEllipses.size());
      else if (what == "ANNOTATIONS")
        got = static_cast<long>(run.st.cadAnnotations.size());
      else if (what == "SURVEYPOINTS")
        got = static_cast<long>(run.st.surveyPoints.size());
      // Not a geometry count: what the drawing currently considers picked. It is the only way to
      // assert that something is NOT selectable — REQ-084's isolation gate, where the object is
      // still in the drawing and must simply refuse to be picked.
      else if (what == "SELECTED")
        got = static_cast<long>(run.st.selection.size());
      else {
        Fail(run, "parse",
             "EXPECT: unknown quantity " + what +
                 " (LINES CIRCLES POLYLINES ARCS ELLIPSES ANNOTATIONS SURVEYPOINTS SELECTED)",
             sourceLine);
        return false;
      }
      if (got != want) {
        Fail(run, "expect",
             what + ": expected " + std::to_string(want) + ", got " + std::to_string(got),
             sourceLine);
        return false;
      }
    }
  } else {
    Fail(run, "parse", "unknown transcript verb: " + verbRaw, sourceLine);
    return false;
  }

  TickFrame(run);
  if (run.checkEveryStep)
    CheckInvariants(run, sourceLine);
  return run.failures.empty();
}

// ---------------------------------------------------------------------------
// ImGui, headless (ADR-031 (c′)) — a font atlas, no window, no GPU.
// ---------------------------------------------------------------------------

void InitHeadlessImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1920.f, 1080.f);  // no real display; NewFrame only needs it non-zero
  io.DeltaTime = 1.0f / 60.0f;
  io.IniFilename = nullptr;  // never write imgui.ini from a test run (CON-07)

  if (!LoadApplicationFont())
    io.Fonts->AddFontDefault();
  io.FontGlobalScale = 1.35f;  // matches main.cpp, so text measures the same as it does in the GUI

  unsigned char* pixels = nullptr;
  int w = 0;
  int h = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);  // builds the atlas on the CPU; nothing is uploaded

  ImGui::NewFrame();  // makes ImGui::GetFont() valid — SurveyPoints.cpp measures through it
}

void ShutdownHeadlessImGui() {
  ImGui::EndFrame();
  ImGui::DestroyContext();
}

int Usage() {
  std::fprintf(stderr,
               "usage: gosurvey_headless run  <transcript> [--json <path>] [--out <dir>]\n"
               "                              [--sig <path>] [--check-at-end]\n"
               "       gosurvey_headless fuzz [--seed N | --seeds A..B] [--out <dir>]\n"
               "                              [--timeout-ms N] [--max-attempts N] [--keep-passing]\n");
  return 2;
}

int RunTranscriptMain(int argc, char** argv);

}  // namespace

// Defined in FuzzMain.cpp. Declared here rather than in a header because there is exactly one
// caller and one definition; a header for a single extern is ceremony, not structure.
int FuzzMain(int argc, char** argv, const char* exePath);

int main(int argc, char** argv) {
  if (argc >= 2 && std::string(argv[1]) == "fuzz")
    return FuzzMain(argc, argv, argv[0]);
  if (argc >= 3 && std::string(argv[1]) == "run")
    return RunTranscriptMain(argc, argv);
  return Usage();
}

namespace {

int RunTranscriptMain(int argc, char** argv) {
  const std::string transcriptPath = argv[2];
  std::string jsonPath;
  std::string outDir;
  std::string sigPath;
  bool checkEveryStep = true;

  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--json" && i + 1 < argc)
      jsonPath = argv[++i];
    else if (a == "--out" && i + 1 < argc)
      outDir = argv[++i];
    else if (a == "--sig" && i + 1 < argc)
      sigPath = argv[++i];
    else if (a == "--check-at-end")
      checkEveryStep = false;
    else
      return Usage();
  }

  std::ifstream in(transcriptPath);
  if (!in) {
    std::fprintf(stderr, "cannot open transcript: %s\n", transcriptPath.c_str());
    return 2;
  }

  InitHeadlessImGui();

  Run run;
  run.checkEveryStep = checkEveryStep;
  run.outDir = outDir.empty()
                   ? (std::filesystem::temp_directory_path() / "gosurvey_headless")
                   : std::filesystem::path(outDir);
  std::error_code ec;
  std::filesystem::create_directories(run.outDir, ec);
  headless::ClearDialogAnswers();

  std::string line;
  int sourceLine = 0;
  bool aborted = false;
  while (std::getline(in, line)) {
    ++sourceLine;
    const std::string s = Trim(line);
    if (s.empty() || s[0] == '#')
      continue;
    if (!ExecuteStep(run, s, sourceLine)) {
      aborted = true;
      break;
    }
    ++run.stepIndex;
  }

  if (!aborted && !run.checkEveryStep)
    CheckInvariants(run, sourceLine);

  const size_t pending = headless::PendingDialogAnswers();
  ShutdownHeadlessImGui();

  // --- Report ---------------------------------------------------------------------------------
  const bool passed = run.failures.empty();
  if (!passed) {
    const Failure& f = run.failures.front();
    std::fprintf(stderr, "FAIL [%s] step %d (line %d): %s\n", f.reason.c_str(), f.stepIndex,
                 f.sourceLine, f.detail.c_str());
    if (run.failures.size() > 1)
      std::fprintf(stderr, "  (+%zu more)\n", run.failures.size() - 1);
  } else {
    std::fprintf(stdout, "PASS %d steps, %zu log lines\n", run.stepIndex, run.log.size());
  }
  if (pending != 0) {
    // Not a failure: the transcript queued an answer no command asked for. Worth saying, because it
    // means the transcript and the code disagree about whether a command opens a dialog.
    std::fprintf(stderr, "note: %zu queued dialog answer(s) were never consumed\n", pending);
  }

  // The signature file is how the fuzz parent tells "same defect" from "some other failure" while
  // minimizing. Written even on success ("pass") so a missing file always means the child died
  // before it could report — a crash, not a clean verdict.
  if (!sigPath.empty()) {
    std::ofstream sg(sigPath, std::ios::binary);
    if (sg)
      sg << FailureSignature(run) << "\n";
  }

  if (!jsonPath.empty()) {
    std::ofstream js(jsonPath, std::ios::binary);
    if (js) {
      js << "{\n  \"pass\": " << (passed ? "true" : "false") << ",\n";
      js << "  \"steps\": " << run.stepIndex << ",\n";
      js << "  \"transcript\": \"" << JsonEscape(transcriptPath) << "\",\n";
      js << "  \"pendingDialogAnswers\": " << pending << ",\n";
      js << "  \"entities\": {\n";
      js << "    \"lines\": " << run.st.userLinesFlat.size() / 6 << ",\n";
      js << "    \"circles\": " << run.st.userCirclesCxCyZR.size() / 4 << ",\n";
      js << "    \"polylines\": " << PolylineCountOf(run.st) << ",\n";
      js << "    \"arcs\": " << run.st.userArcs.size() << ",\n";
      js << "    \"ellipses\": " << run.st.userEllipses.size() << ",\n";
      js << "    \"annotations\": " << run.st.cadAnnotations.size() << ",\n";
      js << "    \"surveyPoints\": " << run.st.surveyPoints.size() << "\n";
      js << "  },\n";
      js << "  \"failures\": [\n";
      for (size_t i = 0; i < run.failures.size(); ++i) {
        const Failure& f = run.failures[i];
        js << "    {\"reason\": \"" << JsonEscape(f.reason) << "\", \"detail\": \""
           << JsonEscape(f.detail) << "\", \"step\": " << f.stepIndex
           << ", \"line\": " << f.sourceLine << "}";
        js << (i + 1 < run.failures.size() ? ",\n" : "\n");
      }
      js << "  ],\n";
      js << "  \"log\": [\n";
      for (size_t i = 0; i < run.log.size(); ++i) {
        js << "    \"" << JsonEscape(run.log[i]) << "\"";
        js << (i + 1 < run.log.size() ? ",\n" : "\n");
      }
      js << "  ]\n}\n";
    }
  }

  return passed ? 0 : 1;
}

}  // namespace
