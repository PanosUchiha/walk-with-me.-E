# TETHER revision 17 — connection timing and palm contact

The user confirmed r16 connects and walks, but takes too long and joins the
outside of the wrists. The captured session at
`build/follower-captures/20260917-011435-293/Walk With Me.log` shows one successful
attempt spending about 1.4 seconds awaiting alias cleanup and another 1.5 seconds
on positioning and physics callbacks. Held frames then continue through walking.
An ordinary interrupted setup also imposes an eight-second retry cooldown.

## Changes

- Native code requests priority cleanup for the selected slot once per ticket.
  The quest script services it at a boundary in its existing serialized alias
  sweep. The request itself never mutates aliases. Older slot work must return
  before cleanup is acknowledged, preventing a stale ClearKeepOffset call from
  undoing a new grip. Native checks still require both an empty alias and release
  of WWM's package. The usual sweep remains the fallback.
- Aligned contact must still persist for 0.25 seconds after offset movement
  begins. The separate preceding 0.25-second offset timer is removed.
- Normal release cooldown is three seconds. Actual setup failures keep eight
  seconds and the existing leave-contact-area/toggle rearming requirement.
- Both fixed constraint pivots move from local `(0,0,2)` to `(0,0,5)`, matching
  TETHER's upstream default. The installed XP32 hand skeleton places the major
  finger bases at local Z approximately 6.8–7.2, consistent with the direction
  from wrist into palm. No animated world-space calibration is used.

The r16 approach, arrival-radius restoration, native pace, slight setback,
dialogue release/grace, combat release and ordered physics cleanup are retained.
DLL and WayfarerQuestScript.pex must be installed together while Skyrim is closed.
Settings, the selected companion and ESP are preserved; the installer backs up
the complete previous installed build and verifies payload hashes.

## Limits and validation

A single ball-and-socket joint controls position, not wrist rotation or finger
curl. Moving its attachment into the palm is a contact correction, not proof of
a natural gripping pose. It can still show back-of-hand contact or twisting.
A reliable authored grip pose or a separately validated angular constraint
would be needed if that remains visible. No such rotation constraint is claimed.

Both native configurations must pass all 19 CTest suites before staging. The
modified Papyrus script must compile with no errors or warnings. These checks
cover native eligibility/ownership/cleanup regressions and script compilation;
they do not simulate Papyrus scheduling or prove in-game hand alignment/latency.

## In-game check

1. Restart Skyrim, use third-person Companion mode with sheathed weapons, and
   pause on clear ground. Let the selected companion approach and connect.
2. Compare acquisition timing. Logs should show `Revision 17`, a priority alias
   request, package acknowledgement and the sequential physics callbacks.
3. Inspect both hands from front and rear while stationary, walking and turning.
   Check whether palm contact improves and note any remaining wrist twist.
4. Open dialogue while held, close it, and reconnect. Repeat with interrupted
   setup, combat and disabling handholding. Normal following must resume.

Revision 17 has not been validated in the running game at packaging time.
