inline void RestoreMtextGripOriginal(AppCommandState& st) {
  if (!st.mtextGripMoveActive)
    return;
  const int aix = st.mtextGripAnnotationIndex;
  if (aix < 0 || static_cast<size_t>(aix) >= st.drawing.cadAnnotations.size())
    return;
  CadAnnotation& ann = st.drawing.cadAnnotations[static_cast<size_t>(aix)];
  if (ann.kind != CadAnnotation::Kind::Mtext)
    return;
  ann.boxMinX = st.mtextGripOrigBoxMinX;
  ann.boxMaxX = st.mtextGripOrigBoxMaxX;
  ann.boxMinY = st.mtextGripOrigBoxMinY;
  ann.boxMaxY = st.mtextGripOrigBoxMaxY;
  if (ann.surveyPointLabelForId < 0) {
    CadMtextSyncInsertion(ann);  // issue #1667: the attachment point of the restored frame
  } else {
    ann.insX = ann.boxMinX;
    ann.insY = ann.boxMinY;
  }
}

inline void ClearMtextGripInteraction(AppCommandState& st) {
  st.mtextGripMoveActive = false;
  st.mtextGripAnnotationIndex = -1;
  st.mtextGripCorner = -1;
  st.mtextGripDownWorldX = 0.f;
  st.mtextGripDownWorldY = 0.f;
}

/// Cancel in-progress MTEXT grip edit and restore the box (selection change, new command, fence, etc.).
inline void AbortMtextGripInteraction(AppCommandState& st) {
  RestoreMtextGripOriginal(st);
  ClearMtextGripInteraction(st);
}

inline void CloseMtextRichEditorUi(AppCommandState& st) {
  st.mtextRichEditorOpen = false;
  st.mtextRichEditorPlacement = false;
  st.mtextRichEditorPaper = false;
  st.mtextRichEditorPaperLayout = -1;
  st.mtextRichEditorPlain = false;
  st.mtextRichEditorAnnIndex = -1;
  st.mtextRichEditorMarkerIndex = -1;  // REQ-359
  st.mtextRichEditorMarkerJustPlaced = false;
  st.mtextRichEditorMultileaderIndex = -1;  // REQ-367
  st.mtextRichEditorMultileaderJustPlaced = false;
  st.mtextRichEditorBuf.clear();
  st.mtextRichEditorFocusRequest = false;
  st.mtextRichEditorCursor = 0;
  st.mtextRichEditorSelStart = 0;
  st.mtextRichEditorSelEnd = 0;
  st.mtextRichEditorTypingAllCaps = false;
  st.mtextEditCaret = 0;
  st.mtextEditAnchor = 0;
  st.mtextEditFocused = false;
  st.mtextEditMouseSelecting = false;
  st.mtextEditScrollY = 0.f;
  st.mtextEditUndo.clear();
  st.mtextEditRedo.clear();
}

/// The annotation the in-place text editor is currently editing (REQ-039 phase 2): a model
/// \c cadAnnotations entry or, when \c mtextRichEditorPaper, the active paper layout's \c paperTexts
/// entry. Returns \c nullptr for placement mode (no existing object yet) or an out-of-range index.
inline CadAnnotation* MtextRichEditorTargetAnnotation(AppCommandState& st) {
  if (!st.mtextRichEditorOpen || st.mtextRichEditorPlacement)
    return nullptr;
  if (st.mtextRichEditorMarkerIndex >= 0) {  // a Position Marker's own label (REQ-359 item 3)
    const size_t mi = static_cast<size_t>(st.mtextRichEditorMarkerIndex);
    return mi < st.drawing.cadPositionMarkers.size() ? &st.drawing.cadPositionMarkers[mi].label : nullptr;
  }
  if (st.mtextRichEditorMultileaderIndex >= 0) {  // REQ-367
    const size_t li = static_cast<size_t>(st.mtextRichEditorMultileaderIndex);
    return li < st.drawing.cadMultileaders.size() ? &st.drawing.cadMultileaders[li].label : nullptr;
  }
  const int ix = st.mtextRichEditorAnnIndex;
  if (ix < 0)
    return nullptr;
  if (st.mtextRichEditorPaper) {
    if (st.mtextRichEditorPaperLayout < 0 ||
        static_cast<size_t>(st.mtextRichEditorPaperLayout) >= st.drawing.paperLayouts.size())
      return nullptr;
    PaperLayout& L = st.drawing.paperLayouts[static_cast<size_t>(st.mtextRichEditorPaperLayout)];
    if (static_cast<size_t>(ix) >= L.paperTexts.size())
      return nullptr;
    return &L.paperTexts[static_cast<size_t>(ix)];
  }
  if (static_cast<size_t>(ix) >= st.drawing.cadAnnotations.size())
    return nullptr;
  return &st.drawing.cadAnnotations[static_cast<size_t>(ix)];
}

/// The attributes parallel to \ref MtextRichEditorTargetAnnotation — the entity colour/layer/linetype row
/// of the text being edited (REQ-051's colour control writes it). Same nullptr cases, plus a parallel-vector
/// length mismatch, which is never expected but must not index out of range.
inline EntityAttributes* MtextRichEditorTargetAttrs(AppCommandState& st) {
  if (!st.mtextRichEditorOpen || st.mtextRichEditorPlacement)
    return nullptr;
  if (st.mtextRichEditorMarkerIndex >= 0) {  // REQ-359: the marker's row
    const size_t mi = static_cast<size_t>(st.mtextRichEditorMarkerIndex);
    return mi < st.drawing.cadPositionMarkerAttrs.size() ? &st.drawing.cadPositionMarkerAttrs[mi] : nullptr;
  }
  if (st.mtextRichEditorMultileaderIndex >= 0) {  // REQ-367
    const size_t li = static_cast<size_t>(st.mtextRichEditorMultileaderIndex);
    return li < st.drawing.cadMultileaderAttrs.size() ? &st.drawing.cadMultileaderAttrs[li] : nullptr;
  }
  const int ix = st.mtextRichEditorAnnIndex;
  if (ix < 0)
    return nullptr;
  if (st.mtextRichEditorPaper) {
    if (st.mtextRichEditorPaperLayout < 0 ||
        static_cast<size_t>(st.mtextRichEditorPaperLayout) >= st.drawing.paperLayouts.size())
      return nullptr;
    PaperLayout& L = st.drawing.paperLayouts[static_cast<size_t>(st.mtextRichEditorPaperLayout)];
    if (static_cast<size_t>(ix) >= L.paperTextAttrs.size())
      return nullptr;
    return &L.paperTextAttrs[static_cast<size_t>(ix)];
  }
  if (static_cast<size_t>(ix) >= st.drawing.cadAnnotationAttrs.size())
    return nullptr;
  return &st.drawing.cadAnnotationAttrs[static_cast<size_t>(ix)];
}


inline void ClearDimGripInteraction(AppCommandState& st) {
  st.dimGripAnnotationIndex = -1;
  st.dimGripWhich = -1;
  st.dimGripMoveActive = false;
  st.dimGripTextAlongN = 0.f;
  st.dimGripTextAlongT = 0.f;
}
