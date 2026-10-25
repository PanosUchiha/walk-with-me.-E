# Hand holding

Experimental and off by default. Use third person and sheathed weapons. The quickest
way to start is the **Hold hands** tile on the Party actions page, or its hotkey: with
no partner chosen yet, it takes the companion you are looking at, or else the nearest
one in your party, and switches the formation to Companion, the only style hand
holding runs in. You can also choose a follower in Travel settings and enable Hand
holding there. Pause to let them approach and connect. Walking, jogging and running
are supported.

## Choose a method

In Travel settings, choose **Hand-holding method**:

- **Classic** poses the arms procedurally in an interlocked clasp. It does not
  use the clasp clips or TETHER physics. After the hands connect,
  the engine walks the companion beside you with the same offset movement TETHER
  uses (see [Classic native r5](classic-native-r5.md)).
- **TETHER** keeps the physics grip and the included OAR clasp poses.

Classic is the default. An explicitly saved TETHER selection is preserved, except
that turning hand holding on with TETHER selected and Open Animation Replacer
missing switches the method to Classic and says so.

Both methods use the same enable toggle, selected companion, lead-in delay and
connection/release distances. Changing methods releases the current grip;
Classic waits for any outstanding TETHER physics cleanup before starting.
For manual configuration, set `[HandHolding] sMethod = classic` or `tether`.
Unknown or missing values use Classic. Both methods remain experimental; Classic
can still show wrist twisting, sliding or awkward turns with some actors.

Bind **Controls > Hold hands** (`iHandHoldKey`) to turn hand holding on and off
from the keyboard. It toggles the same setting as the menu and saves it.

### Classic spacing and side

- **Hand spacing** (`fClassicSpacing`, default 46, 36–56) is the distance between
  the two actors' centres. The old fixed 35–40 put the shoulders together.
- **Companion side** (`sClassicSide = nearest | left | right`). Nearest keeps the
  side the companion arrives on. If your camera sits over one shoulder and the
  companion fades out while holding hands, choose the other side.

Changing either releases the grip, which forms again at the new position.
While Classic holds hands (and during its final approach), character collision
is off for both you and the companion, so neither can shove the other. It is
restored on release. See [Classic comfort r4](classic-comfort-r4.md).

The direct grip r4 update connects TETHER before alias cleanup. The follower
holds their arrival position while physics is prepared, then changes movement
control after the hands connect. There is no arrival dwell or gradual grip-gap
widening. See [direct grip testing](tether-direct-r4.md).

TETHER now updates the companion's facing throughout a connected hold. If their
AI package changes, it renews the offset movement command. A companion who stays
still while the player moves gets one movement retry, then releases back to
ordinary following if the stall persists. See [movement r5](tether-movement-r5.md).

After the grip connects, the existing WWM route keeps following the player's
side while alias cleanup is queued. It yields as soon as the alias or package
is released; offset movement still waits for cleanup acknowledgment. This
addresses the stationary wait observed with Inigo. See [handoff r6](tether-handoff-r6.md).

## Clasp animations

TETHER uses 68 OAR clasp clips, included in the main download under
`meshes/OpenAnimationReplacer/WWM Clasp`. Classic does not use them. Each clip is a
vanilla idle, walk or run clip with WWM-authored arm, wrist and finger rotations.
The legs, root motion, events and bindings are Skyrim's own. They play only while
that actor holds a TETHER grip. No behavior regeneration is needed.

If you generated clasp clips yourself for an earlier version, disable or remove that
mod (named like `Walk-With-Me-0.2.2-Clasp`). The included clips are identical. A
clasp mod generated for 0.16.8 is worse: its OAR conditions require a plugin
version above 0.2.x, so OAR never loads the clasp poses.

The clips were made with the unmodified
[PyNifly codec](https://github.com/BadDogSkyrim/PyNifly) at revision
`bcb723aba2ca4a80e976d957efbde69b33f2e56f`. The generator, the codec's source and
license, and the rotation recipe are in the source download. To rebuild the clips,
see [build](build.md).

## Behavior and limitations

The TETHER backend combines native approach, ticketed package handoff, a
TETHER-based physics grip, and authored OAR clasp poses. The hands connect first;
script cleanup and the engine package transition gate only the subsequent
offset movement. Classic approaches through WWM travel, connects with
procedural arms, then hands the companion to the same ticketed offset movement.
It writes no position, heading or gait once that movement is active.

Sprinting, combat, drawn weapons, jumping, swimming, mounting, dialogue, scenes,
rest, first person, blocked movement or excessive separation release the grip.
Failed attempts require space and another approach. The selected partner is
never silently replaced by another companion.

## Tuning the clasp

The settings in this section apply only to TETHER.

The follower turns at their arrival position before physics is prepared. Bodies
and the constraint are created in three serialized script calls. No alias-cleanup
round trip or second walking destination precedes the grip. Script scheduling,
stopping, turning and the current hand pose can still affect connection time.

Two `[HandHolding]` settings decide how the clasp itself looks, because the right
values depend on the body and hand meshes installed:

- `fGripWristGap` (default 45) is how close the wrists must already be before the
  constraint is created. The constraint pulls the two palm anchors onto one
  another the instant it exists, so a smaller value clasps more accurately and a
  larger one clasps more readily. This limit applies immediately and does not
  grow with time. Older explicit values are retained; raise a saved value of 20
  to 45 for the direct grip behavior tested with Rumarin's recorded distances.
- `fPalmX` / `fPalmY` / `fPalmZ` (default `0, -2.35, 5`) place the contact point on
  the palm surface in hand-local game units. The skeleton's hands are mirrored, so
  one triple describes the same point on both palms.

To set these from measurement rather than guesswork, approach with logging on and
read the acquisition trace, which prints the measured wrist gap against the limit
in force once a second:

```
[HandHolding/TETHER] Session 12 gate=grip proximity root=44.8 wrist=46.3/45.0 ...
```

A `wrist=` figure that never falls near the limit means `fGripWristGap` is tighter
than this pair can achieve; raise it to just above the value the log settles at.
Changing any of these releases a live grip so the constraint is rebuilt with them.

While connected, the clasp poses override matching locomotion replacements
for those actors. Other replacers resume when the grip is released. Custom
bodies, skeletons and non-interruptible animations can affect appearance and
timing. Wrist contact, turning and transitions still need in-game evaluation.

For reports, include a video, the actors' sides, movement state, skeleton and
animation mods, and `Walk With Me.log`. The standalone TETHER plugin is not required.
