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
//   STN <at> <to> SIDE
//       Declares a side shot instead: a radial detail point shot from AT
//       (which must already be an occupied station — the start station, or
//       any earlier TO — not necessarily the chain's current end), TO is the
//       shot's own point number, and it does NOT need to continue the chain.
//       HANG/HDIST/SDIST/VANG/DESC below address it exactly like a leg, except
//       HANG's FROM is never validated (a side shot has no "previous leg").
//   STN <at> <to> CHECK [known-e known-n]
//       Declares a check shot: a side shot flagged as a repeat/verification
//       observation. The optional known-e/known-n are the already-known
//       coordinates to compare the shot's computed position against; GoSurvey
//       reports the discrepancy. Everything else is identical to SIDE.
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
//   PRISM <at> <to> <constant> <error>
//       Prism constant: a real EDM correction, added to the leg's slope distance
//       before reduction (only meaningful alongside SDIST — a directly-entered
//       HDIST has no slope-distance step to correct). The error is a-priori only
//       (see TSERR below) — a prism's rated accuracy has no deterministic
//       correction, only an uncertainty contribution.
//   POLE <at> <to> <error>
//       The pole's centering/plumb a-priori error (its rated or estimated
//       accuracy) — a-priori only, like PRISM's error: there is no deterministic
//       correction for how well a pole was plumbed over a point.
//   TSERR <at> <to> <angle-arcsec> <dist-const> <dist-ppm>
//       This leg's own equipment accuracy spec, overriding the traverse-wide
//       ERRORS weights for just this leg's least-squares observations.
//
// PRISM's error and POLE's error combine with this leg's distance a-priori sigma
// (TSERR if given, else the traverse-wide ERRORS) in quadrature, matching how a
// real geodetic adjustment package (e.g. MicroSurvey StarNet, SALSA) treats
// prism/centering a-priori error: effective = sqrt(dist_const² + prism_error² +
// pole_error²). They have no effect outside the least-squares adjustment.
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
