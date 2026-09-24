# Hand holding

Experimental and off by default. Use Companion mode, third person and sheathed
weapons. Choose a follower in Travel settings, enable Hand holding, then pause
to let them approach and connect. Walking, jogging and running are supported.

## Choose a method

In Travel settings, choose **Hand-holding method**:

- **Classic** restores the earlier procedural arm pose and sticky
  walking attachment. It needs no generated clasp clips and does not use TETHER
  physics. This uses the retained classic implementation with its later
  direction and controller-velocity fixes, rather than reverting the whole mod.
- **TETHER** keeps the physics grip and generated OAR clasp poses.

Classic is the default. An explicitly saved TETHER selection is preserved.

Both methods use the same enable toggle, selected companion, lead-in delay and
connection/release distances. Changing methods releases the current grip;
Classic waits for any outstanding TETHER physics cleanup before starting.
For manual configuration, set `[HandHolding] sMethod = classic` or `tether`.
Unknown or missing values use Classic. Both methods remain experimental; Classic
can still show wrist twisting, sliding or awkward turns with some actors.

The direct grip r4 update connects TETHER before alias cleanup. The follower
holds their arrival position while physics is prepared, then changes movement
control after the hands connect. There is no arrival dwell or gradual grip-gap
widening. See [direct grip testing](tether-direct-r4.md).

## Generate the optional clasp animations

These clips are required only for TETHER. The public mod contains no
Skyrim-derived clasp clips. Without generated clips, TETHER acquisition stays
disabled; Classic and the rest of Walk With Me still work.

1. Install Python 3.10 or later. The generator includes its codec source and
   needs no Blender installation or additional Python packages.
2. Extract the `meshes/actors/character/animations/male` and `female` folders
   from **your own** `Skyrim - Animations.bsa` into a separate working folder,
   using a BSA extraction tool. Use the vanilla clips, not animation replacers.
3. Open a terminal in the extracted `ClaspGenerator` folder from the mod archive:

   ```powershell
   py -3 build-tether-clasp-clips.py --animations "C:/Extracted/meshes/actors/character/animations" --output "C:/Generated/Walk-With-Me-0.2.2-Clasp"
   ```

4. The script checks all inputs, generates and validates 68 clips, then creates
   `Walk-With-Me-0.2.2-Clasp.zip` beside the output folder. It will not overwrite
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

The TETHER backend combines native approach, ticketed package handoff, a
TETHER-based physics grip, and authored OAR clasp poses. The hands connect first;
script cleanup and the engine package transition gate only the subsequent
offset movement. Classic uses the
earlier procedural arm/body-position backend and retains the native travel
package with its original hand-holding movement pacing.

Sprinting, combat, drawn weapons, jumping, swimming, mounting, dialogue, scenes,
rest, first person, blocked movement or excessive separation release the grip.
Failed attempts require space and another approach. The selected partner is
never silently replaced by another companion.

## Tuning the clasp

The settings in this section apply only to TETHER.

The follower turns at their arrival position before physics is prepared. Bodies
and the constraint are created in three serialized script calls. No alias-cleanup
round trip or second walking destination precedes the grip. Script scheduling,
stopping, turning and the current hand pose can still affect connection time.

Two `[HandHolding]` settings decide how the clasp itself looks, because the right
values depend on the body and hand meshes installed:

- `fGripWristGap` (default 45) is how close the wrists must already be before the
  constraint is created. The constraint pulls the two palm anchors onto one
  another the instant it exists, so a smaller value clasps more accurately and a
  larger one clasps more readily. This limit applies immediately and does not
  grow with time. Older explicit values are retained; raise a saved value of 20
  to 45 for the direct grip behavior tested with Rumarin's recorded distances.
- `fPalmX` / `fPalmY` / `fPalmZ` (default `0, -2.35, 5`) place the contact point on
  the palm surface in hand-local game units. The skeleton's hands are mirrored, so
  one triple describes the same point on both palms.

To set these from measurement rather than guesswork, approach with logging on and
read the acquisition trace, which prints the measured wrist gap against the limit
in force once a second:

```
[HandHolding/TETHER] Session 12 gate=grip proximity root=44.8 wrist=46.3/45.0 ...
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
