# Loot scouting

One companion, the **loot finder**, looks over the finds in view, walks to the
richest one and kneels to search it. That gives you time to notice and come
over. Then they stand and wait beside it for up to **15 seconds**. A chest emblem appears above their
head until they leave. It pops up, floats gently and stays slightly transparent:

- **White**: ordinary loot.
- **Gold**: at least one item is worth more than **Valuable above**
  (`iValuableGold`, 1000 by default). A pile of septims counts as its total.

They return to formation when you open the find, when it is emptied, or when
you move beyond **Leave when you are beyond** from it, or the 15-second wait ends.
An unopened find no longer holds them indefinitely. The wait begins after standing
and pauses with normal paused gameplay; it does not cut short the search animation.
If you open the find or leave while they
are kneeling, they stand up first. A find they could not reach within a few
seconds is dropped, and the next richest one can be chosen.

## One find per area

When the first find comes into view, the finder keeps looking for about a
second more, then picks the find holding the most in total value. Its best
single item, then nearness to you, break ties. So a chest a few steps further in
still beats a bandit's body at the doorway if it holds more.

Once the finder reaches a find, they stop searching. They look again only
after you are **Search again after moving** (2000 units) from that find, or
after you enter another interior or worldspace.

## Settings

Scouting settings are on the **Loot** page, with numeric controls under
**Search tuning**. **Show loot icons** is under **Interface > Follower icons**.
Settings save automatically. The matching `[Loot]` keys in `Wayfarer.ini` are
shown in brackets.

| Setting | Default | Effect |
| --- | --- | --- |
| Point out loot (`bEnabled`) | On | Turns the feature on or off. |
| Notice within (`fSearchRadius`) | 1000 units | Distance from the finder. They also need line of sight. |
| Leave when you are beyond (`fLeaveDistance`) | 1500 units | Distance from you to the find. New finds must be within 80% of it. Capped below `[Safety] fReleaseDistance`. |
| Valuable above (`iValuableGold`) | 1000 gold | Threshold for the gold emblem. |
| Search for (`fSearchSeconds`) | 12 s | Time kneeling at the find before standing to wait (5-30). |
| Search again after moving (`fAreaDistance`) | 2000 units | How far you must move from the last find before the next (800-6000). |
| Loot finder (`sFinder`, `sFinderName`) | Any companion | Only this companion scouts. Any companion sends the nearest free one. |
| Containers (`bContainers`) | On | Chests, urns, sacks and similar. |
| Bodies (`bBodies`) | On | Dead people and creatures. |
| Loose valuables (`bLooseItems`) | On | Unowned loose items, only when they are valuable. |
| Show loot icons (`bIcons`) | On | The animated chest emblem, controlled on the Interface page. Scouting still works without it. |

Only one companion is away at a time, whichever mode you choose.

## What counts as a find

- The finder must see it. A line-of-sight ray runs from their head to its
  middle, or to its top over a low obstacle. Only the find itself may stop the
  ray, so a chest pressed against the far side of a wall counts as hidden.
- They must be able to walk there. There must be a standing spot beside the find
  on the navmesh connected to the finder, reached by a short route (at most 1.5
  times **Notice within**, and at most twice the straight distance plus 300
  units). Nothing may block the reach between that spot and the find. A chest
  seen through bars, across a gap or on a ledge is skipped for the rest of the
  area. One reachable only through the next room is skipped until the party is
  closer.
- It must be on roughly the same floor (within 200 units vertically).
- It must be at least 150 units from you, since you are already standing at it
  otherwise.
- It must hold something worth a detour: a named, playable item that is not
  only food, ingredients, pelts or animal parts, unless that item is valuable.
  A barrel of cabbages or a wolf with only a pelt is skipped.
- Owned containers and items (taking them would be stealing) and your own
  storage are ignored. Dead followers and summons are ignored.
- Finds you open are never shown again. Leaving an interior or worldspace
  forgets everything, so unlooted finds can be shown again when you return.

Reading an unopened container resolves its leveled loot a little earlier than
opening it would, as quick-loot mods do. To compare finds, every find the finder
can see is read once, up to six new ones a second. Finds out of sight are never read.

## The search animations

All three are vanilla looping idles that Skyrim itself plays without furniture:

| Find | Idle | Vanilla use |
| --- | --- | --- |
| Bodies, and loose items on the floor | `IdleSearchBody` (`000EFC64`, graph event `IdleKneeling`) | `SearchingBodyIdleMarker`, `WE23ScavengerPackage`, and Ralof in Helgen at the end of a Travel package, the template WWM travels with |
| Low containers (center under 45 units) | `IdleSearchingChest` (`0006FF14`) | `SearchingChestIdleMarker` |
| Tall containers and raised items | `IdleSearchingTable` (`0006FF0F`) | `SearchingTableIdleMarker` |

The behavior graph (`mt_behavior.hkx`) holds each as an enter/loop/exit state,
so the pose lasts until it is ended. The finder then sends `IdleChairExitStart`,
or `IdleStop` if that has no transition, to play the stand-up. As a safety net, a
follower still reporting `bIdlePlaying` about three seconds later is cleared with
`IdleForceDefaultState`, so nobody walks off frozen in a kneel. If something
interrupts the loop mid-search, it is restarted, at most twice.

If the idle is refused, the graph event is sent directly. If that fails too, a
brief `IdlePickup_Ground` plays instead. The log says which one ran.

## When it runs

Scouting is off inside places where people live or work: houses, inns, temples,
shops, castles, guilds, barracks and your own home. The place is classified the
same way as for [area activities](area-activities.md). A finder already out is
called back as soon as you enter one. Caves, ruins, clearable dungeons and the
outdoors are unaffected. **Inside homes and inns** in the Loot scouting menu, or
`[Loot] bInhabitedInteriors`, turns it back on. The default is off.

Scouting uses the same control as formation travel, so it follows the same
rules:

- It works while you travel and while you stand still. While the finder is at
  a find, temporary rest is held off so they are not pulled away.
- It does not start during temporary rest, the **Relax** order or Vanilla mode.
- Combat, sneaking, a drawn weapon, dialogue and the other `[Safety]` release
  rules end it, as they end formation travel. A kneeling finder stands up first.
- The hand-holding companion is never sent. Walking banter partners finish
  their conversation first.

## Implementation notes

- No ESP changes. The finder keeps their slot's travel package, and the slot
  marker is moved onto the find. The package stops about 65 units short, at the
  edge on whichever side they approach from. On arrival the marker is parked where they
  stand, so the package never tugs them out of the kneel. `GetSlotFollower`
  keeps the finder bound while you stand still.
- Reachability reuses the rest system's navmesh search (`RestArea` and
  `FindRestDestination`), which follows connected triangles but not load doors.
  Up to four finds are measured a second, and each answer is reused for five seconds.
  Up to 16 spots are tried around each find, on the finder's side first. If the
  search hits its 2048-triangle limit without finding a spot, the result is
  unknown rather than unreachable, and the stall check decides.
- Two checks remain as backstops. On arrival, the finder must have a clear line
  from hand height to the find, so they never kneel at a wall. During the
  approach, four seconds without progress gives up.
- The navmesh search crosses doorways and gates whether they are open, closed
  or locked, and CommonLib does not expose their state (`BSPathingDoor`). A find
  behind a locked door therefore counts as reachable, and the finder gives up
  after those four seconds.
- Opening a find is detected from `TESActivateEvent`. Emptying it through a
  menu that does not activate the reference is caught by re-reading the find
  once a second.
- The final presentation lasts at most 15 seconds, then releases scouting control
  through the same cleanup as opening the find. The area stays marked as visited,
  so the finder does not immediately return to this chest or choose another nearby.
- Log lines start with `[Loot]`: which find was chosen, its total and best item
  value, how many finds were in view, the search idle, the stand-up, and why the
  finder left. `shows ... rejoining in 15s if left unopened` marks the final wait;
  `finished showing the find` marks its timeout.

## In-game checks still needed

Unit tests cover the rules and the visit timing. These need a playtest:

- The three search loops hold for the full time and the stand-up plays.
- Chests large enough to block the approach still leave the finder close enough
  for the rummage to line up.
- Finds behind walls, bars or ledges log `Skipped ... no walkable way to it` and
  are never approached. Reachable finds in the open are not wrongly skipped.
- The emblem sits above the head in first and third person, and fades in.
- Leave a find unopened after the scout stands: after 15 seconds the icon disappears
  and normal following/rest resumes. Opening it or moving away ends the wait sooner.

The chest emblem is "Locked chest" by Lorc from Game-icons.net (CC BY 3.0),
credited in `package/ThirdParty/GameIcons-NOTICE.txt`.
