#pragma once

#include "TraverseCalc.hpp"

#include <string>

/// Parses a MicroSurvey STAR*NET .dat file and populates \p td (GitHub issue
/// #846, Phase B). Verified against the STAR*NET v10 Reference Manual's
/// documented input grammar (Chapter 5, "Preparing Input Data") — not guessed.
///
/// Supports exactly STAR*NET's own "Traverse" data type, which maps directly
/// onto GoSurvey's sequential TraverseLeg chain:
///   C <name> <N> <E> [elev] [...]      — a station's known/reference coordinates
///   .ORDER [NE|EN] [...]               — the only inline option honored; selects
///                                         whether C's two numbers are N-then-E or
///                                         E-then-N (default NE, per the manual)
///   TB <backsight-name-or-bearing> ['description]
///                                       — begins the traverse: a backsight to an
///                                         already-coordinated station, OR a fixed
///                                         bearing/azimuth (dash D-M-S, "N..E"-style,
///                                         or "B=..." packed/GONS prefix)
///   T <name> <angle> <dist> [angle-stderr] [dist-stderr]
///                                       — one leg: <name> is the OCCUPIED station;
///                                         the angle/distance go to the NEXT station,
///                                         which is named by the following T or TE
///                                         line (this is STAR*NET's own convention,
///                                         not GoSurvey's — the importer re-indexes
///                                         it onto GoSurvey's foresight-keyed legs).
///                                         A present pair of numeric std errors
///                                         becomes that leg's TSERR override.
///   TE <name>                          — ends the traverse at the named station
///                                         (a closing angle after the name is a
///                                         defined gap, see below).
///
/// 2D conventional traverse data only (angle, horizontal distance, a-priori
/// errors) — matching GoSurvey's existing TraverseData model, per the scope
/// decision recorded on issue #845. Everything else in a real STAR*NET file is a
/// defined SPEC gap, reported as a parse failure rather than silently dropped or
/// guessed at, including: any record type other than C/TB/T/TE (A, D, V, B, M,
/// BM, DV, SS, leveling, position/ellipsoid-height types); any inline option
/// other than .ORDER; a second TB before a TE (multiple/interconnected
/// traverses); a 3D-looking T line (a third observation token); and a TE line
/// with a closing angle. \p td is left unchanged on failure.
bool StarNetImport(const char* path, TraverseData& td, std::string& errorMsg);
