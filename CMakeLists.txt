cmake_minimum_required(VERSION 3.20)
project(GoSurvey VERSION 0.5.0 LANGUAGES CXX C RC)

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

set(IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui_backend STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_backend PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imgui_SOURCE_DIR}/misc/cpp
)
target_link_libraries(imgui_backend PUBLIC glfw)
target_compile_definitions(imgui_backend PUBLIC
  IMGUI_DEFINE_MATH_OPERATORS
)

add_executable(GoSurvey
  # Application entry
  src/app/main.cpp

  # Command system
  src/commands/CadCommands.cpp
  src/commands/CadLinetype.cpp
  src/commands/EntityId.cpp
  src/survey/PointGroupRule.cpp
  src/commands/CadCoordinateFrame.cpp

  # Viewport system
  src/viewport/CadSnap.cpp
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
  src/ui/MtextRichFormat.cpp
  src/ui/RichTextEdit.cpp
  src/ui/FontRegistry.cpp
  src/ui/ShxDraw.cpp
  src/ui/ViewCube.cpp
  src/ui/PdfAttachDialog.cpp

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
  src/io/UserPrefs.cpp
  src/io/SurveyCsv.cpp
  src/io/DxfIo.cpp
  src/io/DwgIo.cpp
  src/io/DwgProbe.cpp
  src/io/DxfColors.cpp
  src/io/PdfPlot.cpp

  # PDF underlay
  src/pdf/PdfAttach.cpp

  # Platform
  src/platform/AppIcon.cpp
  src/platform/WinFileDialogs.cpp
  src/platform/ProcessRun.cpp
  src/platform/HttpFetch.cpp

  # Update check (REQ-077/078, ADR-029)
  src/update/UpdateCheck.cpp
  src/update/UpdateService.cpp

  # Geometry math (util)
  src/util/geom2d.cpp
  src/util/curveintersect.cpp
  src/util/meshgeom.cpp
  src/util/gltfimport.cpp
  src/util/stlimport.cpp
  src/io/DwgMeshConvert.cpp
  src/io/GsMigrate.cpp
  src/util/tinbuild.cpp
  src/util/benchscene.cpp
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
    tests/SurveyCsvValidateTests.cpp
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
    tests/UpdateCheckTests.cpp
    tests/GsMigrateTests.cpp
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
  )
  target_include_directories(GoSurveyTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/traverse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io
    # For the REQ-063 `.gs` round-trip test: GsIo.cpp itself is not linkable here (it pulls the
    # whole command layer), but the serializer it relies on is, and that is where the precision
    # risk actually lives.
    ${NLOHMANN_JSON_INCLUDE}
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
endif()
