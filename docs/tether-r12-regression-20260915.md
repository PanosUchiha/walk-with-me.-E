# TETHER r12 regression and installation rollback

## Observed failure

User reported worse following and no handholding after installing r12.
Final stopped-game capture: `build/follower-captures/20260915-022641-711`.
Skyrim SE 1.5.97, process 26040. The follower log contains older sessions too;
filter by process ID before interpreting it.

The session logged five native handholding approach starts for Rumarin, with
no completed TETHER physics operation and no connected grip. Repeated travel
stalls affected Rumarin and other followers. These observations establish a
failed approach and travel stalls, but do not establish that Havok caused them:
the new physics setup never completed its first operation.

Source inspection shows connection requires simultaneous side/forward position,
heading, wrist-distance, line-of-sight, and eligibility gates. Native travel
alone was not demonstrated to satisfy them. The specific blocking gate was not
logged, so the precise connection failure cannot be recovered from these logs.
The five-unit arrival-radius change is a candidate for selected-partner travel
regressions; it does not explain stalls for every party member.

## Recovery completed

Restored the full pre-r12 installation from
`backups/installed-pre-tether-r12-20260915-021229` after Skyrim exited.
Retained current INI settings, MO2 metadata, and KEI997's license attribution
for the existing travel-speed hook reference. Removed the r12-only scripts by
replacing the complete installed directory, retaining the failed directory at
`backups/installed-failed-tether-r12-20260915-022732`.

Installed build: **Walk-With-Me-0.16.8-Rest-HUD-r3**.
DLL SHA-256: `786ebe5de3d4ee402a05ec63c5e1d88708618bea52785cf6e086f5045cfc0129`.
Receipt: `build/tether-r12-rollback-20260915-022732.json`.
All 117 installed payload files were hash-verified; INI and metadata matched
the failed installation byte-for-byte. Wayfarer remains enabled in MO2.
Victory reactions after three enemies and the temporary-rest toggle / larger
rest-resume icons remain present. No save files were changed.

This restores the preceding implementation; it does not establish that its
earlier handholding jitter is fixed. No in-game validation after rollback has
been performed. The working source tree and immutable r12 source archive retain
the experimental adaptation; they do not match the restored installed binary.

## Required before another adaptation test

- Log the approach rejection gate, root and wrist distances, relative position,
  heading, active travel package and requested arrival radius at a bounded rate.
- Establish ordinary following first, then isolate the selected-partner approach.
- Demonstrate an achievable stationary approach and arm pose before enabling
  the constraint. Relaxing wrist gates without an alignment method is unproven.
- Validate moving grip, turns, release and following recovery in the actual game;
  lifecycle unit tests and successful builds alone do not validate locomotion.

The failed behavior belongs to this Walk With Me adaptation, not a demonstrated
failure of KEI997's standalone TETHER mod.
