# The GoSurvey system libraries. Ownership and allowed dependencies: docs/architecture/system-map.md.
#
# gosurvey_library(<target> <alias> [INTERFACE] [INCLUDE <public include dir>]
#                  [SOURCES ...] [PRIVATE_INCLUDES ...] [DEPS ...] [PRIVATE_DEPS ...])
function(gosurvey_library target alias)
  cmake_parse_arguments(L "INTERFACE" "INCLUDE" "SOURCES;PRIVATE_INCLUDES;DEPS;PRIVATE_DEPS" ${ARGN})
  if(L_INTERFACE)
    add_library(${target} INTERFACE)
    set(scope INTERFACE)
  else()
    add_library(${target} STATIC ${L_SOURCES})
    set(scope PUBLIC)
    gosurvey_target_warnings(${target})
  endif()
  add_library(${alias} ALIAS ${target})
  if(L_INCLUDE)
    target_include_directories(${target} ${scope} "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/${L_INCLUDE}>")
  endif()
  if(L_PRIVATE_INCLUDES)
    target_include_directories(${target} PRIVATE ${L_PRIVATE_INCLUDES})
  endif()
  if(L_DEPS)
    target_link_libraries(${target} ${scope} ${L_DEPS})
  endif()
  if(L_PRIVATE_DEPS)
    target_link_libraries(${target} PRIVATE ${L_PRIVATE_DEPS})
  endif()
endfunction()

# --- Foundation ---------------------------------------------------------------------------------
gosurvey_library(gosurvey_core GoSurvey::Core INCLUDE src/core/include
  SOURCES src/core/src/Core.cpp src/core/src/Units.cpp)
target_compile_definitions(gosurvey_core PRIVATE GOSURVEY_CORE_IMPLEMENTATION=1)

gosurvey_library(gosurvey_math GoSurvey::Math INTERFACE INCLUDE src/math/include DEPS GoSurvey::Core)

gosurvey_library(gosurvey_events GoSurvey::Events INCLUDE src/events/include
  SOURCES src/events/src/Events.cpp
  DEPS GoSurvey::Core GoSurvey::Math)

gosurvey_library(gosurvey_ui GoSurvey::UI INTERFACE INCLUDE src/ui/include DEPS GoSurvey::Core GoSurvey::Events)

gosurvey_library(gosurvey_debug GoSurvey::Debug INCLUDE src/debug/include
  SOURCES src/debug/src/BenchScene.cpp src/debug/src/DocumentInvariants.cpp
  DEPS GoSurvey::Core)

# The SHX font provider is Core-owned; Pdf uses it without depending on IO.
gosurvey_library(gosurvey_core_shx_font GoSurvey::CoreShxFont INCLUDE src/core/include
  SOURCES src/font/ShxFont.cpp
  PRIVATE_INCLUDES src
  DEPS GoSurvey::Core)

# --- Kernel -------------------------------------------------------------------------------------
gosurvey_library(gosurvey_geometry GoSurvey::Geometry INCLUDE src/geometry/include
  SOURCES
    src/geometry/src/geom2d.cpp
    src/geometry/src/TriangleGridIndex.cpp
    src/geometry/src/spline.cpp
    src/geometry/src/ClothoidIntegration.cpp
    src/geometry/src/curveintersect.cpp
    src/geometry/src/meshgeom.cpp
    src/geometry/src/brep.cpp
    src/geometry/src/nurbs.cpp
  DEPS GoSurvey::Math GoSurvey::Core)

# --- Domain -------------------------------------------------------------------------------------
gosurvey_library(gosurvey_document GoSurvey::Document INCLUDE src/document/include
  SOURCES src/document/src/MtextRichFormat.cpp
  DEPS GoSurvey::Geometry GoSurvey::Math GoSurvey::Core GoSurvey::Events)

gosurvey_library(gosurvey_survey GoSurvey::Survey INCLUDE src/survey/include
  SOURCES
    src/survey/src/Alignment.cpp
    src/survey/src/Clothoid.cpp
    src/survey/src/AlignmentLabels.cpp
    src/survey/src/AlignmentReport.cpp
    src/survey/src/AlignmentEdit.cpp
    src/survey/src/AlignmentLayout.cpp
    src/survey/src/AlignmentGround.cpp
    src/survey/src/AlignmentSampleLines.cpp
    src/survey/src/AlignmentViewFrames.cpp
    src/survey/src/AlignmentProfile.cpp
    src/survey/src/AlignmentDesign.cpp
    src/survey/src/AlignmentTables.cpp
    src/survey/src/CoordinateSystems.cpp
    src/survey/src/DrawingConversion.cpp
    src/survey/src/LocalGridTransform.cpp
    src/survey/src/EcefTransform.cpp
    src/survey/src/TraverseCalc.cpp
    src/survey/src/TraverseLeastSquares.cpp
    src/survey/src/TraverseReport.cpp
    src/survey/src/PointGroupRule.cpp
    src/survey/src/SurveyPoints.cpp
  PRIVATE_INCLUDES src src/alignment src/util
  DEPS GoSurvey::Document GoSurvey::Geometry GoSurvey::Math GoSurvey::Core
  PRIVATE_DEPS CsMap::csmap)
# Only CoordinateSystems.cpp may see the CS-MAP headers (ADR-063), so it skips the shared PCH.
set_source_files_properties(src/survey/src/CoordinateSystems.cpp PROPERTIES
  INCLUDE_DIRECTORIES "${GOSURVEY_CSMAP_DIR}/include"
  COMPILE_DEFINITIONS "WIN32;_WINDOWS"
  SKIP_PRECOMPILE_HEADERS ON)

gosurvey_library(gosurvey_renderer GoSurvey::Renderer INCLUDE src/renderer/include
  SOURCES src/renderer/src/AlignmentDisplayCache.cpp
  PRIVATE_INCLUDES src/renderer/src
  DEPS GoSurvey::Document GoSurvey::Geometry GoSurvey::Survey)

gosurvey_library(gosurvey_terrain GoSurvey::Terrain INCLUDE src/terrain/include
  SOURCES
    src/terrain/src/AlignmentFeatureLine.cpp
    src/terrain/src/contourgen.cpp
    src/terrain/src/daylight.cpp
    src/terrain/src/gridsurface.cpp
    src/terrain/src/surfaceanalysis.cpp
    src/terrain/src/surfacequery.cpp
    src/terrain/src/surfacestats.cpp
    src/terrain/src/surfacevolume.cpp
    src/terrain/src/tinbuild.cpp
    src/terrain/src/tinvolume.cpp
    src/terrain/src/watershed.cpp
  DEPS GoSurvey::Core GoSurvey::Math GoSurvey::Geometry GoSurvey::Survey)

gosurvey_library(gosurvey_pdf GoSurvey::Pdf INCLUDE src/pdf/include
  SOURCES
    src/pdf/src/PdfDocument.cpp
    src/pdf/src/PdfSplit.cpp
    src/pdf/src/PdfAnnotate.cpp
    src/pdf/src/PdfRaw.cpp
    src/pdf/src/PdfMeasure.cpp
    src/pdf/src/PdfScaleCheck.cpp
    src/pdf/src/PdfDimAudit.cpp
    src/pdf/src/PdfCoordExtract.cpp
    src/pdf/src/PdfAlign.cpp
    src/pdf/src/PdfDiff.cpp
    src/pdf/src/PdfViewerCore.cpp
    src/pdf/src/PdfSnap.cpp
  PRIVATE_INCLUDES src src/pdf
  DEPS GoSurvey::Core GoSurvey::Math GoSurvey::Geometry pdfium
  PRIVATE_DEPS GoSurvey::CoreShxFont)
target_compile_definitions(gosurvey_pdf PRIVATE NOMINMAX)

gosurvey_library(gosurvey_pointcloud GoSurvey::PointCloud INCLUDE src/pointcloud/include
  SOURCES src/pointcloud/src/Octree.cpp src/pointcloud/src/Cache.cpp src/pointcloud/src/CylinderFit.cpp
  PRIVATE_INCLUDES src
  DEPS GoSurvey::Math GoSurvey::Core)

# --- IO -----------------------------------------------------------------------------------------
gosurvey_library(gosurvey_io_traverse_report GoSurvey::IOTraverseReport INCLUDE src/io/include
  SOURCES src/io/src/TraverseReportExport.cpp
  PRIVATE_INCLUDES src
  PRIVATE_DEPS GoSurvey::Pdf)

gosurvey_library(gosurvey_io_pointcloud GoSurvey::IOPointCloud INCLUDE src/io/include
  SOURCES
    src/io/src/PointCloudCacheImport.cpp
    src/io/src/CadPointCloudE57.cpp
    src/io/src/CadPointCloudPts.cpp
    src/io/src/CadPointCloudPtx.cpp
    src/io/src/CadPointCloudLas.cpp
    src/io/src/CadPointCloudLaz.cpp
  PRIVATE_INCLUDES src
  DEPS GoSurvey::PointCloud
  PRIVATE_DEPS E57Format::E57Format Laszip::laszip)

# File formats and persistence. Still reaches into the command layer's headers (dependency baseline).
gosurvey_library(gosurvey_io GoSurvey::IO INCLUDE src/io/include
  SOURCES
    src/io/src/FbkImport.cpp
    src/io/src/TraverseIo.cpp
    src/io/src/TraverseCommandLang.cpp
    src/io/src/StarNetImport.cpp
    src/io/src/Rw5Import.cpp
    src/io/src/JobXmlImport.cpp
    src/io/src/SpreadsheetIo.cpp
    src/io/src/GsIo.cpp
    src/io/src/RasterImage.cpp
    src/io/src/UserPrefs.cpp
    src/io/src/RecentDrawings.cpp
    src/io/src/Project.cpp
    src/io/src/ProjectPointDb.cpp
    src/io/src/ProjectPointRules.cpp
    src/io/src/ProjectAddDrawing.cpp
    src/io/src/ProjectFiles.cpp
    src/io/src/ProjectPack.cpp
    src/io/src/ProjectTurnover.cpp
    src/io/src/WhatsNewContent.cpp
    src/io/src/WikiContent.cpp
    src/io/src/WikiHelp.cpp
    src/io/src/SurveyCsv.cpp
    src/io/src/LandXmlIo.cpp
    src/io/src/DxfIo.cpp
    src/io/src/DwgIo.cpp
    src/io/src/DwgProbe.cpp
    src/io/src/LibreDwg.cpp
    src/io/src/LibreDwgCad.cpp
    src/io/src/LibreDwgCivil.cpp
    src/io/src/LibreDwgField.cpp
    src/io/src/LibreDwgDynamicBlock.cpp
    src/io/src/LibreDwgMaterial.cpp
    src/io/src/LibreDwgAnnotContext.cpp
    src/io/src/LibreDwgVisualStyle.cpp
    src/io/src/LibreDwgLights.cpp
    src/io/src/LibreDwgPlant.cpp
    src/io/src/DxfColors.cpp
    src/io/src/DwgMeshConvert.cpp
    src/io/src/GsMigrate.cpp
    src/io/src/gltfimport.cpp
    src/io/src/stlimport.cpp
    src/io/src/AcisSatParser.cpp
    src/io/src/AcisSatWriter.cpp
  PRIVATE_INCLUDES
    src src/app src/commands src/viewport src/ui src/util src/io src/io/src src/debug/include
    src/survey src/survey/include src/network/include src/renderer/include src/platform src/pdf
    src/font src/update third_party "${PUGIXML_INCLUDE}" ${GOSURVEY_OPENXLSX_INCLUDES}
    "${CMAKE_BINARY_DIR}/generated"
  DEPS
    GoSurvey::Core GoSurvey::Math GoSurvey::Geometry GoSurvey::Document GoSurvey::Survey
    GoSurvey::Terrain GoSurvey::PointCloud GoSurvey::Pdf GoSurvey::IOPointCloud GoSurvey::IOTraverseReport
  PRIVATE_DEPS
    LibreDWG::libredwg E57Format::E57Format miniz Laszip::laszip GoSurvey::CoreShxFont
    gosurvey_openxlsx gosurvey_pugixml)
# The two LibreDWG bridges use its private headers.
set_source_files_properties(src/io/src/LibreDwg.cpp src/io/src/LibreDwgCad.cpp PROPERTIES
  INCLUDE_DIRECTORIES "${GOSURVEY_LIBREDWG_DIR}/src")

# --- Application domain -------------------------------------------------------------------------
# The command layer and everything else the app, the headless driver and the tests share. Nothing
# here opens a window or needs a GPU. Each executable supplies the platform seams: file dialogs
# (WinFileDialogs.cpp or HeadlessFileDialogs.cpp) and PDF attach (PdfAttach.cpp or HeadlessPdfAttach.cpp).
set(GOSURVEY_DOMAIN_SOURCES
  # Commands
  src/commands/CadCommands.cpp
  src/commands/pipe/CadPipeCommands.cpp
  src/commands/surface/CadSurfaceCommands.cpp
  src/commands/connect/CadBlockConnectCommands.cpp
  src/commands/blocks/CadBlockCreateCommands.cpp
  src/commands/report/CadVolumeReportCommands.cpp
  src/commands/CadCommands_Align.cpp
  src/commands/CadCommands_Alignment.cpp
  src/commands/CadCommands_Bench.cpp
  src/commands/CadCommands_DrawInquiry.cpp
  src/commands/CadCommands_Geo.cpp
  src/commands/CadCommands_Mleader.cpp
  src/commands/CadCommands_Ucs.cpp
  src/commands/CadBlocks.cpp
  src/commands/CadColor.cpp
  src/commands/CadCoordinateFrame.cpp
  src/commands/CadDynInput.cpp
  src/commands/CadField.cpp
  src/commands/CadLinetype.cpp
  src/commands/CadOnlineMap.cpp
  src/commands/CadPropertyEdit.cpp
  src/commands/CadSurfaceEdit.cpp
  src/commands/ConvertDrawing.cpp
  src/commands/EntityId.cpp
  src/commands/ProjectAddFlow.cpp
  src/commands/ProjectFiles.cpp
  src/commands/ProjectPoints.cpp
  src/commands/ProjectSettings.cpp
  src/commands/ProjectWarnings.cpp
  src/commands/SurveyPointActions.cpp
  src/commands/TraverseActions.cpp
  src/commands/TraverseFileActions.cpp
  src/commands/TraverseReportActions.cpp
  src/commands/ViewportZoomScale.cpp
  # Snapping and preview geometry (no GL despite the folder name)
  src/viewport/CadSnap.cpp
  src/viewport/TransformPreview.cpp
  src/viewport/CadRubberPreview.cpp
  src/alignment/AlignmentGrips.cpp
  src/util/solidpick.cpp
  src/util/WhatsNewLogic.cpp
  # PDF plot output (needs the command layer's drawing state)
  src/io/src/PdfPlot.cpp
  src/io/src/PlotStyleTable.cpp
  # Online map tiles (ADR-064); the app injects the network fetch and texture upload
  src/geo/WebMercator.cpp
  src/platform/MapTileService.cpp
  src/platform/StbImage.cpp
  # Platform pieces with no window
  src/platform/AppPaths.cpp
  src/platform/ProcessRun.cpp
  src/platform/CrashBuildInfo.cpp
  src/platform/CrashReporterWireReason.cpp
  src/update/UpdateCheck.cpp)
add_library(gosurvey_domain STATIC ${GOSURVEY_DOMAIN_SOURCES})
target_include_directories(gosurvey_domain PUBLIC
  src src/app src/commands src/viewport src/ui src/util src/io/src src/survey src/survey/include
  src/network/include src/renderer/include src/platform src/pdf src/font src/update third_party
  "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(gosurvey_domain PUBLIC
  GoSurvey::IO GoSurvey::Core GoSurvey::Math GoSurvey::Geometry GoSurvey::Survey GoSurvey::Debug
  GoSurvey::Terrain GoSurvey::PointCloud GoSurvey::IOPointCloud GoSurvey::Pdf GoSurvey::IOTraverseReport
  GoSurvey::Renderer GoSurvey::UI
  LibreDWG::libredwg E57Format::E57Format CsMap::csmap miniz Laszip::laszip
  comdlg32 ole32 shell32)
gosurvey_target_warnings(gosurvey_domain)
gosurvey_enable_pch(gosurvey_domain)
target_compile_definitions(gosurvey_domain PRIVATE NOMINMAX)
set_source_files_properties(src/commands/CadCommands.cpp PROPERTIES COMPILE_OPTIONS /bigobj)
if(GOSURVEY_ENABLE_ANALYZE)
  set_source_files_properties(src/platform/StbImage.cpp PROPERTIES COMPILE_OPTIONS /analyze-)
endif()

# Windowless implementations of the platform seams (file dialogs, PDF attach).
add_library(gosurvey_headless_seams STATIC
  src/platform/HeadlessFileDialogs.cpp
  src/platform/HeadlessPdfAttach.cpp)
target_link_libraries(gosurvey_headless_seams PUBLIC gosurvey_domain)
gosurvey_target_warnings(gosurvey_headless_seams)

# Umbrella for application consumers. Each system stays linkable on its own.
add_library(gosurvey_engine INTERFACE)
add_library(GoSurvey::Engine ALIAS gosurvey_engine)
target_link_libraries(gosurvey_engine INTERFACE gosurvey_domain GoSurvey::Document GoSurvey::Events)
