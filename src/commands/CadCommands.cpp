#include "CadCommands.hpp"
#include "ProjectSettings.hpp"  // REQ-375: NoteUserPlotScale
#include "ProjectFiles.hpp"  // REQ-379: RequestProjectAttach
#include "ProjectWarnings.hpp"  // REQ-383: TagClipboardOrigin / CheckClipboardPaste
#include "CadCommandsInternal.hpp"
#include "surface/CadSurfaceInternal.hpp"
#include "input/CadInputInternal.hpp"
#include "paperspace/CadPaperSpaceInternal.hpp"
#include "annotation/CadAnnotationInternal.hpp"
#include "draw/CadDrawInternal.hpp"
#include "inquiry/CadInquiryInternal.hpp"
#include "modify/CadModifyInternal.hpp"
#include "modify/CadOffsetInternal.hpp"
#include "modify/CadTrimInternal.hpp"
#include "CadColor.hpp"
#include "CadBlocks.hpp"
#include "ToolspaceCatalog.hpp"
#include "OrthoConstrain.hpp"
#include "TextStyle.hpp"
#include "CadCoordinateFrame.hpp"
#include "CadLinetype.hpp"
#include "HatchGeom.hpp"
#include <GoSurvey/Geometry/HatchBoundary.hpp>
#include <GoSurvey/Geometry/FilletChamfer.hpp>
#include <GoSurvey/Geometry/geom2d.hpp>
#include <GoSurvey/Debug/BenchScene.hpp>
#include "GoSurvey/Document/cadpiperun.hpp"  // CadPipeRun solid generation (issue #486 increment B1 / REQ-345)
#include <GoSurvey/Geometry/meshgeom.hpp>
#include "GoSurvey/PointCloud/Octree.hpp"  // REQ-171/172 out-of-core octree (ADR-060)
#include "GoSurvey/IO/PointCloudCacheImport.hpp"   // ADR-060 .gscloud out-of-core cache (streams the E57 internally)
#include "GoSurvey/PointCloud/CylinderFit.hpp"       // REQ-347 EXTRACTCENTERLINE's least-squares cylinder-axis fit
#include "io/src/CadPointCloudE57.hpp"    // QuickPointCountEstimate, for the import progress bar's total
#include "io/src/CadPointCloudPts.hpp"    // REQ-172 PTS increment — reader/writer, same contract as E57
#include "io/src/CadPointCloudPtx.hpp"    // REQ-172 PTX increment — reader/writer, same contract as E57
#include "io/src/CadPointCloudLas.hpp"    // REQ-172 LAS increment — reader/writer, same contract as E57
#include "io/src/CadPointCloudLaz.hpp"    // REQ-172 LAZ increment — reader/writer, same contract as E57
#include "io/src/RasterImage.hpp"        // REQ-511 readable model-space raster attachments
#include <GoSurvey/Terrain/tinbuild.hpp>
#include <GoSurvey/Terrain/contourgen.hpp>  // REQ-070 contour generation (ADR-036 (f)) — pure, like tinbuild
#include <GoSurvey/Terrain/surfaceanalysis.hpp>  // REQ-072 banding + slope arrows (ADR-036 (g)) — pure, like contourgen
#include <GoSurvey/Terrain/surfacevolume.hpp>  // REQ-073 surface-to-surface volumes (TASK-095) — pure, like surfaceanalysis
#include <GoSurvey/Terrain/surfacestats.hpp>   // REQ-125
#include <GoSurvey/Terrain/watershed.hpp>      // REQ-132…134
#include "GoSurvey/Survey/AlignmentGroundModel.hpp" // REQ-513 surface profiles
#include <GoSurvey/Terrain/tinvolume.hpp>      // REQ-136
#include <GoSurvey/Terrain/gridsurface.hpp>    // REQ-137
#include <GoSurvey/Terrain/surfacequery.hpp>   // REQ-137
#include <GoSurvey/Terrain/daylight.hpp>       // REQ-398 side slope grading — the daylight solver
#include "io/src/LandXmlIo.hpp"  // issue #1002: EXPORTLANDXML
#include "io/src/SurveyCsv.hpp"  // REQ-086: a surface reads its linked point files through the REQ-083 parser
#include "io/src/gltfimport.hpp"
#include "io/src/stlimport.hpp"
#include "DwgMeshConvert.hpp"
#include "DwgIo.hpp"
#include <GoSurvey/UI/FileDialogs.hpp>
#include <GoSurvey/Core/NumFormat.hpp>
#include "GoSurvey/Document/PlotScales.hpp"  // VPSCALE (REQ-027): PlotScaleLabel / metric-ratio helpers
#include "GoSurvey/Document/MtextRichFormat.hpp"
#include "AppPaths.hpp"

#include <chrono>

#include <GoSurvey/Core/StringUtil.hpp>
#include "AppPaths.hpp"

#include "CadSnap.hpp"



#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <limits>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <numeric>
#include <utility>

// REQ-044: stamp the active text style onto a new user TEXT/MTEXT (defined below, used at the commit sites).


namespace {
/// A stored tab or undo frame may hold an empty style table; a live drawing never does. Both
/// restores refill the same tables (the tab restore also refills alignment styles).
void FillEmptyDrawingContentTables(DrawingContent& c) {
  if (c.textStyles.empty())
    c.textStyles = TextStyles::DefaultTextStyles();
  if (c.alignmentStyles.empty())
    c.alignmentStyles = AlignmentStyles::DefaultAlignmentStyles();
  if (c.tableStyles.empty())
    c.tableStyles = CadTableBuiltinStyles();
  if (c.surveyPointStyles.empty())
    c.surveyPointStyles = DefaultSurveyPointStyles();
  if (c.surveyPointLabelStyles.empty())
    c.surveyPointLabelStyles = DefaultSurveyPointLabelStyles();
  if (c.alignmentLabelSets.empty())
    c.alignmentLabelSets = AlignmentLabelSets::DefaultAlignmentLabelSets();
  if (c.alignmentLabelStyles.empty())
    c.alignmentLabelStyles = AlignmentLabelStyles::DefaultAlignmentLabelStyles();  // REQ-492.5
  if (c.dimensionStyles.empty())
    c.dimensionStyles = DimensionStyles::DefaultStyles();
  if (c.multileaderStyles.empty())
    c.multileaderStyles = MultileaderStyles::DefaultStyles();
  // The current layer, alignment style and text style are names into tables a restore may have
  // just replaced. A name the restored drawing no longer has falls back to the built-in default,
  // the same fallback the live drawing already uses for a missing style.
  const bool layerExists = std::any_of(c.drawingLayerTable.begin(), c.drawingLayerTable.end(),
                                       [&](const CadLayerRow& row) { return row.name == c.currentLayer; });
  if (!layerExists)
    c.currentLayer = "0";
  if (AlignmentStyles::Find(c.alignmentStyles, c.alignmentStyleCurrent) == nullptr)
    c.alignmentStyleCurrent = AlignmentStyles::kStandardName;
  if (TextStyles::Find(c.textStyles, c.activeTextStyleName) == nullptr) {
    c.activeTextStyleName = TextStyles::kStandardName;
    // Same height sync as SetActiveTextStyle: the fallback style's height, not the missing one's.
    if (const TextStyle* s = TextStyles::Find(c.textStyles, c.activeTextStyleName))
      c.defaultPlottedTextHeightInches = std::max(s->heightInches, 1.e-6f);
  }
}
} // namespace

void SaveDocumentToSnapshot(AppCommandState& cmd, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= cmd.documents.size()) return;
  DrawingDocument& doc       = cmd.documents[static_cast<size_t>(idx)];
  doc.currentViewportZoomScaleIndex = cmd.currentViewportZoomScaleIndex;
  doc.currentAnnotationScaleIndex   = cmd.currentAnnotationScaleIndex;
  doc.viewportPanX           = cmd.viewportPanX;
  doc.viewportPanY           = cmd.viewportPanY;
  doc.viewportZoom           = cmd.viewportZoom;
  doc.viewportPanZ           = cmd.viewportPanZ;
  doc.viewportAzimuthDeg     = cmd.viewportAzimuthDeg;    // camera orientation is per-drawing (REQ-058)
  doc.viewportElevationDeg   = cmd.viewportElevationDeg;
  doc.viewportRollDeg        = cmd.viewportRollDeg;       // screen roll under a tilted-UCS PLAN (#153)
  doc.viewportProjection     = cmd.viewportProjection;    // projection likewise (REQ-309)
  doc.viewportFovDeg         = cmd.viewportFovDeg;
  doc.viewportSectionClip       = cmd.viewportSectionClip;  // per tab, never to internal document payload (REQ-341)
  doc.viewportSectionClipOffset = cmd.viewportSectionClipOffset;
  doc.viewportSectionClipFlip   = cmd.viewportSectionClipFlip;
  doc.viewportSectionClipFrameValid = cmd.viewportSectionClipFrameValid;  // the live plane, not its undo copy
  doc.viewportSectionClipFrame      = cmd.viewportSectionClipFrame;
  doc.viewportSectionClipExtent     = cmd.viewportSectionClipExtent;
  // The coordinate system is per-drawing (REQ-154). Without this, switching tabs would carry one
  // drawing's UCS into another's — and every coordinate typed afterwards would be read in a frame
  // belonging to a different drawing, with nothing on screen to say so.
  // REQ-155: while floating model space is entered, cmd.activeUcs is a VIEWPORT's frame; the
  // drawing's own UCS is parked in the stash. Snapshot the drawing-scoped one (and patch the
  // floating viewport's frame into doc.content.paperLayouts below).
  doc.activeUcs              = CadDrawingScopedUcs(cmd);
  doc.ucsPrevious            = cmd.ucsPrevious;
  doc.activeViewName         = cmd.activeViewName;
  doc.ucsFollow              = cmd.ucsFollow;
  // ADR-0010: everything the drawing saves and the user edits, in one assignment.
  doc.content = static_cast<const DrawingContent&>(cmd.drawing);
  doc.dwgImportedLights           = cmd.dwgImportedLights;
  doc.dwgImportedSun              = cmd.dwgImportedSun;
  doc.dwgImportedSunPresent       = cmd.dwgImportedSunPresent;
  doc.dwgImportedLightList        = cmd.dwgImportedLightList;
  doc.dwgImportedLightListPresent = cmd.dwgImportedLightListPresent;
  doc.dwgImportedLightListDictKey = cmd.dwgImportedLightListDictKey;
  doc.blockRecent                 = cmd.blockRecent;
  doc.blockFavorites              = cmd.blockFavorites;
  doc.nextEntityId           = cmd.nextEntityId;  // per-drawing id counter (REQ-076)
  doc.selectedSurveyPointIndices = cmd.selectedSurveyPointIndices;
  doc.selection              = cmd.selection;
  doc.hiddenEntityIds        = cmd.hiddenEntityIds;  // isolation is per-drawing (REQ-084 (d))
  // REQ-155: fold the live floating-viewport UCS into the snapshot's copy of that viewport, so a
  // tab switch while floating does not lose a UCS change made in the viewport.
  if (cmd.floatingUcsSwapActive && cmd.floatingViewportLayout >= 0 &&
      static_cast<size_t>(cmd.floatingViewportLayout) < doc.content.paperLayouts.size()) {
    PaperLayout& dl = doc.content.paperLayouts[static_cast<size_t>(cmd.floatingViewportLayout)];
    if (cmd.floatingViewportIndex >= 0 &&
        static_cast<size_t>(cmd.floatingViewportIndex) < dl.viewports.size())
      dl.viewports[static_cast<size_t>(cmd.floatingViewportIndex)].activeUcs = cmd.activeUcs;
  }
  doc.activeSpaceIndex       = cmd.activeSpaceIndex;
  doc.cadGpuRevision         = cmd.cadGpuRevision;
  doc.savedRevision          = cmd.activeDocSavedRevision;
  doc.filePath               = cmd.activeDocFilePath;
}

void RestoreDocumentFromSnapshot(AppCommandState& cmd, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= cmd.documents.size()) return;
  const DrawingDocument& doc     = cmd.documents[static_cast<size_t>(idx)];
  cmd.currentViewportZoomScaleIndex = doc.currentViewportZoomScaleIndex;
  cmd.currentAnnotationScaleIndex   = doc.currentAnnotationScaleIndex;
  cmd.viewportPanX               = doc.viewportPanX;
  cmd.viewportPanY               = doc.viewportPanY;
  cmd.viewportZoom               = doc.viewportZoom;
  cmd.viewportPanZ               = doc.viewportPanZ;
  cmd.viewportAzimuthDeg         = doc.viewportAzimuthDeg;
  cmd.viewportElevationDeg       = doc.viewportElevationDeg;
  cmd.viewportRollDeg            = doc.viewportRollDeg;  // #153
  cmd.viewportProjection         = doc.viewportProjection;  // REQ-309
  cmd.viewportFovDeg             = doc.viewportFovDeg;
  cmd.viewportSectionClip        = doc.viewportSectionClip;  // per tab (REQ-341, D-2026-09-16-b)
  cmd.viewportSectionClipOffset  = doc.viewportSectionClipOffset;
  cmd.viewportSectionClipFlip    = doc.viewportSectionClipFlip;
  cmd.viewportSectionClipFrameValid = doc.viewportSectionClipFrameValid;
  cmd.viewportSectionClipFrame      = doc.viewportSectionClipFrame;
  cmd.viewportSectionClipExtent     = doc.viewportSectionClipExtent;
  cmd.viewAnimActive             = false;  // never resume another tab's animation
  cmd.activeUcs                  = doc.activeUcs;  // per-drawing coordinate system (REQ-154)
  cmd.ucsPrevious                = doc.ucsPrevious;
  cmd.activeViewName             = doc.activeViewName;
  cmd.ucsFollow                  = doc.ucsFollow;
  cmd.drawing = doc.content;  // ADR-0010
  FillEmptyDrawingContentTables(cmd.drawing);
  cmd.dwgImportedLights           = doc.dwgImportedLights;
  cmd.dwgImportedSun              = doc.dwgImportedSun;
  cmd.dwgImportedSunPresent       = doc.dwgImportedSunPresent;
  cmd.dwgImportedLightList        = doc.dwgImportedLightList;
  cmd.dwgImportedLightListPresent = doc.dwgImportedLightListPresent;
  cmd.dwgImportedLightListDictKey = doc.dwgImportedLightListDictKey;
  cmd.blockRecent                 = doc.blockRecent;
  cmd.blockFavorites              = doc.blockFavorites;
  cmd.nextEntityId               = doc.nextEntityId;  // per-drawing id counter (REQ-076)
  // Force a re-sweep for the incoming drawing. The guard compares against cadGpuRevision, which is
  // per-document and restored just below — two tabs can legitimately sit at the same revision, and
  // a match there would wrongly certify this drawing's entities as already swept.
  cmd.entityIdSweepRevision      = kEntityIdSweepNever;
  // The derived swept solids belong to the drawing we are leaving. Their rebuild is gated on a
  // signature over the runs alone, and the incoming drawing can legitimately hash the same (two
  // empty drawings always do), so clear the arrays here rather than trusting the gate to notice.
  cmd.pipeRunWorldSolids.clear();
  cmd.pipeRunWorldSolidAttrs.clear();
  cmd.pipeRunWorldSolidOwnerIndex.clear();
  cmd.pipeRunWorldSolidsSig = 0;  // 0 is never a real signature — forces the next refresh to derive
  cmd.selectedSurveyPointIndices = doc.selectedSurveyPointIndices;
  cmd.mleaderStyleDraft          = cmd.drawing.activeMultileaderStyle;
  cmd.selection                  = doc.selection;
  cmd.hiddenEntityIds            = doc.hiddenEntityIds;  // isolation is per-drawing (REQ-084 (d))
  cmd.activeSpaceIndex           = doc.activeSpaceIndex;
  cmd.lastPaperLayoutIndex       = doc.activeSpaceIndex >= 0 ? doc.activeSpaceIndex : 0;
  cmd.selectedViewports.clear();  // transient selection; indices don't carry across documents
  cmd.selectedViewportIndex = -1;
  cmd.selectedViewportLayout = -1;
  cmd.paperGripCorner = -2;
  cmd.paperMovePhase = 0;
  cmd.paperScalePhase = 0;
  cmd.paperSelBoxActive = false;
  cmd.floatingViewportLayout = -1;  // floating model space is transient, not per-document
  cmd.floatingViewportIndex = -1;
  cmd.floatingUcsSwapActive = false;  // REQ-155: no viewport-UCS swap outside floating model space
  cmd.modelViewSaved = false;       // restored view belongs to the restored active space
  cmd.cadGpuRevision             = doc.cadGpuRevision;
  cmd.activeDocSavedRevision     = doc.savedRevision;
  cmd.activeDocFilePath          = doc.filePath;
  cmd.active = AppCommandState::Kind::None;  // cancel any in-progress command on switch
  MigrateLegacyAnnotationTables(cmd);
}



// ---------------------------------------------------------------------------
// Undo / Redo
// ---------------------------------------------------------------------------

namespace {

static void WriteUndoHistoryLogLine(const std::string& msg) {
  const std::filesystem::path dir = UserDataDirectory();
  if (dir.empty())
    return;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const std::filesystem::path logPath = dir / "history.log";
  std::ofstream f(logPath, std::ios::app);
  if (!f)
    return;
  std::time_t t = std::time(nullptr);
  char timeBuf[32];
  struct tm tmInfo{};
#ifdef _WIN32
  localtime_s(&tmInfo, &t);
#else
  localtime_r(&t, &tmInfo);
#endif
  std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
  f << timeBuf << "  " << msg << "\n";
}

DrawingGeometrySnapshot CaptureGeometrySnapshot(const AppCommandState& st, const std::string& description) {
  DrawingGeometrySnapshot snap;
  // ADR-0010: one assignment. Meshes, point clouds, surfaces and solids copy as shared pointers,
  // not payloads (architecture §11.5 as amended 2026-08-12), so this stays O(entities), not
  // O(triangles) — which is what makes undo affordable with a model loaded.
  snap.content = static_cast<const DrawingContent&>(st.drawing);
  // Zero GL texture IDs: restored snapshots must not reference freed GPU resources.
  for (auto& att : snap.content.pdfAttachments)
    att.glTexId = 0;
  // Section plane (REQ-343 amended, issue #479 acceptance 8) — undoable and internal document payload-persisted, but
  // ONLY the face-derived plane SECTIONPLANE creates (`viewportSectionClipFrameValid`). REQ-341's
  // `SECTIONCLIP`, aimed from the active UCS, stays the view state it always was: its own offset/
  // flip/on-off edits still make no undo entry, and this gate is what keeps them from being swept
  // into an unrelated undo step just because they share the same underlying fields ("one clip
  // plane, two ways to aim it", D-2026-09-11-b). `sectionPlaneFrameValid` left false here is the
  // signal `RestoreGeometrySnapshot` reads to mean "this step predates a face-derived plane", not
  // "no plane" — see its own comment.
  snap.sectionPlaneFrameValid  = st.viewportSectionClipFrameValid;
  if (st.viewportSectionClipFrameValid) {
    snap.sectionPlaneActive = st.viewportSectionClip;
    snap.sectionPlaneFrame  = st.viewportSectionClipFrame;
    snap.sectionPlaneOffset = st.viewportSectionClipOffset;
    snap.sectionPlaneFlip   = st.viewportSectionClipFlip;
    snap.sectionPlaneExtent = st.viewportSectionClipExtent;
  }
  snap.description          = description;
  return snap;
}

void RestoreGeometrySnapshot(AppCommandState& st, const DrawingGeometrySnapshot& snap) {
  // A restore that adds/removes a layout (undo/redo of layout delete or add) shifts every layout
  // index, so the floating/viewport/active-space state would point at the wrong layout (#763).
  const bool layoutCountChanged = st.drawing.paperLayouts.size() != snap.content.paperLayouts.size();
  if (layoutCountChanged && st.floatingViewportIndex >= 0) {
    std::vector<std::string> ignored;
    ExitFloatingModelSpace(st, ignored);  // before the swap, so the drawing's UCS is restored
  }
  // A restored plot scale resizes the survey-point labels exactly as setting it does (REQ-357),
  // so remember the live one before the content swap overwrites it.
  const float previousPlotScale = st.drawing.modelUnitsPerPlottedInch;
  st.drawing = snap.content;  // ADR-0010
  FillEmptyDrawingContentTables(st.drawing);
  st.pipeRunWorldSolidsSig = 0;  // force RebuildPipeRunWorldSolids to re-derive from the swap
  st.mleaderStyleDraft = st.drawing.activeMultileaderStyle;
  st.selectedPaperEntities.clear();  // restored layouts invalidate paper-entity indices
  if (layoutCountChanged) {
    ClearViewportSelection(st);
    if (st.activeSpaceIndex >= static_cast<int>(st.drawing.paperLayouts.size()))
      st.activeSpaceIndex = kModelSpaceIndex;
    st.lastPaperLayoutIndex =
        std::clamp(st.lastPaperLayoutIndex, 0, std::max(0, static_cast<int>(st.drawing.paperLayouts.size()) - 1));
  }
  // Section plane (REQ-343 amended, issue #479 acceptance 8).
  //
  // `snap.sectionPlaneFrameValid` gates whether this step even has an opinion about the plane — see
  // `CaptureGeometrySnapshot`'s comment. Three cases:
  //  - snapshot HAS a face-derived plane (frameValid true): restore it verbatim. This is undoing/
  //    redoing an actual section-plane action.
  //  - snapshot has none, but the LIVE state currently does: this step PREDATES the plane's
  //    creation, so restoring to it means "as if it never existed" — turn it off.
  //  - neither: this step has nothing to do with the plane at all (REQ-341's SECTIONCLIP, or no
  //    plane ever placed). Leave the live SECTIONCLIP view state exactly as it is — REQ-341's "UNDO
  //    reaches straight past it" still holds for the UCS-aimed clip.
  if (snap.sectionPlaneFrameValid) {
    st.viewportSectionClip           = snap.sectionPlaneActive;
    st.viewportSectionClipFrameValid = true;
    st.viewportSectionClipFrame      = snap.sectionPlaneFrame;
    st.viewportSectionClipOffset     = snap.sectionPlaneOffset;
    st.viewportSectionClipFlip       = snap.sectionPlaneFlip;
    st.viewportSectionClipExtent     = snap.sectionPlaneExtent;
  } else if (st.viewportSectionClipFrameValid) {
    st.viewportSectionClip           = false;
    st.viewportSectionClipFrameValid = false;
    st.viewportSectionClipFrame      = ucs::Ucs{};
    st.viewportSectionClipOffset     = 0.0;
    st.viewportSectionClipFlip       = false;
    st.viewportSectionClipExtent     = SectionPlaneExtent{};
  }
  // A restored frame invalidates any in-progress grip drag/selection the same way it invalidates
  // st.selection below (DoUndo/DoRedo clear it) — a drag holds a frozen axis/anchor derived from the
  // PRE-restore plane (REQ-344), and continuing it after a jump would move the plane along a stale
  // direction.
  st.sectionPlaneSelected = false;
  st.sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
  st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  if (st.drawing.modelUnitsPerPlottedInch != previousPlotScale)
    SetDrawingPlotScale(st, st.drawing.modelUnitsPerPlottedInch);
}

} // namespace

// External-linkage wrappers so the block editor (CadBlocks.cpp, ADR-043) can stash/restore the
// whole model-geometry set — the two functions above have internal linkage.
DrawingGeometrySnapshot CadCaptureGeometrySnapshot(const AppCommandState& st, const std::string& description) {
  return CaptureGeometrySnapshot(st, description);
}
void CadRestoreGeometrySnapshot(AppCommandState& st, const DrawingGeometrySnapshot& snap) {
  RestoreGeometrySnapshot(st, snap);
}

// Undo-stack depth for the active tab. The block editor (ADR-043) marks the depth on enter and
// truncates back to it on close, so a later Undo cannot pull a block-geometry snapshot (pushed by
// an ordinary draw command mid-session) into model space.
std::size_t CadActiveUndoStackSize(const AppCommandState& st) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<std::size_t>(idx) >= st.documents.size())
    return 0;
  return st.documents[static_cast<std::size_t>(idx)].undoStack.size();
}
void CadTruncateActiveUndoStack(AppCommandState& st, std::size_t n) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<std::size_t>(idx) >= st.documents.size())
    return;
  auto& doc = st.documents[static_cast<std::size_t>(idx)];
  if (n < doc.undoStack.size())
    doc.undoStack.resize(n);
  doc.redoStack.clear();
}

namespace {

/// The fixed order the id sweep walks, and the order \ref FindEntityById searches.
///
/// **Order is load-bearing**: it is what makes assignment deterministic, and therefore what makes a
/// legacy `internal document payload` load with the same ids every time (REQ-076). Appending a new entity kind is safe;
/// reordering the existing entries would renumber every legacy drawing on its next load.
/// The order ids are handed out in. **Append only** — see \ref EntityKind. Reordering, or inserting
/// anywhere but the end, renumbers every entity in every existing drawing on its next load, and
/// REQ-069's breakline and boundary references are stored by exactly those ids.
const EntityKind kEntityKindsInSweepOrder[] = {
    EntityKind::Line,       EntityKind::Circle,       EntityKind::Arc,  EntityKind::Ellipse,
    EntityKind::Polyline,   EntityKind::Annotation,   EntityKind::FilledRegion, EntityKind::Mesh,
    EntityKind::FeatureLine,
    EntityKind::Surface,
    EntityKind::Table,
    EntityKind::BlockRef,
    EntityKind::Solid,
    EntityKind::PointCloud,
    EntityKind::PositionMarker,  ///< REQ-359 — append-only id sweep
    EntityKind::Multileader,     ///< REQ-367
    EntityKind::Spline,          ///< REQ-443
    EntityKind::Point};          ///< REQ-456 — last, so kinds above keep their ids.

/// The attribute array for a kind. One accessor for both the const and mutable walks, so the
/// two can never disagree about which arrays are covered.
template <typename StateT>
auto* AttrsForKind(StateT& st, EntityKind k) {
  switch (k) {
  case EntityKind::Line:         return &st.drawing.userLineAttrs;
  case EntityKind::Circle:       return &st.drawing.userCircleAttrs;
  case EntityKind::Arc:          return &st.drawing.userArcAttrs;
  case EntityKind::Ellipse:      return &st.drawing.userEllAttrs;
  case EntityKind::Polyline:     return &st.drawing.userPolylineAttrs;
  case EntityKind::Annotation:   return &st.drawing.cadAnnotationAttrs;
  case EntityKind::FilledRegion: return &st.drawing.cadFilledRegionAttrs;
  case EntityKind::Mesh:         return &st.drawing.cadMeshAttrs;
  case EntityKind::FeatureLine:  return &st.drawing.featureLineAttrs;
  case EntityKind::Surface:      return &st.drawing.cadSurfaceAttrs;  // REQ-068 / ADR-036 (a)
  case EntityKind::Table:        return &st.drawing.cadTableAttrs;    // REQ-148
  case EntityKind::BlockRef:     return &st.drawing.cadBlockRefAttrs;
  case EntityKind::Solid:        return &st.drawing.cadSolidAttrs;     // REQ-313 / ADR-045
  case EntityKind::PointCloud:   return &st.drawing.cadPointCloudAttrs; // REQ-171 / ADR-042
  case EntityKind::PositionMarker: return &st.drawing.cadPositionMarkerAttrs; // REQ-359
  case EntityKind::Multileader:    return &st.drawing.cadMultileaderAttrs;   // REQ-367
  case EntityKind::Spline:         return &st.drawing.cadSplineAttrs;        // REQ-443
  case EntityKind::Point:          return &st.drawing.cadPointAttrs;         // REQ-456
  }
  return &st.drawing.userLineAttrs;
}

} // namespace


/// UI-thread only: resolves a surface's definition against CURRENT drawing state — point groups,
/// breaklines, boundaries — pruning any id that no longer resolves (REQ-069) and logging what was
/// dropped or found in conflict. This is the only part of a surface build that touches
/// \c AppCommandState; everything after it (\ref RunSurfaceBuild) is pure and thread-safe.
SurfaceBuildInputs ResolveSurfaceInputs(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log) {
  SurfaceBuildInputs in;
  in.originX = st.drawing.worldDocumentOriginX;
  in.originY = st.drawing.worldDocumentOriginY;

  if (surface.gradingGroup) {
    for (const auto &member : surface.gradingMembers) {
      if (!member.strips) {
        in.inputsIncomplete = true;
        in.incompleteReason = "missing baked grading strips";
        log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason);
        return in;
      }
      for (const auto &strip : *member.strips) {
        if (((member.infill && (strip.size() < 9 || strip.size() % 3 != 0)) ||
             (!member.infill && strip.size() != 12)) ||
            !std::all_of(strip.begin(), strip.end(),
                         [](double v) { return std::isfinite(v); })) {
          in.inputsIncomplete = true;
          in.incompleteReason = "invalid baked grading strip";
          log.push_back("Surface \"" + surface.name +
                        "\": " + in.incompleteReason);
          return in;
        }
        if (member.infill) {
          std::string polygonError;
          if (!IsSimpleInfillPolygon(strip, &polygonError)) {
            in.inputsIncomplete = true;
            in.incompleteReason = "infill " + polygonError;
            log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason);
            return in;
          }
        }
        ResolvedChain chain;
        TinBoundaryLoop loop;
        loop.kind =
            TinBoundaryKind::Clip; // union, including disconnected members
        for (size_t v = 0; v < strip.size(); v += 3) {
          const double x = strip[v] + in.originX, y = strip[v + 1] + in.originY;
          chain.verts.push_back({x, y, strip[v + 2]});
          loop.ring.emplace_back(x, y);
        }
        AppendChainConstraints(chain, true, in.constraints, &in.constraintSourceIds, member.gradingId);
        in.cullLoops.push_back(std::move(loop));
      }
    }
    if (!TinFindCrossingConflicts(in.constraints).empty()) {
      in.inputsIncomplete = true;
      in.incompleteReason = "grading strips cross at different elevations";
      log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason);
    }
    return in;
  }

  // Gather points from every named group. Groups are referenced by name (REQ-067), so a name that
  // no longer resolves is reported rather than silently contributing nothing — otherwise a renamed
  // group would quietly shrink a surface with no indication why.
  int unresolvedGroups = 0;
  for (const std::string& gname : surface.sourcePointGroups) {
    const int gi = FindPointGroupIndex(st, gname);
    if (gi < 0) {
      ++unresolvedGroups;
      log.push_back("Surface \"" + surface.name + "\": point group \"" + gname + "\" no longer exists.");
      continue;
    }
    for (int pi : ResolvePointGroup(st, st.drawing.pointGroups[static_cast<size_t>(gi)], &log)) {
      const SurveyPoint& p = st.drawing.surveyPoints[static_cast<size_t>(pi)];
      // Triangulate in WORLD coordinates, in double: at state-plane magnitudes the local frame is
      // what keeps float storage precise, but the predicates need the real spacing between points
      // (ADR-028 (d)). The result is converted back to local by \ref ToLocalTin.
      in.pts.push_back({p.easting + st.drawing.worldDocumentOriginX, p.northing + st.drawing.worldDocumentOriginY,
                        static_cast<float>(p.elevation)});  // TIN vertex Z stays float (Phase G, #453)
    }
  }
  (void)unresolvedGroups;

  // Linked point files (REQ-086). Read HERE, on the UI thread, so the rebuild worker keeps touching
  // neither AppCommandState nor the filesystem. Coordinates come out of the file already in world
  // double, which is the frame the triangulator wants — no origin round trip.
  for (const CadSurfacePointFile& pf : surface.sourcePointFiles) {
    std::vector<SurveyFilePoint> filePts;
    int skipped = 0;
    std::string err;
    if (!SurveyCsvReadPointsOnly(pf.path.c_str(), SurveyCsvLayoutFromUiIndex(pf.layoutIndex),
                                 pf.skipFirstRow, &filePts, &skipped, &err)) {
      // Named, not swallowed — and it stops the build (see SurfaceBuildInputs::inputsIncomplete).
      in.inputsIncomplete = true;
      in.incompleteReason = "point file \"" + pf.path + "\" could not be read (" + err + ")";
      log.push_back("Surface \"" + surface.name + "\": " + in.incompleteReason +
                    ". Keeping the previous triangulation.");
      continue;
    }
    if (skipped > 0)
      log.push_back("Surface \"" + surface.name + "\": point file \"" + pf.path + "\" — " +
                    std::to_string(skipped) + " unreadable row(s) skipped.");
    for (const SurveyFilePoint& p : filePts)
      in.pts.push_back({p.easting, p.northing, p.elevation});
  }

  ApplyDefinitionPointEdits(surface, in);

  // Breaklines (REQ-069): resolve each by stable entity id, dropping — not merely skipping — any
  // that no longer resolve, so the STORED definition never holds a dangling reference (§8 ASSUMPTION-1).
  std::vector<CadSurfaceBreakline> resolvedBreaklines;
  int droppedBreaklines = 0;
  for (const CadSurfaceBreakline& bl : surface.breaklines) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, bl.entityId, /*requireClosed=*/false, chain)) {
      ++droppedBreaklines;
      continue;
    }
    resolvedBreaklines.push_back(bl);
    AppendChainConstraints(chain, /*forceClosed=*/false, in.constraints, &in.constraintSourceIds, bl.entityId);
  }
  surface.breaklines = std::move(resolvedBreaklines);
  if (droppedBreaklines > 0)
    log.push_back("Surface \"" + surface.name + "\": " + std::to_string(droppedBreaklines) +
                  " breakline(s) no longer exist and were removed from the definition.");

  std::vector<CadSurfaceBreakline> resolvedContours;
  int droppedContours = 0;
  for (const CadSurfaceBreakline& c : surface.contourSources) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, c.entityId, /*requireClosed=*/false, chain)) {
      ++droppedContours;
      continue;
    }
    resolvedContours.push_back(c);
    AppendChainConstraints(chain, /*forceClosed=*/false, in.constraints, &in.constraintSourceIds, c.entityId);
  }
  surface.contourSources = std::move(resolvedContours);
  if (droppedContours > 0)
    log.push_back("Surface \"" + surface.name + "\": " + std::to_string(droppedContours) +
                  " contour source(s) no longer exist and were removed from the definition.");

  // Boundaries (REQ-069): same dangling-id handling; each ring's edges become constraints too (Q1),
  // so the triangulation conforms exactly to the boundary and culling is exact, not approximate.
  std::vector<CadSurfaceBoundary> resolvedBoundaries;
  int droppedBoundaries = 0;
  for (const CadSurfaceBoundary& b : surface.boundaries) {
    ResolvedChain chain;
    if (!ResolveDefinitionChain(st, b.entityId, /*requireClosed=*/true, chain)) {
      ++droppedBoundaries;
      continue;
    }
    resolvedBoundaries.push_back(b);
    AppendChainConstraints(chain, /*forceClosed=*/true, in.constraints, &in.constraintSourceIds, b.entityId);
    TinBoundaryLoop loop;
    loop.kind = b.kind == CadBoundaryKind::Outer ? TinBoundaryKind::Outer
              : b.kind == CadBoundaryKind::Hide  ? TinBoundaryKind::Hide
              : b.kind == CadBoundaryKind::Show  ? TinBoundaryKind::Show
              : b.kind == CadBoundaryKind::Mask  ? TinBoundaryKind::Mask
                                                 : TinBoundaryKind::Clip;
    for (const auto& v : chain.verts)
      loop.ring.push_back({v[0], v[1]});
    in.cullLoops.push_back(std::move(loop));
  }
  surface.boundaries = std::move(resolvedBoundaries);
  if (droppedBoundaries > 0)
    log.push_back("Surface \"" + surface.name + "\": " + std::to_string(droppedBoundaries) +
                  " boundary(ies) no longer exist and were removed from the definition.");

  // REQ-128: if any clip ring exists, keep only points inside at least one clip (union).
  {
    std::vector<const TinBoundaryLoop*> clips;
    for (const TinBoundaryLoop& loop : in.cullLoops) {
      if (loop.kind == TinBoundaryKind::Clip)
        clips.push_back(&loop);
    }
    if (!clips.empty()) {
      std::vector<TinInputPoint> kept;
      kept.reserve(in.pts.size());
      for (const TinInputPoint& p : in.pts) {
        bool inside = false;
        for (const TinBoundaryLoop* clip : clips) {
          if (TinPointInPolygon(p.x, p.y, clip->ring)) {
            inside = true;
            break;
          }
        }
        if (inside)
          kept.push_back(p);
      }
      in.pts = std::move(kept);
    }
  }

  // Crossing breaklines at different elevations (REQ-069): reported by name and location so the
  // conflict can actually be found and fixed, rather than left to be inferred from a stray edge.
  ApplySurfaceCrossingCorrections(surface, in);
  for (const TinCrossingIssue& issue : TinFindCrossingConflicts(in.constraints))
    log.push_back("Surface \"" + surface.name + "\": breaklines cross at (" +
                  std::to_string(issue.x) + ", " + std::to_string(issue.y) +
                  ") with different elevations (" + std::to_string(issue.zFromA) + " vs " +
                  std::to_string(issue.zFromB) + ").");

  return in;
}



namespace {
/// The file is read once here purely to refuse a path that cannot be read at all — linking something
/// unreadable and only discovering it at the next rebuild would put the error a long way from the
/// action that caused it (REQ-201).
void RunSurfaceAddFile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() < 2 || f[0].empty() || f[1].empty()) {
    log.push_back("SURFACEADDFILE — usage: SURFACEADDFILE <surface>, <path>[, <PNEZD|PENZD|NEZ|ENZ>[, HEADER]].");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEADDFILE — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurfacePointFile pf;
  if (SurfaceRefusesPointEdits(st.drawing.cadSurfaces[static_cast<size_t>(si)], "SURFACEADDFILE", log)) return;
  pf.path = f[1];
  if (f.size() >= 3 && !f[2].empty()) {
    const std::string lay = StringUtil::toLowerAsciiCopy(f[2]);
    if (lay == "pnezd")      pf.layoutIndex = 0;
    else if (lay == "penzd") pf.layoutIndex = 1;
    else if (lay == "nez")   pf.layoutIndex = 2;
    else if (lay == "enz")   pf.layoutIndex = 3;
    else {
      log.push_back("SURFACEADDFILE — layout must be PNEZD, PENZD, NEZ or ENZ.");
      return;
    }
  }
  for (size_t i = 3; i < f.size(); ++i)
    if (StringUtil::toLowerAsciiCopy(f[i]) == "header")
      pf.skipFirstRow = true;

  std::vector<SurveyFilePoint> probe;
  int skipped = 0;
  std::string err;
  if (!SurveyCsvReadPointsOnly(pf.path.c_str(), SurveyCsvLayoutFromUiIndex(pf.layoutIndex), pf.skipFirstRow,
                               &probe, &skipped, &err)) {
    log.push_back("SURFACEADDFILE — cannot read \"" + pf.path + "\": " + err + ". Not linked.");
    return;
  }

  PushUndoSnapshot(st, "Link point file to surface");
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  if (SurfaceRefusesPointEdits(s, "SURFACEIMPORTFILE", log)) return;
  log.push_back("SURFACEADDFILE — linked \"" + pf.path + "\" to \"" + s.name + "\" (" +
                std::to_string(probe.size()) + " point(s)" +
                (skipped > 0 ? ", " + std::to_string(skipped) + " row(s) unreadable" : "") + ").");
  s.sourcePointFiles.push_back(std::move(pf));
  BumpCadGpuCache(st);
}

/// `SURFACEIMPORTFILE <surface>, <n>` — REQ-086's "break the link": reads the linked file once
/// through the REQ-083 import path, so its points become real survey points in the drawing and a
/// point group the surface references, then drops the link. The surface must still build identically
/// afterwards, which is the acceptance condition this exists to satisfy.
void RunSurfaceImportFile(AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
  const std::vector<std::string> f = SplitCommaFields(args);
  if (f.size() != 2 || f[0].empty()) {
    log.push_back("SURFACEIMPORTFILE — usage: SURFACEIMPORTFILE <surface>, <number> (SURFACELIST numbers them).");
    return;
  }
  const int si = FindSurfaceIndex(st, f[0]);
  if (si < 0) {
    log.push_back("SURFACEIMPORTFILE — no surface named \"" + f[0] + "\".");
    return;
  }
  CadSurface& s = st.drawing.cadSurfaces[static_cast<size_t>(si)];
  char* numEnd = nullptr;
  const long parsed = std::strtol(f[1].c_str(), &numEnd, 10);
  const bool numOk = !f[1].empty() && numEnd && *numEnd == '\0';
  const int n = numOk ? static_cast<int>(parsed) : 0;
  if (n < 1 || static_cast<size_t>(n) > s.sourcePointFiles.size()) {
    log.push_back("SURFACEIMPORTFILE — \"" + s.name + "\" has " + std::to_string(s.sourcePointFiles.size()) +
                  " point file(s); " + f[1] + " is out of range.");
    return;
  }

  const CadSurfacePointFile pf = s.sourcePointFiles[(static_cast<size_t>(n)- 1)];
  PushUndoSnapshot(st, "Import surface point file");

  // Drive the REQ-083 importer through its own state, so a file imported this way and a file imported
  // from the Import Points panel go down exactly one code path.
  const int savedLayout = st.surveyImportCsvLayoutIdx;
  const bool savedSkip = st.surveyImportCsvSkipFirstRow;
  std::string savedPath = st.surveyImportCsvPath;
  std::snprintf(st.surveyImportCsvPath, sizeof(st.surveyImportCsvPath), "%s", pf.path.c_str());
  st.surveyImportCsvLayoutIdx = pf.layoutIndex;
  st.surveyImportCsvSkipFirstRow = pf.skipFirstRow;
  const size_t before = st.drawing.surveyPoints.size();
  const bool ok = SurveyCsvImportFile(st, log);
  std::snprintf(st.surveyImportCsvPath, sizeof(st.surveyImportCsvPath), "%s", savedPath.c_str());
  st.surveyImportCsvLayoutIdx = savedLayout;
  st.surveyImportCsvSkipFirstRow = savedSkip;

  if (!ok) {
    log.push_back("SURFACEIMPORTFILE — import failed; the link is left in place.");
    return;
  }
  const size_t added = st.drawing.surveyPoints.size() - before;
  if (added == 0) {
    // The importer skips rows whose point id already exists (REQ-083's rule), so a file whose ids
    // collide with the drawing imports nothing. Breaking the link here would silently delete the
    // file's contribution from the surface — the link is the only thing still supplying those
    // points. Keep it, and say why.
    log.push_back("SURFACEIMPORTFILE — no points were imported (see the lines above; duplicate point "
                  "ids are skipped). The link to \"" + pf.path + "\" is left in place.");
    return;
  }

  // A group covering exactly the points just imported, so the surface keeps the same points by the
  // same rule every other source uses (REQ-067) rather than by a second mechanism.
  PointGroup g;
  g.name = "Imported: " + std::filesystem::path(pf.path).filename().string();
  for (int i = 1; i < 10000 && FindPointGroupIndex(st, g.name) >= 0; ++i)
    g.name = "Imported: " + std::filesystem::path(pf.path).filename().string() + " (" + std::to_string(i + 1) + ")";
  // Explicit ids, not a description rule: the group must mean "exactly the points this file brought
  // in", and a description wildcard would silently pick up unrelated shots that happen to match.
  for (size_t i = before; i < st.drawing.surveyPoints.size(); ++i)
    g.rule.explicitIds.push_back(st.drawing.surveyPoints[i].id);
  st.drawing.pointGroups.push_back(g);

  s.sourcePointGroups.push_back(g.name);
  s.sourcePointFiles.erase(s.sourcePointFiles.begin() + (n - 1));
  log.push_back("SURFACEIMPORTFILE — imported " + std::to_string(added) + " point(s) from \"" + pf.path +
                "\" into point group \"" + g.name + "\"; the link is broken.");
  BumpCadGpuCache(st);
}

/// EXTRACT (REQ-071) — defined further down, beside the layer helpers it needs, and declared here
/// because the command dispatch above reaches it first.
} // namespace

bool AttachModelRasterImage(AppCommandState& st, const std::string& path, float x, float y,
                            float width, float height, float rotationRad, std::vector<std::string>& log) {
  if (path.empty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
      !std::isfinite(height) || !std::isfinite(rotationRad) || width <= 0.f || height <= 0.f) {
    log.push_back("IMAGEATTACH — path and finite placement with positive width and height are required; no image was added.");
    return false;
  }
  CadRasterImage image;
  std::string why;
  double localX = 0.0, localY = 0.0;
  CadCoord::LocalFromWorld(st, x, y, &localX, &localY);
  if (!MakeSheetRasterImage(path, static_cast<float>(localX), static_cast<float>(localY),
                            width, height, rotationRad, &image, &why)) {
    log.push_back("IMAGEATTACH — image \"" + path + "\" could not be read (" + why + "); no image was added.");
    return false;
  }
  EnsureEntityIds(st);
  image.id = AllocEntityId(st);
  image.layer = st.drawing.currentLayer.empty() ? "0" : st.drawing.currentLayer;
  PushUndoSnapshot(st, "Attach image");
  st.drawing.cadRasterImages.push_back(std::move(image));
  BumpCadGpuCache(st);
  log.push_back("IMAGEATTACH — attached \"" + path + "\" as image " +
                std::to_string(st.drawing.cadRasterImages.back().id) + ".");
  return true;
}


void EnsureEntityIds(AppCommandState& st) {
  // Model raster IMAGEs carry their own stable ID in CadRasterImage rather than a parallel
  // EntityAttributes array. Keep the shared drawing counter above those IDs so loading and then
  // attaching another raster cannot recycle an existing image identity.
  for (const CadRasterImage& image : st.drawing.cadRasterImages)
    if (image.id >= st.nextEntityId && image.id != std::numeric_limits<std::uint64_t>::max())
      st.nextEntityId = image.id + 1;
  // Geometry has not changed since the last sweep, so nothing can be missing an id. This is what
  // lets callers invoke it unconditionally — including once a frame — without paying for a walk.
  if (st.entityIdSweepRevision == st.cadGpuRevision)
    return;
  st.entityIdSweepRevision = st.cadGpuRevision;

  std::vector<std::vector<EntityAttributes>*> arrays;
  arrays.reserve(std::size(kEntityKindsInSweepOrder));
  for (EntityKind k : kEntityKindsInSweepOrder)
    arrays.push_back(AttrsForKind(st, k));
  st.nextEntityId = AssignMissingEntityIds(arrays, st.nextEntityId);
}

std::uint64_t AllocEntityId(AppCommandState& st) { return st.nextEntityId++; }

EntityRef FindEntityById(const AppCommandState& st, std::uint64_t id) {
  for (EntityKind k : kEntityKindsInSweepOrder) {
    const int ix = FindEntityIndexById(*AttrsForKind(st, k), id);
    if (ix >= 0)
      return {k, ix};
  }
  // Erased, or never issued. Note this is NOT the entity that took its index — that is the point.
  return {};
}


void PushUndoSnapshot(AppCommandState& st, const std::string& description) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return;
  // Ids before the copy, so every undo frame is id-complete and a restored frame carries the same
  // identities it was captured with (REQ-076 — "unchanged by undo/redo").
  EnsureEntityIds(st);
  auto& doc = st.documents[static_cast<size_t>(idx)];
  doc.undoStack.push_back(CaptureGeometrySnapshot(st, description));
  doc.redoStack.clear();
  if (st.undoHistoryMaxSize > 0) {
    while (static_cast<int>(doc.undoStack.size()) > st.undoHistoryMaxSize)
      doc.undoStack.erase(doc.undoStack.begin());
  }
}

bool CanUndo(const AppCommandState& st) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return false;
  return !st.documents[static_cast<size_t>(idx)].undoStack.empty();
}

bool CanRedo(const AppCommandState& st) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return false;
  return !st.documents[static_cast<size_t>(idx)].redoStack.empty();
}

bool DoUndo(AppCommandState& st, std::vector<std::string>& log) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return false;
  auto& doc = st.documents[static_cast<size_t>(idx)];
  if (doc.undoStack.empty()) {
    log.push_back("Nothing to undo.");
    return false;
  }
  DrawingGeometrySnapshot current = CaptureGeometrySnapshot(st, "");
  doc.redoStack.push_back(std::move(current));
  const DrawingGeometrySnapshot& frame = doc.undoStack.back();
  const std::string desc = frame.description;
  RestoreGeometrySnapshot(st, frame);
  doc.undoStack.pop_back();
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  st.selection.clear();
  st.selectedSurveyPointIndices.clear();
  log.push_back("UNDO" + (desc.empty() ? "." : ": " + desc));
  WriteUndoHistoryLogLine("UNDO: " + desc);
  return true;
}

bool DoRedo(AppCommandState& st, std::vector<std::string>& log) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return false;
  auto& doc = st.documents[static_cast<size_t>(idx)];
  if (doc.redoStack.empty()) {
    log.push_back("Nothing to redo.");
    return false;
  }
  DrawingGeometrySnapshot current = CaptureGeometrySnapshot(st, "");
  doc.undoStack.push_back(std::move(current));
  const DrawingGeometrySnapshot& frame = doc.redoStack.back();
  const std::string desc = frame.description;
  RestoreGeometrySnapshot(st, frame);
  doc.redoStack.pop_back();
  BumpCadGpuCache(st);
  st.active = AppCommandState::Kind::None;
  st.selection.clear();
  st.selectedSurveyPointIndices.clear();
  log.push_back("REDO" + (desc.empty() ? "." : ": " + desc));
  WriteUndoHistoryLogLine("REDO: " + desc);
  return true;
}

bool SubmitLineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);

bool SubmitPolylineVertex(AppCommandState& st, float x, float y, std::vector<std::string>& log);

// REQ-303: SubmitViewportPickImpl (below) needs to call this on a click-to-close, ahead of its
// definition further down the file.


float CadOffsetEntityPickTolWorld(const AppCommandState& st);
void CommitDesignateAt(AppCommandState& st, float wx, float wy, bool isBoundary, std::vector<std::string>& log);

/// Where \p ray meets the horizontal plane z = \p z, as XY. False when the ray runs along the plane.

int PickCadAnnotationAt(float wxIn, float wyIn, const AppCommandState& cmd, float orthoHalfHeightWorld,
                        float viewportHeightPx, const ray3d::Ray* ray) {
  const float tol =
      CadSnap::WorldToleranceFromPixels(viewportHeightPx, orthoHalfHeightWorld, cmd.objectSnapAperturePx);
  const float tol2 = tol * tol;
  auto distSqSeg = [](float px, float py, float ax, float ay, float bx, float by) -> float {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float len2 = vx * vx + vy * vy;
    if (len2 < 1.e-18f) {
      const float dx = px - ax;
      const float dy = py - ay;
      return dx * dx + dy * dy;
    }
    const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
    const float qx = ax + t * vx;
    const float qy = ay + t * vy;
    const float dx = px - qx;
    const float dy = py - qy;
    return dx * dx + dy * dy;
  };
  for (int i = static_cast<int>(cmd.drawing.cadAnnotations.size()) - 1; i >= 0; --i) {
    // REQ-084 (d): isolated-out text is not drawn, so it must not be pickable or hoverable.
    if (!cmd.hiddenEntityIds.empty() && static_cast<size_t>(i) < cmd.drawing.cadAnnotationAttrs.size() &&
        CadEntityIdHidden(&cmd.hiddenEntityIds, cmd.drawing.cadAnnotationAttrs[static_cast<size_t>(i)].id))
      continue;
    const CadAnnotation& a = cmd.drawing.cadAnnotations[static_cast<size_t>(i)];
    // Orbited (issue #564 §2): test the annotation where the camera ray meets ITS plane, not where
    // it meets the work plane — the two differ as soon as the text is not at the work plane's Z.
    float wx = wxIn;
    float wy = wyIn;
    if (ray && !RayAtElevation(*ray, static_cast<double>(a.insZ), &wx, &wy))
      continue;  // seen exactly edge-on: the text is a line on screen and names no point
    if (a.kind == CadAnnotation::Kind::DimAligned || a.kind == CadAnnotation::Kind::DimLinear) {
      float sx1 = 0.f, sy1 = 0.f, sx2 = 0.f, sy2 = 0.f, tx = 0.f, ty = 0.f, nx = 0.f, ny = 0.f, meas = 0.f;
      if (!CadDimAnyGeometry(a, &sx1, &sy1, &sx2, &sy2, &tx, &ty, &nx, &ny, &meas))
        continue;
      float best = tol2 + 1.f;
      auto upd = [&](float ax, float ay, float bx, float by) {
        best = std::min(best, distSqSeg(wx, wy, ax, ay, bx, by));
      };
      const float gap = std::clamp(0.012f * meas, 1.e-5f * meas, 0.12f * meas);
      const float over = std::clamp(0.02f * meas, 1.e-5f * meas, 0.1f * meas);
      const float leg1 = std::hypot(sx1 - a.dimExt1X, sy1 - a.dimExt1Y);
      const float u1 = leg1 > 1.e-8f ? gap / leg1 : 0.f;
      const float ex1 = a.dimExt1X + (sx1 - a.dimExt1X) * u1;
      const float ey1 = a.dimExt1Y + (sy1 - a.dimExt1Y) * u1;
      const float leg2 = std::hypot(sx2 - a.dimExt2X, sy2 - a.dimExt2Y);
      const float u2 = leg2 > 1.e-8f ? gap / leg2 : 0.f;
      const float ex2 = a.dimExt2X + (sx2 - a.dimExt2X) * u2;
      const float ey2 = a.dimExt2Y + (sy2 - a.dimExt2Y) * u2;
      upd(ex1, ey1, sx1 + nx * over, sy1 + ny * over);
      upd(ex2, ey2, sx2 + nx * over, sy2 + ny * over);
      upd(sx1, sy1, sx2, sy2);
      const float h = CadAnnotationHeightWorld(a, cmd.drawing.modelUnitsPerPlottedInch);
      const float charFactor = 0.55f;
      const float tw = std::max(h * charFactor * std::max(1.f, static_cast<float>(a.text.size())), h * 2.f);
      const float c = std::cos(a.rotationRad);
      const float s = std::sin(a.rotationRad);
      auto corner = [&](float lx, float ly, float* ox, float* oy) {
        const float rx = lx * c - ly * s;
        const float ry = lx * s + ly * c;
        *ox = a.insX + rx;
        *oy = a.insY + ry;
      };
      float xs[4]{};
      float ys[4]{};
      corner(0.f, 0.f, &xs[0], &ys[0]);
      corner(tw, 0.f, &xs[1], &ys[1]);
      corner(tw, -h, &xs[2], &ys[2]);
      corner(0.f, -h, &xs[3], &ys[3]);
      for (int e = 0; e < 4; ++e) {
        const int e2 = (e + 1) % 4;
        upd(xs[e], ys[e], xs[e2], ys[e2]);
      }
      if (best <= tol2)
        return i;
      continue;
    }
    if (a.kind == CadAnnotation::Kind::Table)
      continue;
    float mnX = 0.f;
    float mnY = 0.f;
    float mxX = 0.f;
    float mxY = 0.f;
    CadAnnotationRoughBounds(a, cmd.drawing.modelUnitsPerPlottedInch, &mnX, &mnY, &mxX, &mxY);
    if (wx >= mnX - tol && wx <= mxX + tol && wy >= mnY - tol && wy <= mxY + tol)
      return i;
  }
  return -1;
}

int PickCadTableAt(float wx, float wy, const AppCommandState& cmd, float orthoHalfHeightWorld,
                   float viewportHeightPx) {
  const float tol =
      CadSnap::WorldToleranceFromPixels(viewportHeightPx, orthoHalfHeightWorld, cmd.objectSnapAperturePx);
  for (int i = static_cast<int>(cmd.drawing.cadTables.size()) - 1; i >= 0; --i) {
    if (!cmd.hiddenEntityIds.empty() && static_cast<size_t>(i) < cmd.drawing.cadTableAttrs.size() &&
        CadEntityIdHidden(&cmd.hiddenEntityIds, cmd.drawing.cadTableAttrs[static_cast<size_t>(i)].id))
      continue;
    if (CadTableContainsLocal(cmd.drawing.cadTables[static_cast<size_t>(i)], wx, wy, tol))
      return i;
  }
  return -1;
}


namespace CadCmdGeom {

[[nodiscard]] float DistSqPointSegment(float px, float py, float ax, float ay, float bx, float by) {
  const float vx = bx - ax;
  const float vy = by - ay;
  const float len2 = vx * vx + vy * vy;
  if (len2 < 1.e-18f) {
    const float dx = px - ax;
    const float dy = py - ay;
    return dx * dx + dy * dy;
  }
  const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / len2, 0.f, 1.f);
  const float qx = ax + t * vx;
  const float qy = ay + t * vy;
  const float dx = px - qx;
  const float dy = py - qy;
  return dx * dx + dy * dy;
}

/// Minimum squared distance between finite segments AB and CD (dense sampling handles skew / parallel robustly).
[[nodiscard]] float MinDistSqSegSeg(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
  float best = DistSqPointSegment(ax, ay, cx, cy, dx, dy);
  best = std::min(best, DistSqPointSegment(bx, by, cx, cy, dx, dy));
  best = std::min(best, DistSqPointSegment(cx, cy, ax, ay, bx, by));
  best = std::min(best, DistSqPointSegment(dx, dy, ax, ay, bx, by));
  constexpr int N = 28;
  for (int i = 1; i < N; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(N);
    const float px = ax + t * (bx - ax);
    const float py = ay + t * (by - ay);
    best = std::min(best, DistSqPointSegment(px, py, cx, cy, dx, dy));
    const float qx = cx + t * (dx - cx);
    const float qy = cy + t * (dy - cy);
    best = std::min(best, DistSqPointSegment(qx, qy, ax, ay, bx, by));
  }
  return best;
}

} // namespace CadCmdGeom



// ---------------------------------------------------------------------------
// Draft-state reset cluster. External linkage (declared in CadCommandsInternal.hpp)
// so the cohesive command slices split out of this mega-TU can reach
// ResetAllCadDraftTools — TASK-150 Phase 2 (issue #142). Self-contained: calls only
// header inlines and each other.
// ---------------------------------------------------------------------------
void ResetCircleDraft(AppCommandState& st) {
  st.circleStyle = AppCommandState::CircleStyle::CenterRadius;
  st.circlePhase = AppCommandState::CirclePhase::WaitCenterOrMode;
  st.circleCx = st.circleCy = 0.f;
  st.c3p1x = st.c3p1y = st.c3p2x = st.c3p2y = 0.f;
  st.circleCz = st.c3p1z = st.c3p2z = 0.f;
}

void ResetPolylineDraft(AppCommandState& st) {
  st.polylinePhase = AppCommandState::PolylinePhase::NeedFirstPoint;
  st.polyFirstX = st.polyFirstY = 0.f;
  st.polyDraftSegments = 0;
  st.polylineDraftVerts.clear();
  st.polylineDraftBulge.clear();       // REQ-316 / ADR-047
  st.polylineArcMode = false;
  st.polylineArcRadius = 0.f;
  st.polylineArcAngleDeg = 0.f;
  st.polylineArcAngleValid = false;
  st.polylineDraft3d = false;  // REQ-085: the next POLYLINE is 2D unless 3DPOLY says otherwise
  st.polylineTypedZValid = false;
  st.polylineTypedZRelative = false;
  st.polylineTypedZ = 0.f;
}

/// REQ-087 / TASK-082. The feature-line draft had no reset function at all: StartFeatureLineCommand
/// cleared it by hand and ResetAllCadDraftTools did not touch it, so cancelling FEATURELINE left the
/// draft vertices behind — harmless only because nothing read them, which BUG-2's preview changes.
void ResetFeatureLineDraft(AppCommandState& st) {
  st.featureLineDraftVerts.clear();
  st.featureLineDraftElevPt.clear();
  st.featureLineDraftName.clear();
  st.featureLinePendingPoint = false;
  st.featureLinePendingX = st.featureLinePendingY = 0.f;
  st.featureLinePendingDefaultZ = 0.f;
  st.featureLineNextIsElevPoint = false;
}

void ResetArcDraft(AppCommandState& st) {
  st.arcPhase = AppCommandState::ArcPhase::WaitStart;
  st.arcAx = st.arcAy = st.arcBx = st.arcBy = 0.f;
  st.arcAz = st.arcBz = 0.f;
}

void ResetEllipseDraft(AppCommandState& st) {
  st.ellPhase = AppCommandState::EllipsePhase::WaitCenter;
  st.ellCx = st.ellCy = 0.f;
  st.ellMajEx = st.ellMajEy = 0.f;
  st.ellCz = st.ellMajEz = 0.f;
}

void ResetRectDraft(AppCommandState& st) {
  st.rectPhase = AppCommandState::RectPhase::WaitFirstCorner;
  st.rectX1 = st.rectY1 = 0.f;
}

void ResetTextCmdDraft(AppCommandState& st) {
  st.textPhase = AppCommandState::TextCmdPhase::WaitInsertion;
  st.textInsX = st.textInsY = 0.f;
  st.textHeightDraft = DefaultAnnotationTextHeightWorld(st);
  st.textRotDraft = 0.f;
}

void ResetMtextDraft(AppCommandState& st) {
  st.mtextPhase = AppCommandState::MtextPhase::WaitCorner1;
  st.mtxtX1 = st.mtxtY1 = st.mtxtX2 = st.mtxtY2 = 0.f;
  CloseMtextRichEditorUi(st);
}

void ResetDimDraft(AppCommandState& st) {
  st.dimPhase = AppCommandState::DimPhase::WaitExt1;
  st.dimE1x = st.dimE1y = st.dimE2x = st.dimE2y = 0.f;
  st.dimLinearDraftVertical = false;
  st.dimLinearOrientUserLock = false;
  st.dimLinearLockCursorWx = st.dimLinearLockCursorWy = 0.f;
}

void ResetDimAngularDraft(AppCommandState& st) {
  st.dimAngularPhase = AppCommandState::DimAngularPhase::WaitVertex;
  st.dimAngVx = st.dimAngVy = 0.f;
}

void ResetSurveyInverseDraft(AppCommandState& st) {
  st.surveyInversePhase = AppCommandState::SurveyInversePhase::WaitFrom;
  st.surveyInverseFromX = st.surveyInverseFromY = 0.f;
}

void ResetDistDraft(AppCommandState& st) {
  st.distPhase = AppCommandState::DistPhase::WaitFrom;
  st.distFromX = st.distFromY = st.distFromZ = 0.f;
}

void ResetAllCadDraftTools(AppCommandState& st) {
  // An armed section-plane handle drag (REQ-343). Every command start calls this, which is exactly
  // where a live drag has to end: the drag runs off the frame loop and is gated only on the clip
  // being on, so a drag left armed kept rewriting the clip offset while the NEXT command took its
  // picks — grab the Move handle, type LINE, and the cut slides across the model as the line is
  // drawn. `SECTIONPLANE` itself had the same hole: a refused face pick left the plane tracking the
  // cursor while the user hunted for a flat face.
  //
  // The plane stays where the drag had already put it — the drag has been writing the live value
  // all along, so there is nothing to restore and this is a disarm, not a cancel. ESC is where a
  // true cancel lives.
  st.sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
  st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  // A 3D gizmo command replaced by another command mid-run (a ribbon button, say) still owes the
  // user their gizmo op back — the command set it only for its own duration (GitHub issue #564).
  if (IsGizmoCommandKind(st.active))
    EndGizmoCommand(st);
  // UCS / PLAN prompt state (REQ-154). Reset here with every other draft so a cancelled UCS cannot
  // leave a half-collected origin behind for the next command to pick up.
  //
  // The resolved-point elevation goes with them: it describes the point one command was placing,
  // and carrying it into the next would commit that command's geometry at the previous one's Z.
  st.resolvedPointZValid = false;
  st.resolvedPointZ = 0.f;
  st.resolvedPointZTyped = false;
  st.ucsPhase = AppCommandState::UcsPhase::Idle;
  st.planPhase = AppCommandState::PlanPhase::Idle;
  ResetCircleDraft(st);
  ResetPolylineDraft(st);
  ResetFeatureLineDraft(st);  // REQ-087: was missing, so a cancelled FEATURELINE kept its draft
  ResetArcDraft(st);
  ResetEllipseDraft(st);
  st.splineDraftPoints.clear();
  st.splineMethod = AppCommandState::SplineMethod::Fit;
  ResetCadDrawInquiryDrafts(st);
  ResetRectDraft(st);
  ResetTextCmdDraft(st);
  ResetMtextDraft(st);
  ResetMleaderDraft(st);  // REQ-367
  ResetDimDraft(st);
  ResetDimAngularDraft(st);
  ResetSurveyInverseDraft(st);
  ResetDistDraft(st);
  ClearDimGripInteraction(st);
  AbortMtextGripInteraction(st);
  // Release PDF draft resources if any command was running
  if (st.pdfDraftCache) {
    PdfDraftCache_Free(st.pdfDraftCache);
    st.pdfDraftCache = nullptr;
  }
  if (st.pdfAttachPreviewReady) {
    PdfAttach_ReleaseTexture(st.pdfAttachPreview);
    st.pdfAttachPreviewReady = false;
  }
  st.pdfAttachDialogOpen = false;
  st.pdfAttachPhase = AppCommandState::PdfAttachPhase::WaitDialog;
  st.insertBlockDialogOpen = false;
  st.insertBlockPhase = AppCommandState::InsertBlockPhase::WaitDialog;
  st.insertBlockAttrDialogOpen = false;
  st.blockCreateDialogOpen = false;
  st.blockCreatePhase = AppCommandState::BlockCreatePhase::WaitDialog;
  st.wblockDialogOpen = false;  // same reason as the two block dialogs above it
  st.insertBlockAttrRefIndex = -1;
  st.insertBlockSnappedPipeRun = -1;
  // The solid-edge fillet's radius prompt (REQ-323). Here rather than only in `StartFilletCommand`
  // so ESC clears it: without this, cancelling would leave the flag set, and the NEXT typed line -
  // whatever it was - would be read as a fillet radius.
  st.filletSolidAwaitingRadius = false;
  st.chamferSolidAwaitingDistance = false;  // and the same for the solid chamfer's (REQ-331)
}

// External linkage (CadCommandsInternal.hpp) for the split-out command slices — TASK-150 Phase 2.
EntityAttributes MakeNewEntityAttrs(const AppCommandState& st) {
  EntityAttributes a;
  a.layer = st.drawing.currentLayer.empty() ? std::string("0") : st.drawing.currentLayer;
  a.color = st.drawing.currentColor.empty() ? std::string("ByLayer") : st.drawing.currentColor;  // REQ-356
  a.linetype = "ByLayer";
  a.lineweightMm = -1.f;
  a.transparency = -1.f;
  return a;
}

namespace {

bool ParseTwoFloats(std::string s, float* x, float* y) {
  for (char& c : s) {
    if (c == ',')
      c = ' ';
  }
  std::istringstream iss(s);
  if (!(iss >> *x))
    return false;
  if (!(iss >> *y))
    return false;
  return true;
}

} // namespace
// Double-precision sibling of ParseTwoFloats, for the typed-coordinate path (REQ-101). Same grammar,
// same comma-or-space separation; the only difference is that it does not throw away the decimals a
// state-plane easting needs before the document origin has been subtracted.
bool ParseTwoDoubles(std::string s, double* x, double* y) {
  for (char& c : s) {
    if (c == ',')
      c = ' ';
  }
  std::istringstream iss(s);
  if (!(iss >> *x))
    return false;
  if (!(iss >> *y))
    return false;
  return true;
}

bool ParseOneFloat(const std::string& s, float* v) {
  std::istringstream iss(StringUtil::trimCopy(s));
  return static_cast<bool>(iss >> *v);
}
namespace {

struct CmdEntry {
  const char* primary;
  const char* aliases;
  const char* description;
};

const CmdEntry kRegistry[] = {
    {"line", "l", "Draw line segments"},
    {"centermark", "", "Add a center mark to the selected circles and arcs"},
    {"centerline", "", "Draw a centerline between the two selected lines"},
    {"circle", "c", "Draw a circle"},
    {"polyline", "pl", "Draw a connected polyline"},
    {"3dpoly", "3dp, 3dpolyline", "Draw a polyline whose vertices each carry their own elevation"},
    {"featureline", "fl",
     "Draw a feature line: named 3D linework with per-vertex elevations "
     "(REQ-087)"},
    {"grading", "grd",
     "Side slopes from a feature line out to a surface, giving the daylight "
     "line (REQ-398)"},
    {"featurelinelist", "fllist", "List every feature line and its vertices"},
    {"rect", "rectang, rectangle", "Draw a rectangle (two opposite corners)"},
    {"trimstate", "", "TRIM mode: 0 = draw a line to trim (default), 1 = pick cutting edges"},
    {"bench", "",
     "REQ-100 frame-budget benchmark: BENCH [segments] | BENCH SURFACE "
     "[points] | BENCH MESH [triangles] | BENCH SOLID [count]"},
    {"visualstyle", "vs, vscurrent", "Viewport visual style: 2D / HIDDEN / SHADED"},
    {"perspective", "projection, persp", "View projection: ON (perspective) / OFF (orthographic)"},
    {"sectionclip", "sclip, clip", "Live section clip by the UCS plane: ON / OFF / FLIP / <offset>"},
    {"sectionplane", "splane", "Place the section plane on a solid's flat face"},
    {"fov", "lens", "Perspective field of view, in degrees"},
    {"crosshair3d", "cursor3d, xhair3d", "3D crosshair cursor showing the UCS axes: ON / OFF"},
    {"importmodel", "gltf, import3d", "Import a glTF/GLB 3D model as reference geometry"},
    {"pointcloudattach", "importpointcloud",
     "Import a point cloud (E57, PTS, PTX, LAS, or LAZ) as reference geometry "
     "(REQ-172)"},
    {"exportpts", "",
     "Export the selected point cloud(s) (or all, if none selected) to a PTS "
     "file (REQ-172)"},
    {"exportptx", "",
     "Export the selected point cloud(s) (or all, if none selected) to a PTX "
     "file (REQ-172)"},
    {"exportlas", "",
     "Export the selected point cloud(s) (or all, if none selected) to a LAS "
     "file (REQ-172)"},
    {"exportlaz", "",
     "Export the selected point cloud(s) (or all, if none selected) to a LAZ "
     "file (REQ-172)"},
    // The seven primitive solids (REQ-313 / ADR-045). Each takes a base point
    // in the active UCS and then its exact dimensions; the UCS supplies the
    // orientation.
    {"box", "", "Create a box solid: BOX <X,Y[,Z]> <length> <width> <height>"},
    {"wedge", "we", "Create a wedge solid: WEDGE <X,Y[,Z]> <length> <width> <height>"},
    {"pyramid", "pyr",
     "Create a pyramid solid: PYRAMID <X,Y[,Z]> <sides> <base r> <top r> "
     "<height>"},
    {"cylinder", "cyl", "Create a cylinder solid: CYLINDER <X,Y[,Z]> <radius> <height>"},
    {"cone", "", "Create a cone solid: CONE <X,Y[,Z]> <base radius> <top radius> <height>"},
    {"sphere", "sph", "Create a sphere solid: SPHERE <X,Y[,Z]> <radius>"},
    {"torus", "tor", "Create a torus solid: TORUS <X,Y[,Z]> <radius> <tube radius>"},
    {"presspull", "pp",
     "Move a solid FACE, or turn a closed shape into a solid: PRESSPULL, "
     "select a target, then a distance"},
    {"solidlist", "solids", "List every solid: kind, layer, volume, surface area, topology counts"},
    {"massprop", "massproperties, solidmassprop, massp",
     "Mass properties of every solid: volume, area, centroid, inertia tensor, "
     "principal axes (REQ-349)"},
    {"section",     "",
     "Cross-section of solids by a plane through three points (or the UCS), as "
     "a closed polyline"},
    {"solidcheck", "scheck",
     "Check every solid (or the selection): closed, manifold, oriented, "
     "self-intersecting"},
    {"polysolid", "psolid",
     "Sweep a wall along a path: POLYSOLID, then points (A arc, C close, "
     "H/W/J, O object)"},
    {"revcloud", "",
     "Revision cloud: click corners round an area, or O to convert a closed "
     "shape (A arc length, R reverse, D delete original)"},
    {"piperun", "pipe",
     "Route a pipe run: PIPERUN, nominal size [class], wall thickness (Enter = "
     "schedule 40), then "
     "points (U undo, END/Enter finishes)"},
    {"pipesys", "pipenet, pipingsystem",
     "Named piping networks: PIPESYS NEW/ADD/REMOVE/RENAME/DELETE/LIST <name> "
     "(ADD/REMOVE use the "
     "current pipe-run selection)"},
    {"pipecatalog", "pcat",
     "Look up a fitting in the library by size/class/type: PIPECATALOG <part "
     "type> <size> [class]"},
    {"isolines", "",
     "Curves drawn around a curved solid face: ISOLINES [0-256], or bare to "
     "report"},
    {"extrude", "ext",
     "Extrude a selected closed polyline or circle into a solid: EXTRUDE "
     "<height>"},
    {"revolve", "rev", "Revolve a selected closed polyline or circle about an axis into a solid"},
    {"slice", "sl",
     "Cut selected solids with a plane (three points), keeping one side or "
     "both"},
    {"loft", "lft",
     "Loft a solid through two or more selected closed polylines or circles, "
     "in pick order"},
    {"sweep", "swp",
     "Sweep a closed profile along a line, arc or open polyline path (T twist, "
     "A path-alignment)"},
    {"union", "uni", "Combine two selected solids into one"},
    {"subtract", "su", "Subtract the second selected solid from the first"},
    {"intersect", "in", "Keep only the volume two selected solids share"},
    {"elev", "ucs", "Elevation new geometry is drawn at (W = world Z 0)"},
    {"arc", "", "Draw an arc"},
    {"ellipse", "el", "Draw an ellipse"},
    {"spline", "spl", "Draw a fit-point or control-vertex spline (CV, Close, End, Undo)"},
    {"point", "", "Place CAD point entities (PDMODE / PDSIZE control the marker)"},
    {"pdmode", "", "Point display mode: 0 dot, 1 none, 2 plus, 3 X, 4 tick; +32 circle, +64 square"},
    {"pdsize", "", "Point display size: 0 = 5% of view, negative = percent, positive = drawing units"},
    {"leader", "le", "Draw a leader: points, then Mtext or None annotation"},
    {"mleader", "ml", "Draw a multileader: arrow tip, landing, then label"},
    {"pedit", "pe", "Edit a polyline (Close, Open, Join, Reverse, Edit vertex, Fit, Spline, Decurve)"},
    {"measure", "me", "Place points at a measured interval along an object"},
    {"divide", "div", "Place points that divide an object into equal segments"},
    {"area", "aa", "Measure the area and perimeter of points or a closed object"},
    {"list", "li", "List type, layer, space and geometry of selected objects"},
    {"dataextraction", "", "Extract selected objects to an Excel or CSV file: type, layer, text, coordinates"},
    {"table", "", "Insert an empty table: style, columns, rows, then the insertion point"},
    {"addpointtable", "", "Add a point table of survey points in a point table style (dialog in the editor)"},
    {"hatch", "h, bhatch", "Fill a closed area (pick an internal point)"},
    {"text", "", "Place single-line text"},
    {"mtext", "mt", "Place multiline text"},
    {"dimaligned", "dal", "Aligned dimension"},
    {"dimlinear", "dli", "Linear dimension"},
    {"dimangular", "dan", "Angular dimension"},
    {"dimsty", "dimstyle, dsty", "Dimension style editor"},
    {"tablestyle", "tsty", "Drawing table style manager"},
    {"msty", "mleaderstyle, mlsty", "Multileader style editor"},
    {"id", "", "Identify point coordinates"},
    {"inverse", "inv", "Inverse between two points"},
    {"dist", "di", "3D distance between two points: dX, dY, dZ, slope distance"},
    {"surfelev", "se", "Surface elevation at a point; grade between two"},
    {"designatebreakline", "dbl", "Add a picked line/polyline as a surface breakline"},
    {"designatecontour", "dcon", "Add a picked line/polyline as a surface contour source"},
    {"designateboundary", "dbd",
     "Add a picked closed polyline as a surface boundary "
     "(outer/hide/show/clip)"},
    {"surfacecreate", "sfcreate",
     "Create a surface from point groups: SURFACECREATE <name>, <group>[, "
     "<group>…]"},
    {"surfacecreategrid", "sfgrid",
     "Create a grid surface: SURFACECREATEGRID <name>, ox, oy, sx, sy, cols, "
     "rows[, z…]"},
    {"surfacecreatecorr", "sfcorr", "Create a corridor surface: SURFACECREATECORR <name>"},
    {"surfacecreatevolgrid", "sfvolgrid", "Grid volume surface: SURFACECREATEVOLGRID <name>, <base>, <comparison>"},
    {"surfswapedge", "sfswap", "Swap a TIN interior edge: SURFSWAPEDGE <surface>[, <x>, <y>]"},
    {"surfaceaddpoint", "sfaddpt", "Add a TIN definition point: SURFACEADDPOINT <surface>[, <x>, <y>, <z>]"},
    {"surfacemovepoint", "sfmovept",
     "Move a TIN definition point: SURFACEMOVEPOINT <surface>[, <x1>, <y1>, "
     "<x2>, <y2>, <z2>]"},
    {"surflodlow", "", "Set a surface to Reduced viewport detail: SURFLODLOW <surface>"},
    {"imageattach", "", "Attach a readable model-space image: IMAGEATTACH <path>, x, y, width, height, rotation-degrees"},
    {"surfdrape", "", "Drape an image onto a surface: SURFDRAPE <surface>, <image-id>"},
    {"createdatashortcut", "", "Register a project surface source: CREATEDATASHORTCUT <surface>"},
    {"createsurfacereference", "", "Create a read-only project surface reference: CREATESURFACEREFERENCE <source-drawing-rel>, <source-surface-id>, <name>"},
    {"surflodhigh", "", "Set a surface to full viewport detail: SURFLODHIGH <surface>"},
    {"movetosurface", "", "Move selected block references to a surface: MOVETOSURFACE <surface>"},
    {"surfacevisibility", "surfvis", "Surface LOS or ZVI analysis: SURFACEVISIBILITY <surface>, LOS|ZVI, ..."},
    {"surfcrossbl", "", "List or resolve surface-local breakline crossings: SURFCROSSBL <surface>, ..."},
    {"surfdelline", "sfdelline", "Delete an interior TIN edge: SURFDELLINE <surface>[, <x>, <y>]"},
    {"surfacedelpoint", "sfdelpt",
     "Delete nearest TIN definition point: SURFACEDELPOINT <surface>[, <x>, "
     "<y>]"},
    {"quickprofile", "qprof",
     "Quick profile along two points: QUICKPROFILE <surface>[, <x1>, <y1>, "
     "<x2>, <y2>]"},
    {"alignment", "createalignment", "Create an alignment (no arguments: the Create Alignment dialog): ALIGNMENT <name>[, <description>[, <station>[, <layer>[, <style>]]]][, TYPE=][, LABELSET=]"},
    {"alignmentadd", "", "Add tangent, curve or spiral: ALIGNMENTADD <name>, TANGENT|TANGENTTO|CURVE|CURVELEN|SPIRAL, ..."},
    {"alignmentlist", "", "Report alignments, stations and entities: ALIGNMENTLIST [<name>]"},
    {"alignmentdelete", "", "Delete an alignment: ALIGNMENTDELETE <name>"},
    {"createviewframes", "", "Create view frames along an alignment: CREATEVIEWFRAMES <alignment>, <start station>, <end station>, <sheet>, <scale or AUTO>, <overlap>[, CENTRED|LEFT|RIGHT]"},
    {"viewframedelete", "", "Delete an alignment's view frame groups: VIEWFRAMEDELETE <alignment>"},
    {"createsheets", "", "Create paper sheets from view frames: CREATESHEETS <alignment>[, <frames>] (frames: ALL or names such as VF-1 VF-3)"},
    {"createsamplelines", "", "Create sample lines along an alignment: CREATESAMPLELINES <alignment>, <start station>, <end station>, <interval>, <left swath>, <right swath>[, <surface>]"},
    {"createsectionsheets", "", "Create section sheets from sample lines: CREATESECTIONSHEETS <alignment>, <sample lines>, <sections per sheet>, <axis elevation>"},
    {"createdesignprofile", "", "Create an empty design profile on an alignment: CREATEDESIGNPROFILE <alignment>, <profile name>"},
    {"profilepvi", "", "Add or edit a vertical point of a design profile: PROFILEPVI <profile>, <station>, <elevation>[, <curve length>]"},
    {"profiledelpvi", "", "Remove a vertical point from a design profile: PROFILEDELPVI <profile>, <station>"},
    {"profilequery", "", "Report a design profile at a station: PROFILEQUERY <profile>, <station>"},
    {"profilelist", "", "List the design profiles in the drawing: PROFILELIST"},
    {"profiledelete", "", "Delete a design profile: PROFILEDELETE <profile>"},
    {"getstationoffset", "", "Station and offset of a point: GETSTATIONOFFSET <alignment>[, <x>, <y> | , POINT, <number>][, SURFACE=<surface>] (alignment alone: pick until ESC)"},
    {"getpointfromstationoffset", "", "Point at a station and offset: GETPOINTFROMSTATIONOFFSET <alignment>, <station>, <offset>"},
    {"createalignmentfromobjects", "createalignmententities", "Alignment from lines/arcs/polylines: CREATEALIGNMENTFROMOBJECTS (pick them) | <name>[, START=][, FILLET=][, REVERSE][, ERASE]"},
    {"alignmentlayout", "", "Lay out an alignment by PIs: ALIGNMENTLAYOUT <name>[, CURVES|NOCURVES][, FROM=<x> <y>][, DIRECTION=<deg>]"},
    {"alignmentcurvesettings", "", "Curve and spiral settings for the layout: ALIGNMENTCURVESETTINGS [RADIUS=][, SPIRALIN=][, SPIRALOUT=]"},
    {"alignmentmeta", "", "Alignment metadata: ALIGNMENTMETA <alignment>, SET, <name>, <value> | REMOVE, <name> | LIST"},
    {"stationformat", "", "How stations are written: STATIONFORMAT [DELIMITER=+|-|_|/|C|K|NONE][, DIGITS=][, DECIMALS=][, PREFIX=][, SUFFIX=] | DEFAULT"},
    {"alignmentedit", "", "Edit an alignment parameter: ALIGNMENTEDIT <name>, <entity#>, BEARING|LENGTH|RADIUS|STARTRADIUS|DELTA|DIRECTION|CONSTRAINT|DEGREE|TANGENTLENGTH|CHORD|EXTERNAL|MIDORDINATE|A, <value> | THROUGH, <x>, <y>"},
    {"alignmentlock", "", "Lock a parameter: ALIGNMENTLOCK <name>, <entity#>, RADIUS|LENGTH|BEARING|STATION, ON|OFF"},
    {"alignmentinsert", "", "Insert a tangent, curve or spiral: ALIGNMENTINSERT <name>, <after#>, TANGENT|CURVE|SPIRAL, ..."},
    {"alignmentremove", "", "Remove an entity: ALIGNMENTREMOVE <name>, <entity#>"},
    {"alignmentcontinuity", "", "Set or list continuity checks: ALIGNMENTCONTINUITY <name>, C0|C1|C2 | LIST"},
    {"reversealignment", "", "Reverse alignment direction and stationing: REVERSEALIGNMENT <name>"},
    {"alignmentstation", "", "Starting station, reference point and station equations: ALIGNMENTSTATION <name>, START|REFERENCE|EQ, ..."},
    {"alignmentstyle", "", "Alignment display styles: ALIGNMENTSTYLE LIST|NEW|DELETE|ASSIGN|SHOW|HIDE|COLOR|SET|CURRENT|MARKER, ..."},
    {"alignmentlabelset", "", "Alignment label sets: ALIGNMENTLABELSET LIST|NEW|DELETE|ASSIGN|MAJORINTERVAL|MINORINTERVAL|SHOW|HIDE|STYLE|GEOMETRY, ..."},
    {"alignmentlabelstyle", "", "Alignment label styles (text style, height, colour, precision, prefix/suffix, orientation): ALIGNMENTLABELSTYLE LIST|NEW|DELETE|SET, ..."},
    {"alignmentreport", "", "Alignment report (text/CSV): ALIGNMENTREPORT [<name>][, TXT|CSV[, <path>]]"},
    {"offsetalignment", "", "Create offset alignments: OFFSETALIGNMENT <parent>, <distance>[, <distance>...] (L12 left, R12 right)"},
    {"alignmentdetach", "", "Make a linked offset independent: ALIGNMENTDETACH <name>"},
    {"alignmentintersect", "", "Intersections: ALIGNMENTINTERSECT <a>, <b> | <a>, LINE, ... | <a>, CIRCLE, ..."},
    {"editalignment", "", "Open the Alignment Layout table: EDITALIGNMENT [<name>]"},
    {"alignmentsurface", "", "Ground elevation along an alignment: ALIGNMENTSURFACE <alignment>, <surface>, STATION=<sta>[, OFFSET=<off>] | INTERVAL=<n>"},
    {"alignmentlabel", "", "Label an alignment's segments (needed for tables): ALIGNMENTLABEL <name>, SEGMENTS | SEGMENT, <entity#> | CLEAR"},
    {"featurelinefromalignment", "", "Feature line from an alignment: FEATURELINEFROMALIGNMENT <alignment>[, INTERVAL=<n>][, TOL=<n>][, SURFACE=<name> | Z=<value>]"},
    {"alignmentprops", "", "Alignment properties: ALIGNMENTPROPS <alignment>, NAME|DESCRIPTION|STYLE|LAYER|START, <value>"},
    {"alignmentinverse", "", "Distance along an alignment: ALIGNMENTINVERSE <alignment>, STA|PT, ... [, CHORD]"},
    {"alignmentbearing", "", "Bearing at a station: ALIGNMENTBEARING <alignment>, <station>"},
    {"alignmentdistance", "", "Distance along an alignment to a point: ALIGNMENTDISTANCE <alignment>, <x>, <y>"},
    {"alignmentcheckset", "", "Design check sets: ALIGNMENTCHECKSET NEW|DELETE|LIST|SET|LOAD|SAVE, ..."},
    {"alignmentcriteria", "", "Apply a check set to an alignment: ALIGNMENTCRITERIA <alignment>, <set>|NONE"},
    {"alignmentspeed", "", "Design speed from a station: ALIGNMENTSPEED <alignment>, <station>, <speed>|CLEAR"},
    {"alignmentcheck", "", "Check an alignment against its design criteria: ALIGNMENTCHECK <alignment>"},
    {"editalignmentstyle", "", "Open the Alignment Style editor: EDITALIGNMENTSTYLE [<style>]"},
    {"alignmenttable", "", "Insert a line, curve, spiral, segment or equation table: ALIGNMENTTABLE <name>, LINE|CURVE|SPIRAL|SEGMENT|EQUATIONS[, STATIC|DYNAMIC][, SPLIT=<rows>][, STYLE=<name>][, <x>, <y>]"},
    {"volreport", "", "Insert MTEXT of the last volume report: VOLREPORT [TABLE]"},
    {"voltable", "", "Insert a TABLE of the last volume report: VOLTABLE"},
    {"volcsv", "", "Write CSV of volume results: VOLCSV [<path>]"},
    {"volumesurface", "volsurf", "Create a TIN volume surface: VOLUMESURFACE <name>, <base>, <comparison>"},
    {"surfacerename", "sfrename", "Rename a surface: SURFACERENAME <old>, <new>"},
    {"surfacedelete", "sfdelete", "Delete a surface: SURFACEDELETE <name>"},
    {"surfacerebuild", "sfrebuild",
     "Rebuild a surface now (all surfaces if no name): SURFACEREBUILD "
     "[<name>]"},
    {"surfacelist", "sflist", "List every surface and its full definition"},
    {"surfacestats", "sfstats", "Surface statistics: SURFACESTATS [<name>]"},
    {"watershed", "wshed", "Watershed basins: WATERSHED [<name>]"},
    {"waterdrop", "wdrop", "Water-drop path: WATERDROP <name>[, <x>, <y>] | WATERDROP EXTRACT"},
    {"catchment", "catch",
     "Catchment at an outlet: CATCHMENT <name>[, <x>, <y>] | CATCHMENT "
     "EXTRACT"},
    {"undesignate", "undes",
     "Remove one definition item: UNDESIGNATE <surface>, "
     "<BREAKLINE|BOUNDARY|POINTFILE|CONTOUR>, <n>"},
    {"surfaceaddfile", "sfaddfile",
     "Link a point file into a surface: SURFACEADDFILE <surface>, <path>[, "
     "<layout>[, HEADER]]"},
    {"surfaceimportfile", "sfimportfile", "Import a linked point file into the drawing and break the link"},
    {"flelev", "",
     "Feature line elevations: FLELEV <n> "
     "[SET|GRADEAHEAD|GRADEBACK|RAISE|INSERT|DELETE …]"},
    {"flelevedit", "", "Open the feature line elevation editor: FLELEVEDIT [<n>]"},
    {"plotscale", "pscale", "Set the plot scale"},
    {"gizmo", "", "Show the 3D gizmo on every selection: GIZMO MOVE | ROTATE | SCALE | OFF"},
    {"3dmove", "", "Move objects with the 3D gizmo (axis handles or a typed distance)"},
    {"3drotate", "", "Rotate objects with the 3D gizmo (ring or a typed angle)"},
    {"3dscale", "", "Scale objects uniformly with the 3D gizmo (handle or a typed factor)"},
    {"move", "m", "Move objects"},
    {"copy", "cp", "Copy objects"},
    {"rotate", "ro", "Rotate objects"},
    {"scale", "sc", "Scale objects"},
    {"mirror", "mi", "Mirror objects across a line"},
    {"array", "ar", "Rectangular or polar array of copies"},
    {"lengthen", "len", "Change an object's length (DElta/Percent/Total/DYnamic)"},
    {"extend", "ex", "Extend objects to a boundary edge"},
    {"break", "br", "Split an object at one or two picked points"},
    {"stretch", "s", "Crossing/window-select, then move only the vertices inside the box"},
    {"fillet", "f", "Round a corner between two curves with a tangent arc (Radius/Trim)"},
    {"chamfer", "cha", "Connect two curves with a straight bevel (Distance/Angle/Trim)"},
    {"delete", "del, erase", "Erase objects"},
    {"chprop", "", "Change the color, layer, linetype or lineweight of selected objects"},
    {"matchprop", "ma, painter", "Copy one object's layer, color, linetype and lineweight onto others"},
    {"laymcur", "", "Make the layer of a picked object current"},
    {"join", "j", "Join collinear objects"},
    {"trim", "tr", "Trim objects to an edge"},
    {"offset", "o", "Offset at a distance"},
    {"zoomextents", "ze", "Zoom to drawing extents"},
    {"zoomwindow", "zw", "Zoom to a window"},
    {"pan", "p", "Pan the view (drag with the left mouse button)"},
    {"ucsfollow", "",
     "0 = changing the UCS leaves the view alone; 1 = it switches to a plan "
     "view"},
    {"ucs", "",
     "Define the User Coordinate System (N names one; restore/delete in the "
     "View Manager)"},
    {"plan", "", "View the XY plane of a coordinate system (does not change the UCS)"},
    {"orbit", "3dorbit, 3do", "Free orbit the model view (drag with the left mouse button)"},
    {"isolateobjects", "isolate", "Hide everything except the selection"},
    {"hideobjects", "", "Hide the selected objects"},
    {"unisolateobjects", "unisolate", "Show every object hidden by isolation"},
    {"vpfreeze", "vpf", "Freeze the picked entities' layers in the current viewport"},
    {"vpthaw", "vpt", "Thaw the picked entities' layers in the current viewport"},
    {"createpoints", "crtpts", "Create survey points"},
    {"viewpoints", "vwpts", "View / edit survey points"},
    {"importpoints", "imppts", "Import survey points"},
    {"exportpoints", "exppts", "Export survey points"},
    {"exportlandxml", "",
     "Export survey points, TIN surfaces and feature lines to LandXML 1.2: "
     "EXPORTLANDXML [<path>]"},
    {"importlandxml", "",
     "Import survey points, TIN surfaces and feature lines from LandXML 1.2: "
     "IMPORTLANDXML [<path>]"},
    {"traverse", "trav, traverseeditor", "Open the LSA Editor"},
    {"select", "", "Build a selection set"},
    {"help", "", "Show command help"},
    {"options", "op, settings", "Open the Options dialog"},
    {"regen", "re", "Regenerate the drawing"},
    {"layer", "la", "Open the Layer manager"},
    {"style", "st, ddstyle", "Text style manager: create / edit named text styles"},
    {"surfstyle", "ss", "Surface style editor: contours, triangles, border (REQ-070)"},
    {"extract", "",
     "Bake a surface's displayed contours into polylines: EXTRACT <surface>[, "
     "<layer>]"},
    {"padsolid", "pad",
     "Cut/fill solids for a pad: select the boundary, then PADSOLID <surface>, "
     "<elevation>"},
    {"gradinggroup", "",
     "Grading group: GRADINGGROUP <name>[, Add/Remove/List] with selected "
     "grading results"},
    {"gradinginfill", "",
     "GRADINGINFILL <group>: add selected closed feature-line interiors to a grading group"},
    {"volumes", "vol",
     "Cut/fill/net volume between two surfaces: VOLUMES <base>, <comparison>[, "
     "<clip id>]"},
    {"voldash", "",
     "Volume Dashboard: live cut/fill/net panel between two surfaces "
     "(REQ-073)"},
    {"units", "un, ddunits", "Drawing units: display precision & angle format"},
    {"drawingsettings", "editdrawingsettings", "Drawing Settings: units, scale and the drawing's settings"},
    {"projectsettings", "",
     "Project Settings: the project's coordinate system, units and defaults "
     "(REQ-375)"},
    {"projecthealth", "",
     "Project Health: linked, missing and unsaved files before you pack or "
     "hand over (REQ-379)"},
    {"packproject", "",
     "Pack Project: write the whole project as one .gspack file to send "
     "(REQ-380)"},
    {"turnover", "",
     "Create Turnover: record which project files were handed to whom, and "
     "when (REQ-381)"},
    {"openpack", "",
     "Open Packed Project: unpack a .gspack into a folder and open it "
     "(REQ-380)"},
    {"adddrawing", "",
     "Add Drawing to Project: bring an existing drawing and its points into "
     "this project (REQ-378)"},
    {"geomarkpoint", "", "Place a Position Marker at a picked point (geolocated drawing)"},
    {"geomarklatlong", "", "Place a Position Marker at a typed latitude and longitude"},
    {"georeorientmarker", "", "Set the geographic marker: a design point, then north"},
    {"pdfattach", "pa", "Attach a PDF underlay"},
    {"pdfview", "pv", "Open a PDF in the built-in viewer (REQ-387)"},
    {"pdfsplit", "", "Save chosen pages of the open PDF as a new PDF (REQ-389)"},
    {"overkill",     "ok", "Remove duplicate geometry"},
    {"align",        "al", "Align objects to others"},
    {"quickselect",  "qs", "Select by object properties"},
    {"view", "v, ddview",
     "Named views: VIEW [Save/Restore/Delete/?] <name>, or VIEW alone for the "
     "View Manager"},
    // SELECTSIMILAR had no registry entry at all (issue 05): the function
    // existed, was declared in the header and was perfectly reachable, but its
    // only caller in the whole codebase was one right-click menu item - so
    // nothing could invoke it by name, users included, and it had zero possible
    // test coverage.
    {"selectsimilar", "sesim", "Select entities matching the current selection's type, layer and colour"},
    {"paste",        "", "Paste from clipboard"},
    {"pasteorig",    "po", "Paste at original coordinates"},
    {"mview",        "rectviewport, rectvp", "Rectangular paper-space viewport (two clicks)"},
    {"mspace",       "ms", "Edit the model through the selected viewport (floating model space)"},
    {"pspace",       "ps", "Return to paper space from a floating viewport"},
    {"block", "",
     "Define a block from the selection: BLOCK <name>, <x>, <y>[, "
     "CONVERT|DELETE|RETAIN]"},
    {"insert", "i",
     "Insert a block (dialog); or INSERT <name>, <x>, <y>[, sx, sy, rotDeg[, "
     "sz, z]]"},
    {"explode", "x", "Explode selected block references"},
    {"bedit", "", "Open the block editor: BEDIT <name>"},
    {"bsave", "", "Save the block being edited"},
    {"bclose", "", "Close the block editor"},
    {"bsaveas", "", "Save the edited block under a new name"},
    {"attdef", "", "Attribute definition (in BEDIT)"},
    {"attedit", "", "Edit attribute values on selected inserts"},
    {"attsync", "", "Synchronize attributes from definitions"},
    {"attext", "", "Extract attributes to the command log / file"},
    {"bconnect", "", "Add a connection port (in BEDIT): pick a face, or type its values"},
    {"bconnectedit", "", "Edit or remove a connection port's role/engagement/tag (in BEDIT)"},
    {"bconnectmode", "", "Add/edit/remove a smart connection mode on a port (in BEDIT)"},
    {"blockfitting", "", "Tag the block being edited as a piping fitting (in BEDIT)"},
    {"blocklist", "", "List block definitions"},
    {"blockstats", "", "Definition statistics"},
    {"purge", "-purge", "Purge unused block definitions"},
    {"wblock", "", "Write a block definition to its own .dwg file"},
    {"blockimport", "", "Import block definitions (.dxf/.dwg/.sat); omit the path to browse"},
    {"blocklib", "blockbrowser", "List the drawing block library with previews"},
    {"blocksearch", "", "Search block names"},
    {"blockfav", "", "Favorite blocks"},
    {"blockrecent", "", "Recently used blocks"},
    {"makeblock", "", "Create an empty block definition: MAKEBLOCK <name>"},
    {"mkline", "", "Add a line: MKLINE x0, y0, x1, y1"},
    {"selline", "", "Select the last line"},
    {"selblock", "", "Select the last block reference"},
    {"movesel", "", "Move selected inserts: MOVESEL dx, dy"},
    {"copysel", "", "Copy selected inserts: COPYSEL dx, dy"},
    {"rotatesel", "", "Rotate selected inserts: ROTATESEL x, y, deg"},
    {"scalesel", "", "Scale selected inserts: SCALESEL x, y, factor"},
    {"mirrorsel", "", "Mirror selected inserts: MIRRORSEL x0, y0, x1, y1"},
    {"copyclip", "", "Copy the selection to the CAD clipboard"},
    {"pasteblock", "", "Paste clipboard block references: PASTEBLOCK dx, dy"},
    {"blockmodel", "", "Switch to model space"},
    {"blockpaper", "", "Switch to the first paper layout"},
};

/// The registry entry whose primary name or alias is exactly \p low (already lowercased), or null.
///
/// One matcher, used by the dispatch loop that STARTS a command and by the failure path that has to
/// recognise a command name it cannot start (GitHub issue #233). Two copies of this would be free to
/// disagree about which words are command names, and the whole point of the failure message is that
/// it agrees with the dispatcher.
[[nodiscard]] const CmdEntry* FindRegistryEntry(const std::string& low) {
  for (const CmdEntry& e : kRegistry) {
    if (low == StringUtil::toLowerAsciiCopy(e.primary))
      return &e;
    if (e.aliases[0] == '\0')
      continue;
    std::istringstream als(std::string(e.aliases));
    std::string a;
    while (std::getline(als, a, ',')) {
      a = StringUtil::trimCopy(a);
      if (!a.empty() && low == StringUtil::toLowerAsciiCopy(a))
        return &e;
    }
  }
  return nullptr;
}

/// Report a line the active command could not use (GitHub issue #233).
///
/// When the line is the NAME of another command, say that — because the honest reason it did nothing
/// is "a command is still running", not "that is not a valid point". A registry lookup here cannot
/// swallow a command's own keywords: this is the failure path, reached only after the active command
/// has already declined the line, so `ARC` inside POLYLINE and `L` inside a solid prompt are consumed
/// long before they reach it.
///
/// Why it matters more than the wording suggests: the two commands people use most are still running
/// at exactly the moment the next command name gets typed. LINE's blank Enter deliberately ends the
/// chain and RESTARTS the command (D-2026-08-25-j), and CIRCLE loops back for the next circle — both
/// chosen, neither wrong — so a user who believes they have finished types the next verb into a live
/// point prompt and is told their coordinate syntax is bad. That reads as a freeze.
void ReportUnparsedCommandInput(const AppCommandState& st, const std::string& line,
                                const std::string& fallback, std::vector<std::string>& log,
                                std::size_t markBeforeHandler) {
  const std::string trimmed = StringUtil::trimCopy(line);
  const CmdEntry* entry =
      trimmed.empty() ? nullptr : FindRegistryEntry(StringUtil::toLowerAsciiCopy(trimmed));
  const char* running = AppCommandState::KindName(st.active);
  if (entry && running && running[0] != '\0') {
    // Name the CANONICAL command rather than echoing the alias back: a user who typed `c` is better
    // served by "then type CIRCLE" than by being shown their own single letter.
    //
    // This half fires whether or not the handler already spoke, and deliberately: it answers a
    // DIFFERENT confusion - "I thought this command had finished" - which the handler's own message
    // about columns or radii does not address.
    std::string wanted = entry->primary;
    for (char& c : wanted)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    log.push_back(std::string(running) + " is still running, so \"" + trimmed +
                  "\" was read as input to it rather than as a command. Press Esc to end " + running +
                  ", then type " + wanted + ".");
    return;
  }
  // The generic fallback is a LAST resort. It is better than silence and worse than anything the
  // handler said in the command's own words (REQ-201), so it is suppressed when the handler already
  // spoke -- which \p markBeforeHandler is how the caller reports.
  //
  // **TASK-225, and the reason this is a rule rather than a per-command patch.** Under a message
  // like "ARRAY Rectangular - number of columns must be a positive whole number", a following
  // "Could not parse ARRAY input - see command hints." adds nothing when the input really was
  // unparseable, and is flatly FALSE when it parsed and was declined on a rule -- `0` columns,
  // a scale factor of `-2`, a mirror line whose two points coincide. Those two cases share one
  // `return false` inside most handlers, so no return value can separate them; what CAN be observed,
  // at the caller and uniformly, is whether the handler already explained itself.
  if (log.size() > markBeforeHandler)
    return;
  log.push_back(fallback);
}

bool DispatchByPrimary(const std::string& primary, AppCommandState& st, std::vector<std::string>& log);

int FuzzySubsequenceScore(std::string_view query, std::string_view cand) {
  if (query.empty())
    return 0;
  size_t qi = 0;
  int score = 0;
  bool prevMatch = false;
  for (size_t i = 0; i < cand.size() && qi < query.size(); ++i) {
    char qc = static_cast<char>(std::tolower(static_cast<unsigned char>(query[qi])));
    char cc = static_cast<char>(std::tolower(static_cast<unsigned char>(cand[i])));
    if (qc == cc) {
      score += 10 + (prevMatch ? 5 : 0);
      prevMatch = true;
      ++qi;
    } else
      prevMatch = false;
  }
  if (qi != query.size())
    return -1;
  score += static_cast<int>(50 - cand.size());
  return score;
}

bool DispatchByPrimary(const std::string& primary, AppCommandState& st, std::vector<std::string>& log) {
  if (primary == "centermark") { ExecuteCenterMarkFromSelection(st, log); return true; }
  if (primary == "centerline") { ExecuteCenterlineFromSelection(st, log); return true; }
  if (primary == "line") {
    StartLineCommand(st, log);
    return true;
  }
  if (primary == "circle") {
    StartCircleCommand(st, log);
    return true;
  }
  if (primary == "polyline") {
    StartPolylineCommand(st, log);
    return true;
  }
  if (primary == "3dpoly") {
    StartPolyline3dCommand(st, log);  // REQ-085
    return true;
  }
  if (primary == "grading") {
    StartGradingCommand(st, log);  // REQ-398
    return true;
  }
  if (primary == "rect") {
    StartRectCommand(st, log);
    return true;
  }
  if (primary == "elev" || primary == "ucs") {
    StartElevCommand(st, log);
    return true;
  }
  if (primary == "trimstate") {
    StartTrimStateCommand(st, log);
    return true;
  }
  if (primary == "arc") {
    StartArcCommand(st, log);
    return true;
  }
  if (primary == "ellipse") {
    StartEllipseCommand(st, log);
    return true;
  }
  if (primary == "spline") {
    StartSplineCommand(st, log);
    return true;
  }
  if (primary == "point") {
    StartPointCommand(st, log);
    return true;
  }
  if (primary == "pdmode") {
    ExecutePdModeCommand(st, std::string(), log);
    return true;
  }
  if (primary == "pdsize") {
    ExecutePdSizeCommand(st, std::string(), log);
    return true;
  }
  if (primary == "leader") {
    StartLeaderCommand(st, log);
    return true;
  }
  if (primary == "pedit") {
    StartPeditCommand(st, log);
    return true;
  }
  if (primary == "measure") {
    StartMeasureCommand(st, log);
    return true;
  }
  if (primary == "divide") {
    StartDivideCommand(st, log);
    return true;
  }
  if (primary == "area") {
    StartAreaCommand(st, log);
    return true;
  }
  if (primary == "list") {
    StartListCommand(st, log);
    return true;
  }
  if (primary == "dataextraction") {
    StartDataExtractCommand(st, log);
    return true;
  }
  if (primary == "table") {
    StartTableCommand(st, log);
    return true;
  }
  if (primary == "addpointtable") {
    StartAddPointTableCommand(st, log);
    return true;
  }
  if (primary == "text") {
    StartTextCommand(st, log);
    return true;
  }
  if (primary == "mtext") {
    StartMtextCommand(st, log);
    return true;
  }
  if (primary == "mleader") {
    StartMleaderCommand(st, log);
    return true;
  }
  if (primary == "dimaligned") {
    StartDimAlignedCommand(st, log);
    return true;
  }
  if (primary == "dimlinear") {
    StartDimLinearCommand(st, log);
    return true;
  }
  if (primary == "dimangular") {
    StartDimAngularCommand(st, log);
    return true;
  }
  if (primary == "dimsty" || primary == "dimstyle" || primary == "dsty") {
    StartDimStyleCommand(st, log);
    return true;
  }
  if (primary == "msty" || primary == "mleaderstyle" || primary == "mlsty") {
    StartMleaderStyleCommand(st, log);
    return true;
  }
  if (primary == "tablestyle" || primary == "tsty") {
    st.showTableStyleDialog = true;
    log.push_back("TABLESTYLE — table style manager opened.");
    return true;
  }
  if (primary == "id") {
    StartIdPointCommand(st, log);
    return true;
  }
  if (primary == "inverse") {
    StartSurveyInverseCommand(st, log);
    return true;
  }
  if (primary == "dist") {
    StartDistCommand(st, log);
    return true;
  }
  if (primary == "padsolid") {
    // Bare PADSOLID: the usage line, so a user who types the name alone is told what it wants
    // rather than met with silence.
    ExecutePadSolidCommand(st, std::string(), log);
    return true;
  }
  if (primary == "extract") {
    ExecuteExtractCommand(st, std::string(), log);
    return true;
  }
  if (primary == "surfstyle") {
    ExecuteSurfStyleCommand(st, std::string(), log);
    return true;
  }
  if (primary == "surfelev") {
    StartSurfaceElevGradeCommand(st, log);
    return true;
  }
  if (primary == "surflodlow") {
    RunSurfaceLodCommand(st, std::string(), true, log);
    return true;
  }
  if (primary == "surflodhigh") {
    RunSurfaceLodCommand(st, std::string(), false, log);
    return true;
  }
  if (primary == "movetosurface") {
    RunMoveToSurfaceCommand(st, std::string(), log);
    return true;
  }
  if (primary == "surfacevisibility") {
    RunSurfaceVisibility(st, std::string(), log);
    return true;
  }
  if (primary == "surfcrossbl") {
    RunSurfaceCrossings(st, std::string(), log);
    return true;
  }
  if (primary == "watershed") {
    RunWatershed(st, std::string(), log);
    return true;
  }
  if (primary == "waterdrop") {
    RunWaterDropCommand(st, std::string(), log);
    return true;
  }
  if (primary == "catchment") {
    RunCatchmentCommand(st, std::string(), log);
    return true;
  }
  if (primary == "plotscale") {
    log.push_back(
        "PLOTSCALE sets drawing units per plotted inch (e.g. 50 for civil 1\"=50'). Usage: PLOTSCALE 50");
    return true;
  }
  if (primary == "regen") {
    BumpCadGpuCache(st);
    log.push_back("REGEN — viewport caches refreshed.");
    return true;
  }
  if (primary == "layer") {
    SyncDrawingLayerTableWithGeometry(st);
    st.showLayerManagerWindow = true;
    log.push_back("LAYER — layer manager opened.");
    return true;
  }
  if (primary == "units") {
    st.showUnitsWindow = true;
    log.push_back("UNITS — drawing units dialog opened.");
    return true;
  }
  // REQ-359: the Geolocation tab's Mark Position and Reorient Marker, also typeable.
  if (primary == "geomarkpoint") {
    StartGeoMarkPointCommand(st, log);
    return true;
  }
  if (primary == "geomarklatlong") {
    StartGeoMarkLatLongCommand(st, log);
    return true;
  }
  if (primary == "georeorientmarker") {
    StartGeoReorientMarkerCommand(st, log);
    return true;
  }
  if (primary == "drawingsettings") {
    // The window itself refuses the GUI's Start tab (REQ-308); headless runs have no Start tab.
    st.showDrawingSettingsWindow = true;
    log.push_back("DRAWINGSETTINGS — Drawing Settings opened.");
    return true;
  }
  if (primary == "projectsettings") {  // REQ-375
    const uint32_t uid = st.activeDrawingIdx >= 1 && st.activeDrawingIdx < static_cast<int>(st.drawingTabs.size())
                             ? st.drawingTabs[static_cast<size_t>(st.activeDrawingIdx)].projectUid
                             : 0u;
    if (uid == 0) {
      log.push_back("PROJECTSETTINGS — this drawing is not in a project.");
    } else {
      st.projectSettingsUid = uid;
      log.push_back("PROJECTSETTINGS — Project Settings opened.");
    }
    return true;
  }
  if (primary == "projecthealth") {  // REQ-379 clause 4
    const uint32_t uid = st.activeDrawingIdx >= 1 && st.activeDrawingIdx < static_cast<int>(st.drawingTabs.size())
                             ? st.drawingTabs[static_cast<size_t>(st.activeDrawingIdx)].projectUid
                             : 0u;
    if (uid == 0) {
      log.push_back("PROJECTHEALTH — this drawing is not in a project.");
    } else {
      st.projectHealthUid = uid;
      log.push_back("PROJECTHEALTH — Project Health opened.");
    }
    return true;
  }
  if (primary == "packproject") {  // REQ-380
    const uint32_t uid = st.activeDrawingIdx >= 1 && st.activeDrawingIdx < static_cast<int>(st.drawingTabs.size())
                             ? st.drawingTabs[static_cast<size_t>(st.activeDrawingIdx)].projectUid
                             : 0u;
    if (uid == 0) {
      log.push_back("PACKPROJECT — this drawing is not in a project.");
    } else {
      st.projectPackPrompt = {};
      st.projectPackPrompt.projectUid = uid;
      log.push_back("PACKPROJECT — Pack Project opened.");
    }
    return true;
  }
  if (primary == "turnover") {  // REQ-381
    const uint32_t uid = st.activeDrawingIdx >= 1 && st.activeDrawingIdx < static_cast<int>(st.drawingTabs.size())
                             ? st.drawingTabs[static_cast<size_t>(st.activeDrawingIdx)].projectUid
                             : 0u;
    if (uid == 0) {
      log.push_back("TURNOVER — this drawing is not in a project.");
    } else {
      st.projectTurnoverPrompt = {};
      st.projectTurnoverPrompt.projectUid = uid;
      log.push_back("TURNOVER — Create Turnover opened.");
    }
    return true;
  }
  if (primary == "openpack") {  // REQ-380
    st.openPackRequested = true;
    log.push_back("OPENPACK — choose the .gspack file to open.");
    return true;
  }
  if (primary == "adddrawing") {  // REQ-378
    const uint32_t uid = st.activeDrawingIdx >= 1 && st.activeDrawingIdx < static_cast<int>(st.drawingTabs.size())
                             ? st.drawingTabs[static_cast<size_t>(st.activeDrawingIdx)].projectUid
                             : 0u;
    if (uid == 0) {
      log.push_back("ADDDRAWING — open a drawing of the project first.");
    } else {
      st.addDrawingToProjectUid = uid;
      log.push_back("ADDDRAWING — choose the drawing to add.");
    }
    return true;
  }
  if (primary == "style") {
    TextStyles::EnsureStandard(st.drawing.textStyles);
    st.showTextStyleManagerWindow = true;
    log.push_back("STYLE — text style manager opened.");
    return true;
  }
  if (primary == "move") {
    StartMoveCommand(st, log);
    return true;
  }
  if (primary == "copy") {
    StartCopyCommand(st, log);
    return true;
  }
  if (primary == "rotate") {
    StartRotateCommand(st, log);
    return true;
  }
  if (primary == "scale") {
    StartScaleCommand(st, log);
    return true;
  }
  // GitHub issue #564 section 3: the gizmo, summoned by a command.
  if (primary == "3dmove") {
    StartGizmoCommand(st, AppCommandState::Kind::Move3d, log);
    return true;
  }
  if (primary == "3drotate") {
    StartGizmoCommand(st, AppCommandState::Kind::Rotate3d, log);
    return true;
  }
  if (primary == "3dscale") {
    StartGizmoCommand(st, AppCommandState::Kind::Scale3d, log);
    return true;
  }
  if (primary == "mirror") {
    StartMirrorCommand(st, log);
    return true;
  }
  if (primary == "array") {
    StartArrayCommand(st, log);
    return true;
  }
  if (primary == "lengthen") {
    StartLengthenCommand(st, log);
    return true;
  }
  if (primary == "extend") {
    StartExtendCommand(st, log);
    return true;
  }
  if (primary == "break") {
    StartBreakCommand(st, log);
    return true;
  }
  if (primary == "stretch") {
    StartStretchCommand(st, log);
    return true;
  }
  if (primary == "fillet") {
    StartFilletCommand(st, log);
    return true;
  }
  if (primary == "chamfer") {
    StartChamferCommand(st, log);
    return true;
  }
  if (primary == "delete") {
    StartDeleteCommand(st, log);
    return true;
  }
  // REQ-356 (GitHub issue #575): the property commands (MA / PAINTER alias MATCHPROP above).
  if (primary == "chprop") {
    StartChPropCommand(st, log);
    return true;
  }
  if (primary == "matchprop") {
    StartMatchPropCommand(st, log);
    return true;
  }
  if (primary == "laymcur") {
    StartLayMCurCommand(st, log);
    return true;
  }
  if (primary == "join") {
    StartJoinCommand(st, log);
    return true;
  }
  if (primary == "trim") {
    StartTrimCommand(st, log);
    return true;
  }
  if (primary == "offset") {
    StartOffsetCommand(st, log);
    return true;
  }
  if (primary == "hatch" || primary == "bhatch" || primary == "h") {
    StartHatchCommand(st, log);
    return true;
  }
  if (primary == "zoomextents") {
    StartZoomExtentsCommand(st, log);
    return true;
  }
  if (primary == "zoomwindow") {
    StartZoomWindowCommand(st, log);
    return true;
  }
  if (primary == "pan") {
    StartPanCommand(st, log);
    return true;
  }
  // REQ-084 (c): the shortcut menu's view + isolation entries are real commands, so they are also
  // typeable. AutoCAD's aliases are kept so muscle memory carries over.
  if (primary == "orbit" || primary == "3dorbit" || primary == "3do") {
    StartOrbitCommand(st, log);
    return true;
  }
  // UCS / PLAN (REQ-154), bare. The inline forms (`UCS W`, `PLAN World`) are handled where the
  // argument is still in hand, and they open the same prompt before feeding it — so there is one
  // keyword parser, not a second one that could disagree with the prompt about what `W` means.
  if (primary == "ucs") {
    StartUcsCommand(st, log);
    return true;
  }
  if (primary == "plan") {
    StartPlanCommand(st, log);
    return true;
  }
  if (primary == "isolateobjects" || primary == "isolate") {
    IsolateSelectedObjects(st, log);
    return true;
  }
  if (primary == "hideobjects") {
    HideSelectedObjects(st, log);
    return true;
  }
  if (primary == "unisolateobjects" || primary == "unisolate") {
    EndObjectIsolation(st, log);
    return true;
  }
  if (primary == "vpfreeze") {
    StartVpFreezeCommand(st, log);
    return true;
  }
  if (primary == "vpthaw") {
    StartVpThawCommand(st, log);
    return true;
  }
  if (primary == "createpoints") {
    StartCreatePointsCommand(st, log);
    return true;
  }
  if (primary == "viewpoints") {
    StartViewPointsCommand(st, log);
    return true;
  }
  if (primary == "importpoints") {
    StartImportPointsCommand(st, log);
    return true;
  }
  if (primary == "exportpoints") {
    StartExportPointsCommand(st, log);
    return true;
  }
  if (primary == "traverse" || primary == "trav" || primary == "traverseeditor") {
    StartTraverseEditorCommand(st, log);
    return true;
  }
  if (primary == "pdfview") {  // REQ-387: the built-in PDF viewer; the window shows the file dialog
    st.pdfViewerPickRequest = true;
    log.push_back("PDFVIEW — choose a PDF to open in the viewer.");
    return true;
  }
  if (primary == "pdfsplit") {  // REQ-389: the viewer window shows the Split dialog
    st.pdfSplitRequest = true;
    return true;
  }
  if (primary == "pdfattach" || primary == "pdfatt") {
    StartPdfAttachCommand(st, log);
    return true;
  }
  if (primary == "select") {
    ClearPendingViewportZoom(st);
    ClearSelection(st);
    st.selBoxWaitingSecond = false;
    log.push_back("SELECT — click two corners for a window (default when no command is active).");
    return true;
  }
  if (primary == "options" || primary == "op" || primary == "settings") {
    StartOptionsCommand(st, log);
    return true;
  }
  if (primary == "help") {
    log.push_back(
        "LINE (L), POLYLINE (PL, CLOSE to close), ARC (3-point), ELLIPSE (center, axis end, ratio), TEXT, MTEXT, "
        "DIMALIGNED (DAL), DIMLINEAR (DLI), DIMANGULAR (DAN), ID, INVERSE (INV), CIRCLE (C), MOVE (M), COPY (CP), ROTATE (RO), SCALE (SC), DELETE (DEL), OFFSET (O), ZOOM (ZE/ZW), "
        "OVERKILL (OK), ALIGN (AL), PLOTSCALE "
        "(PSCALE), REGEN (RE), LAYER (LA). SURVEY: CRTPTS, VWPTS, IMPPTS, EXPPTS, INVERSE (INV). Idle: two-click box selects. ESC.");
    log.push_back(
        "LINE: @dx,dy from anchor; A or ANGLE alone then bearing on next line (blank Enter cancels); A<bearing> (+ "
        "optional +90) on one line; 2P (=AP) + two picks then Enter (or +90) locks bearing; distance (+/-) along ray; A "
        "clears when lock or 2P pick is active. Ortho: distance toward cursor.");
    log.push_back(
        "ROTATE: ° clockwise from north / DMS; R reference; then bearing or P. SCALE: after base, factor or pick from "
        "base; R / REFERENCE then two-point ref length, then new length (type or two picks). INVERSE: two points "
        "(World X=E, Y=N); logs dE, dN, distance, bearing in deg/min/sec and decimal deg CW from north. DELETE / ZW use unsnapped "
        "windows. TRIM "
        "matches Civil 3D: cutting edges, Enter, trim clicks. OFFSET: pick entity, distance + side or through-click "
        "(line/circle/arc). ZE fits geometry. OVERKILL: remove zero-length/duplicate/overlapping lines, merge "
        "collinear overlapping lines, remove arcs over circles, deduplicate circles and arcs — operates on entire drawing.");
    return true;
  }
  if (primary == "overkill") {
    ExecuteOverkill(st, log);
    return true;
  }
  if (primary == "align") {
    StartAlignCommand(st, log);
    return true;
  }
  if (primary == "quickselect" || primary == "qs") {
    StartQuickSelectCommand(st, log);
    return true;
  }
  if (primary == "selectsimilar" || primary == "sesim") {
    SelectSimilarToCurrentSelection(st, &log);
    return true;
  }
  if (primary == "paste") {
    StartPasteCommand(st, log);
    return true;
  }
  if (primary == "pasteorig" || primary == "po") {
    StartPasteOrigCommand(st, log);
    return true;
  }
  if (primary == "mview" || primary == "rectviewport" || primary == "rectvp") {
    StartPaperRectViewportCommand(st, log);
    return true;
  }
  if (primary == "mspace" || primary == "ms") {
    if (st.activeSpaceIndex != kModelSpaceIndex && st.selectedViewportIndex >= 0)
      EnterFloatingModelSpace(st, st.activeSpaceIndex, st.selectedViewportIndex, log);
    else
      log.push_back("MSPACE — select a viewport in a paper layout first.");
    return true;
  }
  if (primary == "pspace" || primary == "ps") {
    if (InFloatingModelSpace(st))
      ExitFloatingModelSpace(st, log);
    else
      log.push_back("PSPACE — not in a floating viewport.");
    return true;
  }
  if (primary == "blockimport") {
    CadBlocksImportWithPicker(st, log);
    return true;
  }
  if (primary == "insert") {
    StartInsertBlockCommand(st, log);
    return true;
  }
  return false;
}


// Shared with CREATEVIEWFRAMES (REQ-517.6): true when the active tab's project is read-only.
} // namespace
bool ActiveProjectPointsReadOnly(const AppCommandState& st) {
  const int tab = st.activeDrawingIdx;
  if (tab <= 0 || tab >= static_cast<int>(st.drawingTabs.size())) return false;
  const auto& drawing = st.drawingTabs[static_cast<size_t>(tab)];
  if (drawing.projectUid == 0 || drawing.pointsMode == AppCommandState::DrawingTab::PointsMode::Detached) return false;
  for (const auto& project : st.openProjects)
    if (project.uid == drawing.projectUid) return project.readOnly;
  return false;
}


/// Every primary command name in `kRegistry` (issue #785: a merge can silently drop a handler
/// without dropping its registry entry, which is otherwise invisible — the command just goes
/// silent). Declared in `CadCommands.hpp` so the regression test can enumerate the registry
/// without duplicating it.
std::vector<std::string> RegisteredCommandPrimariesForTest() {
  std::vector<std::string> out;
  out.reserve(std::size(kRegistry));
  for (const CmdEntry& e : kRegistry)
    out.emplace_back(e.primary);
  return out;
}

/// Runs `DispatchByPrimary` for a name taken from `kRegistry` and reports whether anything claimed
/// it, the same way the real command line is caught/let-through (issue #785).
bool DispatchByPrimaryForTest(const std::string& primary, AppCommandState& st, std::vector<std::string>& log) {
  return DispatchByPrimary(primary, st, log);
}



namespace OffsetCmd {
static void HandlePaperOffsetViewportPick(AppCommandState& st, double wx, double wy, std::vector<std::string>& log) {
  using OP = AppCommandState::OffsetPhase;
  if (st.offsetPaperLayoutIndex >= 0 && st.activeSpaceIndex != st.offsetPaperLayoutIndex) {
    log.push_back("OFFSET — return to the paper layout where the object was picked, or cancel OFFSET.");
    return;
  }
  if (st.offsetPhase == OP::WaitDistanceOrThrough) {
    log.push_back("OFFSET — type a positive offset distance, or T for through-point mode.");
    return;
  }
  if (st.offsetPhase == OP::WaitSelectEntity) {
    if (st.activeSpaceIndex < 0 || static_cast<size_t>(st.activeSpaceIndex) >= st.drawing.paperLayouts.size()) {
      log.push_back("OFFSET — no active paper layout.");
      return;
    }
    PaperRef hit{};
    const PaperLayout& layout = st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
    float tolerance = 0.05f;
    if (st.viewportLastSurveyLayoutHeightPx > 1.f && st.viewportLastSurveyLayoutOrthoHalfH > 0.f)
      tolerance = std::clamp(CadSnap::WorldToleranceFromPixels(
          st.viewportLastSurveyLayoutHeightPx, st.viewportLastSurveyLayoutOrthoHalfH, st.objectSnapAperturePx),
          0.01f, 0.25f);
    if (!PickPaperEntityAt(layout, static_cast<float>(wx), static_cast<float>(wy), tolerance, &hit, nullptr)) {
      log.push_back("OFFSET — nothing under cursor; try again.");
      return;
    }
    SelectedEntity::Type type{};
    switch (hit.type) {
    case PaperRef::Type::Line: type = SelectedEntity::Type::LineSeg; break;
    case PaperRef::Type::Circle: type = SelectedEntity::Type::Circle; break;
    case PaperRef::Type::Arc: type = SelectedEntity::Type::Arc; break;
    case PaperRef::Type::Ellipse: type = SelectedEntity::Type::Ellipse; break;
    case PaperRef::Type::Polyline: type = SelectedEntity::Type::Polyline; break;
    case PaperRef::Type::Text:
      log.push_back("OFFSET — paper text cannot be offset. Pick a line, circle, arc, ellipse, or polyline.");
      return;
    case PaperRef::Type::Block:
      log.push_back("OFFSET — paper blocks cannot be offset. Pick a line, circle, arc, ellipse, or polyline.");
      return;
    }
    st.offsetEntity = {type, hit.index};
    st.offsetEntityValid = true;
    st.offsetPaperLayoutIndex = st.activeSpaceIndex;
    if (st.offsetThroughMode) {
      if (type == SelectedEntity::Type::Ellipse || type == SelectedEntity::Type::Polyline) {
        log.push_back("OFFSET — through-point mode does not support paper ellipses or polylines. Pick a line, circle, or arc.");
        st.offsetEntityValid = false;
        st.offsetPaperLayoutIndex = -1;
        return;
      }
      st.offsetPhase = OP::WaitThroughPick;
      log.push_back("OFFSET — click the through point (line / circle / arc).");
    } else {
      st.offsetPhase = OP::WaitSidePick;
      log.push_back("OFFSET — pick which side of the object to offset.");
    }
    return;
  }
  if (st.offsetPhase == OP::WaitThroughPick || st.offsetPhase == OP::WaitSidePick)
    CommitPaperOffset(st, wx, wy, log);
}
} // namespace OffsetCmd

namespace {
// Returns true when the bearing-pick state fully consumed the click (caller must return).
// When false, the caller should call SubmitLineVertex / SubmitPolylineVertex with the
// (possibly angle-locked or ortho-clamped) wx/wy.
static bool ApplySegmentAnglePickToViewportPick(AppCommandState& st, double& wx, double& wy,
                                                bool inNextPtPhase, std::vector<std::string>& log) {
  using SAP = AppCommandState::SegmentAnglePickPhase;
  if (!inNextPtPhase) return false;
  if (st.segmentAngleKeyboardAwaitBearing) {
    log.push_back("Finish bearing entry on the command line (blank Enter cancels) before viewport picks.");
    return true;
  }
  if (st.segmentAnglePickPhase == SAP::WaitP1) {
    st.segmentPickRefX1 = static_cast<float>(wx); st.segmentPickRefY1 = static_cast<float>(wy);
    st.segmentAnglePickPhase = SAP::WaitP2;
    log.push_back("Bearing pick — second reference point:");
    return true;
  }
  if (st.segmentAnglePickPhase == SAP::WaitP2) {
    const float dx = static_cast<float>(wx - st.segmentPickRefX1), dy = static_cast<float>(wy - st.segmentPickRefY1);
    if (std::hypot(dx, dy) < 1e-8f)
      log.push_back("Bearing pick — points coincide; pick again.");
    else {
      st.segmentPickDraftBearingDeg = BearingCwNorthDegFromMathAngleRad(std::atan2(dy, dx));
      st.segmentAnglePickPhase = SAP::WaitAdjustOrCommit;
      log.push_back("Bearing from picks — Enter locks as-is; type +90 / -45 (° CW from N) to adjust and lock (one line).");
    }
    return true;
  }
  if (st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit) {
    log.push_back("Bearing pick — press Enter to lock (or type +90 / -45); viewport click ignored in this step.");
    return true;
  }
  if (st.segmentAngleLockActive) {
    // These constraint helpers work in `float` (an axis/ray-locked pick is a computed, pixel-bounded
    // point — REQ-101 scopes picks out of ±0.002 ft — and object snap, the bit-identical path, is
    // mutually exclusive with an active angle lock). Narrow across the call and back.
    float lwx = static_cast<float>(wx), lwy = static_cast<float>(wy);
    ApplySegmentAngleLockToWorldPick(st.anchorX, st.anchorY, st.segmentLockUx, st.segmentLockUy, &lwx, &lwy, false);
    wx = lwx;
    wy = lwy;
  } else {
    // no-op when ORTHO off (REQ-047); anchorZ/the cursor's resolved Z are passed so a tilted or
    // edge-on UCS plane constrains correctly (issue #371) instead of guessing Z from x,y.
    //
    // The Z lock comes back through st.resolvedPointZ (issue #371 follow-up): squaring to a
    // Front/Left/Right-style UCS's vertical axis locks world Z to the anchor's, and
    // CadCommitElevation() reads resolvedPointZ for the vertex this pick is about to commit. Without
    // updating it here, the commit would read the cursor's raw (unlocked) elevation and the geometry
    // would land off the ORTHO line wx/wy just reported.
    float owx = static_cast<float>(wx), owy = static_cast<float>(wy);
    ApplyOrthoConstrainFromAnchor(st, st.anchorX, st.anchorY, &owx, &owy, st.orthoMode, st.anchorZ,
                                  st.resolvedPointZ, &st.resolvedPointZ);
    wx = owx;
    wy = owy;
    st.resolvedPointZValid = true;
  }
  return false;
}


void SubmitViewportPickImpl(AppCommandState& st, double wx, double wy, std::vector<std::string>& log,
                             bool windowSelectionSubtract, bool fenceLeftToRightWindowMode,
                             const ray3d::Ray* pickRay) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using RP = AppCommandState::RotatePhase;
  using SP = AppCommandState::ScalePhase;
  using MirP = AppCommandState::MirrorPhase;

  if (st.active == K::PaperRectViewport) {
    if (st.activeSpaceIndex < 0 || static_cast<size_t>(st.activeSpaceIndex) >= st.drawing.paperLayouts.size()) {
      log.push_back("MVIEW — switch to a paper layout first.");
      st.active = K::None;
      return;
    }
    if (st.paperVpMode == 1) {
      PaperRef hit{};
      const PaperLayout& layout = st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
      if (!PickPaperEntityAt(layout, static_cast<float>(wx), static_cast<float>(wy),
                             std::max(0.12f, CadOffsetEntityPickTolWorld(st)), &hit, &st.drawing.blockDefs) ||
          hit.type != PaperRef::Type::Polyline) {
        log.push_back("MVIEW Object — pick a closed paper-space polyline.");
        return;
      }
      if (static_cast<size_t>(hit.index) >= layout.paperPolyClosed.size() ||
          layout.paperPolyClosed[static_cast<size_t>(hit.index)] == 0) {
        log.push_back("MVIEW Object — the selected polyline is open.");
        return;
      }
      if (AddViewportFromPaperPolyline(st, st.activeSpaceIndex, hit.index, log)) {
        st.active = K::None;
        st.paperVpMode = 0;
        st.paperVpPhase = 0;
      }
      return;
    }
    const float x = static_cast<float>(wx), y = static_cast<float>(wy);
    if (st.paperVpPhase == 0) {
      st.paperVpFirstXIn = x;
      st.paperVpFirstYIn = y;
      st.paperVpPhase = 1;
      log.push_back("MVIEW — click the opposite corner, or type [Fit/2/3/4/Object].");
      return;
    }
    if (std::fabs(x - st.paperVpFirstXIn) < 0.1f || std::fabs(y - st.paperVpFirstYIn) < 0.1f) {
      log.push_back("MVIEW — viewport corners must be at least 0.1 in apart; pick the opposite corner again.");
      return;
    }
    PushUndoSnapshot(st, "Create MVIEW viewport");
    AddViewportRect(st, st.activeSpaceIndex, st.paperVpFirstXIn, st.paperVpFirstYIn, x, y);
    st.active = K::None;
    st.paperVpPhase = 0;
    st.paperVpMode = 0;
    log.push_back("MVIEW — rectangular viewport created.");
    return;
  }

  // UCS takes its picks here (REQ-154), before anything else looks at them: an origin pick must not
  // also start a selection. It works in WORLD coordinates because a coordinate frame is a
  // world-space object — see CadActiveUcsStorage for why that distinction is load-bearing.
  //
  // The Z comes from CadCommitElevation, so an object snap places the UCS origin at the snapped
  // point's own elevation rather than flattening it onto the current work plane.
  if (st.active == K::Ucs) {
    double ucsWx = 0.;
    double ucsWy = 0.;
    CadCoord::WorldFromLocal(st, static_cast<float>(wx), static_cast<float>(wy), &ucsWx, &ucsWy);
    if (ProcessUcsViewportPick(st, {ucsWx, ucsWy, static_cast<double>(CadCommitElevation(st))}, log, pickRay))
      return;
  }

  // TRIM has a dedicated picker because its click semantics differ from coordinate commands.
  // Keep the Qt-only frontend on that same tested entry point in both spaces.
  if (st.active == K::Trim) {
    (void)SubmitTrimViewportPick(st, static_cast<float>(wx), static_cast<float>(wy),
                                 CadOffsetEntityPickTolWorld(st), log, pickRay);
    return;
  }
  if (st.active == K::PdfAttach) {
    using PAP = AppCommandState::PdfAttachPhase;
    if (st.pdfAttachPhase == PAP::WaitInsertPoint)
      SubmitPdfAttachInsertPoint(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }
  if (st.active == K::InsertBlock) {
    using IPh = AppCommandState::InsertBlockPhase;
    if (st.insertBlockPhase == IPh::WaitInsertPoint || st.insertBlockPhase == IPh::WaitScale ||
        st.insertBlockPhase == IPh::WaitRotation || st.insertBlockPhase == IPh::WaitAlignFace) {
      float wz = CadCommitElevation(st);
      if (st.viewportSnapPickValid)
        wz = st.viewportSnapPickLocalZ;
      SubmitInsertBlockPick(st, static_cast<float>(wx), static_cast<float>(wy), wz, log);
    }
    return;
  }
  if (st.active == K::None && st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st) &&
      HandlePaperModifyPick(st, static_cast<float>(wx), static_cast<float>(wy), log))
    return;

  // Box-selection projects to screen space only when the view is actually orbited; in plan view a
  // null camera keeps the historical world-rect test byte-for-byte (REQ-058).
  const Camera boxSelCamValue = CadViewCamera(st);
  const Camera* const boxSelCam = CadViewIsPlan(st) ? nullptr : &boxSelCamValue;
  auto finishBox = [&]() {
    if (PaperLayout* paper = ActivePaperGeometryTarget(st)) {
      const float mnX = std::min(st.selBoxAnchorX, static_cast<float>(wx));
      const float mxX = std::max(st.selBoxAnchorX, static_cast<float>(wx));
      const float mnY = std::min(st.selBoxAnchorY, static_cast<float>(wy));
      const float mxY = std::max(st.selBoxAnchorY, static_cast<float>(wy));
      std::vector<PaperEntityRef> hits;
      SelectPaperEntitiesInBox(*paper, mnX, mnY, mxX, mxY, fenceLeftToRightWindowMode, hits, &st.drawing.blockDefs);
      if (windowSelectionSubtract) {
        st.selectedPaperEntities.erase(
            std::remove_if(st.selectedPaperEntities.begin(), st.selectedPaperEntities.end(),
                           [&](const PaperEntityRef& selected) {
                             return std::find_if(hits.begin(), hits.end(), [&](const PaperEntityRef& hit) {
                                      return hit.type == selected.type && hit.index == selected.index;
                                    }) != hits.end();
                           }),
            st.selectedPaperEntities.end());
      } else {
        for (const PaperEntityRef& hit : hits) {
          const bool alreadySelected = std::any_of(
              st.selectedPaperEntities.begin(), st.selectedPaperEntities.end(), [&](const PaperEntityRef& selected) {
                return selected.type == hit.type && selected.index == hit.index;
              });
          if (!alreadySelected)
            st.selectedPaperEntities.push_back(hit);
        }
      }
      st.paperSelBoxLastValid = true;
      st.paperSelBoxLastMnXIn = mnX;
      st.paperSelBoxLastMxXIn = mxX;
      st.paperSelBoxLastMnYIn = mnY;
      st.paperSelBoxLastMxYIn = mxY;
      st.selBoxWaitingSecond = false;
      log.push_back(std::string(fenceLeftToRightWindowMode ? "Window" : "Fence") + " — CAD " +
                    std::to_string(hits.size()) + ", survey 0 (" +
                    (fenceLeftToRightWindowMode ? "window" : "crossing") + ").");
      return;
    }
    const bool inclSurvey = (st.active == AppCommandState::Kind::None || st.active == K::Move ||
                             st.active == K::Copy || st.active == K::Rotate || st.active == K::Scale ||
                             st.active == K::Mirror || st.active == K::Align || st.active == K::Stretch ||
                             // REQ-305: included so DropArrayUnsupportedFromSelection can log the
                             // exclusion by name (REQ-201) rather than silently never selecting them.
                             st.active == K::Array);
    ComputeSelectionFromRect(st, st.selBoxAnchorX, st.selBoxAnchorY, st.selBoxAnchorZ, static_cast<float>(wx), static_cast<float>(wy),
                             st.uiCursorWorldZ, windowSelectionSubtract, fenceLeftToRightWindowMode,
                             inclSurvey, boxSelCam, st.uiViewportWidthPx, st.uiViewportHeightPx);
    if (st.active == K::Stretch) {
      if (CadWorkPlaneIsWorldXy(st)) {
        // Plain world XY, not camera-projected — REQ-103 STRETCH's stated simplification; entity
        // CANDIDACY above still goes through ComputeSelectionFromRect's own camera-aware test.
        st.stretchRectMnX = static_cast<float>(std::min<double>(st.selBoxAnchorX, wx));
        st.stretchRectMxX = static_cast<float>(std::max<double>(st.selBoxAnchorX, wx));
        st.stretchRectMnY = static_cast<float>(std::min<double>(st.selBoxAnchorY, wy));
        st.stretchRectMxY = static_cast<float>(std::max<double>(st.selBoxAnchorY, wy));
        st.stretchRectInUcsPlane = false;
      } else {
        // REQ-329 increment 4: the crossing box is drawn ON the active work plane, so store it in
        // that plane's own local 2D frame. Each corner's world Z rides along on selBoxAnchorZ /
        // uiCursorWorldZ (set by BeginSelectionBoxCorner and the headless CLICKUCS box path), so the
        // world point is recoverable and WorldToPlane gives its (u, v). `ApplyStretchToSelection`
        // projects every candidate vertex the same way before the box test.
        const ucs::Ucs frame = CadActiveUcsStorage(st);
        const ucs::Point2D c1 = ucs::WorldToPlane(
            frame, {static_cast<double>(st.selBoxAnchorX), static_cast<double>(st.selBoxAnchorY),
                    static_cast<double>(st.selBoxAnchorZ)});
        const ucs::Point2D c2 = ucs::WorldToPlane(
            frame, {static_cast<double>(wx), static_cast<double>(wy),
                    static_cast<double>(st.uiCursorWorldZ)});
        st.stretchRectMnX = static_cast<float>(std::min(c1.x, c2.x));
        st.stretchRectMxX = static_cast<float>(std::max(c1.x, c2.x));
        st.stretchRectMnY = static_cast<float>(std::min(c1.y, c2.y));
        st.stretchRectMxY = static_cast<float>(std::max(c1.y, c2.y));
        st.stretchRectInUcsPlane = true;
      }
    }
    st.selBoxWaitingSecond = false;
    // A completed fence IS an entity selection, so it takes the other side of REQ-318 item 9's
    // mutual exclusion: sub-objects go. Here rather than only in the viewport's click handler
    // because this is the shared path a transcript can drive — the clear that lives in `CadUi.cpp`
    // covers the plain CLICK, which has no headless equivalent at all.
    st.subObjectSelection.clear();
    log.push_back("Fence — CAD " + std::to_string(st.selection.size()) + ", survey " +
                  std::to_string(st.selectedSurveyPointIndices.size()) +
                  (fenceLeftToRightWindowMode ? " (window)." : " (crossing)."));
  };

  if (st.active == K::Line) {
    using LP = AppCommandState::LinePhase;
    const bool nextPt = st.linePhase == LP::NeedNextPoint;
    if (!ApplySegmentAnglePickToViewportPick(st, wx, wy, nextPt, log))
      SubmitLineVertex(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Polyline) {
    using PP = AppCommandState::PolylinePhase;
    const bool nextPt = st.polylinePhase == PP::NeedNextPoint;
    // REQ-303 (issue #80): a click that landed exactly on the polyline's own start point closes it,
    // mirroring the typed CLOSE keyword below rather than adding a duplicate vertex there. Exact
    // equality is safe, not fragile: wx/wy arrive here as CadSnap::FindBest's Hit verbatim (CadUi.cpp's
    // commitX/commitY = viewportSnapPickLocalX/Y = snap.x/y, copied through with no arithmetic), and
    // FindBest only offers this candidate by reading these same two stored floats.
    if (nextPt && !st.polylineDraftVerts.empty() && wx == st.polyFirstX && wy == st.polyFirstY) {
      if (st.polyDraftSegments == 0)
        log.push_back("POLYLINE CLOSE — need at least one segment after the start point.");
      else
        CommitPolylineDraft(st, /*closed=*/true, log);
      return;
    }
    if (!ApplySegmentAnglePickToViewportPick(st, wx, wy, nextPt, log))
      SubmitPolylineVertex(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  // REQ-398. GRADING's first phase is a PICK — the baseline feature line — so it belongs in this
  // list for exactly the reason TASK-082's comment below gives: a click that reaches no branch is
  // discarded in silence and the command looks hung. A transcript cannot catch that, because a
  // transcript types and never clicks.
  if (st.active == K::Grading &&
      st.gradingPhase == AppCommandState::GradingPhase::WaitBaseline) {
    HandleGradingPick(st, wx, wy, log);
    return;
  }

  // REQ-087 / TASK-082 BUG-1. Missing entirely until 2026-08-20, so every click during FEATURELINE
  // was silently discarded and the command appeared to hang on its first prompt — the exact failure
  // the comment above CadUi.cpp's point-picking list warns about. A transcript could not catch it
  // because a transcript types coordinates and never clicks.
  //
  // A click carries X and Y only, so it goes to SubmitFeatureLinePoint, which prompts for the
  // elevation rather than taking one from the work plane the way POLYLINE does.
  if (st.active == K::FeatureLine) {
    if (st.featureLinePendingPoint) {
      // A second click while an elevation is owed. Move the pending point to where they clicked
      // rather than ignoring it: the click is unambiguous, and silently dropping it would repeat
      // BUG-1 in miniature.
      st.featureLinePendingX = static_cast<float>(wx);
      st.featureLinePendingY = static_cast<float>(wy);
      char buf[192];
      std::snprintf(buf, sizeof(buf),
                    "FEATURELINE — point moved. Elevation for %s %zu <%.3f>:",
                    st.featureLineNextIsElevPoint ? "elevation point" : "point",
                    st.featureLineDraftElevPt.size() + 1,
                    static_cast<double>(st.featureLinePendingDefaultZ));
      log.push_back(buf);
      return;
    }
    SubmitFeatureLinePoint(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Rect) {
    using RectP = AppCommandState::RectPhase;
    if (st.rectPhase == RectP::WaitFirstCorner) {
      st.rectX1 = static_cast<float>(wx);
      st.rectY1 = static_cast<float>(wy);
      st.rectPhase = RectP::WaitSecondCorner;
      // The anchor is the base for relative (@dx,dy) entry, exactly as it is for LINE.
      st.anchorX = static_cast<float>(wx);
      st.anchorY = static_cast<float>(wy);
      log.push_back("RECT — pick the opposite corner (or type X,Y / @dx,dy):");
    } else {
      CommitRectangle(st, st.rectX1, st.rectY1, static_cast<float>(wx), static_cast<float>(wy), log);
    }
    return;
  }

  if (st.active == K::Arc) {
    using AP = AppCommandState::ArcPhase;
    switch (st.arcPhase) {
    case AP::WaitStart:
      st.arcAx = static_cast<float>(wx);
      st.arcAy = static_cast<float>(wy);
      st.arcAz = CadCommitElevation(st);
      st.arcPhase = AP::WaitMid;
      log.push_back("ARC — pick middle point on arc:");
      break;
    case AP::WaitMid:
      st.arcBx = static_cast<float>(wx);
      st.arcBy = static_cast<float>(wy);
      st.arcBz = CadCommitElevation(st);
      st.arcPhase = AP::WaitEnd;
      log.push_back("ARC — pick end point:");
      break;
    case AP::WaitEnd:
      CommitArcThreePoints(st, st.arcAx, st.arcAy, st.arcAz, st.arcBx, st.arcBy, st.arcBz, static_cast<float>(wx), static_cast<float>(wy),
                           CadCommitElevation(st), log);
      break;
    }
    return;
  }

  if (st.active == K::PadSolid) {
    if (st.padSolidPhase == AppCommandState::PadSolidPhase::WaitBoundary)
      HandlePadSolidPick(st, wx, wy, log);
    return;
  }

  if (st.active == K::Ellipse) {
    using EP = AppCommandState::EllipsePhase;
    switch (st.ellPhase) {
    case EP::WaitCenter:
      st.ellCx = static_cast<float>(wx);
      st.ellCy = static_cast<float>(wy);
      st.ellCz = CadCommitElevation(st);
      st.ellPhase = EP::WaitMajorEnd;
      log.push_back("ELLIPSE — major axis endpoint:");
      break;
    case EP::WaitMajorEnd:
      st.ellMajEx = static_cast<float>(wx);
      st.ellMajEy = static_cast<float>(wy);
      st.ellMajEz = CadCommitElevation(st);
      st.ellPhase = EP::WaitRatio;
      log.push_back("ELLIPSE — type minor/major ratio (0-1], or Enter for 0.5:");
      break;
    case EP::WaitRatio:
      log.push_back("ELLIPSE — type ratio on command line (middle mouse pick ignored here).");
      break;
    }
    return;
  }
  if (st.active == K::Text) {
    using TP = AppCommandState::TextCmdPhase;
    if (st.textPhase == TP::WaitInsertion) {
      st.textInsX = static_cast<float>(wx);
      st.textInsY = static_cast<float>(wy);
      st.textPhase = TP::WaitHeight;
      log.push_back("TEXT — height (Enter = plot-scale default):");
    } else
      log.push_back("TEXT — continue on command line (height / rotation / text).");
    return;
  }

  if (st.active == K::Mleader) {  // REQ-367
    using MLp = AppCommandState::MleaderPhase;
    switch (st.mleaderPhase) {
    case MLp::WaitArrowTip:
      st.mleaderTipX = static_cast<float>(wx);
      st.mleaderTipY = static_cast<float>(wy);
      st.mleaderTipZ = CadCommitElevation(st);
      st.mleaderPhase = MLp::WaitLanding;
      log.push_back("MLEADER — specify landing location (text side):");
      break;
    case MLp::WaitLanding:
      CommitMleaderLandingAt(st, static_cast<float>(wx), static_cast<float>(wy), log);
      break;
    case MLp::WaitLabel:
      break;
    }
    return;
  }

  if (st.active == K::MleaderAddLeader) {
    CommitMleaderAddLeaderAt(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Mtext) {
    using MPt = AppCommandState::MtextPhase;
    switch (st.mtextPhase) {
    case MPt::WaitCorner1:
      st.mtxtX1 = static_cast<float>(wx);
      st.mtxtY1 = static_cast<float>(wy);
      st.mtextPhase = MPt::WaitCorner2;
      log.push_back("MTEXT — opposite corner:");
      break;
    case MPt::WaitCorner2:
      st.mtxtX2 = static_cast<float>(wx);
      st.mtxtY2 = static_cast<float>(wy);
      st.mtextPhase = MPt::WaitString;
      OpenMtextRichEditorForPlacement(st, &log);
      break;
    case MPt::WaitString:
      break;
    }
    return;
  }

  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    using DP = AppCommandState::DimPhase;
    const bool linear = st.active == K::DimLinear;
    switch (st.dimPhase) {
    case DP::WaitExt1:
      st.dimE1x = static_cast<float>(wx);
      st.dimE1y = static_cast<float>(wy);
      st.dimPhase = DP::WaitExt2;
      log.push_back(std::string(linear ? "DIMLINEAR" : "DIMALIGNED") + " — second extension point:");
      break;
    case DP::WaitExt2:
      st.dimE2x = static_cast<float>(wx);
      st.dimE2y = static_cast<float>(wy);
      st.dimPhase = DP::WaitDimLinePt;
      if (linear) {
        st.dimLinearOrientUserLock = false;
        CadDimLinearUpdateDraftOrientation(st, static_cast<float>(wx), static_cast<float>(wy));
        log.push_back(
            "DIMLINEAR — pick dimension line position (horizontal vs vertical follows cursor; H / V to lock); type X,Y or @dx,dy from chord mid.");
      } else
        log.push_back("DIMALIGNED — pick dimension line position (offset from measured segment).");
      break;
    case DP::WaitDimLinePt:
      if (linear)
        CommitDimLinearAt(st, static_cast<float>(wx), static_cast<float>(wy), log);
      else
        CommitDimAlignedAt(st, static_cast<float>(wx), static_cast<float>(wy), log);
      break;
    }
    return;
  }

  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    switch (st.dimAngularPhase) {
    case DAP::WaitVertex:
      st.dimAngVx = static_cast<float>(wx);
      st.dimAngVy = static_cast<float>(wy);
      st.dimAngularPhase = DAP::WaitRay1;
      log.push_back("DIMANGULAR — first ray point (on first leg):");
      break;
    case DAP::WaitRay1:
      st.dimE1x = static_cast<float>(wx);
      st.dimE1y = static_cast<float>(wy);
      st.dimAngularPhase = DAP::WaitRay2;
      log.push_back("DIMANGULAR — second ray point (on second leg):");
      break;
    case DAP::WaitRay2:
      st.dimE2x = static_cast<float>(wx);
      st.dimE2y = static_cast<float>(wy);
      st.dimAngularPhase = DAP::WaitArc;
      log.push_back("DIMANGULAR — pick arc / label side (radius along angle bisector); type X,Y or @dx,dy from vertex.");
      break;
    case DAP::WaitArc:
      CommitDimAngularAt(st, static_cast<float>(wx), static_cast<float>(wy), log);
      break;
    }
    return;
  }

  if (st.active == K::IdPoint) {
    CommitIdPointAt(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }
  if (st.active == K::StationOffsetPick) {  // REQ-484.4
    CommitStationOffsetPick(st, wx, wy, log);
    return;
  }

  if (st.active == K::GeoMarkPoint || st.active == K::GeoReorientMarker ||
      st.active == K::DrawingSettingsPick || st.active == K::GeoCaptureArea) {  // REQ-359 / 360 / 364
    SubmitGeoCommandPoint(st, wx, wy, log);
    return;
  }

  if (st.active == K::AlignmentLayout || st.active == K::AlignmentFromObjects) {  // REQ-485
    (void)SubmitAlignmentCommandPoint(st, wx, wy, log);
    return;
  }

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    if (st.surveyInversePhase == SIP::WaitFrom) {
      st.surveyInverseFromX = static_cast<float>(wx);
      st.surveyInverseFromY = static_cast<float>(wy);
      st.surveyInversePhase = SIP::WaitTo;
      log.push_back("INVERSE — second point (pick or type X,Y; @dx,dy from first):");
      return;
    }
    CommitSurveyInverseSecondPoint(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Dist) {
    using DP = AppCommandState::DistPhase;
    if (st.distPhase == DP::WaitFrom) {
      st.distFromX = static_cast<float>(wx);
      st.distFromY = static_cast<float>(wy);
      st.distFromZ = CadCommitElevation(st);
      st.distPhase = DP::WaitTo;
      log.push_back("DIST — second point (pick or type X,Y; @dx,dy from first):");
      return;
    }
    CommitDistSecondPoint(st, st.distFromX, st.distFromY, st.distFromZ, static_cast<float>(wx), static_cast<float>(wy), CadCommitElevation(st), log);
    return;
  }

  if (st.active == K::SurfaceElevGrade) {
    using SEP = AppCommandState::SurfaceElevPhase;
    if (st.surfaceElevPhase == SEP::WaitFirst) {
      ReportSurfaceElevationAt(st, wx, wy, log);
      st.surfaceElevPhase = SEP::WaitSecond;
      log.push_back("SURFELEV — second point for grade, or ESC to stop here.");
      return;
    }
    ReportSurfaceGradeTo(st, wx, wy, log);
    return;
  }

  if (st.active == K::WaterDrop) {
    const int si = FindSurfaceIndex(st, st.waterDropSurfaceName);
    if (si < 0) {
      log.push_back("WATERDROP — no surface named \"" + st.waterDropSurfaceName + "\".");
      st.active = K::None;
      return;
    }
    RunWaterDropAt(st, static_cast<size_t>(si), wx, wy, log);
    st.active = K::None;
    return;
  }
  if (st.active == K::Catchment) {
    const int si = FindSurfaceIndex(st, st.catchmentSurfaceName);
    if (si < 0) {
      log.push_back("CATCHMENT — no surface named \"" + st.catchmentSurfaceName + "\".");
      st.active = K::None;
      return;
    }
    RunCatchmentAt(st, static_cast<size_t>(si), wx, wy, log);
    st.active = K::None;
    return;
  }
  if (st.active == K::SwapTinEdge) {
    const int si = FindSurfaceIndex(st, st.swapEdgeSurfaceName);
    if (si < 0) {
      log.push_back("SURFSWAPEDGE — no surface named \"" + st.swapEdgeSurfaceName + "\".");
      st.active = K::None;
      return;
    }
    CommitSurfSwapEdgeLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(wx),
                            static_cast<double>(wy), log);
    st.active = K::None;
    return;
  }
  if (st.active == K::AddTinPoint) {
    const int si = FindSurfaceIndex(st, st.addPointSurfaceName);
    if (si < 0) {
      log.push_back("SURFACEADDPOINT — no surface named \"" + st.addPointSurfaceName + "\".");
      st.active = K::None;
      return;
    }
    CommitSurfAddPointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(wx),
                            static_cast<double>(wy), CadCommitElevation(st), log);
    st.active = K::None;
    return;
  }
  if (st.active == K::DelTinPoint) {
    const int si = FindSurfaceIndex(st, st.delPointSurfaceName);
    if (si < 0) {
      log.push_back("SURFACEDELPOINT — no surface named \"" + st.delPointSurfaceName + "\".");
      st.active = K::None;
      return;
    }
    CommitSurfDelPointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(wx),
                            static_cast<double>(wy), log);
    st.active = K::None;
    return;
  }
  if (st.active == K::MoveTinPoint) {
    const int si = FindSurfaceIndex(st, st.movePointSurfaceName);
    if (si < 0) {
      log.push_back("SURFACEMOVEPOINT — no surface named \"" + st.movePointSurfaceName + "\".");
      st.active = K::None;
      return;
    }
    using MP = AppCommandState::MoveTinPointPhase;
    if (st.moveTinPointPhase == MP::WaitFrom) {
      st.moveTinFromX = static_cast<double>(wx);
      st.moveTinFromY = static_cast<double>(wy);
      st.moveTinPointPhase = MP::WaitTo;
      log.push_back("SURFACEMOVEPOINT — pick the new position (Z = work plane).");
      return;
    }
    CommitSurfMovePointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], st.moveTinFromX, st.moveTinFromY,
                             static_cast<double>(wx), static_cast<double>(wy), CadCommitElevation(st), log);
    st.active = K::None;
    return;
  }
  if (st.active == K::DelTinLine) {
    const int si = FindSurfaceIndex(st, st.delLineSurfaceName);
    if (si < 0) {
      log.push_back("SURFDELLINE — no surface named \"" + st.delLineSurfaceName + "\".");
      st.active = K::None;
      return;
    }
    CommitSurfDelLineLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(wx),
                           static_cast<double>(wy), log);
    st.active = K::None;
    return;
  }
  if (st.active == K::QuickProfile) {
    const int si = FindSurfaceIndex(st, st.quickProfileSurfaceName);
    if (si < 0) {
      log.push_back("QUICKPROFILE — no surface named \"" + st.quickProfileSurfaceName + "\".");
      st.active = K::None;
      return;
    }
    using QP = AppCommandState::QuickProfilePhase;
    if (st.quickProfilePhase == QP::WaitFirst) {
      st.quickProfileFromX = static_cast<double>(wx);
      st.quickProfileFromY = static_cast<double>(wy);
      st.quickProfilePhase = QP::WaitSecond;
      log.push_back("QUICKPROFILE — second point of the sample line.");
      return;
    }
    CommitQuickProfileLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], st.quickProfileFromX, st.quickProfileFromY,
                            static_cast<double>(wx), static_cast<double>(wy), log);
    st.active = K::None;
    return;
  }

  if (st.active == K::DesignateBreakline) {
    CommitDesignateAt(st, static_cast<float>(wx), static_cast<float>(wy), /*isBoundary=*/false, log);
    return;
  }
  if (st.active == K::DesignateBoundary) {
    CommitDesignateAt(st, static_cast<float>(wx), static_cast<float>(wy), /*isBoundary=*/true, log);
    return;
  }

  if (st.active == K::Align) {
    using AP = AppCommandState::AlignPhase;
    if (st.alignPhase == AP::PickSelection) {
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    if (st.alignPhase == AP::PickSrc) {
      AppCommandState::AlignControlPt cp{};
      cp.srcX = static_cast<float>(wx);
      cp.srcY = static_cast<float>(wy);
      st.alignControlPts.push_back(cp);
      st.alignPhase = AP::PickDst;
      log.push_back("ALIGN — destination for pair " + std::to_string(st.alignControlPts.size()) +
                    " (pick or type real-world X,Y):");
    } else {
      st.alignControlPts.back().dstX = static_cast<float>(wx);
      st.alignControlPts.back().dstY = static_cast<float>(wy);
      st.alignPhase = AP::PickSrc;
      const size_t n = st.alignControlPts.size();
      log.push_back("ALIGN — pair " + std::to_string(n) + " added.  Pick next source, or Enter to apply (" +
                    std::to_string(n) + " pair" + (n == 1 ? "" : "s") + " ready).");
    }
    return;
  }

  if (st.active == K::Offset) {
    if (st.offsetPaperLayoutIndex >= 0 || (st.activeSpaceIndex >= 0 && !InFloatingModelSpace(st)))
      OffsetCmd::HandlePaperOffsetViewportPick(st, wx, wy, log);
    else
      OffsetCmd::HandleOffsetViewportPick(st, wx, wy, log);
    return;
  }

  if (st.active == K::Solid) {
    SubmitSolidViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Extrude) {
    if (st.extrudePhase == AppCommandState::ExtrudePhase::SelectProfiles) {
      // A finished fence merges into the accumulating selection; the phase advances only on Enter —
      // the same shape MOVE/COPY's PickSelection uses (D-2026-08-25-n).
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    SubmitExtrudeViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::PressPull) {
    if (st.pressPullPhase == AppCommandState::PressPullPhase::SelectTarget) {
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    SubmitPressPullViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Revolve) {
    if (st.revolvePhase == AppCommandState::RevolvePhase::SelectProfiles) {
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    SubmitRevolveViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Loft) {
    // One phase, and it is the accumulate-and-Enter shape: a finished fence merges into the
    // selection, the command advances only on Enter (HandleLoftTextInput).
    if (st.selBoxWaitingSecond)
      finishBox();
    return;
  }

  if (st.active == K::Sweep) {
    if (st.selBoxWaitingSecond)
      finishBox();
    return;
  }

  // 3DMOVE / 3DROTATE / 3DSCALE's select step (GitHub issue #564): LOFT's accumulate-and-Enter
  // shape — a finished fence merges into the selection, and Enter moves on to the handles. Past it
  // the viewport routes clicks to the gizmo (`GizmoHandlePick`), never here.
  if (IsGizmoCommandKind(st.active)) {
    if (st.gizmoCmdPhase == AppCommandState::GizmoCmdPhase::BasePoint) {
      // The base point is an ordinary snapped pick on the work plane, carrying its elevation the
      // way MOVE's base point does (REQ-329 increment 1).
      SubmitGizmoBasePoint(st, wx, wy, CadCommitElevation(st), log);
      return;
    }
    if (st.selBoxWaitingSecond)
      finishBox();
    return;
  }

  // CHPROP / MATCHPROP / LAYMCUR (REQ-356): every pick step is the accumulate shape; a closed
  // window merges, and the single-pick steps then act on it at once.
  if (IsPropCommandKind(st.active)) {
    if (st.selBoxWaitingSecond) {
      finishBox();
      CadPropCommandSelectionChanged(st, log);
    }
    return;
  }

  if (st.active == K::Slice) {
    if (st.slicePhase == AppCommandState::SlicePhase::SelectSolids) {
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    SubmitSliceViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  // SECTION (REQ-335 increment 2), the same shape as SLICE above. During the selection phase a
  // click must fall through to ordinary SELECTING — that is what makes picking the solid work —
  // and only afterwards does a click mean a point on the plane.
  if (st.active == K::Section) {
    if (st.sectionPhase == AppCommandState::SectionPhase::SelectSolids) {
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    SubmitSectionViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Boolean) {
    if (st.selBoxWaitingSecond)
      finishBox();  // every phase is a selection step; Enter advances it
    return;
  }

  if (st.active == K::Polysolid) {
    SubmitPolysolidViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Revcloud) {
    SubmitRevcloudViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Spline) {
    st.splineDraftPoints.push_back({wx, wy, CadCommitElevation(st)});
    log.push_back("SPLINE — point added. Pick next point, or type Close / End / Undo:");
    return;
  }

  if (SubmitCadDrawInquiryViewportPick(st, wx, wy, log))
    return;

  if (st.active == K::PipeRun) {
    SubmitPipeRunViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::PipeFit) {
    SubmitPipeFitViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::PipeSplit) {
    SubmitPipeSplitViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Circle) {
    switch (st.circlePhase) {
    case AppCommandState::CirclePhase::WaitCenterOrMode:
      st.circleCx = static_cast<float>(wx);
      st.circleCy = static_cast<float>(wy);
      st.circleCz = CadCommitElevation(st);
      st.circlePhase = AppCommandState::CirclePhase::WaitRadius;
      log.push_back("Center set — specify radius (click near edge), type radius, or D + diameter.");
      break;
    case AppCommandState::CirclePhase::WaitRadius: {
      CommitCircleFromRimPick(st, st.circleCx, st.circleCy, st.circleCz, static_cast<float>(wx), static_cast<float>(wy), CadCommitElevation(st),
                              log);
      break;
    }
    case AppCommandState::CirclePhase::ThreeP_WaitP1:
      st.c3p1x = static_cast<float>(wx);
      st.c3p1y = static_cast<float>(wy);
      st.c3p1z = CadCommitElevation(st);
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP2;
      log.push_back("Second point of circle:");
      break;
    case AppCommandState::CirclePhase::ThreeP_WaitP2:
      st.c3p2x = static_cast<float>(wx);
      st.c3p2y = static_cast<float>(wy);
      st.c3p2z = CadCommitElevation(st);
      st.circlePhase = AppCommandState::CirclePhase::ThreeP_WaitP3;
      log.push_back("Third point of circle:");
      break;
    case AppCommandState::CirclePhase::ThreeP_WaitP3: {
      if (!CommitCircleThreePoints(st, st.c3p1x, st.c3p1y, st.c3p1z, st.c3p2x, st.c3p2y, st.c3p2z, static_cast<float>(wx), static_cast<float>(wy),
                                   CadCommitElevation(st), log))
        log.push_back("Points are collinear — pick a non-collinear third point.");
      break;
    }
    }
    return;
  }

  if (st.active == K::Zoom) {
    if (st.selBoxWaitingSecond) {
      st.pendingZoomMnX = static_cast<float>(std::min<double>(st.selBoxAnchorX, wx));
      st.pendingZoomMxX = static_cast<float>(std::max<double>(st.selBoxAnchorX, wx));
      st.pendingZoomMnY = static_cast<float>(std::min<double>(st.selBoxAnchorY, wy));
      st.pendingZoomMxY = static_cast<float>(std::max<double>(st.selBoxAnchorY, wy));
      st.selBoxWaitingSecond = false;
      st.pendingZoomWindow = true;
      st.active = K::None;
    }
    return;
  }

  if (st.active == K::Join || st.active == K::Delete) {
    // REQ-121 (GitHub #91 review, D-2026-08-26-d): a finished box now MERGES into the accumulating
    // selection and the command stays put, exactly as MOVE/COPY's PickSelection does below. It no
    // longer executes here — `ProcessCommandLineSubmit`'s Enter branch is what acts on the
    // selection, which is what makes the shared "Select objects, ENTER to continue" prompt true for
    // these two. Clicks that land on an entity never reach here at all; they are accumulated by
    // `ViewportClickRoute::SelectionAccumulate` in CadUi.
    //
    // `finishBox` computes `inclSurvey` false for both, which is the survey-point behaviour the
    // two-corner box already had — deliberately unchanged here.
    if (st.selBoxWaitingSecond)
      finishBox();
    return;
  }

  if (st.active == K::Move || st.active == K::Copy) {
    if (st.modifyPhase == MP::PickSelection) {
      // This fix: a finished box merges into the accumulating selection and the phase stays put —
      // it no longer advances here. Advancing (to NeedBase) happens only on Enter, in
      // ProcessCommandLineSubmit's matching PickSelection branch, which is also where an entity
      // click (ViewportClickRoute::SelectionAccumulate, CadUi.cpp) lands without a box at all.
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    if (st.modifyPhase == MP::NeedBase) {
      st.modifyBaseX = static_cast<float>(wx);
      st.modifyBaseY = static_cast<float>(wy);
      // REQ-329 increment 1: the pick lands on the active work plane, so it carries that plane's
      // elevation (or a snapped point's own Z). Plan view under the World UCS leaves this at the
      // work-plane elevation for both picks, so the delta's Z is zero and every existing drag is
      // byte-identical.
      st.modifyBaseZ = CadCommitElevation(st);
      st.modifyPhase = MP::NeedDestination;
      log.push_back(st.active == K::Copy ? "COPY — destination:" : "MOVE — destination:");
      return;
    }
    if (st.modifyPhase == MP::NeedDestination) {
      const bool wasCopy = (st.active == K::Copy);
      const float dx = static_cast<float>(wx - st.modifyBaseX);
      const float dy = static_cast<float>(wy - st.modifyBaseY);
      const float dz = CadCommitElevation(st) - st.modifyBaseZ;  // REQ-329 increment 1
      PushUndoSnapshot(st, wasCopy ? "Copy" : "Move");
      if (wasCopy)
        FinalizeCopyTranslation(st, dx, dy, dz, log);
      else {
        ApplyTranslationToSelection(st, dx, dy, dz, log);
        // Stay in MOVE — same selection at new position, ready for another base+destination.
        st.modifyPhase = MP::NeedBase;
        log.push_back("MOVE complete — base point (ESC to exit):");
      }
    }
    return;
  }

  // REQ-305 ARRAY. PickSelection is the click-or-window-select shape MOVE/COPY share (this fix); the
  // interactive spatial phases (column/row spacing, polar center, fill angle) read the click the
  // same way OFFSET's distance-or-click and ROTATE's angle-by-point do. Typed-only phases (WaitType,
  // the two counts, rotate Yes/No) have no branch here — \c ViewportClickRouteFor routes them to
  // \c Ignore.
  if (st.active == K::Array) {
    using AP = AppCommandState::ArrayPhase;
    if (st.arrayPhase == AP::PickSelection) {
      if (st.arrayOnPaper) {
        PaperLayout* layout = ActivePaperGeometryTarget(st);
        PaperRef hit{};
        if (!layout || !PickPaperEntityAt(*layout, static_cast<float>(wx), static_cast<float>(wy),
                                          std::max(0.12f, CadOffsetEntityPickTolWorld(st)), &hit,
                                          &st.drawing.blockDefs))
          return;
        const auto selected = std::find_if(st.selectedPaperEntities.begin(), st.selectedPaperEntities.end(),
                                           [&](const PaperEntityRef& item) {
                                             return item.type == hit.type && item.index == hit.index;
                                           });
        if (windowSelectionSubtract) {
          if (selected != st.selectedPaperEntities.end())
            st.selectedPaperEntities.erase(selected);
        } else if (selected == st.selectedPaperEntities.end()) {
          st.selectedPaperEntities.push_back(hit);
        }
        log.push_back("ARRAY — paper selection: " + std::to_string(st.selectedPaperEntities.size()) + " object(s).");
        return;
      }
      // A finished box merges into the selection and the phase stays put; DropArrayUnsupportedFrom-
      // Selection and the WaitType advance now happen only on Enter (ProcessCommandLineSubmit).
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    if (st.arrayPhase == AP::Rect_WaitColumnSpacing) {
      // GitHub issue #400: resolved onto the active UCS work plane, anchored at the selection's own
      // anchor, then expressed as a LOCAL UCS-plane distance — colSpacing is a UCS-X distance, not a
      // world-X one (REQ-305 acceptance 10). Under the World UCS this is byte-identical to the old
      // `wx - st.arrayAnchorX`.
      float px = 0.f, py = 0.f, pz = 0.f;
      CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pickRay, st.arrayAnchorX, st.arrayAnchorY,
                                     st.arrayAnchorZ, &px, &py, &pz);
      const ucs::Ucs frame = CadWorkPlaneAnchoredAt(st, st.arrayAnchorX, st.arrayAnchorY, st.arrayAnchorZ);
      const ucs::Point2D local = ucs::WorldToPlane(frame, {px, py, static_cast<double>(pz)});
      st.arrayColSpacing = static_cast<float>(local.x);
      st.arrayPhase = AP::Rect_WaitRows;
      log.push_back("ARRAY Rectangular — number of rows:");
      return;
    }
    if (st.arrayPhase == AP::Rect_WaitRowSpacing) {
      float px = 0.f, py = 0.f, pz = 0.f;
      CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pickRay, st.arrayAnchorX, st.arrayAnchorY,
                                     st.arrayAnchorZ, &px, &py, &pz);
      const ucs::Ucs frame = CadWorkPlaneAnchoredAt(st, st.arrayAnchorX, st.arrayAnchorY, st.arrayAnchorZ);
      const ucs::Point2D local = ucs::WorldToPlane(frame, {px, py, static_cast<double>(pz)});
      st.arrayRowSpacing = static_cast<float>(local.y);
      if (st.arrayOnPaper) {
        st.arrayLevels = 1;
        CommitArrayRectangular(st, log);
        return;
      }
      // GitHub issue #400 increment 2: advance to the levels prompt, same as the typed path —
      // this click-driven branch committing directly (skipping levels entirely) was a bug in
      // increment 2, caught while adding increment 3's test coverage.
      st.arrayPhase = AP::Rect_WaitLevels;
      log.push_back("ARRAY Rectangular — number of levels <1 = 2D>:");
      return;
    }
    if (st.arrayPhase == AP::Polar_WaitCenter) {
      // Only reached when the active UCS is horizontal (WaitType refused switching to Polar
      // otherwise), so the flat wx/wy pick already lands correctly under an orbited camera once
      // resolved through the ray/plane intersection — same helper as the rectangular phases.
      float pz = 0.f;
      CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pickRay, st.arrayAnchorX, st.arrayAnchorY,
                                     st.arrayAnchorZ, &st.arrayCenterX, &st.arrayCenterY, &pz);
      st.arrayCenterZ = pz;
      st.arrayPhase = AP::Polar_WaitItemCount;
      log.push_back("ARRAY Polar — number of items (total, including the original):");
      return;
    }
    if (st.arrayPhase == AP::Polar_WaitAngle) {
      // Interactive fill-angle entry: the absolute angle (standard math convention, CCW from +X)
      // from center to the click, taken directly as the sweep magnitude. Typed entry
      // (HandleArrayText) sets the same field from a plain number of degrees.
      //
      // GitHub issue #400 increment 4: the angle is measured IN THE ACTIVE UCS PLANE, not in world
      // X/Y. Under FRONT/BACK/LEFT/RIGHT/orbited UCS the drawing plane's in-plane axes are world X
      // and world Z (not Y), so a raw `atan2(py - centerY, px - centerX)` collapses to ~0/180 for
      // every pick (the world-Y term is near-constant across the plane) and the polar array's step
      // angle came out zero — every instance stacked on the original. Convert the pick and the
      // centre into the plane's own local 2D frame (anchored at the centre, so the centre is the
      // origin) and take the angle there — the same WorldToPlane conversion the rectangular
      // spacing phases above already use. Under the World UCS this reduces to the old arithmetic.
      float px = 0.f, py = 0.f, pz = 0.f;
      CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pickRay, st.arrayCenterX, st.arrayCenterY,
                                     st.arrayCenterZ, &px, &py, &pz);
      const ucs::Ucs angleFrame =
          CadWorkPlaneAnchoredAt(st, st.arrayCenterX, st.arrayCenterY, st.arrayCenterZ);
      const ucs::Point2D angleLocal =
          ucs::WorldToPlane(angleFrame, {px, py, static_cast<double>(pz)});
      float deg = std::atan2(static_cast<float>(angleLocal.y), static_cast<float>(angleLocal.x)) *
                  (180.f / 3.14159265358979323846f);
      if (deg < 0.f)
        deg += 360.f;
      st.arrayFillAngleDeg = deg;
      st.arrayPhase = AP::Polar_WaitRotateAnswer;
      log.push_back("ARRAY Polar — rotate items? [Y]es / [N]o:");
      return;
    }
    return;
  }

  if (st.active == K::Stretch) {
    if (st.modifyPhase == MP::PickSelection) {
      if (st.selBoxWaitingSecond) {
        finishBox();
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — pick two corners again.");
        else {
          st.modifyPhase = MP::NeedBase;
          log.push_back("STRETCH — base point:");
        }
      }
      return;
    }
    if (st.modifyPhase == MP::NeedBase) {
      st.modifyBaseX = static_cast<float>(wx);
      st.modifyBaseY = static_cast<float>(wy);
      st.modifyBaseZ = CadCommitElevation(st);  // REQ-329 increment 4
      st.modifyPhase = MP::NeedDestination;
      log.push_back("STRETCH — destination:");
      return;
    }
    if (st.modifyPhase == MP::NeedDestination) {
      const float dx = static_cast<float>(wx - st.modifyBaseX);
      const float dy = static_cast<float>(wy - st.modifyBaseY);
      const float dz = CadCommitElevation(st) - st.modifyBaseZ;  // REQ-329 increment 4
      PushUndoSnapshot(st, "Stretch");
      ApplyStretchToSelection(st, dx, dy, dz, st.stretchRectMnX, st.stretchRectMxX, st.stretchRectMnY,
                              st.stretchRectMxY, st.stretchRectInUcsPlane, log);
      // Stay in STRETCH — same selection+box at new position, ready for another base+destination
      // (MOVE's own looping shape for repeated displacement rounds on one selection).
      st.modifyPhase = MP::NeedBase;
      log.push_back("STRETCH complete — base point (ESC to exit):");
    }
    return;
  }

  if (st.active == K::Paste && st.modifyPhase == MP::NeedDestination) {
    CommitClipboardPasteAt(st, wx, wy, log);  // routes by active space; builds the new selection
    return;
  }

  if (st.active == K::Scale) {
    if (st.modifyPhase == MP::PickSelection) {
      // This fix: stays in PickSelection after a box finishes; Enter (ProcessCommandLineSubmit)
      // advances to NeedBase.
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    if (st.modifyPhase == MP::NeedBase) {
      st.modifyBaseX = static_cast<float>(wx);
      st.modifyBaseY = static_cast<float>(wy);
      st.modifyBaseZ = CadCommitElevation(st);  // REQ-329 (SCALE)
      st.scaleRefDist = ComputeScaleReferenceDistance(st, static_cast<float>(wx), static_cast<float>(wy));
      st.scalePhase = SP::FactorPick;
      st.modifyPhase = MP::NeedDestination;
      log.push_back(
          "SCALE — pick second point or type factor (>0), or R / REFERENCE on command line for two-point reference "
          "length.");
      return;
    }
    if (st.modifyPhase == MP::NeedDestination) {
      switch (st.scalePhase) {
      case SP::FactorPick: {
        const float d = static_cast<float>(std::hypot(wx - st.modifyBaseX, wy - st.modifyBaseY));
        const float s = std::max(d / std::max(st.scaleRefDist, 1e-20f), 1e-6f);
        FinishScaleCommand(st, s, log);
        return;
      }
      case SP::Ref_WaitP1:
        st.scaleRefP1X = static_cast<float>(wx);
        st.scaleRefP1Y = static_cast<float>(wy);
        st.scalePhase = SP::Ref_WaitP2;
        log.push_back("SCALE ref — second point of reference length:");
        return;
      case SP::Ref_WaitP2: {
        const float refLen = static_cast<float>(std::hypot(wx - st.scaleRefP1X, wy - st.scaleRefP1Y));
        if (!(refLen > 1e-8f) || !std::isfinite(refLen)) {
          log.push_back("SCALE ref — reference length is too small; pick two distinct points.");
          return;
        }
        st.scaleRefDist = refLen;
        st.scalePhase = SP::NewLength_WaitTypedOrP1;
        log.push_back("SCALE ref — type new length (model units) or pick first point of new length segment.");
        return;
      }
      case SP::NewLength_WaitTypedOrP1:
        st.scaleNewLenP1X = static_cast<float>(wx);
        st.scaleNewLenP1Y = static_cast<float>(wy);
        st.scalePhase = SP::NewLength_WaitP2;
        log.push_back("SCALE ref — second point of new length segment:");
        return;
      case SP::NewLength_WaitP2: {
        const float newLen = static_cast<float>(std::hypot(wx - st.scaleNewLenP1X, wy - st.scaleNewLenP1Y));
        if (!(newLen > 1e-8f) || !std::isfinite(newLen)) {
          log.push_back("SCALE ref — new length is too small; pick two distinct points.");
          return;
        }
        FinishScaleCommand(st, newLen / std::max(st.scaleRefDist, 1e-20f), log);
        return;
      }
      }
    }
    return;
  }

  if (st.active == K::Rotate) {
    if (st.rotatePhase == RP::PickSelection) {
      // This fix: stays in PickSelection after a box finishes; Enter (ProcessCommandLineSubmit)
      // advances to NeedBase.
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    if (st.rotatePhase == RP::NeedBase) {
      st.rotateBaseX = static_cast<float>(wx);
      st.rotateBaseY = static_cast<float>(wy);
      st.rotateBaseZ = CadCommitElevation(st);  // REQ-329 increment 2: the axis passes through here
      st.rotatePhase = RP::NeedAngleOrReference;
      log.push_back(
          "ROTATE — ° clockwise from north or R reference or C copy; decimal/DMS or click-drag preview.");
      return;
    }
    if (st.rotatePhase == RP::NeedAngleOrReference) {
      // Click confirms the angle shown in the live preview: bearing CW from north, base→cursor.
      float rad = 0.f;
      if (CadWorkPlaneIsWorldXy(st)) {
        rad = static_cast<float>(-std::atan2(wx - st.rotateBaseX, wy - st.rotateBaseY));  // unchanged
      } else {
        // REQ-329 increment 2: under a tilted UCS the click's world-XY delta does not describe an
        // angle in the drawing plane (the failure #400 increment 4 fixed for ARRAY's fill angle) —
        // measure it in the active work plane's own local X/Y.
        float ax = 0.f, ay = 0.f, az = 0.f;
        CadResolvePickOnWorkPlaneAnchored(st, static_cast<float>(wx), static_cast<float>(wy), pickRay, st.rotateBaseX, st.rotateBaseY,
                                          st.rotateBaseZ, &ax, &ay, &az);
        const ucs::Point2D loc = ucs::WorldToPlane(
            CadWorkPlaneAnchoredAt(st, st.rotateBaseX, st.rotateBaseY, st.rotateBaseZ),
            {ax, ay, static_cast<double>(az)});
        rad = -std::atan2(static_cast<float>(loc.x), static_cast<float>(loc.y));
      }
      FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, rad, log);
      return;
    }
    if (st.rotatePhase == RP::AfterReference_WaitAngleOrP) {
      // Click confirms the angle shown in the live preview: angle from reference segment to base→cursor.
      const float thetaRef =
          std::atan2(st.rotateRefY2 - st.rotateRefY1, st.rotateRefX2 - st.rotateRefX1);
      const float delta = static_cast<float>(std::atan2(wy - st.rotateBaseY, wx - st.rotateBaseX) - thetaRef);
      FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
      return;
    }
    if (st.rotatePhase == RP::Ref_WaitP1) {
      st.rotateRefX1 = static_cast<float>(wx);
      st.rotateRefY1 = static_cast<float>(wy);
      st.rotatePhase = RP::Ref_WaitP2;
      log.push_back("Reference — second point:");
      return;
    }
    if (st.rotatePhase == RP::Ref_WaitP2) {
      st.rotateRefX2 = static_cast<float>(wx);
      st.rotateRefY2 = static_cast<float>(wy);
      st.rotatePhase = RP::AfterReference_WaitAngleOrP;
      log.push_back("Enter new bearing from north ° (matches properties), or P for two-point line.");
      return;
    }
    if (st.rotatePhase == RP::AnglePoints_WaitP1) {
      st.rotateAnglePt1X = static_cast<float>(wx);
      st.rotateAnglePt1Y = static_cast<float>(wy);
      st.rotatePhase = RP::AnglePoints_WaitP2;
      log.push_back("Angle — second point:");
      return;
    }
    if (st.rotatePhase == RP::AnglePoints_WaitP2) {
      const float delta =
          RotateDeltaFromReferenceAndNewSegment(st.rotateRefX1, st.rotateRefY1, st.rotateRefX2, st.rotateRefY2,
                                                  st.rotateAnglePt1X, st.rotateAnglePt1Y, static_cast<float>(wx), static_cast<float>(wy));
      FinishRotateCommand(st, st.rotateBaseX, st.rotateBaseY, delta, log);
    }
    return;
  }

  if (st.active == K::Mirror) {
    if (st.mirrorPhase == MirP::PickSelection) {
      // This fix: stays in PickSelection after a box finishes; Enter (ProcessCommandLineSubmit)
      // advances to NeedP1.
      if (st.selBoxWaitingSecond)
        finishBox();
      return;
    }
    if (st.mirrorPhase == MirP::NeedP1) {
      st.mirrorP1X = static_cast<float>(wx);
      st.mirrorP1Y = static_cast<float>(wy);
      st.mirrorP1Z = CadCommitElevation(st);  // REQ-329 increment 5
      st.mirrorPhase = MirP::NeedP2;
      log.push_back("MIRROR — specify second point of mirror line:");
      return;
    }
    if (st.mirrorPhase == MirP::NeedP2) {
      if (std::hypot(wx - st.mirrorP1X, wy - st.mirrorP1Y) < 1e-8f) {
        log.push_back("MIRROR — mirror line needs two distinct points; try again.");
        return;
      }
      st.mirrorP2X = static_cast<float>(wx);
      st.mirrorP2Y = static_cast<float>(wy);
      st.mirrorP2Z = CadCommitElevation(st);  // REQ-329 increment 5
      st.mirrorPhase = MirP::NeedEraseAnswer;
      log.push_back("Erase source objects? [Yes/No] <N>:");
    }
    // NeedEraseAnswer has no viewport-pick meaning — it is answered on the command line
    // (HandleMirrorText), matching TRIM's cutting-edge prompt convention.
    return;
  }

  if (st.active == K::Lengthen) {
    HandleLengthenViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Extend) {
    HandleExtendViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Break) {
    HandleBreakViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::Fillet) {
    HandleFilletViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log, pickRay);
    return;
  }

  if (st.active == K::Chamfer) {
    HandleChamferViewportPick(st, static_cast<float>(wx), static_cast<float>(wy), log);
    return;
  }

  if (st.active == K::None && st.selBoxWaitingSecond)
    finishBox();
}

} // namespace

/// is the camera's own definition of the view's world scale.
static float CadGizmoOrthoHalfHeightWorld(const AppCommandState& st) {
  return (1.f / std::max(st.viewportZoom, 1.e-9f)) * 50.f;
}

float CadGizmoHandleLenWorld(const AppCommandState& st) {
  // A definite viewport height even with no window: a transcript has no framebuffer, and a handle
  // length of zero would make every hit test fail silently rather than fail loudly.
  const float vph = st.uiViewportHeightPx > 1.f ? st.uiViewportHeightPx : 700.f;
  return std::max(1.e-6f, CadSnap::WorldToleranceFromPixels(vph, CadGizmoOrthoHalfHeightWorld(st),
                                                            kGizmoHandleLenPx));
}


void StartTraverseEditorCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.showTraverseEditorWindow = true;
  log.push_back("TRAVERSE — traverse editor opened.");
}

void StartOptionsCommand(AppCommandState& st, std::vector<std::string>& log) {
  st.showSettingsWindow = true;
  log.push_back("OPTIONS — settings dialog opened.");
}


void ClearCadGeometry(AppCommandState& st) {
  st.drawing.clearCadGeometry();
  // A cleared drawing starts in the WCS with no saved frames (REQ-154), for the same reason the id
  // space restarts below: the UCS belongs to the drawing, and carrying the previous drawing's
  // coordinate frame into a new one would read every coordinate typed next in a frame that has
  // nothing to do with it. A `internal document payload` load overwrites all four of these from the file immediately
  // after clearing.
  st.activeUcs = ucs::Ucs{};
  st.ucsPrevious.clear();
  st.ucsFollow = false;
  st.activeViewName.clear();
  // A cleared drawing is a new drawing: its id space restarts. Ids are unique *within* a drawing
  // (REQ-076), not globally, and every entity that could have held one is about to be erased. A
  // `internal document payload` load overwrites this from the file immediately after clearing.
  st.nextEntityId = 1;
  st.entityIdSweepRevision = kEntityIdSweepNever;  // whatever is loaded next must be swept
  st.currentAnnotationScaleIndex = -1;
  st.currentViewportZoomScaleIndex = -1;
  st.dwgImportedLights.clear();
  st.dwgImportedSunPresent = false;
  st.dwgImportedLightList = {};
  st.dwgImportedLightListPresent = false;
  st.dwgImportedLightListDictKey.clear();
  st.solidDisplayCache.clear();
  st.solidDisplayGeometry.solids.clear();
  // TIN surfaces (REQ-068) and their generated display geometry (ADR-036 (e)). The cache is keyed
  // on stable entity ids and lives on AppCommandState, not in the drawing snapshot — so it must be
  // dropped whenever the drawing's geometry is cleared or replaced. Leaving it across OPEN/IMPORT
  // let a cache entry for the same id early-out against a new triangulation and, worse, hand the
  // renderer buffers built for the previous document (issue #663).
  st.surfaceDisplayCache.clear();
  st.surfaceWatershedCache.clear();
  st.surfaceDisplayGeometry.lines.clear();
  st.surfaceDisplayGeometry.bandTriangles.clear();
  st.waterDropPreviewLines.clear();
  st.surfaceRebuildAsync.clear();  // join/cancel any in-flight worker before ids are reused
  st.blockRefWorldSolids.clear();
  st.blockRefWorldSolidAttrs.clear();
  st.blockRefWorldSolidsSig = 0;
  // Pipe runs are CAD geometry too (issue #486 / REQ-345). Left behind, an import into a drawing
  // that already had runs would keep them alongside the imported content — the same "written before
  // pipe runs existed" miss the document snapshot had. The derived solids go with them, the way
  // blockRefWorldSolids does just above.
  st.pipeRunWorldSolids.clear();
  st.pipeRunWorldSolidAttrs.clear();
  st.pipeRunWorldSolidOwnerIndex.clear();
  st.pipeRunWorldSolidsSig = 0;
  st.blockEditorName.clear();
  st.importedDxfAttrDefs.clear();
  ClearPendingOneShotObjectSnap(st);
  ClearCadSelection(st);
  // Isolation is keyed on entity ids, and the id space restarts above — so a hidden set kept here
  // would name whatever the next drawing happens to number the same way. This is also what makes
  // "opening a drawing always shows all of it" true (REQ-084 (d)), since every load clears first.
  st.hiddenEntityIds.clear();
  BumpCadGpuCache(st);
}

void ClearPendingOneShotObjectSnap(AppCommandState& st) {
  st.objectSnapKindOverrideValid = false;
}

void ResetCadToolStateToIdle(AppCommandState& st) {
  CancelTableCellEditor(st);
  ClearPendingOneShotObjectSnap(st);
  st.active = AppCommandState::Kind::None;
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  ResetSegmentAngleLock(st);
  st.trimPhase = AppCommandState::TrimPhase::SelectCuttingEdges;
  st.trimCutters.clear();
  st.paperTrimCutters.clear();
  // EXTEND's boundary list and BREAK's pending entity are reset by ResetModifyRotateDraft below,
  // which now owns every modify command's draft state (TASK-099 F4) — duplicated here previously.
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  ResetArrayDraft(st);
  st.selBoxWaitingSecond = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  AbortMtextGripInteraction(st);
}

void ClearSelection(AppCommandState& st) {
  ClearCadSelection(st);
  st.selectedSurveyPointIndices.clear();
}

// Squared point-segment distance in double precision. Picking math must run in double: at state-plane
// magnitudes (eastings/northings in the millions) a 32-bit float's ULP is ~1 ft, so a float-only distance
// suffers catastrophic cancellation and stops matching the rendered position — entities then "hover" while
// the cursor is feet away. Stored geometry is promoted per-coordinate so distances match what is drawn.

float CadOffsetEntityPickTolWorld(const AppCommandState& st) {
  double mnX = 0.;
  double mxX = 0.;
  double mnY = 0.;
  double mxY = 0.;
  float geom = 1e-3f;
  // Use the robust (outlier-trimmed) extent: stray entities sitting millions of feet away — common in
  // Civil 3D state-plane DXFs — would otherwise inflate this scale floor into a tens-of-feet "magnetic"
  // pick radius that highlights lines the cursor is nowhere near.
  int skipped = 0;
  if (ComputeRobustWorldExtents(st, &mnX, &mxX, &mnY, &mxY, &skipped))
    geom = std::max(1e-5f, static_cast<float>(2.5e-5 * std::max(mxX - mnX, mxY - mnY)));
  const float px = CadSnap::WorldToleranceFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                                     st.viewportLastSurveyLayoutOrthoHalfH, st.objectSnapAperturePx);
  return std::max(geom, px * 1.5f);
}

float CadHoverEntityPickTolWorld(const AppCommandState& st) {
  // The idle hover highlight activates once geometry is inside the CURSOR APERTURE — the box the
  // crosshair draws at its centre (CadUi: `ap * 0.5` px half-size, `ap = clamp(objectSnapAperturePx,
  // 4, 64)`). So the tolerance is that box's half-extent in world units: what the user sees is what
  // picks. (Was a fixed 3 px, which required the stroke to pass through the crosshair centre — user
  // GUI pass, 2026-09-02.) Constant in screen space at every zoom; a tiny floor for a degenerate view.
  const float apPx = std::clamp(st.objectSnapAperturePx, 4.f, 64.f) * 0.5f;
  return std::max(1e-6f, CadSnap::WorldToleranceFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                                           st.viewportLastSurveyLayoutOrthoHalfH, apPx));
}

void CadTrimAppendCutLineRemovedPreview(const AppCommandState& st, float fenceP1x, float fenceP1y, float fenceP2x,
                                        float fenceP2y, float pickPreviewX, float pickPreviewY,
                                        std::vector<float>* previewLinesOut) {
  if (!previewLinesOut)
    return;

  // The drawn fence line trims the ONE edge nearest it, exactly as ExecuteDrawnSegmentTrimOnce
  // commits it — so preview only that edge.
  //
  // Issue #166: the previous version previewed a hypothetical removal for EVERY line and polyline
  // edge in the drawing, and for each it re-tessellated the entire drawing's cutting geometry
  // (CollectAllDrawingCutSegmentsExceptTarget — every circle/arc/ellipse sampled, every polyline
  // walked). That is O(edges x drawing) with a fresh multi-thousand-element allocation per edge:
  // on a real survey (hundreds of lines, a thousand polylines) it measured ~2.4 s PER FRAME while
  // the rubber line was being dragged — a locked viewport, not a stutter. It was also misleading:
  // it dashed edges the commit would never touch.
  //
  // Matching the commit's own target selection collapses this to one O(N) proximity scan, one
  // tessellation pass and one intersection test — sub-millisecond on the same drawing.
  float matchTol = std::max(CadSnap::WorldToleranceFromPixels(st.viewportLastSurveyLayoutHeightPx,
                                                             st.viewportLastSurveyLayoutOrthoHalfH,
                                                             st.objectSnapAperturePx) *
                                4.f,
                            1e-6f);
  double mnX = 0., mxX = 0., mnY = 0., mxY = 0.;
  if (ComputeWorldExtents(st, &mnX, &mxX, &mnY, &mxY))
    matchTol = std::max(matchTol, static_cast<float>(2e-5 * std::max(mxX - mnX, mxY - mnY)));

  TrimTargetEdge tgt{};
  double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
  float dEdge = 0.f;
  if (!PickTrimTargetClosestToDrawnSegment(st, fenceP1x, fenceP1y, fenceP2x, fenceP2y, matchTol, &tgt, &ax, &ay,
                                           &bx, &by, &dEdge))
    return;

  std::vector<std::array<double, 4>> cuts;
  CollectAllDrawingCutSegmentsExceptTarget(st, &tgt, &cuts);
  if (cuts.empty())
    return;

  double ix = 0.0, iy = 0.0;
  bool trimA = false;
  if (!TrimSegmentIntersectPickSide(ax, ay, bx, by, pickPreviewX, pickPreviewY, cuts, st, fenceP1x, fenceP1y,
                                    fenceP2x, fenceP2y, true, &ix, &iy, &trimA, nullptr))
    return;

  const double rx = trimA ? ax : ix;
  const double ry = trimA ? ay : iy;
  const double sx = trimA ? ix : bx;
  const double sy = trimA ? iy : by;
  previewLinesOut->push_back(rx);
  previewLinesOut->push_back(ry);
  previewLinesOut->push_back(0.f);
  previewLinesOut->push_back(sx);
  previewLinesOut->push_back(sy);
  previewLinesOut->push_back(0.f);
}

void CadTrimAppendCutLineRemovedPreview3D(const AppCommandState& st, const ray3d::Vec3& f0, const ray3d::Vec3& f1,
                                          std::vector<float>* previewLinesOut) {
  if (!previewLinesOut)
    return;
  // Same pixel-aperture tolerance the 2D sibling derives internally (Solve3DDrawnLineTrim scales it
  // by 4 and floors it against the drawing extent, matching ExecuteDrawnSegmentTrimOnce).
  const float tolWorld = CadSnap::WorldToleranceFromPixels(
      st.viewportLastSurveyLayoutHeightPx, st.viewportLastSurveyLayoutOrthoHalfH, st.objectSnapAperturePx);
  DrawnLineTrimSolution s;
  if (!Solve3DDrawnLineTrim(st, f0, f1, tolWorld, &s, nullptr))
    return;
  // The portion the commit would remove: from the moving end to the cut point.
  const ray3d::Vec3 a = s.trimA ? s.ta : s.cut;
  const ray3d::Vec3 b = s.trimA ? s.cut : s.tb;
  previewLinesOut->push_back(static_cast<float>(a.x));
  previewLinesOut->push_back(static_cast<float>(a.y));
  previewLinesOut->push_back(static_cast<float>(a.z));
  previewLinesOut->push_back(static_cast<float>(b.x));
  previewLinesOut->push_back(static_cast<float>(b.y));
  previewLinesOut->push_back(static_cast<float>(b.z));
}

/// Nothing touches the drawing until the parse has fully succeeded — the undo snapshot is pushed
/// *after* the importer returns ok, so a malformed file leaves the drawing exactly as it was, which
/// is what REQ-065's "no partial import" condition means in practice.
bool ImportGltfModel(AppCommandState& st, const std::string& path, double unitScale, double insX, double insY,
                     double insZ, std::vector<std::string>& log) {
  modelimport::Options opt;
  opt.unitScale = unitScale;
  opt.insertX = insX;
  opt.insertY = insY;
  opt.insertZ = insZ;

  // Dispatch on the file's kind. A DWG cannot be read for 3D content directly — its geometry may be
  // vendor custom objects that only the vendor's enabler can decode — so it is routed through an
  // installed AutoCAD, which is ADR-024's converter pattern applied to 3D (ADR-026 addendum).
  std::string ext = std::filesystem::u8path(path).extension().u8string();
  ext = StringUtil::toLowerAsciiCopy(ext);

  modelimport::Result r;
  if (ext == ".dwg") {
    std::string whyNot;
    if (!dwgmesh::ConversionAvailable(&whyNot)) {
      log.push_back("Cannot import a DWG model — " + whyNot);
      return false;
    }
    log.push_back("Converting the DWG's 3D solids via " + FindDwgConverter().displayName +
                  " — this can take a few minutes on a large model.");
    const dwgmesh::ConvertResult cr = dwgmesh::ConvertDwgToMesh(path, opt);
    if (!cr.ok) {
      log.push_back("Model import failed — " + cr.error);
      return false;
    }
    r = cr.model;
  } else if (ext == ".stl") {
    r = stl::ImportStlFile(path, opt);
  } else {
    r = gltf::ImportGltfFile(path, opt);  // .glb / .gltf, and the fallback for an odd extension
  }

  if (!r.ok) {
    log.push_back("Model import failed — " + r.error);
    return false;
  }

  auto mesh = std::make_shared<CadMesh>();
  mesh->vertsXyz = r.vertsXyz;
  mesh->normalsXyz = r.normalsXyz;
  mesh->indices = r.indices;
  mesh->sourceName = std::filesystem::u8path(path).filename().u8string();
  mesh->parts.reserve(r.parts.size());
  for (const modelimport::Part& p : r.parts) {
    CadMeshPart cp;
    cp.name = p.name;
    cp.indexBegin = p.indexBegin;
    cp.indexCount = p.indexCount;
    cp.r = p.r;
    cp.g = p.g;
    cp.b = p.b;
    mesh->parts.push_back(std::move(cp));
  }

  PushUndoSnapshot(st, "Import model");
  st.drawing.cadMeshes.push_back(std::move(mesh));
  st.drawing.cadMeshAttrs.push_back(MakeNewEntityAttrs(st));
  BumpCadGpuCache(st);

  char msg[320];
  std::snprintf(msg, sizeof(msg), "Imported %s — %d triangles, %d parts, scale %.6g.",
                st.drawing.cadMeshes.back()->sourceName.c_str(), r.triangleCount(),
                static_cast<int>(r.parts.size()), unitScale);
  log.push_back(msg);
  // REQ-065 / REQ-201: what did not come in is stated, never dropped in silence.
  if (!r.skipped.empty()) {
    std::string s = "Not imported (geometry only): ";
    for (size_t i = 0; i < r.skipped.size(); ++i)
      s += (i ? ", " : "") + r.skipped[i];
    log.push_back(s + ".");
  }
  return true;
}

/// Bounded preview point budget (used by RunPointCloudImportWorker below): `CadPointCloud::pointsXyz` exists for
/// extents/selection-depth/`internal document payload` persistence/DXF-exclusion-count, not for rendering full detail —
bool StartPointCloudImportAsync(AppCommandState& st, const std::string& path,
                                std::vector<std::string>& log) {
  if (st.pointCloudImportAsync && !st.pointCloudImportAsync->done.load(std::memory_order_acquire)) {
    log.push_back("POINTCLOUDATTACH — an import is already in progress; wait for it to finish.");
    return false;
  }
  st.pointCloudImportAsync.reset();  // join/discard a finished-but-unreaped job, if any

  auto job = std::make_unique<AppCommandState::PointCloudImportAsync>();
  job->sourcePath = path;
  job->cachePath = path + ".gscloud";
  // Format is decided once, here, from the source path's own extension — every later step
  // (QuickPointCountEstimate above, BuildFromE57/BuildFromPts/BuildFromPtx/BuildFromLas/
  // BuildFromLaz in the worker) reads `job->format` rather than re-sniffing the path, so the
  // async plumbing stays shared across formats (REQ-172).
  using Format = AppCommandState::PointCloudImportAsync::SourceFormat;
  const std::string ext =
      StringUtil::toLowerAsciiCopy(std::filesystem::path(path).extension().string());
  job->format = (ext == ".pts") ? Format::Pts
              : (ext == ".ptx") ? Format::Ptx
              : (ext == ".las") ? Format::Las
              : (ext == ".laz") ? Format::Laz
                                 : Format::E57;
  job->totalPointsEstimate = (job->format == Format::Pts)
                                 ? pointcloud_pts::QuickPointCountEstimate(path)
                             : (job->format == Format::Ptx)
                                 ? pointcloud_ptx::QuickPointCountEstimate(path)
                             : (job->format == Format::Las)
                                 ? pointcloud_las::QuickPointCountEstimate(path)
                             : (job->format == Format::Laz)
                                 ? pointcloud_laz::QuickPointCountEstimate(path)
                                 : pointcloud_e57::QuickPointCountEstimate(path);
  job->startTime = std::chrono::steady_clock::now();

  AppCommandState::PointCloudImportAsync* jobPtr = job.get();
  job->thread = std::thread(RunPointCloudImportWorker, jobPtr);
  st.pointCloudImportAsync = std::move(job);

  log.push_back("POINTCLOUDATTACH — importing " +
                std::filesystem::u8path(path).filename().u8string() + "...");
  return true;
}

bool ExportPointCloudsToFile(AppCommandState& st, PointCloudExportFormat format,
                             const std::string& pathUtf8, std::vector<std::string>& log) {
  const std::string name = PointCloudExportCommandName(format);
  if (st.drawing.cadPointClouds.empty()) {
    log.push_back(name + " — the drawing has no point clouds.");
    return false;
  }

  std::set<int> resolved;
  std::vector<double> xyz;
  std::vector<float> colors, intensity;
  std::int64_t totalCount = 0;
  CollectSelectedPointCloudsForExport(st, resolved, xyz, colors, intensity, totalCount);

  if (xyz.empty()) {
    log.push_back(name + " — the selected point cloud(s) have no points to export.");
    return false;
  }

  std::string error;
  bool ok = false;
  switch (format) {
    case PointCloudExportFormat::Pts: {
      const auto wr = pointcloud_pts::WritePtsFile(pathUtf8, xyz, colors, intensity);
      ok = wr.ok; error = wr.errorMessage; break;
    }
    case PointCloudExportFormat::Ptx: {
      const auto wr = pointcloud_ptx::WritePtxFile(pathUtf8, xyz, colors, intensity);
      ok = wr.ok; error = wr.errorMessage; break;
    }
    case PointCloudExportFormat::Las: {
      const auto wr = pointcloud_las::WriteLasFile(pathUtf8, xyz, colors, intensity);
      ok = wr.ok; error = wr.errorMessage; break;
    }
    case PointCloudExportFormat::Laz: {
      const auto wr = pointcloud_laz::WriteLazFile(pathUtf8, xyz, colors, intensity);
      ok = wr.ok; error = wr.errorMessage; break;
    }
  }
  if (!ok) {
    log.push_back(name + " — " + error);
    return false;
  }
  char msg[384];
  std::snprintf(msg, sizeof(msg), " — wrote %lld point(s) from %zu cloud(s) to %s.",
               static_cast<long long>(totalCount), resolved.size(),
               std::filesystem::u8path(pathUtf8).filename().u8string().c_str());
  log.push_back(name + msg);
  return true;
}

/// `EXPORTPTS` / `EXPORTPTX` / `EXPORTLAS` / `EXPORTLAZ` command bodies: ask for a destination
/// with the native save dialog, then hand off to \ref ExportPointCloudsToFile.
static void ExportSelectedPointClouds(AppCommandState& st, PointCloudExportFormat format,
                                      std::vector<std::string>& log) {
  const std::string name = PointCloudExportCommandName(format);
  if (st.drawing.cadPointClouds.empty()) {
    log.push_back(name + " — the drawing has no point clouds.");
    return;
  }
  const PointCloudExportPlan plan = PlanPointCloudExport(st, format);
  if (plan.pointCount <= 0) {
    log.push_back(name + " — the selected point cloud(s) have no points to export.");
    return;
  }
  const std::string& defaultName = plan.defaultFileName;
  char buf[1024]{};
  const bool picked = format == PointCloudExportFormat::Pts ? BrowseSaveFilePtsUtf8(buf, sizeof(buf), defaultName.c_str())
                    : format == PointCloudExportFormat::Ptx ? BrowseSaveFilePtxUtf8(buf, sizeof(buf), defaultName.c_str())
                    : format == PointCloudExportFormat::Las ? BrowseSaveFileLasUtf8(buf, sizeof(buf), defaultName.c_str())
                                                            : BrowseSaveFileLazUtf8(buf, sizeof(buf), defaultName.c_str());
  if (!picked) {
    log.push_back(name + " — cancelled.");
    return;
  }
  ExportPointCloudsToFile(st, format, buf, log);
}


void CancelActiveCommand(AppCommandState& st, std::vector<std::string>& log) {
  if (HasPaperModifyPhase(st)) {
    if (st.paperLengthenPhase) log.push_back("LENGTHEN canceled.");
    else if (st.paperExtendPhase) log.push_back("EXTEND canceled.");
    else if (st.paperBreakPhase) log.push_back("BREAK canceled.");
    else if (st.paperStretchPhase) log.push_back("STRETCH canceled.");
    else if (st.paperFilletPhase) log.push_back("FILLET canceled.");
    else if (st.paperChamferPhase) log.push_back("CHAMFER canceled.");
    ResetPaperModifyDrafts(st);
    return;
  }
  if (st.bconnectAwaitingFace) {
    st.bconnectAwaitingFace = false;
    st.bconnectNameBuf[0] = '\0';
    st.bconnectSizeBuf[0] = '\0';
    st.bconnectCompatTagPending.clear();
    // The BCONNECT wizard (issue #496) is still st.active == Kind::BConnect during the face-pick
    // step (so a click routes here regardless of st.active) — release it on cancel too, or it
    // would stay stuck open with no way to type into it again.
    if (st.active == AppCommandState::Kind::BConnect)
      st.active = AppCommandState::Kind::None;
    log.push_back("BCONNECT canceled.");
    return;
  }
  if (st.active == AppCommandState::Kind::None)
    return;
  ClearPendingOneShotObjectSnap(st);
  const AppCommandState::Kind prev = st.active;
  if (st.active == AppCommandState::Kind::Line)
    log.push_back("LINE canceled.");
  else if (st.active == AppCommandState::Kind::Circle)
    log.push_back("CIRCLE canceled.");
  else if (st.active == AppCommandState::Kind::Solid) {
    // Named, like every other cancel here (REQ-201) - a solid command abandoned in silence looks
    // exactly like one that quietly created something.
    log.push_back(SolidVerbUpper(st.solidKind) + " canceled.");
    CancelSolidCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Extrude) {
    log.push_back("EXTRUDE canceled.");
    CancelExtrudeCommand(st);
  }
  else if (st.active == AppCommandState::Kind::PressPull) {
    log.push_back("PRESSPULL canceled.");
    CancelPressPullCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Revolve) {
    log.push_back("REVOLVE canceled.");
    CancelRevolveCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Slice) {
    log.push_back("SLICE canceled.");
    CancelSliceCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Section) {
    log.push_back("SECTION canceled — nothing was drawn.");
    CancelSectionCommand(st);
  }
  else if (st.active == AppCommandState::Kind::SectionPlane) {
    // "no plane was placed", not "nothing changed": an ESC out of SECTIONPLANE leaves any plane a
    // PREVIOUS run placed exactly where it was, and saying otherwise would be wrong.
    log.push_back("SECTIONPLANE canceled — no plane was placed.");
    CancelSectionPlaneCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Loft) {
    log.push_back("LOFT canceled.");
    CancelLoftCommand(st);
  }
  else if (IsGizmoCommandKind(st.active)) {
    // A live drag has changed nothing in the store, so ending the command IS the restore; the
    // gizmo op goes back to what it was before the command (GitHub issue #564 section 3).
    log.push_back(std::string(AppCommandState::KindName(st.active)) + " canceled.");
    EndGizmoCommand(st);
  }
  else if (IsPropCommandKind(st.active)) {
    // REQ-356: every applied change was its own undo step, so Esc only ends the command.
    log.push_back(std::string(AppCommandState::KindName(st.active)) + " canceled.");
    EndPropCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Sweep) {
    log.push_back("SWEEP canceled.");
    CancelSweepCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Boolean) {
    log.push_back("Command canceled.");
    CancelBooleanCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Polysolid) {
    // REQ-317. Named like every other cancel here: a command abandoned in silence looks exactly
    // like one that quietly created something.
    log.push_back("POLYSOLID canceled.");
    CancelPolysolidCommand(st);
  }
  else if (st.active == AppCommandState::Kind::Revcloud) {
    log.push_back("REVCLOUD canceled.");  // REQ-201: never silent
    CancelRevcloudCommand(st);
  }
  else if (st.active == AppCommandState::Kind::PipeRun) {
    log.push_back("PIPERUN canceled.");
    CancelPipeRunCommand(st);
  }
  else if (st.active == AppCommandState::Kind::PipeFit) {
    log.push_back("PIPEFIT canceled.");
    st.pipeFitRunIndex = -1;
    st.pipeFitPartType = CadPipePartType::None;
  }
  else if (st.active == AppCommandState::Kind::PipeSplit) {
    log.push_back("PIPESPLIT canceled.");
    st.pipeSplitRunIndex = -1;
  }
  else if (st.active == AppCommandState::Kind::Polyline)
    log.push_back("POLYLINE canceled.");
  else if (st.active == AppCommandState::Kind::FeatureLine)
    log.push_back("FEATURELINE canceled.");  // REQ-201: was silent
  else if (st.active == AppCommandState::Kind::Arc)
    log.push_back("ARC canceled.");
  else if (st.active == AppCommandState::Kind::Ellipse)
    log.push_back("ELLIPSE canceled.");
  else if (st.active == AppCommandState::Kind::Spline) {
    st.splineDraftPoints.clear();
    st.splineMethod = AppCommandState::SplineMethod::Fit;
    log.push_back("SPLINE canceled — nothing was added.");
  }
  else if (st.active == AppCommandState::Kind::Point) {
    ResetCadDrawInquiryDrafts(st);
    log.push_back("POINT canceled.");
  }
  else if (st.active == AppCommandState::Kind::Leader) {
    ResetCadDrawInquiryDrafts(st);
    log.push_back("LEADER canceled — nothing was added.");
  }
  else if (st.active == AppCommandState::Kind::Pedit) {
    ResetCadDrawInquiryDrafts(st);
    log.push_back("PEDIT canceled.");
  }
  else if (st.active == AppCommandState::Kind::Measure) {
    ResetCadDrawInquiryDrafts(st);
    log.push_back("MEASURE canceled.");
  }
  else if (st.active == AppCommandState::Kind::Divide) {
    ResetCadDrawInquiryDrafts(st);
    log.push_back("DIVIDE canceled.");
  }
  else if (st.active == AppCommandState::Kind::Area) {
    ResetCadDrawInquiryDrafts(st);
    log.push_back("AREA canceled.");
  }
  else if (st.active == AppCommandState::Kind::List) {
    ResetCadDrawInquiryDrafts(st);
    log.push_back("LIST canceled.");
  }
  else if (st.active == AppCommandState::Kind::DataExtract) {
    ResetCadDrawInquiryDrafts(st);
    st.active = AppCommandState::Kind::None;
    log.push_back("DATAEXTRACTION canceled.");
  }
  else if (st.active == AppCommandState::Kind::Table) {
    ResetCadDrawInquiryDrafts(st);
    st.active = AppCommandState::Kind::None;
    log.push_back("TABLE canceled.");
  }
  else if (st.active == AppCommandState::Kind::PointTable) {
    ResetCadDrawInquiryDrafts(st);
    st.active = AppCommandState::Kind::None;
    log.push_back("ADDPOINTTABLE canceled.");
  }
  else if (st.active == AppCommandState::Kind::Rect)
    log.push_back("RECT canceled.");
  else if (st.active == AppCommandState::Kind::TrimState)
    log.push_back("TRIMSTATE unchanged (" + std::to_string(st.trimState) + ").");
  else if (st.active == AppCommandState::Kind::SectionClip)
    log.push_back("SECTIONCLIP canceled. " + SectionClipReport(st));
  else if (st.active == AppCommandState::Kind::Elev)
    log.push_back("Elevation unchanged.");
  else if (st.active == AppCommandState::Kind::Ucs)
    log.push_back("UCS canceled - the coordinate system is unchanged.");
  else if (st.active == AppCommandState::Kind::Plan)
    log.push_back("PLAN canceled - the view is unchanged.");
  else if (st.active == AppCommandState::Kind::Text)
    log.push_back("TEXT canceled.");
  else if (st.active == AppCommandState::Kind::Mtext)
    log.push_back("MTEXT canceled.");
  else if (st.active == AppCommandState::Kind::Mleader)
    log.push_back("MLEADER canceled.");
  else if (st.active == AppCommandState::Kind::MleaderAddLeader)
    log.push_back("Add Leader canceled.");
  else if (st.active == AppCommandState::Kind::DimAligned)
    log.push_back("DIMALIGNED canceled.");
  else if (st.active == AppCommandState::Kind::DimLinear)
    log.push_back("DIMLINEAR canceled.");
  else if (st.active == AppCommandState::Kind::DimAngular)
    log.push_back("DIMANGULAR canceled.");
  else if (st.active == AppCommandState::Kind::Move)
    log.push_back("MOVE canceled.");
  else if (st.active == AppCommandState::Kind::Copy)
    log.push_back("COPY canceled.");
  else if (st.active == AppCommandState::Kind::Rotate)
    log.push_back("ROTATE canceled.");
  else if (st.active == AppCommandState::Kind::Scale)
    log.push_back("SCALE canceled.");
  else if (st.active == AppCommandState::Kind::Array)
    log.push_back("ARRAY canceled.");
  else if (st.active == AppCommandState::Kind::Mirror)
    log.push_back("MIRROR canceled.");
  else if (st.active == AppCommandState::Kind::Lengthen)
    log.push_back("LENGTHEN canceled.");
  else if (st.active == AppCommandState::Kind::Extend)
    log.push_back("EXTEND canceled.");
  else if (st.active == AppCommandState::Kind::Break)
    log.push_back("BREAK canceled.");
  else if (st.active == AppCommandState::Kind::Stretch)
    log.push_back("STRETCH canceled.");
  else if (st.active == AppCommandState::Kind::Delete)
    log.push_back("DELETE canceled.");
  else if (st.active == AppCommandState::Kind::Join)
    log.push_back("JOIN canceled.");
  else if (st.active == AppCommandState::Kind::Trim)
    log.push_back("TRIM canceled.");
  else if (st.active == AppCommandState::Kind::Offset)
    log.push_back("OFFSET canceled.");
  else if (st.active == AppCommandState::Kind::IdPoint)
    log.push_back("ID canceled.");
  else if (st.active == AppCommandState::Kind::StationOffsetPick)
    log.push_back("GETSTATIONOFFSET ended.");
  else if (st.active == AppCommandState::Kind::GeoMarkPoint || st.active == AppCommandState::Kind::GeoMarkLatLong ||
           st.active == AppCommandState::Kind::GeoReorientMarker ||
           st.active == AppCommandState::Kind::DrawingSettingsPick ||
           st.active == AppCommandState::Kind::GeoCaptureArea)  // REQ-359 / REQ-360 / REQ-364
    log.push_back(std::string(AppCommandState::KindName(st.active)) + " canceled.");
  else if (st.active == AppCommandState::Kind::AlignmentLayout ||
           st.active == AppCommandState::Kind::AlignmentFromObjects)  // REQ-485
    CancelAlignmentCommand(st, log);
  else if (st.active == AppCommandState::Kind::SurveyInverse)
    log.push_back("INVERSE canceled.");
  else if (st.active == AppCommandState::Kind::Dist)
    log.push_back("DIST canceled.");
  else if (st.active == AppCommandState::Kind::Zoom)
    log.push_back("ZOOM WINDOW canceled.");
  else if (st.active == AppCommandState::Kind::Pan)
    log.push_back("PAN — exited.");
  else if (st.active == AppCommandState::Kind::Orbit)
    log.push_back("ORBIT — exited.");
  else if (st.active == AppCommandState::Kind::VpFreeze)
    log.push_back("VPFREEZE — exited.");
  else if (st.active == AppCommandState::Kind::VpThaw)
    log.push_back("VPTHAW — exited.");
  else if (st.active == AppCommandState::Kind::PdfAttach)
    log.push_back("PDFATTACH canceled.");
  else if (st.active == AppCommandState::Kind::InsertBlock) {
    // REQ-350 (f) — drop the palette's one-shot splice arming with the command it belonged to.
    st.insertBlockPipeSpliceArmed = false;
    log.push_back("INSERT canceled.");
  }
  else if (st.active == AppCommandState::Kind::Paste)
    log.push_back("PASTE canceled.");
  else if (st.active == AppCommandState::Kind::PaperRectViewport) {
    st.paperVpPhase = 0;
    st.paperVpMode = 0;
    log.push_back("MVIEW canceled.");
  }
  else if (st.active == AppCommandState::Kind::BConnectMode)
    log.push_back("BCONNECTMODE canceled.");
  else if (st.active == AppCommandState::Kind::BConnect)
    log.push_back("BCONNECT canceled.");
  else if (st.active == AppCommandState::Kind::BConnectEdit)
    log.push_back("BCONNECTEDIT canceled.");
  else if (st.active == AppCommandState::Kind::BlockFitting)
    log.push_back("BLOCKFITTING canceled.");
  else if (st.active == AppCommandState::Kind::Align) {
    st.alignControlPts.clear();
    st.alignSelectionSnapshot.clear();
    st.alignSurveySnapshot.clear();
    st.alignHasSelection    = false;
    st.alignPhase           = AppCommandState::AlignPhase::PickSrc;
    st.selBoxWaitingSecond  = false;
    log.push_back("ALIGN canceled.");
  }
  if (prev == AppCommandState::Kind::Mleader && st.mtextRichEditorMultileaderJustPlaced)
    AbandonJustPlacedMultileader(st);
  st.active = AppCommandState::Kind::None;
  if (prev == AppCommandState::Kind::Offset)
    OffsetCmd::ResetOffsetDraft(st);
  st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
  ResetSegmentAngleLock(st);
  st.trimPhase = AppCommandState::TrimPhase::SelectCuttingEdges;
  st.trimCutters.clear();
  st.paperTrimCutters.clear();
  // EXTEND's boundary list and BREAK's pending entity are reset by ResetModifyRotateDraft below,
  // which now owns every modify command's draft state (TASK-099 F4) — duplicated here previously.
  ResetAllCadDraftTools(st);
  ResetModifyRotateDraft(st);
  ResetArrayDraft(st);
  st.selBoxWaitingSecond = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  ClearEntityGripInteraction(st);
  ClearDimGripInteraction(st);
  if (prev == AppCommandState::Kind::Zoom)
    ClearPendingViewportZoom(st);
}

// The `wx`/`wy` spelling inside SubmitViewportPickImpl and its callees is kept deliberately: those
// names appear hundreds of times across the pick handlers, and renaming them would bury this
// two-line clarification in an unreviewable diff. The space is stated here, at the entry point, which
// is where a caller looks.
void SubmitViewportPick(AppCommandState& st, double localX, double localY, std::vector<std::string>& log,
                         bool windowSelectionSubtract, bool fenceLeftToRightWindowMode,
                         const ray3d::Ray* pickRay) {
  ClearPendingOneShotObjectSnap(st);
  SubmitViewportPickImpl(st, localX, localY, log, windowSelectionSubtract, fenceLeftToRightWindowMode, pickRay);
}

// consume their clicks before ordinary paper selection can intercept them.

static void MaybeEstablishDocumentOriginFromTypedPoint(AppCommandState& st, const std::string& line,
                                                       std::vector<std::string>& log) {
  if (st.drawing.worldDocumentOriginX != 0.0 || st.drawing.worldDocumentOriginY != 0.0)
    return;  // frame already established — inert, and must stay inert (see above)
  double wx = 0.;
  double wy = 0.;
  // Absolute points only. A relative `@dx,dy` is resolved against an anchor that is already in the
  // current frame, so it cannot be the first thing that needs a new one.
  if (!ParseWorldPointD(line, &wx, &wy, /*allowRelative=*/false, 0., 0.))
    return;  // not a coordinate at all — a command name, a distance, a bare Enter
  const double mag = std::max(std::fabs(wx), std::fabs(wy));
  if (mag < CadCoord::kLargeCoordinateRebaseThreshold)
    return;  // small enough that the float local frame is already precise
  // And an upper bound: past this a value is not a coordinate, and building a frame around it would
  // make garbage representable instead of refused. See kMaxEstablishableOriginMagnitude.
  if (mag > CadCoord::kMaxEstablishableOriginMagnitude)
    return;
  CadCoord::ApplyDocumentOriginRebase(st, wx, wy, &log);
}

void ProcessCommandLineSubmit(char* cmdBuf, int cmdBufSize, AppCommandState& st, std::vector<std::string>& log) {
  [&]() {
  (void)cmdBufSize;
  std::string line = StringUtil::trimCopy(std::string(cmdBuf));

  // Before anything interprets this text (REQ-101). Placed ahead of dispatch so the frame is already
  // able to represent the value by the time a command's ParseStoragePoint narrows it.
  MaybeEstablishDocumentOriginFromTypedPoint(st, line, log);

  // Record the entry for the command bar's history dropdown (REQ-040): newest last,
  // skip blanks and consecutive duplicates, cap at 20.
  if (!line.empty() && (st.cmdEnteredHistory.empty() || st.cmdEnteredHistory.back() != line)) {
    st.cmdEnteredHistory.push_back(line);
    if (st.cmdEnteredHistory.size() > 20)
      st.cmdEnteredHistory.erase(st.cmdEnteredHistory.begin());
  }

  // GIZMO MOVE | ROTATE | SCALE | OFF — the PERSISTENT REQ-060 gizmo (TASK-232; OFF and the
  // persistence itself from GitHub issue #564 section 3, D-2026-09-28-a). A selection shows no
  // gizmo by default; naming an op here turns on the old behaviour — the gizmo follows every
  // selection, doing that op — and OFF returns to the default. 3DMOVE / 3DROTATE / 3DSCALE are the
  // everyday way to summon it. Consumed here, ahead of the main dispatch, because it is a one-line
  // SETTING with no phases. Bare `GIZMO` reports the current setting rather than changing it, so
  // the command is safe to type when you have forgotten where it is.
  {
    std::istringstream gz(StringUtil::toLowerAsciiCopy(line));
    std::string gzCmd;
    if ((gz >> gzCmd) && gzCmd == "gizmo") {
      if (IsGizmoCommandKind(st.active)) {
        // The running command owns the op until it ends; a change now would be undone on exit.
        log.push_back(std::string("GIZMO — finish or cancel ") + AppCommandState::KindName(st.active) +
                      " first.");
        return;
      }
      std::string mode;
      const bool haveMode = static_cast<bool>(gz >> mode);
      const auto opName = [](CadGizmoOp o) {
        return o == CadGizmoOp::Translate ? "MOVE" : o == CadGizmoOp::Rotate ? "ROTATE" : "SCALE";
      };
      if (!haveMode) {
        log.push_back(std::string("GIZMO — currently ") +
                      (st.gizmoPersistent ? opName(st.gizmoOp) : "OFF") +
                      ". Use GIZMO MOVE, GIZMO ROTATE or GIZMO SCALE to show it on every "
                      "selection, GIZMO OFF to hide it, or 3DMOVE / 3DROTATE / 3DSCALE.");
        return;
      }
      if (mode == "off" || mode == "0" || mode == "no") {
        CancelGizmoDrag(st);
        st.gizmoPersistent = false;
        st.gizmoHoverAxis = -1;
        log.push_back("GIZMO — OFF. Selecting shows no gizmo; use 3DMOVE, 3DROTATE or 3DSCALE.");
        return;
      }
      CadGizmoOp want = st.gizmoOp;
      if (mode == "move" || mode == "m" || mode == "translate")
        want = CadGizmoOp::Translate;
      else if (mode == "rotate" || mode == "ro" || mode == "r")
        want = CadGizmoOp::Rotate;
      else if (mode == "scale" || mode == "sc" || mode == "s")
        want = CadGizmoOp::Scale;
      else {
        log.push_back("GIZMO — unknown mode '" + mode + "'. Use MOVE, ROTATE, SCALE or OFF.");
        return;
      }
      // An armed drag was measured in the OLD operation's units, so it cannot survive the switch.
      // Cancelled before the change, because the cancel's neutral value depends on the op it is
      // ending (see `gizmoDragDistance`).
      CancelGizmoDrag(st);
      st.gizmoOp = want;
      st.gizmoPersistent = true;
      st.gizmoHoverAxis = -1;
      log.push_back(std::string("GIZMO — ") + opName(want) + ".");
      return;
    }
  }

  using K = AppCommandState::Kind;
  using LP = AppCommandState::LinePhase;
  using PP = AppCommandState::PolylinePhase;
  using SAP = AppCommandState::SegmentAnglePickPhase;

  // Grip stretch, direct-distance entry (REQ-047): with a grip armed there is no active command, so a bare
  // number would otherwise be dispatched as a command name. Consume it here and pin the grip that far along
  // the ORTHO axis the crosshair indicates. Anything else falls through to normal command dispatch, so the
  // command line still works while a grip is up.
  if (st.entityGripMoveActive) {
    float gripDist = 0.f;
    if (ParseSingleFloatToken(line, &gripDist)) {
      if (std::fabs(gripDist) < 1e-20f) {
        log.push_back("Grip — distance must be non-zero.");
        return;
      }
      float ux = 0.f;
      float uy = 0.f;
      if (st.entityGripLengthenMode) {
        // Lengthen (REQ-356): the direction is the line's own frozen angle, not wherever the
        // cursor happens to be — a typed distance here never needs the crosshair off-axis.
        ux = st.entityGripLengthenDirX;
        uy = st.entityGripLengthenDirY;
      } else {
        float cursorLocalX = 0.f;
        float cursorLocalY = 0.f;
        CadCoord::LocalFromWorld(st, static_cast<double>(st.uiCursorWorldX), static_cast<double>(st.uiCursorWorldY),
                                 &cursorLocalX, &cursorLocalY);
        if (!OrthoUnitTowardPoint(st.entityGripAnchorX, st.entityGripAnchorY, cursorLocalX, cursorLocalY, &ux,
                                  &uy)) {
          log.push_back("Grip distance needs a direction — move the crosshair off the grip, then enter a distance.");
          return;
        }
      }
      st.entityGripTypedX = st.entityGripAnchorX + ux * gripDist;
      st.entityGripTypedY = st.entityGripAnchorY + uy * gripDist;
      st.entityGripTypedDistanceValid = true;
      // Arming the grip already pushed the pre-drag undo snapshot, so the typed placement just overwrites
      // whatever the live drag had put there — one undo returns the entity to where it started.
      ApplyEntityGripPoint(st, st.entityGripTypedX, st.entityGripTypedY, CadCommitElevation(st));
      char gripMsg[128];
      if (st.entityGripLengthenMode)
        std::snprintf(gripMsg, sizeof(gripMsg), "Grip lengthened %.6g along the line's angle.",
                      static_cast<double>(gripDist));
      else
        std::snprintf(gripMsg, sizeof(gripMsg), "Grip stretched %.6g %s.",
                      static_cast<double>(std::fabs(gripDist)),
                      ux > 0.f ? "right" : (ux < 0.f ? "left" : (uy > 0.f ? "up" : "down")));
      log.push_back(gripMsg);
      ClearEntityGripInteraction(st);
      BumpCadGpuCache(st);
      return;
    }
  }

  if (st.active == K::PaperRectViewport && !line.empty()) {
    const std::string option = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));
    if (option == "fit" || option == "2" || option == "3" || option == "4" || option == "object") {
      (void)CreateMviewPreset(st, option, log);
      return;
    }
    log.push_back("MVIEW — option must be Fit, 2, 3, 4, or Object; Esc cancels.");
    return;
  }
  // #764: typed points for the click-driven paper MOVE/COPY/ROTATE/MIRROR/SCALE gestures.
  if (HandlePaperPhaseText(st, line, log))
    return;

  const bool segPickNeedAdjust =
      ((st.active == K::Line && st.linePhase == LP::NeedNextPoint) ||
       (st.active == K::Polyline && st.polylinePhase == PP::NeedNextPoint)) &&
      st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit;

  if (segPickNeedAdjust) {
    if (line.empty()) {
      CommitSegmentAnglePickLock(st, log);
      return;
    }
    const std::string t = StringUtil::trimCopy(line);
    if (!t.empty() && (t[0] == '+' || t[0] == '-')) {
      float dlt = 0.f;
      if (!ParseAngleDegreesInternal(t, &dlt)) {
        log.push_back("Bearing pick — invalid adjustment (decimal/DMS). Blank Enter locks; +/- adds turn.");
        return;
      }
      st.segmentPickDraftBearingDeg = NormalizeBearingDegreesCwNorth(st.segmentPickDraftBearingDeg + dlt);
      CommitSegmentAnglePickLock(st, log);
      return;
    }
    log.push_back("Bearing pick — blank Enter to lock, or +90 / -45 first (° clockwise from north).");
    return;
  }

  const bool linePolyNextNeedPoint =
      (st.active == K::Line && st.linePhase == LP::NeedNextPoint) ||
      (st.active == K::Polyline && st.polylinePhase == PP::NeedNextPoint);

  if (linePolyNextNeedPoint && st.segmentAngleKeyboardAwaitBearing) {
    if (line.empty()) {
      st.segmentAngleKeyboardAwaitBearing = false;
      log.push_back("Bearing entry canceled.");
      return;
    }
    float combined = 0.f;
    if (!ParseBearingCwNorthStringWithOptionalDelta(line, &combined, log)) {
      return;
    }
    const float theta = MathAngleRadFromBearingCwNorthDeg(combined);
    st.segmentLockUx = std::cos(theta);
    st.segmentLockUy = std::sin(theta);
    st.segmentAngleLockActive = true;
    st.segmentAnglePickPhase = SAP::Idle;
    st.segmentAngleKeyboardAwaitBearing = false;
    char bufKb[144];
    std::snprintf(bufKb, sizeof(bufKb),
                  "Bearing lock %.6g° clockwise from north — distance (+/- along ray) or click (A clears).",
                  static_cast<double>(combined));
    log.push_back(bufKb);
    return;
  }

  if (st.active == K::Mtext && st.mtextPhase == AppCommandState::MtextPhase::WaitString && !line.empty()) {
    log.push_back("MTEXT — type in the on-screen editor over the box (Ctrl+Enter reformats; Save to place).");
    return;
  }

  if (st.active == K::Spline) {
    (void)HandleSplineTextInput(line, st, log);
    return;
  }

  if (HandleCadDrawInquiryTextInput(line, st, log))
    return;

  if (line.empty()) {
    // REQ-307 (GitHub #106): paper-space MOVE/COPY/DELETE's selection step never sets st.active, so
    // it cannot be reached by any Kind-keyed branch below — checked first, ahead of all of them, the
    // same way the raw viewport Enter check in CadUi.cpp calls the identical shared logic.
    if (st.paperMoveWaitingSelection) {
      ProcessPaperMoveWaitingSelectionEnter(st, log);
      return;
    }
    if (st.paperDeleteWaitingSelection) {
      ProcessPaperDeleteWaitingSelectionEnter(st, log);
      return;
    }
    // TASK-082. FEATURELINE with a point awaiting its elevation: Enter accepts the default shown in
    // the prompt. This has to be handled HERE, not in the FEATURELINE block further down, because a
    // blank line never reaches that block — it is consumed by this one.
    if (st.active == K::FeatureLine && st.featureLinePendingPoint) {
      CommitFeatureLinePendingPoint(st, st.featureLinePendingDefaultZ, log);
      return;
    }
    // GRADING (REQ-398). A bare Enter is the command's ACTION — "grade now, with what is shown" —
    // so it has to be handled HERE for the same reason FEATURELINE's pending point is above: a
    // blank line never reaches the Kind-keyed block further down, this one consumes it first.
    // Without this the options prompt would sit there ignoring Enter, which is exactly the defect
    // PADSOLID shipped with (#150): a command that looks like it is waiting but cannot be told to go.
    if (st.active == K::Grading) {
      ProcessGradingCommandLine(st, line, log);
      return;
    }
    // REQ-485: Enter finishes a layout, ends object picking and accepts the direction - consumed here for the reason
    // every note in this block gives.
    if (st.active == K::AlignmentLayout || st.active == K::AlignmentFromObjects) {
      (void)HandleAlignmentCommandText(st, line, log);
      return;
    }
    // UCS / PLAN (REQ-154). A bare Enter is *meaningful* at several of their prompts — it takes the
    // <World> default, accepts an origin without an X-axis point, or accepts an X axis without a
    // third point — and it has to be handled HERE for the same reason FEATURELINE's is above: a
    // blank line never reaches the Kind-keyed block further down, this one consumes it first.
    if (st.active == K::Ucs) {
      ProcessUcsCommandLine(st, line, log);
      return;
    }
    if (st.active == K::Plan) {
      ProcessPlanCommandLine(st, line, log);
      return;
    }
    // The prompted solid primitives (REQ-313 as amended): Enter is what CREATES the solid, so it is
    // the most meaningful blank line of the lot. Handled here for exactly the reason FEATURELINE's
    // and UCS's notes above give — this block consumes a blank line and the Kind-keyed branch
    // further down never sees one.
    if (st.active == K::Solid) {
      (void)HandleSolidTextInput(line, st, log);
      return;
    }
    // REQ-317 POLYSOLID: Enter FINISHES the wall, so it is handled here for the same reason - this
    // block consumes a blank line and the Kind-keyed branch further down never sees one.
    if (st.active == K::Polysolid) {
      (void)HandlePolysolidTextInput(line, st, log);
      return;
    }
    // REQ-444 REVCLOUD: Enter closes the cloud, so it is handled here for the same reason.
    if (st.active == K::Revcloud) {
      (void)HandleRevcloudTextInput(line, st, log);
      return;
    }
    // PIPERUN (REQ-345 / D-2026-09-24-c): a bare Enter is meaningful at EVERY one of its prompts —
    // it keeps the remembered size, takes the schedule-40 wall, and finishes the run — and all three
    // are advertised in the prompt text itself. Handled here for the reason every note above gives:
    // this block consumes a blank line and the Kind-keyed branch further down never sees one, so
    // without this case the command's own blank-Enter handling (which is correct, and unit-tested)
    // was simply unreachable from the GUI. Measured through the Developer Shell before and after.
    if (st.active == K::PipeRun) {
      (void)HandlePipeRunTextInput(line, st, log);
      return;
    }
    // REQ-341 SECTIONCLIP: a bare Enter accepts the current state and closes the prompt, the way
    // TRIMSTATE's system-variable prompt does. Handled HERE for the reason every note above gives —
    // this block consumes a blank line and the Kind-keyed branch further down never sees one.
    if (st.active == K::SectionClip) {
      log.push_back(SectionClipReport(st));
      st.active = K::None;
      return;
    }
    // 3DMOVE / 3DROTATE / 3DSCALE (GitHub issue #564 section 3): Enter confirms the selection,
    // commits an armed drag, or ends the command — handled here for the reason every note above
    // gives: this block consumes a blank line and the Kind-keyed branch further down never sees one.
    if (IsGizmoCommandKind(st.active)) {
      (void)HandleGizmoCommandTextInput(line, st, log);
      return;
    }
    // CHPROP / MATCHPROP / LAYMCUR (REQ-356): Enter confirms CHPROP's selection, ends a value
    // prompt or ends the command — here for the same reason as the gizmo commands above.
    if (IsPropCommandKind(st.active)) {
      (void)HandlePropCommandTextInput(line, st, log);
      return;
    }
    if (st.active == K::Pan) {
      // Enter (or right-click in Enter mode) exits PAN; Esc exits via CancelActiveCommand.
      st.active = K::None;
      log.push_back("PAN — exited.");
      return;
    }
    if (st.active == K::Orbit) {
      // Same contract as PAN above (REQ-084 (c)).
      st.active = K::None;
      log.push_back("ORBIT — exited.");
      return;
    }
    if (st.active == K::VpFreeze || st.active == K::VpThaw) {
      // Enter exits VPFREEZE/VPTHAW; Esc exits via CancelActiveCommand (REQ-046).
      const char* nm = st.active == K::VpFreeze ? "VPFREEZE" : "VPTHAW";
      st.active = K::None;
      log.push_back(std::string(nm) + " — exited.");
      return;
    }
    if (st.active == K::Line && st.linePhase == AppCommandState::LinePhase::NeedNextPoint) {
      // Blank Enter ends the current line chain; restart LINE for the next one.
      ResetSegmentAngleLock(st);
      st.linePhase = AppCommandState::LinePhase::NeedFirstPoint;
      st.lineDraftSegments = 0;
      log.push_back("LINE — specify first point (click or type X,Y / X Y). ESC to cancel.");
      return;
    }
    if ((st.active == K::DimAligned || st.active == K::DimLinear) &&
        st.dimPhase == AppCommandState::DimPhase::WaitDimLinePt) {
      float lx = 0.f;
      float ly = 0.f;
      CadCoord::LocalFromWorld(st, static_cast<double>(st.uiCursorWorldX), static_cast<double>(st.uiCursorWorldY),
                               &lx, &ly);
      if (st.active == K::DimLinear)
        CommitDimLinearAt(st, lx, ly, log);
      else
        CommitDimAlignedAt(st, lx, ly, log);
      return;
    }
    if (st.active == K::Polyline && st.polylinePhase == PP::NeedNextPoint) {
      // REQ-303 (issue #80) / REQ-118: blank Enter finishes POLYLINE/3DPOLY OPEN, mirroring the
      // typed END keyword below (same minimum-segment gate, same commit call). Unlike LINE's blank
      // Enter above, this EXITS the command instead of restarting a new chain — the issue's explicit
      // ask, and CommitPolylineDraft already does the exiting (st.active = Kind::None). The asymmetry
      // with LINE was put to the user and chosen, not inherited by accident (D-2026-08-25-j).
      // CancelSegmentAnglePick/ResetSegmentAngleLock clear any in-progress bearing pick/lock first
      // (merge reconciliation, D-2026-08-25-l): CommitPolylineDraft's own draft reset
      // (ResetPolylineDraft) does not touch that state, so without this a lock left active when the
      // user pressed blank Enter would still read as active for whatever command starts next.
      CancelSegmentAnglePick(st, nullptr);
      ResetSegmentAngleLock(st);
      if (st.polyDraftSegments == 0)
        log.push_back("POLYLINE END — need at least one segment after the start point.");
      else
        CommitPolylineDraft(st, /*closed=*/false, log);
      return;
    }
    if (st.active == K::Trim) {
      using TP = AppCommandState::TrimPhase;
      const bool paperTrim = st.activeSpaceIndex != kModelSpaceIndex && !InFloatingModelSpace(st);
      if (st.trimPhase == TP::SelectCuttingEdges) {
        if (paperTrim ? st.paperTrimCutters.empty() : st.trimCutters.empty())
          log.push_back("TRIM — pick at least one cutting edge before pressing Enter.");
        else {
          st.trimPhase = TP::SelectTrimTargets;
          log.push_back("TRIM — click segments to trim (near the piece to remove). Enter when done.");
        }
      } else if (st.trimPhase == TP::CuttingLine_WaitP1 || st.trimPhase == TP::CuttingLine_WaitP2) {
        log.push_back("TRIM — specify cutting-line points in the viewport.");
      } else {
        st.active = K::None;
        st.trimPhase = TP::SelectCuttingEdges;
        st.trimCutters.clear();
        st.paperTrimCutters.clear();
        log.push_back("TRIM — finished.");
      }
    } else if (st.active == K::Extend) {
      using EP = AppCommandState::ExtendPhase;
      if (st.extendPhase == EP::SelectBoundaries) {
        if (st.extendBoundaries.empty())
          log.push_back("EXTEND — pick at least one boundary edge before pressing Enter.");
        else {
          st.extendPhase = EP::SelectTargets;
          log.push_back("EXTEND — click objects to extend (near the end to stretch). Enter when done.");
        }
      } else {
        st.active = K::None;
        st.extendPhase = EP::SelectBoundaries;
        st.extendBoundaries.clear();
        log.push_back("EXTEND — finished.");
      }
    } else if (st.active == K::Extrude) {
      // Enter confirms the selected profiles (SelectProfiles) or commits at the cursor height
      // (WaitHeight) — HandleExtrudeTextInput handles both, so the empty line is not swallowed here.
      (void)HandleExtrudeTextInput("", st, log);
    } else if (st.active == K::PressPull) {
      // Enter confirms the selected target (SelectTarget) or commits at the cursor distance
      // (WaitDistance) — HandlePressPullTextInput handles both, so the empty line is not swallowed
      // here. Without this branch a blank Enter (or right-click-as-Enter, or a raw Enter keypress
      // with the command line focused) during PRESSPULL falls through this whole block's else-if
      // chain, matches nothing, and is silently discarded by the `return` below — exactly EXTRUDE's
      // note above, for the same reason.
      (void)HandlePressPullTextInput("", st, log);
    } else if (st.active == K::Revolve) {
      // Enter confirms the selection, or (at the angle prompt) commits at the default angle.
      (void)HandleRevolveTextInput("", st, log);
    } else if (st.active == K::Loft) {
      // Enter skins the solid through the selected profiles (in selection order).
      (void)HandleLoftTextInput("", st, log);
    } else if (st.active == K::Sweep) {
      // Enter sweeps the selected profile along the selected path.
      (void)HandleSweepTextInput("", st, log);
    } else if (st.active == K::Slice) {
      (void)HandleSliceTextInput("", st, log);
    } else if (st.active == K::Section) {
      // Enter CONFIRMS the solid selection. This is the step that turns SECTION from a refusal into
      // a prompt (REQ-335 increment 2) — without it, the command ends before the first click.
      (void)HandleSectionTextInput("", st, log);
    } else if (st.active == K::Boolean) {
      (void)HandleBooleanTextInput("", st, log);
    } else if (st.active == K::Offset) {
      using OP = AppCommandState::OffsetPhase;
      if (st.offsetPhase == OP::WaitSelectEntity) {
        // REQ-103: Enter at "select object" (with no object picked yet) ends OFFSET — the same
        // "<Enter to exit>" TRIM/OFFSET's per-target loop supports, not just a hint message.
        log.push_back("OFFSET complete.");
        OffsetCmd::FinishOffsetAndIdle(st, log);
      } else if (st.offsetPhase == OP::WaitDistanceOrThrough)
        log.push_back("OFFSET — type a positive distance, or T for through-point mode.");
      else if (st.offsetPhase == OP::WaitSidePick)
        log.push_back("OFFSET — pick which side to offset.");
      else
        log.push_back("OFFSET — click the through point.");
    } else if (st.active == K::Move || st.active == K::Copy) {
      // This fix: PickSelection no longer auto-advances when a box finishes (SubmitViewportPickImpl
      // above), so Enter is what confirms the accumulated selection and moves on — same shape ALIGN
      // already used for its own selection step.
      using MP = AppCommandState::ModifyPhase;
      if (st.modifyPhase == MP::PickSelection) {
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
        else {
          st.modifyPhase = MP::NeedBase;
          log.push_back(st.active == K::Copy ? "COPY — base point:" : "MOVE — base point:");
        }
      }
    } else if (st.active == K::Delete) {
      // REQ-121 (GitHub #91 review, D-2026-08-26-d). DELETE used to answer Enter with "finish
      // window-select in the viewport (two clicks)" — a refusal that directly contradicted the
      // shared "Select objects, ENTER to continue" prompt PR #102 gave it. Enter now acts, the
      // same shape MOVE/COPY use directly above.
      //
      // Survey points first and CAD second, mirroring StartDeleteCommand's own order and for its
      // stated reason: deleting a point also removes its linked label, and running the CAD erase
      // first would strand that label in the selection.
      if (st.selection.empty() && st.selectedSurveyPointIndices.empty()) {
        log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
      } else {
        if (!st.selectedSurveyPointIndices.empty())
          DeleteSelectedSurveyPoints(st, log);
        if (!st.selection.empty())
          ExecuteDeleteSelection(st, log);
        st.active = K::None;
        ResetModifyRotateDraft(st);
      }
    } else if (st.active == K::Join) {
      // Survey points are not joinable and are deliberately not consulted here, so an Enter with
      // survey points alone is still "nothing selected" — the honest answer (REQ-201) rather than
      // a silent no-op. JOIN's own report says what it merged.
      const bool paperActive = ActivePaperGeometryTarget(st) != nullptr;
      if (paperActive ? st.selectedPaperEntities.empty() : st.selection.empty()) {
        log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
      } else {
        ExecuteJoinSelection(st, log);
        st.active = K::None;
        ResetModifyRotateDraft(st);
      }
    } else if (st.active == K::Scale) {
      using MP = AppCommandState::ModifyPhase;
      if (st.modifyPhase == MP::PickSelection) {
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
        else {
          st.modifyPhase = MP::NeedBase;
          log.push_back("SCALE — base point:");
        }
      }
    } else if (st.active == K::Rotate) {
      using RP = AppCommandState::RotatePhase;
      if (st.rotatePhase == RP::PickSelection) {
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
        else {
          st.rotatePhase = RP::NeedBase;
          log.push_back("ROTATE — base point:");
        }
      }
    } else if (st.active == K::Array) {
      using AP = AppCommandState::ArrayPhase;
      if (st.arrayPhase == AP::PickSelection) {
        if (st.arrayOnPaper) {
          if (st.selectedPaperEntities.empty()) {
            log.push_back("Nothing selected — click paper objects or drag a selection window, then press Enter.");
          } else {
            st.arrayPhase = AP::WaitType;
            SetArrayAnchorFromPaperSelection(st);
            log.push_back("ARRAY — select array type: [R]ectangular / [P]olar:");
          }
        } else {
          // Matches the old box-finish sequence exactly (finishBox(); DropArrayUnsupportedFrom-
          // Selection(); check st.selection.empty()), just triggered by Enter instead.
          DropArrayUnsupportedFromSelection(st, log);
          if (st.selection.empty())
            log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
          else {
            st.arrayPhase = AP::WaitType;
            SetArrayAnchorFromSelection(st);
            log.push_back("ARRAY — select array type: [R]ectangular / [P]olar:");
          }
        }
      } else if (st.arrayPhase == AP::Rect_WaitLevels) {
        // GitHub issue #400 increment 2: a bare Enter here is "1 level" — the pre-#400 2D grid —
        // handled HERE for the same reason FEATURELINE's/UCS's blank-Enter defaults above are: a
        // blank line never reaches HandleArrayText, this block consumes it first.
        st.arrayLevels = 1;
        CommitArrayRectangular(st, log);
      }
    } else if (st.active == K::Align) {
      using AP = AppCommandState::AlignPhase;
      if (st.alignPhase == AP::PickSelection) {
        st.alignSelectionSnapshot = st.selection;
        st.alignSurveySnapshot    = st.selectedSurveyPointIndices;
        st.alignHasSelection      = !st.alignSelectionSnapshot.empty() || !st.alignSurveySnapshot.empty();
        st.alignPhase             = AP::PickSrc;
        const int nCad = static_cast<int>(st.alignSelectionSnapshot.size());
        const int nSrv = static_cast<int>(st.alignSurveySnapshot.size());
        if (st.alignHasSelection)
          log.push_back("ALIGN — " + std::to_string(nCad) + " CAD, " + std::to_string(nSrv) +
                        " survey point(s). Pick SOURCE survey point 1 in drawing (snap to it).");
        else
          log.push_back("ALIGN — no selection; all geometry will be transformed. "
                        "Pick SOURCE survey point 1 in drawing (snap to it).");
      } else {
        ExecuteAlignCommand(st, log);
      }
    } else if (st.active == K::Mirror) {
      using MirP = AppCommandState::MirrorPhase;
      if (st.mirrorPhase == MirP::PickSelection) {
        // This fix: same shape as MOVE/COPY/SCALE/ROTATE/ARRAY above — Enter confirms the
        // accumulated selection instead of the box auto-advancing.
        if (st.selection.empty() && st.selectedSurveyPointIndices.empty())
          log.push_back("Nothing selected — click objects or drag a selection window, then press Enter.");
        else {
          st.mirrorPhase = MirP::NeedP1;
          log.push_back("MIRROR — specify first point of mirror line:");
        }
      } else {
        // A blank Enter past selection is exactly how MIRROR's "Erase source objects? [Yes/No] <N>"
        // prompt is meant to be answered (the documented default, D-2026-08-23-j) — but this whole
        // block always `return`s below, so without this case the empty line would be swallowed here
        // and never reach HandleMirrorText's own (identical) empty-string-defaults-to-No handling
        // further down.
        HandleMirrorText(st, "", log);
      }
    } else if (st.active == K::Lengthen) {
      using LP = AppCommandState::LengthenPhase;
      if (st.lengthenPhase == LP::WaitSelectOrMode) {
        // Blank Enter at "select object" ends LENGTHEN — same convention TRIM's target-picking
        // loop uses ("Enter when done"). Mode/value settings persist for the next invocation.
        st.active = K::None;
        ResetModifyRotateDraft(st);
        log.push_back("LENGTHEN — finished.");
      } else {
        HandleLengthenText(st, "", log);
      }
    } else if (st.active == K::Break) {
      using BP = AppCommandState::BreakPhase;
      if (st.breakPhase == BP::SelectFirstPoint) {
        // Blank Enter at "select object" ends BREAK — same convention LENGTHEN's loop uses.
        st.active = K::None;
        log.push_back("BREAK — finished.");
      } else {
        log.push_back("BREAK — specify second break point in the viewport.");
      }
    } else if (st.active == K::Fillet) {
      using FP = AppCommandState::FilletPhase;
      if (st.filletPhase == FP::WaitFirstEntity && !st.filletTextAwaitingRadius &&
          !st.filletTextAwaitingTrim && !st.filletSolidAwaitingRadius &&
          !CadSubObjectSelectionIsAllEdges(st)) {
        // Blank Enter at "select first object" ends FILLET — same convention LENGTHEN/BREAK's loop
        // uses. (Mid-prompt for R/T it falls through to HandleFilletText's own "must be a number"/
        // "type T or N" refusal instead, which is the more useful message there.)
        st.active = K::None;
        log.push_back("FILLET — finished.");
      } else if (st.filletTextAwaitingRadius || st.filletTextAwaitingTrim ||
                 st.filletSolidAwaitingRadius || CadSubObjectSelectionIsAllEdges(st)) {
        HandleFilletText(st, "", log);
      } else {
        log.push_back("FILLET — specify second object in the viewport.");
      }
    } else if (st.active == K::Chamfer) {
      using CP = AppCommandState::ChamferPhase;
      // The solid chamfer's distance prompt and a live solid-edge selection both count as "waiting
      // for a value", so blank Enter accepts the remembered distance instead of ending the command —
      // the same two terms FILLET's branch above carries (REQ-331).
      const bool awaitingValue = st.chamferTextAwaitingFirstValue || st.chamferTextAwaitingSecondDist ||
                                 st.chamferTextAwaitingAngle || st.chamferTextAwaitingTrim ||
                                 st.chamferSolidAwaitingDistance ||
                                 CadSubObjectSelectionIsAllEdges(st);
      if (st.chamferPhase == CP::WaitFirstEntity && !awaitingValue) {
        // Blank Enter at "select first object" ends CHAMFER — same convention FILLET's loop uses.
        st.active = K::None;
        log.push_back("CHAMFER — finished.");
      } else if (awaitingValue) {
        HandleChamferText(st, "", log);
      } else {
        log.push_back("CHAMFER — specify second object in the viewport.");
      }
    }
    return;
  }

  if (st.active == K::None) {
    std::istringstream issIdle(StringUtil::trimCopy(std::string(cmdBuf)));
    std::string plotTok;
    issIdle >> plotTok;
    plotTok = StringUtil::toLowerAsciiCopy(plotTok);
    if (CadBlocksTryIdleCommand(st, plotTok, issIdle, log))
      return;
    // `TRIMSTATE 1` sets it in one line; a bare `TRIMSTATE` falls through to the registry and prompts.
    if (plotTok == "trimstate") {
      int tv = 0;
      if (issIdle >> tv) {
        ApplyTrimStateValue(st, tv, log);
        return;
      }
    }
    // `IMPORTMODEL [path] [scale] [x] [y] [z]` (REQ-065). Bare form opens the file browser and uses
    // scale 1 at the origin, stating both — the same shape as the other file commands, and the form
    // that makes the import scriptable for verification.
    if (plotTok == "importmodel" || plotTok == "gltf" || plotTok == "import3d") {
      std::string path;
      double scale = 1.0;
      double ix = 0.0;
      double iy = 0.0;
      double iz = 0.0;
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      // A path may contain spaces, so quoted-first: "C:\a b\m.glb" 0.0833 100 200
      if (!rest.empty() && rest.front() == '"') {
        const size_t close = rest.find('"', 1);
        if (close != std::string::npos) {
          path = rest.substr(1, close - 1);
          rest = StringUtil::trimCopy(rest.substr(close + 1));
        }
      } else if (!rest.empty()) {
        std::istringstream ps(rest);
        ps >> path;
        std::getline(ps, rest);
      }
      {
        std::istringstream ns(rest);
        double v = 0.0;
        if (ns >> v) scale = v;
        if (ns >> v) ix = v;
        if (ns >> v) iy = v;
        if (ns >> v) iz = v;
      }
      if (path.empty()) {
        char buf[1024]{};
        if (!BrowseOpenFileGltfUtf8(buf, sizeof(buf))) {
          log.push_back("IMPORTMODEL — cancelled.");
          return;
        }
        path = buf;
        log.push_back("IMPORTMODEL — unit scale 1, insertion 0,0,0. "
                      "Use IMPORTMODEL \"<path>\" <scale> <x> <y> <z> to place it otherwise.");
      }
      if (!std::isfinite(scale) || scale == 0.0) {
        log.push_back("IMPORTMODEL — unit scale must be a non-zero finite number.");
        return;
      }
      ImportGltfModel(st, path, scale, ix, iy, iz, log);
      return;
    }
    // Single import command for every point-cloud format (REQ-172): E57, PTS, PTX, LAS, LAZ.
    // Format is decided from the chosen/typed path's own extension — StartPointCloudImportAsync
    // does that sniffing internally, so this handler never branches on format itself. Replaces
    // the earlier per-format IMPORTPTS/IMPORTPTX/IMPORTLAS/IMPORTLAZ commands (issue #825 follow-up:
    // one import command, not five).
    if (plotTok == "pointcloudattach" || plotTok == "importpointcloud") {
      std::string path;
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      // A path may contain spaces, so quoted-first: "C:\a b\scan.e57"
      if (!rest.empty() && rest.front() == '"') {
        const size_t close = rest.find('"', 1);
        if (close != std::string::npos)
          path = rest.substr(1, close - 1);
      } else if (!rest.empty()) {
        path = rest;
      }
      if (path.empty()) {
        char buf[1024]{};
        if (!BrowseOpenFilePointCloudUtf8(buf, sizeof(buf))) {
          log.push_back("POINTCLOUDATTACH — cancelled.");
          return;
        }
        path = buf;
      }
      // REQ-379 clause 1: in a project drawing a file from outside the project asks copy / link first;
      // the modal's answer starts the import.
      if (RequestProjectAttach(st, AppCommandState::ProjectAttachPrompt::Kind::PointCloud, path, log))
        return;
      StartPointCloudImportAsync(st, path, log);
      return;
    }
    if (plotTok == "exportpts") {
      ExportSelectedPointClouds(st, PointCloudExportFormat::Pts, log);
      return;
    }
    if (plotTok == "exportptx") {
      ExportSelectedPointClouds(st, PointCloudExportFormat::Ptx, log);
      return;
    }
    if (plotTok == "exportlas") {
      ExportSelectedPointClouds(st, PointCloudExportFormat::Las, log);
      return;
    }
    if (plotTok == "exportlaz") {
      ExportSelectedPointClouds(st, PointCloudExportFormat::Laz, log);
      return;
    }
    // `EXPORTLANDXML "<path>"` (issue #1002). The destination is part of the command so it runs the
    // same headless as in the Qt window; the window's own handler asks for it with a save dialog
    // when none is typed, then submits this form.
    if (plotTok == "exportlandxml") {
      std::string path;
      std::getline(issIdle, path);
      path = StringUtil::trimCopy(path);
      if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);
      if (path.empty())
        log.push_back("EXPORTLANDXML — type a destination, e.g. EXPORTLANDXML \"site.xml\".");
      else
        ExportLandXmlFile(st, path.c_str(), log);
      return;
    }
    // `IMPORTLANDXML "<path>"` (issue #1003, REQ-506). Like EXPORTLANDXML the file is part of the command;
    // the Qt window asks for it with an open dialog when none is typed, then submits this form.
    if (plotTok == "importlandxml") {
      std::string path;
      std::getline(issIdle, path);
      path = StringUtil::trimCopy(path);
      if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
        path = path.substr(1, path.size() - 2);
      if (st.activeDrawingIdx <= 0 || st.activeDrawingIdx >= static_cast<int>(st.drawingTabs.size()))
        log.push_back("IMPORTLANDXML — open a drawing first.");
      else if (path.empty())
        log.push_back("IMPORTLANDXML — type a file, e.g. IMPORTLANDXML \"site.xml\".");
      else if (ActiveProjectPointsReadOnly(st))
        log.push_back("IMPORTLANDXML — this project is read-only; nothing was imported.");
      else
        ImportLandXmlFile(st, path.c_str(), log);
      return;
    }
    // `VS SHADED` in one line; a bare `VISUALSTYLE` reports the current value and lists the options,
    // the same shape as a system-variable prompt (the TRIMSTATE precedent).
    if (plotTok == "visualstyle" || plotTok == "vs" || plotTok == "vscurrent") {
      std::string vsArg;
      if (issIdle >> vsArg) {
        ApplyVisualStyleValue(st, vsArg, log);
      } else {
        log.push_back(std::string("Visual style = ") + VisualStyleName(st.viewportVisualStyle) +
                      ". Usage: VS 2D | HIDDEN | SHADED.");
      }
      return;
    }
    // `PERFHUD` toggles the frame-time diagnostic overlay (issue #166 investigation). Unlike BENCH
    // it measures the LIVE drawing and the current command — the actual thing the user is doing —
    // broken into frame / viewport-UI / hover-pick / snap / render.
    // PIPEPERF — where the time goes while a pipe run is being routed (user request 2026-09-24).
    //
    // Live routing builds real geometry on every click (D-2026-09-24-d) and a swept tube is the most
    // expensive thing the command does, so "it is slow" needs to say WHICH of four things is slow:
    // the sweep, the display tessellation, the whole rebuild pass, or the per-frame ghost. The
    // counters reset when PIPERUN starts, so the report always describes the run just drawn.
    //
    // Reported to the command line AND written to a file, because the useful thing to do with this
    // is paste it to someone. Bare `PIPEPERF` reports; `PIPEPERF RESET` zeroes it by hand.
    if (plotTok == "pipeperf") {
      const AppCommandState::PipeRunPerf& p = st.pipeRunPerf;
      char line[256];
      std::vector<std::string> out;
      out.push_back("PIPEPERF - live pipe routing profile (ms)");
      std::snprintf(line, sizeof(line), "  route            : %d vertices at last rebuild, %d click(s)",
                    p.lastRunVerts, p.clicks);
      out.push_back(line);
      const auto row = [&](const char* name, const AppCommandState::PipeRunPerfStat& s, const char* unit) {
        std::snprintf(line, sizeof(line), "  %-17s: %6d %s  total %9.1f  avg %7.2f  max %7.2f", name,
                      s.calls, unit, s.totalMs, s.AvgMs(), s.maxMs);
        out.push_back(line);
      };
      row("sweep (tube)", p.sweep, "call(s) ");
      row("tessellate", p.tessellate, "solid(s)");
      row("rebuild pass", p.rebuild, "pass(es)");
      row("  .. faces", p.tessFaces, "solid(s)");
      row("  .. edges", p.tessEdges, "solid(s)");
      row("  .. isolines", p.tessIso, "solid(s)");
      row("ghost preview", p.ghost, "frame(s)");
      std::snprintf(line, sizeof(line), "  cache            : %d run(s) re-swept, %d reused",
                    p.runsSwept, p.runsReused);
      out.push_back(line);
      std::snprintf(line, sizeof(line), "  triangles        : %lld produced by display tessellation",
                    p.tessTriangles);
      out.push_back(line);
      const double perClick = p.clicks > 0 ? (p.sweep.totalMs + p.tessellate.totalMs) /
                                                 static_cast<double>(p.clicks)
                                           : 0.0;
      std::snprintf(line, sizeof(line), "  per click        : %.1f ms of sweep + tessellation", perClick);
      out.push_back(line);
      std::snprintf(line, sizeof(line), "  per ghost frame  : %.2f ms average", p.ghost.AvgMs());
      out.push_back(line);

      // Beside the user data directory, falling back to the working directory — best effort, and the
      // command line still carries the whole report either way.
      namespace fs = std::filesystem;
      fs::path outPath = UserDataDirectory();
      if (outPath.empty())
        outPath = fs::current_path();
      outPath /= "gosurvey-pipeperf.txt";
      std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
      if (f) {
        for (const std::string& l : out)
          f << l << "\n";
        f.close();
        out.push_back("  written to       : " + outPath.u8string());
      }
      for (const std::string& l : out)
        log.push_back(l);
      return;
    }

    if (plotTok == "perfhud" || plotTok == "framestats") {
      st.perfHudVisible = !st.perfHudVisible;
      log.push_back(std::string("PERFHUD — frame-time overlay ") + (st.perfHudVisible ? "ON." : "OFF."));
      return;
    }
    // The seven solid primitives (REQ-313 / ADR-045). One typed line each — base point in the active
    // UCS, then exact dimensions — which is what REQ-313's acceptance asks for and no more. The
    // active UCS supplies the orientation, so a cylinder gets an arbitrary 3D axis without a new
    // command or an axis argument (#120), the same rule REQ-312 settled for tilted arcs.
    // PRESSPULL (REQ-319, widened by GitHub issue #396) — the first command that EDITS a solid
    // rather than creating one, and (since #396) one that can also build a new one from a closed
    // 2D shape. A bare verb opens the prompted form (select a target, then a typed or dragged
    // distance — EXTRUDE's own shape); a distance argument is the one-line shortcut, same as
    // EXTRUDE's report-or-set split.
    // FILLET on a solid EDGE (REQ-323, GitHub issue #148 acceptance 5). The same verb the 2D fillet
    // uses, because it is the same idea and every CAD package spells it the same way — and the two
    // cannot be confused, because this path is taken ONLY when the sub-object selection holds solid
    // edges and nothing else, a state the 2D flow has never been able to reach. With no such
    // selection the line falls through untouched, so every existing FILLET habit and transcript
    // behaves exactly as before.
    if (plotTok == "fillet" && CadSubObjectSelectionIsAllEdges(st)) {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      CadFilletSolidEdges(st, restOfLine, log);
      return;
    }
    // CHAMFER on a solid EDGE (REQ-331). Same gate, same reasoning: only reachable when the
    // sub-object selection holds solid edges and nothing else, so every existing 2D CHAMFER habit
    // and transcript is untouched. `cha` is the 2D command's own alias and is honoured here too, so
    // the two spellings do not diverge.
    if ((plotTok == "chamfer" || plotTok == "cha") && CadSubObjectSelectionIsAllEdges(st)) {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      CadChamferSolidEdges(st, restOfLine, log);
      return;
    }
    if (plotTok == "presspull" || plotTok == "pp") {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      if (StringUtil::trimCopy(restOfLine).empty())
        StartPressPullCommand(st, log);
      else
        CadPressPull(st, restOfLine, log);
      return;
    }
    if (CadIsSolidPrimitiveVerb(plotTok)) {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      // Arguments mean the one-line form; a bare verb opens the prompted one (REQ-313 as amended).
      // The same report-or-set split `VS` and `PERSPECTIVE` use, and it is what keeps every existing
      // transcript and the typed-dimensions acceptance working unchanged.
      if (StringUtil::trimCopy(restOfLine).empty())
        StartSolidPrimitiveCommand(st, plotTok, log);
      else
        CadCreateSolidPrimitive(st, plotTok, restOfLine, log);
      return;
    }
    // `SOLIDLIST` reports every solid's kind, layer, volume, surface area and topology counts —
    // the numbers #120's Solid Properties section asks for, on the surface that exists today.
    // `ISOLINES 8` sets how many curves are drawn around a curved solid face; a bare `ISOLINES`
    // reports it. The report-or-set shape `VS` and `PERSPECTIVE` use, and AutoCAD's own name for the
    // setting, so someone who knows the variable finds it where they expect.
    // REQ-317 POLYSOLID: a wall swept along a picked path. Prompted only - a path has no fixed
    // argument count, so there is no one-line form to offer.
    if (plotTok == "polysolid" || plotTok == "psolid") {
      StartPolysolidCommand(st, log);
      return;
    }
    // REQ-444 REVCLOUD: prompted only - a path has no fixed argument count.
    if (plotTok == "revcloud") {
      StartRevcloudCommand(st, log);
      return;
    }
    // PIPERUN (issue #486 increment B2 / REQ-345): prompted only, same reason POLYSOLID is — a
    // routed path has no fixed argument count.
    if (plotTok == "piperun" || plotTok == "pipe") {
      StartPipeRunCommand(st, log);
      return;
    }
    // PIPESYS (issue #486 increment B3 / REQ-345): named piping networks. One-shot, text-only —
    // see HandlePipingSystemCommand's own doc comment for why it needs no Kind state machine.
    if (plotTok == "pipesys" || plotTok == "pipenet" || plotTok == "pipingsystem") {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      HandlePipingSystemCommand(restOfLine, st, log);
      return;
    }
    // LAYOUT (REQ-025, issue #751/#753 V3): typed NEW/RENAME/DELETE, mirroring the layout tab bar.
    if (plotTok == "layout") {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      HandleLayoutCommand(restOfLine, st, log);
      return;
    }
    // VPSCALE (REQ-027, issue #751/#753 V3): typed equivalent of the viewport-scale combo.
    if (plotTok == "vpscale") {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      HandleVpscaleCommand(restOfLine, st, log);
      return;
    }
    // VPMATCHSCALE (REQ-484): apply a named drawing scale to selected paper viewport(s).
    if (plotTok == "vpmatchscale") {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      HandleVpMatchScaleCommand(restOfLine, st, log);
      return;
    }
    // PAGESETUP (REQ-026, issue #751/#753 V3): typed equivalent of "Page Setup Manager…".
    if (plotTok == "pagesetup") {
      StartPageSetupCommand(st, log);
      return;
    }
    // PIPEFIT (issue #486 increment B7 / REQ-345): manual fitting placement on an already-selected
    // pipe run. The part type is a required inline argument (unlike PIPERUN, there is no multi-turn
    // prompt phase for it) — the same "args upfront, one thing left to pick" shape as many other
    // point-picking commands.
    if (plotTok == "pipefit" || plotTok == "pfit") {
      std::string partTypeTok;
      issIdle >> partTypeTok;
      StartPipeFitCommand(st, partTypeTok, log);
      return;
    }
    // PIPESPLIT/PIPEJOIN/PIPEPROP (issue #486 increment B8 / REQ-345): the rest of Track B's edit
    // operations. PIPESPLIT is prompted (one point left to pick, PIPEFIT's own shape minus the
    // part-type argument); PIPEJOIN and PIPEPROP act immediately on the current selection, the same
    // "no state machine, selection is already the input" shape PIPESYS established.
    if (plotTok == "pipesplit" || plotTok == "psplit") {
      StartPipeSplitCommand(st, log);
      return;
    }
    if (plotTok == "pipejoin" || plotTok == "pjoin") {
      HandlePipeJoinCommand(st, log);
      return;
    }
    if (plotTok == "pipeprop" || plotTok == "pprop") {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      HandlePipePropCommand(restOfLine, st, log);
      return;
    }
    if (plotTok == "pdmode") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecutePdModeCommand(st, rest, log);
      return;
    }
    if (plotTok == "pdsize") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecutePdSizeCommand(st, rest, log);
      return;
    }
    if (plotTok == "isolines") {
      std::string isoArg;
      if (issIdle >> isoArg) {
        char* end = nullptr;
        const long v = std::strtol(isoArg.c_str(), &end, 10);
        if (isoArg.empty() || !end || *end != '\0' || v < 0 || v > kSolidMaxIsolines) {
          log.push_back("ISOLINES — enter a whole number between 0 and " +
                        std::to_string(kSolidMaxIsolines) + " (0 draws edges only).");
          return;
        }
        st.viewportSolidIsolines = static_cast<int>(v);
        BumpCadGpuCache(st);
        log.push_back("ISOLINES = " + std::to_string(st.viewportSolidIsolines) + ".");
      } else {
        log.push_back("ISOLINES = " + std::to_string(st.viewportSolidIsolines) +
                      ". Usage: ISOLINES <0-" + std::to_string(kSolidMaxIsolines) + ">.");
      }
      return;
    }
    if (plotTok == "solidlist" || plotTok == "solids") {
      CadReportSolids(st, log);
      return;
    }
    // MASSPROP (REQ-349, GitHub #460): detailed mass properties including inertia and principal axes.
    if (plotTok == "massprop" || plotTok == "massproperties" || plotTok == "solidmassprop" ||
        plotTok == "massp") {
      CadReportMassProperties(st, log);
      return;
    }
    // SECTION (REQ-335 increment 2): choose solids, then three points defining the plane — the
    // same order SLICE asks in, and the same order AutoCAD's SECTION asks in. The solids themselves
    // are untouched: it inspects rather than cuts, which is what separates it from SLICE.
    if (plotTok == "section") {
      StartSectionCommand(st, log);
      return;
    }
    // SOLIDCHECK (REQ-313 as amended, D-2026-09-09-j): validity, and separately self-intersection —
    // the geometric fault Validate deliberately calls Ok. Read-only; nothing is repaired.
    if (plotTok == "solidcheck" || plotTok == "scheck") {
      CadCheckSolids(st, log);
      return;
    }
    // EXTRUDE (REQ-314 / ADR-046 increment 1b): a selected closed polyline or circle becomes a
    // B-rep solid, swept the typed height perpendicular to its plane. One line, one undo step; the
    // source profile is left in place. The interactive "select objects / drag the height" form is a
    // later increment.
    if (plotTok == "extrude" || plotTok == "ext") {
      std::string restOfLine;
      std::getline(issIdle, restOfLine);
      // A bare verb opens the prompted form (select objects, then a typed or dragged height); a
      // height argument is the one-line shortcut. The same report-or-set split the solid primitives
      // use.
      if (StringUtil::trimCopy(restOfLine).empty())
        StartExtrudeCommand(st, log);
      else
        CadExtrudeSelection(st, restOfLine, log);
      return;
    }
    if (plotTok == "revolve" || plotTok == "rev") {
      StartRevolveCommand(st, log);
      return;
    }
    if (plotTok == "loft" || plotTok == "lft") {
      // A bare verb opens the prompted form (select profiles, Enter to build); with two or more
      // profiles already selected, StartLoftCommand builds straight away.
      StartLoftCommand(st, log);
      return;
    }
    if (plotTok == "sweep" || plotTok == "swp") {
      StartSweepCommand(st, log);
      return;
    }
    if (plotTok == "slice" || plotTok == "sl") {
      StartSliceCommand(st, log);
      return;
    }
    if (plotTok == "union" || plotTok == "uni") {
      StartBooleanCommand(st, CadBooleanOp::Union, log);
      return;
    }
    if (plotTok == "subtract" || plotTok == "su") {
      StartBooleanCommand(st, CadBooleanOp::Subtract, log);
      return;
    }
    if (plotTok == "intersect" || plotTok == "in") {
      StartBooleanCommand(st, CadBooleanOp::Intersect, log);
      return;
    }
    // `PERSPECTIVE ON` in one line; a bare `PERSPECTIVE` reports the current projection — the same
    // report-or-set shape as VS above (REQ-309). The maths behind this has existed since REQ-058;
    // until now nothing could select it.
    if (plotTok == "perspective" || plotTok == "projection" || plotTok == "persp") {
      std::string projArg;
      if (issIdle >> projArg) {
        ApplyProjectionValue(st, projArg, log);
      } else {
        log.push_back(std::string("Projection = ") + ProjectionName(st.viewportProjection) +
                      ". Usage: PERSPECTIVE ON | OFF.");
      }
      return;
    }
    // SECTIONCLIP (REQ-341): live section clipping — hide what is in front of the active UCS plane
    // so the interior can be inspected. Report-or-set, same shape as PERSPECTIVE and CROSSHAIR3D.
    // A view state: no undo entry and no geometry, which is what separates it from SECTION.
    if (plotTok == "sectionclip" || plotTok == "sclip" || plotTok == "clip") {
      // The REST of the line, not one token: `OFFSET -4` is two words and is a documented form, so
      // reading a single token would silently drop the number and refuse a request that was
      // perfectly well formed. `ON` and a bare `-4` come through the same path unchanged.
      std::string clipArg;
      std::getline(issIdle, clipArg);
      clipArg = StringUtil::trimCopy(clipArg);
      if (!clipArg.empty()) {
        ApplySectionClipValue(st, clipArg, log);
      } else {
        // Bare form: report and WAIT, so the keywords in the hint are clickable. The usage text
        // that used to live here is now the hint itself, where the options are links.
        StartSectionClipCommand(st, log);
      }
      return;
    }
    // SECTIONPLANE (REQ-342): put that same clip plane on a face of a solid, and draw it hatched so
    // it can be found. Aiming, not a new cut — see D-2026-09-11-b for why there is one plane.
    if (plotTok == "sectionplane" || plotTok == "splane") {
      StartSectionPlaneCommand(st, log);
      return;
    }
    // `CROSSHAIR3D ON` in one line; bare reports (REQ-310), same shape as VS and PERSPECTIVE.
    if (plotTok == "crosshair3d" || plotTok == "cursor3d" || plotTok == "xhair3d") {
      std::string chArg;
      if (issIdle >> chArg) {
        ApplyCrosshair3dValue(st, chArg, log);
      } else {
        log.push_back(std::string("3D crosshair = ") + (st.viewportCrosshair3d ? "ON" : "OFF") +
                      ". Usage: CROSSHAIR3D ON | OFF.");
      }
      return;
    }
    if (plotTok == "fov" || plotTok == "lens") {
      std::string fovArg;
      if (issIdle >> fovArg) {
        ApplyFovValue(st, fovArg, log);
      } else {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "FOV = %.4g degrees. Usage: FOV <%.0f-%.0f>.",
                      static_cast<double>(st.viewportFovDeg), static_cast<double>(kMinFovDeg),
                      static_cast<double>(kMaxFovDeg));
        log.push_back(buf);
      }
      return;
    }
    // `BENCH` runs the REQ-100 frame-budget measurement at the budget's own density; `BENCH <segs>`
    // and `BENCH <segs> <frames>` override it for a quick check or a longer sample.
    if (plotTok == "bench") {
      int segs = 250000;  // REQ-100: the density of a real topo with contours
      int frames = 900;   // ~1.25 turns of the scripted orbit after warm-up
      st.bench.surfacePointCount = 0;
      st.bench.surfaceTriangleCount = 0;
      st.bench.meshTriangleCount = 0;
      st.bench.solidCount = 0;
      st.bench.solidTriangleCount = 0;
      st.bench.alignmentCount = 0;
      st.bench.alignmentEntityCount = 0;

      // `BENCH SURFACE [points] [frames]` selects the surface cost profile (REQ-100 as amended,
      // ADR-028), `BENCH MESH [triangles] [frames]` the shaded-mesh one (REQ-100 (b)). All three
      // are measured separately because none predicts the others: a surface's edges are regenerated
      // display geometry, and a shaded triangle is depth-tested and lit where a line segment is not.
      std::string benchArg;
      const std::streampos afterTok = issIdle.tellg();
      if (issIdle >> benchArg) {
        std::string lower;
        for (char c : benchArg)
          lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (lower == "surface" || lower == "surf" || lower == "s") {
          int pts = 100000;  // REQ-100's surface profile: ~200k triangles
          int v = 0;
          if (issIdle >> v)
            pts = v;
          if (issIdle >> v)
            frames = v;
          st.bench.surfacePointCount = pts;
          StartFrameBudgetBench(st, 1, frames, log);
          return;
        }
        // `BENCH SOLID [count] [frames]` — the B-rep solid profile (REQ-313). Its own keyword and
        // not a mode of the mesh one, because it measures a different shape of work: many small
        // stream-uploaded batches with a cache lookup each, rather than one big indexed upload.
        if (lower == "solid" || lower == "solids") {
          int count = 400;  // a few hundred objects is what a real site model looks like
          int v = 0;
          if (issIdle >> v)
            count = v;
          if (issIdle >> v)
            frames = v;
          st.bench.solidCount = count;
          StartFrameBudgetBench(st, 1, frames, log);
          return;
        }
        // `BENCH ALIGNMENT [count] [entities] [frames]` — REQ-495 acceptance 4: 1 000 alignments of 100 entities.
        if (lower == "alignment" || lower == "alignments") {
          int count = 1000, entities = 100;
          int v = 0;
          if (issIdle >> v)
            count = v;
          if (issIdle >> v)
            entities = v;
          if (issIdle >> v)
            frames = v;
          st.bench.alignmentCount = std::clamp(count, 1, 100000);
          st.bench.alignmentEntityCount = std::clamp(entities, 1, 100000);
          StartFrameBudgetBench(st, 1, frames, log);
          return;
        }
        // `BENCH PDFVIEW [pages]` — REQ-387: open a synthetic PDF of that many pages in the viewer
        // and scroll it top to bottom, reporting first-page time and the viewer's per-frame cost.
        if (lower == "pdfview" || lower == "pdf") {
          int pages = 500;
          int v = 0;
          const std::streampos beforeArg = issIdle.tellg();
          if (issIdle >> v) {
            pages = std::clamp(v, 1, 5000);
          } else {  // not a number: the rest of the line is a real PDF to time (its path may hold spaces)
            issIdle.clear();
            issIdle.seekg(beforeArg);
            std::string path;
            std::getline(issIdle, path);
            path = StringUtil::trimCopy(path);
            if (!path.empty()) {
              st.pdfViewBenchPath = path;
              log.push_back("BENCH PDFVIEW - timing " + path);
              return;
            }
          }
          st.pdfViewBenchPages = pages;
          log.push_back("BENCH PDFVIEW — building a " + std::to_string(pages) + "-page PDF; the viewer window will scroll it.");
          return;
        }
        // `BENCH PDFCOMPARE [pages]` — REQ-392: overlay two generated PDFs of that many pages in a
        // viewer, zoom and pan the overlay, and report the viewer's per-frame cost.
        if (lower == "pdfcompare") {
          int pages = 500;
          int v = 0;
          if (issIdle >> v)
            pages = std::clamp(v, 1, 5000);
          st.pdfCompareBenchPages = pages;
          log.push_back("BENCH PDFCOMPARE — building two " + std::to_string(pages) + "-page PDFs; the viewer window will overlay and pan them.");
          return;
        }
        // `BENCH PDFDIFF` — REQ-393: automatically align and compare a generated pair of 36 x 24 in sheets (a few
        // hundred thousand line segments) and report the time and the worst viewer frame.
        if (lower == "pdfdiff") {
          st.pdfDiffBench = true;
          log.push_back("BENCH PDFDIFF — building two 36 x 24 in sheets; the viewer window will align and compare them.");
          return;
        }
        if (lower == "mesh" || lower == "m") {
          int tris = 2000000;  // REQ-100 (b): the density decided 2026-08-15, TASK-041's fixture
          int v = 0;
          if (issIdle >> v)
            tris = v;
          if (issIdle >> v)
            frames = v;
          st.bench.meshTriangleCount = tris;
          StartFrameBudgetBench(st, 1, frames, log);
          return;
        }
        // `BENCH POINTCLOUD <path>` — REQ-100 profile (e) (TASK-270 §14). Unlike SURFACE/SOLID/MESH,
        // the reference scene is not synthesized; it is the real file at <path>, named explicitly by
        // REQ-100 as "the user's own driving E57." The path is read as the rest of the line (not a
        // single token) because a real file path can contain spaces; there is no inline frame-count
        // override in this form for the same reason — a trailing number would be ambiguous against
        // the path, so this form always uses the 900-frame default.
        if (lower == "pointcloud" || lower == "cloud" || lower == "pc") {
          std::string path;
          std::getline(issIdle, path);
          path = StringUtil::trimCopy(path);
          StartPointCloudBench(st, path, log);
          return;
        }
        issIdle.clear();
        issIdle.seekg(afterTok);  // neither keyword: re-read the token as a segment count
      } else {
        issIdle.clear();
      }
      int v = 0;
      if (issIdle >> v)
        segs = v;
      if (issIdle >> v)
        frames = v;
      StartFrameBudgetBench(st, segs, frames, log);
      return;
    }
    // `UCS <option>` / `PLAN <option>` — the inline forms (REQ-154). The command is opened and the
    // rest of the line handed to its own prompt handler, so `UCS X` reaches the same code `UCS`
    // then `X` does. `UCS` used to be an alias for ELEV; it is now the real command, and ELEV keeps
    // the elevation-only half it always had.
    // UCSFOLLOW (REQ-154): 0 leaves the view alone when the UCS changes, 1 switches to a PLAN view
    // of the new UCS. A system variable rather than a prompted command, which is what AutoCAD makes
    // it and what the surrounding TRIMSTATE / PLOTSCALE entries here already look like.
    if (plotTok == "ucsfollow") {
      std::string fArg;
      if (!(issIdle >> fArg)) {
        log.push_back("UCSFOLLOW = " + std::to_string(st.ucsFollow ? 1 : 0) +
                      "  (0 = changing the UCS leaves the view alone, 1 = it switches to a plan view "
                      "of the new UCS)");
        return;
      }
      if (fArg == "0" || fArg == "1") {
        st.ucsFollow = (fArg == "1");
        log.push_back(st.ucsFollow ? "UCSFOLLOW = 1 - changing the UCS now switches to a plan view of it."
                                   : "UCSFOLLOW = 0 - changing the UCS leaves the view alone.");
      } else {
        log.push_back("UCSFOLLOW — enter 0 or 1.");
      }
      return;
    }
    // VIEW (REQ-106) — inline in every form, so it never leaves a prompt open. Placed beside UCS
    // because they are the same shape: a frame/view is saved, restored and deleted by name.
    if (plotTok == "view" || plotTok == "v" || plotTok == "ddview") {
      std::string rest;
      std::getline(issIdle, rest);
      ProcessViewCommandLine(st, rest, log);
      return;
    }
    if (plotTok == "ucs" || plotTok == "plan") {
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      if (plotTok == "ucs") {
        StartUcsCommand(st, log);
        if (!rest.empty())
          ProcessUcsCommandLine(st, rest, log);
      } else {
        StartPlanCommand(st, log);
        if (!rest.empty())
          ProcessPlanCommandLine(st, rest, log);
      }
      return;
    }
    // `ELEV 12.5` — the inline form. Falling through to the prompt when no argument follows is what
    // makes bare `ELEV` still work.
    if (plotTok == "elev") {
      std::string evArg;
      if (issIdle >> evArg) {
        const std::string evLow = StringUtil::toLowerAsciiCopy(evArg);
        if (evLow == "w" || evLow == "world") {
          ApplyUcsWorld(st, log);
          return;
        }
        try {
          size_t used = 0;
          const double ez = std::stod(evArg, &used);
          if (used == evArg.size()) {
            ApplyElevValue(st, ez, log);
            return;
          }
        } catch (...) { // NOLINT(bugprone-empty-catch) - not a number; falls through to the usage message below
        }
        log.push_back("ELEV — usage: ELEV <elevation>, or ELEV W for world.");
        return;
      }
    }
    if (plotTok == "plotscale" || plotTok == "pscale") {
      float pv = 0.f;
      if (!(issIdle >> pv) || pv <= 0.f)
        log.push_back("PLOTSCALE — usage: PLOTSCALE <model_units_per_plotted_inch> (example: 50 for 1\"=50').");
      else {
        if (pv != st.drawing.modelUnitsPerPlottedInch) {
          PushUndoSnapshot(st, "PLOTSCALE");  // the plot scale is undoable (REQ-357)
          SetDrawingPlotScale(st, pv);
          NoteUserPlotScale(st);  // REQ-375
        }
        log.push_back("Plot scale: 1 plotted inch = " + std::to_string(pv) + " model units.");
      }
      return;
    }
    // REQ-071. `EXTRACT <surface>[, <layer>]` — comma-separated, because a surface name and a layer
    // name both routinely contain spaces.
    // ADR-069 (d), GitHub #150: what in this drawing moves when a surface is rebuilt.
    if (plotTok == "drapelinks") {
      ExecuteDrapeLinksCommand(st, log);
      return;
    }
    // GitHub #150 (3D Phase 7). `DRAPE <surface>[, LINK]` - lay the selection on the ground.
    if (plotTok == "drape") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteDrapeCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    // GitHub #150 (3D Phase 7). `PADSOLID <surface>, <boundary id>, <pad elevation>` - the cut and
    // fill a building pad represents, as solids you can measure (D-2026-10-02-c).
    if (plotTok == "padsolid" || plotTok == "pad") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecutePadSolidCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    if (plotTok == "extract") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteExtractCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    // REQ-070. `SURFSTYLE [<verb> …]` — bare opens the editor; the verbs are what a headless
    // transcript drives, because REQ-070's acceptance conditions are end-to-end and a dialog is not
    // reachable from one.
    if (plotTok == "surfstyle") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteSurfStyleCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    if (plotTok == "toolspace" || plotTok == "ts") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteToolspaceCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    // REQ-073. `VOLUMES <base surface>, <comparison surface>` — the synchronous command form of
    // the cut/fill/net report, reachable from a headless transcript the way SURFELEV/SURFSTYLE are.
    if (plotTok == "gradinggroup") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteGradingGroupCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    if (plotTok == "gradinginfill") {
      std::string name;
      std::getline(issIdle, name);
      ExecuteGradingInfillCommand(st, StringUtil::trimCopy(name), log);
      return;
    }
    if (plotTok == "volumes" || plotTok == "vol") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteVolumesCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    // REQ-073 amendment. `VOLDASH [<verb> …]` — bare opens the panel; the verbs are what a headless
    // transcript drives, because the panel itself is a Qt window this driver cannot reach.
    if (plotTok == "voldash") {
      std::string rest;
      std::getline(issIdle, rest);
      ExecuteVolDashCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    // REQ-087. `FEATURELINE [<name>]` — the whole remainder is the name, so it may contain spaces.
    if (plotTok == "featureline" || plotTok == "fl") {
      std::string rest;
      std::getline(issIdle, rest);
      StartFeatureLineCommand(st, StringUtil::trimCopy(rest), log);
      return;
    }
    if (plotTok == "featurelinelist" || plotTok == "fllist") {
      const size_t n = st.drawing.featureLineOffsets.empty() ? 0 : st.drawing.featureLineOffsets.size() - 1;
      if (n == 0) {
        log.push_back("FEATURELINELIST — no feature lines in the drawing.");
        return;
      }
      for (size_t i = 0; i < n; ++i) {
        const int b = st.drawing.featureLineOffsets[i], e = st.drawing.featureLineOffsets[i + 1];
        const std::string nm = i < st.drawing.featureLineInfo.size() ? st.drawing.featureLineInfo[i].name : std::string();
        const bool closed = i < st.drawing.featureLineClosed.size() && st.drawing.featureLineClosed[i] != 0;
        int elevPts = 0;
        for (int v = b; v < e; ++v)
          if (static_cast<size_t>(v) < st.drawing.featureLineElevPt.size() && st.drawing.featureLineElevPt[v])
            ++elevPts;
        log.push_back("Feature line " + std::to_string(i + 1) + ": \"" + nm + "\" — " +
                      std::to_string(e - b) + " vertices (" + std::to_string((e - b) - elevPts) +
                      " PI, " + std::to_string(elevPts) + " elevation point(s))" +
                      (closed ? ", closed" : "") + ", entity id " +
                      std::to_string(i < st.drawing.featureLineAttrs.size() ? st.drawing.featureLineAttrs[i].id : 0));
        for (int v = b; v < e; ++v) {
          const size_t base = static_cast<size_t>(v) * 3;
          if (base + 2 >= st.drawing.featureLineVerts.size())
            break;
          char buf[160];
          std::snprintf(buf, sizeof(buf), "  %s %d: %.3f, %.3f, elevation %.3f",
                        (static_cast<size_t>(v) < st.drawing.featureLineElevPt.size() && st.drawing.featureLineElevPt[v])
                            ? "elev pt"
                            : "PI     ",
                        v - b + 1, static_cast<double>(st.drawing.featureLineVerts[base]),
                        static_cast<double>(st.drawing.featureLineVerts[base + 1]),
                        static_cast<double>(st.drawing.featureLineVerts[base + 2]));
          log.push_back(buf);
        }
      }
      return;
    }
    // REQ-088 — the elevation editor's whole command surface. One verb with sub-actions rather than
    // seven top-level words, following UNDESIGNATE's shape:
    //
    //   FLELEV <n>                        list the table
    //   FLELEV <n> SET        <point> <elev>
    //   FLELEV <n> GRADEAHEAD <point> <pct>
    //   FLELEV <n> GRADEBACK  <point> <pct>
    //   FLELEV <n> RAISE      <delta>     negative lowers
    //   FLELEV <n> INSERT     <station> <elev>
    //   FLELEV <n> DELETE     <point>
    //
    // REQ-203: stage 2's panel routes its edited cells through exactly these, the way the Surfaces
    // panel routes through ProcessCommandLineSubmit — so what the tests drive is what the UI drives.
    // REQ-088: open the elevation editor, optionally aimed at a feature line. Separate from FLELEV
    // so that FLELEV stays usable with no window (REQ-203) — the driver must be able to read and
    // edit the table without a panel existing.
    if (plotTok == "flelevedit" || plotTok == "featurelineelevedit") {
      const int flCount =
          st.drawing.featureLineOffsets.empty() ? 0 : static_cast<int>(st.drawing.featureLineOffsets.size()) - 1;
      if (flCount <= 0) {
        log.push_back("FLELEVEDIT — no feature lines in the drawing. Draw one with FEATURELINE first.");
        return;
      }
      int which = 0;
      if (issIdle >> which) {
        if (which < 1 || which > flCount) {
          log.push_back("FLELEVEDIT — there is no feature line " + std::to_string(which) + "; the "
                        "drawing has " + std::to_string(flCount) + ".");
          return;
        }
        st.featureLineElevIndex = which - 1;
      }
      st.showFeatureLineElevWindow = true;
      log.push_back("FLELEVEDIT — elevation editor opened on feature line " +
                    std::to_string(st.featureLineElevIndex + 1) + ".");
      return;
    }
    if (plotTok == "flelev" || plotTok == "featurelineelev") {
      std::istringstream& fe = issIdle;  // the verb is already consumed
      int flNum = 0;
      if (!(fe >> flNum)) {
        log.push_back("FLELEV — usage: FLELEV <feature line #> [SET|GRADEAHEAD|GRADEBACK|RAISE|"
                      "INSERT|DELETE ...]. FLELEV <n> alone lists the table.");
        return;
      }
      std::string sub;
      if (!(fe >> sub)) {
        std::vector<FeatureLineElevRow> rows;
        if (!BuildFeatureLineElevTable(st, flNum - 1, &rows)) {
          log.push_back("FLELEV — no feature line " + std::to_string(flNum) + ".");
          return;
        }
        const size_t ii = (static_cast<size_t>(flNum)- 1);
        const std::string nm = ii < st.drawing.featureLineInfo.size() ? st.drawing.featureLineInfo[ii].name : std::string();
        log.push_back("FLELEV — feature line " + std::to_string(flNum) + " \"" + nm + "\": " +
                      std::to_string(rows.size()) + " points.");
        log.push_back("   #  type     station    elevation   length ahead   grade back   grade ahead");
        for (const FeatureLineElevRow& r : rows) {
          // A dash, not "0.00%", where a segment does not exist — see FeatureLineElevRow. Printing
          // zero there would state that the ground is level at a place that has no ground.
          const auto gradeText = [](double pct, char* buf, size_t cap) -> const char* {
            if (std::isnan(pct))
              return "-";
            std::snprintf(buf, cap, "%.2f%%", pct);
            return buf;
          };
          char gbBuf[16], gaBuf[16];
          const char* gb = gradeText(r.gradeBackPct, gbBuf, sizeof(gbBuf));
          const char* ga = gradeText(r.gradeAheadPct, gaBuf, sizeof(gaBuf));
          char buf[256];
          std::snprintf(buf, sizeof(buf), "  %2d  %-7s  %9.3f  %11.3f  %13.3f  %11s  %12s",
                        r.vertexIndex + 1, r.isElevationPoint ? "elev pt" : "PI", r.station,
                        static_cast<double>(r.elevation), r.lengthAhead, gb, ga);
          log.push_back(buf);
        }
        return;
      }
      for (char& c : sub)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (sub == "set" || sub == "gradeahead" || sub == "gradeback" || sub == "delete") {
        int pt = 0;
        if (!(fe >> pt)) {
          log.push_back("FLELEV — " + sub + " needs a point number.");
          return;
        }
        if (sub == "delete") {
          DeleteFeatureLineElevationPoint(st, flNum, pt, log);
          return;
        }
        double val = 0.0;
        if (!(fe >> val)) {
          log.push_back("FLELEV — " + sub + " needs a " + (sub == "set" ? "elevation." : "grade in percent."));
          return;
        }
        if (sub == "set")
          SetFeatureLinePointElevation(st, flNum, pt, static_cast<float>(val), log);
        else if (sub == "gradeahead")
          SetFeatureLineGradeAhead(st, flNum, pt, val, log);
        else
          SetFeatureLineGradeBack(st, flNum, pt, val, log);
        return;
      }
      if (sub == "raise") {
        double d = 0.0;
        if (!(fe >> d)) {
          log.push_back("FLELEV — RAISE needs a delta (negative lowers).");
          return;
        }
        RaiseFeatureLineElevations(st, flNum, static_cast<float>(d), log);
        return;
      }
      if (sub == "insert") {
        double station = 0.0, elev = 0.0;
        if (!(fe >> station) || !(fe >> elev)) {
          log.push_back("FLELEV — INSERT needs a station and an elevation.");
          return;
        }
        InsertFeatureLineElevationPoint(st, flNum, station, static_cast<float>(elev), log);
        return;
      }
      log.push_back("FLELEV — unknown option \"" + sub +
                    "\". Use SET, GRADEAHEAD, GRADEBACK, RAISE, INSERT, or DELETE.");
      return;
    }
    // Surface definition commands (REQ-068/069). Each reads the WHOLE remainder of the line — names
    // contain spaces — and splits on commas where it needs more than one argument. Together with
    // DESIGNATEBREAKLINE/DESIGNATEBOUNDARY below they make every surface operation reachable without
    // the Surfaces panel, which is what lets the REQ-203 driver exercise them at all: a surface has
    // no entity id, and panel buttons are unreachable with no window.
    if (plotTok == "surfacecreate" || plotTok == "sfcreate" || plotTok == "volumesurface" ||
        plotTok == "volsurf" || plotTok == "surfacerename" ||
        plotTok == "sfrename" || plotTok == "surfacedelete" || plotTok == "sfdelete" ||
        plotTok == "surfacerebuild" || plotTok == "sfrebuild" || plotTok == "surfacelist" ||
        plotTok == "sflist" || plotTok == "undesignate" || plotTok == "undes" ||
        plotTok == "surfaceaddfile" || plotTok == "sfaddfile" || plotTok == "surfaceimportfile" ||
        plotTok == "sfimportfile" || plotTok == "surfacestats" || plotTok == "sfstats" ||
        plotTok == "watershed" || plotTok == "wshed" || plotTok == "waterdrop" || plotTok == "wdrop" ||
        plotTok == "catchment" || plotTok == "catch" || plotTok == "surfacecreategrid" ||
        plotTok == "sfgrid" || plotTok == "surfacecreatecorr" || plotTok == "sfcorr" ||
        plotTok == "surfacecreatevolgrid" || plotTok == "sfvolgrid" || plotTok == "surfswapedge" ||
        plotTok == "sfswap" ||         plotTok == "surfaceaddpoint" || plotTok == "sfaddpt" ||
        plotTok == "surfacedelpoint" || plotTok == "sfdelpt" || plotTok == "surfacemovepoint" ||
        plotTok == "sfmovept" || plotTok == "surfdelline" || plotTok == "sfdelline" ||
        plotTok == "surflodlow" || plotTok == "surflodhigh" || plotTok == "movetosurface" ||
        plotTok == "imageattach" || plotTok == "surfdrape" || plotTok == "createdatashortcut" ||
        plotTok == "createsurfacereference" ||
        plotTok == "surfacevisibility" || plotTok == "surfvis" || plotTok == "surfcrossbl" || plotTok == "surflabel" || plotTok == "surfacelegend" || plotTok == "createprofilefromsurface" || plotTok == "profileview" ||
        plotTok == "quickprofile" || plotTok == "qprof" ||
        plotTok == "alignment" || plotTok == "createalignment" || plotTok == "alignmentadd" ||
        plotTok == "alignmentlist" || plotTok == "alignmentdelete" || plotTok == "createviewframes" || plotTok == "viewframedelete" || plotTok == "createsheets" || plotTok == "createsamplelines" || plotTok == "createsectionsheets" || plotTok == "createdesignprofile" || plotTok == "profilepvi" || plotTok == "profiledelpvi" || plotTok == "profilequery" || plotTok == "profilelist" || plotTok == "profiledelete" || plotTok == "getstationoffset" ||
        plotTok == "getpointfromstationoffset" || plotTok == "createalignmentfromobjects" ||
        plotTok == "createalignmententities" || plotTok == "createalignmentlayout" ||
        plotTok == "alignmentlayout" || plotTok == "alignmentcurvesettings" || plotTok == "stationformat" || plotTok == "alignmentmeta" ||
        plotTok == "alignmentedit" ||
        plotTok == "alignmentlock" ||
        plotTok == "alignmentinsert" ||
        plotTok == "alignmentremove" ||
        plotTok == "alignmentcontinuity" ||
        plotTok == "reversealignment" ||
        plotTok == "reversealigndirection" ||
        plotTok == "alignmentstation" ||
        plotTok == "alignmentstyle" ||
        plotTok == "alignmentlabelset" ||
        plotTok == "alignmentlabelstyle" ||
        plotTok == "alignmentreport" ||
        plotTok == "offsetalignment" ||
        plotTok == "alignmentdetach" ||
        plotTok == "alignmentintersect" ||
        plotTok == "editalignment" || plotTok == "alignmentsurface" || plotTok == "alignmentlabel" ||
        plotTok == "alignmenttable" || plotTok == "editalignmentstyle" || plotTok == "featurelinefromalignment" ||
        plotTok == "alignmentprops" || plotTok == "alignmentinverse" || plotTok == "alignmentbearing" ||
        plotTok == "alignmentdistance" ||
        plotTok == "alignmentcheckset" || plotTok == "alignmentcriteria" || plotTok == "alignmentspeed" ||
        plotTok == "alignmentcheck" ||
        plotTok == "volreport" || plotTok == "voltable" || plotTok == "volcsv") {
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      if (plotTok == "surfacecreate" || plotTok == "sfcreate")
        RunSurfaceCreate(st, rest, log);
      else if (plotTok == "surfacecreategrid" || plotTok == "sfgrid")
        RunSurfaceCreateGrid(st, rest, log);
      else if (plotTok == "surfacecreatecorr" || plotTok == "sfcorr")
        RunSurfaceCreateCorr(st, rest, log);
      else if (plotTok == "surfacecreatevolgrid" || plotTok == "sfvolgrid")
        RunSurfaceCreateVolGrid(st, rest, log);
      else if (plotTok == "surfswapedge" || plotTok == "sfswap")
        RunSurfSwapEdge(st, rest, log);
      else if (plotTok == "surfaceaddpoint" || plotTok == "sfaddpt")
        RunSurfAddPoint(st, rest, log);
      else if (plotTok == "surfacedelpoint" || plotTok == "sfdelpt")
        RunSurfDelPoint(st, rest, log);
      else if (plotTok == "surfacemovepoint" || plotTok == "sfmovept")
        RunSurfMovePoint(st, rest, log);
      else if (plotTok == "surflodlow")
        RunSurfaceLodCommand(st, rest, true, log);
      else if (plotTok == "imageattach") {
        const std::vector<std::string> fields = SplitCommaFields(rest);
        auto number = [&](size_t i, float* out) {
          if (i >= fields.size()) return false;
          char* end = nullptr;
          const double value = std::strtod(fields[i].c_str(), &end);
          if (!end || *end != '\0' || !std::isfinite(value) || std::abs(value) > 1.e30) return false;
          *out = static_cast<float>(value);
          return true;
        };
        float x = 0.f, y = 0.f, width = 0.f, height = 0.f, rotation = 0.f;
        if (fields.size() != 6 || fields[0].empty() || !number(1, &x) || !number(2, &y) ||
            !number(3, &width) || !number(4, &height) || !number(5, &rotation))
          log.push_back("IMAGEATTACH — usage: IMAGEATTACH <path>, x, y, width, height, rotation-degrees.");
        else
          AttachModelRasterImage(st, fields[0], x, y, width, height,
              rotation * static_cast<float>(3.14159265358979323846 / 180.0), log);
      }
      else if (plotTok == "surfdrape") {
        const std::vector<std::string> fields = SplitCommaFields(rest);
        char* end = nullptr;
        const unsigned long long id = fields.size() == 2 ? std::strtoull(fields[1].c_str(), &end, 10) : 0;
        const int surface = fields.size() == 2 ? FindSurfaceIndex(st, fields[0]) : -1;
        if (surface < 0 || !end || *end != '\0' || id == 0)
          log.push_back("SURFDRAPE — usage: SURFDRAPE <surface>, <image-id>.");
        else {
          const auto image = std::find_if(st.drawing.cadRasterImages.begin(), st.drawing.cadRasterImages.end(),
              [&](const CadRasterImage& candidate) { return candidate.id == static_cast<std::uint64_t>(id); });
          if (image == st.drawing.cadRasterImages.end()) log.push_back("SURFDRAPE — no attached image has ID " + std::to_string(id) + ".");
          else SetModelRasterDrape(st, static_cast<size_t>(image - st.drawing.cadRasterImages.begin()),
                                   static_cast<size_t>(surface), log);
        }
      }
      else if (plotTok == "createdatashortcut") {
        const int source = FindSurfaceIndex(st, StringUtil::trimCopy(rest));
        if (source < 0) log.push_back("CREATEDATASHORTCUT — name an existing source surface.");
        else CreateSurfaceDataShortcut(st, static_cast<size_t>(source), log);
      }
      else if (plotTok == "createsurfacereference") {
        const std::vector<std::string> fields = SplitCommaFields(rest);
        char* end = nullptr;
        const unsigned long long id = fields.size() == 3 ? std::strtoull(fields[1].c_str(), &end, 10) : 0;
        if (!id || !end || *end != '\0' || fields[0].empty() || fields[2].empty())
          log.push_back("CREATESURFACEREFERENCE — usage: CREATESURFACEREFERENCE <source-drawing-rel>, <source-surface-id>, <name>.");
        else CreateSurfaceReference(st, fields[0], static_cast<std::uint64_t>(id), fields[2], log);
      }
      else if (plotTok == "surflodhigh")
        RunSurfaceLodCommand(st, rest, false, log);
      else if (plotTok == "movetosurface")
        RunMoveToSurfaceCommand(st, rest, log);
      else if (plotTok == "surfacevisibility" || plotTok == "surfvis")
        RunSurfaceVisibility(st, rest, log);
      else if (plotTok == "surfcrossbl")
        RunSurfaceCrossings(st, rest, log);
      else if (plotTok == "surflabel")
        RunSurfaceLabel(st, rest, log);
      else if (plotTok == "surfacelegend")
        RunSurfaceLegend(st, rest, log);
      else if (plotTok == "createprofilefromsurface")
        RunCreateSurfaceProfile(st, rest, log);
      else if (plotTok == "profileview")
        RunSurfaceProfileView(st, rest, log);
      else if (plotTok == "surfdelline" || plotTok == "sfdelline")
        RunSurfDelLine(st, rest, log);
      else if (plotTok == "quickprofile" || plotTok == "qprof")
        RunQuickProfile(st, rest, log);
      else if (plotTok == "alignment" || plotTok == "createalignment" || plotTok == "createalignmentlayout")  // REQ-480 / 485
        RunAlignmentCreate(st, rest, log);
      else if (plotTok == "alignmentlayout")  // REQ-485.1
        RunAlignmentLayout(st, rest, log);
      else if (plotTok == "alignmentcurvesettings")
        RunAlignmentCurveSettings(st, rest, log);
      else if (plotTok == "stationformat")  // REQ-483.4
        RunStationFormat(st, rest, log);
      else if (plotTok == "alignmentmeta")  // REQ-480.1
        RunAlignmentMeta(st, rest, log);
      else if (plotTok == "createalignmententities")  // Civil 3D's command name for Create from Objects
        RunCreateAlignmentFromObjects(st, rest, log);
      else if (plotTok == "alignmentadd")
        RunAlignmentAdd(st, rest, log);
      else if (plotTok == "alignmentlist")
        RunAlignmentList(st, rest, log);
      else if (plotTok == "alignmentdelete")
        RunAlignmentDelete(st, rest, log);
      else if (plotTok == "createviewframes")
        RunCreateViewFrames(st, rest, log);
      else if (plotTok == "viewframedelete")
        RunViewFrameDelete(st, rest, log);
      else if (plotTok == "createsheets")
        RunCreateSheets(st, rest, log);
      else if (plotTok == "createsamplelines")
        RunCreateSampleLines(st, rest, log);
      else if (plotTok == "createsectionsheets")
        RunCreateSectionSheets(st, rest, log);
      else if (plotTok == "createdesignprofile")
        RunCreateDesignProfile(st, rest, log);
      else if (plotTok == "profilepvi")
        RunProfilePvi(st, rest, log);
      else if (plotTok == "profiledelpvi")
        RunProfileDelPvi(st, rest, log);
      else if (plotTok == "profilequery")
        RunProfileQuery(st, rest, log);
      else if (plotTok == "profilelist")
        RunProfileList(st, rest, log);
      else if (plotTok == "profiledelete")
        RunProfileDelete(st, rest, log);
      else if (plotTok == "getstationoffset")
        RunGetStationOffset(st, rest, log);
      else if (plotTok == "getpointfromstationoffset")
        RunGetPointFromStationOffset(st, rest, log);
      else if (plotTok == "createalignmentfromobjects")
        RunCreateAlignmentFromObjects(st, rest, log);
      else if (plotTok == "alignmentedit")
        RunAlignmentEdit(st, rest, log);
      else if (plotTok == "alignmentlock")
        RunAlignmentLock(st, rest, log);
      else if (plotTok == "alignmentinsert")
        RunAlignmentInsert(st, rest, log);
      else if (plotTok == "alignmentremove")
        RunAlignmentRemove(st, rest, log);
      else if (plotTok == "alignmentcontinuity")
        RunAlignmentContinuity(st, rest, log);
      else if (plotTok == "reversealignment")
        RunReverseAlignment(st, rest, log);
      else if (plotTok == "reversealigndirection")
        RunReverseAlignment(st, rest, log);
      else if (plotTok == "alignmentstation")
        RunAlignmentStation(st, rest, log);
      else if (plotTok == "alignmentstyle")
        RunAlignmentStyle(st, rest, log);
      else if (plotTok == "alignmentlabelset")
        RunAlignmentLabelSet(st, rest, log);
      else if (plotTok == "alignmentlabelstyle")
        RunAlignmentLabelStyle(st, rest, log);
      else if (plotTok == "alignmentreport")
        RunAlignmentReport(st, rest, log);
      else if (plotTok == "offsetalignment")
        RunOffsetAlignment(st, rest, log);
      else if (plotTok == "alignmentdetach")
        RunAlignmentDetach(st, rest, log);
      else if (plotTok == "alignmentintersect")
        RunAlignmentIntersect(st, rest, log);
      else if (plotTok == "editalignment")
        RunEditAlignment(st, rest, log);
      else if (plotTok == "alignmentsurface")
        RunAlignmentSurface(st, rest, log);
      else if (plotTok == "alignmentlabel")
        RunAlignmentLabel(st, rest, log);
      else if (plotTok == "alignmenttable")
        RunAlignmentTable(st, rest, log);
      else if (plotTok == "editalignmentstyle")
        RunEditAlignmentStyle(st, rest, log);
      else if (plotTok == "featurelinefromalignment")
        RunFeatureLineFromAlignment(st, rest, log);
      else if (plotTok == "alignmentprops")
        RunAlignmentProps(st, rest, log);
      else if (plotTok == "alignmentinverse")
        RunAlignmentInverse(st, rest, log);
      else if (plotTok == "alignmentbearing")
        RunAlignmentBearing(st, rest, log);
      else if (plotTok == "alignmentdistance")
        RunAlignmentDistance(st, rest, log);
      else if (plotTok == "alignmentcheckset")
        RunAlignmentCheckSet(st, rest, log);
      else if (plotTok == "alignmentcriteria")
        RunAlignmentCriteria(st, rest, log);
      else if (plotTok == "alignmentspeed")
        RunAlignmentSpeed(st, rest, log);
      else if (plotTok == "alignmentcheck")
        RunAlignmentCheck(st, rest, log);
      else if (plotTok == "volreport")
        RunVolReport(st, rest, log);
      else if (plotTok == "voltable")
        RunVolReport(st, "table", log);
      else if (plotTok == "volcsv")
        RunVolCsv(st, rest, log);
      else if (plotTok == "volumesurface" || plotTok == "volsurf")
        RunVolumeSurfaceCreate(st, rest, log);
      else if (plotTok == "surfacerename" || plotTok == "sfrename")
        RunSurfaceRename(st, rest, log);
      else if (plotTok == "surfacedelete" || plotTok == "sfdelete")
        RunSurfaceDelete(st, rest, log);
      else if (plotTok == "surfacerebuild" || plotTok == "sfrebuild")
        RunSurfaceRebuild(st, rest, log);
      else if (plotTok == "surfacelist" || plotTok == "sflist")
        ReportSurfaces(st, log);
      else if (plotTok == "surfaceaddfile" || plotTok == "sfaddfile")
        RunSurfaceAddFile(st, rest, log);
      else if (plotTok == "surfaceimportfile" || plotTok == "sfimportfile")
        RunSurfaceImportFile(st, rest, log);
      else if (plotTok == "surfacestats" || plotTok == "sfstats")
        RunSurfaceStats(st, rest, log);
      else if (plotTok == "watershed" || plotTok == "wshed")
        RunWatershed(st, rest, log);
      else if (plotTok == "waterdrop" || plotTok == "wdrop")
        RunWaterDropCommand(st, rest, log);
      else if (plotTok == "catchment" || plotTok == "catch")
        RunCatchmentCommand(st, rest, log);
      else
        RunUndesignate(st, rest, log);
      return;
    }
    // `DESIGNATEBREAKLINE <surface name>` (REQ-069) — the whole remainder is the name (surface
    // names can contain spaces, e.g. "Existing Ground"), so this reads the rest of the line rather
    // than one `>>` token.
    if (plotTok == "designatebreakline" || plotTok == "dbl") {
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      if (rest.empty()) {
        log.push_back("DESIGNATEBREAKLINE — usage: DESIGNATEBREAKLINE <surface name>.");
        return;
      }
      StartDesignateBreaklineCommand(st, rest, log);
      return;
    }
    if (plotTok == "designatecontour" || plotTok == "dcon") {
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      if (rest.empty()) {
        log.push_back("DESIGNATECONTOUR — usage: DESIGNATECONTOUR <surface name>.");
        return;
      }
      StartDesignateContourCommand(st, rest, log);
      return;
    }
    // `DESIGNATEBOUNDARY <surface name> <OUTER|HIDE|SHOW>` — the kind is always the LAST token, so
    // the surface name is whatever remains before it, spaces and all.
    if (plotTok == "designateboundary" || plotTok == "dbd") {
      std::string rest;
      std::getline(issIdle, rest);
      rest = StringUtil::trimCopy(rest);
      const size_t sp = rest.find_last_of(" \t");
      const bool usageError = rest.empty() || sp == std::string::npos;
      const std::string name = usageError ? std::string() : StringUtil::trimCopy(rest.substr(0, sp));
      const std::string kindWord =
          usageError ? std::string() : StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(rest.substr(sp + 1)));
      CadBoundaryKind kind = CadBoundaryKind::Outer;
      const bool kindOk = kindWord == "outer" || kindWord == "hide" || kindWord == "show" ||
                          kindWord == "clip" || kindWord == "mask";
      if (kindWord == "hide")
        kind = CadBoundaryKind::Hide;
      else if (kindWord == "show")
        kind = CadBoundaryKind::Show;
      else if (kindWord == "clip")
        kind = CadBoundaryKind::Clip;
      else if (kindWord == "mask")
        kind = CadBoundaryKind::Mask;
      if (usageError || name.empty() || !kindOk) {
        log.push_back("DESIGNATEBOUNDARY — usage: DESIGNATEBOUNDARY <surface name> <OUTER|HIDE|SHOW|CLIP|MASK>.");
        return;
      }
      StartDesignateBoundaryCommand(st, name, kind, log);
      return;
    }
  }

  // Typed TEXT during DELETE / JOIN. A bare Enter never reaches here — the `line.empty()` block
  // above consumes it, which is where these two now act on their selection (D-2026-08-26-d). This
  // is only the "you typed something that is not a command" case, and its wording had to change
  // with the behaviour: it used to say "finish window-select in the viewport (two clicks)", which
  // is no longer how either command ends.
  if (st.active == AppCommandState::Kind::Delete || st.active == AppCommandState::Kind::Join) {
    log.push_back(std::string(st.active == AppCommandState::Kind::Delete ? "DELETE" : "JOIN") +
                  " — click objects or drag a selection window, then press Enter. ESC cancels.");
    return;
  }

  if (st.active == AppCommandState::Kind::TrimState) {
    const std::string tsIn = StringUtil::trimCopy(line);
    if (tsIn.empty()) {  // bare Enter keeps the current value, as an AutoCAD system-variable prompt does
      log.push_back("TRIMSTATE unchanged (" + std::to_string(st.trimState) + ").");
      st.active = AppCommandState::Kind::None;
      return;
    }
    int tv = 0;
    std::istringstream tsIss(tsIn);
    if (!(tsIss >> tv) || !(tsIss >> std::ws).eof()) {
      log.push_back("TRIMSTATE — enter 0 or 1 (blank Enter keeps the current value).");
      return;
    }
    if (ApplyTrimStateValue(st, tv, log))
      st.active = AppCommandState::Kind::None;
    return;
  }

  // SECTIONCLIP's keyword prompt (REQ-341). Reached by a typed answer OR by clicking one of the
  // `[ON/OFF/FLIP]` links in the hint, which submit `on`, `off` and `flip` — the same three tokens
  // `ApplySectionClipValue` already accepts, so the click path and the typed path are one path.
  if (st.active == AppCommandState::Kind::SectionClip) {
    // A bare Enter never arrives here — the `line.empty()` block far above consumes every blank
    // line, and that is where this command's Enter is handled. No empty check, deliberately: one
    // that looked live here would be dead code inviting the next reader to maintain two answers.
    const std::string scIn = StringUtil::trimCopy(line);
    // A refusal keeps the prompt OPEN rather than dropping to idle: the links are still on screen
    // and still the fastest way to answer, so throwing the user out for a typo would take away the
    // thing they were most likely reaching for.
    if (ApplySectionClipValue(st, scIn, log))
      st.active = AppCommandState::Kind::None;
    return;
  }

  // UCS / PLAN own their whole prompt sequence, so a line goes straight to them (REQ-154). Placed
  // ahead of the generic point handling below because several of their prompts accept a bare
  // keyword that would otherwise be read as a failed coordinate.
  if (st.active == AppCommandState::Kind::Ucs) {
    ProcessUcsCommandLine(st, line, log);
    return;
  }
  if (st.active == AppCommandState::Kind::Plan) {
    ProcessPlanCommandLine(st, line, log);
    return;
  }

  if (st.active == AppCommandState::Kind::Elev) {
    const std::string evIn = StringUtil::trimCopy(line);
    if (evIn.empty()) {  // bare Enter keeps the current plane, as a system-variable prompt does
      char buf[96];
      std::snprintf(buf, sizeof(buf), "Elevation unchanged (%.4f).", static_cast<double>(CadWorkPlaneElevation(st)));
      log.push_back(buf);
      st.active = AppCommandState::Kind::None;
      return;
    }
    const std::string evLow = StringUtil::toLowerAsciiCopy(evIn);
    if (evLow == "w" || evLow == "world") {
      ApplyUcsWorld(st, log);
      st.active = AppCommandState::Kind::None;
      return;
    }
    double ez = 0.0;
    std::istringstream evIss(evIn);
    if (!(evIss >> ez) || !(evIss >> std::ws).eof()) {
      log.push_back("ELEV — enter an elevation, or W for world (blank Enter keeps the current value).");
      return;
    }
    if (ApplyElevValue(st, ez, log))
      st.active = AppCommandState::Kind::None;
    return;
  }

  if (st.active == AppCommandState::Kind::Trim) {
    using TP = AppCommandState::TrimPhase;
    const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));
    // L and T switch modes mid-run, so either style is reachable whatever TRIMSTATE is set to.
    if (low == "l" || low == "line") {
      if (st.trimPhase == TP::CuttingLine_WaitP1 || st.trimPhase == TP::CuttingLine_WaitP2)
        log.push_back("TRIM — already drawing the trim line; pick its points in the viewport.");
      else {
        st.trimCutters.clear();
        st.paperTrimCutters.clear();
        st.trimPhase = TP::CuttingLine_WaitP1;
        log.push_back("TRIM — draw along the segment to trim: first point (rubber band shows dashed preview).");
      }
      return;
    }
    if (low == "t" || low == "cutting" || low == "edges") {
      if (st.trimPhase == TP::SelectCuttingEdges || st.trimPhase == TP::SelectTrimTargets)
        log.push_back("TRIM — already picking cutting edges.");
      else {
        st.trimCutters.clear();
        st.paperTrimCutters.clear();
        st.trimPhase = TP::SelectCuttingEdges;
        log.push_back("TRIM — pick cutting edges (hover highlights), Enter when done, then click pieces to trim.");
      }
      return;
    }
    log.push_back("TRIM — viewport picks; L draws the trim line, T picks cutting edges; ESC cancels.");
    return;
  }

  if (st.active == AppCommandState::Kind::Zoom) {
    log.push_back("ZOOM WINDOW — finish two clicks in the viewport, or ESC to cancel.");
    return;
  }

  if (st.active == K::Offset) {
    using OP = AppCommandState::OffsetPhase;
    if (st.offsetPhase != OP::WaitDistanceOrThrough) {
      log.push_back("OFFSET — use viewport picks to select the object and its side.");
      return;
    }
    const std::string trimmed = StringUtil::trimCopy(line);
    if (StringUtil::toLowerAsciiCopy(trimmed) == "t" || StringUtil::toLowerAsciiCopy(trimmed) == "through") {
      st.offsetThroughMode = true;
      st.offsetTypedDistance = 0.f;
      st.offsetPhase = OP::WaitSelectEntity;
      log.push_back("OFFSET — through-point mode. Select line, circle, arc, ellipse, or polyline.");
      return;
    }
    float d = 0.f;
    if (!ParseOneFloat(trimmed, &d)) {
      log.push_back("OFFSET — type a positive offset distance (model units), or T for through-point mode.");
      return;
    }
    if (d <= 0.f) {
      log.push_back("OFFSET — distance must be positive.");
      return;
    }
    st.offsetThroughMode = false;
    st.offsetTypedDistance = d;
    st.offsetPhase = OP::WaitSelectEntity;
    log.push_back("OFFSET — select line, circle, arc, ellipse, or polyline.");
    return;
  }

  if (HandleGeoCommandText(st, line, log))  // REQ-359: GEOMARKLATLONG / GEOMARKPOINT / GEOREORIENTMARKER
    return;
  if (HandleAlignmentCommandText(st, line, log))  // REQ-485: ALIGNMENTLAYOUT / CREATEALIGNMENTFROMOBJECTS
    return;

  if (st.active == K::IdPoint) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("ID — pick in viewport or type X,Y (model units, UCS World).");
      return;
    }
    CommitIdPointAt(st, px, py, log);
    return;
  }
  if (st.active == K::StationOffsetPick) {  // REQ-484.4: a typed X,Y instead of a pick
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("GETSTATIONOFFSET - pick in the viewport or type X,Y; ESC ends.");
      return;
    }
    CommitStationOffsetPick(st, px, py, log);
    return;
  }

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    float px = 0.f;
    float py = 0.f;
    if (st.surveyInversePhase == SIP::WaitFrom) {
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("INVERSE — type X,Y (World) or pick first point in Drawing1.");
        return;
      }
      st.surveyInverseFromX = px;
      st.surveyInverseFromY = py;
      st.surveyInversePhase = SIP::WaitTo;
      log.push_back("INVERSE — second point (X,Y or @dx,dy from first).");
      return;
    }
    if (!ParseStoragePoint(st, line, &px, &py, true, st.surveyInverseFromX, st.surveyInverseFromY)) {
      log.push_back("INVERSE — type X,Y or @dx,dy from first point.");
      return;
    }
    CommitSurveyInverseSecondPoint(st, px, py, log);
    return;
  }

  if (st.active == K::Dist) {
    using DP = AppCommandState::DistPhase;
    float px = 0.f;
    float py = 0.f;
    if (st.distPhase == DP::WaitFrom) {
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("DIST — type X,Y (World) or pick first point in Drawing1.");
        return;
      }
      st.distFromX = px;
      st.distFromY = py;
      st.distFromZ = CadCommitElevation(st);
      st.distPhase = DP::WaitTo;
      log.push_back("DIST — second point (X,Y or @dx,dy from first).");
      return;
    }
    if (!ParseStoragePoint(st, line, &px, &py, true, st.distFromX, st.distFromY)) {
      log.push_back("DIST — type X,Y or @dx,dy from first point.");
      return;
    }
    CommitDistSecondPoint(st, st.distFromX, st.distFromY, st.distFromZ, px, py, CadCommitElevation(st), log);
    return;
  }

  if (st.active == K::BConnectMode) {
    BConnectModeSubmitLine(st, line, log);
    return;
  }

  if (st.active == K::BConnect) {
    BConnectSubmitLine(st, line, log);
    return;
  }

  if (st.active == K::BConnectEdit) {
    BConnectEditSubmitLine(st, line, log);
    return;
  }

  if (st.active == K::BlockFitting) {
    BlockFittingSubmitLine(st, line, log);
    return;
  }

  if (st.active == K::PdfAttach) {
    using PAP = AppCommandState::PdfAttachPhase;
    if (st.pdfAttachPhase == PAP::WaitInsertPoint) {
      float px = 0.f;
      float py = 0.f;
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("PDFATTACH — pick in viewport or type X,Y.");
        return;
      }
      SubmitPdfAttachInsertPoint(st, px, py, log);
      return;
    }
    if (st.pdfAttachPhase == PAP::WaitDialog || st.pdfAttachPhase == PAP::Building)
      log.push_back("PDFATTACH — complete the attach dialog, or ESC to cancel.");
    else
      log.push_back("PDFATTACH — pick the insertion point in the viewport, or ESC to cancel.");
    return;
  }

  if (st.active == K::InsertBlock) {
    using IPh = AppCommandState::InsertBlockPhase;
    if (st.insertBlockPhase == IPh::WaitDialog) {
      log.push_back("INSERT — complete the Insert dialog, or ESC to cancel.");
      return;
    }
    if (st.insertBlockPhase == IPh::WaitAttributes) {
      if (line.empty())
        CadBlocksCommitInsertAttrDialog(st, log);
      else
        log.push_back("INSERT — use the Edit Attributes dialog, or press Enter to accept.");
      return;
    }
    if (st.insertBlockPhase == IPh::WaitRotation) {
      if (line.empty()) {
        CadBlocksPlacePendingInsert(st, log);
        return;
      }
      if (line.find(',') == std::string::npos) {
        float ang = 0.f;
        if (ParseAngleDegrees(line, &ang)) {
          st.insertBlockRotDeg = ang;
          CadBlocksPlacePendingInsert(st, log);
          return;
        }
      }
    }
    if (st.insertBlockPhase == IPh::WaitInsertPoint) {
      float px = 0.f;
      float py = 0.f;
      float wz = 0.f;
      bool consumed = false;
      if (!ResolveTypedModifyPoint(st, line, true, 0.f, 0.f, 0.f, "INSERT", &px, &py, &wz, &consumed, log)) {
        if (consumed)
          return;
        log.push_back("INSERT — type X,Y or X,Y,Z or pick in the viewport.");
        return;
      }
      // ResolveTypedModifyPoint returns 0 for WCS with no explicit Z; the insertion point should
      // sit on the work plane in that case (CadCommitElevation), not at Z=0 when ELEV is set.
      // Detect explicit Z by counting commas in the raw input (PeelTypedElevation's hasZ).
      {
        int commas = 0;
        for (char c : line) if (c == ',') ++commas;
        bool hasExplicitZ = commas >= 2;
        if (!hasExplicitZ && CadWorkPlaneIsWorldXy(st))
          wz = CadCommitElevation(st);
      }
      SubmitInsertBlockPick(st, px, py, wz, log);
      return;
    }
    float px = 0.f;
    float py = 0.f;
    const bool rel = true;
    if (!ParseStoragePoint(st, line, &px, &py, rel, st.insertBlockX, st.insertBlockY)) {
      log.push_back("INSERT — type X,Y or pick in the viewport.");
      return;
    }
    SubmitInsertBlockPick(st, px, py, log);
    return;
  }

  if (st.active == K::SurfaceElevGrade) {
    using SEP = AppCommandState::SurfaceElevPhase;
    float px = 0.f;
    float py = 0.f;
    if (st.surfaceElevPhase == SEP::WaitFirst) {
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("SURFELEV — type X,Y (World) or pick a point in the drawing.");
        return;
      }
      ReportSurfaceElevationAt(st, px, py, log);
      st.surfaceElevPhase = SEP::WaitSecond;
      log.push_back("SURFELEV — second point for grade, or ESC to stop here.");
      return;
    }
    if (!ParseStoragePoint(st, line, &px, &py, true, static_cast<float>(st.surfaceElevFromX),
                           static_cast<float>(st.surfaceElevFromY))) {
      log.push_back("SURFELEV — type X,Y or @dx,dy from the first point.");
      return;
    }
    ReportSurfaceGradeTo(st, px, py, log);
    return;
  }

  if (st.active == K::WaterDrop) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("WATERDROP — type X,Y (World) or pick a point in the drawing.");
      return;
    }
    const int si = FindSurfaceIndex(st, st.waterDropSurfaceName);
    if (si < 0) {
      log.push_back("WATERDROP — no surface named \"" + st.waterDropSurfaceName + "\".");
      st.active = AppCommandState::Kind::None;
      return;
    }
    RunWaterDropAt(st, static_cast<size_t>(si), px, py, log);
    st.active = AppCommandState::Kind::None;
    return;
  }
  if (st.active == K::Catchment) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("CATCHMENT — type X,Y (World) or pick a point in the drawing.");
      return;
    }
    const int si = FindSurfaceIndex(st, st.catchmentSurfaceName);
    if (si < 0) {
      log.push_back("CATCHMENT — no surface named \"" + st.catchmentSurfaceName + "\".");
      st.active = AppCommandState::Kind::None;
      return;
    }
    RunCatchmentAt(st, static_cast<size_t>(si), px, py, log);
    st.active = AppCommandState::Kind::None;
    return;
  }
  if (st.active == K::SwapTinEdge) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("SURFSWAPEDGE — type X,Y (World) or pick a point in the drawing.");
      return;
    }
    const int si = FindSurfaceIndex(st, st.swapEdgeSurfaceName);
    if (si < 0) {
      log.push_back("SURFSWAPEDGE — no surface named \"" + st.swapEdgeSurfaceName + "\".");
      st.active = AppCommandState::Kind::None;
      return;
    }
    CommitSurfSwapEdgeLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(px),
                            static_cast<double>(py), log);
    st.active = AppCommandState::Kind::None;
    return;
  }
  if (st.active == K::AddTinPoint) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("SURFACEADDPOINT — type X,Y (World) or pick a point in the drawing.");
      return;
    }
    const int si = FindSurfaceIndex(st, st.addPointSurfaceName);
    if (si < 0) {
      log.push_back("SURFACEADDPOINT — no surface named \"" + st.addPointSurfaceName + "\".");
      st.active = AppCommandState::Kind::None;
      return;
    }
    CommitSurfAddPointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(px),
                            static_cast<double>(py), CadCommitElevation(st), log);
    st.active = AppCommandState::Kind::None;
    return;
  }
  if (st.active == K::DelTinPoint) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("SURFACEDELPOINT — type X,Y (World) or pick a point in the drawing.");
      return;
    }
    const int si = FindSurfaceIndex(st, st.delPointSurfaceName);
    if (si < 0) {
      log.push_back("SURFACEDELPOINT — no surface named \"" + st.delPointSurfaceName + "\".");
      st.active = AppCommandState::Kind::None;
      return;
    }
    CommitSurfDelPointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(px),
                            static_cast<double>(py), log);
    st.active = AppCommandState::Kind::None;
    return;
  }
  if (st.active == K::MoveTinPoint) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("SURFACEMOVEPOINT — type X,Y (World) or pick a point in the drawing.");
      return;
    }
    const int si = FindSurfaceIndex(st, st.movePointSurfaceName);
    if (si < 0) {
      log.push_back("SURFACEMOVEPOINT — no surface named \"" + st.movePointSurfaceName + "\".");
      st.active = AppCommandState::Kind::None;
      return;
    }
    using MP = AppCommandState::MoveTinPointPhase;
    if (st.moveTinPointPhase == MP::WaitFrom) {
      st.moveTinFromX = static_cast<double>(px);
      st.moveTinFromY = static_cast<double>(py);
      st.moveTinPointPhase = MP::WaitTo;
      log.push_back("SURFACEMOVEPOINT — pick the new position (Z = work plane).");
      return;
    }
    CommitSurfMovePointLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], st.moveTinFromX, st.moveTinFromY,
                             static_cast<double>(px), static_cast<double>(py), CadCommitElevation(st), log);
    st.active = AppCommandState::Kind::None;
    return;
  }
  if (st.active == K::DelTinLine) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("SURFDELLINE — type X,Y (World) or pick a point in the drawing.");
      return;
    }
    const int si = FindSurfaceIndex(st, st.delLineSurfaceName);
    if (si < 0) {
      log.push_back("SURFDELLINE — no surface named \"" + st.delLineSurfaceName + "\".");
      st.active = AppCommandState::Kind::None;
      return;
    }
    CommitSurfDelLineLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], static_cast<double>(px),
                           static_cast<double>(py), log);
    st.active = AppCommandState::Kind::None;
    return;
  }
  if (st.active == K::QuickProfile) {
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("QUICKPROFILE — type X,Y (World) or pick a point in the drawing.");
      return;
    }
    const int si = FindSurfaceIndex(st, st.quickProfileSurfaceName);
    if (si < 0) {
      log.push_back("QUICKPROFILE — no surface named \"" + st.quickProfileSurfaceName + "\".");
      st.active = AppCommandState::Kind::None;
      return;
    }
    using QP = AppCommandState::QuickProfilePhase;
    if (st.quickProfilePhase == QP::WaitFirst) {
      st.quickProfileFromX = static_cast<double>(px);
      st.quickProfileFromY = static_cast<double>(py);
      st.quickProfilePhase = QP::WaitSecond;
      log.push_back("QUICKPROFILE — second point of the sample line.");
      return;
    }
    CommitQuickProfileLocal(st, st.drawing.cadSurfaces[static_cast<size_t>(si)], st.quickProfileFromX, st.quickProfileFromY,
                            static_cast<double>(px), static_cast<double>(py), log);
    st.active = AppCommandState::Kind::None;
    return;
  }

  if (st.active == K::Align) {
    using AP = AppCommandState::AlignPhase;
    // Empty Enter is handled above; PickSelection accepts no typed coordinates.
    if (st.alignPhase == AP::PickSelection) {
      log.push_back("ALIGN — click objects or drag a selection window in the drawing, then press Enter.");
      return;
    }
    if (line.empty()) {
      ExecuteAlignCommand(st, log);
      return;
    }
    float px = 0.f, py = 0.f;
    if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
      log.push_back("ALIGN — type X,Y for the point, or press Enter to apply with current pairs.");
      return;
    }
    if (st.alignPhase == AP::PickSrc) {
      AppCommandState::AlignControlPt cp{};
      cp.srcX = px;
      cp.srcY = py;
      st.alignControlPts.push_back(cp);
      st.alignPhase = AP::PickDst;
      log.push_back("ALIGN — destination for pair " + std::to_string(st.alignControlPts.size()) +
                    " (pick or type real-world X,Y):");
    } else {
      st.alignControlPts.back().dstX = px;
      st.alignControlPts.back().dstY = py;
      st.alignPhase = AP::PickSrc;
      const size_t n = st.alignControlPts.size();
      log.push_back("ALIGN — pair " + std::to_string(n) + " added.  Pick next source, or Enter to apply (" +
                    std::to_string(n) + " pair" + (n == 1 ? "" : "s") + " ready).");
    }
    return;
  }

  if (st.active == AppCommandState::Kind::Move || st.active == AppCommandState::Kind::Copy) {
    const std::size_t mark = log.size();
    if (HandleModifyText(st, st.active == AppCommandState::Kind::Copy, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse MOVE/COPY input — use X,Y or @dx,dy from base.", log, mark);
    return;
  }

  if (st.active == AppCommandState::Kind::Stretch) {
    const std::size_t mark = log.size();
    if (HandleStretchText(st, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse STRETCH input — use X,Y or @dx,dy from base.", log, mark);
    return;
  }

  if (st.active == AppCommandState::Kind::Scale) {
    const std::size_t mark = log.size();
    if (HandleScaleText(st, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse SCALE input — see command hints (base X,Y; factor; R + reference/new length).", log, mark);
    return;
  }

  if (st.active == AppCommandState::Kind::Rotate) {
    const std::size_t mark = log.size();
    if (HandleRotateText(st, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse ROTATE input — see command hints.", log, mark);
    return;
  }

  if (st.active == AppCommandState::Kind::Mirror) {
    const std::size_t mark = log.size();
    if (HandleMirrorText(st, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse MIRROR input — see command hints.", log, mark);
    return;
  }

  if (st.active == AppCommandState::Kind::Array) {
    const std::size_t mark = log.size();
    if (HandleArrayText(st, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse ARRAY input — see command hints.", log, mark);
    return;
  }

  if (st.active == AppCommandState::Kind::Lengthen) {
    const std::size_t mark = log.size();
    if (HandleLengthenText(st, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse LENGTHEN input — see command hints.", log, mark);
    return;
  }

  if (st.active == AppCommandState::Kind::Grading) {
    ProcessGradingCommandLine(st, line, log);  // REQ-398
    return;
  }

  if (st.active == AppCommandState::Kind::Fillet) {
    const std::size_t mark = log.size();
    if (HandleFilletText(st, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse FILLET input — see command hints.", log, mark);
    return;
  }

  if (st.active == AppCommandState::Kind::Chamfer) {
    const std::size_t mark = log.size();
    if (HandleChamferText(st, line, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse CHAMFER input — see command hints.", log, mark);
    return;
  }

  if (st.active == K::FeatureLine) {
    // REQ-087. Same X,Y,Z entry as 3DPOLY — the peel below is shared — with one extra word: E marks
    // the next vertex an elevation point rather than a PI.
    const std::string lowFl = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));

    // TASK-082. A point is waiting for its elevation, so THIS line is that elevation and nothing
    // else — checked before CLOSE/END and before coordinate parsing, because "50" is a valid
    // elevation and would otherwise be read as a malformed point.
    if (st.featureLinePendingPoint) {
      const std::string zText = StringUtil::trimCopy(line);
      if (zText.empty()) {  // bare Enter accepts the default shown in the prompt
        CommitFeatureLinePendingPoint(st, st.featureLinePendingDefaultZ, log);
        return;
      }
      char* zEnd = nullptr;
      const double zv = std::strtod(zText.c_str(), &zEnd);
      if (!zEnd || *zEnd != '\0' || !std::isfinite(zv)) {
        // Stay in the prompt. Dropping back to point entry would silently discard the point the
        // user just clicked, and they would have no way to know which of the two it lost.
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "FEATURELINE — elevation must be a number. Type one, or Enter for <%.3f>:",
                      static_cast<double>(st.featureLinePendingDefaultZ));
        log.push_back(buf);
        return;
      }
      CommitFeatureLinePendingPoint(st, static_cast<float>(zv), log);
      return;
    }

    if (lowFl == "close" || lowFl == "cl") {
      CommitFeatureLineDraft(st, true, log);
      return;
    }
    if (lowFl == "end") {
      CommitFeatureLineDraft(st, false, log);
      return;
    }
    bool nextIsElevPoint = false;
    std::string flText = StringUtil::trimCopy(line);
    if (!flText.empty() && (flText[0] == 'e' || flText[0] == 'E') &&
        (flText.size() == 1 || flText[1] == ' ' || flText[1] == '\t')) {
      nextIsElevPoint = true;
      flText = StringUtil::trimCopy(flText.substr(1));
      if (flText.empty()) {
        // TASK-082: bare E ARMS the flag rather than erroring. Before a click could supply X,Y, the
        // only way to place a point was to type it, so E had to be followed by coordinates on the
        // same line; now the coordinates arrive from the mouse and the flag has to wait for them.
        st.featureLineNextIsElevPoint = true;
        log.push_back("FEATURELINE — next point will be an elevation point. Click it, or type X,Y "
                      "/ X,Y,Z.");
        return;
      }
    }
    if (st.featureLineNextIsElevPoint)
      nextIsElevPoint = true;  // armed by an earlier bare E
    // Peel a typed elevation, exactly as 3DPOLY does, and for the same reason: the shared 2D parser
    // is REQ-101-critical and must not learn about a third component.
    {
      const size_t c1 = flText.find(',');
      const size_t c2 = (c1 == std::string::npos) ? std::string::npos : flText.find(',', c1 + 1);
      if (c2 != std::string::npos) {
        if (flText.find(',', c2 + 1) != std::string::npos) {
          log.push_back("FEATURELINE — too many coordinates: X,Y,Z or @dx,dy,dz.");
          return;
        }
        const std::string zText = StringUtil::trimCopy(flText.substr(c2 + 1));
        char* zEnd = nullptr;
        const double zv = std::strtod(zText.c_str(), &zEnd);
        if (zText.empty() || !zEnd || *zEnd != '\0' || !std::isfinite(zv)) {
          log.push_back("FEATURELINE — elevation must be a number: X,Y,Z or @dx,dy,dz.");
          return;
        }
        st.polylineTypedZ = static_cast<float>(zv);
        st.polylineTypedZRelative = !flText.empty() && flText[0] == '@';
        st.polylineTypedZValid = true;
        flText = flText.substr(0, c2);
      }
    }
    float fx = 0.f, fy = 0.f;
    const bool flRel = !st.featureLineDraftVerts.empty();
    if (ParseStoragePoint(st, flText, &fx, &fy, flRel, st.anchorX, st.anchorY)) {
      // TASK-082 Q1: the rule is "a point that arrives WITHOUT an elevation prompts for one", not
      // "clicks prompt". A typed X,Y,Z already carries its answer, so asking again would be noise —
      // and it is the form every existing transcript uses. A typed X,Y takes the same path a click
      // does.
      if (st.polylineTypedZValid) {
        st.featureLineNextIsElevPoint = false;  // consumed by this vertex
        SubmitFeatureLineVertex(st, fx, fy, nextIsElevPoint, log);
      } else {
        st.featureLineNextIsElevPoint = nextIsElevPoint;  // survives until the elevation arrives
        SubmitFeatureLinePoint(st, fx, fy, log);
      }
      return;
    }
    st.polylineTypedZValid = false;  // the point failed to parse; do not carry the Z to the next try
    log.push_back("FEATURELINE — click a point, or type X,Y / X,Y,Z (or @dx,dy,dz), E for an "
                  "elevation point, CLOSE, END, or ESC.");
    return;
  }

  if (st.active == K::Polyline) {
    using PP = AppCommandState::PolylinePhase;
    const std::string low = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(line));

    // REQ-085: peel a trailing `,z` off `x,y,z` so the shared 2D parser never sees it. That parser
    // is REQ-101-critical and used by every command; widening it to three components to serve one
    // command would put every other command's coordinate handling at risk for no gain. 3DPOLY only —
    // a plain POLYLINE keeps refusing a third component rather than silently ignoring it.
    std::string pointText = line;
    if (st.polylineDraft3d) {
      const std::string trimmed = StringUtil::trimCopy(line);
      const size_t c1 = trimmed.find(',');
      const size_t c2 = (c1 == std::string::npos) ? std::string::npos : trimmed.find(',', c1 + 1);
      if (c2 != std::string::npos) {
        // Strict about the field count, because the shared 2D parser is NOT: ParseTwoDoubles reads
        // two numbers and ignores whatever follows, so `1,2,3,4` would otherwise land silently at
        // (1,2). Refusing here keeps that quiet-drop out of the one command where a third field is
        // meaningful. (The same silent drop still applies to LINE/POLYLINE/RECT — pre-existing, and
        // reported rather than changed under this task.)
        if (trimmed.find(',', c2 + 1) != std::string::npos) {
          log.push_back("3DPOLY — too many coordinates: X,Y,Z or @dx,dy,dz.");
          return;
        }
        const std::string zText = StringUtil::trimCopy(trimmed.substr(c2 + 1));
        char* zEnd = nullptr;
        const double zv = std::strtod(zText.c_str(), &zEnd);
        if (!zText.empty() && zEnd && *zEnd == '\0' && std::isfinite(zv)) {
          st.polylineTypedZ = static_cast<float>(zv);
          st.polylineTypedZRelative = !trimmed.empty() && trimmed[0] == '@';
          st.polylineTypedZValid = true;
          pointText = trimmed.substr(0, c2);  // `x,y`, with any leading `@` still attached
        } else {
          log.push_back("3DPOLY — elevation must be a number: X,Y,Z or @dx,dy,dz.");
          return;
        }
      }
    }
    // REQ-316 / ADR-047 / D-2026-09-02-f: arc-mode keywords. Full words — `A`/`ANGLE` are the
    // segment-bearing lock. 3DPOLY stays line-only.
    if (!st.polylineDraft3d) {
      if (low == "arc") {
        st.polylineArcMode = true;
        log.push_back("POLYLINE — arc mode. Next segment is a circular arc (LINE to switch back).");
        return;
      }
      if (low == "line") {
        st.polylineArcMode = false;
        st.polylineArcRadius = 0.f;
        st.polylineArcAngleValid = false;
        log.push_back("POLYLINE — line mode.");
        return;
      }
      if (st.polylineArcMode && (low.rfind("radius", 0) == 0 || low.rfind("rad ", 0) == 0 ||
                                 (low.size() > 1 && low[0] == 'r' && (std::isdigit((unsigned char)low[1]) ||
                                                                      low[1] == '-' || low[1] == '.')))) {
        const size_t sp = low.find_first_of(" 0123456789-.");
        float rv = 0.f;
        if (sp != std::string::npos && ParseSingleFloatToken(StringUtil::trimCopy(line.substr(sp)), &rv) && rv > 1e-9f) {
          st.polylineArcRadius = rv;
          st.polylineArcAngleValid = false;
          log.push_back("POLYLINE arc — radius set; pick the arc end point.");
        } else {
          log.push_back("POLYLINE arc — RADIUS <value>.");
        }
        return;
      }
      if (st.polylineArcMode && (low.rfind("cangle", 0) == 0 || low.rfind("ca ", 0) == 0)) {
        const size_t sp = low.find_first_of(" 0123456789-.");
        float av = 0.f;
        if (sp != std::string::npos && ParseSingleFloatToken(StringUtil::trimCopy(line.substr(sp)), &av) &&
            std::fabs(av) > 1e-6f) {
          st.polylineArcAngleDeg = av;
          st.polylineArcAngleValid = true;
          st.polylineArcRadius = 0.f;
          log.push_back("POLYLINE arc — included angle set; pick the arc end point.");
        } else {
          log.push_back("POLYLINE arc — CANGLE <degrees>.");
        }
        return;
      }
    }
    if (low == "undo" || low == "u") {
      // REQ-316: drop the most recently added vertex and its leaving-segment bulge.
      if (st.polylinePhase != PP::NeedNextPoint || st.polylineDraftVerts.size() < 6) {
        log.push_back("POLYLINE — nothing to undo.");
        return;
      }
      st.polylineDraftVerts.resize(st.polylineDraftVerts.size() - 3);
      if (!st.polylineDraftBulge.empty())
        st.polylineDraftBulge.pop_back();
      if (!st.polylineDraftBulge.empty())
        st.polylineDraftBulge.back() = 0.f;  // the segment that had led to the removed vertex
      if (st.polyDraftSegments > 0)
        --st.polyDraftSegments;
      const size_t n = st.polylineDraftVerts.size();
      st.anchorX = st.polylineDraftVerts[n - 3];
      st.anchorY = st.polylineDraftVerts[n - 2];
      st.anchorZ = st.polylineDraftVerts[n - 1];
      log.push_back("POLYLINE — last vertex removed.");
      return;
    }
    if (low == "close" || low == "cl") {
      CancelSegmentAnglePick(st, nullptr);
      if (st.polylinePhase != PP::NeedNextPoint || st.polyDraftSegments == 0) {
        log.push_back("POLYLINE CLOSE — need at least one segment after the start point.");
      } else {
        if (st.polylineArcMode && !st.polylineDraftBulge.empty())
          st.polylineDraftBulge.back() = CadPolylineDraftBulgeForNextPoint(st, st.polyFirstX, st.polyFirstY);
        CommitPolylineDraft(st, true, log);
      }
      return;
    }
    if (low == "end") {
      CancelSegmentAnglePick(st, nullptr);
      if (st.polylinePhase != PP::NeedNextPoint || st.polyDraftSegments == 0)
        log.push_back("POLYLINE END — need at least one segment after the start point.");
      else
        CommitPolylineDraft(st, false, log);
      return;
    }

    float px = 0.f;
    float py = 0.f;
    const bool allowRel = st.polylinePhase == PP::NeedNextPoint;

    if (allowRel && TryParseSegmentAngleLockCommand(st, line, log)) {
      return;
    }

    // `pointText` is `line` with any 3DPOLY elevation already peeled off; identical to `line` for a
    // plain POLYLINE.
    if (ParseStoragePoint(st, pointText, &px, &py, allowRel, st.anchorX, st.anchorY, &st.anchorZ)) {
      SubmitPolylineVertex(st, px, py, log);
      return;
    }

    if (allowRel && st.segmentAngleLockActive) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        if (std::fabs(dist) < 1e-20f)
          log.push_back("POLYLINE — distance must be non-zero.");
        else
          SubmitPolylineVertex(st, st.anchorX + st.segmentLockUx * dist, st.anchorY + st.segmentLockUy * dist, log);
        return;
      }
    }

    if (allowRel && st.orthoMode) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        // Under a UCS, the direction AND the elevation both come from the same UCS-ortho decision
        // the mouse-drag path uses (issue #371 5th follow-up) — OrthoUnitTowardUiCursorFromAnchor is
        // a flat X/Y computation that has no notion of the UCS's own axes or of Z.
        if (CadUcsIsWorld(st)) {
          float ux = 0.f;
          float uy = 0.f;
          if (!OrthoUnitTowardUiCursorFromAnchor(st, &ux, &uy))
            log.push_back(
                "Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
          else
            SubmitPolylineVertex(st, st.anchorX + ux * dist, st.anchorY + uy * dist, log);
        } else {
          float px = 0.f, py = 0.f;
          if (!OrthoUcsDirectDistancePoint(st, dist, &px, &py))
            log.push_back(
                "Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
          else
            SubmitPolylineVertex(st, px, py, log);
        }
        return;
      }
    }

    log.push_back(
        "POLYLINE — X,Y / @dx,dy / A or 2P bearing / ARC / LINE / UNDO / CLOSE / END / ortho distance.");
    return;
  }

  if (st.active == K::PadSolid) {
    HandlePadSolidText(st, line, log);
    return;
  }

  if (st.active == K::Ellipse && st.ellPhase == AppCommandState::EllipsePhase::WaitRatio) {
    const std::string tr = StringUtil::trimCopy(line);
    float ratio = 0.5f;
    if (!tr.empty() && !ParseSingleFloatToken(tr, &ratio)) {
      log.push_back("ELLIPSE — enter one number for minor/major ratio (0-1], or blank for 0.5.");
      return;
    }
    FinishEllipseFromRatio(st, ratio, log);
    return;
  }

  if (st.active == K::Text) {
    using TP = AppCommandState::TextCmdPhase;
    if (st.textPhase == TP::WaitInsertion) {
      float px = 0.f;
      float py = 0.f;
      if (!ParseStoragePoint(st, line, &px, &py, false, 0.f, 0.f)) {
        log.push_back("TEXT — type insertion X,Y or click in viewport.");
        return;
      }
      st.textInsX = px;
      st.textInsY = py;
      st.textPhase = TP::WaitHeight;
      log.push_back("TEXT height — Enter for plot-scale default:");
      return;
    }
    if (st.textPhase == TP::WaitHeight) {
      // In paper space the height is in plotted (paper) inches directly; in model space it is world units.
      const bool paperText = ActivePaperGeometryTarget(st) != nullptr;
      const std::string tr = StringUtil::trimCopy(line);
      if (tr.empty())
        st.textHeightDraft = paperText ? st.drawing.defaultPlottedTextHeightInches : DefaultAnnotationTextHeightWorld(st);
      else if (!ParseSingleFloatToken(tr, &st.textHeightDraft) || !std::isfinite(st.textHeightDraft) ||
               st.textHeightDraft <= 0.f) {
        log.push_back("TEXT — invalid height.");
        return;
      }
      st.textPhase = TP::WaitRotation;
      log.push_back("TEXT rotation ° clockwise from north (decimal/DMS) — Enter for 90 (horizontal):");
      return;
    }
    if (st.textPhase == TP::WaitRotation) {
      const std::string tr = StringUtil::trimCopy(line);
      // Bearing convention (clockwise from north): 0 = north (text runs up), 90 = east (reads
      // left-to-right). Enter defaults to 90 (horizontal), matching AutoCAD's default text orientation.
      float deg = 90.f;
      if (!tr.empty() && (!ParseAngleDegrees(tr, &deg) || !std::isfinite(deg))) {
        log.push_back("TEXT — could not parse angle.");
        return;
      }
      st.textRotDraft = MathAngleRadFromBearingCwNorthDeg(deg);
      st.textPhase = TP::WaitString;
      log.push_back("TEXT — enter content:");
      return;
    }
    if (st.textPhase == TP::WaitString) {
      CadAnnotation ann;
      ann.kind = CadAnnotation::Kind::Text;
      ann.insX = st.textInsX;
      ann.insY = st.textInsY;
      ann.rotationRad = st.textRotDraft;
      ann.text = line;
      if (!ann.text.empty()) {
        const bool paperTarget = ActivePaperGeometryTarget(st) != nullptr;
        // Paper-space TEXT (REQ-037): textHeightDraft is already in paper (plotted) inches — store it
        // directly. Model-space TEXT divides by the plot scale to reach plotted inches.
        const float plottedHeightInches =
            paperTarget ? st.textHeightDraft
                        : st.textHeightDraft / std::max(st.drawing.modelUnitsPerPlottedInch, 1.e-6f);
        const float insZ = paperTarget ? 0.f : CadCommitElevation(st);
        ann.plottedHeightInches = plottedHeightInches;
        // REQ-204 finite-coords / REQ-201: a non-finite insertion, elevation, rotation, or height —
        // and any height that drives the annotation's world extent past FLT_MAX — must be refused and
        // reported, never stored. It must never reach SAVEAS or the load-time origin rebase, which
        // reads CadAnnotationRoughBounds for every annotation: one inf corner there rebases the whole
        // drawing's origin to inf and every coordinate in the file follows. The RESULT is what is
        // checked, not the typed input — a finite but huge height (e.g. 1e38) passes std::isfinite on
        // the way in and overflows to inf only once scaled by the text length. Issue #117; same shape
        // as the CIRCLE infinite-radius guard (REQ-204, issue #59).
        float rbMnX = 0.f, rbMnY = 0.f, rbMxX = 0.f, rbMxY = 0.f;
        CadAnnotationRoughBounds(ann, st.drawing.modelUnitsPerPlottedInch, &rbMnX, &rbMnY, &rbMxX, &rbMxY);
        if (!std::isfinite(ann.insX) || !std::isfinite(ann.insY) || !std::isfinite(insZ) ||
            !std::isfinite(ann.rotationRad) || !std::isfinite(plottedHeightInches) ||
            plottedHeightInches <= 0.f || !std::isfinite(rbMnX) || !std::isfinite(rbMnY) ||
            !std::isfinite(rbMxX) || !std::isfinite(rbMxY)) {
          log.push_back(
              "TEXT rejected - the insertion point, elevation, rotation, or height is not a finite number.");
          st.active = K::None;
          ResetTextCmdDraft(st);
          return;
        }
        PushUndoSnapshot(st, "Text");
        if (PaperLayout* L = ActivePaperGeometryTarget(st)) {
          L->paperTexts.push_back(std::move(ann));
          L->paperTextAttrs.push_back(MakeNewEntityAttrs(st));
        } else {
          ann.insZ = insZ;  // model TEXT: snap overrides ELEV (REQ-058)
          StampActiveTextStyleOnNewText(st, ann);  // REQ-044: new TEXT adopts the active text style
          st.drawing.cadAnnotations.push_back(std::move(ann));
          st.drawing.cadAnnotationAttrs.push_back(MakeNewEntityAttrs(st));
        }
        // Issue #56. Not a rendering concern — this is what makes the new entity get an id at all.
        // MakeNewEntityAttrs leaves id 0 and relies on the EnsureEntityIds sweep, and that sweep
        // early-outs on `entityIdSweepRevision == cadGpuRevision` with the reasoning "geometry has
        // not changed since the last sweep, so nothing can be missing an id". Committing an entity
        // without bumping the revision makes that premise false, so the sweep skipped this
        // annotation and it was saved to `internal document payload` with id 0 (REQ-076), which also broke REQ-079 resave
        // idempotence. Every other commit path bumps here; TEXT was the one that did not.
        BumpCadGpuCache(st);
        log.push_back("TEXT placed.");
      } else
        log.push_back("TEXT — empty; canceled.");
      st.active = K::None;
      ResetTextCmdDraft(st);
      return;
    }
  }

  if (st.active == AppCommandState::Kind::Line) {
    float px = 0.f;
    float py = 0.f;
    const bool allowRel = st.linePhase == AppCommandState::LinePhase::NeedNextPoint;

    if (allowRel && TryParseSegmentAngleLockCommand(st, line, log)) {
      return;
    }

    if (ParseStoragePoint(st, line, &px, &py, allowRel, st.anchorX, st.anchorY, &st.anchorZ)) {
      SubmitLineVertex(st, px, py, log);
      return;
    }
    if (!st.alignmentStationSnapRejectReason.empty()) {
      log.push_back(st.alignmentStationSnapRejectReason);
      st.alignmentStationSnapRejectReason.clear();
      return;
    }

    if (allowRel && st.segmentAngleLockActive) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        if (std::fabs(dist) < 1e-20f)
          log.push_back("LINE — distance must be non-zero.");
        else {
          px = st.anchorX + st.segmentLockUx * dist;
          py = st.anchorY + st.segmentLockUy * dist;
          SubmitLineVertex(st, px, py, log);
        }
        return;
      }
    }

    if (allowRel && st.orthoMode) {
      float dist = 0.f;
      if (ParseSingleFloatToken(line, &dist)) {
        // Under a UCS, the direction AND the elevation both come from the same UCS-ortho decision
        // the mouse-drag path uses (issue #371 5th follow-up) — OrthoUnitTowardUiCursorFromAnchor is
        // a flat X/Y computation that has no notion of the UCS's own axes or of Z.
        if (CadUcsIsWorld(st)) {
          float ux = 0.f;
          float uy = 0.f;
          if (!OrthoUnitTowardUiCursorFromAnchor(st, &ux, &uy))
            log.push_back(
                "Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
          else {
            px = st.anchorX + ux * dist;
            py = st.anchorY + uy * dist;
            SubmitLineVertex(st, px, py, log);
          }
        } else {
          if (!OrthoUcsDirectDistancePoint(st, dist, &px, &py))
            log.push_back(
                "Ortho distance needs cursor direction — move crosshair away from anchor, then enter distance.");
          else
            SubmitLineVertex(st, px, py, log);
        }
        return;
      }
    }

    // No handler ran for this one - the parsing above is inline - so nothing can have spoken for
    // this input, and `log.size()` is the honest mark. The fallback below is the only message.
    ReportUnparsedCommandInput(
        st, line,
        std::string("Could not parse point. Use X,Y or X Y") +
            (allowRel ? "; @dx,dy; A / 2P (two picks); A 45 +90; ortho distance toward cursor."
                      : "."),
        log, log.size());
    return;
  }

  if (st.active == K::Rect) {
    using RectP = AppCommandState::RectPhase;
    const bool second = st.rectPhase == RectP::WaitSecondCorner;
    float px = 0.f;
    float py = 0.f;
    if (ParseStoragePoint(st, line, &px, &py, second, st.rectX1, st.rectY1)) {
      if (second) {
        CommitRectangle(st, st.rectX1, st.rectY1, px, py, log);
      } else {
        st.rectX1 = px;
        st.rectY1 = py;
        st.anchorX = px;
        st.anchorY = py;
        st.rectPhase = RectP::WaitSecondCorner;
        log.push_back("RECT — pick the opposite corner (or type X,Y / @dx,dy):");
      }
      return;
    }
    log.push_back(std::string("RECT — could not parse point. Use X,Y") +
                  (second ? " or @dx,dy for an exact width x height." : "."));
    return;
  }

  if (st.active == AppCommandState::Kind::Circle) {
    const std::size_t mark = log.size();
    if (HandleCircleTextInput(line, st, log)) {
      return;
    }
    ReportUnparsedCommandInput(st, line, "Could not parse input for current CIRCLE step — see hint below.", log, mark);
    return;
  }

  // The prompted solid primitives (REQ-313 as amended). Placed with CIRCLE's branch because it is
  // the same shape of command: a point, then a value that also answers to a keyword.
  if (st.active == AppCommandState::Kind::Solid) {
    if (HandleSolidTextInput(line, st, log))
      return;
    log.push_back(CadSolidPromptText(st));
    return;
  }

  if (st.active == AppCommandState::Kind::Extrude) {
    if (HandleExtrudeTextInput(line, st, log))
      return;
    log.push_back(CadExtrudePromptText(st));
    return;
  }

  if (st.active == AppCommandState::Kind::PressPull) {
    if (HandlePressPullTextInput(line, st, log))
      return;
    log.push_back(CadPressPullPromptText(st));
    return;
  }

  if (st.active == AppCommandState::Kind::Loft) {
    if (HandleLoftTextInput(line, st, log))
      return;
    log.push_back(CadLoftPromptText(st));
    return;
  }

  if (IsGizmoCommandKind(st.active)) {
    (void)HandleGizmoCommandTextInput(line, st, log);
    return;
  }

  if (IsPropCommandKind(st.active)) {  // REQ-356
    (void)HandlePropCommandTextInput(line, st, log);
    return;
  }

  if (st.active == AppCommandState::Kind::Sweep) {
    if (HandleSweepTextInput(line, st, log))
      return;
    log.push_back(CadSweepPromptText(st));
    return;
  }

  if (st.active == AppCommandState::Kind::Revolve) {
    if (HandleRevolveTextInput(line, st, log))
      return;
    log.push_back(CadRevolvePromptText(st));
    return;
  }

  if (st.active == AppCommandState::Kind::Slice) {
    if (HandleSliceTextInput(line, st, log))
      return;
    log.push_back(CadSlicePromptText(st));
    return;
  }

  if (st.active == AppCommandState::Kind::Section) {
    if (HandleSectionTextInput(line, st, log))
      return;
    log.push_back(CadSectionPromptText(st));
    return;
  }

  if (st.active == AppCommandState::Kind::SectionPlane) {
    if (HandleSectionPlaneTextInput(line, st, log))
      return;
    log.push_back(CadSectionPlanePromptText(st));
    return;
  }

  if (st.active == AppCommandState::Kind::Boolean) {
    if (HandleBooleanTextInput(line, st, log))
      return;
    log.push_back(CadBooleanPromptText(st));
    return;
  }

  // REQ-317 POLYSOLID, beside it for the same reason: a point, then letters that set the wall.
  if (st.active == AppCommandState::Kind::Polysolid) {
    if (HandlePolysolidTextInput(line, st, log))
      return;
    log.push_back(CadPolysolidPromptText(st));
    return;
  }

  // REQ-444 REVCLOUD, beside it for the same reason: a corner, or a letter that sets an option.
  if (st.active == AppCommandState::Kind::Revcloud) {
    if (HandleRevcloudTextInput(line, st, log))
      return;
    log.push_back(CadRevcloudPromptText(st));
    return;
  }

  // PIPERUN (issue #486 increment B2 / REQ-345), the same self-contained shape as POLYSOLID above:
  // a nominal-size/class line, then points.
  if (st.active == AppCommandState::Kind::PipeRun) {
    if (HandlePipeRunTextInput(line, st, log))
      return;
    log.push_back(CadPipeRunPromptText(st));
    return;
  }

  // PIPEFIT (issue #486 increment B7 / REQ-345): a single typed X,Y,Z station point, same shape
  // as PIPERUN's own coordinate entry.
  if (st.active == AppCommandState::Kind::PipeFit) {
    if (HandlePipeFitTextInput(line, st, log))
      return;
    log.push_back("PIPEFIT - pick a point on the run, or type X,Y,Z. ESC to cancel.");
    return;
  }

  // PIPESPLIT (issue #486 increment B8 / REQ-345): a single typed X,Y,Z station point.
  if (st.active == AppCommandState::Kind::PipeSplit) {
    if (HandlePipeSplitTextInput(line, st, log))
      return;
    log.push_back("PIPESPLIT - pick a point on the run, or type X,Y,Z. ESC to cancel.");
    return;
  }

  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    using DP = AppCommandState::DimPhase;
    const bool linear = st.active == K::DimLinear;
    const std::string dimTrim = StringUtil::trimCopy(line);
    const std::string dimLow = StringUtil::toLowerAsciiCopy(dimTrim);
    if (linear && st.dimPhase == DP::WaitDimLinePt && (dimLow == "h" || dimLow == "v")) {
      CadDimLinearApplyHVHotkey(st, dimLow == "v", log);
      return;
    }
    float px = 0.f;
    float py = 0.f;
    bool allowRel = false;
    float ax = 0.f;
    float ay = 0.f;
    if (st.dimPhase == DP::WaitExt2) {
      allowRel = true;
      ax = st.dimE1x;
      ay = st.dimE1y;
    } else if (st.dimPhase == DP::WaitDimLinePt) {
      allowRel = true;
      ax = 0.5f * (st.dimE1x + st.dimE2x);
      ay = 0.5f * (st.dimE1y + st.dimE2y);
    }
    if (!ParseStoragePoint(st, dimTrim, &px, &py, allowRel, ax, ay)) {
      log.push_back(linear ? "DIMLINEAR — X,Y or @dx,dy from chord mid; H / V locks orientation."
                           : "DIMALIGNED — X,Y or @dx,dy from chord mid.");
      return;
    }
    SubmitViewportPick(st, px, py, log, false, false);
    return;
  }

  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    const std::string dimTrim = StringUtil::trimCopy(line);
    float px = 0.f;
    float py = 0.f;
    bool allowRel = false;
    float ax = 0.f;
    float ay = 0.f;
    if (st.dimAngularPhase != DAP::WaitVertex) {
      allowRel = true;
      ax = st.dimAngVx;
      ay = st.dimAngVy;
    }
    if (!ParseStoragePoint(st, dimTrim, &px, &py, allowRel, ax, ay)) {
      log.push_back("DIMANGULAR — X,Y or @dx,dy from vertex.");
      return;
    }
    SubmitViewportPick(st, px, py, log, false, false);
    return;
  }

  std::string low = StringUtil::toLowerAsciiCopy(line);
  // Through the same matcher the failure path uses (issue #233), so the two cannot disagree about
  // which words are command names — which is the whole basis of that message.
  if (const CmdEntry* e = FindRegistryEntry(low)) {
    DispatchByPrimary(StringUtil::toLowerAsciiCopy(e->primary), st, log);
    return;
  }

  auto fuzzy = FuzzyCommandMatches(line, 6);
  if (!fuzzy.empty()) {
    std::string hint = "Unknown command. Did you mean:";
    for (const auto& w : fuzzy)
      hint += " " + w + ",";
    if (!hint.empty() && hint.back() == ',')
      hint.pop_back();
    hint += "?";
    log.push_back(hint);
  } else
    log.push_back("Unknown command. Type HELP.");

  }();
  cmdBuf[0] = '\0';
}

std::vector<std::string> FuzzyCommandMatches(const std::string& query, int maxResults) {
  std::vector<std::string> names;
  for (const CmdEntry& e : kRegistry)
    names.push_back(e.primary);

  struct Scored {
    std::string name;
    int score;
  };
  std::vector<Scored> ranked;
  std::string qlow = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(query));
  if (qlow.empty())
    return {};

  for (const auto& n : names) {
    int sc = FuzzySubsequenceScore(qlow, n);
    if (sc >= 0)
      ranked.push_back({n, sc});
  }
  std::sort(ranked.begin(), ranked.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score)
      return a.score > b.score;
    return a.name < b.name;
  });

  std::vector<std::string> out;
  for (size_t i = 0; i < ranked.size() && static_cast<int>(out.size()) < maxResults; ++i)
    out.push_back(ranked[i].name);
  return out;
}

std::vector<CommandSuggestion> FuzzyCommandSuggestions(const std::string& query, int maxResults) {
  struct Scored {
    const CmdEntry* entry;
    int score;
    bool exactMatch;
  };
  std::vector<Scored> ranked;
  const std::string qlow = StringUtil::toLowerAsciiCopy(StringUtil::trimCopy(query));
  if (qlow.empty())
    return {};

  // Preserve exact and prefix ranking, then use the same subsequence score as the
  // command-match helper so a small typo can still find the intended command.
  const auto isPrefix = [](const std::string& q, const std::string& s) {
    return s.size() >= q.size() && StringUtil::toLowerAsciiCopy(s).compare(0, q.size(), q) == 0;
  };
  for (const CmdEntry& e : kRegistry) {
    int best = -1;
    const std::string nameLow = StringUtil::toLowerAsciiCopy(e.primary);
    bool exactMatch = nameLow == qlow;
    if (exactMatch)
      best = 5000000;
    else if (isPrefix(qlow, e.primary))
      best = 3000000 + FuzzySubsequenceScore(qlow, nameLow);
    else
      best = 1000000 + FuzzySubsequenceScore(qlow, nameLow);
    if (e.aliases[0] != '\0') {
      std::istringstream als(std::string(e.aliases));
      std::string a;
      while (std::getline(als, a, ',')) {
        a = StringUtil::trimCopy(a);
        if (a.empty())
          continue;
        const std::string aliasLow = StringUtil::toLowerAsciiCopy(a);
        if (aliasLow == qlow) {
          exactMatch = true;
          best = std::max(best, 4000000);
        } else if (isPrefix(qlow, a))
          best = std::max(best, 2000000 + FuzzySubsequenceScore(qlow, aliasLow));
      }
    }
    if (best >= 1000000)
      ranked.push_back({&e, best, exactMatch});
  }
  std::sort(ranked.begin(), ranked.end(), [](const Scored& a, const Scored& b) {
    if (a.score != b.score)
      return a.score > b.score;  // stronger matches (exact name/alias) first
    return std::string(a.entry->primary) < std::string(b.entry->primary);  // then alphabetical
  });

  std::vector<CommandSuggestion> out;
  for (size_t i = 0; i < ranked.size() && static_cast<int>(out.size()) < maxResults; ++i) {
    CommandSuggestion s;
    s.name = ranked[i].entry->primary;
    for (char& ch : s.name)
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));  // NAME in caps (nanoCAD style)
    s.description = ranked[i].entry->description ? ranked[i].entry->description : "";
    s.exactMatch = ranked[i].exactMatch;
    out.push_back(std::move(s));
  }
  return out;
}

std::string ResolveCommandLineSubmission(const std::string& typedText) {
  const std::string trimmed = StringUtil::trimCopy(typedText);
  if (trimmed.empty() || trimmed.find_first_of(" \t") != std::string::npos)
    return typedText;
  const auto top = FuzzyCommandSuggestions(trimmed, 1);
  if (top.empty())
    return typedText;
  std::string resolved = top[0].name;
  for (char& ch : resolved)
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return resolved;
}

const char* CircleCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Circle)
    return "";
  switch (st.circlePhase) {
  case AppCommandState::CirclePhase::WaitCenterOrMode:
    return "CIRCLE: Click or type center | Type [3P] for three-point circle | ESC cancel";
  case AppCommandState::CirclePhase::WaitRadius:
    return "CIRCLE: Click edge for radius | Type radius | D <value> or D<value> for diameter | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP1:
    return "CIRCLE (3P): Point 1 of 3 — click or X,Y | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP2:
    return "CIRCLE (3P): Point 2 of 3 — click or X,Y | ESC cancel";
  case AppCommandState::CirclePhase::ThreeP_WaitP3:
    return "CIRCLE (3P): Point 3 of 3 — click or X,Y | ESC cancel";
  }
  return "";
}

const char* ModifyCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  if (st.active == K::Paste && st.modifyPhase == MP::NeedDestination)
    return "PASTE: Click destination point | ESC cancel";
  // REQ-121: MIRROR and ARRAY had NO footer hint at all, so their selection step showed nothing in
  // the dynamic cursor text while showing a prompt on the command line — REQ-304's rule is that the
  // two agree, and this pair was missed by that audit. Handled here rather than in two new
  // delegates, on REQ-304's own precedent (it closed its eight gaps by extending
  // `DrawingExtrasFooterHint` rather than adding functions). They are modify commands; this is the
  // modify hint.
  if (st.active == K::Mirror)
    return st.mirrorPhase == AppCommandState::MirrorPhase::PickSelection ? kSelectObjectsPrompt : "";
  if (st.active == K::Array)
    return st.arrayPhase == AppCommandState::ArrayPhase::PickSelection ? kSelectObjectsPrompt : "";
  if (st.active != K::Move && st.active != K::Copy)
    return "";
  if (st.modifyPhase == MP::PickSelection)
    return kSelectObjectsPrompt;  // REQ-121
  if (st.modifyPhase == MP::NeedBase)
    return "MOVE/COPY: Base point — click or X,Y | ESC cancel";
  if (st.modifyPhase == MP::NeedDestination)
    return "MOVE/COPY: Second point — click or X,Y or @dx,dy from base | ESC cancel";
  return "";
}

const char* RotateCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using RP = AppCommandState::RotatePhase;
  if (st.active != K::Rotate)
    return "";
  switch (st.rotatePhase) {
  case RP::PickSelection:
    return kSelectObjectsPrompt;  // REQ-121
  case RP::NeedBase:
    return "ROTATE: Base point — click or X,Y | ESC cancel";
  case RP::NeedAngleOrReference:
    return "ROTATE: ° clockwise / DMS | [R]eference | [C]opy | ESC (north=0° CW)";
  case RP::Ref_WaitP1:
    return "ROTATE ref: First point | [C] toggles copy | ESC cancel";
  case RP::Ref_WaitP2:
    return "ROTATE ref: Second point | [C] toggles copy | ESC cancel";
  case RP::AfterReference_WaitAngleOrP:
    return "ROTATE ref: New bearing ° from north (like props) | [P] two pts | [C]opy | ESC";
  case RP::AnglePoints_WaitP1:
    return "ROTATE angle pts: First point | [C]opy | ESC cancel";
  case RP::AnglePoints_WaitP2:
    return "ROTATE angle pts: Second point | [C]opy | ESC cancel";
  }
  return "";
}

const char* ScaleCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using SP = AppCommandState::ScalePhase;
  if (st.active != K::Scale)
    return "";
  if (st.modifyPhase == MP::PickSelection)
    return kSelectObjectsPrompt;  // REQ-121
  if (st.modifyPhase == MP::NeedBase)
    return "SCALE: Base point — click or X,Y | ESC cancel";
  if (st.modifyPhase == MP::NeedDestination) {
    switch (st.scalePhase) {
    case SP::FactorPick:
      return "SCALE: Second point or type factor (>0) — dist/base-ref | [R]eference = two-point ref length | ESC";
    case SP::Ref_WaitP1:
      return "SCALE ref: First point of reference length | ESC cancel";
    case SP::Ref_WaitP2:
      return "SCALE ref: Second point (reference length) | ESC cancel";
    case SP::NewLength_WaitTypedOrP1:
      return "SCALE ref: Type new length (model units) or pick first point of new length | ESC";
    case SP::NewLength_WaitP2:
      return "SCALE ref: Second point of new length (preview) | ESC cancel";
    default:
      return "";
    }
  }
  return "";
}

const char* DeleteCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Delete)
    return "";
  return kSelectObjectsPrompt;  // REQ-121
}

const char* JoinCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Join)
    return "";
  return kSelectObjectsPrompt;  // REQ-121
}

const char* TrimCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using TP = AppCommandState::TrimPhase;
  if (st.active != K::Trim)
    return "";
  switch (st.trimPhase) {
  case TP::SelectCuttingEdges:
    return "TRIM: Pick cutting edges (hover highlights) | Enter | [L] — draw the trim line | ESC cancel";
  case TP::CuttingLine_WaitP1:
    return "TRIM: First point of the trim line | [T] — pick cutting edges instead | ESC cancel";
  case TP::CuttingLine_WaitP2:
    return "TRIM: Second point — dashed = removed part (midpoint picks side) | Ortho | ESC";
  case TP::SelectTrimTargets:
    return "TRIM: Click segment near end to remove | Enter done | ESC cancel";
  }
  return "";
}

const char* OffsetCommandFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using OP = AppCommandState::OffsetPhase;
  if (st.active != K::Offset)
    return "";
  switch (st.offsetPhase) {
  case OP::WaitDistanceOrThrough:
    return "OFFSET: Type offset distance, or T for through-point mode | ESC cancel";
  case OP::WaitSelectEntity:
    return "OFFSET: Pick line, circle, arc, ellipse, or polyline | Enter to finish | ESC cancel";
  case OP::WaitSidePick:
    return "OFFSET: Pick side of object (polyline/ellipse use closest edge) | ESC cancel";
  case OP::WaitThroughPick:
    return "OFFSET: Click the through point (line / circle / arc) | ESC cancel";
  }
  return "";
}

const char* ZoomCommandFooterHint(const AppCommandState& st) {
  if (st.active != AppCommandState::Kind::Zoom)
    return "";
  return "ZOOM WINDOW: Two corners (unsnapped) — rubber previews fit area | ESC cancel";
}

const char* LineCommandFooterHint(const AppCommandState& st) {
  using LP = AppCommandState::LinePhase;
  using SAP = AppCommandState::SegmentAnglePickPhase;
  if (st.active != AppCommandState::Kind::Line)
    return "";
  if (st.linePhase == LP::NeedFirstPoint)
    return "LINE: First point — click or X,Y | ESC ends command";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAngleKeyboardAwaitBearing)
    return "LINE: Type bearing ° CW from N (decimal/DMS) | blank Enter cancels | ESC ends command";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP1)
    return "LINE bearing pick: First direction point — click | 2P started | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP2)
    return "LINE bearing pick: Second point — direction | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
    return "LINE bearing pick: Enter locks | +90/-45 adjust+lock | ESC cancels pick";
  if (st.linePhase == LP::NeedNextPoint && st.segmentAngleLockActive)
    return "LINE (bearing lock): distance ± along ray | click on line | X,Y | @dx,dy | [A] clears";
  // Basic next-point (ortho or not). Bracketed [A]/[2P] are rendered as clickable
  // links in the command-line footer (DrawCommandLinePanel); the same text feeds the
  // dynamic-cursor label and the height calc, so keep it identical.
  return "Next: click; X, Y; @dx,dy; [A]zimuth, [2P];";
}

const char* DrawingExtrasFooterHint(const AppCommandState& st) {
  using K = AppCommandState::Kind;
  using PP = AppCommandState::PolylinePhase;
  using AP = AppCommandState::ArcPhase;
  using EP = AppCommandState::EllipsePhase;
  using TP = AppCommandState::TextCmdPhase;
  using MP = AppCommandState::MtextPhase;
  using DP = AppCommandState::DimPhase;

  if (st.active == K::IdPoint)
    return "ID: Pick point (OSNAP when enabled) or type X,Y — logs UCS World | ESC cancel";
  if (st.active == K::StationOffsetPick)
    return "GETSTATIONOFFSET: Pick point (OSNAP when enabled) or type X,Y — reports station and offset | ESC ends";
  if (const char* alignmentPrompt = AlignmentCommandPrompt(st))  // REQ-485
    return alignmentPrompt;
  if (st.active == K::GeoMarkPoint)  // REQ-359
    return "GEOMARKPOINT: Pick the position or type X,Y | ESC cancel";
  if (st.active == K::GeoMarkLatLong)
    return st.geoCmdPhase == AppCommandState::GeoCmdPhase::WaitFirst
               ? "GEOMARKLATLONG: Latitude — decimal degrees or D M S N/S (lat,long in one go) | ESC cancel"
               : "GEOMARKLATLONG: Longitude — decimal degrees (west negative) or D M S E/W | ESC cancel";
  if (st.active == K::DrawingSettingsPick)  // REQ-360
    return "DRAWINGSETTINGS: Pick for the Transformation tab or type X,Y | ESC back to the window";
  if (st.active == K::GeoReorientMarker)
    return st.geoCmdPhase == AppCommandState::GeoCmdPhase::WaitFirst
               ? "GEOREORIENTMARKER: Pick the design point or type X,Y | ESC cancel"
               : "GEOREORIENTMARKER: Pick a point to the north of the design point | ESC cancel";
  if (st.active == K::GeoCaptureArea)  // REQ-364
    return st.geoCmdPhase == AppCommandState::GeoCmdPhase::Capturing
               ? "Capture Area: gathering the map tiles | ESC cancel (nothing is kept)"
               : st.geoCmdPhase == AppCommandState::GeoCmdPhase::WaitFirst
                     ? "Capture Area: Pick the first corner or type X,Y | ESC cancel"
                     : "Capture Area: Pick the opposite corner or type X,Y | ESC cancel";

  if (st.active == K::SurveyInverse) {
    using SIP = AppCommandState::SurveyInversePhase;
    if (st.surveyInversePhase == SIP::WaitFrom)
      return "INVERSE: First point — pick or X,Y (Easting, Northing) | ESC cancel";
    return "INVERSE: Second point — pick or X,Y / @ from first | ESC cancel";
  }

  if (st.active == K::Dist) {
    using DP = AppCommandState::DistPhase;
    if (st.distPhase == DP::WaitFrom)
      return "DIST: First point — pick or X,Y | ESC cancel";
    return "DIST: Second point — pick or X,Y / @ from first | ESC cancel";
  }

  if (st.active == K::SurfaceElevGrade) {
    using SEP = AppCommandState::SurfaceElevPhase;
    if (st.surfaceElevPhase == SEP::WaitFirst)
      return "SURFELEV: Pick a point for its surface elevation | ESC cancel";
    return "SURFELEV: Second point for grade | ESC stops after the elevation";
  }

  if (st.active == K::WaterDrop)
    return "WATERDROP: Pick a plan position on the surface | ESC cancel";
  if (st.active == K::Catchment)
    return "CATCHMENT: Pick an outlet on the surface | ESC cancel";
  if (st.active == K::SwapTinEdge)
    return "SURFSWAPEDGE: Pick an interior TIN edge | ESC cancel";
  if (st.active == K::AddTinPoint)
    return "SURFACEADDPOINT: Pick a plan position (Z = work plane) | ESC cancel";
  if (st.active == K::DelTinPoint)
    return "SURFACEDELPOINT: Pick the definition point to remove | ESC cancel";
  if (st.active == K::MoveTinPoint) {
    using MP = AppCommandState::MoveTinPointPhase;
    if (st.moveTinPointPhase == MP::WaitFrom)
      return "SURFACEMOVEPOINT: Pick the point to move | ESC cancel";
    return "SURFACEMOVEPOINT: Pick the new position (Z = work plane) | ESC cancel";
  }
  if (st.active == K::DelTinLine)
    return "SURFDELLINE: Pick an interior TIN edge | ESC cancel";
  if (st.active == K::QuickProfile) {
    using QP = AppCommandState::QuickProfilePhase;
    if (st.quickProfilePhase == QP::WaitFirst)
      return "QUICKPROFILE: First point of the sample line | ESC cancel";
    return "QUICKPROFILE: Second point of the sample line | ESC cancel";
  }

  if (st.active == K::DesignateBreakline)
    return "DESIGNATEBREAKLINE: Pick a line or polyline | ESC cancel";
  if (st.active == K::DesignateBoundary)
    return "DESIGNATEBOUNDARY: Pick a CLOSED polyline | ESC cancel";

  // REQ-304 (GitHub issue #82). HATCH/ELEV/VPFREEZE/VPTHAW/PDFATTACH/FEATURELINE/FILLET/CHAMFER
  // previously had no branch here, so both the command line's live hint and the dynamic cursor
  // (which reads this same function — CadUi.cpp's CommandInputHint fallback) fell through to the
  // generic "Command:" placeholder for every one of their states.
  if (st.active == K::Hatch)
    return "HATCH: Pick an internal point inside a closed area | ESC cancel";

  if (st.active == K::Elev) {
    static char buf[128];
    std::snprintf(buf, sizeof(buf), "ELEV: New default elevation <%.4f> — [W]orld = Z 0 | ESC cancel",
                  static_cast<double>(CadWorkPlaneElevation(st)));
    return buf;
  }

  if (st.active == K::VpFreeze)
    return "VPFREEZE: Pick objects to freeze their layer in this viewport | Enter/ESC to finish";
  if (st.active == K::VpThaw)
    return "VPTHAW: Pick objects to thaw their layer in this viewport | Enter/ESC to finish";

  if (st.active == K::PdfAttach) {
    using PAP = AppCommandState::PdfAttachPhase;
    if (st.pdfAttachPhase == PAP::WaitInsertPoint)
      return "PDFATTACH: Insertion point — click or X,Y | ESC cancel";
    if (st.pdfAttachPhase == PAP::WaitScaleRef)
      return "PDFATTACH: Second point for scale | ESC cancel";
    if (st.pdfAttachPhase == PAP::WaitRotationPt)
      return "PDFATTACH: Rotation reference point | ESC cancel";
    return "PDFATTACH: Configure in the dialog";
  }

  if (st.active == K::InsertBlock) {
    using IPh = AppCommandState::InsertBlockPhase;
    if (st.insertBlockPhase == IPh::WaitInsertPoint)
      return "INSERT: Insertion point — click or X,Y or X,Y,Z | ESC cancel";
    if (st.insertBlockPhase == IPh::WaitScale)
      return "INSERT: Scale point — click or X,Y | ESC cancel";
    if (st.insertBlockPhase == IPh::WaitRotation)
      return "INSERT: Specify rotation angle <0d0'0\"> — click or type | ESC cancel";
    if (st.insertBlockPhase == IPh::WaitAlignFace)
      return "INSERT: Pick a flat face to align the fitting | ESC cancel";
    if (st.insertBlockPhase == IPh::WaitConnectorTarget)
      return "INSERT: Pick target connection port — click near port | ESC cancel";
    if (st.insertBlockPhase == IPh::WaitAttributes)
      return "INSERT: Enter attribute values in the dialog | ESC cancel";
    return "INSERT: Configure in the Insert dialog";
  }

  if (st.active == K::FeatureLine) {
    if (st.featureLinePendingPoint) {
      static char buf[192];
      std::snprintf(buf, sizeof(buf), "FEATURELINE: Elevation for %s %zu <%.3f> | ESC cancel",
                    st.featureLineNextIsElevPoint ? "elevation point" : "point",
                    st.featureLineDraftElevPt.size() + 1,
                    static_cast<double>(st.featureLinePendingDefaultZ));
      return buf;
    }
    if (st.featureLineDraftVerts.empty())
      return "FEATURELINE: First point — click or X,Y/X,Y,Z | [E] = elevation point | [CLOSE]/[END] | ESC cancel";
    return "FEATURELINE: Next point — click or X,Y/X,Y,Z | [E] = elevation point | [CLOSE]/[END] | ESC cancel";
  }

  if (st.active == K::Fillet) {
    using FP = AppCommandState::FilletPhase;
    // Edges gathered by Ctrl+click while the command is running: say how many, and that a radius is
    // what finishes it. Ahead of the 2D prompts because it is the more specific state.
    if (CadSubObjectSelectionIsAllEdges(st)) {
      static char buf[128];
      std::snprintf(buf, sizeof(buf),
                    "FILLET: %d solid edge(s) | Ctrl+click more, radius <%.4f> | ESC cancel",
                    static_cast<int>(st.subObjectSelection.size()),
                    static_cast<double>(st.filletRadius));
      return buf;
    }
    if (st.filletSolidAwaitingRadius) {
      static char buf[128];
      std::snprintf(buf, sizeof(buf), "FILLET: radius for %d solid edge(s) <%.4f> | ESC cancel",
                    static_cast<int>(st.subObjectSelection.size()),
                    static_cast<double>(st.filletRadius));
      return buf;
    }
    if (st.filletTextAwaitingRadius) {
      static char buf[96];
      std::snprintf(buf, sizeof(buf), "FILLET: New radius <%.3f> | ESC cancel",
                    static_cast<double>(st.filletRadius));
      return buf;
    }
    if (st.filletTextAwaitingTrim) {
      static char buf[96];
      std::snprintf(buf, sizeof(buf), "FILLET: Trim mode [Trim/No trim] <%s> | ESC cancel",
                    st.cornerTrimMode ? "Trim" : "No trim");
      return buf;
    }
    if (st.filletPhase == FP::WaitSecondEntity)
      return "FILLET: Select second object | ESC cancel";
    static char buf[128];
    std::snprintf(buf, sizeof(buf), "FILLET: Select first object or [Radius/Trim] <R=%.3f, %s> | ESC cancel",
                  static_cast<double>(st.filletRadius), st.cornerTrimMode ? "Trim" : "No trim");
    return buf;
  }

  if (st.active == K::Chamfer) {
    using CP = AppCommandState::ChamferPhase;
    // Edges gathered by Ctrl+click while the command is running, then the distance prompt. Ahead of
    // the 2D prompts because they are the more specific state — the order FILLET's block uses.
    if (CadSubObjectSelectionIsAllEdges(st)) {
      static char buf[128];
      std::snprintf(buf, sizeof(buf),
                    "CHAMFER: %d solid edge(s) | Ctrl+click more, distance <%.4f> | ESC cancel",
                    static_cast<int>(st.subObjectSelection.size()),
                    static_cast<double>(st.chamferDist1));
      return buf;
    }
    if (st.chamferSolidAwaitingDistance) {
      static char buf[128];
      std::snprintf(buf, sizeof(buf), "CHAMFER: distance for %d solid edge(s) <%.4f> | ESC cancel",
                    static_cast<int>(st.subObjectSelection.size()),
                    static_cast<double>(st.chamferDist1));
      return buf;
    }
    if (st.chamferTextAwaitingFirstValue) {
      static char buf[96];
      if (st.chamferMode == 0)
        std::snprintf(buf, sizeof(buf), "CHAMFER Distance: First distance <%.3f> | ESC cancel",
                      static_cast<double>(st.chamferDist1));
      else
        std::snprintf(buf, sizeof(buf), "CHAMFER Angle: Distance <%.3f> | ESC cancel",
                      static_cast<double>(st.chamferDist1));
      return buf;
    }
    if (st.chamferTextAwaitingSecondDist) {
      static char buf[96];
      std::snprintf(buf, sizeof(buf), "CHAMFER: Second distance <%.3f> | ESC cancel",
                    static_cast<double>(st.chamferDist2));
      return buf;
    }
    if (st.chamferTextAwaitingAngle) {
      static char buf[96];
      std::snprintf(buf, sizeof(buf), "CHAMFER: Angle in degrees <%.1f> | ESC cancel",
                    static_cast<double>(st.chamferAngle));
      return buf;
    }
    if (st.chamferTextAwaitingTrim) {
      static char buf[96];
      std::snprintf(buf, sizeof(buf), "CHAMFER: Trim mode [Trim/No trim] <%s> | ESC cancel",
                    st.cornerTrimMode ? "Trim" : "No trim");
      return buf;
    }
    if (st.chamferPhase == CP::WaitSecondEntity)
      return "CHAMFER: Select second object | ESC cancel";
    static char buf[128];
    if (st.chamferMode == 0)
      std::snprintf(buf, sizeof(buf),
                    "CHAMFER: Select first object or [Distance/Angle/Trim] <D1=%.3f, D2=%.3f, %s> | ESC cancel",
                    static_cast<double>(st.chamferDist1), static_cast<double>(st.chamferDist2),
                    st.cornerTrimMode ? "Trim" : "No trim");
    else
      std::snprintf(buf, sizeof(buf),
                    "CHAMFER: Select first object or [Distance/Angle/Trim] <D=%.3f, A=%.1f, %s> | ESC cancel",
                    static_cast<double>(st.chamferDist1), static_cast<double>(st.chamferAngle),
                    st.cornerTrimMode ? "Trim" : "No trim");
    return buf;
  }

  if (st.active == K::Polyline) {
    using SAP = AppCommandState::SegmentAnglePickPhase;
    if (st.polylinePhase == PP::NeedFirstPoint)
      return "POLYLINE: First point — click or X,Y | [CLOSE] closes | ESC cancel";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAngleKeyboardAwaitBearing)
      return "POLYLINE: Type bearing ° CW from N | blank Enter cancels | ESC cancel";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP1)
      return "POLYLINE bearing pick: First direction click | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitP2)
      return "POLYLINE bearing pick: Second point | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAnglePickPhase == SAP::WaitAdjustOrCommit)
      return "POLYLINE bearing pick: Enter locks | +90/-45 adjust+lock | ESC cancels pick";
    if (st.polylinePhase == PP::NeedNextPoint && st.segmentAngleLockActive)
      return "POLYLINE (bearing lock): distance ± | click on ray | X,Y | [A] clears | [CLOSE] / [END]";
    return "POLYLINE: Next — click | X,Y | @dx,dy | [A]/[2P] | [CLOSE] / [END] | ESC";
  }
  if (st.active == K::Arc) {
    switch (st.arcPhase) {
    case AP::WaitStart:
      return "ARC: Start point | ESC cancel";
    case AP::WaitMid:
      return "ARC: Point on arc | ESC cancel";
    case AP::WaitEnd:
      return "ARC: End point | ESC cancel";
    }
  }
  if (st.active == K::Ellipse) {
    switch (st.ellPhase) {
    case EP::WaitCenter:
      return "ELLIPSE: Center | ESC cancel";
    case EP::WaitMajorEnd:
      return "ELLIPSE: Major axis end | ESC cancel";
    case EP::WaitRatio:
      return "ELLIPSE: Ratio (0-1] on command line | Enter = 0.5 | ESC cancel";
    }
  }
  if (st.active == K::Spline) {
    static char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "SPLINE: %zu points — click or type X,Y | CV / Fit | Close / End / Undo | ESC cancel",
                  st.splineDraftPoints.size());
    return buf;
  }
  if (const char* drawInquiry = CadDrawInquiryCommandInputHint(st))
    return drawInquiry;
  if (st.active == K::Text) {
    switch (st.textPhase) {
    case TP::WaitInsertion:
      return "TEXT: Insertion — click or X,Y | ESC cancel";
    case TP::WaitHeight:
      return "TEXT: Height — Enter for plot-scale default | ESC cancel";
    case TP::WaitRotation:
      return "TEXT: Rotation ° CW from north — decimal/DMS or Enter=0 | ESC cancel";
    case TP::WaitString:
      return "TEXT: Enter content | ESC cancel";
    }
  }
  if (st.active == K::Mleader) {
    using MLp = AppCommandState::MleaderPhase;
    switch (st.mleaderPhase) {
    case MLp::WaitArrowTip:
      return "MLEADER: Arrowhead location | ESC cancel";
    case MLp::WaitLanding:
      return "MLEADER: Landing location (text side) | ESC cancel";
    case MLp::WaitLabel:
      return "MLEADER: Edit label — Save to place | Esc cancel";
    }
  }
  if (st.active == K::MleaderAddLeader)
    return "Add Leader: Arrowhead location | ESC cancel";
  if (st.active == K::Mtext) {
    switch (st.mtextPhase) {
    case MP::WaitCorner1:
      return "MTEXT: First corner | ESC cancel";
    case MP::WaitCorner2:
      return "MTEXT: Opposite corner | ESC cancel";
    case MP::WaitString:
      return "MTEXT: Edit in drawing box — Ctrl+Enter reformats | Save to place | Esc cancel";
    }
  }
  if (st.active == K::DimAligned || st.active == K::DimLinear) {
    switch (st.dimPhase) {
    case DP::WaitExt1:
      return st.active == K::DimLinear ? "DIMLINEAR: Extension 1 | ESC cancel" : "DIMALIGNED: Extension 1 | ESC cancel";
    case DP::WaitExt2:
      return st.active == K::DimLinear ? "DIMLINEAR: Extension 2 | ESC cancel" : "DIMALIGNED: Extension 2 | ESC cancel";
    case DP::WaitDimLinePt:
      return st.active == K::DimLinear
                 ? "DIMLINEAR: Line — dominant X vs Y from chord mid; [H] / [V] keys; X,Y | @ from chord mid | ESC"
                 : "DIMALIGNED: Offset — click | X,Y | @ from chord mid | ESC";
    }
  }
  if (st.active == K::DimAngular) {
    using DAP = AppCommandState::DimAngularPhase;
    switch (st.dimAngularPhase) {
    case DAP::WaitVertex:
      return "DIMANGULAR: Vertex | X,Y | ESC cancel";
    case DAP::WaitRay1:
      return "DIMANGULAR: First ray point | X,Y | @ from vertex | ESC";
    case DAP::WaitRay2:
      return "DIMANGULAR: Second ray point | X,Y | @ from vertex | ESC";
    case DAP::WaitArc:
      return "DIMANGULAR: Arc radius (bisector) | click | X,Y | @ from vertex | ESC";
    }
  }
  return "";
}

void RepeatLastCommand(AppCommandState& st, std::vector<std::string>& log) {
  using K = AppCommandState::Kind;
  switch (st.lastCommand) {
    case K::Line:       StartLineCommand(st, log);       break;
    case K::Circle:     StartCircleCommand(st, log);     break;
    case K::Polyline:   StartPolylineCommand(st, log);   break;
    case K::Rect:       StartRectCommand(st, log);       break;
    case K::Arc:        StartArcCommand(st, log);        break;
    case K::Ellipse:    StartEllipseCommand(st, log);    break;
    case K::Spline:     StartSplineCommand(st, log);     break;
    case K::Point:      StartPointCommand(st, log);      break;
    case K::Leader:     StartLeaderCommand(st, log);     break;
    case K::Pedit:      StartPeditCommand(st, log);      break;
    case K::Measure:    StartMeasureCommand(st, log);    break;
    case K::Divide:     StartDivideCommand(st, log);     break;
    case K::Area:       StartAreaCommand(st, log);       break;
    case K::List:       StartListCommand(st, log);       break;
    case K::DataExtract: StartDataExtractCommand(st, log); break;
    case K::Table:      StartTableCommand(st, log);      break;
    case K::PointTable: StartAddPointTableCommand(st, log); break;
    case K::Text:       StartTextCommand(st, log);       break;
    case K::Mtext:      StartMtextCommand(st, log);      break;
    case K::Mleader:    StartMleaderCommand(st, log);    break;
    case K::DimAligned: StartDimAlignedCommand(st, log); break;
    case K::DimLinear:  StartDimLinearCommand(st, log);  break;
    case K::DimAngular: StartDimAngularCommand(st, log); break;
    case K::Move:       StartMoveCommand(st, log);       break;
    case K::Copy:       StartCopyCommand(st, log);       break;
    case K::Rotate:     StartRotateCommand(st, log);     break;
    case K::Scale:      StartScaleCommand(st, log);      break;
    case K::Mirror:     StartMirrorCommand(st, log);     break;
    case K::Lengthen:   StartLengthenCommand(st, log);   break;
    case K::Extend:     StartExtendCommand(st, log);     break;
    case K::Break:      StartBreakCommand(st, log);      break;
    case K::Stretch:    StartStretchCommand(st, log);    break;
    case K::Fillet:     StartFilletCommand(st, log);     break;
    case K::Chamfer:    StartChamferCommand(st, log);    break;
    case K::Delete:     StartDeleteCommand(st, log);     break;
    case K::Join:       StartJoinCommand(st, log);       break;
    case K::Trim:       StartTrimCommand(st, log);       break;
    case K::Offset:     StartOffsetCommand(st, log);     break;
    case K::Hatch:      StartHatchCommand(st, log);      break;
    case K::Move3d:
    case K::Rotate3d:
    case K::Scale3d:    StartGizmoCommand(st, st.lastCommand, log); break;
    case K::ChProp:     StartChPropCommand(st, log);     break;
    case K::MatchProp:  StartMatchPropCommand(st, log);  break;
    case K::LayMCur:    StartLayMCurCommand(st, log);    break;
    default: break;
  }
}
