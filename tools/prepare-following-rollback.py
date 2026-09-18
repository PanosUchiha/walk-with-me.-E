"""Restore reviewed 0.16.7 movement sources and retain later victory/HUD features."""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import subprocess
import zipfile

repo = Path(__file__).resolve().parents[1]
baseline = '99fa995'  # 0.16.7 plus the subsequent manual-enrollment fixes.
stamp = datetime.now().strftime('%Y%m%d-%H%M%S')
backup = repo / 'backups' / f'source-before-following-rollback-{stamp}.zip'
paths = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=repo).decode().split('\0')
with zipfile.ZipFile(backup, 'x', zipfile.ZIP_DEFLATED) as archive:
    for name in sorted(set(filter(None, paths))):
        path = repo / name
        if path.is_file():
            archive.write(path, name)
with zipfile.ZipFile(backup) as archive:
    assert archive.testzip() is None

restore = [
    'CMakeLists.txt', 'build-release.ps1', 'include/formation_controller.h',
    'include/party_constants.h', 'include/hand_holding.h',
    'include/hand_hold_locomotion.h', 'include/hand_hold_overrides.h',
    'src/formation_controller.cpp', 'src/travel_planner.cpp',
    'src/travel_speed.cpp', 'src/hand_holding.cpp', 'src/plugin.cpp',
    'src/papyrus_bridge.cpp', 'tests/hand_hold_tests.cpp',
    'tests/travel_planner_tests.cpp', 'tests/party_layout_tests.cpp',
    'package/Scripts/Source/Wayfarer.psc',
    'package/Scripts/Source/WayfarerQuestScript.psc',
    'tools/build-esl.ps1', 'tools/verify-package.py',
]
for name in restore:
    (repo / name).write_bytes(subprocess.check_output(['git', 'show', f'{baseline}:{name}'], cwd=repo))

def replace(name, old, new):
    path = repo / name
    data = path.read_text(encoding='utf-8')
    assert data.count(old) == 1, (name, old[:70], data.count(old))
    path.write_text(data.replace(old, new), encoding='utf-8')

# Keep this identifiable as a combined build, not the original 0.16.7 release.
replace('CMakeLists.txt', 'project(Wayfarer VERSION 0.16.7', 'project(Wayfarer VERSION 0.16.8')
replace('include/formation_controller.h', '#include "victory_encounter.h"', '#include "victory_encounter.h"\n#include "victory_reaction.h"')
replace('include/formation_controller.h', 'bool VictoryActorReady(', 'std::string VictoryActorRejection(')
replace('include/formation_controller.h', 'bool inspectBanterScene=false) const;', 'bool inspectBanterScene=false, bool allowDrawnWeapon=false) const;')
replace('include/formation_controller.h', '            bool started{};', '''            bool started{};
            std::uint32_t variation{};
            float nextAttempt{};
            Victory::Equipment equipment{Victory::Equipment::Unsupported};
            std::string lastReason;''')
replace('src/formation_controller.cpp', 'bool inspectBanterScene) const', 'bool inspectBanterScene, bool allowDrawnWeapon) const')
replace('src/formation_controller.cpp', 'if (!held && actorState->IsWeaponDrawn())', 'if (!held && !allowDrawnWeapon && actorState->IsWeaponDrawn())')
replace('src/formation_controller.cpp', '        InitializeVictoryEvents();', '''        InitializeVictoryEvents();
        logger::info("[Following] Rollback r1: 0.16.7 movement, alias lifecycle and AI refresh; victory r2 and rest HUD r3 retained");''')

# Victory waits for the original alias-release lifecycle, not the withdrawn gates.
replace('src/victory_celebration.cpp', '''            const int slot = managed.at(id).slot;
            if (aliasSyncBusy || OwnsMovementPackage(actor, slot) ||
                (movementGates[slot] && movementGates[slot]->value != 0)) return "movement handoff pending";''', '''            const auto* alias = GetRefAlias(questCache, FollowerAlias(managed.at(id).slot));
            if (aliasSyncBusy || (alias && alias->GetReference() == &actor)) return "movement handoff pending";''')

# Keep the rest/HUD and victory UI, remove help text describing withdrawn behavior.
menu = repo / 'src/menu_ui.cpp'
menu.write_bytes((repo / 'build/Walk-With-Me-0.16.8-Rest-HUD-r3-source/src/menu_ui.cpp').read_bytes())
replace('src/menu_ui.cpp', ' NFF manages teleporting its own followers.', '')
replace('src/menu_ui.cpp', ' NFF tasks and its Very High priority setting retain control.', '')

record = {'baseline': baseline, 'backup': str(backup), 'restoredFiles': restore,
          'featuresRetained': ['victory-r2', 'rest-hud-r3', 'manual-enrollment', 'logging-bootstrap'],
          'backupSHA256': hashlib.sha256(backup.read_bytes()).hexdigest()}
(repo / 'build/following-rollback-source.json').write_text(json.dumps(record, indent=2))
print(json.dumps(record, indent=2))
