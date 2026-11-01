#pragma once

/// Drawing TABLE layout helpers (REQ-148). Header-only so Catch2 can cover cell geometry without GL.
/// CadTable is a first-class entity (D-2026-08-28-i): insertion, size, rotation, and cell strings.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct CadTableCellRect {
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
};

/// Model-space TABLE. Local origin is the unrotated top-left (\ref insX, \ref insY). Local +X is
/// along the top edge; local +Y-down is toward the bottom edge. World (when \ref localYFlipped is
/// false):
///   wx = insX + lx * cos(r) + lyDown * sin(r)
///   wy = insY + lx * sin(r) - lyDown * cos(r)
/// When \ref localYFlipped is true the \c lyDown terms are negated so the body lies on the opposite
/// side of the top edge (needed after an odd number of reflections).
struct CadTable {
  float insX = 0.f;
  float insY = 0.f;
  float insZ = 0.f;
  float width = 48.f;
  float height = 4.f;
  float rotationRad = 0.f;
  bool localYFlipped = false;
  int cols = 2;
  std::vector<std::string> cells;
  float plottedHeightInches = 0.125f;
  std::string fontFamily;
  /// Table style name (REQ-475). Empty for tables made before table styles existed; they draw as Standard.
  std::string styleName;
  std::vector<float> columnWeights;
  std::vector<float> rowWeights;
  std::vector<float> rowTextHeightsInches;
  bool mergeTitleRow = false;
  bool centerText = false;
  /// When true, data rows are filled from survey points; cell text must not be edited in place.
  bool surveyPointDataRows = false;
};

[[nodiscard]] inline int CadTableRowCount(int cols, const std::vector<std::string>& cells) {
  if (cols <= 0)
    return 0;
  const int n = static_cast<int>(cells.size());
  return (n + cols - 1) / cols;
}

[[nodiscard]] inline int CadTableRowCount(const CadTable& t) {
  return std::max(1, CadTableRowCount(t.cols, t.cells));
}

[[nodiscard]] inline float CadTableHeightWorld(const CadTable& t, float modelUnitsPerPlottedInch) {
  return t.plottedHeightInches * std::max(modelUnitsPerPlottedInch, 1.e-6f);
}

/// A band boundary scaled to the current table size. Invalid/incomplete weights use equal bands.
[[nodiscard]] inline float CadTableBandBoundary(const std::vector<float>& weights, int bands,
                                               int boundary, float extent) {
  if (bands <= 0) return 0;
  boundary = std::clamp(boundary, 0, bands);
  if (weights.size() == static_cast<std::size_t>(bands)) {
    double total = 0, before = 0;
    bool valid = true;
    for (int i = 0; i < bands; ++i) {
      if (!std::isfinite(weights[i]) || weights[i] <= 0) { valid = false; break; }
      total += weights[i];
      if (i < boundary) before += weights[i];
    }
    if (valid && total > 0) return static_cast<float>(extent * before / total);
  }
  return extent * static_cast<float>(boundary) / static_cast<float>(bands);
}

/// Cell rectangle in local coordinates, with y increasing downwards. Covered title cells are empty.
[[nodiscard]] inline CadTableCellRect CadTableLocalCellRect(const CadTable& t, int index) {
  CadTableCellRect rect;
  const int rows = CadTableRowCount(t);
  if (t.cols <= 0 || index < 0 || index >= rows * t.cols) return rect;
  const int row = index / t.cols, column = index % t.cols;
  if (t.mergeTitleRow && row == 0 && column != 0) return rect;
  rect.x0 = CadTableBandBoundary(t.columnWeights, t.cols, column, t.width);
  rect.x1 = t.mergeTitleRow && row == 0 ? t.width :
      CadTableBandBoundary(t.columnWeights, t.cols, column + 1, t.width);
  rect.y0 = CadTableBandBoundary(t.rowWeights, rows, row, t.height);
  rect.y1 = CadTableBandBoundary(t.rowWeights, rows, row + 1, t.height);
  return rect;
}

inline void CadTableLocalToWorld(const CadTable& t, float lx, float lyDown, float* wx, float* wy) {
  if (!wx || !wy)
    return;
  const float c = std::cos(t.rotationRad);
  const float s = std::sin(t.rotationRad);
  const float ly = t.localYFlipped ? -lyDown : lyDown;
  *wx = t.insX + lx * c + ly * s;
  *wy = t.insY + lx * s - ly * c;
}

inline void CadTableWorldToLocal(const CadTable& t, float wx, float wy, float* lx, float* lyDown) {
  if (!lx || !lyDown)
    return;
  const float dx = wx - t.insX;
  const float dy = wy - t.insY;
  const float c = std::cos(t.rotationRad);
  const float s = std::sin(t.rotationRad);
  *lx = dx * c + dy * s;
  const float ly = dx * s - dy * c;
  *lyDown = t.localYFlipped ? -ly : ly;
}

/// Corners in world: 0 = top-left, 1 = top-right, 2 = bottom-right, 3 = bottom-left.
inline void CadTableWorldCorner(const CadTable& t, int corner, float* wx, float* wy) {
  const float w = std::max(t.width, 1.e-3f);
  const float h = std::max(t.height, 1.e-3f);
  float lx = 0.f;
  float ly = 0.f;
  switch (corner) {
  case 1:
    lx = w;
    break;
  case 2:
    lx = w;
    ly = h;
    break;
  case 3:
    ly = h;
    break;
  default:
    break;
  }
  CadTableLocalToWorld(t, lx, ly, wx, wy);
}

inline void CadTableWorldAabb(const CadTable& t, float* mnX, float* mnY, float* mxX, float* mxY) {
  if (!mnX || !mnY || !mxX || !mxY)
    return;
  float x = 0.f, y = 0.f;
  CadTableWorldCorner(t, 0, &x, &y);
  *mnX = *mxX = x;
  *mnY = *mxY = y;
  for (int i = 1; i < 4; ++i) {
    CadTableWorldCorner(t, i, &x, &y);
    *mnX = std::min(*mnX, x);
    *mxX = std::max(*mxX, x);
    *mnY = std::min(*mnY, y);
    *mxY = std::max(*mxY, y);
  }
}

/// Equal-width, equal-height cells filling [boxMinX, boxMaxX] x [boxMinY, boxMaxY].
/// Row 0 is the top of the box (larger Y). Used for axis-aligned legacy annotation tables.
inline void CadTableLayoutCells(float boxMinX, float boxMinY, float boxMaxX, float boxMaxY, int cols,
                                const std::vector<std::string>& cells, std::vector<CadTableCellRect>* out) {
  if (!out)
    return;
  out->clear();
  if (cols <= 0)
    return;
  const int rows = std::max(1, CadTableRowCount(cols, cells));
  const float x0 = std::min(boxMinX, boxMaxX);
  const float x1 = std::max(boxMinX, boxMaxX);
  const float y0 = std::min(boxMinY, boxMaxY);
  const float y1 = std::max(boxMinY, boxMaxY);
  const float w = std::max(x1 - x0, 1.e-3f);
  const float h = std::max(y1 - y0, 1.e-3f);
  const float cw = w / static_cast<float>(cols);
  const float rh = h / static_cast<float>(rows);
  const int n = rows * cols;
  out->resize(static_cast<size_t>(n));
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      CadTableCellRect cell;
      cell.x0 = x0 + static_cast<float>(c) * cw;
      cell.x1 = cell.x0 + cw;
      cell.y1 = y1 - static_cast<float>(r) * rh;
      cell.y0 = cell.y1 - rh;
      (*out)[static_cast<size_t>(r * cols + c)] = cell;
    }
  }
}

/// World AABB of each cell (unrotated local layout mapped through the table's insertion/rotation).
inline void CadTableLayoutWorldCells(const CadTable& t, std::vector<CadTableCellRect>* out) {
  if (!out)
    return;
  out->clear();
  if (t.cols <= 0)
    return;
  const int rows = CadTableRowCount(t);
  const int n = rows * t.cols;
  out->resize(static_cast<size_t>(n));
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < t.cols; ++c) {
      const auto local = CadTableLocalCellRect(t, r * t.cols + c);
      const float lx0 = local.x0, ly0 = local.y0;
      const float cw = local.x1 - local.x0, rh = local.y1 - local.y0;
      float x00 = 0.f, y00 = 0.f, x10 = 0.f, y10 = 0.f, x01 = 0.f, y01 = 0.f, x11 = 0.f, y11 = 0.f;
      CadTableLocalToWorld(t, lx0, ly0, &x00, &y00);
      CadTableLocalToWorld(t, lx0 + cw, ly0, &x10, &y10);
      CadTableLocalToWorld(t, lx0, ly0 + rh, &x01, &y01);
      CadTableLocalToWorld(t, lx0 + cw, ly0 + rh, &x11, &y11);
      CadTableCellRect cell;
      cell.x0 = std::min(std::min(x00, x10), std::min(x01, x11));
      cell.x1 = std::max(std::max(x00, x10), std::max(x01, x11));
      cell.y0 = std::min(std::min(y00, y10), std::min(y01, y11));
      cell.y1 = std::max(std::max(y00, y10), std::max(y01, y11));
      (*out)[static_cast<size_t>(r * t.cols + c)] = cell;
    }
  }
}

[[nodiscard]] inline int CadTableVisibleCharCount(std::string_view s) {
  int best = 0;
  int n = 0;
  for (unsigned char c : s) {
    if (c == '\n') {
      best = std::max(best, n);
      n = 0;
      continue;
    }
    if ((c & 0xC0) != 0x80)
      ++n;
  }
  return std::max(best, n);
}

/// Glyph width as a fraction of text height (wider than average so TTF text stays inside the cell).
inline constexpr float kCadTableEmPerHeight = 0.78f;
/// Horizontal padding on each side of a cell, in text-heights.
inline constexpr float kCadTableCellPadXEm = 0.45f;
/// Row height as a multiple of text height (leading + padding).
inline constexpr float kCadTableRowToText = 1.60f;

[[nodiscard]] inline float CadTableEstimatedTextWidth(std::string_view s, float textHeightWorld) {
  const float h = std::max(textHeightWorld, 1.e-6f);
  const int n = std::max(1, CadTableVisibleCharCount(s));
  return static_cast<float>(n) * h * kCadTableEmPerHeight;
}

/// Size the axis-aligned box so equal-width columns and equal-height rows contain the cell strings
/// at the table's plotted height. Grows from insertion (top-left); does not move `insX`/`insY`.
/// When Civil column widths are absent, distribute the table width by each column's widest cell.
inline void CadTableFitColumnWeightsFromContent(CadTable* t, float modelUnitsPerPlottedInch) {
  if (!t || t->cols <= 0)
    return;
  const int rows = CadTableRowCount(*t);
  if (rows <= 0)
    return;
  const float mupi = modelUnitsPerPlottedInch > 1.e-6f ? modelUnitsPerPlottedInch : 1.f;
  std::vector<float> widest(static_cast<size_t>(t->cols), 0.f);
  for (int row = 0; row < rows; ++row) {
    const float rowHeightInches = row < static_cast<int>(t->rowTextHeightsInches.size()) ?
        t->rowTextHeightsInches[static_cast<size_t>(row)] : t->plottedHeightInches;
    const float textHeightWorld = rowHeightInches * mupi;
    for (int column = 0; column < t->cols; ++column) {
      if (t->mergeTitleRow && row == 0 && column != 0)
        continue;
      const std::size_t index = static_cast<size_t>(row * t->cols + column);
      if (index >= t->cells.size())
        break;
      widest[static_cast<size_t>(column)] = std::max(
          widest[static_cast<size_t>(column)], CadTableEstimatedTextWidth(t->cells[index], textHeightWorld));
    }
  }
  const float pad = t->plottedHeightInches * mupi * kCadTableCellPadXEm * 2.f;
  t->columnWeights.clear();
  for (float width : widest)
    t->columnWeights.push_back(std::max(width + pad, 1.f));
}

inline void CadTableFitToContent(CadTable* t, float modelUnitsPerPlottedInch) {
  if (!t || t->cols <= 0)
    return;
  const float hText = CadTableHeightWorld(*t, modelUnitsPerPlottedInch);
  const float padX = hText * kCadTableCellPadXEm * 2.f;
  float maxInner = hText;
  for (const std::string& cell : t->cells)
    maxInner = std::max(maxInner, CadTableEstimatedTextWidth(cell, hText));
  const int rows = CadTableRowCount(*t);
  t->width = std::max(static_cast<float>(t->cols) * (maxInner + padX), 1.e-3f);
  t->height = std::max(static_cast<float>(rows) * hText * kCadTableRowToText, 1.e-3f);
}

[[nodiscard]] inline bool CadTableContainsLocal(const CadTable& t, float wx, float wy, float tol) {
  float lx = 0.f, ly = 0.f;
  CadTableWorldToLocal(t, wx, wy, &lx, &ly);
  const float w = std::max(t.width, 1.e-3f);
  const float h = std::max(t.height, 1.e-3f);
  return lx >= -tol && lx <= w + tol && ly >= -tol && ly <= h + tol;
}

/// Row-major cell index, or -1 if the point is outside the table.
[[nodiscard]] inline int CadTableHitCell(const CadTable& t, float wx, float wy) {
  if (t.cols <= 0)
    return -1;
  float lx = 0.f, ly = 0.f;
  CadTableWorldToLocal(t, wx, wy, &lx, &ly);
  const float w = std::max(t.width, 1.e-3f);
  const float h = std::max(t.height, 1.e-3f);
  if (lx < 0.f || lx > w || ly < 0.f || ly > h)
    return -1;
  const int rows = CadTableRowCount(t);
  int c = 0;
  int r = 0;
  while (c + 1 < t.cols && lx >= CadTableBandBoundary(t.columnWeights, t.cols, c + 1, w)) ++c;
  while (r + 1 < rows && ly >= CadTableBandBoundary(t.rowWeights, rows, r + 1, h)) ++r;
  if (t.mergeTitleRow && r == 0) return 0;
  return r * t.cols + c;
}

/// True when \p cellIndex is a survey-point data cell (not title/header) on a linked point table.
[[nodiscard]] inline bool CadTableCellIsSurveyPointData(const CadTable& t, int cellIndex) {
  if (!t.surveyPointDataRows || t.cols <= 0 || cellIndex < 0) return false;
  const int rows = CadTableRowCount(t);
  if (rows <= 0) return false;
  const int row = cellIndex / t.cols;
  const int firstDataRow = t.mergeTitleRow ? 2 : 1;
  return row >= firstDataRow && row < rows;
}

inline void CadTableExpandWidthForColumnWeights(CadTable* t) {
  if (!t || t->cols <= 0 || t->columnWeights.size() != static_cast<std::size_t>(t->cols)) return;
  float sum = 0.f;
  for (float weight : t->columnWeights) sum += weight;
  if (sum > t->width) t->width = sum;
}

/// Recompute column weights for point tables when layout metadata is missing.
inline void CadTableEnsurePointTableLayout(CadTable* t, float modelUnitsPerPlottedInch) {
  if (!t || t->cols <= 0) return;
  if (t->columnWeights.size() != static_cast<std::size_t>(t->cols))
    CadTableFitColumnWeightsFromContent(t, modelUnitsPerPlottedInch);
}

[[nodiscard]] inline CadTable CadTableFromAxisAlignedBox(float boxMinX, float boxMinY, float boxMaxX,
                                                         float boxMaxY, int cols,
                                                         std::vector<std::string> cells, float insZ,
                                                         float plottedHeightInches, std::string fontFamily) {
  CadTable t;
  t.insX = std::min(boxMinX, boxMaxX);
  t.insY = std::max(boxMinY, boxMaxY);
  t.insZ = insZ;
  t.width = std::max(std::fabs(boxMaxX - boxMinX), 1.e-3f);
  t.height = std::max(std::fabs(boxMaxY - boxMinY), 1.e-3f);
  t.cols = cols;
  t.cells = std::move(cells);
  t.plottedHeightInches = plottedHeightInches;
  t.fontFamily = std::move(fontFamily);
  return t;
}

inline void CadTableTranslate(CadTable* t, float dx, float dy) {
  if (!t)
    return;
  t->insX += dx;
  t->insY += dy;
}

inline void CadTableRotateAround(CadTable* t, float bx, float by, float rad) {
  if (!t)
    return;
  const float c = std::cos(rad);
  const float s = std::sin(rad);
  const float dx = t->insX - bx;
  const float dy = t->insY - by;
  t->insX = bx + dx * c - dy * s;
  t->insY = by + dx * s + dy * c;
  t->rotationRad += rad;
}

inline void CadTableScaleAround(CadTable* t, float bx, float by, float sc) {
  if (!t)
    return;
  t->insX = bx + sc * (t->insX - bx);
  t->insY = by + sc * (t->insY - by);
  t->width = std::max(t->width * sc, 1.e-3f);
  t->height = std::max(t->height * sc, 1.e-3f);
  t->plottedHeightInches = std::max(t->plottedHeightInches * sc, 1.e-6f);
}

inline void CadTableReflectAcrossLine(CadTable* t, float ax, float ay, float bx, float by) {
  if (!t)
    return;
  auto reflect = [&](float* px, float* py) {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float len2 = vx * vx + vy * vy;
    if (len2 < 1.e-18f)
      return;
    const float wx = *px - ax;
    const float wy = *py - ay;
    const float proj = (wx * vx + wy * vy) / len2;
    const float qx = ax + proj * vx;
    const float qy = ay + proj * vy;
    *px = 2.f * qx - *px;
    *py = 2.f * qy - *py;
  };
  float tlx = 0.f, tly = 0.f, trx = 0.f, try_ = 0.f, blx = 0.f, bly = 0.f;
  CadTableLocalToWorld(*t, 0.f, 0.f, &tlx, &tly);
  CadTableLocalToWorld(*t, std::max(t->width, 1.e-3f), 0.f, &trx, &try_);
  CadTableLocalToWorld(*t, 0.f, std::max(t->height, 1.e-3f), &blx, &bly);
  reflect(&tlx, &tly);
  reflect(&trx, &try_);
  reflect(&blx, &bly);
  t->insX = tlx;
  t->insY = tly;
  t->width = std::max(std::hypot(trx - tlx, try_ - tly), 1.e-3f);
  t->height = std::max(std::hypot(blx - tlx, bly - tly), 1.e-3f);
  t->rotationRad = std::atan2(try_ - tly, trx - tlx);
  t->localYFlipped = !t->localYFlipped;
}
