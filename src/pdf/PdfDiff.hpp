#pragma once

// REQ-393 — finding what changed between two revisions of a sheet, by what is DRAWN: both pages are rendered
// at a working resolution, the revision is laid onto the base through the alignment (PdfAlign), and the ink of
// one is compared with the ink of the other. Pure: bitmaps in, regions out; no PDFium, no window.

#include "PdfAlign.hpp"
#include "PdfAnnotate.hpp"

#include <functional>
#include <vector>

namespace pdfdiff {

/// REQ-393 clause 1 and 2. Lengths are millimetres at print size (1 mm = 72 / 25.4 points).
struct Settings {
  double toleranceMm = 1.0; ///< a mark within this of a mark on the other sheet is "the same" (anti-aliasing, tiny shifts)
  double minSizeMm = 2.0;   ///< a change region whose longer side is under this is a speck and is dropped
  double mergeMm = 4.0;     ///< differences closer than this belong to one region
};

enum class Kind { Added, Removed, Changed };

const char* KindName(Kind k); ///< "Added" / "Removed" / "Changed"

/// A change region in the BASE page's points (origin lower-left, y up).
struct Region {
  Kind kind = Kind::Added;
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  long addedPx = 0, removedPx = 0; ///< differing pixels of each kind inside it
  double Width() const { return x1 - x0; }
  double Height() const { return y1 - y0; }
};

struct Result {
  std::vector<Region> regions;
  bool cancelled = false;
};

/// Compare \p base with \p rev, both the same size and drawn at \p pxPerPt, \p rev already laid onto the base's
/// grid. A revision mark with no base mark within the tolerance is Added; a base mark with no revision mark within
/// it is Removed; added and removed differences within the merge distance of each other make one Changed region.
/// \p cancel is polled and \p progress (0..1) reported between stages; both may be empty.
Result FindChanges(const pdfview::Bitmap& base, const pdfview::Bitmap& rev, double pxPerPt, double baseHPt, const Settings& s,
                   const std::function<bool()>& cancel = {}, const std::function<void(float)>& progress = {});

/// REQ-393 clause 4: one rectangle annotation per region, on revision page \p revPage, in the revision's own
/// points (the regions are carried through \p baseToRev). Colour by kind, contents "Added, 12.3 x 4.5 pt".
std::vector<pdfview::Annot> RegionsToMarkups(const std::vector<Region>& regions, int revPage, const pdfalign::Transform& baseToRev);

} // namespace pdfdiff
