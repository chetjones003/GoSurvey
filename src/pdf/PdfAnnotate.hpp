#pragma once

// REQ-388 / ADR-067 (e) — PDF annotations: text, line, rectangle, ellipse. Pure: PDFium only, no window.
// Edits are held in memory (AnnotSession, with undo/redo) and written only by SaveAnnotated ("Save As"),
// which never opens the original for writing. Coordinates are PDF user space: points, origin at the
// page's bottom-left, y up. PDFium must already be initialised.

#include <filesystem>
#include <string>
#include <vector>

namespace pdfview {

struct Annot {
  enum class Kind { Text, Line, Rect, Ellipse };
  Kind kind = Kind::Rect;
  int page = 0; ///< zero-based
  /// Line: start and end. Rect / Ellipse / Text: two opposite corners of the box (any order).
  float x0 = 0.f, y0 = 0.f, x1 = 0.f, y1 = 0.f;
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
  const std::vector<Annot>& Items() const { return items_; }
  int Add(const Annot& a);                     ///< returns the new item's index
  bool Remove(int index);
  bool Replace(int index, const Annot& a);
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  bool Undo();
  bool Redo();
  /// True when the list differs from the last MarkSaved (or from empty before any save).
  bool Dirty() const { return items_ != saved_; }
  void MarkSaved() { saved_ = items_; }

private:
  void Push();
  std::vector<Annot> items_, saved_;
  std::vector<std::vector<Annot>> undo_, redo_;
};

/// Writes \p source plus \p items to \p dest as a new PDF ("Save As"). The source is read and closed at once and
/// is never modified; \p dest equal to \p source is refused. Written to a temporary beside \p dest and renamed
/// into place, so a failure leaves no partial \p dest. Returns "" on success, otherwise a stated reason.
std::string SaveAnnotated(const std::filesystem::path& source, const std::vector<Annot>& items,
                          const std::filesystem::path& dest);

/// Reads back the annotations of \p file that SaveAnnotated writes (Text, Line, Rect, Ellipse); other
/// annotations in the file are skipped. Empty on any failure.
std::vector<Annot> ReadAnnotations(const std::filesystem::path& file);

/// A text annotation's size estimate in points (no font is loaded): used to size a new note's box.
void EstimateTextBox(const std::string& utf8, float fontSize, float& wPt, float& hPt);

} // namespace pdfview
