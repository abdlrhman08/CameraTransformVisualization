#pragma once
// Everything ImGui: options, theme, small widgets, the window layout, and the
// non-3D windows (Node Editor, Settings, Matrices).

#include <imgui.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct NodeGraph;
struct EvalResult;
struct ViewportFBO;

// ---------------------------------------------------------------------------
// User-facing toggles shared by Settings, the node editor and the views
// ---------------------------------------------------------------------------
struct Options {
    bool playAnimations = true;
    bool dimOutside = true;
    bool showMatrices = true;   // matrix readouts on the nodes
    bool showHints = true;      // explanations above and over the 3D views
    bool showLabels = true;     // labels drawn at 3D points (axis ticks, "camera")
    bool cameraGrid = true;     // view 3: draw the floor grid and world axes

    // View 2: perspective divide
    float warp = 0.0f;          // 0 = eye space, 1 = fully divided
    bool animateWarp = false;
    float warpPhase = 0.0f;
    int depthMode = 0;          // 0 = distance spread evenly, 1 = real NDC z
    bool showGhost = true;
    bool clipOutside = false;
};

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------
ImVec4 Hex(unsigned rgb, float a = 1.0f);  // 0xRRGGBB to an ImGui color

// Background colors of the three 3D views.
inline const glm::vec3 kThinkBg(0.075f, 0.085f, 0.105f);
inline const glm::vec3 kActualBg(0.085f, 0.075f, 0.105f);
inline const glm::vec3 kCameraBg(0.035f, 0.04f, 0.05f);

void ApplyDarkTheme(float scale);  // ImGui + imnodes; call after both contexts exist
void LoadFonts(float scale);       // Roboto + Cousine, or ImGui's default font

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
// A dark box with lines of text, drawn over the current window at `at`.
void Overlay(ImVec2 at, const std::vector<std::string>& lines, ImU32 color = IM_COL32(230, 230, 230, 255));

// Shows the FBO as an image filling `size`. An invisible button on top reports
// whether it is being dragged (`active`) or hovered, for orbit controls.
void ShowViewport(const char* id, const ViewportFBO& fbo, ImVec2 size, bool* active, bool* hovered);

// A 4x4 matrix in the monospace font, rows as you'd write them on paper.
void MatrixText(const glm::mat4& m);

// Draws `text` at a 3D point. `viewProj` is the observer's projection * view,
// and `imagePos`/`imageSize` the on-screen rectangle of the viewport image.
void Label3D(const glm::mat4& viewProj, ImVec2 imagePos, ImVec2 imageSize,
             glm::vec3 p, const char* text, ImU32 color);

// ---------------------------------------------------------------------------
// Windows and docking
// To add a window: give it a title and a WindowVisibility flag here, a slot in
// BuildDefaultLayout, a View-menu entry in main.cpp, and draw it from the loop.
// ---------------------------------------------------------------------------
inline const char* kWinThink    = "1. What you think happens";
inline const char* kWinActual   = "2. What actually happens";
inline const char* kWinCamera   = "3. What the camera sees";
inline const char* kWinEditor   = "Node Editor";
inline const char* kWinSettings = "Settings";
inline const char* kWinMatrices = "Matrices";
inline const char* kWinShader   = "Shader";

struct WindowVisibility {
    bool think = true, actual = true, camera = true, editor = true, settings = true, matrices = true, shader = true;
};

// Three views on top, node editor below, Settings + Matrices tabbed to its right.
void BuildDefaultLayout(ImGuiID dockId);

extern const char* kControlsHelp;  // Help menu and the node editor's (?) tooltip

void DrawNodeEditor(NodeGraph& graph, Options& opt, const EvalResult& lastEval, bool* open);
void DrawSettingsWindow(Options& opt, bool* open);
void DrawMatricesWindow(NodeGraph& graph, const EvalResult& ev, bool* open);
// The minimal vertex shader (M, V, P and gl_Position), written from what the graph connects.
void DrawShaderWindow(NodeGraph& graph, const EvalResult& ev, bool* open);
