# Walk With Me 0.2.0

Changes since 0.16.7, including the subsequent local 0.16.8 builds.
This release uses the requested **0.2.0** version label.

## Restored r3 movement candidate r9

- Restored the verified September 19 r3 movement implementation, including
  destination selection, startup timing, package refresh and recovery behavior.
- Removed the later actor-route corner waypoints, departure timing changes,
  handoff state machine and corrective-reset guard. The r5–r8 entries below
  describe historical experiments, not the movement behavior shipped in r9.
- Kept the later armed and sheathed victory reactions, varied party assignments,
  shoulder animation support, three-enemy minimum and separate gestures add-on.
- This establishes the user's preferred comparison baseline; it does not claim
  that every narrow passage or freeze is fixed. See [r3-movement-r9.md](r3-movement-r9.md).

## Reset cadence candidate r8 (historical)

- Added a common one-second corrective-reset guard during established blocked
  movement or stalls, persisting through brief reacquisition. Ordinary pace,
  heading, braking and obstacle updates cannot bypass it.
- Kept marker updates, initial route submission, bounded retries and r7 cleanup.
  Sustained acquired movement restores ordinary reset cadence. Context releases
  discard the old guard.
- Added applied/deferred reset reasons and corrected the startup revision banner.

## Control handoff candidate r7

- Pending alias assignments no longer accumulate route-stall time or cancel
  themselves at the 4.5-second route threshold. Assigned aliases get a separate
  15-second package acquisition timeout with bounded existing package retries.
- Confirmed WWM ownership enables route-stall detection, including a subsequent
  engine MovementBlocked package. Alias loss or another ordinary package resets
  that ownership evidence.
- Recovery waits for a subsequent serialized alias pass and an absent actor
  before reacquiring. Per-request handoff logs distinguish assignment, ownership,
  blocked movement, acquisition timeout and recovery cleanup.

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
- Added an original 6.5-second shoulder recovery, vanilla brow wiping and hand
  inspection alongside applause. Reactions are distributed across nearby eligible
  teammates, with actor ordering rotated each encounter, rather than independently
  choosing applause for the whole party. The three-enemy minimum is unchanged.
- Armed sword/greatsword reactions use the existing Civil War cheer, then allow
  sheathed recovery. Other drawn equipment tries a brief native armed idle and
  retains sheathed recovery if that branch is rejected. This branch needs playtesting.
- Added actor-specific OAR selection, staggered starts, a longer start window,
  and cleanup on interruption. Missing custom assets fall back to applause.
  Startup now allows asynchronous idle ownership to settle; shoulder diagnostics
  distinguish engine acceptance from OAR condition evaluation and visible playback.

## Bridge and departure candidate r6 (in-game validation pending)

- Renew identical corner waypoints without forcing another route reset. Advance
  checks run twice a second, and transient failures have a bounded stale hold.
- Keep corner guidance active for lagging companions across attached neighboring
  cells; intermediate points no longer imply arrival at the final formation slot.
- Cap corner speeds at the selected normal travel cap, keeping the open-field
  distant catch-up behavior. Overlap departure delays and run the first ready
  travel update immediately.

## Actor-route candidate r5

- Checks the companion's own connected route to its formation destination and
  briefly holds a collision-checked waypoint before a bend. Searches are bounded
  and throttled; unavailable routes retain Skyrim's native destination.
- Holds valid formation roles steady indoors and during turns to reduce crossing
  and repeated destination changes. Missing navmesh links still need a mod patch.
- Built on the restored r3 movement lifecycle. The regressed r4 package-reset and
  MovementBlocked hold experiment remains withdrawn.

## Experimental hand holding

- Integrated the TETHER-based physics grip with the selected companion and
  serialized setup/cleanup, replacing the old procedural pose backend.
- Improved approach, facing, alias/package handoff, cancellation, and rearming
  after failed attempts. Script cleanup acknowledgement can precede the engine's
  package transition without losing the handoff.
- Each setup step now waits only on what it actually depends on. The ragdoll
  bodies are created while the follower is still turning into place, leaving the
  constraint as the single script call between contact and a joined hand.
- Shortened the acquisition dwells and recovery timers: arrival settles in 0.20s,
  grip contact in 0.15s, the default lead-in is one second, an attempt gives up
  after eight seconds, and a failed attempt now waits four seconds before
  rearming instead of eight.
- The constraint pulls both palm anchors onto one another the instant it is
  created, so it is no longer formed across a wide gap. `fGripWristGap`
  (default 20) sets how close the wrists must already be; when a pair cannot
  reach that, the requirement widens to 45 over 1.5 seconds rather than refusing
  to connect.
- Added `fPalmX`, `fPalmY` and `fPalmZ` for the palm contact point, because the
  correct value depends on the installed body and hand meshes. While acquiring,
  the log prints the measured wrist gap against the limit in force once a second,
  so both settings can be chosen from a log rather than guessed.
- Added authored arm, wrist and finger rotations for both hands and genders.
  The optional generator makes 68 clasp clips from the player's own extracted
  vanilla locomotion animations while retaining root motion, events and bindings.
- The public downloads include the generator, authored rotation recipe and
  codec source. They **do not include Skyrim-derived clasp HKX files**.
  Install the locally generated companion archive to enable clasp acquisition.
- Hand holding remains opt-in and experimental. The standalone TETHER mod is
  not required; KEI997's MIT-licensed contribution is credited.

## Stability

- Command-gesture head tracking kept the player's skeleton by raw address in
  order to test whether the 3D had been rebuilt underneath it. That node is now
  held by reference, so a discarded skeleton cannot be mistaken for the original
  one when a new node is allocated at the same address. The pointer was only
  ever compared and never followed, so this closes a mistaken-identity case
  rather than a crash.

## Source and packaging

- Corrected the script extender declaration. The plugin declared that it uses no
  game structures, which was never true of a mod that reads actor runtime data,
  AI process state, character controller flags and animation graph variables. It
  now declares the post-1.6.629 layouts it is actually compiled against. Both
  declarations are accepted on every supported runtime, so this is a correctness
  fix and not a compatibility fix.
- The packager now refuses to build an archive around anything but the
  CommonLibSSE-NG binary, and checks the declaration flags that binary ships
  with before writing the archive.
- Standardized first-party C++ formatting with the repository's four-space,
  explicit-brace style and retained direct functions and plain state structures.
- Replaced the default prototype-oriented release wrapper with a dedicated
  public packager, explicit asset checks, and matching complete source archives.
- Updated DLL/resource metadata and OAR condition requirements to 0.2.0.
- Preserved Wayfarer filenames, plugin/form IDs, API version and save formats.

## Upgrade and validation

Pathing/reaction test build: pending victory reactions survive brief player
movement within their existing fifteen-second window. Formation goals now check
body clearance in interiors and towns as well as wilderness, compress in narrow
spaces, and can select nearby connected navmesh destinations around bends.
Hand-guided approaches no longer enter wilderness obstacle detours. See
reactions-pathing-review.md for evidence, limits and the Lost Prospect Mine test.

Animation hotfix preparation: command clips and the GPMA extension now package
as a separate optional Command Gestures download. The base archive no longer
installs partial GPMA patches or FPI-dependent signal configs. Nemesis patch
serialization preserves Havok indentation and multiline arrays. Added OAR
failure diagnostics and a loaded-OAR guard for gesture playback. Replace older
combined installs and regenerate after changing the gesture add-on. See
animation-hotfix-audit.md for findings and pending in-game validation.

Close Skyrim, install the full 0.2.0 archive, and preserve your INI if you want
to keep preferences. Replace DLL, ESP and scripts together. Because the new
version label is numerically below 0.16.x, select this archive explicitly in your
mod manager. Existing saves and manual selections use the same identifiers.

Both native builds, Papyrus compilation, package checks and offline UI checks
are recorded in RELEASE-VALIDATION.md. Offline tests cannot establish in-game
animation feel, every follower framework's behavior, or all supported runtimes.
Unstick is a manual recovery attempt, not a guaranteed repair for broken scripts.
