# Victory reactions — candidate revision 5

On by default. Set `[PartyLife] bVictoryCelebrations=false` to disable.
The minimum remains **three distinct defeated enemies in one won encounter**.
Separate small fights do not add up; fleeing or unconfirmed enemies prevent a
victory. The tracked party must leave combat/searching for 1.25 seconds.

## Party participation and variety

All nearby eligible managed teammates are queued, including those not recorded
as combat participants. Starts are staggered. Actor order rotates each encounter,
and reactions are dealt in this order: shoulder recovery, applause, brow wiping,
hand inspection, then repeat. A four-person eligible party therefore gets one
of each, if equipment/state permit. A small party still reserves a shoulder slot.
An unavailable shoulder replacement falls back to applause.

| Equipment at playback | Requested reaction |
| --- | --- |
| Fully sheathed | Assigned shoulder recovery, applause, brow wipe or hand inspection |
| Drawn empty hands | Assigned vanilla reaction; shoulder slot uses applause |
| Drawn single sword or greatsword | Existing Civil War cheer, then assigned recovery after sheathing |
| Other drawn equipment | Best-effort brief native armed idle, then recovery after sheathing |
| Drawing or sheathing | Wait for transition to finish |

Armed reactions no longer consume or block the assigned sheathed recovery. No
weapons are equipped, unequipped, drawn or sheathed by this feature. The general
armed idle is equipment/behavior dependent and needs in-game validation; an
engine rejection immediately advances to waiting for sheathed recovery.

The subtle vanilla options are `IdleWipeBrow` (about 2.3 seconds) and `IdleExamine`
(about 6.2 seconds). The latter requires no shield or torch out. Sword/greatsword
cheers use the same 4.77-second clip; they are not separately authored salutes.
Shoulder recovery remains the original approved 6.5-second clip.

## Timing and interruptions

Each phase can wait fifteen seconds for movement cleanup, a still companion,
finished speech and stable equipment. Brief player unavailability retains pending
reactions. An accepted start gets half a second for asynchronous idle ownership
to settle; early ownership loss can retry within a bounded three-attempt budget.
The short generic armed idle does not retry merely because it finishes quickly.

Waiting, dismissed, excluded, distant, incompatible or unsafe actors cannot be
forced to react. The radius is 900 game units with at most 180 vertical units.
Human behavior graphs are accepted even when an undead companion lacks the
ActorTypeNPC keyword; creature graphs remain excluded. Combat, dialogue, scenes,
new orders, moving away and loading can interrupt. Normal travel resumes after
reactions and cleanup. Queuing the team is not a guarantee that every actor can
visibly animate during an interrupted encounter.

## Shoulder replacement and diagnostics

Shoulder recovery replaces the `IdleApplaud3` carrier using the actor-scoped OAR
condition `WWM_PostCombatShoulderRecovery`. It is true only for selected companions,
fully sheathed and out of combat. Selection is transient and clears on completion,
failed starts, interruptions and load. No save records or behavior patches are
added. OAR and the shoulder files belong to the main mod; the separate Command
Gestures add-on and behavior regeneration are not required for victory reactions.

The global `meshes/OpenAnimationReplacer/Walk With Me Victory/...` layout is valid:
OAR searches below meshes and strips the replacer/mod/submod portion to obtain the
original animation path. The layout alone does not establish a discovery failure.

`[Victory]` logs distinguish:

- Encounter outcome, queue size, waiting reasons and expired starts.
- Engine acceptance of a specific IDLE, which does not prove visible playback.
- `shoulder selection reached OAR=true/false` after startup. True confirms that
  OAR evaluated this actor's selected condition; it does not prove the clip won
  against every other replacer or remained visible for its entire duration.
- Loss of idle ownership and interruptions, with elapsed time.

Ordinary applause remains subject to other installed OAR replacers. No tavern
conditions are bypassed, and no third-party or game animation assets are bundled.
The new subtle options reference existing Skyrim IDLE records.

## Evidence and remaining playtest

Older September 16 logs show accepted applause requests losing ownership after
0.03 seconds. This motivated the startup grace and retry; it is not proof that
all later missing shoulders had that cause. A September 19 encounter queued four
companions after seven deaths, then cancelled pending reactions when the player
was temporarily unready. Pending reactions now retain their deadline.

Native unit tests cover the three-enemy threshold, cancellation, party variation,
equipment choices, bounded startup retry and per-actor OAR selection. The DLL and
vanilla records/clips are checked locally. Skyrim playback remains unverified.

Playtest a three-enemy fight with several companions, stand nearby and allow
natural sheathing. Check armed then sheathed reactions, shoulder visibility and
variety across two encounters. Repeat with bows, magic, dual wielding, a shield,
no equipped weapons and an eligible undead companion. Interrupt with movement
and renewed combat. Compare visible playback against `[Victory]` diagnostics.
