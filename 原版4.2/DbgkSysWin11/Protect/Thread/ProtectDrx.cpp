#include "../../Driver.h"
#include "../../ntos/inc/mmtypes.h"
#include "../../ntos/inc/ntdbg.h"
#include "../../ntos/inc/ketypes.h"
#include "../../ntos/inc/extypes.h"
#include "../../ntos/inc/ntosdef.h"
#include "../../ntos/inc/amd64.h"
#include "../../ntos/inc/mi.h"
#include "../../ntos/inc/pstypes.h"
#include "../../ntos/inc/obtypes.h"
#include "../../ntos/inc/peb_teb.h"
#include "../../Log/log.h"
#include "../../List/MyList.h"
#include "../../ntos/inc/ntlpcapi.h"
#include "../../ntos/inc/psp.h"
#include "../../Globals.h"
#include "../../DbgkApi/DbgkApi.h"
#include "ProtectDrx.h"

//32位进程调用这个函数获取Context
NTSTATUS NtQueryInformationThread(
	_In_       HANDLE ThreadHandle,
	_In_       THREADINFOCLASS ThreadInformationClass,
	_Inout_    PVOID           ThreadInformation,
	_In_       ULONG           ThreadInformationLength,
	_Out_opt_  PULONG          ReturnLength
)
{
	PDEBUG_PROCESS DebugProcess;
	PWOW64_CONTEXT ThreadContext;
	ASSERT(Original_NtQueryInformationThread);
	NTSTATUS Status = Original_NtQueryInformationThread(ThreadHandle, ThreadInformationClass, ThreadInformation, ThreadInformationLength, ReturnLength);
	if (NT_SUCCESS(Status))
	{
		if (ThreadInformationClass == ThreadWow64Context)
		{
			// 判断当前进程是否是游戏的进程
			// 如果是游戏进程则将shadow drx里的内容返回
			if (IsDebugTargetProcess((_EPROCESS*)PsGetCurrentProcess(), &DebugProcess))
			{
				if (DebugProcess->wow64Context.initializated)
				{
					ThreadContext = (PWOW64_CONTEXT)ThreadInformation;
					ThreadContext->Dr0 = DebugProcess->wow64Context.Dr0;
					ThreadContext->Dr1 = DebugProcess->wow64Context.Dr1;
					ThreadContext->Dr2 = DebugProcess->wow64Context.Dr2;
					ThreadContext->Dr3 = DebugProcess->wow64Context.Dr3;
					ThreadContext->Dr6 = DebugProcess->wow64Context.Dr6;
					ThreadContext->Dr7 = DebugProcess->wow64Context.Dr7;
					ThreadContext->EFlags = DebugProcess->wow64Context.EFlags;
				}
			}
		}
	}
	return Status;
}

//64位进程调用此函数获取Context
NTSTATUS NtGetContextThread(_In_ HANDLE hThread, _Inout_ PCONTEXT ThreadContext/*此参数是用户层提供的缓冲区*/)
{
	PDEBUG_PROCESS DebugProcess;

	NTSTATUS Status = Original_NtGetContextThread(hThread, ThreadContext);

	if (NT_SUCCESS(Status))
	{
		// 以下是处理pubg的硬件断点占坑的情况
		// 判断当前进程是否是被调试的进程
		// 如果是被调试的进程则将shadow drx里的内容返回
		if (IsDebugTargetProcess((_EPROCESS*)PsGetCurrentProcess(), &DebugProcess))
		{
			ExAcquireFastMutex(&DebugProcess->Mutex);
			if (DebugProcess->Context.initializated)
			{
				//ThreadContext->EFlags = DebugProcess->Context.EFlags;
				ThreadContext->EFlags = 0x229;
				DbgPrint("DebugProcess->Context.EFlags: %x\n", ThreadContext->EFlags);
			}			
			ExReleaseFastMutex(&DebugProcess->Mutex);			

			//if (eflags.TF)
			//{
			//	eflags.TF = 0;
			//	ThreadContext->EFlags = eflags.flags;
			//	DbgPrint("隐藏TF位.\n");
			//}

			//if (eflags.RF)
			//{
			//	eflags.RF = 0;
			//	ThreadContext->EFlags = eflags.flags;
			//	DbgPrint("隐藏RF位.\n");
			//}
		}
	}
	return Status;
}

NTSTATUS
NtSetContextThread(
	__in HANDLE ThreadHandle,
	__in PCONTEXT ThreadContext  /*此参数是用户层提供的缓冲区*/
)
{
	NTSTATUS Status = STATUS_SUCCESS;
	PDEBUG_PROCESS DebugProcess;
	// 以下是处理pubg的硬件断点占坑的情况
	// 判断当前进程是否是被调试的进程
	// 如果是被调试的进程则设置shadow drx里的内容
	if (IsDebugTargetProcess((_EPROCESS*)PsGetCurrentProcess(), &DebugProcess))
	{
		ExAcquireFastMutex(&DebugProcess->Mutex);
		DebugProcess->Context.EFlags = ThreadContext->EFlags;
		DebugProcess->Context.initializated = TRUE;
		ExReleaseFastMutex(&DebugProcess->Mutex);
		DbgPrint("[set]ThreadContext->EFlags: %x\n", ThreadContext->EFlags);
		//return Status;
	}
	return Original_NtSetContextThread(ThreadHandle, ThreadContext);
}