$ErrorActionPreference = "Stop"

function Show-Device {
    param(
        [Parameter(Mandatory = $true)]
        [Microsoft.Management.Infrastructure.CimInstance] $Device
    )

    Write-Host ""
    Write-Host "Name: $($Device.FriendlyName)"
    Write-Host "Class: $($Device.Class)"
    Write-Host "Status: $($Device.Status)"
    Write-Host "InstanceId: $($Device.InstanceId)"

    $ids = Get-PnpDeviceProperty -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_HardwareIds" -ErrorAction SilentlyContinue
    if ($ids -and $ids.Data) {
        Write-Host "HardwareIds:"
        foreach ($id in $ids.Data) {
            Write-Host "  $id"
        }
    }

    $service = Get-PnpDeviceProperty -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_Service" -ErrorAction SilentlyContinue
    if ($service -and $service.Data) {
        Write-Host "Service: $($service.Data)"
    }

    $upperFilters = Get-PnpDeviceProperty -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_UpperFilters" -ErrorAction SilentlyContinue
    if ($upperFilters -and $upperFilters.Data) {
        Write-Host "UpperFilters:"
        foreach ($filter in $upperFilters.Data) {
            Write-Host "  $filter"
        }
    }

    $lowerFilters = Get-PnpDeviceProperty -InstanceId $Device.InstanceId -KeyName "DEVPKEY_Device_LowerFilters" -ErrorAction SilentlyContinue
    if ($lowerFilters -and $lowerFilters.Data) {
        Write-Host "LowerFilters:"
        foreach ($filter in $lowerFilters.Data) {
            Write-Host "  $filter"
        }
    }
}

Write-Host "Connected GameSir / Cyclone / XInput-ish devices"
Write-Host "Run this while the Cyclone 2 is connected through the 2.4g dongle in the exact mode you use in games."

$devices = Get-PnpDevice -PresentOnly | Where-Object {
    $_.FriendlyName -match "GameSir|Cyclone|Xbox|XInput|XUSB|Controller|HID-compliant game controller" -or
    $_.InstanceId -match "VID_3537|HID\\VID_|IG_|XINPUT|XUSB"
} | Sort-Object Class, FriendlyName, InstanceId

foreach ($device in $devices) {
    Show-Device -Device $device
}

Write-Host ""
Write-Host "What to look for:"
Write-Host "1. A HIDClass child whose HardwareIds include HID\\VID_xxxx&PID_xxxx&IG_nn."
Write-Host "2. Whether UpperFilters contains Cyclone2DpadFilter on that same HIDClass child after install."
Write-Host "3. If the game-facing device is Xbox/XInput/XUSB/XnaComposite, this HID filter is not on the game input path."
