"""Layered post-combat recovery: breath, torso, clavicles, shoulder rub and nod."""
import bpy, math, json
from pathlib import Path
from mathutils import Vector, Matrix, Quaternion

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/shoulder-rub-natural-20260916'
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
def pulse(t,start,peak,end):
    return smooth((t-start)/(peak-start))*(1-smooth((t-peak)/(end-peak)))
def worldTurn(name,x=0,y=0,z=0):
    p=r.pose.bones[name]
    q=Quaternion(Vector((0,0,1)),math.radians(z))@Quaternion(Vector((0,1,0)),math.radians(y))@Quaternion(Vector((1,0,0)),math.radians(x))
    rotate(p,q@orient(p))
u,f,h=[r.pose.bones['NPC '+n+'.R'] for n in ('UpperArm','Forearm','Hand')]
start=point(h).copy()
neutralPole=(point(f)-point(u)).normalized()
lengths=((point(f)-point(u)).length,(point(h)-point(f)).length)
head=r.pose.bones['NPC Head'];head0=r.matrix_world@head.matrix
r.animation_data.action=None
a=bpy.data.actions.new('WWM_PostCombat_ShoulderRub_Natural_v2');a.use_fake_user=True
r.animation_data.action=a
animated={'NPC '+n+'.R' for n in ('UpperArm','Forearm','Hand','ForearmTwist1','ForearmTwist2')}
animated|={f'NPC Finger{d}{j}.R' for d in range(5) for j in range(3)}
animated|={'NPC Head','NPC Neck','NPC Spine','NPC Spine1','NPC Spine2','NPC Clavicle.L','NPC Clavicle.R','NPC UpperArm.L','NPC Forearm.L','NPC Hand.L'}
previous={};samples=[]
for frame in range(1,197):
    s.frame_set(frame)
    for p in r.pose.bones:
        p.rotation_mode='QUATERNION';p.location,p.rotation_quaternion,p.scale=base[p.name]
    update();t=(frame-1)/30
    weight=smooth((t-.28)/1.22)*(1-smooth((t-4.48)/1.57))
    # Two unequal pressure strokes, each followed by a lighter return. Avoid a
    # metronomic loop: the second is smaller and slower, with a settling pause.
    rub1=pulse(t,1.58,1.94,2.42)
    rub2=pulse(t,2.48,2.93,3.58)
    pressure=rub1+.72*rub2
    stroke=2.0*rub1+1.45*rub2
    inhale=pulse(t,0,.82,2.12)
    exhale=pulse(t,1.8,3.85,6.5)
    recoveryBreath=pulse(t,4.45,5.25,6.5)
    bodyTurn=pulse(t,.12,1.46,5.85)
    shoulderLift=pulse(t,.4,1.55,4.78)
    release=pulse(t,4.4,5.15,6.4)
    nod=pulse(t,3.72,4.13,4.68)
    glance=pulse(t,.6,1.45,3.55)
    # Motion is distributed through the thorax, rather than hinging the neck.
    worldTurn('NPC Spine',x=.7*inhale-.7*exhale,y=.6*bodyTurn,z=.65*bodyTurn)
    worldTurn('NPC Spine1',x=1.2*inhale-1.3*exhale+.5*recoveryBreath,y=1.1*bodyTurn,z=1.2*bodyTurn)
    worldTurn('NPC Spine2',x=1.4*inhale-1.6*exhale+.7*recoveryBreath,y=.9*bodyTurn,z=1.8*bodyTurn-.45*pressure)
    worldTurn('NPC Clavicle.L',x=-1.5*shoulderLift,y=5.2*shoulderLift+.8*pressure,z=-1.5*bodyTurn)
    worldTurn('NPC Clavicle.R',x=-2.3*weight,y=-4.8*weight,z=4.2*weight)
    # Free arm follows the chest with a delayed, smaller counter-swing.
    freeSwing=pulse(t,.55,1.9,5.5)
    worldTurn('NPC UpperArm.L',x=-1.8*freeSwing+1.2*release,y=-.8*freeSwing,z=-1.1*freeSwing)
    worldTurn('NPC Forearm.L',x=.8*freeSwing-.6*release)
    worldTurn('NPC Hand.L',x=.55*freeSwing,y=.5*release)
    worldTurn('NPC Neck',x=-2.7*nod-.7*pressure,y=-1.4*glance,z=2.4*glance-1.3*bodyTurn)
    worldTurn('NPC Head',x=-7.8*nod-1.2*glance,y=-2.0*glance,z=4.0*glance-1.6*bodyTurn)
    shoulder=point(r.pose.bones['NPC UpperArm.L'])
    contact=shoulder+Vector((7.0+.55*pressure,11.0-.6*pressure,-2.1-stroke))
    # Follow the free relaxed wrist reference as the thorax shifts, then reach.
    relaxed=point(h).copy()
    target=relaxed.lerp(contact,weight)
    # Bow the reaching wrist forward to clear the torso during both transitions.
    target.y+=17*math.sin(math.pi*weight)
    axis=(target-point(u)).normalized();distance=(target-point(u)).length;aa,bb=lengths
    distance=max(abs(aa-bb)+.01,min(distance,aa+bb-.01));target=point(u)+axis*distance
    along=(aa*aa-bb*bb+distance*distance)/(2*distance)
    pole=neutralPole.lerp(Vector((.6+.05*pressure,1,-.9-.12*pressure)).normalized(),weight);bend=(pole-axis*pole.dot(axis)).normalized()
    elbow=point(u)+axis*along+bend*math.sqrt(max(0,aa*aa-along*along))
    rotate(u,(point(f)-point(u)).rotation_difference(elbow-point(u))@orient(u))
    rotate(f,(point(h)-point(f)).rotation_difference(target-point(f))@orient(f))
    long=((point(r.pose.bones['NPC Finger10.R'])+point(r.pose.bones['NPC Finger40.R']))/2-point(h)).normalized()
    across=point(r.pose.bones['NPC Finger10.R'])-point(r.pose.bones['NPC Finger40.R'])
    normal=across.cross(long).normalized()
    desiredLong=Vector((-.70,-.58,.42-.05*pressure)).normalized()
    desiredNormal=Vector((-.1-.04*pressure,-.55,-.85));desiredNormal=(desiredNormal-desiredLong*desiredNormal.dot(desiredLong)).normalized()
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
    curlAxis=desiredLong.cross(desiredNormal).normalized()
    for d in range(1,5):
        for j in range(3):
            p=r.pose.bones[f'NPC Finger{d}{j}.R']
            delay=(d-1)*.025
            fingerWeight=smooth((t-.85-delay)/.65)*(1-smooth((t-4.25-delay)/.55))
            curls=((6,11,8),(8,14,9),(10,16,11),(12,18,12))
            rotate(p,Quaternion(curlAxis,math.radians((curls[d-1][j]+2*pressure)*fingerWeight))@orient(p))
    # Thumb settles independently around the shoulder, then lets go first.
    for j in range(2):
        p=r.pose.bones[f'NPC Finger0{j}.R']
        worldTurn(p.name,x=(3+j*2)*pulse(t,1.0,1.65,4.65))
    if frame in (1,196):
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
ellipsoid('Preview mannequin head',(0,-.7,123.5),(6.4,6.0,9.4),skin)
ellipsoid('Preview neck',(0,-1.8,111.5),(3.5,3.5,5),skin)
ellipsoid('Preview nose',(0,5.1,122.5),(1.05,1.9,1.7),skin)
for x in (-2.6,2.6):ellipsoid('Preview eye',(x,4.6,125.5),(1.0,.55,.5),(.035,.065,.075,1))
for name in ('CBBE','Hands'):bpy.data.objects[name].color=skin
camData=bpy.data.cameras.new('ShoulderRub_Review');cam=bpy.data.objects.new('ShoulderRub_Review',camData);s.collection.objects.link(cam)
camData.type='ORTHO';camData.ortho_scale=77;camData.clip_end=1500
target=Vector((0,0,108));cam.location=target+Vector((58,175,25));cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();s.camera=cam
s.render.engine='BLENDER_WORKBENCH';s.render.resolution_x=600;s.render.resolution_y=680;s.render.resolution_percentage=100
s.render.image_settings.file_format='PNG';s.view_settings.view_transform='Standard'
sh=s.display.shading;sh.color_type='OBJECT';sh.light='STUDIO';sh.show_shadows=True;sh.show_cavity=True;sh.background_type='WORLD';s.world.color=(.035,.045,.055);s.display.render_aa='16'
s.frame_start=1;s.frame_end=196;s.render.fps=30
for marker in list(s.timeline_markers):s.timeline_markers.remove(marker)
for label,frame in [('Inhale / anticipation',1),('Reach',22),('Shoulder contact',46),('Pressure stroke 1',59),('Pressure stroke 2',89),('Exhale / nod',125),('Fingers release',136),('Arm follows',157),('Settle',182),('Relaxed end',196)]:s.timeline_markers.new(label,frame=frame)
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.shading.color_type='OBJECT'
readme=bpy.data.texts.new('ShoulderRub_README');readme.write(__doc__+'\n6.5 seconds at 30 FPS. Unequal shoulder rubs; chest breathing, clavicle response, counter-swing, exhale nod and staggered release.\nHead geometry is a preview proxy. Source actions retained. No runtime installation.\n')
s.frame_set(70);bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'shoulder-rub-natural-v2.blend'))
for label,frame in [('neutral',1),('reach',28),('contact',59),('nod',125),('release',166)]:
    s.frame_set(frame);s.render.filepath=str(OUT/(label+'.png'));bpy.ops.render.render(write_still=True)
for view,offset in [('front',(0,175,15)),('side',(-175,25,15))]:
    cam.location=target+Vector(offset);cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
    s.frame_set(70);s.render.filepath=str(OUT/(view+'.png'));bpy.ops.render.render(write_still=True)
(OUT/'authoring.json').write_text(json.dumps({'blend':str(OUT/'shoulder-rub-natural-v2.blend'),'action':a.name,'fps':30,'frames':196,'installed':False,'animatedBones':sorted(animated),'samples':samples},indent=2))
