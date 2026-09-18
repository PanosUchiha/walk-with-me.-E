# Publish the release

Run `tools/package-release.py` after building the matching DLL, six scripts and
ESP and reviewing the changelog and validation record. It creates these public
files in `release`:

- `Walk-With-Me-0.2.0-SE-AE.zip`
- `Walk-With-Me-0.2.0-Source.zip`
- A SHA-256 file for each archive
- `Walk-With-Me-0.2.0-CHANGELOG.md`

Attach both ZIPs and checksums to the same release. Link the complete source
archive from the mod page and keep it available with the corresponding DLL.
GitHub's automatic source ZIP does not contain the dependency sources.

The public archives include the hand-clasp generator and authored rotation
recipe. Do not upload locally generated clasp archives, extracted game files,
local experiment folders or the workspace backups. Generated clasp clips contain
data from the player's own Skyrim installation and remain local.

Review the public README, license notices and changelog before publishing.
Packaging does not upload files, push Git commits or create a hosted release.
