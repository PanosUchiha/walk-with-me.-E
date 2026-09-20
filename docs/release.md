# Walk With Me 0.2.1

This release preserves the user-approved r9 build: September 19 r3 movement,
later post-combat reactions and a separate gestures add-on. The r5–r8 movement
experiments are removed. See `docs/baseline-0.2.1.md` for the accepted baseline
and remaining limitations.

## Requirements

Windows Skyrim runtimes accepted by this build:

- 1.5.97
- 1.6.318, 323, 342, 353, 629, 640, 659, 1130, 1170 and 1179
- 1.7.99 and 1.7.104

The 1.6/GOG and 1.7 paths still need in-game testing. VR, Game Pass, Epic and
other 1.7 patches are not accepted.

Install matching [SKSE](https://skse.silverlock.org/),
[Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444) and
[SKSE Menu Framework 3](https://www.nexusmods.com/skyrimspecialedition/mods/120352)
builds, plus the x64 Visual C++ runtime. GOG 1.6.1179 needs SKSE 2.2.6 GOG.
NFF and other frameworks are optional.

Command gestures also need First Person Interactions, Open Animation Replacer
and generated GP Offset Movement Animation behavior. Install the matching
`Command-Gestures.zip` add-on for the third-person clips and behavior source patch.
The base archive has neither the GPMA patch nor FPI-dependent signal configs.
FPI first-person clips are not bundled.

Open Animation Replacer is also required for the authored shoulder reaction and
optional clasp poses. Generate clasp clips locally using HAND_HOLDING.md; the
public package ships the generator and codec source instead of game-derived clips.

## Install

Close Skyrim and install `Walk-With-Me-0.2.1-SE-AE.zip` through your mod manager.
Replace the old mod, keep your INI if wanted, and enable `Wayfarer.esp`.
Do not merge over the old combined download: remove its leftover WWM gesture
files by replacing the installation. Follow GESTURES.md to install the optional
add-on and regenerate behaviors, or to remove the old gesture patch.
Do not rename Wayfarer files or enable two copies.

Read USAGE.md for controls, HAND_HOLDING.md for the experimental feature and
CHANGELOG.md for changes. Read GESTURES.md before generating behaviors. Hand holding is off by default; wrist and turning
problems remain. These documents are also under docs in the source.

## Source

The DLL links CommonLibSSE-NG 7.5.1 at
`bedcb1e05418baba7b316a650b6180c2dd6007a8`.
The code uses GPL-3.0-or-later with the included exceptions. Asset licenses are
separate. See LICENSE, LICENSING.md and ThirdParty.

Upload `Walk-With-Me-0.2.1-Source.zip` with this download. It includes mod,
CommonLib and dependency source. Build instructions are in docs/build.md.
