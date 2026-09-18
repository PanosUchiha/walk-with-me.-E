$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$stage = (Resolve-Path -LiteralPath (Join-Path $repo 'build/Walk-With-Me-0.16.8-Companion-UI-r2')).Path
$destination = (Resolve-Path -LiteralPath (Join-Path $repo '../MODS/mods/Wayfarer')).Path
$manifest = Get-Content -LiteralPath (Join-Path $stage 'test-build.json') -Raw | ConvertFrom-Json
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $repo "backups/installed-pre-companion-ui-$stamp"
if (Test-Path -LiteralPath $backup) { throw 'Backup already exists.' }
function FileHash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
foreach ($entry in $manifest.files.PSObject.Properties) {
    if ((FileHash (Join-Path $stage $entry.Name)) -ne $entry.Value) { throw "Invalid staged file: $($entry.Name)" }
}
$settings = Join-Path $destination 'SKSE/Plugins/Wayfarer.ini'
$settingsHash = FileHash $settings
$settingsBefore = [IO.File]::ReadAllText($settings)
$settingsAfter = [regex]::Replace($settingsBefore, '(?m)^bAutoDiscover\s*=.*$', 'bAutoDiscover = false' + "`r")
$settingsAfter = [regex]::Replace($settingsAfter, '(?m)^iEnrollmentVersion\s*=.*$', 'iEnrollmentVersion = 2' + "`r")
if ($settingsAfter -notmatch '(?m)^iCompanionKey\s*=') {
    $settingsAfter = [regex]::Replace($settingsAfter, '(?m)^\[Hotkeys\]\r?$', "[Hotkeys]`r`niCompanionKey = 34`r`niCompanionModifiers = 0`r")
}
if ($settingsAfter -notmatch '(?m)^bAutoDiscover = false' -or $settingsAfter -notmatch '(?m)^iEnrollmentVersion = 2') { throw 'Could not update enrollment settings.' }
$liveDll = Join-Path $destination 'SKSE/Plugins/Wayfarer.dll'
$oldDllHash = FileHash $liveDll
$changed = @($manifest.files.PSObject.Properties | Where-Object {
    $_.Name -ne 'SKSE/Plugins/Wayfarer.ini' -and
    (!(Test-Path -LiteralPath (Join-Path $destination $_.Name)) -or
     (FileHash (Join-Path $destination $_.Name)) -ne $_.Value)
})
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $settings -Destination (Join-Path $backup 'Wayfarer.ini')
foreach ($entry in $changed) {
    $existing = Join-Path $destination $entry.Name
    if (Test-Path -LiteralPath $existing) {
        $saved = Join-Path $backup $entry.Name
        New-Item -ItemType Directory -Path (Split-Path $saved -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $existing -Destination $saved
    }
}
if (Test-Path -LiteralPath (Join-Path $destination 'companion-ui-install.json')) {
    Copy-Item -LiteralPath (Join-Path $destination 'companion-ui-install.json') -Destination $backup
}
$gameRunning = @(Get-Process SkyrimSE -ErrorAction SilentlyContinue).Count -gt 0
if ($oldDllHash -ne $manifest.dllSHA256) {
    $pending = Join-Path $destination "SKSE/Plugins/Wayfarer.dll.pending-$stamp"
    $loadedBackup = Join-Path $backup 'Wayfarer.loaded.dll'
    # Resolve every move target within the explicitly named mod/backup folders.
    foreach ($path in @($liveDll, $pending)) {
        if (![IO.Path]::GetFullPath($path).StartsWith($destination + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid mod path.' }
    }
    if (![IO.Path]::GetFullPath($loadedBackup).StartsWith($backup + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid backup path.' }
    Copy-Item -LiteralPath (Join-Path $stage 'SKSE/Plugins/Wayfarer.dll') -Destination $pending
    if ((FileHash $pending) -ne $manifest.dllSHA256) { throw 'Pending DLL failed verification.' }
    Move-Item -LiteralPath $liveDll -Destination $loadedBackup
    try { Move-Item -LiteralPath $pending -Destination $liveDll }
    catch { Move-Item -LiteralPath $loadedBackup -Destination $liveDll; throw }
}
foreach ($entry in $changed) {
    if ($entry.Name -eq 'SKSE/Plugins/Wayfarer.dll') { continue }
    $target = Join-Path $destination $entry.Name
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $stage $entry.Name) -Destination $target
}
foreach ($entry in $manifest.files.PSObject.Properties) {
    if ($entry.Name -ne 'SKSE/Plugins/Wayfarer.ini' -and (FileHash (Join-Path $destination $entry.Name)) -ne $entry.Value) {
        throw "Installed file failed verification: $($entry.Name)"
    }
}
if ((FileHash $settings) -ne $settingsHash) { throw 'Settings changed during installation.' }
[IO.File]::WriteAllText($settings, $settingsAfter, [Text.UTF8Encoding]::new($false))
if ([IO.File]::ReadAllText($settings) -ne $settingsAfter) { throw 'Settings failed verification.' }
$receipt = [ordered]@{
    build = $manifest.build
    destination = $destination
    backup = $backup
    installedDLLSHA256 = FileHash $liveDll
    previousDLLSHA256 = $oldDllHash
    otherPreferencesPreserved = $true
    settingsChanges = @('Automatic enrollment off', 'Enrollment migration version 2', 'Companion control key G when previously unset')
    previousSettingsSHA256 = $settingsHash
    settingsSHA256 = FileHash $settings
    restartRequired = $gameRunning
    changedFiles = @($changed | ForEach-Object { $_.Name })
    time = (Get-Date).ToString('o')
}
$json = $receipt | ConvertTo-Json -Depth 5
$json | Set-Content -LiteralPath (Join-Path $backup 'install.json') -Encoding UTF8
$json | Set-Content -LiteralPath (Join-Path $destination 'companion-ui-install.json') -Encoding UTF8
$json
