"""Build the approved, static SVG layers. Animation is performed by the DLL."""
from pathlib import Path
import math,re,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
source=root/'assets/svg-source'
target=root/'package/Interface/WalkWithMe'
target.mkdir(parents=True,exist_ok=True)
def data(name):return ET.fromstring((source/(name+'.svg')).read_text()).find('{http://www.w3.org/2000/svg}path').attrib['d']
def write(name,path):
    text=f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512"><path fill="#ffffff" d="{path}"/></svg>'
    ET.fromstring(text)
    (target/(name+'.svg')).write_text(text,encoding='utf-8')
write('natural',data('footsteps'));write('shield',data('shield'));write('loot',data('locked-chest'))
parts=re.split(r'(?=M)',data('arrow'))[1:]
assert len(parts)==3
for name,path in zip(('arrow','wing-a','wing-b'),parts):write(name,path)
flame,tail=data('fire').split('M173.876',1)
ember,logs=tail.split('zm74.47',1)
write('flame',flame);write('ember','M173.876'+ember+'z')
write('logs','M248.346 317.218'+logs.split('269.094',1)[1])
def point(angle,r):return f'{256+r*math.cos(angle):.3f} {256+r*math.sin(angle):.3f}'
def sector(inset):
    inner,outer=240*.51+inset,240-inset
    start,end=math.radians(-119)+inset/180,math.radians(-61)-inset/180
    corner=7
    return (f'M{point(start+corner/inner,inner)} '
        f'A{inner:.3f} {inner:.3f} 0 0 1 {point(end-corner/inner,inner)} '
        f'Q{point(end,inner)} {point(end,inner+corner)} L{point(end,outer-corner)} '
        f'Q{point(end,outer)} {point(end-corner/outer,outer)} '
        f'A{outer:.3f} {outer:.3f} 0 0 0 {point(start+corner/outer,outer)} '
        f'Q{point(start,outer)} {point(start,outer-corner)} L{point(start,inner+corner)} '
        f'Q{point(start,inner)} {point(start+corner/inner,inner)} Z')
layers=[];previous=0
for inset,coverage in [(-2,.04),(-1.5,.13),(-1,.30),(-.5,.53),(0,.76),(.5,.92),(1,1)]:
    opacity=(coverage-previous)/(1-previous)
    layers.append(f'<path fill="#ffffff" fill-opacity="{opacity:.5f}" d="{sector(inset)}"/>')
    previous=coverage
(target/'wheel-slice.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">'+''.join(layers)+'</svg>',encoding='utf-8')
print('Built nine icon layers and rounded wheel slice:',sum(f.stat().st_size for f in target.glob('*.svg')),'bytes')
