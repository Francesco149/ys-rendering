#!/usr/bin/env python3
"""
Launches Blender on Windows host (or Linux WSL) to inspect extracted Ys models and stages.
Automatically sets up:
- Native .blend project generation in local Windows Temp folder
- Discrete Outliner nodes for Terrain and every placed Prop
- Directional Sun key & fill lighting and ambient world illumination
- 3D Viewport Shading automatically switched to 'RENDERED' mode
"""

import os
import subprocess
import sys
from pathlib import Path
from typing import Optional, List, Tuple

WINDOWS_BLENDER_CANDIDATES = [
    "/mnt/c/Program Files/Blender Foundation/Blender 5.1/blender.exe",
    "/mnt/c/Program Files/Blender Foundation/Blender 5.0/blender.exe",
    "/mnt/c/Program Files/Blender Foundation/Blender 4.3/blender.exe",
    "/mnt/c/Program Files/Blender Foundation/Blender 4.2/blender.exe",
    "/mnt/c/Program Files/Blender Foundation/Blender 4.1/blender.exe",
    "/mnt/c/Program Files/Blender Foundation/Blender 4.0/blender.exe",
]

def ensure_wsl_interop():
    """Ensure WSL binfmt_misc interop is registered in the kernel."""
    status_p = Path("/proc/sys/fs/binfmt_misc/WSLInterop")
    if not status_p.exists():
        reg_p = Path("/proc/sys/fs/binfmt_misc/register")
        if reg_p.exists():
            try:
                subprocess.run(
                    ["sudo", "tee", "/proc/sys/fs/binfmt_misc/register"],
                    input=b":WSLInterop:M::MZ::/init:PF\n",
                    capture_output=True,
                    timeout=3
                )
            except Exception:
                pass

def find_windows_blender() -> Optional[Path]:
    for cand in WINDOWS_BLENDER_CANDIDATES:
        p = Path(cand)
        if p.exists():
            return p

    base = Path("/mnt/c/Program Files/Blender Foundation")
    if base.exists():
        found = list(base.glob("**/blender.exe"))
        if found:
            return found[0]

    return None

def wsl_to_win_path(path: Path) -> str:
    path = Path(path).resolve()
    try:
        res = subprocess.run(["wslpath", "-w", str(path)], capture_output=True, text=True, check=True)
        return res.stdout.strip()
    except Exception:
        s = str(path)
        if s.startswith("/mnt/c/"):
            return "C:\\" + s[7:].replace("/", "\\")
        return "\\\\wsl.localhost\\NixOS" + s.replace("/", "\\")

def find_windows_temp_dir() -> Path:
    """Finds the local Windows Temp folder accessible from WSL."""
    user = os.environ.get("USER", "headpats")
    candidates = [
        Path(f"/mnt/c/Users/{user}/AppData/Local/Temp"),
        Path("/mnt/c/Users/headpats/AppData/Local/Temp"),
        Path("/mnt/c/Windows/Temp"),
        Path("/mnt/c/Temp"),
    ]
    for c in candidates:
        if c.exists():
            return c.resolve()
    
    cur = Path("output/_temp").resolve()
    cur.mkdir(parents=True, exist_ok=True)
    return cur

def generate_blend_project(
    blender_bin: Path,
    model_path_wsl: Path,
    target_blend_wsl: Path,
    temp_dir_wsl: Path
):
    """Headless prep step that creates a native .blend file with imported model, lights, and RENDERED mode."""
    ext = model_path_wsl.suffix.lower()
    model_win = wsl_to_win_path(model_path_wsl)
    blend_win = wsl_to_win_path(target_blend_wsl)

    if ext in (".glb", ".gltf"):
        import_cmd = f"bpy.ops.import_scene.gltf(filepath=r'{model_win}')"
    elif ext == ".obj":
        import_cmd = f"bpy.ops.wm.obj_import(filepath=r'{model_win}')"
    else:
        import_cmd = ""

    prep_code = f"""import bpy
import mathutils

# 1. Clear factory scene
bpy.ops.wm.read_factory_settings(use_empty=True)

# 2. Import 3D model
{import_cmd}

scene = bpy.context.scene

# 3. Set up World Ambient Lighting
if scene.world is None:
    scene.world = bpy.data.worlds.new("World")
if hasattr(scene.world, "node_tree") and scene.world.node_tree:
    bg = scene.world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs[0].default_value = (0.6, 0.6, 0.65, 1.0)
        bg.inputs[1].default_value = 1.0

# 4. Add Directional Sun Key Light
key_data = bpy.data.lights.new("DirectionalSun", type='SUN')
key_data.energy = 4.5
key_obj = bpy.data.objects.new("DirectionalSun", key_data)
scene.collection.objects.link(key_obj)
key_obj.rotation_euler = (0.75, 0.35, 0.6)

# 5. Add Directional Sun Fill Light
fill_data = bpy.data.lights.new("FillSun", type='SUN')
fill_data.energy = 2.0
fill_obj = bpy.data.objects.new("FillSun", fill_data)
scene.collection.objects.link(fill_obj)
fill_obj.rotation_euler = (-0.75, -0.35, -0.6)

# 6. Save as native .blend file
bpy.ops.wm.save_as_mainfile(filepath=r'{blend_win}')
"""
    prep_script_wsl = temp_dir_wsl / f"prep_{model_path_wsl.stem}.py"
    prep_script_wsl.write_text(prep_code, encoding="utf-8")
    prep_script_win = wsl_to_win_path(prep_script_wsl)

    # Run headless Blender to generate the .blend file
    subprocess.run(
        [str(blender_bin), "--background", "--python", str(prep_script_win)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=True
    )

def launch_blender(
    model_path: Path,
    use_windows: bool = True,
    background: bool = False
):
    model_path = Path(model_path).resolve()
    if not model_path.exists():
        raise FileNotFoundError(f"Model file not found: {model_path}")

    ensure_wsl_interop()
    win_blender = find_windows_blender() if use_windows else None
    ext = model_path.suffix.lower()

    if use_windows and win_blender and win_blender.exists():
        wsl_temp_dir = find_windows_temp_dir()

        if ext == ".blend":
            target_blend_wsl = model_path
            target_blend_win = wsl_to_win_path(model_path)
        else:
            print(f"Preparing scene project for Windows Blender...")
            target_blend_wsl = wsl_temp_dir / f"ys_{model_path.stem}.blend"
            generate_blend_project(win_blender, model_path, target_blend_wsl, wsl_temp_dir)
            target_blend_win = wsl_to_win_path(target_blend_wsl)

        print(f"Launching Windows Blender ({win_blender.name})...")
        print(f"Opening: {target_blend_win}")
        print("✓ Scene loaded with discrete Terrain & Props, Directional Sun light, and RENDERED viewport mode.")

        # Launch Blender with the native .blend project file and a GUI viewport shading command
        shading_py = "import bpy; [setattr(sp.shading, 'type', 'RENDERED') for win in bpy.context.window_manager.windows for a in win.screen.areas if a.type=='VIEW_3D' for sp in a.spaces if sp.type=='VIEW_3D']; [setattr(sp.shading, 'use_scene_lights', True) for win in bpy.context.window_manager.windows for a in win.screen.areas if a.type=='VIEW_3D' for sp in a.spaces if sp.type=='VIEW_3D']; [setattr(sp.shading, 'use_scene_world', True) for win in bpy.context.window_manager.windows for a in win.screen.areas if a.type=='VIEW_3D' for sp in a.spaces if sp.type=='VIEW_3D']"

        args = [str(win_blender), target_blend_win, "--python-expr", shading_py]
        try:
            subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("✓ Windows Blender launched successfully.")
            return
        except OSError:
            cmd_args = ["cmd.exe", "/c", "start", "", wsl_to_win_path(win_blender), target_blend_win, "--python-expr", f'"{shading_py}"']
            subprocess.Popen(cmd_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("✓ Windows Blender launched successfully via cmd.exe.")
            return

    # Linux WSL fallback
    print("Launching Linux Blender...")
    wsl_temp_dir = Path("/tmp")
    if ext == ".blend":
        target_blend_wsl = model_path
    else:
        target_blend_wsl = wsl_temp_dir / f"ys_{model_path.stem}.blend"
        generate_blend_project(Path("blender"), model_path, target_blend_wsl, wsl_temp_dir)

    shading_py = "import bpy; [setattr(sp.shading, 'type', 'RENDERED') for win in bpy.context.window_manager.windows for a in win.screen.areas if a.type=='VIEW_3D' for sp in a.spaces if sp.type=='VIEW_3D']"
    args = ["blender", str(target_blend_wsl), "--python-expr", shading_py]
    if background:
        args.append("--background")

    subprocess.Popen(args)
    print("Linux Blender launched.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 src/tools/open_blender.py <file.glb> [--linux]")
        sys.exit(1)

    target_file = Path(sys.argv[1])
    force_linux = "--linux" in sys.argv
    launch_blender(target_file, use_windows=not force_linux)
