#pragma once

#include <imgui.h>

/// ADR-033 chrome palette. One instance, written by ApplyCad*Theme and (Debug) the
/// Developer Shell tuner (REQ-161 / ADR-040). Every theme entry must fill every field.
struct UiChrome {
  ImU32 bandFace;
  ImU32 bandHilite;
  ImU32 bandShadow;
  ImU32 bandSunken;
  ImU32 bandRaised;
  ImU32 statusBarFace;
  ImU32 statusStripFace;
  ImU32 panelFill;
  ImU32 propValueBg;
  ImU32 headerFaceL;
  ImU32 headerFaceR;
  ImU32 headerHoverL;
  ImU32 headerHoverR;
  ImU32 headerText;
  ImU32 headerEdgeTop;
  ImU32 headerEdgeBot;
  ImU32 headerGlyphBg;
  ImU32 headerGlyphEdge;
  ImU32 headerGlyph;
  bool  headerBoxGlyph;
  ImU32 popupFace;
  ImU32 popupBorder;
  ImU32 plateHilite;
  ImU32 plateShadow;
  ImU32 windowShadow;
  bool  axisBadges;
  ImU32 axisX, axisY, axisZ;
  ImU32 axisText;

  /// Ribbon panel title rule (hairline above "Draw" / "Modify" …).
  ImU32 ribbonPanelRule;
  ImU32 ribbonPanelTitle;
  /// Active Home/Insert/… tab (and other mode-toggle buttons).
  ImU32 ribbonTabOn;
  ImU32 ribbonTabOnHovered;
  ImU32 ribbonTabOnActive;
  ImU32 ribbonTabOnText;
  /// Contextual ribbon tabs (Tin Surface / Survey Point / Feature Line).
  ImU32 ribbonCtxTab;
  ImU32 ribbonCtxTabDim;
  ImU32 ribbonCtxTabHovered;
  ImU32 ribbonCtxTabActive;
  ImU32 ribbonCtxTabText;
  float ribbonTabPadY;
  float ribbonTabStripGapY;
  float ribbonBottomGutter;
  float ribbonTitleH;
  float ribbonBodyFontScale;
};

UiChrome& CadUiChrome();
