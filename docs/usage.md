# Usage

## Requirements

Use the dependencies listed for your download. See release.md in the source,
or README.md in the mod archive. Command gestures have extra requirements below.

## Install or update

Close Skyrim. Replace the old mod in MO2 or Vortex and enable `Wayfarer.esp`.
Keep `SKSE/Plugins/Wayfarer.ini` to retain settings. Start the game through SKSE.
Do not rename the Wayfarer DLL, ESP, scripts or INI; saves and other mods use them.

## Party

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

Open the wheel with its assigned key. Use 1-6, arrows and Enter, or mouse clicks.
Escape closes it. On gamepad, hold LB and click the right stick; aim with the
right stick, confirm with A, cancel with B. Releasing the stick issues no order.

Two mouse controls sit below the wheel:

- **Teleport party** closes the menu and brings eligible travelling companions
  to clear nearby ground, including companions available from active follower
  slots in other cells. Waiting, excluded, combat and scene actors are skipped.
  The button shows the available count or why teleport is unavailable.
- **Temporary relaxation** toggles automatic rest when you stop and saves the
  preference immediately. Turning it off leaves the explicit **Relax** order
  available. This is the same setting as **Temporary rest** under Travel.

These controls have hover animations and an animated toggle. They do not add
gamepad bindings or change the six wheel directions. See
[Party Orders details](party-orders-utilities.md).

Change keys under Controls. Shift, Ctrl and Alt combinations work; matching is
exact. Escape cancels capture, Delete clears it. See hotkeys.md in the source.

## Travel and rest

Adjust spacing, catch-up speed and turn delay under Travel. The default turn
delay is 0.22 seconds. Companion individuality defaults to 1; use 0 for shared pace.

Toggle **Temporary rest** under **Travel > Rest & Sandbox** to enable or disable
automatic rest when you stop. This setting saves immediately and preserves the
existing `Sandbox/bAutomatic` preference (previously labeled "Relax when stopped").
Temporary rest ends when you leave its range. The **Relax** command works with
the toggle off and stays active until another order. Temporary-rest and resume
HUD icons are 75% larger and still follow your HUD scale setting.
Activities need nearby furniture, idles and connected navigation.
Stairs and ramps work within the height limit; load doors are not crossed.

Walking banter is on by default. Eligible pairs can talk while moving, but scenes
with their own packages or other restrictions keep control. No voiced lines are added.
Victory reactions are on by default after a fight with at least three enemies:
applause with empty hands, or a cheer with a drawn sword or greatsword.

## Gestures and hand holding

Command hand signals are optional. Third-person Lead, Companion and Rear clips
are included, with female variants. First-person clips come from First Person
Interactions. Install FPI, Open Animation Replacer and GP Offset Movement
Animation, then generate its behavior with Nemesis or a compatible Pandora setup.
Gestures skip busy states. Vanilla has no signal. Gesture Animation Remix is not needed.

Hand holding is experimental and off by default. Select one follower and use
Companion mode in third person. The selected follower approaches
your side through normal pathfinding. Pause to let them arrive, turn into alignment
and connect before walking. Offset movement then targets a position slightly behind.
Dialogue releases the grip and retains the existing dialogue grace. Sprinting
also releases it. Handholding adds no speed boost or player slowdown.
After a failed attempt, step away by over 150 units or toggle handholding to retry.
Revision 18 remembers finished script cleanup while waiting for the package
transition. Normal release has a three-second retry cooldown; failed setup
retains its eight-second cooldown and rearming requirement. OAR supplies a clasp
pose for the selected arms during offset positioning and holding. The local
test clips retain vanilla locomotion while changing the holding arm's rotation;
other animation replacers resume when the grip releases. OAR and matching pose
assets are required. Visual alignment still needs testing with the live actors.
See tether-r18-handoff.md for checks and limitations.
Physics, offset movement and the speed hook credit
[TETHER](https://github.com/Kei-BWV997/TETHER) by **KEI997** under MIT.

## Problems

Send your runtime version, follower mods, location and `Walk With Me.log`.
The log is in your game's SKSE log folder. Include video for animation problems.

Gesture setup: see docs/gestures.md in the source or GESTURES.md in the mod ZIP.
The GPMA patch is included. Let its files win over GPMA, then regenerate with your
existing Nemesis or Pandora setup. Do not replace another modlist's generated graph.
