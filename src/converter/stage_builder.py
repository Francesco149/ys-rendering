#!/usr/bin/env python3
"""
Falcom Ys Stage & Scene Assembler.
Parses .SOB (Scene Object Placement) files, loads base map geometry and placed props/doors/objects,
and combines them into a full composite 3D scene (glTF 2.0 / GLB / OBJ).
"""

import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Dict, Optional, Tuple, Any
import numpy as np

from src.converter.ymo_parser import YmoModel, YmoParser, YmoMesh, YmoSubmesh, YmoMaterial
from src.converter.gltf_exporter import GltfExporter
from src.converter.obj_exporter import ObjExporter

@dataclass
class PlacedObject:
    index: int
    ymo_path_str: str
    position: Tuple[float, float, float]
    rotation_euler: Tuple[float, float, float]  # (rx, ry, rz) in radians
    scale: Tuple[float, float, float]
    model: Optional[YmoModel] = None
    resolved_path: Optional[Path] = None

@dataclass
class StageScene:
    stage_name: str
    base_model_path: Optional[Path]
    base_model: Optional[YmoModel]
    placed_objects: List[PlacedObject]

class StageBuilder:
    @staticmethod
    def euler_to_matrix(pos: Tuple[float, float, float], rot: Tuple[float, float, float], scale: Tuple[float, float, float]) -> np.ndarray:
        px, py, pz = pos
        rx, ry, rz = rot
        sx, sy, sz = scale

        # Rotation matrices
        cx, sx_val = math.cos(rx), math.sin(rx)
        cy, sy_val = math.cos(ry), math.sin(ry)
        cz, sz_val = math.cos(rz), math.sin(rz)

        Rx = np.array([
            [1, 0, 0, 0],
            [0, cx, -sx_val, 0],
            [0, sx_val, cx, 0],
            [0, 0, 0, 1]
        ], dtype=np.float32)

        Ry = np.array([
            [cy, 0, sy_val, 0],
            [0, 1, 0, 0],
            [-sy_val, 0, cy, 0],
            [0, 0, 0, 1]
        ], dtype=np.float32)

        Rz = np.array([
            [cz, -sz_val, 0, 0],
            [sz_val, cz, 0, 0],
            [0, 0, 1, 0],
            [0, 0, 0, 1]
        ], dtype=np.float32)

        S = np.diag([sx, sy, sz, 1.0]).astype(np.float32)
        T = np.eye(4, dtype=np.float32)
        T[0, 3] = px
        T[1, 3] = py
        T[2, 3] = pz

        # In Falcom engine: T * Ry * Rx * Rz * S or T * R * S
        R = Ry @ Rx @ Rz
        return T @ R @ S

    @classmethod
    def parse_sob(cls, sob_path: Path, assets_root: Path) -> StageScene:
        sob_path = Path(sob_path)
        data = sob_path.read_bytes()
        if len(data) < 16:
            raise ValueError(f"SOB file too short: {len(data)} bytes")

        magic, ver, entry_size, count = struct.unpack("<4I", data[:16])
        if data[:3] != b"SOB":
            raise ValueError(f"Invalid SOB magic: {data[:4]}")

        stage_name = sob_path.stem
        placed_objects: List[PlacedObject] = []
        base_model_path: Optional[Path] = None
        base_model: Optional[YmoModel] = None

        for i in range(count):
            entry_offset = 16 + i * entry_size
            chunk = data[entry_offset:entry_offset + entry_size]
            if len(chunk) < 0x128:
                break

            null_pos = chunk.find(b"\x00")
            ymo_rel = chunk[:null_pos if null_pos != -1 else 256].decode("ascii", errors="replace").strip()

            px, py, pz = struct.unpack_from("<3f", chunk, 0x104)
            rx, ry, rz = struct.unpack_from("<3f", chunk, 0x110)
            sx, sy, sz = struct.unpack_from("<3f", chunk, 0x11C)

            # Resolve model file
            norm_rel = ymo_rel.replace("\\", "/").lstrip("/")
            if norm_rel.lower().startswith("data/"):
                norm_rel = norm_rel[5:]

            cand_paths = [
                assets_root / norm_rel,
                assets_root / norm_rel.upper(),
                assets_root / "MAP" / norm_rel,
                assets_root / "MAP" / norm_rel.upper(),
                sob_path.parent / Path(norm_rel).name,
            ]
            resolved_p = None
            for cp in cand_paths:
                if cp.exists():
                    resolved_p = cp
                    break

            obj_model = None
            if resolved_p and resolved_p.exists():
                try:
                    obj_model = YmoParser.parse_file(resolved_p)
                except Exception as ex:
                    print(f"Warning: failed to parse {resolved_p}: {ex}")

            po = PlacedObject(
                index=i,
                ymo_path_str=ymo_rel,
                position=(px, py, pz),
                rotation_euler=(rx, ry, rz),
                scale=(sx if sx > 0 else 1.0, sy if sy > 0 else 1.0, sz if sz > 0 else 1.0),
                model=obj_model,
                resolved_path=resolved_p
            )

            # Check if this is the base stage model (usually object 0 at 0,0,0)
            if i == 0 or Path(ymo_rel).stem.upper() == stage_name.upper():
                base_model_path = resolved_p
                base_model = obj_model

            placed_objects.append(po)

        return StageScene(
            stage_name=stage_name,
            base_model_path=base_model_path,
            base_model=base_model,
            placed_objects=placed_objects
        )

    @classmethod
    def export_composite_glb(cls, scene: StageScene, assets_root: Path, output_path: Path, include_collision: bool = False) -> Path:
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        # Merge all placed objects into a unified YmoModel with transformed vertices
        merged_materials: List[YmoMaterial] = []
        mat_tex_to_merged_idx: Dict[str, int] = {}

        merged_positions = []
        merged_normals = []
        merged_colors = []
        merged_uvs = []
        merged_triangles = []
        merged_submeshes: List[YmoSubmesh] = []
        submesh_triangles_list: List[np.ndarray] = []

        total_vert_offset = 0

        for obj in scene.placed_objects:
            if not obj.model:
                continue

            matrix = cls.euler_to_matrix(obj.position, obj.rotation_euler, obj.scale)
            # Map object materials to merged materials
            obj_mat_to_merged: Dict[int, int] = {}
            for mat in obj.model.materials:
                key = mat.texture_name.lower()
                if key in mat_tex_to_merged_idx:
                    obj_mat_to_merged[mat.index] = mat_tex_to_merged_idx[key]
                else:
                    new_idx = len(merged_materials)
                    merged_materials.append(YmoMaterial(
                        index=new_idx,
                        flags=mat.flags,
                        alpha=mat.alpha,
                        texture_path=mat.texture_path,
                        texture_name=mat.texture_name,
                        raw_data=mat.raw_data
                    ))
                    mat_tex_to_merged_idx[key] = new_idx
                    obj_mat_to_merged[mat.index] = new_idx

            # Transform and append mesh geometry
            for mesh in obj.model.meshes:
                if len(mesh.positions) == 0:
                    continue

                # Transform positions
                homo_pos = np.hstack([mesh.positions, np.ones((len(mesh.positions), 1), dtype=np.float32)])
                trans_pos = (homo_pos @ matrix.T)[:, :3]

                # Transform normals
                rot_mat = matrix[:3, :3]
                inv_rot = np.linalg.inv(rot_mat).T
                trans_norm = (mesh.normals @ inv_rot.T)
                norm_lengths = np.linalg.norm(trans_norm, axis=1, keepdims=True)
                norm_lengths[norm_lengths == 0] = 1.0
                trans_norm = trans_norm / norm_lengths

                merged_positions.append(trans_pos)
                merged_normals.append(trans_norm)
                merged_colors.append(mesh.colors)
                merged_uvs.append(mesh.uvs)

                for s_idx, sm in enumerate(mesh.submeshes):
                    merged_mat_idx = obj_mat_to_merged.get(sm.material_index, 0)
                    sm_tris = mesh.submesh_triangles[s_idx] if s_idx < len(mesh.submesh_triangles) else mesh.indices
                    if len(sm_tris) == 0:
                        continue

                    offset_tris = sm_tris + total_vert_offset
                    submesh_triangles_list.append(offset_tris)
                    merged_triangles.append(offset_tris)

                    merged_submeshes.append(YmoSubmesh(
                        triangle_count=len(offset_tris),
                        vertex_start=sm.vertex_start + total_vert_offset,
                        vertex_count=sm.vertex_count,
                        material_index=merged_mat_idx,
                        cumulative_verts_end=total_vert_offset + len(mesh.positions)
                    ))

                total_vert_offset += len(mesh.positions)

        if not merged_positions:
            raise ValueError(f"No geometry found in stage scene {scene.stage_name}")

        final_pos = np.vstack(merged_positions)
        final_norm = np.vstack(merged_normals)
        final_col = np.vstack(merged_colors)
        final_uvs = np.vstack(merged_uvs)
        final_tris = np.vstack(merged_triangles) if merged_triangles else np.zeros((0, 3), dtype=np.uint32)

        composite_mesh = YmoMesh(
            name=scene.stage_name,
            submeshes=merged_submeshes,
            vertex_stride=36,
            positions=final_pos,
            normals=final_norm,
            colors=final_col,
            uvs=final_uvs,
            indices=final_tris,
            submesh_triangles=submesh_triangles_list
        )

        composite_model = YmoModel(
            filename=f"{scene.stage_name}_composite.ymo",
            version=9,
            materials=merged_materials,
            nodes=[],
            meshes=[composite_mesh],
            collision_files=[]
        )

        # Export using GLTF exporter
        ref_path = scene.base_model_path if scene.base_model_path else assets_root / f"MAP/{scene.stage_name}/{scene.stage_name}.YMO"
        GltfExporter.export_glb(composite_model, ref_path, output_path, include_collision=include_collision)
        print(f"Exported composite stage scene {scene.stage_name} to {output_path} ({len(final_pos)} vertices, {len(final_tris)} triangles)")
        return output_path

if __name__ == "__main__":
    import sys
    sob_p = Path(sys.argv[1])
    root = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("extracted")
    out_p = Path(sys.argv[3]) if len(sys.argv) > 3 else Path(f"output/{sob_p.stem}_composite.glb")
    scene = StageBuilder.parse_sob(sob_p, root)
    print(f"Loaded SOB {sob_p.name} with {len(scene.placed_objects)} objects:")
    for obj in scene.placed_objects:
        print(f"  [{obj.index}] {obj.ymo_path_str} @ pos={obj.position} rot={obj.rotation_euler} -> {obj.resolved_path}")
    StageBuilder.export_composite_glb(scene, root, out_p)
