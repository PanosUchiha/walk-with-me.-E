# Publish the release

Run `tools/package-release.py` after building the matching DLL, seven scripts and
ESP and reviewing the changelog and validation record. It creates these public
files in the selected output directory (`build-release.ps1` defaults to `release/0.2.5`):

- `Walk-With-Me-0.2.5-SE-AE.zip` (Main Files)
- `Walk-With-Me-0.2.5-Command-Gestures.zip` (Optional Files)
- `Walk-With-Me-0.2.5-Source.zip` (complete source; developer download)
- A SHA-256 file for each archive
- `Walk-With-Me-0.2.5-CHANGELOG.md`

Use the changelog for the release post. It compares against 0.2.2; there was
no 0.2.3 or 0.2.4.
The gesture guide links to the mod's Files tab. That link will offer this add-on
only after it is uploaded; packaging alone does not make it downloadable there.

Attach all three ZIPs and checksums to the same release. Link the complete source
archive from the mod page and keep it available with the corresponding DLL.
GitHub's automatic source ZIP does not contain the dependency sources.

The main archive includes the 68 hand-clasp clips staged in `package/`. The
packager ships only the animation hashes listed in
`assets/release-animation-hashes.json`. Do not upload extracted game files, local
experiment folders or the workspace backups.

Review the public README, license notices and changelog before publishing.
Packaging does not upload files, push Git commits or create a hosted release.
