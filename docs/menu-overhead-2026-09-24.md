# Follower icons and settings — September 24, 2026

The current working tree retains the existing hand-holding, rest-area activities,
inn/table behavior, loot scouting and teleport work. Victory reactions remain
disabled. This change does not alter dialogue eligibility or resolve the existing
Katana menu-opener finding in the September 23 handoff.

## Overhead icons

Chat and loot emblems pop from 65% size with a small overshoot over 0.42 seconds,
rise into place, and gently float. Settled opacity is about 80%, with a soft shadow
for contrast. Existing ivory chat and white/gold loot artwork is retained.

Animation uses render-frame time and separate actor-keyed state for chat and loot.
Reordering marker snapshots cannot transfer or restart a different companion's
animation. Missing or hidden markers are discarded immediately; returning markers
get a fresh entrance. Game objects and head projection remain on the game thread;
the render snapshot adds only a numeric actor ID. Menus, visibility and existing
eligibility rules still hide markers.

**Interface > Follower icons** contains:

- **Show chat icons**: existing `[Interface] bRestDialogueIcons` setting.
- **Animate chat icons**: new `[Interface] bAnimateChatIcons`, default `true`.
  Turning this off leaves a still, translucent chat icon.
- **Show loot icons**: existing `[Loot] bIcons` setting. Loot scouting itself is
  enabled on the Loot page; chat animation does not affect loot animation.

## Menu organization

The user requested SceneDirector's density and organization, with Walk With Me's
yellowish palette preserved. Settings use the framework's current font at 0.85
scale, SceneDirector's 6/3 item spacing, 4/2 frame padding and 6/2 cell padding.
No font or global framework preference is replaced. Child window scale and style
stacks are restored when each page ends.

| Page | Contents |
| --- | --- |
| Party | Enable switch, highlighted current order, roster, enrollment |
| Travel | Formation, catch-up/teleport, collapsed movement tuning and hand holding |
| Rest | Automatic rest/radii, collapsed timing, activities, party life |
| Loot | Enable switch, finder, categories, collapsed search tuning |
| Interface | Follower icons, order emblem, collapsed diagnostic logging |
| Controls | Key bindings, gamepad wheel, command gestures |

Boolean groups use adaptive two-column layouts, or three columns for the short
follower-icon options. Narrow panels fall back to one column. Sliders and selectors
use borderless proportional rows; tooltips wrap. Duplicate order radio controls
were removed. Existing autosave/game-thread application remains in use.

The Travel reset preserves the Rest page's automatic-rest delay. Loot tuning reset
changes only its numeric tuning, preserving the finder, enablement, categories and
icon preferences. Existing personal INI values are preserved during installation;
the new animation key falls back to enabled until settings are next saved.

## Framework reference and checks

- Local design reference: `../SD/src/Menu/Settings.cpp`.
- [SKSE Menu Framework 3 documentation](https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/master/README.md):
  registered sections, HUD callback, SVG textures and exported ImGui API.
- `tools/verify-menu-exports.py`: all 110 used API exports found in the installed framework.
- Both `build/manual` and `build/modern`: build and 24 CTest suites passed, including
  per-actor animation lifecycle, hidden-marker reset and 30/144 FPS equivalence.
- Actual `src/config.cpp` compiled into an isolated save/reload check: legacy INI
  fallback and animation on/off roundtrips passed without changing the other preferences.
- Offline ImGui rendering extracted the actual settings functions and used the
  installed framework font. All six pages and a narrow Interface page were inspected;
  style/color stacks balanced. Images and the one-off harness are under
  `build/settings-visual-proof`, with build/check logs under `build/menu-overhead-*`.

The offline renders use sample followers and do not establish in-game head tracking
or appearance. Remaining playtest: chat/loot pop-in and floating, chat-animation
toggle, menu transitions, off-screen/occluded followers, and narrow framework windows.

Pre-edit source snapshot: `backups/menu-overhead-source-20260924-001147`.
The local installation backup and hashes are recorded separately under
`backups/menu-overhead-preinstall-*`. No commit, push or public release is made.
