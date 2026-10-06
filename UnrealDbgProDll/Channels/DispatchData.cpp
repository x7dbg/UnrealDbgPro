#include "../dllmain.h"
#include "../Init/InitNTDevice.h"
#include "DispatchData.h"

namespace
{
    // Keep the public V1 constants callable by old code, but make every
    // request originating from the maintained DLL use the access-controlled
    // V2 protocol.  This also makes the protocol transition observable in
    // diagnostic logs without changing any exported DLL API.
    DWORD UpgradeToSecureIoctl(const DWORD code) noexcept
    {
        switch (code)
        {
        case IOCTL_LOAD_SYMBOLS_TABLE: return IOCTL_LOAD_SYMBOLS_TABLE_V2;
        case IOCTL_LOAD_DEBUGGER_STATE: return IOCTL_LOAD_DEBUGGER_STATE_V2;
        case IOCTL_LOAD_PROTECT_OBJ_DATA: return IOCTL_LOAD_PROTECT_OBJ_DATA_V2;
        case IOCTL_LOAD_DEBUGGER_DATA: return IOCTL_LOAD_DEBUGGER_DATA_V2;
        case IOCTL_CREATE_REMOTE_THREAD: return IOCTL_CREATE_REMOTE_THREAD_V2;
        case IOCTL_SET_HARDWARE_BREAKPOINT: return IOCTL_SET_HARDWARE_BREAKPOINT_V2;
        case IOCTL_GET_PROCESS_INFO: return IOCTL_GET_PROCESS_INFO_V2;
        case IOCTL_TL_BLOCK_RESUME_THREAD: return IOCTL_TL_BLOCK_RESUME_THREAD_V2;
        case IOCTL_DEL_HARDWARE_BREAKPOINT: return IOCTL_DEL_HARDWARE_BREAKPOINT_V2;
        case IOCTL_SET_SOFTWARE_BREAKPOINT: return IOCTL_SET_SOFTWARE_BREAKPOINT_V2;
        case IOCTL_DEL_SOFTWARE_BREAKPOINT: return IOCTL_DEL_SOFTWARE_BREAKPOINT_V2;
        case IOCTL_READ_SOFTWARE_BREAKPOINT: return IOCTL_READ_SOFTWARE_BREAKPOINT_V2;
        default: return code;
        }
    }
}

//派遣数据到驱动
BOOL DispatchDataToDriver(DWORD dwIoControlCode,
    PUSER_DATA userData,
    PVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned)
{
    const DWORD requestedCode = dwIoControlCode;
    dwIoControlCode = UpgradeToSecureIoctl(dwIoControlCode);
    if (lpBytesReturned != nullptr)
    {
        *lpBytesReturned = 0;
    }

    if (g_hGeneralDriverDevice == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        logger.LogError("IOCTL 设备句柄无效", ERROR_INVALID_HANDLE);
        logger.Log("[DEBUG] 失败的 IOCTL 代码=0x%08lX", dwIoControlCode);
        return FALSE;
    }

    if (userData == nullptr)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        logger.LogError("IOCTL 用户数据为空", ERROR_INVALID_PARAMETER);
        logger.Log("[DEBUG] 失败的 IOCTL 代码=0x%08lX", dwIoControlCode);
        return FALSE;
    }

    logger.Log("[DEBUG] IOCTL 审计：请求=0x%08lX 实际=0x%08lX 协议=%s 输入=%zu 输出=%lu",
        requestedCode,
        dwIoControlCode,
        requestedCode == dwIoControlCode ? "V1/自定义" : "V2-受控",
        static_cast<size_t>(userData->uSize),
        static_cast<unsigned long>(nOutBufferSize));

    BOOL bRet = DeviceIoControl(g_hGeneralDriverDevice,
        dwIoControlCode,
        userData,
        sizeof(USER_DATA),
        lpOutBuffer,
        nOutBufferSize,
        lpBytesReturned,
        NULL);

    if (!bRet)
    {
        logger.LogError("执行设备 IOCTL：DeviceIoControl", GetLastError());
    }
    else
    {
        logger.Log("[DEBUG] IOCTL 完成：代码=0x%08lX 字节数=%lu",
            dwIoControlCode,
            lpBytesReturned ? static_cast<unsigned long>(*lpBytesReturned) : 0UL);
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
    if (size > static_cast<SIZE_T>(MAXDWORD))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        logger.LogError("IOCTL 负载过大", ERROR_INVALID_PARAMETER);
        logger.Log("[DEBUG] 负载详情：IOCTL=0x%08lX 大小=%zu", dwIoControlCode, static_cast<size_t>(size));
        return FALSE;
    }

    std::string encodeData;
    USER_DATA userData = { 0 };
    userData.uSize = static_cast<ULONG>(size);  //记录明文长度
    if (source)
    {
        encodeData = EncryptData((const char*)source, size, KEY);
        if (encodeData.empty() && size != 0)
        {
            SetLastError(ERROR_INVALID_DATA);
            logger.LogError("IOCTL 加密后负载为空", ERROR_INVALID_DATA);
            logger.Log("[DEBUG] 失败的 IOCTL 代码=0x%08lX", dwIoControlCode);
            return FALSE;
        }
        userData.pUserData = (ULONG64)encodeData.c_str();
    }
    else if (size != 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        logger.LogError("IOCTL 源数据为空但长度非零", ERROR_INVALID_PARAMETER);
        logger.Log("[DEBUG] 负载详情：IOCTL=0x%08lX 大小=%zu", dwIoControlCode, static_cast<size_t>(size));
        return FALSE;
    }

    logger.Log("[DEBUG] 正在准备 IOCTL：代码=0x%08lX 负载=%zu 加密后=%zu",
        dwIoControlCode,
        static_cast<size_t>(size),
        encodeData.size());

    BOOL bRet = DispatchDataToDriver(dwIoControlCode,
        &userData,
        lpOutBuffer,
        nOutBufferSize,
        lpBytesReturned);
    if (!bRet)
    {
        logger.LogError("IOCTL 分发失败", GetLastError());
        logger.Log("[DEBUG] 分发失败的 IOCTL 代码=0x%08lX", dwIoControlCode);
    }
    return bRet;
}

//获取驱动层数据
//ULONG GetDriverData(DWORD dwIoControlCode, PVOID pBuf)
//{
//    USER_DATA userData = { 0 };
//    userData.pUserData = (ULONG64)pBuf;
//    if (DispatchDataToDriver(dwIoControlCode, &userData))
//    {
//        return userData.uSize;
//    }
//    return 0;
//}
