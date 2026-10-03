#include "Views.h"
#include "NodeGraph.h"
#include "UI.h"

#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

static glm::mat4 ObserverProj(ImVec2 size) {
    return glm::perspective(glm::radians(45.0f), size.x / size.y, 0.1f, 500.0f);
}

// ---------------------------------------------------------------------------
// 1. What you think happens
// ---------------------------------------------------------------------------
ThinkView::ThinkView() {
    orbit.target = {0, 0.5f, -2}; orbit.yawDeg = 40; orbit.pitchDeg = 32; orbit.distance = 24;
}

void ThinkView::Draw(FrameContext& ctx, bool* open) {
    if (!ImGui::Begin(kWinThink, open)) { ImGui::End(); return; }
    const EvalResult& ev = ctx.ev;
    const glm::mat4& C = ev.cameraPose;

    if (ctx.opt.showHints) ImGui::TextWrapped("The world stays still. The camera (pose C) moves through it.");
    ImVec2 size = ImGui::GetContentRegionAvail();
    fbo.Resize((int)size.x, (int)size.y);
    if (fbo.Valid() && size.x > 1 && size.y > 1) {
        ViewSetup vs;
        vs.obsView = orbit.View();
        vs.obsProj = ObserverProj(size);
        vs.toEye = ev.view;          // scene space = world
        vs.camProj = ev.projection;
        vs.dimOutside = ctx.opt.dimOutside;
        vs.nearPlane = ev.nearPlane; vs.farPlane = ev.farPlane;
        vs.bg = kThinkBg;
        ctx.renderer.Begin(fbo, vs);
        DrawWorld(ctx.renderer, ctx.scene, ev, glm::mat4(1.0f));
        DrawCameraGizmo(ctx.renderer, ctx.scene, C, false);
        ctx.renderer.End();

        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool active, hovered;
        ShowViewport("##think", fbo, size, &active, &hovered);
        orbit.HandleInput(active, hovered);
        char buf[160];
        glm::vec3 cp(C[3]);
        std::snprintf(buf, sizeof(buf), "camera position: (%.2f, %.2f, %.2f)", cp.x, cp.y, cp.z);
        if (ctx.opt.showHints) Overlay(pos, {"World: fixed.  Camera: moves by C.", buf, "drag: orbit   wheel: zoom"});
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// 2. What actually happens
// ---------------------------------------------------------------------------
ActualView::ActualView() {
    orbit.target = {0, 0, -5}; orbit.yawDeg = 42; orbit.pitchDeg = 24; orbit.distance = 22;
}

void ActualView::Draw(FrameContext& ctx, bool* open) {
    if (!ImGui::Begin(kWinActual, open)) { ImGui::End(); return; }
    const EvalResult& ev = ctx.ev;
    Options& opt = ctx.opt;
    Renderer& renderer = ctx.renderer;
    Scene& scene = ctx.scene;

    // ---- controls ----
    if (opt.showHints) ImGui::TextWrapped("The camera never moves: it sits at the origin looking down -Z, on a floor that never moves either. "
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

    ImVec2 size = ImGui::GetContentRegionAvail();
    fbo.Resize((int)size.x, (int)size.y);
    if (!fbo.Valid() || size.x <= 1 || size.y <= 1) { ImGui::End(); return; }

    // ---- render ----
    ViewSetup vs;
    vs.obsView = orbit.View();
    vs.obsProj = ObserverProj(size);
    vs.toEye = glm::mat4(1.0f);  // scene space = eye space
    vs.camProj = ev.projection;
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
    renderer.Begin(fbo, vs);

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
        DrawWorld(renderer, scene, ev, ev.view, g, false);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    DrawWorld(renderer, scene, ev, ev.view, {}, false);  // the world, moved by V (its floor grid is left out)
    DrawCameraGizmo(renderer, scene, glm::mat4(1.0f), true);
    renderer.End();

    // ---- show + overlays ----
    ImVec2 pos = ImGui::GetCursorScreenPos();
    bool active, hovered;
    ShowViewport("##actual", fbo, size, &active, &hovered);
    orbit.HandleInput(active, hovered);

    char buf[160];
    glm::vec3 wo(ev.view[3]);
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

    glm::mat4 obsVP = vs.obsProj * vs.obsView;
    auto label = [&](glm::vec3 p, const char* text, ImU32 col) {
        if (opt.showLabels) Label3D(obsVP, pos, size, p, text, col);
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
    if (opt.showHints) Overlay(pos, lines);
    ImGui::End();
}

// ---------------------------------------------------------------------------
// 3. What the camera sees
// ---------------------------------------------------------------------------
void CameraView::Draw(FrameContext& ctx, bool* open) {
    if (!ImGui::Begin(kWinCamera, open)) { ImGui::End(); return; }
    const EvalResult& ev = ctx.ev;

    if (ctx.opt.showHints) ImGui::TextWrapped("The final image: every vertex goes through clip = P * V * M * v, then gets divided by w.");

    // ---- play controls ----
    if (playing) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(170, 60, 60, 255));
        if (ImGui::Button("Stop")) playing = false;
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(45, 130, 70, 255));
        if (ImGui::Button("Play")) { playing = true; playError = nullptr; ImGui::SetWindowFocus(); }
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fly this camera: WASD move, Q/E down/up, Shift faster,\n"
                          "drag the image to look around, Esc to stop.\n"
                          "Moves are written into the node graph.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("speed", &flySpeed, 0.5f, 20.0f, "%.1f");
    ImGui::SameLine();
    ImGui::Checkbox("grid and axes", &ctx.opt.cameraGrid);
    if (playError) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", playError);

    ImVec2 size = ImGui::GetContentRegionAvail();
    fbo.Resize((int)size.x, (int)size.y);
    if (size.x > 1 && size.y > 1) aspect = size.x / size.y;
    if (fbo.Valid() && size.x > 1 && size.y > 1) {
        ViewSetup vs;
        vs.obsView = ev.view;        // the observer *is* the student's camera
        vs.obsProj = ev.projection;
        vs.toEye = ev.view;
        vs.camProj = ev.projection;
        vs.nearPlane = ev.nearPlane; vs.farPlane = ev.farPlane;
        vs.bg = kCameraBg;
        ctx.renderer.Begin(fbo, vs);
        DrawWorld(ctx.renderer, ctx.scene, ev, glm::mat4(1.0f), {}, ctx.opt.cameraGrid, ctx.opt.cameraGrid);
        ctx.renderer.End();

        ImVec2 pos = ImGui::GetCursorScreenPos();
        bool active, hovered;
        ShowViewport("##camera", fbo, size, &active, &hovered);
        if (playing) {
            ImGui::GetWindowDrawList()->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                                IM_COL32(90, 200, 120, 255), 0.0f, 0, 2.0f);
            Fly(ctx, active);
        }
        if (!ctx.opt.showHints) {
            // hints hidden
        } else if (ev.customProjection) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "depth -1 at %.2f, depth +1 at %.2f in front of the camera", ev.nearPlane, ev.farPlane);
            Overlay(pos, {ev.perspective ? "Custom projection with a divide: size depends on distance."
                                         : "Custom projection, w stays 1: no perspective shrinking.", buf});
        } else if (ev.hasProjection) {
            char buf[160];
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
    ImGui::End();
}

// Moves the student's camera from its current pose C and writes the result
// back into the graph. Look: yaw around world up, pitch around the camera's
// right axis. Move: along the camera's forward/right, and world up for Q/E.
void CameraView::Fly(FrameContext& ctx, bool dragging) {
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGuiIO& io = ImGui::GetIO();
    if (focused && ImGui::IsKeyPressed(ImGuiKey_Escape)) { playing = false; return; }

    const glm::mat4& C = ctx.ev.cameraPose;
    glm::vec3 pos(C[3]);
    glm::vec3 fwd = -glm::normalize(glm::vec3(C[2]));
    float yaw = std::atan2(fwd.x, -fwd.z);
    float pitch = std::asin(glm::clamp(fwd.y, -1.0f, 1.0f));

    bool changed = false;
    if (dragging && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        yaw += io.MouseDelta.x * 0.004f;
        pitch = glm::clamp(pitch - io.MouseDelta.y * 0.004f, glm::radians(-89.0f), glm::radians(89.0f));
        changed = true;
    }
    fwd = glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw));
    glm::vec3 up(0, 1, 0);
    glm::vec3 right = glm::normalize(glm::cross(fwd, up));

    if (focused && !io.WantTextInput) {
        glm::vec3 move(0.0f);
        if (ImGui::IsKeyDown(ImGuiKey_W)) move += fwd;
        if (ImGui::IsKeyDown(ImGuiKey_S)) move -= fwd;
        if (ImGui::IsKeyDown(ImGuiKey_D)) move += right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) move -= right;
        if (ImGui::IsKeyDown(ImGuiKey_E)) move += up;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) move -= up;
        if (glm::length(move) > 0.0f) {
            float speed = flySpeed * (io.KeyShift ? 3.0f : 1.0f);
            pos += glm::normalize(move) * speed * ctx.dt;
            changed = true;
        }
    }
    if (!changed) return;

    glm::mat4 newPose = glm::inverse(glm::lookAt(pos, pos + fwd, up));
    playError = ctx.graph.SetCameraPose(newPose);
    if (playError) playing = false;
}
