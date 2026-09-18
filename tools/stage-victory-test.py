"""Stage victory revision 2 on the existing matching 0.16.8 test package."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,zipfile

repo=Path(__file__).resolve().parents[1]
review=repo/'build/victory-review-20260915'
baseline=repo/'build/Walk-With-Me-0.16.8-Coexistence-r1'
name='Walk-With-Me-0.16.8-Victory-r2'
stage=repo/'build'/name
archive=stage.parent/(name+'.zip')
source=repo/'build'/(name+'-source')
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if any(p.exists() for p in (stage,archive,source)):
    raise SystemExit('Test artifacts exist; choose a new revision to preserve them.')
for build,count in (('modern',25),('legacy',26)):
    assert f'100% tests passed, 0 tests failed out of {count}' in (review/f'tests-{build}.log').read_text(encoding='utf-8-sig')
    assert 'error C' not in (review/f'build-{build}.log').read_text(encoding='utf-8-sig')
manifest=json.loads((baseline/'test-build.json').read_text())
for relative,expected in manifest['files'].items():
    assert digest(baseline/relative)==expected,relative
for script in (baseline/'Scripts/Source').glob('*.psc'):
    assert script.read_bytes()==(repo/'package/Scripts/Source'/script.name).read_bytes(),script.name
shutil.copytree(baseline,stage)
dll=repo/'build/modern/bin/Release/Wayfarer.dll'
assert b'[Victory] Revision 2:' in dll.read_bytes()
shutil.copy2(dll,stage/'SKSE/Plugins/Wayfarer.dll')
for doc in ('victory.md','usage.md','testing.md','changes-0.16.8.md'):
    shutil.copy2(repo/'docs'/doc,stage/'docs'/doc)
for log in ('build-modern.log','build-legacy.log','tests-modern.log','tests-legacy.log'):
    shutil.copy2(review/log,stage/'test-evidence'/('victory-'+log))
shutil.copy2(review/'clips.json',stage/'test-evidence/victory-clips.json')
(stage/'README.md').write_text('''# Walk With Me 0.16.8 — Victory revision 2 test

After three confirmed enemies die in one fight, participating companions applaud
with empty hands or use the existing Civil War cheer with a drawn sword/greatsword.
Both weapon types use the same vanilla cheer; no separate overhead salute is added.
The party must leave combat/searching for 1.25 seconds. Draw/sheath transitions
wait; movement/animation handoff gets eight seconds, with logged failure reasons.

This complete local test package includes the PRE-EXISTING 0.16.8 coexistence and
hand-facing work. Install the DLL, ESP and scripts together with Skyrim closed,
preferably in a test profile. Preserve your existing INI preferences. The installed
mod has NOT been changed by staging this archive. Do not treat this as a DLL-only
update to an older installation.

Modern build: 25/25 tests. Legacy build: 26/26 tests. The included DLL is modern.
No copied vanilla/third-party animation assets or behavior graph changes were added
for victory reactions. In-game weapon retention, timing, OAR appearance and follower
framework behavior still need testing. See docs/victory.md and
docs/framework-coexistence.md. Startup should log [Victory] Revision 2.
''',encoding='utf-8')
source.mkdir()
for folder in ('src','include','tests','cmake','docs'):
    shutil.copytree(repo/folder,source/folder)
for f in ('CMakeLists.txt','CMakePresets.json','vcpkg.json','vcpkg-configuration.json','LICENSE','LICENSING.md','EXCEPTIONS.md'):
    shutil.copy2(repo/f,source/f)
(source/'tools').mkdir()
for f in ('stage-victory-test.py','verify-package.py','build-compatibility.ps1','build-release.ps1'):
    origin=repo/f if f=='build-release.ps1' else repo/'tools'/f
    shutil.copy2(origin,source/('build-release.ps1' if f=='build-release.ps1' else 'tools/'+f))
shutil.copy2(repo/'build/modern/bin/Release/Wayfarer.pdb',source/'Wayfarer.pdb')
subprocess.run([sys.executable,str(repo/'tools/verify-package.py'),str(stage)],check=True)
record={'build':name,'basedOn':manifest['build'],'dllSHA256':digest(dll),'inGameTested':False,'installedModChanged':False,
    'files':{p.relative_to(stage).as_posix():digest(p) for p in stage.rglob('*') if p.is_file() and p.name!='test-build.json'}}
(stage/'test-build.json').write_text(json.dumps(record,indent=2))
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(stage.rglob('*')):
        if p.is_file():z.write(p,p.relative_to(stage).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for relative,expected in record['files'].items():assert hashlib.sha256(z.read(relative)).hexdigest()==expected
archive.with_suffix('.zip.sha256').write_text(digest(archive)+'  '+archive.name+'\n')
print(json.dumps({'archive':str(archive),'source':str(source),'dllSHA256':digest(dll),'archiveSHA256':digest(archive)},indent=2))
