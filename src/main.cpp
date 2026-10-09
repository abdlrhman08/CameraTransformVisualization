// MVP Visualizer: a teaching tool for the Model / View / Projection matrices.
//
// Source layout:
//   main.cpp       window + ImGui setup, menu bar, docking, the frame loop
//   NodeGraph.*    the node graph data model, its evaluation, and presets
//   Renderer.*     OpenGL: shader, meshes, framebuffers, scene drawing, orbit camera
//   Model.*        model files (OBJ, PLY, STL, glTF, ...) loaded with Assimp
//   UI.*           theme, widgets, window layout, Node Editor / Settings / Matrices
//   Views.*        the three 3D windows

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilderGetNode
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imnodes.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Model.h"
#include "NodeGraph.h"
#include "Renderer.h"
#include "UI.h"
#include "Views.h"

// Model files dropped on the window, picked up by the frame loop.
static std::vector<std::string> gDroppedFiles;
static void OnDrop(GLFWwindow*, int count, const char** paths) {
    for (int i = 0; i < count; ++i) gDroppedFiles.push_back(paths[i]);
}

// Each opened or dropped model becomes a new Object node linked to the Output.
static void AddModelNode(NodeGraph& graph, const std::string& path) {
    static int added = 0;
    float off = (float)(added++ % 8) * 40.0f;
    AddModelObject(graph, path, 80.0f + off, 80.0f + off);
}

static void DrawMenuBar(GLFWwindow* window, NodeGraph& graph, Options& opt, WindowVisibility& win, bool& resetLayout) {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open model...", "Ctrl+O")) {
            std::string path = OpenModelDialog();
            if (!path.empty()) AddModelNode(graph, path);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Adds an Object showing the file. You can also drop files on the window.");
        ImGui::Separator();
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
        ImGui::MenuItem("Explanations on the views", "H", &opt.showHints);
        ImGui::MenuItem("Labels in the 3D scene", "L", &opt.showLabels);
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

int main(int argc, char** argv) {
    // ---------------- Window, OpenGL, ImGui ----------------
    // Windows dragged out of the app become real OS windows (ImGui multi-viewports).
    // This ImGui version can't place windows on Wayland, so prefer X11 when GLFW
    // has it (on a Wayland desktop that runs through XWayland).
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    if (glfwPlatformSupported(GLFW_PLATFORM_X11)) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
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
    glfwSetDropCallback(window, OnDrop);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::fprintf(stderr, "Failed to init GLAD\n");
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // windows can be dragged outside the app
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
    // Popped-out windows are OS windows: square corners and an opaque background.
    ImGui::GetStyle().WindowRounding = 0.0f;
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;

    // ---------------- App state ----------------
    Renderer renderer;
    renderer.Init();
    Scene scene;
    scene.Init();

    ThinkView thinkView;
    ActualView actualView;
    CameraView cameraView;

    NodeGraph graph;
    ResetGraph(graph, false);  // start with just the MVP Output node
    for (int i = 1; i < argc; ++i) AddModelNode(graph, argv[i]);  // model files given on the command line
    Options opt;
    WindowVisibility win;
    bool resetLayout = false, firstFrame = true;
    EvalResult ev = graph.Evaluate();
    double lastTime = glfwGetTime();

    // ---------------- Frame loop ----------------
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)glm::min(now - lastTime, 0.1);
        lastTime = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawMenuBar(window, graph, opt, win, resetLayout);

        for (auto& path : gDroppedFiles) AddModelNode(graph, path);
        gDroppedFiles.clear();

        // H and L toggle the text hints, unless the user is typing into a field.
        if (!io.WantTextInput) {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                std::string path = OpenModelDialog();
                if (!path.empty()) AddModelNode(graph, path);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_H, false)) opt.showHints = !opt.showHints;
            if (ImGui::IsKeyPressed(ImGuiKey_L, false)) opt.showLabels = !opt.showLabels;
        }

        // Dockspace over the whole app window. Build the default layout when
        // imgui.ini has no saved one, or when the user asks for a reset.
        ImGuiID dockId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (firstFrame) {
            ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId);
            if (!node || !node->IsSplitNode()) resetLayout = true;
            firstFrame = false;
        }
        if (resetLayout) { BuildDefaultLayout(dockId); resetLayout = false; }

        // Editing windows first, so this frame's edits show up in the views.
        if (win.editor) DrawNodeEditor(graph, opt, ev, &win.editor);
        if (win.settings) DrawSettingsWindow(opt, &win.settings);

        // Animate and evaluate the graph.
        // Spinning nodes would fight the user while flying, so they pause in play mode.
        if (opt.playAnimations && !cameraView.playing) graph.Animate(dt);
        if (opt.animateWarp) {
            opt.warpPhase += dt * 0.35f;
            opt.warp = 0.5f - 0.5f * std::cos(opt.warpPhase * 3.14159265f);
        }
        for (auto& n : graph.nodes) if (n.type == NodeType::Projection) n.aspect = cameraView.aspect;
        ev = graph.Evaluate();
        scene.UpdateFrustum(ev);

        // The three 3D views, then the matrices panel.
        FrameContext ctx{renderer, scene, ev, opt, graph, dt};
        if (win.think)  thinkView.Draw(ctx, &win.think);
        if (win.actual) actualView.Draw(ctx, &win.actual);
        if (win.camera) cameraView.Draw(ctx, &win.camera);
        if (win.matrices) DrawMatricesWindow(graph, ev, &win.matrices);

        // Present.
        ImGui::Render();
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Draw the windows that were dragged outside, then switch back to the
        // main window's GL context (each OS window has its own, sharing textures).
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(window);
        }
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
