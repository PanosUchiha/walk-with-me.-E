# Walk With Me 0.2.2

Changes since **0.2.0**, the previous public Nexus release. This also includes
the separate gesture packaging and movement corrections prepared in local 0.2.1.

## Menu and overhead icons

- Reorganized settings into **Party, Travel, Rest, Loot, Interface and Controls**.
- Adopted SceneDirector's compact text sizing and spacing while retaining Walk
  With Me's yellow/ivory colors. Related checkboxes share responsive columns;
  advanced tuning stays in collapsible sections.
- Added the Walk With Me artwork as a faint, fixed background in the bottom-right
  corner of settings. It preserves its proportions and stays behind controls.
- Conversation and loot icons now pop into view, settle above the follower's
  head, and float gently with partial transparency.
- Added **Animate chat icons** under **Interface > Follower icons**. Turning it
  off keeps the chat icon still; chat and loot icons also have separate visibility
  switches. Loot markers retain white/gold coloring for ordinary/valuable finds.

## Rest and companion activities

- Added conversation indicators for available follower dialogue during temporary
  rest and Relax. Management, trade, empty and already-heard nested branches are
  filtered so fewer irrelevant topics produce an icon.
- Added activities suited to the surroundings: fires and warming hands, shrines,
  standing stones, water's edges, lookouts and place-specific vanilla idles.
- Expanded inn behavior with tables, serving interactions, food/drink idles and
  reactions to bard performances. No new voiced dialogue is included.
- Companions begin spreading out within the rest area sooner. Completed rest
  walks update their sandbox center, preventing the return to the old stopping
  point. Searches remain tied to the original rest area to limit drift.
- Improved rest destination checks and excluded work, ritual and quest furniture
  from ordinary seating/activity choices.

## Loot scouting

- Added one designated loot finder, or the nearest available companion, to point
  out the richest reachable chest, body or valuable loose item in view.
- The finder walks over, searches briefly, then waits by the find with an overhead
  chest icon. They leave when you open/empty it or move away; they do not take it.
- The final wait is capped at 15 seconds after standing up. Ignoring an unopened
  find no longer leaves the scout parked indefinitely, and ending the wait does
  not trigger another find in the same area.
- Added category, distance, search-time and value controls. One find is selected
  per area, with checks for line of sight, reachable ground and suitable contents.
- Owned goods, player storage and dead followers are skipped. Scouting inside
  homes, inns and other inhabited interiors is off by default. Relax and temporary
  rest do not start new scouting trips.

## Catch-up and travel

- Automatic teleport catch-up now runs independently of travel updates, including
  while the player stands still, rides, sneaks, swims or fights.
- Distant companions get a two-second delay; companions in another cell or with
  unloaded 3D get three seconds for normal following first. Each recall has an
  eight-second cooldown.
- Added landing choices along the player's recent trail and connected nearby
  ground, with closer fallbacks for corridors, corners and slopes.
- Waiting companions, scenes and other protected states retain control. Manual
  Relax holds the camp; temporary rest allows a wider roaming distance.
- Party teleport uses the same landing search and reports companions left behind.
- Retained the accepted September 19 r3 travel baseline and its destination/
  clearance improvements. Later local movement experiments are not included.

## Experimental hand holding

- Restored **Classic** procedural hand holding alongside **TETHER** and made
  Classic the default. Hand holding itself remains off by default. Classic needs
  no generated clasp animations; an explicitly saved TETHER selection is kept.
- Switching methods releases the current grip and waits for pending physics
  cleanup before handing control to the other method.
- Fixed Classic's stationary grip release and improved approach/arrival handling.
- TETHER now holds the follower's arrival position and connects the hands before
  waiting for movement handoff. Removed the extra arrival dwell and gradual grip
  gap widening; the default wrist limit is 45, while saved values are preserved.
- Serialized setup, cancellation and cleanup more carefully, with retries for
  temporary pose mismatches and better timing diagnostics.

## Gestures and packaging

- Moved command clips and the GPMA extension into the separate optional
  **Command Gestures** download. The Main download no longer places partial GPMA
  patches or FPI-dependent signal configs in installations that do not want them.
- Corrected GPMA patch indentation and multiline arrays for Nemesis's parser.
  The add-on extends the full Offset Movement Animation mod; it does not replace
  that requirement. Regenerate behaviors after adding, updating or removing it.
- Improved OAR availability checks and diagnostics. Existing WWM OAR condition
  minimum versions remain 0.2.0 for compatibility.
- Shelved post-victory reactions and removed their menu controls and shoulder
  animation assets. A compatibility script remains for cleanup; it adds no barks.
- Included matching DLL, ESP, seven compiled scripts, updated guides, original
  artwork, complete source and SHA-256 checksums.

## Updating

Close Skyrim, back up your settings, and **replace** the previous main mod instead
of merging over it. Keep `SKSE/Plugins/Wayfarer.ini` if you want existing preferences.
Enable `Wayfarer.esp`; the compatibility filenames and save identifiers remain.
Disable old test overlays that could overwrite this release's DLL or scripts.

For gestures, also install the matching 0.2.2 Command Gestures add-on and follow
[Gesture setup](gestures.md). Replace old combined installs so obsolete GPMA,
signal and victory files are removed. Regenerate behavior output after changing
the gesture patch, including when switching to Main only.

Hand holding remains experimental. Katana's personal-dialogue opener can still
keep the conversation marker visible. Fresh Nemesis generation, in-game animation
playback and the supported runtime matrix have not been revalidated for this
release. See [Validation](validation-0.2.2.md) for the checks performed.
