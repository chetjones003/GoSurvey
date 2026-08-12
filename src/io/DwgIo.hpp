#pragma once

#include <string>
#include <vector>

struct AppCommandState;

// DWG import/export (REQ-052, ADR-024 — Phase 1: external-converter route).
//
// DWG is Autodesk's proprietary binary format; unlike DXF it has no public specification and
// AC1032 (R2018) is not documented at all. Phase 1 therefore does not parse DWG in-tree: it
// converts DWG <-> DXF out of process with a converter already installed on the machine, then
// reuses DxfIo. A native codec replaces this later (see docs/dwg-plan.txt); the functions below
// are the seam that swap does not disturb.

/// Which external tool performs the conversion.
enum class DwgConverterKind {
  None = 0,
  OdaFileConverter, ///< ODA File Converter — folder-in / folder-out CLI.
  AutoCadCore,      ///< AutoCAD accoreconsole.exe driven by a generated .scr script.
};

/// A located converter. \c kind == None means nothing usable was found.
struct DwgConverter {
  DwgConverterKind kind = DwgConverterKind::None;
  std::string exePath;     ///< Absolute path to the executable.
  std::string displayName; ///< Human name for messages, e.g. "AutoCAD 2026 (accoreconsole)".
  bool available() const { return kind != DwgConverterKind::None; }
};

// --- Probe half (implemented in DwgProbe.cpp) ------------------------------------------------
// Version detection and converter discovery deliberately live in their own translation unit: they
// depend on nothing but <filesystem>, so the test target can link them without dragging in DxfIo
// and the GUI stack behind it. The public seam of ADR-024 is unchanged — all four functions are
// still declared here.

/// Locates a DWG converter, preferring (1) the \c GOSURVEY_DWG_CONVERTER environment override,
/// (2) ODA File Converter, (3) any installed AutoCAD's accoreconsole. Result is cached after the
/// first call; pass \p forceRescan to look again (e.g. after the user installs one).
const DwgConverter& FindDwgConverter(bool forceRescan = false);

/// Maps a 6-character DWG format tag ("AC1032") to a release name ("AutoCAD 2018"). Returns an
/// empty string when \p tag6 is not a DWG tag at all, and "unknown DWG (ACxxxx)" for an AC-prefixed
/// tag from a release we do not know — the two cases are distinct and callers rely on that.
std::string DwgVersionNameFromTag(const std::string& tag6);

/// Reads a file's 6-byte format tag and maps it via \ref DwgVersionNameFromTag.
/// Returns an empty string if the file is missing, shorter than the tag, or is not a DWG.
std::string DwgVersionName(const char* pathUtf8);

/// Replaces CAD geometry from a DWG. Converts to DXF in a temporary directory, then defers to
/// \ref ImportDxfFile, so the supported entity set is exactly DxfIo's. Appends progress and every
/// limitation encountered to \p log. Returns false (with a reason logged) when no converter is
/// installed, the file is not a DWG, or conversion fails.
bool ImportDwgFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);

/// Writes CAD geometry as a DWG by exporting DXF and converting it.
///
/// LOSSY BY CONSTRUCTION in Phase 1: the payload is whatever \ref ExportDxfFile emits, so blocks,
/// proxies and any object GoSurvey does not model are not preserved. Callers overwriting a file
/// GoSurvey imported must warn the user first (REQ-052 acceptance). Returns false with a reason
/// in \p log on failure; on failure the destination file is left untouched.
bool ExportDwgFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
