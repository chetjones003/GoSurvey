#pragma once

// Document invariants (REQ-204 / ADR-031 (d)).
//
// These are the properties a drawing must satisfy at rest, whatever route it took to get there —
// a command, a file import, an undo, or a fuzzed transcript. They are checked after every step by
// the headless driver (REQ-203) and directly by the Catch2 suite, which is the two present-day
// concrete uses that let this module exist at all under REQ-301.
//
// The module is deliberately PURE: no GL, no file IO, no logging, and it never mutates the state
// it inspects. That is what makes it callable from a `const` context and cheap enough to run after
// every step of a fuzz run.
//
// Scope note. Only invariants that are a function of a SINGLE AppCommandState live here. The
// differential oracles REQ-204 also names — undo/redo identity, `.gs` save/load/save stability,
// DXF export/import convergence — compare *two* states or need file IO, so they belong to the
// driver, not to this header.

#include <string>
#include <vector>

struct AppCommandState;

/// One failed invariant. `name` is a stable id (never reworded casually) because the fuzz triage
/// pipeline hashes it into a deduplication signature — see docs/fuzz-harness.md §6.
struct InvariantViolation {
  const char* name = "";
  std::string detail;    ///< Human-readable specifics: which store, which index, what value.
  int entityIndex = -1;  ///< Offending entity index where one applies; -1 when it does not.
};

/// Append every violated invariant in \p st to \p out. An empty result means the document is sound.
///
/// \p out is appended to, never cleared, so a caller can accumulate across several documents.
void CheckDocumentInvariants(const AppCommandState& st, std::vector<InvariantViolation>* out);

/// One-line summary for a log or an issue title, e.g.
/// `flat-strides: userLinesFlat.size()=19 is not a multiple of 6`. Empty string when \p v is empty.
std::string FormatInvariantViolations(const std::vector<InvariantViolation>& v);

// ---------------------------------------------------------------------------
// Invariant ids. Named constants rather than bare string literals so a typo in a test is a compile
// error, and so the triage signature and the check agree by construction.
// ---------------------------------------------------------------------------
namespace docinv {

/// Every flat geometry store's size is a whole number of vertices (architecture §11.8).
inline constexpr const char* kFlatStrides = "flat-strides";
/// No stored coordinate is NaN or infinite.
inline constexpr const char* kFiniteCoords = "finite-coords";
/// Entity ids are unique within the drawing, and `nextEntityId` is past every one of them (REQ-076).
inline constexpr const char* kEntityIds = "entity-ids";
/// Every selection entry addresses an entity that exists (architecture §11.9).
inline constexpr const char* kSelectionInRange = "selection-in-range";
/// Each parallel attribute array matches its geometry array's entity count.
inline constexpr const char* kAttrCounts = "attr-counts";
/// Polyline offsets are non-decreasing, start at 0, and end at the vertex count.
inline constexpr const char* kPolylineOffsets = "polyline-offsets";
/// A filled region's loop starts are non-decreasing and inside its vertex range.
inline constexpr const char* kRegionLoops = "region-loops";
/// A survey point's label reference resolves to an annotation, or to nothing — never to a stale id.
inline constexpr const char* kSurveyLabelLinks = "survey-label-links";
/// REQ-087: a feature line's CSR offsets, and its per-VERTEX elevation-point flag array, which must
/// be exactly one entry per vertex. A short flag array is silent — the missing tail simply reads as
/// "all PIs", so elevation points vanish with the geometry still looking right (ADR-035 (a)).
inline constexpr const char* kFeatureLineOffsets = "feature-line-offsets";
/// REQ-316 / ADR-047: the parallel per-vertex polyline bulge array is either empty (all segments
/// straight) or exactly one entry per vertex, and every bulge is finite. A short array is silent —
/// the missing tail reads as "straight", so arcs vanish with the polyline still looking right.
inline constexpr const char* kPolylineBulge = "polyline-bulge";

}  // namespace docinv
