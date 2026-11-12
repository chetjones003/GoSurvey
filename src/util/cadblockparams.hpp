#pragma once

// REQ-524 / REQ-525 (GitHub issue #808) — edits to a block definition's parameters and to its block
// properties table.
//
// Every edit function here is all-or-nothing: it returns an empty string and applies the change, or
// returns the reason (a lower-case sentence ending in a full stop, ready to follow "COMMAND — ") and
// leaves the definition untouched. Nothing here touches the drawing, the undo history or the UI; the
// command layer (`CadBlocksApplyDefinitionEdit`) wraps these for that.

#include "StringUtil.hpp"
#include "cadblock.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

[[nodiscard]] inline std::string_view CadBlockParamKindLabel(CadBlockParamKind kind) {
  switch (kind) {
    case CadBlockParamKind::Polar: return "Polar";
    case CadBlockParamKind::Rotation: return "Rotation";
    case CadBlockParamKind::Flip: return "Flip";
    case CadBlockParamKind::Visibility: return "Visibility";
    case CadBlockParamKind::Move: return "Move";
    case CadBlockParamKind::Lookup: return "Lookup";
    case CadBlockParamKind::Linear:
    default: return "Linear";
  }
}

[[nodiscard]] inline bool CadBlockIsDigits(std::string_view text) {
  return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

/// Nine significant digits, enough for decimal text to read back to the same float.
[[nodiscard]] inline std::string CadBlockFormatNumber(float value) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(value));
  return buf;
}

/// Parses a whole token as a finite number that fits a `float`. Locale-independent (always '.'), and a
/// leading '+' is accepted as typed input usually allows.
[[nodiscard]] inline bool CadBlockParseNumber(std::string_view raw, float* out) {
  std::string_view s = StringUtil::trimView(raw);
  if (!s.empty() && s.front() == '+')
    s.remove_prefix(1);
  if (s.empty())
    return false;
  double v = 0.0;
  const std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), v);
  if (r.ec != std::errc() || r.ptr != s.data() + s.size())
    return false;
  if (!std::isfinite(v) || std::fabs(v) > static_cast<double>(std::numeric_limits<float>::max()))
    return false;
  if (out != nullptr)
    *out = static_cast<float>(v);
  return true;
}

/// Parses a 1-based list or row number: digits only, 1 or more.
[[nodiscard]] inline bool CadBlockParseListNumber(std::string_view raw, int* out) {
  const std::string_view s = StringUtil::trimView(raw);
  if (s.size() > 9 || !CadBlockIsDigits(s))
    return false;
  int n = 0;
  for (const char c : s) {
    n = n * 10 + (c - '0');
  }
  if (n < 1)
    return false;
  if (out != nullptr)
    *out = n;
  return true;
}

[[nodiscard]] inline int CadBlockParamNameCount(const CadBlockDefinition& def, std::string_view name) {
  int n = 0;
  for (const CadBlockParameter& p : def.parameters) {
    if (CadBlockEqCi(p.name, name))
      ++n;
  }
  return n;
}

/// Index of the first parameter named `name`, or -1.
[[nodiscard]] inline int CadBlockFindParameter(const CadBlockDefinition& def, std::string_view name) {
  for (size_t i = 0; i < def.parameters.size(); ++i) {
    if (CadBlockEqCi(def.parameters[i].name, name))
      return static_cast<int>(i);
  }
  return -1;
}

[[nodiscard]] inline int CadBlockActionsBoundTo(const CadBlockDefinition& def, std::string_view name) {
  int n = 0;
  for (const CadBlockAction& a : def.actions) {
    if (CadBlockEqCi(a.paramName, name))
      ++n;
  }
  return n;
}

/// Resolves a typed `<parameter>`: a list number (a token of digits only) or a name. Returns the
/// 0-based index, or -1 with the reason in \p err.
[[nodiscard]] inline int CadBlockResolveParameter(const CadBlockDefinition& def, std::string_view tokenRaw,
                                                  std::string* err) {
  const std::string_view token = StringUtil::trimView(tokenRaw);
  const auto fail = [err](std::string reason) {
    if (err != nullptr)
      *err = std::move(reason);
    return -1;
  };
  if (token.empty())
    return fail("name a parameter or give its list number.");
  if (CadBlockIsDigits(token)) {
    int n = 0;
    if (!CadBlockParseListNumber(token, &n) || static_cast<size_t>(n) > def.parameters.size())
      return fail("there is no parameter " + std::string(token) + " (the block has " +
                  std::to_string(def.parameters.size()) + ").");
    return n - 1;
  }
  const int count = CadBlockParamNameCount(def, token);
  if (count == 0)
    return fail("no parameter named \"" + std::string(token) + "\".");
  if (count > 1)
    return fail(std::to_string(count) + " parameters are named \"" + std::string(token) +
                "\"; use the list number instead.");
  return CadBlockFindParameter(def, token);
}

[[nodiscard]] inline int CadBlockTableColumnIndex(const CadBlockDefinition& def, std::string_view name) {
  for (size_t i = 0; i < def.propertyTable.columns.size(); ++i) {
    if (CadBlockEqCi(def.propertyTable.columns[i], name))
      return static_cast<int>(i);
  }
  return -1;
}

/// True when the table column matching a parameter's name takes its rules from parameter `paramIdx` — the
/// first parameter of that name, which is the one a column was added for.
[[nodiscard]] inline bool CadBlockTableColumnBelongsTo(const CadBlockDefinition& def, int paramIdx) {
  if (paramIdx < 0 || static_cast<size_t>(paramIdx) >= def.parameters.size())
    return false;
  const std::string& name = def.parameters[static_cast<size_t>(paramIdx)].name;
  return CadBlockTableColumnIndex(def, name) >= 0 && CadBlockFindParameter(def, name) == paramIdx;
}

/// The cell a parameter contributes to a new row or a new column: its current value, or for a
/// visibility parameter the block's first visibility state (empty when it has none).
[[nodiscard]] inline std::string CadBlockTableDefaultCell(const CadBlockDefinition& def,
                                                          const CadBlockParameter& param) {
  if (param.kind == CadBlockParamKind::Visibility)
    return def.visibilityStates.empty() ? std::string() : def.visibilityStates.front();
  return CadBlockFormatNumber(param.value);
}

/// The first visibility state makes existing empty visibility cells valid (REQ-525 item 5).
inline void CadBlockTableFillEmptyVisibilityCells(CadBlockDefinition& def, std::string_view state) {
  for (size_t column = 0; column < def.propertyTable.columns.size(); ++column) {
    const int parameterIndex = CadBlockFindParameter(def, def.propertyTable.columns[column]);
    if (parameterIndex < 0 ||
        def.parameters[static_cast<size_t>(parameterIndex)].kind != CadBlockParamKind::Visibility)
      continue;
    for (std::vector<std::string>& row : def.propertyTable.rows) {
      if (column < row.size() && row[column].empty())
        row[column] = state;
    }
  }
}

/// Checks \p raw against the rules of \p param's column and writes the text to store. REQ-525 item 5.
[[nodiscard]] inline std::string CadBlockTableCanonCell(const CadBlockDefinition& def,
                                                        const CadBlockParameter& param, std::string_view raw,
                                                        std::string* out) {
  assert(out != nullptr);
  const std::string_view text = StringUtil::trimView(raw);
  if (param.kind == CadBlockParamKind::Visibility) {
    if (def.visibilityStates.empty()) {
      if (!text.empty())
        return "the block has no visibility states, so \"" + param.name + "\" must be left empty.";
      out->clear();
      return {};
    }
    for (const std::string& state : def.visibilityStates) {
      if (CadBlockEqCi(state, text)) {
        *out = state;
        return {};
      }
    }
    std::string list;
    for (const std::string& state : def.visibilityStates)
      list += (list.empty() ? "" : ", ") + state;
    return "\"" + param.name + "\" must be one of the block's visibility states (" + list + ").";
  }
  float v = 0.f;
  if (!CadBlockParseNumber(text, &v))
    return "\"" + param.name + "\" must be a number.";
  if (v < param.minValue || v > param.maxValue)
    return "\"" + param.name + "\" must be from " + CadBlockFormatNumber(param.minValue) + " to " +
           CadBlockFormatNumber(param.maxValue) + ".";
  *out = CadBlockFormatNumber(v);
  return {};
}

/// 1-based number of the row equal to \p cells, skipping \p exceptRow (0-based, -1 = none); 0 = none.
[[nodiscard]] inline int CadBlockTableFindRow(const CadBlockPropertyTable& table,
                                              const std::vector<std::string>& cells, int exceptRow) {
  for (size_t r = 0; r < table.rows.size(); ++r) {
    if (static_cast<int>(r) != exceptRow && table.rows[r] == cells)
      return static_cast<int>(r) + 1;
  }
  return 0;
}

// --- parameters (REQ-524) -----------------------------------------------------------------------

[[nodiscard]] inline std::string CadBlockParamRename(CadBlockDefinition& def, int idx, std::string_view newNameRaw) {
  if (idx < 0 || static_cast<size_t>(idx) >= def.parameters.size())
    return "that parameter no longer exists.";
  const std::string newName(StringUtil::trimView(newNameRaw));
  if (newName.empty())
    return "a parameter needs a name.";
  if (newName.find(',') != std::string::npos)
    return "a parameter name cannot contain a comma.";
  if (std::all_of(newName.begin(), newName.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
    return "a parameter name cannot contain only digits; command arguments treat numbers as list positions.";
  for (size_t i = 0; i < def.parameters.size(); ++i) {
    if (static_cast<int>(i) != idx && CadBlockEqCi(def.parameters[i].name, newName))
      return "\"" + newName + "\" is already used by another parameter.";
  }
  const std::string oldName = def.parameters[static_cast<size_t>(idx)].name;
  if (CadBlockParamNameCount(def, oldName) > 1 && CadBlockFindParameter(def, oldName) == idx &&
      CadBlockTableColumnIndex(def, oldName) >= 0)
    return "remove the \"" + oldName + "\" table column before renaming its parameter; another parameter shares the name.";
  // Actions and the table column are bound by name. They follow the rename only when the old name
  // was this parameter's alone; shared, they stay with the parameter that keeps it.
  if (CadBlockParamNameCount(def, oldName) == 1) {
    for (CadBlockAction& a : def.actions) {
      if (CadBlockEqCi(a.paramName, oldName))
        a.paramName = newName;
    }
    const int col = CadBlockTableColumnIndex(def, oldName);
    if (col >= 0)
      def.propertyTable.columns[static_cast<size_t>(col)] = newName;
  }
  def.parameters[static_cast<size_t>(idx)].name = newName;
  return {};
}

enum class CadBlockParamField : std::uint8_t { Value = 0, Min, Max };

[[nodiscard]] inline bool CadBlockParamFieldFromCommand(std::string_view command, CadBlockParamField* out) {
  if (command == "value") {
    *out = CadBlockParamField::Value;
    return true;
  }
  if (command == "min") {
    *out = CadBlockParamField::Min;
    return true;
  }
  if (command == "max") {
    *out = CadBlockParamField::Max;
    return true;
  }
  return false;
}

[[nodiscard]] inline std::string_view CadBlockParamFieldLabel(CadBlockParamField field) {
  switch (field) {
    case CadBlockParamField::Min: return "minimum";
    case CadBlockParamField::Max: return "maximum";
    case CadBlockParamField::Value:
    default: return "value";
  }
}

[[nodiscard]] inline float CadBlockParamFieldValue(const CadBlockParameter& parameter, CadBlockParamField field) {
  switch (field) {
    case CadBlockParamField::Min: return parameter.minValue;
    case CadBlockParamField::Max: return parameter.maxValue;
    case CadBlockParamField::Value:
    default: return parameter.value;
  }
}

[[nodiscard]] inline std::string CadBlockParamSetNumber(CadBlockDefinition& def, int idx, CadBlockParamField field,
                                                        std::string_view raw) {
  if (idx < 0 || static_cast<size_t>(idx) >= def.parameters.size())
    return "that parameter no longer exists.";
  CadBlockParameter& p = def.parameters[static_cast<size_t>(idx)];
  float v = 0.f;
  if (!CadBlockParseNumber(raw, &v)) {
    return "the " + std::string(CadBlockParamFieldLabel(field)) + " must be a number.";
  }
  if (field == CadBlockParamField::Value) {
    if (v < p.minValue || v > p.maxValue)
      return "the value must be from " + CadBlockFormatNumber(p.minValue) + " to " +
             CadBlockFormatNumber(p.maxValue) + " (change the minimum or maximum first).";
    p.value = v;
    return {};
  }
  const bool isMin = field == CadBlockParamField::Min;
  if (isMin && v > p.maxValue)
    return "the minimum cannot be above the maximum (" + CadBlockFormatNumber(p.maxValue) + ").";
  if (!isMin && v < p.minValue)
    return "the maximum cannot be below the minimum (" + CadBlockFormatNumber(p.minValue) + ").";
  if (isMin ? p.value < v : p.value > v)
    return std::string(isMin ? "the minimum cannot be above" : "the maximum cannot be below") +
           " the current value (" + CadBlockFormatNumber(p.value) + ").";
  // A limit must not strand a block-table cell outside it (REQ-524 item 4).
  if (p.kind != CadBlockParamKind::Visibility && CadBlockTableColumnBelongsTo(def, idx)) {
    const size_t col = static_cast<size_t>(CadBlockTableColumnIndex(def, p.name));
    for (size_t r = 0; r < def.propertyTable.rows.size(); ++r) {
      float cell = 0.f;
      if (col >= def.propertyTable.rows[r].size() || !CadBlockParseNumber(def.propertyTable.rows[r][col], &cell))
        continue;
      if (isMin ? cell < v : cell > v)
        return "row " + std::to_string(r + 1) + " of the block table holds " + CadBlockFormatNumber(cell) +
               (isMin ? ", below that minimum." : ", above that maximum.");
    }
  }
  (isMin ? p.minValue : p.maxValue) = v;
  return {};
}

/// Removes column \p col and its cells; a table with no columns has no rows either.
inline void CadBlockTableEraseColumn(CadBlockPropertyTable& table, size_t col) {
  table.columns.erase(table.columns.begin() + static_cast<std::ptrdiff_t>(col));
  for (std::vector<std::string>& row : table.rows) {
    if (col < row.size())
      row.erase(row.begin() + static_cast<std::ptrdiff_t>(col));
  }
  if (table.columns.empty())
    table.rows.clear();
}

[[nodiscard]] inline std::string CadBlockParamDelete(CadBlockDefinition& def, int idx, int* actionsRemoved) {
  if (actionsRemoved != nullptr)
    *actionsRemoved = 0;
  if (idx < 0 || static_cast<size_t>(idx) >= def.parameters.size())
    return "that parameter no longer exists.";
  const std::string name = def.parameters[static_cast<size_t>(idx)].name;
  if (CadBlockParamNameCount(def, name) > 1 && CadBlockFindParameter(def, name) == idx &&
      CadBlockTableColumnIndex(def, name) >= 0)
    return "remove the \"" + name + "\" table column before deleting its parameter; another parameter shares the name.";
  // Bound by name: another parameter of the same name keeps the actions and the column.
  if (CadBlockParamNameCount(def, name) == 1) {
    int removed = 0;
    for (size_t i = def.actions.size(); i-- > 0;) {
      if (CadBlockEqCi(def.actions[i].paramName, name)) {
        def.actions.erase(def.actions.begin() + static_cast<std::ptrdiff_t>(i));
        ++removed;
      }
    }
    if (actionsRemoved != nullptr)
      *actionsRemoved = removed;
    const int col = CadBlockTableColumnIndex(def, name);
    if (col >= 0)
      CadBlockTableEraseColumn(def.propertyTable, static_cast<size_t>(col));
  }
  def.parameters.erase(def.parameters.begin() + idx);
  return {};
}

// --- block properties table (REQ-525) -------------------------------------------------------------

[[nodiscard]] inline std::string CadBlockTableAddColumn(CadBlockDefinition& def, int paramIdx) {
  if (paramIdx < 0 || static_cast<size_t>(paramIdx) >= def.parameters.size())
    return "that parameter no longer exists.";
  const CadBlockParameter& p = def.parameters[static_cast<size_t>(paramIdx)];
  if (CadBlockParamNameCount(def, p.name) > 1)
    return "more than one parameter is named \"" + p.name + "\"; rename one first (PARAMETERS).";
  if (CadBlockTableColumnIndex(def, p.name) >= 0)
    return "\"" + p.name + "\" is already in the table.";
  std::string cell;
  const std::string reason = CadBlockTableCanonCell(def, p, CadBlockTableDefaultCell(def, p), &cell);
  if (!reason.empty())
    return reason;
  def.propertyTable.columns.push_back(p.name);
  for (std::vector<std::string>& row : def.propertyTable.rows)
    row.push_back(cell);
  return {};
}

[[nodiscard]] inline std::string CadBlockTableRemoveColumn(CadBlockDefinition& def, std::string_view nameRaw) {
  const std::string_view name = StringUtil::trimView(nameRaw);
  const int col = CadBlockTableColumnIndex(def, name);
  if (col < 0)
    return "\"" + std::string(name) + "\" is not in the table.";
  CadBlockTableEraseColumn(def.propertyTable, static_cast<size_t>(col));
  return {};
}

/// Checks one row's cells against their columns' rules and writes the text to store.
[[nodiscard]] inline std::string CadBlockTableCanonRow(const CadBlockDefinition& def,
                                                       const std::vector<std::string>& values,
                                                       std::vector<std::string>* out) {
  assert(out != nullptr);
  const CadBlockPropertyTable& t = def.propertyTable;
  assert(values.size() == t.columns.size());
  out->assign(t.columns.size(), std::string());
  for (size_t c = 0; c < t.columns.size(); ++c) {
    const int pi = CadBlockFindParameter(def, t.columns[c]);
    if (pi < 0)
      return "the table column \"" + t.columns[c] + "\" has no parameter; remove the column.";
    const std::string err = CadBlockTableCanonCell(def, def.parameters[static_cast<size_t>(pi)], values[c], &(*out)[c]);
    if (!err.empty())
      return err;
  }
  return {};
}

/// Adds a row. Empty \p values takes each column's parameter default.
[[nodiscard]] inline std::string CadBlockTableAddRow(CadBlockDefinition& def, const std::vector<std::string>& values) {
  CadBlockPropertyTable& t = def.propertyTable;
  if (t.columns.empty())
    return "add a parameter to the table first (BTABLE ADDCOLUMN).";
  std::vector<std::string> cells;
  if (values.empty()) {
    for (const std::string& column : t.columns) {
      const int pi = CadBlockFindParameter(def, column);
      if (pi < 0)
        return "the table column \"" + column + "\" has no parameter; remove the column.";
      cells.emplace_back();
      const std::string reason = CadBlockTableCanonCell(
          def, def.parameters[static_cast<size_t>(pi)],
          CadBlockTableDefaultCell(def, def.parameters[static_cast<size_t>(pi)]), &cells.back());
      if (!reason.empty())
        return reason;
    }
  } else {
    if (values.size() != t.columns.size())
      return "the table has " + std::to_string(t.columns.size()) + " column(s) but " +
             std::to_string(values.size()) + " value(s) were given.";
    const std::string err = CadBlockTableCanonRow(def, values, &cells);
    if (!err.empty())
      return err;
  }
  if (const int same = CadBlockTableFindRow(t, cells, -1); same > 0)
    return "row " + std::to_string(same) + " already holds those values.";
  t.rows.push_back(std::move(cells));
  return {};
}

[[nodiscard]] inline std::string CadBlockTableRowRangeError(const CadBlockPropertyTable& table, int row1) {
  return "there is no row " + std::to_string(row1) + " (the table has " + std::to_string(table.rows.size()) + ").";
}

/// Sets one cell. \p row1 is 1-based; \p columnName names the column's parameter.
[[nodiscard]] inline std::string CadBlockTableSetCell(CadBlockDefinition& def, int row1, std::string_view columnName,
                                                      std::string_view raw) {
  CadBlockPropertyTable& t = def.propertyTable;
  if (row1 < 1 || static_cast<size_t>(row1) > t.rows.size())
    return CadBlockTableRowRangeError(t, row1);
  const int col = CadBlockTableColumnIndex(def, StringUtil::trimView(columnName));
  if (col < 0)
    return "\"" + std::string(StringUtil::trimView(columnName)) + "\" is not in the table.";
  const int pi = CadBlockFindParameter(def, t.columns[static_cast<size_t>(col)]);
  if (pi < 0)
    return "the table column \"" + t.columns[static_cast<size_t>(col)] + "\" has no parameter; remove the column.";
  std::string cell;
  const std::string err = CadBlockTableCanonCell(def, def.parameters[static_cast<size_t>(pi)], raw, &cell);
  if (!err.empty())
    return err;
  std::vector<std::string> cells = t.rows[static_cast<size_t>(row1 - 1)];
  cells.resize(t.columns.size());
  cells[static_cast<size_t>(col)] = std::move(cell);
  if (const int same = CadBlockTableFindRow(t, cells, row1 - 1); same > 0)
    return "row " + std::to_string(same) + " already holds those values.";
  t.rows[static_cast<size_t>(row1 - 1)] = std::move(cells);
  return {};
}

[[nodiscard]] inline std::string CadBlockTableRemoveRow(CadBlockDefinition& def, int row1) {
  CadBlockPropertyTable& t = def.propertyTable;
  if (row1 < 1 || static_cast<size_t>(row1) > t.rows.size())
    return CadBlockTableRowRangeError(t, row1);
  t.rows.erase(t.rows.begin() + (row1 - 1));
  return {};
}

[[nodiscard]] inline std::string CadBlockTableClear(CadBlockDefinition& def) {
  if (def.propertyTable.columns.empty())
    return "the block has no table.";
  def.propertyTable = CadBlockPropertyTable{};
  return {};
}

/// Sets \p ref's parameters and visibility state from row \p row1 (1-based) of its definition's table.
/// False when the definition has no such row. REQ-525 item 6.
inline bool CadBlockTableApplyRow(const CadBlockDefinition& def, int row1, CadBlockRef* ref) {
  assert(ref != nullptr);
  const CadBlockPropertyTable& t = def.propertyTable;
  if (row1 < 1 || static_cast<size_t>(row1) > t.rows.size())
    return false;
  const std::vector<std::string>& cells = t.rows[static_cast<size_t>(row1 - 1)];
  for (size_t c = 0; c < t.columns.size() && c < cells.size(); ++c) {
    const int pi = CadBlockFindParameter(def, t.columns[c]);
    if (pi < 0)
      continue;
    const CadBlockParameter& p = def.parameters[static_cast<size_t>(pi)];
    if (p.kind == CadBlockParamKind::Visibility) {
      ref->visState = cells[c];
      continue;
    }
    float v = 0.f;
    if (CadBlockParseNumber(cells[c], &v))
      CadBlockParamSet(ref, p.name, v);
  }
  return true;
}

// --- change detection ---------------------------------------------------------------------------

/// True when the parts of a definition these functions edit are equal. Lets the command layer skip
/// the undo step for an edit that changed nothing (a value set to what it already was).
[[nodiscard]] inline bool CadBlockAuthoringEqual(const CadBlockDefinition& a, const CadBlockDefinition& b) {
  if (a.parameters.size() != b.parameters.size() || a.actions.size() != b.actions.size())
    return false;
  for (size_t i = 0; i < a.parameters.size(); ++i) {
    const CadBlockParameter& x = a.parameters[i];
    const CadBlockParameter& y = b.parameters[i];
    if (x.name != y.name || x.kind != y.kind || x.value != y.value || x.minValue != y.minValue ||
        x.maxValue != y.maxValue)
      return false;
  }
  for (size_t i = 0; i < a.actions.size(); ++i) {
    if (a.actions[i].paramName != b.actions[i].paramName || a.actions[i].kind != b.actions[i].kind)
      return false;
  }
  return a.propertyTable.columns == b.propertyTable.columns && a.propertyTable.rows == b.propertyTable.rows;
}
