"""Stage a matching local test build without modifying the installed mod."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

repo = Path(__file__).resolve().parents[1]
name = 'Walk-With-Me-0.16.8-Coexistence-r1'
stage = repo / 'build' / name
archive = stage.parent / (name + '.zip')
source = repo / 'build' / (name + '-source')
symbols = repo / 'build' / (name + '-symbols')
if any(p.exists() for p in (stage, archive, source, symbols)):
    raise SystemExit('Test artifacts already exist. Choose a new revision; do not overwrite prior evidence.')

build = repo / 'build/modern'
tests = (build / 'handoff-tests.log').read_text(encoding='utf-8-sig')
if '100% tests passed, 0 tests failed out of 25' not in tests:
    raise SystemExit('The handoff test run must pass before staging.')
dll = build / 'bin/Release/Wayfarer.dll'
shutil.copy2(dll, repo / 'package/SKSE/Plugins/Wayfarer.dll')
shutil.copytree(repo / 'package', stage)
for doc in ('framework-coexistence.md', 'changes-0.16.8.md', 'follower-diagnostics.md'):
    shutil.copy2(repo / 'docs' / doc, stage / 'docs' / doc)
(stage / 'README.md').write_text('''# Walk With Me 0.16.8 — coexistence test, revision 1

Install this complete package together in a separate test profile, with Skyrim
closed. Keep your existing INI preferences if desired. Do not combine DLL,
ESP or scripts from different builds. No installed mod files were changed by
staging this archive.

This build keeps WWM enrollment separate from movement ownership and adds NFF
handoff safeguards. It includes the local diagnostic/hand-facing baseline.
Native build, Papyrus compilation, 25 tests and package checks passed; in-game
NFF coexistence and the cause of the reported CTDs remain unverified.

Read docs/framework-coexistence.md for behavior, limitations and the required
playtest. Use docs/follower-diagnostics.md to capture evidence.
''', encoding='utf-8')
evidence = stage / 'test-evidence'
evidence.mkdir()
for log in ('handoff-tests.log', 'handoff-build.log', 'handoff-final-build.log'):
    shutil.copy2(build / log, evidence / log)

# Snapshot the sources that accompany this exact DLL, including pre-existing
# local edits. Dependency provenance remains in the repository's build docs.
source.mkdir()
for directory in ('src', 'include', 'tests', 'cmake', 'tools', 'docs', 'LICENSES'):
    shutil.copytree(repo / directory, source / directory, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
for file in ('CMakeLists.txt', 'CMakePresets.json', 'vcpkg.json', 'vcpkg-configuration.json', 'LICENSE', 'LICENSING.md', 'EXCEPTIONS.md'):
    shutil.copy2(repo / file, source / file)
shutil.copytree(stage, source / 'package')
symbols.mkdir()
shutil.copy2(build / 'bin/Release/Wayfarer.pdb', symbols / 'Wayfarer.pdb')
hash_file = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
manifest = {
    'build': name,
    'dllSHA256': hash_file(dll),
    'pdbSHA256': hash_file(symbols / 'Wayfarer.pdb'),
    'inGameTested': False,
    'installedModChanged': False,
    'files': {p.relative_to(stage).as_posix(): hash_file(p) for p in sorted(stage.rglob('*')) if p.is_file()},
    'sourceFiles': {p.relative_to(source).as_posix(): hash_file(p) for p in sorted(source.rglob('*')) if p.is_file()},
}
(stage / 'test-build.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
subprocess.run([sys.executable, str(repo / 'tools/verify-package.py'), str(stage)], check=True)
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for file in sorted(stage.rglob('*')):
        if file.is_file(): z.write(file, file.relative_to(stage).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for file, expected in manifest['files'].items():
        assert hashlib.sha256(z.read(file)).hexdigest() == expected
archive.with_suffix('.zip.sha256').write_text(hash_file(archive) + '  ' + archive.name + '\n', encoding='ascii')
print(json.dumps({'archive': str(archive), 'source': str(source), 'symbols': str(symbols),
                  'dllSHA256': manifest['dllSHA256'], 'zipSHA256': hash_file(archive)}, indent=2))
