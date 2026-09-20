# Actor routes and victory variety — candidate r5, 19 September 2026

This candidate follows the user's report that the r4 sharp-turn recovery build
was worse. R4 remains withdrawn. Installed files were restored to r3 before this
work; creating this candidate does not install it or establish an in-game fix.

## Evidence and pathing approach

R3 sharp-turn logs did not show WWM catch-up, party recall or recovery teleports
in the reported sequence. R4 logs showed MovementBlocked holds of several seconds,
followed by stationary companions and repeated ordinary package evaluations.
They do not establish the cause of the observed hovering, or prove AI reset alone
was responsible. The r4 hold/retry experiment is not included in this candidate.

The prior corridor fitting was primarily anchored on the player. A formation
point can be reachable from the player while requiring a different route around
a corner from a companion. R5 adds a bounded connected-triangle search anchored
on the companion, reconstructs its route, and selects a body-clear waypoint
before the obstruction. It holds that waypoint briefly, rather than repeatedly
moving the goal across a bend. Valid formation roles stay fixed indoors and
during heading changes or active waypoint holds.

Searches run at most twice a second per companion, visit at most 256 triangles,
use a 1,800-unit radius, 512-unit height limit and 4,000-unit route budget. Visible
waypoints are within 500 units. Up to eight candidate points share a 144-ray
clearance budget; the initial direct-route probe has its own 144-ray cap. A
selected waypoint is held for up to one second, or until close enough to advance.
World rays ignore actors so companion bodies do not become permanent barriers.
Native AI remains responsible for actor avoidance and actual locomotion.

No verified waypoint means retaining the native goal. No new navmesh connection
is fabricated, no actor is repositioned by this path, and disconnected gates or
overwritten navmesh still require the relevant compatibility patch. The samples
are not a full capsule sweep. Small corners, large triangles, actor scale and
performance with a large party still require in-game checks.

## Reactions

The three-enemy minimum is unchanged. Nearby eligible teammates now all receive
an assigned reaction, irrespective of recorded participation. Assignment rotates
among actors and distributes shoulder recovery, applause, brow wiping and hand
inspection. Armed reactions can precede the assigned sheathed recovery, instead
of shoulder selection suppressing the armed phase.

The shoulder selection survives asynchronous startup rather than being cleared
on the next frame. A diagnostic records whether OAR evaluated it. The filesystem
layout is supported by OAR's recursive directory loader and replacement-path
mapping; no layout change or regeneration is needed for this clip.

Local Skyrim.esm confirms IdleWipeBrow (000977EC), IdleExamine (00075C3D),
IdleCivilWarCheer (000F7C8C) and CombatIdleRegular (000B8C71). WipeBrow and the
cheer are root IDLE records without conditions. Examine requires no shield or
torch out. CombatIdleRegular belongs to an equipment-conditioned native branch;
it is a best-effort armed pause, not a guarantee for every weapon graph. If the
engine rejects it, recovery after sheathing remains queued. No vanilla HKX files
are redistributed. The decoded clips and record observations are retained locally
under build/victory-variety-clips.json and build/victory-variety-records.json.

OAR implementation reference inspected locally:
https://github.com/ersh1/OpenAnimationReplacer (Parsing.cpp, CacheDirectoriesInternal,
StripReplacerPath; ReplacementAnimation.cpp, GetOriginalPath).

## Validation and test order

Release build and native regression results are recorded alongside the candidate
archives. Tests exercise route reconstruction through bends, disconnected links,
bounded malformed routes, encounter thresholds, reaction distribution, equipment
selection, startup grace and transient OAR state. These tests do not run Skyrim,
Havok, NFF or this profile's behavior graph.

1. In Lost Prospect Mine, test the thin passage both ways with Natural and Lead,
   then Companion and Rear. Pause before a sharp turn and walk through it. Check
   for stops, role swapping, hovering and repeated reset behavior.
2. With movement tracing enabled, compare RouteCorner waypoints, CorridorPosition
   targets and TravelRetry entries. Check an exterior sharp turn, stairs and a gate.
3. Win a three-enemy fight, stand nearby, keep weapons drawn briefly, then allow
   sheathing. Check every eligible actor and compare the Victory logs to what
   actually plays. Repeat to see assignment rotate.
4. Test interruptions, incompatible rigs, bows/magic and a shield. Test two enemies
   separately to confirm the minimum was not lowered.

The main, Command Gestures and source ZIPs remain separate. Existing local clasp
and first-person animation files are not part of the public archives and must be
preserved when installing over the local mod folders.
