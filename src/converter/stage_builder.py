#!/usr/bin/env python3
"""
Falcom Ys Stage & Scene Assembler.
Parses .SOB (Scene Object Placement) files, loads base map geometry and placed props/doors/objects,
and combines them into a modular composite 3D scene (glTF 2.0 / GLB) with separate nodes for every prop, trigger, and terrain.
"""

import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Dict, Optional, Tuple, Any
import numpy as np
from PIL import Image

import pygltflib
from pygltflib import (
    GLTF2,
    Scene,
    Node,
    Mesh,
    Primitive,
    Attributes,
    Buffer,
    BufferView,
    Accessor,
    Material,
    PbrMetallicRoughness,
    Texture,
    TextureInfo,
    Image as GltfImage,
    Sampler,
)

from src.converter.ymo_parser import YmoModel, YmoParser, YmoMesh, YmoSubmesh, YmoMaterial
from src.converter.yco_parser import YcoCollisionMesh, YcoParser
from src.converter.gltf_exporter import GltfExporter

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
    TRIGGER_KEYWORDS = ("2mdoor", "3x3", "tofu", "check", "trap", "portal", "trigger")

    @classmethod
    def is_trigger_object(cls, ymo_path_str: str, model: Optional[YmoModel]) -> bool:
        stem = Path(ymo_path_str.replace("\\", "/")).stem.lower()
        if any(k in stem for k in cls.TRIGGER_KEYWORDS):
            return True
        if model:
            # Check if all materials have no texture
            if all(not m.texture_name for m in model.materials):
                return True
        return False

    @staticmethod
    def euler_to_matrix(pos: Tuple[float, float, float], rot: Tuple[float, float, float], scale: Tuple[float, float, float]) -> np.ndarray:
        px, py, pz = pos
        rx, ry, rz = rot
        sx, sy, sz = scale

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
    def export_composite_glb(
        cls,
        scene: StageScene,
        assets_root: Path,
        output_path: Path,
        include_collision: bool = False
    ) -> Path:
        output_path = Path(output_path)
        output_path.parent.mkdir(parents=True, exist_ok=True)

        gltf = GLTF2(
            scenes=[Scene(nodes=[0])],
            scene=0,
            nodes=[],
            meshes=[],
            materials=[],
            textures=[],
            images=[],
            samplers=[Sampler(magFilter=pygltflib.LINEAR, minFilter=pygltflib.LINEAR_MIPMAP_LINEAR)],
            accessors=[],
            bufferViews=[],
            buffers=[Buffer(byteLength=0)],
        )

        binary_blob = bytearray()
        mat_cache: Dict[str, int] = {}
        tex_cache: Dict[str, int] = {}

        def get_or_create_material(ymo_mat: YmoMaterial, ref_model_path: Path, is_trigger: bool = False) -> int:
            if is_trigger:
                mat_key = "Mat_Trigger_Portal"
                if mat_key in mat_cache:
                    return mat_cache[mat_key]
                gltf_mat_idx = len(gltf.materials)
                gltf.materials.append(Material(
                    name="Mat_Trigger_Portal",
                    pbrMetallicRoughness=PbrMetallicRoughness(
                        baseColorFactor=[0.2, 0.6, 1.0, 0.25],
                        metallicFactor=0.0,
                        roughnessFactor=0.9
                    ),
                    alphaMode="BLEND",
                    doubleSided=True
                ))
                mat_cache[mat_key] = gltf_mat_idx
                return gltf_mat_idx

            tex_p = GltfExporter.resolve_texture(ref_model_path, ymo_mat.texture_name)
            tex_key = str(tex_p.resolve()) if (tex_p and tex_p.exists()) else f"no_tex_{ymo_mat.index}"
            mat_key = f"{tex_key}_{ymo_mat.flags}_{ymo_mat.alpha:.3f}"

            if mat_key in mat_cache:
                return mat_cache[mat_key]

            gltf_tex_idx = None
            if tex_p and tex_p.exists():
                if tex_key in tex_cache:
                    gltf_tex_idx = tex_cache[tex_key]
                else:
                    is_add = bool(ymo_mat.texture_name and ymo_mat.texture_name.upper().startswith("Z_"))
                    png_data = GltfExporter.dds_to_png_bytes(tex_p, is_additive=is_add)
                    while len(binary_blob) % 4 != 0:
                        binary_blob.append(0)

                    img_bv_idx = len(gltf.bufferViews)
                    img_offset = len(binary_blob)
                    img_length = len(png_data)
                    binary_blob.extend(png_data)

                    gltf.bufferViews.append(BufferView(
                        buffer=0,
                        byteOffset=img_offset,
                        byteLength=img_length,
                    ))

                    img_idx = len(gltf.images)
                    gltf.images.append(GltfImage(
                        bufferView=img_bv_idx,
                        mimeType="image/png",
                        name=tex_p.stem
                    ))

                    gltf_tex_idx = len(gltf.textures)
                    gltf.textures.append(Texture(
                        sampler=0,
                        source=img_idx,
                        name=tex_p.stem
                    ))
                    tex_cache[tex_key] = gltf_tex_idx

            pbr = PbrMetallicRoughness(
                metallicFactor=0.0,
                roughnessFactor=0.85,
            )
            if gltf_tex_idx is not None:
                pbr.baseColorTexture = TextureInfo(index=gltf_tex_idx)

            has_texture_alpha = False
            if tex_p and tex_p.exists():
                try:
                    with Image.open(tex_p) as chk_im:
                        if chk_im.mode in ("RGBA", "LA") or "transparency" in chk_im.info:
                            chk_arr = np.array(chk_im.convert("RGBA"))
                            if (chk_arr[:, :, 3] < 250).any():
                                has_texture_alpha = True
                            del chk_arr
                except Exception:
                    pass

            alpha_mode = "OPAQUE"
            alpha_cutoff = None
            if ymo_mat.alpha < 0.99 or (ymo_mat.texture_name and ymo_mat.texture_name.upper().startswith("Z_")):
                alpha_mode = "BLEND"
            elif has_texture_alpha:
                alpha_mode = "MASK"
                alpha_cutoff = 0.5

            gltf_mat_idx = len(gltf.materials)
            gltf.materials.append(Material(
                name=f"Mat_{ymo_mat.texture_name or 'default'}",
                pbrMetallicRoughness=pbr,
                alphaMode=alpha_mode,
                alphaCutoff=alpha_cutoff,
                doubleSided=True
            ))
            mat_cache[mat_key] = gltf_mat_idx
            return gltf_mat_idx

        # Build each placed object as a discrete Node & Mesh
        stage_children_nodes = []
        total_verts_count = 0
        total_tris_count = 0

        for obj in scene.placed_objects:
            if not obj.model or not obj.model.meshes:
                continue

            ref_p = obj.resolved_path if obj.resolved_path else Path(obj.ymo_path_str)
            obj_stem = Path(obj.ymo_path_str.replace("\\", "/")).stem
            is_base = (obj.index == 0 or obj_stem.upper() == scene.stage_name.upper())
            is_trigger = cls.is_trigger_object(obj.ymo_path_str, obj.model)

            if is_base:
                node_name = "Terrain"
            elif is_trigger:
                node_name = f"Trigger_{obj.index:02d}_{obj_stem}"
            else:
                node_name = f"Prop_{obj.index:02d}_{obj_stem}"

            # Convert each mesh in the object
            obj_mesh_primitives = []
            for mesh in obj.model.meshes:
                num_verts = len(mesh.positions)
                if num_verts == 0:
                    continue

                total_verts_count += num_verts
                total_tris_count += len(mesh.indices)

                # Positions
                while len(binary_blob) % 4 != 0:
                    binary_blob.append(0)
                pos_offset = len(binary_blob)
                pos_data = mesh.positions.astype(np.float32).tobytes()
                binary_blob.extend(pos_data)
                pos_bv_idx = len(gltf.bufferViews)
                gltf.bufferViews.append(BufferView(
                    buffer=0,
                    byteOffset=pos_offset,
                    byteLength=len(pos_data),
                    target=pygltflib.ARRAY_BUFFER
                ))
                pos_acc_idx = len(gltf.accessors)
                gltf.accessors.append(Accessor(
                    bufferView=pos_bv_idx,
                    byteOffset=0,
                    componentType=pygltflib.FLOAT,
                    count=num_verts,
                    type=pygltflib.VEC3,
                    min=mesh.positions.min(axis=0).tolist(),
                    max=mesh.positions.max(axis=0).tolist(),
                ))

                # Normals
                while len(binary_blob) % 4 != 0:
                    binary_blob.append(0)
                norm_offset = len(binary_blob)
                norm_data = mesh.normals.astype(np.float32).tobytes()
                binary_blob.extend(norm_data)
                norm_bv_idx = len(gltf.bufferViews)
                gltf.bufferViews.append(BufferView(
                    buffer=0,
                    byteOffset=norm_offset,
                    byteLength=len(norm_data),
                    target=pygltflib.ARRAY_BUFFER
                ))
                norm_acc_idx = len(gltf.accessors)
                gltf.accessors.append(Accessor(
                    bufferView=norm_bv_idx,
                    byteOffset=0,
                    componentType=pygltflib.FLOAT,
                    count=num_verts,
                    type=pygltflib.VEC3,
                ))

                # UVs
                uvs_gltf = mesh.uvs.copy()
                while len(binary_blob) % 4 != 0:
                    binary_blob.append(0)
                uv_offset = len(binary_blob)
                uv_data = uvs_gltf.astype(np.float32).tobytes()
                binary_blob.extend(uv_data)
                uv_bv_idx = len(gltf.bufferViews)
                gltf.bufferViews.append(BufferView(
                    buffer=0,
                    byteOffset=uv_offset,
                    byteLength=len(uv_data),
                    target=pygltflib.ARRAY_BUFFER
                ))
                uv_acc_idx = len(gltf.accessors)
                gltf.accessors.append(Accessor(
                    bufferView=uv_bv_idx,
                    byteOffset=0,
                    componentType=pygltflib.FLOAT,
                    count=num_verts,
                    type=pygltflib.VEC2,
                ))

                # Colors
                while len(binary_blob) % 4 != 0:
                    binary_blob.append(0)
                col_offset = len(binary_blob)
                col_data = mesh.colors.astype(np.uint8).tobytes()
                binary_blob.extend(col_data)
                col_bv_idx = len(gltf.bufferViews)
                gltf.bufferViews.append(BufferView(
                    buffer=0,
                    byteOffset=col_offset,
                    byteLength=len(col_data),
                    target=pygltflib.ARRAY_BUFFER
                ))
                col_acc_idx = len(gltf.accessors)
                gltf.accessors.append(Accessor(
                    bufferView=col_bv_idx,
                    byteOffset=0,
                    componentType=pygltflib.UNSIGNED_BYTE,
                    normalized=True,
                    count=num_verts,
                    type=pygltflib.VEC4,
                ))

                attrs = Attributes(
                    POSITION=pos_acc_idx,
                    NORMAL=norm_acc_idx,
                    TEXCOORD_0=uv_acc_idx,
                    COLOR_0=col_acc_idx,
                )

                for s_idx, sm in enumerate(mesh.submeshes):
                    sm_tris = mesh.submesh_triangles[s_idx] if s_idx < len(mesh.submesh_triangles) else mesh.indices
                    if len(sm_tris) == 0:
                        continue

                    idx_flat = sm_tris.flatten().astype(np.uint32)
                    while len(binary_blob) % 4 != 0:
                        binary_blob.append(0)
                    idx_offset = len(binary_blob)
                    idx_data = idx_flat.tobytes()
                    binary_blob.extend(idx_data)

                    idx_bv_idx = len(gltf.bufferViews)
                    gltf.bufferViews.append(BufferView(
                        buffer=0,
                        byteOffset=idx_offset,
                        byteLength=len(idx_data),
                        target=pygltflib.ELEMENT_ARRAY_BUFFER
                    ))

                    idx_acc_idx = len(gltf.accessors)
                    gltf.accessors.append(Accessor(
                        bufferView=idx_bv_idx,
                        byteOffset=0,
                        componentType=pygltflib.UNSIGNED_INT,
                        count=len(idx_flat),
                        type=pygltflib.SCALAR,
                        min=[int(idx_flat.min())],
                        max=[int(idx_flat.max())],
                    ))

                    ymo_mat = obj.model.materials[sm.material_index] if sm.material_index < len(obj.model.materials) else YmoMaterial(sm.material_index, 0, 1.0, "", "", b"")
                    gltf_mat_idx = get_or_create_material(ymo_mat, ref_p, is_trigger=is_trigger)

                    obj_mesh_primitives.append(Primitive(
                        attributes=attrs,
                        indices=idx_acc_idx,
                        material=gltf_mat_idx,
                        mode=pygltflib.TRIANGLES
                    ))

            if not obj_mesh_primitives:
                continue

            gltf_mesh_idx = len(gltf.meshes)
            gltf.meshes.append(Mesh(
                name=f"Mesh_{node_name}",
                primitives=obj_mesh_primitives
            ))

            # Transform matrix (column-major order flat array for glTF)
            matrix_4x4 = cls.euler_to_matrix(obj.position, obj.rotation_euler, obj.scale)
            mat_col_major = matrix_4x4.T.flatten().tolist()

            gltf_node_idx = len(gltf.nodes)
            gltf.nodes.append(Node(
                name=node_name,
                mesh=gltf_mesh_idx,
                matrix=mat_col_major,
                extras={
                    "object_index": obj.index,
                    "asset_path": obj.ymo_path_str,
                    "is_terrain": is_base,
                    "is_trigger": is_trigger
                }
            ))
            stage_children_nodes.append(gltf_node_idx)

        # Root stage container node
        root_stage_node_idx = len(gltf.nodes)
        gltf.nodes.append(Node(
            name=f"Stage_{scene.stage_name}",
            children=stage_children_nodes
        ))
        gltf.scenes[0].nodes = [root_stage_node_idx]

        gltf.buffers[0].byteLength = len(binary_blob)
        gltf.set_binary_blob(bytes(binary_blob))
        gltf.save(output_path)
        print(f"Exported modular stage scene {scene.stage_name} to {output_path} ({len(stage_children_nodes)} discrete nodes, {total_verts_count} vertices, {total_tris_count} triangles)")
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
