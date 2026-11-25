  // --- Deferred survey-duplicate modal state (survives after COPY/ROTATE/MIRROR session ends) ---
  /// COPY modal: when true, duplicate survey selection by pending rotation instead of translation.
  bool pendingSurveyDupIsRotate = false;
  float pendingRotateCopyBx = 0.f, pendingRotateCopyBy = 0.f, pendingRotateCopyRad = 0.f;
  /// COPY modal: when true, duplicate survey selection by pending reflection instead of
  /// translation/rotation. Checked ahead of \ref pendingSurveyDupIsRotate.
  bool pendingSurveyDupIsMirror = false;
  float pendingMirrorX0 = 0.f, pendingMirrorY0 = 0.f, pendingMirrorX1 = 0.f, pendingMirrorY1 = 0.f;
  /// MIRROR erase-source, survey half: whether original survey points must also be removed once
  /// the duplicate-policy modal resolves.
  bool mirrorEraseSourcePending = false;

  // --- LENGTHEN persisted session settings (REQ-103 step 2) ---
  enum class LengthenMode : std::uint8_t { Delta, Percent, Total, Dynamic } lengthenMode = LengthenMode::Total;
  float lengthenDeltaValue = 0.f;
  float lengthenPercentValue = 100.f;
  float lengthenTotalValue = 0.f;
  bool lengthenModeValueSet = false;

  // --- FILLET / CHAMFER persisted app-level settings (REQ-103 step 6) ---
  float filletRadius = 0.5f;
  int cornerTrimMode = 1;
  float chamferDist1 = 0.5f;
  float chamferDist2 = 0.5f;
  float chamferAngle = 45.f;
  int chamferMode = 0;
