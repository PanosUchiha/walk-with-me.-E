# Walk With Me

Skyrim follower mod. Travel with up to 10 companions, change their formation,
and let them rest nearby.

Version **0.2.0** includes targeted companion controls, Unstick, party teleport,
temporary relaxation, updated victory reactions and experimental TETHER-based
hand holding. See the [changelog](docs/changes-0.2.0.md) and
[companion controls](docs/package-control.md).

The public package includes a local generator for optional hand-clasp animations.
It contains no Skyrim-derived clasp clips. See [hand holding](docs/hand-holding.md).

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

- [Controls and features](docs/usage.md)
- [Gesture setup](docs/gestures.md)
- [Build](docs/build.md)
- [API](docs/api-guide.md)
- [Tests](docs/testing.md)
- [Changes](docs/changes-0.2.0.md)
- [Publish source](docs/publish.md)

## License

GPL-3.0-or-later with the permissions in [EXCEPTIONS.md](EXCEPTIONS.md).
See [LICENSING.md](LICENSING.md) for dependency and asset licenses.
Publish the matching complete source ZIP with each mod download.

Created by Tobih with Codex. Third-party credits are in `package/ThirdParty`.

The travel-speed hook credits [TETHER](https://github.com/Kei-BWV997/TETHER)
by **KEI997** (MIT, Copyright (c) 2026 KEI997).
Revision 18 adds an authored clasp pose through OAR and fixes a handoff wait
when script cleanup finishes before the engine changes packages. Pause to
arrive and connect. The combined pose/physics result needs an in-game check;
see [the test guide](docs/tether-r18-handoff.md).
See `package/ThirdParty/TETHER-NOTICE.txt`.

## Shoutout The Haters for Reporting

Love y'all.
