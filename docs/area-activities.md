# Area activities

Resting companions now behave according to where they are. The **Area activities**
toggle in Rest & Sandbox controls this; the INI key is `[Sandbox] bAreaActivities`,
and it is on by default. It works without **Extra rest poses**. Extra rest poses
still adds the generic poses: ground sitting anywhere in the wilds, reading and
stretching.

## The snap-back fix

The 2026-09-23 log showed dispersal walks being undone within seconds. M'rissi
walked 685 units from her stopping point, and seven seconds later she was 50 units
from it again. Skjel reached an idle marker 418 units out and was pulled back the
same way. The cause was the native sandbox package: it is centred on the per-slot
anchor marker, and when it resumes it heads for that centre.

After any rest walk, the anchor marker now moves to where the companion ended up.
This happens whether they arrived or were stopped by taking a seat within 250 units
of the destination. The native radius narrows to 200 units near furniture and
idle markers, and 320 units elsewhere. The original stopping point is kept
separately as `restHome`. Every Wayfarer search is still centred on `restHome` with
the full radius, so companions do not drift across the map.

## Places

The place comes from the location's keywords and whether the cell is an interior:

| Place | Keywords |
| --- | --- |
| Wilderness | Any exterior that is not a town |
| Town | City, town, settlement, farm, orc stronghold, hold capital, lumber mill (exterior) |
| Inn | `LocTypeInn` |
| Player home | `LocTypePlayerHouse` |
| Temple | `LocTypeTemple` |
| Shop | `LocTypeStore` |
| Hall | Castle, guild, barracks, jail, steward's dwelling |
| Cave | `LocSetCave`, `LocSetCaveIce`, animal den, mine |
| Ruin | Nordic/Dwemer ruin, fort, crypt, dragon priest lair, Falmer hive, vampire/warlock lair, generic dungeon |
| Home | Any other interior |

## Things worth walking to

The rest-area search also looks for the following. Models are matched by path and
cached per base form.

- **Fires.** Lit campfires (`...Burning.nif`), fire effects, fire pits, fireplaces,
  braziers, and cooking pots or spits. Unlit (`...Off.nif`), hanging and sconce
  models are skipped. Up to three companions take places in a ring around a fire.
  In the wilds they sit facing it or crouch to warm their hands. In caves and ruins
  they crouch; indoors and in town they stand. Fire poses last 40-70 seconds, and a
  companion who stays nearby usually carries on. Cold weather (snow outside, or an
  ice cave) makes fires much more attractive.
- **Shrines.** The Divines' shrines and named shrines. The companion walks up, faces
  the shrine, prays, and then usually stands in reflection. One companion per shrine.
- **Standing stones.** The companion studies or examines the stone.
- **The water's edge.** In exterior wilds and towns, reachable ground no more than
  40 units above the water level. The companion faces away from the land, then
  kneels at the water or looks out.
- **Lookouts.** In the wilderness, reachable ground at least 180 units above the
  rest point. The companion faces outward and looks into the distance.

Two cases are handled differently:

- If nothing else is found outdoors, the periodic idle search falls back to the
  water's edge and high ground.
- Work, ritual and quest furniture is no longer offered as somewhere to rest:
  shackles, mining and hammer markers, walk-through-wall markers, the Molag Bal
  altar, execution blocks, cart and ladder markers, and similar.

## Fitting the place (2026-09-25)

Report: companions kept eating bread in the middle of town. Two things did it:
Skyrim's own sandbox eats whatever food a companion carries (the sandbox package's
Allow Eating input), and it was allowed everywhere but dungeons; and the refreshment
table counted a town's streets as a "settled" place, weighting food 4 to 1 there.

Now, in `include/area_activities.h`:

- **Meals** (`MealPlace`, `NativeMeals`) happen only at an inn, in a home, the
  player's home or a hall, or at a campfire in the wilds. Never in a town's streets,
  a temple, a shop or a dungeon. Scripted standing food and drink are narrower still:
  the inn and the player's own home. Other homes and halls eat at their tables
  through the native sandbox.
- **Reading** is not offered in the street, a shop, a cave or a ruin. **Stretching**
  is not offered in a temple or a shop, and a temple drops the generic examine.
- **Temporary rest is a short stop** (`settled` false). It keeps to brief standing
  poses: no eating (except a drink at the inn), reading, ground sitting, studying or
  sweeping, and at a campfire they crouch rather than sit. A Relax order settles in,
  and a Relax camp in the wilds may eat by its fire.

These poses are chosen when nothing in particular is being walked to:

| Place | Poses |
| --- | --- |
| Wilderness | Look into the distance; rub arms when cold; clean a one-handed sword |
| Town | Hands behind the back; look around; rub arms when cold |
| Home | Study; hands behind the back |
| Player home | Sweep (the broom comes from the animation); study; food, drink and reading with Extra rest poses |
| Temple | Pray; study; hands behind the back |
| Shop | Examine the wares; study |
| Hall | Hands behind the back; study |
| Inn | Drinks and food; hands behind the back |

On a short stop, study and sweep are left out, and only the inn offers a drink.
| Cave | Examine; nervous glances; wipe brow; look into the dark; clean a sword |
| Ruin | Examine; study carvings; nervous glances; look ahead; clean a sword |

The idles are all vanilla, root-level and conditioned only on no torch and no
shield out; clean sword additionally needs a sword. The behaviour graph was checked
for which ones loop:

- **Loops**, held for their full duration: warm hands (standing and crouched),
  kneel, study, sweep and nervous.
- **One-shot clips**, held for 5-12 seconds: look far, pray, wipe brow, clean
  sword, rub arms and applaud.

Kneeling and crouching end through `IdleChairExitStart`, falling back to `IdleStop`.
If the actor is still posed 3.5 seconds later, `IdleForceDefaultState` clears it.
Before a pose at a fire, shrine, shore or lookout starts, the companion turns toward
it at 4 radians per second.

## Conversations by place

| Place | Length | Gestures | Style |
| --- | --- | --- | --- |
| Inn | 150-180 s | none after the first | Both partners pick up a drink and talk over it |
| Cave, ruin | 90-120 s | about 1 turn in 3 | Hushed: negative/positive responses, glances, wiping the brow |
| Temple | 90-120 s | about 1 in 4 | Quiet |
| Shop, hall | 100-140 s | about half | |
| Town | 120-180 s | about 2 in 3 | Lively: laughter, pointing into the distance |
| Wilderness, homes | 120-180 s | about 2 in 3 | Includes a laugh and pointing outdoors |

## The inn table

- **Forming.** When any companion sits on a chair, bench or stool, that spot becomes
  the table. Anyone sitting within 300 units of it, however they got there, counts
  as seated at it.
- **Joining.** Everyone else in the inn walks to the nearest open seat within 280
  units of the table. This includes companions already sitting at another table,
  strolling, or in the middle of a pose. Only a conversation in progress, or having
  left this table in the last minute, keeps them away. Benches count one place per
  sit marker, so a bench with one companion on it still has room.

  This uses the existing seat package: the per-slot seat alias is pointed at that
  seat instead of the ground-seat marker. Getting up and crossing the room may take
  up to 45 seconds. A table seat is held for 4-6 minutes. After that the companion
  gets up, stretches their legs for at least a minute, and may rejoin.

  The first playtest (2026-09-23) showed nobody joining. Native sandbox had already
  sent the others to seats about 1000 units away before the table formed, and
  anyone already seated was skipped.
- **Calling a server.** About 15 seconds after two companions are seated, and every
  2.5-4 minutes after that, one of them calls a server. The server is the nearest
  `JobInnServer` who is free: not seated, not in a scene and not talking.
  Innkeepers are never pulled out from behind the bar. The caller turns to watch.
  The server is sent to open floor beside the caller's chair with Papyrus
  `PathToReference` and nods on arrival. Everyone seated then drinks in their chair.
  If no server is free, the companions drink a round they brought over themselves.
  A server who has not arrived within 35 seconds is given up on.
- **Between rounds.** Every 14-30 seconds something happens at the table. Usually
  two companions turn to each other and talk for 20-35 seconds, sometimes over a
  drink. Otherwise one of them drinks, or occasionally eats. Seated drinking and
  eating are the vanilla chair loops (`ChairDrinkingStart`, `ChairEatingStart`),
  and `IdleStop` puts the mug down without standing up. Seated companions can speak
  without leaving the table.
- **The bard.** When a `JobBardFaction` performer in the same cell finishes a scene
  (a song), companions who are standing applaud.
- **Breaking up.** The table breaks up when nobody has been seated for 8 seconds
  (40 if someone is still walking to a chair), or when rest ends or the party
  leaves the inn.

## Log lines

- `[AreaActivity] X warms their hands at the fire in the wilderness` shows each
  place- or feature-driven pose.
- `[RestActivity] X approaching fire|shrine|standing stone ...` and `... heading for
  the water's edge|high ground` show the walk toward a feature.
- `[RestRecovery] X returning to native sandbox; ... settling here (radius N)`
  shows the new focus point.
- The inn table logs as `[Tavern] X sits down at a table`, `X joins Y at the
  table`, `X calls Y over for drinks`, `Y brings a round to the table`,
  `X drinks at the table accepted=...`, `X and Y talk at the table`, and `the bard
  finishes a song`.

## Not verified in game

The following has unit coverage for the pure logic only (`WayfarerAreaTests`):

- the fire and shore facing;
- the crouch and kneel exits;
- `PathToReference` on servers;
- the seated drink loops;
- chair joining through the seat alias;
- the bard scene detection.
