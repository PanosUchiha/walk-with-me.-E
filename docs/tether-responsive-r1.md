# TETHER responsive facing — candidate over 0.2.1

This is a narrow candidate on commit d1a0530. UI and the previously shelved
Classic/TETHER selection, hotkeys and random activation work remain on hold.

The accepted baseline rotated a nearby follower incrementally after starting
KeepOffsetFromActor. Its turn rate was capped at roughly 86 degrees per second.
The recorded session at 18:02:09–18:02:11 on September 20 still waited on
`follower turn` after offset setup. The discarded UI-era candidate replaced the
increment with SetHeading and then assumed zero heading error; it did not verify
that the reference/pose had actually reached that heading.

This candidate uses the reference SetAngle operation, preserving pitch and roll,
after the selected follower's ticketed alias/package cleanup and before starting
offset movement. No actor position is changed. A correction invalidates setup
gates for that update; the following update measures facing and hand positions
again. Offset dispatch and its Papyrus last-moment validation now require facing
agreement. Physics setup still waits for stationary, correctly sided actors;
the constraint still requires line of sight, close wrists and the existing
distance/height limits. The extra 0.15-second contact dwell is removed.

Direct rotation is prohibited during pending operations, cancellation, or after
either physics body exists. Normal travel, connected movement, UI, scripts,
animation assets and personal settings are unchanged. No Pandora rebuild.

Startup identifies `Responsive-facing r1`. Correction logs include measured
before/after heading and elapsed acquisition time; normal gate logs distinguish
facing correction, alias cleanup, offset, physics and wrist-contact delays.

Validation: build and CTest results are recorded in the local installation
record. Engine rendering, visual turn behavior and Havok response require an
in-game test; automated checks cannot prove the visible spin is gone.

Test with the same save and partner in Companion mode, hand-holding enabled,
third person and weapons sheathed. Pause with the companion beside either side,
including initially facing backwards. Confirm a quick facing correction and
clasp, then walk, release via combat/weapon draw, and reconnect. If it still
turns slowly, the new logs distinguish a failed reference rotation from a
movement/animation delay instead of reporting an assumed alignment.
