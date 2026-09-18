"""Editable sheathed post-combat shoulder rub and nod. Draft, not installed."""
import bpy, math, json
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/shoulder-rub-20260916'
OUT.mkdir(parents=True,exist_ok=True)
s=bpy.context.scene
r=bpy.data.objects['NPC Root [Root]:ARMATURE']
r.animation_data.action=bpy.data.actions['WWM_Lead_RelaxedRest_v2']
s.frame_set(1);bpy.context.view_layer.update()
base={p.name:(p.location.copy(),p.rotation_quaternion.copy(),p.scale.copy()) for p in r.pose.bones}
def update(): bpy.context.view_layer.update()
def point(p): return r.matrix_world@p.matrix.translation
def orient(p): return r.matrix_world.to_quaternion()@p.matrix.to_quaternion()
def rotate(p,q):
    desired=(r.matrix_world.to_quaternion().inverted()@q).to_matrix().to_4x4()
    desired.translation=p.matrix.translation
    local=p.bone.convert_local_to_pose(desired,p.bone.matrix_local,parent_matrix=p.parent.matrix,parent_matrix_local=p.parent.bone.matrix_local,invert=True)
    p.rotation_quaternion=local.to_quaternion().normalized();update()
def smooth(t):
    t=max(0,min(1,t));return max(0,min(1,t*t*t*(t*(t*6-15)+10)))
u,f,h=[r.pose.bones['NPC '+n+'.R'] for n in ('UpperArm','Forearm','Hand')]
start=point(h).copy()
neutralPole=(point(f)-point(u)).normalized()
lengths=((point(f)-point(u)).length,(point(h)-point(f)).length)
head=r.pose.bones['NPC Head'];head0=r.matrix_world@head.matrix
r.animation_data.action=None
a=bpy.data.actions.new('WWM_PostCombat_ShoulderRub_Nod_v1');a.use_fake_user=True
r.animation_data.action=a
animated={'NPC '+n+'.R' for n in ('UpperArm','Forearm','Hand','ForearmTwist1','ForearmTwist2')}
animated|={f'NPC Finger{d}{j}.R' for d in range(5) for j in range(3)}
animated|={'NPC Head','NPC Neck','NPC Spine2'}
previous={};samples=[]
for frame in range(1,152):
    s.frame_set(frame)
    for p in r.pose.bones:
        p.rotation_mode='QUATERNION';p.location,p.rotation_quaternion,p.scale=base[p.name]
    update();t=(frame-1)/30
    weight=smooth(t/1.25)*(1-smooth((t-3.45)/1.55))
    rub=math.sin(2*math.pi*(t-1.25)/1.05)*smooth((t-1.25)/.25)*(1-smooth((t-3.1)/.35)) if 1.25<=t<=3.45 else 0
    nod=math.sin(math.pi*max(0,min(1,(t-2.45)/.8)))**2 if 2.45<t<3.25 else 0
    for name,angle in [('NPC Spine2',-1.5*weight),('NPC Neck',-2*nod),('NPC Head',-9*nod)]:
        p=r.pose.bones[name];rotate(p,Quaternion(Vector((1,0,0)),math.radians(angle))@orient(p))
    shoulder=point(r.pose.bones['NPC UpperArm.L'])
    contact=shoulder+Vector((7.0,11.0,-3.0+1.35*rub))
    target=start.lerp(contact,weight)
    # Bow the reaching wrist forward to clear the torso during both transitions.
    target.y+=9*math.sin(math.pi*weight)
    axis=(target-point(u)).normalized();distance=(target-point(u)).length;aa,bb=lengths
    distance=max(abs(aa-bb)+.01,min(distance,aa+bb-.01));target=point(u)+axis*distance
    along=(aa*aa-bb*bb+distance*distance)/(2*distance)
    pole=neutralPole.lerp(Vector((.6,1,-.9)).normalized(),weight);bend=(pole-axis*pole.dot(axis)).normalized()
    elbow=point(u)+axis*along+bend*math.sqrt(max(0,aa*aa-along*along))
    rotate(u,(point(f)-point(u)).rotation_difference(elbow-point(u))@orient(u))
    rotate(f,(point(h)-point(f)).rotation_difference(target-point(f))@orient(f))
    long=((point(r.pose.bones['NPC Finger10.R'])+point(r.pose.bones['NPC Finger40.R']))/2-point(h)).normalized()
    across=point(r.pose.bones['NPC Finger10.R'])-point(r.pose.bones['NPC Finger40.R'])
    normal=across.cross(long).normalized()
    desiredLong=Vector((-.70,-.58,.42)).normalized()
    desiredNormal=Vector((-.1,-.55,-.85));desiredNormal=(desiredNormal-desiredLong*desiredNormal.dot(desiredLong)).normalized()
    src=Matrix((long,normal,long.cross(normal))).transposed()
    dst=Matrix((desiredLong,desiredNormal,desiredLong.cross(desiredNormal))).transposed()
    handQ=(dst@src.transposed()).to_quaternion()@orient(h)
    desiredHand=orient(h).slerp(handQ,weight)
    delta=desiredHand@orient(h).inverted()
    if delta.w<0:delta.negate()
    foreAxis=(point(h)-point(f)).normalized()
    twist=2*math.atan2(Vector((delta.x,delta.y,delta.z)).dot(foreAxis),delta.w)
    helpers={j:orient(r.pose.bones[f'NPC ForearmTwist{j}.R']) for j in (1,2)}
    rotate(f,Quaternion(foreAxis,twist*.85)@orient(f))
    for j,fraction in ((1,.82),(2,.45)):
        rotate(r.pose.bones[f'NPC ForearmTwist{j}.R'],Quaternion(foreAxis,twist*fraction)@helpers[j])
    rotate(h,desiredHand)
    for d in range(1,5):
        for j in range(3):
            p=r.pose.bones[f'NPC Finger{d}{j}.R']
            p.rotation_quaternion=base[p.name][1]@Quaternion(Vector((1,0,0)),math.radians((8 if j==0 else 12)*weight))
    if frame in (1,151):
        for p in r.pose.bones: p.location,p.rotation_quaternion,p.scale=base[p.name]
    for name in animated:
        p=r.pose.bones[name];q=p.rotation_quaternion
        if name in previous and q.dot(previous[name])<0:q.negate()
        previous[name]=q.copy();p.keyframe_insert('rotation_quaternion',frame=frame,group=name)
    update();samples.append({'frame':frame,'wrist':list(point(h)),'weight':weight})
for layer in a.layers:
    for strip in layer.strips:
        for bag in strip.channelbags:
            for fc in bag.fcurves:
                for k in fc.keyframe_points:k.interpolation='LINEAR'
s.frame_set(1);update()
# A simple non-game mannequin head makes the nod legible; the imported fit body
# contains no head mesh. It follows the actual animated head bone.
anchor=bpy.data.objects.new('Preview_Head_Anchor',None);s.collection.objects.link(anchor)
anchor.matrix_world=head0
constraint=anchor.constraints.new('COPY_TRANSFORMS');constraint.target=r;constraint.subtarget='NPC Head'
def ellipsoid(name,location,scale,color):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=20,location=location)
    obj=bpy.context.object;obj.name=name;obj.scale=scale;obj.color=color
    for p in obj.data.polygons:p.use_smooth=True
    update();mat=obj.matrix_world.copy();obj.parent=anchor;obj.matrix_world=mat;update()
    return obj
skin=(.36,.51,.56,1)
ellipsoid('Preview mannequin head',(0,-.7,130),(6.4,6.0,9.4),skin)
ellipsoid('Preview neck',(0,-1.8,117),(3.5,3.5,8),skin)
ellipsoid('Preview nose',(0,5.1,129),(1.05,1.9,1.7),skin)
for x in (-2.6,2.6):ellipsoid('Preview eye',(x,4.6,132),(1.0,.55,.5),(.035,.065,.075,1))
for name in ('CBBE','Hands'):bpy.data.objects[name].color=skin
camData=bpy.data.cameras.new('ShoulderRub_Review');cam=bpy.data.objects.new('ShoulderRub_Review',camData);s.collection.objects.link(cam)
camData.type='ORTHO';camData.ortho_scale=77;camData.clip_end=1500
target=Vector((0,0,108));cam.location=target+Vector((58,175,25));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();s.camera=cam
s.render.engine='BLENDER_WORKBENCH';s.render.resolution_x=600;s.render.resolution_y=680;s.render.resolution_percentage=100
s.render.image_settings.file_format='PNG';s.view_settings.view_transform='Standard'
sh=s.display.shading;sh.color_type='OBJECT';sh.light='STUDIO';sh.show_shadows=True;sh.show_cavity=True;sh.background_type='WORLD';s.world.color=(.035,.045,.055);s.display.render_aa='16'
s.frame_start=1;s.frame_end=151;s.render.fps=30
for marker in list(s.timeline_markers):s.timeline_markers.remove(marker)
for label,frame in [('Relaxed',1),('Shoulder contact',39),('Rub 1',55),('Rub 2',85),('Nod',87),('Release',105),('Relaxed end',151)]:s.timeline_markers.new(label,frame=frame)
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.shading.color_type='OBJECT'
readme=bpy.data.texts.new('ShoulderRub_README');readme.write(__doc__+'\n5 seconds at 30 FPS. Right hand rubs left shoulder; one subtle nod.\nHead geometry is a preview proxy. Source actions retained. No runtime installation.\n')
s.frame_set(70);bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'shoulder-rub-nod-v1.blend'))
for label,frame in [('neutral',1),('reach',25),('contact',55),('nod',87),('release',125)]:
    s.frame_set(frame);s.render.filepath=str(OUT/(label+'.png'));bpy.ops.render.render(write_still=True)
for view,offset in [('front',(0,175,15)),('side',(-175,25,15))]:
    cam.location=target+Vector(offset);cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
    s.frame_set(70);s.render.filepath=str(OUT/(view+'.png'));bpy.ops.render.render(write_still=True)
(OUT/'authoring.json').write_text(json.dumps({'blend':str(OUT/'shoulder-rub-nod-v1.blend'),'action':a.name,'fps':30,'frames':151,'installed':False,'animatedBones':sorted(animated),'samples':samples},indent=2))
