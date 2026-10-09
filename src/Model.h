#pragma once
// Model files (OBJ, PLY, STL, glTF, FBX, ...) loaded with Assimp into the
// app's position + color meshes. The shader is unlit, so a simple light is
// baked into the vertex colors to keep the shape readable.

#include "Renderer.h"

#include <string>
#include <glm/glm.hpp>

struct ModelAsset {
    bool ok = false;
    std::string error;           // why loading failed, when !ok
    Mesh triangles, lines, points;
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};  // in the file's own units
    glm::mat4 fit{1.0f};         // centers the model and scales it into a 1x1x1 box
    int vertexCount = 0, triangleCount = 0;
};

// Loads `path` on first use and caches it. Needs the GL context to be current.
// Never returns null; check `ok`.
ModelAsset& GetModel(const std::string& path);

// Drops the cached copy so the next GetModel reads the file again.
void ReloadModel(const std::string& path);

// File patterns Assimp can read, e.g. "*.obj *.ply *.stl", for file dialogs.
std::string ModelFilePatterns();

// Asks for a model file with the native dialog. Returns "" if cancelled or if
// no dialog is available (on Linux that needs zenity or kdialog).
std::string OpenModelDialog();

// Just the file name, for labels.
std::string FileNameOf(const std::string& path);
