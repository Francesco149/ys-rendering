# Ys: The Oath in Felghana — 3D Map & Mesh Extraction Pipeline

Turnkey extraction, conversion, and visualization pipeline for *Ys: The Oath in Felghana* (and compatible Falcom Napishtim Engine games including *Ys VI* and *Ys Origin*). Converts proprietary game archives, map geometry, textures, collision boundaries, and scene object placements into textured, standard **glTF 2.0 / GLB** and **Wavefront OBJ** models ready for topology and UV analysis in Blender.

---

## 🚀 Quickstart

Enter the development shell with all dependencies (Python 3, Pillow, NumPy, PyGLTF, Blender, toolchains):
```bash
nix develop
```

### 1. Extract Game Assets from Steam Installation
```bash
# Extract all map files from default Steam installation:
python3 src/cli.py extract --filter "MAP\\"

# Extract a specific stage or enemy folder:
python3 src/cli.py extract --filter "MAP\\S_01" --output extracted
python3 src/cli.py extract --filter "MAP\\MAPOBJ" --output extracted
```

### 2. Convert a Single 3D Model (`.YMO`)
```bash
# Convert to textured GLB with auto-discovered collision layers:
python3 src/cli.py convert-model extracted/MAP/S_01/S_0100/S_0100.YMO --output output/S_0100.glb

# Convert to Wavefront OBJ + MTL + PNG textures:
python3 src/cli.py convert-model extracted/MAP/S_01/S_0100/S_0100.YMO --format obj --output output/S_0100.obj
```

### 3. Convert a Full Composite Stage Scene (`.SOB`)
Places base terrain, doors, torches, props, and animated objects with correct world transforms:
```bash
python3 src/cli.py convert-stage extracted/MAP/S_01/S_0100/S_0100.SOB --output output/S_0100_composite.glb
```

### 4. Open in Blender with Auto-Organized Collision Collections
```bash
blender --python src/tools/blender_import.py -- output/S_0100_composite.glb
```

---

## 📂 Repository Architecture

```
ys-rendering/
├── flake.nix                  # Nix Flake providing Python, Blender, pygltflib, tools
├── flake.lock
├── docs/
│   └── FORMAT_SPECS.md        # Dense binary format specifications for .NA, .NI, .YMO, .YCO, .SOB
├── src/
│   ├── cli.py                 # Unified CLI tool (extract, convert-model, convert-stage, batch-convert)
│   ├── extractor/
│   │   └── archive.py         # NNI header decryption (LCG cipher) and .na/.z zlib decompressor
│   ├── converter/
│   │   ├── ymo_parser.py      # Binary parser for .YMO (submeshes, vertex buffers, index buffers)
│   │   ├── yco_parser.py      # Binary parser for .YCO (walkable, wall, camera collision meshes)
│   │   ├── gltf_exporter.py   # glTF 2.0 / GLB exporter (texture conversion, PBR, UVs, collision layers)
│   │   ├── obj_exporter.py    # Wavefront OBJ/MTL exporter
│   │   └── stage_builder.py   # Composite stage scene assembler parsing .SOB object placements
│   └── tools/
│       ├── render_model.py    # Headless Blender rendering for smoke tests and visual validation
│       └── blender_import.py  # Blender scene organizer (Collision collection manager)
└── ORIENTATION.md             # This file
```

---

## 🎨 Material & Collision Conventions

- **Textures:** DDS textures are automatically converted to PNG and embedded into the GLB container or placed alongside `.obj`.
- **Additive & Light Shafts:** Textures starting with `Z_` or named `Z_ZHIKARI` have alpha generated from luminance and use `alphaMode="BLEND"` to blend softly without black box artifacts.
- **UV Coordinates:** Directly mapped to match top-left texture coordinates.
- **Collision Layers:**
  - `Collision_Walkable` (`__s.yco`): Semi-transparent Emerald Green (`[0.1, 0.9, 0.4, 0.4]`).
  - `Collision_Wall` (`__w.yco`): Semi-transparent Coral Orange (`[1.0, 0.4, 0.1, 0.4]`).
  - `Collision_Camera` (`__c.yco`): Semi-transparent Cyan Blue (`[0.1, 0.6, 1.0, 0.4]`).
  - In Blender, all collision layers are grouped under a dedicated `Collision` collection and hidden by default.

---

## 🗺️ Known Stages & IDs

- `S_00`: Opening Ship / Port arrival
- `S_01`: Town of Redmont & all building interiors (`S_0100` Tavern/Inn, `S_0110` Weapon Shop, `S_0120` Mayor's House, `S_0150` Church)
- `S_02`: Town Outskirts & Gate
- `S_10`: Tigray Quarry (mine tunnels, exterior canyon, storehouse)
- `S_20`: Ruins of Illburns (temple entrance, underground volcano)
- `S_25`: Lava Zone
- `S_30` / `S_31`: Elderm Mountains (snow peaks, ice caverns)
- `S_35`: Valestein Castle (courtyard, throne room, corridors, clock tower)
- `S_50`: Genos Island (final dungeon, sanctuary)
- `MAPOBJ`: 116 shared 3D props (doors, levers, breakable walls, treasure chests, torches)

---

## 📝 Git Commit Conventions for Future Sessions

All commits created in this repository MUST include the model slug co-author trailer:
```
Co-authored-by: <model-slug> <<model-slug>>
```
Example:
```
feat(converter): add support for animated mesh vertex morphs

Co-authored-by: google-antigravity/gemini-3.7-flash-tiered <google-antigravity/gemini-3.7-flash-tiered>
```
