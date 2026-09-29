cmake_minimum_required(VERSION 3.16)
project(gfx_node_editor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---- GLFW ----
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ---- GLM (header only) ----
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# ---- Dear ImGui, docking branch (no CMakeLists upstream, so we grab sources and build manually) ----
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.90.9-docking
)
FetchContent_MakeAvailable(imgui)


# ---- imnodes (also plain sources) ----
# Deliberately NOT using FetchContent_MakeAvailable here: imnodes ships its
# own CMakeLists.txt that does find_package(imgui), expecting an installed
# imgui package rather than the source-only copy we fetched above. We just
# want the source files, so populate without add_subdirectory-ing into it.
FetchContent_Declare(
  imnodes
  GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
  GIT_TAG master
)
FetchContent_GetProperties(imnodes)
if(NOT imnodes_POPULATED)
  FetchContent_Populate(imnodes)
endif()

# ---- GLAD loader ----
# Not fetched automatically: generate it yourself at https://glad.dav1d.de/
#   Language: C/C++, Specification: OpenGL, API gl: Version 3.3, Profile: Core
#   Download the zip, then copy so you have:
#     external/glad/include/glad/glad.h
#     external/glad/include/KHR/khrplatform.h
#     external/glad/src/glad.c
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/glad/src/glad.c")
  message(FATAL_ERROR "Missing external/glad — generate it from https://glad.dav1d.de/ (see README.md) and place it under external/glad/")
endif()

add_executable(gfx_node_editor
  main.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imnodes_SOURCE_DIR}/imnodes.cpp
  external/glad/src/glad.c
)

target_include_directories(gfx_node_editor PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imnodes_SOURCE_DIR}
  external/glad/include
)

target_link_libraries(gfx_node_editor PRIVATE glfw glm::glm)

# ---- Fonts ----
# Roboto (UI) and Cousine (monospace, for matrices) ship with Dear ImGui.
# They are copied next to the executable; the source path is a fallback.
target_compile_definitions(gfx_node_editor PRIVATE MVP_FONT_DIR="${imgui_SOURCE_DIR}/misc/fonts")
add_custom_command(TARGET gfx_node_editor POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:gfx_node_editor>/fonts
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf
          ${imgui_SOURCE_DIR}/misc/fonts/Cousine-Regular.ttf
          $<TARGET_FILE_DIR:gfx_node_editor>/fonts
)

if(WIN32)
  target_link_libraries(gfx_node_editor PRIVATE opengl32)
elseif(APPLE)
  target_link_libraries(gfx_node_editor PRIVATE "-framework OpenGL")
else()
  target_link_libraries(gfx_node_editor PRIVATE GL dl pthread)
endif()
