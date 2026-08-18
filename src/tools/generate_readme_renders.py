import bpy
import mathutils
import sys
from pathlib import Path

img_dir = Path("docs/images")
img_dir.mkdir(parents=True, exist_ok=True)

def render_scene(glb_path: str, out_img: str, cam_pos, cam_target, light_dir=(0.75, 0.35, 0.6), hide_coll=True):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=glb_path)
    
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in dir(bpy.types.RenderSettings) else 'BLENDER_EEVEE'
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.filepath = out_img

    if scene.world is None:
        scene.world = bpy.data.worlds.new("World")
    if hasattr(scene.world, "node_tree") and scene.world.node_tree:
        bg = scene.world.node_tree.nodes.get("Background")
        if bg:
            bg.inputs[0].default_value = (0.65, 0.65, 0.7, 1.0)
            bg.inputs[1].default_value = 1.0

    if hide_coll:
        for obj in scene.objects:
            if "collision" in obj.name.lower() or obj.get("is_collision", False) or "trigger" in obj.name.lower():
                obj.hide_render = True
                obj.hide_viewport = True

    # Camera
    cam = bpy.data.cameras.new("Cam")
    cam_obj = bpy.data.objects.new("Cam", cam)
    scene.collection.objects.link(cam_obj)
    scene.camera = cam_obj
    cam_obj.location = mathutils.Vector(cam_pos)
    
    direction = mathutils.Vector(cam_target) - mathutils.Vector(cam_pos)
    cam_obj.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()

    # Directional Key Light (aligned with camera view for consistent illumination)
    key = bpy.data.lights.new("Key", type='SUN')
    key.energy = 4.5
    key_obj = bpy.data.objects.new("Key", key)
    scene.collection.objects.link(key_obj)
    key_obj.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()

    # Directional Fill Light
    fill = bpy.data.lights.new("Fill", type='SUN')
    fill.energy = 2.5
    fill_obj = bpy.data.objects.new("Fill", fill)
    scene.collection.objects.link(fill_obj)
    fill_obj.rotation_euler = (0.3, 0.0, 0.0)

    bpy.ops.render.render(write_still=True)
    print(f"Rendered: {out_img}")

# 1. Redmont Tavern (S_0100) - Rotated 90 deg left from previous view
render_scene(
    "output/stages/S_01/S_0100/S_0100_composite.glb",
    "docs/images/s_0100_tavern.png",
    cam_pos=(25.0, 16.0, 18.0),
    cam_target=(0.0, 0.0, 3.5)
)

# 2. Village Scene (S_0000) - Rotated ~180 deg from previous view
render_scene(
    "output/stages/S_00/S_0000/S_0000_composite.glb",
    "docs/images/s_0000_village.png",
    cam_pos=(-25.0, 55.0, 35.0),
    cam_target=(-4.0, 6.0, 7.0)
)

# 3. Castle Gate & Outskirts (S_2000) - Rotated 90 deg left from previous view
render_scene(
    "output/stages/S_20/S_2000/S_2000_composite.glb",
    "docs/images/s_2000_castle_gate.png",
    cam_pos=(-85.0, -55.0, 45.0),
    cam_target=(-28.0, -20.0, 13.0)
)

# 4. S_1000 Looking directly towards the 2.5D mountain backdrop plane behind the road & river
render_scene(
    "output/stages/S_10/S_1000/S_1000_composite.glb",
    "docs/images/s_1000_flat_backdrop.png",
    cam_pos=(15.0, 40.0, 16.0),
    cam_target=(85.0, -45.0, 20.0),
    light_dir=(0.7, 0.3, 0.6)
)
