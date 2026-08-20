#!/usr/bin/env python3
"""
Exports Falcom YMO models and YCO collision meshes to glTF 2.0 / GLB format.
Converts DDS textures to PNG, creates standard PBR materials,
encodes geometry buffers, and includes color-coded, semi-transparent collision layers.
"""

import io
import struct
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

class GltfExporter:
    @staticmethod
    def resolve_texture(model_path: Path, tex_name: str, prefer_high_res: bool = True) -> Optional[Path]:
        if not tex_name:
            return None
        clean_tex = tex_name.replace("\\", "/")
        filename = Path(clean_tex).name

        stage_dir = model_path.parent
        stage_parent = stage_dir.parent
        root_dir = model_path
        while root_dir.parent != root_dir and root_dir.name.upper() not in ("MAP", "EXTRACTED"):
            root_dir = root_dir.parent

        candidates = [
            stage_dir,
            stage_dir / "H",
            stage_dir / "h",
            stage_dir / "L",
            stage_dir / "l",
            stage_parent / "COMMON" / "H",
            stage_parent / "common" / "h",
            stage_parent / "COMMON" / "L",
            stage_parent / "common" / "l",
            stage_parent / "COMMON",
            stage_parent / "common",
            root_dir / "MAP" / stage_parent.name / "COMMON" / "H",
            root_dir / "map" / stage_parent.name / "common" / "h",
            root_dir / "MAP" / "COMMON" / "H",
            root_dir / "map" / "common" / "h",
            root_dir / "MAP" / "COMMON",
            root_dir / "map" / "common",
            root_dir / "COMMON" / "H",
            root_dir / "common" / "h",
            root_dir / "COMMON",
            root_dir / "common",
        ]
        names_to_try = [filename, filename.upper(), filename.lower(), filename.capitalize()]
        if not filename.lower().startswith("_c_"):
            names_to_try.extend(["_c_" + filename, "_C_" + filename, "_C_" + filename.upper(), "_c_" + filename.lower()])
        else:
            stripped = filename[4:]
            names_to_try.extend([stripped, stripped.upper(), stripped.lower()])

        for c in candidates:
            for fname in names_to_try:
                p = c / fname
                if p.exists():
                    return p
        return None

    @staticmethod
    def dds_to_png_bytes(dds_path: Path, is_additive: bool = False) -> bytes:
        with Image.open(dds_path) as im:
            if is_additive:
                im_rgba = im.convert("RGBA")
                arr = np.array(im_rgba)
                luminance = np.max(arr[:, :, :3], axis=2)
                arr[:, :, 3] = luminance
                im_final = Image.fromarray(arr)
            elif im.mode not in ("RGB", "RGBA"):
                im_final = im.convert("RGBA")
            else:
                im_final = im
            buf = io.BytesIO()
            im_final.save(buf, format="PNG", compress_level=4)
            return buf.getvalue()
    @classmethod
    def export_glb(
        cls,
        model: YmoModel,
        model_path: Path,
        output_path: Path,
        collision_meshes: Optional[List[YcoCollisionMesh]] = None,
        include_collision: bool = False,
        embed_textures: bool = True
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
        mat_map: Dict[int, int] = {}
        tex_cache: Dict[str, int] = {}

        # Materials
        for ymo_mat in model.materials:
            tex_p = cls.resolve_texture(model_path, ymo_mat.texture_name)
            gltf_tex_idx = None

            if tex_p and tex_p.exists():
                tex_key = str(tex_p.resolve())
                if tex_key in tex_cache:
                    gltf_tex_idx = tex_cache[tex_key]
                else:
                    is_add = bool(ymo_mat.texture_name and ymo_mat.texture_name.upper().startswith("Z_"))
                    png_data = cls.dds_to_png_bytes(tex_p, is_additive=is_add)
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

            # Check if texture has transparency
            has_texture_alpha = False
            if tex_p and tex_p.exists():
                try:
                    chk_im = Image.open(tex_p)
                    if chk_im.mode in ("RGBA", "LA") or "transparency" in chk_im.info:
                        chk_arr = np.array(chk_im.convert("RGBA"))
                        if (chk_arr[:, :, 3] < 250).any():
                            has_texture_alpha = True
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
                name=f"Mat_{ymo_mat.index}_{ymo_mat.texture_name or 'default'}",
                pbrMetallicRoughness=pbr,
                alphaMode=alpha_mode,
                alphaCutoff=alpha_cutoff,
                doubleSided=True
            ))
            mat_map[ymo_mat.index] = gltf_mat_idx

        # Collision Materials (Emerald Green, Coral Orange, Cyan Blue)
        coll_mat_indices = {}
        coll_colors = {
            "walkable": ([0.1, 0.9, 0.4, 0.4], "Mat_Collision_Walkable"),
            "wall": ([1.0, 0.4, 0.1, 0.4], "Mat_Collision_Wall"),
            "camera": ([0.1, 0.6, 1.0, 0.4], "Mat_Collision_Camera"),
            "generic": ([0.9, 0.8, 0.2, 0.4], "Mat_Collision_Generic"),
        }
        for c_type, (col, c_name) in coll_colors.items():
            c_idx = len(gltf.materials)
            gltf.materials.append(Material(
                name=c_name,
                pbrMetallicRoughness=PbrMetallicRoughness(
                    baseColorFactor=col,
                    metallicFactor=0.0,
                    roughnessFactor=0.9
                ),
                alphaMode="BLEND",
                doubleSided=True
            ))
            coll_mat_indices[c_type] = c_idx

        # Main mesh primitives
        gltf_primitives: List[Primitive] = []
        for mesh in model.meshes:
            num_verts = len(mesh.positions)
            if num_verts == 0:
                continue

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

                gltf_mat = mat_map.get(sm.material_index, 0)

                gltf_primitives.append(Primitive(
                    attributes=attrs,
                    indices=idx_acc_idx,
                    material=gltf_mat,
                    mode=pygltflib.TRIANGLES
                ))

        main_mesh_idx = len(gltf.meshes)
        gltf.meshes.append(Mesh(
            name=model.filename.split(".")[0],
            primitives=gltf_primitives
        ))

        # Root Scene Node
        root_children = []
        main_node_idx = len(gltf.nodes)
        gltf.nodes.append(Node(name=model.filename.split(".")[0], mesh=main_mesh_idx))
        root_children.append(main_node_idx)

        # Auto-discover collision meshes if enabled
        if include_collision and collision_meshes is None and model_path:
            discovered_coll = []
            stage_dir = model_path.parent
            base_name = model_path.stem.split("__")[0]
            for ext_type in ("__s.yco", "__w.yco", "__c.yco", "__S.YCO", "__W.YCO", "__C.YCO"):
                cand = stage_dir / f"{base_name}{ext_type}"
                if cand.exists():
                    try:
                        discovered_coll.append(YcoParser.parse_file(cand))
                    except Exception:
                        pass

            # If no .yco collision found, check for Ys Origin collision companion (e.g. S_1000_.ymo)
            if len(discovered_coll) == 0:
                for under_suffix in ("_.ymo", "_.YMO"):
                    cand_under = stage_dir / f"{base_name}{under_suffix}"
                    if cand_under.exists():
                        try:
                            under_model = YmoParser.parse_file(cand_under)
                            for mesh in under_model.meshes:
                                for s_idx, sm in enumerate(mesh.submeshes):
                                    sm_tris = mesh.submesh_triangles[s_idx] if s_idx < len(mesh.submesh_triangles) else mesh.indices
                                    if len(sm_tris) == 0:
                                        continue
                                    # Classify into walkable / wall by surface normals
                                    v0 = mesh.positions[sm_tris[:, 0]]
                                    v1 = mesh.positions[sm_tris[:, 1]]
                                    v2 = mesh.positions[sm_tris[:, 2]]
                                    fn = np.cross(v1 - v0, v2 - v0)
                                    fn_len = np.linalg.norm(fn, axis=1, keepdims=True)
                                    fn_len[fn_len == 0] = 1.0
                                    fn = fn / fn_len
                                    mean_ny = np.mean(np.abs(fn[:, 1]))
                                    c_type = "walkable" if mean_ny > 0.45 else "wall"
                                    
                                    # Build collision mesh primitive
                                    unique_idx, inv_idx = np.unique(sm_tris.flatten(), return_inverse=True)
                                    sub_pos = mesh.positions[unique_idx]
                                    sub_norm = mesh.normals[unique_idx]
                                    sub_idx = inv_idx.reshape((-1, 3))
                                    
                                    discovered_coll.append(YcoCollisionMesh(
                                        filename=cand_under.name,
                                        collision_type=c_type,
                                        triangles=[],
                                        positions=sub_pos,
                                        normals=sub_norm,
                                        indices=sub_idx
                                    ))
                        except Exception:
                            pass
                        break

            collision_meshes = discovered_coll
        # Add Collision Meshes if requested
        if include_collision and collision_meshes:
            for coll in collision_meshes:
                if len(coll.positions) == 0:
                    continue

                while len(binary_blob) % 4 != 0:
                    binary_blob.append(0)
                c_pos_offset = len(binary_blob)
                c_pos_data = coll.positions.astype(np.float32).tobytes()
                binary_blob.extend(c_pos_data)

                c_pos_bv_idx = len(gltf.bufferViews)
                gltf.bufferViews.append(BufferView(
                    buffer=0,
                    byteOffset=c_pos_offset,
                    byteLength=len(c_pos_data),
                    target=pygltflib.ARRAY_BUFFER
                ))

                c_pos_acc_idx = len(gltf.accessors)
                gltf.accessors.append(Accessor(
                    bufferView=c_pos_bv_idx,
                    byteOffset=0,
                    componentType=pygltflib.FLOAT,
                    count=len(coll.positions),
                    type=pygltflib.VEC3,
                    min=coll.positions.min(axis=0).tolist(),
                    max=coll.positions.max(axis=0).tolist(),
                ))

                # Normals
                while len(binary_blob) % 4 != 0:
                    binary_blob.append(0)
                c_norm_offset = len(binary_blob)
                c_norm_data = coll.normals.astype(np.float32).tobytes()
                binary_blob.extend(c_norm_data)

                c_norm_bv_idx = len(gltf.bufferViews)
                gltf.bufferViews.append(BufferView(
                    buffer=0,
                    byteOffset=c_norm_offset,
                    byteLength=len(c_norm_data),
                    target=pygltflib.ARRAY_BUFFER
                ))

                c_norm_acc_idx = len(gltf.accessors)
                gltf.accessors.append(Accessor(
                    bufferView=c_norm_bv_idx,
                    byteOffset=0,
                    componentType=pygltflib.FLOAT,
                    count=len(coll.normals),
                    type=pygltflib.VEC3,
                ))

                # Indices
                c_idx_flat = coll.indices.flatten().astype(np.uint32)
                while len(binary_blob) % 4 != 0:
                    binary_blob.append(0)
                c_idx_offset = len(binary_blob)
                c_idx_data = c_idx_flat.tobytes()
                binary_blob.extend(c_idx_data)

                c_idx_bv_idx = len(gltf.bufferViews)
                gltf.bufferViews.append(BufferView(
                    buffer=0,
                    byteOffset=c_idx_offset,
                    byteLength=len(c_idx_data),
                    target=pygltflib.ELEMENT_ARRAY_BUFFER
                ))

                c_idx_acc_idx = len(gltf.accessors)
                gltf.accessors.append(Accessor(
                    bufferView=c_idx_bv_idx,
                    byteOffset=0,
                    componentType=pygltflib.UNSIGNED_INT,
                    count=len(c_idx_flat),
                    type=pygltflib.SCALAR,
                    min=[int(c_idx_flat.min())],
                    max=[int(c_idx_flat.max())],
                ))

                c_mat_idx = coll_mat_indices.get(coll.collision_type, coll_mat_indices["generic"])

                coll_mesh_idx = len(gltf.meshes)
                gltf.meshes.append(Mesh(
                    name=f"Collision_{coll.collision_type.capitalize()}",
                    primitives=[Primitive(
                        attributes=Attributes(
                            POSITION=c_pos_acc_idx,
                            NORMAL=c_norm_acc_idx
                        ),
                        indices=c_idx_acc_idx,
                        material=c_mat_idx,
                        mode=pygltflib.TRIANGLES
                    )]
                ))

                coll_node_idx = len(gltf.nodes)
                gltf.nodes.append(Node(
                    name=f"Collision_{coll.collision_type.capitalize()}",
                    mesh=coll_mesh_idx,
                    extras={"is_collision": True, "collision_type": coll.collision_type}
                ))
                root_children.append(coll_node_idx)

        # Root container node
        if len(root_children) == 1:
            gltf.scenes[0].nodes = [0]
        else:
            root_node_idx = len(gltf.nodes)
            gltf.nodes.append(Node(
                name=f"{output_path.stem}_Scene",
                children=root_children
            ))
            gltf.scenes[0].nodes = [root_node_idx]

        gltf.buffers[0].byteLength = len(binary_blob)
        gltf.set_binary_blob(bytes(binary_blob))
        gltf.save(output_path)
        print(f"Exported GLB with collision layers to {output_path} ({len(binary_blob)} bytes buffer)")
        return output_path

if __name__ == "__main__":
    import sys
    model_p = Path(sys.argv[1])
    out_p = Path(sys.argv[2]) if len(sys.argv) > 2 else model_p.with_suffix(".glb")
    model = YmoParser.parse_file(model_p)
    GltfExporter.export_glb(model, model_p, out_p)
