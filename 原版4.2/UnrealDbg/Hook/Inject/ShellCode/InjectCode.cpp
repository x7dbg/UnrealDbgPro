#include "../../dllmain.h"
#include "../../Log/Log.h"
#include "../../Globals.h"
#include "InjectCode.h"

unsigned char ShellCode[] =
{
	//0x48, 0x83, 0xEC, 0x28, 0x90, 0x33, 0xC9, 0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xD0, /*0x50, 0xC3,*/ 0x48, 0x83, 0xC4, 0x28, 0xC3
	0x48, 0x83, 0xEC, 0x28, 0xCC, 0x48, 0x83, 0xC4, 0x28, 0xC3
};


const DWORD ins_len = 20;

BYTE originalInstructions[ins_len] = { 0 };


//摘除钩子
void RemoveHook(DWORD dwPid)
{
	try
	{
		HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid);
		if (hProcess)
		{
			LdrInitializeThunk = (PFN_LDRINITIALIZETHUNK)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "LdrInitializeThunk");
			SIZE_T Size = 0;
			DWORD dwOldProtect;
			BYTE InstructionsBuf[ins_len] = { 0 };

			VirtualProtect((LPVOID)LdrInitializeThunk, ins_len, PAGE_EXECUTE_READWRITE, &dwOldProtect);
			memcpy(InstructionsBuf, LdrInitializeThunk, ins_len);  //将自身内存里的没有被修改的拷一份下来

			//备份目标进程的内容
			if (!ReadProcessMemory(hProcess, LdrInitializeThunk, originalInstructions, sizeof(originalInstructions), &Size))
			{
				throw std::runtime_error("RemoveHook 拷贝失败");
			}
			//摘除钩子
			if (!WriteProcessMemory(hProcess, LdrInitializeThunk, InstructionsBuf, sizeof(InstructionsBuf), &Size))
			{
				throw std::runtime_error("RemoveHook 写入数据失败");
			}
			VirtualProtect((LPVOID)LdrInitializeThunk, ins_len, dwOldProtect, &dwOldProtect);
			// 关闭进程句柄
			CloseHandle(hProcess);
		}
		else
		{
			logger.Log("打开进程失败 error: %d",GetLastError());
		}
	}
	catch (const std::exception& e)
	{
		OutputDebugStringA(e.what());
	}
}

//恢复钩子
void RestoreHook(DWORD dwPid)
{
	try
	{
		HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid);
		if (hProcess)
		{
			LdrInitializeThunk = (PFN_LDRINITIALIZETHUNK)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "LdrInitializeThunk");
			SIZE_T Size = 0;

			//恢复钩子
			if (!WriteProcessMemory(hProcess, LdrInitializeThunk, originalInstructions, sizeof(originalInstructions), &Size))
			{
				throw std::runtime_error("RestoreHook 写入数据失败");
			}
			// 关闭进程句柄
			CloseHandle(hProcess);
		}
		else
		{
			logger.Log("打开进程失败 error: %d", GetLastError());
		}
	}
	catch (const std::exception& e)
	{
		OutputDebugStringA(e.what());
	}
}


//摘除钩子
void RemoveKiUserApcDispatcherHook(DWORD dwPid)
{
	try
	{
		HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid);
		if (hProcess)
		{
			KiUserApcDispatcher = (PFN_LDRINITIALIZETHUNK)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "KiUserApcDispatcher");
			SIZE_T Size = 0;
			DWORD dwOldProtect;
			BYTE InstructionsBuf[ins_len] = { 0 };

			VirtualProtect((LPVOID)KiUserApcDispatcher, ins_len, PAGE_EXECUTE_READWRITE, &dwOldProtect);
			memcpy(InstructionsBuf, KiUserApcDispatcher, ins_len);  //将自身内存里的没有被修改的拷一份下来

			//备份目标进程的内容
			if (!ReadProcessMemory(hProcess, KiUserApcDispatcher, originalInstructions, sizeof(originalInstructions), &Size))
			{
				throw std::runtime_error("RemoveHook 拷贝失败");
			}
			//摘除钩子
			if (!WriteProcessMemory(hProcess, KiUserApcDispatcher, InstructionsBuf, sizeof(InstructionsBuf), &Size))
			{
				throw std::runtime_error("RemoveHook 写入数据失败");
			}
			VirtualProtect((LPVOID)KiUserApcDispatcher, ins_len, dwOldProtect, &dwOldProtect);
			// 关闭进程句柄
			CloseHandle(hProcess);
		}
		else
		{
			logger.Log("打开进程失败 error: %d", GetLastError());
		}
	}
	catch (const std::exception& e)
	{
		OutputDebugStringA(e.what());
	}
}

//恢复钩子
void RestoreKiUserApcDispatcherHook(DWORD dwPid)
{
	try
	{
		HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid);
		if (hProcess)
		{
			KiUserApcDispatcher = (PFN_LDRINITIALIZETHUNK)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "KiUserApcDispatcher");
			SIZE_T Size = 0;

			//恢复钩子
			if (!WriteProcessMemory(hProcess, KiUserApcDispatcher, originalInstructions, sizeof(originalInstructions), &Size))
			{
				throw std::runtime_error("RestoreHook 写入数据失败");
			}
			// 关闭进程句柄
			CloseHandle(hProcess);
		}
		else
		{
			logger.Log("打开进程失败 error: %d", GetLastError());
		}
	}
	catch (const std::exception& e)
	{
		OutputDebugStringA(e.what());
	}
}


BOOL InjectCode(HANDLE hProcess)
{
	HANDLE remoteThread;
	PVOID remoteAddress;
	DWORD error = 0;
	BOOL boRet = FALSE;

	HANDLE hThread;
	CLIENT_ID ClientId;
	NTSTATUS Status;

	//*(ULONG64*)(&ShellCode[9]) = (ULONG64)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "RtlExitUserThread");
	//*(ULONG64*)(&ShellCode[9]) = (ULONG64)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "DbgBreakPoint");

	//hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid);
	if (hProcess)
	{
		//remoteAddress = VirtualAllocEx(hProcess, NULL, sizeof(ShellCode), (MEM_RESERVE | MEM_COMMIT), PAGE_EXECUTE_READWRITE);
		//if (remoteAddress)
		//{
		//	WriteProcessMemory(hProcess, remoteAddress, ShellCode, sizeof(ShellCode), NULL);


			///* Create the thread that will do the breakin */
			//Status = RtlCreateUserThread(hProcess,
			//    NULL,
			//    FALSE,
			//    0,
			//    0,
			//    PAGE_SIZE,
			//    (PUSER_THREAD_START_ROUTINE)DbgBreakPoint,
			//    NULL,
			//    &hThread,
			//    &ClientId);

			///* Close the handle on success */
			//if (NT_SUCCESS(Status))
			//{
			//	NtClose(hThread);
			//}
			//else
			//{
			//	error = GetLastError();
			//	outDebug((TCHAR*)_T("启动远程线程失败！(error:%d)"), error);
			//}

		//	remoteThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)DbgBreakPoint, NULL, 0, NULL);
		//	if (remoteThread)
		//	{
		//		//outDebug((TCHAR*)_T("remoteAddress:  %llX"), remoteAddress);
		//		CloseHandle(remoteThread);
		//	}
		//	else
		//	{
		//		error = GetLastError();
		//		outDebug((TCHAR*)_T("启动远程线程失败！(error:%d)"), error);
		//	}
		//}
		//else
		//{
		//	error = GetLastError();
		//	outDebug((TCHAR*)_T("分配远进程内存失败！(error:%d)"), error);
		//}

		//remoteThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)DbgBreakPoint, NULL, 0, NULL);
		remoteThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)((BYTE*)DbgUserBreakPoint + 0x10), NULL, 0, NULL);
		if (remoteThread)
		{
			logger.Log("启动远程线程DbgBreakPoint成功！");
			CloseHandle(remoteThread);
			boRet = TRUE;
		}
		else
		{
			error = GetLastError();
			outDebug((TCHAR*)_T("启动远程线程DbgBreakPoint失败！(error:%d)"), error);
		}
	}
	else
	{
		error = GetLastError();
		outDebug((TCHAR*)_T("打开进程句柄失败！(error:%d)"), error);
	}
	return boRet;
}