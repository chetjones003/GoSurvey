// ADR-0012 / Issue #1578: PDF, PaperSpace, PointCloud, Layers, Project, and Misc command module.

#include "misc/CadMiscSessions.hpp"
#include "engine/CommandRegistry.hpp"
#include "engine/StepEngine.hpp"
#include "CadCommands.hpp"
#include "CadCommandsInternal.hpp"
#include "paperspace/CadPaperSpaceInternal.hpp"
#include "ProjectFiles.hpp"
#include "ProjectSettings.hpp"
#include "ToolspaceCatalog.hpp"
#include "TextStyle.hpp"
#include <GoSurvey/Core/StringUtil.hpp>
#include <GoSurvey/IO/LandXmlIo.hpp>
#include <GoSurvey/UI/FileDialogs.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>

void ExecutePipePerfCommand(AppCommandState& st, std::vector<std::string>& log);
void ExecuteToolspaceCommand(AppCommandState& st, const std::string& args,
                             std::vector<std::string>& log);
void ExportSelectedPointClouds(AppCommandState& st, PointCloudExportFormat format,
                               std::vector<std::string>& log);
bool ImportGltfModel(AppCommandState& st, const std::string& path, double unitScale, double insX,
                     double insY, double insZ, std::vector<std::string>& log);

namespace stepengine {

// ---------------------------------------------------------------------------
// PdfAttachSession
// ---------------------------------------------------------------------------

StepDescriptor PdfAttachSession::currentStep(const AppCommandState&) const {
  using PAP = PdfAttachPhase;
  if (phase == PAP::WaitInsertPoint) {
    StepDynInputSpec dyn;
    dyn.pointEntry = true;
    return StepDescriptor::Custom("PDFATTACH: Insertion point — click or X,Y | ESC cancel",
                                  StepClickRoute::PdfAttachInsertPoint, BlankEnterPolicy::Custom,
                                  false, false, dyn);
  }
  if (phase == PAP::WaitScaleRef) {
    return StepDescriptor::Custom("PDFATTACH: Second point for scale | ESC cancel",
                                  StepClickRoute::Ignore, BlankEnterPolicy::Custom);
  }
  if (phase == PAP::WaitRotationPt) {
    return StepDescriptor::Custom("PDFATTACH: Rotation reference point | ESC cancel",
                                  StepClickRoute::Ignore, BlankEnterPolicy::Custom);
  }
  return StepDescriptor::Custom("PDFATTACH: Configure in the dialog",
                                StepClickRoute::Ignore, BlankEnterPolicy::Custom);
}

StepSubmitResult PdfAttachSession::onText(AppCommandState& st, const std::string& trimmedLine,
                                          std::vector<std::string>& log) {
  using PAP = PdfAttachPhase;
  if (phase == PAP::WaitInsertPoint) {
    float px = 0.f;
    float py = 0.f;
    if (!ParseStoragePoint(st, trimmedLine, &px, &py, false, 0.f, 0.f)) {
      log.push_back("PDFATTACH — pick in viewport or type X,Y.");
      return StepSubmitResult::Handled;
    }
    SubmitPdfAttachInsertPoint(st, px, py, log);
    return StepSubmitResult::Handled;
  }
  if (phase == PAP::WaitDialog || phase == PAP::Building)
    log.push_back("PDFATTACH — complete the attach dialog, or ESC to cancel.");
  else
    log.push_back("PDFATTACH — pick the insertion point in the viewport, or ESC to cancel.");
  return StepSubmitResult::Handled;
}

void PdfAttachSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                      std::vector<std::string>& log) {
  if (phase == PdfAttachPhase::WaitInsertPoint) {
    SubmitPdfAttachInsertPoint(st, static_cast<float>(pick.localX),
                               static_cast<float>(pick.localY), log);
  }
}

void PdfAttachSession::onCancel(AppCommandState& st, std::vector<std::string>& log) {
  onReset(st);
  log.push_back("PDFATTACH canceled.");
}

void PdfAttachSession::onReset(AppCommandState& st) {
  if (st.pdfDraftCache) {
    PdfDraftCache_Free(st.pdfDraftCache);
    st.pdfDraftCache = nullptr;
  }
  if (st.pdfAttachPreviewReady) {
    PdfAttach_ReleaseTexture(st.pdfAttachPreview);
    st.pdfAttachPreviewReady = false;
  }
  st.pdfAttachDialogOpen = false;
}

// ---------------------------------------------------------------------------
// PaperRectViewportSession (MVIEW)
// ---------------------------------------------------------------------------

StepDescriptor PaperRectViewportSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", StepClickRoute::Ignore, BlankEnterPolicy::Custom);
}

StepSubmitResult PaperRectViewportSession::onText(AppCommandState& st,
                                                  const std::string& trimmedLine,
                                                  std::vector<std::string>& log) {
  const std::string option = StringUtil::toLowerAsciiCopy(trimmedLine);
  if (option == "fit" || option == "2" || option == "3" || option == "4" || option == "object") {
    (void)CreateMviewPreset(st, option, log);
    return StepSubmitResult::Handled;
  }
  log.push_back("MVIEW — option must be Fit, 2, 3, 4, or Object; Esc cancels.");
  return StepSubmitResult::Handled;
}

void PaperRectViewportSession::onViewportPick(AppCommandState& st, const StepPickInput& pick,
                                              std::vector<std::string>& log) {
  if (st.activeSpaceIndex < 0 ||
      static_cast<size_t>(st.activeSpaceIndex) >= st.drawing.paperLayouts.size()) {
    log.push_back("MVIEW — switch to a paper layout first.");
    EndSession(st);
    return;
  }
  if (mode == 1) {
    PaperRef hit{};
    const PaperLayout& layout = st.drawing.paperLayouts[static_cast<size_t>(st.activeSpaceIndex)];
    if (!PickPaperEntityAt(layout, static_cast<float>(pick.localX), static_cast<float>(pick.localY),
                           std::max(0.12f, CadOffsetEntityPickTolWorld(st)), &hit,
                           &st.drawing.blockDefs) ||
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
      EndSession(st);
    }
    return;
  }
  const float x = static_cast<float>(pick.localX);
  const float y = static_cast<float>(pick.localY);
  if (phase == 0) {
    firstXIn = x;
    firstYIn = y;
    phase = 1;
    log.push_back("MVIEW — click the opposite corner, or type [Fit/2/3/4/Object].");
    return;
  }
  if (std::fabs(x - firstXIn) < 0.1f || std::fabs(y - firstYIn) < 0.1f) {
    log.push_back("MVIEW — viewport corners must be at least 0.1 in apart; pick the opposite corner again.");
    return;
  }
  const float x0 = firstXIn;
  const float y0 = firstYIn;
  PushUndoSnapshot(st, "Create MVIEW viewport");
  AddViewportRect(st, st.activeSpaceIndex, x0, y0, x, y);
  EndSession(st);
  log.push_back("MVIEW — rectangular viewport created.");
}

void PaperRectViewportSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("MVIEW canceled.");
}

// ---------------------------------------------------------------------------
// VpFreezeSession / VpThawSession / ExtractCenterlineSession
// ---------------------------------------------------------------------------

StepDescriptor VpFreezeSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom(
      "VPFREEZE: Pick objects to freeze their layer in this viewport | Enter/ESC to finish",
      StepClickRoute::Ignore, BlankEnterPolicy::Finish);
}

StepSubmitResult VpFreezeSession::onText(AppCommandState&, const std::string&,
                                         std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

StepSubmitResult VpFreezeSession::onBlankEnter(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("VPFREEZE — exited.");
  return StepSubmitResult::Finished;
}

void VpFreezeSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("VPFREEZE — exited.");
}

StepDescriptor VpThawSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom(
      "VPTHAW: Pick objects to thaw their layer in this viewport | Enter/ESC to finish",
      StepClickRoute::Ignore, BlankEnterPolicy::Finish);
}

StepSubmitResult VpThawSession::onText(AppCommandState&, const std::string&,
                                       std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

StepSubmitResult VpThawSession::onBlankEnter(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("VPTHAW — exited.");
  return StepSubmitResult::Finished;
}

void VpThawSession::onCancel(AppCommandState&, std::vector<std::string>& log) {
  log.push_back("VPTHAW — exited.");
}

StepDescriptor ExtractCenterlineSession::currentStep(const AppCommandState&) const {
  return StepDescriptor::Custom("", StepClickRoute::ExtractCenterlinePick, BlankEnterPolicy::Custom);
}

StepSubmitResult ExtractCenterlineSession::onText(AppCommandState&, const std::string&,
                                                  std::vector<std::string>&) {
  return StepSubmitResult::Unparsed;
}

void ExtractCenterlineSession::onCancel(AppCommandState& st, std::vector<std::string>&) {
  st.extractCenterlineHoverValid = false;
}

// ---------------------------------------------------------------------------
// Helper for active drawing's project UID
// ---------------------------------------------------------------------------

namespace {

uint32_t ActiveDrawingProjectUid(const AppCommandState& st) {
  return st.activeDrawingIdx >= 1 &&
                 st.activeDrawingIdx < static_cast<int>(st.drawingTabs.size())
             ? st.drawingTabs[static_cast<size_t>(st.activeDrawingIdx)].projectUid
             : 0u;
}

} // namespace

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------

void RegisterMiscCommands(CommandRegistry& reg) {
  // --- PDF ---
  reg.Add({"pdfattach", "pa, pdfatt", "Attach a PDF underlay",
           CadCommandKind::PdfAttach, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPdfAttachCommand(st, log);
           }});
  reg.Add({"pdfview", "pv", "Open a PDF in the built-in viewer (REQ-387)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             st.pdfViewerPickRequest = true;
             log.push_back("PDFVIEW — choose a PDF to open in the viewer.");
           }});
  reg.Add({"pdfsplit", "", "Save chosen pages of the open PDF as a new PDF (REQ-389)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>&) {
             st.pdfSplitRequest = true;
           }});

  // --- PaperSpace / Clipboard ---
  reg.Add({"pasteorig", "po", "Paste at original coordinates",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPasteOrigCommand(st, log);
           }});
  reg.Add({"mview", "rectviewport, rectvp", "Rectangular paper-space viewport (two clicks)",
           CadCommandKind::PaperRectViewport, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPaperRectViewportCommand(st, log);
           }});
  reg.Add({"mspace", "ms", "Edit the model through the selected viewport (floating model space)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             if (st.activeSpaceIndex != kModelSpaceIndex && st.selectedViewportIndex >= 0)
               EnterFloatingModelSpace(st, st.activeSpaceIndex, st.selectedViewportIndex, log);
             else
               log.push_back("MSPACE — select a viewport in a paper layout first.");
           }});
  reg.Add({"pspace", "ps", "Return to paper space from a floating viewport",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             if (InFloatingModelSpace(st))
               ExitFloatingModelSpace(st, log);
             else
               log.push_back("PSPACE — not in a floating viewport.");
           }});
  reg.Add({"vpfreeze", "vpf", "Freeze the picked entities' layers in the current viewport",
           CadCommandKind::VpFreeze, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartVpFreezeCommand(st, log);
           }});
  reg.Add({"vpthaw", "vpt", "Thaw the picked entities' layers in the current viewport",
           CadCommandKind::VpThaw, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartVpThawCommand(st, log);
           }});
  reg.Add({"layout", "", "", CadCommandKind::None, false, false,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             HandleLayoutCommand(args, st, log);
           }});
  reg.Add({"vpscale", "", "", CadCommandKind::None, false, false,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             HandleVpscaleCommand(args, st, log);
           }});
  reg.Add({"vpmatchscale", "", "", CadCommandKind::None, false, false,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             HandleVpMatchScaleCommand(args, st, log);
           }});
  reg.Add({"pagesetup", "", "", CadCommandKind::None, false, false,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartPageSetupCommand(st, log);
           }});

  // --- Point Cloud / 3D Model / LandXML ---
  reg.Add({"importmodel", "gltf, import3d",
           "Import a glTF/GLB 3D model as reference geometry",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::string path;
             double scale = 1.0;
             double ix = 0.0, iy = 0.0, iz = 0.0;
             std::string rest = StringUtil::trimCopy(args);
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
           }});

  reg.Add({"pointcloudattach", "importpointcloud",
           "Import a point cloud (E57, PTS, PTX, LAS, or LAZ) as reference geometry (REQ-172)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::string path;
             const std::string rest = StringUtil::trimCopy(args);
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
             if (RequestProjectAttach(st, AppCommandState::ProjectAttachPrompt::Kind::PointCloud,
                                      path, log))
               return;
             StartPointCloudImportAsync(st, path, log);
           }});

  reg.Add({"exportpts", "",
           "Export the selected point cloud(s) (or all, if none selected) to a PTS file (REQ-172)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExportSelectedPointClouds(st, PointCloudExportFormat::Pts, log);
           }});
  reg.Add({"exportptx", "",
           "Export the selected point cloud(s) (or all, if none selected) to a PTX file (REQ-172)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExportSelectedPointClouds(st, PointCloudExportFormat::Ptx, log);
           }});
  reg.Add({"exportlas", "",
           "Export the selected point cloud(s) (or all, if none selected) to a LAS file (REQ-172)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExportSelectedPointClouds(st, PointCloudExportFormat::Las, log);
           }});
  reg.Add({"exportlaz", "",
           "Export the selected point cloud(s) (or all, if none selected) to a LAZ file (REQ-172)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExportSelectedPointClouds(st, PointCloudExportFormat::Laz, log);
           }});
  reg.Add({"extractcenterline", "", "", CadCommandKind::ExtractCenterline, false, false,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartExtractCenterlineCommand(st, log);
           }});

  reg.Add({"exportlandxml", "",
           "Export survey points, TIN surfaces and feature lines to LandXML 1.2: EXPORTLANDXML [<path>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::string path = StringUtil::trimCopy(args);
             if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
               path = path.substr(1, path.size() - 2);
             if (path.empty())
               log.push_back("EXPORTLANDXML — type a destination, e.g. EXPORTLANDXML \"site.xml\".");
             else
               ExportLandXmlFile(st, path.c_str(), log);
           }});

  reg.Add({"importlandxml", "",
           "Import survey points, TIN surfaces and feature lines from LandXML 1.2: IMPORTLANDXML [<path>]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             std::string path = StringUtil::trimCopy(args);
             if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
               path = path.substr(1, path.size() - 2);
             if (st.activeDrawingIdx <= 0 ||
                 st.activeDrawingIdx >= static_cast<int>(st.drawingTabs.size()))
               log.push_back("IMPORTLANDXML — open a drawing first.");
             else if (path.empty())
               log.push_back("IMPORTLANDXML — type a file, e.g. IMPORTLANDXML \"site.xml\".");
             else if (ActiveProjectPointsReadOnly(st))
               log.push_back("IMPORTLANDXML — this project is read-only; nothing was imported.");
             else
               ImportLandXmlFile(st, path.c_str(), log);
           }});

  // --- Layers / Styles / Units / PlotScale / Options / Help / Survey ---
  reg.Add({"layer", "la", "Open the Layer manager",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             SyncDrawingLayerTableWithGeometry(st);
             st.showLayerManagerWindow = true;
             log.push_back("LAYER — layer manager opened.");
           }});
  reg.Add({"style", "st, ddstyle", "Text style manager: create / edit named text styles",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             TextStyles::EnsureStandard(st.drawing.textStyles);
             st.showTextStyleManagerWindow = true;
             log.push_back("STYLE — text style manager opened.");
           }});
  reg.Add({"units", "un, ddunits", "Drawing units: display precision & angle format",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             st.showUnitsWindow = true;
             log.push_back("UNITS — drawing units dialog opened.");
           }});
  reg.Add({"plotscale", "pscale", "Set the plot scale",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             if (args.empty()) {
               log.push_back(
                   "PLOTSCALE sets drawing units per plotted inch (e.g. 50 for civil 1\"=50'). Usage: PLOTSCALE 50");
               return;
             }
             std::istringstream iss(args);
             float pv = 0.f;
             if (!(iss >> pv) || pv <= 0.f) {
               log.push_back(
                   "PLOTSCALE — usage: PLOTSCALE <model_units_per_plotted_inch> (example: 50 for 1\"=50').");
               return;
             }
             if (pv != st.drawing.modelUnitsPerPlottedInch) {
               PushUndoSnapshot(st, "PLOTSCALE");
               SetDrawingPlotScale(st, pv);
               NoteUserPlotScale(st);
             }
             log.push_back("Plot scale: 1 plotted inch = " + std::to_string(pv) + " model units.");
           }});
  reg.Add({"options", "op, settings", "Open the Options dialog",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartOptionsCommand(st, log);
           }});
  reg.Add({"help", "", "Show command help",
           CadCommandKind::None, false, true,
           [](AppCommandState&, const std::string&, std::vector<std::string>& log) {
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
           }});

  reg.Add({"traverse", "trav, traverseeditor", "Open the LSA Editor",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartTraverseEditorCommand(st, log);
           }});
  reg.Add({"createpoints", "crtpts", "Create survey points",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartCreatePointsCommand(st, log);
           }});
  reg.Add({"viewpoints", "vwpts", "View / edit survey points",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartViewPointsCommand(st, log);
           }});
  reg.Add({"importpoints", "imppts", "Import survey points",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartImportPointsCommand(st, log);
           }});
  reg.Add({"exportpoints", "exppts", "Export survey points",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             StartExportPointsCommand(st, log);
           }});

  // --- Project & Drawing Settings ---
  reg.Add({"drawingsettings", "editdrawingsettings",
           "Drawing Settings: units, scale and the drawing's settings",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             st.showDrawingSettingsWindow = true;
             log.push_back("DRAWINGSETTINGS — Drawing Settings opened.");
           }});
  reg.Add({"projectsettings", "",
           "Project Settings: the project's coordinate system, units and defaults (REQ-375)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             const uint32_t uid = ActiveDrawingProjectUid(st);
             if (uid == 0) {
               log.push_back("PROJECTSETTINGS — this drawing is not in a project.");
             } else {
               st.projectSettingsUid = uid;
               log.push_back("PROJECTSETTINGS — Project Settings opened.");
             }
           }});
  reg.Add({"projecthealth", "",
           "Project Health: linked, missing and unsaved files before you pack or hand over (REQ-379)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             const uint32_t uid = ActiveDrawingProjectUid(st);
             if (uid == 0) {
               log.push_back("PROJECTHEALTH — this drawing is not in a project.");
             } else {
               st.projectHealthUid = uid;
               log.push_back("PROJECTHEALTH — Project Health opened.");
             }
           }});
  reg.Add({"packproject", "",
           "Pack Project: write the whole project as one .gspack file to send (REQ-380)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             const uint32_t uid = ActiveDrawingProjectUid(st);
             if (uid == 0) {
               log.push_back("PACKPROJECT — this drawing is not in a project.");
             } else {
               st.projectPackPrompt = {};
               st.projectPackPrompt.projectUid = uid;
               log.push_back("PACKPROJECT — Pack Project opened.");
             }
           }});
  reg.Add({"turnover", "",
           "Create Turnover: record which project files were handed to whom, and when (REQ-381)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             const uint32_t uid = ActiveDrawingProjectUid(st);
             if (uid == 0) {
               log.push_back("TURNOVER — this drawing is not in a project.");
             } else {
               st.projectTurnoverPrompt = {};
               st.projectTurnoverPrompt.projectUid = uid;
               log.push_back("TURNOVER — Create Turnover opened.");
             }
           }});
  reg.Add({"openpack", "",
           "Open Packed Project: unpack a .gspack into a folder and open it (REQ-380)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             st.openPackRequested = true;
             log.push_back("OPENPACK — choose the .gspack file to open.");
           }});
  reg.Add({"adddrawing", "",
           "Add Drawing to Project: bring an existing drawing and its points into this project (REQ-378)",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             const uint32_t uid = ActiveDrawingProjectUid(st);
             if (uid == 0) {
               log.push_back("ADDDRAWING — open a drawing of the project first.");
             } else {
               st.addDrawingToProjectUid = uid;
               log.push_back("ADDDRAWING — choose the drawing to add.");
             }
           }});

  // --- Benchmarks / Diagnostics / Toolspace ---
  reg.Add({"bench", "",
           "REQ-100 frame-budget benchmark: BENCH [segments] | BENCH SURFACE [points] | BENCH MESH [triangles] | BENCH SOLID [count]",
           CadCommandKind::None, false, true,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             int segs = 250000;
             int frames = 900;
             st.bench.surfacePointCount = 0;
             st.bench.surfaceTriangleCount = 0;
             st.bench.meshTriangleCount = 0;
             st.bench.solidCount = 0;
             st.bench.solidTriangleCount = 0;
             st.bench.alignmentCount = 0;
             st.bench.alignmentEntityCount = 0;

             std::istringstream iss(args);
             std::string benchArg;
             const std::streampos afterTok = iss.tellg();
             if (iss >> benchArg) {
               const std::string lower = StringUtil::toLowerAsciiCopy(benchArg);
               if (lower == "surface" || lower == "surf" || lower == "s") {
                 int pts = 100000;
                 int v = 0;
                 if (iss >> v) pts = v;
                 if (iss >> v) frames = v;
                 st.bench.surfacePointCount = pts;
                 StartFrameBudgetBench(st, 1, frames, log);
                 return;
               }
               if (lower == "solid" || lower == "solids") {
                 int count = 400;
                 int v = 0;
                 if (iss >> v) count = v;
                 if (iss >> v) frames = v;
                 st.bench.solidCount = count;
                 StartFrameBudgetBench(st, 1, frames, log);
                 return;
               }
               if (lower == "alignment" || lower == "alignments") {
                 int count = 1000, entities = 100;
                 int v = 0;
                 if (iss >> v) count = v;
                 if (iss >> v) entities = v;
                 if (iss >> v) frames = v;
                 st.bench.alignmentCount = std::clamp(count, 1, 100000);
                 st.bench.alignmentEntityCount = std::clamp(entities, 1, 100000);
                 StartFrameBudgetBench(st, 1, frames, log);
                 return;
               }
               if (lower == "pdfview" || lower == "pdf") {
                 int pages = 500;
                 int v = 0;
                 const std::streampos beforeArg = iss.tellg();
                 if (iss >> v) {
                   pages = std::clamp(v, 1, 5000);
                 } else {
                   iss.clear();
                   iss.seekg(beforeArg);
                   std::string path;
                   std::getline(iss, path);
                   path = StringUtil::trimCopy(path);
                   if (!path.empty()) {
                     st.pdfViewBenchPath = path;
                     log.push_back("BENCH PDFVIEW - timing " + path);
                     return;
                   }
                 }
                 st.pdfViewBenchPages = pages;
                 log.push_back("BENCH PDFVIEW — building a " + std::to_string(pages) +
                               "-page PDF; the viewer window will scroll it.");
                 return;
               }
               if (lower == "pdfcompare") {
                 int pages = 500;
                 int v = 0;
                 if (iss >> v) pages = std::clamp(v, 1, 5000);
                 st.pdfCompareBenchPages = pages;
                 log.push_back("BENCH PDFCOMPARE — building two " + std::to_string(pages) +
                               "-page PDFs; the viewer window will overlay and pan them.");
                 return;
               }
               if (lower == "pdfdiff") {
                 st.pdfDiffBench = true;
                 log.push_back(
                     "BENCH PDFDIFF — building two 36 x 24 in sheets; the viewer window will align and compare them.");
                 return;
               }
               if (lower == "mesh" || lower == "m") {
                 int tris = 2000000;
                 int v = 0;
                 if (iss >> v) tris = v;
                 if (iss >> v) frames = v;
                 st.bench.meshTriangleCount = tris;
                 StartFrameBudgetBench(st, 1, frames, log);
                 return;
               }
               if (lower == "pointcloud" || lower == "cloud" || lower == "pc") {
                 std::string path;
                 std::getline(iss, path);
                 StartPointCloudBench(st, StringUtil::trimCopy(path), log);
                 return;
               }
               iss.clear();
               iss.seekg(afterTok);
             } else {
               iss.clear();
             }
             int v = 0;
             if (iss >> v) segs = v;
             if (iss >> v) frames = v;
             StartFrameBudgetBench(st, segs, frames, log);
           }});

  reg.Add({"pipeperf", "", "", CadCommandKind::None, false, false,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             ExecutePipePerfCommand(st, log);
           }});
  reg.Add({"perfhud", "framestats", "", CadCommandKind::None, false, false,
           [](AppCommandState& st, const std::string&, std::vector<std::string>& log) {
             st.perfHudVisible = !st.perfHudVisible;
             log.push_back(std::string("PERFHUD — frame-time overlay ") +
                           (st.perfHudVisible ? "ON." : "OFF."));
           }});
  reg.Add({"toolspace", "ts", "", CadCommandKind::None, false, false,
           [](AppCommandState& st, const std::string& args, std::vector<std::string>& log) {
             ExecuteToolspaceCommand(st, args, log);
           }});
}

} // namespace stepengine
