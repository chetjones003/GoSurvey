#pragma once

// REQ-392 — lining two revisions of a sheet up, and telling what is on one, the other, or both. Pure: no
// PDFium, no ImGui, no OpenGL, so every rule is unit-testable. Points are PDF points from a page's
// bottom-left corner, as the viewer reports them.

#include "PdfDocument.hpp" // Bitmap only

#include <functional>
#include <string>

namespace pdfalign {

struct Pt {
  double x = 0, y = 0;
};

/// A shift, a uniform scale and a rotation, carrying a revision page's point onto the base page:
///   x' = a x - b y + tx,   y' = b x + a y + ty     (a = s cos t, b = s sin t)
struct Transform {
  double a = 1, b = 0, tx = 0, ty = 0;
  Pt Apply(Pt p) const { return {a * p.x - b * p.y + tx, b * p.x + a * p.y + ty}; }
  double Scale() const;
  double RotationDeg() const;
  /// The transform that undoes this one (the scale must not be zero).
  Transform Inverse() const;
};

/// REQ-392 clause 4, one matching point: shift only, so \p rev lands exactly on \p base.
Transform FromOnePoint(Pt rev, Pt base);

/// REQ-392 clause 4, two matching points: shift, scale and rotation, so rev1 lands on base1 and rev2 on
/// base2. Returns false with a stated reason when the two points on a sheet are the same point.
bool FromTwoPoints(Pt rev1, Pt rev2, Pt base1, Pt base2, Transform& out, std::string& why);

/// A pixel counts as drawn ink when it is clearly darker than the white page.
bool IsInk(const uint8_t* bgra);

/// REQ-392 clause 2 (Tint): what a pixel is, given whether each sheet has ink there.
enum class Ink { None, BaseOnly, RevOnly, Both };
Ink Classify(bool baseInk, bool revInk);

/// The Tint colour of a class as BGRA: base only red, revision only blue, both dark grey, neither white.
void TintBgra(Ink k, uint8_t out[4]);

/// Draw the revision into the base's pixel grid: \p out is outW x outH at \p basePxPerPt for a base page
/// \p baseHPt tall, each pixel taken from \p rev (rendered at \p revPxPerPt, page \p revHPt tall) through
/// \p revToBase. Where the revision has no page the pixel is white.
void ResampleAligned(const pdfview::Bitmap& rev, float revHPt, float revPxPerPt, const Transform& revToBase, int outW, int outH,
                     float basePxPerPt, float baseHPt, pdfview::Bitmap& out);

/// The Base / Revision views (REQ-392 clause 2): an overlay the size of \p sheet that is \p bgr (blue, green, red) where
/// \p sheet has ink with nothing darker than a faint grey on \p other within two pixels, and transparent elsewhere. Cleaned up by whole marks: a small mark (a letter, a dash, a dot) is coloured whole when about a third or more of it is new and left plain when only a sliver is; in a big connected mark only the new pixels are coloured and clusters under about 6 square points are dropped. \p pxPerPt is the images' pixels per point.
void MarkOnlyIn(const pdfview::Bitmap& sheet, const pdfview::Bitmap& other, const uint8_t bgr[3], pdfview::Bitmap& out, double pxPerPt = 1.0);

/// Per-pixel Tint of two same-sized bitmaps (REQ-392 clause 2).
void TintImage(const pdfview::Bitmap& base, const pdfview::Bitmap& rev, pdfview::Bitmap& out);

/// REQ-393 (and REQ-392 clause 3): find the shift, uniform scale and small rotation (up to 5 degrees) that lay the
/// revision's line work onto the base's. Both bitmaps are drawn at \p pxPerPt (they may differ in size: sheets of
/// different paper size are allowed); \p baseHPt / \p revHPt are the pages' heights in points. `confidence` is the
/// share of the revision's ink that lies within about 2 pt of base ink after alignment; under kLowConfidence the
/// result is not claimed (`matched` false) and the transform is the identity.
constexpr double kLowConfidence = 0.6;
struct AutoResult {
  Transform xf;
  double confidence = 0.0;
  bool matched = false;
};
AutoResult AutoAlign(const pdfview::Bitmap& base, double baseHPt, const pdfview::Bitmap& rev, double revHPt, double pxPerPt,
                     const std::function<bool()>& cancel = {});

} // namespace pdfalign
