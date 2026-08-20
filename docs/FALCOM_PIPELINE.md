# Falcom's Level Production Pipeline — Distilled Analysis & Map Design Research

A comprehensive breakdown of how Nihon Falcom (*Ys VI: The Ark of Napishtim*,
*The Legend of Heroes: Trails in the Sky*, *Ys: The Oath in Felghana*, *Ys Origin*,
*Gurumin*, *Zwei II*) authored and rendered their 3D maps, the historical DCC
tooling that enabled tiny teams to produce vast JRPG worlds at high velocity, and
how to recreate this "2.5D Set Studio" workflow in modern tools (Blender → Godot)
without tool friction.

Evidence basis: Binary reverse-engineering in this repository (`docs/FORMAT_SPECS.md`,
SCM/SFO/SCT/SEN analysis, extracted geometry/textures), Japanese developer
interviews and recruitment archives, historical LightWave 3D DCC documentation,
and direct mesh wireframe analysis.

---

## 1. Historical Studio Context & DCC Tooling

During their golden 2000s PC era, Falcom operated with remarkably lean graphic
teams (typically **3 to 5 map/background designers** total per title):

* **Primary 3D DCC Tool:** **LightWave 3D** (LightWave 6.5–8.x Modeler & Layout)
  distributed in Japan by D-Storm, paired with **Adobe Photoshop**.
* **The 2D-to-3D Paradigm Shift:** Before *Ys VI* (2003), Falcom specialized in
  2D pixel art and 2D isometric tile engines (*Vantage Master Japan*, *Dinosaur*,
  *Zwei!!*, *Ys I & II Complete*). When transitioning to 3D, their artists did not
  adopt freeform 360° digital sculpting or organic modeling. Instead, they
  transposed their **2D isometric tile thinking directly into 3D polygonal sets**.
* **LightWave Modeler’s Unique Architecture:**
  - **4-View Orthographic Modeling:** Artists worked primarily in the 2D Top View
    with strict numeric grid snapping, maintaining constant isometric proportion.
  - **Planar & Box Projection Defaults:** LightWave allowed immediate planar
    texture assignment by surface name without manual UV unwrapping or seam cutting.
  - **Layered Scratchpads:** Modeler's background layers allowed designers to keep
    2D floorplans/grid templates in the background while rapidly pulling up
    geometry in the foreground.

```
+-------------------------------------------------------------------------------+
|                        FALCOM 2000s LEVEL CREATION PIPELINE                   |
+-------------------------------------------------------------------------------+
|  LightWave 3D (Modeler)              Photoshop (256x256 DDS)                  |
|  - 4-View Ortho Modeling             - Tiling Environment Materials (YUKA/WALL)|
|  - 2D Top-View Grid Snap Blockout    - Alpha-tested Foliage & Props           |
|  - Planar / Cubic Auto-UVs           - Hand-painted Set Dressing              |
+-----------------------------+-------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------------------------+
|  Proprietary Exporters & Scene Assembly                                       |
|  - Vertex Color Baking (AO + Color Tint into Vertex Stream)                   |
|  - .YMO (Visual Meshes: Single-sided, Culled Backfaces, Billboard Cards)       |
|  - .YCO (Simplified Collision Prisms: Walkable, Wall, Camera Blocking)        |
|  - .SOB (Prop Placement Matrices) + .SCM (Fixed Camera Bounds & Pitch)        |
+-------------------------------------------------------------------------------+
```

---

## 2. Geometric Deconstruction: Grids, 45° Triangulation & Planar UVs

Detailed inspection of extracted game meshes (such as Canaan Island / Quatera Woods
in *Ys VI*) reveals three core principles of Falcom's map construction:

```
          [ Camera Ray: 45° Yaw, ~35-45° Pitch ]
                         \
                          \
                           v
       +---------------------------------------+
      / \                                     / \
     /   \  Octagonal Deck Triangulation:    /   \  <-- Planar UV (Y-axis)
    /     \ Diagonal edges match camera     /     \     runs straight across
   +-------+ silhouette and reduce edge-    +-------+    all diagonal cuts
    \     /  aliasing.                      \     /
     \   /                                   \   /
      \ /                                     \ /
       +---------------------------------------+
           |                               |
     [Front Wall/Stairs]              [Backfaces]
   Modeled & UV-textured         COMPLETELY DELETED
                                (Zero geometry behind)
```

### A. The Diamond (45°) Grid Subdivision
* **Isometric Line-of-Sight Alignment:** In a locked isometric or 45° pitch/yaw
  camera, rendering a standard axis-aligned square quad grid causes stair-stepping
  and aliasing along visual diagonal axes.
* By rotating the terrain subdivision grid 45° (or triangulating quads along
  the diagonal), polygon edge boundaries align parallel and perpendicular to the
  camera view frustum. This yields razor-sharp silhouette edges on ramps, cliffs,
  and stairs without increasing polygon density.

### B. Planar World-Space Y-Axis UV Mapping (Zero Manual Unwrapping)
* Notice how wood planking or stone paving textures on octagonal/circular platforms
  and diagonal ramps continue seamlessly across non-rectangular polygon cuts.
* **Mechanism:** Artists did not unwrap UV coordinates per polygon. They assigned a
  **Planar UV Projection along the global Y-axis** with a uniform world-space scale
  (e.g., $1.0\text{ UV} = 2.0\text{ meters}$).
* **Production Benefit:** Designers could slice, boolean, bevel, or rotate floor
  shapes arbitrarily without ever creating UV seams, distortion, or stretching.

### C. Theatrical Stage-Set Construction (舞台セット方式)
Because every room is viewed from a bounded, locked camera pitch (45° outdoor/dungeon,
0° top-down indoor):
1. **Complete Backface Deletion:** Surfaces facing away from the camera ($+Z$ wall
   backs, hidden cliff rears, undersides of bridges) were never created or were
   immediately deleted. The mesh is deliberately non-manifold.
2. **Camera-Oriented Billboard Clusters:** Palm trees, ferns, bushes, and decorative
   lanterns are 2.5D polygonal cards oriented toward the camera normal.
3. **Black Horizon Collapses:** Room boundaries do not render skyboxes; edge
   vertices are collapsed to a corner and mapped with a 1x1 black texture
   (`_C_20BLACK1.DDS`), fading seamlessly into the viewport letterbox.

---

## 3. Runtime Tech — What the Engine Actually Renders

### Materials & Vertex Streams (from YMO RE)
- **Vertex formats** (stride 36 or 40 bytes):
  - 36: `Pos(3f), Norm(3f), Color(u32 ARGB), UV(2f)`
  - 40: `Pos(3f), Norm(2f/3f), Color0(u32 ARGB), Color1(u32 ARGB), UV(2f), UV2(1f/2f)`
- **Dual baked vertex-color channels**: Wide format encodes pre-baked AO/tint
  (multiply) + second tint channel (specular/emissive-ish). No real-time dynamic
  GI exists; all lighting and mood are baked directly into vertex colors.
- **Material records**: Blend mode flags (opaque, alpha-test, additive),
  `alpha` value (1.0 opaque, ~0.2 glass, **0.0 = trigger/logic mesh**), and DDS
  texture path.
- **Texture conventions**: DDS 256² (H) and 128² (L) variants. Names encode
  zone/type: `01WALL12`, `01YUKA12` (yuka = floor), `20GAKE` (cliff),
  `Z_ZHIKARI` (additive light shaft), `_C_*` (shared zone commons).

### Camera System (from SCM/SFO/SCT/SEN RE)
- **SCM = Camera AABB & Pitch per room**: 6 floats defining camera bounding box
  (`max_x, max_y, max_z, min_x, min_y, min_z`) + pitch angle (0.79 rad ≈ 45°
  outdoor/dungeon, 0.0 = top-down indoor). Camera tracks player clamped to this AABB.
- **SFO = Play Area AABB**: Clamping and entity culling bounds.
- **SCT = Scripted Camera Transitions**: Named camera interpolation tracks
  (`CAST_M100`, `CAST_M110`) triggered at room thresholds.
- **SEN = Scene Transition Graph**: Room exit/entrance connectivity table.

### Scene Composition (from SOB RE)
- **Modular Props**: Rooms + props (doors, chests, torches, levers, breakables)
  are separate `.YMO` models placed by `.SOB` records (path + translate + Euler
  rotate + scale).
- **Separated Collision Geometry**:
  - `__s.yco`: Walkable floor collision.
  - `__w.yco`: Wall blocking collision.
  - `__c.yco`: Camera obstruction / trigger volumes.

---

## 4. Why This Pipeline Outperforms Traditional 3D in Iteration Speed

| Traditional 360° 3D Workflow | Falcom Fixed-Angle 2.5D Set Workflow |
|---|---|
| Model all 6 sides of every building and prop | Model only the 1 or 2 camera-facing facades |
| Manual UV unwrapping, seam placement, packing | Planar/Cubic world projection on the grid |
| Lightmap UV baking, PBR material graph setup | Direct vertex color painting (AO/tint) on mesh |
| Full 3D LODs, occlusion culling, collision generation | Collision is a 2D/prism mesh; visual mesh is unconstrained |
| **Iteration cycle:** Days to weeks per area | **Iteration cycle:** A full dungeon room in 4–6 hours |

---

## 5. Modern Recreation: The "2.5D Set Studio" (Blender → Godot)

To replicate Falcom’s rapid set-building speed in modern tools without fighting
defaults (perspective free-cam, PBR shaders, manual UV unwrapping):

```
+-------------------------------------------------------------------------------+
|                            BLENDER WORKSPACE SETUP                            |
+-------------------------------------------------------------------------------+
|  LEFT VIEWPORT: Ortho Top View [7]   |  RIGHT VIEWPORT: Camera View [Numpad 0]|
|  - Grid Snap: Increment (0.5m / 1m)  |  - Locked Pitch: 35.264° / 45°         |
|  - Rapid Plane/Box Extrusion         |  - Shading: Flat / Unshaded Texture    |
|  - Knife/Slice at 45° angles         |  - Live feedback of exact final frame  |
+--------------------------------------+----------------------------------------+
```

### Step-by-Step Production Recipe

1. **Camera Lock First:** Set up your Camera3D with fixed pitch ($35.264^\circ$
   isometric or $45^\circ$) and orthogonal or fixed FOV ($30^\circ$) before placing
   the first polygon.
2. **World-Space Triplanar / Planar Shader:** Eliminate manual UV unwrapping
   entirely. Use Godot's `uv1_world_triplanar = true` on `StandardMaterial3D`
   (or Object-Generated texture coordinates in Blender). Geometry can be cut and
   beveled at any angle; textures align automatically.
3. **Stage-Set Modeling:** In 2D Top View with grid snapping enabled, extrude
   floors and front-facing walls. Immediately delete all rear, bottom, and hidden
   polygons.
4. **Billboard Dressing:** Place quad cards for vegetation, trees, and props.
   Rotate them to face the camera normal ($Y\text{-yaw} + X\text{-pitch}$).
5. **Vertex AO Baking & Tinting:** Bake ambient occlusion to vertex colors (Blender:
   *Dirty Vertex Colors* or Cycles Bake to Vertex Color). Use Vertex Paint to apply
   atmospheric ground tints.
6. **Bake Composite Splats (Optional):** Where unique ground dirt/moss is needed,
   merge the local region, project UVs onto an $N \times M$ tiled texture, and
   texture-paint the details in Photoshop/Blender.
7. **Separate Collision Prisms:** Extrude 2D floor boundaries upward to generate
   simple convex collision bodies (`StaticBody3D`), keeping collision separate
   from the visual stage-set.
8. **Unshaded Rendering & Linear Fog:** Set materials to unshaded mode
   (`shading_mode = unshaded`) with linear distance fog in `WorldEnvironment`.

---

## 6. Implementation Reference Matrix

| Element | Blender (Built-in) | Godot (Built-in) |
|---|---|---|
| Grid blockout | Top Ortho [7], Increment Snap, Box/Plane extrude | GLB import, default meshes |
| Auto-aligned UVs | World/Generated Texture Mapping (no unwrapping) | `uv1_world_triplanar = true`, unshaded |
| Baked vertex AO | *Dirty Vertex Colors* / Bake to Vertex Color | `vertex_color_use_as_albedo = true` |
| Stage Dressing | Alpha-scissor quad cards facing camera | `transparency = alpha_scissor`, unshaded |
| Bounded Camera | Camera object locked at 45°/35.264° | Camera3D + AABB clamp follow script (~15 lines) |
| Distance Fog | World Mist preview | WorldEnvironment → Fog (Linear depth) |
| Background | Emission/Unshaded Plane at stage edge | MeshInstance3D + unshaded backdrop material |
| Collision Separation | Low-poly extruded prisms in separate collection | StaticBody3D with `create_trimesh_collision()` |

---

## Appendix: Cross-Reference to Repository Formats

- `docs/FORMAT_SPECS.md` — YMO/YCO/SOB/NA/NI binary specifications.
- SCM/SFO/SCT/SEN findings: Parsed camera bounding boxes, pitch values (S_0100 tavern = 0.0 top-down, S_1000 quarry = 0.79 rad / 45°), scene transition graph.
- Texture conventions: DDS 256² (H) and 128² (L) pairs, `_C_` shared commons, `Z_ZHIKARI` additive shafts, `_C_20BLACK1` edge fades.
