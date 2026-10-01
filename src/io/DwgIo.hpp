#pragma once

#include <string>
#include <string_view>
#include <vector>

struct AppCommandState;

// DWG interchange via GNU LibreDWG (REQ-170 / ADR-041). File Import/Export does not use ODA
// File Converter or AutoCAD for read/write. Save targets R2000/R2004 (user picks at export;
// D-2026-10-01-a). FindDwgConverter remains for 3D solid tessellation only (not File DWG).

/// DWG save format the user chose in the Export DWG dialog (issue #600).
enum class DwgSaveVersion {
  R2000 = 0,
  R2004 = 1,
};

/// One row in the Export DWG version list (selectable or "coming later").
struct DwgExportFormatRow {
  const char* displayName;  ///< e.g. "AutoCAD 2004"
  const char* acTag;        ///< six-char magic, e.g. "AC1018"
  DwgSaveVersion version;   ///< meaningful only when \c selectable
  bool selectable;
};

/// Menu order for the export dialog; non-selectable rows are shown disabled.
const DwgExportFormatRow* DwgExportFormatRows(size_t* outCount);

const char* DwgSaveVersionAcTag(DwgSaveVersion version);
const char* DwgSaveVersionDisplayName(DwgSaveVersion version);

enum class DwgConverterKind {
  None = 0,
  OdaFileConverter,
  AutoCadCore,
};

struct DwgConverter {
  DwgConverterKind kind = DwgConverterKind::None;
  std::string exePath;
  std::string displayName;
  bool available() const { return kind != DwgConverterKind::None; }
};

const DwgConverter& FindDwgConverter(bool forceRescan = false);
std::string DwgVersionNameFromTag(const std::string& tag6);
std::string DwgVersionName(const char* pathUtf8);

bool ImportDwgFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool ExportDwgFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);

/// Append the GoSurvey JSON trailer to an on-disk DWG (LibreDWG bytes only). Used by ExportDwgFile;
/// exposed for regression tests of the locked-file retry path (issue #167).
bool AppendGoSurveyPayloadToDwgFile(const char* pathUtf8, const AppCommandState& st,
                                    std::vector<std::string>& log);

/// REQ-175: Open/Save a drawing path. `.dwg` (payload when present) — `.gs` is no longer an
/// openable document format (issue #264).
bool OpenDrawingDocument(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);
bool SaveDrawingDocument(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log);

/// Trailer JSON for SAMEFILE (ADR-044 (e)). Empty if this is not a GoSurvey DWG.
bool TryGoSurveyDwgPayloadFromBytes(std::string_view fileBytes, std::string& jsonOut);

/// One entity class (or degradation) that DWG save will drop or alter, with how many objects it
/// affects (REQ-170 / REQ-201, issue #614). \c label reads naturally after "drops" / "will alter",
/// e.g. "12 survey points" or "40 colours (rounded to the nearest AutoCAD index colour)".
struct DwgExportLoss {
  std::string label;
  int count = 0;
};

/// Scans the drawing and reports exactly what \ref ExportLibreCadFile (DWG only — `asDxf=false`)
/// will drop or degrade, computed from what the drawing actually contains rather than a fixed
/// list. Empty when nothing is lost. The "Export DWG" warning dialog and the save log both render
/// from this one list, so they cannot disagree, and a loss disappears from both the moment the
/// gap it names is fixed.
std::vector<DwgExportLoss> ComputeDwgExportLosses(const AppCommandState& st);
