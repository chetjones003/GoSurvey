#pragma once

#include "TraverseCalc.hpp"

#include <string>

/// Parses a MicroSurvey STAR*NET .dat file and populates \p td (GitHub issue
/// #846, Phase B). Verified against the STAR*NET v10 Reference Manual's
/// documented input grammar (Chapter 5, "Preparing Input Data") — not guessed.
///
/// Supports STAR*NET traverse lines and the single-observation network lines
/// A, D, V, B, M, BM, DV, and L. Those lines are converted into GoSurvey
/// stations and measurements (our own command words: CTRL, ANGLE, DIST,
/// AZIMUTH, ZENITH, EDIF). A few of their dotted options are read only so the
/// conversion is right (coordinate order, 2D versus 3D, elevation-difference
/// versus zenith, angle-name order). Those names are not GoSurvey commands
/// (D-2026-10-13-e). Sideshots (SS) and direction sets are still reported
/// as unsupported.
///
/// Supports STAR*NET's own "Traverse" data type, which maps directly
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
/// Also supports GPS vector records (GitHub issue #847), grounded in MicroSurvey
/// support documentation (the PRO Edition Supplement's own grammar for these
/// records is not publicly available):
///   .GPS WEIGHT COVARIANCE             — required before any G-record; the
///                                         alternate ".GPS WEIGHT STDERRCORR"
///                                         encoding is rejected (its exact G2/G3
///                                         field semantics could not be confirmed).
///   G0 ...                             — free-text/date header; ignored.
///   G1 <from>-<to> <dX> <dY> <dZ> [...] — ECEF baseline vector (meters) between
///                                         two named stations; any trailing
///                                         token(s) are parsed but discarded
///                                         (meaning unconfirmed, not used by any
///                                         computation).
///   G2 <varX> <varY> <varZ>            — covariance diagonal (meters²).
///   G3 <covXY> <covXZ> <covYZ>         — covariance off-diagonal (meters²).
///   G4 ...                             — rejected: no authoritative description
///                                         of this record type could be found.
///
/// 2D conventional traverse data only (angle, horizontal distance, a-priori
/// errors) — matching GoSurvey's existing TraverseData model, per the scope
/// decision recorded on issue #845. Everything else in a real STAR*NET file is a
/// defined SPEC gap, reported as a parse failure rather than silently dropped or
/// guessed at, including: any record type other than C/TB/T/TE/G0-G3 (A, D, V, B,
/// M, BM, DV, SS, leveling, position/ellipsoid-height types, G4); any inline
/// option other than .ORDER and .GPS WEIGHT COVARIANCE; a second TB before a TE
/// (multiple/interconnected traverses); a 3D-looking T line (a third observation
/// token); and a TE line with a closing angle. \p td is left unchanged on failure.
bool StarNetImport(const char* path, TraverseData& td, std::string& errorMsg);
