# September 26 crash review

## Current evidence

Windows Application Error recorded the latest Skyrim crash at **14:19:43 local**:

- Process: `SkyrimSE.exe`, PID 4068.
- Faulting module: `BetterThirdPersonSelection.dll`.
- Fault offset: `0x1161FD`.
- Exception: `0xC0000005` (access violation).

The earlier crash at **13:42:25** has the same module, offset and exception. That
crash preceded the 14:00 TETHER r6 installation and the 14:11 compass replacement.
This makes BTPS the strongest current lead. A faulting module alone cannot prove
whether another plugin contributed to the invalid state.

Crash Logger registered its handlers but did not produce a new text crash report
for this run. Windows did save a dump at
`C:/Users/Tobih/AppData/Local/CrashDumps/SkyrimSE.exe.4068.dmp`.
The earlier matching dump is `SkyrimSE.exe.22780.dmp` in the same directory.

Both minidumps independently confirm a **read** access violation at the same BTPS
instruction. The exception reports the attempted address as `FFFFFFFFFFFFFFFF`;
the register being used as a pointer contains a noncanonical address in both runs.
The initial raw stack scans contained the same series of BTPS code addresses.
After correcting the offline unwinder's memory-read callback, StackWalk64 resolves
the following BTPS frames before reaching a trampoline outside known images:

`+1161FD → +67D25 → +67D3A → +6AF9A → +6A3C0 → +67AA4 → +68D9A → +76629`.

Disassembly identifies the failure as a reference-count increment while copying
a child pointer inside `FocusManager::FindChildObjectREFR` (entry `+67CB0`). Both
captured child values are noncanonical: `3F64A983436F7C5C` and `479EEEEF00000000`.
The relevant heap pages are absent from the dumps, so the originating node, actor
and source of corruption cannot be established. Raw stack mentions of other
plugins do not establish that those plugins were callers.

## Walk With Me timeline

The installed DLL hash is still
`7FDC09FC96FC8C73B473ED0D071C298913C70257C1D0D446EDD1117E6AC26CD2`.
Its startup log confirms TETHER r6 is active.

| Local time | Recorded activity |
| --- | --- |
| 14:19:33.928 | Inigo's physics grip connected. |
| 14:19:35.282 | Grip released because separation exceeded the limit. |
| 14:19:35.658 | All three physics cleanup operations reported success. |
| 14:19:36–39 | Normal follower travel updates continued. |
| 14:19:40.155 | Inigo began another close approach. |
| 14:19:40.254 | Grip gate stayed closed because of distance and movement. |
| 14:19:40.287 | Approach package arrival radius changed to 12; last WWM log entry. |

There are no warning/error entries or Pathfinder activity in this run's WWM log.
No second grip or physics operation is recorded before the crash. The final log
line is timing evidence, not proof that its operation caused the crash.

The runtime log separately confirms that r6 did not eliminate the startup
movement problem in this run: Inigo separated before offset movement began.

## Preserved evidence

`build/crash-20260926-141940` contains the WWM and follower logs, BTPS log,
Crash Logger startup log and Windows Application Error records.
`build/btps-crash-fix` contains disassembly, the corrected unwind results, guard
test evidence and the installation manifest.

## Local mitigation installed at 14:46

At the user's request to keep BTPS enabled, a separate `BTPSSceneGraphGuard.dll`
was installed alongside BTPS with Skyrim closed. It replaces only the implicated
child lookup in memory, rejects invalid or unreadable branches, and avoids the
intrusive pointer increment that failed. It requires Skyrim 1.5.97, an exact
SHA-256 match of the installed BTPS DLL, and all 199 original function bytes.

Release build, 3,036 checks and an offline compiled-DLL load/Query check passed.
Actual SKSE activation and in-game selection behavior remain unverified. This
mitigates the observed fault path; it does not repair the source of corruption.
Objects with rejected branches may temporarily be harder to select.

All 22 original BTPS files were backed up and hash-verified. BTPS files, Nolvus
settings and the WWM plugin files were verified unchanged after installation.
Backup: `backups/btps-scene-graph-guard-preinstall-20260926-144631-586/mod`.
Manifest: `build/btps-crash-fix/installation.json`.

See [the guard README](../compat/BTPSSceneGraphGuard/README.md) for implementation,
compatibility, startup-log verification and removal instructions. BTPS remains
enabled, with its original DLL and settings. No load-order entries were changed.
