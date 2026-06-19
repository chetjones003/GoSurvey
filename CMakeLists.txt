cmake_minimum_required(VERSION 3.20)
project(GoSurvey VERSION 0.4.0 LANGUAGES CXX C RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
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
  src/ui/SplashScreen.cpp
  src/ui/ImGuiLayout.cpp
  src/ui/MtextRichFormat.cpp
  src/ui/FontRegistry.cpp
  src/ui/ShxFont.cpp
  src/ui/PdfAttachDialog.cpp

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
  src/io/DxfColors.cpp
  src/io/PdfPlot.cpp

  # PDF underlay
  src/pdf/PdfAttach.cpp

  # Platform
  src/platform/AppIcon.cpp
  src/platform/WinFileDialogs.cpp

  # Geometry math (util)
  src/util/geom2d.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${NLOHMANN_JSON_INCLUDE}
)
target_link_libraries(GoSurvey PRIVATE
  imgui_backend
  glfw
  OpenGL::GL
  glew_static
  pdfium
)
if(WIN32)
  target_link_libraries(GoSurvey PRIVATE comdlg32)
  target_sources(GoSurvey PRIVATE src/platform/WinFrameControls.cpp)

  # Embed the application icon so Windows shows it in Explorer, the taskbar,
  # the Start Menu, and desktop shortcuts.
  target_sources(GoSurvey PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.rc"
  )

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
  set_target_properties(GoSurvey PROPERTIES OUTPUT_NAME "GoSurvey-${PROJECT_VERSION}" WIN32_EXECUTABLE TRUE)
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
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GOSURVEY_DEFAULT_TEMPLATE}"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/default-template.gs"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
          "$<TARGET_FILE_DIR:GoSurvey>/resources/icons"
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
    src/traverse/TraverseCalc.cpp
    src/traverse/TraverseLeastSquares.cpp
    src/traverse/FbkImport.cpp
  )
  target_include_directories(GoSurveyTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/traverse
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
