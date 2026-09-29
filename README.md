# MVP Visualizer

A teaching tool for the Model / View / Projection matrices. You build the
transforms with a node graph, and three views show the same scene:

1. **What you think happens.** The world stays still and the camera flies
   through it. The camera is drawn at its pose `C`.
2. **What actually happens.** The camera never moves. It sits at the origin
   looking down -Z, and the whole world is moved by `V = inverse(C)`.
   - The **perspective divide** slider bends the scene from eye space into the
     projected space. The frustum turns into a box and identical pillars
     shrink with distance, because x and y get divided by depth.
   - The depth dropdown switches between keeping real distance (easy to read)
     and NDC z, which is what the depth buffer stores. NDC z crowds
     everything towards the far plane.
   - "ghost" draws a faint wireframe of where things were before the divide.
3. **What the camera sees.** The final image, `clip = P * V * M * vertex`.

Windows 1 and 2 can be orbited by dragging and zoomed with the mouse wheel.
Things the camera can't see are dimmed. Trails show the camera's path in
window 1 and the world origin's path around the camera in window 2. They are
mirror images of each other.

## Windows and docking

Every window is dockable. Drag a window by its tab or title bar and drop it
on the docking targets to split, tab or float it. The layout is saved in
`imgui.ini`, in the folder you run the app from, and restored on the next
start.

- The **View** menu shows or hides each window. **View > Reset layout**
  restores the default arrangement.
- The **Graph** menu loads the starter example or starts an empty graph.
- **Settings** holds the scene toggles and the perspective-divide controls.
- **Matrices** lists C, V, P and each object's M and P * V * M.

The app uses the docking branch of Dear ImGui, pinned to `v1.90.9-docking`.

The dark theme and its colors are set in `ApplyDarkTheme` in `main.cpp`. The
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

- Chains read in **data-flow order**. `Object -> Rotate -> Translate` rotates
  the object first and then moves it, so `M = T * R`.
- The camera chain builds the camera's pose `C`. The Output node inverts it to
  get `V`. Put a Rotate Y after the Camera to orbit it around the world origin.
- Rotate nodes have a **spin** option to animate them.
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

- `NodeGraph.h` holds the node and link data model and the graph evaluation.
- `main.cpp` holds the window, the shaders, the rendering of the three views
  and the node editor UI.

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
