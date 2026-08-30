# REQ-170 / ADR-041 — GNU LibreDWG compiled in this tree (MSVC + Ninja) and
# linked statically into GoSurvey. FetchContent downloads into the build
# directory (REQ-200); the pin is the 0.13.3 annotated release.
#
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

set(LIBREDWG_GIT_TAG "0.13.3")
# Peeled commit of the 0.13.3 annotated tag (REQ-200: not a moving branch).
set(LIBREDWG_GIT_COMMIT "97c7225596c17430b82fd0161e7eff6beb5b1034")

set(LIBREDWG_LIBONLY ON CACHE BOOL "GoSurvey links the library, not dwgread.exe" FORCE)
set(DISABLE_WERROR ON CACHE BOOL "LibreDWG /WX is not compatible with MSVC + this tree" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "Do not let LibreDWG set GLOBAL IPO" FORCE)
set(LIBREDWG_DISABLE_WRITE OFF CACHE BOOL "" FORCE)
# JSON in/out is not required for DWG increment 1; upstream turns BUILD_SHARED_LIBS
# off when JSON is disabled, which is the static-in-tree path we want.
set(LIBREDWG_DISABLE_JSON ON CACHE BOOL "" FORCE)

FetchContent_Declare(libredwg
  GIT_REPOSITORY https://github.com/LibreDWG/libredwg.git
  GIT_TAG ${LIBREDWG_GIT_TAG}
  GIT_SHALLOW TRUE
)

FetchContent_GetProperties(libredwg)
if(NOT libredwg_POPULATED)
  FetchContent_Populate(libredwg)
  # Upstream's `if(EXISTS ".version")` is a relative path (often the *parent*
  # build dir). Write both so PACKAGE_VERSION is 0.13.3, not `git describe`
  # of GoSurvey.
  file(WRITE "${libredwg_SOURCE_DIR}/.version" "${LIBREDWG_GIT_TAG}\n")
  file(WRITE "${CMAKE_BINARY_DIR}/.version" "${LIBREDWG_GIT_TAG}\n")

  set(_gosurvey_saved_build_shared "${BUILD_SHARED_LIBS}")
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory("${libredwg_SOURCE_DIR}" "${libredwg_BINARY_DIR}" EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS "${_gosurvey_saved_build_shared}")
endif()

if(NOT TARGET libredwg)
  message(FATAL_ERROR "LibreDWG did not create target 'libredwg' (MSVC name). Pin=${LIBREDWG_GIT_TAG}")
endif()

if(NOT TARGET LibreDWG::libredwg)
  add_library(LibreDWG::libredwg ALIAS libredwg)
endif()

if(WIN32)
  target_link_libraries(libredwg PUBLIC ws2_32)
endif()

target_compile_definitions(libredwg INTERFACE
  "GOSURVEY_LIBREDWG_VERSION=\"${LIBREDWG_GIT_TAG}\"")

message(STATUS "LibreDWG ${LIBREDWG_GIT_TAG} (${LIBREDWG_GIT_COMMIT}) static → libredwg")
