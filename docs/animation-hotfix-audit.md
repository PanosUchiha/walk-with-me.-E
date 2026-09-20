# Animation hotfix audit — 19 September 2026

## Findings

The [live description](https://www.nexusmods.com/skyrimspecialedition/mods/191283?tab=description)
lists only Address Library and Menu Framework in its Nexus requirements table.
OAR, FPI and GPMA are mentioned later as optional gesture requirements. The
combined download nevertheless installs seven GPMA extension nodes even if the
user does not install GPMA. Those nodes reference missing original GPMA nodes;
they are not a standalone behavior patch. The exact reported master.txt error
code/log was not available, so this is a confirmed packaging flaw, not a claim
that every master.txt failure has this cause.

The patch generator also serialized counted arrays inline and removed the
two-tab root indentation used by GPMA. Nemesis uses a line-oriented parser
([upstream source](https://github.com/ShikyoKira/Project-New-Reign---Nemesis-Main/blob/master/src/update/functionupdate.cpp)),
so XML validity and a successful Pandora result are insufficient compatibility
checks. The serializer now preserves Havok layout and validates array lengths.

The [OAR report](https://www.nexusmods.com/skyrimspecialedition/mods/191283?tab=posts)
suggests changing requiredPlugin to Wayfarer.esp, then installing Detection
Plugin. WWM registers its own provider as `Walk With Me`; the ESP is unrelated
to that lookup. No shipped WWM config uses Detection Plugin conditions, and the
local FPI configs inspected do not declare that dependency either. Do not add a
blanket Detection Plugin requirement without the actual missing-condition log.

Existing uncommitted work already added OAR V3-to-V2 negotiation and the legacy
custom-condition discriminator for OAR 2.x. This audit preserves that work,
includes it in the rebuilt DLL, adds actionable failure logging, and prevents
command gestures from starting when the OAR DLL is not loaded.

## Implemented

- Public packaging separates GPMA and FPI-dependent signal configs/clips into
  Command-Gestures.zip. Base follower and victory files remain in SE-AE.zip.
- Both downloads retain license notices and checksums; the complete source ZIP
  keeps the combined staging files for rebuilding.
- Patch generation and release validation check Nemesis text layout and counts.
- Updated Nexus Markdown/BBCode, release/build instructions and gesture recovery
  steps explain replacement installs, GPMA conflict priority, generation, OAR
  provider names and locally generated clasp clips.

## Validation and release limits

Release DLL build succeeded; 19 CTest tests and five animation packaging tests
passed. Public staged-package verification passed. Existing Menu Framework
header compiler warnings remain.
The public packager successfully wrote and verified all three candidate ZIPs,
including per-file hashes, archive integrity and the approved animation inventory.
Copy the three animation requirement entries in docs/nexus.md into Nexus's
Requirements table as well as the description when publishing.

Still required before publishing: clean Nemesis generation with GPMA 1.2 plus
this add-on; fresh Pandora generation; startup registration and playback with
OAR 2.x and 3.x; third/first-person gestures while walking; shoulder reaction;
locally generated clasp selection. Offline checks do not establish these results.

This task changes local source and page drafts. The live Nexus description,
requirements table and downloads have not been changed. Test packages retain
the current development version; assign a release version before publication.
