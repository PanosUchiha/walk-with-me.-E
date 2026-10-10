# Command Gestures add-on: download and setup

## Download

For the 0.3.0 release, use the
[Walk With Me Files page](https://www.nexusmods.com/skyrimspecialedition/mods/191283?tab=files).
Download **Walk-With-Me-0.3.0-SE-AE.zip** from Main Files, and the matching
**Walk-With-Me-0.3.0-Command-Gestures.zip** from Optional Files once the release
has been uploaded. The Source ZIP is for developers, not a mod-manager install.
The add-on requires the main mod. It supplies third-person command signals and a
GPMA source extension, not the full GPMA mod or the optional hand-holding clips.

Use your existing Nemesis or Pandora setup. Installing a behavior engine alone
is not enough: regenerate after installing Walk With Me and its requirements.
The public packager now produces a separate `Command-Gestures.zip`. The base
SE/AE archive contains no GPMA patch or FPI-dependent signal configs. Install the
gesture add-on only when you want command signals and have their requirements.

## Install

1. Install [Open Animation Replacer](https://www.nexusmods.com/skyrimspecialedition/mods/92109),
   [First Person Interactions](https://www.nexusmods.com/skyrimspecialedition/mods/123129)
   and [Offset Movement Animation](https://www.nexusmods.com/skyrimspecialedition/mods/110408).
   Enable `FirstPersonInteractions.esp` and their required dependencies.
2. Install Walk With Me and its matching Command Gestures add-on. Let the add-on win file conflicts against Offset Movement Animation
   under `Nemesis_Engine/mod/gpma/0_master`. In MO2, put **Walk With Me - Command
   Gestures** below **Offset Movement Animation** in the left pane (higher file
   priority). This is a file conflict rule, not ESP load order. In Vortex, set the
   add-on to load after Offset Movement Animation for those conflicts and deploy.
3. Run your generator with the active mod list visible. For MO2, launch it through MO2.
   Keep your other required behavior patches enabled.

## Nemesis

Tick **GP Offset Movement Animation**, update the engine, then launch generation.
Enable the new output in your mod manager. See the
[GPMA author's steps](https://www.nexusmods.com/skyrimspecialedition/mods/110408).

Keep the generated output below the input animation/behavior mods in MO2 so its
generated HKXs win. Use your existing output mod and modlist generation procedure;
do not replace other patches with an output made for someone else's mod list.

OAR has no Nemesis checkbox. It reads the installed animation replacement configs
when Skyrim starts. There is no separate **Walk With Me** Nemesis checkbox either:
the add-on extends **GP Offset Movement Animation**.

## Pandora

Enable **GP Offset Movement Animation** and generate. Pandora reads Nemesis-format
patches, including this one. Direct output to a separate folder with `-o "path"`.
In MO2, enable that output mod. In Vortex, install the output as a mod and deploy.
See [Pandora setup](https://github.com/Monitor221hz/Pandora-Behaviour-Engine-Plus#quickstart).

There is no extra Walk With Me checkbox. The included files extend the GPMA patch.
Use the generator your list already supports; do not layer old Nemesis output over
new Pandora output, or the reverse. If your modlist has its own generation procedure,
follow it while keeping this patch visible.

## In game

Restart Skyrim through SKSE. In SKSE Menu Framework, open **Walk With Me > Controls
> Wheel & signals** and enable **Command hand signal**.
Enable Point for Lead and Invitation for Companion if you want those signals.
Try an order in third person with weapons sheathed, then while walking.
The legs should keep moving and the arms should return to rest.
First-person signals use the separately installed FPI clips.

## Patch and conflicts

The mod includes seven GPMA source files, built against GPMA 1.2. They add state 3
for Walk With Me's upper-body gestures. Full generated behavior HKXs are not shipped;
generate them for your own mod list. No separate patch download is needed.

The included patch was structurally checked against local Pandora output, including
the leg/root-motion layers. That does not confirm every animation or mod combination
in game. A fresh Nemesis generation has not been tested here. GPMA's author also
lists Nemesis and Pandora, with a note about testing Pandora 4.1.2.

If another mod edits the same GPMA files or claims state 3, those edits need a merge.
Do not assume choosing a file winner preserves both patches. FNIS alone does not
apply this Nemesis-format patch.

For missing gestures, check FPI/OAR requirements and that GPMA was enabled during
generation. For frozen legs or looping arms, check file conflicts and old output,
then regenerate. Send `Walk With Me.log`, the generator log and your behavior mods
if it still fails. Turn command signals off to keep using the follower features.

## Upgrading from the combined 0.2.0 download

Replace the old mod rather than merging the new base archive over it. Otherwise
the old `Nemesis_Engine/mod/gpma` files and `Walk With Me Signals` configs remain.
Install the matching gesture add-on only with the full GPMA and FPI mods enabled.
Regenerate behaviors after adding, replacing or removing the gesture add-on.
Turning off signals in the INI cannot hide installed files from Nemesis or OAR.

## Nemesis errors mentioning master.txt or 0_master

Record the full error code and path; these filenames alone do not identify one
cause. The old download installed only an extension to GPMA, not the complete
resource. Verify GPMA's `Nemesis_Engine/mod/gpma/info.ini` and its original behavior
nodes are visible to the generator. Do not create a dummy master.txt or copy
someone else's generated 0_Master.hkx. Replace the old package, install the full
requirements and the updated add-on, let the add-on win its seven conflicts, then
Update Engine and generate with GP Offset Movement Animation selected. For a
base-only install, remove the old WWM gesture files through the mod manager and
regenerate using your remaining behavior mods.

The updated patch preserves Havok indentation and multiline arrays for Nemesis's
line-based parser. A fresh Nemesis run and in-game playback still need validation.

## OAR errors and missing animations

- Unknown `WWM_PostCombatShoulderRecovery` or a **Walk With Me Victory** entry:
  the 0.3.0 packages contain neither. Replace the old mod and disable old overlays
  so their leftover victory configs are removed; do not edit their conditions to
  force them on. Post-victory reactions are shelved.
- `Walk With Me: Missing` or unknown clasp conditions: check that both Wayfarer.dll
  and OpenAnimationReplacer.dll loaded. Look in skse64.log,
  OpenAnimationReplacer.log and Walk With Me.log for OAR interface/registration
  errors. The DLL requests interface V3, then V2 for older OAR builds.
- Keep `requiredPlugin: "Walk With Me"`. This is the registered SKSE condition
  provider name, not `Wayfarer.esp`. Replacing it with the ESP filename breaks lookup.
- Detection Plugin is a separate OAR extension. None of WWM's shipped configs
  use its conditions. Install it if the exact OAR error names it or another
  installed animation pack requires it; it cannot register WWM's own conditions.
- Signals need `FirstPersonInteractions.esp` enabled, OAR loaded, the gesture
  add-on installed and freshly generated GPMA behavior. In the OAR menu, look for
  **Walk With Me Signals**. Signal conditions normally
  evaluate false until the relevant order is issued.
- No clasp animations in OAR: check that **WWM Clasp** from the main download is
  installed and not overridden by an older, locally generated clasp mod.
