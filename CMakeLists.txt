cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)  # let CMAKE_MSVC_RUNTIME_LIBRARY (below) control /MD vs /MDd

# sccache requires embedded MSVC debug information instead of the default /Zi PDB format.
# CMP0141 must be NEW before project() initializes compiler flags; the launcher is passed by CI.
if(POLICY CMP0141)
  if(CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
  endif()
endif()

project(GoSurvey VERSION 0.7.4 LANGUAGES CXX C RC)

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

# Keep runnable applications separate from generator files, libraries and helper tools.
set(GOSURVEY_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/build" CACHE PATH "Application output directory")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  string(APPEND GOSURVEY_RUNTIME_DIR "/debug")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GOSURVEY_RUNTIME_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")
if(CMAKE_CONFIGURATION_TYPES)
  foreach(_config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${_config}" _upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_upper} "${CMAKE_SOURCE_DIR}/build/${_config}")
  endforeach()
endif()
set(GOSURVEY_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/../../artifacts/${CMAKE_BUILD_TYPE}")
file(MAKE_DIRECTORY "${GOSURVEY_ARTIFACT_DIR}")

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
# CMAKE_CXX_FLAGS was seeded. Our targets and vendored libraries depend on
# working exceptions and RTTI.
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
#   4267       size_t -> 32-bit at Win32 / third-party API boundaries.
#   4245       signed/unsigned mismatch on Win32 API constants.
#   4456/4457/4458/4459  local/param/member/global shadowing — style, not a bug.
#   4200       zero-sized (flexible) array members in LibreDWG's public dwg.h, which is a C header
#              we vendor unmodified; the extension is how that API is declared.
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
#
# GOSURVEY_ANALYZE_WERROR (issue #977) adds /WX to the same targets, so any /W4 or /analyze warning
# fails the build. It is meant for the pr-checks `lint / msvc-analyze` job; release.yml leaves it
# OFF so REQ-324's "the release build is not failed by findings" still holds. It is only valid
# together with GOSURVEY_ENABLE_ANALYZE, because the suppressions below are what keep the
# backlog at zero.
#
# Triage of the full-codebase /analyze run (issue #977); each decision is recorded in
# spec/coding-standards.md section 11 and workshop/tasks/TASK-977-analyze-triage.md:
#   fixed       every real finding in our code (C28182, C6011, C28199, C6336, C4701, C6031/C4834,
#               C4189, C4505, C4005, C4996, C6326) was fixed at the source.
#   C6246       shadowing under another number -- same policy as 4456-4459, so same /wd.
#   C6262       "function uses N bytes of stack". /analyze:stacksize raises the 16 KB default to
#               64 KB for product code (the biggest frames are ~40 KB, non-recursive, on 1 MB+
#               stacks). Test targets (TEST) drop the check: Catch2 test bodies and the headless
#               transcript driver legitimately hold large fixtures on the stack, up to ~230 KB.
#   third_party compiled into our targets (pugixml, stb_image) is excluded from /analyze below;
#   <angle-bracket> headers (Windows SDK, Qt) are external: no warnings, no analysis.
option(GOSURVEY_ENABLE_ANALYZE
  "Add MSVC /analyze to our own targets (REQ-324). Report-only unless GOSURVEY_ANALYZE_WERROR." OFF)
option(GOSURVEY_ANALYZE_WERROR
  "With GOSURVEY_ENABLE_ANALYZE, add /WX so any warning fails the build (issue #977)." OFF)
function(gosurvey_target_warnings tgt)
  cmake_parse_arguments(GSW "TEST" "" "" ${ARGN})
  if(MSVC)
    target_compile_options(${tgt} PRIVATE
      /W4 /permissive-
      /wd4244 /wd4305 /wd4267 /wd4245
      /wd4456 /wd4457 /wd4458 /wd4459
      /wd4200)
    target_compile_definitions(${tgt} PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(GOSURVEY_ENABLE_ANALYZE)
      target_compile_options(${tgt} PRIVATE /analyze /wd6246 /external:anglebrackets /external:W0 /analyze:external-)
      if(GSW_TEST)
        target_compile_options(${tgt} PRIVATE /wd6262)
      else()
        target_compile_options(${tgt} PRIVATE "SHELL:/analyze:stacksize 65536")
      endif()
      if(GOSURVEY_ANALYZE_WERROR)
        target_compile_options(${tgt} PRIVATE /WX)
      endif()
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
set(pdfium_binaries_SOURCE_DIR    "${GOSURVEY_TP}/pdfium")
set(GLEW_ROOT                     "${GOSURVEY_TP}/glew")
set(NLOHMANN_JSON_INCLUDE         "${GOSURVEY_TP}")   # resolves <nlohmann/json.hpp>
set(PUGIXML_INCLUDE               "${GOSURVEY_TP}/pugixml/src")   # resolves <pugixml.hpp>

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

# --- LASzip: vendored upstream source, built in-tree (REQ-172 LAZ increment, issue #825 #4) ---
# The compression codec LAZ files use (third_party/laszip/VENDORED.md). Only the library sources
# upstream's src/CMakeLists.txt lists (LASZIP_SOURCES) are built — not its own CMake probes,
# example programs, or the dynamic-loading shim (dll/laszip_api.c), which would duplicate the
# symbols laszip_dll.cpp already defines when both are compiled into one static library. The
# public surface CadPointCloudLaz.cpp calls is dll/laszip_api.h's plain C API (laszip_create,
# laszip_open_reader/_writer, laszip_read/_write_point, ...) — laszip_dll.cpp implements it
# directly; no dynamic loading, no DLL boundary.
set(laszip_SOURCE_DIR "${GOSURVEY_TP}/laszip")
set(_lasz "${laszip_SOURCE_DIR}/src")
add_library(laszip STATIC
  ${_lasz}/mydefs.cpp ${_lasz}/lasmessage.cpp ${_lasz}/arithmeticdecoder.cpp
  ${_lasz}/arithmeticencoder.cpp ${_lasz}/arithmeticmodel.cpp ${_lasz}/integercompressor.cpp
  ${_lasz}/lasindex.cpp ${_lasz}/lasinterval.cpp ${_lasz}/lasquadtree.cpp
  ${_lasz}/lasreaditemcompressed_v1.cpp ${_lasz}/lasreaditemcompressed_v2.cpp
  ${_lasz}/lasreaditemcompressed_v3.cpp ${_lasz}/lasreaditemcompressed_v4.cpp
  ${_lasz}/lasreadpoint.cpp
  ${_lasz}/laswriteitemcompressed_v1.cpp ${_lasz}/laswriteitemcompressed_v2.cpp
  ${_lasz}/laswriteitemcompressed_v3.cpp ${_lasz}/laswriteitemcompressed_v4.cpp
  ${_lasz}/laswritepoint.cpp ${_lasz}/laszip.cpp ${_lasz}/laszip_dll.cpp)
  # PRIVATE "${laszip_SOURCE_DIR}/include/laszip": upstream's own src/ files quote-include
  # "laszip_common.h" unqualified (e.g. mydefs.hpp, lasmessage.hpp), expecting that header's own
  # directory on the include path, not its parent — the PUBLIC "include" dir below only satisfies
  # the <laszip/laszip_common.h>-style angle include dll/laszip_api.h itself uses.
target_include_directories(laszip PRIVATE "${_lasz}" "${laszip_SOURCE_DIR}/include/laszip")
# PUBLIC: consumers (gosurvey_domain, GoSurveyTests, ...) get laszip_api.h's "laszip/..." angle
# includes and the bare laszip_api.h/laszip_common.h/laszip_api_version.h names for free, the same
# way LibreDWG::libredwg's PUBLIC include already covers its consumers below.
target_include_directories(laszip PUBLIC
  "${laszip_SOURCE_DIR}/include" "${laszip_SOURCE_DIR}/dll")
# LASZIP_API_VERSION selects laszip_api.h's own "<laszip/...>"-prefixed include form (matching
# where this vendoring puts laszip_api_version.h/laszip_common.h); PUBLIC because any consumer
# that also includes laszip_api.h directly (CadPointCloudLaz.cpp) needs the same branch taken.
target_compile_definitions(laszip PUBLIC LASZIP_API_VERSION)
# Vendored: silence its warnings so ours stay visible (as for glew/libredwg above).
if(MSVC)
  target_compile_options(laszip PRIVATE /w /utf-8)
  target_compile_definitions(laszip PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(laszip PRIVATE -w)
endif()
# LASZIPDLL_EXPORTS: lasindex.cpp/.hpp branch on this to use lasreadpoint.hpp (vendored) instead of
# the full LASlib lasreader.hpp/laswriter.hpp (not vendored — GoSurvey only needs LASzip's own
# compression codec, not all of LASlib) for the optional LAX spatial-index path laszip_dll.cpp
# calls into. This is upstream's own documented way of building LASzip's DLL target without LASlib.
target_compile_definitions(laszip PRIVATE LASZIPDLL_EXPORTS)
target_compile_definitions(laszip INTERFACE "GOSURVEY_LASZIP_VERSION=\"3.5.0\"")
add_library(Laszip::laszip ALIAS laszip)
unset(_lasz)

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
# Vendored under third_party/md4c/; the Qt UI presents the parsed Markdown.
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
# Domain sources — shared by the Qt app, gosurvey_headless, and GoSurveyTests
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
  src/commands/TraverseActions.cpp
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
  # Issue #891: property-panel edit services shared by the Qt Properties panels
  src/commands/CadPropertyEdit.cpp
  # Issue #893: surface / surface-style edit services used by the Qt surface dialogs
  src/commands/CadSurfaceEdit.cpp

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

  # MTEXT rich-wire normalisation (pure; rendered by the Qt frontend)
  src/ui/MtextRichFormat.cpp

  # Fonts (shared lower layer — SHX stroke geometry, ADR-022)
  src/font/ShxFont.cpp

  # Survey / COGO
  src/survey/SurveyPoints.cpp

  # Traverse engine
  src/traverse/TraverseCalc.cpp
  src/traverse/TraverseLeastSquares.cpp
  src/traverse/FbkImport.cpp
  src/traverse/TraverseIo.cpp
  src/traverse/TraverseCommandLang.cpp
  src/traverse/StarNetImport.cpp
  src/traverse/Rw5Import.cpp
  src/traverse/JobXmlImport.cpp
  ${GOSURVEY_TP}/pugixml/src/pugixml.cpp

  # I/O
  src/io/GsIo.cpp
  # Coordinate systems — the one CS-MAP wrapper (REQ-358 / ADR-063)
  src/geo/CoordinateSystems.cpp
  # REQ-378 (#696 P5): the pure similarity-transform plan for Convert
  src/geo/DrawingConversion.cpp
  # REQ-360: the pure local <-> grid transformation (no CS-MAP)
  src/geo/LocalGridTransform.cpp
  # Issue #847: WGS84 ECEF <-> geodetic + local ENU rotation (pure, no CS-MAP)
  src/geo/EcefTransform.cpp
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
  # and clock passed in by the caller — so the shared test executable exercises it through gosurvey_domain.
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
  src/io/LandXmlIo.cpp
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
  # %APPDATA% no longer transitively requires GLEW.
  src/platform/AppPaths.cpp

  # Update check (REQ-077/078, ADR-029) — UpdatePrefs is reachable from AppCommandState
  src/update/UpdateCheck.cpp

  # Geometry math (util)
  src/util/geom2d.cpp
  # REQ-443 SPLINE mathematics (NURBS curve: fit / control-vertex construction, evaluation, tessellation)
  src/util/spline.cpp
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
  # REQ-313 B-rep solid kernel (ADR-045, GitHub issue #146). Pure by that ADR — no GL, no UI, no
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

  # PTS (Cyclone-style ASCII) point-cloud reader/writer (REQ-172, PTS increment — delivered after
  # E57 per `spec/file-format-specs.md` §6). In-tree, no delimiter sniffing.
  src/io/CadPointCloudPts.cpp

  # PTX (Cyclone-style ASCII, per-scan-setup) point-cloud reader/writer (REQ-172, issue #825
  # increment 2 — delivered after PTS per `spec/file-format-specs.md` §6). In-tree, no delimiter
  # sniffing.
  src/io/CadPointCloudPtx.cpp

  # LAS (ASPRS binary) point-cloud reader/writer (REQ-172, LAS increment — delivered after PTX
  # per `spec/file-format-specs.md` §6). In-tree, no LASlib dependency.
  src/io/CadPointCloudLas.cpp

  # LAZ (LASzip-compressed LAS) point-cloud reader/writer (REQ-172, LAZ increment, issue #825
  # increment 4 — the last of REQ-172's four remaining formats). Parses the LAS Public Header
  # Block itself (same byte layout as a plain LAS) and hands the compressed point-data section to
  # vendored LASzip for decode/encode (third_party/laszip/VENDORED.md).
  src/io/CadPointCloudLaz.cpp

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
# CadCommands.cpp exceeds MSVC's default COFF section limit on current hosted toolchains.
if(MSVC)
  set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/commands/CadCommands.cpp" PROPERTIES COMPILE_OPTIONS "/bigobj")
endif()
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
  ${PUGIXML_INCLUDE}
  ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(gosurvey_domain PUBLIC LibreDWG::libredwg E57Format::E57Format CsMap::csmap miniz Laszip::laszip)
if(WIN32)
  # comdlg32: file-dialog header link compatibility. ole32/shell32: <filesystem> and
  # ProcessRun's shell paths. No opengl32 — the windowed app adds it itself.
  target_link_libraries(gosurvey_domain PUBLIC comdlg32 ole32 shell32)
endif()
gosurvey_target_warnings(gosurvey_domain)
gosurvey_enable_pch(gosurvey_domain)
# Vendored sources compiled into our own target are not ours to triage (issue #977).
set_source_files_properties(
  "${GOSURVEY_TP}/pugixml/src/pugixml.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/StbImage.cpp"
  PROPERTIES COMPILE_OPTIONS "/analyze-")

# ADR-063 (c): CoordinateSystems.cpp is the only source that sees CS-MAP headers. Keep its
# platform macros and C header out of the shared precompiled header.
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

# Qt/Win32 is the sole supported desktop frontend.
include(cmake/GoSurveyQt.cmake)

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
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/drawing-templates/*"
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/wiki/*")
set(GOSURVEY_BUNDLED_RESOURCE_FILES
  "${GOSURVEY_DEFAULT_TEMPLATE}"
  "${GOSURVEY_WHATS_NEW_MD}"
  "${GOSURVEY_WHATS_NEW_BG}"
  "${GOSURVEY_SPLASH_BG}"
  ${GOSURVEY_WHATS_NEW_IMAGES}
  ${GOSURVEY_RESOURCE_DIR_FILES})
set(_GOSURVEY_RESOURCE_STAMP "${CMAKE_CURRENT_BINARY_DIR}/.gosurvey_resources-$<CONFIG>.stamp")
add_custom_command(
  OUTPUT "${_GOSURVEY_RESOURCE_STAMP}"
  DEPENDS ${GOSURVEY_BUNDLED_RESOURCE_FILES}
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GoSurveyQt>/resources"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GoSurveyQt>/resources/layouts"
  # `copy_directory` never removes files that were deleted or renamed in the source tree, so a
  # rebuild after dropping an old icon left the stale copy beside the exe (and in the installer
  # payload) forever. Wipe the mirrored dirs first so the deploy is an exact copy of source.
  COMMAND ${CMAKE_COMMAND} -E rm -rf
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/icons"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/hatches"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/blocks"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/fonts"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/drawing-templates"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/wiki"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/whats-new"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_DEFAULT_TEMPLATE}"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/default-template.gst"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_WHATS_NEW_MD}"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/whats-new.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_WHATS_NEW_BG}"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/whats-new-bg.png"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_SPLASH_BG}"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/splash-bg.png"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/icons"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/hatches"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/hatches"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/blocks"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/blocks"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/fonts"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/drawing-templates"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/drawing-templates"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/wiki"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/wiki"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${GOSURVEY_WHATS_NEW_IMG_DIR}"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/whats-new"
  COMMAND ${CMAKE_COMMAND} -E touch "${_GOSURVEY_RESOURCE_STAMP}"
  COMMENT "Stage bundled resources beside GoSurveyQt executable")
add_custom_target(gosurvey_stage_resources DEPENDS "${_GOSURVEY_RESOURCE_STAMP}")
if(TARGET GoSurveyQt)
  add_dependencies(GoSurveyQt gosurvey_stage_resources)
endif()

# CS-MAP dictionaries + US grid files (REQ-358, D-2026-09-29-d) → resources/csmap/. Their own stamp:
# ~230 MB is copied only when one of them changes, not on every resource edit.
file(GLOB_RECURSE GOSURVEY_CSMAP_DICTIONARY_FILES CONFIGURE_DEPENDS "${GOSURVEY_CSMAP_DIR}/dictionaries/*")
set(_GOSURVEY_CSMAP_STAMP "${CMAKE_CURRENT_BINARY_DIR}/.gosurvey_csmap-$<CONFIG>.stamp")
add_custom_command(
  OUTPUT "${_GOSURVEY_CSMAP_STAMP}"
  DEPENDS ${GOSURVEY_CSMAP_DICTIONARY_FILES}
  COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:GoSurveyQt>/resources/csmap"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${GOSURVEY_CSMAP_DIR}/dictionaries"
          "$<TARGET_FILE_DIR:GoSurveyQt>/resources/csmap"
  COMMAND ${CMAKE_COMMAND} -E touch "${_GOSURVEY_CSMAP_STAMP}"
  COMMENT "Stage CS-MAP dictionaries beside GoSurveyQt executable")
add_custom_target(gosurvey_stage_csmap DEPENDS "${_GOSURVEY_CSMAP_STAMP}")
if(TARGET GoSurveyQt)
  add_dependencies(GoSurveyQt gosurvey_stage_csmap)
endif()


# gosurvey_headless (REQ-203 / ADR-031): the Commands layer, drivable with no window.
#
# Links the shared domain library and headless platform seams without creating a GUI window.
# TEST-ONLY, like Catch2 (REQ-300): GoSurvey.exe neither links nor ships any of it.
# ---------------------------------------------------------------------------
option(GOSURVEY_BUILD_HEADLESS "Build the headless transcript driver (REQ-203)" ON)
if(GOSURVEY_BUILD_HEADLESS)
  add_executable(gosurvey_headless
    src/platform/HeadlessFileDialogs.cpp
    src/platform/HeadlessPdfAttach.cpp
    tests/headless/HeadlessDriver.cpp
    src/qt/QtText.cpp
    tests/headless/FuzzGenerator.cpp
    tests/headless/Minimizer.cpp
    tests/headless/FuzzMain.cpp
  )

  target_include_directories(gosurvey_headless PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qt
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
  ${PUGIXML_INCLUDE}
    ${CMAKE_BINARY_DIR}/generated
  )

  # LibreDWG::libredwg is linked explicitly because gosurvey_domain compiles LibreDwgCad.cpp
  # without exposing the lib.
  target_link_libraries(gosurvey_headless PRIVATE gosurvey_domain Qt6::Gui LibreDWG::libredwg E57Format::E57Format)

  # %DATA% in a transcript resolves to this, so a fixture is named relative to the repository
  # rather than to whoever's checkout the transcript was written in.
  target_compile_definitions(gosurvey_headless PRIVATE
    GOSURVEY_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples")

  gosurvey_target_warnings(gosurvey_headless TEST)
  add_custom_command(TARGET gosurvey_headless POST_BUILD
    COMMAND "${GOSURVEY_WINDEPLOYQT}" --no-translations --no-compiler-runtime
      --plugindir "$<TARGET_FILE_DIR:gosurvey_headless>/qt-plugins" "$<TARGET_FILE:gosurvey_headless>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_qt_root}/plugins/platforms/qoffscreen.dll"
      "$<TARGET_FILE_DIR:gosurvey_headless>/qt-plugins/platforms/qoffscreen.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt.conf" "$<TARGET_FILE_DIR:gosurvey_headless>/qt.conf"
    VERBATIM)

  # GsJsonDwgFixture (issue #264): converts between raw GoSurvey JSON (the .gst template shape,
  # REQ-175) and a .dwg carrying it as the ADR-044 trailer. Used by tools/Make-*.ps1 to author
  # samples/*.dwg test fixtures from readable JSON — not shipped, like gosurvey_headless.
  add_executable(GsJsonDwgFixture
    tools/GsJsonDwgFixture.cpp
    src/qt/QtText.cpp
    src/platform/HeadlessFileDialogs.cpp
    src/platform/HeadlessPdfAttach.cpp
  )
  # Both helper executables deploy Qt into the shared tools directory after linking.
  # Serialize them to prevent concurrent windeployqt copies from colliding.
  add_dependencies(GsJsonDwgFixture gosurvey_headless)
  target_include_directories(GsJsonDwgFixture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qt
    ${CMAKE_CURRENT_SOURCE_DIR}/src/commands
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}/src/survey
    ${CMAKE_CURRENT_SOURCE_DIR}/src/render
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/src/font
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${NLOHMANN_JSON_INCLUDE}
  ${PUGIXML_INCLUDE}
    ${CMAKE_BINARY_DIR}/generated
  )
  target_link_libraries(GsJsonDwgFixture PRIVATE gosurvey_domain Qt6::Gui LibreDWG::libredwg E57Format::E57Format)
  add_custom_command(TARGET GsJsonDwgFixture POST_BUILD
    COMMAND "${GOSURVEY_WINDEPLOYQT}" --no-translations --no-compiler-runtime
      --plugindir "$<TARGET_FILE_DIR:GsJsonDwgFixture>/qt-plugins" "$<TARGET_FILE:GsJsonDwgFixture>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_qt_root}/plugins/platforms/qoffscreen.dll"
      "$<TARGET_FILE_DIR:GsJsonDwgFixture>/qt-plugins/platforms/qoffscreen.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt.conf" "$<TARGET_FILE_DIR:GsJsonDwgFixture>/qt.conf"
    VERBATIM)
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
    tests/EcefTransformTests.cpp
    tests/NumFormatTests.cpp
    tests/AngleFormatTests.cpp
    tests/PaperSpaceTests.cpp
    tests/PaperLeaderTests.cpp
    tests/PaperTableTests.cpp
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
    # REQ-341 live section clip (GitHub #149 acceptance 6). Header-only and pure for the same reason
    # Camera is, and for one more: the plane the shader receives is NOT in world coordinates, and a
    # version that forgets that is exact at the origin and 2,196,000 ft out at state-plane
    # coordinates. That cannot be checked by looking at a window.
    tests/SectionClipTests.cpp
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
    # REQ-323 / ADR-046 (j) — FILLET. Closed-form acceptance numbers, written before the code.
    tests/ChamferEdgeTests.cpp
    tests/FilletEdgeTests.cpp
    # REQ-319 / ADR-046 amendment (i) — push/pull, the first kernel op that EDITS a solid. Its own
    # file because its subject is the PRECONDITION Validate cannot enforce, not another builder.
    tests/PushPullTests.cpp
    # REQ-333 / ADR-046 amendment (n) — moving a VERTEX or an EDGE. Its own file because its subject
    # is the DEFINITION: moving a corner and leaving everything else alone is not representable, so
    # the operation is "offset the adjacent planes and re-solve", and these cases pin that.
    tests/MoveSubObjectTests.cpp
    # REQ-332 / ADR-046 amendment (m) — rigid rotation and uniform scale. Its own file because its
    # subject is the point/direction split: a frame's origin and its axes transform by different
    # rules, and getting that wrong still produces a solid Validate accepts.
    tests/RotateSolidTests.cpp
    # REQ-351 / D-2026-09-28-f — mirror. Its own file because its subject is ORIENTATION: a naive
    # reflection leaves left-handed frames and inside-out loops, and these cases pin the repair.
    tests/MirrorSolidTests.cpp
    # REQ-320 / ADR-051 — the ACIS SAT parser (GitHub issue #299). Pure, so a hand-authored SAT
    # fixture pins the supported plane/cylinder/cone shapes and every refusal without a DWG file.
    tests/AcisSatParserTests.cpp
    src/util/AcisSatParser.cpp
  src/util/AcisSatWriter.cpp
    src/util/brep.cpp
    # REQ-315 / ADR-048: the NURBS patch math. Partition-of-unity, a bilinear patch reproducing a
    # plane, a rational arc row that is exactly on its circle, and derivative agreement with finite
    # differences are all decided here, with no window — the same reason BrepTests exists.
    tests/NurbsTests.cpp
    src/util/nurbs.cpp
    # REQ-315 acceptance: a solid survives document-payload JSON save/reopen with topology counts identical and
    # volume/area stable. BrepJson.hpp is header-only and pulls in only nlohmann/json + brep.hpp, so
    # the serializer the saver actually runs is exercised directly here (GsIo.cpp is not linkable).
    tests/BrepJsonTests.cpp
    tests/SolidPickTests.cpp
    src/util/solidpick.cpp
    # REQ-171/REQ-172, ADR-060 — the octree is pure Domain code (no file IO, no GL), so it is
    # pinned directly here, the same way BrepTests pins src/util/brep.cpp with no window.
    tests/PointCloudOctreeTests.cpp
    src/util/pointcloudoctree.cpp
    # REQ-172 — E57 reader round-trip (writes a tiny fixture E57 with libE57Format's own Simple
    # Writer, then reads it back) and malformed/missing-file refusal. Pinned directly, same as
    # AcisSatParserTests pins its own reader above.
    tests/CadPointCloudE57Tests.cpp
    src/io/CadPointCloudE57.cpp
    # REQ-172 PTS increment (delivered after E57, `spec/file-format-specs.md` §6) — in-tree ASCII
    # reader/writer round-trip (3/4/7-token line shapes) and malformed/truncated/missing-file
    # refusal. Pinned directly, same as the E57 reader above.
    tests/CadPointCloudPtsTests.cpp
    src/io/CadPointCloudPts.cpp
    # REQ-172 PTX increment (issue #825 increment 2, delivered after PTS, `spec/file-format-specs.md`
    # §6) — in-tree ASCII reader/writer round-trip (per-scan-setup 4x4 transform, including a
    # hand-checked non-identity case), "no return" filtering, and malformed/truncated/missing-file
    # refusal. Pinned directly, same as the E57 reader above.
    tests/CadPointCloudPtxTests.cpp
    src/io/CadPointCloudPtx.cpp
    # REQ-172 LAS increment (delivered after PTX, `spec/file-format-specs.md` §6) — in-tree binary
    # reader/writer round-trip (point formats 0/2, hand-checked scale+offset decode) and
    # malformed/truncated/unsupported-format/missing-file refusal. Pinned directly, same as the
    # E57 reader above.
    tests/CadPointCloudLasTests.cpp
    src/io/CadPointCloudLas.cpp
    # REQ-172 LAZ increment (issue #825 increment 4, the last of REQ-172's four remaining
    # formats) — LASzip-compressed LAS round-trip (point formats 0/2, matching the uncompressed
    # LAS convention), the LAS/LAZ cross-check (same points import equal counts and XYZ from
    # either format), and malformed/truncated/corrupt-compressed-stream/missing-file refusal.
    # Pinned directly, same as the E57 reader above.
    tests/CadPointCloudLazTests.cpp
    src/io/CadPointCloudLaz.cpp
    # ADR-060 — .gscloud out-of-core cache build/read/stamp round-trip. Pinned directly, same as
    # the octree and E57 reader above.
    tests/PointCloudCacheTests.cpp
    src/util/pointcloudcache.cpp
    # REQ-347 — least-squares cylinder-axis fit. Pinned directly, same as the octree above.
    tests/CylinderFitTests.cpp
    src/util/cylinderfit.cpp
    tests/MeshPayloadRoundTripTests.cpp
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
    # REQ-398 side slope grading. Every expected offset is hand-computed in the test from the two
    # equations that cross, because a daylight offset wrong by a slope factor is entirely plausible
    # on screen — and comparing against another approximation of the same thing would hide a
    # systematic error in both.
    tests/DaylightTests.cpp
    tests/CadTableTests.cpp
    tests/CadBlockTests.cpp
    tests/CadPipeRunTests.cpp
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
    # REQ-444 (issue #803): the scallop geometry behind REVCLOUD. Pure geom2d, like BulgeArcTests.
    tests/RevisionCloudTests.cpp
    # REQ-443 (issue #803): the SPLINE mathematics. Pure util/spline - needs spline.cpp, added below.
    tests/SplineTests.cpp
    tests/UpdateCheckTests.cpp
    # REQ-308 Start screen. RecentDrawingsTests drives the MRU store; StartScreenTests covers the
    # tab-index sentinel invariant and the thumbnail-cache naming/eviction (header-only, pure).
    tests/RecentDrawingsTests.cpp
    # REQ-336 What's New dismiss / auto-open predicates (pure).
    tests/WhatsNewLogicTests.cpp
    src/util/WhatsNewLogic.cpp
    tests/StartScreenTests.cpp
    src/io/RecentDrawings.cpp
    # REQ-373/374/382 (#696 P1)
    tests/ProjectTests.cpp
    src/io/Project.cpp
    src/io/ProjectPointDb.cpp
    src/io/ProjectPointRules.cpp
    tests/ProjectPointRulesTests.cpp
    tests/ProjectPointDbTests.cpp
    # REQ-378 (#696 P5): the pure database-merge half of Add Drawing to Project
    tests/ProjectAddDrawingTests.cpp
    src/io/ProjectAddDrawing.cpp
    # REQ-379 (#696 P6): the pure tracked-file rules
    tests/ProjectFilesTests.cpp
    src/io/ProjectFiles.cpp
    # REQ-380 (#696 P7): the pure pack / unpack
    tests/ProjectPackTests.cpp
    src/io/ProjectPack.cpp
    # REQ-381 (#696 P8): the pure turnover records
    tests/ProjectTurnoverTests.cpp
    src/io/ProjectTurnover.cpp
    tests/TelemetryPingTests.cpp
    tests/CrashReporterTests.cpp
    src/platform/CrashReporterWireReason.cpp
    tests/AuthPingTests.cpp
    tests/GsMigrateTests.cpp
    tests/GpuPreferenceTests.cpp
    # REQ-204: one deliberately-broken fixture per document invariant. This is the second present-day
    # concrete use of util/docinvariants (the headless driver is the first), which is what lets the
    # module exist at all under architecture §11.4.
    tests/DocInvariantsTests.cpp
    src/util/docinvariants.cpp
    src/util/SaveTrace.cpp
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
    src/traverse/TraverseReport.cpp
    src/traverse/FbkImport.cpp
    src/traverse/TraverseIo.cpp
    src/traverse/TraverseCommandLang.cpp
    src/traverse/StarNetImport.cpp
    src/traverse/Rw5Import.cpp
    src/traverse/JobXmlImport.cpp
    src/geo/EcefTransform.cpp
    ${GOSURVEY_TP}/pugixml/src/pugixml.cpp
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
    src/util/spline.cpp
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
    # REQ-398 side slope grading. Pure over ISurfaceQuery — no GL, no document — so a reversed cut/
    # fill sign or a daylight offset off by a slope factor is caught against hand arithmetic rather
    # than by looking at a slope in a viewport, where a plausible wrong answer looks correct.
    src/util/daylight.cpp
    # REQ-077/078 version ordering + manifest parse. Pure by ADR-029 (g) — no network, no window.
    # Wrong version ordering has NO symptom (the user is simply never offered an update), which is
    # exactly why it must be pinned by a test rather than noticed in use.
    src/update/UpdateCheck.cpp
    # REQ-079 document-payload schema migration. Pure by ADR-030 (d) — JSON in, JSON out — so the composition
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
    # For the REQ-063 payload round-trip test: GsIo.cpp itself is not linkable here (it pulls the
    # whole command layer), but the serializer it relies on is, and that is where the precision
    # risk actually lives.
    ${NLOHMANN_JSON_INCLUDE}
  ${PUGIXML_INCLUDE}
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
  # REQ-387: the PDF viewer's pure core and its PDFium wrapper (opened against generated PDFs).
  target_sources(GoSurveyTests PRIVATE
    tests/PdfViewerTests.cpp
    tests/PdfSplitTests.cpp
    tests/PdfAnnotateTests.cpp
    tests/PdfLeaderTests.cpp
    tests/PdfMeasureTests.cpp
    tests/PdfDimensionTests.cpp
    tests/PdfSnapTests.cpp
    tests/PdfScaleCheckTests.cpp
    tests/PdfDimAuditTests.cpp
    tests/PdfDimAuditPdfTests.cpp
    tests/PdfAlignTests.cpp
    tests/PdfDiffTests.cpp
    tests/PdfCoordExtractTests.cpp
    src/pdf/PdfViewerCore.cpp
    src/pdf/PdfDocument.cpp
    src/pdf/PdfSplit.cpp
    src/pdf/PdfAnnotate.cpp
    src/font/ShxFont.cpp
    src/pdf/PdfRaw.cpp
    src/pdf/PdfMeasure.cpp
    src/pdf/PdfSnap.cpp
    src/pdf/PdfScaleCheck.cpp
    src/pdf/PdfDimAudit.cpp
    src/pdf/PdfCoordExtract.cpp
    src/pdf/PdfAlign.cpp
    src/pdf/PdfDiff.cpp)
  target_link_libraries(GoSurveyTests PRIVATE Catch2::Catch2WithMain LibreDWG::libredwg E57Format::E57Format miniz pdfium Laszip::laszip)
  if(WIN32)
    add_custom_command(TARGET GoSurveyTests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${pdfium_binaries_SOURCE_DIR}/bin/pdfium.dll"
              "$<TARGET_FILE_DIR:GoSurveyTests>/pdfium.dll"
      COMMENT "Copy pdfium.dll beside GoSurveyTests")
  endif()

  include(Catch)

  # ---------------------------------------------------------------------------
  # Command-layer cases share the Catch2 executable and existing domain library.
  # These tests run without a GUI window.
  # ---------------------------------------------------------------------------
  target_sources(GoSurveyTests PRIVATE
    src/platform/HeadlessFileDialogs.cpp
    src/platform/HeadlessPdfAttach.cpp
    tests/CadFieldTests.cpp
    tests/CadSnapTests.cpp
    # DRAPE needs the surface stores and ProcessCommandLineSubmit, both in gosurvey_domain,
    # so it rides with GoSurveyTests like the other command tests above.
    tests/DrapeCommandTests.cpp
    # Issue #916: shared point-cloud export and import-progress behavior.
    tests/PointCloudExportTests.cpp
    tests/LandXmlExportTests.cpp
    # PADSOLID needs the surface stores, the brep kernel and ProcessCommandLineSubmit, so it rides
    # with GoSurveyTests like the other command tests above.
    tests/PadSolidCommandTests.cpp
    tests/GsIoDimAngularTests.cpp
    tests/GsIoPipeRunTests.cpp
    tests/GsIoViewportCameraTests.cpp
    tests/GsMigrateLegacyBreaklineTests.cpp
    tests/ViewportUcsTests.cpp
    # REQ-154 / issue #371 — ORTHO squaring to a vertical (Front/Left/Right-style) UCS. Needs
    # gosurvey_domain for ApplyOrthoConstrainFromAnchor, so it rides with GoSurveyTests like
    # ViewportUcsTests.cpp above.
    tests/OrthoUcsConstrainTests.cpp
    # REQ-318 increment 2 (issue #148) — the sub-object SELECTION: its store, the expiring
    # reference, mutual exclusion and cross-solid depth order. Needs gosurvey_domain for
    # SubmitSubObjectPick / ExpireSubObjectSelection, like ViewportUcsTests above.
    tests/SubObjectSelectionTests.cpp
    # REQ-341 (code review on #478): the section clip's command-layer half — per-tab state, picks
    # and snaps that honour it, SECTION's selection guard, and the indicator's sizing. Needs
    # gosurvey_domain, like SubObjectSelectionTests above.
    tests/SectionClipStateTests.cpp
    # REQ-060 (issue #148 Phase 5 slice 4b) — the TRANSLATE gizmo: the skew-line solve, the anchor,
    # and the two negatives (a click that misses every handle, an empty selection). Needs
    # gosurvey_domain for SubmitGizmoClick / ApplyTranslationToSelection.
    tests/GizmoTranslateTests.cpp
    # GitHub issue #564 s1: orbited ZOOM EXTENTS, driven through ProcessPendingViewportZoom (domain).
    tests/ZoomExtents3dTests.cpp
    # GitHub issue #564 s2: depth-ordered hover/click resolution (ResolveViewportPick, domain).
    tests/ViewportPickDepthTests.cpp
    # REQ-060 rotate/scale handles (TASK-232, slice 4c). Its own file because its subject is the
    # AGREEMENT: a gizmo drag and the function the typed command calls, compared coordinate for
    # coordinate — which is the acceptance itself rather than a proxy for it.
    tests/GizmoRotateScaleTests.cpp
    # REQ-333 increment 2 (TASK-234) — the vertex and edge grips. Its own file because its subject is
    # the handle COUNTS (each a statement about degrees of freedom) and the rule that no handle is
    # drawn where the kernel would refuse the drag.
    tests/GizmoSubObjectMoveTests.cpp
    tests/LibreDwgCadTests.cpp
    tests/CadBlockImportTests.cpp
    tests/SurveyLabelMeasureTests.cpp
    # REQ-375 (#696 P2): project settings resolve against AppCommandState (EnforceProjectSettings), so
    # this rides with gosurvey_domain like CadBlockImportTests.
    tests/ProjectSettingsTests.cpp
    tests/ProjectPointsSyncTests.cpp
    # REQ-383 (#696 P9): delete / number-conflict / paste / unsaved-close warnings against AppCommandState
    tests/ProjectWarningsTests.cpp
    # REQ-378 (#696 P5): Add Drawing to Project end to end + Convert (needs AppCommandState, CS-MAP, LibreDWG)
    tests/ProjectAddFlowTests.cpp
    # REQ-379 (#696 P6): attach prompt, save-time record and open-time re-attach against AppCommandState
    tests/ProjectFilesFlowTests.cpp
    # issue #486 increment B2 (REQ-347) — the PIPERUN command layer: StartPipeRunCommand /
    # HandlePipeRunTextInput / SubmitPipeRunViewportPick live in CadCommands.cpp, so this rides
    # with gosurvey_domain, like CadBlockImportTests above.
    tests/CadPipeRunCommandTests.cpp
    # REQ-350 / TASK-275 — the Pipe Fittings palette's filtering and grouping rules. Pure functions,
    # but they live in CadBlocks.cpp beside the library scan they filter, so this rides with
    # GoSurveyTests (gosurvey_domain) for the same reason CadPipeRunCommandTests above does.
    tests/CadPipePaletteTests.cpp
    # Issue #891: property-panel edit services and the shared right-click classifier
    tests/CadPropertyEditTests.cpp
    tests/CadSurfaceEditTests.cpp
    # REQ-351 / D-2026-09-28-f — MOVE / ROTATE / SCALE / MIRROR / COPY / ARRAY on a pipe run.
    tests/ModifyPipeRunTests.cpp
    # REQ-352 / D-2026-09-28-g — layer and colour edits on solids and pipe runs.
    tests/SolidLayerColorTests.cpp
    # REQ-353 / D-2026-09-28-i — pipe colour by nominal size (issue #564 section 7).
    tests/PipeSizePaletteTests.cpp
    # REQ-354 / D-2026-09-28-j — the dynamic input shows its mode (issue #564 section 5).
    tests/DynInputTests.cpp
    tests/ModelingRibbonTests.cpp
    # GitHub issue #762 — regression guard for Home/Insert ribbon wiring gaps.
    tests/RibbonWiringGapTests.cpp
    # REQ-356 / D-2026-09-29-a — CHPROP, MATCHPROP, LAYMCUR, the current colour (issue #575).
    tests/PropertyCommandTests.cpp
    # REQ-370 / D-2026-10-05-a — box-selection live preview.
    tests/BoxSelectionPreviewTests.cpp
    # REQ-357 / D-2026-09-29-b,c — the Drawing Settings window (issue #582 increment 1).
    tests/DrawingSettingsTests.cpp
    # REQ-358: the CS-MAP wrapper + the zone as drawing data (loads the staged dictionary).
    tests/CoordinateSystemsTests.cpp
    # REQ-359 / D-2026-09-29-e: the Geolocation tab's commands and the Position Marker.
    tests/GeolocationTests.cpp
    # REQ-360: the Drawing Settings Transformation tab (local <-> grid).
    tests/TransformationTests.cpp
    # REQ-361 / D-2026-09-29-f: the Object Layers tab and every creation path that uses it.
    tests/ObjectLayersTests.cpp
    # REQ-362 / D-2026-09-29-g: reading an AutoCAD / Civil 3D GEODATA (write deferred to #590).
    tests/GeoDataReadTests.cpp
    # REQ-363 / D-2026-09-30-b / ADR-064: the online base map (issue #583 increment 1).
    tests/OnlineMapTests.cpp
    # issue #166 — the TRIM cut-line rubber preview. Needs gosurvey_domain (it calls into
    # CadCommands.cpp's trim helpers), so it rides with gosurvey_domain.
    tests/TrimLinePreviewTests.cpp
    # issue #399 increment 1 — 3D Line-vs-Line TRIM via a pick ray. Same reason as
    # TrimLinePreviewTests above (needs gosurvey_domain for SubmitTrimViewportPick).
    tests/Trim3DLineLineTests.cpp
    # issue #399 increment 4 — smart (drawn-line) TRIM in true 3D. Same reason as above.
    tests/Trim3DDrawnLineTests.cpp
    # REQ-105 (issue #382) — DIST. Needs gosurvey_domain for ProcessCommandLineSubmit /
    # StartDistCommand, like TrimLinePreviewTests above.
    tests/DistCommandTests.cpp
    # Issue #785 — regression test that every row in the command registry still has code behind
    # it. Needs gosurvey_domain for ProcessCommandLineSubmit / RegisteredCommandPrimariesForTest.
    tests/CommandRegistryHandlerTests.cpp
    # OFFSET distance-first flow + select/side loop (REQ-103), and double-precision offset math at
    # state-plane coordinate magnitude. Needs gosurvey_domain for ProcessCommandLineSubmit /
    # SubmitViewportPick, like DistCommandTests above.
    tests/OffsetCommandTests.cpp
    tests/DefaultTemplateOriginTests.cpp
    # GitHub issue #396 — EXTRUDE's typed height must take its sign from the live preview
    # direction. Needs gosurvey_domain for HandleExtrudeTextInput, like DistCommandTests above.
    tests/ExtrudeTypedHeightDirectionTests.cpp
    # GitHub issue #396 (PRESSPULL addendum) — PRESSPULL was brought in line with EXTRUDE's own
    # select-target / wait-distance flow and widened to closed 2D shapes. Needs gosurvey_domain
    # for HandlePressPullTextInput / StartPressPullCommand, like DistCommandTests above.
    tests/PressPullTypedDistanceDirectionTests.cpp
    # Overlapping pick disambiguation — depth tie-break when SEL is off.
    tests/PickDisambiguationTests.cpp
  )
  target_include_directories(GoSurveyTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qt
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
  ${PUGIXML_INCLUDE}
    ${CMAKE_BINARY_DIR}/generated
  )
  target_link_libraries(GoSurveyTests PRIVATE gosurvey_domain Catch2::Catch2WithMain LibreDWG::libredwg E57Format::E57Format)
  target_compile_definitions(GoSurveyTests PRIVATE
    GOSURVEY_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/samples"
    GOSURVEY_CSMAP_DICTIONARY_DIR="$<TARGET_FILE_DIR:GoSurveyQt>/resources/csmap")
  add_dependencies(GoSurveyTests gosurvey_stage_csmap)  # REQ-358 tests load the staged copy
  gosurvey_target_warnings(GoSurveyTests TEST)
  gosurvey_enable_pch(GoSurveyTests)
  # Domain implementations are linked once via gosurvey_domain, not compiled again.
  get_target_property(_test_sources GoSurveyTests SOURCES)
  list(REMOVE_ITEM _test_sources ${GOSURVEY_DOMAIN_SOURCES})
  list(REMOVE_DUPLICATES _test_sources)
  set_property(TARGET GoSurveyTests PROPERTY SOURCES "${_test_sources}")
  # Catch2's 2,500+ cases are deterministic and sequential within the runner. Register the suite
  # as one CTest process to avoid paying process startup for every case; Catch still reports the
  # failing case and supports direct filters (`GoSurveyTests "test name"`). The full runner was
  # verified locally before batching (all cases pass in one process).
  add_test(NAME unit.catch COMMAND GoSurveyTests)
  set_tests_properties(unit.catch PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:GoSurveyQt>")

  # Register the independent, isolated Qt shards before the transcript cases so CTest can run both
  # groups at once instead of letting the faster headless list postpone the GUI work.
  include(cmake/GoSurveyQtTests.cmake)

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
                       --out "${GOSURVEY_ARTIFACT_DIR}/headless/${_name}")
      # Transcripts write only under the build tree (CON-07, REQ-200) and must not depend on the
      # directory ctest happens to start them in.
      set_tests_properties(headless.${_name} PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endforeach()

    # Oracles held DISABLED rather than deleted or WILL_FAIL'ed.
    #
    # ***** THE LIST IS EMPTY. *****  Keep the mechanism and this note: it is the record of what the
    # convention is for, and the next oracle to find a real defect goes here rather than being
    # deleted to get a green suite. TASK-056 made this call for the separate payload comparison test
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
                     --out "${GOSURVEY_ARTIFACT_DIR}/headless/fuzz-smoke"
                     --timeout-ms 120000)
    set_tests_properties(headless.fuzz-smoke PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    # The document-payload comparison was briefly a separate, DISABLED ctest case here while issues
    # #56 and #57 were open. Both are fixed, so the check is live again and now lives inside the
    # transcript itself as `EXPECT SAMEFILE` — one artifact instead of two, and it needs no
    # knowledge of the build layout. Disabling it was the right call at the time (deleting would
    # have lost the oracle; WILL_FAIL would have turned the fix into a red build), and re-enabling
    # it is the regression test for both issues.
  endif()
endif()

if(GOSURVEY_BUILD_TESTS)
  add_test(NAME build.output-layout COMMAND "${CMAKE_COMMAND}"
    "-DRUNTIME_DIR=$<TARGET_FILE_DIR:GoSurveyQt>"
    -P "${CMAKE_SOURCE_DIR}/tests/cmake/BuildOutputLayout.cmake")
  # Issue #918: the deployed Qt set has no ImGui/GLFW and launches from a clean copy.
  add_test(NAME qt.deployment
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify-qt-deployment.ps1"
      -RuntimeDirectory "$<TARGET_FILE_DIR:GoSurveyQt>" -WorkDirectory "${GOSURVEY_ARTIFACT_DIR}"
      -TimeoutSec 120)
  set_tests_properties(qt.deployment PROPERTIES TIMEOUT 300)
  # Issue #919: keep sign-off claims tied to current workflow tests and tracker items.
  add_test(NAME qt.parity-ledger
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/verify-qt-parity-ledger.ps1")
endif()

# Developer-only helpers remain available to ctest/tools without adding app executables.
# Keep each helper's Qt deployment in its own directory: Ninja may link these targets in parallel,
# and their POST_BUILD windeployqt steps otherwise race while copying the same plugins/DLLs.
foreach(_helper gosurvey_headless GsJsonDwgFixture GoSurveyTests)
  if(TARGET ${_helper})
    set_target_properties(${_helper} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/${_helper}")
    foreach(_config IN LISTS CMAKE_CONFIGURATION_TYPES)
      string(TOUPPER "${_config}" _upper)
      set_target_properties(${_helper} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_upper} "${CMAKE_BINARY_DIR}/tools/${_config}/${_helper}")
    endforeach()
  endif()
endforeach()
