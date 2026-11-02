#include "NodeGraph.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <utility>
#include <imgui.h>
#include <imnodes.h>
#include <glm/gtc/matrix_transform.hpp>

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------
void Node::RecomputeLocal() {
    float angle = angleDeg * (IsRotation(type) ? animFraction : 1.0f);
    switch (type) {
        case NodeType::Translate: localMatrix = glm::translate(glm::mat4(1.0f), vec); break;
        case NodeType::RotateX:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(1, 0, 0)); break;
        case NodeType::RotateY:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0, 1, 0)); break;
        case NodeType::RotateZ:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angle), glm::vec3(0, 0, 1)); break;
        case NodeType::Scale:     localMatrix = glm::scale(glm::mat4(1.0f), vec); break;
        case NodeType::Matrix:    localMatrix = custom; break;
        case NodeType::Camera:    localMatrix = CameraBasePose(); break;
        case NodeType::Projection:localMatrix = ProjectionMatrix(); break;
        case NodeType::PlanarProjection: localMatrix = exactSlides ? PlanarSlideMatrix() : PlanarGpuMatrix(); break;
        default:                  localMatrix = glm::mat4(1.0f); break;
    }
    // Animated: blend from the identity (a projection fades in its divide this way).
    if (animFraction < 1.0f && !IsRotation(type))
        localMatrix = (1.0f - animFraction) * glm::mat4(1.0f) + animFraction * localMatrix;
}

bool CanAnimate(const Node& n) {
    if (n.type == NodeType::PlanarProjection) return n.PlanarSteps().size() == 1;
    return IsTransform(n.type) || n.type == NodeType::Projection;
}

// The camera's pose in the world: inverse of lookAt. Guards against the
// degenerate cases (target == position, looking straight up/down).
glm::mat4 Node::CameraBasePose() const {
    glm::vec3 target = camTarget;
    if (glm::length(target - camPos) < 1e-4f) target = camPos + glm::vec3(0, 0, -1);
    glm::vec3 dir = glm::normalize(target - camPos);
    glm::vec3 up(0, 1, 0);
    if (std::fabs(glm::dot(dir, up)) > 0.999f) up = glm::vec3(0, 0, -1);
    return glm::inverse(glm::lookAt(camPos, target, up));
}

glm::mat4 Node::ProjectionMatrix() const {
    float n = glm::max(nearPlane, 0.001f);
    float f = glm::max(farPlane, n + 0.01f);
    return glm::perspective(glm::radians(glm::clamp(fovDeg, 1.0f, 170.0f)), glm::max(aspect, 0.01f), n, f);
}

// ---------------------------------------------------------------------------
// Planar projections (projections.pdf)
// ---------------------------------------------------------------------------
const PlanarInfo& GetPlanarInfo(int kind) {
    static const PlanarInfo kInfo[kPlanarCount] = {
        {"Front view", "Multi-view", "z is dropped: the xy-plane, as is"},
        {"Side view", "Multi-view", "x is dropped: the yz-plane, as is"},
        {"Top view", "Multi-view", "y is dropped: the zx-plane, as is"},
        {"Isometric", "Axonometric", "normal [n, n, n]: x, y, z shrink equally"},
        {"Dimetric", "Axonometric", "normal [n, m, n]: two axes shrink equally"},
        {"Trimetric", "Axonometric", "no two axes shrink equally"},
        {"Axonometric", "Axonometric", "rotate -theta about y, phi about x, then front view"},
        {"Cavalier", "Oblique", "alpha = 45: lines along z keep their length"},
        {"Cabinet", "Oblique", "alpha = 63.4: lines along z at half length"},
        {"Oblique", "Oblique", "lines along z at cot(alpha) of their length"},
        {"One-point", "Perspective", "COP at the origin, view plane at distance d"},
        {"One-point, plane at z = 0", "Perspective", "view plane at z = 0, COP at distance d"},
        {"Two-point", "Perspective", "view plane turned by theta about y"},
        {"Three-point", "Perspective", "view plane turned by theta about y and phi about x"},
    };
    return kInfo[glm::clamp(kind, 0, kPlanarCount - 1)];
}

void Node::PlanarAngles(float& t, float& p) const {
    switch (planar) {
        case kPlanarFront:     t = 0.0f;  p = 0.0f;  break;
        case kPlanarSide:      t = 90.0f; p = 0.0f;  break;
        case kPlanarTop:       t = 0.0f;  p = 90.0f; break;
        // phi = atan(n / (sqrt(2) n)) = 35.26 for [n, n, n], atan(m / (sqrt(2) n)) for [n, m, n].
        case kPlanarIsometric: t = 45.0f; p = glm::degrees(std::atan(1.0f / std::sqrt(2.0f))); break;
        case kPlanarDimetric:  t = 45.0f; p = glm::degrees(std::atan(dimetricRatio / std::sqrt(2.0f))); break;
        case kPlanarTrimetric: {
            glm::vec3 nv = viewNormal;
            if (glm::length(nv) < 1e-6f) nv = glm::vec3(0, 0, 1);
            t = glm::degrees(std::atan2(nv.x, nv.z));
            p = glm::degrees(std::atan2(nv.y, std::sqrt(nv.x * nv.x + nv.z * nv.z)));
            break;
        }
        default: t = theta; p = phi; break;
    }
}

// How long lines along z are drawn by an oblique projection: cot(alpha).
static float ObliqueLength(const Node& n) {
    if (n.planar == kPlanarCavalier) return 1.0f;
    if (n.planar == kPlanarCabinet) return 0.5f;
    return 1.0f / std::tan(glm::radians(glm::clamp(n.obliqueAlpha, 1.0f, 90.0f)));
}

static glm::mat4 RotY(float deg) { return glm::rotate(glm::mat4(1.0f), glm::radians(deg), glm::vec3(0, 1, 0)); }
static glm::mat4 RotX(float deg) { return glm::rotate(glm::mat4(1.0f), glm::radians(deg), glm::vec3(1, 0, 0)); }

// The homogeneous matrices of the slides, entry for entry (m[col][row]).
// They map a point onto the view plane, so most drop a coordinate and have det 0.
glm::mat4 Node::PlanarSlideMatrix() const {
    glm::mat4 S(1.0f);
    float d = glm::max(viewDist, 0.01f);
    switch (planar) {
        case kPlanarFront: S[2][2] = 0.0f; break;
        case kPlanarSide:  S[0][0] = 0.0f; break;
        case kPlanarTop:   S[1][1] = 0.0f; break;
        case kPlanarIsometric: case kPlanarDimetric: case kPlanarTrimetric: case kPlanarAxonometric: {
            // 3 project (front view) * 2 rotate phi about x * 1 rotate -theta about y
            float t, p;
            PlanarAngles(t, p);
            glm::mat4 front(1.0f);
            front[2][2] = 0.0f;
            S = front * RotX(p) * RotY(-t);
            break;
        }
        case kPlanarCavalier: case kPlanarCabinet: case kPlanarOblique: {
            float L = ObliqueLength(*this), a = glm::radians(obliqueAngle);
            S[2][0] = L * std::cos(a);
            S[2][1] = L * std::sin(a);
            S[2][2] = 0.0f;
            break;
        }
        case kPlanarOnePoint:        S[2][3] = 1.0f / d; S[3][3] = 0.0f; break;
        case kPlanarOnePointShifted: S[2][2] = 0.0f; S[2][3] = 1.0f / d; break;
        case kPlanarTwoPoint: {
            float t = glm::radians(theta);
            S[0][3] = std::sin(t) / d; S[2][3] = std::cos(t) / d; S[3][3] = 0.0f;
            break;
        }
        case kPlanarThreePoint: {
            float t = glm::radians(theta), p = glm::radians(phi);
            S[0][3] = std::sin(t) / d;
            S[1][3] = std::cos(t) * std::sin(p) / d;
            S[2][3] = std::cos(t) * std::cos(p) / d;
            S[3][3] = 0.0f;
            break;
        }
        default: break;
    }
    return S;
}

// The same image, in a form the GPU can draw:
//  - the view plane faces the screen. Side, top, two- and three-point skip the
//    slides' last step, which puts the image back on a plane in the world.
//  - z is kept as depth, so the depth buffer can tell what is in front.
//  - perspective looks down -z like OpenGL (the slides look down +z), so w = -z/d.
//  - x and y are scaled so +-windowHalf of the view plane fills [-1, 1].
glm::mat4 Node::PlanarGpuMatrix() const {
    float h = glm::max(windowHalf, 0.01f), a = glm::max(aspect, 0.01f);
    glm::mat4 window = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / (h * a), 1.0f / h, 1.0f));
    glm::mat4 G(1.0f);
    if (IsPlanarPerspective(planar)) {
        // s = distance in front of the COP. NDC z = (A s + B) / (s / d) is -1 at near, +1 at far.
        float d = glm::max(viewDist, 0.01f);
        float n = glm::max(nearPlane, 0.001f), f = glm::max(farPlane, n + 0.01f);
        float A = (f + n) / (d * (f - n)), B = -2.0f * f * n / (d * (f - n));
        G[2][2] = -A; G[3][2] = B;              // s = -z
        G[2][3] = -1.0f / d; G[3][3] = 0.0f;    // w = s / d
        if (planar == kPlanarOnePointShifted) {
            // COP at z = d, view plane at z = 0: s = d - z.
            G[3][2] = A * d + B;
            G[3][3] = 1.0f;
        }
        if (planar == kPlanarTwoPoint) G = G * RotY(-theta);
        if (planar == kPlanarThreePoint) {
            // Turn the slides' normal n = (sin t, cos t sin p, cos t cos p) onto z
            // without rolling the image, so the horizon stays level.
            float t = glm::radians(theta), p = glm::radians(phi);
            glm::vec3 nv(std::sin(t), std::cos(t) * std::sin(p), std::cos(t) * std::cos(p));
            float az = glm::degrees(std::atan2(nv.x, nv.z));
            float el = glm::degrees(std::asin(glm::clamp(nv.y, -1.0f, 1.0f)));
            G = G * RotX(el) * RotY(-az);
        }
    } else if (IsPlanarOblique(planar)) {
        float L = ObliqueLength(*this), ang = glm::radians(obliqueAngle);
        G[2][0] = L * std::cos(ang);
        G[2][1] = L * std::sin(ang);
        G[2][2] = -1.0f / glm::max(depthRange, 0.01f);
    } else {
        // Every multi-view and axonometric view: rotate the normal onto z, then front view.
        float t, p;
        PlanarAngles(t, p);
        G = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f / glm::max(depthRange, 0.01f))) * RotX(p) * RotY(-t);
    }
    return window * G;
}

glm::mat4 ProjStep::At(float f) const {
    if (axis >= 0) {
        glm::vec3 ax(0.0f);
        ax[axis] = 1.0f;
        return glm::rotate(glm::mat4(1.0f), glm::radians(deg * f), ax);
    }
    return (1.0f - f) * glm::mat4(1.0f) + f * target;
}

static std::string Format(const char* fmt, double a = 0.0, double b = 0.0) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    return buf;
}

// The slides' steps, in camera space. Perspective looks down -z like the GPU
// form, so the view plane is at z = -d and points slide towards the COP.
std::vector<ProjStep> Node::PlanarSteps() const {
    std::vector<ProjStep> steps;
    float h = glm::max(windowHalf, 0.01f), a = glm::max(aspect, 0.01f), d = glm::max(viewDist, 0.01f);
    glm::mat4 square = glm::scale(glm::mat4(1.0f), glm::vec3(h * a, h, 1.0f));
    const char* axisName[3] = {"x", "y", "z"};
    auto rotate = [&](int axis, float deg, const char* why) {
        ProjStep s;
        s.label = Format("Rotate %.2f deg about ", deg) + axisName[axis] + why;
        s.axis = axis;
        s.deg = deg;
        steps.push_back(s);
    };
    auto project = [&](const std::string& label, const glm::mat4& P, const glm::mat4& plane) {
        ProjStep s;
        s.label = label;
        s.target = P;
        s.projects = true;
        s.plane = plane;
        steps.push_back(s);
    };
    glm::mat4 dropZ(1.0f);
    dropZ[2][2] = 0.0f;
    // One-point onto z = -d: x and y scale by d / (-z), and w = -z/d.
    glm::mat4 onePoint(1.0f);
    onePoint[2][3] = -1.0f / d;
    onePoint[3][3] = 0.0f;
    glm::mat4 onPlaneD = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -d)) * square;
    std::string onePointLabel = Format("Project towards the COP onto the plane z = -%.2f (one-point)", d);

    switch (planar) {
        case kPlanarFront: project("Project: drop z, onto the xy-plane", dropZ, square); break;
        case kPlanarSide: {
            glm::mat4 P(1.0f);
            P[0][0] = 0.0f;
            project("Project: drop x, onto the yz-plane", P, RotY(90.0f) * square);
            break;
        }
        case kPlanarTop: {
            glm::mat4 P(1.0f);
            P[1][1] = 0.0f;
            project("Project: drop y, onto the zx-plane", P, RotX(-90.0f) * square);
            break;
        }
        case kPlanarIsometric: case kPlanarDimetric: case kPlanarTrimetric: case kPlanarAxonometric: {
            float t, p;
            PlanarAngles(t, p);
            rotate(1, -t, " (-theta)");
            rotate(0, p, " (phi): the normal is now on z");
            project("Project: front view, drop z", dropZ, square);
            break;
        }
        case kPlanarCavalier: case kPlanarCabinet: case kPlanarOblique:
            project(Format("Project along slanted projectors (lines along z at %.2f)", ObliqueLength(*this)),
                    PlanarSlideMatrix(), square);
            break;
        case kPlanarOnePoint: project(onePointLabel, onePoint, onPlaneD); break;
        case kPlanarOnePointShifted: {
            // COP at z = d, view plane z = 0: w = (d - z) / d.
            glm::mat4 P(1.0f);
            P[2][2] = 0.0f;
            P[2][3] = -1.0f / d;
            project(Format("Project towards the COP at z = %.2f onto the plane z = 0", d), P, square);
            break;
        }
        case kPlanarTwoPoint:
            rotate(1, -theta, " (-theta): the normal is now on z");
            project(onePointLabel, onePoint, onPlaneD);
            rotate(1, theta, " (theta): rotate back");
            break;
        case kPlanarThreePoint:
            rotate(0, phi, " (phi): the normal is now in the zx-plane");
            rotate(1, -theta, " (-theta): the normal is now on z");
            project(onePointLabel, onePoint, onPlaneD);
            rotate(1, theta, " (theta): rotate back");
            rotate(0, -phi, " (-phi): rotate back");
            break;
        default: break;
    }
    return steps;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------
int NodeGraph::AddNode(NodeType type, const std::string& name) {
    Node n;
    n.id = nextNodeId++;
    n.type = type;
    n.name = name;
    if (type == NodeType::Scale) n.vec = glm::vec3(1.0f);
    n.RecomputeLocal();
    nodes.push_back(n);
    return n.id;
}

void NodeGraph::RemoveNode(int id) {
    for (size_t i = 0; i < links.size();) {
        if (NodeOfAttr(links[i].startAttr) == id || NodeOfAttr(links[i].endAttr) == id)
            links.erase(links.begin() + i);
        else
            ++i;
    }
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        if (it->id == id) { nodes.erase(it); return; }
    }
}

bool NodeGraph::AddLink(int a, int b) {
    // imnodes reports the output pin first, but normalize anyway.
    if (!IsOutputAttr(a) && IsOutputAttr(b)) std::swap(a, b);
    if (!IsOutputAttr(a) || IsOutputAttr(b)) return false;
    if (NodeOfAttr(a) == NodeOfAttr(b)) return false;
    Node* dst = FindNode(NodeOfAttr(b));
    if (!dst) return false;

    // The Output node's Model pin accepts many objects. Every other input
    // accepts exactly one link, so a new link replaces the old one.
    bool multi = dst->type == NodeType::Output && b == InAttr(dst->id);
    for (auto it = links.begin(); it != links.end(); ++it) {
        if (it->endAttr == b && (!multi || it->startAttr == a)) { links.erase(it); break; }
    }
    links.push_back({nextLinkId++, a, b});
    return true;
}

void NodeGraph::RemoveLink(int linkId) {
    for (auto it = links.begin(); it != links.end(); ++it) {
        if (it->id == linkId) { links.erase(it); return; }
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
Node* NodeGraph::FindNode(int id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

Node* NodeGraph::OutputNode() {
    for (auto& n : nodes) if (n.type == NodeType::Output) return &n;
    return nullptr;
}

Node* NodeGraph::Upstream(int inAttr) {
    for (auto& l : links) if (l.endAttr == inAttr) return FindNode(NodeOfAttr(l.startAttr));
    return nullptr;
}

std::vector<Node*> NodeGraph::AllUpstream(int inAttr) {
    std::vector<Node*> out;
    for (auto& l : links)
        if (l.endAttr == inAttr)
            if (Node* n = FindNode(NodeOfAttr(l.startAttr))) out.push_back(n);
    return out;
}

int NodeGraph::SourceOf(Node* n) {
    int guard = 0;
    while (n && IsTransform(n->type) && guard++ < 256) n = Upstream(InAttr(n->id));
    return n ? n->id : -1;
}

// ---------------------------------------------------------------------------
// Animation and evaluation
// ---------------------------------------------------------------------------
const char* NodeGraph::SetCameraPose(const glm::mat4& C) {
    Node* out = OutputNode();
    if (!out) return "There is no MVP Output node.";

    // The camera's pose as a position + look-at. Roll is dropped, like lookAt.
    auto writeCamera = [](Node& cam, const glm::mat4& pose) {
        glm::vec3 pos(pose[3]);
        glm::vec3 fwd = -glm::normalize(glm::vec3(pose[2]));
        float dist = glm::max(glm::length(cam.camTarget - cam.camPos), 1.0f);
        cam.camPos = pos;
        cam.camTarget = pos + fwd * dist;
    };

    Node* end = Upstream(ViewInAttr(out->id));
    if (!end) {
        // Nothing drives the view yet: add a Camera node next to the Output.
        // AddNode can reallocate `nodes`, so keep the id, not the pointer.
        int outId = out->id;
        int id = AddNode(NodeType::Camera, "Camera");
        writeCamera(*FindNode(id), C);
        AddLink(OutAttr(id), ViewInAttr(outId));
        ImNodes::SetNodeGridSpacePos(id, ImVec2(300.0f, 220.0f));
        return nullptr;
    }
    if (end->kind == ChainKind::Invalid) return "The View chain has a cycle.";

    // Walk to the first node of the chain.
    Node* start = end;
    for (int guard = 0; IsTransform(start->type) && guard < 256; ++guard) {
        Node* up = Upstream(InAttr(start->id));
        if (!up) break;
        start = up;
    }
    if (start->type != NodeType::Camera && start->type != NodeType::Matrix)
        return "The View chain must start with a Camera or a Matrix node.";

    // The chain is C = T * L, with L the first node's matrix and T everything
    // after it. So T = end.cumulative * L^-1, and the new L is T^-1 * C, which
    // simplifies to L * end.cumulative^-1 * C.
    if (std::fabs(glm::determinant(end->cumulativeMatrix)) < 1e-8f) return "The View chain can't be inverted.";
    glm::mat4 L = start->cumulativeMatrix * glm::inverse(end->cumulativeMatrix) * C;
    if (start->type == NodeType::Matrix) start->custom = L;
    else writeCamera(*start, L);
    return nullptr;
}

void NodeGraph::Animate(float dt) {
    for (auto& n : nodes) {
        if (!n.spin || !IsRotation(n.type)) continue;
        n.angleDeg += n.spinSpeed * dt;
        if (n.angleDeg > 360.0f) n.angleDeg -= 360.0f;
        if (n.angleDeg < -360.0f) n.angleDeg += 360.0f;
    }
    if (animPlaying) {
        float count = (float)animNodes.size();
        animT += 0.6f * dt;
        if (animT >= count) { animT = count; animPlaying = false; }
    }
}

bool NodeGraph::IsAnimated(int id) const {
    for (int a : animNodes) if (a == id) return true;
    return false;
}

int NodeGraph::StartAnimation(const std::vector<int>& ids) {
    // Pipeline order: clip = P * V * M * v, so M chains first, then V, then P.
    // Within a chain, upstream first. Nodes connected to nothing come last.
    Node* out = OutputNode();
    auto pinOf = [&](Node* n) {
        int guard = 0;
        for (Node* e = n; e && guard++ < 256; ) {
            for (auto& l : links) {
                if (l.startAttr != OutAttr(e->id) || !out || NodeOfAttr(l.endAttr) != out->id) continue;
                if (l.endAttr == InAttr(out->id)) return 0;
                if (l.endAttr == ViewInAttr(out->id)) return 1;
                if (l.endAttr == ProjInAttr(out->id)) return 2;
            }
            // Follow the first link out of e towards the Output.
            Node* next = nullptr;
            for (auto& l : links)
                if (l.startAttr == OutAttr(e->id)) { next = FindNode(NodeOfAttr(l.endAttr)); break; }
            e = next;
        }
        return 3;
    };
    auto depthOf = [&](Node* n) {
        int d = 0;
        for (Node* u = n; u && IsTransform(u->type) && d < 256; u = Upstream(InAttr(u->id))) ++d;
        return d;
    };
    struct Item { int pin, source, depth, id; };
    std::vector<Item> items;
    for (int id : ids) {
        Node* n = FindNode(id);
        if (!n || !CanAnimate(*n)) continue;
        items.push_back({pinOf(n), SourceOf(n), depthOf(n), id});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.pin != b.pin) return a.pin < b.pin;
        if (a.source != b.source) return a.source < b.source;
        return a.depth < b.depth;
    });
    animNodes.clear();
    for (auto& it : items) animNodes.push_back(it.id);
    animT = 0.0f;
    animPlaying = !animNodes.empty();
    return (int)animNodes.size();
}

std::vector<int> NodeGraph::ProjectionChain() {
    std::vector<int> ids;
    Node* out = OutputNode();
    for (Node* n = out ? Upstream(ProjInAttr(out->id)) : nullptr; n && IsTransform(n->type) && ids.size() < 64;
         n = Upstream(InAttr(n->id)))
        ids.push_back(n->id);
    return ids;
}

std::vector<ProjStep> NodeGraph::ProjectionSteps(int nodeId) {
    std::vector<Node*> chain;  // walked backwards, from nodeId up to the source
    for (Node* n = FindNode(nodeId); n && chain.size() < 64; n = IsTransform(n->type) ? Upstream(InAttr(n->id)) : nullptr)
        chain.push_back(n);
    std::vector<ProjStep> steps;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        Node* n = *it;
        size_t first = steps.size();
        if (n->type == NodeType::PlanarProjection) {
            for (ProjStep& s : n->PlanarSteps()) steps.push_back(s);
        } else if (IsRotation(n->type)) {
            ProjStep s;
            s.axis = n->type == NodeType::RotateX ? 0 : n->type == NodeType::RotateY ? 1 : 2;
            s.deg = n->angleDeg;
            s.label = n->name + Format(": rotate %.2f deg about ", n->angleDeg) + "xyz"[s.axis];
            steps.push_back(s);
        } else if (n->type != NodeType::Object) {
            // Its whole matrix, not the part an animation has applied so far.
            Node full = *n;
            full.animFraction = 1.0f;
            full.RecomputeLocal();
            ProjStep s;
            s.label = n->name;
            s.target = full.localMatrix;
            steps.push_back(s);
        }
        for (size_t i = first; i < steps.size(); ++i) steps[i].nodeId = n->id;
    }
    return steps;
}

void SetPlanarKind(Node& n, int kind) {
    const std::string oldName = GetPlanarInfo(n.planar).name;
    if (n.name.rfind(oldName, 0) == 0) n.name = GetPlanarInfo(kind).name + n.name.substr(oldName.size());
    // A parallel view shows the scene, a perspective one a window at distance d.
    if (IsPlanarPerspective(kind) != IsPlanarPerspective(n.planar)) n.windowHalf = IsPlanarPerspective(kind) ? 1.0f : 2.5f;
    n.planar = kind;
}

std::vector<ExpandedNode> PlanarExpansion(const Node& n) {
    auto rot = [](NodeType t, const char* name, float deg) { return ExpandedNode{t, name, deg, -1}; };
    auto proj = [](const char* name, int kind) { return ExpandedNode{NodeType::PlanarProjection, name, 0.0f, kind}; };
    float t, p;
    n.PlanarAngles(t, p);
    switch (n.planar) {
        // On the slides these drop a coordinate in one matrix. The GPU form turns
        // the plane to face the screen first, then takes the front view.
        case kPlanarSide:
            if (n.exactSlides) return {};
            return {rot(NodeType::RotateY, "Rotate Y (-90): yz-plane to the front", -90.0f), proj("Front view", kPlanarFront)};
        case kPlanarTop:
            if (n.exactSlides) return {};
            return {rot(NodeType::RotateX, "Rotate X (90): zx-plane to the front", 90.0f), proj("Front view", kPlanarFront)};
        case kPlanarIsometric: case kPlanarDimetric: case kPlanarTrimetric: case kPlanarAxonometric:
            return {rot(NodeType::RotateY, "Rotate Y (-theta)", -t), rot(NodeType::RotateX, "Rotate X (phi)", p),
                    proj("Front view", kPlanarFront)};
        case kPlanarCavalier: case kPlanarCabinet: case kPlanarOblique: {
            // The slides' oblique matrix is a shear along the projectors, then the
            // front view: [1 0 L cos(phi); 0 1 L sin(phi); 0 0 0] = front * shear.
            float L = ObliqueLength(n), a = glm::radians(n.obliqueAngle);
            ExpandedNode shear{NodeType::Matrix, Format("Shear along the projectors (L = %.2f)", L)};
            shear.matrix[2][0] = L * std::cos(a);
            shear.matrix[2][1] = L * std::sin(a);
            return {shear, proj("Front view", kPlanarFront)};
        }
        case kPlanarTwoPoint:
            // The slides' rotate back lays the flat image back into the world. On
            // the GPU it would mix depth into x, so only the exact form keeps it.
            if (n.exactSlides)
                return {rot(NodeType::RotateY, "Rotate Y (-theta)", -n.theta), proj("One-point", kPlanarOnePoint),
                        rot(NodeType::RotateY, "Rotate Y (theta): rotate back", n.theta)};
            return {rot(NodeType::RotateY, "Rotate Y (-theta)", -n.theta), proj("One-point", kPlanarOnePoint)};
        case kPlanarThreePoint: {
            if (n.exactSlides)
                return {rot(NodeType::RotateX, "Rotate X (phi)", n.phi), rot(NodeType::RotateY, "Rotate Y (-theta)", -n.theta),
                        proj("One-point", kPlanarOnePoint), rot(NodeType::RotateY, "Rotate Y (theta): rotate back", n.theta),
                        rot(NodeType::RotateX, "Rotate X (-phi): rotate back", -n.phi)};
            // The GPU form turns onto the slides' normal without roll (see PlanarGpuMatrix).
            float tr = glm::radians(n.theta), pr = glm::radians(n.phi);
            glm::vec3 nv(std::sin(tr), std::cos(tr) * std::sin(pr), std::cos(tr) * std::cos(pr));
            float az = glm::degrees(std::atan2(nv.x, nv.z));
            float el = glm::degrees(std::asin(glm::clamp(nv.y, -1.0f, 1.0f)));
            return {rot(NodeType::RotateY, "Rotate Y (-azimuth of n)", -az), rot(NodeType::RotateX, "Rotate X (elevation of n)", el),
                    proj("One-point", kPlanarOnePoint)};
        }
        default: return {};
    }
}

bool NodeGraph::ExpandPlanar(int nodeId) {
    Node* src = FindNode(nodeId);
    if (!src || src->type != NodeType::PlanarProjection) return false;
    std::vector<ExpandedNode> plan = PlanarExpansion(*src);
    if (plan.empty()) return false;
    Node base = *src;  // copied: AddNode can reallocate `nodes`
    ImVec2 at = ImNodes::GetNodeGridSpacePos(nodeId);

    // Remember the links around the node, then put the chain in its place.
    int inFrom = -1;
    std::vector<int> outTo;
    for (auto& l : links) {
        if (l.endAttr == InAttr(nodeId)) inFrom = l.startAttr;
        if (l.startAttr == OutAttr(nodeId)) outTo.push_back(l.endAttr);
    }
    if (IsAnimated(nodeId)) StopAnimation();
    RemoveNode(nodeId);

    int prevOut = inFrom;
    for (size_t i = 0; i < plan.size(); ++i) {
        const ExpandedNode& e = plan[i];
        int id = AddNode(e.type, e.name);
        Node* n = FindNode(id);
        if (e.type == NodeType::PlanarProjection) {
            std::string name = n->name;
            *n = base;
            n->id = id;
            n->name = name;
            n->planar = e.planar;
        } else if (e.type == NodeType::Matrix) {
            n->custom = e.matrix;
        } else {
            n->angleDeg = e.angleDeg;
        }
        if (prevOut >= 0) AddLink(prevOut, InAttr(id));
        prevOut = OutAttr(id);
        ImNodes::SetNodeGridSpacePos(id, ImVec2(at.x + 260.0f * (float)i, at.y));
    }
    for (int end : outTo) AddLink(prevOut, end);
    return true;
}

// How far into step i the time t is: each step moves for 80% of its slot and
// then holds, eased in and out.
static float StepFraction(float t, int i) {
    float f = glm::clamp((t - (float)i) * 1.25f, 0.0f, 1.0f);
    return f * f * (3.0f - 2.0f * f);
}

// Walk backwards from `n` to its source, composing matrices in flow order.
// Memoized per evaluation via `state` (0 = unvisited, 1 = in progress, 2 = done).
void NodeGraph::Resolve(Node* n, std::vector<int>& state) {
    size_t idx = n - nodes.data();
    if (state[idx] == 2) return;
    if (state[idx] == 1) { n->kind = ChainKind::Invalid; return; } // cycle
    state[idx] = 1;

    n->RecomputeLocal();
    switch (n->type) {
        case NodeType::Object:     n->kind = ChainKind::Model;      n->cumulativeMatrix = glm::mat4(1.0f); break;
        case NodeType::Camera:     n->kind = ChainKind::CameraPose; n->cumulativeMatrix = n->localMatrix;  break;
        case NodeType::Projection: n->kind = ChainKind::Projection; n->cumulativeMatrix = n->localMatrix;  break;
        case NodeType::Output:     n->kind = ChainKind::None;       n->cumulativeMatrix = glm::mat4(1.0f); break;
        default: {
            Node* up = Upstream(InAttr(n->id));
            if (!up) {
                n->kind = ChainKind::None;
                n->cumulativeMatrix = n->localMatrix;
                break;
            }
            Resolve(up, state);
            if (up->kind == ChainKind::Invalid) {
                n->kind = ChainKind::Invalid;
                n->cumulativeMatrix = n->localMatrix;
            } else {
                // Any chain can be extended: model, camera pose, projection, or a
                // free-standing chain with no source. Flow order: apply this node
                // *after* everything upstream.
                n->kind = up->kind;
                n->cumulativeMatrix = n->localMatrix * up->cumulativeMatrix;
            }
            break;
        }
    }
    state[idx] = 2;
}

EvalResult NodeGraph::Evaluate() {
    EvalResult r;

    // ---- Animation: how far along each animated node is ----
    // Node i moves while animT goes from i to i + 1. Deleted nodes, and ones that
    // can no longer animate (a Planar Projection switched to several matrices), drop out.
    for (auto& n : nodes) n.animFraction = 1.0f;
    animNodes.erase(std::remove_if(animNodes.begin(), animNodes.end(),
                                   [&](int id) { Node* n = FindNode(id); return !n || !CanAnimate(*n); }),
                    animNodes.end());
    if (animNodes.empty()) animPlaying = false;
    animT = glm::clamp(animT, 0.0f, (float)animNodes.size());
    for (size_t i = 0; i < animNodes.size(); ++i) FindNode(animNodes[i])->animFraction = StepFraction(animT, (int)i);

    std::vector<int> state(nodes.size(), 0);
    for (auto& n : nodes) Resolve(&n, state);

    if (!animNodes.empty()) {
        std::vector<int> projChain = ProjectionChain();
        int current = glm::min((int)animT, (int)animNodes.size() - 1);
        r.animLines.push_back("Animating, in pipeline order (M, then V, then P):");
        for (size_t i = 0; i < animNodes.size(); ++i) {
            Node* n = FindNode(animNodes[i]);
            bool inP = n->type == NodeType::Projection || n->kind == ChainKind::Projection ||
                       std::find(projChain.begin(), projChain.end(), n->id) != projChain.end();
            const char* tag = inP ? "P" : n->kind == ChainKind::Model ? "M" : n->kind == ChainKind::CameraPose ? "C" : "-";
            const char* mark = (int)i == current && n->animFraction < 1.0f ? "> " : n->animFraction >= 1.0f ? "  done  " : "  ";
            r.animLines.push_back(std::string(mark) + n->name + "  (" + tag + ")");
        }
        Node* cur = FindNode(animNodes[current]);
        char buf[200];
        std::snprintf(buf, sizeof(buf), "Animating %d of %d: %s  (%.0f%%)", current + 1, (int)animNodes.size(),
                      cur->name.c_str(), 100.0f * cur->animFraction);
        r.animStatus = buf;
    }

    Node* out = OutputNode();
    r.hasOutput = out != nullptr;
    if (!out) r.warnings.push_back("No MVP Output node: add one with right-click.");

    // ---- Model ----
    if (out) for (Node* m : AllUpstream(InAttr(out->id))) {
        if (m->kind == ChainKind::Invalid) {
            r.warnings.push_back("Model pin: '" + m->name + "' is part of a cycle.");
            continue;
        }
        // An Object chain uses its object's shape. Any other chain (a lone Matrix,
        // a camera or projection chain...) is used as M for a cube.
        int src = SourceOf(m);
        Node* srcNode = FindNode(src);
        bool isObject = srcNode && srcNode->type == NodeType::Object;
        ObjectInstance inst;
        inst.sourceNodeId = isObject ? src : m->id;
        inst.model = m->cumulativeMatrix;
        inst.shape = isObject ? srcNode->shape : kShapeCube;
        if (isObject) {
            for (int i = 0; i < 3; ++i) inst.tri[i] = srcNode->tri[i];
            inst.modelPath = srcNode->modelPath;
            inst.fitModel = srcNode->fitModel;
        }
        r.objects.push_back(inst);
    }
    if (out && r.objects.empty()) r.warnings.push_back("Nothing is connected to Model.");

    // ---- View ----
    if (Node* c = out ? Upstream(ViewInAttr(out->id)) : nullptr) {
        // Any chain works as the camera pose C, e.g. a lone Matrix. A singular
        // matrix can't be inverted into V, so V falls back to the identity.
        if (c->kind == ChainKind::Invalid) {
            r.warnings.push_back("View pin: '" + c->name + "' is part of a cycle.");
        } else if (std::fabs(glm::determinant(c->cumulativeMatrix)) < 1e-8f) {
            r.warnings.push_back("View pin: the camera pose has determinant 0, so it has no inverse V (V = identity).");
        } else {
            r.hasCamera = true;
            r.cameraPose = c->cumulativeMatrix;
        }
    } else if (out) {
        r.warnings.push_back("Nothing is connected to View: no camera, V = identity.");
    }
    // No camera: V is the identity, so camera space is world space and the view
    // volume sits at the world origin as the canonical volume of P.
    if (!r.hasCamera) r.cameraPose = glm::mat4(1.0f);
    r.view = glm::inverse(r.cameraPose);

    // ---- Projection ----
    Node* p = out ? Upstream(ProjInAttr(out->id)) : nullptr;
    if (p && p->kind == ChainKind::Invalid) {
        r.warnings.push_back("Projection pin: '" + p->name + "' is part of a cycle.");
    } else if (p) {
        // Any chain works as P: a Projection node, Projection -> Matrix, a lone Matrix...
        r.hasProjection = true;
        r.projection = p->cumulativeMatrix;
        r.customProjection = p->type != NodeType::Projection;
        // The slides put the COP at the origin: with no camera that is the world
        // origin, usually inside the objects.
        if (!r.hasCamera)
            for (Node* n = p; n && IsTransform(n->type); n = Upstream(InAttr(n->id)))
                if (n->type == NodeType::PlanarProjection && IsPlanarPerspective(n->planar)) {
                    r.warnings.push_back("Perspective: the COP is the camera. With no camera it is the world origin, "
                                         "usually inside the objects. Add a Camera in front of the scene.");
                    break;
                }
        // Named only when it is all there, not part-way through an animation.
        if (p->type == NodeType::PlanarProjection && !Upstream(InAttr(p->id)) && p->animFraction >= 1.0f)
            r.planarKind = p->planar;
        if (Node* src = FindNode(SourceOf(p)); src && src->type == NodeType::Projection) {
            r.fovDeg = src->fovDeg; r.aspect = src->aspect;
        }
    }

    if (r.hasProjection) {
        // Read near and far back out of P: un-project the centres of the NDC
        // depth faces z = -1 and z = +1 with P^-1, and measure their distance in
        // front of the camera. For glm::perspective this gives exactly near/far.
        const glm::mat4& P = r.projection;
        r.perspective = std::fabs(P[0][3]) + std::fabs(P[1][3]) + std::fabs(P[2][3]) > 1e-6f;
        if (std::fabs(glm::determinant(P)) < 1e-12f) {
            r.warnings.push_back("Projection: P has determinant 0. It can still render, but its volume can't be drawn.");
        } else {
            glm::mat4 inv = glm::inverse(P);
            auto depthOf = [&](float zNdc) {
                glm::vec4 e = inv * glm::vec4(0, 0, zNdc, 1);
                return std::fabs(e.w) < 1e-9f ? 1e6f : -e.z / e.w;
            };
            r.nearPlane = depthOf(-1.0f);
            r.farPlane = depthOf(1.0f);
            if (std::fabs(r.farPlane - r.nearPlane) < 1e-4f) r.farPlane = r.nearPlane + 1.0f;
        }
    } else {
        // No projection at all: P = identity, so clip = V * M * v and w stays 1.
        // The GPU keeps x, y, z in [-1, 1] of camera space: a 2x2x2 box around
        // the camera. Its "near" face (depth 0) is z = -1, one unit in front of
        // the camera, and its "far" face (depth 1) is z = +1, behind it.
        // Stored as distances in front of the camera, so near = 1 and far = -1.
        r.nearPlane = 1.0f;
        r.farPlane = -1.0f;
        r.projection = glm::mat4(1.0f);
    }

    // ---- The Projection chain acting on the world, for view 1 ----
    // Only while some of its nodes are animated. Each step goes as far as its
    // node: done (1), moving, or waiting (0); nodes not animated are done.
    std::vector<ProjStep> steps;
    if (!animNodes.empty() && p && p->kind != ChainKind::Invalid) steps = ProjectionSteps(p->id);
    bool anyAnimated = false;
    for (const ProjStep& s : steps) anyAnimated = anyAnimated || IsAnimated(s.nodeId);
    if (anyAnimated) {
        // Later steps multiply on the left. The view plane appears with the
        // projecting step and then moves with the steps after it.
        glm::mat4 done(1.0f), after(1.0f), plane(0.0f);
        bool projected = false;
        r.stepsLines.push_back("The projection chain acting on the world:");
        for (size_t i = 0; i < steps.size(); ++i) {
            Node* n = FindNode(steps[i].nodeId);
            float f = n ? n->animFraction : 1.0f;
            // Stop a flattening step just short of flat, so the depth buffer can
            // still tell the front faces from the back ones.
            glm::mat4 M = steps[i].At(steps[i].projects ? glm::min(f, 0.995f) : f);
            done = M * done;
            if (projected) after = M * after;
            if (steps[i].projects) { plane = steps[i].plane; projected = true; }
            const char* mark = f >= 1.0f ? "  done  " : f > 0.0f ? "> " : "  ";
            r.stepsLines.push_back(std::string(mark) + std::to_string(i + 1) + ". " + steps[i].label);
        }
        r.stepsActive = true;
        r.stepsMatrix = done;
        r.stepsPlane = after * plane;
    }
    return r;
}

// ---------------------------------------------------------------------------
// Presets
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

    place(objA, 10, 10);   place(rotA, 240, 10);  place(trA, 490, 10);
    place(objB, 10, 225);  place(trB, 240, 225);
    place(cam, 770, 10);   place(camRot, 1030, 10);
    place(proj, 770, 225);
    place(out, 1290, 110);
}

int AddModelObject(NodeGraph& graph, const std::string& path, float gridX, float gridY) {
    size_t slash = path.find_last_of("/\\");
    int id = graph.AddNode(NodeType::Object, slash == std::string::npos ? path : path.substr(slash + 1));
    Node* n = graph.FindNode(id);
    n->shape = kShapeModel;
    n->modelPath = path;
    if (Node* out = graph.OutputNode()) graph.AddLink(NodeGraph::OutAttr(id), NodeGraph::InAttr(out->id));
    ImNodes::SetNodeGridSpacePos(id, ImVec2(gridX, gridY));
    return id;
}

static void ClearGraph(NodeGraph& graph) {
    // Keep the id counters going so imnodes never sees a reused id.
    int nid = graph.nextNodeId, lid = graph.nextLinkId;
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    graph = NodeGraph();
    graph.nextNodeId = nid; graph.nextLinkId = lid;
}

const char* kProjectionExamples[kProjectionExampleCount] = {
    "Parallel: multi-view, axonometric, oblique",
    "Isometric step by step (Rotate Y, Rotate X, front view)",
    "Perspective: one-, two-, three-point",
    "Two-point step by step (Rotate Y, one-point)",
};

// Object -> [Scale] -> Translate -> Output.Model, on one row of the canvas.
static void AddBox(NodeGraph& g, int out, const char* name, glm::vec3 scale, glm::vec3 at, float row) {
    auto place = [](int id, float x, float y) { ImNodes::SetNodeEditorSpacePos(id, ImVec2(x, y)); };
    int obj = g.AddNode(NodeType::Object, name);
    int last = obj;
    place(obj, 10, row);
    if (scale != glm::vec3(1.0f)) {
        int sc = g.AddNode(NodeType::Scale, "Scale");
        g.FindNode(sc)->vec = scale;
        g.AddLink(NodeGraph::OutAttr(last), NodeGraph::InAttr(sc));
        place(sc, 200, row);
        last = sc;
    }
    int tr = g.AddNode(NodeType::Translate, "Translate");
    g.FindNode(tr)->vec = at;
    g.AddLink(NodeGraph::OutAttr(last), NodeGraph::InAttr(tr));
    g.AddLink(NodeGraph::OutAttr(tr), NodeGraph::InAttr(out));
    place(tr, 450, row);
}

void LoadProjectionExample(NodeGraph& g, int index) {
    ClearGraph(g);
    auto place = [](int id, float x, float y) { ImNodes::SetNodeEditorSpacePos(id, ImVec2(x, y)); };
    int out = g.AddNode(NodeType::Output, "MVP Output");
    place(out, 1290, 110);
    bool perspective = index >= 2;
    bool steps = index == 1 || index == 3;

    if (!perspective) {
        // Around the origin and no camera, so V = identity and the slides'
        // world axes are the camera's axes.
        AddBox(g, out, "Block", {2.0f, 1.0f, 1.0f}, {0.0f, 0.5f, 0.0f}, 10);
        AddBox(g, out, "Tower", glm::vec3(1.0f), {0.5f, 1.5f, 0.0f}, 230);
        AddBox(g, out, "Small cube", glm::vec3(0.5f), {-0.6f, 0.25f, 0.9f}, 450);
    } else {
        // A camera that only moves (no turn), so the slides' COP at the origin is
        // the camera and their axes are still the world's. The slides turn the
        // view plane about the COP, which turns the view: the tower stands 30
        // degrees to the left, where two- and three-point (theta = 30) look, so
        // they see its corner. One-point looks straight ahead along the wall.
        AddBox(g, out, "Tower", {1.6f, 6.0f, 1.6f}, {-3.5f, 3.0f, -2.1f}, 10);
        AddBox(g, out, "Wall", {0.5f, 2.0f, 10.0f}, {2.5f, 1.0f, -6.0f}, 230);
        AddBox(g, out, "Shed", glm::vec3(1.5f), {-1.2f, 0.75f, -0.5f}, 450);
        int cam = g.AddNode(NodeType::Camera, "Camera (COP)");
        g.FindNode(cam)->camPos = {0.0f, 1.6f, 4.0f};
        g.FindNode(cam)->camTarget = {0.0f, 1.6f, -1.0f};
        g.AddLink(NodeGraph::OutAttr(cam), NodeGraph::ViewInAttr(out));
        place(cam, 770, 10);
    }

    int kind = perspective ? kPlanarOnePoint : steps ? kPlanarFront : kPlanarIsometric;
    int proj = g.AddNode(NodeType::PlanarProjection, GetPlanarInfo(kind).name);
    Node* p = g.FindNode(proj);
    p->planar = kind;
    if (perspective) {
        p->windowHalf = 1.0f;
        p->theta = 30.0f;  // ready for two- and three-point: towards the tower
        p->phi = -25.0f;   // three-point looks up the tower
    }
    g.AddLink(NodeGraph::OutAttr(proj), NodeGraph::ProjInAttr(out));
    place(proj, 1030, steps ? 560 : 330);

    if (steps) {
        // The slides' derivation as a chain of nodes, in data-flow order:
        // Rotate Y (-theta) -> [Rotate X (phi)] -> projection.
        int prev = -1;
        auto addRot = [&](NodeType t, const char* name, float deg, float x) {
            int id = g.AddNode(t, name);
            g.FindNode(id)->angleDeg = deg;
            if (prev >= 0) g.AddLink(NodeGraph::OutAttr(prev), NodeGraph::InAttr(id));
            place(id, x, 560);
            prev = id;
        };
        if (perspective) {
            addRot(NodeType::RotateY, "Rotate Y (-theta)", -30.0f, 770);
        } else {
            addRot(NodeType::RotateY, "Rotate Y (-theta)", -45.0f, 510);
            addRot(NodeType::RotateX, "Rotate X (phi)", glm::degrees(std::atan(1.0f / std::sqrt(2.0f))), 770);
        }
        g.AddLink(NodeGraph::OutAttr(prev), NodeGraph::InAttr(proj));
    }
}

void ResetGraph(NodeGraph& graph, bool starter) {
    ClearGraph(graph);
    if (starter) {
        BuildStarterGraph(graph);
    } else {
        int out = graph.AddNode(NodeType::Output, "MVP Output");
        ImNodes::SetNodeGridSpacePos(out, ImVec2(600, 60));
    }
}
