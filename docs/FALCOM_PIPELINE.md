# Falcom's Level Production Pipeline — Distilled Analysis

How *Ys: The Oath in Felghana* (and the Napishtim engine family) likely
authored and rendered its levels, and how to recreate the workflow in
Blender → Godot using built-in tools only.

Evidence basis: binary RE in this repo (`docs/FORMAT_SPECS.md`, this
session's SCM/SFO/SCT/SEN analysis, extracted geometry/textures) plus direct
geometry observation. Facts from RE are stated as such; production-process
claims are labeled inference where they extrapolate.

---

## 1. Runtime tech — what the engine actually renders

### Materials & vertex data (from YMO RE)
- **Vertex formats** (stride 36 or 40 bytes):
  - 36: `Pos(3f), Norm(3f), Color(u32 ARGB), UV(2f)`
  - 40: `Pos(3f), Norm(2f/3f), Color0(u32 ARGB), Color1(u32 ARGB), UV(2f), UV2(1f/2f)`
- **Two baked vertex-color channels** on the wide format — reads as pre-baked
  AO/tint (multiply) + a second tint channel (specular/emissive-ish). No
  real-time GI exists; all shading is baked into vertex colors.
- **Material record**: `flags` (blend modes, double-sided, additive),
  `alpha` (1.0 opaque, ~0.2 glass, **0.0 = trigger** — invisible logic mesh),
  texture path to a DDS.
- **Textures are DDS, 256² (H) and 128² (L)** variants per asset — the game
  streams/resolves resolution per distance or platform. Names encode
  zone+type+variant: `01WALL12`, `01YUKA12` (yuka=floor), `20GAKE` (cliff),
  `Z_ZHIKARI` (light shaft, additive), `_C_*` (shared "common" across a zone).

### Camera system (from SCM/SFO/SCT/SEN RE)
- **SCM = one camera config per room**: 6 floats = camera AABB
  (`max_x,max_y,max_z,min_x,min_y,min_z`), plus a pitch field
  (0.79 rad ≈ 45° for outdoor/dungeon, **0.0 = top-down for indoor** —
  e.g. S_0100 tavern is flat top-down, S_1000 quarry is 45°).
- Camera follows the player **clamped to the AABB** — a locked-angle,
  room-bounded camera. This is the single strongest identity element.
- **SFO = play-area AABB** (camera clamping + entity culling bounds).
- **SCT = named scripted camera transition records** (`CAST_M100`,
  `CAST_M110`, …) — preset angles/positions the camera lerps between when
  crossing room thresholds.
- **SEN = scene transition graph** — per-stage records naming destination
  stages (exits/entrances), forming the map connectivity.

### Scene composition (from SOB RE)
- Maps are **modular kits**: room meshes + props (doors, chests, torches,
  levers, breakable walls) as separate YMO models, placed by SOB records
  (path + translate + Euler rotate + scale). 116 shared props in `MAPOBJ`.
- **Collision is authored separately** from visuals: `__s.yco` walkable,
  `__w.yco` walls, `__c.yco` camera volumes — not derived from the render mesh.
  Same pattern must carry into Godot.

### Effects
- Backdrops are **flat planes in the distance, not skyboxes**.
- **Distance fog** per map.
- **Scrolling UV water / waterfalls** (multiple water textures: WATER0/1/2/3).
- **Additive light shafts** (`Z_ZHIKARI`).
- **Alpha-tested billboard foliage** and **pre-rendered character billboards**
  (sprites baked from low-poly 3D models at fixed angles).
- **Particles** (fire, snow, rain).
- **Fade-to-black** via black-textured geometry (see §2).

---

## 2. How the maps were authored — production pipeline hypothesis

The geometry itself tells the story (user observations + RE cross-check):

### a. Grid blockout → tile-aligned base pass
All visible geometry is **subdivided at consistent intervals matching the
tiling base texture's world scale**. Floors/walls are boxes/planes whose
faces map 1:1 to texture tiles: `UV = world_position / tile_world_size`
for seamless repetition across the whole room. The blockout IS the final
geometry — nothing is sculpted or remeshed later.

### b. Detail is splatted in — and baked before it reaches game data
Some areas carry *other* textures over the base (grass patches, dirt,
stains, scorch, blood). Since the shipped YMO contains **only UVs + vertex
colors + a DDS reference** — no blend weights, no runtime splat
mechanism — the splatting **must have been baked into the texture offline**
in the editor/toolchain. Two baked forms are visible in the data:

1. **Vertex-color splatting** — wide-format dual color channels tint
   regions of the base tile (AO + localized color variation). Cheap, no
   extra textures.
2. **Composite-texture splatting** — for areas that need real painted
   detail, the tiling geometry region is **merged** and re-UV'd onto one
   larger image: the base tile **repeated enough times to stay aligned**
   with the surrounding tiling, with the painted/splatted detail composited
   on top in that same image. The seams between tiled base and merged
   region are invisible because the repeat aligns perfectly.

### c. Fade-to-black via degenerate black geometry
Maps fade out to black at room edges/transitions. The artist **splatted a
pure-black texture** onto the geometry; and because black is black
everywhere, the black region's geometry is **collapsed/merged toward a
single vertex in a map corner** — the seam degeneracy is invisible, saving
draw cost and export noise. (`_C_20BLACK1.DDS` exists in the zone commons:
a tiny black texture.)

### d. Assembly & camera dressing
Rooms + props are placed into the composite scene via the SOB placement
records; each room's SCM camera AABB + pitch is authored in the editor;
SCT records script the transition angles; SEN links the rooms. Baked AO
vertex colors are generated once at export.

### e. Why it looks the way it does
Texture density carries the fidelity, not geometry. The tile-aligned grid
+ baked composite splats + baked vertex AO give the clean, flat, "painted
diorama" look. The 45°/top-down locked camera means you only ever see
floor + one or two wall faces — so maps are effectively **2.5D sets**:
full 3D geometry, but composed for a fixed view.

---

## 3. The aesthetic recipe (checklist)

1. Fixed camera angle per room (45° outdoor, top-down indoor), clamped to
   a room AABB, scripted transitions between rooms.
2. Low-poly, sharp-edged, grid-aligned geometry — no bevels/smoothing.
3. Unlit flat shading with **baked vertex AO** — no PBR, no dynamic light.
4. Tile-aligned base textures; detail baked into composite textures or
   vertex-color tints.
5. Flat-color backdrop planes instead of skyboxes.
6. Linear distance fog on outdoor/large rooms.
7. Alpha-tested billboards for foliage; pre-rendered character billboards.
8. Additive light shafts (god rays / window light).
9. Scrolling-UV water and waterfalls.
10. Particles for fire/snow/rain.
11. Fade-to-black on room transitions (black-textured geometry).
12. Textures at 256², tight palette, muted natural colors + warm interior
    wood/stone.

---

## 4. Recreating in Blender → Godot with built-ins

Per element: Blender default tool → Godot default feature. No addons, no
custom importers, no export plugins — plain GLB.

| Element | Blender (built-in) | Godot (built-in) |
|---|---|---|
| Grid blockout | Snap to grid; box/plane modeling; keep subdivision scale = tile world size | GLB import, default meshes |
| Tile-aligned UVs | Image Texture with Repeat; snap UVs to the grid in UV editor; or UV Project from an ortho view. Rule: 1 tile = 1 UV square | StandardMaterial3D, default UV repeat |
| Baked vertex AO | Vertex Paint (black multiply strokes) or bake AO from a temp lightmap scene to vertex color | `vertex_color_use_as_albedo` on StandardMaterial3D |
| Composite splat textures | Merge region, UV-unwrap to a blank image sized to N×M tile repeats; Texture Paint the detail directly into it (the base tile is already painted in by unwrap alignment) | Just a texture — imported with the GLB |
| Vertex-color splats | Vertex Paint a tint layer on top of base tile | `vertex_color_use_as_albedo`; tint multiplies the texture |
| Fade-to-black | Black material on edge geometry (degenerate/merged mesh optional — invisible anyway) | Same material; black renders identically |
| Unlit look | Emission shader in viewport (or Viewport Shading → Flat) | StandardMaterial3D `shading_mode = unshaded` |
| Fixed camera | Camera object at the 45°/top-down pose for preview framing | Camera3D + a ~15-line GDScript: follow target, clamp to AABB, `look_at`. Ortho `projection` + `size` if you want the flat look |
| Scripted camera transitions | Multiple camera empties | Tween camera transforms between room triggers (AnimationPlayer or small script) |
| Distance fog | (preview: World → Mist or none) | WorldEnvironment → Environment → Fog (linear depth) |
| Backdrop planes | Plane mesh, emission/flat material, placed at map edge | MeshInstance3D + unshaded material; no skybox in the project |
| Billboards (foliage) | Quads + texture with alpha | StandardMaterial3D `transparency = alpha_scissor`, `billboard_mode = enabled` (all built-in) |
| Character billboards | Render the low-poly model to a sprite sheet at fixed angles (ordinary render) | Sprite3D/AnimatedSprite3D with `billboard` mode |
| Light shafts | Additive-ish emission plane; texture `Z_*`-style | StandardMaterial3D `blend_mode = add` |
| Scrolling water | Animated texture preview not needed — UVs are just UVs | ShaderMaterial (5-line UV-scroll) *or* keyframe `uv1_offset` on StandardMaterial3D with an AnimationPlayer — zero shader code |
| Particles | — | GPUParticles3D default presets (fire/snow/rain) |
| Fade-to-black transitions | — | CanvasLayer + ColorRect, tween modulate to black |
| Collision (walkable/wall/camera) | Separate low-poly collision meshes in named collections | StaticBody3D + `create_trimesh_collision()` from each mesh; walkable=layer 1, wall=layer 2, camera volume=Area3D layer 3. Same separation as Ys' `__s/__w/__c.yco` |

### Workflow order (one level)
1. Block out the room on the tile grid (grid snap, tile = e.g. 2 m).
2. Assign the base tiling material; snap UVs so tiles repeat in world space.
3. Vertex Paint AO + any tint splats over the base tile.
4. Where real detail is needed: merge the region, unwrap onto a composite
   image sized to the tile repeats, Texture Paint the detail in.
5. Paint black material on fade-out edges.
6. Set up the room camera (45° or top-down) + fog + backdrop plane.
7. Separate collision meshes (walkable/wall/camera volumes).
8. Export GLB (Blender's default exporter, `+Y up`), drop into Godot,
   apply the StandardMaterial3D settings above per mesh group.

Everything is stock Blender + stock Godot; the only custom code anywhere is
the ~15-line camera-follow script.

---

## Appendix: cross-reference to this repo

- `docs/FORMAT_SPECS.md` — YMO/YCO/SOB/NA/NI binary specs (RE source).
- SCM/SFO/SCT/SEN findings: parsed this session from `extracted/MAP/*`
  (camera AABBs + pitch per room, play bounds, camera transition names,
  scene graph). Notable: S_0100 (tavern) pitch=0 top-down; S_1000 (quarry)
  pitch=0.79 (45°).
- Texture conventions: `output/s00_textures/`, `extracted/MAP/*/COMMON/{H,L}/`
  (H=256², L=128² DDS pairs, `_C_` common pool, `Z_ZHIKARI` additive,
  `_C_20BLACK1` black-fade).
- Scrapped exploratory Blender→Godot pipeline (addon + import plugin +
  custom shaders): archived at `/opt/scratch/godot-ys` — kept only for
  reference; the approach above deliberately avoids custom tooling.
