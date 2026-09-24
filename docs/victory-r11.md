# Victory revision 11

The three September 21 encounters showed Rumarin and Katana expiring behind
the bAnimationDriven start gate. Remove that gate: retain scene, speech,
furniture, combat, actor-state, velocity, distance, sheathing and alias checks.
Let PlayIdle decide whether the idle request is accepted.

Katana also had one accepted custom clip with no OAR observation. Give custom
clips one retry after one second without observation. Observed clips never
retry. The original ten-second encounter window continues through attempts;
retries cannot reset it. No forced weapon transitions are reintroduced.

Loaded compatible followers outside playback range may approach during that
same window. Playback still requires the existing range and height checks.
Incompatible rigs are still excluded, now with race and behavior graph logged
separately from distance. Skjel's prior combined exclusion message could not
identify which restriction applied.

Validation: Release build and all 19 CTest suites pass, including retry limits,
observed-clip protection and the original deadline. In-game confirmation is
pending. Animations, voices and behavior assets are unchanged; no Pandora run.
