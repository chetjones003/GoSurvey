#pragma once

#include <algorithm>
#include <string>

#include "CadEntities.hpp"

/// Active multileader style for new MLEADER callouts (issue #619). Single style object like
/// `DimensionStyle` phase 1 — DWG export still writes LibreDWG's Standard MLEADERSTYLE table entry.
struct MultileaderStyle {
  std::string name = "Standard";
  float textSizeInches = 0.10f;
  std::string textFont;  ///< empty = app / active text style default
  float arrowSizeInches = 0.10f;
  float landingGapInches = 0.05f;

  bool operator==(const MultileaderStyle& o) const {
    return name == o.name && textSizeInches == o.textSizeInches && textFont == o.textFont &&
           arrowSizeInches == o.arrowSizeInches && landingGapInches == o.landingGapInches;
  }
  bool operator!=(const MultileaderStyle& o) const { return !(*this == o); }
};

namespace MultileaderStyles {

inline MultileaderStyle Default() {
  MultileaderStyle s;
  s.name = "Standard";
  return s;
}

inline void BakeOntoMultileaderLabel(CadAnnotation& label, const MultileaderStyle& sty) {
  if (label.kind != CadAnnotation::Kind::Mtext)
    return;
  label.plottedHeightInches = std::max(sty.textSizeInches, 0.01f);
  if (!sty.textFont.empty())
    label.fontFamily = sty.textFont;
}

}  // namespace MultileaderStyles
