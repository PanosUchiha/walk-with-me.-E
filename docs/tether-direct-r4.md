# TETHER direct grip r4

Rumarin's September 22 22:22 attempts reached the player, then waited for the
WWM alias/package cleanup before any hand physics could begin. Session
1016096348 took 0.737 seconds for that cleanup and 1.413 seconds from arrival to
grip. Session 1016096351 took 2.722 seconds for cleanup and 3.297 seconds from
arrival to grip (5.303 seconds including approach). Several intervening attempts
were interrupted by another arm animation while waiting and approached again.
The captured log is `build/tether-direct-r4/Rumarin-before.log`.

## Change

- At close arrival, retain WWM's package and replace its destination with the
  follower's arrival position. Commit/repath that stop immediately once, even
  inside the ordinary marker refresh threshold. Keep it throughout setup.
- Select the clasp pose and correct facing before physics exists. Check a fresh
  frame, stationary player, settled follower, line of sight, height, skeletons,
  side and wrist separation before creating the grip.
- Create player body, follower body and constraint before requesting movement
  handoff. The existing serialized script acknowledgement still gates offset
  movement, so an older ClearKeepOffset call cannot erase the new offset.
- Remove the 0.2-second arrival dwell and 1.5-second gap-widening timer. The
  default wrist limit is now 45, the previous maximum. Explicit smaller values
  remain supported. The local installation sets Rumarin's value from 20 to 45.
- After connection, pump movement setup even if the player starts walking. Do
  not reapply the stationary acquisition gate to an already connected pair.
  An eight-second handoff timeout still releases if cleanup cannot finish.
- Cancellation drains only resources actually acquired, including a constraint
  created before any offset. Failed removals retain ownership for retry.
- Logs distinguish time until grip from cleanup time after grip, and show when
  the arrival position is held and whether offset movement is active.

Classic remains the default backend. Existing explicit TETHER selection and
other personal settings remain intact. No change to scripts, ESP, clasp clips
or behavior generation is required on an installation with the r2 scripts.

## Validation

Regression coverage exercises the reordered setup, cancellation and failed
operations at each stage, deferred operations that retain a live constraint,
grip setup without handoff acknowledgement, release between connection and
offset, first-frame arrival, and an immediate arrival stop that does not
repeatedly restart pathfinding. Build/test and installation results are recorded
with the local artifact under `build/tether-direct-r4`. Modern and legacy Release
builds succeeded, with all 21 CTest suites passing in each. The installed r2
script hashes match the package; no script recompilation is needed. Existing
integer-to-float compiler warnings in `party_life.h` remain.

This removes measured waits from the connection path; it does not claim zero
engine latency or a verified in-game animation result. The three physics calls
still use Skyrim's VM. Check the next Rumarin attempt on each side, walking after
connection, release during setup, dialogue, and changing back to Classic.
