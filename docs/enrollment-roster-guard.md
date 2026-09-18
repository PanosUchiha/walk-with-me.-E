# NFF roster guard r1

The housecarl report exposed a permissive enrollment path: a teammate outside
CurrentFollowerFaction was treated as a custom follower, allowing its normal AI
package to be overridden. This is a plausible cause, not a reproduced diagnosis
of the reporter's save.

The installed NFF scripts and ESP were inspected. Its controller fills active
DialogueFollower aliases on recruitment/import (normal slots 2–11, with legacy
slots 0–1 also recognized by its search). Ordinary dismissal clears follow-package
aliases, teammate status and the active slot, and sets follower faction ranks to
-1. NFF home-base residents use a separate quest and do not establish recruitment.

Discovery and Party/dialogue Add now share a recruitment guard:

- With NFF installed, ordinary NPCs must occupy an active follower slot. Stale
  teammate or follower-faction state cannot substitute for roster membership.
- Known independent followers retain the existing explicit exceptions: Serana,
  Inigo, Lucien, Auri, Remiel and Katana (the plugin identities in
  `legacyCustomExclusions`). They need teammate status and a finite, nonnegative
  WaitingForPlayer value. This is not a universal custom-follower detector.
- Without NFF, ordinary followers require a nonnegative CurrentFollowerFaction
  rank and, by default, teammate status. Unknown mod origin alone grants no bypass.
- NFF automatic enrollment takes precedence over the custom enrollment toggle.
  Party Add can still opt an actively recruited companion in when automatic
  enrollment is off; dismissal invalidates that dialogue registration.
- Explicit API registrations remain owned by the registering integration, which
  must unregister on dismissal. Unknown private recruitment systems require such
  an integration or a dedicated detector; Party Add uses the same recruitment
  evidence as discovery.

Existing erroneously managed ordinary NPCs are released by normal eligibility
checks and roster scans. No save editing, NFF state changes or ESP/script changes
are needed. Startup logs identify this DLL with `[Enrollment] NFF roster guard r1`.

## In-game verification

Visit inactive Lydia and a Hearthfire housecarl, leave the house, enter/exit an inn,
and verify neither joins WWM. Repeat after recruiting and dismissing each through
NFF, including dismissal of a companion added through the WWM Party page. Confirm
that recruitment restores eligibility and that waiting prevents movement. Check
NFF home residents, both automatic enrollment toggles, and Serana/Inigo outside
NFF. Repeat vanilla recruitment/dismissal without NFF. Native tests cannot verify
the runtime aliases or reproduce the reporter's save.
