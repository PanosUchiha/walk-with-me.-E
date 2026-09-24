# Walk With Me 0.2.2 validation

Release preparation: September 24, 2026. The comparison baseline is the preserved
public 0.2.0 archive; local 0.2.1 is not treated as a published Nexus update.

## Build and package checks

- Both legacy and modern Release builds succeeded, with **24/24 CTest suites
  passing in each build**. The public package uses the modern CommonLibSSE-NG
  7.5.1 DLL, not the local legacy DLL.
- All **seven Papyrus scripts** compiled successfully with zero errors/warnings.
  WayfarerVictoryVoice is a compatibility cleanup helper; reactions remain disabled.
- The ESP and SEQ were regenerated and passed staged-package verification,
  including preserved form/alias/global identities and PEX checks.
- All **5/5 animation packaging regressions** passed, including GPMA text layout,
  reference arrays and separation of the optional add-on.
- **113/113 used Menu Framework exports** were found in the installed framework.
- The packager checks DLL version 0.2.2, modern runtime declarations, disabled
  victory behavior, approved original HKX hashes and the 742 x 465 RGBA artwork.
- Main contains no Nemesis patch or FPI-dependent WWM signal configs. The gesture
  add-on contains seven GPMA source files and the original third-person signals.
  Neither archive includes removed victory assets or locally generated clasp clips.
- ZIP integrity, file inventory and per-file content hashes are checked when
  writing all three archives. Main and Gestures include internal SHA-256 manifests;
  all three archives have external SHA-256 files and matching complete source.
- OAR condition minimum versions remain at 0.2.0; this is the condition API version,
  not the release label.
- The loot regression reproduces Rumarin's indefinite wait at an unopened chest.
  It fails without the presentation timeout and checks release after 15 seconds,
  the full search/stand-up before that wait, earlier departure reasons and a fresh
  timer on the next visit. The area remains consumed after the timeout.

## Scope of visual checks

The preceding menu work was rendered offline through the actual ImGui settings
functions. All six pages, narrow layout and scrolling were inspected; the artwork
stays in the lower-right corner and behind controls. Chat animation settings were
also checked through the actual config load/save code, including an older INI.

These are offline checks, not a Skyrim playtest. New overhead icon animation,
rest/loot behavior, hand holding, fresh Nemesis output and the complete supported
runtime matrix still need in-game validation. The existing integer-to-float
warnings in party_life.h remain. Katana's personal-dialogue opener can leave a
conversation indicator visible. Hand/wrist alignment can vary by skeleton and
animation setup; hand holding remains experimental.
