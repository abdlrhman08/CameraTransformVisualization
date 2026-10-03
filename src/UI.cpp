#include "UI.h"
#include "NodeGraph.h"
#include "Renderer.h"

#include <imgui_internal.h>   // DockBuilder API
#include <imnodes.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#ifdef __linux__
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Theme and fonts
// ---------------------------------------------------------------------------
#ifdef __linux__
#endif

static ImFont* gMonoFont = nullptr;  // used for matrices so the columns line up

ImVec4 Hex(unsigned rgb, float a) {
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

void ApplyDarkTheme(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);

    st.WindowPadding = ImVec2(10, 10);
    st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 6);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.ScrollbarSize = 12;
    st.GrabMinSize = 10;
    st.WindowBorderSize = 1;
    st.FrameBorderSize = 0;
    st.PopupBorderSize = 1;
    st.TabBorderSize = 0;
    st.WindowRounding = 6;
    st.ChildRounding = 6;
    st.FrameRounding = 5;
    st.PopupRounding = 6;
    st.ScrollbarRounding = 6;
    st.GrabRounding = 5;
    st.TabRounding = 5;
    st.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.SeparatorTextBorderSize = 1;
    st.DockingSeparatorSize = 2;

    const ImVec4 bg      = Hex(0x111318);  // window background
    const ImVec4 bgDeep  = Hex(0x0B0C10);  // title bars, docking empty space
    const ImVec4 panel   = Hex(0x181B22);  // frames, headers
    const ImVec4 panelHi = Hex(0x222631);
    const ImVec4 panelAc = Hex(0x2B303D);
    const ImVec4 border  = Hex(0x262A34);
    const ImVec4 text    = Hex(0xD8DCE3);
    const ImVec4 muted   = Hex(0x767E8C);
    const ImVec4 accent  = Hex(0x6C8CFF);  // soft blue
    const ImVec4 accentD = Hex(0x6C8CFF, 0.55f);
    const ImVec4 accentF = Hex(0x6C8CFF, 0.25f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = muted;
    c[ImGuiCol_WindowBg]              = bg;
    c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = Hex(0x15181E, 0.98f);
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = panel;
    c[ImGuiCol_FrameBgHovered]        = panelHi;
    c[ImGuiCol_FrameBgActive]         = panelAc;
    c[ImGuiCol_TitleBg]               = bgDeep;
    c[ImGuiCol_TitleBgActive]         = bgDeep;
    c[ImGuiCol_TitleBgCollapsed]      = bgDeep;
    c[ImGuiCol_MenuBarBg]             = bgDeep;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = panelAc;
    c[ImGuiCol_ScrollbarGrabHovered]  = Hex(0x3A4150);
    c[ImGuiCol_ScrollbarGrabActive]   = Hex(0x485063);
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accentD;
    c[ImGuiCol_SliderGrabActive]      = accent;
    c[ImGuiCol_Button]                = panelHi;
    c[ImGuiCol_ButtonHovered]         = panelAc;
    c[ImGuiCol_ButtonActive]          = accentD;
    c[ImGuiCol_Header]                = panel;
    c[ImGuiCol_HeaderHovered]         = panelHi;
    c[ImGuiCol_HeaderActive]          = panelAc;
    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accentD;
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = accentD;
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_Tab]                   = bgDeep;
    c[ImGuiCol_TabHovered]            = panelHi;
    c[ImGuiCol_TabActive]             = bg;
    c[ImGuiCol_TabUnfocused]          = bgDeep;
    c[ImGuiCol_TabUnfocusedActive]    = bg;
    c[ImGuiCol_DockingPreview]        = accentF;
    c[ImGuiCol_DockingEmptyBg]        = bgDeep;
    c[ImGuiCol_TextSelectedBg]        = accentF;
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_NavHighlight]          = accent;
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.5f);

    st.ScaleAllSizes(scale);

    // Node editor to match.
    ImNodes::StyleColorsDark();
    ImNodesStyle& ns = ImNodes::GetStyle();
    auto U = [](ImVec4 v) { return ImGui::ColorConvertFloat4ToU32(v); };
    ns.Colors[ImNodesCol_GridBackground]         = U(Hex(0x0E1014));
    ns.Colors[ImNodesCol_GridLine]               = U(Hex(0x1A1D24));
    ns.Colors[ImNodesCol_GridLinePrimary]        = U(Hex(0x232733));
    ns.Colors[ImNodesCol_NodeBackground]         = U(Hex(0x1A1D24));
    ns.Colors[ImNodesCol_NodeBackgroundHovered]  = U(Hex(0x1F232B));
    ns.Colors[ImNodesCol_NodeBackgroundSelected] = U(Hex(0x232835));
    ns.Colors[ImNodesCol_NodeOutline]            = U(Hex(0x2C313D));
    ns.Colors[ImNodesCol_Link]                   = U(Hex(0x6C8CFF, 0.85f));
    ns.Colors[ImNodesCol_LinkHovered]            = U(Hex(0x93AAFF));
    ns.Colors[ImNodesCol_LinkSelected]           = U(Hex(0xB9C8FF));
    ns.Colors[ImNodesCol_Pin]                    = U(Hex(0x6C8CFF));
    ns.Colors[ImNodesCol_PinHovered]             = U(Hex(0xB9C8FF));
    ns.Colors[ImNodesCol_BoxSelector]            = U(Hex(0x6C8CFF, 0.15f));
    ns.Colors[ImNodesCol_BoxSelectorOutline]     = U(Hex(0x6C8CFF, 0.6f));
    ns.Colors[ImNodesCol_MiniMapBackground]      = U(Hex(0x0B0C10, 0.85f));
    ns.Colors[ImNodesCol_MiniMapOutline]         = U(Hex(0x2C313D));
    ns.NodeCornerRounding = 6.0f * scale;
    ns.NodePadding = ImVec2(9.0f * scale, 7.0f * scale);
    ns.NodeBorderThickness = 1.0f;
    ns.LinkThickness = 2.5f * scale;
    ns.PinCircleRadius = 4.5f * scale;
    ns.GridSpacing = 28.0f * scale;
}

// Looks for the fonts next to the executable first, then in the Dear ImGui
// sources (MVP_FONT_DIR is set by CMake).
void LoadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    std::vector<std::string> dirs;
#ifdef __linux__
    char exe[4096];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        std::string path(exe, (size_t)len);
        dirs.push_back(path.substr(0, path.find_last_of('/')) + "/fonts");
    }
#endif
    dirs.push_back("fonts");
#ifdef MVP_FONT_DIR
    dirs.push_back(MVP_FONT_DIR);
#endif
    auto exists = [](const std::string& f) { return std::ifstream(f).good(); };
    for (const std::string& d : dirs) {
        std::string ui = d + "/Roboto-Medium.ttf", mono = d + "/Cousine-Regular.ttf";
        if (!exists(ui)) continue;
        ImFontConfig cfg;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;
        io.Fonts->AddFontFromFileTTF(ui.c_str(), 16.0f * scale, &cfg);
        if (exists(mono)) gMonoFont = io.Fonts->AddFontFromFileTTF(mono.c_str(), 14.5f * scale, &cfg);
        return;
    }
    std::fprintf(stderr, "Fonts not found, using ImGui's default font\n");
    io.Fonts->AddFontDefault();
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
void Overlay(ImVec2 at, const std::vector<std::string>& lines, ImU32 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lh = ImGui::GetTextLineHeightWithSpacing();
    float w = 0;
    for (auto& l : lines) w = glm::max(w, ImGui::CalcTextSize(l.c_str()).x);
    ImVec2 p0(at.x + 6, at.y + 6);
    dl->AddRectFilled(p0, ImVec2(p0.x + w + 12, p0.y + lh * lines.size() + 8), IM_COL32(0, 0, 0, 150), 4.0f);
    for (size_t i = 0; i < lines.size(); ++i)
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4 + lh * i), color, lines[i].c_str());
}

void ShowViewport(const char* id, const ViewportFBO& fbo, ImVec2 size, bool* active, bool* hovered) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)fbo.colorTex, size, ImVec2(0, 1), ImVec2(1, 0));
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, size);
    *active = ImGui::IsItemActive();
    *hovered = ImGui::IsItemHovered();
}

void MatrixText(const glm::mat4& m) {
    ImFont* mono = gMonoFont;
    if (mono) ImGui::PushFont(mono);
    ImGui::PushStyleColor(ImGuiCol_Text, Hex(0x9AA3B2));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 1.0f));
    for (int r = 0; r < 4; ++r)
        ImGui::Text("%6.2f %6.2f %6.2f %6.2f", m[0][r], m[1][r], m[2][r], m[3][r]);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    if (mono) ImGui::PopFont();
}

static glm::vec4 Brighten(ImU32 c, float k) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    return glm::vec4(glm::min(v.x * k, 1.0f), glm::min(v.y * k, 1.0f), glm::min(v.z * k, 1.0f), v.w);
}

// A button in a solid color, optionally disabled.
static bool ColoredButton(const char* label, ImU32 color, bool enabled = true) {
    glm::vec4 h = Brighten(color, 1.25f), a = Brighten(color, 1.45f);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(h.x, h.y, h.z, h.w));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(a.x, a.y, a.z, a.w));
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    return pressed;
}

void Label3D(const glm::mat4& viewProj, ImVec2 imagePos, ImVec2 imageSize,
             glm::vec3 p, const char* text, ImU32 color) {
    glm::vec4 c = viewProj * glm::vec4(p, 1.0f);
    if (c.w <= 0.0f) return;
    glm::vec2 n = glm::vec2(c) / c.w;
    if (std::fabs(n.x) > 1.0f || std::fabs(n.y) > 1.0f) return;
    ImVec2 at(imagePos.x + (n.x * 0.5f + 0.5f) * imageSize.x, imagePos.y + (0.5f - n.y * 0.5f) * imageSize.y);
    ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 4, at.y - 6), color, text);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
const char* kControlsHelp =
    "Add nodes with the toolbar, or right-click the canvas\n"
    "Remove: the x on a node, right-click it, or select + Delete\n"
    "Drag between pins: connect      Ctrl+click a link: detach it\n"
    "Click-drag on empty canvas: box select      Middle-drag: pan\n"
    "Views 1 and 2: drag to orbit, mouse wheel to zoom\n"
    "Windows: drag a tab or title bar to dock it anywhere\n\n"
    "Chains read left to right: Object -> Rotate -> Translate\n"
    "rotates the object first, then moves it (M = T * R).";

void BuildDefaultLayout(ImGuiID dockId) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, vp->WorkSize);

    ImGuiID top, bottom, left, rest, mid, right, editor, side;
    ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Down, 0.48f, &bottom, &top);
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 1.0f / 3.0f, &left, &rest);
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, 0.5f, &mid, &right);
    ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.24f, &side, &editor);

    ImGui::DockBuilderDockWindow(kWinThink, left);
    ImGui::DockBuilderDockWindow(kWinActual, mid);
    ImGui::DockBuilderDockWindow(kWinCamera, right);
    ImGui::DockBuilderDockWindow(kWinEditor, editor);
    ImGui::DockBuilderDockWindow(kWinSettings, side);
    ImGui::DockBuilderDockWindow(kWinMatrices, side);
    ImGui::DockBuilderFinish(dockId);
}

// ---------------------------------------------------------------------------
// Node editor
// ---------------------------------------------------------------------------
static ImU32 TitleColorFor(const Node& n) {
    switch (n.type) {
        case NodeType::Object:     return IM_COL32(40, 100, 170, 255);
        case NodeType::Camera:     return IM_COL32(185, 120, 30, 255);
        case NodeType::Projection: return IM_COL32(125, 60, 160, 255);
        case NodeType::Output:     return IM_COL32(40, 130, 70, 255);
        default: break;
    }
    switch (n.kind) {
        case ChainKind::Model:      return IM_COL32(60, 85, 125, 255);
        case ChainKind::CameraPose: return IM_COL32(130, 95, 45, 255);
        case ChainKind::Projection: return IM_COL32(95, 55, 120, 255);
        case ChainKind::Invalid:    return IM_COL32(150, 40, 40, 255);
        default:                    return IM_COL32(70, 70, 70, 255);
    }
}

struct NodeKindInfo { NodeType type; const char* label; };
static const NodeKindInfo kSourceKinds[] = {
    {NodeType::Object, "Object"}, {NodeType::Camera, "Camera"}, {NodeType::Projection, "Projection"},
};
static const NodeKindInfo kTransformKinds[] = {
    {NodeType::Translate, "Translate"}, {NodeType::RotateX, "Rotate X"}, {NodeType::RotateY, "Rotate Y"},
    {NodeType::RotateZ, "Rotate Z"}, {NodeType::Scale, "Scale"}, {NodeType::Matrix, "Matrix"},
};

static const char* NodeKindName(NodeType t) {
    for (auto& k : kSourceKinds) if (k.type == t) return k.label;
    for (auto& k : kTransformKinds) if (k.type == t) return k.label;
    return "MVP Output";
}

static ImU32 TypeColor(NodeType t) {
    switch (t) {
        case NodeType::Object:     return IM_COL32(40, 100, 170, 255);
        case NodeType::Camera:     return IM_COL32(185, 120, 30, 255);
        case NodeType::Projection: return IM_COL32(125, 60, 160, 255);
        case NodeType::Output:     return IM_COL32(40, 130, 70, 255);
        default:                   return IM_COL32(75, 75, 80, 255);
    }
}

// Editable 4x4 matrix, shown in rows as written on paper. GLM stores columns,
// so row r is (m[0][r], m[1][r], m[2][r], m[3][r]).
static void DrawMatrixEditor(glm::mat4& m) {
    ImGui::PushItemWidth(230.0f);
    for (int r = 0; r < 4; ++r) {
        float row[4] = {m[0][r], m[1][r], m[2][r], m[3][r]};
        ImGui::PushID(r);
        if (ImGui::DragFloat4("##row", row, 0.01f, 0.0f, 0.0f, "%.2f"))
            for (int c = 0; c < 4; ++c) m[c][r] = row[c];
        ImGui::PopID();
    }
    ImGui::PopItemWidth();

    if (ImGui::SmallButton("Identity")) m = glm::mat4(1.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Transpose")) m = glm::transpose(m);
    ImGui::SameLine();
    float det = glm::determinant(m);
    bool invertible = std::fabs(det) > 1e-6f;
    ImGui::BeginDisabled(!invertible);
    if (ImGui::SmallButton("Invert")) m = glm::inverse(m);
    ImGui::EndDisabled();
    if (!invertible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("The determinant is 0, so this matrix has no inverse.");
    ImGui::TextDisabled("det = %.3f", det);
}

// "Translate", "Translate 2", "Translate 3", ...
static std::string UniqueName(NodeGraph& g, NodeType t, const char* base) {
    int count = 0;
    for (auto& n : g.nodes) if (n.type == t) ++count;
    return count == 0 ? std::string(base) : std::string(base) + " " + std::to_string(count + 1);
}

void DrawNodeEditor(NodeGraph& graph, Options& opt, const EvalResult& lastEval, bool* open) {
    if (!ImGui::Begin(kWinEditor, open)) { ImGui::End(); return; }

    static int spawnCount = 0;           // cascades nodes added from the toolbar
    std::vector<int> nodesToDelete;      // applied after EndNodeEditor
    std::vector<int> linksToDelete;
    auto queueSelected = [&]() {
        int nl = ImNodes::NumSelectedLinks();
        if (nl > 0) { std::vector<int> ids(nl); ImNodes::GetSelectedLinks(ids.data()); linksToDelete.insert(linksToDelete.end(), ids.begin(), ids.end()); }
        int nn = ImNodes::NumSelectedNodes();
        if (nn > 0) { std::vector<int> ids(nn); ImNodes::GetSelectedNodes(ids.data()); nodesToDelete.insert(nodesToDelete.end(), ids.begin(), ids.end()); }
    };
    auto duplicate = [&](int id) {
        Node* src = graph.FindNode(id);
        if (!src || src->type == NodeType::Output) return;
        Node copy = *src;
        int newId = graph.AddNode(copy.type, UniqueName(graph, copy.type, NodeKindName(copy.type)));
        std::string name = graph.FindNode(newId)->name;
        Node* dst = graph.FindNode(newId);
        *dst = copy;
        dst->id = newId;
        dst->name = name;
        ImVec2 p = ImNodes::GetNodeGridSpacePos(id);
        ImNodes::SetNodeGridSpacePos(newId, ImVec2(p.x + 40, p.y + 40));
    };

    // ---- toolbar: add / remove ----
    int numSelNodes = ImNodes::NumSelectedNodes(), numSelLinks = ImNodes::NumSelectedLinks();
    ImVec2 addAt(0, 0);
    NodeType toAdd = NodeType::Object;
    const char* toAddLabel = nullptr;

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Add:");
    for (auto& k : kSourceKinds) {
        ImGui::SameLine();
        if (ColoredButton(k.label, TypeColor(k.type))) { toAdd = k.type; toAddLabel = k.label; }
    }
    ImGui::SameLine(); ImGui::TextDisabled("|");
    for (auto& k : kTransformKinds) {
        ImGui::SameLine();
        if (ColoredButton(k.label, TypeColor(k.type))) { toAdd = k.type; toAddLabel = k.label; }
    }
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    bool hasOutput = graph.OutputNode() != nullptr;
    if (ColoredButton("MVP Output", TypeColor(NodeType::Output), !hasOutput)) { toAdd = NodeType::Output; toAddLabel = "MVP Output"; }
    if (hasOutput && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("There can only be one MVP Output.");

    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    ImGui::BeginDisabled(numSelNodes == 0);
    if (ImGui::Button("Duplicate")) {
        std::vector<int> ids(numSelNodes);
        ImNodes::GetSelectedNodes(ids.data());
        for (int id : ids) duplicate(id);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    char delLabel[64];
    std::snprintf(delLabel, sizeof(delLabel), "Delete selected (%d)###delsel", numSelNodes + numSelLinks);
    if (ColoredButton(delLabel, IM_COL32(150, 45, 45, 255), numSelNodes + numSelLinks > 0)) queueSelected();
    ImGui::SameLine();
    if (ImGui::Button("Reset...")) ImGui::OpenPopup("reset_graph");
    if (ImGui::BeginPopup("reset_graph")) {
        ImGui::TextDisabled("Replace the whole graph with:");
        if (ImGui::MenuItem("The starter example")) ResetGraph(graph, true);
        if (ImGui::MenuItem("An empty graph (just MVP Output)")) ResetGraph(graph, false);
        ImGui::Separator();
        ImGui::MenuItem("Cancel");
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Show matrices", &opt.showMatrices);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kControlsHelp);
    for (auto& w : lastEval.warnings)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "! %s", w.c_str());

    // New nodes from the toolbar appear in the middle of the visible canvas.
    {
        ImVec2 c = ImGui::GetCursorScreenPos(), a = ImGui::GetContentRegionAvail();
        float off = (float)(spawnCount % 6) * 30.0f;
        addAt = ImVec2(c.x + a.x * 0.4f + off, c.y + a.y * 0.3f + off);
    }
    if (toAddLabel) {
        int id = graph.AddNode(toAdd, UniqueName(graph, toAdd, toAddLabel));
        ImNodes::SetNodeScreenSpacePos(id, addAt);
        ++spawnCount;
    }

    // Input fields need more contrast on a node than on a plain window.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Hex(0x272C38));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Hex(0x303644));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Hex(0x394052));
    ImNodes::BeginNodeEditor();

    for (auto& n : graph.nodes) {
        ImU32 title = TitleColorFor(n);
        glm::vec4 hov = Brighten(title, 1.25f), sel = Brighten(title, 1.45f);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, ImGui::ColorConvertFloat4ToU32(ImVec4(hov.x, hov.y, hov.z, hov.w)));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, ImGui::ColorConvertFloat4ToU32(ImVec4(sel.x, sel.y, sel.z, sel.w)));

        ImNodes::BeginNode(n.id);
        ImGui::PushID(n.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(n.name.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 60));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(200, 60, 60, 255));
        if (ImGui::SmallButton("x")) nodesToDelete.push_back(n.id);
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this node");
        ImNodes::EndNodeTitleBar();

        ImGui::PushItemWidth(150.0f);

        if (n.type == NodeType::Output) {
            ImNodes::BeginInputAttribute(NodeGraph::InAttr(n.id));
            ImGui::TextUnformatted("Model  (M, one per object)");
            ImNodes::EndInputAttribute();
            ImNodes::BeginInputAttribute(NodeGraph::ViewInAttr(n.id));
            ImGui::TextUnformatted("View   (camera pose C)");
            ImNodes::EndInputAttribute();
            ImNodes::BeginInputAttribute(NodeGraph::ProjInAttr(n.id));
            ImGui::TextUnformatted("Projection  (P)");
            ImNodes::EndInputAttribute();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "clip = P * V * M * vertex");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "V = inverse(C)");
            if (opt.showMatrices) {
                ImGui::TextUnformatted("V (world -> camera):");
                MatrixText(lastEval.view);
            }
        } else {
            if (n.type != NodeType::Object && n.type != NodeType::Camera && n.type != NodeType::Projection) {
                ImNodes::BeginInputAttribute(NodeGraph::InAttr(n.id));
                ImGui::TextUnformatted("in");
                ImNodes::EndInputAttribute();
            }

            switch (n.type) {
                case NodeType::Object:
                {
                    const char* shapes[] = {"Cube", "Triangle"};
                    ImGui::SetNextItemWidth(110.0f);
                    ImGui::Combo("shape", &n.shape, shapes, 2);
                    if (n.shape == kShapeTriangle) {
                        ImGui::TextDisabled("corners (local space)");
                        const char* names[3] = {"A", "B", "C"};
                        for (int i = 0; i < 3; ++i)
                            ImGui::DragFloat3(names[i], &n.tri[i].x, 0.02f, 0.0f, 0.0f, "%.2f");
                        if (ImGui::SmallButton("Reset triangle")) {
                            Node def;
                            for (int i = 0; i < 3; ++i) n.tri[i] = def.tri[i];
                        }
                    }
                    break;
                }
                case NodeType::Translate:
                    ImGui::DragFloat3("offset", &n.vec.x, 0.05f);
                    break;
                case NodeType::Scale:
                    ImGui::DragFloat3("factor", &n.vec.x, 0.02f);
                    break;
                case NodeType::Matrix:
                    DrawMatrixEditor(n.custom);
                    break;
                case NodeType::RotateX:
                case NodeType::RotateY:
                case NodeType::RotateZ:
                    ImGui::DragFloat("deg", &n.angleDeg, 1.0f, -360.0f, 360.0f);
                    ImGui::Checkbox("spin", &n.spin);
                    if (n.spin) { ImGui::SameLine(); ImGui::PushItemWidth(80); ImGui::DragFloat("deg/s", &n.spinSpeed, 1.0f, -360.0f, 360.0f); ImGui::PopItemWidth(); }
                    break;
                case NodeType::Camera:
                    ImGui::DragFloat3("position", &n.camPos.x, 0.05f);
                    ImGui::DragFloat3("look at", &n.camTarget.x, 0.05f);
                    break;
                case NodeType::Projection:
                    ImGui::DragFloat("fov", &n.fovDeg, 0.5f, 10.0f, 150.0f);
                    ImGui::DragFloat("near", &n.nearPlane, 0.01f, 0.01f, 20.0f);
                    ImGui::DragFloat("far", &n.farPlane, 0.25f, 1.0f, 500.0f);
                    ImGui::TextDisabled("aspect %.2f (from camera window)", n.aspect);
                    break;
                default: break;
            }

            if (opt.showMatrices && n.type != NodeType::Object) {
                const char* label =
                    n.type == NodeType::Projection        ? "P:" :
                    n.kind == ChainKind::Model            ? "model so far (M):" :
                    n.kind == ChainKind::CameraPose       ? "camera pose so far (C):" :
                    n.kind == ChainKind::Projection       ? "projection so far (P):" :
                    n.kind == ChainKind::Invalid          ? "cycle: invalid chain" : "matrix so far (no source):";
                ImGui::TextUnformatted(label);
                MatrixText(n.cumulativeMatrix);
            }

            ImNodes::BeginOutputAttribute(NodeGraph::OutAttr(n.id));
            ImGui::Indent(n.type == NodeType::Object && n.shape != kShapeTriangle ? 120.0f : 150.0f);
            ImGui::TextUnformatted("out");
            ImNodes::EndOutputAttribute();
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
        ImNodes::EndNode();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    for (auto& l : graph.links) ImNodes::Link(l.id, l.startAttr, l.endAttr);

    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();
    ImGui::PopStyleColor(3);

    // ---- link edits (must happen after EndNodeEditor) ----
    int startAttr, endAttr;
    if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) graph.AddLink(startAttr, endAttr);
    int destroyed;
    if (ImNodes::IsLinkDestroyed(&destroyed)) graph.RemoveLink(destroyed);

    // ---- keyboard delete ----
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsAnyItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)))
        queueSelected();

    // ---- right-click menus: on a node, on a link, or on empty canvas ----
    static int contextNode = -1, contextLink = -1;
    if (ImNodes::IsEditorHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 25.0f) {
        int hovered;
        if (ImNodes::IsNodeHovered(&hovered))      { contextNode = hovered; ImGui::OpenPopup("node_menu"); }
        else if (ImNodes::IsLinkHovered(&hovered)) { contextLink = hovered; ImGui::OpenPopup("link_menu"); }
        else                                         ImGui::OpenPopup("add_node");
    }
    if (ImGui::BeginPopup("node_menu")) {
        Node* n = graph.FindNode(contextNode);
        if (n) {
            ImGui::TextDisabled("%s", n->name.c_str());
            if (ImGui::MenuItem("Duplicate", nullptr, false, n->type != NodeType::Output)) duplicate(contextNode);
            if (ImGui::MenuItem("Disconnect all links")) {
                for (auto& l : graph.links)
                    if (NodeGraph::NodeOfAttr(l.startAttr) == contextNode || NodeGraph::NodeOfAttr(l.endAttr) == contextNode)
                        linksToDelete.push_back(l.id);
            }
            if (ImGui::MenuItem("Delete", "Del")) nodesToDelete.push_back(contextNode);
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("link_menu")) {
        if (ImGui::MenuItem("Delete link")) linksToDelete.push_back(contextLink);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("add_node")) {
        ImVec2 at = ImGui::GetMousePosOnOpeningCurrentPopup();
        auto add = [&](NodeType t, const char* label) {
            int id = graph.AddNode(t, UniqueName(graph, t, label));
            ImNodes::SetNodeScreenSpacePos(id, at);
        };
        ImGui::TextDisabled("Sources");
        for (auto& k : kSourceKinds) if (ImGui::MenuItem(k.label)) add(k.type, k.label);
        ImGui::Separator();
        ImGui::TextDisabled("Transforms");
        for (auto& k : kTransformKinds) if (ImGui::MenuItem(k.label)) add(k.type, k.label);
        ImGui::Separator();
        if (ImGui::MenuItem("MVP Output", nullptr, false, !hasOutput)) add(NodeType::Output, "MVP Output");
        if (numSelNodes + numSelLinks > 0) {
            ImGui::Separator();
            if (ImGui::MenuItem("Delete selected", "Del")) queueSelected();
        }
        ImGui::EndPopup();
    }

    // ---- apply deletions ----
    if (!nodesToDelete.empty() || !linksToDelete.empty()) {
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        for (int id : linksToDelete) graph.RemoveLink(id);
        for (int id : nodesToDelete) graph.RemoveNode(id);
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Settings and Matrices
// ---------------------------------------------------------------------------
// Scene-wide toggles.
void DrawSettingsWindow(Options& opt, bool* open) {
    if (ImGui::Begin(kWinSettings, open)) {
        ImGui::SeparatorText("Animation");
        ImGui::Checkbox("Play animations", &opt.playAnimations);
        ImGui::SeparatorText("Scene");
        ImGui::Checkbox("Dim what the camera can't see", &opt.dimOutside);
        ImGui::Checkbox("Grid and axes in the camera view", &opt.cameraGrid);
        ImGui::SeparatorText("Text");
        ImGui::Checkbox("Explanations on the views", &opt.showHints);
        ImGui::Checkbox("Labels in the 3D scene", &opt.showLabels);
        ImGui::SeparatorText("Perspective divide (view 2)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##warp", &opt.warp, 0.0f, 1.0f, "divide: %.2f")) opt.animateWarp = false;
        ImGui::Checkbox("Animate", &opt.animateWarp);
        ImGui::Checkbox("Ghost of the undivided scene", &opt.showGhost);
        ImGui::Checkbox("Cut away outside [-1, 1]", &opt.clipOutside);
        const char* depthModes[] = {"z: distance, spread evenly", "z: real NDC z (depth buffer)"};
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##depthmode2", &opt.depthMode, depthModes, 2);
        ImGui::SeparatorText("Node editor");
        ImGui::Checkbox("Show matrices on nodes", &opt.showMatrices);
    }
    ImGui::End();
}

// Every matrix in play, in one place.
void DrawMatricesWindow(NodeGraph& graph, const EvalResult& ev, bool* open) {
    if (ImGui::Begin(kWinMatrices, open)) {
        ImGui::TextWrapped("clip = P * V * M * vertex, and V = inverse(C).");
        if (ImGui::CollapsingHeader("C: camera pose (camera -> world)", ImGuiTreeNodeFlags_DefaultOpen)) MatrixText(ev.cameraPose);
        if (ImGui::CollapsingHeader("V = inverse(C): world -> camera", ImGuiTreeNodeFlags_DefaultOpen)) MatrixText(ev.view);
        const char* pLabel = !ev.hasProjection    ? "P: none, identity (clip = V * M * v)"
                           : ev.customProjection ? "P: custom (camera -> clip space)"
                                                 : "P: camera -> clip space";
        if (ImGui::CollapsingHeader(pLabel, ImGuiTreeNodeFlags_DefaultOpen)) MatrixText(ev.projection);
        for (size_t i = 0; i < ev.objects.size(); ++i) {
            const ObjectInstance& o = ev.objects[i];
            Node* src = graph.FindNode(o.sourceNodeId);
            std::string name = src ? src->name : "Object";
            ImGui::PushID((int)i);
            if (ImGui::CollapsingHeader((name + ": M (local -> world)").c_str())) MatrixText(o.model);
            if (ImGui::CollapsingHeader((name + ": P * V * M (local -> clip)").c_str())) MatrixText(ev.projection * ev.view * o.model);
            ImGui::PopID();
        }
    }
    ImGui::End();
}

