"""Make the dedicated GPMA state part of normal Pandora/Nemesis generation.

Overrides only GPMA's state list and adds six objects. The existing enabled
GPMA patch discovers these files through MO2; no extra checkbox is required.
"""
from pathlib import Path
import importlib.util,xml.etree.ElementTree as ET,json,hashlib
from animation_package import nemesis_text, verify_patch
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('overlay',root/'tools/patch-command-overlay.py')
overlay=importlib.util.module_from_spec(spec);spec.loader.exec_module(overlay)
source=root.parent/'MODS/mods/Nemesis - Offset Movement Animation/Nemesis_Engine/mod/gpma/0_master'
out=root/'package/Nemesis_Engine/mod/gpma/0_master';out.mkdir(parents=True,exist_ok=True)
character=root.parent/'MODS/mods/Pandora Output/meshes/actors/character'
mask=overlay.upper_body_mask(character/'Characters/DefaultMale.hkx')
assert mask==overlay.upper_body_mask(character/'Characters Female/DefaultFemale.hkx')
assert len(mask)==99 and mask[:24]==[0]*24
def load(i):return ET.fromstring((source/f'#gpma${i}.txt').read_text())
def setp(obj,name,value):obj.find(f"hkparam[@name='{name}']").text=str(value)
def write(obj,i):
    obj.set('name',f'#gpma${i}')
    path=out/f'#gpma${i}.txt';path.write_text(nemesis_text(obj))
    return path
files=[]
for index,values in [(37,[1-x for x in mask]),(38,mask)]:
    obj=load(18);p=obj.find("hkparam[@name='boneWeights']");p.set('numelements','99')
    p.text=' '.join(f'{v:.6f}' for v in values);files.append(write(obj,index))
for index,generator,weights,root_motion in [(39,'#0340',37,1),(40,'#gpma$16',38,0)]:
    obj=load(19);setp(obj,'generator',generator);setp(obj,'boneWeights',f'#gpma${weights}')
    setp(obj,'worldFromModelWeight',f'{root_motion:.6f}');files.append(write(obj,index))
obj=load(20);setp(obj,'name',overlay.MARKER.decode());setp(obj,'children','#gpma$39 #gpma$40');files.append(write(obj,41))
obj=load(28);setp(obj,'name','WWM_CommandUpperBody_State');setp(obj,'stateId',3);setp(obj,'generator','#gpma$41');files.append(write(obj,42))
obj=load(30);p=obj.find("hkparam[@name='states']");assert p.get('numelements')=='3'
p.set('numelements','4');p.text=p.text.strip()+' #gpma$42';files.append(write(obj,30))
for path in files:
    obj=ET.parse(path).getroot()
    for p in obj.findall('hkparam'):
        if 'numelements' in p.attrib and not list(p):assert len((p.text or '').split())==int(p.get('numelements'))
verify_patch(out)
report={str(p.relative_to(root/'package')):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
audit=root/'build/gesture-generator-fix';audit.mkdir(exist_ok=True)
(audit/'generator-source.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
