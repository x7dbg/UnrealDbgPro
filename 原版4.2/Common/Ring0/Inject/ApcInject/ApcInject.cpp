#include <ntifs.h>
#include <ntstrsafe.h>
#include "../../../Common/Ring0/SymbolicAccess/Utils/Log.h"
#include "../../../Common/Ring0/PE/PE_struct.h"
#include "ApcInject.h"

PVOID g_KernelBase = NULL;
ULONG g_KernelSize = 0;
PSYSTEM_SERVICE_DESCRIPTOR_TABLE g_SSDT = NULL;
// OS Dependant data
DYNAMIC_DATA dynData;

/// <summary>
/// Check if thread does not satisfy APC requirements
/// </summary>
/// <param name="pThread">Thread to check</param>
/// /// <param name="isWow64">If TRUE - check Wow64 TEB</param>
/// <returns>If TRUE - BBLookupProcessThread should skip thread</returns>
BOOLEAN BBSkipThread(IN PETHREAD pThread, IN BOOLEAN isWow64)
{
    PUCHAR pTeb64 = (PUCHAR)PsGetThreadTeb(pThread);
    if (!pTeb64)
        return TRUE;

    // Skip GUI treads. APC to GUI thread causes ZwUserGetMessage to fail
    // TEB64 + 0x78  = Win32ThreadInfo
    if (*(PULONG64)(pTeb64 + 0x78) != 0)
        return TRUE;

    // Skip threads with no ActivationContext
    // Skip threads with no TLS pointer
    if (isWow64)
    {
        PUCHAR pTeb32 = pTeb64 + 0x2000;

        // TEB32 + 0x1A8 = ActivationContextStackPointer
        if (*(PULONG32)(pTeb32 + 0x1A8) == 0)
            return TRUE;

        // TEB64 + 0x2C = ThreadLocalStoragePointer
        if (*(PULONG32)(pTeb32 + 0x2C) == 0)
            return TRUE;
    }
    else
    {
        // TEB64 + 0x2C8 = ActivationContextStackPointer
        if (*(PULONG64)(pTeb64 + 0x2C8) == 0)
            return TRUE;

        // TEB64 + 0x58 = ThreadLocalStoragePointer
        if (*(PULONG64)(pTeb64 + 0x58) == 0)
            return TRUE;
    }

    return FALSE;
}


/// <summary>
/// Find first thread of the target process
/// </summary>
/// <param name="pProcess">Target process</param>
/// <param name="ppThread">Found thread. Thread object reference count is increased by 1</param>
/// <returns>Status code</returns>
NTSTATUS BBLookupProcessThread(IN PEPROCESS pProcess, OUT PETHREAD* ppThread)
{
    NTSTATUS status = STATUS_SUCCESS;
    HANDLE pid = PsGetProcessId(pProcess);
    PVOID pBuf = ExAllocatePoolWithTag(NonPagedPool, 1024 * 1024, BB_POOL_TAG);
    PSYSTEM_PROCESS_INFO pInfo = (PSYSTEM_PROCESS_INFO)pBuf;

    ASSERT(ppThread != NULL);
    if (ppThread == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!pInfo)
    {
        outLog("BBLookupProcessThread 分配内存失败");
        return STATUS_NO_MEMORY;
    }

    // Get the process thread list
    status = ZwQuerySystemInformation(SystemProcessInformation, pInfo, 1024 * 1024, NULL);
    if (!NT_SUCCESS(status))
    {
        ExFreePoolWithTag(pBuf, BB_POOL_TAG);
        return status;
    }

    // Find target thread
    if (NT_SUCCESS(status))
    {
        status = STATUS_NOT_FOUND;
        for (;;)
        {
            if (pInfo->UniqueProcessId == pid)
            {
                status = STATUS_SUCCESS;
                break;
            }
            else if (pInfo->NextEntryOffset)
                pInfo = (PSYSTEM_PROCESS_INFO)((PUCHAR)pInfo + pInfo->NextEntryOffset);
            else
                break;
        }
    }

    BOOLEAN wow64 = PsGetProcessWow64Process(pProcess) != NULL;

    // Reference target thread
    if (NT_SUCCESS(status))
    {
        status = STATUS_NOT_FOUND;

        // Get first thread
        for (ULONG i = 0; i < pInfo->NumberOfThreads; i++)
        {
            // Skip current thread
            if (/*pInfo->Threads[i].WaitReason == Suspended ||
                 pInfo->Threads[i].ThreadState == 5 ||*/
                pInfo->Threads[i].ClientId.UniqueThread == PsGetCurrentThreadId())
            {
                continue;
            }

            status = PsLookupThreadByThreadId(pInfo->Threads[i].ClientId.UniqueThread, ppThread);

            // Skip specific threads
            if (*ppThread && BBSkipThread(*ppThread, wow64))
            {
                ObDereferenceObject(*ppThread);
                *ppThread = NULL;
                continue;
            }

            break;
        }
    }
    else
    {
        outLog("查找目标线程失败");
    }

    if (pBuf)
        ExFreePoolWithTag(pBuf, BB_POOL_TAG);

    // No suitable thread
    if (!*ppThread)
        status = STATUS_NOT_FOUND;

    return status;
}

/// <summary>
/// Inject dll using APC
/// Must be running in target process context
/// </summary>
/// <param name="pUserBuf">Injcetion code</param>
/// <param name="pProcess">Target process</param>
/// <param name="initRVA">Init routine RVA</param>
/// <param name="InitArg">Init routine argument</param>
/// <returns>Status code</returns>
NTSTATUS BBApcInject(IN PINJECT_BUFFER pUserBuf, IN PEPROCESS pProcess, IN ULONG initRVA, IN PCWCHAR InitArg)
{
    NTSTATUS status = STATUS_SUCCESS;
    PETHREAD pThread = NULL;

    // Get suitable thread
    status = BBLookupProcessThread(pProcess, &pThread);

    if (NT_SUCCESS(status))
    {
        //调用LdrLoadDll
        status = BBQueueUserApc(pThread, pUserBuf->code, NULL, NULL, NULL, TRUE);

        // Wait for completion
        if (NT_SUCCESS(status))
        {
            outLog("BBApcInject 注入成功");
        }
    }
    else
    {
        outLog("BBApcInject 注入失败");
    }

    if (pThread)
        ObDereferenceObject(pThread);

    return status;
}

//使用apc注入远程线程
NTSTATUS BBApcInject2(IN PVOID pUserFunc, IN PEPROCESS pProcess, IN ULONG initRVA, IN PCWCHAR InitArg)
{
    NTSTATUS status = STATUS_SUCCESS;
    PETHREAD pThread = NULL;

    // Get suitable thread
    status = BBLookupProcessThread(pProcess, &pThread);

    if (NT_SUCCESS(status))
    {
        //调用LdrLoadDll
        status = BBQueueUserApc(pThread, pUserFunc, NULL, NULL, NULL, TRUE);

        // Wait for completion
        if (NT_SUCCESS(status))
        {
            outLog("BBApcInject 注入成功");
        }
    }
    else
    {
        outLog("BBApcInject 注入失败");
    }

    if (pThread)
        ObDereferenceObject(pThread);

    return status;
}


/// <summary>
/// Get module base address by name
/// </summary>
/// <param name="pProcess">Target process</param>
/// <param name="ModuleName">Nodule name to search for</param>
/// <param name="isWow64">If TRUE - search in 32-bit PEB</param>
/// <returns>Found address, NULL if not found</returns>
PVOID BBGetUserModule(IN PEPROCESS pProcess, IN PUNICODE_STRING ModuleName, IN BOOLEAN isWow64)
{
    ASSERT(pProcess != NULL);
    if (pProcess == NULL)
        return NULL;

    // Protect from UserMode AV
    __try
    {
        LARGE_INTEGER time = { 0 };
        time.QuadPart = -250ll * 10 * 1000;     // 250 msec.

        // Wow64 process
        if (isWow64)
        {
            PPEB32 pPeb32 = (PPEB32)PsGetProcessWow64Process(pProcess);
            if (pPeb32 == NULL)
            {
                outLog("获取Wow64进程的PEB失败");
                return NULL;
            }

            // Wait for loader a bit
            for (INT i = 0; !pPeb32->Ldr && i < 10; i++)
            {                
                KeDelayExecutionThread(KernelMode, TRUE, &time);
            }

            // Still no loader
            if (!pPeb32->Ldr)
            {
                outLog("进程的ldr为空");
                return NULL;
            }

            // Search in InLoadOrderModuleList
            for (PLIST_ENTRY32 pListEntry = (PLIST_ENTRY32)((PPEB_LDR_DATA32)pPeb32->Ldr)->InLoadOrderModuleList.Flink;
                pListEntry != &((PPEB_LDR_DATA32)pPeb32->Ldr)->InLoadOrderModuleList;
                pListEntry = (PLIST_ENTRY32)pListEntry->Flink)
            {
                UNICODE_STRING ustr;
                PLDR_DATA_TABLE_ENTRY32 pEntry = CONTAINING_RECORD(pListEntry, LDR_DATA_TABLE_ENTRY32, InLoadOrderLinks);

                RtlUnicodeStringInit(&ustr, (PWCH)pEntry->BaseDllName.Buffer);

                if (RtlCompareUnicodeString(&ustr, ModuleName, TRUE) == 0)
                    return (PVOID)pEntry->DllBase;
            }
        }
        else
        {
            PPEB pPeb = PsGetProcessPeb(pProcess);
            if (!pPeb)
            {
                outLog("获取进程PEB失败");
                return NULL;
            }

            // Wait for loader a bit
            for (INT i = 0; !pPeb->Ldr && i < 10; i++)
            {
                KeDelayExecutionThread(KernelMode, TRUE, &time);
            }

            // Still no loader
            if (!pPeb->Ldr)
            {
                outLog("进程ldr为空");
                return NULL;
            }

            // Search in InLoadOrderModuleList
            for (PLIST_ENTRY pListEntry = pPeb->Ldr->InLoadOrderModuleList.Flink;
                pListEntry != &pPeb->Ldr->InLoadOrderModuleList;
                pListEntry = pListEntry->Flink)
            {
                PLDR_DATA_TABLE_ENTRY pEntry = CONTAINING_RECORD(pListEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
                if (RtlCompareUnicodeString(&pEntry->BaseDllName, ModuleName, TRUE) == 0)
                    return pEntry->DllBase;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outLog("BBGetUserModule Exception, Code: 0x%X\n", GetExceptionCode());
    }

    return NULL;
}


/// <summary>
/// Get file name from full path
/// </summary>
/// <param name="path">Path.</param>
/// <param name="name">Resulting name</param>
/// <returns>Status code</returns>
NTSTATUS BBStripPath(IN PUNICODE_STRING path, OUT PUNICODE_STRING name)
{
    ASSERT(path != NULL && name);
    if (path == NULL || name == NULL)
        return STATUS_INVALID_PARAMETER;

    // Empty string
    if (path->Length < 2)
    {
        *name = *path;
        return STATUS_NOT_FOUND;
    }

    for (USHORT i = (path->Length / sizeof(WCHAR)) - 1; i != 0; i--)
    {
        if (path->Buffer[i] == L'\\' || path->Buffer[i] == L'/')
        {
            name->Buffer = &path->Buffer[i + 1];
            name->Length = name->MaximumLength = path->Length - (i + 1) * sizeof(WCHAR);
            return STATUS_SUCCESS;
        }
    }

    *name = *path;
    return STATUS_NOT_FOUND;
}


/// <summary>
/// Search for substring
/// </summary>
/// <param name="source">Source string</param>
/// <param name="target">Target string</param>
/// <param name="CaseInSensitive">Case insensitive search</param>
/// <returns>Found position or -1 if not found</returns>
LONG BBSafeSearchString(IN PUNICODE_STRING source, IN PUNICODE_STRING target, IN BOOLEAN CaseInSensitive)
{
    ASSERT(source != NULL && target != NULL);
    if (source == NULL || target == NULL || source->Buffer == NULL || target->Buffer == NULL)
        return STATUS_INVALID_PARAMETER;

    // Size mismatch
    if (source->Length < target->Length)
        return -1;

    USHORT diff = source->Length - target->Length;
    for (USHORT i = 0; i <= (diff / sizeof(WCHAR)); i++)
    {
        if (RtlCompareUnicodeStrings(
            source->Buffer + i,
            target->Length / sizeof(WCHAR),
            target->Buffer,
            target->Length / sizeof(WCHAR),
            CaseInSensitive
        ) == 0)
        {
            return i;
        }
    }

    return -1;
}



/// <summary>
/// Allocate and copy string
/// </summary>
/// <param name="result">Resulting string</param>
/// <param name="source">Source string</param>
/// <returns>Status code</returns>
NTSTATUS BBSafeInitString(OUT PUNICODE_STRING result, IN PUNICODE_STRING source)
{
    ASSERT(result != NULL && source != NULL);
    if (result == NULL || source == NULL || source->Buffer == NULL)
        return STATUS_INVALID_PARAMETER;

    // No data to copy
    if (source->Length == 0)
    {
        result->Length = result->MaximumLength = 0;
        result->Buffer = NULL;
        return STATUS_SUCCESS;
    }

    result->Buffer = (PWCH)ExAllocatePoolWithTag(PagedPool, source->MaximumLength, BB_POOL_TAG);
    result->Length = source->Length;
    result->MaximumLength = source->MaximumLength;

    memcpy(result->Buffer, source->Buffer, source->Length);

    return STATUS_SUCCESS;
}

/// <summary>
/// Try to resolve image via API SET map
/// </summary>
/// <param name="pProcess">Target process. Must be run in the context of this process</param>
/// <param name="name">Name to resolve</param>
/// <param name="baseImage">Parent image name</param>
/// <param name="resolved">Resolved name if any</param>
/// <returns>Status code</returns>
NTSTATUS BBResolveApiSet(
    IN PEPROCESS pProcess,
    IN PUNICODE_STRING name,
    IN PUNICODE_STRING baseImage,
    OUT PUNICODE_STRING resolved
)
{
    NTSTATUS status = STATUS_NOT_FOUND;
    PPEB32 pPeb32 = (PPEB32)PsGetProcessWow64Process(pProcess);
    PPEB pPeb = PsGetProcessPeb(pProcess);
    PAPISET_NAMESPACE_ARRAY pApiSetMap = (PAPISET_NAMESPACE_ARRAY)(pPeb32 != NULL ? (PVOID)pPeb32->ApiSetMap : pPeb->ApiSetMap);

    // Invalid name
    if (name == NULL || name->Length < 4 * sizeof(WCHAR) || name->Buffer == NULL ||
        (memcmp(name->Buffer, L"api-", 4) != 0 && memcmp(name->Buffer, L"ext-", 4) != 0))
        return STATUS_NOT_FOUND;

    // Iterate api set map
    for (ULONG i = 0; i < pApiSetMap->Count; i++)
    {
        PAPISET_NAMESPACE_ENTRY pDescriptor = NULL;
        PAPISET_VALUE_ARRAY pHostArray = NULL;
        wchar_t apiNameBuf[255] = { 0 };
        UNICODE_STRING apiName = { 0 };

#ifdef _WIN10_
        pDescriptor = (PAPISET_NAMESPACE_ENTRY)((PUCHAR)pApiSetMap + pApiSetMap->End + i * sizeof(API_SET_NAMESPACE_ENTRY_10));
        pHostArray = (PAPISET_VALUE_ARRAY)((PUCHAR)pApiSetMap + pApiSetMap->Start + sizeof(API_SET_VALUE_ARRAY_10) * pDescriptor->Size);

        memcpy(apiNameBuf, (PUCHAR)pApiSetMap + pHostArray->NameOffset, pHostArray->NameLength);
#else
        pDescriptor = pApiSetMap->Array + i;
        memcpy(apiNameBuf, (PUCHAR)pApiSetMap + pDescriptor->NameOffset, pDescriptor->NameLength);
#endif   
        RtlUnicodeStringInit(&apiName, apiNameBuf);

        // Check if this is a target api
        if (BBSafeSearchString(name, &apiName, TRUE) >= 0)
        {
            PAPISET_VALUE_ENTRY pHost = NULL;
            wchar_t apiHostNameBuf[255] = { 0 };
            UNICODE_STRING apiHostName = { 0 };

#ifdef _WIN10_
            pHost = (PAPISET_VALUE_ENTRY)((PUCHAR)pApiSetMap + pHostArray->DataOffset);
#else
            pHostArray = (PAPISET_VALUE_ARRAY)((PUCHAR)pApiSetMap + pDescriptor->DataOffset);
            pHost = pHostArray->Array;
#endif
            // Sanity check
            if (pHostArray->Count < 1)
                return STATUS_NOT_FOUND;

            memcpy(apiHostNameBuf, (PUCHAR)pApiSetMap + pHost->ValueOffset, pHost->ValueLength);
            RtlUnicodeStringInit(&apiHostName, apiHostNameBuf);

            // No base name redirection
            if (pHostArray->Count == 1 || baseImage == NULL || baseImage->Buffer[0] == 0)
            {
                BBSafeInitString(resolved, &apiHostName);
                return STATUS_SUCCESS;
            }
            // Redirect accordingly to base name
            else
            {
                UNICODE_STRING baseImageName = { 0 };
                BBStripPath(baseImage, &baseImageName);

                if (RtlCompareUnicodeString(&apiHostName, &baseImageName, TRUE) == 0)
                {
                    memset(apiHostNameBuf, 0, sizeof(apiHostNameBuf));
                    memcpy(apiHostNameBuf, (PUCHAR)pApiSetMap + pHost[1].ValueOffset, pHost[1].ValueLength);
                    RtlCreateUnicodeString(resolved, apiHostNameBuf);
                    return STATUS_SUCCESS;
                }
                else
                {
                    BBSafeInitString(resolved, &apiHostName);
                    return STATUS_SUCCESS;
                }
            }
        }
    }

    return status;
}


/// <summary>
/// Allocate new Unicode string from Paged pool
/// </summary>
/// <param name="result">Resulting string</param>
/// <param name="size">Buffer size in bytes to alloacate</param>
/// <returns>Status code</returns>
NTSTATUS BBSafeAllocateString(OUT PUNICODE_STRING result, IN USHORT size)
{
    ASSERT(result != NULL);
    if (result == NULL || size == 0)
        return STATUS_INVALID_PARAMETER;

    result->Buffer = (PWCH)ExAllocatePoolWithTag(PagedPool, size, BB_POOL_TAG);
    result->Length = 0;
    result->MaximumLength = size;

    if (result->Buffer)
        RtlZeroMemory(result->Buffer, size);
    else
        return STATUS_NO_MEMORY;

    return STATUS_SUCCESS;
}


/// <summary>
/// Get directory path name from full path
/// </summary>
/// <param name="path">Path</param>
/// <param name="name">Resulting directory path</param>
/// <returns>Status code</returns>
NTSTATUS BBStripFilename(IN PUNICODE_STRING path, OUT PUNICODE_STRING dir)
{
    ASSERT(path != NULL && dir);
    if (path == NULL || dir == NULL)
        return STATUS_INVALID_PARAMETER;

    // Empty string
    if (path->Length < 2)
    {
        *dir = *path;
        return STATUS_NOT_FOUND;
    }

    for (USHORT i = (path->Length / sizeof(WCHAR)) - 1; i != 0; i--)
    {
        if (path->Buffer[i] == L'\\' || path->Buffer[i] == L'/')
        {
            dir->Buffer = path->Buffer;
            dir->Length = dir->MaximumLength = i * sizeof(WCHAR);
            return STATUS_SUCCESS;
        }
    }

    *dir = *path;
    return STATUS_NOT_FOUND;
}


/// <summary>
/// Check if file exists
/// </summary>
/// <param name="path">Fully qualifid path to a file</param>
/// <returns>Status code</returns>
NTSTATUS BBFileExists(IN PUNICODE_STRING path)
{
    HANDLE hFile = NULL;
    IO_STATUS_BLOCK statusBlock = { 0 };
    OBJECT_ATTRIBUTES obAttr = { 0 };
    InitializeObjectAttributes(&obAttr, path, OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS status = ZwCreateFile(
        &hFile, FILE_READ_DATA | SYNCHRONIZE, &obAttr,
        &statusBlock, NULL, FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0
    );

    if (NT_SUCCESS(status))
        ZwClose(hFile);

    return status;
}

/// <summary>
/// Resolve image name to fully qualified path
/// </summary>
/// <param name="pContext">Loader context</param>
/// <param name="pProcess">Target process. Must be running in the context of this process</param>
/// <param name="flags">Flags</param>
/// <param name="path">Image name to resolve</param>
/// <param name="baseImage">Base image name for API SET translation</param>
/// <param name="resolved">Resolved image path</param>
/// <returns>Status code</returns>
NTSTATUS BBResolveImagePath(
    IN PMMAP_CONTEXT pContext,
    IN PEPROCESS pProcess,
    IN ResolveFlags flags,
    IN PUNICODE_STRING path,
    IN PUNICODE_STRING baseImage,
    OUT PUNICODE_STRING resolved
)
{
    NTSTATUS status = STATUS_SUCCESS;
    UNICODE_STRING pathLow = { 0 };
    UNICODE_STRING filename = { 0 };
    UNICODE_STRING fullResolved = { 0 };

    UNREFERENCED_PARAMETER(baseImage);

    ASSERT(pProcess != NULL && path != NULL && resolved != NULL);
    if (pProcess == NULL || path == NULL || resolved == NULL)
    {
        outLog("BBResolveImagePath 参数无效");
        return STATUS_INVALID_PARAMETER;
    }

    RtlDowncaseUnicodeString(&pathLow, path, TRUE);
    BBStripPath(&pathLow, &filename);

    // API Schema
    if (NT_SUCCESS(BBResolveApiSet(pProcess, &filename, baseImage, resolved)))
    {
        BBSafeAllocateString(&fullResolved, 512);

        // Perpend system directory
        if (PsGetProcessWow64Process(pProcess) != NULL)
            RtlUnicodeStringCatString(&fullResolved, L"\\SystemRoot\\syswow64\\");
        else
            RtlUnicodeStringCatString(&fullResolved, L"\\SystemRoot\\system32\\");

        RtlUnicodeStringCat(&fullResolved, resolved);
        RtlFreeUnicodeString(resolved);
        RtlFreeUnicodeString(&pathLow);

        //DPRINT( "BlackBone: %s: Resolved image '%wZ' to '%wZ' by ApiSetSchema\n", __FUNCTION__, path, fullResolved );

        *resolved = fullResolved;
        return STATUS_SUCCESS;
    }

    // Api schema only
    if (flags & KApiShemaOnly)
        goto skip;

    if (flags & KSkipSxS)
        goto SkipSxS;

    // SxS
    status = BBResolveSxS(pContext, &filename, resolved);
    if (pContext && NT_SUCCESS(status))
    {
        BBSafeAllocateString(&fullResolved, 1024);
        RtlUnicodeStringCatString(&fullResolved, L"\\??\\");
        RtlUnicodeStringCat(&fullResolved, resolved);

        RtlFreeUnicodeString(resolved);
        RtlFreeUnicodeString(&pathLow);

        *resolved = fullResolved;
        return STATUS_SUCCESS;
    }
    else if (status == STATUS_UNHANDLED_EXCEPTION)
    {
        *resolved = pathLow;
        return status;
    }
    else
        status = STATUS_SUCCESS;

SkipSxS:
    BBSafeAllocateString(&fullResolved, 0x400);

    //
    // Executable directory
    //
    ULONG bytes = 0;
    if (NT_SUCCESS(ZwQueryInformationProcess(ZwCurrentProcess(), ProcessImageFileName, fullResolved.Buffer + 0x100, 0x200, &bytes)))
    {
        PUNICODE_STRING pPath = (PUNICODE_STRING)(fullResolved.Buffer + 0x100);
        UNICODE_STRING parentDir = { 0 };
        BBStripFilename(pPath, &parentDir);

        RtlCopyUnicodeString(&fullResolved, &parentDir);
        RtlUnicodeStringCatString(&fullResolved, L"\\");
        RtlUnicodeStringCat(&fullResolved, &filename);

        if (NT_SUCCESS(BBFileExists(&fullResolved)))
        {
            RtlFreeUnicodeString(resolved);
            RtlFreeUnicodeString(&pathLow);

            *resolved = fullResolved;
            return STATUS_SUCCESS;
        }
    }

    fullResolved.Length = 0;
    RtlZeroMemory(fullResolved.Buffer, 0x400);

    //
    // System directory
    //
    if (PsGetProcessWow64Process(pProcess) != NULL)
        RtlUnicodeStringCatString(&fullResolved, L"\\SystemRoot\\SysWOW64\\");
    else
        RtlUnicodeStringCatString(&fullResolved, L"\\SystemRoot\\System32\\");

    RtlUnicodeStringCat(&fullResolved, &filename);
    if (NT_SUCCESS(BBFileExists(&fullResolved)))
    {
        RtlFreeUnicodeString(resolved);
        RtlFreeUnicodeString(&pathLow);

        *resolved = fullResolved;
        return STATUS_SUCCESS;
    }

    RtlFreeUnicodeString(&fullResolved);

    // Nothing found
skip:
    *resolved = pathLow;
    return status;
}


ULONG GenPrologue32(IN PUCHAR pBuf)
{
    *pBuf = 0x55;
    *(PUSHORT)(pBuf + 1) = 0xE589;

    return 3;
}

ULONG GenEpilogue32(IN PUCHAR pBuf, IN INT retSize)
{
    *(PUSHORT)pBuf = 0xEC89;
    *(pBuf + 2) = 0x5D;
    *(pBuf + 3) = 0xC2;
    *(PUSHORT)(pBuf + 4) = (USHORT)retSize;

    return 6;
}

ULONG GenCall32(IN PUCHAR pBuf, IN PVOID pFn, IN INT argc, ...)
{
    va_list vl;
    va_start(vl, argc);
    ULONG res = GenCall32V(pBuf, pFn, argc, vl);
    va_end(vl);

    return res;
}

ULONG GenCall32V(IN PUCHAR pBuf, IN PVOID pFn, IN INT argc, IN va_list vl)
{
    ULONG ofst = 0;

    PULONG pArgBuf = (PULONG)ExAllocatePoolWithTag(PagedPool, argc * sizeof(ULONG), BB_POOL_TAG);

    // cast args
    for (INT i = 0; i < argc; i++)
    {
        PVOID arg = va_arg(vl, PVOID);
        pArgBuf[i] = (ULONG)(ULONG_PTR)arg;
    }

    // push args
    for (INT i = argc - 1; i >= 0; i--)
    {
        *(PUSHORT)(pBuf + ofst) = 0x68;                 // push arg
        *(PULONG)(pBuf + ofst + 1) = pArgBuf[i];        //
        ofst += 5;
    }

    *(PUCHAR)(pBuf + ofst) = 0xB8;                      // mov eax, pFn
    *(PULONG)(pBuf + ofst + 1) = (ULONG)(ULONG_PTR)pFn; //
    ofst += 5;

    *(PUSHORT)(pBuf + ofst) = 0xD0FF;                   // call eax
    ofst += 2;

    ExFreePoolWithTag(pArgBuf, BB_POOL_TAG);

    return ofst;
}

ULONG GenSync32(IN PUCHAR pBuf, IN PNTSTATUS pStatus, IN PVOID pSetEvent, IN HANDLE hEvent)
{
    ULONG ofst = 0;

    *(PUCHAR)(pBuf + ofst) = 0xA3;                  // mov [pStatus], eax
    *(PVOID*)(pBuf + ofst + 1) = pStatus;           //
    ofst += 5;

    *(PUSHORT)(pBuf + ofst) = 0x006A;               // push FALSE
    ofst += 2;

    *(PUCHAR)(pBuf + ofst) = 0x68;                  // push hEvent
    *(PULONG)(pBuf + ofst + 1) = (ULONG)(ULONG_PTR)hEvent;  //
    ofst += 5;

    *(PUCHAR)(pBuf + ofst) = 0xB8;                  // mov eax, pSetEvent
    *(PULONG)(pBuf + ofst + 1) = (ULONG)(ULONG_PTR)pSetEvent;//
    ofst += 5;

    *(PUSHORT)(pBuf + ofst) = 0xD0FF;               // call eax
    ofst += 2;

    return ofst;
}



ULONG GenPrologue64(IN PUCHAR pBuf)
{
    *(PULONG)(pBuf + 0) = 0x244C8948;       // mov [rsp + 0x08], rcx
    *(PUCHAR)(pBuf + 4) = 0x8;              // 
    *(PULONG)(pBuf + 5) = 0x24548948;       // mov [rsp + 0x10], rdx
    *(PUCHAR)(pBuf + 9) = 0x10;             // 
    *(PULONG)(pBuf + 10) = 0x2444894C;      // mov [rsp + 0x18], r8
    *(PUCHAR)(pBuf + 14) = 0x18;            // 
    *(PULONG)(pBuf + 15) = 0x244C894C;      // mov [rsp + 0x20], r9
    *(PUCHAR)(pBuf + 19) = 0x20;            // 
    return 20;
}

ULONG GenEpilogue64(IN PUCHAR pBuf, IN INT retSize)
{
    UNREFERENCED_PARAMETER(retSize);

    *(PULONG)(pBuf + 0) = 0x244C8B48;       // mov rcx, [rsp + 0x08]
    *(PUCHAR)(pBuf + 4) = 0x8;              // 
    *(PULONG)(pBuf + 5) = 0x24548B48;       // mov rdx, [rsp + 0x10]
    *(PUCHAR)(pBuf + 9) = 0x10;             // 
    *(PULONG)(pBuf + 10) = 0x24448B4C;      // mov r8, [rsp + 0x18]
    *(PUCHAR)(pBuf + 14) = 0x18;            // 
    *(PULONG)(pBuf + 15) = 0x244C8B4C;      // mov r9, [rsp + 0x20]
    *(PUCHAR)(pBuf + 19) = 0x20;            // 
    *(PUCHAR)(pBuf + 20) = 0xC3;            // ret
    return 21;
}

ULONG GenCall64(IN PUCHAR pBuf, IN PVOID pFn, INT argc, ...)
{
    va_list vl;
    va_start(vl, argc);
    ULONG res = GenCall64V(pBuf, pFn, argc, vl);
    va_end(vl);

    return res;
}

ULONG GenCall64V(IN PUCHAR pBuf, IN PVOID pFn, IN INT argc, IN va_list vl)
{
    USHORT rsp_diff = 0x28;
    ULONG ofst = 0;
    if (argc > 4)
    {
        rsp_diff = (USHORT)(argc * sizeof(ULONG_PTR));
        if (rsp_diff % 0x10)
            rsp_diff = ((rsp_diff / 0x10) + 1) * 0x10;
        rsp_diff += 8;
    }

    // sub rsp, rsp_diff
    *(PULONG)(pBuf + ofst) = (0x00EC8348 | rsp_diff << 24);
    ofst += 4;

    if (argc > 0)
    {
        PVOID arg = va_arg(vl, PVOID);
        *(PUSHORT)(pBuf + ofst) = 0xB948;           // mov rcx, arg
        *(PVOID*)(pBuf + ofst + 2) = arg;           //
        ofst += 10;
    }
    if (argc > 1)
    {
        PVOID arg = va_arg(vl, PVOID);
        *(PUSHORT)(pBuf + ofst) = 0xBA48;           // mov rdx, arg
        *(PVOID*)(pBuf + ofst + 2) = arg;           //
        ofst += 10;
    }
    if (argc > 2)
    {
        PVOID arg = va_arg(vl, PVOID);
        *(PUSHORT)(pBuf + ofst) = 0xB849;           // mov r8, arg
        *(PVOID*)(pBuf + ofst + 2) = arg;           //
        ofst += 10;
    }
    if (argc > 3)
    {
        PVOID arg = va_arg(vl, PVOID);
        *(PUSHORT)(pBuf + ofst) = 0xB949;           // mov r9, arg
        *(PVOID*)(pBuf + ofst + 2) = arg;           //
        ofst += 10;
    }

    for (INT i = 4; i < argc; i++)
    {
        PVOID arg = va_arg(vl, PVOID);

        *(PUSHORT)(pBuf + ofst) = 0xB848;           // mov rcx, arg
        *(PVOID*)(pBuf + ofst + 2) = arg;           //
        ofst += 10;

        // mov [rsp + i*8], rax
        *(PULONG)(pBuf + ofst) = 0x24448948;
        *(PUCHAR)(pBuf + ofst + 4) = (UCHAR)(0x20 + (i - 4) * sizeof(arg));
        ofst += 5;
    }


    *(PUSHORT)(pBuf + ofst) = 0xB848;               // mov rax, pFn
    *(PVOID*)(pBuf + ofst + 2) = pFn;               //
    ofst += 10;

    *(PUSHORT)(pBuf + ofst) = 0xD0FF;               // call rax
    ofst += 2;

    // add rsp, rsp_diff
    *(PULONG)(pBuf + ofst) = (0x00C48348 | rsp_diff << 24);
    ofst += 4;

    return ofst;
}

ULONG GenSync64(IN PUCHAR pBuf, IN PNTSTATUS pStatus, IN PVOID pSetEvent, IN HANDLE hEvent)
{
    ULONG ofst = 0;

    *(PUSHORT)(pBuf + ofst) = 0xA348;           // mov [pStatus], rax
    *(PVOID*)(pBuf + ofst + 2) = pStatus;       //
    ofst += 10;

    *(PUSHORT)(pBuf + ofst) = 0xB948;           // mov rcx, hEvent
    *(PHANDLE)(pBuf + ofst + 2) = hEvent;       //
    ofst += 10;

    *(pBuf + ofst) = 0x48;                      // xor rdx, rdx
    *(PUSHORT)(pBuf + ofst + 1) = 0xD231;       //
    ofst += 3;

    *(PUSHORT)(pBuf + ofst) = 0xB848;           // mov rax, pSetEvent
    *(PVOID*)(pBuf + ofst + 2) = pSetEvent;     //
    ofst += 10;

    *(PUSHORT)(pBuf + ofst) = 0xD0FF;           // call rax
    ofst += 2;

    return ofst;
}

ULONG GenPrologueT(IN BOOLEAN wow64, IN PUCHAR pBuf)
{
    return wow64 ? GenPrologue32(pBuf) : GenPrologue64(pBuf);
}

ULONG GenEpilogueT(IN BOOLEAN wow64, IN PUCHAR pBuf, IN INT retSize)
{
    return wow64 ? GenEpilogue32(pBuf, retSize) : GenEpilogue64(pBuf, retSize);
}

ULONG GenCallT(IN BOOLEAN wow64, IN PUCHAR pBuf, IN PVOID pFn, IN INT argc, ...)
{
    va_list vl;
    va_start(vl, argc);
    ULONG res = wow64 ? GenCall32V(pBuf, pFn, argc, vl) : GenCall64V(pBuf, pFn, argc, vl);
    va_end(vl);

    return res;
}

ULONG GenCallTV(IN BOOLEAN wow64, IN PUCHAR pBuf, IN PVOID pFn, IN INT argc, IN va_list vl)
{
    return wow64 ? GenCall32V(pBuf, pFn, argc, vl) : GenCall64V(pBuf, pFn, argc, vl);
}

ULONG GenSyncT(IN BOOLEAN wow64, IN PUCHAR pBuf, IN PNTSTATUS pStatus, IN PVOID pSetEvent, IN HANDLE hEvent)
{
    return wow64 ? GenSync32(pBuf, pStatus, pSetEvent, hEvent) : GenSync64(pBuf, pStatus, pSetEvent, hEvent);
}



/// <summary>
/// Get ntoskrnl base address
/// </summary>
/// <param name="pSize">Size of module</param>
/// <returns>Found address, NULL if not found</returns>
PVOID GetKernelBase(OUT PULONG pSize)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG bytes = 0;
    PRTL_PROCESS_MODULES pMods = NULL;
    PVOID checkPtr = NULL;
    UNICODE_STRING routineName;

    // Already found
    if (g_KernelBase != NULL)
    {
        if (pSize)
            *pSize = g_KernelSize;
        return g_KernelBase;
    }

    RtlUnicodeStringInit(&routineName, L"NtOpenFile");

    checkPtr = MmGetSystemRoutineAddress(&routineName);
    if (checkPtr == NULL)
        return NULL;

    // Protect from UserMode AV
    status = ZwQuerySystemInformation(SystemModuleInformation, 0, bytes, &bytes);
    if (bytes == 0)
    {
        outLog("获取模块大小失败");
        return NULL;
    }

    pMods = (PRTL_PROCESS_MODULES)ExAllocatePoolWithTag(NonPagedPool, bytes, BB_POOL_TAG);
    RtlZeroMemory(pMods, bytes);

    status = ZwQuerySystemInformation(SystemModuleInformation, pMods, bytes, &bytes);

    if (NT_SUCCESS(status))
    {
        PRTL_PROCESS_MODULE_INFORMATION pMod = pMods->Modules;

        for (ULONG i = 0; i < pMods->NumberOfModules; i++)
        {
            // System routine is inside module
            if (checkPtr >= pMod[i].ImageBase &&
                checkPtr < (PVOID)((PUCHAR)pMod[i].ImageBase + pMod[i].ImageSize))
            {
                g_KernelBase = pMod[i].ImageBase;
                g_KernelSize = pMod[i].ImageSize;
                if (pSize)
                    *pSize = g_KernelSize;
                break;
            }
        }
    }

    if (pMods)
        ExFreePoolWithTag(pMods, BB_POOL_TAG);

    return g_KernelBase;
}


/// <summary>
/// Search for pattern
/// </summary>
/// <param name="pattern">Pattern to search for</param>
/// <param name="wildcard">Used wildcard</param>
/// <param name="len">Pattern length</param>
/// <param name="base">Base address for searching</param>
/// <param name="size">Address range to search in</param>
/// <param name="ppFound">Found location</param>
/// <returns>Status code</returns>
NTSTATUS BBSearchPattern(IN PCUCHAR pattern, IN UCHAR wildcard, IN ULONG_PTR len, IN const VOID* base, IN ULONG_PTR size, OUT PVOID* ppFound)
{
    ASSERT(ppFound != NULL && pattern != NULL && base != NULL);
    if (ppFound == NULL || pattern == NULL || base == NULL)
        return STATUS_INVALID_PARAMETER;

    for (ULONG_PTR i = 0; i < size - len; i++)
    {
        BOOLEAN found = TRUE;
        for (ULONG_PTR j = 0; j < len; j++)
        {
            if (pattern[j] != wildcard && pattern[j] != ((PCUCHAR)base)[i + j])
            {
                found = FALSE;
                break;
            }
        }

        if (found != FALSE)
        {
            *ppFound = (PUCHAR)base + i;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_NOT_FOUND;
}

/// <summary>
/// Gets SSDT base - KiServiceTable
/// </summary>
/// <returns>SSDT base, NULL if not found</returns>
PSYSTEM_SERVICE_DESCRIPTOR_TABLE GetSSDTBase()
{
    PUCHAR ntosBase = (PUCHAR)GetKernelBase(NULL);

    // Already found
    if (g_SSDT != NULL)
        return g_SSDT;

    if (!ntosBase)
        return NULL;

    PIMAGE_NT_HEADERS pHdr = RtlImageNtHeader(ntosBase);
    PIMAGE_SECTION_HEADER pFirstSec = (PIMAGE_SECTION_HEADER)(pHdr + 1);
    for (PIMAGE_SECTION_HEADER pSec = pFirstSec; pSec < pFirstSec + pHdr->FileHeader.NumberOfSections; pSec++)
    {
        // Non-paged, non-discardable, readable sections
        // Probably still not fool-proof enough...
        if (pSec->Characteristics & IMAGE_SCN_MEM_NOT_PAGED &&
            pSec->Characteristics & IMAGE_SCN_MEM_EXECUTE &&
            !(pSec->Characteristics & IMAGE_SCN_MEM_DISCARDABLE) &&
            (*(PULONG)pSec->Name != 'TINI') &&
            (*(PULONG)pSec->Name != 'EGAP'))
        {
            PVOID pFound = NULL;

            // KiSystemServiceRepeat pattern
            UCHAR pattern[] = "\x4c\x8d\x15\xcc\xcc\xcc\xcc\x4c\x8d\x1d\xcc\xcc\xcc\xcc\xf7";
            NTSTATUS status = BBSearchPattern(pattern, 0xCC, sizeof(pattern) - 1, ntosBase + pSec->VirtualAddress, pSec->Misc.VirtualSize, &pFound);
            if (NT_SUCCESS(status))
            {
                g_SSDT = (PSYSTEM_SERVICE_DESCRIPTOR_TABLE)((PUCHAR)pFound + *(PULONG)((PUCHAR)pFound + 3) + 7);
                //DPRINT( "BlackBone: %s: KeSystemServiceDescriptorTable = 0x%p\n", __FUNCTION__, g_SSDT );
                return g_SSDT;
            }
        }
    }

    return NULL;
}

/// <summary>
/// Gets the SSDT entry address by index.
/// </summary>
/// <param name="index">Service index</param>
/// <returns>Found service address, NULL if not found</returns>
PVOID GetSSDTEntry(IN ULONG index)
{
    ULONG size = 0;
    PSYSTEM_SERVICE_DESCRIPTOR_TABLE pSSDT = GetSSDTBase();
    PVOID pBase = GetKernelBase(&size);

    if (pSSDT && pBase)
    {
        // Index range check
        if (index > pSSDT->NumberOfServices)
            return NULL;

        return (PUCHAR)pSSDT->ServiceTableBase + (((PLONG)pSSDT->ServiceTableBase)[index] >> 4);
    }

    return NULL;
}

NTSTATUS
NTAPI
ZwCreateThreadEx(
    OUT PHANDLE hThread,
    IN ACCESS_MASK DesiredAccess,
    IN PVOID ObjectAttributes,
    IN HANDLE ProcessHandle,
    IN PVOID lpStartAddress,
    IN PVOID lpParameter,
    IN ULONG Flags,
    IN SIZE_T StackZeroBits,
    IN SIZE_T SizeOfStackCommit,
    IN SIZE_T SizeOfStackReserve,
    IN PNT_PROC_THREAD_ATTRIBUTE_LIST AttributeList
)
{
    NTSTATUS status = STATUS_SUCCESS;

    fnNtCreateThreadEx NtCreateThreadEx = (fnNtCreateThreadEx)(ULONG_PTR)GetSSDTEntry(dynData.NtCreateThdExIndex);
    if (NtCreateThreadEx)
    {
        //
        // If previous mode is UserMode, addresses passed into ZwCreateThreadEx must be in user-mode space
        // Switching to KernelMode allows usage of kernel-mode addresses
        //
        PUCHAR pPrevMode = (PUCHAR)PsGetCurrentThread() + dynData.PrevMode;
        UCHAR prevMode = *pPrevMode;
        *pPrevMode = KernelMode;

        status = NtCreateThreadEx(
            hThread, DesiredAccess, ObjectAttributes,
            ProcessHandle, lpStartAddress, lpParameter,
            Flags, StackZeroBits, SizeOfStackCommit,
            SizeOfStackReserve, AttributeList
        );

        *pPrevMode = prevMode;
    }
    else
        status = STATUS_NOT_FOUND;

    return status;
}


/// <summary>
/// Create new thread in the target process
/// Must be running in target process context
/// </summary>
/// <param name="pBaseAddress">Thread start address</param>
/// <param name="pParam">Thread argument</param>
/// <param name="flags">Thread creation flags</param>
/// <param name="wait">If set to TRUE - wait for thread completion</param>
/// <param name="pExitStatus">Thread exit status</param>
/// <returns>Status code</returns>
NTSTATUS BBExecuteInNewThread(
    IN PVOID pBaseAddress,
    IN PVOID pParam,
    IN ULONG flags,
    IN BOOLEAN wait,
    OUT PNTSTATUS pExitStatus
)
{
    HANDLE hThread = NULL;
    OBJECT_ATTRIBUTES ob = { 0 };

    InitializeObjectAttributes(&ob, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS status = ZwCreateThreadEx(
        &hThread, THREAD_QUERY_LIMITED_INFORMATION, &ob,
        ZwCurrentProcess(), pBaseAddress, pParam, flags,
        0, 0x1000, 0x100000, NULL
    );

    // Wait for completion
    if (NT_SUCCESS(status) && wait != FALSE)
    {
        // Force 60 sec timeout
        LARGE_INTEGER timeout = { 0 };
        timeout.QuadPart = -(60ll * 10 * 1000 * 1000);

        status = ZwWaitForSingleObject(hThread, TRUE, &timeout);
        if (NT_SUCCESS(status))
        {
            THREAD_BASIC_INFORMATION info = { 0 };
            ULONG bytes = 0;

            status = ZwQueryInformationThread(hThread, ThreadBasicInformation, &info, sizeof(info), &bytes);
            if (NT_SUCCESS(status) && pExitStatus)
            {
                *pExitStatus = info.ExitStatus;
            }
            else if (!NT_SUCCESS(status))
            {
                outLog("查询线程基本信息失败: 0x%X", status);
            }
        }
        else
        {
            outLog("ZwWaitForSingleObject失败: 0x%X", status);
        }
    }
    else
    {
        outLog("ZwCreateThreadEx失败: 0x%X", status);
    }

    if (hThread)
        ZwClose(hThread);

    return status;
}

/// <summary>
/// Call arbitrary function
/// </summary>
/// <param name="newThread">Perform call in a separate thread</param>
/// <param name="pContext">Loader context</param>
/// <param name="pRoutine">Routine to call.</param>
/// <param name="argc">Number of arguments.</param>
/// <param name="...">Arguments</param>
/// <returns>Status code</returns>
NTSTATUS BBCallRoutine(IN BOOLEAN newThread, IN PMMAP_CONTEXT pContext, IN PVOID pRoutine, IN INT argc, ...)
{
    NTSTATUS status = STATUS_SUCCESS;
    va_list vl;
    BOOLEAN wow64 = PsGetProcessWow64Process(pContext->pProcess) != NULL;

    va_start(vl, argc);
    ULONG ofst = GenPrologueT(wow64, pContext->userMem->code);
    ofst += GenCallTV(wow64, pContext->userMem->code + ofst, pRoutine, argc, vl);
    ofst += GenSyncT(wow64, pContext->userMem->code + ofst, &pContext->userMem->status, pContext->pSetEvent, pContext->hSync);
    ofst += GenEpilogueT(wow64, pContext->userMem->code + ofst, argc * sizeof(ULONG));

    if (newThread)
    {
        status = BBExecuteInNewThread(pContext->userMem->code, NULL, 0/*THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER*/, TRUE, NULL);
    }
    else
    {
        KeResetEvent(pContext->pSync);
        status = BBQueueUserApc(pContext->pWorker, pContext->userMem->code, NULL, NULL, NULL, pContext->noThreads);
        if (NT_SUCCESS(status))
        {
            LARGE_INTEGER timeout = { 0 };
            timeout.QuadPart = -(10ll * 10 * 1000 * 1000);  // 10s

            status = KeWaitForSingleObject(pContext->pSync, Executive, UserMode, TRUE, &timeout);

            timeout.QuadPart = -(1ll * 10 * 1000);          // 1ms
            KeDelayExecutionThread(KernelMode, TRUE, &timeout);
        }
    }

    va_end(vl);

    return status;
}

/// <summary>
///Try to resolve image via SxS isolation
/// </summary>
/// <param name="pContext">Loader context.</param>
/// <param name="name">Name to resolve</param>
/// <param name="resolved">Resolved name if any</param>
/// <returns>Status code</returns>
NTSTATUS BBResolveSxS(
    IN PMMAP_CONTEXT pContext,
    IN PUNICODE_STRING name,
    OUT PUNICODE_STRING resolved
)
{
    NTSTATUS status = STATUS_NOT_FOUND;
    UNICODE_STRING ustrNtdll = { 0 };
    BOOLEAN wow64 = PsGetProcessWow64Process(pContext->pProcess) != NULL;

    typedef struct _STRIBG_BUF
    {
        union
        {
            UNICODE_STRING name1;
            UNICODE_STRING32 name132;
        };
        union
        {
            UNICODE_STRING name2;
            UNICODE_STRING32 name232;
        };
        union
        {
            UNICODE_STRING origName;
            UNICODE_STRING32 origName32;
        };
        union
        {
            PUNICODE_STRING pResolved;
            ULONG pResolved32;
        };
        wchar_t origBuf[0x100];
        wchar_t staticBuf[0x200];
    } STRIBG_BUF, * PSTRIBG_BUF;

    PSTRIBG_BUF pStringBuf = (PSTRIBG_BUF)pContext->userMem->buffer;

    RtlUnicodeStringInit(&ustrNtdll, L"ntdll.dll");

    PVOID hNtdll = BBGetUserModule(pContext->pProcess, &ustrNtdll, wow64);
    PVOID pQueryName = BBGetModuleExport(hNtdll, "RtlDosApplyFileIsolationRedirection_Ustr", pContext->pProcess, NULL);

    if (pQueryName == NULL)
    {
        outLog("RtlDosApplyFileIsolationRedirection_Ustr 失败");
        return STATUS_NOT_FOUND;
    }

    RtlZeroMemory(pStringBuf->origBuf, sizeof(pStringBuf->origBuf));
    RtlZeroMemory(pStringBuf->staticBuf, sizeof(pStringBuf->staticBuf));

    // Fill params
    memcpy(pStringBuf->origBuf, name->Buffer, name->Length);
    if (wow64)
    {
        pStringBuf->origName32.Buffer = (ULONG)(ULONG_PTR)pStringBuf->origBuf;
        pStringBuf->origName32.MaximumLength = sizeof(pStringBuf->origBuf);
        pStringBuf->origName32.Length = name->Length;

        pStringBuf->name132.Buffer = (ULONG)(ULONG_PTR)pStringBuf->staticBuf;
        pStringBuf->name132.MaximumLength = sizeof(pStringBuf->staticBuf);
        pStringBuf->name132.Length = 0;

        pStringBuf->name232.Buffer = 0;
        pStringBuf->name232.Length = pStringBuf->name232.MaximumLength = 0;
    }
    else
    {
        RtlInitUnicodeString(&pStringBuf->origName, pStringBuf->origBuf);
        RtlInitEmptyUnicodeString(&pStringBuf->name1, pStringBuf->staticBuf, sizeof(pStringBuf->staticBuf));
        RtlInitEmptyUnicodeString(&pStringBuf->name2, NULL, 0);
    }


    // Prevent some unpredictable shit
    __try
    {
        // RtlDosApplyFileIsolationRedirection_Ustr
        status = BBCallRoutine(
            FALSE, pContext, pQueryName, 9,
            (PVOID)TRUE, &pStringBuf->origName, NULL,
            &pStringBuf->name1, &pStringBuf->name2, &pStringBuf->pResolved,
            NULL, NULL, NULL
        );

        if (NT_SUCCESS(status) && NT_SUCCESS(pContext->userMem->status))
        {
            if (wow64)
            {
                ULONG tmp = ((PUNICODE_STRING32)pStringBuf->pResolved32)->Buffer;
                pStringBuf->pResolved = &pStringBuf->name1;
                pStringBuf->pResolved->Buffer = (PWCH)tmp;
            }

            RtlDowncaseUnicodeString(resolved, pStringBuf->pResolved, TRUE);
            // TODO: name2 cleanup
        }

        return NT_SUCCESS(status) ? pContext->userMem->status : status;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        outLog("BBResolveSxS崩溃 Exception Code: 0x%X", GetExceptionCode());
        return STATUS_UNHANDLED_EXCEPTION;
    }
}

/// <summary>
/// Get exported function address
/// </summary>
/// <param name="pBase">Module base</param>
/// <param name="name_ord">Function name or ordinal</param>
/// <param name="pProcess">Target process for user module</param>
/// <param name="baseName">Dll name for api schema</param>
/// <returns>Found address, NULL if not found</returns>
PVOID BBGetModuleExport(IN PVOID pBase, IN PCCHAR name_ord, IN PEPROCESS pProcess, IN PUNICODE_STRING baseName)
{
    PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)pBase;
    PIMAGE_NT_HEADERS32 pNtHdr32 = NULL;
    PIMAGE_NT_HEADERS64 pNtHdr64 = NULL;
    PIMAGE_EXPORT_DIRECTORY pExport = NULL;
    ULONG expSize = 0;
    ULONG_PTR pAddress = 0;

    ASSERT(pBase != NULL);
    if (pBase == NULL)
        return NULL;

    /// Not a PE file
    if (pDosHdr->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    pNtHdr32 = (PIMAGE_NT_HEADERS32)((PUCHAR)pBase + pDosHdr->e_lfanew);
    pNtHdr64 = (PIMAGE_NT_HEADERS64)((PUCHAR)pBase + pDosHdr->e_lfanew);

    // Not a PE file
    if (pNtHdr32->Signature != IMAGE_NT_SIGNATURE)
        return NULL;

    // 64 bit image
    if (pNtHdr32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        pExport = (PIMAGE_EXPORT_DIRECTORY)(pNtHdr64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress + (ULONG_PTR)pBase);
        expSize = pNtHdr64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    }
    // 32 bit image
    else
    {
        pExport = (PIMAGE_EXPORT_DIRECTORY)(pNtHdr32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress + (ULONG_PTR)pBase);
        expSize = pNtHdr32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    }

    PUSHORT pAddressOfOrds = (PUSHORT)(pExport->AddressOfNameOrdinals + (ULONG_PTR)pBase);
    PULONG  pAddressOfNames = (PULONG)(pExport->AddressOfNames + (ULONG_PTR)pBase);
    PULONG  pAddressOfFuncs = (PULONG)(pExport->AddressOfFunctions + (ULONG_PTR)pBase);

    for (ULONG i = 0; i < pExport->NumberOfFunctions; ++i)
    {
        USHORT OrdIndex = 0xFFFF;
        PCHAR  pName = NULL;

        // Find by index
        if ((ULONG_PTR)name_ord <= 0xFFFF)
        {
            OrdIndex = (USHORT)i;
        }
        // Find by name
        else if ((ULONG_PTR)name_ord > 0xFFFF && i < pExport->NumberOfNames)
        {
            pName = (PCHAR)(pAddressOfNames[i] + (ULONG_PTR)pBase);
            OrdIndex = pAddressOfOrds[i];
        }
        // Weird params
        else
            return NULL;

        if (((ULONG_PTR)name_ord <= 0xFFFF && (USHORT)((ULONG_PTR)name_ord) == OrdIndex + pExport->Base) ||
            ((ULONG_PTR)name_ord > 0xFFFF && strcmp(pName, name_ord) == 0))
        {
            pAddress = pAddressOfFuncs[OrdIndex] + (ULONG_PTR)pBase;

            // Check forwarded export
            if (pAddress >= (ULONG_PTR)pExport && pAddress <= (ULONG_PTR)pExport + expSize)
            {
                WCHAR strbuf[256] = { 0 };
                ANSI_STRING forwarder = { 0 };
                ANSI_STRING import = { 0 };

                UNICODE_STRING uForwarder = { 0 };
                ULONG delimIdx = 0;
                PVOID forwardBase = NULL;
                PVOID result = NULL;

                // System image, not supported
                if (pProcess == NULL)
                    return NULL;

                RtlInitAnsiString(&forwarder, (PCSZ)pAddress);
                RtlInitEmptyUnicodeString(&uForwarder, strbuf, sizeof(strbuf));

                RtlAnsiStringToUnicodeString(&uForwarder, &forwarder, FALSE);
                for (ULONG j = 0; j < uForwarder.Length / sizeof(WCHAR); j++)
                {
                    if (uForwarder.Buffer[j] == L'.')
                    {
                        uForwarder.Length = (USHORT)(j * sizeof(WCHAR));
                        uForwarder.Buffer[j] = L'\0';
                        delimIdx = j;
                        break;
                    }
                }

                // Get forward function name/ordinal
                RtlInitAnsiString(&import, forwarder.Buffer + delimIdx + 1);
                RtlAppendUnicodeToString(&uForwarder, L".dll");

                //
                // Check forwarded module
                //
                UNICODE_STRING resolved = { 0 };
                UNICODE_STRING resolvedName = { 0 };
                BBResolveImagePath(NULL, pProcess, KApiShemaOnly, &uForwarder, baseName, &resolved);
                BBStripPath(&resolved, &resolvedName);

                forwardBase = BBGetUserModule(pProcess, &resolvedName, PsGetProcessWow64Process(pProcess) != NULL);
                result = BBGetModuleExport(forwardBase, import.Buffer, pProcess, &resolvedName);
                RtlFreeUnicodeString(&resolved);

                return result;
            }

            break;
        }
    }

    return (PVOID)pAddress;
}

NTSTATUS ApcCreateRemoteThread(HANDLE ProcessHandle, PVOID pUserFunc)
{
    KAPC_STATE apc;
    PVOID DbgUserBreakPoint = NULL;
    PEPROCESS pProcess = NULL;
    UNICODE_STRING ntdllMod;
    RtlInitUnicodeString(&ntdllMod, L"ntdll.dll");

    //NTSTATUS status = PsLookupProcessByProcessId(pid, &pProcess);

    NTSTATUS status = ObReferenceObjectByHandle(ProcessHandle,
        PROCESS_ALL_ACCESS,
        *PsProcessType,
        KernelMode,
        (PVOID*)&pProcess,
        NULL);
    if (NT_SUCCESS(status))
    {
        BOOLEAN isWow64 = (PsGetProcessWow64Process(pProcess) != NULL) ? TRUE : FALSE;

        KeStackAttachProcess(pProcess, &apc);
        // Get ntdll base
        //PVOID pNtdll = BBGetUserModule(pProcess, &ntdllMod, isWow64);

        //if (!pNtdll)
        //{
        //    outLog("获取ntdll模块基址失败");
        //    status = STATUS_NOT_FOUND;
        //}

        //if (NT_SUCCESS(status))
        //{
        //    DbgUserBreakPoint = BBGetModuleExport(pNtdll, "DbgUserBreakPoint", pProcess, NULL);
        //    if (!DbgUserBreakPoint)
        //    {
        //        outLog("获取DbgUserBreakPoint函数地址失败");
        //        status = STATUS_NOT_FOUND;
        //    }
        //}

        status = BBApcInject2(pUserFunc, pProcess, NULL, NULL);

        KeUnstackDetachProcess(&apc);
        if (pProcess)
            ObDereferenceObject(pProcess);
    }
    return status;
}



/// <summary>
/// Queue user-mode APC to the target thread
/// </summary>
/// <param name="pThread">Target thread</param>
/// <param name="pUserFunc">APC function</param>
/// <param name="Arg1">Argument 1</param>
/// <param name="Arg2">Argument 2</param>
/// <param name="Arg3">Argument 3</param>
/// <param name="bForce">If TRUE - force delivery by issuing special kernel APC</param>
/// <returns>Status code</returns>
NTSTATUS BBQueueUserApc(
    IN PETHREAD pThread,
    IN PVOID pUserFunc,
    IN PVOID Arg1,
    IN PVOID Arg2,
    IN PVOID Arg3,
    IN BOOLEAN bForce
)
{
    ASSERT(pThread != NULL);
    if (pThread == NULL)
        return STATUS_INVALID_PARAMETER;

    // Allocate APC
    PKAPC pPrepareApc = NULL;
    PKAPC pInjectApc = (PKAPC)ExAllocatePoolWithTag(NonPagedPool, sizeof(KAPC), BB_POOL_TAG);

    if (pInjectApc == NULL)
    {
        outLog("创建APC对象失败");
        return STATUS_NO_MEMORY;
    }

    // Actual APC
    KeInitializeApc(
        pInjectApc, (PKTHREAD)pThread,
        OriginalApcEnvironment, &KernelApcInjectCallback,
        NULL, (PKNORMAL_ROUTINE)(ULONG_PTR)pUserFunc, UserMode, Arg1
    );

    // Setup force-delivery APC
    if (bForce)
    {
        pPrepareApc = (PKAPC)ExAllocatePoolWithTag(NonPagedPool, sizeof(KAPC), BB_POOL_TAG);
        KeInitializeApc(
            pPrepareApc, (PKTHREAD)pThread,
            OriginalApcEnvironment, &KernelApcPrepareCallback,
            NULL, NULL, KernelMode, NULL
        );
    }

    // Insert APC
    if (KeInsertQueueApc(pInjectApc, Arg2, Arg3, 0))
    {
        if (bForce && pPrepareApc)
            KeInsertQueueApc(pPrepareApc, NULL, NULL, 0);

        return STATUS_SUCCESS;
    }
    else
    {
        outLog("插入APC失败");

        ExFreePoolWithTag(pInjectApc, BB_POOL_TAG);

        if (pPrepareApc)
            ExFreePoolWithTag(pPrepareApc, BB_POOL_TAG);

        return STATUS_NOT_CAPABLE;
    }
}

//
// Injection APC routines
//
VOID KernelApcPrepareCallback(
    PKAPC Apc,
    PKNORMAL_ROUTINE* NormalRoutine,
    PVOID* NormalContext,
    PVOID* SystemArgument1,
    PVOID* SystemArgument2
)
{
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    //DPRINT( "BlackBone: %s: Called\n", __FUNCTION__ );

    // Alert current thread
    KeTestAlertThread(UserMode);
    ExFreePoolWithTag(Apc, BB_POOL_TAG);
}

VOID KernelApcInjectCallback(
    PKAPC Apc,
    PKNORMAL_ROUTINE* NormalRoutine,
    PVOID* NormalContext,
    PVOID* SystemArgument1,
    PVOID* SystemArgument2
)
{
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    //DPRINT( "BlackBone: %s: Called. NormalRoutine = 0x%p\n", __FUNCTION__, *NormalRoutine );

    // Skip execution
    if (PsIsThreadTerminating(PsGetCurrentThread()))
        *NormalRoutine = NULL;

    // Fix Wow64 APC
    if (PsGetCurrentProcessWow64Process() != NULL)
        PsWrapApcWow64Thread(NormalContext, (PVOID*)NormalRoutine);

    ExFreePoolWithTag(Apc, BB_POOL_TAG);
}