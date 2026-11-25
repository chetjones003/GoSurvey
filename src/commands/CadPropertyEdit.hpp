#pragma once
// Property-panel edits as services (issue #891, REQ-024 / REQ-057 / REQ-101).
//
// The Properties panel edits LINE, CIRCLE and TEXT/MTEXT geometry inside its own draw
// code, one widget at a time. A second frontend cannot reuse widgets, so the rules those widgets
// enforce live here instead: what a field means, which value is stored, what is refused, and what
// one undo step covers.
//
// Values passed in are what the user typed — WORLD X/Y (storage is local; world = local +
// worldDocumentOrigin, REQ-101), absolute Z, and a clockwise-from-north bearing for text rotation.
// Nothing here knows about widgets.

#include "CadCommands.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace propedit {

enum class Result : std::uint8_t {
  Applied,    ///< The stored value changed; one undo step was pushed.
  Unchanged,  ///< The value already matched; nothing was pushed (no empty undo steps).
  Rejected,   ///< Refused; the drawing is untouched. \c message says why.
};

struct Outcome {
  Result result = Result::Unchanged;
  /// Rejection reason, or the note about a value that was corrected (e.g. a radius raised to the
  /// minimum) — always shown to the user, never swallowed (REQ-201). Empty when nothing to say.
  std::string message;
};

enum class LineField : std::uint8_t { StartX, StartY, StartZ, EndX, EndY, EndZ };
enum class CircleField : std::uint8_t { CenterX, CenterY, CenterZ, Radius };
enum class TextField : std::uint8_t {
  InsertionX, InsertionY, InsertionZ, PlottedHeightInches,
  RotationBearingDeg,  ///< TEXT and MTEXT; MTEXT turns about its insertion point.
  /// MTEXT only: the frame (wrap) width along the text, in model units as drawn (an annotative MTEXT at
  /// the current annotation scale). The insertion point stays put; the height follows the content.
  FrameWidth,
  Content,
  /// Dimensions only (issue #1676): one-off sizes in MODEL units; 0 or less clears back to the style.
  DimTextHeightModel, DimArrowSizeModel,
};

/// The smallest radius a circle keeps — the 0.7.4 panel floors a typed radius at this value.
inline constexpr double kMinCircleRadius = 1e-6;
/// What a non-positive plotted text height becomes — the 0.7.4 panel's reset value.
inline constexpr float kDefaultPlottedTextHeightInches = 0.0625f;

[[nodiscard]] Outcome SetLineField(AppCommandState& st, int lineIndex, LineField field, double value);
[[nodiscard]] Outcome SetCircleField(AppCommandState& st, int circleIndex, CircleField field, double value);
[[nodiscard]] Outcome SetTextNumber(AppCommandState& st, int annotationIndex, TextField field, double value);
[[nodiscard]] Outcome SetTextContent(AppCommandState& st, int annotationIndex, const std::string& text);

/// Live model-space readout for a dimension in Properties (issue #1676): drawn text height,
/// arrow length, and label anchor (auto-placed until dragged or edited).
struct DimDerivedSizes {
  double textHeightModel = 0.0;
  double arrowSizeModel = 0.0;
  double labelX = 0.0;
  double labelY = 0.0;
};
[[nodiscard]] DimDerivedSizes ReadDimensionSizes(const AppCommandState& st, const CadAnnotation& ann);

/// The General-section values of the current selection. \c *Mixed means the selected objects
/// disagree (the field then shows "varies" and an edit applies to all of them).
struct GeneralAttrs {
  int count = 0;  ///< Selected objects that carry attributes.
  std::string layer, color, linetype;
  float lineweightMm = -1.f, transparency = -1.f;
  bool layerMixed = false, colorMixed = false, linetypeMixed = false;
  bool lineweightMixed = false, transparencyMixed = false;
};
[[nodiscard]] GeneralAttrs ReadGeneralAttrs(const AppCommandState& st);

}  // namespace propedit
