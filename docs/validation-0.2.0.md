# Walk With Me 0.2.0 validation

Prepared September 17, 2026. Comparison baseline: 0.16.7, with the installed
Companion Unstick r1 / TETHER r18 build used to check retained assets.

- Modern native build: **19/19 CTest suites pass**.
- Legacy native build: **19/19 CTest suites pass**.
- All six Papyrus scripts compile with **zero errors and warnings**.
- **116** first-party C++ files pass the repository clang-format 19 style check.
- Actual ImGui panel/button tests: **zero failures**, including Unstick, disabled
  actions, drag cancellation, duplicate activation and closing input lockout.
- Wheel and three companion-state previews are byte-identical to the preceding
  Unstick build after formatting, confirming unchanged rendered output.
- **105** required GUI exports are present in the installed Menu Framework.
- DLL metadata reports **0.2.0**, with the existing modern runtime-reader flags.
- ESP, dialogue, aliases, scripts, settings and artwork pass package validation.
- The generator extracted from the public ZIP runs with Python site packages
  disabled (`-S`), confirming that the bundled codec has no Blender or pip dependency.
- The local generator produces **68/68** clasp clips matching the prior validated
  r18 animation payloads. Round-trip checks retain motion, annotations and bindings.
- Public packaging admits only the **eight reviewed original WWM animation clips**.
  Generated clasp HKX files are excluded from both public archives.
- Each ZIP is checked for corruption and each entry against its source payload;
  archive SHA-256 files and the mod's internal file manifest accompany the release.

## Native binary fingerprints

Modern DLL: `079bf4b4e4b2b42c767b055df1dad602cc044a43ce2c89226f5c0fa78c4b95d1`

Legacy validation DLL: `01d26925782f8e3a32823fb3110c9796eb481b016c6512ad5ccc7512428d4a25`

The public SE/AE archive uses the modern DLL. The legacy DLL is a validation build.

## Remaining game checks

This packaging pass did not run Skyrim. Verify pause/input cleanup, physical
Unstick recovery, clasp contact and release, victory transitions, save/load and
follower-framework behavior in game. The 1.6/GOG and 1.7 runtime paths still need
runtime-specific playtests. Passing offline tests is not a compatibility guarantee.
