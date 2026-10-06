#pragma once

// REQ-390 / ADR-067 addendum 2 — the scale of a PDF page ("1 inch on the sheet = 20 feet"), kept as standard
// PDF measurement data (a page /VP Viewport holding a rectilinear /Measure) so Bluebeam and Acrobat read the
// same scale. The maths and the text building/parsing are pure; the file functions use PDFium to load the
// file and PdfRaw to edit the saved bytes.

#include "PdfViewerCore.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace pdfview {

enum class Unit { Inch, Foot, Yard, Mile, Millimetre, Centimetre, Metre, Kilometre };

const char* UnitLabel(Unit u);            ///< "in", "ft", "yd", "mi", "mm", "cm", "m", "km"
const char* UnitName(Unit u);             ///< "inch", "foot", ... for menus
double UnitInMetres(Unit u);
bool ParseUnit(const std::string& text, Unit& out); ///< labels, names, and ' / " for feet / inches
const std::vector<Unit>& AllUnits();

struct PageScale {
  double pageValue = 1.0; ///< this much on the sheet ...
  Unit pageUnit = Unit::Inch;
  double realValue = 1.0; ///< ... is this much in the real world
  Unit realUnit = Unit::Foot;
  std::string label;      ///< display text when it is a named preset ("1:100"); else built from the numbers

  bool Valid() const { return pageValue > 0.0 && realValue > 0.0; }
  double RealPerPoint() const;                         ///< real units (realUnit) per PDF point
  double PointsToReal(double pts) const { return pts * RealPerPoint(); }
  double SqPointsToReal(double sqPts) const { return sqPts * RealPerPoint() * RealPerPoint(); }
  std::string RatioText() const;                       ///< "1 in = 20 ft", or the preset's label
  bool operator==(const PageScale& o) const;
  bool operator!=(const PageScale& o) const { return !(*this == o); }
};

/// Two picked points \p pagePoints apart on the sheet are \p realValue \p realUnit apart in reality.
PageScale ScaleFromCalibration(double pagePoints, double realValue, Unit realUnit);

/// The architectural, engineering and metric presets, in menu order.
const std::vector<PageScale>& PresetScales();

/// "12.50" style number with a fixed count of decimals.
std::string FormatValue(double v, int decimals);

/// The rectilinear `<</Type/Measure ...>>` dictionary for a scale: the page's /VP holds it, and so does every
/// measurement annotation (REQ-391).
std::string BuildMeasureDict(const PageScale& s);

/// The `/VP[...]` entry for a page of this size and scale.
std::string BuildViewport(const PageScale& s, double x0, double y0, double x1, double y1);

/// Reads the scale out of a page object's text (references already expanded). Returns true with \p out set;
/// false with \p why empty when the page has no measure data, or non-empty when it has some that cannot be used.
bool ParseViewport(const std::string& expandedPageText, PageScale& out, std::string& why);

struct ScaleRead {
  std::map<int, PageScale> scales;     ///< zero-based page -> its scale, for pages that have one
  std::map<int, std::string> unusable; ///< page -> why its measure data cannot be used (REQ-201)
  std::string error;                   ///< non-empty when the file could not be read at all
};

/// Reads every page's scale from \p file (the file is rewritten in memory by PDFium so compressed object
/// streams from other programs can be read; nothing is written to disk).
ScaleRead ReadPageScales(const std::filesystem::path& file);

/// For each (page -> scale) in \p changes, replaces that page's /VP in the plain PDF bytes \p pdf (an invalid
/// scale removes it) and refreshes the cross-reference table. "" on success, else the reason; \p pdf is left
/// unchanged on failure.
std::string ApplyPageScales(std::string& pdf, const std::map<int, PageScale>& changes, const std::vector<PageSize>& sizes);

} // namespace pdfview
