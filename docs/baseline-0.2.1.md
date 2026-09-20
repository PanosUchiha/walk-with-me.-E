# Accepted baseline for future feature work: 0.2.1

On September 20, 2026, the user accepted installed candidate r9 as the best
version tested and requested that it be committed as 0.2.1 before starting a new
conversation for feature updates. Preserve this behavior as the comparison
baseline for future changes.

## What is accepted

- Movement: the September 19 r3 implementation, restored in r9. This includes
  formation travel, departure timing, navigation queries, AI refresh and recovery.
- Victory: the later r5 reactions and OAR fixes retained in r9. The three-enemy
  minimum remains intentional, with varied armed and sheathed reactions.
- Packaging: Main, Command Gestures and complete Source are separate ZIPs.
- User settings and locally generated clasp/FPI extras remain outside the public
  release. Existing Pandora output works with the unchanged behavior inputs.

The r9 DLL accepted by the user has SHA-256
`15A14A0EAD5F9643EA3C57F21AA85C3781CDFE31F0A819A83440CD61AA0EF999`.
The 0.2.1 DLL has new version metadata; the first-party `src`, `include` and test
files are unchanged from the r9 source archive. The startup movement banner
still identifies `Baseline r9`; the plugin version identifies release 0.2.1.

## Avoid reintroducing failed experiments

The r4 sharp-turn recovery change was rejected. The r5–r8 actor-route corner
waypoints, revised departure delays, ownership/cleanup fences and reset-cadence
guard were removed when restoring r3. Their documents remain as historical
evidence, not instructions to reapply them. See [r3-movement-r9.md](r3-movement-r9.md).

Earlier problem areas were Lost Prospect Mine's thin passage, sharp turns,
the bridge and hills beyond Honningbrew, and the eastern Whiterun waterfall
route. Hand-holding was enabled, but freezes affected multiple companions.
The evidence did not establish slope correction or hand-holding as the sole
cause. No new slope correction is included in this release.

The user's acceptance is a comparative playtest result, not exhaustive navmesh
validation. Future feature work should preserve this baseline and compare
movement changes on the same save, party, formation and routes.
