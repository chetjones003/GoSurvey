find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
add_executable(GoSurveyQtTests tests/QtEditorTests.cpp)
set_target_properties(GoSurveyQtTests PROPERTIES AUTOMOC ON)
target_link_libraries(GoSurveyQtTests PRIVATE gosurvey_qt_frontend Qt6::Test)
target_compile_definitions(GoSurveyQtTests PRIVATE GOSURVEY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" GOSURVEY_TEST_ARTIFACT_DIR="${GOSURVEY_ARTIFACT_DIR}")
gosurvey_target_warnings(GoSurveyQtTests)
if(MSVC)
  target_compile_options(GoSurveyQtTests PRIVATE /utf-8)
endif()
add_custom_command(TARGET GoSurveyQtTests POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:GoSurveyQtTests>"
  VERBATIM)
add_dependencies(GoSurveyQtTests GoSurveyQt)
# The suite opens real windows, so ctest runs it on a private invisible desktop: nothing flashes on
# the user's screen, no focus is stolen, and the launcher kills a hung run. Start
# GoSurveyQtTests.exe directly for a visible run (e.g. to watch a failing test).
add_test(NAME qt.editor
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-qt-tests-hidden.ps1"
    -Exe "$<TARGET_FILE:GoSurveyQtTests>" -TimeoutSec 280
    -WorkingDirectory "${GOSURVEY_ARTIFACT_DIR}")
set_tests_properties(qt.editor PROPERTIES TIMEOUT 300 WORKING_DIRECTORY "${GOSURVEY_ARTIFACT_DIR}")
