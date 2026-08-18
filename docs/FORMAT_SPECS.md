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

## 3. Collision Geometry Format (`.YCO`)

Collision meshes provide ground stepping, wall collision, and camera volume boundaries:
- `__s.yco`: Standable / Walkable ground geometry
- `__w.yco`: Wall / Obstacle barrier geometry
- `__c.yco`: Camera bounding volume / occlusion frustum

### 3.1 `.YCO` Polygon Record (96 bytes per triangle)
- `+0x00`: `v0` (x, y, z floats - 12 bytes)
- `+0x0C`: `v1` (x, y, z floats - 12 bytes)
- `+0x18`: `v2` (x, y, z floats - 12 bytes)
- `+0x24`: `normal` (nx, ny, nz floats - 12 bytes)
- `+0x30`: `plane_d` (float - 4 bytes)
- `+0x34`: `bounding_box` (min_x, min_y, min_z, max_x floats - 16 bytes)
- `+0x48`: `surface_flags` (uint32)
- `+0x4C`: `collision_attributes` (uint32)

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
