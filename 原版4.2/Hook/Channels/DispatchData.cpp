
#include "../dllmain.h"
#include "../Globals.h"
#include "DispatchData.h"


//派遣数据到驱动
BOOL DispatchDataToDriver(DWORD dwIoControlCode,
    PUSER_DATA userData,
    PVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned)
{
    BOOL bRet = FALSE;
    if (g_hGeneralDriverDevice != INVALID_HANDLE_VALUE)
    {
        bRet = DeviceIoControl(g_hGeneralDriverDevice,
            dwIoControlCode,
            userData,
            sizeof(USER_DATA),
            lpOutBuffer,
            nOutBufferSize,
            lpBytesReturned,
            NULL);
        DWORD dwError = GetLastError();
    }
    return bRet;
}

BOOL SendUserDataToDriver(DWORD dwIoControlCode,
    PVOID source,
    SIZE_T size,
    PVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned)
{
    BOOL bRet;
    std::string encodeData;
    USER_DATA userData = { 0 };
    userData.uSize = size;  //记录明文长度
    if (source)
    {
        encodeData = EncryptData((const char*)source, size, KEY);
        userData.pUserData = (ULONG64)encodeData.c_str();
    }
    bRet = DispatchDataToDriver(dwIoControlCode,
        &userData,
        lpOutBuffer,
        nOutBufferSize,
        lpBytesReturned);
    return bRet;
}
