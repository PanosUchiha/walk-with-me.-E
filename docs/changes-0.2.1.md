# Walk With Me 0.2.1

This release adopts the movement build preferred by the user after in-game
comparison and keeps the animation fixes developed alongside it.

## Movement

- Retains September 19 r3 destination selection, formation control, departure
  timing, package refresh and recovery, as restored in candidate r9.
- Keeps that baseline's narrow-passage collision checks, turn handling and
  expanded travel diagnostics.
- Excludes the later r5–r8 corner-waypoint, departure, ownership and reset-cadence
  experiments. No new movement changes were made when promoting r9 to 0.2.1.

## Animations and packaging

- Fixes the GPMA patch formatting used by Nemesis's line-based reader.
- Ships command signals and their behavior patch as a separate optional
  **Command Gestures** ZIP. See [gestures.md](gestures.md) for requirements.
- Retains OAR condition registration fixes, the authored shoulder recovery and
  varied post-combat party reactions, including armed reactions where supported.
- Keeps the minimum of three defeated enemies per encounter and animation-start
  retries. Reactions do not force companions to sheathe their weapons.
- Keeps the OAR condition minimum at 0.2.0 for existing clip compatibility; DLL
  and Windows resource metadata report 0.2.1.

## Updating

Replace the main installation, preserve your INI and locally generated clips,
and install the separate gestures add-on if desired. When upgrading from the
original combined download, follow the migration in [gestures.md](gestures.md).
The r9-to-0.2.1 update changes no scripts, ESP, animations or behavior patch
inputs, so an existing r9 Pandora output does not need regeneration.

The user accepted r9 as the best version tested; this is not a claim that every
narrow passage or route is fixed. See [baseline-0.2.1.md](baseline-0.2.1.md) and
[validation-0.2.1.md](validation-0.2.1.md). Earlier release history is in
[changes-0.2.0.md](changes-0.2.0.md).
