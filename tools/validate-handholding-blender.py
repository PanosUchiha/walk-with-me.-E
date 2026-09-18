"""Check the authored pair's held contact, seams, and animation continuity."""
import bpy, math, json
from pathlib import Path
from mathutils.kdtree import KDTree

out=Path(__file__).resolve().parents[1]/'build/handholding-blender-20260915'
scene=bpy.context.scene
rigs=[bpy.data.objects[n] for n in ('WWM_Clasp_Player','WWM_Clasp_Partner')]
sides=('R','L')
def update(frame):
    scene.frame_set(int(frame),subframe=frame%1);bpy.context.view_layer.update()
def verts(name):
    o=bpy.data.objects[name].evaluated_get(bpy.context.evaluated_depsgraph_get())
    return [o.matrix_world@v.co for v in o.data.vertices]
def angle(a,b):
    return math.degrees(a.rotation_difference(b).angle)
update(1)
base=[{p.name:(p.location.copy(),p.rotation_quaternion.copy(),p.scale.copy()) for p in r.pose.bones} for r in rigs]
seams=[]
for r in rigs:
    body=verts(r.name+'_CBBE');hands=verts(r.name+'_Hands')
    tree=KDTree(len(body))
    for i,v in enumerate(body):tree.insert(v,i)
    tree.balance();pairs=[]
    for i,v in enumerate(hands):
        _,j,d=tree.find(v)
        if d<.025:pairs.append((i,j))
    assert pairs
    seams.append(pairs)
previous=[{} for r in rigs]
peak_step=0;peak_seam=0;max_static=0;max_translation=0;max_scale=0;wrist_angles=[0,0];gaps=[]
for sample in range(601):
    t=1+sample/4;update(t)
    for i,(r,side) in enumerate(zip(rigs,sides)):
        animated={'NPC '+part+'.'+side for part in ('UpperArm','Forearm','Hand','ForearmTwist1','ForearmTwist2')}
        animated|={f'NPC Finger{d}{j}.{side}' for d in range(5) for j in range(3)}
        for p in r.pose.bones:
            loc,q,scale=base[i][p.name];current=p.rotation_quaternion.normalized()
            assert all(math.isfinite(v) for v in current)
            max_translation=max(max_translation,(p.location-loc).length)
            max_scale=max(max_scale,(p.scale-scale).length)
            if p.name not in animated:max_static=max(max_static,angle(q,current))
            if p.name in previous[i]:peak_step=max(peak_step,angle(previous[i][p.name],current))
            previous[i][p.name]=current.copy()
        if 31<=t<=121:
            wrist_angles[i]=max(wrist_angles[i],angle(base[i]['NPC Hand.'+side][1],r.pose.bones['NPC Hand.'+side].rotation_quaternion))
        if sample%40==0:
            body=verts(r.name+'_CBBE');hands=verts(r.name+'_Hands')
            peak_seam=max(peak_seam,max((hands[h]-body[b]).length for h,b in seams[i]))
    if 31<=t<=121:
        wrists=[r.matrix_world@r.pose.bones['NPC Hand.'+s].matrix.translation for r,s in zip(rigs,sides)]
        gaps.append((wrists[1]-wrists[0]).length)
endpoint=max(angle(base[i][p.name][1],p.rotation_quaternion) for i,r in enumerate(rigs) for p in r.pose.bones)
report={'samples':601,'fps':30,'frames':151,'contactFrames':[31,121],'maxQuarterFrameRotationDegrees':peak_step,
        'maxUnanimatedBoneRotationDegrees':max_static,'maxBoneTranslation':max_translation,'maxBoneScaleChange':max_scale,
        'endpointRotationErrorDegrees':endpoint,'seamVertexPairs':[len(s) for s in seams],
        'maxBodyHandSeamGap':peak_seam,'heldWristLocalRotationDegrees':wrist_angles,
        'heldWristGapMin':min(gaps),'heldWristGapMax':max(gaps),
        'scope':'Blender imported reference body only. No live Skyrim, XPMSSE or Rumarin mesh validation.'}
(out/'validation.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
assert max_static<.1 and max_translation<1e-5 and max_scale<1e-5
assert endpoint<.1 and peak_step<5
assert peak_seam<.1
assert max(gaps)-min(gaps)<.02
