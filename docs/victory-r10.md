# Victory revision 10

Forced sheathing and weapon restoration are removed. After the existing encounter
qualification (at least three defeated enemies and 1.25 seconds of quiet), each
nearby eligible follower has ten seconds to start a reaction. Both the player and
that follower must be naturally sheathed, with 0.35 seconds of stable sheathed
state. Pending reactions expire; started clips keep their full duration unless
interrupted. Normal follower AI owns all weapon transitions.

Victory, arm stretch, shoulder recovery, and vanilla clapping remain separate,
one-shot reactions. Assignment rotates with follower index and encounter count.

At most one post-combat voice request is dispatched per encounter, after a
completed gesture. It uses Skyrim.esm DCETCombatToNormal (0008548E), checks topic
quest availability and response conditions, and lets Say select the actual line.
The Papyrus helper rechecks combat, weapons, scenes, and dialogue before speaking.
This is best-effort: custom voice types may have no matching line. No Shout power,
borrowed voice, new dialogue records, or behavior graph generation is involved.

Validation: Release DLL build, 19/19 CTest suites, Papyrus helper compiled with
zero errors/warnings. In-game timing, voice availability, and animation playback
still require a playtest. No Pandora run is required.

Try a qualifying battle, sheath promptly, and let nearby followers settle. Then
try another battle while remaining armed past ten seconds: sheathing afterward
must not start stale reactions. Renewed combat must cancel celebrations normally.
