# Walk With Me 0.2.0 validation

## September 20 restored r3 movement candidate r9

- Modern Release build succeeded; **19/19 CTest suites passed** and
  **5/5 animation packaging tests passed**.
- Movement source compared against the checksum-verified September 19 r3 source
  archive. The controller differs only in its revision banner; its header keeps
  the later victory state fields. Later victory implementation and tests remain
  unchanged from r8.
- Restores r3's movement regression coverage while retaining later victory tests.
- Runtime validation of this combination is pending. Earlier candidate sections
  below are historical. See [r3-movement-r9.md](r3-movement-r9.md).

## September 19 reset cadence candidate r8 (historical)

- Replays the observed blocked/reacquired/reset-burst sequence, including pace,
  heading, lateral, braking, settled-goal and obstacle-override requests.
- Checks that marker commits continue, persistent blocks retain retries and
  recovery, release clears the guard, and ordinary travel regains its cadence.
- This is the panel's isolated first experiment. It does not establish that
  resets initiate every freeze. See [reset-cadence-r8.md](reset-cadence-r8.md).

## September 19 control handoff candidate r7

- Native regression coverage separates delayed alias assignment, package
  acquisition timeout, owned route stalls, and serialized recovery cleanup.
- Hand-holding remains enabled for in-game validation. See
  [control-handoff-r7.md](control-handoff-r7.md) for limits and the repeat test.
- This candidate does not establish that a particular bridge or cave has a
  broken navmesh or that all narrow-area routing failures are fixed.

## September 19 bridge / departure candidate r6

- Modern Release DLL built successfully; **19/19 CTest suites passed**.
- Added regressions for stable waypoint renewal, transient probe failure, bounded
  stale holds, intermediate pacing and overlapping departure delays.
- In-game validation is pending. See [bridge-startup-r6.md](bridge-startup-r6.md)
  for the captured r5 log evidence, changes and remaining tests.

## September 19 actor-route / victory candidate r5

- Modern Release DLL built successfully; **19/19 CTest suites passed**.
- Animation packaging regressions: **5/5 passed**.
- New coverage includes connected route reconstruction, disconnected/cyclic
  routes, varied party assignments, equipment selection, startup grace and OAR state.
- Candidate r5 has not been playtested in Skyrim. Earlier legacy-build and
  visual checks below are historical and were not repeated for this candidate.
- Main, Command Gestures and source ZIPs remain separate. See
  [actor-routes-victory-r5.md](actor-routes-victory-r5.md) for limits and playtest order.

## September 17 baseline

Prepared September 17, 2026. Comparison baseline: 0.16.7, with the installed
Companion Unstick r1 / TETHER r18 build used to check retained assets.

- Modern native build: **19/19 CTest suites pass**.
- Legacy native build: **19/19 CTest suites pass**.
- All six Papyrus scripts compile with **zero errors and warnings**.
- **116** first-party C++ files pass the repository clang-format 19 style check.
- Actual ImGui panel/button tests: **zero failures**, including Unstick, disabled
  actions, drag cancellation, duplicate activation and closing input lockout.
- Wheel and three companion-state previews are byte-identical to the preceding
  Unstick build after formatting, confirming unchanged rendered output.
- **105** required GUI exports are present in the installed Menu Framework.
- DLL metadata reports **0.2.0**. The modern build declares the address library,
  the v5 reader and post-1.6.629 structure layouts; the legacy validation build
  declares the same except the v5 reader, which its library cannot set.
- ESP, dialogue, aliases, scripts, settings and artwork pass package validation.
- The generator extracted from the public ZIP runs with Python site packages
  disabled (`-S`), confirming that the bundled codec has no Blender or pip dependency.
- The local generator produces **68/68** clasp clips matching the prior validated
  r18 animation payloads. Round-trip checks retain motion, annotations and bindings.
- Public packaging admits only the **eight reviewed original WWM animation clips**.
  Generated clasp HKX files are excluded from both public archives.
- Each ZIP is checked for corruption and each entry against its source payload;
  archive SHA-256 files and the mod's internal file manifest accompany the release.

## Hand holding acquisition pass

Re-checked after the acquisition timing, clasp distance and declaration changes.

- Modern and legacy native builds: **19/19 CTest suites pass** on both.
- **116** first-party C++ files pass the clang-format 19 check after the change.
- New coverage for the progressive wrist gap, follower settling and the
  per-operation acquisition gates; the runtime support list is covered for both
  the modern and legacy libraries.
- The script extender declaration was checked by decoding `SKSEPlugin_Version`
  out of both DLLs rather than by reading the source. Modern reports
  `versionIndependenceEx 0x2` and `versionIndependence 0x5`; legacy reports `0x0`
  and `0x5`. Both are accepted by SKSE 2.2.6 on every supported runtime.
- Papyrus was not recompiled. No `.psc` changed, and the shipped `.pex` files are
  unchanged from the first 0.2.0 pass.
- No public archive was produced in this pass, so the archive corruption and
  per-entry payload checks above have not been run against these binaries.

## Native binary fingerprints

Modern DLL: `ce1b8eb5d821b07beeaf025132f1b5d920e9becd0df98c14b6834cbf50552ecf`

Legacy validation DLL: `c35aaae290c3483ce3d63c6ec619d1afd27f80a28096b2c012ca8867bba8d257`

The public SE/AE archive uses the modern DLL. The legacy DLL is a validation build.

## Observed in game

One session on 1.5.97 with the modern DLL exercised the new acquisition path.
The build used carries the same acquisition changes; only the declaration flags
and source formatting changed afterwards.

- A grip was acquired with a 3DNPC follower and held through walking and running
  at player speeds around 170 to 180.
- Measured wrist separation while held was **4.6 to 8.7** units.
- Release ran the full teardown. Disconnect, follower removal, player removal and
  offset clearing all reported success, followed by cleanup completion.

This is a single session, one follower, one body and skeleton setup, on one
runtime. It does not establish clasp appearance on other meshes, behavior over
long sessions, or any 1.6, GOG or 1.7 path.

## Remaining game checks

This packaging pass did not run Skyrim beyond the session above. Verify
pause/input cleanup, physical Unstick recovery, clasp contact and release,
victory transitions, save/load and follower-framework behavior in game. The
1.6/GOG and 1.7 runtime paths still need runtime-specific playtests. Passing
offline tests is not a compatibility guarantee.
