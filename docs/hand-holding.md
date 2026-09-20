# Hand holding

Experimental and off by default. Use Companion mode, third person and sheathed
weapons. Choose a follower in Travel settings, enable Hand holding, then pause
to let them approach and connect. Walking, jogging and running are supported.

## Generate the optional clasp animations

The public mod contains no Skyrim-derived clasp clips. Without generated clips,
hand-clasp acquisition stays disabled; the rest of Walk With Me still works.

1. Install Python 3.10 or later. The generator includes its codec source and
   needs no Blender installation or additional Python packages.
2. Extract the `meshes/actors/character/animations/male` and `female` folders
   from **your own** `Skyrim - Animations.bsa` into a separate working folder,
   using a BSA extraction tool. Use the vanilla clips, not animation replacers.
3. Open a terminal in the extracted `ClaspGenerator` folder from the mod archive:

   ```powershell
   py -3 build-tether-clasp-clips.py --animations "C:/Extracted/meshes/actors/character/animations" --output "C:/Generated/Walk-With-Me-0.2.1-Clasp"
   ```

4. The script checks all inputs, generates and validates 68 clips, then creates
   `Walk-With-Me-0.2.1-Clasp.zip` beside the output folder. It will not overwrite
   an existing output. Install that ZIP after the main mod in MO2 or Vortex.
5. Restart Skyrim before testing. No behavior regeneration is needed for the
   clasp clips. Optional command gestures have separate GPMA setup instructions.

Regenerate the clasp package when upgrading from a local 0.16.8 build. The new
OAR condition files require plugin version 0.2.0 or later; the old higher version requirement
would prevent those conditions from loading.

Keep the generated ZIP local. It contains data from your own game files and is
not part of the public download. The supplied rotation recipe is WWM-authored.
The generator uses the unmodified [PyNifly codec](https://github.com/BadDogSkyrim/PyNifly)
at revision `bcb723aba2ca4a80e976d957efbde69b33f2e56f`; source and license are included.

## Behavior and limitations

The current backend combines native approach, ticketed package handoff, a
TETHER-based physics grip, and authored OAR clasp poses. It waits for script
cleanup and the engine package transition before connecting. It does not use
the retired per-frame procedural arm/body-position backend.

Sprinting, combat, drawn weapons, jumping, swimming, mounting, dialogue, scenes,
rest, first person, blocked movement or excessive separation release the grip.
Failed attempts require space and another approach. The selected partner is
never silently replaced by another companion.

## Tuning the clasp

Each acquisition step now waits only on what it depends on. The ragdoll bodies
are created while the follower is still turning into place, so the constraint is
the only Papyrus round trip left once the hands reach each other.

Two `[HandHolding]` settings decide how the clasp itself looks, because the right
values depend on the body and hand meshes installed:

- `fGripWristGap` (default 20) is how close the wrists must already be before the
  constraint is created. The constraint pulls the two palm anchors onto one
  another the instant it exists, so a smaller value clasps more accurately and a
  larger one clasps more readily. If the pair cannot reach the value, it widens
  to 45 over 1.5 seconds rather than refusing to connect.
- `fPalmX` / `fPalmY` / `fPalmZ` (default `0, -2.35, 5`) place the contact point on
  the palm surface in hand-local game units. The skeleton's hands are mirrored, so
  one triple describes the same point on both palms.

To set these from measurement rather than guesswork, approach with logging on and
read the acquisition trace, which prints the measured wrist gap against the limit
in force once a second:

```
[HandHolding/TETHER] Session 12 gate=grip proximity root=44.8 wrist=26.3/20.0 ...
```

A `wrist=` figure that never falls near the limit means `fGripWristGap` is tighter
than this pair can achieve; raise it to just above the value the log settles at.
Changing any of these releases a live grip so the constraint is rebuilt with them.

While connected, the generated poses override matching locomotion replacements
for those actors. Other replacers resume when the grip is released. Custom
bodies, skeletons and non-interruptible animations can affect appearance and
timing. Wrist contact, turning and transitions still need in-game evaluation.

For reports, include a video, the actors' sides, movement state, skeleton and
animation mods, and `Walk With Me.log`. The standalone TETHER plugin is not required.
