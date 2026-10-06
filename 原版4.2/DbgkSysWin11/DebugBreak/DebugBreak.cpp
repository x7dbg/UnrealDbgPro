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
#include "../Encrypt/Blowfish/Blowfish.h"
#include "../Hvm/AsmCallset.h"
#include "../Hvm/vmcall_reason.h"
#include "DebugBreak.h"

bool vmcall_internal(PVOID vmcallinfo)
{
    unsigned long ecode = 0;
    bool boSuccess = false;
    __try {
        //若非vmx模式则该指令会触发#UD异常
        boSuccess = __vm_call(((PVMCALLINFO)vmcallinfo)->command, (unsigned __int64)vmcallinfo, 0, 0);
    }
    __except (ecode = GetExceptionCode(), 1) {
        outToFile("执行vmcall时遇到了错误 (error: 0x%X)", ecode);
    }
    return boSuccess;
}

//只会向当前逻辑处理器发送请求
bool current_vmcall(PVOID vmcallinfo)
{
    return vmcall_internal(vmcallinfo);
}

bool AddHardwareBreakpoint(PBREAKPOINT_RECORD Breakpoint)
{
    bool boSuccess = false;
    VT_BREAK_POINT vmcallinfo = { 0 };
    vmcallinfo.cr3 = Breakpoint->cr3;
    vmcallinfo.VirtualAddress = Breakpoint->Address;
    vmcallinfo.Size = Breakpoint->length;
    vmcallinfo.command = Breakpoint->command;
    vmcallinfo.CPUCount = Breakpoint->CPUCount;
    vmcallinfo.LoopUserMode = Breakpoint->LoopUserMode;
    vmcallinfo.watchid = -1;

    KAPC_STATE ApcState;
    PEPROCESS Process = NULL;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)Breakpoint->pid, &Process);
    if (NT_SUCCESS(status))
    {
        __try
        {
            KeStackAttachProcess(Process, &ApcState);
            //outToFile("数值: %X", *(BYTE*)Breakpoint->Address);
            //outToFile("地址: %p", Breakpoint->Address);
            //outToFile("cr3: %p", __readcr3());

            //apex使用了假cr3，通过此解决方案可以获得真实的cr3
            //我们对目标地址读取一个字节的内容，从而使得系统触发#GP异常
            //eac会在异常里接管#GP异常，从而恢复真实的cr3给cr3寄存器
            //然后回到触发#GP异常的指令处，继续执行，故而我们便在此时有机会获得真实的cr3
            *(volatile BYTE*)Breakpoint->Address;  //volatile关键字可以不被Release优化
            _disable(); //关中断
            vmcallinfo.cr3 = __readcr3();
            _enable();  //开中断
            boSuccess = current_vmcall(&vmcallinfo);
            KeUnstackDetachProcess(&ApcState);
        }
        __except (1)
        {
            outToFile("设置硬件断点时崩溃了");
        }

        Breakpoint->watchid = vmcallinfo.watchid;

        if (!boSuccess)
        {
            outLog("current_vmcall 失败!");
        }
        else
        {
            outLog("current_vmcall 成功!  errorCode:%d", vmcallinfo.errorCode);
        }
        ObDereferenceObject(Process);
    }
    return boSuccess;
}

void SetHardwareBreakpoint(IN PUSER_DATA userData, IN PIRP pIrp)
{
    USER_DATA user = GetUserData(userData);

    PBREAKPOINT_RECORD output = (PBREAKPOINT_RECORD)pIrp->AssociatedIrp.SystemBuffer;  //内核的缓冲区，输入输出都用的这个
    RtlZeroMemory(output, sizeof(BREAKPOINT_RECORD));

    //分配明文缓存区
    BYTE* aucPlainText = allocate_pool<BYTE*>(user.uSize);
    DecryptData((PVOID)user.pUserData, aucPlainText);

    // 计算明文缓冲区中的结构体数量
    size_t numElements = user.uSize / sizeof(BREAKPOINT_RECORD);

    // 遍历明文缓冲区中的结构体
    for (size_t i = 0; i < numElements; i++)
    {
        PBREAKPOINT_RECORD pInfo = reinterpret_cast<PBREAKPOINT_RECORD>(aucPlainText + i * sizeof(BREAKPOINT_RECORD));

        if (AddHardwareBreakpoint(pInfo))
        {
            *output = *pInfo;
        }
        break;
    }
    free_pool(aucPlainText);
}

bool DeleteHardwareBreakpoint(PBREAKPOINT_RECORD Breakpoint)
{
    bool boSuccess = false;
    VT_BREAK_POINT vmcallinfo = { 0 };
    vmcallinfo.cr3 = Breakpoint->cr3;
    vmcallinfo.VirtualAddress = Breakpoint->Address;
    vmcallinfo.Size = Breakpoint->length;
    vmcallinfo.command = VMCALL_WATCH_DELETE;
    vmcallinfo.LoopUserMode = Breakpoint->LoopUserMode;
    vmcallinfo.watchid = Breakpoint->watchid;
    vmcallinfo.CPUCount = Breakpoint->CPUCount;

    KAPC_STATE ApcState;
    PEPROCESS Process = NULL;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)Breakpoint->pid, &Process);
    if (NT_SUCCESS(status))
    {
        __try
        {
            KeStackAttachProcess(Process, &ApcState);

            //apex使用了假cr3，通过此解决方案可以获得真实的cr3
            //我们对目标地址读取一个字节的内容，从而使得系统触发#GP异常
            //eac会在异常里接管#GP异常，从而恢复真实的cr3给cr3寄存器
            //然后回到触发#GP异常的指令处，继续执行，故而我们便在此时有机会获得真实的cr3
            *(volatile BYTE*)Breakpoint->Address;  //volatile关键字可以不被Release优化
            _disable(); //关中断
            vmcallinfo.cr3 = __readcr3();
            _enable();  //开中断
            boSuccess = current_vmcall(&vmcallinfo);
            KeUnstackDetachProcess(&ApcState);
        }
        __except (1)
        {
            outToFile("删除硬件断点时崩溃了");
        }
        ObDereferenceObject(Process);
    }
    return boSuccess;
}

//移除硬件断点
void RemoveHardwareBreakpoint(IN PUSER_DATA userData, IN PIRP pIrp)
{
    USER_DATA user = GetUserData(userData);

    DWORD* output = (DWORD*)pIrp->AssociatedIrp.SystemBuffer;  //内核的缓冲区，输入输出都用的这个
    RtlZeroMemory(output, sizeof(DWORD));

    //分配明文缓存区
    BYTE* aucPlainText = allocate_pool<BYTE*>(user.uSize);
    DecryptData((PVOID)user.pUserData, aucPlainText);

    // 计算明文缓冲区中的结构体数量
    size_t numElements = user.uSize / sizeof(BREAKPOINT_RECORD);

    // 遍历明文缓冲区中的结构体
    for (size_t i = 0; i < numElements; i++)
    {
        PBREAKPOINT_RECORD pInfo = reinterpret_cast<PBREAKPOINT_RECORD>(aucPlainText + i * sizeof(BREAKPOINT_RECORD));

        if (DeleteHardwareBreakpoint(pInfo))
        {
            *output = 1998;  //成功
        }
        else
        {
            *output = 520;   //失败
        }
        break;
    }
    free_pool(aucPlainText);
}

//添加软件断点
bool AddSoftwareBreakpoint(PVT_BREAK_POINT vmcallinfo)
{
    bool boSuccess = false;
    KAPC_STATE ApcState;
    PEPROCESS Process = NULL;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)vmcallinfo->pid, &Process);
    if (NT_SUCCESS(status))
    {
        __try
        {
            KeStackAttachProcess(Process, &ApcState);
            *(volatile BYTE*)vmcallinfo->VirtualAddress;  //volatile关键字可以不被Release优化
            _disable(); //关中断
            vmcallinfo->cr3 = __readcr3();
            _enable();  //开中断
            boSuccess = current_vmcall(vmcallinfo);
            KeUnstackDetachProcess(&ApcState);
        }
        __except (1)
        {
            outToFile("设置软件断点时崩溃了");
        }
        ObDereferenceObject(Process);
    }
    return boSuccess;
}

void SetSoftwareBreakpoint(IN PUSER_DATA userData, IN PIRP pIrp)
{
    USER_DATA user = GetUserData(userData);

    PVT_BREAK_POINT output = (PVT_BREAK_POINT)pIrp->AssociatedIrp.SystemBuffer;  //内核的缓冲区，输入输出都用的这个
    RtlZeroMemory(output, sizeof(VT_BREAK_POINT));

    //分配明文缓存区
    BYTE* aucPlainText = allocate_pool<BYTE*>(user.uSize);
    DecryptData((PVOID)user.pUserData, aucPlainText);

    // 计算明文缓冲区中的结构体数量
    size_t numElements = user.uSize / sizeof(VT_BREAK_POINT);

    // 遍历明文缓冲区中的结构体
    for (size_t i = 0; i < numElements; i++)
    {
        PVT_BREAK_POINT pInfo = reinterpret_cast<PVT_BREAK_POINT>(aucPlainText + i * sizeof(VT_BREAK_POINT));

        if (AddSoftwareBreakpoint(pInfo))
        {
            *output = *pInfo;
        }
        break;
    }
    free_pool(aucPlainText);
}

bool DeleteSoftwareBreakpoint(PVT_BREAK_POINT vmcallinfo)
{
    bool boSuccess = false;
    KAPC_STATE ApcState;
    PEPROCESS Process = NULL;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)vmcallinfo->pid, &Process);
    if (NT_SUCCESS(status))
    {
        __try
        {
            KeStackAttachProcess(Process, &ApcState);
            *(volatile BYTE*)vmcallinfo->VirtualAddress;  //volatile关键字可以不被Release优化
            _disable(); //关中断
            vmcallinfo->cr3 = __readcr3();
            _enable();  //开中断
            boSuccess = current_vmcall(vmcallinfo);
            KeUnstackDetachProcess(&ApcState);
        }
        __except (1)
        {
            outToFile("删除软件断点时崩溃了");
        }
        ObDereferenceObject(Process);
    }
    return boSuccess;
}

//移除软件断点
void RemoveSoftwareBreakpoint(IN PUSER_DATA userData, IN PIRP pIrp)
{
    USER_DATA user = GetUserData(userData);

    DWORD* output = (DWORD*)pIrp->AssociatedIrp.SystemBuffer;  //内核的缓冲区，输入输出都用的这个
    RtlZeroMemory(output, sizeof(DWORD));

    //分配明文缓存区
    BYTE* aucPlainText = allocate_pool<BYTE*>(user.uSize);
    DecryptData((PVOID)user.pUserData, aucPlainText);

    // 计算明文缓冲区中的结构体数量
    size_t numElements = user.uSize / sizeof(VT_BREAK_POINT);

    // 遍历明文缓冲区中的结构体
    for (size_t i = 0; i < numElements; i++)
    {
        PVT_BREAK_POINT pInfo = reinterpret_cast<PVT_BREAK_POINT>(aucPlainText + i * sizeof(VT_BREAK_POINT));

        if (DeleteSoftwareBreakpoint(pInfo))
        {
            *output = 1998;  //成功
        }
        else
        {
            *output = 520;   //失败
        }
        break;
    }
    free_pool(aucPlainText);
}

bool GetSoftwareBreakpoint(PVT_BREAK_POINT vmcallinfo)
{
    bool boSuccess = false;
    KAPC_STATE ApcState;
    PEPROCESS Process = NULL;
    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)vmcallinfo->pid, &Process);
    if (NT_SUCCESS(status))
    {
        __try
        {
            KeStackAttachProcess(Process, &ApcState);
            *(volatile BYTE*)vmcallinfo->VirtualAddress;  //volatile关键字可以不被Release优化
            _disable(); //关中断
            vmcallinfo->cr3 = __readcr3();
            _enable();  //开中断
            boSuccess = current_vmcall(vmcallinfo);
            KeUnstackDetachProcess(&ApcState);
        }
        __except (1)
        {
            outToFile("读软件断点时崩溃了");
        }
        ObDereferenceObject(Process);
    }
    if (boSuccess)
    {
        vmcallinfo->errorCode = 1998;
    }
    return boSuccess;
}

//读int3断点
void ReadSoftwareBreakpoint(IN PUSER_DATA userData, IN PIRP pIrp)
{
    USER_DATA user = GetUserData(userData);

    PVT_BREAK_POINT output = (PVT_BREAK_POINT)pIrp->AssociatedIrp.SystemBuffer;  //内核的缓冲区，输入输出都用的这个
    RtlZeroMemory(output, sizeof(VT_BREAK_POINT));

    //分配明文缓存区
    BYTE* aucPlainText = allocate_pool<BYTE*>(user.uSize);
    DecryptData((PVOID)user.pUserData, aucPlainText);

    // 计算明文缓冲区中的结构体数量
    size_t numElements = user.uSize / sizeof(VT_BREAK_POINT);

    // 遍历明文缓冲区中的结构体
    for (size_t i = 0; i < numElements; i++)
    {
        PVT_BREAK_POINT pInfo = reinterpret_cast<PVT_BREAK_POINT>(aucPlainText + i * sizeof(VT_BREAK_POINT));

        if (GetSoftwareBreakpoint(pInfo))
        {
            *output = *pInfo;  //成功
        }
        break;
    }
    free_pool(aucPlainText);
}