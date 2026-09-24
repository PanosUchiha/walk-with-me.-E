# Classic stationary grip regression — r3

The September 21 playtest showed six consecutive Classic attachments to Rumarin
(250AB6EC) immediately followed by `follower travel released`:

| Attached | Released | Duration |
| --- | --- | ---: |
| 22:56:42.164 | 22:56:42.310 | 0.146 s |
| 22:56:44.643 | 22:56:44.743 | 0.100 s |
| 22:56:48.525 | 22:56:48.592 | 0.067 s |
| 22:56:51.622 | 22:56:51.734 | 0.112 s |
| 22:56:55.472 | 22:56:55.573 | 0.101 s |
| 22:56:56.689 | 22:56:56.799 | 0.110 s |

The first release includes `no usable travel corridor; yielding`. The diagnostic
is throttled, while all six releases follow attachment before a full holding
trace can be emitted. The native approach repeatedly starts again and can
choose a different side, explaining the repeated reaching/crossing attempts.

## Cause and fix

The Classic integration omitted the original grounded fallback after the full,
half and quarter-distance locomotion route probes. At rest, Classic's forward
lead is zero. Ordinary corridor tracing rejects that zero-length route, causing
the travel controller to release the follower and thereby cancel the new grip.

The original implementation falls back to `AttachmentPoint(pos, pos)` after
those probes fail. r3 restores that behavior through a shared locomotion-target
helper used by the controller and regression tests. Valid standing ground keeps
the gait marker and attachment alive; missing ground still fails. Classic still
checks the actual movement step, collisions, reach and separation each frame.

The tests exercise real corridor and ground-step tracing on a slope: 60 stopped
updates, resuming movement, a blocked forward marker, and missing ground. Modern
and legacy Release DLLs build and pass all 20 CTest suites. Existing conversion
warnings remain. No post-fix in-game result is claimed; this addresses the
observed immediate release loop, not every possible pose/animation problem.

## Test update

The r3 ZIP includes the corrected DLL and the same matching scripts as r2, so it
can be installed after the 0.2.1 base. Classic remains the default. TETHER retains
the latency r2 changes. No settings, ESP, animation or behavior rebuild is needed.

With Rumarin selected, use Classic and pause beside him. Confirm the grip lasts
while idle, then walk, stop, turn and resume on both sides. The log should show
`Sticky` holding traces instead of repeated attachment/release pairs. Draw a
weapon or disable hand holding to check normal release, then reconnect.
