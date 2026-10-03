# MVP Visualizer

A teaching tool for the Model / View / Projection matrices. You build the
transforms with a node graph, and three views show the same scene:

1. **What you think happens.** The world stays still and the camera flies
   through it. The camera is drawn at its pose `C`.
2. **What actually happens.** The camera never moves. It sits at the origin
   looking down -Z, and the whole world is moved by `V = inverse(C)`.
   - The **perspective divide** slider bends the scene from eye space into the
     projected space. The frustum turns into the cube [-1, 1] and far objects
     shrink, because x and y get divided by depth.
   - The depth dropdown switches between keeping real distance (easy to read)
     and NDC z, which is what the depth buffer stores. NDC z crowds
     everything towards the far plane.
   - "ghost" draws a faint wireframe of where things were before the divide.
3. **What the camera sees.** The final image, `clip = P * V * M * vertex`.

Windows 1 and 2 can be orbited by dragging and zoomed with the mouse wheel.

**Play mode.** Press **Play** in "What the camera sees" to fly the camera:
WASD moves, Q/E go down/up, Shift is faster, dragging the image looks around,
and Esc or **Stop** ends it. Every move is written back into the node graph,
so the other windows and the Camera node's fields follow. The View chain must
start with a Camera or a Matrix node; transforms after it keep working. With
nothing on the View pin, a Camera node is created and linked. Spinning nodes
pause while you fly.
Things the camera can't see are dimmed.

**No projection.** If nothing is connected to the Projection pin, P is the
identity, so `clip = V * M * vertex` and `w` stays 1. The GPU only keeps
x, y and z in [-1, 1] of camera space, which is a 2x2x2 box around the
camera. Only objects inside that box show up, nothing shrinks with distance,
and the image stretches to the window's shape. Depth also comes out reversed:
z = -1, in front of the camera, is depth 0, and z = +1, behind it, is
depth 1. So farther things draw on top. In view 2 the divide slider is
disabled, because there is nothing to divide.

## Windows and docking

Every window is dockable. Drag a window by its tab or title bar and drop it
on the docking targets to split, tab or float it. The layout is saved in
`imgui.ini`, in the folder you run the app from, and restored on the next
start.

- Drag a window outside the app and it becomes its own OS window, which you
  can put on another monitor. Drag it back onto the app to dock it again. On
  Linux the app runs on X11 (XWayland on a Wayland desktop), because this
  ImGui version can't place separate windows on Wayland.
- **H** toggles the explanations on the views and **L** toggles the labels
  in the 3D scene. Both are also in the View menu and in Settings.
- The **View** menu shows or hides each window. **View > Reset layout**
  restores the default arrangement.
- The **Graph** menu loads the starter example or starts an empty graph.
- **Settings** holds the scene toggles and the perspective-divide controls.
- **Matrices** lists C, V, P and each object's M and P * V * M.

The app uses the docking branch of Dear ImGui, pinned to `v1.90.9-docking`.

The dark theme and its colors are set in `ApplyDarkTheme` in `src/UI.cpp`. The
UI font is Roboto and the matrices use Cousine, a monospace font. Both ship
with Dear ImGui, and the build copies them into a `fonts` folder next to the
executable. If they can't be found, the app falls back to ImGui's built-in font.
Fonts and spacing scale up automatically on high-DPI screens.

## The node graph

```
Object ─► Rotate ─► Translate ─► MVP Output.Model      (any number of objects)
Camera ─► Rotate (orbit) ──────► MVP Output.View       (camera pose C)
Projection ────────────────────► MVP Output.Projection
```

- The editor starts with only the **MVP Output** node. **Graph > Load starter
  example** builds a ready-made scene.
- An **Object** is a unit cube or a flat orange triangle. Pick the shape on the node.
- Chains read in **data-flow order**. `Object -> Rotate -> Translate` rotates
  the object first and then moves it, so `M = T * R`.
- The camera chain builds the camera's pose `C`. The Output node inverts it to
  get `V`. Put a Rotate Y after the Camera to orbit it around the world origin.
- Rotate nodes have a **spin** option to animate them.
- The **Matrix** node holds any 4×4 matrix, typed row by row as written on
  paper, with Identity, Transpose and Invert buttons. It works anywhere:
  - in an object chain, as part of M. On its own it is drawn as a cube.
  - in the camera chain, or on its own on the View pin, as the camera pose C.
  - on the Projection pin as P, or after a Projection node to modify it.
  The camera volume and near/far are worked out from P itself, so the views
  stay correct for any invertible matrix.
- An output pin can feed several nodes, so chains can share transforms.
- Title bars are colored by chain: blue for model, orange for camera, purple
  for projection. Red means the chain is invalid, for example a cycle.
- Each node shows the matrix accumulated so far. Toggle this with
  "Show matrices".
- The toolbar at the top of the Node Editor adds any node type. Each button
  has the color the node will have. New nodes appear in the middle of the canvas.
- Remove a node with the **x** in its title bar, or select nodes or links and
  press Delete or Backspace. The toolbar's "Delete selected" button does the
  same thing.
- Right-click a node to duplicate it, disconnect it, or delete it. Right-click
  a link to delete it. Right-click empty canvas to add a node at the mouse.
- "Duplicate" copies the selected nodes with their settings, but not their links.
- "Reset..." replaces the graph with the starter example or an empty graph.
- Drag on empty canvas to box-select. Ctrl+click a link to detach it.

## Files

All the code is in `src/`:

| File | What's in it |
|---|---|
| `main.cpp` | Window and ImGui setup, menu bar, docking, the frame loop |
| `NodeGraph.h/.cpp` | Node and link data model, chain evaluation, the starter example |
| `Renderer.h/.cpp` | OpenGL: the shader, meshes and shapes, framebuffers, scene drawing, orbit camera |
| `UI.h/.cpp` | Options, theme and fonts, widgets, window layout, Node Editor, Settings, Matrices |
| `Views.h/.cpp` | The three 3D windows |

Common changes:

- **New node type:** add it to `NodeType` and `Node::RecomputeLocal` in NodeGraph, then its controls in `DrawNodeEditor` in UI.cpp.
- **New object shape:** add a vertex builder and upload it in `Scene::Init` in Renderer.cpp, then pick it in `DrawWorld`.
- **New window:** add its title and visibility flag in UI.h, a slot in `BuildDefaultLayout`, and a View-menu entry and draw call in main.cpp.

## One manual step: GLAD

GLAD isn't fetched automatically because it's generated per project. Generate
it once:

1. Go to https://glad.dav1d.de/
2. Language: **C/C++**, Specification: **OpenGL**, API gl: **Version 3.3**,
   Profile: **Core**, check "Generate a loader"
3. Place the contents so you have:
   ```
   external/glad/include/glad/glad.h
   external/glad/include/KHR/khrplatform.h
   external/glad/src/glad.c
   ```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/gfx_node_editor
```

The first configure clones GLFW, ImGui, imnodes and GLM, so it takes a while.
If your shell sets `CMAKE_GENERATOR`, always configure a given build folder
with the same generator, or delete the folder and start over.
