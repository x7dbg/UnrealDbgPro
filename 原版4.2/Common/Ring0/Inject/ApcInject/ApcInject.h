#pragma once

#ifndef _APC_INJECT_H
#define _APC_INJECT_H

#define BB_POOL_TAG 'ApcB'


typedef enum _MEMORY_INFORMATION_CLASS_EX
{
    MemoryBasicInformationEx = 0,
    MemoryWorkingSetInformation = 1,
    MemoryMappedFilenameInformation = 2,
    MemoryRegionInformation = 3,
    MemoryWorkingSetExInformation = 4,
} MEMORY_INFORMATION_CLASS_EX;

typedef enum _PS_PROTECTED_SIGNER
{
    PsProtectedSignerNone = 0,
    PsProtectedSignerAuthenticode = 1,
    PsProtectedSignerCodeGen = 2,
    PsProtectedSignerAntimalware = 3,
    PsProtectedSignerLsa = 4,
    PsProtectedSignerWindows = 5,
    PsProtectedSignerWinTcb = 6,
    PsProtectedSignerMax = 7
} PS_PROTECTED_SIGNER;

typedef enum _PS_PROTECTED_TYPE
{
    PsProtectedTypeNone = 0,
    PsProtectedTypeProtectedLight = 1,
    PsProtectedTypeProtected = 2,
    PsProtectedTypeMax = 3
} PS_PROTECTED_TYPE;

typedef enum _SYSTEM_INFORMATION_CLASS
{
    SystemBasicInformation = 0x0,
    SystemProcessorInformation = 0x1,
    SystemPerformanceInformation = 0x2,
    SystemTimeOfDayInformation = 0x3,
    SystemPathInformation = 0x4,
    SystemProcessInformation = 0x5,
    SystemCallCountInformation = 0x6,
    SystemDeviceInformation = 0x7,
    SystemProcessorPerformanceInformation = 0x8,
    SystemFlagsInformation = 0x9,
    SystemCallTimeInformation = 0xa,
    SystemModuleInformation = 0xb,
    SystemLocksInformation = 0xc,
    SystemStackTraceInformation = 0xd,
    SystemPagedPoolInformation = 0xe,
    SystemNonPagedPoolInformation = 0xf,
    SystemHandleInformation = 0x10,
    SystemObjectInformation = 0x11,
    SystemPageFileInformation = 0x12,
    SystemVdmInstemulInformation = 0x13,
    SystemVdmBopInformation = 0x14,
    SystemFileCacheInformation = 0x15,
    SystemPoolTagInformation = 0x16,
    SystemInterruptInformation = 0x17,
    SystemDpcBehaviorInformation = 0x18,
    SystemFullMemoryInformation = 0x19,
    SystemLoadGdiDriverInformation = 0x1a,
    SystemUnloadGdiDriverInformation = 0x1b,
    SystemTimeAdjustmentInformation = 0x1c,
    SystemSummaryMemoryInformation = 0x1d,
    SystemMirrorMemoryInformation = 0x1e,
    SystemPerformanceTraceInformation = 0x1f,
    SystemObsolete0 = 0x20,
    SystemExceptionInformation = 0x21,
    SystemCrashDumpStateInformation = 0x22,
    SystemKernelDebuggerInformation = 0x23,
    SystemContextSwitchInformation = 0x24,
    SystemRegistryQuotaInformation = 0x25,
    SystemExtendServiceTableInformation = 0x26,
    SystemPrioritySeperation = 0x27,
    SystemVerifierAddDriverInformation = 0x28,
    SystemVerifierRemoveDriverInformation = 0x29,
    SystemProcessorIdleInformation = 0x2a,
    SystemLegacyDriverInformation = 0x2b,
    SystemCurrentTimeZoneInformation = 0x2c,
    SystemLookasideInformation = 0x2d,
    SystemTimeSlipNotification = 0x2e,
    SystemSessionCreate = 0x2f,
    SystemSessionDetach = 0x30,
    SystemSessionInformation = 0x31,
    SystemRangeStartInformation = 0x32,
    SystemVerifierInformation = 0x33,
    SystemVerifierThunkExtend = 0x34,
    SystemSessionProcessInformation = 0x35,
    SystemLoadGdiDriverInSystemSpace = 0x36,
    SystemNumaProcessorMap = 0x37,
    SystemPrefetcherInformation = 0x38,
    SystemExtendedProcessInformation = 0x39,
    SystemRecommendedSharedDataAlignment = 0x3a,
    SystemComPlusPackage = 0x3b,
    SystemNumaAvailableMemory = 0x3c,
    SystemProcessorPowerInformation = 0x3d,
    SystemEmulationBasicInformation = 0x3e,
    SystemEmulationProcessorInformation = 0x3f,
    SystemExtendedHandleInformation = 0x40,
    SystemLostDelayedWriteInformation = 0x41,
    SystemBigPoolInformation = 0x42,
    SystemSessionPoolTagInformation = 0x43,
    SystemSessionMappedViewInformation = 0x44,
    SystemHotpatchInformation = 0x45,
    SystemObjectSecurityMode = 0x46,
    SystemWatchdogTimerHandler = 0x47,
    SystemWatchdogTimerInformation = 0x48,
    SystemLogicalProcessorInformation = 0x49,
    SystemWow64SharedInformationObsolete = 0x4a,
    SystemRegisterFirmwareTableInformationHandler = 0x4b,
    SystemFirmwareTableInformation = 0x4c,
    SystemModuleInformationEx = 0x4d,
    SystemVerifierTriageInformation = 0x4e,
    SystemSuperfetchInformation = 0x4f,
    SystemMemoryListInformation = 0x50,
    SystemFileCacheInformationEx = 0x51,
    SystemThreadPriorityClientIdInformation = 0x52,
    SystemProcessorIdleCycleTimeInformation = 0x53,
    SystemVerifierCancellationInformation = 0x54,
    SystemProcessorPowerInformationEx = 0x55,
    SystemRefTraceInformation = 0x56,
    SystemSpecialPoolInformation = 0x57,
    SystemProcessIdInformation = 0x58,
    SystemErrorPortInformation = 0x59,
    SystemBootEnvironmentInformation = 0x5a,
    SystemHypervisorInformation = 0x5b,
    SystemVerifierInformationEx = 0x5c,
    SystemTimeZoneInformation = 0x5d,
    SystemImageFileExecutionOptionsInformation = 0x5e,
    SystemCoverageInformation = 0x5f,
    SystemPrefetchPatchInformation = 0x60,
    SystemVerifierFaultsInformation = 0x61,
    SystemSystemPartitionInformation = 0x62,
    SystemSystemDiskInformation = 0x63,
    SystemProcessorPerformanceDistribution = 0x64,
    SystemNumaProximityNodeInformation = 0x65,
    SystemDynamicTimeZoneInformation = 0x66,
    SystemCodeIntegrityInformation = 0x67,
    SystemProcessorMicrocodeUpdateInformation = 0x68,
    SystemProcessorBrandString = 0x69,
    SystemVirtualAddressInformation = 0x6a,
    SystemLogicalProcessorAndGroupInformation = 0x6b,
    SystemProcessorCycleTimeInformation = 0x6c,
    SystemStoreInformation = 0x6d,
    SystemRegistryAppendString = 0x6e,
    SystemAitSamplingValue = 0x6f,
    SystemVhdBootInformation = 0x70,
    SystemCpuQuotaInformation = 0x71,
    SystemNativeBasicInformation = 0x72,
    SystemErrorPortTimeouts = 0x73,
    SystemLowPriorityIoInformation = 0x74,
    SystemBootEntropyInformation = 0x75,
    SystemVerifierCountersInformation = 0x76,
    SystemPagedPoolInformationEx = 0x77,
    SystemSystemPtesInformationEx = 0x78,
    SystemNodeDistanceInformation = 0x79,
    SystemAcpiAuditInformation = 0x7a,
    SystemBasicPerformanceInformation = 0x7b,
    SystemQueryPerformanceCounterInformation = 0x7c,
    SystemSessionBigPoolInformation = 0x7d,
    SystemBootGraphicsInformation = 0x7e,
    SystemScrubPhysicalMemoryInformation = 0x7f,
    SystemBadPageInformation = 0x80,
    SystemProcessorProfileControlArea = 0x81,
    SystemCombinePhysicalMemoryInformation = 0x82,
    SystemEntropyInterruptTimingInformation = 0x83,
    SystemConsoleInformation = 0x84,
    SystemPlatformBinaryInformation = 0x85,
    SystemThrottleNotificationInformation = 0x86,
    SystemHypervisorProcessorCountInformation = 0x87,
    SystemDeviceDataInformation = 0x88,
    SystemDeviceDataEnumerationInformation = 0x89,
    SystemMemoryTopologyInformation = 0x8a,
    SystemMemoryChannelInformation = 0x8b,
    SystemBootLogoInformation = 0x8c,
    SystemProcessorPerformanceInformationEx = 0x8d,
    SystemSpare0 = 0x8e,
    SystemSecureBootPolicyInformation = 0x8f,
    SystemPageFileInformationEx = 0x90,
    SystemSecureBootInformation = 0x91,
    SystemEntropyInterruptTimingRawInformation = 0x92,
    SystemPortableWorkspaceEfiLauncherInformation = 0x93,
    SystemFullProcessInformation = 0x94,
    SystemKernelDebuggerInformationEx = 0x95,
    SystemBootMetadataInformation = 0x96,
    SystemSoftRebootInformation = 0x97,
    SystemElamCertificateInformation = 0x98,
    SystemOfflineDumpConfigInformation = 0x99,
    SystemProcessorFeaturesInformation = 0x9a,
    SystemRegistryReconciliationInformation = 0x9b,
    MaxSystemInfoClass = 0x9c,
} SYSTEM_INFORMATION_CLASS;

typedef enum _KAPC_ENVIRONMENT
{
    OriginalApcEnvironment,
    AttachedApcEnvironment,
    CurrentApcEnvironment,
    InsertApcEnvironment
} KAPC_ENVIRONMENT, * PKAPC_ENVIRONMENT;

typedef enum _MI_VAD_TYPE
{
    VadNone,
    VadDevicePhysicalMemory,
    VadImageMap,
    VadAwe,
    VadWriteWatch,
    VadLargePages,
    VadRotatePhysical,
    VadLargePageSection
} MI_VAD_TYPE, * PMI_VAD_TYPE;

typedef enum _MMSYSTEM_PTE_POOL_TYPE
{
    SystemPteSpace,
    NonPagedPoolExpansion,
    MaximumPtePoolTypes
} MMSYSTEM_PTE_POOL_TYPE;


typedef struct _SYSTEM_THREAD_INFORMATION
{
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG WaitTime;
    PVOID StartAddress;
    CLIENT_ID ClientId;
    KPRIORITY Priority;
    LONG BasePriority;
    ULONG ContextSwitches;
    ULONG ThreadState;
    KWAIT_REASON WaitReason;
}SYSTEM_THREAD_INFORMATION, * PSYSTEM_THREAD_INFORMATION;


typedef struct _SYSTEM_PROCESS_INFO
{
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount;
    ULONG NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
    SYSTEM_THREAD_INFORMATION Threads[1];
}SYSTEM_PROCESS_INFO, * PSYSTEM_PROCESS_INFO;

typedef struct _INJECT_BUFFER
{
    UCHAR code[0x200];
    union
    {
        UNICODE_STRING path;
        UNICODE_STRING32 path32;
    };

    wchar_t buffer[488];
    PVOID module;
    ULONG complete;
    NTSTATUS status;
} INJECT_BUFFER, * PINJECT_BUFFER;

typedef struct _PEB32
{
    UCHAR InheritedAddressSpace;
    UCHAR ReadImageFileExecOptions;
    UCHAR BeingDebugged;
    UCHAR BitField;
    ULONG Mutant;
    ULONG ImageBaseAddress;
    ULONG Ldr;
    ULONG ProcessParameters;
    ULONG SubSystemData;
    ULONG ProcessHeap;
    ULONG FastPebLock;
    ULONG AtlThunkSListPtr;
    ULONG IFEOKey;
    ULONG CrossProcessFlags;
    ULONG UserSharedInfoPtr;
    ULONG SystemReserved;
    ULONG AtlThunkSListPtr32;
    ULONG ApiSetMap;
} PEB32, * PPEB32;

typedef struct _PEB_LDR_DATA
{
    ULONG Length;
    UCHAR Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} PEB_LDR_DATA, * PPEB_LDR_DATA;


typedef struct _PEB
{
    UCHAR InheritedAddressSpace;
    UCHAR ReadImageFileExecOptions;
    UCHAR BeingDebugged;
    UCHAR BitField;
    PVOID Mutant;
    PVOID ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
    PVOID ProcessParameters;
    PVOID SubSystemData;
    PVOID ProcessHeap;
    PVOID FastPebLock;
    PVOID AtlThunkSListPtr;
    PVOID IFEOKey;
    PVOID CrossProcessFlags;
    PVOID KernelCallbackTable;
    ULONG SystemReserved;
    ULONG AtlThunkSListPtr32;
    PVOID ApiSetMap;
} PEB, * PPEB;



typedef struct _PEB_LDR_DATA32
{
    ULONG Length;
    UCHAR Initialized;
    ULONG SsHandle;
    LIST_ENTRY32 InLoadOrderModuleList;
    LIST_ENTRY32 InMemoryOrderModuleList;
    LIST_ENTRY32 InInitializationOrderModuleList;
} PEB_LDR_DATA32, * PPEB_LDR_DATA32;

typedef struct _LDR_DATA_TABLE_ENTRY32
{
    LIST_ENTRY32 InLoadOrderLinks;
    LIST_ENTRY32 InMemoryOrderLinks;
    LIST_ENTRY32 InInitializationOrderLinks;
    ULONG DllBase;
    ULONG EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING32 FullDllName;
    UNICODE_STRING32 BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    LIST_ENTRY32 HashLinks;
    ULONG TimeDateStamp;
} LDR_DATA_TABLE_ENTRY32, * PLDR_DATA_TABLE_ENTRY32;

typedef struct _LDR_DATA_TABLE_ENTRY
{
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    USHORT LoadCount;
    USHORT TlsIndex;
    LIST_ENTRY HashLinks;
    ULONG TimeDateStamp;
} LDR_DATA_TABLE_ENTRY, * PLDR_DATA_TABLE_ENTRY;


/// <summary>
/// User-mode memory region
/// </summary>
typedef struct _USER_CONTEXT
{
    UCHAR code[0x1000];             // Code buffer
    union
    {
        UNICODE_STRING ustr;
        UNICODE_STRING32 ustr32;
    };
    wchar_t buffer[0x400];          // Buffer for unicode string


    // Activation context data
    union
    {
        ACTCTXW actx;
        ACTCTXW32 actx32;
    };
    HANDLE hCTX;
    ULONG hCookie;

    PVOID ptr;                      // Tmp data
    union
    {
        NTSTATUS status;            // Last execution status
        PVOID retVal;               // Function return value
        ULONG retVal32;             // Function return value
    };

    //UCHAR tlsBuf[0x100];
} USER_CONTEXT, * PUSER_CONTEXT;

/// <summary>
/// Manual map context
/// </summary>
typedef struct _MMAP_CONTEXT
{
    PEPROCESS pProcess;     // Target process
    PVOID pWorkerBuf;       // Worker thread code buffer
    HANDLE hWorker;         // Worker thread handle
    PETHREAD pWorker;       // Worker thread object
    LIST_ENTRY modules;     // Manual module list
    PUSER_CONTEXT userMem;  // Tmp buffer in user space
    HANDLE hSync;           // APC sync handle
    PKEVENT pSync;          // APC sync object
    PVOID pSetEvent;        // ZwSetEvent address
    PVOID pLoadImage;       // LdrLoadDll address
    BOOLEAN tlsInitialized; // Static TLS was initialized
    BOOLEAN noThreads;      // No threads should be created
} MMAP_CONTEXT, * PMMAP_CONTEXT;

// Image name resolve flags
typedef enum _ResolveFlags
{
    KApiShemaOnly = 1,
    KSkipSxS = 2,
    KFullPath = 4,
} ResolveFlags;

typedef struct _API_SET_NAMESPACE_ARRAY_10
{
    ULONG Version;
    ULONG Size;
    ULONG Flags;
    ULONG Count;
    ULONG Start;
    ULONG End;
    ULONG Unk[2];
} API_SET_NAMESPACE_ARRAY_10, * PAPI_SET_NAMESPACE_ARRAY_10;

typedef struct _API_SET_NAMESPACE_ENTRY_10
{
    ULONG Limit;
    ULONG Size;
} API_SET_NAMESPACE_ENTRY_10, * PAPI_SET_NAMESPACE_ENTRY_10;

typedef struct _API_SET_VALUE_ARRAY_10
{
    ULONG Flags;
    ULONG NameOffset;
    ULONG Unk;
    ULONG NameLength;
    ULONG DataOffset;
    ULONG Count;
} API_SET_VALUE_ARRAY_10, * PAPI_SET_VALUE_ARRAY_10;

typedef struct _API_SET_VALUE_ENTRY_10
{
    ULONG Flags;
    ULONG NameOffset;
    ULONG NameLength;
    ULONG ValueOffset;
    ULONG ValueLength;
} API_SET_VALUE_ENTRY_10, * PAPI_SET_VALUE_ENTRY_10;


typedef struct _NT_PROC_THREAD_ATTRIBUTE_ENTRY
{
    ULONG Attribute;    // PROC_THREAD_ATTRIBUTE_XXX
    SIZE_T Size;
    ULONG_PTR Value;
    ULONG Unknown;
} NT_PROC_THREAD_ATTRIBUTE_ENTRY, * NT_PPROC_THREAD_ATTRIBUTE_ENTRY;

typedef struct _NT_PROC_THREAD_ATTRIBUTE_LIST
{
    ULONG Length;
    NT_PROC_THREAD_ATTRIBUTE_ENTRY Entry[1];
} NT_PROC_THREAD_ATTRIBUTE_LIST, * PNT_PROC_THREAD_ATTRIBUTE_LIST;

typedef struct _SYSTEM_SERVICE_DESCRIPTOR_TABLE
{
    PULONG_PTR ServiceTableBase;
    PULONG ServiceCounterTableBase;
    ULONG_PTR NumberOfServices;
    PUCHAR ParamTableBase;
} SYSTEM_SERVICE_DESCRIPTOR_TABLE, * PSYSTEM_SERVICE_DESCRIPTOR_TABLE;

typedef struct _RTL_PROCESS_MODULE_INFORMATION
{
    HANDLE Section;         // Not filled in
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    UCHAR  FullPathName[MAXIMUM_FILENAME_LENGTH];
} RTL_PROCESS_MODULE_INFORMATION, * PRTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES
{
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, * PRTL_PROCESS_MODULES;


typedef enum _WinVer
{
    WINVER_7 = 0x0610,
    WINVER_7_SP1 = 0x0611,
    WINVER_8 = 0x0620,
    WINVER_81 = 0x0630,
    WINVER_10 = 0x0A00,
    WINVER_10_RS1 = 0x0A01, // Anniversary update
    WINVER_10_RS2 = 0x0A02, // Creators update
    WINVER_10_RS3 = 0x0A03, // Fall creators update
    WINVER_10_RS4 = 0x0A04, // Spring creators update
    WINVER_10_RS5 = 0x0A05, // October 2018 update
    WINVER_10_19H1 = 0x0A06, // May 2019 update 19H1
    WINVER_10_19H2 = 0x0A07, // November 2019 update 19H2
    WINVER_10_20H1 = 0x0A08, // April 2020 update 20H1
} WinVer;

/// <summary>
/// OS-dependent stuff
/// </summary>
typedef struct _DYNAMIC_DATA
{
    WinVer  ver;            // OS version
    ULONG   buildNo;        // OS build revision
    BOOLEAN correctBuild;   // OS kernel build number is correct and supported

    ULONG KExecOpt;         // KPROCESS::ExecuteOptions 
    ULONG Protection;       // EPROCESS::Protection
    ULONG EProcessFlags2;   // EPROCESS::Flags2
    ULONG ObjTable;         // EPROCESS::ObjectTable
    ULONG VadRoot;          // EPROCESS::VadRoot
    ULONG NtProtectIndex;   // NtProtectVirtualMemory SSDT index
    ULONG NtCreateThdExIndex; // NtCreateThreadEx SSDT index
    ULONG NtTermThdIndex;   // NtTerminateThread SSDT index
    ULONG PrevMode;         // KTHREAD::PreviousMode
    ULONG ExitStatus;       // ETHREAD::ExitStatus
    ULONG MiAllocPage;      // MiAllocateDriverPage offset
    ULONG ExRemoveTable;    // Ex(p)RemoveHandleTable offset

    ULONG_PTR DYN_PDE_BASE; // Win10 AU+ relocated PDE base VA
    ULONG_PTR DYN_PTE_BASE; // Win10 AU+ relocated PTE base VA
} DYNAMIC_DATA, * PDYNAMIC_DATA;

typedef struct _THREAD_BASIC_INFORMATION
{
    NTSTATUS ExitStatus;
    PVOID TebBaseAddress;
    CLIENT_ID ClientId;
    ULONG_PTR AffinityMask;
    LONG Priority;
    LONG BasePriority;
} THREAD_BASIC_INFORMATION, * PTHREAD_BASIC_INFORMATION;



typedef struct _API_SET_VALUE_ENTRY_V2 {
    ULONG NameOffset;
    ULONG NameLength;
    ULONG ValueOffset;
    ULONG ValueLength;
} API_SET_VALUE_ENTRY_V2, * PAPI_SET_VALUE_ENTRY_V2;

typedef struct _API_SET_VALUE_ARRAY_V2 {
    ULONG Count;
    API_SET_VALUE_ENTRY_V2 Array[ANYSIZE_ARRAY];
} API_SET_VALUE_ARRAY_V2, * PAPI_SET_VALUE_ARRAY_V2;

typedef struct _API_SET_NAMESPACE_ENTRY_V2 {
    ULONG NameOffset;
    ULONG NameLength;
    ULONG DataOffset;   // API_SET_VALUE_ARRAY
} API_SET_NAMESPACE_ENTRY_V2, * PAPI_SET_NAMESPACE_ENTRY_V2;

typedef struct _API_SET_NAMESPACE_ARRAY_V2 {
    ULONG Version;
    ULONG Count;
    _Field_size_full_(Count) API_SET_NAMESPACE_ENTRY_V2 Array[ANYSIZE_ARRAY];
} API_SET_NAMESPACE_ARRAY_V2, * PAPI_SET_NAMESPACE_ARRAY_V2;


#if defined (_WIN10_)
typedef PAPI_SET_VALUE_ENTRY_10     PAPISET_VALUE_ENTRY;
typedef PAPI_SET_VALUE_ARRAY_10     PAPISET_VALUE_ARRAY;
typedef PAPI_SET_NAMESPACE_ENTRY_10 PAPISET_NAMESPACE_ENTRY;
typedef PAPI_SET_NAMESPACE_ARRAY_10 PAPISET_NAMESPACE_ARRAY;
#else
typedef PAPI_SET_VALUE_ENTRY_V2     PAPISET_VALUE_ENTRY;
typedef PAPI_SET_VALUE_ARRAY_V2     PAPISET_VALUE_ARRAY;
typedef PAPI_SET_NAMESPACE_ENTRY_V2 PAPISET_NAMESPACE_ENTRY;
typedef PAPI_SET_NAMESPACE_ARRAY_V2 PAPISET_NAMESPACE_ARRAY;
#endif


typedef NTSTATUS(NTAPI* fnNtCreateThreadEx)
(
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
    OUT PVOID lpBytesBuffer
    );


typedef VOID(NTAPI* PKNORMAL_ROUTINE)(
    PVOID NormalContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2
    );

typedef VOID(NTAPI* PKKERNEL_ROUTINE)(
    PRKAPC Apc,
    PKNORMAL_ROUTINE* NormalRoutine,
    PVOID* NormalContext,
    PVOID* SystemArgument1,
    PVOID* SystemArgument2
    );

typedef VOID(NTAPI* PKRUNDOWN_ROUTINE)(PRKAPC Apc);

EXTERN_C
NTSYSAPI
NTSTATUS
NTAPI
ZwQueryInformationThread(
    IN HANDLE ThreadHandle,
    IN THREADINFOCLASS ThreadInformationClass,
    OUT PVOID ThreadInformation,
    IN ULONG ThreadInformationLength,
    OUT PULONG ReturnLength OPTIONAL
);

EXTERN_C
NTSYSAPI
PIMAGE_NT_HEADERS
NTAPI
RtlImageNtHeader(PVOID Base);

EXTERN_C
NTSYSAPI
NTSTATUS
NTAPI
ZwQueryInformationProcess(
    IN  HANDLE ProcessHandle,
    IN  PROCESSINFOCLASS ProcessInformationClass,
    OUT PVOID ProcessInformation,
    IN  ULONG ProcessInformationLength,
    IN  PULONG ReturnLength
);

EXTERN_C
NTKERNELAPI
PPEB
NTAPI
PsGetProcessPeb(IN PEPROCESS Process);

EXTERN_C
NTKERNELAPI
VOID
NTAPI
KeInitializeApc(
    IN PKAPC Apc,
    IN PKTHREAD Thread,
    IN KAPC_ENVIRONMENT ApcStateIndex,
    IN PKKERNEL_ROUTINE KernelRoutine,
    IN PKRUNDOWN_ROUTINE RundownRoutine,
    IN PKNORMAL_ROUTINE NormalRoutine,
    IN KPROCESSOR_MODE ApcMode,
    IN PVOID NormalContext
);

EXTERN_C
NTKERNELAPI
BOOLEAN
NTAPI
KeInsertQueueApc(
    PKAPC Apc,
    PVOID SystemArgument1,
    PVOID SystemArgument2,
    KPRIORITY Increment
);

EXTERN_C
NTKERNELAPI
BOOLEAN
NTAPI
KeTestAlertThread(IN KPROCESSOR_MODE AlertMode);

EXTERN_C
NTKERNELAPI
PVOID
NTAPI
PsGetCurrentProcessWow64Process();

EXTERN_C
NTSYSAPI
NTSTATUS
NTAPI
ZwQuerySystemInformation(
    IN SYSTEM_INFORMATION_CLASS SystemInformationClass,
    OUT PVOID SystemInformation,
    IN ULONG SystemInformationLength,
    OUT PULONG ReturnLength OPTIONAL
);

EXTERN_C
NTKERNELAPI
PVOID
NTAPI
PsGetProcessWow64Process(IN PEPROCESS Process);

EXTERN_C
NTKERNELAPI
PVOID
NTAPI
PsGetThreadTeb(IN PETHREAD Thread);

VOID KernelApcInjectCallback(
    PKAPC Apc,
    PKNORMAL_ROUTINE* NormalRoutine,
    PVOID* NormalContext,
    PVOID* SystemArgument1,
    PVOID* SystemArgument2
);

VOID KernelApcPrepareCallback(
    PKAPC Apc,
    PKNORMAL_ROUTINE* NormalRoutine,
    PVOID* NormalContext,
    PVOID* SystemArgument1,
    PVOID* SystemArgument2
);

NTSTATUS BBQueueUserApc(
    IN PETHREAD pThread,
    IN PVOID pUserFunc,
    IN PVOID Arg1,
    IN PVOID Arg2,
    IN PVOID Arg3,
    IN BOOLEAN bForce
);

NTSTATUS BBResolveSxS(
    IN PMMAP_CONTEXT pContext,
    IN PUNICODE_STRING name,
    OUT PUNICODE_STRING resolved
);

ULONG GenCall32V(IN PUCHAR pBuf, IN PVOID pFn, IN INT argc, IN va_list vl);

ULONG GenCall64V(IN PUCHAR pBuf, IN PVOID pFn, IN INT argc, IN va_list vl);

PVOID BBGetModuleExport(IN PVOID pBase, IN PCCHAR name_ord, IN PEPROCESS pProcess, IN PUNICODE_STRING baseName);


NTSTATUS ApcCreateRemoteThread(HANDLE pid, PVOID pUserFunc);

#endif // !_APC_INJECT_H
