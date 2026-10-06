#include <windows.h>

typedef struct {
    unsigned int  bPresent;
    unsigned short wYear;
    unsigned char  bMonth;
    unsigned char  bDay;
} VMProtectSerialNumberData;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

extern "C"
{
    __declspec(dllexport) void VMProtectBegin(const char*) {}
    __declspec(dllexport) void VMProtectBeginVirtualization(const char*) {}
    __declspec(dllexport) void VMProtectBeginMutation(const char*) {}
    __declspec(dllexport) void VMProtectBeginUltra(const char*) {}
    __declspec(dllexport) void VMProtectBeginVirtualizationLockByKey(const char*) {}
    __declspec(dllexport) void VMProtectBeginUltraLockByKey(const char*) {}
    __declspec(dllexport) void VMProtectEnd(void) {}

    __declspec(dllexport) bool VMProtectIsProtected() { return false; }
    __declspec(dllexport) bool VMProtectIsDebuggerPresent(bool) { return false; }
    __declspec(dllexport) bool VMProtectIsVirtualMachinePresent(void) { return false; }
    __declspec(dllexport) bool VMProtectIsValidImageCRC(void) { return true; }
    __declspec(dllexport) const char* VMProtectDecryptStringA(const char* value) { return value; }
    __declspec(dllexport) const wchar_t* VMProtectDecryptStringW(const wchar_t* value) { return value; }
    __declspec(dllexport) bool VMProtectFreeString(const void*) { return true; }

    __declspec(dllexport) int VMProtectSetSerialNumber(const char*) { return 0; }
    __declspec(dllexport) int VMProtectGetSerialNumberState() { return 0; }
    __declspec(dllexport) bool VMProtectGetSerialNumberData(VMProtectSerialNumberData*, int) { return false; }
    __declspec(dllexport) int VMProtectGetCurrentHWID(char*, int) { return 0; }

    __declspec(dllexport) int VMProtectActivateLicense(const char*, char*, int) { return -1; }
    __declspec(dllexport) int VMProtectDeactivateLicense(const char*) { return -1; }
    __declspec(dllexport) int VMProtectGetOfflineActivationString(const char*, char*, int) { return -1; }
    __declspec(dllexport) int VMProtectGetOfflineDeactivationString(const char*, char*, int) { return -1; }
}