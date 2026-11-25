# Vendored dependencies (third_party/). No downloads: each folder's VENDORED.md records its upstream
# version and how to update it.
set(GOSURVEY_TP "${CMAKE_SOURCE_DIR}/third_party")
set(PUGIXML_INCLUDE "${GOSURVEY_TP}/pugixml/src")
find_package(OpenGL REQUIRED)

# Vendored source builds with its warnings off, so ours stay visible.
function(gosurvey_vendored_library name)
  add_library(${name} STATIC ${ARGN})
  target_compile_options(${name} PRIVATE /w)
endfunction()

# A vendored prebuilt static library: gosurvey_prebuilt_library(<name> <lib> [INCLUDE dir] [LINK libs...])
function(gosurvey_prebuilt_library name lib)
  cmake_parse_arguments(P "" "INCLUDE" "LINK" ${ARGN})
  if(NOT EXISTS "${lib}")
    message(FATAL_ERROR "Missing vendored library ${lib}. See the VENDORED.md beside it.")
  endif()
  add_library(${name} STATIC IMPORTED GLOBAL)
  set_target_properties(${name} PROPERTIES IMPORTED_LOCATION "${lib}")
  if(P_INCLUDE)
    set_target_properties(${name} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${P_INCLUDE}")
  endif()
  if(P_LINK)
    set_property(TARGET ${name} APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${P_LINK})
  endif()
endfunction()

# LibreDWG: GoSurvey's own copy of tag 0.14 (ADR-041). Library sources only; src/config.h is committed.
set(GOSURVEY_LIBREDWG_DIR "${GOSURVEY_TP}/libredwg")
set(_src "${GOSURVEY_LIBREDWG_DIR}/src")   # reused below for each library's source folder
gosurvey_vendored_library(libredwg
  ${_src}/bits.c ${_src}/codepages.c ${_src}/common.c ${_src}/logging.c ${_src}/classes.c
  ${_src}/objects.c ${_src}/decode.c ${_src}/decode2.c ${_src}/decode_r11.c ${_src}/decode_r2007.c
  ${_src}/dwg.c ${_src}/hash.c ${_src}/dwg_api.c ${_src}/dynapi.c ${_src}/dxfclasses.c ${_src}/free.c
  ${_src}/geom.c ${_src}/out_dxf.c ${_src}/encode.c ${_src}/encode2.c
  ${_src}/in_dxf.c ${_src}/reedsolomon.c ${_src}/print.c)
set_property(TARGET libredwg PROPERTY C_STANDARD 99)
target_compile_options(libredwg PRIVATE /utf-8)
target_compile_definitions(libredwg PRIVATE _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS DEBUG_CLASSES
  INTERFACE "GOSURVEY_LIBREDWG_VERSION=\"0.14\"")
target_include_directories(libredwg PRIVATE "${_src}" PUBLIC "${GOSURVEY_LIBREDWG_DIR}/include")
target_link_libraries(libredwg PUBLIC ws2_32)
add_library(LibreDWG::libredwg ALIAS libredwg)

# LASzip 3.5.0: the LAZ codec. laszip_dll.cpp implements the plain C API in dll/laszip_api.h;
# LASZIPDLL_EXPORTS builds it without the rest of LASlib.
set(_src "${GOSURVEY_TP}/laszip/src")
gosurvey_vendored_library(laszip
  ${_src}/mydefs.cpp ${_src}/lasmessage.cpp ${_src}/arithmeticdecoder.cpp ${_src}/arithmeticencoder.cpp
  ${_src}/arithmeticmodel.cpp ${_src}/integercompressor.cpp ${_src}/lasindex.cpp ${_src}/lasinterval.cpp
  ${_src}/lasquadtree.cpp ${_src}/lasreaditemcompressed_v1.cpp ${_src}/lasreaditemcompressed_v2.cpp
  ${_src}/lasreaditemcompressed_v3.cpp ${_src}/lasreaditemcompressed_v4.cpp ${_src}/lasreadpoint.cpp
  ${_src}/laswriteitemcompressed_v1.cpp ${_src}/laswriteitemcompressed_v2.cpp
  ${_src}/laswriteitemcompressed_v3.cpp ${_src}/laswriteitemcompressed_v4.cpp ${_src}/laswritepoint.cpp
  ${_src}/laszip.cpp ${_src}/laszip_dll.cpp)
target_compile_options(laszip PRIVATE /utf-8)
target_compile_definitions(laszip PRIVATE _CRT_SECURE_NO_WARNINGS LASZIPDLL_EXPORTS
  PUBLIC LASZIP_API_VERSION INTERFACE "GOSURVEY_LASZIP_VERSION=\"3.5.0\"")
target_include_directories(laszip
  PRIVATE "${_src}" "${GOSURVEY_TP}/laszip/include/laszip"
  PUBLIC "${GOSURVEY_TP}/laszip/include" "${GOSURVEY_TP}/laszip/dll")
add_library(Laszip::laszip ALIAS laszip)
unset(_src)

# CS-MAP r3078 (ADR-063). Its headers are added only to CoordinateSystems.cpp (cmake/Engine.cmake);
# its dictionaries and US grid files are staged as resources/csmap (cmake/GoSurveyQt.cmake).
set(GOSURVEY_CSMAP_DIR "${GOSURVEY_TP}/csmap")
gosurvey_prebuilt_library(csmap "${GOSURVEY_CSMAP_DIR}/lib/win-x64/csmap.lib")
add_library(CsMap::csmap ALIAS csmap)

# Xerces-C 3.3.0 and libE57Format 3.4.0 (E57 point clouds, ADR-042).
gosurvey_prebuilt_library(xerces_c "${GOSURVEY_TP}/xerces-c/lib/win-x64/xerces-c_3.lib"
  INCLUDE "${GOSURVEY_TP}/xerces-c/include" LINK ws2_32)
gosurvey_prebuilt_library(e57format "${GOSURVEY_TP}/libe57format/lib/win-x64/E57Format.lib"
  INCLUDE "${GOSURVEY_TP}/libe57format/include" LINK xerces_c)
add_library(E57Format::E57Format ALIAS e57format)

# PDFium (chromium/7857), a prebuilt DLL.
add_library(pdfium SHARED IMPORTED)
set_target_properties(pdfium PROPERTIES
  IMPORTED_LOCATION "${GOSURVEY_TP}/pdfium/bin/pdfium.dll"
  IMPORTED_IMPLIB "${GOSURVEY_TP}/pdfium/lib/pdfium.dll.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${GOSURVEY_TP}/pdfium/include")

# Copy pdfium.dll beside an executable that loads it.
function(gosurvey_copy_pdfium target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:${target}>")
endfunction()

gosurvey_vendored_library(glew_static "${GOSURVEY_TP}/glew/src/glew.c")
target_include_directories(glew_static PUBLIC "${GOSURVEY_TP}/glew/include")
target_compile_definitions(glew_static PUBLIC GLEW_STATIC)
target_link_libraries(glew_static PUBLIC OpenGL::GL)

gosurvey_vendored_library(md4c "${GOSURVEY_TP}/md4c/md4c.c")        # What's New Markdown (ADR-056)
target_include_directories(md4c PUBLIC "${GOSURVEY_TP}/md4c")

gosurvey_vendored_library(miniz "${GOSURVEY_TP}/miniz/miniz.c")     # .gspack ZIP files (ADR-066)
target_include_directories(miniz PUBLIC "${GOSURVEY_TP}/miniz")

gosurvey_vendored_library(gosurvey_pugixml "${PUGIXML_INCLUDE}/pugixml.cpp")
target_include_directories(gosurvey_pugixml PUBLIC "${PUGIXML_INCLUDE}")

# OpenXLSX: .xlsx read/write for Extract Data and Link Data (REQ-467, REQ-468).
file(GLOB GOSURVEY_OPENXLSX_SOURCES CONFIGURE_DEPENDS "${GOSURVEY_TP}/OpenXLSX/sources/*.cpp")
gosurvey_vendored_library(gosurvey_openxlsx ${GOSURVEY_OPENXLSX_SOURCES})
set(GOSURVEY_OPENXLSX_INCLUDES
  "${GOSURVEY_TP}/OpenXLSX" "${GOSURVEY_TP}/OpenXLSX/headers"
  "${GOSURVEY_TP}/OpenXLSX/generated" "${GOSURVEY_TP}/OpenXLSX/shims")
target_include_directories(gosurvey_openxlsx PRIVATE ${GOSURVEY_OPENXLSX_INCLUDES})
target_link_libraries(gosurvey_openxlsx PRIVATE gosurvey_pugixml miniz)
# Both use standard C++ spellings (such as `not`) that MSVC accepts only in conformance mode.
target_compile_options(gosurvey_openxlsx PRIVATE /permissive-)
target_compile_options(gosurvey_pugixml PRIVATE /permissive-)
