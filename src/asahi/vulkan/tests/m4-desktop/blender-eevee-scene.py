# SPDX-License-Identifier: MIT
"""Run through Blender --python under app-observe and the GPU experiment guard.

Small reproducible Eevee scene: diffuse cube, metallic sphere, ground and area
light shadows. Saves the blend, exact settings, renderer and final PNG into the
runner's retained diagnostics. A completed render is not a pixel comparison.
"""
import bpy
import gpu
import hashlib
import json
import os
from pathlib import Path
from mathutils import Vector

out = Path(os.environ['MESA_SHADER_CACHE_DIR']).parent / 'eevee'
out.mkdir(exist_ok=False)
scene = bpy.context.scene
bpy.context.preferences.view.show_splash = False
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 192
scene.render.resolution_y = 144
scene.render.resolution_percentage = 100
scene.eevee.taa_render_samples = 16
scene.eevee.taa_samples = 16
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = str(out / 'render.png')
scene.view_settings.view_transform = 'Standard'
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.12, .16, .22, 1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = .25

def material(name, color, metallic=0., roughness=.4):
    m = bpy.data.materials.new(name)
    m.diffuse_color = color
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = color
    bsdf.inputs['Metallic'].default_value = metallic
    bsdf.inputs['Roughness'].default_value = roughness
    return m

bpy.ops.mesh.primitive_plane_add(size=200)
bpy.context.object.data.materials.append(material('Ground', (.3,.35,.4,1), roughness=.65))
bpy.ops.mesh.primitive_cube_add(size=1.5, location=(-.9,0,.75))
cube = bpy.context.object
cube.rotation_euler.z = .3
cube.data.materials.append(material('Red', (.65,.04,.025,1)))
bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=.75, location=(1.05,.1,.75))
bpy.context.object.data.materials.append(material('Metal', (.15,.35,.65,1), metallic=.8, roughness=.23))
bpy.ops.object.shade_smooth()
bpy.ops.object.light_add(type='AREA', location=(-3,-4,6))
light = bpy.context.object
light.data.energy = 900
light.data.shape = 'DISK'
light.data.size = 2
light.rotation_euler = (Vector((0,0,.4))-light.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(4,-7,4.5))
cam = bpy.context.object
cam.rotation_euler = (Vector((0,0,.7))-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.lens = 45
scene.camera = cam
bpy.ops.wm.save_as_mainfile(filepath=str(out/'scene.blend'))
metadata = dict(engine=scene.render.engine, renderer=gpu.platform.renderer_get(),
                vendor=gpu.platform.vendor_get(), backend=gpu.platform.backend_type_get(),
                version=bpy.app.version_string, samples=scene.eevee.taa_render_samples,
                resolution=[192,144], script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                build_options={k:getattr(bpy.app.build_options,k) for k in dir(bpy.app.build_options)
                               if not k.startswith('_') and isinstance(getattr(bpy.app.build_options,k),bool)})
(out/'start.json').write_text(json.dumps(metadata,indent=2)+'\n')
print('EEVEE renderer:',json.dumps(metadata),flush=True)

def render():
    bpy.ops.render.render(write_still=True)
    image = out/'render.png'
    assert image.is_file() and image.stat().st_size > 100
    metadata.update(completed=True, image_sha256=hashlib.sha256(image.read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print('PASS: Eevee render completed; compare retained image',flush=True)
    return None

bpy.app.timers.register(render, first_interval=1)
