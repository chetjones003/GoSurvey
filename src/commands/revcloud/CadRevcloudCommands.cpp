#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"

#include <cstdio>

// ---------------------------------------------------------------------------------------------
// REVCLOUD (REQ-444, GitHub issue #803): a scalloped closed outline. Shaped after POLYSOLID above
// - a path-building command with its own Kind, its own draft state, and one prompt-text function
// the status bar and the typed-line echo share - but it commits NO new entity kind: the result is
// an ordinary closed polyline whose segments are all arcs (REQ-316), so selection, snapping, grips,
// layers, undo and DXF/DWG are already right for it. The scallop geometry is `BuildRevisionCloud`
// (geom2d), which the live preview and the commit both call, so what is on screen is what is made.
// ---------------------------------------------------------------------------------------------

namespace {

[[nodiscard]] std::string RevcloudNumber(double v) {
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

/// Add the finished cloud to the polyline store. The caller owns the undo snapshot.
void AppendRevcloudPolyline(AppCommandState& st, const std::vector<double>& xy,
                            const std::vector<float>& bulge, double z) {
  const int n = static_cast<int>(bulge.size());
  if (st.drawing.userPolylineOffsets.empty())
    st.drawing.userPolylineOffsets.push_back(0);
  const int baseVert = st.drawing.userPolylineOffsets.back();
  for (int i = 0; i < n; ++i) {
    st.drawing.pushVertex(CadPolyVertex{static_cast<float>(xy[static_cast<std::size_t>(i) * 2]), static_cast<float>(xy[static_cast<std::size_t>(i) * 2 + 1]), static_cast<float>(z)});
  }
  st.drawing.userPolylineOffsets.push_back(baseVert + n);
  SyncPolylineBulge(st.drawing.userPolylineVertsBulge, st.drawing.userPolylineVerts.size());
  for (int i = 0; i < n; ++i)
    st.drawing.userPolylineVertsBulge[static_cast<std::size_t>(baseVert) + static_cast<std::size_t>(i)] =
        bulge[static_cast<std::size_t>(i)];
  SyncPolylineNormal(st.drawing.userPolylineVertsNormal, st.drawing.userPolylineVerts.size());
  st.drawing.userPolylineClosed.push_back(1u);
  st.drawing.userPolylineAttrs.push_back(MakeNewEntityAttrs(st));
  EnsureAttrCounts(st);
  BumpCadGpuCache(st);
}

/// Build the cloud for \p corners and store it. \p replace, when set, is erased first and shares
/// the one undo step. False (nothing changed) with the reason logged if the cloud cannot be built.
bool CommitRevcloud(AppCommandState& st, const std::vector<double>& corners, double z,
                    const SelectedEntity* replace, std::vector<std::string>& log) {
  std::vector<double> xy;
  std::vector<float> bulge;
  std::string why;
  if (!BuildRevisionCloud(corners, st.revcloudArcLength, st.revcloudReverse, &xy, &bulge, &why)) {
    log.push_back("REVCLOUD - " + why + ".");
    return false;
  }
  if (replace) {
    // ExecuteDeleteSelection takes the undo snapshot itself, and the cloud is added after it, so
    // one UNDO puts the original back and removes the cloud together.
    st.selection.assign(1, *replace);
    ExecuteDeleteSelection(st, log);
  } else {
    PushUndoSnapshot(st, "Revision Cloud");
  }
  AppendRevcloudPolyline(st, xy, bulge, z);
  log.push_back("REVCLOUD - cloud of " + std::to_string(bulge.size()) + " arcs created.");
  return true;
}

}  // namespace

void CancelRevcloudCommand(AppCommandState& st) {
  if (auto* s = st.sessionAs<stepengine::RevcloudSession>()) {
    s->phase = stepengine::RevcloudPhase::WaitFirstPoint;
    s->draft.clear();
    s->pending = 0;
  }
}

std::string CadRevcloudPromptText(const AppCommandState& st) {
  using P = stepengine::RevcloudPhase;
  const auto* s = st.sessionAs<stepengine::RevcloudSession>();
  const P phase = s ? s->phase : P::WaitFirstPoint;
  const std::string opts = std::string("[Arc length=") + RevcloudNumber(st.revcloudArcLength) +
                           (st.revcloudReverse ? " Reverse=on" : "") + "]";
  switch (phase) {
  case P::WaitObject:
    return std::string("REVCLOUD - select a closed polyline, rectangle or circle to turn into a cloud ") +
           opts + (st.revcloudDeleteOriginal ? " (original will be deleted)" : " (original is kept)");
  case P::WaitFirstPoint:
    return "REVCLOUD - first corner, or Object / Arc length / Reverse / Delete original " + opts;
  case P::WaitNextPoint:
    break;
  }
  return std::string("REVCLOUD - next corner, or Close / Undo / Arc length / Reverse, Enter to close ") + opts;
}

void StartRevcloudCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (ActivePaperGeometryTarget(st)) {
    log.push_back("REVCLOUD - revision clouds can only be drawn in model space.");
    return;
  }
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  stepengine::BeginSession(st, std::make_unique<stepengine::RevcloudSession>());
  log.push_back(CadRevcloudPromptText(st));
}

std::vector<double> CadRevcloudPreviewPolyline(const AppCommandState& st, double curX, double curY,
                                               std::vector<float>* outBulge) {
  const auto* s = st.sessionAs<stepengine::RevcloudSession>();
  if (!s)
    return {};
  std::vector<double> corners = s->draft;
  corners.push_back(curX);
  corners.push_back(curY);
  std::vector<double> xy;
  std::vector<float> bulge;
  if (corners.size() < 6 || !BuildRevisionCloud(corners, st.revcloudArcLength, st.revcloudReverse, &xy, &bulge, nullptr))
    return {};
  if (outBulge)
    *outBulge = std::move(bulge);
  return xy;
}

namespace {

/// The corners of the closed polyline or circle under (\p wx, \p wy), and the elevation it lies at.
/// False, with the reason logged, for anything that is not a flat closed shape of straight edges
/// or a circle.
bool RevcloudCornersFromObject(const AppCommandState& st, const SelectedEntity& hit,
                               std::vector<double>* corners, double* z, std::vector<std::string>& log) {
  using T = SelectedEntity::Type;
  const std::size_t i = static_cast<std::size_t>(hit.index);
  if (hit.type == T::Circle) {
    float nx = 0.f, ny = 0.f, nz = 1.f;
    CircleNormalAt(st.drawing.userCircleNormals, i, &nx, &ny, &nz);
    if (std::fabs(nx) > 1e-6f || std::fabs(ny) > 1e-6f) {
      log.push_back("REVCLOUD - that circle is not flat in the XY plane; a cloud can only follow a flat shape.");
      return false;
    }
    const CadCircleRecord circle = st.drawing.circle(i);
    const double cx = circle.cx;
    const double cy = circle.cy;
    const double r = circle.r;
    *z = circle.cz;
    // Corners spaced so each chord's scallop is no longer than the arc length, never fewer than 8
    // or the polygon is a poor stand-in for the circle.
    const double chord = st.revcloudArcLength / RevisionCloudArcPerChord();
    const double sinHalf = std::min(1.0, chord / (2.0 * r));
    const int n = std::max(8, static_cast<int>(std::ceil(3.14159265358979323846 / std::asin(sinHalf))));
    if (n > kRevisionCloudMaxScallops) {
      log.push_back("REVCLOUD - that arc length is too small for this circle; the cloud would be enormous.");
      return false;
    }
    for (int k = 0; k < n; ++k) {
      const double a = 2.0 * 3.14159265358979323846 * k / n;
      corners->push_back(cx + r * std::cos(a));
      corners->push_back(cy + r * std::sin(a));
    }
    return true;
  }
  // Polyline.
  if (i >= st.drawing.userPolylineClosed.size() || !st.drawing.userPolylineClosed[i]) {
    log.push_back("REVCLOUD - that polyline is not closed; close it first (or draw the cloud by clicking points).");
    return false;
  }
  const int a = st.drawing.userPolylineOffsets[i];
  const int b = st.drawing.userPolylineOffsets[i + 1];
  *z = st.drawing.vertex(a).z;
  for (int v = a; v < b; ++v) {
    const std::size_t at = static_cast<std::size_t>(v);
    if (static_cast<std::size_t>(v) < st.drawing.userPolylineVertsBulge.size() && st.drawing.userPolylineVertsBulge[at] != 0.f) {
      log.push_back("REVCLOUD - that polyline has curved segments; a cloud can only follow straight edges.");
      return false;
    }
    if (std::fabs(static_cast<double>(st.drawing.vertex(static_cast<size_t>(at)).z) - *z) > 1e-4) {
      log.push_back("REVCLOUD - that polyline is not flat; a cloud can only follow a flat shape.");
      return false;
    }
    corners->push_back(st.drawing.vertex(static_cast<size_t>(at)).x);
    corners->push_back(st.drawing.vertex(static_cast<size_t>(at)).y);
  }
  return true;
}

void ConvertRevcloudObjectAt(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  SelectedEntity hit;
  if (!PickClosestCadEntity(st, wx, wy, CadOffsetEntityPickTolWorld(st), &hit, nullptr)) {
    log.push_back("REVCLOUD - nothing there. Pick a closed polyline, rectangle or circle (Esc cancels).");
    return;
  }
  if (hit.type != SelectedEntity::Type::Polyline && hit.type != SelectedEntity::Type::Circle) {
    log.push_back("REVCLOUD - a cloud can follow a closed polyline, a rectangle or a circle. Nothing else is a closed shape of straight edges.");
    return;
  }
  std::vector<double> corners;
  double z = 0.0;
  if (!RevcloudCornersFromObject(st, hit, &corners, &z, log))
    return;
  if (!CommitRevcloud(st, corners, z, st.revcloudDeleteOriginal ? &hit : nullptr, log))
    return;  // the reason is logged; the command stays open at the Object prompt
  stepengine::EndSession(st);
}

/// Close the draft path into a cloud and end the command; stays open (reason logged) if it cannot.
void FinishRevcloudPath(AppCommandState& st, std::vector<std::string>& log) {
  auto* s = st.sessionAs<stepengine::RevcloudSession>();
  if (!s)
    return;
  if (s->draft.size() < 6) {
    log.push_back("REVCLOUD - a cloud needs at least three corners.");
    return;
  }
  if (!CommitRevcloud(st, s->draft, s->z, nullptr, log))
    return;
  stepengine::EndSession(st);
}

void AddRevcloudCorner(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  auto* s = st.sessionAs<stepengine::RevcloudSession>();
  if (!s)
    return;
  using P = stepengine::RevcloudPhase;
  if (s->phase == P::WaitFirstPoint) {
    s->draft.assign({x, y});
    s->z = static_cast<double>(CadCommitElevation(st));
    s->phase = P::WaitNextPoint;
    log.push_back(CadRevcloudPromptText(st));
    return;
  }
  const std::size_t n = s->draft.size();
  if (n >= 6 && std::hypot(x - s->draft[0], y - s->draft[1]) < st.revcloudArcLength * 0.5) {
    FinishRevcloudPath(st, log);
    return;
  }
  if (std::hypot(x - s->draft[n - 2], y - s->draft[n - 1]) < 1e-9) {
    log.push_back("REVCLOUD - that is the same point as the last corner.");
    return;
  }
  s->draft.push_back(x);
  s->draft.push_back(y);
  log.push_back(CadRevcloudPromptText(st));
}

bool SetRevcloudArcLength(AppCommandState& st, const std::string& text, std::vector<std::string>& log) {
  char* end = nullptr;
  const double v = std::strtod(text.c_str(), &end);
  if (text.empty() || !end || *end != '\0' || !std::isfinite(v) || v <= 0.0) {
    log.push_back("REVCLOUD - the arc length must be a number greater than zero; it stays " +
                  RevcloudNumber(st.revcloudArcLength) + ".");
    return false;
  }
  st.revcloudArcLength = v;
  log.push_back("REVCLOUD - arc length = " + RevcloudNumber(v) + ".");
  return true;
}

}  // namespace

bool HandleRevcloudTextInput(const std::string& lineIn, AppCommandState& st, std::vector<std::string>& log) {
  auto* s = st.sessionAs<stepengine::RevcloudSession>();
  if (!s)
    return false;
  using P = stepengine::RevcloudPhase;
  const std::string line = StringUtil::trimCopy(lineIn);

  if (s->pending != 0) {
    s->pending = 0;
    if (!line.empty())
      (void)SetRevcloudArcLength(st, line, log);
    log.push_back(CadRevcloudPromptText(st));
    return true;
  }

  if (line.empty()) {
    if (s->phase == P::WaitNextPoint) {
      FinishRevcloudPath(st, log);
      return true;
    }
    log.push_back("REVCLOUD canceled.");
    stepengine::EndSession(st);
    return true;
  }

  const char head = static_cast<char>(std::toupper(static_cast<unsigned char>(line[0])));
  const std::string rest = StringUtil::trimCopy(line.substr(1));
  const bool bareLetter = rest.empty();

  if (head == 'A') {
    if (bareLetter) {
      s->pending = 'A';
      log.push_back("REVCLOUD - arc length?");
      return true;
    }
    (void)SetRevcloudArcLength(st, rest, log);
    log.push_back(CadRevcloudPromptText(st));
    return true;
  }
  if (head == 'R' && bareLetter) {
    st.revcloudReverse = !st.revcloudReverse;
    log.push_back(st.revcloudReverse ? "REVCLOUD - reverse direction on: the arcs bulge inward."
                                     : "REVCLOUD - reverse direction off: the arcs bulge outward.");
    log.push_back(CadRevcloudPromptText(st));
    return true;
  }
  if (head == 'D' && bareLetter) {
    st.revcloudDeleteOriginal = !st.revcloudDeleteOriginal;
    log.push_back(st.revcloudDeleteOriginal ? "REVCLOUD - Object mode will delete the original shape."
                                            : "REVCLOUD - Object mode will keep the original shape.");
    log.push_back(CadRevcloudPromptText(st));
    return true;
  }
  if (head == 'O' && bareLetter) {
    if (s->phase == P::WaitNextPoint) {
      log.push_back("REVCLOUD - Object converts a shape already drawn; it cannot join a path already under way.");
      return true;
    }
    s->phase = P::WaitObject;
    log.push_back(CadRevcloudPromptText(st));
    return true;
  }
  if (head == 'C' && bareLetter) {
    if (s->phase != P::WaitNextPoint) {
      log.push_back("REVCLOUD - nothing to close yet.");
      return true;
    }
    FinishRevcloudPath(st, log);
    return true;
  }
  if (head == 'U' && bareLetter) {
    if (s->phase != P::WaitNextPoint) {
      log.push_back("REVCLOUD - nothing to undo yet.");
      return true;
    }
    s->draft.resize(s->draft.size() - 2);
    if (s->draft.empty()) {
      s->phase = P::WaitFirstPoint;
      log.push_back("REVCLOUD - back to the first corner.");
    }
    log.push_back(CadRevcloudPromptText(st));
    return true;
  }

  if (s->phase == P::WaitObject) {
    log.push_back("REVCLOUD - click the closed polyline, rectangle or circle to turn into a cloud (Esc cancels).");
    return true;
  }
  ray3d::Vec3 last{};
  const bool haveLast = s->phase == P::WaitNextPoint;
  if (haveLast)
    last = ray3d::Vec3{s->draft[s->draft.size() - 2], s->draft.back(), s->z};
  ray3d::Vec3 pt{};
  if (!ParseSolidBasePoint(st, line, &pt, log, "REVCLOUD", haveLast ? &last : nullptr))
    return true;
  AddRevcloudCorner(st, pt.x, pt.y, log);
  return true;
}

void SubmitRevcloudViewportPick(AppCommandState& st, float wx, float wy, std::vector<std::string>& log) {
  auto* s = st.sessionAs<stepengine::RevcloudSession>();
  if (!s)
    return;
  if (s->phase == stepengine::RevcloudPhase::WaitObject) {
    ConvertRevcloudObjectAt(st, wx, wy, log);
    return;
  }
  AddRevcloudCorner(st, static_cast<double>(wx), static_cast<double>(wy), log);
}
