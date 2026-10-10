# Walk With Me 0.3.0 validation

Release preparation: October 10, 2026. Compared against 0.2.5.

## Build and package checks

- Both legacy and modern Release builds succeeded with every CTest suite passing. The
  public package uses the modern CommonLibSSE-NG 7.5.1 DLL.
- All seven Papyrus scripts compiled, and the ESP was regenerated.
- The animation packaging tests passed, and the packager checked DLL version 0.3.0,
  the runtime declarations, the clasp clips and configs and the approved animation
  hashes, as for 0.2.5.

## Without Fight With Me

Walk With Me does not link against Fight With Me. The Battle plan tile looks up
`FightWithMe.dll` and its `FightWithMe_OpenBattlePlan` export by name each time the
party actions page is drawn, until it is found:

- Not installed: the tile is dimmed, reads "Needs Fight With Me", and cannot be
  clicked or chosen with Enter, A or its number key. Nothing else reads the lookup.
- Installed: the tile opens the battle plan. If Fight With Me reports that it could
  not open it, Walk With Me logs a warning; Fight With Me shows its own message.

The grid navigation tests cover the new 4 x 2 layout: up and down move between
columns, edges stop rather than wrap.

## Formation with weapons out

Combat and drawn weapons now release the party only while an enemy within 6000 units
is targeting the player, a teammate or a managed companion. This change was installed
and in use on SE 1.5.97 throughout Fight With Me's development, but has not had a
playtest of its own.

## Scope

These are build and offline checks plus development play on SE 1.5.97, not a full
playtest of the packaged build. The 1.6, GOG and 1.7 paths still need in-game testing.
Known issues from 0.2.5 remain: gamepad button releases, including LB, are swallowed
while the party menu is open.
