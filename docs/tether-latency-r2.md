# Classic default and TETHER latency r2

The method selector now says **Classic**, and Classic is the default for new,
missing or invalid settings. Explicit `sMethod = tether` choices are preserved
by the settings loader. Local installation sets this user's method to Classic
as requested while retaining the selected companion and all tuning values.

## Recorded delay

The September 21 22:27-22:29 game log shows these successful TETHER attempts:

| Session | Alias/package cleanup | Handoff request to connected grip |
| --- | ---: | ---: |
| 930332503 | 2.679 s | 3.431 s |
| 930332504 | 0.857 s | 1.528 s |
| 930332509 | 1.432 s | 2.208 s |

These times exclude walking to the player's side. Another attempt failed at
operation 1 after creating the player's ragdoll body. The previous Boolean
script result did not distinguish invalidated contact from engine failure, so
the exact reason for that failure cannot be recovered from the old log.

## Changes

- An idle alias sweep services a priority handoff directly from the request
  stack. A busy sweep still owns alias mutation and services requests at safe
  boundaries. There is no second concurrent alias-cleanup stack.
- Native handoff validation combines ticket, dialogue, actor and alias checks
  in one call. Priority work precedes ordinary slot/rest queries. Ordinary
  slot cleanup acknowledges completion before unrelated social queries.
- The script distinguishes a temporary positioning mismatch from failure,
  returning a deferred result only before any engine mutation. Native code
  retains acquired resources and retries when the pose is ready, with the
  existing overall acquisition timeout. Cancellation still drains resources.
- Follower body creation waits for its current settled pose even if the player
  body already exists. Bodies, constraint and offset teardown remain serial;
  failed removals retain ownership. Distance/visibility/skeleton checks and the
  fresh-frame facing gate remain in force.
- Logs include cleanup elapsed time, each script callback duration, deferrals,
  and approach/setup/total time on connection.

## Validation and installation

Modern and legacy Release DLL builds succeeded; all 20 CTest suites passed in
each. Tests include deferring each setup step, retaining earlier resources,
cancelling during a deferral, rejecting cleanup deferral, and Classic as the
default. All three changed Papyrus scripts compile without errors or warnings.
Existing integer-to-float compiler warnings in party_life.h remain.

This is a candidate latency repair. No post-change in-game timing or animation
result is claimed. Test TETHER with the same partner, standing still in third
person, on both sides. Compare cleanup and total connection times, including
release/retry and changing back to Classic during acquisition. Some delay
remains for actual walking, native script scheduling and hand alignment.

Install the r2 update after the 0.2.1 base with Skyrim closed. It includes the
DLL plus matching Wayfarer, WayfarerQuestScript and WayfarerTether PEX/source
files; install them together. Keep your INI and generated clasp assets. No ESP
change or behavior regeneration is needed. A saved explicit TETHER selection
can be changed to Classic in Travel. The local installation backs up every
replaced file and the INI before setting this user's selection to Classic.
