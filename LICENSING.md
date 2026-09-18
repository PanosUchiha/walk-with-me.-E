# Licensing

Walk With Me uses **GPL-3.0-or-later**. See [LICENSE](LICENSE).
The permissions in [EXCEPTIONS.md](EXCEPTIONS.md) also apply to the mod code.

The DLL links alandtse/CommonLibSSE-NG 7.5.1, revision
`bedcb1e05418baba7b316a650b6180c2dd6007a8`. This fork uses GPL with the included
modding and linking exceptions.

Earlier MIT notices are kept in [LICENSES/Wayfarer-MIT.txt](LICENSES/Wayfarer-MIT.txt).
Earlier grants remain valid. Original command animations keep that MIT license.
Third-party files keep their own licenses:

- SKSE Menu Framework API: LGPL-2.1.
- Open Animation Replacer conditions SDK: GPL-3.0 with the upstream modding
  and linking exceptions; see `third_party/OpenAnimationReplacer` and the
  corresponding `package/ThirdParty/OpenAnimationReplacer-*` notices.
- Bundled PyNifly animation codec: GPL-3.0, with its upstream source and license.
  Generated clasp clips are made locally from the player's own game and are excluded
  from public binary and source downloads.
- Cinzel Decorative: OFL-1.1.
- Credited Game-icons.net artwork: CC-BY-3.0.
- TETHER travel-speed hook reference and physics adaptation: MIT,
  Copyright (c) 2026 KEI997. Full license and adaptation details are in
  `package/ThirdParty/TETHER-LICENSE.txt` and `TETHER-NOTICE.txt`.

See `package/ThirdParty` in the source or `ThirdParty` in the mod ZIP.
These notices do not license Skyrim's game files.

## Releases

Publish the matching complete Source.zip beside the mod download, at no extra
charge. It includes mod source, build scripts, CommonLib and dependency source.
Keep it available for that DLL version. GitHub's automatic source ZIP is incomplete.

Include LICENSE, EXCEPTIONS.md, LICENSING.md and ThirdParty notices with the mod.
The release packager adds these. See [build](docs/build.md) and [publish](docs/publish.md).
