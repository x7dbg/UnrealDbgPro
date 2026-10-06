#include "../dllmain.h"
#include "../Globals.h"
#include "../Log/Log.h"
#include "vmx.h"


//bool __vm_call(unsigned __int64 vmcall_reason, unsigned __int64 rdx, unsigned __int64 r8, unsigned __int64 r9)
//{
//    __asm
//    {
//        vmcall
//    }
//}

//将线程运行在指定cpu核上
//CPU核数从0开始
void RunOnCPU(HANDLE hThread, int CpuNo)
{
    try
    {
        if (SetProcessAffinityMask(GetCurrentProcess(), 1 << CpuNo))
        {
            DWORD_PTR previous_mask = SetThreadAffinityMask(hThread, 1 << CpuNo);
            if (!previous_mask)
            {
                throw std::runtime_error("设置线程亲和性失败");
            }
        }
        else
        {
            throw std::runtime_error("设置进程亲和性失败");
        }
    }
    catch (const std::exception& e)
    {
        ReportSeriousError(e.what());
    }
}

bool vmcall_internal(PVOID vmcallinfo)
{
    unsigned long ecode = 0;
    bool boSuccess = false;
    __try {
        //若非vmx模式则该指令会触发#UD异常
#ifdef _WIN64
        boSuccess = __vm_call(((PVMCALLINFO)vmcallinfo)->command, (unsigned __int64)vmcallinfo, 0, 0);
#else
        boSuccess = __vm_call(((PVMCALLINFO)vmcallinfo)->command, vmcallinfo);
#endif // _WIN64

    }
    __except (ecode = GetExceptionCode(), 1) {
        logger.Log("执行vmcall时遇到了错误 (error: 0x%x)", ecode);
    }
    return boSuccess;
}

//会广播给所有逻辑处理器
bool vmcall(PVOID vmcallinfo)
{
    bool boSuccess = false;
    LONG status = 0;
    SYSTEM_INFO SysInfo = { 0 };
    GetSystemInfo(&SysInfo);
    for (int i = 0; i < SysInfo.dwNumberOfProcessors; i++)
    {
        //将当前线程运行在指定的处理器上
        RunOnCPU(GetCurrentThread(), i);
        if (vmcall_internal(vmcallinfo))
        {            
            InterlockedIncrement(&status);
        }
    }
    return status == SysInfo.dwNumberOfProcessors;
}

//遍历逻辑处理器找到返回事件成功的那个
bool vmcall2(PVOID vmcallinfo)
{
    bool boSuccess = false;
    SYSTEM_INFO SysInfo = { 0 };
    GetSystemInfo(&SysInfo);
    for (int i = 0; i < SysInfo.dwNumberOfProcessors; i++)
    {
        //将当前线程运行在指定的处理器上
        RunOnCPU(GetCurrentThread(), i);
        if (vmcall_internal(vmcallinfo))
        {
            boSuccess = true;
            break;
        }
    }
    return boSuccess;
}


//只会向当前逻辑处理器发送请求
bool current_vmcall(PVOID vmcallinfo)
{
    return vmcall_internal(vmcallinfo);
}