# Sharp-turn recovery review — 2026-09-19, r4

**Withdrawn after in-game regression.** The user reported worse following.
At 17:21 the captured r4 log still shows prolonged MovementBlocked holds and
Skjel motionless with WWM ownership despite repeated normal evaluations.
Removing AI resets and holding the destination did not resolve the underlying
failure. The earlier reset hypothesis was incomplete; do not treat it as a
confirmed cause or deploy r4 again. The installed mod and the four source/test
files changed for r4 were restored to r3 on September 19 at approximately
17:22. Settings and local animations were preserved. R3 still has the original
sharp-turn and mine problems and is only the previous baseline.

Regression evidence: `build/r4-regression-20260919/Walk With Me.log`.
Installed r3 DLL SHA256:
`caa730a047e9d333a0ba7a10e0478de8be942612ce9ba1e0e79a41f76940da83`.
The r4 archives remain as evidence, not a recommended download. The historical
description below records the withdrawn experiment.

Evidence: `build/sharp-turn-review-20260919/Walk With Me.log`, captured after
the r3 test. `[CorridorPosition]` and `[TravelRetry]` confirm the new code ran;
the old startup compile timestamp came from a translation unit that had not
needed rebuilding.

At 16:35:50–55 all four companions became stationary with runtime package
type 36 and procedure 44. Both values name `kMovementBlocked` in the pinned
CommonLib `TESPackage.h`. Several corridor checks reported usable=true,
zero navmesh rejects, and no physical blocker. Those checks cover destinations
from the player, so they do not establish that each actor's route was clear.

The trace records repeated `[TravelRetry] ... ownsPackage=false` at roughly
one-second intervals. `ApplyTravel` called `EvaluatePackage(false, true)`;
CommonLib's declaration names the second parameter `a_resetAI`. Thus WWM was
requesting AI resets during Skyrim's blocked-movement interrupt. It also moved
the travel marker as the player turned. These are confirmed behaviors that
could disrupt native recovery; the logs do not prove what first blocked the
actors or which system caused their reported brief lift above the floor.

The first releases occurred at 16:35:54.827–16:35:55.848. By 16:35:58.531
Rumarin and M'rissi were moving under NFF's normal follow package. This fits
the reported several-second freeze and restart. No `[Catchup]`, `[PartyRecall]`,
or `[CompanionRecovery]` events occur in the captured sequence, so attributing
the reported repositioning to WWM teleportation would be unsupported.

Changes:

- Routine travel evaluations use `EvaluatePackage(false, false)`.
- Repath requires actual WWM package ownership, including initial submissions.
  The existing script alias handoff performs initial package acquisition.
- During `kMovementBlocked`, hold the existing marker and clear the speed
  override. Do not generate another target or evaluate the package.
- Keep bounded stall recovery if the interrupt never resolves; no new teleport,
  actor reset, or push is introduced.
- Startup identifies `Sharp-turn r4`; `[TravelBlocked]` logs the interrupt.

Validation: Release DLL built and all 19 configured CTest targets passed.
Regressions cover missing-route ownership, no timed retries against other
packages, stable markers through a turn during blocked recovery, resuming after
the interrupt, and eventual release for persistent blockage. These tests do not
run Skyrim's AI. Sharp turns, mine bends, and a stationary arrival still require
an in-game check. Existing narrow-navmesh and prediction changes remain in r4.
