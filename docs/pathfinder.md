# Pathfinder

Added 2026-09-26. While you travel in Natural, Lead, Companion or Custom, a companion who spots a
place you have never found runs out toward it. They turn to catch your eye with a wave and point
it out. Then they lead the way. Whenever they get 1,500 units ahead of you, they stop and wave you
on. Once you come within 600 units, they walk on. When you arrive, they point at the place once
more and come back to their place. Walk another way, or discover the place, and they come back
at once. A notice names what they saw, for example "Katana points out a Nordic ruin to the
north-east." Toggle it with **Roles > Pathfinder > Point out new places**, or the **Pathfinder**
tile on the party actions grid. It is on by default. Roles replaces the former Loot settings
page; its loot settings remain above Pathfinder. Travel no longer has a Pathfinder switch.

## Settings

**Pathfinder** chooses a specific companion, or **Any companion, curious first**.
The default favours an eligible Curious companion who can see the place. A named
choice restricts the role to that companion; another companion does not substitute
when they are unavailable. **Wave first**, **Name what they saw** and **Lead the way**
are independent switches, and all three start on. With **Wave first** off, they still stop
and wait, but without waving. With **Lead the way** off, they run out once, wave, point
and come back, as before 09-28.

Under **Pathfinder tuning**:

| Setting | Default | Allowed range |
| --- | --- | --- |
| Notice places within | 7,000 units | 3,000–12,000 units |
| Stop ahead of you at | 1,500 units | 400–3,000 units |
| Walk on when you're within | 600 units | 200–1,200 units |
| Between sightings | 75 seconds | 20–300 seconds |

**Walk on when you're within** never counts for more than half of **Stop ahead of you at**.
If the two were close, every leg would end the moment it began. **Restore pathfinder
tuning** restores these four numbers while keeping the companion choice and the switches.

## When

The following must all hold:

- Walk With Me is on, in Natural, Lead, Companion or Custom, and the party is travelling. Not
  resting, regrouping or just loaded.
- You are outdoors, and not fighting, sneaking or riding.
- No companion is already away pointing something out or scouting loot.
- The configured cooldown has passed since the last sighting (75 seconds by default), and
  since the last trip ended.

Every 1.5 seconds, Walk With Me scans for the nearest map marker that meets the rules
below. It reads markers from two places: the worldspace's persistent cell and the
player's list for the current worldspace, which the map and compass use. SE 1.5.97 reads
that list at offset 0x4F8, because legacy CommonLib compiles it out of multi-runtime
builds. A marker must:

- names a place (`MARKER_TYPE` 1-59: not doors, quest targets or your own marker);
- is not discovered yet (no `kCanTravelTo`; one merely shown on the map still counts);
- is at least 1,500 units away and within the configured range (7,000 by default);
- lies within about 100 degrees of the direction of travel, never behind you;
- has not been pointed out already this session.

Then it has to be in view. Sight rays run from your eyes and from the companion's eyes to
250, 700 and 1,400 units above the marker. A tower or fort wall can show over a rise
that hides a cave mouth. A ray counts if nothing blocks it or it stops close to the far
end, which is usually the structure. The companion must be travelling within 800 units of
you and not holding hands. With the default companion choice, proximity to the running
point and a preference for **Curious** companions decide who goes. A named companion goes
only when they are free.

### 09-26 evening: why nothing happened

In a 5-hour session, much of it in Natural outdoors, Pathfinder never logged a line. Every
failed scan returned silently, so the log could not say which check failed. The sight
test was the most likely cause. It used a single ray from the companion's eyes to 250 units
above the marker, so any tree, rock or rise in between hid the place. The rewrite adds the
extra sight heights and your eyes, both marker sources, an 800-unit reach (Lucien was more
than 500 units from you in 30% of samples) and Custom. Every scan that gives up now logs why.

### 09-28: why they stopped so far from it

The first sighting in game, Lucien on a city 8,612 units south-east, looked like a
companion stopping well short of anything. Two things caused it. The running point was
only 360 units from you (the menu allowed at most 600, and 500 was set), and it leaned
only 45% toward the place. And the run gave up after a fixed 4.5 s. Lucien's travel alias
had not filled when he was chosen: NFF's package was still running. He covered about 160
units before the timer ended the run, then waved from where he stood. The run now heads
straight for the place, goes 1,500 units by default, and ends on lost progress rather
than a short clock.

### 09-28 later: leading the way

With that build, Valgus chased his running point for the full 20 s. The point stayed
1,500 units ahead of you while you walked, so he never quite reached it. A leg now ends on
the distance from you instead. The point is set 300 units past the stopping distance, so
it is never the thing they wait on. The single run became a guided trip: stop, wave, wait
for you, and walk on.

## What they do

`include/pathfinder.h` handles timing and geometry as a state machine (`State::Tick`, with
`Sense` in and `Cue` out); `UpdatePathfinder` measures, moves and gestures.

From the moment they spot the place until the trip ends, the Pathfinder emblem floats above
their head. It is the scout from the menu tile, near-white over a soft shadow like the loot
chest. It fades in over 0.35 s, then pops and bobs like the other overhead icons. It is gone
the moment the trip ends: after the last point (the only point with **Lead the way** off), or
whenever they are called back. Pointing the place out at the first stop does not end the
trip, so the emblem stays while they lead the way. Turn it off with **Interface > Follower
icons > Show pathfinder icons** (`[Pathfinder] bIcon`). It is projected with the loot chest
(`Loot::Update`, markers tagged `pathfinder`), and has the same rules: hidden in menus, and
when you cannot see the companion.

1. **Run (a leg).** They run straight at the place, to a point 1,800 units from you (the
   stopping distance plus 300), but never closer to the place than 450 units. The point is
   resolved on the navmesh along the straight line and along lines 15 and 30 degrees either
   side, and whichever ends nearest the place wins, so a rock in the way does not cut the
   leg short. Navmesh is gathered out to the leg length plus 400 units for this, not the
   usual 1,200. Each time you walk on 300 units, the point is worked out again from where
   you are. It uses the companion's own slot travel package and marker, at run speed with a
   1.35x boost, as loot scouting does. The leg ends on the first of these:
   - they are within 100 units of the stopping distance from you (1,400 by default);
   - they reach the point;
   - they have gained less than 40 units in 3 s;
   - the leg's time is up: 3 s plus 1 s per 250 units, between 4.5 and 20 s. A new point adds
     time for its distance.
2. **Wave.** If **Wave first** is on, they turn to face you, for at most a second,
   then `IdleWave` (0x3EA32). They hold it for 1.8 s.
3. **Point (first stop only).** They turn to the place, for at most a second, then
   `IdlePointFar_01/02` (0xB8F1E/F), chosen per companion. They hold it for 2.4 s. The
   notice appears when **Name what they saw** is on. With **Lead the way** off, they
   rejoin here.
4. **Wait.** They face you and watch you come, waving you on again every 7 s. Once you are
   within 600 units they lead on: the next leg is worked out from where you stand. A leg
   that would gain less than 300 units on the place is not run.
5. **Last stop.** A stop is the last when it is within 700 units of the place (450 plus
   250), or when a leg after the first gained less than 300 units on it, because the ground
   would not let them closer. When no further leg would gain 300 units, the stop where they
   stand becomes the last. At the last stop they wait for you as usual. When you arrive,
   they turn to the place and point at it once more, without a second notice.
6. **Rejoin.** The travel goal is released and they recommit to their formation place. The
   cooldown starts again from here, so a long trip is not followed at once by another.

While leading, they come back early when:

- **you walk away from them.** While they wait, you are 500 units farther from them than
  the closest you came during that wait.
- **you turn away from the place.** At any point, you are 800 units farther from it than
  the closest you have been.
- **you stop following.** They have waited 90 s.
- **you find it.** The map marker becomes discovered, and the game's banner says the rest.

In every mode they break off if they end up farther from you than the greater of 750
units or the stopping distance plus 400 (1,900 units with default tuning). They also break
off if anything makes them unavailable: combat, a drawn weapon, a scene, hand holding, or a
change of style or order. Stopping mid-gesture does not cancel it. The quest keeps them on
their package, and they finish the gesture.

While a companion is out, the other systems leave them alone:

- teleport catch-up does not move them;
- loot scouting does not pick them;
- automatic rest does not start, since you often stand still to watch them.

The pause, dialogue and a few other states skip Walk With Me's update entirely. A trip then
freezes and carries on afterwards.

## Storage and older settings

The packaged defaults in `SKSE/Plugins/Wayfarer.ini` are:

```ini
[Pathfinder]
bEnabled = true
sCompanion =
sCompanionName =
fRange = 7000.0
fRunToward = 1500.0
fResume = 600.0
fCooldown = 75.0
bWave = true
bNotice = true
bLead = true
bIcon = true
```

`fRunToward` is **Stop ahead of you at**, and `fResume` is **Walk on when you're within**.
An empty companion key means **Any companion, curious first**. The menu writes a selected
companion's reference key and display name. Loading clamps the four numbers to the ranges
above.

Earlier builds used `[Movement] bPathfinder`. That value is still read when the new
`[Pathfinder] bEnabled` key is absent. Saving settings writes the new section and
removes the legacy key, preserving the previous on/off choice. Keep personal INI
files when updating the DLL and artwork.

`fRunAhead` (200–600 units ahead of you) is not read any more, and saving settings
removes it. It measured something else, and carrying a value like 500 over would keep
the run as short as before. `fRunToward` starts at its default.

## Log

```
[Pathfinder] waiting: <why the last scans pointed nothing out>
[Pathfinder] Katana spotted a Nordic ruin (0001A2B3) 5120 units north-east; running 2010 units toward it, to 3340 from it (in plain view)
[Pathfinder] Katana stops 1412 units from you and 3460 from it after 6.1s (far enough ahead)
[Pathfinder] Katana waves (idle accepted)
[Pathfinder] Katana points (idle accepted)
[Pathfinder] Katana waves you on again (idle accepted)
[Pathfinder] Katana leads on, 1180 units closer to it
[Pathfinder] Katana stops 1020 units from you and 640 from it after 19.4s (arrived; the last stop)
[Pathfinder] Katana waves (idle accepted)
[Pathfinder] Katana points (idle accepted)
[Pathfinder] Katana rejoins after 31.0s and 2 stops: pointed it out
```

A stop gives the reason the leg ended: `arrived`, `far enough ahead`, `no longer gaining
ground` or `out of time`. A trip ends with `pointed it out`, `you found it`, `you went
another way`, `you stopped following`, `too far from you` or `interrupted`. `can get no
closer (N units from it)` means no further leg would have gained 300 units.

Waiting reasons:

- a setting or state: `switched off`, `not in this travel style`, `indoors`, `in combat`,
  `sneaking`, `riding`, or the party resting, regrouping or just loaded;
- `no undiscovered place within 7000 units (N map markers nearby)`: nothing to find here;
- `N undiscovered places within 7000 units, none ahead of you and at least 1500 away`;
- `<place> ahead, but <companion>: <reason>`: the chosen companion is in a scene, too far,
  not travelling in formation, holding hands, or not in the party;
- `<place> ahead, but hidden from view`: every sight ray was blocked;
- `<place> ahead, but no open ground to run to`: no line toward it has 200 units of navmesh.

A reason is logged when its kind changes. When only its numbers change, it is logged at
most every 30 seconds.

## Not yet seen in game

Some behaviour can only be judged in game:

- whether the sight test is too strict or too loose on real terrain;
- whether stopping 1,500 units ahead and walking on at 600 feels right, and whether the
  wave reads from that far;
- whether 500 units of walking away (while they wait) and 800 units of turning away (from
  the place) cancel too eagerly on winding roads;
- whether the discovery radius usually ends the trip before the last point.

The map marker is not revealed on the map; the companion only points.
