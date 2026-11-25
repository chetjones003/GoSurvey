// Document snapshot, undo/redo, entity IDs, draft-tool reset, and shared command-state helpers.
// Extracted from CadCommands.cpp as part of ADR-0012 / Issue #1578.

#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "engine/StepEngine.hpp"
#include "TextStyle.hpp"
#include <GoSurvey/Core/StringUtil.hpp>

#include <algorithm>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

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
    c.alignmentLabelStyles = AlignmentLabelStyles::DefaultAlignmentLabelStyles();
  if (c.dimensionStyles.empty())
    c.dimensionStyles = DimensionStyles::DefaultStyles();
  if (c.multileaderStyles.empty())
    c.multileaderStyles = MultileaderStyles::DefaultStyles();
  const bool layerExists = std::any_of(c.drawingLayerTable.begin(), c.drawingLayerTable.end(),
                                       [&](const CadLayerRow& row) { return row.name == c.currentLayer; });
  if (!layerExists)
    c.currentLayer = "0";
  if (AlignmentStyles::Find(c.alignmentStyles, c.alignmentStyleCurrent) == nullptr)
    c.alignmentStyleCurrent = AlignmentStyles::kStandardName;
  if (TextStyles::Find(c.textStyles, c.activeTextStyleName) == nullptr) {
    c.activeTextStyleName = TextStyles::kStandardName;
    if (const TextStyle* s = TextStyles::Find(c.textStyles, c.activeTextStyleName))
      c.defaultPlottedTextHeightInches = std::max(s->heightInches, 1.e-6f);
  }
}

DrawingGeometrySnapshot CaptureGeometrySnapshot(const AppCommandState& st, const std::string& description) {
  DrawingGeometrySnapshot snap;
  snap.content = static_cast<const DrawingContent&>(st.drawing);
  for (auto& att : snap.content.pdfAttachments)
    att.glTexId = 0;
  snap.sectionPlaneFrameValid = st.viewportSectionClipFrameValid;
  if (st.viewportSectionClipFrameValid) {
    snap.sectionPlaneActive = st.viewportSectionClip;
    snap.sectionPlaneFrame = st.viewportSectionClipFrame;
    snap.sectionPlaneOffset = st.viewportSectionClipOffset;
    snap.sectionPlaneFlip = st.viewportSectionClipFlip;
    snap.sectionPlaneExtent = st.viewportSectionClipExtent;
  }
  snap.description = description;
  return snap;
}

void RestoreGeometrySnapshot(AppCommandState& st, const DrawingGeometrySnapshot& snap) {
  const bool layoutCountChanged = st.drawing.paperLayouts.size() != snap.content.paperLayouts.size();
  if (layoutCountChanged && st.floatingViewportIndex >= 0) {
    std::vector<std::string> ignored;
    ExitFloatingModelSpace(st, ignored);
  }
  const float previousPlotScale = st.drawing.modelUnitsPerPlottedInch;
  st.drawing = snap.content;
  FillEmptyDrawingContentTables(st.drawing);
  st.pipeRunWorldSolidsSig = 0;
  st.mleaderStyleDraft = st.drawing.activeMultileaderStyle;
  st.selectedPaperEntities.clear();
  if (layoutCountChanged) {
    ClearViewportSelection(st);
    if (st.activeSpaceIndex >= static_cast<int>(st.drawing.paperLayouts.size()))
      st.activeSpaceIndex = kModelSpaceIndex;
    st.lastPaperLayoutIndex =
        std::clamp(st.lastPaperLayoutIndex, 0, std::max(0, static_cast<int>(st.drawing.paperLayouts.size()) - 1));
  }
  if (snap.sectionPlaneFrameValid) {
    st.viewportSectionClip = snap.sectionPlaneActive;
    st.viewportSectionClipFrameValid = true;
    st.viewportSectionClipFrame = snap.sectionPlaneFrame;
    st.viewportSectionClipOffset = snap.sectionPlaneOffset;
    st.viewportSectionClipFlip = snap.sectionPlaneFlip;
    st.viewportSectionClipExtent = snap.sectionPlaneExtent;
  } else if (st.viewportSectionClipFrameValid) {
    st.viewportSectionClip = false;
    st.viewportSectionClipFrameValid = false;
    st.viewportSectionClipFrame = ucs::Ucs{};
    st.viewportSectionClipOffset = 0.0;
    st.viewportSectionClipFlip = false;
    st.viewportSectionClipExtent = SectionPlaneExtent{};
  }
  st.sectionPlaneSelected = false;
  st.sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
  st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  if (st.drawing.modelUnitsPerPlottedInch != previousPlotScale)
    SetDrawingPlotScale(st, st.drawing.modelUnitsPerPlottedInch);
}

const EntityKind kEntityKindsInSweepOrder[] = {
    EntityKind::Line,           EntityKind::Circle,         EntityKind::Arc,       EntityKind::Ellipse,
    EntityKind::Polyline,       EntityKind::Annotation,     EntityKind::FilledRegion, EntityKind::Mesh,
    EntityKind::FeatureLine,    EntityKind::Surface,        EntityKind::Table,     EntityKind::BlockRef,
    EntityKind::Solid,          EntityKind::PointCloud,     EntityKind::PositionMarker,
    EntityKind::Multileader,    EntityKind::Spline,         EntityKind::Point};

template <typename StateT>
auto* AttrsForKind(StateT& st, EntityKind k) {
  switch (k) {
  case EntityKind::Line:           return &st.drawing.userLineAttrs;
  case EntityKind::Circle:         return &st.drawing.userCircleAttrs;
  case EntityKind::Arc:            return &st.drawing.userArcAttrs;
  case EntityKind::Ellipse:        return &st.drawing.userEllAttrs;
  case EntityKind::Polyline:       return &st.drawing.userPolylineAttrs;
  case EntityKind::Annotation:     return &st.drawing.cadAnnotationAttrs;
  case EntityKind::FilledRegion:   return &st.drawing.cadFilledRegionAttrs;
  case EntityKind::Mesh:           return &st.drawing.cadMeshAttrs;
  case EntityKind::FeatureLine:    return &st.drawing.featureLineAttrs;
  case EntityKind::Surface:        return &st.drawing.cadSurfaceAttrs;
  case EntityKind::Table:          return &st.drawing.cadTableAttrs;
  case EntityKind::BlockRef:       return &st.drawing.cadBlockRefAttrs;
  case EntityKind::Solid:          return &st.drawing.cadSolidAttrs;
  case EntityKind::PointCloud:     return &st.drawing.cadPointCloudAttrs;
  case EntityKind::PositionMarker: return &st.drawing.cadPositionMarkerAttrs;
  case EntityKind::Multileader:    return &st.drawing.cadMultileaderAttrs;
  case EntityKind::Spline:         return &st.drawing.cadSplineAttrs;
  case EntityKind::Point:          return &st.drawing.cadPointAttrs;
  case EntityKind::PipeRun:        return &st.drawing.cadPipeRunAttrs;
  }
  return &st.drawing.userLineAttrs;
}

} // namespace

void SaveDocumentToSnapshot(AppCommandState& cmd, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= cmd.documents.size()) return;
  DrawingDocument& doc = cmd.documents[static_cast<size_t>(idx)];
  doc.currentViewportZoomScaleIndex = cmd.currentViewportZoomScaleIndex;
  doc.currentAnnotationScaleIndex = cmd.currentAnnotationScaleIndex;
  doc.viewportPanX = cmd.viewportPanX;
  doc.viewportPanY = cmd.viewportPanY;
  doc.viewportZoom = cmd.viewportZoom;
  doc.viewportPanZ = cmd.viewportPanZ;
  doc.viewportAzimuthDeg = cmd.viewportAzimuthDeg;
  doc.viewportElevationDeg = cmd.viewportElevationDeg;
  doc.viewportRollDeg = cmd.viewportRollDeg;
  doc.viewportProjection = cmd.viewportProjection;
  doc.viewportFovDeg = cmd.viewportFovDeg;
  doc.viewportSectionClip = cmd.viewportSectionClip;
  doc.viewportSectionClipOffset = cmd.viewportSectionClipOffset;
  doc.viewportSectionClipFlip = cmd.viewportSectionClipFlip;
  doc.viewportSectionClipFrameValid = cmd.viewportSectionClipFrameValid;
  doc.viewportSectionClipFrame = cmd.viewportSectionClipFrame;
  doc.viewportSectionClipExtent = cmd.viewportSectionClipExtent;
  doc.activeUcs = CadDrawingScopedUcs(cmd);
  doc.ucsPrevious = cmd.ucsPrevious;
  doc.activeViewName = cmd.activeViewName;
  doc.ucsFollow = cmd.ucsFollow;
  doc.content = static_cast<const DrawingContent&>(cmd.drawing);
  doc.dwgImportedLights = cmd.dwgImportedLights;
  doc.dwgImportedSun = cmd.dwgImportedSun;
  doc.dwgImportedSunPresent = cmd.dwgImportedSunPresent;
  doc.dwgImportedLightList = cmd.dwgImportedLightList;
  doc.dwgImportedLightListPresent = cmd.dwgImportedLightListPresent;
  doc.dwgImportedLightListDictKey = cmd.dwgImportedLightListDictKey;
  doc.blockRecent = cmd.blockRecent;
  doc.blockFavorites = cmd.blockFavorites;
  doc.nextEntityId = cmd.nextEntityId;
  doc.selectedSurveyPointIndices = cmd.selectedSurveyPointIndices;
  doc.selection = cmd.selection;
  doc.hiddenEntityIds = cmd.hiddenEntityIds;
  if (cmd.floatingUcsSwapActive && cmd.floatingViewportLayout >= 0 &&
      static_cast<size_t>(cmd.floatingViewportLayout) < doc.content.paperLayouts.size()) {
    PaperLayout& dl = doc.content.paperLayouts[static_cast<size_t>(cmd.floatingViewportLayout)];
    if (cmd.floatingViewportIndex >= 0 &&
        static_cast<size_t>(cmd.floatingViewportIndex) < dl.viewports.size())
      dl.viewports[static_cast<size_t>(cmd.floatingViewportIndex)].activeUcs = cmd.activeUcs;
  }
  doc.activeSpaceIndex = cmd.activeSpaceIndex;
  doc.cadGpuRevision = cmd.cadGpuRevision;
  doc.savedRevision = cmd.activeDocSavedRevision;
  doc.filePath = cmd.activeDocFilePath;
}

void RestoreDocumentFromSnapshot(AppCommandState& cmd, int idx) {
  if (idx < 0 || static_cast<size_t>(idx) >= cmd.documents.size()) return;
  const DrawingDocument& doc = cmd.documents[static_cast<size_t>(idx)];
  cmd.currentViewportZoomScaleIndex = doc.currentViewportZoomScaleIndex;
  cmd.currentAnnotationScaleIndex = doc.currentAnnotationScaleIndex;
  cmd.viewportPanX = doc.viewportPanX;
  cmd.viewportPanY = doc.viewportPanY;
  cmd.viewportZoom = doc.viewportZoom;
  cmd.viewportPanZ = doc.viewportPanZ;
  cmd.viewportAzimuthDeg = doc.viewportAzimuthDeg;
  cmd.viewportElevationDeg = doc.viewportElevationDeg;
  cmd.viewportRollDeg = doc.viewportRollDeg;
  cmd.viewportProjection = doc.viewportProjection;
  cmd.viewportFovDeg = doc.viewportFovDeg;
  cmd.viewportSectionClip = doc.viewportSectionClip;
  cmd.viewportSectionClipOffset = doc.viewportSectionClipOffset;
  cmd.viewportSectionClipFlip = doc.viewportSectionClipFlip;
  cmd.viewportSectionClipFrameValid = doc.viewportSectionClipFrameValid;
  cmd.viewportSectionClipFrame = doc.viewportSectionClipFrame;
  cmd.viewportSectionClipExtent = doc.viewportSectionClipExtent;
  cmd.viewAnimActive = false;
  cmd.activeUcs = doc.activeUcs;
  cmd.ucsPrevious = doc.ucsPrevious;
  cmd.activeViewName = doc.activeViewName;
  cmd.ucsFollow = doc.ucsFollow;
  cmd.drawing = doc.content;
  FillEmptyDrawingContentTables(cmd.drawing);
  cmd.dwgImportedLights = doc.dwgImportedLights;
  cmd.dwgImportedSun = doc.dwgImportedSun;
  cmd.dwgImportedSunPresent = doc.dwgImportedSunPresent;
  cmd.dwgImportedLightList = doc.dwgImportedLightList;
  cmd.dwgImportedLightListPresent = doc.dwgImportedLightListPresent;
  cmd.dwgImportedLightListDictKey = doc.dwgImportedLightListDictKey;
  cmd.blockRecent = doc.blockRecent;
  cmd.blockFavorites = doc.blockFavorites;
  cmd.nextEntityId = doc.nextEntityId;
  cmd.entityIdSweepRevision = kEntityIdSweepNever;
  cmd.pipeRunWorldSolids.clear();
  cmd.pipeRunWorldSolidAttrs.clear();
  cmd.pipeRunWorldSolidOwnerIndex.clear();
  cmd.pipeRunWorldSolidsSig = 0;
  cmd.selectedSurveyPointIndices = doc.selectedSurveyPointIndices;
  cmd.mleaderStyleDraft = cmd.drawing.activeMultileaderStyle;
  cmd.selection = doc.selection;
  cmd.hiddenEntityIds = doc.hiddenEntityIds;
  cmd.activeSpaceIndex = doc.activeSpaceIndex;
  cmd.lastPaperLayoutIndex = doc.activeSpaceIndex >= 0 ? doc.activeSpaceIndex : 0;
  cmd.selectedViewports.clear();
  cmd.selectedViewportIndex = -1;
  cmd.selectedViewportLayout = -1;
  cmd.paperGripCorner = -2;
  cmd.paperMovePhase = 0;
  cmd.paperScalePhase = 0;
  cmd.paperSelBoxActive = false;
  cmd.floatingViewportLayout = -1;
  cmd.floatingViewportIndex = -1;
  cmd.floatingUcsSwapActive = false;
  cmd.modelViewSaved = false;
  cmd.cadGpuRevision = doc.cadGpuRevision;
  cmd.activeDocSavedRevision = doc.savedRevision;
  cmd.activeDocFilePath = doc.filePath;
  stepengine::ResetActiveSession(cmd);
  cmd.active = AppCommandState::Kind::None;
  MigrateLegacyAnnotationTables(cmd);
}

DrawingGeometrySnapshot CadCaptureGeometrySnapshot(const AppCommandState& st, const std::string& description) {
  return CaptureGeometrySnapshot(st, description);
}

void CadRestoreGeometrySnapshot(AppCommandState& st, const DrawingGeometrySnapshot& snap) {
  RestoreGeometrySnapshot(st, snap);
}

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

void EnsureEntityIds(AppCommandState& st) {
  for (const CadRasterImage& image : st.drawing.cadRasterImages)
    if (image.id >= st.nextEntityId && image.id != std::numeric_limits<std::uint64_t>::max())
      st.nextEntityId = image.id + 1;
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
  return {};
}

void PushUndoSnapshot(AppCommandState& st, const std::string& description) {
  const int idx = st.activeDrawingIdx;
  if (idx < 0 || static_cast<size_t>(idx) >= st.documents.size())
    return;
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
  stepengine::ResetActiveSession(st);
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
  stepengine::ResetActiveSession(st);
  st.active = AppCommandState::Kind::None;
  st.selection.clear();
  st.selectedSurveyPointIndices.clear();
  log.push_back("REDO" + (desc.empty() ? "." : ": " + desc));
  WriteUndoHistoryLogLine("REDO: " + desc);
  return true;
}

void ResetAllCadDraftTools(AppCommandState& st) {
  stepengine::ResetActiveSession(st);
  st.sectionPlaneGripDrag = static_cast<int>(SectionPlaneGrip::None);
  st.sectionPlaneGripHover = static_cast<int>(SectionPlaneGrip::None);
  st.resolvedPointZValid = false;
  st.resolvedPointZ = 0.f;
  st.resolvedPointZTyped = false;
  st.polylineTypedZValid = false;
  st.polylineTypedZRelative = false;
  st.polylineTypedZ = 0.f;
  CloseMtextRichEditorUi(st);
  ClearDimGripInteraction(st);
  AbortMtextGripInteraction(st);
  st.blockCreateDialogOpen = false;
  st.blockCreateAwaitingBasePoint = false;
  st.wblockDialogOpen = false;
}

EntityAttributes MakeNewEntityAttrs(const AppCommandState& st) {
  EntityAttributes a;
  a.layer = st.drawing.currentLayer.empty() ? std::string("0") : st.drawing.currentLayer;
  a.color = st.drawing.currentColor.empty() ? std::string("ByLayer") : st.drawing.currentColor;
  a.linetype = "ByLayer";
  a.lineweightMm = -1.f;
  a.transparency = -1.f;
  return a;
}

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

bool ActiveProjectPointsReadOnly(const AppCommandState& st) {
  const int tab = st.activeDrawingIdx;
  if (tab <= 0 || tab >= static_cast<int>(st.drawingTabs.size())) return false;
  const auto& drawing = st.drawingTabs[static_cast<size_t>(tab)];
  if (drawing.projectUid == 0 ||
      drawing.pointsMode == AppCommandState::DrawingTab::PointsMode::Detached)
    return false;
  for (const auto& project : st.openProjects)
    if (project.uid == drawing.projectUid) return project.readOnly;
  return false;
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
  st.activeUcs = ucs::Ucs{};
  st.ucsPrevious.clear();
  st.ucsFollow = false;
  st.activeViewName.clear();
  st.nextEntityId = 1;
  st.entityIdSweepRevision = kEntityIdSweepNever;
  st.currentAnnotationScaleIndex = -1;
  st.currentViewportZoomScaleIndex = -1;
  st.dwgImportedLights.clear();
  st.dwgImportedSunPresent = false;
  st.dwgImportedLightList = {};
  st.dwgImportedLightListPresent = false;
  st.dwgImportedLightListDictKey.clear();
  st.solidDisplayCache.clear();
  st.solidDisplayGeometry.solids.clear();
  st.surfaceDisplayCache.clear();
  st.surfaceWatershedCache.clear();
  st.surfaceDisplayGeometry.lines.clear();
  st.surfaceDisplayGeometry.bandTriangles.clear();
  st.waterDropPreviewLines.clear();
  st.surfaceRebuildAsync.clear();
  st.blockRefWorldSolids.clear();
  st.blockRefWorldSolidAttrs.clear();
  st.blockRefWorldSolidsSig = 0;
  st.pipeRunWorldSolids.clear();
  st.pipeRunWorldSolidAttrs.clear();
  st.pipeRunWorldSolidOwnerIndex.clear();
  st.pipeRunWorldSolidsSig = 0;
  st.blockEditorName.clear();
  st.importedDxfAttrDefs.clear();
  ClearPendingOneShotObjectSnap(st);
  ClearCadSelection(st);
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
  ResetSegmentAngleLock(st);
  ResetAllCadDraftTools(st);
  st.selBoxWaitingSecond = false;
  st.copySurveyDupModalOpen = false;
  st.copySurveyDupModalOpenRequested = false;
  AbortMtextGripInteraction(st);
}

void ClearSelection(AppCommandState& st) {
  ClearCadSelection(st);
  st.selectedSurveyPointIndices.clear();
}
