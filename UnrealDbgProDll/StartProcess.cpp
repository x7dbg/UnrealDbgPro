#include "dllmain.h"
#include "Globals.h"
#include "StartProcess.h"
#include "Channels/DispatchData.h"
#include "Interface/Interface.h"

#define INJECT_DLL_32 ((TCHAR*)_T("Hook.dll"))
#define INJECT_DLL_64 ((TCHAR*)_T("Hook64.dll"))

//下发调试器信息给驱动
BOOL SendDebuggerDataToDriver(DWORD dwProcessId)
{
	if (dwProcessId == 0)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		logger.LogError("向驱动注册调试器进程（PID 为空）", ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	BOOL bRet;
	DWORD BytesReturned = 0;
	RING3_DEBUGGER_TABLE_ENTRY DebuggerInfo = { 0 };
	DebuggerInfo.dwPid = dwProcessId;
	DebuggerInfo.fileData2 = 0x1998;
	bRet = SendUserDataToDriver(IOCTL_LOAD_DEBUGGER_DATA, &DebuggerInfo, sizeof(RING3_DEBUGGER_TABLE_ENTRY), NULL, 0, &BytesReturned);
	if (!bRet)
	{
		logger.LogError("向驱动注册调试器进程：SendDebuggerDataToDriver", GetLastError());
	}
	else
	{
		logger.Log("[INFO] 调试器进程已注册：pid=%lu 字节数=%lu", dwProcessId, static_cast<unsigned long>(BytesReturned));
	}
	return bRet;
}

//根据光标位置获取进程pid
DWORD GetProcessId_ByCursor()
{
	POINT CursorPos;
	DWORD error = 0;

	//获取当前鼠标的位置
	if (!GetCursorPos(&CursorPos))
	{
		error = GetLastError();
		logger.LogError("获取鼠标位置：GetCursorPos", error);
		return 0;
	}

	//从鼠标位置获取当前窗体的句柄
	HWND hWnd = WindowFromPoint(CursorPos);
	if (hWnd == NULL)
	{
		SetLastError(ERROR_NOT_FOUND);
		logger.LogError("根据鼠标位置获取窗口（WindowFromPoint 返回空）", ERROR_NOT_FOUND);
		return 0;
	}

	//获取窗体句柄的pid
	DWORD dwProcId;
	if (GetWindowThreadProcessId(hWnd, &dwProcId) == 0)
	{
		logger.LogError("获取窗口进程 ID：GetWindowThreadProcessId", GetLastError());
		return 0;
	}
	logger.Log("[DEBUG] 鼠标指向目标：hwnd=%p pid=%lu", hWnd, dwProcId);
	return dwProcId;
}

BOOL Is64BitsProcess(DWORD dwProcessId)
{
	BOOL boWow64Process = FALSE;
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessId);
	if (hProcess)
	{
		if (!IsWow64Process(hProcess, &boWow64Process))
		{
			logger.LogError("检测进程位数：IsWow64Process", GetLastError());
		}
		CloseHandle(hProcess);
	}
	else
	{
		logger.LogError("打开目标进程：OpenProcess(PROCESS_ALL_ACCESS)", GetLastError());
	}
	logger.Log("[DEBUG] 目标进程位数：pid=%lu is64=%d", dwProcessId, !boWow64Process);
	return !boWow64Process;
}


void _StartProcess_(PSTARTUP_INFO pStartInfo)
{
	if (pStartInfo == nullptr)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		logger.LogError("启动线程收到空的启动信息", ERROR_INVALID_PARAMETER);
		return;
	}

	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	DWORD error = 0;
	TCHAR szDllPath[256] = { 0 };
	BOOL is64Process;
	TCHAR* szExe = pStartInfo->szExe;
	TCHAR* sPath = pStartInfo->sPath;
	si.cb = sizeof(si);
	logger.LogW(L"[INFO] 正在启动目标进程：exe=%ls 基础目录=%ls", szExe, sPath);
	if (szExe == nullptr || sPath == nullptr || szExe[0] == 0 || sPath[0] == 0)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		logger.LogError("创建目标进程（可执行文件或 DLL 目录为空）", ERROR_INVALID_PARAMETER);
		return;
	}

	if (!CreateProcess(szExe,
		NULL,
		NULL,
		NULL,
		NULL,
		0,
		NULL,
		NULL,
		&si,
		&pi
	))
	{
		error = GetLastError();
		logger.LogError("创建目标进程：CreateProcess", error);
	}
	else
	{
		logger.Log("[INFO] 目标进程已创建：pid=%lu tid=%lu", pi.dwProcessId, pi.dwThreadId);
		//InsertDebuggerList(pi.dwProcessId);
		//DispatchDebuggerList();
		//Sleep(1000);
		wcscpy(szDllPath, sPath);
		is64Process = Is64BitsProcess(pi.dwProcessId);
		if (is64Process)
		{
			wcscat(szDllPath, INJECT_DLL_64);
		}
		else
		{
			wcscat(szDllPath, INJECT_DLL_32);
		}
		logger.LogW(L"[DEBUG] 选定注入 DLL=%ls", szDllPath);
		Sleep(1000);
		InjectDll_0(pi.dwProcessId, szDllPath, is64Process);
		if (!SendDebuggerDataToDriver(pi.dwProcessId))
		{
			logger.LogError("目标进程已创建，但调试器注册失败", GetLastError());
			logger.Log("[DEBUG] 注册失败的目标 PID=%lu", pi.dwProcessId);
		}
		else
		{
			logger.Log("[INFO] 目标进程初始化完成；pid=%lu", pi.dwProcessId);
		}
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
}


unsigned __stdcall _StartProcess(PVOID pArgList)
{
	_StartProcess_((PSTARTUP_INFO)pArgList);
	delete pArgList;
	return 0;
}

BOOL StartProcess(TCHAR* szExe, TCHAR* sPath)
{
	if (szExe == nullptr || sPath == nullptr)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		logger.LogError("StartProcess 请求被拒绝：参数为空", ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	PSTARTUP_INFO pStartInfo = new STARTUP_INFO;
	if (pStartInfo == nullptr)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		logger.LogError("StartProcess 分配启动参数失败", ERROR_NOT_ENOUGH_MEMORY);
		return FALSE;
	}
	ZeroMemory(pStartInfo, sizeof(STARTUP_INFO));
	wcscpy(pStartInfo->szExe, szExe);
	wcscpy(pStartInfo->sPath, sPath);
	HANDLE hThread = (HANDLE)_beginthreadex(nullptr, 0, _StartProcess, pStartInfo, 0, nullptr);
	if (hThread == nullptr)
	{
		logger.LogError("创建启动线程：_beginthreadex", GetLastError());
		delete pStartInfo;
		return FALSE;
	}
	CloseHandle(hThread);
	return TRUE;
}
