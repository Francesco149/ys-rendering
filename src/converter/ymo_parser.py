#!/usr/bin/env python3
"""
Falcom YMO (Ys Model Object) Binary Format Parser.
Extracts geometry, submeshes, materials, vertex buffers, and index buffers.
Supports single-stream, multi-stream, and multi-mesh files across all Falcom Napishtim engine games.
"""

import math
import struct
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Dict, Tuple, Optional, Any
import numpy as np

@dataclass
class YmoMaterial:
    index: int
    flags: int
    alpha: float
    texture_path: str
    texture_name: str
    raw_data: bytes

@dataclass
class YmoNode:
    name: str
    parent_name: str
    matrix: np.ndarray

@dataclass
class YmoSubmesh:
    triangle_count: int
    vertex_start: int
    vertex_count: int
    material_index: int
    cumulative_verts_end: int

@dataclass
class YmoMesh:
    name: str
    submeshes: List[YmoSubmesh]
    vertex_stride: int
    positions: np.ndarray       # Nx3 float32
    normals: np.ndarray         # Nx3 float32
    colors: np.ndarray          # Nx4 uint8 (RGBA)
    uvs: np.ndarray             # Nx2 float32
    indices: np.ndarray         # Mx3 uint32 (triangles)
    submesh_triangles: List[np.ndarray] = field(default_factory=list)

@dataclass
class YmoModel:
    filename: str
    version: int
    materials: List[YmoMaterial]
    nodes: List[YmoNode]
    meshes: List[YmoMesh]
    collision_files: List[str]

class YmoParser:
    MATERIAL_RECORD_SIZE = 388

    @classmethod
    def parse_file(cls, path: Path) -> YmoModel:
        data = Path(path).read_bytes()
        return cls.parse_bytes(data, filename=path.name)

    @classmethod
    def parse_bytes(cls, data: bytes, filename: str = "model.ymo") -> YmoModel:
        if len(data) < 68:
            raise ValueError(f"YMO file too short: {len(data)} bytes")

        magic = data[:4]
        if magic != b"YMO\x00":
            raise ValueError(f"Invalid YMO magic: {magic}")

        hdr = struct.unpack("<12I", data[:48])
        version = hdr[1]
        f1 = hdr[2]
        is_v17 = (version == 17 or f1 == 17 or version == 0x0004FD59)
        mat_count = hdr[4] if is_v17 else hdr[5]
        motion_count = hdr[5] if is_v17 else hdr[6]
        mesh_count = hdr[6] if is_v17 else hdr[7]
        node_count = hdr[7] if is_v17 else hdr[8]
        # Collision files (3 x 80 byte fixed strings at 0x0078 or 0x004C)
        collision_files = []
        pos = 0x004C if is_v17 else 0x0078
        for _ in range(3):
            if pos + 80 <= len(data):
                raw_s = data[pos:pos+80]
                null_pos = raw_s.find(b"\x00")
                if null_pos != -1:
                    raw_s = raw_s[:null_pos]
                s = raw_s.decode("ascii", errors="replace").strip()
                if s:
                    collision_files.append(s)
                pos = pos + 0xC0 if is_v17 else pos + 80

        # Materials start:
        mat_start = 0x02AC if is_v17 else ((0x0170 + motion_count * 68) if motion_count > 0 else 0x0178)
        mat_rec_size = 748 if is_v17 else cls.MATERIAL_RECORD_SIZE

        materials: List[YmoMaterial] = []
        for i in range(mat_count):
            m_pos = mat_start + i * mat_rec_size
            if m_pos + mat_rec_size > len(data):
                break
            mat_raw = data[m_pos:m_pos + mat_rec_size]
            flags, alpha = struct.unpack("<If", mat_raw[:8])
            tex_str_raw = mat_raw[0x8C:]
            null_pos = tex_str_raw.find(b"\x00")
            if null_pos != -1:
                tex_str_raw = tex_str_raw[:null_pos]
            tex_path = tex_str_raw.decode("ascii", errors="replace").strip()
            tex_name = Path(tex_path.replace("\\", "/")).name if tex_path else ""

            materials.append(YmoMaterial(
                index=i,
                flags=flags,
                alpha=alpha if alpha > 0.001 else 1.0,
                texture_path=tex_path,
                texture_name=tex_name,
                raw_data=mat_raw
            ))

        # Nodes
        node_start = mat_start + mat_count * mat_rec_size
        if motion_count > 0:
            node_start -= 8

        nodes: List[YmoNode] = []
        pos = node_start

        node_start = mat_start + mat_count * mat_rec_size
        if motion_count > 0 and not is_v17:
            node_start -= 8
        node_rec_size = 576 if is_v17 else 240

        nodes: List[YmoNode] = []
        pos = node_start

        for _ in range(node_count):
            if pos + node_rec_size > len(data):
                break
            node_name_raw = data[pos:pos+16]
            null_pos = node_name_raw.find(b"\x00")
            node_name = node_name_raw[:null_pos if null_pos != -1 else 16].decode("ascii", errors="replace")
            
            parent_raw = data[pos+16:pos+32]
            null_pos = parent_raw.find(b"\x00")
            parent_name = parent_raw[:null_pos if null_pos != -1 else 16].decode("ascii", errors="replace")

            mat_vals = struct.unpack("<16f", data[pos+32:pos+96])
            matrix = np.array(mat_vals, dtype=np.float32).reshape((4, 4))

            nodes.append(YmoNode(name=node_name, parent_name=parent_name, matrix=matrix))
            pos += node_rec_size

        first_mesh_start = pos if is_v17 else (node_start + node_count * 240 + 80)
        mesh_offsets = []
        for m in re.finditer(rb"m_[0-9a-zA-Z_]{4}\x00", data):
            if m.start() >= first_mesh_start:
                mesh_offsets.append(m.start())

        if not mesh_offsets:
            for p in range(first_mesh_start, len(data) - 48, 4):
                v_cnt, st = struct.unpack_from("<2I", data, p)
                if st in (36, 40, 48) and 10 <= v_cnt <= 200000:
                    mesh_offsets.append(p)
                    break

        if not mesh_offsets and first_mesh_start < len(data) - 32:
            mesh_offsets = [first_mesh_start]

        meshes: List[YmoMesh] = []
        for m_idx, m_offset in enumerate(mesh_offsets):
            pos = m_offset
            mesh_name_raw = data[pos:pos+16]
            null_pos = mesh_name_raw.find(b"\x00")
            mesh_name = mesh_name_raw[:null_pos if null_pos != -1 else 16].decode("ascii", errors="replace")
            if not mesh_name or not mesh_name.startswith("m_"):
                mesh_name = f"m_{m_idx:04d}"
            if not is_v17:
                pos += 16

            # Submesh table
            submeshes: List[YmoSubmesh] = []
            if is_v17:
                total_verts = 0
                stride = 48
                v_start = pos
                desc_pos = 0
                for p in range(0x02AC, len(data) - 48, 4):
                    v_cnt, st, v_type = struct.unpack_from("<3I", data, p)
                    if st in (36, 40, 48) and 10 <= v_cnt <= 200000 and (v_type in (722, 466, 0x02D2)):
                        cand = p + 12
                        if cand + st <= len(data):
                            pf = struct.unpack("<3f", data[cand:cand+12])
                            nf = struct.unpack("<3f", data[cand+12:cand+24])
                            n_sq = nf[0]**2 + nf[1]**2 + nf[2]**2
                            if abs(n_sq - 1.0) < 0.35 and abs(pf[0]) < 10000 and abs(pf[1]) < 10000 and abs(pf[2]) < 10000:
                                desc_pos = p
                                v_start = cand
                                total_verts = v_cnt
                                stride = st
                                break

                # Scan for v17 submesh table before desc_pos
                if desc_pos > 0 and mat_count > 0:
                    sm_search_min = max(0, desc_pos - 32 * mat_count - 512)
                    for p in range(sm_search_min, desc_pos, 4):
                        curr_tri = 0
                        recs = []
                        for si in range(mat_count):
                            r_pos = p + si * 32
                            if r_pos + 16 > len(data): break
                            w0, w1, w2, w3 = struct.unpack_from("<4I", data, r_pos)
                            if w1 == si and w2 == curr_tri and w3 < 200000:
                                recs.append(YmoSubmesh(
                                    triangle_count=w3,
                                    vertex_start=0,
                                    vertex_count=total_verts,
                                    material_index=si,
                                    cumulative_verts_end=total_verts
                                ))
                                curr_tri += w3
                            else:
                                break
                        if len(recs) == mat_count and curr_tri > 0:
                            submeshes = recs
                            break
            else:
                while pos + 32 <= len(data):
                    f0, f1, f2, f3, f4, f5, f6, f7 = struct.unpack_from("<8I", data, pos)
                    if f0 == 0:
                        pos += 32
                        break
                    if f1 in (36, 40) and f2 == 0:
                        pos += 32
                        break

                    if mat_count > 0:
                        resolved_mat_idx = (f6 - 1) % mat_count if f6 > 0 else (mat_count - 1)
                    else:
                        resolved_mat_idx = 0

                    submeshes.append(YmoSubmesh(
                        triangle_count=f0,
                        vertex_start=f1,
                        vertex_count=f2,
                        material_index=resolved_mat_idx,
                        cumulative_verts_end=f1 + f2
                    ))
                    pos += 32
                    if len(submeshes) >= mat_count and mat_count > 0:
                        break

                total_verts = max((sm.vertex_start + sm.vertex_count for sm in submeshes), default=0)
                if total_verts <= 0 or total_verts > 200000:
                    continue
                desc_offsets = [p for p in range(pos - 32, min(pos + 128, len(data) - 12), 4) if struct.unpack_from("<I", data, p)[0] == total_verts]
                if not desc_offsets:
                    desc_offsets = [pos]
                last_desc = desc_offsets[-1]
                first_desc = desc_offsets[0]
                st = struct.unpack_from("<3I", data, first_desc)[2]
                stride = st if st in (36, 40) else (40 if total_verts > 100 else 36)
                v_start = last_desc + 28
                for c_cand in (last_desc + 28, last_desc + 32, last_desc + 24):
                    if c_cand + stride <= len(data):
                        px, py, pz = struct.unpack_from("<3f", data, c_cand)
                        if not (math.isnan(px) or math.isnan(py) or math.isnan(pz)) and abs(px) < 10000 and abs(py) < 10000 and abs(pz) < 10000:
                            nx, ny = struct.unpack_from("<2f", data, c_cand + 12)
                            if abs(nx) <= 1.05 and abs(ny) <= 1.05:
                                v_start = c_cand
                                break

            pos = v_start
            v_bytes = data[pos:pos + total_verts * stride]
            pos += total_verts * stride
            positions = np.zeros((total_verts, 3), dtype=np.float32)
            normals = np.zeros((total_verts, 3), dtype=np.float32)
            colors = np.ones((total_verts, 4), dtype=np.uint8) * 255
            uvs = np.zeros((total_verts, 2), dtype=np.float32)

            for vi in range(total_verts):
                v_off = vi * stride
                if v_off + stride > len(v_bytes):
                    break
                if stride == 36:
                    px, py, pz, nx, ny, nz, col, u, v = struct.unpack_from("<6fI2f", v_bytes, v_off)
                elif stride == 48:
                    px, py, pz, nx, ny, nz = struct.unpack_from("<6f", v_bytes, v_off)
                    col = struct.unpack_from("<I", v_bytes, v_off + 24)[0]
                    u, v = struct.unpack_from("<2f", v_bytes, v_off + 32)
                else: # 40 bytes
                    px, py, pz = struct.unpack_from("<3f", v_bytes, v_off)
                    w3_f, w4_f, w5_f = struct.unpack_from("<3f", v_bytes, v_off + 12)
                    w5_u, w6_u, w7_u = struct.unpack_from("<3I", v_bytes, v_off + 20)
                    w7_f, w8_f, w9_f = struct.unpack_from("<3f", v_bytes, v_off + 28)

                    if math.isnan(w5_f) or abs(w5_f) > 10.0 or (w5_u >> 24) == 0xFF:
                        nx = w3_f
                        ny = w4_f
                        nz_sq = max(0.0, 1.0 - nx*nx - ny*ny)
                        nz = math.sqrt(nz_sq)
                        col = w5_u
                        u = w7_f
                        v = w8_f
                    else:
                        nx = w3_f
                        ny = w4_f
                        nz = w5_f
                        col = w6_u
                        u = w8_f
                        v = w9_f

                # Sanitize any NaNs in coordinates
                px = 0.0 if math.isnan(px) else px
                py = 0.0 if math.isnan(py) else py
                pz = 0.0 if math.isnan(pz) else pz
                nx = 0.0 if math.isnan(nx) else nx
                ny = 0.0 if math.isnan(ny) else ny
                nz = 1.0 if math.isnan(nz) else nz
                u = 0.0 if math.isnan(u) else u
                v = 0.0 if math.isnan(v) else v

                positions[vi] = [px, py, pz]
                normals[vi] = [nx, ny, nz]
                b = col & 0xFF
                g = (col >> 8) & 0xFF
                r = (col >> 16) & 0xFF
                a = (col >> 24) & 0xFF
                colors[vi] = [r, g, b, a]
                uvs[vi] = [u, v]

            # Find Index buffer header
            total_indices = 0
            if is_v17:
                for p in range(pos, min(pos + 64, len(data) - 16), 4):
                    ti = struct.unpack_from("<I", data, p)[0]
                    if 0 < ti < 300000 and p + 12 + ti * 2 <= len(data):
                        test_idx = struct.unpack_from("<2H", data, p + 12)
                        if test_idx[0] < total_verts and test_idx[1] < total_verts:
                            total_indices = ti
                            pos = p + 12
                            break
            else:
                scan_start = max(0, pos - 1024)
                for p in range(scan_start, len(data) - 16, 4):
                    # Form B
                    ti_b, b_type_b, p_type_b = struct.unpack_from("<3I", data, p)
                    if b_type_b == 101 and p_type_b in (1, 2) and 0 < ti_b < 300000 and p + 12 + ti_b * 2 <= len(data):
                        total_indices = ti_b
                        pos = p + 12
                        break
                    # Form A
                    if p + 32 <= len(data):
                        pad, ti_a, b_type_a, p_type_a = struct.unpack_from("<4I", data, p)
                        if b_type_a == 101 and p_type_a in (1, 2) and 0 < ti_a < 300000 and p + 32 + ti_a * 2 <= len(data):
                            total_indices = ti_a
                            pos = p + 32
                            break
            idx_bytes = data[pos:pos + total_indices * 2]
            pos += total_indices * 2
            raw_indices = np.frombuffer(idx_bytes, dtype=np.uint16).astype(np.uint32)

            tri_count = len(raw_indices) // 3
            all_triangles = raw_indices[:tri_count * 3].reshape((tri_count, 3))

            submesh_triangles = []
            curr_tri_idx = 0
            for sm in submeshes:
                sm_tris = all_triangles[curr_tri_idx:curr_tri_idx + sm.triangle_count]
                submesh_triangles.append(sm_tris)
                curr_tri_idx += sm.triangle_count

            if not submeshes and len(all_triangles) > 0:
                submeshes.append(YmoSubmesh(
                    triangle_count=len(all_triangles),
                    vertex_start=0,
                    vertex_count=total_verts,
                    material_index=0,
                    cumulative_verts_end=total_verts
                ))
                submesh_triangles.append(all_triangles)

            meshes.append(YmoMesh(
                name=mesh_name,
                submeshes=submeshes,
                vertex_stride=stride,
                positions=positions,
                normals=normals,
                colors=colors,
                uvs=uvs,
                indices=all_triangles,
                submesh_triangles=submesh_triangles
            ))

        return YmoModel(
            filename=filename,
            version=version,
            materials=materials,
            nodes=nodes,
            meshes=meshes,
            collision_files=collision_files
        )

if __name__ == "__main__":
    import sys
    model = YmoParser.parse_file(Path(sys.argv[1]))
    print(f"Loaded {model.filename}:")
    print(f"  Materials ({len(model.materials)}):")
    for m in model.materials:
        print(f"    [{m.index}] tex='{m.texture_name}' alpha={m.alpha}")
    print(f"  Nodes ({len(model.nodes)}): {[n.name for n in model.nodes]}")
    print(f"  Meshes ({len(model.meshes)}):")
    for m in model.meshes:
        print(f"    '{m.name}': {len(m.positions)} vertices, {len(m.indices)} triangles across {len(m.submeshes)} submeshes")
        for sm in m.submeshes:
            m_name = model.materials[sm.material_index].texture_name if sm.material_index < len(model.materials) else "OUT_OF_BOUNDS"
            print(f"      Submesh: tris={sm.triangle_count:4d}, v_start={sm.vertex_start:5d}, v_count={sm.vertex_count:5d} -> Mat[{sm.material_index:2d}] ({m_name})")
