# 0.16.8 — following rollback r1

The current build restores 0.16.7 following, including AI refresh, route timing,
alias lifecycle, framework priority/catch-up behavior and original handholding.
Victory revision 2, temporary-rest controls, HUD revision 3 and manual enrollment
remain. See [following-rollback.md](following-rollback.md).

## Historical experimental changes (withdrawn unless retained above)

- Separate persistent WWM enrollment from conditional movement permission.
- Preserve existing aliases and markers through temporary handoffs and restore
  valid enrollment from saved aliases after load.
- Use ordinary AI evaluation; stop unowned replan/recovery retries and stop
  clearing another mod's keep-offset state.
- Respect NFF Very High priority, active task aliases and special packages;
  leave NFF follower catch-up to NFF.
- Add per-slot permission, combat, death and wait conditions to every package.
- Add handoff diagnostics, ownership/slot tests and ESP condition validation.
- Victory revision 2: require three defeated enemies; reuse tavern applause with
  empty hands and the Civil War cheer with drawn swords or greatswords. Wait for
  stable equipment and movement handoff; log skipped, delayed and expired starts.
- Legacy diagnostic builds report `avoidWait=-1` where older headers do not
  expose that optional timer, fixing compilation without assuming an offset.
- Rest HUD revision 3: label the existing automatic-rest switch **Temporary rest**
  and explain that disabling it leaves the Relax command available. Enlarge both
  temporary-rest and resume HUD icons by 75%, preserving existing HUD scaling.
- Handholding revision 12: adapt TETHER's physics grip and elastic speed behavior
  with credit to KEI997 (MIT), checked setup/cleanup, and WWM native pathfinding.
  This replaces the previous per-frame body-position and arm-IK override backend.
  No TETHER animation assets are bundled; see tether-handholding.md for testing.

Install DLL, ESP and scripts together. All existing form/alias IDs are preserved.
This is a staged test build requiring the playtests in framework-coexistence.md;
it is not a confirmed CTD fix or a compatibility guarantee for every framework.
