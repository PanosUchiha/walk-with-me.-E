"""Stage the companion Unstick action over the verified installed Companion UI r3 build."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys

repo = Path(__file__).resolve().parents[1]
installed = repo.parent / 'MODS/mods/Wayfarer'
stage = repo / 'build/Walk-With-Me-0.16.8-Companion-Unstick-r1'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert not stage.exists(), 'Preserve previous artifacts.'
previous = json.loads((installed / 'test-build.json').read_text(encoding='utf-8-sig'))
assert previous['build'] == 'Walk-With-Me-0.16.8-Companion-UI-r3'
for relative, expected in previous['files'].items():
    if relative not in ('SKSE/Plugins/Wayfarer.ini', 'meta.ini'):
        assert digest(installed / relative) == expected, relative
allowed = {'src/menu_ui.cpp', 'src/formation_controller.cpp', 'src/papyrus_bridge.cpp', 'include/companion_menu.h', 'include/menu_ui.h', 'include/formation_controller.h', 'include/companion_recovery.h', 'tests/package_control_tests.cpp', 'package/Scripts/Source/WayfarerRecovery.psc'}
for relative, expected in previous['sourceHashes'].items():
    assert relative in allowed or digest(repo / relative) == expected, relative
for kind in ('modern', 'legacy'):
    assert '100% tests passed, 0 tests failed out of 19' in (repo / f'build/companion-unstick-r1-{kind}-tests.log').read_text(encoding='utf-8-sig')
    log = (repo / f'build/companion-unstick-r1-{kind}-build.log').read_text(encoding='utf-8-sig')
    assert not any(e in log for e in ('error C', 'error LNK', 'error MSB'))
assert 'Actual companion panel ImGui clicks: 0 failures' in (repo / 'build/companion-unstick-r1-preview.log').read_text(encoding='utf-8-sig')
shutil.copytree(installed, stage)
dll = repo / 'build/modern/bin/Release/Wayfarer.dll'
for marker in (b'[HandHolding] Revision 18:', b'[Victory] Revision 4:', b'[PackageControl] Popup r1:'):
    assert marker in dll.read_bytes(), marker
shutil.copy2(dll, stage / 'SKSE/Plugins/Wayfarer.dll')
assert '1 succeeded, 0 failed' in (repo / 'build/companion-unstick-r1-papyrus.log').read_text(encoding='utf-8-sig')
shutil.copy2(repo / 'build/companion-unstick-r1-scripts/WayfarerRecovery.pex', stage / 'Scripts/WayfarerRecovery.pex')
shutil.copy2(repo / 'package/Scripts/Source/WayfarerRecovery.psc', stage / 'Scripts/Source/WayfarerRecovery.psc')
shutil.copy2(repo / 'docs/package-control.md', stage / 'docs/package-control.md')
evidence = stage / 'test-evidence'
evidence.mkdir(exist_ok=True)
for path in (repo / 'build').glob('companion-unstick-r1-*.log'):
    shutil.copy2(path, evidence)
for path in (repo / 'build/wheel-visual-proof').glob('wheel-*.png'):
    shutil.copy2(path, evidence / ('companion-unstick-r1-' + path.name))
for relative in allowed | {'tools/preview-order-wheel.py', 'tools/stage-companion-unstick-r1.py', 'tools/verify-package.py', 'build-release.ps1'}:
    target = evidence / 'companion-unstick-r1-source' / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(repo / relative, target)
(stage / 'COMPANION-UNSTICK-r1.md').write_text(
    '# Companion Unstick r1\n\n'
    'Aim at a companion, press G, then choose Unstick (U / gamepad X). '
    'After the menu closes, one gentle PushActorAway call can release a frozen animation. '
    'Uses a fresh target/state check, a single-use ticket, a five-second expiry and '
    'a three-second cooldown. Travel ownership and recruitment are unchanged.\n\n'
    'Built over Companion UI r3 with TETHER r18 preserved. Includes matching '
    'WayfarerRecovery.pex and DLL; both must be installed together. Both native builds '
    'pass 19/19 tests; the recovery script compiles without errors or warnings; '
    'offline panel click and visual checks pass. Physical recovery and gamepad input '
    'still require in-game playtesting. Settings are preserved.\n', encoding='utf-8')
subprocess.run([sys.executable, str(repo / 'tools/verify-package.py'), str(stage), '--installed-settings'], check=True)
subprocess.run([sys.executable, str(repo / 'tools/verify-menu-exports.py')], check=True)
manifest = {
    'build': stage.name, 'assetsBasedOn': previous['build'], 'inGameTested': False,
    'nativeTests': {'modern': '19/19', 'legacy': '19/19'}, 'offlineVisualQAChecked': True,
    'dllSHA256': digest(dll), 'legacyDLLSHA256': digest(repo / 'build/skyrim/bin/Release/Wayfarer.dll'),
    'sourceHashes': {p.relative_to(repo).as_posix(): digest(p)
                     for folder in ('src', 'include', 'tests', 'package/Scripts/Source')
                     for p in (repo / folder).rglob('*') if p.is_file()},
    'files': {p.relative_to(stage).as_posix(): digest(p) for p in stage.rglob('*')
              if p.is_file() and p.name not in ('test-build.json', 'manifest.sha256')}
}
(stage / 'test-build.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
(stage / 'manifest.sha256').write_text(''.join(f'{sha}  {path}\n' for path, sha in sorted(manifest['files'].items())), encoding='utf-8')
print(json.dumps({'stage': str(stage), 'dllSHA256': digest(dll)}, indent=2))
