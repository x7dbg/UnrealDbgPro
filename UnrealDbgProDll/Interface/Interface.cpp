#include "../dllmain.h"
#include "../Globals.h"
#include "../Channels/DispatchData.h"
#include "Interface.h"

typedef void(__stdcall* PFN_INJECTDLL)(DWORD dwPid, TCHAR* DLLPathName, BOOL is64Process);

PFN_INJECTDLL AI_InjectDll;

BOOL TL_BlockGameResumeThread_internal(DWORD dwProcessId)
{
	if (dwProcessId == 0)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		logger.LogError("TL 阻止恢复线程请求被拒绝：PID 为空", ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	BOOL bRet;
	DWORD BytesReturned = 0;
	RING3_TL_GAME_TABLE_ENTRY GameInfo = { 0 };
	GameInfo.dwPid = dwProcessId;
	bRet = SendUserDataToDriver(IOCTL_TL_BLOCK_RESUME_THREAD, &GameInfo, sizeof(RING3_TL_GAME_TABLE_ENTRY), NULL, 0, &BytesReturned);
	if (!bRet)
	{
		logger.LogError("发送 TL 阻止恢复线程 IOCTL", GetLastError());
	}
	else
	{
		logger.Log("[INFO] TL 恢复线程阻止已启用：pid=%lu 字节数=%lu", dwProcessId, static_cast<unsigned long>(BytesReturned));
	}
	return bRet;
}

void InitFunctionPtr()
{
	logger.Log("[DEBUG] 正在解析 AIHelper.dll!InjectDll");
	HMODULE ownerModule = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&InitFunctionPtr), &ownerModule))
	{
		logger.LogError("获取 UnrealDbgDll 模块句柄：GetModuleHandleExW", GetLastError());
		AI_InjectDll = nullptr;
		return;
	}
	const std::wstring moduleDirectory = FileSystem::GetModuleDirectory(ownerModule);
	const std::wstring aiHelperPath = moduleDirectory + L"AIHelper.dll";
	logger.LogW(L"[DEBUG] AIHelper.dll 路径=%ls", aiHelperPath.c_str());
	HMODULE hMod = LoadLibraryW(aiHelperPath.c_str());
	if (hMod == nullptr)
	{
		logger.LogError("加载 AIHelper.dll：LoadLibraryW", GetLastError());
		AI_InjectDll = nullptr;
		return;
	}
	AI_InjectDll = (PFN_INJECTDLL)GetProcAddress(hMod, "InjectDll");
	if (AI_InjectDll == nullptr)
	{
		logger.LogError("获取 AIHelper.dll!InjectDll：GetProcAddress", GetLastError());
	}
	else
	{
		logger.Log("[DEBUG] AIHelper.dll!InjectDll 已解析，地址=%p", AI_InjectDll);
	}
}

void InjectDll_0(DWORD dwPid, TCHAR* DLLPathName, BOOL is64Process)
{
	if (dwPid == 0 || DLLPathName == nullptr || DLLPathName[0] == 0)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		logger.LogError("InjectDll_0 请求被拒绝：PID 或 DLL 路径无效", ERROR_INVALID_PARAMETER);
		return;
	}
	if (AI_InjectDll == nullptr)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		logger.LogError("InjectDll_0 已中止：找不到 AIHelper.dll!InjectDll", ERROR_PROC_NOT_FOUND);
		return;
	}
	logger.LogW(L"[INFO] 正在注入 DLL：pid=%lu 路径=%ls is64=%d", dwPid, DLLPathName, is64Process);
	try
	{
		AI_InjectDll(dwPid, DLLPathName, is64Process);
		logger.Log("[DEBUG] InjectDll 调用已返回");
	}
	catch (const std::exception& ex)
	{
		SetLastError(ERROR_UNHANDLED_EXCEPTION);
		logger.LogError("InjectDll_0 抛出 std::exception", ERROR_UNHANDLED_EXCEPTION);
		logger.Log("[DEBUG] 异常文本：%s", ex.what());
	}
	catch (...)
	{
		SetLastError(ERROR_UNHANDLED_EXCEPTION);
		logger.LogError("InjectDll_0 抛出未知异常", ERROR_UNHANDLED_EXCEPTION);
	}
}

BOOL TL_BlockGameResumeThread(DWORD dwPid)
{
	if (dwPid && dwPid != GetCurrentProcessId())
	{
		return TL_BlockGameResumeThread_internal(dwPid);
	}
	logger.Log("[DEBUG] 已忽略 TL 阻止恢复线程请求：pid=%lu", dwPid);
	return FALSE;
}
