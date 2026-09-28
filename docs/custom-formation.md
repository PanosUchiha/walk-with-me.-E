# Custom formation

Added 2026-09-26. Players draw their own formation. It becomes the seventh order on the
party wheel, **Custom**. The wedge sits lower-right, between Rear and Vanilla, and is
dimmed ("Draw one in Travel settings") until a formation has been saved.

## Name and emblem

In **Travel > Custom formation**, set **Name** and **Emblem** before opening the
editor. The name appears on the wheel, the party menu's active-order label, the
orders HUD, the travel-style list and the editor. Blank names return to **Custom**.
The limit is 24 UTF-8 bytes: up to 24 ASCII characters, fewer for some other scripts.

The emblem picker offers 16 Game-icons designs: Band (the default), Banner, Crown,
Castle, Swords, Dragon, Wolf, Raven, Stag, Steed, Pine, Rune, Watch, Moon, Calm and
Tavern. The selected emblem appears in the settings group, wheel and orders HUD.
These emblems are distinct from the mod's other order and action icons. Artwork
ships as `Interface/WalkWithMe/crest-<key>.svg`, with credits in
`ThirdParty/GameIcons-NOTICE.txt`.

Saving places from the editor preserves the latest name and emblem stored in
settings, including after using **Start from** or **Clear**.

## The editor

Open it from **Travel > Custom formation > Create formation / Edit formation** in the SKSE
Menu Framework settings. It needs a loaded game, because it lists your party. The editor is
a window of its own, over the settings, in the party menu's style.

**The map.** It is seen from above, with you in the middle and the way ahead at the top.
The dashes of the line of travel drift forward. Rings mark 2, 4, 6 and 8 metres; one metre
is 70 units.

- Click the map to add a place. A ghost ring shows where it will go.
- Drag a place to move it.
- Right-click it, or select it and press Delete, to remove it.
- Arrow keys nudge the selected place one grid step.

Places snap to a 25-unit (35 cm) grid unless **Snap** is off. They cannot sit on your own
footprint (75 units) or closer than 55 units to each other, and they reach at most 8 m.
There are up to ten places, numbered in the order you add them. Removing one renumbers
the rest.

**Who stands where.** Select a place and choose a companion under **Who stands here**, or
leave it as *Anyone*. A companion reserved again somewhere else leaves their old place.

**Start from** fills the map with the first places of Natural, Lead, Companion or Rear, one
for each companion in your party (at least three). **Mirror** copies the selected place to
the other side. **Flip left and right** mirrors everything. **Clear** starts over.

If your party is larger than the formation, dotted rings show where the extra companions
fall in. The footer has **Close**, **Save** and **Save and use** (saves, switches the party
to Custom, and closes). Escape closes without saving.

## In the world

`FormationMode::kCustom` (6) uses `Custom::RolePosition`:

- Placement is exact: no personal jitter, no preferred-side mirroring and no sociable pull.
  The player placed everyone.
- Companions reserved for a place take it. Everyone else fills the remaining places in the
  player's numbering: free places first, then places kept for companions who are not
  present, then fall-in spots behind the rearmost place. Within that set they take the
  nearest free place, as in the other styles.
- Interiors compress the formation as they do the others (55% across, 65% along), and
  spacing still applies.
- Selecting Custom with none saved is refused with a notice. Clearing the formation while
  in Custom returns the party to Natural.

## Storage

`Wayfarer.ini`:

```
[CustomFormation]
sTitle = Custom
sEmblem = band
iPositions = 3
fPosition1Side = -150.0      ; + right of the player
fPosition1Ahead = 100.0      ; + ahead
sPosition1Companion = katana.esp|005900   ; optional; HandHolding::ReferenceKey
sPosition1Name = Katana
...
```

Loading sanitizes the section: the count is clamped, bad numbers are dropped, and places
out of reach or on top of each other are fixed or removed. `iFormationMode = 6` falls back to
Natural when no formation exists.

Existing formations without the new keys keep their places and use **Custom** and
**Band**. Unknown emblem keys also fall back to Band. Saving settings writes the
current name and emblem alongside the places; no manual migration is needed.

## Code

- `include/custom_formation.h`: names, emblem keys, placing, removing, mirroring, reserving, sanitizing,
  `RolePosition`, `ReservedRoles` and `OpenRoles`. Tested in `WayfarerCustomFormation`,
  along with the planner and the seven-way wheel. `WithPlaces` merges editor geometry
  without replacing the latest configured name and emblem.
- `src/travel_planner.cpp`: `SetCustomFormation`, `CustomFormation()`, and
  `FormationPosition(kCustom, role)`.
- `src/formation_controller.cpp`: mode gating, and reserved/open role assignment in
  `UpdateTravelRoles`.
- `src/menu_ui.cpp`: the `FORMATION_EDITOR` block (rendered by the offline preview), the
  Travel settings group, and the dimmed wedge.
- `tools/build-svg-assets.py`: `SECTORS = 7` regenerates `wheel-slice.svg` for the
  seven-way wheel.
