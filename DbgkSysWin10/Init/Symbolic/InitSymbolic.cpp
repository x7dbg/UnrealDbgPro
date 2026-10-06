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
        outLog("符号表初始化失败：ntoskrnl.exe 符号解析失败；解决方案：安装与当前 Windows 内核版本匹配的 PDB。 ");
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
        outLog("符号表初始化失败：一个或多个必需内核函数地址为空；解决方案：确认 PDB 与当前系统 Build 完全匹配，不要强行继续。 ");
        status = STATUS_NOT_SUPPORTED;
        goto cleanup;
    }
    g_IsInitGlobalVariable = TRUE;
    if (!DispatchOffsetToHost())
    {
        outLog("符号表初始化失败：VMCALL 无法把偏移发送到 VT host；解决方案：确认 VT_Driver 已成功进入 VMX root mode，并检查 Hyper-V/VBS 冲突。 ");
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

#undef CHECK_FUNC_PTR
#define CHECK_FUNC_PTR(ptr) do { \
    if ((ptr) == nullptr) { \
        outLog("必需符号 %s 地址为空；解决方案：检查对应 PDB 是否与当前系统 Build 匹配。 ", #ptr); \
        allValid = FALSE; \
    } \
} while (0)

BOOLEAN CheckFunctionPointers()
{
    //ntos
    BOOLEAN allValid = TRUE;
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

    return allValid;
}
