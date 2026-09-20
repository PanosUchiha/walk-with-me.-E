# Walk With Me 0.2.1 validation

The user approved the installed r9 candidate on September 20, 2026 as the best
movement version tested. Release 0.2.1 promotes that behavior with updated
release metadata and documentation.

- Modern Release rebuild succeeded; **19/19 native test suites** and
  **5/5 animation packaging tests** passed.
- First-party `src`, `include` and test files match the r9 source archive.
- DLL version comes from CMake 0.2.1; the packager checks the SKSE export version
  and modern runtime metadata before writing the public archives.
- OAR condition requirements remain at 0.2.0 to retain existing clip compatibility.
- Separate Main and Command Gestures archives carry internal SHA-256 manifests;
  Main, Command Gestures and complete Source each have an archive checksum.
- Scripts, ESP, animation clips and behavior patch inputs are unchanged from r9.

The version-labelled rebuild has not received a separate in-game playtest.
User acceptance applies to r9's identical movement and animation implementation.
Older runtime compatibility limits and narrow-passage limitations still apply;
see [release.md](release.md) and [baseline-0.2.1.md](baseline-0.2.1.md).
