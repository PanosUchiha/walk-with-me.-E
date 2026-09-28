# Classic comfort — r4

A user report (2026-09-25) said Classic hand holding works but "glues" the
companion to the player. Symptoms: running then suddenly stopping, the model
snapping or flickering (at 144 Hz), a collision that hinders player movement,
and the companion fading out as if too close to the camera. TETHER was reported
working and is unchanged. The user also asked for a toggle key.

## Evidence

The report's 9-second clip shows the pair shoulder to shoulder, with the
companion's shoulder overlapping the player's. For about two seconds both
figures render semi-transparent. The camera looks between them, offset toward
the companion's side.

The September 24 local Classic session with Rumarin (66 `Sticky` traces) shows
the same geometry and the per-frame fight:

- `gap` 35.8–38.9: actor centres about 36 units apart. The old spacing was
  `clamp(connectDistance / 2, 35, 40)`, so every default setup held at 35–40.
- `correction` 1.9–11.6 units per write, including 2.8 while walking steady at
  119 units/s. Motion at that speed is about 1.9 units per frame, so the companion
  starts each frame 1.5 frames off the attachment point. The companion is
  teleported back every frame. The September 15 review reached the same result
  (median 3.86, max 18.98).
- `npcUpdates` 30 per second against about 62 frames: the companion's native
  update runs every other frame.
- `characterCollisionOff=true` for the companion only; nothing touched the
  player's controller.

## Causes and changes

1. **Shoulder-to-shoulder spacing.** A 36-unit centre gap overlaps the two bodies,
   and very likely the two character capsules. Classic now uses
   `fClassicSpacing` (default 46, clamped 36–56). A unit test places the grip as
   Classic does, with 34- and 38-unit arms, and shows 56 is still within reach of
   both arms. TETHER keeps its own spacing.
2. **One-sided collision.** Only the companion had `kNoCharacterCollisions`, so
   the player's controller could still meet the companion's capsule. The player
   now gets the same lease whenever the companion's is held (final approach and
   holding). It is restored with the companion's on every release path, and a
   collision override that existed beforehand is preserved. Side effect: while
   holding hands the player also passes through other NPCs.
3. **Native movement fighting the attachment.** On frames where the companion's
   own update runs after the attachment write, it replaces the controller
   velocity with its animation-driven one. The physics step moves the companion
   off the attachment and the next frame snaps them back. The partner update
   hook now re-applies the attachment velocity after that update (horizontal
   only; vertical is left to the controller). This is the leading explanation
   for the correction numbers above. It is not yet proven. **Disproved by the r4
   playtest** (`nativeVelocityDrift=0` while moving) and removed in
   [r5](classic-native-r5.md).
4. **Camera fade.** Not fixed directly. The game has an actor fade near the
   camera (`fActorFadeOutLimit` under `[Camera]`), but how it picks actors was
   not traced. The wider spacing moves the companion further from the camera
   line. The new `sClassicSide = nearest | left | right` lets a player with a
   shoulder camera keep the companion on the other side. `nearest` is the old
   behaviour.
5. **Toggle key.** Controls gains **Hold hands** (`iHandHoldKey`, unbound). It
   flips `[HandHolding] bEnabled`, saves, and says so. With no companion chosen it
   asks for one instead of turning on.

Changing spacing or side releases a live Classic grip so it re-forms.

## New trace fields

Each `Sticky` line now ends with:

| Field | Meaning |
| --- | --- |
| `spacing` | Spacing in use for this attachment |
| `meanCorrection`, `peakCorrection` | Mean and largest per-write snap over the second |
| `peakAlong` / `peakAcross` / `peakVertical` | Largest snap split along, across and up |
| `nativeVelocityDrift` | Largest velocity change the companion's native update made |
| `velocityHolds` | How often the attachment velocity was re-applied |
| `playerCollisionOff` | Player controller's character-collision flag |
| `fadeLow` | Lowest `BSFadeNode::currentFade` seen on the companion |

Reading them:

- Corrections should fall well below the old ~3 units at a walk.
- A large `nativeVelocityDrift` with small corrections confirms cause 3.
- Corrections that stay large with `peakVertical` dominant point to navmesh
  height against physical ground (stairs), which r4 does not change.
- `fadeLow` below 1 while the companion visibly fades means the fade goes
  through the fade node, and a node-level fix is possible. If it stays at 1
  during a visible fade, the fade is applied somewhere else.

## Validation

Legacy and modern Release DLLs build; 24/24 CTest suites pass in each. New
tests cover spacing clamps, side choice, reach at 36/46/56, contact at the
configured spacing, and the hotkey row/INI defaults. **No in-game result is
claimed.**

## Game check

With logging on and Classic selected:

1. Default spacing: pause, connect, then walk, jog and run on both sides. Stop
   and start, and turn sharply both ways. Watch for body jitter and for the
   player stopping short.
2. Walk into a wall or doorway edge while holding and confirm the player is not
   blocked by the companion. Release (draw a weapon) and confirm the player
   bumps NPCs normally again.
3. Try spacing 40 and 52, and each side. The grip should drop and re-form after
   each change.
4. With an over-the-shoulder camera, compare the camera-side and far-side
   companion for fading. Send the `Sticky` lines from both.
5. Bind **Hold hands**, then toggle it while walking, while held, and with no
   companion selected.
