#include "CadCommandsInternal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static bool AddViewportFromPaperPolyline(AppCommandState& cmd, int layoutIdx, int polylineIndex,
                                         std::vector<std::string>& log) {
  if (layoutIdx < 0 || static_cast<size_t>(layoutIdx) >= cmd.drawing.paperLayouts.size())
    return false;
  PaperLayout& layout = cmd.drawing.paperLayouts[static_cast<size_t>(layoutIdx)];
  if (polylineIndex < 0 || static_cast<size_t>(polylineIndex + 1) >= layout.paperPolyOffsets.size() ||
      static_cast<size_t>(polylineIndex) >= layout.paperPolyClosed.size() ||
      layout.paperPolyClosed[static_cast<size_t>(polylineIndex)] == 0) {
    log.push_back("MVIEW Object — select a closed paper-space polyline.");
    return false;
  }
  const int begin = layout.paperPolyOffsets[static_cast<size_t>(polylineIndex)];
  const int end = layout.paperPolyOffsets[static_cast<size_t>(polylineIndex + 1)];
  if (begin < 0 || end - begin < 2 || static_cast<size_t>(end) * 3 > layout.paperPolyVerts.size()) {
    log.push_back("MVIEW Object — the polyline boundary is invalid.");
    return false;
  }
  Viewport viewport;
  viewport.clipBoundary.reserve(static_cast<size_t>(end - begin));
  float minX = std::numeric_limits<float>::infinity(), minY = minX;
  float maxX = -minX, maxY = -minX;
  double twiceArea = 0.0;
  for (int i = begin; i < end; ++i) {
    const size_t p = static_cast<size_t>(i) * 3;
    const float x = layout.paperPolyVerts[p], y = layout.paperPolyVerts[p + 1];
    const size_t next = static_cast<size_t>(i + 1 < end ? i + 1 : begin) * 3;
    const float nx = layout.paperPolyVerts[next], ny = layout.paperPolyVerts[next + 1];
    const float bulge = static_cast<size_t>(i) < layout.paperPolyVertsBulge.size()
                            ? layout.paperPolyVertsBulge[static_cast<size_t>(i)] : 0.f;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(bulge)) {
      log.push_back("MVIEW Object — the polyline boundary contains a non-finite coordinate.");
      return false;
    }
    viewport.clipBoundary.push_back({x, y, bulge});
    ForEachBulgeChord(x, y, nx, ny, bulge, 32, [&](double ax, double ay, double bx, double by) {
      minX = std::min(minX, static_cast<float>((std::min)(ax, bx)));
      minY = std::min(minY, static_cast<float>((std::min)(ay, by)));
      maxX = std::max(maxX, static_cast<float>((std::max)(ax, bx)));
      maxY = std::max(maxY, static_cast<float>((std::max)(ay, by)));
      twiceArea += ax * by - ay * bx;
    });
  }
  if (maxX - minX < 1.e-4f || maxY - minY < 1.e-4f || std::fabs(twiceArea) < 1.e-8) {
    log.push_back("MVIEW Object — the polyline boundary has no usable area.");
    return false;
  }
  viewport.paperXIn = minX;
  viewport.paperYIn = minY;
  viewport.paperWIn = maxX - minX;
  viewport.paperHIn = maxY - minY;
  viewport.modelCenterX = cmd.drawing.worldDocumentOriginX;
  viewport.modelCenterY = cmd.drawing.worldDocumentOriginY;
  viewport.scaleModelPerPaperIn = cmd.drawing.modelUnitsPerPlottedInch > 1.e-6f ? cmd.drawing.modelUnitsPerPlottedInch : 50.f;
  viewport.annotationScaleName = CadActiveAnnotationScaleName(&cmd.drawing.annotationScales, cmd.currentAnnotationScaleIndex,
                                                                nullptr, cmd.drawing.modelUnitsPerPlottedInch);
  PushUndoSnapshot(cmd, "Create MVIEW object viewport");
  layout.viewports.push_back(std::move(viewport));
  const int idx = static_cast<int>(layout.viewports.size()) - 1;
  cmd.selectedViewportLayout = layoutIdx;
  cmd.selectedViewportIndex = idx;
  cmd.selectedViewports = {idx};
  BumpCadGpuCache(cmd);
  log.push_back("MVIEW Object — viewport created from the closed polyline; source polyline unchanged.");
  return true;
}

static bool CreateMviewPreset(AppCommandState& st, const std::string& option, std::vector<std::string>& log) {
  if (st.activeSpaceIndex < 0 || static_cast<size_t>(st.activeSpaceIndex) >= st.drawing.paperLayouts.size()) {
    log.push_back("MVIEW — switch to a paper layout first (MODEL/PAPER button).");
    return false;
  }
  if (option == "object") {
    st.paperVpMode = 1;
    st.paperVpPhase = 0;
    log.push_back("MVIEW Object — select a closed paper-space polyline (Esc to cancel).");
    return true;
  }
  PaperLayout& layout = st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
  const float width = layout.sheetWidthIn(), height = layout.sheetHeightIn();
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.f || height <= 0.f) {
    log.push_back("MVIEW — the active layout has invalid paper dimensions.");
    return false;
  }
  std::vector<std::array<float, 4>> rects;
  if (option == "fit") rects.push_back({0.f, 0.f, width, height});
  else if (option == "2") {
    rects.push_back({0.f, 0.f, width * .5f, height});
    rects.push_back({width * .5f, 0.f, width, height});
  } else if (option == "3") {
    rects.push_back({0.f, 0.f, width * .5f, height});
    rects.push_back({width * .5f, height * .5f, width, height});
    rects.push_back({width * .5f, 0.f, width, height * .5f});
  } else if (option == "4") {
    rects.push_back({0.f, 0.f, width * .5f, height * .5f});
    rects.push_back({width * .5f, 0.f, width, height * .5f});
    rects.push_back({0.f, height * .5f, width * .5f, height});
    rects.push_back({width * .5f, height * .5f, width, height});
  } else {
    log.push_back("MVIEW — option must be Fit, 2, 3, 4, or Object.");
    return false;
  }
  PushUndoSnapshot(st, "Create MVIEW viewport(s)");
  std::vector<int> created;
  for (const auto& r : rects) {
    const int index = AddViewportRect(st, st.activeSpaceIndex, r[0], r[1], r[2], r[3]);
    if (index >= 0) created.push_back(index);
  }
  st.selectedViewports = created;
  st.selectedViewportIndex = created.empty() ? -1 : created.front();
  st.selectedViewportLayout = created.empty() ? -1 : st.activeSpaceIndex;
  st.paperVpMode = 0;
  st.paperVpPhase = 0;
  stepengine::EndSession(st);
  log.push_back("MVIEW " + (option == "fit" ? std::string("Fit") : option) + " — " +
                std::to_string(created.size()) + " viewport(s) created.");
  return !created.empty();
}


static bool HandlePaperModifyPick(AppCommandState& st, float x, float y, std::vector<std::string>& log);

static bool HasPaperModifyPhase(const AppCommandState& st);

//
// The origin deliberately becomes the FIRST large coordinate's neighbourhood rather than the eventual
// extents centre. Precision only needs the locals to be small, not centred: a 5,000 ft site whose
// origin sits at one corner still stores locals under 5,000, which float represents to ~1e-4 ft. This
// also matches what the DXF importer already does when its header extents are untrustworthy.
// #764 - typed points for the pure-paper-space MOVE / COPY / ROTATE / MIRROR / SCALE gestures, which are
// click-driven state machines (cmd.paper*Phase) and never set cmd.active, so the model-space typed-input
// handlers cannot see them. Paper points are plain paper inches (no document origin, no UCS), and `@dx,dy`
// is relative to the previous point, as in model space. Returns true when it consumed the line; text that is
// not a point (a command name) falls through to normal dispatch, as it always did mid-gesture.
static bool ParsePaperPoint(const std::string& line, bool allowRelative, float baseX, float baseY, float* x,
                            float* y) {
  double px = 0., py = 0.;
  if (!ParseWorldPointD(line, &px, &py, allowRelative, static_cast<double>(baseX), static_cast<double>(baseY)))
    return false;
  if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(static_cast<float>(px)) ||
      !std::isfinite(static_cast<float>(py)))
    return false;
  *x = static_cast<float>(px);
  *y = static_cast<float>(py);
  return true;
}

static bool HandlePaperPhaseText(AppCommandState& st, const std::string& line, std::vector<std::string>& log) {
  const bool paperModify = HasPaperModifyPhase(st);
  if (paperModify && st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    if (st.paperFilletPhase) {
      if (line.empty() && st.paperFilletPhase == 1) { st.paperFilletPhase = 0; return true; }
      if (line.empty()) { log.push_back("FILLET — pick the second object, or ESC to cancel."); return true; }
      return HandleFilletText(st, line, log);
    }
    if (st.paperChamferPhase) {
      if (line.empty() && st.paperChamferPhase == 1) { st.paperChamferPhase = 0; return true; }
      if (line.empty()) { log.push_back("CHAMFER — pick the second object, or ESC to cancel."); return true; }
      return HandleChamferText(st, line, log);
    }
    if (line.empty()) {
      if (st.paperExtendPhase == 1 && !st.paperExtendBoundaries.empty()) {
        st.paperExtendPhase = 2;
        log.push_back("EXTEND — select object to extend, or Enter to finish.");
        return true;
      }
      if (st.paperExtendPhase == 2) {
        st.paperExtendPhase = 0; st.paperExtendBoundaries.clear(); return true;
      }
      if (st.paperBreakPhase == 1) { st.paperBreakPhase = 0; return true; }
      if (st.paperBreakPhase == 2) { log.push_back("BREAK — specify the second break point in the viewport."); return true; }
      if (st.paperLengthenPhase) { st.paperLengthenPhase = 0; return true; }
      if (st.paperStretchPhase) { log.push_back("STRETCH — specify a point, or ESC to cancel."); return true; }
      return false;
    }
    if (st.paperStretchPhase) {
      float x = 0.f, y = 0.f;
      const float bx = st.paperStretchPhase == 2 ? st.paperStretchBaseXIn : 0.f;
      const float by = st.paperStretchPhase == 2 ? st.paperStretchBaseYIn : 0.f;
      if (!ParsePaperPoint(line, st.paperStretchPhase == 2, bx, by, &x, &y)) return false;
      if (st.paperStretchPhase == 1) {
        st.paperStretchBaseXIn = x; st.paperStretchBaseYIn = y; st.paperStretchPhase = 2;
        log.push_back("STRETCH — specify destination point:");
      } else {
        ApplyStretchToPaperSelection(st, x - bx, y - by, log); st.paperStretchPhase = 0;
      }
      return true;
    }
    return false;
  }
  const bool move = st.paperMovePhase != 0, rot = st.paperRotatePhase != 0, mir = st.paperMirrorPhase != 0,
             scl = st.paperScalePhase != 0;
  if (line.empty() || !(move || rot || mir || scl))
    return false;
  // A gesture left half-finished by a space switch must not swallow typed model-space points.
  if (st.activeSpaceIndex == kModelSpaceIndex || InFloatingModelSpace(st))
    return false;
  float x = 0.f, y = 0.f;
  if (move) {
    const char* name = st.paperMoveIsCopy ? "COPY" : "MOVE";
    if (st.paperMovePhase == 1) {
      if (!ParsePaperPoint(line, false, 0.f, 0.f, &x, &y)) {
        return false;
      }
      st.paperMoveBaseXIn = x;
      st.paperMoveBaseYIn = y;
      st.paperMovePhase = 2;
      log.push_back(std::string(name) + " — specify destination (click, X,Y or @dx,dy).");
      return true;
    }
    if (!ParsePaperPoint(line, true, st.paperMoveBaseXIn, st.paperMoveBaseYIn, &x, &y)) {
      return false;
    }
    FinishPaperMoveCopy(st, x - st.paperMoveBaseXIn, y - st.paperMoveBaseYIn, log);
    return true;
  }
  if (rot) {
    if (st.paperRotatePhase == 1) {
      if (!ParsePaperPoint(line, false, 0.f, 0.f, &x, &y)) {
        return false;
      }
      st.paperRotateBaseXIn = x;
      st.paperRotateBaseYIn = y;
      st.paperRotatePhase = 2;
      log.push_back("ROTATE — specify a point that sets the rotation angle (click, X,Y or @dx,dy).");
      return true;
    }
    if (!ParsePaperPoint(line, true, st.paperRotateBaseXIn, st.paperRotateBaseYIn, &x, &y)) {
      return false;
    }
    RotateSelectedPaperEntities(st, st.paperRotateBaseXIn, st.paperRotateBaseYIn,
                                std::atan2(y - st.paperRotateBaseYIn, x - st.paperRotateBaseXIn), log);
    st.paperRotatePhase = 0;
    return true;
  }
  if (mir) {
    if (st.paperMirrorPhase == 1) {
      if (!ParsePaperPoint(line, false, 0.f, 0.f, &x, &y)) {
        return false;
      }
      st.paperMirrorP1XIn = x;
      st.paperMirrorP1YIn = y;
      st.paperMirrorPhase = 2;
      log.push_back("MIRROR — specify second point of mirror line (click, X,Y or @dx,dy).");
      return true;
    }
    if (!ParsePaperPoint(line, true, st.paperMirrorP1XIn, st.paperMirrorP1YIn, &x, &y)) {
      return false;
    }
    FinishPaperMirror(st, x, y, log);
    return true;
  }
  // SCALE
  if (st.paperScalePhase == 1) {
    if (!ParsePaperPoint(line, false, 0.f, 0.f, &x, &y)) {
      return false;
    }
    st.paperScaleBaseXIn = x;
    st.paperScaleBaseYIn = y;
    st.paperScalePhase = 2;
    log.push_back("SCALE — type the scale factor (>0), or pick a point: its distance from the base in paper "
                  "inches is the factor.");
    return true;
  }
  // A point first: ParseOneFloat would read the "3" of "3,4" as a factor.
  float factor = 0.f;
  if (ParsePaperPoint(line, true, st.paperScaleBaseXIn, st.paperScaleBaseYIn, &x, &y)) {
    factor = std::hypot(x - st.paperScaleBaseXIn, y - st.paperScaleBaseYIn);
    if (!(factor > 1e-6f)) {
      log.push_back("SCALE — that point is on the base point; pick a different point or type a factor.");
      return true;
    }
  } else if (ParseOneFloat(line, &factor)) {
    if (!(factor > 0.f) || !std::isfinite(factor)) {
      log.push_back("SCALE — scale factor must be a positive finite number.");
      return true;
    }
  } else {
    return false;  // not a number or point: let a typed command name fall through
  }
  ScaleSelectedPaperEntities(st, st.paperScaleBaseXIn, st.paperScaleBaseYIn, factor, log);
  st.paperScalePhase = 0;
  return true;
}

static bool HasPaperModifyPhase(const AppCommandState& st) {
  return st.paperLengthenPhase || st.paperExtendPhase || st.paperBreakPhase || st.paperStretchPhase ||
         st.paperFilletPhase || st.paperChamferPhase;
}

// REQ-972: paper modify commands keep their phases in AppCommandState while active is None, so

static bool HandlePaperModifyPick(AppCommandState& st, float x, float y, std::vector<std::string>& log) {
  if (!HasPaperModifyPhase(st) || st.activeSpaceIndex < 0 ||
      static_cast<size_t>(st.activeSpaceIndex) >= st.drawing.paperLayouts.size() || InFloatingModelSpace(st))
    return false;
  PaperLayout& layout = st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
  PaperEntityRef hit{};
  const bool picked = PickPaperEntityAt(layout, x, y, std::max(0.12f, CadOffsetEntityPickTolWorld(st)), &hit);
  auto isCurve = [](const PaperEntityRef& ref) {
    return ref.type == PaperEntityRef::Type::Line || ref.type == PaperEntityRef::Type::Circle ||
           ref.type == PaperEntityRef::Type::Arc || ref.type == PaperEntityRef::Type::Ellipse ||
           ref.type == PaperEntityRef::Type::Polyline;
  };

  if (st.paperLengthenPhase) {
    if (!picked) { log.push_back("LENGTHEN — nothing under cursor; try again."); return true; }
    (void)ApplyLengthenToPaperEntity(st, hit, x, y, log);
    return true;
  }
  if (st.paperExtendPhase) {
    if (!picked || !isCurve(hit)) { log.push_back("EXTEND — pick a line, circle, arc, ellipse, or polyline."); return true; }
    if (st.paperExtendPhase == 1) {
      if (std::find_if(st.paperExtendBoundaries.begin(), st.paperExtendBoundaries.end(), [&](const PaperEntityRef& item) {
            return item.type == hit.type && item.index == hit.index;
          }) != st.paperExtendBoundaries.end()) {
        log.push_back("EXTEND — already a boundary edge.");
        return true;
      }
      st.paperExtendBoundaries.push_back(hit);
      log.push_back("EXTEND — boundary edge added. Pick more edges, or Enter when done.");
    } else {
      (void)ApplyExtendToPaperEntity(st, hit, x, y, log);
    }
    return true;
  }
  if (st.paperBreakPhase) {
    if (st.paperBreakPhase == 1) {
      if (!picked || (hit.type != PaperEntityRef::Type::Line && hit.type != PaperEntityRef::Type::Circle &&
                      hit.type != PaperEntityRef::Type::Arc && hit.type != PaperEntityRef::Type::Polyline)) {
        log.push_back("BREAK — pick a line, circle, arc, or polyline."); return true;
      }
      BreakPoint first{};
      if (!ClosestPointOnPaperEntity(layout, hit, x, y, &first)) {
        log.push_back("BREAK — could not resolve a point on that object; try again."); return true;
      }
      st.paperBreakEntity = hit;
      st.paperBreakP1 = first;
      st.paperBreakPhase = 2;
      log.push_back("BREAK — specify second break point:");
    } else {
      if (ApplyBreakToPaperEntity(st, st.paperBreakEntity, st.paperBreakP1, x, y, log)) {
        st.paperBreakPhase = 1;
        st.paperBreakEntity = PaperEntityRef{};
        log.push_back("BREAK — select object (the pick is break point 1), or Enter to finish.");
      }
    }
    return true;
  }
  if (st.paperStretchPhase) {
    if (st.paperStretchPhase == 1) {
      st.paperStretchBaseXIn = x; st.paperStretchBaseYIn = y; st.paperStretchPhase = 2;
      log.push_back("STRETCH — specify destination point:");
    } else {
      ApplyStretchToPaperSelection(st, x - st.paperStretchBaseXIn, y - st.paperStretchBaseYIn, log);
      st.paperStretchPhase = 0;
    }
    return true;
  }
  if (st.paperFilletPhase || st.paperChamferPhase) {
    const bool fillet = st.paperFilletPhase != 0;
    int segment = -1;
    const bool eligible = picked && (fillet ? PaperFilletEligibility(layout, hit, x, y, &segment, log)
                                             : PaperChamferEligibility(layout, hit, x, y, &segment, log));
    if (!eligible) {
      if (!picked) log.push_back(fillet ? "FILLET — no object at pick." : "CHAMFER — no object at pick.");
      return true;
    }
    int& phase = fillet ? st.paperFilletPhase : st.paperChamferPhase;
    PaperEntityRef& first = fillet ? st.paperFilletFirstEntity : st.paperChamferFirstEntity;
    int& firstSegment = fillet ? st.paperFilletFirstPolySeg : st.paperChamferFirstPolySeg;
    float& firstX = fillet ? st.paperFilletFirstPickX : st.paperChamferFirstPickX;
    float& firstY = fillet ? st.paperFilletFirstPickY : st.paperChamferFirstPickY;
    if (phase == 1) {
      first = hit; firstSegment = segment; firstX = x; firstY = y; phase = 2;
      log.push_back(fillet ? "FILLET — select second object:" : "CHAMFER — select second object:");
    } else {
      if (fillet) (void)ApplyFilletToPaperEntities(st, first, firstSegment, firstX, firstY, hit, segment, x, y, log);
      else (void)ApplyChamferToPaperEntities(st, first, firstSegment, firstX, firstY, hit, segment, x, y, log);
      phase = 1;
      log.push_back(fillet ? "FILLET — select first object or Enter to finish." : "CHAMFER — select first object or Enter to finish.");
    }
    return true;
  }
  return false;
}
