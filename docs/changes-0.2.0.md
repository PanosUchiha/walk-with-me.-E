# Walk With Me 0.2.0

Changes since 0.16.7, including the subsequent local 0.16.8 builds.
This release uses the requested **0.2.0** version label.

## Companion controls

- Aim at an NPC and press **G** to open a focused companion panel. The selected
  NPC stays fixed until the panel closes, and the game pauses behind the panel.
- **Travel together** lets an explicitly selected NPC use Walk With Me's travel
  and rest orders. **Release** returns control to their own AI without dismissing
  or recruiting them through another follower framework.
- **Unstick** applies one gentle push after the menu closes to help a frozen
  companion recover. Use the button, **U**, or gamepad **X**. It checks the target
  again before execution, expires stale requests, and has a three-second cooldown.
- Added a visible **Cancel** button with Esc/gamepad B hints.
- Matched the wheel footer and companion controls with dark teal panels, gold
  edging, inscription lettering, and keyboard/controller badges.
- Added staggered entrances, icon ring reveals, eased hover and press feedback,
  activation highlights, and a short exit that blocks duplicate submissions.
  The wheel-to-companion transition retains the dim backdrop.

## Party orders and follower handling

- Added **Teleport party** to recall eligible travelling companions to checked
  nearby landing positions, including companions in other cells.
- Added **Temporary relaxation** directly to the wheel. Its setting saves
  immediately; the separate manual Relax order remains available.
- Improved automatic-rest/resume HUD presentation and scaled the wheel and
  companion panel to fit different viewport sizes.
- Kept automatic enrollment off by default. Older settings receive a one-time
  migration; later explicit opt-in remains saved.
- Made NFF's active follower roster authoritative for automatic eligibility,
  avoiding unintended enrollment of inactive housecarls or stale teammates.
- Added explicit control for custom followers with private recruitment systems,
  with per-save ownership and detectable dismissal handling.
- Fixed nearby-companion checks across exterior cell boundaries in the same
  worldspace. Separate interiors remain separate.
- Retained 0.16.7's established formation movement and alias lifecycle rather
  than shipping the withdrawn movement-ownership experiments.

## Victory reactions

- Improved encounter, equipment and animation-handoff checks for reactions after
  battles with at least three defeated enemies.
- Added an original 6.5-second shoulder-recovery animation alongside randomized
  applause. Armed sword/greatsword reactions use the existing Civil War cheer.
- Added actor-specific OAR selection, staggered starts, a longer start window,
  and cleanup on interruption. Missing custom assets fall back to applause.

## Experimental hand holding

- Integrated the TETHER-based physics grip with the selected companion and
  serialized setup/cleanup, replacing the old procedural pose backend.
- Improved approach, facing, alias/package handoff, cancellation, and rearming
  after failed attempts. Script cleanup acknowledgement can precede the engine's
  package transition without losing the handoff.
- Added authored arm, wrist and finger rotations for both hands and genders.
  The optional generator makes 68 clasp clips from the player's own extracted
  vanilla locomotion animations while retaining root motion, events and bindings.
- The public downloads include the generator, authored rotation recipe and
  codec source. They **do not include Skyrim-derived clasp HKX files**.
  Install the locally generated companion archive to enable clasp acquisition.
- Hand holding remains opt-in and experimental. The standalone TETHER mod is
  not required; KEI997's MIT-licensed contribution is credited.

## Source and packaging

- Standardized first-party C++ formatting with the repository's four-space,
  explicit-brace style and retained direct functions and plain state structures.
- Replaced the default prototype-oriented release wrapper with a dedicated
  public packager, explicit asset checks, and matching complete source archives.
- Updated DLL/resource metadata and OAR condition requirements to 0.2.0.
- Preserved Wayfarer filenames, plugin/form IDs, API version and save formats.

## Upgrade and validation

Close Skyrim, install the full 0.2.0 archive, and preserve your INI if you want
to keep preferences. Replace DLL, ESP and scripts together. Because the new
version label is numerically below 0.16.x, select this archive explicitly in your
mod manager. Existing saves and manual selections use the same identifiers.

Both native builds, Papyrus compilation, package checks and offline UI checks
are recorded in RELEASE-VALIDATION.md. Offline tests cannot establish in-game
animation feel, every follower framework's behavior, or all supported runtimes.
Unstick is a manual recovery attempt, not a guaranteed repair for broken scripts.
