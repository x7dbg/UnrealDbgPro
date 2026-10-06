#include "dllmain.h"
#include "Init/InitNTDevice.h"
#include "Channels/DispatchData.h"
#include "Globals.h"
#include "Interface/Interface.h"
#include "StartProcess.h"

Logger logger("UnrealDbgDll.log");


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
        // Perform any necessary cleanup.      
        break;
    }

    }
    return bRet;
}


void InitSymbol()
{
	g_SymbolTable = { 0 };
	HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
	g_SymbolTable.lpDbgUiRemoteBreakin = (size_t)GetProcAddress(hNtdll, "DbgUiRemoteBreakin");
	g_SymbolTable.lpDbgBreakPoint = (size_t)GetProcAddress(hNtdll, "DbgBreakPoint");
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
    BOOL boSuccess = TRUE;
	std::wstring processPath = FileSystem::GetModuleDirectory(NULL);
	if (_Initialize(processPath.c_str()))  //加载驱动
	{
		//驱动初始化成功的处理逻辑
        //Common::ReportSeriousError("阻塞");
        if (InitSymbolsTable(key))
        {
            SendDebuggerDataToDriver(GetCurrentProcessId());
        }
        else
        {
            boSuccess = FALSE;
        }
	}
    return boSuccess;
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
    BOOL bRet;
    DWORD BytesReturned = 0;
    RING3_VERIFY info = { 0 };
    info.key = key;
    DWORD dwSuccess = 520;
    bRet = SendUserDataToDriver(IOCTL_LOAD_SYMBOLS_TABLE, &info, sizeof(RING3_VERIFY), &dwSuccess, sizeof(DWORD), &BytesReturned);
    if (dwSuccess != 1998)
    {
        return FALSE;
    }
    VMProtectEnd();
    return bRet;
}