"""Check continuity, static lower body, endpoints and wrist seams; render motion."""
import bpy, math, json
from pathlib import Path
from mathutils.kdtree import KDTree
out=Path(__file__).resolve().parents[1]/'build/shoulder-recovery-v5'
s=bpy.context.scene;r=bpy.data.objects['NPC Root [Root]:ARMATURE']
animated=set(json.loads((out/'authoring.json').read_text())['animatedBones'])
def update(t):s.frame_set(int(t),subframe=t%1);bpy.context.view_layer.update()
def vertices(name):
    o=bpy.data.objects[name].evaluated_get(bpy.context.evaluated_depsgraph_get())
    return [o.matrix_world@v.co for v in o.data.vertices]
def angle(a,b):return math.degrees(a.rotation_difference(b).angle)
update(1);base={p.name:(p.location.copy(),p.rotation_quaternion.copy(),p.scale.copy()) for p in r.pose.bones}
body=vertices('CBBE');hands=vertices('Hands');tree=KDTree(len(body))
for i,v in enumerate(body):tree.insert(v,i)
tree.balance();seams=[]
for i,v in enumerate(hands):
    _,j,d=tree.find(v)
    if d<.025:seams.append((i,j))
previous={};maxStep=maxStatic=maxLoc=maxScale=maxGap=0
leftArmSwing=0.0
for sample in range(781):
    update(1+sample/4)
    leftArmSwing=max(leftArmSwing,angle(base['NPC UpperArm.L'][1],r.pose.bones['NPC UpperArm.L'].rotation_quaternion))
    for p in r.pose.bones:
        loc,q,scale=base[p.name];current=p.rotation_quaternion.normalized()
        assert all(math.isfinite(x) for x in current)
        maxLoc=max(maxLoc,(p.location-loc).length);maxScale=max(maxScale,(p.scale-scale).length)
        if p.name not in animated:maxStatic=max(maxStatic,angle(q,current))
        if p.name in previous:maxStep=max(maxStep,angle(previous[p.name],current))
        previous[p.name]=current.copy()
    if sample%20==0:
        body=vertices('CBBE');hands=vertices('Hands');maxGap=max(maxGap,max((hands[h]-body[b]).length for h,b in seams))
endpoint=max(angle(base[p.name][1],p.rotation_quaternion) for p in r.pose.bones)
source=json.loads((out/'authoring.json').read_text())
contact=[v for v in source['samples'] if 1.6<=(v['frame']-1)/30<=4.1]
from mathutils import Vector,Quaternion
maxPalmGap=max((Vector(v['palm'])-Vector(v['surface'])).length for v in contact)
handRange=max(angle(Quaternion(a['handQuaternion']),Quaternion(b['handQuaternion'])) for a in contact for b in contact)
localWrists=[Vector(v['wrist'])-Vector(v['shoulder']) for v in contact]
wristTravel=[max(v[i] for v in localWrists)-min(v[i] for v in localWrists) for i in range(3)]
rollRange=max(v['wristRollDegrees'] for v in contact)-min(v['wristRollDegrees'] for v in contact)
report={'samples':781,'maxQuarterFrameDegrees':maxStep,'leftUpperArmSwingDegrees':leftArmSwing,'unanimatedDegrees':maxStatic,'translationChange':maxLoc,'scaleChange':maxScale,'endpointDegrees':endpoint,'wristSeamPairs':len(seams),'maxWristSeamGap':maxGap,'palmProxyMaxDistanceToSurface':maxPalmGap,'handOrientationRangeDegrees':handRange,'wristRollRangeDegrees':rollRange,'wristTravelRelativeShoulderXYZ':wristTravel,'scope':'Reference Blender rig and palm proxy only; armor and in-game playback untested.'}
(out/'validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
assert maxStep<5 and maxStatic<.1 and endpoint<.1 and maxLoc<1e-5 and maxScale<1e-5 and maxGap<.1
assert 8<leftArmSwing<18,'Left arm should show a modest shoulder stretch'
assert maxPalmGap<1.05 and 2<handRange<25 and 1<rollRange<15
prior=json.loads((out.parent/'shoulder-rub-orbit-v3/validation.json').read_text())
assert handRange<prior['handOrientationRangeDegrees']*.5 and rollRange<prior['wristRollRangeDegrees']*.3
assert max(wristTravel)<max(prior['wristTravelRelativeShoulderXYZ'])*.6
s.render.resolution_x=480;s.render.resolution_y=544
for frame in range(1,197):
    update(frame);s.render.filepath=str(out/f'motion-{frame:03}.png');bpy.ops.render.render(write_still=True)
# Include the whole left arm so its shoulder circle is easy to judge.
camera=s.camera;camera.data.ortho_scale=70
target=Vector((-13,0,97));camera.location=target+Vector((-85,145,35));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
s.render.resolution_x=480;s.render.resolution_y=480
for frame in range(43,130):
    update(frame);s.render.filepath=str(out/f'detail-{frame:03}.png');bpy.ops.render.render(write_still=True)
