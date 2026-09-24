# Walk With Me 0.2.2 — release post

Publisher note: this is prepared copy. Upload the three matching archives before
posting. The optional download described below is not published by packaging.

## What's changed since 0.2.0

- Animated, translucent conversation and loot icons with a pop-up entrance.
  Chat animation can be switched off separately in Interface settings.
- A compact six-page menu with the original yellow/ivory colors, responsive
  checkbox columns and faint Walk With Me artwork in the bottom-right corner.
- More rest activities around fires, shrines, shorelines, lookouts and inns;
  fixes for companions returning to their original stopping point after roaming.
- A loot finder who points out reachable valuables, searches briefly and waits
  beside the find for up to 15 seconds, then resumes normal following/rest even
  if you leave it unopened. Category and distance controls are on the Loot page.
- More reliable automatic catch-up across travel states and cells, with landing
  choices along your recent path. Party teleport reports skipped companions.
- Classic hand holding restored as the default method, plus improvements to
  TETHER setup and cleanup. Hand holding remains experimental and opt-in.
- Command gestures are now a separate optional download. Post-victory reactions
  are shelved, and their animation files have been removed.

## Download and enable command gestures

1. Open the [Walk With Me Files tab](https://www.nexusmods.com/skyrimspecialedition/mods/191283?tab=files).
   Install **Walk-With-Me-0.2.2-SE-AE.zip** from Main Files and
   **Walk-With-Me-0.2.2-Command-Gestures.zip** from Optional Files.
2. Install [Open Animation Replacer](https://www.nexusmods.com/skyrimspecialedition/mods/92109),
   [First Person Interactions](https://www.nexusmods.com/skyrimspecialedition/mods/123129)
   and the full [Offset Movement Animation](https://www.nexusmods.com/skyrimspecialedition/mods/110408),
   with the requirements listed on their pages. Enable `FirstPersonInteractions.esp`
   even when using third-person signals: WWM uses its shared trigger.
3. In MO2's left pane, put **Walk With Me - Command Gestures** below **Offset
   Movement Animation**, letting the add-on win its seven GPMA source-file
   conflicts. In Vortex, give the add-on the matching file conflict rule and deploy.
4. Launch Nemesis through MO2 with the correct profile active, or through your
   existing deployed setup. Keep your usual patches selected, tick **GP Offset
   Movement Animation**, click **Update Engine**, then **Launch Nemesis Behavior
   Engine**. Enable the generated output and let its generated HKXs win.
5. OAR reads the installed configs automatically. There is no OAR or Walk With Me
   Nemesis checkbox. In OAR's menu, look for **Walk With Me Signals**; its conditions
   usually read false until you issue the relevant command.
6. Restart Skyrim through SKSE. Open **Walk With Me > Controls > Wheel & signals**
   in SKSE Menu Framework and enable **Command hand signal**. Try Lead, Companion
   and Rear with weapons sheathed, first standing and then walking.

Keep your modlist's existing behavior-generation procedure and other patches.
Do not install another player's generated behaviors. If your list uses Pandora,
keep its supported workflow and enable the GPMA patch there; do not stack old
output from both engines. The included GESTURES.md covers troubleshooting.

## Upgrading

Close Skyrim and replace the old main mod rather than merging over it. Back up
and restore `SKSE/Plugins/Wayfarer.ini` to keep preferences. Enable `Wayfarer.esp`
and disable older DLL/script test overlays. Replacing the combined 0.2.0 install
removes obsolete gesture/victory files. Regenerate behavior output after adding,
updating or removing the gesture add-on, including when choosing Main only.

The main follower features and Classic hand holding do not require OAR/Nemesis.
The optional TETHER method has a separate local clasp-generation procedure in
HAND_HOLDING.md; the command gesture add-on does not provide those clips.

This build has offline regression and package checks; fresh Nemesis generation
and in-game playback still need validation. Hand holding remains experimental,
and Katana's personal-dialogue opener may keep its chat indicator visible.

## File descriptions

- **Main:** Walk With Me 0.2.2. Requires matching SKSE, Address Library and SKSE Menu
  Framework 3. Install through your mod manager; enable Wayfarer.esp. Replace old
  installs. Optional command gestures are a separate download.
- **Command Gestures:** Optional add-on for Walk With Me 0.2.2. Requires Main,
  Open Animation Replacer, First Person Interactions and the full Offset Movement
  Animation mod. Let this add-on overwrite GPMA's seven source files, then update
  and run Nemesis with GP Offset Movement Animation checked.
- **Source:** Complete matching mod, CommonLib and dependency source, build tools
  and licenses. For developers; not an additional mod-manager install.
