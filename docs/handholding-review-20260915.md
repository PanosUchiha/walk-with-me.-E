# Handholding log and Blender review — September 15

## Current runtime evidence

Captured the live logs at 00:49:20 Eastern into
`build/follower-captures/20260915-004920-338`. The main log starts at 00:37:42
and the captured handholding trace ends at 00:47:14.931. The selected partner
is Rumarin, form ID 250AB6EC, on the player's right. The user reports that the
follower's body or legs jitter, rather than just the hand pose.

The loaded build identifies itself as 0.16.7, hand revision 11.3. The installed
DLL hash at capture was
`807701b95bac8cf8fc809071100e7dab9fa42f0671700f1511595dd2d522154f`.

- 55 sticky samples, including 50 moving samples. Physics speed matches the
  requested speed in every sample; zero samples reproduce the old zero-velocity bug.
- The native heading guard intercepts 26–31 requests per sampled interval.
  Rejected heading error reaches 6 degrees; final heading error is zero.
- Position correction has a median of 3.86 game units and a maximum of 18.98.
  At 00:46:01.862 the player speed is 328.3 and requested follower speed is 420.9.
  Walking samples around 00:47:04–00:47:13 repeatedly correct about 4–6 units.
- Every final wrist gap is 4.00 units. This is wrist-to-wrist distance after
  posing, not palm contact quality or a measurement across all rendered frames.
- At 00:46:10.210 the grip releases due to `another arm animation`, then
  reconnects at 00:46:12.358. A controller reset releases it at 00:46:21.471.

The corrections are consistent with the reported whole-body jitter, but are not
proof of its cause. `UpdatePartner` writes a grounded attachment position while
the follower also retains native movement and a travel package. On turns, the
outside actor can legitimately need more speed than the player. A sampled speed
difference alone is therefore not proof of an error.

The log samples once per second, after correction. It does not split horizontal
and vertical correction, record every intermediate native graph speed, or show
the actor/controller position before and after each native update. Those are the
next useful measurements; neither the existing log nor Blender establishes a
tested movement fix. Runtime code and installed game files were not changed by
this review.

`tools/report-handholding-log.py` produces the summary, including original line
numbers and the largest correction samples, from the preserved log.

## Blender artifact

Output: `build/handholding-blender-20260915/walk-with-me-soft-clasp-v1.blend`.
Input: the existing `walk-with-me-gestures-relaxed-v2.blend` in Blender-Docs.
The input was preserved. Blender 5.2.1 LTS was run with factory startup settings;
the configured connector's regular startup timed out.

The new file contains two copied rigs, editable SoftClasp actions, the retained
revision 11 pose reference, timeline markers, and a review camera. Each action
has 151 frames at 30 FPS: reach 1–31, hold 31–121, release 121–151. Only the
connected arm, fingers, thumbs and forearm twist bones animate; the legs, root,
torso and opposite arm remain at the imported reference pose. The small shared
hold swing is an animation study, not a walking or running cycle.

The draft lowers the shared grip from 76% to 92% of the shorter arm's reach,
uses front/back palm contact, distributes forearm roll, grades the finger curls,
and poses the thumbs. The poses were inspected from front, back and three-quarter
views. The reference body is CBBE, not Rumarin's live mesh or the live XPMSSE rig.

Validation sampled 601 poses, including subframes:

- Unanimated bones, translations and scales unchanged; exact endpoint return.
- Maximum quarter-frame rotation: 1.085 degrees.
- 42 matching wrist seam vertex pairs per actor; maximum seam gap 0.001285 units.
- Held wrist gap remains 4.87134–4.87136 units on this reference pair.

These checks verify continuity and seams, not all interpenetration or in-game
appearance. The draft still needs testing with the actual actors and both sides.

### Integration status

The `.blend` is editable animation source, not an installed game replacement.
Walk With Me's current grip is produced by C++ IK in `src/hand_holding.cpp`; it
does not load these Blender actions. A standalone HKX export would not replace
the active procedural grip. Integration must port the approved geometry and
bone ownership/restoration into that solver, or deliberately replace it with an
animation overlay. Existing rejected revisions 12/13 make live comparison
necessary before calling another runtime pose successful.

Rebuild using `tools/author-handholding-blender.py`; validate the saved file with
`tools/validate-handholding-blender.py`. Package the raw renders with
`tools/package-handholding-blender-preview.py`.
