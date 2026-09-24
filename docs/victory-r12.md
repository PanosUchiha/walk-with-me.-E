# Victory revision 12

React at the follower's sheath position before returning to the player. After
natural sheathing, dispatch a temporary SetDontMove hold before waiting for the
movement-alias cleanup. Wait for acknowledgement before playing the gesture.
Remove the extra 900-unit/180-height arrival restriction; normal follower
eligibility and configured release distance still apply.

The helper releases movement when its actor/ticket no longer owns a pending or
playing reaction, when combat resumes, weapons draw, a scene starts, or the actor
dies/unloads. It also has a 200-iteration failsafe with 0.1-second waits. It then
reevaluates the normal package. Previously movement-blocked actors are skipped.
Load/reset clears the reaction list, so resumed helper stacks release their hold.
No AI disable, teleport, forced sheathing, or weapon restoration is used.

Voice lookup now searches loaded CombatToNormal topics and their conditioned
responses, including unnamed topics from custom follower mods. Installed files
contain 3DNPC's 3DNPCGenericCombattoNormalFix and Katana's unnamed post-combat
topics. The previous vanilla-only lookup never reached these. Requests still
respect active quests and response conditions; at most one is dispatched after
a completed gesture. Missing matches are logged. Dispatch is not proof that an
audible response played; verify in game. Remove the stale dialogue-target check
from the helper while retaining scene/combat/weapon guards.

Skjel's graph is Actors/Draugr/DraugrSkeletonProject.hkx; human OAR clips remain
excluded for that incompatible rig.

Validation: Release build, all 19 CTest suites, new hold lifecycle checks for
acknowledgement, stale tickets, deadline, active duration, interruption, and
non-revival. Papyrus compiled with zero errors/warnings. In-game verification
pending. Install the updated DLL and WayfarerVictoryVoice.pex together; no
Pandora run required.
