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

namespace
{
    enum class DriverLoadStage
    {
        Idle,
        CompatibilityChecked,
        VtCoreReady,
        BridgeReady,
        DeviceOpened,
        Ready,
        FailedRecoverable,
        FailedRestartRequired
    };

    struct DriverLoadTransaction
    {
        DriverLoadStage stage{ DriverLoadStage::Idle };
        bool vtStartedByThisAttempt{};
        bool bridgeStartedByThisAttempt{};
        bool vtMayStillBeActive{};
    };

    struct ExactWindowsVersion
    {
        DWORD major{};
        DWORD minor{};
        DWORD build{};
        DWORD ubr{};
    };

    DriverLoadTransaction g_loadTransaction;

    const wchar_t* LoadStageText(const DriverLoadStage stage) noexcept
    {
        switch (stage)
        {
        case DriverLoadStage::CompatibilityChecked: return L"兼容性已确认";
        case DriverLoadStage::VtCoreReady: return L"VT 核心已就绪";
        case DriverLoadStage::BridgeReady: return L"桥接驱动已就绪";
        case DriverLoadStage::DeviceOpened: return L"设备已打开";
        case DriverLoadStage::Ready: return L"驱动事务完成";
        case DriverLoadStage::FailedRecoverable: return L"事务失败，可安全重试";
        case DriverLoadStage::FailedRestartRequired: return L"事务失败，需要重启恢复";
        default: return L"未开始";
        }
    }

    void SetLoadStage(const DriverLoadStage stage)
    {
        g_loadTransaction.stage = stage;
        logger.LogW(L"[驱动事务] 阶段：%ls", LoadStageText(stage));
    }

    bool QueryExactWindowsVersion(ExactWindowsVersion& result)
    {
        using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto rtlGetVersion = ntdll == nullptr ? nullptr :
            reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtlGetVersion == nullptr)
        {
            SetLastError(ERROR_PROC_NOT_FOUND);
            return false;
        }

        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        if (rtlGetVersion(&version) != 0)
        {
            SetLastError(ERROR_NOT_SUPPORTED);
            return false;
        }

        result.major = version.dwMajorVersion;
        result.minor = version.dwMinorVersion;
        result.build = version.dwBuildNumber;
        DWORD ubr{};
        DWORD ubrSize = sizeof(ubr);
        const LSTATUS registryResult = RegGetValueW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD,
            nullptr, &ubr, &ubrSize);
        // RegGetValue does not require UBR to exist on older systems.  Build
        // selection is still exact; a missing UBR is represented as zero.
        if (registryResult == ERROR_SUCCESS) result.ubr = ubr;
        else result.ubr = 0;
        return true;
    }
    // AIHelper.dll is loaded by UnrealDbgDll, not by the process current
    // directory.  Resolving it relative to this DLL prevents a shortcut or
    // another launcher from selecting a stale copy (or no copy at all).
    std::wstring GetUnrealDbgDllDirectory()
    {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetUnrealDbgDllDirectory), &module))
        {
            return {};
        }
        return FileSystem::GetModuleDirectory(module);
    }

    bool QueryServiceStatus(const wchar_t* serviceName, SERVICE_STATUS_PROCESS& status)
    {
        if (serviceName == nullptr || *serviceName == L'\0')
        {
            return false;
        }
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (manager == nullptr)
        {
            return false;
        }
        SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_QUERY_STATUS);
        if (service == nullptr)
        {
            CloseServiceHandle(manager);
            return false;
        }
        DWORD bytes{};
        const BOOL queried = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes);
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return queried && bytes >= sizeof(status);
    }

    std::wstring QueryServiceImagePath(const wchar_t* serviceName)
    {
        if (serviceName == nullptr || *serviceName == L'\0')
        {
            return {};
        }
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (manager == nullptr)
        {
            return {};
        }
        SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_QUERY_CONFIG);
        if (service == nullptr)
        {
            CloseServiceHandle(manager);
            return {};
        }

        DWORD required{};
        QueryServiceConfigW(service, nullptr, 0, &required);
        if (required == 0)
        {
            CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return {};
        }
        std::vector<BYTE> buffer(required);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        const BOOL queried = QueryServiceConfigW(service, config, required, &required);
        std::wstring imagePath = queried && config->lpBinaryPathName != nullptr
            ? config->lpBinaryPathName : L"";
        CloseServiceHandle(service);
        CloseServiceHandle(manager);
        return imagePath;
    }

    std::wstring NormalizeServiceImagePath(std::wstring path)
    {
        while (!path.empty() && (path.front() == L'"' || path.front() == L' '))
        {
            path.erase(path.begin());
        }
        while (!path.empty() && (path.back() == L'"' || path.back() == L' '))
        {
            path.pop_back();
        }
        constexpr wchar_t kNtPathPrefix[] = L"\\??\\";
        if (path.rfind(kNtPathPrefix, 0) == 0)
        {
            // Remove the complete NT prefix before comparing paths.
            path.erase(0, wcslen(kNtPathPrefix));
        }
        return path;
    }

    bool ValidateServiceIdentity(const std::wstring& requestedPath, const std::wstring& serviceName,
        const wchar_t* role)
    {
        SERVICE_STATUS_PROCESS status{};
        const bool statusAvailable = QueryServiceStatus(serviceName.c_str(), status);
        const std::wstring registered = NormalizeServiceImagePath(QueryServiceImagePath(serviceName.c_str()));
        if (registered.empty())
        {
            return true; // service does not exist yet; LoadNT will create it.
        }

        const std::wstring requested = NormalizeServiceImagePath(requestedPath);
        if (_wcsicmp(registered.c_str(), requested.c_str()) == 0)
        {
            return true;
        }

        if (!statusAvailable)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            logger.LogW(L"[驱动事务] 无法读取 %ls 服务 %ls 的运行状态；已拒绝复用不同路径的服务。已登记=%ls，请求=%ls",
                role, serviceName.c_str(), registered.c_str(), requested.c_str());
            logger.LogError("检查驱动服务身份", ERROR_ACCESS_DENIED);
            return false;
        }
        if (status.dwCurrentState == SERVICE_RUNNING)
        {
            SetLastError(ERROR_REVISION_MISMATCH);
            logger.LogW(L"[驱动事务] 拒绝复用正在运行的 %ls 服务 %ls：已登记路径=%ls；当前发布包路径=%ls。为避免把不同版本的驱动与 DLL 混用，未停止该服务。",
                role, serviceName.c_str(), registered.c_str(), requested.c_str());
            logger.LogError("驱动服务路径与当前发布包不一致", ERROR_REVISION_MISMATCH);
            return false;
        }

        logger.LogW(L"[驱动事务] 发现已停止的旧 %ls 服务 %ls：已登记路径=%ls；当前路径=%ls。将在 LoadNT 返回“服务已存在”后，仅清理该停止服务并重新注册。",
            role, serviceName.c_str(), registered.c_str(), requested.c_str());
        return true;
    }

    bool WasServiceRunning(const std::wstring& serviceName)
    {
        SERVICE_STATUS_PROCESS status{};
        return QueryServiceStatus(serviceName.c_str(), status) && status.dwCurrentState == SERVICE_RUNNING;
    }

    DWORD QueryStoppedServiceError(const wchar_t* serviceName)
    {
        SERVICE_STATUS_PROCESS status{};
        if (!QueryServiceStatus(serviceName, status) || status.dwCurrentState != SERVICE_STOPPED)
        {
            return ERROR_SUCCESS;
        }

        // A service can report ERROR_SERVICE_SPECIFIC_ERROR and put the real
        // driver status in dwServiceSpecificExitCode. Preserve that value too.
        if (status.dwWin32ExitCode == ERROR_SERVICE_SPECIFIC_ERROR &&
            status.dwServiceSpecificExitCode != ERROR_SUCCESS)
        {
            return status.dwServiceSpecificExitCode;
        }
        return status.dwWin32ExitCode == ERROR_SUCCESS ? ERROR_SUCCESS : status.dwWin32ExitCode;
    }

    void PreserveDriverFailureCode(const char* operation, const wchar_t* serviceName)
    {
        const DWORD original = GetLastError();
        const DWORD serviceError = QueryStoppedServiceError(serviceName);
        const DWORD diagnostic = serviceError == ERROR_SUCCESS ?
            (original == ERROR_SUCCESS ? ERROR_GEN_FAILURE : original) : serviceError;
        SetLastError(diagnostic);
        logger.LogError(operation, diagnostic);
        if (serviceError != ERROR_SUCCESS && serviceError != original)
        {
            logger.LogW(L"[ERROR] 服务 %ls 的退出码已还原：%lu (0x%08lX)", serviceName,
                static_cast<unsigned long>(serviceError), static_cast<unsigned long>(serviceError));
        }
    }

    // AIHelper 的 LoadNT 对“服务已存在”只返回 183。若该服务其实已经
    // 停止（常见于上一次运行使用了另一配置目录），继续复用它会让
    // 当前请求的驱动路径永远无法生效。此时安全地删除停止服务并按
    // 本次传入的路径重新注册；运行中的服务不在这里强制停止。
    bool RetryLoadAfterRemovingStoppedService(const std::wstring& driverPath,
        const std::wstring& serviceName, const DWORD originalError)
    {
        switch (originalError)
        {
        case ERROR_SERVICE_EXISTS:
        case ERROR_SERVICE_ALREADY_RUNNING:
        case ERROR_INVALID_IMAGE_HASH:
        case ERROR_DRIVER_BLOCKED:
        case ERROR_GEN_FAILURE:
            break;
        default:
            return false;
        }

        SERVICE_STATUS_PROCESS status{};
        if (!QueryServiceStatus(serviceName.c_str(), status) || status.dwCurrentState != SERVICE_STOPPED)
        {
            return false;
        }
        const std::wstring registeredPath = NormalizeServiceImagePath(QueryServiceImagePath(serviceName.c_str()));
        const std::wstring requestedPath = NormalizeServiceImagePath(driverPath);
        if (registeredPath.empty() || _wcsicmp(registeredPath.c_str(), requestedPath.c_str()) == 0)
        {
            // 同一路径仍然启动失败（例如签名无效）时，删除/重注册不会
            // 修复根因，保留原始错误码交给上层诊断。
            return false;
        }
        if (pfnUnloadNT == nullptr || pfnLoadNT == nullptr)
        {
            SetLastError(ERROR_PROC_NOT_FOUND);
            logger.LogError("清理停止的旧驱动服务：UnloadNT/LoadNT 导出函数不可用", ERROR_PROC_NOT_FOUND);
            return false;
        }

        logger.LogW(L"检测到已停止的旧服务 %ls（错误码=%lu），正在清理后按当前路径重新注册：%ls",
            serviceName.c_str(), static_cast<unsigned long>(originalError), driverPath.c_str());
        SetLastError(ERROR_SUCCESS);
        if (!pfnUnloadNT(serviceName))
        {
            const DWORD unloadLastError = GetLastError();
            const DWORD unloadError = unloadLastError == ERROR_SUCCESS ? ERROR_GEN_FAILURE : unloadLastError;
            SetLastError(unloadError);
            logger.LogError("清理停止的旧驱动服务：UnloadNT", unloadError);
            return false;
        }

        logger.LogW(L"旧服务 %ls 已清理，正在重新调用 LoadNT", serviceName.c_str());
        SetLastError(ERROR_SUCCESS);
        if (pfnLoadNT(driverPath, serviceName))
        {
            logger.LogW(L"服务 %ls 已按当前驱动路径重新注册并启动", serviceName.c_str());
            return true;
        }

        const DWORD retryLastError = GetLastError();
        const DWORD retryError = retryLastError == ERROR_SUCCESS ? ERROR_GEN_FAILURE : retryLastError;
        SetLastError(retryError);
        logger.LogError("清理旧服务后重新加载驱动：LoadNT", retryError);
        return false;
    }
}


//连接驱动
HANDLE CreateDeviceHandle(const std::wstring DriveImagePath, const std::wstring ServiceName)
{
    logger.outDebug(L"连接驱动设备：服务=%ls 驱动=%ls", ServiceName.c_str(), DriveImagePath.c_str());

    if (!ValidateServiceIdentity(DriveImagePath, ServiceName, L"桥接"))
    {
        return INVALID_HANDLE_VALUE;
    }

    // V2 IOCTL requires an explicitly read/write handle.  The bridge device
    // DACL further limits opening it to SYSTEM and Administrators.
    HANDLE hDevice = CreateFile(SYMBOLICLINK, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        logger.outDebug(L"设备已存在，直接连接成功");
        return hDevice;
    }

    DWORD initialError = GetLastError();
    logger.LogError("连接既有驱动设备：CreateFile", initialError);
    if (pfnLoadNT == nullptr)
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        logger.LogError("LoadNT 导出函数不可用，无法加载服务", ERROR_PROC_NOT_FOUND);
        logger.LogW(L"[DEBUG] 服务名称=%ls", ServiceName.c_str());
        return INVALID_HANDLE_VALUE;
    }

    const bool serviceWasRunning = WasServiceRunning(ServiceName);
    logger.outDebug(L"设备未连接，开始加载服务 %ls", ServiceName.c_str());
    if (!pfnLoadNT(DriveImagePath, ServiceName))
    {
        // AIHelper returns ERROR_SERVICE_EXISTS when a previous run already
        // registered the service. This is only benign when that service is
        // actually running; otherwise resolve the stopped service's true
        // driver exit code (for example 577 for an untrusted signature).
        const DWORD original = GetLastError();
        SERVICE_STATUS_PROCESS status{};
        if (RetryLoadAfterRemovingStoppedService(DriveImagePath, ServiceName, original))
        {
            // 已按当前路径重新注册并启动，继续执行后面的设备连接。
            g_loadTransaction.bridgeStartedByThisAttempt = true;
        }
        else if ((original == ERROR_SERVICE_EXISTS || original == ERROR_SERVICE_ALREADY_RUNNING) &&
            QueryServiceStatus(ServiceName.c_str(), status) &&
            status.dwCurrentState == SERVICE_RUNNING)
        {
            logger.LogW(L"LoadNT 报告服务已存在（错误码=%lu），但服务 %ls 当前正在运行；继续连接设备",
                static_cast<unsigned long>(original), ServiceName.c_str());
            SetLastError(ERROR_SUCCESS);
        }
        else
        {
            SetLastError(original);
            PreserveDriverFailureCode("LoadNT 加载驱动服务", ServiceName.c_str());
            logger.LogW(L"[DEBUG] 服务名称=%ls", ServiceName.c_str());
        }
    }
    else
    {
        g_loadTransaction.bridgeStartedByThisAttempt = !serviceWasRunning;
    }

    hDevice = CreateFile(SYMBOLICLINK, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        logger.outDebug(L"连接驱动成功！service=%ls", ServiceName.c_str());
        return hDevice;
    }

    PreserveDriverFailureCode("加载驱动后连接设备：CreateFile", ServiceName.c_str());
    return INVALID_HANDLE_VALUE;
}

//加载VT驱动
BOOL LoadVT(const std::wstring DriveImagePath, const std::wstring ServiceName)
{
    logger.outDebug(L"加载 VT 驱动：服务=%ls 路径=%ls", ServiceName.c_str(), DriveImagePath.c_str());
    if (!ValidateServiceIdentity(DriveImagePath, ServiceName, L"VT 核心"))
    {
        return FALSE;
    }
    if (pfnLoadNT == nullptr)
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        logger.LogError("LoadVT 中止：LoadNT 导出函数不可用", ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    const bool serviceWasRunning = WasServiceRunning(ServiceName);
    if (!pfnLoadNT(DriveImagePath, ServiceName))
    {
        // Reusing a service that is already running is safe and avoids the
        // misleading intermediate ERROR_SERVICE_EXISTS (183). A stopped
        // service is not accepted; report its actual exit code instead.
        const DWORD original = GetLastError();
        SERVICE_STATUS_PROCESS status{};
        if (RetryLoadAfterRemovingStoppedService(DriveImagePath, ServiceName, original))
        {
            // 已清理停止的旧服务，并成功按当前路径启动。
            g_loadTransaction.vtStartedByThisAttempt = true;
            g_loadTransaction.vtMayStillBeActive = true;
        }
        else if ((original == ERROR_SERVICE_EXISTS || original == ERROR_SERVICE_ALREADY_RUNNING) &&
            QueryServiceStatus(ServiceName.c_str(), status) &&
            status.dwCurrentState == SERVICE_RUNNING)
        {
            logger.LogW(L"LoadNT 报告 VT 服务已存在（错误码=%lu），但服务当前正在运行；复用现有服务",
                static_cast<unsigned long>(original));
            SetLastError(ERROR_SUCCESS);
        }
        else
        {
            SetLastError(original);
            PreserveDriverFailureCode("LoadNT 加载 VT 服务", ServiceName.c_str());
            return FALSE;
        }
    }
    else
    {
        g_loadTransaction.vtStartedByThisAttempt = !serviceWasRunning;
        g_loadTransaction.vtMayStillBeActive = true;
    }
    g_loadTransaction.vtMayStillBeActive = true;
    logger.outDebug(L"VT 驱动加载请求已返回成功");
    return TRUE;
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

void CompensateAfterBridgeFailure(const DWORD originalError)
{
    // The bridge driver is a conventional device driver.  If this transaction
    // created it and no device handshake succeeded, removing that service is a
    // bounded compensation step.  VT_Driver is deliberately *not* unloaded:
    // after VMX state may have been entered, automatic unload has not yet been
    // proven safe on every supported Windows build.
    if (g_hGeneralDriverDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hGeneralDriverDevice);
        g_hGeneralDriverDevice = INVALID_HANDLE_VALUE;
    }
    if (g_loadTransaction.bridgeStartedByThisAttempt && pfnUnloadNT != nullptr)
    {
        logger.LogW(L"[驱动事务] 桥接阶段失败，开始补偿：卸载本次启动的 UnrealDevice 服务。");
        if (pfnUnloadNT(L"UnrealDevice"))
        {
            logger.LogW(L"[驱动事务] 桥接驱动补偿完成；该部分可安全重试。");
        }
        else
        {
            logger.LogError("驱动事务补偿：卸载 UnrealDevice", GetLastError());
        }
    }

    if (g_loadTransaction.vtMayStillBeActive)
    {
        SetLoadStage(DriverLoadStage::FailedRestartRequired);
        logger.LogW(L"[驱动事务] VT 核心已被加载或复用，但桥接设备/握手未完成。为避免在 VMX 状态下执行未经验证的卸载，程序不会自动卸载 VT_Driver。解决方案：不要继续调试；关闭程序并重启 Windows 后再修复签名、兼容性或服务路径问题。原始错误码=%lu。",
            static_cast<unsigned long>(originalError));
    }
    else
    {
        SetLoadStage(DriverLoadStage::FailedRecoverable);
    }
    SetLastError(originalError);
}

//初始化驱动设备
BOOL InitializeDevice(const std::wstring DriveImagePath)
{
    logger.outDebug(L"初始化驱动设备开始：基础目录=%ls", DriveImagePath.c_str());
    g_loadTransaction = {};

    ExactWindowsVersion version{};
    if (!QueryExactWindowsVersion(version))
    {
        logger.LogError("获取精确 Windows 版本（RtlGetVersion/UBR）", GetLastError());
        SetLoadStage(DriverLoadStage::FailedRecoverable);
        return FALSE;
    }
    const auto* support = unrealdbg::windows_support::Find(version.build, version.ubr);
    if (support == nullptr || support->level == unrealdbg::windows_support::SupportLevel::Blocked)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        logger.LogW(L"[驱动事务] 已阻止未知 Windows 版本：%lu.%lu Build %lu，UBR %lu。原因：不存在精确支持表条目，不能再以“Build >= 22000”猜测驱动。解决方案：先在隔离快照虚拟机完成该 Build/UBR 的驱动加载与符号握手验证，再新增支持表条目。",
            static_cast<unsigned long>(version.major), static_cast<unsigned long>(version.minor),
            static_cast<unsigned long>(version.build), static_cast<unsigned long>(version.ubr));
        logger.LogError("Windows 版本兼容性检查", ERROR_NOT_SUPPORTED);
        SetLoadStage(DriverLoadStage::FailedRecoverable);
        return FALSE;
    }

    logger.LogW(L"[驱动事务] 精确 Windows 版本：%lu.%lu Build %lu，UBR %lu；匹配=%ls；状态=%ls；桥接驱动=%ls；说明=%ls",
        static_cast<unsigned long>(version.major), static_cast<unsigned long>(version.minor),
        static_cast<unsigned long>(version.build), static_cast<unsigned long>(version.ubr),
        support->marketingName, unrealdbg::windows_support::LevelText(support->level),
        support->bridgeDriver, support->note);
    SetLoadStage(DriverLoadStage::CompatibilityChecked);

    if (!LoadVT(DriveImagePath + L"VT_Driver.sys", L"VT_Driver"))
    {
        logger.LogError("加载 VT_Driver.sys", GetLastError());
        SetLoadStage(DriverLoadStage::FailedRecoverable);
        return FALSE;
    }
    SetLoadStage(DriverLoadStage::VtCoreReady);

    if (!LoadGeneralDriver(DriveImagePath + support->bridgeDriver, L"UnrealDevice"))
    {
        const DWORD failure = GetLastError() == ERROR_SUCCESS ? ERROR_GEN_FAILURE : GetLastError();
        logger.LogError("加载桥接驱动或打开 UnrealDbg 设备", failure);
        CompensateAfterBridgeFailure(failure);
        return FALSE;
    }

    SetLoadStage(DriverLoadStage::BridgeReady);
    SetLoadStage(DriverLoadStage::DeviceOpened);
    SetLoadStage(DriverLoadStage::Ready);
    logger.outDebug(L"初始化驱动设备结束：结果=1");
    return TRUE;
}

//初始化接口
BOOL InitInterface()
{
    logger.outDebug(L"初始化接口：正在加载 AIHelper.dll");
    const std::wstring aiHelperPath = GetUnrealDbgDllDirectory() + L"AIHelper.dll";
    logger.LogW(L"[DEBUG] AIHelper.dll 路径=%ls", aiHelperPath.c_str());
    HMODULE AIHelperMod = LoadLibraryW(aiHelperPath.c_str());
    if (!AIHelperMod)
    {
        logger.LogError("加载 AIHelper.dll：LoadLibraryW", GetLastError());
        return FALSE;
    }
    pfnLoadNT = (PFN_LOADNT)GetProcAddress(AIHelperMod, "LoadNT");
    pfnUnloadNT = (PFN_UNLOADNT)GetProcAddress(AIHelperMod, "UnloadNT");
    outDebug = (PFN_OUTDEBUG)GetProcAddress(AIHelperMod, "outDebug");
    if (pfnLoadNT == nullptr || pfnUnloadNT == nullptr || outDebug == nullptr)
    {
        logger.LogError("获取 AIHelper.dll 导出函数：GetProcAddress", GetLastError());
        logger.Log("[DEBUG] AIHelper.dll 导出状态：LoadNT=%p UnloadNT=%p outDebug=%p",
            pfnLoadNT, pfnUnloadNT, outDebug);
        return FALSE;
    }
    logger.outDebug(L"AIHelper.dll 接口加载完成");
    return TRUE;
}

BOOL _Initialize(const TCHAR* sPath)
{
    if (sPath == nullptr)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        logger.LogError("_Initialize 收到空路径", ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    BOOL boInit = FALSE;
    std::wstring sDrivePath(sPath);
    if (sDrivePath.empty())
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        logger.LogError("_Initialize 收到空驱动目录", ERROR_PATH_NOT_FOUND);
        return FALSE;
    }

    // AIHelper's LoadNT/outDebug ABI resolves PrintLog from the host EXE.
    // Validate that contract before invoking it so a legacy host receives a
    // precise error instead of AIHelper's old ANSI MessageBox.
    HMODULE hostModule = GetModuleHandleW(nullptr);
    FARPROC printLog = hostModule == nullptr ? nullptr : GetProcAddress(hostModule, "PrintLog");
    if (printLog == nullptr)
    {
        const DWORD lastError = GetLastError();
        const DWORD error = lastError == ERROR_SUCCESS ? ERROR_PROC_NOT_FOUND : lastError;
        SetLastError(error);
        logger.LogError("初始化中止：宿主未导出 PrintLog（AIHelper 日志回调）", error);
        return FALSE;
    }
    logger.Log("[DEBUG] 已验证宿主 PrintLog 导出：地址=%p", printLog);

    if (InitInterface())
    {
        boInit = InitializeDevice(sDrivePath);
    }
    logger.outDebug(L"初始化函数返回结果=%d", boInit);
    return boInit;
}

//结束时的扫尾工作
BOOL UnInitialize()
{
    logger.outDebug(L"卸载初始化开始（事务安全模式）");
    BOOL bridgeUnloaded = TRUE;
    if (g_hGeneralDriverDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hGeneralDriverDevice);
        g_hGeneralDriverDevice = INVALID_HANDLE_VALUE;
    }

    if (pfnUnloadNT == nullptr)
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        logger.LogError("卸载初始化中止：UnloadNT 导出函数不可用", ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    // 仅卸载本次事务创建的桥接服务；若它原本由另一个实例运行，绝不
    // 因为当前前端退出而停止对方的调试会话。
    if (!g_loadTransaction.bridgeStartedByThisAttempt)
    {
        logger.LogW(L"[驱动事务] UnrealDevice 由其他事务创建，本次不停止该服务");
    }
    else if (pfnUnloadNT(L"UnrealDevice"))
    {
        outDebug(L"停止桥接驱动服务成功。");
    }
    else
    {
        logger.LogError("卸载 UnrealDevice 服务：UnloadNT", GetLastError());
        outDebug(L"停止桥接驱动服务失败。");
        bridgeUnloaded = FALSE;
    }

    if (g_loadTransaction.vtMayStillBeActive)
    {
        // VT_Driver may have entered VMX root mode.  Its safe runtime unload
        // contract is not verified on every matrix row, so never pretend a
        // normal application exit has completely reverted this state.
        SetLastError(ERROR_SUCCESS_REBOOT_REQUIRED);
        SetLoadStage(DriverLoadStage::FailedRestartRequired);
        logger.LogW(L"[驱动事务] 未自动卸载 VT_Driver：该核心可能仍处于 VMX 状态。解决方案：关闭程序并重启 Windows，随后再进行下一次 VT 调试。桥接驱动卸载结果=%d。",
            bridgeUnloaded ? 1 : 0);
        return FALSE;
    }
    logger.outDebug(L"卸载初始化结束：桥接结果=%d", bridgeUnloaded);
    return bridgeUnloaded;
}
