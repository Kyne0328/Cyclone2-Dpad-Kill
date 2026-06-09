//
// XInput1_4 proxy DLL for neutralizing the GameSir Cyclone 2 D-pad.
//
// Drops in place of a bundled XInput1_4.dll (e.g. GameSir Connect's
// resources\dfu\XInput1_4.dll). Every export forwards to the renamed original
// (XInput1_4_real.dll in the same folder); the state-reading exports strip the
// four D-pad button bits before returning, so any consumer of this DLL sees the
// D-pad as permanently neutral while all other inputs pass through untouched.
//
// Build BOTH Win32 and x64; install the one whose bitness matches the original
// XInput1_4.dll you are replacing (and the process that loads it).
//

#include <windows.h>

//
// XInput types are defined locally rather than via <xinput.h>: that header
// declares every entry point as __declspec(dllimport), which collides with our
// __declspec(dllexport) definitions (C2375, "different linkage"). We only need
// the structs and the D-pad bit constants; the exports themselves come from the
// .def file.
//
typedef struct _XINPUT_GAMEPAD {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} XINPUT_GAMEPAD;

typedef struct _XINPUT_STATE {
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

typedef struct _XINPUT_VIBRATION {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION;

typedef struct _XINPUT_CAPABILITIES {
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    XINPUT_GAMEPAD Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES;

typedef struct _XINPUT_BATTERY_INFORMATION {
    BYTE BatteryType;
    BYTE BatteryLevel;
} XINPUT_BATTERY_INFORMATION;

typedef struct _XINPUT_KEYSTROKE {
    WORD  VirtualKey;
    WCHAR Unicode;
    WORD  Flags;
    BYTE  UserIndex;
    BYTE  HidCode;
} XINPUT_KEYSTROKE, *PXINPUT_KEYSTROKE;

#define XINPUT_GAMEPAD_DPAD_UP    0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN  0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT  0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008

// Provided by the linker; address of this module's image base. Declared before
// first use in GetRealModule().
extern "C" IMAGE_DOS_HEADER __ImageBase;

static HMODULE g_real = nullptr;

//
// Lazily loads the renamed original DLL from this proxy's own directory. Safe
// under concurrent first-call: the loser of the race frees its extra handle.
//
static HMODULE GetRealModule()
{
    if (g_real) {
        return g_real;
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW((HMODULE)&__ImageBase, path, MAX_PATH);

    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) {
        *(slash + 1) = L'\0';
        wcscat_s(path, L"XInput1_4_real.dll");
    }

    HMODULE loaded = LoadLibraryW(path);
    if (loaded) {
        HMODULE prev = (HMODULE)InterlockedCompareExchangePointer(
            (volatile PVOID*)&g_real, loaded, nullptr);
        if (prev != nullptr && prev != loaded) {
            FreeLibrary(loaded);
        }
    }

    return g_real;
}

static FARPROC RealProc(const char* name)
{
    HMODULE m = GetRealModule();
    return m ? GetProcAddress(m, name) : nullptr;
}

//
// The guide-button / extended exports of XInput1_4.dll have no names -- they are
// exported by ordinal only -- so they must be resolved from the original DLL by
// ordinal, not by name.
//
static FARPROC RealProcOrdinal(WORD ordinal)
{
    HMODULE m = GetRealModule();
    return m ? GetProcAddress(m, MAKEINTRESOURCEA(ordinal)) : nullptr;
}

static void StripDpad(XINPUT_STATE* state)
{
    if (state) {
        state->Gamepad.wButtons &= ~(
            XINPUT_GAMEPAD_DPAD_UP |
            XINPUT_GAMEPAD_DPAD_DOWN |
            XINPUT_GAMEPAD_DPAD_LEFT |
            XINPUT_GAMEPAD_DPAD_RIGHT);
    }
}

typedef DWORD (WINAPI *PFN_GetState)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI *PFN_SetState)(DWORD, XINPUT_VIBRATION*);
typedef DWORD (WINAPI *PFN_GetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef void  (WINAPI *PFN_Enable)(BOOL);
typedef DWORD (WINAPI *PFN_GetBattery)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD (WINAPI *PFN_GetKeystroke)(DWORD, DWORD, PXINPUT_KEYSTROKE);
typedef DWORD (WINAPI *PFN_GetAudioDeviceIds)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
typedef DWORD (WINAPI *PFN_WaitGuide)(DWORD, DWORD, LPVOID);
typedef DWORD (WINAPI *PFN_Cancel)(DWORD);
typedef DWORD (WINAPI *PFN_PowerOff)(DWORD);

//
// Named exports.
//

extern "C" __declspec(dllexport)
DWORD WINAPI XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    auto real = (PFN_GetState)RealProc("XInputGetState");
    if (!real) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD result = real(dwUserIndex, pState);
    if (result == ERROR_SUCCESS) {
        StripDpad(pState);
    }
    return result;
}

extern "C" __declspec(dllexport)
DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
{
    auto real = (PFN_SetState)RealProc("XInputSetState");
    return real ? real(dwUserIndex, pVibration) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport)
DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities)
{
    auto real = (PFN_GetCapabilities)RealProc("XInputGetCapabilities");
    return real ? real(dwUserIndex, dwFlags, pCapabilities) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport)
void WINAPI XInputEnable(BOOL enable)
{
    auto real = (PFN_Enable)RealProc("XInputEnable");
    if (real) {
        real(enable);
    }
}

extern "C" __declspec(dllexport)
DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation)
{
    auto real = (PFN_GetBattery)RealProc("XInputGetBatteryInformation");
    return real ? real(dwUserIndex, devType, pBatteryInformation) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport)
DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke)
{
    auto real = (PFN_GetKeystroke)RealProc("XInputGetKeystroke");
    return real ? real(dwUserIndex, dwReserved, pKeystroke) : ERROR_EMPTY;
}

extern "C" __declspec(dllexport)
DWORD WINAPI XInputGetAudioDeviceIds(DWORD dwUserIndex, LPWSTR pRenderDeviceId, UINT* pRenderCount, LPWSTR pCaptureDeviceId, UINT* pCaptureCount)
{
    auto real = (PFN_GetAudioDeviceIds)RealProc("XInputGetAudioDeviceIds");
    return real
        ? real(dwUserIndex, pRenderDeviceId, pRenderCount, pCaptureDeviceId, pCaptureCount)
        : ERROR_DEVICE_NOT_CONNECTED;
}

//
// Ordinal-only exports (no names in the real DLL). XInputGetStateEx (ordinal
// 100) is the common path many apps actually call; it returns an XINPUT_STATE
// just like XInputGetState, so the D-pad must be stripped here too.
//

extern "C" __declspec(dllexport)
DWORD WINAPI XInputGetStateEx(DWORD dwUserIndex, XINPUT_STATE* pState)
{
    auto real = (PFN_GetState)RealProcOrdinal(100);
    if (!real) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD result = real(dwUserIndex, pState);
    if (result == ERROR_SUCCESS) {
        StripDpad(pState);
    }
    return result;
}

extern "C" __declspec(dllexport)
DWORD WINAPI XInputWaitForGuideButton(DWORD dwUserIndex, DWORD dwFlag, LPVOID pVoid)
{
    auto real = (PFN_WaitGuide)RealProcOrdinal(101);
    return real ? real(dwUserIndex, dwFlag, pVoid) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport)
DWORD WINAPI XInputCancelGuideButtonWait(DWORD dwUserIndex)
{
    auto real = (PFN_Cancel)RealProcOrdinal(102);
    return real ? real(dwUserIndex) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" __declspec(dllexport)
DWORD WINAPI XInputPowerOffController(DWORD dwUserIndex)
{
    auto real = (PFN_PowerOff)RealProcOrdinal(103);
    return real ? real(dwUserIndex) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_DETACH && g_real) {
        FreeLibrary(g_real);
        g_real = nullptr;
    }
    return TRUE;
}
