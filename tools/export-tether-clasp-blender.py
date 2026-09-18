import bpy,sys,json
from pathlib import Path
sys.path.insert(0,str(Path.home()/'AppData/Roaming/Blender Foundation/Blender/5.2/scripts/addons'))
from io_scene_nifly.hkx.import_hkx import extract_fo4_animation
out=Path(__file__).resolve().parents[1]/'build/tether-r18-pose';out.mkdir(exist_ok=True)
result={}
for name,side in [('WWM_Clasp_Player','R'),('WWM_Clasp_Partner','L')]:
 r=bpy.data.objects[name];a=extract_fo4_animation(r,fps=30)
 assert a.num_tracks==99 and a.num_frames==151
 selected={}
 for bone,track in zip(a.bone_names,a.tracks):
  if bone.startswith('NPC '+side+' ') and any(part in bone for part in ['UpperArm','Forearm','Hand','Finger']):
   selected[bone]=track.rotations[75]
 assert len(selected)>=18,(side,selected.keys())
 result[side]={'bones':a.bone_names,'rotations':selected}
(out/'clasp-rotations.json').write_text(json.dumps(result,indent=2))
print('EXPORTED',[(s,len(p['rotations'])) for s,p in result.items()])
