param(
    [string]$Python = "python",
    [string]$VcpkgRoot = "X:\vcpkg",
    [string]$DependencyPrefix = "X:\!--- Main\Documents\Nolvus\_ops_native\OutfitPreviewSelectorCamera\build\skyrim\vcpkg_installed\x64-windows-static-md",
    [string]$CMake = "X:\VisualStudio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    [string]$PapyrusCompiler = "$PSScriptRoot\..\_ops_tools\papyrus-compiler-2026.03.15\papyrus-compiler\Original Compiler\PapyrusCompiler.exe",
    [string]$PapyrusImports = "$PSScriptRoot\..\_mos_native_research\skse64\scripts\vanilla",
    [string]$PapyrusFlags = "$PSScriptRoot\..\_mos_native_research\skse64\scripts\vanilla\TESV_Papyrus_Flags.flg"
)

$ErrorActionPreference = "Stop"
$build = Join-Path $PSScriptRoot "build\manual"
$package = Join-Path $PSScriptRoot "package"
$release = Join-Path $PSScriptRoot "release"
$cmakePrefix = $DependencyPrefix
$versionMatch = [regex]::Match((Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Raw), 'project\(Wayfarer VERSION ([0-9.]+)')
if (-not $versionMatch.Success) { throw "Cannot determine release version." }
$version = $versionMatch.Groups[1].Value

& $CMake -S $PSScriptRoot -B $build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$cmakePrefix" "-DSIMPLEINI_INCLUDE_DIRS=$DependencyPrefix\include"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

& $CMake --build $build --config Release --parallel 8
if ($LASTEXITCODE -ne 0) { throw "Native build failed." }

& (Join-Path (Split-Path $CMake) "ctest.exe") --test-dir $build -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed." }

Copy-Item (Join-Path $build "bin\Release\Wayfarer.dll") (Join-Path $package "SKSE\Plugins\Wayfarer.dll") -Force

Push-Location (Join-Path $package "Scripts\Source")
try {
    $imports = $PapyrusImports + ';' + (Join-Path $package "Scripts\Source")
    foreach ($script in @('Wayfarer.psc', 'WayfarerQuestScript.psc', 'WayfarerDialogueAdd.psc', 'WayfarerDialogueRemove.psc', 'WayfarerTether.psc', 'WayfarerRecovery.psc')) {
        & $PapyrusCompiler $script ('-i=' + $imports) ('-f=' + $PapyrusFlags) ('-o=' + (Join-Path $package 'Scripts'))
        if ($LASTEXITCODE -ne 0) { throw "Papyrus compilation failed: $script" }
    }
}
finally {
    Pop-Location
}

& (Join-Path $PSScriptRoot "tools\build-esl.ps1") -Out (Join-Path $package "Wayfarer.esp")

Copy-Item (Join-Path $PSScriptRoot "README.md") (Join-Path $package "README.md") -Force
Copy-Item (Join-Path $PSScriptRoot "LICENSE") (Join-Path $package "LICENSE") -Force
New-Item -ItemType Directory -Force -Path (Join-Path $package 'docs') | Out-Null
foreach ($document in @('api-guide.md', 'nexus.md', "changes-$version.md", 'testing.md', 'follower-dialogue.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "docs\$document") -Destination (Join-Path $package "docs\$document") -Force
}



& (Join-Path $PSScriptRoot 'tools/build-compatibility.ps1') -Vcpkg (Join-Path $VcpkgRoot 'vcpkg.exe') -CMake $CMake
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'build/modern/bin/Release/Wayfarer.dll') -Destination (Join-Path $package 'SKSE/Plugins/Wayfarer.dll') -Force
& $Python -X utf8 (Join-Path $PSScriptRoot 'tools/package-release.py') --vcpkg-root $VcpkgRoot
if ($LASTEXITCODE -ne 0) { throw 'Unified release packaging failed.' }
