# Conversation icons during rest

A small ivory chat emblem, using the wheel and HUD's silhouette style, appears
above a managed companion who has an
unheard conversation available. It is enabled only while **temporary rest** or
manual **Rest / Relax** is active. Resuming travel, disabling Walk With Me, or
releasing a companion removes the indicator. Combat, scenes, dialogue and menus
hide it. Off-screen, distant and obstructed companions do not get a marker.

Toggle **Show chat icons** in **Interface > Follower icons**, or set
`[Interface] bRestDialogueIcons = false` in `Wayfarer.ini`. Existing INIs default
to enabled. The option is independent of the order emblem setting.

Icons pop up over 0.42 seconds, then float gently at about 80% opacity, with a
soft shadow for contrast. Turn off **Animate chat icons** in the same group
(`[Interface] bAnimateChatIcons = false`) for a still translucent icon.
Existing INIs default to animation on. Animation runs on the render thread,
keyed by companion ID; hidden or missing markers are cleared immediately.

## Detection

The scanner works from the loaded dialogue records for all managed companions;
it has no follower allowlist and needs no companion-specific patch.

- Considers player conversations in accessible top-level or blocking branches.
  Running quest conditions and response conditions use the companion as speaker,
  the player as target, and the owning quest for alias resolution.
- Respects the first qualifying response, explicit speaker restrictions,
  exclusive branches and blocking branches. A repeatable heard response cannot
  expose an unheard, lower-priority alternative.
- Follows explicit outgoing links only through unheard responses, for example
  a silent hub leading to speech. A heard/grey topic ends the search: unused
  alternative replies inside an old conversation do not count as a newly
  offered conversation. Cycles and oversized trees are bounded, and Goodbye
  responses end traversal.
- Uses Skyrim's saved `saidOnce` state, including shared responses. Opening a
  conversation invalidates the marker; closing it triggers a fresh scan. An
  icon can remain if another unheard topic is still available.
- Requires authored response content. Empty internal topics, silent responses
  and punctuation-only placeholders cannot create an icon. Shared INFO chains
  are resolved before deciding; an empty local response may legitimately use
  a shared spoken response. Cycles and missing shared records fail closed.
- Treats a scripted response consisting only of an acknowledgement (for example
  “Okay.” or “As you wish.”) as an action confirmation. A longer spoken line in
  the same response keeps it eligible. Scripts alone are not grounds for
  exclusion: quest conversations can change stages. Empty hubs and short
  acknowledgements may still lead to eligible conversation through explicit links
  while they are unheard.
- Excludes NFF, WWM, vanilla follower management, AFT and EFF management records;
  services, favors, combat chatter and randomized responses; and command-like
  prompts and editor IDs for trading, waiting, dismissal, outfits, tactics,
  formations and related controls. Whole command phrases avoid rejecting
  personal questions such as “How did you learn your trade?”
  Equipment requests embedded in personal quests are filtered too, including
  Rumarin's “I want you to switch to a bow.” Polite dismissal preambles, outfit
  creation requests, and both `[NFF]` and `(NFF)` labels are recognized.
  Each sentence is checked for explicit instructions, including destination-based
  waiting, meeting and return-home commands. This covers Skjel's “I have some
  errands to do. Wait for me in Riften.” even though he gives a longer reply.
  Quoted instructions and conversational interjections such as “Wait. Do you
  know me?” are preserved; location names are not an allowlist.

Read-only background file parsing supplies INFO prompt overrides, speaker fields,
response content, script-binding presence and links absent from CommonLib's
resident record structure. Both ordinary and
zlib-compressed records are supported. Record sizes, group bounds, decompressed
lengths and checksums are checked. File-owned FormIDs resolve through that file's
masters and ESL indices, so patches are read from the winning INFO's file.
Workers receive no game objects; condition evaluation and head projection remain
on the game thread. Renderer snapshots contain only screen coordinates.

Definition data is cached; availability is rescanned about every two seconds with
a one-millisecond work budget plus one step per companion to avoid starvation.
The first scan may take longer while definition files load. Head position is
updated each player frame. This code does not start dialogue, play responses,
execute dialogue scripts, change quest stages, or mark anything heard.

## Limits and validation

This is a heuristic for arbitrary mods, not a semantic understanding of dialogue.
Unusual command wording or identifiers may escape the command filter. Script
presence plus acknowledgement text is supporting evidence, not a general static
analysis of arbitrary Papyrus actions. A scripted one-word story response can be
missed by this conservative rule. Text
rules are English; translated topics still benefit from editor IDs, record
types and framework exclusions. Localized INFO prompt IDs are not interpreted as
text. Nonzero localized response IDs preserve speech eligibility; English
acknowledgement filtering is not applied to untranslated string IDs.
Conditions which require actually entering dialogue, or scripts that
unlock a topic only after another response executes, can produce a missed icon.
New questions hidden behind an already-heard hub are intentionally not advertised;
the player can still revisit that conversation normally. A newly offered root
or a new qualifying INFO on an existing root remains eligible.
Malformed, missing or dynamically created records without readable metadata are
skipped. No icon is evidence that the scanner found nothing, not proof that a
companion has no dialogue.

Automated coverage includes command/story discrimination, OR/AND condition
grouping, prompt overrides, links, speaker IDs, compressed data, checksums and
record traversal. Regression checks cover the empty `BSAssets.esm` INFO
`016025E9` that triggered M'rissi's false alert, Rumarin's scripted bow
confirmation `3DNPC.esp:05327CE3`, and his actual personal conversation
`3DNPC.esp:05219624`. M'rissi's home, dismissal, waiting and satchel topics are
also checked against the command classifier.
Skjel's `3DNPC.esp:052BD12E` waiting command is covered alongside his eligible
knock-knock conversation `3DNPC.esp:052C40AC`. The parser was checked against
installed Inigo, Lucien, Remiel and Auri records. These checks do not substitute for testing Skyrim's
condition evaluator or the camera projection in game.
Traversal regressions include Rumarin's heard war-paint response
`3DNPC.esp:05219623` leading to unused reply `3DNPC.esp:050AC1FA`, as observed in
the runtime log after his visible conversations had been exhausted.

In a test save:

1. Recruit and enroll a companion with a known unheard conversation. While
   travelling, confirm no icon. Trigger temporary rest, then try manual Relax.
   Both should show a small chat bubble above the companion's actual head.
2. Open the conversation and exhaust the topic. The icon hides during dialogue
   and stays gone afterward unless another unheard option remains. Save/load;
   an already-heard conversation should remain cleared.
3. Check a familiar conversation hub with a newly unlocked linked question, and
   a companion whose only remaining options are trading, NFF or formation commands.
4. Resume travel, leave the rest area, enter combat, release the companion and
   disable the mod. Each must remove the icon. Check menu pause and console too.
5. Check seated companions, first/third person, camera turns, walls and changing
   cells. No marker should appear behind the camera or persist across loading.

`[RestDialogue]` log entries report indexed files, detected topic/INFO IDs, and
the actual prompt for investigating false positives. The initial icons and
M'rissi's empty-topic correction were confirmed in game. The heard-parent
traversal correction still needs an in-game check.
