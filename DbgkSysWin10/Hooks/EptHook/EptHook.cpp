#include "../../Driver.h"
#include "../../ntos/inc/extypes.h"
#include "../../ntos/inc/ketypes.h"
#include "../../ntos/inc/ntosdef.h"
#include "../../ntos/inc/amd64.h"
#include "../../ntos/inc/mi.h"
#include "../../ntos/inc/pstypes.h"
#include "../../ntos/inc/obtypes.h"
#include "../../ntos/inc/mmtypes.h"
#include "../../ntos/inc/ntdbg.h"
#include "../../ntos/inc/peb_teb.h"
#include "../../List/MyList.h"
#include "../../ntos/inc/ntlpcapi.h"
#include "../../Log/log.h"
#include "../../ntos/inc/ki.h"
#include "../../ntos/inc/psp.h"
#include "../../Globals.h"
#include "../../DbgkApi/DbgkApi.h"
#include "../../Protect/Windows/BypassFindWnd.h"
#include "../../Protect/Thread/ProtectDrx.h"
#include "../../Protect/Process/ProtectProcess.h"
#include "../../Memory/ReadWrite.h"
#include "../../ntos/inc/ntexapi.h"
#include "../../Hvm/hypervisor_gateway.h"
#include "../../Init/Symbolic/InitWin32kbase.h"
#include "../../Init/Symbolic/InitWin32kfull.h"
#include "../../Process/Process.h"
#include "EptHook.h"

EXTERN_C
VOID UnEptHook()
{
    //卸载所有ept钩子
    if (hvgt::ept_unhook())
    {
        outLog("卸载所有ept钩子.");
    }
    else
    {
        outLog("卸载ept钩子失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
    }
}

EXTERN_C
VOID Hook_Test()
{
    if (g_IsInitGlobalVariable)
    {
        {
            if (hvgt::hook_function(Sys_DbgkpQueueMessage, DbgkpQueueMessage, NULL))
            {
                outLog("挂钩 系统函数成功。");
            }
            else
            {
                outLog("挂钩 系统函数失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
    }
}

EXTERN_C
VOID SetupEptHook()
{
    if (g_IsInitGlobalVariable)
    {
        //ntos
        Hook_NtCreateDebugObject();    //此函数是创建调试对象 必须第一个先hook
        Hook_PspInsertProcess();        
        Hook_NtSetInformationDebugObject();
        Hook_NtRemoveProcessDebug();
        Hook_NtDebugActiveProcess();
        Hook_NtWaitForDebugEvent();
        Hook_NtDebugContinue();
        Hook_DbgkMapViewOfSection();
        Hook_DbgkUnMapViewOfSection();
        Hook_DbgkCreateThread();
        Hook_DbgkExitThread();
        Hook_DbgkExitProcess();
        Hook_DbgkForwardException();
        Hook_DbgkpQueueMessage();
        Hook_PspCallThreadNotifyRoutines();
        Hook_PspExitThread();
        Hook_ObpReferenceObjectByHandleWithTag();

        //win32k.sys
        Hook_ValidateHwnd();  //win32k中此函数必须先hook
        Hook_NtUserFindWindowEx();        
        Hook_NtUserWindowFromPoint();
    }
}

//
//EXTERN_C
//VOID Hook_DbgkOpenProcessDebugPort()
//{
//    if (g_IsInitGlobalVariable)
//    {
//        ASSERT(Sys_DbgkOpenProcessDebugPort);
//        if (Sys_DbgkOpenProcessDebugPort)
//        {
//            if (hvgt::hook_function(Sys_DbgkOpenProcessDebugPort, DbgkOpenProcessDebugPort, NULL))
//            {
//                outLog("挂钩 DbgkOpenProcessDebugPort成功。");
//            }
//            else
//            {
//                outLog("挂钩 DbgkOpenProcessDebugPort失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
//            }
//        }
//        else
//        {
//            outLog("Sys_DbgkOpenProcessDebugPort为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
//        }
//    }
//}
//
EXTERN_C
VOID Hook_NtCreateDebugObject()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtCreateDebugObject);
        if (Sys_NtCreateDebugObject)
        {
            if (hvgt::hook_function(Sys_NtCreateDebugObject, NtCreateDebugObject, NULL))
            {
                outLog("挂钩 NtCreateDebugObject成功。");
            }
            else
            {
                outLog("挂钩 NtCreateDebugObject失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtCreateDebugObject为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
    else
    {
        outLog("Hook_NtCreateDebugObject 失败");
    }
}

EXTERN_C
VOID Hook_NtSetInformationDebugObject()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtSetInformationDebugObject);
        if (Sys_NtSetInformationDebugObject)
        {
            if (hvgt::hook_function(Sys_NtSetInformationDebugObject, NtSetInformationDebugObject, NULL))
            {
                outLog("挂钩 NtSetInformationDebugObject成功。");
            }
            else
            {
                outLog("挂钩 NtSetInformationDebugObject失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtSetInformationDebugObject为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtRemoveProcessDebug()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtRemoveProcessDebug);
        if (Sys_NtRemoveProcessDebug)
        {
            if (hvgt::hook_function(Sys_NtRemoveProcessDebug, NtRemoveProcessDebug, NULL))
            {
                outLog("挂钩 NtRemoveProcessDebug成功。");
            }
            else
            {
                outLog("挂钩 NtRemoveProcessDebug失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtRemoveProcessDebug为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtDebugActiveProcess()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtDebugActiveProcess);
        if (Sys_NtDebugActiveProcess)
        {
            if (hvgt::hook_function(Sys_NtDebugActiveProcess, NtDebugActiveProcess, NULL))
            {
                outLog("挂钩 NtDebugActiveProcess成功。");
            }
            else
            {
                outLog("挂钩 NtDebugActiveProcess失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtDebugActiveProcess为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtWaitForDebugEvent()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtWaitForDebugEvent);
        if (Sys_NtWaitForDebugEvent)
        {
            if (hvgt::hook_function(Sys_NtWaitForDebugEvent, NtWaitForDebugEvent, NULL))
            {
                outLog("挂钩 NtWaitForDebugEvent成功。");
            }
            else
            {
                outLog("挂钩 NtWaitForDebugEvent失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtWaitForDebugEvent为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_KiDispatchException()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_KiDispatchException);
        if (Sys_KiDispatchException)
        {
            if (hvgt::hook_function(Sys_KiDispatchException, KiDispatchException, (PVOID*)&Original_KiDispatchException))
            {
                outLog("挂钩 KiDispatchException成功。");
            }
            else
            {
                outLog("挂钩 KiDispatchException失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_KiDispatchException为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_PspInsertProcess()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_PspInsertProcess);
        if (Sys_PspInsertProcess)
        {
            if (hvgt::hook_function(Sys_PspInsertProcess, PspInsertProcess, NULL))
            {
                outLog("挂钩 PspInsertProcess成功。");
            }
            else
            {
                outLog("挂钩 PspInsertProcess失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_PspInsertProcess为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

//EXTERN_C
//VOID Hook_PspInsertThread()
//{
//    if (g_IsInitGlobalVariable)
//    {
//        ASSERT(Sys_PspInsertThread);
//        if (Sys_PspInsertThread)
//        {
//            if (hvgt::hook_function(Sys_PspInsertThread, PspInsertThread, NULL))
//            {
//                outLog("挂钩 PspInsertThread成功。");
//            }
//            else
//            {
//                outLog("挂钩 PspInsertThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
//            }
//        }
//        else
//        {
//            outLog("Sys_PspInsertThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
//        }
//    }
//}

EXTERN_C
VOID Hook_NtDebugContinue()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtDebugContinue);
        if (Sys_NtDebugContinue)
        {
            if (hvgt::hook_function(Sys_NtDebugContinue, NtDebugContinue, NULL))
            {
                outLog("挂钩 NtDebugContinue成功。");
            }
            else
            {
                outLog("挂钩 NtDebugContinue失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtDebugContinue为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_DbgkMapViewOfSection()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_DbgkMapViewOfSection);
        if (Sys_DbgkMapViewOfSection)
        {
            if (hvgt::hook_function(Sys_DbgkMapViewOfSection, DbgkMapViewOfSection, NULL))
            {
                outLog("挂钩 DbgkMapViewOfSection成功。");
            }
            else
            {
                outLog("挂钩 DbgkMapViewOfSection失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_DbgkMapViewOfSection为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_DbgkUnMapViewOfSection()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_DbgkUnMapViewOfSection);
        if (Sys_DbgkUnMapViewOfSection)
        {
            if (hvgt::hook_function(Sys_DbgkUnMapViewOfSection, DbgkUnMapViewOfSection, NULL))
            {
                outLog("挂钩 DbgkUnMapViewOfSection成功。");
            }
            else
            {
                outLog("挂钩 DbgkUnMapViewOfSection失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_DbgkUnMapViewOfSection为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtQueryInformationThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtQueryInformationThread);
        if (Sys_NtQueryInformationThread)
        {
            if (hvgt::hook_function(Sys_NtQueryInformationThread, NewNtQueryInformationThread, (PVOID*)&Original_NtQueryInformationThread))
            {
                outLog("挂钩 NtQueryInformationThread成功。");
            }
            else
            {
                outLog("挂钩 NtQueryInformationThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtQueryInformationThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtSuspendThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtSuspendThread);
        if (Sys_NtSuspendThread)
        {
            if (hvgt::hook_function(Sys_NtSuspendThread, NewNtSuspendThread, (PVOID*)&Original_NtSuspendThread))
            {
                outLog("挂钩 NtSuspendThread成功。");
            }
            else
            {
                outLog("挂钩 NtSuspendThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtSuspendThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtResumeThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtResumeThread);
        if (Sys_NtResumeThread)
        {
            if (hvgt::hook_function(Sys_NtResumeThread, NewNtResumeThread, (PVOID*)&Original_NtResumeThread))
            {
                outLog("挂钩 NtResumeThread成功。");
            }
            else
            {
                outLog("挂钩 NtResumeThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtResumeThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

//EXTERN_C
//VOID Hook_DbgkCreateThread()
//{
//    if (g_IsInitGlobalVariable)
//    {
//        ASSERT(Sys_DbgkCreateThread);
//        if (Sys_DbgkCreateThread)
//        {
//            if (hvgt::hook_function(Sys_DbgkCreateThread, DbgkCreateThread, (PVOID*)&Original_DbgkCreateThread))
//            {
//                outLog("挂钩 DbgkCreateThread成功。");
//            }
//            else
//            {
//                outLog("挂钩 DbgkCreateThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
//            }
//        }
//        else
//        {
//            outLog("Sys_DbgkCreateThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
//        }
//        SetupHook_DbgkCreateThread_CMP_Debugport();
//    }
//}

EXTERN_C
VOID Hook_DbgkCreateThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_DbgkCreateThread);
        if (Sys_DbgkCreateThread)
        {
            if (hvgt::hook_function(Sys_DbgkCreateThread, DbgkCreateThread, NULL))
            {
                outLog("挂钩 DbgkCreateThread成功。");
            }
            else
            {
                outLog("挂钩 DbgkCreateThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_DbgkCreateThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_DbgkExitThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_DbgkExitThread);
        if (Sys_DbgkExitThread)
        {
            if (hvgt::hook_function(Sys_DbgkExitThread, DbgkExitThread, NULL))
            {
                outLog("挂钩 DbgkExitThread成功。");
            }
            else
            {
                outLog("挂钩 DbgkExitThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_DbgkExitThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_DbgkExitProcess()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_DbgkExitProcess);
        if (Sys_DbgkExitProcess)
        {
            if (hvgt::hook_function(Sys_DbgkExitProcess, DbgkExitProcess, NULL))
            {
                outLog("挂钩 DbgkExitProcess成功。");
            }
            else
            {
                outLog("挂钩 DbgkExitProcess失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_DbgkExitProcess为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_DbgkForwardException()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_DbgkForwardException);
        if (Sys_DbgkForwardException)
        {
            if (hvgt::hook_function(Sys_DbgkForwardException, DbgkForwardException, NULL))
            {
                outLog("挂钩 DbgkForwardException成功。");
            }
            else
            {
                outLog("挂钩 DbgkForwardException失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_DbgkForwardException为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_DbgkpQueueMessage()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_DbgkpQueueMessage);
        if (Sys_DbgkpQueueMessage)
        {
            if (hvgt::hook_function(Sys_DbgkpQueueMessage, DbgkpQueueMessage, NULL))
            {
                outLog("挂钩 DbgkpQueueMessage成功。");
            }
            else
            {
                outLog("挂钩 DbgkpQueueMessage失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_DbgkpQueueMessage为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_KeStackAttachProcess()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_KeStackAttachProcess);
        if (Sys_KeStackAttachProcess)
        {
            if (hvgt::hook_function(Sys_KeStackAttachProcess, NewKeStackAttachProcess, (PVOID*)&Original_KeStackAttachProcess))
            {
                outLog("挂钩 KeStackAttachProcess成功。");
            }
            else
            {
                outLog("挂钩 KeStackAttachProcess失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_KeStackAttachProcess为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_KiStackAttachProcess()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_KiStackAttachProcess);
        if (Sys_KiStackAttachProcess)
        {
            if (hvgt::hook_function(Sys_KiStackAttachProcess, NewKiStackAttachProcess, (PVOID*)&Original_KiStackAttachProcess))
            {
                outLog("挂钩 KiStackAttachProcess成功。");
            }
            else
            {
                outLog("挂钩 KiStackAttachProcess失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_KiStackAttachProcess为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtProtectVirtualMemory()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtProtectVirtualMemory);
        if (Sys_NtProtectVirtualMemory)
        {
            if (hvgt::hook_function(Sys_NtProtectVirtualMemory, NtProtectVirtualMemory, (PVOID*)&Original_NtProtectVirtualMemory))
            {
                outLog("挂钩 NtProtectVirtualMemory成功。");
            }
            else
            {
                outLog("挂钩 NtProtectVirtualMemory失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtProtectVirtualMemory为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_MiObtainReferencedVadEx()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_MiObtainReferencedVadEx);
        if (Sys_MiObtainReferencedVadEx)
        {
            if (hvgt::hook_function(Sys_MiObtainReferencedVadEx, MiObtainReferencedVadEx, (PVOID*)&Original_MiObtainReferencedVadEx))
            {
                outLog("挂钩 MiObtainReferencedVadEx成功。");
            }
            else
            {
                outLog("挂钩 MiObtainReferencedVadEx失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_MiObtainReferencedVadEx为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_MmProtectVirtualMemory()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_MmProtectVirtualMemory);
        if (Sys_MmProtectVirtualMemory)
        {
            if (hvgt::hook_function(Sys_MmProtectVirtualMemory, MmProtectVirtualMemory, (PVOID*)&Original_MmProtectVirtualMemory))
            {
                outLog("挂钩 MmProtectVirtualMemory成功。");
            }
            else
            {
                outLog("挂钩 MmProtectVirtualMemory失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_MmProtectVirtualMemory为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtGetContextThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtGetContextThread);
        if (Sys_NtGetContextThread)
        {
            if (hvgt::hook_function(Sys_NtGetContextThread, NtGetContextThread, (PVOID*)&Original_NtGetContextThread))
            {
                outLog("挂钩 NtGetContextThread成功。");
            }
            else
            {
                outLog("挂钩 NtGetContextThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtGetContextThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtSetContextThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtSetContextThread);
        if (Sys_NtSetContextThread)
        {
            if (hvgt::hook_function(Sys_NtSetContextThread, NtSetContextThread, (PVOID*)&Original_NtSetContextThread))
            {
                outLog("挂钩 NtSetContextThread成功。");
            }
            else
            {
                outLog("挂钩 NtSetContextThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtSetContextThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

//EXTERN_C
//VOID Hook_NtShutdownSystem()
//{
//    if (g_IsInitGlobalVariable)
//    {
//        ASSERT(Sys_NtShutdownSystem);
//        if (Sys_NtShutdownSystem)
//        {
//            if (hvgt::hook_function(Sys_NtShutdownSystem, NtShutdownSystem, NULL))
//            {
//                outLog("挂钩 NtShutdownSystem成功。");
//            }
//            else
//            {
//                outLog("挂钩 NtShutdownSystem失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
//            }
//        }
//        else
//        {
//            outLog("Sys_NtShutdownSystem为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
//        }
//    }
//}

EXTERN_C
VOID Hook_NtOpenProcess()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtOpenProcess);
        if (Sys_NtOpenProcess)
        {
            if (hvgt::hook_function(Sys_NtOpenProcess, NewNtOpenProcess, (PVOID*)&Original_NtOpenProcess))
            {
                outLog("挂钩 NtOpenProcess成功。");
            }
            else
            {
                outLog("挂钩 NtOpenProcess失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtOpenProcess为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtReadVirtualMemory()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtReadVirtualMemory);
        if (Sys_NtReadVirtualMemory)
        {
            if (hvgt::hook_function(Sys_NtReadVirtualMemory, NtReadVirtualMemory, (PVOID*)&Original_NtReadVirtualMemory))
            {
                outLog("挂钩 NtReadVirtualMemory成功。");
            }
            else
            {
                outLog("挂钩 NtReadVirtualMemory失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtReadVirtualMemory为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtWriteVirtualMemory()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtWriteVirtualMemory);
        if (Sys_NtWriteVirtualMemory)
        {
            if (hvgt::hook_function(Sys_NtWriteVirtualMemory, NtWriteVirtualMemory, (PVOID*)&Original_NtWriteVirtualMemory))
            {
                outLog("挂钩 NtWriteVirtualMemory成功。");
            }
            else
            {
                outLog("挂钩 NtWriteVirtualMemory失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtWriteVirtualMemory为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_ObReferenceObjectByHandle()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_ObReferenceObjectByHandle);
        if (Sys_ObReferenceObjectByHandle)
        {
            if (hvgt::hook_function(Sys_ObReferenceObjectByHandle, NewObReferenceObjectByHandle, (PVOID*)&Original_ObReferenceObjectByHandle))
            {
                outLog("挂钩 ObReferenceObjectByHandle成功。");
            }
            else
            {
                outLog("挂钩 ObReferenceObjectByHandle失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_ObReferenceObjectByHandle为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_ObReferenceObjectByHandleWithTag()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_ObReferenceObjectByHandleWithTag);
        if (Sys_ObReferenceObjectByHandleWithTag)
        {
            if (hvgt::hook_function(Sys_ObReferenceObjectByHandleWithTag, NewObReferenceObjectByHandleWithTag, (PVOID*)&Original_ObReferenceObjectByHandleWithTag))
            {
                outLog("挂钩 ObReferenceObjectByHandleWithTag成功。");
            }
            else
            {
                outLog("挂钩 ObReferenceObjectByHandleWithTag失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_ObReferenceObjectByHandleWithTag为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_ObpReferenceObjectByHandleWithTag()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_ObpReferenceObjectByHandleWithTag);
        if (Sys_ObpReferenceObjectByHandleWithTag)
        {
            if (hvgt::hook_function(Sys_ObpReferenceObjectByHandleWithTag, NewObpReferenceObjectByHandleWithTag, (PVOID*)&Original_ObpReferenceObjectByHandleWithTag))
            {
                outLog("挂钩 ObpReferenceObjectByHandleWithTag成功。");
            }
            else
            {
                outLog("挂钩 ObpReferenceObjectByHandleWithTag失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_ObpReferenceObjectByHandleWithTag为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_ObfDereferenceObjectWithTag()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_ObfDereferenceObjectWithTag);
        if (Sys_ObfDereferenceObjectWithTag)
        {
            if (hvgt::hook_function(Sys_ObfDereferenceObjectWithTag, NewObfDereferenceObjectWithTag, (PVOID*)&Original_ObfDereferenceObjectWithTag))
            {
                outLog("挂钩 ObfDereferenceObjectWithTag成功。");
            }
            else
            {
                outLog("挂钩 ObfDereferenceObjectWithTag失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_ObfDereferenceObjectWithTag为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_ObfDereferenceObject()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_ObfDereferenceObject);
        if (Sys_ObfDereferenceObject)
        {
            if (hvgt::hook_function(Sys_ObfDereferenceObject, NewObfDereferenceObject, (PVOID*)&Original_ObfDereferenceObject))
            {
                outLog("挂钩 ObfDereferenceObject成功。");
            }
            else
            {
                outLog("挂钩 ObfDereferenceObject失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_ObfDereferenceObject为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_MmCopyVirtualMemory()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_MmCopyVirtualMemory);
        if (Sys_MmCopyVirtualMemory)
        {
            if (hvgt::hook_function(Sys_MmCopyVirtualMemory, NewMmCopyVirtualMemory, (PVOID*)&Original_MmCopyVirtualMemory))
            {
                outLog("挂钩 MmCopyVirtualMemory成功。");
            }
            else
            {
                outLog("挂钩 MmCopyVirtualMemory失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_MmCopyVirtualMemory为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_PspCreateUserContext()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_PspCreateUserContext);
        if (Sys_PspCreateUserContext)
        {
            if (hvgt::hook_function(Sys_PspCreateUserContext, NewPspCreateUserContext, (PVOID*)&Original_PspCreateUserContext))
            {
                outLog("挂钩 PspCreateUserContext成功。");
            }
            else
            {
                outLog("挂钩 PspCreateUserContext失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_PspCreateUserContext为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_PspCallThreadNotifyRoutines()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_PspCallThreadNotifyRoutines);
        if (Sys_PspCallThreadNotifyRoutines)
        {
            if (hvgt::hook_function(Sys_PspCallThreadNotifyRoutines, NewPspCallThreadNotifyRoutines, (PVOID*)&Original_PspCallThreadNotifyRoutines))
            {
                outLog("挂钩 PspCallThreadNotifyRoutines成功。");
            }
            else
            {
                outLog("挂钩 PspCallThreadNotifyRoutines失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_PspCallThreadNotifyRoutines为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtTerminateProcess()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtTerminateProcess);
        if (Sys_NtTerminateProcess)
        {
            if (hvgt::hook_function(Sys_NtTerminateProcess, NewNtTerminateProcess, (PVOID*)&Original_NtTerminateProcess))
            {
                outLog("挂钩 NtTerminateProcess成功。");
            }
            else
            {
                outLog("挂钩 NtTerminateProcess失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtTerminateProcess为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_PspExitThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_PspExitThread);
        if (Sys_PspExitThread)
        {
            if (hvgt::hook_function(Sys_PspExitThread, PspExitThread, (PVOID*)&Original_PspExitThread))
            {
                outLog("挂钩 PspExitThread成功。");
            }
            else
            {
                outLog("挂钩 PspExitThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_PspExitThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
        SetupHook_PspExitThread_CMP_Debugport();
    }
}

EXTERN_C
VOID Hook_PspCreateThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_PspCreateThread);
        if (Sys_PspCreateThread)
        {
            if (hvgt::hook_function(Sys_PspCreateThread, PspCreateThread, (PVOID*)&Original_PspCreateThread))
            {
                outLog("挂钩 PspCreateThread成功。");
            }
            else
            {
                outLog("挂钩 PspCreateThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_PspCreateThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtCreateThreadEx()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtCreateThreadEx);
        if (Sys_NtCreateThreadEx)
        {
            if (hvgt::hook_function(Sys_NtCreateThreadEx, NtCreateThreadEx, (PVOID*)&Original_NtCreateThreadEx))
            {
                outLog("挂钩 NtCreateThreadEx成功。");
            }
            else
            {
                outLog("挂钩 NtCreateThreadEx失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtCreateThreadEx为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_PspAllocateThread()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_PspAllocateThread);
        if (Sys_PspAllocateThread)
        {
            if (hvgt::hook_function(Sys_PspAllocateThread, NewPspAllocateThread, (PVOID*)&Original_PspAllocateThread))
            {
                outLog("挂钩 PspAllocateThread成功。");
            }
            else
            {
                outLog("挂钩 PspAllocateThread失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_PspAllocateThread为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

//EXTERN_C
//VOID Hook_DbgkpCloseObject()
//{
//    if (g_IsInitGlobalVariable)
//    {
//        ASSERT(Sys_DbgkpCloseObject);
//        if (Sys_DbgkpCloseObject)
//        {
//            if (hvgt::hook_function(Sys_DbgkpCloseObject, DbgkpCloseObject, NULL))
//            {
//                outLog("挂钩 DbgkpCloseObject成功。");
//            }
//            else
//            {
//                outLog("挂钩 DbgkpCloseObject失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
//            }
//        }
//        else
//        {
//            outLog("Sys_DbgkpCloseObject为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
//        }
//    }
//}
//


//win32k.sys
EXTERN_C
VOID Hook_NtUserFindWindowEx()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtUserFindWindowEx);
        if (Sys_NtUserFindWindowEx)
        {
            if (hvgt::hook_function(Sys_NtUserFindWindowEx, NewNtUserFindWindowEx, (PVOID*)&Original_NtUserFindWindowEx))
            {
                outLog("挂钩 NtUserFindWindowEx成功。");
            }
            else
            {
                outLog("挂钩 NtUserFindWindowEx失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtUserFindWindowEx为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_ValidateHwnd()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_ValidateHwnd);
        if (Sys_ValidateHwnd)
        {
            if (hvgt::hook_function(Sys_ValidateHwnd, NewValidateHwnd, (PVOID*)&Original_ValidateHwnd))
            {
                outLog("挂钩 ValidateHwnd成功。");
            }
            else
            {
                outLog("挂钩 ValidateHwnd失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_ValidateHwnd为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}

EXTERN_C
VOID Hook_NtUserWindowFromPoint()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_NtUserWindowFromPoint);
        if (Sys_NtUserWindowFromPoint)
        {
            if (hvgt::hook_function(Sys_NtUserWindowFromPoint, NewNtUserWindowFromPoint, (PVOID*)&Original_NtUserWindowFromPoint))
            {
                outLog("挂钩 NtUserWindowFromPoint成功。");
            }
            else
            {
                outLog("挂钩 NtUserWindowFromPoint失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。");
            }
        }
        else
        {
            outLog("Sys_NtUserWindowFromPoint为空指针；原因：符号表未提供该函数；解决方案：检查 PDB 与 Windows 版本是否匹配。");
        }
    }
}