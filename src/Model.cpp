#ifdef _WIN32
#define NOMINMAX  // portable-file-dialogs includes windows.h, whose min/max macros break std::max
#endif
#include "Model.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <portable-file-dialogs.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>
#include <memory>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

// Color used when the file has neither vertex colors nor a material color.
static const glm::vec3 kDefaultModelColor(0.72f, 0.76f, 0.84f);

// A fixed light from above and to the side, in the model's own space. It turns
// with the object, which is fine here: it only has to make the shape readable.
// Files don't agree on which way their triangles wind, so the normal's sign
// can't be trusted, and both sides of a face are lit alike.
static float Shade(glm::vec3 n) {
    static const glm::vec3 light = glm::normalize(glm::vec3(0.45f, 0.8f, 0.4f));
    float len = glm::length(n);
    if (len < 1e-8f) return 0.75f;
    return 0.4f + 0.6f * std::fabs(glm::dot(n / len, light));
}

static glm::vec3 MaterialColor(const aiScene* scene, const aiMesh* mesh) {
    if (mesh->mMaterialIndex >= scene->mNumMaterials) return kDefaultModelColor;
    const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
    aiColor4D c;
    if (mat->Get(AI_MATKEY_BASE_COLOR, c) == AI_SUCCESS ||   // glTF / PBR
        mat->Get(AI_MATKEY_COLOR_DIFFUSE, c) == AI_SUCCESS)
        return {c.r, c.g, c.b};
    return kDefaultModelColor;
}

static glm::mat4 ToGlm(const aiMatrix4x4& m) {
    // Assimp is row-major, GLM column-major.
    return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
                     m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4);
}

struct Geometry {
    std::vector<Vertex> tris, lines, points;
    glm::vec3 lo{FLT_MAX}, hi{-FLT_MAX};
};

// Appends one mesh, placed by `xf` (its node's transform within the file).
static void AddMesh(Geometry& g, const aiScene* scene, const aiMesh* mesh, const glm::mat4& xf) {
    glm::mat3 nxf = glm::transpose(glm::inverse(glm::mat3(xf)));
    glm::vec3 base = MaterialColor(scene, mesh);
    auto pos = [&](unsigned i) {
        const aiVector3D& p = mesh->mVertices[i];
        return glm::vec3(xf * glm::vec4(p.x, p.y, p.z, 1.0f));
    };
    // `faceNormal` stands in when the file's normal is missing or zero.
    auto vertex = [&](unsigned i, glm::vec3 faceNormal) {
        glm::vec3 p = pos(i), c = base, n = faceNormal;
        if (mesh->HasVertexColors(0)) { const aiColor4D& vc = mesh->mColors[0][i]; c = {vc.r, vc.g, vc.b}; }
        if (mesh->HasNormals()) {
            const aiVector3D& vn = mesh->mNormals[i];
            glm::vec3 fileN = nxf * glm::vec3(vn.x, vn.y, vn.z);
            if (glm::length(fileN) > 1e-8f) n = fileN;
        }
        c *= Shade(n);
        return Vertex{p.x, p.y, p.z, c.r, c.g, c.b};
    };
    for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
        glm::vec3 p = pos(i);
        g.lo = glm::min(g.lo, p);
        g.hi = glm::max(g.hi, p);
    }
    if (mesh->mNumFaces == 0) {  // a bare point cloud, e.g. a PLY with only vertices
        for (unsigned i = 0; i < mesh->mNumVertices; ++i) g.points.push_back(vertex(i, glm::vec3(0.0f)));
        return;
    }
    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace& face = mesh->mFaces[f];
        const unsigned* idx = face.mIndices;
        if (face.mNumIndices == 3) {
            glm::vec3 fn = glm::cross(pos(idx[1]) - pos(idx[0]), pos(idx[2]) - pos(idx[0]));
            for (int k = 0; k < 3; ++k) g.tris.push_back(vertex(idx[k], fn));
        } else if (face.mNumIndices == 2) {
            g.lines.push_back(vertex(idx[0], glm::vec3(0.0f)));
            g.lines.push_back(vertex(idx[1], glm::vec3(0.0f)));
        } else if (face.mNumIndices == 1) {
            g.points.push_back(vertex(idx[0], glm::vec3(0.0f)));
        }  // larger polygons can't happen after aiProcess_Triangulate
    }
}

static void AddNode(Geometry& g, const aiScene* scene, const aiNode* node, glm::mat4 xf, std::vector<bool>& used) {
    xf = xf * ToGlm(node->mTransformation);
    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        unsigned m = node->mMeshes[i];
        if (m >= scene->mNumMeshes) continue;
        used[m] = true;
        AddMesh(g, scene, scene->mMeshes[m], xf);
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i) AddNode(g, scene, node->mChildren[i], xf, used);
}

static std::unique_ptr<ModelAsset> LoadModel(const std::string& path) {
    auto asset = std::make_unique<ModelAsset>();
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_GenSmoothNormals);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        asset->error = importer.GetErrorString();
        if (asset->error.empty()) asset->error = "could not read the file";
        return asset;
    }

    // The whole file becomes one object: every mesh placed by its node's
    // transform, plus any mesh no node refers to, as it is.
    Geometry g;
    std::vector<bool> used(scene->mNumMeshes, false);
    if (scene->mRootNode) AddNode(g, scene, scene->mRootNode, glm::mat4(1.0f), used);
    for (unsigned m = 0; m < scene->mNumMeshes; ++m)
        if (!used[m]) AddMesh(g, scene, scene->mMeshes[m], glm::mat4(1.0f));
    if (g.tris.empty() && g.lines.empty() && g.points.empty()) {
        asset->error = "the file has no geometry";
        return asset;
    }

    asset->ok = true;
    asset->triangles.Upload(g.tris, GL_TRIANGLES);
    asset->lines.Upload(g.lines, GL_LINES);
    asset->points.Upload(g.points, GL_POINTS);
    asset->boundsMin = g.lo;
    asset->boundsMax = g.hi;
    asset->vertexCount = (int)(g.tris.size() + g.lines.size() + g.points.size());
    asset->triangleCount = (int)(g.tris.size() / 3);
    glm::vec3 size = g.hi - g.lo;
    float extent = std::max({size.x, size.y, size.z});
    float s = extent > 1e-8f ? 1.0f / extent : 1.0f;
    asset->fit = glm::scale(glm::mat4(1.0f), glm::vec3(s)) * glm::translate(glm::mat4(1.0f), -(g.lo + g.hi) * 0.5f);
    return asset;
}

static std::map<std::string, std::unique_ptr<ModelAsset>>& Cache() {
    static std::map<std::string, std::unique_ptr<ModelAsset>> cache;
    return cache;
}

ModelAsset& GetModel(const std::string& path) {
    auto& slot = Cache()[path];
    if (!slot) slot = LoadModel(path);
    return *slot;
}

void ReloadModel(const std::string& path) {
    auto it = Cache().find(path);
    if (it == Cache().end()) return;
    it->second->triangles.Free();
    it->second->lines.Free();
    it->second->points.Free();
    Cache().erase(it);
}

std::string ModelFilePatterns() {
    aiString list;  // "*.obj;*.ply;..."
    Assimp::Importer().GetExtensionList(list);
    std::string s = list.C_Str();
    std::replace(s.begin(), s.end(), ';', ' ');
    return s;
}

std::string OpenModelDialog() {
    if (!pfd::settings::available()) return "";
    auto files = pfd::open_file("Open a 3D model", "",
                                {"3D models", ModelFilePatterns(), "All files", "*"}).result();
    return files.empty() ? "" : files[0];
}

std::string FileNameOf(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
