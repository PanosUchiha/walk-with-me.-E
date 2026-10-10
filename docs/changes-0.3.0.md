# Walk With Me 0.3.0

Changes since **0.2.5**.

## Changed

- **Weapons out no longer breaks the formation.** Drawing a weapon, yours or a
  companion's, and the game's combat flag only release the party while enemies are
  actually fighting you or a companion. Walking around with your sword out keeps
  everyone in formation; a real fight still hands companions back to their own combat
  AI at once. The `[Safety]` switches in the INI work as before.

## New

- **Battle plan tile** on the Party actions page, for the separate
  **Fight With Me** mod, which gives
  companions roles, a focus and a place in a fight. With Fight With Me installed the tile
  opens its battle plan. Without it the tile is dimmed and reads "Needs Fight With Me";
  Walk With Me does not need Fight With Me for anything else, and nothing else changes.
- The Party actions page is now two even rows of four, so D-pad up and down move
  straight between columns. Number keys 1-8 pick a tile.

## Fixes

- **SKSE Menu Framework 3 not found** although it was installed: Walk With Me now asks
  the loaded framework for its version directly instead of checking a file path that can
  fail while the DLL is loaded. The log names the reason when it really is missing.
