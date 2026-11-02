#pragma once
// Node graph data model for the MVP teaching tool.
//
// The graph has three kinds of chains that all end in a single "MVP Output" node:
//
//   Object ─► [Translate / Rotate / Scale / Matrix]* ─► MVP.Model   (one or more objects)
//   Camera ─► [Translate / Rotate / Scale / Matrix]* ─► MVP.View    (the camera's pose C)
//   Projection ───────────────────────────────► MVP.Projection
//   [Rotate...] ─► Planar Projection ──────────► MVP.Projection  (the slides' projections)
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
    PlanarProjection,  // a projection from the slides: multi-view, axonometric, oblique, perspective
    Output       // sink: Model (many) + View + Projection
};

// The planar geometric projections of the course slides (projections.pdf).
enum PlanarKind {
    kPlanarFront, kPlanarSide, kPlanarTop,                                   // multi-view
    kPlanarIsometric, kPlanarDimetric, kPlanarTrimetric, kPlanarAxonometric, // axonometric
    kPlanarCavalier, kPlanarCabinet, kPlanarOblique,                         // oblique
    kPlanarOnePoint, kPlanarOnePointShifted, kPlanarTwoPoint, kPlanarThreePoint, // perspective
    kPlanarCount
};

struct PlanarInfo {
    const char* name;   // "Isometric"
    const char* group;  // "Axonometric"
    const char* note;   // one line on what it does
};
const PlanarInfo& GetPlanarInfo(int kind);
inline bool IsPlanarPerspective(int kind) { return kind >= kPlanarOnePoint; }
inline bool IsPlanarOblique(int kind) { return kind >= kPlanarCavalier && kind <= kPlanarOblique; }

// One step of building a projection, the way the slides derive it, acting on
// camera space. A rotation turns by a growing angle; any other step blends from
// the identity to `target`, so a projection slides points along their projectors.
struct ProjStep {
    std::string label;
    int nodeId = -1;          // the node this step belongs to
    int axis = -1;            // 0, 1, 2: rotate about x, y, z by `deg`
    float deg = 0.0f;
    glm::mat4 target{1.0f};   // used when axis < 0
    bool projects = false;    // the step that flattens onto the view plane
    glm::mat4 plane{1.0f};    // projects: places the square [-1, 1]^2 (z = 0) on the view plane
    glm::mat4 At(float f) const;  // the step done to fraction f in [0, 1]
};

// What kind of chain a node currently belongs to (decided by its source).
enum class ChainKind { None, Model, CameraPose, Projection, Invalid };

enum ObjectShape { kShapeCube = 0, kShapeTriangle = 1, kShapeModel = 2 };

inline bool IsTransform(NodeType t) {
    return t == NodeType::Translate || t == NodeType::RotateX || t == NodeType::RotateY ||
           t == NodeType::RotateZ || t == NodeType::Scale || t == NodeType::Matrix ||
           t == NodeType::PlanarProjection;
}

inline bool IsRotation(NodeType t) {
    return t == NodeType::RotateX || t == NodeType::RotateY || t == NodeType::RotateZ;
}

struct Node;
// Transforms and Projection nodes can be animated. A Planar Projection made of
// several matrices has to be expanded first.
bool CanAnimate(const Node& n);

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

    // Planar projection params (angles in degrees, named as on the slides)
    int planar = kPlanarIsometric;
    float theta = 45.0f;            // azimuth: rotate by -theta about y
    float phi = 35.2644f;           // elevation: rotate by phi about x
    float dimetricRatio = 0.5f;     // m / n for the dimetric normal [n, m, n]
    glm::vec3 viewNormal{2.0f, 1.5f, 3.0f};  // trimetric: the view plane normal
    float obliqueAlpha = 53.13f;    // angle between the projector and the view plane
    float obliqueAngle = 45.0f;     // phi of the oblique slides: direction of the receding lines
    float viewDist = 1.5f;          // perspective: d, from the center of projection to the view plane
    float windowHalf = 2.5f;        // half the height of the view plane region shown on screen
    float depthRange = 10.0f;       // parallel: depth kept within +-depthRange of the view plane
    bool exactSlides = false;       // send the slide's matrix as is (z discarded) instead of the GPU form

    // Set by NodeGraph::Evaluate while the node is animated: 0 = identity, 1 = its
    // full matrix. Rotations turn by this much of their angle, others blend.
    float animFraction = 1.0f;

    // Filled in by NodeGraph::Evaluate for the UI
    glm::mat4 localMatrix{1.0f};
    glm::mat4 cumulativeMatrix{1.0f};
    ChainKind kind = ChainKind::None;

    void RecomputeLocal();
    glm::mat4 CameraBasePose() const;    // camera -> world, from position + look-at
    glm::mat4 ProjectionMatrix() const;
    // PlanarProjection: the matrix exactly as on the slides, and the form sent to
    // the GPU (same image, laid onto the screen's [-1, 1] with depth kept).
    glm::mat4 PlanarSlideMatrix() const;
    glm::mat4 PlanarGpuMatrix() const;
    // The azimuth and elevation the axonometric kinds rotate by.
    void PlanarAngles(float& thetaDeg, float& phiDeg) const;
    // The slides' steps for this kind (rotate, project, rotate back...).
    std::vector<ProjStep> PlanarSteps() const;
};

// One node of the chain a Planar Projection expands into.
struct ExpandedNode {
    NodeType type;      // RotateX, RotateY, Matrix, or PlanarProjection
    std::string name;
    float angleDeg = 0.0f;  // rotations
    int planar = -1;        // PlanarProjection: its kind (other settings are copied)
    glm::mat4 matrix{1.0f}; // Matrix
};

// Switches a Planar Projection to another kind. A name that is the old kind's
// ("Isometric", "Isometric 2") follows it, and switching between parallel and
// perspective resets the window to suit.
void SetPlanarKind(Node& n, int kind);
// The chain of existing nodes whose product is the matrix `n` sends, one node
// per matrix of the slides. Empty when the kind is a single matrix already.
std::vector<ExpandedNode> PlanarExpansion(const Node& n);

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
    bool hasCamera = false;      // false: nothing (usable) on View, so C = V = identity
    bool hasProjection = false;  // false: nothing connected, P is the identity
    bool customProjection = false;  // P comes from anything other than a lone Projection node
    bool perspective = false;    // P puts depth into w, so the divide changes x and y
    glm::mat4 cameraPose{1.0f};  // C: camera -> world
    glm::mat4 view{1.0f};        // V = C^-1: world -> camera
    glm::mat4 projection{1.0f};  // P
    // fov/aspect come from a Projection node. near/far are read back from P itself
    // (distances in front of the camera of the -1 and +1 depth faces).
    float fovDeg = 60.0f, aspect = 16.0f / 9.0f, nearPlane = 0.5f, farPlane = 25.0f;
    int planarKind = -1;         // P is a lone Planar Projection node of this kind, else -1
    // Animation: one line per animated node for the overlays, and which one moves.
    std::vector<std::string> animLines;
    std::string animStatus;      // "2 of 3: Rotate X (phi)", empty when nothing is animated
    // When the animation includes nodes of the Projection chain, view 1 shows
    // that chain acting on camera space: the matrix so far, where the view plane
    // is, and one overlay line per step.
    bool stepsActive = false;
    glm::mat4 stepsMatrix{1.0f};
    glm::mat4 stepsPlane{1.0f};
    std::vector<std::string> stepsLines;
    std::vector<std::string> warnings;
};

struct NodeGraph {
    std::vector<Node> nodes;
    std::vector<Link> links;
    int nextNodeId = 1;
    int nextLinkId = 1;

    // Animating a set of matrices, one after another, in every view. animNodes is
    // in pipeline order (M chains, then V, then P; upstream first). Node i moves
    // while animT goes from i to i + 1; the ones after it wait at the identity.
    std::vector<int> animNodes;
    float animT = 0.0f;
    bool animPlaying = false;

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

    // Advances every spinning Rotate node, and the animated node, by dt seconds.
    void Animate(float dt);

    // The steps of the chain ending at `nodeId`, in data-flow order: each
    // node's own, and a Planar Projection's slide steps.
    std::vector<ProjStep> ProjectionSteps(int nodeId);
    // The ids of the transforms feeding the Output's Projection pin.
    std::vector<int> ProjectionChain();

    // Starts animating these nodes (any order; the ones that can't animate are
    // dropped) from the start. Returns how many will animate.
    int StartAnimation(const std::vector<int>& ids);
    void StopAnimation() { animNodes.clear(); animPlaying = false; animT = 0.0f; }
    bool IsAnimated(int id) const;
    // Replaces a Planar Projection with the chain from PlanarExpansion, keeping
    // its links. Needs an imnodes context (it places the new nodes).
    bool ExpandPlanar(int nodeId);

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

// Examples for the projections of the slides, for the Graph menu.
constexpr int kProjectionExampleCount = 4;
extern const char* kProjectionExamples[kProjectionExampleCount];
void LoadProjectionExample(NodeGraph& graph, int index);
