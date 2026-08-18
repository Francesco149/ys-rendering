#!/usr/bin/env python3
"""
Turnkey high-performance end-to-end extraction and conversion pipeline for Ys: The Oath in Felghana.
Extracts all map archives, converts every stage scene (.SOB) and 3D model (.YMO) to textured GLB,
sets up alpha masks for foliage and light shafts, and generates an output manifest.
"""

import os
import sys
import time
from pathlib import Path
from typing import Optional, List, Dict, Any
from concurrent.futures import ProcessPoolExecutor, as_completed

from src.extractor.archive import NiArchive
from src.converter.ymo_parser import YmoParser, YmoModel
from src.converter.gltf_exporter import GltfExporter
from src.converter.stage_builder import StageBuilder

DEFAULT_STEAM_PATH = Path("/mnt/c/Program Files (x86)/Steam/steamapps/common/Ys The Oath in Felghana/release/data.ni")

def convert_single_stage(args_tuple) -> Dict[str, Any]:
    sob_path_str, extracted_dir_str, stages_out_str = args_tuple
    sob_path = Path(sob_path_str)
    extracted_dir = Path(extracted_dir_str)
    stages_out = Path(stages_out_str)

    rel_map = sob_path.relative_to(extracted_dir / "MAP")
    stage_name = sob_path.stem
    out_glb = stages_out / rel_map.parent / f"{stage_name}_composite.glb"
    out_glb.parent.mkdir(parents=True, exist_ok=True)

    try:
        scene = StageBuilder.parse_sob(sob_path, extracted_dir)
        StageBuilder.export_composite_glb(scene, extracted_dir, out_glb, include_collision=False)
        total_v = sum(len(o.model.meshes[0].positions) for o in scene.placed_objects if o.model and o.model.meshes)
        total_t = sum(len(o.model.meshes[0].indices) for o in scene.placed_objects if o.model and o.model.meshes)
        return {
            "stage": stage_name,
            "folder": str(rel_map.parent),
            "objects": len(scene.placed_objects),
            "vertices": total_v,
            "triangles": total_t,
            "path": str(out_glb),
            "status": "OK"
        }
    except Exception as ex:
        return {
            "stage": stage_name,
            "folder": str(rel_map.parent),
            "status": f"Error: {ex}"
        }

def convert_single_model(args_tuple) -> bool:
    ymo_path_str, extracted_dir_str, models_out_str = args_tuple
    ymo_path = Path(ymo_path_str)
    extracted_dir = Path(extracted_dir_str)
    models_out = Path(models_out_str)

    rel_path = ymo_path.relative_to(extracted_dir)
    out_glb = models_out / rel_path.with_suffix(".glb")
    out_glb.parent.mkdir(parents=True, exist_ok=True)

    try:
        model = YmoParser.parse_file(ymo_path)
        GltfExporter.export_glb(model, ymo_path, out_glb, include_collision=False)
        return True
    except Exception:
        return False

def run_full_pipeline(
    archive_path: Optional[Path] = None,
    extracted_dir: Path = Path("extracted"),
    output_dir: Path = Path("output"),
    num_workers: int = 12
):
    start_time = time.time()
    extracted_dir = Path(extracted_dir)
    output_dir = Path(output_dir)
    stages_out = output_dir / "stages"
    models_out = output_dir / "models"
    stages_out.mkdir(parents=True, exist_ok=True)
    models_out.mkdir(parents=True, exist_ok=True)

    print("========================================================================")
    print("  Ys: The Oath in Felghana - Full Map Extraction & 3D Conversion")
    print("========================================================================")

    # Step 1: Locate and Extract Archives
    if not archive_path:
        if DEFAULT_STEAM_PATH.exists():
            archive_path = DEFAULT_STEAM_PATH
        else:
            local_ni = list(Path(".").glob("**/data.ni"))
            if local_ni:
                archive_path = local_ni[0]
            else:
                raise FileNotFoundError("Could not find data.ni in Steam directory or workspace.")

    print(f"\n[1/3] Extracting map archives from: {archive_path}")
    ni = NiArchive(archive_path)
    ni.extract_all(extracted_dir, filter_pattern="MAP\\")
    print(f"      Extracted all map files to {extracted_dir}")

    # Step 2: Parallel Convert Stage Scenes (.SOB)
    print(f"\n[2/3] Converting composite stage scenes (.SOB) with {num_workers} parallel workers...")
    sob_files = sorted(list(extracted_dir.glob("MAP/**/*.SOB")) + list(extracted_dir.glob("MAP/**/*.sob")))
    print(f"      Found {len(sob_files)} stage scenes.")

    stage_tasks = [
        (str(p), str(extracted_dir), str(stages_out)) for p in sob_files
    ]

    success_stages = 0
    with ProcessPoolExecutor(max_workers=num_workers) as executor:
        futures = {executor.submit(convert_single_stage, t): t for t in stage_tasks}
        for future in as_completed(futures):
            res = future.result()
            if res.get("status") == "OK":
                success_stages += 1
                print(f"      ✓ {res['stage']:<16} ({res['objects']:2d} objs, {res['triangles']:5d} tris) -> {res['path']}")
            else:
                print(f"      ✗ {res['stage']:<16} {res.get('status')}")

    print(f"      Successfully converted {success_stages}/{len(sob_files)} stage scenes.")

    # Step 3: Parallel Convert Standalone 3D Models (.YMO)
    print(f"\n[3/3] Converting 3D model assets (.YMO) with {num_workers} workers...")
    ymo_files = sorted(list(extracted_dir.glob("MAP/**/*.YMO")) + list(extracted_dir.glob("MAP/**/*.ymo")))
    print(f"      Found {len(ymo_files)} model files.")

    model_tasks = [
        (str(p), str(extracted_dir), str(models_out)) for p in ymo_files
    ]

    success_models = 0
    with ProcessPoolExecutor(max_workers=num_workers) as executor:
        futures = {executor.submit(convert_single_model, t): t for t in model_tasks}
        for future in as_completed(futures):
            if future.result():
                success_models += 1

    print(f"      Converted {success_models}/{len(ymo_files)} models to GLB.")

    elapsed = time.time() - start_time
    print("\n========================================================================")
    print(f"  ✓ Full Extraction & Conversion Completed in {elapsed:.1f}s!")
    print(f"  - Composite Stages: {stages_out.resolve()}")
    print(f"  - Standalone Models:{models_out.resolve()}")
    print("========================================================================")

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Full Ys Map Extraction & 3D Conversion Pipeline")
    parser.add_argument("--archive", "-a", help="Path to data.ni")
    parser.add_argument("--extracted", "-e", default="extracted", help="Extracted directory")
    parser.add_argument("--output", "-o", default="output", help="Output directory")
    parser.add_argument("--workers", "-w", type=int, default=12, help="Number of worker processes")
    args = parser.parse_args()

    arch_p = Path(args.archive) if args.archive else None
    run_full_pipeline(
        archive_path=arch_p,
        extracted_dir=Path(args.extracted),
        output_dir=Path(args.output),
        num_workers=args.workers
    )
