#include "../dllmain.h"
#include "InitNTDevice.h"


//加载NT驱动
typedef BOOL(__stdcall* PFN_LOADNT)(const std::wstring DriveImagePath, const std::wstring ServiceName);
//卸载NT驱动
typedef BOOL(__stdcall* PFN_UNLOADNT)(const std::wstring ServiceName);

typedef int(__stdcall* PFN_OUTDEBUG)(const TCHAR* _Format, ...);

PFN_LOADNT pfnLoadNT;
PFN_UNLOADNT pfnUnloadNT;
PFN_OUTDEBUG outDebug;

HANDLE g_hGeneralDriverDevice = INVALID_HANDLE_VALUE;


//连接驱动
HANDLE CreateDeviceHandle(const std::wstring DriveImagePath, const std::wstring ServiceName)
{
    DWORD error = 0;
    //创建符号连接
    HANDLE hDevice = CreateFile(SYMBOLICLINK, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        return hDevice;
    }
    else
    {
        pfnLoadNT(DriveImagePath, ServiceName);

        logger.outDebug(L"正在连接符号...");
        hDevice = CreateFile(SYMBOLICLINK, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDevice != INVALID_HANDLE_VALUE)
        {
            logger.outDebug(L"连接驱动成功！");
            return hDevice;
        }
        else
        {
            error = GetLastError();
            logger.outDebug(L"连接驱动失败，可能是驱动服务尚未安装! (error:%d)", error);
        }
    }

    return INVALID_HANDLE_VALUE;
}

//加载VT驱动
BOOL LoadVT(const std::wstring DriveImagePath, const std::wstring ServiceName)
{
    if (!pfnLoadNT(DriveImagePath, ServiceName))
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

//常规驱动加载
BOOL LoadGeneralDriver(const std::wstring DriveImagePath, const std::wstring ServiceName)
{
    g_hGeneralDriverDevice = CreateDeviceHandle(DriveImagePath, ServiceName);
    if (g_hGeneralDriverDevice == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

//初始化驱动设备
BOOL InitializeDevice(const std::wstring DriveImagePath)
{
    BOOL bRet = TRUE;

    if (!LoadVT(DriveImagePath + L"VT_Driver.sys", L"VT_Driver"))
    {
        return FALSE;
    }

    DWORD dwMajorVer, dwMinorVer, dwBuildNumber, error;
    if (Common::GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuildNumber))
    {
        if (dwBuildNumber >= 22000)
        {
            //Win11
            if (!LoadGeneralDriver(DriveImagePath + L"DbgkSysWin11.sys", L"UnrealDevice"))
            {
                bRet = FALSE;
            }
        }
        else
        {
            //Win10
            if (!LoadGeneralDriver(DriveImagePath + L"DbgkSysWin10.sys", L"UnrealDevice"))
            {
                bRet = FALSE;
            }
        }
    }
    else
    {
        error = GetLastError();
        logger.outDebug(L"无法获取系统版本信息! (error:%d)", error);
        bRet = FALSE;
    }
    return bRet;
}

//初始化接口
BOOL InitInterface()
{
    HMODULE AIHelperMod = LoadLibrary(L"AIHelper.dll");
    if (!AIHelperMod)
    {
        ::MessageBox(NULL, _T("没有找到 AIHelper.dll"), _T("错误信息:"), MB_ICONWARNING);
        return FALSE;
    }
    pfnLoadNT = (PFN_LOADNT)GetProcAddress(AIHelperMod, "LoadNT");
    pfnUnloadNT = (PFN_UNLOADNT)GetProcAddress(AIHelperMod, "UnloadNT");
    outDebug = (PFN_OUTDEBUG)GetProcAddress(AIHelperMod, "outDebug");
    return TRUE;
}

BOOL _Initialize(const TCHAR* sPath)
{
    BOOL boInit = FALSE;
    std::wstring sDrivePath(sPath);
    if (!sDrivePath.empty())
    {
        if (InitInterface())
        {
            boInit = InitializeDevice(sDrivePath);
        }
    }
    return boInit;
}

//结束时的扫尾工作
BOOL UnInitialize()
{
    BOOL boUnInit = FALSE;
    if (g_hGeneralDriverDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hGeneralDriverDevice);
        g_hGeneralDriverDevice = INVALID_HANDLE_VALUE;
    }

    if (pfnUnloadNT(L"BACDevice"))
    {
        outDebug(L"停止虚幻引擎服务成功..");
        boUnInit = TRUE;
    }
    else
    {
        outDebug(L"停止虚幻引擎服务失败!");
        boUnInit = FALSE;
    }

    if (pfnUnloadNT(L"VT_Driver"))
    {
        outDebug(L"停止VT服务成功..");
        boUnInit = TRUE;
    }
    else
    {
        outDebug(L"停止VT服务失败!");
        boUnInit = FALSE;
    }
    return boUnInit;
}