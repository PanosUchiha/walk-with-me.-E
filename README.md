# Walk With Me

Skyrim follower mod. Travel with up to 10 companions, change their formation,
and let them rest nearby.

Version **0.2.2** adds animated follower icons, compact settings with the original
yellow/ivory palette and background artwork, richer rest activities, loot scouting,
reworked teleport catch-up, and Classic/TETHER hand-holding choices.
See the [changelog since public 0.2.0](docs/changes-0.2.2.md).

For future feature work, start from the [accepted baseline](docs/baseline-0.2.1.md).

The public package includes a local generator for optional hand-clasp animations.
Travel settings also offer **Classic** hand holding, which needs no
generated clasp animations. Classic is the default method.
It contains no Skyrim-derived clasp clips. See [hand holding](docs/hand-holding.md).
Command signals and their GPMA extension now ship in the separate Command Gestures
add-on. Install its requirements and regenerate behaviors as described in
[gesture setup](docs/gestures.md). Replace old combined installs rather than merging.

## Setup

Install with MO2 or Vortex, enable `Wayfarer.esp`, then start Skyrim through SKSE.
Aim at a companion and press **G**, then choose **Travel together**. Open
**Walk With Me** in SKSE Menu Framework to manage Party and rebind controls.
Automatic enrollment is opt-in. Settings save automatically.

The SE/AE build targets 1.5.97, selected 1.6.x builds and 1.7.99/1.7.104.
The 1.6/GOG and 1.7 paths still need in-game testing. Install matching SKSE,
Address Library and Menu Framework 3 builds, plus the x64 Visual C++ runtime.
NFF and other frameworks are optional. See [release details](docs/release.md).

## Docs

- [Current work and complete review handoff](docs/review-handoff-2026-09-23.md)
- [Controls and features](docs/usage.md)
- [Gesture setup](docs/gestures.md)
- [Build](docs/build.md)
- [API](docs/api-guide.md)
- [Tests](docs/testing.md)
- [Changes](docs/changes-0.2.2.md)
- [Publish source](docs/publish.md)

## License

GPL-3.0-or-later with the permissions in [EXCEPTIONS.md](EXCEPTIONS.md).
See [LICENSING.md](LICENSING.md) for dependency and asset licenses.
Publish the matching complete source ZIP with each mod download.

Created by Tobih with Codex. Third-party credits are in `package/ThirdParty`.

The travel-speed hook credits [TETHER](https://github.com/Kei-BWV997/TETHER)
by **KEI997** (MIT, Copyright (c) 2026 KEI997).
Classic is the default hand-holding method. The optional TETHER method uses
locally generated OAR clasp poses and connects the grip before movement handoff.
See [hand holding](docs/hand-holding.md) for setup and limitations.
See `package/ThirdParty/TETHER-NOTICE.txt`.

## Shoutout The Haters for Reporting

Love y'all.
