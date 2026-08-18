#!/usr/bin/env python3
"""
Launches Blender on Windows host (or Linux WSL) to inspect extracted Ys models and stages.
Automatically sets up:
- Clean scene setup (no default cube)
- Asset import (.glb, .gltf, .obj)
- Directional Sun key & fill lighting and ambient world illumination
- 3D Viewport Shading automatically set to 'RENDERED' mode
"""

import os
import subprocess
import sys
import tempfile
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

def find_windows_temp() -> Tuple[Path, str]:
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
            win_path = wsl_to_win_path(c)
            return c, win_path
    
    # Fallback to current dir
    cur = Path("output/_scripts")
    cur.mkdir(parents=True, exist_ok=True)
    return cur, wsl_to_win_path(cur)

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

def generate_startup_script(model_path_str: str, ext: str) -> str:
    escaped_path = model_path_str.replace("\\", "\\\\")
    
    if ext in (".glb", ".gltf"):
        import_cmd = f"bpy.ops.import_scene.gltf(filepath=r'{escaped_path}')"
    elif ext == ".obj":
        import_cmd = f"bpy.ops.wm.obj_import(filepath=r'{escaped_path}')"
    else:
        import_cmd = ""

    return f"""import bpy
import mathutils

# 1. Clear factory scene
bpy.ops.wm.read_factory_settings(use_empty=True)

# 2. Import model
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

# 4. Add Directional Key Sun Light
key_data = bpy.data.lights.new("KeySun", type="SUN")
key_data.energy = 4.5
key_obj = bpy.data.objects.new("KeySun", key_data)
scene.collection.objects.link(key_obj)
key_obj.rotation_euler = (0.75, 0.35, 0.6)

# 5. Add Directional Fill Sun Light
fill_data = bpy.data.lights.new("FillSun", type="SUN")
fill_data.energy = 2.0
fill_obj = bpy.data.objects.new("FillSun", fill_data)
scene.collection.objects.link(fill_obj)
fill_obj.rotation_euler = (-0.75, -0.35, -0.6)

# 6. Switch Viewport Shading to RENDERED mode once UI is loaded
def set_viewport_rendered():
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == "VIEW_3D":
                for space in area.spaces:
                    if space.type == "VIEW_3D":
                        space.shading.type = "RENDERED"
                        space.shading.use_scene_lights = True
                        space.shading.use_scene_world = True

bpy.app.timers.register(set_viewport_rendered, first_interval=0.2)
"""

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
        wsl_temp, win_temp_str = find_windows_temp()
        script_file_wsl = wsl_temp / "ys_open_blender.py"
        script_file_win = os.path.join(win_temp_str, "ys_open_blender.py")

        win_model_p = wsl_to_win_path(model_path)
        print(f"Launching Windows Blender ({win_blender.name})...")
        print(f"Model: {win_model_p}")
        print("Importing model, adding directional lights, and switching to RENDERED mode...")

        script_content = generate_startup_script(win_model_p, ext)
        script_file_wsl.write_text(script_content, encoding="utf-8")

        args = [str(win_blender), "--python", script_file_win]

        try:
            subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("✓ Windows Blender launched successfully.")
            return
        except OSError:
            win_blender_win = wsl_to_win_path(win_blender)
            cmd_args = ["cmd.exe", "/c", "start", "", win_blender_win, "--python", script_file_win]
            subprocess.Popen(cmd_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("✓ Windows Blender launched successfully via cmd.exe.")
            return

    # Linux WSL fallback
    print("Launching Linux Blender...")
    temp_dir = Path("/tmp")
    script_file = temp_dir / "ys_open_blender.py"
    script_content = generate_startup_script(str(model_path), ext)
    script_file.write_text(script_content, encoding="utf-8")

    args = ["blender", "--python", str(script_file)]
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
