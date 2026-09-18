# Follower diagnostics: followers-diag-20260914-r1

This build observes all managed followers during ordinary play. It does not
change movement decisions, speed tuning, AI ownership, save data, or recovery.
The baseline includes the installed September 13 travel repath changes and
September 14 hand-holding revision 11.3.

## Capture

The SKSE log directory contains `Walk With Me-followers.log` and numbered
rotations. Each line carries the build and process ID; incidents also identify
the actor, save-load generation, incident sequence, and monotonic timestamp.
The ordinary `Walk With Me.log` retains general errors, eligibility decisions,
hand-holding diagnostics, and global state changes.

The observer runs at approximately 0.15-second game-update intervals, including
recovery and managed actors rejected from travel. It pauses with paused menus.
Each actor retains up to 10 seconds / 256 entries of samples and events in memory.
MovementBlocked during travel, 4.5 seconds of blocking outside travel, three
seconds of stationary travel far from a target, a recovery release, or failure
to find a travel corridor starts an incident. Initial history is written with
its original monotonic times, followed by a 10-second burst and approximately
one sample per second while the incident persists. Important events can produce
additional lines. A normal health snapshot is written every 30 seconds.

Samples include actual displacement and velocity, movement requested by the
engine, target and submitted route, packages, aliases, cells/worldspace, recovery,
animation graph values with availability flags, AI state, scene/furniture/combat,
hand attachment, bump state, and avoid-wait timer. Native travel and sandbox
reevaluations carry their decision inputs. Alias transitions are observed;
Papyrus reevaluation calls are not individually hooked.

During incidents the observer records up to eight nearby high-process NPCs once
per second and the player's distance. Nearby means candidate, not proven blocker.
Every five seconds of an eligible active route, a short collision/navigation
probe uses an independent avoidance state. Its result never changes the live
route. These static probes exclude actors, as the existing steering probes do.
They inspect the collected local navmeshes and cannot prove global reachability.
Diagnostic probe results are explicitly marked `observationOnly=true`.

Recovery requires two seconds of displacement toward the previous target or
stable arrival. Ownership alone, target movement, sideways jitter, and pauses
cannot establish recovery. Context changes, unloading, removal and save reload
are distinguished from successful recovery. Missing end records at process exit
remain unresolved; a crash cannot be inferred from that alone.

## Retention and analysis

Follower logs rotate at 8 MiB with 32 backups (about 264 MiB maximum), and ordinary
logs at 16 MiB with eight backups (about 144 MiB). Starting a new game process
rotates instead of truncating the prior log. Retention is bounded, so old sessions
eventually age out. Incident data is flushed at least once per second while
active, and on incident boundaries; a crash may lose the most recent buffered
records. These are synchronous logs, so runtime performance still needs a playtest.

Run `tools/collect-follower-diagnostics.ps1` to preserve the retained logs,
current INI and installed DLL hash in a new timestamped capture directory. It
also includes existing SceneDirector/SKSE/Papyrus and recent crash logs when
available. It does not enable global Papyrus logging or delete source logs.
For another install, pass `-InstalledMod` and `-GameDocuments` explicitly.

For analysis, group by build, PID, generation, actor and incident. Sort history
by `mono`, not its later emission timestamp. Correlate transitions and native
reevaluations before blocking, compare nearby NPCs and probe results, and check
that retry cycles end in sustained movement. General log errors and crashes
remain separate evidence. No observer can detect every possible follower bug.

## Validation

The diagnostic tests exercise ordinary idle behavior, unavailable context,
blocking, repeated recovery, sideways motion, pauses, stable arrival, unresolved
context changes, bounded history, and persistent-incident output rates. The full
existing test suite must also pass. Compilation and these tests do not replace
an in-game check of engine fields, overhead, or physical movement.
