# TETHER handoff r6

## Inigo's recorded pause

The September 26 r5 log confirms a pause after the hands join:

| Session | Grip callback | Movement result |
| --- | --- | --- |
| `306637221` | 13:39:08.814 | Still on WWM's stationary arrival package at 13:39:10.068; separation released the grip at 13:39:10.668 before handoff completed. |
| `306637223` | 13:39:24.498 | Script cleanup acknowledged at 13:39:26.434; offset completed at 13:39:26.577, 2.079 seconds after grip. |

During the second session, the 13:39:25.698 sample reports player displacement
speed 88.1, follower displacement speed 0.2, and `offsetMovement=false`. Inigo
still ran WWM package `FECEA800`. After offset activates, his recorded movement
resumes and heading stays near the player's throughout walking and running.
The preserved log is `build/tether-handoff-r6/Inigo-before.log`.

## Change

The connected pair keeps its existing WWM travel route moving toward the
player's side while the serialized alias cleanup is queued. Previously the
handoff immediately discarded that route, left the arrival marker stationary,
and restored the broad ordinary stopping radius while the old package still ran.

- Acquisition still holds the arrival position until the grip connects.
- After connection, the approach target follows the player again. The tight
  arrival radius remains leased while this route is active.
- Native travel continues only for the selected actor while its alias still
  contains that actor and its exact WWM slot package remains current.
- The route uses current player speed for pace. A target change of at least
  8 units can repath after 0.15 seconds; a stationary target does not repeatedly
  restart pathfinding. Other travel retains its normal timing.
- `GetSlotFollower` still yields the actor to the script throughout handoff.
  The temporary route update cannot refill a cleared alias or reclaim a package.
- Cleanup completion, alias/package loss, cancellation and offset activation
  end the temporary route. Offset still waits for the ticketed acknowledgment,
  so an older `ClearKeepOffsetFromActor` cannot erase the new offset.

No actor position writes, extra offset calls before acknowledgment, or script
changes are added. Classic's handoff remains unchanged. The existing held-facing
and movement-renewal fixes remain active.

## Validation

Regression tests reproduce a delayed handoff while the existing route advances,
then check alias/package loss, cleanup acknowledgment, offset activation and
cancellation. They also check stationary targets, repath cadence and restoration
of the borrowed arrival radius. Build/test logs are under
`build/tether-handoff-r6`.

Both legacy and modern Release builds completed. All 26 CTest tests passed in
each build, including the delayed-handoff regression. `git diff --check` passed.

The script-to-engine transition between alias removal and offset activation
still takes time. This change addresses the measured long stationary wait while
the old WWM package owns movement; it does not establish zero-frame latency.

In-game check: enable TETHER for Inigo, pause to connect, then immediately walk.
The log should show `Handoff r6` at startup and `handoffTravel=true` while the
old travel package follows the moving target. Check follower movement during
`offsetMovement=false`, then normal offset walking, turning, stopping and release.
Test both sides and release while handoff is pending. Confirm ordinary following
resumes and no route update reclaims the alias afterward.

## Local payload

Use the legacy Release DLL for this Skyrim 1.5.97 installation. Install the
DLL and this note with Skyrim closed, preserving the personal INI, scripts,
ESP and generated clasp assets. The current working tree also contains ongoing
menu changes; include its missing pathfinder and crest SVG dependencies and
their attribution so the rebuilt DLL has the assets it references. Back up and
hash-check the old and new files.
Visual validation in Skyrim remains pending when the build is installed.
