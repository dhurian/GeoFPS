cmake_minimum_required(VERSION 3.24)

project(GeoFPS VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "Suppress dependency deprecation warnings" FORCE)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "Suppress dependency developer warnings" FORCE)

option(GEOFPS_BUILD_TESTS "Build tests" OFF)

include(FetchContent)

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

# Dependencies
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# GLAD without Python generation step.
# This uses a repo that already contains generated glad.c / glad.h files,
# so users do not need Python or Jinja2 just to build the engine.
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/premake-libs/glad.git
  GIT_TAG master
)
FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
  FetchContent_Populate(glad)
  add_library(glad STATIC
    ${glad_SOURCE_DIR}/src/glad.c
  )
  target_include_directories(glad PUBLIC
    ${glad_SOURCE_DIR}/include
  )
endif()

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()

FetchContent_Declare(
  tinygltf
  GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
  GIT_TAG v2.9.5
)
FetchContent_GetProperties(tinygltf)
if(NOT tinygltf_POPULATED)
  FetchContent_Populate(tinygltf)
endif()

# Dear ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9b-docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PRIVATE glfw)
target_compile_definitions(imgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD)

find_package(OpenGL REQUIRED)

file(GLOB_RECURSE GEOFPS_SOURCES CONFIGURE_DEPENDS
  src/*.cpp
)

add_executable(GeoFPS ${GEOFPS_SOURCES})

target_include_directories(GeoFPS PRIVATE
  src
  ${stb_SOURCE_DIR}
  ${tinygltf_SOURCE_DIR}
)
target_link_libraries(GeoFPS PRIVATE
  glfw
  glm
  glad
  imgui
  OpenGL::GL
)

if(APPLE)
  target_link_libraries(GeoFPS PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

if(MSVC)
  target_compile_options(GeoFPS PRIVATE /W4 /permissive-)
else()
  target_compile_options(GeoFPS PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(GeoFPS PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_custom_command(TARGET GeoFPS POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:GeoFPS>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets
          ${CMAKE_BINARY_DIR}/assets
)
