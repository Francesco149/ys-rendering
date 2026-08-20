# Falcom Napishtim Engine 3D & Archive Format Specification
**Target Titles:** *Ys: The Oath in Felghana*, *Ys VI: The Ark of Napishtim*, *Ys Origin*

---

## 1. Archive Format (`.NA` & `.NI`)

The game stores its assets in paired archive files:
- **`data.na`**: Data payload container.
- **`data.ni`**: Encrypted index file containing file records and string table.

### 1.1 `data.ni` File Header (16 bytes)
| Offset | Type | Field | Description |
|---|---|---|---|
| `0x00` | `u32` | `signature` | Magic: `0x00494E4E` (`"NNI\0"`) |
| `0x04` | `u32` | `entry_count` | Total number of file entries |
| `0x08` | `u32` | `string_table_size` | Size in bytes of decrypted filename pool |
| `0x0C` | `u32` | `zero` | Reserved (0) |

### 1.2 `data.ni` Encryption
Both the file entry table and string table are encrypted using a linear congruential cipher (LCG):
```python
def decrypt_ni(data: bytearray) -> bytearray:
    num = 0x7C53F961
    for i in range(len(data)):
        num = (num * 0x3D09) & 0xFFFFFFFF
        data[i] = (data[i] - ((num >> 16) & 0xFF)) & 0xFF
```

### 1.3 `data.ni` File Entry Record (16 bytes)
| Offset | Type | Field | Description |
|---|---|---|---|
| `0x00` | `u32` | `file_id` | Unique resource ID |
| `0x04` | `u32` | `compressed_size` | Size of payload in `data.na` |
| `0x08` | `u32` | `data_offset` | Byte offset in `data.na` |
| `0x0C` | `u32` | `name_offset` | Byte offset into decrypted string table |

### 1.4 `.Z` Compressed Data Payload
Files ending in `.z` in `data.na` start with an 8-byte header followed by standard raw zlib `DEFLATE` streams:
- `0x00`: `u32` CRC32 of uncompressed data
- `0x04`: `u32` uncompressed size
- `0x08..`: zlib compressed stream

---

## 2. 3D Model Format (`.YMO` - Ys Model Object)

### 2.1 File Header (68 bytes)
| Offset | Type | Field | Description |
|---|---|---|---|
| `0x00` | `4 bytes`| `magic` | `"YMO\0"` |
| `0x04` | `u32` | `version` | Version (`9`) |
| `0x08` | `u32` | `flags_0` | Engine flags |
| `0x0C` | `u32` | `flags_1` | Engine flags |
| `0x10` | `u32` | `flags_2` | Engine flags |
| `0x14` | `u32` | `material_count` | Number of material records |
| `0x18` | `u32` | `motion_count` | Number of animation motion tracks |
| `0x1C` | `u32` | `mesh_count` | Number of 3D meshes |
| `0x20` | `u32` | `node_count` | Number of transform/bone nodes |
| `0x24..0x44`| `floats`| `bounds` | Bounding sphere / box coordinates |
| `0x78..0x168`| `3x80B`| `collision_refs` | Associated `.yco` collision filenames (`__s.yco`, `__w.yco`, `__c.yco`) |

### 2.2 Material Records (388 bytes each)
Materials start at `0x0178` (or `0x0170 + motion_count * 68` when motions > 0):
- `+0x00`: `u32` `flags` (render blend modes, double-sided, additive)
- `+0x04`: `f32` `alpha` (opacity: 1.0 = opaque, 0.2 = glass, 0.0 = trigger)
- `+0x8C`: `string` null-terminated texture path (e.g. `..\common\01wall12.dds`)

### 2.3 Node Records (240 bytes each)
Nodes start immediately after the material table (`mat_start + mat_count * 388`, shifted -8 bytes if motions > 0):
- `+0x00`: `16 bytes` Node name (e.g. `f_0000`)
- `+0x10`: `16 bytes` Parent node name (or `""` for root)
- `+0x20`: `16x f32` 4x4 Transformation matrix

### 2.4 Mesh Structure
Each mesh begins at `pos = node_start + node_count * 240 + 80`:
- `+0x00`: `16 bytes` Mesh name (e.g. `m_0000`)
- `+0x10`: **Submesh Table** (32 bytes per submesh):
  - `f0`: `u32` `triangle_count`
  - `f1`: `u32` `vertex_start`
  - `f2`: `u32` `vertex_count`
  - `f3..f5`: Reserved (0)
  - `f6`: `u32` `material_index`
  - `f7`: `u32` `cumulative_verts`
- **Vertex Stream Descriptor**:
  - `strip_count`: `u32`
  - `stride`: `u32` (36 or 40 bytes)
  - 16 bytes alignment padding
- **Vertex Buffer** (`total_verts * stride` bytes):
  - `stride = 36`: `Pos(3f), Norm(3f), Color(u32 ARGB), UV(2f)`
  - `stride = 40`: `Pos(3f), Norm(2f/3f), Color0(u32 ARGB), Color1(u32 ARGB), UV(2f), UV2(1f/2f)`
- **Index Buffer Header**:
  - `pad`: `u32` (0)
  - `total_indices`: `u32` (count of uint16 indices)
  - `buf_type`: `u32` (101)
  - `prim_type`: `u32` (2 = Triangles)
  - 16 bytes alignment padding
- **Index Buffer Data**:
  - `total_indices * 2` bytes (`uint16` triangle list).

---

## 3. Collision Geometry Architecture Across Engine Generations

Falcom used two distinct collision architectures across the Napishtim Engine family:
1. **Dedicated Collision Format (`.YCO`)** — *Ys: The Oath in Felghana* and *Ys VI: The Ark of Napishtim*.
2. **Low-Poly Companion Model Format (`Stage_.YMO`)** — *Ys Origin*.

---

### 3.1 `.YCO` Format (*Felghana* & *Ys VI*)

Collision geometry in Felghana and Ys VI is authored as separate binary files alongside stage and prop models:
- `__s.yco`: Standable / Walkable ground geometry (semi-transparent emerald green in viewer).
- `__w.yco`: Wall / Obstacle barrier geometry (semi-transparent coral orange in viewer).
- `__c.yco`: Camera bounding volume / occlusion frustum (semi-transparent cyan blue in viewer).

#### 3.1.1 `.YCO` Header (28 bytes)
| Offset | Type | Field | Description |
|---|---|---|---|
| `0x00` | `4 bytes` | `magic` | `"YCO\0"` (`0x004F4359`) |
| `0x04` | `u32` | `version` | Version (`1`) |
| `0x08` | `u32` | `polygon_count` | Exact count of 96-byte collision triangle records |
| `0x0C` | `u32` | `spatial_cell_count` | Number of spatial partition index entries |
| `0x10` | `u32` | `spatial_grid_dim` | Spatial partition grid dimension (e.g. `10`) |
| `0x14` | `f32` | `cell_size` | Grid cell world scale factor (e.g. `1.0f`) |
| `0x18` | `u32` | `spatial_depth_flags` | Spatial hierarchy depth & engine flags (e.g. `8`) |

#### 3.1.2 `.YCO` Polygon Record (96 bytes per triangle, starting at `0x1C`)
| Offset | Type | Field | Description |
|---|---|---|---|
| `+0x00` | `3x f32` | `v0` | Vertex 0 position `(x, y, z)` |
| `+0x0C` | `3x f32` | `v1` | Vertex 1 position `(x, y, z)` |
| `+0x18` | `3x f32` | `v2` | Vertex 2 position `(x, y, z)` |
| `+0x24` | `3x f32` | `normal` | Triangle plane unit normal `(nx, ny, nz)` |
| `+0x30` | `f32` | `plane_d` | Plane equation constant `D` where $\vec{N} \cdot \vec{P} + D = 0$ |
| `+0x34` | `4x f32` | `bounding_box_min` | Triangle AABB `(min_x, min_y, min_z, max_x)` |
| `+0x44` | `2x f32` | `bounding_box_max` | Triangle AABB `(max_y, max_z)` |
| `+0x48` | `u32` | `surface_flags` | Material type (wood, stone, water, ice, metal, slippery, fall-through) |
| `+0x4C` | `u32` | `collision_attributes`| Trigger ID, damage zone flag, sound FX ID |

#### 3.1.3 `.YCO` Spatial Acceleration Structure (Tail)
Immediately following the polygon array (`0x1C + polygon_count * 96`):
- `6x f32`: Overall collision volume AABB `(min_x, min_y, min_z, max_x, max_y, max_z)`.
- Array of `u32` spatial grid indices for $O(1)$ capsule sweep and raycast broadphase queries.

---

### 3.2 `Stage_.YMO` Companion Collision Format (*Ys Origin*)

In *Ys Origin*, Falcom deprecated the proprietary `.YCO` compiler and standardized collision authoring on standard low-poly companion `.YMO` models:
- Every stage has a visual mesh `S_XXXX.YMO` (high-poly, textured) and a collision mesh `S_XXXX_.YMO` (low-poly proxy, untextured).
- Map objects follow the same convention: `MAPOBJ/DOOR_07/DOOR_07.YMO` has `DOOR_07_.YMO`.
- **Materials:** Empty texture string (`tex=''`), `flags=0x00000000`, `alpha=1.0`.
- **Submeshes & Normal Partitioning:**
  - **Wall Submeshes:** Face normals are horizontal ($|N_y| \approx 0.0$), defining solid boundaries and obstacles.
  - **Walkable Submeshes:** Face normals point upward ($|N_y| > 0.45$), defining standable floors, stairs, and ramps.
- **Viewer Integration:** The viewer deduplicates `S_XXXX_.YMO` from the map browser, links it as `st.coll_mesh_path`, and renders it as a toggleable collision layer in the viewport.
---

## 4. Scene Object Placement Format (`.SOB`)

Stages use `.SOB` files to place the base map geometry along with props, doors, torches, and interactive objects into a composite world scene.

### 4.1 `.SOB` Header (16 bytes)
- `0x00`: `4 bytes` Magic `"SOB\0"`
- `0x04`: `u32` Version
- `0x08`: `u32` Entry size (`680` bytes)
- `0x0C`: `u32` Object count

### 4.2 `.SOB` Object Placement Record (680 bytes)
- `+0x00`: `string` Relative model path (e.g. `data\map\mapobj\s01dor30\s01dor30.ymo`)
- `+0x104`: `3x f32` Translation position `(px, py, pz)`
- `+0x110`: `3x f32` Euler rotation in radians `(rx, ry, rz)`
- `+0x11C`: `3x f32` Scaling factor `(sx, sy, sz)`
