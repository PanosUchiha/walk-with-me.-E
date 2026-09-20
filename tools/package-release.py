"""Package the public SE/AE mod and matching complete source; no local game assets."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import zipfile
from animation_package import split_gestures, verify_patch


def payload(value):
    return value.read_bytes() if isinstance(value, Path) else value


def write_archive(path, files):
    assert not path.exists(), f'Preserve existing release: {path}'
    temporary = path.with_suffix('.zip.building')
    with zipfile.ZipFile(temporary, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for name, value in sorted(files.items()):
            if isinstance(value, Path):
                output.write(value, name)
            else:
                output.writestr(name, value)
    with zipfile.ZipFile(temporary) as check:
        assert check.testzip() is None
        assert set(check.namelist()) == set(files)
        for name, value in files.items():
            assert hashlib.sha256(check.read(name)).digest() == hashlib.sha256(payload(value)).digest(), name
    temporary.rename(path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    path.with_suffix('.zip.sha256').write_text(f'{digest}  {path.name}\n')
    print(f'{path.name}: {len(files)} files, {path.stat().st_size / 1048576:.2f} MiB, SHA256 {digest}', flush=True)


def verify_dll(dll, version):
    pe = struct.unpack_from('<I', dll, 0x3c)[0]
    count = struct.unpack_from('<H', dll, pe + 6)[0]
    optional = struct.unpack_from('<H', dll, pe + 20)[0]
    sections = [struct.unpack_from('<4I', dll, pe + 24 + optional + 40 * i + 8) for i in range(count)]
    def offset(rva):
        for _, start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError('Export RVA is not file backed')
    def u32(at):
        return struct.unpack_from('<I', dll, at)[0]
    export = offset(u32(pe + 24 + 112))
    names, ordinals, functions = (offset(u32(export + n)) for n in (32, 36, 28))
    for i in range(u32(export + 24)):
        name = offset(u32(names + 4 * i))
        if dll[name:dll.index(b'\0', name)] == b'SKSEPlugin_Version':
            ordinal = struct.unpack_from('<H', dll, ordinals + 2 * i)[0]
            metadata = offset(u32(functions + 4 * ordinal))
            major, minor, patch = map(int, version.split('.'))
            assert u32(metadata + 4) == (major << 24) | (minor << 16) | (patch << 4)
            independence_ex, independence = u32(metadata + 0x304), u32(metadata + 0x308)
            # Only the NG build sets the v5 address-library bit, so this is the
            # cheapest way to assert the universal DLL is what gets shipped. The
            # script extender does not read this bit; it is a build marker here,
            # not a compatibility requirement.
            assert independence_ex & 2, 'Modern address-library reader required; this is not the NG build'
            assert not independence_ex & 1, 'This plugin reads game structs and must not declare no-struct-use'
            assert independence == 5, 'Expected address library plus post-629 struct layouts'
            return
    raise ValueError('Missing SKSEPlugin_Version export')


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vcpkg-root', type=Path, default=Path('X:/vcpkg'))
    parser.add_argument('--output-dir', type=Path, default=root / 'release')
    args = parser.parse_args()
    package = root / 'package'
    version = re.search(r'project\(Wayfarer VERSION ([0-9.]+)', (root / 'CMakeLists.txt').read_text()).group(1)
    release = args.output_dir.resolve()
    release.mkdir(parents=True, exist_ok=True)
    binary_zip = release / f'Walk-With-Me-{version}-SE-AE.zip'
    source_zip = release / f'Walk-With-Me-{version}-Source.zip'
    gesture_zip = release / f'Walk-With-Me-{version}-Command-Gestures.zip'
    assert not any(p.exists() for p in (binary_zip, source_zip, gesture_zip)), 'Release archives already exist'
    verify_patch(package / 'Nemesis_Engine/mod/gpma/0_master')
    subprocess.run([sys.executable, str(root / 'tools/verify-package.py'), str(package), '--public'], check=True)
    dll = root / 'build/modern/bin/Release/Wayfarer.dll'
    verify_dll(dll.read_bytes(), version)

    files = {}
    for folder in ('Interface', 'Scripts', 'SKSE', 'ThirdParty', 'meshes', 'Nemesis_Engine', 'SEQ'):
        for path in (package / folder).rglob('*'):
            if path.is_file():
                assert path.suffix.lower() not in ('.log', '.pdb', '.bak', '.tmp')
                files[path.relative_to(package).as_posix()] = path
    files['SKSE/Plugins/Wayfarer.dll'] = dll
    files['Wayfarer.esp'] = package / 'Wayfarer.esp'
    for name in ('LICENSE', 'LICENSING.md', 'EXCEPTIONS.md', 'LICENSES/Wayfarer-MIT.txt'):
        files[name] = root / name
    for output, document in {
        'README.md': 'release.md', 'USAGE.md': 'usage.md', 'HAND_HOLDING.md': 'hand-holding.md',
        'GESTURES.md': 'gestures.md', 'CHANGELOG.md': f'changes-{version}.md',
        'RELEASE-VALIDATION.md': f'validation-{version}.md',
    }.items():
        files[output] = root / 'docs' / document
    for name in ('build.md', 'publish.md', 'gog.md', 'hotkeys.md', 'package-control.md',
                 'party-orders-utilities.md', 'custom-followers.md', 'victory.md', 'testing.md', 'api-guide.md',
                 'animation-hotfix-audit.md', 'reactions-pathing-review.md', 'actor-routes-victory-r5.md',
                 'bridge-startup-r6.md', 'control-handoff-r7.md', 'reset-cadence-r8.md',
                 'r3-movement-r9.md', 'baseline-0.2.1.md'):
        files['docs/' + name] = root / 'docs' / name

    files['ClaspGenerator/build-tether-clasp-clips.py'] = root / 'tools/build-tether-clasp-clips.py'
    files['ClaspGenerator/clasp-rotations.json'] = root / 'assets/clasp-rotations.json'
    files['ClaspGenerator/README.md'] = root / 'docs/hand-holding.md'
    for name in ('anim_skyrim.py', 'anim_fo4.py'):
        files['ClaspGenerator/codec/' + name] = root / 'third_party/PyNifly/hkx' / name
    for name in ('LICENSE', 'NOTICE.md'):
        files['ClaspGenerator/' + name] = root / 'third_party/PyNifly' / name

    dependency = root / 'build/deps/CommonLibSSE-NG-7.5.1'
    revision = subprocess.check_output(['git', '-C', str(dependency), 'rev-parse', 'HEAD']).decode().strip()
    assert revision == 'bedcb1e05418baba7b316a650b6180c2dd6007a8'
    provenance = root / 'build/deps/vcpkg-7.5.1/x64-windows-static-md/share'
    files['ThirdParty/CommonLib-EXCEPTIONS.md'] = dependency / 'EXCEPTIONS.md'
    for path in (dependency / 'licenses').rglob('*'):
        if path.is_file():
            files['ThirdParty/CommonLib/' + path.relative_to(dependency / 'licenses').as_posix()] = path
    for path in provenance.glob('*/copyright'):
        files[f'ThirdParty/Dependencies/{path.parent.name}-LICENSE.txt'] = path
    files['ThirdParty/SKSEMenuFramework.h'] = root / 'include/SKSEMenuFramework.h'
    files['ThirdParty/NOTICE.txt'] = payload(files['ThirdParty/NOTICE.txt']).replace(
        b'include/SKSEMenuFramework.h', b'ThirdParty/SKSEMenuFramework.h')

    # Only individually reviewed, original WWM clips may leave this workspace.
    approved = json.loads((root / 'assets/release-animation-hashes.json').read_text())
    animations = {name: hashlib.sha256(payload(value)).hexdigest()
                  for name, value in files.items() if name.lower().endswith('.hkx')}
    assert animations == approved, 'Unreviewed or game-derived animation in public package'
    assert not any('WWM Clasp/' in name or '_1stPerson' in name for name in files)
    for name in files:
        if name.endswith('config.json') and 'meshes/OpenAnimationReplacer/' in name:
            config = json.loads(payload(files[name]))
            for condition in config.get('conditions', []):
                if condition.get('requiredPlugin') == 'Walk With Me':
                    # This is the minimum condition implementation version, not
                    # the current release label. Existing 0.2.0 clips remain valid.
                    assert condition['requiredVersion'] == '0.2.0.0', name
    source = {}
    extensions = {'.cpp', '.h', '.py', '.ps1', '.md', '.txt', '.json', '.toml', '.cmake', '.in', '.svg', '.ini', '.psc'}
    for folder in ('src', 'include', 'tests', 'tools', 'docs', 'cmake', 'LICENSES', 'third_party', 'assets'):
        for path in (root / folder).rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts and (path.suffix in extensions or path.name in {'LICENSE', 'COPYING', 'EXCEPTIONS'}):
                relative = path.relative_to(root).as_posix()
                if relative.startswith(('tools/champollion/', 'tools/caprica/')):
                    continue
                source['WalkWithMe/' + relative] = path
    for name in ('.clang-format', '.gitignore', '.gitattributes', 'CMakeLists.txt', 'CMakePresets.json',
                 'build-release.ps1', 'deploy-local.ps1', 'vcpkg.json', 'vcpkg-configuration.json',
                 'README.md', 'LICENSE', 'LICENSING.md', 'EXCEPTIONS.md', 'third_party/PyNifly/LICENSE'):
        source['WalkWithMe/' + name] = root / name
    for name, value in files.items():
        source['WalkWithMe/package/' + name] = value
    for name in subprocess.check_output(['git', '-C', str(dependency), 'ls-files']).decode().splitlines():
        if (dependency / name).is_file():
            source['CommonLibSSE-NG-7.5.1/' + name] = dependency / name
    for name in ('fmt', 'spdlog', 'directxtk', 'directxmath', 'simpleini', 'rapidcsv', 'xbyak', 'nlohmann-json', 'toml11'):
        trees = list((args.vcpkg_root / 'buildtrees' / name / 'src').glob('*.clean'))
        assert trees, f'Missing dependency source: {name}'
        for tree in trees:
            for path in tree.rglob('*'):
                if path.is_file() and '.git' not in path.relative_to(tree).parts:
                    source[f'Dependencies/{name}/{tree.name}/' + path.relative_to(tree).as_posix()] = path
    for path in provenance.rglob('*'):
        if path.is_file():
            source['DependencyProvenance/' + path.relative_to(provenance).as_posix()] = path
    source['DependencyProvenance/vcpkg-status.txt'] = root / 'build/deps/vcpkg-7.5.1/vcpkg/status'
    source['build.md'] = root / 'docs/build.md'
    assert all(not name.startswith(('WalkWithMe/build/', 'WalkWithMe/experiments/')) and 'WWM Clasp/' not in name for name in source)
    assert {name.removeprefix('WalkWithMe/package/') for name in source if name.lower().endswith('.hkx')} == set(approved)
    for mapping in (files, source):
        for value in mapping.values():
            if isinstance(value, Path):
                assert value.is_file(), value
    files, gestures = split_gestures(files)
    gestures['GESTURES.md'] = root / 'docs/gestures.md'
    for name in ('LICENSE', 'LICENSING.md', 'EXCEPTIONS.md'):
        gestures[name] = root / name
    for path in (package / 'ThirdParty').rglob('*'):
        if path.is_file():
            gestures[path.relative_to(package).as_posix()] = path
    for mapping in (files, gestures):
        mapping['manifest.sha256'] = ''.join(
            f'{hashlib.sha256(payload(value)).hexdigest()}  {name}\n'
            for name, value in sorted(mapping.items())).encode()
    write_archive(binary_zip, files)
    write_archive(gesture_zip, gestures)
    write_archive(source_zip, source)
    (release / f'Walk-With-Me-{version}-CHANGELOG.md').write_bytes((root / f'docs/changes-{version}.md').read_bytes())


if __name__ == '__main__':
    main()
