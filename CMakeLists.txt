cmake_minimum_required(VERSION 3.20)
project(GoSurvey VERSION 0.5.3 LANGUAGES CXX C RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Version (ADR-029 (a)) — `project(VERSION)` above is the ONE source.
#
# It names the release being worked toward, so it is bumped just after a stable
# release ships. CI passes GOSURVEY_VERSION_SUFFIX ("-beta.7") and
# GOSURVEY_VERSION_BUILD (7) for a prerelease; a stable build and a developer
# build leave both at their defaults.
#
# Both generated files land in the build tree, never the source tree (REQ-200).
# ---------------------------------------------------------------------------
set(GOSURVEY_VERSION_SUFFIX "" CACHE STRING
    "Prerelease suffix appended to the version, e.g. -beta.7 (empty for a stable build)")
set(GOSURVEY_VERSION_BUILD "0" CACHE STRING
    "Fourth field of the Windows VERSIONINFO quad; the beta number, or 0 for a stable build")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app/Version.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/Version.hpp"
  @ONLY)

# The same version, in a form Inno Setup's preprocessor can #include, so that running
# installer/GoSurvey.iss by hand picks up the CMake version instead of a stale literal.
# CI overrides these with ISCC /D defines; the script prefers the defines when present.
file(WRITE "${CMAKE_BINARY_DIR}/generated/version.iss"
"; GENERATED FILE - do not edit. Written by CMakeLists.txt from project(VERSION).\n\
#define AppVersion \"${PROJECT_VERSION}${GOSURVEY_VERSION_SUFFIX}\"\n\
#define AppVersionQuad \"${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${GOSURVEY_VERSION_BUILD}\"\n")

find_package(OpenGL REQUIRED)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

# PDFium prebuilt binaries (Windows x64).
# Update the URL to the latest release from:
#   https://github.com/bblanchon/pdfium-binaries/releases
FetchContent_Declare(pdfium_binaries
  URL https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F7857/pdfium-win-x64.tgz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Pinned to a COMMIT, not the `docking` branch: a moving branch means two clean builds of the
# same GoSurvey commit compile different ImGui, which violates REQ-200 (deterministic build).
# This SHA is v1.92.9 on `docking` (2026-07-30) — the commit already in use when the pin landed,
# so pinning changed nothing about the produced binary. ImGuizmo (REQ-060) compiles against ImGui
# internals, so a floating ImGui would turn an upstream change into a mystery build break.
# To update: bump the SHA deliberately, rebuild, and re-run the test suite.
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG 81cfcb8c1fed1e4dac8bcdc059ce811c17b6194c
)

FetchContent_Declare(glew
  URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(glfw imgui glew pdfium_binaries)

# Create an imported target for the pdfium DLL.
add_library(pdfium SHARED IMPORTED)
set_target_properties(pdfium PROPERTIES
  IMPORTED_LOCATION "${pdfium_binaries_SOURCE_DIR}/bin/pdfium.dll"
  IMPORTED_IMPLIB   "${pdfium_binaries_SOURCE_DIR}/lib/pdfium.dll.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${pdfium_binaries_SOURCE_DIR}/include"
)

set(NLOHMANN_JSON_INCLUDE "${CMAKE_BINARY_DIR}/_deps")
set(NLOHMANN_JSON_FILE "${NLOHMANN_JSON_INCLUDE}/nlohmann/json.hpp")
if(NOT EXISTS "${NLOHMANN_JSON_FILE}")
  file(MAKE_DIRECTORY "${NLOHMANN_JSON_INCLUDE}/nlohmann")
  file(DOWNLOAD
    "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
    "${NLOHMANN_JSON_FILE}"
    TLS_VERIFY ON
  )
endif()

set(GLEW_ROOT "${glew_SOURCE_DIR}")
if(EXISTS "${glew_SOURCE_DIR}/glew-2.2.0/src/glew.c")
  set(GLEW_ROOT "${glew_SOURCE_DIR}/glew-2.2.0")
endif()

add_library(glew_static STATIC ${GLEW_ROOT}/src/glew.c)
target_include_directories(glew_static PUBLIC ${GLEW_ROOT}/include)
target_compile_definitions(glew_static PUBLIC GLEW_STATIC)
target_link_libraries(glew_static PUBLIC OpenGL::GL)

# ---------------------------------------------------------------------------
# ImGui, split into core and backends (ADR-031 (c′)).
#
# The split exists because the headless target (REQ-203) needs ImGui *core* and must not have a
# window or a GPU. That sounds contradictory until you know why: loading a `.gs` measures survey
# label text through the current font and stores the result as annotation geometry
# (SurveyPoints.cpp, RepositionSurveyLabelMtextForPoint), so the domain layer genuinely needs a
# font atlas. Building an atlas is CPU work — only *uploading* it needs a GPU, and headless never
# uploads. The backends are the part that require GLFW and OpenGL, so they, and only they, are
# what the headless target leaves behind.
# ---------------------------------------------------------------------------
add_library(imgui_core STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(imgui_core PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/misc/cpp
)
target_compile_definitions(imgui_core PUBLIC
  IMGUI_DEFINE_MATH_OPERATORS
)

# The windowed application's backends. Linking this transitively brings in imgui_core and glfw, so
# `imgui_backend` still means what it meant before this split and GoSurvey's link line is unchanged.
add_library(imgui_backend STATIC
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_backend PUBLIC
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_backend PUBLIC imgui_core glfw)

# ---------------------------------------------------------------------------
# Domain sources — shared by GoSurvey and gosurvey_headless (ADR-031 (a), REQ-203).
#
# ONE list, deliberately: a source added to the application must not be silently absent from the
# headless driver, and the failure mode of forgetting is then a link error rather than a coverage
# gap nobody notices. Everything here runs with no window and no GPU.
#
# Two members are *absent* on purpose, because each target supplies its own implementation of the
# same pure header (the link-time platform seams, ADR-031 (b′)):
#   src/platform/WinFileDialogs.cpp   -> headless: src/platform/HeadlessFileDialogs.cpp
#   src/pdf/PdfAttach.cpp             -> headless: src/platform/HeadlessPdfAttach.cpp
# PdfAttach.cpp is where <GL/glew.h> reaches the Commands layer, which is precisely why it is a seam.
# ---------------------------------------------------------------------------
set(GOSURVEY_DOMAIN_SOURCES
  # Command system
  src/commands/CadCommands.cpp
  src/commands/CadLinetype.cpp
  src/commands/EntityId.cpp
  src/survey/PointGroupRule.cpp
  src/commands/CadCoordinateFrame.cpp

  # Object snapping (pure geometry; no GL despite living under viewport/)
  src/viewport/CadSnap.cpp

  # Text measurement + font registry. In the domain set because label geometry depends on metrics
  # (ADR-031 (c′)) — these need ImGui core, never a backend.
  src/ui/MtextRichFormat.cpp
  src/ui/FontRegistry.cpp
  # SHX stroke text emitter. Here because MtextRichFormat's layout calls Shx::DrawText to measure
  # SHX runs — it takes an ImDrawList but issues no GL of its own, so headless links it happily.
  src/ui/ShxDraw.cpp

  # Fonts (shared lower layer — SHX stroke geometry, ADR-022)
  src/font/ShxFont.cpp

  # Survey / COGO
  src/survey/SurveyPoints.cpp

  # Traverse engine
  src/traverse/TraverseCalc.cpp
  src/traverse/TraverseLeastSquares.cpp
  src/traverse/FbkImport.cpp

  # I/O
  src/io/GsIo.cpp
  # User preferences. In the domain set for UserDataDirectory(), which the BENCH command writes its
  # results through — pure <filesystem>, no window.
  src/io/UserPrefs.cpp
  src/io/SurveyCsv.cpp
  src/io/DxfIo.cpp
  src/io/DwgIo.cpp
  src/io/DwgProbe.cpp
  src/io/DxfColors.cpp
  src/io/DwgMeshConvert.cpp
  src/io/GsMigrate.cpp

  # Platform (OS process launch for the DWG converter — no window)
  src/platform/ProcessRun.cpp
  # Executable / user-data / bundled-asset paths. Split out of AppIcon.cpp (TASK-056) so resolving
  # %APPDATA% no longer transitively requires GLEW and GLFW.
  src/platform/AppPaths.cpp

  # Update check (REQ-077/078, ADR-029) — UpdatePrefs is reachable from AppCommandState
  src/update/UpdateCheck.cpp

  # Geometry math (util)
  src/util/geom2d.cpp
  src/util/curveintersect.cpp
  src/util/meshgeom.cpp
  src/util/gltfimport.cpp
  src/util/stlimport.cpp
  src/util/tinbuild.cpp
  src/util/benchscene.cpp

  # Document invariants (REQ-204) — pure; shared by the Catch2 suite and the headless driver
  src/util/docinvariants.cpp
)

add_executable(GoSurvey
  ${GOSURVEY_DOMAIN_SOURCES}

  # Application entry
  src/app/main.cpp

  # Viewport system
  src/viewport/CadRubberPreview.cpp
  src/viewport/TransformPreview.cpp

  # Renderer
  src/render/ViewportRenderer.cpp

  # UI panels
  src/ui/CadUi.cpp
  src/ui/CadUiSettings.cpp
  src/ui/CadUi_CopySurveyModal.cpp
  src/ui/CadUi_DxfPointConflictModal.cpp
  src/ui/CadUi_PageSetup.cpp
  src/ui/CadUi_SurveyReports.cpp
  src/ui/CadUi_ImportExportPoints.cpp
  src/ui/CadUi_TraverseEditor.cpp
  src/ui/CadUi_PointGroups.cpp
  src/ui/CadUi_Surfaces.cpp
  src/ui/CadUi_UpdateDialog.cpp
  src/ui/SplashScreen.cpp
  src/ui/ImGuiLayout.cpp
  src/ui/RichTextEdit.cpp
  src/ui/ViewCube.cpp
  src/ui/PdfAttachDialog.cpp

  # I/O (windowed application only)
  src/io/PdfPlot.cpp

  # PDF underlay — the real, GL-backed implementation (headless links a seam instead)
  src/pdf/PdfAttach.cpp

  # Platform
  src/platform/AppIcon.cpp
  src/platform/WinFileDialogs.cpp
  src/platform/GpuPreference.cpp
  src/platform/HttpFetch.cpp

  # Telemetry (REQ-080, ADR-032)
  src/telemetry/TelemetryPing.cpp
  src/telemetry/TelemetryService.cpp

  # Update check (REQ-077/078, ADR-029)
  src/update/UpdateService.cpp
)

target_include_directories(GoSurvey PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app
  ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
  ${CMAKE_CURRENT_SOURCE_DIR}/src/viewport
  ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/src/util
  ${CMAKE_CURRENT_SOURCE_DIR}/src/io
  ${CMAKE_CURRENT_SOURCE_DIR}/src/survey
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf
  ${CMAKE_CURRENT_SOURCE_DIR}/src/font
  ${CMAKE_CURRENT_SOURCE_DIR}/src/telemetry
  ${CMAKE_CURRENT_SOURCE_DIR}/src/update
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${NLOHMANN_JSON_INCLUDE}
  ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(GoSurvey PRIVATE
  imgui_backend
  glfw
  OpenGL::GL
  glew_static
  pdfium
)
if(WIN32)
  # winhttp + bcrypt: the REQ-077/078 update check's HTTPS fetch and SHA-256 (ADR-029 (e)).
  # Both ship with Windows, which is why this adds no third-party dependency under REQ-300.
  # ole32 is for the Network List Manager connectivity query in HttpFetch.cpp (REQ-077).
  target_link_libraries(GoSurvey PRIVATE comdlg32 winhttp bcrypt ole32)
  target_sources(GoSurvey PRIVATE src/platform/WinFrameControls.cpp)

  # Embed the application icon so Windows shows it in Explorer, the taskbar,
  # the Start Menu, and desktop shortcuts.
  target_sources(GoSurvey PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.rc"
  )

  # Version resource, so the shipped .exe reports its version to Explorer's Properties
  # dialog and to the installer (ADR-029 (a)). Separate from app.rc because app.rc
  # resolves "app.ico" relative to itself and cannot be moved into the build tree.
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/version.rc.in"
    "${CMAKE_BINARY_DIR}/generated/version.rc"
    @ONLY)
  target_sources(GoSurvey PRIVATE "${CMAKE_BINARY_DIR}/generated/version.rc")

  # Windows.Data.Pdf via C++/WinRT (fast hardware-accelerated thumbnail path).
  # Locate the cppwinrt headers in whichever Windows SDK version is installed.
  file(GLOB _sdk_versions LIST_DIRECTORIES true
       "C:/Program Files (x86)/Windows Kits/10/Include/10.*")
  list(SORT _sdk_versions ORDER DESCENDING)
  foreach(_sdk_ver ${_sdk_versions})
    if(EXISTS "${_sdk_ver}/cppwinrt/winrt/windows.data.pdf.h")
      target_include_directories(GoSurvey PRIVATE "${_sdk_ver}/cppwinrt")
      target_link_libraries(GoSurvey PRIVATE runtimeobject Windowscodecs)
      target_compile_definitions(GoSurvey PRIVATE PDF_WINRT_ENABLED)
      # C++/WinRT requires C++20 <coroutine> (not available in the C++17
      # experimental path used by older MSVC).  Compile only PdfAttach.cpp
      # at /std:c++20 so the rest of the project stays on C++17.
      if(MSVC)
        set_source_files_properties(src/pdf/PdfAttach.cpp PROPERTIES
          COMPILE_OPTIONS "/std:c++20")
      else()
        set_source_files_properties(src/pdf/PdfAttach.cpp PROPERTIES
          COMPILE_OPTIONS "-std=c++20")
      endif()
      message(STATUS "Windows.Data.Pdf WinRT support enabled (SDK ${_sdk_ver})")
      break()
    endif()
  endforeach()
endif()
target_compile_definitions(GoSurvey PRIVATE GLEW_STATIC)

if(WIN32)
  # Stable, version-free filename (ADR-029 (b)). A version-stamped name changes the path
  # of the installed program on every release, which breaks shortcuts, the .gs association's
  # shell\open\command, and any form of self-replacement — all three of which REQ-078 needs.
  # Existing GoSurvey-<version>.exe files are swept by [InstallDelete] in installer/GoSurvey.iss.
  set_target_properties(GoSurvey PROPERTIES OUTPUT_NAME "GoSurvey" WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(GoSurvey PRIVATE /ENTRY:mainCRTStartup)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(GoSurvey PRIVATE "LINKER:/ENTRY:mainCRTStartup")
  endif()
endif()

if(MSVC)
  target_compile_options(GoSurvey PRIVATE /W4 /permissive-)
else()
  target_compile_options(GoSurvey PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(GOSURVEY_DEFAULT_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/default-template.gs")
add_custom_command(TARGET GoSurvey POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GoSurvey>/resources"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GoSurvey>/resources/layouts"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GoSurvey>/resources/icons"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GoSurvey>/resources/hatches"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_DEFAULT_TEMPLATE}"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/default-template.gs"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/icons"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/hatches"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/hatches"
  COMMENT "Copy resources beside executable")

# Copy pdfium.dll next to the executable so it can be found at runtime.
if(WIN32)
  add_custom_command(TARGET GoSurvey POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${pdfium_binaries_SOURCE_DIR}/bin/pdfium.dll"
            "$<TARGET_FILE_DIR:GoSurvey>/pdfium.dll"
    COMMENT "Copy pdfium.dll beside executable")
endif()

# ---------------------------------------------------------------------------
# gosurvey_headless (REQ-203 / ADR-031): the Commands layer, drivable with no window.
#
# Links ${GOSURVEY_DOMAIN_SOURCES} plus the two headless platform seams, and imgui_core WITHOUT
# imgui_backend — so there is no GLFW, no GLEW, no OpenGL, and no window, while text still measures
# through the same font the application uses (ADR-031 (c′), which is what makes REQ-203's
# "save a .gs and diff against the GUI" condition meaningful rather than unmeetable).
#
# TEST-ONLY, like Catch2 (REQ-300): GoSurvey.exe neither links nor ships any of it.
# ---------------------------------------------------------------------------
option(GOSURVEY_BUILD_HEADLESS "Build the headless transcript driver (REQ-203)" ON)
if(GOSURVEY_BUILD_HEADLESS)
  add_executable(gosurvey_headless
    ${GOSURVEY_DOMAIN_SOURCES}
    src/platform/HeadlessFileDialogs.cpp
    src/platform/HeadlessPdfAttach.cpp
    tests/headless/HeadlessDriver.cpp
    tests/headless/FuzzGenerator.cpp
    tests/headless/Minimizer.cpp
    tests/headless/FuzzMain.cpp
  )

  target_include_directories(gosurvey_headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewport
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/survey
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf
    ${CMAKE_CURRENT_SOURCE_DIR}/src/font
    ${CMAKE_CURRENT_SOURCE_DIR}/src/update
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${NLOHMANN_JSON_INCLUDE}
    ${CMAKE_BINARY_DIR}/generated
  )

  # imgui_core only. Adding imgui_backend here would drag in glfw and OpenGL and silently undo the
  # property this target exists to hold.
  target_link_libraries(gosurvey_headless PRIVATE imgui_core)
  if(WIN32)
    # comdlg32 for the file-dialog header's link compatibility; ole32/shell32 for <filesystem> and
    # the shell paths ProcessRun uses. No opengl32, no glfw — verified by REQ-203's acceptance.
    target_link_libraries(gosurvey_headless PRIVATE comdlg32 ole32 shell32)
  endif()

  if(MSVC)
    target_compile_options(gosurvey_headless PRIVATE /W4 /permissive-)
  else()
    target_compile_options(gosurvey_headless PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests (ADR-002): domain regression suite via Catch2 + ctest.
# Catch2 is a TEST-ONLY dependency (decision log, REQ-300) — GoSurvey does not
# link it. The test target compiles only the pure-compute Domain sources, so it
# stays free of the UI/GL/Win32 layers.
# ---------------------------------------------------------------------------
option(GOSURVEY_BUILD_TESTS "Build the GoSurvey domain test suite" ON)
if(GOSURVEY_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
  )
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")

  enable_testing()
  add_executable(GoSurveyTests
    tests/TraverseTests.cpp
    tests/NumFormatTests.cpp
    tests/AngleFormatTests.cpp
    tests/PaperSpaceTests.cpp
    tests/CommandLineTests.cpp
    tests/RightClickTests.cpp
    tests/SurveyCsvValidateTests.cpp
    # REQ-083: the save dialog's default-extension rule. Header-only and pure, because
    # WinFileDialogs.cpp is Win32-only and cannot be linked here.
    tests/PointFileExtTests.cpp
    tests/HatchGeomTests.cpp
    tests/ZStoreTests.cpp
    tests/Ray3dTests.cpp
    tests/CameraTests.cpp
    tests/TextStyleTests.cpp
    tests/OrthoConstrainTests.cpp
    tests/ColorContrastTests.cpp
    tests/PlotFontTests.cpp
    tests/MtextToolbarTests.cpp
    tests/RichTextLayoutTests.cpp
    tests/MtextTextOpsTests.cpp
    tests/DxfEntityEmitTests.cpp
    tests/DwgProbeTests.cpp
    tests/LinetypeTessellationTests.cpp
    tests/CurveIntersectTests.cpp
    tests/BenchSceneTests.cpp
    tests/VisualStyleTests.cpp
    tests/MeshGeomTests.cpp
    tests/MeshGsRoundTripTests.cpp
    tests/GltfImportTests.cpp
    tests/StlImportTests.cpp
    tests/EntityIdTests.cpp
    tests/PointGroupTests.cpp
    tests/TinBuildTests.cpp
    tests/TinQueryTests.cpp
    tests/TinConstraintTests.cpp
    tests/UpdateCheckTests.cpp
    tests/TelemetryPingTests.cpp
    tests/GsMigrateTests.cpp
    tests/GpuPreferenceTests.cpp
    # REQ-204: one deliberately-broken fixture per document invariant. This is the second present-day
    # concrete use of util/docinvariants (the headless driver is the first), which is what lets the
    # module exist at all under architecture §11.4.
    tests/DocInvariantsTests.cpp
    src/util/docinvariants.cpp
    # REQ-204 generator + minimizer. Both are pure — the generator takes its command list as a
    # parameter, the minimizer takes a predicate — so reproducibility, termination and the attempt
    # cap are testable here with no process launch and no GL.
    tests/FuzzHarnessTests.cpp
    tests/headless/FuzzGenerator.cpp
    tests/headless/Minimizer.cpp
    # BUG-013 per-application GPU preference. Win32 registry only — no GL, no window — so the value
    # the user's choice actually turns into is testable without the application running.
    src/platform/GpuPreference.cpp
    src/traverse/TraverseCalc.cpp
    src/traverse/TraverseLeastSquares.cpp
    src/traverse/FbkImport.cpp
    # DWG version detection + converter discovery. Deliberately its own TU (see DwgProbe.cpp):
    # it depends only on <filesystem>, so tests link it without DxfIo and the GUI stack.
    src/io/DwgProbe.cpp
    # Linetype dash tessellation, including per-vertex Z (REQ-058). Standalone like DwgProbe.cpp —
    # only <string>/<vector>/<cmath>/<cctype> — so it links without the GUI stack.
    src/commands/CadLinetype.cpp
    # Intersection math behind the INT/APPINT snaps (REQ-062). Pure and dependency-free on purpose:
    # this is where REQ-101 is actually enforced, and it must be testable without a GL context.
    src/util/curveintersect.cpp
    # REQ-100 bench scene generator + frame statistics. Pure and deterministic by design.
    src/util/benchscene.cpp
    # REQ-063 mesh validation + bounds + normals. Pure; the GPU-facing checks must be testable.
    src/util/meshgeom.cpp
    # REQ-065 glTF reader. Pure and GL-free by design (ADR-026 (d)) so it is tested without a window.
    src/util/gltfimport.cpp
    src/util/stlimport.cpp
    # REQ-076 stable entity identity. Standalone like DwgProbe.cpp — only <cstdint>/<vector> and the
    # POD attribute struct — so the never-reuse and resolve-to-nothing rules are tested without the
    # GUI stack that AppCommandState drags in.
    src/commands/EntityId.cpp
    # REQ-067 point-group membership rules. Pure like EntityId.cpp: the union-vs-intersection
    # semantics are a recorded user decision and must be testable without the GUI stack.
    src/survey/PointGroupRule.cpp
    # REQ-068 Delaunay triangulation. Pure by ADR-028 (c); its double-precision predicates are the
    # part a float build gets silently wrong, so they must be pinned without a GL context.
    src/util/tinbuild.cpp
    # REQ-077/078 version ordering + manifest parse. Pure by ADR-029 (g) — no network, no window.
    # Wrong version ordering has NO symptom (the user is simply never offered an update), which is
    # exactly why it must be pinned by a test rather than noticed in use.
    src/update/UpdateCheck.cpp
    # REQ-079 .gs forward migration. Pure by ADR-030 (d) — JSON in, JSON out — so the composition
    # logic is testable without the command layer that GsIo.cpp drags in.
    src/io/GsMigrate.cpp
    # REQ-080 telemetry. Pure by ADR-032 (b) — payload building, event decision, rate limiting —
    # no network, so the timing logic is testable without HttpFetch or WinHTTP.
    src/telemetry/TelemetryPing.cpp
  )
  target_include_directories(GoSurveyTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/traverse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    # For the REQ-063 `.gs` round-trip test: GsIo.cpp itself is not linkable here (it pulls the
    # whole command layer), but the serializer it relies on is, and that is where the precision
    # risk actually lives.
    ${NLOHMANN_JSON_INCLUDE}
    # REQ-204 (DocInvariantsTests): CadCommands.hpp's header closure. Headers only — the test target
    # still links no command, UI or GL translation unit, which is the property ADR-002 protects.
    # AppCommandState is plain data, so constructing one costs nothing but these include paths.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
    ${CMAKE_CURRENT_SOURCE_DIR}/src/survey
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf
    ${CMAKE_CURRENT_SOURCE_DIR}/src/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/src/update
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    # For FuzzHarnessTests, which includes headless/FuzzGenerator.hpp and headless/Minimizer.hpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
  )
  target_compile_definitions(GoSurveyTests PRIVATE
    GOSURVEY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples"
  )
  target_link_libraries(GoSurveyTests PRIVATE Catch2::Catch2WithMain)
  if(MSVC)
    target_compile_options(GoSurveyTests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(GoSurveyTests PRIVATE -Wall -Wextra)
  endif()

  include(Catch)
  catch_discover_tests(GoSurveyTests)

  # REQ-203: the transcript corpus runs on every push and a non-zero exit fails the build (REQ-202).
  # Registered here, inside GOSURVEY_BUILD_TESTS, because enable_testing() lives in this block —
  # each transcript becomes its own ctest case so a failure names the transcript that failed.
  if(GOSURVEY_BUILD_HEADLESS)
    file(GLOB GOSURVEY_TRANSCRIPTS
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless/transcripts/*.txt")
    foreach(_t ${GOSURVEY_TRANSCRIPTS})
      get_filename_component(_name "${_t}" NAME_WE)
      add_test(NAME headless.${_name}
               COMMAND gosurvey_headless run "${_t}"
                       --out "${CMAKE_BINARY_DIR}/headless-out/${_name}")
      # Transcripts write only under the build tree (CON-07, REQ-200) and must not depend on the
      # directory ctest happens to start them in.
      set_tests_properties(headless.${_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()

    # Oracles that currently FAIL, held DISABLED rather than deleted or WILL_FAIL'ed.
    #
    # Same call TASK-056 made for `headless.gs-roundtrip.compare` while #56 and #57 were open, and
    # for the same reason: an oracle that has found a real defect is EVIDENCE, and deleting it to
    # get a green suite throws the evidence away. WILL_FAIL is worse than either — it goes green the
    # moment the failure turns into a DIFFERENT failure, which is the one outcome nobody wants to
    # miss. Each file's header says what it found; re-enable when that is fixed.
    #
    #   dxf-export-stable              — #63: the exporter has no ARC or ELLIPSE branch (silent data
    #                                    loss). #64: a LWPOLYLINE comes back from a round trip as
    #                                    loose LINEs.
    foreach(_disabled dxf-export-stable)
      if(TEST headless.${_disabled})
        set_tests_properties(headless.${_disabled} PROPERTIES DISABLED TRUE)
      endif()
    endforeach()

    # REQ-204: a small, fixed fuzz sweep on every push. Not a bug hunt — 12 seeds find nothing that
    # 5000 seeds did not — but a canary for the HARNESS: if generation stops producing commands, the
    # subprocess runner breaks, or the seed range stops being deterministic, this goes red instead of
    # the fuzzer quietly reporting "0 failures" forever, which looks exactly like success.
    #
    # The seeds are pinned and known-clean at the time of writing. A future real failure here is a
    # genuine finding and should be triaged, not silenced by moving the range.
    add_test(NAME headless.fuzz-smoke
             COMMAND gosurvey_headless fuzz --seeds 1..12
                     --out "${CMAKE_BINARY_DIR}/headless-out/fuzz-smoke"
                     --timeout-ms 30000)
    set_tests_properties(headless.fuzz-smoke PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    # The `gs-roundtrip` comparison was briefly a separate, DISABLED ctest case here while issues
    # #56 and #57 were open. Both are fixed, so the check is live again and now lives inside the
    # transcript itself as `EXPECT SAMEFILE` — one artifact instead of two, and it needs no
    # knowledge of the build layout. Disabling it was the right call at the time (deleting would
    # have lost the oracle; WILL_FAIL would have turned the fix into a red build), and re-enabling
    # it is the regression test for both issues.
  endif()
endif()
