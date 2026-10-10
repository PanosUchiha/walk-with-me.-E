# Walk With Me 0.3.0

The formation holds while weapons are out with nobody to fight, a Battle plan tile
for the optional Fight With Me mod, and a fix for SKSE Menu Framework 3 not being
found. See CHANGELOG.md for all changes since 0.2.5.

## Requirements

Windows Skyrim runtimes accepted by this build:

- 1.5.97
- 1.6.318, 323, 342, 353, 629, 640, 1130 and 1170 (Steam)
- 1.6.1179 and 659 (GOG)
- 1.7.99 and 1.7.104

The 1.6/GOG and 1.7 paths still need in-game testing. GOG 1.6.1170 is refused:
update it to 1.6.1179. VR, Game Pass, Epic and other 1.7 patches are not accepted.

Install matching [SKSE](https://skse.silverlock.org/),
[Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) and
[SKSE Menu Framework 3](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
builds, plus the x64 Visual C++ runtime. GOG 1.6.1179 needs SKSE 2.2.6 GOG.
NFF, other frameworks and Fight With Me are optional.

Command gestures also need First Person Interactions, Open Animation Replacer
and generated GP Offset Movement Animation behavior. Install the matching
`Command-Gestures.zip` add-on for the third-person clips and behavior source patch.

The main follower features and Classic hand holding need no behavior generator.
The optional TETHER method needs Open Animation Replacer. Its clasp poses are
included in the main download, not the gesture add-on. See HAND_HOLDING.md.

## Install

Close Skyrim and install `Walk-With-Me-0.3.0-SE-AE.zip` through your mod manager.
Replace the old mod, keep your INI if wanted, and enable `Wayfarer.esp`.
Upgrading from 0.2.5 needs no new game and no behavior regeneration.
Do not rename Wayfarer files or enable two copies.

Read USAGE.md for controls, HAND_HOLDING.md for the experimental feature and
CHANGELOG.md for changes. These documents are also under docs in the source.

## Source

The DLL links CommonLibSSE-NG 7.5.1 at
`bedcb1e05418baba7b316a650b6180c2dd6007a8`.
The code uses GPL-3.0-or-later with the included exceptions. Asset licenses are
separate. See LICENSE, LICENSING.md and ThirdParty.

The matching `Walk-With-Me-0.3.0-Source.zip` includes mod,
CommonLib and dependency source. Build instructions are in docs/build.md.
