param([string]$ClangFormat = 'clang-format', [switch]$Check)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$files = Get-ChildItem (Join-Path $root 'src'), (Join-Path $root 'include'), (Join-Path $root 'tests') -File |
    Where-Object { $_.Extension -in '.cpp', '.h' -and $_.Name -ne 'SKSEMenuFramework.h' }
foreach ($file in $files) {
    if ($Check) { & $ClangFormat --dry-run --Werror $file.FullName }
    else { & $ClangFormat -i $file.FullName }
    if ($LASTEXITCODE -ne 0) { throw "Formatting check failed: $($file.FullName)" }
}
Write-Output "Checked $($files.Count) first-party files."
