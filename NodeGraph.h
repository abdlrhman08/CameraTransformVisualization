#pragma once
// Node graph data model for the MVP teaching tool.
//
// The graph has three kinds of chains that all end in a single "MVP Output" node:
//
//   Object ─► [Translate / Rotate / Scale]* ─► MVP.Model       (one or more objects)
//   Camera ─► [Translate / Rotate / Scale]* ─► MVP.View        (the camera's pose C)
//   Projection ───────────────────────────────► MVP.Projection
//
// Chains are read in data-flow order: the thing on the left is transformed by
// each node it flows through. So `Object -> Rotate -> Translate` first rotates
// the object, then moves it, giving M = T * R.
//
// The camera chain builds the camera's *pose* C (where the camera sits in the
// world). The view matrix that actually goes to the GPU is its inverse:
// V = C^-1. That inverse is the whole point of the tool.

#include <string>
#include <vector>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

enum class NodeType {
    Object,      // source of a model chain
    Camera,      // source of a camera-pose chain (look-at)
    Projection,  // source of the projection matrix
    Translate,
    RotateX,
    RotateY,
    RotateZ,
    Scale,
    Output       // sink: Model (many) + View + Projection
};

// What kind of chain a node currently belongs to (decided by its source).
enum class ChainKind { None, Model, CameraPose, Projection, Invalid };

inline bool IsTransform(NodeType t) {
    return t == NodeType::Translate || t == NodeType::RotateX || t == NodeType::RotateY ||
           t == NodeType::RotateZ || t == NodeType::Scale;
}

struct Node {
    int id = -1;
    NodeType type = NodeType::Object;
    std::string name;

    // Transform params
    glm::vec3 vec{0.0f};      // Translate: offset, Scale: factors
    float angleDeg = 0.0f;    // RotateX/Y/Z
    bool spin = false;        // RotateX/Y/Z: animate the angle
    float spinSpeed = 30.0f;  // degrees per second

    // Camera params (initial pose, before any transforms in its chain)
    glm::vec3 camPos{0.0f, 2.0f, 7.0f};
    glm::vec3 camTarget{0.0f, 0.5f, 0.0f};

    // Projection params
    float fovDeg = 60.0f;
    float aspect = 16.0f / 9.0f;
    float nearPlane = 0.5f;
    float farPlane = 25.0f;

    // Filled in by NodeGraph::Evaluate for the UI
    glm::mat4 localMatrix{1.0f};
    glm::mat4 cumulativeMatrix{1.0f};
    ChainKind kind = ChainKind::None;

    void RecomputeLocal() {
        switch (type) {
            case NodeType::Translate: localMatrix = glm::translate(glm::mat4(1.0f), vec); break;
            case NodeType::RotateX:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(1, 0, 0)); break;
            case NodeType::RotateY:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(0, 1, 0)); break;
            case NodeType::RotateZ:   localMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), glm::vec3(0, 0, 1)); break;
            case NodeType::Scale:     localMatrix = glm::scale(glm::mat4(1.0f), vec); break;
            case NodeType::Camera:    localMatrix = CameraBasePose(); break;
            case NodeType::Projection:localMatrix = ProjectionMatrix(); break;
            default:                  localMatrix = glm::mat4(1.0f); break;
        }
    }

    // The camera's pose in the world: inverse of lookAt. Guards against the
    // degenerate cases (target == position, looking straight up/down).
    glm::mat4 CameraBasePose() const {
        glm::vec3 target = camTarget;
        if (glm::length(target - camPos) < 1e-4f) target = camPos + glm::vec3(0, 0, -1);
        glm::vec3 dir = glm::normalize(target - camPos);
        glm::vec3 up(0, 1, 0);
        if (std::fabs(glm::dot(dir, up)) > 0.999f) up = glm::vec3(0, 0, -1);
        return glm::inverse(glm::lookAt(camPos, target, up));
    }

    glm::mat4 ProjectionMatrix() const {
        float n = glm::max(nearPlane, 0.001f);
        float f = glm::max(farPlane, n + 0.01f);
        return glm::perspective(glm::radians(glm::clamp(fovDeg, 1.0f, 170.0f)), glm::max(aspect, 0.01f), n, f);
    }
};

struct Link {
    int id = -1;
    int startAttr = -1; // output attribute id
    int endAttr = -1;   // input attribute id
};

struct ObjectInstance {
    int sourceNodeId = -1;  // the Object node this chain started from
    glm::mat4 model{1.0f};
};

// Everything the renderer needs, produced by NodeGraph::Evaluate.
struct EvalResult {
    std::vector<ObjectInstance> objects;
    bool hasOutput = false;
    bool hasCamera = false;
    bool hasProjection = false;
    glm::mat4 cameraPose{1.0f};  // C: camera -> world
    glm::mat4 view{1.0f};        // V = C^-1: world -> camera
    glm::mat4 projection{1.0f};  // P
    float fovDeg = 60.0f, aspect = 16.0f / 9.0f, nearPlane = 0.5f, farPlane = 25.0f;
    std::vector<std::string> warnings;
};

struct NodeGraph {
    std::vector<Node> nodes;
    std::vector<Link> links;
    int nextNodeId = 1;
    int nextLinkId = 1;

    // Attribute ids are derived from the node id so imnodes pins stay unique.
    static int OutAttr(int nodeId)      { return nodeId * 8 + 1; }
    static int InAttr(int nodeId)       { return nodeId * 8 + 2; } // transforms' input, Output's Model input
    static int ViewInAttr(int nodeId)   { return nodeId * 8 + 3; } // Output's View input
    static int ProjInAttr(int nodeId)   { return nodeId * 8 + 4; } // Output's Projection input
    static int NodeOfAttr(int attr)     { return attr / 8; }
    static bool IsOutputAttr(int attr)  { return attr % 8 == 1; }

    int AddNode(NodeType type, const std::string& name) {
        Node n;
        n.id = nextNodeId++;
        n.type = type;
        n.name = name;
        if (type == NodeType::Scale) n.vec = glm::vec3(1.0f);
        n.RecomputeLocal();
        nodes.push_back(n);
        return n.id;
    }

    void RemoveNode(int id) {
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

    // Returns false if the link is not allowed.
    bool AddLink(int a, int b) {
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

    void RemoveLink(int linkId) {
        for (auto it = links.begin(); it != links.end(); ++it) {
            if (it->id == linkId) { links.erase(it); return; }
        }
    }

    Node* FindNode(int id) {
        for (auto& n : nodes) if (n.id == id) return &n;
        return nullptr;
    }

    // The node feeding a given input pin (inputs have at most one link,
    // except the Output's Model pin, which is handled separately).
    Node* Upstream(int inAttr) {
        for (auto& l : links) if (l.endAttr == inAttr) return FindNode(NodeOfAttr(l.startAttr));
        return nullptr;
    }

    std::vector<Node*> AllUpstream(int inAttr) {
        std::vector<Node*> out;
        for (auto& l : links)
            if (l.endAttr == inAttr)
                if (Node* n = FindNode(NodeOfAttr(l.startAttr))) out.push_back(n);
        return out;
    }

    Node* OutputNode() {
        for (auto& n : nodes) if (n.type == NodeType::Output) return &n;
        return nullptr;
    }

    // Walk backwards from `n` to its source, composing matrices in flow order.
    // Memoized per evaluation via `state` (0 = unvisited, 1 = in progress, 2 = done).
    void Resolve(Node* n, std::vector<int>& state) {
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
                if (up->kind == ChainKind::Model || up->kind == ChainKind::CameraPose) {
                    n->kind = up->kind;
                    // Flow order: apply this node *after* everything upstream.
                    n->cumulativeMatrix = n->localMatrix * up->cumulativeMatrix;
                } else if (up->kind == ChainKind::None) {
                    n->kind = ChainKind::None;
                    n->cumulativeMatrix = n->localMatrix * up->cumulativeMatrix;
                } else {
                    n->kind = ChainKind::Invalid; // e.g. Projection -> Rotate
                    n->cumulativeMatrix = n->localMatrix;
                }
                break;
            }
        }
        state[idx] = 2;
    }

    EvalResult Evaluate() {
        EvalResult r;
        std::vector<int> state(nodes.size(), 0);
        for (auto& n : nodes) Resolve(&n, state);

        Node* out = OutputNode();
        r.hasOutput = out != nullptr;
        if (!out) r.warnings.push_back("No MVP Output node: add one with right-click.");

        if (out) for (Node* m : AllUpstream(InAttr(out->id))) {
            if (m->kind == ChainKind::Model) r.objects.push_back({SourceOf(m), m->cumulativeMatrix});
            else r.warnings.push_back("Model pin: '" + m->name + "' is not part of an Object chain.");
        }
        if (out && r.objects.empty()) r.warnings.push_back("Nothing is connected to Model.");

        if (Node* c = out ? Upstream(ViewInAttr(out->id)) : nullptr) {
            if (c->kind == ChainKind::CameraPose) {
                r.hasCamera = true;
                r.cameraPose = c->cumulativeMatrix;
            } else {
                r.warnings.push_back("View pin: '" + c->name + "' is not part of a Camera chain.");
            }
        } else if (out) {
            r.warnings.push_back("Nothing is connected to View (using a default camera).");
        }
        if (!r.hasCamera) {
            Node def; def.type = NodeType::Camera;
            r.cameraPose = def.CameraBasePose();
        }
        r.view = glm::inverse(r.cameraPose);

        Node* p = out ? Upstream(ProjInAttr(out->id)) : nullptr;
        if (p && p->kind == ChainKind::Projection) {
            r.hasProjection = true;
            r.fovDeg = p->fovDeg; r.aspect = p->aspect;
            r.nearPlane = glm::max(p->nearPlane, 0.001f);
            r.farPlane = glm::max(p->farPlane, r.nearPlane + 0.01f);
        } else if (out) {
            r.warnings.push_back(p ? "Projection pin must come straight from a Projection node."
                                   : "Nothing is connected to Projection (using a default one).");
        }
        Node tmp; tmp.fovDeg = r.fovDeg; tmp.aspect = r.aspect; tmp.nearPlane = r.nearPlane; tmp.farPlane = r.farPlane;
        r.projection = tmp.ProjectionMatrix();
        return r;
    }

private:
    int SourceOf(Node* n) {
        int guard = 0;
        while (n && IsTransform(n->type) && guard++ < 256) n = Upstream(InAttr(n->id));
        return n ? n->id : -1;
    }
};
