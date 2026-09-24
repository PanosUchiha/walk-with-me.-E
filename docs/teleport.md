# Teleport

Covers automatic catch-up (**Teleport distant companions** under Travel) and the
**Teleport party** button on the wheel. Code: `UpdateTeleportCatchup`, `FindLanding`
and `RecallParty` in `src/formation_controller.cpp`. The pure parts are in
`include/follower_catchup.h`, tested by `WayfarerCatchup`.

## Why it was rewritten (2026-09-23)

Logs from 09-16 to 09-23 showed every automatic catch-up firing late: companions
were recalled from 6,000 to 23,000 units when the trigger was 2,463. There were
three causes.

1. Catch-up ran inside the travel update. Standing still ("waiting for sustained
   movement"), riding, sneaking, a drawn weapon, combat or swimming returned before
   it was reached. `ReleaseAll` also reset its two-second timer every frame in those
   states.
2. Landing needed a straight navmesh line to one of six spots 220-400 units behind
   you. The ground there had to be within 32 units of your height, and five samples
   within 10 units of each other. Any slope over about 14 degrees, a corridor or a
   bend failed. A failure was silent and retried two seconds later.
3. Companions in another cell or an unloaded exterior were never considered.

Nether's Follower Framework does the opposite. It checks on its own timer whatever
the player is doing. It treats "3D not loaded" as far away. Then it calls `MoveTo`
256 or 512 units behind you and never checks the ground.

## Automatic catch-up

Runs every 0.25 s from `Update`, before any travel gate. A companion is recalled after:

- 2 s beyond **Teleport beyond**. Distance counts height twice, so a drop into a
  ravine or a stranding on a ledge qualifies.
- 3 s in another interior or worldspace, or unloaded. The engine's own door
  following gets that long first.

Each companion then has an 8 s cooldown.

It works while you ride, sneak, fight, swim or stand still. It holds when you are in
a scene, seated, down or movement-locked. It also holds everyone in Sovngarde,
Skuldafn, Apocrypha, the Soul Cairn and the Whiterun Underforge. The first four
are NFF's own restricted list (`ignoredLocs`/`ignoredCells`, read from
nwsFollowerFramework.esp). The Soul Cairn was added because only Serana belongs there.

A companion is left alone while waiting (`WaitingForPlayer`; Serana's wait sets it
too), in combat, in a scene or talking, riding, seated, restrained, down, holding
hands, or scouting loot. A Relax order disables catch-up entirely, because it
keeps the camp in place on purpose. Temporary rest raises the threshold to at least
`fMaxRadius + 800`, so roaming does not count as being lost.

## Landing

Tried in order. The first spot that no other actor stands on wins:

1. **behind**: this companion's formation spot behind you, reached by a straight
   line of connected navmesh at any slope.
2. **trail**: a point you stood on 200-1200 units back along your route. It was
   walkable a moment ago and joins you by that route, so it works around corners,
   on stairs and in narrow passages. While you ride, the trail is your horse's footing.
3. **navmesh**: any triangle reachable on foot within 120-700 units, behind you
   first, preferring spots whose route is not much longer than the straight line.
4. **beside**, **footsteps**, **player**: 80 units beside you, then your last few
   steps, then your own position. Collision separates you. This tier never refuses.

The trail is cleared on a new cell or worldspace, a step over 600 units, or a height
change over 120 units, because a fall may not be linked by navmesh.

## Party teleport

Uses the same landing search. It works in combat and on horseback. It still refuses
during scenes, while seated, or when you are down. Companions in combat are brought
too; waiting, scripted, seated and riding ones are not. The notification names them,
for example "Stayed: Skjel the Gray (waiting)".

## Log lines

```
[Catchup] recalled <name> (<id>) from <N units|another cell> via <tier> to (x, y, z)
[Catchup] holding <name> (<id>) <at N units|in another cell>: <reason>
[PartyRecall] Summoned <name> (<id>) via <tier> to (x, y, z)
[PartyRecall] Left <name> (<id>): <reason>
```

`holding` is logged once per change of reason, and only while the companion is far
or in another cell.
