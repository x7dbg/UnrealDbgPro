#include "../dllmain.h"
#include "../HookCallSet/functionSet.h"
#include "../Globals.h"
#include "../Log/Log.h"
#include "DebugEvent.h"

#ifdef _WIN64

BOOL
WINAPI
NewWaitForDebugEvent(
    __in LPDEBUG_EVENT lpDebugEvent,
    __in DWORD dwMilliseconds
)
{
    BOOL bRet = FALSE;
    BOOL bFlag;
    DWORD dwOldProtect;
    DWORD error = 0;
    BOOL boWow64Process = FALSE;

    if (lpDebugEvent)
    {
        bRet = Sys_WaitForDebugEvent(lpDebugEvent, dwMilliseconds);
        if (bRet)
        {
            //处理非附加调试的情况
            if (g_process_info.isCreate)
            {
                logger.Log("%s[%d] 进程创建状态 isCreate=%d", __func__, __LINE__, g_process_info.isCreate);
                switch (lpDebugEvent->dwDebugEventCode)
                {
                case LOAD_DLL_DEBUG_EVENT:
                {
                    if (!g_SetDbgBreakPoint.boBaseThreadInitThunk)
                    {
                        assert(BaseThreadInitThunk);
                        if (BaseThreadInitThunk)
                        {
                            //设置int3中断
                            UCHAR chBuffer[3] = { 0x90,0xCC,0xEB };
                            PVOID BreakPointAddr = (PVOID)((ULONG_PTR)BaseThreadInitThunk + 4);
                            bFlag = VirtualProtectEx(g_process_info.ProcessHandle, BreakPointAddr, sizeof(chBuffer), PAGE_EXECUTE_READWRITE, &dwOldProtect);
                            if (bFlag)
                            {
                                bFlag = WriteProcessMemory(g_process_info.ProcessHandle, BreakPointAddr, &chBuffer, sizeof(chBuffer), NULL);
                                if (!bFlag)
                                {
                                    error = GetLastError();
                                    outDebug((TCHAR*)_T("[加载 DLL 调试事件] 设置断点失败！(错误码:%d)"), error);
                                }
                                else
                                {
                                    //outDebug((TCHAR*)_T("设置int3中断成功！"));
                                    g_SetDbgBreakPoint.boBaseThreadInitThunk = TRUE;
                                }
                                VirtualProtectEx(g_process_info.ProcessHandle, BreakPointAddr, sizeof(chBuffer), dwOldProtect, &dwOldProtect);
                            }
                            else
                            {
                                error = GetLastError();
                                outDebug((TCHAR*)_T("[加载 DLL 调试事件] 修改内存属性失败！(错误码:%d)"), error);
                            }
                        }
                        else
                        {
                            error = GetLastError();
                            outDebug((TCHAR*)_T("[加载 DLL 调试事件] BaseThreadInitThunk 空指针！(错误码:%d)"), error);
                        }
                    }
                    break;
                }
                case EXCEPTION_DEBUG_EVENT:
                {
                    if ((lpDebugEvent->u.Exception.ExceptionRecord.ExceptionAddress == (PVOID)((ULONG_PTR)BaseThreadInitThunk + 5)) &&
                        (lpDebugEvent->u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT))
                    {
                        g_first_breakpoint = TRUE;
                    }
                    break;
                }
                }
            }
            else
            {
                //处理附加的情况
                if (lpDebugEvent->dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
                {
                    if ((lpDebugEvent->u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP) ||
                        (lpDebugEvent->u.Exception.ExceptionRecord.ExceptionCode == STATUS_WX86_SINGLE_STEP))
                    {
                        HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, false, lpDebugEvent->dwThreadId);
                        if (hThread)
                        {
                            //调用原函数
                            CONTEXT Context = { 0 };
                            Context.ContextFlags = CONTEXT_ALL | CONTEXT_EXTENDED_REGISTERS;
                            BOOL boSuccess = Sys_GetThreadContext(hThread, &Context);
                            if (boSuccess)
                            {
                                Dr6 dr6;
                                dr6.flags = Context.Dr6;
                                if (dr6.BS)  //单步执行
                                {
                                    InterlockedExchange(&g_debug_condition_detected, 2);
                                }
                                else
                                {
                                    InterlockedExchange(&g_debug_condition_detected, 1);
                                }
                            }
                            else
                            {
                                logger.Log("获取线程上下文失败（错误码：%d）", GetLastError());
                            }
                            CloseHandle(hThread);                            
                        }
                    }
                }
            }
        }
    }
    else
    {
        outDebug((TCHAR*)_T("WaitForDebugEvent参数无效！"));
    }
    return bRet;
}

BOOL
WINAPI
NewContinueDebugEvent(
    _In_ DWORD dwProcessId,
    _In_ DWORD dwThreadId,
    _In_ DWORD dwContinueStatus
)
{
    BOOL bFlag;
    DWORD dwOldProtect;
    DWORD error = 0;

    //处理非附加调试的情况
    if (g_process_info.isCreate)
    {
        logger.Log("%s[%d] 进程创建状态 isCreate=%d", __func__, __LINE__, g_process_info.isCreate);
        if (dwContinueStatus == DBG_CONTINUE)
        {
            if (g_first_breakpoint)
            {
                g_first_breakpoint = FALSE;
                assert(BaseThreadInitThunk);
                if (BaseThreadInitThunk)
                {
                    UCHAR chOldBytes[3] = { 0 };
                    PVOID BreakPointAddr = (PVOID)((ULONG_PTR)BaseThreadInitThunk + 4);
                    bFlag = VirtualProtectEx(g_process_info.ProcessHandle, BreakPointAddr, sizeof(chOldBytes), PAGE_EXECUTE_READWRITE, &dwOldProtect);
                    if (bFlag)
                    {
                        bFlag = ReadProcessMemory(g_process_info.ProcessHandle, BreakPointAddr, &chOldBytes, sizeof(chOldBytes), NULL);
                        if (bFlag)
                        {
                            if (chOldBytes[1] == 0xCC)
                            {
                                UCHAR chBuffer[3] = { 0x85,0xC9,0x75 };
                                bFlag = WriteProcessMemory(g_process_info.ProcessHandle, BreakPointAddr, &chBuffer, sizeof(chBuffer), NULL);
                                if (!bFlag)
                                {
                                    error = GetLastError();
                                    outDebug((TCHAR*)_T("[异常调试事件] 移除断点失败！(错误码:%d)"), error);
                                }
                            }
                            VirtualProtectEx(g_process_info.ProcessHandle, BreakPointAddr, sizeof(chOldBytes), dwOldProtect, &dwOldProtect);
                        }
                        else
                        {
                            error = GetLastError();
                            outDebug((TCHAR*)_T("[异常调试事件] 检查内存时失败！(错误码:%d)"), error);
                        }
                    }
                    else
                    {
                        error = GetLastError();
                        outDebug((TCHAR*)_T("[异常调试事件] 修改内存属性失败！(错误码:%d)"), error);
                    }
                }
                else
                {
                    error = GetLastError();
                    outDebug((TCHAR*)_T("[异常调试事件] BaseThreadInitThunk 空指针！(错误码:%d)"), error);
                }
            }
        }
    }
    return Sys_ContinueDebugEvent(dwProcessId, dwThreadId, dwContinueStatus);
}

#else

BOOL
WINAPI
NewWaitForDebugEvent(
    __in LPDEBUG_EVENT lpDebugEvent,
    __in DWORD dwMilliseconds
)
{
    BOOL bRet = FALSE;
    BOOL bFlag;
    DWORD dwOldProtect;
    DWORD error = 0;

    if (lpDebugEvent)
    {
        bRet = Sys_WaitForDebugEvent(lpDebugEvent, dwMilliseconds);
        if (bRet)
        {
            //处理非附加调试的情况
            if (g_process_info.isCreate)
            {
                //Debug event
                switch (lpDebugEvent->dwDebugEventCode)
                {
                case LOAD_DLL_DEBUG_EVENT:
                {
                    if (!g_SetDbgBreakPoint.boBaseThreadInitThunk)
                    {
                        assert(BaseThreadInitThunk);
                        if (BaseThreadInitThunk)
                        {
                            //设置int3中断
                            UCHAR chBuffer[2] = { 0x90,0xCC };
                            bFlag = VirtualProtectEx(g_process_info.ProcessHandle, BaseThreadInitThunk, sizeof(chBuffer), PAGE_EXECUTE_READWRITE, &dwOldProtect);
                            if (bFlag)
                            {
                                bFlag = WriteProcessMemory(g_process_info.ProcessHandle, BaseThreadInitThunk, &chBuffer, sizeof(chBuffer), NULL);
                                if (!bFlag)
                                {
                                    error = GetLastError();
                                    outDebug((TCHAR*)_T("[加载 DLL 调试事件] 设置断点失败！(错误码:%d)"), error);
                                }
                                else
                                {
                                    //outDebug((TCHAR*)_T("设置int3中断成功！"));
                                    g_SetDbgBreakPoint.boBaseThreadInitThunk = TRUE;
                                }
                                VirtualProtectEx(g_process_info.ProcessHandle, BaseThreadInitThunk, sizeof(chBuffer), dwOldProtect, &dwOldProtect);
                            }
                            else
                            {
                                error = GetLastError();
                                outDebug((TCHAR*)_T("[加载 DLL 调试事件] 修改内存属性失败！(错误码:%d)"), error);
                            }
                        }
                        else
                        {
                            error = GetLastError();
                            outDebug((TCHAR*)_T("[加载 DLL 调试事件] BaseThreadInitThunk 空指针！(错误码:%d)"), error);
                        }
                    }
                    break;
                }
                case EXCEPTION_DEBUG_EVENT:
                {
                    if ((lpDebugEvent->u.Exception.ExceptionRecord.ExceptionAddress == (PVOID)((ULONG_PTR)BaseThreadInitThunk + 1)) &&
                        (lpDebugEvent->u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT))
                    {
                        g_first_breakpoint = TRUE;
                    }
                    break;
                }
                }
            }
            else
            {
                //处理附加的情况
                if (lpDebugEvent->dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
                {
                    if (lpDebugEvent->u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP)
                    {
                        HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, false, lpDebugEvent->dwThreadId);
                        if (hThread)
                        {
                            //调用原函数
                            CONTEXT Context = { 0 };
                            Context.ContextFlags = CONTEXT_ALL | CONTEXT_EXTENDED_REGISTERS;
                            BOOL boSuccess = Sys_GetThreadContext(hThread, &Context);
                            if (boSuccess)
                            {
                                Dr6 dr6;
                                dr6.flags = Context.Dr6;
                                if (dr6.BS)  //单步执行
                                {
                                    InterlockedExchange(&g_debug_condition_detected, 2);
                                }
                                else
                                {
                                    InterlockedExchange(&g_debug_condition_detected, 1);
                                }
                            }
                            CloseHandle(hThread);
                        }
                    }
                }
            }
        }
    }
    else
    {
        outDebug((TCHAR*)_T("WaitForDebugEvent参数无效！"));
    }
    return bRet;
}

BOOL
WINAPI
NewContinueDebugEvent(
    _In_ DWORD dwProcessId,
    _In_ DWORD dwThreadId,
    _In_ DWORD dwContinueStatus
)
{
    BOOL bFlag;
    DWORD dwOldProtect;
    DWORD error = 0;

    //处理非附加调试的情况
    if (g_process_info.isCreate)
    {
        if (dwContinueStatus == DBG_CONTINUE)
        {
            if (g_first_breakpoint)
            {
                g_first_breakpoint = FALSE;
                assert(BaseThreadInitThunk);
                if (BaseThreadInitThunk)
                {
                    UCHAR chOldBytes[2] = { 0,0 };
                    bFlag = VirtualProtectEx(g_process_info.ProcessHandle, BaseThreadInitThunk, sizeof(chOldBytes), PAGE_EXECUTE_READWRITE, &dwOldProtect);
                    if (bFlag)
                    {
                        bFlag = ReadProcessMemory(g_process_info.ProcessHandle, BaseThreadInitThunk, &chOldBytes, sizeof(chOldBytes), NULL);
                        if (bFlag)
                        {
                            if (chOldBytes[1] == 0xCC)
                            {
                                UCHAR chBuffer[2] = { 0x8B,0xFF };
                                bFlag = WriteProcessMemory(g_process_info.ProcessHandle, BaseThreadInitThunk, &chBuffer, sizeof(chBuffer), NULL);
                                if (!bFlag)
                                {
                                    error = GetLastError();
                                    outDebug((TCHAR*)_T("[异常调试事件] 移除断点失败！(错误码:%d)"), error);
                                }
                            }
                            VirtualProtectEx(g_process_info.ProcessHandle, BaseThreadInitThunk, sizeof(chOldBytes), dwOldProtect, &dwOldProtect);
                        }
                        else
                        {
                            error = GetLastError();
                                outDebug((TCHAR*)_T("[异常调试事件] 检查内存时失败！(错误码:%d)"), error);
                        }
                    }
                    else
                    {
                        error = GetLastError();
                            outDebug((TCHAR*)_T("[异常调试事件] 修改内存属性失败！(错误码:%d)"), error);
                    }
                }
                else
                {
                    error = GetLastError();
                        outDebug((TCHAR*)_T("[异常调试事件] BaseThreadInitThunk 空指针！(错误码:%d)"), error);
                }
            }
        }
    }
    return Sys_ContinueDebugEvent(dwProcessId, dwThreadId, dwContinueStatus);
}

#endif // _WIN64
