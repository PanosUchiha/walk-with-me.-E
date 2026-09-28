# Walk With Me 0.2.5 validation

Release preparation: September 28, 2026. Compared against 0.2.2.

## Build and package checks

- Both legacy and modern Release builds succeeded, with **27/27 CTest suites passing
  in each**. The public package uses the modern CommonLibSSE-NG 7.5.1 DLL.
- All **seven Papyrus scripts** compiled with zero errors and warnings.
- The ESP and SEQ were regenerated, and the staged package passed the public
  package verifier.
- All **5/5 animation packaging tests** passed.
- The packager checks DLL version 0.2.5, the modern runtime declarations, disabled
  victory behavior, all 68 clasp clips and both clasp configs, approved animation
  hashes, and OAR condition minimum version 0.2.0.0.
- Compared with 0.2.2, the main archive adds 16 formation emblems, the pathfinder icon
  and the clasp clips with their configs. The clasp generator moved to the source
  archive only. The gesture add-on's animations and GPMA patch are byte-identical to
  0.2.2; only its documents changed.
- The source archive now skips git-ignored files, so local notes are not published.

## GOG

- SKSE's GOG builds report the runtime as 1.6.1179.1. The runtime test covers GOG 1179
  and 659 (accepted), GOG 1170 and Epic (refused), and every Steam runtime.
- GOG 1.6.1179's Address Library matches Steam 1.6.1170's function sizes for 99.76% of
  functions, including every ID Walk With Me resolves itself.
- The travel speed hook's call target was decoded on 1.5.97 as `GetScale`. On other
  builds, the hook now checks for that target before patching.
- **Not tested in game on GOG.** No GOG installation was available.

## Scope

These are build and offline checks, not a playtest. Parts of this release were
installed and tried on SE 1.5.97 during development, but the combined 0.2.5 build
has not been playtested. Custom formations, the Pathfinder, the Party actions page,
Classic's new movement, TETHER's handoff changes and the NFF takeover still need
in-game confirmation. Hand holding remains experimental.

Known issues remain: gamepad button releases, including LB, are swallowed while the
party menu is open. The existing integer-to-float warnings in party_life.h also remain.
