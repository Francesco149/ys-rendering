#!/usr/bin/env python3
"""
Blender Import & Scene Organizer Script for Ys: The Oath in Felghana extracted maps.
Imports GLB files, groups collision meshes into a dedicated 'Collision' collection,
and disables collision collection visibility by default for clean viewport inspection.

Usage:
  blender <file.glb> --python src/tools/blender_import.py
  blender --python src/tools/blender_import.py -- <file.glb>
"""

import bpy
import sys
from pathlib import Path

def setup_scene_collections():
    scene = bpy.context.scene
    main_coll = scene.collection

    collision_coll = bpy.data.collections.get("Collision")
    if not collision_coll:
        collision_coll = bpy.data.collections.new("Collision")
        main_coll.children.link(collision_coll)

    moved_count = 0
    for obj in list(scene.objects):
        obj_name_lower = obj.name.lower()
        is_coll = "collision" in obj_name_lower or (obj.get("is_collision", False))
        if is_coll:
            # Unlink from all parent collections and link into Collision collection
            for c in list(obj.users_collection):
                c.objects.unlink(obj)
            if obj.name not in collision_coll.objects:
                collision_coll.objects.link(obj)
            # Also hide individual object in viewport by default
            obj.hide_viewport = True
            obj.hide_render = True
            moved_count += 1

    # Disable collision collection visibility in viewport and render by default
    # The user can toggle the checkbox in the Outliner anytime
    collision_coll.hide_viewport = True
    collision_coll.hide_render = True

    print(f"Organized {moved_count} collision objects into 'Collision' collection (hidden by default).")

def main():
    args = sys.argv
    glb_files = [a for a in args if a.lower().endswith(".glb") or a.lower().endswith(".gltf")]
    if glb_files:
        glb_path = glb_files[-1]
        print(f"Importing {glb_path} into Blender...")
        bpy.ops.import_scene.gltf(filepath=glb_path)

    setup_scene_collections()
    print("Blender scene organization complete.")

if __name__ == "__main__":
    main()
