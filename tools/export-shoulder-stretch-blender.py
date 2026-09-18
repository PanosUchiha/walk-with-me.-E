"""Export the isolated shoulder-rub draft; never install or register an idle."""
import bpy,sys,json,math,hashlib
from pathlib import Path
from mathutils import Quaternion
sys.path.insert(0,str(Path.home()/'AppData/Roaming/Blender Foundation/Blender/5.2/scripts/addons'))
from io_scene_nifly.hkx.import_hkx import extract_fo4_animation
from io_scene_nifly.hkx.anim_skyrim import write_skyrim_animation,load_skyrim_animation
out=Path(__file__).resolve().parents[1]/'build/shoulder-recovery-v5'
r=bpy.data.objects['NPC Root [Root]:ARMATURE'];s=bpy.context.scene
bpy.ops.object.select_all(action='DESELECT');r.select_set(True);bpy.context.view_layer.objects.active=r
assert r.animation_data.action.name=='WWM_PostCombat_ShoulderRecovery_v5'
s.render.fps=30
anim=extract_fo4_animation(r,fps=30)
assert anim and anim.num_tracks==99 and anim.num_frames==196 and not anim.annotations and anim.blend_hint==0
anim.max_frames_per_block=256;anim.num_blocks=1;anim.block_duration=255/30
for track in anim.tracks:
    for attr in ('translations','scales'):
        values=getattr(track,attr)
        assert max(abs(a-b) for row in values for a,b in zip(row,values[0]))<.001
        setattr(track,attr,[list(values[0]) for _ in values])
    previous=None
    for row in track.rotations:
        if previous and sum(a*b for a,b in zip(row,previous))<0:row[:]=[-x for x in row]
        previous=row
path=out/'wwm_postcombat_shoulder_recovery_v5.hkx'
write_skyrim_animation(str(path),anim,ptr_size=8)
decoded=load_skyrim_animation(str(path));error=0
assert decoded.num_frames==196 and decoded.num_tracks==99 and abs(decoded.duration-6.5)<.001
for a,b in zip(anim.tracks,decoded.tracks):
    for qa,qb in zip(a.rotations,b.rotations):
        qa=Quaternion((qa[3],*qa[:3]));qb=Quaternion((qb[3],*qb[:3]))
        error=max(error,math.degrees(2*math.acos(min(1,abs(qa.normalized().dot(qb.normalized()))))))
assert error<.3,error
report={'path':str(path),'frames':anim.num_frames,'tracks':anim.num_tracks,'duration':anim.duration,'maxRoundTripDegrees':error,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'installed':False,'runtimeRegistered':False}
(out/'export.json').write_text(json.dumps(report,indent=2))
(out/'clip-source.json').write_text(json.dumps({'bones':anim.bone_names,'rotations':[t.rotations for t in anim.tracks]}))
print(json.dumps(report,indent=2))
