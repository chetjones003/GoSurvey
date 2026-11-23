# Test suites. Catch2 is test-only: GoSurvey.exe never links it.
gosurvey_vendored_library(Catch2_amalgamated "${GOSURVEY_TP}/catch2/catch_amalgamated.cpp")
target_include_directories(Catch2_amalgamated PUBLIC "${GOSURVEY_TP}/catch2")
add_library(Catch2::Catch2WithMain ALIAS Catch2_amalgamated)
list(APPEND CMAKE_MODULE_PATH "${GOSURVEY_TP}/catch2")
include(Catch)
enable_testing()

# Engine consumers must not see Core's private header.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES gosurvey_engine)
set(CMAKE_REQUIRED_DEFINITIONS -DGOSURVEY_CORE_IMPLEMENTATION=1)
unset(GOSURVEY_CORE_PRIVATE_HEADER_IS_CONSUMABLE CACHE)
check_cxx_source_compiles("#if __has_include(<CorePrivate.hpp>)\n#include <CorePrivate.hpp>\n#elif __has_include(<core/src/CorePrivate.hpp>)\n#include <core/src/CorePrivate.hpp>\n#else\n#error not visible\n#endif\nint main() { return 0; }"
  GOSURVEY_CORE_PRIVATE_HEADER_IS_CONSUMABLE)
unset(CMAKE_REQUIRED_LIBRARIES)
unset(CMAKE_REQUIRED_DEFINITIONS)
if(GOSURVEY_CORE_PRIVATE_HEADER_IS_CONSUMABLE)
  message(FATAL_ERROR "Core private headers are consumable by Engine clients")
endif()

# A system's own Catch2 suite: gosurvey_test(<exe> <label> SOURCES ... LIBS ... [INCLUDES ...] [PDFIUM])
function(gosurvey_test name label)
  cmake_parse_arguments(T "PDFIUM" "" "SOURCES;LIBS;INCLUDES" ${ARGN})
  add_executable(${name} ${T_SOURCES})
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  target_link_libraries(${name} PRIVATE ${T_LIBS} Catch2::Catch2WithMain)
  if(T_INCLUDES)
    target_include_directories(${name} PRIVATE ${T_INCLUDES})
  endif()
  gosurvey_target_warnings(${name} TEST)
  if(T_PDFIUM)
    gosurvey_copy_pdfium(${name})
    # Discover at test time: PDFium must be beside the exe before it can list its tests.
    catch_discover_tests(${name} PROPERTIES LABELS ${label} DISCOVERY_MODE PRE_TEST)
  else()
    catch_discover_tests(${name} PROPERTIES LABELS ${label})
  endif()
endfunction()

# Proves a system's public headers and link closure work without the app's private include roots.
function(gosurvey_public_consumer system lib source)
  set(name GoSurvey${system}PublicConsumer)
  add_executable(${name} ${source})
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  target_link_libraries(${name} PRIVATE ${lib})
  gosurvey_target_warnings(${name} TEST)
  add_test(NAME ${system}.public-consumer COMMAND ${name})
  set_tests_properties(${system}.public-consumer PROPERTIES LABELS ${system})
endfunction()

# --- Per-system suites --------------------------------------------------------------------------
gosurvey_test(GoSurveyCoreTests Core LIBS GoSurvey::Core SOURCES
  tests/Core/CoreTests.cpp tests/Core/NumFormatTests.cpp tests/Core/AngleFormatTests.cpp tests/Core/UnitsTests.cpp)

gosurvey_test(GoSurveyEventsTests Events LIBS GoSurvey::Events SOURCES tests/Events/EventBusTests.cpp)

gosurvey_test(GoSurveyMathTests Math LIBS GoSurvey::Math SOURCES tests/Math/Ray3dTests.cpp tests/Math/UcsTests.cpp)

gosurvey_test(GoSurveyGeometryTests Geometry LIBS GoSurvey::Geometry SOURCES
  tests/Geometry/BulgeArcTests.cpp tests/Geometry/ChamferEdgeTests.cpp tests/Geometry/CurveIntersectTests.cpp
  tests/Geometry/FilletEdgeTests.cpp tests/FilletGeomTests.cpp tests/ChamferGeomTests.cpp
  tests/Geometry/MeshGeomTests.cpp tests/Geometry/NurbsTests.cpp tests/Geometry/RevisionCloudTests.cpp)

gosurvey_test(GoSurveyDocumentTests Document LIBS GoSurvey::Document SOURCES tests/Document/MtextTextOpsTests.cpp)

gosurvey_test(GoSurveyRendererTests Renderer LIBS GoSurvey::Renderer SOURCES
  tests/Renderer/CameraTests.cpp tests/Renderer/SectionClipTests.cpp)

gosurvey_test(GoSurveyTerrainTests Terrain
  LIBS GoSurvey::Terrain GoSurvey::Debug GoSurvey::PointCloud GoSurvey::Pdf
  INCLUDES src src/util src/commands src/io/src src/survey src/renderer/include src/platform src/pdf
           src/network/include third_party
  SOURCES
    tests/Terrain/ContourGenTests.cpp tests/Terrain/DaylightTests.cpp tests/Terrain/SurfaceAnalysisTests.cpp
    tests/Terrain/SurfaceStatsTests.cpp tests/Terrain/SurfaceVolumeTests.cpp tests/Terrain/TinBorderTests.cpp
    tests/Terrain/TinBuildTests.cpp tests/Terrain/TinConstraintTests.cpp tests/Terrain/TinQueryTests.cpp
    tests/Terrain/TinVolumeTests.cpp tests/Terrain/WatershedTests.cpp tests/Terrain/Issue119SurfaceTests.cpp
    tests/Terrain/SurfaceProfileTests.cpp tests/Terrain/SurfaceStyleTests.cpp
    tests/Terrain/SurfaceRebuildLifetimeTests.cpp tests/Terrain/FeatureLineStationsTests.cpp)

gosurvey_test(GoSurveyPdfTests Pdf PDFIUM LIBS GoSurvey::Pdf INCLUDES src SOURCES
  tests/Pdf/PdfViewerTests.cpp tests/Pdf/PdfSplitTests.cpp tests/Pdf/PdfAnnotateTests.cpp
  tests/Pdf/PdfLeaderTests.cpp tests/Pdf/PdfMeasureTests.cpp tests/Pdf/PdfDimensionTests.cpp
  tests/Pdf/PdfSnapTests.cpp tests/Pdf/PdfScaleCheckTests.cpp tests/Pdf/PdfDimAuditTests.cpp
  tests/Pdf/PdfDimAuditPdfTests.cpp tests/Pdf/PdfAlignTests.cpp tests/Pdf/PdfDiffTests.cpp
  tests/Pdf/PdfCoordExtractTests.cpp)

gosurvey_test(GoSurveyDebugTests Debug
  LIBS GoSurvey::Debug GoSurvey::Geometry GoSurvey::PointCloud GoSurvey::Pdf GoSurvey::Survey
  INCLUDES src src/commands src/io/src src/util src/survey src/renderer/include src/platform src/pdf
           src/survey/include src/network/include src/terrain/include tests third_party
  SOURCES tests/Debug/BenchSceneTests.cpp tests/Debug/FrameWatchTests.cpp
          tests/Debug/DocumentSnapshotTests.cpp tests/Debug/DocumentInvariantsTests.cpp)

gosurvey_test(GoSurveyUITests UI LIBS GoSurvey::UI GoSurvey::Document SOURCES
  tests/CommandLineTests.cpp tests/MtextToolbarTests.cpp tests/RichTextLayoutTests.cpp)

gosurvey_test(GoSurveyPointCloudTests PointCloud
  LIBS GoSurvey::PointCloud GoSurvey::IOPointCloud E57Format::E57Format
  INCLUDES src third_party
  SOURCES tests/PointCloud/PointCloudOctreeTests.cpp tests/PointCloud/PointCloudCacheTests.cpp
          tests/PointCloud/CylinderFitTests.cpp)
gosurvey_enable_pch(GoSurveyPointCloudTests)

gosurvey_test(GoSurveySurveyTests Survey LIBS GoSurvey::Survey GoSurvey::Terrain INCLUDES src third_party SOURCES
  tests/Survey/PointGroupTests.cpp tests/Survey/AlignmentReportTests.cpp tests/Survey/AlignmentTests.cpp
  tests/Survey/AlignmentSampleLineTests.cpp tests/Survey/EcefTransformTests.cpp)

gosurvey_public_consumer(Renderer GoSurvey::Renderer tests/Renderer/PublicConsumer.cpp)
gosurvey_public_consumer(Document GoSurvey::Document tests/Document/PublicConsumer.cpp)
gosurvey_public_consumer(Pdf GoSurvey::Pdf tests/Pdf/PublicConsumer.cpp)
gosurvey_copy_pdfium(GoSurveyPdfPublicConsumer)
gosurvey_public_consumer(UI GoSurvey::UI tests/UI/PublicConsumer.cpp)
gosurvey_public_consumer(PointCloud GoSurvey::PointCloud tests/PointCloud/PublicConsumer.cpp)
gosurvey_public_consumer(Survey GoSurvey::Survey tests/Survey/SurveyPublicConsumer.cpp)

# --- GoSurveyTests: tests that need the command layer -------------------------------------------
add_executable(GoSurveyTests
  tests/TestSoundGuard.cpp
  # File formats
  tests/IO/AcisSatParserTests.cpp
  tests/IO/BrepJsonTests.cpp
  tests/IO/CadPointCloudE57Tests.cpp
  tests/IO/CadPointCloudLasTests.cpp
  tests/IO/CadPointCloudLazTests.cpp
  tests/IO/CadPointCloudPtsTests.cpp
  tests/IO/CadPointCloudPtxTests.cpp
  tests/IO/DwgProbeTests.cpp
  tests/IO/DxfEntityEmitTests.cpp
  tests/IO/GeoDataReadTests.cpp
  tests/IO/GltfImportTests.cpp
  tests/IO/GsAnnotationJsonTests.cpp
  tests/IO/GsIoDimAngularTests.cpp
  tests/IO/GsIoPipeRunTests.cpp
  tests/IO/GsIoViewportCameraTests.cpp
  tests/IO/GsMigrateTests.cpp
  tests/IO/LandXmlExportTests.cpp
  tests/IO/LandXmlImportTests.cpp
  tests/IO/LibreDwgCadTests.cpp
  tests/IO/LibreDwgTests.cpp
  tests/IO/PlotFontTests.cpp
  tests/IO/PlotSheetPdfTests.cpp
  tests/IO/PlotStrokeTests.cpp
  tests/IO/PointCloudExportTests.cpp
  tests/IO/ProjectFilesTests.cpp
  tests/IO/ProjectPackTests.cpp
  tests/IO/SpreadsheetIoTests.cpp
  tests/IO/StlImportTests.cpp
  tests/IO/SurveyCsvValidateTests.cpp
  tests/AlignmentDwgTests.cpp
  tests/CadBlockImportTests.cpp
  tests/MeshPayloadRoundTripTests.cpp
  # Projects and the shared point database
  tests/ProjectTests.cpp
  tests/ProjectPointDbTests.cpp
  tests/ProjectPointsSyncTests.cpp
  tests/ProjectFilesFlowTests.cpp
  tests/ProjectAddDrawingTests.cpp
  tests/ProjectTurnoverTests.cpp
  tests/Survey/ProjectPointRulesTests.cpp
  # Survey maths and coordinates
  tests/Survey/TraverseTests.cpp
  tests/Survey/CoordinateSystemsTests.cpp
  tests/Survey/AlignmentSpecTests.cpp
  tests/Survey/AlignmentProfileTests.cpp
  tests/TransformationTests.cpp
  # Geometry and the document model
  tests/BrepTests.cpp
  tests/SplineTests.cpp
  tests/HatchGeomTests.cpp
  tests/ZStoreTests.cpp
  tests/CadPipeRunTests.cpp
  tests/Document/CadBlockTests.cpp
  tests/Document/EntityIdTests.cpp
  tests/Document/PaperSpaceTests.cpp
  tests/Document/DrawingSettingsTests.cpp
  tests/DrawingContentTests.cpp
  tests/CutClipboardTests.cpp
  # Commands, updates and crash reports
  tests/CommandRegistryHandlerTests.cpp
  tests/StepEngineTests.cpp
  tests/UpdateCheckTests.cpp
  tests/CrashReporterTests.cpp)
target_include_directories(GoSurveyTests PRIVATE src/qt src/telemetry tests "${PUGIXML_INCLUDE}")
target_link_libraries(GoSurveyTests PRIVATE gosurvey_domain gosurvey_headless_seams Catch2::Catch2WithMain)
target_compile_definitions(GoSurveyTests PRIVATE
  NOMINMAX
  GOSURVEY_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/samples"
  GOSURVEY_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples"
  GOSURVEY_CSMAP_DICTIONARY_DIR="$<TARGET_FILE_DIR:GoSurveyQt>/resources/csmap")
set_source_files_properties(tests/IO/LibreDwgCadTests.cpp PROPERTIES COMPILE_OPTIONS /bigobj)
gosurvey_target_warnings(GoSurveyTests TEST)
gosurvey_enable_pch(GoSurveyTests)
gosurvey_copy_pdfium(GoSurveyTests)
gosurvey_tool_output_dir(GoSurveyTests)
# The tests read the staged CS-MAP dictionaries and bundled resources.
add_dependencies(GoSurveyTests gosurvey_stage_csmap gosurvey_stage_resources)

# Run as a few shards rather than one process per case: process start-up dominates otherwise.
# Each shard isolates its mutable paths (tools/run-catch-test-shard.ps1).
set(GOSURVEY_CATCH_TEST_SHARDS 2 CACHE STRING "Number of Catch2 shard processes for GoSurveyTests")
if(NOT GOSURVEY_CATCH_TEST_SHARDS MATCHES "^[1-9][0-9]*$" OR GOSURVEY_CATCH_TEST_SHARDS GREATER 16)
  message(FATAL_ERROR "GOSURVEY_CATCH_TEST_SHARDS must be between 1 and 16")
endif()
math(EXPR _last "${GOSURVEY_CATCH_TEST_SHARDS} - 1")
foreach(_shard RANGE 0 ${_last})
  add_test(NAME unit.catch.shard-${_shard}
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_SOURCE_DIR}/tools/run-catch-test-shard.ps1"
      -Exe "$<TARGET_FILE:GoSurveyTests>"
      -ShardIndex ${_shard} -ShardCount ${GOSURVEY_CATCH_TEST_SHARDS}
      -WorkingDirectory "$<TARGET_FILE_DIR:GoSurveyQt>"
      -OutputDirectory "${CMAKE_BINARY_DIR}/../../artifacts/catch")
  set_tests_properties(unit.catch.shard-${_shard} PROPERTIES
    TIMEOUT 900 COST 100 LABELS "Integration;Terrain;Survey;Document;Renderer;IO")
endforeach()

# --- Headless transcripts (ADR-031): one CTest case per transcript ------------------------------
if(GOSURVEY_BUILD_HEADLESS)
  file(GLOB _transcripts CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/headless/transcripts/*.txt")
  foreach(_t IN LISTS _transcripts)
    get_filename_component(_name "${_t}" NAME_WE)
    add_test(NAME headless.${_name}
      COMMAND gosurvey_headless run "${_t}" --out "${GOSURVEY_ARTIFACT_DIR}/headless/${_name}")
    set_tests_properties(headless.${_name} PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endforeach()
  # A fixed, known-clean seed range: goes red if the fuzz harness itself stops working.
  add_test(NAME headless.fuzz-smoke
    COMMAND gosurvey_headless fuzz --seeds 1..12 --out "${GOSURVEY_ARTIFACT_DIR}/headless/fuzz-smoke"
            --timeout-ms 120000)
  set_tests_properties(headless.fuzz-smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# --- Packaging checks ---------------------------------------------------------------------------
add_test(NAME build.output-layout COMMAND "${CMAKE_COMMAND}"
  "-DRUNTIME_DIR=$<TARGET_FILE_DIR:GoSurveyQt>" -P "${CMAKE_SOURCE_DIR}/tests/cmake/BuildOutputLayout.cmake")
# The deployed set launches from a clean copy and passes its smoke checks.
add_test(NAME qt.deployment
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_SOURCE_DIR}/tools/verify-qt-deployment.ps1"
    -RuntimeDirectory "$<TARGET_FILE_DIR:GoSurveyQt>" -WorkDirectory "${GOSURVEY_ARTIFACT_DIR}"
    -TimeoutSec 120)
set_tests_properties(qt.deployment PROPERTIES TIMEOUT 300)
