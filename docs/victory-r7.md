# Victory revision 7 — temporary sheathing and restoration

Followers temporarily sheath, wait for 0.35 seconds of stable sheathed state, and request one separate reaction. The four-way victory / arm-stretch / shoulder / clap rotation is unchanged. The player is never animated or forced to sheath.

Accepted animation selection remains latched through asynchronous startup, with a 0.75-second startup window. A transient weapon-state change alone no longer deletes it after one frame. Once accepted, an interrupted reaction is never replayed during that encounter. Combat and unsafe actor states still interrupt.

Normal completion, failed startup, timeout, cancellation and controller reset all queue cleanup for a follower whose sheathing or playback began. Cleanup stops the owned/queued idle, briefly allows the graph to settle, then checks the player's CURRENT weapon intent. Drawn/drawing player: draw. Sheathed/sheathing player: remain sheathed. Renewed combat: draw immediately. Package reevaluation returns the follower to its usual AI. Unchanged weapon intent is not repeatedly issued. Cleanup is bounded and does not retain a permanent movement lock.

The engine's IsWeaponDrawn helper also returns true while sheathing. Restoration instead uses WEAPON_STATE intent, preventing a player who sheathes during the reaction from making followers draw again at completion.

The failed revision-6 log showed three different requested clips followed by lost ownership after 0.04 seconds. It did not record the rejection reason or prove custom playback; revised diagnostics include rejection, equipment-change and OAR-observation details. OAR registration and asset availability were successful in that log. These findings do not establish a verified in-game fix.

Release build succeeds; 19/19 CTest suites pass, including draw/sheath-transition intent, combat priority, settlement timing and rotation checks. No HKX, OAR configuration, ESP, Papyrus or behavior-generator changes in this revision. No Pandora run is required. Skyrim runtime playback, follower-framework interaction, and recovery from the failed session still require an in-game test, preferably from a save before that celebration.
