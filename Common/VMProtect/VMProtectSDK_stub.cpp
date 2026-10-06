#ifdef UNREALDBG_VMP_STUB
#include "VMProtectSDK.h"

extern "C"
{
    void VMProtectBegin(const char*) {}
    void VMProtectBeginVirtualization(const char*) {}
    void VMProtectBeginMutation(const char*) {}
    void VMProtectBeginUltra(const char*) {}
    void VMProtectBeginVirtualizationLockByKey(const char*) {}
    void VMProtectBeginUltraLockByKey(const char*) {}
    void VMProtectEnd(void) {}

    bool VMProtectIsProtected() { return false; }
    bool VMProtectIsDebuggerPresent(bool) { return false; }
    bool VMProtectIsVirtualMachinePresent(void) { return false; }
    bool VMProtectIsValidImageCRC(void) { return true; }
    const char* VMProtectDecryptStringA(const char* value) { return value; }
    const VMP_WCHAR* VMProtectDecryptStringW(const VMP_WCHAR* value) { return value; }
    bool VMProtectFreeString(const void*) { return true; }

    int VMProtectSetSerialNumber(const char*) { return SERIAL_STATE_SUCCESS; }
    int VMProtectGetSerialNumberState() { return SERIAL_STATE_SUCCESS; }
    bool VMProtectGetSerialNumberData(VMProtectSerialNumberData*, int) { return false; }
    int VMProtectGetCurrentHWID(char*, int) { return 0; }

    int VMProtectActivateLicense(const char*, char*, int) { return ACTIVATION_NO_CONNECTION; }
    int VMProtectDeactivateLicense(const char*) { return ACTIVATION_NO_CONNECTION; }
    int VMProtectGetOfflineActivationString(const char*, char*, int) { return ACTIVATION_NO_CONNECTION; }
    int VMProtectGetOfflineDeactivationString(const char*, char*, int) { return ACTIVATION_NO_CONNECTION; }
}
#endif // UNREALDBG_VMP_STUB
