<#
.SYNOPSIS
    Extracts app.asar to a temp folder and greps for HID / D-pad / controller
    logic. Diagnostic only -- use if the auto-patch reports node-hid is missing,
    or to confirm how GameSir Connect reads input.
#>
$ErrorActionPreference = "Stop"

$root = Join-Path $env:LOCALAPPDATA "Programs\gamesir_connect\resources"
$asar = Join-Path $root "app.asar"
$work = Join-Path $env:TEMP "gamesir_asar_search"
$out  = Join-Path ([Environment]::GetFolderPath("Desktop")) "gamesir_search_hits.txt"

if (-not (Test-Path $asar)) { throw "app.asar not found: $asar" }

if (Test-Path $work) { Remove-Item $work -Recurse -Force }
npx --yes @electron/asar@3.2.10 extract "$asar" "$work"

$patterns = @(
    "node-hid", "HID.devices", "new HID", "\.read\(", "on\(['""]data",
    "getFeatureReport", "dpad", "d-pad", "hat", "pov",
    "xinput", "sapjoy", "VID_3537", "PID_100B", "PID_1053", "3537"
)

Get-ChildItem $work -Recurse -File -Include *.js, *.json |
    Select-String -Pattern $patterns |
    Select-Object Path, LineNumber, Line |
    Out-File $out -Width 400

Write-Host "Hits written to: $out"
Write-Host "Extracted tree kept at: $work"
