cmake_minimum_required(VERSION 3.16)
project(gfx_node_editor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Static C/C++ runtime ----
# Set before any dependency is added so every target (GLFW included) agrees.
# MSVC: /MT (/MTd in Debug) instead of the DLL runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
# GCC/Clang on Linux and MinGW: bundle libstdc++ and libgcc into the binary.
# macOS is skipped: Apple doesn't support a static libc++, and the system one
# ships with every macOS install anyway.
if(NOT MSVC AND NOT APPLE)
  if(MINGW)
    add_link_options(-static)
  else()
    add_link_options(-static-libstdc++ -static-libgcc)
  endif()
endif()

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

# ---- Assimp: model loading (OBJ, PLY, STL, glTF, FBX, ...) ----
# Built as a static library with only the importers below, so the build stays
# reasonably quick. To support another format, add its ASSIMP_BUILD_<NAME>_IMPORTER.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_VIEW OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)  # bundled zlib, so no libz.so dependency
set(USE_STATIC_CRT ON CACHE BOOL "" FORCE)     # MSVC: /MT, like the rest of the app
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
foreach(fmt OBJ PLY STL GLTF FBX COLLADA 3DS OFF 3MF X BLEND DXF LWO)
  set(ASSIMP_BUILD_${fmt}_IMPORTER ON CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG v6.0.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(assimp)

# ---- portable-file-dialogs: native "Open file" dialog (header only) ----
FetchContent_Declare(
  pfd
  GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
  GIT_TAG c12ea8c9a727f5320a2b4570aee863bbede2a204
)
FetchContent_GetProperties(pfd)
if(NOT pfd_POPULATED)
  FetchContent_Populate(pfd)
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

# ---- App sources ----
set(APP_SOURCES
  src/main.cpp
  src/Model.cpp
  src/NodeGraph.cpp
  src/Renderer.cpp
  src/UI.cpp
  src/Views.cpp
)

add_executable(gfx_node_editor
  ${APP_SOURCES}
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imnodes_SOURCE_DIR}/imnodes.cpp
  external/glad/src/glad.c
)

target_include_directories(gfx_node_editor PRIVATE
  src
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imnodes_SOURCE_DIR}
  ${pfd_SOURCE_DIR}
  external/glad/include
)

target_link_libraries(gfx_node_editor PRIVATE glfw glm::glm assimp::assimp)

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
  target_link_libraries(gfx_node_editor PRIVATE opengl32 comdlg32 ole32 shell32)
elseif(APPLE)
  target_link_libraries(gfx_node_editor PRIVATE "-framework OpenGL")
else()
  target_link_libraries(gfx_node_editor PRIVATE GL dl pthread)
endif()
