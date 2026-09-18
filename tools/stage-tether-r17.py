"""Stage revision 17 over the verified installed build; never install from here."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

repo = Path(__file__).resolve().parents[1]
installed = repo.parent / 'MODS/mods/Wayfarer'
name = 'Walk-With-Me-0.16.8-TETHER-r17'
stage = repo / 'build' / name
archive = stage.parent / (name + '.zip')
source_archive = stage.parent / (name + '-Source.zip')
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert not any(p.exists() for p in (stage, archive, source_archive)), 'Preserve previous artifacts.'
previous = json.loads((installed / 'test-build.json').read_text(encoding='utf-8-sig'))
for relative, expected in previous['files'].items():
    if relative not in ('SKSE/Plugins/Wayfarer.ini', 'meta.ini'):
        assert digest(installed / relative) == expected, relative
assert digest(installed / 'SKSE/Plugins/Wayfarer.dll') == previous['dllSHA256']
for kind in ('modern', 'legacy'):
    assert '100% tests passed, 0 tests failed out of 19' in (repo / f'build/tether-r17-{kind}-tests.log').read_text(encoding='utf-8-sig')
    log = (repo / f'build/tether-r17-{kind}-build.log').read_text(encoding='utf-8-sig')
    assert not any(e in log for e in ('error C', 'error LNK', 'error MSB'))

papyrus_log = (repo / 'build/tether-r17-papyrus-build.log').read_text(encoding='utf-8-sig')
assert '0 error(s), 0 warning(s)' in papyrus_log and '1 succeeded, 0 failed' in papyrus_log
shutil.copytree(installed, stage)
shutil.copy2(repo / 'build/tether-r17-papyrus-build.log', stage / 'test-evidence')
dll = repo / 'build/modern/bin/Release/Wayfarer.dll'
payload = dll.read_bytes()
assert b'[HandHolding] Revision 17:' in payload
assert b'[HandHolding] Revision 11.3:' not in payload
assert b'[Victory] Revision 4:' in payload
assert b'[PackageControl] Popup r1:' in payload
shutil.copy2(dll, stage / 'SKSE/Plugins/Wayfarer.dll')
for script in ('Wayfarer', 'WayfarerQuestScript', 'WayfarerTether'):
    if script == 'WayfarerQuestScript':
        compiled = repo / f'build/tether-r17-scripts/{script}.pex'
        assert compiled.exists()
        shutil.copy2(compiled, stage / f'Scripts/{script}.pex')
    else:
        assert digest(repo / f'package/Scripts/Source/{script}.psc') == digest(installed / f'Scripts/Source/{script}.psc'), script
    shutil.copy2(repo / f'package/Scripts/Source/{script}.psc', stage / f'Scripts/Source/{script}.psc')
for relative in ('ThirdParty/TETHER-NOTICE.txt', 'ThirdParty/TETHER-LICENSE.txt'):
    shutil.copy2(repo / 'package' / relative, stage / relative)
for relative in ('README.md', 'docs/usage.md', 'docs/testing.md', 'docs/tether-r17-handoff.md', 'docs/tether-r16-handoff.md', 'docs/tether-r15-handoff.md', 'docs/tether-r14-handoff.md', 'docs/tether-r13-handoff.md', 'docs/tether-handholding.md'):
    shutil.copy2(repo / relative, stage / relative)
shutil.copy2(repo / 'docs/usage.md', stage / 'USAGE.md')
shutil.copy2(repo / 'docs/tether-r17-handoff.md', stage / 'HAND_HOLDING.md')
shutil.copy2(repo / 'docs/tether-r17-handoff.md', stage / 'TESTING.md')
for kind in ('modern', 'legacy'):
    for action in ('build', 'tests'):
        shutil.copy2(repo / f'build/tether-r17-{kind}-{action}.log', stage / 'test-evidence')

assert digest(stage / 'Wayfarer.esp') == digest(installed / 'Wayfarer.esp')
assert digest(stage / 'SKSE/Plugins/Wayfarer.ini') == digest(installed / 'SKSE/Plugins/Wayfarer.ini')
for script, symbols in {
    'Wayfarer': (b'TetherStepStatus', b'GetTetherHandoffTicket', b'ConfirmTetherHandoff'),
    'WayfarerQuestScript': (b'GetTetherHandoffTicket', b'ConfirmTetherHandoff', b'RequestTetherHandoff', b'ServiceTetherHandoff'),
    'WayfarerTether': (b'TetherStepStatus', b'KeepOffsetFromActor', b'AddHavokBallAndSocketConstraint', b'RemoveHavokConstraints'),
}.items():
    data = (stage / f'Scripts/{script}.pex').read_bytes()
    assert data[:4] == b'\xfa\x57\xc0\xde'
    assert all(s in data for s in symbols), script
subprocess.run([sys.executable, str(repo / 'tools/verify-package.py'), str(stage)], check=True)
manifest = {
    'build': name, 'assetsBasedOn': previous['build'], 'inGameTested': False,
    'nativeTests': {'modern': '19/19', 'legacy': '19/19'},
    'dllSHA256': digest(dll),
    'legacyDLLSHA256': digest(repo / 'build/skyrim/bin/Release/Wayfarer.dll'),
    'sourceArchive': source_archive.name,
    'sourceHashes': {p.relative_to(repo).as_posix(): digest(p) for folder in ('src', 'include', 'tests', 'package/Scripts/Source')
                     for p in (repo / folder).rglob('*') if p.is_file()},
    'files': {p.relative_to(stage).as_posix(): digest(p) for p in stage.rglob('*')
              if p.is_file() and p.name not in ('test-build.json', 'manifest.sha256')}
}
(stage / 'test-build.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
(stage / 'manifest.sha256').write_text(''.join(f'{sha}  {path}\n' for path, sha in sorted(manifest['files'].items())), encoding='utf-8')

# Exact current sources and staged payload, including dependency sources used
# by the existing release workflow. The archive is local and is not published.
files = {}
tracked = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard'], cwd=repo).decode().splitlines()
for relative in set(tracked):
    path = repo / relative
    if path.is_file() and not relative.startswith('package/'):
        files['WalkWithMe/' + relative] = path
for path in stage.rglob('*'):
    if path.is_file(): files['WalkWithMe/package/' + path.relative_to(stage).as_posix()] = path
dependency = repo / 'build/deps/CommonLibSSE-NG-7.5.1'
for relative in subprocess.check_output(['git', 'ls-files'], cwd=dependency).decode().splitlines():
    path = dependency / relative
    if path.is_file(): files['CommonLibSSE-NG-7.5.1/' + relative] = path
for name in ('fmt', 'spdlog', 'directxtk', 'directxmath', 'simpleini', 'rapidcsv', 'xbyak', 'nlohmann-json', 'toml11'):
    trees = list((Path('X:/vcpkg/buildtrees') / name / 'src').glob('*.clean'))
    assert trees, f'Missing dependency source: {name}'
    for tree in trees:
        for path in tree.rglob('*'):
            if path.is_file() and '.git' not in path.relative_to(tree).parts:
                files[f'Dependencies/{name}/{tree.name}/' + path.relative_to(tree).as_posix()] = path
provenance = repo / 'build/deps/vcpkg-7.5.1/x64-windows-static-md/share'
for path in provenance.rglob('*'):
    if path.is_file(): files['DependencyProvenance/' + path.relative_to(provenance).as_posix()] = path
files['DependencyProvenance/vcpkg-status.txt'] = repo / 'build/deps/vcpkg-7.5.1/vcpkg/status'
files['build.md'] = repo / 'docs/build.md'
for target, entries in ((source_archive, files), (archive, {p.relative_to(stage).as_posix(): p for p in stage.rglob('*') if p.is_file()})):
    with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED) as output:
        for relative, path in sorted(entries.items()): output.write(path, relative)
    with zipfile.ZipFile(target) as check:
        assert check.testzip() is None
        for relative, path in entries.items(): assert hashlib.sha256(check.read(relative)).hexdigest() == digest(path), relative
    target.with_suffix('.zip.sha256').write_text(digest(target) + '  ' + target.name + '\n', encoding='utf-8')
print(json.dumps({'archive': str(archive), 'source': str(source_archive), 'dllSHA256': digest(dll)}, indent=2))
