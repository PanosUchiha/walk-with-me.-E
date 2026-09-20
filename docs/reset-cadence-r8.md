# Reset cadence candidate r8

Five independent reviewers and three judges assessed the r7 exterior freeze
capture. All judges chose a bounded corrective-reset interval as the first
isolated experiment. The log shows resets roughly one third of a second apart
after movement ownership returns. The planner's nominal one-second stalled retry
spacing was bypassable through later pace/geometry branches and the controller's
obstacle-change override. This establishes the reset bursts, not the cause of
every initial MovementBlocked event.

## Policy

After acquired movement enters MovementBlocked or accumulates one second of the
existing stall timer, latch a reset episode. All ordinary corrective predicates
pass through one decision and require at least one second since the previous
route submission during that episode. Initial submissions remain eligible.
An obsolete route is still released by existing context/eligibility and missing
corridor handling; the guard does not prevent those releases.

A brief ownership return or single movement sample cannot clear the episode.
Clear it after at least one second of continuous acquired movement above the
existing motion threshold, or arrival within the normal radius, with stall below
one second and at least one second since the last reset. Alias loss, other package
ownership and release invalidate the episode. Fresh requests start without it.
No episode is serialized into saves.

This is reset spacing, not an additional pause in locomotion. Marker updates
continue, and a persistent blocked actor still gets bounded retries and existing
stall recovery. The one-second bound is an experimental choice based on the
existing retry intent, not a documented Skyrim requirement.

The older r4 build removed resets and broadly held blocked markers; it regressed.
r8 retains resets and marker updates. It changes no waypoint cache lifetime,
navmesh/collision search, speed scaling, release distance, gesture assets or
post-combat animation selection. The three-enemy minimum remains.

## Diagnostics

`[TravelReset]` records actor, slot, request number, action (applied/deferred),
reason, route age, episode state, progress time and current control phase.
An applied entry means WWM called EvaluatePackage with resetAI=true, not that
Skyrim necessarily accepted or completed a path. Reasons describe the first
qualifying predicate; concurrent secondary predicates are not enumerated.
Deferred entries do not move the reset clock. The existing TravelRetry and
TravelHandoff logs remain. Movement tracing can produce more log lines.

## Validation

Tests replay blocked retry followed by reacquisition and requests at approximately
0.34 and 0.66 seconds. They cover pace, lateral, heading, braking, settled-goal and
obstacle bypasses; continued marker commits; an eligible retry after one second;
return to ordinary cadence after sustained movement; context release and alias
loss; and persistent MovementBlocked reaching normal recovery.

Compare r7 and r8 on the same Whiterun stretch at matched walking/running speeds,
party and formation, with hand-holding enabled. Then repeat Lost Prospect Mine.
Measure stopped time, re-blocking, actual traversal, lag and recalls. Fewer reset
messages alone are not success. A recall is failed traversal.

If burst suppression leaves freezes unchanged, test the local/direct route
boundary separately. If it delays useful recovery or worsens following, withdraw
this candidate instead of extending the guard. Sustained confinement detection
remains separate future work: this capture also contains substantial genuine
movement while companions fall behind, so that movement must not be mislabeled
as shuffling.

Main, Command Gestures and complete Source ZIPs remain separate. Updating the
validated r7 installation does not require animation behavior regeneration.
