"""Stage the complete TETHER adaptation and corresponding source; no installation."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

repo = Path(__file__).resolve().parents[1]
name = 'Walk-With-Me-0.16.8-TETHER-r12'
stage = repo / 'build' / name
archive = stage.parent / (name + '.zip')
source_archive = stage.parent / (name + '-Source.zip')
baseline = repo / 'build/Walk-With-Me-0.16.8-Rest-HUD-r3'
symbols = stage.parent / (name + '-symbols')
if any(p.exists() for p in (stage, archive, source_archive, symbols)):
    raise SystemExit('Preserve prior artifacts; choose a new revision.')

digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
previous = json.loads((baseline / 'test-build.json').read_text(encoding='utf-8-sig'))
for relative, expected in previous['files'].items():
    assert digest(baseline / relative) == expected, relative
for kind, count in (('', 26), ('-legacy', 27)):
    assert f'100% tests passed, 0 tests failed out of {count}' in (repo / f'build/tether-r12{kind}-tests.log').read_text(encoding='utf-8-sig')
    log = (repo / f'build/tether-r12{kind}-build.log').read_text(encoding='utf-8-sig')
    assert not any(error in log for error in ('error C', 'error LNK', 'error MSB'))
for script in (baseline / 'Scripts/Source').glob('*.psc'):
    if script.name != 'Wayfarer.psc':
        assert script.read_bytes() == (repo / 'package/Scripts/Source' / script.name).read_bytes(), script.name

shutil.copytree(baseline, stage)
dll = repo / 'build/modern/bin/Release/Wayfarer.dll'
assert b'[HandHolding] Revision 12:' in dll.read_bytes()
assert b'[HandHolding] Revision 11.3:' not in dll.read_bytes()
shutil.copy2(dll, stage / 'SKSE/Plugins/Wayfarer.dll')
for script in ('Wayfarer', 'WayfarerTether'):
    shutil.copy2(repo / f'build/tether-r12-scripts/{script}.pex', stage / f'Scripts/{script}.pex')
    shutil.copy2(repo / f'package/Scripts/Source/{script}.psc', stage / f'Scripts/Source/{script}.psc')
for doc in ('usage.md', 'testing.md', 'changes-0.16.8.md', 'nexus.md', 'tether-handholding.md'):
    shutil.copy2(repo / 'docs' / doc, stage / 'docs' / doc)
shutil.copy2(repo / 'docs/tether-handholding.md', stage / 'HAND_HOLDING.md')
for notice in ('NOTICE.txt', 'TETHER-NOTICE.txt', 'TETHER-LICENSE.txt'):
    shutil.copy2(repo / 'package/ThirdParty' / notice, stage / 'ThirdParty' / notice)
for notice in ('LICENSE', 'LICENSING.md', 'EXCEPTIONS.md'):
    shutil.copy2(repo / notice, stage / notice)
for kind in ('', '-legacy'):
    for suffix in ('build', 'tests'):
        log = f'tether-r12{kind}-{suffix}.log'
        shutil.copy2(repo / 'build' / log, stage / 'test-evidence' / log)
(stage / 'README.md').write_text('''# Walk With Me 0.16.8 — TETHER handholding revision 12

Handholding physics and elastic speed behavior adapted from TETHER by KEI997.
Copyright (c) 2026 KEI997; MIT. https://github.com/Kei-BWV997/TETHER
Full credit and license: ThirdParty/TETHER-NOTICE.txt and TETHER-LICENSE.txt.

Includes Victory r2, Rest HUD r3 and the earlier framework coexistence changes.
Install the entire package with Skyrim closed. Preserve your Wayfarer.ini.
Choose one companion and enable experimental handholding in Companion mode.
The standalone TETHER mod is not required. No TETHER HKX assets are included.

This replaces the previous handholding body/heading/animation-graph overrides
with a physics connection, native pathfinding and a distance-based speed limit.
Temporary arrival-radius changes restore after handholding. Setup, cancellation,
equipment changes and loading have guarded cleanup and diagnostic logs.

Modern build: 26/26 tests. Legacy build: 27/27 tests. This package uses modern.
Physics stability, exact hand poses and whole-body jitter need in-game testing.
Use the checks in HAND_HOLDING.md. The Blender clasp draft is not included.
Corresponding source: Walk-With-Me-0.16.8-TETHER-r12-Source.zip.
''', encoding='utf-8')
subprocess.run([sys.executable, str(repo / 'tools/verify-package.py'), str(stage)], check=True)
manifest = {'build': name, 'basedOn': previous['build'], 'dllSHA256': digest(dll),
    'inGameTested': False, 'sourceArchive': source_archive.name,
    'files': {p.relative_to(stage).as_posix(): digest(p) for p in stage.rglob('*') if p.is_file() and p.name != 'test-build.json'}}
(stage / 'test-build.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')

# Include the exact current mod sources, staged runtime/scripts, CommonLib and
# dependency source/provenance used by the project's existing release workflow.
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
for dependency_name in ('fmt','spdlog','directxtk','directxmath','simpleini','rapidcsv','xbyak','nlohmann-json','toml11'):
    trees = list((Path('X:/vcpkg/buildtrees') / dependency_name / 'src').glob('*.clean'))
    assert trees, f'Missing dependency source: {dependency_name}'
    for tree in trees:
        for path in tree.rglob('*'):
            if path.is_file() and '.git' not in path.relative_to(tree).parts:
                files[f'Dependencies/{dependency_name}/{tree.name}/' + path.relative_to(tree).as_posix()] = path
provenance = repo / 'build/deps/vcpkg-7.5.1/x64-windows-static-md/share'
for path in provenance.rglob('*'):
    if path.is_file(): files['DependencyProvenance/' + path.relative_to(provenance).as_posix()] = path
files['DependencyProvenance/vcpkg-status.txt'] = repo / 'build/deps/vcpkg-7.5.1/vcpkg/status'
files['build.md'] = repo / 'docs/build.md'
with zipfile.ZipFile(source_archive, 'w', zipfile.ZIP_DEFLATED) as output:
    for relative, path in sorted(files.items()): output.write(path, relative)
with zipfile.ZipFile(source_archive) as check:
    assert check.testzip() is None
    for relative, path in files.items(): assert hashlib.sha256(check.read(relative)).hexdigest() == digest(path), relative
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
    for path in sorted(stage.rglob('*')):
        if path.is_file(): output.write(path, path.relative_to(stage).as_posix())
with zipfile.ZipFile(archive) as check:
    assert check.testzip() is None
    for relative, expected in manifest['files'].items(): assert hashlib.sha256(check.read(relative)).hexdigest() == expected
symbols.mkdir()
shutil.copy2(repo / 'build/modern/bin/Release/Wayfarer.pdb', symbols / 'Wayfarer.pdb')
for output in (archive, source_archive):
    output.with_suffix('.zip.sha256').write_text(digest(output)+'  '+output.name+'\n')
print(json.dumps({'archive': str(archive), 'source': str(source_archive), 'dllSHA256': digest(dll),
    'archiveSHA256': digest(archive), 'sourceSHA256': digest(source_archive)}, indent=2))
