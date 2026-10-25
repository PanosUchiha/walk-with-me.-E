# Walk With Me 0.3.1 validation

Release preparation: October 22, 2026. Compared against 0.3.0.

## Build and package checks

- Both legacy and modern Release builds succeeded with all 27 CTest suites passing.
  The public package uses the modern CommonLibSSE-NG 7.5.1 DLL.
- The ESP was regenerated. Byte for byte it differs from 0.3.0's only in the PKDT
  Interrupt Override of its 60 packages, 255 before and 0 now. All 5,961 packages in
  Skyrim.esm use 0 (None) or 4 (Combat) there.

## Combat release

- A companion in combat is rejected as "in combat" again regardless of what enemies
  are doing, which is the 0.2.5 rule. The player's own combat flag and drawn weapons
  still release the party only while enemies are engaged, as in 0.3.0.
- The engaged check is still sampled every 250 ms; a positive result now holds for
  3 s. It is reset on game load.

## Hand holding

- The Hold hands tile is enabled whenever a game is loaded and no other toggle is
  saving. With no partner it picks the crosshair companion if they are in the party,
  otherwise the nearest party member with a persistent reference.

## Scope

Build and offline checks only. None of these fixes has been played in game yet: the
combat release, the hand-holding picker and the regenerated ESP need a playtest on
SE 1.5.97 before release. The 1.6, GOG and 1.7 paths still need in-game testing.
Known issues from 0.3.0 remain: gamepad button releases, including LB, are swallowed
while the party menu is open.
