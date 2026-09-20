# GOG startup

The reported load failure is not reproduced locally. Compilation and available
Address Library IDs do not prove live hook compatibility.

For GOG 1.6.1179, use SKSE 2.2.6 GOG, the 1.6.x Address Library including
`versionlib-1-6-1179-0.bin`, a matching Menu Framework 3 build and the x64 Visual C++
runtime. A missing C++ runtime is a possible cause, not a confirmed diagnosis.

Check skse64.log for DLL errors, then Walk With Me.log for runtime, hooks and menu
registration. An absent mod log points to failure before logger startup. Supply
the affected runtime and logs; do not substitute Steam offsets.

## Reading the skse64.log line

SKSE 2.2.6 writes one reason per rejected plugin. Each points somewhere
different, so get the line before changing anything.

- `disabled, address library needs to be updated`. The script extender could not
  open `Data\SKSE\Plugins\versionlib-1-6-1179-0.bin`. Install the 1.6.x Address
  Library. Nothing in the mod can work around a missing database.
- `reported as incompatible during load`. Walk With Me's own entry point refused
  the runtime, and a message box names the version it saw. Check that version
  against the supported list in `include/runtime_compatibility.h`.
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
