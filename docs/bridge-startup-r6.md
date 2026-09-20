# Bridge exit and departure review — candidate r6

The September 19 r5 playtest improved open-field travel but still struggled in
narrow areas and after the bridge beyond Honningbrew Meadery. The captured log
ends at **18:07:10.514**. Evidence is saved in
`build/bridge-startup-review-20260919-r6/Walk With Me.log` and `analysis.json`.

## Findings

- The last thirty seconds contain repeated MovementBlocked runtime packages,
  stationary samples and fifteen TravelRetry events. Travel aliases remain assigned.
- Around 18:07:06–07, three companions receive the same `(37830,-12791,-4035)`
  waypoint. R5 renews sampled waypoints after one second and marks them changed
  even when their coordinates are identical. That can force another AI refresh.
- Route guidance is disabled beyond 1,400 units from the player. The final samples
  show Rumarin and Katana far enough behind to lose it while still struggling.
- Near waypoints produce small goal distances and lower pace estimates. Returning
  to distant formation goals produces much larger catch-up estimates, reaching
  3.5x in this window. This creates an avoidable stop/acceleration cycle.
- R5's navmesh collection samples only 1,200 units around the player, which can
  omit an attached neighboring cell containing a lagging companion. This is a
  code limit; the existing log does not prove which meshes were omitted.
- Startup uses the configured 0.30-second sustained-movement check, then the
  individual reaction delay (0.17–0.45 seconds for this party), plus an update
  interval. These delays can stack unnecessarily.

`CorridorPosition connected=false` means the player's ordinary corridor branch
was used. It does **not** mean the navmesh is disconnected. The log cannot prove
a bridge/navmesh conflict. One automatic recall of M'rissi occurred earlier at
18:06:13 from 2,774 units; no recall appears in the final thirty-second window.

## Changes

- Rechecking the same waypoint, or less than 25 units of sample jitter, retains
  the point without flagging a new destination. A changed corner moves its marker
  through the normal repath checks rather than forcing an extra reset.
- Check for onward routing every half second. Brief failed probes retain the last
  verified waypoint for at most two seconds; arrival, direct-route availability,
  release and hand guidance clear it as appropriate. Existing stall recovery remains.
- Extend guidance to 2,800 units. Collect attached cells across a full neighboring
  cell span. The graph search remains capped at 256 nodes, with a 3,400-unit radius,
  512-unit height limit and 6,000-unit route budget. Collision-ray caps are unchanged.
- Intermediate waypoints maintain a cruise-distance allowance while the final
  destination is still far away. Corner guidance uses the user's normal travel-speed
  cap rather than the doubled distant catch-up boost. Open-field pace is unchanged.
- Personal departure reactions now overlap sustained-movement detection. The first
  ready movement update runs immediately, without an extra update-interval wait.
  The initial check still rejects brief incidental steps; user settings are preserved.
- Expanded RouteCorner diagnostics show active/changed/probe state, stale age,
  actual waypoint and final destination distance.

No geometry, navmesh records, collision layers, actor positions or behavior files
are changed. These are steering changes; native AI still handles actor avoidance.
Shared narrow passages and missing navmesh links can still cause difficulty.
Victory revision 5 and its three-enemy minimum are retained.

## Validation and playtest

The modern Release DLL built successfully. All **19 CTest suites passed**, including
new tests for identical waypoint renewal, jitter, failed-probe expiry, direct-route
release, intermediate pacing and overlapping departure delays. Tests do not run
Skyrim/Havok, so r6 remains an unverified in-game candidate.

Repeat the bridge-to-hills route with the same party, then a stop/start in the open
field and Lost Prospect Mine in both directions. Compare RouteCorner changed=false
renewals, TravelRetry frequency, stationary periods, catch-up speed and time to
resume movement. Confirm returning to open ground restores ordinary travel.

Main, Command Gestures and source archives remain separate. The gesture assets
and behavior patch are unchanged from r5; no regeneration is needed for this update.
