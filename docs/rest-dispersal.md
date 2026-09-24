# Rest dispersal and paired conversations

## What was wrong

Logs from 2026-09-23 showed the same pattern on every Relax order: all four
companions stayed 50-200 units from their anchors for the first 40-50 seconds,
then drifted farther. The free-roam radius was already 1000 at that point, so the
radius was not the cause. The delay had three sources:

- Skyrim's sandbox procedure shuffles actors near the package centre first.
- Wayfarer's own interest-point search waited for 8-16 seconds of uninterrupted
  plain standing. The early shuffling kept resetting that timer.
- Only one companion could walk to an activity at a time. There was also a
  2.5-second global cooldown between searches, followed by 35-50 seconds per
  companion.

Automatic rest added a 30-second nearby phase at 180 units before any of that
began.

## Behaviour now

- **Dispersal.** Each companion searches for somewhere to relax when rest begins,
  and again when the area widens. Searches are staggered by about 0.6 seconds per
  slot. They do not wait for plain standing, and companions can set off together.
- **Search order.** A companion first looks for furniture, idle markers or settled
  NPCs. If none are available, it walks to open reachable ground roughly 45% of
  the radius out (250-900 units). Each companion leans toward its own bearing, and
  spots already taken by another companion score poorly, so the party fans out.
- **Poses.** On arriving at open ground, a rest pose can start after 2-5 seconds
  instead of waiting out the initial 12-24 second cooldown.
- **Idle searches.** Later searches keep the plain-standing rule and still send
  one companion at a time.
- **Pairs.** The first pair forms about 20 seconds into a rest. If the two are
  within 200 units and can see each other, they talk where they stand. Otherwise
  the initiator walks to a navmesh point about 95 units from a partner up to 1800
  units away. The partner holds still and turns to watch once the walker is within
  600 units. The conversation starts when they are within 160 units. The walk gives
  up after 45 seconds, or if the waiting partner moves for more than 3 seconds.
- **Conversation length.** A conversation lasts 120-180 seconds. Turns last 4-8
  seconds, and about two turns in three include a gesture. The next pair waits
  45-90 seconds.

## Defaults

| Setting | Was | Now |
| --- | --- | --- |
| `fStartRadius` (nearby) | 180 | 400 |
| `fMaxRadius` (free roam) | 1000 | 1600 |
| `fSettledActivityRadius` | 1000 | 1600 |
| `fFullSandboxAfter` | 30 s | 10 s |

The free-roam and settled radii are still capped at 1800, or at
`fReleaseDistance - 200` if that is lower. The rest navmesh search now
explores up to 4096 triangles, up from 2048, so the larger area is not cut short.

## Log lines

- `[RestActivity] X heading for a quiet spot`: a dispersal walk to open ground.
- `[RestActivity] X found no open ground to spread out`: no reachable spot met
  the spacing rules; native sandbox continues as before.
- `[Social] conversation: A and B; walking over first` followed by
  `[Social] A walks over to talk with B`: a pair is forming.
- `[Social] A and B talking for Ns`: the conversation began.
- `[Social] X did not reach their conversation partner`: stalled or interrupted.

## Not yet verified in game

These points have unit coverage for the planner and timers only:

- whether native sandbox keeps a companion near the spot it walked to;
- how the stroll package handles long walks over rough terrain;
- whether two minutes of the social package ever lets Skyrim start voiced
  NPC-to-NPC chatter between the pair.
