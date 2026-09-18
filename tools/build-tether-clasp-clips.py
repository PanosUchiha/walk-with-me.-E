"""Build local OAR clasp clips, preserving the original locomotion container.

Only the spline payload is replaced. Root-motion objects, event tracks, bindings,
float metadata and all original relocation destinations remain in place.
Uses the bundled PyNifly codec and the user's locally extracted game clips.
"""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import math
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
codec = None


def layout(raw):
    assert raw[0x10] == 8, 'Only verified SE 64-bit containers are supported.'
    sections = codec._parse_hkx_sections(raw)
    sec = sections['__data__']
    objects = codec._parse_virtual_fixups(raw, sec, sections['__classnames__']['offset'])
    anims = [offset for offset, kind in objects if kind == 'hkaSplineCompressedAnimation']
    assert len(anims) == 1 and sec['end'] == len(raw)
    return sec, anims[0], codec._parse_local_fixups(raw, sec)


def transplant(original, encoded):
    sec, anim, fixups = layout(original)
    generated, other, new_fixups = layout(encoded)
    source_at, new_at = sec['offset'] + anim, generated['offset'] + other
    # These offsets are the SE hkaAnimation scalar fields. Never drop floats.
    assert original[source_at + 16:source_at + 28] == encoded[new_at + 16:new_at + 28]
    assert original[source_at + 56:source_at + 68] == encoded[new_at + 56:new_at + 68]
    def array(raw, section, base, links, field, width):
        count = struct.unpack_from('<I', raw, section['offset'] + base + field + 8)[0]
        at = section['offset'] + links.get(base + field, 0)
        return raw[at:at + count * width]
    def integers(raw):
        return list(struct.unpack('<' + 'I' * (len(raw)//4), raw))
    old_blob = array(original, sec, anim, fixups, 152, 1)
    new_blob = array(encoded, generated, other, new_fixups, 152, 1)
    old_starts = integers(array(original, sec, anim, fixups, 88, 4))
    new_starts = integers(array(encoded, generated, other, new_fixups, 88, 4))
    float_offsets = integers(array(original, sec, anim, fixups, 104, 4))
    old_mask = struct.unpack_from('<I', original, source_at + 68)[0]
    new_mask = struct.unpack_from('<I', encoded, new_at + 68)[0]
    assert old_mask >= new_mask and (old_mask-new_mask)%4 == 0
    # The codec handles transforms only. Preserve the source float masks and
    # compressed float streams verbatim, with new per-block offsets.
    merged, starts, floats = bytearray(), [], []
    assert len(old_starts) == len(new_starts) == len(float_offsets)
    for i, (old_start, new_start) in enumerate(zip(old_starts, new_starts)):
        old_end = old_starts[i+1] if i+1 < len(old_starts) else len(old_blob)
        new_end = new_starts[i+1] if i+1 < len(new_starts) else len(new_blob)
        starts.append(len(merged))
        block = bytearray(new_blob[new_start:new_start+new_mask])
        block += old_blob[old_start+new_mask:old_start+old_mask]
        block += new_blob[new_start+new_mask:new_end]
        floats.append(len(block))
        block += old_blob[old_start+float_offsets[i]:old_end]
        block += b'\x00' * (-len(block)%16)
        merged += block
    overrides = {88: struct.pack('<'+'I'*len(starts), *starts),
                 104: struct.pack('<'+'I'*len(floats), *floats), 152: bytes(merged)}
    core = bytearray(original[sec['offset']:sec['data1']])
    protected = bytes(core)
    changed = set()

    def replace(offset, data):
        core[offset:offset + len(data)] = data
        changed.update(range(offset, offset + len(data)))

    # Frame/block description belongs to the newly compressed spline.
    replace(anim + 56, encoded[new_at + 56:new_at + 84])
    replace(anim + 68, struct.pack('<I', old_mask))
    for field, element in ((88, 4), (104, 4), (120, 4), (136, 4), (152, 1)):
        header = bytearray(encoded[new_at + field:new_at + field + 16])
        if field in overrides:
            count = len(overrides[field])//element
            struct.pack_into('<II', header, 8, count, count | 0x80000000)
        count = struct.unpack_from('<I', header, 8)[0]
        replace(anim + field, header)
        fixups.pop(anim + field, None)
        if count:
            core.extend(b'\x00' * (-len(core) % 16))
            fixups[anim + field] = len(core)
            start = generated['offset'] + new_fixups[other + field]
            payload = overrides.get(field, encoded[start:start + count * element])
            assert len(payload) == count * element
            core.extend(payload)
    core.extend(b'\x00' * (-len(core) % 16))
    # Includes the original extracted-motion object, annotation events/strings,
    # binding and bone map: every byte outside the spline header is preserved.
    assert all(core[i] == value for i, value in enumerate(protected) if i not in changed)
    local = b''.join(struct.pack('<II', a, b) for a, b in sorted(fixups.items()))
    local += b'\xff' * (-len(local) % 16)
    old_global = sec['data2'] - sec['offset']
    delta = len(core) + len(local) - old_global
    output = bytearray(original[:sec['offset']]) + core + local + original[sec['data2']:]
    section_header = next(0x40 + i * 0x30 for i in range(3)
                          if original[0x40 + i * 0x30:0x50 + i * 0x30].split(b'\x00')[0] == b'__data__')
    struct.pack_into('<I', output, section_header + 0x18, len(core))
    for field in (0x1C, 0x20, 0x24, 0x28, 0x2C):
        value = struct.unpack_from('<I', original, section_header + field)[0]
        struct.pack_into('<I', output, section_header + field, value + delta)
    layout(output)
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--animations', type=Path, required=True,
                        help='Extracted vanilla meshes/actors/character/animations directory')
    parser.add_argument('--output', type=Path, required=True, help='New output mod folder; must not already exist')
    parser.add_argument('--codec', type=Path, help='Optional override for the PyNifly hkx codec folder')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    codec_dir = args.codec or (here / 'codec' if (here / 'codec').exists() else ROOT / 'third_party/PyNifly/hkx')
    sys.path.insert(0, str(codec_dir))
    global codec
    import anim_skyrim as codec
    pose_file = here / 'clasp-rotations.json'
    if not pose_file.exists():
        pose_file = ROOT / 'assets/clasp-rotations.json'
    poses = json.loads(pose_file.read_text(encoding='utf-8'))
    vanilla = args.animations.resolve(strict=True)
    out = args.output.resolve()
    archive = out.with_name(out.name + '.zip')
    if out.exists() or archive.exists():
        parser.error('Output folder or ZIP already exists. Choose a new name to preserve previous files.')
    if out == vanilla or vanilla in out.parents:
        parser.error('Output must be outside the extracted game animation folder.')
    sources = [vanilla / relative for relative in poses['clips']]
    missing = [str(p.relative_to(vanilla)) for p in sources if not p.is_file()]
    if missing:
        parser.error('Missing vanilla clips: ' + ', '.join(missing))
    # Check every input before creating output; source animation files are read-only.
    for source in sources:
        layout(source.read_bytes())
    out.mkdir(parents=True)
    report = []
    for side in ('L', 'R'):
        folder = out / 'meshes/OpenAnimationReplacer/WWM Clasp' / ('Left' if side == 'L' else 'Right')
        folder.mkdir(parents=True, exist_ok=True)
        config = {'name': f'{side} hand clasp', 'description': 'Authored clasp arm while this actor owns a WWM TETHER grip.',
                  'priority': 27103000, 'interruptible': True,
                  'conditions': [{'condition': f'WWM_TetherClasp{side}', 'requiredVersion': '0.2.0.0', 'requiredPlugin': 'Walk With Me'}]}
        (folder / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
        for source in sources:
            original = codec.load_skyrim_animation(str(source))
            assert original and original.blend_hint == 0
            edited = copy.deepcopy(original)
            mapping = original.track_to_bone_indices or list(range(original.num_tracks))
            owned = []
            for track_index, bone_index in enumerate(mapping):
                name = poses[side]['bones'][bone_index]
                if name in poses[side]['rotations']:
                    edited.tracks[track_index].rotations = [poses[side]['rotations'][name][:] for _ in range(original.num_frames)]
                    owned.append(track_index)
            assert len(owned) == 20, (source, owned)
            scratch = out / 'encoded.hkx'
            codec.write_skyrim_animation(str(scratch), edited, ptr_size=8)
            payload = transplant(source.read_bytes(), scratch.read_bytes())
            relative = Path('actors/character/animations') / source.relative_to(vanilla)
            target = folder / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)
            decoded = codec.load_skyrim_animation(str(target))
            assert decoded.num_frames == original.num_frames and decoded.num_tracks == original.num_tracks
            assert decoded.duration == original.duration and decoded.annotations == original.annotations
            assert decoded.track_to_bone_indices == original.track_to_bone_indices
            max_rotation, max_position = 0.0, 0.0
            for expected, actual in zip(edited.tracks, decoded.tracks):
                for a, b in zip(expected.rotations, actual.rotations):
                    cosine = abs(sum(x*y for x, y in zip(a, b))) / math.sqrt(sum(x*x for x in a)*sum(x*x for x in b))
                    max_rotation = max(max_rotation, math.degrees(2*math.acos(min(1, cosine))))
                for field in ('translations', 'scales'):
                    for a, b in zip(getattr(expected, field), getattr(actual, field)):
                        max_position = max(max_position, *(abs(x-y) for x, y in zip(a, b)))
            assert max_rotation < .3 and max_position < .003, (source, max_rotation, max_position)
            report.append({'side': side, 'clip': relative.as_posix(), 'frames': original.num_frames,
                           'changedRotationTracks': len(owned), 'maxRoundTripDegrees': max_rotation,
                           'maxRoundTripPosition': max_position, 'containerMetadataPreserved': True,
                           'sha256': hashlib.sha256(payload).hexdigest()})
            print(f'Validated {side}: {source.relative_to(vanilla)}', flush=True)
    (out / 'encoded.hkx').unlink()
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    (out / 'README.txt').write_text('Walk With Me 0.2.0 - locally generated hand-clasp clips.\n'
                                   'Install after the main mod. Restart Skyrim. Keep this generated mod local.\n')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as output:
        for path in sorted(out.rglob('*')):
            if path.is_file():
                output.write(path, path.relative_to(out).as_posix())
    with zipfile.ZipFile(archive) as check:
        assert check.testzip() is None
    print(f'Validated {len(report)} clasp clips; root motion, annotations and bindings preserved.')
    print(f'Install this local-only archive after Walk With Me: {archive}')


if __name__ == '__main__':
    main()
