# Post-combat shoulder rub — motion draft

## Runtime integration: victory revision 4

The user approved v6 and requested it as a randomized post-combat option after
weapons are fully sheathed. The runtime now selects it in one of four equal
sheathed choices, alongside the three existing applause gestures. It uses an
actor-scoped OAR replacement, retains its 6.5-second duration, and cancels on
weapon-state changes or the existing victory interruption gates. The original
draft artifacts below remain unchanged. See `victory.md` for runtime behavior
and `build/victory-r4-review/` for build, package and installation evidence.

## Revision 6: user video reference

The September 16 reference shows a bent elbow leading a fuller shoulder circle:
the forearm folds up, moves forward and opens down beside the hip. The supporting
hand stays against the shoulder, and the torso follows with a small sway. V6
uses two loose circles with a smaller second pass, soft left fingers and wrist,
and coordinated clavicle/chest motion. The existing reach, exhale nod and relaxed
release remain. This is manually authored from the clip, not motion capture.

The right hand follows the clavicle and deformed shoulder surface rather than
the full upper-arm rotation, keeping the supporting hold steady. The left
upper arm establishes the elbow's bend plane before the forearm folds, avoiding
a sideways hinge at the elbow. The circular action runs roughly from 1.4 to
4.3 seconds within the 6.5-second, 196-frame clip at 30 FPS.

Artifacts are in `build/shoulder-recovery-v6/`: editable Blender action, isolated
HKX, full motion and shoulder-detail MP4 previews, plus validation receipts.
Rebuild using the `*-shoulder-reference-*` tools. Earlier revisions remain
available. Checks cover transform continuity, elbow travel and flexion,
hand/body seams, palm contact proxy, endpoint return and HKX round-trip error.
The animation remains a sheathed post-combat draft, uninstalled and untested
in Skyrim. The original user video remains in its attachment directory.

## Revision 5: one left shoulder circle

The user approved v4's relaxed recovery and requested more left-arm movement,
clarifying a circular motion from the shoulder joint. V5 retains the single
right-hand squeeze, short rub, chest breath and small exhale nod. The left upper
arm makes one modest forward/outward/back/down circle beneath that hand.
The left clavicle supports the motion; elbow and wrist remain soft. The circle
runs from 1.75 to 4.4 seconds, before the right hand lowers.

Artifacts are in `build/shoulder-recovery-v5/`: editable Blender source, isolated
HKX, full preview and a detail view that includes the whole left arm. The earlier
revisions are preserved. Authoring and verification use the
`*-shoulder-stretch-*` scripts. The clip remains 6.5 seconds at 30 FPS; it has not
been installed or tested in Skyrim.

## Revision 4: relaxed recovery after exertion

V3 feedback: repeated circular rolls read as caressing the shoulder. V4 changes
the performance to a relaxed recovery gesture: settle the hand, squeeze once,
make a short downward/outward rub, let the shoulder loosen under the palm,
then exhale with a smaller nod and lower the arm. There are no repeated circles.
Chest breathing and coordinated clavicle/arm motion remain, at lower intensity.

`build/shoulder-recovery-v4/` contains the new editable action, isolated HKX,
full and close-up MP4 previews, and validation receipts. Earlier versions remain
intact. The duration stays 6.5 seconds at 30 FPS, and the sheathed post-combat
intent is unchanged. Nothing is installed in Skyrim.

Compared with v3, contact wrist roll falls from roughly 51 to 9 degrees and total
hand orientation range from 64.5 to 18.2 degrees. The motion is one pressure
cycle and a small one-way displacement, with a short hold before releasing.
Continuity, hand/body seams, endpoint return and decoded HKX transforms are
checked separately. Live armor/body fit and playback still need testing.

Author, validate, export and encode with the `*-shoulder-recovery-*` scripts in
`tools/`; these preserve the v3 sources and artifacts.

## Revision 3: circular contact and wrist motion

V2 feedback: the wrist stayed too fixed and the gesture needed more fluid,
dynamic motion. The new working files are in `build/shoulder-rub-orbit-v3/`.
The v2 artifacts remain available for comparison.

V3 traces two rounded paths across the deformed shoulder cap. The hand's contact
point is sampled on the reference mesh; its orientation follows that surface,
with a separate rolling motion at the wrist. The second circle is smaller.
The elbow follows an arc, and the clavicles and chest respond with a short delay.
The motion accelerates smoothly, maintains a steadier pace through the circles,
then eases into the release and exhale nod. The feet and root stay stationary.

The elbow plane is transported through the reach to avoid a pole-vector flip.
The wrist keeps one continuous rotation path while approaching and leaving the
shoulder. Forearm twist is distributed through the helper bones.

The full-motion and wrist-detail previews use 30 FPS source renders. The detail
view exposes the wrist roll and hand contact. `validation.json` checks hand
orientation change, wrist travel relative to the shoulder, contact proxy distance,
continuity, seams and endpoint return. A palm proxy is not a complete hand/skin
collision model; different bodies and armor still require review.

Final v3 checks: 781 samples, 2.179-degree maximum quarter-frame bone rotation,
51.19-degree authored wrist-roll range, and 64.50-degree total hand-orientation
range through contact. The hand/body seam gap stays below 0.0013 reference units.
The decoded HKX differs by at most 0.069 degrees. Both MP4 previews encode at
30 FPS and decode without errors. These figures verify the artifact, not live
Skyrim playback or production animation quality.

Rebuild using the `author-shoulder-rub-orbit-blender.py`,
`validate-shoulder-rub-orbit-blender.py`, `export-shoulder-rub-orbit-blender.py`,
`package-shoulder-rub-orbit-preview.py` and `encode-shoulder-rub-orbit-video.py`
scripts in `tools/`. These produce an editable `.blend`, an isolated HKX and
review media. No animation is installed or registered in Skyrim.
Run the video encoder in ordinary Python; it uses the local `imageio-ffmpeg`
dependency in `build/preview-media-deps`.

## Earlier v2 draft

September 16, 2026. User confirmed no visible reaction with weapons drawn; after
sheathing, companions clapped. The drawn-weapon Civil War cheer remains visibly
unverified despite engine acceptance. No victory runtime changes were made here.

Requested direction: a natural post-combat shoulder rub and nod, after sheathing,
with coordinated chest, shoulder and arm motion. The first isolated arm study
was superseded by the layered v2 draft.

## Motion

6.5 seconds / 196 frames at 30 FPS. The actor breathes in and turns slightly,
raises the right hand across the chest, and lifts the left shoulder into contact.
Two unequal pressure strokes drive small clavicle and chest responses. The free
arm follows with a delayed counter-swing. A short nod accompanies the exhale;
the fingers let go before the arm lowers and the chest settles.

Spine, both clavicles, neck, head, arms, wrist twist helpers and fingers animate.
Root and legs remain stationary. All bone translations and scales remain fixed.
The forearm shares axial rotation with its twist helpers. The reach bows forward
to provide chest clearance; actual armor and different body shapes still need
checking. No facial animation is authored. The simple mannequin head is a review
proxy because the source fit mesh contains only the body and hands.

## Artifacts

`build/shoulder-rub-natural-20260916/` contains:

- `shoulder-rub-natural-v2.blend`: editable action and review camera; source actions retained.
- `shoulder-rub-natural-preview.gif`: unretouched animated Blender renders.
- `wwm_postcombat_shoulder_rub_v2.hkx`: isolated Skyrim SE draft, not installed or registered.
- `authoring.json`, `validation.json`, `export.json`: timing, scope and verification.

Scripts: `tools/author-shoulder-rub-natural-blender.py`,
`tools/validate-shoulder-rub-natural-blender.py`,
`tools/package-shoulder-rub-preview.py`, `tools/export-shoulder-rub-blender.py`.
The existing relaxed gesture Blender source is preserved.

## Validation and remaining work

781 quarter-frame samples: maximum rotation step 2.182 degrees, unchanged
unanimated bones, unchanged translations/scales and exact endpoint return.
42 hand/body seam pairs stayed within 0.0013 reference units. These checks do not
measure every collision or establish production animation quality.

The exported 99-track HKX has 196 frames and a 6.5-second duration. Its decoded
rotation error is at most 0.069 degrees versus the exported samples. The existing
8.5-second spline block duration convention is retained. Live behavior playback
is not tested. No existing idle, animation replacer or game installation changed.

Before integration: review the performance, test body/armor variants, establish
an isolated animation event or scoped replacement, and match runtime duration to
6.5 seconds (the current victory reaction stops at 4.9). Preserve interruption
on combat, dialogue, weapon drawing and incompatible orders. Sheathed playback
only, as requested. AAA quality is a target for iteration, not a verified status.
