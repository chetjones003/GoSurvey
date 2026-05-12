cmake_minimum_required(VERSION 3.20)
project(GoSurvey VERSION 0.2.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenGL REQUIRED)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)

FetchContent_Declare(glew
  URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(glfw imgui glew)

set(GLEW_ROOT "${glew_SOURCE_DIR}")
if(EXISTS "${glew_SOURCE_DIR}/glew-2.2.0/src/glew.c")
  set(GLEW_ROOT "${glew_SOURCE_DIR}/glew-2.2.0")
endif()

add_library(glew_static STATIC ${GLEW_ROOT}/src/glew.c)
target_include_directories(glew_static PUBLIC ${GLEW_ROOT}/include)
target_compile_definitions(glew_static PUBLIC GLEW_STATIC)
target_link_libraries(glew_static PUBLIC OpenGL::GL)

set(IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui_backend STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_backend PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imgui_SOURCE_DIR}/misc/cpp
)
target_link_libraries(imgui_backend PUBLIC glfw)
target_compile_definitions(imgui_backend PUBLIC
  IMGUI_DEFINE_MATH_OPERATORS
)

add_executable(GoSurvey
  src/main.cpp
  src/SplashScreen.cpp
  src/AppIcon.cpp
  src/ViewportRenderer.cpp
  src/CadUi.cpp
  src/CadCommands.cpp
  src/MtextRichFormat.cpp
  src/CadSnap.cpp
  src/SurveyPoints.cpp
  src/SurveyCsv.cpp
  src/DxfIo.cpp
  src/DxfColors.cpp
  src/WinFileDialogs.cpp
)

target_include_directories(GoSurvey PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(GoSurvey PRIVATE
  imgui_backend
  glfw
  OpenGL::GL
  glew_static
)
if(WIN32)
  target_link_libraries(GoSurvey PRIVATE comdlg32)
endif()
target_compile_definitions(GoSurvey PRIVATE GLEW_STATIC)

if(WIN32)
  set_target_properties(GoSurvey PROPERTIES OUTPUT_NAME "GoSurvey-${PROJECT_VERSION}" WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(GoSurvey PRIVATE /ENTRY:mainCRTStartup)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(GoSurvey PRIVATE "LINKER:/ENTRY:mainCRTStartup")
  endif()
endif()

if(MSVC)
  target_compile_options(GoSurvey PRIVATE /W4 /permissive-)
else()
  target_compile_options(GoSurvey PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/icons")
  # Merge icons into the output dir without wiping extra files there (e.g. user-added main_logo.png).
  # copy_directory can behave like a full sync on some generators and remove destination-only assets.
  file(GLOB _icon_src_files "${CMAKE_CURRENT_SOURCE_DIR}/icons/*")
  if(_icon_src_files)
    set(_icon_post_cmds COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:GoSurvey>/icons")
    foreach(_abs IN LISTS _icon_src_files)
      if(IS_DIRECTORY "${_abs}")
        continue()
      endif()
      get_filename_component(_leaf "${_abs}" NAME)
      list(APPEND _icon_post_cmds
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_abs}"
                "$<TARGET_FILE_DIR:GoSurvey>/icons/${_leaf}")
    endforeach()
    add_custom_command(TARGET GoSurvey POST_BUILD ${_icon_post_cmds})
  endif()
endif()
