# Control handoff candidate r7

The r6 playtest recorded stalls across all four companions. Hand-holding changes
one actor's target; the evidence does not establish it as the party-wide cause.
Keep hand-holding enabled for this candidate's route test.

## Correction

Previously, TravelGoalState ignored package ownership and counted any stationary
requested actor as stalled. After 4.5 seconds it withdrew the request, which the
serialized Papyrus alias loop could still be processing. The logs are consistent
with that loop; they do not measure how much of each delay it caused.

r7 observes the live slot alias and package on each travel tick. Pending alias
assignment keeps its request alive, records its wait and wakes the existing
serialized script at most once every five seconds per actor. It does not invent
a completion if the script cannot run. After alias assignment, failure to acquire
the package has a separate 15-second timeout; existing one-second package retry
spacing remains. This is a tuning choice requiring in-game validation.

Route-stall time starts only with the matching alias and WWM package. A subsequent
engine MovementBlocked package remains eligible for recovery. Losing the alias
or entering another ordinary package invalidates that ownership evidence. This
avoids treating unrelated stationary AI as a blocked WWM route.

Recovery captures a fence in the existing serialized alias-sync counter: one new
pass if idle, two if a pass was already running. The actor must also be absent
from the live alias. A momentary empty alias in the older pass cannot release the
fence. Request numbers identify logs; there are no new asynchronous script
acknowledgements, script variables or save migrations. Load clears managed states.
Existing scene, combat and tether eligibility checks remain authoritative.

## Diagnostics and validation

`[TravelHandoff]` includes actor, slot, request number and transitions through
assignment-requested, alias-assigned, package-owned and movement-blocked. Recovery
states distinguish route-stall from acquisition-timeout, cleanup-requested,
cleanup-waiting and cleanup-observed. These are sampled native observations,
not exact timestamps inside Papyrus. Existing corridor and obstruction logs
remain necessary to investigate geometry failures independently.

Native tests cover an unassigned stationary request surviving 60 seconds and
moving targets, fresh acquisition time after assignment, blocked movement after
ownership, ownership invalidation, separate acquisition timeout and stale sync
counts during cleanup. Existing hand-holding, travel and victory suites also run.

Repeat the same failing bend from the same save, formation and direction, with
the same four companions, hand-holding and follower framework enabled. Repeat in
both directions and after a reload. Verify timely ownership and traversal without
automatic recall or repeated recovery cycles; check an open-field start too.
Successful teleportation is not successful traversal.

This candidate deliberately addresses the shared handoff policy first. The r6
geometry search bounds, configured 2500-unit release boundary and occasional
missing corridors still need evidence from another route test. The three-enemy
victory threshold and r5 animation selection remain. Main, Command Gestures and
complete Source ZIPs are separate. No animation graph regeneration is needed
when updating the already validated r6 installation.
