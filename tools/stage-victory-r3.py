"""Stage the victory handoff fix on the verified installed following rollback."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

repo = Path(__file__).resolve().parents[1]
installed = repo.parent / 'MODS/mods/Wayfarer'
review = repo / 'build/victory-r3-review'
name = 'Walk-With-Me-0.16.8-Victory-r3'
stage = repo / 'build' / name
archive = stage.parent / (name + '.zip')
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert not stage.exists() and not archive.exists(), 'Preserve previous artifacts.'
original = json.loads((installed / 'test-build.json').read_text(encoding='utf-8-sig'))
assert original['build'] == 'Walk-With-Me-0.16.8-Following-Rollback-r1'
for relative, expected in original['files'].items():
    if relative not in ('SKSE/Plugins/Wayfarer.ini', 'meta.ini'):
        assert digest(installed / relative) == expected, relative
for kind in ('modern', 'legacy'):
    assert '100% tests passed, 0 tests failed out of 16' in (review / f'tests-{kind}.log').read_text(encoding='utf-8-sig')
    assert not any(error in (review / f'build-{kind}.log').read_text(encoding='utf-8-sig') for error in ('error C', 'error LNK', 'error MSB'))
shutil.copytree(installed, stage)
dll = repo / 'build/modern/bin/Release/Wayfarer.dll'
assert b'[Victory] Revision 3:' in dll.read_bytes()
assert b'[Following] Rollback r1:' in dll.read_bytes()
shutil.copy2(dll, stage / 'SKSE/Plugins/Wayfarer.dll')
shutil.copy2(repo / 'docs/victory.md', stage / 'docs/victory.md')
for kind in ('modern', 'legacy'):
    for action in ('build', 'tests'):
        shutil.copy2(review / f'{action}-{kind}.log', stage / f'test-evidence/victory-r3-{action}-{kind}.log')
(stage / 'VICTORY-R3.md').write_text('''# Victory revision 3

Built on Following Rollback r1. Wait for a completed alias cleanup after the
encounter; subsequent routine scans no longer block released companions.
Allow fifteen seconds for natural recovery and sheathing. Log accepted starts,
lost idle ownership and equipment/state cancellations. No new voice audio.

Modern and legacy native suites: 16/16 each. Visible in-game playback is pending.
The complete package retains the installed INI, ESP, scripts, animations and
following behavior. Startup identifies Victory Revision 3 and Following Rollback r1.
''', encoding='utf-8')
record = dict(original)
record.update(build=name, basedOn=original['build'], dllSHA256=digest(dll), inGameTested=False)
record['featuresRetained'] = ['victory-r3' if x == 'victory-r2' else x for x in original['featuresRetained']]
record['files'] = {p.relative_to(stage).as_posix(): digest(p) for p in stage.rglob('*') if p.is_file() and p.name not in ('test-build.json', 'manifest.sha256')}
(stage / 'test-build.json').write_text(json.dumps(record, indent=2))
(stage / 'manifest.sha256').write_text(''.join(f'{digest(p)}  {p.relative_to(stage).as_posix()}\n' for p in sorted(stage.rglob('*')) if p.is_file() and p.name != 'manifest.sha256'))
subprocess.run([sys.executable, str(repo / 'tools/verify-package.py'), str(stage)], check=True)
with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as output:
    for p in sorted(stage.rglob('*')):
        if p.is_file(): output.write(p, p.relative_to(stage).as_posix())
with zipfile.ZipFile(archive) as check:
    assert check.testzip() is None
    for relative, expected in record['files'].items():
        assert hashlib.sha256(check.read(relative)).hexdigest() == expected, relative
archive.with_suffix('.zip.sha256').write_text(digest(archive) + '  ' + archive.name + '\n')
with zipfile.ZipFile(review / 'source-and-symbols.zip', 'x', zipfile.ZIP_DEFLATED) as output:
    for folder in ('src', 'include', 'tests', 'docs'):
        for p in (repo / folder).rglob('*'):
            if p.is_file(): output.write(p, p.relative_to(repo).as_posix())
    output.write(repo / 'CMakeLists.txt', 'CMakeLists.txt')
    output.write(repo / 'build/modern/bin/Release/Wayfarer.pdb', 'Wayfarer.pdb')
print(json.dumps({'stage': str(stage), 'archive': str(archive), 'dllSHA256': digest(dll)}, indent=2))
