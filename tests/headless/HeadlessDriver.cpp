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
#include "SurveyCsv.hpp"
#include "SurveyPoints.hpp"
#include "docinvariants.hpp"

#include <imgui.h>

#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <array>
#include <tuple>
#include <fstream>
#include <sstream>
#include <utility>
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

  /// How many DISTINCT triangulations surface 0 has had, counted by watching its `shared_ptr` across
  /// frames (`EXPECT SURFACETINGEN`). A rebuild replaces the pointer wholesale (ADR-028 (a)), so a
  /// change here means a retriangulation happened and an unchanged value means one did not — which
  /// is REQ-070's "without rebuilding the triangulation", stated as something a transcript can fail.
  int surfaceTinGeneration = 0;
  std::weak_ptr<const CadTin> lastSurfaceTin;
  bool sawSurfaceTin = false;
};

/// Expand %OUT% to the run's temp directory. Transcripts must never write into the source tree
/// (CON-07 / REQ-200), and a fuzz run writes a lot of files.
std::string ExpandVars(const Run& run, const std::string& s) {
  // %OUT%  — this run's scratch directory.
  // %DATA% — the repository's `samples/` directory, so a transcript can name a fixture file
  //          without hard-coding whoever's checkout it was written in.
  const std::pair<const char*, std::string> vars[] = {
      {"%OUT%", run.outDir.string()},
      {"%DATA%", std::string(GOSURVEY_SAMPLES_DIR)},
  };
  std::string o = s;
  for (const auto& v : vars) {
    const std::string token = v.first;
    for (size_t p = o.find(token); p != std::string::npos; p = o.find(token, p)) {
      o.replace(p, token.size(), v.second);
      p += v.second.size();
    }
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
  // The surface display-geometry cache (ADR-036 (e)), in the same order main.cpp's frame loop runs
  // it: after ids, because it is keyed on them. TickSurfaceRebuilds is deliberately NOT called here —
  // see the req069 transcript's header on why this driver uses the synchronous SURFACEREBUILD.
  RefreshSurfaceDisplayGeometry(run.st);

  // Watch surface 0's triangulation identity, for EXPECT SURFACETINGEN. Compared with
  // `owner_before`-free pointer equality against a locked weak_ptr rather than by holding a
  // shared_ptr: keeping one alive here would pin a superseded triangulation for the whole run and
  // change the very lifetime the assertion is about.
  const std::shared_ptr<const CadTin> tin =
      run.st.cadSurfaces.empty() ? nullptr : run.st.cadSurfaces[0].tin;
  if (!run.sawSurfaceTin || run.lastSurfaceTin.lock() != tin) {
    if (tin || run.sawSurfaceTin)
      ++run.surfaceTinGeneration;
    run.lastSurfaceTin = tin;
    run.sawSurfaceTin = true;
  }
}

/// Segment count of one of surface 0's cached display buffers (REQ-070 / ADR-036 (e)).
///
/// Reached through the surface's stable id, exactly as the drawing code does, so a transcript that
/// erases a surface and asserts what the surviving one draws is testing the real lookup rather than
/// an array position that happens to line up.
size_t SurfaceCacheSegs(const AppCommandState& st,
                        std::vector<float> AppCommandState::SurfaceDisplayCacheEntry::*member) {
  if (st.cadSurfaces.empty() || st.cadSurfaceAttrs.empty())
    return 0;
  const std::uint64_t id = st.cadSurfaceAttrs[0].id;
  if (id == 0)
    return 0;
  for (const auto& e : st.surfaceDisplayCache)
    if (e.surfaceId == id)
      return (e.*member).size() / 6;
  return 0;
}

/// Do the drawing's polylines carry EXACTLY the segments the surface display cache is showing as
/// contours? (REQ-071's first acceptance condition, ADR-036 (f).)
///
/// This is the assertion the whole extraction rests on: "extraction produces polylines at exactly the
/// displayed contour elevations". It is checked by comparing the two things the requirement says must
/// be equal — the cache's own contour buffers and the polylines EXTRACT created — rather than by
/// re-deriving an expectation, which would only prove the test agrees with itself.
///
/// Both sides are reduced to a sorted multiset of segments, so traversal order and which contour a
/// segment came from are deliberately not part of the comparison; only the geometry is. Equality is
/// EXACT, not toleranced: both sides are float copies of the same doubles out of one pure function,
/// so anything other than bit equality means they did not come from the same generation.
///
/// **Assumes every polyline in the drawing came from EXTRACT** — true in the transcript that uses it,
/// which starts from a drawing with none.
bool ExtractedContoursMatchDisplay(const AppCommandState& st) {
  using Seg = std::array<float, 6>;
  const auto canon = [](float ax, float ay, float az, float bx, float by, float bz) {
    // Endpoint order is not meaningful — one side may walk a contour the other way — so each segment
    // is stored with its lexicographically smaller end first.
    const bool swap = std::tie(bx, by, bz) < std::tie(ax, ay, az);
    return swap ? Seg{bx, by, bz, ax, ay, az} : Seg{ax, ay, az, bx, by, bz};
  };

  std::vector<Seg> shown;
  for (const auto& e : st.surfaceDisplayCache) {
    for (const std::vector<float>* buf : {&e.minorContours, &e.majorContours}) {
      for (size_t i = 0; i + 5 < buf->size(); i += 6)
        shown.push_back(canon((*buf)[i], (*buf)[i + 1], (*buf)[i + 2], (*buf)[i + 3], (*buf)[i + 4],
                              (*buf)[i + 5]));
    }
  }

  std::vector<Seg> baked;
  const size_t plCount = PolylineCountOf(st);
  for (size_t p = 0; p < plCount; ++p) {
    const int b = st.userPolylineOffsets[p];
    const int e = st.userPolylineOffsets[p + 1];
    const auto vx = [&](int v, int c) { return st.userPolylineVerts[static_cast<size_t>(v) * 3 + c]; };
    for (int v = b; v + 1 < e; ++v)
      baked.push_back(canon(vx(v, 0), vx(v, 1), vx(v, 2), vx(v + 1, 0), vx(v + 1, 1), vx(v + 1, 2)));
    // A closed polyline's closing segment is implied, exactly as the display buffer's is.
    if (e - b > 2 && p < st.userPolylineClosed.size() && st.userPolylineClosed[p])
      baked.push_back(canon(vx(e - 1, 0), vx(e - 1, 1), vx(e - 1, 2), vx(b, 0), vx(b, 1), vx(b, 2)));
  }

  if (shown.empty() || shown.size() != baked.size())
    return false;
  std::sort(shown.begin(), shown.end());
  std::sort(baked.begin(), baked.end());
  return shown == baked;
}

/// True when a point file's first row is a header (`P,N,E,Z,D`) rather than data.
///
/// Decided by the first cell alone: a point number is always a number, and a header's first column
/// never is. That is the whole rule — a point file has no format marker to consult.
bool FirstRowLooksLikeHeader(const std::string& path) {
  std::ifstream f(path);
  std::string line;
  if (!f || !std::getline(f, line))
    return false;
  const std::string cell = Trim(line.substr(0, line.find_first_of(",; \t")));
  if (cell.empty())
    return false;
  const char c = cell[0];
  return !(c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9'));
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
    if (fmt == "POINTS") {
      // IMPORT POINTS <path> — a survey point file (REQ-041 / REQ-083), which is how survey label
      // geometry actually comes into a drawing.
      //
      // This is the one place the driver does NOT go through the command line, and the reason is
      // that there is no command line to go through: IMPORTPOINTS only raises the import window,
      // and the GUI's Import button is what fills these three fields and calls the importer. The
      // driver sets exactly those fields and calls exactly that function, so the path under test is
      // still the user's path — the window is a form, not logic.
      if (verb == "EXPORT") {
        Fail(run, "parse", "EXPORT POINTS is not driven; use IMPORT POINTS", sourceLine);
        return false;
      }
      std::snprintf(run.st.surveyImportCsvPath, sizeof run.st.surveyImportCsvPath, "%s", path.c_str());
      run.st.surveyImportCsvLayoutIdx = 0;  // P,N,E,Z,D — the layout every samples/ point file uses
      // Skip a header row if there is one. The importer would otherwise reject it as an unparsable
      // row and say so, which is correct behavior but reads as a failure in a transcript log.
      run.st.surveyImportCsvSkipFirstRow = FirstRowLooksLikeHeader(path);
      if (!SurveyCsvImportFile(run.st, run.log)) {
        Fail(run, "io", "IMPORT POINTS failed: " + path, sourceLine);
        return false;
      }
    } else if (fmt == "DXF") {
      const bool ok = (verb == "EXPORT") ? ExportDxfFile(run.st, path.c_str(), run.log)
                                         : ImportDxfFile(run.st, path.c_str(), run.log);
      if (!ok) {
        Fail(run, "io", verb + " " + fmt + " failed: " + path, sourceLine);
        return false;
      }
    } else {
      Fail(run, "parse", verb + ": unsupported format " + fmt + " (expected DXF or POINTS)",
           sourceLine);
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
  } else if (verb == "CLIPCOPY") {
    // CLIPCOPY — copy the current selection to the clipboard.
    //
    // There is no COPYCLIP command-line verb: the clipboard is bound to Ctrl+C in main.cpp and
    // CadUi.cpp and reachable no other way, so a transcript cannot get at it through
    // ProcessCommandLineSubmit. This hands the selection to the same CopySelectionToClipboard the
    // key binding calls, exactly as BOX above arms the selection-box fields the viewport would arm
    // and TRIMPICK hands a click to the entry point the GUI routes TRIM's clicks to.
    //
    // Like TRIMPICK's, this is a REQ-203 gap in the clipboard's own input routing rather than
    // something this verb repairs — the whole of copy/paste is undrivable without it.
    CopySelectionToClipboard(run.st, run.log);
  } else if (verb == "ESC") {
    CancelActiveCommand(run.st, run.log);
  } else if (verb == "UNDO") {
    DoUndo(run.st, run.log);
  } else if (verb == "REDO") {
    DoRedo(run.st, run.log);
  } else if (verb == "DUMP") {
    // DUMP LABELS — every survey point's label box, in world units, beside the point it labels.
    //
    // Not an assertion. Label PLACEMENT is a relationship between two rectangles, and no
    // `EXPECT <count>` can express it while a `.gs` diff shows it only as eight bare floats. This
    // prints the two numbers that decide whether the placement rule holds — the box's offset from
    // its point, and the box's size — so that a rule like "the anchor does not move when the text
    // gets longer" can be read straight off the log.
    const std::string what = UpperAscii(Trim(rest));
    if (what != "LABELS" && what != "SURFACES") {
      Fail(run, "parse", "DUMP expects LABELS or SURFACES, got: " + rest, sourceLine);
      return false;
    }
    // DUMP SURFACES — every surface's style, its generated component sizes, and how many batches
    // the renderer would be handed (REQ-070 / ADR-036 (e)).
    //
    // Not an assertion either, and here for the same reason DUMP LABELS is: the counts a transcript
    // has to assert are properties of a FIXTURE — this triangulation at this interval — and reading
    // them out of the running program is how they get into a transcript as a stated fact rather than
    // as a number someone tuned until the test went green.
    if (what == "SURFACES") {
      run.log.push_back("[dump] surface | style | tris | border | minor | major (segments)");
      for (size_t si = 0; si < run.st.cadSurfaces.size(); ++si) {
        const CadSurface& s = run.st.cadSurfaces[si];
        const std::uint64_t id = si < run.st.cadSurfaceAttrs.size() ? run.st.cadSurfaceAttrs[si].id : 0;
        const auto it = std::find_if(
            run.st.surfaceDisplayCache.begin(), run.st.surfaceDisplayCache.end(),
            [&](const AppCommandState::SurfaceDisplayCacheEntry& e) { return e.surfaceId == id; });
        char buf[512];
        if (it == run.st.surfaceDisplayCache.end()) {
          std::snprintf(buf, sizeof buf, "[dump] %s | %s | (no cache entry)", s.name.c_str(),
                        s.styleName.empty() ? "(default)" : s.styleName.c_str());
        } else {
          std::snprintf(buf, sizeof buf, "[dump] %s | %s | %zu | %zu | %zu | %zu", s.name.c_str(),
                        it->style.name.c_str(), it->triangleEdges.size() / 6,
                        it->borderEdges.size() / 6, it->minorContours.size() / 6,
                        it->majorContours.size() / 6);
        }
        run.log.push_back(buf);
      }
      run.log.push_back("[dump] batches handed to the renderer: " +
                        std::to_string(run.st.surfaceDisplayGeometry.lines.size()) +
                        ", tin generations: " + std::to_string(run.surfaceTinGeneration));
      TickFrame(run);
      if (run.checkEveryStep)
        CheckInvariants(run, sourceLine);
      return run.failures.empty();
    }
    run.log.push_back("[dump] id | dx=left edge, dy=vertical centre, from point | box w,h | text");
    for (const SurveyPoint& p : run.st.surveyPoints) {
      const int aix = FindSurveyLabelAnnIndex(run.st, p);
      if (aix < 0) {
        run.log.push_back("[dump] " + std::to_string(p.id) + " | (no label)");
        continue;
      }
      const CadAnnotation& a = run.st.cadAnnotations[static_cast<size_t>(aix)];
      char buf[1024];
      std::snprintf(buf, sizeof buf,
                    "[dump] %d | dx=%+.4f dy=%+.4f | w=%.4f h=%.4f | \"%s\"", p.id,
                    static_cast<double>(a.boxMinX - p.easting),
                    static_cast<double>(0.5f * (a.boxMinY + a.boxMaxY) - p.northing),
                    static_cast<double>(a.boxMaxX - a.boxMinX),
                    static_cast<double>(a.boxMaxY - a.boxMinY), a.text.c_str());
      run.log.push_back(buf);
    }
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
    } else if (what == "LABELANCHOR") {
      // EXPECT LABELANCHOR — every survey label holds the same position relative to its own point,
      // whatever its text says: same LEFT EDGE offset, same VERTICAL CENTRE offset.
      //
      // Two axes, two different reasons, and the asymmetry is the design rather than an oversight.
      // X is where the marker was in danger: with a centred X, half of every added character came
      // back west, so the longest description was the one that buried its own point. X is therefore
      // pinned at the left edge and the box grows away east. Y was never exposed — the box already
      // stands clear to the east — so Y is centred, which is what puts the label beside the point
      // instead of hanging under it. Pinning `boxMaxY` here instead would re-assert the old
      // hangs-below layout and fail the moment a label had two lines rather than one.
      //
      // The width check is a vacuity guard, in the spirit of EXPECT DIFFERENTFILE: six boxes of
      // identical size would satisfy an equal-offsets test perfectly while proving nothing, because
      // the text never varied. Offsets equal AND widths varying is what has content.
      float dx0 = 0.f;
      float dy0 = 0.f;
      float w0 = 0.f;
      bool first = true;
      bool widthVaries = false;
      long labels = 0;
      for (const SurveyPoint& p : run.st.surveyPoints) {
        const int aix = FindSurveyLabelAnnIndex(run.st, p);
        if (aix < 0)
          continue;
        const CadAnnotation& a = run.st.cadAnnotations[static_cast<size_t>(aix)];
        const float dx = a.boxMinX - p.easting;
        const float dy = 0.5f * (a.boxMinY + a.boxMaxY) - p.northing;
        const float w = a.boxMaxX - a.boxMinX;
        ++labels;
        if (dx < 0.f) {
          Fail(run, "expect",
               "LABELANCHOR: point " + std::to_string(p.id) + " starts west of its own marker (dx=" +
                   std::to_string(dx) + ")",
               sourceLine);
          return false;
        }
        if (first) {
          dx0 = dx;
          dy0 = dy;
          w0 = w;
          first = false;
          continue;
        }
        // Tolerance is a whisker: these offsets are the SAME arithmetic on the same two floats for
        // every point, so anything beyond rounding means the text moved the anchor.
        constexpr float kTol = 1.e-3f;
        if (std::fabs(dx - dx0) > kTol || std::fabs(dy - dy0) > kTol) {
          Fail(run, "expect",
               "LABELANCHOR: point " + std::to_string(p.id) + " anchors at (" + std::to_string(dx) +
                   ", " + std::to_string(dy) + ") but the first label anchors at (" +
                   std::to_string(dx0) + ", " + std::to_string(dy0) +
                   ") — the label's text is moving its anchor",
               sourceLine);
          return false;
        }
        if (std::fabs(w - w0) > kTol)
          widthVaries = true;
      }
      if (labels < 2) {
        Fail(run, "expect",
             "LABELANCHOR: needs at least two labelled points to compare, found " +
                 std::to_string(labels),
             sourceLine);
        return false;
      }
      if (!widthVaries) {
        Fail(run, "expect",
             "LABELANCHOR: every label box is the same width, so equal anchors prove nothing — "
             "the fixture must vary the description text",
             sourceLine);
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
      else if (what == "SURFACES")
        got = static_cast<long>(run.st.cadSurfaces.size());
      // How many of the CURRENT selection are TIN surfaces (REQ-068 / ADR-036 (b)). Distinct from
      // SELECTED on purpose: "1 object is selected" and "the selected object is the surface" are
      // different claims, and a surface pick that silently returned the polyline underneath it would
      // satisfy the first. It is also the only way to assert a REFUSAL — that MOVE dropped the
      // surface and kept everything else — which is ADR-036 (c)'s whole obligation.
      else if (what == "SELECTEDSURFACES")
        got = static_cast<long>(std::count_if(
            run.st.selection.begin(), run.st.selection.end(),
            [](const SelectedEntity& e) { return e.type == SelectedEntity::Type::Surface; }));
      // Border segments the display cache holds for surface 0 (ADR-036 (e)). Asserts the cache is
      // POPULATED, which is what the selection highlight reads; a highlight that silently drew
      // nothing would otherwise look exactly like a surface that was never selected.
      else if (what == "SURFACEBORDERSEGS") {
        const std::vector<float>* b = run.st.cadSurfaces.empty() ? nullptr : SurfaceBorderEdges(run.st, 0);
        got = b ? static_cast<long>(b->size() / 6) : 0;
      }
      // The rest of surface 0's generated display geometry (REQ-070 / ADR-036 (e)). These are what
      // make REQ-070's toggle matrix assertable — "triangles off and contours on draws only
      // contours" is a claim about which of these buffers is EMPTY, and a renderer that skipped a
      // component it had nonetheless generated would pass an eyes-only check of the same thing.
      else if (what == "SURFACETRISEGS")
        got = static_cast<long>(SurfaceCacheSegs(run.st, &AppCommandState::SurfaceDisplayCacheEntry::triangleEdges));
      else if (what == "SURFACEMINORSEGS")
        got = static_cast<long>(SurfaceCacheSegs(run.st, &AppCommandState::SurfaceDisplayCacheEntry::minorContours));
      else if (what == "SURFACEMAJORSEGS")
        got = static_cast<long>(SurfaceCacheSegs(run.st, &AppCommandState::SurfaceDisplayCacheEntry::majorContours));
      // How many batches the renderer would be handed, across every visible surface. Zero means
      // nothing is drawn for surfaces at all, which is how "a surface on a frozen layer" and "a
      // style with everything switched off" are told apart from a cache that simply never filled.
      else if (what == "SURFACEBATCHES")
        got = static_cast<long>(run.st.surfaceDisplayGeometry.lines.size());
      // How many DISTINCT triangulations surface 0 has had since the transcript began. This is the
      // only direct way to assert REQ-070's central condition — "changing the contour interval
      // updates the display **without rebuilding the triangulation**". Segment counts cannot say it:
      // a retriangulation that produced the same number of edges would pass one, and this cannot be
      // satisfied by anything except the triangulation genuinely not being rebuilt.
      else if (what == "SURFACETINGEN")
        got = static_cast<long>(run.surfaceTinGeneration);
      // 1 when the drawing's polylines carry exactly the segments the display cache is showing as
      // contours (REQ-071). This is the acceptance condition that cannot be expressed as a count: a
      // wrong-but-plausible extraction — one interval out, or the previous style's contours — has the
      // same polyline count as a right one.
      else if (what == "EXTRACTMATCHESDISPLAY")
        got = ExtractedContoursMatchDisplay(run.st) ? 1 : 0;
      else {
        Fail(run, "parse",
             "EXPECT: unknown quantity " + what +
                 " (LINES CIRCLES POLYLINES ARCS ELLIPSES ANNOTATIONS SURVEYPOINTS SELECTED"
                 " SURFACES SELECTEDSURFACES SURFACEBORDERSEGS SURFACETRISEGS SURFACEMINORSEGS"
                 " EXTRACTMATCHESDISPLAY"
                 " SURFACEMAJORSEGS SURFACEBATCHES SURFACETINGEN)",
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
  bool printLog = false;

  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--json" && i + 1 < argc)
      jsonPath = argv[++i];
    else if (a == "--out" && i + 1 < argc)
      outDir = argv[++i];
    else if (a == "--sig" && i + 1 < argc)
      sigPath = argv[++i];
    else if (a == "--print-log")
      printLog = true;  // authoring aid: DUMP output is otherwise only visible on a failure
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
  // `--print-log` is an authoring aid, not part of any test's verdict: DUMP output and command
  // replies are otherwise only visible when a step fails, which is the wrong time to be reading the
  // numbers a new transcript needs to assert.
  if (printLog) {
    for (const std::string& l : run.log)
      std::fprintf(stdout, "%s\n", l.c_str());
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
