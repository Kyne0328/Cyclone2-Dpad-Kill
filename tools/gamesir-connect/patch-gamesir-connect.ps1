<#
.SYNOPSIS
    Patches GameSir Connect to neutralize the Cyclone 2 D-pad.

.DESCRIPTION
    node-hid is a native module, so Electron unpacks the entire node-hid folder
    to app.asar.unpacked and loads nodehid.js from there at runtime -- the copy
    inside app.asar is ignored. So we append the neutralizer directly to the
    UNPACKED nodehid.js. No asar extract/repack needed.

    Idempotent: the original nodehid.js is saved once as nodehid.js.cyclonebak,
    and every run restores from it before re-appending, so the snippet is never
    added twice.

    Falls back to asar extract/repack only if node-hid is not unpacked (unusual).

.PARAMETER Log
    Patch with logging enabled (raw HID reports -> <temp>\gamesir_hid_log.txt)
    to confirm the real D-pad offset.
#>
param(
    [switch]$Log
)

$ErrorActionPreference = "Stop"

$root        = Join-Path $env:LOCALAPPDATA "Programs\gamesir_connect\resources"
$unpackedHid = Join-Path $root "app.asar.unpacked\node_modules\node-hid"
$snippetSrc  = Join-Path $PSScriptRoot "dpad-neutralizer.js"
$marker      = "=== Cyclone2 D-pad neutralizer (injected) ==="

if (-not (Test-Path $snippetSrc)) { throw "Neutralizer not found: $snippetSrc" }

Write-Host "Closing GameSir Connect..."
Get-Process -ErrorAction SilentlyContinue |
    Where-Object { ($_.Path -and $_.Path -like "*gamesir_connect*") -or ($_.Name -like "*gamesir*") } |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800

# Resolve node-hid's main module inside the unpacked folder.
function Get-HidMainPath {
    param([string]$HidDir)
    $main = "nodehid.js"
    $pkgJson = Join-Path $HidDir "package.json"
    if (Test-Path $pkgJson) {
        $pkg = Get-Content $pkgJson -Raw | ConvertFrom-Json
        if ($pkg.main) { $main = $pkg.main }
    }
    return (Join-Path $HidDir ($main -replace '^\./', ''))
}

$snippet = Get-Content $snippetSrc -Raw
if ($Log) {
    $snippet = $snippet -replace "var LOG = false;", "var LOG = true;"
    Write-Host "Logging ENABLED -> %TEMP%\gamesir_hid_log.txt (in the app's temp dir)"
}

if (Test-Path $unpackedHid) {
    $mainPath = Get-HidMainPath -HidDir $unpackedHid
    if (-not (Test-Path $mainPath)) { throw "Unpacked node-hid main not found: $mainPath" }

    $bakMain = "$mainPath.cyclonebak"
    if (-not (Test-Path $bakMain)) {
        Copy-Item $mainPath $bakMain -Force
        Write-Host "Backed up $(Split-Path $mainPath -Leaf) -> *.cyclonebak"
    } else {
        # Restore the pristine original first so we never double-append.
        Copy-Item $bakMain $mainPath -Force
        Write-Host "Restored pristine nodehid.js from backup before re-patching."
    }

    # GameSir ships nodehid.js read-only; clear the attribute before writing.
    attrib -R "$mainPath" 2>$null

    try {
        Add-Content -Path $mainPath -Value "`n// $marker`n$snippet" -ErrorAction Stop
    } catch {
        throw "Cannot write $mainPath. Close GameSir Connect fully (check Task " +
              "Manager / tray for any 'GameSir' process), then re-run. If it " +
              "persists, run this PowerShell as Administrator. ($_)"
    }

    if (-not (Select-String -Path $mainPath -SimpleMatch $marker -Quiet)) {
        throw "Append verification failed on $mainPath"
    }

    Write-Host "Patched (direct): $mainPath"
    Write-Host ""
    Write-Host "Done. Launch GameSir Connect and test the D-pad."
    if ($Log) { Write-Host "Press the D-pad, then check the log for the report bytes." }
    return
}

#
# Fallback: node-hid is packed inside app.asar (no unpacked copy). Extract,
# inject, repack. Requires Node.js / npx.
#
Write-Host "node-hid is not unpacked; falling back to asar repack."

$asar    = Join-Path $root "app.asar"
$bak     = Join-Path $root "app.asar.bak"
$work    = Join-Path $env:TEMP "gamesir_asar_work"
$asarPkg = "@electron/asar@3.2.10"

if (-not (Test-Path $asar)) { throw "app.asar not found: $asar" }
if (-not (Get-Command npx -ErrorAction SilentlyContinue)) {
    throw "npx (Node.js) not found on PATH. Install Node.js first."
}

if (-not (Test-Path $bak)) {
    Copy-Item $asar $bak -Force
    Write-Host "Backed up app.asar -> app.asar.bak"
}
Copy-Item $bak $asar -Force

if (Test-Path $work) { Remove-Item $work -Recurse -Force }
npx --yes $asarPkg extract "$asar" "$work"
if ($LASTEXITCODE -ne 0) { Copy-Item $bak $asar -Force; throw "asar extract failed ($LASTEXITCODE). Restored." }

$nodeHid = Join-Path $work "node_modules\node-hid"
if (-not (Test-Path $nodeHid)) { throw "node-hid not found in app.asar." }
$mainPath = Get-HidMainPath -HidDir $nodeHid
Add-Content -Path $mainPath -Value "`n// $marker`n$snippet"

npx --yes $asarPkg pack "$work" "$asar" --unpack "*.node"
if ($LASTEXITCODE -ne 0) { Copy-Item $bak $asar -Force; throw "asar pack failed ($LASTEXITCODE). Restored." }

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host "Done (asar repack). Launch GameSir Connect and test the D-pad."
