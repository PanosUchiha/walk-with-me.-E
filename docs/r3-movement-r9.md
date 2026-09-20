# Restored r3 movement candidate r9 — September 20, 2026

The user identified September 19 r3 as the best movement build after the later
experiments continued to freeze companions. This candidate restores that entire
movement baseline and retains the later animation work.

## Provenance and scope

The baseline is `release/lost-prospect-review-20260919-r3`, with source and main
archive checksums verified. Its DLL SHA-256 is
`CAA730A047E9D333A0BA7A10E0478DE8BE942612CE9BA1E0E79A41F76940DA83`.
That matches the September 19 16:25 installation record and the subsequent
pre-turn backup. The new r9 DLL has a different hash because it retains later
victory work and identifies itself in the startup log.

Restored code includes travel planning, individual departure timing, formation
control, navigation queries and rest reachability, plus their original tests.
Formation control matches r3 except for the startup banner. Its header retains
only the later victory reaction fields in addition to r3's fields.

Removed movement experiments include actor-origin corner routing and its cache,
the larger navigation search radius, overlapping startup delays, r7 ownership
and cleanup fences, and r8's blocked-movement reset guard. This is a complete
movement baseline restoration, rather than a new combination of those policies.
Existing r3 corridor and travel diagnostics remain available.

Later victory code and its tests are unchanged: the three-enemy threshold,
armed reactions, sheathed shoulder/clap/wipe/examine variety, party assignments,
and animation startup retries remain. Main and Command Gestures remain separate
ZIPs. Animation, Papyrus and behavior patch payloads are unchanged from r8.

## Limits and repeat test

The user's comparison favors r3, but r3 still had narrow-passage and startup
problems. Restoring it also restores its original pending-assignment stall timer,
package refresh cadence and recovery behavior. This candidate is not a proven
fix for every route, and no new slope correction is being claimed.

Use the same save, formation, hand-holding setting and party for comparison:

1. Walk east from Whiterun toward the waterfall, including the uphill section.
2. Cross the bridge beyond Honningbrew and follow the turns into the hills.
3. Repeat Lost Prospect Mine's thin passage with Lead selected.
4. Win an encounter with at least three enemies and check armed and sheathed
   reactions after leaving combat.

Record where movement fails and whether followers wait, recover, or continue.
The startup log should read `Baseline r9: September 19 r3 movement restored`.
The removed r7/r8 diagnostic records are not expected from this build.

Local installation preserves the user's INI, generated clasp clips, first-person
gesture extras, enabled mod order and existing Pandora output. With unchanged
behavior inputs, this update does not require regenerating Pandora.
