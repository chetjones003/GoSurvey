cmake_minimum_required(VERSION 3.20)
project(GoSurvey VERSION 0.5.6 LANGUAGES CXX C RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# sccache cannot hash MSVC's default /Zi PDB. CI passes CMAKE_CXX_COMPILER_LAUNCHER=sccache;
# local builds leave the launcher unset and keep the default debug format.
if(MSVC AND CMAKE_CXX_COMPILER_LAUNCHER)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()

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

# REQ-205 / D-2026-08-31-a (issue #142): a per-target precompiled header for the
# stable heavy headers. A build-time device only — see cmake/gosurvey_pch.hpp.
# Turn OFF to prove every TU still compiles without it (portability / REQ-200).
option(GOSURVEY_USE_PCH
  "Precompiled header for stable heavy headers (REQ-205). OFF = determinism/portability check." ON)
function(gosurvey_enable_pch tgt)
  if(GOSURVEY_USE_PCH)
    target_precompile_headers(${tgt} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gosurvey_pch.hpp")
  endif()
endfunction()

# Warning configuration for our own targets. /W4 + /permissive- stay on; a small
# set of /W4-tier warnings is disabled because this codebase has a deliberate,
# consistent convention that fires them by design — see spec/coding-standards.md §11.
#   4244/4305  double->float narrowing. Geometry is STORED in float on purpose;
#              REQ-101's rule is that the world->local subtraction happens in
#              double BEFORE the narrowing, which these sites already do.
#   4267       size_t -> 32-bit at Win32 / ImGui / third-party API boundaries.
#   4245       signed/unsigned mismatch on Win32 API constants.
#   4456/4457/4458/4459  local/param/member/global shadowing — style, not a bug.
# _CRT_SECURE_NO_WARNINGS silences C4996 for standard C functions (getenv, strncpy).
function(gosurvey_target_warnings tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE
      /W4 /permissive-
      /wd4244 /wd4305 /wd4267 /wd4245
      /wd4456 /wd4457 /wd4458 /wd4459)
    target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${tgt} PRIVATE
      -Wall -Wextra
      -Wno-shadow -Wno-conversion -Wno-float-conversion -Wno-sign-conversion)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Dependencies (D-2026-08-31-b, GitHub issue #142).
#
# Every build dependency is VENDORED in third_party/ — no FetchContent, no
# network, no per-configure git clone or dependency configure. Each subtree has
# a VENDORED.md with its upstream URL + pinned tag/commit + rebuild recipe.
# REQ-200 is strengthened: the dependency bytes are in the tree, not a ref a
# host resolves. To update one: replace its files and bump its VENDORED.md.
# ---------------------------------------------------------------------------
set(GOSURVEY_TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# The old FetchContent variable names are reused so the target definitions below
# stay untouched — only the source of the bytes changed.
set(imgui_SOURCE_DIR              "${GOSURVEY_TP}/imgui")
set(imgui_test_engine_SOURCE_DIR  "${GOSURVEY_TP}/imgui_test_engine")
set(pdfium_binaries_SOURCE_DIR    "${GOSURVEY_TP}/pdfium")
set(GLEW_ROOT                     "${GOSURVEY_TP}/glew")
set(NLOHMANN_JSON_INCLUDE         "${GOSURVEY_TP}")   # resolves <nlohmann/json.hpp>

# REQ-161 / ADR-040: Developer Shell is compile-gated. Release cannot turn it on.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_gosurvey_devshell_default ON)
else()
  set(_gosurvey_devshell_default OFF)
endif()
option(GOSURVEY_DEVELOPER_SHELL
  "Debug-only Developer Shell + Dear ImGui Test Engine (REQ-161). Forced off for Release."
  ${_gosurvey_devshell_default})
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND GOSURVEY_DEVELOPER_SHELL)
  message(WARNING "REQ-161: GOSURVEY_DEVELOPER_SHELL forced OFF for CMAKE_BUILD_TYPE=Release")
  set(GOSURVEY_DEVELOPER_SHELL OFF CACHE BOOL "" FORCE)
endif()
if(GOSURVEY_DEVELOPER_SHELL AND NOT EXISTS "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp")
  message(FATAL_ERROR
    "GOSURVEY_DEVELOPER_SHELL is ON but third_party/imgui_test_engine/ is not populated. "
    "See third_party/imgui_test_engine/VENDORED.md to vendor it (Debug-only dep).")
endif()

# --- GLFW: vendored 3.4 source, built in-tree (GLFW_BUILD_* off) ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${GOSURVEY_TP}/glfw" "${CMAKE_BINARY_DIR}/glfw-build" EXCLUDE_FROM_ALL)

# --- LibreDWG: vendored headers + prebuilt static lib (REQ-170 / ADR-041) ---
# The 0.13.3 tag's headers (public + the src/*.h that LibreDwgCad.cpp includes:
# bits.h, out_dxf.h, …) and the generated config.h, plus a prebuilt MSVC x64
# Release /MD static lib. Building LibreDWG from source cost ~30 try_compile
# probes + ~400 s of C compile on every clean build (issue #142 §5).
set(libredwg_SOURCE_DIR "${GOSURVEY_TP}/libredwg")
set(libredwg_BINARY_DIR "${GOSURVEY_TP}/libredwg")   # config.h lives beside src/*.h
add_library(libredwg STATIC IMPORTED GLOBAL)
set_target_properties(libredwg PROPERTIES
  IMPORTED_LOCATION "${GOSURVEY_TP}/libredwg/lib/win-x64/libredwg.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${GOSURVEY_TP}/libredwg/include")
if(WIN32)
  set_property(TARGET libredwg APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32)
endif()
set_property(TARGET libredwg APPEND PROPERTY
  INTERFACE_COMPILE_DEFINITIONS "GOSURVEY_LIBREDWG_VERSION=\"0.13.3\"")
add_library(LibreDWG::libredwg ALIAS libredwg)
message(STATUS "LibreDWG 0.13.3 vendored (prebuilt third_party/libredwg/lib/win-x64/libredwg.lib)")

# --- pdfium: vendored prebuilt (chromium/7857, Windows x64) ---
add_library(pdfium SHARED IMPORTED)
set_target_properties(pdfium PROPERTIES
  IMPORTED_LOCATION "${pdfium_binaries_SOURCE_DIR}/bin/pdfium.dll"
  IMPORTED_IMPLIB   "${pdfium_binaries_SOURCE_DIR}/lib/pdfium.dll.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${pdfium_binaries_SOURCE_DIR}/include"
)

add_library(glew_static STATIC ${GLEW_ROOT}/src/glew.c)
target_include_directories(glew_static PUBLIC ${GLEW_ROOT}/include)
target_compile_definitions(glew_static PUBLIC GLEW_STATIC)
target_link_libraries(glew_static PUBLIC OpenGL::GL)
# Vendored: silence glew.c's warnings so ours stay visible (same call as LibreDWG).
if(MSVC)
  target_compile_options(glew_static PRIVATE /w)
else()
  target_compile_options(glew_static PRIVATE -w)
endif()

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

if(GOSURVEY_DEVELOPER_SHELL)
  set(_IMGUI_TE "${imgui_test_engine_SOURCE_DIR}/imgui_test_engine")
  add_library(imgui_core_testengine STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${_IMGUI_TE}/imgui_te_engine.cpp
    ${_IMGUI_TE}/imgui_te_context.cpp
    ${_IMGUI_TE}/imgui_te_coroutine.cpp
    ${_IMGUI_TE}/imgui_te_exporters.cpp
    ${_IMGUI_TE}/imgui_te_perftool.cpp
    ${_IMGUI_TE}/imgui_te_ui.cpp
    ${_IMGUI_TE}/imgui_te_utils.cpp
    ${_IMGUI_TE}/imgui_capture_tool.cpp
  )
  target_include_directories(imgui_core_testengine PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/misc/cpp
    ${_IMGUI_TE}
    ${imgui_test_engine_SOURCE_DIR}
    ${_IMGUI_TE}/thirdparty/Str
  )
  target_compile_definitions(imgui_core_testengine PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
    IMGUI_ENABLE_TEST_ENGINE
    IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
  )
  if(MSVC)
    target_compile_options(imgui_core_testengine PRIVATE /W3)
  endif()
endif()

# Backends need imgui headers; the executable links one imgui_core* implementation (ADR-040).
add_library(imgui_backend STATIC
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_backend PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui_backend PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(imgui_backend PUBLIC glfw)

# ---------------------------------------------------------------------------
# Domain sources — shared by GoSurvey, gosurvey_headless, and GoSurveySnapTests
# (ADR-031 (a), REQ-203).
#
# ONE list, deliberately: a source added to the application must not be silently absent from the
# headless driver, and the failure mode of forgetting is then a link error rather than a coverage
# gap nobody notices. Everything here runs with no window and no GPU.
# Compiled once as gosurvey_domain (below); the three executables link that library.
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
  # Cohesive slices split out of the CadCommands.cpp mega-TU (TASK-150 Phase 2,
  # issue #142). Same TU rules; entry points declared in CadCommands.hpp.
  src/commands/CadCommands_Align.cpp
  src/commands/CadCommands_Ucs.cpp
  src/commands/CadCommands_Bench.cpp
  src/commands/CadBlocks.cpp
  src/commands/CadLinetype.cpp
  src/commands/EntityId.cpp
  src/survey/PointGroupRule.cpp
  src/commands/CadCoordinateFrame.cpp

  # Object snapping (pure geometry; no GL despite living under viewport/)
  src/viewport/CadSnap.cpp

  # Preview geometry (pure geometry; no GL, same as CadSnap.cpp above — it builds vertex lists, it
  # does not draw them). Moved into the shared list by TASK-101 so the headless driver can assert
  # what a preview PROMISES: BREAK's preview says which half of an object is about to disappear,
  # and on a closed entity that answer depends on click order, so it can be confidently wrong.
  src/viewport/TransformPreview.cpp
  # Draft-command rubber (pure geometry; no GL — same rationale as TransformPreview.cpp above).
  # Moved into the shared list by TASK-146 so the INSERT block-ghost preview can be asserted
  # headlessly (REQ-107, D-2026-08-29-i).
  src/viewport/CadRubberPreview.cpp

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
  # REQ-308 recent-drawings MRU store (D-2026-08-30-b). Pure — <filesystem>/<fstream>/json, path
  # and clock passed in by the caller — so GoSurveyTests links it directly too (see that target).
  src/io/RecentDrawings.cpp
  src/io/SurveyCsv.cpp
  src/io/DxfIo.cpp
  src/io/DwgIo.cpp
  src/io/DwgProbe.cpp
  src/io/LibreDwg.cpp
  src/io/LibreDwgCad.cpp
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
  src/util/contourgen.cpp
  src/util/surfaceanalysis.cpp
  src/util/surfacevolume.cpp
  src/util/surfacestats.cpp
  src/util/watershed.cpp
  src/util/tinvolume.cpp
  src/util/surfacequery.cpp
  src/util/gridsurface.cpp
  src/util/benchscene.cpp
  # REQ-313 B-rep solid kernel (ADR-045, GitHub issue #146). Pure by that ADR — no GL, no ImGui, no
  # document — so a wrong volume integral or an unclosed shell is caught against a closed-form
  # expectation rather than by looking at a shaded viewport.
  src/util/brep.cpp
  # REQ-320 / ADR-051: the ACIS SAT record parser feeding 3DSOLID import (GitHub issue #299). Pure
  # and dependency-free like brep.cpp itself — no LibreDWG, no GL — so a malformed or unsupported
  # record is caught against a hand-authored SAT fixture rather than a real vendor block library.
  src/util/AcisSatParser.cpp
  # REQ-315 / ADR-048: the freeform (NURBS) surface patch — pure math (evaluation, derivatives,
  # validation, the loft builders), no GL and no brep coupling, so its correctness is pinned against
  # closed-form circle / plane expectations rather than a viewport.
  src/util/nurbs.cpp
  # REQ-318 sub-object picking (ADR-049, GitHub issue #148). Pure for the same reason the kernel
  # above it is: the precedence between a face, an edge and a vertex, and the refusal to report a
  # pick it cannot justify, are decided without a window.
  src/util/solidpick.cpp

  # Document invariants (REQ-204) — pure; shared by the Catch2 suite and the headless driver
  src/util/docinvariants.cpp
)

# One static library so CadCommands.cpp (and the rest of the domain list) is compiled
# once per configuration rather than once per executable that needs it.
add_library(gosurvey_domain STATIC ${GOSURVEY_DOMAIN_SOURCES})
target_include_directories(gosurvey_domain PUBLIC
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
target_link_libraries(gosurvey_domain PUBLIC imgui_core LibreDWG::libredwg)
if(WIN32)
  # comdlg32: file-dialog header link compatibility. ole32/shell32: <filesystem> and
  # ProcessRun's shell paths. No opengl32, no glfw — the windowed app adds those itself.
  target_link_libraries(gosurvey_domain PUBLIC comdlg32 ole32 shell32)
endif()
gosurvey_target_warnings(gosurvey_domain)
gosurvey_enable_pch(gosurvey_domain)

add_executable(GoSurvey
  # Application entry
  src/app/main.cpp

  # Viewport system (CadRubberPreview.cpp / TransformPreview.cpp are in GOSURVEY_DOMAIN_SOURCES —
  # no GL, headless-testable)

  # Renderer
  src/render/ViewportRenderer.cpp

  # UI panels
  src/ui/CadUi.cpp
  # Cohesive panels split out of the CadUi.cpp mega-TU (TASK-150 Phase 2, issue
  # #142). Shared helpers go through src/ui/CadUiInternal.hpp.
  src/ui/CadUi_Modals.cpp
  src/ui/CadUiSettings.cpp
  src/ui/CadUi_CopySurveyModal.cpp
  src/ui/CadUi_DxfPointConflictModal.cpp
  src/ui/CadUi_PageSetup.cpp
  src/ui/CadUi_SurveyReports.cpp
  src/ui/CadUi_ImportExportPoints.cpp
  src/ui/CadUi_TraverseEditor.cpp
  src/ui/CadUi_PointGroups.cpp
  src/ui/CadUi_Surfaces.cpp
  src/ui/CadUi_SurfaceStyles.cpp
  src/ui/CadUi_Toolspace.cpp
  src/ui/CadUi_SurfaceProperties.cpp
  src/ui/CadUi_VolumeDashboard.cpp
  src/ui/CadUi_QuickProfile.cpp
  src/ui/CadUi_FeatureLineElev.cpp
  src/ui/CadUi_UpdateDialog.cpp
  src/ui/CadUi_SignInGate.cpp
  src/ui/CadUi_StartScreen.cpp
  src/ui/SplashScreen.cpp
  src/ui/ImGuiLayout.cpp
  src/ui/RichTextEdit.cpp
  src/ui/ViewCube.cpp
  src/ui/UcsIcon.cpp
  src/ui/PdfAttachDialog.cpp
  src/ui/CadUi_InsertBlock.cpp
  src/ui/CadUi_BlockAuthoring.cpp

  # I/O (windowed application only)
  src/io/PdfPlot.cpp

  # PDF underlay — the real, GL-backed implementation (headless links a seam instead)
  src/pdf/PdfAttach.cpp

  # Platform
  src/platform/AppIcon.cpp
  src/platform/WinFileDialogs.cpp
  src/platform/GpuPreference.cpp
  src/platform/HttpFetch.cpp
  src/platform/OAuthListener.cpp
  src/platform/CredentialStore.cpp

  # Telemetry (REQ-080, ADR-032)
  src/telemetry/TelemetryPing.cpp
  src/telemetry/TelemetryService.cpp

  # Accounts sign-in (REQ-091, ADR-037)
  src/auth/AuthPing.cpp
  src/auth/AuthService.cpp

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
  ${CMAKE_CURRENT_SOURCE_DIR}/src/auth
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
  LibreDWG::libredwg
)
target_include_directories(GoSurvey PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/devshell)
if(GOSURVEY_DEVELOPER_SHELL)
  # Domain objects must share the Test Engine ImGui ABI (ADR-040). Do not link gosurvey_domain
  # (compiled against imgui_core without hooks) into the Debug windowed exe.
  target_sources(GoSurvey PRIVATE ${GOSURVEY_DOMAIN_SOURCES})
  target_sources(GoSurvey PRIVATE
    src/devshell/DevShell.cpp
    src/devshell/DevShellTests.cpp
  )
  target_compile_definitions(GoSurvey PRIVATE GOSURVEY_DEVELOPER_SHELL)
  target_link_libraries(GoSurvey PRIVATE imgui_core_testengine)
else()
  target_link_libraries(GoSurvey PRIVATE gosurvey_domain)
endif()

if(TARGET libredwg)
  set_property(SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/LibreDwgCad.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io/LibreDwg.cpp"
    APPEND PROPERTY INCLUDE_DIRECTORIES
    "${libredwg_SOURCE_DIR}/src"
    "${libredwg_BINARY_DIR}/src")
endif()
if(WIN32)
  # winhttp + bcrypt: the REQ-077/078 update check's HTTPS fetch and SHA-256 (ADR-029 (e)).
  # Both ship with Windows, which is why this adds no third-party dependency under REQ-300.
  # ole32 is for the Network List Manager connectivity query in HttpFetch.cpp (REQ-077).
  # ws2_32 is the REQ-091 loopback OAuth listener's raw Winsock socket calls (ADR-037 (b));
  # advapi32 is Windows Credential Manager (ADR-037 (c)). Both ship with Windows.
  target_link_libraries(GoSurvey PRIVATE comdlg32 winhttp bcrypt ole32 ws2_32 advapi32)
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
  # of the installed program on every release, which breaks shortcuts and any form of
  # self-replacement — both of which REQ-078 needs.
  # Existing GoSurvey-<version>.exe files are swept by [InstallDelete] in installer/GoSurvey.iss.
  set_target_properties(GoSurvey PROPERTIES OUTPUT_NAME "GoSurvey" WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(GoSurvey PRIVATE /ENTRY:mainCRTStartup)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(GoSurvey PRIVATE "LINKER:/ENTRY:mainCRTStartup")
  endif()
endif()

gosurvey_target_warnings(GoSurvey)
gosurvey_enable_pch(GoSurvey)
# PdfAttach.cpp is compiled at /std:c++20 (C++/WinRT); the PCH is built at the
# project default (C++17), so it cannot consume it (D-2026-08-31-a).
set_source_files_properties(src/pdf/PdfAttach.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

set(GOSURVEY_DEFAULT_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/default-template.gst")
add_custom_command(TARGET GoSurvey POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GoSurvey>/resources"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GoSurvey>/resources/layouts"
  # `copy_directory` never removes files that were deleted or renamed in the source tree, so a
  # rebuild after dropping an old icon left the stale copy beside the exe (and in the installer
  # payload) forever. Wipe the mirrored dirs first so the deploy is an exact copy of source.
  COMMAND ${CMAKE_COMMAND} -E rm -rf
          "$<TARGET_FILE_DIR:GoSurvey>/resources/icons"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/hatches"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/blocks"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_DEFAULT_TEMPLATE}"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/default-template.gst"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/icons"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/hatches"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/hatches"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/blocks"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/blocks"
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
# Links gosurvey_domain plus the two headless platform seams, and imgui_core (via the domain
# library) WITHOUT imgui_backend — so there is no GLFW, no GLEW, no OpenGL, and no window, while
# text still measures through the same font the application uses (ADR-031 (c′), which is what
# makes REQ-203's "save a .gs and diff against the GUI" condition meaningful rather than unmeetable).
#
# TEST-ONLY, like Catch2 (REQ-300): GoSurvey.exe neither links nor ships any of it.
# ---------------------------------------------------------------------------
option(GOSURVEY_BUILD_HEADLESS "Build the headless transcript driver (REQ-203)" ON)
if(GOSURVEY_BUILD_HEADLESS)
  add_executable(gosurvey_headless
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

  # gosurvey_domain brings imgui_core. Adding imgui_backend here would drag in glfw and OpenGL
  # and silently undo the property this target exists to hold. LibreDWG::libredwg is linked
  # explicitly because gosurvey_domain compiles LibreDwgCad.cpp without exposing the lib.
  target_link_libraries(gosurvey_headless PRIVATE gosurvey_domain LibreDWG::libredwg)

  # %DATA% in a transcript resolves to this, so a fixture is named relative to the repository
  # rather than to whoever's checkout the transcript was written in.
  target_compile_definitions(gosurvey_headless PRIVATE
    GOSURVEY_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples")

  gosurvey_target_warnings(gosurvey_headless)

  # GsJsonDwgFixture (issue #264): converts between raw GoSurvey JSON (the .gst template shape,
  # REQ-175) and a .dwg carrying it as the ADR-044 trailer. Used by tools/Make-*.ps1 to author
  # samples/*.dwg test fixtures from readable JSON — not shipped, like gosurvey_headless.
  add_executable(GsJsonDwgFixture
    tools/GsJsonDwgFixture.cpp
    src/platform/HeadlessFileDialogs.cpp
    src/platform/HeadlessPdfAttach.cpp
  )
  target_include_directories(GsJsonDwgFixture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/src/survey
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/font
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${NLOHMANN_JSON_INCLUDE}
    ${CMAKE_BINARY_DIR}/generated
  )
  target_link_libraries(GsJsonDwgFixture PRIVATE gosurvey_domain LibreDWG::libredwg)
endif()

# ---------------------------------------------------------------------------
# Tests (ADR-002): domain regression suite via Catch2 + ctest.
# Catch2 is a TEST-ONLY dependency (decision log, REQ-300) — GoSurvey does not
# link it. The test target compiles only the pure-compute Domain sources, so it
# stays free of the UI/GL/Win32 layers.
# ---------------------------------------------------------------------------
option(GOSURVEY_BUILD_TESTS "Build the GoSurvey domain test suite" ON)
if(GOSURVEY_BUILD_TESTS)
  # Catch2 v3.5.2 — vendored AMALGAMATED distribution (D-2026-08-31-b): one .hpp +
  # one .cpp instead of the whole Catch2 CMake project. `Catch2::Catch2WithMain`
  # is our own tiny static lib built from catch_amalgamated.cpp (which defines
  # main()); ctest integration uses the vendored Catch.cmake / CatchAddTests.cmake.
  set(GOSURVEY_CATCH2_DIR "${GOSURVEY_TP}/catch2")
  add_library(Catch2_amalgamated STATIC "${GOSURVEY_CATCH2_DIR}/catch_amalgamated.cpp")
  target_include_directories(Catch2_amalgamated PUBLIC "${GOSURVEY_CATCH2_DIR}")
  target_compile_features(Catch2_amalgamated PUBLIC cxx_std_17)
  if(MSVC)
    target_compile_options(Catch2_amalgamated PRIVATE /w)
  endif()
  add_library(Catch2::Catch2WithMain ALIAS Catch2_amalgamated)
  list(APPEND CMAKE_MODULE_PATH "${GOSURVEY_CATCH2_DIR}")

  enable_testing()
  add_executable(GoSurveyTests
    tests/TraverseTests.cpp
    tests/NumFormatTests.cpp
    tests/AngleFormatTests.cpp
    tests/PaperSpaceTests.cpp
    tests/DimViewportTests.cpp
    tests/ViewportCameraTests.cpp
    tests/CommandLineTests.cpp
    tests/RightClickTests.cpp
    tests/SurveyCsvValidateTests.cpp
    # REQ-083: the save dialog's default-extension rule. Header-only and pure, because
    # WinFileDialogs.cpp is Win32-only and cannot be linked here.
    tests/PointFileExtTests.cpp
    tests/HatchGeomTests.cpp
    tests/ZStoreTests.cpp
    tests/UcsTests.cpp
    tests/Ray3dTests.cpp
    tests/CameraTests.cpp
    # REQ-310 3D crosshair (GitHub #144). Header-only and pure for the same reason Camera is: an
    # axis arm drawn along the wrong screen direction still looks like a 3D cursor, so the sign
    # conventions need a test that does not depend on a window.
    tests/Crosshair3dTests.cpp
    # REQ-122 zoom framing (GitHub issue #88, TASK-117). Header-only and pure, like OrthoConstrain
    # above it: the ZOOMEXTENTS acceptance list (margin, aspect, no edge clipping, degenerate extents,
    # never a NaN camera) is decided entirely in this arithmetic, and the deferred consumer that calls
    # it cannot be reached from any test target (TASK-113 DEBT-1).
    tests/ZoomFramingTests.cpp
    tests/TextStyleTests.cpp
    tests/OrthoConstrainTests.cpp
    tests/ColorContrastTests.cpp
    tests/PlotFontTests.cpp
    tests/CadFontNameTests.cpp
    tests/MtextToolbarTests.cpp
    tests/RichTextLayoutTests.cpp
    tests/MtextTextOpsTests.cpp
    tests/DxfEntityEmitTests.cpp
    tests/DwgProbeTests.cpp
    tests/LibreDwgTests.cpp
    tests/LinetypeTessellationTests.cpp
    tests/CurveIntersectTests.cpp
    tests/BenchSceneTests.cpp
    tests/VisualStyleTests.cpp
    tests/MeshGeomTests.cpp
    # REQ-313 B-rep solid kernel (ADR-045, GitHub issue #146). The whole point of the kernel being
    # graphics-free is that this suite can exist: every primitive's topology, its closed-form volume
    # and surface area, its refusals, and its behaviour at state-plane coordinate magnitudes are all
    # decided here, with no window and no document.
    tests/BrepTests.cpp
    # REQ-319 / ADR-046 amendment (i) — push/pull, the first kernel op that EDITS a solid. Its own
    # file because its subject is the PRECONDITION Validate cannot enforce, not another builder.
    tests/PushPullTests.cpp
    # REQ-320 / ADR-051 — the ACIS SAT parser (GitHub issue #299). Pure, so a hand-authored SAT
    # fixture pins the supported plane/cylinder/cone shapes and every refusal without a DWG file.
    tests/AcisSatParserTests.cpp
    src/util/AcisSatParser.cpp
    src/util/brep.cpp
    # REQ-315 / ADR-048: the NURBS patch math. Partition-of-unity, a bilinear patch reproducing a
    # plane, a rational arc row that is exactly on its circle, and derivative agreement with finite
    # differences are all decided here, with no window — the same reason BrepTests exists.
    tests/NurbsTests.cpp
    src/util/nurbs.cpp
    # REQ-315 acceptance: a solid survives `.gs` save/reopen with topology counts identical and
    # volume/area stable. BrepJson.hpp is header-only and pulls in only nlohmann/json + brep.hpp, so
    # the serializer the saver actually runs is exercised directly here (GsIo.cpp is not linkable).
    tests/BrepJsonTests.cpp
    tests/SolidPickTests.cpp
    src/util/solidpick.cpp
    tests/MeshGsRoundTripTests.cpp
    tests/GsAnnotationJsonTests.cpp
    tests/GltfImportTests.cpp
    tests/StlImportTests.cpp
    tests/EntityIdTests.cpp
    tests/PointGroupTests.cpp
    tests/TinBuildTests.cpp
    tests/TinQueryTests.cpp
    tests/TinConstraintTests.cpp
    # REQ-068 / REQ-070 border extraction (ADR-036 (b)). Pure, like the rest of tinbuild — and the
    # border-vs-convex-hull distinction is the part that looks correct on simple data and is wrong
    # on exactly the shapes real survey data produces (a hole, a notch).
    tests/TinBorderTests.cpp
    # REQ-070 contour generation (ADR-036 (f)). Pure marching triangles: the vertex-tie rule and the
    # shared-edge chaining are both invisible on simple data and wrong on exactly the shapes a real
    # surface produces — a saddle, a level grazing a vertex, a contour running off the border.
    tests/ContourGenTests.cpp
    # REQ-072 surface analysis (ADR-036 (g)). Pure band assignment and downhill vectors: the
    # breakpoint rule and the flat-triangle refusal are both invisible on ordinary data and wrong
    # exactly at a band boundary and on level ground, which is most of a real site.
    tests/SurfaceAnalysisTests.cpp
    # REQ-073 surface-to-surface volumes (TASK-095). Pure spatial index + grid-sample integrator: the
    # cut/fill sign convention and the bbox-disjoint early-out are both invisible on a single hand
    # example and wrong on exactly the cases a real grading comparison needs (no overlap, self vs
    # self, partial overlap).
    tests/SurfaceVolumeTests.cpp
    tests/CadTableTests.cpp
    tests/CadBlockTests.cpp
    tests/SurfaceStatsTests.cpp
    tests/WatershedTests.cpp
    tests/TinVolumeTests.cpp
    tests/Issue119SurfaceTests.cpp
    tests/SurfaceProfileTests.cpp
    # REQ-070 surface style table (ADR-036 (d)). Header-only and pure, so it needs no source entry
    # below. Three of REQ-070's five acceptance conditions — the deleted-style fallback, the legacy
    # empty-name fallback, and the rejection of a major interval that is not a whole multiple of the
    # minor — are decided entirely here, before any geometry exists to look at.
    tests/SurfaceStyleTests.cpp
    tests/ToolspaceCatalogTests.cpp
    # REQ-089 surface rollover dwell. Header-only and pure, like the style table above. It is the
    # feature's ONLY new logic — the readout's numbers come from TinQueryTests' and
    # SurfaceStyleTests' already-pinned calls — and the one-shot it asserts is what keeps the
    # per-rest surface query off the per-frame path REQ-100 profile (c) measures.
    tests/HoverDwellTests.cpp
    # GitHub issue #166. Header-only and pure, like HoverDwellTests above. The gate is the only new
    # logic in the fix for the TRIM frame-rate drop — it decides whether the per-frame entity hover
    # scan re-runs — and both failure directions (scans too often / highlight goes stale) are the
    # ones a user would report, so both are pinned here with no GL context.
    tests/HoverPickGateTests.cpp
    # GitHub issue #168. Header-only and pure, like HoverPickGateTests above. The stall detector is
    # the only new logic in the investigation instrument for the multi-select freeze — it decides
    # where in a stall episode a frame sits — and its edges (began / ended) are what gate the one
    # diagnostic snapshot per episode, so both are pinned here with no GL context.
    tests/FrameWatchTests.cpp
    # REQ-316 / ADR-047: the bulge -> arc (centre, radius, start, sweep) helper and arc-segment
    # length. Pure geom2d, double math; the tangency and quarter-circle cases are what POLYLINE arc
    # mode and JOIN both rest on, and a sign error here is silent in plan view.
    tests/BulgeArcTests.cpp
    tests/UpdateCheckTests.cpp
    # REQ-308 Start screen. RecentDrawingsTests drives the MRU store; StartScreenTests covers the
    # tab-index sentinel invariant and the thumbnail-cache naming/eviction (header-only, pure).
    tests/RecentDrawingsTests.cpp
    tests/StartScreenTests.cpp
    src/io/RecentDrawings.cpp
    tests/TelemetryPingTests.cpp
    tests/AuthPingTests.cpp
    tests/GsMigrateTests.cpp
    tests/GpuPreferenceTests.cpp
    # REQ-204: one deliberately-broken fixture per document invariant. This is the second present-day
    # concrete use of util/docinvariants (the headless driver is the first), which is what lets the
    # module exist at all under architecture §11.4.
    tests/DocInvariantsTests.cpp
    src/util/docinvariants.cpp
    # REQ-069: the background rebuild worker's lifetime. Header-only against CadCommands.hpp (the
    # struct and its destructor), so it needs no extra domain source on the link line.
    tests/SurfaceRebuildLifetimeTests.cpp
    # REQ-103 STRETCH (TASK-098). Header-only against CadCommands.hpp — RecomputeArcFromEndpoints
    # is inline and pure (<cmath> + CadArc only), the same reasoning as the gate test above.
    tests/StretchGeomTests.cpp
    # REQ-103 FILLET (TASK-102). Header-only against CadCommands.hpp — the tangent-arc solve
    # (SolveFilletCenter and friends) is inline and pure (<cmath>/<vector> + curveintersect.hpp,
    # both dependency-free), the same reasoning as the STRETCH geometry test above.
    tests/FilletGeomTests.cpp
    # REQ-103 CHAMFER (TASK-103). Header-only against CadCommands.hpp, same reasoning as FILLET's
    # own geometry test above — ChamferPointAtDistance/ChamferRayIntersect are inline and pure.
    tests/ChamferGeomTests.cpp
    # REQ-087: the gate that decides the viewport has geometry to draw. Header-only against
    # CadCommands.hpp — the predicate is pure, which is the whole reason it is testable at all
    # while the rendering around it is not.
    tests/ExtendedGeometryGateTests.cpp
    # TASK-099: the viewport's click-routing policy. Header-only against CadCommands.hpp, like the
    # gate above, and for the same reason — the decision is pure, the UI around it is not, and the
    # decision is where three separate "the command ignores every click" bugs actually lived.
    tests/ViewportPickPolicyTests.cpp
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
    src/io/LibreDwg.cpp
    # Linetype dash tessellation, including per-vertex Z (REQ-058). Standalone like DwgProbe.cpp —
    # only <string>/<vector>/<cmath>/<cctype> — so it links without the GUI stack.
    src/commands/CadLinetype.cpp
    # Intersection math behind the INT/APPINT snaps (REQ-062). Pure and dependency-free on purpose:
    # this is where REQ-101 is actually enforced, and it must be testable without a GL context.
    src/util/curveintersect.cpp
    # REQ-316 / ADR-047 bulge -> arc math (BulgeArcTests). Pure geom2d — <cmath> only — so the
    # quarter-circle and tangency cases POLYLINE arc mode and JOIN rest on are pinned without GL.
    src/util/geom2d.cpp
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
    # REQ-070 contour generation. Pure by ADR-036 (f) — no GL, no document — so a half-open contour
    # is caught against a hand-computed expectation rather than by looking at a viewport.
    src/util/contourgen.cpp
    # REQ-072 surface analysis. Pure by ADR-036 (g) — no GL, no document — so a reversed arrow is
    # caught against a hand-computed fall line rather than by squinting at a viewport full of them.
    src/util/surfaceanalysis.cpp
    # REQ-073 surface-to-surface volumes + the spatial index they run on. Pure — no GL, no document —
    # so a wrong sign on cut vs. fill is caught against a hand-computed planar case, TASK-095.
    src/util/surfacevolume.cpp
    src/util/surfacestats.cpp
    src/util/watershed.cpp
    src/util/tinvolume.cpp
    src/util/surfacequery.cpp
    src/util/gridsurface.cpp
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
    # REQ-091 accounts sign-in. Pure by ADR-037 (d) — PKCE construction, base64url, the authorize
    # URL, and the silent-refresh-vs-interactive decision — no network/window, same split as
    # TelemetryPing above. The BCrypt SHA-256 call PKCE needs is in HttpFetch and is exercised by
    # AuthService, not by this pure module or its tests.
    src/auth/AuthPing.cpp
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
    # TASK-099 (ViewportPickPolicyTests): ViewportPickPolicy.hpp. A header, like the ones above —
    # the test target still links no viewport translation unit and needs no GL.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/viewport
    ${CMAKE_CURRENT_SOURCE_DIR}/src/survey
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    # REQ-308 StartScreenTests: ThumbnailCache.hpp (header-only, pure — <filesystem> only).
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdf
    ${CMAKE_CURRENT_SOURCE_DIR}/src/telemetry
    ${CMAKE_CURRENT_SOURCE_DIR}/src/auth
    ${CMAKE_CURRENT_SOURCE_DIR}/src/update
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    # For FuzzHarnessTests, which includes headless/FuzzGenerator.hpp and headless/Minimizer.hpp.
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
  )
  target_compile_definitions(GoSurveyTests PRIVATE
    GOSURVEY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples"
  )
  target_link_libraries(GoSurveyTests PRIVATE Catch2::Catch2WithMain LibreDWG::libredwg)
  gosurvey_target_warnings(GoSurveyTests)
  gosurvey_enable_pch(GoSurveyTests)

  include(Catch)
  catch_discover_tests(GoSurveyTests)

  # ---------------------------------------------------------------------------
  # GoSurveySnapTests (issue #103): CadSnap::FindBest needs AppCommandState AND the rest of
  # gosurvey_domain it transitively calls into (SurveyPoints.cpp reaches into CadCommands.cpp
  # for EraseCadAnnotationAtIndex/FindEntityById etc.), which is exactly what GoSurveyTests above
  # is built to avoid linking. It is otherwise identical to gosurvey_headless's link set — domain
  # library + imgui_core, no GLFW/OpenGL/window — just with a Catch2 main instead of the
  # transcript driver.
  # ---------------------------------------------------------------------------
  add_executable(GoSurveySnapTests
    src/platform/HeadlessFileDialogs.cpp
    src/platform/HeadlessPdfAttach.cpp
    tests/CadSnapTests.cpp
    tests/GsIoDimAngularTests.cpp
    tests/GsIoViewportCameraTests.cpp
    tests/GsMigrateLegacyBreaklineTests.cpp
    tests/ViewportUcsTests.cpp
    # REQ-318 increment 2 (issue #148) — the sub-object SELECTION: its store, the expiring
    # reference, mutual exclusion and cross-solid depth order. Needs gosurvey_domain for
    # SubmitSubObjectPick / ExpireSubObjectSelection, like ViewportUcsTests above.
    tests/SubObjectSelectionTests.cpp
    # REQ-060 (issue #148 Phase 5 slice 4b) — the TRANSLATE gizmo: the skew-line solve, the anchor,
    # and the two negatives (a click that misses every handle, an empty selection). Needs
    # gosurvey_domain for SubmitGizmoClick / ApplyTranslationToSelection.
    tests/GizmoTranslateTests.cpp
    tests/LibreDwgCadTests.cpp
    tests/CadBlockImportTests.cpp
    # issue #166 — the TRIM cut-line rubber preview. Needs gosurvey_domain (it calls into
    # CadCommands.cpp's trim helpers), so it rides with GoSurveySnapTests, not GoSurveyTests.
    tests/TrimLinePreviewTests.cpp
  )
  target_include_directories(GoSurveySnapTests PRIVATE
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
  target_link_libraries(GoSurveySnapTests PRIVATE gosurvey_domain Catch2::Catch2WithMain LibreDWG::libredwg)
  target_compile_definitions(GoSurveySnapTests PRIVATE
    GOSURVEY_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples")
  gosurvey_target_warnings(GoSurveySnapTests)
  gosurvey_enable_pch(GoSurveySnapTests)
  catch_discover_tests(GoSurveySnapTests)

  if(NOT GOSURVEY_DEVELOPER_SHELL)
    add_test(NAME req161-release-no-devshell
      COMMAND ${CMAKE_COMMAND}
              -DGOSURVEY_EXE=$<TARGET_FILE:GoSurvey>
              -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/Req161ReleaseNoDevShell.cmake")
  endif()

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

    # Oracles held DISABLED rather than deleted or WILL_FAIL'ed.
    #
    # ***** THE LIST IS EMPTY. *****  Keep the mechanism and this note: it is the record of what the
    # convention is for, and the next oracle to find a real defect goes here rather than being
    # deleted to get a green suite. TASK-056 made this call for `headless.gs-roundtrip.compare`
    # while #56/#57 were open, and it is why those defects stayed visible. WILL_FAIL is worse than
    # either option — it goes green the moment the failure turns into a DIFFERENT failure, which is
    # the one outcome nobody wants to miss.
    #
    # `dxf-export-stable` lived here from 2026-08-18 to 2026-08-26 and is now RE-ENABLED. It found
    # three defects, and needed all three closed before it could come back:
    #
    #   #64  LWPOLYLINE came back from a round trip as loose LINEs   — TASK-083, 2026-08-21
    #   #63  ARC and ELLIPSE dropped entirely by the writer          — TASK-114, 2026-08-26
    #   #94  the round trip walked the drawing ~2e-6 every cycle     — TASK-116, 2026-08-26
    #
    # Each left a regression of its own behind, so a closed defect never waited on an open one:
    # `regression-64-dxf-polyline-identity.txt`, `regression-63-dxf-arc-ellipse-identity.txt`,
    # `regression-94-dxf-origin-no-drift.txt` and `regression-94a-dxf-origin-large-coord.txt`.
    # Those stay whether or not this oracle is enabled — the oracle proves the invariant, and each
    # regression pins the specific defect that broke it.
    foreach(_disabled )
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
                     --timeout-ms 120000)
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
