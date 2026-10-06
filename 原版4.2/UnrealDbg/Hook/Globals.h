#pragma once

#ifndef _GLOBALS_H
#define _GLOBALS_H

#define PAGE_SHIFT 12L
#define GET_PFN(_VAR_) (_VAR_ >> PAGE_SHIFT)

#define MAP_PROTECT  0x66666666

typedef struct _SET_DBG_BREAKPOINT {
	BOOL boLdrInitializeThunk;
	BOOL boBaseThreadInitThunk;
} SET_DBG_BREAKPOINT, * PSET_DBG_BREAKPOINT;

typedef struct _PROCESS_INFO {
	HANDLE ProcessHandle;
	BOOL isCreate;  //是通过创建调试
} PROCESS_INFO, * PPROCESS_INFO;


typedef BOOL(APIENTRY* PFN_DEBUGACTIVEPROCESS)(
    _In_ DWORD dwProcessId
);

typedef NTSTATUS(NTAPI* PFN_NTDEBUGACTIVEPROCESS)(_In_ HANDLE ProcessHandle, _In_ HANDLE DebugObjectHandle);

typedef NTSTATUS(NTAPI* PFN_DBGUIISSUEREMOTEBREAKIN)(IN HANDLE Process);

typedef NTSTATUS(NTAPI* PFN_NTCREATEUSERPROCESS)(
    _Out_ PHANDLE ProcessHandle,
    _Out_ PHANDLE ThreadHandle,
    _In_ ACCESS_MASK ProcessDesiredAccess,
    _In_ ACCESS_MASK ThreadDesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ProcessObjectAttributes,
    _In_opt_ POBJECT_ATTRIBUTES ThreadObjectAttributes,
    _In_ ULONG ProcessFlags,
    _In_ ULONG ThreadFlags,
    _In_ PRTL_USER_PROCESS_PARAMETERS ProcessParameters,
    _Inout_ PPS_CREATE_INFO CreateInfo,
    _In_ PPS_ATTRIBUTE_LIST AttributeList
    );

typedef BOOL(WINAPI* PFN_WAITFORDEBUGEVENT)(
    __in LPDEBUG_EVENT lpDebugEvent,
    __in DWORD dwMilliseconds
    );

typedef BOOL(WINAPI* PFN_CONTINUEDEBUGEVENT)(
    _In_ DWORD dwProcessId,
    _In_ DWORD dwThreadId,
    _In_ DWORD dwContinueStatus
    );

typedef void(WINAPI* PFN_LDRINITIALIZETHUNK)(PVOID ContextRecord, PVOID SystemArgument1);

typedef VOID(WINAPI* PFN_OUTPUTDEBUGSTRINGA)(
    _In_opt_ LPCSTR lpOutputString
);

typedef VOID(WINAPI* PFN_OUTPUTDEBUGSTRINGW)(
    _In_opt_ LPCWSTR lpOutputString
);

typedef NTSTATUS(NTAPI* PFN_DBGUIDEBUGACTIVEPROCESS)(
    _In_ HANDLE Process
);

typedef BOOL(WINAPI* PFN_SETTHREADCONTEXT)(
    _In_ HANDLE hThread,
    _In_ CONST CONTEXT* lpContext
);

typedef BOOL(WINAPI* PFN_GETTHREADCONTEXT)(
    _In_ HANDLE hThread,
    _Inout_ LPCONTEXT lpContext
);

typedef BOOL(WINAPI* PFN_VIRTUALPROTECTEX)(
    _In_ HANDLE hProcess,
    _In_ LPVOID lpAddress,
    _In_ SIZE_T dwSize,
    _In_ DWORD flNewProtect,
    _Out_ PDWORD lpflOldProtect
);

typedef BOOL(WINAPI* PFN_WRITEPROCESSMEMORY)(
    _In_ HANDLE hProcess,
    _In_ LPVOID lpBaseAddress,
    _In_reads_bytes_(nSize) LPCVOID lpBuffer,
    _In_ SIZE_T nSize,
    _Out_opt_ SIZE_T* lpNumberOfBytesWritten
);

typedef BOOL(WINAPI* PFN_READPROCESSMEMORY)(
    _In_ HANDLE hProcess,
    _In_ LPCVOID lpBaseAddress,
    _Out_writes_bytes_to_(nSize, *lpNumberOfBytesRead) LPVOID lpBuffer,
    _In_ SIZE_T nSize,
    _Out_opt_ SIZE_T* lpNumberOfBytesRead
);

typedef NTSTATUS(NTAPI* PFN_NTDEBUGCONTINUE)(
    _In_ HANDLE DebugObjectHandle,
    _In_ PCLIENT_ID ClientId,
    _In_ NTSTATUS ContinueStatus
);


extern DWORD g_dwNumberOfProcessors;  //逻辑处理器数量
extern LONG g_debug_condition_detected;  //记录是TF单步执行，还是drx断点
extern DWORD g_target_pid;  //目标进程的pid
extern ULONG64 g_target_cr3; //目标进程的cr3
extern vectorExt<BREAKPOINT_RECORD> BreakpointList;
extern vectorExt<VT_BREAK_POINT> INT3BreakpointList;
extern HANDLE g_hGeneralDriverDevice;
extern BOOL g_first_breakpoint;
extern PROCESS_INFO g_process_info;
extern SET_DBG_BREAKPOINT g_SetDbgBreakPoint;
extern PFN_LDRINITIALIZETHUNK LdrInitializeThunk;
extern PVOID BaseThreadInitThunk;
extern PVOID KiUserApcDispatcher;

extern PFN_DEBUGACTIVEPROCESS Sys_DebugActiveProcess;
extern PFN_NTDEBUGACTIVEPROCESS Sys_NtDebugActiveProcess;
extern PFN_DBGUIISSUEREMOTEBREAKIN Sys_DbgUiIssueRemoteBreakin;
extern PFN_NTCREATEUSERPROCESS Sys_NtCreateUserProcess;
extern PFN_WAITFORDEBUGEVENT Sys_WaitForDebugEvent;
extern PFN_CONTINUEDEBUGEVENT Sys_ContinueDebugEvent;
extern PFN_OUTPUTDEBUGSTRINGA Sys_OutputDebugStringA;
extern PFN_OUTPUTDEBUGSTRINGW Sys_OutputDebugStringW;
extern PFN_DBGUIDEBUGACTIVEPROCESS Sys_DbgUiDebugActiveProcess;
extern PFN_SETTHREADCONTEXT Sys_SetThreadContext;
extern PFN_GETTHREADCONTEXT Sys_GetThreadContext;
extern PFN_VIRTUALPROTECTEX Sys_VirtualProtectEx;
extern PFN_WRITEPROCESSMEMORY Sys_WriteProcessMemory;
extern PFN_READPROCESSMEMORY Sys_ReadProcessMemory;
extern PFN_NTDEBUGCONTINUE Sys_NtDebugContinue;

#endif // !_GLOBALS_H
