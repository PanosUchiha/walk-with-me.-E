# Walk With Me 0.2.5

Changes since **0.2.2**. There was no 0.2.3 or 0.2.4.

## New

- **Custom formation.** Draw your own formation under Travel > Custom formation and
  give it a name and one of 16 emblems. It becomes a seventh order on the party wheel.
- **Pathfinder.** While you travel in Natural, Lead, Companion or Custom, a companion
  spots places you have never found. They wave, point the place out and lead you there,
  stopping to wait when they get too far ahead. It is on by default. Its settings are on
  the new **Roles** page, which replaces the Loot page; the loot settings are there too.
- **Party actions page.** Teleport party, Temporary rest and Companion control moved off
  the wheel onto a second page. It also has quick switches for Walk With Me, Hold hands,
  Loot scouting and Pathfinder. Open it with D-pad down, Down, Tab or the bar under the
  wheel.
- **Hold hands hotkey** under Controls (unbound by default).
- Sociable companions walk about 30% closer to you.

## Fixes

- **GOG:** the mod now loads on GOG 1.6.1179 and 1.6.659. GOG 1.6.1170 is refused with
  a message to update the game.
- **"Could not save settings" under MO2** when mods and the overwrite folder are on
  different drives. Every change failed, so the mod seemed to do nothing.
- **Adding companions** works in every travel style, including Vanilla and with Walk With
  Me switched off. Recruited followers can be added from anywhere. A companion who briefly
  stops following, for example during a quest scene, is paused and resumed instead of
  being dropped.
- Companions no longer swap places every second while you walk.
- Companions no longer eat in town streets, temples, shops or dungeons. Meals happen at
  inns, homes, halls and campfires in the wilds. A temporary stop keeps to short standing
  poses.
- Less background script work while travelling.

## Catch-up and NFF

- Teleport catch-up works while you sit, craft or have movement locked. Seated or
  sleeping companions are no longer left behind.
- **Leave relaxing companions in place** (Travel, on by default): a Relax camp stays put
  however far you go.
- **Take over NFF's catch-up** (Travel, on by default): with NFF installed, Walk With Me
  does the teleporting, so NFF no longer empties Relax camps or races it. NFF's own
  setting is restored before every save and while its menu is open, so uninstalling Walk
  With Me leaves NFF as it was.
- Fixed campers appearing beside you after a load door, then walking back to camp.

## Hand holding (experimental, off by default)

- The 68 TETHER clasp animations are now included, so there is nothing to generate.
  Remove any clasp mod you generated for an earlier version. Clasp mods made for
  0.16.8 never loaded their poses at all.
- **Classic:** a new interlocked clasp. After the hands connect, the game walks the
  companion beside you instead of snapping them into place every frame, which caused
  the flicker. New **Hand spacing** and **Companion side** settings. You and the
  companion no longer push each other while holding hands.
- **TETHER:** the companion keeps facing the right way, recovers when their AI changes
  or they stall, and no longer stands still after the hands connect.

## Updating

Close Skyrim and **replace** the old main mod instead of merging over it. Keep
`SKSE/Plugins/Wayfarer.ini` if you want your settings, and enable `Wayfarer.esp`. The
Command Gestures add-on's animations and patch are unchanged since 0.2.2, so there is
no need to regenerate behaviors.

The new features have had limited in-game testing, and GOG has not been tested in
game. See [Validation](validation-0.2.5.md).
