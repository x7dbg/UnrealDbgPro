#include "../dllmain.h"
#include "../Globals.h"
#include "../Channels/DispatchData.h"
#include "../vmx/vmx.h"
#include "../Log/Log.h"
#include "DebugBreak.h"

BOOL
WINAPI
NewSetThreadContext(
    _In_ HANDLE hThread,
    _In_ CONTEXT* lpContext
)
{
    //CE在首次附加时会将drx内的值清空，这种设计会导致有硬件断点占坑的程序检测到
    if (lpContext->Dr0)
    {        
        //我们可以通过解析dr7来获得用户的意图，比如监视读，监视写，监视执行。
        //logger.Log("lpContext->Dr0: %p", lpContext->Dr0);

        //Vol3B[18.2 DEBUG REGISTERS]
        union {
            size_t flags;
            struct
            {
                size_t L0 : 1; //bit0
                size_t G0 : 1; //bit1
                size_t L1 : 1; //bit2
                size_t G1 : 1; //bit3
                size_t L2 : 1; //bit4
                size_t G2 : 1; //bit5
                size_t L3 : 1; //bit6
                size_t G3 : 1; //bit7
                size_t LE : 1; //bit8
                size_t GE : 1; //bit9
                size_t Reserved_1 : 1; //bit10
                size_t RTM : 1; //bit11
                size_t Reserved_2 : 1; //bit12
                size_t GD : 1; //bit13
                size_t Reserved_3 : 2; //bit15:14
                size_t RW_0 : 2; //bit17:16  dr0的读、写、执行控制位
                size_t LEN_0 : 2; //bit19:18 dr0的断点监视的长度
            };
        }Dr7;

        Dr7.flags = lpContext->Dr7;

        int length = 1;  //至少设置1字节
        switch (Dr7.LEN_0)
        {
        case BYTE_1:
        {
            length = 1;
            break;
        }
        case BYTE_2:
        {
            length = 2;
            break;
        }
        case BYTE_4:
        {
            length = 4;
            break;
        }
        case BYTE_8:
        {
            length = 8;
            break;
        }
        }

        char szBuf[MAX_PATH] = { 0 };
        sprintf(szBuf, "[MyDebug] 设置硬件断点  断点长度: %d\n", length);
        OutputDebugStringA(szBuf);

        switch (Dr7.RW_0)
        {
        case WATCH_WRITE:
        {
            AddBreakpoint((PVOID)lpContext->Dr0, VMCALL_WATCH_WRITES, length); //监视写
            break;
        }
        case WATCH_READWRITE:
        {
            AddBreakpoint((PVOID)lpContext->Dr0, VMCALL_WATCH_READS, length); //监视读写
            break;
        }
        default:
        {
            char szBuf[MAX_PATH] = { 0 };
            sprintf(szBuf, "[MyDebug] 未知意图 dr7: %p\n", Dr7.flags);
            OutputDebugStringA(szBuf);
            break;
        }
        //case WATCH_EXECUTION_ONLY:
        //{
        //    AddBreakpoint((PVOID)lpContext->Dr0, VMCALL_WATCH_EXECUTES, length); //监视执行
        //    break;
        //}
        }    
    }
    else
    {
        //不在需要dr0
        RemoveBreakpoint();
    }

    //if (lpContext->Dr1 || lpContext->Dr2 || lpContext->Dr3)
    //{
    //    ReportSeriousError("设置断点过多，目前仅支持一个debugreg断点");
    //}


    //可能是为了清理dr6
    if (lpContext->Dr6 == 0)
    {
        InterlockedExchange(&g_debug_condition_detected, 0);
    }


    //调用原函数
    CONTEXT Context = { 0 };
    Context.ContextFlags = CONTEXT_ALL | CONTEXT_EXTENDED_REGISTERS;
    BOOL boSuccess = Sys_GetThreadContext(hThread, &Context);

    if (boSuccess)
    {
        Context.EFlags = lpContext->EFlags;  //将调试器的设置上去，这样就能保证TF标志位被设置
#ifdef _WIN64
        Context.Rip = lpContext->Rip;  //需要设置rip，这用于调整int3
#else
        Context.Eip = lpContext->Eip;
#endif
        Context.ContextFlags = lpContext->ContextFlags;
        //Context.Dr7 = 0xF0401;          
        Sys_SetThreadContext(hThread, &Context);  //调用系统函数   
    }
    return TRUE;
}

//添加断点
//bool AddBreakpoint(_In_ CONST CONTEXT* lpContext, unsigned __int64 command)
//{
//    bool boSuccess = false;
//
//    // 遍历 DR0 至 DR3 寄存器    
//    BreakpointList.Lock();
//    for (int i = 0; i < 4; ++i)
//    {
//        PVOID setAddress = nullptr;
//
//        switch (i)
//        {
//        case 0:
//            setAddress = (PVOID)lpContext->Dr0;
//            break;
//        case 1:
//            setAddress = (PVOID)lpContext->Dr1;
//            break;
//        case 2:
//            setAddress = (PVOID)lpContext->Dr2;
//            break;
//        case 3:
//            setAddress = (PVOID)lpContext->Dr3;
//            break;
//        default:
//            break;
//        }
//
//        // 检查地址是否有效且不在断点列表中
//        if (setAddress && std::find_if(BreakpointList.begin(), BreakpointList.end(), [setAddress](auto& bp) {
//            return bp.Address == (ULONG64)setAddress;
//        }) == BreakpointList.end()/*遍历到末尾说明不存在*/)
//        {
//            logger.Log("添加断点: %p", setAddress);
//            if (BreakpointList.size() < 4)
//            {
//                // Add the breakpoint
//                BREAKPOINT_RECORD Breakpoint = { 0 };
//                Breakpoint.Address = (ULONG64)setAddress;
//                BreakpointList.push_back(Breakpoint);
//
//                // Apply the breakpoint
//                boSuccess = SetBreakpoint(setAddress, command);
//                if (!boSuccess)
//                {
//                    ReportSeriousError("设置断点失败");
//                }
//            }
//            break; // 每次调用仅添加一个断点
//        }
//    }
//    BreakpointList.UnLock();
//
//    return boSuccess;
//}

bool RemoveBreakpoint()
{
    bool boSuccess = false;

    BreakpointList.Lock();

    //for (const auto& Breakpoint : BreakpointList)  //如果列表为0，它也会自动跳过，不会进for循环里
    //{
    //    if (Breakpoint.Address)
    //    {
    //        VT_BREAK_POINT vmcallinfo = { 0 };
    //        vmcallinfo.cr3 = Breakpoint.cr3;
    //        vmcallinfo.VirtualAddress = Breakpoint.Address;
    //        vmcallinfo.Size = Breakpoint.length;
    //        vmcallinfo.command = VMCALL_WATCH_DELETE;
    //        vmcallinfo.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;
    //        vmcallinfo.watchid = Breakpoint.watchid;

    //        logger.Log("要移除的监视id: %d", Breakpoint.watchid);

    //        SYSTEM_INFO SysInfo = { 0 };
    //        GetSystemInfo(&SysInfo);
    //        vmcallinfo.CPUCount = SysInfo.dwNumberOfProcessors;
    //        boSuccess = current_vmcall(&vmcallinfo);
    //        if (!boSuccess)
    //        {
    //            ReportSeriousError("移除断点失败");
    //        }
    //    }
    //}

    DWORD BytesReturned = 0;

    for (auto& Breakpoint : BreakpointList)
    {
        if (Breakpoint.Address)
        {
            DWORD output = 520;
            if (SendUserDataToDriver(IOCTL_DEL_HARDWARE_BREAKPOINT,
                &Breakpoint,
                sizeof(BREAKPOINT_RECORD),
                &output,
                sizeof(DWORD),
                &BytesReturned))
            {
                if (output == 1998)
                {
                    boSuccess = true;                    
                    logger.Log("移除的监视id: %d", Breakpoint.watchid);
                }
                else
                {
                    ReportSeriousError("移除硬件断点失败");
                }
            }
            else
            {
                logger.Log("IOCTL_DEL_BREAKPOINT 失败!");
            }
        }
    }


    BreakpointList.clear();
    BreakpointList.UnLock();

    return boSuccess;
}

//添加断点
bool AddBreakpoint(PVOID setAddress, unsigned __int64 command, int length)
{
    bool boSuccess = false;  

    // 检查地址是否有效且不在断点列表中
    if (setAddress && std::find_if(BreakpointList.begin(), BreakpointList.end(), [setAddress](auto& bp) {
        return bp.Address == (ULONG64)setAddress;
    }) == BreakpointList.end()/*遍历到末尾说明不存在*/)
    {
        logger.Log("添加断点: %p", setAddress);
        if (BreakpointList.size() < BREAKPOINT_COUNT)
        {
            // Apply the breakpoint
            boSuccess = SetBreakpoint(setAddress, command, length);
            if (!boSuccess)
            {
                ReportSeriousError("设置硬件断点失败");
            }
        }
        else
        {
            ReportSeriousError("设置断点过多，目前仅支持一个debugreg断点");
        }
    }
    else
    {
        char szBuf[MAX_PATH] = { 0 };
        sprintf(szBuf, "[MyDebug] 断点已经存在: %p\n", setAddress);
        OutputDebugStringA(szBuf);
    }

    return boSuccess;
}

bool SetBreakpoint(PVOID lpBaseAddress, unsigned __int64 command, int length)
{
    bool boSuccess = false;
    DWORD BytesReturned = 0;

    if (!g_target_cr3 || !g_target_pid)
    {
        logger.Log("[%s] cr3 或 pid为空", __func__);
        return false;
    }

    SYSTEM_INFO SysInfo = { 0 };
    GetSystemInfo(&SysInfo);
    BREAKPOINT_RECORD Breakpoint = { 0 };
    Breakpoint.Address = (unsigned __int64)lpBaseAddress;
    Breakpoint.length = length;
    Breakpoint.cr3 = g_target_cr3;
    Breakpoint.command = command;
    Breakpoint.CPUCount = SysInfo.dwNumberOfProcessors;
    Breakpoint.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;
    Breakpoint.pid = g_target_pid;
    Breakpoint.watchid = -1;
    BreakpointList.Lock();
    BreakpointList.push_back(Breakpoint);
    BreakpointList.UnLock();

    BREAKPOINT_RECORD output = { 0 };
    if (SendUserDataToDriver(IOCTL_SET_HARDWARE_BREAKPOINT,
        &Breakpoint, 
        sizeof(BREAKPOINT_RECORD),
        &output,
        sizeof(BREAKPOINT_RECORD),
        &BytesReturned))
    {
        logger.Log("返回的监视id: %d", output.watchid);
        if (output.watchid != -1)
        {
            boSuccess = true;
            logger.Log("SetBreakpoint 成功!");            

            for (auto& Breakpoint : BreakpointList)
            {
                if ((Breakpoint.Address == output.Address) &&
                    (Breakpoint.command == output.command))
                {
                    Breakpoint.watchid = output.watchid;
                    break;
                }
            }

        }
        else
        {
            logger.Log("SetBreakpoint 失败!");
        }
    }
    else
    {
        logger.Log("IOCTL_SET_BREAKPOINT 请求失败!");
    }





    //if (!g_target_cr3)
    //{
    //    logger.Log("[%s] g_target_cr3 空指针", __func__);
    //    return false;
    //}
    //VT_BREAK_POINT vmcallinfo = { 0 };
    //vmcallinfo.cr3 = g_target_cr3;
    //vmcallinfo.VirtualAddress = (unsigned __int64)lpBaseAddress;
    //vmcallinfo.Size = length;
    //vmcallinfo.command = command;
    //vmcallinfo.LoopUserMode = (unsigned __int64)DbgUserBreakPoint;

    //SYSTEM_INFO SysInfo = { 0 };
    //GetSystemInfo(&SysInfo);
    //vmcallinfo.CPUCount = SysInfo.dwNumberOfProcessors;
    //boSuccess = current_vmcall(&vmcallinfo);
    //if (boSuccess)
    //{
    //    logger.Log("返回的监视id: %d", vmcallinfo.watchid);
    //    // Add the breakpoint
    //    BREAKPOINT_RECORD Breakpoint = { 0 };
    //    Breakpoint.Address = vmcallinfo.VirtualAddress;
    //    Breakpoint.length = vmcallinfo.Size;
    //    Breakpoint.cr3 = vmcallinfo.cr3;
    //    Breakpoint.watchid = vmcallinfo.watchid;
    //    BreakpointList.Lock();
    //    BreakpointList.push_back(Breakpoint);
    //    BreakpointList.UnLock();
    //}
    //else
    //{
    //    logger.Log("current_vmcall 失败!  errorCode:%d", vmcallinfo.errorCode);
    //}
    return boSuccess;
}

BOOL WINAPI NewGetThreadContext(
    _In_ HANDLE    hThread,
    _Inout_ LPCONTEXT lpContext
)
{
    //调用原函数
    BOOL boSuccess = Sys_GetThreadContext(hThread, lpContext);

    //从我们自己的断点列表给断点
    if (boSuccess)
    {
        int i = 0;

        //logger.Log("lpContext->Dr6: %p", lpContext->Dr6);

        lpContext->Dr0 = 0;
        lpContext->Dr1 = 0;
        lpContext->Dr2 = 0;
        lpContext->Dr3 = 0;
        lpContext->Dr6 = 0;

        BreakpointList.Lock();
        for (const auto& Breakpoint : BreakpointList)  //即使如果列表为0，它也会自动跳过，不会进for循环里
        {
            if (Breakpoint.Address)
            {
                switch (i)
                {
                case 0:
                {
                    //只要有#DB事件过来，我们就这样设置
                    //因为我们已经将目标程序的#DB进行了过滤
                    //只有vt抛的#DB异常会被发送给调试器
                    lpContext->Dr0 = Breakpoint.Address;

                    //判断触发原因
                    if (g_debug_condition_detected == 1)  //debugreg
                    {
                        lpContext->Dr6 = 1 << 0;
                    }
                    else if (g_debug_condition_detected == 2)  //单步执行
                    {
                        lpContext->Dr6 = 1 << 14; //设置bs位
                        logger.Log("lpContext->Dr6: %p", lpContext->Dr6);
                    }
                    break;
                }
                case 1:
                {
                    lpContext->Dr1 = Breakpoint.Address;
                    break;
                }
                case 2:
                {
                    lpContext->Dr2 = Breakpoint.Address;
                    break;
                }
                case 3:
                {
                    lpContext->Dr3 = Breakpoint.Address;
                    break;
                }
                default:
                {
                    ReportSeriousError("遍历断点数量过多！");
                    break;
                }
                }
                i++;
            }
        }
        BreakpointList.UnLock();      

        //if (BreakpointList.size() > 0)
        //{
        //    DWORD dwThreadId = GetThreadId(hThread);
        //    if (dwThreadId == 0)
        //    {
        //        ReportSeriousError("无法获取线程id！");
        //        return boSuccess;
        //    }

        //    BREAKPOINT_DETECTED vmcallinfo = { 0 };
        //    vmcallinfo.command = VMCALL_GET_BREAKPOINT;
        //    vmcallinfo.Cid.UniqueThread = (HANDLE)dwThreadId;

        //    if (vmcall2(&vmcallinfo))
        //    {
        //        logger.Log("breakpoint_detected: %p", vmcallinfo.breakpoint_detected);
        //        if (vmcallinfo.breakpoint_detected == lpContext->Dr0)
        //        {
        //            lpContext->Dr6 = 1 << 0;
        //        }
        //        else if (vmcallinfo.breakpoint_detected == lpContext->Dr1)
        //        {
        //            lpContext->Dr6 = 1 << 1;
        //        }
        //        else if (vmcallinfo.breakpoint_detected == lpContext->Dr2)
        //        {
        //            lpContext->Dr6 = 1 << 2;
        //        }
        //        else if (vmcallinfo.breakpoint_detected == lpContext->Dr3)
        //        {
        //            lpContext->Dr6 = 1 << 3;
        //        }
        //        logger.Log("lpContext->Dr6: %p", lpContext->Dr6);
        //    }
        //}
    }
    return boSuccess;
}


bool __stdcall GetPhysicalAddress(HANDLE hProcess, PVOID lpBaseAddress, ULONG64 Address)
{
    return 0;
}