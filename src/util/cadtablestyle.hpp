#pragma once

#include "cadtable.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Table styles, point table rows and cell hit-testing (REQ-474, REQ-475, REQ-476; D-2026-10-30-b).
// Pure: no GL, Qt or command state, so Catch2 covers it directly.

enum class CadPointTableColumn : std::uint8_t { Name, Northing, Easting, Elevation, Description };

inline const char* CadPointTableColumnTitle(CadPointTableColumn c) {
  switch (c) {
  case CadPointTableColumn::Name:        return "Point Name";
  case CadPointTableColumn::Northing:    return "Northing";
  case CadPointTableColumn::Easting:     return "Easting";
  case CadPointTableColumn::Elevation:   return "Elevation";
  case CadPointTableColumn::Description: return "Description";
  }
  return "";
}

/// A named table style (REQ-475). Data styles hold any text; Point styles list survey points, one row each,
/// with the columns in \ref pointColumns.
struct CadTableStyle {
  std::string name;
  bool point = false;
  bool headerRow = true;
  float textHeightInches = 0.125f;
  float columnWidthInches = 1.5f;
  float rowHeightInches = 0.375f;
  std::vector<CadPointTableColumn> pointColumns;
  // Civil source column expressions remain drawing-owned, including fields that do not yet
  // have a GoSurvey point-column enum (latitude/longitude and custom formatted expressions).
  std::vector<std::string> columnHeaders;
  std::vector<std::string> columnExpressions;
  std::vector<double> columnWidths;
  std::string title;
  std::string civilStyleName;
  std::string textStyleName;
  std::string headerTextStyleName;
  std::string titleTextStyleName;
  float headerTextHeightInches = 0.125f;
  float titleTextHeightInches = 0.125f;
  std::string dataTextColor;
  std::string headerTextColor;
  std::string titleTextColor;
  std::string gridLineColor;
  std::string borderColor;
};

/// The styles every drawing has (REQ-475 clause 2). The point column lists follow the NRCS point file layout
/// (name, north, east, elevation, description); they are an assumption until checked in Civil 3D.
inline std::vector<CadTableStyle> CadTableBuiltinStyles() {
  CadTableStyle standard;
  standard.name = "Standard";
  CadTableStyle nezd;
  nezd.name = "NameNEZD";
  nezd.point = true;
  nezd.headerRow = true;
  nezd.pointColumns = {CadPointTableColumn::Name, CadPointTableColumn::Northing, CadPointTableColumn::Easting,
                       CadPointTableColumn::Elevation};
  CadTableStyle withComments = nezd;
  withComments.name = "NameNEZD with Comments";
  withComments.pointColumns.push_back(CadPointTableColumn::Description);
  return {standard, nezd, withComments};
}

inline bool CadTableNameEquals(const std::string& a, const std::string& b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

/// The style called \p name (case-insensitive), or nullptr.
inline const CadTableStyle* CadTableFindStyle(const std::vector<CadTableStyle>& styles, const std::string& name) {
  for (const CadTableStyle& s : styles) {
    if (CadTableNameEquals(s.name, name))
      return &s;
  }
  return nullptr;
}

/// One survey point as a row of a point table (REQ-474).
struct CadTablePointRow {
  int id = 0;
  double northing = 0.0;
  double easting = 0.0;
  double elevation = 0.0;
  std::string description;
};

/// Cell text for a point table: a header row of column titles, then one row per point, in the order given.
/// Coordinates use \p decimals decimal places.
inline std::vector<std::string> CadPointTableCells(const CadTableStyle& style, const std::vector<CadTablePointRow>& rows,
                                                   int decimals) {
  std::vector<std::string> cells;
  if (style.pointColumns.empty())
    return cells;
  for (CadPointTableColumn c : style.pointColumns)
    cells.emplace_back(CadPointTableColumnTitle(c));
  const int d = decimals < 0 ? 0 : (decimals > 8 ? 8 : decimals);
  char buf[64];
  for (const CadTablePointRow& r : rows) {
    for (CadPointTableColumn c : style.pointColumns) {
      switch (c) {
      case CadPointTableColumn::Name:
        cells.push_back(std::to_string(r.id));
        break;
      case CadPointTableColumn::Northing:
        std::snprintf(buf, sizeof(buf), "%.*f", d, r.northing);
        cells.emplace_back(buf);
        break;
      case CadPointTableColumn::Easting:
        std::snprintf(buf, sizeof(buf), "%.*f", d, r.easting);
        cells.emplace_back(buf);
        break;
      case CadPointTableColumn::Elevation:
        std::snprintf(buf, sizeof(buf), "%.*f", d, r.elevation);
        cells.emplace_back(buf);
        break;
      case CadPointTableColumn::Description:
        cells.push_back(r.description);
        break;
      }
    }
  }
  return cells;
}

/// Sizes a table for \p rows by \p cols from \p style and blanks its cells. Sizes are plotted inches scaled to
/// model units by \p modelUnitsPerPlottedInch.
inline void CadTableApplyStyleSize(CadTable* t, const CadTableStyle& style, int rows, int cols,
                                   float modelUnitsPerPlottedInch) {
  if (!t || rows <= 0 || cols <= 0)
    return;
  const float mupi = modelUnitsPerPlottedInch > 1.e-6f ? modelUnitsPerPlottedInch : 1.f;
  t->cols = cols;
  t->width = static_cast<float>(cols) * style.columnWidthInches * mupi;
  t->height = static_cast<float>(rows) * style.rowHeightInches * mupi;
  t->plottedHeightInches = style.textHeightInches;
  t->styleName = style.name;
  t->cells.assign(static_cast<size_t>(rows) * static_cast<size_t>(cols), std::string());
}

/// Index of the cell under world point (\p wx, \p wy), or -1 when the point is outside the table.
inline int CadTableCellAtWorld(const CadTable& t, float wx, float wy) {
  const int rows = CadTableRowCount(t);
  if (t.cols <= 0 || rows <= 0 || t.width <= 0.f || t.height <= 0.f)
    return -1;
  float lx = 0.f;
  float ly = 0.f;
  CadTableWorldToLocal(t, wx, wy, &lx, &ly);
  if (lx < 0.f || ly < 0.f || lx > t.width || ly > t.height)
    return -1;
  int c = 0, r = 0;
  while (c + 1 < t.cols && lx >= CadTableBandBoundary(t.columnWeights, t.cols, c + 1, t.width)) ++c;
  while (r + 1 < rows && ly >= CadTableBandBoundary(t.rowWeights, rows, r + 1, t.height)) ++r;
  if (t.mergeTitleRow && r == 0) return 0;
  return r * t.cols + c;
}

/// True when the table uses a Civil/point-table style (survey-linked body cells).
inline bool CadTableUsesSurveyPointData(const std::vector<CadTableStyle>& styles, const CadTable& t) {
  if (t.surveyPointDataRows) return true;
  const CadTableStyle* style = CadTableFindStyle(styles, t.styleName);
  return style != nullptr && style->point;
}

[[nodiscard]] inline bool CadTableCellIsSurveyPointData(const std::vector<CadTableStyle>& styles,
                                                        const CadTable& t, int cellIndex) {
  if (!CadTableUsesSurveyPointData(styles, t) || t.cols <= 0 || cellIndex < 0) return false;
  const int rows = CadTableRowCount(t);
  if (rows <= 0) return false;
  const int row = cellIndex / t.cols;
  const int firstDataRow = t.mergeTitleRow ? 2 : 1;
  return row >= firstDataRow && row < rows;
}

inline void CadTableSyncSurveyPointTables(std::vector<CadTable>& tables,
                                          const std::vector<CadTableStyle>& styles,
                                          float modelUnitsPerPlottedInch) {
  for (CadTable& table : tables) {
    if (!CadTableUsesSurveyPointData(styles, table)) continue;
    table.surveyPointDataRows = true;
    CadTableEnsurePointTableLayout(&table, modelUnitsPerPlottedInch);
    CadTableExpandWidthForColumnWeights(&table);
  }
}
