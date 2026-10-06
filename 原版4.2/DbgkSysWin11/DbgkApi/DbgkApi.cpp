#include "../Driver.h"
#include "../ntos/inc/ntosdef.h"
#include "../ntos/inc/ketypes.h"
#include "../ntos/inc/amd64.h"
#include "../ntos/inc/extypes.h"
#include "../ntos/inc/mi.h"
#include "../ntos/inc/pstypes.h"
#include "../ntos/inc/obtypes.h"
#include "../ntos/inc/peb_teb.h"
#include "../ntos/inc/mmtypes.h"
#include "../ntos/inc/ntdbg.h"
#include "../List/MyList.h"
#include "../ntos/inc/ntlpcapi.h"
#include "../ntos/inc/ke.h"
#include "../ntos/inc/ki.h"
#include "../ntos/inc/pecoff.h"
#include "../Log/log.h"
#include "../ntos/inc/psp.h"
#include "../Globals.h"
#include "../Hvm/hypervisor_gateway.h"
#include "String/StringHandler.h"
#include "DbgkApi.h"



#define WOW64_ROUND_TO_PAGES(Size)  \
        (((ULONG_PTR)(Size) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define WOW64_GET_TEB32_SAFE(teb64) \
        ((PTEB32) ((ULONGLONG)teb64 + WOW64_ROUND_TO_PAGES (sizeof(TEB))))

NTSTATUS DbgkInitialize()
{
    NTSTATUS Status = STATUS_SUCCESS;
    UNICODE_STRING Name;
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer = { 0 };
    GENERIC_MAPPING GenericMapping = { STANDARD_RIGHTS_READ | DEBUG_READ_EVENT,
                                      STANDARD_RIGHTS_WRITE | DEBUG_PROCESS_ASSIGN,
                                      STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
                                      DEBUG_ALL_ACCESS };

    PAGED_CODE();
    /* Initialize the process debug port mutex */
    ExInitializeFastMutex(&DbgkpProcessDebugPortMutex);

    /* Create the Debug Object Type */
    RtlZeroMemory(&ObjectTypeInitializer, sizeof(ObjectTypeInitializer));
    RtlInitUnicodeString(&Name, L"Hvm_DebugObject");

    ObjectTypeInitializer.Length = sizeof(ObjectTypeInitializer);
    ObjectTypeInitializer.SecurityRequired = TRUE;
    ObjectTypeInitializer.InvalidAttributes = 0;
    ObjectTypeInitializer.PoolType = NonPagedPool;
    ObjectTypeInitializer.ValidAccessMask = DEBUG_ALL_ACCESS;
    ObjectTypeInitializer.GenericMapping = GenericMapping;
    //ObjectTypeInitializer.DefaultNonPagedPoolCharge = sizeof(DEBUG_OBJECT);    
    ObjectTypeInitializer.DefaultPagedPoolCharge = 0;
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = 0;
    ObjectTypeInitializer.CloseProcedure = DbgkpCloseObject;  //注册回调函数
    ObjectTypeInitializer.DeleteProcedure = NULL;

    //Hvm_DbgkDebugObjectType = *DbgkDebugObjectType;

    Status = ObCreateObjectType(&Name, &ObjectTypeInitializer, NULL, &Hvm_DbgkDebugObjectType);
    if (!NT_SUCCESS(Status))
    {
        Hvm_DbgkDebugObjectType = GetDebugObjectType(Name);
        if (Hvm_DbgkDebugObjectType)
        {
            Hvm_DbgkDebugObjectType->TypeInfo.CloseProcedure = DbgkpCloseObject;
            Status = STATUS_SUCCESS;
        }
    }
    return Status;
}


EXTERN_C
VOID
DbgkpCloseObject(
    IN _EPROCESS* Process,
    IN PVOID Object,
    IN ACCESS_MASK GrantedAccess,
    IN ULONG_PTR SystemHandleCount
)
/*++

Routine Description:

    Called by the object manager when a handle is closed to the object.

Arguments:

    Process - Process doing the close
    Object - Debug object being deleted
    GrantedAccess - Access ranted for this handle
    ProcessHandleCount - Unused and unmaintained by OB
    SystemHandleCount - Current handle count for this object

Return Value:

    None.

--*/
{
    PDEBUG_OBJECT DebugObject = (PDEBUG_OBJECT)Object;
    PDEBUG_EVENT DebugEvent;
    PLIST_ENTRY ListPtr;
    BOOLEAN Deref;
    PDEBUG_OBJECT Port;
    PDEBUG_PROCESS DebugProcess;
    size_t ptr_DebugPort = NULL;

    PAGED_CODE();

    //DbgBreakPoint();

    UNREFERENCED_PARAMETER(GrantedAccess);

    //
    // If this isn't the last handle then do nothing.
    //
    if (SystemHandleCount > 1) {
        return;
    }

    ExAcquireFastMutex(&DebugObject->Mutex);

    //
    // Mark this object as going away and wake up any processes that are waiting.
    //
    DebugObject->Flags |= DEBUG_OBJECT_DELETE_PENDING;

    //
    // Remove any events and queue them to a temporary queue
    //
    ListPtr = DebugObject->EventList.Flink;
    InitializeListHead(&DebugObject->EventList);

    ExReleaseFastMutex(&DebugObject->Mutex);

    //
    // Wake anyone waiting. They need to leave this object alone now as its deleting
    //
    KeSetEvent(&DebugObject->EventsPresent, 0, FALSE);

    //
    // Loop over all processes and remove the debug port from any that still have it.
    // Debug port propagation was disabled by setting the delete pending flag above so we only have to do this
    // once. No more refs can appear now.
    // 循环遍历所有进程，并删除仍然具有调试端口的进程。通过设置删除待定标志禁用了调试端口传播，因此我们只需执行一次此操作。
    // 现在不再会出现更多引用。
    //
    for (Process = PsGetNextProcess(NULL);
        Process != NULL;
        Process = PsGetNextProcess(Process))
    {

        if (IsDebugTargetProcess(Process, &DebugProcess))
        {
            Port = DebugProcess->DebugObject;
            ptr_DebugPort = NULL;
        }
        else
        {
            ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
            Port = *(PDEBUG_OBJECT*)ptr_DebugPort;
        }

        if (Port == DebugObject)
        {
            Deref = FALSE;
            ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);
            if (Port == DebugObject)
            {
                if (ptr_DebugPort)
                {
                    *(PDEBUG_OBJECT*)ptr_DebugPort = NULL;
                }                
                Deref = TRUE;
            }
            ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);


            if (Deref)
            {
                DbgkpMarkProcessPeb(Process);  //清理进程的peb结构
                //
                // If the caller wanted process deletion on debugger dying (old interface) then kill off the process.
                //
                if (DebugObject->Flags & DEBUG_OBJECT_KILL_ON_CLOSE)
                {
                    DbgPrint("结束进程");
                    PsTerminateProcess(Process, STATUS_DEBUGGER_INACTIVE);
                }
                ObDereferenceObject(DebugObject);
            }
        }
    }
    //
    // Wake up all the removed threads.
    //
    while (ListPtr != &DebugObject->EventList) {
        DebugEvent = CONTAINING_RECORD(ListPtr, DEBUG_EVENT, EventList);
        ListPtr = ListPtr->Flink;
        DebugEvent->Status = STATUS_DEBUGGER_INACTIVE;
        DbgkpWakeTarget(DebugEvent);
    }

    DbgPrint("移除调试对象");
    DeleteDebugProcess(DebugObject);
}

VOID DbgkUnInitialize()
{
    PAGED_CODE();
    //if (Hvm_DbgkDebugObjectType)
    //{
    //    ObDereferenceObject(Hvm_DbgkDebugObjectType);
    //}    
}

POBJECT_TYPE GetDebugObjectType(UNICODE_STRING Name)
{
    //TypeIndex下标从2开始存储的才是有效指针
    for (DWORD TypeIndex = 2; ObTypeIndexTable[TypeIndex] != NULL; TypeIndex++)
    {
        if (RtlEqualUnicodeString(&ObTypeIndexTable[TypeIndex]->Name, &Name, FALSE))
        {
            return ObTypeIndexTable[TypeIndex];
        }
    }
    return NULL;
}

//调试器运行后 为调试器创建调试对象
EXTERN_C
NTSTATUS NtCreateDebugObject(OUT PHANDLE DebugHandle,
    IN ACCESS_MASK DesiredAccess,
    IN POBJECT_ATTRIBUTES ObjectAttributes,
    IN ULONG Flags)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PDEBUG_OBJECT DebugObject;
    HANDLE hDebug = NULL;
    NTSTATUS Status;
    PAGED_CODE();

    /* Check if we were called from user mode*/
    if (PreviousMode != KernelMode)
    {
        /* Enter SEH for probing */
        _SEH2_TRY
        {
            /* Probe the handle */
            ProbeForWriteHandle(DebugHandle);
        }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        } _SEH2_END;
    }

    /* Check for invalid flags */
    if (Flags & ~DBGK_ALL_FLAGS) return STATUS_INVALID_PARAMETER;

    /* Create the Object */
    //根据调试类型创建调试对象
    Status = ObCreateObject(PreviousMode,
        Hvm_DbgkDebugObjectType,
        ObjectAttributes,
        PreviousMode,
        NULL,
        sizeof(DEBUG_OBJECT),
        0,
        0,
        (PVOID*)&DebugObject);
    if (NT_SUCCESS(Status))
    {
        /* Initialize the Debug Object's Fast Mutex */
        ExInitializeFastMutex(&DebugObject->Mutex);

        /* Initialize the State Event List */
        InitializeListHead(&DebugObject->EventList);

        /* Initialize the Debug Object's Wait Event */
        KeInitializeEvent(&DebugObject->EventsPresent,
            NotificationEvent,
            FALSE);

        /* Set the Flags */
        if (Flags & DBGK_KILL_PROCESS_ON_EXIT) //退出时终止进程
        {
            DebugObject->Flags = DEBUG_OBJECT_KILL_ON_CLOSE;
        }
        else
        {
            DebugObject->Flags = 0;
        }

        /* Insert it */
        Status = ObInsertObject((PVOID)DebugObject,
            NULL,
            DesiredAccess,
            0,
            NULL,
            &hDebug);
        if (NT_SUCCESS(Status))
        {
            /* Enter SEH to protect the write */
            _SEH2_TRY
            {
                /* Return the handle */
                *DebugHandle = hDebug;
            }
                _SEH2_EXCEPT(ExSystemExceptionFilter())
            {
                /* Get the exception code */
                Status = _SEH2_GetExceptionCode();
            } _SEH2_END;
        }
    }

    /* Return Status */
    DBGKTRACE(DBGK_OBJECT_DEBUG, "Handle: %p DebugObject: %p\n",
        hDebug, DebugObject);
    return Status;
}

NTSTATUS
NtSetInformationDebugObject(
    IN HANDLE DebugObjectHandle,
    IN DEBUGOBJECTINFOCLASS DebugObjectInformationClass,
    IN PVOID DebugInformation,
    IN ULONG DebugInformationLength,
    OUT PULONG ReturnLength OPTIONAL
)
/*++

Routine Description:

    This function sets the state of a debug object.

Arguments:

    ProcessHandle - Supplies a handle to a process object.

    ProcessInformationClass - Supplies the class of information being
        set.

    ProcessInformation - Supplies a pointer to a record that contains the
        information to set.

    ProcessInformationLength - Supplies the length of the record that contains
        the information to set.

Return Value:

    NTSTATUS - Status of call

--*/
{
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;
    PDEBUG_OBJECT DebugObject;
    ULONG Flags;

    PreviousMode = KeGetPreviousMode();

    __try {
        if (PreviousMode != KernelMode) {
            ProbeForRead(DebugInformation,
                DebugInformationLength,
                sizeof(ULONG));
            if (ARGUMENT_PRESENT(ReturnLength)) {
                ProbeForWriteUlong(ReturnLength);
            }
        }
        if (ARGUMENT_PRESENT(ReturnLength)) {
            *ReturnLength = 0;
        }

        switch (DebugObjectInformationClass)
        {
        case DebugObjectFlags:
        {

            if (DebugInformationLength != sizeof(ULONG)) {
                if (ARGUMENT_PRESENT(ReturnLength)) {
                    *ReturnLength = sizeof(ULONG);
                }
                return STATUS_INFO_LENGTH_MISMATCH;
            }
            Flags = *(PULONG)DebugInformation;

            break;
        }
        default: {
            return STATUS_INVALID_PARAMETER;
        }
        }
    } 
    __except(ExSystemExceptionFilter()) {
        return GetExceptionCode();
    }


    switch (DebugObjectInformationClass)
    {
    case DebugObjectFlags:
    {
        if (Flags & ~DBGK_KILL_PROCESS_ON_EXIT) {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ObReferenceObjectByHandle(DebugObjectHandle,
            DEBUG_SET_INFORMATION,
            Hvm_DbgkDebugObjectType,
            PreviousMode,
            (PVOID*)&DebugObject,
            NULL);

        if (!NT_SUCCESS(Status)) {
            return Status;
        }
        ExAcquireFastMutex(&DebugObject->Mutex);

        if (Flags & DBGK_KILL_PROCESS_ON_EXIT) {
            DebugObject->Flags |= DEBUG_OBJECT_KILL_ON_CLOSE;
        }
        else {
            DebugObject->Flags &= ~DEBUG_OBJECT_KILL_ON_CLOSE;
        }

        ExReleaseFastMutex(&DebugObject->Mutex);

        ObDereferenceObject(DebugObject);
    }
    }
    return STATUS_SUCCESS;
}

VOID InsertDebuggerList(PDEBUGGER_TABLE_ENTRY Debugger)
{
    if (Debugger)
    {
        ExAcquireFastMutex(&g_DebuggerList.Mutex);
        InsertNode(&g_DebuggerList.list_entry, &Debugger->list_entry);
        ExReleaseFastMutex(&g_DebuggerList.Mutex);
    }
}

VOID InsertDebugProcessList(PDEBUG_PROCESS debug_process)
{
    if (debug_process)
    {
        ExAcquireFastMutex(&g_DebugProcessList.Mutex);
        InsertNode(&g_DebugProcessList.list_entry, &debug_process->list_entry);
        ExReleaseFastMutex(&g_DebugProcessList.Mutex);
    }
}

//VOID ReleaseDebuggerList()
//{
//    g_DebuggerList->Lock();
//    __try
//    {
//        for (DWORD i = 0; i < g_DebuggerList->Count(); i++)
//        {
//            PDEBUGGER_DATA tmp = (PDEBUGGER_DATA)g_DebuggerList->Get(i);
//            if (tmp)
//            {
//                delete tmp;
//            }
//        }
//    }
//    __finally
//    {
//        g_DebuggerList->UnLock();
//    }
//    delete g_DebuggerList;
//}

//释放列表
VOID ReleaseDebugProcessList()
{
    PLIST_ENTRY ListHead, NextEntry;
    PDEBUG_PROCESS entry;

    __try
    {
        ListHead = &g_DebugProcessList.list_entry.ListHead;
        NextEntry = ListHead->Flink;
        while (ListHead != NextEntry)
        {
            entry = CONTAINING_RECORD(NextEntry,
                DEBUG_PROCESS,
                list_entry);

            RemoveEntryList(NextEntry);

            /* Move to the next entry */
            NextEntry = NextEntry->Flink;

            free_pool(entry);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LogError("释放调试进程列表崩溃!");
    }
}

//移除元素
VOID DeleteDebugProcess(PDEBUG_OBJECT DebugObject)
{
    PLIST_ENTRY ListHead, NextEntry;
    PDEBUG_PROCESS entry;

    ExAcquireFastMutex(&g_DebugProcessList.Mutex);
    ListHead = &g_DebugProcessList.list_entry.ListHead;
    NextEntry = ListHead->Flink;
    while (ListHead != NextEntry)
    {
        entry = CONTAINING_RECORD(NextEntry,
            DEBUG_PROCESS,
            list_entry);

        if (entry)
        {
            if (entry->DebugObject == DebugObject)
            {
                RemoveEntryList(NextEntry);
                free_pool(entry);
                break;
            }
        }

        /* Move to the next entry */
        NextEntry = NextEntry->Flink;
    }
    ExReleaseFastMutex(&g_DebugProcessList.Mutex);
}

//设置调试对象
BOOLEAN SetDebugTargetProcess(_EPROCESS* Process, PDEBUG_OBJECT DebugObject)
{
    BOOLEAN result = FALSE;    

    if (IsDebugger(PsGetCurrentProcess()))
    {
        PDEBUG_PROCESS entry = allocate_pool<DEBUG_PROCESS>();
        entry->Process = Process;
        entry->DebugObject = DebugObject;
        ExInitializeFastMutex(&entry->Mutex);
        InsertDebugProcessList(entry);
        result = TRUE;
    }
    return result;
}

//判断目标进程是否是被调试的进程
BOOLEAN IsDebugTargetProcess(IN _EPROCESS* Process,
    OUT PDEBUG_PROCESS* DebugProcess)
{
    BOOLEAN result = FALSE;
    *DebugProcess = NULL;


    PLIST_ENTRY ListHead, NextEntry;
    PDEBUG_PROCESS entry;

    ExAcquireFastMutex(&g_DebugProcessList.Mutex);
    ListHead = &g_DebugProcessList.list_entry.ListHead;
    NextEntry = ListHead->Flink;
    while (ListHead != NextEntry)
    {
        entry = CONTAINING_RECORD(NextEntry,
            DEBUG_PROCESS,
            list_entry);

        if (entry)
        {
            if (entry->Process == Process)
            {
                *DebugProcess = entry;
                result = TRUE;
                break;
            }
        }

        /* Move to the next entry */
        NextEntry = NextEntry->Flink;
    }
    ExReleaseFastMutex(&g_DebugProcessList.Mutex);
    return result;
}


EXTERN_C
NTSTATUS DbgkOpenProcessDebugPort(IN PEPROCESS Process,
    IN KPROCESSOR_MODE PreviousMode,
    OUT HANDLE* DebugHandle)
{
    PDEBUG_OBJECT DebugObject;
    PVOID* DebugPort;
    NTSTATUS Status;
    PAGED_CODE();

    DbgPrint("调用了这个函数");

    return STATUS_PORT_NOT_SET;
}

#pragma optimize("", off)
//EXTERN_C
//VOID DbgkCreateThread(IN _ETHREAD* Thread)
//{
//    Debugport_Struct s;
//    PDEBUG_OBJECT Port;
//    PDEBUG_PROCESS DebugProcess;
//    size_t kthread_base = (size_t)Thread + ethread_offset::Tcb;
//    size_t ApcState = kthread_base + kthread_offset::ApcState;
//    size_t ptr_Process = ApcState + kapc_state_offset::Process;
//
//    if (IsDebugTargetProcess(*(_EPROCESS**)ptr_Process, &DebugProcess))
//    {
//        //DbgPrint("DbgkCreateThread ptr_Process: %p", *(_EPROCESS**)ptr_Process);
//        Port = DebugProcess->DebugObject;
//    }
//    else
//    {
//        size_t ptr_DebugPort = *(size_t*)ptr_Process + eprocess_offset::DebugPort;
//        Port = *(PDEBUG_OBJECT*)ptr_DebugPort;
//    }    
//
//    s.id = 0xd7b32a726e23dbc2;  //DbgkCreateThread 经16位md5哈希摘要
//    s.DebugPort = (size_t)Port;
//    Original_DbgkCreateThread(Thread);
//}

EXTERN_C
VOID PspExitThread(_In_ NTSTATUS ExitStatus)
{
    Debugport_Struct s;
    PDEBUG_OBJECT Port;
    PDEBUG_PROCESS DebugProcess;
    _EPROCESS* Process = (_EPROCESS*)PsGetCurrentProcess();

    if (IsDebugTargetProcess(Process, &DebugProcess))
    {
        Port = DebugProcess->DebugObject;
    }
    else
    {
        size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
        Port = *(PDEBUG_OBJECT*)ptr_DebugPort;
    }

    s.id = 0xfbbd6252520ea3d5;  //PspExitThread 经16位md5哈希摘要
    s.DebugPort = (size_t)Port;
    Original_PspExitThread(ExitStatus);
}
#pragma optimize("", on)


EXTERN_C
VOID DbgkCreateThread(IN PETHREAD Thread)
{
    PEPROCESS Process;
    IMAGE_INFO_EX ImageInfoEx = { 0 };
    PIMAGE_NT_HEADERS NtHeader;
    NTSTATUS Status;
    PVOID DebugPort;
    DBGKM_APIMSG ApiMessage = { 0 };
    PDBGKM_CREATE_THREAD CreateThread = &ApiMessage.u.CreateThread;
    PDBGKM_CREATE_PROCESS CreateProcess = &ApiMessage.u.CreateProcess;
    PFILE_OBJECT FileObject = NULL;
    PUNICODE_STRING ImageFileName;
    PPS_SYSTEM_DLL_INFO DllInfo;
    PSECTION Section;
    PCONTROL_AREA ControlArea;
    PDEBUG_PROCESS DebugProcess;
    bool isWoW64Process = false;
    PROCESSFLAGS OldFlags;
    PPSP_SYSTEM_DLL_DATA SystemDllData;

    PAGED_CODE();

    Process = PsGetCurrentProcessByThread(Thread);
    size_t ptr_WoW64Process = (size_t)Process + eprocess_offset::WoW64Process;
    size_t ptr_CurrentMachine = (size_t)Process + eprocess_offset::Machine;
    isWoW64Process = *(size_t*)ptr_WoW64Process && (*(unsigned short*)ptr_CurrentMachine == IMAGE_FILE_MACHINE_I386 || *(unsigned short*)ptr_CurrentMachine == IMAGE_FILE_MACHINE_ARMNT);

    size_t ptr_Flags = (size_t)Process + eprocess_offset::Flags;

    //将ImageNotifyDone和CreateReported置位
    OldFlags.Flags = InterlockedOr((volatile LONG*)ptr_Flags, 0x400001u);

    if (!(OldFlags.ImageNotifyDone) && (*PspNotifyEnableMask & 1 || *PerfGlobalGroupMask & 4))
    {
        ImageInfoEx.Size = 0;
        ImageInfoEx.ImageInfo.Properties = 0;
        ImageInfoEx.ImageInfo.ImageAddressingMode = IMAGE_ADDRESSING_MODE_32BIT;
        size_t ptr_SectionBaseAddress = (size_t)Process + eprocess_offset::SectionBaseAddress;
        ImageInfoEx.ImageInfo.ImageBase = *(PVOID*)ptr_SectionBaseAddress;
        ImageInfoEx.ImageInfo.ImageSize = 0;

        __try
        {
            /* Get the NT Headers */
            NtHeader = RtlImageNtHeader(ImageInfoEx.ImageInfo.ImageBase);
            if (NtHeader)
            {
                /* Set image size */
                ImageInfoEx.ImageInfo.ImageSize = NtHeader->OptionalHeader.SizeOfImage;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ImageInfoEx.ImageInfo.ImageSize = 0;
        }

        ImageInfoEx.ImageInfo.ImageSelector = 0;
        ImageInfoEx.ImageInfo.ImageSectionNumber = 0;

        PsReferenceProcessFilePointer(Process, (PVOID*)&FileObject);
        Status = SeLocateProcessImageName(Process, &ImageFileName);

        if (!NT_SUCCESS(Status))
        {
            ImageFileName = NULL;
        }
        PsCallImageNotifyRoutines(ImageFileName, Process, &ImageInfoEx, FileObject);   //通知模块回调
        if (ImageFileName)
        {
            ExFreePool(ImageFileName);
        }

        ObDereferenceObject(FileObject);

        //Win11 i < 7
        for (int i = 0; i < PsSystemDllTotalTypes; i++)
        {
            DllInfo = PsQuerySystemDllInfo(i);

            if (DllInfo && (i <= 0 || ((DllInfo->Flags & 8) && *(size_t*)ptr_WoW64Process && i == PsWow64GetProcessNtdllType(Process))))
            {
                ImageInfoEx.ImageInfo.Properties = 0;
                ImageInfoEx.ImageInfo.ImageAddressingMode = IMAGE_ADDRESSING_MODE_32BIT;
                ImageInfoEx.ImageInfo.ImageBase = DllInfo->BaseAddress;
                ImageInfoEx.ImageInfo.ImageSize = 0;


                __try
                {
                    NtHeader = RtlImageNtHeader(DllInfo->BaseAddress);
                    if (NtHeader)
                    {
                        ImageInfoEx.ImageInfo.ImageSize = NtHeader->OptionalHeader.SizeOfImage;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    ImageInfoEx.ImageInfo.ImageSize = 0;
                }

                ImageInfoEx.ImageInfo.ImageSelector = 0;
                ImageInfoEx.ImageInfo.ImageSectionNumber = 0;

                SystemDllData = CONTAINING_RECORD(DllInfo, PSP_SYSTEM_DLL_DATA, DllInfo);
                Section = PspReferenceSystemDll(SystemDllData->SystemDll);  //快速引用对象
                ControlArea = MiSectionControlArea(Section);
                FileObject = MiReferenceControlAreaFileWithTag(ControlArea, 'tlfD');
                if (Section)
                {
                    ObFastDereferenceObject(&SystemDllData->SystemDll->DllSection, Section, 'dSsP');
                }
                PsCallImageNotifyRoutines(&DllInfo->DllPath, Process, &ImageInfoEx, FileObject);  //通知模块回调
                ObDereferenceObject(FileObject);
            }
        }
    }

    /* Fail if we have no port */
    if (IsDebugTargetProcess((_EPROCESS*)Process, &DebugProcess))
    {
        DebugPort = DebugProcess->DebugObject;
    }
    else
    {
        size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
        DebugPort = *(PDEBUG_OBJECT*)ptr_DebugPort;
    }

    if (!DebugPort) return;

    //检查报告是否已经创建
    if (!(OldFlags.CreateReported))
    {
        /* Setup the information structure for the new thread */
        CreateProcess->InitialThread.SubSystemKey = 0;
        CreateProcess->InitialThread.StartAddress = NULL;

        /* And for the new process */
        CreateProcess->SubSystemKey = 0;
        size_t ptr_SectionObject = (size_t)Process + eprocess_offset::SectionObject;
        CreateProcess->FileHandle = DbgkpSectionToFileHandle(*(PVOID*)ptr_SectionObject);
        size_t ptr_SectionBaseAddress = (size_t)Process + eprocess_offset::SectionBaseAddress;
        CreateProcess->BaseOfImage = *(PVOID*)ptr_SectionBaseAddress;
        CreateProcess->DebugInfoFileOffset = 0;
        CreateProcess->DebugInfoSize = 0;

        __try
        {
            /* Get the NT Header */
            NtHeader = RtlImageNtHeader(*(PVOID*)ptr_SectionBaseAddress);
            if (NtHeader)
            {
                /* Fill out data from the header */
                if (isWoW64Process)
                {
                    CreateProcess->InitialThread.StartAddress = ULongToPtr(((PIMAGE_NT_HEADERS32)NtHeader)->OptionalHeader.ImageBase +
                        ((PIMAGE_NT_HEADERS32)NtHeader)->OptionalHeader.AddressOfEntryPoint);
                }
                else
                {
                    CreateProcess->InitialThread.StartAddress = (PVOID)(NtHeader->OptionalHeader.ImageBase + NtHeader->OptionalHeader.AddressOfEntryPoint);
                }

                CreateProcess->DebugInfoFileOffset = NtHeader->FileHeader.PointerToSymbolTable;
                CreateProcess->DebugInfoSize = NtHeader->FileHeader.NumberOfSymbols;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            CreateProcess->InitialThread.StartAddress = NULL;
            CreateProcess->DebugInfoFileOffset = 0;
            CreateProcess->DebugInfoSize = 0;
        }

        /* Setup the API Message */
        ApiMessage.h.u1.Length = 0x600038;
        ApiMessage.h.u2.ZeroInit = LPC_DEBUG_EVENT;
        ApiMessage.ApiNumber = DbgKmCreateProcessApi;

        /* Send the message */
        DbgkpSendApiMessage((_EPROCESS*)Process, FALSE, &ApiMessage);

        if (CreateProcess->FileHandle)
        {
            /* Close the handle */
            ObCloseHandle(CreateProcess->FileHandle, KernelMode);
        }
        DbgkSendSystemDllMessages(NULL, NULL, &ApiMessage);
    }
    else
    {
        /* Otherwise, do it just for the thread */
        CreateThread->SubSystemKey = 0;
        size_t ptr_Win32StartAddress = (size_t)Thread + ethread_offset::Win32StartAddress;
        CreateThread->StartAddress = *(PVOID*)ptr_Win32StartAddress;

        /* Setup the API Message */
        ApiMessage.h.u1.Length = 0x400018;
        ApiMessage.h.u2.ZeroInit = LPC_DEBUG_EVENT;
        ApiMessage.ApiNumber = DbgKmCreateThreadApi;

        /* Send the message */
        DbgkpSendApiMessage((_EPROCESS*)Process, TRUE, &ApiMessage);
    }

    size_t ptr_SameThreadPassiveFlags = (size_t)Thread + ethread_offset::SameThreadPassiveFlags;
    if (*(unsigned long*)ptr_SameThreadPassiveFlags & 16)
        DbgkpPostModuleMessages(Process, Thread, NULL);
}

NTSTATUS DbgkpQueueMessage(IN _EPROCESS* Process,
    IN _ETHREAD* Thread,
    IN PDBGKM_APIMSG Message,
    IN ULONG Flags,
    IN PDEBUG_OBJECT TargetObject OPTIONAL)
{
    PDEBUG_EVENT DebugEvent;
    DEBUG_EVENT LocalDebugEvent;
    PDEBUG_OBJECT DebugObject;
    NTSTATUS Status;
    BOOLEAN NewEvent;
    PDEBUG_PROCESS DebugProcess;

    PAGED_CODE();
    DBGKTRACE(DBGK_MESSAGE_DEBUG,
        "Process: %p Thread: %p Message: %p Flags: %lx\n",
        Process, Thread, Message, Flags);

    //DbgBreakPoint();

    /* Check if we have to allocate a debug event */
    NewEvent = (Flags & DEBUG_EVENT_NOWAIT) ? TRUE : FALSE;
    if (NewEvent)
    {
        /* Allocate it */
        DebugEvent = (PDEBUG_EVENT)ExAllocatePoolWithTag(NonPagedPool, sizeof(DEBUG_EVENT), 'EgbD');
        if (!DebugEvent) return STATUS_INSUFFICIENT_RESOURCES;

        /* Set flags */
        DebugEvent->Flags = Flags | DEBUG_EVENT_INACTIVE;

        /* Reference the thread and process */
        ObReferenceObject(Thread);
        ObReferenceObject(Process);

        /* Set the current thread */
        DebugEvent->BackoutThread = (_ETHREAD*)PsGetCurrentThread();

        /* Set the debug object */
        //if (IsDebugTargetProcess((_EPROCESS*)Process, &DebugProcess))
        //    DebugObject = DebugProcess->DebugObject;
        //else
            DebugObject = TargetObject;
    }
    else
    {
        /* Use the debug event on the stack */
        DebugEvent = &LocalDebugEvent;
        DebugEvent->Flags = Flags;

        /* Acquire the port lock */
        ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);

        /* Get the debug object */
        if (IsDebugTargetProcess(Process, &DebugProcess))
        {
            DebugObject = DebugProcess->DebugObject;
        }
        else
        {
            size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
            DebugObject = *(PDEBUG_OBJECT*)ptr_DebugPort;
        }

        /* Check what kind of API message this is */
        switch (Message->ApiNumber)
        {
            /* Process or thread creation */
        case DbgKmCreateThreadApi:
        case DbgKmCreateProcessApi:
        {
            /* Make sure we're not skipping creation messages */
            //size_t ptr_SkipCreationMsg = (size_t)Thread + ethread_offset::CrossThreadFlags;
            //if (*(unsigned long*)ptr_SkipCreationMsg) DebugObject = NULL;
            break;
        }

        /* Process or thread exit */
        case DbgKmExitThreadApi:
        case DbgKmExitProcessApi:
        {
            /* Make sure we're not skipping exit messages */
            //size_t ptr_SkipTerminationMsg = (size_t)Thread + ethread_offset::SkipTerminationMsg;
            //if (*(unsigned long*)ptr_SkipTerminationMsg) DebugObject = NULL;
            break;
        }
        case DbgKmLoadDllApi:
        {
            //size_t ptr_SkipCreationMsg = (size_t)Thread + ethread_offset::SkipCreationMsg;
            //if (*(unsigned long*)ptr_SkipCreationMsg && (Flags & 0x40)) DebugObject = NULL;
            break;
        }
        /* No special handling for other messages */
        default:
            break;
        }
    }

    /* Setup the Debug Event */
    KeInitializeEvent(&DebugEvent->ContinueEvent, SynchronizationEvent, FALSE);
    DebugEvent->Process = Process;
    DebugEvent->Thread = Thread;
    DebugEvent->ApiMsg = *Message;
    //DbgBreakPoint();
    size_t ptr_Cid = (size_t)Thread + ethread_offset::Cid;
    DebugEvent->ClientId = *(CLIENT_ID*)ptr_Cid;

    /* Check if we have a port object */
    if (!DebugObject)
    {
        /* Fail */
        Status = STATUS_PORT_NOT_SET;
    }
    else
    {
        /* Acquire the debug object mutex */
        ExAcquireFastMutex(&DebugObject->Mutex);

        /* Check if a debugger is active */
        if (!(DebugObject->Flags & DEBUG_OBJECT_DELETE_PENDING))
        {
            /* Add the event into the object's list */
            DBGKTRACE(DBGK_MESSAGE_DEBUG, "Inserting: %p %d\n",
                DebugEvent, Message->ApiNumber);
            InsertTailList(&DebugObject->EventList, &DebugEvent->EventList);  //将调试事件插入到事件队列

            /* Check if we have to signal it */
            if (!NewEvent)
            {
                /* Signal it */
                KeSetEvent(&DebugObject->EventsPresent,
                    IO_NO_INCREMENT,
                    FALSE);
            }

            /* Set success */
            Status = STATUS_SUCCESS;
        }
        else
        {
            /* No debugger */
            Status = STATUS_DEBUGGER_INACTIVE;
        }

        /* Release the object lock */
        ExReleaseFastMutex(&DebugObject->Mutex);
    }

    /* Check if we had acquired the port lock */
    if (!NewEvent)
    {
        /* Release it */
        ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);

        /* Check if we got here through success */
        if (NT_SUCCESS(Status))
        {
            //DbgBreakPoint();
            /* Wait on the continue event */
            KeWaitForSingleObject(&DebugEvent->ContinueEvent,
                Executive,
                KernelMode,
                FALSE,
                NULL);

            /* Copy API Message back */
            *Message = DebugEvent->ApiMsg;

            /* Set return status */
            Status = DebugEvent->Status;
        }
    }
    else
    {
        /* Check if we failed */
        if (!NT_SUCCESS(Status))
        {
            /* Dereference the process and thread */
            ObDereferenceObject(Thread);
            ObDereferenceObject(Process);

            /* Free the debug event */
            ExFreePoolWithTag(DebugEvent, 'EgbD');
        }
    }

    /* Return status */
    DBGKTRACE(DBGK_MESSAGE_DEBUG, "Status: %lx\n", Status);
    return Status;
}


//转发异常
BOOLEAN
DbgkForwardException(IN PEXCEPTION_RECORD ExceptionRecord,
    IN BOOLEAN IsUseDebugPort,
    IN BOOLEAN SecondChance)
{
    DBGKM_APIMSG ApiMessage = { 0 };
    PDBGKM_EXCEPTION DbgKmException = &ApiMessage.u.Exception;
    NTSTATUS Status;
    _EPROCESS* Process = (_EPROCESS*)PsGetCurrentProcess();
    PVOID Port;
    BOOLEAN UseLpc = FALSE;
    PDEBUG_PROCESS DebugProcess;
    __int64 v13 = 1;
    HANDLE Handle;

    PAGED_CODE();
    DBGKTRACE(DBGK_EXCEPTION_DEBUG,
        "ExceptionRecord: %p Port: %u\n", ExceptionRecord, IsUseDebugPort);

    /* Setup the API Message */

    ApiMessage.h.u1.Length = 0xD000A8;
    ApiMessage.h.u2.ZeroInit = 8;
    ApiMessage.ApiNumber = DbgKmExceptionApi;

    //DBGKM_FORMAT_API_MSG(ApiMessage, DbgKmExceptionApi, sizeof(*DbgKmException));

    if (SecondChance)
    {
        PsSetProcessFaultInformation(Process, (int*)&v13);
    }

    /* Check if this is to be sent on the debug port */
    if (IsUseDebugPort)  //是否将异常转发到调试端口
    {
        /* Use the debug port, unless the thread is being hidden */
        //outLog(("转发异常........\n"));
        if (IsDebugTargetProcess(Process, &DebugProcess))
        {
            Port = DebugProcess->DebugObject;
            outLog("DbgkDebugObject: %llX", DebugProcess->DebugObject);
        }
        else
        {
            size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
            Port = *(PDEBUG_OBJECT*)ptr_DebugPort;
            //outLog(("走这里了........\n"));
        }
    }
    else
    {
        //否则，使用异常端口
        /* Otherwise, use the exception port */
        ApiMessage.h.u2.ZeroInit = LPC_EXCEPTION;
        UseLpc = TRUE;
        Port = PsCaptureExceptionPort(Process);
    }

    /* Break out if there's no port */
    if (!Port && IsUseDebugPort) return FALSE;

    if (IsDebugTargetProcess(Process, &DebugProcess))
    {
        //DbgPrint("ExceptionRecord->ExceptionCode: %x\n", ExceptionRecord->ExceptionCode);

        if ((ExceptionRecord->ExceptionCode == STATUS_ILLEGAL_INSTRUCTION) ||
            (ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION))
        {
            return FALSE;
        }

        //过滤目标程序触发的单步异常
        //通过检查B0-B3的条件，因为我们不会去设置dr0-dr3的寄存器    
        if ((ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP) ||
            (ExceptionRecord->ExceptionCode == STATUS_WX86_SINGLE_STEP))
        {

            _ETHREAD* Thread = (_ETHREAD*)PsGetCurrentThread();
            size_t kthread_base = (size_t)Thread + ethread_offset::Tcb;
            size_t ptr_TrapFrame = kthread_base + kthread_offset::TrapFrame;
            PKTRAP_FRAME TrapFrame = *(PKTRAP_FRAME*)ptr_TrapFrame;

            Dr6 dr6;
            dr6.flags = TrapFrame->Dr6;
            if (dr6.BS == 0)  //判断是否是tf单步执行
            {
                //在vt中通过向guest注入#DB异常并不会修改dr6寄存器
                if (dr6.B0 || dr6.B1 || dr6.B2 || dr6.B3)
                {
                    //将#DB异常抛给被调试者
                    return FALSE;
                }
            }
            
        }        
    }

    /* Fill out the exception information */
    //DbgKmException->ExceptionRecord = *ExceptionRecord;
    KeCopyExceptionRecord(&ApiMessage.u.Exception.ExceptionRecord, ExceptionRecord);
    DbgKmException->FirstChance = !SecondChance;

    /* Check if we should use LPC */
    if (UseLpc)
    {
        /* Send the message on the LPC Port */
        if (!Port)
        {
            Status = STATUS_SUCCESS;
            ApiMessage.ReturnedStatus = DBG_EXCEPTION_NOT_HANDLED;
        }
        else
        {
            Status = DbgkpSendApiMessageLpc(&ApiMessage, Port, IsUseDebugPort);
            ObDereferenceObject(Port);
        }
    }
    else
    {
        /* Use native debug object */
        Status = DbgkpSendApiMessage(Process, IsUseDebugPort != 0, &ApiMessage);
    }

    /* Check if we failed, and for a debug port, also check the return status */
    if (!(NT_SUCCESS(Status)))
    {
        /* Fail */
        return FALSE;
    }

    if (ApiMessage.ReturnedStatus == DBG_EXCEPTION_NOT_HANDLED)
    {
        if (!IsUseDebugPort)
        {
            Status = DbgkpSendErrorMessage(ExceptionRecord, 2, &ApiMessage);
            return NT_SUCCESS(Status);
        }

        //调试器未能正确处理STATUS_SINGLE_STEP异常，可能是因为监视断点已经被取消
        if ((ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP) ||
            (ExceptionRecord->ExceptionCode == STATUS_WX86_SINGLE_STEP))
        {
            return TRUE;
        }

        /* Fail */
        return FALSE;
    }
    Status = ApiMessage.ReturnedStatus;

    /* Otherwise, we're ok */
    return NT_SUCCESS(Status);
}

//NTSTATUS
//DbgkClearProcessDebugObject(
//    IN _EPROCESS* Process,
//    IN PDEBUG_OBJECT SourceDebugObject
//)
///*++
//
//Routine Description:
//
//    Remove a debug object from a process.
//
//Arguments:
//
//    Process           - Process to be debugged
//    sourceDebugObject - Debug object to detach
//
//Return Value:
//
//    NTSTATUS - Status of call.
//
//--*/
//{
//    NTSTATUS Status;
//    PDEBUG_OBJECT DebugObject;
//    PDEBUG_EVENT DebugEvent;
//    LIST_ENTRY TempList;
//    PLIST_ENTRY Entry;
//
//    PAGED_CODE();
//
//    ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);
//
//    DebugObject = (PDEBUG_OBJECT)Process->DebugPort;
//    if (DebugObject == NULL || (DebugObject != SourceDebugObject && SourceDebugObject != NULL))
//    {
//        DebugObject = NULL;
//        Status = STATUS_PORT_NOT_SET;
//    }
//    else
//    {
//        Process->DebugPort = NULL;
//        Status = STATUS_SUCCESS;
//    }
//    ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);
//
//    if (NT_SUCCESS(Status))
//    {
//        DbgkpMarkProcessPeb(Process);
//    }
//
//    //
//    // Remove any events for this process and wake up the threads.
//    //
//    if (DebugObject)
//    {
//        //
//        // Remove any events and queue them to a temporary queue
//        //
//        InitializeListHead(&TempList);
//
//        ExAcquireFastMutex(&DebugObject->Mutex);
//
//        for (Entry = DebugObject->EventList.Flink;
//            Entry != &DebugObject->EventList;
//            Entry = Entry->Flink)
//        {
//            DebugEvent = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);
//            if (DebugEvent->Process == Process)
//            {
//                RemoveEntryList(&DebugEvent->EventList);
//                InsertTailList(&TempList, &DebugEvent->EventList);
//            }
//        }
//        ExReleaseFastMutex(&DebugObject->Mutex);
//
//        ObDereferenceObject(DebugObject);
//
//        //
//        // Wake up all the removed threads.
//        //
//        while (!IsListEmpty(&TempList))
//        {
//            Entry = RemoveHeadList(&TempList);
//            DebugEvent = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);
//            DebugEvent->Status = STATUS_DEBUGGER_INACTIVE;
//            DbgkpWakeTarget(DebugEvent);
//        }
//    }
//
//    return Status;
//}

EXTERN_C
NTSTATUS
NtRemoveProcessDebug(
    IN HANDLE ProcessHandle,
    IN HANDLE DebugObjectHandle
)
/*++

Routine Description:

    Remove a debug object from a process.

Arguments:

    ProcessHandle - Handle to a process currently being debugged

Return Value:

    NTSTATUS - Status of call.

--*/
{
    NTSTATUS Status;
    PDEBUG_OBJECT DebugObject;
    _EPROCESS* Process;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProcessHandle,
        PROCESS_SET_PORT,
        *PsProcessType,
        KernelMode,
        (PVOID*)&Process,
        NULL);

    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    //if (PreviousMode == UserMode && !(((_EPROCESS*)PsGetCurrentProcess())->Flags2 & PS_PROCESS_FLAGS2_PROTECTED_PROCESS))
    //{
    //    if (Process->Flags2 & PS_PROCESS_FLAGS2_PROTECTED_PROCESS)
    //    {
    //        ObDereferenceObject(Process);
    //        return STATUS_PROCESS_IS_PROTECTED;
    //    }
    //}

    Status = ObReferenceObjectByHandle(DebugObjectHandle,
        DEBUG_PROCESS_ASSIGN,
        Hvm_DbgkDebugObjectType,
        KernelMode,
        (PVOID*)&DebugObject,
        NULL);

    if (NT_SUCCESS(Status)) {
        Status = DbgkClearProcessDebugObject(Process, DebugObject);
        DeleteDebugProcess(DebugObject);
        DbgPrint("DbgkClearProcessDebugObject Status: %x", Status);
        ObDereferenceObject(DebugObject);
    }

    ObDereferenceObject(Process);
    return Status;
}


NTSTATUS DbgkpSetProcessDebugObject(IN _EPROCESS* Process,
    IN PDEBUG_OBJECT DebugObject,
    IN NTSTATUS MsgStatus,
    IN _ETHREAD* LastThread)
{
    NTSTATUS Status;
    LIST_ENTRY TempList;
    BOOLEAN GlobalHeld = FALSE, DoSetEvent = TRUE;
    _ETHREAD* ThisThread, *FirstThread;
    PLIST_ENTRY NextEntry;
    PDEBUG_EVENT DebugEvent;
    _ETHREAD* EventThread;
    _ETHREAD* CurrentThread;
    PDEBUG_PROCESS DebugProcess;

    PAGED_CODE();
    DBGKTRACE(DBGK_PROCESS_DEBUG, "Process: %p DebugObject: %p\n",
        Process, DebugObject);

    //DbgBreakPoint();

    CurrentThread = (_ETHREAD*)PsGetCurrentThread();
    size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;

    /* Initialize the temporary list */
    InitializeListHead(&TempList);

    /* Check if we have a success message */
    if (NT_SUCCESS(MsgStatus))
    {
        /* Then default to STATUS_SUCCESS */
        Status = STATUS_SUCCESS;
    }
    else
    {
        /* No last thread, and set the failure code */
        LastThread = NULL;
        Status = MsgStatus;
    }

    /* Now check what status we have here */
    if (NT_SUCCESS(Status))
    {
        for (;;)
        {
            /* Acquire the global lock */
            GlobalHeld = TRUE;
            ExAcquireFastMutex(&DbgkpProcessDebugPortMutex);

            /* Check if we already have a port */            
            PDEBUG_OBJECT Port = *(PDEBUG_OBJECT*)ptr_DebugPort;
            if (Port)
            {
                /* Set failure */
                // 不空则说明已经设置了
                Status = STATUS_PORT_ALREADY_SET;
                break;
            }
            else
            {
                /* Otherwise, set the port and reference the thread */
                SetDebugTargetProcess(Process, DebugObject);
                if (IsDebugTargetProcess(Process, &DebugProcess))
                {
                    *(PDEBUG_OBJECT*)ptr_DebugPort = NULL;
                }
                else
                {
                    *(PDEBUG_OBJECT*)ptr_DebugPort = DebugObject;
                }
                ObReferenceObject(LastThread);

                /* Get the next thread */
                ThisThread = PsGetNextProcessThread(Process, LastThread);
                if (ThisThread)
                {
                    /* Clear the debug port and release the lock */
                    *(PDEBUG_OBJECT*)ptr_DebugPort = NULL;
                    ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);
                    GlobalHeld = FALSE;

                    /* Dereference the thread */
                    ObDereferenceObject(LastThread);

                    /* Post fake messages */
                    Status = DbgkpPostFakeThreadMessages(Process,
                        DebugObject,
                        ThisThread,
                        &FirstThread,
                        &LastThread);
                    if (!NT_SUCCESS(Status))
                    {
                        /* Clear the last thread */
                        LastThread = NULL;
                        break;
                    }
                    /* Dereference the first thread and re-acquire the lock */
                    ObDereferenceObject(FirstThread);
                }
                else
                {
                    break;
                }
            }
        }
    }

    /* Acquire the debug object's lock */
    ExAcquireFastMutex(&DebugObject->Mutex);

    /* Check our status here */
    if (NT_SUCCESS(Status))
    {
        /* Check if we're disconnected */
        if (DebugObject->Flags & DEBUG_OBJECT_DELETE_PENDING)
        {
            /* Set status */
            *(PDEBUG_OBJECT*)ptr_DebugPort = NULL;
            Status = STATUS_DEBUGGER_INACTIVE;
        }
        else
        {
            /* Set the process flags */
            if (!IsDebugTargetProcess(Process, &DebugProcess))
            {
                size_t ptr_TargetFlags = (size_t)Process + eprocess_offset::Flags;
                InterlockedOr((volatile LONG*)ptr_TargetFlags, 3);// set CreateReported & NoDebugInherit
            }

            /* Reference the debug object */
            ObReferenceObject(DebugObject);
        }
    }

    /* Loop the events list */
    NextEntry = DebugObject->EventList.Flink;
    while (NextEntry != &DebugObject->EventList)
    {
        /* Get the debug event and go to the next entry */
        DebugEvent = CONTAINING_RECORD(NextEntry, DEBUG_EVENT, EventList);
        NextEntry = NextEntry->Flink;
        DBGKTRACE(DBGK_PROCESS_DEBUG, "DebugEvent: %p Flags: %lx TH: %p/%p\n",
            DebugEvent, DebugEvent->Flags,
            DebugEvent->BackoutThread, CurrentThread);

        /* Check for if the debug event queue needs flushing */
        if ((DebugEvent->Flags & DEBUG_EVENT_INACTIVE) &&
            (DebugEvent->BackoutThread == CurrentThread))
        {
            /* Get the event's thread */
            EventThread = DebugEvent->Thread;
            DBGKTRACE(DBGK_PROCESS_DEBUG, "EventThread: %p MsgStatus: %lx\n",
                EventThread, MsgStatus);

            /* Check if the status is success */
            if (NT_SUCCESS(Status))
            {
                /* Check if we couldn't acquire rundown for it */
                if (DebugEvent->Flags & DEBUG_EVENT_PROTECT_FAILED)
                {
                    /* Set the skip termination flag */
                    //PspSetCrossThreadFlag(EventThread, CT_SKIP_TERMINATION_MSG_BIT);

                    /* Insert it into the temp list */
                    RemoveEntryList(&DebugEvent->EventList);
                    InsertTailList(&TempList, &DebugEvent->EventList);
                }
                else
                {
                    /* Do we need to signal the event */
                    if (DoSetEvent)
                    {
                        /* Do it */
                        DebugEvent->Flags &= ~DEBUG_EVENT_INACTIVE;
                        KeSetEvent(&DebugObject->EventsPresent,
                            IO_NO_INCREMENT,
                            FALSE);
                        DoSetEvent = FALSE;
                    }

                    /* Clear the backout thread */
                    DebugEvent->BackoutThread = NULL;

                    /* Set skip flag */
                    //PspSetCrossThreadFlag(EventThread, CT_SKIP_CREATION_MSG_BIT);
                }
            }
            else
            {
                /* Insert it into the temp list */
                RemoveEntryList(&DebugEvent->EventList);
                InsertTailList(&TempList, &DebugEvent->EventList);
            }

            /* Check if the lock is held */
            if (DebugEvent->Flags & DEBUG_EVENT_RELEASE)
            {
                /* Release it */
                DebugEvent->Flags &= ~DEBUG_EVENT_RELEASE;
                size_t ptr_RundownProtect = (size_t)EventThread + ethread_offset::RundownProtect;
                ExReleaseRundownProtection((PEX_RUNDOWN_REF)ptr_RundownProtect);
            }
        }
    }

    /* Release the debug object */
    ExReleaseFastMutex(&DebugObject->Mutex);

    /* Release the global lock if acquired */
    if (GlobalHeld) ExReleaseFastMutex(&DbgkpProcessDebugPortMutex);

    /* Check if there's a thread to dereference */
    if (LastThread) ObDereferenceObject(LastThread);

    /* Loop our temporary list */
    while (!IsListEmpty(&TempList))
    {
        /* Remove the event */
        NextEntry = RemoveHeadList(&TempList);
        DebugEvent = CONTAINING_RECORD(NextEntry, DEBUG_EVENT, EventList);

        /* Wake it */
        DbgkpWakeTarget(DebugEvent);
    }

    /* Check if we got here through success and mark the PEB, then return */
    if (NT_SUCCESS(Status)) DbgkpMarkProcessPeb(Process);
    return Status;
}

//附加调试进程
EXTERN_C
NTSTATUS NtDebugActiveProcess(IN HANDLE ProcessHandle,
    IN HANDLE DebugHandle)
{
    NTSTATUS Status; // eax
    _ETHREAD* LastThread; // [rsp+30h] [rbp-18h]
    _EPROCESS* TargetProcess; // [rsp+60h] [rbp+18h]
    PDEBUG_OBJECT DebugObject; // [rsp+68h] [rbp+20h]

    PAGED_CODE();
    DBGKTRACE(DBGK_PROCESS_DEBUG, "Process: %p Handle: %p\n",
        ProcessHandle, DebugHandle);

    Status = ObReferenceObjectByHandle(ProcessHandle,
        PROCESS_SET_PORT,
        *PsProcessType,
        KernelMode,
        (PVOID*)&TargetProcess,
        NULL);
    if (NT_SUCCESS(Status))
    {
        if (TargetProcess == (_EPROCESS*)PsGetCurrentProcess() || TargetProcess == (_EPROCESS*)PsInitialSystemProcess)
        {
            /* Dereference and fail */
            ObDereferenceObject(TargetProcess);
            return STATUS_ACCESS_DENIED;
        }
        else
        {
            _EPROCESS* CurrentProcess = (_EPROCESS*)PsGetCurrentProcess();
            size_t ptr_CurrentWoW64Process = (size_t)CurrentProcess + eprocess_offset::WoW64Process;
            size_t ptr_CurrentMachine = (size_t)CurrentProcess + eprocess_offset::Machine;

            size_t ptr_TargetWoW64Process = (size_t)TargetProcess + eprocess_offset::WoW64Process;
            size_t ptr_TargetMachine = (size_t)TargetProcess + eprocess_offset::Machine;

            if ((!*(size_t*)ptr_CurrentWoW64Process) ||
                (*(unsigned short*)ptr_CurrentMachine != IMAGE_FILE_MACHINE_I386 && *(unsigned short*)ptr_CurrentMachine != IMAGE_FILE_MACHINE_ARMNT) ||
                (*(size_t*)ptr_TargetWoW64Process && (*(unsigned short*)ptr_TargetMachine == IMAGE_FILE_MACHINE_I386 || *(unsigned short*)ptr_TargetMachine == IMAGE_FILE_MACHINE_ARMNT)))
            {
                Status = ObReferenceObjectByHandle(DebugHandle,
                    DEBUG_PROCESS_ASSIGN,
                    Hvm_DbgkDebugObjectType,
                    KernelMode,
                    (PVOID*)&DebugObject,
                    NULL);
                if (NT_SUCCESS(Status))
                {
                    /* Acquire process rundown */
                    size_t ptr_RundownProtect = (size_t)TargetProcess + eprocess_offset::RundownProtect;
                    if (ExAcquireRundownProtection((PEX_RUNDOWN_REF)ptr_RundownProtect))
                    {
                        Status = DbgkpPostFakeProcessCreateMessages(TargetProcess, DebugObject, &LastThread);
                        Status = DbgkpSetProcessDebugObject(TargetProcess, DebugObject, Status, LastThread);
                        /* Release rundown protection */
                        ExReleaseRundownProtection((PEX_RUNDOWN_REF)ptr_RundownProtect);
                    }
                    else
                    {
                        Status = STATUS_PROCESS_IS_TERMINATING;
                    }
                    ObDereferenceObject(DebugObject);
                }
            }
            else
            {
                Status = STATUS_NOT_SUPPORTED;
            }
        }
        ObDereferenceObject(TargetProcess);
    }
    return Status;
}
//
//VOID
//NTAPI
//DbgkpConvertKernelToUserStateChange(OUT PDBGUI_WAIT_STATE_CHANGE WaitStateChange,
//    IN PDEBUG_EVENT DebugEvent)
//{
//    DBGKTRACE(DBGK_OBJECT_DEBUG, "DebugEvent: %p\n", DebugEvent);
//
//    /* Start by copying the client ID */
//    WaitStateChange->AppClientId = DebugEvent->ClientId;
//
//    /* Now check which kind of event this was */
//    outLog("事件: %d", DebugEvent->ApiMsg.ApiNumber);
//    switch (DebugEvent->ApiMsg.ApiNumber)
//    {
//        /* New process */
//    case DbgKmCreateProcessApi:
//
//        /* Set the right native code */
//        WaitStateChange->NewState = DbgCreateProcessStateChange;
//
//        /* Copy the information */
//        WaitStateChange->StateInfo.CreateProcessInfo.NewProcess =
//            DebugEvent->ApiMsg.u.CreateProcess;
//
//        /* Clear the file handle for us */
//        DebugEvent->ApiMsg.u.CreateProcess.FileHandle = NULL;
//        break;
//
//        /* New thread */
//    case DbgKmCreateThreadApi:
//
//        /* Set the right native code */
//        WaitStateChange->NewState = DbgCreateThreadStateChange;
//
//        /* Copy information */
//        WaitStateChange->StateInfo.CreateThread.NewThread.StartAddress =
//            DebugEvent->ApiMsg.u.CreateThread.StartAddress;
//        WaitStateChange->StateInfo.CreateThread.NewThread.SubSystemKey =
//            DebugEvent->ApiMsg.u.CreateThread.SubSystemKey;
//        break;
//
//        /* Exception (or breakpoint/step) */
//    case DbgKmExceptionApi:
//
//        /* Look at the exception code */
//        if ((NTSTATUS)DebugEvent->ApiMsg.u.Exception.ExceptionRecord.ExceptionCode ==
//            STATUS_BREAKPOINT)
//        {
//            /* Update this as a breakpoint exception */
//            WaitStateChange->NewState = DbgBreakpointStateChange;
//        }
//        else if ((NTSTATUS)DebugEvent->ApiMsg.u.Exception.ExceptionRecord.ExceptionCode ==
//            STATUS_SINGLE_STEP)
//        {
//            /* Update this as a single step exception */
//            WaitStateChange->NewState = DbgSingleStepStateChange;
//        }
//        else
//        {
//            /* Otherwise, set default exception */
//            WaitStateChange->NewState = DbgExceptionStateChange;
//        }
//
//        /* Copy the exception record */
//        WaitStateChange->StateInfo.Exception.ExceptionRecord =
//            DebugEvent->ApiMsg.u.Exception.ExceptionRecord;
//        /* Copy FirstChance flag */
//        WaitStateChange->StateInfo.Exception.FirstChance =
//            DebugEvent->ApiMsg.u.Exception.FirstChance;
//        break;
//
//        /* Process exited */
//    case DbgKmExitProcessApi:
//
//        /* Set the right native code and copy the exit code */
//        WaitStateChange->NewState = DbgExitProcessStateChange;
//        WaitStateChange->StateInfo.ExitProcess.ExitStatus =
//            DebugEvent->ApiMsg.u.ExitProcess.ExitStatus;
//        break;
//
//        /* Thread exited */
//    case DbgKmExitThreadApi:
//
//        /* Set the right native code */
//        WaitStateChange->NewState = DbgExitThreadStateChange;
//        WaitStateChange->StateInfo.ExitThread.ExitStatus =
//            DebugEvent->ApiMsg.u.ExitThread.ExitStatus;
//        break;
//
//        /* DLL Load */
//    case DbgKmLoadDllApi:
//
//        /* Set the native code */
//        WaitStateChange->NewState = DbgLoadDllStateChange;
//
//        /* Copy the data */
//        WaitStateChange->StateInfo.LoadDll = DebugEvent->ApiMsg.u.LoadDll;
//
//        /* Clear the file handle for us */
//        DebugEvent->ApiMsg.u.LoadDll.FileHandle = NULL;
//        break;
//
//        /* DLL Unload */
//    case DbgKmUnloadDllApi:
//
//        /* Set the native code and copy the address */
//        WaitStateChange->NewState = DbgUnloadDllStateChange;
//        WaitStateChange->StateInfo.UnloadDll.BaseAddress = DebugEvent->ApiMsg.u.UnloadDll.BaseAddress;
//        break;
//
//    default:
//        /* Shouldn't happen */
//        ASSERT(FALSE);
//    }
//}

//
////获得句柄
//VOID
//NTAPI
//DbgkpOpenHandles(IN PDBGUI_WAIT_STATE_CHANGE WaitStateChange,
//    IN _EPROCESS* Process,
//    IN _ETHREAD* Thread)
//{
//    NTSTATUS Status;
//    HANDLE Handle;
//    PHANDLE DupHandle;
//    PAGED_CODE();
//    DBGKTRACE(DBGK_OBJECT_DEBUG, "Process: %p Thread: %p State: %lx\n",
//        Process, Thread, WaitStateChange->NewState);
//
//    /* Check which state this is */
//    switch (WaitStateChange->NewState)
//    {
//        /* New thread */
//    case DbgCreateThreadStateChange:
//
//        /* Get handle to thread */
//        Status = ObOpenObjectByPointer(Thread,
//            0,
//            NULL,
//            THREAD_ALL_ACCESS,
//            *PsThreadType,
//            KernelMode,
//            &Handle);
//        if (NT_SUCCESS(Status))
//        {
//            /* Save the thread handle */
//            WaitStateChange->StateInfo.CreateThread.HandleToThread = Handle;
//        }
//        return;
//
//        /* New process */
//    case DbgCreateProcessStateChange:
//
//        /* Get handle to thread */
//        Status = ObOpenObjectByPointer(Thread,
//            0,
//            NULL,
//            THREAD_ALL_ACCESS,
//            *PsThreadType,
//            KernelMode,
//            &Handle);
//        if (NT_SUCCESS(Status))
//        {
//            /* Save the thread handle */
//            WaitStateChange->StateInfo.CreateProcessInfo.HandleToThread = Handle;
//        }
//
//        /* Get handle to process */
//        Status = ObOpenObjectByPointer(Process,
//            0,
//            NULL,
//            PROCESS_ALL_ACCESS,
//            *PsProcessType,
//            KernelMode,
//            &Handle);
//        if (NT_SUCCESS(Status))
//        {
//            /* Save the process handle */
//            WaitStateChange->StateInfo.CreateProcessInfo.HandleToProcess = Handle;
//        }
//
//        /* Fall through to duplicate file handle */
//        DupHandle = &WaitStateChange->StateInfo.CreateProcessInfo.NewProcess.FileHandle;
//        break;
//
//        /* DLL Load */
//    case DbgLoadDllStateChange:
//
//        /* Fall through to duplicate file handle */
//        DupHandle = &WaitStateChange->StateInfo.LoadDll.FileHandle;
//        break;
//
//        /* Anything else has no handles */
//    default:
//        return;
//    }
//
//    /* If we got here, then we have to duplicate a handle, possibly */
//    Handle = *DupHandle;
//    if (Handle)
//    {
//        /* Duplicate it */
//        Status = ObDuplicateObject((_EPROCESS*)PsGetCurrentProcess(),
//            Handle,
//            (_EPROCESS*)PsGetCurrentProcess(),
//            DupHandle,
//            0,
//            0,
//            DUPLICATE_SAME_ACCESS,
//            KernelMode);
//        if (!NT_SUCCESS(Status)) *DupHandle = NULL;
//
//        /* Close the original handle */
//        ObCloseHandle(Handle, KernelMode);
//    }
//}

//取出调试事件
EXTERN_C
NTSTATUS NtWaitForDebugEvent(IN HANDLE DebugHandle,
    IN BOOLEAN Alertable,
    IN PLARGE_INTEGER Timeout OPTIONAL,
    OUT PDBGUI_WAIT_STATE_CHANGE StateChange)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    LARGE_INTEGER LocalTimeOut;
    _EPROCESS* Process;
    LARGE_INTEGER StartTime;
    _ETHREAD* Thread;
    BOOLEAN GotEvent;
    LARGE_INTEGER NewTime;
    PDEBUG_OBJECT DebugObject;
    DBGUI_WAIT_STATE_CHANGE WaitStateChange;
    NTSTATUS Status;
    PDEBUG_EVENT DebugEvent = NULL, DebugEvent2;
    PLIST_ENTRY ListHead, NextEntry, NextEntry2;

    //DbgBreakPoint();

    PAGED_CODE();
    DBGKTRACE(DBGK_OBJECT_DEBUG, "Handle: %p\n", DebugHandle);

    /* Clear the initial wait state change structure and the timeout */
    RtlZeroMemory(&WaitStateChange, sizeof(WaitStateChange));
    LocalTimeOut.QuadPart = 0;

    /* Check if we were called from user mode */
    if (PreviousMode != KernelMode)
    {
        /* Protect probe in SEH */
        _SEH2_TRY
        {
            /* Check if we came with a timeout */
            if (Timeout)
            {
                /* Probe it */
                ProbeForReadLargeInteger(Timeout);

                /* Make a local copy */
                LocalTimeOut = *Timeout;
                Timeout = &LocalTimeOut;
            }

        /* Probe the state change structure */
        ProbeForWrite(StateChange, sizeof(*StateChange), sizeof(ULONG));
        }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        /* Copy directly */
        if (Timeout) LocalTimeOut = *Timeout;
    }

    /* If we were passed a timeout, query the current time */
    if (Timeout) KeQuerySystemTime(&StartTime);

    /* Get the debug object */
    Status = ObReferenceObjectByHandle(DebugHandle,   //根据句柄引用调试对象
        DEBUG_READ_EVENT,
        Hvm_DbgkDebugObjectType,
        PreviousMode,
        (PVOID*)&DebugObject,
        NULL);
    if (!NT_SUCCESS(Status)) return Status;  //获取调试对象失败 则退出

    /* Clear process and thread */
    Process = NULL;
    Thread = NULL;

    /* Wait on the debug object given to us */
    while (TRUE)
    {
        Status = KeWaitForSingleObject(&DebugObject->EventsPresent,  //等待调试事件
            Executive,
            PreviousMode,
            Alertable,
            Timeout);

        if (!NT_SUCCESS(Status) ||
            (Status == STATUS_TIMEOUT) ||
            (Status == STATUS_ALERTED) ||
            (Status == STATUS_USER_APC))
        {
            /* Break out the wait */
            break;  //失败 或者等待超时 则退出
        }

        /* Lock the object */
        GotEvent = FALSE;
        ExAcquireFastMutex(&DebugObject->Mutex);  //加锁

        /* Check if a debugger is connected */
        if (DebugObject->Flags & DEBUG_OBJECT_DELETE_PENDING)  //检查调试器是否已连接
        {
            /* Not connected */
            Status = STATUS_DEBUGGER_INACTIVE;
        }
        else
        {
            /* Loop the events */
            ListHead = &DebugObject->EventList;
            NextEntry = ListHead->Flink;
            while (ListHead != NextEntry)
            {
                /* Get the debug event */
                DebugEvent = CONTAINING_RECORD(NextEntry,
                    DEBUG_EVENT,
                    EventList);
                DBGKTRACE(DBGK_PROCESS_DEBUG, "DebugEvent: %p Flags: %lx\n",
                    DebugEvent, DebugEvent->Flags);

                /* Check flags */
                if (!(DebugEvent->Flags & (DEBUG_EVENT_INACTIVE | DEBUG_EVENT_READ)))
                {
                    /* We got an event */
                    GotEvent = TRUE;

                    /* Loop the list internally */
                    NextEntry2 = DebugObject->EventList.Flink;
                    while (NextEntry2 != NextEntry)
                    {
                        /* Get the debug event */
                        DebugEvent2 = CONTAINING_RECORD(NextEntry2,
                            DEBUG_EVENT,
                            EventList);

                        /* Try to match process IDs */
                        if (DebugEvent2->ClientId.UniqueProcess ==
                            DebugEvent->ClientId.UniqueProcess)
                        {
                            /* Found it, break out */
                            DebugEvent->Flags |= DEBUG_EVENT_INACTIVE;
                            DebugEvent->BackoutThread = NULL;
                            GotEvent = FALSE;
                            break;
                        }

                        /* Move to the next entry */
                        NextEntry2 = NextEntry2->Flink;
                    }

                    /* Check if we still have a valid event */
                    if (GotEvent) break;
                }

                /* Move to the next entry */
                NextEntry = NextEntry->Flink;
            }

            /* Check if we have an event */
            if (GotEvent)
            {
                /* Save and reference the process and thread */
                Process = DebugEvent->Process;
                Thread = DebugEvent->Thread;
                ObReferenceObject(Process);
                ObReferenceObject(Thread);

                /* Convert to user-mode structure */
                DbgkpConvertKernelToUserStateChange(&WaitStateChange, DebugEvent);

                /* Set flag */
                DebugEvent->Flags |= DEBUG_EVENT_READ;
            }
            else
            {
                /* Unsignal the event */
                KeResetEvent(&DebugObject->EventsPresent);
            }

            /* Set success */
            Status = STATUS_SUCCESS;
        }

        /* Release the mutex */
        ExReleaseFastMutex(&DebugObject->Mutex);  //解锁
        if (!NT_SUCCESS(Status)) break;

        /* Check if we got an event */
        if (!GotEvent)
        {
            /* Check if we can wait again */
            if (LocalTimeOut.QuadPart < 0)
            {
                /* Query the new time */
                KeQuerySystemTime(&NewTime);

                /* Substract times */
                LocalTimeOut.QuadPart += (NewTime.QuadPart - StartTime.QuadPart);
                StartTime = NewTime;

                /* Check if we've timed out */
                if (LocalTimeOut.QuadPart >= 0)
                {
                    /* We have, break out of the loop */
                    Status = STATUS_TIMEOUT;
                    break;
                }
            }
        }
        else
        {
            /* Open the handles and dereference the objects */
            DbgkpOpenHandles(&WaitStateChange, Process, Thread);  //获得进线程句柄返回给Ring3
            ObDereferenceObject(Process);  //释放进程对象
            ObDereferenceObject(Thread);   //释放线程对象
            break;
        }
    }

    /* We're done, dereference the object */
    ObDereferenceObject(DebugObject);  //释放调试对象

    /* Protect write with SEH */
    _SEH2_TRY
    {
        /* Return our wait state change structure */
        *StateChange = WaitStateChange;
    }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        /* Get SEH Exception code */
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    /* Return status */
    return Status;
}

//继续运行被暂停的线程
EXTERN_C
NTSTATUS
NtDebugContinue(
    IN HANDLE DebugObjectHandle,
    IN PCLIENT_ID ClientId,
    IN NTSTATUS ContinueStatus)
    /*++

    Routine Description:

        Continues a stalled debugged thread

    Arguments:

        DebugObjectHandle - Handle to a debug object
        ClientId - ClientId of thread tro continue
        ContinueStatus - Status of continue

    Return Value:

        Status of operation

    --*/
{
    NTSTATUS Status;
    PDEBUG_OBJECT DebugObject;
    PDEBUG_EVENT DebugEvent, FoundDebugEvent;
    KPROCESSOR_MODE PreviousMode;
    CLIENT_ID Clid;
    PLIST_ENTRY Entry;
    BOOLEAN GotEvent;

    //DbgBreakPoint();

    PreviousMode = KeGetPreviousMode();

    //如果是用户模式传进来的参数需要探测是否可读写
    __try
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForReadSmallStructure(ClientId, sizeof(*ClientId), sizeof(UCHAR));
        }
        Clid = *ClientId;        

    }
    __except (ExSystemExceptionFilter())  // 如果先前的模式是内核模式，则不处理异常
    {
        return GetExceptionCode();
    }

    switch (ContinueStatus)
    {
    case DBG_EXCEPTION_HANDLED:
    case DBG_EXCEPTION_NOT_HANDLED:
    case DBG_TERMINATE_THREAD:
    case DBG_TERMINATE_PROCESS:
    case DBG_CONTINUE:
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }

    Status = ObReferenceObjectByHandle(DebugObjectHandle,
        DEBUG_READ_EVENT,
        Hvm_DbgkDebugObjectType,
        KernelMode,
        (PVOID*)&DebugObject,
        NULL);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    GotEvent = FALSE;
    FoundDebugEvent = NULL;

    ExAcquireFastMutex(&DebugObject->Mutex);

    for (Entry = DebugObject->EventList.Flink;
        Entry != &DebugObject->EventList;
        Entry = Entry->Flink)
    {

        DebugEvent = CONTAINING_RECORD(Entry, DEBUG_EVENT, EventList);

        //
        // Make sure the client ID matches and that the debugger saw all the events.
        // We don't allow the caller to start a thread that it never saw a message for.
        //
        if (DebugEvent->ClientId.UniqueProcess == Clid.UniqueProcess)
        {
            if (!GotEvent)
            {
                if ((DebugEvent->ClientId.UniqueThread == Clid.UniqueThread) && (DebugEvent->Flags & DEBUG_EVENT_READ))
                {
                    RemoveEntryList(Entry);  //移除自身
                    FoundDebugEvent = DebugEvent;
                    GotEvent = TRUE;
                }
            }
            else
            {
                //
                // VC breaks if it sees more than one event at a time
                // for the same process.
                //
                DebugEvent->Flags &= ~DEBUG_EVENT_INACTIVE;
                KeSetEvent(&DebugObject->EventsPresent, 0, FALSE);
                break;
            }
        }
    }

    ExReleaseFastMutex(&DebugObject->Mutex);

    ObDereferenceObject(DebugObject);

    if (GotEvent)
    {
        //DbgBreakPoint();
        FoundDebugEvent->ApiMsg.ReturnedStatus = ContinueStatus;
        FoundDebugEvent->Status = STATUS_SUCCESS;
        DbgkpWakeTarget(FoundDebugEvent);
    }
    else
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    return Status;
}

//DLL加载
EXTERN_C
VOID
DbgkMapViewOfSection(IN _EPROCESS* Process,
    IN PVOID SectionObject,
    IN PVOID BaseAddress,
    IN ULONG SectionOffset,
    IN ULONG_PTR ViewSize)
{
    _TEB* Teb; // rdi
    PIMAGE_NT_HEADERS NtHeaders; // rax
    _ETHREAD* Thread; // r9
    DBGKM_APIMSG ApiMsg = { 0 }; // [rsp+20h] [rbp-118h]
    PDBGKM_LOAD_DLL LoadDllArgs;
    PVOID DebugPort;
    PDEBUG_PROCESS DebugProcess;
    PVOID lpLdrInitializeThunk;
    KAPC_STATE ApcState;
    HANDLE Handle;
    ACCESS_STATE AccessState;
    NTSTATUS Status;
    SIZE_T RegionSize;
    ULONG OldProtect;

    PAGED_CODE();

    //DbgBreakPoint();

    Thread = (_ETHREAD*)PsGetCurrentThread();

    size_t kthread_base = (size_t)Thread + ethread_offset::Tcb;
    size_t ptr_PreviousMode = kthread_base + kthread_offset::PreviousMode;
    if (*(char*)ptr_PreviousMode)
    {
        //if (!(Thread->CrossThreadFlags & PS_CROSS_THREAD_FLAGS_HIDEFROMDBG))
        {
            if (IsDebugTargetProcess(Process, &DebugProcess))
            {
                DebugPort = DebugProcess->DebugObject;
            }
            else
            {
                size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
                DebugPort = *(PDEBUG_OBJECT*)ptr_DebugPort;
            }

            if (DebugPort)
            {
                //DbgBreakPoint();
                LoadDllArgs = &ApiMsg.u.LoadDll;
                //if (Thread->Tcb.MiscFlags & PS_MISC_FLAGS_SYSTEM_THREAD || Thread->Tcb.ApcStateIndex == 1)
                //{
                //    Teb = NULL;
                //}
                //else
                {
                    size_t ptr_Teb = kthread_base + kthread_offset::Teb;
                    Teb = *(PTEB*)ptr_Teb;
                }

                size_t ptr_Process = kthread_base + kthread_offset::Process;
                if (Teb && (Process == *(_EPROCESS**)ptr_Process))
                {
                    if (DbgkpSuppressDbgMsg(Teb))
                        return;
                    LoadDllArgs->NamePointer = &Teb->NtTib.ArbitraryUserPointer;
                }
                else
                {
                    LoadDllArgs->NamePointer = NULL;
                }

                LoadDllArgs->FileHandle = DbgkpSectionToFileHandle(SectionObject);
                LoadDllArgs->BaseOfDll = BaseAddress;

                __try
                {
                    NtHeaders = RtlImageNtHeader(BaseAddress);
                    if (NtHeaders)
                    {
                        size_t ptr_FileHeader = (size_t)NtHeaders + image_nt_headers64_offset::FileHeader;
                        size_t ptr_PointerToSymbolTable  = ptr_FileHeader + image_file_header_offset::PointerToSymbolTable;
                        size_t ptr_NumberOfSymbols = ptr_FileHeader + image_file_header_offset::NumberOfSymbols;
                        LoadDllArgs->DebugInfoFileOffset = *(unsigned long*)ptr_PointerToSymbolTable;
                        LoadDllArgs->DebugInfoSize = *(unsigned long*)ptr_NumberOfSymbols;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {

                    LoadDllArgs->DebugInfoFileOffset = 0;
                    LoadDllArgs->DebugInfoSize = 0;
                    LoadDllArgs->NamePointer = NULL;

                }

                ApiMsg.h.u1.Length = 0x500028;
                ApiMsg.h.u2.ZeroInit = LPC_DEBUG_EVENT;
                ApiMsg.ApiNumber = DbgKmLoadDllApi;

                DbgkpSendApiMessage(Process, TRUE, &ApiMsg);
                if (LoadDllArgs->FileHandle)
                    ObCloseHandle(LoadDllArgs->FileHandle, KernelMode);
            }
        }
    }
}

//DLL卸载
EXTERN_C
VOID
DbgkUnMapViewOfSection(IN _EPROCESS* Process, IN PVOID BaseAddress)
{
    _ETHREAD* Thread; // r8
    PTEB Teb; // rcx
    DBGKM_APIMSG ApiMsg = { 0 }; // [rsp+20h] [rbp-118h]
    PDEBUG_PROCESS DebugProcess;
    PVOID DebugPort;

    Thread = (_ETHREAD*)KeGetCurrentThread();
    size_t kthread_base = (size_t)Thread + ethread_offset::Tcb;
    size_t ptr_PreviousMode = kthread_base + kthread_offset::PreviousMode;
    if (*(char*)ptr_PreviousMode)
    {        
        //if (!(Thread->CrossThreadFlags & PS_CROSS_THREAD_FLAGS_HIDEFROMDBG))
        {
            if (IsDebugTargetProcess(Process, &DebugProcess))
            {
                DebugPort = DebugProcess->DebugObject;
            }
            else
            {
                size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
                DebugPort = *(PDEBUG_OBJECT*)ptr_DebugPort;
            }

            if (DebugPort)
            {
                //if (Thread->Tcb.MiscFlags & PS_MISC_FLAGS_SYSTEM_THREAD
                //    || Thread->Tcb.ApcStateIndex == 1)
                //{
                //    Teb = NULL;
                //}
                //else
                size_t kthread_base = (size_t)Thread + ethread_offset::Tcb;
                {
                    size_t ptr_Teb = kthread_base + kthread_offset::Teb;
                    Teb = *(PTEB*)ptr_Teb;
                }
                size_t ptr_Process = kthread_base + kthread_offset::Process;
                if (!Teb || (Process != *(_EPROCESS**)ptr_Process) || (!DbgkpSuppressDbgMsg(Teb)))
                {
                    ApiMsg.u.UnloadDll.BaseAddress = BaseAddress;
                    ApiMsg.h.u1.Length = 0x380010;
                    ApiMsg.h.u2.ZeroInit = LPC_DEBUG_EVENT;
                    ApiMsg.ApiNumber = DbgKmUnloadDllApi;
                    DbgkpSendApiMessage(Process, TRUE, &ApiMsg);
                }
            }
        }
    }
}

EXTERN_C
VOID
DbgkExitThread(
    NTSTATUS ExitStatus
)

/*++

Routine Description:

    This function is called when a new thread terminates. At this
    point, the thread will no longer execute in user-mode. No other
    exit processing has occured.

    If a message is sent, then while the thread is awaiting a reply,
    all other threads in the process are suspended.

Arguments:

    ExitStatus - Supplies the ExitStatus of the exiting thread.

Return Value:

    None.

--*/

{
    PVOID Port = NULL;
    DBGKM_APIMSG ApiMsg = { 0 };
    _EPROCESS* Process;
    PETHREAD Thread;
    PDEBUG_PROCESS DebugProcess;

    PAGED_CODE();    

    Process = (_EPROCESS*)PsGetCurrentProcess();

    DbgPrint("线程退出\n");
    PrintProcessName(Process);

    if (IsDebugTargetProcess(Process, &DebugProcess))
    {
        Port = DebugProcess->DebugObject;
    }
    else
    {
        size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
        Port = *(PDEBUG_OBJECT*)ptr_DebugPort;
    }

    if (!Port)
    {
        return;
    }

    Thread = PsGetCurrentThread();

    size_t ptr_CrossThreadFlags = (size_t)Thread + ethread_offset::CrossThreadFlags;
    CROSSTHREADFLAGS CrossThreadFlags = *(CROSSTHREADFLAGS*)ptr_CrossThreadFlags;

    if (CrossThreadFlags.ThreadInserted)
    {
        ApiMsg.u.ExitThread.ExitStatus = ExitStatus;
        ApiMsg.h.u1.Length = 0x34000C;
        ApiMsg.h.u2.ZeroInit = LPC_DEBUG_EVENT;
        ApiMsg.ApiNumber = DbgKmExitThreadApi;
        DbgkpSendApiMessage(Process, TRUE, &ApiMsg);
        DbgPrint("转发线程退出事件\n");
    }
}

EXTERN_C
VOID
DbgkExitProcess(
    NTSTATUS ExitStatus
)

/*++

Routine Description:

    This function is called when a process terminates. The address
    space of the process is still intact, but no threads exist in
    the process.

Arguments:

    ExitStatus - Supplies the ExitStatus of the exiting process.

Return Value:

    None.

--*/

{
    PVOID Port = NULL;
    DBGKM_APIMSG ApiMsg = { 0 };
    _EPROCESS* Process;
    PETHREAD Thread;
    PDEBUG_PROCESS DebugProcess;

    PAGED_CODE();

    DbgPrint("进程退出\n");

    Process = (_EPROCESS*)PsGetCurrentProcess();

    if (IsDebugTargetProcess(Process, &DebugProcess))
    {
        Port = DebugProcess->DebugObject;
    }
    else
    {
        size_t ptr_DebugPort = (size_t)Process + eprocess_offset::DebugPort;
        Port = *(PDEBUG_OBJECT*)ptr_DebugPort;
    }

    if (!Port)
    {
        return;
    }

    //
    // this ensures that other timed lockers of the process will bail
    // since this call is done while holding the process lock, and lock duration
    // is controlled by debugger
    //


    Thread = PsGetCurrentThread();

    size_t ptr_CrossThreadFlags = (size_t)Thread + ethread_offset::CrossThreadFlags;
    CROSSTHREADFLAGS CrossThreadFlags = *(CROSSTHREADFLAGS*)ptr_CrossThreadFlags;

    if (CrossThreadFlags.ThreadInserted)
    {
        size_t ptr_ExitTime = (size_t)Process + eprocess_offset::ExitTime;
        KeQuerySystemTime(ptr_ExitTime);

        ApiMsg.u.ExitProcess.ExitStatus = ExitStatus;
        ApiMsg.h.u1.Length = 0x34000C;
        ApiMsg.h.u2.ZeroInit = LPC_DEBUG_EVENT;
        ApiMsg.ApiNumber = DbgKmExitProcessApi;
        DbgkpSendApiMessage(Process, FALSE, &ApiMsg);        
        PrintProcessName(Process);
    }
}