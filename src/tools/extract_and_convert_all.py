#!/usr/bin/env python3
"""
Turnkey high-performance end-to-end extraction and conversion pipeline for Falcom Napishtim Engine titles:
- Ys: The Oath in Felghana
- Ys Origin
- Ys VI: The Ark of Napishtim

Extracts all map archives, converts every stage scene (.SOB) and 3D model (.YMO) to textured GLB with discrete nodes,
sets up alpha masks for foliage and light shafts, and auto-discovers installed Steam games.
"""

import os
import sys
import time
from pathlib import Path
from typing import Optional, List, Dict, Any, Tuple
from concurrent.futures import ProcessPoolExecutor, as_completed

from src.extractor.archive import NiArchive
from src.converter.ymo_parser import YmoParser, YmoModel
from src.converter.gltf_exporter import GltfExporter
from src.converter.stage_builder import StageBuilder

KNOWN_GAMES = {
    "felghana": {
        "name": "Ys: The Oath in Felghana",
        "steam_path": Path("/mnt/c/Program Files (x86)/Steam/steamapps/common/Ys The Oath in Felghana/release/data.ni"),
        "archive_filter": "MAP\\",
    },
    "origin": {
        "name": "Ys Origin",
        "steam_path": Path("/mnt/c/Program Files (x86)/Steam/steamapps/common/Ys Origin/release/data.ni"),
        "archive_filter": "MAP\\",
    },
    "ys6": {
        "name": "Ys VI: The Ark of Napishtim",
        "steam_path": Path("/mnt/c/Program Files (x86)/Steam/steamapps/common/Ys VI/release/data.ni"),
        "archive_filter": "MAP\\",
    },
}

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

def process_game(
    game_key: str,
    game_info: Dict[str, Any],
    base_extracted_dir: Path,
    base_output_dir: Path,
    num_workers: int = 12
):
    game_name = game_info["name"]
    archive_path = game_info["steam_path"]
    
    if not archive_path.exists():
        print(f"\n[!] Skipping {game_name}: Archive not found at {archive_path}")
        return

    extracted_dir = base_extracted_dir / game_key
    output_dir = base_output_dir / game_key
    stages_out = output_dir / "stages"
    models_out = output_dir / "models"
    stages_out.mkdir(parents=True, exist_ok=True)
    models_out.mkdir(parents=True, exist_ok=True)

    print(f"\n========================================================================")
    print(f"  Processing: {game_name}")
    print(f"  Archive:    {archive_path}")
    print(f"  Output:     {output_dir}")
    print(f"========================================================================")

    # 1. Extraction
    print(f"\n[1/3] Extracting map archives...")
    ni = NiArchive(archive_path)
    ni.extract_all(extracted_dir, filter_pattern=game_info.get("archive_filter", "MAP\\"))
    print(f"      Extracted map assets to {extracted_dir}")

    # 2. Stage Scenes (.SOB)
    print(f"\n[2/3] Converting composite stage scenes (.SOB)...")
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

    # 3. Standalone Models (.YMO)
    print(f"\n[3/3] Converting 3D model assets (.YMO)...")
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

def run_full_pipeline(
    target_game: str = "all",
    extracted_dir: Path = Path("extracted"),
    output_dir: Path = Path("output"),
    num_workers: int = 12
):
    start_time = time.time()
    extracted_dir = Path(extracted_dir)
    output_dir = Path(output_dir)

    print("========================================================================")
    print("  Falcom Napishtim Engine - Multi-Game Map Extraction & 3D Conversion")
    print("========================================================================")

    games_to_run = []
    if target_game == "all":
        games_to_run = list(KNOWN_GAMES.keys())
    elif target_game in KNOWN_GAMES:
        games_to_run = [target_game]
    else:
        raise ValueError(f"Unknown game: '{target_game}'. Available: {list(KNOWN_GAMES.keys())} or 'all'")

    for g_key in games_to_run:
        process_game(g_key, KNOWN_GAMES[g_key], extracted_dir, output_dir, num_workers=num_workers)

    elapsed = time.time() - start_time
    print("\n========================================================================")
    print(f"  ✓ Multi-Game Pipeline Completed in {elapsed:.1f}s!")
    print(f"  Output directory: {output_dir.resolve()}")
    print("========================================================================")

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Multi-Game Ys Map Extraction & 3D Conversion Pipeline")
    parser.add_argument("--game", "-g", default="all", choices=["felghana", "origin", "ys6", "all"], help="Game to process")
    parser.add_argument("--extracted", "-e", default="extracted", help="Extracted directory")
    parser.add_argument("--output", "-o", default="output", help="Output directory")
    parser.add_argument("--workers", "-w", type=int, default=12, help="Number of worker processes")
    args = parser.parse_args()

    run_full_pipeline(
        target_game=args.game,
        extracted_dir=Path(args.extracted),
        output_dir=Path(args.output),
        num_workers=args.workers
    )
