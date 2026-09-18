# TETHER revision 18 — authored clasp and package handoff

The user reports r17 still joins wrists and connects inconsistently. Capture
`build/follower-captures/20260917-013714-549` confirms r17 was loaded. Of six
recorded handoffs, two reached physics connection, two cancelled when the player
resumed movement, and two cancelled for a grouped actor/camera/controls reason.
One successful handoff waited over seven seconds for cleanup acknowledgement.
The grouped reason is now split into specific diagnostics.

## Handoff correction

Script cleanup can finish before the engine changes packages. Previously the
combined acknowledgement was discarded until another Papyrus pass. Cleanup
completion is now remembered by actor and ticket; the main update checks the
package transition after that acknowledgement. It never substitutes an empty
alias for completion of the script's in-flight ClearKeepOffset calls. A refilled
alias invalidates completion and requires a fresh script acknowledgement.
The script only calls the priority service when a request actually exists.

Tests cover delayed package transitions, old tickets, reassigned actors,
refilled aliases and the existing serial acquisition/teardown behavior.
This repairs a code path consistent with the observed wait; the old log does
not record when individual script functions finished, so the exact amount of
in-game latency improvement remains unmeasured.

## Authored hand pose

The ball-and-socket joint alone does not control hand rotation or finger curl.
Rather than another distance-only adjustment, this build adds two OAR conditions
and 68 local test clips covering male/female idle, walk, run and turning for
either selected hand. Twenty rotation tracks change: the selected upper arm,
forearm, forearm twist helpers, hand and fingers. WWM's existing soft-clasp
Blender study supplies those rotations. No old procedural movement/IK hooks are
restored. Pose roles activate during offset positioning and clear on release,
failure and load. Missing OAR registration or core assets disables acquisition.

The fixed anchors now lie on the palm surfaces `(0,-2.35,5)` in body-local game
units. This is used together with the authored opposing palm pose; a surface
offset without that pose was not a sufficient solution.

The local clips use the user's extracted vanilla locomotion containers. While
held, these override matching idle/movement replacements for the two actors;
the usual replacers resume on release. Existing non-interruptible OAR clips may
wait until a loop or movement transition before the clasp replacement starts.
No behavior regeneration, new full-body idle event or ESP change is required.

The normal exporter drops extracted motion and float channels. The build tool
therefore replaces only spline payloads in the original containers, retains
original motion objects, events and bindings, and copies the compressed float
channels. Every original non-spline-header payload byte is preserved. All clips
are decoded again and checked for frame counts, duration, bindings, events and
round-trip transforms (rotation <0.3 degrees, position/scale <0.003).

The decoded female walk pose was rendered in Blender; its palm normals oppose
at about 167 degrees in the sampled frame. This preview has no Havok connection
and is not proof of contact, skin quality on other bodies, or in-game behavior.
The reference pair remains separated until physics would bring it together.

## Validation and installation

Modern and legacy builds: 19/19 CTest suites each. The quest script compiles
with zero errors/warnings. Animation validation and the decoded-pose review are
included in test evidence. DLL, quest PEX and OAR assets install together while
Skyrim is closed. Settings/ESP are preserved and r17 is backed up with hashes.
The build has not been tested in Skyrim at packaging time.

Restart Skyrim. Pause in third-person Companion mode with sheathed weapons,
allow connection, then walk and turn. Inspect palms, finger curl and wrist
seams. Repeat on the other side and after dialogue/combat. Confirm normal travel
returns after release. Logs should show Revision 18, registered clasp conditions,
available pose assets, script cleanup completion and package acknowledgement.

The generated local clips contain Skyrim-derived locomotion data. Keep this
test package local; it is not a public redistribution release. The authored
rotation JSON and tools allow rebuilding against a user's own game assets.
