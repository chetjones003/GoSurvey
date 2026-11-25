#include "CadPropertyEdit.hpp"

#include "CadColor.hpp"
#include "CadDimStroke.hpp"
#include "CadLinetype.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace propedit {
namespace {

Outcome Rejected(const char* why) { return {Result::Rejected, why}; }
Outcome NotFinite() { return Rejected("Enter a finite number."); }

/// Stores \p next into \p slot as ONE undo step, taken before the write, only when it differs.
template <class T>
Outcome Store(AppCommandState& st, T& slot, T next, const char* undoLabel, std::string note = {}) {
  if (slot == next)
    return {Result::Unchanged, std::move(note)};
  PushUndoSnapshot(st, undoLabel);
  slot = next;
  BumpCadGpuCache(st);
  return {Result::Applied, std::move(note)};
}

}  // namespace

Outcome SetLineField(AppCommandState& st, int lineIndex, LineField field, double value) {
  if (!std::isfinite(value))
    return NotFinite();
  if (lineIndex < 0 || static_cast<size_t>(lineIndex) >= st.drawing.lineCount())
    return Rejected("That line no longer exists.");
  using F = LineField;
  using C = CadLineComponent;
  const C slot = field == F::StartX ? C::X0 : field == F::StartY ? C::Y0 : field == F::StartZ ? C::Z0
               : field == F::EndX ? C::X1 : field == F::EndY ? C::Y1 : C::Z1;
  // X/Y are typed in WORLD coordinates and stored LOCAL (REQ-101); Z is absolute (ADR-025 D2).
  const bool isX = slot == C::X0 || slot == C::X1;
  const bool isY = slot == C::Y0 || slot == C::Y1;
  const double origin = isX ? st.drawing.worldDocumentOriginX : isY ? st.drawing.worldDocumentOriginY : 0.0;
  const char* label = isX ? "Edit line X" : isY ? "Edit line Y" : "Edit line Z";
  return Store(st, st.drawing.lineComponent(static_cast<size_t>(lineIndex), slot), value - origin, label);
}

Outcome SetCircleField(AppCommandState& st, int circleIndex, CircleField field, double value) {
  if (!std::isfinite(value))
    return NotFinite();
  if (circleIndex < 0 || static_cast<size_t>(circleIndex) >= st.drawing.circleCount())
    return Rejected("That circle no longer exists.");
  const size_t k = static_cast<size_t>(circleIndex);
  using F = CircleField;
  switch (field) {
  case F::CenterX:
    return Store(st, st.drawing.circleComponent(k, CadCircleComponent::X), value - st.drawing.worldDocumentOriginX, "Edit circle X");
  case F::CenterY:
    return Store(st, st.drawing.circleComponent(k, CadCircleComponent::Y), value - st.drawing.worldDocumentOriginY, "Edit circle Y");
  case F::CenterZ:
    return Store(st, st.drawing.circleComponent(k, CadCircleComponent::Z), value, "Edit circle Z");
  case F::Radius:
    break;
  }
  // The 0.7.4 panel floors a typed radius rather than refusing it; keep the outcome but say so.
  if (value >= kMinCircleRadius)
    return Store(st, st.drawing.circleComponent(k, CadCircleComponent::Radius), value, "Edit circle radius");
  return Store(st, st.drawing.circleComponent(k, CadCircleComponent::Radius), kMinCircleRadius, "Edit circle radius",
               "A radius must be positive; it was set to the minimum, 0.000001.");
}

Outcome SetTextNumber(AppCommandState& st, int annotationIndex, TextField field, double value) {
  if (!std::isfinite(value))
    return NotFinite();
  if (annotationIndex < 0 || static_cast<size_t>(annotationIndex) >= st.drawing.cadAnnotations.size())
    return Rejected("That text no longer exists.");
  CadAnnotation& ann = st.drawing.cadAnnotations[static_cast<size_t>(annotationIndex)];
  const bool isText = ann.kind == CadAnnotation::Kind::Text;
  const bool isMtext = ann.kind == CadAnnotation::Kind::Mtext;
  using F = TextField;
  switch (field) {
  case F::InsertionX: {
    const float next = static_cast<float>(value - st.drawing.worldDocumentOriginX);
    double curX = ann.insX, curY = ann.insY;
    const bool isDim = CadAnnotationIsDimension(ann);
    if (isDim && !ann.dimTextUserPlaced)
      CadDimModelSpaceLabelPosition(st, ann, &curX, &curY);
    if (next == static_cast<float>(curX))
      return {};
    PushUndoSnapshot(st, "Edit text X");
    if (isMtext) {  // the box travels with its insertion point
      ann.boxMinX += next - ann.insX;
      ann.boxMaxX += next - ann.insX;
    }
    if (isDim && !ann.dimTextUserPlaced) {  // issue #1676: lock Y to its drawn position before moving X
      ann.insY = static_cast<float>(curY);
      ann.dimTextUserPlaced = true;
    }
    ann.insX = next;
    BumpCadGpuCache(st);
    return {Result::Applied, {}};
  }
  case F::InsertionY: {
    const float next = static_cast<float>(value - st.drawing.worldDocumentOriginY);
    double curX = ann.insX, curY = ann.insY;
    const bool isDim = CadAnnotationIsDimension(ann);
    if (isDim && !ann.dimTextUserPlaced)
      CadDimModelSpaceLabelPosition(st, ann, &curX, &curY);
    if (next == static_cast<float>(curY))
      return {};
    PushUndoSnapshot(st, "Edit text Y");
    if (isMtext) {
      ann.boxMinY += next - ann.insY;
      ann.boxMaxY += next - ann.insY;
    }
    if (isDim && !ann.dimTextUserPlaced) {  // issue #1676: lock X to its drawn position before moving Y
      ann.insX = static_cast<float>(curX);
      ann.dimTextUserPlaced = true;
    }
    ann.insY = next;
    BumpCadGpuCache(st);
    return {Result::Applied, {}};
  }
  case F::InsertionZ:
    return Store(st, ann.insZ, static_cast<float>(value), "Edit text Z");
  case F::PlottedHeightInches:
    if (value > 0.0)
      return Store(st, ann.plottedHeightInches, static_cast<float>(value), "Edit text height");
    return Store(st, ann.plottedHeightInches, kDefaultPlottedTextHeightInches, "Edit text height",
                 "A text height must be positive; it was reset to 0.0625 in.");
  case F::RotationBearingDeg:
    if (!isText && !isMtext)
      return Rejected("Only text has a rotation.");
    return Store(st, ann.rotationRad, MathAngleRadFromBearingCwNorthDeg(static_cast<float>(value)),
                 "Edit text rotation");
  case F::BoxMinX: case F::BoxMinY: case F::BoxMaxX: case F::BoxMaxY: {
    if (!isMtext)
      return Rejected("Only multiline text has a box.");
    float* edge = field == F::BoxMinX ? &ann.boxMinX : field == F::BoxMinY ? &ann.boxMinY
                : field == F::BoxMaxX ? &ann.boxMaxX : &ann.boxMaxY;
    const float next = static_cast<float>(value);
    if (*edge == next)
      return {};
    PushUndoSnapshot(st, "Edit text box");
    *edge = next;
    // Keep min <= max and the insertion point on the box's corner, as the 0.7.4 panel does.
    if (ann.boxMinX > ann.boxMaxX) std::swap(ann.boxMinX, ann.boxMaxX);
    if (ann.boxMinY > ann.boxMaxY) std::swap(ann.boxMinY, ann.boxMaxY);
    ann.insX = ann.boxMinX;
    ann.insY = ann.boxMinY;
    BumpCadGpuCache(st);
    return {Result::Applied, {}};
  }
  case F::DimTextHeightModel:
  case F::DimArrowSizeModel: {
    if (ann.kind != CadAnnotation::Kind::DimAligned && ann.kind != CadAnnotation::Kind::DimLinear &&
        ann.kind != CadAnnotation::Kind::DimAngular)
      return Rejected("Only dimensions have text and arrow size overrides.");
    const bool text = field == F::DimTextHeightModel;
    double& slot = text ? ann.dimTextHeightModel : ann.dimArrowSizeModel;
    const char* label = text ? "Edit dimension text height" : "Edit dimension arrow size";
    if (value <= 0.0)  // blank, zero or negative: follow the style again
      return Store(st, slot, 0.0, label);
    return Store(st, slot, value, label);
  }
  case F::Content:
    break;
  }
  return Rejected("Use SetTextContent for the text contents.");
}

Outcome SetTextContent(AppCommandState& st, int annotationIndex, const std::string& text) {
  if (annotationIndex < 0 || static_cast<size_t>(annotationIndex) >= st.drawing.cadAnnotations.size())
    return Rejected("That text no longer exists.");
  CadAnnotation& ann = st.drawing.cadAnnotations[static_cast<size_t>(annotationIndex)];
  if (ann.kind == CadAnnotation::Kind::Table)
    return Rejected("A table's text is edited cell by cell.");
  return Store(st, ann.text, text, "Edit text");
}

DimDerivedSizes ReadDimensionSizes(const AppCommandState& st, const CadAnnotation& ann) {
  DimDerivedSizes out;
  const DimensionStyle& dimStyle = CadDimensionStyleForAnnotation(st, ann);
  const float dimMup = CadDimModelUnitsPerPlottedInch(
      ann, &dimStyle, st.drawing.modelUnitsPerPlottedInch, &st.drawing.annotationScales,
      st.currentAnnotationScaleIndex, nullptr, true);
  out.textHeightModel = CadDimTextHeightWorld(ann, &dimStyle, dimMup);
  out.arrowSizeModel = (std::isfinite(ann.dimArrowSizeModel) && ann.dimArrowSizeModel > 0.0)
      ? ann.dimArrowSizeModel
      : static_cast<double>(dimStyle.arrowSizeInches) * dimMup * st.viewportDimArrowScale;
  CadDimModelSpaceLabelPosition(st, ann, &out.labelX, &out.labelY);
  return out;
}

GeneralAttrs ReadGeneralAttrs(const AppCommandState& st) {
  GeneralAttrs g;
  std::string linetypeCanon;
  for (const SelectedEntity& e : st.selection) {
    const EntityAttributes* a = CadEditableAttrsForSelected(st, e);
    if (!a)
      continue;
    const std::string color = a->color.empty() ? std::string("ByLayer") : a->color;
    const std::string canon = CadCanonicalLinetypeNameForDxf(a->linetype);
    if (g.count++ == 0) {
      g.layer = a->layer;
      g.color = color;
      g.linetype = a->linetype;
      linetypeCanon = canon;
      g.lineweightMm = a->lineweightMm;
      g.transparency = a->transparency;
      continue;
    }
    g.layerMixed = g.layerMixed || a->layer != g.layer;
    g.colorMixed = g.colorMixed || !CadColorStorageMatches(g.color, color);
    g.linetypeMixed = g.linetypeMixed || canon != linetypeCanon;
    g.lineweightMixed = g.lineweightMixed || std::fabs(a->lineweightMm - g.lineweightMm) > 1e-5f;
    g.transparencyMixed = g.transparencyMixed || std::fabs(a->transparency - g.transparency) > 1e-5f;
  }
  return g;
}

}  // namespace propedit
