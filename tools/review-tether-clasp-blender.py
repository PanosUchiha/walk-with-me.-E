import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path.home()/'AppData/Roaming/Blender Foundation/Blender/5.2/scripts/addons'))
from io_scene_nifly.hkx import import_hkx
from io_scene_nifly.hkx.anim_skyrim import load_skyrim_animation
root=Path(__file__).resolve().parents[1];out=root/'build/tether-r18-pose'
poses=json.loads((out/'clasp-rotations.json').read_text())
scene=bpy.context.scene
report=[]
for name,side in [('WWM_Clasp_Player','R'),('WWM_Clasp_Partner','L')]:
 r=bpy.data.objects[name]
 clip=out/'OAR'/('Right' if side=='R' else 'Left')/'actors/character/animations/female/mt_walkforward.hkx'
 data=load_skyrim_animation(str(clip))
 import_hkx.apply_fo4_animation(r,data,poses[side]['bones'],'WWM_Tether_Walk_'+side,30,rename_bones=True,bone_dict=import_hkx.skyrimDict)
scene.frame_set(8);bpy.context.view_layer.update()
for name,side in [('WWM_Clasp_Player','R'),('WWM_Clasp_Partner','L')]:
 r=bpy.data.objects[name]
 point=lambda n:r.matrix_world@r.pose.bones[n].matrix.translation
 hand=point('NPC Hand.'+side)
 across=point('NPC Finger10.'+side)-point('NPC Finger40.'+side)
 along=(point('NPC Finger10.'+side)+point('NPC Finger40.'+side))*.5-hand
 normal=across.cross(along).normalized()*(-1 if side=='L' else 1)
 report.append({'actor':name,'wrist':list(hand),'palmNormal':list(normal)})
angle=math.degrees(math.acos(max(-1,min(1,Vector(report[0]['palmNormal']).dot(Vector(report[1]['palmNormal']))))))
assert angle>150,angle
target=(Vector(report[0]['wrist'])+Vector(report[1]['wrist']))*.5+Vector((0,0,-3))
camera=scene.camera;camera.location=target+Vector((35,-65,20));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=32
scene.render.resolution_x=960;scene.render.resolution_y=720;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.render.filepath=str(out/'decoded-walk-pose.png')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'decoded-walk-pose.blend'))
bpy.ops.render.render(write_still=True)
(out/'pose-review.json').write_text(json.dumps({'opposingPalmDegrees':angle,'actors':report,'note':'Decoded HKX pose on reference female meshes. Physics not simulated; not in-game proof.'},indent=2))
print('POSE_REVIEW',angle)

