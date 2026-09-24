# Walk With Me

Travel with up to 10 followers. Choose Natural, Lead, Companion, Rear, Relax or
Vanilla. Adjust spacing, pace and rest settings in SKSE Menu Framework.

Add followers from Party or their **Walk with me** dialogue. Removing them releases
this mod's control without dismissing them. Automatic enrollment is off by default; add each companion or explicitly opt in.

Followers can use nearby seats, idles and conversation spots while resting.
Temporary rest ends when you leave its range; Relax waits for another order.
NFF and custom followers have separate enforcement settings.

Aim at an NPC and press G for Travel together, Release, Unstick and Cancel.
The wheel includes party teleport and temporary relaxation controls.

## Requirements

Windows, matching SKSE, Address Library, SKSE Menu Framework 3 and the x64 Visual
C++ runtime. The SE/AE package targets 1.5.97, selected 1.6.x builds and
1.7.99/1.7.104. The 1.6/GOG and 1.7 paths still need in-game testing.
See the download README for the exact runtime list.

Command gestures are optional. They need First Person Interactions, Open Animation
Replacer, the full Offset Movement Animation mod, the matching **Command Gestures**
optional download and regenerated GPMA behavior. Install GPMA before the add-on,
let the add-on win its seven source-file conflicts, then Update Engine and generate
with **GP Offset Movement Animation** enabled. Launch the generator through MO2
when using MO2; deploy mods before running it with Vortex.
Third-person clips are in the add-on; FPI's first-person clips are separate.
Gesture Animation Remix is not needed.

Open Animation Replacer is also needed for locally generated TETHER clasp poses.
The base follower features do not require a
behavior generator.

Animation requirements:
- [Open Animation Replacer](https://www.nexusmods.com/skyrimspecialedition/mods/92109): command signals and optional TETHER clasp poses.
- [First Person Interactions](https://www.nexusmods.com/skyrimspecialedition/mods/123129): command signals, including the shared trigger used in third person.
- [Offset Movement Animation](https://www.nexusmods.com/skyrimspecialedition/mods/110408): command signals; install the complete mod and regenerate with Nemesis or Pandora.

The OAR Detection Plugin is not used by WWM's own animation configs. If OAR names
that plugin in an error, follow the requirements of the animation pack using it.
It does not fix missing WWM conditions. `requiredPlugin: "Walk With Me"` is correct:
it identifies the SKSE condition provider, not the ESP filename.

Hand holding is experimental and off by default. Classic is the default method
and needs no generated clips. The optional TETHER method uses a physics grip and
OAR clasp poses generated from your own Skyrim files using HAND_HOLDING.md.
The public archive includes the generator; the gesture add-on does not contain
clasp clips. Post-victory reactions are shelved in 0.2.2.

## Credits

The travel-speed hook references
[TETHER](https://github.com/Kei-BWV997/TETHER) by **KEI997** (MIT).
Copyright (c) 2026 KEI997. Full license and adaptation details are included in
ThirdParty/TETHER-LICENSE.txt and TETHER-NOTICE.txt.

## Install

Close Skyrim, replace the old mod through MO2 or Vortex and enable `Wayfarer.esp`.
Keep the INI to retain settings. Do not rename Wayfarer files or install two copies.
When upgrading from the combined 0.2.0 archive, replace rather than merge so old
GPMA patch files and signal configs do not remain. Install the new optional gesture
add-on only with its requirements. Regenerate after adding or removing the patch.

## Source and license

GPL-3.0-or-later with the included exceptions. Asset licenses and credits are in
ThirdParty. Download the matching complete Source.zip from the same release.

Gesture setup: see docs/gestures.md in the source or GESTURES.md in the mod ZIP.
The GPMA patch is in the Command Gestures optional download. Let its files win over GPMA, then regenerate with your
existing Nemesis or Pandora setup. Do not replace another modlist's generated graph.
