"""Check continuity, static lower body, endpoints and wrist seams; render motion."""
import bpy, math, json
from pathlib import Path
from mathutils.kdtree import KDTree
out=Path(__file__).resolve().parents[1]/'build/shoulder-rub-20260916'
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
for sample in range(601):
    update(1+sample/4)
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
report={'samples':601,'maxQuarterFrameDegrees':maxStep,'unanimatedDegrees':maxStatic,'translationChange':maxLoc,'scaleChange':maxScale,'endpointDegrees':endpoint,'wristSeamPairs':len(seams),'maxWristSeamGap':maxGap,'scope':'Reference Blender rig only; armor and in-game playback untested.'}
(out/'validation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
assert maxStep<5 and maxStatic<.1 and endpoint<.1 and maxLoc<1e-5 and maxScale<1e-5 and maxGap<.1
s.render.resolution_x=480;s.render.resolution_y=544
for frame in range(1,152,3):
    update(frame);s.render.filepath=str(out/f'motion-{frame:03}.png');bpy.ops.render.render(write_still=True)
