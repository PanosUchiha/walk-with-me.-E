# Companion controls

This build provides an explicit runtime import/release path for custom follower AI.
It includes Enrollment roster guard r1 and the Following rollback r1 movement
baseline. NFF is optional.

## Use

Aim at a companion and press **G**. The focused panel shows their name, a short
status and two animated icon buttons: **Travel together** and **Release**. Only
the applicable action is enabled. There is no NPC dropdown or automatic fallback
target. The selected reference stays locked until the panel closes. Details about
an unavailable action are shown on hover over the status.

G is rebindable in Controls. If a previous WWM binding already uses plain G, the
new default stays unbound rather than stealing that binding. The command wheel's
Companion control button / C / gamepad X and the Party page still open the panel.
Enter or gamepad A invokes the available action; **Cancel**, Escape or gamepad B closes.

The wheel's bottom controls share its inscription font, dark teal surfaces and
gold trim. Keyboard/gamepad badges identify their shortcuts. The companion panel
reveals its heading, divider, action cards and Cancel button in sequence. Hover
lift, eased press travel, icon ring reveals and a brief activation highlight use
frame-rate-independent motion. Hit areas stay fixed while the artwork moves.
Normal dismissal fades out over 200 ms and blocks duplicate actions; native
forced close and save changes still clean up immediately. Opening from the wheel
retains the dim backdrop throughout the transition.

**Unstick** (U / gamepad X) applies a gentle push to the selected nearby NPC after
the menu closes and gameplay resumes. Skyrim's normal knockback/get-up sequence
can free a frozen animation. This is a manual recovery attempt, not automatic
stuck detection or a guaranteed fix for broken AI/scripts. It works independently
of Travel together and does not change recruitment, factions, waiting orders or
travel-control ownership. It does not teleport or reset the actor.

The push is unavailable during combat, dialogue/scenes, furniture use, swimming,
mounting, ragdoll/get-up, unconsciousness or an active hand-holding connection.
Both the button and the eventual script call check the NPC's availability and
distance. Release hands and allow cleanup to finish before using it. A three-second
cooldown prevents repeated pushes. Pending requests expire after five seconds and
are cancelled on save changes. Hover a disabled Unstick button for the reason.

Install the matching WayfarerRecovery.pex with the DLL. No NFF dependency or new
ESP records are required. Native tests cover target identity, one-shot delivery,
menu dismissal, cooldown, invalidated targets, script timeout and save changes;
the actual panel click harness covers Unstick, disabled actions and drag cancel.
The physical knockback and recovery still require an in-game check on a frozen NPC.

The panel pauses the game through an invisible native Skyrim menu while its ImGui
animations continue. Closing, choosing an action, loading a save or a native forced
menu close removes this menu's pause. Other paused menus retain their own pause.
This works independently of Menu Framework's global FreezeTimeOnMenu preference.

Automatic enrollment defaults to off, including a one-time version-2 migration
that reverses the old automatic default. Explicit saved additions remain. Players
can opt back in under Party settings; future reloads preserve their choice.

Explicit control lets a selected NPC use WWM's existing formation/rest packages
without automatic follower eligibility. It does not change the NPC's teammate
flag, factions, quests, recruitment dialogue or original package records. It is
not a replacement for their recruitment system. Use their own dialogue to recruit
or dismiss them. Native packages from other mods can still outrank WWM, and private
scripts can still conflict; import does not establish universal compatibility.

**Release** removes WWM's registration and managed state and excludes that
NPC from automatic re-addition. Its temporary package aliases use the existing
release lifecycle. The original AI resumes when Skyrim reevaluates packages.
The panel also opens for an imported companion through their Party Control
button when listed. Import/release is serialized separately from dialogue Add and API
registrations, using resolved reference IDs. A queued action from another loaded
save is discarded.

## Recruitment and safety

- An active NFF slot is tracked when present. Leaving that roster releases the
  import even if a teammate flag remains set. NFF membership takes precedence
  over transient teammate changes.
- Independent teammates are released when teammate status clears. Negative or
  invalid WaitingForPlayer values also release imports.
- For NPCs exposing neither signal, explicit control lasts until released or a
  detectable dismissal appears. Use Release when finished travelling together.
  Stale signals cannot reliably reveal dismissal either.
- If a private NPC later exposes teammate state or joins NFF, monitoring adopts
  that stronger signal. Dead, disabled or hostile actors are released.
- Existing wait, combat, scene, weapon and other movement guards still apply.
  Import itself does not teleport an NPC. The separate Party Orders teleport
  action can recall eligible imports across cells.
- API-owned actors cannot be imported through the popup. Registering through an
  API transfers control back to that integration. Ordinary automatic enrollment
  and Party Add keep the stricter roster guard; no NPC is imported automatically.
- Import requires a nearby, loaded, nonhostile NPC in the same interior cell or
  outdoor worldspace, within 800 game units of the player, a free
  party slot and an enabled travel style. Pending imports reserve slots too.

## Validation and in-game checks

Native tests cover explicit versus automatic eligibility, recruitment loss, NFF
precedence, waiting, invalid saved flags, invalid follow state and duplicate Add
suppression. Both DLL variants compile; dynamic menu exports are checked against
the installed Menu Framework. Both builds pass 19 CTest suites, including settings
migration, hotkey persistence/collisions and popup layout bounds. A separate ImGui
harness renders the actual panel code and checks manage/release clicks, disabled
actions, drag cancellation, duplicate activation, Cancel, closing input suppression
and both wheel footer buttons. Motion checks cover 30/60/144 Hz, repeated cancel,
interrupted entrances and reopening. Offline previews were
visually checked; this does not establish in-game runtime compatibility.

In game, test a private custom companion with automatic enrollment disabled:
open from wheel and Party, confirm the selected name, import, walk/rest/wait, then
release and verify the original routine resumes. Test closing and targeting another NPC,
gamepad controls, a full party, scripted scenes, and importing again after release.
Save/load and verify only that save's imports return.
Dismiss an imported NFF companion and an independent teammate. Confirm inactive
housecarls remain unmanaged unless explicitly selected and imported. Check a
save load while the popup is open. The 0.2.0 package has offline validation; these runtime behaviors still
need in-game checks.
