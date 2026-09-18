"""Stage the UI-only r3 update over the verified installed TETHER r18 build."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys

repo = Path(__file__).resolve().parents[1]
installed = repo.parent / 'MODS/mods/Wayfarer'
stage = repo / 'build/Walk-With-Me-0.16.8-Companion-UI-r3'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert not stage.exists(), 'Preserve previous artifacts.'
previous = json.loads((installed / 'test-build.json').read_text(encoding='utf-8-sig'))
assert previous['build'] == 'Walk-With-Me-0.16.8-TETHER-r18'
for relative, expected in previous['files'].items():
    if relative not in ('SKSE/Plugins/Wayfarer.ini', 'meta.ini'):
        assert digest(installed / relative) == expected, relative
allowed = {'src/menu_ui.cpp', 'include/companion_menu.h', 'tests/package_control_tests.cpp'}
for relative, expected in previous['sourceHashes'].items():
    assert relative in allowed or digest(repo / relative) == expected, relative
for kind in ('modern', 'legacy'):
    assert '100% tests passed, 0 tests failed out of 19' in (repo / f'build/companion-ui-r3-{kind}-tests.log').read_text(encoding='utf-8-sig')
    log = (repo / f'build/companion-ui-r3-{kind}-build.log').read_text(encoding='utf-8-sig')
    assert not any(e in log for e in ('error C', 'error LNK', 'error MSB'))
assert 'Actual companion panel ImGui clicks: 0 failures' in (repo / 'build/companion-ui-r3-preview.log').read_text(encoding='utf-8-sig')
shutil.copytree(installed, stage)
dll = repo / 'build/modern/bin/Release/Wayfarer.dll'
for marker in (b'[HandHolding] Revision 18:', b'[Victory] Revision 4:', b'[PackageControl] Popup r1:'):
    assert marker in dll.read_bytes(), marker
shutil.copy2(dll, stage / 'SKSE/Plugins/Wayfarer.dll')
shutil.copy2(repo / 'docs/package-control.md', stage / 'docs/package-control.md')
evidence = stage / 'test-evidence'
evidence.mkdir(exist_ok=True)
for path in (repo / 'build').glob('companion-ui-r3-*.log'):
    shutil.copy2(path, evidence)
for path in (repo / 'build/wheel-visual-proof').glob('wheel-*.png'):
    shutil.copy2(path, evidence / ('companion-ui-r3-' + path.name))
for relative in allowed | {'tools/preview-order-wheel.py', 'tools/stage-companion-ui-r3.py'}:
    target = evidence / 'companion-ui-r3-source' / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(repo / relative, target)
(stage / 'COMPANION-UI-r3.md').write_text(
    '# Companion UI r3\n\n'
    'Wheel-styled footer controls and a labeled Cancel button for the targeted NPC panel. '
    'Staggered entrances, icon ring reveals, hover lift, eased presses, activation feedback '
    'and a 200 ms input-locked exit. The wheel-to-panel handoff preserves the backdrop.\n\n'
    'Built over the installed TETHER r18 source and assets. Only menu_ui.cpp and '
    'companion_menu.h change runtime behavior. Settings, scripts, ESP and character '
    'animation assets are preserved. Modern and legacy native suites pass 19/19; actual '
    'ImGui click tests and visual previews pass. In-game feel and pause/input integration '
    'still need playtesting. Restart Skyrim after installing.\n', encoding='utf-8')
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
