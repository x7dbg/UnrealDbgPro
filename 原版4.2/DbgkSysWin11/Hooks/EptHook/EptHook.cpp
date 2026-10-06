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
#include "../../Process/process.h"
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
        outLog("卸载ept钩子失败.");
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


EXTERN_C
VOID Hook_DbgkOpenProcessDebugPort()
{
    if (g_IsInitGlobalVariable)
    {
        ASSERT(Sys_DbgkOpenProcessDebugPort);
        if (Sys_DbgkOpenProcessDebugPort)
        {
            if (hvgt::hook_function(Sys_DbgkOpenProcessDebugPort, DbgkOpenProcessDebugPort, NULL))
            {
                outLog("hook DbgkOpenProcessDebugPort成功.");
            }
            else
            {
                outLog("hook DbgkOpenProcessDebugPort失败.");
            }
        }
        else
        {
            outLog("Sys_DbgkOpenProcessDebugPort为空指针.");
        }
    }
}

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
                outLog("hook NtCreateDebugObject成功.");
            }
            else
            {
                outLog("hook NtCreateDebugObject失败.");
            }
        }
        else
        {
            outLog("Sys_NtCreateDebugObject为空指针.");
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
                outLog("hook NtSetInformationDebugObject成功.");
            }
            else
            {
                outLog("hook NtSetInformationDebugObject失败.");
            }
        }
        else
        {
            outLog("Sys_NtSetInformationDebugObject为空指针.");
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
                outLog("hook NtRemoveProcessDebug成功.");
            }
            else
            {
                outLog("hook NtRemoveProcessDebug失败.");
            }
        }
        else
        {
            outLog("Sys_NtRemoveProcessDebug为空指针.");
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
                outLog("hook NtDebugActiveProcess成功.");
            }
            else
            {
                outLog("hook NtDebugActiveProcess失败.");
            }
        }
        else
        {
            outLog("Sys_NtDebugActiveProcess为空指针.");
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
                outLog("hook NtWaitForDebugEvent成功.");
            }
            else
            {
                outLog("hook NtWaitForDebugEvent失败.");
            }
        }
        else
        {
            outLog("Sys_NtWaitForDebugEvent为空指针.");
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
                outLog("hook KiDispatchException成功.");
            }
            else
            {
                outLog("hook KiDispatchException失败.");
            }
        }
        else
        {
            outLog("Sys_KiDispatchException为空指针.");
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
                outLog("hook PspInsertProcess成功.");
            }
            else
            {
                outLog("hook PspInsertProcess失败.");
            }
        }
        else
        {
            outLog("Sys_PspInsertProcess为空指针.");
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
//                outLog("hook PspInsertThread成功.");
//            }
//            else
//            {
//                outLog("hook PspInsertThread失败.");
//            }
//        }
//        else
//        {
//            outLog("Sys_PspInsertThread为空指针.");
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
                outLog("hook NtDebugContinue成功.");
            }
            else
            {
                outLog("hook NtDebugContinue失败.");
            }
        }
        else
        {
            outLog("Sys_NtDebugContinue为空指针.");
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
                outLog("hook DbgkMapViewOfSection成功.");
            }
            else
            {
                outLog("hook DbgkMapViewOfSection失败.");
            }
        }
        else
        {
            outLog("Sys_DbgkMapViewOfSection为空指针.");
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
                outLog("hook DbgkUnMapViewOfSection成功.");
            }
            else
            {
                outLog("hook DbgkUnMapViewOfSection失败.");
            }
        }
        else
        {
            outLog("Sys_DbgkUnMapViewOfSection为空指针.");
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
                outLog("hook NtQueryInformationThread成功.");
            }
            else
            {
                outLog("hook NtQueryInformationThread失败.");
            }
        }
        else
        {
            outLog("Sys_NtQueryInformationThread为空指针.");
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
                outLog("hook NtSuspendThread成功.");
            }
            else
            {
                outLog("hook NtSuspendThread失败.");
            }
        }
        else
        {
            outLog("Sys_NtSuspendThread为空指针.");
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
                outLog("hook NtResumeThread成功.");
            }
            else
            {
                outLog("hook NtResumeThread失败.");
            }
        }
        else
        {
            outLog("Sys_NtResumeThread为空指针.");
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
//                outLog("hook DbgkCreateThread成功.");
//            }
//            else
//            {
//                outLog("hook DbgkCreateThread失败.");
//            }
//        }
//        else
//        {
//            outLog("Sys_DbgkCreateThread为空指针.");
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
                outLog("hook DbgkCreateThread成功.");
            }
            else
            {
                outLog("hook DbgkCreateThread失败.");
            }
        }
        else
        {
            outLog("Sys_DbgkCreateThread为空指针.");
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
                outLog("hook DbgkExitThread成功.");
            }
            else
            {
                outLog("hook DbgkExitThread失败.");
            }
        }
        else
        {
            outLog("Sys_DbgkExitThread为空指针.");
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
                outLog("hook DbgkExitProcess成功.");
            }
            else
            {
                outLog("hook DbgkExitProcess失败.");
            }
        }
        else
        {
            outLog("Sys_DbgkExitProcess为空指针.");
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
                outLog("hook DbgkForwardException成功.");
            }
            else
            {
                outLog("hook DbgkForwardException失败.");
            }
        }
        else
        {
            outLog("Sys_DbgkForwardException为空指针.");
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
                outLog("hook DbgkpQueueMessage成功.");
            }
            else
            {
                outLog("hook DbgkpQueueMessage失败.");
            }
        }
        else
        {
            outLog("Sys_DbgkpQueueMessage为空指针.");
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
                outLog("hook KeStackAttachProcess成功.");
            }
            else
            {
                outLog("hook KeStackAttachProcess失败.");
            }
        }
        else
        {
            outLog("Sys_KeStackAttachProcess为空指针.");
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
                outLog("hook KiStackAttachProcess成功.");
            }
            else
            {
                outLog("hook KiStackAttachProcess失败.");
            }
        }
        else
        {
            outLog("Sys_KiStackAttachProcess为空指针.");
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
                outLog("hook NtProtectVirtualMemory成功.");
            }
            else
            {
                outLog("hook NtProtectVirtualMemory失败.");
            }
        }
        else
        {
            outLog("Sys_NtProtectVirtualMemory为空指针.");
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
                outLog("hook MiObtainReferencedVadEx成功.");
            }
            else
            {
                outLog("hook MiObtainReferencedVadEx失败.");
            }
        }
        else
        {
            outLog("Sys_MiObtainReferencedVadEx为空指针.");
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
                outLog("hook MmProtectVirtualMemory成功.");
            }
            else
            {
                outLog("hook MmProtectVirtualMemory失败.");
            }
        }
        else
        {
            outLog("Sys_MmProtectVirtualMemory为空指针.");
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
                outLog("hook NtGetContextThread成功.");
            }
            else
            {
                outLog("hook NtGetContextThread失败.");
            }
        }
        else
        {
            outLog("Sys_NtGetContextThread为空指针.");
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
                outLog("hook NtSetContextThread成功.");
            }
            else
            {
                outLog("hook NtSetContextThread失败.");
            }
        }
        else
        {
            outLog("Sys_NtSetContextThread为空指针.");
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
//                outLog("hook NtShutdownSystem成功.");
//            }
//            else
//            {
//                outLog("hook NtShutdownSystem失败.");
//            }
//        }
//        else
//        {
//            outLog("Sys_NtShutdownSystem为空指针.");
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
                outLog("hook NtOpenProcess成功.");
            }
            else
            {
                outLog("hook NtOpenProcess失败.");
            }
        }
        else
        {
            outLog("Sys_NtOpenProcess为空指针.");
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
                outLog("hook NtReadVirtualMemory成功.");
            }
            else
            {
                outLog("hook NtReadVirtualMemory失败.");
            }
        }
        else
        {
            outLog("Sys_NtReadVirtualMemory为空指针.");
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
                outLog("hook NtWriteVirtualMemory成功.");
            }
            else
            {
                outLog("hook NtWriteVirtualMemory失败.");
            }
        }
        else
        {
            outLog("Sys_NtWriteVirtualMemory为空指针.");
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
                outLog("hook ObReferenceObjectByHandle成功.");
            }
            else
            {
                outLog("hook ObReferenceObjectByHandle失败.");
            }
        }
        else
        {
            outLog("Sys_ObReferenceObjectByHandle为空指针.");
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
                outLog("hook ObReferenceObjectByHandleWithTag成功.");
            }
            else
            {
                outLog("hook ObReferenceObjectByHandleWithTag失败.");
            }
        }
        else
        {
            outLog("Sys_ObReferenceObjectByHandleWithTag为空指针.");
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
                outLog("hook ObpReferenceObjectByHandleWithTag成功.");
            }
            else
            {
                outLog("hook ObpReferenceObjectByHandleWithTag失败.");
            }
        }
        else
        {
            outLog("Sys_ObpReferenceObjectByHandleWithTag为空指针.");
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
                outLog("hook ObfDereferenceObjectWithTag成功.");
            }
            else
            {
                outLog("hook ObfDereferenceObjectWithTag失败.");
            }
        }
        else
        {
            outLog("Sys_ObfDereferenceObjectWithTag为空指针.");
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
                outLog("hook ObfDereferenceObject成功.");
            }
            else
            {
                outLog("hook ObfDereferenceObject失败.");
            }
        }
        else
        {
            outLog("Sys_ObfDereferenceObject为空指针.");
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
                outLog("hook MmCopyVirtualMemory成功.");
            }
            else
            {
                outLog("hook MmCopyVirtualMemory失败.");
            }
        }
        else
        {
            outLog("Sys_MmCopyVirtualMemory为空指针.");
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
                outLog("hook PspCreateUserContext成功.");
            }
            else
            {
                outLog("hook PspCreateUserContext失败.");
            }
        }
        else
        {
            outLog("Sys_PspCreateUserContext为空指针.");
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
                outLog("hook PspCallThreadNotifyRoutines成功.");
            }
            else
            {
                outLog("hook PspCallThreadNotifyRoutines失败.");
            }
        }
        else
        {
            outLog("Sys_PspCallThreadNotifyRoutines为空指针.");
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
                outLog("hook NtTerminateProcess成功.");
            }
            else
            {
                outLog("hook NtTerminateProcess失败.");
            }
        }
        else
        {
            outLog("Sys_NtTerminateProcess为空指针.");
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
                outLog("hook PspExitThread成功.");
            }
            else
            {
                outLog("hook PspExitThread失败.");
            }
        }
        else
        {
            outLog("Sys_PspExitThread为空指针.");
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
                outLog("hook PspCreateThread成功.");
            }
            else
            {
                outLog("hook PspCreateThread失败.");
            }
        }
        else
        {
            outLog("Sys_PspCreateThread为空指针.");
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
                outLog("hook NtCreateThreadEx成功.");
            }
            else
            {
                outLog("hook NtCreateThreadEx失败.");
            }
        }
        else
        {
            outLog("Sys_NtCreateThreadEx为空指针.");
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
                outLog("hook PspAllocateThread成功.");
            }
            else
            {
                outLog("hook PspAllocateThread失败.");
            }
        }
        else
        {
            outLog("Sys_PspAllocateThread为空指针.");
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
//                outLog("hook DbgkpCloseObject成功.");
//            }
//            else
//            {
//                outLog("hook DbgkpCloseObject失败.");
//            }
//        }
//        else
//        {
//            outLog("Sys_DbgkpCloseObject为空指针.");
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
                outLog("hook NtUserFindWindowEx成功.");
            }
            else
            {
                outLog("hook NtUserFindWindowEx失败.");
            }
        }
        else
        {
            outLog("Sys_NtUserFindWindowEx为空指针.");
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
                outLog("hook ValidateHwnd成功.");
            }
            else
            {
                outLog("hook ValidateHwnd失败.");
            }
        }
        else
        {
            outLog("Sys_ValidateHwnd为空指针.");
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
                outLog("hook NtUserWindowFromPoint成功.");
            }
            else
            {
                outLog("hook NtUserWindowFromPoint失败.");
            }
        }
        else
        {
            outLog("Sys_NtUserWindowFromPoint为空指针.");
        }
    }
}