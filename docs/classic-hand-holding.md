# Classic hand holding option

Travel > Hand-holding method now offers Classic and TETHER.
The enable toggle stays off by default and Classic is the default method.
Existing companion/tuning settings are preserved. The original Classic addition
needed no script change; the [latency r2 update](tether-latency-r2.md) now requires
matching scripts for its TETHER changes. No ESP change is required.

Classic restores the retained revision 11.2 procedural arm pose, approach and
sticky attachment, including the direction and scene-refresh fixes made during
the 0.16.2 era. It is not a byte-for-byte rollback to the initial 0.16.2 release.
Its travel route again uses hand spacing, attachment heading, lead distance,
pace and repathing from the classic implementation. Other travel retains the
current behavior. Classic requires no generated clasp clips or TETHER physics.

Changing methods clears Classic's pose/collision/graph leases or pumps TETHER's
asynchronous cleanup before switching. TETHER retains movement ownership and
Papyrus callbacks while cleanup is pending. Classic's hooks are installed once
and pass through while Classic is inactive.

The [r3 stationary-grip fix](classic-ground-r3.md) restores the original grounded
fallback omitted from the first Classic integration, preventing immediate grip
release when its gait marker has no forward travel distance.

## Validation

Modern and legacy Release DLL builds succeeded. All 20 CTest suites passed in
each build, including the original hand-holding/locomotion tests and a new test
of the production method dispatcher with engine-free fake backends. The new
test covers pending cleanup, repeated cleanup waits, reversing a pending
selection, switching in both directions, pause/disable, reset and load.
Existing party_life.h integer-to-float compiler warnings remain.

In-game verification has not been performed. Check both sides, idle/walk/run,
sharp turns, release, companion removal, equipment changes and loading a save.
Test Classic without generated clasp assets; test switching from an attached
TETHER grip and during its acquisition/cleanup. Check that neither actor keeps
collision or pose overrides after release. Skeleton and animation differences
can still cause wrist twisting, sliding or awkward transitions.

## Original local test update (r1)

The Classic-Handholding-r1 ZIP was a DLL-only test update for the current Walk
With Me 0.2.1 base. Install it as a separate mod after the base; keep the base
enabled. It includes no INI, scripts, ESP or animation replacements. Choose
Classic in Travel, enable Hold hands and select a companion. Disabling the test
update restores the previous DLL. Restart Skyrim when changing DLLs.

The original r1 archive used the label Classic (0.16.2 style) and defaulted to
TETHER. The current r2 update uses Classic and defaults to it. These test builds
include the existing local changes and are not separately published releases.
