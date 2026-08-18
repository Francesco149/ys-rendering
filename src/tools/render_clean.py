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

# World lighting
if scene.world is None:
    scene.world = bpy.data.worlds.new("World")
if hasattr(scene.world, "node_tree") and scene.world.node_tree:
    bg = scene.world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs[0].default_value = (0.7, 0.7, 0.75, 1.0)
        bg.inputs[1].default_value = 1.0

# Hide all collision objects
map_meshes = []
for obj in list(scene.objects):
    if obj.type == 'MESH':
        if "collision" in obj.name.lower() or obj.get("is_collision", False):
            obj.hide_render = True
            obj.hide_viewport = True
        else:
            map_meshes.append(obj)

if not map_meshes:
    map_meshes = [obj for obj in scene.objects if obj.type == 'MESH']

# Compute bounding box of visible map meshes only
min_corner = [float('inf')] * 3
max_corner = [float('-inf')] * 3

for obj in map_meshes:
    for corner in obj.bound_box:
        world_corner = obj.matrix_world @ mathutils.Vector(corner)
        for i in range(3):
            min_corner[i] = min(min_corner[i], world_corner[i])
            max_corner[i] = max(max_corner[i], world_corner[i])

center = [(min_corner[i] + max_corner[i]) / 2.0 for i in range(3)]
size = [max_corner[i] - min_corner[i] for i in range(3)]
max_dim = max(size) if max(size) > 0.001 else 1.0

print(f"Map bounds: center={center}, size={size}, max_dim={max_dim}")

# Add camera
cam_data = bpy.data.cameras.new("RenderCamera")
cam_obj = bpy.data.objects.new("RenderCamera", cam_data)
scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

dist = max_dim * 1.4
cam_pos = mathutils.Vector((center[0] + dist * 0.6, center[1] - dist * 0.9, center[2] + dist * 0.7))
cam_obj.location = cam_pos

direction = mathutils.Vector(center) - cam_pos
cam_obj.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()

# Key light
key_data = bpy.data.lights.new("KeySun", type='SUN')
key_data.energy = 4.5
key_obj = bpy.data.objects.new("KeySun", key_data)
scene.collection.objects.link(key_obj)
key_obj.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()

# Fill light
fill_data = bpy.data.lights.new("TopSun", type='SUN')
fill_data.energy = 2.5
fill_obj = bpy.data.objects.new("TopSun", fill_data)
scene.collection.objects.link(fill_obj)
fill_obj.rotation_euler = (0.2, 0.0, 0.0)

bpy.ops.render.render(write_still=True)
print(f"Rendered image saved to: {output_img}")
