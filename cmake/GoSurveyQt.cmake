# GoSurvey.exe: the Qt frontend, its bundled resources and its Qt runtime.
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OpenGLWidgets)

# Qt text layout, shared by the app and the headless tools.
add_library(gosurvey_qt_text STATIC src/qt/QtText.cpp)
target_include_directories(gosurvey_qt_text PUBLIC src/qt)
target_link_libraries(gosurvey_qt_text PUBLIC GoSurvey::Document Qt6::Gui)
target_compile_options(gosurvey_qt_text PRIVATE /utf-8)
gosurvey_target_warnings(gosurvey_qt_text)

add_library(gosurvey_qt_frontend STATIC
  src/qt/DrawingDocumentTabBar.cpp
  src/qt/EditorWindow.cpp
  src/qt/EditorInput.cpp
  src/qt/EditorHelp.cpp
  src/qt/EditorRibbon.cpp
  src/qt/EditorChrome.cpp
  src/qt/QtAppearance.cpp
  src/qt/QtSplash.cpp
  src/qt/QtViewCube.cpp
  src/qt/QtAnnotations.cpp
  src/qt/EditorViewport.cpp
  src/qt/EditorProjects.cpp
  src/qt/EditorProjectSettings.cpp
  src/qt/EditorProjectAttach.cpp
  src/qt/EditorPointClouds.cpp
  src/qt/EditorUpdate.cpp
  src/qt/EditorDialogs.cpp
  src/qt/StyleEditors.cpp
  src/qt/EditorPoints.cpp
  src/qt/EditorPointGroups.cpp
  src/qt/EditorAlignmentLayout.cpp
  src/qt/EditorAlignmentCreate.cpp
  src/qt/EditorViewFrameDialog.cpp
  src/qt/EditorSheetsDialog.cpp
  src/qt/EditorSampleLinesDialog.cpp
  src/qt/EditorSectionSheetsDialog.cpp
  src/qt/EditorAlignmentStyle.cpp
  src/qt/EditorPointTableDialog.cpp
  src/qt/EditorLayerManager.cpp
  src/qt/EditorProperties.cpp
  src/qt/EditorSurfaces.cpp
  src/qt/EditorSurfaceManager.cpp
  src/qt/EditorSurfaceStyles.cpp
  src/qt/EditorContextMenu.cpp
  src/qt/EditorPointTransfer.cpp
  src/qt/EditorBlockAuthoring.cpp
  src/qt/EditorPipePalette.cpp
  src/qt/EditorGradingPalette.cpp
  src/qt/AdjustmentWindow.cpp
  src/qt/AdjustmentEditor.cpp
  src/qt/PdfWindow.cpp
  src/qt/PdfWindowCompare.cpp
  src/qt/PdfScaleUi.cpp
  src/qt/PdfWindowDimensions.cpp
  src/qt/FailureReport.cpp
  src/renderer/src/ViewportRenderer.cpp
  src/renderer/src/ViewportHatchPass.cpp
  src/pdf/src/PdfAttach.cpp
  src/platform/WinFileDialogs.cpp
  src/platform/MapTilePlatform.cpp
  src/platform/HttpFetch.cpp
  src/update/UpdateService.cpp
  src/telemetry/TelemetryPing.cpp
  src/telemetry/TelemetryService.cpp
  src/platform/CrashReporter.cpp
  src/platform/StartupFailure.cpp
  src/platform/FailureReportUi.cpp)
target_include_directories(gosurvey_qt_frontend PRIVATE src/telemetry)
target_link_libraries(gosurvey_qt_frontend PUBLIC
  gosurvey_domain gosurvey_qt_text Qt6::Widgets Qt6::OpenGLWidgets OpenGL::GL glew_static pdfium
  Windowscodecs winhttp bcrypt ws2_32 advapi32)
target_compile_definitions(gosurvey_qt_frontend PUBLIC NOMINMAX QT_NO_KEYWORDS)
target_compile_options(gosurvey_qt_frontend PRIVATE /utf-8)
gosurvey_target_warnings(gosurvey_qt_frontend)

add_executable(GoSurveyQt WIN32 src/qt/main.cpp resources/icons/app.rc)
set_target_properties(GoSurveyQt PROPERTIES OUTPUT_NAME GoSurvey)
target_link_libraries(GoSurveyQt PRIVATE gosurvey_qt_frontend GoSurvey::Engine)
target_compile_options(GoSurveyQt PRIVATE /utf-8)
gosurvey_target_warnings(GoSurveyQt)

# Deploy the Qt runtime from the configured kit (no downloads, no guessed paths).
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(GOSURVEY_QT_BIN "${_qmake}" DIRECTORY)
get_filename_component(GOSURVEY_QT_ROOT_DIR "${GOSURVEY_QT_BIN}/.." ABSOLUTE)
find_program(GOSURVEY_WINDEPLOYQT windeployqt HINTS "${GOSURVEY_QT_BIN}" REQUIRED NO_DEFAULT_PATH)
add_custom_command(TARGET GoSurveyQt POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:GoSurveyQt>"
  COMMAND "${GOSURVEY_WINDEPLOYQT}" --no-translations --no-compiler-runtime
    --plugindir "$<TARGET_FILE_DIR:GoSurveyQt>/qt-plugins" "$<TARGET_FILE:GoSurveyQt>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_SOURCE_DIR}/cmake/qt.conf" "$<TARGET_FILE_DIR:GoSurveyQt>/qt.conf"
  VERBATIM)

# --- Bundled resources --------------------------------------------------------------------------
# Staged by a stamp so an edited resource is copied even when nothing relinks. Folders are wiped
# first so a deleted source file does not linger beside the exe or in the installer.
set(_res "${CMAKE_SOURCE_DIR}/resources")
set(_out "$<TARGET_FILE_DIR:GoSurveyQt>/resources")
set(GOSURVEY_RESOURCE_FILES default-template.gst whats-new.md whats-new-bg.png splash-bg.png qt-splash-bg.png)
set(GOSURVEY_RESOURCE_DIRS icons hatches blocks fonts drawing-templates wiki whats-new)
foreach(_file IN LISTS GOSURVEY_RESOURCE_FILES ITEMS wiki/Home.md)
  if(NOT EXISTS "${_res}/${_file}")
    message(FATAL_ERROR "resources/${_file} is required.")
  endif()
endforeach()

set(_depends "")
set(_commands COMMAND "${CMAKE_COMMAND}" -E make_directory "${_out}/layouts")
foreach(_file IN LISTS GOSURVEY_RESOURCE_FILES)
  list(APPEND _depends "${_res}/${_file}")
  list(APPEND _commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_res}/${_file}" "${_out}/${_file}")
endforeach()
foreach(_dir IN LISTS GOSURVEY_RESOURCE_DIRS)
  file(GLOB_RECURSE _files CONFIGURE_DEPENDS "${_res}/${_dir}/*")
  list(APPEND _depends ${_files})
  list(APPEND _commands
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_out}/${_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_res}/${_dir}" "${_out}/${_dir}")
endforeach()
# A manifest of the file list, rewritten only when it changes, so deleting a file also restages.
function(gosurvey_write_manifest path)
  string(REPLACE ";" "\n" _list "${ARGN}")
  file(CONFIGURE OUTPUT "${path}" CONTENT "${_list}\n" @ONLY)
endfunction()
gosurvey_write_manifest("${CMAKE_BINARY_DIR}/resources.manifest" ${_depends})
set(_stamp "${CMAKE_BINARY_DIR}/.gosurvey_resources-$<CONFIG>.stamp")
add_custom_command(OUTPUT "${_stamp}" ${_commands}
  COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
  DEPENDS ${_depends} "${CMAKE_BINARY_DIR}/resources.manifest"
  COMMENT "Stage bundled resources beside GoSurvey.exe")
add_custom_target(gosurvey_stage_resources DEPENDS "${_stamp}")

# CS-MAP dictionaries and US grid files (~230 MB) have their own stamp so they copy only when changed.
file(GLOB_RECURSE _files CONFIGURE_DEPENDS "${GOSURVEY_CSMAP_DIR}/dictionaries/*")
gosurvey_write_manifest("${CMAKE_BINARY_DIR}/csmap.manifest" ${_files})
set(_stamp "${CMAKE_BINARY_DIR}/.gosurvey_csmap-$<CONFIG>.stamp")
add_custom_command(OUTPUT "${_stamp}" DEPENDS ${_files} "${CMAKE_BINARY_DIR}/csmap.manifest"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_out}/csmap"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${GOSURVEY_CSMAP_DIR}/dictionaries" "${_out}/csmap"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
  COMMENT "Stage CS-MAP dictionaries beside GoSurvey.exe")
add_custom_target(gosurvey_stage_csmap DEPENDS "${_stamp}")

add_dependencies(GoSurveyQt gosurvey_stage_resources gosurvey_stage_csmap)
