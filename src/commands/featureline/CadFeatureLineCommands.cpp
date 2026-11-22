#include "CadCommandsInternal.hpp"

void StartFeatureLineCommand(AppCommandState& st, const std::string& name, std::vector<std::string>& log) {
  // REQ-087 / TASK-082. Drawn like LINE: a click gives X and Y, then the command PROMPTS for the
  // elevation. That is the difference from POLYLINE and 3DPOLY, which take Z from the work plane
  // without asking — right for tracing linework, wrong for a grading design, where every elevation
  // is a decision. Typing X,Y,Z still commits in one go, since it already answers the prompt.
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);  // clears the feature-line draft, including any pending point
  ResetSegmentAngleLock(st);
  st.selectedSurveyPointIndices.clear();
  st.selBoxWaitingSecond = false;
  st.active = AppCommandState::Kind::FeatureLine;
  st.featureLineDraftName = name;
  st.polylineTypedZValid = false;   // shared with the 3DPOLY peel below
  st.polylineTypedZRelative = false;
  log.push_back("FEATURELINE" + (name.empty() ? std::string() : " \"" + name + "\"") +
                " — click a point (you will be asked for its elevation), or type X,Y / X,Y,Z. "
                "E marks the next point an elevation point. CLOSE/CL, END, ESC cancels.");
}

/// Appends one vertex to the feature-line draft. \p isElevPoint marks it an elevation point rather
/// than a PI (ADR-035 (a)) — geometrically it lies on the line either way.
bool SubmitFeatureLineVertex(AppCommandState& st, float x, float y, bool isElevPoint,
                             std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::FeatureLine)
    return false;

  float vz = CadCommitElevation(st);
  if (st.polylineTypedZValid) {
    vz = st.polylineTypedZRelative ? st.anchorZ + st.polylineTypedZ : st.polylineTypedZ;
    st.polylineTypedZValid = false;
    st.polylineTypedZRelative = false;
  }
  st.featureLineDraftVerts.push_back(x);
  st.featureLineDraftVerts.push_back(y);
  st.featureLineDraftVerts.push_back(vz);
  st.featureLineDraftElevPt.push_back(isElevPoint ? 1u : 0u);
  st.anchorX = x;
  st.anchorY = y;
  st.anchorZ = vz;
  char buf[128];
  std::snprintf(buf, sizeof(buf), "FEATURELINE %s %zu at elevation %.3f.",
                isElevPoint ? "elevation point" : "PI",
                st.featureLineDraftElevPt.size(), static_cast<double>(vz));
  log.push_back(buf);
  return true;
}

/// TASK-082: take an X,Y that arrived WITHOUT an elevation — a viewport click, or a typed `X,Y` —
/// and hold it while the command prompts for one.
///
/// This is the difference between FEATURELINE and POLYLINE/3DPOLY. Those take Z from the work plane
/// silently, which is right for linework being traced; a feature line is a grading design, where
/// every elevation is a decision and the user asked to be asked.
bool SubmitFeatureLinePoint(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (st.active != AppCommandState::Kind::FeatureLine)
    return false;

  // ASSUMPTION-1, in order. The snap override is CadCommitElevation's existing REQ-058 rule: an
  // object snap returns the object's real 3D point, and offering anything else as the default would
  // make snapping to a 3D object silently ignore the object. Otherwise the previous vertex, because
  // grading a line means most points sit near the last one.
  float defZ = CadWorkPlaneElevation(st);
  if (st.viewportSnapPickValid)
    defZ = static_cast<float>(st.viewportSnapPickLocalZ);
  else if (st.featureLineDraftVerts.size() >= 3)
    defZ = st.featureLineDraftVerts[st.featureLineDraftVerts.size() - 1];

  st.featureLinePendingPoint = true;
  st.featureLinePendingX = x;
  st.featureLinePendingY = y;
  st.featureLinePendingDefaultZ = defZ;

  char buf[192];
  std::snprintf(buf, sizeof(buf), "FEATURELINE — elevation for %s %zu <%.3f>:",
                st.featureLineNextIsElevPoint ? "elevation point" : "point",
                st.featureLineDraftElevPt.size() + 1, static_cast<double>(defZ));
  log.push_back(buf);
  return true;
}

/// TASK-082: resolve the pending point at \p z and add it to the draft.
void CommitFeatureLinePendingPoint(AppCommandState& st, float z, std::vector<std::string>& log) {
  if (!st.featureLinePendingPoint)
    return;
  const float x = st.featureLinePendingX;
  const float y = st.featureLinePendingY;
  const bool isElevPt = st.featureLineNextIsElevPoint;
  st.featureLinePendingPoint = false;
  st.featureLineNextIsElevPoint = false;
  // Route through SubmitFeatureLineVertex rather than pushing here, so the draft is appended in
  // exactly one place: typed X,Y,Z and click-then-elevation must not be able to disagree about what
  // a vertex is.
  st.polylineTypedZ = z;
  st.polylineTypedZRelative = false;
  st.polylineTypedZValid = true;
  SubmitFeatureLineVertex(st, x, y, isElevPt, log);
}

void CommitFeatureLineDraft(AppCommandState& st, bool closed, std::vector<std::string>& log) {
  const size_t nvert = st.featureLineDraftVerts.size() / 3;
  if (closed ? nvert < 3 : nvert < 2) {
    log.push_back(std::string("FEATURELINE — need at least ") + (closed ? "three" : "two") +
                  " vertices.");
    return;
  }
  PushUndoSnapshot(st, closed ? "Feature line (closed)" : "Feature line");
  if (st.drawing.featureLineOffsets.empty())
    st.drawing.featureLineOffsets.push_back(0);
  const int baseVert = st.drawing.featureLineOffsets.back();
  st.drawing.featureLineVerts.insert(st.drawing.featureLineVerts.end(), st.featureLineDraftVerts.begin(),
                             st.featureLineDraftVerts.end());
  st.drawing.featureLineElevPt.insert(st.drawing.featureLineElevPt.end(), st.featureLineDraftElevPt.begin(),
                              st.featureLineDraftElevPt.end());
  st.drawing.featureLineOffsets.push_back(baseVert + static_cast<int>(nvert));
  st.drawing.featureLineClosed.push_back(static_cast<uint8_t>(closed ? 1 : 0));
  CadFeatureLineInfo info;
  info.name = st.featureLineDraftName;
  st.drawing.featureLineInfo.push_back(std::move(info));
  st.drawing.featureLineAttrs.push_back(
      MakeNewObjectAttrs(st, ObjectLayerKind::FeatureLine, st.drawing.featureLineInfo.back().name));  // REQ-361
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  st.featureLineDraftVerts.clear();
  st.featureLineDraftElevPt.clear();
  st.featureLineDraftName.clear();
  log.push_back(std::string("FEATURELINE ") + (closed ? "closed" : "complete") + " — " +
                std::to_string(nvert) + " vertices.");
}

// --- REQ-088 — feature line elevation editing ---------------------------------------------------
//
// ADR-035 (e): the elevation editor is a VIEW, not a store. Station, length, grade back and grade
// ahead are all derived here on demand; nothing below adds a field to any store, and `internal document payload` is
// untouched. The edits write elevations back into the existing stride-3 vertex array.

namespace {

/// Two points closer than this in plan are the same point. Well inside REQ-101's ±0.01 ft, so a
/// grade over a run this short is reported as undefined rather than as a huge number.
constexpr double kFeatureLinePlanEps = 1e-4;

/// Vertex range [*v0, *v1) of feature line \p fi, with the vertex array proven long enough to
/// address all of it — so every caller below can index without re-checking.
bool FeatureLineRange(const AppCommandState& st, int fi, int* v0, int* v1) {
  if (fi < 0 || (static_cast<size_t>(fi)+ 1) >= st.drawing.featureLineOffsets.size())
    return false;
  const int a = st.drawing.featureLineOffsets[static_cast<size_t>(fi)];
  const int b = st.drawing.featureLineOffsets[(static_cast<size_t>(fi)+ 1)];
  if (b - a < 2 || a < 0 || static_cast<size_t>(b) * 3 > st.drawing.featureLineVerts.size())
    return false;
  *v0 = a;
  *v1 = b;
  return true;
}

} // namespace

bool BuildFeatureLineElevTable(const AppCommandState& st, int fi, std::vector<FeatureLineElevRow>* out) {
  if (!out)
    return false;
  out->clear();
  int v0 = 0, v1 = 0;
  if (!FeatureLineRange(st, fi, &v0, &v1))
    return false;
  const int n = v1 - v0;
  const bool closed = static_cast<size_t>(fi) < st.drawing.featureLineClosed.size() &&
                      st.drawing.featureLineClosed[static_cast<size_t>(fi)] != 0;

  const auto px = [&](int i) { return static_cast<double>(st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ i) * 3]); };
  const auto py = [&](int i) { return static_cast<double>(st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ i) * 3 + 1]); };
  const auto pz = [&](int i) { return st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ i) * 3 + 2]; };
  // PLAN length, not slope length — REQ-088 says stations and lengths agree with the feature line's
  // plan geometry, and grade is rise over the horizontal run (as SURFELEV computes it).
  const auto planLen = [&](int a, int b) { return std::hypot(px(b) - px(a), py(b) - py(a)); };

  const double kNaN = std::numeric_limits<double>::quiet_NaN();
  double station = 0.0;
  for (int i = 0; i < n; ++i) {
    // A closed line's last point continues to the first, and its first point is preceded by the
    // last; an open line's ends simply have no such segment.
    const int next = (i + 1 < n) ? i + 1 : (closed ? 0 : -1);
    const int prev = (i > 0) ? i - 1 : (closed ? n - 1 : -1);

    FeatureLineElevRow r;
    r.vertexIndex = i;
    r.isElevationPoint = (static_cast<size_t>(v0)+ i) < st.drawing.featureLineElevPt.size() &&
                         st.drawing.featureLineElevPt[(static_cast<size_t>(v0)+ i)] != 0;
    r.station = station;
    r.elevation = static_cast<float>(pz(i));
    r.lengthAhead = next >= 0 ? planLen(i, next) : 0.0;
    r.gradeAheadPct = (next >= 0 && r.lengthAhead > kFeatureLinePlanEps)
                          ? (static_cast<double>(pz(next)) - pz(i)) / r.lengthAhead * 100.0
                          : kNaN;
    const double lenBack = prev >= 0 ? planLen(prev, i) : 0.0;
    r.gradeBackPct = (prev >= 0 && lenBack > kFeatureLinePlanEps)
                         ? (static_cast<double>(pz(i)) - pz(prev)) / lenBack * 100.0
                         : kNaN;
    out->push_back(r);
    station += r.lengthAhead;
  }
  return true;
}

namespace {

/// Writable Z of point \p i of feature line \p fi. Callers have already validated the range.
double& FeatureLineZ(AppCommandState& st, int v0, int i) {
  return st.drawing.featureLineVerts[(static_cast<size_t>(v0)+ i) * 3 + 2];
}

/// Everything every elevation edit has to do around the mutation itself, in one place: validate the
/// feature line and the point index, snapshot for undo, then bump the revision.
///
/// The bump is what makes REQ-088's last acceptance condition — "the surface rebuilds with no user
/// action" — true without a line of new surface code: TickSurfaceRebuilds' dirty check is exactly
/// "cadGpuRevision moved", so a feature line used as a breakline (REQ-069) re-triangulates on the
/// next frame. Doing it in one place is also what makes "undoable in one step" uniform across all
/// six edits rather than six chances to forget the snapshot.
///
/// **\p fn must call `commit()` immediately before its first mutation and not before.** The snapshot
/// is deferred this way because several of these edits can only discover they must refuse after
/// computing the table — and PushUndoSnapshot CLEARS THE REDO STACK. Pushing eagerly and popping on
/// refusal would therefore destroy the user's redo history as the price of a rejected keystroke,
/// which is a worse bug than the one it tidies.
template <class Fn>
bool EditFeatureLineElevations(AppCommandState& st, int flNumber, int pointNumber, bool needPoint,
                               const char* undoLabel, std::vector<std::string>& log, Fn&& fn) {
  const int fi = flNumber - 1;  // commands are 1-based; the store is 0-based
  int v0 = 0, v1 = 0;
  if (!FeatureLineRange(st, fi, &v0, &v1)) {
    log.push_back("FLELEV — no feature line " + std::to_string(flNumber) + ".");
    return false;
  }
  const int n = v1 - v0;
  if (needPoint && (pointNumber < 1 || pointNumber > n)) {
    log.push_back("FLELEV — feature line " + std::to_string(flNumber) + " has " + std::to_string(n) +
                  " points; there is no point " + std::to_string(pointNumber) + ".");
    return false;
  }
  bool pushed = false;
  const auto commit = [&] {
    if (!pushed) {
      PushUndoSnapshot(st, undoLabel);
      pushed = true;
    }
  };
  if (!fn(fi, v0, n, commit))
    return false;
  BumpCadGpuCache(st);
  return true;
}

} // namespace

/// REQ-088: set one point's elevation outright.
bool SetFeatureLinePointElevation(AppCommandState& st, int flNumber, int pointNumber, float elevation,
                                  std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, pointNumber, true, "Feature line elevation", log,
      [&](int /*fi*/, int v0, int /*n*/, const auto& commit) {
        commit();
        FeatureLineZ(st, v0, pointNumber - 1) = elevation;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "FLELEV — feature line %d point %d elevation %.3f.",
                      flNumber, pointNumber, static_cast<double>(elevation));
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: set the grade of the segment AHEAD of \p pointNumber, which moves the NEXT point.
///
/// ASSUMPTION-1: a grade edit holds the earlier point and moves the later one. The Acceptance pins
/// this half — "typing a grade ahead moves the next point's elevation and leaves the current one
/// alone" — and GRADEBACK below is the same rule seen from the other end.
bool SetFeatureLineGradeAhead(AppCommandState& st, int flNumber, int pointNumber, double gradePct,
                              std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, pointNumber, true, "Feature line grade ahead", log,
      [&](int fi, int v0, int n, const auto& commit) {
        std::vector<FeatureLineElevRow> rows;
        if (!BuildFeatureLineElevTable(st, fi, &rows))
          return false;
        const int i = pointNumber - 1;
        const bool closed = static_cast<size_t>(fi) < st.drawing.featureLineClosed.size() &&
                            st.drawing.featureLineClosed[static_cast<size_t>(fi)] != 0;
        const int next = (i + 1 < n) ? i + 1 : (closed ? 0 : -1);
        if (next < 0) {
          log.push_back("FLELEV — point " + std::to_string(pointNumber) +
                        " is the last point of an open feature line; it has no grade ahead.");
          return false;
        }
        if (rows[static_cast<size_t>(i)].lengthAhead <= kFeatureLinePlanEps) {
          log.push_back("FLELEV — the segment ahead of point " + std::to_string(pointNumber) +
                        " has no plan length, so a grade cannot set an elevation across it.");
          return false;
        }
        commit();
        FeatureLineZ(st, v0, next) =
            static_cast<float>(static_cast<double>(FeatureLineZ(st, v0, i)) +
                               gradePct / 100.0 * rows[static_cast<size_t>(i)].lengthAhead);
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "FLELEV — feature line %d grade ahead of point %d = %.2f%%; point %d is now %.3f.",
                      flNumber, pointNumber, gradePct, next + 1,
                      static_cast<double>(FeatureLineZ(st, v0, next)));
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: set the grade of the segment BEHIND \p pointNumber, which moves THIS point and holds the
/// previous one — ASSUMPTION-1, and what "updates the downstream elevations" means read directionally.
bool SetFeatureLineGradeBack(AppCommandState& st, int flNumber, int pointNumber, double gradePct,
                             std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, pointNumber, true, "Feature line grade back", log,
      [&](int fi, int v0, int n, const auto& commit) {
        std::vector<FeatureLineElevRow> rows;
        if (!BuildFeatureLineElevTable(st, fi, &rows))
          return false;
        const int i = pointNumber - 1;
        const bool closed = static_cast<size_t>(fi) < st.drawing.featureLineClosed.size() &&
                            st.drawing.featureLineClosed[static_cast<size_t>(fi)] != 0;
        const int prev = (i > 0) ? i - 1 : (closed ? n - 1 : -1);
        if (prev < 0) {
          log.push_back("FLELEV — point " + std::to_string(pointNumber) +
                        " is the first point of an open feature line; it has no grade back.");
          return false;
        }
        const double lenBack = rows[static_cast<size_t>(prev)].lengthAhead;
        if (lenBack <= kFeatureLinePlanEps) {
          log.push_back("FLELEV — the segment behind point " + std::to_string(pointNumber) +
                        " has no plan length, so a grade cannot set an elevation across it.");
          return false;
        }
        commit();
        FeatureLineZ(st, v0, i) = static_cast<float>(
            static_cast<double>(FeatureLineZ(st, v0, prev)) + gradePct / 100.0 * lenBack);
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "FLELEV — feature line %d grade back of point %d = %.2f%%; point %d is now %.3f.",
                      flNumber, pointNumber, gradePct, pointNumber,
                      static_cast<double>(FeatureLineZ(st, v0, i)));
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: "Points may be raised or lowered as a set by a delta." A negative delta lowers.
bool RaiseFeatureLineElevations(AppCommandState& st, int flNumber, float delta,
                                std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, 0, false, "Feature line raise/lower", log, [&](int /*fi*/, int v0, int n, const auto& commit) {
        commit();
        for (int i = 0; i < n; ++i)
          FeatureLineZ(st, v0, i) += delta;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "FLELEV — feature line %d: all %d point(s) %s %.3f.",
                      flNumber, n, delta < 0.f ? "lowered by" : "raised by",
                      std::fabs(static_cast<double>(delta)));
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: insert an elevation point at plan station \p station.
///
/// The plan position is INTERPOLATED along the segment the station falls in, so the new vertex lies
/// exactly on the line by construction — which is why ADR-035 (b)'s drift risk does not arise here.
/// It carries the elevation-point flag, so it is not a PI: it changes the surface without changing
/// the plan shape, which is REQ-088's fourth acceptance condition.
bool InsertFeatureLineElevationPoint(AppCommandState& st, int flNumber, double station, float elevation,
                                     std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, 0, false, "Insert elevation point", log, [&](int fi, int v0, int n, const auto& commit) {
        std::vector<FeatureLineElevRow> rows;
        if (!BuildFeatureLineElevTable(st, fi, &rows))
          return false;
        if (station <= kFeatureLinePlanEps) {
          log.push_back("FLELEV — station must be past the start of the feature line.");
          return false;
        }
        // Find the segment holding the station. The last row's lengthAhead is 0 on an open line, so
        // a station past the end matches nothing and is reported rather than clamped.
        int seg = -1;
        double along = 0.0;
        for (int i = 0; i + 1 < n; ++i) {
          const double s0 = rows[static_cast<size_t>(i)].station;
          const double len = rows[static_cast<size_t>(i)].lengthAhead;
          if (station < s0 + len - kFeatureLinePlanEps) {
            seg = i;
            along = station - s0;
            break;
          }
        }
        if (seg < 0 || along <= kFeatureLinePlanEps) {
          const double total = rows.back().station;
          char buf[200];
          std::snprintf(buf, sizeof(buf),
                        "FLELEV — station %.3f is not inside a segment (the feature line runs 0.000 "
                        "to %.3f, and a station AT an existing point would add nothing).",
                        station, total);
          log.push_back(buf);
          return false;
        }

        const size_t a = (static_cast<size_t>(v0)+ seg) * 3;
        const size_t b = (static_cast<size_t>(v0)+ seg + 1) * 3;
        const double t = along / rows[static_cast<size_t>(seg)].lengthAhead;
        const float nx = static_cast<float>(st.drawing.featureLineVerts[a] +
                                            t * (st.drawing.featureLineVerts[b] - st.drawing.featureLineVerts[a]));
        const float ny = static_cast<float>(st.drawing.featureLineVerts[a + 1] +
                                            t * (st.drawing.featureLineVerts[b + 1] - st.drawing.featureLineVerts[a + 1]));

        // Insert AFTER point `seg`, i.e. at vertex slot v0+seg+1. The three arrays are cut on
        // different strides — verts by 3, flags by 1 — which EraseFeatureLineByIndex already showed
        // is silent when it goes wrong, so both are done here together.
        const int at = v0 + seg + 1;
        commit();
        const float v[3] = {nx, ny, elevation};
        st.drawing.featureLineVerts.insert(st.drawing.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(at) * 3,
                                   v, v + 3);
        if (st.drawing.featureLineElevPt.size() < static_cast<size_t>(at))
          st.drawing.featureLineElevPt.resize(static_cast<size_t>(at), 0);
        st.drawing.featureLineElevPt.insert(st.drawing.featureLineElevPt.begin() + static_cast<std::ptrdiff_t>(at),
                                    static_cast<uint8_t>(1));
        // Every feature line after this one starts a vertex later.
        for (size_t k = static_cast<size_t>(fi) + 1; k < st.drawing.featureLineOffsets.size(); ++k)
          st.drawing.featureLineOffsets[k] += 1;

        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "FLELEV — feature line %d: elevation point added at station %.3f, elevation "
                      "%.3f (now point %d of %d).",
                      flNumber, station, static_cast<double>(elevation), seg + 2, n + 1);
        log.push_back(buf);
        return true;
      });
}

/// REQ-088: delete an elevation point. Refuses a PI — removing one is geometry editing, which is
/// REQ-087's "insert and delete a PI" and is not built. Refusing out loud beats silently deleting
/// a vertex the user thought was only carrying an elevation.
bool DeleteFeatureLineElevationPoint(AppCommandState& st, int flNumber, int pointNumber,
                                     std::vector<std::string>& log) {
  return EditFeatureLineElevations(
      st, flNumber, pointNumber, true, "Delete elevation point", log,
      [&](int fi, int v0, int /*n*/, const auto& commit) {
        const int at = v0 + pointNumber - 1;
        const bool isElevPt = static_cast<size_t>(at) < st.drawing.featureLineElevPt.size() &&
                              st.drawing.featureLineElevPt[static_cast<size_t>(at)] != 0;
        if (!isElevPt) {
          log.push_back("FLELEV — point " + std::to_string(pointNumber) +
                        " is a PI, not an elevation point. Deleting a PI changes the plan shape and "
                        "is not an elevation edit.");
          return false;
        }
        commit();
        st.drawing.featureLineVerts.erase(
            st.drawing.featureLineVerts.begin() + static_cast<std::ptrdiff_t>(at) * 3,
            st.drawing.featureLineVerts.begin() + (static_cast<std::ptrdiff_t>(at)+ 1) * 3);
        st.drawing.featureLineElevPt.erase(st.drawing.featureLineElevPt.begin() + static_cast<std::ptrdiff_t>(at));
        for (size_t k = static_cast<size_t>(fi) + 1; k < st.drawing.featureLineOffsets.size(); ++k)
          st.drawing.featureLineOffsets[k] -= 1;
        log.push_back("FLELEV — feature line " + std::to_string(flNumber) + ": elevation point " +
                      std::to_string(pointNumber) + " deleted.");
        return true;
      });
}
