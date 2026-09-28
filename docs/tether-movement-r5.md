# TETHER movement r5

The user reported intermittent freezing and a companion who kept facing one
direction while holding hands. The held branch corrected facing only during
acquisition, although upstream TETHER also corrects yaw every held frame.
It also treated the first successful `KeepOffsetFromActor` call as permanent;
the companion's later AI package changes never renewed that instruction.

## Changes

- While connected, follow the player's yaw along the shortest turn, limited to
  6 radians per second and 0.3 radians per update. Preserve pitch and roll.
  Actor, scene, dialogue, skeleton, travel and separation checks run first.
  Acquisition still requires a fresh measured pose before adding physics.
- Capture the active package when offset movement is dispatched. Once its
  callback finishes, renew if the package changed, including a transition from
  no package to a valid package. Allow at least 0.5 seconds of settling between
  completed requests. A package change during a pending call is not lost.
- After offset activation, measure actual XY displacement each frame. When the
  player moves faster than 15 units/second, the follower moves slower than 5,
  and the follower is over 20 units from the offset target for 0.6 seconds,
  renew the command once. Give the retry settling time, then release after
  another 1.5 seconds of continuous stalling. Stops, normal movement and pauses
  reset the stall timer. Existing separation limits may release sooner.
- Renew through the existing serialized operation sequence. Keep ownership of
  the old offset across failed, deferred and cancelled renewals, so teardown
  still clears it. No renewal overlaps another physics or movement operation.
- Held logs now include heading error, measured player/follower movement,
  offset target error, package ID and renewal state.

This restores the upstream held-facing behavior with a turn limit. Earlier
responsive-r1 documentation's restriction on direct rotation after physics
creation applies to acquisition; connected holds now have their own yaw update.

The physics grip still precedes the serialized alias handoff. This change does
not remove the initial VM delay before offset movement activates. In the
September 26 Inigo capture, session `304037005` waited 1.365 seconds after grip.
The capture lacks held heading and follower displacement, so it cannot establish
whether a later freeze was caused by a lost offset. Session `304037004` also
stopped outside acquisition range; that is separate from held movement.

## Validation and local installation

Regression tests cover wrapped and bounded turns, package changes while an
offset call is pending, null packages, ordinary stops and motion, stall retry,
and renewal cancellation/failure/defer with complete teardown. Build and test
results are saved under `build/tether-movement-r5`.

Legacy and modern Release builds succeeded, and all 26 CTest suites passed in
each. `git diff --check` passed. The first legacy build hit a transient object
file lock; its subsequent complete build succeeded. These checks do not verify
Skyrim's animation or Havok behavior.

Use the legacy `build/manual` DLL for this local Skyrim 1.5.97 installation.
Only the DLL and this note need updating; scripts, ESP, INI and generated clasp
assets are unchanged. Close Skyrim before replacing its loaded DLL, back up the
installed files and verify replacement hashes. The modern build is checked for
CommonLib compatibility as well.

In-game validation remains required: connect with Inigo, walk and turn both
ways, stop and resume, then release via the toggle or drawn weapon. Repeat on
both sides and confirm ordinary following resumes. Look for `Movement r5` at
startup, small held heading errors during normal turns, and measured follower
movement while walking. Check that package renewals are occasional and do not
repeat while the package and movement remain stable.
