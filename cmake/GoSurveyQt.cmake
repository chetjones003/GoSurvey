find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OpenGLWidgets)

add_executable(GoSurveyQt WIN32
  src/qt/main.cpp
  src/qt/EditorWindow.cpp
  src/qt/EditorViewport.cpp
  src/qt/EditorProjects.cpp
  src/qt/AdjustmentWindow.cpp
  src/qt/PdfWindow.cpp
  src/render/ViewportRenderer.cpp
  src/pdf/PdfAttach.cpp
  src/pdf/PdfDocument.cpp
  src/pdf/PdfViewerCore.cpp
  src/platform/WinFileDialogs.cpp
  src/platform/AppIcon.cpp
  resources/icons/app.rc
)
target_link_libraries(GoSurveyQt PRIVATE
  gosurvey_domain Qt6::Widgets Qt6::OpenGLWidgets OpenGL::GL glew_static pdfium
  glfw comdlg32 ole32 shell32 Windowscodecs)
target_include_directories(GoSurveyQt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/qt")
target_compile_definitions(GoSurveyQt PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN QT_NO_KEYWORDS)
gosurvey_target_warnings(GoSurveyQt)
if(MSVC)
  target_compile_options(GoSurveyQt PRIVATE /utf-8)
endif()

# Use the same installed kit for runtime deployment; no downloads or guessed DLL paths.
get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
find_program(GOSURVEY_WINDEPLOYQT windeployqt HINTS "${_qt_bin}" REQUIRED NO_DEFAULT_PATH)
add_custom_command(TARGET GoSurveyQt POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:GoSurveyQt>"
  COMMAND "${GOSURVEY_WINDEPLOYQT}" --no-translations --no-compiler-runtime
    "$<TARGET_FILE:GoSurveyQt>"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resources" "$<TARGET_FILE_DIR:GoSurveyQt>/resources"
  VERBATIM)
