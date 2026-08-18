import bpy
import mathutils
import sys

bpy.ops.wm.read_factory_settings(use_empty=True)

glb_path = sys.argv[-2]
output_img = sys.argv[-1]

print(f"Loading GLB from: {glb_path}")
bpy.ops.import_scene.gltf(filepath=glb_path)

scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in dir(bpy.types.RenderSettings) else 'BLENDER_EEVEE'
scene.render.resolution_x = 1280
scene.render.resolution_y = 720
scene.render.filepath = output_img

if scene.world is None:
    scene.world = bpy.data.worlds.new("World")
if hasattr(scene.world, "node_tree") and scene.world.node_tree:
    bg = scene.world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs[0].default_value = (0.7, 0.7, 0.75, 1.0)
        bg.inputs[1].default_value = 1.0

# Add camera
cam = bpy.data.cameras.new("Cam")
cam_obj = bpy.data.objects.new("Cam", cam)
scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

# Camera looking at middle section of the quarry
cam_pos = mathutils.Vector((15.0, -30.0, 20.0))
cam_obj.location = cam_pos
target = mathutils.Vector((15.0, 10.0, 0.0))
direction = target - cam_pos
cam_obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()

# Lights
key = bpy.data.lights.new("Key", type="SUN")
key.energy = 5.0
key_obj = bpy.data.objects.new("Key", key)
scene.collection.objects.link(key_obj)
key_obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()

fill = bpy.data.lights.new("Fill", type="SUN")
fill.energy = 3.0
fill_obj = bpy.data.objects.new("Fill", fill)
scene.collection.objects.link(fill_obj)
fill_obj.rotation_euler = (0.4, 0.0, 0.0)

bpy.ops.render.render(write_still=True)
print(f"Rendered: {output_img}")
