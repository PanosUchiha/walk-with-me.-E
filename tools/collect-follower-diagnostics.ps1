param(
    [string]$GameDocuments = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'My Games/Skyrim Special Edition'),
    [string]$InstalledMod = (Join-Path $PSScriptRoot '../../MODS/mods/Wayfarer'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot ('../build/follower-captures/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')))
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new capture directory; existing captures are preserved.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$skse = Join-Path $GameDocuments 'SKSE'
$files = @(Get-ChildItem -LiteralPath $skse -File | Where-Object {
    $_.Name -like 'Walk With Me*.log' -or $_.Name -in @('Wayfarer.log','skse64.log','SceneDirector.log')
})
$papyrus = Join-Path $GameDocuments 'Logs/Script'
if (Test-Path -LiteralPath $papyrus) { $files += @(Get-ChildItem -LiteralPath $papyrus -Filter 'Papyrus*.log' -File) }
$crashes = Join-Path $skse 'Crashlogs'
if (Test-Path -LiteralPath $crashes) { $files += @(Get-ChildItem -LiteralPath $crashes -Filter '*.log' -File | Sort-Object LastWriteTime -Descending | Select-Object -First 5) }
$inventory = foreach ($file in $files) {
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $OutputDirectory $file.Name)
    [pscustomobject]@{ Source=$file.FullName; LastWriteTime=$file.LastWriteTime.ToString('o'); Bytes=$file.Length }
}
$ini=Join-Path $InstalledMod 'SKSE/Plugins/Wayfarer.ini'
if(Test-Path -LiteralPath $ini){Copy-Item -LiteralPath $ini -Destination (Join-Path $OutputDirectory 'Wayfarer.ini')}
$dll=Join-Path $InstalledMod 'SKSE/Plugins/Wayfarer.dll'
$dllHash=if(Test-Path -LiteralPath $dll){(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash}else{'unavailable'}
[pscustomobject]@{
    CapturedAt=(Get-Date).ToString('o'); InstalledDLL=$dll; DLLSHA256=$dllHash;
    SkyrimRunning=[bool](Get-Process SkyrimSE -ErrorAction SilentlyContinue); Files=@($inventory);
    Note='Live captures can end mid-write. Quit the game for a final complete capture. No files were deleted.'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'capture.json') -Encoding utf8
Write-Output "Captured $($files.Count) logs to $OutputDirectory"
