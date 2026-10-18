# Render an OBJ with Blender headless (Workbench, flat textured).
# Usage: blender -b -P blender_render.py -- level.obj /abs/out_prefix "x,y,z,yawdeg,pitchdeg" ...
# Camera coords are Blender metres (UE cm / 100, Y negated).
import bpy, sys, math, mathutils
argv=sys.argv[sys.argv.index('--')+1:]
obj_path, out_prefix = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.wm.obj_import(filepath=obj_path)
sc=bpy.context.scene
sc.render.engine='BLENDER_WORKBENCH'
sc.display.shading.light='FLAT'
sc.display.shading.color_type='TEXTURE'
sc.render.resolution_x=1280; sc.render.resolution_y=960
objs=[o for o in sc.objects if o.type=='MESH']
mn=mathutils.Vector((1e9,)*3); mx=mathutils.Vector((-1e9,)*3)
for o in objs:
    for c in o.bound_box:
        w=o.matrix_world@mathutils.Vector(c); mn=mathutils.Vector(map(min,mn,w)); mx=mathutils.Vector(map(max,mx,w))
print('bounds',mn,mx)
cam_data=bpy.data.cameras.new('cam'); cam=bpy.data.objects.new('cam',cam_data); sc.collection.objects.link(cam); sc.camera=cam
cam_data.clip_end=2000
# top ortho
ctr=(mn+mx)/2
cam_data.type='ORTHO'; cam_data.ortho_scale=max(mx.x-mn.x,mx.y-mn.y)*1.05
cam.location=(ctr.x,ctr.y,mx.z+50); cam.rotation_euler=(0,0,0)
sc.render.filepath=out_prefix+'_top.png'; bpy.ops.render.render(write_still=True)
# perspective views given as extra args: x,y,z,yawdeg,pitchdeg
cam_data.type='PERSP'; cam_data.lens=18
for i,spec in enumerate(argv[2:]):
    x,y,z,yaw,pitch=map(float,spec.split(','))
    cam.location=(x,y,z)
    cam.rotation_euler=(math.radians(90+pitch),0,math.radians(yaw-90))
    sc.render.filepath=f'{out_prefix}_view{i}.png'; bpy.ops.render.render(write_still=True)
