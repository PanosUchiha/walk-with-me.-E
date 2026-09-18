"""Build a separate, editable paired clasp study from the imported Skyrim rig.

Run in Blender --background --factory-startup with the existing gesture .blend.
Writes a new .blend and previews; never edits the input file or game assets.
This is a pose/transition study, not a replacement locomotion animation.
"""
import bpy
import math
import json
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/handholding-blender-20260915'
OUT.mkdir(parents=True, exist_ok=True)
scene = bpy.context.scene
source = bpy.data.objects['NPC Root [Root]:ARMATURE']
source.animation_data.action = bpy.data.actions['GPMAOffsetAnimation']
scene.frame_set(1)
bpy.context.view_layer.update()
originals = list(scene.objects)
base = {p.name: (p.location.copy(), p.rotation_quaternion.copy(), p.scale.copy()) for p in source.pose.bones}
for o in originals:
    o.hide_render = True
    o.hide_set(True)

def update():
    bpy.context.view_layer.update()

def point(r, p):
    return r.matrix_world @ p.matrix.translation

def orient(r, p):
    return r.matrix_world.to_quaternion() @ p.matrix.to_quaternion()

def rotate(r, p, q):
    desired = (r.matrix_world.to_quaternion().inverted() @ q).to_matrix().to_4x4()
    desired.translation = p.matrix.translation
    local = p.bone.convert_local_to_pose(desired, p.bone.matrix_local,
        parent_matrix=p.parent.matrix, parent_matrix_local=p.parent.bone.matrix_local, invert=True)
    p.rotation_quaternion = local.to_quaternion().normalized()
    update()

rigs = []
for i, side in enumerate(('R','L')):
    r = source.copy(); r.data = source.data.copy(); r.animation_data_clear()
    r.name = 'WWM_Clasp_Player' if i == 0 else 'WWM_Clasp_Partner'
    scene.collection.objects.link(r); r.hide_set(False); r.hide_render = False
    r.location.x += i * 40
    r.show_in_front = True
    rigs.append(r)
    for name in ('CBBE','Hands'):
        mesh = bpy.data.objects[name].copy()
        mesh.name = r.name + '_' + name
        scene.collection.objects.link(mesh)
        mesh.parent = None
        mesh.matrix_world = bpy.data.objects[name].matrix_world.copy()
        mesh.location.x += i * 40
        mesh.hide_render = False; mesh.hide_set(False)
        mesh.color = (.26,.49,.63,1) if i == 0 else (.69,.43,.27,1)
        for mod in mesh.modifiers:
            if mod.type == 'ARMATURE': mod.object = r
    for p in r.pose.bones:
        p.rotation_mode = 'QUATERNION'
        p.location, p.rotation_quaternion, p.scale = base[p.name]
update()

def reset(r):
    for p in r.pose.bones:
        p.location, p.rotation_quaternion, p.scale = base[p.name]
    update()

def arm(r, side):
    return [r.pose.bones['NPC '+n+'.'+side] for n in ('UpperArm','Forearm','Hand')]

def pose_pair(candidate=True, swing=0):
    for r in rigs: reset(r)
    arms = [arm(r, side) for r,side in zip(rigs,('R','L'))]
    grip = sum((point(r,b[0]) for r,b in zip(rigs,arms)), Vector()) / 2
    lengths = [[(point(r,b[j+1])-point(r,b[j])).length for j in range(2)] for r,b in zip(rigs,arms)]
    grip.z -= min(map(sum,lengths)) * (.92 if candidate else .76)
    grip.y += 4 + swing
    for i,(r,side,bones,lens) in enumerate(zip(rigs,('R','L'),arms,lengths)):
        u,f,h = bones
        target = grip + (Vector((-.5 if i==0 else .5, -2.35 if i==0 else 2.35, .4 if i==0 else -.4)) if candidate else Vector((-2 if i==0 else 2,0,0)))
        s,e,w = [point(r,p) for p in bones]
        axis = (target-s).normalized(); d=(target-s).length; a,b=lens
        assert abs(a-b)+.01 < d < a+b-.01
        along = (a*a-b*b+d*d)/(2*d)
        pole = Vector((0,-30,0))
        bend = (pole-axis*pole.dot(axis)).normalized()
        elbow = s+axis*along+bend*math.sqrt(a*a-along*along)
        rotate(r,u,(e-s).rotation_difference(elbow-s)@orient(r,u))
        rotate(r,f,(point(r,h)-point(r,f)).rotation_difference(target-point(r,f))@orient(r,f))
        fingers = [[r.pose.bones['NPC Finger%d%d.%s'%(digit,j,side)] for j in range(3)] for digit in range(5)]
        long = ((point(r,fingers[1][0])+point(r,fingers[4][0]))*.5-point(r,h)).normalized()
        across = point(r,fingers[1][0])-point(r,fingers[4][0])
        normal = across.cross(long).normalized() * (-1 if i else 1)
        down = Vector((0,0,-1))
        palm = Vector((0,1 if i==0 else -1,0)) if candidate else Vector((1 if i==0 else -1,0,0))
        src = Matrix((long,normal,long.cross(normal))).transposed()
        dst = Matrix((down,palm,down.cross(palm))).transposed()
        delta = (dst@src.transposed()).to_quaternion()
        desired_hand = delta@orient(r,h)
        if candidate:
            # Spread axial roll along the skinned forearm rather than crushing
            # the entire rotation into the wrist seam.
            if delta.w < 0: delta.negate()
            fore_axis=(point(r,h)-point(r,f)).normalized()
            twist=2*math.atan2(Vector((delta.x,delta.y,delta.z)).dot(fore_axis),delta.w)
            helper_before={number:orient(r,r.pose.bones[f'NPC ForearmTwist{number}.{side}']) for number in (1,2)}
            rotate(r,f,Quaternion(fore_axis,twist*.85)@orient(r,f))
            for number,fraction in ((1,.82),(2,.45)):
                helper=r.pose.bones[f'NPC ForearmTwist{number}.{side}']
                rotate(r,helper,Quaternion(fore_axis,twist*fraction)@helper_before[number])
        rotate(r,h,desired_hand)
        # Existing reference pose has a slight bend already. Add a small,
        # graded curl, rather than .38 radians to every joint equally.
        curls = ((.10,.23,.10),(.12,.30,.13),(.16,.34,.16),(.19,.38,.19))
        curl_axis = down.cross(palm).normalized()
        for digit in range(1,5):
            for j,p in enumerate(fingers[digit]):
                amount = curls[digit-1][j] if candidate else .38
                rotate(r,p,Quaternion(curl_axis,amount)@orient(r,p))
        if candidate:
            # Lay the thumb across the index base. Keep its distal joint soft.
            for j in range(2):
                p,child = fingers[0][j:j+2]
                destination = point(r,fingers[1][0]) + down*(.8+j*.7) + palm*.9
                change = (point(r,child)-point(r,p)).rotation_difference(destination-point(r,p))
                rotate(r,p,Quaternion().slerp(change,.35)@orient(r,p))
    return grip

# Keep the current runtime geometry as independent reference actions.
pose_pair(False)
baseline_q = [{p.name:p.rotation_quaternion.copy() for p in r.pose.bones} for r in rigs]
for i,r in enumerate(rigs):
    r.animation_data_create(); a=bpy.data.actions.new(r.name+'_Revision11_Reference'); a.use_fake_user=True
    r.animation_data.action=a
    for p in r.pose.bones: p.keyframe_insert('rotation_quaternion',frame=1,group=p.name)

grip = pose_pair(True)
clasp_q = [{p.name:p.rotation_quaternion.copy() for p in r.pose.bones} for r in rigs]
for r in rigs: r.animation_data.action=None

# Bake a gentle reach, connected hold with shared swing, and release. Keep the
# torso, legs, root and the other arm at their original transforms.
actions=[]
for r in rigs:
    a=bpy.data.actions.new(r.name+'_SoftClasp_v1'); a.use_fake_user=True
    a['description']='Editable clasp study: reach 1-31, hold 31-121, release 121-151. Not live game locomotion.'
    r.animation_data.action=a; actions.append(a)
animated=[set([p.name for p in arm(r,side)])|{f'NPC Finger{d}{j}.{side}' for d in range(5) for j in range(3)}|{f'NPC ForearmTwist{j}.{side}' for j in (1,2)} for r,side in zip(rigs,('R','L'))]
previous=[{} for r in rigs]
for frame in range(1,152):
    scene.frame_set(frame)
    raw=min(1,(frame-1)/30,(151-frame)/30); raw=max(0,raw)
    weight=raw*raw*(3-2*raw)
    swing=.65*math.sin(2*math.pi*(frame-31)/90) if 31<=frame<=121 else 0
    pose_pair(True,swing)
    for i,r in enumerate(rigs):
        for p in r.pose.bones:
            q=base[p.name][1].slerp(p.rotation_quaternion,weight) if p.name in animated[i] else base[p.name][1].copy()
            if p.name in previous[i] and q.dot(previous[i][p.name])<0:q.negate()
            p.rotation_quaternion=q
            previous[i][p.name]=q.copy()
            if p.name in animated[i]:p.keyframe_insert('rotation_quaternion',frame=frame,group=p.name)
    update()
for a in actions:
    for layer in a.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for fc in bag.fcurves:
                    for key in fc.keyframe_points:key.interpolation='LINEAR'
scene.frame_start=1; scene.frame_end=151; scene.render.fps=30
for label,frame in [('Relaxed',1),('Contact',31),('Hold',76),('Release',121),('Relaxed end',151)]:
    scene.timeline_markers.new(label,frame=frame)
scene.frame_set(76);update()

camera_data=bpy.data.cameras.new('WWM_Clasp_Review')
camera=bpy.data.objects.new('WWM_Clasp_Review',camera_data);scene.collection.objects.link(camera)
camera_data.type='ORTHO';camera_data.ortho_scale=31;camera_data.clip_end=1000
camera.location=grip+Vector((22,65,7))
target=grip+Vector((0,0,-4))
camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
scene.camera=camera
scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=720;scene.render.resolution_y=720;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='Standard'
sh=scene.display.shading;sh.color_type='OBJECT';sh.light='STUDIO';sh.show_shadows=True;sh.show_cavity=True
sh.background_type='WORLD';scene.world.color=(.035,.042,.052)
scene.display.render_aa='16'
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.shading.color_type='OBJECT'
text=bpy.data.texts.new('WWM_Clasp_README')
text.write(__doc__+'\n\nSource actions are retained. Two SoftClasp actions animate the facing arms only.\nScrub 1-151; contact 31-121. Source body is a fit reference, not Rumarin\'s in-game mesh.\nGame currently uses procedural IK and does not load this .blend or these actions.\n')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'walk-with-me-soft-clasp-v1.blend'))
for view,offset in [('front',(0,65,7)),('back',(0,-65,7)),('three-quarter',(22,65,7))]:
    camera.location=grip+Vector(offset);camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    for name,qs in [('current',baseline_q),('soft',clasp_q)]:
        for i,r in enumerate(rigs):
            r.animation_data.action=None
            for p in r.pose.bones:p.rotation_quaternion=qs[i][p.name]
        update();scene.render.filepath=str(OUT/f'{name}-{view}.png');bpy.ops.render.render(write_still=True)
for r,a in zip(rigs,actions):r.animation_data.action=a
camera.location=grip+Vector((22,65,7));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
scene.render.resolution_x=480;scene.render.resolution_y=480
for frame in range(1,152,3):
    scene.frame_set(frame);update();scene.render.filepath=str(OUT/f'preview-{frame:03}.png');bpy.ops.render.render(write_still=True)
(OUT/'authoring.json').write_text(json.dumps({'blend':str(OUT/'walk-with-me-soft-clasp-v1.blend'),'frames':151,'fps':30,'actions':[a.name for a in actions],'gameInstalled':False},indent=2))
