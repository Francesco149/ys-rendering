#!/usr/bin/env python3
"""
Exports Falcom YMO models to Wavefront OBJ / MTL format.
Preserves submeshes, vertex normals, UVs, vertex colors, and materials.
"""

from pathlib import Path
from typing import List, Dict, Optional
from PIL import Image
import numpy as np

from src.converter.ymo_parser import YmoModel, YmoParser
from src.converter.gltf_exporter import GltfExporter

class ObjExporter:
    @classmethod
    def export_obj(cls, model: YmoModel, model_path: Path, output_path: Path, export_textures: bool = True) -> Path:
        output_path = Path(output_path)
        out_dir = output_path.parent
        out_dir.mkdir(parents=True, exist_ok=True)

        mtl_filename = output_path.stem + ".mtl"
        mtl_path = out_dir / mtl_filename

        # Convert and write MTL file
        mtl_lines = ["# Wavefront Material Library", f"# Generated for {model.filename}", ""]
        mat_tex_names: Dict[int, str] = {}

        for ymo_mat in model.materials:
            mat_name = f"Mat_{ymo_mat.index}_{ymo_mat.texture_name or 'default'}"
            mtl_lines.append(f"newmtl {mat_name}")
            mtl_lines.append("Ka 1.000 1.000 1.000")
            mtl_lines.append("Kd 1.000 1.000 1.000")
            mtl_lines.append("Ks 0.000 0.000 0.000")
            mtl_lines.append(f"d {ymo_mat.alpha:.4f}")
            mtl_lines.append("illum 2")

            tex_p = GltfExporter.resolve_texture(model_path, ymo_mat.texture_name)
            if tex_p and tex_p.exists():
                png_filename = tex_p.stem + ".png"
                mat_tex_names[ymo_mat.index] = png_filename
                mtl_lines.append(f"map_Kd {png_filename}")

                if export_textures:
                    target_png = out_dir / png_filename
                    if not target_png.exists():
                        is_add = bool(ymo_mat.texture_name and ymo_mat.texture_name.upper().startswith("Z_"))
                        png_bytes = GltfExporter.dds_to_png_bytes(tex_p, is_additive=is_add)
                        with open(target_png, "wb") as f:
                            f.write(png_bytes)
            mtl_lines.append("")

        with open(mtl_path, "w", encoding="utf-8") as f:
            f.write("\n".join(mtl_lines))

        # Write OBJ file
        obj_lines = [
            f"# Wavefront OBJ file",
            f"# Exported from Falcom YMO: {model.filename}",
            f"mtllib {mtl_filename}",
            f"o {output_path.stem}",
            ""
        ]

        vert_offset = 1  # OBJ is 1-indexed
        for mesh in model.meshes:
            num_verts = len(mesh.positions)
            if num_verts == 0:
                continue

            # Geometric vertices (with optional vertex colors)
            for i in range(num_verts):
                p = mesh.positions[i]
                c = mesh.colors[i] / 255.0
                obj_lines.append(f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f} {c[0]:.4f} {c[1]:.4f} {c[2]:.4f}")

            # Texture coordinates (UV)
            # In OBJ standard: u v, where v=0 is bottom
            # Note: with top-left DDS images, v is (1.0 - v) for standard bottom-left UV space
            for i in range(num_verts):
                uv = mesh.uvs[i]
                obj_lines.append(f"vt {uv[0]:.6f} {1.0 - uv[1]:.6f}")

            # Vertex normals
            for i in range(num_verts):
                n = mesh.normals[i]
                obj_lines.append(f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}")

            # Faces by submesh
            for s_idx, sm in enumerate(mesh.submeshes):
                mat_name = f"Mat_{sm.material_index}_{model.materials[sm.material_index].texture_name if sm.material_index < len(model.materials) else 'default'}"
                obj_lines.append(f"\ng {mesh.name}_submesh_{s_idx}")
                obj_lines.append(f"usemtl {mat_name}")
                obj_lines.append("s 1")

                sm_tris = mesh.submesh_triangles[s_idx] if s_idx < len(mesh.submesh_triangles) else mesh.indices
                for tri in sm_tris:
                    v1 = tri[0] + vert_offset
                    v2 = tri[1] + vert_offset
                    v3 = tri[2] + vert_offset
                    obj_lines.append(f"f {v1}/{v1}/{v1} {v2}/{v2}/{v2} {v3}/{v3}/{v3}")

            vert_offset += num_verts

        with open(output_path, "w", encoding="utf-8") as f:
            f.write("\n".join(obj_lines))

        print(f"Exported OBJ to {output_path} with material library {mtl_path}")
        return output_path

if __name__ == "__main__":
    import sys
    model_p = Path(sys.argv[1])
    out_p = Path(sys.argv[2]) if len(sys.argv) > 2 else model_p.with_suffix(".obj")
    model = YmoParser.parse_file(model_p)
    ObjExporter.export_obj(model, model_p, out_p)
