# TETHER revision 15 — restore ordinary approach

Revision 14 regressed in the user's game: worse hand alignment and erratic
following. This revision removes its aggressive acquisition and calibration.
It is a conservative correction, not a claim of visually correct handholding.

## Evidence

The r14 capture is `build/follower-captures/20260916-234239-061`.
It records handoff requests at roughly 236–239 units, package cleanup delays
of several seconds, and multiple cancelled attempts. A 1.5x acquisition boost
and 1.15–1.65x held boost were applied on top of offset movement. The ordinary
WWM approach also bypassed its usual route and pace smoothing for this actor.

The three logged per-attempt follower pivots are (8.83,1.45,6.01),
(-5.97,6.29,4.07) and (4.35,8.27,-0.02). Those changing offsets and the user's
screenshot invalidate the claim that this calculation solved alignment.
Animation and rigid-body poses sampled during movement may disagree; the
precise source of that discrepancy is not established by these logs.

## Changes

- TETHER no longer supplies an approach target to the formation controller.
  Normal formation routes, pace smoothing, obstacle handling and idle behavior
  remain active until a close, settled pair qualifies for handoff.
- Acquisition requires 0.75 seconds of continuous adjacent contact: root distance
  at most 80 units (also respecting a lower configured limit), wrist gap at most
  45, lateral separation 20–60, longitudinal position -45 to +20, heading error
  at most 0.5 radians, player speed at most 8 and follower speed at most 12.
  Either side works. Hand selection locks when the handoff starts.
- Moving catch-up never starts a handoff. Pause beside the companion, facing
  the same direction, and wait for connection before walking. Resuming travel
  during setup cancels it. Setup is capped at eight seconds and 85 units.
- Each queued setup operation rechecks proximity, sight, heading and player
  speed. Cancellation and cleanup still use the ticketed sequential protocol.
- A failed operation or timeout blocks repeated attempts until the pair move
  more than 150 units apart, or the setting/selection changes. Releases have an
  eight-second cooldown. Dialogue release and its existing grace remain.
- Remove TETHER's additional speed multipliers for both actors. Normal WWM
  movement retains its existing speed behavior. Held movement uses the engine's
  offset following without an additional catch-up or tension speed controller.
- Restore r13's fixed (0,0,2) constraint pivots and offset movement parameters:
  40 units to the side, 10 behind, catch-up radius 60 and follow radius 10.
  Runtime r14 palm calibration is removed. Its unused math helper remains in
  the source history and is not used by the running plugin.

This retains a single ball-and-socket constraint. It does not author a gripping
finger pose or control wrist rotation. Restoring the earlier pivots is a rollback
of worse calibration, not a demonstrated alignment fix. Held locomotion and
hand contact still require in-game validation. Running may separate the pair;
the correction deliberately does not accelerate the follower to compensate.

## Validation

Both modern and legacy native builds pass 19/19 CTest suites. Updated regression
cases exercise a moving arrival trace, idle-player/moving-follower rejection,
wrong-facing and directly-behind rejection, uninterrupted settling, failed
attempt rearming, either-side contact, neutral speed, cancellation and cleanup.
All three runtime Papyrus scripts compile without errors or warnings.
These checks do not establish visual quality or engine behavior.

1. First walk, turn, stop and resume normally with handholding enabled. Before
   standing beside the companion, the log should say ordinary travel is retained
   and should not request a package handoff.
2. Pause beside the companion facing the same way. Let setup finish, then walk.
   Inspect hands from both sides. Compare contact with r13, not only r14.
3. Open dialogue while held and while setup is pending. Normal following should
   return after cleanup. Check that other followers retain their normal travel.
4. Move away during setup; verify cancellation. After a failed setup, separate
   by over 150 units or toggle handholding before trying again.
5. Test weapons, sprinting, camera changes, equipment, doors and save/load.

Installation preserves the current INI, ESP, companion selection and other
features. A hash-verified backup of the installed r14 payload precedes replacement.
