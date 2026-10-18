find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OpenGLWidgets)

add_library(gosurvey_qt_frontend STATIC
  src/qt/EditorWindow.cpp
  src/qt/EditorRibbon.cpp
  src/qt/EditorChrome.cpp
  src/qt/QtAppearance.cpp
  src/qt/QtText.cpp
  src/qt/QtAnnotations.cpp
  src/qt/EditorViewport.cpp
  src/qt/EditorProjects.cpp
  src/qt/EditorProjectSettings.cpp
  src/qt/EditorProjectAttach.cpp
  src/qt/EditorUpdate.cpp
  src/qt/EditorDialogs.cpp
  src/qt/EditorPoints.cpp
  src/qt/EditorProperties.cpp
  src/qt/EditorSurfaces.cpp
  src/qt/EditorSurfaceManager.cpp
  src/qt/EditorSurfaceStyles.cpp
  src/qt/EditorContextMenu.cpp
  src/qt/EditorPointTransfer.cpp
  src/qt/AdjustmentWindow.cpp
  src/qt/PdfWindow.cpp
  src/qt/FailureReport.cpp
  src/render/ViewportRenderer.cpp
  src/pdf/PdfAttach.cpp
  src/pdf/PdfDocument.cpp
  src/pdf/PdfViewerCore.cpp
  src/pdf/PdfAnnotate.cpp
  src/pdf/PdfMeasure.cpp
  src/pdf/PdfScaleCheck.cpp
  src/pdf/PdfRaw.cpp
  src/pdf/PdfSplit.cpp
  src/io/PdfPlot.cpp
  src/traverse/TraverseReport.cpp
  src/platform/WinFileDialogs.cpp
  src/platform/HttpFetch.cpp
  src/platform/OAuthListener.cpp
  src/platform/CredentialStore.cpp
  src/auth/AuthService.cpp
  src/auth/AuthPing.cpp
  src/update/UpdateService.cpp
  # REQ-080 usage ping; REQ-425 crash and startup-failure reports
  src/telemetry/TelemetryPing.cpp
  src/telemetry/TelemetryService.cpp
  src/platform/CrashReporter.cpp
  src/platform/CrashReporterWireReason.cpp
  src/platform/StartupFailure.cpp
  src/platform/FailureReportUi.cpp
)
target_link_libraries(gosurvey_qt_frontend PUBLIC
  gosurvey_domain Qt6::Widgets Qt6::OpenGLWidgets OpenGL::GL glew_static pdfium
  comdlg32 ole32 shell32 Windowscodecs winhttp bcrypt ws2_32 advapi32)
target_include_directories(gosurvey_qt_frontend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/qt")
target_include_directories(gosurvey_qt_frontend PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/telemetry")
target_compile_definitions(gosurvey_qt_frontend PUBLIC NOMINMAX QT_NO_KEYWORDS)
gosurvey_target_warnings(gosurvey_qt_frontend)
add_executable(GoSurveyQt WIN32 src/qt/main.cpp resources/icons/app.rc)
target_link_libraries(GoSurveyQt PRIVATE gosurvey_qt_frontend)
gosurvey_target_warnings(GoSurveyQt)
if(MSVC)
  target_compile_options(GoSurveyQt PRIVATE /utf-8)
  target_compile_options(gosurvey_qt_frontend PRIVATE /utf-8)
endif()

# Use the same installed kit for runtime deployment; no downloads or guessed DLL paths.
get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
find_program(GOSURVEY_WINDEPLOYQT windeployqt HINTS "${_qt_bin}" REQUIRED NO_DEFAULT_PATH)
add_custom_command(TARGET GoSurveyQt POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:GoSurveyQt>"
  COMMAND "${GOSURVEY_WINDEPLOYQT}" --no-translations --no-compiler-runtime
    --plugindir "$<TARGET_FILE_DIR:GoSurveyQt>/qt-plugins"
    "$<TARGET_FILE:GoSurveyQt>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt.conf" "$<TARGET_FILE_DIR:GoSurveyQt>/qt.conf"
  VERBATIM)

add_dependencies(GoSurveyQt gosurvey_stage_resources gosurvey_stage_csmap)
