# Follower victory reactions — revision 6

Four separate choices rotate across followers and encounters: Victory, Arm Stretch, the existing Shoulder Recovery, and vanilla Clapping. A stable party of four gets one of each. Smaller parties cycle through the choices over successive encounters; parties larger than four reuse the four choices. Each follower receives one reaction per encounter, never a victory-and-stretch sequence. Engine-rejected starts can retry within the existing window; accepted playback is never replayed after interruption.

Eligible managed followers automatically request sheathing once after a qualifying victory, independently of the player's weapon state. Playback waits for fully sheathed weapons. Renewed party combat cancels pending/active reactions, stops only the idle still owned by this feature, and requests weapon drawing for followers whose sheathing or reaction had begun. Ordinary combat AI then takes over. The player's weapons and animation are not controlled.

Existing victory qualification is preserved: at least three distinct defeated enemies in the same won encounter, followed by 1.25 seconds without party combat/searching. Movement, dialogue, unsafe states, dismissal and distance may prevent or interrupt reactions. No behavior regeneration or new ESP is needed.

Sources: `X:/!--- Main/Downloads/Victory.fbx` and `X:/!--- Main/Downloads/Arm Stretching.fbx`. Editable projects: `Victory.blend` and `ArmStretching.blend`. `convert.py` retargets a source; `export_separate.py` exports and packages separate SE HKX clips. No combined clip is used.

Validation: both new clips read back with 99 tracks at 30 fps; 257 frames / 8.533 seconds and 267 frames / 8.867 seconds. Maximum rotational round-trip error is below 0.07 degrees. Front/side skeletal samples inspected. Release native build and 19/19 test suites pass. Body deformation, armor fit, automatic sheathing against follower AI, and actual Skyrim playback remain unverified in game.

Runtime built in isolated `runtime` worktree from Wayfarer commit `d1a0530`, excluding unrelated uncommitted hand-holding edits. `package` is an update overlay for the existing Walk With Me main mod. `install.json` records installed hashes and backup directory. Locally supplied Mixamo-derived assets have not been published. The `vanilla` folder contains local game references; do not redistribute it.
