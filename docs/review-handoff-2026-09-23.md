# Complete review handoff — September 23, 2026

## Request and repository state

The user asked to bring all changes into `main`, leave the GitHub commit for
later, and make all context available for Claude to review and add a new feature.
They explicitly clarified that **everything done so far is relevant**, not just
the conversation-icon feature. The new feature itself has not been specified in
this conversation.

The repository is `X:/!--- Main/Documents/Nolvus/Wayfarer`. It was already on
`main` with the changes in its working tree, so there was no separate feature
branch to merge. HEAD is `d1a0530` (tag `v0.2.1`), one commit ahead of the locally
recorded `origin/main` at `443ace0`. The remote was not refreshed for this handoff.
Nothing was committed, pushed, reset, stashed, or discarded. The index was empty
when checked. New files are untracked and must be included in a later commit.

Useful review commands from the repository:

```powershell
git status --short --branch
git diff HEAD
git ls-files --others --exclude-standard
```

There are existing detached worktrees under `../VictoryAnimation/runtime` and
`build/follower-diagnostics-source`. They are historical/independent checkouts;
they were not merged or altered during this handoff.

## Accepted baseline and current feature map

Read [baseline-0.2.1.md](baseline-0.2.1.md) first. The user accepted movement
candidate r9, which restored the September 19 r3 travel implementation. Earlier
sharp-turn, route-waypoint, departure, ownership-fence and reset-cadence experiments
were removed. Their documents remain historical evidence, not a request to
reintroduce them. See [r3-movement-r9.md](r3-movement-r9.md).

| Area | Current working-tree state | Main review entry points |
| --- | --- | --- |
| Formation travel | Accepted baseline with later hand-holding integration and resting-actor enumeration | `src/formation_controller.cpp`, `include/formation_controller.h`, baseline docs |
| Classic hand holding | Available and default method; enable toggle remains off by default; stationary ground fallback restored | `src/hand_holding.cpp`, `include/hand_hold_locomotion.h`, `include/hand_hold_state.h` |
| TETHER hand holding | Retained physics backend with facing, latency, arrival hold and reordered grip setup changes | `src/hand_holding_tether.cpp`, `include/tether_session.h`, `include/tether_approach.h`, `include/engine_compatibility.h` |
| Method switching | Dispatcher keeps TETHER ownership while asynchronous cleanup finishes | `src/hand_holding_dispatch.cpp`, `include/hand_holding_backends.h`, dispatcher tests/stubs |
| Papyrus handoff | Serialized priority alias cleanup, combined native validation, deferred versus failed physics steps | `package/Scripts/Source/Wayfarer*.psc`, `src/papyrus_bridge.cpp` |
| Post-victory reactions | Shelved in the current build; experimental source retained | `CMakeLists.txt`, `src/victory_disabled.cpp`, victory history below |
| Rest conversation icons | Implemented and installed through revision r5; Katana audit remains open | `src/rest_dialogue.cpp`, `include/rest_dialogue*.h`, `tests/rest_dialogue_tests.cpp` |
| UI/artwork | Classic/TETHER selector, rest conversation toggle, overhead ivory chat emblem | `src/menu_ui.cpp`, `src/order_visuals.cpp`, `package/Interface/WalkWithMe/chat.svg` |
| Packaging/licensing | Chat asset, puff decoder/license, source archive inclusion, victory asset removal and legacy cleanup script checks | `build-release.ps1`, `tools/package-release.py`, `tools/verify-package.py`, `LICENSING.md` |

Some files contain changes from more than one area. Review and preserve the whole
working tree rather than assuming each file belongs to one isolated feature.

## Hand-holding history and constraints

Read these in sequence, using current code to resolve superseded details:

1. [tether-responsive-r1.md](tether-responsive-r1.md): direct reference yaw
   correction, followed by a fresh measured frame before setup. Its statements
   about the selector being on hold describe that earlier candidate.
2. [classic-hand-holding.md](classic-hand-holding.md): Classic restores the
   retained procedural/sticky implementation; it needs no generated clasp clips.
   Switching methods releases resources before the next backend takes over.
3. [tether-latency-r2.md](tether-latency-r2.md): Classic becomes the default;
   explicit TETHER settings remain supported. Priority alias cleanup is still
   serialized. The script/native protocol distinguishes deferral from failure.
   These changes require the matching Papyrus scripts.
4. [classic-ground-r3.md](classic-ground-r3.md): restores grounded fallback when
   stopped route probes have zero length, addressing immediate grip/release loops.
5. [tether-direct-r4.md](tether-direct-r4.md): holds the follower's arrival position,
   creates the grip before movement handoff, and still requires the handoff
   acknowledgement before offset movement. Cancellation releases only resources
   actually acquired. Failed teardown retains ownership for retry.

Keep dialogue/combat/scene/actor-state guards, fresh-frame alignment checks,
skeleton and wrist limits, and serialized cleanup intact. Do not infer successful
animation playback or zero latency from passing native tests. The docs contain
measured delays and the remaining in-game checks.

## Victory history versus active code

The pending tree retains revisions described by [victory-r6.md](victory-r6.md),
[victory-r7.md](victory-r7.md), [victory-r10.md](victory-r10.md),
[victory-r11.md](victory-r11.md), and [victory-r12.md](victory-r12.md).
They cover natural sheathing, reaction deadlines, playback retry/observation,
position holds, and custom-follower post-combat voice lookup.

**Those experiments are currently shelved.** `CMakeLists.txt` selects
`src/victory_disabled.cpp` instead of the experimental runtime. Configuration
forces the feature off and removes its saved/public toggle. Victory animation
assets were removed from the public package. The retained
`WayfarerVictoryVoice` helper only releases old movement holds; compatibility
endpoints remain so existing saved script stacks can clean up.

Do not accidentally reactivate the experiment by adding the old `.cpp` files
back to the build or restoring deleted assets. Historical victory tests still
exist, so a passing victory test does not mean the feature is active.

## Conversation icons: user intent

- Show a small chat icon above a managed follower only while temporary rest or
  manual Rest/Relax is active.
- Include new conversational material broadly, including personal dialogue and
  quest updates. The initial narrower preference was superseded by “include
  everything” while excluding NFF, trading, formations and other action commands.
- Match the wheel menu and right-side HUD artwork. The current icon uses the
  existing SVG renderer and ivory tint.
- Clear the icon when useful new conversations are exhausted. It should not
  continually advertise management commands or unused alternatives buried inside
  already-heard conversations.

The setting is `[Interface] bRestDialogueIcons`, enabled by default and exposed
as **New conversation icons** in Rest & Sandbox. Temporary-rest enablement is a
separate user preference and must not be changed just to demonstrate the icons.

## Conversation implementation and revisions

The full technical description is [rest-dialogue.md](rest-dialogue.md).

The scanner evaluates loaded quest/INFO conditions on the game thread, reads
winning plugin definitions asynchronously for metadata missing from CommonLib,
and uses saved `saidOnce` state. The renderer receives screen coordinates only.
The scanner does not execute dialogue scripts, advance quests, or mark responses
heard. Plugin parsing includes bounds checks and zlib record decoding using the
vendored upstream puff implementation and its license.

The player-update hook calls `RestDialogue::Update`; the controller exposes only
actively resting managed followers through `RestDialogueActors()`. UI, combat,
scene, distance, visibility and head projection gates control marker display.

| Revision/finding | Evidence and resulting behavior |
| --- | --- |
| Initial icons | User confirmed the overhead icons worked; requested a wheel/HUD-style appearance. |
| r2: Rumarin bow command | `3DNPC.esp` INFO `05327CE3`, runtime `25327CE3`, uses “I want you to switch to a bow.” inside his personal quest. Added equipment/directive filtering, punctuation handling, NFF labels and ivory SVG artwork. |
| r3: M'rissi empty topic | Runtime trigger was `BSAssets.esm` INFO `016025E9` (runtime `146025E9`), an empty internal “...” topic. Require response content, resolve shared responses, reject scripted acknowledgement-only replies. User later confirmed M'rissi was fixed. |
| r4: Skjel waiting command | `3DNPC.esp` INFO `052BD12E` says “I have some errands to do. Wait for me in Riften.” Added sentence-level directive checks, destination waiting/meeting/return commands, and quote/interjection regressions. His longer acknowledgement alone cannot identify the topic as conversational. |
| r5: Rumarin unused nested replies | Log first showed the humor topic, then INFO `050AC1FA`, the “whiskers” reply linked from heard war-paint INFO `05219623`. Stop traversal at heard/grey responses, so unchosen alternatives inside old conversations cannot keep the marker alive. Newly offered roots or new selected INFOs still qualify. |

Filtering remains heuristic. English phrase rules and short scripted
acknowledgements are imperfect; scripts alone cannot be rejected because real
quest conversations advance stages. Localized IDs must not be treated as text.
The r5 choice intentionally does not advertise new questions hidden behind an
already-heard hub. The player can still revisit that hub normally.

## Open Katana finding

The latest user request was to check whether only **“Thoughts?”** was keeping
Katana's icon alive. This was a read-only audit; no Katana-specific fix was made.

The September 23 09:33–09:39 log repeatedly identifies:

- Runtime topic `5D6317C1` / INFO `5D6317C2`, from `Katana.esp` raw
  `056317C1` / `056317C2`.
- Prompt: **“I want to know more about you, Katana.”**
- Reply: “That's fair. What do you want to know?”
- The record is an introduction to a personal-question menu with outgoing links.
  The scanner currently counts the opener itself; it does not verify that any
  unheard substantive questions remain underneath it.

The installed **“Thoughts?”** topic is raw `05733BA3`. All 49 of its responses
have the Random flag (`0x2`), which the current scanner excludes. Thus it was
not the logged cause of the icon. That exclusion is an implementation limitation;
the user did not ask for a Katana-only allowlist or explicitly request that
genuine “Thoughts?” conversations be suppressed.

Review menu-openers versus substantive content, and how repeatable/random
conversation should interact with “new” before adding more prompt-specific
exceptions. The audit does **not** establish that all Katana's personal questions
are exhausted. Avoid claiming that without current gameplay evidence.

## Validation and installation

The latest r5 source built successfully for both existing configurations:

- `build/manual`: local legacy build used for this Skyrim SE 1.5.97 installation.
- `build/modern`: modern CommonLibSSE-NG compatibility build.

Both passed **21/21 CTest suites**. Additional installed-record regressions used
`BSAssets.esm`, `3DNPC.esp`, and `MrissiTailOfTroubles.esp`, including the actual
Rumarin parent/child graph, command exclusions, positive conversations and the
earlier empty-topic fix. `git diff --check` passed. These results precede this
documentation-only handoff; they are not proof of full in-game correctness.

From the repository with the tools on PATH:

```powershell
cmake --build build/manual --config Release
ctest --test-dir build/manual -C Release --output-on-failure
cmake --build build/modern --config Release
ctest --test-dir build/modern -C Release --output-on-failure
```

Local CMake/CTest are under
`X:/VisualStudio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin`.
See [build.md](build.md) for setup rather than recreating the existing build trees.

The r5 DLL was installed at
`../MODS/mods/Walk With Me - Main/SKSE/Plugins/Wayfarer.dll` with SHA-256
`386E1AB623711DF8324ABB1549EAA0038DF9481D8900D8480D17ABCB3FEFA3CE`.
Backups and install manifest are in
`backups/rest-dialogue-r5-preinstall-20260923-091622-282`.
Only the DLL and feature documentation were replaced for r5; the INI was preserved.
Close Skyrim before replacing its DLL. Keep personal INI values and generated
clasp assets intact.

Useful local evidence (ignored by Git):

- `build/rest-dialogue-r5-{manual,modern}-{build,tests}.log`
- `build/rest-dialogue-r5-installed-records.log`
- `build/rest-dialogue-r5-rumarin-tree.log`
- `build/rest-dialogue-katana-audit.log`
- `build/tether-direct-r4/` and the earlier candidate/installation records
- Runtime log:
  `C:/Users/Tobih/OneDrive/Documents/My Games/Skyrim Special Edition/SKSE/Walk With Me.log`

## Packaging and historical context

The public output remains separate Main, Command Gestures and complete Source
archives. Build outputs, backups, PEX/ESP staging outputs and generated clasp
assets are not all tracked. Do not treat them as missing authored source or
blindly add ignored folders to the repository.

Current packaging expects seven compiled scripts, including the legacy victory
cleanup helper; older prose in `build.md`/`publish.md` still says six and should
be reconciled during release review. The puff `.c`/`.h` files and license must
be included with matching source distributions. Public packages must not include
locally generated game-derived clasp clips.

For broader context, use [usage.md](usage.md), [package-control.md](package-control.md),
[party-orders-utilities.md](party-orders-utilities.md),
[custom-followers.md](custom-followers.md), [hand-holding.md](hand-holding.md),
[gestures.md](gestures.md), [testing.md](testing.md),
[changes-0.2.1.md](changes-0.2.1.md), and [release.md](release.md).
The older `tether-r13` through `tether-r18` handoff documents and movement/victory
candidate reports preserve investigation history. Prefer current source and the
explicitly accepted/superseded status above over an old candidate's instructions.
