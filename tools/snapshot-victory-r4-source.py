"""Verify the installed Companion UI source and snapshot only the gesture update."""
from pathlib import Path
import hashlib,json,shutil
repo=Path(__file__).resolve().parents[1]
baseline=repo/'build/Walk-With-Me-0.16.8-Companion-UI-r2/test-build.json'
record=json.loads(baseline.read_text())
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
changed={'src/plugin.cpp','src/victory_celebration.cpp','include/formation_controller.h','include/victory_reaction.h','tests/victory_encounter_tests.cpp'}
actual={name for name,sha in record['sourceHashes'].items() if digest(repo/name)!=sha}
assert actual==changed,actual
installed=repo.parent/'MODS/mods/Wayfarer'
assert digest(installed/'SKSE/Plugins/Wayfarer.dll')==record['dllSHA256']
out=repo/'build/victory-r4-current-source'
assert not out.exists()
for folder in ('src','include','tests','docs','cmake','third_party/OpenAnimationReplacer'):
    shutil.copytree(repo/folder,out/folder)
for name in ('CMakeLists.txt','LICENSE','EXCEPTIONS.md','LICENSING.md'):
    shutil.copy2(repo/name,out/name)
review=repo/'build/victory-r4-review'
shutil.copy2(review/'source-baseline.json',review/'superseded-r3-source-baseline.json')
manifest={'baseline':record['build'],'baselineManifest':str(baseline),'installedBaselineDLL':record['dllSHA256'],'modifiedExistingSourceFiles':sorted(changed),'newSourceFiles':['src/victory_animation.cpp','include/victory_animation.h'],'otherSourceFilesVerified':len(record['sourceHashes'])-len(changed),'source':str(out),'sourceSHA256':{p.relative_to(out).as_posix():digest(p) for p in out.rglob('*') if p.is_file()}}
(review/'source-baseline.json').write_text(json.dumps(manifest,indent=2))
print(f"Verified {manifest['otherSourceFilesVerified']} existing source files; only the five intended existing files changed. Snapshot: {out}")
