# Compiler settings for GoSurvey's own targets. Vendored code uses gosurvey_vendored_library instead.

option(GOSURVEY_USE_PCH "Use the precompiled header (cmake/gosurvey_pch.hpp). OFF checks every file compiles without it." ON)
option(GOSURVEY_ENABLE_ANALYZE "Add MSVC /analyze to our own targets (REQ-324)." OFF)
option(GOSURVEY_ANALYZE_WERROR "With GOSURVEY_ENABLE_ANALYZE, fail the build on any warning (issue #977)." OFF)

function(gosurvey_enable_pch target)
  if(GOSURVEY_USE_PCH)
    target_precompile_headers(${target} PRIVATE "${CMAKE_SOURCE_DIR}/cmake/gosurvey_pch.hpp")
  endif()
endfunction()

# /W4 /permissive- with the warnings this codebase triggers by design disabled (coding standards §11):
#   4244 4305  double -> float: geometry is stored in float after a double world -> local subtraction
#   4267 4245  size_t and signed/unsigned at Win32 and third-party API boundaries
#   4456-4459  shadowing (style, not a bug)
#   4200       zero-sized arrays in LibreDWG's public dwg.h
# /analyze triage is recorded in issue #977. Pass TEST for test targets: Catch2 bodies keep large
# fixtures on the stack, so the stack-size check (C6262) is off for them.
function(gosurvey_target_warnings target)
  cmake_parse_arguments(W "TEST" "" "" ${ARGN})
  target_compile_options(${target} PRIVATE
    /W4 /permissive- /wd4244 /wd4305 /wd4267 /wd4245 /wd4456 /wd4457 /wd4458 /wd4459 /wd4200)
  target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  if(GOSURVEY_ENABLE_ANALYZE)
    target_compile_options(${target} PRIVATE
      /analyze /wd6246 /external:anglebrackets /external:W0 /analyze:external-)
    if(W_TEST)
      target_compile_options(${target} PRIVATE /wd6262)
    else()
      target_compile_options(${target} PRIVATE "SHELL:/analyze:stacksize 65536")
    endif()
    if(GOSURVEY_ANALYZE_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  endif()
endfunction()
