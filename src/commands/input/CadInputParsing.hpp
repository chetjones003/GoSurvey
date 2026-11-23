inline void ResetSegmentAngleLock(AppCommandState& st) {
  if (auto* ang = stepengine::ActiveSegmentAngleState(st))
    ang->reset();
}


/// Abort bearing-from-two-points flow (\p AP) without ending LINE/POLYLINE.
void CancelSegmentAnglePick(AppCommandState& st, std::vector<std::string>* log);

/// LINE / POLYLINE (next point): \p A / \p AP / optional \p +delta on same line (° clockwise from north).
bool TryParseSegmentAngleLockCommand(AppCommandState& st, const std::string& lineIn, std::vector<std::string>& log);

/// Trim and parse absolute "x,y" / "x y" or relative "@dx,dy" when allowed.
///
/// **Interpreted in the active UCS** (REQ-154): under a rotated UCS `10,0` is 10 units along the UCS
/// X axis, not the world's. Under the WCS — the default, and every drawing that predates the UCS
/// command — this is the original world-frame parse, unchanged.
///
/// A third number is the point's Z (REQ-354): `x,y,z`, or `@dx,dy,dz` measured from \p baseWorldZ
/// (the work plane when null). It is published through AppCommandState::resolvedPointZ.
bool ParseStoragePoint(AppCommandState& st, const std::string& raw, float* lx, float* ly, bool allowRelative,
                       float baseLocalX, float baseLocalY, const float* baseWorldZ = nullptr);

/// \ref ParseStoragePoint, additionally reporting the resolved point's world Z. Callers that are
/// about to commit geometry want this: on a tilted UCS the work plane's elevation varies across it,
/// so the point's own Z is the only correct answer (see AppCommandState::resolvedPointZ).
bool ParseStoragePointZ(AppCommandState& st, const std::string& raw, float* lx, float* ly, double* outWorldZ,
                        bool allowRelative, float baseLocalX, float baseLocalY, const float* baseWorldZ = nullptr);

/// Split a typed point into its two numbers and whether it carried a leading `@`, without deciding
/// which frame those numbers are in. That separation is what lets one parser serve both frames.
bool ParsePointComponents(const std::string& raw, double* a, double* b, bool* isRelative, bool allowRelative);

bool ParseWorldPoint(const std::string& raw, float* ox, float* oy, bool allowRelative, float baseX, float baseY);

/// Double-precision form, and the real implementation — the float overload above narrows its result.
/// Prefer this wherever the value is destined for the local storage frame: narrowing before the
/// document origin is subtracted quantizes at world magnitude and silently violates REQ-101 (at
/// easting 2e6, `2000000.10` became `2000000.125`). See the definition for the measurement.
bool ParseWorldPointD(const std::string& raw, double* ox, double* oy, bool allowRelative, double baseX,
                      double baseY);

/// If ortho: snaps dx/dy so segment from anchor is horizontal or vertical (CAD-style).
/// When \p ortho is false but \p st has POLAR tracking on, applies the polar snap instead — the two
/// share this one entry point so every existing ortho call site picks up polar with no change
/// (\ref AppCommandState::polarMode is mutually exclusive with ortho).
///
/// \p anchorZ / \p targetZ are the REAL, already-resolved elevations of the anchor and the cursor
/// pick (issue #371) — e.g. \c AppCommandState::anchorZ and \c AppCommandState::resolvedPointZ /
/// \c uiCursorWorldZ. When both are finite they are used as-is. When either is left at its default
/// NaN, the UCS branch falls back to solving the work-plane equation for Z from (x,y) alone, which
/// is only valid while the plane is close enough to horizontal that Z is a function of (x,y) — it
/// degenerates for a plane that stands on edge to world Z (a Front/Left/Right-style UCS), which is
/// exactly issue #371's repro. New call sites should always pass the real Z when they have it.
///
/// \p wz (optional, issue #371 follow-up) receives the ortho-adjusted world Z when the locked UCS
/// axis maps onto world Z, e.g. squaring to a Front/Left/Right-style UCS's vertical axis. Left
/// untouched when \p wz is null, when ORTHO/POLAR did not fire, or under the World UCS (ORTHO never
/// touches Z there). A caller that commits or previews a point's elevation independently (via
/// \c CadCommitElevation / \c uiCursorWorldZ) MUST pass \p wz and use the value it comes back with
/// instead of re-deriving Z from the raw cursor, or the lock silently fails to reach the render or
/// the committed geometry even though \p wx / \p wy report a locked point.
void ApplyOrthoConstrainFromAnchor(const AppCommandState& st, float anchorX, float anchorY, float* wx, float* wy,
                                   bool ortho, float anchorZ = std::numeric_limits<float>::quiet_NaN(),
                                   float targetZ = std::numeric_limits<float>::quiet_NaN(),
                                   float* wz = nullptr);

/// POLAR tracking (issue #154, REQ-154): snap the world pick onto the nearest polar ray around the
/// anchor, measured in the active UCS's XY plane from +X. No-op unless \p polar and
/// \ref AppCommandState::polarMode. Called from \ref ApplyOrthoConstrainFromAnchor; exposed for the
/// preview paths that want it explicitly.
///
/// \p anchorZ / \p targetZ — see \ref ApplyOrthoConstrainFromAnchor; the same edge-on-plane caveat
/// applies here.
void ApplyPolarConstrainFromAnchor(const AppCommandState& st, float anchorX, float anchorY, float* wx, float* wy,
                                   bool polar, float anchorZ = std::numeric_limits<float>::quiet_NaN(),
                                   float targetZ = std::numeric_limits<float>::quiet_NaN(),
                                   float* wz = nullptr);

/// Piping ortho/polar compass (REQ-346): the PIPERUN counterpart of \ref ApplyPolarConstrainFromAnchor,
/// snapping the next-vertex pick onto the nearest preset angle ray from the run's last committed
/// vertex. Reuses REQ-108's own angle set (\ref AppCommandState::polarIncrementDeg /
/// \c polarExtraAnglesDeg) rather than a second, piping-only list — deliberate, per D-2026-09-17-a —
/// but is gated on its own toggle, \ref AppCommandState::pipeRunCompassOn, so turning it off does not
/// touch ordinary POLAR tracking in other commands. No-op unless \p compass and \c pipeRunCompassOn.
void ApplyPipeRunCompassFromAnchor(const AppCommandState& st, float anchorX, float anchorY, float* wx, float* wy,
                                   bool compass, float anchorZ = std::numeric_limits<float>::quiet_NaN(),
                                   float targetZ = std::numeric_limits<float>::quiet_NaN(),
                                   float* wz = nullptr);

/// Snap pick onto anchor + t*(ux,uy). Negative \p t allowed unless \p forwardOnly.
void ApplySegmentAngleLockToWorldPick(float anchorX, float anchorY, float lockUx, float lockUy, float* wx, float* wy,
                                      bool forwardOnly);

/// ORTHO axis unit vector from the draft anchor toward the crosshair, converting the world-space crosshair
/// into local storage first (REQ-047). False if the crosshair coincides with the anchor.
/// The pure form lives in `OrthoConstrain.hpp` as \c OrthoUnitTowardPoint (both points in one frame).
bool OrthoUnitTowardUiCursorFromAnchor(const AppCommandState& st, float* ux, float* uy);

/// Direct-distance entry under a UCS (issue #371 5th follow-up): the UCS/screen-aware counterpart of
/// \ref OrthoUnitTowardUiCursorFromAnchor, for every UCS other than World. Runs the anchor and the
/// raw cursor hit through the same UCS-ortho decision \ref ApplyOrthoConstrainFromAnchor uses and
/// returns the point \p dist units from the anchor along whichever axis it locked onto, landing in
/// the plane through the anchor. Publishes the resulting elevation through
/// \c AppCommandState::resolvedPointZ (which \c CadCommitElevation reads), so the caller's ordinary
/// commit path picks it up unchanged. False when the cursor coincides with the anchor's locked
/// direction.
bool OrthoUcsDirectDistancePoint(AppCommandState& st, float dist, float* outX, float* outY);

/// Trimmed input parses as exactly one float (allows negative).
bool ParseSingleFloatToken(const std::string& raw, float* out);

/// Parse decimal degrees or `NdNmNs` / `NdNm` (e.g. 45d30m10s). Returns false if invalid.
bool ParseAngleDegrees(const std::string& raw, float* degreesOut);

/// App angle convention: **north (+Y) = 0°, clockwise positive** (survey bearing).
float MathAngleRadFromBearingCwNorthDeg(float bearingDegClockwiseFromNorth);
float BearingCwNorthDegFromMathAngleRad(float mathAngleRadFromEastCcw);

/// Rotation (rad, CCW from +X / math \c atan2(dy,dx)) mapping reference segment ref1→ref2 onto new1→new2.
float RotateDeltaFromReferenceAndNewSegment(float refX1, float refY1, float refX2, float refY2,
                                             float newX1, float newY1, float newX2, float newY2);


/// Build the angle-display settings (REQ-021) from the live UNITS state.
/// The style tables and plot scale an alignment's labels are placed with (REQ-492.5).
[[nodiscard]] inline AlignmentLabelSets::LabelContext CadAlignmentLabelContext(const AppCommandState& st) {
  return AlignmentLabelSets::LabelContext{st.drawing.alignmentLabelSets, st.drawing.alignmentStyles, st.drawing.alignmentLabelStyles,
                                          st.drawing.drawingInsUnits, static_cast<double>(st.drawing.modelUnitsPerPlottedInch)};
}

[[nodiscard]] inline AngleDisplaySettings CadAngleDisplaySettings(const AppCommandState& st) {
  return AngleDisplaySettings{static_cast<AngleDisplayType>(st.angleDisplayType), st.angleDisplayPrecision,
                              st.angleDisplayClockwise, st.angleDisplayBaseDeg};
}
