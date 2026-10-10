# Usage

## Requirements

Use the dependencies listed for your download. See release.md in the source,
or README.md in the mod archive. Command gestures have extra requirements below.

## Install or update

Close Skyrim. Replace the old mod in MO2 or Vortex and enable `Wayfarer.esp`.
Keep `SKSE/Plugins/Wayfarer.ini` to retain settings. Start the game through SKSE.
Do not rename the Wayfarer DLL, ESP, scripts or INI; saves and other mods use them.

## Party

During temporary rest or manual Rest / Relax, a small chat bubble marks managed
companions with unheard conversation options. Trading, NFF and other follower
commands are filtered. It disappears when rest ends. Under **Interface > Follower
icons**, toggle **Show chat icons** or turn off **Animate chat icons** to keep a
still, slightly transparent emblem. See [detection details](rest-dialogue.md).

The settings pages are **Party**, **Travel**, **Rest**, **Loot**, **Interface**, and
**Controls**. They retain Walk With Me's yellowish/ivory palette with SceneDirector's
0.85 font scale, compact spacing and aligned setting rows. Checkbox groups use
two or three columns when space permits and fewer in narrow panels. Advanced movement,
rest timing, loot tuning and hand-holding settings are collapsible.

The supplied Walk With Me artwork sits behind every settings page in the
bottom-right corner at 20% opacity. It keeps its proportions, scales with the
panel and stays in place while the settings scroll. The PNG is installed as
`Interface/WalkWithMe/menu-background.png`.

Automatic enrollment is off by default. Aim at a companion and press **G**, then
choose **Travel together**, or use Party Add for a recruited follower. The party
limit is 10. Existing explicit additions remain saved; no new game is needed.

Open Walk With Me in Menu Framework to manage Party. **Remove** releases control
and prevents automatic re-addition without dismissing them. **Add** allows them
back. These choices are saved. This upgrade turns automatic enrollment off once,
correcting the earlier automatic default. You can opt in under **Automatically
add followers** afterward; that choice survives reloads. Other preferences remain.

With NFF installed, ordinary followers must be in its active roster. Home residents
and dismissed followers stay unmanaged. Known independent companions such as
Serana and Inigo retain their separate recruitment checks. Unknown private
recruitment systems can use the explicit **Companion control** popup below, an API
integration or a dedicated detector. Ordinary Party Add uses the same recruitment
evidence as automatic discovery.

## Custom companion control

Aim at a nearby companion and press **G** to open their compact control panel.
The name stays locked and the world pauses while the panel is open.
Click **Travel together** to apply
your existing formation and rest orders. There is no nearby-NPC dropdown and no
fallback to another NPC when nothing is targeted. Rebind G under Controls.

The command wheel's **Companion control** button (keyboard **C**, gamepad **X**)
and Party page remain entry points. Enter / gamepad A invokes the available action;
Escape / gamepad B or **Back** closes the panel.

Use **Release** in the same panel to return their own AI and prevent
automatic re-addition. Imported companions also have a **Control** button on the
Party page when listed. Choices are saved per game.

This explicitly overrides enrollment requirements for the selected NPC; it does
not recruit them into NFF or change their follower quests. Recruit/dismiss through
their original dialogue. WWM releases detectable dismissals; followers with hidden
or stale recruitment state need manual Release. Wait, combat and scene guards
still apply. See [details and test steps](package-control.md).

**Enforce custom followers** is on by default. Disable it to let their own AI take
over, or exclude one follower. NFF enforcement has a separate setting. Wait orders,
dismissal, combat and scripted scenes still take priority.

The targeted panel also offers **Unstick** (U / gamepad X) to gently push a frozen
companion after the menu closes. **Cancel** (Esc / gamepad B) dismisses the panel.

## Orders

- Natural: followers choose their positions.
- Lead: walk ahead.
- Companion: walk beside you.
- Rear: follow behind.
- Relax: stay near the rest point until another order.
- Vanilla: return control to Skyrim or your follower framework.
- Custom: the formation you drew under Travel > Custom formation. The wedge stays dimmed
  until one is saved. See [custom-formation.md](custom-formation.md).

Sociable companions walk about 30% closer to you than their place in Natural, Lead,
Companion and Rear, but never inside 95 units. In Natural, Lead and Companion, the
**Pathfinder** option has a companion point out places you have never found; see
[pathfinder.md](pathfinder.md).

Open the wheel with its assigned key. Use 1-6, Left/Right and Enter, or mouse clicks.
Escape closes it. On gamepad, hold LB and click the right stick; aim with the
right stick or D-pad left/right, confirm with A, cancel with B. Releasing the stick
issues no order.

**Party actions** is a second page under the wheel. Reach it through the
translucent bar at the bottom, D-pad down, the Down arrow, Tab or the mouse wheel.
Go back with the bar at the top, D-pad up from the top row, Tab, the mouse wheel,
Escape or B. It is two rows of four tiles in the wheel's style with yellow accents:

- **Teleport party** closes the menu and brings eligible travelling companions
  to nearby ground, including companions available from active follower slots
  in other cells. It works in combat, on horseback and while you sit. Waiting,
  excluded, scripted and riding companions stay put, and the notification names
  them. The tile shows the available count or why teleport is unavailable.
  See [teleport.md](teleport.md).
- **Temporary rest** toggles automatic rest when you stop and saves it at once.
  The explicit **Relax** order stays available.
- **Companion control** opens the take/release panel for the companion you were
  looking at when you opened the menu (also C, or X on gamepad, from either page).
- **Hold hands**, **Loot scouting**, **Pathfinder** and **Walk With Me** switch those
  features on or off and save immediately, like their settings.
- **Battle plan** opens Fight With Me's battle plan, where each companion gets a role,
  a focus and a place in a fight. It needs the separate Fight With Me mod; without it
  the tile stays dimmed and says so, and nothing else changes.

Choose with the mouse, arrows or D-pad; select with a click, Enter, A or 1-8. A
line under the grid explains the focused tile, and an unavailable tile says why.
Tiles rise in one after another, a yellow frame glides to the focused tile, and
the switches animate. See [party-actions-grid.md](party-actions-grid.md).

Change keys under Controls. Shift, Ctrl and Alt combinations work; matching is
exact. Escape cancels capture, Delete clears it. See hotkeys.md in the source.

## Travel and rest

Adjust spacing, catch-up speed and turn delay under Travel. The default turn
delay is 0.22 seconds. Companion individuality defaults to 1; use 0 for shared pace.

Toggle **Temporary rest** under **Rest > Rest & exploration** to enable or disable
automatic rest when you stop. This setting saves immediately and preserves the
existing `Sandbox/bAutomatic` preference (previously labeled "Relax when stopped").
Temporary rest ends when you leave its range. The **Relax** command works with
the toggle off and stays active until another order. Temporary-rest and resume
HUD icons are 75% larger and still follow your HUD scale setting.
As soon as rest starts, companions set out for seats, idle markers or a patch of
open ground of their own, spreading across a 1600-unit area by default. From time
to time two of them pair up and talk for two to three minutes; if they are apart,
one walks over first. See [rest dispersal](rest-dispersal.md).
With **Area activities** on, the place shapes all of it: fires, shrines, shores and
lookouts to walk to, place-specific poses, hushed talk in caves, and at an inn a
shared table with a server called over for drinks. See [area activities](area-activities.md).
Activities need nearby furniture, idles and connected navigation.
Stairs and ramps work within the height limit; load doors are not crossed.

Walking banter is on by default. Eligible pairs can talk while moving, but scenes
with their own packages or other restrictions keep control. No voiced lines are added.
Post-victory reactions and their settings are not included in this release.

## Loot scouting

Under **Loot > Loot scouting**, choose a **Loot finder**, or leave it on **Any
companion** to use whoever is nearest. The finder looks over the chests, bodies
and valuable loose items in view and walks to the richest one. They kneel to
search it for **Search for** seconds (12 by default), then wait beside it for up to
15 seconds before resuming normal following/rest. A chest
appears above their head: white for ordinary loot, gold when one item is worth
more than the **Valuable above** value (1000 gold by default). They leave when
you open the find or move beyond **Leave when you are beyond**. After one find
they stop searching until you move on. Only one companion goes at a time.
Scouting is off inside houses, inns, temples, shops and castles unless you enable
**Inside homes and inns**. See
[loot scouting](loot-scouting.md).

## Gestures and hand holding

Command hand signals are optional. Third-person Lead, Companion and Rear clips,
with female variants, come in the separate **Command Gestures** add-on.
First-person clips come from First Person Interactions. Install the matching
add-on, FPI, Open Animation Replacer and the full GP Offset Movement Animation
mod, then generate its behavior with Nemesis or your compatible Pandora setup.
Enable **Controls > Wheel & signals > Command hand signal**. See
[Gesture setup](gestures.md) for download links and mod-manager order.
Gestures skip busy states. Vanilla has no signal. Gesture Animation Remix is not needed.

Hand holding is experimental and off by default. Select one follower and use
Companion mode in third person. Choose **Classic** in Travel's
**Hand-holding method** selector for the earlier procedural hand holding without
clasp clips, or keep **TETHER** for the physics grip. Switching releases
the current grip before reconnecting. Classic also offers **Hand spacing** and
**Companion side**. Bind **Controls > Hold hands** to turn hand holding on and off
without opening the menu. See [hand holding](hand-holding.md).

With TETHER, the follower approaches your side through normal pathfinding.
Pause to let them arrive, turn and connect. The hands connect before movement
handoff; script cleanup still gates subsequent offset movement. Dialogue and
sprinting release the grip. Hand holding adds no speed boost or player slowdown.
TETHER requires OAR (its clasp poses are included); Classic does not.
See [hand holding](hand-holding.md) for setup, tuning and current limitations.
Physics, offset movement and the speed hook credit
[TETHER](https://github.com/Kei-BWV997/TETHER) by **KEI997** under MIT.

## Problems

Send your runtime version, follower mods, location and `Walk With Me.log`.
The log is in your game's SKSE log folder. Include video for animation problems.

Gesture setup: see docs/gestures.md in the source or GESTURES.md in the mod ZIP.
The GPMA patch is in the optional Command Gestures download. Let its files win over GPMA, then regenerate with your
existing Nemesis or Pandora setup. Do not replace another modlist's generated graph.
