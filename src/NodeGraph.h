#pragma once
// Node graph data model for the MVP teaching tool.
//
// The graph has three kinds of chains that all end in a single "MVP Output" node:
//
//   Object ─► [Translate / Rotate / Scale / Matrix]* ─► MVP.Model   (one or more objects)
//   Camera ─► [Translate / Rotate / Scale / Matrix]* ─► MVP.View    (the camera's pose C)
//   Projection ───────────────────────────────► MVP.Projection
//
// Chains are read in data-flow order: the thing on the left is transformed by
// each node it flows through. So `Object -> Rotate -> Translate` first rotates
// the object, then moves it, giving M = T * R.
//
// The camera chain builds the camera's *pose* C (where the camera sits in the
// world). The view matrix that actually goes to the GPU is its inverse:
// V = C^-1. That inverse is the whole point of the tool.
//
// To add a node type: add it to NodeType, give it a matrix in
// Node::RecomputeLocal (NodeGraph.cpp), and add its UI in DrawNodeEditor (UI.cpp).

#include <string>
#include <vector>
#include <glm/glm.hpp>

enum class NodeType {
    Object,      // source of a model chain
    Camera,      // source of a camera-pose chain (look-at)
    Projection,  // source of the projection matrix
    Translate,
    RotateX,
    RotateY,
    RotateZ,
    Scale,
    Matrix,      // any 4x4 matrix, typed in by the user
    Output       // sink: Model (many) + View + Projection
};

// What kind of chain a node currently belongs to (decided by its source).
enum class ChainKind { None, Model, CameraPose, Projection, Invalid };

enum ObjectShape { kShapeCube = 0, kShapeTriangle = 1, kShapeModel = 2 };

inline bool IsTransform(NodeType t) {
    return t == NodeType::Translate || t == NodeType::RotateX || t == NodeType::RotateY ||
           t == NodeType::RotateZ || t == NodeType::Scale || t == NodeType::Matrix;
}

inline bool IsRotation(NodeType t) {
    return t == NodeType::RotateX || t == NodeType::RotateY || t == NodeType::RotateZ;
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

    // Matrix param (column-major, like GLM and GLSL: custom[col][row])
    glm::mat4 custom{1.0f};

    // Object params
    int shape = kShapeCube;
    // Triangle corners in the object's local space (used when shape == kShapeTriangle)
    glm::vec3 tri[3] = {{-0.6f, -0.5f, 0.0f}, {0.6f, -0.5f, 0.0f}, {0.0f, 0.6f, 0.0f}};
    // Model file (used when shape == kShapeModel). With fitModel it is centered
    // and scaled into a 1x1x1 box, like the cube; otherwise it keeps the file's units.
    std::string modelPath;
    bool fitModel = true;

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

    void RecomputeLocal();
    glm::mat4 CameraBasePose() const;    // camera -> world, from position + look-at
    glm::mat4 ProjectionMatrix() const;
};

struct Link {
    int id = -1;
    int startAttr = -1; // output attribute id
    int endAttr = -1;   // input attribute id
};

struct ObjectInstance {
    int sourceNodeId = -1;  // the Object node this chain started from
    glm::mat4 model{1.0f};
    int shape = kShapeCube;
    glm::vec3 tri[3] = {};   // triangle corners, local space
    std::string modelPath;   // model file, for kShapeModel
    bool fitModel = true;
};

// Everything the renderer needs, produced by NodeGraph::Evaluate.
struct EvalResult {
    std::vector<ObjectInstance> objects;
    bool hasOutput = false;
    bool hasCamera = false;
    bool hasProjection = false;  // false: nothing connected, P is the identity
    bool customProjection = false;  // P comes from anything other than a lone Projection node
    bool perspective = false;    // P puts depth into w, so the divide changes x and y
    glm::mat4 cameraPose{1.0f};  // C: camera -> world
    glm::mat4 view{1.0f};        // V = C^-1: world -> camera
    glm::mat4 projection{1.0f};  // P
    // fov/aspect come from a Projection node. near/far are read back from P itself
    // (distances in front of the camera of the -1 and +1 depth faces).
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

    int AddNode(NodeType type, const std::string& name);
    void RemoveNode(int id);
    bool AddLink(int a, int b);  // returns false if the link is not allowed
    void RemoveLink(int linkId);

    Node* FindNode(int id);
    Node* OutputNode();
    Node* Upstream(int inAttr);                  // the node feeding an input pin
    std::vector<Node*> AllUpstream(int inAttr);  // for the Output's multi-link Model pin

    // Makes the View chain produce the camera pose `C` by editing the chain's
    // first node: a Camera node's position/look-at, or a Matrix node's matrix.
    // Transforms after it are kept. If nothing is connected to View, a Camera
    // node is created and linked. Returns nullptr on success, else the reason.
    const char* SetCameraPose(const glm::mat4& C);

    // Advances every spinning Rotate node by dt seconds.
    void Animate(float dt);

    // Resolves every chain and collects what the renderer needs.
    EvalResult Evaluate();

private:
    void Resolve(Node* n, std::vector<int>& state);
    int SourceOf(Node* n);
};

// Adds an Object node showing the model file at `path`, linked to the Output's
// Model pin (when there is an Output), at `gridPos` on the imnodes canvas.
int AddModelObject(NodeGraph& graph, const std::string& path, float gridX, float gridY);

// Replaces the whole graph: the starter example, or just an MVP Output node.
// Also places the nodes on the imnodes canvas, so an imnodes context must exist.
void ResetGraph(NodeGraph& graph, bool starter);
