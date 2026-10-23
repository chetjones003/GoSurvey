#pragma once

// REQ-403 (GitHub issue #834) — save/reload a Traverse Editor session to its own `.gstrav` file.
// Pure JSON in/out: a path, a TraverseData, and the a-priori LSA weights. No filesystem dialog, no
// AppCommandState (ADR-002 precedent) — the UI layer owns the file picker and project
// tracking; this module only knows how to read and write the format.
//
// Computed/derived fields (per-leg computed coordinates, closure results, the LSA adjustment) are
// deliberately NOT part of the format: they are pure functions of the fields that are saved, so the
// caller reruns ComputeTraverse() after Load() and gets back exactly the closure that was showing
// when the file was saved, with nothing stored twice.

#include "TraverseCalc.hpp"
#include "TraverseLeastSquares.hpp"

#include <string>

namespace traverseio {

/// Writes \p td and \p weights to \p path as a `.gstrav` JSON file. False with \p err on failure
/// (unwritable path, etc.); nothing is left partially written.
bool Save(const std::string& path, const TraverseData& td, const LsaWeights& weights, std::string& err);

/// Reads a `.gstrav` file written by \ref Save back into \p td and \p weights. False with \p err on
/// failure (missing file, unreadable, not a recognized `.gstrav` document) — \p td and \p weights are
/// left unchanged on failure.
bool Load(const std::string& path, TraverseData& td, LsaWeights& weights, std::string& err);

}  // namespace traverseio
