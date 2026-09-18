# Framework coexistence — 0.16.8 test build, revision 1

**Archived experiment:** withdrawn in Following rollback r1 after repeated
in-game travel stalls. This document describes the earlier build, not the
current behavior. See [following-rollback.md](following-rollback.md).

This is a compatibility experiment with a built and tested implementation. It
does not establish the cause of any reported CTD or demonstrate in-game stability.

## Ownership

WWM and NFF retain separate aliases pointing at the same follower. WWM now keeps
its enrollment during ordinary stops, waiting, combat, unloading, disabling and
Vanilla mode. Removal, dismissal detected by the existing enrollment rules,
exclusion or an invalid actor still removes WWM membership. External API
registrations retain their existing caller-managed lifetime.

Each WWM slot has a new movement-permission global, 0x890–0x899. All six packages
for that slot require its permission, a living actor, no combat, and
WaitingForPlayer == 0. Travel additionally requires the rest global to be off.
The old form and alias IDs remain unchanged. These aliases have only the
Optional and Allow Reserved flags; they do not confer teammate, essential,
protected, inventory, or faction management.

The DLL opens a permission only when both actor and destination aliases match
the native slot. It closes permission on release, without removing enrollment.
On load it closes every permission and rebuilds valid enrollment from saved
aliases before allowing the Papyrus synchronizer to run. A missing global or
package whose first condition lacks the matching permission disables movement
and logs an error. DLL, ESP and scripts must be installed together.

Ordinary ownership changes use EvaluatePackage(false, false), without an AI
reset. Travel and hand-holding replans require an owned package. Stationary
actors running another owner's package cannot trigger WWM's recovery loop.
After observed displacement, WWM yields until the package changes, travel stops
and resumes, or the player issues another order. Arrival can legitimately finish
a travel package; that case can submit a fresh route. Recovery and victory
read package ownership rather than using an empty alias as proof of release.
The script no longer calls ClearKeepOffsetFromActor on another mod's movement.

## NFF policy

- Recruitment, dismissal, inventory, management settings and catch-up remain
  with NFF. WWM does not teleport an actor identified as an NFF follower.
- WWM keeps its authored quest priority of 96. It no longer raises it to 100.
  NFF's Very High alias quest (99) blocks WWM movement for its occupants.
- WWM may replace ordinary NFF follow/default/automatic sandbox behavior while
  a WWM formation or rest request is active. Existing WWM automatic-rest and
  explicit Relax settings still determine when WWM requests rest.
- NFF scenes and active crafting, tavern, gift, sell and sparring quest aliases
  block WWM movement. WWM observes those aliases without modifying them.
- An already-running NFF heal, loot, flee, horse, home or otherwise unknown
  package is not treated as ordinary follow merely because NFF owns the actor.

The ordinary package IDs audited against the installed NFF plugin are 0x47147B
(FollowPKG), 0x4FECA0 (FollowPsvPKG), 0x3A64A7 (DefaultPKG), and 0x19664
(BoxPackPKG). The installed MO2 metadata labels NFF 2.8.6; that metadata is not a
guarantee of its binary provenance. Dedicated task quest IDs are recorded in
src/movement_handoff.cpp. The persistent crafting-assignment quest is excluded
from task detection because assignment alone does not establish an active task.

## Limits and required playtest

There is no universal NFF/WWM API agreement here. WWM cannot infer every task
request hidden underneath its current package, and it does not suspend NFF's
background scripts or change their settings. In particular, initiation of
automatic looting/healing, dynamic speed adjustments and newer NFF versions
still need testing. EFF and SFF have no dedicated adapter in this change.

Use a separate test profile/save and install the full matching test package:

1. Use NFF Normal/High package priority. Walk, stop, resume, change formations,
   and arrive at markers. Check that ordinary travel restarts without a reset loop.
2. Repeat with NFF Very High. WWM should retain enrollment and yield movement.
3. Issue NFF wait/follow and dismiss/recruit commands. Test WWM Party Remove,
   disable/enable and Vanilla. Only membership changes should clear WWM aliases.
4. Trigger crafting, selling, tavern, sparring and scripted scenes while WWM
   is already walking. NFF must take over; WWM should resume after the task ends.
5. Test auto-loot, healing, mounts and catch-up explicitly. Verify that NFF
   performs recalls and that both systems are not moving the actor independently.
6. Compare WWM automatic rest enabled/disabled and explicit Relax. Test furniture,
   combat interruptions, doors, unloaded followers, fast travel and save reload.
7. Remove an actor and fill that slot with another while scripts are busy.
   Neither the old occupant nor the new occupant may receive a stale target.
8. Repeat with hand holding off, then on. Capture any crash independently of
   movement-stall diagnostics; retain the full crash log and exact DLL hash.

The follower diagnostic log includes movementAllowed, ownershipYield and
displacedBy, plus grant/release events. Compare package/alias transitions and
physical movement; a passing unit test does not establish engine behavior.

## Build and verification

The modern CommonLib build is used. CTest includes ownership displacement,
resumption, slot identity, incomplete alias synchronization and unowned recovery
scenarios. tools/verify-package.py checks the emitted conditions on all 60
packages and the compiled script/native bridge names. The pre-existing logging
tests required find_package(spdlog) in the parent CMake scope to configure this
modern build; that build correction is included.

The test build includes the pre-existing local follower diagnostics, hand-facing
and travel changes present when this work started. Compare against that local
baseline when isolating the effect of this handoff change.
