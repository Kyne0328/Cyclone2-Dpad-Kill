#include "Cyclone2DpadFilter.h"

static NTSTATUS CycloneReadUlongRegistryValue(
    _In_ WDFDEVICE Device,
    _In_ PCUNICODE_STRING ValueName,
    _In_ ULONG DefaultValue,
    _Out_ PULONG Value
    )
{
    WDFKEY key;
    NTSTATUS status;

    *Value = DefaultValue;

    status = WdfDeviceOpenRegistryKey(
        Device,
        PLUGPLAY_REGKEY_DEVICE,
        KEY_READ,
        WDF_NO_OBJECT_ATTRIBUTES,
        &key
        );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfRegistryQueryULong(key, ValueName, Value);
    WdfRegistryClose(key);

    return status;
}

static VOID CycloneLoadSettings(_In_ WDFDEVICE Device)
{
    PDEVICE_CONTEXT context = DeviceGetContext(Device);
    ULONG value;
    DECLARE_CONST_UNICODE_STRING(reportIdName, L"ReportId");
    DECLARE_CONST_UNICODE_STRING(offsetName, L"DpadByteOffset");
    DECLARE_CONST_UNICODE_STRING(maskName, L"DpadMask");
    DECLARE_CONST_UNICODE_STRING(neutralName, L"DpadNeutralValue");

    context->ReportId = CYCLONE_DEFAULT_REPORT_ID;
    context->DpadByteOffset = CYCLONE_DEFAULT_DPAD_BYTE_OFFSET;
    context->DpadMask = CYCLONE_DEFAULT_DPAD_MASK;
    context->DpadNeutralValue = CYCLONE_DEFAULT_DPAD_NEUTRAL_VALUE;

    if (NT_SUCCESS(CycloneReadUlongRegistryValue(Device, &reportIdName, context->ReportId, &value))) {
        context->ReportId = value;
    }

    if (NT_SUCCESS(CycloneReadUlongRegistryValue(Device, &offsetName, context->DpadByteOffset, &value))) {
        context->DpadByteOffset = value;
    }

    if (NT_SUCCESS(CycloneReadUlongRegistryValue(Device, &maskName, context->DpadMask, &value))) {
        context->DpadMask = (UCHAR)(value & 0xFF);
    }

    if (NT_SUCCESS(CycloneReadUlongRegistryValue(Device, &neutralName, context->DpadNeutralValue, &value))) {
        context->DpadNeutralValue = (UCHAR)(value & 0xFF);
    }
}

static VOID CycloneFilterDpadReport(
    _In_ PDEVICE_CONTEXT Context,
    _Inout_updates_bytes_(Length) PUCHAR Report,
    _In_ size_t Length
    )
{
    UNREFERENCED_PARAMETER(Context);

    //
    // Diagnostic build: ignore registry settings and force the observed D-pad
    // bytes to neutral. This proves whether this filter can modify the actual
    // report buffer that hidapi/GameSir Connect receives.
    //
    if (Length > 11) {
        Report[11] = 0x00;
    }

    if (Length > 12) {
        Report[12] = 0x00;
    }
}

static BOOLEAN CycloneTryGetReportBuffer(
    _In_ WDFREQUEST Request,
    _In_ size_t BytesReturned,
    _Outptr_result_bytebuffer_(*Length) PUCHAR *Buffer,
    _Out_ size_t *Length
    )
{
    PIRP irp;
    NTSTATUS status;

    *Buffer = NULL;
    *Length = 0;

    status = WdfRequestRetrieveOutputBuffer(Request, 1, (PVOID *)Buffer, Length);
    if (NT_SUCCESS(status)) {
        if (BytesReturned < *Length) {
            *Length = BytesReturned;
        }
        return TRUE;
    }

    irp = WdfRequestWdmGetIrp(Request);
    if (irp == NULL) {
        return FALSE;
    }

    if (irp->MdlAddress != NULL) {
        *Buffer = (PUCHAR)MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
    } else if (irp->AssociatedIrp.SystemBuffer != NULL) {
        *Buffer = (PUCHAR)irp->AssociatedIrp.SystemBuffer;
    } else if (irp->UserBuffer != NULL) {
        *Buffer = (PUCHAR)irp->UserBuffer;
    }

    if (*Buffer == NULL || BytesReturned == 0) {
        *Buffer = NULL;
        *Length = 0;
        return FALSE;
    }

    *Length = BytesReturned;
    return TRUE;
}

NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    WDF_DRIVER_CONFIG config;

    WDF_DRIVER_CONFIG_INIT(&config, CycloneEvtDeviceAdd);

    return WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
        );
}

static VOID CycloneForwardWithCompletion(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request)
{
    WDFIOTARGET target = WdfDeviceGetIoTarget(Device);
    WDF_REQUEST_SEND_OPTIONS options;
    NTSTATUS status;

    WdfRequestFormatRequestUsingCurrentType(Request);

    WdfRequestSetCompletionRoutine(Request, CycloneEvtReadComplete, Device);

    WDF_REQUEST_SEND_OPTIONS_INIT(&options, 0);

    if (!WdfRequestSend(Request, target, &options)) {
        status = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, status);
    }
}

static VOID CycloneForwardAndForget(_In_ WDFDEVICE Device, _In_ WDFREQUEST Request)
{
    WDFIOTARGET target = WdfDeviceGetIoTarget(Device);
    WDF_REQUEST_SEND_OPTIONS options;
    NTSTATUS status;

    WdfRequestFormatRequestUsingCurrentType(Request);

    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    if (!WdfRequestSend(Request, target, &options)) {
        status = WdfRequestGetStatus(Request);
        WdfRequestComplete(Request, status);
    }
}

NTSTATUS CycloneEvtDeviceAdd(
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

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    CycloneLoadSettings(device);

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoRead = CycloneEvtIoRead;
    queueConfig.EvtIoDeviceControl = CycloneEvtIoDeviceControl;
    queueConfig.EvtIoInternalDeviceControl = CycloneEvtIoInternalDeviceControl;
    queueConfig.EvtIoDefault = CycloneEvtIoDefault;

    status = WdfIoQueueCreate(
        device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        WDF_NO_HANDLE
        );

    return status;
}

VOID CycloneEvtIoRead(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length
    )
{
    UNREFERENCED_PARAMETER(Length);

    CycloneForwardWithCompletion(WdfIoQueueGetDevice(Queue), Request);
}

VOID CycloneEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    //
    // Apps that poll the D-pad via HidD_GetInputReport reach the filter as
    // IOCTL_HID_GET_INPUT_REPORT. Filter those reports too; forward the rest.
    //
    if (IoControlCode == IOCTL_HID_GET_INPUT_REPORT) {
        CycloneForwardWithCompletion(device, Request);
    } else {
        CycloneForwardAndForget(device, Request);
    }
}

VOID CycloneEvtIoInternalDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (IoControlCode == IOCTL_HID_READ_REPORT ||
        IoControlCode == IOCTL_HID_GET_INPUT_REPORT) {
        CycloneForwardWithCompletion(device, Request);
    } else {
        CycloneForwardAndForget(device, Request);
    }
}

VOID CycloneEvtReadComplete(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context
    )
{
    WDFDEVICE device = (WDFDEVICE)Context;
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(device);
    PUCHAR buffer = NULL;
    size_t length = 0;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Target);

    status = Params->IoStatus.Status;

    if (NT_SUCCESS(status) && Params->IoStatus.Information > 0) {
        if (CycloneTryGetReportBuffer(Request, (size_t)Params->IoStatus.Information, &buffer, &length)) {
            __try {
            CycloneFilterDpadReport(deviceContext, buffer, length);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                ;
            }
        }

        status = Params->IoStatus.Status;
    }

    WdfRequestCompleteWithInformation(Request, status, Params->IoStatus.Information);
}

VOID CycloneEvtIoDefault(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request
    )
{
    CycloneForwardAndForget(WdfIoQueueGetDevice(Queue), Request);
}
