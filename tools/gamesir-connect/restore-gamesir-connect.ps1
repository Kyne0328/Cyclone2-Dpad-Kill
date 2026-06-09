<#
.SYNOPSIS
    Undoes the Cyclone 2 D-pad patch in GameSir Connect.

.DESCRIPTION
    Restores the unpacked nodehid.js from its *.cyclonebak (the normal patch
    path), and also restores app.asar from app.asar.bak if the asar-repack
    fallback was ever used.
#>
$ErrorActionPreference = "Stop"

$root        = Join-Path $env:LOCALAPPDATA "Programs\gamesir_connect\resources"
$unpackedHid = Join-Path $root "app.asar.unpacked\node_modules\node-hid"
$asar        = Join-Path $root "app.asar"
$bak         = Join-Path $root "app.asar.bak"

Write-Host "Closing GameSir Connect..."
Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path -like "*gamesir_connect*" } |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$restored = $false

if (Test-Path $unpackedHid) {
    $main = "nodehid.js"
    $pkgJson = Join-Path $unpackedHid "package.json"
    if (Test-Path $pkgJson) {
        $pkg = Get-Content $pkgJson -Raw | ConvertFrom-Json
        if ($pkg.main) { $main = $pkg.main }
    }
    $mainPath = Join-Path $unpackedHid ($main -replace '^\./', '')
    $bakMain  = "$mainPath.cyclonebak"

    if (Test-Path $bakMain) {
        Copy-Item $bakMain $mainPath -Force
        Remove-Item $bakMain -Force
        Write-Host "Restored unpacked nodehid.js and removed its backup."
        $restored = $true
    }
}

if (Test-Path $bak) {
    Copy-Item $bak $asar -Force
    Write-Host "Restored app.asar from app.asar.bak."
    $restored = $true
}

if (-not $restored) {
    Write-Host "No patch backups found; nothing to restore."
}
