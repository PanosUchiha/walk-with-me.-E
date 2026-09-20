# Testing

Run CTest after a build. Then use a separate save for these game checks.

## Basic checks

- Add and remove followers through Party and dialogue. Save and reload.
- Try all six orders with keyboard, mouse and gamepad. Cancel and drag clicks too.
- Test Shift/Ctrl/Alt bindings, both sides of the keyboard, Escape and Delete.
- Walk, run, turn, stop and resume. Followers should catch up without repeated stops.
- Try stairs, narrow doors, towns, rocks and exterior cell boundaries.
- Check temporary rest, its return range and Relax. Relax should wait for an order.
- Start dialogue, combat and scripted scenes. Those must keep control.
- Disable the mod or choose Vanilla. Travel, rest and gestures must stop.
- Reload a save and fast travel. No old order or celebration should replay.

## Follower checks

Test NFF and custom followers separately. Check recruitment, waiting, dismissal,
exclusions and enforcement settings. Inigo's negative WaitingForPlayer value means
 dismissal even if teammate status remains. Check Serana outside NFF too.

Walking banter must not interrupt scenes with their own packages. Paired roles
must not change the quest's follower aliases. Test single-file paths.

## Animations

Try Lead, Companion and Rear in third and first person. Test headtracking with
and without TDM. Interrupt with weapons, dialogue, camera changes and save/load.

Enable hand holding for one follower. Check standing, walking, running, turning
and release on each side. Check that other followers keep normal movement.
Revision 18 retains native close approach, remembers script handoff completion
and adds authored clasp animations. Follow tether-r18-handoff.md, especially
timing, wrist orientation, interrupted acquisition and following recovery.
Record video; unit tests do not establish visual quality.

Defeat two enemies: no celebration. Defeat three in one fight: eligible followers
may applaud with empty hands or cheer with a drawn sword/greatsword. Test both
weapon types, sheathing during the reaction, and a spell/bow waiting for sheathe.
Leaving one alive, renewed combat, a new order or loading
must cancel it. Check that borrowed AI and animation state is restored.

## Reports

For the pending-reaction and confined-path test build, follow
[reactions-pathing-review.md](reactions-pathing-review.md), including both
directions through Lost Prospect Mine in Natural, Lead, Companion and Rear.

Include game version, follower/framework mods, location, reproduction steps and
`Walk With Me.log`. For startup failures, include `skse64.log` too.
