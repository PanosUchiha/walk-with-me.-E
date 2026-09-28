# Hotkeys

In Controls, click a binding, hold Shift/Ctrl/Alt and press a key. Either side works.
Escape cancels; Delete clears. A modifier by itself can also be bound.

**G** opens Companion control for the NPC under your crosshair. It does nothing
to another nearby NPC when no actor is targeted. The panel pauses the game. Within it, Enter / gamepad A uses the available travel
action, U / gamepad X applies Unstick, and Esc / gamepad B selects Cancel.

Matching is exact: G and Ctrl+G are different. Assigning an existing chord clears
its old action. Settings save immediately. Other mods handle conflicts separately.

## INI

| Key entry | Modifier entry |
| --- | --- |
| iCommandKey | iCommandModifiers |
| iCompanionKey | iCompanionModifiers |
| iToggleKey | iToggleModifiers |
| iCycleModeKey | iCycleModeModifiers |
| iScoutKey | iScoutModifiers |
| iRearKey | iRearModifiers |
| iRoamKey | iRoamModifiers |
| iReloadKey | iReloadModifiers |
| iHandHoldKey | iHandHoldModifiers |

**Hold hands** (unbound by default) turns hand holding on or off and saves the
setting. It needs a hand-holding companion chosen in Travel settings, and the
grip only forms in Companion mode.

Masks: 0 none, 1 Shift, 2 Ctrl, 4 Alt. Add values for combinations.
For Ctrl+G, use iCommandKey=34 and iCommandModifiers=2.
Missing or invalid masks become 0. An unbound key (-1) clears its mask.

Test both keyboard sides, capture/cancel, duplicate bindings, menus and save/reload.
The wheel uses its full chord to open and close.

Inside the party menu (2026-09-26): on the wheel, Left/Right or D-pad left/right turn
it and Down, Tab or D-pad down opens **Party actions**. On that grid, arrows or the
D-pad move; Up or D-pad up from the top row, Tab, Escape or B go back to the wheel.
Escape and B close the menu only from the wheel.
