"""Package the 0.16.7 following rollback with victory/rest/HUD features retained."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

repo = Path(__file__).resolve().parents[1]
name = 'Walk-With-Me-0.16.8-Following-Rollback-r1'
stage = repo / 'build' / name
archive = stage.parent / (name + '.zip')
source_archive = stage.with_name(name + '-Source.zip')
symbols = stage.with_name(name + '-symbols')
baseline = repo / 'build/Walk-With-Me-0.16.8-Rest-HUD-r3'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert not any(p.exists() for p in (stage, archive, source_archive, symbols)), 'Preserve existing artifacts.'
for kind in ('modern', 'legacy'):
    assert '100% tests passed, 0 tests failed out of 16' in (repo / f'build/following-rollback-r1-{kind}-tests.log').read_text(encoding='utf-8-sig')
    log = (repo / f'build/following-rollback-r1-{kind}-build.log').read_text(encoding='utf-8-sig')
    assert not any(error in log for error in ('error C', 'error LNK', 'error MSB'))
previous = json.loads((baseline / 'test-build.json').read_text(encoding='utf-8-sig'))
for relative, expected in previous['files'].items():
    assert digest(baseline / relative) == expected, relative
shutil.copytree(baseline, stage)
dll = repo / 'build/modern/bin/Release/Wayfarer.dll'
payload = dll.read_bytes()
assert b'[Following] Rollback r1:' in payload and b'[Victory] Revision 2:' in payload
assert b'[Handoff] Persistent aliases' not in payload and b'[HandHolding/TETHER]' not in payload
shutil.copy2(dll, stage / 'SKSE/Plugins/Wayfarer.dll')
compiled = repo / 'build/following-rollback-r1-scripts'
shutil.copy2(compiled / 'Wayfarer.esp', stage / 'Wayfarer.esp')
for script in ('Wayfarer', 'WayfarerQuestScript', 'WayfarerDialogueAdd', 'WayfarerDialogueRemove'):
    shutil.copy2(compiled / (script + '.pex'), stage / 'Scripts' / (script + '.pex'))
    shutil.copy2(repo / 'package/Scripts/Source' / (script + '.psc'), stage / 'Scripts/Source' / (script + '.psc'))
    assert b'TetherStepAllowed' not in (stage / 'Scripts' / (script + '.pex')).read_bytes()
for doc in ('following-rollback.md', 'victory.md', 'usage.md', 'testing.md', 'changes-0.16.8.md', 'framework-coexistence.md', 'tether-handholding.md', 'nexus.md'):
    shutil.copy2(repo / 'docs' / doc, stage / 'docs' / doc)
for source, target in [('README.md', 'README.md'), ('LICENSING.md', 'LICENSING.md'),
                       ('docs/usage.md', 'USAGE.md'), ('docs/testing.md', 'TESTING.md'),
                       ('docs/following-rollback.md', 'HAND_HOLDING.md'),
                       ('docs/changes-0.16.8.md', 'CHANGELOG.md')]:
    shutil.copy2(repo / source, stage / target)
for notice in ('NOTICE.txt', 'TETHER-NOTICE.txt', 'TETHER-LICENSE.txt'):
    shutil.copy2(repo / 'package/ThirdParty' / notice, stage / 'ThirdParty' / notice)
for log in (repo / 'build').glob('following-rollback-r1-*.log'):
    shutil.copy2(log, stage / 'test-evidence' / log.name)
subprocess.run([sys.executable, str(repo / 'tools/verify-package.py'), str(stage)], check=True)

# Audit original movement files against the baseline used to create this build.
baseline_revision = '99fa995'
movement_files = ('src/travel_planner.cpp', 'src/travel_speed.cpp', 'src/hand_holding.cpp',
                  'src/papyrus_bridge.cpp', 'src/plugin.cpp', 'include/hand_hold_locomotion.h',
                  'include/hand_hold_overrides.h', 'include/hand_holding.h',
                  'include/party_constants.h', 'tools/build-esl.ps1',
                  'package/Scripts/Source/Wayfarer.psc', 'package/Scripts/Source/WayfarerQuestScript.psc')
for relative in movement_files:
    assert (repo / relative).read_bytes() == subprocess.check_output(['git', 'show', baseline_revision + ':' + relative], cwd=repo), relative
manifest = {'build': name, 'movementBaseline': baseline_revision, 'assetsBasedOn': previous['build'],
            'nativeTests': {'modern': '16/16', 'legacy': '16/16'}, 'papyrusScriptsCompiled': 4,
            'inGameTested': False, 'dllSHA256': digest(dll),
            'featuresRetained': ['victory-r2', 'temporary-rest-toggle', 'rest-resume-icons-75-percent-larger', 'manual-enrollment'],
            'movementFilesVerifiedAgainstBaseline': list(movement_files),
            'files': {p.relative_to(stage).as_posix(): digest(p) for p in stage.rglob('*')
                      if p.is_file() and p.name not in ('test-build.json', 'manifest.sha256')}}
(stage / 'test-build.json').write_text(json.dumps(manifest, indent=2))
(stage / 'manifest.sha256').write_text(''.join(f'{digest(p)}  {p.relative_to(stage).as_posix()}\n' for p in sorted(stage.rglob('*')) if p.is_file() and p.name != 'manifest.sha256'))
with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as output:
    for path in sorted(stage.rglob('*')):
        if path.is_file(): output.write(path, path.relative_to(stage).as_posix())
with zipfile.ZipFile(archive) as check:
    assert check.testzip() is None
    for relative, expected in manifest['files'].items():
        assert hashlib.sha256(check.read(relative)).hexdigest() == expected

# Dependency sources are unchanged from the verified complete r12 source bundle.
dependency_archive = repo / 'build/Walk-With-Me-0.16.8-TETHER-r12-Source.zip'
assert digest(dependency_archive) == 'dcf0c4af426ceb3f473ac46f05e0da2fc74ab2a6d327f4c9aa699d1875d101bb'
files = {}
tracked = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard'], cwd=repo).decode().splitlines()
for relative in set(tracked):
    path = repo / relative
    if path.is_file() and not relative.startswith('package/'):
        files['WalkWithMe/' + relative] = path
for path in stage.rglob('*'):
    if path.is_file(): files['WalkWithMe/package/' + path.relative_to(stage).as_posix()] = path
dependency_hashes = {}
with zipfile.ZipFile(dependency_archive) as old, zipfile.ZipFile(source_archive, 'x', zipfile.ZIP_DEFLATED) as output:
    for entry in old.infolist():
        if entry.filename.startswith(('CommonLibSSE-NG-7.5.1/', 'Dependencies/', 'DependencyProvenance/')):
            data = old.read(entry)
            dependency_hashes[entry.filename] = hashlib.sha256(data).hexdigest()
            output.writestr(entry.filename, data)
    for relative, path in sorted(files.items()): output.write(path, relative)
with zipfile.ZipFile(source_archive) as check:
    assert check.testzip() is None
    for relative, path in files.items(): assert hashlib.sha256(check.read(relative)).hexdigest() == digest(path), relative
    for relative, expected in dependency_hashes.items(): assert hashlib.sha256(check.read(relative)).hexdigest() == expected, relative
symbols.mkdir()
shutil.copy2(repo / 'build/modern/bin/Release/Wayfarer.pdb', symbols / 'Wayfarer.pdb')
for output in (archive, source_archive): output.with_suffix('.zip.sha256').write_text(digest(output) + '  ' + output.name + '\n')
print(json.dumps({'archive': str(archive), 'source': str(source_archive), 'dllSHA256': digest(dll),
                  'archiveSHA256': digest(archive), 'sourceSHA256': digest(source_archive)}, indent=2))
