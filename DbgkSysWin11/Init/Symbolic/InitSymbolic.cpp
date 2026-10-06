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

NTSTATUS InitSymbolsTable(IN PUSER_DATA userData, IN PIRP pIrp)
{
    constexpr ULONG kMaxPlaintextSize = 0x8000;
    NTSTATUS status = STATUS_SUCCESS;
    BYTE* plainText = nullptr;
    CHAR* cipherText = nullptr;
    DWORD* output = nullptr;

    if (pIrp == nullptr || pIrp->AssociatedIrp.SystemBuffer == nullptr || userData == nullptr)
    {
        outLog("符号表初始化失败：IRP、输入结构或系统缓冲区为空；解决方案：确认使用 METHOD_BUFFERED 且输入长度至少为 USER_DATA。 ");
        return STATUS_INVALID_PARAMETER;
    }

    const USER_DATA user = GetUserData(userData);
    output = static_cast<DWORD*>(pIrp->AssociatedIrp.SystemBuffer);
    *output = 0;
    if (user.uSize == 0 || user.uSize > kMaxPlaintextSize ||
        (user.uSize % sizeof(RING3_VERIFY)) != 0 || user.pUserData == 0)
    {
        outLog("符号表初始化失败：负载长度=%lu、指针=%p 不符合验证结构；解决方案：传入非空、按 RING3_VERIFY 对齐且不超过 32KB 的加密负载。 ",
            user.uSize, reinterpret_cast<PVOID>(user.pUserData));
        return STATUS_INVALID_PARAMETER;
    }

    const SIZE_T paddedSize = (static_cast<SIZE_T>(user.uSize) + 7u) & ~static_cast<SIZE_T>(7u);
    const SIZE_T cipherSize = paddedSize * 2u;
    if (cipherSize == 0 || cipherSize >= (NTSTRSAFE_MAX_CCH - 1u))
    {
        outLog("符号表初始化失败：加密负载长度=%zu 超过安全上限；解决方案：减少一次提交的数据量。 ", cipherSize);
        return STATUS_INVALID_PARAMETER;
    }

    cipherText = allocate_pool<CHAR*>(cipherSize + 1u);
    plainText = allocate_pool<BYTE*>(paddedSize);
    if (cipherText == nullptr || plainText == nullptr)
    {
        outLog("符号表初始化失败：内核非分页内存分配不足；解决方案：释放其他驱动资源后重试或检查系统内存。 ");
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    __try
    {
        ProbeForRead(reinterpret_cast<PVOID>(user.pUserData), cipherSize, sizeof(UCHAR));
        RtlCopyMemory(cipherText, reinterpret_cast<PVOID>(user.pUserData), cipherSize);
        cipherText[cipherSize] = '\0';
        for (SIZE_T i = 0; i < cipherSize; ++i)
        {
            const CHAR ch = cipherText[i];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')))
            {
                outLog("符号表初始化失败：加密负载含非法十六进制字符（位置=%zu）；解决方案：使用匹配版本的 UnrealDbgDll.dll 重新生成负载。 ", i);
                status = STATUS_INVALID_PARAMETER;
                goto cleanup;
            }
        }
        DecryptData(cipherText, plainText);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
        outLog("符号表初始化失败：读取或解密用户负载触发异常，异常码=0x%08X；解决方案：检查调用进程指针和 DLL/驱动版本是否匹配。 ", status);
        goto cleanup;
    }

    {
        BOOLEAN authorized = FALSE;
        const SIZE_T elementCount = user.uSize / sizeof(RING3_VERIFY);
        for (SIZE_T i = 0; i < elementCount; ++i)
        {
            const PRING3_VERIFY info = reinterpret_cast<PRING3_VERIFY>(plainText + i * sizeof(RING3_VERIFY));
            if (info->key == 0x9dd14d00f5dd71bdull)
            {
                authorized = TRUE;
                break;
            }
        }
        if (!authorized)
        {
            outLog("符号表初始化失败：授权校验未通过；解决方案：使用当前版本的合法授权密钥。 ");
            status = STATUS_ACCESS_DENIED;
            goto cleanup;
        }
    }

    g_IsInitGlobalVariable = FALSE;
    if (!InitNtoskrnlSymbolsTable())
    {
        outLog("符号表初始化失败：ntoskrnl.exe 符号解析失败；解决方案：安装与当前 Win11 内核版本完全匹配的 PDB。 ");
        status = STATUS_NOT_FOUND;
        goto cleanup;
    }
    if (!InitWin32kbaseSymbolsTable())
    {
        outLog("符号表初始化失败：win32kbase.sys 符号解析失败；解决方案：安装匹配版本的 win32kbase PDB。 ");
        status = STATUS_NOT_FOUND;
        goto cleanup;
    }
    if (!InitWin32kfullSymbolsTable())
    {
        outLog("符号表初始化失败：win32kfull.sys 符号解析失败；解决方案：安装匹配版本的 win32kfull PDB。 ");
        status = STATUS_NOT_FOUND;
        goto cleanup;
    }
    if (!CheckFunctionPointers())
    {
        outLog("符号表初始化失败：一个或多个必需内核函数地址为空；解决方案：确认 PDB 与系统 Build 完全匹配，不要强行继续。 ");
        status = STATUS_NOT_SUPPORTED;
        goto cleanup;
    }

#ifdef DEBUG
    DumpOffsetAndFuncPtr();
#endif

    g_IsInitGlobalVariable = TRUE;
    if (!DispatchOffsetToHost())
    {
        outLog("符号表初始化失败：VMCALL 无法把偏移发送到 VT host；解决方案：先确认 VT_Driver 已成功进入 VMX root mode，并检查 Hyper-V/VBS 冲突。 ");
        g_IsInitGlobalVariable = FALSE;
        status = STATUS_DEVICE_NOT_READY;
        goto cleanup;
    }

    status = DbgkInitialize();
    if (!NT_SUCCESS(status))
    {
        outLog("符号表初始化失败：调试对象子系统初始化失败，状态=0x%08X；解决方案：检查符号版本和驱动依赖后重启重试。 ", status);
        g_IsInitGlobalVariable = FALSE;
        goto cleanup;
    }
    SetupEptHook();
    *output = 1998;
    outLog("符号表初始化完成：授权、三套符号、函数指针、VMCALL 和调试对象均已成功。 ");

cleanup:
    if (cipherText != nullptr) free_pool(cipherText);
    if (plainText != nullptr) free_pool(plainText);
    return status;
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
            outLog("为vt host初始化offset成功。.");
            return true;
        }
        else
        {
            outLog("为vt host初始化offset失败；原因：目标符号地址无效、状态不匹配或资源不足；解决方案：检查匹配版本的符号表、驱动状态和内存池后重试。.");
        }
    }
    return false;
}

//Dump对象偏移和函数指针
void DumpOffsetAndFuncPtr()
{
    //函数指针
    outLog("DbgkDebugObjectType  0x%p", DbgkDebugObjectType);
    outLog("PsFreezeProcess  0x%p", PsFreezeProcess);
    outLog("PsThawProcess  0x%p", PsThawProcess);
    outLog("Sys_NtCreateDebugObject  0x%p", Sys_NtCreateDebugObject);
    outLog("DbgkpSuspendProcess  0x%p", DbgkpSuspendProcess);
    outLog("Sys_DbgkCreateThread  0x%p", Sys_DbgkCreateThread);
    outLog("Sys_DbgkpQueueMessage  0x%p", Sys_DbgkpQueueMessage);
    outLog("DbgkpSectionToFileHandle  0x%p", DbgkpSectionToFileHandle);
    outLog("DbgkpSendApiMessage  0x%p", DbgkpSendApiMessage);
    outLog("Sys_DbgkMapViewOfSection  0x%p", Sys_DbgkMapViewOfSection);
    outLog("Sys_DbgkUnMapViewOfSection  0x%p", Sys_DbgkUnMapViewOfSection);
    outLog("DbgkpSuppressDbgMsg  0x%p", DbgkpSuppressDbgMsg);
    outLog("Sys_DbgkExitThread  0x%p", Sys_DbgkExitThread);
    outLog("PsSetProcessFaultInformation  0x%p", PsSetProcessFaultInformation);
    outLog("PsCaptureExceptionPort  0x%p", PsCaptureExceptionPort);
    outLog("DbgkpSendApiMessageLpc  0x%p", DbgkpSendApiMessageLpc);
    outLog("DbgkpSendErrorMessage  0x%p", DbgkpSendErrorMessage);
    outLog("Sys_DbgkForwardException  0x%p", Sys_DbgkForwardException);
    outLog("DbgkpPostFakeProcessCreateMessages  0x%p", DbgkpPostFakeProcessCreateMessages);
    outLog("Sys_NtDebugActiveProcess  0x%p", Sys_NtDebugActiveProcess);
    outLog("Sys_DbgkExitProcess  0x%p", Sys_DbgkExitProcess);
    outLog("Sys_PspExitThread  0x%p", Sys_PspExitThread);
    outLog("DbgkpWakeTarget  0x%p", DbgkpWakeTarget);
    outLog("Sys_NtDebugContinue  0x%p", Sys_NtDebugContinue);
    outLog("Sys_NtWaitForDebugEvent  0x%p", Sys_NtWaitForDebugEvent);
    outLog("ObDuplicateObject  0x%p", ObDuplicateObject);
    outLog("DbgkClearProcessDebugObject  0x%p", DbgkClearProcessDebugObject);
    outLog("Sys_NtRemoveProcessDebug  0x%p", Sys_NtRemoveProcessDebug);
    outLog("PsGetNextProcess  0x%p", PsGetNextProcess);
    outLog("DbgkpMarkProcessPeb  0x%p", DbgkpMarkProcessPeb);
    outLog("PsTerminateProcess  0x%p", PsTerminateProcess);
    outLog("ObCreateObjectType  0x%p", ObCreateObjectType);
    outLog("PsGetNextProcessThread  0x%p", PsGetNextProcessThread);
    outLog("DbgkpPostFakeThreadMessages  0x%p", DbgkpPostFakeThreadMessages);
    outLog("KiStackAttachProcess  0x%p", KiStackAttachProcess);
    outLog("KiUnstackDetachProcess  0x%p", KiUnstackDetachProcess);
    outLog("Sys_NtReadVirtualMemory  0x%p", Sys_NtReadVirtualMemory);
    outLog("Sys_NtWriteVirtualMemory  0x%p", Sys_NtWriteVirtualMemory);
    outLog("ZwProtectVirtualMemory  0x%p", ZwProtectVirtualMemory);
    outLog("Sys_NtProtectVirtualMemory  0x%p", Sys_NtProtectVirtualMemory);
    outLog("Sys_PspCreateThread  0x%p", Sys_PspCreateThread);
    outLog("Sys_NtCreateThreadEx  0x%p", Sys_NtCreateThreadEx);
    outLog("Sys_NtOpenProcess  0x%p", Sys_NtOpenProcess);
    outLog("DbgkpConvertKernelToUserStateChange  0x%p", DbgkpConvertKernelToUserStateChange);
    outLog("DbgkpOpenHandles  0x%p", DbgkpOpenHandles);
    outLog("KeCopyExceptionRecord  0x%p", KeCopyExceptionRecord);
    outLog("Sys_ObReferenceObjectByHandleWithTag  0x%p", Sys_ObReferenceObjectByHandleWithTag);
    outLog("Sys_ObReferenceObjectByHandle  0x%p", Sys_ObReferenceObjectByHandle);
    outLog("Sys_ObfDereferenceObjectWithTag  0x%p", Sys_ObfDereferenceObjectWithTag);
    outLog("Sys_ObfDereferenceObject  0x%p", Sys_ObfDereferenceObject);
    outLog("KiCheckForKernelApcDelivery  0x%p", KiCheckForKernelApcDelivery);
    outLog("KeEnterCriticalRegionThread  0x%p", KeEnterCriticalRegionThread);
    outLog("KeLeaveCriticalRegionThread  0x%p", KeLeaveCriticalRegionThread);
    outLog("Sys_MmCopyVirtualMemory  0x%p", Sys_MmCopyVirtualMemory);
    outLog("Sys_PspCreateUserContext  0x%p", Sys_PspCreateUserContext);
    outLog("Sys_PspCallThreadNotifyRoutines  0x%p", Sys_PspCallThreadNotifyRoutines);
    outLog("Sys_PspAllocateThread  0x%p", Sys_PspAllocateThread);
    outLog("Sys_ObpReferenceObjectByHandleWithTag  0x%p", Sys_ObpReferenceObjectByHandleWithTag);
    outLog("Sys_MiObtainReferencedVadEx  0x%p", Sys_MiObtainReferencedVadEx);
    outLog("Sys_MmProtectVirtualMemory  0x%p", Sys_MmProtectVirtualMemory);
    outLog("Sys_NtGetContextThread  0x%p", Sys_NtGetContextThread);
    outLog("Sys_NtSetContextThread  0x%p", Sys_NtSetContextThread);
    outLog("ZwGetContextThread  0x%p", ZwGetContextThread);
    outLog("PspGetContextThreadInternal  0x%p", PspGetContextThreadInternal);
    outLog("Sys_KiDispatchException  0x%p", Sys_KiDispatchException);
    outLog("Sys_KeStackAttachProcess  0x%p", Sys_KeStackAttachProcess);
    outLog("Sys_KiStackAttachProcess  0x%p", Sys_KiStackAttachProcess);
    outLog("Sys_NtSetInformationDebugObject  0x%p", Sys_NtSetInformationDebugObject);
    outLog("Sys_NtTerminateProcess  0x%p", Sys_NtTerminateProcess);
    outLog("Sys_NtSuspendThread  0x%p", Sys_NtSuspendThread);
    outLog("Sys_NtResumeThread  0x%p", Sys_NtResumeThread);
    outLog("Sys_NtQueryInformationThread  0x%p", Sys_NtQueryInformationThread);

    outLog("\n");

    //对象偏移
    outLog("eprocess_offset::Pcb  0x%x", eprocess_offset::Pcb);
    outLog("eprocess_offset::DebugPort  0x%x", eprocess_offset::DebugPort);
    outLog("eprocess_offset::ImageFileName  0x%x", eprocess_offset::ImageFileName);
    outLog("eprocess_offset::WoW64Process  0x%x", eprocess_offset::WoW64Process);
    outLog("eprocess_offset::RundownProtect  0x%x", eprocess_offset::RundownProtect);
    outLog("eprocess_offset::ExitTime  0x%x", eprocess_offset::ExitTime);
    outLog("eprocess_offset::Machine  0x%x", eprocess_offset::Machine);
    outLog("kprocess_offset::DirectoryTableBase  0x%x", kprocess_offset::DirectoryTableBase);
    outLog("ethread_offset::Tcb  0x%x", ethread_offset::Tcb);
    outLog("ethread_offset::CrossThreadFlags  0x%x", ethread_offset::CrossThreadFlags);
    outLog("ethread_offset::Cid  0x%x", ethread_offset::Cid);
    outLog("ethread_offset::RundownProtect  0x%x", ethread_offset::RundownProtect);
    outLog("kthread_offset::ApcState  0x%x", kthread_offset::ApcState);
    outLog("kthread_offset::PreviousMode  0x%x", kthread_offset::PreviousMode);
    outLog("kthread_offset::Teb  0x%x", kthread_offset::Teb);
    outLog("kthread_offset::Process  0x%x", kthread_offset::Process);
    outLog("kthread_offset::KernelApcDisable  0x%x", kthread_offset::KernelApcDisable);
    outLog("kthread_offset::MiscFlags  0x%x", kthread_offset::MiscFlags);
    outLog("kthread_offset::TrapFrame  0x%x", kthread_offset::TrapFrame);
    outLog("kthread_offset::SuspendCount  0x%x", kthread_offset::SuspendCount);
    outLog("kapc_state_offset::Process  0x%x", kapc_state_offset::Process);
    outLog("image_nt_headers64_offset::FileHeader  0x%x", image_nt_headers64_offset::FileHeader);
    outLog("image_nt_headers64_offset::OptionalHeader  0x%x", image_nt_headers64_offset::OptionalHeader);
    outLog("image_nt_headers64_offset::Signature  0x%x", image_nt_headers64_offset::Signature);
    outLog("image_file_header_offset::PointerToSymbolTable  0x%x", image_file_header_offset::PointerToSymbolTable);
    outLog("image_file_header_offset::NumberOfSymbols  0x%x", image_file_header_offset::NumberOfSymbols);
    outLog("mmvad_offset::Core  0x%x", mmvad_offset::Core);
    outLog("mmvad_short_offset::LongFlags  0x%x", mmvad_short_offset::LongFlags);
}


#undef CHECK_FUNC_PTR
#define CHECK_FUNC_PTR(ptr) do { \
    if ((ptr) == nullptr) { \
        outLog("必需符号 %s 地址为空；解决方案：检查对应 PDB 是否与当前系统 Build 匹配。 ", #ptr); \
        allValid = FALSE; \
    } \
} while (0)

BOOLEAN CheckFunctionPointers()
{
    BOOLEAN allValid = TRUE;
    //ntos

    CHECK_FUNC_PTR(DbgkDebugObjectType);
    CHECK_FUNC_PTR(PspNotifyEnableMask);
    CHECK_FUNC_PTR(PerfGlobalGroupMask);
    CHECK_FUNC_PTR(PspActiveProcessLock);
    CHECK_FUNC_PTR(PspProcessSequenceNumber);
    CHECK_FUNC_PTR(PsActiveProcessHead);

    CHECK_FUNC_PTR(PsFreezeProcess);
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
    CHECK_FUNC_PTR(MiReferenceControlAreaFileWithTag);
    CHECK_FUNC_PTR(Sys_PspInsertProcess);
    CHECK_FUNC_PTR(SeAuditingWithTokenForSubcategory);
    CHECK_FUNC_PTR(SeAuditProcessCreation);
    CHECK_FUNC_PTR(PspImplicitAssignProcessToJob);
    CHECK_FUNC_PTR(PspInheritSyscallProvider);
    CHECK_FUNC_PTR(PspUnlockProcessListExclusive);
    CHECK_FUNC_PTR(ObCheckRefTraceProcess);
    CHECK_FUNC_PTR(PspValidateJobAffinityState);
    CHECK_FUNC_PTR(DbgkCopyProcessDebugPort);
    CHECK_FUNC_PTR(SeCreateAccessStateEx);
    CHECK_FUNC_PTR(ObInsertObjectEx);
    CHECK_FUNC_PTR(SepDeleteAccessState);
    CHECK_FUNC_PTR(DbgkSendSystemDllMessages);
    CHECK_FUNC_PTR(Sys_DbgkOpenProcessDebugPort);

    //win32kbase
    CHECK_FUNC_PTR(Sys_ValidateHwnd);

    //win32kfull
    CHECK_FUNC_PTR(Sys_NtUserFindWindowEx);
    CHECK_FUNC_PTR(Sys_NtUserWindowFromPoint);

    return allValid;
}
