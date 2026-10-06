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
#include "../../ntos/inc/psp.h"
#include "../../Globals.h"
#include "../../DbgkApi/DbgkApi.h"
#include "../../Log/log.h"
#include "../../Hooks/EptHook/EptHook.h"
#include "InitSymbolic.h"
#include "../../Encrypt/Blowfish/Blowfish.h"
#include "../../Hvm/vmcall_reason.h"
#include "../../Hvm/hypervisor_gateway.h"

VOID InitSymbolsTable(IN PUSER_DATA userData, IN PIRP pIrp)
{    
    BOOLEAN boSuccess = FALSE;
    USER_DATA user = GetUserData(userData);

    DWORD* output = (DWORD*)pIrp->AssociatedIrp.SystemBuffer;  //内核的缓冲区，输入输出都用的这个
    RtlZeroMemory(output, sizeof(DWORD));

    //分配明文缓存区
    BYTE* aucPlainText = allocate_pool<BYTE*>(user.uSize);
    DecryptData((PVOID)user.pUserData, aucPlainText);

    // 计算明文缓冲区中的结构体数量
    size_t numElements = user.uSize / sizeof(RING3_VERIFY);

    // 遍历明文缓冲区中的结构体
    for (size_t i = 0; i < numElements; i++)
    {
        PRING3_VERIFY pInfo = reinterpret_cast<PRING3_VERIFY>(aucPlainText + i * sizeof(RING3_VERIFY));

        if (pInfo->key == 0x9dd14d00f5dd71bd)
        {
            boSuccess = TRUE;
            *output = 1998;
            break;
        }
    }
    free_pool(aucPlainText);


    if (boSuccess)
    {
        if (!InitNtoskrnlSymbolsTable())
        {
            return;
        }

        if (!InitWin32kbaseSymbolsTable())
        {
            return;
        }

        if (!InitWin32kfullSymbolsTable())
        {
            return;
        }

        CheckFunctionPointers();


        g_IsInitGlobalVariable = TRUE;
        if (g_IsInitGlobalVariable)
        {
            if (DispatchOffsetToHost())
            {
                DbgkInitialize();
                SetupEptHook();
            }
        }
    }
}

//将内核结构的偏移发送给vt host
bool DispatchOffsetToHost()
{
    if (g_IsInitGlobalVariable)
    {
        WINDOWS_STRUCT vmcallinfo = { 0 };
        vmcallinfo.ethread_offset_Cid = ethread_offset::Cid;
        vmcallinfo.command = VMCALL_INIT_OFFSET;
        if (hvgt::vmcall(&vmcallinfo))
        {
            outLog("为vt host初始化offset成功..");
            return true;
        }
        else
        {
            outLog("为vt host初始化offset失败..");
        }
    }
    return false;
}

void CheckFunctionPointers()
{
    //ntos

    CHECK_FUNC_PTR(PspLoaderInitRoutine);
    CHECK_FUNC_PTR(DbgkDebugObjectType);
    CHECK_FUNC_PTR(PspNotifyEnableMask);
    CHECK_FUNC_PTR(PerfGlobalGroupMask);
    CHECK_FUNC_PTR(PspActiveProcessLock);
    CHECK_FUNC_PTR(PspProcessSequenceNumber);
    CHECK_FUNC_PTR(PsActiveProcessHead);

    CHECK_FUNC_PTR(PsFreezeProcess);
    CHECK_FUNC_PTR(PsThawProcess);
    CHECK_FUNC_PTR(Sys_NtCreateDebugObject);
    CHECK_FUNC_PTR(DbgkpSuspendProcess);
    CHECK_FUNC_PTR(Sys_DbgkCreateThread);
    CHECK_FUNC_PTR(Sys_DbgkpQueueMessage);
    CHECK_FUNC_PTR(DbgkpSectionToFileHandle);
    CHECK_FUNC_PTR(DbgkpSendApiMessage);
    CHECK_FUNC_PTR(Sys_DbgkMapViewOfSection);
    CHECK_FUNC_PTR(Sys_DbgkUnMapViewOfSection);
    CHECK_FUNC_PTR(DbgkpSuppressDbgMsg);
    CHECK_FUNC_PTR(Sys_DbgkExitThread);
    CHECK_FUNC_PTR(PsSetProcessFaultInformation);
    CHECK_FUNC_PTR(PsCaptureExceptionPort);
    CHECK_FUNC_PTR(DbgkpSendApiMessageLpc);
    CHECK_FUNC_PTR(DbgkpSendErrorMessage);
    CHECK_FUNC_PTR(Sys_DbgkForwardException);
    CHECK_FUNC_PTR(DbgkpPostFakeProcessCreateMessages);
    CHECK_FUNC_PTR(Sys_NtDebugActiveProcess);
    CHECK_FUNC_PTR(Sys_DbgkExitProcess);
    CHECK_FUNC_PTR(Sys_PspExitThread);
    CHECK_FUNC_PTR(DbgkpWakeTarget);
    CHECK_FUNC_PTR(Sys_NtDebugContinue);
    CHECK_FUNC_PTR(Sys_NtWaitForDebugEvent);
    CHECK_FUNC_PTR(ObDuplicateObject);
    CHECK_FUNC_PTR(DbgkClearProcessDebugObject);
    CHECK_FUNC_PTR(Sys_NtRemoveProcessDebug);
    CHECK_FUNC_PTR(PsGetNextProcess);
    CHECK_FUNC_PTR(DbgkpMarkProcessPeb);
    CHECK_FUNC_PTR(PsTerminateProcess);
    CHECK_FUNC_PTR(ObCreateObjectType);
    CHECK_FUNC_PTR(PsGetNextProcessThread);
    CHECK_FUNC_PTR(DbgkpPostFakeThreadMessages);
    CHECK_FUNC_PTR(KiStackAttachProcess);
    CHECK_FUNC_PTR(KiUnstackDetachProcess);
    CHECK_FUNC_PTR(Sys_NtReadVirtualMemory);
    CHECK_FUNC_PTR(Sys_NtWriteVirtualMemory);
    CHECK_FUNC_PTR(ZwProtectVirtualMemory);
    CHECK_FUNC_PTR(Sys_NtProtectVirtualMemory);
    CHECK_FUNC_PTR(Sys_PspCreateThread);
    CHECK_FUNC_PTR(Sys_NtCreateThreadEx);
    CHECK_FUNC_PTR(Sys_NtOpenProcess);
    CHECK_FUNC_PTR(DbgkpConvertKernelToUserStateChange);
    CHECK_FUNC_PTR(DbgkpOpenHandles);
    CHECK_FUNC_PTR(KeCopyExceptionRecord);
    CHECK_FUNC_PTR(Sys_ObReferenceObjectByHandleWithTag);
    CHECK_FUNC_PTR(Sys_ObReferenceObjectByHandle);
    CHECK_FUNC_PTR(Sys_ObfDereferenceObjectWithTag);
    CHECK_FUNC_PTR(Sys_ObfDereferenceObject);
    CHECK_FUNC_PTR(KiCheckForKernelApcDelivery);
    CHECK_FUNC_PTR(KeEnterCriticalRegionThread);
    CHECK_FUNC_PTR(KeLeaveCriticalRegionThread);
    CHECK_FUNC_PTR(Sys_MmCopyVirtualMemory);
    CHECK_FUNC_PTR(Sys_PspCreateUserContext);
    CHECK_FUNC_PTR(Sys_PspCallThreadNotifyRoutines);
    CHECK_FUNC_PTR(Sys_PspAllocateThread);
    CHECK_FUNC_PTR(Sys_ObpReferenceObjectByHandleWithTag);
    CHECK_FUNC_PTR(Sys_MiObtainReferencedVadEx);
    CHECK_FUNC_PTR(Sys_MmProtectVirtualMemory);
    CHECK_FUNC_PTR(Sys_NtGetContextThread);
    CHECK_FUNC_PTR(Sys_NtSetContextThread);
    CHECK_FUNC_PTR(ZwGetContextThread);
    CHECK_FUNC_PTR(PspGetContextThreadInternal);
    CHECK_FUNC_PTR(Sys_KiDispatchException);
    CHECK_FUNC_PTR(Sys_KeStackAttachProcess);
    CHECK_FUNC_PTR(Sys_KiStackAttachProcess);
    CHECK_FUNC_PTR(Sys_NtSetInformationDebugObject);
    CHECK_FUNC_PTR(Sys_NtTerminateProcess);
    CHECK_FUNC_PTR(Sys_NtSuspendThread);
    CHECK_FUNC_PTR(Sys_NtResumeThread);
    CHECK_FUNC_PTR(Sys_NtQueryInformationThread);
    CHECK_FUNC_PTR(PsGetCurrentProcessByThread);
    CHECK_FUNC_PTR(PsQuerySystemDllInfo);
    CHECK_FUNC_PTR(PsWow64GetProcessNtdllType);
    CHECK_FUNC_PTR(PspReferenceSystemDll);
    CHECK_FUNC_PTR(MiSectionControlArea);
    CHECK_FUNC_PTR(MiReferenceControlAreaFile);
    CHECK_FUNC_PTR(ObFastDereferenceObject);
    CHECK_FUNC_PTR(DbgkpPostModuleMessages);
    CHECK_FUNC_PTR(PsCallImageNotifyRoutines);
    CHECK_FUNC_PTR(PsReferenceProcessFilePointer);
    CHECK_FUNC_PTR(SeAuditingWithTokenForSubcategory);
    CHECK_FUNC_PTR(SeAuditProcessCreation);
    CHECK_FUNC_PTR(PspImplicitAssignProcessToJob);
    CHECK_FUNC_PTR(PspUnlockProcessListExclusive);
    CHECK_FUNC_PTR(DbgkCopyProcessDebugPort);
    CHECK_FUNC_PTR(SeCreateAccessStateEx);
    CHECK_FUNC_PTR(ObInsertObjectEx);
    CHECK_FUNC_PTR(ObCheckRefTraceProcess);
    CHECK_FUNC_PTR(PspValidateJobAffinityState);
    CHECK_FUNC_PTR(SepDeleteAccessState);
    CHECK_FUNC_PTR(Sys_PspInsertProcess);
    CHECK_FUNC_PTR(DbgkSendSystemDllMessages);

    //win32kbase
    CHECK_FUNC_PTR(Sys_ValidateHwnd);

    //win32kfull
    CHECK_FUNC_PTR(Sys_NtUserFindWindowEx);
    CHECK_FUNC_PTR(Sys_NtUserWindowFromPoint);
}