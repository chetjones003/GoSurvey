#pragma once

// GitHub issue #845 (Phase A) — GoSurvey's own traverse command-editor language.
//
// A plain-text, line-oriented syntax for authoring/editing a TraverseData directly,
// as an alternative to the form-based Traverse Editor panel. Inspired by the general
// idea of keyed-line survey "command" files (e.g. MicroSurvey StarNet's .dat format)
// but NOT a copy of StarNet's syntax — GoSurvey defines its own keywords and layout.
//
// Each non-blank, non-comment line is one directive: an upper/lower-case keyword
// followed by zero or more `key=value` fields (quote a value that contains spaces).
// Comment lines start with '#'. Recognized directives:
//
//   STATION id=<int> e=<num> n=<num> z=<num> [bearing=<deg>]
//       The starting station. `bearing` is the reference orientation in decimal
//       degrees; omit it if there is none yet.
//   LOOP closed=on|off
//       Whether the traverse is a closed loop back to the start station.
//   SCALE apply=on|off [kind=combined|grid]
//       Whether to apply the drawing's ground->grid scale factor; the factor
//       itself is resolved from Drawing Settings, not set here.
//   ERRORS angle=<arcsec> dist=<const-dist-err> ppm=<ppm>
//       A-priori least-squares standard errors (LsaWeights).
//   LEG stn=<int> [desc="..."] [angle=<deg>] [hdist=<num>] [sdist=<num>]
//       [vangle=<deg>] [zenith=on|off]
//       One traverse leg. `angle` is the horizontal angle turned from the
//       backsight; `hdist`/`sdist` are horizontal/slope distance (at most one is
//       normally given); `vangle` is the vertical/zenith angle; `zenith` selects
//       the vertical-angle convention (on = zenith, off = elevation).
//
// This format only round-trips what TraverseData already models: a leg's raw F1/F2
// observation sets (src/traverse/FbkImport.cpp import detail) are not represented
// here, since they have no manual-entry counterpart in the form panel either.

#include "TraverseCalc.hpp"
#include "TraverseLeastSquares.hpp"

#include <string>

namespace travcmd {

/// Serializes \p td and \p weights into GoSurvey's traverse command-language text.
std::string Serialize(const TraverseData& td, const LsaWeights& weights);

/// Parses GoSurvey traverse command-language text into \p td and \p weights.
/// Returns false with a 1-based "line N: ..." message in \p errorMsg on the first
/// malformed line; \p td and \p weights are left unchanged on failure.
bool Parse(const std::string& text, TraverseData& td, LsaWeights& weights, std::string& errorMsg);

}  // namespace travcmd
