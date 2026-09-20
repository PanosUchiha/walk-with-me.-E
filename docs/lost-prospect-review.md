# Lost Prospect Mine follow-up — 2026-09-19

Evidence snapshot: `build/lost-prospect-review-20260919/Walk With Me.log`.
The session loaded the September 19 build at 16:04:05. OAR V3 and both custom
condition registrations succeeded. These are movement stalls, not evidence of
an animation registration failure.

At 16:17:04–16:17:07 M'rissi was motionless with WWM's travel package reported
as owned and a destination 567–640 units away. Stalled time reached 4 seconds;
at 16:17:07.665 recovery yielded, with a runtime package active at that instant.
At 16:17:11–16:17:16 Rumarin repeatedly failed all five corridor attempts with
zero collision rays and five navmesh rejections. Connected destinations were
found later. NFF packages appear after releases; this does not prove that NFF
caused the stalls. Existing traces do not identify the exact physical obstacle
or distinguish centerline failures from shoulder-support failures.

Candidate changes:

- Keep the connected navmesh centerline and full-width Havok sweeps at three
  heights. Remove the additional requirement that both shoulder-offset paths
  also lie on navmesh. This reduces over-rejection where physical clearance is
  wider than the navigable center strip. It is a plausible contributor, not a
  confirmed diagnosis of the specific mine geometry.
- Retract forward prediction quickly while movement turns away from the
  smoothed formation heading, then restore it gradually as heading catches up.
  Existing turn filtering, formation layout, and stop behavior remain.
- Retry unchanged stalled destinations at one-second intervals before the
  existing recovery timeout. This closes a planner gap; the recorded stall
  also includes recent route submissions, so this alone is not a complete fix.
- Trace actor/player/target positions, cell ID, connected destination use, and
  stalled route retries to distinguish geometry, target changes, and ownership.

Validation: Release DLL built; all 19 configured CTest targets pass, including
new sharp-turn retraction/restoration, static-goal retry, retry-rate, continuing
progress, and eventual recovery cases. Havok and the actual mine geometry are
not exercised by the standalone tests. Retest the narrow mine passage in Lead
and Natural, then right-angle turns in Whiterun, with movement tracing enabled.
No verified claim that the in-game sticking is resolved yet.
