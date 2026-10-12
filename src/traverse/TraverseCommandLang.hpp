#pragma once

// GitHub issue #845 (Phase A) — GoSurvey's own traverse command-editor language.
//
// A plain-text, line-oriented syntax for authoring/editing a TraverseData directly,
// as an alternative to the form-based Traverse Editor panel. Inspired by the general
// idea of keyed-line survey "command" files (e.g. MicroSurvey StarNet's .dat format)
// but NOT a copy of StarNet's syntax — GoSurvey defines its own keywords, and every
// line is plain whitespace-separated tokens with no `key=value` punctuation.
//
// Each non-blank, non-comment line is one directive: an upper/lower-case keyword
// followed by its values, in a fixed order. Comment lines start with '#'.
//
// Header directives (each at most once, before any leg directives):
//   STATION <id> <e> <n> <z>
//       The starting station's point number and coordinates.
//   BEARING <deg>
//       Reference orientation at the start station, decimal degrees. Omit the
//       line if there is none yet.
//   LOOP ON|OFF
//       Whether the traverse is a closed loop back to the start station.
//   SCALE ON|OFF [COMBINED|GRID]
//       Whether to apply the drawing's ground->grid scale factor (the factor
//       itself is resolved from Drawing Settings, not set here); COMBINED is
//       assumed when the kind is omitted.
//   ERRORS <angle-arcsec> <dist-const> <dist-ppm>
//       A-priori least-squares standard errors (LsaWeights).
//
// Leg directives — every traverse leg is identified by the station occupied for
// it (AT) and the foresight it points to (TO). A leg must be declared with STN
// before any HANG/HDIST/SDIST/VANG/DESC line that refers to its TO station, and
// AT must continue the chain (the previous leg's TO, or the start station for
// the first leg) — this format only represents a single sequential traverse,
// not arbitrary multi-foresight setups.
//
//   STN <at> <to>
//       Declares the leg: station AT is occupied, sighting foresight TO.
//   HANG <from> <at> <to> <angle-deg>
//       The horizontal angle turned, backsighting FROM through AT to TO.
//       FROM is informational only (it has no effect on the computed bearing,
//       which is always start-bearing + the running sum of turned angles): for
//       every leg after the first it must equal the station occupied for the
//       previous leg, and GoSurvey fills it in for you when it (re)generates
//       this text. The first leg has no modeled backsight station, so any
//       token is accepted there (conventionally the name of the real-world
//       backsight mark) and it round-trips as the literal `REF`.
//   HDIST <at> <to> <dist>
//       Horizontal distance (direct measurement).
//   SDIST <at> <to> <dist>
//       Slope (EDM) distance; reduced using the leg's vertical angle.
//   VANG <at> <to> <angle-deg> [ZENITH|ELEV]
//       Vertical angle; ZENITH (90° level) is assumed when omitted.
//   DESC <at> <to> <text to end of line>
//       Station description / field notes for this leg.
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
