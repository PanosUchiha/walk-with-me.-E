"""Stage the Party Orders utilities test build from verified Companion Control assets."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

repo = Path(__file__).resolve().parents[1]
name = 'Walk-With-Me-0.16.8-Party-Orders-r1'
baseline = repo / 'build/Walk-With-Me-0.16.8-Companion-Control-r1'
stage = repo / 'build' / name
archive = stage.parent / (name + '.zip')
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
assert not stage.exists() and not archive.exists(), 'Preserve prior artifacts.'
previous = json.loads((baseline / 'test-build.json').read_text(encoding='utf-8-sig'))
for relative, expected in previous['files'].items():
    assert digest(baseline / relative) == expected, relative
for kind in ('modern', 'legacy'):
    log = repo / f'build/party-orders-r1-{kind}-tests.log'
    assert '100% tests passed, 0 tests failed out of 18' in log.read_text(encoding='utf-8-sig')
    build_log = (repo / f'build/party-orders-r1-{kind}-build.log').read_text(encoding='utf-8-sig')
    assert not any(error in build_log for error in ('error C', 'error LNK', 'error MSB'))
for source in (repo / 'package/Scripts/Source').glob('*.psc'):
    assert source.read_bytes() == (baseline / 'Scripts/Source' / source.name).read_bytes(), source.name

shutil.copytree(baseline, stage)
dll = repo / 'build/modern/bin/Release/Wayfarer.dll'
for marker in (b'[PackageControl] Popup r1:', b'[Enrollment] NFF roster guard r1:', b'[PartyRecall] Summoned'):
    assert marker in dll.read_bytes(), marker
shutil.copy2(dll, stage / 'SKSE/Plugins/Wayfarer.dll')
shutil.copy2(repo / 'README.md', stage / 'README.md')
shutil.copy2(repo / 'docs/usage.md', stage / 'USAGE.md')
for doc in ('usage.md', 'party-orders-utilities.md'):
    shutil.copy2(repo / 'docs' / doc, stage / 'docs' / doc)
for kind in ('modern', 'legacy'):
    for task in ('build', 'tests'):
        shutil.copy2(repo / f'build/party-orders-r1-{kind}-{task}.log', stage / 'test-evidence')
for preview in ('wheel-companion.png', 'wheel-relax.png'):
    shutil.copy2(repo / 'build/wheel-visual-proof' / preview, stage / 'test-evidence' / preview)
(stage / 'TESTING.md').write_text(
    '# Party Orders utilities r1 test build\n\n'
    'Install this complete mod with MO2 or Vortex, preserving your Wayfarer.ini. '
    'Restart Skyrim after replacing the DLL. This local 0.16.8 test build includes '
    'Companion Control popup r1, the NFF roster guard and Following rollback r1.\n\n'
    'Open Party Orders and use the mouse action row below the wheel: Teleport party '
    'recalls eligible travelling companions; Temporary relaxation toggles automatic '
    'rest without disabling explicit Relax. Read docs/party-orders-utilities.md '
    'for behavior, limitations and in-game checks.\n\n'
    'Both native builds pass 18 test suites. Menu export validation passes. '
    'Offline visual previews were checked; this build has not been playtested '
    'in Skyrim or published.\n', encoding='utf-8')
subprocess.run([sys.executable, str(repo / 'tools/verify-package.py'), str(stage)], check=True)
subprocess.run([sys.executable, str(repo / 'tools/verify-menu-exports.py')], check=True)
manifest = {
    'build': name, 'assetsBasedOn': previous['build'],
    'nativeTests': {'modern': '18/18', 'legacy': '18/18'},
    'inGameTested': False, 'offlineVisualQAChecked': True,
    'dllSHA256': digest(dll),
    'legacyDLLSHA256': digest(repo / 'build/skyrim/bin/Release/Wayfarer.dll'),
    'sourceHashes': {p.relative_to(repo).as_posix(): digest(p)
                     for folder in ('src', 'include', 'tests') for p in (repo / folder).rglob('*') if p.is_file()},
    'files': {p.relative_to(stage).as_posix(): digest(p) for p in stage.rglob('*')
              if p.is_file() and p.name not in ('test-build.json', 'manifest.sha256')}
}
(stage / 'test-build.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
(stage / 'manifest.sha256').write_text(''.join(f'{sha}  {path}\n' for path, sha in sorted(manifest['files'].items())), encoding='utf-8')
with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as output:
    for path in sorted(stage.rglob('*')):
        if path.is_file(): output.write(path, path.relative_to(stage).as_posix())
with zipfile.ZipFile(archive) as check:
    assert check.testzip() is None
    for relative, expected in manifest['files'].items():
        assert hashlib.sha256(check.read(relative)).hexdigest() == expected, relative
archive.with_suffix('.zip.sha256').write_text(digest(archive) + '  ' + archive.name + '\n', encoding='utf-8')
print(json.dumps({'archive': str(archive), 'dllSHA256': digest(dll), 'archiveSHA256': digest(archive)}, indent=2))
