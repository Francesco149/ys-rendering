#!/usr/bin/env python3
"""
Ys: The Oath in Felghana - 3D Map & Mesh Extraction & Conversion Pipeline.
Converts proprietary Falcom Napishtim Engine archives (.na/.ni) and models (.ymo/.sob)
into textured glTF 2.0 / GLB and Wavefront OBJ models ready for Blender analysis.
"""

import argparse
import sys
from pathlib import Path
from typing import Optional

from src.extractor.archive import NiArchive
from src.converter.ymo_parser import YmoParser, YmoModel
from src.converter.gltf_exporter import GltfExporter
from src.converter.obj_exporter import ObjExporter
from src.converter.stage_builder import StageBuilder

DEFAULT_STEAM_PATH = Path("/mnt/c/Program Files (x86)/Steam/steamapps/common/Ys The Oath in Felghana/release/data.ni")

def find_game_archive(custom_path: Optional[str] = None) -> Path:
    if custom_path:
        p = Path(custom_path)
        if p.exists():
            return p
        raise FileNotFoundError(f"Specified archive not found: {custom_path}")
    
    if DEFAULT_STEAM_PATH.exists():
        return DEFAULT_STEAM_PATH
    
    # Try local search
    local_ni = list(Path(".").glob("**/data.ni"))
    if local_ni:
        return local_ni[0]
    
    raise FileNotFoundError("Could not find data.ni. Please specify --archive path.")

def cmd_extract(args):
    archive_path = find_game_archive(args.archive)
    print(f"Opening archive: {archive_path}")
    ni = NiArchive(archive_path)
    out_dir = Path(args.output or "extracted")
    
    filter_pat = args.filter or "MAP\\"
    print(f"Extracting files matching '{filter_pat}' to {out_dir}...")
    ni.extract_all(out_dir, filter_pattern=filter_pat)
    print("Extraction complete.")

def cmd_convert_model(args):
    ymo_path = Path(args.input)
    if not ymo_path.exists():
        raise FileNotFoundError(f"Input file not found: {ymo_path}")
    
    out_path = Path(args.output or (ymo_path.with_suffix(".glb") if args.format == "glb" else ymo_path.with_suffix(".obj")))
    model = YmoParser.parse_file(ymo_path)
    print(f"Parsed {model.filename}: {len(model.materials)} materials, {len(model.meshes)} meshes")

    if args.format.lower() == "glb" or out_path.suffix.lower() == ".glb":
        GltfExporter.export_glb(model, ymo_path, out_path)
    else:
        ObjExporter.export_obj(model, ymo_path, out_path)

def cmd_convert_stage(args):
    sob_path = Path(args.input)
    if not sob_path.exists():
        raise FileNotFoundError(f"Input SOB not found: {sob_path}")
    
    assets_root = Path(args.assets_root or "extracted")
    out_path = Path(args.output or f"output/{sob_path.stem}_composite.glb")
    
    scene = StageBuilder.parse_sob(sob_path, assets_root)
    print(f"Parsed stage {scene.stage_name} with {len(scene.placed_objects)} placed objects")
    StageBuilder.export_composite_glb(scene, assets_root, out_path)

def cmd_batch_convert(args):
    assets_root = Path(args.assets_root or "extracted")
    out_dir = Path(args.output or "output/stages")
    out_dir.mkdir(parents=True, exist_ok=True)

    # Find all SOB files
    sob_files = list(assets_root.glob("MAP/**/*.SOB")) + list(assets_root.glob("MAP/**/*.sob"))
    print(f"Found {len(sob_files)} stage placement files (.SOB).")

    for sob in sob_files:
        try:
            rel_stage = sob.relative_to(assets_root / "MAP")
            stage_out = out_dir / rel_stage.parent / f"{sob.stem}_composite.glb"
            scene = StageBuilder.parse_sob(sob, assets_root)
            StageBuilder.export_composite_glb(scene, assets_root, stage_out)
        except Exception as ex:
            print(f"Warning: Failed to convert stage {sob}: {ex}")

    # Find all standalone YMO files not already part of a stage
    ymo_files = list(assets_root.glob("MAP/**/*.YMO")) + list(assets_root.glob("MAP/**/*.ymo"))
    print(f"Found {len(ymo_files)} YMO models.")
    models_out = out_dir / "models"
    for ymo in ymo_files:
        try:
            rel = ymo.relative_to(assets_root)
            out_p = models_out / rel.with_suffix(".glb")
            model = YmoParser.parse_file(ymo)
            GltfExporter.export_glb(model, ymo, out_p)
        except Exception as ex:
            print(f"Warning: Failed to convert model {ymo}: {ex}")

def main():
    parser = argparse.ArgumentParser(description="Ys: The Oath in Felghana 3D Mesh & Map Converter")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # Extract
    p_ext = subparsers.add_parser("extract", help="Extract assets from data.na/data.ni")
    p_ext.add_argument("--archive", "-a", help="Path to data.ni")
    p_ext.add_argument("--output", "-o", default="extracted", help="Output directory")
    p_ext.add_argument("--filter", "-f", default="MAP\\", help="Filter pattern (e.g. MAP\\, ENEMY\\)")
    p_ext.set_defaults(func=cmd_extract)

    # Convert model
    p_conv = subparsers.add_parser("convert-model", help="Convert single YMO model to GLB/OBJ")
    p_conv.add_argument("input", help="Path to .YMO file")
    p_conv.add_argument("--output", "-o", help="Output GLB/OBJ path")
    p_conv.add_argument("--format", default="glb", choices=["glb", "obj"], help="Output format")
    p_conv.set_defaults(func=cmd_convert_model)

    # Convert stage
    p_stage = subparsers.add_parser("convert-stage", help="Convert full SOB stage scene to composite GLB")
    p_stage.add_argument("input", help="Path to .SOB file")
    p_stage.add_argument("--assets-root", "-r", default="extracted", help="Extracted assets root directory")
    p_stage.add_argument("--output", "-o", help="Output GLB path")
    p_stage.set_defaults(func=cmd_convert_stage)

    # Batch convert
    p_batch = subparsers.add_parser("batch-convert", help="Batch convert all stages and models")
    p_batch.add_argument("--assets-root", "-r", default="extracted", help="Extracted assets root directory")
    p_batch.add_argument("--output", "-o", default="output/all_stages", help="Output directory")
    p_batch.set_defaults(func=cmd_batch_convert)

    args = parser.parse_args()
    args.func(args)

if __name__ == "__main__":
    main()
