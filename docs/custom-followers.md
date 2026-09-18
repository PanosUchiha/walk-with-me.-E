# Custom followers

Automatic enrollment is off by default. Aim at a companion and press G to add
their travel/rest control explicitly, or opt into automatic enrollment in Party.
Custom enforcement remains on by default. Wayfarer assigns its travel and rest packages at runtime; no follower patch
or behavior generation is needed for enrollment. Command gestures are separate.

With NFF installed, ordinary followers must occupy its active follower roster.
Home residents and dismissed followers are not automatically enrolled. Known
independent followers retain their explicit recruitment checks outside NFF.
Unknown private recruitment systems can use the explicit Companion control popup,
an API integration or a dedicated detector. Ordinary Party Add uses the same
recruitment checks as discovery. The popup is a separate, saved player opt-in.
This does not recruit NPCs or replace a follower manager's quests and dialogue.

Custom enforcement is on by default. Known independent custom teammates can bypass
the vanilla follower faction requirement; other custom followers need active
recruitment evidence. Eligible custom followers can bypass ordinary travel package
requirements. Wait, dismissal, combat and scene guards remain. Custom blocklists and exclusions stay;
the old default blocklist migrates away.

Serana's own recruitment path can set teammate status without CurrentFollowerFaction.
Inigo can use a negative WaitingForPlayer value for dismissal. Detection must respect
both cases. These were record/script findings, not proof that every follower works.
Test package ownership and handoff with each framework.
