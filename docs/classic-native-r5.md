# Classic native movement and interlocked clasp — r5

The r4 playtest (2026-09-25, Rumarin) still showed heavy hand flicker and
direction flicker. The clasp also looked like two palms touching rather than a
held hand: each hand stayed on its own side instead of reaching over the other.

## What the r4 trace showed

29 `Sticky` lines over 7 grips (each lasted 2–11 s):

- `nativeVelocityDrift=0.0` on every moving line. The companion's own update
  never changed the controller velocity, so r4's velocity hold did nothing.
- The per-frame snap was still 3–13 units on average (peaks 10–23), almost all
  of it along the walking direction.
- `nativeDirection` held about 0.93–0.998 on the left side and 0.125 on the right
  side. Both point away from the player (up to 45°). Right-side grips had the
  largest snaps (mean 10–13).
- Releases came from the attachment path: `movement obstacle` ×2,
  `attachment left reachable ground` ×2, `another arm animation` ×2,
  `actor unavailable or busy` ×1.

Skyrim's own steering keeps pushing the companion away from the player. Classic
put them back every frame by teleporting, and the legs and hands showed both
positions. The camera-fade reading stayed at 1.00 throughout (no fade in this run).

## Movement: native after the hands connect

Classic keeps its approach and connection. On connecting it now starts the same
ticketed alias handoff TETHER uses. WWM's movement alias is cleared by the
serialized Papyrus sweep and acknowledged with a ticket. After that,
`KeepOffsetFromActor` walks the companion beside the player. The offset is
`side × fClassicSpacing` across and 10 behind, with catch-up 60 and follow 10,
the values TETHER already runs with. Facing follows the player.

While native movement is active Classic writes no position, heading or gait
variables. It only poses the arms and watches the pair:

- Separation gets TETHER's grace: 0.4 s beyond the release distance, or at once
  45 units past it.
- Arms may stay out of reach for 1.5 s (was 0.25 s) while the companion catches up.
- A change of the companion's package re-requests the offset, because
  KeepOffsetFromActor belongs to the running package.
- Release queues `ClearKeepOffsetFromActor` and returns the companion to WWM.
  WWM's alias sync also clears the offset when it reclaims them. If the offset
  call lands after release, its callback clears it again.

Until the handoff is acknowledged, the old per-frame attachment bridges the gap,
so connecting while walking still works. Classic tickets are negative and
TETHER's positive, so neither backend accepts the other's acknowledgement. The
dispatcher routes ownership to whichever backend holds it. No script or ESP
change is needed.

## Clasp: interlocked instead of palm to palm

Old pose: each palm turned to face the other person, with the wrists 2 units
either side of the centre line. That flipped each hand about 180° from its
natural hanging position and left the hands side by side.

New pose (`IK::InterlockedHand`):

- The player's palm faces back and the companion's faces forward. The palms press
  front to back, one hand's depth apart (2 × 2.35).
- Each hand's fingers point down and 35° across the centre line, so they reach
  past the other hand. Each wrist stays on its owner's side.
- Fingers curl 0.5 rad per joint (was 0.38) toward the palm, around the other
  hand's far edge.
- The wrist now rolls about 90° instead of 180°. A third of that roll goes on
  `ForearmTwist1` and two thirds on `ForearmTwist2`, so the skin follows instead
  of wringing at the wrist. Skeletons without twist bones skip this.

Thumbs are unchanged.

## Validation

Legacy and modern Release DLLs build; 24/24 CTest suites pass in each. New tests:

- palm directions, palm centres meeting at the grip, wrists on their own sides
  and fingertips across the centre line, for both sides;
- the aligned hand matches the clasp frame;
- the roll is under 100° here, where the old pose needed 180°;
- clasp wrists are within reach at spacing 36/46/56;
- Classic handoff tickets route to Classic and TETHER's to TETHER.

**No in-game result is claimed.**

## Game check

1. Pause beside the companion and connect. The log should show `Attached ...
   alias handoff -N requested`, then `Alias handoff -N acknowledged` and
   `Native movement active`. `Sticky` lines should then read `native=true`.
2. Walk, jog and run, stop and start, and turn both ways, on both sides.
   Watch body, legs and hands for flicker. `lateral` and `along` show where the
   engine actually holds the companion.
3. Look at the clasp from behind and from each side. The player's hand should
   be in front, fingers crossing over the companion's hand.
4. Release by drawing a weapon. The companion should rejoin normal travel
   without staying glued beside you.
5. Toggle hand holding with the key while held, and reload a save made while
   holding hands.
