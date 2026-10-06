#include "../dllmain.h"
#include "../Globals.h"
#include "../DebugEvent/DebugEvent.h"
#include "../Inject/ShellCode/InjectCode.h"
#include "../Inject/ApcInject/ApcInject.h"
#include "../DebugBreak/DebugBreak.h"
#include "../Channels/DispatchData.h"
#include "../Log/Log.h"
#include "../vmx/vmx.h"
#include "functionSet.h"

#define PAGE_ALIGN(Va) ((PVOID)((ULONG_PTR)(Va) & ~(PAGE_SIZE - 1)))

//#ifdef _WIN64
//#pragma comment(lib, "ntdll_x64.lib")
//#else
//#pragma comment(lib, "ntdll_x86.lib")
//#endif // _WIN64

EXTERN_C void NewDbgUiRemoteBreakin();



NTSTATUS NTAPI NewNtDebugActiveProcess(
    _In_ HANDLE ProcessHandle,
    _In_ HANDLE DebugObjectHandle)
{
    NTSTATUS Status;

    DWORD BytesReturned = 0;
    RING3_PROCESS_INFO proc_info = { 0 };
    RING3_PROCESS_INFO output = { 0 };
    proc_info.ProcessHandle = (ULONG64)ProcessHandle;
    if (SendUserDataToDriver(IOCTL_GET_PROCESS_INFO, 
        &proc_info, 
        sizeof(RING3_PROCESS_INFO),
        &output,
        sizeof(RING3_PROCESS_INFO),
        &BytesReturned))
    {
        g_target_pid = GetProcessId(ProcessHandle);
        g_target_cr3 = output.cr3;

        logger.Log("g_target_cr3: 0x%p", g_target_cr3);
        logger.Log("g_target_pid: %d", g_target_pid);

        if (!g_target_cr3 || !g_target_pid)
        {
            ReportSeriousError("cr3 或 pid为空");
            return STATUS_UNSUCCESSFUL;
        }

        //连接调试器
        Status = Sys_NtDebugActiveProcess(ProcessHandle, DebugObjectHandle);
        return Status;
    }
    else
    {
        ReportSeriousError("无法获取目标进程cr3");
        return STATUS_UNSUCCESSFUL;
    }
}

//此段代码需要被写入到被调试的目标进程里
//VOID
//NTAPI
//NewDbgUiRemoteBreakin(VOID)
//{
//    /* Make sure a debugger is enabled; if so, breakpoint */
//    DbgBreakPoint();
//
//    /* Exit the thread */
//    RtlExitUserThread(STATUS_SUCCESS);
//}

NTSTATUS
NTAPI
NewDbgUiIssueRemoteBreakin(IN HANDLE Process)
{
    HANDLE hThread;
    CLIENT_ID ClientId;
    NTSTATUS Status;

    //logger.Log("开始附加");
    //DWORD dwPid = GetProcessId(Process);

    //logger.Log("目标进程pid: %d", dwPid);

    if (InjectCode(Process))
    {
        return STATUS_SUCCESS;
    }
    else
    {
        return STATUS_UNSUCCESSFUL;
    }

    ///* Create the thread that will do the breakin */
    //Status = RtlCreateUserThread(Process,
    //    NULL,
    //    FALSE,
    //    0,
    //    0,
    //    PAGE_SIZE,
    //    (PUSER_THREAD_START_ROUTINE)&DbgUiRemoteBreakin,
    //    NULL,
    //    &hThread,
    //    &ClientId);

    ///* Close the handle on success */
    //if (NT_SUCCESS(Status)) NtClose(hThread);

    ///* Return status */
    //return Status;
}

NTSTATUS NTAPI NewDbgUiDebugActiveProcess(HANDLE hProcess)
{
    NTSTATUS Status; // ebx

    Status = NtDebugActiveProcess(hProcess, NtCurrentTeb()->DbgSsReserved[1]);
    //if (NT_SUCCESS(Status))
    //{
    //    //在目标进程内触发int3事件
    //    Status = DbgUiIssueRemoteBreakin(hProcess);
    //    if (!NT_SUCCESS(Status))
    //        ZwRemoveProcessDebug(hProcess, NtCurrentTeb()->DbgSsReserved[1]);
    //}
    return Status;
}

NTSTATUS
NTAPI
NewNtCreateUserProcess(
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
)
{
    NTSTATUS Status = Sys_NtCreateUserProcess(ProcessHandle, ThreadHandle, ProcessDesiredAccess, ThreadDesiredAccess, ProcessObjectAttributes, ThreadObjectAttributes,
        ProcessFlags, ThreadFlags, ProcessParameters, CreateInfo, AttributeList);
    if (NT_SUCCESS(Status))
    {
        //DoDebuggerBreak(ProcessHandle);
        g_process_info.ProcessHandle = *ProcessHandle;
        g_process_info.isCreate = TRUE;
        logger.Log("创建调试进程");
    }
    return Status;
}

BOOL
APIENTRY
NewDebugActiveProcess(
    _In_ DWORD dwProcessId
)
{
    logger.Log("附加到目标进程");
    return Sys_DebugActiveProcess(dwProcessId);
}

VOID
WINAPI
NewOutputDebugStringA(
    _In_opt_ LPCSTR lpOutputString
)
{
    return;
    //return Sys_OutputDebugStringA(lpOutputString);
}

VOID
WINAPI
NewOutputDebugStringW(
    _In_opt_ LPCWSTR lpOutputString
)
{
    return;
}

BOOL
WINAPI
NewVirtualProtectEx(
    _In_ HANDLE hProcess,
    _In_ LPVOID lpAddress,
    _In_ SIZE_T dwSize,
    _In_ DWORD flNewProtect,
    _Out_ PDWORD lpflOldProtect
)
{
    //if (flNewProtect == MAP_PROTECT)
    //{
    //    return TRUE;
    //}
    BOOL boResult = Sys_VirtualProtectEx(hProcess, lpAddress, dwSize, flNewProtect, lpflOldProtect);
    if (!boResult)
    {
        return TRUE;

        //if (GetLastError() == 87)
        //{
        //    //可能是由于自映射导致的
        //    *lpflOldProtect = MAP_PROTECT;
        //    return TRUE;
        //}
    }
    return boResult;
}

BOOL
WINAPI
NewWriteProcessMemory(
    _In_ HANDLE hProcess,
    _In_ LPVOID lpBaseAddress,
    _In_reads_bytes_(nSize) LPCVOID lpBuffer,
    _In_ SIZE_T nSize,
    _Out_opt_ SIZE_T* lpNumberOfBytesWritten
)
{    
    bool boSuccess = false;

    if (((*(BYTE*)lpBuffer != 0xCC)) && (nSize == 1))
    {
        //logger.Log("准备移除cc断点  lpBaseAddress: %p   字节: %x", lpBaseAddress, *(BYTE*)lpBuffer);
        INT3BreakpointList.Lock();

        int elementCount = (int)INT3BreakpointList.size();
        for (int i = elementCount - 1; i != -1; i--)
        {
            VT_BREAK_POINT Breakpoint = INT3BreakpointList.at(i);
            if ((lpBaseAddress == (LPVOID)Breakpoint.VirtualAddress) &&
                (*(BYTE*)lpBuffer == Breakpoint.OriginalBytes)) //可能是为了删除断点
            {
                //VT_BREAK_POINT vmcallinfo = { 0 };
                //vmcallinfo.cr3 = Breakpoint.cr3;
                //vmcallinfo.VirtualAddress = Breakpoint.VirtualAddress;
                //vmcallinfo.Size = Breakpoint.Size;
                //vmcallinfo.command = VMCALL_WATCH_DELETE;
                //vmcallinfo.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;
                //vmcallinfo.OriginalBytes = Breakpoint.OriginalBytes;
                //vmcallinfo.watchid = Breakpoint.watchid;

                //vmcallinfo.CPUCount = g_dwNumberOfProcessors;
                //boSuccess = current_vmcall(&vmcallinfo);
                //if (boSuccess)
                //{
                //    logger.Log("删除cc断点");
                //    INT3BreakpointList.erase(it);
                //    break;
                //}
                //else
                //{
                //    logger.Log("删除cc断点失败");
                //}


                VT_BREAK_POINT vmcallinfo = { 0 };
                vmcallinfo.cr3 = Breakpoint.cr3;
                vmcallinfo.VirtualAddress = Breakpoint.VirtualAddress;
                vmcallinfo.PhysicalAddress = Breakpoint.PhysicalAddress;
                vmcallinfo.Size = Breakpoint.Size;
                vmcallinfo.command = VMCALL_WATCH_DELETE;
                vmcallinfo.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;
                vmcallinfo.OriginalBytes = Breakpoint.OriginalBytes;
                vmcallinfo.watchid = Breakpoint.watchid;
                vmcallinfo.CPUCount = g_dwNumberOfProcessors;
                vmcallinfo.pid = g_target_pid;

                DWORD BytesReturned = 0;
                DWORD output = 520;
                if (SendUserDataToDriver(IOCTL_DEL_SOFTWARE_BREAKPOINT,
                    &vmcallinfo,
                    sizeof(VT_BREAK_POINT),
                    &output,
                    sizeof(DWORD),
                    &BytesReturned))
                {
                    if (output == 1998)
                    {
                        boSuccess = true;
                        logger.Log("删除cc断点");
                        INT3BreakpointList.erase(INT3BreakpointList.begin() + i);
                        break;
                    }
                    else
                    {
                        boSuccess = false;
                        ReportSeriousError("删除cc断点失败");
                    }
                }
                else
                {
                    ReportSeriousError("IOCTL_DEL_SOFTWARE_BREAKPOINT 请求失败!");
                }
            }
        }
        INT3BreakpointList.UnLock();

        if (boSuccess)
        {
            return TRUE;
        }
    }

    if ((*(BYTE*)lpBuffer == 0xCC) &&
        (nSize == 1))  /*可能是为了设置cc断点*/
    {
        BYTE OriginalBytes = 0;
        SIZE_T NumberOfBytes = 0;
        if (Sys_ReadProcessMemory(hProcess, lpBaseAddress, &OriginalBytes, 1, &NumberOfBytes))  //备份原字节
        {
            //触发写拷贝
            NumberOfBytes = 0;
            if (Sys_WriteProcessMemory(hProcess, lpBaseAddress, &OriginalBytes, 1, &NumberOfBytes) == FALSE)
            {
                logger.Log("可能有map保护");
            }
            else
            {
                logger.Log("触发写拷贝");
            }


            //VT_BREAK_POINT vmcallinfo = { 0 };
            //vmcallinfo.cr3 = g_target_cr3;
            //vmcallinfo.VirtualAddress = (unsigned __int64)lpBaseAddress;
            //vmcallinfo.Size = 1; //cc断点就1字节
            //vmcallinfo.command = VMCALL_HIDE_SOFTWARE_BREAKPOINT;
            //vmcallinfo.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;
            //vmcallinfo.OriginalBytes = OriginalBytes;

            //vmcallinfo.CPUCount = g_dwNumberOfProcessors;
            //boSuccess = current_vmcall(&vmcallinfo);
            //if (boSuccess)
            //{                
            //    INT3BreakpointList.Lock();
            //    INT3BreakpointList.push_back(vmcallinfo);
            //    INT3BreakpointList.UnLock();
            //    logger.Log("设置int3断点成功  物理页: %x", GET_PFN(vmcallinfo.PhysicalAddress));
            //}



            VT_BREAK_POINT vmcallinfo = { 0 };
            vmcallinfo.cr3 = g_target_cr3;
            vmcallinfo.VirtualAddress = (unsigned __int64)lpBaseAddress;
            vmcallinfo.Size = 1; //cc断点就1字节
            vmcallinfo.command = VMCALL_HIDE_SOFTWARE_BREAKPOINT;
            vmcallinfo.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;
            vmcallinfo.OriginalBytes = OriginalBytes;
            vmcallinfo.CPUCount = g_dwNumberOfProcessors;
            vmcallinfo.pid = g_target_pid;

            DWORD BytesReturned = 0;
            VT_BREAK_POINT output = { 0 };
            if (SendUserDataToDriver(IOCTL_SET_SOFTWARE_BREAKPOINT,
                &vmcallinfo,
                sizeof(VT_BREAK_POINT),
                &output,
                sizeof(VT_BREAK_POINT),
                &BytesReturned))
            {
                if (output.PhysicalAddress)
                {
                    boSuccess = true;

                    INT3BreakpointList.Lock();
                    INT3BreakpointList.push_back(output);
                    INT3BreakpointList.UnLock();
                    logger.Log("设置int3断点成功  物理页: %x", GET_PFN(output.PhysicalAddress));
                }
                else
                {
                    boSuccess = false;
                    ReportSeriousError("设置int3断点失败");
                }
            }
            else
            {
                ReportSeriousError("IOCTL_SET_SOFTWARE_BREAKPOINT 请求失败!");
            }
        }
        if (boSuccess)
        {
            return TRUE;
        }
        else
        {            
            return FALSE;
        }
    }

    return Sys_WriteProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesWritten);
}

BOOL
WINAPI
NewReadProcessMemory(
    _In_ HANDLE hProcess,
    _In_ LPCVOID lpBaseAddress,
    _Out_writes_bytes_to_(nSize, *lpNumberOfBytesRead) LPVOID lpBuffer,
    _In_ SIZE_T nSize,
    _Out_opt_ SIZE_T* lpNumberOfBytesRead
)
{
    bool boSuccess = false;
    if (lpBaseAddress && (nSize <= 2))
    {

        INT3BreakpointList.Lock();

        int elementCount = (int)INT3BreakpointList.size();
        for (int i = elementCount - 1; i != -1; i--)
        {
            VT_BREAK_POINT Breakpoint = INT3BreakpointList.at(i);
            if (lpBaseAddress == (LPVOID)Breakpoint.VirtualAddress) //可能是为了检查断点是否设置成功
            {
                //VT_BREAK_POINT vmcallinfo = { 0 };
                //vmcallinfo.cr3 = Breakpoint.cr3;
                //vmcallinfo.VirtualAddress = (unsigned __int64)lpBaseAddress;
                //vmcallinfo.PhysicalAddress = Breakpoint.PhysicalAddress;
                //vmcallinfo.Size = nSize;
                //vmcallinfo.command = VMCALL_READ_SOFTWARE_BREAKPOINT;
                //vmcallinfo.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;
                //vmcallinfo.OriginalBytes = Breakpoint.OriginalBytes;                

                //vmcallinfo.CPUCount = g_dwNumberOfProcessors;
                //boSuccess = current_vmcall(&vmcallinfo);
                //if (boSuccess)
                //{
                //    //返回输出参数
                //    if (lpBuffer)
                //    {
                //        memcpy(lpBuffer, vmcallinfo.buffer, nSize);
                //    }

                //    if (lpNumberOfBytesRead)
                //    {
                //        *(SIZE_T*)lpNumberOfBytesRead = nSize;
                //    }
                //    break;
                //}


                VT_BREAK_POINT vmcallinfo = { 0 };
                vmcallinfo.cr3 = Breakpoint.cr3;
                vmcallinfo.VirtualAddress = (unsigned __int64)lpBaseAddress;
                vmcallinfo.PhysicalAddress = Breakpoint.PhysicalAddress;
                vmcallinfo.Size = nSize;
                vmcallinfo.command = VMCALL_READ_SOFTWARE_BREAKPOINT;
                vmcallinfo.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;
                vmcallinfo.OriginalBytes = Breakpoint.OriginalBytes;                
                vmcallinfo.CPUCount = g_dwNumberOfProcessors;
                vmcallinfo.pid = g_target_pid;

                DWORD BytesReturned = 0;
                VT_BREAK_POINT output = { 0 };
                if (SendUserDataToDriver(IOCTL_READ_SOFTWARE_BREAKPOINT,
                    &vmcallinfo,
                    sizeof(VT_BREAK_POINT),
                    &output,
                    sizeof(VT_BREAK_POINT),
                    &BytesReturned))
                {
                    if (output.errorCode == 1998)
                    {
                        boSuccess = true;
                        //返回输出参数
                        if (lpBuffer)
                        {
                            memcpy(lpBuffer, output.buffer, nSize);
                        }

                        if (lpNumberOfBytesRead)
                        {
                            *(SIZE_T*)lpNumberOfBytesRead = nSize;
                        }
                        break;
                    }
                    else
                    {
                        boSuccess = false;
                        ReportSeriousError("读取int3断点失败!");
                    }
                }
                else
                {
                    ReportSeriousError("IOCTL_READ_SOFTWARE_BREAKPOINT 请求失败!");
                }
            }
        }
        INT3BreakpointList.UnLock();

        if (boSuccess)
        {
            return boSuccess;
        }        
    }
    return Sys_ReadProcessMemory(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesRead);
}

NTSTATUS
NTAPI
NewNtDebugContinue(
    _In_ HANDLE DebugObjectHandle,
    _In_ PCLIENT_ID ClientId,
    _In_ NTSTATUS ContinueStatus
)
{
    char szBuf[MAX_PATH] = { 0 };
    sprintf(szBuf, "ring3 ClientId: %p\n", ClientId);
    OutputDebugStringA(szBuf);
    return Sys_NtDebugContinue(DebugObjectHandle, ClientId, ContinueStatus);
}

void Hook_DebugActiveProcess()
{
    Sys_DebugActiveProcess = (PFN_DEBUGACTIVEPROCESS)DebugActiveProcess;
    assert(Sys_DebugActiveProcess);
    if (Sys_DebugActiveProcess)
    {
        HookOn((PVOID*)&Sys_DebugActiveProcess, NewDebugActiveProcess, GetCurrentThread());
    }
    else
    {
        logger.Log("DebugActiveProcess空指针");
    }
}

void UnHook_DebugActiveProcess()
{
    if (Sys_DebugActiveProcess)
    {
        HookOff((PVOID*)&Sys_DebugActiveProcess, NewDebugActiveProcess, GetCurrentThread());
    }
}

void Hook_NtDebugActiveProcess()
{
    Sys_NtDebugActiveProcess = (PFN_NTDEBUGACTIVEPROCESS)NtDebugActiveProcess;
    assert(Sys_NtDebugActiveProcess);
    if (Sys_NtDebugActiveProcess)
    {
        HookOn((PVOID*)&Sys_NtDebugActiveProcess, NewNtDebugActiveProcess, GetCurrentThread());
    }
    else
    {
        logger.Log("NtDebugActiveProcess空指针");
    }
}

void UnHook_NtDebugActiveProcess()
{
    if (Sys_NtDebugActiveProcess)
    {
        HookOff((PVOID*)&Sys_NtDebugActiveProcess, NewNtDebugActiveProcess, GetCurrentThread());
    }
}

void Hook_DbgUiIssueRemoteBreakin()
{
    Sys_DbgUiIssueRemoteBreakin = (PFN_DBGUIISSUEREMOTEBREAKIN)DbgUiIssueRemoteBreakin;
    assert(Sys_DbgUiIssueRemoteBreakin);
    if (Sys_DbgUiIssueRemoteBreakin)
    {
        HookOn((PVOID*)&Sys_DbgUiIssueRemoteBreakin, NewDbgUiIssueRemoteBreakin, GetCurrentThread());
    }
    else
    {
        logger.Log("DbgUiIssueRemoteBreakin空指针");
    }
}

void UnHook_DbgUiIssueRemoteBreakin()
{
    if (Sys_DbgUiIssueRemoteBreakin)
    {
        HookOff((PVOID*)&Sys_DbgUiIssueRemoteBreakin, NewDbgUiIssueRemoteBreakin, GetCurrentThread());
    }
}

void Hook_DbgUiDebugActiveProcess()
{
    Sys_DbgUiDebugActiveProcess = (PFN_DBGUIDEBUGACTIVEPROCESS)DbgUiDebugActiveProcess;
    assert(Sys_DbgUiDebugActiveProcess);
    if (Sys_DbgUiDebugActiveProcess)
    {
        HookOn((PVOID*)&Sys_DbgUiDebugActiveProcess, NewDbgUiDebugActiveProcess, GetCurrentThread());
    }
    else
    {
        logger.Log("DbgUiDebugActiveProcess空指针");
    }
}

void UnHook_DbgUiDebugActiveProcess()
{
    if (Sys_DbgUiDebugActiveProcess)
    {
        HookOff((PVOID*)&Sys_DbgUiDebugActiveProcess, NewDbgUiDebugActiveProcess, GetCurrentThread());
    }
}

void Hook_NtCreateUserProcess()
{
    Sys_NtCreateUserProcess = (PFN_NTCREATEUSERPROCESS)NtCreateUserProcess;
    assert(Sys_NtCreateUserProcess);
    if (Sys_NtCreateUserProcess)
    {
        HookOn((PVOID*)&Sys_NtCreateUserProcess, NewNtCreateUserProcess, GetCurrentThread());
    }
    else
    {
        logger.Log("NtCreateUserProcess空指针");
    }
}

void UnHook_NtCreateUserProcess()
{
    if (Sys_NtCreateUserProcess)
    {
        HookOff((PVOID*)&Sys_NtCreateUserProcess, NewNtCreateUserProcess, GetCurrentThread());
    }
}

void Hook_WaitForDebugEvent()
{
    Sys_WaitForDebugEvent = (PFN_WAITFORDEBUGEVENT)WaitForDebugEvent;
    assert(Sys_WaitForDebugEvent);
    if (Sys_WaitForDebugEvent)
    {
        HookOn((PVOID*)&Sys_WaitForDebugEvent, NewWaitForDebugEvent, GetCurrentThread());
    }
    else
    {
        logger.Log("WaitForDebugEvent空指针");
    }
}

void UnHook_WaitForDebugEvent()
{
    if (Sys_WaitForDebugEvent)
    {
        HookOff((PVOID*)&Sys_WaitForDebugEvent, NewWaitForDebugEvent, GetCurrentThread());
    }
}

void Hook_ContinueDebugEvent()
{
    Sys_ContinueDebugEvent = (PFN_CONTINUEDEBUGEVENT)ContinueDebugEvent;
    assert(Sys_ContinueDebugEvent);
    if (Sys_ContinueDebugEvent)
    {
        HookOn((PVOID*)&Sys_ContinueDebugEvent, NewContinueDebugEvent, GetCurrentThread());
    }
    else
    {
        logger.Log("ContinueDebugEvent空指针");
    }
}

void UnHook_ContinueDebugEvent()
{
    if (Sys_ContinueDebugEvent)
    {
        HookOff((PVOID*)&Sys_ContinueDebugEvent, NewContinueDebugEvent, GetCurrentThread());
    }
}

//Hook OutputDebugStringA/W处理敏感日志检测
void Hook_OutputDebugStringA()
{
    Sys_OutputDebugStringA = (PFN_OUTPUTDEBUGSTRINGA)OutputDebugStringA;
    assert(Sys_OutputDebugStringA);
    if (Sys_OutputDebugStringA)
    {
        HookOn((PVOID*)&Sys_OutputDebugStringA, NewOutputDebugStringA, GetCurrentThread());
    }
    else
    {
        logger.Log("OutputDebugStringA空指针");
    }
}

void UnHook_OutputDebugStringA()
{
    if (Sys_OutputDebugStringA)
    {
        HookOff((PVOID*)&Sys_OutputDebugStringA, NewOutputDebugStringA, GetCurrentThread());
    }
}

void Hook_OutputDebugStringW()
{
    Sys_OutputDebugStringW = (PFN_OUTPUTDEBUGSTRINGW)OutputDebugStringW;
    assert(Sys_OutputDebugStringW);
    if (Sys_OutputDebugStringW)
    {
        HookOn((PVOID*)&Sys_OutputDebugStringW, NewOutputDebugStringW, GetCurrentThread());
    }
    else
    {
        logger.Log("OutputDebugStringW空指针");
    }
}

void UnHook_OutputDebugStringW()
{
    if (Sys_OutputDebugStringW)
    {
        HookOff((PVOID*)&Sys_OutputDebugStringW, NewOutputDebugStringW, GetCurrentThread());
    }
}

void Hook_SetThreadContext()
{
    Sys_SetThreadContext = (PFN_SETTHREADCONTEXT)SetThreadContext;
    assert(Sys_SetThreadContext);
    if (Sys_SetThreadContext)
    {
        HookOn((PVOID*)&Sys_SetThreadContext, NewSetThreadContext, GetCurrentThread());
    }
    else
    {
        logger.Log("SetThreadContext空指针");
    }
}

void UnHook_SetThreadContext()
{
    if (Sys_SetThreadContext)
    {
        HookOff((PVOID*)&Sys_SetThreadContext, NewSetThreadContext, GetCurrentThread());
    }
}

void Hook_GetThreadContext()
{
    Sys_GetThreadContext = (PFN_GETTHREADCONTEXT)GetThreadContext;
    assert(Sys_GetThreadContext);
    if (Sys_GetThreadContext)
    {
        HookOn((PVOID*)&Sys_GetThreadContext, NewGetThreadContext, GetCurrentThread());
    }
    else
    {
        logger.Log("Sys_GetThreadContext空指针");
    }
}

void UnHook_GetThreadContext()
{
    if (Sys_GetThreadContext)
    {
        HookOff((PVOID*)&Sys_GetThreadContext, NewGetThreadContext, GetCurrentThread());
    }
}


void Hook_VirtualProtectEx()
{
    Sys_VirtualProtectEx = (PFN_VIRTUALPROTECTEX)VirtualProtectEx;
    assert(Sys_VirtualProtectEx);
    if (Sys_VirtualProtectEx)
    {
        HookOn((PVOID*)&Sys_VirtualProtectEx, NewVirtualProtectEx, GetCurrentThread());
    }
    else
    {
        logger.Log("Sys_VirtualProtectEx空指针");
    }
}

void UnHook_VirtualProtectEx()
{
    if (Sys_VirtualProtectEx)
    {
        HookOff((PVOID*)&Sys_VirtualProtectEx, NewVirtualProtectEx, GetCurrentThread());
    }
}

void Hook_WriteProcessMemory()
{
    Sys_WriteProcessMemory = (PFN_WRITEPROCESSMEMORY)WriteProcessMemory;
    assert(Sys_WriteProcessMemory);
    if (Sys_WriteProcessMemory)
    {
        HookOn((PVOID*)&Sys_WriteProcessMemory, NewWriteProcessMemory, GetCurrentThread());
    }
    else
    {
        logger.Log("Sys_WriteProcessMemory空指针");
    }    
}

void UnHook_WriteProcessMemory()
{
    if (Sys_WriteProcessMemory)
    {
        HookOff((PVOID*)&Sys_WriteProcessMemory, NewWriteProcessMemory, GetCurrentThread());
    }
}

void Hook_ReadProcessMemory()
{
    Sys_ReadProcessMemory = (PFN_READPROCESSMEMORY)ReadProcessMemory;
    assert(Sys_ReadProcessMemory);
    if (Sys_ReadProcessMemory)
    {
        HookOn((PVOID*)&Sys_ReadProcessMemory, NewReadProcessMemory, GetCurrentThread());
    }
    else
    {
        logger.Log("Sys_ReadProcessMemory空指针");
    }
}

void UnHook_ReadProcessMemory()
{
    if (Sys_ReadProcessMemory)
    {
        HookOff((PVOID*)&Sys_ReadProcessMemory, NewReadProcessMemory, GetCurrentThread());
    }
}

void Hook_NtDebugContinue()
{
    Sys_NtDebugContinue = (PFN_NTDEBUGCONTINUE)NtDebugContinue;
    assert(Sys_NtDebugContinue);
    if (Sys_NtDebugContinue)
    {
        HookOn((PVOID*)&Sys_NtDebugContinue, NewNtDebugContinue, GetCurrentThread());
    }
    else
    {
        logger.Log("Sys_NtDebugContinue空指针");
    }
}

void UnHook_NtDebugContinue()
{
    if (Sys_NtDebugContinue)
    {
        HookOff((PVOID*)&Sys_NtDebugContinue, NewNtDebugContinue, GetCurrentThread());
    }
}


/// <summary>
/// 测试用的
/// </summary>
void NewLdrInitializeThunk(PCONTEXT ContextRecord, PVOID SystemArgument1)
{
    //logger.Log("LdrInitializeThunk 执行了 ContextRecord: %p   SystemArgument1: %p", ContextRecord, SystemArgument1);
    //logger.Log("rax: %p", ContextRecord->Rax);
    //logger.Log("rbx: %p", ContextRecord->Rbx);
    //logger.Log("rcx: %p", ContextRecord->Rcx);
    //logger.Log("rdx: %p", ContextRecord->Rdx);
    //logger.Log("rdi: %p", ContextRecord->Rdi);
    //logger.Log("rsi: %p", ContextRecord->Rsi);
    //logger.Log("rsp: %p", ContextRecord->Rsp);
    //LdrInitializeThunk(ContextRecord, SystemArgument1);
}

void Hook_LdrInitializeThunk()
{
    LdrInitializeThunk = (PFN_LDRINITIALIZETHUNK)GetProcAddress(GetModuleHandle(_T("ntdll.dll")), "LdrInitializeThunk");
    assert(LdrInitializeThunk);
    if (LdrInitializeThunk)
    {
        HookOn((PVOID*)&LdrInitializeThunk, NewLdrInitializeThunk, GetCurrentThread());
    }
    else
    {
        logger.Log("LdrInitializeThunk空指针");
    }
}