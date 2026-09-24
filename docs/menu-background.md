# Settings background

The user-supplied `assets/WalkWithME.png` is copied unchanged to
`package/Interface/WalkWithMe/menu-background.png`. It is a 742 × 465 RGBA PNG.

Every settings page draws it once behind its widgets, at 20% opacity in the
bottom-right corner. Its width is at most half the panel's usable width; its
height is capped at 45% of the usable height. The original proportions and alpha
channel are preserved. Window coordinates keep it fixed while content scrolls.
The draw call creates no interactive widget and does not affect content sizing.
If the texture is unavailable, the settings remain usable without the artwork.

The yellowish menu palette, checkbox columns, wheel and gameplay HUD are unchanged.
No INI change is required. PNG source assets are included in source archives, and
the package verifier permits this background while still rejecting obsolete raster icons.

Validation: both existing DLL configurations build; 24/24 CTest suites pass in
each. Package validation and all 113 required installed Menu Framework exports
pass. Offline renders of the actual settings code cover all six pages, a narrow
panel and scrolling. In-game appearance remains to be checked.

Source PNG SHA-256: `37BD1F17B62A7998A9481F6E9687E8EEA64EA3B4FF20C1FCAB9317529CDF0738`.
Source backup: `backups/menu-background-source-20260924-003401`.
Installation backups and hashes: `backups/menu-background-preinstall-*`.
