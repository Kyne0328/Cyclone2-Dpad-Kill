#include "Cyclone2UsbFilter.h"

//
// Returns a kernel-virtual pointer to a URB transfer buffer regardless of
// whether the URB carries it as a flat buffer or an MDL.
//
static PUCHAR CycloneUsbGetTransferBuffer(
    _In_ PVOID TransferBuffer,
    _In_opt_ PMDL TransferBufferMdl
    )
{
    if (TransferBuffer != NULL) {
        return (PUCHAR)TransferBuffer;
    }

    if (TransferBufferMdl != NULL) {
        return (PUCHAR)MmGetSystemAddressForMdlSafe(TransferBufferMdl, NormalPagePriority);
    }

    return NULL;
}

//
// Neutralizes the D-pad in a single completed IN transfer. The same logical
// D-pad shows up in several wire formats depending on which interface produced
// the report, so each known format is matched and masked in place. Unknown
// formats are logged for later reverse engineering.
//
static VOID CycloneUsbFilterTransfer(
    _Inout_updates_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ USBD_PIPE_HANDLE PipeHandle
    )
{
    if (Buffer == NULL || Length == 0) {
        return;
    }

    //
    // XInput / Xbox 360 wired packet (MI_00, xusb22). Identified by the fixed
    // report-type/length header so we do not mask unrelated equal-length data.
    //
    if (Length >= 4 &&
        Buffer[0] == CYCLONE_XUSB_REPORT_TYPE &&
        Buffer[1] == CYCLONE_XUSB_REPORT_LEN) {

        UCHAR before = Buffer[CYCLONE_XUSB_DPAD_OFFSET];
        UCHAR after = (UCHAR)(before & ~CYCLONE_XUSB_DPAD_MASK);

        if (before != after) {
            Buffer[CYCLONE_XUSB_DPAD_OFFSET] = after;
            DbgPrint("CycloneUsb: xusb dpad %02X->%02X len=%lu pipe=%p\n",
                     before, after, Length, PipeHandle);
        }
        return;
    }

    //
    // GameSir Connect vendor report (MI_01, endpoint 0x84, HID report ID 0x12).
    // 64-byte report confirmed by USBPcap capture. D-pad hat at byte[5] and its
    // firmware mirror at byte[58]; both forced to 0x0F (neutral).
    //
    if (Length == CYCLONE_VENDOR_REPORT_LEN &&
        Buffer[0] == CYCLONE_VENDOR_REPORT_ID) {

        UCHAR before5  = Buffer[CYCLONE_VENDOR_DPAD_OFFSET];
        UCHAR before58 = Buffer[CYCLONE_VENDOR_DPAD_MIRROR_OFFSET];

        Buffer[CYCLONE_VENDOR_DPAD_OFFSET]        = CYCLONE_VENDOR_DPAD_NEUTRAL;
        Buffer[CYCLONE_VENDOR_DPAD_MIRROR_OFFSET] = CYCLONE_VENDOR_DPAD_NEUTRAL;

        if (before5 != CYCLONE_VENDOR_DPAD_NEUTRAL ||
            before58 != CYCLONE_VENDOR_DPAD_NEUTRAL) {
            DbgPrint("CycloneUsb: vendor dpad byte[5]=%02X->0F byte[58]=%02X->0F len=%lu pipe=%p\n",
                     before5, before58, Length, PipeHandle);
        }
        return;
    }

    //
    // NOTE: the IG_01 HID game-controller report (byte 0x0B / mask 0x1C) is
    // intentionally NOT masked here. Keyboard, mouse, consumer, and other HID
    // reports share this parent stack; blindly writing byte 0x0B would corrupt
    // them. IG_01 is already handled by the Cyclone2DpadFilter HID upper filter.
    //

#if CYCLONE_USB_VERBOSE
    //
    // Discovery aid: dump the first bytes of every IN transfer so the GameSir
    // Connect / MI_01 vendor report layout can be located, then added above.
    //
    {
        ULONG dumpLen = Length < 20 ? Length : 20;
        ULONG i;
        char line[3 * 20 + 1];
        for (i = 0; i < dumpLen; i++) {
            RtlStringCchPrintfA(&line[i * 3], 4, "%02X ", Buffer[i]);
        }
        line[dumpLen * 3 == 0 ? 0 : dumpLen * 3 - 1] = '\0';
        DbgPrint("CycloneUsb: IN pipe=%p len=%lu [%s]\n", PipeHandle, Length, line);
    }
#endif
}

//
// Completion routine for IN transfers we chose to inspect. The URB pointer is
// passed as the completion context; the device stack has already filled the
// transfer buffer by the time we run.
//
VOID CycloneUsbEvtUrbComplete(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context
    )
{
    PURB urb = (PURB)Context;

    UNREFERENCED_PARAMETER(Target);

    if (NT_SUCCESS(Params->IoStatus.Status) && urb != NULL) {
        struct _URB_BULK_OR_INTERRUPT_TRANSFER *transfer =
            &urb->UrbBulkOrInterruptTransfer;

        PUCHAR buffer = CycloneUsbGetTransferBuffer(
            transfer->TransferBuffer,
            transfer->TransferBufferMDL);

        if (buffer != NULL) {
            __try {
                CycloneUsbFilterTransfer(
                    buffer,
                    transfer->TransferBufferLength,
                    transfer->PipeHandle);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                ;
            }
        }
    }

    WdfRequestComplete(Request, Params->IoStatus.Status);
}

//
// Forwards a request to the next-lower driver. When InspectUrb is non-NULL the
// request is sent with a completion routine so we can read the filled IN buffer
// on the way back up; otherwise it is sent and forgotten.
//
static VOID CycloneUsbForward(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_opt_ PURB InspectUrb
    )
{
    WDFIOTARGET target = WdfDeviceGetIoTarget(Device);
    WDF_REQUEST_SEND_OPTIONS options;
    NTSTATUS status;

    WdfRequestFormatRequestUsingCurrentType(Request);

    if (InspectUrb != NULL) {
        WdfRequestSetCompletionRoutine(Request, CycloneUsbEvtUrbComplete, InspectUrb);
        WDF_REQUEST_SEND_OPTIONS_INIT(&options, 0);
    } else {
        WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    }

    if (!WdfRequestSend(Request, target, &options)) {
        status = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, status);
    }
}

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, CycloneUsbEvtDeviceAdd);

    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
        );
}

NTSTATUS CycloneUsbEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFDEVICE device;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    WdfFdoInitSetFilter(DeviceInit);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, USB_DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    DbgPrint("CycloneUsb: EvtDeviceAdd attached to composite parent\n");

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoInternalDeviceControl = CycloneUsbEvtIoInternalDeviceControl;
    queueConfig.EvtIoDefault = CycloneUsbEvtIoDefault;

    return WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE
        );
}

VOID CycloneUsbEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PURB inspectUrb = NULL;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    //
    // All USB I/O reaches a lower filter as IOCTL_INTERNAL_USB_SUBMIT_URB. The
    // URB rides in the WDM IRP stack location, not the WDF buffers, so reach
    // through to the IRP to read it.
    //
    if (IoControlCode == IOCTL_INTERNAL_USB_SUBMIT_URB) {
        PIRP irp = WdfRequestWdmGetIrp(Request);
        PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(irp);
        PURB urb = (PURB)stack->Parameters.Others.Argument1;

        if (urb != NULL &&
            urb->UrbHeader.Function == URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER &&
            (urb->UrbBulkOrInterruptTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)) {
            inspectUrb = urb;
        }
    }

    CycloneUsbForward(device, Request, inspectUrb);
}

VOID CycloneUsbEvtIoDefault(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request
    )
{
    CycloneUsbForward(WdfIoQueueGetDevice(Queue), Request, NULL);
}
