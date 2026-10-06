#include "dllmain.h"
#include "Init/InitNTDevice.h"
#include "Channels/DispatchData.h"
#include "Globals.h"
#include "Interface/Interface.h"
#include "StartProcess.h"


namespace
{
    // 日志文件必须跟随 DLL 所在目录，而不是跟随进程当前工作目录。
    // 从命令行、快捷方式或其他启动器加载时，当前目录可能不同，
    // 否则前端读取的 x64\\*\\Log\\UnrealDbgDll.log 会变成另一份旧文件。
    std::string GetModuleLogFilename()
    {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetModuleLogFilename), &module))
        {
            return "Log\\UnrealDbgDll.log";
        }

        std::wstring directory = FileSystem::GetModuleDirectory(module);
        // UnrealDbgDll.dll is deployed in <release>\\bin, while the front end
        // tails <release>\\Log\\UnrealDbgDll.log. Keep one shared log file by
        // moving the log base back to the release directory when applicable.
        size_t end = directory.size();
        while (end > 0 && (directory[end - 1] == L'\\' || directory[end - 1] == L'/'))
        {
            --end;
        }
        const size_t separator = directory.find_last_of(L"\\/", end == 0 ? 0 : end - 1);
        const std::wstring leaf = separator == std::wstring::npos
            ? directory.substr(0, end) : directory.substr(separator + 1, end - separator - 1);
        if (_wcsicmp(leaf.c_str(), L"bin") == 0 && separator != std::wstring::npos)
        {
            directory.resize(separator + 1);
        }
        if (directory.empty())
        {
            return "Log\\UnrealDbgDll.log";
        }

        return Common::wideStringToString2(directory + L"Log\\UnrealDbgDll.log");
    }
}

Logger logger(GetModuleLogFilename());
namespace
{
    bool g_driverInitializationAttempted = false;
}


BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  // handle to DLL module
    DWORD fdwReason,     // reason for calling function
    LPVOID lpReserved)   // reserved
{
    BOOL bRet = TRUE;


    // Perform actions based on the reason for calling.
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hinstDLL);
        logger.Log("[INFO] UnrealDbgDll 进程附加；模块=%p", hinstDLL);
        InitFunctionPtr();
        break;
    }

    case DLL_THREAD_ATTACH:
        // Do thread-specific initialization.
        break;

    case DLL_THREAD_DETACH:
    {
        // Do thread-specific cleanup.
        break;
    }

    case DLL_PROCESS_DETACH:
    {
        logger.Log("[INFO] UnrealDbgDll 进程分离；保留参数=%p", lpReserved);
        break;
    }

    }
    return bRet;
}


void InitSymbol()
{
	g_SymbolTable = { 0 };
	HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
	if (hNtdll == nullptr)
	{
		logger.LogError("获取 ntdll.dll 模块句柄：GetModuleHandle", GetLastError());
		return;
	}
	g_SymbolTable.lpDbgUiRemoteBreakin = (size_t)GetProcAddress(hNtdll, "DbgUiRemoteBreakin");
	g_SymbolTable.lpDbgBreakPoint = (size_t)GetProcAddress(hNtdll, "DbgBreakPoint");
	logger.Log("[DEBUG] 符号已解析：DbgUiRemoteBreakin=%p DbgBreakPoint=%p",
		(PVOID)g_SymbolTable.lpDbgUiRemoteBreakin,
		(PVOID)g_SymbolTable.lpDbgBreakPoint);
	if (g_SymbolTable.lpDbgUiRemoteBreakin == 0 || g_SymbolTable.lpDbgBreakPoint == 0)
	{
		logger.LogError("获取 ntdll 调试符号：GetProcAddress", GetLastError());
	}
}

void DispatchSymbol()
{
	InitSymbol();

	// 将缓冲区地址传递给驱动程序
	DWORD BytesReturned = 0;
	//SendUserDataToDriver(IOCTL_DISPATCH_SYMBOL, &g_SymbolTable, sizeof(SYMBOL_TABLE), NULL, 0, &BytesReturned);
}

BOOL Initialize(ULONG64 key)
{
    // key 是驱动握手的敏感输入。保留“已收到”的诊断事实即可，不能把
    // 可复用的原始值写入本地日志、调试输出或用户提交的诊断包。
    logger.Log("[INFO] VT 初始化开始；初始化密钥已接收（按脱敏策略不写入日志）");
    BOOL boSuccess = TRUE;
	// 驱动和 AIHelper 与 UnrealDbgDll.dll 一起部署在 bin。不能使用
	// GetModuleDirectory(NULL)，因为 NULL 返回宿主 EXE（上级目录），
	// 会让重组后的发布包再次从错误的根目录寻找 .sys 文件。
	HMODULE ownerModule = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&Initialize), &ownerModule))
	{
		SetLastError(ERROR_MOD_NOT_FOUND);
		logger.LogError("初始化中止：无法定位 UnrealDbgDll 模块目录", ERROR_MOD_NOT_FOUND);
		return FALSE;
	}
	std::wstring processPath = FileSystem::GetModuleDirectory(ownerModule);
	if (processPath.empty())
	{
		SetLastError(ERROR_PATH_NOT_FOUND);
		logger.LogError("初始化中止：模块目录为空", ERROR_PATH_NOT_FOUND);
		return FALSE;
	}
	logger.LogW(L"[DEBUG] 驱动目录=%ls", processPath.c_str());
	const bool interfaceReady = InitInterface() != FALSE;
	if (interfaceReady)
	{
		// Mark the transaction before entering _Initialize.  A bridge failure
		// can return FALSE after VT_Driver has already entered VMX; Shutdown
		// must still run the compensation/restart-required path in that case.
		g_driverInitializationAttempted = true;
	}
	if (interfaceReady && _Initialize(processPath.c_str()))  //加载驱动
	{
		logger.Log("[INFO] 内核设备初始化成功");
		logger.Log("[INFO] 正在加载符号表并执行驱动握手");
		//驱动初始化成功的处理逻辑
        //Common::ReportSeriousError("阻塞");
        if (InitSymbolsTable(key))
        {
            logger.Log("[INFO] 驱动已接受符号表");
            if (!SendDebuggerDataToDriver(GetCurrentProcessId()))
            {
                logger.LogError("向驱动注册当前调试器进程：SendDebuggerDataToDriver", GetLastError());
                boSuccess = FALSE;
            }
        }
        else
        {
			SetLastError(ERROR_INVALID_DATA);
            logger.LogError("驱动拒绝符号表：请检查 PDB 文件和 Windows 版本兼容性", ERROR_INVALID_DATA);
            boSuccess = FALSE;
        }
	}
	else
    {
        const DWORD error = GetLastError();
        logger.LogError("初始化驱动设备：InitializeDevice", error);
        logger.Log("[INFO] 驱动设备初始化失败，符号表加载阶段未执行");
        boSuccess = FALSE;
    }
    logger.Log("[INFO] VT 初始化结束；结果=%d", boSuccess);
    return boSuccess;
}

BOOL Shutdown()
{
    if (!g_driverInitializationAttempted)
    {
        logger.Log("[INFO] Shutdown：本次 DLL 未启动驱动，无需执行卸载");
        return TRUE;
    }
    logger.Log("[INFO] Shutdown：开始执行驱动事务安全收尾");
    const BOOL result = UnInitialize();
    const DWORD error = result ? ERROR_SUCCESS : (GetLastError() == ERROR_SUCCESS ? ERROR_SUCCESS_REBOOT_REQUIRED : GetLastError());
    if (!result)
    {
        SetLastError(error);
        logger.LogError("Shutdown：驱动收尾未完全完成", error);
    }
    else
    {
        logger.Log("[INFO] Shutdown：驱动收尾完成");
        g_driverInitializationAttempted = false;
    }
    return result;
}

void GetFileVersion(_In_ TCHAR* FileName, _Out_ TCHAR* VerInfo)
{
    VS_FIXEDFILEINFO* pVsInfo;
    UINT iFileInfoSize = sizeof(VS_FIXEDFILEINFO);

    DWORD iVerInfoSize = GetFileVersionInfoSize(FileName, NULL);
    if (iVerInfoSize)
    {
        TCHAR* pBuf = new TCHAR[iVerInfoSize];
        if (pBuf)
        {
            if (GetFileVersionInfo(FileName, 0, iVerInfoSize, pBuf))
            {
                if (VerQueryValue(pBuf, _T("\\"), (LPVOID*)&pVsInfo, &iFileInfoSize))
                {
                    wsprintf(pBuf, _T("%d.%d.%d.%d"), HIWORD(pVsInfo->dwFileVersionMS), LOWORD(pVsInfo->dwFileVersionMS), HIWORD(pVsInfo->dwFileVersionLS), LOWORD(pVsInfo->dwFileVersionLS));
                    wcscpy(VerInfo, pBuf);
                }
            }
            delete[] pBuf;
        }
    }
}

BOOL InitSymbolsTable(ULONG64 key)
{
    return LoadSymbolsTable(key);
}

//加载符号表
BOOL LoadSymbolsTable(ULONG64 key)
{
    VMProtectBeginVirtualization("VMP");
    logger.Log("[INFO] 符号表加载开始：正在发送 IOCTL_LOAD_SYMBOLS_TABLE");
    BOOL bRet = FALSE;
    DWORD BytesReturned = 0;
    RING3_VERIFY info = { 0 };
    info.key = key;
    DWORD dwSuccess = 520;
    bRet = SendUserDataToDriver(IOCTL_LOAD_SYMBOLS_TABLE, &info, sizeof(RING3_VERIFY), &dwSuccess, sizeof(DWORD), &BytesReturned);
    const DWORD ioError = bRet ? ERROR_SUCCESS : GetLastError();
    logger.Log("[DEBUG] 符号表 IOCTL 返回：DeviceIoControl=%d response=%lu expected=1998 bytes=%lu",
        bRet,
        static_cast<unsigned long>(dwSuccess),
        static_cast<unsigned long>(BytesReturned));
    if (!bRet)
    {
        VMProtectEnd();
        logger.LogError("加载符号表：DeviceIoControl", ioError);
        return FALSE;
    }
    if (dwSuccess != 1998)
    {
        SetLastError(ERROR_INVALID_DATA);
        logger.LogError("符号表握手响应无效（期望响应值 1998）", ERROR_INVALID_DATA);
        VMProtectEnd();
        return FALSE;
    }
    VMProtectEnd();
    logger.Log("[INFO] 符号表加载完成：驱动握手响应有效，返回字节数=%lu", static_cast<unsigned long>(BytesReturned));
    return bRet;
}
