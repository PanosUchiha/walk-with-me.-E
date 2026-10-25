# Walk With Me 0.3.1

Changes since **0.3.0**. A fix release; no new game and no behavior regeneration needed.

## Changes

- **Fight With Me runs the fights.** While Fight With Me has companions fighting, or
  has just called them into a fight, Walk With Me lets go of the whole party and takes
  it back once the fight is over. Weapons out with nobody to fight still keeps the
  formation. Needs Fight With Me 0.3.1 or later; older versions are not asked.

## Fixes

- **Companions would not fight, stopped dead, or the frame rate dropped after a
  fight.** 0.3.0 kept a companion in formation while they were in combat unless an
  enemy was already targeting the party. A companion who spotted an enemy first, or
  who was still searching after a fight, stayed under Walk With Me's travel control,
  and its package was re-evaluated every second. That knocked them out of the fight
  they were starting, and the combat AI and Walk With Me kept undoing each other
  until the save was reloaded. A companion in combat is now always left to their own
  combat AI, as in 0.2.5. Weapons out with nobody to fight still keeps the formation.
- **The party no longer flickers between released and regrouped mid-fight.** Once an
  enemy is at you or a companion, the fight counts for three seconds after the last
  one turns away, so target switches and short lulls do not regroup the party only to
  release it again. Enemies fighting your summons or a companion's summons now count
  too.
- **Less work every frame while the party is released** (in combat, sneaking,
  mounted, swimming). Walk With Me's rest and social flags were looked up by plugin
  name on every frame; they are now looked up once.
- **The Hold hands tile was greyed out** until a partner had been picked in Travel
  settings, and the hotkey only said to go there. Hold hands now always works: with
  no partner chosen it takes the companion you are looking at, or else the nearest one
  in your party, and saves that choice. Turning it on also switches the formation to
  Companion, the only style hand holding runs in. If TETHER is selected and Open
  Animation Replacer is missing, it switches to Classic and says so instead of
  silently never connecting.
- **xEdit reported 60 errors in Wayfarer.esp** ("Interrupt Override -> Unknown: 255")
  on every package. The field is now None, the value Skyrim's own packages use. The
  packages behave the same; only the error check changes.
