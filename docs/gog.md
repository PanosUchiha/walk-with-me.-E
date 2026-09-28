# GOG startup

Fixed in 0.2.5. SKSE reports the store in the last field of the game version
(Steam 0, GOG 1), so GOG shows up as **1.6.1179.1**. Up to 0.2.2 Walk With Me
accepted only `.0` and refused to load on GOG. The Address Library lookup was never
the problem: CommonLib reads the executable's own version, 1.6.1179.0.

GOG 1.6.1179 and 1.6.659 are supported. GOG 1.6.1170 is refused on purpose: it
shares its number with Steam 1.6.1170, so it would load Steam's
`versionlib-1-6-1170-0.bin` and every address would be wrong. Update it to 1.6.1179.

GOG 1.6.1179 is Steam 1.6.1170's code relinked: 99.8% of functions, and every one
Walk With Me looks up itself, are the same size. The one fixed offset inside a
function, the travel speed hook, checks that it still calls `GetScale` before
patching, and otherwise logs `[Speed] Unexpected hook instruction`.

Use SKSE 2.2.6 GOG, the 1.6.x Address Library including `versionlib-1-6-1179-0.bin`,
a matching Menu Framework 3 build and the x64 Visual C++ runtime. Logs are under
`My Games/Skyrim Special Edition GOG/SKSE/`.

Check skse64.log for DLL errors, then Walk With Me.log for runtime, hooks and menu
registration. On GOG the `[Runtime]` line reads `Skyrim 1.6.1179.1`. An absent mod
log points to failure before logger startup. Supply the affected runtime and logs;
do not substitute Steam offsets.

## Reading the skse64.log line

SKSE 2.2.6 writes one reason per rejected plugin. Each points somewhere
different, so get the line before changing anything.

- `disabled, address library needs to be updated`. The script extender could not
  open `Data\SKSE\Plugins\versionlib-1-6-1179-0.bin`. Install the 1.6.x Address
  Library. Nothing in the mod can work around a missing database.
- `reported as incompatible during load`. Walk With Me's own entry point refused
  the runtime, and a message box names the version it saw. On GOG 1.6.1179 with a
  DLL from 0.2.2 or earlier, this is the bug above. Otherwise check the version
  against `include/runtime_compatibility.h`.
- `disabled, only compatible with versions earlier than 1.6.629`. The post-629
  structure gate. From 0.2.0 the plugin declares those layouts, so this should
  not appear. If it does, the installed DLL predates 0.2.0.
- `disabled, unsupported version independence method`. The DLL sets a flag the
  installed script extender does not know, so the DLL is newer than SKSE.
- No line at all, and the plugin loads. The failure is past the script extender.
  Check the x64 Visual C++ runtime, the Menu Framework build, and whether the DLL
  was built against a library older than the runtime it runs on.

Do not chase the `kVersionIndependentEx_AddressLibraryV5` flag. CommonLibSSE-NG
sets it by default, but SKSE 2.2.6 never reads it; only bit 0 of
`versionIndependenceEx` is examined. A build missing that flag is not rejected
for it, and setting it fixes nothing on this runtime.
