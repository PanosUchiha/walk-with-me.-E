# TETHER revision 14 — moving approach and palm calibration

This local test build follows the r13 in-game report: handholding connects, but
alignment is off and the follower has difficulty connecting/remaining attached
while the player moves. Desired contact remains hand-to-hand, slightly behind
the player. Dialogue release and the r13 ticketed package handoff remain.

## Changes

- Start the package handoff within 240 units instead of 110. The selected
  follower may catch a moving player without waiting for an exact side-by-side
  position. WWM still handles the distant approach.
- Allow up to 600 units during the unconnected catch-up and 15 seconds for the
  handoff/approach. The physics grip still requires close contact: within both
  the configured connection distance and 85 units, a palm gap no greater than
  70 units, line of sight and no more than 40 units of height difference.
- Apply a 1.5x follower catch-up multiplier as soon as offset movement starts,
  before physics connects. While held, follower speed rises from 1.15x to 1.65x
  as root separation grows from 45 to 100 units. The existing player tension
  slowdown remains. No additional speed hook or permanent actor value is used.
- Target 36 units to the selected side and 24 behind; catch-up radius 45 and
  follow radius 10. Selection can switch to the nearest side before offset
  starts, allowing the player to step directly beside either side of the NPC.
  Once attached, the selected hands stay fixed for that grip.
- A 0.4-second grace tolerates brief separation beyond the configured release
  distance. Another 45 units releases immediately. Skeleton changes, large
  movement discontinuities, dialogue and other safety releases have no grace.
- Replace the fixed (0,0,2) pivots with independent palm measurements from each
  actor's wrist and three finger bases. The contact lies 55% toward the average
  finger base and one unit toward the other palm. After both actors' physics
  bodies have been added, each point is converted into its actual hand rigid
  body's local coordinates. Invalid/missing anatomy or excessive local offsets
  cancel acquisition and roll back. The chosen pivots are logged.

This uses the same ball-and-socket grip. A single point constraint does not
control finger curls or the full relative rotation of both hands; those remain
dependent on the installed animation and physics behavior. No new arm animation,
pose hook, teleport or heading lock is introduced.

## Evidence and checks

The r13 capture is build/follower-captures/20260916-231622-842. It shows successful
setup/teardown callbacks and several-second alias-cleanup waits, including
23:12:08.914 to 23:12:12.302. It also records separation/skeleton/travel releases;
those older grouped messages cannot distinguish the exact release cause.
Revision 14 logs separation separately and adds held follower/player speeds.

Read-only inspection of the running SE 1.5.97 executable verified the Papyrus
pivot unit conversion: the constraint helper multiplies all six caller offsets
by 0.014287499710917473 before passing the local pivots to Havok. The implementation
therefore supplies body-local offsets in game units, not bone-local coordinates
or raw Havok units. No game code was modified or bundled. Relevant preferred
addresses: wrapper 0x140971CA0, deferred task 0x140971460, helper 0x140DAEDB0,
conversion 0x140DAF208–0x140DAF26B. Runtime hooks are not added at these addresses.

Automated tests cover translated/rotated palm conversion, malformed anatomy,
unreasonable pivots, moving handoff distance, pre-grip speed, side selection and
soft/hard separation, plus all r13 cancellation, ownership and dialogue tests.
Both DLL variants and the changed Papyrus script must pass before installation.
This is not an in-game validation of revision 14.

Test with the selected companion, third person and sheathed weapons:

1. Walk with the follower behind you and allow them to catch up and connect.
2. Stand directly beside them on each side and allow connection without lining
   the wrists up manually. Check the palms from front, back and above.
3. Walk, turn, stop and resume. Check for arm pulling, twisted hands or leg jitter.
4. Open dialogue while approaching and while held. Confirm separation, then
   reconnect only after dialogue and the existing grace/lead-in.
5. Disable handholding and confirm ordinary following; test weapons, equipment,
   camera changes, interior doors, adjacent exterior cells and loading.

Look for 'palm calibration' followed by operation 2 and the held traces. If
'palm calibration unavailable' occurs, the log identifies a rejected attempt;
do not interpret it as connected. The full install backup allows rollback to r13.
