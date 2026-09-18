# Build

Needs Visual Studio 2022 C++ tools, Windows SDK, CMake, vcpkg and Python 3.
The public DLL uses CommonLibSSE-NG 7.5.1 at
`bedcb1e05418baba7b316a650b6180c2dd6007a8`.

## Git checkout

From the repo folder, use your actual tool paths:

```powershell
.\tools\build-compatibility.ps1 -Vcpkg 'C:\vcpkg\vcpkg.exe' -CMake 'C:\path\to\cmake.exe'
```

This checks the CommonLib revision, installs dependencies, builds and runs CTest.
Output: `build/modern/bin/Release/Wayfarer.dll`. The root `skyrim` CMake preset
uses the older dependency setup; use the helper above for the public build.

## Complete source ZIP

Extract all folders. `WalkWithMe` is the mod; `CommonLibSSE-NG-7.5.1` is its library.
`Dependencies` holds dependency sources. `DependencyProvenance` records installed
versions and ports.

The extracted library has no Git metadata. From the archive's top folder, with
CMake on PATH:

```powershell
$commonlib = (Resolve-Path '.\CommonLibSSE-NG-7.5.1').Path
$modSource = (Resolve-Path '.\WalkWithMe').Path
$installed = Join-Path $modSource 'build\deps\vcpkg-7.5.1'
& 'C:\vcpkg\vcpkg.exe' install "--x-manifest-root=$commonlib" "--x-install-root=$installed" --triplet=x64-windows-static-md --x-no-default-features
$prefix = Join-Path $installed 'x64-windows-static-md'
cmake -S $modSource -B "$modSource\build\modern" -G 'Visual Studio 17 2022' -A x64 "-DWAYFARER_COMMONLIB_SOURCE=$commonlib" "-DCMAKE_PREFIX_PATH=$prefix" "-DSIMPLEINI_INCLUDE_DIRS=$prefix/include" -DBUILD_TESTS=OFF -DENABLE_SKYRIM_SE=ON -DENABLE_SKYRIM_AE=ON -DENABLE_SKYRIM_VR=OFF -DSKSE_SUPPORT_XBYAK=ON -DSKSE_SUPPORT_PATCH_SAFETY=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
cmake --build "$modSource\build\modern" --config Release --parallel 8
ctest --test-dir "$modSource\build\modern" -C Release --output-on-failure
```

Stop on errors. Compare installed versions with
`DependencyProvenance/vcpkg-status.txt` for a historical rebuild. The ZIP preserves
the source versions used. Different compilers or SDKs may change the DLL bytes.

## Package

`tools/build-esl.ps1` writes the ESP and SEQ. Compile all six scripts in
`package/Scripts/Source` using the Papyrus compiler, vanilla/SKSE imports and
TESV_Papyrus_Flags.flg. Obtain those game/SDK tools separately.

`build-release.ps1` handles those steps and both DLL builds. Its default paths are
local to the author; override them, including DependencyPrefix for the legacy build.

With the ESP, scripts and assets staged, run from a Git checkout:

```powershell
python -X utf8 tools/package-release.py --vcpkg-root C:/vcpkg
```

Keep vcpkg's `buildtrees/*/src/*.clean` folders for source packaging.
The public packager includes the local clasp generator and its GPL-licensed
PyNifly codec source. It rejects game-derived clasp clips and checks every
bundled original animation against `assets/release-animation-hashes.json`.
See hand-holding.md to generate the optional clips from a user's own game files;
keep that generated ZIP separate from public downloads.

Upload both public ZIPs and their checksums. Test the result in game.

## Source style

Use the existing `.clang-format` (clang-format 19) for first-party C++:

```powershell
.\tools\format-source.ps1 -ClangFormat 'C:\path\to\clang-format.exe'
.\tools\format-source.ps1 -ClangFormat 'C:\path\to\clang-format.exe' -Check
```

Keep direct functions, plain state structs, explicit ownership and early exits,
as in the 0.16.7 baseline. Do not introduce a framework for a single operation.
Third-party API/codec sources retain their upstream style. The default public
packager is `tools/package-release.py`; older prototype packagers are historical.
