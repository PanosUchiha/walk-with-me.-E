"""Shared release layout and Nemesis source-format checks."""
import re
import xml.etree.ElementTree as ET

GESTURE_PREFIXES = ('Nemesis_Engine/', 'meshes/OpenAnimationReplacer/Walk With Me Signals/')


def split_gestures(files):
    gestures = {name: value for name, value in files.items() if name.startswith(GESTURE_PREFIXES)}
    base = {name: value for name, value in files.items() if name not in gestures}
    if not any(name.startswith('Nemesis_Engine/') for name in gestures):
        raise ValueError('Missing gesture behavior patch')
    if not any(name.endswith('.hkx') for name in gestures):
        raise ValueError('Missing gesture clips')
    return base, gestures


def nemesis_text(obj):
    # Nemesis consumes lines and indentation, not just XML semantics. Preserve
    # Havok's two-tab object depth and separate counted arrays from their tags.
    ET.indent(obj, space='\t', level=2)
    for param in obj.iter('hkparam'):
        if 'numelements' in param.attrib and not list(param):
            values = (param.text or '').split()
            if len(values) != int(param.get('numelements')):
                raise ValueError('Incorrect array size: ' + param.get('name'))
            if values:
                param.text = '\n' + '\n'.join('\t' * 4 + ' '.join(values[i:i + 16])
                                              for i in range(0, len(values), 16)) + '\n' + '\t' * 3
    return '\t\t' + ET.tostring(obj, encoding='unicode', short_empty_elements=False) + '\n'


def verify_patch(folder):
    expected = {f'#gpma${i}.txt' for i in (30, 37, 38, 39, 40, 41, 42)}
    if {p.name for p in folder.glob('*.txt')} != expected:
        raise ValueError('Expected exactly seven WWM GPMA patch nodes')
    for path in folder.glob('*.txt'):
        text = path.read_text()
        node = ET.fromstring(text)
        if node.get('name') != path.stem or not text.startswith('\t\t<hkobject '):
            raise ValueError('Invalid Nemesis object layout: ' + str(path))
        for param in node.iter('hkparam'):
            if 'numelements' in param.attrib and not list(param):
                if len((param.text or '').split()) != int(param.get('numelements')):
                    raise ValueError('Invalid array count: ' + str(path))
        if re.search(r'numelements="[1-9][0-9]*">[^\n<]+</hkparam>', text):
            raise ValueError('Inline Nemesis array: ' + str(path))
    state = ET.parse(folder / '#gpma$30.txt').find("hkparam[@name='states']")
    if state.text.split() != ['#gpma$28', '#gpma$22', '#gpma$13', '#gpma$42']:
        raise ValueError('GPMA base states or WWM state missing')
