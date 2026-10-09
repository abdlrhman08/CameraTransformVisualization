#include "NodeGraph.h"

#include <cmath>
#include <utility>
#include <imgui.h>
#include <imnodes.h>
#include <glm/gtc/matrix_transform.hpp>

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------
void Node::RecomputeLocal() {
    switch (type) {
        case NodeType::Translate: localMatrix = glm::translate(glm::mat4(1.0f), vec); break;
        case NodeType::RotateX:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(1, 0, 0)); break;
        case NodeType::RotateY:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(0, 1, 0)); break;
        case NodeType::RotateZ:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(0, 0, 1)); break;
        case NodeType::Scale:     localMatrix = glm::scale(glm::mat4(1.0f), vec); break;
        case NodeType::Matrix:    localMatrix = custom; break;
        case NodeType::Camera:    localMatrix = CameraBasePose(); break;
        case NodeType::Projection:localMatrix = ProjectionMatrix(); break;
        default:                  localMatrix = glm::mat4(1.0f); break;
    }
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
    std::vector<int> state(nodes.size(), 0);
    for (auto& n : nodes) Resolve(&n, state);

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

void ResetGraph(NodeGraph& graph, bool starter) {
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
