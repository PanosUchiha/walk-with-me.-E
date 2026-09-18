# TETHER handholding revision 13 — local test build

Historical r13 behavior. Current changes and checks: [revision 14](tether-r14-handoff.md).

This replaces WWM's previous procedural handholding with a TETHER-derived Havok
ball-and-socket grip and KeepOffsetFromActor movement. TETHER by KEI997 is MIT;
full provenance is in ThirdParty/TETHER-NOTICE.txt. The standalone TETHER DLL is
not required. Do not activate another tether on the same actors simultaneously.

## Movement and dialogue

1. WWM approaches the selected companion's side using its ordinary travel package
   and arrival radius. Other party members keep normal formation behavior.
2. Within 110 units, with line of sight and at most 40 units of height difference,
   the selected actor is withheld from WWM's movement alias. A new session ticket
   must be acknowledged at the end of that slot's Papyrus cleanup, after its
   ClearKeepOffsetFromActor and EvaluatePackage calls. The alias must be empty
   and the current package must no longer belong to WWM.
3. Offset movement targets 40 units to the chosen side and 10 behind, with a
   follow radius of 10 and catch-up radius of 60. After at least 0.25 seconds,
   root distance must be within both the configured connection limit and 85
   units; wrist distance must be at most 70, with line of sight and a safe height
   difference. These are initial test thresholds, not measured visual guarantees.
   The old exact lateral/heading and 28-unit wrist gates are removed.
4. Sequential checked calls add each actor's ragdoll bodies and then connect the
   hands. Only a true constraint result enables the held speed adjustments.
   Player speed scales from 1.0 to 0.55 with separation; partner speed is 1.3x.
5. Dialogue or its opening/closing transition cancels acquisition or holding.
   The constraint and borrowed physics are removed, then the offset is cleared.
   WWM keeps that actor out of its movement alias until cleanup completes. The
   existing 1.5-second dialogue grace and configured handholding delay govern
   reconnection. No AI alias updates are forced during dialogue.

Acquisition times out after 10 seconds and retries only after cleanup and a
three-second cooldown. An outstanding callback blocks further mutations; a
five-second callback timeout requests cancellation but still waits for that
callback. Failed teardown retains ownership and retries. Old save/session
tickets are invalidated on preload and postload. Replacement skeletons never
receive old physics teardown. There are no actor-position writes, collision
overrides, heading locks or procedural arm hooks in this backend.

Existing combat, equipment, camera, scene, rest, selection, cell-transition and
distance release conditions remain. Ordinary following retains the rollback
baseline and adjacent-exterior-cell fix. Victory r4 and companion controls remain.
The ESP and its packages are unchanged. Install the complete DLL and matching
Wayfarer, WayfarerQuestScript and WayfarerTether scripts together.

## Validation

Both modern and legacy DLLs build and pass 19 CTest suites. The new suite covers
handoff acknowledgements, stale/reassigned tickets, cancellation at each setup
operation, late and duplicate callbacks, partial failure, teardown retry,
dialogue grace, reachable proximity thresholds and speed bounds. All three
updated Papyrus scripts compile with zero errors and warnings. The package
verifier checks the unchanged ESP and matching script/native bridge.

These checks do not prove pathfinding, Havok stability, arm appearance or dialogue
timing in the running game. No TETHER animation pack is bundled, so this adapts
its physics/movement rather than reproducing its authored motions. Vanilla or
installed locomotion animations remain. The physics APIs do not expose complete
ownership information for other mods' ragdoll operations.

## First in-game check

Use Companion mode, third person, sheathed weapons and the selected companion.
Enable experimental handholding in Travel; existing preferences are preserved.
Temporarily disable automatic relaxation if it interrupts the stationary check.
Stand near the companion, then walk, stop, turn and jog. Check both sides.

Start dialogue during approach, during connection and while held. The hands
should separate and remain free through dialogue. Close dialogue and verify
reconnection after the grace/delay. Disable handholding and verify normal
following. Also test equipment changes, weapons, combat, first person, interior
doors, adjacent exterior cells, loading a save and removing the companion.

Expected log sequence in Walk With Me.log:

- Revision 13 startup; approach and package-handoff request.
- WWM alias cleanup acknowledged.
- Operation 6 (offset), then 0, 1, 2 (physics setup), then Physics grip connected.
- On release: operations 3, 4, 5, 7, then cleanup finished.

Once-per-second gate traces report root/wrist distance, height, line of sight
and current package. A timeout without a connected message is a failed attempt,
not a successful grip. Partial setup cleans only resources actually acquired.
