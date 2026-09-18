# Victory reactions

On by default. Set `[PartyLife] bVictoryCelebrations=false` to disable.
After at least three distinct enemies die in one encounter, eligible companions
can briefly applaud, loosen a shoulder or cheer. Smaller separate fights do not add up.

The tracked party must leave combat/searching for 1.25 seconds. Only participating,
nearby humanoid followers can react. Starts are staggered and can wait up to fifteen
seconds for movement/animation handoff. Applause and cheers last at most 4.9 seconds;
the authored shoulder recovery gets its full 6.5 seconds
while safe to stop. A failed engine start retries every half second within that
window; drawing and sheathing must finish before playback.

| Equipment at playback | Existing animation |
| --- | --- |
| Fully sheathed weapons | IdleApplaud3, IdleApplaud2, IdleApplaud5 or shoulder recovery (25% each) |
| Drawn empty hands | IdleApplaud3, IdleApplaud2 or IdleApplaud5 |
| Drawn one-handed sword | IdleCivilWarCheer |
| Drawn greatsword | IdleCivilWarCheer |
| Other drawn weapons, spells or dual blades | Wait for sheathing, then use applause; otherwise skip |

Both swords reuse the same raised-arm cheer. This is not a separately authored
two-handed overhead salute. No equipment is forced on/off and the reaction never
forces combat, weapon drawing, or a sheathe. Changing equipment category or
starting to draw/sheathe ends the current reaction.

Waiting, dismissed, excluded or unsafe actors are skipped. Combat, dialogue,
scenes, a new order, moving away or loading cancels the reaction. It waits for
quest alias cleanup and restores normal travel afterward.

Uses Skyrim's existing IDLE forms. Shoulder recovery uses the `IdleApplaud3`
carrier with an actor-scoped OAR condition, `WWM_PostCombatShoulderRecovery`.
That condition is true only for companions selected by WWM, fully sheathed and
out of combat. The selection is transient and clears on completion, failed
starts, loss of ownership, interruptions and game load. It does not add factions
or records to saves. No behavior regeneration is needed. If OAR registration or
the authored assets are unavailable, selection falls back to the three applause
options. The shoulder clip never plays in a drawn-empty-hands state either.
Once selected, shoulder recovery waits for sheathing within the start window;
an armed cheer does not consume that pending choice. The other choices keep
their existing equipment-compatible behavior.

Ordinary IdleApplaud3 uses `npc_applaud3.hkx`, which the
installed Conditional Tavern Cheering mod replaces; OAR evaluates its normal
conditions. No tavern-only conditions are bypassed, and no mugs, tankards, copied
third-party clips, new behavior files or voices are added. The shoulder HKX is
the original user-approved v6 animation, authored from the user's video reference.

Cleanup stops only the idle owned by this feature; another mod's replacement
is left alone. Replaying the same idle from another mod cannot always be distinguished.

Test two versus three enemies, fleeing/live enemies, interruptions and save/load.
Check transitions with your animation replacers and separately with a sword and
greatsword still drawn. `[Victory]` logs now record the encounter outcome, waiting
reason, expired starts, selected animation and accepted playback. Following
rollback r1 waits for the original WWM alias-release lifecycle before playback.
Victory revision 3 requires a completed cleanup pass after queuing and a cleared
companion alias. Later routine sync passes no longer block a released companion.
The fifteen-second start window allows more time for bleedout recovery and
natural sheathing. Unsafe actors still wait; equipment is never forced.

Revision 4 adds the randomized shoulder recovery and per-gesture duration.
Its startup log reports whether the OAR shoulder condition and files are available.
An accepted shoulder start logs `Shoulder recovery` and a 6.5-second duration.
The sheathed test matrix includes no weapons equipped, sword, greatsword, bow,
magic, and starting to draw during the gesture. In-game body/armor appearance
and interruption timing still need observation on the updated build.

The September 15 four-enemy log confirmed the encounter and queued Katana,
Rumarin and Megara. Katana's command was accepted; Rumarin recovered from bleedout,
and Megara waited on movement/equipment before both exceeded the old eight-second
window. Acceptance alone does not confirm visible playback. Revision 3 logs lost
idle ownership and equipment/state interruptions to diagnose that distinction.

The local September 15 build verifies record IDs, decodes the vanilla clips and
passes native tests. Weapon retention and visible animation remain in-game tests.
The Civil War cheer is 4.7667 seconds long, contains no attack/sheath animation
annotations, and its vanilla IDLE record has no combat-only condition. Those facts
support reuse after combat but do not prove every replacer's behavior.

For a direct visual check, select an eligible companion in the console and run
`playidle IdleCivilWarCheer` with a sword/greatsword drawn, or
`playidle IdleApplaud3` with weapons sheathed. This bypasses only WWM's encounter
trigger; it does not validate detection, handoff or cancellation. Resume ordinary
play and test a three-enemy fight for the complete flow.
