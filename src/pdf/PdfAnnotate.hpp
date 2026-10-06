#pragma once

// REQ-388 / ADR-067 (e) — PDF annotations: text, line, rectangle, ellipse. Pure: PDFium only, no window.
// Edits are held in memory (AnnotSession, with undo/redo) and written only by SaveAnnotated ("Save As"),
// which never opens the original for writing. Coordinates are PDF user space: points, origin at the
// page's bottom-left, y up. PDFium must already be initialised.

#include "PdfMeasure.hpp"
#include "PdfScaleCheck.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Shx {
class Font;
}

namespace pdfview {

struct Annot {
  /// Length, PolyLength, Area and Angle are the REQ-391 scaled dimensions: their geometry is `pts`, and their
  /// label is worked out from the page's scale (it is never stored, so a scale change keeps it true).
  enum class Kind { Text, Line, Rect, Ellipse, Leader, Length, PolyLength, Area, Angle };
  Kind kind = Kind::Rect;
  int page = 0; ///< zero-based
  /// Line: start and end. Rect / Ellipse / Text: two opposite corners of the box (any order).
  /// Leader (REQ-396): the box of the note (as Text, with kLeaderPad round the text) and `pts[0]`, the arrow tip.
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
  /// Dimensions: Length 2 points; PolyLength 2 or more; Area 3 or more (closed); Angle 3 (the middle one is the corner).
  std::vector<std::pair<float, float>> pts;
  int decimals = 2; ///< dimensions: digits after the point in the label
  /// Length only: how far the dimension line sits from the two measured points, perpendicular to them. Positive is
  /// to the left of the direction from the first point to the second; 0 puts the line on the points.
  float offset = 0.f;
  bool IsDimension() const { return kind >= Kind::Length; }
  unsigned color = 0xFF0000; ///< 0xRRGGBB: the stroke, and the text colour
  float thickness = 1.f;     ///< stroke width in points (Line, Rect, Ellipse)
  bool fill = false;         ///< Rect / Ellipse: fill with `color`
  std::string text;          ///< Text: UTF-8, '\n' starts a new line
  std::string font = "Helvetica"; ///< Text: family label (Helvetica, Times, Courier, or an installed TrueType family)
  bool bold = false;
  bool italic = false;
  std::string fontFile; ///< Text: a resolved .ttf to embed; empty = the standard PDF font for `font`
  float fontSize = 12.f; ///< Text: points

  bool operator==(const Annot& o) const;
  bool operator!=(const Annot& o) const { return !(*this == o); }
};

/// The in-memory edit list. Every change is one undo step; a new change clears redo.
class AnnotSession {
public:
  const std::vector<Annot>& Items() const { return state_.items; }
  /// Page scales set in this session (REQ-390): page -> scale; an invalid scale means "remove this page's scale".
  /// Pages not listed keep whatever scale the file already has.
  const std::map<int, PageScale>& Scales() const { return state_.scales; }
  /// One undo step; false when nothing changes. \p alsoAdd (optional) records a scale check in the same step: a
  /// calibration counts as the first check (REQ-394).
  bool SetScales(const std::map<int, PageScale>& changes, const ScaleCheck* alsoAdd = nullptr);
  /// REQ-394: the checks the user has made. They are working marks, undoable like the rest, but they are never
  /// written to the PDF and do not make the session "unsaved".
  const std::vector<ScaleCheck>& Checks() const { return state_.checks; }
  int AddCheck(const ScaleCheck& c);
  bool RemoveCheck(int index);
  int Add(const Annot& a);                     ///< returns the new item's index
  bool Remove(int index);
  bool Replace(int index, const Annot& a);
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  bool Undo();
  bool Redo();
  /// True when the list differs from the last MarkSaved (or from empty before any save).
  bool Dirty() const { return !(state_ == saved_); }
  void MarkSaved() { saved_ = state_; }

private:
  struct State {
    std::vector<Annot> items;
    std::map<int, PageScale> scales;
    std::vector<ScaleCheck> checks;
    bool operator==(const State& o) const { return items == o.items && scales == o.scales; } // checks are not saved
  };
  void Push();
  State state_, saved_;
  std::vector<State> undo_, redo_;
};

/// Writes \p source plus \p items to \p dest as a new PDF ("Save As"). The source is read and closed at once and
/// is never modified; \p dest equal to \p source is refused. Written to a temporary beside \p dest and renamed
/// into place, so a failure leaves no partial \p dest. Returns "" on success, otherwise a stated reason.
/// \p changes are the page scales the user set (REQ-390); \p pageScales is the scale in force on each page that
/// has a dimension (a dimension on a page missing from it is refused: its value would be a guess).
std::string SaveAnnotated(const std::filesystem::path& source, const std::vector<Annot>& items,
                          const std::filesystem::path& dest, const std::map<int, PageScale>& changes = {},
                          const std::map<int, PageScale>& pageScales = {});

/// REQ-391 values, in PDF points / square points / degrees (before the scale).
double PathLengthPt(const std::vector<std::pair<float, float>>& pts, bool closed);
double PolygonAreaSqPt(const std::vector<std::pair<float, float>>& pts);
double AngleDegrees(const std::vector<std::pair<float, float>>& pts); ///< at the middle of three points, 0..180
bool DimensionComplete(const Annot& a);                             ///< has enough points for its kind
/// A Length dimension's drawn line (REQ-391): the two measured points moved sideways by the offset, with the unit
/// normal (to the left of point 1 -> point 2) the offset is measured along.
struct DimLine {
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
  float nx = 0.f, ny = 1.f;
};
DimLine LengthDimLine(const Annot& a);
/// The angle of a dimension's label text in degrees, turned so it never reads upside down (0 for all but Length).
float DimensionLabelAngleDeg(const Annot& a);
/// Where a dimension's label is centred, in page points (the longest segment's middle, the area's centre, beside
/// the angle's corner); the saved file and the on-screen view use the same spot.
std::pair<float, float> DimensionLabelAnchor(const Annot& a);
/// The text a dimension shows, in \p scale's real units: "50.00 ft", "A = 1000.00 sq ft" + newline + "P = 140.00 ft",
/// or "37.50°" (an angle needs no scale).
std::string DimensionLabel(const Annot& a, const PageScale& scale);

/// Reads back the annotations of \p file that SaveAnnotated writes (Text, Line, Rect, Ellipse); other
/// annotations in the file are skipped. Empty on any failure.
std::vector<Annot> ReadAnnotations(const std::filesystem::path& file);

/// REQ-396: the padding between a Leader's box and its text, in points.
constexpr float kLeaderPad = 4.f;

/// A Leader's drawn line: from the point on its box nearest the tip (the middle of one edge) to the tip, plus the
/// two back corners of the arrowhead at the tip.
struct LeaderGeom {
  float sx = 0.f, sy = 0.f; ///< where the line leaves the box
  float tx = 0.f, ty = 0.f; ///< the tip
  float w1x = 0.f, w1y = 0.f, w2x = 0.f, w2y = 0.f; ///< the arrowhead's two back corners
};
LeaderGeom LeaderLine(const Annot& a);

/// REQ-397: the line pitch of stroke (SHX) text, as a multiple of its text height (CAD's 5/3 is too loose for notes).
constexpr float kStrokeLineSpacing = 1.5f;

/// The SHX stroke font to draw \p text in when \p family names one (romans.shx) and it is installed, else null
/// (use a TrueType / standard PDF font). Null too when the text has a degree sign: SHX fonts have no glyph for it.
Shx::Font* StrokeFontFor(const std::string& family, const std::string& text);

/// The size of \p utf8 set in \p family at \p size points (the text height for a stroke font): exact advances for a
/// stroke font, EstimateTextBox's estimate otherwise.
void MeasureText(const std::string& family, const std::string& utf8, float size, float& wPt, float& hPt);

/// A text annotation's size estimate in points (no font is loaded): used to size a new note's box.
void EstimateTextBox(const std::string& utf8, float fontSize, float& wPt, float& hPt);

} // namespace pdfview
