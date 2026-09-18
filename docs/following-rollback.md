# Following rollback r1

This combined build restores the 0.16.7 following implementation while retaining
later victory and rest/HUD features. It is identified as 0.16.8 with a distinct
`[Following] Rollback r1` startup log, so it cannot be mistaken for the original
0.16.7 release or the failed coexistence/TETHER builds.

## Restored movement

- Original travel refresh and AI-reset behavior, route timing and stall recovery.
- Original temporary follower/marker alias lifecycle and matching Papyrus scripts.
- Original package conditions, framework priority, NFF/custom-follower enforcement
  and catch-up behavior. The experimental movement-permission globals are removed.
- Original 0.16.7 handholding and shared travel-speed hook; no TETHER grip backend.

Baseline: commit `99fa995`, consisting of release `6d86178` plus the subsequent
manual-enrollment fixes. The latter changed follower eligibility, not travel
refresh or movement. The controller's only additions to that baseline are a
startup identifier and the narrowly scoped drawn-weapon exception for victory.
Ordinary travel still rejects drawn weapons exactly as before.

## Retained features

- Victory after three confirmed enemy deaths in one encounter. Empty hands use
  existing applause; drawn swords/greatswords use the existing Civil War cheer.
- Temporary rest can be toggled, settings save immediately, and manual Relax
  remains available when automatic rest is disabled.
- Temporary-rest and resume HUD icons retain the 75% size increase.
- Manual enrollment, follower dialogue, command gestures, formation options,
  rest activities and all existing configuration preferences.
- KEI997's TETHER credit and MIT notice for the existing speed-hook reference.

Victory now waits for the restored alias-release lifecycle instead of the
withdrawn movement-permission gates. The ESP keeps all original package, alias,
dialogue and quest IDs. Install the DLL, ESP and four matching scripts together.
No save files or generated behavior graphs are modified.

## Validation and limits

The build/package receipts record native tests, compiled scripts, structural ESP
checks and payload hashes. Tests cover the restored travel behavior and retained
victory logic. They cannot establish in-game pathfinding or animation quality.
The original handholding's known animation limitations remain.

After restarting Skyrim, check continuous following, stop/resume and a turn on
the route that regressed. Check temporary rest on/off and a three-enemy fight.
The previous installation and complete pre-rollback source are backed up.
