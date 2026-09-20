# Reactions and confined-path review — 19 September 2026

This describes the earlier candidate. See [actor-routes-victory-r5.md](actor-routes-victory-r5.md)
for the current implementation and the subsequent failed r4 experiment.

## Post-combat findings

The latest local Walk With Me.log registered the shoulder condition and reported
its assets available. At 01:03:12 it queued four companions after seven deaths.
At 01:03:14 it cancelled the entire queue with partyFighting=false and
playerReady=false, before the fifteen-second retry window could help. Pending
actors had reported movement, speech and a non-humanoid animation rig.

Pending reactions now survive temporary player unavailability within that same
deadline. Victories can queue while the player is moving. Started reactions
still stop when interrupted, and new combat cancels all reactions. Three-enemy
minimum, participation, equipment, rig, distance and alias-release requirements
are unchanged. OAR is only needed for the authored shoulder replacement, not
vanilla applause/cheers; Command Gestures is not needed for victory reactions.

## Passage handling, including Natural and Lead

The reported local case is Lost Prospect Mine: companions waited at a narrow
section until the player exited. It affects all formations. Lead additionally
failed to get ahead. The Whiterun outskirts case came from another player's
video, whose load order is unavailable; it is not a verified local JK conflict.

Previously, formation goals were checked against a straight navmesh trace from
the player, without body collision checks. Local obstacle detours ran only in
wilderness. Clipped endpoints could become destinations, leaving actors trying
to hold a formation near walls. The updated path now:

- Checks formation destinations against navmesh and world collision in caves,
  towns and wilderness when Movement/bForwardCollision is enabled.
- Tries full width, half width and centerline, then reduces forward/rear reach.
- Samples supported width every 75 units and rays at 32, 80 and 128 units high,
  at the center and 28 units to either side. Caps a probe batch at 144 rays.
- If the projection is unusable or too short, searches up to 128 connected
  triangles within 500 units, 192 vertical units and 900 route units. Up to
  eight ranked destinations get a local body-clearance check. Lead keeps a
  forward target; other formations search toward their requested slot.
- Leaves actual routing around bends to native AI and suppresses local
  wilderness detours for these connected-route targets. No actor is moved
  through geometry and no missing navmesh link is fabricated.
- Passes the actual hand-guided flag to the local steering gate, so detours
  cannot overwrite a hand-holding approach target.

This uses ray samples, not a full capsule sweep. Intermediate collisions on
curved native routes remain the engine's responsibility. Geometry, skeleton
sizes, dynamic obstacles and performance need in-game observation. Navigation
mods with missing or overwritten links still require their own compatibility
patch; the video alone cannot establish that diagnosis.

## Playtest

The Release DLL compiled successfully and all 19 CTest targets passed, including
new pending-reaction interruption, narrow-gate, bent-passage, rear-slot and
disconnected-navmesh regressions. These tests do not run Skyrim or Havok.

1. Replace the base mod in a test profile. Command-Gestures.zip remains a
   separate optional installation; regenerate if its patch changed.
2. In Lost Prospect Mine, traverse the thin passage in both directions with
   Natural, Lead, Companion and Rear. Pause before the bend, resume, then try
   multiple companions. Check that goals remain in the passage and that Lead
   can reach a connected point ahead when the mesh provides one.
3. Repeat at a straight narrow doorway, stairs, an exterior gate and around a
   wilderness rock. Check frame time with the full party and confirm hand
   holding still owns its approach target.
4. Defeat at least three enemies with participating companions. Briefly run
   while they finish speaking/sheathing, then stop within fifteen seconds.
   Confirm pending reactions resume. Restart combat and confirm cancellation.
5. Read [Corridor], [Obstacle] and [Victory] entries in Walk With Me.log.
   Debug/bTraceMovement enables corridor diagnostics. Blocker FormIDs help
   identify physical references; navRejects records failed navmesh probes.

These are development test packages, retaining version 0.2.0. They are not
in-game-validated release builds. The installed game profile is not changed.
