# BTPS Scene Graph Guard — local build 0.1.0

This is a local compatibility guard for the installed Better Third Person
Selection DLL on Skyrim **1.5.97**. BTPS remains enabled. Its original DLL,
MCM, scripts, settings and load-order entries are preserved.

## Evidence and change

The September 26 crash dumps fail at BTPS `+0x1161FD`, an intrusive reference-count
increment while copying a child pointer. Disassembly and the repaired offline
unwind identify recursive `FocusManager::FindChildObjectREFR` at `+0x67CB0` as
the caller. Both dumps contain noncanonical child pointers. The dump does not
contain the relevant scene-graph heap pages, so the original source of those
bad pointers remains unknown.

The guard replaces that one lookup in memory after SKSE finishes loading plugins.
It reads node metadata and child pointers without copying intrusive smart pointers
or modifying engine memory. Failed reads and malformed branches return no match;
valid siblings remain eligible. It retains BTPS's depth limit and ownership match.
It compares the AsNode vtable slot with Skyrim's known node/leaf implementations,
without calling through an untrusted virtual function pointer. Custom overrides
of that slot are skipped. Traversal has a finite work budget.

This addresses the observed failure path. It does not repair the source of scene
graph corruption or establish that all possible BTPS crashes are fixed. A skipped
branch can temporarily make an object harder to select. Other BTPS selection paths
and the original parent lookup remain unchanged.

## Strict compatibility

The guard installs only for this exact BTPS file:

`42E9107ED191CD92DAD429AF31BC181F7F55B21C2694E0F993E0C6E5E51EF4D7`

The installed mod metadata calls it 0.8.6. The file is 2,725,888 bytes, built May 10,
2025. The guard additionally compares all 199 bytes of the live target function.
A different DLL, an existing patch at that function or any runtime other than
Skyrim 1.5.97 is rejected. No other game's address layout is assumed.

## Checks

- Release build succeeds.
- 3,036 test checks pass, including both captured crash values, unreadable pages,
  malformed child arrays, sparse valid trees, recursion limits, untouched
  reference counts, mixed valid/invalid siblings and executable-hook fixtures.
- SHA-256 implementation checked against a standard vector and the installed DLL;
  a one-byte modification is rejected.
- Compiled plugin loads in the offline smoke check; Query accepts 1.5.97 and
  rejects a different runtime, editor and null interface.
- Actual in-game activation and behavior still require a new Skyrim launch.

On that launch, `SKSE/BTPSSceneGraphGuard.log` under the game's Documents logs
should say `Active: protected BTPS child traversal`. A warning beginning
`Inactive:` means the guard did not install. Skipped branches are logged at a
limited rate. Normal startup will not create a new ESP or change a save.

## Install and remove

Install `BTPSSceneGraphGuard.dll` alongside `BetterThirdPersonSelection.dll` in
the existing mod's `SKSE/Plugins` folder, with Skyrim closed. Keep the original
BTPS DLL. The local installation manifest and backup path are recorded in
`Wayfarer/build/btps-crash-fix/installation.json`.

To remove this guard, close Skyrim and remove only `BTPSSceneGraphGuard.dll`.
The next launch uses the original BTPS function. No save rollback is required
by this guard because it has no serialization or persistent game-state writes.

## Source and build

The complete guard source is in `Wayfarer/compat/BTPSSceneGraphGuard`. Configure
with the local CommonLibSSE CMake prefix, then build Release. Tests use only the
Windows SDK. `make_fingerprint.py` intentionally refuses a different BTPS file.

BTPS's source is by Shrimperator, under Apache 2.0:
https://gitlab.com/Shrimperator/skyrim-mod-betterthirdpersonselection

The relevant source is `src/FocusManager.cpp`, `FindChildObjectREFR`, inspected at
upstream commit `93e05fb`. The separate guard uses the existing local CommonLibSSE
package. Third-party notices accompany this local installation. Nothing was
uploaded, committed or published.
