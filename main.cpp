// MVP teaching tool.
//
// Windows:
//   1. "What you think happens"  - a static world, and a camera flying through it (pose C).
//   2. "What actually happens"   - the camera is nailed to the origin looking down -Z and
//                                  the whole world is moved by V = C^-1. A slider applies
//                                  the perspective divide so you can watch far things shrink
//                                  and the view frustum turn into a box.
//   3. "What the camera sees"    - the final image: clip = P * V * M * vertex.
//   Node Editor                  - build Model / Camera / Projection chains with imnodes.
//
// All three views go through the same shader. They only differ in which space the
// scene is drawn in ("scene space") and which observer camera looks at it.

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder API, used to create the default layout
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imnodes.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#ifdef __linux__
#include <unistd.h>
#endif

#include "NodeGraph.h"

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
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

static GLuint BuildProgram() {
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
struct Vertex { float px, py, pz, r, g, b; };

struct Mesh {
    GLuint vao = 0, vbo = 0;
    int count = 0;
    GLenum mode = GL_TRIANGLES;

    void Upload(const std::vector<Vertex>& verts, GLenum drawMode, GLenum usage = GL_STATIC_DRAW) {
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
};

static void PushLine(std::vector<Vertex>& v, glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    v.push_back({a.x, a.y, a.z, c.r, c.g, c.b});
    v.push_back({b.x, b.y, b.z, c.r, c.g, c.b});
}

// Unit cube centered at the origin with one color per face, so orientation reads clearly.
static std::vector<Vertex> CubeVerts() {
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

// A flat orange triangle in the XY plane.
static std::vector<Vertex> TriangleVerts() {
    return {
        {-0.6f, -0.5f, 0.0f, 1.0f, 0.55f, 0.12f},
        { 0.6f, -0.5f, 0.0f, 1.0f, 0.55f, 0.12f},
        { 0.0f,  0.6f, 0.0f, 1.0f, 0.55f, 0.12f},
    };
}

// Ground grid on y = 0. Lines are split into 1-unit segments so they bend
// smoothly (and clip cleanly) when the perspective divide is applied.
static std::vector<Vertex> GridVerts(int half) {
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

static std::vector<Vertex> AxesVerts(float len) {
    std::vector<Vertex> v;
    PushLine(v, {0, 0.01f, 0}, {len, 0.01f, 0}, {1.0f, 0.3f, 0.3f});
    PushLine(v, {0, 0.01f, 0}, {0, len, 0},     {0.3f, 1.0f, 0.3f});
    PushLine(v, {0, 0.01f, 0}, {0, 0.01f, len}, {0.35f, 0.55f, 1.0f});
    return v;
}

// The NDC cube [-1,1]^3, plus the x = 0 / z = 0 lines on its floor.
static std::vector<Vertex> NdcBoxVerts() {
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

// Camera-local frustum. The first kFrustumWarpable vertices are the near/far
// rectangles and their connecting edges (these get warped by the perspective
// divide). The rest are the lines from the eye to the near plane plus an "up"
// marker, which are drawn unwarped.
static const int kFrustumWarpable = 24;

static std::vector<Vertex> FrustumVerts(const EvalResult& ev) {
    float th = std::tan(glm::radians(ev.fovDeg) * 0.5f);
    auto corner = [&](float d, float sx, float sy) {
        if (!ev.hasProjection)  // no projection: the box [-1, 1] around the camera
            return glm::vec3(sx, sy, -d);
        return glm::vec3(sx * th * ev.aspect * d, sy * th * d, -d);
    };
    float n = ev.nearPlane, f = ev.farPlane;
    glm::vec3 p[8] = {
        corner(n, -1, -1), corner(n, 1, -1), corner(n, 1, 1), corner(n, -1, 1),
        corner(f, -1, -1), corner(f, 1, -1), corner(f, 1, 1), corner(f, -1, 1),
    };
    int edges[12][2] = {{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}};
    glm::vec3 yellow(1.0f, 0.85f, 0.2f), dimYellow(0.7f, 0.6f, 0.2f);
    std::vector<Vertex> v;
    for (auto& e : edges) PushLine(v, p[e[0]], p[e[1]], yellow);
    if (ev.hasProjection)  // lines from the eye to the near plane
        for (int i = 0; i < 4; ++i) PushLine(v, glm::vec3(0), p[i], dimYellow);
    PushLine(v, {0, 0.2f, 0.2f}, {0, 0.55f, 0.2f}, {0.3f, 1.0f, 0.3f}); // camera's "up"
    return v;
}

// ---------------------------------------------------------------------------
// Render target shown inside an ImGui window
// ---------------------------------------------------------------------------
struct ViewportFBO {
    GLuint fbo = 0, colorTex = 0, depthRbo = 0;
    int width = 0, height = 0;

    void Resize(int w, int h) {
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

    bool Valid() const { return fbo != 0 && width > 0 && height > 0; }
};

// ---------------------------------------------------------------------------
// Observer camera for windows 1 and 2: drag to orbit, scroll to zoom
// ---------------------------------------------------------------------------
struct OrbitCamera {
    glm::vec3 target{0.0f};
    float yawDeg = 40.0f, pitchDeg = 30.0f, distance = 20.0f;

    glm::mat4 View() const {
        float y = glm::radians(yawDeg), p = glm::radians(pitchDeg);
        glm::vec3 dir(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y));
        return glm::lookAt(target + dir * distance, target, glm::vec3(0, 1, 0));
    }

    void HandleInput(bool active, bool hovered) {
        ImGuiIO& io = ImGui::GetIO();
        if (active) {
            yawDeg -= io.MouseDelta.x * 0.4f;
            pitchDeg = glm::clamp(pitchDeg + io.MouseDelta.y * 0.4f, -89.0f, 89.0f);
        }
        if (hovered && io.MouseWheel != 0.0f)
            distance = glm::clamp(distance * std::pow(0.9f, io.MouseWheel), 2.0f, 200.0f);
    }
};

// ---------------------------------------------------------------------------
// Renderer: one program, uniforms set per viewport and per draw
// ---------------------------------------------------------------------------
struct ViewSetup {
    glm::mat4 obsView{1.0f}, obsProj{1.0f};
    glm::mat4 toEye{1.0f};     // scene space -> student eye space
    glm::mat4 camProj{1.0f};
    float warp = 0.0f;         // only meaningful when scene space == eye space
    int depthMode = 0;
    bool dimOutside = false;
    float nearPlane = 0.5f, farPlane = 25.0f;
    float boxHalf = 4.0f, boxCenter = 4.5f;  // where the [-1,1] cube is drawn in window 2
    bool clipOutside = false;
    glm::vec3 bg{0.08f};
};

struct DrawOpts {
    glm::vec3 tint{0.0f};
    float tintAmount = 0.0f;
    bool warp = true;   // allow the perspective-divide warp for this draw
    bool dim = true;    // allow dimming outside the frustum for this draw
};

struct Renderer {
    GLuint prog = 0;
    GLint uModel, uToEye, uCamProj, uObsView, uObsProj, uWarp, uDepthMode, uNear, uFar, uBoxHalf, uBoxCenter;
    GLint uTint, uTintAmount, uDimOutside, uClipBehind, uClipOutside, uBg;
    ViewSetup cur;

    void Init() {
        prog = BuildProgram();
        auto L = [&](const char* n) { return glGetUniformLocation(prog, n); };
        uModel = L("uModel"); uToEye = L("uToEye"); uCamProj = L("uCamProj");
        uObsView = L("uObsView"); uObsProj = L("uObsProj"); uWarp = L("uWarp");
        uDepthMode = L("uDepthMode"); uNear = L("uNear"); uFar = L("uFar"); uBoxHalf = L("uBoxHalf"); uBoxCenter = L("uBoxCenter");
        uTint = L("uTint"); uTintAmount = L("uTintAmount"); uDimOutside = L("uDimOutside");
        uClipBehind = L("uClipBehind"); uClipOutside = L("uClipOutside"); uBg = L("uBg");
    }

    void Begin(const ViewportFBO& fbo, const ViewSetup& v) {
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

    void Draw(const Mesh& m, const glm::mat4& model, const DrawOpts& o = {}, int first = 0, int count = -1) {
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

    void End() {
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------
struct Scene {
    Mesh cube, triangle, grid, axes, frustum;
    Mesh ndcBox;   // edges of the cube [-1,1]^3 plus the center lines on its floor
};

struct Options {
    bool playAnimations = true;
    bool dimOutside = true;
    bool showMatrices = true;
    // window 2
    float warp = 0.0f;
    bool animateWarp = false;
    float warpPhase = 0.0f;
    int depthMode = 0;
    bool showGhost = true;
    bool clipOutside = false;
};

// Draws the "world": grid, axes and every object of the graph.
// `worldToScene` is identity when the observer looks at the world directly (window 1/3),
// and V when the observer looks at eye space (window 2).
static void DrawWorld(Renderer& r, Scene& s, const EvalResult& ev, const Options& opt,
                      const glm::mat4& worldToScene, DrawOpts base = {}, bool drawGrid = true) {
    if (drawGrid) r.Draw(s.grid, worldToScene, base);
    DrawOpts axes = base; axes.dim = false;
    r.Draw(s.axes, worldToScene, axes);

    for (auto& o : ev.objects)
        r.Draw(o.shape == kShapeTriangle ? s.triangle : s.cube, worldToScene * o.model, base);
}

// Camera body + frustum. `camToScene` places the camera in scene space.
static void DrawCameraGizmo(Renderer& r, Scene& s, const glm::mat4& camToScene, bool warpFrustum) {
    DrawOpts body; body.tint = glm::vec3(0.25f, 0.25f, 0.3f); body.tintAmount = 0.85f; body.warp = false; body.dim = false;
    r.Draw(s.cube, camToScene * glm::translate(glm::mat4(1.0f), {0, 0, 0.3f}) * glm::scale(glm::mat4(1.0f), {0.4f, 0.3f, 0.5f}), body);
    DrawOpts lens = body; lens.tint = glm::vec3(1.0f, 0.85f, 0.2f); lens.tintAmount = 0.9f;
    r.Draw(s.cube, camToScene * glm::translate(glm::mat4(1.0f), {0, 0, 0.0f}) * glm::scale(glm::mat4(1.0f), {0.18f, 0.18f, 0.12f}), lens);

    DrawOpts fr; fr.dim = false; fr.warp = warpFrustum;
    r.Draw(s.frustum, camToScene, fr, 0, kFrustumWarpable);
    fr.warp = false;
    r.Draw(s.frustum, camToScene, fr, kFrustumWarpable);
}

// ---------------------------------------------------------------------------
// Theme and fonts
// ---------------------------------------------------------------------------
static ImFont* gMonoFont = nullptr;  // used for matrices so the columns line up

static ImVec4 Hex(unsigned rgb, float a = 1.0f) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

// Background colors of the three 3D views, picked to sit well inside the theme.
static const glm::vec3 kThinkBg(0.075f, 0.085f, 0.105f);
static const glm::vec3 kActualBg(0.085f, 0.075f, 0.105f);
static const glm::vec3 kCameraBg(0.035f, 0.04f, 0.05f);

static void ApplyDarkTheme(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);

    st.WindowPadding = ImVec2(10, 10);
    st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 6);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.ScrollbarSize = 12;
    st.GrabMinSize = 10;
    st.WindowBorderSize = 1;
    st.FrameBorderSize = 0;
    st.PopupBorderSize = 1;
    st.TabBorderSize = 0;
    st.WindowRounding = 6;
    st.ChildRounding = 6;
    st.FrameRounding = 5;
    st.PopupRounding = 6;
    st.ScrollbarRounding = 6;
    st.GrabRounding = 5;
    st.TabRounding = 5;
    st.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.SeparatorTextBorderSize = 1;
    st.DockingSeparatorSize = 2;

    const ImVec4 bg      = Hex(0x111318);  // window background
    const ImVec4 bgDeep  = Hex(0x0B0C10);  // title bars, docking empty space
    const ImVec4 panel   = Hex(0x181B22);  // frames, headers
    const ImVec4 panelHi = Hex(0x222631);
    const ImVec4 panelAc = Hex(0x2B303D);
    const ImVec4 border  = Hex(0x262A34);
    const ImVec4 text    = Hex(0xD8DCE3);
    const ImVec4 muted   = Hex(0x767E8C);
    const ImVec4 accent  = Hex(0x6C8CFF);  // soft blue
    const ImVec4 accentD = Hex(0x6C8CFF, 0.55f);
    const ImVec4 accentF = Hex(0x6C8CFF, 0.25f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = muted;
    c[ImGuiCol_WindowBg]              = bg;
    c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = Hex(0x15181E, 0.98f);
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = panel;
    c[ImGuiCol_FrameBgHovered]        = panelHi;
    c[ImGuiCol_FrameBgActive]         = panelAc;
    c[ImGuiCol_TitleBg]               = bgDeep;
    c[ImGuiCol_TitleBgActive]         = bgDeep;
    c[ImGuiCol_TitleBgCollapsed]      = bgDeep;
    c[ImGuiCol_MenuBarBg]             = bgDeep;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = panelAc;
    c[ImGuiCol_ScrollbarGrabHovered]  = Hex(0x3A4150);
    c[ImGuiCol_ScrollbarGrabActive]   = Hex(0x485063);
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accentD;
    c[ImGuiCol_SliderGrabActive]      = accent;
    c[ImGuiCol_Button]                = panelHi;
    c[ImGuiCol_ButtonHovered]         = panelAc;
    c[ImGuiCol_ButtonActive]          = accentD;
    c[ImGuiCol_Header]                = panel;
    c[ImGuiCol_HeaderHovered]         = panelHi;
    c[ImGuiCol_HeaderActive]          = panelAc;
    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accentD;
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = accentD;
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_Tab]                   = bgDeep;
    c[ImGuiCol_TabHovered]            = panelHi;
    c[ImGuiCol_TabActive]             = bg;
    c[ImGuiCol_TabUnfocused]          = bgDeep;
    c[ImGuiCol_TabUnfocusedActive]    = bg;
    c[ImGuiCol_DockingPreview]        = accentF;
    c[ImGuiCol_DockingEmptyBg]        = bgDeep;
    c[ImGuiCol_TextSelectedBg]        = accentF;
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_NavHighlight]          = accent;
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.5f);

    st.ScaleAllSizes(scale);

    // Node editor to match.
    ImNodes::StyleColorsDark();
    ImNodesStyle& ns = ImNodes::GetStyle();
    auto U = [](ImVec4 v) { return ImGui::ColorConvertFloat4ToU32(v); };
    ns.Colors[ImNodesCol_GridBackground]         = U(Hex(0x0E1014));
    ns.Colors[ImNodesCol_GridLine]               = U(Hex(0x1A1D24));
    ns.Colors[ImNodesCol_GridLinePrimary]        = U(Hex(0x232733));
    ns.Colors[ImNodesCol_NodeBackground]         = U(Hex(0x1A1D24));
    ns.Colors[ImNodesCol_NodeBackgroundHovered]  = U(Hex(0x1F232B));
    ns.Colors[ImNodesCol_NodeBackgroundSelected] = U(Hex(0x232835));
    ns.Colors[ImNodesCol_NodeOutline]            = U(Hex(0x2C313D));
    ns.Colors[ImNodesCol_Link]                   = U(Hex(0x6C8CFF, 0.85f));
    ns.Colors[ImNodesCol_LinkHovered]            = U(Hex(0x93AAFF));
    ns.Colors[ImNodesCol_LinkSelected]           = U(Hex(0xB9C8FF));
    ns.Colors[ImNodesCol_Pin]                    = U(Hex(0x6C8CFF));
    ns.Colors[ImNodesCol_PinHovered]             = U(Hex(0xB9C8FF));
    ns.Colors[ImNodesCol_BoxSelector]            = U(Hex(0x6C8CFF, 0.15f));
    ns.Colors[ImNodesCol_BoxSelectorOutline]     = U(Hex(0x6C8CFF, 0.6f));
    ns.Colors[ImNodesCol_MiniMapBackground]      = U(Hex(0x0B0C10, 0.85f));
    ns.Colors[ImNodesCol_MiniMapOutline]         = U(Hex(0x2C313D));
    ns.NodeCornerRounding = 6.0f * scale;
    ns.NodePadding = ImVec2(9.0f * scale, 7.0f * scale);
    ns.NodeBorderThickness = 1.0f;
    ns.LinkThickness = 2.5f * scale;
    ns.PinCircleRadius = 4.5f * scale;
    ns.GridSpacing = 28.0f * scale;
}

// Looks for the fonts next to the executable first, then in the Dear ImGui
// sources (MVP_FONT_DIR is set by CMake). Falls back to ImGui's built-in font.
static void LoadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    std::vector<std::string> dirs;
#ifdef __linux__
    char exe[4096];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        std::string path(exe, (size_t)len);
        dirs.push_back(path.substr(0, path.find_last_of('/')) + "/fonts");
    }
#endif
    dirs.push_back("fonts");
#ifdef MVP_FONT_DIR
    dirs.push_back(MVP_FONT_DIR);
#endif
    auto exists = [](const std::string& f) { return std::ifstream(f).good(); };
    for (const std::string& d : dirs) {
        std::string ui = d + "/Roboto-Medium.ttf", mono = d + "/Cousine-Regular.ttf";
        if (!exists(ui)) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;
        io.Fonts->AddFontFromFileTTF(ui.c_str(), 16.0f * scale, &cfg);
        if (exists(mono)) gMonoFont = io.Fonts->AddFontFromFileTTF(mono.c_str(), 14.5f * scale, &cfg);
        return;
    }
    std::fprintf(stderr, "Fonts not found, using ImGui's default font\n");
    io.Fonts->AddFontDefault();
}

// ---------------------------------------------------------------------------
// ImGui helpers
// ---------------------------------------------------------------------------
static void Overlay(ImVec2 at, const std::vector<std::string>& lines, ImU32 color = IM_COL32(230, 230, 230, 255)) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lh = ImGui::GetTextLineHeightWithSpacing();
    float w = 0;
    for (auto& l : lines) w = glm::max(w, ImGui::CalcTextSize(l.c_str()).x);
    ImVec2 p0(at.x + 6, at.y + 6);
    dl->AddRectFilled(p0, ImVec2(p0.x + w + 12, p0.y + lh * lines.size() + 8), IM_COL32(0, 0, 0, 150), 4.0f);
    for (size_t i = 0; i < lines.size(); ++i)
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4 + lh * i), color, lines[i].c_str());
}

// Shows the FBO as an image filling `size` and returns (active, hovered) of an
// invisible button on top of it, used for orbit controls.
static void ShowViewport(const char* id, const ViewportFBO& fbo, ImVec2 size, bool* active, bool* hovered) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)fbo.colorTex, size, ImVec2(0, 1), ImVec2(1, 0));
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    *active = ImGui::IsItemActive();
    *hovered = ImGui::IsItemHovered();
}

static void MatrixText(const glm::mat4& m) {
    if (gMonoFont) ImGui::PushFont(gMonoFont);
    ImGui::PushStyleColor(ImGuiCol_Text, Hex(0x9AA3B2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 1.0f));
    for (int r = 0; r < 4; ++r)
        ImGui::Text("%6.2f %6.2f %6.2f %6.2f", m[0][r], m[1][r], m[2][r], m[3][r]);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (gMonoFont) ImGui::PopFont();
}

static ImU32 TitleColorFor(const Node& n) {
    switch (n.type) {
        case NodeType::Object:     return IM_COL32(40, 100, 170, 255);
        case NodeType::Camera:     return IM_COL32(185, 120, 30, 255);
        case NodeType::Projection: return IM_COL32(125, 60, 160, 255);
        case NodeType::Output:     return IM_COL32(40, 130, 70, 255);
        default: break;
    }
    switch (n.kind) {
        case ChainKind::Model:      return IM_COL32(60, 85, 125, 255);
        case ChainKind::CameraPose: return IM_COL32(130, 95, 45, 255);
        case ChainKind::Invalid:    return IM_COL32(150, 40, 40, 255);
        default:                    return IM_COL32(70, 70, 70, 255);
    }
}

static glm::vec4 Brighten(ImU32 c, float k) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    return glm::vec4(glm::min(v.x * k, 1.0f), glm::min(v.y * k, 1.0f), glm::min(v.z * k, 1.0f), v.w);
}

// ---------------------------------------------------------------------------
// Dockable windows
// ---------------------------------------------------------------------------
static const char* kWinThink    = "1. What you think happens";
static const char* kWinActual   = "2. What actually happens";
static const char* kWinCamera   = "3. What the camera sees";
static const char* kWinEditor   = "Node Editor";
static const char* kWinSettings = "Settings";
static const char* kWinMatrices = "Matrices";

struct WindowVisibility {
    bool think = true, actual = true, camera = true, editor = true, settings = true, matrices = true;
};

// Three views on top, node editor below, Settings + Matrices tabbed to the right of it.
static void BuildDefaultLayout(ImGuiID dockId) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);

    ImGuiID top, bottom, left, rest, mid, right, editor, side;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down, 0.48f, &bottom, &top);
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 1.0f / 3.0f, &left, &rest);
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.5f, &mid, &right);
    ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.24f, &side, &editor);

    ImGui::DockBuilderDockWindow(kWinThink, left);
    ImGui::DockBuilderDockWindow(kWinActual, mid);
    ImGui::DockBuilderDockWindow(kWinCamera, right);
    ImGui::DockBuilderDockWindow(kWinEditor, editor);
    ImGui::DockBuilderDockWindow(kWinSettings, side);
    ImGui::DockBuilderDockWindow(kWinMatrices, side);
    ImGui::DockBuilderFinish(dockId);
}

// ---------------------------------------------------------------------------
// Node editor window
// ---------------------------------------------------------------------------
static void BuildStarterGraph(NodeGraph& g);

// Replaces the whole graph with the starter example, or with just an MVP Output node.
static void ResetGraph(NodeGraph& graph, bool starter) {
    // Keep the id counters going so imnodes never sees a reused id.
    int nid = graph.nextNodeId, lid = graph.nextLinkId;
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    graph = NodeGraph();
    graph.nextNodeId = nid; graph.nextLinkId = lid;
    if (starter) {
        BuildStarterGraph(graph);
    } else {
        int out = graph.AddNode(NodeType::Output, "MVP Output");
        ImNodes::SetNodeGridSpacePos(out, ImVec2(600, 60));
    }
}

static const char* kControlsHelp =
    "Add nodes with the toolbar, or right-click the canvas\n"
    "Remove: the x on a node, right-click it, or select + Delete\n"
    "Drag between pins: connect      Ctrl+click a link: detach it\n"
    "Click-drag on empty canvas: box select      Middle-drag: pan\n"
    "Views 1 and 2: drag to orbit, mouse wheel to zoom\n"
    "Windows: drag a tab or title bar to dock it anywhere\n\n"
    "Chains read left to right: Object -> Rotate -> Translate\n"
    "rotates the object first, then moves it (M = T * R).";

struct NodeKindInfo { NodeType type; const char* label; };
static const NodeKindInfo kSourceKinds[] = {
    {NodeType::Object, "Object"}, {NodeType::Camera, "Camera"}, {NodeType::Projection, "Projection"},
};
static const NodeKindInfo kTransformKinds[] = {
    {NodeType::Translate, "Translate"}, {NodeType::RotateX, "Rotate X"}, {NodeType::RotateY, "Rotate Y"},
    {NodeType::RotateZ, "Rotate Z"}, {NodeType::Scale, "Scale"},
};

static const char* NodeKindName(NodeType t) {
    for (auto& k : kSourceKinds) if (k.type == t) return k.label;
    for (auto& k : kTransformKinds) if (k.type == t) return k.label;
    return "MVP Output";
}

static ImU32 TypeColor(NodeType t) {
    switch (t) {
        case NodeType::Object:     return IM_COL32(40, 100, 170, 255);
        case NodeType::Camera:     return IM_COL32(185, 120, 30, 255);
        case NodeType::Projection: return IM_COL32(125, 60, 160, 255);
        case NodeType::Output:     return IM_COL32(40, 130, 70, 255);
        default:                   return IM_COL32(75, 75, 80, 255);
    }
}

// A button tinted with the color the node will have in the graph.
static bool ColoredButton(const char* label, ImU32 color, bool enabled = true) {
    glm::vec4 h = Brighten(color, 1.25f), a = Brighten(color, 1.45f);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(h.x, h.y, h.z, h.w));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x, a.y, a.z, a.w));
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    return pressed;
}

// "Translate", "Translate 2", "Translate 3", ...
static std::string UniqueName(NodeGraph& g, NodeType t, const char* base) {
    int count = 0;
    for (auto& n : g.nodes) if (n.type == t) ++count;
    return count == 0 ? std::string(base) : std::string(base) + " " + std::to_string(count + 1);
}

static void DrawNodeEditor(NodeGraph& graph, Options& opt, const EvalResult& lastEval, bool* open) {
    if (!ImGui::Begin(kWinEditor, open)) { ImGui::End(); return; }

    static int spawnCount = 0;           // cascades nodes added from the toolbar
    std::vector<int> nodesToDelete;      // applied after EndNodeEditor
    std::vector<int> linksToDelete;
    auto queueSelected = [&]() {
        int nl = ImNodes::NumSelectedLinks();
        if (nl > 0) { std::vector<int> ids(nl); ImNodes::GetSelectedLinks(ids.data()); linksToDelete.insert(linksToDelete.end(), ids.begin(), ids.end()); }
        int nn = ImNodes::NumSelectedNodes();
        if (nn > 0) { std::vector<int> ids(nn); ImNodes::GetSelectedNodes(ids.data()); nodesToDelete.insert(nodesToDelete.end(), ids.begin(), ids.end()); }
    };
    auto duplicate = [&](int id) {
        Node* src = graph.FindNode(id);
        if (!src || src->type == NodeType::Output) return;
        Node copy = *src;
        int newId = graph.AddNode(copy.type, UniqueName(graph, copy.type, NodeKindName(copy.type)));
        std::string name = graph.FindNode(newId)->name;
        Node* dst = graph.FindNode(newId);
        *dst = copy;
        dst->id = newId;
        dst->name = name;
        ImVec2 p = ImNodes::GetNodeGridSpacePos(id);
        ImNodes::SetNodeGridSpacePos(newId, ImVec2(p.x + 40, p.y + 40));
    };

    // ---- toolbar: add / remove ----
    int numSelNodes = ImNodes::NumSelectedNodes(), numSelLinks = ImNodes::NumSelectedLinks();
    ImVec2 addAt(0, 0);
    NodeType toAdd = NodeType::Object;
    const char* toAddLabel = nullptr;

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Add:");
    for (auto& k : kSourceKinds) {
        ImGui::SameLine();
        if (ColoredButton(k.label, TypeColor(k.type))) { toAdd = k.type; toAddLabel = k.label; }
    }
    ImGui::SameLine(); ImGui::TextDisabled("|");
    for (auto& k : kTransformKinds) {
        ImGui::SameLine();
        if (ColoredButton(k.label, TypeColor(k.type))) { toAdd = k.type; toAddLabel = k.label; }
    }
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    bool hasOutput = graph.OutputNode() != nullptr;
    if (ColoredButton("MVP Output", TypeColor(NodeType::Output), !hasOutput)) { toAdd = NodeType::Output; toAddLabel = "MVP Output"; }
    if (hasOutput && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("There can only be one MVP Output.");

    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    ImGui::BeginDisabled(numSelNodes == 0);
    if (ImGui::Button("Duplicate")) {
        std::vector<int> ids(numSelNodes);
        ImNodes::GetSelectedNodes(ids.data());
        for (int id : ids) duplicate(id);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    char delLabel[64];
    std::snprintf(delLabel, sizeof(delLabel), "Delete selected (%d)###delsel", numSelNodes + numSelLinks);
    if (ColoredButton(delLabel, IM_COL32(150, 45, 45, 255), numSelNodes + numSelLinks > 0)) queueSelected();
    ImGui::SameLine();
    if (ImGui::Button("Reset...")) ImGui::OpenPopup("reset_graph");
    if (ImGui::BeginPopup("reset_graph")) {
        ImGui::TextDisabled("Replace the whole graph with:");
        if (ImGui::MenuItem("The starter example")) ResetGraph(graph, true);
        if (ImGui::MenuItem("An empty graph (just MVP Output)")) ResetGraph(graph, false);
        ImGui::Separator();
        ImGui::MenuItem("Cancel");
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Show matrices", &opt.showMatrices);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kControlsHelp);
    for (auto& w : lastEval.warnings)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "! %s", w.c_str());


    // New nodes from the toolbar appear in the middle of the visible canvas.
    {
        ImVec2 c = ImGui::GetCursorScreenPos(), a = ImGui::GetContentRegionAvail();
        float off = (float)(spawnCount % 6) * 30.0f;
        addAt = ImVec2(c.x + a.x * 0.4f + off, c.y + a.y * 0.3f + off);
    }
    if (toAddLabel) {
        int id = graph.AddNode(toAdd, UniqueName(graph, toAdd, toAddLabel));
        ImNodes::SetNodeScreenSpacePos(id, addAt);
        ++spawnCount;
    }

    // Input fields need more contrast on a node than on a plain window.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Hex(0x272C38));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Hex(0x303644));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Hex(0x394052));
    ImNodes::BeginNodeEditor();

    for (auto& n : graph.nodes) {
        ImU32 title = TitleColorFor(n);
        glm::vec4 hov = Brighten(title, 1.25f), sel = Brighten(title, 1.45f);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, ImGui::ColorConvertFloat4ToU32(ImVec4(hov.x, hov.y, hov.z, hov.w)));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, ImGui::ColorConvertFloat4ToU32(ImVec4(sel.x, sel.y, sel.z, sel.w)));

        ImNodes::BeginNode(n.id);
        ImGui::PushID(n.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(n.name.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 60));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(200, 60, 60, 255));
        if (ImGui::SmallButton("x")) nodesToDelete.push_back(n.id);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this node");
        ImNodes::EndNodeTitleBar();

        ImGui::PushItemWidth(150.0f);

        if (n.type == NodeType::Output) {
            ImNodes::BeginInputAttribute(NodeGraph::InAttr(n.id));
            ImGui::TextUnformatted("Model  (M, one per object)");
            ImNodes::EndInputAttribute();
            ImNodes::BeginInputAttribute(NodeGraph::ViewInAttr(n.id));
            ImGui::TextUnformatted("View   (camera pose C)");
            ImNodes::EndInputAttribute();
            ImNodes::BeginInputAttribute(NodeGraph::ProjInAttr(n.id));
            ImGui::TextUnformatted("Projection  (P)");
            ImNodes::EndInputAttribute();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "clip = P * V * M * vertex");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "V = inverse(C)");
            if (opt.showMatrices) {
                ImGui::TextUnformatted("V (world -> camera):");
                MatrixText(lastEval.view);
            }
        } else {
            if (n.type != NodeType::Object && n.type != NodeType::Camera && n.type != NodeType::Projection) {
                ImNodes::BeginInputAttribute(NodeGraph::InAttr(n.id));
                ImGui::TextUnformatted("in");
                ImNodes::EndInputAttribute();
            }

            switch (n.type) {
                case NodeType::Object:
                {
                    const char* shapes[] = {"Cube", "Triangle"};
                    ImGui::SetNextItemWidth(110.0f);
                    ImGui::Combo("shape", &n.shape, shapes, 2);
                    break;
                }
                    break;
                case NodeType::Translate:
                    ImGui::DragFloat3("offset", &n.vec.x, 0.05f);
                    break;
                case NodeType::Scale:
                    ImGui::DragFloat3("factor", &n.vec.x, 0.02f);
                    break;
                case NodeType::RotateX:
                case NodeType::RotateY:
                case NodeType::RotateZ:
                    ImGui::DragFloat("deg", &n.angleDeg, 1.0f, -360.0f, 360.0f);
                    ImGui::Checkbox("spin", &n.spin);
                    if (n.spin) { ImGui::SameLine(); ImGui::PushItemWidth(80); ImGui::DragFloat("deg/s", &n.spinSpeed, 1.0f, -360.0f, 360.0f); ImGui::PopItemWidth(); }
                    break;
                case NodeType::Camera:
                    ImGui::DragFloat3("position", &n.camPos.x, 0.05f);
                    ImGui::DragFloat3("look at", &n.camTarget.x, 0.05f);
                    break;
                case NodeType::Projection:
                    ImGui::DragFloat("fov", &n.fovDeg, 0.5f, 10.0f, 150.0f);
                    ImGui::DragFloat("near", &n.nearPlane, 0.01f, 0.01f, 20.0f);
                    ImGui::DragFloat("far", &n.farPlane, 0.25f, 1.0f, 500.0f);
                    ImGui::TextDisabled("aspect %.2f (from camera window)", n.aspect);
                    break;
                default: break;
            }

            if (opt.showMatrices && n.type != NodeType::Object) {
                const char* label =
                    n.type == NodeType::Projection        ? "P:" :
                    n.kind == ChainKind::Model            ? "model so far (M):" :
                    n.kind == ChainKind::CameraPose       ? "camera pose so far (C):" :
                    n.kind == ChainKind::Invalid          ? "invalid chain" : "not connected to a source";
                ImGui::TextUnformatted(label);
                MatrixText(n.cumulativeMatrix);
            }

            ImNodes::BeginOutputAttribute(NodeGraph::OutAttr(n.id));
            ImGui::Indent(n.type == NodeType::Object ? 120.0f : 150.0f);
            ImGui::TextUnformatted("out");
            ImNodes::EndOutputAttribute();
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
        ImNodes::EndNode();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    for (auto& l : graph.links) ImNodes::Link(l.id, l.startAttr, l.endAttr);

    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();
    ImGui::PopStyleColor(3);

    // ---- link edits (must happen after EndNodeEditor) ----
    int startAttr, endAttr;
    if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) graph.AddLink(startAttr, endAttr);
    int destroyed;
    if (ImNodes::IsLinkDestroyed(&destroyed)) graph.RemoveLink(destroyed);

    // ---- keyboard delete ----
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsAnyItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)))
        queueSelected();

    // ---- right-click menus: on a node, on a link, or on empty canvas ----
    static int contextNode = -1, contextLink = -1;
    if (ImNodes::IsEditorHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 25.0f) {
        int hovered;
        if (ImNodes::IsNodeHovered(&hovered))      { contextNode = hovered; ImGui::OpenPopup("node_menu"); }
        else if (ImNodes::IsLinkHovered(&hovered)) { contextLink = hovered; ImGui::OpenPopup("link_menu"); }
        else                                         ImGui::OpenPopup("add_node");
    }
    if (ImGui::BeginPopup("node_menu")) {
        Node* n = graph.FindNode(contextNode);
        if (n) {
            ImGui::TextDisabled("%s", n->name.c_str());
            if (ImGui::MenuItem("Duplicate", nullptr, false, n->type != NodeType::Output)) duplicate(contextNode);
            if (ImGui::MenuItem("Disconnect all links")) {
                for (auto& l : graph.links)
                    if (NodeGraph::NodeOfAttr(l.startAttr) == contextNode || NodeGraph::NodeOfAttr(l.endAttr) == contextNode)
                        linksToDelete.push_back(l.id);
            }
            if (ImGui::MenuItem("Delete", "Del")) nodesToDelete.push_back(contextNode);
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("link_menu")) {
        if (ImGui::MenuItem("Delete link")) linksToDelete.push_back(contextLink);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("add_node")) {
        ImVec2 at = ImGui::GetMousePosOnOpeningCurrentPopup();
        auto add = [&](NodeType t, const char* label) {
            int id = graph.AddNode(t, UniqueName(graph, t, label));
            ImNodes::SetNodeScreenSpacePos(id, at);
        };
        ImGui::TextDisabled("Sources");
        for (auto& k : kSourceKinds) if (ImGui::MenuItem(k.label)) add(k.type, k.label);
        ImGui::Separator();
        ImGui::TextDisabled("Transforms");
        for (auto& k : kTransformKinds) if (ImGui::MenuItem(k.label)) add(k.type, k.label);
        ImGui::Separator();
        if (ImGui::MenuItem("MVP Output", nullptr, false, !hasOutput)) add(NodeType::Output, "MVP Output");
        if (numSelNodes + numSelLinks > 0) {
            ImGui::Separator();
            if (ImGui::MenuItem("Delete selected", "Del")) queueSelected();
        }
        ImGui::EndPopup();
    }

    // ---- apply deletions ----
    if (!nodesToDelete.empty() || !linksToDelete.empty()) {
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        for (int id : linksToDelete) graph.RemoveLink(id);
        for (int id : nodesToDelete) graph.RemoveNode(id);
    }

    ImGui::End();
}

// Scene-wide toggles.
static void DrawSettingsWindow(Options& opt, bool* open) {
    if (ImGui::Begin(kWinSettings, open)) {
        ImGui::SeparatorText("Animation");
        ImGui::Checkbox("Play animations", &opt.playAnimations);
        ImGui::SeparatorText("Scene");
        ImGui::Checkbox("Dim what the camera can't see", &opt.dimOutside);
        ImGui::SeparatorText("Perspective divide (view 2)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##warp", &opt.warp, 0.0f, 1.0f, "divide: %.2f")) opt.animateWarp = false;
        ImGui::Checkbox("Animate", &opt.animateWarp);
        ImGui::Checkbox("Ghost of the undivided scene", &opt.showGhost);
        ImGui::Checkbox("Cut away outside [-1, 1]", &opt.clipOutside);
        const char* depthModes[] = {"z: distance, spread evenly", "z: real NDC z (depth buffer)"};
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##depthmode2", &opt.depthMode, depthModes, 2);
        ImGui::SeparatorText("Node editor");
        ImGui::Checkbox("Show matrices on nodes", &opt.showMatrices);
    }
    ImGui::End();
}

// Every matrix in play, in one place.
static void DrawMatricesWindow(NodeGraph& graph, const EvalResult& ev, bool* open) {
    if (ImGui::Begin(kWinMatrices, open)) {
        ImGui::TextWrapped("clip = P * V * M * vertex, and V = inverse(C).");
        if (ImGui::CollapsingHeader("C: camera pose (camera -> world)", ImGuiTreeNodeFlags_DefaultOpen)) MatrixText(ev.cameraPose);
        if (ImGui::CollapsingHeader("V = inverse(C): world -> camera", ImGuiTreeNodeFlags_DefaultOpen)) MatrixText(ev.view);
        const char* pLabel = ev.hasProjection ? "P: camera -> clip space"
                                              : "P: none, identity (clip = V * M * v)";
        if (ImGui::CollapsingHeader(pLabel, ImGuiTreeNodeFlags_DefaultOpen)) MatrixText(ev.projection);
        for (size_t i = 0; i < ev.objects.size(); ++i) {
            const ObjectInstance& o = ev.objects[i];
            Node* src = graph.FindNode(o.sourceNodeId);
            std::string name = src ? src->name : "Object";
            ImGui::PushID((int)i);
            if (ImGui::CollapsingHeader((name + ": M (local -> world)").c_str())) MatrixText(o.model);
            if (ImGui::CollapsingHeader((name + ": P * V * M (local -> clip)").c_str())) MatrixText(ev.projection * ev.view * o.model);
            ImGui::PopID();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
static void BuildStarterGraph(NodeGraph& g) {
    auto place = [](int id, float x, float y) { ImNodes::SetNodeEditorSpacePos(id, ImVec2(x, y)); };

    // Object A: rotate it, then lift it so it sits on the grid.
    int objA = g.AddNode(NodeType::Object, "Object A");
    int rotA = g.AddNode(NodeType::RotateY, "Rotate Y");
    int trA  = g.AddNode(NodeType::Translate, "Translate");
    g.FindNode(rotA)->angleDeg = 30.0f;
    g.FindNode(trA)->vec = glm::vec3(0.0f, 0.5f, 0.0f);

    // Object B: a triangle pushed far away, to show perspective shrinking.
    int objB = g.AddNode(NodeType::Object, "Object B");
    g.FindNode(objB)->shape = kShapeTriangle;
    int trB  = g.AddNode(NodeType::Translate, "Translate");
    g.FindNode(trB)->vec = glm::vec3(1.2f, 0.5f, -9.0f);

    // Camera: a look-at pose, then orbited around the world's Y axis.
    int cam    = g.AddNode(NodeType::Camera, "Camera");
    int camRot = g.AddNode(NodeType::RotateY, "Orbit (Rotate Y)");
    g.FindNode(camRot)->spin = true;
    g.FindNode(camRot)->spinSpeed = 15.0f;

    int proj = g.AddNode(NodeType::Projection, "Projection");
    int out  = g.AddNode(NodeType::Output, "MVP Output");

    g.AddLink(NodeGraph::OutAttr(objA), NodeGraph::InAttr(rotA));
    g.AddLink(NodeGraph::OutAttr(rotA), NodeGraph::InAttr(trA));
    g.AddLink(NodeGraph::OutAttr(trA),  NodeGraph::InAttr(out));
    g.AddLink(NodeGraph::OutAttr(objB), NodeGraph::InAttr(trB));
    g.AddLink(NodeGraph::OutAttr(trB),  NodeGraph::InAttr(out));
    g.AddLink(NodeGraph::OutAttr(cam),  NodeGraph::InAttr(camRot));
    g.AddLink(NodeGraph::OutAttr(camRot), NodeGraph::ViewInAttr(out));
    g.AddLink(NodeGraph::OutAttr(proj), NodeGraph::ProjInAttr(out));

    place(objA, 10, 10);   place(rotA, 150, 10);  place(trA, 400, 10);
    place(objB, 10, 225);  place(trB, 150, 225);
    place(cam, 680, 10);   place(camRot, 940, 10);
    place(proj, 680, 225);
    place(out, 1200, 110);
}

// ---------------------------------------------------------------------------
int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1600, 950, "MVP Visualizer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to init GLAD\n");
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;  // dragging inside a view orbits it instead of moving the window
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ImNodes::CreateContext();
    ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;

    // Scale fonts and spacing for high-DPI screens.
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float uiScale = glm::clamp(glm::max(xscale, yscale), 1.0f, 3.0f);
    ApplyDarkTheme(uiScale);
    LoadFonts(uiScale);

    Renderer renderer;
    renderer.Init();

    Scene scene;
    scene.cube.Upload(CubeVerts(), GL_TRIANGLES);
    scene.triangle.Upload(TriangleVerts(), GL_TRIANGLES);
    scene.grid.Upload(GridVerts(12), GL_LINES);
    scene.axes.Upload(AxesVerts(1.5f), GL_LINES);
    scene.ndcBox.Upload(NdcBoxVerts(), GL_LINES);

    ViewportFBO thinkFbo, actualFbo, cameraFbo;
    OrbitCamera thinkOrbit;  thinkOrbit.target = {0, 0.5f, -2};  thinkOrbit.yawDeg = 40;  thinkOrbit.pitchDeg = 32; thinkOrbit.distance = 24;
    OrbitCamera actualOrbit; actualOrbit.target = {0, 0, -5};    actualOrbit.yawDeg = 42; actualOrbit.pitchDeg = 24; actualOrbit.distance = 22;

    NodeGraph graph;
    ResetGraph(graph, false);  // start with just the MVP Output node
    Options opt;
    WindowVisibility win;
    bool resetLayout = false, firstFrame = true;
    EvalResult ev = graph.Evaluate();
    float cameraAspect = 16.0f / 9.0f;
    double lastTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)glm::min(now - lastTime, 0.1);
        lastTime = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ---------------- Menu bar ----------------
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Quit", "Alt+F4")) glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem(kWinThink, nullptr, &win.think);
                ImGui::MenuItem(kWinActual, nullptr, &win.actual);
                ImGui::MenuItem(kWinCamera, nullptr, &win.camera);
                ImGui::Separator();
                ImGui::MenuItem(kWinEditor, nullptr, &win.editor);
                ImGui::MenuItem(kWinSettings, nullptr, &win.settings);
                ImGui::MenuItem(kWinMatrices, nullptr, &win.matrices);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset layout")) { resetLayout = true; win = WindowVisibility(); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Graph")) {
                if (ImGui::MenuItem("Load starter example")) ResetGraph(graph, true);
                if (ImGui::MenuItem("New empty graph")) ResetGraph(graph, false);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::TextUnformatted(kControlsHelp);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // ---------------- Dockspace over the whole app window ----------------
        ImGuiID dockId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (firstFrame) {
            // No saved layout in imgui.ini yet: build the default one.
            ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId);
            if (!node || !node->IsSplitNode()) resetLayout = true;
            firstFrame = false;
        }
        if (resetLayout) { BuildDefaultLayout(dockId); resetLayout = false; }

        // ---------------- Node Editor, Settings ----------------
        if (win.editor) DrawNodeEditor(graph, opt, ev, &win.editor);
        if (win.settings) DrawSettingsWindow(opt, &win.settings);

        // ---------------- Animate + evaluate ----------------
        if (opt.playAnimations) {
            for (auto& n : graph.nodes) {
                if (n.spin && (n.type == NodeType::RotateX || n.type == NodeType::RotateY || n.type == NodeType::RotateZ)) {
                    n.angleDeg += n.spinSpeed * dt;
                    if (n.angleDeg > 360.0f) n.angleDeg -= 360.0f;
                    if (n.angleDeg < -360.0f) n.angleDeg += 360.0f;
                }
            }
        }
        if (opt.animateWarp) {
            opt.warpPhase += dt * 0.35f;
            opt.warp = 0.5f - 0.5f * std::cos(opt.warpPhase * 3.14159265f);
        }
        for (auto& n : graph.nodes) if (n.type == NodeType::Projection) n.aspect = cameraAspect;
        ev = graph.Evaluate();

        const glm::mat4& V = ev.view;
        const glm::mat4& C = ev.cameraPose;
        const glm::mat4& P = ev.projection;

        scene.frustum.Upload(FrustumVerts(ev), GL_LINES, GL_STREAM_DRAW);


        char buf[160];

        // ---------------- 1. What you think happens ----------------
        if (win.think) {
            if (ImGui::Begin(kWinThink, &win.think)) {
            ImGui::TextWrapped("The world stays still. The camera (pose C) moves through it.");
            {
                ImVec2 size = ImGui::GetContentRegionAvail();
                thinkFbo.Resize((int)size.x, (int)size.y);
                if (thinkFbo.Valid() && size.x > 1 && size.y > 1) {
                    ViewSetup vs;
                    vs.obsView = thinkOrbit.View();
                    vs.obsProj = glm::perspective(glm::radians(45.0f), size.x / size.y, 0.1f, 500.0f);
                    vs.toEye = V;               // scene space = world
                    vs.camProj = P;
                    vs.dimOutside = opt.dimOutside;
                    vs.nearPlane = ev.nearPlane; vs.farPlane = ev.farPlane;
                    vs.bg = kThinkBg;
                    renderer.Begin(thinkFbo, vs);
                    DrawWorld(renderer, scene, ev, opt, glm::mat4(1.0f));
                    DrawCameraGizmo(renderer, scene, C, false);
                    renderer.End();

                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    bool active, hovered;
                    ShowViewport("##think", thinkFbo, size, &active, &hovered);
                    thinkOrbit.HandleInput(active, hovered);
                    glm::vec3 cp(C[3]);
                    std::snprintf(buf, sizeof(buf), "camera position: (%.2f, %.2f, %.2f)", cp.x, cp.y, cp.z);
                    Overlay(pos, {"World: fixed.  Camera: moves by C.", buf, "drag: orbit   wheel: zoom"});
                }
            }
            }
            ImGui::End();
        }

        // ---------------- 2. What actually happens ----------------
        if (win.actual) {
            if (ImGui::Begin(kWinActual, &win.actual)) {
            ImGui::TextWrapped("The camera never moves: it sits at the origin looking down -Z, on a floor that never moves either. "
                               "The world is moved by V = inverse(C). The divide then squeezes what the camera sees into the cube [-1, 1].");
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.35f);
            ImGui::BeginDisabled(!ev.hasProjection);
            if (ImGui::SliderFloat("perspective divide", &opt.warp, 0.0f, 1.0f, "%.2f")) opt.animateWarp = false;
            ImGui::EndDisabled();
            if (!ev.hasProjection && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("No projection connected: w is always 1, so there is nothing to divide.");
            ImGui::SameLine(); ImGui::Checkbox("animate", &opt.animateWarp);
            ImGui::SameLine(); ImGui::Checkbox("ghost", &opt.showGhost);
            const char* depthModes[] = {"z: distance, spread evenly", "z: real NDC z (depth buffer)"};
            ImGui::PopItemWidth();
            ImGui::SetNextItemWidth(260.0f);
            ImGui::Combo("##depthmode", &opt.depthMode, depthModes, 2);
            ImGui::SameLine(); ImGui::Checkbox("cut away outside [-1, 1]", &opt.clipOutside);
            {
                ImVec2 size = ImGui::GetContentRegionAvail();
                actualFbo.Resize((int)size.x, (int)size.y);
                if (actualFbo.Valid() && size.x > 1 && size.y > 1) {
                    ViewSetup vs;
                    vs.obsView = actualOrbit.View();
                    vs.obsProj = glm::perspective(glm::radians(45.0f), size.x / size.y, 0.1f, 500.0f);
                    vs.toEye = glm::mat4(1.0f);  // scene space = eye space
                    vs.camProj = P;
                    // Without a projection w stays 1: the divide changes nothing.
                    float warp = ev.hasProjection ? opt.warp : 0.0f;
                    vs.warp = warp;
                    vs.depthMode = opt.depthMode;
                    vs.dimOutside = opt.dimOutside;
                    vs.nearPlane = ev.nearPlane; vs.farPlane = ev.farPlane;
                    vs.boxHalf = 4.0f;
                    vs.boxCenter = ev.nearPlane + vs.boxHalf;  // near face of the cube sits on the near plane
                    vs.clipOutside = opt.clipOutside && (warp > 0.001f || !ev.hasProjection);
                    vs.bg = kActualBg;
                    renderer.Begin(actualFbo, vs);

                    // The camera's own floor: fixed in camera space, level with the bottom of the cube.
                    // Only the world moves; this never does.
                    glm::mat4 cubeToEye = glm::translate(glm::mat4(1.0f), {0, 0, -vs.boxCenter}) *
                                          glm::scale(glm::mat4(1.0f), {vs.boxHalf, vs.boxHalf, -vs.boxHalf});
                    // No projection: NDC is camera space itself, so the [-1, 1] cube is the
                    // 2x2x2 box around the camera (cube z = -1 is camera z = -1, in front).
                    if (!ev.hasProjection) cubeToEye = glm::mat4(1.0f);
                    float cubeAlpha = ev.hasProjection ? warp : 1.0f;
                    DrawOpts floor; floor.warp = false; floor.dim = false;
                    floor.tint = glm::vec3(0.22f, 0.3f, 0.45f); floor.tintAmount = 0.75f;
                    renderer.Draw(scene.grid, glm::translate(glm::mat4(1.0f), {0, -vs.boxHalf - 0.01f, -vs.boxCenter}), floor);

                    // The [-1,1] cube fades in as the divide is applied.
                    if (cubeAlpha > 0.001f) {
                        DrawOpts box; box.warp = false; box.dim = false;
                        box.tint = vs.bg; box.tintAmount = 1.0f - cubeAlpha;
                        renderer.Draw(scene.ndcBox, cubeToEye, box);
                    }

                    if (opt.showGhost && warp > 0.001f) {
                        // Where things were before the divide, as a faint wireframe.
                        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                        DrawOpts g; g.warp = false; g.dim = false; g.tint = glm::vec3(0.32f, 0.3f, 0.36f); g.tintAmount = 1.0f;
                        DrawWorld(renderer, scene, ev, opt, V, g, false);
                        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                    }
                    DrawWorld(renderer, scene, ev, opt, V, {}, false);  // the world, moved by V (its floor grid is left out)
                    DrawCameraGizmo(renderer, scene, glm::mat4(1.0f), true);
                    renderer.End();

                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    bool active, hovered;
                    ShowViewport("##actual", actualFbo, size, &active, &hovered);
                    actualOrbit.HandleInput(active, hovered);
                    glm::vec3 wo(V[3]);
                    std::snprintf(buf, sizeof(buf), "world origin is now at: (%.2f, %.2f, %.2f)", wo.x, wo.y, wo.z);
                    std::vector<std::string> lines = {"Camera and floor: fixed.  World: moves by V = C^-1.", buf};
                    if (!ev.hasProjection) {
                        lines.push_back("No projection: clip = V * M * v and w stays 1,");
                        lines.push_back("so the divide does nothing and nothing shrinks.");
                        lines.push_back("Only the box [-1, 1] around the camera is drawn.");
                    } else if (warp > 0.001f) {
                        lines.push_back("x and y are divided by w (the distance), so far things shrink");
                        lines.push_back("the frustum becomes the cube [-1, 1]; only what is inside is drawn");
                    }

                    // Labels drawn over the image at 3D positions.
                    glm::mat4 obsVP = vs.obsProj * vs.obsView;
                    auto label = [&](glm::vec3 p, const char* text, ImU32 col) {
                        glm::vec4 c = obsVP * glm::vec4(p, 1.0f);
                        if (c.w <= 0.0f) return;
                        glm::vec2 n = glm::vec2(c) / c.w;
                        if (std::fabs(n.x) > 1.0f || std::fabs(n.y) > 1.0f) return;
                        ImVec2 at(pos.x + (n.x * 0.5f + 0.5f) * size.x, pos.y + (0.5f - n.y * 0.5f) * size.y);
                        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 4, at.y - 6), col, text);
                    };
                    label({0, -0.5f, 0.4f}, "camera (never moves)", IM_COL32(255, 220, 90, 255));
                    if (!ev.hasProjection) {
                        ImU32 col = IM_COL32(255, 255, 255, 255);
                        // x on the top front edge, y on the right front edge, z on the bottom left edge.
                        label({-1, 1, -1}, "x=-1", col);  label({1, 1, -1}, "x=+1  y=+1", col);
                        label({1, -1, -1}, "y=-1", col);
                        label({-1, -1, -1}, "z=-1: depth 0, in front", col);
                        label({-1, -1, 1}, "z=+1: depth 1, behind the camera", col);
                    } else if (cubeAlpha > 0.05f) {
                        ImU32 col = IM_COL32(255, 255, 255, (int)(255 * cubeAlpha));
                        auto cube = [&](float x, float y, float z) { return glm::vec3(cubeToEye * glm::vec4(x, y, z, 1)); };
                        // x and y ticks on the far face, z ticks along the bottom-right edge.
                        label(cube(-1, -1, 1), "x=-1", col);   label(cube(0, -1, 1), "x=0", col);
                        label(cube( 1, -1, 1), "x=+1  y=-1", col);
                        label(cube( 1,  0, 1), "y=0", col);    label(cube(1, 1, 1), "y=+1", col);
                        label(cube( 1, -1, -1), "z=-1 (near)", col);
                        label(cube( 1, -1, 0), "z=0", col);
                    }
                    lines.push_back("drag: orbit   wheel: zoom");
                    Overlay(pos, lines);
                }
            }
            }
            ImGui::End();
        }

        // ---------------- 3. What the camera sees ----------------
        if (win.camera) {
            if (ImGui::Begin(kWinCamera, &win.camera)) {
            ImGui::TextWrapped("The final image: every vertex goes through clip = P * V * M * v, then gets divided by w.");
            {
                ImVec2 size = ImGui::GetContentRegionAvail();
                cameraFbo.Resize((int)size.x, (int)size.y);
                if (size.x > 1 && size.y > 1) cameraAspect = size.x / size.y;
                if (cameraFbo.Valid() && size.x > 1 && size.y > 1) {
                    ViewSetup vs;
                    vs.obsView = V;              // the observer *is* the student's camera
                    vs.obsProj = P;
                    vs.toEye = V;
                    vs.camProj = P;
                    vs.nearPlane = ev.nearPlane; vs.farPlane = ev.farPlane;
                    vs.bg = kCameraBg;
                    renderer.Begin(cameraFbo, vs);
                    DrawWorld(renderer, scene, ev, opt, glm::mat4(1.0f));
                    renderer.End();

                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    bool active, hovered;
                    ShowViewport("##camera", cameraFbo, size, &active, &hovered);
                    if (ev.hasProjection) {
                        std::snprintf(buf, sizeof(buf), "fov %.0f   near %.2f   far %.1f   aspect %.2f",
                                      ev.fovDeg, ev.nearPlane, ev.farPlane, ev.aspect);
                        Overlay(pos, {"Perspective: far objects look smaller.", buf});
                    } else {
                        Overlay(pos, {"No projection: P = identity, so clip = V * M * v.",
                                      "Only x, y, z in [-1, 1] around the camera is visible.",
                                      "Nothing shrinks, the image stretches to the window,",
                                      "and depth is reversed: farther things draw on top."});
                    }
                }
            }
            }
            ImGui::End();
        }

        if (win.matrices) DrawMatricesWindow(graph, ev, &win.matrices);

        // ---------------- Present ----------------
        ImGui::Render();
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImNodes::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
