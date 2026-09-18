# TETHER revision 16 — native close approach

Revision 15 did not create a grip in the recorded session. Its passive observer
required a close, stationary, aligned pair, while ordinary formation goals kept
the selected companion far away. The installed travel package also has a 65-unit
destination radius. Merely targeting a handholding position would still permit
the engine to stop outside the attachment area.

Evidence: `build/follower-captures/20260917-004824-349/Walk With Me.log`.
The latest recorded attempts target Katana; no package handoff or grip is logged.
The installed companion selection is preserved.

## Behavior

1. After the existing lead-in, WWM routes the selected companion to 40 units at
   the player's side and 10 units behind. The side remains fixed for the attempt.
   WWM keeps its travel package until arrival. The approach continues when the
   player stops, so manual side-by-side staging is no longer required.
2. The selected slot temporarily borrows a 12-unit package arrival radius and
   a matching planner tolerance. The normal pace choice, speed smoothing/caps,
   obstacle handling, recovery and route-refresh machinery remain in use.
   The old hand-guided speed override, unconditional marker updates and obstacle
   bypass are removed. Other followers use their ordinary goals and tolerances.
3. Pause to connect. Nearby arrival at the chosen side must persist for 0.35 s,
   with player speed at most 15, follower speed at most 130 and at most 28 units
   of error from the side/behind position. Root distance is capped at 90 and
   also respects a lower configured connection distance. Facing and wrist
   proximity are deliberately not prerequisites for beginning positioning.
4. The original arrival radius restores as WWM releases its package. TETHER
   waits for the existing ticketed alias/package cleanup, then starts offset
   positioning. Only in this phase, before either physics body is added, a
   stationary follower may turn toward the player's heading at a bounded rate
   of about 1.5 radians per second. Normal travel is never rotated this way.
5. Physics waits for 0.25 s of aligned contact: root distance at most 80 (or the
   lower configured limit), wrist gap at most 45, heading error at most 0.35
   radians, follower speed at most 20, player speed at most 15, matching side,
   line of sight and compatible height. Each queued setup operation rechecks.
6. The setup window is 12 s and cannot chase beyond 100 units. Resuming player
   travel during setup cancels it. Existing failed-attempt rearming, dialogue
   release, cleanup ordering and safety releases remain. No TETHER speed boost
   or player slowdown is restored.

The temporary radius also restores on navigation failure, combat, dialogue,
selection changes, disabling, mode changes, follower removal and load/reset.
It borrows the existing value rather than assuming every installation uses 65.
No ESP edits, new packages, position teleports or animation assets are included.
Papyrus scripts and the fixed hand pivots remain unchanged from r15.

## Validation and limits

Both native configurations pass 19/19 CTest suites. The TETHER suite now links
the actual travel planner and formation math. Tests check the stopping circle
against admission on both sides, idle goal submission and repathing, unchanged
speed policy, absence of per-tick forced repaths, radius restoration, arrival
versus physics eligibility, cancellation, dialogue grace and cleanup ordering.
The source and compiled Papyrus payload are reused unchanged from verified r15.

This is not an in-game validation. Successful ball-and-socket setup does not
prove a gripping finger pose or correct palm/wrist rotation. Running connection
is not claimed: pause for setup, then walk once connected. Alignment remains a
separate unresolved visual issue; r14's unstable calibration stays removed.

## In-game check

- In third-person Companion mode with weapons sheathed, leave the selected
  companion behind, stop on clear ground and let them approach your side.
- Look for `[HandHolding/Approach]` with radius 12, then restoration of the
  original radius, package cleanup acknowledgement, offset and physics callbacks.
  Heading, wrist gap and follower speed are logged if setup waits.
- After connection, walk, turn, stop and resume. Inspect hand contact from both
  sides. Confirm other followers retain their ordinary travel.
- Open dialogue during approach and while held. Disable handholding, switch
  companion/mode, draw weapons and load a save; ordinary travel must recover.
- If setup fails, step more than 150 units away or toggle handholding to rearm.
  Capture logs rather than interpreting unit-test success as an in-game pass.

Installation preserves INI/ESP and makes a hash-verified backup of r15.
