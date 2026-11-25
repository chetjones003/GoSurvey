#include "CadCommandsInternal.hpp"
#include "modify/CadModifySessions.hpp"
#include "engine/StepEngine.hpp"
#include "ProjectWarnings.hpp"
#include "CadBlocks.hpp"
#include "CadCoordinateFrame.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// entities (REQ-038 #5). Caller pushes the undo snapshot.
static void CommitPasteIntoModel(AppCommandState& st, double dx, double dy) {
  const CadClipboard& cb = st.clipboard;
  using ST = SelectedEntity::Type;
  st.selection.clear();

  for (size_t i = 0; i < cb.content.lineCount(); ++i) {
    const CadLineRecord ln = cb.content.line(i);
    st.drawing.pushLine(CadLineRecord{ln.x0 + dx, ln.y0 + dy, ln.z0, ln.x1 + dx, ln.y1 + dy, ln.z1});
    st.drawing.userLineAttrs.push_back(cb.content.userLineAttrs[i]);
    st.selection.push_back({ST::LineSeg, static_cast<int>(st.drawing.userLineAttrs.size()) - 1});
  }
  // Clipboard → model: both are cx,cy,z,r, so Z carries through a paste unchanged (REQ-057).
  for (size_t i = 0; i < cb.content.circleCount(); ++i) {
    const CadCircleRecord c = cb.content.circle(i);
    st.drawing.pushCircle(CadCircleRecord{c.cx + dx, c.cy + dy, c.cz, c.r});
    st.drawing.userCircleAttrs.push_back(cb.content.userCircleAttrs[i]);
    float pnx = 0.f, pny = 0.f, pnz = 1.f;
    CircleNormalAt(cb.content.userCircleNormals, i, &pnx, &pny, &pnz);
    PushCircleNormal(st.drawing.userCircleNormals, pnx, pny, pnz);
    st.selection.push_back({ST::Circle, static_cast<int>(st.drawing.userCircleAttrs.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.userArcs.size(); ++i) {
    CadArc a = cb.content.userArcs[i];
    a.cx += dx;
    a.cy += dy;
    st.drawing.userArcs.push_back(a);
    st.drawing.userArcAttrs.push_back(cb.content.userArcAttrs[i]);
    st.selection.push_back({ST::Arc, static_cast<int>(st.drawing.userArcs.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.userEllipses.size(); ++i) {
    CadEllipse el = cb.content.userEllipses[i];
    el.cx += dx;
    el.cy += dy;
    st.drawing.userEllipses.push_back(el);
    st.drawing.userEllAttrs.push_back(cb.content.userEllAttrs[i]);
    st.selection.push_back({ST::Ellipse, static_cast<int>(st.drawing.userEllipses.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.cadSplines.size(); ++i) {  // REQ-443
    CadSpline sp = cb.content.cadSplines[i];
    spline::MapPoints(&sp, [&](const spline::Vec3& p) { return spline::Vec3{p.x + dx, p.y + dy, p.z}; });
    st.drawing.cadSplines.push_back(std::move(sp));
    st.drawing.cadSplineAttrs.push_back(i < cb.content.cadSplineAttrs.size() ? cb.content.cadSplineAttrs[i] : EntityAttributes{});
    st.selection.push_back({ST::Spline, static_cast<int>(st.drawing.cadSplines.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.cadPoints.size(); ++i) {
    CadPoint p = cb.content.cadPoints[i];
    p.x += dx;
    p.y += dy;
    st.drawing.cadPoints.push_back(p);
    st.drawing.cadPointAttrs.push_back(i < cb.content.cadPointAttrs.size() ? cb.content.cadPointAttrs[i] : EntityAttributes{});
    st.selection.push_back({ST::Point, static_cast<int>(st.drawing.cadPoints.size()) - 1});
  }
  const int nPoly = static_cast<int>(cb.content.userPolylineOffsets.size()) - 1;
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = cb.content.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = cb.content.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const int nv = v1 - v0;
    if (nv < 2)
      continue;
    const int baseVert = st.drawing.userPolylineOffsets.empty() ? 0 : st.drawing.userPolylineOffsets.back();
    for (int vi = v0; vi < v1; ++vi) {
      st.drawing.pushVertex(CadPolyVertex{cb.content.vertex(vi).x + dx, cb.content.vertex(vi).y + dy, cb.content.vertex(vi).z});
      st.drawing.userPolylineVertsBulge.push_back(vi < static_cast<int>(cb.content.userPolylineVertsBulge.size())  // REQ-316 / ADR-047
                                              ? cb.content.userPolylineVertsBulge[static_cast<size_t>(vi)]
                                              : 0.0f);
    }
    if (st.drawing.userPolylineOffsets.empty())
      st.drawing.userPolylineOffsets.push_back(baseVert);
    st.drawing.userPolylineOffsets.push_back(baseVert + nv);
    uint8_t cl = (static_cast<size_t>(pi) < cb.content.userPolylineClosed.size()) ? cb.content.userPolylineClosed[static_cast<size_t>(pi)] : 0u;
    st.drawing.userPolylineClosed.push_back(cl);
    st.drawing.userPolylineAttrs.push_back(cb.content.userPolylineAttrs[static_cast<size_t>(pi)]);
    st.selection.push_back({ST::Polyline, static_cast<int>(st.drawing.userPolylineOffsets.size()) - 2});
  }
  for (size_t i = 0; i < cb.content.cadAnnotations.size(); ++i) {
    CadAnnotation a = cb.content.cadAnnotations[i];
    a.surveyPointLabelForId = -1;  // strip survey link so pasted labels are independent
    a.surveyLabelHasUserOffset = false;
    a.insX += dx;
    a.insY += dy;
    if (a.kind == CadAnnotation::Kind::Mtext) {
      a.boxMinX += dx; a.boxMinY += dy;
      a.boxMaxX += dx; a.boxMaxY += dy;
    } else if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      a.dimExt1X += dx; a.dimExt1Y += dy;
      a.dimExt2X += dx; a.dimExt2Y += dy;
    } else if (a.kind == CadAnnotation::Kind::DimAngular) {
      a.dimAngVertexX += dx; a.dimAngVertexY += dy;
      a.dimExt1X += dx; a.dimExt1Y += dy;
      a.dimExt2X += dx; a.dimExt2Y += dy;
    }
    // Cross-space height (paper→model): paper text height is in paper inches, which transfer 1:1 to model units;
    // model text stores plotted-inches (× modelUnitsPerPlottedInch = model units), so divide by the factor.
    if (cb.fromPaper)
      a.plottedHeightInches /= std::max(st.drawing.modelUnitsPerPlottedInch, 1.e-6f);
    st.drawing.cadAnnotations.push_back(std::move(a));
    st.drawing.cadAnnotationAttrs.push_back(cb.content.cadAnnotationAttrs[i]);
    st.selection.push_back({ST::Annotation, static_cast<int>(st.drawing.cadAnnotations.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.cadTables.size(); ++i) {
    CadTable t = cb.content.cadTables[i];
    CadTableTranslate(&t, dx, dy);
    if (cb.fromPaper)
      t.plottedHeightInches /= std::max(st.drawing.modelUnitsPerPlottedInch, 1.e-6f);
    st.drawing.cadTables.push_back(std::move(t));
    st.drawing.cadTableAttrs.push_back(cb.content.cadTableAttrs[i]);
    st.selection.push_back({ST::Table, static_cast<int>(st.drawing.cadTables.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.cadBlockRefs.size(); ++i) {
    CadBlockRef r = cb.content.cadBlockRefs[i];
    CadBlockTranslate(&r, dx, dy, 0.f);
    st.drawing.cadBlockRefs.push_back(std::move(r));
    st.drawing.cadBlockRefAttrs.push_back(cb.content.cadBlockRefAttrs[i]);
    st.selection.push_back({ST::BlockRef, static_cast<int>(st.drawing.cadBlockRefs.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.cadFilledRegions.size(); ++i) {  // solid fills — now selectable (REQ-042)
    CadFilledRegion fr = cb.content.cadFilledRegions[i];
    // Model-space paste: offset X/Y and carry the clipboard's Z through unchanged (REQ-057) —
    // paste must not flatten elevation.
    for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3) {
      fr.vertsXyz[v] += dx;
      fr.vertsXyz[v + 1] += dy;
    }
    st.drawing.cadFilledRegions.push_back(std::move(fr));
    st.drawing.cadFilledRegionAttrs.push_back(cb.content.cadFilledRegionAttrs[i]);
    st.selection.push_back({ST::FilledRegion, static_cast<int>(st.drawing.cadFilledRegions.size()) - 1});
  }
  BumpCadGpuCache(st);
}

// Paste clipboard geometry into a PAPER layout's store with (dx, dy) applied (paper inches). Builds
// st.selectedPaperEntities (REQ-038 #5). Returns the count of non-text annotations skipped (paper holds
// only Text/Mtext). Caller pushes the undo snapshot. ADR-013: cross-space is an explicit 1:1 transfer.
static int CommitPasteIntoPaper(AppCommandState& st, PaperLayout& L, float dx, float dy) {
  const CadClipboard& cb = st.clipboard;
  using PT = PaperEntityRef::Type;
  st.selectedPaperEntities.clear();
  int skipped = 0;

  for (size_t i = 0; i < cb.content.lineCount(); ++i) {
    const CadLineRecord ln = cb.content.line(i);
    L.paperLines.push_back(static_cast<float>(ln.x0 + dx));
    L.paperLines.push_back(static_cast<float>(ln.y0 + dy));
    L.paperLines.push_back(static_cast<float>(ln.z0));
    L.paperLines.push_back(static_cast<float>(ln.x1 + dx));
    L.paperLines.push_back(static_cast<float>(ln.y1 + dy));
    L.paperLines.push_back(static_cast<float>(ln.z1));
    L.paperLineAttrs.push_back(cb.content.userLineAttrs[i]);
    st.selectedPaperEntities.push_back({PT::Line, static_cast<int>(L.paperLines.size() / 6) - 1});
  }
  // Clipboard (cx,cy,z,r) → paper (cx,cy,r): Z is DROPPED at the sheet boundary, not carried.
  // A sheet is 2D (ADR-025 (g)), so an elevated model circle pasted onto paper lands flat —
  // deliberate and visible, rather than silently keeping an elevation paper cannot represent.
  for (size_t i = 0; i < cb.content.circleCount(); ++i) {
    const CadCircleRecord c = cb.content.circle(i);
    L.paperCircles.push_back(static_cast<float>(c.cx + dx));
    L.paperCircles.push_back(static_cast<float>(c.cy + dy));
    L.paperCircles.push_back(static_cast<float>(c.r));  // radius — the Z is discarded at the sheet
    L.paperCircleAttrs.push_back(cb.content.userCircleAttrs[i]);
    st.selectedPaperEntities.push_back({PT::Circle, static_cast<int>(L.paperCircles.size() / 3) - 1});
  }
  for (size_t i = 0; i < cb.content.userArcs.size(); ++i) {
    CadArc a = cb.content.userArcs[i];
    a.cx += dx;
    a.cy += dy;
    L.paperArcs.push_back(a);
    L.paperArcAttrs.push_back(cb.content.userArcAttrs[i]);
    st.selectedPaperEntities.push_back({PT::Arc, static_cast<int>(L.paperArcs.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.userEllipses.size(); ++i) {
    CadEllipse el = cb.content.userEllipses[i];
    el.cx += dx;
    el.cy += dy;
    L.paperEllipses.push_back(el);
    L.paperEllAttrs.push_back(cb.content.userEllAttrs[i]);
    st.selectedPaperEntities.push_back({PT::Ellipse, static_cast<int>(L.paperEllipses.size()) - 1});
  }
  const int nPoly = static_cast<int>(cb.content.userPolylineOffsets.size()) - 1;
  for (int pi = 0; pi < nPoly; ++pi) {
    const int v0 = cb.content.userPolylineOffsets[static_cast<size_t>(pi)];
    const int v1 = cb.content.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
    const int nv = v1 - v0;
    if (nv < 2)
      continue;
    const int baseVert = L.paperPolyOffsets.empty() ? 0 : L.paperPolyOffsets.back();
    for (int vi = v0; vi < v1; ++vi) {
      L.paperPolyVerts.push_back(static_cast<float>(cb.content.vertex(vi).x + dx));
      L.paperPolyVerts.push_back(static_cast<float>(cb.content.vertex(vi).y + dy));
      L.paperPolyVerts.push_back(static_cast<float>(cb.content.vertex(vi).z));
      L.paperPolyVertsBulge.push_back(static_cast<size_t>(vi) < cb.content.userPolylineVertsBulge.size() ? cb.content.userPolylineVertsBulge[static_cast<size_t>(vi)]
                                                                                       : 0.f);
    }
    if (L.paperPolyOffsets.empty())
      L.paperPolyOffsets.push_back(baseVert);
    L.paperPolyOffsets.push_back(baseVert + nv);
    uint8_t cl = (static_cast<size_t>(pi) < cb.content.userPolylineClosed.size()) ? cb.content.userPolylineClosed[static_cast<size_t>(pi)] : 0u;
    L.paperPolyClosed.push_back(cl);
    L.paperPolyAttrs.push_back(cb.content.userPolylineAttrs[static_cast<size_t>(pi)]);
    st.selectedPaperEntities.push_back({PT::Polyline, static_cast<int>(L.paperPolyOffsets.size()) - 2});
  }
  for (size_t i = 0; i < cb.content.cadAnnotations.size(); ++i) {
    const CadAnnotation& src = cb.content.cadAnnotations[i];
    if (src.kind != CadAnnotation::Kind::Text && src.kind != CadAnnotation::Kind::Mtext) {
      ++skipped;  // dimensions have no paper store (ADR-013 scope); reported by the caller, not dropped silently.
      continue;
    }
    CadAnnotation a = src;
    a.surveyPointLabelForId = -1;
    a.surveyLabelHasUserOffset = false;
    a.insX += dx;
    a.insY += dy;
    if (a.kind == CadAnnotation::Kind::Mtext) {
      a.boxMinX += dx; a.boxMinY += dy;
      a.boxMaxX += dx; a.boxMaxY += dy;
    }
    // Cross-space height: a model annotation's world height is plottedHeightInches × modelUnitsPerPlottedInch
    // (model units), which transfers 1:1 to paper inches. Paper text uses plottedHeightInches AS paper inches,
    // so scale it up by the factor. (Paper→paper keeps the value.)
    if (!cb.fromPaper)
      a.plottedHeightInches *= std::max(st.drawing.modelUnitsPerPlottedInch, 1.e-6f);
    L.paperTexts.push_back(std::move(a));
    L.paperTextAttrs.push_back(cb.content.cadAnnotationAttrs[i]);
    st.selectedPaperEntities.push_back({PT::Text, static_cast<int>(L.paperTexts.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.cadFilledRegions.size(); ++i) {  // solid fills onto the sheet (not selectable)
    CadFilledRegion fr = cb.content.cadFilledRegions[i];
    for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3) {
      fr.vertsXyz[v] += dx;
      fr.vertsXyz[v + 1] += dy;
      fr.vertsXyz[v + 2] = 0.f;  // landing on a sheet: paper space is 2D (ADR-025 (g))
    }
    L.paperFilledRegions.push_back(std::move(fr));
    L.paperFilledRegionAttrs.push_back(cb.content.cadFilledRegionAttrs[i]);
  }
  for (size_t i = 0; i < cb.content.cadBlockRefs.size(); ++i) {
    CadBlockRef r = cb.content.cadBlockRefs[i];
    CadBlockTranslate(&r, dx, dy, 0.f);
    L.paperBlockRefs.push_back(std::move(r));
    L.paperBlockRefAttrs.push_back(i < cb.content.cadBlockRefAttrs.size() ? cb.content.cadBlockRefAttrs[i] : EntityAttributes{});
    st.selectedPaperEntities.push_back({PT::Block, static_cast<int>(L.paperBlockRefs.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.cadSplines.size(); ++i) {
    CadSpline sp = cb.content.cadSplines[i];
    spline::MapPoints(&sp, [&](const spline::Vec3& p) { return spline::Vec3{p.x + dx, p.y + dy, 0.0}; });
    L.paperSplines.push_back(std::move(sp));
    L.paperSplineAttrs.push_back(i < cb.content.cadSplineAttrs.size() ? cb.content.cadSplineAttrs[i] : EntityAttributes{});
    st.selectedPaperEntities.push_back({PT::Spline, static_cast<int>(L.paperSplines.size()) - 1});
  }
  for (size_t i = 0; i < cb.content.cadPoints.size(); ++i) {
    CadPoint p = cb.content.cadPoints[i];
    p.x += dx;
    p.y += dy;
    p.z = 0.0;
    L.paperPoints.push_back(p);
    L.paperPointAttrs.push_back(i < cb.content.cadPointAttrs.size() ? cb.content.cadPointAttrs[i] : EntityAttributes{});
    st.selectedPaperEntities.push_back({PT::Point, static_cast<int>(L.paperPoints.size()) - 1});
  }
  BumpCadGpuCache(st);
  return skipped;
}



// Paste clipboard geometry into the ACTIVE space (model or the active paper layout) with (dx, dy) applied.
// Routes by ActivePaperGeometryTarget (ADR-009/013). Caller pushes the undo snapshot.

static void CommitPasteFromClipboard(AppCommandState& st, double dx, double dy, std::vector<std::string>& log) {
  // A pasted entity is a *different* entity and must not inherit its source's identity (REQ-076).
  // Cleared on the clipboard rather than on the destination arrays because this is the one place
  // both paste paths (PASTE and PASTEORIG) and both destination spaces pass through; the next
  // EnsureEntityIds then mints fresh ids. Re-clearing on a repeat paste is a harmless no-op.
  ClearEntityIdsFrom(st.clipboard.content.userLineAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.content.userCircleAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.content.userArcAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.content.userEllAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.content.userPolylineAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.content.cadAnnotationAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.content.cadFilledRegionAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.content.cadSplineAttrs, 0);  // REQ-443: pasted spline receives fresh identity.
  ClearEntityIdsFrom(st.clipboard.content.cadPointAttrs, 0);
  ClearEntityIdsFrom(st.clipboard.content.cadBlockRefAttrs, 0);
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    st.selection.clear();  // crossing into paper: model selection no longer applies
    const int skipped = CommitPasteIntoPaper(st, *L, static_cast<float>(dx), static_cast<float>(dy));
    if (skipped > 0)
      log.push_back("PASTE — " + std::to_string(skipped) +
                    " object(s) skipped (paper space does not store dimensions).");
    if (!st.clipboard.content.surveyPoints.empty())
      log.push_back("PASTE — " + std::to_string(st.clipboard.content.surveyPoints.size()) +
                    " survey point(s) not pasted: survey points belong to model space.");
  } else {
    ClearPaperEntitySelection(st);
    CommitPasteIntoModel(st, dx, dy);
    st.selectedSurveyPointIndices.clear();
    if (ActiveProjectPointsReadOnly(st) && !st.clipboard.content.surveyPoints.empty()) {
      log.push_back("PASTE — survey points not pasted: this project is open read-only.");
    } else {
      for (const SurveyPoint& source : st.clipboard.content.surveyPoints) {
        SurveyPoint copy = source;
        copy.easting += dx;
        copy.northing += dy;
        auto existing = std::find_if(st.drawing.surveyPoints.begin(), st.drawing.surveyPoints.end(),
                                     [&](const SurveyPoint& p) { return p.id == copy.id; });
        if (st.clipboardPasteOverwrite && existing != st.drawing.surveyPoints.end()) {
          const size_t index = static_cast<size_t>(existing - st.drawing.surveyPoints.begin());
          copy.labelMtextAnnId = existing->labelMtextAnnId;
          *existing = std::move(copy);
          EnsureSurveyPointLabelMtext(st, index, &log);
          st.selectedSurveyPointIndices.push_back(static_cast<int>(index));
        } else {
          copy.labelMtextAnnId = 0;
          st.drawing.surveyPoints.push_back(std::move(copy));
          EnsureSurveyPointLabelMtext(st, st.drawing.surveyPoints.size() - 1, &log);
          st.selectedSurveyPointIndices.push_back(static_cast<int>(st.drawing.surveyPoints.size() - 1));
        }
      }
      if (!st.clipboard.content.surveyPoints.empty())
        log.push_back("PASTE — " + std::to_string(st.clipboard.content.surveyPoints.size()) + " survey point(s) pasted.");
    }
    st.clipboardPasteOverwrite = false;
  }
}


// (world for model, paper inches for a paper layout). Used by the model pick path and the paper overlay click.
// Calls the file-local CommitPasteFromClipboard (visible here via the anonymous namespace's using-directive).
void CommitClipboardPasteAt(AppCommandState& st, double x, double y, std::vector<std::string>& log) {
  if (st.clipboard.empty())
    return;
  const double dx = x - st.clipboard.basePtX;
  const double dy = y - st.clipboard.basePtY;
  if (!st.clipboardPasteResolving && !st.clipboard.content.surveyPoints.empty() &&
      !ActivePaperGeometryTarget(st) && !ActiveProjectPointsReadOnly(st)) {
    std::unordered_set<int> destinationIds;
    for (const SurveyPoint& point : st.drawing.surveyPoints)
      destinationIds.insert(point.id);
    const int tab = st.activeDrawingIdx;
    if (tab > 0 && tab < static_cast<int>(st.drawingTabs.size())) {
      const uint32_t projectUid = st.drawingTabs[static_cast<size_t>(tab)].projectUid;
      if (projectUid != 0 && st.drawingTabs[static_cast<size_t>(tab)].pointsMode !=
                                 AppCommandState::DrawingTab::PointsMode::Detached)
        for (const auto& project : st.openProjects)
          if (project.uid == projectUid && project.points)
            for (const auto& entry : project.points->points)
              destinationIds.insert(entry.point.id);
    }
    std::vector<int> conflicts;
    for (const SurveyPoint& point : st.clipboard.content.surveyPoints)
      if (destinationIds.count(point.id))
        conflicts.push_back(point.id);
    if (!conflicts.empty()) {
      st.pointEditPrompt = {};
      st.pointEditPrompt.active = true;
      st.pointEditPrompt.clipboardPaste = true;
      st.pointEditPrompt.tabIdx = st.activeDrawingIdx;
      st.pointEditPrompt.conflicts = std::move(conflicts);
      st.pointEditPrompt.projectUid = tab > 0 && tab < static_cast<int>(st.drawingTabs.size())
                                          ? st.drawingTabs[static_cast<size_t>(tab)].projectUid : 0;
      st.pointEditPrompt.projectName = "clipboard paste";
      st.clipboardPasteResolving = false;
      st.pendingCopyDx = dx;
      st.pendingCopyDy = dy;
      stepengine::EndSession(st);
      log.push_back("PASTE — point number conflict; choose overwrite, renumber, or cancel.");
      return;
    }
  }
  st.clipboardPasteResolving = false;
  PushUndoSnapshot(st, st.clipboardPasteOriginal ? "Paste original" : "Paste");
  CommitPasteFromClipboard(st, dx, dy, log);
  stepengine::EndSession(st);
  if (st.clipboardPasteOriginal) {
    log.push_back("PASTEORIG — clipboard contents pasted at original coordinates.");
    st.clipboardPasteOriginal = false;
  } else {
    log.push_back("PASTE complete.");
  }
}

void ResolveClipboardPastePointConflict(AppCommandState& st, AppCommandState::PointEditPrompt::Answer answer,
                                        std::vector<std::string>& log) {
  if (!st.pointEditPrompt.active || !st.pointEditPrompt.clipboardPaste)
    return;
  const std::vector<int> conflicts = st.pointEditPrompt.conflicts;
  st.pointEditPrompt = {};
  if (answer == AppCommandState::PointEditPrompt::Answer::Cancel) {
    st.clipboardPasteOriginal = false;
    log.push_back("PASTE — cancelled; the drawing and clipboard are unchanged.");
    return;
  }
  std::vector<SurveyPoint> originalPoints = st.clipboard.content.surveyPoints;
  if (answer == AppCommandState::PointEditPrompt::Answer::Renumber) {
    std::unordered_set<int> used;
    for (const SurveyPoint& point : st.drawing.surveyPoints) used.insert(point.id);
    const int tab = st.activeDrawingIdx;
    if (tab > 0 && tab < static_cast<int>(st.drawingTabs.size())) {
      const uint32_t uid = st.drawingTabs[static_cast<size_t>(tab)].projectUid;
      if (uid != 0 && st.drawingTabs[static_cast<size_t>(tab)].pointsMode !=
                          AppCommandState::DrawingTab::PointsMode::Detached)
        for (const auto& project : st.openProjects)
          if (project.uid == uid && project.points)
            for (const auto& entry : project.points->points) used.insert(entry.point.id);
    }
    int next = st.drawing.createPointsNextId - 1;
    for (const int id : used) next = std::max(next, id);
    ++next;
    for (SurveyPoint& point : st.clipboard.content.surveyPoints) {
      if (std::find(conflicts.begin(), conflicts.end(), point.id) == conflicts.end()) continue;
      point.id = next;
      used.insert(next++);
      point.labelMtextAnnId = 0;
    }
    st.drawing.createPointsNextId = std::max(st.drawing.createPointsNextId, next);
  } else if (answer == AppCommandState::PointEditPrompt::Answer::Proceed) {
    st.clipboardPasteOverwrite = true;
  }
  st.clipboardPasteResolving = true;
  const double x = st.clipboard.basePtX + st.pendingCopyDx;
  const double y = st.clipboard.basePtY + st.pendingCopyDy;
  CommitClipboardPasteAt(st, x, y, log);
  st.clipboard.content.surveyPoints = std::move(originalPoints);
  if (answer == AppCommandState::PointEditPrompt::Answer::Proceed) {
    const int tab = st.activeDrawingIdx;
    if (tab > 0 && tab < static_cast<int>(st.drawingTabs.size()) &&
        st.drawingTabs[static_cast<size_t>(tab)].projectUid != 0 &&
        st.drawingTabs[static_cast<size_t>(tab)].pointsMode != AppCommandState::DrawingTab::PointsMode::Detached) {
      st.pointEditPrompt.active = true;
      st.pointEditPrompt.projectUid = st.drawingTabs[static_cast<size_t>(tab)].projectUid;
      st.pointEditPrompt.tabIdx = tab;
      st.pointEditPrompt.conflicts = conflicts;
      st.pointEditPrompt.conflictAnswer = AppCommandState::PointEditPrompt::Answer::Proceed;
    }
  }
}

// Append filled regions fully enclosed by [mnX,mxX]×[mnY,mxY] to the clipboard (REQ-038 addendum). Fills are
// not an independently selectable entity, so a copy carries the fills inside the selection's bounding box.
static void CopyEnclosedFilledRegions(CadClipboard& cb, const std::vector<CadFilledRegion>& regions,
                                      const std::vector<EntityAttributes>& attrs, float mnX, float mnY, float mxX,
                                      float mxY, const std::set<int>* skipIdx = nullptr) {
  if (mnX > mxX)
    return;
  for (size_t i = 0; i < regions.size(); ++i) {
    if (skipIdx && skipIdx->count(static_cast<int>(i)))
      continue;  // already copied as a directly-selected fill (REQ-042)
    const CadFilledRegion& fr = regions[i];
    if (fr.vertsXyz.size() < 9)  // < 3 vertices × 3 floats
      continue;
    bool inside = true;
    // Enclosure is tested in plan (X/Y) only — the selection box is a 2D window (REQ-038 addendum).
    for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3)
      if (fr.vertsXyz[v] < mnX || fr.vertsXyz[v] > mxX || fr.vertsXyz[v + 1] < mnY ||
          fr.vertsXyz[v + 1] > mxY) {
        inside = false;
        break;
      }
    if (!inside)
      continue;
    cb.content.cadFilledRegions.push_back(fr);
    cb.content.cadFilledRegionAttrs.push_back(i < attrs.size() ? attrs[i] : EntityAttributes{});
  }
}

// Copy the active paper layout's selected entities into st.clipboard (REQ-038, ADR-013). Coordinates are
// paper inches; they enter the clipboard verbatim and a later paste applies the 1:1 transfer.
static void CopyPaperSelectionToClipboard(AppCommandState& st, PaperLayout& L, std::vector<std::string>& log) {
  if (st.selectedPaperEntities.empty()) {
    log.push_back("COPYCLIP — nothing selected. Select paper objects first.");
    return;
  }
  CadClipboard& cb = st.clipboard;
  cb = CadClipboard{};
  TagClipboardOrigin(st);  // REQ-383
  cb.fromPaper = true;
  float mnX = 1e30f, mnY = 1e30f, mxX = -1e30f, mxY = -1e30f;
  auto expandBbox = [&](float x, float y) {
    mnX = std::min(mnX, x); mnY = std::min(mnY, y);
    mxX = std::max(mxX, x); mxY = std::max(mxY, y);
  };
  auto attrAt = [](const std::vector<EntityAttributes>& v, int i) {
    return (i >= 0 && static_cast<size_t>(i) < v.size()) ? v[static_cast<size_t>(i)] : EntityAttributes{};
  };
  for (const PaperRef& r : st.selectedPaperEntities) {
    switch (r.type) {
    case PaperRef::Type::Line: {
      const size_t k = static_cast<size_t>(r.index) * 6;
      if (k + 5 >= L.paperLines.size())
        break;
      for (int j = 0; j < 6; ++j)
        cb.content.userLinesFlat.push_back(L.paperLines[k + static_cast<size_t>(j)]);
      cb.content.userLineAttrs.push_back(attrAt(L.paperLineAttrs, r.index));
      expandBbox(L.paperLines[k], L.paperLines[k + 1]);
      expandBbox(L.paperLines[k + 3], L.paperLines[k + 4]);
      break;
    }
    case PaperRef::Type::Circle: {
      const size_t k = static_cast<size_t>(r.index) * 3;
      if (k + 2 >= L.paperCircles.size())
        break;
      // Paper (cx,cy,r) → clipboard (cx,cy,z,r): a sheet has no elevation, so Z enters as 0.
      cb.content.pushCircle(CadCircleRecord{L.paperCircles[k], L.paperCircles[k + 1], 0.0, L.paperCircles[k + 2]});  // radius last
      cb.content.userCircleAttrs.push_back(attrAt(L.paperCircleAttrs, r.index));
      PushCircleNormal(cb.content.userCircleNormals);   // a sheet is 2D (ADR-025 (g)): the plane is world XY
      expandBbox(L.paperCircles[k], L.paperCircles[k + 1]);
      break;
    }
    case PaperRef::Type::Arc: {
      if (static_cast<size_t>(r.index) >= L.paperArcs.size())
        break;
      cb.content.userArcs.push_back(L.paperArcs[static_cast<size_t>(r.index)]);
      cb.content.userArcAttrs.push_back(attrAt(L.paperArcAttrs, r.index));
      expandBbox(static_cast<float>(L.paperArcs[static_cast<size_t>(r.index)].cx), static_cast<float>(L.paperArcs[static_cast<size_t>(r.index)].cy));
      break;
    }
    case PaperRef::Type::Ellipse: {
      if (static_cast<size_t>(r.index) >= L.paperEllipses.size())
        break;
      cb.content.userEllipses.push_back(L.paperEllipses[static_cast<size_t>(r.index)]);
      cb.content.userEllAttrs.push_back(attrAt(L.paperEllAttrs, r.index));
      expandBbox(static_cast<float>(L.paperEllipses[static_cast<size_t>(r.index)].cx), static_cast<float>(L.paperEllipses[static_cast<size_t>(r.index)].cy));
      break;
    }
    case PaperRef::Type::Polyline: {
      const int pi = r.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= L.paperPolyOffsets.size())
        break;
      const int v0 = L.paperPolyOffsets[static_cast<size_t>(pi)];
      const int v1 = L.paperPolyOffsets[(static_cast<size_t>(pi)+ 1)];
      if (v1 - v0 < 2)
        break;
      if (cb.content.userPolylineOffsets.empty())
        cb.content.userPolylineOffsets.push_back(0);
      const int baseVert = cb.content.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        cb.content.pushVertex(CadPolyVertex{L.paperPolyVerts[(static_cast<size_t>(vi)* 3 + 0)], L.paperPolyVerts[(static_cast<size_t>(vi)* 3 + 1)], L.paperPolyVerts[(static_cast<size_t>(vi)* 3 + 2)]});
        cb.content.userPolylineVertsBulge.push_back(static_cast<size_t>(vi) < L.paperPolyVertsBulge.size()
                                       ? L.paperPolyVertsBulge[static_cast<size_t>(vi)]
                                       : 0.f);
        expandBbox(L.paperPolyVerts[(static_cast<size_t>(vi)* 3 + 0)], L.paperPolyVerts[(static_cast<size_t>(vi)* 3 + 1)]);
      }
      cb.content.userPolylineOffsets.push_back(baseVert + (v1 - v0));
      cb.content.userPolylineClosed.push_back(static_cast<size_t>(pi) < L.paperPolyClosed.size() ? L.paperPolyClosed[static_cast<size_t>(pi)] : 0u);
      cb.content.userPolylineAttrs.push_back(attrAt(L.paperPolyAttrs, pi));
      break;
    }
    case PaperRef::Type::Text: {
      if (static_cast<size_t>(r.index) >= L.paperTexts.size())
        break;
      cb.content.cadAnnotations.push_back(L.paperTexts[static_cast<size_t>(r.index)]);
      cb.content.cadAnnotationAttrs.push_back(attrAt(L.paperTextAttrs, r.index));
      expandBbox(L.paperTexts[static_cast<size_t>(r.index)].insX, L.paperTexts[static_cast<size_t>(r.index)].insY);
      break;
    }
    case PaperRef::Type::Spline: {
      if (static_cast<size_t>(r.index) >= L.paperSplines.size())
        break;
      cb.content.cadSplines.push_back(L.paperSplines[static_cast<size_t>(r.index)]);
      cb.content.cadSplineAttrs.push_back(attrAt(L.paperSplineAttrs, r.index));
      for (const auto& v : L.paperSplines[static_cast<size_t>(r.index)].ctrl)
        expandBbox(static_cast<float>(v.x), static_cast<float>(v.y));
      break;
    }
    case PaperRef::Type::Point: {
      if (static_cast<size_t>(r.index) >= L.paperPoints.size())
        break;
      const CadPoint& p = L.paperPoints[static_cast<size_t>(r.index)];
      cb.content.cadPoints.push_back(p);
      cb.content.cadPointAttrs.push_back(attrAt(L.paperPointAttrs, r.index));
      expandBbox(static_cast<float>(p.x), static_cast<float>(p.y));
      break;
    }
    default:
      break;
    }
  }
  CopyEnclosedFilledRegions(cb, L.paperFilledRegions, L.paperFilledRegionAttrs, mnX, mnY, mxX, mxY);
  if (mnX > mxX) {
    cb.basePtX = 0.f;
    cb.basePtY = 0.f;
  } else {
    cb.basePtX = (mnX + mxX) * 0.5f;
    cb.basePtY = (mnY + mxY) * 0.5f;
  }
  log.push_back("COPYCLIP — " + std::to_string(st.selectedPaperEntities.size()) + " paper object(s) copied to clipboard.");
}

void CopySelectionToClipboard(AppCommandState& st, std::vector<std::string>& log) {
  // Route by active space (ADR-009/013): a paper layout active (and not floating model space) copies the
  // paper selection; otherwise the model selection.
  if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
    CopyPaperSelectionToClipboard(st, *L, log);
    return;
  }
  if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
    log.push_back("COPYCLIP — nothing selected. Select objects first.");
    return;
  }
  // A surface is not copyable (ADR-036 (b)): the clipboard carries geometry, and a surface's geometry
  // is derived from a definition that names point groups, breaklines and boundaries in THIS drawing.
  // Pasting the triangles alone would produce something that looks like a surface, rebuilds into
  // nothing, and shares an id with the original. Refused out loud rather than by the silent skip the
  // copy loop below would otherwise perform (REQ-201).
  {
    const size_t nSurf = static_cast<size_t>(std::count_if(
        st.selection.begin(), st.selection.end(),
        [](const SelectedEntity& e) { return e.type == SelectedEntity::Type::Surface; }));
    if (nSurf > 0) {
      log.push_back("COPYCLIP — " + std::to_string(nSurf) +
                    " surface(s) not copied: a surface is defined by its point groups, breaklines and"
                    " boundaries, which do not travel with a paste. Create a surface in the target"
                    " drawing instead.");
      if (nSurf == st.selection.size())
        return;  // nothing else was selected — do not go on to clear the clipboard and report success
    }
  }
  CadClipboard& cb = st.clipboard;
  cb = CadClipboard{};
  TagClipboardOrigin(st);  // REQ-383
  cb.fromPaper = false;  // copied from model space

  double mnX = 1e300, mnY = 1e300, mxX = -1e300, mxY = -1e300;
  auto expandBbox = [&](double x, double y) {
    mnX = std::min(mnX, x); mnY = std::min(mnY, y);
    mxX = std::max(mxX, x); mxY = std::max(mxY, y);
  };
  std::set<int> directFills;  // fill indices copied because they were directly selected (REQ-042)

  for (const auto& e : st.selection) {
    if (e.type == SelectedEntity::Type::LineSeg) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.lineCount())
        continue;
      cb.content.pushLine(st.drawing.line(k));
      cb.content.userLineAttrs.push_back(static_cast<size_t>(e.index) < st.drawing.userLineAttrs.size()
                                 ? st.drawing.userLineAttrs[static_cast<size_t>(e.index)] : EntityAttributes{});
      expandBbox(static_cast<float>(st.drawing.line(k).x0), static_cast<float>(st.drawing.line(k).y0));
      expandBbox(static_cast<float>(st.drawing.line(k).x1), static_cast<float>(st.drawing.line(k).y1));
    } else if (e.type == SelectedEntity::Type::Circle) {
      const size_t ci = static_cast<size_t>(e.index);
      if (ci >= st.drawing.circleCount())
        continue;
      cb.content.pushCircle(st.drawing.circle(ci));  // z survives a model copy
      cb.content.userCircleAttrs.push_back(static_cast<size_t>(e.index) < st.drawing.userCircleAttrs.size()
                                   ? st.drawing.userCircleAttrs[static_cast<size_t>(e.index)] : EntityAttributes{});
      float ccx = 0.f, ccy = 0.f, ccz = 1.f;
      CircleNormalAt(st.drawing.userCircleNormals, static_cast<size_t>(e.index), &ccx, &ccy, &ccz);
      PushCircleNormal(cb.content.userCircleNormals, ccx, ccy, ccz);
      expandBbox(static_cast<float>(st.drawing.circle(ci).cx), static_cast<float>(st.drawing.circle(ci).cy));
    } else if (e.type == SelectedEntity::Type::Arc) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.userArcs.size())
        continue;
      cb.content.userArcs.push_back(st.drawing.userArcs[k]);
      cb.content.userArcAttrs.push_back(k < st.drawing.userArcAttrs.size() ? st.drawing.userArcAttrs[k] : EntityAttributes{});
      expandBbox(static_cast<float>(st.drawing.userArcs[k].cx), static_cast<float>(st.drawing.userArcs[k].cy));
    } else if (e.type == SelectedEntity::Type::Ellipse) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.userEllipses.size())
        continue;
      cb.content.userEllipses.push_back(st.drawing.userEllipses[k]);
      cb.content.userEllAttrs.push_back(k < st.drawing.userEllAttrs.size() ? st.drawing.userEllAttrs[k] : EntityAttributes{});
      expandBbox(static_cast<float>(st.drawing.userEllipses[k].cx), static_cast<float>(st.drawing.userEllipses[k].cy));
    } else if (e.type == SelectedEntity::Type::Spline) {  // REQ-443
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.cadSplines.size()) continue;
      cb.content.cadSplines.push_back(st.drawing.cadSplines[k]);
      cb.content.cadSplineAttrs.push_back(k < st.drawing.cadSplineAttrs.size() ? st.drawing.cadSplineAttrs[k] : EntityAttributes{});
      std::vector<spline::Vec3> points;
      spline::Tessellate(st.drawing.cadSplines[k], kSplineSampleTolerance, &points);
      for (const auto& p : points) expandBbox(static_cast<float>(p.x), static_cast<float>(p.y));
    } else if (e.type == SelectedEntity::Type::Point) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.cadPoints.size()) continue;
      cb.content.cadPoints.push_back(st.drawing.cadPoints[k]);
      cb.content.cadPointAttrs.push_back(k < st.drawing.cadPointAttrs.size() ? st.drawing.cadPointAttrs[k] : EntityAttributes{});
      expandBbox(static_cast<float>(st.drawing.cadPoints[k].x), static_cast<float>(st.drawing.cadPoints[k].y));
    } else if (e.type == SelectedEntity::Type::Polyline) {
      const int pi = e.index;
      if (pi < 0 || (static_cast<size_t>(pi)+ 1) >= st.drawing.userPolylineOffsets.size())
        continue;
      const int v0 = st.drawing.userPolylineOffsets[static_cast<size_t>(pi)];
      const int v1 = st.drawing.userPolylineOffsets[(static_cast<size_t>(pi)+ 1)];
      const int nv = v1 - v0;
      if (nv < 2)
        continue;
      if (cb.content.userPolylineOffsets.empty())
        cb.content.userPolylineOffsets.push_back(0);
      const int baseVert = cb.content.userPolylineOffsets.back();
      for (int vi = v0; vi < v1; ++vi) {
        cb.content.pushVertex(CadPolyVertex{st.drawing.vertex(vi).x, st.drawing.vertex(vi).y, st.drawing.vertex(vi).z});
        cb.content.userPolylineVertsBulge.push_back(vi < static_cast<int>(st.drawing.userPolylineVertsBulge.size())  // REQ-316 / ADR-047
                                        ? st.drawing.userPolylineVertsBulge[static_cast<size_t>(vi)]
                                        : 0.0f);
        expandBbox(static_cast<float>(st.drawing.vertex(vi).x),
                   static_cast<float>(st.drawing.vertex(vi).y));
      }
      cb.content.userPolylineOffsets.push_back(baseVert + nv);
      uint8_t cl = static_cast<size_t>(pi) < st.drawing.userPolylineClosed.size()
                       ? st.drawing.userPolylineClosed[static_cast<size_t>(pi)] : 0u;
      cb.content.userPolylineClosed.push_back(cl);
      cb.content.userPolylineAttrs.push_back(static_cast<size_t>(pi) < st.drawing.userPolylineAttrs.size()
                                 ? st.drawing.userPolylineAttrs[static_cast<size_t>(pi)] : EntityAttributes{});
    } else if (e.type == SelectedEntity::Type::Annotation) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.cadAnnotations.size())
        continue;
      cb.content.cadAnnotations.push_back(st.drawing.cadAnnotations[k]);
      cb.content.cadAnnotationAttrs.push_back(k < st.drawing.cadAnnotationAttrs.size()
                                       ? st.drawing.cadAnnotationAttrs[k] : EntityAttributes{});
      expandBbox(st.drawing.cadAnnotations[k].insX, st.drawing.cadAnnotations[k].insY);
    } else if (e.type == SelectedEntity::Type::Table) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.cadTables.size())
        continue;
      cb.content.cadTables.push_back(st.drawing.cadTables[k]);
      cb.content.cadTableAttrs.push_back(k < st.drawing.cadTableAttrs.size() ? st.drawing.cadTableAttrs[k] : EntityAttributes{});
      expandBbox(st.drawing.cadTables[k].insX, st.drawing.cadTables[k].insY);
    } else if (e.type == SelectedEntity::Type::BlockRef) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.cadBlockRefs.size())
        continue;
      cb.content.cadBlockRefs.push_back(st.drawing.cadBlockRefs[k]);
      cb.content.cadBlockRefAttrs.push_back(k < st.drawing.cadBlockRefAttrs.size() ? st.drawing.cadBlockRefAttrs[k] : EntityAttributes{});
      expandBbox(st.drawing.cadBlockRefs[k].xf.x, st.drawing.cadBlockRefs[k].xf.y);
    } else if (e.type == SelectedEntity::Type::FilledRegion) {
      const size_t k = static_cast<size_t>(e.index);
      if (k >= st.drawing.cadFilledRegions.size())
        continue;
      directFills.insert(static_cast<int>(k));
      const CadFilledRegion& fr = st.drawing.cadFilledRegions[k];
      cb.content.cadFilledRegions.push_back(fr);
      cb.content.cadFilledRegionAttrs.push_back(k < st.drawing.cadFilledRegionAttrs.size() ? st.drawing.cadFilledRegionAttrs[k]
                                                                        : EntityAttributes{});
      for (size_t v = 0; v + 2 < fr.vertsXyz.size(); v += 3)
        expandBbox(static_cast<float>(fr.vertsXyz[v]), static_cast<float>(fr.vertsXyz[v + 1]));
    }
  }
  for (const int index : st.selectedSurveyPointIndices) {
    if (index < 0 || static_cast<size_t>(index) >= st.drawing.surveyPoints.size())
      continue;
    const SurveyPoint& point = st.drawing.surveyPoints[static_cast<size_t>(index)];
    cb.content.surveyPoints.push_back(point);
    expandBbox(point.easting, point.northing);
  }
  // Directly-selected fills are copied above; CopyEnclosedFilledRegions adds any *other* fills inside the
  // selection bbox (e.g. a title block's logo hatch carried with its linework — ADR-013 addendum), skipping
  // the ones already taken so they aren't duplicated.
  CopyEnclosedFilledRegions(cb, st.drawing.cadFilledRegions, st.drawing.cadFilledRegionAttrs, mnX, mnY, mxX, mxY, &directFills);

  if (mnX > mxX) {
    cb.basePtX = 0.f;
    cb.basePtY = 0.f;
  } else {
    cb.basePtX = (mnX + mxX) * 0.5;
    cb.basePtY = (mnY + mxY) * 0.5;
  }

  // REQ-087 / REQ-201. The clipboard has no feature-line store, so the walk above skips them — and
  // the count below would still report them as copied, because it counts the SELECTION rather than
  // what was actually taken. A user would then paste and find the feature line missing with no
  // message anywhere. Copying them properly needs clipboard arrays, which is stage 3's successor,
  // not stage 3; saying so is what stops the gap being invisible until someone loses work.
  int featureLinesNotCopied = 0;
  for (const auto& e : st.selection)
    if (e.type == SelectedEntity::Type::FeatureLine)
      ++featureLinesNotCopied;
  log.push_back("COPYCLIP — " +
                std::to_string(st.selection.size() - static_cast<size_t>(featureLinesNotCopied)) +
                " object(s) and " + std::to_string(cb.content.surveyPoints.size()) +
                " survey point(s) copied to clipboard.");
  if (featureLinesNotCopied > 0)
    log.push_back("COPYCLIP — " + std::to_string(featureLinesNotCopied) + " feature line" +
                  (featureLinesNotCopied == 1 ? "" : "s") +
                  " not copied: the clipboard cannot carry a feature line yet. Use COPY to "
                  "duplicate one in place.");
}

// Cut (REQ-084 (c) shortcut menu's Clipboard submenu, issue #805): copy the selection, then delete it. The check runs
// first and refuses with nothing changed unless the clipboard can carry every selected object. The
// delete that follows erases whatever is selected, so an object the copy does not carry would be lost.
// The allow-lists below name exactly the types CopySelectionToClipboard and CopyPaperSelectionToClipboard
// handle. Any type not on them, now or added later, is refused; the message names it when the name is known
// and says "an object" otherwise.
namespace {
bool ClipboardCarriesModelType(SelectedEntity::Type type) {
  switch (type) {
  case SelectedEntity::Type::LineSeg: case SelectedEntity::Type::Circle: case SelectedEntity::Type::Arc:
  case SelectedEntity::Type::Ellipse: case SelectedEntity::Type::Spline: case SelectedEntity::Type::Point:
  case SelectedEntity::Type::Polyline: case SelectedEntity::Type::Annotation: case SelectedEntity::Type::Table:
  case SelectedEntity::Type::BlockRef: case SelectedEntity::Type::FilledRegion:
    return true;
  default:
    return false;
  }
}

std::string ClipboardCannotCarryModelName(SelectedEntity::Type type) {
  switch (type) {
  case SelectedEntity::Type::Surface: return "a surface";
  case SelectedEntity::Type::FeatureLine: return "a feature line";
  case SelectedEntity::Type::Mesh: return "a mesh";
  case SelectedEntity::Type::PdfUnderlay: return "a PDF underlay";
  case SelectedEntity::Type::Solid: return "a solid";
  case SelectedEntity::Type::PipeRun: return "a pipe run";
  case SelectedEntity::Type::PointCloud: return "a point cloud";
  case SelectedEntity::Type::PositionMarker: return "a position marker";
  case SelectedEntity::Type::Multileader: return "a multileader";
  case SelectedEntity::Type::Alignment: return "an alignment";
  default: return "an object";
  }
}

bool ClipboardCarriesPaperType(PaperRef::Type type) {
  switch (type) {
  case PaperRef::Type::Line: case PaperRef::Type::Circle: case PaperRef::Type::Arc:
  case PaperRef::Type::Ellipse: case PaperRef::Type::Polyline: case PaperRef::Type::Text:
  case PaperRef::Type::Spline: case PaperRef::Type::Point:
    return true;
  default:
    return false;
  }
}

std::string ClipboardCannotCarryPaperName(PaperRef::Type type) {
  switch (type) {
  case PaperRef::Type::Block: return "a block";
  case PaperRef::Type::Leader: return "a leader";
  case PaperRef::Type::Table: return "a table";
  default: return "an object";
  }
}
}  // namespace

void CutSelectionToClipboard(AppCommandState& st, std::vector<std::string>& log) {
  const bool paper = ActivePaperGeometryTarget(st) != nullptr;
  // A stale layout index routes the copy to the model selection while DELETE takes the paper path, so refuse it.
  if (!paper && st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st)) {
    log.push_back("CUT — not done: the active layout is not available. Switch to model space first.");
    return;
  }
  if (paper) {
    if (st.selectedPaperEntities.empty() && st.selectedViewports.empty()) {
      log.push_back("CUT — nothing selected. Select objects first.");
      return;
    }
    if (!st.selectedViewports.empty()) {
      log.push_back("CUT — not done: a paper viewport cannot be copied to the clipboard, so Cut would erase it "
                    "for good. Deselect it, then Cut again.");
      return;
    }
    for (const PaperRef& ref : st.selectedPaperEntities) {
      if (!ClipboardCarriesPaperType(ref.type)) {
        log.push_back("CUT — not done: " + ClipboardCannotCarryPaperName(ref.type) +
                      " cannot be copied to the clipboard, so Cut would erase it for good. Deselect it, then Cut again.");
        return;
      }
    }
  } else {
    if (st.sectionPlaneSelected) {
      log.push_back("CUT — not done: a section plane cannot be copied to the clipboard, so Cut would erase it "
                    "for good. Deselect it, then Cut again.");
      return;
    }
    if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
      log.push_back("CUT — nothing selected. Select objects first.");
      return;
    }
    for (const SelectedEntity& e : st.selection) {
      if (!ClipboardCarriesModelType(e.type)) {
        log.push_back("CUT — not done: " + ClipboardCannotCarryModelName(e.type) +
                      " cannot be copied to the clipboard, so Cut would erase it for good. Deselect it, then Cut again.");
        return;
      }
    }
  }

  // A survey point and its label are deleted in two steps, and each step pushes an undo frame. The point's step
  // runs first, so the frame it pushes holds the state from before the whole Cut. The entity step pushes its frame
  // last, and dropping that frame leaves one undo step for the whole Cut. The history cap is switched off while
  // both steps run, so neither frame can evict the pre-Cut frame (this matters at the minimum limit of 1). The cap
  // is restored and enforced afterwards.
  const bool deletesTwice = !st.selectedSurveyPointIndices.empty() && !st.selection.empty();
  const int savedHistoryCap = st.undoHistoryMaxSize;
  if (deletesTwice) st.undoHistoryMaxSize = 0;
  CopySelectionToClipboard(st, log);
  StartDeleteCommand(st, log);
  st.undoHistoryMaxSize = savedHistoryCap;
  const int idx = st.activeDrawingIdx;
  if (deletesTwice && idx >= 0 && static_cast<size_t>(idx) < st.documents.size()) {
    auto& undo = st.documents[static_cast<size_t>(idx)].undoStack;
    if (!undo.empty() && undo.back().description == "Delete") {
      undo.pop_back();
      if (!undo.empty()) undo.back().description = "Cut";
    }
    if (savedHistoryCap > 0)
      while (static_cast<int>(undo.size()) > savedHistoryCap) undo.erase(undo.begin());
  }
  // Success is the erase clearing the selection. ExecuteDeleteSelection stops early, and keeps the selection,
  // when a grading group cannot be rebuilt; the paper erase clears its selection only when it completes.
  const bool erased = paper ? st.selectedPaperEntities.empty() : st.selection.empty();
  if (!erased) {
    log.push_back(deletesTwice
                      ? "CUT — only the survey points were erased; the lines could not be. Undo restores the survey points."
                      : "CUT — nothing was erased; the selection could not be removed. The copy is on the clipboard.");
    return;
  }
  log.push_back("CUT — copied to the clipboard and erased. Undo restores the objects.");
}

/// REQ-383 clauses 1 and 3: true when this paste must not start now — it is blocked, or it waits for
/// the user's answer to the warning the UI is about to show. "Paste anyway" and "Convert and paste" set
/// `pasteWarningAnswered` and start the paste again, which lets it through once.
static bool PasteWaitsForUser(AppCommandState& st, bool original, std::vector<std::string>& log) {
  if (st.pasteWarningAnswered) {
    st.pasteWarningAnswered = false;
    return false;
  }
  const PasteCheck pc = CheckClipboardPaste(st);
  if (pc.verdict == PasteCheck::Verdict::Ok)
    return false;
  st.pastePrompt = {true, pc.verdict == PasteCheck::Verdict::Block, original, pc.text, true, pc.canConvert};
  log.push_back(std::string(original ? "PASTEORIG" : "PASTE") +
                (pc.verdict == PasteCheck::Verdict::Block ? " — blocked: " : " — check before pasting: ") + pc.text);
  return true;
}

void StartPasteCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.clipboard.empty()) {
    log.push_back("PASTE — clipboard is empty. Use Ctrl+C to copy objects first.");
    return;
  }
  if (PasteWaitsForUser(st, false, log))
    return;
  st.clipboardPasteOriginal = false;
  ClearPendingViewportZoom(st);
  ResetAllCadDraftTools(st);
  stepengine::BeginSession(
      st, std::make_unique<stepengine::PasteSession>(st.clipboard.basePtX, st.clipboard.basePtY), true);
  log.push_back("PASTE — click destination point to place copied objects. ESC to cancel.");
}

void StartPasteOrigCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (st.clipboard.empty()) {
    log.push_back("PASTEORIG — clipboard is empty. Use Ctrl+C to copy objects first.");
    return;
  }
  if (PasteWaitsForUser(st, true, log))
    return;
  st.clipboardPasteOriginal = true;
  CommitClipboardPasteAt(st, st.clipboard.basePtX, st.clipboard.basePtY, log);
}
