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
#include "viewport/CadRubberPreview.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"  // CadCoord::WorldFromLocal, for EXPECT LINEXYZ (REQ-154)
#include "DxfIo.hpp"
#include "DwgIo.hpp"
#include "GsIo.hpp"
#include "GsAnnotationJson.hpp"
#include "HeadlessFileDialogs.hpp"
#include "SurveyCsv.hpp"
#include "SurveyPoints.hpp"
#include "TransformPreview.hpp"
#include "viewport/CadSnap.hpp"  // WorldToleranceFromPixels, for the GIZMO verb's screen-derived aperture
#include "ViewportPickPolicy.hpp"
#include "docinvariants.hpp"

#include <imgui.h>

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <array>
#include <tuple>
#include <fstream>
#include <set>
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
  /// Last HOVER position, so EXPECT PREVIEWBOUNDS can rebuild the rubber the viewport would draw.
  double hoverX = 0.0;
  double hoverY = 0.0;
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
  // The solid tessellation cache (REQ-313), in main.cpp's own order — right after the surface
  // refresh. Running it here is what lets a transcript assert the CACHE rather than only the
  // document: "tessellation is cached" and "a solid is drawn" are claims about this call's output,
  // and without it the driver would only ever see solids that exist and never solids that draw.
  RefreshSolidDisplayGeometry(run.st);

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

/// Total REQ-072 band-fill triangles across every bucket of surface 0's cache entry (its per-band
/// buffers plus the "unbanded" overflow bucket — `bandTriangleBuffers`, `CadCommands.hpp`). One count
/// across all buckets, because EXPECT is asking "how much banded geometry exists", not which band it
/// landed in — the per-band split is what the Analysis tab's colours are for, not this driver.
size_t SurfaceCacheBandTriCount(const AppCommandState& st) {
  if (st.cadSurfaces.empty() || st.cadSurfaceAttrs.empty())
    return 0;
  const std::uint64_t id = st.cadSurfaceAttrs[0].id;
  if (id == 0)
    return 0;
  for (const auto& e : st.surfaceDisplayCache)
    if (e.surfaceId == id) {
      size_t floats = 0;
      for (const auto& buf : e.bandTriangleBuffers)
        floats += buf.size();
      return floats / 9;
    }
  return 0;
}

/// Total REQ-072 slope-arrow line segments across every bucket of surface 0's cache entry
/// (`arrowLineBuffers`) — shaft plus both head barbs count as three segments per arrow
/// (`BuildSurfaceAnalysisGeometry`, `CadCommands.cpp`).
size_t SurfaceCacheArrowSegCount(const AppCommandState& st) {
  if (st.cadSurfaces.empty() || st.cadSurfaceAttrs.empty())
    return 0;
  const std::uint64_t id = st.cadSurfaceAttrs[0].id;
  if (id == 0)
    return 0;
  for (const auto& e : st.surfaceDisplayCache)
    if (e.surfaceId == id) {
      size_t floats = 0;
      for (const auto& buf : e.arrowLineBuffers)
        floats += buf.size();
      return floats / 6;
    }
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
    LoadBundledBlockLibrary(run.st, run.log);
    run.log.push_back("[driver] NEW");
  } else if (verb == "SPACE") {
    // SPACE PAPER | SPACE MODEL — switch the active space, so a transcript can exercise the
    // paper-space half of a command (REQ-037 / REQ-039).
    //
    // This is the ONE thing a transcript could not previously reach. Switching layouts is UI-only
    // in the shipped app — the layout tab (`CadUi.cpp`) and the MODEL/PAPER status button are its
    // only callers, and there is no command-line verb for it — so every paper-space acceptance
    // condition was manual-test-only. `SetActiveSpace` is the same entry point those buttons call,
    // so this reaches paper space the way the GUI does rather than by setting the field directly.
    //
    // PAPER creates a layout on first use, mirroring ToggleModelPaperSpace's own "create one on
    // first switch so PAPER has somewhere to go".
    const std::string which = UpperAscii(Trim(rest));
    if (which == "PAPER") {
      if (run.st.paperLayouts.empty())
        AddPaperLayout(run.st);
      SetActiveSpace(run.st, 0);
      run.log.push_back("[driver] SPACE PAPER");
    } else if (which == "MODEL") {
      SetActiveSpace(run.st, kModelSpaceIndex);
      run.log.push_back("[driver] SPACE MODEL");
    } else {
      Fail(run, "parse", "SPACE expects PAPER or MODEL, got: " + rest, sourceLine);
      return false;
    }
  } else if (verb == "OPEN") {
    const std::string path = ExpandVars(run, rest);
    if (!OpenDrawingDocument(run.st, path.c_str(), run.log)) {
      Fail(run, "io", "OPEN failed: " + path, sourceLine);
      return false;
    }
  } else if (verb == "SAVEAS") {
    const std::string path = ExpandVars(run, rest);
    if (!SaveDrawingDocument(run.st, path.c_str(), run.log)) {
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
    // A command may raise `pendingZoomExtents`, which the app consumes on its NEXT FRAME. The
    // driver has no frames, so consume it here — otherwise a transcript would assert against a flag
    // that never got acted on, which looks like a pass and proves nothing.
    //
    // Zero framebuffer size is honest, not a fudge: `ProcessPendingViewportZoom` early-returns on it
    // for every case whose camera is the WINDOW's (TASK-113 DEBT-1, still open). The REQ-123
    // floating-viewport case runs above that guard because its aspect is the viewport's rect on the
    // sheet, so it needs no framebuffer — which is exactly why it is the one zoom behaviour a
    // transcript can drive end to end.
    ProcessPendingViewportZoom(run.st, nullptr, nullptr, nullptr, 0, 0, 1.f, run.log);
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
  } else if (verb == "CLICK" || verb == "CLICKUCS") {
    // CLICK <x> <y> [SUBTRACT] [CROSSING] — a viewport click routed the way the GUI routes it.
    //
    // TASK-099. PICK above hands its coordinates straight to SubmitViewportPick, which skips the
    // one layer that decides whether a click reaches the command at all. That layer used to be an
    // inline whitelist in src/ui/CadUi.cpp, and a command missing from it silently discarded every
    // click — RECT, then FEATURELINE, then all five of REQ-103's MIRROR/LENGTHEN/EXTEND/BREAK/
    // STRETCH shipped that way, with green PICK-based transcripts the whole time.
    //
    // CLICK asks ViewportClickRouteFor the same question DrawDrawingViewport asks, then acts on the
    // answer. A command the UI does not route now fails its transcript instead of quietly passing.
    std::istringstream is(rest);
    float x = 0.f;
    float y = 0.f;
    if (!(is >> x >> y)) {
      Fail(run, "parse", "CLICK expects two world coordinates, got: " + rest, sourceLine);
      return false;
    }
    // An optional third coordinate: see the solid-pick note below for what it is for.
    float clickZ = 0.f;
    bool clickHasZ = false;
    {
      const std::streampos save = is.tellg();
      if (is >> clickZ) {
        clickHasZ = true;
      } else {
        is.clear();
        if (save != std::streampos(-1))
          is.seekg(save);
      }
    }
    if (verb == "CLICKUCS") {
      // CLICKUCS <u> <v> - a viewport click at (u, v) in the ACTIVE UCS XY plane (REQ-312).
      //
      // CLICK and PICK hand storage coordinates straight through, which cannot express a click on
      // a TILTED work plane at all: the GUI resolves one by intersecting the cursor ray with that
      // plane and publishing the hit point's own Z (AppCommandState::resolvedPointZ), and a pair
      // of storage X/Y carries none of that. On a VERTICAL work plane it is not even well posed:
      // two points on a wall share an (x, y) and differ only in height. So this states the pick
      // where the user actually made it, in the plane being drawn on. Under the WCS it is CLICK.
      const ray3d::Vec3 world =
          ucs::UcsToWorld(run.st.activeUcs, {static_cast<double>(x), static_cast<double>(y), 0.0});
      CadCoord::LocalFromWorld(run.st, world.x, world.y, &x, &y);
      run.st.resolvedPointZValid = true;
      run.st.resolvedPointZ = static_cast<float>(world.z);
    }
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
        Fail(run, "parse", "CLICK: unknown modifier " + mod + " (expected SUBTRACT or CROSSING)",
             sourceLine);
        return false;
      }
    }
    switch (ViewportClickRouteFor(run.st)) {
    case ViewportClickRoute::RawEntityPick:
    case ViewportClickRoute::SnappedPointPick:
      // The GUI's raw-vs-snapped distinction is an OSNAP adjustment, and a transcript has no
      // OSNAP — both land on the coordinates the transcript named. What CLICK is testing here is
      // that the command is routed AT ALL.
      //
      // A prompted solid command reads the cursor through `CadResolveSolidPick` rather than from the
      // click coordinates directly — a radius is a distance, a height is a closest approach — so the
      // same resolution the viewport performs each frame is performed here first. Doing it in the
      // driver rather than duplicating the arithmetic in a verb is the point: a test that resolved
      // the pick its own way would be a test of its own arithmetic.
      if (run.st.active == AppCommandState::Kind::Solid) {
        double z = ucs::WorkPlaneZAt(CadActiveWorkPlane(run.st), static_cast<double>(x),
                                     static_cast<double>(y));
        // An explicit third coordinate aims the ray at a point OFF the work plane, which is the only
        // way a transcript can say "point at the spot 25 feet up the axis". A height is the closest
        // approach between the cursor ray and that axis, so aiming at a plan XY resolves to whatever
        // height that sight line happens to cross — geometrically right, and impossible to write an
        // expectation for.
        if (clickHasZ)
          z = static_cast<double>(clickZ);
        const ray3d::Ray* rayPtr = nullptr;
        ray3d::Ray camRay;
        if (!CadViewIsPlan(run.st) && run.st.uiViewportWidthPx > 0.f) {
          // Off plan view a height IS pickable, and it needs the ray the camera would cast. Aimed
          // at the cursor point itself, which is what the viewport's own ray does.
          const Camera cam = CadViewCamera(run.st);
          float sx = 0.f, sy = 0.f;
          cam.WorldToScreen(static_cast<double>(x), static_cast<double>(y), z, run.st.uiViewportWidthPx,
                            run.st.uiViewportHeightPx, &sx, &sy);
          camRay = cam.ScreenRay(sx, sy, run.st.uiViewportWidthPx, run.st.uiViewportHeightPx);
          rayPtr = &camRay;
        }
        CadResolveSolidPick(run.st, ray3d::Vec3{static_cast<double>(x), static_cast<double>(y), z}, rayPtr);
      }
      SubmitViewportPick(run.st, x, y, run.log);
      break;
    case ViewportClickRoute::SelectionBox:
    case ViewportClickRoute::IdleSelection:
    case ViewportClickRoute::SelectionAccumulate:
      // Arm the first corner or close the box, exactly as the viewport does. The anchor fields are
      // written directly for the same reason BOX writes them: BeginSelectionBoxCorner also takes
      // screen coordinates, and a transcript has no screen.
      //
      // SelectionAccumulate's OTHER behavior — a click that lands on an entity toggles just that
      // one entity instead of arming a box corner — is screen-space picking (PickClosestCadEntity
      // et al., driven by pixel coordinates CadUi.cpp derives from the real viewport) with no
      // headless equivalent, the same reason idle click-select has none either (see
      // ViewportClickRoute::IdleSelection above). CLICK here always behaves like a fence corner;
      // covering entity click-select needs a manual GUI pass.
      if (!run.st.selBoxWaitingSecond) {
        run.st.selBoxWaitingSecond = true;
        run.st.selBoxAnchorX = x;
        run.st.selBoxAnchorY = y;
      } else {
        SubmitViewportPick(run.st, x, y, run.log, windowSelectionSubtract, fenceLeftToRightWindowMode);
      }
      break;
    case ViewportClickRoute::TrimPick:
      // Same fixed world tolerance TRIMPICK uses, and for the same reason (no viewport height to
      // derive one from). CLICK subsumes TRIMPICK; TRIMPICK stays for the transcripts using it.
      SubmitTrimViewportPick(run.st, x, y, 1.f, run.log);
      break;
    case ViewportClickRoute::HatchPick:
      Fail(run, "state",
           "CLICK cannot drive this command yet (HATCH boundary tracing is not wired into the "
           "driver); add the route here when a transcript needs it",
           sourceLine);
      return false;
    case ViewportClickRoute::PdfAttachInsertPoint:
      Fail(run, "state",
           "CLICK cannot drive this command yet (PDFATTACH insertion is not wired into the "
           "driver); add the route here when a transcript needs it",
           sourceLine);
      return false;
    case ViewportClickRoute::InsertBlockPick:
      SubmitInsertBlockPick(run.st, x, y, run.log);
      break;
    case ViewportClickRoute::Ignore:
      // The whole point of this verb: a command the UI does not route is a failure, not a no-op.
      Fail(run, "state",
           std::string("CLICK: the active command (") +
               AppCommandState::KindName(run.st.active) +
               ") takes no model-space viewport click in its current phase — the click would be "
               "discarded, which is the bug this verb exists to catch",
           sourceLine);
      return false;
    }
  } else if (verb == "SUBOBJECT") {
    // SUBOBJECT <x> <y> <z> [SHIFT] — one Ctrl+click on a solid's face, edge or vertex (REQ-318
    // increment 2, issue #148).
    //
    // Its own verb rather than a modifier on CLICK, because the two ask different questions. CLICK
    // routes through `ViewportClickRouteFor` to prove a command receives clicks at all; this drives
    // a SELECTION, which that router deliberately has nothing to say about — and idle click-select
    // has no headless equivalent at all (see CLICK's own note), which is exactly why the sub-object
    // click's meaning was moved OUT of `CadUi.cpp` into `SubmitSubObjectPick` before this verb was
    // written. The verb calls that shared function; it does not re-implement the rule.
    //
    // A full XYZ, not a plan XY: the target is a point on a solid's surface in three dimensions, and
    // naming it in plan alone cannot distinguish the top face of a box from the bottom one directly
    // beneath it. The ray is then the one the CAMERA would cast at that point — built exactly as
    // CLICK builds one for a prompted solid — so what is tested is the pick the user gets, not a
    // synthetic axis-aligned ray no viewport would ever produce.
    std::istringstream is(rest);
    float sx = 0.f, sy = 0.f, sz = 0.f;
    if (!(is >> sx >> sy >> sz)) {
      Fail(run, "parse",
           "SUBOBJECT expects <x> <y> <z> [<vertexTol> <edgeTol>] [SHIFT], got: " + rest, sourceLine);
      return false;
    }
    // Optional explicit tolerances, and the reason they are worth a verb argument: in the GUI these
    // are screen-derived (REQ-318 item 5) from the cursor aperture and the viewport's height in
    // pixels — neither of which a transcript has. Left to the default they come out around 3 units
    // on a 20 x 10 x 8 box, which swallows the whole precedence rule: a click in the MIDDLE of a
    // face lands within 3 units of that face's edge and the edge wins, so every assertion would be
    // about the default's size rather than about the pick. Stating them makes each case say what
    // geometry it is actually distinguishing, and makes REQ-318's "a zero tolerance means that kind
    // is never reported" expressible here as well as in the unit tests.
    bool haveTol = false;
    float tolV = 0.f, tolE = 0.f;
    {
      const std::streampos save = is.tellg();
      if (is >> tolV >> tolE) {
        haveTol = true;
      } else {
        is.clear();
        if (save != std::streampos(-1))
          is.seekg(save);
      }
    }
    bool toggle = false;
    std::string mod;
    while (is >> mod) {
      if (UpperAscii(mod) == "SHIFT") {
        toggle = true;
      } else {
        Fail(run, "parse", "SUBOBJECT: unknown modifier " + mod + " (expected SHIFT)", sourceLine);
        return false;
      }
    }
    // A projection needs a viewport size; a transcript has no window, so give it the same definite
    // one VIEWANGLES does.
    if (run.st.uiViewportWidthPx <= 0.f || run.st.uiViewportHeightPx <= 0.f) {
      run.st.uiViewportWidthPx = 1200.f;
      run.st.uiViewportHeightPx = 700.f;
    }
    const Camera subCam = CadViewCamera(run.st);
    float ssx = 0.f, ssy = 0.f;
    subCam.WorldToScreen(static_cast<double>(sx), static_cast<double>(sy), static_cast<double>(sz),
                         run.st.uiViewportWidthPx, run.st.uiViewportHeightPx, &ssx, &ssy);
    const ray3d::Ray subRay =
        subCam.ScreenRay(ssx, ssy, run.st.uiViewportWidthPx, run.st.uiViewportHeightPx);
    solidpick::Tolerance subTol;
    if (haveTol) {
      subTol.vertex = static_cast<double>(tolV);
      subTol.edge = static_cast<double>(tolE);
    } else {
      // No explicit budget: the same function the GUI calls, so an unstated transcript still gets
      // the product's own answer rather than a number invented here.
      subTol.vertex = static_cast<double>(CadOffsetEntityPickTolWorld(run.st));
      subTol.edge = subTol.vertex;
    }
    SubmitSubObjectPick(run.st, subRay, subTol, toggle, run.log);
  } else if (verb == "GIZMO") {
    // GIZMO GRAB <x> <y> <z>  |  GIZMO DROP <x> <y> <z>  |  GIZMO CANCEL
    //
    // The translate gizmo (REQ-060, issue #148 slice 4b), driven the way a mouse drives it: each
    // form casts the ray the CAMERA would cast at the named world point and hands it to
    // `SubmitGizmoClick`, exactly as SUBOBJECT does a few verbs up.
    //
    // **A ray, not a distance, and that is the whole point of the verb.** REQ-060's second
    // acceptance bullet is that "a gizmo drag and the equivalent typed command produce coordinates
    // agreeing within REQ-101". A verb that handed the command layer a ready-made offset would
    // assert that two ways of calling one function agree, which is not a fact about the product. By
    // aiming at a point and letting the skew-line solve decide the distance, what is asserted is the
    // pick, the projection and the transform together — the same three the user's drag goes through.
    //
    // GRAB fails loudly when no handle is under the ray: a transcript whose grab silently missed
    // would then assert that a move did not happen, which is exactly what a broken gizmo does.
    std::istringstream is(rest);
    std::string what;
    if (!(is >> what)) {
      Fail(run, "parse", "GIZMO expects GRAB <x> <y> <z>, DROP <x> <y> <z> or CANCEL", sourceLine);
      return false;
    }
    what = UpperAscii(what);
    if (what == "CANCEL") {
      CancelGizmoDrag(run.st);
      return true;
    }
    if (what != "GRAB" && what != "DROP") {
      Fail(run, "parse", "GIZMO: unknown form " + what + " (expected GRAB, DROP or CANCEL)", sourceLine);
      return false;
    }
    float gx = 0.f, gy = 0.f, gz = 0.f;
    if (!(is >> gx >> gy >> gz)) {
      Fail(run, "parse", "GIZMO " + what + " expects <x> <y> <z>, got: " + rest, sourceLine);
      return false;
    }
    // A projection needs a viewport size; a transcript has no window, so give it the same definite
    // one VIEWANGLES and SUBOBJECT do. It also fixes the handle length and the grab aperture, both
    // of which are screen-derived — so a transcript's gizmo is the same size as the user's.
    if (run.st.uiViewportWidthPx <= 0.f || run.st.uiViewportHeightPx <= 0.f) {
      run.st.uiViewportWidthPx = 1200.f;
      run.st.uiViewportHeightPx = 700.f;
    }
    const Camera gizCam = CadViewCamera(run.st);
    float gsx = 0.f, gsy = 0.f;
    gizCam.WorldToScreen(static_cast<double>(gx), static_cast<double>(gy), static_cast<double>(gz),
                         run.st.uiViewportWidthPx, run.st.uiViewportHeightPx, &gsx, &gsy);
    const ray3d::Ray gizRay =
        gizCam.ScreenRay(gsx, gsy, run.st.uiViewportWidthPx, run.st.uiViewportHeightPx);
    const double gizTol = static_cast<double>(CadSnap::WorldToleranceFromPixels(
        run.st.uiViewportHeightPx,
        (1.f / std::max(run.st.viewportZoom, 1.e-9f)) * 50.f, kGizmoHandleGrabPx));
    if (what == "GRAB" && run.st.gizmoDragActive) {
      Fail(run, "state", "GIZMO GRAB while a drag is already armed - DROP or CANCEL it first",
           sourceLine);
      return false;
    }
    if (what == "DROP" && !run.st.gizmoDragActive) {
      Fail(run, "state", "GIZMO DROP with no armed drag - GRAB a handle first", sourceLine);
      return false;
    }
    if (!SubmitGizmoClick(run.st, gizRay, gizTol, run.log)) {
      Fail(run, "pick",
           "GIZMO GRAB found no handle under (" + std::to_string(gx) + ", " + std::to_string(gy) +
               ", " + std::to_string(gz) + ") - is anything selected, and is the point on a handle?",
           sourceLine);
      return false;
    }
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
    // Each corner's ELEVATION, solved on the active work plane the way the viewport's own plan-view
    // branch does. Without these both corners default to Z = 0, and the fence is then projected from
    // a plane the drag never happened on — which is the defect this verb exists to be able to catch,
    // so leaving them at zero would build the bug into the test.
    {
      const ray3d::Plane wp = CadActiveWorkPlane(run.st);
      auto planeZ = [&](float x, float y) {
        return static_cast<float>(
            ucs::WorkPlaneZAt(wp, static_cast<double>(x), static_cast<double>(y)));
      };
      run.st.selBoxAnchorZ = planeZ(x0, y0);
      run.st.uiCursorWorldZ = planeZ(x1, y1);
    }
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
  } else if (verb == "LAYOUT") {
    // LAYOUT NEW | LAYOUT MODEL — switch the active space (REQ-037/ADR-009). Like CLIPCOPY above,
    // this is a REQ-203 gap: AddPaperLayout/SetActiveSpace are bound to the layout tab bar in
    // CadUi.cpp and reachable no other way, so a transcript could not otherwise put a paper layout
    // in play to exercise the paper-space commit path (issue #84).
    const std::string arg = UpperAscii(Trim(rest));
    if (arg == "MODEL") {
      SetActiveSpace(run.st, kModelSpaceIndex);
    } else if (arg == "NEW" || arg.empty()) {
      const int idx = AddPaperLayout(run.st);
      SetActiveSpace(run.st, idx);
    } else {
      Fail(run, "parse", "LAYOUT expects NEW or MODEL, got: " + rest, sourceLine);
      return false;
    }
  } else if (verb == "VIEWPORT") {
    // VIEWPORT <x0In> <y0In> <x1In> <y1In> — create a paper-space viewport on the active layout.
    //
    // A REQ-203 gap of the same shape as LAYOUT and CLIPCOPY above: RECTVP is driven by two clicks
    // in the PAPER click block, which `ViewportClickRouteFor` deliberately routes to `Ignore` (it is
    // model-space routing), so CLICK cannot reach it. `AddViewportRect` is the same entry point the
    // UI's second click calls.
    if (run.st.activeSpaceIndex < 0 ||
        static_cast<size_t>(run.st.activeSpaceIndex) >= run.st.paperLayouts.size()) {
      Fail(run, "state", "VIEWPORT needs a paper layout active — use SPACE PAPER or LAYOUT NEW first",
           sourceLine);
      return false;
    }
    std::istringstream is(rest);
    float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
    if (!(is >> x0 >> y0 >> x1 >> y1)) {
      Fail(run, "parse", "VIEWPORT expects four paper-inch coordinates, got: " + rest, sourceLine);
      return false;
    }
    const int vpIx = AddViewportRect(run.st, run.st.activeSpaceIndex, x0, y0, x1, y1);
    if (vpIx < 0) {
      Fail(run, "state", "VIEWPORT: the rectangle was rejected (degenerate?)", sourceLine);
      return false;
    }
    run.st.selectedViewportIndex = vpIx;  // MSPACE acts on the SELECTED viewport
    run.log.push_back("[driver] VIEWPORT " + std::to_string(vpIx));
  } else if (verb == "CLAYER") {
    // CLAYER <name> — set the current layer, creating it if new. The Layer manager is a DIALOG
    // (`LAYER` opens it), so there is no typed route to this and a transcript could not otherwise put
    // geometry on a named layer — which REQ-123's per-viewport freeze test needs.
    const std::string name = Trim(rest);
    if (name.empty()) {
      Fail(run, "parse", "CLAYER expects a layer name", sourceLine);
      return false;
    }
    run.st.currentLayer = name;
    // Registers the name in the drawing's layer table, exactly as the Layer manager's OK does.
    SyncDrawingLayerTableWithGeometry(run.st);
  } else if (verb == "VIEWANGLES") {
    // VIEWANGLES <azimuthDeg> <elevationDeg> — orbit the model view.
    //
    // Here for the same reason CLAYER and LAYERSTATE are: the only routes to these in the product
    // are the ViewCube and a mouse drag, so without this verb NO transcript can exercise anything
    // that only happens once the view is orbited — and that is a whole class of behaviour, because
    // picking, snapping and box-selection all switch from the plan-view XY path to a camera
    // PROJECTION there (REQ-058). A defect that only appears off plan view had no failing test
    // available to it, which is exactly how the Z = 0 fence projection survived.
    std::istringstream is(rest);
    float az = 0.f;
    float el = 90.f;
    if (!(is >> az >> el)) {
      Fail(run, "parse", "VIEWANGLES expects <azimuthDeg> <elevationDeg>", sourceLine);
      return false;
    }
    run.st.viewportAzimuthDeg = az;
    run.st.viewportElevationDeg = el;
    // The projection needs a viewport size; a transcript has no window, so give it a definite one.
    if (run.st.uiViewportWidthPx <= 0.f || run.st.uiViewportHeightPx <= 0.f) {
      run.st.uiViewportWidthPx = 1200.f;
      run.st.uiViewportHeightPx = 700.f;
    }
  } else if (verb == "LAYERSTATE") {
    // LAYERSTATE <name> ON|OFF|FREEZE|THAW — flip a layer's visibility, exactly as the Layer
    // manager's checkboxes do.
    //
    // Here for the same reason CLAYER is: the Layer manager is a DIALOG, so there is no typed route
    // to this, and without it a transcript cannot state the rule every entity kind is held to —
    // that what is invisible is also unclickable (REQ-084 (d)). A visibility filter that silently
    // stopped working would otherwise have no failing test available to it.
    std::string stateRaw;
    const std::string name = Trim(FirstWord(rest, &stateRaw));
    std::string stateArgRaw;
    const std::string state = UpperAscii(Trim(FirstWord(Trim(stateRaw), &stateArgRaw)));
    if (name.empty() || state.empty()) {
      Fail(run, "parse", "LAYERSTATE expects <name> ON|OFF|FREEZE|THAW|COLOR <name>", sourceLine);
      return false;
    }
    CadLayerRow* row = nullptr;
    for (CadLayerRow& r : run.st.drawingLayerTable) {
      if (UpperAscii(r.name) == UpperAscii(name)) {
        row = &r;
        break;
      }
    }
    if (!row) {
      Fail(run, "state", "LAYERSTATE: no layer named " + name, sourceLine);
      return false;
    }
    if (state == "ON")
      row->on = true;
    else if (state == "OFF")
      row->on = false;
    else if (state == "FREEZE")
      row->frozen = true;
    else if (state == "THAW")
      row->frozen = false;
    else if (state == "COLOR") {
      // LAYERSTATE <name> COLOR <colorname> — the Layer manager's swatch. Here so a transcript can
      // give two layers different resolved colours, which is what GitHub #194's draw-batch
      // coalescing splits on (same colour/lineweight merges, a colour difference does not).
      const std::string colorName = Trim(stateArgRaw);
      if (colorName.empty()) {
        Fail(run, "parse", "LAYERSTATE ... COLOR expects a colour name", sourceLine);
        return false;
      }
      row->color = colorName;
    } else {
      Fail(run, "parse", "LAYERSTATE: unknown state " + state + " (ON|OFF|FREEZE|THAW|COLOR)", sourceLine);
      return false;
    }
  } else if (verb == "HOVER") {
    // HOVER <x> <y> [z] — move the cursor without clicking, so the live preview can be asserted.
    //
    // The preview is the whole point of the feature and it is the half a CLICK cannot show: by the
    // time a click has landed the value is committed and the rubber is gone. This resolves the pick
    // exactly as the viewport does each frame and stops there.
    std::istringstream is(rest);
    float hx = 0.f;
    float hy = 0.f;
    if (!(is >> hx >> hy)) {
      Fail(run, "parse", "HOVER expects <x> <y> [z]", sourceLine);
      return false;
    }
    float hz = 0.f;
    bool hasHz = false;
    if (is >> hz)
      hasHz = true;
    double hzWorld = ucs::WorkPlaneZAt(CadActiveWorkPlane(run.st), static_cast<double>(hx),
                                       static_cast<double>(hy));
    if (hasHz)
      hzWorld = static_cast<double>(hz);
    ray3d::Ray hray;
    const ray3d::Ray* hrayPtr = nullptr;
    if (!CadViewIsPlan(run.st) && run.st.uiViewportWidthPx > 0.f) {
      const Camera hcam = CadViewCamera(run.st);
      float hsx = 0.f, hsy = 0.f;
      hcam.WorldToScreen(static_cast<double>(hx), static_cast<double>(hy), hzWorld,
                         run.st.uiViewportWidthPx, run.st.uiViewportHeightPx, &hsx, &hsy);
      hray = hcam.ScreenRay(hsx, hsy, run.st.uiViewportWidthPx, run.st.uiViewportHeightPx);
      hrayPtr = &hray;
    }
    CadResolveSolidPick(run.st, ray3d::Vec3{static_cast<double>(hx), static_cast<double>(hy), hzWorld},
                        hrayPtr);
    run.hoverX = static_cast<double>(hx);
    run.hoverY = static_cast<double>(hy);
  } else if (verb == "VIEWANGLES") {
    // VIEWANGLES <azimuthDeg> <elevationDeg> — orbit the model view.
    //
    // Here for the same reason CLAYER and LAYERSTATE are: the only routes to this in the product are
    // the ViewCube and a mouse drag, so without it no transcript can exercise anything that only
    // happens once the view is orbited. For the solid commands that is not a nicety — a HEIGHT is
    // read off the cursor RAY, and in plan view there is no ray and no height to read.
    std::istringstream is(rest);
    float az = 0.f;
    float el = 90.f;
    if (!(is >> az >> el)) {
      Fail(run, "parse", "VIEWANGLES expects <azimuthDeg> <elevationDeg>", sourceLine);
      return false;
    }
    run.st.viewportAzimuthDeg = az;
    run.st.viewportElevationDeg = el;
    // The projection needs a viewport size; a transcript has no window, so give it a definite one.
    if (run.st.uiViewportWidthPx <= 0.f || run.st.uiViewportHeightPx <= 0.f) {
      run.st.uiViewportWidthPx = 1200.f;
      run.st.uiViewportHeightPx = 700.f;
    }
  } else if (verb == "UCSNAMED") {
    // UCSNAMED RESTORE|DELETE <name> — the View Manager's two named-UCS buttons.
    //
    // `UCS Named` saves and only saves; restoring and deleting a saved frame are the View Manager's,
    // because choosing among saved frames wants a list a command prompt cannot show. So there is no
    // typed route to them, exactly as CLAYER above has none, and this calls the same
    // RestoreNamedUcs / DeleteNamedUcs the dialog's buttons call rather than a second copy.
    std::string name;
    const std::string what = UpperAscii(FirstWord(rest, &name));
    name = Trim(name);
    if (name.empty() || (what != "RESTORE" && what != "DELETE")) {
      Fail(run, "parse", "UCSNAMED expects RESTORE <name> | DELETE <name>, got: " + rest, sourceLine);
      return false;
    }
    if (what == "RESTORE")
      RestoreNamedUcs(run.st, name, run.log);
    else
      DeleteNamedUcs(run.st, name, run.log);
  } else if (verb == "VPFREEZE") {
    // VPFREEZE <layer> — freeze a layer in the SELECTED viewport (REQ-028 / REQ-046).
    //
    // The VPFREEZE command picks ENTITIES inside a floating viewport and freezes their layers, and
    // `ViewportClickRouteFor` routes it to `Ignore` in model space by design, so CLICK cannot reach
    // it. This calls the same pure `FreezeLayerInViewport` the command ends up calling.
    if (run.st.activeSpaceIndex < 0 ||
        static_cast<size_t>(run.st.activeSpaceIndex) >= run.st.paperLayouts.size()) {
      Fail(run, "state", "VPFREEZE needs a paper layout active", sourceLine);
      return false;
    }
    const std::string name = Trim(rest);
    if (name.empty()) {
      Fail(run, "parse", "VPFREEZE expects a layer name", sourceLine);
      return false;
    }
    PaperLayout& FL = run.st.paperLayouts[static_cast<size_t>(run.st.activeSpaceIndex)];
    if (run.st.selectedViewportIndex < 0 ||
        static_cast<size_t>(run.st.selectedViewportIndex) >= FL.viewports.size()) {
      Fail(run, "state", "VPFREEZE: no viewport selected — use VIEWPORT or VPSELECT first", sourceLine);
      return false;
    }
    FreezeLayerInViewport(FL.viewports[static_cast<size_t>(run.st.selectedViewportIndex)], name);
  } else if (verb == "VPSELECT") {
    // VPSELECT <n> — choose which viewport MSPACE will enter. Selection is a click on the viewport
    // border in the UI, which has no headless equivalent; this is the field that click writes.
    // Needed for issue #100's "consistent when multiple viewports exist" condition.
    if (run.st.activeSpaceIndex < 0 ||
        static_cast<size_t>(run.st.activeSpaceIndex) >= run.st.paperLayouts.size()) {
      Fail(run, "state", "VPSELECT needs a paper layout active", sourceLine);
      return false;
    }
    std::istringstream is(rest);
    int n = -1;
    if (!(is >> n)) {
      Fail(run, "parse", "VPSELECT expects a viewport index, got: " + rest, sourceLine);
      return false;
    }
    const PaperLayout& L = run.st.paperLayouts[static_cast<size_t>(run.st.activeSpaceIndex)];
    if (n < 0 || static_cast<size_t>(n) >= L.viewports.size()) {
      Fail(run, "state", "VPSELECT: no viewport " + std::to_string(n) + " on this layout", sourceLine);
      return false;
    }
    run.st.selectedViewportIndex = n;
  } else if (verb == "DUMP") {
    // DUMP LABELS — every survey point's label box, in world units, beside the point it labels.
    //
    // Not an assertion. Label PLACEMENT is a relationship between two rectangles, and no
    // `EXPECT <count>` can express it while a `.gs` diff shows it only as eight bare floats. This
    // prints the two numbers that decide whether the placement rule holds — the box's offset from
    // its point, and the box's size — so that a rule like "the anchor does not move when the text
    // gets longer" can be read straight off the log.
    std::string what = UpperAscii(Trim(rest));
    // DUMP BREAKSPAN <x> <y> — what BREAK's live preview says it would remove, with the cursor at
    // (x,y). REQ-103 step 4 / TASK-101.
    //
    // Unlike the two below, this one IS meant to be asserted on: the summary line carries the span's
    // endpoints and total length, all hand-computable, and `EXPECT LOG` can match them. The preview
    // answers "which half of this object is about to disappear", and on a closed entity that answer
    // flips with click order — so it is exactly the kind of thing that can be confidently wrong and
    // look completely plausible on screen.
    if (what.rfind("BREAKSPAN", 0) == 0) {
      std::istringstream is(Trim(rest).substr(std::string("BREAKSPAN").size()));
      float x = 0.f;
      float y = 0.f;
      if (!(is >> x >> y)) {
        Fail(run, "parse", "DUMP BREAKSPAN expects two world coordinates, got: " + rest, sourceLine);
        return false;
      }
      // Both halves of what main.cpp hands the renderer: the span and its two markers. Reported
      // separately so the span's length can be asserted against a hand-computed figure without the
      // markers' decoration in the total.
      std::vector<float> span;
      std::vector<float> markers;
      BuildBreakRemovalPreview(run.st, x, y, -1.f, &span, &markers);
      const size_t nSeg = span.size() / 6;
      double total = 0.0;
      for (size_t s = 0; s < nSeg; ++s) {
        const float* v = &span[s * 6];
        total += std::hypot(static_cast<double>(v[3] - v[0]), static_cast<double>(v[4] - v[1]));
      }
      char buf[256];
      if (nSeg == 0) {
        std::snprintf(buf, sizeof(buf), "[dump] breakspan: 0 segments (nothing removed)");
      } else {
        std::snprintf(buf, sizeof(buf),
                      "[dump] breakspan: %zu segments, from %.3f,%.3f to %.3f,%.3f, length %.3f", nSeg,
                      static_cast<double>(span[0]), static_cast<double>(span[1]),
                      static_cast<double>(span[(nSeg - 1) * 6 + 3]),
                      static_cast<double>(span[(nSeg - 1) * 6 + 4]), total);
      }
      run.log.push_back(buf);
      char buf2[192];
      std::snprintf(buf2, sizeof(buf2), "[dump] breakmarkers: %zu segments", markers.size() / 6);
      run.log.push_back(buf2);
      return true;
    }
    if (what != "LABELS" && what != "SURFACES") {
      Fail(run, "parse", "DUMP expects LABELS, SURFACES, or BREAKSPAN <x> <y>, got: " + rest, sourceLine);
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
      run.log.push_back(
          "[dump] surface | style | tris | border | minor | major (segments) | bandtris | arrowsegs (REQ-072)");
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
          size_t bandTris = 0;
          for (const auto& b : it->bandTriangleBuffers)
            bandTris += b.size() / 9;
          size_t arrowSegs = 0;
          for (const auto& b : it->arrowLineBuffers)
            arrowSegs += b.size() / 6;
          std::snprintf(buf, sizeof buf, "[dump] %s | %s | %zu | %zu | %zu | %zu | %zu | %zu", s.name.c_str(),
                        it->style.name.c_str(), it->triangleEdges.size() / 6,
                        it->borderEdges.size() / 6, it->minorContours.size() / 6,
                        it->majorContours.size() / 6, bandTris, arrowSegs);
        }
        run.log.push_back(buf);
      }
      run.log.push_back("[dump] batches handed to the renderer: " +
                        std::to_string(run.st.surfaceDisplayGeometry.lines.size()) +
                        ", band batches: " + std::to_string(run.st.surfaceDisplayGeometry.bandTriangles.size()) +
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
    if (what == "ANNKIND") {
      // EXPECT ANNKIND <index> <tag> — issue #125: DIMANGULAR must still be DimAngular after OPEN.
      int ix = -1;
      std::string want;
      std::istringstream is(arg);
      if (!(is >> ix >> want) || ix < 0) {
        Fail(run, "parse", "EXPECT ANNKIND needs index and kind tag, got: " + arg, sourceLine);
        return false;
      }
      if (static_cast<size_t>(ix) >= run.st.cadAnnotations.size()) {
        Fail(run, "state", "EXPECT ANNKIND: annotation index out of range", sourceLine);
        return false;
      }
      const char* got = AnnotationKindTag(run.st.cadAnnotations[static_cast<size_t>(ix)].kind);
      if (want != got) {
        Fail(run, "expect",
             "ANNKIND: annotation " + std::to_string(ix) + " expected " + want + ", got " + got, sourceLine);
        return false;
      }
    } else if (what == "VPFRAME") {
      // EXPECT VPFRAME <centreX> <centreY> <scaleModelPerPaperIn> — the FLOATING viewport's framing
      // (REQ-123 / GitHub #100).
      //
      // A viewport's view is three numbers, and asserting the LOG line instead would assert a
      // `%.6g` rendering of them — which turns a float rounding at the sixth significant digit into
      // a red test about nothing. Compared with a relative tolerance for the same reason.
      std::istringstream vs(arg);
      double wantCx = 0., wantCy = 0., wantScale = 0.;
      if (!(vs >> wantCx >> wantCy >> wantScale)) {
        Fail(run, "parse", "EXPECT VPFRAME needs centreX centreY scale, got: " + arg, sourceLine);
        return false;
      }
      if (!InFloatingModelSpace(run.st) || run.st.floatingViewportLayout < 0 ||
          static_cast<size_t>(run.st.floatingViewportLayout) >= run.st.paperLayouts.size()) {
        Fail(run, "state", "EXPECT VPFRAME: not in floating model space — use CMD MSPACE first", sourceLine);
        return false;
      }
      const PaperLayout& FL = run.st.paperLayouts[static_cast<size_t>(run.st.floatingViewportLayout)];
      if (run.st.floatingViewportIndex < 0 ||
          static_cast<size_t>(run.st.floatingViewportIndex) >= FL.viewports.size()) {
        Fail(run, "state", "EXPECT VPFRAME: the floating viewport index is out of range", sourceLine);
        return false;
      }
      const Viewport& FV = FL.viewports[static_cast<size_t>(run.st.floatingViewportIndex)];
      const double gotCx = FV.modelCenterX;
      const double gotCy = FV.modelCenterY;
      const double gotScale = static_cast<double>(FV.scaleModelPerPaperIn);
      auto near = [](double got, double want) {
        return std::fabs(got - want) <= 1e-4 * std::max(1.0, std::fabs(want));
      };
      if (!near(gotCx, wantCx) || !near(gotCy, wantCy) || !near(gotScale, wantScale)) {
        char vb[256];
        std::snprintf(vb, sizeof(vb), "VPFRAME: expected centre %.6g, %.6g scale %.6g; got %.6g, %.6g scale %.6g",
                      wantCx, wantCy, wantScale, gotCx, gotCy, gotScale);
        Fail(run, "expect", vb, sourceLine);
        return false;
      }
    } else if (what == "SAMEFILE" || what == "DIFFERENTFILE") {
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
      const std::string saRaw((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
      const std::string sbRaw((std::istreambuf_iterator<char>(fb)), std::istreambuf_iterator<char>());
      std::string saPayload;
      std::string sbPayload;
      const std::string& sa =
          TryGoSurveyDwgPayloadFromBytes(saRaw, saPayload) ? saPayload : saRaw;
      const std::string& sb =
          TryGoSurveyDwgPayloadFromBytes(sbRaw, sbPayload) ? sbPayload : sbRaw;
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
    } else if (what == "FILECONTAINS" || what == "FILELACKS") {
      // EXPECT FILECONTAINS <path> "<text>"   /   EXPECT FILELACKS <path> "<text>"
      //
      // A literal substring test over a saved document, reading the `.gs` JSON out of a DWG trailer
      // exactly as SAMEFILE above does, so it works on either extension.
      //
      // It exists for a shape of acceptance condition no count and no byte comparison can state:
      // that a key is ABSENT. REQ-312 requires a flat drawing to save with NO plane-normal key at
      // all — that omission is the whole mechanism by which a legacy drawing re-saves byte for byte
      // — and a save/reopen/re-save round trip passes just as happily with the key written on every
      // circle in the file. FILELACKS is the half that can fail.
      const std::string expanded = ExpandVars(run, arg);
      std::istringstream fs3(expanded);
      std::string path;
      if (!(fs3 >> path)) {
        Fail(run, "parse", "EXPECT " + what + " needs <path> then the text to look for", sourceLine);
        return false;
      }
      std::string needle = Trim(expanded.substr(std::min(expanded.size(), expanded.find(path) + path.size())));
      if (needle.size() >= 2 && needle.front() == '"' && needle.back() == '"')
        needle = needle.substr(1, needle.size() - 2);
      if (needle.empty()) {
        Fail(run, "parse", "EXPECT " + what + ": the text to look for is empty", sourceLine);
        return false;
      }
      std::ifstream ff(path, std::ios::binary);
      if (!ff) {
        Fail(run, "io", "EXPECT " + what + ": cannot open " + path, sourceLine);
        return false;
      }
      const std::string rawBytes((std::istreambuf_iterator<char>(ff)), std::istreambuf_iterator<char>());
      std::string payload;
      const std::string& hay = TryGoSurveyDwgPayloadFromBytes(rawBytes, payload) ? payload : rawBytes;
      const bool found = hay.find(needle) != std::string::npos;
      if (what == "FILECONTAINS" && !found) {
        Fail(run, "expect", "FILECONTAINS: " + path + " does not contain: " + needle, sourceLine);
        return false;
      }
      if (what == "FILELACKS" && found) {
        Fail(run, "expect", "FILELACKS: " + path + " contains: " + needle, sourceLine);
        return false;
      }
    } else if (what == "LAYERSDEFINED") {
      // EXPECT LAYERSDEFINED <dxf> — every layer an ENTITIES-section group 8 names has an
      // AcDbLayerTableRecord in the same file.
      //
      // Issue #72: `ExportDxfFile_Impl`'s layer-name sweep collected names from lines, circles,
      // annotations, survey points and the drawing's own layer table — but not from
      // `userPolylineAttrs` or `cadFilledRegionAttrs`. A polyline or a hatch was therefore the one
      // entity that could write a layer reference the LAYER table did not define. An entity naming
      // an undefined layer is an invalid file, and it was written silently.
      //
      // This reads the WRITTEN FILE rather than the document, which is the point: the defect is
      // entirely in what the exporter emits, and a document-side assertion cannot see it. It is
      // also why the existing `dxf-export-stable` round trip misses it — that oracle compares two
      // exports, and both name the same undefined layer.
      const std::string path = ExpandVars(run, Trim(arg));
      std::ifstream df(path, std::ios::binary);
      if (!df) {
        Fail(run, "io", "EXPECT LAYERSDEFINED: cannot open " + path, sourceLine);
        return false;
      }
      // A DXF is a flat stream of (group code, value) line pairs. Section is tracked so a group 8
      // inside TABLES/BLOCKS is not mistaken for an entity's layer reference, and so the LAYER
      // records read are the real table rather than anything else carrying a group 2.
      std::set<std::string> defined;
      std::set<std::string> referenced;
      std::string section;
      std::string tableName;
      bool inLayerRecord = false;
      std::string codeLine;
      std::string valueLine;
      while (std::getline(df, codeLine) && std::getline(df, valueLine)) {
        const std::string code = Trim(codeLine);
        const std::string value = Trim(valueLine);
        if (code == "0") {
          if (value == "SECTION") {
            section.clear();  // the following group 2 names it
          } else if (value == "ENDSEC") {
            section.clear();
            tableName.clear();
          } else if (value == "TABLE") {
            tableName.clear();  // likewise
          }
          inLayerRecord = (section == "TABLES" && tableName == "LAYER" && value == "LAYER");
          continue;
        }
        if (code == "2") {
          if (section.empty())
            section = value;            // SECTION's name: HEADER / TABLES / BLOCKS / ENTITIES
          else if (section == "TABLES" && tableName.empty())
            tableName = value;          // TABLE's name: VPORT / LTYPE / LAYER / ...
          else if (inLayerRecord)
            defined.insert(value);      // an AcDbLayerTableRecord's own name
          continue;
        }
        if (code == "8" && section == "ENTITIES")
          referenced.insert(value.empty() ? std::string("0") : value);
      }
      std::vector<std::string> undefined;
      for (const std::string& r : referenced) {
        if (defined.find(r) == defined.end())
          undefined.push_back(r);
      }
      if (!undefined.empty()) {
        std::string names;
        for (size_t i = 0; i < undefined.size(); ++i)
          names += (i ? ", " : "") + undefined[i];
        Fail(run, "expect",
             "LAYERSDEFINED: " + std::to_string(undefined.size()) +
                 " layer(s) referenced by an entity but absent from the LAYER table: " + names +
                 " (" + std::to_string(defined.size()) + " defined, " +
                 std::to_string(referenced.size()) + " referenced)",
             sourceLine);
        return false;
      }
      if (referenced.empty()) {
        // Vacuity guard, in the spirit of EXPECT DIFFERENTFILE: a file with no entity at all
        // satisfies "every referenced layer is defined" perfectly while proving nothing. If the
        // export dropped everything (see #63), this check must say so rather than pass.
        Fail(run, "expect",
             "LAYERSDEFINED: no entity in the file names a layer at all — nothing was checked",
             sourceLine);
        return false;
      }
    } else if (what == "HANDLESUNIQUE") {
      // EXPECT HANDLESUNIQUE <dxf> — no two records in the file share a group-5 handle, and
      // $HANDSEED exceeds every handle the file uses.
      //
      // Issue #71: `ExportDxfFile_Impl` reserves a block of entity handles by COUNTING the entities
      // it is about to write, and that count omitted polylines and filled regions. Every OBJECTS
      // handle derives from the sum, so the running `entHandle++` walks straight through the OBJECTS
      // block and both own the same handles. A handle is the identity a DXF uses for every internal
      // reference — group 330 ownership, dictionary entries, XDATA links — so duplicating them is
      // not cosmetic, and a $HANDSEED below the highest handle in use means the next handle a
      // consumer allocates collides again.
      //
      // Why this reads the written file rather than comparing two exports: the collision is
      // DETERMINISTIC. `dxf-export-stable` exports twice and compares, and both passes collide
      // identically, so a differential oracle reports success on a file AutoCAD would reject. That
      // is the whole reason this verb exists rather than a round-trip step.
      const std::string path = ExpandVars(run, Trim(arg));
      std::ifstream df(path, std::ios::binary);
      if (!df) {
        Fail(run, "io", "EXPECT HANDLESUNIQUE: cannot open " + path, sourceLine);
        return false;
      }
      // $HANDSEED is a HEADER variable that happens to be carried on group 5. It is the declared
      // ceiling, not a handle anything owns, so it is pulled out here rather than counted as one.
      std::set<std::string> seen;
      std::set<std::string> dupes;
      std::string seedHex;
      std::string section;
      std::string lastVar;
      unsigned long long maxHandle = 0;
      size_t handleCount = 0;
      std::string codeLine;
      std::string valueLine;
      while (std::getline(df, codeLine) && std::getline(df, valueLine)) {
        const std::string code = Trim(codeLine);
        const std::string value = Trim(valueLine);
        if (code == "0") {
          if (value == "SECTION" || value == "ENDSEC")
            section.clear();  // the following group 2 names the next one
          continue;
        }
        if (code == "2" && section.empty()) {
          section = value;
          continue;
        }
        if (code == "9") {
          lastVar = value;
          continue;
        }
        if (code != "5")
          continue;
        if (section == "HEADER" && lastVar == "$HANDSEED") {
          seedHex = value;
          continue;
        }
        if (!seen.insert(value).second)
          dupes.insert(value);
        ++handleCount;
        const unsigned long long h = std::strtoull(value.c_str(), nullptr, 16);
        if (h > maxHandle)
          maxHandle = h;
      }
      if (handleCount == 0) {
        // Vacuity guard, in the spirit of EXPECT DIFFERENTFILE: a file holding no handle at all
        // satisfies "all handles are unique" perfectly while proving nothing.
        Fail(run, "expect", "HANDLESUNIQUE: the file carries no group-5 handle — nothing was checked",
             sourceLine);
        return false;
      }
      if (!dupes.empty()) {
        std::string names;
        size_t i = 0;
        for (const std::string& d : dupes)
          names += (i++ ? ", " : "") + d;
        Fail(run, "expect",
             "HANDLESUNIQUE: " + std::to_string(dupes.size()) + " handle(s) used more than once: " +
                 names + " (" + std::to_string(handleCount) + " handles, " +
                 std::to_string(seen.size()) + " distinct)",
             sourceLine);
        return false;
      }
      if (seedHex.empty()) {
        Fail(run, "expect", "HANDLESUNIQUE: the file declares no $HANDSEED", sourceLine);
        return false;
      }
      const unsigned long long seed = std::strtoull(seedHex.c_str(), nullptr, 16);
      if (seed <= maxHandle) {
        Fail(run, "expect",
             "HANDLESUNIQUE: $HANDSEED is " + seedHex + " but handle " +
                 std::to_string(maxHandle) + " (decimal) is in use — the seed must exceed every "
                 "handle in the file, or the next handle a consumer allocates collides",
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
    } else if (what == "PROJECTION") {
      // EXPECT PROJECTION <ORTHOGRAPHIC|PERSPECTIVE> — the LIVE projection (REQ-309).
      //
      // Needed because `EXPECT LOG` is a substring match over the WHOLE accumulated log, so once a
      // transcript has switched to perspective even once, every later `EXPECT LOG "Projection =
      // Perspective"` passes whether or not it is still true. That makes exactly the assertions
      // this requirement most needs — the ones after a save/reopen and after restoring a named
      // view — silently vacuous. Proven, not assumed: suppressing the named-view projection write
      // in `GsIo` left the log-based transcript green.
      std::string wantS = Trim(arg);
      for (char& c : wantS)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      bool wantPersp = false;
      if (wantS == "PERSPECTIVE" || wantS == "P")
        wantPersp = true;
      else if (wantS == "ORTHOGRAPHIC" || wantS == "ORTHO" || wantS == "O")
        wantPersp = false;
      else {
        Fail(run, "parse", "EXPECT PROJECTION needs ORTHOGRAPHIC or PERSPECTIVE", sourceLine);
        return false;
      }
      const bool isPersp = run.st.viewportProjection == Camera::Projection::Perspective;
      if (isPersp != wantPersp) {
        Fail(run, "expect",
             std::string("EXPECT PROJECTION: is ") + (isPersp ? "Perspective" : "Orthographic") +
                 ", expected " + (wantPersp ? "Perspective" : "Orthographic"),
             sourceLine);
        return false;
      }
    } else if (what == "CROSSHAIR3D") {
      // EXPECT CROSSHAIR3D <ON|OFF> — the LIVE setting (REQ-310). Same reason as EXPECT PROJECTION:
      // EXPECT LOG matches the whole accumulated log, so it cannot assert a toggle's CURRENT value
      // once that value has been reported at least once.
      std::string wantS = Trim(arg);
      for (char& c : wantS)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      bool want = false;
      if (wantS == "ON" || wantS == "1")
        want = true;
      else if (wantS == "OFF" || wantS == "0")
        want = false;
      else {
        Fail(run, "parse", "EXPECT CROSSHAIR3D needs ON or OFF", sourceLine);
        return false;
      }
      if (run.st.viewportCrosshair3d != want) {
        Fail(run, "expect",
             std::string("EXPECT CROSSHAIR3D: is ") + (run.st.viewportCrosshair3d ? "ON" : "OFF") +
                 ", expected " + (want ? "ON" : "OFF"),
             sourceLine);
        return false;
      }
    } else if (what == "FOV") {
      // EXPECT FOV <degrees> — the LIVE field of view (REQ-309). Same reason as EXPECT PROJECTION.
      std::istringstream is(arg);
      double want = 0.0;
      if (!(is >> want)) {
        Fail(run, "parse", "EXPECT FOV needs <degrees>", sourceLine);
        return false;
      }
      const double got = static_cast<double>(run.st.viewportFovDeg);
      if (std::fabs(got - want) > 1e-3) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "EXPECT FOV: is %.6g, expected %.6g", got, want);
        Fail(run, "expect", buf, sourceLine);
        return false;
      }
    } else if (what == "SOLIDPROPS") {
      // EXPECT SOLIDPROPS <index> <volume> <area> <vertices> <edges> <faces>
      //
      // One verb for all six numbers, because they are one claim: this solid is the shape it was
      // asked for. Splitting them into six verbs would let a transcript assert the volume of a solid
      // whose topology had silently changed, which is exactly the failure that must not pass.
      //
      // Volume and area are compared to a RELATIVE 1e-6, not to REQ-101's 0.01: these come from
      // closed-form integrals over analytic faces (REQ-313), so a figure that merely scraped under
      // 0.01 would mean something had been faceted or narrowed on the way through.
      std::istringstream is(arg);
      long idx = -1;
      double wantVol = 0.0;
      double wantArea = 0.0;
      long wantV = 0;
      long wantE = 0;
      long wantF = 0;
      if (!(is >> idx >> wantVol >> wantArea >> wantV >> wantE >> wantF)) {
        Fail(run, "parse",
             "EXPECT SOLIDPROPS needs <index> <volume> <area> <vertices> <edges> <faces>", sourceLine);
        return false;
      }
      if (idx < 0 || static_cast<size_t>(idx) >= run.st.cadSolids.size() ||
          !run.st.cadSolids[static_cast<size_t>(idx)]) {
        Fail(run, "expect",
             "EXPECT SOLIDPROPS: no solid at index " + std::to_string(idx) + " (there are " +
                 std::to_string(run.st.cadSolids.size()) + ")",
             sourceLine);
        return false;
      }
      const brep::Solid& s = *run.st.cadSolids[static_cast<size_t>(idx)];
      const brep::Problem why = brep::Validate(s);
      if (why != brep::Problem::Ok) {
        Fail(run, "expect",
             std::string("EXPECT SOLIDPROPS: solid ") + std::to_string(idx) + " is not valid — " +
                 brep::ProblemText(why),
             sourceLine);
        return false;
      }
      const brep::MassProperties mp = brep::ComputeMassProperties(s);
      const double volTol = std::max(1e-6, std::fabs(wantVol) * 1e-6);
      const double areaTol = std::max(1e-6, std::fabs(wantArea) * 1e-6);
      char buf[220];
      if (std::fabs(mp.volume - wantVol) > volTol) {
        std::snprintf(buf, sizeof(buf), "EXPECT SOLIDPROPS %ld: volume is %.9g, expected %.9g", idx,
                      mp.volume, wantVol);
        Fail(run, "expect", buf, sourceLine);
        return false;
      }
      if (std::fabs(mp.surfaceArea - wantArea) > areaTol) {
        std::snprintf(buf, sizeof(buf), "EXPECT SOLIDPROPS %ld: area is %.9g, expected %.9g", idx,
                      mp.surfaceArea, wantArea);
        Fail(run, "expect", buf, sourceLine);
        return false;
      }
      const long gotV = static_cast<long>(s.vertices.size());
      const long gotE = static_cast<long>(s.edges.size());
      const long gotF = static_cast<long>(s.faces.size());
      if (gotV != wantV || gotE != wantE || gotF != wantF) {
        std::snprintf(buf, sizeof(buf),
                      "EXPECT SOLIDPROPS %ld: topology is %ld/%ld/%ld (v/e/f), expected %ld/%ld/%ld",
                      idx, gotV, gotE, gotF, wantV, wantE, wantF);
        Fail(run, "expect", buf, sourceLine);
        return false;
      }
    } else if (what == "PREVIEWBOUNDS") {
      // EXPECT PREVIEWBOUNDS <mnX> <mnY> <mnZ> <mxX> <mxY> <mxZ> — the bounding box of the RUBBER
      // the viewport would draw right now, in world coordinates, to REQ-101's 0.01 ft.
      //
      // This is what makes the live preview a tested claim rather than a screenshot. Asserting a
      // segment COUNT would prove only that something was drawn; asserting the bounds proves the
      // preview is the shape the cursor implies — and paired with a CLICK at the same place, that it
      // is the same shape the commit builds.
      std::istringstream is(arg);
      double want[6] = {0, 0, 0, 0, 0, 0};
      if (!(is >> want[0] >> want[1] >> want[2] >> want[3] >> want[4] >> want[5])) {
        Fail(run, "parse", "EXPECT PREVIEWBOUNDS needs <mnX> <mnY> <mnZ> <mxX> <mxY> <mxZ>", sourceLine);
        return false;
      }
      std::vector<float> rubber;
      AppendCadDraftRubberLines(run.st, run.hoverX, run.hoverY, /*orthoEnabled=*/false, 0.0, 0.0,
                                run.st.viewportZoom > 0.f ? 50.f / run.st.viewportZoom : 50.f, 700,
                                rubber);
      if (rubber.size() < 6) {
        Fail(run, "expect", "EXPECT PREVIEWBOUNDS: the preview is empty", sourceLine);
        return false;
      }
      double got[6] = {1e300, 1e300, 1e300, -1e300, -1e300, -1e300};
      for (std::size_t i = 0; i + 2 < rubber.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
          const double c = static_cast<double>(rubber[i + static_cast<std::size_t>(k)]) +
                           (k == 0 ? run.st.worldDocumentOriginX : (k == 1 ? run.st.worldDocumentOriginY : 0.0));
          got[k] = std::min(got[k], c);
          got[k + 3] = std::max(got[k + 3], c);
        }
      }
      const char* names[6] = {"mnX", "mnY", "mnZ", "mxX", "mxY", "mxZ"};
      for (int k = 0; k < 6; ++k) {
        // Looser than REQ-101 on purpose, and the reason is geometric rather than sloppy: the preview
        // is CHORDED, so a circle of radius r has its extreme vertex up to a sagitta inside r. Twice
        // the chord tolerance covers that and still fails on any real error - a wrong radius is out
        // by feet, not by hundredths.
        if (std::fabs(got[k] - want[k]) > 2.0 * kSolidChordToleranceFt) {
          char msg[160];
          std::snprintf(msg, sizeof(msg), "EXPECT PREVIEWBOUNDS: %s is %.6f, expected %.6f", names[k],
                        got[k], want[k]);
          Fail(run, "expect", msg, sourceLine);
          return false;
        }
      }
    } else if (what == "SOLIDBOUNDS") {
      // EXPECT SOLIDBOUNDS <index> <mnX> <mnY> <mnZ> <mxX> <mxY> <mxZ> — the solid's analytic bounds
      // in WORLD coordinates, to REQ-101's 0.01 ft.
      //
      // The only verb here that says WHERE a solid is. Every other one says what shape it is, and a
      // review found exactly the defect that gap allows: a solid that did not follow the document
      // origin when it was established silently moved by the origin's whole magnitude, with correct
      // volume, correct area and correct topology the entire time.
      std::istringstream is(arg);
      long idx = -1;
      double want[6] = {0, 0, 0, 0, 0, 0};
      if (!(is >> idx >> want[0] >> want[1] >> want[2] >> want[3] >> want[4] >> want[5])) {
        Fail(run, "parse", "EXPECT SOLIDBOUNDS needs <index> <mnX> <mnY> <mnZ> <mxX> <mxY> <mxZ>",
             sourceLine);
        return false;
      }
      if (idx < 0 || static_cast<size_t>(idx) >= run.st.cadSolids.size() ||
          !run.st.cadSolids[static_cast<size_t>(idx)]) {
        Fail(run, "expect", "EXPECT SOLIDBOUNDS: no solid at index " + std::to_string(idx), sourceLine);
        return false;
      }
      const brep::Bounds b = brep::ComputeBounds(*run.st.cadSolids[static_cast<size_t>(idx)]);
      if (!b.valid) {
        Fail(run, "expect", "EXPECT SOLIDBOUNDS: the solid has no bounds", sourceLine);
        return false;
      }
      // Storage is local in XY and absolute in Z (ADR-025 D2), so the box is lifted back to world
      // before comparing — otherwise the expected numbers would depend on where the origin happens
      // to sit, which is the very thing this verb exists to check.
      const double got[6] = {b.mn.x + run.st.worldDocumentOriginX, b.mn.y + run.st.worldDocumentOriginY,
                             b.mn.z,
                             b.mx.x + run.st.worldDocumentOriginX, b.mx.y + run.st.worldDocumentOriginY,
                             b.mx.z};
      const char* names[6] = {"mnX", "mnY", "mnZ", "mxX", "mxY", "mxZ"};
      for (int k = 0; k < 6; ++k) {
        if (std::fabs(got[k] - want[k]) > 0.01) {
          char msg[160];
          std::snprintf(msg, sizeof(msg), "EXPECT SOLIDBOUNDS %ld: %s is %.6f, expected %.6f", idx,
                        names[k], got[k], want[k]);
          Fail(run, "expect", msg, sourceLine);
          return false;
        }
      }
    } else if (what == "SOLIDKIND") {
      // EXPECT SOLIDKIND <index> <name> — the recipe the solid remembers (ADR-045 (c)). Separate
      // from SOLIDPROPS on purpose: the recipe is NOT the geometry, and a test that could only
      // check them together could not tell the two apart.
      std::istringstream is(arg);
      long idx = -1;
      std::string wantName;
      if (!(is >> idx >> wantName)) {
        Fail(run, "parse", "EXPECT SOLIDKIND needs <index> <kind name>", sourceLine);
        return false;
      }
      if (idx < 0 || static_cast<size_t>(idx) >= run.st.cadSolids.size() ||
          !run.st.cadSolids[static_cast<size_t>(idx)]) {
        Fail(run, "expect", "EXPECT SOLIDKIND: no solid at index " + std::to_string(idx), sourceLine);
        return false;
      }
      const std::string got = brep::PrimitiveKindName(run.st.cadSolids[static_cast<size_t>(idx)]->recipe.kind);
      if (got != wantName) {
        Fail(run, "expect", "EXPECT SOLIDKIND " + std::to_string(idx) + ": is " + got + ", expected " + wantName,
             sourceLine);
        return false;
      }
    } else if (what == "LINEXYZ") {
      // EXPECT LINEXYZ <index> <x1> <y1> <z1> <x2> <y2> <z2> — one line's endpoints, in WORLD
      // coordinates, to REQ-101's 0.01 ft.
      //
      // Added for the UCS work (REQ-154). Every other EXPECT here counts entities or matches log
      // text, and neither can state the thing a UCS has to be judged on: that geometry drawn in a
      // rotated or tilted frame lands at the WORLD position the frame implies. A count passes just
      // as happily when the line went somewhere else entirely, and the command log reports what was
      // typed rather than where it ended up — so without this, "drawing commands respect the UCS"
      // has no failing test available to it.
      std::istringstream is(arg);
      long idx = -1;
      double want[6] = {0, 0, 0, 0, 0, 0};
      if (!(is >> idx) || !(is >> want[0] >> want[1] >> want[2] >> want[3] >> want[4] >> want[5])) {
        Fail(run, "parse", "EXPECT LINEXYZ needs <index> <x1> <y1> <z1> <x2> <y2> <z2>", sourceLine);
        return false;
      }
      const size_t base = static_cast<size_t>(idx) * 6;
      if (idx < 0 || base + 5 >= run.st.userLinesFlat.size()) {
        Fail(run, "expect",
             "EXPECT LINEXYZ: no line at index " + std::to_string(idx) + " (there are " +
                 std::to_string(run.st.userLinesFlat.size() / 6) + ")",
             sourceLine);
        return false;
      }
      // Storage is local in XY and absolute in Z (ADR-025 (b)), so the endpoints are lifted back to
      // world before comparing — otherwise a transcript's expected numbers would silently depend on
      // whether the drawing happened to have been rebased.
      double gx1 = 0.;
      double gy1 = 0.;
      double gx2 = 0.;
      double gy2 = 0.;
      CadCoord::WorldFromLocal(run.st, run.st.userLinesFlat[base], run.st.userLinesFlat[base + 1], &gx1, &gy1);
      CadCoord::WorldFromLocal(run.st, run.st.userLinesFlat[base + 3], run.st.userLinesFlat[base + 4], &gx2, &gy2);
      const double got[6] = {gx1, gy1, static_cast<double>(run.st.userLinesFlat[base + 2]),
                             gx2, gy2, static_cast<double>(run.st.userLinesFlat[base + 5])};
      const char* names[6] = {"x1", "y1", "z1", "x2", "y2", "z2"};
      for (int k = 0; k < 6; ++k) {
        if (std::fabs(got[k] - want[k]) > 0.01) {
          char msg[256];
          std::snprintf(msg, sizeof(msg), "EXPECT LINEXYZ %ld: %s is %.6f, expected %.6f", idx, names[k], got[k],
                        want[k]);
          Fail(run, "expect", msg, sourceLine);
          return false;
        }
      }
      return true;
    } else if (what == "POLYBULGE") {
      // EXPECT POLYBULGE <polylineIndex> <vertexIndexWithinPolyline> <bulge> — one polyline
      // segment's per-vertex bulge (REQ-316 / ADR-047). A count or a log line cannot state that a
      // segment is the RIGHT amount of curved; a wrong sign or a flattened arc is silent in plan
      // view, the same reason EXPECT LINEXYZ exists for the UCS work. An empty bulge array reads as
      // 0 for every vertex (a straight polyline).
      std::istringstream is(arg);
      long pi = -1, vi = -1;
      double want = 0.0;
      if (!(is >> pi) || !(is >> vi) || !(is >> want)) {
        Fail(run, "parse", "EXPECT POLYBULGE needs <polylineIndex> <vertexIndex> <bulge>", sourceLine);
        return false;
      }
      if (pi < 0 || static_cast<size_t>(pi) + 1 >= run.st.userPolylineOffsets.size()) {
        Fail(run, "expect", "EXPECT POLYBULGE: no polyline at index " + std::to_string(pi), sourceLine);
        return false;
      }
      const int v0 = run.st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = run.st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      if (vi < 0 || vi >= (v1 - v0)) {
        Fail(run, "expect", "EXPECT POLYBULGE: vertex index out of range for that polyline", sourceLine);
        return false;
      }
      const size_t gv = static_cast<size_t>(v0 + vi);
      const double got = gv < run.st.userPolylineVertsBulge.size()
                             ? static_cast<double>(run.st.userPolylineVertsBulge[gv])
                             : 0.0;
      if (std::fabs(got - want) > 1e-4) {
        char msg[192];
        std::snprintf(msg, sizeof(msg), "EXPECT POLYBULGE %ld %ld: is %.6f, expected %.6f", pi, vi, got, want);
        Fail(run, "expect", msg, sourceLine);
        return false;
      }
      return true;
    } else if (what == "PICKAT") {
      // EXPECT PICKAT <x> <y> <tolWorld> <NONE|LINE|ARC|POLYLINE|CIRCLE|ELLIPSE> — what
      // PickClosestCadEntity resolves at a world point within a world tolerance. The screen-space
      // hover/click plumbing has no headless equivalent, but the geometry math (REQ-316: a curved
      // polyline segment / an arc must be pickable ON the curve, not only near its chord) does.
      std::istringstream is(arg);
      double px = 0.0, py = 0.0, tol = 0.0;
      std::string want;
      if (!(is >> px >> py >> tol >> want)) {
        Fail(run, "parse", "EXPECT PICKAT needs <x> <y> <tolWorld> <TYPE>", sourceLine);
        return false;
      }
      SelectedEntity hit{};
      float d2 = 0.f;
      const bool got = PickClosestCadEntity(run.st, px, py, static_cast<float>(tol), &hit, &d2, nullptr);
      const std::string gotType =
          !got ? "NONE"
               : (hit.type == SelectedEntity::Type::LineSeg     ? "LINE"
                  : hit.type == SelectedEntity::Type::Arc        ? "ARC"
                  : hit.type == SelectedEntity::Type::Polyline   ? "POLYLINE"
                  : hit.type == SelectedEntity::Type::Circle     ? "CIRCLE"
                  : hit.type == SelectedEntity::Type::Ellipse    ? "ELLIPSE"
                                                                 : "OTHER");
      if (gotType != UpperAscii(want)) {
        Fail(run, "expect",
             "EXPECT PICKAT (" + std::to_string(px) + "," + std::to_string(py) + " tol " +
                 std::to_string(tol) + "): got " + gotType + ", expected " + UpperAscii(want),
             sourceLine);
        return false;
      }
      return true;
    } else if (what == "POLYARCS") {
      // EXPECT POLYARCS <polylineIndex> <count> — how many of a polyline's segments are curved
      // (non-zero bulge). Direction-independent, so it is the right assertion for a JOIN result
      // whose Eulerian walk may traverse an arc either way (REQ-316 / ADR-047).
      std::istringstream is(arg);
      long pi = -1, want = -1;
      if (!(is >> pi) || !(is >> want)) {
        Fail(run, "parse", "EXPECT POLYARCS needs <polylineIndex> <count>", sourceLine);
        return false;
      }
      if (pi < 0 || static_cast<size_t>(pi) + 1 >= run.st.userPolylineOffsets.size()) {
        Fail(run, "expect", "EXPECT POLYARCS: no polyline at index " + std::to_string(pi), sourceLine);
        return false;
      }
      const int v0 = run.st.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = run.st.userPolylineOffsets[static_cast<size_t>(pi + 1)];
      long got = 0;
      for (int vi = v0; vi < v1; ++vi)
        if (static_cast<size_t>(vi) < run.st.userPolylineVertsBulge.size() &&
            std::fabs(run.st.userPolylineVertsBulge[static_cast<size_t>(vi)]) > 1e-9)
          ++got;
      if (got != want) {
        Fail(run, "expect",
             "EXPECT POLYARCS " + std::to_string(pi) + ": " + std::to_string(got) +
                 " curved segments, expected " + std::to_string(want),
             sourceLine);
        return false;
      }
      return true;
    } else if (what == "CIRCLEXYZ") {
      // EXPECT CIRCLEXYZ <index> <cx> <cy> <cz> <r> <nx> <ny> <nz> — one circle's centre in WORLD
      // coordinates, its radius, and its PLANE NORMAL (REQ-312).
      //
      // The normal is the half no count and no log line can see. A circle drawn on a tilted UCS
      // whose normal came out world +Z is a flat circle in the wrong plane, and it passes every
      // other oracle in this file — which is exactly how the gap this requirement closes survived.
      std::istringstream is(arg);
      long idx = -1;
      double want[7] = {0, 0, 0, 0, 0, 0, 1};
      if (!(is >> idx) || !(is >> want[0] >> want[1] >> want[2] >> want[3] >> want[4] >> want[5] >> want[6])) {
        Fail(run, "parse", "EXPECT CIRCLEXYZ needs <index> <cx> <cy> <cz> <r> <nx> <ny> <nz>", sourceLine);
        return false;
      }
      const size_t base = static_cast<size_t>(idx) * 4;
      if (idx < 0 || base + 3 >= run.st.userCirclesCxCyZR.size()) {
        Fail(run, "expect",
             "EXPECT CIRCLEXYZ: no circle at index " + std::to_string(idx) + " (there are " +
                 std::to_string(run.st.userCirclesCxCyZR.size() / 4) + ")",
             sourceLine);
        return false;
      }
      double gcx = 0.;
      double gcy = 0.;
      CadCoord::WorldFromLocal(run.st, run.st.userCirclesCxCyZR[base], run.st.userCirclesCxCyZR[base + 1], &gcx,
                               &gcy);
      float nx = 0.f;
      float ny = 0.f;
      float nz = 1.f;
      CircleNormalAt(run.st.userCircleNormals, static_cast<size_t>(idx), &nx, &ny, &nz);
      const double got[7] = {gcx,
                             gcy,
                             static_cast<double>(run.st.userCirclesCxCyZR[base + 2]),
                             static_cast<double>(run.st.userCirclesCxCyZR[base + 3]),
                             static_cast<double>(nx),
                             static_cast<double>(ny),
                             static_cast<double>(nz)};
      const char* names[7] = {"cx", "cy", "cz", "r", "nx", "ny", "nz"};
      for (int k = 0; k < 7; ++k) {
        // The first four are lengths and get REQ-101's 0.01 ft. The normal is a unit direction with
        // no unit of length at all, so it is held to 1e-4 — loose enough for the float round trip
        // through the store, tight enough that a plane off by a fifth of a degree still fails.
        const double tol = k < 4 ? 0.01 : 1e-4;
        if (std::fabs(got[k] - want[k]) > tol) {
          char msg[256];
          std::snprintf(msg, sizeof(msg), "EXPECT CIRCLEXYZ %ld: %s is %.6f, expected %.6f", idx, names[k],
                        got[k], want[k]);
          Fail(run, "expect", msg, sourceLine);
          return false;
        }
      }
      return true;
    } else if (what == "ARCPOINTS") {
      // EXPECT ARCPOINTS <index> <sx> <sy> <sz> <ex> <ey> <ez> — where an arc actually STARTS and
      // ENDS, in WORLD coordinates, to REQ-101's 0.01 ft.
      //
      // The arc counterpart of LINEXYZ, and deliberately expressed as endpoints rather than as
      // centre/start/sweep: REQ-312's acceptance is written about where an arc's ends land, and a
      // centre, a start angle and a sweep can each be individually plausible while the plane they
      // are measured in is the wrong one. Resolved through the one shared parametrisation
      // (`CurvePlane` + `CurvePointAt`), so this asserts the same maths the renderer draws with.
      std::istringstream is(arg);
      long idx = -1;
      double want[6] = {0, 0, 0, 0, 0, 0};
      if (!(is >> idx) || !(is >> want[0] >> want[1] >> want[2] >> want[3] >> want[4] >> want[5])) {
        Fail(run, "parse", "EXPECT ARCPOINTS needs <index> <sx> <sy> <sz> <ex> <ey> <ez>", sourceLine);
        return false;
      }
      if (idx < 0 || static_cast<size_t>(idx) >= run.st.userArcs.size()) {
        Fail(run, "expect",
             "EXPECT ARCPOINTS: no arc at index " + std::to_string(idx) + " (there are " +
                 std::to_string(run.st.userArcs.size()) + ")",
             sourceLine);
        return false;
      }
      const CadArc& a = run.st.userArcs[static_cast<size_t>(idx)];
      const ucs::Ucs plane = CurvePlane(a);
      const ray3d::Vec3 s = CurvePointAt(plane, static_cast<double>(a.r), static_cast<double>(a.startRad));
      const ray3d::Vec3 e =
          CurvePointAt(plane, static_cast<double>(a.r), static_cast<double>(a.startRad) + static_cast<double>(a.sweepRad));
      // Storage is local in XY, absolute in Z (ADR-025 (b)) — lifted to world so a transcript's
      // numbers do not silently depend on whether the drawing happened to have been rebased.
      double sx = 0.;
      double sy = 0.;
      double ex = 0.;
      double ey = 0.;
      CadCoord::WorldFromLocal(run.st, static_cast<float>(s.x), static_cast<float>(s.y), &sx, &sy);
      CadCoord::WorldFromLocal(run.st, static_cast<float>(e.x), static_cast<float>(e.y), &ex, &ey);
      const double got[6] = {sx, sy, s.z, ex, ey, e.z};
      const char* names[6] = {"sx", "sy", "sz", "ex", "ey", "ez"};
      for (int k = 0; k < 6; ++k) {
        if (std::fabs(got[k] - want[k]) > 0.01) {
          char msg[256];
          std::snprintf(msg, sizeof(msg), "EXPECT ARCPOINTS %ld: %s is %.6f, expected %.6f", idx, names[k],
                        got[k], want[k]);
          Fail(run, "expect", msg, sourceLine);
          return false;
        }
      }
      return true;
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
      // Paper-space counts, so a transcript can say WHICH SPACE geometry landed in rather than only
      // how much of it exists. A draw command that writes to the wrong store leaves the model count
      // right and the paper count zero, which is invisible to every model-side count above — that is
      // issue #84 exactly, and REQ-039 (6) ("none of these paper edits change model geometry") has
      // no other way to be stated as something a transcript can fail. Both verbs sum across every
      // layout (merge reconciliation, D-2026-08-25-l) rather than reading layout 0 only, so a
      // multi-layout drawing is checkable the same way a single-layout one already was.
      else if (what == "PAPERPOLYLINES") {
        got = 0;
        for (const PaperLayout& L : run.st.paperLayouts)
          got += static_cast<long>(L.paperPolyOffsets.empty() ? 0 : L.paperPolyOffsets.size() - 1);
      } else if (what == "PAPERLINES") {
        got = 0;
        for (const PaperLayout& L : run.st.paperLayouts)
          got += static_cast<long>(L.paperLines.size() / 6);
      }
      // Sum of every layout's paper-space circles/arcs/ellipses (REQ-039 / issue #86), the same
      // model-vs-paper split PAPERPOLYLINES established for issue #84.
      else if (what == "PAPERCIRCLES") {
        got = 0;
        for (const PaperLayout& L : run.st.paperLayouts)
          got += static_cast<long>(L.paperCircles.size() / 3);
      } else if (what == "PAPERARCS") {
        got = 0;
        for (const PaperLayout& L : run.st.paperLayouts)
          got += static_cast<long>(L.paperArcs.size());
      } else if (what == "PAPERELLIPSES") {
        got = 0;
        for (const PaperLayout& L : run.st.paperLayouts)
          got += static_cast<long>(L.paperEllipses.size());
      }
      else if (what == "ARCS")
        got = static_cast<long>(run.st.userArcs.size());
      else if (what == "ELLIPSES")
        got = static_cast<long>(run.st.userEllipses.size());
      else if (what == "ANNOTATIONS")
        got = static_cast<long>(run.st.cadAnnotations.size());
      else if (what == "TABLES")
        got = static_cast<long>(run.st.cadTables.size());
      else if (what == "SURVEYPOINTS")
        got = static_cast<long>(run.st.surveyPoints.size());
      // Not a geometry count: what the drawing currently considers picked. It is the only way to
      // assert that something is NOT selectable — REQ-084's isolation gate, where the object is
      // still in the drawing and must simply refuse to be picked.
      else if (what == "SELECTED")
        got = static_cast<long>(run.st.selection.size());
      // How many FACES / EDGES / VERTICES of solids are selected (REQ-318 increment 2). A separate
      // count from SELECTED and not a subset of it: the two stores are mutually exclusive by
      // decision (D-2026-09-04-a), so "SELECTED 0 / SUBOBJECTS 1" is the assertion that says the
      // sub-object selection did not leak into the entity one — which is #148's criterion 2 stated
      // as a number rather than as a promise.
      else if (what == "SUBOBJECTS")
        got = static_cast<long>(run.st.subObjectSelection.size());
      // Whether a translate gizmo is drawn at all (REQ-060 acceptance 3: "no gizmo is drawn when
      // the selection is empty"). Asserted through `CadGizmoVisible`, which is the same predicate
      // `BuildGizmoOverlay` early-outs on — so this is the drawing decision itself, not a
      // restatement of it that could drift.
      else if (what == "GIZMO")
        got = CadGizmoVisible(run.st) ? 1L : 0L;
      // Which handle is armed: -1 none, 0 X, 1 Y, 2 Z. "A drag is armed" and "the drag is along the
      // axis you aimed at" are different claims, and the second is the one a mis-projected pick
      // breaks silently.
      else if (what == "GIZMOAXIS")
        got = static_cast<long>(run.st.gizmoDragActive ? run.st.gizmoDragAxis : -1);
      // How many handles the gizmo has: 3 on an entity selection, 1 on a solid FACE, 0 for none.
      // The count is the difference between the two modes made assertable — a face gizmo that grew
      // a second handle would be offering a direction `brep::PushPullFace` cannot move a face in.
      else if (what == "GIZMOAXES")
        got = static_cast<long>(CadGizmoAxisCountFor(run.st));
      else if (what == "SUBOBJECTFACES" || what == "SUBOBJECTEDGES" || what == "SUBOBJECTVERTICES") {
        const solidpick::Kind want = what == "SUBOBJECTFACES"   ? solidpick::Kind::Face
                                     : what == "SUBOBJECTEDGES" ? solidpick::Kind::Edge
                                                                : solidpick::Kind::Vertex;
        // By KIND, because "one sub-object is selected" and "the selected sub-object is the face
        // you aimed at" are different claims — the same distinction SELECTEDSURFACES draws below,
        // and here it is load-bearing: precedence is vertex, then edge, then face, so a pick that
        // silently returned the wrong KIND is precisely the failure this increment can have.
        got = static_cast<long>(std::count_if(
            run.st.subObjectSelection.begin(), run.st.subObjectSelection.end(),
            [&](const SelectedSubObject& s) { return s.kind == want; }));
      }
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
      else if (what == "SOLIDS")
        got = static_cast<long>(run.st.cadSolids.size());
      // How many solids the renderer would be handed this frame — the cache and the visibility
      // filter, not the document. A solid on a frozen layer counts in SOLIDS and not in SOLIDBATCHES,
      // which is the only way a transcript can state REQ-084 (d) for a solid.
      else if (what == "SOLIDBATCHES")
        got = static_cast<long>(run.st.solidDisplayGeometry.solids.size());
      // How many visible solids feed the batches this frame — the visibility filter's output BEFORE
      // GitHub #194's coalescing merges solids that share an appearance. SOLIDBATCHES went from "one
      // per drawn solid" to "one per resolved colour/lineweight"; this is what still lets a transcript
      // count what layer off/freeze removed. Derived the same way the assembly pass does it.
      else if (what == "SOLIDVISIBLE") {
        got = 0;
        for (size_t si = 0; si < run.st.cadSolids.size(); ++si) {
          if (!SolidVisible(run.st, si))
            continue;
          const CadSolidPtr& sp = run.st.cadSolids[si];
          const auto ce = std::find_if(run.st.solidDisplayCache.begin(), run.st.solidDisplayCache.end(),
                                       [&](const CadSolidTessellation& e) { return e.key.lock() == sp; });
          if (ce != run.st.solidDisplayCache.end() && !ce->empty())
            ++got;
        }
      }
      // Distinct (re)tessellations across the run — #120's "do not regenerate a solid's render mesh
      // every frame" expressed as a count. Paired with SOLIDTRIS: the cache HAS content and is not
      // being silently rebuilt behind an orbit.
      else if (what == "SOLIDTESSGEN")
        got = static_cast<long>(run.st.solidDisplayRegenCount);
      // Triangles in the whole solid tessellation cache. #120 asks that the render mesh not be
      // regenerated every frame; this is what lets a transcript assert the cache HAS content and,
      // paired with SOLIDTESSGEN below, that it is not being rebuilt behind the scenes.
      // Segments in the solid wireframe the renderer is handed - edges PLUS isolines, which share one
      // buffer. This is how a transcript can say that ISOLINES actually reaches the display rather
      // than only being stored.
      else if (what == "SOLIDEDGESEGS") {
        got = 0;
        for (const CadSolidTessellation& e : run.st.solidDisplayCache)
          got += static_cast<long>(e.edgeVerts.size() / 6);
      }
      else if (what == "SOLIDTRIS") {
        got = 0;
        for (const CadSolidTessellation& e : run.st.solidDisplayCache)
          got += static_cast<long>(e.triVerts.size() / 9);
      }
      else if (what == "SURFACEBATCHES")
        got = static_cast<long>(run.st.surfaceDisplayGeometry.lines.size());
      // REQ-072 band-fill and slope-arrow geometry, surface 0's cache entry only — see
      // SurfaceCacheBandTriCount / SurfaceCacheArrowSegCount above for what "one" counts as.
      else if (what == "SURFACEBANDTRIS")
        got = static_cast<long>(SurfaceCacheBandTriCount(run.st));
      else if (what == "SURFACEARROWSEGS")
        got = static_cast<long>(SurfaceCacheArrowSegCount(run.st));
      // How many REQ-072 band batches the renderer would be handed, across every visible surface —
      // the band-fill twin of SURFACEBATCHES.
      else if (what == "SURFACEBANDBATCHES")
        got = static_cast<long>(run.st.surfaceDisplayGeometry.bandTriangles.size());
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
                 " (LINES CIRCLES POLYLINES ARCS ELLIPSES ANNOTATIONS TABLES SURVEYPOINTS SELECTED"
                 " SURFACES SELECTEDSURFACES SURFACEBORDERSEGS SURFACETRISEGS SURFACEMINORSEGS"
                 " EXTRACTMATCHESDISPLAY"
                 " SURFACEMAJORSEGS SURFACEBATCHES SURFACETINGEN SURFACEBANDTRIS SURFACEARROWSEGS"
                 " SURFACEBANDBATCHES SOLIDS SOLIDBATCHES SOLIDTRIS SOLIDEDGESEGS)",
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
