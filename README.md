# Cyclone2DpadFilter

Windows KMDF HID upper-filter driver scaffold for GameSir Cyclone 2 D-pad filtering.

> **Covering XInput and the GameSir Connect app:** this HID upper filter only
> sits on the DirectInput HID path (`IG_01`). Most games read **XInput**
> (`xusb22`, `MI_00`), which this filter never touches. To neutralize the D-pad
> across every consumer at once -- XInput games, DirectInput apps, and the
> GameSir Connect config app -- see the USB lower filter in
> [Cyclone2UsbFilter.md](Cyclone2UsbFilter.md), which attaches at the USB
> composite parent.

## Current INF target

The INF is an Extension INF, not a primary HIDClass device INF. It adds the filter above the existing in-box HID function driver.

The model section is decorated as `NTamd64.10.0...18362` because:

- `DIRID 13` requires Windows 10 build 16299 or newer.
- `DDInstall.Filters` / `AddFilter` requires Windows 10 build 18362 or newer.

So the effective minimum install target is Windows 10 version 1903 / build 18362 or newer.

The current Cyclone 2 USB game-controller target is:

```inf
HID\VID_3537&PID_100B&IG_01
```

This is the HID game-controller child device, not the USB composite/Xbox interface parent. The earlier `USB\VID_3537&PID_100B&MI_00` target installs on the XnaComposite/Xbox 360 interface and does not intercept the HID input report stream used by this filter.

For the 2.4g dongle, do not assume this same HID ID is the game-facing device.
The Cyclone 2 can expose different devices depending on connection type and
input mode. In PC dongle mode it commonly presents an XInput/XUSB gamepad path
for games, plus one or more HID paths for configuration/telemetry. Values
captured from a HID logger can be correct while still not affecting the XInput
controller used by the game.

Run the inspection helper while connected through the dongle:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\inspect-cyclone2-paths.ps1
```

If the game-facing device is Xbox/XInput/XUSB/XnaComposite, this HIDClass upper
filter is the wrong interception point for games. Either switch the controller
to a HID/DS4/NS mode and target that HID collection, or write a separate filter
for the XInput/XUSB stack.

## Build

Use the GitHub Actions workflow or build locally with Visual Studio + WDK/EWDK.

Expected package files after a successful GitHub Actions build:

- `Cyclone2DpadFilter.sys`
- `Cyclone2DpadFilter.inf`
- `Cyclone2DpadFilter.cat`
- `Cyclone2DpadFilter-TestCert.cer`
- `Cyclone2DpadFilter-TestCert-Thumbprint.txt`
- optional `Cyclone2DpadFilter.pdb`

The workflow creates a temporary self-signed test code-signing certificate, signs the catalog file, verifies the signature, and exports the public certificate into the artifact.

## D-pad report settings

The current HID report values are:

- `ReportId=0`
- `DpadByteOffset=0x0B`
- `DpadMask=0x1C`
- `DpadNeutralValue=0x00`

These were derived from reports where byte 11 changed between `00`, `04`, `0C`, `14`, and `1C` during D-pad presses.

The driver now applies these settings directly:

```text
newByte = (oldByte & ~DpadMask) | (DpadNeutralValue & DpadMask)
```

So only the configured D-pad bits are neutralized; unrelated bits in the same
byte are preserved.

## Install notes

This is a kernel driver. Test only on a machine where you can recover through Safe Mode, Device Manager, or driver removal.

Because the GitHub Actions package is test-signed, enable Windows test-signing from an elevated Command Prompt:

```cmd
bcdedit /set testsigning on
shutdown /r /t 0
```

If Secure Boot blocks that command, temporarily disable Secure Boot in BIOS/UEFI first.

After reboot, import the exported test certificate into the local machine Trusted Root and Trusted Publisher stores:

```cmd
certutil -addstore -f Root Cyclone2DpadFilter-TestCert.cer
certutil -addstore -f TrustedPublisher Cyclone2DpadFilter-TestCert.cer
```

Then install from an elevated Command Prompt in the artifact folder:

```cmd
pnputil /add-driver Cyclone2DpadFilter.inf /install
```

Unplug and replug the controller, or disable and re-enable the targeted
HID-compliant game controller in Device Manager.

After install, verify that the filter is actually attached:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\inspect-cyclone2-paths.ps1
```

The targeted HIDClass child should show `Cyclone2DpadFilter` in `UpperFilters`.
If it does not, the INF did not match the connected dongle/mode device ID.

For kernel debug confirmation, watch for these messages:

```text
CycloneFilter: EvtDeviceAdd attached
CycloneFilter: ReadComplete ...
CycloneFilter: dpad byte[...] ...
```

If `EvtDeviceAdd attached` appears but there are no read completions while a
game reads input, the game is probably using a different device path. If the
D-pad byte log appears but the game still sees D-pad input, the game is
definitely not consuming this modified HID report.

## Uninstall

Find the published INF:

```cmd
pnputil /enum-drivers
```

Then remove it:

```cmd
pnputil /delete-driver oemXX.inf /uninstall /force
```

Remove the test certificate if you no longer need it:

```cmd
certutil -delstore Root "Cyclone2DpadFilter Test Certificate"
certutil -delstore TrustedPublisher "Cyclone2DpadFilter Test Certificate"
```

Turn off test-signing:

```cmd
bcdedit /set testsigning off
shutdown /r /t 0
```

Re-enable Secure Boot afterward if you disabled it.
