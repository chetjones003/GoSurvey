cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)  # let CMAKE_MSVC_RUNTIME_LIBRARY (below) control /MD vs /MDd
project(GoSurvey VERSION 0.7.3 LANGUAGES CXX C RC)

if(MSVC)
  # third_party/xerces-c/lib/win-x64/xerces-c_3.lib is vendored prebuilt against the Release DLL CRT
  # (/MD) only — there is no /MDd build available. Left alone, a Debug configure links every other
  # object file against /MDd and fails at link time with ~500 LNK2038 CRT-mismatch errors, all against
  # xerces-c_3.lib. Force the Release DLL CRT in every configuration, matching the vendored lib,
  # rather than carrying a Debug CRT mismatch no vendored replacement can fix.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

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
# MSVC exception + RTTI model (GitHub issue #375).
#
# CMake's MSVC toolchain seeds CMAKE_CXX_FLAGS with `/DWIN32 /D_WINDOWS /EHsc /GR`.
# A first configure done with `-DCMAKE_CXX_FLAGS=...` on the command line (an early
# REQ-324 /analyze experiment did exactly this) REPLACES that seed rather than
# adding to it, silently dropping /EHsc. After that, any thrown C++ exception
# (e.g. nlohmann::json::parse_error on a corrupt recent-files store) unwinds
# through frames compiled with no unwind data, misses every catch, and hits
# std::terminate -> abort -> __fastfail, which surfaces as exit code 0xC0000409
# in ucrtbase.dll. build.bat only reconfigures when build.ninja is missing, so a
# tree poisoned once stays poisoned across every incremental build.
#
# Restore the two flags here whenever they are absent, no matter how
# CMAKE_CXX_FLAGS was seeded. Our targets and the vendored ones (Catch2, Dear
# ImGui) all depend on working exceptions and RTTI.
if(MSVC)
  if(NOT CMAKE_CXX_FLAGS MATCHES "/EH")
    string(APPEND CMAKE_CXX_FLAGS " /EHsc")
  endif()
  if(NOT CMAKE_CXX_FLAGS MATCHES "/GR")
    string(APPEND CMAKE_CXX_FLAGS " /GR")
  endif()
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
#
# GOSURVEY_ENABLE_ANALYZE (REQ-324, off by default) adds MSVC's /analyze as a per-target
# compile option, not a CMAKE_CXX_FLAGS override: CMake's MSVC platform module supplies
# CMAKE_CXX_FLAGS' default content (/EHsc /GR among it), and a command-line
# -DCMAKE_CXX_FLAGS=... on first configure REPLACES that default rather than adding to it
# — losing /EHsc silently. That broke exception-using tests with a stack-buffer-overrun
# crash the first time this was tried (CI run for REQ-324's own PR). Routing it through
# this function keeps it scoped to our own targets (third_party/ vendored code is never
# passed through gosurvey_target_warnings) and additive to whatever flags CMake already set.
option(GOSURVEY_ENABLE_ANALYZE
  "Add MSVC /analyze to our own targets (REQ-324). Report-only: not paired with /WX." OFF)
function(gosurvey_target_warnings tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE
      /W4 /permissive-
      /wd4244 /wd4305 /wd4267 /wd4245
      /wd4456 /wd4457 /wd4458 /wd4459)
    target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(GOSURVEY_ENABLE_ANALYZE)
      target_compile_options(${tgt} PRIVATE /analyze)
    endif()
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

# --- LibreDWG: GoSurvey's own copy, built from source (REQ-170 / ADR-041 (h), D-2026-09-30-d) ---
# The library source of upstream tag 0.14 plus GoSurvey's changes (third_party/libredwg/VENDORED.md).
# Only the library is built: upstream's own CMakeLists (its ~30 try_compile probes, programs and
# tests) is not used. src/config.h is the one upstream's CMake generates for MSVC x64, committed,
# so there is nothing to configure. Sources and flags mirror upstream's LIBREDWG_LIBONLY build
# with JSON disabled and write enabled.
set(libredwg_SOURCE_DIR "${GOSURVEY_TP}/libredwg")
set(libredwg_BINARY_DIR "${GOSURVEY_TP}/libredwg")   # config.h lives beside src/*.h
set(_lrd "${libredwg_SOURCE_DIR}/src")
add_library(libredwg STATIC
  ${_lrd}/bits.c ${_lrd}/codepages.c ${_lrd}/common.c ${_lrd}/logging.c ${_lrd}/classes.c
  ${_lrd}/objects.c ${_lrd}/decode.c ${_lrd}/decode2.c ${_lrd}/decode_r11.c ${_lrd}/decode_r2007.c
  ${_lrd}/dwg.c ${_lrd}/hash.c ${_lrd}/dwg_api.c ${_lrd}/dynapi.c ${_lrd}/dxfclasses.c ${_lrd}/free.c
  ${_lrd}/geom.c ${_lrd}/out_dxf.c ${_lrd}/out_dxfb.c ${_lrd}/encode.c ${_lrd}/encode2.c
  ${_lrd}/in_dxf.c ${_lrd}/reedsolomon.c ${_lrd}/print.c)
set_property(TARGET libredwg PROPERTY C_STANDARD 99)
target_include_directories(libredwg PRIVATE "${_lrd}")
target_include_directories(libredwg PUBLIC "${libredwg_SOURCE_DIR}/include")
# Vendored: silence its warnings so ours stay visible (as for glew below).
if(MSVC)
  target_compile_options(libredwg PRIVATE /w /utf-8)
  target_compile_definitions(libredwg PRIVATE _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS
    DEBUG_CLASSES)
else()
  target_compile_options(libredwg PRIVATE -w)
endif()
if(WIN32)
  target_link_libraries(libredwg PUBLIC ws2_32)
endif()
target_compile_definitions(libredwg INTERFACE "GOSURVEY_LIBREDWG_VERSION=\"0.14\"")
add_library(LibreDWG::libredwg ALIAS libredwg)
unset(_lrd)

# --- CS-MAP: vendored headers + prebuilt static lib + dictionaries (REQ-358 / ADR-063) ---
# OSGeo MetaCRS CS-MAP, the coordinate-system catalogue and projection engine. Prebuilt MSVC x64
# Release /MD static lib, like LibreDWG. Its headers are NOT an interface include directory: only
# src/geo/CoordinateSystems.cpp may include them (ADR-063 (c)), so they are added to that one source
# below. The compiled dictionaries and the US grid files (D-2026-09-29-d) are staged beside the
# executable as resources/csmap/ — installer payload and what the tests load.
set(GOSURVEY_CSMAP_DIR "${GOSURVEY_TP}/csmap")
add_library(csmap STATIC IMPORTED GLOBAL)
set_target_properties(csmap PROPERTIES
  IMPORTED_LOCATION "${GOSURVEY_CSMAP_DIR}/lib/win-x64/csmap.lib")
add_library(CsMap::csmap ALIAS csmap)
message(STATUS "CS-MAP r3078 vendored (prebuilt third_party/csmap/lib/win-x64/csmap.lib)")

# --- Xerces-C: vendored headers + prebuilt static lib (dependency of libE57Format) ---
if(NOT EXISTS "${GOSURVEY_TP}/xerces-c/lib/win-x64/xerces-c_3.lib")
  message(FATAL_ERROR "REQ-172: third_party/xerces-c/lib/win-x64/xerces-c_3.lib is missing. See third_party/xerces-c/VENDORED.md.")
endif()
add_library(xerces_c STATIC IMPORTED GLOBAL)
set_target_properties(xerces_c PROPERTIES
  IMPORTED_LOCATION "${GOSURVEY_TP}/xerces-c/lib/win-x64/xerces-c_3.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${GOSURVEY_TP}/xerces-c/include")
if(WIN32)
  set_property(TARGET xerces_c APPEND PROPERTY INTERFACE_LINK_LIBRARIES ws2_32)
endif()
add_library(XercesC::xerces_c ALIAS xerces_c)
message(STATUS "Xerces-C 3.3.0 vendored (prebuilt third_party/xerces-c/lib/win-x64/xerces-c_3.lib)")

# --- libE57Format: vendored headers + prebuilt static lib (REQ-172, ADR-042) ---
if(NOT EXISTS "${GOSURVEY_TP}/libe57format/lib/win-x64/E57Format.lib")
  message(FATAL_ERROR "REQ-172: third_party/libe57format/lib/win-x64/E57Format.lib is missing. See third_party/libe57format/VENDORED.md.")
endif()
add_library(e57format STATIC IMPORTED GLOBAL)
set_target_properties(e57format PROPERTIES
  IMPORTED_LOCATION "${GOSURVEY_TP}/libe57format/lib/win-x64/E57Format.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${GOSURVEY_TP}/libe57format/include")
set_property(TARGET e57format APPEND PROPERTY INTERFACE_LINK_LIBRARIES XercesC::xerces_c)
add_library(E57Format::E57Format ALIAS e57format)
message(STATUS "libE57Format 3.4.0 vendored (prebuilt third_party/libe57format/lib/win-x64/E57Format.lib)")

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
# md4c (REQ-336 / ADR-056) — CommonMark parser for the What's New billboard.
# Vendored under third_party/md4c/; UI draw layer is in-tree (MarkdownImGui).
# ---------------------------------------------------------------------------
if(NOT EXISTS "${GOSURVEY_TP}/md4c/md4c.c")
  message(FATAL_ERROR "REQ-336: third_party/md4c/md4c.c is missing. See third_party/md4c/VENDORED.md.")
endif()
add_library(md4c STATIC "${GOSURVEY_TP}/md4c/md4c.c")
target_include_directories(md4c PUBLIC "${GOSURVEY_TP}/md4c")
if(MSVC)
  target_compile_options(md4c PRIVATE /w)
else()
  target_compile_options(md4c PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# miniz (REQ-380 / ADR-066 / D-2026-10-05-i) — ZIP read/write for the project pack (.gspack).
# Vendored under third_party/miniz/.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${GOSURVEY_TP}/miniz/miniz.c")
  message(FATAL_ERROR "REQ-380: third_party/miniz/miniz.c is missing. See third_party/miniz/VENDORED.md.")
endif()
add_library(miniz STATIC "${GOSURVEY_TP}/miniz/miniz.c")
target_include_directories(miniz PUBLIC "${GOSURVEY_TP}/miniz")
if(MSVC)
  target_compile_options(miniz PRIVATE /w)
else()
  target_compile_options(miniz PRIVATE -w)
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
  src/commands/CadField.cpp
  # Cohesive slices split out of the CadCommands.cpp mega-TU (TASK-150 Phase 2,
  # issue #142). Same TU rules; entry points declared in CadCommands.hpp.
  src/commands/CadCommands_Align.cpp
  src/commands/CadCommands_Geo.cpp
  src/commands/ProjectSettings.cpp
  src/commands/ProjectPoints.cpp
  # REQ-383 (#696 P9): the warnings before destructive / cross-project actions on shared data
  src/commands/ProjectWarnings.cpp
  # REQ-378 (#696 P5): Add Drawing to Project - the flow, and the whole-drawing conversion it offers
  src/commands/ProjectAddFlow.cpp
  # REQ-379 (#696 P6): tracked files at the drawing level (copy/link prompt, save-time record, open-time re-attach)
  src/commands/ProjectFiles.cpp
  src/commands/ConvertDrawing.cpp
  src/commands/CadCommands_Mleader.cpp
  src/commands/CadCommands_Ucs.cpp
  src/commands/CadDynInput.cpp
  src/commands/CadCommands_Bench.cpp
  src/commands/CadBlocks.cpp
  src/commands/CadLinetype.cpp
  src/commands/EntityId.cpp
  src/survey/PointGroupRule.cpp
  src/commands/CadCoordinateFrame.cpp
  src/commands/CadColor.cpp

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
  # REQ-302/ADR-053 (issue #323) — ribbon measure pass runtime (icon side override for tests + app).
  src/ui/RibbonLayoutMeasure.cpp
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
  # Coordinate systems — the one CS-MAP wrapper (REQ-358 / ADR-063)
  src/geo/CoordinateSystems.cpp
  # REQ-378 (#696 P5): the pure similarity-transform plan for Convert
  src/geo/DrawingConversion.cpp
  # REQ-360: the pure local <-> grid transformation (no CS-MAP)
  src/geo/LocalGridTransform.cpp
  # REQ-363 / ADR-064: Web Mercator tile maths (pure)
  src/geo/WebMercator.cpp
  # REQ-363 / ADR-064 (c): online map tile fetch + disk cache (fetch injected; no WinHTTP here)
  src/platform/MapTileService.cpp
  src/platform/StbImage.cpp
  # REQ-363: the online map controller (GL-free; the app injects texture upload)
  src/commands/CadOnlineMap.cpp
  # User preferences. In the domain set for UserDataDirectory(), which the BENCH command writes its
  # results through — pure <filesystem>, no window.
  src/io/UserPrefs.cpp
  # REQ-308 recent-drawings MRU store (D-2026-08-30-b). Pure — <filesystem>/<fstream>/json, path
  # and clock passed in by the caller — so GoSurveyTests links it directly too (see that target).
  src/io/RecentDrawings.cpp
  # REQ-373/374/382 (#696 P1) project file, join detection, lock file. Pure <filesystem>/json.
  src/io/Project.cpp
  src/io/ProjectPointDb.cpp
  src/io/ProjectPointRules.cpp
  src/io/ProjectAddDrawing.cpp
  # REQ-379 (#696 P6): tracked files - attach, record, find, health. Pure <filesystem>/json.
  src/io/ProjectFiles.cpp
  # REQ-380 (#696 P7): Pack Project / Open Packed Project (.gspack = ZIP via miniz)
  src/io/ProjectPack.cpp
  # REQ-381 (#696 P8): turnover records. Pure <filesystem>/json + miniz CRC.
  src/io/ProjectTurnover.cpp
  # REQ-336 — What's New markdown load (path resolve + read); dismiss predicates in WhatsNewLogic.
  src/io/WhatsNewContent.cpp
  src/io/WikiContent.cpp
  src/io/WikiHelp.cpp
  src/util/WhatsNewLogic.cpp
  src/io/SurveyCsv.cpp
  src/io/DxfIo.cpp
  src/io/DwgIo.cpp
  src/io/DwgProbe.cpp
  src/io/LibreDwg.cpp
  src/io/LibreDwgCad.cpp
  src/io/LibreDwgField.cpp
  src/io/LibreDwgDynamicBlock.cpp
  src/io/LibreDwgMaterial.cpp
  src/io/LibreDwgAnnotContext.cpp
  src/io/LibreDwgVisualStyle.cpp
  src/io/LibreDwgLights.cpp
  src/io/LibreDwgPlant.cpp
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
  src/util/daylight.cpp
  src/util/benchscene.cpp
  # REQ-313 B-rep solid kernel (ADR-045, GitHub issue #146). Pure by that ADR — no GL, no ImGui, no
  # document — so a wrong volume integral or an unclosed shell is caught against a closed-form
  # expectation rather than by looking at a shaded viewport.
  src/util/brep.cpp
  # REQ-320 / ADR-051: the ACIS SAT record parser feeding 3DSOLID import (GitHub issue #299). Pure
  # and dependency-free like brep.cpp itself — no LibreDWG, no GL — so a malformed or unsupported
  # record is caught against a hand-authored SAT fixture rather than a real vendor block library.
  src/util/AcisSatParser.cpp
  src/util/AcisSatWriter.cpp
  # REQ-315 / ADR-048: the freeform (NURBS) surface patch — pure math (evaluation, derivatives,
  # validation, the loft builders), no GL and no brep coupling, so its correctness is pinned against
  # closed-form circle / plane expectations rather than a viewport.
  src/util/nurbs.cpp
  # REQ-318 sub-object picking (ADR-049, GitHub issue #148). Pure for the same reason the kernel
  # above it is: the precedence between a face, an edge and a vertex, and the refusal to report a
  # pick it cannot justify, are decided without a window.
  src/util/solidpick.cpp

  # Point-cloud out-of-core octree (REQ-171/REQ-172, ADR-060) — pure; builds/queries the spatial
  # index over an in-memory point set, no file IO and no GL calls.
  src/util/pointcloudoctree.cpp

  # E57 point-cloud reader (REQ-172, ADR-042 (d)) — adapts vendored libE57Format to GoSurvey's
  # flat interleaved point layout. Delivered first among REQ-172's formats (D-2026-09-17-d).
  src/io/CadPointCloudE57.cpp

  # .gscloud out-of-core cache codec (ADR-060) — builds/reads the disk-backed octree so a
  # multi-GB scan is never fully resident in memory.
  src/util/pointcloudcache.cpp

  # Least-squares cylinder-axis fit (REQ-347) — pure; fits a neighborhood of point-cloud points to
  # a cylinder axis via PCA + radial least squares, no file IO and no GL calls.
  src/util/cylinderfit.cpp

  # Document invariants (REQ-204) — pure; shared by the Catch2 suite and the headless driver
  src/util/docinvariants.cpp
  src/util/SaveTrace.cpp
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
target_link_libraries(gosurvey_domain PUBLIC imgui_core LibreDWG::libredwg E57Format::E57Format CsMap::csmap miniz)
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
  # REQ-302/ADR-053 (issue #325) — RibbonLayout::DrawSection. Calls RibbonDrawButtonForLayout
  # (CadUi.cpp), so it needs a live ImGui window like the rest of this list; no test target links
  # CadUi.cpp (it pulls GLFW/OpenGL through the windowed app), so this is exercised manually.
  src/ui/RibbonLayoutDraw.cpp
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
  src/ui/CadUi_SurveyPointGrid.cpp
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
  src/ui/CadUi_Projects.cpp
  src/ui/CadUi_WhatsNew.cpp
  src/ui/CadUi_DrawingSettings.cpp
  src/ui/CadUi_Wiki.cpp
  src/ui/CadUi_ColorPicker.cpp
  src/ui/MarkdownImGui.cpp
  src/ui/SplashScreen.cpp
  src/ui/ImGuiLayout.cpp
  src/ui/RichTextEdit.cpp
  src/ui/ViewCube.cpp
  src/ui/UcsIcon.cpp
  src/ui/PdfAttachDialog.cpp
  src/ui/CadUi_InsertBlock.cpp
  src/ui/CadUi_BlockAuthoring.cpp
  src/ui/CadUi_PipeFittingPalette.cpp
  src/ui/CadUi_ConnectionModes.cpp

  # I/O (windowed application only)
  src/io/PdfPlot.cpp

  # PDF underlay — the real, GL-backed implementation (headless links a seam instead)
  src/pdf/PdfAttach.cpp

  # REQ-387 / ADR-067 — the built-in PDF viewer: pure core, PDFium wrapper, ImGui window
  src/pdf/PdfViewerCore.cpp
  src/pdf/PdfDocument.cpp
  src/pdf/PdfSplit.cpp
  src/pdf/PdfAnnotate.cpp
  src/pdf/PdfRaw.cpp
  src/pdf/PdfMeasure.cpp
  src/pdf/PdfSnap.cpp
  src/pdf/PdfScaleCheck.cpp
  src/pdf/PdfDimAudit.cpp
  src/pdf/PdfCoordExtract.cpp
  src/pdf/PdfAlign.cpp
  src/pdf/PdfDiff.cpp
  src/ui/PdfViewerWindow.cpp
  src/ui/PdfCompare.cpp

  # Platform
  src/platform/AppIcon.cpp
  src/platform/WinFileDialogs.cpp
  src/platform/GpuPreference.cpp
  src/platform/HttpFetch.cpp
  src/platform/OAuthListener.cpp
  src/platform/CredentialStore.cpp
  src/platform/StartupFailure.cpp
  src/platform/FailureReportUi.cpp
  src/platform/CrashReporter.cpp
  src/platform/CrashReporterWireReason.cpp

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
  E57Format::E57Format
  CsMap::csmap
  md4c
)
target_include_directories(GoSurvey PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/devshell)
if(GOSURVEY_DEVELOPER_SHELL)
  # Domain objects must share the Test Engine ImGui ABI (ADR-040). Do not link gosurvey_domain
  # (compiled against imgui_core without hooks) into the Debug windowed exe.
  target_sources(GoSurvey PRIVATE ${GOSURVEY_DOMAIN_SOURCES})
  target_sources(GoSurvey PRIVATE
    src/devshell/DevShell.cpp
    src/devshell/DevShellTests.cpp
    src/devshell/DevShellProjectTests.cpp
  )
  target_compile_definitions(GoSurvey PRIVATE GOSURVEY_DEVELOPER_SHELL)
  target_link_libraries(GoSurvey PRIVATE imgui_core_testengine)
  # The domain sources are compiled into this target, so what gosurvey_domain links PUBLIC (miniz,
  # for ProjectPack / ProjectTurnover - REQ-380/381) has to be linked here too.
  target_link_libraries(GoSurvey PRIVATE miniz)
else()
  target_link_libraries(GoSurvey PRIVATE gosurvey_domain)
endif()

# ADR-063 (c): the only source that sees CS-MAP's headers. No PCH: cs_map.h is a C header with its
# own platform macros, kept out of the shared precompiled header.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/geo/CoordinateSystems.cpp"
  APPEND PROPERTY INCLUDE_DIRECTORIES "${GOSURVEY_CSMAP_DIR}/include")
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/geo/CoordinateSystems.cpp"
  PROPERTY SKIP_PRECOMPILE_HEADERS ON)
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/geo/CoordinateSystems.cpp"
  APPEND PROPERTY COMPILE_DEFINITIONS WIN32 _WINDOWS)

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
  target_link_libraries(GoSurvey PRIVATE comdlg32 winhttp bcrypt ole32 ws2_32 advapi32 shell32 dbghelp)
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
set(GOSURVEY_WHATS_NEW_MD "${CMAKE_CURRENT_SOURCE_DIR}/resources/whats-new.md")
set(GOSURVEY_WHATS_NEW_BG "${CMAKE_CURRENT_SOURCE_DIR}/resources/whats-new-bg.png")
set(GOSURVEY_WHATS_NEW_IMG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/whats-new")
file(GLOB GOSURVEY_WHATS_NEW_IMAGES CONFIGURE_DEPENDS "${GOSURVEY_WHATS_NEW_IMG_DIR}/*.png")
set(GOSURVEY_SPLASH_BG "${CMAKE_CURRENT_SOURCE_DIR}/resources/splash-bg.png")
# REQ-336: the billboard content must exist at configure time so a missing file fails the build,
# not only a packaged installer.
if(NOT EXISTS "${GOSURVEY_WHATS_NEW_MD}")
  message(FATAL_ERROR "REQ-336: resources/whats-new.md is required (What's New billboard).")
endif()
if(NOT EXISTS "${GOSURVEY_WHATS_NEW_BG}")
  message(FATAL_ERROR "REQ-336: resources/whats-new-bg.png is required (What's New backdrop).")
endif()
if(NOT EXISTS "${GOSURVEY_SPLASH_BG}")
  message(FATAL_ERROR "REQ-093: resources/splash-bg.png is required (startup splash backdrop).")
endif()
set(GOSURVEY_WIKI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/wiki")
if(NOT EXISTS "${GOSURVEY_WIKI_DIR}/Home.md")
  message(FATAL_ERROR "resources/wiki/Home.md is required (shipped user manual).")
endif()
# Stage bundled resources beside the executable. A POST_BUILD hook only runs when GoSurvey is
# relinked, so changing splash-bg.png (or any other resource) left a stale copy in build/resources/
# until a code change forced a rebuild. This stamp target re-runs whenever any listed resource
# changes, even on an incremental build that skips linking.
file(GLOB_RECURSE GOSURVEY_RESOURCE_DIR_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/hatches/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/blocks/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/wiki/*")
set(GOSURVEY_BUNDLED_RESOURCE_FILES
  "${GOSURVEY_DEFAULT_TEMPLATE}"
  "${GOSURVEY_WHATS_NEW_MD}"
  "${GOSURVEY_WHATS_NEW_BG}"
  "${GOSURVEY_SPLASH_BG}"
  ${GOSURVEY_WHATS_NEW_IMAGES}
  ${GOSURVEY_RESOURCE_DIR_FILES})
set(_GOSURVEY_RESOURCE_STAMP "${CMAKE_CURRENT_BINARY_DIR}/.gosurvey_resources.stamp")
add_custom_command(
  OUTPUT "${_GOSURVEY_RESOURCE_STAMP}"
  DEPENDS ${GOSURVEY_BUNDLED_RESOURCE_FILES}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/resources"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/resources/layouts"
  # `copy_directory` never removes files that were deleted or renamed in the source tree, so a
  # rebuild after dropping an old icon left the stale copy beside the exe (and in the installer
  # payload) forever. Wipe the mirrored dirs first so the deploy is an exact copy of source.
  COMMAND ${CMAKE_COMMAND} -E rm -rf
          "${CMAKE_CURRENT_BINARY_DIR}/resources/icons"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/hatches"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/blocks"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/fonts"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/wiki"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/whats-new"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_DEFAULT_TEMPLATE}"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/default-template.gst"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_WHATS_NEW_MD}"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/whats-new.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_WHATS_NEW_BG}"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/whats-new-bg.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_SPLASH_BG}"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/splash-bg.png"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/icons"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/hatches"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/hatches"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/blocks"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/blocks"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/fonts"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/wiki"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/wiki"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${GOSURVEY_WHATS_NEW_IMG_DIR}"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/whats-new"
  COMMAND ${CMAKE_COMMAND} -E touch "${_GOSURVEY_RESOURCE_STAMP}"
  COMMENT "Stage bundled resources beside GoSurvey executable")
add_custom_target(gosurvey_stage_resources DEPENDS "${_GOSURVEY_RESOURCE_STAMP}")
add_dependencies(GoSurvey gosurvey_stage_resources)

# CS-MAP dictionaries + US grid files (REQ-358, D-2026-09-29-d) → resources/csmap/. Their own stamp:
# ~230 MB is copied only when one of them changes, not on every resource edit.
file(GLOB_RECURSE GOSURVEY_CSMAP_DICTIONARY_FILES CONFIGURE_DEPENDS "${GOSURVEY_CSMAP_DIR}/dictionaries/*")
set(_GOSURVEY_CSMAP_STAMP "${CMAKE_CURRENT_BINARY_DIR}/.gosurvey_csmap.stamp")
add_custom_command(
  OUTPUT "${_GOSURVEY_CSMAP_STAMP}"
  DEPENDS ${GOSURVEY_CSMAP_DICTIONARY_FILES}
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/resources/csmap"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${GOSURVEY_CSMAP_DIR}/dictionaries"
          "${CMAKE_CURRENT_BINARY_DIR}/resources/csmap"
  COMMAND ${CMAKE_COMMAND} -E touch "${_GOSURVEY_CSMAP_STAMP}"
  COMMENT "Stage CS-MAP dictionaries beside GoSurvey executable")
add_custom_target(gosurvey_stage_csmap DEPENDS "${_GOSURVEY_CSMAP_STAMP}")
add_dependencies(GoSurvey gosurvey_stage_csmap)

# Copy pdfium.dll next to the executable so it can be found at runtime.
if(WIN32)
  add_custom_command(TARGET GoSurvey POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${pdfium_binaries_SOURCE_DIR}/bin/pdfium.dll"
            "$<TARGET_FILE_DIR:GoSurvey>/pdfium.dll"
    COMMENT "Copy pdfium.dll beside executable")
endif()
