#!/usr/bin/env python3
"""
Launches Blender on Windows host (or Linux WSL) to inspect extracted Ys models and stages.
Automatically detects Windows Blender installation, ensures WSL interop is active,
translates WSL2 paths to Windows UNC paths, and imports GLB/OBJ models directly.
"""

import os
import subprocess
import sys
from pathlib import Path
from typing import Optional, List

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

def launch_blender(
    model_path: Path,
    use_windows: bool = True,
    import_script: Optional[Path] = None,
    background: bool = False
):
    model_path = Path(model_path).resolve()
    if not model_path.exists():
        raise FileNotFoundError(f"Model file not found: {model_path}")

    ensure_wsl_interop()
    win_blender = find_windows_blender() if use_windows else None
    ext = model_path.suffix.lower()

    if use_windows and win_blender and win_blender.exists():
        win_model_p = wsl_to_win_path(model_path)
        print(f"Launching Windows Blender ({win_blender.name})...")
        print(f"Importing: {win_model_p}")

        win_blender_str = str(win_blender)
        if ext == ".blend":
            args = [win_blender_str, win_model_p]
        elif ext in (".glb", ".gltf"):
            py_code = f"import bpy; bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=r'{win_model_p}')"
            args = [win_blender_str, "--python-expr", py_code]
        elif ext == ".obj":
            py_code = f"import bpy; bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.wm.obj_import(filepath=r'{win_model_p}')"
            args = [win_blender_str, "--python-expr", py_code]
        else:
            args = [win_blender_str, win_model_p]

        if import_script and Path(import_script).exists():
            win_script_p = wsl_to_win_path(Path(import_script))
            args.extend(["--python", win_script_p])

        try:
            subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("Windows Blender launched successfully with imported model.")
            return
        except OSError:
            win_blender_win = wsl_to_win_path(win_blender)
            if ext in (".glb", ".gltf"):
                cmd_args = ["cmd.exe", "/c", "start", "", win_blender_win, "--python-expr", f"\"import bpy; bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=r'{win_model_p}')\""]
            else:
                cmd_args = ["cmd.exe", "/c", "start", "", win_blender_win, win_model_p]
            subprocess.Popen(cmd_args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print("Windows Blender launched successfully via cmd.exe.")
            return

    # Linux WSL fallback
    print("Launching Linux Blender...")
    if ext in (".glb", ".gltf"):
        py_code = f"import bpy; bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.import_scene.gltf(filepath=r'{model_path}')"
        args = ["blender", "--python-expr", py_code]
    elif ext == ".obj":
        py_code = f"import bpy; bpy.ops.wm.read_factory_settings(use_empty=True); bpy.ops.wm.obj_import(filepath=r'{model_path}')"
        args = ["blender", "--python-expr", py_code]
    else:
        args = ["blender", str(model_path)]

    if import_script and Path(import_script).exists():
        args.extend(["--python", str(import_script)])
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
