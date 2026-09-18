# TETHER handholding adaptation — revision 12

Historical report. The current revision 13 implementation and test instructions
are in [tether-r13-handoff.md](tether-r13-handoff.md).

## Status: withdrawn after in-game regression

The September 15 test produced five approach attempts and zero physics
operations. The user also reported worse following; the logs show repeated
travel stalls. The installed mod was restored to Rest HUD r3, with current
settings preserved. Revision 12 remains a development draft in this source
tree and must not be treated as the currently installed or validated build.
Following rollback r1 subsequently restored the full 0.16.7 movement backend;
see [following-rollback.md](following-rollback.md).
See [the regression report](tether-r12-regression-20260915.md).

Handholding physics and elastic speed behavior are adapted from
[TETHER by KEI997](https://github.com/Kei-BWV997/TETHER), MIT,
Copyright (c) 2026 KEI997. Upstream revision:
`19e04c23a3dac77c5345b8101d6a94df9895d35f`.
The complete license and provenance are in ThirdParty/TETHER-LICENSE.txt and
TETHER-NOTICE.txt. Credits also appear in Travel, the README and Nexus text.

## Behavior

Use the existing experimental handholding toggle, selected companion, and
Companion mode. Native pathfinding brings the selected actor to the chosen side.
Connection waits for aligned actors, free hands, line of sight, and wrists within
28 units. The actors must be within the configured connection distance.
The selected slot's travel arrival radius temporarily narrows from its ordinary
value (normally 65 units) to 5 units, then restores on release. This was intended
to bring the actor close enough, but the in-game test did not establish a grip.

Like TETHER, this backend adds the actors' ragdoll bodies to the physics world
and connects the hand bones with a Havok ball-and-socket constraint. Either side
is supported. Each Papyrus operation validates the current session and skeleton;
the next operation waits for the previous callback. Held status requires the
constraint function to return true. Cancelled setup rolls back completed steps.
A five-second callback timeout cancels acquisition and waits for the outstanding
operation before cleanup. No new session starts while cleanup is pending.

The existing WWM speed hook applies TETHER-derived distance interpolation:
player speed scales from 1.0 to 0.55 as separation grows; the partner gets 1.3x.
These factors replace WWM's ordinary speed factor for the pair while connected,
so two WWM speed adjustments do not multiply. Other actors retain normal settings.

WWM retains native formation pathfinding and its framework handoff checks.
It does not use TETHER's KeepOffsetFromActor or clear another mod's offset.
The previous handholding implementation remains in the source for comparison,
but is excluded from this build: its position, heading, collision and animation
graph override hooks are not installed. The Blender clasp draft is not included.

Equipment changes, rest, disable/removal, combat, weapons, sprinting, jumping,
swimming, scenes, dialogue, first-person camera, interior/worldspace changes,
large movement discontinuities and excessive separation release the connection.
Valid exterior-to-exterior cell transitions keep it. Cleanup checks each actor's
captured skeleton, so a stale callback cannot remove physics from a replacement
skeleton. Follower and player teardown are attempted independently.
After load/new game, old session tickets are invalidated before accepting a new
grip; old Papyrus stacks cannot act on the newly loaded actors. Failed cleanup
dispatches retain ownership and retry instead of reporting a successful release.

## Assets and dependencies

Install the complete matching DLL and scripts, including WayfarerTether.pex and
the updated Wayfarer.pex. No new ESP records are required. TETHER itself is not a
dependency. Its OAR SDK and animation packs are not bundled.

The upstream GitHub repository has no HKX files. Consequently this build adapts
the connection system, not the exact authored motion shown in a TETHER video.
Ordinary installed movement animations remain active. Fine finger contact and
arm orientation are not guaranteed by a ball-and-socket constraint alone.

## Required in-game checks

This is an experimental test build. Automated tests cover sequencing,
cancellation, partial setup failure, stale/duplicate operation results and speed
limits; they cannot establish Havok stability or animation quality.

1. Start with a separate test save; enable the selected companion and wait for
   connection. Test both sides, actor height differences, walking, jogging,
   running, sharp turns, stopping and resuming. Check feet and whole-body jitter.
2. Release and reconnect repeatedly. Logs must show operations 0, 1, 2 for
   setup, followed by 3, 4, 5 for a full cleanup. Failed partial setups only
   clean resources already acquired. Speed must return immediately on release.
3. Change armor and weapons on either actor, draw weapons, enter combat, jump,
   use furniture, start dialogue, switch camera, disable handholding, remove
   the companion, choose Relax or Vanilla, and issue NFF wait/task orders.
4. Test load/reload while connected, fast travel, interior doors, exterior cell
   boundaries and a follower unloading. Confirm both actors return to normal.
5. Check headtracking, Precision/HDT physics, collision with nearby actors,
   follower-framework movement and unrelated companions' speed.

Upstream reports possible crashes during abrupt ragdoll/3D state changes and
occasional follower stalls. Sequencing checks reduce specific setup/cleanup races;
they do not prove those engine issues fixed. Existing physics ownership from
other mods is not fully observable through these Papyrus functions. Do not run
the standalone TETHER handhold on the same pair while this grip is enabled.
