# Cyclone2UsbFilter

USB **lower** filter on the Cyclone 2 composite parent `USB\VID_3537&PID_100B`
(service `usbccgp`). One filter, one chokepoint: every child interface funnels
its URBs down this device stack, so this driver sees and neutralizes the D-pad
in every input stream at once.

```
        usbccgp (USB\VID_3537&PID_100B)
        /        |            \
   MI_00       MI_01         IG_01
   xusb22    HidUsb(vendor)  HidUsb(gamepad)
  XInput   GameSir Connect   DirectInput
        \________|___________/
                 |
        Cyclone2UsbFilter  (lower)
                 |
              USB hub
```

## Coverage

| Consumer | Interface | Wire format | Status |
|---|---|---|---|
| XInput games | MI_00 / xusb22 | 20-byte Xbox360 packet, `byte[2]` nibble | **masked** |
| GameSir Connect | MI_01 HID vendor, EP 0x84 | 64-byte report ID `0x12`, `byte[5]` + `byte[58]` hat | **masked** |
| DirectInput apps | IG_01 HID | report `byte[0x0B]` mask `0x1C` | **masked** (by HID upper filter) |
| Bluetooth | separate stack | n/a | **not covered** |

- **2.4g dongle = USB → covered.** Bluetooth is a separate stack and is **not**
  covered by this filter.
- The filter never calls into the device itself; it only inspects URBs passing
  through, so it coexists with `xusb22`, `HidUsb`, and the existing HID filter.

## Ceiling

A host filter can only touch bytes the device sends over USB. If the intent is
that the D-pad never functions on the controller at all -- including inside
GameSir Connect's own remap logic -- the only complete fix is a **firmware
remap** done in GameSir Connect (disable / unbind the D-pad). The driver covers
every host-side consumer; firmware covers the device itself.

## Install / safety

Kernel driver. Test only where you can recover via Safe Mode / Device Manager.
Same test-signing + certificate steps as `README.md`. A lower filter on the USB
parent affects the whole composite device, so verify recovery before relying on
it. Install:

```cmd
pnputil /add-driver Cyclone2UsbFilter.inf /install
```

Replug the controller, then confirm with the inspection script that
`LowerFilters` on `USB\VID_3537&PID_100B` contains `Cyclone2UsbFilter`.
