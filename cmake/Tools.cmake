# Developer tools built on the command layer with no window. Not shipped.
# Uses gosurvey_qt_text and the windeployqt path from cmake/GoSurveyQt.cmake, included first.

# Each tool deploys Qt into its own folder, so parallel windeployqt runs never collide.
function(gosurvey_tool name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE gosurvey_domain gosurvey_headless_seams gosurvey_qt_text)
  target_include_directories(${name} PRIVATE "${PUGIXML_INCLUDE}")
  gosurvey_tool_output_dir(${name})
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND "${GOSURVEY_WINDEPLOYQT}" --no-translations --no-compiler-runtime
      --plugindir "$<TARGET_FILE_DIR:${name}>/qt-plugins" "$<TARGET_FILE:${name}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${GOSURVEY_QT_ROOT_DIR}/plugins/platforms/qoffscreen.dll"
      "$<TARGET_FILE_DIR:${name}>/qt-plugins/platforms/qoffscreen.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_SOURCE_DIR}/cmake/qt.conf" "$<TARGET_FILE_DIR:${name}>/qt.conf"
    VERBATIM)
endfunction()

# gosurvey_headless (ADR-031): runs command transcripts and the fuzzer (registered in cmake/Tests.cmake).
gosurvey_tool(gosurvey_headless
  tests/headless/HeadlessDriver.cpp
  tests/headless/FuzzGenerator.cpp
  tests/headless/Minimizer.cpp
  tests/headless/FuzzMain.cpp
  tests/TestSoundGuard.cpp)
# %DATA% in a transcript resolves here, so fixtures are named relative to the repository.
target_compile_definitions(gosurvey_headless PRIVATE GOSURVEY_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/samples")
gosurvey_target_warnings(gosurvey_headless TEST)

# GsJsonDwgFixture: converts readable JSON to and from samples/*.dwg test fixtures (tools/Make-*.ps1).
gosurvey_tool(GsJsonDwgFixture tools/GsJsonDwgFixture.cpp)
