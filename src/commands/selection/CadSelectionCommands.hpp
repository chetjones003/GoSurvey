inline void ClearEntityGripInteraction(AppCommandState& st) {
  st.entityGripMoveActive = false;
  st.entityGripEntityIndex = -1;
  st.entityGripWhich = -1;
  st.entityGripDownWorldX = 0.f;
  st.entityGripDownWorldY = 0.f;
  st.entityGripTypedDistanceValid = false;
  st.entityGripLengthenMode = false;
}

void RefreshAlignmentOffsets(AppCommandState& st, std::vector<std::string>& log);

inline void RestoreEntityGripOriginal(AppCommandState& st) {
  if (!st.entityGripMoveActive)
    return;
  const int idx = st.entityGripEntityIndex;
  switch (st.entityGripType) {
  case SelectedEntity::Type::LineSeg: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.lineCount()) return;
    CadLineRecord ln = st.drawing.line(static_cast<size_t>(idx));
    ln.x0 = st.entityGripOrigX0;
    ln.y0 = st.entityGripOrigY0;
    ln.x1 = st.entityGripOrigX1;
    ln.y1 = st.entityGripOrigY1;
    st.drawing.setLine(static_cast<size_t>(idx), ln);
    break;
  }
  case SelectedEntity::Type::Circle: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.circleCount()) return;
    CadCircleRecord cr = st.drawing.circle(static_cast<size_t>(idx));
    cr.cx = st.entityGripOrigCx;
    cr.cy = st.entityGripOrigCy;
    cr.r = st.entityGripOrigR;  // cz is Z — a grip drag does not change it
    st.drawing.setCircle(static_cast<size_t>(idx), cr);
    break;
  }
  case SelectedEntity::Type::Spline: {  // REQ-443
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.cadSplines.size())
      return;
    st.drawing.cadSplines[static_cast<size_t>(idx)] = st.entityGripOrigSpline;
    break;
  }
  case SelectedEntity::Type::Alignment: {  // REQ-487.1
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.alignments.size())
      return;
    st.drawing.alignments[static_cast<size_t>(idx)] = st.entityGripOrigAlignment;
    std::vector<std::string> quiet;
    RefreshAlignmentOffsets(st, quiet);  // the linked offsets followed the drag; put them back with it
    break;
  }
  case SelectedEntity::Type::Polyline: {
    if (st.entityGripOrigPolyBulgeVi >= 0) {  // REQ-316 / ADR-047: an arc-segment bulge grip
      if (static_cast<size_t>(st.entityGripOrigPolyBulgeVi) < st.drawing.userPolylineVertsBulge.size())
        st.drawing.userPolylineVertsBulge[static_cast<size_t>(st.entityGripOrigPolyBulgeVi)] = st.entityGripOrigPolyBulge;
      break;
    }
    if (st.entityGripOrigPolylineXIdx < 0)
      return;
    const size_t vIdx = static_cast<size_t>(st.entityGripOrigPolylineXIdx);
    if (vIdx >= st.drawing.vertexCount())
      return;
    st.drawing.vertexComponent(vIdx, CadVertexComponent::X) = st.entityGripOrigPolyVertX;
    st.drawing.vertexComponent(vIdx, CadVertexComponent::Y) = st.entityGripOrigPolyVertY;
    break;
  }
  case SelectedEntity::Type::Arc: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.userArcs.size())
      return;
    CadArc& a = st.drawing.userArcs[static_cast<size_t>(idx)];
    a.cx = st.entityGripOrigCx;
    a.cy = st.entityGripOrigCy;
    a.r = st.entityGripOrigR;
    a.startRad = st.entityGripOrigStartRad;
    a.sweepRad = st.entityGripOrigSweepRad;
    break;
  }
  case SelectedEntity::Type::Ellipse: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.userEllipses.size())
      return;
    CadEllipse& el = st.drawing.userEllipses[static_cast<size_t>(idx)];
    el.cx = st.entityGripOrigEllCx;
    el.cy = st.entityGripOrigEllCy;
    el.majVx = st.entityGripOrigEllMajVx;
    el.majVy = st.entityGripOrigEllMajVy;
    el.ratio = st.entityGripOrigEllRatio;
    break;
  }
  case SelectedEntity::Type::BlockRef: {
    if (idx < 0 || static_cast<size_t>(idx) >= st.drawing.cadBlockRefs.size())
      return;
    CadBlockRef& r = st.drawing.cadBlockRefs[static_cast<size_t>(idx)];
    CadBlockParamSet(&r, "DistNeg", st.entityGripOrigX0);
    CadBlockParamSet(&r, "DistPos", st.entityGripOrigY0);
    CadBlockParamSet(&r, "SheetOff", st.entityGripOrigX1);
    CadBlockParamSet(&r, "NorthOff", st.entityGripOrigY1);
    CadBlockParamSet(&r, "Flip", st.entityGripOrigR);
    r.xf.x = st.entityGripOrigCx;
    r.xf.y = st.entityGripOrigCy;
    break;
  }
  default:
    break;
  }
}

/// Closest CAD entity within tolerance (later draw order wins on tie). False if none.
/// \param outDistSq Optional: pass null when only the entity matters, not how near the pick was.
/// \param pickRay When non-null AND valid, entities are measured against this world ray in 3D
///        instead of against \p wx,\p wy in plan (REQ-058). Pass it whenever the camera is
///        orbited: the ray crosses the work plane at one XY and an elevated entity at another, so
///        the plan test would measure to the wrong place. Null (the default) keeps the exact
///        pre-3D behaviour, which is what plan view continues to use.
bool PickClosestCadEntity(const AppCommandState& st, double wx, double wy, float tolWorld, SelectedEntity* out,
                          float* outDistSq, const ray3d::Ray* pickRay = nullptr,
                          std::vector<CadPickCandidate>* allCandidates = nullptr);
/// From a non-empty candidate list, pick the entity nearest the camera (ray mode) or highest Z (plan).
bool PickCadEntityByDepth(const std::vector<CadPickCandidate>& candidates, SelectedEntity* out,
                          const ray3d::Ray* pickRay);
/// The nearest whole SOLID under \p ray, as a `SelectedEntity` of `Type::Solid`. False if none.
///
/// **`PickClosestCadEntity` above cannot answer this and never could**: it returns only `LineSeg`,
/// `Arc`, `Circle`, `Ellipse` and `Polyline`. Every consumer of it — the hover pre-highlight and
/// every click that selects one entity — therefore behaved as though solids were not there, and the
/// only thing that ever put a solid in a selection was `ComputeSelectionFromRect`. So a solid could
/// be selected by dragging a rectangle around it and by nothing else, with no highlight beforehand,
/// in every command and when idle. Reported from the real app twice in one session, as two separate
/// complaints that turned out to be this one gap.
///
/// Built on \ref PickSubObjectAcrossSolids, which already does ray-versus-solid hit testing for the
/// `Ctrl`+click sub-object pick (REQ-318): the geometry was there, only a whole-solid caller was
/// missing. **Which hit names the solid follows the visual style, as in AutoCAD** (D-2026-09-16-b):
/// in 2D Wireframe no face is drawn, so only an edge or a vertex answers — a click inside the
/// outline starts a selection box, and an edge seen through the solid can be clicked. In Hidden and
/// Shaded a visible face answers too. Clicking anywhere on a face in plan view had been taking
/// clicks meant for survey points and selection boxes inside a building pad (code review on #478,
/// finding 4).
///
/// Geometry the live section clip has hidden does not answer (finding 5) — see
/// \ref PickSubObjectAcrossSolids.
///
/// Like the sub-object pick it never tessellates (REQ-318 item 7) — a solid absent from the display
/// cache is simply not picked.
[[nodiscard]] bool PickClosestSolidEntity(const AppCommandState& st, const ray3d::Ray& ray, float tolWorld,
                                          SelectedEntity* out, double* outRayT = nullptr);

/// Which kind of thing \ref ResolveViewportPick found under the cursor (GitHub issue #564 §2).
enum class ViewportPickFamily : std::uint8_t { None, Table, Annotation, Linework, Solid, FilledRegion, PdfUnderlay };

/// The inputs a viewport hover or click already has, gathered so both ask the SAME question.
struct ViewportPickRequest {
  /// The cursor on the work plane — the plan-view pick point, exactly as before.
  double rawX = 0.0;
  double rawY = 0.0;
  /// The orbited-view ray the linework, text and fill picks measure against (REQ-058); null in plan
  /// view, which keeps every pre-3D XY test byte-identical.
  const ray3d::Ray* orbitRay = nullptr;
  /// The camera ray through the pixel, in every view — what the solid pick and every DEPTH
  /// comparison use.
  ray3d::Ray eyeRay{};
  bool eyeRayValid = false;
  /// Linework tolerance: the hover's tight one or the click's wider one.
  float lineTol = 0.f;
  /// For the text / table tolerance (pixel aperture → world).
  float orthoHalfH = 50.f;
  float viewportHeightPx = 700.f;
  bool modelSpace = true;
  /// A survey point is under the cursor: solids yield to it, as they always have.
  bool surveyPointUnderCursor = false;
};

struct ViewportPickResult {
  ViewportPickFamily family = ViewportPickFamily::None;
  SelectedEntity entity{};
  /// For \c Linework: the candidates within tolerance that are VISIBLE (not behind an opaque
  /// solid), for the disambiguation popup. Empty otherwise.
  std::vector<CadPickCandidate> candidates;
};

/// What visible thing is under this pixel (GitHub issue #564 §2, D-2026-09-28-e). The ONE resolution
/// the viewport hover and both click paths use, so what pre-highlights is what a click takes.
///
/// Families keep their long-standing precedence — table, text, linework, solid, fill — with one rule
/// added: **nothing behind an opaque solid answers.** In Hidden and Shaded a solid's surface is found
/// along the camera ray and any candidate farther than it (beyond the pick tolerance) is dropped; and
/// where a solid is nearer the eye than the linework under the cursor, the solid wins. In 2D
/// Wireframe a solid is see-through (D-2026-09-16-b, Q1 answered 2026-09-28): only its EDGES answer,
/// by the same nearer-wins rule, and it hides nothing. With no solids in the drawing, every step is
/// the pre-change call with the pre-change arguments.
ViewportPickResult ResolveViewportPick(const AppCommandState& st, const ViewportPickRequest& rq);

/// The CLICK's answer (issue #564 §2): the hover's own question first — \p rq with the hover's
/// \p hoverLineTol — and only when that finds nothing, \p rq as given (the click's wider linework
/// tolerance). So whatever pre-highlighted is exactly what the click takes, while a click that lands
/// just outside the aperture keeps the forgiving radius it always had. With only linework under the
/// cursor this is the pre-change answer: the nearest entity inside the tight radius is also the
/// nearest inside the wide one. A linework answer carries the WIDE query's visible candidates, as
/// the disambiguation popup always has.
ViewportPickResult ResolveViewportClickPick(const AppCommandState& st, const ViewportPickRequest& rq,
                                            float hoverLineTol);
