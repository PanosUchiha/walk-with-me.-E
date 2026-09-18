"""Stage shoulder recovery over the verified installed Companion UI r2 build."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,zipfile

repo=Path(__file__).resolve().parents[1]
installed=repo.parent/'MODS/mods/Wayfarer'
review=repo/'build/victory-r4-review'
stage=repo/'build/Walk-With-Me-0.16.8-Victory-r4'
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
original=json.loads((repo/'build/Walk-With-Me-0.16.8-Companion-UI-r2/test-build.json').read_text(encoding='utf-8-sig'))
receipt=json.loads((installed/'companion-ui-install.json').read_text(encoding='utf-8-sig'))
assert original['build']==receipt['build']=='Walk-With-Me-0.16.8-Companion-UI-r2'
assert digest(installed/'SKSE/Plugins/Wayfarer.dll')==receipt['installedDLLSHA256']==original['dllSHA256']
for relative,expected in original['files'].items():
    if relative not in ('SKSE/Plugins/Wayfarer.ini','meta.ini'):
        assert digest(installed/relative)==expected,relative
for kind in ('modern','manual'):
    assert '100% tests passed, 0 tests failed out of 18' in (review/f'tests-{kind}.log').read_text(encoding='utf-8-sig')
    log=(review/f'build-{kind}.log').read_text(encoding='utf-8-sig')
    assert not any(error in log for error in ('error C','error LNK','error MSB'))
assert not stage.exists(),'Preserve previous staged build.'
shutil.copytree(installed,stage)
dll=repo/'build/modern/bin/Release/Wayfarer.dll'
assert b'[Victory] Revision 4:' in dll.read_bytes() and b'[Following] Rollback r1:' in dll.read_bytes()
assert all(marker in dll.read_bytes() for marker in (b'[PackageControl] Popup r1:',b'[Enrollment] NFF roster guard r1:',b'[PartyRecall] Summoned'))
shutil.copy2(dll,stage/'SKSE/Plugins/Wayfarer.dll')
shutil.copytree(repo/'package/meshes/OpenAnimationReplacer/Walk With Me Victory',stage/'meshes/OpenAnimationReplacer/Walk With Me Victory')
for name in ('NOTICE.txt','OpenAnimationReplacer-COPYING.txt','OpenAnimationReplacer-EXCEPTIONS.txt'):
    shutil.copy2(repo/'package/ThirdParty'/name,stage/'ThirdParty'/name)
shutil.copy2(repo/'LICENSING.md',stage/'LICENSING.md')
for name in ('victory.md','shoulder-rub.md'):shutil.copy2(repo/'docs'/name,stage/'docs'/name)
for kind in ('modern','manual'):
    for action in ('build','tests'):
        shutil.copy2(review/f'{action}-{kind}.log',stage/f'test-evidence/victory-r4-{action}-{kind}.log')
for name in ('authoring.json','validation.json','export.json'):
    shutil.copy2(repo/'build/shoulder-recovery-v6'/name,stage/f'test-evidence/shoulder-recovery-v6-{name}')
shutil.copy2(review/'source-baseline.json',stage/'test-evidence/victory-r4-source-baseline.json')
clip=stage/'meshes/OpenAnimationReplacer/Walk With Me Victory/Shoulder Recovery/actors/character/animations/npc_applaud3.hkx'
export=json.loads((repo/'build/shoulder-recovery-v6/export.json').read_text())
assert digest(clip)==export['sha256'] and export['duration']==6.5 and export['frames']==196
config=json.loads((clip.parents[3]/'config.json').read_text())
assert config['conditions']==[{'condition':'WWM_PostCombatShoulderRecovery','requiredVersion':'0.16.8.0','requiredPlugin':'Walk With Me'}]
assert config['priority']==27102000

(stage/'VICTORY-R4.md').write_text('''# Victory revision 4 — shoulder recovery

One in four participating companions can choose the user-approved shoulder
recovery after a qualifying encounter. This choice waits for complete sheathing,
including unarmed draw states. It lasts 6.5 seconds and uses WWM's existing
combat, dialogue, movement, equipment-change and order cancellation rules.

Requires Open Animation Replacer 3.1 or newer. The actor-scoped replacement
does not replace ordinary applause outside a selected shoulder reaction.
No ESP, Papyrus, behavior graph, following behavior or INI changes.
No Pandora regeneration is required. Restart Skyrim after installing.

Includes the installed Companion UI r2, Party Orders and Following Rollback r1.
Both native builds pass 18/18 tests. HKX and package checks pass.
Visible in-game playback and body/armor fit remain to be tested.
''',encoding='utf-8')
subprocess.run([sys.executable,str(repo/'tools/verify-package.py'),str(stage)],check=True)
record=dict(original)
record.update(build=stage.name,basedOn=original['build'],dllSHA256=digest(dll),inGameTested=False)
record['nativeTests']={'modern':'18/18','legacy':'18/18'}
record['legacyDLLSHA256']=digest(repo/'build/manual/bin/Release/Wayfarer.dll')
record['featuresRetained']=['companion-ui-r2','party-orders','enrollment-roster-guard','following-rollback-r1','victory-r4','sheathed-shoulder-recovery-v6']
record['sourceHashes']={p.relative_to(repo).as_posix():digest(p) for folder in ('src','include','tests') for p in (repo/folder).rglob('*') if p.is_file()}
record['files']={p.relative_to(stage).as_posix():digest(p) for p in stage.rglob('*') if p.is_file() and p.name not in ('test-build.json','manifest.sha256')}
(stage/'test-build.json').write_text(json.dumps(record,indent=2))
(stage/'manifest.sha256').write_text(''.join(f'{digest(p)}  {p.relative_to(stage).as_posix()}\n' for p in sorted(stage.rglob('*')) if p.is_file() and p.name!='manifest.sha256'))
changes=[]
for p in stage.rglob('*'):
    if not p.is_file():continue
    relative=p.relative_to(stage).as_posix();target=installed/relative
    before=digest(target) if target.exists() else None
    if before!=digest(p):changes.append({'relative':relative,'source':str(p),'before':before,'sha256':digest(p)})
assert all(item['relative'] not in ('Wayfarer.esp','SKSE/Plugins/Wayfarer.ini') and not item['relative'].startswith('Scripts/') for item in changes)
manifest={'build':stage.name,'installedRoot':str(installed),'stage':str(stage),'files':changes,'inGameTested':False}
(review/'update-manifest.json').write_text(json.dumps(manifest,indent=2))
with zipfile.ZipFile(stage.with_suffix('.zip'),'x',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(stage.rglob('*')):
        if p.is_file():z.write(p,p.relative_to(stage).as_posix())
with zipfile.ZipFile(stage.with_suffix('.zip')) as z:assert z.testzip() is None
with zipfile.ZipFile(review/'source-and-symbols.zip','x',zipfile.ZIP_DEFLATED) as z:
    for p in (repo/'build/victory-r4-current-source').rglob('*'):
        if p.is_file():z.write(p,p.relative_to(repo/'build/victory-r4-current-source').as_posix())
    for name in ('snapshot-victory-r4-source.py','stage-victory-r4.py','install-victory-r4.ps1'):
        z.write(repo/'tools'/name,'tools/'+name)
    z.write(repo/'build/modern/bin/Release/Wayfarer.pdb','Wayfarer.pdb')
print(json.dumps({'stage':str(stage),'changedFiles':len(changes),'dllSHA256':digest(dll),'clipSHA256':digest(clip)},indent=2))
