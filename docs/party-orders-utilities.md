# Party Orders utilities r1

The command wheel has a mouse action row with **Teleport party** and
**Temporary relaxation**. The existing six orders, keyboard shortcuts, gamepad
directions and separate Companion control popup remain available.

## Teleport party

The button shows the current eligible companion count. Clicking closes the
wheel and queues one recall after blocking menus and dialogue have closed.
The request expires after five seconds and is cancelled on a save change or
unsafe player state. Repeated clicks cannot queue multiple recalls.

Recall considers managed companions, explicit registrations and active vanilla
or NFF follower slots, up to the configured party limit. It can bring those
references from another cell. It uses the same enrollment checks as the party:
nearby housecarls do not become eligible merely because they live with you.
An unknown custom follower must first be imported with Companion control.
Followers unavailable through those sources cannot be recalled.

Waiting, excluded, dead, disabled, hostile, mounted, swimming, restrained,
unconscious, bleeding-out, seated, combat and scene/speaking actors are skipped.
The player must also be standing outside combat/scenes with movement enabled.
Destinations use the existing catch-up ground/navigation checks and avoid other
actors and reserved landing points. Companions without a clear destination stay
where they are. A notification reports how many were placed. Successful recall
clears old travel/rest anchors so the current order can resume nearby.

This explicit action does not change the separate automatic teleport catch-up
preference. Custom frameworks may subsequently reassert their own AI behavior.

## Temporary relaxation

The switch changes the existing `Sandbox/bAutomatic` preference and saves it
immediately. ON permits automatic rest when the player stops; OFF releases an
automatic rest session. Explicit Relax continues to work with this switch off.
The switch reflects the saved setting and displays a saving state while queued.

## Presentation and verification

The utility cards use animated entrance/hover lift, Lorc's Teleport and Campfire
artwork from Game-icons.net, and a sliding rest switch. Captions show the live count, disabled reason or saved
rest state. The wheel scales to both viewport dimensions so the new action row
and footer fit smaller and ultrawide displays. The controls are mouse-only.

Both modern and legacy native builds pass 19 CTest suites. Coverage includes
recall deferral, expiry, save-change cancellation and single dispatch; responsive
layout bounds; actual ImGui mouse clicks and drag cancellation across wheel and
utility buttons; and automatic-rest versus explicit-Relax behavior. Menu Framework
export validation passes. Offline previews render the actual wheel/card drawing
code and were visually checked with the switch on and off.

This build has not been playtested in Skyrim. In-game checks: recall an active
vanilla/NFF companion from another cell; confirm a dismissed housecarl and waiting
follower stay put; try crowded ground; toggle temporary relaxation while resting;
confirm manual Relax still works with the toggle off; and check mouse and gamepad
orders at your normal resolution. Restart Skyrim after installing the DLL.
