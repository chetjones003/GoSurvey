# Version and build identity (ADR-029, ADR-074). Generated files go in the build tree.
set(GOSURVEY_VERSION_SUFFIX "" CACHE STRING "Prerelease suffix appended to the version")
set(GOSURVEY_VERSION_BUILD "0" CACHE STRING "Fourth field of the Windows VERSIONINFO quad")
set(GOSURVEY_BUILD_CHANNEL "dev" CACHE STRING "Crash-report channel: stable, beta or dev")
if(NOT GOSURVEY_BUILD_CHANNEL MATCHES "^(stable|beta|dev)$")
  message(FATAL_ERROR "GOSURVEY_BUILD_CHANNEL must be stable, beta or dev (got '${GOSURVEY_BUILD_CHANNEL}').")
endif()
set(GOSURVEY_GIT_COMMIT "" CACHE STRING "Short commit recorded in crash reports (empty: read from git)")

if(NOT GOSURVEY_GIT_COMMIT)
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _commit
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    # Re-run configure when HEAD moves: watch HEAD, its reflog, the current branch's ref file and
    # packed-refs, so a commit is seen even with reflogs turned off.
    execute_process(COMMAND "${GIT_EXECUTABLE}" symbolic-ref --quiet HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _branch_ref
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    foreach(_ref HEAD logs/HEAD ${_branch_ref} packed-refs)
      execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --git-path ${_ref}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _path
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(_path)
        get_filename_component(_path "${_path}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(EXISTS "${_path}")
          set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_path}")
        endif()
      endif()
    endforeach()
  endif()
  if(NOT _commit)
    set(_commit "unknown")
  endif()
  set(GOSURVEY_GIT_COMMIT "${_commit}")
endif()

configure_file(src/app/Version.hpp.in "${CMAKE_BINARY_DIR}/generated/Version.hpp" @ONLY)
# The commit lives in its own header so a new commit recompiles only CrashReporter.cpp.
configure_file(src/app/BuildCommit.hpp.in "${CMAKE_BINARY_DIR}/generated/BuildCommit.hpp" @ONLY)

# The same version for Inno Setup, so a hand-run installer/GoSurvey.iss matches the build.
file(WRITE "${CMAKE_BINARY_DIR}/generated/version.iss"
"; GENERATED FILE - do not edit. Written by cmake/Version.cmake.\n\
#define AppVersion \"${PROJECT_VERSION}${GOSURVEY_VERSION_SUFFIX}\"\n\
#define AppVersionQuad \"${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${GOSURVEY_VERSION_BUILD}\"\n")
