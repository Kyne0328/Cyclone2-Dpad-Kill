# Cyclone2DpadFilter

Windows KMDF HID upper-filter driver scaffold for GameSir Cyclone 2 D-pad filtering.

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

The current USB HID report values are:

- `ReportId=0`
- `DpadByteOffset=0x0B`
- `DpadMask=0x1C`
- `DpadNeutralValue=0x00`

These were derived from reports where byte 11 changed between `00`, `04`, `0C`, `14`, and `1C` during D-pad presses.

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

Unplug and replug the controller, or disable and re-enable the HID-compliant game controller in Device Manager.

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
