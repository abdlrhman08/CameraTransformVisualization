#pragma once
// The three 3D windows. Each owns its offscreen framebuffer and, for views 1
// and 2, the orbit camera you look through.
//
//   1. ThinkView   "What you think happens": the world stays still and the
//                  camera (pose C) moves through it.
//   2. ActualView  "What actually happens": the camera never moves and the
//                  world is moved by V = inverse(C). Also shows the
//                  perspective divide squeezing everything into [-1, 1].
//   3. CameraView  "What the camera sees": the final image, P * V * M * v.

#include "Renderer.h"

struct EvalResult;
struct Options;
struct NodeGraph;

// What every view needs to draw one frame.
struct FrameContext {
    Renderer& renderer;
    Scene& scene;
    const EvalResult& ev;
    Options& opt;
    NodeGraph& graph;
    float dt;
};

struct ThinkView {
    ViewportFBO fbo;
    OrbitCamera orbit;
    ThinkView();
    void Draw(FrameContext& ctx, bool* open);
};

struct ActualView {
    ViewportFBO fbo;
    OrbitCamera orbit;
    ActualView();
    void Draw(FrameContext& ctx, bool* open);
};

struct CameraView {
    ViewportFBO fbo;
    float aspect = 16.0f / 9.0f;  // of the window; fed back into the Projection nodes

    // Play mode: fly the student's camera with WASD + mouse. Every move is
    // written back into the node graph, so all windows follow.
    bool playing = false;
    float flySpeed = 4.0f;        // units per second
    const char* playError = nullptr;

    void Draw(FrameContext& ctx, bool* open);

private:
    void Fly(FrameContext& ctx, bool dragging);
};
