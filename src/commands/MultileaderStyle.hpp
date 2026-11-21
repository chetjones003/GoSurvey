#pragma once

#include <GoSurvey/Document/MultileaderStyle.hpp>
#include "CadEntities.hpp"

/// Active multileader style for new MLEADER callouts (issue #619). Single style object like
/// `DimensionStyle` phase 1 — DWG export still writes LibreDWG's Standard MLEADERSTYLE table entry.
namespace MultileaderStyles {

inline void BakeOntoMultileaderLabel(CadAnnotation& label, const MultileaderStyle& sty) {
  if (label.kind != CadAnnotation::Kind::Mtext)
    return;
  label.plottedHeightInches = std::max(sty.textSizeInches, 0.01f);
  if (!sty.textFont.empty())
    label.fontFamily = sty.textFont;
}

}  // namespace MultileaderStyles
