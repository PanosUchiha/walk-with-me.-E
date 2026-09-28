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

It works while you ride, sneak, fight, swim, sit, craft or stand still, and with
movement locked. It holds only while you are in a scene, down, dead or in a kill
move, flying (a dragon), or in the air. It also holds everyone in Sovngarde,
Skuldafn, Apocrypha, the Soul Cairn and the Whiterun Underforge. The first four
are NFF's own restricted list (`ignoredLocs`/`ignoredCells`, read from
nwsFollowerFramework.esp). The Soul Cairn was added because only Serana belongs there.

A companion is left alone while waiting (`WaitingForPlayer`; Serana's wait sets it
too), in combat, in a scene or talking, riding, restrained, down, holding hands,
scouting loot, or running NFF's loot package. A seated or sleeping companion is
*not* left behind any more: `MoveTo` stands them up, as it does for NFF. Temporary
rest raises the threshold to at least `fMaxRadius + 800`, so roaming does not count
as being lost.

### Relax camps (2026-09-25)

`[Movement] bHoldRelaxingCompanions` (default on; Travel > **Leave relaxing
companions in place**). During a Relax order, companions keep their camp however far
you go, and NFF is kept from teleporting them too (below). Off: they catch up like
anyone else, with the roaming threshold, and relax wherever they land, because the
teleport clears their rest anchor. Temporary rest never holds anyone.

### Taking over NFF's catch-up (2026-09-25)

NFF's warp (`nwsFollowerSandboxScript.WarpCheckPlayer`) runs every `nwsWarpStart`
seconds (default 8) and `MoveTo`s anyone beyond `nwsWarpRange` (2560) or unloaded to
512 units behind the player, with no ground check. It knows nothing of a WWM Relax
camp: its only relax check is its own `nwsGroupRelax`. So it used to empty WWM camps,
and in ordinary travel it raced WWM's catch-up.

`[Compatibility] bReplaceNFFCatchup` (default on; Travel > **Take over NFF's
catch-up**). While NFF's Catch Up is on and WWM is directing the party (enabled, not
Vanilla) and either teleporting or holding a Relax camp, WWM:

- parks NFF's `nwsWarpStart` global (`0x068AE2`) at 1e9 seconds, so NFF's timer never
  fires. `nwsAllowWarp` is left alone on purpose: switching it off also stops NFF's
  update loop from re-registering when sandboxing and dynamic speed are off.
- teleports every follower in NFF's active slots itself, WWM companion or not, with
  NFF's own exceptions (waiting, combat, riding, scenes, its loot packages) and a
  threshold of `nwsSandboxRange + 1024` while NFF's own sandbox package is running.
  These log as `recalled ... for NFF`.

The player's own interval is written back before every save (SKSE `kSaveGame` is sent
before the game writes it), while the journal (NFF's MCM) is open, and whenever the
takeover ends. A save therefore never holds the parked value, and uninstalling WWM
leaves NFF exactly as configured. A change made in NFF's menu is adopted the next
time the timer is parked. Pure logic: `FrameworkWarpPark`, `OwnsFrameworkCatchup`.

### NFF's countdown (2026-09-26)

Reported: companions relaxing at a camp appeared beside you in the next room
or area, then walked back out to the camp. The logs from 09-26 01:01 and 13:01
show it. At a load door, the door autosave handed NFF its interval back. About
three seconds later Walk With Me parked it again. A second after that, all four
campers were in the new cell, 64-70k units from their anchors. Within five
seconds, most had walked back through the door.

The cause is how NFF counts. `WarpCheckPlayer` adds one to a script variable,
`warpCDTime`, every one-second tick, and warps once the count reaches
`nwsWarpStart`. Parking the interval stops the warp, not the count. After a
few minutes parked the count is in the hundreds, so any hand-back (a save, the
journal, the takeover ending) fired a warp on NFF's next tick. The load screen
left that tick time to land before the re-park.

Now, whenever Walk With Me writes the player's interval back, it also sets
`warpCDTime` to 0 on NFF's `nwsFollowerSandbox` quest (local 0x179283). NFF then
needs a full interval, 8 s by default, before it can warp. Walk With Me re-parks
within a few frames. Saves therefore hold count 0 beside the real interval. After
a load, with the takeover allowed, the count is also restarted once, which covers
saves written by earlier builds. If the variable cannot be found (a different NFF
version), the restore line ends in "(its countdown could not be restarted)".

This does not change when the takeover applies. With **Leave relaxing companions in
place** off and Walk With Me's teleport off, NFF teleports relaxing companions as
before.

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

Uses the same landing search. It works in combat, on horseback and while you sit.
It still refuses during scenes or when you are down. Companions in combat or seated
are brought too; waiting, scripted and riding ones are not. The notification names them,
for example "Stayed: Skjel the Gray (waiting)".

## Log lines

```
[Catchup] recalled <name> (<id>) from <N units|another cell> via <tier> to (x, y, z)[ for NFF]
[Catchup] holding <name> (<id>) <at N units|in another cell>: <reason>
[Catchup] Taking over / Returning Nether's Follower Framework catch-up
[Catchup] Parked NFF's catch-up timer (the player's value is N s)
[Catchup] Restored NFF's catch-up timer to N s [for the save|while the journal is open]
[Catchup] Restarted NFF's catch-up countdown after loading
[PartyRecall] Summoned <name> (<id>) via <tier> to (x, y, z)
[PartyRecall] Left <name> (<id>): <reason>
```

`holding` is logged once per change of reason, and only while the companion is far
or in another cell.
