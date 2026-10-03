#include "Renderer.h"
#include "NodeGraph.h"

#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// ---------------------------------------------------------------------------
// Shader
// ---------------------------------------------------------------------------
// The one shader program shared by all three views. The views only differ in
// which space the scene is drawn in ("scene space") and which observer camera
// looks at it. See render/Renderer.h for the uniforms each draw sets.

static const char* kVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uModel;     // local -> scene space (the space the observer looks at)
uniform mat4 uToEye;     // scene space -> the student's camera eye space
uniform mat4 uCamProj;   // the student's projection matrix
uniform mat4 uObsView;   // observer camera of this viewport
uniform mat4 uObsProj;

// Perspective-divide visualisation (only used when scene space == eye space)
uniform float uWarp;      // 0 = plain eye space, 1 = after dividing by w
uniform int   uDepthMode; // 0 = distance remapped linearly to [-1,1], 1 = real NDC z (what the depth buffer stores)
uniform float uNear;
uniform float uFar;
uniform float uBoxHalf;   // the NDC cube [-1,1]^3 is drawn with this half size...
uniform float uBoxCenter; // ...centered this far in front of the camera

out vec3 vColor;
out vec4 vCamClip;
out float vEyeDist;

void main() {
    vec4 scene = uModel * vec4(aPos, 1.0);
    vec4 eye = uToEye * scene;
    vec4 clip = uCamProj * eye;

    vec3 pos = scene.xyz;
    if (uWarp > 0.0) {
        float w = max(clip.w, 1e-3);
        vec3 ndc = clip.xyz / w;
        // x and y are now in [-1,1] for everything the camera can see: they were divided by w,
        // which is the distance from the camera. That division is what makes far things small.
        float zn = (uDepthMode == 0) ? 2.0 * (-eye.z - uNear) / (uFar - uNear) - 1.0 : ndc.z;
        // Lay the cube out in front of the fixed camera: NDC z = -1 is the near face, +1 the far face.
        vec3 projected = vec3(ndc.x, ndc.y, 0.0) * uBoxHalf + vec3(0.0, 0.0, -(uBoxCenter + zn * uBoxHalf));
        projected = clamp(projected, vec3(-300.0), vec3(300.0));
        pos = mix(scene.xyz, projected, uWarp);
    }

    gl_Position = uObsProj * uObsView * vec4(pos, 1.0);
    vColor = aColor;
    vCamClip = clip;
    vEyeDist = -eye.z;
}
)";

static const char* kFragSrc = R"(
#version 330 core
in vec3 vColor;
in vec4 vCamClip;
in float vEyeDist;
out vec4 FragColor;

uniform vec3  uTint;
uniform float uTintAmount;
uniform int   uDimOutside;   // fade what the student's camera cannot see
uniform int   uClipBehind;   // drop fragments in front of the near plane
uniform int   uClipOutside;  // drop everything outside the [-1,1] cube (what the GPU really does)
uniform float uNear;
uniform vec3  uBg;

void main() {
    if (uClipBehind == 1 && vEyeDist < uNear) discard;
    vec3 c = mix(vColor, uTint, uTintAmount);
    bool inside = vCamClip.w > 0.0 && all(lessThanEqual(abs(vCamClip.xyz), vec3(vCamClip.w)));
    if (uClipOutside == 1 && !inside) discard;
    if (uDimOutside == 1 && !inside) c = mix(c, uBg, 0.72);
    FragColor = vec4(c, 1.0);
}
)";

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error: %s\n", log);
    }
    return s;
}

static GLuint BuildSceneProgram() {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------
void Mesh::Upload(const std::vector<Vertex>& verts, GLenum drawMode, GLenum usage) {
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), usage);
    glBindVertexArray(0);
    count = (int)verts.size();
    mode = drawMode;
}

void PushLine(std::vector<Vertex>& v, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    v.push_back({a.x, a.y, a.z, c.r, c.g, c.b});
    v.push_back({b.x, b.y, b.z, c.r, c.g, c.b});
}

std::vector<Vertex> CubeVerts() {
    glm::vec3 colors[6] = {
        {0.85f, 0.25f, 0.25f}, {0.25f, 0.75f, 0.35f}, {0.25f, 0.45f, 0.9f},
        {0.9f, 0.8f, 0.2f},    {0.8f, 0.3f, 0.8f},     {0.3f, 0.8f, 0.8f},
    };
    float p = 0.5f;
    glm::vec3 facePos[6][4] = {
        {{-p,-p, p}, { p,-p, p}, { p, p, p}, {-p, p, p}}, // +Z
        {{ p,-p,-p}, {-p,-p,-p}, {-p, p,-p}, { p, p,-p}}, // -Z
        {{-p,-p,-p}, {-p,-p, p}, {-p, p, p}, {-p, p,-p}}, // -X
        {{ p,-p, p}, { p,-p,-p}, { p, p,-p}, { p, p, p}}, // +X
        {{-p, p, p}, { p, p, p}, { p, p,-p}, {-p, p,-p}}, // +Y
        {{-p,-p,-p}, { p,-p,-p}, { p,-p, p}, {-p,-p, p}}, // -Y
    };
    std::vector<Vertex> verts;
    int order[6] = {0, 1, 2, 0, 2, 3};
    for (int f = 0; f < 6; ++f)
        for (int idx : order) {
            glm::vec3 q = facePos[f][idx];
            verts.push_back({q.x, q.y, q.z, colors[f].r, colors[f].g, colors[f].b});
        }
    return verts;
}

std::vector<Vertex> TriangleVerts(const glm::vec3 c[3]) {
    std::vector<Vertex> v;
    for (int i = 0; i < 3; ++i) v.push_back({c[i].x, c[i].y, c[i].z, 1.0f, 0.55f, 0.12f});
    return v;
}

// Lines are split into 1-unit segments so they bend smoothly (and clip
// cleanly) when the perspective divide is applied.
std::vector<Vertex> GridVerts(int half) {
    std::vector<Vertex> v;
    for (int i = -half; i <= half; ++i) {
        glm::vec3 c = (i == 0) ? glm::vec3(0.45f) : glm::vec3(0.26f);
        for (int s = -half; s < half; ++s) {
            PushLine(v, {(float)i, 0, (float)s}, {(float)i, 0, (float)(s + 1)}, c);
            PushLine(v, {(float)s, 0, (float)i}, {(float)(s + 1), 0, (float)i}, c);
        }
    }
    return v;
}

std::vector<Vertex> AxesVerts(float len) {
    std::vector<Vertex> v;
    PushLine(v, {0, 0.01f, 0}, {len, 0.01f, 0}, {1.0f, 0.3f, 0.3f});
    PushLine(v, {0, 0.01f, 0}, {0, len, 0},     {0.3f, 1.0f, 0.3f});
    PushLine(v, {0, 0.01f, 0}, {0, 0.01f, len}, {0.35f, 0.55f, 1.0f});
    return v;
}

// Plus the x = 0 / z = 0 lines on its floor.
std::vector<Vertex> NdcBoxVerts() {
    std::vector<Vertex> v;
    glm::vec3 c(0.95f);
    for (int a = 0; a < 3; ++a)
        for (int i = 0; i < 4; ++i) {
            glm::vec3 p0, p1;
            float u = (i & 1) ? 1.0f : -1.0f, w = (i & 2) ? 1.0f : -1.0f;
            p0[a] = -1; p1[a] = 1;
            p0[(a + 1) % 3] = p1[(a + 1) % 3] = u;
            p0[(a + 2) % 3] = p1[(a + 2) % 3] = w;
            PushLine(v, p0, p1, c);
        }
    glm::vec3 m(0.5f);
    PushLine(v, {0, -1, -1}, {0, -1, 1}, m);
    PushLine(v, {-1, -1, 0}, {1, -1, 0}, m);
    return v;
}

std::vector<Vertex> FrustumVerts(const EvalResult& ev) {
    // The visible volume is whatever P maps onto the NDC cube [-1, 1]^3, so its
    // corners are the cube's corners mapped back through P^-1. This works for any
    // invertible P: a perspective pyramid, an orthographic box, the identity's
    // 2x2x2 box around the camera, or a custom matrix.
    glm::vec3 p[8];
    const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
    bool invertible = std::fabs(glm::determinant(ev.projection)) > 1e-12f;
    if (invertible) {
        glm::mat4 inv = glm::inverse(ev.projection);
        for (int i = 0; i < 8; ++i) {
            glm::vec4 c = inv * glm::vec4(sx[i % 4], sy[i % 4], i < 4 ? -1.0f : 1.0f, 1.0f);
            float w = std::fabs(c.w) < 1e-9f ? 1e-9f : c.w;
            p[i] = glm::vec3(c) / w;
        }
    } else {
        // Singular P: fall back to the Projection node's fov/near/far.
        float th = std::tan(glm::radians(ev.fovDeg) * 0.5f);
        for (int i = 0; i < 8; ++i) {
            float d = i < 4 ? ev.nearPlane : ev.farPlane;
            p[i] = glm::vec3(sx[i % 4] * th * ev.aspect * d, sy[i % 4] * th * d, -d);
        }
    }
    int edges[12][2] = {{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}};
    glm::vec3 yellow(1.0f, 0.85f, 0.2f), dimYellow(0.7f, 0.6f, 0.2f);
    std::vector<Vertex> v;
    for (auto& e : edges) PushLine(v, p[e[0]], p[e[1]], yellow);
    if (ev.perspective)  // lines from the eye to the near plane
        for (int i = 0; i < 4; ++i) PushLine(v, glm::vec3(0), p[i], dimYellow);
    PushLine(v, {0, 0.2f, 0.2f}, {0, 0.55f, 0.2f}, {0.3f, 1.0f, 0.3f}); // camera's "up"
    return v;
}

// ---------------------------------------------------------------------------
// Framebuffer and renderer
// ---------------------------------------------------------------------------
void ViewportFBO::Resize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (w == width && h == height && fbo != 0) return;
    width = w; height = h;

    if (fbo == 0) glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    if (colorTex == 0) glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);

    if (depthRbo == 0) glGenRenderbuffers(1, &depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "Framebuffer incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::Init() {
    prog = BuildSceneProgram();
    auto L = [&](const char* n) { return glGetUniformLocation(prog, n); };
    uModel = L("uModel"); uToEye = L("uToEye"); uCamProj = L("uCamProj");
    uObsView = L("uObsView"); uObsProj = L("uObsProj"); uWarp = L("uWarp");
    uDepthMode = L("uDepthMode"); uNear = L("uNear"); uFar = L("uFar"); uBoxHalf = L("uBoxHalf"); uBoxCenter = L("uBoxCenter");
    uTint = L("uTint"); uTintAmount = L("uTintAmount"); uDimOutside = L("uDimOutside");
    uClipBehind = L("uClipBehind"); uClipOutside = L("uClipOutside"); uBg = L("uBg");
}

void Renderer::Begin(const ViewportFBO& fbo, const ViewSetup& v) {
    cur = v;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glViewport(0, 0, fbo.width, fbo.height);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(v.bg.r, v.bg.g, v.bg.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(prog);
    glUniformMatrix4fv(uToEye, 1, GL_FALSE, glm::value_ptr(v.toEye));
    glUniformMatrix4fv(uCamProj, 1, GL_FALSE, glm::value_ptr(v.camProj));
    glUniformMatrix4fv(uObsView, 1, GL_FALSE, glm::value_ptr(v.obsView));
    glUniformMatrix4fv(uObsProj, 1, GL_FALSE, glm::value_ptr(v.obsProj));
    glUniform1i(uDepthMode, v.depthMode);
    glUniform1f(uNear, v.nearPlane);
    glUniform1f(uFar, v.farPlane);
    glUniform1f(uBoxHalf, v.boxHalf);
    glUniform1f(uBoxCenter, v.boxCenter);
    glUniform3fv(uBg, 1, glm::value_ptr(v.bg));
}

void Renderer::Draw(const Mesh& m, const glm::mat4& model, const DrawOpts& o, int first, int count) {
    if (count < 0) count = m.count - first;
    if (count <= 0) return;
    bool warping = o.warp && cur.warp > 0.0f;
    glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniform1f(uWarp, warping ? cur.warp : 0.0f);
    glUniform1i(uClipBehind, warping && o.dim ? 1 : 0);
    glUniform1i(uClipOutside, cur.clipOutside && o.dim ? 1 : 0);
    glUniform1i(uDimOutside, cur.dimOutside && o.dim ? 1 : 0);
    glUniform3fv(uTint, 1, glm::value_ptr(o.tint));
    glUniform1f(uTintAmount, o.tintAmount);
    glBindVertexArray(m.vao);
    glDrawArrays(m.mode, first, count);
}

void Renderer::End() {
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------
void Scene::Init() {
    cube.Upload(CubeVerts(), GL_TRIANGLES);
    grid.Upload(GridVerts(12), GL_LINES);
    axes.Upload(AxesVerts(1.5f), GL_LINES);
    ndcBox.Upload(NdcBoxVerts(), GL_LINES);
}

void Scene::UpdateFrustum(const EvalResult& ev) {
    frustum.Upload(FrustumVerts(ev), GL_LINES, GL_STREAM_DRAW);
}

void DrawWorld(Renderer& r, Scene& s, const EvalResult& ev, const glm::mat4& worldToScene,
               DrawOpts base, bool drawGrid, bool drawAxes) {
    if (drawGrid) r.Draw(s.grid, worldToScene, base);
    DrawOpts axes = base; axes.dim = false;
    if (drawAxes) r.Draw(s.axes, worldToScene, axes);

    for (auto& o : ev.objects) {
        if (o.shape == kShapeTriangle) {
            s.triangle.Upload(TriangleVerts(o.tri), GL_TRIANGLES, GL_DYNAMIC_DRAW);
            r.Draw(s.triangle, worldToScene * o.model, base);
        } else {
            r.Draw(s.cube, worldToScene * o.model, base);
        }
    }
}

void DrawCameraGizmo(Renderer& r, Scene& s, const glm::mat4& camToScene, bool warpFrustum) {
    DrawOpts body; body.tint = glm::vec3(0.25f, 0.25f, 0.3f); body.tintAmount = 0.85f; body.warp = false; body.dim = false;
    r.Draw(s.cube, camToScene * glm::translate(glm::mat4(1.0f), {0, 0, 0.3f}) * glm::scale(glm::mat4(1.0f), {0.4f, 0.3f, 0.5f}), body);
    DrawOpts lens = body; lens.tint = glm::vec3(1.0f, 0.85f, 0.2f); lens.tintAmount = 0.9f;
    r.Draw(s.cube, camToScene * glm::scale(glm::mat4(1.0f), {0.18f, 0.18f, 0.12f}), lens);

    DrawOpts fr; fr.dim = false; fr.warp = warpFrustum;
    r.Draw(s.frustum, camToScene, fr, 0, kFrustumWarpable);
    fr.warp = false;
    r.Draw(s.frustum, camToScene, fr, kFrustumWarpable);
}

glm::mat4 OrbitCamera::View() const {
    float y = glm::radians(yawDeg), p = glm::radians(pitchDeg);
    glm::vec3 dir(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y));
    return glm::lookAt(target + dir * distance, target, glm::vec3(0, 1, 0));
}

void OrbitCamera::HandleInput(bool active, bool hovered) {
    ImGuiIO& io = ImGui::GetIO();
    if (active) {
        yawDeg -= io.MouseDelta.x * 0.4f;
        pitchDeg = glm::clamp(pitchDeg + io.MouseDelta.y * 0.4f, -89.0f, 89.0f);
    }
    if (hovered && io.MouseWheel != 0.0f)
        distance = glm::clamp(distance * std::pow(0.9f, io.MouseWheel), 2.0f, 200.0f);
}
