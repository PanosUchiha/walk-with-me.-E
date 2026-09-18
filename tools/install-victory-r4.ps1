$ErrorActionPreference='Stop'
function Assert-GameClosed {
    if(Get-Process SkyrimSE -ErrorAction SilentlyContinue){throw 'Skyrim is still running. Close it before installing Victory r4.'}
}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
Assert-GameClosed
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$review=Join-Path $repo 'build/victory-r4-review'
$manifest=Get-Content -LiteralPath (Join-Path $review 'update-manifest.json') -Raw | ConvertFrom-Json
$installed=(Resolve-Path -LiteralPath (Join-Path $repo '../MODS/mods/Wayfarer')).Path
if($manifest.build -ne 'Walk-With-Me-0.16.8-Victory-r4' -or $manifest.installedRoot -ne $installed){throw 'Unexpected update destination'}
$checked=@()
foreach($entry in $manifest.files){
    $target=[IO.Path]::GetFullPath((Join-Path $installed $entry.relative))
    if(-not $target.StartsWith($installed+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Target escaped Wayfarer directory'}
    if((Hash $entry.source) -ne $entry.sha256){throw "Staged file changed: $($entry.relative)"}
    if($null -eq $entry.before){if(Test-Path -LiteralPath $target){throw "New file already exists: $target"}}
    elseif(-not (Test-Path -LiteralPath $target) -or (Hash $target) -ne $entry.before){throw "Installed file changed: $target"}
    $checked+=@{entry=$entry;target=$target}
}
$snapshot=@{}
foreach($file in Get-ChildItem -LiteralPath $installed -File -Recurse){$snapshot[$file.FullName]=Hash $file.FullName}
$backup=Join-Path $repo ('backups/installed-pre-victory-r4-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
foreach($item in $checked){
    if($null -ne $item.entry.before){
        $saved=Join-Path $backup $item.entry.relative
        New-Item -ItemType Directory -Force -Path (Split-Path $saved) | Out-Null
        Copy-Item -LiteralPath $item.target -Destination $saved
        if((Hash $saved) -ne $item.entry.before){throw 'Backup hash mismatch'}
        $item.backup=$saved
    }
}
$written=@()
try {
    foreach($item in $checked){
        Assert-GameClosed
        New-Item -ItemType Directory -Force -Path (Split-Path $item.target) | Out-Null
        $written+=$item
        Copy-Item -LiteralPath $item.entry.source -Destination $item.target -Force
        if((Hash $item.target) -ne $item.entry.sha256){throw 'Installed file hash mismatch'}
    }
    $targets=@($checked | ForEach-Object {$_.target})
    foreach($path in $snapshot.Keys){
        if($path -notin $targets -and (Hash $path) -ne $snapshot[$path]){throw "Unrelated file changed: $path"}
    }
} catch {
    foreach($item in $written){
        if($item.backup){Copy-Item -LiteralPath $item.backup -Destination $item.target -Force}
        elseif(Test-Path -LiteralPath $item.target){Remove-Item -LiteralPath $item.target}
    }
    throw
}
$receipt=@{build=$manifest.build;installedAt=(Get-Date -Format o);backup=$backup;inGameTested=$false;files=@($checked | ForEach-Object {@{destination=$_.target;before=$_.entry.before;sha256=$_.entry.sha256}});preservedExistingFiles=@($snapshot.Keys | Where-Object {$_ -notin $targets}).Count}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $review 'install.json') -Encoding utf8
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'install.json') -Encoding utf8
$receipt | ConvertTo-Json -Depth 6
