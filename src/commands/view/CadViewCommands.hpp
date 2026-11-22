/// Build the model viewport's camera from the canonical view state (REQ-058 / ADR-025 (c)).
///
/// The camera is **derived, never stored**: pan is the target, zoom is the ortho half-height, and
/// only the two orientation angles are new state. Constructing it fresh at each use makes drift
/// between "the camera" and "the view" impossible. Commands → Renderer is a downward dependency
/// (architecture §2), so including the Camera value type here is legal.
///
/// `halfH = (1/zoom) * 50` reproduces the constant the renderer and the UI have always shared.
///
/// The near/far range is the Camera's own +/-100000, NOT the pre-3D `Ortho(..., -1000, 1000)`.
/// The old +/-1000 was carried over from the flat renderer, where nothing ever had a Z; once Z is
/// real it silently clips every entity above 1000 out of the view — and a surveyed site sits at an
/// elevation of a few thousand feet, so that is the whole drawing, in plan view, where Z should not
/// affect visibility at all. Widening costs nothing: depth testing is off (draw order decides), so
/// the range only ever determines what survives clipping.
inline Camera CadViewCamera(const AppCommandState& st) {
  Camera c = Camera::Plan(st.viewportPanX, st.viewportPanY,
                          (1.f / std::max(st.viewportZoom, 1.e-9f)) * 50.f);
  c.targetZ = st.viewportPanZ;
  c.azimuthDeg = st.viewportAzimuthDeg;
  c.elevationDeg = st.viewportElevationDeg;
  c.rollDeg = st.viewportRollDeg;  // #153: nonzero only under a tilted-UCS PLAN
  c.projection = st.viewportProjection;
  c.fovDeg = st.viewportFovDeg;
  c.nearZ = -100000.f;
  c.farZ = 100000.f;
  return c;
}

/// True when the model view is unrotated — the case in which every pre-3D screen/world mapping,
/// pick test and snap remains exactly valid and is therefore used unchanged (REQ-058 acceptance:
/// "plan view renders pixel-comparable to the pre-change build").
inline bool CadViewIsPlan(const AppCommandState& st) {
  return std::fabs(st.viewportElevationDeg - 90.f) < 1e-4f && std::fabs(st.viewportAzimuthDeg) < 1e-4f;
}

/// True when model-space Qt overlay and linework should show geometry on \p layerName.
inline bool CadLayerAllowsViewportDisplay(const AppCommandState& st, const std::string& layerName) {
  const CadLayerRow* row = FindDrawingLayerRowCi(st, layerName);
  if (!row)
    return true;
  return row->on && !row->frozen;
}

/// Prefer \p preferred, but fall back to layer \c 0 or the first on/thawed layer so new entities are visible.
inline std::string CadVisibleLayerForNewEntity(const AppCommandState& st, const std::string& preferred) {
  const std::string layer = preferred.empty() ? std::string("0") : preferred;
  if (CadLayerAllowsViewportDisplay(st, layer))
    return layer;
  if (CadLayerAllowsViewportDisplay(st, "0"))
    return "0";
  for (const CadLayerRow& row : st.drawing.drawingLayerTable) {
    if (CadLayerAllowsViewportDisplay(st, row.name))
      return row.name;
  }
  return layer;
}

inline const DimensionStyle& CadDimensionStyleForAnnotation(const AppCommandState& st,
                                                              const CadAnnotation& ann) {
  if (!ann.dimensionStyleName.empty()) {
    for (const DimensionStyle& style : st.drawing.dimensionStyles) {
      if (style.name == ann.dimensionStyleName)
        return style;
    }
  }
  return st.drawing.activeDimensionStyle;
}

/// Duration of a ViewCube orientation change, in seconds. Short enough not to feel sluggish, long
/// enough to read the rotation — REQ-059 requires the view to settle within 0.5 s.
inline constexpr float kViewAnimSeconds = 0.28f;

/// Begin easing the view to \p az / \p el (REQ-059). Azimuth travels the SHORT way around, so a
/// move from 350° to 45° turns 55° forward rather than 305° backward.
inline void CadStartViewAnimation(AppCommandState& st, float az, float el, float roll = 0.f) {
  st.viewAnimFromAz = st.viewportAzimuthDeg;
  st.viewAnimFromEl = st.viewportElevationDeg;
  st.viewAnimFromRoll = st.viewportRollDeg;
  // Unwrapped target, so the lerp below cannot take the long way round (Camera::ShortestAzimuthDelta).
  st.viewAnimToAz = st.viewportAzimuthDeg + Camera::ShortestAzimuthDelta(st.viewportAzimuthDeg, az);
  st.viewAnimToEl = el;
  // Roll wraps like azimuth, so ease it the short way too (#153). Callers that do not pass a roll
  // get 0 here, which returns a rolled view to upright — the correct move for every non-PLAN caller.
  st.viewAnimToRoll = st.viewportRollDeg + Camera::ShortestAzimuthDelta(st.viewportRollDeg, roll);
  st.viewAnimT = 0.f;
  st.viewAnimActive = true;
}

/// Advance an in-flight view animation by \p dtSeconds. No-op when nothing is animating.
inline void CadTickViewAnimation(AppCommandState& st, float dtSeconds) {
  if (!st.viewAnimActive)
    return;
  st.viewAnimT += (dtSeconds > 0.f ? dtSeconds : 0.f) / kViewAnimSeconds;
  if (st.viewAnimT >= 1.f) {
    st.viewAnimT = 1.f;
    st.viewAnimActive = false;
  }
  const float t = st.viewAnimT;
  const float e = t * t * (3.f - 2.f * t);  // smoothstep: eases in and out, no overshoot
  float az = st.viewAnimFromAz + (st.viewAnimToAz - st.viewAnimFromAz) * e;
  while (az < 0.f)
    az += 360.f;
  while (az >= 360.f)
    az -= 360.f;
  st.viewportAzimuthDeg = az;
  st.viewportElevationDeg = st.viewAnimFromEl + (st.viewAnimToEl - st.viewAnimFromEl) * e;
  float roll = st.viewAnimFromRoll + (st.viewAnimToRoll - st.viewAnimFromRoll) * e;
  while (roll < 0.f)
    roll += 360.f;
  while (roll >= 360.f)
    roll -= 360.f;
  st.viewportRollDeg = roll;
}

/// Elevation at which newly drawn geometry lands — the active work plane's Z (REQ-058 / REQ-154).
///
/// Two channels, in order:
///
///  1. **The resolved point's own Z**, when point entry has published one. On a tilted UCS the work
///     plane's Z *varies across the plane*, so a single constant cannot describe where a point
///     lands — the value has to come from the point that was actually resolved (the click's
///     ray x plane hit, or the typed UCS coordinate mapped through the frame).
///  2. Otherwise the UCS origin's Z, which is exactly the old behaviour.
///
/// Under any UCS parallel to world XY — every pre-UCS drawing, and every UCS that is a rotation
/// about Z — the two agree by construction, so this is a strict superset of what ELEV did rather
/// than a change to it.
inline float CadWorkPlaneElevation(const AppCommandState& st) {
  if (st.resolvedPointZValid)
    return st.resolvedPointZ;
  return static_cast<float>(st.activeUcs.origin.z);
}

/// Elevation a click should COMMIT at: the snapped point's own Z when an object snap is active,
/// otherwise the work plane (REQ-058).
///
/// This is the AutoCAD rule — an object snap returns the object's real 3D point, so snapping to
/// the end of a line lying on the datum gives you that endpoint even when ELEV is set well above
/// it. Without the override, snapped geometry would be silently lifted to the current elevation
/// and would not touch the thing it was snapped to.
inline float CadCommitElevation(const AppCommandState& st) {
  return st.viewportSnapPickValid ? static_cast<float>(st.viewportSnapPickLocalZ) : CadWorkPlaneElevation(st);
}

/// True when the active UCS is the World Coordinate System — the default, and what the status bar
/// reports as "World".
inline bool CadUcsIsWorld(const AppCommandState& st) { return ucs::IsWorld(st.activeUcs); }

/// The DRAWING-scoped active UCS (REQ-154), correct even while a per-viewport frame is live in
/// \ref AppCommandState::activeUcs because floating model space is entered (REQ-155). Persistence —
/// the `internal document payload` save and the per-tab document snapshot — must read this, never `activeUcs` directly,
/// or a save/tab-switch performed while floating would record the viewport's frame as the drawing's.
inline const ucs::Ucs& CadDrawingScopedUcs(const AppCommandState& st) {
  return st.floatingUcsSwapActive ? st.drawingActiveUcsStash : st.activeUcs;
}

/// The active UCS expressed in **storage space** (local XY, absolute Z).
///
/// \ref AppCommandState::activeUcs is stored in TRUE WORLD coordinates, so that a document-origin
/// rebase — which shifts every stored coordinate to keep float precision (REQ-101) — cannot move
/// the user's coordinate frame out from under them. Nothing has to remember to shift it, because
/// there is nothing frame-relative to shift.
///
/// The camera, the picking rays and the geometry they hit all live in storage space, so anything
/// that meets a ray converts here first. Only the origin moves; a translation cannot rotate a
/// basis, so the axes pass through untouched.
inline ucs::Ucs CadActiveUcsStorage(const AppCommandState& st) {
  ucs::Ucs u = st.activeUcs;
  u.origin.x -= st.drawing.worldDocumentOriginX;
  u.origin.y -= st.drawing.worldDocumentOriginY;
  return u;
}

/// The live section clip as it stands (REQ-341), in STORAGE coordinates like everything the renderer
/// and the picks see. Inactive when the clip is off. The one derivation the renderer, the solid pick
/// and the snap all read, so the three cannot disagree about where the cut is.
[[nodiscard]] ucs::Ucs CadEffectiveSectionClipFrame(const AppCommandState& st);
inline SectionClipPlane CadActiveSectionClip(const AppCommandState& st) {
  if (!st.viewportSectionClip)
    return SectionClipPlane{};
  // REQ-342: the face SECTIONPLANE was given, or the active UCS when it was never given one.
  return SectionClipFromUcs(CadEffectiveSectionClipFrame(st), st.viewportSectionClipOffset,
                            st.viewportSectionClipFlip);
}

/// The active work plane (UCS XY) a viewport click resolves against (REQ-058 / ADR-025 (e)).
/// In storage space, because that is the space the ray is in.
inline ray3d::Plane CadActiveWorkPlane(const AppCommandState& st) { return ucs::WorkPlane(CadActiveUcsStorage(st)); }

/// True when the active work plane is parallel to world XY and faces up (REQ-312).
///
/// Every UCS that is a translation and/or a rotation about Z satisfies this - which is the whole
/// 2D survey case, and the default. It is the branch guard for the arbitrary-plane drawing paths,
/// and it is deliberately NOT `CadUcsIsWorld`: a UCS squared to a road centreline is still a
/// flat drawing, and it must keep the exact float path every existing drawing, transcript and test
/// already goes through. That is REQ-154's own reasoning for its WCS branch, applied one level out.
inline bool CadWorkPlaneIsWorldXy(const AppCommandState& st) {
  // States the condition directly rather than borrowing `ucs::PlanViewIsExact`, which this used to
  // call. That predicate answers the CAMERA's question - "can PLAN put UCS +Y up the screen
  // exactly?" - and issue #153 gave `Camera` a roll axis, after which the answer became yes for
  // EVERY valid frame. The name did not change and neither did the call site, so a tilted drawing
  // silently began taking the flat branch here: the guard inverted without a compiler error, and
  // the four REQ-312 transcripts are what caught it. A predicate named for another subsystem's
  // concern is not a safe way to ask whether a plane is parallel to world XY, so this asks.
  const ucs::Ucs& u = st.activeUcs;
  constexpr double kTol = 1e-6;
  return std::fabs(u.zAxis.x) <= kTol && std::fabs(u.zAxis.y) <= kTol && u.zAxis.z > 0.0;
}

/// The work plane, moved so its origin sits on \p ox,\p oy,\p oz (REQ-312).
///
/// Anchoring on the first pick rather than on the UCS origin keeps the 2D coordinates that come out
/// of it small. The planar maths the draw commands use (circumcircle, swept angle) runs in float,
/// and at state-plane magnitude a float has a quarter-foot of resolution - the same REQ-101
/// narrowing hazard the document origin exists to avoid, arriving through a different door.
inline ucs::Ucs CadWorkPlaneAnchoredAt(const AppCommandState& st, float ox, float oy, float oz) {
  return ucs::WithOrigin(CadActiveUcsStorage(st),
                         {static_cast<double>(ox), static_cast<double>(oy), static_cast<double>(oz)});
}

/// The normal of the plane a new curve commits into: the active UCS's Z axis (REQ-312).
inline void CadActiveDrawPlaneNormal(const AppCommandState& st, float* nx, float* ny, float* nz) {
  const ucs::Ucs u = st.activeUcs;  // a translation cannot rotate a basis, so storage vs world is moot
  if (nx)
    *nx = static_cast<float>(u.zAxis.x);
  if (ny)
    *ny = static_cast<float>(u.zAxis.y);
  if (nz)
    *nz = static_cast<float>(u.zAxis.z);
}

/// A circle solved from picks: where its centre is, how big it is, and which way its plane faces.
///
/// The return type of the CIRCLE solvers below. It exists so the geometry a set of picks defines can
/// be computed WITHOUT committing it -- the rubber-band preview needs exactly that, and computing it
/// a second way in the preview is how a preview comes to show a shape the commit does not produce.
struct CadCircleSolution {
  float cx = 0.f;
  float cy = 0.f;
  float cz = 0.f;
  float r = 0.f;
  float nx = kFlatNormalX;
  float ny = kFlatNormalY;
  float nz = kFlatNormalZ;
};

/// CIRCLE centre-and-radius: the circle a centre pick and a rim pick define on the work plane.
///
/// On a flat work plane this is the pre-REQ-312 arithmetic to the bit. On a tilted one the rim pick
/// is displaced in Z as well, so the radius is the 3D distance to it -- its XY projection is short
/// by cos(tilt), and on a vertical plane it collapses to nothing at all.
[[nodiscard]] CadCircleSolution CadSolveCircleFromRimPick(const AppCommandState& st, float cx, float cy, float cz,
                                                          float px, float py, float pz);

/// CIRCLE 3P: the circle through three picks on the active work plane.
///
/// False when the picks are collinear -- in the plane, which on a tilted plane is not the same
/// question as collinear in the XY projection.
[[nodiscard]] bool CadSolveCircleThreePoints(const AppCommandState& st, float ax, float ay, float az, float bx,
                                             float by, float bz, float cx, float cy, float cz,
                                             CadCircleSolution* out);

/// ARC 3P: the arc through three picks on the active work plane, angles measured in the arc's own
/// frame (`ucs::FromNormal`), which is where CadArc::startRad and CadArc::sweepRad live.
///
/// False when the picks are collinear. \p out is left untouched on failure. The caller decides what
/// a failure means -- the commit reports it and resets the draft, the preview just draws nothing.
[[nodiscard]] bool CadSolveArcThreePoints(const AppCommandState& st, float ax, float ay, float az, float bx,
                                          float by, float bz, float cx, float cy, float cz, CadArc* out);

/// The **camera-azimuth offset** that squares the view with the active UCS's north (REQ-059).
///
/// Negated relative to the UCS's own rotation, and that sign is not a detail to gloss: a positive
/// UCS rotation about Z turns the frame counter-clockwise, while a positive camera azimuth turns
/// screen-up clockwise (measured against `Camera`, see `ucs::PlanViewAngles`). Handing the ViewCube
/// the un-negated angle makes its compass square up the wrong way — visibly, but only under a
/// rotated UCS, which is exactly the case nobody exercises by accident.
inline float CadUcsViewAzimuthOffsetDeg(const AppCommandState& st) {
  return -ucs::AzimuthAboutWorldZDeg(st.activeUcs);
}
