find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
add_executable(GoSurveyQtTests tests/QtEditorTests.cpp)
set_target_properties(GoSurveyQtTests PROPERTIES AUTOMOC ON)
target_link_libraries(GoSurveyQtTests PRIVATE gosurvey_qt_frontend Qt6::Test)
gosurvey_assert_no_imgui_in_link_graph(GoSurveyQtTests)
target_compile_definitions(GoSurveyQtTests PRIVATE GOSURVEY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" GOSURVEY_TEST_ARTIFACT_DIR="${GOSURVEY_ARTIFACT_DIR}")
gosurvey_target_warnings(GoSurveyQtTests TEST)
if(MSVC)
  # The single Qt test translation unit exceeds MSVC's default COFF section limit.
  target_compile_options(GoSurveyQtTests PRIVATE /utf-8 /bigobj)
endif()
add_custom_command(TARGET GoSurveyQtTests POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:GoSurveyQtTests>"
  VERBATIM)
add_dependencies(GoSurveyQtTests GoSurveyQt)
# The suite opens real windows. Each shard runs on its own private invisible desktop and gets
# isolated temp, settings, and artifact paths. Qt's -functions output determines the current test
# list at runtime; every slot is assigned to exactly one shard, so adding a test cannot silently
# leave it out of CTest.
set(GOSURVEY_QT_TEST_SHARDS 6 CACHE STRING "Number of isolated Qt test processes")
if(NOT GOSURVEY_QT_TEST_SHARDS MATCHES "^[1-9][0-9]*$" OR GOSURVEY_QT_TEST_SHARDS GREATER 16)
  message(FATAL_ERROR "GOSURVEY_QT_TEST_SHARDS must be between 1 and 16")
endif()
set(_qt_editor_shards ${GOSURVEY_QT_TEST_SHARDS})
math(EXPR _qt_editor_last_shard "${_qt_editor_shards} - 1")
foreach(_shard RANGE 0 ${_qt_editor_last_shard})
  add_test(NAME qt.editor.shard-${_shard}
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-qt-test-shard.ps1"
      -Exe "$<TARGET_FILE:GoSurveyQtTests>"
      -ShardIndex ${_shard} -ShardCount ${_qt_editor_shards}
      -TimeoutSec 900 -WorkingDirectory "${GOSURVEY_ARTIFACT_DIR}")
  set_tests_properties(qt.editor.shard-${_shard} PROPERTIES
    TIMEOUT 920 COST 100 WORKING_DIRECTORY "${GOSURVEY_ARTIFACT_DIR}")
endforeach()
