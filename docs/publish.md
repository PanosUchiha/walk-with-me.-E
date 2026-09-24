# Publish the release

Run `tools/package-release.py` after building the matching DLL, seven scripts and
ESP and reviewing the changelog and validation record. It creates these public
files in the selected output directory (`build-release.ps1` defaults to `release/0.2.2`):

- `Walk-With-Me-0.2.2-SE-AE.zip` (Main Files)
- `Walk-With-Me-0.2.2-Command-Gestures.zip` (Optional Files)
- `Walk-With-Me-0.2.2-Source.zip` (complete source; developer download)
- A SHA-256 file for each archive
- `Walk-With-Me-0.2.2-CHANGELOG.md`

Use `docs/nexus-release-0.2.2.md` for the release post and file descriptions.
The changelog compares against public 0.2.0; 0.2.1 was packaged locally.
The gesture guide links to the mod's Files tab. That link will offer this add-on
only after it is uploaded; packaging alone does not make it downloadable there.

Attach all three ZIPs and checksums to the same release. Link the complete source
archive from the mod page and keep it available with the corresponding DLL.
GitHub's automatic source ZIP does not contain the dependency sources.

The public archives include the hand-clasp generator and authored rotation
recipe. Do not upload locally generated clasp archives, extracted game files,
local experiment folders or the workspace backups. Generated clasp clips contain
data from the player's own Skyrim installation and remain local.

Review the public README, license notices and changelog before publishing.
Packaging does not upload files, push Git commits or create a hosted release.
