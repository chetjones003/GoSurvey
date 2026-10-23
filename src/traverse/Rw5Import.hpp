#pragma once

#include "TraverseCalc.hpp"

#include <string>

/// Parses a Carlson SurvCE/SurvPC .rw5 raw data file and populates \p td
/// (GitHub issue #846, Phase B). Grounded in Carlson's official "SurvCE Raw
/// Data File Format (*.rw5) — Version 3.03" reference — not guessed. This is
/// the same RW5 record format shared by SurvCE, SurvPC, and (per Carlson's
/// own documentation) derived from the TDS raw data specification, so this
/// one importer covers the "Carlson RW5" and "SurvCE/SurvPC RW5" items from
/// issue #846.
///
/// RW5 is a flat, comma-separated, one-record-per-line ASCII format; each
/// field is a 2-letter header (1 letter for N/E, which are followed by a
/// literal space) immediately followed by its value, no separator. Angle
/// values are plain decimal degrees throughout (no DMS/packed convention, per
/// the spec's own samples).
///
/// Supported records, matching the conventional 2D traverse scope decision
/// recorded on issue #845:
///   JB    — job header; read and ignored.
///   MO    — mode setup; only the UN (distance unit) field is checked. UN=1
///           (meters) is a defined gap (this importer assumes feet/US feet).
///   SP    — a known point's coordinates (PN, N, E, EL), like StarNet's C line.
///   OC    — occupies a station (OP), optionally with its own coordinates
///           (N, E, EL); the first OC in the file becomes the start station.
///   BK    — backsight for the current OC: BP (backsight point) and/or BS
///           (a plain decimal-degree backsight azimuth/circle reading). Only
///           the FIRST BK's BS/BP resolves TraverseData::startBearingDeg
///           (exactly like FbkImport's backsight circle reading); every
///           later BK's BP is instead validated against the station the
///           chain expects as the backsight — the actual circle reading
///           doesn't matter once the chain is running on turned angles.
///   TR    — one traverse leg: OP (occupy, validated against the chain), FP
///           (foresight, becomes the leg's station), one angle field (AR or
///           AL — Angle-Right direct, Angle-Left negated/normalized to the
///           same CW convention; AZ/BR/DR/DL are a defined gap), one vertical
///           field (ZE zenith or VA elevation angle; CE — a literal elevation
///           delta GoSurvey's leg model has no slot for — is a defined gap),
///           and one distance field (SD slope or HD horizontal).
///
/// Ignored by design (RW5 records real field data always contains these, and
/// GoSurvey's simple sequential-chain TraverseData has no slot for a shot
/// that doesn't advance the traverse — rejecting them would make this
/// importer fail on essentially every real RW5 file): SS (sideshot, almost
/// always including the routine backsight check shot), BD/BR/FD/FR (face-set
/// shots — Carlson's own doc calls these "identical to a Sideshot record"),
/// OF (vertical-angle offset correction), and "--" note-only records.
///
/// Explicit defined-gap failures (reported with a line number, not silently
/// dropped or guessed at): GPS, BP-as-GNSS-base-position, G0–G4 (GPS
/// vectors — out of scope per issue #847), any unknown record type, a BK/TR
/// with no preceding OC, a TR whose OP doesn't match the currently occupied
/// station or whose backsight doesn't match what the traverse chain expects,
/// an AZ/BR/DR/DL angle field, a CE vertical field, and MO's UN=1 (meters).
bool Rw5Import(const char* path, TraverseData& td, std::string& errorMsg);
