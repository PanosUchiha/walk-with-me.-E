# Party actions grid

Added 2026-09-26. The party menu now has two pages. The wheel has seven orders,
including the named custom formation.
The actions that used to sit under it (Teleport party, Temporary relaxation and the
Companion control button) moved to a second page with quick toggles.

## Layout

The window keeps its size and position, so the wheel never moves on screen.

| Page | From the top |
| --- | --- |
| Wheel | Title, "Party orders", the wheel, the active order, then a translucent bar with two chevrons pointing down: **Party actions** |
| Grid | Title, "Party actions" (yellow), a translucent bar pointing up: **Party orders**, four party-action tiles above three centred role tiles, then a line explaining the focused tile |

Hints for the current input device sit at the bottom of both pages.

The seven tiles, in order:

1. **Teleport party**: the old button. The caption shows how many are ready, or why not.
2. **Temporary rest**: the old toggle, with a switch.
3. **Companion control**: opens the panel for the companion under the crosshair when
   the menu opened; the caption names them. Unavailable when nobody was targeted.
4. **Walk With Me**: `[General] bEnabled` (the F10 toggle).
5. **Hold hands**: `[HandHolding] bEnabled`. The caption names the partner and notes
   that it only runs in Companion mode.
6. **Loot scouting**: `[Loot] bEnabled`. The caption names the finder.
7. **Pathfinder**: `[Pathfinder] bEnabled`. The caption names the selected companion
   or says **Curious first**. In styles other than Natural, Lead, Companion and Custom, it
   says **Not in this style**; the preference can still be toggled. Companion choice,
   wave, notice and tuning are in **Roles > Pathfinder**.

The first four fill the top row. Hold hands, Loot scouting and Pathfinder form the
centred bottom row. All seven tiles have the same size.

The toggles save through `ApplyPreferences` like their settings, one at a time.

## Look and motion

The tiles use the wheel's surfaces, dark teal slate with gold hairlines. The page's
accent is yellow `(240, 199, 74)` in place of the wheel's muted gold.

- **Page change.** The page eases over about a quarter of a second (`ActionGrid::Motion::page`,
  follow rate 11). The wheel lifts up and clears in the first 65% of the move. The grid
  rises 5% of the window width and fades in over the last 80%, so the two never overlap
  at half strength.
- **Reveal.** Tiles rise and fade in one after another, 35 ms apart, each time the grid
  is entered.
- **Focus.** Yellow corner brackets glide to the focused tile (follow rate 20). The
  tile lifts slightly and warms to a dark gold. A soft pool of light sits behind its
  icon, which turns yellow and plays its own motion: the teleport bob, the flame, the
  chest hops, the sighting ring turns into place, the rings slide together and
  interlock, and the power ring draws itself in.
- **Selecting.** The tile flashes, and switches slide between grey-left and yellow-right.
- **Unavailable** tiles dim to 42% and put the reason in their caption. They can still
  be focused, so the reason can be read.
- **Page bars.** Translucent yellow pills with fine rules and two chevrons that drift
  in the direction the bar leads. They brighten on hover.

Companion control, Hold hands and Walk With Me have no artwork of their own. They use
line glyphs drawn from primitives (`OrderVisuals::DrawGlyph`), stroked at the same
weight. Pathfinder uses Lorc's compass from Game-icons as
`Interface/WalkWithMe/pathfinder.svg`, replacing the earlier walking-scout figure.

## Input

| | Wheel page | Grid page |
| --- | --- | --- |
| Move | Stick, D-pad left/right, Left/Right, mouse | Right stick, D-pad, arrows, mouse |
| Select | A, Enter, click, 1-7 (orders) | A, Enter, click, 1-7 (tiles) |
| Change page | D-pad down, Down, Tab, wheel down, the bar | D-pad up from the top row, Up from the top row, Tab, wheel up, the bar, B, Escape |
| Close | B, Escape, the menu key | the menu key |
| Companion control | X, C | X, C |

`HandleInput` collects D-pad presses in `padDx`/`padDy`. The render loop reads them
by page. Left and right stay in the current row and stop at its ends. Up and down
select the nearest tile by horizontal centre in the other row, choosing the left
tile on a tie. Up from the top row returns to the wheel. On the grid, the right
stick steps like the D-pad (`ActionGrid::StickSteps`): a
firm push moves one tile, and holding repeats after 0.42 s, then every 0.16 s. After a
page change the stick is ignored until it recentres, so the push that aimed the wheel never
lands on a tile, and the one that left the grid never re-aims the wheel. While the grid shows, `gridBackEnabled` turns Escape into "back" instead of
"close".

## Code

- `include/action_grid.h`: tile boxes, the two bars, D-pad movement (`Move`, where
  up from the top row returns -1 for "back") and `Motion`. It is pure and tested in
  `WayfarerCatchup` for layout at four resolutions, navigation edges, and settle time
  and frame-rate independence at 30/60/144 Hz.
- `src/menu_ui.cpp`: `PartyHeader`, `PageBar` and `ActionGridPage` sit between the
  `PARTY_HEADER`/`ACTION_GRID` markers. `RenderCommands` builds the tiles from the
  party view and runs both pages.
- `src/order_visuals.cpp`: `Icon` has a tint parameter, and `DrawGlyph` draws the new
  glyphs.

## Previews

`tools/preview-order-wheel.py` compiles the marked blocks from `menu_ui.cpp`, and all
of `order_visuals.cpp`, against a real ImGui. It drives them through a scripted
timeline and rasterizes the triangles. The timeline opens the wheel, goes down to the
grid, glides across and down, switches loot scouting off, and goes back up. The tool
also clicks every tile through the real code: a click selects once, dragging off
cancels, and a disabled tile can be focused but not used.

```
python tools/preview-order-wheel.py --cmake <cmake.exe> --edge "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" --stills --gif
```

This writes `build/wheel-visual-proof/party-*.png` and `party-menu.gif`. The
rasterizer does not antialias geometry beyond what ImGui's fringe triangles provide,
and fonts come from Segoe UI plus the packaged inscription font. The previews show
the real layout, colours and motion; they are not an in-game capture.

The September 26 preview supports all seven tiles, a custom formation name and
emblem, and a gallery of all 16 emblems. The combined reviewed still is
`build/wheel-visual-proof/roles-custom-review.png`; the individual Pathfinder
grid, custom wheel and emblem gallery are `party-grid-pathfinder.png`,
`party-wheel-custom.png` and `formation-emblems.png` in that directory.
