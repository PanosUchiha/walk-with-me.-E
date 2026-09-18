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
Replacer and generated GP Offset Movement Animation behavior. Third-person clips
are bundled; FPI's first-person clips are separate. Gesture Animation Remix is not needed.

Hand holding is experimental and off by default. It uses a TETHER-based physics grip and authored clasp poses. Generate the optional
clasp clips from your own Skyrim files using HAND_HOLDING.md. The public archive
contains the generator, not the game-derived clips.

## Credits

The travel-speed hook references
[TETHER](https://github.com/Kei-BWV997/TETHER) by **KEI997** (MIT).
Copyright (c) 2026 KEI997. Full license and adaptation details are included in
ThirdParty/TETHER-LICENSE.txt and TETHER-NOTICE.txt.

## Install

Close Skyrim, replace the old mod through MO2 or Vortex and enable `Wayfarer.esp`.
Keep the INI to retain settings. Do not rename Wayfarer files or install two copies.

## Source and license

GPL-3.0-or-later with the included exceptions. Asset licenses and credits are in
ThirdParty. Download the matching complete Source.zip from the same release.

Gesture setup: see docs/gestures.md in the source or GESTURES.md in the mod ZIP.
The GPMA patch is included. Let its files win over GPMA, then regenerate with your
existing Nemesis or Pandora setup. Do not replace another modlist's generated graph.
