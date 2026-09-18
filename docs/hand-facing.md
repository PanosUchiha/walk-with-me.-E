# Attached facing: revision 11.3

The September 13 log from 0.16.7 confirms that revision 11.2 preserves physics
velocity. It still records native turn requests reaching 180 degrees, while the
final heading error is zero. A final correction cannot show whether another
heading was visible earlier in the frame.

On Skyrim 1.5.97, the movement function at RVA `0x5D87F0` adds its accumulated turn
to the actor's heading and calls `Actor::SetHeading` at `0x5D91B7`. The call reaches
the reference rotation setter and writes the actor's yaw before our outer update
corrects it. This provides a concrete path for the reported facing conflict;
whether it accounts for every visible flicker still requires a game test.

Revision 11.3 filters that movement call for the attached follower. It passes the
pair's current yaw through the existing call target, preserving any earlier mod
hook. The actor and yaw are published together without taking the pose mutex.
Release clears the override; other actors and the player retain native rotation.
The follower also aligns immediately on contact and before its native update.
Graph overrides retain ownership between applications instead of briefly
restoring the old native direction and turn each time.

The call uses Address Library IDs 36365/37356 and SE/AE offsets 0x9C7/0xA87,
cross-referenced against TrueDirectionalMovement's `Actor_SetRotationHook`.
An unexpected call opcode leaves the hook uninstalled and logs an error.
Revision 11's attachment, wrist targets, fingers and pose solver are unchanged;
revision 11.2's velocity-preserving scene updates remain.

## Verification

- Modern build: 16/16 tests passed. Legacy build: 17/17 tests passed.
- The C++ facing replay observes 11,232 conflicting native writes between update
  stages at 30, 60 and 144 FPS, including stops, reversals, both sides, another
  actor, partner replacement and release.
- An isolated CPU replay executes the actual 1.5.97 rotation call and setters.
  All 16 unfiltered cases expose a different intermediate yaw; all 16 filtered
  cases retain the shared yaw and all 16 released cases regain native turning.
  The virtual angle getter and dirty marking are stubbed, and the argument filter
  models the hook. This is not execution of the plugin inside Skyrim.
- The previous physics replay still passes all 24 cases, preserving velocity
  through 144 scene refreshes.
- Runtime evidence and build/test logs are in
  `build/handholding-direction-review`. Only 1.5.97 was replayed; compatibility
  compilation does not prove animation behavior on another runtime.

## Game check

Attach while the follower is approaching at an angle, then walk straight, stop,
reverse direction, turn a corner and release. Repeat from the other side. Check
the body facing and leg direction separately, along with retained hand placement.

Startup should report `Native heading guard installed` and `Revision 11.3`.
Each sticky trace now includes `nativeHeadingWrites` and
`maxNativeHeadingErrorDegrees`, recording native attempts intercepted during the
interval. The latter measures the rejected request, not the final applied error.
A zero count throughout moving attachment means this hook was not exercised and
needs investigation. Visual success must be confirmed in game.
