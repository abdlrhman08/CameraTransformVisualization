#pragma once
// Everything that touches OpenGL: the shader, meshes, offscreen framebuffers,
// the renderer, the shared scene meshes, and the orbiting observer camera.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

struct EvalResult;

// ---------------------------------------------------------------------------
// Meshes
// ---------------------------------------------------------------------------
// Position + color. Every mesh in the app uses this layout.
struct Vertex { float px, py, pz, r, g, b; };

// A vertex buffer plus how to draw it (triangles, lines, ...).
struct Mesh {
    GLuint vao = 0, vbo = 0;
    int count = 0;
    GLenum mode = GL_TRIANGLES;

    // Creates the buffers on first use, then replaces their contents.
    void Upload(const std::vector<Vertex>& verts, GLenum drawMode, GLenum usage = GL_STATIC_DRAW);
    void Free();  // deletes the buffers
};

// Appends one line segment (two vertices) of a single color.
void PushLine(std::vector<Vertex>& v, glm::vec3 a, glm::vec3 b, glm::vec3 color);

// Vertex data for every shape. To add an object shape, write a builder here,
// upload it in Scene::Init, and pick it in DrawWorld.
std::vector<Vertex> CubeVerts();           // unit cube, one color per face
std::vector<Vertex> TriangleVerts(const glm::vec3 corners[3]);  // flat orange triangle
std::vector<Vertex> GridVerts(int half);   // ground grid on y = 0, from -half to +half
std::vector<Vertex> AxesVerts(float len);  // red X, green Y, blue Z
std::vector<Vertex> NdcBoxVerts();         // edges of the cube [-1, 1]^3

// The student's camera volume, in camera space. The first kFrustumWarpable
// vertices are the near/far rectangles and their connecting edges, which get
// warped by the perspective divide. The rest are drawn unwarped.
constexpr int kFrustumWarpable = 24;
std::vector<Vertex> FrustumVerts(const EvalResult& ev);

// ---------------------------------------------------------------------------
// Offscreen render target and renderer
// ---------------------------------------------------------------------------
// An offscreen color + depth target. Each 3D view renders into one and shows
// the color texture inside its ImGui window.
struct ViewportFBO {
    GLuint fbo = 0, colorTex = 0, depthRbo = 0;
    int width = 0, height = 0;

    // Creates or resizes the attachments. Does nothing if the size is unchanged.
    void Resize(int w, int h);
    bool Valid() const { return fbo != 0 && width > 0 && height > 0; }
};
// Per-view state, uploaded once in Renderer::Begin.
struct ViewSetup {
    glm::mat4 obsView{1.0f}, obsProj{1.0f};  // the camera looking at this viewport
    glm::mat4 toEye{1.0f};     // scene space -> student eye space
    glm::mat4 camProj{1.0f};   // the student's projection
    float warp = 0.0f;         // perspective-divide amount; only meaningful when scene space == eye space
    int depthMode = 0;
    bool dimOutside = false;
    float nearPlane = 0.5f, farPlane = 25.0f;
    float boxHalf = 4.0f, boxCenter = 4.5f;  // where the [-1,1] cube is drawn in view 2
    bool clipOutside = false;
    glm::vec3 bg{0.08f};
};

// Per-draw options.
struct DrawOpts {
    glm::vec3 tint{0.0f};
    float tintAmount = 0.0f;
    bool warp = true;   // allow the perspective-divide warp for this draw
    bool dim = true;    // allow dimming and clipping outside the frustum for this draw
};

// Thin wrapper around the scene shader: Begin per view, Draw, then End.
struct Renderer {
    GLuint prog = 0;
    GLint uModel, uToEye, uCamProj, uObsView, uObsProj, uWarp, uDepthMode, uNear, uFar, uBoxHalf, uBoxCenter;
    GLint uTint, uTintAmount, uDimOutside, uClipBehind, uClipOutside, uBg;
    ViewSetup cur;

    void Init();
    void Begin(const ViewportFBO& fbo, const ViewSetup& v);  // binds and clears the target
    void Draw(const Mesh& m, const glm::mat4& model, const DrawOpts& o = {}, int first = 0, int count = -1);
    void End();
};

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------
struct Scene {
    Mesh cube, grid, axes;
    Mesh triangle;  // re-uploaded per triangle object, since each has its own corners
    Mesh frustum;  // rebuilt every frame from the projection settings
    Mesh ndcBox;   // edges of the cube [-1,1]^3 plus the center lines on its floor

    void Init();                             // uploads the static meshes
    void UpdateFrustum(const EvalResult& ev);
};

// Draws the "world": grid, axes and every object of the graph.
// `worldToScene` is identity when the observer looks at the world directly
// (views 1 and 3), and V when the observer looks at eye space (view 2).
void DrawWorld(Renderer& r, Scene& s, const EvalResult& ev, const glm::mat4& worldToScene,
               DrawOpts base = {}, bool drawGrid = true, bool drawAxes = true);

// Camera body + frustum. `camToScene` places the camera in scene space.
void DrawCameraGizmo(Renderer& r, Scene& s, const glm::mat4& camToScene, bool warpFrustum);

// ---------------------------------------------------------------------------
// Observer camera for views 1 and 2: drag to orbit, scroll to zoom.
// This is the "you looking at the scene" camera, not the student's camera.
struct OrbitCamera {
    glm::vec3 target{0.0f};
    float yawDeg = 40.0f, pitchDeg = 30.0f, distance = 20.0f;

    glm::mat4 View() const;

    // `active`: the mouse is dragging on the view. `hovered`: the mouse is over it.
    void HandleInput(bool active, bool hovered);
};
