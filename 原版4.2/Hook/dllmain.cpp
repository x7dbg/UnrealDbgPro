#include "dllmain.h"
#include "HookCallSet/functionSet.h"
#include "Globals.h"
#include "Log/Log.h"

#ifdef _WIN64
Logger logger("Hook64.log");
#else
Logger logger("Hook.log");
#endif // _WIN64

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
		InitGlobalVariables();
		InitFunction();
		SetupHook();
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
		UnHook();
		break;
	}

	}
	return bRet;
}

void SetupHook()
{
	if (InitializeDevice())
	{
		Hook_NtDebugActiveProcess();
		Hook_DbgUiIssueRemoteBreakin();
		//Hook_DbgUiDebugActiveProcess();
		Hook_WaitForDebugEvent();
		Hook_ContinueDebugEvent();
		Hook_OutputDebugStringA();
		Hook_OutputDebugStringW();
		Hook_SetThreadContext();
		Hook_GetThreadContext();
		Hook_WriteProcessMemory();
		Hook_ReadProcessMemory();
		Hook_VirtualProtectEx();

		logger.Log("初始化成功，钩子安装完毕");
	}
	else
	{
		logger.Log("初始化设备失败");
	}
}

void UnHook()
{
	UnHook_DebugActiveProcess();
	UnHook_NtDebugActiveProcess();
	UnHook_DbgUiIssueRemoteBreakin();
	UnHook_DbgUiDebugActiveProcess();
	UnHook_NtCreateUserProcess();
	UnHook_WaitForDebugEvent();
	UnHook_ContinueDebugEvent();
	UnHook_OutputDebugStringA();
	UnHook_OutputDebugStringW();
	UnHook_SetThreadContext();
	UnHook_GetThreadContext();
	UnHook_WriteProcessMemory();
	UnHook_ReadProcessMemory();
	UnHook_VirtualProtectEx();
}

//初始化设备
BOOL InitializeDevice()
{
	g_hGeneralDriverDevice = CreateDeviceHandle();
	if (g_hGeneralDriverDevice == INVALID_HANDLE_VALUE)
	{
		logger.Log("连接驱动失败 error: %d", GetLastError());
		return FALSE;
	}
	else
	{
		return TRUE;
	}
}

//连接驱动
HANDLE CreateDeviceHandle()
{
	DWORD error = 0;
	return CreateFile(SYMBOLICLINK, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

PVOID MyGetProcAddress(HMODULE modBase, TCHAR* modName, LPCSTR lpProcName)
{
	HANDLE hFile, hFileMap;  //文件句柄和内存映射文件句柄
	WIN32_FIND_DATAW find = { 0 };
	DWORD fileAttrib;
	PVOID mod_base;
	HMODULE hModule;
	PVOID func_addr = NULL;
	DWORD error = 0;

	//hModule = LoadLibrary(L"DbgHelp.dll");
	//if (hModule)
	//{
	//	ImageRvaToVa = (PFN_GETPROCADDRESS)GetProcAddress(hModule, "ImageRvaToVa");
	//	if (ImageRvaToVa == NULL)
	//	{
	//		FreeLibrary(hModule);
	//		return NULL;
	//	}
	//}
	//else
	//{
	//	return NULL;
	//}

	//返回值为NULL，则文件不存在，退出
	if (FindFirstFile(modName, &find) == NULL)
	{
		return NULL;
	}
	else
	{
		fileAttrib = find.dwFileAttributes;
	}

	hFile = CreateFile(modName, GENERIC_READ, 0, 0, OPEN_EXISTING, fileAttrib, 0);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		//error = GetLastError();
		//outDebug((TCHAR*)_T("[MyGetProcAddress] 打开文件失败！(error:%d)"), error);
		return NULL;
	}
	hFileMap = CreateFileMapping(hFile, 0, PAGE_READONLY, 0, 0, 0);
	if (hFileMap == NULL)
	{
		CloseHandle(hFile);
		return NULL;
	}
	mod_base = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 0);
	if (mod_base == NULL)
	{
		CloseHandle(hFileMap);
		CloseHandle(hFile);
		return NULL;
	}

	IMAGE_DOS_HEADER* pDosHeader = (IMAGE_DOS_HEADER*)mod_base;
	IMAGE_NT_HEADERS32* pNtHeader = (IMAGE_NT_HEADERS32*)((BYTE*)mod_base + pDosHeader->e_lfanew);  //得到NT头首址
	IMAGE_OPTIONAL_HEADER32 pOptHeader = ((IMAGE_NT_HEADERS32*)pNtHeader)->OptionalHeader;  //Optional头首址
	IMAGE_EXPORT_DIRECTORY* pExportDesc = (IMAGE_EXPORT_DIRECTORY*)ImageRvaToVa((PIMAGE_NT_HEADERS)pNtHeader, mod_base, pOptHeader.DataDirectory[0].VirtualAddress/*导出表RVA*/, 0);

	//导出名称表
	PDWORD NameTable = (PDWORD)ImageRvaToVa((PIMAGE_NT_HEADERS)pNtHeader, mod_base, pExportDesc->AddressOfNames, 0); //每个DWORD代表一个函数名RVA
	//导出名称序号表
	PWORD OrdinalTable = (PWORD)ImageRvaToVa((PIMAGE_NT_HEADERS)pNtHeader, mod_base, pExportDesc->AddressOfNameOrdinals, 0); //每个WORD代表一个函数序号
	//导出函数地址表
	PDWORD AddressTable = (PDWORD)ImageRvaToVa((PIMAGE_NT_HEADERS)pNtHeader, mod_base, pExportDesc->AddressOfFunctions, 0); //每个DWORD代表一个函数RVA

	for (int i = 0; i < pExportDesc->NumberOfNames; i++)
	{
		PCHAR func_name = (PCHAR)ImageRvaToVa((PIMAGE_NT_HEADERS)pNtHeader, mod_base, NameTable[i], 0);
		if (strcmp(func_name, lpProcName) == 0)
		{
			DWORD func_rva = AddressTable[OrdinalTable[i]];
			func_addr = (PVOID)((DWORD)modBase + func_rva);
			break;
		}
	}

	CloseHandle(hFileMap);
	CloseHandle(hFile);
	return func_addr;
}

HMODULE GetProcessModuleHandle(_In_ HANDLE hProcess, _In_ TCHAR* modName, _Out_ TCHAR* outMod)
{
	TCHAR szModPath[MAX_PATH] = { 0 };
	DWORD lpcbNeeded = 0;
	HMODULE hMod = NULL;

	//枚举模块路径名
	if (EnumProcessModulesEx(hProcess, NULL, 0, &lpcbNeeded, LIST_MODULES_ALL))  //先获取大小
	{
		HMODULE* lphModule = (HMODULE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lpcbNeeded);
		if (lphModule)
		{
			if (EnumProcessModulesEx(hProcess, lphModule, lpcbNeeded, &lpcbNeeded, LIST_MODULES_ALL))
			{
				for (DWORD i = 0; i < (lpcbNeeded / sizeof(HMODULE)); i++)
				{
					ZeroMemory(szModPath, MAX_PATH);
					if (GetModuleFileNameEx(hProcess, lphModule[i], (LPWSTR)szModPath, MAX_PATH))
					{
						OutputDebugString(szModPath);
						OutputDebugString(_T("\n"));
						if (StrStrI((PCWSTR)szModPath, modName))
						{
							wcscpy(outMod, szModPath);
							hMod = lphModule[i];
							break;
						}
					}
				}
			}
			HeapFree(GetProcessHeap(), 0, lphModule);
		}
	}
	return hMod;
}

int Power()
{
	TOKEN_PRIVILEGES tp;
	HANDLE hToken;
	LUID luid;
	DWORD error = 0;

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken))
	{
		if (LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid))
		{
			tp.PrivilegeCount = 1;
			tp.Privileges[0].Luid = luid;
			if (TRUE)
			{
				tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			}
			else
			{
				tp.Privileges[0].Attributes = 0;
			}

			//提升进程权限
			if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), (PTOKEN_PRIVILEGES)NULL, (PDWORD)NULL))
			{
				error = GetLastError();
				outDebug((TCHAR*)_T("提权失败！(error:%d)"), error);
			}
		}
		else
		{
			error = GetLastError();
			outDebug((TCHAR*)_T("LookupPrivilegeValue失败！(error:%d)"), error);
		}
	}
	else
	{
		error = GetLastError();
		outDebug((TCHAR*)_T("OpenProcessToken失败！(error:%d)"), error);
	}

	return 0;
}

PVOID GetRoutinePointer(TCHAR* modName, LPCSTR lpProcName)
{
	HMODULE modHandle;
	PVOID funcAddress = NULL;
	TCHAR modPath[256] = { 0 };

	if (g_process_info.ProcessHandle)
	{
		modHandle = GetProcessModuleHandle(g_process_info.ProcessHandle, modName, modPath);
		if (modHandle)
		{
			funcAddress = MyGetProcAddress(modHandle, modPath, lpProcName);
		}
	}
	return funcAddress;
}

void InitFunction()
{
	BaseThreadInitThunk = GetProcAddress(GetModuleHandle(_T("kernel32.dll")), "BaseThreadInitThunk");
	//BaseThreadInitThunk = GetRoutinePointer((TCHAR*)_T("kernel32.dll"), (LPCSTR)_T("BaseThreadInitThunk"));
}

void InitGlobalVariables()
{
	g_process_info = { 0 };
	SYSTEM_INFO SysInfo = { 0 };
	GetSystemInfo(&SysInfo);
	g_dwNumberOfProcessors = SysInfo.dwNumberOfProcessors;
}