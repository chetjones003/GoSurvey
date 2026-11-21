cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)  # CMAKE_MSVC_RUNTIME_LIBRARY controls /MD

# sccache (CI) needs embedded /Z7 debug info, which CMP0141 must select before project().
if(POLICY CMP0141 AND (CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER))
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
endif()

# Stable releases use this version; CI passes GOSURVEY_BETA_VERSION for beta builds (ADR-074).
set(GOSURVEY_STABLE_VERSION "0.8.0")
set(_gosurvey_version "${GOSURVEY_STABLE_VERSION}")
if(NOT "${GOSURVEY_BETA_VERSION}" STREQUAL "")
  set(_gosurvey_version "${GOSURVEY_BETA_VERSION}")
endif()
project(GoSurvey VERSION "${_gosurvey_version}" LANGUAGES CXX C RC)

if(NOT MSVC)
  message(FATAL_ERROR "GoSurvey builds with MSVC only. Run build.bat or configure from an MSVC developer shell.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# The vendored xerces-c_3.lib exists only for the Release DLL CRT, so every configuration uses /MD.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# A -DCMAKE_CXX_FLAGS=... first configure replaces CMake's default flags and drops /EHsc and /GR
# (issue #375). Restore them.
if(NOT CMAKE_CXX_FLAGS MATCHES "/EH")
  string(APPEND CMAKE_CXX_FLAGS " /EHsc")
endif()
if(NOT CMAKE_CXX_FLAGS MATCHES "/GR")
  string(APPEND CMAKE_CXX_FLAGS " /GR")
endif()

# Every build links a PDB so crash reports match an archived one (REQ-425). /Z7 keeps sccache
# working; /OPT:REF /OPT:ICF restore the size optimisations /DEBUG turns off.
add_compile_options(/Z7)
add_link_options(/DEBUG /OPT:REF /OPT:ICF)

# Layout: applications in build/ (build/debug/ for Debug); everything else in the binary dir.
set(GOSURVEY_RUNTIME_DIR "${CMAKE_SOURCE_DIR}/build" CACHE PATH "Application output directory")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  string(APPEND GOSURVEY_RUNTIME_DIR "/debug")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${GOSURVEY_RUNTIME_DIR}")
foreach(_config IN LISTS CMAKE_CONFIGURATION_TYPES)
  string(TOUPPER "${_config}" _upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_upper} "${CMAKE_SOURCE_DIR}/build/${_config}")
endforeach()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")
# Put a helper executable in <binary dir>/tools/<name>/ (per configuration for Visual Studio).
function(gosurvey_tool_output_dir target)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/${target}")
  foreach(_config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${_config}" _upper)
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${_upper} "${CMAKE_BINARY_DIR}/tools/${_config}/${target}")
  endforeach()
endfunction()
set(GOSURVEY_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/../../artifacts/${CMAKE_BUILD_TYPE}")
file(MAKE_DIRECTORY "${GOSURVEY_ARTIFACT_DIR}")

option(GOSURVEY_BUILD_HEADLESS "Build the headless transcript driver and fixture tool" ON)
option(GOSURVEY_BUILD_TESTS "Build the test suites" ON)

include(cmake/Version.cmake)      # Version.hpp, BuildCommit.hpp, version.iss
include(cmake/Compiler.cmake)     # warnings, /analyze, precompiled header
include(cmake/ThirdParty.cmake)   # vendored libraries under third_party/
include(cmake/Engine.cmake)       # the GoSurvey system libraries
include(cmake/GoSurveyQt.cmake)   # GoSurvey.exe, its resources and Qt deployment
if(GOSURVEY_BUILD_HEADLESS)
  include(cmake/Tools.cmake)      # gosurvey_headless, GsJsonDwgFixture
endif()
if(GOSURVEY_BUILD_TESTS)
  include(cmake/Tests.cmake)
endif()
