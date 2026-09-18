"""Structural checks for the authored ESP and staged release files. Run with Python 3."""
from pathlib import Path
import struct
import sys
import hashlib

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'package'
data = (root / 'Wayfarer.esp').read_bytes()

def u32(b, offset=0):
    return struct.unpack_from('<I', b, offset)[0]

def subrecords(b):
    offset, result = 0, []
    while offset < len(b):
        assert offset + 6 <= len(b), 'truncated subrecord'
        sig, size = struct.unpack_from('<4sH', b, offset)
        offset += 6
        assert offset + size <= len(b), 'invalid subrecord length'
        result.append((sig.decode(), b[offset:offset + size]))
        offset += size
    return result

def records(b, offset=0, end=None):
    end = len(b) if end is None else end
    while offset < end:
        assert offset + 24 <= end, 'truncated record'
        sig = b[offset:offset + 4].decode()
        size = u32(b, offset + 4)
        if sig == 'GRUP':
            assert size >= 24 and offset + size <= end, 'invalid group length'
            yield from records(b, offset + 24, offset + size)
            offset += size
        else:
            assert offset + 24 + size <= end, 'invalid record length'
            yield sig, u32(b, offset + 12), u32(b, offset + 8), subrecords(b[offset + 24:offset + 24 + size])
            offset += 24 + size

all_records = list(records(data))
assert len(all_records) == 90
header = all_records[0]
assert header[:3] == ('TES4', 0, 0x200), 'expected ESL header'
h = dict(header[3])
assert u32(h['HEDR'], 4) == 89 and u32(h['HEDR'], 8) == 0x887
assert h['MAST'] == b'Skyrim.esm\0'
def package_id(slot): return 0x01000800 + slot + (1 if slot >= 5 else 0)
def follower_alias(slot): return slot if slot < 5 else slot + 5
def marker_alias(slot): return slot + 5 if slot < 5 else slot + 10
for slot, record in enumerate(all_records[1:11]):
    sig, form, flags, subs = record
    fields = dict(subs)
    assert sig == 'PACK' and form == package_id(slot) and flags == 0
    assert u32(fields['QNAM']) == 0x01000805, 'alias destination must resolve in owning quest'
    assert u32(fields['PKDT']) == 0x2000, 'speed enabled; combat and facing flags absent'
    assert fields['PKDT'][4] == 18 and fields['PKDT'][6] == 0
    assert struct.unpack('<III', fields['PKCU']) == (3, 0x16FAA, 3), '0.2.1 Travel template'
    assert struct.unpack('<III', fields['PLDT']) == (8, marker_alias(slot), 65), 'Travel uses location alias and original arrival tolerance'
    assert [v for k,v in subs if k == 'CNAM'] == [b'\0',b'\0']
    assert [v for k,v in subs if k == 'UNAM'] == [bytes([v]) for v in (0,2,4)]
for slot, record in enumerate(all_records[11:21]):
    sig, form, flags, subs = record
    fields = dict(subs)
    assert sig == 'PACK' and form == 0x01000810 + slot and flags == 0
    assert u32(fields['QNAM']) == 0x01000805
    assert struct.unpack('<III', fields['PKCU']) == (12,0x1C254,10), 'native Sandbox template'
    assert struct.unpack('<III', fields['PLDT']) == (8,marker_alias(slot),200)
    assert u32(fields['PKDT']) == 0x2000 and fields['PKDT'][4] == 18
    assert [v for k,v in subs if k == 'CNAM'] == [bytes([v]) for v in (0,1,0,1,1,1,1,1,0,0)]+[struct.pack('<f',50)]
    assert [v for k,v in subs if k == 'UNAM'] == [bytes([v]) for v in (0,14,1,3,4,5,6,31,7,25,27,29)]
    assert struct.unpack('<IfIIIIII',fields['CTDA']) == (0,1.0,74,0x0100080B,0,0,0,0xFFFFFFFF), 'conditional sandbox gate'
for slot, record in enumerate(all_records[21:31]):
    sig, form, flags, subs = record
    fields = dict(subs)
    assert (sig, form, flags) == ('PACK', 0x01000820 + slot, 0)
    assert u32(fields['QNAM']) == 0x01000805
    assert struct.unpack('<III', fields['PKCU']) == (12,0x1C254,10)
    assert struct.unpack('<III', fields['PLDT']) == (2,0,80), 'social holds use their own starting location'
    assert u32(fields['PKDT']) == 0x2000 and fields['PKDT'][4] == 18
    assert [v for k,v in subs if k == 'CNAM'] == [bytes([v]) for v in (0,0,0,1,0,0,0,0,0,0)]+[struct.pack('<f',50)], 'only conversations enabled during social hold'
    assert [v for k,v in subs if k == 'UNAM'] == [bytes([v]) for v in (0,14,1,3,4,5,6,31,7,25,27,29)]
    assert struct.unpack('<IfIIIIII',fields['CTDA']) == (0,1.0,74,0x01000830+slot,0,0,0,0xFFFFFFFF)
for slot, (sig, form, flags, subs) in enumerate(all_records[31:41]):
    fields=dict(subs)
    assert (sig,form,flags)==('PACK',0x01000840+slot,0)
    assert struct.unpack('<III',fields['PKCU'])==(12,0x1C254,10)
    assert struct.unpack('<III',fields['PLDT'])==(2,0,80)
    assert [v for k,v in subs if k=='CNAM']==[b'\0']*10+[struct.pack('<f',50)], 'held standing poses cannot wander or start dialogue'
    assert struct.unpack('<IfIIIIII',fields['CTDA'])==(0,1.0,74,0x01000860+slot,0,0,0,0xFFFFFFFF)
for slot, (sig, form, flags, subs) in enumerate(all_records[41:51]):
    fields=dict(subs)
    assert (sig,form,flags)==('PACK',0x01000850+slot,0)
    assert u32(fields['QNAM'])==0x01000805
    assert struct.unpack('<III',fields['PKCU'])==(3,0xA9277,2), 'native SitTarget template'
    assert struct.unpack('<III',fields['PTDA'])==(4,20+slot,0), 'dedicated ground-seat reference alias'
    assert [v for k,v in subs if k=='CNAM']==[struct.pack('<f',120),b'\0'], 'controller releases before native timeout; movement is not locked'
    assert [v for k,v in subs if k=='UNAM']==[bytes([v]) for v in (16,3,4)]
    assert struct.unpack('<IfIIIIII',fields['CTDA'])==(0,2.0,74,0x01000860+slot,0,0,0,0xFFFFFFFF)
for slot, (sig, form, flags, subs) in enumerate(all_records[51:61]):
    fields=dict(subs)
    assert (sig,form,flags)==('PACK',0x01000870+slot,0)
    assert u32(fields['QNAM'])==0x01000805
    assert struct.unpack('<III',fields['PKCU'])==(3,0x16FAA,3)
    assert struct.unpack('<III',fields['PLDT'])==(8,30+slot,45)
    assert struct.unpack('<IfIIIIII',fields['CTDA'])==(0,2.0,74,0x01000830+slot,0,0,0,0xFFFFFFFF)
    assert fields['PKDT'][6]==0, 'gathering uses a native walk'
sig, form, flags, subs = all_records[61]
assert sig == 'QUST' and form == 0x01000805 and flags == 0
assert dict(subs)['DNAM'][2] == 96
assert b'WayfarerQuestScript' in dict(subs)['VMAD']
aliases = {}
alias = None
for sig, value in subs:
    if sig == 'ALST':
        alias = u32(value)
        assert alias not in aliases
        aliases[alias] = []
    if sig == 'ALPC':
        aliases[alias].append(u32(value))
assert set(aliases) == set(range(40))
assert all(aliases[follower_alias(i)] == [0x01000850+i,0x01000840+i,0x01000870+i,0x01000820+i,0x01000810+i,package_id(i)] for i in range(10))
assert all(aliases[marker_alias(i)] == [] for i in range(10))
assert all(aliases[20+i] == [] for i in range(10))
sig, form, flags, subs = all_records[62]
assert (sig,form,flags) == ('GLOB',0x0100080B,0)
assert dict(subs)['FNAM'] == b'f' and struct.unpack('<f',dict(subs)['FLTV'])[0] == 0
for slot, (sig, form, flags, subs) in enumerate(all_records[63:73]):
    assert (sig, form, flags) == ('GLOB',0x01000830+slot,0)
    assert dict(subs)['FNAM'] == b'f' and struct.unpack('<f',dict(subs)['FLTV'])[0] == 0
for slot, (sig, form, flags, subs) in enumerate(all_records[73:83]):
    assert (sig, form, flags) == ('GLOB',0x01000860+slot,0)
    assert dict(subs)['FNAM'] == b'f' and struct.unpack('<f',dict(subs)['FLTV'])[0] == 0
by_id = {form: (sig, dict(subs)) for sig, form, flags, subs in all_records}
assert by_id[0x01000880][0] == 'FACT'
assert u32(by_id[0x01000880][1]['DATA']) == 1, 'dialogue state faction must be hidden from NPC AI'
assert (root/'SEQ/Wayfarer.seq').read_bytes() == struct.pack('<I', 0x01000805)
def wstring(value):
    raw=value.encode('ascii')
    return struct.pack('<H',len(raw))+raw
for action, verb in enumerate(('Add', 'Remove')):
    branch_id, topic_id, info_id = (0x01000881+action, 0x01000883+action, 0x01000885+action)
    kind, branch = by_id[branch_id]
    assert kind == 'DLBR' and u32(branch['DNAM']) == 1
    assert u32(branch['QNAM']) == 0x01000805 and u32(branch['SNAM']) == topic_id
    kind, topic = by_id[topic_id]
    assert kind == 'DIAL' and u32(topic['BNAM']) == branch_id and u32(topic['QNAM']) == 0x01000805
    assert topic['DATA'] == bytes(4) and topic['SNAM'] == b'CUST' and u32(topic['TIFC']) == 1
    kind, info = by_id[info_id]
    assert kind == 'INFO' and u32(info['TPIC']) == topic_id
    assert info['ENAM'] == b'\x01\0\0\0', 'command ends dialogue before applying AI changes'
    rank = 0 if action == 0 else 2
    assert info['CTDA'] == struct.pack('<If6I', 0, rank, 73, 0x01000880, 0, 0, 0, 0xffffffff)
    script='WayfarerDialogue'+verb
    expected_vmad=struct.pack('<3H',5,2,1)+wstring(script)+b'\0\0\0\x02\x01'+wstring(script)+b'\x01'+wstring(script)+wstring('Fragment_0')
    assert info['VMAD'] == expected_vmad, 'INFO begin fragment must bind to its compiled script'
    pex=(root/'Scripts'/(script+'.pex')).read_bytes()
    assert pex[:4] == b'\xfa\x57\xc0\xde' and b'Fragment_0' in pex and b'SetDialogueManagement' in pex
for topic_id in (0x01000883, 0x01000884):
    label=struct.pack('<II',topic_id,7)
    assert data.count(label) == 1
assert b'SetDialogueManagement' in (root/'Scripts/Wayfarer.pex').read_bytes()
import configparser
settings=configparser.ConfigParser(inline_comment_prefixes=(';',))
settings.read(root/'SKSE/Plugins/Wayfarer.ini',encoding='utf-8-sig')
assert not settings.getboolean('General','bAutoDiscover'), 'shipped defaults must require explicit follower enrollment'
assert settings.getint('General','iEnrollmentVersion') == 2, 'manual enrollment migration marker missing'
if '--installed-settings' in sys.argv:
    assert 0 <= settings.getint('Hotkeys','iCompanionKey') <= 511, 'invalid installed shortcut scan code'
else:
    assert settings.getint('Hotkeys','iCompanionKey') == 34, 'target companion shortcut must default to G'
assert (root/'Interface/WalkWithMe/teleport.svg').is_file(), 'game teleport artwork missing'

import xml.etree.ElementTree as ET
art=root/'Interface/WalkWithMe'
expected={'natural','arrow','wing-a','wing-b','shield','logs','flame','ember','wheel-slice','teleport'}
assert {p.stem for p in art.glob('*.svg')}==expected
assert not list(art.glob('*.png')), 'obsolete generated raster artwork must not ship'
for name in expected:
    data=(art/(name+'.svg')).read_bytes();node=ET.fromstring(data)
    assert node.attrib['viewBox']=='0 0 512 512'
    assert len(node.findall('{http://www.w3.org/2000/svg}path'))==(7 if name=='wheel-slice' else 1)
    assert b'clipPath' not in data and b'<animate' not in data, 'native renderer owns cropping and animation'
assert sum(p.stat().st_size for p in art.glob('*.svg'))<16000
assert (root/'ThirdParty/GameIcons-NOTICE.txt').is_file()
font=(root/'SKSE/Plugins/Fonts/WalkWithMeInscription.ttf').read_bytes()
assert font[:4]==b'\x00\x01\x00\x00' and len(font)>10000, 'bundled inscription font'
assert 'SIL OPEN FONT LICENSE' in (root/'ThirdParty/CinzelDecorative/OFL.txt').read_text(encoding='utf-8')
for name in ['Wayfarer', 'WayfarerQuestScript']:
    pex = (root / 'Scripts' / (name + '.pex')).read_bytes()
    assert pex[:4] == b'\xfa\x57\xc0\xde', 'expected Skyrim big-endian PEX'
    assert b'ReportAliasSync' in pex, 'victory reactions require the matching compiled alias-sync bridge'
assert (root / 'SKSE/Plugins/Wayfarer.dll').read_bytes()[:2] == b'MZ'
recovery = (root / 'Scripts/WayfarerRecovery.pex').read_bytes()
assert recovery[:4] == b'\xfa\x57\xc0\xde' and all(name in recovery for name in (b'ConsumePush', b'PushActorAway')), 'matching unstick bridge missing'
assert (root / 'Scripts/Source/WayfarerRecovery.psc').is_file()
tether = (root / 'Scripts/WayfarerTether.pex').read_bytes()
assert tether[:4] == b'\xfa\x57\xc0\xde' and b'AddHavokBallAndSocketConstraint' in tether, 'matching tether bridge missing'
assert b'RequestTetherHandoff' in (root / 'Scripts/WayfarerQuestScript.pex').read_bytes()
if '--public' in sys.argv:
    assert not (root / 'meshes/OpenAnimationReplacer/WWM Clasp').exists(), 'generate clasp clips locally, never publish them'
    assert len(list((root / 'Scripts').glob('*.pex'))) == 6, 'expected six matching compiled scripts'
assert not list(root.rglob('*.bak')), 'backup files must stay out of the package'
print('Package verified: preserved packages/aliases/globals, dialogue and fragments, SEQ, Skyrim PEX files including unstick bridge, artwork and DLL.')
