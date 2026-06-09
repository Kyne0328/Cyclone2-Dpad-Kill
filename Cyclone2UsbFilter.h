#pragma once

#include <ntddk.h>
#include <ntstrsafe.h>
#include <wdf.h>
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>
#include <wdfusb.h>

//
// Cyclone2UsbFilter
//
// USB *lower* filter for the GameSir Cyclone 2 composite parent
// (USB\VID_3537&PID_100B, service usbccgp). Because every child interface
// (MI_00 xusb22 / XInput, MI_01 HidUsb / vendor, IG_01 HidUsb / DirectInput)
// funnels its URBs down this single device stack to the hub, a lower filter
// here sees every input report from every consumer -- games via XInput, the
// GameSir Connect config app via its vendor channel, and DirectInput apps.
//
// Each input stream encodes the D-pad differently, so the filter classifies a
// completed IN transfer and neutralizes the D-pad in place. Unknown streams are
// logged (under CYCLONE_USB_VERBOSE) so their format can be reverse engineered
// and added as a masker.
//

//
// XInput / Xbox 360 wired input packet (MI_00, xusb22):
//   byte[0] = 0x00 (report type)
//   byte[1] = 0x14 (report length = 20)
//   byte[2] = buttons low  -> D-pad: UP 0x01 DOWN 0x02 LEFT 0x04 RIGHT 0x08
//   byte[3] = buttons high
//
#define CYCLONE_XUSB_REPORT_TYPE   0x00
#define CYCLONE_XUSB_REPORT_LEN    0x14
#define CYCLONE_XUSB_DPAD_OFFSET   0x02
#define CYCLONE_XUSB_DPAD_MASK     0x0F

//
// HID game-controller report (IG_01) shares the layout the in-box HID filter
// already uses: byte 0x0B, mask 0x1C, neutral 0x00. Applied when an IN transfer
// matches that report's length and report id.
//
#define CYCLONE_HID_DPAD_OFFSET    0x0B
#define CYCLONE_HID_DPAD_MASK      0x1C
#define CYCLONE_HID_DPAD_NEUTRAL   0x00

//
// GameSir Connect vendor report (MI_01, endpoint 0x84, HID class):
//   Report ID 0x12, 64 bytes.
//   byte[5]  = D-pad hat switch: 0x0F = neutral, 0x00=Up 0x02=Right 0x04=Down 0x06=Left
//   byte[58] = mirror of byte[5] (second copy of same hat field in firmware report)
//
// Confirmed by USBPcap capture:
//   neutral:  ... 0F ...  byte[58]=0F
//   D-pad Up: ... 00 ...  byte[58]=00
//
#define CYCLONE_VENDOR_REPORT_ID          0x12
#define CYCLONE_VENDOR_REPORT_LEN         64
#define CYCLONE_VENDOR_DPAD_OFFSET        5
#define CYCLONE_VENDOR_DPAD_MIRROR_OFFSET 58
#define CYCLONE_VENDOR_DPAD_NEUTRAL       0x0F

//
// Set to 1 to log every IN transfer (pipe handle + bytes) for discovery of
// unknown report streams. Noisy; disable for release builds.
//
#ifndef CYCLONE_USB_VERBOSE
#define CYCLONE_USB_VERBOSE 1
#endif

typedef struct _USB_DEVICE_CONTEXT {
    ULONG Reserved;
} USB_DEVICE_CONTEXT, *PUSB_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(USB_DEVICE_CONTEXT, UsbDeviceGetContext)

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD CycloneUsbEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL CycloneUsbEvtIoInternalDeviceControl;
EVT_WDF_IO_QUEUE_IO_DEFAULT CycloneUsbEvtIoDefault;
EVT_WDF_REQUEST_COMPLETION_ROUTINE CycloneUsbEvtUrbComplete;
