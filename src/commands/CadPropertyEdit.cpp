#include "CadPropertyEdit.hpp"

#include "CadColor.hpp"
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
  const size_t k = static_cast<size_t>(lineIndex) * 6;
  if (lineIndex < 0 || k + 5 >= st.userLinesFlat.size())
    return Rejected("That line no longer exists.");
  using F = LineField;
  const size_t slot = field == F::StartX ? 0 : field == F::StartY ? 1 : field == F::StartZ ? 2
                    : field == F::EndX ? 3 : field == F::EndY ? 4 : 5;
  // X/Y are typed in WORLD coordinates and stored LOCAL (REQ-101); Z is absolute (ADR-025 D2).
  const double origin = (slot % 3 == 0) ? st.worldDocumentOriginX : (slot % 3 == 1) ? st.worldDocumentOriginY : 0.0;
  const char* label = (slot % 3 == 0) ? "Edit line X" : (slot % 3 == 1) ? "Edit line Y" : "Edit line Z";
  return Store(st, st.userLinesFlat[k + slot], value - origin, label);
}

Outcome SetCircleField(AppCommandState& st, int circleIndex, CircleField field, double value) {
  if (!std::isfinite(value))
    return NotFinite();
  const size_t k = static_cast<size_t>(circleIndex) * 4;
  if (circleIndex < 0 || k + 3 >= st.userCirclesCxCyZR.size())
    return Rejected("That circle no longer exists.");
  using F = CircleField;
  switch (field) {
  case F::CenterX:
    return Store(st, st.userCirclesCxCyZR[k], value - st.worldDocumentOriginX, "Edit circle X");
  case F::CenterY:
    return Store(st, st.userCirclesCxCyZR[k + 1], value - st.worldDocumentOriginY, "Edit circle Y");
  case F::CenterZ:
    return Store(st, st.userCirclesCxCyZR[k + 2], value, "Edit circle Z");
  case F::Radius:
    break;
  }
  // The 0.7.4 panel floors a typed radius rather than refusing it; keep the outcome but say so.
  if (value >= kMinCircleRadius)
    return Store(st, st.userCirclesCxCyZR[k + 3], value, "Edit circle radius");
  return Store(st, st.userCirclesCxCyZR[k + 3], kMinCircleRadius, "Edit circle radius",
               "A radius must be positive; it was set to the minimum, 0.000001.");
}

Outcome SetTextNumber(AppCommandState& st, int annotationIndex, TextField field, double value) {
  if (!std::isfinite(value))
    return NotFinite();
  if (annotationIndex < 0 || static_cast<size_t>(annotationIndex) >= st.cadAnnotations.size())
    return Rejected("That text no longer exists.");
  CadAnnotation& ann = st.cadAnnotations[static_cast<size_t>(annotationIndex)];
  const bool isText = ann.kind == CadAnnotation::Kind::Text;
  const bool isMtext = ann.kind == CadAnnotation::Kind::Mtext;
  using F = TextField;
  switch (field) {
  case F::InsertionX: {
    const float next = static_cast<float>(value - st.worldDocumentOriginX);
    if (next == ann.insX)
      return {};
    PushUndoSnapshot(st, "Edit text X");
    if (isMtext) {  // the box travels with its insertion point
      ann.boxMinX += next - ann.insX;
      ann.boxMaxX += next - ann.insX;
    }
    ann.insX = next;
    BumpCadGpuCache(st);
    return {Result::Applied, {}};
  }
  case F::InsertionY: {
    const float next = static_cast<float>(value - st.worldDocumentOriginY);
    if (next == ann.insY)
      return {};
    PushUndoSnapshot(st, "Edit text Y");
    if (isMtext) {
      ann.boxMinY += next - ann.insY;
      ann.boxMaxY += next - ann.insY;
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
    if (!isText)
      return Rejected("Only single-line text has a rotation.");
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
  case F::Content:
    break;
  }
  return Rejected("Use SetTextContent for the text contents.");
}

Outcome SetTextContent(AppCommandState& st, int annotationIndex, const std::string& text) {
  if (annotationIndex < 0 || static_cast<size_t>(annotationIndex) >= st.cadAnnotations.size())
    return Rejected("That text no longer exists.");
  CadAnnotation& ann = st.cadAnnotations[static_cast<size_t>(annotationIndex)];
  if (ann.kind == CadAnnotation::Kind::Table)
    return Rejected("A table's text is edited cell by cell.");
  return Store(st, ann.text, text, "Edit text");
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
